"""Read-only PE64/RTTI/string probes. Does not load or execute the target image."""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

GAME_SHA256 = "a45aaa36dd2f6cc151670a639ea5547043cf38ea79ff4178b963c6ed71f98d7b"
DEFAULT_GAME = r"E:\SteamLibrary\steamapps\common\DARK SOULS REMASTERED\DarkSoulsRemastered.exe"


class Image:
    def __init__(self, path, expected=GAME_SHA256):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        if self.sha256 != expected.lower():
            raise ValueError("Target SHA-256 does not match the explicitly selected build")
        pe = self.u32(0x3c)
        if self.data[:2] != b"MZ" or self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE image")
        optional = pe + 24
        if self.u16(pe + 4) != 0x8664 or self.u16(optional) != 0x20b:
            raise ValueError("Expected x64 PE32+ image")
        self.base = self.u64(optional + 24)
        self.sections = []
        for index in range(self.u16(pe + 6)):
            at = optional + self.u16(pe + 20) + index * 40
            virtual_size, rva, size, raw = struct.unpack_from("<IIII", self.data, at + 8)
            self.sections.append(dict(name=self.data[at:at + 8].rstrip(b"\0").decode(),
                                      rva=rva, virtual_size=virtual_size, raw=raw, size=size,
                                      flags=self.u32(at + 36)))
        self.functions = []
        for section in self.sections:
            if section["name"] == ".pdata":
                for at in range(section["raw"], section["raw"] + section["size"] - 11, 12):
                    start, end, _ = struct.unpack_from("<III", self.data, at)
                    if 0 < start < end:
                        self.functions.append((start, end))
        self.functions.sort()
        self.starts = [f[0] for f in self.functions]

    def u16(self, at): return struct.unpack_from("<H", self.data, at)[0]
    def u32(self, at): return struct.unpack_from("<I", self.data, at)[0]
    def u64(self, at): return struct.unpack_from("<Q", self.data, at)[0]

    def raw(self, rva):
        for s in self.sections:
            if s["rva"] <= rva < s["rva"] + s["size"]:
                return s["raw"] + rva - s["rva"]
        raise ValueError(f"RVA 0x{rva:x} has no file-backed bytes")

    def rva(self, raw):
        for s in self.sections:
            if s["raw"] <= raw < s["raw"] + s["size"]:
                return s["rva"] + raw - s["raw"]
        raise ValueError("Offset outside sections")

    def owner(self, rva):
        index = bisect.bisect_right(self.starts, rva) - 1
        return self.functions[index] if index >= 0 and rva < self.functions[index][1] else None

    def matches(self, value):
        start = 0
        while True:
            found = self.data.find(value, start)
            if found < 0:
                return
            yield found
            start = found + 1

    def refs(self, target):
        # Candidate x64 RIP-relative REX.W LEA/MOV. Verify boundaries with objdump.
        for s in self.sections:
            if not s["flags"] & 0x20000000:
                continue
            code = self.data[s["raw"]:s["raw"] + s["size"]]
            for m in re.finditer(rb"(?=([\x48\x4c][\x8b\x8d][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....))", code, re.S):
                site = s["rva"] + m.start()
                if site + 7 + struct.unpack_from("<i", m.group(1), 3)[0] == target:
                    yield dict(site=hex(site), unwind_region=[hex(x) for x in self.owner(site)] if self.owner(site) else None)

    def rtti(self, name):
        output = []
        for found in self.matches(name.encode() + b"\0"):
            descriptor = self.rva(found - 16)
            locators = []
            for pointer in self.matches(struct.pack("<I", descriptor)):
                at = pointer - 12
                if at < 0 or at + 24 > len(self.data):
                    continue
                fields = struct.unpack_from("<IIIIII", self.data, at)
                try:
                    locator = self.rva(at)
                    if fields[0] != 1 or fields[5] != locator:
                        continue
                    tables = []
                    for reference in self.matches(struct.pack("<Q", self.base + locator)):
                        slots = []
                        for index in range(12):
                            value = self.u64(reference + 8 + index * 8) - self.base
                            if not any(s["flags"] & 0x20000000 and s["rva"] <= value < s["rva"] + s["size"] for s in self.sections):
                                break
                            slots.append(hex(value))
                        if slots:
                            tables.append(dict(rva=hex(self.rva(reference + 8)), first_slots=slots))
                    hierarchy = self.raw(fields[4])
                    count = self.u32(hierarchy + 8)
                    bases = []
                    if count <= 64:
                        array = self.raw(self.u32(hierarchy + 12))
                        for index in range(count):
                            entry = self.raw(self.u32(array + index * 4))
                            base_type = self.raw(self.u32(entry))
                            end = self.data.index(b"\0", base_type + 16)
                            bases.append(dict(name=self.data[base_type + 16:end].decode(),
                                              member_displacement=struct.unpack_from("<i", self.data, entry + 8)[0]))
                    locators.append(dict(rva=hex(locator), subobject_offset=fields[1], vtables=tables, bases=bases))
                except (ValueError, struct.error, UnicodeDecodeError):
                    continue
            output.append(dict(type_name=name, descriptor=hex(descriptor), locators=locators))
        return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default=DEFAULT_GAME)
    parser.add_argument("--sha256", default=GAME_SHA256)
    parser.add_argument("--strings", help="ASCII/UTF-16LE string regex")
    parser.add_argument("--rtti", nargs="+")
    parser.add_argument("--refs", type=lambda x: int(x, 16))
    parser.add_argument("--limit", type=int, default=32, help="Maximum strings/references displayed")
    parser.add_argument("--output", help="New JSON evidence file; does not overwrite")
    parser.add_argument("--range", nargs=2, type=lambda x: int(x, 16), metavar=("START_RVA", "END_RVA"))
    args = parser.parse_args()
    if args.limit < 1:
        parser.error("Use a positive output limit")
    if args.output and Path(args.output).exists():
        parser.error("Choose a new evidence path")
    if args.output and args.range:
        parser.error("--output saves JSON probes; use --range for console disassembly")
    image = Image(args.target, args.sha256)
    if args.range:
        lo, hi = args.range
        subprocess.run(["objdump", "-d", "-M", "intel", f"--start-address={image.base + lo}",
                        f"--stop-address={image.base + hi}", str(image.path)], check=True)
        return
    result = dict(sha256=image.sha256, preferred_image_base=hex(image.base))
    if args.strings:
        pattern = re.compile(args.strings, re.I)
        strings = []
        for encoding, regex in (("ascii", rb"[\x20-\x7e]{5,}"), ("utf-16le", rb"(?:[\x20-\x7e]\0){5,}")):
            for match in re.finditer(regex, image.data):
                value = match.group().decode(encoding)
                if pattern.search(value):
                    try:
                        strings.append(dict(rva=hex(image.rva(match.start())), encoding=encoding, text=value))
                    except ValueError:
                        pass
        result["string_matches"] = len(strings)
        result["strings"] = strings[:args.limit]
    if args.rtti:
        result["rtti"] = [item for name in args.rtti for item in image.rtti(name)]
    if args.refs is not None:
        refs = list(image.refs(args.refs))
        result["rip_candidate_ref_count"] = len(refs)
        result["rip_candidate_refs"] = refs[:args.limit]
    encoded = json.dumps(result, indent=2)
    if args.output:
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8") as stream:
            stream.write(encoded + "\n")
        print(f"Evidence saved: {output}")
    else:
        print(encoded)


if __name__ == "__main__":
    main()
