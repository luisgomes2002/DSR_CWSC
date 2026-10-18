"""Synthetic child process. Owns its test heap; never opens or loads a game."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from player_reader import WORLD_RVA, VTABLES

heap = ctypes.create_string_buffer(0x2000000)
base = ctypes.addressof(heap)
world, player, controller, movement = [base + n for n in (0x1000, 0x4000, 0x7000, 0x9000)]


def put(address, data):
    ctypes.memmove(address, data, len(data))


def pointer(address, value):
    put(address, struct.pack("<Q", value))


def restore():
    for name, address in (("world", world), ("player", player), ("controller", controller)):
        pointer(address, base + VTABLES[name])
    pointer(base + WORLD_RVA, world)
    pointer(base + VTABLES["player"] + 0x150, base + 0x322730)
    pointer(world + 0x68, player)
    pointer(player + 0x68, controller)
    pointer(controller + 0x10, player)
    pointer(controller + 0x28, movement)
    put(movement, struct.pack("<8f", 0.1, 0.2, 0.3, 0, 10, 20, 30, 0))


api = ctypes.WinDLL("kernel32", use_last_error=True)
api.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
api.VirtualAlloc.restype = ctypes.c_void_p
api.VirtualProtect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
api.VirtualProtect.restype = wintypes.BOOL
api.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
api.VirtualFree.restype = wintypes.BOOL
pages = api.VirtualAlloc(None, 12288, 0x3000, 4)
if not pages:
    raise ctypes.WinError(ctypes.get_last_error())
old = wintypes.DWORD()
if not api.VirtualProtect(pages + 4096, 4096, 1, ctypes.byref(old)):
    raise ctypes.WinError(ctypes.get_last_error())
if not api.VirtualProtect(pages + 8192, 4096, 0x104, ctypes.byref(old)):
    raise ctypes.WinError(ctypes.get_last_error())
restore()
print(json.dumps({"pid": os.getpid(), "base": base, "movement": movement,
                  "unreadable_page": pages + 4096, "guard_page": pages + 8192}), flush=True)
try:
    for command in sys.stdin:
        command = command.strip()
        if command == "exit":
            break
        if command == "null":
            pointer(base + WORLD_RVA, 0)
        elif command == "nan":
            put(movement + 0x10, struct.pack("<f", float("nan")))
        elif command == "move":
            put(movement + 0x10, struct.pack("<4f", 40, 50, 60, 0))
        elif command == "bad_type":
            pointer(player, base + VTABLES["player"] + 8)
        elif command == "restore":
            restore()
        else:
            raise RuntimeError("Unknown fixture command")
        print(json.dumps({"done": command}), flush=True)
finally:
    api.VirtualFree(pages, 0, 0x8000)
