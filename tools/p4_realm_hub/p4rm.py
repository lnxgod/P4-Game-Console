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
MAX_ACTION_NONCE = 0x7FFFFFFFFFFFFFFF

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
DIRECTORY_DEEDS = 22
GUILD_STATUS = 23
DIRECTORY_GUILD = 24
GUILD_PAGE = 25
GUILD_SUMMARY = 26

ACTION_MAIL = 1
ACTION_TRANSFER = 2
ACTION_FRIEND = 3
ACTION_TEAM = 4
ACTION_MENTOR = 5
ACTION_PVP_BEGIN = 6
ACTION_PVP_RESOLVE = 7
ACTION_TAVERN = 8
ACTION_NEWS = 9
ACTION_GUILD = 10

GUILD_CREATE = 0
GUILD_JOIN = 1
GUILD_LEAVE = 2
GUILD_RALLY = 3
GUILD_CLASH = 4
GUILD_CHEER = 5

GUILD_NAME_COUNT = 16
GUILD_NAME_NONE = 0
GUILD_MAX_MEMBERS = 8
GUILD_QUEST_GOAL = 12

GUILD_ROLE_NONE = 0
GUILD_ROLE_LEADER = 1
GUILD_ROLE_MEMBER = 2

GUILD_OUTCOME_NONE = 0
GUILD_OUTCOME_WIN = 1
GUILD_OUTCOME_LOSS = 2
GUILD_OUTCOME_DRAW = 3

GUILD_DAILY_RALLIED = 1 << 0
GUILD_DAILY_OUTGOING = 1 << 1
GUILD_DAILY_CHEERED = 1 << 2
GUILD_DAILY_ELIGIBLE = 1 << 3

ACTION_OK = 0
ACTION_INVALID = 1
ACTION_NOT_FOUND = 2
ACTION_DENIED = 3
ACTION_BUSY = 4

WELCOME_HAS_SNAPSHOT = 1 << 0
WELCOME_ROLLOVER_PENDING = 1 << 1
WELCOME_ACCEPT_LOCAL = 1 << 2
WELCOME_LOCAL_CONFLICT = 1 << 3
WELCOME_ADOPT_LOCAL = 1 << 4

HELLO_HAS_LOCAL = 1 << 0
HELLO_HAS_SYNC_BASE = 1 << 1
HELLO_LOCAL_DIRTY = 1 << 2

COMMIT_OK = 0
COMMIT_CONFLICT = 1
COMMIT_INVALID = 2
COMMIT_STORAGE_ERROR = 3
COMMIT_STALE_DAY = 4


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
    if not 1 <= kind <= GUILD_SUMMARY:
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
    if flags != 0 or not 1 <= kind <= GUILD_SUMMARY:
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
    elif base_revision != 0 or committed_save_sequence != 0:
        raise ValueError("hello has undeclared sync base")
    elif actor_id != b"\0" * 16 and not (has_local and dirty):
        raise ValueError("hello has an actor without a local adoption bridge")
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
    head_player_day: int = 0,
) -> bytes:
    if len(actor_id) != 16 or actor_id == b"\0" * 16:
        raise ValueError("invalid actor ID")
    if flags & ~(
        WELCOME_HAS_SNAPSHOT
        | WELCOME_ROLLOVER_PENDING
        | WELCOME_ACCEPT_LOCAL
        | WELCOME_LOCAL_CONFLICT
        | WELCOME_ADOPT_LOCAL
    ):
        raise ValueError("invalid welcome flags")
    if flags & WELCOME_HAS_SNAPSHOT and flags & (
        WELCOME_ACCEPT_LOCAL | WELCOME_LOCAL_CONFLICT
    ):
        raise ValueError("snapshot welcome has incompatible local result")
    if flags & WELCOME_ACCEPT_LOCAL and flags & WELCOME_LOCAL_CONFLICT:
        raise ValueError("welcome cannot accept and conflict")
    if flags & WELCOME_ADOPT_LOCAL and flags & (
        WELCOME_HAS_SNAPSHOT | WELCOME_ACCEPT_LOCAL | WELCOME_LOCAL_CONFLICT
    ):
        raise ValueError("adoption welcome has incompatible local result")
    if not 0 <= head_player_day <= 0xFFFF:
        raise ValueError("invalid welcome head player day")
    if flags & WELCOME_ROLLOVER_PENDING and head_player_day == 0:
        raise ValueError("rollover welcome has no head player day")
    return actor_id + struct.pack(
        "<IQIBHx",
        head_revision,
        realm_day_id,
        seconds_remaining,
        flags,
        head_player_day,
    )


def decode_welcome(payload: bytes) -> tuple[bytes, int, int, int, int, int]:
    if len(payload) != 36 or payload[:16] == b"\0" * 16 or payload[35] != 0:
        raise ValueError("invalid P4RM welcome")
    revision, day_id, remaining, flags, head_player_day = struct.unpack_from(
        "<IQIBH", payload, 16
    )
    if flags & ~(
        WELCOME_HAS_SNAPSHOT
        | WELCOME_ROLLOVER_PENDING
        | WELCOME_ACCEPT_LOCAL
        | WELCOME_LOCAL_CONFLICT
        | WELCOME_ADOPT_LOCAL
    ):
        raise ValueError("invalid P4RM welcome flags")
    encode_welcome(
        payload[:16], revision, day_id, remaining, flags, head_player_day
    )
    return payload[:16], revision, day_id, remaining, flags, head_player_day


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
        not ACTION_MAIL <= kind <= ACTION_GUILD
        or not 0 <= code <= 0xFF
        or not 0 <= value <= 0xFFFF
        or len(target_actor_id) != 16
        or not 1 <= nonce <= MAX_ACTION_NONCE
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
        not ACTION_MAIL <= kind <= ACTION_GUILD
        or not 1 <= nonce <= MAX_ACTION_NONCE
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
        or not ACTION_MAIL <= kind <= ACTION_GUILD
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
        or not ACTION_MAIL <= kind <= ACTION_GUILD
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
        or not ACTION_MAIL <= kind <= ACTION_GUILD
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
        or not ACTION_MAIL <= kind <= ACTION_GUILD
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


def encode_guild_status(
    actor_id: bytes,
    guild_id: int,
    name_code: int,
    members: int,
    role: int,
    prestige: int,
    season_points: int,
    banner_stars: int,
    quest_progress: int,
    quest_goal: int,
    wins: int,
    losses: int,
    draws: int,
    last_outcome: int,
    last_opponent_code: int,
    daily_flags: int,
) -> bytes:
    values = (
        guild_id,
        prestige,
        season_points,
        banner_stars,
        quest_progress,
        quest_goal,
        wins,
        losses,
        draws,
    )
    if (
        len(actor_id) != 16
        or actor_id == b"\0" * 16
        or any(not 0 <= item <= 0xFFFFFFFF for item in values[:3])
        or any(not 0 <= item <= 0xFFFF for item in values[3:])
        or not 0 <= members <= GUILD_MAX_MEMBERS
        or role not in (GUILD_ROLE_NONE, GUILD_ROLE_LEADER, GUILD_ROLE_MEMBER)
        or last_outcome not in (
            GUILD_OUTCOME_NONE,
            GUILD_OUTCOME_WIN,
            GUILD_OUTCOME_LOSS,
            GUILD_OUTCOME_DRAW,
        )
        or daily_flags & ~0x0F
    ):
        raise ValueError("invalid guild status")
    if guild_id == 0:
        if (
            name_code != GUILD_NAME_NONE
            or members != 0
            or role != GUILD_ROLE_NONE
            or prestige != 0
            or season_points != 0
            or banner_stars != 0
            or quest_progress != 0
            or quest_goal not in (0, GUILD_QUEST_GOAL)
            or wins != 0
            or losses != 0
            or draws != 0
            or last_outcome != GUILD_OUTCOME_NONE
            or last_opponent_code != GUILD_NAME_NONE
            or daily_flags != 0
        ):
            raise ValueError("invalid empty guild status")
    elif not 1 <= name_code <= GUILD_NAME_COUNT or members == 0 or role == 0:
        raise ValueError("invalid active guild status")
    if last_opponent_code != GUILD_NAME_NONE and not (
        1 <= last_opponent_code <= GUILD_NAME_COUNT
    ):
        raise ValueError("invalid guild opponent")
    return struct.pack(
        "<16sIHBBIIHHHHHHBHB",
        actor_id,
        guild_id,
        name_code,
        members,
        role,
        prestige,
        season_points,
        banner_stars,
        quest_progress,
        quest_goal,
        wins,
        losses,
        draws,
        last_outcome,
        last_opponent_code,
        daily_flags,
    )


def decode_guild_status(payload: bytes) -> tuple[int | bytes, ...]:
    if len(payload) != 48:
        raise ValueError("invalid guild status length")
    values = struct.unpack("<16sIHBBIIHHHHHHBHB", payload)
    encode_guild_status(*values)
    return values


def encode_directory_guild(
    actor_id: bytes, guild_id: int, name_code: int
) -> bytes:
    if len(actor_id) != 16 or actor_id == b"\0" * 16:
        raise ValueError("invalid directory guild actor")
    if guild_id == 0:
        if name_code != GUILD_NAME_NONE:
            raise ValueError("invalid empty directory guild")
    elif not 0 <= guild_id <= 0xFFFFFFFF or not 1 <= name_code <= GUILD_NAME_COUNT:
        raise ValueError("invalid directory guild")
    return struct.pack("<16sIHH", actor_id, guild_id, name_code, 0)


def decode_directory_guild(payload: bytes) -> tuple[bytes, int, int]:
    if len(payload) != 24:
        raise ValueError("invalid directory guild length")
    actor_id, guild_id, name_code, reserved = struct.unpack("<16sIHH", payload)
    if reserved != 0:
        raise ValueError("invalid directory guild reserved field")
    encode_directory_guild(actor_id, guild_id, name_code)
    return actor_id, guild_id, name_code


def encode_guild_page_request(offset: int) -> bytes:
    if not 0 <= offset <= 0xFFFF:
        raise ValueError("invalid guild page offset")
    return struct.pack("<H", offset)


def decode_guild_page_request(payload: bytes) -> int:
    if len(payload) != 2:
        raise ValueError("invalid guild page request")
    return struct.unpack("<H", payload)[0]


def encode_guild_page(offset: int, total: int) -> bytes:
    if not 0 <= offset <= total <= 0xFFFF:
        raise ValueError("invalid guild page")
    return struct.pack("<HH", offset, total)


def decode_guild_page(payload: bytes) -> tuple[int, int]:
    if len(payload) != 4:
        raise ValueError("invalid guild page length")
    offset, total = struct.unpack("<HH", payload)
    if offset > total:
        raise ValueError("invalid guild page values")
    return offset, total


def encode_guild_summary(
    guild_id: int,
    name_code: int,
    members: int,
    banner_stars: int,
    prestige: int,
    season_points: int,
    wins: int,
    losses: int,
    draws: int,
) -> bytes:
    if (
        not 1 <= guild_id <= 0xFFFFFFFF
        or not 1 <= name_code <= GUILD_NAME_COUNT
        or not 1 <= members <= GUILD_MAX_MEMBERS
        or not 0 <= banner_stars <= 0xFF
        or not 0 <= prestige <= 0xFFFFFFFF
        or not 0 <= season_points <= 0xFFFFFFFF
        or any(not 0 <= item <= 0xFFFF for item in (wins, losses, draws))
    ):
        raise ValueError("invalid guild summary")
    return struct.pack(
        "<IHBBIIHHHH",
        guild_id,
        name_code,
        members,
        banner_stars,
        prestige,
        season_points,
        wins,
        losses,
        draws,
        0,
    )


def decode_guild_summary(
    payload: bytes,
) -> tuple[int, int, int, int, int, int, int, int, int]:
    if len(payload) != 24:
        raise ValueError("invalid guild summary length")
    values = struct.unpack("<IHBBIIHHHH", payload)
    if values[-1] != 0:
        raise ValueError("invalid guild summary reserved field")
    encode_guild_summary(*values[:-1])
    return values[:-1]
