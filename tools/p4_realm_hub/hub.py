"""Backend-hosted P4MP lobby and LORD realm state machine."""

from __future__ import annotations

import dataclasses
import hashlib
import secrets
import sqlite3
import struct
import time
import zlib
from collections.abc import Callable

from tools.p4_realm_hub import p4mp, p4rm
from tools.p4_realm_hub.lord_snapshot import decode_lord_sync
from tools.p4_realm_hub.store import RealmEvent, RealmStore


LORD_GAME_ID = "org.p4console.lord"
LORD_P4RM_PROTOCOL = 0x4C53


def _nonzero_u32() -> int:
    return secrets.randbelow(0xFFFFFFFF) + 1


def _sequence_newer(value: int, prior: int) -> bool:
    difference = (value - prior) & 0xFFFFFFFF
    return difference != 0 and difference < 0x80000000


def validate_lord_sync(
    record: bytes,
    actor_id: bytes,
    nonce: int,
    *,
    allow_adoption_bridge: bool = False,
) -> bool:
    """Validate the complete fixed LRSY/LDSV C layout and protected fields."""
    try:
        decode_lord_sync(
            record,
            expected_actor_id=actor_id,
            expected_nonce=nonce,
            allow_adoption_bridge=allow_adoption_bridge,
        )
    except ValueError:
        return False
    return True


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
    adoption_grant_id: int | None = None
    chunks: list[bytes] = dataclasses.field(default_factory=list)


@dataclasses.dataclass
class ActionUpload:
    transaction_id: int
    kind: int
    code: int
    value: int
    target_actor_id: bytes
    nonce: int
    body_bytes: int
    body_crc32: int


@dataclasses.dataclass
class OutgoingEvent:
    transaction_id: int
    event: RealmEvent
    body_sent: bool = False
    last_sent_at: float = 0.0


class RealmHubSession:
    """One console-to-Mac P4MP session, independent of BLE or H1 framing."""

    def __init__(
        self,
        profile: str,
        store: RealmStore,
        send_datagram: Callable[[bytes], None],
        offer: p4mp.Offer,
        *,
        session_id: int | None = None,
        now: Callable[[], float] = time.time,
        log_event: Callable[[str], None] = lambda _message: None,
    ) -> None:
        self.profile = profile
        self.store = store
        self.send_datagram = send_datagram
        self.now = now
        self.log_event = log_event
        self.actor_id = store.actor_for_profile(profile)
        self.peer_id = _nonzero_u32()
        self.next_sequence = _nonzero_u32()
        self.offer = offer
        self.last_remote_sequence = 0
        if session_id is not None and not 1 <= session_id <= 0xFFFFFFFF:
            raise ValueError("invalid fixed P4MP session ID")
        self.fixed_session_id = session_id
        self.session_id = session_id or _nonzero_u32()
        self.remote_peer_id = 0
        self.connected = False
        self.game_online = False
        self.awaiting_local_commit = False
        self.awaiting_local_save_sequence: int | None = None
        self.awaiting_local_rollover_allowed = False
        self.download: Download | None = None
        self.upload: Upload | None = None
        self.action_upload: ActionUpload | None = None
        self.outgoing_event: OutgoingEvent | None = None
        self.received_event_cursor = 0
        self.awaiting_event_commit = 0
        self.last_clock_day = 0
        self.next_clock_at = 0.0
        self.next_directory_at = 0.0
        self.directory_pending: list[tuple[int, int, int, bytes]] = []
        self.directory_offset = 0
        self.adoption_grant_id: int | None = None
        self.adoption_retry: tuple[int, int, bytes] | None = None
        self.last_rx_at = self.now()
        self.last_tx_at = 0.0
        self.next_start_ready_at = 0.0
        self.start_ready_until = 0.0

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
        if packet.packet_type == p4mp.DISCOVER:
            self._send_packet(p4mp.OFFER, p4mp.encode_offer(self.offer))
            return
        if (
            packet.session_id != self.session_id
            or packet.peer_id == self.peer_id
        ):
            return
        if packet.packet_type == p4mp.JOIN:
            self._receive_join(packet)
            return
        if not self.connected or packet.peer_id != self.remote_peer_id:
            return
        if self.last_remote_sequence and not _sequence_newer(
            packet.sequence, self.last_remote_sequence
        ):
            return
        self.last_remote_sequence = packet.sequence
        if packet.packet_type == p4mp.PING:
            self._send_packet(p4mp.PONG, packet.payload, packet.sequence)
            return
        if packet.packet_type == p4mp.PONG:
            return
        if packet.packet_type == p4mp.LEAVE:
            self._reset_lobby()
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
        self._reset_lobby()

    def _reset_lobby(self) -> None:
        self.session_id = self.fixed_session_id or _nonzero_u32()
        self.offer = dataclasses.replace(
            self.offer,
            session_seed=(secrets.randbits(64) or 1),
            players_present=1,
        )
        self.remote_peer_id = 0
        self.last_remote_sequence = 0
        self.connected = False
        self.game_online = False
        self.awaiting_local_commit = False
        self.awaiting_local_save_sequence = None
        self.awaiting_local_rollover_allowed = False
        self.download = None
        self.upload = None
        self.action_upload = None
        self.outgoing_event = None
        self.received_event_cursor = 0
        self.awaiting_event_commit = 0
        self.directory_pending = []
        self.directory_offset = 0
        self.adoption_grant_id = None
        self.adoption_retry = None
        self.next_start_ready_at = 0.0
        self.start_ready_until = 0.0

    def _receive_join(self, packet: p4mp.Packet) -> None:
        try:
            requested_slot, compatibility, _nonce = p4mp.decode_join(packet.payload)
        except (ValueError, struct.error):
            return
        if (
            compatibility != self.offer.compatibility_sha256
            or requested_slot not in (1, 0xFF)
            or (self.remote_peer_id not in (0, packet.peer_id))
        ):
            return
        if self.last_remote_sequence and not _sequence_newer(
            packet.sequence, self.last_remote_sequence
        ):
            return
        self.remote_peer_id = packet.peer_id
        self.last_remote_sequence = packet.sequence
        self.connected = True
        self.game_online = False
        self.awaiting_local_commit = False
        self.awaiting_local_save_sequence = None
        self.awaiting_local_rollover_allowed = False
        self.download = None
        self.upload = None
        self.action_upload = None
        self.outgoing_event = None
        self.received_event_cursor = 0
        self.awaiting_event_commit = 0
        self.adoption_grant_id = None
        self.adoption_retry = None
        accept = p4mp.encode_accept(
            1,
            2,
            self.offer.input_delay_ticks,
            0,
            self.offer.session_seed,
            self.offer.game_settings,
        )
        self._send_packet(p4mp.ACCEPT, accept, packet.sequence)
        self.log_event(
            f"CLIENT_ACCEPTED session={self.session_id:08x} "
            f"peer={self.remote_peer_id:08x} slot=1"
        )
        now = self.now()
        self.next_start_ready_at = now
        self.start_ready_until = now + 3.0
        self._send_start_ready()

    def _send_start_ready(self) -> None:
        token = p4mp.start_token(self.session_id, self.offer.session_seed)
        self._send_packet(p4mp.PING, p4mp.encode_start_ready(token))
        self.next_start_ready_at = self.now() + 0.1

    def _receive_realm(self, message: p4rm.Message) -> None:
        if message.kind == p4rm.HELLO:
            self._begin_game_sync(message)
        elif not self.game_online:
            self._send_realm(
                p4rm.ERROR, message.transaction_id, struct.pack("<H", 1)
            )
        elif self.awaiting_local_commit:
            if message.kind == p4rm.ACK:
                self._receive_ack(message)
            elif message.kind == p4rm.UPLOAD_BEGIN:
                self._begin_upload(message)
            elif message.kind == p4rm.UPLOAD_CHUNK:
                self._receive_upload_chunk(message)
            elif message.kind == p4rm.ACTION_BEGIN:
                self._begin_action(message)
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
        elif message.kind == p4rm.ACTION_BEGIN:
            self._begin_action(message)
        elif message.kind == p4rm.ACTION_BODY:
            self._receive_action_body(message)
        elif message.kind == p4rm.EVENT_ACK:
            self._receive_event_ack(message)
        elif message.kind == p4rm.DIRECTORY_PAGE:
            self._receive_directory_page(message)

    def _begin_game_sync(self, message: p4rm.Message) -> None:
        self.awaiting_local_commit = False
        self.awaiting_local_save_sequence = None
        self.awaiting_local_rollover_allowed = False
        try:
            local_actor, base_revision, committed, current, hello_flags = (
                p4rm.decode_hello(message.payload)
            )
        except ValueError:
            self._send_realm(
                p4rm.ERROR, message.transaction_id, struct.pack("<H", 1)
            )
            return
        try:
            head = self.store.read_head(self.actor_id)
        except (OSError, RuntimeError, sqlite3.Error):
            self._send_realm(
                p4rm.ERROR, message.transaction_id, struct.pack("<H", 2)
            )
            return
        day_id, remaining = self.store.realm_clock(self.now())
        flags = 0
        send_snapshot = False
        head_player_day = 0
        self.adoption_grant_id = None
        self.adoption_retry = None
        self.received_event_cursor = 0
        self.awaiting_event_commit = 0
        self.outgoing_event = None
        has_local = bool(hello_flags & p4rm.HELLO_HAS_LOCAL)
        has_base = bool(hello_flags & p4rm.HELLO_HAS_SYNC_BASE)
        local_dirty = bool(hello_flags & p4rm.HELLO_LOCAL_DIRTY)
        local_actor_mismatch = (
            has_local
            and local_actor != bytes(16)
            and local_actor != self.actor_id
        )
        if head.snapshot is not None:
            head_nonce = struct.unpack_from("<Q", head.snapshot, 24)[0]
            try:
                head_record = decode_lord_sync(
                    head.snapshot,
                    expected_actor_id=self.actor_id,
                    expected_nonce=head_nonce,
                    allow_adoption_bridge=True,
                )
            except ValueError:
                self._send_realm(
                    p4rm.ERROR, message.transaction_id, struct.pack("<H", 2)
                )
                return
            head_player_day = head_record.player.day
            head_save_sequence = head_record.save_sequence
            base_matches = (
                has_base
                and local_actor == self.actor_id
                and base_revision == head.revision
                and committed == head_save_sequence
            )
            lost_commit_common = (
                has_local
                and local_dirty
                and local_actor == self.actor_id
                and current != 0xFFFFFFFF
                and current == head_save_sequence
                and head_record.save_version >= 5
                and head_record.sync_actor_id == self.actor_id
            )
            lost_commit_with_base = (
                lost_commit_common
                and has_base
                and head.revision > 0
                and base_revision + 1 == head.revision
                and head_record.sync_server_revision == base_revision
                and head_record.sync_committed_save_sequence == committed
            )
            lost_empty_adoption_ack = (
                lost_commit_common
                and not has_base
                and base_revision == 0
                and committed == 0
                and head.revision == 1
                and head_record.sync_server_revision == 0
                and head_record.sync_committed_save_sequence == 0
            )
            lost_commit_ack = lost_commit_with_base or lost_empty_adoption_ack
            if local_actor_mismatch:
                flags |= p4rm.WELCOME_LOCAL_CONFLICT
            elif lost_commit_ack:
                flags |= p4rm.WELCOME_HAS_SNAPSHOT
                send_snapshot = True
            elif has_local and base_matches:
                flags |= p4rm.WELCOME_ACCEPT_LOCAL
            elif local_dirty:
                try:
                    self.adoption_grant_id = self.store.pending_local_adoption(
                        self.actor_id
                    )
                except (OSError, RuntimeError, sqlite3.Error):
                    self._send_realm(
                        p4rm.ERROR,
                        message.transaction_id,
                        struct.pack("<H", 2),
                    )
                    return
                if self.adoption_grant_id is None:
                    flags |= p4rm.WELCOME_LOCAL_CONFLICT
                else:
                    flags |= p4rm.WELCOME_ADOPT_LOCAL
            else:
                flags |= p4rm.WELCOME_HAS_SNAPSHOT
                send_snapshot = True
            if (
                flags
                & (p4rm.WELCOME_LOCAL_CONFLICT | p4rm.WELCOME_ADOPT_LOCAL)
                == 0
                and head.last_day_id < day_id
            ):
                flags |= p4rm.WELCOME_ROLLOVER_PENDING
        elif has_local:
            if local_actor_mismatch:
                flags |= p4rm.WELCOME_LOCAL_CONFLICT
            else:
                try:
                    self.adoption_grant_id = self.store.pending_local_adoption(
                        self.actor_id
                    )
                except (OSError, RuntimeError, sqlite3.Error):
                    self._send_realm(
                        p4rm.ERROR,
                        message.transaction_id,
                        struct.pack("<H", 2),
                    )
                    return
                if self.adoption_grant_id is None:
                    flags |= p4rm.WELCOME_LOCAL_CONFLICT
                else:
                    flags |= p4rm.WELCOME_ADOPT_LOCAL
        self._send_realm(
            p4rm.WELCOME,
            message.transaction_id,
            p4rm.encode_welcome(
                self.actor_id,
                head.revision,
                day_id,
                remaining,
                flags,
                head_player_day,
            ),
        )
        self.game_online = (flags & p4rm.WELCOME_LOCAL_CONFLICT) == 0
        self.awaiting_local_commit = local_dirty and bool(
            flags & (p4rm.WELCOME_ACCEPT_LOCAL | p4rm.WELCOME_ADOPT_LOCAL)
        )
        if self.awaiting_local_commit:
            self.awaiting_local_save_sequence = current
            self.awaiting_local_rollover_allowed = bool(
                flags & p4rm.WELCOME_ROLLOVER_PENDING
            )
        if flags & p4rm.WELCOME_LOCAL_CONFLICT:
            sync_mode = "conflict"
        elif flags & p4rm.WELCOME_ADOPT_LOCAL:
            sync_mode = "adopt"
        elif flags & p4rm.WELCOME_ACCEPT_LOCAL:
            sync_mode = "local"
        elif send_snapshot:
            sync_mode = "download"
        else:
            sync_mode = "new"
        self.log_event(
            f"REALM_ONLINE profile={self.profile} actor={self.actor_id.hex()} "
            f"revision={head.revision} day={day_id} "
            f"sync={sync_mode}"
        )
        self.last_clock_day = day_id
        self.next_clock_at = self.now() + 30.0
        self.next_directory_at = self.now()
        if send_snapshot and head.snapshot is not None:
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
        if len(message.payload) != 1:
            return
        outgoing = self.outgoing_event
        if (
            outgoing is not None
            and message.transaction_id == outgoing.transaction_id
            and message.payload[0] == p4rm.EVENT_BEGIN
            and message.chunk_index == p4rm.BEGIN_INDEX
            and not outgoing.body_sent
            and outgoing.event.body
        ):
            outgoing.body_sent = True
            self._send_event_part()
            return
        if self.download is None:
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

    def _begin_action(self, message: p4rm.Message) -> None:
        kind, code, value, target, nonce, body_bytes, body_crc = (
            p4rm.decode_action_begin(message.payload)
        )
        if self.awaiting_local_commit:
            self._send_realm(
                p4rm.ACTION_RESULT,
                message.transaction_id,
                p4rm.encode_action_result(
                    p4rm.ACTION_BUSY, kind, 0, 0, 0
                ),
            )
            return
        expected_chunks = 1 if body_bytes else 0
        if (
            message.chunk_index != p4rm.BEGIN_INDEX
            or message.chunk_count != expected_chunks
        ):
            self._send_realm(
                p4rm.ACTION_RESULT,
                message.transaction_id,
                p4rm.encode_action_result(
                    p4rm.ACTION_INVALID, kind, 0, 0, 0
                ),
            )
            return
        self.action_upload = ActionUpload(
            message.transaction_id,
            kind,
            code,
            value,
            target,
            nonce,
            body_bytes,
            body_crc,
        )
        self._send_realm(
            p4rm.ACK,
            message.transaction_id,
            bytes([p4rm.ACTION_BEGIN]),
            p4rm.BEGIN_INDEX,
            expected_chunks,
        )
        if body_bytes == 0:
            self._finish_action(b"")

    def _receive_action_body(self, message: p4rm.Message) -> None:
        action = self.action_upload
        if (
            action is None
            or message.transaction_id != action.transaction_id
            or action.body_bytes == 0
            or message.chunk_index != 0
            or message.chunk_count != 1
            or len(message.payload) != action.body_bytes
            or zlib.crc32(message.payload) & 0xFFFFFFFF != action.body_crc32
        ):
            return
        self._send_realm(
            p4rm.ACK,
            message.transaction_id,
            bytes([p4rm.ACTION_BODY]),
            0,
            1,
        )
        self._finish_action(message.payload)

    def _finish_action(self, body: bytes) -> None:
        action = self.action_upload
        if action is None:
            return
        try:
            result = self.store.perform_action(
                self.actor_id,
                action.nonce,
                action.kind,
                action.code,
                action.value,
                action.target_actor_id,
                body,
            )
        except (OSError, RuntimeError, sqlite3.Error, ValueError):
            self._send_realm(
                p4rm.ERROR, action.transaction_id, struct.pack("<H", 3)
            )
            self.action_upload = None
            return
        self._send_realm(
            p4rm.ACTION_RESULT,
            action.transaction_id,
            p4rm.encode_action_result(
                result.status,
                action.kind,
                result.code,
                result.value,
                result.related_id,
            ),
        )
        self.action_upload = None
        self.next_directory_at = self.now()

    def _send_event_part(self) -> None:
        outgoing = self.outgoing_event
        if outgoing is None:
            return
        event = outgoing.event
        if outgoing.body_sent:
            self._send_realm(
                p4rm.EVENT_BODY,
                outgoing.transaction_id,
                event.body,
                0,
                1,
            )
        else:
            self._send_realm(
                p4rm.EVENT_BEGIN,
                outgoing.transaction_id,
                p4rm.encode_event_begin(
                    event.event_id,
                    event.kind,
                    event.code,
                    event.value,
                    event.source_actor_id,
                    event.source_name[:15],
                    event.body,
                ),
                p4rm.BEGIN_INDEX,
                1 if event.body else 0,
            )
        outgoing.last_sent_at = self.now()

    def _receive_event_ack(self, message: p4rm.Message) -> None:
        outgoing = self.outgoing_event
        if outgoing is None or len(message.payload) != 8:
            return
        event_id = struct.unpack("<Q", message.payload)[0]
        if (
            message.transaction_id != outgoing.transaction_id
            or event_id != outgoing.event.event_id
        ):
            return
        try:
            received = self.store.acknowledge_event(self.actor_id, event_id)
            head = self.store.read_head(self.actor_id)
            committed_event_cursor = 0
            if head.snapshot is not None:
                committed_event_cursor = decode_lord_sync(
                    head.snapshot,
                    expected_actor_id=self.actor_id,
                ).last_realm_event_id
        except (OSError, RuntimeError, sqlite3.Error, ValueError):
            return
        if not received:
            return
        self.received_event_cursor = max(self.received_event_cursor, event_id)
        self.awaiting_event_commit = (
            0 if committed_event_cursor >= event_id else event_id
        )
        self.outgoing_event = None

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
            self.adoption_grant_id,
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
        commit_adoption_grant = upload.adoption_grant_id
        if commit_adoption_grant is None and self.adoption_retry is not None:
            retry_grant, retry_nonce, retry_sha256 = self.adoption_retry
            if (
                upload.nonce == retry_nonce
                and hashlib.sha256(record).digest() == retry_sha256
            ):
                commit_adoption_grant = retry_grant
        if (
            len(record) != upload.total_bytes
            or zlib.crc32(record) & 0xFFFFFFFF != upload.crc32
            or not validate_lord_sync(
                record,
                self.actor_id,
                upload.nonce,
                allow_adoption_bridge=commit_adoption_grant is not None,
            )
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
                adoption_grant_id=commit_adoption_grant,
                now=self.now(),
            )
            current_head_revision = (
                self.store.read_head(self.actor_id).revision
                if status == "ok"
                else revision
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
            "stale-day": p4rm.COMMIT_STALE_DAY,
        }[status]
        self._send_commit_result(message.transaction_id, result, revision)
        if status == "stale-day" and self.awaiting_local_commit:
            # The stock client applies exactly one trusted hourly refresh
            # before regenerating this dirty branch for the returned day.
            self.awaiting_local_rollover_allowed = True
        commit_matches_hello = True
        if self.awaiting_local_commit:
            try:
                committed_generation = decode_lord_sync(
                    record,
                    expected_actor_id=self.actor_id,
                    expected_nonce=upload.nonce,
                    allow_adoption_bridge=commit_adoption_grant is not None,
                ).save_sequence
            except ValueError:
                commit_matches_hello = False
            else:
                rollover_generation = (
                    None
                    if self.awaiting_local_save_sequence is None
                    or self.awaiting_local_save_sequence == 0xFFFFFFFF
                    else self.awaiting_local_save_sequence + 1
                )
                commit_matches_hello = committed_generation == (
                    self.awaiting_local_save_sequence
                ) or (
                    self.awaiting_local_rollover_allowed
                    and committed_generation == rollover_generation
                )
        # Store-level idempotency deliberately returns an operation's original
        # revision.  It can release a dirty-first barrier only if it is still
        # the authoritative head *and* it is the local generation accepted by
        # this session's HELLO (optionally plus its one authorized rollover).
        # Replaying an old or already-current record cannot smuggle a different
        # dirty branch past synchronization.
        if (
            status == "ok"
            and revision == current_head_revision
            and commit_matches_hello
        ):
            self.awaiting_local_commit = False
            self.awaiting_local_save_sequence = None
            self.awaiting_local_rollover_allowed = False
            if self.awaiting_event_commit != 0:
                committed = decode_lord_sync(
                    record,
                    expected_actor_id=self.actor_id,
                    expected_nonce=upload.nonce,
                    allow_adoption_bridge=commit_adoption_grant is not None,
                )
                if (
                    committed.last_realm_event_id
                    >= self.awaiting_event_commit
                ):
                    self.awaiting_event_commit = 0
            if upload.adoption_grant_id is not None:
                self.adoption_retry = (
                    upload.adoption_grant_id,
                    upload.nonce,
                    hashlib.sha256(record).digest(),
                )
                self.adoption_grant_id = None
            elif commit_adoption_grant is None:
                self.adoption_retry = None
        self.upload = None

    def _receive_profile(self, message: p4rm.Message) -> None:
        # Legacy clients still emit this advertisement.  Keep only the
        # ephemeral at-inn bit; protected state is projected from commits.
        if len(message.payload) != 48 or message.payload[23] & ~0x03:
            return
        try:
            self.store.update_presence(
                self.actor_id, at_inn=bool(message.payload[23] & 0x02)
            )
        except (ValueError, OSError, RuntimeError, sqlite3.Error):
            return
        self.next_directory_at = self.now()

    def _receive_profile_stats(self, message: p4rm.Message) -> None:
        # Economy/progression packets are deliberately ignored; see PROFILE.
        return

    def _queue_directory(self) -> None:
        total = self.store.profile_count(exclude_actor_id=self.actor_id)
        if total == 0:
            self.directory_offset = 0
        elif self.directory_offset >= total:
            self.directory_offset = (total - 1) // 8 * 8
        profiles = self.store.list_profiles(
            exclude_actor_id=self.actor_id,
            offset=self.directory_offset,
        )
        pending: list[tuple[int, int, int, bytes]] = []
        count = len(profiles)
        pending.append(
            (
                p4rm.DIRECTORY_PAGE,
                0,
                0,
                struct.pack("<HH", self.directory_offset, total),
            )
        )
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
                "<iiiiIIBB2x",
                profile.hit_points,
                profile.max_hit_points,
                profile.strength,
                profile.defense,
                profile.experience,
                profile.chompcoin,
                profile.trust,
                int(profile.teamed),
            )
            pending.append((p4rm.DIRECTORY_SUMMARY, index, count, bytes(summary)))
            pending.append((p4rm.DIRECTORY_STATS, index, count, stats))
        self.directory_pending = pending

    def _receive_directory_page(self, message: p4rm.Message) -> None:
        if len(message.payload) != 2:
            return
        offset = struct.unpack("<H", message.payload)[0]
        if offset >= 100 or offset % 8 != 0:
            return
        self.directory_offset = offset
        try:
            self._queue_directory()
        except (ValueError, OSError, RuntimeError, sqlite3.Error):
            self._send_realm(p4rm.ERROR, message.transaction_id, struct.pack("<H", 3))

    def _send_commit_result(self, transaction_id: int, status: int, revision: int) -> None:
        if status not in (p4rm.COMMIT_OK, p4rm.COMMIT_STALE_DAY):
            self.game_online = False
        day_id, remaining = self.store.realm_clock(self.now())
        payload = struct.pack("<BIQI", status, revision, day_id, remaining)
        self._send_realm(p4rm.COMMIT_RESULT, transaction_id, payload)

    def tick(self) -> None:
        now = self.now()
        if self.connected and now - self.last_rx_at >= 15.0:
            self._reset_lobby()
            return
        if (
            self.connected
            and now < self.start_ready_until
            and now >= self.next_start_ready_at
        ):
            self._send_start_ready()
            return
        if self.connected and now - self.last_tx_at >= 1.0:
            stamp = int(now * 1000) & 0xFFFFFFFFFFFFFFFF
            self._send_packet(p4mp.PING, struct.pack("<Q", stamp))
        if not self.game_online:
            return
        if self.download is not None and now - self.download.last_sent_at >= 1.0:
            self._send_download_part()
            return
        if self.download is not None:
            return
        if (
            self.awaiting_local_commit
            or self.awaiting_event_commit != 0
            or self.upload is not None
        ):
            return
        if self.outgoing_event is not None:
            if now - self.outgoing_event.last_sent_at >= 1.0:
                self._send_event_part()
            return
        try:
            event = self.store.next_event(
                self.actor_id, after_event_id=self.received_event_cursor
            )
        except (OSError, RuntimeError, sqlite3.Error):
            self._send_realm(p4rm.ERROR, _nonzero_u32(), struct.pack("<H", 3))
            self.game_online = False
            return
        if event is not None:
            self.outgoing_event = OutgoingEvent(_nonzero_u32(), event)
            self._send_event_part()
            return
        if now >= self.next_directory_at:
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
