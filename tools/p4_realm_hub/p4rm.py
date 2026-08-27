"""P4RM v3, a LORD realm protocol carried in P4MP Game Message."""

from __future__ import annotations

import dataclasses
import math
import struct
import zlib


MAGIC = b"P4RM"
VERSION = 3
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
ACTION_BEGIN = 15
ACTION_BODY = 16
ACTION_RESULT = 17
EVENT_BEGIN = 18
EVENT_BODY = 19
EVENT_ACK = 20
DIRECTORY_PAGE = 21

ACTION_MAIL = 1
ACTION_TRANSFER = 2
ACTION_FRIEND = 3
ACTION_TEAM = 4
ACTION_MENTOR = 5
ACTION_PVP_BEGIN = 6
ACTION_PVP_RESOLVE = 7
ACTION_TAVERN = 8
ACTION_NEWS = 9

ACTION_OK = 0
ACTION_INVALID = 1
ACTION_NOT_FOUND = 2
ACTION_DENIED = 3
ACTION_BUSY = 4

WELCOME_HAS_SNAPSHOT = 1 << 0
WELCOME_ROLLOVER_PENDING = 1 << 1
WELCOME_ACCEPT_LOCAL = 1 << 2
WELCOME_LOCAL_CONFLICT = 1 << 3

HELLO_HAS_LOCAL = 1 << 0
HELLO_HAS_SYNC_BASE = 1 << 1
HELLO_LOCAL_DIRTY = 1 << 2

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
    if not 1 <= kind <= DIRECTORY_PAGE:
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
    if flags != 0 or not 1 <= kind <= DIRECTORY_PAGE:
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


def encode_hello(
    actor_id: bytes,
    base_revision: int,
    committed_save_sequence: int,
    current_save_sequence: int,
    flags: int,
) -> bytes:
    if len(actor_id) != 16:
        raise ValueError("invalid hello actor ID")
    if flags & ~(HELLO_HAS_LOCAL | HELLO_HAS_SYNC_BASE | HELLO_LOCAL_DIRTY):
        raise ValueError("invalid hello flags")
    has_local = bool(flags & HELLO_HAS_LOCAL)
    has_base = bool(flags & HELLO_HAS_SYNC_BASE)
    dirty = bool(flags & HELLO_LOCAL_DIRTY)
    if has_local != (current_save_sequence != 0):
        raise ValueError("hello local sequence differs")
    if committed_save_sequence > current_save_sequence:
        raise ValueError("hello committed sequence is newer than local")
    if dirty != (has_local and current_save_sequence != committed_save_sequence):
        raise ValueError("hello dirty flag differs from local generation")
    if has_base:
        if actor_id == b"\0" * 16 or base_revision == 0:
            raise ValueError("hello sync base is incomplete")
    elif actor_id != b"\0" * 16 or base_revision != 0 or committed_save_sequence != 0:
        raise ValueError("hello has undeclared sync base")
    return actor_id + struct.pack(
        "<IIIB3x",
        base_revision,
        committed_save_sequence,
        current_save_sequence,
        flags,
    )


def decode_hello(payload: bytes) -> tuple[bytes, int, int, int, int]:
    if len(payload) != 32 or payload[29:] != b"\0" * 3:
        raise ValueError("invalid P4RM hello")
    base, committed, current, flags = struct.unpack_from("<IIIB", payload, 16)
    encode_hello(payload[:16], base, committed, current, flags)
    return payload[:16], base, committed, current, flags


def encode_welcome(
    actor_id: bytes,
    head_revision: int,
    realm_day_id: int,
    seconds_remaining: int,
    flags: int,
) -> bytes:
    if len(actor_id) != 16 or actor_id == b"\0" * 16:
        raise ValueError("invalid actor ID")
    if flags & ~(
        WELCOME_HAS_SNAPSHOT
        | WELCOME_ROLLOVER_PENDING
        | WELCOME_ACCEPT_LOCAL
        | WELCOME_LOCAL_CONFLICT
    ):
        raise ValueError("invalid welcome flags")
    if flags & WELCOME_HAS_SNAPSHOT and flags & (
        WELCOME_ACCEPT_LOCAL | WELCOME_LOCAL_CONFLICT
    ):
        raise ValueError("snapshot welcome has incompatible local result")
    if flags & WELCOME_ACCEPT_LOCAL and flags & WELCOME_LOCAL_CONFLICT:
        raise ValueError("welcome cannot accept and conflict")
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
    if flags & ~(
        WELCOME_HAS_SNAPSHOT
        | WELCOME_ROLLOVER_PENDING
        | WELCOME_ACCEPT_LOCAL
        | WELCOME_LOCAL_CONFLICT
    ):
        raise ValueError("invalid P4RM welcome flags")
    encode_welcome(payload[:16], revision, day_id, remaining, flags)
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


def encode_action_begin(
    kind: int,
    code: int,
    value: int,
    target_actor_id: bytes,
    nonce: int,
    body: bytes,
) -> bytes:
    if (
        not ACTION_MAIL <= kind <= ACTION_NEWS
        or not 0 <= code <= 0xFF
        or not 0 <= value <= 0xFFFF
        or len(target_actor_id) != 16
        or nonce == 0
        or len(body) > MAX_PAYLOAD_BYTES
    ):
        raise ValueError("invalid realm action")
    return struct.pack(
        "<BBH16sQHHI",
        kind,
        code,
        value,
        target_actor_id,
        nonce,
        len(body),
        0,
        zlib.crc32(body) & 0xFFFFFFFF if body else 0,
    )


def decode_action_begin(payload: bytes) -> tuple[int, int, int, bytes, int, int, int]:
    if len(payload) != 36:
        raise ValueError("invalid action begin")
    kind, code, value, target, nonce, body_bytes, reserved, body_crc = struct.unpack(
        "<BBH16sQHHI", payload
    )
    if (
        not ACTION_MAIL <= kind <= ACTION_NEWS
        or nonce == 0
        or body_bytes > MAX_PAYLOAD_BYTES
        or reserved != 0
        or (body_bytes == 0) != (body_crc == 0)
    ):
        raise ValueError("invalid action metadata")
    return kind, code, value, target, nonce, body_bytes, body_crc


def encode_action_result(
    status: int, kind: int, code: int, value: int, related_id: int
) -> bytes:
    if (
        not ACTION_OK <= status <= ACTION_BUSY
        or not ACTION_MAIL <= kind <= ACTION_NEWS
        or not 0 <= code <= 0xFF
        or not 0 <= value <= 0xFFFFFFFF
        or not 0 <= related_id <= 0x7FFFFFFFFFFFFFFF
    ):
        raise ValueError("invalid action result")
    return struct.pack("<BBBBIQ", status, kind, code, 0, value, related_id)


def decode_action_result(payload: bytes) -> tuple[int, int, int, int, int]:
    if len(payload) != 16:
        raise ValueError("invalid action result")
    status, kind, code, reserved, value, related_id = struct.unpack("<BBBBIQ", payload)
    if (
        not ACTION_OK <= status <= ACTION_BUSY
        or not ACTION_MAIL <= kind <= ACTION_NEWS
        or reserved != 0
    ):
        raise ValueError("invalid action result values")
    return status, kind, code, value, related_id


def encode_event_begin(
    event_id: int,
    kind: int,
    code: int,
    value: int,
    source_actor_id: bytes,
    source_name: str,
    body: bytes,
) -> bytes:
    encoded_name = source_name.encode("ascii", errors="strict")
    if (
        not 1 <= event_id <= 0x7FFFFFFFFFFFFFFF
        or not ACTION_MAIL <= kind <= ACTION_NEWS
        or not 0 <= code <= 0xFF
        or not 0 <= value <= 0xFFFFFFFF
        or len(source_actor_id) != 16
        or source_actor_id == b"\0" * 16
        or not 1 <= len(encoded_name) <= 15
        or len(body) > MAX_PAYLOAD_BYTES
    ):
        raise ValueError("invalid realm event")
    name = encoded_name + b"\0" * (16 - len(encoded_name))
    return struct.pack(
        "<QBBBBI16s16s",
        event_id,
        kind,
        code,
        len(body),
        0,
        value,
        source_actor_id,
        name,
    )


def decode_event_begin(
    payload: bytes,
) -> tuple[int, int, int, int, bytes, str, int]:
    if len(payload) != 48:
        raise ValueError("invalid event begin")
    event_id, kind, code, body_bytes, reserved, value, source, name_bytes = (
        struct.unpack("<QBBBBI16s16s", payload)
    )
    terminator = name_bytes.find(b"\0")
    if (
        event_id == 0
        or not ACTION_MAIL <= kind <= ACTION_NEWS
        or body_bytes > MAX_PAYLOAD_BYTES
        or reserved != 0
        or source == b"\0" * 16
        or terminator <= 0
        or any(name_bytes[terminator:])
    ):
        raise ValueError("invalid event metadata")
    try:
        name = name_bytes[:terminator].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("invalid event source name") from error
    return event_id, kind, code, value, source, name, body_bytes
