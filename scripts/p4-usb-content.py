#!/usr/bin/env python3
"""Install hash-gated game content through the Console OS programming USB port."""

from __future__ import annotations

import argparse
import binascii
from dataclasses import dataclass
import glob
import hashlib
import json
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
DOOM_BYTES = 4_196_020
DOOM_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
CHEX_WAD_BYTES = 12_361_532
CHEX_WAD_SHA256 = "d8eb5277918883f490fb1a4be3c9a8588df2dbaee6dc4beb8df4929148bbffb1"
CHEX_DEH_BYTES = 20_367
CHEX_DEH_SHA256 = "8c0345089fb227fa7f71c25a6c6e31ff5bd4bea0580f286cd74e05918d72dd40"
MANIFEST_MAGIC = b"P4M1"
READY_MAGIC = b"P4R1"
HIGH_MAGIC = b"P4H1"
CHUNK_MAGIC = b"P4C1"
ACK_MAGIC = b"P4A1"
DONE_MAGIC = b"P4D1"
CONTENT_KIND_QUAKE_SHAREWARE = 1
CONTENT_KIND_DOOM_SHAREWARE = 2
CONTENT_KIND_CHEX_QUEST_WAD = 3
CONTENT_KIND_CHEX_QUEST_DEH = 4
CONTENT_KIND_FREEDOOM2 = 5

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


@dataclass(frozen=True)
class ContentSpec:
    command: str
    kind: int
    bytes: int
    sha256: str
    default_path: Path


CONTENT_SPECS = {
    "doom": ContentSpec(
        command="doom",
        kind=CONTENT_KIND_DOOM_SHAREWARE,
        bytes=DOOM_BYTES,
        sha256=DOOM_SHA256,
        default_path=ROOT / "local-data/doom/doom1.wad",
    ),
    "quake": ContentSpec(
        command="quake",
        kind=CONTENT_KIND_QUAKE_SHAREWARE,
        bytes=QUAKE_BYTES,
        sha256=QUAKE_SHA256,
        default_path=ROOT / "local-data/quake/id1/pak0.pak",
    ),
    "chex-wad": ContentSpec(
        command="chex-wad",
        kind=CONTENT_KIND_CHEX_QUEST_WAD,
        bytes=CHEX_WAD_BYTES,
        sha256=CHEX_WAD_SHA256,
        default_path=ROOT / "local-data/doom/chex.wad",
    ),
    "chex-deh": ContentSpec(
        command="chex-deh",
        kind=CONTENT_KIND_CHEX_QUEST_DEH,
        bytes=CHEX_DEH_BYTES,
        sha256=CHEX_DEH_SHA256,
        default_path=ROOT / "local-data/doom/chex.deh",
    ),
}


ARENA_BUNDLE = json.loads((ROOT / "third_party/game-data.json").read_text())["game_changers_ai_bundle"]
for arena_file in ARENA_BUNDLE["files"]:
    CONTENT_SPECS[arena_file["command"]] = ContentSpec(
        command=arena_file["command"], kind=arena_file["usb_kind"],
        bytes=arena_file["size_bytes"], sha256=arena_file["sha256"],
        default_path=ROOT / arena_file["local_path"],
    )


def arena_inputs(iwad: Path, pack: Path, dwango: Path):
    result = []
    for entry in ARENA_BUNDLE["files"]:
        spec = CONTENT_SPECS[entry["command"]]
        path = iwad if entry["symbol"] == "BASE" else (dwango if entry["symbol"].startswith("DWANGO") else pack) / entry["filename"]
        result.append((spec, path.resolve()))
    # No serial connection or device writes until every local input passes.
    for spec, path in result:
        validate_content(path, spec)
    return result


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFF_FFFF


def validate_content(path: Path, spec: ContentSpec) -> bytes:
    if not path.is_file() or path.is_symlink():
        raise TransferError(f"input must be one regular file: {path}")
    if path.stat().st_size != spec.bytes:
        raise TransferError(
            f"wrong {spec.command} content size: "
            f"expected {spec.bytes}, got {path.stat().st_size}"
        )
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    if digest.hexdigest() != spec.sha256:
        raise TransferError(
            f"input is not the exact supported {spec.command} content"
        )
    return digest.digest()


def make_manifest(kind: int, size: int, digest: bytes) -> bytes:
    body = struct.pack(
        "<4sBBHI32s",
        MANIFEST_MAGIC,
        kind,
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
    connection.timeout = 0.02
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


class WireReader:
    """Retain trailing bytes when USB delivers multiple protocol frames at once."""

    def __init__(self, connection: serial.Serial) -> None:
        self.connection = connection
        self.buffer = bytearray()

    def frame(self, marker: bytes, frame_bytes: int, timeout: float,
              startup_manifest: bytes | None = None) -> bytes:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            location = self.buffer.find(marker)
            if location >= 0 and len(self.buffer) >= location + frame_bytes:
                result = bytes(self.buffer[location:location + frame_bytes])
                del self.buffer[:location + frame_bytes]
                return result
            # Native Serial/JTAG may reboot on port-open. A manifest sent
            # before the driver starts is lost; retry only upon an explicit
            # service-ready marker, never while the card is hashing a file.
            if startup_manifest is not None and b"P4_USB_CONTENT READY" in self.buffer:
                write_all(self.connection, startup_manifest)
                startup_manifest = None
            if location < 0 and len(self.buffer) > 65536:
                del self.buffer[:-256]
            block = self.connection.read(max(1, self.connection.in_waiting))
            if block:
                self.buffer.extend(block)
            else:
                time.sleep(0.005)
        raise TransferError(f"badge did not return {marker.decode('ascii')} in time")


def status_name(status: int) -> str:
    return STATUS_NAMES.get(status, f"unknown-{status}")


def wait_for_content_ready(
    connection: serial.Serial, reader: WireReader, timeout: float = 600.0
) -> bool:
    # The activation response already proves the complete on-card digest.
    # Demand-validated boards intentionally do not hash every WAD at boot.
    connection.baudrate = IDLE_BAUD
    try:
        reader.frame(b"P4_USB_CONTENT READY", len(b"P4_USB_CONTENT READY"), timeout)
        return True
    except TransferError:
        return False


def install_content(spec: ContentSpec, path: Path, port: str) -> None:
    print(f"P4_H1 validating kind={spec.command} path={path}")
    digest = validate_content(path, spec)
    manifest = make_manifest(spec.kind, spec.bytes, digest)

    with open_port(port) as connection:
        print(f"P4_H1 connecting port={port} baud={IDLE_BAUD}")
        reader = WireReader(connection)
        time.sleep(0.25)
        write_all(connection, manifest)
        # Already-installed content is re-hashed before the badge reports that
        # result. Slow 1-bit cards can need several minutes for this gate.
        ready = reader.frame(READY_MAGIC, 11, 600.0, startup_manifest=manifest)
        status = ready[4]
        transfer_baud = struct.unpack_from("<I", ready, 5)[0]
        chunk_bytes = struct.unpack_from("<H", ready, 9)[0]
        if status == 10:
            print("P4_H1 INSTALLED result=already-present hash=verified")
            return
        if status != 0:
            raise TransferError(f"badge rejected manifest: {status_name(status)}")
        if transfer_baud != 921_600 or not 1 <= chunk_bytes <= 4096:
            raise TransferError(
                f"badge proposed unsupported transport: baud={transfer_baud} chunk={chunk_bytes}"
            )

        connection.baudrate = transfer_baud
        reader.frame(HIGH_MAGIC, 4, 5.0)
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
                ack = reader.frame(ACK_MAGIC, 9, 20.0)
                ack_sequence = struct.unpack_from("<I", ack, 4)[0]
                ack_status = ack[8]
                if ack_sequence != sequence or ack_status != 0:
                    raise TransferError(
                        "badge rejected chunk "
                        f"{sequence}: ack={ack_sequence} status={status_name(ack_status)}"
                    )
                sent += len(payload)
                sequence += 1
                percent = sent * 100 // spec.bytes
                if percent >= last_percent + 5 or percent == 100:
                    elapsed = max(time.monotonic() - started, 0.001)
                    rate_kib = sent / elapsed / 1024
                    print(
                        f"P4_H1 progress={percent:3d}% bytes={sent}/{spec.bytes} "
                        f"rate={rate_kib:.0f}KiB/s"
                    )
                    last_percent = percent

        # Final activation includes a complete SD readback hash. Do not turn a
        # slow card into a false host-side failure while that proof is active.
        done = reader.frame(DONE_MAGIC, 41, 900.0)
        done_status = done[4]
        installed_bytes = struct.unpack_from("<I", done, 5)[0]
        installed_digest = done[9:41]
        if done_status != 0:
            raise TransferError(f"badge failed activation: {status_name(done_status)}")
        if installed_bytes != spec.bytes or installed_digest != digest:
            raise TransferError("badge completion proof does not match the source")
        elapsed = time.monotonic() - started
        print(
            f"P4_H1 ACTIVATED bytes={installed_bytes} sha256={digest.hex()} "
            f"seconds={elapsed:.1f}"
        )
        if not wait_for_content_ready(connection, reader):
            raise TransferError(
                "content activated, but the reboot log did not confirm it before timeout"
            )
        print(f"P4_H1 PASS rebooted=1 transfer_service=ready kind={spec.command} hash=verified")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description=(
            "Copy verified game content to a running P4 Console OS through "
            "the H1 USB-UART port."
        )
    )
    subparsers = result.add_subparsers(dest="kind", required=True)
    for command, spec in CONTENT_SPECS.items():
        content = subparsers.add_parser(
            command,
            help=f"install the exact supported {command} game data",
        )
        content.add_argument(
            "input", nargs="?", type=Path, default=spec.default_path
        )
        content.add_argument("--port")
    arena = subparsers.add_parser("game-changers-ai", help="install the verified Freedoom + Pure Hell + DWANGO 5 bundle and notices")
    arena.add_argument("input", nargs="?", type=Path, default=CONTENT_SPECS["freedoom2"].default_path)
    arena.add_argument("--dwango", type=Path, default=CONTENT_SPECS["dwango5"].default_path.parent)
    arena.add_argument("--pack", type=Path, default=CONTENT_SPECS["pure-hell"].default_path.parent)
    arena.add_argument("--port")
    chex = subparsers.add_parser(
        "chex",
        help="install the verified CHEX.WAD and CHEX.DEH pair",
    )
    chex.add_argument(
        "wad", nargs="?", type=Path,
        default=CONTENT_SPECS["chex-wad"].default_path,
    )
    chex.add_argument(
        "deh", nargs="?", type=Path,
        default=CONTENT_SPECS["chex-deh"].default_path,
    )
    chex.add_argument("--port")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        arena_files = arena_inputs(args.input, args.pack, args.dwango) if args.kind == "game-changers-ai" else []
        port = args.port or detect_port()
        if arena_files:
            for spec, path in arena_files:
                install_content(spec, path, port)
        elif args.kind == "chex":
            install_content(
                CONTENT_SPECS["chex-wad"], args.wad.resolve(), port
            )
            install_content(
                CONTENT_SPECS["chex-deh"], args.deh.resolve(), port
            )
        else:
            spec = CONTENT_SPECS[args.kind]
            install_content(spec, args.input.resolve(), port)
    except (OSError, serial.SerialException, TransferError) as error:
        print(f"P4_H1 FAILED reason={error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
