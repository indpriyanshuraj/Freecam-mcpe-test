#!/usr/bin/env python3
"""Verify the exact Minecraft 1.26.52.3 ELF gate used by Levi Freecam."""
from __future__ import annotations

import argparse
import re
import struct
import subprocess
from pathlib import Path

EXPECTED_BUILD_ID = "56de9eed077631e03a31f4f58eb2f0e00071d335"
EXPECTED_TYPE_NAME = b"14ClientInstance\0"

SIGNATURES = {
    "ClientInstanceUpdate": (
        0x098036A4,
        "? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 "
        "FD 03 00 91 ? ? ? D1 59 D0 3B D5 F3 03 00 AA F4 03 01 2A "
        "? ? ? F9 ? ? ? F8 ? ? ? F9 ? ? ? F9",
    ),
    "ClientInstanceGetLocalPlayer": (
        0x09808050,
        "? ? ? D1 ? ? ? A9 ? ? ? F9 ? ? ? 91 53 D0 3B D5 E8 03 00 AA "
        "? ? ? 91 ? ? ? F9 ? ? ? 91 ? ? ? F8 ? ? ? 95 ? ? ? 91 "
        "? ? ? 95 ? ? ? 36 ? ? ? 91 ? ? ? 52 ? ? ? 94",
    ),
}

VMA_RELOCS = {
    "12bc32f8": "12bc42b8",
    "12bc33c8": "98036a4",
    "12bc3400": "9808050",
    "12bc42c0": "2be2beb",
}


def parse_pattern(pattern: str) -> list[int | None]:
    return [None if token == "?" else int(token, 16) for token in pattern.split()]


def elf_vaddr_to_file_offset(data: bytes, vaddr: int) -> int:
    e_phoff = struct.unpack_from("<Q", data, 32)[0]
    e_phentsize = struct.unpack_from("<H", data, 54)[0]
    e_phnum = struct.unpack_from("<H", data, 56)[0]
    for index in range(e_phnum):
        off = e_phoff + index * e_phentsize
        if struct.unpack_from("<I", data, off)[0] != 1:  # PT_LOAD
            continue
        p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<QQQQ", data, off + 8)
        if p_vaddr <= vaddr < p_vaddr + p_filesz:
            return p_offset + (vaddr - p_vaddr)
    raise ValueError(f"virtual address is not file-backed: 0x{vaddr:x}")


def verify_signature(data: bytes, address: int, pattern: str) -> bool:
    expected = parse_pattern(pattern)
    offset = elf_vaddr_to_file_offset(data, address)
    actual = data[offset : offset + len(expected)]
    return len(actual) == len(expected) and all(
        wanted is None or wanted == got for wanted, got in zip(expected, actual)
    )


def readelf_build_id(path: Path) -> str | None:
    try:
        out = subprocess.check_output(
            ["readelf", "-n", str(path)], text=True, stderr=subprocess.STDOUT
        )
    except FileNotFoundError:
        raise SystemExit("readelf is required")
    except subprocess.CalledProcessError as exc:
        print(exc.output)
        raise SystemExit(exc.returncode)
    match = re.search(r"Build ID:\s*([0-9a-fA-F]+)", out)
    return match.group(1).lower() if match else None


def readelf_relocations(path: Path) -> str:
    try:
        return subprocess.check_output(
            ["readelf", "-rW", str(path)], text=True, stderr=subprocess.STDOUT
        )
    except FileNotFoundError:
        raise SystemExit("readelf is required")
    except subprocess.CalledProcessError as exc:
        print(exc.output)
        raise SystemExit(exc.returncode)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=Path)
    args = parser.parse_args()
    if not args.library.is_file():
        raise SystemExit(f"not found: {args.library}")

    data = args.library.read_bytes()
    actual = readelf_build_id(args.library)
    print(f"Build ID: {actual or '<missing>'}")
    if actual != EXPECTED_BUILD_ID:
        raise SystemExit(f"Build ID mismatch: expected {EXPECTED_BUILD_ID}")

    for name, (address, pattern) in SIGNATURES.items():
        ok = verify_signature(data, address, pattern)
        print(f"Signature {name}: {'OK' if ok else 'FAIL'} @ 0x{address:08x}")
        if not ok:
            raise SystemExit(f"signature mismatch: {name}")

    type_name_offset = data.find(EXPECTED_TYPE_NAME)
    if type_name_offset < 0:
        raise SystemExit("ClientInstance RTTI name not found")
    print(f"ClientInstance RTTI name: OK ({EXPECTED_TYPE_NAME[:-1].decode()})")

    relocations = readelf_relocations(args.library).lower()
    for location, target in VMA_RELOCS.items():
        pattern = rf"^0*{location}\s+.*?\s+0*{target}$"
        if not re.search(pattern, relocations, re.MULTILINE):
            raise SystemExit(f"ClientInstance vtable relocation mismatch: {location} -> {target}")
    print("ClientInstance vtable relocations: OK")
    print("Exact 1.26.52.3 target gate: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
