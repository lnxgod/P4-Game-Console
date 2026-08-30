#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import importlib.util
import pathlib
import struct
import sys
import tempfile
import types


ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / "scripts/p4-transfer.py"

try:
    import serial  # noqa: F401
except ModuleNotFoundError:
    sys.modules["serial"] = types.SimpleNamespace(
        Serial=object, SerialException=OSError
    )

SPEC = importlib.util.spec_from_file_location("p4_transfer", TOOL)
assert SPEC is not None and SPEC.loader is not None
TRANSFER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRANSFER)


def resource_package(game_id: str, payload: bytes) -> bytes:
    header = bytearray(128)
    header[:8] = b"P4RES01\0"
    struct.pack_into("<6I", header, 8, 128, 128 + len(payload), 128,
                     len(payload), 1, 0)
    header[32:64] = hashlib.sha256(payload).digest()
    encoded_id = game_id.encode("ascii")
    header[64 : 64 + len(encoded_id)] = encoded_id
    return bytes(header) + payload


def expect_invalid(data: bytes, message: str) -> None:
    try:
        TRANSFER.validate_p4r_host(data)
    except TRANSFER.TransferError as error:
        assert message in str(error), error
    else:
        raise AssertionError("invalid resource pack was accepted")


class UnsupportedConnection:
    baudrate = TRANSFER.IDLE_BAUD

    def write(self, data: bytes) -> int:
        return len(data)


class UnsupportedReader:
    def frame(self, marker: bytes, frame_bytes: int, timeout: float) -> bytes:
        del timeout
        assert marker == TRANSFER.READY_MAGIC and frame_bytes == 52
        return (
            TRANSFER.READY_MAGIC
            + bytes((9, TRANSFER.DIRECTION_UPLOAD, TRANSFER.CLASS_P4R, 0))
            + bytes(44)
        )


def main() -> None:
    assert TRANSFER.CLASS_P4G == 1
    assert TRANSFER.CLASS_EXCHANGE == 2
    assert TRANSFER.CLASS_P4R == 3
    assert TRANSFER.infer_class("DRAGON.P4G") == "p4g"
    assert TRANSFER.infer_class("DRAGON.P4R") == "p4r"
    try:
        TRANSFER.infer_class("notes.txt")
    except TRANSFER.TransferError as error:
        assert "--class exchange" in str(error), error
    else:
        raise AssertionError("opaque file inferred exchange without explicit class")
    assert TRANSFER.p4g_name_valid("DRAGON_1.P4G")
    assert TRANSFER.p4r_name_valid("DRAGON_1.P4R")
    assert not TRANSFER.p4r_name_valid("dragon.P4R")
    assert not TRANSFER.p4r_name_valid("DRAGON.P4G")
    assert not TRANSFER.p4r_name_valid("../DRAGON.P4R")
    assert not TRANSFER.p4r_name_valid("D" * 33 + ".P4R")

    payload = bytes(range(251)) * 17
    package = resource_package("org.example.dragon", payload)
    TRANSFER.validate_p4r_host(package)
    TRANSFER.validate_p4r_host(resource_package("abc", b"x"))
    expect_invalid(bytes(128), "bounded P4RES01")
    expect_invalid(bytes(TRANSFER.P4R_MAX_BYTES + 1), "bounded P4RES01")
    request = TRANSFER.make_request(
        TRANSFER.DIRECTION_UPLOAD,
        TRANSFER.CLASS_P4R,
        "DRAGON.P4R",
        len(package),
        hashlib.sha256(package).digest(),
        replace=True,
    )
    assert len(request) == 88 and request[5] == TRANSFER.CLASS_P4R

    corrupt_payload = bytearray(package)
    corrupt_payload[-1] ^= 1
    expect_invalid(bytes(corrupt_payload), "payload digest")
    corrupt_magic = bytearray(package)
    corrupt_magic[0] ^= 1
    expect_invalid(bytes(corrupt_magic), "bounded P4RES01")
    corrupt_version = bytearray(package)
    struct.pack_into("<I", corrupt_version, 24, 2)
    expect_invalid(bytes(corrupt_version), "header/layout/version")
    corrupt_flags = bytearray(package)
    struct.pack_into("<I", corrupt_flags, 28, 1)
    expect_invalid(bytes(corrupt_flags), "header/layout/version")
    corrupt_geometry = bytearray(package)
    struct.pack_into("<I", corrupt_geometry, 20, len(payload) - 1)
    expect_invalid(bytes(corrupt_geometry), "header/layout/version")
    corrupt_reserved = bytearray(package)
    corrupt_reserved[127] = 1
    expect_invalid(bytes(corrupt_reserved), "header/layout/version")
    corrupt_id = bytearray(package)
    corrupt_id[64] = ord("O")
    expect_invalid(bytes(corrupt_id), "game id")
    unpadded_id = bytearray(package)
    terminator = unpadded_id[64:112].find(0) + 64
    unpadded_id[terminator + 1] = 1
    expect_invalid(bytes(unpadded_id), "game id")

    try:
        TRANSFER.negotiate(
            UnsupportedConnection(),
            UnsupportedReader(),
            b"request",
            TRANSFER.DIRECTION_UPLOAD,
            TRANSFER.CLASS_P4R,
        )
    except TRANSFER.TransferError as error:
        assert "does not support P4R" in str(error), error
    else:
        raise AssertionError("old-firmware P4R rejection was not surfaced")

    with tempfile.TemporaryDirectory() as temporary:
        path = pathlib.Path(temporary) / "DRAGON.P4R"
        path.write_bytes(package)
        size, digest = TRANSFER.validate_upload(path, TRANSFER.CLASS_P4R)
        assert size == len(package)
        assert digest == hashlib.sha256(package).digest()

    push = TRANSFER.parser().parse_args(["push", "DRAGON.P4R"])
    pull = TRANSFER.parser().parse_args(
        ["pull", "DRAGON.P4R", "/tmp/DRAGON.P4R"]
    )
    assert push.file_class is None and pull.file_class is None
    print("P4 H1 transfer tests passed")


if __name__ == "__main__":
    main()
