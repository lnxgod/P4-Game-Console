#!/usr/bin/env python3
"""Push or pull verified files through a running P4 Console OS programming USB port."""

from __future__ import annotations

import argparse
from contextlib import nullcontext
import importlib.util
import re
import binascii
import glob
import hashlib
import os
from pathlib import Path
import secrets
import struct
import sys
import time

import serial


IDLE_BAUD = 115_200
TRANSFER_BAUD = 921_600
CHUNK_BYTES = 4096
P4G_MAX_BYTES = 512 * 1024
EXCHANGE_MAX_BYTES = 8 * 1024 * 1024
NAME_BYTES = 40

REQUEST_MAGIC = b"P4F1"
READY_MAGIC = b"P4R2"
HIGH_MAGIC = b"P4H2"
CHUNK_MAGIC = b"P4C2"
ACK_MAGIC = b"P4A2"
DONE_MAGIC = b"P4D2"

USB_DRIVE_REQUEST_MAGIC = b"P4U1"
USB_DRIVE_RESPONSE_MAGIC = b"P4V1"
USB_DRIVE_PROTOCOL_VERSION = 1
USB_DRIVE_REQUEST_BYTES = 20
USB_DRIVE_RESPONSE_BYTES = 28
USB_DRIVE_COMMAND_STATUS = 1
USB_DRIVE_COMMAND_SET_MODE = 2
USB_DRIVE_STATUS_RETRY_ATTEMPTS = 30
USB_DRIVE_STATUS_ATTEMPT_TIMEOUT = 1.0

DIRECTION_UPLOAD = 1
DIRECTION_DOWNLOAD = 2
CLASS_P4G = 1
CLASS_EXCHANGE = 2
CLASS_P4R = 3
CLASS_P4CART = 4
FILE_CLASSES = {"p4g": CLASS_P4G, "exchange": CLASS_EXCHANGE, "p4r": CLASS_P4R, "p4cart": CLASS_P4CART}
FLAG_REPLACE = 1

STATUS_OK = 0
STATUS_CRC = 6
STATUS_ALREADY_PRESENT = 10
STATUS_NAMES = {
    0: "ok",
    1: "bad-request",
    2: "storage-unavailable",
    3: "destination-occupied",
    4: "io-error",
    5: "sequence-error",
    6: "chunk-crc-error",
    7: "sha256-error",
    8: "transfer-timeout",
    9: "unsupported",
    10: "already-present",
    11: "bad-name",
    12: "bad-package",
    13: "not-found",
    14: "too-large",
    15: "busy",
}

USB_DRIVE_RESULT_NAMES = {
    0: "ok",
    1: "bad-request",
    2: "unavailable",
    3: "busy",
    4: "denied",
    5: "transition-failed",
    6: "session-required",
    7: "session-expired",
    8: "crc-error",
    9: "unsupported",
    10: "stale-sequence",
}


class TransferError(RuntimeError):
    pass


class UsbDriveResponseTimeout(TransferError):
    """The bounded control response was absent; a safe status retry is allowed."""


def bounded_done_timeout(value: str) -> float:
    timeout = float(value)
    if not 30.0 <= timeout <= 600.0:
        raise argparse.ArgumentTypeError("must be between 30 and 600 seconds")
    return timeout


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFF_FFFF


def status_name(status: int) -> str:
    return STATUS_NAMES.get(status, f"unknown-{status}")


def detect_port() -> str:
    candidates = sorted(
        set(glob.glob("/dev/cu.wchusbserial*") + glob.glob("/dev/cu.usbserial-*"))
    )
    if len(candidates) != 1:
        rendered = ", ".join(candidates) if candidates else "none"
        raise TransferError(
            f"expected one P4 H1 port, found: {rendered}; select one with --port"
        )
    return candidates[0]


def open_port(path: str) -> serial.Serial:
    connection = serial.Serial()
    connection.port = path
    connection.baudrate = IDLE_BAUD
    connection.timeout = 0.02
    connection.write_timeout = 15
    connection.dsrdtr = False
    connection.rtscts = False
    connection.dtr = False
    connection.rts = False
    connection.open()
    # Some USB-UART bridges transiently change modem-control lines on open.
    # Reassert the inactive state before accepting any H1 traffic.
    connection.dtr = False
    connection.rts = False
    connection.reset_input_buffer()
    return connection


def write_all(connection: serial.Serial, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        count = connection.write(data[offset:])
        if count is None or count <= 0:
            raise TransferError("H1 serial write stopped")
        offset += count


class WireReader:
    def __init__(self, connection: serial.Serial) -> None:
        self.connection = connection
        self.buffer = bytearray()

    def _fill(self, deadline: float) -> None:
        if time.monotonic() >= deadline:
            raise TransferError("H1 response timed out")
        block = self.connection.read(max(1, self.connection.in_waiting))
        if block:
            self.buffer.extend(block)
        else:
            time.sleep(0.005)

    def frame(self, marker: bytes, frame_bytes: int, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            location = self.buffer.find(marker)
            if location >= 0 and len(self.buffer) >= location + frame_bytes:
                frame = bytes(self.buffer[location : location + frame_bytes])
                del self.buffer[: location + frame_bytes]
                return frame
            if location < 0 and len(self.buffer) > 64 * 1024:
                del self.buffer[: -len(marker)]
            try:
                self._fill(deadline)
            except TransferError:
                break
        diagnostic = bytes(self.buffer[-8192:]).decode("ascii", "replace")
        stage_lines = [
            line.strip()
            for line in diagnostic.splitlines()
            if "P4_FILE_TRANSFER ACTIVATE" in line
        ]
        if stage_lines:
            detail = f"; last device stage: {stage_lines[-1]}"
        else:
            tail = bytes(self.buffer[-256:]).hex()
            detail = f"; buffered_bytes={len(self.buffer)} tail_hex={tail or 'empty'}"
        raise TransferError(
            f"badge did not return {marker.decode()} in time{detail}"
        )

    def exact(self, size: int, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        while len(self.buffer) < size:
            self._fill(deadline)
        result = bytes(self.buffer[:size])
        del self.buffer[:size]
        return result


def game_name_valid(name: str, extension: str) -> bool:
    if not name.endswith(extension) or not len(extension) < len(name) < NAME_BYTES:
        return False
    base = name[:-len(extension)]
    return (
        bool(base)
        and len(base) <= 32
        and base[0].isalnum()
        and base.isascii()
        and all(byte.isupper() or byte.isdigit() or byte in "_-" for byte in base)
    )


def p4g_name_valid(name: str) -> bool:
    return game_name_valid(name, ".P4G")


def exchange_name_valid(name: str) -> bool:
    return (
        0 < len(name) < NAME_BYTES
        and name.isascii()
        and name[0].isalnum()
        and ".." not in name
        and not name.startswith("P4FT.")
        and all(byte.isalnum() or byte in "_.-" for byte in name)
    )


def checked_remote_name(name: str, file_class: int) -> str:
    extension = {CLASS_P4G: ".P4G", CLASS_P4R: ".P4R", CLASS_P4CART: ".P4CART"}.get(file_class)
    valid = game_name_valid(name, extension) if extension else exchange_name_valid(name)
    if not valid:
        kind = f"uppercase NAME{extension}" if extension else "safe basename"
        raise TransferError(f"invalid remote filename {name!r}; expected {kind}")
    return name


def validate_p4g_host(data: bytes) -> None:
    if not 256 < len(data) <= P4G_MAX_BYTES or data[:8] != b"P4GAME1\0":
        raise TransferError("input is not a bounded P4GAME1 cartridge")
    header_bytes, package_bytes, payload_offset, payload_bytes = struct.unpack_from(
        "<IIII", data, 8
    )
    format_version, api_version = struct.unpack_from("<II", data, 24)
    if (
        header_bytes != 256
        or package_bytes != len(data)
        or payload_offset != 256
        or payload_bytes != len(data) - 256
        or format_version != 1
        or api_version != 1
    ):
        raise TransferError("P4G header/layout/version is invalid")
    expected_payload = data[48:80]
    actual_payload = hashlib.sha256(data[payload_offset:]).digest()
    if actual_payload != expected_payload:
        raise TransferError("P4G embedded payload digest is invalid")


def validate_resource_host(data: bytes) -> None:
    if not 128 < len(data) <= EXCHANGE_MAX_BYTES or data[:8] != b"P4RES01\0":
        raise TransferError("input is not a bounded P4RES01 resource")
    header, total, offset, payload, version, flags = struct.unpack_from("<6I", data, 8)
    if (header != 128 or total != len(data) or offset != 128 or
        payload != len(data) - 128 or version != 1 or flags or any(data[112:128])):
        raise TransferError("P4R layout/version is invalid")
    game_id, separator, padding = data[64:112].partition(b"\0")
    if not separator or any(padding) or re.fullmatch(rb"[a-z][a-z0-9.-]{2,46}", game_id) is None:
        raise TransferError("P4R game identity is invalid")
    if data[32:64] != hashlib.sha256(data[128:]).digest():
        raise TransferError("P4R embedded payload digest is invalid")


def validate_cart_host(path: Path) -> None:
    tool = Path(__file__).resolve().parents[1] / "game-platform/scripts/p4cart.py"
    spec = importlib.util.spec_from_file_location("p4cart_transfer_validator", tool)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    try:
        module.inspect_cart(path)
    except (ValueError, OSError) as error:
        raise TransferError(f"P4CART validation failed: {error}") from error


def validate_upload(path: Path, file_class: int) -> tuple[int, bytes]:
    if not path.is_file() or path.is_symlink():
        raise TransferError(f"input must be one regular file: {path}")
    size = path.stat().st_size
    maximum = P4G_MAX_BYTES if file_class == CLASS_P4G else EXCHANGE_MAX_BYTES
    if not 0 < size <= maximum:
        raise TransferError(f"input size {size} exceeds the {maximum}-byte bound")
    data = path.read_bytes() if file_class in (CLASS_P4G, CLASS_P4R) else None
    if file_class == CLASS_P4CART:
        validate_cart_host(path)
    if data is not None:
        if file_class == CLASS_P4G:
            validate_p4g_host(data)
        else:
            validate_resource_host(data)
        digest = hashlib.sha256(data).digest()
    else:
        sha256 = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                sha256.update(block)
        digest = sha256.digest()
    return size, digest


def make_request(
    direction: int,
    file_class: int,
    name: str,
    size: int = 0,
    digest: bytes = b"\0" * 32,
    replace: bool = False,
) -> bytes:
    encoded = name.encode("ascii")
    if len(encoded) >= NAME_BYTES or len(digest) != 32:
        raise TransferError("request fields exceed protocol bounds")
    name_field = encoded + b"\0" * (NAME_BYTES - len(encoded))
    body = struct.pack(
        "<4sBBHI32s40s",
        REQUEST_MAGIC,
        direction,
        file_class,
        FLAG_REPLACE if replace else 0,
        size,
        digest,
        name_field,
    )
    return body + struct.pack("<I", crc32(body))


def make_chunk(sequence: int, payload: bytes) -> bytes:
    if not 0 < len(payload) <= CHUNK_BYTES:
        raise TransferError("chunk length is outside the protocol bound")
    return struct.pack(
        "<4sIHHI", CHUNK_MAGIC, sequence, len(payload), 0, crc32(payload)
    ) + payload


def make_ack(sequence: int, status: int) -> bytes:
    return struct.pack("<4sIB", ACK_MAGIC, sequence, status)


def make_usb_drive_request(
    command: int, mode: bool, session: int, sequence: int
) -> bytes:
    if (
        command not in (USB_DRIVE_COMMAND_STATUS, USB_DRIVE_COMMAND_SET_MODE)
        or (command == USB_DRIVE_COMMAND_STATUS and mode)
        or not 0 < session <= 0xFFFF_FFFF
        or not 0 <= sequence <= 0xFFFF_FFFF
    ):
        raise TransferError("invalid bounded USB Drive control request")
    body = struct.pack(
        "<4sBBBBII",
        USB_DRIVE_REQUEST_MAGIC,
        USB_DRIVE_PROTOCOL_VERSION,
        command,
        int(mode),
        0,
        session,
        sequence,
    )
    request = body + struct.pack("<I", crc32(body))
    if len(request) != USB_DRIVE_REQUEST_BYTES:
        raise TransferError("USB Drive protocol framing error")
    return request


def read_usb_drive_response(reader: WireReader, timeout: float = 5.0) -> dict[str, int]:
    try:
        response = reader.frame(
            USB_DRIVE_RESPONSE_MAGIC, USB_DRIVE_RESPONSE_BYTES, timeout
        )
    except TransferError as error:
        raise UsbDriveResponseTimeout(
            "badge did not return a USB Drive control response"
        ) from error
    if (
        response[4] != USB_DRIVE_PROTOCOL_VERSION
        or crc32(response[:24]) != struct.unpack_from("<I", response, 24)[0]
    ):
        raise TransferError("badge returned an invalid USB Drive control response")
    return {
        "command": response[5],
        "result": response[6],
        "mode": response[7],
        "storage_state": response[8],
        "flags": response[9],
        "generation": struct.unpack_from("<I", response, 12)[0],
        "session": struct.unpack_from("<I", response, 16)[0],
        "sequence": struct.unpack_from("<I", response, 20)[0],
    }


def open_usb_drive_session(
    connection: serial.Serial, reader: WireReader, session: int
) -> dict[str, int]:
    """Open one control session, tolerating a bounded post-open reboot window."""
    request = make_usb_drive_request(
        USB_DRIVE_COMMAND_STATUS, False, session, 0
    )
    last_timeout: UsbDriveResponseTimeout | None = None
    for attempt in range(1, USB_DRIVE_STATUS_RETRY_ATTEMPTS + 1):
        write_all(connection, request)
        try:
            response = read_usb_drive_response(
                reader, timeout=USB_DRIVE_STATUS_ATTEMPT_TIMEOUT
            )
        except UsbDriveResponseTimeout as error:
            last_timeout = error
            continue
        if (
            response["command"] != USB_DRIVE_COMMAND_STATUS
            or response["session"] != session
            or response["sequence"] != 0
        ):
            raise TransferError(
                "badge returned a mismatched USB Drive status response"
            )
        return response
    raise TransferError(
        "H1 USB Drive control did not become ready after "
        f"{USB_DRIVE_STATUS_RETRY_ATTEMPTS} bounded status attempts"
    ) from last_timeout


def render_usb_drive_status(response: dict[str, int]) -> str:
    flags = response["flags"]
    return (
        "P4_H1 USB_DRIVE "
        f"result={USB_DRIVE_RESULT_NAMES.get(response['result'], 'unknown')} "
        f"mode={'on' if response['mode'] else 'off'} "
        f"storage_state={response['storage_state']} "
        f"generation={response['generation']} "
        f"supported={int(bool(flags & 1))} "
        f"attached={int(bool(flags & 2))} "
        f"ejected={int(bool(flags & 4))} "
        f"msc_driver={int(bool(flags & 8))} "
        f"available={int(bool(flags & 16))}"
    )


def usb_drive(args: argparse.Namespace) -> None:
    port = args.port or detect_port()
    session = secrets.randbits(32) or 1
    with open_port(port) as connection:
        reader = WireReader(connection)
        response = open_usb_drive_session(connection, reader, session)
        print(render_usb_drive_status(response))
        if response["result"] != 0:
            raise TransferError(
                "badge did not open a USB Drive control session: "
                f"{USB_DRIVE_RESULT_NAMES.get(response['result'], 'unknown')}"
            )
        if args.usb_drive_command == "status":
            return
        desired = args.usb_drive_command == "on"
        write_all(
            connection,
            make_usb_drive_request(
                USB_DRIVE_COMMAND_SET_MODE, desired, session, 1
            ),
        )
        response = read_usb_drive_response(reader, timeout=20.0)
        if (
            response["command"] != USB_DRIVE_COMMAND_SET_MODE
            or response["session"] != session
            or response["sequence"] != 1
        ):
            raise TransferError("badge returned a mismatched USB Drive mode response")
        print(render_usb_drive_status(response))
        if response["result"] != 0 or bool(response["mode"]) != desired:
            raise TransferError(
                "USB Drive mode was not changed: "
                f"{USB_DRIVE_RESULT_NAMES.get(response['result'], 'unknown')}"
            )


def negotiate(
    connection: serial.Serial,
    reader: WireReader,
    request: bytes,
    direction: int,
    file_class: int,
    timeout: float = 600.0,
) -> tuple[int, int, bytes, int]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        connection.baudrate = IDLE_BAUD
        write_all(connection, request)
        try:
            ready = reader.frame(READY_MAGIC, 52, 5.0)
        except TransferError:
            print("P4_H1 waiting for Console OS and SD storage...")
            continue
        status, accepted_direction, accepted_class = ready[4:7]
        transfer_baud = struct.unpack_from("<I", ready, 8)[0]
        chunk_bytes = struct.unpack_from("<H", ready, 12)[0]
        size = struct.unpack_from("<I", ready, 16)[0]
        digest = ready[20:52]
        if status in (2, 15):
            print(f"P4_H1 waiting: {status_name(status)}")
            time.sleep(1.0)
            continue
        if status == STATUS_ALREADY_PRESENT:
            return status, size, digest, chunk_bytes
        if status != STATUS_OK:
            raise TransferError(f"badge rejected request: {status_name(status)}")
        if accepted_direction != direction or accepted_class != file_class:
            raise TransferError("badge response does not match the requested operation")
        if transfer_baud != TRANSFER_BAUD or not 1 <= chunk_bytes <= CHUNK_BYTES:
            raise TransferError(
                f"badge proposed unsupported baud/chunk: {transfer_baud}/{chunk_bytes}"
            )
        connection.baudrate = transfer_baud
        reader.frame(HIGH_MAGIC, 4, 5.0)
        return status, size, digest, chunk_bytes
    raise TransferError("Console OS transfer service did not become ready")


def verify_done(done: bytes, expected_size: int, expected_digest: bytes) -> None:
    status = done[4]
    size = struct.unpack_from("<I", done, 5)[0]
    digest = done[9:41]
    if status != STATUS_OK:
        raise TransferError(f"badge failed transaction: {status_name(status)}")
    if size != expected_size or digest != expected_digest:
        raise TransferError("badge completion proof differs from the transferred file")


def push(args: argparse.Namespace) -> None:
    source = args.input.resolve()
    file_class = FILE_CLASSES[args.file_class]
    default_name = source.name.upper() if file_class != CLASS_EXCHANGE else source.name
    name = checked_remote_name(args.remote_name or default_name, file_class)
    size, digest = validate_upload(source, file_class)
    request = make_request(
        DIRECTION_UPLOAD,
        file_class,
        name,
        size,
        digest,
        replace=not args.no_replace,
    )
    port = args.port or detect_port()
    print(
        f"P4_H1 PUSH port={port} name={name} bytes={size} sha256={digest.hex()}"
    )
    existing = getattr(args, "connection", None)
    with (nullcontext(existing) if existing is not None else open_port(port)) as connection:
        reader = WireReader(connection)
        status, accepted_size, accepted_digest, chunk_bytes = negotiate(
            connection, reader, request, DIRECTION_UPLOAD, file_class
        )
        if status == STATUS_ALREADY_PRESENT:
            if accepted_size not in (0, size) or accepted_digest not in (
                b"\0" * 32,
                digest,
            ):
                raise TransferError("already-present proof differs from the source")
            print("P4_H1 PASS result=already-present hash=verified")
            return
        if accepted_size != size or accepted_digest != digest:
            raise TransferError("badge accepted different upload metadata")
        started = time.monotonic()
        sent = 0
        sequence = 0
        last_percent = -1
        with source.open("rb") as stream:
            while payload := stream.read(chunk_bytes):
                write_all(connection, make_chunk(sequence, payload))
                ack = reader.frame(ACK_MAGIC, 9, 30.0)
                ack_sequence = struct.unpack_from("<I", ack, 4)[0]
                if ack_sequence != sequence or ack[8] != STATUS_OK:
                    raise TransferError(
                        f"badge rejected chunk {sequence}: "
                        f"ack={ack_sequence} status={status_name(ack[8])}"
                    )
                sent += len(payload)
                sequence += 1
                percent = sent * 100 // size
                if percent >= last_percent + 10 or percent == 100:
                    print(f"P4_H1 progress={percent:3d}% bytes={sent}/{size}")
                    last_percent = percent
        print("P4_H1 activation=waiting device=fsync+double-validation")
        done = reader.frame(DONE_MAGIC, 41, args.done_timeout)
        verify_done(done, size, digest)
        print(
            f"P4_H1 PASS direction=push name={name} bytes={size} "
            f"sha256={digest.hex()} seconds={time.monotonic() - started:.1f}"
        )


def pull(args: argparse.Namespace) -> None:
    file_class = FILE_CLASSES[args.file_class]
    name = checked_remote_name(args.remote_name, file_class)
    output = args.output.resolve()
    if output.exists() and not args.replace:
        raise TransferError(f"output exists; pass --replace to overwrite: {output}")
    temporary = output.with_name(output.name + ".p4ft.tmp")
    if temporary.exists():
        raise TransferError(f"staging path already exists: {temporary}")
    request = make_request(DIRECTION_DOWNLOAD, file_class, name)
    port = args.port or detect_port()
    print(f"P4_H1 PULL port={port} name={name} output={output}")
    completed = False
    try:
        with open_port(port) as connection:
            reader = WireReader(connection)
            status, size, digest, chunk_bytes = negotiate(
                connection, reader, request, DIRECTION_DOWNLOAD, file_class
            )
            if status != STATUS_OK or not 0 < size <= (
                P4G_MAX_BYTES if file_class == CLASS_P4G else EXCHANGE_MAX_BYTES
            ):
                raise TransferError("badge returned invalid download metadata")
            received = 0
            expected_sequence = 0
            sha256 = hashlib.sha256()
            with temporary.open("xb") as stream:
                while received < size:
                    header = reader.frame(CHUNK_MAGIC, 16, 30.0)
                    sequence, length, reserved, supplied_crc = struct.unpack_from(
                        "<IHHI", header, 4
                    )
                    if (
                        sequence != expected_sequence
                        or reserved != 0
                        or not 0 < length <= chunk_bytes
                        or length > size - received
                    ):
                        write_all(connection, make_ack(sequence, 5))
                        raise TransferError("badge sent an invalid chunk header")
                    payload = reader.exact(length, 30.0)
                    if crc32(payload) != supplied_crc:
                        write_all(connection, make_ack(sequence, STATUS_CRC))
                        continue
                    stream.write(payload)
                    sha256.update(payload)
                    received += length
                    expected_sequence += 1
                    write_all(connection, make_ack(sequence, STATUS_OK))
                    print(f"P4_H1 progress={received * 100 // size:3d}% bytes={received}/{size}")
                stream.flush()
                os.fsync(stream.fileno())
            actual_digest = sha256.digest()
            done = reader.frame(DONE_MAGIC, 41, 30.0)
            verify_done(done, size, digest)
            if actual_digest != digest:
                raise TransferError("download SHA-256 differs from badge proof")
            if file_class == CLASS_P4G:
                validate_p4g_host(temporary.read_bytes())
            elif file_class == CLASS_P4R:
                validate_resource_host(temporary.read_bytes())
            elif file_class == CLASS_P4CART:
                validate_cart_host(temporary)
            temporary.replace(output)
            completed = True
            print(
                f"P4_H1 PASS direction=pull name={name} bytes={size} "
                f"sha256={digest.hex()} output={output}"
            )
    finally:
        if not completed and temporary.exists():
            temporary.unlink()


def push_bundle(args: argparse.Namespace) -> None:
    root = args.input.resolve()
    entries = []
    # Resource sidecars precede their native games; Lua carts have their own directory.
    for directory, extension, kind in (("GAMES", "P4R", "p4r"),
                                        ("GAMES", "P4G", "p4g"),
                                        ("P4/GAMES", "P4CART", "p4cart")):
        for source in sorted((root / directory).glob(f"*.{extension}")):
            checked_remote_name(source.name, FILE_CLASSES[kind])
            validate_upload(source, FILE_CLASSES[kind])
            if kind == "p4r":
                native = source.with_suffix(".P4G")
                validate_upload(native, CLASS_P4G)
                if source.read_bytes()[64:112] != native.read_bytes()[80:128]:
                    raise TransferError(f"resource identity differs from {native.name}")
            entries.append((source, kind))
    if not entries:
        raise TransferError("bundle contains no supported game files")
    port = args.port or detect_port()
    with open_port(port) as connection:
        for source, kind in entries:
            push(argparse.Namespace(input=source, file_class=kind, remote_name=None,
                 no_replace=args.no_replace, port=port, done_timeout=args.done_timeout,
                 connection=connection))
    print(f"P4_H1 BUNDLE_PASS files={len(entries)} port={port}")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description="Push or pull verified files through the P4 Console OS H1 port."
    )
    subparsers = result.add_subparsers(dest="command", required=True)
    push_parser = subparsers.add_parser("push", help="upload a P4G or exchange file")
    push_parser.add_argument("input", type=Path)
    push_parser.add_argument("--port")
    push_parser.add_argument(
        "--class", dest="file_class", choices=tuple(FILE_CLASSES), default="p4g"
    )
    push_parser.add_argument("--remote-name")
    push_parser.add_argument("--no-replace", action="store_true")
    push_parser.add_argument(
        "--done-timeout",
        type=bounded_done_timeout,
        default=600.0,
        help="seconds to wait for durable device activation (default: 600)",
    )

    pull_parser = subparsers.add_parser("pull", help="download a P4G or exchange file")
    pull_parser.add_argument("remote_name")
    pull_parser.add_argument("output", type=Path)
    pull_parser.add_argument("--port")
    pull_parser.add_argument(
        "--class", dest="file_class", choices=tuple(FILE_CLASSES), default="p4g"
    )
    pull_parser.add_argument("--replace", action="store_true")
    bundle = subparsers.add_parser("push-bundle", help="install validated native, resource and Lua game files")
    bundle.add_argument("input", type=Path, help="board bundle root containing GAMES and P4/GAMES")
    bundle.add_argument("--port")
    bundle.add_argument("--no-replace", action="store_true")
    bundle.add_argument("--done-timeout", type=bounded_done_timeout, default=600.0)
    usb_drive_parser = subparsers.add_parser(
        "usb-drive",
        help="query or switch the Waveshare H2 USB Drive role through trusted H1",
    )
    usb_drive_parser.add_argument("usb_drive_command", choices=("status", "on", "off"))
    usb_drive_parser.add_argument("--port")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    if args.command == "push" and args.file_class is None:
        args.file_class = "p4g" if args.input.suffix.lower() == ".p4g" else "exchange"
    try:
        if args.command == "push":
            push(args)
        elif args.command == "push-bundle":
            push_bundle(args)
        elif args.command == "pull":
            pull(args)
        else:
            usb_drive(args)
        return 0
    except (OSError, serial.SerialException, TransferError) as error:
        print(f"P4_H1 FAIL {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
