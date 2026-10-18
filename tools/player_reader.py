"""Read-only Windows reader for statically identified DSR player-data candidates."""
import argparse
from collections import Counter
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import struct
import sys
import time

from static_game_probe import GAME_SHA256, Image

# Exactly the build documented in PLAYER_STATIC_CANDIDATES.md. No offset override.
WORLD_RVA = 0x1C77E50
GAME_DATA_RVA = 0x1C8A530
VTABLES = {"world": 0x1328210, "player": 0x13251F0, "controller": 0x132C258,
           "game_data": 0x13DBF70, "player_data": 0x13DD620}
ANCHORS = {
    0x322730: bytes.fromhex("488b0519579501483948680f94c0c3"),
    0x37A500: bytes.fromhex("488b41280f284010488bc2660f7f02c3"),
    0x37A630: bytes.fromhex("488b41280f2800488bc2660f7f02c3"),
}
# Query pages/metadata, read memory and observe process exit. No write/debug rights.
PROCESS_ACCESS = 0x0400 | 0x0010 | 0x00100000
MAX_ADDRESS = 0x00007FFFFFFEFFFF


class ReaderError(RuntimeError):
    pass


class ProcessExited(ReaderError):
    pass


class Unavailable(ReaderError):
    """No values may be published for this sample."""


class ModuleEntry(ctypes.Structure):
    _fields_ = [(name, wintypes.DWORD) for name in (
        "dwSize", "th32ModuleID", "th32ProcessID", "GlblcntUsage", "ProccntUsage")] + [
        ("modBaseAddr", ctypes.c_void_p), ("modBaseSize", wintypes.DWORD),
        ("hModule", wintypes.HMODULE), ("szModule", wintypes.WCHAR * 256),
        ("szExePath", wintypes.WCHAR * 260)]


class MemoryInfo(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wintypes.DWORD), ("PartitionId", wintypes.WORD),
                ("RegionSize", ctypes.c_size_t), ("State", wintypes.DWORD),
                ("Protect", wintypes.DWORD), ("Type", wintypes.DWORD)]


def canonical(path):
    return os.path.normcase(str(Path(path).resolve()))


class WindowsProcess:
    def __init__(self, pid):
        if os.name != "nt" or ctypes.sizeof(ctypes.c_void_p) != 8:
            raise ReaderError("Use 64-bit Python on Windows")
        self.pid, self.handle = pid, None
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "OpenProcess": (wintypes.HANDLE, [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]),
            "CloseHandle": (wintypes.BOOL, [wintypes.HANDLE]),
            "WaitForSingleObject": (wintypes.DWORD, [wintypes.HANDLE, wintypes.DWORD]),
            "QueryFullProcessImageNameW": (wintypes.BOOL, [wintypes.HANDLE, wintypes.DWORD,
                                                         wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]),
            "GetProcessTimes": (wintypes.BOOL, [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4),
            "CreateToolhelp32Snapshot": (wintypes.HANDLE, [wintypes.DWORD, wintypes.DWORD]),
            "Module32FirstW": (wintypes.BOOL, [wintypes.HANDLE, ctypes.POINTER(ModuleEntry)]),
            "Module32NextW": (wintypes.BOOL, [wintypes.HANDLE, ctypes.POINTER(ModuleEntry)]),
            "VirtualQueryEx": (ctypes.c_size_t, [wintypes.HANDLE, ctypes.c_void_p,
                                                ctypes.POINTER(MemoryInfo), ctypes.c_size_t]),
            "ReadProcessMemory": (wintypes.BOOL, [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                                 ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.api, name)
            function.restype, function.argtypes = result, arguments
        self.handle = self.api.OpenProcess(PROCESS_ACCESS, False, pid)
        if not self.handle:
            raise ReaderError(f"OpenProcess failed: {ctypes.WinError(ctypes.get_last_error())}")

    def __enter__(self): return self

    def __exit__(self, *_): self.close()

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None

    def alive(self):
        if not self.handle:
            return False
        status = self.api.WaitForSingleObject(self.handle, 0)
        if status == 0xFFFFFFFF:
            raise ReaderError(f"Process wait failed: {ctypes.WinError(ctypes.get_last_error())}")
        return status == 258

    def image_path(self):
        buffer = ctypes.create_unicode_buffer(32768)
        size = wintypes.DWORD(len(buffer))
        if not self.api.QueryFullProcessImageNameW(self.handle, 0, buffer, ctypes.byref(size)):
            raise ReaderError(f"Process path query failed: {ctypes.WinError(ctypes.get_last_error())}")
        return buffer.value

    def creation_time(self):
        times = [wintypes.FILETIME() for _ in range(4)]
        if not self.api.GetProcessTimes(self.handle, *(ctypes.byref(t) for t in times)):
            raise ReaderError("Process creation-time query failed")
        return (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime

    def main_module(self, expected_path):
        invalid = ctypes.c_void_p(-1).value
        for _ in range(5):
            snapshot = self.api.CreateToolhelp32Snapshot(0x08 | 0x10, self.pid)
            if snapshot != invalid:
                break
            if ctypes.get_last_error() != 24:  # ERROR_BAD_LENGTH can be transient.
                raise ReaderError(f"Module snapshot failed: {ctypes.WinError(ctypes.get_last_error())}")
            time.sleep(0.02)
        else:
            raise ReaderError("Module list remained unstable")
        try:
            entry = ModuleEntry()
            entry.dwSize = ctypes.sizeof(entry)
            present = self.api.Module32FirstW(snapshot, ctypes.byref(entry))
            for _ in range(4096):
                if not present:
                    break
                if canonical(entry.szExePath) == canonical(expected_path):
                    return int(entry.modBaseAddr), int(entry.modBaseSize)
                present = self.api.Module32NextW(snapshot, ctypes.byref(entry))
            raise ReaderError("Exact executable path not found in target modules")
        finally:
            self.api.CloseHandle(snapshot)

    def read(self, address, size):
        if not self.alive():
            raise ProcessExited("process_exited")
        if not 1 <= size <= 256 or not 0x10000 <= address <= MAX_ADDRESS - size + 1:
            raise Unavailable("invalid_read_range")
        cursor, end = address, address + size
        while cursor < end:
            info = MemoryInfo()
            count = self.api.VirtualQueryEx(self.handle, cursor, ctypes.byref(info), ctypes.sizeof(info))
            if count != ctypes.sizeof(info):
                raise Unavailable("page_query_failed")
            region_end = int(info.BaseAddress or 0) + info.RegionSize
            if (info.State != 0x1000 or info.Protect & 0x100 or
                    info.Protect & 0xFF not in (2, 4, 8, 0x20, 0x40, 0x80) or region_end <= cursor):
                raise Unavailable("unreadable_page")
            cursor = min(end, region_end)
        buffer, copied = ctypes.create_string_buffer(size), ctypes.c_size_t()
        if not self.api.ReadProcessMemory(self.handle, address, buffer, size, ctypes.byref(copied)) or copied.value != size:
            if not self.alive():
                raise ProcessExited("process_exited")
            raise Unavailable("failed_or_partial_read")
        return buffer.raw


def u64(memory, address):
    return struct.unpack("<Q", memory.read(address, 8))[0]


def check_anchors(memory, base):
    for rva, expected in ANCHORS.items():
        if memory.read(base + rva, len(expected)) != expected:
            raise ReaderError(f"Unsupported live code at RVA 0x{rva:x}; candidate reader stopped")


def verify_attachment(memory, expected_path, image):
    if canonical(memory.image_path()) != canonical(expected_path):
        raise ReaderError("PID executable path differs from --exe")
    base, size = memory.main_module(expected_path)
    pe = image.u32(0x3c)
    expected_size = image.u32(pe + 24 + 56)
    if size != expected_size or memory.read(base, 2) != b"MZ":
        raise ReaderError("Mapped image size/header differs from the supported executable")
    header = memory.read(base + pe, 88)
    for start, length in ((0, 6), (8, 4), (24, 2), (40, 4), (80, 4)):
        if header[start:start + length] != image.data[pe + start:pe + start + length]:
            raise ReaderError("Mapped PE identity differs from the supported executable")
    check_anchors(memory, base)
    if not memory.alive():
        raise ProcessExited("process_exited")
    return {"pid": memory.pid, "process_creation_filetime": memory.creation_time(),
            "executable_path": memory.image_path(), "executable_sha256": image.sha256,
            "module_base": hex(base), "module_size": size, "process_access": hex(PROCESS_ACCESS)}


class PlayerSampler:
    def __init__(self, memory, base, include_script_id=False):
        self.memory, self.base, self.include_script_id = memory, base, include_script_id
        self.previous, self.valid_last, self.generation = None, False, 0

    def pointer(self, address, name):
        value = u64(self.memory, address)
        if not value:
            raise Unavailable(f"null_{name}")
        if value % 8 or not 0x10000 <= value <= MAX_ADDRESS - 256:
            raise Unavailable(f"invalid_pointer_{name}")
        return value

    def typed(self, address, name):
        if u64(self.memory, address) != self.base + VTABLES[name]:
            raise Unavailable(f"unexpected_vtable_{name}")

    def chain(self):
        world = self.pointer(self.base + WORLD_RVA, "world")
        self.typed(world, "world")
        player = self.pointer(world + 0x68, "player")
        self.typed(player, "player")
        # Validate the statically linked IsLocalPlayer vtable slot too.
        if u64(self.memory, self.base + VTABLES["player"] + 0x150) != self.base + 0x322730:
            raise Unavailable("unexpected_local_player_predicate")
        controller = self.pointer(player + 0x68, "controller")
        self.typed(controller, "controller")
        if u64(self.memory, controller + 0x10) != player:
            raise Unavailable("controller_back_reference_mismatch")
        movement = self.pointer(controller + 0x28, "movement")
        return world, player, controller, movement

    def script_id(self):
        try:
            data = self.pointer(self.base + GAME_DATA_RVA, "game_data")
            self.typed(data, "game_data")
            entry = self.pointer(data + 0x10, "player_data")
            self.typed(entry, "player_data")
            result = struct.unpack("<I", self.memory.read(entry + 0x10, 4))[0]
            if self.pointer(self.base + GAME_DATA_RVA, "game_data") != data or self.pointer(data + 0x10, "player_data") != entry:
                raise Unavailable("unstable_script_id")
            return {"status": "candidate", "value": result}
        except Unavailable as error:
            return {"status": "unavailable", "reason": str(error)}

    def sample(self):
        reason = "unstable_sample"
        for _ in range(3):
            try:
                chain = self.chain()
                first = self.memory.read(chain[3], 32)
                second = self.memory.read(chain[3], 32)
                if first != second or self.chain() != chain:
                    continue
                values = struct.unpack("<8f", first)
                orientation, position = values[:3], values[4:7]
                if not all(math.isfinite(x) for x in orientation + position):
                    raise Unavailable("nonfinite_components")
                if not self.valid_last or chain != self.previous:
                    self.generation += 1
                self.previous, self.valid_last = chain, True
                result = {"status": "candidate", "generation": self.generation,
                          "position_components": position, "orientation_candidate_rad": orientation,
                          "addresses": dict(zip(("world", "player", "controller", "movement"), map(hex, chain)))}
                if self.include_script_id:
                    result["script_id_candidate"] = self.script_id()
                return result
            except Unavailable as error:
                reason = str(error)
                if reason != "unstable_sample":
                    break
        self.valid_last = False
        return {"status": "unavailable", "reason": reason}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", required=True, type=int)
    parser.add_argument("--exe", required=True, help="Exact already-running game's executable path")
    parser.add_argument("--output", required=True, help="New JSONL report outside the game folder")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--hz", type=float, default=10)
    parser.add_argument("--include-script-id", action="store_true", help="Also probe the separate, unvalidated ID candidate")
    args = parser.parse_args()
    if args.pid <= 0 or not 1 <= args.seconds <= 3600 or not 0.5 <= args.hz <= 30:
        parser.error("Use a positive PID, 1-3600 seconds and 0.5-30 Hz")
    output, executable = Path(args.output).resolve(), Path(args.exe).resolve()
    if output.exists():
        parser.error("Choose a new report path; existing files are preserved")
    if output.is_relative_to(executable.parent):
        parser.error("Store the report outside the game installation")
    output.parent.mkdir(parents=True, exist_ok=True)
    started, counts = time.monotonic(), Counter()
    with output.open("x", encoding="utf-8") as stream:
        def emit(record):
            stream.write(json.dumps({"elapsed_s": time.monotonic() - started, **record}, allow_nan=False) + "\n")
            stream.flush()
        emit({"status": "starting", "schema": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
              "expected_sha256": GAME_SHA256, "requested_pid": args.pid, "requested_executable": str(executable),
              "seconds": args.seconds, "hz": args.hz, "semantics": "Unvalidated player-data candidates; axes and units pending"})
        try:
            image = Image(executable)  # Hash check before opening a target process.
            with WindowsProcess(args.pid) as memory:
                identity = verify_attachment(memory, executable, image)
                emit({"status": "attached", **identity})
                print(f"ATTACHED pid={args.pid}, base={identity['module_base']}; output={output}", flush=True)
                base = int(identity["module_base"], 16)
                sampler = PlayerSampler(memory, base, args.include_script_id)
                run_start = time.monotonic()
                next_anchor = run_start + 5
                last_print, last_status = 0.0, None
                while time.monotonic() - run_start < args.seconds:
                    tick = time.monotonic()
                    if not memory.alive():
                        raise ProcessExited("process_exited")
                    if tick >= next_anchor:
                        check_anchors(memory, base)
                        next_anchor = tick + 5
                    record = sampler.sample()
                    counts[record["status"]] += 1
                    emit(record)
                    state = (record["status"], record.get("reason"))
                    if tick - last_print >= 1 or state != last_status:
                        print(json.dumps(record, allow_nan=False), flush=True)
                        last_print, last_status = tick, state
                    time.sleep(max(0, min(1 / args.hz - (time.monotonic() - tick),
                                          args.seconds - (time.monotonic() - run_start))))
            emit({"status": "finished", "counts": dict(counts)})
            return 0 if counts["candidate"] else 2
        except (ProcessExited, KeyboardInterrupt) as error:
            emit({"status": "stopped", "reason": str(error) or "interrupted", "counts": dict(counts)})
            return 0 if isinstance(error, KeyboardInterrupt) or counts["candidate"] else 2
        except (ReaderError, OSError, ValueError, struct.error) as error:
            emit({"status": "error", "reason": str(error), "counts": dict(counts)})
            print(f"ERROR: {error}", file=sys.stderr)
            return 1


if __name__ == "__main__":
    raise SystemExit(main())
