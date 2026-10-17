"""Loopback-only integration tests. Never starts or inspects the game."""
import os
import queue
import secrets
import socket
import subprocess
import sys
import threading
import unittest
from pathlib import Path

EXE = str(Path(sys.argv.pop(1)).resolve())
PROTOCOL = "3"
BUILD_ID = "dsr-network-lab-0.4"


def collect_lines(stream, target):
    for line in stream:
        target.put(line.rstrip("\r\n"))


class Peer:
    def __init__(self, port, tls_context=None):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        if tls_context is not None:
            self.sock = tls_context.wrap_socket(self.sock, server_hostname="127.0.0.1")
        self.sock.settimeout(3)
        self.buffer = b""
        self.id = None

    def close(self):
        self.sock.close()

    def send(self, text):
        self.sock.sendall((text + "\n").encode("ascii"))

    def line(self):
        while b"\n" not in self.buffer:
            data = self.sock.recv(4096)
            if not data:
                raise EOFError("Host encerrou a conexao")
            self.buffer += data
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line.decode("ascii")

    def until(self, prefix):
        for _ in range(256):
            line = self.line()
            if line == prefix or line.startswith(prefix + "\t"):
                return line
        raise AssertionError("Evento esperado nao recebido: " + prefix)

    def login(self, key, name="player"):
        self.send("HELLO\t" + PROTOCOL + "\t" + name + "\t" + key + "\t" + BUILD_ID)
        welcome = self.line().split("\t")
        assert welcome[0:2] == ["WELCOME", PROTOCOL], welcome
        self.id = int(welcome[3])
        self.until("END")
        self.initial_players, self.initial_revision = self.snapshot()
        return welcome

    def snapshot(self):
        header = self.until("PLAYERS").split("\t")
        revision, count = int(header[1]), int(header[2])
        players = {}
        for _ in range(count):
            f = self.line().split("\t")
            assert len(f) == 7 and f[0] == "PLAYER", f
            players[int(f[1])] = (f[2], tuple(float(n) for n in f[3:6]), int(f[6]))
        assert self.line() == f"PLAYERS_END\t{revision}"
        assert len(players) == count
        return players, revision


class NetworkTests(unittest.TestCase):
    def server_args(self):
        return []

    def setUp(self):
        self.key = secrets.token_hex(16)
        self.env = dict(os.environ, DSR_ROOM_PASSWORD=self.key)
        self.clients = []
        self.server = subprocess.Popen(
            [EXE, "host", "--port", "0", "--max-players", "4"] + self.server_args(),
            env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=collect_lines, args=(self.server.stdout, self.lines), daemon=True)
        self.reader.start()
        try:
            ready = self.lines.get(timeout=5).split("\t")
            self.assertEqual(ready[0], "SERVER_READY")
            self.assertEqual(ready[1], "127.0.0.1")
            self.port = int(ready[2])
            self.assertEqual(len(ready[3]), 32)
        except Exception:
            details = self.server.stderr.read() if self.server.poll() is not None else "host ainda ativo"
            self.tearDown()
            raise RuntimeError("Host nao iniciou: " + details)

    def tearDown(self):
        for peer in self.clients:
            peer.close()
        if self.server.poll() is None:
            self.server.terminate()
        try:
            self.server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.server.kill()
            self.server.wait(timeout=3)
        self.reader.join(timeout=2)
        self.server.stdout.close()
        self.server.stderr.close()

    def peer(self, name=None):
        p = Peer(self.port)
        self.clients.append(p)
        if name is not None:
            p.login(self.key, name)
        return p

    def test_01_password_rejected_and_server_remains_available(self):
        p = self.peer()
        p.send("HELLO\t3\tintruder\twrong-password\t" + BUILD_ID)
        self.assertEqual(p.line(), "ERROR\tAUTH")
        with self.assertRaises(EOFError):
            p.line()
        self.peer("valid")

    def test_02_version_and_admission_required(self):
        p = self.peer()
        p.send("HELLO\t1\told\t" + self.key)
        self.assertEqual(p.line(), "ERROR\tVERSION")
        q = self.peer()
        q.send("SAY\tnot-admitted")
        self.assertEqual(q.line(), "ERROR\tEXPECTED_HELLO")

    def test_03_four_clients_chat_and_roster(self):
        members = [self.peer("p" + str(i)) for i in range(4)]
        self.assertEqual(len({p.id for p in members}), 4)
        members[0].send("SAY\thello-everyone")
        for p in members:
            self.assertEqual(p.until("CHAT"), f"CHAT\t{members[0].id}\thello-everyone")
        members[3].send("WHO")
        self.assertEqual(members[3].line(), "ROSTER\t4")
        roster = [members[3].line() for _ in members]
        self.assertEqual(set(roster), {f"MEMBER\t{p.id}\tp{i}" for i, p in enumerate(members)})
        self.assertEqual(members[3].line(), "END")

    def test_04_capacity_and_slot_reuse_without_id_reuse(self):
        members = [self.peer("p" + str(i)) for i in range(4)]
        extra = self.peer()
        extra.send("HELLO\t3\textra\t" + self.key + "\t" + BUILD_ID)
        self.assertEqual(extra.line(), "ERROR\tFULL")
        old_id = members[0].id
        members[0].send("QUIT")
        self.assertEqual(members[0].until("BYE"), "BYE")
        self.assertEqual(members[1].until("LEFT"), f"LEFT\t{old_id}")
        newcomer = self.peer("replacement")
        self.assertGreater(newcomer.id, max(p.id for p in members))

    def test_05_guest_to_guest_is_relayed_with_host_assigned_sender(self):
        a, b, observer = self.peer("a"), self.peer("b"), self.peer("observer")
        a.send(f"SEND\t{b.id}\tdirected-payload")
        self.assertEqual(b.until("DATA"), f"DATA\t{a.id}\tdirected-payload")
        # PONG is a stream barrier: no DATA should precede it for observer.
        observer.send("PING")
        seen = []
        while True:
            line = observer.line()
            if line == "PONG":
                break
            seen.append(line)
        self.assertFalse(any(line.startswith("DATA\t") for line in seen))
        a.send("SEND\t99999\tunknown")
        self.assertEqual(a.until("ERROR"), "ERROR\tNO_SUCH_PEER")
        a.send("DATA\t777\tforged-origin")
        self.assertEqual(a.line(), "ERROR\tBAD_COMMAND")
        b.send("PING")
        self.assertEqual(b.until("PONG"), "PONG")

    def test_06_fragmented_and_coalesced_tcp_frames(self):
        p = self.peer()
        hello = ("HELLO\t3\tfragmented\t" + self.key + "\t" + BUILD_ID + "\n").encode("ascii")
        for i in range(0, len(hello), 3):
            p.sock.sendall(hello[i:i+3])
        self.assertTrue(p.line().startswith("WELCOME\t3\t"))
        p.until("END")
        p.snapshot()
        p.sock.sendall(b"PING\nPING\nWHO\n")
        self.assertEqual(p.line(), "PONG")
        self.assertEqual(p.line(), "PONG")
        self.assertEqual(p.line(), "ROSTER\t1")
        p.until("END")

    def test_07_limits_and_invalid_controls(self):
        p = self.peer()
        p.sock.sendall(b"x" * 2049)
        self.assertEqual(p.line(), "ERROR\tFRAME_TOO_LARGE")
        q = self.peer("controls")
        q.sock.sendall(b"SAY\ttext\x00hidden\n")
        self.assertEqual(q.line(), "ERROR\tBAD_FRAME")
        r = self.peer("longtext")
        r.send("SAY\t" + "z" * 1025)
        self.assertEqual(r.line(), "ERROR\tBAD_COMMAND")

    def test_08_handshake_timeout(self):
        p = self.peer()
        p.sock.settimeout(7)
        self.assertEqual(p.line(), "ERROR\tHANDSHAKE_TIMEOUT")

    def test_09_abrupt_disconnect_announced(self):
        a, b = self.peer("a"), self.peer("b")
        a.close()
        self.assertEqual(b.until("LEFT"), f"LEFT\t{a.id}")
        self.assertTrue(b.line().startswith(f"DESPAWN\t{a.id}\t"))
        b.send("WHO")
        self.assertEqual(b.line(), "ROSTER\t1")

    def test_10_client_executable_and_no_password_in_output(self):
        result = subprocess.run(
            [EXE, "join", "--port", str(self.port), "--name", "console"],
            input="/who\nhello-from-cli\n/quit\n", env=self.env,
            capture_output=True, text=True, timeout=8,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("WELCOME\t3\t", result.stdout)
        self.assertIn("hello-from-cli", result.stdout)
        self.assertIn("BYE", result.stdout)
        self.assertNotIn(self.key, result.stdout + result.stderr)

    def test_11_fail_closed_config(self):
        for args, env in [
            (["host"], dict(os.environ, DSR_ROOM_PASSWORD="")),
            (["host", "--address", "0.0.0.0"], self.env),
            (["host", "--port", "65536"], self.env),
            (["host", "--max-players", "33"], self.env),
            (["join", "--name", "bad name"], self.env),
        ]:
            result = subprocess.run([EXE] + args, env=env, capture_output=True, text=True, timeout=3)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("SERVER_READY", result.stdout)
            self.assertNotIn(self.key, result.stdout + result.stderr)

    def test_12_rate_limit(self):
        p = self.peer("rate")
        p.sock.sendall(b"PING\n" * 110)
        self.assertEqual(p.until("ERROR"), "ERROR\tRATE_LIMIT")

    def test_13_client_exits_when_host_disappears_even_with_stdin_open(self):
        proc = subprocess.Popen(
            [EXE, "join", "--port", str(self.port), "--name", "waiting"],
            env=self.env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True,
        )
        output = queue.Queue()
        reader = threading.Thread(target=collect_lines, args=(proc.stdout, output), daemon=True)
        reader.start()
        try:
            for _ in range(16):
                if output.get(timeout=4).startswith("WELCOME\t"):
                    break
            else:
                self.fail("Cliente nao admitido")
            self.server.terminate()
            self.server.wait(timeout=3)
            proc.wait(timeout=4)
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.wait(timeout=3)
            reader.join(timeout=2)
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()

    def test_14_client_reports_wrong_password(self):
        result = subprocess.run(
            [EXE, "join", "--port", str(self.port)],
            input="", env=dict(self.env, DSR_ROOM_PASSWORD="invalid-password"),
            capture_output=True, text=True, timeout=5,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ERROR\tAUTH", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
