"""Strict P4MP v1 framing used by the Mac realm hub."""

from __future__ import annotations

import dataclasses
import struct
import zlib


MAGIC = b"P4MP"
VERSION = 1
HEADER_BYTES = 28
TRAILER_BYTES = 4
MAX_PAYLOAD_BYTES = 1024
MAX_DATAGRAM_BYTES = HEADER_BYTES + MAX_PAYLOAD_BYTES + TRAILER_BYTES

DISCOVER = 1
OFFER = 2
JOIN = 3
ACCEPT = 4
INPUT = 5
STATE_HASH = 6
PING = 7
PONG = 8
LEAVE = 9
REJECT = 10
GAME_MESSAGE = 11

OFFER_BYTES = 128
JOIN_BYTES = 40
ACCEPT_BYTES = 24
GAME_MESSAGE_MAX_BYTES = 64

VALID_PAYLOAD_LENGTHS: dict[int, range | tuple[int, ...]] = {
    DISCOVER: (0,),
    OFFER: (OFFER_BYTES,),
    JOIN: (JOIN_BYTES,),
    ACCEPT: (ACCEPT_BYTES,),
    INPUT: (24,),
    STATE_HASH: (12,),
    PING: (8,),
    PONG: (8,),
    LEAVE: (2,),
    REJECT: (2,),
    GAME_MESSAGE: range(1, GAME_MESSAGE_MAX_BYTES + 1),
}


@dataclasses.dataclass(frozen=True)
class Packet:
    packet_type: int
    session_id: int
    peer_id: int
    sequence: int
    acknowledgement: int
    payload: bytes


@dataclasses.dataclass(frozen=True)
class Offer:
    mode: int
    game_api_major: int
    game_api_minor: int
    players_present: int
    player_capacity: int
    input_delay_ticks: int
    tick_rate_hz: int
    game_protocol: int
    session_seed: int
    game_id: str
    content_sha256: bytes
    compatibility_sha256: bytes
    game_settings: bytes


def _payload_length_valid(packet_type: int, length: int) -> bool:
    allowed = VALID_PAYLOAD_LENGTHS.get(packet_type)
    return allowed is not None and length in allowed


def encode_packet(
    packet_type: int,
    session_id: int,
    peer_id: int,
    sequence: int,
    payload: bytes = b"",
    acknowledgement: int = 0,
) -> bytes:
    if not _payload_length_valid(packet_type, len(payload)):
        raise ValueError("invalid P4MP payload length")
    if not 1 <= sequence <= 0xFFFFFFFF:
        raise ValueError("P4MP sequence must be nonzero")
    if packet_type == DISCOVER:
        if session_id != 0 or peer_id != 0:
            raise ValueError("Discover identity must be zero")
    elif not 1 <= session_id <= 0xFFFFFFFF or not 1 <= peer_id <= 0xFFFFFFFF:
        raise ValueError("session and peer IDs must be nonzero")
    header = struct.pack(
        "<4sBBHIIIIHH",
        MAGIC,
        VERSION,
        packet_type,
        0,
        session_id,
        peer_id,
        sequence,
        acknowledgement,
        len(payload),
        0,
    )
    body = header + payload
    return body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)


def decode_packet(datagram: bytes) -> Packet:
    if not HEADER_BYTES + TRAILER_BYTES <= len(datagram) <= MAX_DATAGRAM_BYTES:
        raise ValueError("invalid P4MP datagram length")
    (
        magic,
        version,
        packet_type,
        flags,
        session_id,
        peer_id,
        sequence,
        acknowledgement,
        payload_length,
        reserved,
    ) = struct.unpack_from("<4sBBHIIIIHH", datagram)
    if magic != MAGIC or version != VERSION:
        raise ValueError("invalid P4MP identity")
    if flags != 0 or reserved != 0:
        raise ValueError("unsupported P4MP flags")
    if not _payload_length_valid(packet_type, payload_length):
        raise ValueError("invalid P4MP payload")
    if len(datagram) != HEADER_BYTES + payload_length + TRAILER_BYTES:
        raise ValueError("inconsistent P4MP length")
    if sequence == 0:
        raise ValueError("zero P4MP sequence")
    if packet_type == DISCOVER:
        if session_id != 0 or peer_id != 0:
            raise ValueError("invalid Discover identity")
    elif session_id == 0 or peer_id == 0:
        raise ValueError("zero P4MP identity")
    expected_crc = struct.unpack_from("<I", datagram, len(datagram) - 4)[0]
    if expected_crc != zlib.crc32(datagram[:-4]) & 0xFFFFFFFF:
        raise ValueError("invalid P4MP CRC")
    return Packet(
        packet_type,
        session_id,
        peer_id,
        sequence,
        acknowledgement,
        datagram[HEADER_BYTES : HEADER_BYTES + payload_length],
    )


class StreamDecoder:
    """Bounded resynchronizing decoder for H1 UART bytes and log noise."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.discarded_bytes = 0
        self.dropped_frames = 0

    def feed(self, data: bytes) -> list[bytes]:
        if not isinstance(data, bytes):
            raise TypeError("P4MP stream input must be bytes")
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
            payload_length = struct.unpack_from("<H", self.buffer, 24)[0]
            if payload_length > MAX_PAYLOAD_BYTES:
                self.dropped_frames += 1
                self.discarded_bytes += 1
                del self.buffer[0]
                continue
            expected = HEADER_BYTES + payload_length + TRAILER_BYTES
            if len(self.buffer) < expected:
                break
            frame = bytes(self.buffer[:expected])
            del self.buffer[:expected]
            try:
                decode_packet(frame)
            except ValueError:
                self.dropped_frames += 1
            else:
                frames.append(frame)
        if len(self.buffer) > MAX_DATAGRAM_BYTES:
            self.discarded_bytes += len(self.buffer)
            self.buffer.clear()
            self.dropped_frames += 1
        return frames


def decode_offer(payload: bytes) -> Offer:
    if len(payload) != OFFER_BYTES:
        raise ValueError("invalid P4MP offer length")
    schema = payload[0]
    if schema != 2 or payload[7] != 0 or payload[12:16] != b"\0" * 4:
        raise ValueError("invalid P4MP offer schema")
    game_id_bytes = payload[24:56]
    terminator = game_id_bytes.find(b"\0")
    if terminator <= 0:
        raise ValueError("invalid P4MP offer game ID")
    try:
        game_id = game_id_bytes[:terminator].decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("non-ASCII P4MP game ID") from error
    return Offer(
        mode=payload[1],
        game_api_major=payload[2],
        game_api_minor=payload[3],
        players_present=payload[4],
        player_capacity=payload[5],
        input_delay_ticks=payload[6],
        tick_rate_hz=struct.unpack_from("<H", payload, 8)[0],
        game_protocol=struct.unpack_from("<H", payload, 10)[0],
        session_seed=struct.unpack_from("<Q", payload, 16)[0],
        game_id=game_id,
        content_sha256=payload[56:88],
        compatibility_sha256=payload[88:120],
        game_settings=payload[120:128],
    )


def encode_join(compatibility_sha256: bytes, join_nonce: int) -> bytes:
    if len(compatibility_sha256) != 32 or not 1 <= join_nonce <= 0xFFFFFFFF:
        raise ValueError("invalid P4MP join")
    return (
        struct.pack("<BBH", 2, 0xFF, 0)
        + compatibility_sha256
        + struct.pack("<I", join_nonce)
    )


def decode_accept(payload: bytes) -> tuple[int, int, int, int, int, bytes]:
    if len(payload) != ACCEPT_BYTES or payload[0] != 2:
        raise ValueError("invalid P4MP accept")
    return (
        payload[1],
        payload[2],
        payload[3],
        struct.unpack_from("<I", payload, 4)[0],
        struct.unpack_from("<Q", payload, 8)[0],
        payload[16:24],
    )
