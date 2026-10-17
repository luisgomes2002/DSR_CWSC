"""Entity synchronization tests, all on loopback and without the game."""
import subprocess
import queue
import threading
import unittest

import integration as support


class EntityTests(unittest.TestCase):
    setUp = support.NetworkTests.setUp
    tearDown = support.NetworkTests.tearDown
    peer = support.NetworkTests.peer
    server_args = support.NetworkTests.server_args

    def test_01_spawn_at_origin_and_snapshot_includes_self(self):
        a = self.peer("a")
        self.assertEqual(a.initial_players, {a.id: ("a", (0.0, 0.0, 0.0), 1)})
        b = self.peer("b")
        self.assertEqual(set(b.initial_players), {a.id, b.id})
        self.assertEqual(a.until("SPAWN"), f"SPAWN\t{b.id}\tb\t0\t0\t0\t2")

    def test_02_move_is_sent_to_every_client_including_sender(self):
        a, b, c = self.peer("a"), self.peer("b"), self.peer("c")
        a.send("MOVE\t10.5\t-2\t5")
        for p in (a, b, c):
            self.assertEqual(p.until("POSITION"), f"POSITION\t{a.id}\t10.5\t-2\t5\t4")
            p.send("PLAYERS")
            state, revision = p.snapshot()
            self.assertEqual(revision, 4)
            self.assertEqual(state[a.id], ("a", (10.5, -2.0, 5.0), 4))

    def test_03_late_join_receives_current_positions(self):
        a = self.peer("a")
        a.send("MOVE\t25\t0\t-8")
        a.until("POSITION")
        b = self.peer("b")
        self.assertEqual(b.initial_players[a.id], ("a", (25.0, 0.0, -8.0), 2))
        self.assertEqual(b.initial_revision, 3)

    def test_04_invalid_positions_preserve_previous_state_and_connection(self):
        p = self.peer("valid")
        p.send("MOVE\t1\t2\t3")
        p.until("POSITION")
        for value in ("nan", "inf", "-inf", "100001", "-100001", "1e999", "1,5", "1junk", "", " 1"):
            with self.subTest(value=value):
                p.send("MOVE\t" + value + "\t0\t0")
                self.assertEqual(p.line(), "ERROR\tBAD_POSITION")
                p.send("PLAYERS")
                state, revision = p.snapshot()
                self.assertEqual(state[p.id][1], (1.0, 2.0, 3.0))
                self.assertEqual(revision, 2)
        p.send("PING")
        self.assertEqual(p.line(), "PONG")

    def test_05_client_cannot_supply_another_players_id(self):
        a, b = self.peer("a"), self.peer("b")
        a.send(f"MOVE\t{b.id}\t20\t30\t40")
        self.assertEqual(a.until("ERROR"), "ERROR\tBAD_COMMAND")
        b.until("DESPAWN")
        b.send("PLAYERS")
        state, _ = b.snapshot()
        self.assertEqual(set(state), {b.id})
        self.assertEqual(state[b.id][1], (0.0, 0.0, 0.0))

    def test_06_disconnect_removes_entity_and_reconnect_starts_at_origin(self):
        a, b = self.peer("a"), self.peer("b")
        a.send("MOVE\t9\t8\t7")
        b.until("POSITION")
        a.close()
        self.assertEqual(b.until("DESPAWN").split("\t")[1], str(a.id))
        newcomer = self.peer("a")
        self.assertNotEqual(newcomer.id, a.id)
        self.assertNotIn(a.id, newcomer.initial_players)
        self.assertEqual(newcomer.initial_players[newcomer.id][1], (0.0, 0.0, 0.0))

    def test_07_four_players_keep_independent_state(self):
        clients = [self.peer("same-name") for _ in range(4)]
        # Await an echo before the next client moves: stable revision ordering.
        for i, p in enumerate(clients):
            p.send(f"MOVE\t{i + 1}\t{-i}\t{i * 10}")
            for other in clients:
                self.assertEqual(other.until("POSITION").split("\t")[1], str(p.id))
        clients[0].send("PLAYERS")
        state, revision = clients[0].snapshot()
        self.assertEqual(revision, 8)
        for i, p in enumerate(clients):
            self.assertEqual(state[p.id][1], (float(i + 1), float(-i), float(i * 10)))

    def test_08_fragmented_move_and_pipelined_snapshot(self):
        p = self.peer("fragments")
        p.sock.sendall(b"MO")
        p.sock.sendall(b"VE\t-100000\t100000\t0\nPLAYERS\n")
        event = p.line().split("\t")
        self.assertEqual(event[:2], ["POSITION", str(p.id)])
        self.assertEqual(tuple(float(n) for n in event[2:5]), (-100000.0, 100000.0, 0.0))
        self.assertEqual(event[5], "2")
        state, _ = p.snapshot()
        self.assertEqual(state[p.id][1], (-100000.0, 100000.0, 0.0))

    def test_09_cli_move_players_and_local_validation(self):
        result = subprocess.run(
            [support.EXE, "join", "--port", str(self.port), "--name", "console"],
            input="/move nan 0 0\n/move 10 0 5\n/players\n/quit\n",
            env=self.env, capture_output=True, text=True, timeout=8,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Comando invalido", result.stderr)
        self.assertIn("POSITION\t1\t10\t0\t5\t2", result.stdout)
        self.assertIn("PLAYER\t1\tconsole\t10\t0\t5\t2", result.stdout)
        self.assertIn("BYE", result.stdout)

    def test_10_unadmitted_connection_cannot_create_or_move_entities(self):
        valid = self.peer("valid")
        attacker = self.peer()
        attacker.send("MOVE\t50\t50\t50")
        self.assertEqual(attacker.line(), "ERROR\tEXPECTED_HELLO")
        valid.send("PLAYERS")
        state, revision = valid.snapshot()
        self.assertEqual(len(state), 1)
        self.assertEqual(revision, 1)

    def test_11_real_client_applies_remote_spawn_move_and_removal(self):
        proc = subprocess.Popen(
            [support.EXE, "join", "--port", str(self.port), "--name", "observer"],
            env=self.env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True,
        )
        lines = queue.Queue()
        reader = threading.Thread(target=support.collect_lines, args=(proc.stdout, lines), daemon=True)
        reader.start()

        def event(prefix):
            for _ in range(64):
                line = lines.get(timeout=4)
                if line == prefix or line.startswith(prefix + "\t"):
                    return line
            self.fail("Evento nao recebido: " + prefix)

        try:
            event("PLAYERS_END")
            actor = self.peer("actor")
            self.assertEqual(event("SPAWN").split("\t")[1], str(actor.id))
            actor.send("MOVE\t7\t8\t9")
            self.assertEqual(event("POSITION").split("\t")[1:5], [str(actor.id), "7", "8", "9"])
            actor.close()
            self.assertEqual(event("DESPAWN").split("\t")[1], str(actor.id))
            proc.stdin.write("/players\n/quit\n")
            proc.stdin.flush()
            event("BYE")
            proc.wait(timeout=4)
            self.assertEqual(proc.returncode, 0, proc.stderr.read())
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.wait(timeout=3)
            reader.join(timeout=2)
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
