"""P4RM v1, a realm-sync subprotocol carried in P4MP Game Message."""

from __future__ import annotations

import dataclasses
import math
import struct
import zlib


MAGIC = b"P4RM"
VERSION = 1
HEADER_BYTES = 16
MAX_MESSAGE_BYTES = 64
MAX_PAYLOAD_BYTES = MAX_MESSAGE_BYTES - HEADER_BYTES
MAX_RECORD_BYTES = 4148
MAX_CHUNKS = math.ceil(MAX_RECORD_BYTES / MAX_PAYLOAD_BYTES)
BEGIN_INDEX = 0xFFFF

HELLO = 1
WELCOME = 2
DOWNLOAD_BEGIN = 3
DOWNLOAD_CHUNK = 4
ACK = 5
UPLOAD_BEGIN = 6
UPLOAD_CHUNK = 7
COMMIT_RESULT = 8
CLOCK = 9
ERROR = 10
PROFILE = 11
DIRECTORY_SUMMARY = 12
DIRECTORY_STATS = 13
PROFILE_STATS = 14

WELCOME_HAS_SNAPSHOT = 1 << 0
WELCOME_ROLLOVER_PENDING = 1 << 1

COMMIT_OK = 0
COMMIT_CONFLICT = 1
COMMIT_INVALID = 2
COMMIT_STORAGE_ERROR = 3


@dataclasses.dataclass(frozen=True)
class Message:
    kind: int
    transaction_id: int
    chunk_index: int
    chunk_count: int
    payload: bytes


def encode_message(
    kind: int,
    transaction_id: int,
    payload: bytes = b"",
    chunk_index: int = 0,
    chunk_count: int = 0,
) -> bytes:
    if not 1 <= kind <= PROFILE_STATS:
        raise ValueError("invalid P4RM message kind")
    if not 1 <= transaction_id <= 0xFFFFFFFF:
        raise ValueError("P4RM transaction ID must be nonzero")
    if not 0 <= chunk_index <= 0xFFFF or not 0 <= chunk_count <= MAX_CHUNKS:
        raise ValueError("invalid P4RM chunk identity")
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise ValueError("P4RM payload exceeds P4MP Game Message")
    return struct.pack(
        "<4sBBBBIHH",
        MAGIC,
        VERSION,
        kind,
        0,
        HEADER_BYTES,
        transaction_id,
        chunk_index,
        chunk_count,
    ) + payload


def decode_message(data: bytes) -> Message:
    if not HEADER_BYTES <= len(data) <= MAX_MESSAGE_BYTES:
        raise ValueError("invalid P4RM message length")
    magic, version, kind, flags, header_bytes, transaction_id, index, count = (
        struct.unpack_from("<4sBBBBIHH", data)
    )
    if magic != MAGIC or version != VERSION or header_bytes != HEADER_BYTES:
        raise ValueError("invalid P4RM identity")
    if flags != 0 or not 1 <= kind <= PROFILE_STATS:
        raise ValueError("invalid P4RM kind or flags")
    if transaction_id == 0 or count > MAX_CHUNKS:
        raise ValueError("invalid P4RM transaction")
    return Message(kind, transaction_id, index, count, data[HEADER_BYTES:])


def record_chunks(record: bytes) -> list[bytes]:
    if not 1 <= len(record) <= MAX_RECORD_BYTES:
        raise ValueError("invalid P4RM record length")
    return [
        record[offset : offset + MAX_PAYLOAD_BYTES]
        for offset in range(0, len(record), MAX_PAYLOAD_BYTES)
    ]


def encode_welcome(
    actor_id: bytes,
    head_revision: int,
    realm_day_id: int,
    seconds_remaining: int,
    flags: int,
) -> bytes:
    if len(actor_id) != 16 or actor_id == b"\0" * 16:
        raise ValueError("invalid actor ID")
    if flags & ~(WELCOME_HAS_SNAPSHOT | WELCOME_ROLLOVER_PENDING):
        raise ValueError("invalid welcome flags")
    return actor_id + struct.pack(
        "<IQIB3x",
        head_revision,
        realm_day_id,
        seconds_remaining,
        flags,
    )


def decode_welcome(payload: bytes) -> tuple[bytes, int, int, int, int]:
    if len(payload) != 36 or payload[:16] == b"\0" * 16 or payload[33:] != b"\0" * 3:
        raise ValueError("invalid P4RM welcome")
    revision, day_id, remaining, flags = struct.unpack_from("<IQIB", payload, 16)
    if flags & ~(WELCOME_HAS_SNAPSHOT | WELCOME_ROLLOVER_PENDING):
        raise ValueError("invalid P4RM welcome flags")
    return payload[:16], revision, day_id, remaining, flags


def encode_download_begin(record: bytes, head_revision: int) -> bytes:
    if not 1 <= len(record) <= MAX_RECORD_BYTES or head_revision == 0:
        raise ValueError("invalid download record")
    return struct.pack(
        "<III", len(record), zlib.crc32(record) & 0xFFFFFFFF, head_revision
    )


def decode_download_begin(payload: bytes) -> tuple[int, int, int]:
    if len(payload) != 12:
        raise ValueError("invalid download begin")
    total, crc32, revision = struct.unpack("<III", payload)
    if not 1 <= total <= MAX_RECORD_BYTES or crc32 == 0 or revision == 0:
        raise ValueError("invalid download metadata")
    return total, crc32, revision


def encode_upload_begin(
    expected_revision: int,
    record: bytes,
    operation_nonce: int,
    realm_day_id: int,
) -> bytes:
    if not 1 <= len(record) <= MAX_RECORD_BYTES or operation_nonce == 0:
        raise ValueError("invalid upload record")
    return struct.pack(
        "<IIIQQ",
        expected_revision,
        len(record),
        zlib.crc32(record) & 0xFFFFFFFF,
        operation_nonce,
        realm_day_id,
    )


def decode_upload_begin(payload: bytes) -> tuple[int, int, int, int, int]:
    if len(payload) != 28:
        raise ValueError("invalid upload begin")
    expected, total, crc32, nonce, day_id = struct.unpack("<IIIQQ", payload)
    if not 1 <= total <= MAX_RECORD_BYTES or crc32 == 0 or nonce == 0 or day_id == 0:
        raise ValueError("invalid upload metadata")
    return expected, total, crc32, nonce, day_id


def encode_clock(day_id: int, seconds_remaining: int, rollover_pending: bool) -> bytes:
    if day_id == 0 or not 1 <= seconds_remaining <= 3600:
        raise ValueError("invalid realm clock")
    return struct.pack("<QIB3x", day_id, seconds_remaining, int(rollover_pending))


def decode_clock(payload: bytes) -> tuple[int, int, bool]:
    if len(payload) != 16 or payload[13:] != b"\0" * 3:
        raise ValueError("invalid realm clock")
    day_id, remaining, pending = struct.unpack_from("<QIB", payload)
    if day_id == 0 or not 1 <= remaining <= 3600 or pending > 1:
        raise ValueError("invalid realm clock values")
    return day_id, remaining, pending != 0
