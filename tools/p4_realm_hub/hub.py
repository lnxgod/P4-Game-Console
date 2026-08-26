"""P4MP client and LORD realm state machine used by Mac transports."""

from __future__ import annotations

import dataclasses
import secrets
import sqlite3
import struct
import time
import zlib
from collections.abc import Callable

from tools.p4_realm_hub import p4mp, p4rm
from tools.p4_realm_hub.store import RealmProfile, RealmStore


LORD_GAME_ID = "org.p4console.lord"
LORD_P4RM_PROTOCOL = 0x4C52
LORD_SYNC_HEADER_BYTES = 52
LORD_SAVE_HEADER_BYTES = 16


def _nonzero_u32() -> int:
    return secrets.randbelow(0xFFFFFFFF) + 1


def _sequence_newer(value: int, prior: int) -> bool:
    difference = (value - prior) & 0xFFFFFFFF
    return difference != 0 and difference < 0x80000000


def validate_lord_sync(record: bytes, actor_id: bytes, nonce: int) -> bool:
    """Validate both LRSY and nested LDSV envelopes without trusting structs."""
    if not LORD_SYNC_HEADER_BYTES <= len(record) <= p4rm.MAX_RECORD_BYTES:
        return False
    if record[:4] != b"LRSY":
        return False
    version, header_bytes = struct.unpack_from("<HH", record, 4)
    total, stored_crc, realm_revision, save_sequence = struct.unpack_from(
        "<IIII", record, 8
    )
    operation_nonce = struct.unpack_from("<Q", record, 24)[0]
    save_bytes = struct.unpack_from("<I", record, 48)[0]
    if (
        version != 1
        or header_bytes != LORD_SYNC_HEADER_BYTES
        or total != len(record)
        or realm_revision == 0
        or operation_nonce != nonce
        or record[32:48] != actor_id
        or save_bytes != len(record) - LORD_SYNC_HEADER_BYTES
        or stored_crc != zlib.crc32(record[16:]) & 0xFFFFFFFF
    ):
        return False
    save = record[LORD_SYNC_HEADER_BYTES:]
    if len(save) < LORD_SAVE_HEADER_BYTES or save[:4] != b"LDSV":
        return False
    save_version, save_length, save_crc, nested_sequence = struct.unpack_from(
        "<HHII", save, 4
    )
    nested_realm_revision = struct.unpack_from("<I", save, 20)[0]
    return (
        save_version == 3
        and save_length == len(save)
        and save_crc == zlib.crc32(save[LORD_SAVE_HEADER_BYTES:]) & 0xFFFFFFFF
        and nested_sequence == save_sequence
        and nested_realm_revision == realm_revision
    )


@dataclasses.dataclass
class Download:
    transaction_id: int
    chunks: list[bytes]
    begin_payload: bytes
    current_index: int = -1
    last_sent_at: float = 0.0


@dataclasses.dataclass
class Upload:
    transaction_id: int
    expected_revision: int
    total_bytes: int
    crc32: int
    nonce: int
    realm_day_id: int
    chunk_count: int
    chunks: list[bytes] = dataclasses.field(default_factory=list)


class RealmHubSession:
    """One console-to-Mac P4MP session, independent of BLE or H1 framing."""

    def __init__(
        self,
        profile: str,
        store: RealmStore,
        send_datagram: Callable[[bytes], None],
        *,
        now: Callable[[], float] = time.time,
    ) -> None:
        self.profile = profile
        self.store = store
        self.send_datagram = send_datagram
        self.now = now
        self.actor_id = store.actor_for_profile(profile)
        self.peer_id = _nonzero_u32()
        self.next_sequence = _nonzero_u32()
        self.last_remote_sequence = 0
        self.session_id = 0
        self.host_peer_id = 0
        self.connected = False
        self.joining = False
        self.game_online = False
        self.download: Download | None = None
        self.upload: Upload | None = None
        self.last_clock_day = 0
        self.next_clock_at = 0.0
        self.next_directory_at = 0.0
        self.directory_pending: list[tuple[int, int, int, bytes]] = []
        self.pending_profile: tuple[int, RealmProfile] | None = None
        self.last_rx_at = self.now()
        self.last_tx_at = 0.0

    def _send_packet(
        self, packet_type: int, payload: bytes, acknowledgement: int = 0
    ) -> None:
        datagram = p4mp.encode_packet(
            packet_type,
            self.session_id,
            self.peer_id,
            self.next_sequence,
            payload,
            acknowledgement,
        )
        self.next_sequence = self.next_sequence + 1 & 0xFFFFFFFF
        if self.next_sequence == 0:
            self.next_sequence = 1
        self.send_datagram(datagram)
        self.last_tx_at = self.now()

    def _send_realm(
        self,
        kind: int,
        transaction_id: int,
        payload: bytes = b"",
        chunk_index: int = 0,
        chunk_count: int = 0,
    ) -> None:
        self._send_packet(
            p4mp.GAME_MESSAGE,
            p4rm.encode_message(
                kind, transaction_id, payload, chunk_index, chunk_count
            ),
        )

    def receive(self, datagram: bytes) -> None:
        packet = p4mp.decode_packet(datagram)
        self.last_rx_at = self.now()
        if packet.packet_type == p4mp.OFFER:
            try:
                self._receive_offer(packet)
            except (ValueError, struct.error):
                return
            return
        if (
            not self.session_id
            or packet.session_id != self.session_id
            or packet.peer_id != self.host_peer_id
        ):
            return
        if self.last_remote_sequence and not _sequence_newer(
            packet.sequence, self.last_remote_sequence
        ):
            return
        self.last_remote_sequence = packet.sequence
        if packet.packet_type == p4mp.ACCEPT:
            try:
                assigned, players, _delay, _start, seed, _settings = (
                    p4mp.decode_accept(packet.payload)
                )
            except (ValueError, struct.error):
                return
            if assigned == 0 or players != 2 or seed == 0:
                return
            self.connected = True
            self.joining = False
            return
        if packet.packet_type == p4mp.PING:
            self._send_packet(p4mp.PONG, packet.payload, packet.sequence)
            return
        if packet.packet_type == p4mp.PONG:
            return
        if packet.packet_type == p4mp.LEAVE:
            self.connected = False
            self.joining = False
            self.game_online = False
            return
        if packet.packet_type == p4mp.GAME_MESSAGE and self.connected:
            try:
                message = p4rm.decode_message(packet.payload)
                self._receive_realm(message)
            except (ValueError, struct.error):
                self._send_realm(
                    p4rm.ERROR,
                    _nonzero_u32(),
                    struct.pack("<H", 1),
                )

    def transport_disconnected(self) -> None:
        self.session_id = 0
        self.host_peer_id = 0
        self.last_remote_sequence = 0
        self.connected = False
        self.joining = False
        self.game_online = False
        self.download = None
        self.upload = None
        self.directory_pending = []
        self.pending_profile = None

    def _receive_offer(self, packet: p4mp.Packet) -> None:
        offer = p4mp.decode_offer(packet.payload)
        if (
            offer.game_id != LORD_GAME_ID
            or offer.game_protocol != LORD_P4RM_PROTOCOL
            or offer.players_present != 1
            or offer.player_capacity != 2
        ):
            return
        if (self.connected or self.joining) and packet.session_id == self.session_id:
            return
        self.session_id = packet.session_id
        self.host_peer_id = packet.peer_id
        self.last_remote_sequence = 0
        self.connected = False
        self.joining = True
        self.game_online = False
        self.download = None
        self.upload = None
        nonce = self.peer_id ^ self.host_peer_id ^ self.session_id
        if nonce == 0:
            nonce = 1
        discover = p4mp.encode_packet(
            p4mp.DISCOVER, 0, 0, self.next_sequence
        )
        self.next_sequence = self.next_sequence + 1 & 0xFFFFFFFF
        if self.next_sequence == 0:
            self.next_sequence = 1
        self.send_datagram(discover)
        self._send_packet(p4mp.JOIN, p4mp.encode_join(offer.compatibility_sha256, nonce))

    def _receive_realm(self, message: p4rm.Message) -> None:
        if message.kind == p4rm.HELLO:
            self._begin_game_sync(message.transaction_id)
        elif message.kind == p4rm.ACK:
            self._receive_ack(message)
        elif message.kind == p4rm.UPLOAD_BEGIN:
            self._begin_upload(message)
        elif message.kind == p4rm.UPLOAD_CHUNK:
            self._receive_upload_chunk(message)
        elif message.kind == p4rm.PROFILE:
            self._receive_profile(message)
        elif message.kind == p4rm.PROFILE_STATS:
            self._receive_profile_stats(message)

    def _begin_game_sync(self, transaction_id: int) -> None:
        try:
            head = self.store.read_head(self.actor_id)
        except (OSError, RuntimeError, sqlite3.Error):
            self._send_realm(p4rm.ERROR, transaction_id, struct.pack("<H", 2))
            return
        day_id, remaining = self.store.realm_clock(self.now())
        flags = 0
        if head.snapshot is not None:
            flags |= p4rm.WELCOME_HAS_SNAPSHOT
            if head.last_day_id < day_id:
                flags |= p4rm.WELCOME_ROLLOVER_PENDING
        self._send_realm(
            p4rm.WELCOME,
            transaction_id,
            p4rm.encode_welcome(
                self.actor_id, head.revision, day_id, remaining, flags
            ),
        )
        self.game_online = True
        self.last_clock_day = day_id
        self.next_clock_at = self.now() + 30.0
        self.next_directory_at = self.now()
        if head.snapshot is not None:
            transaction = _nonzero_u32()
            chunks = p4rm.record_chunks(head.snapshot)
            self.download = Download(
                transaction,
                chunks,
                p4rm.encode_download_begin(head.snapshot, head.revision),
            )
            self._send_download_part()

    def _send_download_part(self) -> None:
        download = self.download
        if download is None:
            return
        if download.current_index < 0:
            self._send_realm(
                p4rm.DOWNLOAD_BEGIN,
                download.transaction_id,
                download.begin_payload,
                p4rm.BEGIN_INDEX,
                len(download.chunks),
            )
        else:
            self._send_realm(
                p4rm.DOWNLOAD_CHUNK,
                download.transaction_id,
                download.chunks[download.current_index],
                download.current_index,
                len(download.chunks),
            )
        download.last_sent_at = self.now()

    def _receive_ack(self, message: p4rm.Message) -> None:
        if len(message.payload) != 1 or self.download is None:
            return
        download = self.download
        if message.transaction_id != download.transaction_id:
            return
        acknowledged_kind = message.payload[0]
        if acknowledged_kind == p4rm.DOWNLOAD_BEGIN:
            if message.chunk_index != p4rm.BEGIN_INDEX or download.current_index >= 0:
                return
            download.current_index = 0
        elif acknowledged_kind == p4rm.DOWNLOAD_CHUNK:
            if message.chunk_index != download.current_index:
                return
            download.current_index += 1
            if download.current_index >= len(download.chunks):
                self.download = None
                return
        else:
            return
        self._send_download_part()

    def _begin_upload(self, message: p4rm.Message) -> None:
        expected, total, crc32, nonce, day_id = p4rm.decode_upload_begin(
            message.payload
        )
        expected_chunks = (total + p4rm.MAX_PAYLOAD_BYTES - 1) // p4rm.MAX_PAYLOAD_BYTES
        if (
            message.chunk_index != p4rm.BEGIN_INDEX
            or message.chunk_count != expected_chunks
        ):
            self._send_commit_result(message.transaction_id, p4rm.COMMIT_INVALID, 0)
            return
        self.upload = Upload(
            message.transaction_id,
            expected,
            total,
            crc32,
            nonce,
            day_id,
            expected_chunks,
        )
        self._send_realm(
            p4rm.ACK,
            message.transaction_id,
            bytes([p4rm.UPLOAD_BEGIN]),
            p4rm.BEGIN_INDEX,
            expected_chunks,
        )

    def _receive_upload_chunk(self, message: p4rm.Message) -> None:
        upload = self.upload
        if upload is None or message.transaction_id != upload.transaction_id:
            return
        expected_index = len(upload.chunks)
        if (
            message.chunk_count != upload.chunk_count
            or message.chunk_index > expected_index
        ):
            self._send_commit_result(message.transaction_id, p4rm.COMMIT_INVALID, 0)
            self.upload = None
            return
        if message.chunk_index == expected_index:
            upload.chunks.append(message.payload)
        self._send_realm(
            p4rm.ACK,
            message.transaction_id,
            bytes([p4rm.UPLOAD_CHUNK]),
            message.chunk_index,
            upload.chunk_count,
        )
        if len(upload.chunks) != upload.chunk_count:
            return
        record = b"".join(upload.chunks)
        if (
            len(record) != upload.total_bytes
            or zlib.crc32(record) & 0xFFFFFFFF != upload.crc32
            or not validate_lord_sync(record, self.actor_id, upload.nonce)
        ):
            self._send_commit_result(message.transaction_id, p4rm.COMMIT_INVALID, 0)
            self.upload = None
            return
        try:
            status, revision = self.store.commit(
                self.actor_id,
                upload.expected_revision,
                upload.nonce,
                upload.realm_day_id,
                record,
            )
        except (OSError, RuntimeError, sqlite3.Error):
            self._send_commit_result(
                message.transaction_id, p4rm.COMMIT_STORAGE_ERROR, 0
            )
            self.upload = None
            return
        result = {
            "ok": p4rm.COMMIT_OK,
            "conflict": p4rm.COMMIT_CONFLICT,
            "invalid": p4rm.COMMIT_INVALID,
        }[status]
        self._send_commit_result(message.transaction_id, result, revision)
        self.upload = None

    def _receive_profile(self, message: p4rm.Message) -> None:
        if len(message.payload) != 48:
            return
        name_bytes = message.payload[:20]
        terminator = name_bytes.find(b"\0")
        if terminator < 3 or any(name_bytes[terminator:]):
            return
        try:
            name = name_bytes[:terminator].decode("ascii")
        except UnicodeDecodeError:
            return
        style, hero_class, level, flags = message.payload[20:24]
        hit_points, max_hit_points, strength, defense = struct.unpack_from(
            "<iiii", message.payload, 24
        )
        wins, losses = struct.unpack_from("<HH", message.payload, 40)
        experience = struct.unpack_from("<I", message.payload, 44)[0]
        profile = RealmProfile(
            self.actor_id,
            name,
            style,
            hero_class,
            level,
            flags,
            hit_points,
            max_hit_points,
            strength,
            defense,
            experience,
            0,
            wins,
            losses,
            True,
        )
        try:
            self.store.validate_profile(profile)
        except (ValueError, sqlite3.Error):
            return
        self.pending_profile = (message.transaction_id, profile)

    def _receive_profile_stats(self, message: p4rm.Message) -> None:
        if self.pending_profile is None or len(message.payload) != 4:
            return
        transaction_id, profile = self.pending_profile
        if message.transaction_id != transaction_id:
            return
        complete = dataclasses.replace(
            profile,
            chompcoin=struct.unpack("<I", message.payload)[0],
        )
        try:
            self.store.update_profile(complete)
        except (ValueError, sqlite3.Error):
            return
        finally:
            self.pending_profile = None
        self.next_directory_at = self.now()

    def _queue_directory(self) -> None:
        profiles = self.store.list_profiles(exclude_actor_id=self.actor_id)
        pending: list[tuple[int, int, int, bytes]] = []
        count = len(profiles)
        if count == 0:
            pending.append((p4rm.DIRECTORY_SUMMARY, 0, 0, b""))
        for index, profile in enumerate(profiles):
            name = profile.name.encode("ascii")
            summary = bytearray(44)
            summary[:16] = profile.actor_id
            summary[16 : 16 + len(name)] = name
            summary[36] = profile.hero_style
            summary[37] = profile.hero_class
            summary[38] = profile.level
            summary[39] = profile.flags | (0x04 if profile.online else 0)
            struct.pack_into("<HH", summary, 40, profile.pvp_wins, profile.pvp_losses)
            stats = profile.actor_id + struct.pack(
                "<iiiiII",
                profile.hit_points,
                profile.max_hit_points,
                profile.strength,
                profile.defense,
                profile.experience,
                profile.chompcoin,
            )
            pending.append((p4rm.DIRECTORY_SUMMARY, index, count, bytes(summary)))
            pending.append((p4rm.DIRECTORY_STATS, index, count, stats))
        self.directory_pending = pending

    def _send_commit_result(self, transaction_id: int, status: int, revision: int) -> None:
        day_id, remaining = self.store.realm_clock(self.now())
        payload = struct.pack("<BIQI", status, revision, day_id, remaining)
        self._send_realm(p4rm.COMMIT_RESULT, transaction_id, payload)

    def tick(self) -> None:
        now = self.now()
        if self.connected and now - self.last_tx_at >= 1.0:
            stamp = int(now * 1000) & 0xFFFFFFFFFFFFFFFF
            self._send_packet(p4mp.PING, struct.pack("<Q", stamp))
        if not self.game_online:
            return
        if self.download is not None and now - self.download.last_sent_at >= 1.0:
            self._send_download_part()
            return
        if self.download is None and now >= self.next_directory_at:
            try:
                self.store.touch_profile(self.actor_id)
                self._queue_directory()
            except (OSError, RuntimeError, sqlite3.Error):
                self._send_realm(p4rm.ERROR, _nonzero_u32(), struct.pack("<H", 3))
                self.game_online = False
                return
            self.next_directory_at = now + 5.0
        if self.directory_pending:
            kind, index, count, payload = self.directory_pending.pop(0)
            self._send_realm(kind, _nonzero_u32(), payload, index, count)
            return
        if now < self.next_clock_at:
            return
        head = self.store.read_head(self.actor_id)
        day_id, remaining = self.store.realm_clock(now)
        pending = head.snapshot is not None and head.last_day_id < day_id
        transaction = _nonzero_u32()
        self._send_realm(
            p4rm.CLOCK,
            transaction,
            p4rm.encode_clock(day_id, remaining, pending),
        )
        self.last_clock_day = day_id
        self.next_clock_at = now + 30.0
