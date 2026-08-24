#!/usr/bin/env python3
"""Focused host tests for the two-console P4MP serial relay."""

from __future__ import annotations

import importlib.util
import pathlib
import struct
import sys
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts/p4-multiplayer-relay.py"
SPEC = importlib.util.spec_from_file_location("p4_multiplayer_relay", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
relay = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = relay
SPEC.loader.exec_module(relay)


def packet(packet_type: int = 5, payload: bytes = bytes(24)) -> bytes:
    header = struct.pack(
        "<4sBBHIIIIHH",
        b"P4MP", 1, packet_type, 0, 7, 2, 1, 0, len(payload), 0,
    )
    body = header + payload
    return body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def main() -> None:
    expected = packet()
    assert relay.frame_valid(expected)
    decoder = relay.FrameDecoder()
    assert decoder.feed(b"boot log\r\nP4") == []
    assert decoder.feed(b"MP" + expected[4:17]) == []
    assert decoder.feed(expected[17:]) == [expected]
    assert decoder.discarded_bytes == len(b"boot log\r\n")

    broken = bytearray(expected)
    broken[-1] ^= 0x80
    assert decoder.feed(bytes(broken)) == []
    assert decoder.dropped_frames == 1

    oversized = bytearray(28)
    oversized[:6] = b"P4MP\x01\x05"
    struct.pack_into("<H", oversized, 24, 1025)
    assert decoder.feed(bytes(oversized) + expected) == [expected]
    assert decoder.dropped_frames == 2

    discover_header = struct.pack(
        "<4sBBHIIIIHH", b"P4MP", 1, 1, 0, 0, 0, 1, 0, 0, 0
    )
    discover = discover_header + struct.pack(
        "<I", zlib.crc32(discover_header) & 0xFFFFFFFF
    )
    assert relay.frame_valid(discover)
    assert relay.discover_frame() == discover
    assert relay.frame_valid(packet(2, bytes(128)))
    assert relay.frame_valid(packet(4, bytes(24)))
    assert relay.frame_valid(packet(11, b"x"))
    assert relay.frame_valid(packet(11, bytes(64)))
    assert not relay.frame_valid(packet(2, bytes(120)))
    assert not relay.frame_valid(packet(4, bytes(16)))
    assert not relay.frame_valid(packet(11, b""))
    assert not relay.frame_valid(packet(11, bytes(65)))
    print("p4 multiplayer relay tests PASS")


if __name__ == "__main__":
    main()
