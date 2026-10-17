"""Native/Python TLS interoperability and fail-closed tests; loopback only."""
import hashlib
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
import unittest
from unittest.mock import patch

OPENSSL = sys.argv.pop(2)
import integration as support

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from create_tls_identity import create_identity


class TlsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="dsr-tls-tests-")
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
        return ["--tls-pfx", str(self.pfx)]

    def setUp(self):
        with patch.dict(os.environ, DSR_TLS_PFX_PASSWORD=self.pfx_password):
            support.NetworkTests.setUp(self)
        self.assertEqual(self.lines.get(timeout=3), "TLS_PIN\t" + self.pin)

    tearDown = support.NetworkTests.tearDown

    def peer(self, name=None):
        p = support.Peer(self.port, self.context)
        self.clients.append(p)
        if name is not None:
            p.login(self.key, name)
        return p

    def test_01_python_tls_clients_sync_entities(self):
        clients = [self.peer("tls" + str(i)) for i in range(4)]
        clients[0].send("MOVE\t10\t20\t30")
        for p in clients:
            self.assertEqual(p.until("POSITION").split("\t")[1:5], [str(clients[0].id), "10", "20", "30"])
        clients[1].send("QUIT")
        self.assertEqual(clients[1].until("BYE"), "BYE")
        clients[0].until("DESPAWN")

    def test_02_native_tls_client_pin_and_commands(self):
        result = subprocess.run(
            [support.EXE, "join", "--port", str(self.port), "--tls-pin", self.pin],
            input="/move 3 4 5\n/players\n/quit\n", env=self.env,
            capture_output=True, text=True, timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("WELCOME\t3\t", result.stdout)
        self.assertIn("POSITION\t1\t3\t4\t5\t2", result.stdout)
        self.assertIn("BYE", result.stdout)
        self.assertNotIn(self.key, result.stdout + result.stderr)

    def test_03_wrong_password_is_rejected_inside_tls(self):
        p = self.peer()
        p.send("HELLO\t3\twrong\twrong-password\t" + support.BUILD_ID)
        self.assertEqual(p.line(), "ERROR\tAUTH")

    def test_04_build_mismatch_does_not_admit_player(self):
        p = self.peer()
        p.send("HELLO\t3\twrong\t" + self.key + "\tdifferent-build")
        self.assertEqual(p.line(), "ERROR\tBUILD")
        valid = self.peer("valid")
        self.assertEqual(len(valid.initial_players), 1)

    def test_05_plaintext_and_malformed_tls_do_not_crash_host(self):
        for record in (b"HELLO\t3\tnot-tls\n", b"\x16\x03\x03\x00\x04oops"):
            with socket.create_connection(("127.0.0.1", self.port), timeout=4) as sock:
                sock.sendall(record)
                try:
                    self.assertEqual(sock.recv(4096), b"")
                except ConnectionResetError:
                    pass
        self.peer("valid")

    def test_06_native_client_wrong_pin_sends_no_application_data(self):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(str(self.cert), str(self.cert.with_name("host.key.pem")), self.pfx_password)
        observed = queue.Queue()
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(5)

            def rogue_host():
                try:
                    conn, _ = listener.accept()
                    with conn:
                        conn.settimeout(5)
                        with context.wrap_socket(conn, server_side=True) as tls:
                            try:
                                observed.put(tls.recv(4096))
                            except (ssl.SSLEOFError, ConnectionResetError):
                                observed.put(b"")
                except Exception as error:
                    observed.put(error)

            thread = threading.Thread(target=rogue_host, daemon=True)
            thread.start()
            result = subprocess.run(
                [support.EXE, "join", "--port", str(listener.getsockname()[1]), "--tls-pin", "0" * 64],
                input="", env=self.env, capture_output=True, text=True, timeout=10,
            )
            thread.join(timeout=6)
            self.assertFalse(thread.is_alive())
            self.assertEqual(observed.get(timeout=1), b"")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("impressao digital incorreta", result.stderr)
            self.assertNotIn("WELCOME", result.stdout)

    def test_07_external_plaintext_and_old_insecure_flag_rejected(self):
        for args in (
            ["host", "--address", "0.0.0.0", "--lan"],
            ["join", "--address", "192.0.2.1", "--lan"],
            ["host", "--allow-insecure-lan"],
            ["join", "--tls-pin", "bad-pin"],
        ):
            result = subprocess.run([support.EXE] + args, env=self.env, capture_output=True, text=True, timeout=3)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("SERVER_READY", result.stdout)

    def test_08_bad_pfx_password_prevents_listening(self):
        env = dict(self.env, DSR_TLS_PFX_PASSWORD="incorrect-pfx-password")
        result = subprocess.run(
            [support.EXE, "host", "--port", "0", "--tls-pfx", str(self.pfx)],
            env=env, capture_output=True, text=True, timeout=5,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("SERVER_READY", result.stdout)

    def test_09_certificate_pin_matches_der_sha256(self):
        p = self.peer("hash")
        self.assertEqual(hashlib.sha256(p.sock.getpeercert(binary_form=True)).hexdigest(), self.pin)
        self.assertEqual(p.sock.version(), "TLSv1.2")

    def test_10_altered_encrypted_record_is_rejected(self):
        incoming, outgoing = ssl.MemoryBIO(), ssl.MemoryBIO()
        client = self.context.wrap_bio(incoming, outgoing, server_side=False, server_hostname="127.0.0.1")
        with socket.create_connection(("127.0.0.1", self.port), timeout=4) as raw:
            for _ in range(32):
                try:
                    client.do_handshake()
                    break
                except ssl.SSLWantReadError:
                    pending = outgoing.read()
                    if pending:
                        raw.sendall(pending)
                    received = raw.recv(16384)
                    self.assertTrue(received)
                    incoming.write(received)
            else:
                self.fail("Handshake TLS nao completou")
            pending = outgoing.read()
            if pending:
                raw.sendall(pending)
            hello = ("HELLO\t3\ttampered\t" + self.key + "\t" + support.BUILD_ID + "\n").encode("ascii")
            client.write(hello)
            encrypted = bytearray(outgoing.read())
            self.assertTrue(encrypted)
            self.assertNotIn(self.key.encode("ascii"), encrypted)
            encrypted[-1] ^= 1
            raw.sendall(encrypted)
            try:
                self.assertEqual(raw.recv(16384), b"")
            except ConnectionResetError:
                pass
        valid = self.peer("valid")
        self.assertEqual(valid.id, 1)
        self.assertEqual(len(valid.initial_players), 1)

    def test_11_wire_capture_contains_no_cleartext_credentials_or_moves(self):
        captured = bytearray()
        failures = queue.Queue()
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(5)

            def proxy():
                try:
                    front, _ = listener.accept()
                    with front, socket.create_connection(("127.0.0.1", self.port), timeout=5) as back:
                        front.settimeout(5)

                        def relay(source, target, observe):
                            try:
                                while True:
                                    data = source.recv(16384)
                                    if not data:
                                        break
                                    if observe:
                                        captured.extend(data)
                                    target.sendall(data)
                            except (OSError, TimeoutError):
                                pass
                            finally:
                                try:
                                    target.shutdown(socket.SHUT_WR)
                                except OSError:
                                    pass

                        worker = threading.Thread(target=relay, args=(front, back, True), daemon=True)
                        worker.start()
                        relay(back, front, False)
                        worker.join(timeout=6)
                        if worker.is_alive():
                            failures.put("relay nao encerrou")
                except Exception as error:
                    failures.put(error)

            thread = threading.Thread(target=proxy, daemon=True)
            thread.start()
            result = subprocess.run(
                [support.EXE, "join", "--port", str(listener.getsockname()[1]), "--tls-pin", self.pin],
                input="/move 12345 6789 -5\n/quit\n", env=self.env,
                capture_output=True, text=True, timeout=10,
            )
            thread.join(timeout=7)
            self.assertFalse(thread.is_alive())
            self.assertTrue(failures.empty())
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("POSITION", result.stdout)
            self.assertTrue(captured)
            for cleartext in (self.key.encode("ascii"), b"HELLO\t3", b"MOVE\t12345"):
                self.assertNotIn(cleartext, captured)

    def test_12_incomplete_tls_handshake_is_closed(self):
        with socket.create_connection(("127.0.0.1", self.port), timeout=4) as raw:
            raw.settimeout(9)
            self.assertEqual(raw.recv(4096), b"")
        self.peer("valid")


if __name__ == "__main__":
    unittest.main(verbosity=2)
