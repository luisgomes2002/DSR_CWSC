"""Candidate-reader guards, lifecycle and real Windows reads of a synthetic child."""
from contextlib import contextmanager
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import queue
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from player_reader import (ANCHORS, GAME_DATA_RVA, VTABLES, WORLD_RVA, PlayerSampler,
                           ProcessExited, ReaderError, Unavailable, WindowsProcess,
                           canonical, check_anchors, verify_attachment)
from static_game_probe import Image


class Heap:
    # A relocated sparse memory contract; deliberately different from the PE base.
    base = 0x180000000
    world, player, controller, movement = (0x20000, 0x30000, 0x40000, 0x50000)

    def __init__(self):
        self.bytes = {}
        for name in ("world", "player", "controller"):
            self.pointer(getattr(self, name), self.base + VTABLES[name])
        self.pointer(self.base + WORLD_RVA, self.world)
        self.pointer(self.base + VTABLES["player"] + 0x150, self.base + 0x322730)
        self.pointer(self.world + 0x68, self.player)
        self.pointer(self.player + 0x68, self.controller)
        self.pointer(self.controller + 0x10, self.player)
        self.pointer(self.controller + 0x28, self.movement)
        self.put(self.movement, struct.pack("<8f", 0.1, 0.2, 0.3, math.nan, 10, 20, 30, math.nan))

    def put(self, address, data):
        self.bytes.update((address + i, value) for i, value in enumerate(data))

    def pointer(self, address, value): self.put(address, struct.pack("<Q", value))

    def read(self, address, size):
        try:
            return bytes(self.bytes[address + i] for i in range(size))
        except KeyError:
            raise Unavailable("unreadable_page") from None


class SamplerTests(unittest.TestCase):
    def setUp(self):
        self.heap = Heap()
        self.reader = PlayerSampler(self.heap, self.heap.base)

    def test_relocation_and_unknown_fourth_lanes(self):
        sample = self.reader.sample()
        self.assertEqual(sample["status"], "candidate")
        self.assertEqual(sample["position_components"], (10, 20, 30))
        self.assertEqual(len(sample["orientation_candidate_rad"]), 3)
        json.dumps(sample, allow_nan=False)  # Unknown fourth lanes are never exported.

    def test_loading_does_not_republish_old_values(self):
        self.assertEqual(self.reader.sample()["generation"], 1)
        self.heap.pointer(self.heap.base + WORLD_RVA, 0)
        self.assertEqual(self.reader.sample(), {"status": "unavailable", "reason": "null_world"})
        self.heap.pointer(self.heap.base + WORLD_RVA, self.heap.world)
        self.assertEqual(self.reader.sample()["generation"], 2)

    def test_wrong_types_back_reference_and_predicate(self):
        for field in ("world", "player", "controller"):
            with self.subTest(field=field):
                heap = Heap()
                heap.pointer(getattr(heap, field), 0x123400)
                self.assertEqual(PlayerSampler(heap, heap.base).sample()["reason"], f"unexpected_vtable_{field}")
        self.heap.pointer(self.heap.controller + 0x10, 0x123400)
        self.assertEqual(self.reader.sample()["reason"], "controller_back_reference_mismatch")
        self.heap.pointer(self.heap.controller + 0x10, self.heap.player)
        self.heap.pointer(self.heap.base + VTABLES["player"] + 0x150, 0x123400)
        self.assertEqual(self.reader.sample()["reason"], "unexpected_local_player_predicate")

    def test_null_invalid_unreadable_pointers(self):
        for value, reason in ((0, "null_movement"), (3, "invalid_pointer_movement"),
                              (0xFFFF000000000000, "invalid_pointer_movement"),
                              (0x60000, "unreadable_page")):
            self.heap.pointer(self.heap.controller + 0x28, value)
            self.assertEqual(self.reader.sample()["reason"], reason)

    def test_nan_and_infinity_are_rejected(self):
        for value in (math.nan, math.inf, -math.inf):
            for offset in (0, 0x10):
                heap = Heap()
                heap.put(heap.movement + offset, struct.pack("<f", value))
                sample = PlayerSampler(heap, heap.base).sample()
                self.assertEqual(sample, {"status": "unavailable", "reason": "nonfinite_components"})

    def test_changing_vectors_retry_with_a_fixed_budget(self):
        class Flapping(Heap):
            reads = 0

            def read(self, address, size):
                if address == self.movement and size == 32:
                    self.reads += 1
                    self.put(self.movement + 0x10, struct.pack("<f", self.reads))
                return super().read(address, size)
        heap = Flapping()
        self.assertEqual(PlayerSampler(heap, heap.base).sample()["reason"], "unstable_sample")
        self.assertEqual(heap.reads, 6)

    def test_changed_chain_never_publishes_previous_block(self):
        class Replacing(Heap):
            def read(self, address, size):
                if address == self.movement and size == 32:
                    self.pointer(self.base + WORLD_RVA, 0)
                return super().read(address, size)
        heap = Replacing()
        sample = PlayerSampler(heap, heap.base).sample()
        self.assertEqual(sample["status"], "unavailable")
        self.assertNotIn("position_components", sample)

    def test_script_id_is_separate_optional_and_type_checked(self):
        self.assertNotIn("script_id_candidate", self.reader.sample())
        data, entry = 0x70000, 0x80000
        self.heap.pointer(self.heap.base + GAME_DATA_RVA, data)
        self.heap.pointer(data, self.heap.base + VTABLES["game_data"])
        self.heap.pointer(data + 0x10, entry)
        self.heap.pointer(entry, self.heap.base + VTABLES["player_data"])
        self.heap.put(entry + 0x10, struct.pack("<I", 1234))
        reader = PlayerSampler(self.heap, self.heap.base, True)
        self.assertEqual(reader.sample()["script_id_candidate"], {"status": "candidate", "value": 1234})
        self.heap.pointer(entry, 0x123400)
        sample = reader.sample()
        self.assertEqual(sample["status"], "candidate")
        self.assertEqual(sample["script_id_candidate"]["status"], "unavailable")

    def test_live_anchor_changes_stop_the_reader(self):
        for address, data in ANCHORS.items(): self.heap.put(self.heap.base + address, data)
        check_anchors(self.heap, self.heap.base)
        self.heap.put(self.heap.base + next(iter(ANCHORS)), b"\xcc")
        with self.assertRaisesRegex(ReaderError, "Unsupported live code"):
            check_anchors(self.heap, self.heap.base)


def receive(process):
    result = queue.Queue()
    threading.Thread(target=lambda: result.put(process.stdout.readline()), daemon=True).start()
    return json.loads(result.get(timeout=5))


@contextmanager
def fixture():
    process = subprocess.Popen([sys.executable, str(ROOT / "tests" / "player_reader_fixture.py")],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        ready = receive(process)
        yield process, ready
    finally:
        if process.poll() is None:
            process.stdin.write("exit\n")
            process.stdin.flush()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
        for pipe in (process.stdin, process.stdout, process.stderr): pipe.close()


@unittest.skipUnless(os.name == "nt" and ctypes.sizeof(ctypes.c_void_p) == 8, "Windows x64 required")
class WindowsReaderTests(unittest.TestCase):
    def test_real_module_headers_and_creation_identity(self):
        # Validate native module enumeration and PE identity independently of
        # game-specific anchors; those anchors have a separate rejection test.
        digest = hashlib.sha256(Path(sys.executable).read_bytes()).hexdigest()
        image = Image(sys.executable, digest)
        with fixture() as (_, ready), WindowsProcess(ready["pid"]) as memory:
            with patch("player_reader.check_anchors") as anchors:
                identity = verify_attachment(memory, sys.executable, image)
            anchors.assert_called_once_with(memory, int(identity["module_base"], 16))
            self.assertEqual(identity["pid"], ready["pid"])
            self.assertEqual(identity["executable_sha256"], digest)
            self.assertGreater(identity["process_creation_filetime"], 0)

    def test_real_cross_process_reads_loading_and_recovery(self):
        with fixture() as (child, ready), WindowsProcess(ready["pid"]) as memory:
            self.assertTrue(memory.alive())
            self.assertEqual(canonical(memory.image_path()), canonical(sys.executable))
            self.assertGreater(memory.creation_time(), 0)
            base, size = memory.main_module(sys.executable)
            self.assertGreater(size, 0)
            self.assertEqual(memory.read(base, 2), b"MZ")
            reader = PlayerSampler(memory, ready["base"])
            self.assertEqual(reader.sample()["position_components"], (10, 20, 30))
            for command, status in (("move", "candidate"), ("null", "unavailable"),
                                    ("restore", "candidate"), ("bad_type", "unavailable"),
                                    ("restore", "candidate"), ("nan", "unavailable")):
                child.stdin.write(command + "\n"); child.stdin.flush()
                self.assertEqual(receive(child)["done"], command)
                result = reader.sample()
                self.assertEqual(result["status"], status)
                if command == "move": self.assertEqual(result["position_components"], (40, 50, 60))

    def test_real_page_guards_partial_ranges_and_process_exit(self):
        with fixture() as (child, ready), WindowsProcess(ready["pid"]) as memory:
            for address in (ready["unreadable_page"], ready["unreadable_page"] - 4):
                with self.assertRaisesRegex(Unavailable, "unreadable_page"):
                    memory.read(address, 8)
            # Query/reject PAGE_GUARD without consuming its one-shot protection.
            for _ in range(2):
                with self.assertRaisesRegex(Unavailable, "unreadable_page"):
                    memory.read(ready["guard_page"], 8)
            with self.assertRaisesRegex(Unavailable, "invalid_read_range"):
                memory.read(0, 8)
            with self.assertRaisesRegex(ReaderError, "PID executable path"):
                verify_attachment(memory, ROOT / "unrelated.exe", None)
            child.stdin.write("exit\n"); child.stdin.flush(); child.wait(timeout=5)
            self.assertFalse(memory.alive())
            with self.assertRaises(ProcessExited): memory.read(ready["movement"], 32)

    def test_cli_rejects_wrong_build_and_preserves_existing_report(self):
        with tempfile.TemporaryDirectory(prefix="dsr-reader-cli-") as temp:
            output = Path(temp) / "reader.jsonl"
            command = [sys.executable, str(ROOT / "tools" / "player_reader.py"),
                       "--pid", str(os.getpid()), "--exe", sys.executable, "--output", str(output)]
            first = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertEqual(first.returncode, 1)
            original = output.read_bytes()
            records = [json.loads(line) for line in original.decode().splitlines()]
            self.assertEqual(records[-1]["status"], "error")
            self.assertIn("SHA-256", records[-1]["reason"])
            self.assertNotIn("attached", [r["status"] for r in records])
            second = subprocess.run(command, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(output.read_bytes(), original)


if __name__ == "__main__":
    unittest.main(verbosity=2)
