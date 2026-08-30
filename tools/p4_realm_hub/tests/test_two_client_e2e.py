"""Deterministic two-console P4MP/P4RM integration test for LORD.

The SDL game host deliberately has neither a persistent save file nor a
multiplayer service, so two SDL processes cannot reach RealmHubSession.  This
harness instead gives each emulated console its own P4MP sequence space,
P4RM transaction state, local-save generation, event cursor, and reconnect
lifecycle.  The consoles communicate with the hub only through encoded P4MP
datagrams; final SQLite queries are assertions, not shortcuts for gameplay.
"""

from __future__ import annotations

import dataclasses
import sqlite3
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from tools.p4_realm_hub import p4mp, p4rm
from tools.p4_realm_hub.hub import (
    LORD_GAME_ID,
    LORD_P4RM_PROTOCOL,
    RealmHubSession,
)
from tools.p4_realm_hub.lord_snapshot import decode_lord_sync
from tools.p4_realm_hub.store import RealmStore
from tools.p4_realm_hub.tests.test_protocol import lord_record


LRSY_HEADER_BYTES = 52
LDSV_HEADER_BYTES = 16
LDSV_MAIL_COUNT_OFFSET = 30
LDSV_MAIL_OFFSET = 1_002
LDSV_MAIL_RECORD_BYTES = 53
LDSV_MAIL_RECORD_COUNT = 12
LDSV_MAIL_BODY_BYTES = 48
LORD_MAIL_SENDER_TURGON = 9
LORD_MAIL_SENDER_HERO = 10
LORD_MAIL_CUSTOM = 6


def encode_mailbox(record: bytes, mailbox: list[tuple[int, bytes]]) -> bytes:
    """Patch the fixed LDSV mail area and repair both nested CRCs."""
    if len(mailbox) > LDSV_MAIL_RECORD_COUNT:
        raise AssertionError("emulated LORD mailbox overflow")
    patched = bytearray(record)
    if (
        patched[:4] != b"LRSY"
        or patched[LRSY_HEADER_BYTES : LRSY_HEADER_BYTES + 4] != b"LDSV"
    ):
        raise AssertionError("mailbox patch requires an LRSY/LDSV record")

    save_offset = LRSY_HEADER_BYTES
    mail_offset = save_offset + LDSV_MAIL_OFFSET
    mail_bytes = LDSV_MAIL_RECORD_BYTES * LDSV_MAIL_RECORD_COUNT
    if mail_offset + mail_bytes > len(patched):
        raise AssertionError("LDSV mail area is truncated")
    patched[save_offset + LDSV_MAIL_COUNT_OFFSET] = len(mailbox)
    patched[mail_offset : mail_offset + mail_bytes] = bytes(mail_bytes)

    for index, (_event_id, body) in enumerate(mailbox):
        if not body or len(body) >= LDSV_MAIL_BODY_BYTES or b"\0" in body:
            raise AssertionError("invalid emulated LORD mail body")
        slot = mail_offset + index * LDSV_MAIL_RECORD_BYTES
        patched[slot : slot + 5] = bytes(
            (
                LORD_MAIL_SENDER_TURGON,
                LORD_MAIL_SENDER_HERO,
                LORD_MAIL_CUSTOM,
                1,
                0,
            )
        )
        patched[slot + 5 : slot + 5 + len(body)] = body

    inner_crc = zlib.crc32(patched[save_offset + LDSV_HEADER_BYTES :]) & 0xFFFFFFFF
    struct.pack_into("<I", patched, save_offset + 8, inner_crc)
    outer_crc = zlib.crc32(patched[16:]) & 0xFFFFFFFF
    struct.pack_into("<I", patched, 12, outer_crc)
    return bytes(patched)


def decode_mail_bodies(record: bytes) -> tuple[bytes, ...]:
    """Read the bounded mail bodies from an authoritative LDSV record."""
    save_offset = LRSY_HEADER_BYTES
    count = record[save_offset + LDSV_MAIL_COUNT_OFFSET]
    if count > LDSV_MAIL_RECORD_COUNT:
        raise AssertionError("authoritative LORD mailbox count is invalid")
    mail_offset = save_offset + LDSV_MAIL_OFFSET
    bodies: list[bytes] = []
    for index in range(count):
        slot = mail_offset + index * LDSV_MAIL_RECORD_BYTES
        raw = record[slot + 5 : slot + 5 + LDSV_MAIL_BODY_BYTES]
        terminator = raw.find(b"\0")
        if terminator < 0:
            raise AssertionError("authoritative LORD mail is unterminated")
        bodies.append(raw[:terminator])
    return tuple(bodies)


def realm_offer(seed: int = 0x1020304050607080) -> p4mp.Offer:
    content = bytes(range(1, 33))
    compatibility = p4mp.compatibility_sha256(
        mode=1,
        game_api_major=1,
        game_api_minor=0,
        player_capacity=2,
        input_delay_ticks=0,
        tick_rate_hz=30,
        game_protocol=LORD_P4RM_PROTOCOL,
        game_id=LORD_GAME_ID,
        content_sha256=content,
    )
    return p4mp.Offer(
        1,
        1,
        0,
        1,
        2,
        0,
        30,
        LORD_P4RM_PROTOCOL,
        seed,
        LORD_GAME_ID,
        content,
        compatibility,
        bytes(8),
    )


@dataclasses.dataclass(frozen=True)
class ReceivedEvent:
    event_id: int
    kind: int
    code: int
    value: int
    source_actor_id: bytes
    source_name: str
    body: bytes
    duplicate: bool


@dataclasses.dataclass(frozen=True)
class LocalPersistentState:
    actor_id: bytes
    server_revision: int
    committed_save_sequence: int
    save_sequence: int
    last_realm_event_id: int
    chompcoin: int
    bank: int
    experience: int
    hit_points: int
    max_hit_points: int
    player_day: int
    pvp_fights: int
    pvp_wins: int
    pvp_losses: int
    mailbox: tuple[tuple[int, bytes], ...]
    realm_day_id: int


@dataclasses.dataclass(frozen=True)
class UploadAttemptResult:
    """Wire result from one emulated local-save upload attempt.

    ``result_received`` is false when the harness deliberately discards the
    COMMIT_RESULT after the hub has processed the final chunk.  The remaining
    fields still expose what the harness removed from the wire so a test can
    prove the hub-side outcome without pretending the emulated console learned
    it.
    """

    status: int
    revision: int
    realm_day_id: int
    seconds_remaining: int
    nonce: int
    record: bytes
    result_received: bool


class ProtocolConsole:
    """One independent, persistent console-side P4MP/P4RM state machine."""

    def __init__(self, profile: str, name: str, peer_id: int) -> None:
        self.profile = profile
        self.name = name
        self.peer_id = peer_id
        self.next_sequence = 1
        self.next_transaction = 0x1000 + peer_id
        self.next_nonce = 0x100000 + peer_id
        self.session_id = 0
        self.hub_peer_id = 0
        self.hub: RealmHubSession | None = None
        self.inbox: list[p4mp.Packet] = []

        self.actor_id = bytes(16)
        self.server_revision = 0
        self.committed_save_sequence = 0
        self.save_sequence = 0
        self.last_realm_event_id = 0
        self.chompcoin = 500
        self.bank = 500
        self.experience = 500
        self.hit_points = 20
        self.max_hit_points = 30
        self.player_day = 1
        self.pvp_fights = 3
        self.pvp_wins = 2
        self.pvp_losses = 1
        self.mailbox: list[tuple[int, bytes]] = []
        self.duplicate_events = 0
        self.realm_day_id = 0

    def bind(self, hub: RealmHubSession) -> None:
        self.hub = hub
        self.inbox.clear()

    def receive_from_hub(self, datagram: bytes) -> None:
        self.inbox.append(p4mp.decode_packet(datagram))

    def _transaction(self) -> int:
        value = self.next_transaction
        self.next_transaction += 1
        return value

    def _nonce(self) -> int:
        value = self.next_nonce
        self.next_nonce += 1
        return value

    def _send_packet(
        self,
        packet_type: int,
        payload: bytes = b"",
        *,
        acknowledgement: int = 0,
    ) -> None:
        if self.hub is None:
            raise AssertionError("console has no hub session")
        if packet_type == p4mp.DISCOVER:
            session_id = 0
            peer_id = 0
        else:
            session_id = self.session_id
            peer_id = self.peer_id
        frame = p4mp.encode_packet(
            packet_type,
            session_id,
            peer_id,
            self.next_sequence,
            payload,
            acknowledgement,
        )
        self.next_sequence += 1
        self.hub.receive(frame)

    def _send_realm(
        self,
        kind: int,
        transaction_id: int,
        payload: bytes = b"",
        *,
        chunk_index: int = 0,
        chunk_count: int = 0,
    ) -> None:
        self._send_packet(
            p4mp.GAME_MESSAGE,
            p4rm.encode_message(
                kind,
                transaction_id,
                payload,
                chunk_index,
                chunk_count,
            ),
        )

    def _take_packet(self, packet_type: int) -> p4mp.Packet:
        for index, packet in enumerate(self.inbox):
            if packet.packet_type == packet_type:
                return self.inbox.pop(index)
        raise AssertionError(f"missing P4MP packet type {packet_type}")

    def _take_realm(
        self, kind: int, transaction_id: int | None = None
    ) -> p4rm.Message:
        for index, packet in enumerate(self.inbox):
            if packet.packet_type != p4mp.GAME_MESSAGE:
                continue
            message = p4rm.decode_message(packet.payload)
            if message.kind == kind and (
                transaction_id is None
                or message.transaction_id == transaction_id
            ):
                self.inbox.pop(index)
                return message
        raise AssertionError(
            f"missing P4RM kind {kind} transaction {transaction_id}"
        )

    def _drain_realm(self) -> list[p4rm.Message]:
        result: list[p4rm.Message] = []
        remaining: list[p4mp.Packet] = []
        for packet in self.inbox:
            if packet.packet_type == p4mp.GAME_MESSAGE:
                result.append(p4rm.decode_message(packet.payload))
            else:
                remaining.append(packet)
        self.inbox = remaining
        return result

    def connect(self) -> None:
        self.inbox.clear()
        self._send_packet(p4mp.DISCOVER)
        offer_packet = self._take_packet(p4mp.OFFER)
        offer = p4mp.decode_offer(offer_packet.payload)
        self.session_id = offer_packet.session_id
        self.hub_peer_id = offer_packet.peer_id
        self._send_packet(
            p4mp.JOIN,
            p4mp.encode_join(offer.compatibility_sha256, self._transaction()),
        )
        accept = self._take_packet(p4mp.ACCEPT)
        self.assert_packet_identity(accept)
        start = self._take_packet(p4mp.PING)
        expected_token = p4mp.start_token(self.session_id, offer.session_seed)
        if p4mp.decode_start_ready(start.payload) != expected_token:
            raise AssertionError("invalid start-ready token")
        self._send_packet(
            p4mp.PONG,
            start.payload,
            acknowledgement=start.sequence,
        )

    def assert_packet_identity(self, packet: p4mp.Packet) -> None:
        if (
            packet.session_id != self.session_id
            or packet.peer_id != self.hub_peer_id
        ):
            raise AssertionError("hub packet identity changed")

    def hello(self) -> int:
        has_local = self.save_sequence != 0
        has_base = self.server_revision != 0
        dirty = has_local and self.save_sequence != self.committed_save_sequence
        dirty_generation = None
        if dirty:
            dirty_generation = (
                self.actor_id,
                self.server_revision,
                self.save_sequence,
                self.last_realm_event_id,
                self.chompcoin,
                self.bank,
                self.experience,
                self.hit_points,
                self.max_hit_points,
                self.player_day,
                self.pvp_fights,
                self.pvp_wins,
                self.pvp_losses,
                tuple(self.mailbox),
            )
        flags = 0
        if has_local:
            flags |= p4rm.HELLO_HAS_LOCAL
        if has_base:
            flags |= p4rm.HELLO_HAS_SYNC_BASE
        if dirty:
            flags |= p4rm.HELLO_LOCAL_DIRTY
        transaction = self._transaction()
        self._send_realm(
            p4rm.HELLO,
            transaction,
            p4rm.encode_hello(
                self.actor_id if has_local else bytes(16),
                self.server_revision if has_base else 0,
                self.committed_save_sequence if has_base else 0,
                self.save_sequence if has_local else 0,
                flags,
            ),
        )
        welcome = self._take_realm(p4rm.WELCOME, transaction)
        actor, revision, day_id, _remaining, welcome_flags, head_day = (
            p4rm.decode_welcome(welcome.payload)
        )
        if self.actor_id not in (bytes(16), actor):
            raise AssertionError("realm actor changed")
        self.actor_id = actor
        self.realm_day_id = day_id
        if welcome_flags & p4rm.WELCOME_LOCAL_CONFLICT:
            raise AssertionError("unexpected local-save conflict")
        if dirty and not welcome_flags & (
            p4rm.WELCOME_ACCEPT_LOCAL | p4rm.WELCOME_HAS_SNAPSHOT
        ):
            raise AssertionError("hub did not accept the offline branch")
        if welcome_flags & p4rm.WELCOME_HAS_SNAPSHOT:
            self._download_head(revision)
            if dirty_generation is not None:
                downloaded_generation = (
                    self.actor_id,
                    revision - 1,
                    self.save_sequence,
                    self.last_realm_event_id,
                    self.chompcoin,
                    self.bank,
                    self.experience,
                    self.hit_points,
                    self.max_hit_points,
                    self.player_day,
                    self.pvp_fights,
                    self.pvp_wins,
                    self.pvp_losses,
                    tuple(self.mailbox),
                )
                if revision == 0 or downloaded_generation != dirty_generation:
                    raise AssertionError(
                        "authoritative head is not the dirty generation"
                    )
        elif revision != self.server_revision:
            raise AssertionError("unexpected server head revision")
        if welcome_flags & p4rm.WELCOME_ROLLOVER_PENDING:
            self._apply_hourly_rollover(head_day)
        return welcome_flags

    def _download_head(self, revision: int) -> None:
        begin = self._take_realm(p4rm.DOWNLOAD_BEGIN)
        total_bytes, expected_crc, begin_revision = p4rm.decode_download_begin(
            begin.payload
        )
        if begin_revision != revision or begin.chunk_index != p4rm.BEGIN_INDEX:
            raise AssertionError("invalid download-begin metadata")
        if begin.chunk_count == 0:
            raise AssertionError("empty authoritative download")
        self._send_realm(
            p4rm.ACK,
            begin.transaction_id,
            bytes((p4rm.DOWNLOAD_BEGIN,)),
            chunk_index=p4rm.BEGIN_INDEX,
            chunk_count=begin.chunk_count,
        )
        chunks: list[bytes] = []
        for index in range(begin.chunk_count):
            chunk = self._take_realm(p4rm.DOWNLOAD_CHUNK, begin.transaction_id)
            if (
                chunk.chunk_index != index
                or chunk.chunk_count != begin.chunk_count
            ):
                raise AssertionError("authoritative download chunks changed")
            chunks.append(chunk.payload)
            self._send_realm(
                p4rm.ACK,
                begin.transaction_id,
                bytes((p4rm.DOWNLOAD_CHUNK,)),
                chunk_index=index,
                chunk_count=begin.chunk_count,
            )
        record = b"".join(chunks)
        if len(record) != total_bytes:
            raise AssertionError("authoritative download length changed")
        if zlib.crc32(record) & 0xFFFFFFFF != expected_crc:
            raise AssertionError("authoritative download CRC changed")
        decoded = decode_lord_sync(record, expected_actor_id=self.actor_id)
        self.server_revision = revision
        self.committed_save_sequence = decoded.save_sequence
        self.save_sequence = decoded.save_sequence
        self.last_realm_event_id = decoded.last_realm_event_id
        self.chompcoin = decoded.player.chompcoin
        self.bank = decoded.player.bank
        self.experience = decoded.player.experience
        self.hit_points = decoded.player.hit_points
        self.max_hit_points = decoded.player.max_hit_points
        self.player_day = decoded.player.day
        self.pvp_fights = decoded.pvp_fights
        self.pvp_wins = decoded.player.pvp_wins
        self.pvp_losses = decoded.player.pvp_losses
        bodies = decode_mail_bodies(record)
        if tuple(body for _event_id, body in self.mailbox) != bodies:
            self.mailbox = [(0, body) for body in bodies]

    def _apply_hourly_rollover(self, head_player_day: int) -> None:
        if head_player_day == 0 or head_player_day == 0xFFFF:
            raise AssertionError("invalid authoritative rollover day")
        if self.player_day == head_player_day:
            self.player_day += 1
            self.hit_points = self.max_hit_points
            self.pvp_fights = 3
            self.save_sequence += 1
        elif self.player_day != head_player_day + 1:
            raise AssertionError("local LORD day cannot reconcile with hub")

    def create_local_character(self) -> None:
        if self.actor_id == bytes(16) or self.save_sequence != 0:
            raise AssertionError("invalid first local character creation")
        self.save_sequence = 2

    def progress_offline(self, *, coin_gain: int, experience_gain: int) -> None:
        if self.save_sequence == 0:
            raise AssertionError("offline progress requires a local save")
        self.chompcoin += coin_gain
        self.experience += experience_gain
        self.save_sequence += 1

    def snapshot_local_state(self) -> LocalPersistentState:
        return LocalPersistentState(
            actor_id=self.actor_id,
            server_revision=self.server_revision,
            committed_save_sequence=self.committed_save_sequence,
            save_sequence=self.save_sequence,
            last_realm_event_id=self.last_realm_event_id,
            chompcoin=self.chompcoin,
            bank=self.bank,
            experience=self.experience,
            hit_points=self.hit_points,
            max_hit_points=self.max_hit_points,
            player_day=self.player_day,
            pvp_fights=self.pvp_fights,
            pvp_wins=self.pvp_wins,
            pvp_losses=self.pvp_losses,
            mailbox=tuple(self.mailbox),
            realm_day_id=self.realm_day_id,
        )

    def restore_local_state(self, state: LocalPersistentState) -> None:
        self.actor_id = state.actor_id
        self.server_revision = state.server_revision
        self.committed_save_sequence = state.committed_save_sequence
        self.save_sequence = state.save_sequence
        self.last_realm_event_id = state.last_realm_event_id
        self.chompcoin = state.chompcoin
        self.bank = state.bank
        self.experience = state.experience
        self.hit_points = state.hit_points
        self.max_hit_points = state.max_hit_points
        self.player_day = state.player_day
        self.pvp_fights = state.pvp_fights
        self.pvp_wins = state.pvp_wins
        self.pvp_losses = state.pvp_losses
        self.mailbox = list(state.mailbox)
        self.realm_day_id = state.realm_day_id

    def tick_realm_messages(self) -> list[p4rm.Message]:
        if self.hub is None:
            raise AssertionError("console has no hub session")
        self.hub.tick()
        return self._drain_realm()

    def _snapshot(self, nonce: int) -> bytes:
        bound = self.server_revision != 0
        return encode_mailbox(
            lord_record(
                self.actor_id,
                nonce,
                save_sequence=self.save_sequence,
                realm_revision=max(1, self.save_sequence - 1),
                name=self.name,
                chompcoin=self.chompcoin,
                bank=self.bank,
                experience=self.experience,
                hit_points=self.hit_points,
                max_hit_points=self.max_hit_points,
                player_day=self.player_day,
                pvp_fights_remaining=self.pvp_fights,
                pvp_wins=self.pvp_wins,
                pvp_losses=self.pvp_losses,
                last_realm_event_id=self.last_realm_event_id,
                sync_actor_id=self.actor_id if bound else bytes(16),
                sync_server_revision=self.server_revision if bound else 0,
                sync_committed_save_sequence=(
                    self.committed_save_sequence if bound else 0
                ),
            ),
            self.mailbox,
        )

    def upload_local_result(
        self,
        *,
        expected_revision: int | None = None,
        nonce: int | None = None,
        corrupt_chunk_index: int | None = None,
        lose_result: bool = False,
    ) -> UploadAttemptResult:
        """Attempt one upload while exposing conflict and failure results.

        The normal ``upload_local`` wrapper below retains the original strict
        success-only behavior.  Tests that exercise recovery can override the
        optimistic server revision, flip one byte in exactly one transmitted
        chunk, reuse a nonce for an idempotent retry, or discard a successful
        result to model transport loss after the durable commit.
        """
        operation_nonce = self._nonce() if nonce is None else nonce
        if not 1 <= operation_nonce <= 0xFFFFFFFFFFFFFFFF:
            raise ValueError("upload nonce must be a nonzero uint64")
        record = self._snapshot(operation_nonce)
        transaction = self._transaction()
        chunks = p4rm.record_chunks(record)
        if corrupt_chunk_index is not None and not (
            0 <= corrupt_chunk_index < len(chunks)
        ):
            raise ValueError("corrupt upload chunk index is out of range")
        self._send_realm(
            p4rm.UPLOAD_BEGIN,
            transaction,
            p4rm.encode_upload_begin(
                self.server_revision
                if expected_revision is None
                else expected_revision,
                record,
                operation_nonce,
                self.realm_day_id,
            ),
            chunk_index=p4rm.BEGIN_INDEX,
            chunk_count=len(chunks),
        )
        begin_ack = self._take_realm(p4rm.ACK, transaction)
        if (
            begin_ack.payload != bytes((p4rm.UPLOAD_BEGIN,))
            or begin_ack.chunk_index != p4rm.BEGIN_INDEX
        ):
            raise AssertionError("invalid upload-begin acknowledgement")
        for index, chunk in enumerate(chunks):
            transmitted = chunk
            if index == corrupt_chunk_index:
                damaged = bytearray(chunk)
                damaged[0] ^= 0x01
                transmitted = bytes(damaged)
            self._send_realm(
                p4rm.UPLOAD_CHUNK,
                transaction,
                transmitted,
                chunk_index=index,
                chunk_count=len(chunks),
            )
            chunk_ack = self._take_realm(p4rm.ACK, transaction)
            if (
                chunk_ack.payload != bytes((p4rm.UPLOAD_CHUNK,))
                or chunk_ack.chunk_index != index
            ):
                raise AssertionError("invalid upload-chunk acknowledgement")
        result = self._take_realm(p4rm.COMMIT_RESULT, transaction)
        status, revision, day_id, _remaining = struct.unpack(
            "<BIQI", result.payload
        )
        if status not in (
            p4rm.COMMIT_OK,
            p4rm.COMMIT_CONFLICT,
            p4rm.COMMIT_INVALID,
            p4rm.COMMIT_STORAGE_ERROR,
            p4rm.COMMIT_STALE_DAY,
        ):
            raise AssertionError(f"unknown realm upload status {status}")
        if day_id == 0 or not 1 <= _remaining <= 3_600:
            raise AssertionError("invalid realm upload clock result")
        result_received = not lose_result
        if status == p4rm.COMMIT_OK and result_received:
            self.server_revision = revision
            self.realm_day_id = day_id
            self.committed_save_sequence = self.save_sequence
        return UploadAttemptResult(
            status,
            revision,
            day_id,
            _remaining,
            operation_nonce,
            record,
            result_received,
        )

    def upload_local(self) -> bytes:
        result = self.upload_local_result()
        if not result.result_received:
            raise AssertionError("realm upload result was lost")
        if result.status != p4rm.COMMIT_OK:
            raise AssertionError(
                f"realm upload failed with status {result.status}"
            )
        return result.record

    def _take_directory_messages(self) -> list[p4rm.Message]:
        """Remove only directory packets, preserving events and clock traffic."""
        directory_kinds = {
            p4rm.DIRECTORY_PAGE,
            p4rm.DIRECTORY_SUMMARY,
            p4rm.DIRECTORY_STATS,
        }
        result: list[p4rm.Message] = []
        remaining: list[p4mp.Packet] = []
        for packet in self.inbox:
            if packet.packet_type != p4mp.GAME_MESSAGE:
                remaining.append(packet)
                continue
            message = p4rm.decode_message(packet.payload)
            if message.kind in directory_kinds:
                result.append(message)
            else:
                remaining.append(packet)
        self.inbox = remaining
        return result

    @staticmethod
    def _decode_directory_summary(message: p4rm.Message) -> tuple[str, bytes]:
        if len(message.payload) != 44:
            raise AssertionError("directory summary is malformed")
        actor_id = message.payload[:16]
        raw_name = message.payload[16:36]
        terminator = raw_name.find(b"\0")
        if terminator < 0 or any(raw_name[terminator:]):
            raise AssertionError("directory name is unterminated or unpadded")
        try:
            name = raw_name[:terminator].decode("ascii")
        except UnicodeDecodeError as error:
            raise AssertionError("directory name is not ASCII") from error
        hero_style, hero_class, level, flags = message.payload[36:40]
        if (
            actor_id == bytes(16)
            or not 3 <= len(raw_name[:terminator]) < 20
            or hero_style not in (0, 1)
            or hero_class not in (0, 1, 2)
            or not 1 <= level <= 12
            or flags & ~0x07
        ):
            raise AssertionError("directory summary fields are invalid")
        # The final four bytes are two bounded uint16 counters.  Unpacking is
        # intentional validation even though identity lookup needs only name.
        struct.unpack_from("<HH", message.payload, 40)
        return name, actor_id

    @staticmethod
    def _decode_directory_stats(message: p4rm.Message) -> bytes:
        if len(message.payload) != 44 or message.payload[-2:] != b"\0\0":
            raise AssertionError("directory stats are malformed")
        (
            actor_id,
            hit_points,
            max_hit_points,
            strength,
            defense,
            _experience,
            _chompcoin,
            trust,
            teamed,
        ) = struct.unpack("<16siiiiIIBB2x", message.payload)
        if (
            actor_id == bytes(16)
            or max_hit_points <= 0
            or not 0 <= hit_points <= max_hit_points
            or strength <= 0
            or defense < 0
            or trust > 100
            or teamed > 1
        ):
            raise AssertionError("directory stats fields are invalid")
        return actor_id

    def _request_directory_page(
        self, offset: int
    ) -> tuple[int, list[tuple[str, bytes]]]:
        if self.hub is None:
            raise AssertionError("console has no hub session")
        if not 0 <= offset < 100 or offset % 8 != 0:
            raise ValueError("directory offset must be a page below 100")

        # Remove already-sent automatic directory traffic.  Sending an
        # explicit page request replaces any still-queued page in the hub.
        self._take_directory_messages()
        self._send_realm(
            p4rm.DIRECTORY_PAGE,
            self._transaction(),
            struct.pack("<H", offset),
        )
        total: int | None = None
        expected_count: int | None = None
        summaries: dict[int, tuple[str, bytes]] = {}
        stats: dict[int, bytes] = {}
        empty_summary_seen = False
        for _attempt in range(64):
            self.hub.tick()
            for message in self._take_directory_messages():
                if message.kind == p4rm.DIRECTORY_PAGE:
                    if (
                        total is not None
                        or len(message.payload) != 4
                        or message.chunk_index != 0
                        or message.chunk_count != 0
                    ):
                        raise AssertionError("directory page is malformed")
                    page_offset, total = struct.unpack("<HH", message.payload)
                    if page_offset != offset or total > 100:
                        raise AssertionError("directory page metadata changed")
                    expected_count = min(8, max(0, total - offset))
                    continue
                if total is None or expected_count is None:
                    raise AssertionError("directory entry preceded its page")
                if message.kind == p4rm.DIRECTORY_SUMMARY:
                    if not message.payload:
                        if (
                            expected_count != 0
                            or empty_summary_seen
                            or message.chunk_index != 0
                            or message.chunk_count != 0
                        ):
                            raise AssertionError(
                                "unexpected empty directory summary"
                            )
                        empty_summary_seen = True
                        continue
                    if (
                        message.chunk_count != expected_count
                        or not 0 <= message.chunk_index < expected_count
                        or message.chunk_index in summaries
                    ):
                        raise AssertionError("directory summary index is invalid")
                    summaries[message.chunk_index] = (
                        self._decode_directory_summary(message)
                    )
                    continue
                if message.kind == p4rm.DIRECTORY_STATS:
                    if (
                        message.chunk_count != expected_count
                        or not 0 <= message.chunk_index < expected_count
                        or message.chunk_index in stats
                    ):
                        raise AssertionError("directory stats index is invalid")
                    stats[message.chunk_index] = self._decode_directory_stats(
                        message
                    )
            if expected_count == 0 and empty_summary_seen:
                return total, []
            if (
                expected_count is not None
                and len(summaries) == expected_count
                and len(stats) == expected_count
            ):
                entries: list[tuple[str, bytes]] = []
                for index in range(expected_count):
                    name, actor_id = summaries[index]
                    if stats[index] != actor_id:
                        raise AssertionError(
                            "directory summary/stats identity changed"
                        )
                    entries.append((name, actor_id))
                if len({name for name, _actor in entries}) != len(entries):
                    raise AssertionError("directory page repeats a name")
                if len({_actor for _name, _actor in entries}) != len(entries):
                    raise AssertionError("directory page repeats an actor")
                return total, entries
        raise AssertionError("directory page did not complete")

    def request_directory(self) -> dict[str, bytes]:
        actors: dict[str, bytes] = {}
        actor_ids: set[bytes] = set()
        ordered_entries: list[tuple[str, bytes]] = []
        expected_total: int | None = None
        offset = 0
        while True:
            total, entries = self._request_directory_page(offset)
            if expected_total is None:
                expected_total = total
            elif total != expected_total:
                raise AssertionError("directory total changed between pages")
            for name, actor_id in entries:
                if name in actors or actor_id in actor_ids:
                    raise AssertionError("directory repeats an identity")
                actors[name] = actor_id
                actor_ids.add(actor_id)
                ordered_entries.append((name, actor_id))
            if offset + len(entries) >= total:
                break
            if len(entries) != 8:
                raise AssertionError("non-final directory page is short")
            offset += 8
        if len(actors) != expected_total:
            raise AssertionError("directory did not return every profile")
        if ordered_entries != sorted(
            ordered_entries, key=lambda entry: (entry[0].lower(), entry[1])
        ):
            raise AssertionError("directory profiles are not stably sorted")
        return actors

    def request_directory_actor(self, expected_name: str) -> bytes:
        actors = self.request_directory()
        if expected_name in actors:
            return actors[expected_name]
        raise AssertionError(f"directory never listed {expected_name}")

    def action(
        self,
        kind: int,
        code: int,
        value: int,
        target_actor_id: bytes,
        body: bytes = b"",
        *,
        nonce: int | None = None,
    ) -> tuple[int, int, int, int, int]:
        transaction = self._transaction()
        operation_nonce = self._nonce() if nonce is None else nonce
        if not 1 <= operation_nonce <= 0xFFFFFFFFFFFFFFFF:
            raise ValueError("action nonce must be a nonzero uint64")
        self._send_realm(
            p4rm.ACTION_BEGIN,
            transaction,
            p4rm.encode_action_begin(
                kind, code, value, target_actor_id, operation_nonce, body
            ),
            chunk_index=p4rm.BEGIN_INDEX,
            chunk_count=1 if body else 0,
        )
        begin_ack = self._take_realm(p4rm.ACK, transaction)
        if begin_ack.payload != bytes((p4rm.ACTION_BEGIN,)):
            raise AssertionError("invalid action-begin acknowledgement")
        if body:
            self._send_realm(
                p4rm.ACTION_BODY,
                transaction,
                body,
                chunk_index=0,
                chunk_count=1,
            )
            body_ack = self._take_realm(p4rm.ACK, transaction)
            if body_ack.payload != bytes((p4rm.ACTION_BODY,)):
                raise AssertionError("invalid action-body acknowledgement")
        result = self._take_realm(p4rm.ACTION_RESULT, transaction)
        return p4rm.decode_action_result(result.payload)

    def receive_event(self) -> ReceivedEvent:
        if self.hub is None:
            raise AssertionError("console has no hub session")
        for _attempt in range(24):
            self.hub.tick()
            try:
                begin = self._take_realm(p4rm.EVENT_BEGIN)
            except AssertionError:
                continue
            (
                event_id,
                kind,
                code,
                value,
                source_actor,
                source_name,
                body_bytes,
            ) = p4rm.decode_event_begin(begin.payload)
            body = b""
            if body_bytes:
                self._send_realm(
                    p4rm.ACK,
                    begin.transaction_id,
                    bytes((p4rm.EVENT_BEGIN,)),
                    chunk_index=p4rm.BEGIN_INDEX,
                    chunk_count=1,
                )
                event_body = self._take_realm(
                    p4rm.EVENT_BODY, begin.transaction_id
                )
                body = event_body.payload
                if len(body) != body_bytes:
                    raise AssertionError("event body length changed")

            duplicate = event_id <= self.last_realm_event_id
            if duplicate:
                self.duplicate_events += 1
            else:
                if kind == p4rm.ACTION_MAIL:
                    self.mailbox.append((event_id, body))
                elif kind == p4rm.ACTION_TRANSFER and code == 1:
                    if self.bank < value:
                        raise AssertionError("event debit exceeds local bank")
                    self.bank -= value
                elif kind == p4rm.ACTION_TRANSFER and code == 0:
                    self.chompcoin += value
                elif kind == p4rm.ACTION_PVP_RESOLVE and code == 1:
                    if self.chompcoin < value:
                        raise AssertionError("PvP debit exceeds carried ChompCoin")
                    self.chompcoin -= value
                    self.hit_points = 0
                    self.pvp_losses += 1
                elif kind == p4rm.ACTION_PVP_RESOLVE and code == 2:
                    self.chompcoin += value
                    self.pvp_wins += 1
                elif kind == p4rm.ACTION_PVP_RESOLVE and code == 0:
                    self.pvp_wins += 1
                else:
                    raise AssertionError(
                        f"unexpected event kind/code {kind}/{code}"
                    )
                self.last_realm_event_id = event_id
                self.save_sequence += 1
            self._send_realm(
                p4rm.EVENT_ACK,
                begin.transaction_id,
                struct.pack("<Q", event_id),
            )
            return ReceivedEvent(
                event_id,
                kind,
                code,
                value,
                source_actor,
                source_name,
                body,
                duplicate,
            )
        raise AssertionError("realm event was not delivered")

    def leave(self) -> None:
        self._send_packet(p4mp.LEAVE, b"\0\0")
        self.session_id = 0
        self.hub_peer_id = 0
        self.inbox.clear()


class TwoConsoleRealmE2ETests(unittest.TestCase):
    def test_offline_progress_reconnect_actions_replay_and_canonical_heads(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            database_path = Path(directory) / "lord-two-console.sqlite3"
            clock = [1_000.0]
            store = RealmStore(database_path, epoch_seconds=1_000)
            offer = realm_offer()
            pink = ProtocolConsole("pink-289", "Pink Ranger", 0x289)
            green = ProtocolConsole("green", "Green Ranger", 0x386F)

            def bind(console: ProtocolConsole, session_id: int) -> None:
                console.bind(
                    RealmHubSession(
                        console.profile,
                        store,
                        console.receive_from_hub,
                        offer,
                        session_id=session_id,
                        now=lambda: clock[0],
                    )
                )

            bind(pink, 0xA001)
            bind(green, 0xB001)

            # Both consoles enroll through full Discover/Offer/Join/Accept,
            # obtain independent actor IDs, and upload chunked first heads.
            for console in (pink, green):
                console.connect()
                self.assertEqual(console.hello(), 0)
                console.create_local_character()
                console.upload_local()
                self.assertEqual(console.server_revision, 1)
            self.assertNotEqual(pink.actor_id, green.actor_id)

            # Each console disconnects, progresses a different local branch,
            # then reconnects and uploads it using the accepted server base.
            pink.leave()
            green.leave()
            pink.progress_offline(coin_gain=50, experience_gain=100)
            green.progress_offline(coin_gain=25, experience_gain=200)
            for console in (pink, green):
                console.connect()
                self.assertTrue(
                    console.hello() & p4rm.WELCOME_ACCEPT_LOCAL
                )
                console.upload_local()
                self.assertEqual(console.server_revision, 2)

            # The action target comes from the real directory stream rather
            # than from a direct store lookup.
            green_from_directory = pink.request_directory_actor("Green Ranger")
            pink_from_directory = green.request_directory_actor("Pink Ranger")
            self.assertEqual(green_from_directory, green.actor_id)
            self.assertEqual(pink_from_directory, pink.actor_id)

            mail_result = pink.action(
                p4rm.ACTION_MAIL,
                0,
                0,
                green_from_directory,
                b"MEET AT THE INN",
            )
            transfer_result = pink.action(
                p4rm.ACTION_TRANSFER,
                0,
                100,
                green_from_directory,
            )
            self.assertEqual(mail_result[0], p4rm.ACTION_OK)
            self.assertEqual(transfer_result[0], p4rm.ACTION_OK)

            # Snapshot the persisted local state before Green applies and
            # wire-ACKs mail.  A simulated power loss then restores that older
            # state, proving that a receipt alone is not a durable event commit.
            before_mail = green.snapshot_local_state()
            first_mail = green.receive_event()
            self.assertEqual(first_mail.body, b"MEET AT THE INN")
            self.assertFalse(first_mail.duplicate)
            with sqlite3.connect(database_path) as database:
                receipt, committed = database.execute(
                    "SELECT receipt_at, committed_at FROM realm_events "
                    "WHERE event_id = ?",
                    (first_mail.event_id,),
                ).fetchone()
            self.assertIsNotNone(receipt)
            self.assertIsNone(committed)
            if green.hub is None:
                raise AssertionError("green session disappeared")
            green.restore_local_state(before_mail)
            self.assertEqual(green.last_realm_event_id, 0)
            self.assertEqual(green.mailbox, [])
            green.hub.transport_disconnected()

            # A fresh hub-session object proves no volatile delivery cursor is
            # being shared.  The clean, restored client replays and applies the
            # unsaved event exactly once.
            bind(green, 0xB002)
            green.connect()
            clean_flags = green.hello()
            self.assertTrue(clean_flags & p4rm.WELCOME_ACCEPT_LOCAL)
            replay = green.receive_event()
            self.assertEqual(replay.event_id, first_mail.event_id)
            self.assertFalse(replay.duplicate)
            self.assertEqual(green.mailbox, [(first_mail.event_id, first_mail.body)])
            self.assertEqual(green.duplicate_events, 0)

            # This replayed mail cursor is now treated as persisted local state,
            # but transport is lost before its dirty generation uploads.  On
            # the next reconnect, the hub must not deliver the later transfer
            # credit until that accepted branch commits.
            if green.hub is None:
                raise AssertionError("green replay session disappeared")
            green.hub.transport_disconnected()
            bind(green, 0xB003)
            green.connect()
            dirty_flags = green.hello()
            self.assertTrue(dirty_flags & p4rm.WELCOME_ACCEPT_LOCAL)
            for _attempt in range(3):
                messages = green.tick_realm_messages()
                self.assertNotIn(
                    p4rm.EVENT_BEGIN,
                    [message.kind for message in messages],
                    "dirty reconnect delivered an event before local upload",
                )
            green.upload_local()
            self.assertEqual(green.server_revision, 3)
            with sqlite3.connect(database_path) as database:
                mail_committed = database.execute(
                    "SELECT committed_at FROM realm_events WHERE event_id = ?",
                    (first_mail.event_id,),
                ).fetchone()[0]
            self.assertIsNotNone(mail_committed)

            # Pink ACKs the bank debit, then loses that unsaved generation in a
            # simulated power cut.  A new hub session must replay the same event
            # so the restored persisted bank is debited once, not zero or twice.
            before_debit = pink.snapshot_local_state()
            first_debit = pink.receive_event()
            self.assertEqual(
                (first_debit.kind, first_debit.code, first_debit.value),
                (p4rm.ACTION_TRANSFER, 1, 100),
            )
            self.assertEqual(pink.bank, 400)
            with sqlite3.connect(database_path) as database:
                debit_receipt, debit_committed = database.execute(
                    "SELECT receipt_at, committed_at FROM realm_events "
                    "WHERE event_id = ?",
                    (first_debit.event_id,),
                ).fetchone()
            self.assertIsNotNone(debit_receipt)
            self.assertIsNone(debit_committed)
            if pink.hub is None:
                raise AssertionError("pink session disappeared")
            pink.restore_local_state(before_debit)
            self.assertEqual((pink.bank, pink.last_realm_event_id), (500, 0))
            pink.hub.transport_disconnected()

            bind(pink, 0xA002)
            pink.connect()
            self.assertTrue(pink.hello() & p4rm.WELCOME_ACCEPT_LOCAL)
            debit = pink.receive_event()
            self.assertEqual(debit.event_id, first_debit.event_id)
            self.assertFalse(debit.duplicate)
            self.assertEqual(
                (pink.bank, pink.last_realm_event_id), (400, debit.event_id)
            )
            self.assertEqual(pink.duplicate_events, 0)
            pink.upload_local()
            self.assertEqual(pink.server_revision, 3)

            credit = green.receive_event()
            self.assertEqual(
                (credit.kind, credit.code, credit.value),
                (p4rm.ACTION_TRANSFER, 0, 100),
            )
            self.assertGreater(credit.event_id, replay.event_id)
            self.assertFalse(credit.duplicate)
            self.assertEqual(
                green.mailbox, [(first_mail.event_id, first_mail.body)]
            )
            green.upload_local()
            self.assertEqual(green.server_revision, 4)

            # Reopen from disk and compare both decoded heads with the public
            # profiles.  No live client object supplies these final values.
            reopened = RealmStore(database_path, epoch_seconds=1_000)
            pink_head = reopened.read_head(pink.actor_id)
            green_head = reopened.read_head(green.actor_id)
            pink_save = decode_lord_sync(
                pink_head.snapshot, expected_actor_id=pink.actor_id
            )
            green_save = decode_lord_sync(
                green_head.snapshot, expected_actor_id=green.actor_id
            )
            self.assertEqual(
                decode_mail_bodies(green_head.snapshot),
                (b"MEET AT THE INN",),
            )
            self.assertEqual(
                (
                    pink_head.revision,
                    pink_save.player.chompcoin,
                    pink_save.player.bank,
                    pink_save.player.experience,
                    pink_save.last_realm_event_id,
                ),
                (3, 550, 400, 600, debit.event_id),
            )
            self.assertEqual(
                (
                    green_head.revision,
                    green_save.player.chompcoin,
                    green_save.player.bank,
                    green_save.player.experience,
                    green_save.last_realm_event_id,
                ),
                (4, 625, 500, 700, credit.event_id),
            )
            pink_profile = reopened.list_profiles(
                exclude_actor_id=green.actor_id
            )[0]
            green_profile = reopened.list_profiles(
                exclude_actor_id=pink.actor_id
            )[0]
            self.assertEqual(
                (pink_profile.chompcoin, pink_profile.bank), (550, 400)
            )
            self.assertEqual(
                (green_profile.chompcoin, green_profile.bank), (625, 500)
            )
            with sqlite3.connect(database_path) as database:
                committed_events, total_events = database.execute(
                    "SELECT SUM(committed_at IS NOT NULL), COUNT(*) "
                    "FROM realm_events"
                ).fetchone()
                applied_economy, total_economy = database.execute(
                    "SELECT SUM(applied_revision IS NOT NULL), COUNT(*) "
                    "FROM realm_economy_events"
                ).fetchone()
            self.assertEqual((committed_events, total_events), (3, 3))
            self.assertEqual((applied_economy, total_economy), (2, 2))

            print(
                "two-console LORD E2E PASS: "
                "enrollment=2 offline_uploads=2 mail_replays=1 "
                "economy_replays=1 authoritative_mail=1 "
                "events=3/3 economy=2/2 heads=pink:r3,green:r4"
            )


if __name__ == "__main__":
    unittest.main()
