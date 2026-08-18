#!/usr/bin/env python3
"""Relay validated P4MP frames between two Console OS H1 serial links."""

from __future__ import annotations

import argparse
import dataclasses
import struct
import sys
import time
import zlib


MAGIC = b"P4MP"
VERSION = 1
HEADER_BYTES = 28
TRAILER_BYTES = 4
MAX_PAYLOAD_BYTES = 1024
MAX_DATAGRAM_BYTES = HEADER_BYTES + MAX_PAYLOAD_BYTES + TRAILER_BYTES
VALID_PAYLOAD_LENGTHS = {
    1: {0},
    2: range(40, 129),
    3: range(36, 65),
    4: {16},
    5: {24},
    6: {12},
    7: {8},
    8: {8},
    9: {2},
    10: {2},
}


def frame_valid(frame: bytes) -> bool:
    if not HEADER_BYTES + TRAILER_BYTES <= len(frame) <= MAX_DATAGRAM_BYTES:
        return False
    if frame[:4] != MAGIC or frame[4] != VERSION:
        return False
    packet_type = frame[5]
    if packet_type not in VALID_PAYLOAD_LENGTHS:
        return False
    flags, session_id, peer_id, sequence, _ack, payload, reserved = (
        struct.unpack_from("<HIIIIHH", frame, 6)
    )
    if flags != 0 or reserved != 0:
        return False
    if payload not in VALID_PAYLOAD_LENGTHS[packet_type]:
        return False
    if len(frame) != HEADER_BYTES + payload + TRAILER_BYTES:
        return False
    if sequence == 0:
        return False
    if packet_type == 1:
        if session_id != 0 or peer_id != 0:
            return False
    elif session_id == 0 or peer_id == 0:
        return False
    expected_crc = struct.unpack_from("<I", frame, len(frame) - 4)[0]
    return expected_crc == zlib.crc32(frame[:-4]) & 0xFFFFFFFF


@dataclasses.dataclass
class FrameDecoder:
    buffer: bytearray = dataclasses.field(default_factory=bytearray)
    discarded_bytes: int = 0
    dropped_frames: int = 0

    def feed(self, data: bytes) -> list[bytes]:
        if not isinstance(data, bytes):
            raise TypeError("serial input must be bytes")
        self.buffer.extend(data)
        frames: list[bytes] = []
        while True:
            magic_at = self.buffer.find(MAGIC)
            if magic_at < 0:
                keep = 0
                for length in range(1, min(len(MAGIC), len(self.buffer) + 1)):
                    if self.buffer[-length:] == MAGIC[:length]:
                        keep = length
                self.discarded_bytes += len(self.buffer) - keep
                if keep:
                    del self.buffer[:-keep]
                else:
                    self.buffer.clear()
                break
            if magic_at:
                self.discarded_bytes += magic_at
                del self.buffer[:magic_at]
            if len(self.buffer) < HEADER_BYTES:
                break
            payload = struct.unpack_from("<H", self.buffer, 24)[0]
            if payload > MAX_PAYLOAD_BYTES:
                self.dropped_frames += 1
                self.discarded_bytes += 1
                del self.buffer[0]
                continue
            expected = HEADER_BYTES + payload + TRAILER_BYTES
            if len(self.buffer) < expected:
                break
            frame = bytes(self.buffer[:expected])
            del self.buffer[:expected]
            if frame_valid(frame):
                frames.append(frame)
            else:
                self.dropped_frames += 1
        if len(self.buffer) > MAX_DATAGRAM_BYTES:
            self.discarded_bytes += len(self.buffer)
            self.buffer.clear()
            self.dropped_frames += 1
        return frames


@dataclasses.dataclass
class Link:
    name: str
    serial: object
    decoder: FrameDecoder = dataclasses.field(default_factory=FrameDecoder)
    frames_received: int = 0
    frames_forwarded: int = 0


def open_serial(path: str, baud: int):
    try:
        import serial
    except ImportError as error:
        raise SystemExit(
            "pyserial is required; activate the repository ESP-IDF environment"
        ) from error
    device = serial.Serial(
        port=None,
        baudrate=baud,
        timeout=0,
        write_timeout=1,
        exclusive=True,
    )
    device.dtr = False
    device.rts = False
    device.port = path
    device.open()
    if device.dtr or device.rts:
        device.close()
        raise RuntimeError(f"reset controls became active on {path}")
    return device


def read_available(link: Link) -> bytes:
    waiting = min(int(link.serial.in_waiting), 4096)
    return link.serial.read(waiting if waiting > 0 else 1)


def forward(source: Link, destination: Link) -> None:
    data = read_available(source)
    if not data:
        return
    for frame in source.decoder.feed(data):
        source.frames_received += 1
        written = destination.serial.write(frame)
        if written != len(frame):
            raise RuntimeError(
                f"short write from {source.name} to {destination.name}"
            )
        destination.serial.flush()
        source.frames_forwarded += 1


def run(left_path: str, right_path: str, baud: int) -> int:
    if left_path == right_path:
        raise ValueError("left and right serial ports must differ")
    left_device = open_serial(left_path, baud)
    try:
        right_device = open_serial(right_path, baud)
    except BaseException:
        left_device.close()
        raise
    left = Link("left", left_device)
    right = Link("right", right_device)
    print(
        f"P4MP relay ready left={left_path} right={right_path} baud={baud}",
        file=sys.stderr,
    )
    next_status = time.monotonic() + 5.0
    try:
        while True:
            forward(left, right)
            forward(right, left)
            now = time.monotonic()
            if now >= next_status:
                print(
                    "P4MP relay "
                    f"left_rx={left.frames_received} "
                    f"left_drop={left.decoder.dropped_frames} "
                    f"right_rx={right.frames_received} "
                    f"right_drop={right.decoder.dropped_frames}",
                    file=sys.stderr,
                )
                next_status = now + 5.0
            time.sleep(0.002)
    except KeyboardInterrupt:
        return 0
    finally:
        right_device.close()
        left_device.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Relay CRC-checked P4MP frames between two H1 USB-UART ports; "
            "diagnostic log noise is discarded"
        )
    )
    parser.add_argument("--left", required=True)
    parser.add_argument("--right", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    if not 9600 <= args.baud <= 2_000_000:
        parser.error("baud must be in [9600, 2000000]")
    return run(args.left, args.right, args.baud)


if __name__ == "__main__":
    raise SystemExit(main())
