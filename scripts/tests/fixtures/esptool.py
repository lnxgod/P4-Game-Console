#!/usr/bin/env python3
"""Deterministic esptool read_flash mock for app-readback host tests."""

from __future__ import annotations

import os
import pathlib
import sys


RAW_TEST_MAC = "aa:bb:cc:dd:ee:ff"


def required_env(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        raise SystemExit(f"missing mock environment variable: {name}")
    return value


def main() -> None:
    args = sys.argv[1:]
    try:
        baud = args[args.index("--baud") + 1]
        before = args[args.index("--before") + 1]
        after = args[args.index("--after") + 1]
        command_index = args.index("read_flash")
        offset = int(args[command_index + 1], 0)
        count = int(args[command_index + 2], 0)
        output = pathlib.Path(args[command_index + 3])
    except (ValueError, IndexError) as error:
        raise SystemExit(f"unsupported mock invocation: {args!r}: {error}")

    source = pathlib.Path(required_env("P4_MOCK_READBACK_SOURCE"))
    base_offset = int(required_env("P4_MOCK_READBACK_BASE_OFFSET"), 0)
    log = pathlib.Path(required_env("P4_MOCK_READBACK_LOG"))
    mode = os.environ.get("P4_MOCK_READBACK_MODE", "success")
    fail_baud = os.environ.get("P4_MOCK_READBACK_FAIL_BAUD", "")
    with log.open("a", encoding="utf-8") as stream:
        stream.write(f"{offset} {count} {baud} {mode} {before} {after}\n")

    print(f"MAC: {RAW_TEST_MAC}")
    print(f"Mock read_flash offset={offset} bytes={count} baud={baud}")
    if mode == "transport" or baud == fail_baud:
        raise SystemExit(2)

    source_offset = offset - base_offset
    data = source.read_bytes()[source_offset : source_offset + count]
    if len(data) != count:
        raise SystemExit("mock source range is short")
    if mode == "truncate":
        data = data[:-1]
    elif mode == "corrupt":
        data = bytes([data[0] ^ 0x80]) + data[1:]
    elif mode != "success":
        raise SystemExit(f"unknown mock mode: {mode}")
    output.write_bytes(data)


if __name__ == "__main__":
    main()
