#!/usr/bin/env python3
"""Validate one app-only write against the live partition table, without a backup."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile


def factory_app(table: bytes, flash_bytes: int) -> tuple[int, int]:
    if len(table) != 0x1000:
        raise ValueError("live partition table must contain exactly 4096 bytes")
    entries = []
    terminated = False
    for index in range(0, len(table), 32):
        record = table[index:index + 32]
        magic = struct.unpack_from("<H", record)[0]
        if magic == 0xFFFF:
            terminated = True
            break
        if magic == 0xEBEB:
            if record[:16] != b"\xeb\xeb" + b"\xff" * 14:
                raise ValueError("invalid live partition checksum marker")
            if record[16:] != hashlib.md5(table[:index]).digest():
                raise ValueError("live partition table checksum differs")
            terminated = True
            break
        if magic != 0x50AA:
            raise ValueError("invalid live partition entry")
        _, kind, subtype, offset, size, _, _ = struct.unpack("<HBBII16sI", record)
        if not size or offset < 0x9000 or offset + size > flash_bytes:
            raise ValueError("live partition leaves the flash layout")
        if any(offset < old_offset + old_size and old_offset < offset + size
               for _, _, old_offset, old_size in entries):
            raise ValueError("live partitions overlap")
        entries.append((kind, subtype, offset, size))
    if not terminated:
        raise ValueError("live partition table has no terminator")
    apps = [(offset, size) for kind, subtype, offset, size in entries
            if kind == 0 and subtype == 0]
    if len(apps) != 1:
        raise ValueError("app-only route requires exactly one live factory app")
    return apps[0]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--offset", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--bytes", type=int, required=True)
    parser.add_argument("--flash-bytes", type=int, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="p4-live-layout-") as directory:
        table_path = Path(directory) / "partition-table.bin"
        result = subprocess.run([
            "esptool.py", "--chip", "esp32p4", "--port", args.port,
            "--before", "no_reset", "--after", "no_reset",
            "read_flash", "0x8000", "0x1000", str(table_path),
        ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, check=False)
        if result.returncode:
            print(re.sub(r"(?i)(?:[0-9a-f]{2}:){5}[0-9a-f]{2}",
                         "[identity redacted]", result.stdout))
            raise SystemExit("unable to read live partition table; no write performed")
        offset, size = factory_app(table_path.read_bytes(), args.flash_bytes)
        if args.offset != offset or args.bytes <= 0 or args.bytes > size:
            raise SystemExit("candidate app range differs from live factory partition; no write performed")
        print(f"Live factory app layout verified: offset={offset:#x} partition_bytes={size}")


if __name__ == "__main__":
    main()
