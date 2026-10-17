"""Protected UDP opt-in integration. Temporary identities and loopback sockets only."""
import os
from pathlib import Path
import queue
import secrets
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

OPENSSL = sys.argv.pop(2)
import integration as support

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from create_tls_identity import create_identity


class NativePeer:
    def __init__(self, port, pin, env, name):
        self.process = subprocess.Popen(
            [support.EXE, "join", "--udp", "--port", str(port), "--tls-pin", pin, "--name", name],
            env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, bufsize=1,
        )
        self.lines = queue.Queue()
        self.history = []
        self.reader = threading.Thread(target=support.collect_lines, args=(self.process.stdout, self.lines), daemon=True)
        self.reader.start()

    def until(self, prefix, timeout=12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                line = self.lines.get(timeout=0.2)
            except queue.Empty:
                if self.process.poll() is not None:
                    raise AssertionError(f"Cliente saiu: {self.history!r}; {self.process.stderr.read()}")
                continue
            self.history.append(line)
            if line == prefix or line.startswith(prefix + "\t"):
                return line
        raise AssertionError(f"Evento ausente {prefix}: {self.history!r}")

    def command(self, text):
        self.process.stdin.write(text + "\n")
        self.process.stdin.flush()

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=3)
        self.reader.join(timeout=2)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()


class UdpTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="dsr-udp-tests-")
        cls.pfx_password = secrets.token_hex(16)
        try:
            with patch.dict(os.environ, DSR_TLS_PFX_PASSWORD=cls.pfx_password):
                cls.pfx, cls.cert, cls.pin = create_identity(Path(cls.temp.name) / "identity", OPENSSL)
            cls.context = ssl.create_default_context(cafile=str(cls.cert))
            cls.context.minimum_version = ssl.TLSVersion.TLSv1_2
            cls.context.maximum_version = ssl.TLSVersion.TLSv1_2
        except Exception:
            cls.temp.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def server_args(self):
        return ["--tls-pfx", str(self.pfx), "--udp"]

    def setUp(self):
        self.native = []
        with patch.dict(os.environ, DSR_TLS_PFX_PASSWORD=self.pfx_password):
            support.NetworkTests.setUp(self)
        self.assertEqual(self.lines.get(timeout=3), "TLS_PIN\t" + self.pin)

    def tearDown(self):
        for p in self.native:
            p.close()
        support.NetworkTests.tearDown(self)

    def peer(self, name):
        p = support.Peer(self.port, self.context)
        self.clients.append(p)
        p.login(self.key, name)
        return p

    def native_peer(self, name):
        p = NativePeer(self.port, self.pin, self.env, name)
        self.native.append(p)
        p.id = int(p.until("WELCOME").split("\t")[3])
        p.until("UDP_ACTIVE")
        return p

    def test_01_native_udp_move_and_tcp_snapshot(self):
        p = self.native_peer("udp")
        p.command("/move 10.125 -2 5")
        line = p.until("UDP_POSITION").split("\t")
        self.assertEqual(line[1:5], [str(p.id), "10.125", "-2", "5"])
        p.command("/players")
        self.assertIn("\t10.125\t-2\t5\t", p.until("PLAYER"))
        p.command("/quit")
        p.until("BYE")
        self.assertEqual(p.process.wait(timeout=4), 0)
        self.assertFalse(any(line.startswith("POSITION\t") for line in p.history), p.history)

    def test_02_three_udp_clients_and_legacy_tcp(self):
        legacy = self.peer("legacy")
        clients = [self.native_peer("udp" + str(i)) for i in range(3)]
        clients[0].command("/move 7 8 9")
        for p in clients:
            deadline = time.monotonic() + 8
            while True:
                line = p.until("UDP_POSITION", max(0.1, deadline - time.monotonic())).split("\t")
                if line[1] == str(clients[0].id):
                    self.assertEqual(line[2:5], ["7", "8", "9"])
                    break
        self.assertEqual(legacy.until("POSITION").split("\t")[1:5], [str(clients[0].id), "7", "8", "9"])
        legacy.send("MOVE\t11\t12\t13")
        for p in clients:
            p.until("UDP_POSITION\t" + str(legacy.id))

    def test_03_reconnect_new_entity_and_connection(self):
        first = self.native_peer("returning")
        old_offer = next(line for line in first.history if line.startswith("UDP_OFFER\t"))
        first.command("/quit"); first.until("BYE"); first.process.wait(timeout=4)
        second = self.native_peer("returning")
        new_offer = next(line for line in second.history if line.startswith("UDP_OFFER\t"))
        self.assertNotEqual(first.id, second.id)
        self.assertNotEqual(old_offer.split("\t")[3], new_offer.split("\t")[3])
        second.command("/move 3 2 1")
        self.assertEqual(second.until("UDP_POSITION").split("\t")[2:5], ["3", "2", "1"])

    def test_04_udp_requires_tls_before_opening_room(self):
        for args in (["host", "--udp", "--port", "0"], ["join", "--udp"]):
            result = subprocess.run([support.EXE] + args, env=self.env, capture_output=True, text=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("exige TCP/TLS", result.stderr)
            self.assertNotIn("SERVER_READY", result.stdout)

    def test_05_duplicate_negotiation_and_tcp_move_refused(self):
        p = self.peer("request")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            request = "UDP_REQUEST\t1\t" + str(udp.getsockname()[1])
            p.send(request)
            self.assertEqual(p.until("UDP_OFFER").split("\t")[1], "1")
            p.send("MOVE\t1\t2\t3")
            self.assertEqual(p.until("ERROR"), "ERROR\tUSE_UDP")
            p.send(request)
            self.assertEqual(p.until("ERROR"), "ERROR\tUDP_NEGOTIATION")

    def test_06_plaintext_udp_does_not_move_entity(self):
        p = self.peer("unprotected")
        observer = self.peer("observer")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            p.send("UDP_REQUEST\t1\t" + str(udp.getsockname()[1]))
            offer = p.until("UDP_OFFER").split("\t")
            destination = ("127.0.0.1", int(offer[4]))
            udp.sendto(b"MOVE\t90\t80\t70\n", destination)
            import struct
            payload = struct.pack("!4sBBHHH16s16sQIiii", b"DSRM", 1, 1, 0, 68, 0,
                                  bytes.fromhex(offer[2]), bytes.fromhex(offer[3]), 1, p.id, 90000, 80000, 70000)
            udp.sendto(payload, destination)
            observer.send("PLAYERS")
            players, _ = observer.snapshot()
            self.assertEqual(players[p.id][1], (0, 0, 0))

    def test_07_host_termination_disconnects_udp_client(self):
        p = self.native_peer("disconnect")
        self.server.terminate(); self.server.wait(timeout=3)
        p.until("DISCONNECTED")
        p.process.wait(timeout=4)

    def test_08_handshake_timeout_does_not_block_other_peers(self):
        p = self.peer("no-handshake")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind(("127.0.0.1", 0))
            p.send("UDP_REQUEST\t1\t" + str(udp.getsockname()[1]))
            p.until("UDP_OFFER")
            p.sock.settimeout(10)
            self.assertEqual(p.until("ERROR"), "ERROR\tUDP_CHANNEL")
        self.native_peer("survivor")

    def test_09_four_udp_clients_receive_latest_position(self):
        clients = [self.native_peer("four" + str(i)) for i in range(4)]
        clients[3].command("/move -10 20 -30")
        for p in clients:
            line = p.until("UDP_POSITION\t" + str(clients[3].id)).split("\t")
            self.assertEqual(line[2:5], ["-10", "20", "-30"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
