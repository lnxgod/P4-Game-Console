#!/usr/bin/env python3
"""Install hash-gated game content through the Console OS programming USB port."""

from __future__ import annotations

import argparse
import binascii
import glob
import hashlib
import struct
import sys
import time
from pathlib import Path

import serial

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True)


ROOT = Path(__file__).resolve().parents[1]
IDLE_BAUD = 115_200
QUAKE_BYTES = 18_689_235
QUAKE_SHA256 = "35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af"
MANIFEST_MAGIC = b"P4M1"
READY_MAGIC = b"P4R1"
HIGH_MAGIC = b"P4H1"
CHUNK_MAGIC = b"P4C1"
ACK_MAGIC = b"P4A1"
DONE_MAGIC = b"P4D1"
CONTENT_KIND_QUAKE_SHAREWARE = 1

STATUS_NAMES = {
    0: "ok",
    1: "bad-manifest",
    2: "storage-unavailable",
    3: "destination-occupied",
    4: "io-error",
    5: "sequence-error",
    6: "chunk-crc-error",
    7: "sha256-error",
    8: "transfer-timeout",
    9: "unsupported",
    10: "already-present",
}


class TransferError(RuntimeError):
    pass


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFF_FFFF


def validate_quake(path: Path) -> bytes:
    if not path.is_file() or path.is_symlink():
        raise TransferError(f"input must be one regular file: {path}")
    if path.stat().st_size != QUAKE_BYTES:
        raise TransferError(
            f"wrong Quake shareware size: expected {QUAKE_BYTES}, got {path.stat().st_size}"
        )
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    if digest.hexdigest() != QUAKE_SHA256:
        raise TransferError("input is not the exact Quake v1.06 shareware PAK")
    return digest.digest()


def make_manifest(size: int, digest: bytes) -> bytes:
    body = struct.pack(
        "<4sBBHI32s",
        MANIFEST_MAGIC,
        CONTENT_KIND_QUAKE_SHAREWARE,
        0,
        0,
        size,
        digest,
    )
    return body + struct.pack("<I", crc32(body))


def make_chunk(sequence: int, payload: bytes) -> bytes:
    if not payload or len(payload) > 4096:
        raise TransferError("chunk length is outside the protocol bound")
    return struct.pack(
        "<4sIHHI", CHUNK_MAGIC, sequence, len(payload), 0, crc32(payload)
    ) + payload


def detect_port() -> str:
    candidates = sorted(
        set(glob.glob("/dev/cu.wchusbserial*") + glob.glob("/dev/cu.usbserial-*"))
    )
    if len(candidates) != 1:
        rendered = ", ".join(candidates) if candidates else "none"
        raise TransferError(
            f"expected one connected badge programming port, found: {rendered}; use --port"
        )
    return candidates[0]


def open_port(path: str) -> serial.Serial:
    connection = serial.Serial()
    connection.port = path
    connection.baudrate = IDLE_BAUD
    connection.timeout = 0.1
    connection.write_timeout = 10
    connection.dtr = False
    connection.rts = False
    connection.open()
    connection.reset_input_buffer()
    return connection


def write_all(connection: serial.Serial, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        count = connection.write(data[offset:])
        if count <= 0:
            raise TransferError("USB serial write stopped")
        offset += count


def read_frame(
    connection: serial.Serial, marker: bytes, frame_bytes: int, timeout: float
) -> bytes:
    deadline = time.monotonic() + timeout
    buffered = bytearray()
    while time.monotonic() < deadline:
        block = connection.read(4096)
        if block:
            buffered.extend(block)
            location = buffered.find(marker)
            if location >= 0 and len(buffered) >= location + frame_bytes:
                return bytes(buffered[location : location + frame_bytes])
            if location < 0 and len(buffered) > 8192:
                del buffered[:-len(marker)]
        else:
            time.sleep(0.01)
    raise TransferError(f"badge did not return {marker.decode('ascii')} in time")


def status_name(status: int) -> str:
    return STATUS_NAMES.get(status, f"unknown-{status}")


def wait_for_quake_ready(connection: serial.Serial, timeout: float = 600.0) -> bool:
    connection.baudrate = IDLE_BAUD
    connection.reset_input_buffer()
    deadline = time.monotonic() + timeout
    buffered = bytearray()
    while time.monotonic() < deadline:
        block = connection.read(4096)
        if block:
            buffered.extend(block)
            if b"CONTENT_READY" in buffered and b"quake_shareware=1" in buffered:
                return True
            if len(buffered) > 64 * 1024:
                del buffered[:32 * 1024]
        else:
            time.sleep(0.02)
    return False


def install_quake(path: Path, port: str) -> None:
    print(f"P4_USB validating {path}")
    digest = validate_quake(path)
    manifest = make_manifest(QUAKE_BYTES, digest)

    with open_port(port) as connection:
        print(f"P4_USB connecting port={port} baud={IDLE_BAUD}")
        time.sleep(0.25)
        write_all(connection, manifest)
        # An already-installed PAK is re-hashed before the badge reports that
        # result. Slow 1-bit cards can need several minutes for this gate.
        ready = read_frame(connection, READY_MAGIC, 11, 600.0)
        status = ready[4]
        transfer_baud = struct.unpack_from("<I", ready, 5)[0]
        chunk_bytes = struct.unpack_from("<H", ready, 9)[0]
        if status == 10:
            print("P4_USB INSTALLED result=already-present hash=verified")
            return
        if status != 0:
            raise TransferError(f"badge rejected manifest: {status_name(status)}")
        if transfer_baud != 921_600 or not 1 <= chunk_bytes <= 4096:
            raise TransferError(
                f"badge proposed unsupported transport: baud={transfer_baud} chunk={chunk_bytes}"
            )

        connection.baudrate = transfer_baud
        read_frame(connection, HIGH_MAGIC, 4, 5.0)
        started = time.monotonic()
        last_percent = -1
        sequence = 0
        sent = 0
        with path.open("rb") as stream:
            while True:
                payload = stream.read(chunk_bytes)
                if not payload:
                    break
                write_all(connection, make_chunk(sequence, payload))
                ack = read_frame(connection, ACK_MAGIC, 9, 20.0)
                ack_sequence = struct.unpack_from("<I", ack, 4)[0]
                ack_status = ack[8]
                if ack_sequence != sequence or ack_status != 0:
                    raise TransferError(
                        "badge rejected chunk "
                        f"{sequence}: ack={ack_sequence} status={status_name(ack_status)}"
                    )
                sent += len(payload)
                sequence += 1
                percent = sent * 100 // QUAKE_BYTES
                if percent >= last_percent + 5 or percent == 100:
                    elapsed = max(time.monotonic() - started, 0.001)
                    rate_kib = sent / elapsed / 1024
                    print(
                        f"P4_USB progress={percent:3d}% bytes={sent}/{QUAKE_BYTES} "
                        f"rate={rate_kib:.0f}KiB/s"
                    )
                    last_percent = percent

        # Final activation includes a complete SD readback hash. Do not turn a
        # slow card into a false host-side failure while that proof is active.
        done = read_frame(connection, DONE_MAGIC, 41, 900.0)
        done_status = done[4]
        installed_bytes = struct.unpack_from("<I", done, 5)[0]
        installed_digest = done[9:41]
        if done_status != 0:
            raise TransferError(f"badge failed activation: {status_name(done_status)}")
        if installed_bytes != QUAKE_BYTES or installed_digest != digest:
            raise TransferError("badge completion proof does not match the source")
        elapsed = time.monotonic() - started
        print(
            f"P4_USB ACTIVATED bytes={installed_bytes} sha256={digest.hex()} "
            f"seconds={elapsed:.1f}"
        )
        if not wait_for_quake_ready(connection):
            raise TransferError(
                "content activated, but the reboot log did not confirm Quake before timeout"
            )
        print("P4_USB PASS rebooted=1 quake_shareware=1 launcher_enabled=1")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description="Copy verified game content to a running P4 Console OS over USB."
    )
    subparsers = result.add_subparsers(dest="kind", required=True)
    quake = subparsers.add_parser(
        "quake", help="install the exact Quake v1.06 shareware PAK"
    )
    quake.add_argument(
        "input",
        nargs="?",
        type=Path,
        default=ROOT / "local-data/quake/id1/pak0.pak",
    )
    quake.add_argument("--port")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        if args.kind == "quake":
            install_quake(args.input.resolve(), args.port or detect_port())
    except (OSError, serial.SerialException, TransferError) as error:
        print(f"P4_USB FAILED reason={error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
