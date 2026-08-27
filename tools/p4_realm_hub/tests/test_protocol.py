from __future__ import annotations

import sqlite3
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from tools.p4_realm_hub import p4mp, p4rm
from tools.p4_realm_hub.store import MAX_REALM_PLAYERS, RealmFullError, RealmStore
from tools.p4_realm_hub.hub import LORD_GAME_ID, LORD_P4RM_PROTOCOL, RealmHubSession
from tools.p4_realm_hub.store import RealmProfile
from tools.p4_realm_hub.ble_link import BleReassembler, fragment_datagram


def test_offer(seed: int = 0xABCDEF) -> p4mp.Offer:
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
        1, 1, 0, 1, 2, 0, 30, LORD_P4RM_PROTOCOL, seed,
        LORD_GAME_ID, content, compatibility, bytes(8),
    )


class P4MPTests(unittest.TestCase):
    def test_packet_and_noisy_stream_round_trip(self) -> None:
        payload = p4rm.encode_message(p4rm.HELLO, 7, b"hello")
        frame = p4mp.encode_packet(p4mp.GAME_MESSAGE, 11, 22, 3, payload)
        packet = p4mp.decode_packet(frame)
        self.assertEqual(packet.payload, payload)
        decoder = p4mp.StreamDecoder()
        self.assertEqual(decoder.feed(b"boot noise" + frame[:17]), [])
        self.assertEqual(decoder.feed(frame[17:]), [frame])
        self.assertGreaterEqual(decoder.discarded_bytes, len(b"boot noise"))

    def test_unknown_packet_type_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            p4mp.encode_packet(12, 1, 2, 3, b"x")

    def test_offer_and_join_match_firmware_layout(self) -> None:
        expected = test_offer(77)
        offer = p4mp.decode_offer(p4mp.encode_offer(expected))
        self.assertEqual(offer, expected)
        self.assertEqual(offer.game_id, LORD_GAME_ID)
        self.assertEqual(offer.session_seed, 77)
        join = p4mp.encode_join(offer.compatibility_sha256, 123)
        self.assertEqual(join[0:4], b"\x02\xff\x00\x00")
        self.assertEqual(join[4:36], offer.compatibility_sha256)
        self.assertEqual(struct.unpack_from("<I", join, 36)[0], 123)
        self.assertEqual(p4mp.decode_join(join),
                         (0xFF, offer.compatibility_sha256, 123))
        accept = p4mp.encode_accept(1, 2, 0, 0, 77, bytes(8))
        self.assertEqual(p4mp.decode_accept(accept),
                         (1, 2, 0, 0, 77, bytes(8)))
        token = p4mp.start_token(10, 77)
        self.assertEqual(p4mp.decode_start_ready(
            p4mp.encode_start_ready(token)), token)


class P4RMTests(unittest.TestCase):
    def test_offline_hello_round_trip_and_validation(self) -> None:
        actor = bytes(range(1, 17))
        flags = (
            p4rm.HELLO_HAS_LOCAL
            | p4rm.HELLO_HAS_SYNC_BASE
            | p4rm.HELLO_LOCAL_DIRTY
        )
        hello = p4rm.encode_hello(actor, 9, 40, 42, flags)
        self.assertEqual(
            p4rm.decode_hello(hello),
            (actor, 9, 40, 42, flags),
        )
        with self.assertRaises(ValueError):
            p4rm.encode_hello(bytes(16), 0, 0, 1, p4rm.HELLO_LOCAL_DIRTY)
        with self.assertRaises(ValueError):
            p4rm.encode_hello(
                actor,
                9,
                40,
                42,
                p4rm.HELLO_HAS_LOCAL | p4rm.HELLO_HAS_SYNC_BASE,
            )

    def test_chunking_preserves_max_record(self) -> None:
        record = bytes(index & 0xFF for index in range(p4rm.MAX_RECORD_BYTES))
        chunks = p4rm.record_chunks(record)
        self.assertEqual(len(chunks), p4rm.MAX_CHUNKS)
        self.assertTrue(all(len(chunk) <= p4rm.MAX_PAYLOAD_BYTES for chunk in chunks))
        self.assertEqual(b"".join(chunks), record)
        for index, chunk in enumerate(chunks):
            wire = p4rm.encode_message(
                p4rm.UPLOAD_CHUNK, 99, chunk, index, len(chunks)
            )
            decoded = p4rm.decode_message(wire)
            self.assertEqual(decoded.payload, chunk)

    def test_welcome_and_clock(self) -> None:
        actor = bytes(range(16))
        welcome = p4rm.encode_welcome(
            actor, 4, 1234, 300, p4rm.WELCOME_HAS_SNAPSHOT
        )
        self.assertEqual(
            p4rm.decode_welcome(welcome),
            (actor, 4, 1234, 300, p4rm.WELCOME_HAS_SNAPSHOT),
        )
        self.assertEqual(p4rm.decode_clock(p4rm.encode_clock(9, 1, True)), (9, 1, True))

    def test_ble_fragment_round_trip_and_gap_rejection(self) -> None:
        frame = p4mp.encode_packet(
            p4mp.GAME_MESSAGE,
            11,
            22,
            33,
            p4rm.encode_message(p4rm.HELLO, 44),
        )
        fragments = fragment_datagram(frame, 7, 23)
        reassembler = BleReassembler()
        result = None
        for fragment in fragments:
            result = reassembler.consume(fragment)
        self.assertEqual(result, frame)
        reassembler = BleReassembler()
        self.assertIsNone(reassembler.consume(fragments[0]))
        self.assertIsNone(reassembler.consume(fragments[-1]))

    def test_action_and_event_codecs(self) -> None:
        actor = bytes(range(1, 17))
        body = b"MEET ME AT THE INN"
        action = p4rm.encode_action_begin(
            p4rm.ACTION_MAIL, 0, 0, actor, 77, body
        )
        self.assertEqual(
            p4rm.decode_action_begin(action)[:6],
            (p4rm.ACTION_MAIL, 0, 0, actor, 77, len(body)),
        )
        result = p4rm.encode_action_result(
            p4rm.ACTION_OK, p4rm.ACTION_TRANSFER, 0, 100, 9
        )
        self.assertEqual(
            p4rm.decode_action_result(result),
            (p4rm.ACTION_OK, p4rm.ACTION_TRANSFER, 0, 100, 9),
        )
        event = p4rm.encode_event_begin(
            10, p4rm.ACTION_MAIL, 0, 0, actor, "Test Hero", body
        )
        self.assertEqual(
            p4rm.decode_event_begin(event),
            (10, p4rm.ACTION_MAIL, 0, 0, actor, "Test Hero", len(body)),
        )


class StoreTests(unittest.TestCase):
    @staticmethod
    def add_profile(
        store: RealmStore, profile: str, name: str, coin: int, bank: int = 500
    ) -> bytes:
        actor = store.actor_for_profile(profile)
        store.update_profile(
            RealmProfile(
                actor, name, 0, 1, 3, 1, 20, 30, 12, 4,
                500, coin, 2, 1, True, bank,
            )
        )
        return actor

    @staticmethod
    def drain_events(store: RealmStore, actor: bytes) -> list:
        events = []
        while (event := store.next_event(actor)) is not None:
            events.append(event)
            store.acknowledge_event(actor, event.event_id)
        return events

    def test_cas_idempotency_and_hourly_day(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3", epoch_seconds=0)
            self.assertEqual(store.realm_clock(0), (1, 3600))
            self.assertEqual(store.realm_clock(3599), (1, 1))
            self.assertEqual(store.realm_clock(3600), (2, 3600))
            actor = store.actor_for_profile("console-one")
            self.assertEqual(actor, store.actor_for_profile("console-one"))
            day, _ = store.realm_clock()
            status, revision = store.commit(actor, 0, 7, day, b"snapshot")
            self.assertEqual((status, revision), ("ok", 1))
            self.assertEqual(store.commit(actor, 0, 7, day, b"snapshot"), ("ok", 1))
            self.assertEqual(store.commit(actor, 0, 7, day, b"different")[0], "invalid")
            self.assertEqual(store.commit(actor, 0, 8, day, b"other")[0], "conflict")
            self.assertEqual(store.read_head(actor).snapshot, b"snapshot")
            store.update_profile(
                RealmProfile(
                    actor,
                    "Test Hero",
                    0,
                    1,
                    3,
                    1,
                    20,
                    30,
                    12,
                    4,
                    500,
                    0,
                    2,
                    1,
                    True,
                )
            )
            other = store.actor_for_profile("console-two")
            profiles = store.list_profiles(exclude_actor_id=other)
            self.assertEqual(profiles[0].name, "Test Hero")

    def test_realm_accepts_exactly_one_hundred_player_accounts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actors = {
                store.actor_for_profile(f"console-{index:03d}")
                for index in range(MAX_REALM_PLAYERS)
            }
            self.assertEqual(len(actors), MAX_REALM_PLAYERS)
            self.assertEqual(
                store.actor_for_profile("console-000"),
                next(
                    actor
                    for actor in actors
                    if actor == store.actor_for_profile("console-000")
                ),
            )
            with self.assertRaises(RealmFullError):
                store.actor_for_profile("console-100")

    def test_cross_actor_actions_are_idempotent_and_durable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            first = self.add_profile(store, "console-one", "First Hero", 200)
            second = self.add_profile(store, "console-two", "Second Hero", 1000)

            mail = store.perform_action(
                first, 1, p4rm.ACTION_MAIL, 0, 0, second, b"HELLO FRIEND"
            )
            duplicate = store.perform_action(
                first, 1, p4rm.ACTION_MAIL, 0, 0, second, b"HELLO FRIEND"
            )
            self.assertEqual(mail, duplicate)
            events = self.drain_events(store, second)
            self.assertEqual(len(events), 1)
            self.assertEqual(events[0].body, b"HELLO FRIEND")
            self.assertEqual(
                store.perform_action(
                    first, 1, p4rm.ACTION_MAIL, 0, 0, second, b"CHANGED"
                ).status,
                p4rm.ACTION_INVALID,
            )

            transfer = store.perform_action(
                first, 2, p4rm.ACTION_TRANSFER, 0, 100, second, b""
            )
            self.assertEqual((transfer.status, transfer.value), (p4rm.ACTION_OK, 100))
            debit = self.drain_events(store, first)
            self.assertEqual((debit[0].code, debit[0].value), (1, 100))
            self.assertEqual(self.drain_events(store, second)[0].value, 100)
            first_profile = store.list_profiles(exclude_actor_id=second)[0]
            self.assertEqual(first_profile.bank, 400)

            nonce = 10
            for source, target in ((first, second), (second, first)):
                for _ in range(4):
                    self.assertEqual(
                        store.perform_action(
                            source, nonce, p4rm.ACTION_FRIEND, 0, 0, target, b""
                        ).status,
                        p4rm.ACTION_OK,
                    )
                    nonce += 1
            self.drain_events(store, first)
            self.drain_events(store, second)
            invite = store.perform_action(
                first, 30, p4rm.ACTION_TEAM, 0, 0, second, b""
            )
            self.assertEqual((invite.status, invite.code), (p4rm.ACTION_OK, 0))
            self.drain_events(store, second)
            self.drain_events(store, first)
            formed = store.perform_action(
                second, 31, p4rm.ACTION_TEAM, 0, 0, first, b""
            )
            self.assertEqual((formed.status, formed.code), (p4rm.ACTION_OK, 1))
            second_from_first = store.list_profiles(
                exclude_actor_id=first, limit=8, offset=0
            )[0]
            self.assertGreaterEqual(second_from_first.trust, 60)
            self.assertTrue(second_from_first.teamed)
            self.assertEqual(self.drain_events(store, first)[0].code, 1)
            self.drain_events(store, second)

            mentor = store.perform_action(
                first, 32, p4rm.ACTION_MENTOR, 0, 0, second, b""
            )
            self.assertEqual(mentor.status, p4rm.ACTION_OK)
            self.drain_events(store, first)
            self.drain_events(store, second)
            third = self.add_profile(store, "console-three", "Third Hero", 300)
            denied_mentor = store.perform_action(
                first, 33, p4rm.ACTION_MENTOR, 0, 0, third, b""
            )
            self.assertEqual(denied_mentor.status, p4rm.ACTION_DENIED)

            lease = store.perform_action(
                first, 40, p4rm.ACTION_PVP_BEGIN, 0, 0, second, b""
            )
            self.assertEqual(lease.status, p4rm.ACTION_OK)
            resolution = store.perform_action(
                first,
                41,
                p4rm.ACTION_PVP_RESOLVE,
                1,
                0,
                second,
                lease.related_id.to_bytes(8, "little") + b"\x01",
            )
            self.assertEqual((resolution.status, resolution.value), (p4rm.ACTION_OK, 550))
            duel_event = self.drain_events(store, second)[0]
            self.assertEqual((duel_event.code, duel_event.value), (1, 550))

            post = store.perform_action(
                first, 50, p4rm.ACTION_TAVERN, 0, 0, b"\0" * 16,
                b"DRAGON AT MIDNIGHT",
            )
            self.assertEqual(post.status, p4rm.ACTION_OK)
            self.assertEqual(self.drain_events(store, second)[0].body,
                             b"DRAGON AT MIDNIGHT")


def lord_record(actor_id: bytes, nonce: int) -> bytes:
    save = bytearray(24)
    save[:4] = b"LDSV"
    struct.pack_into("<HHII", save, 4, 3, len(save), 0, 2)
    struct.pack_into("<II", save, 16, 0x12345678, 1)
    struct.pack_into("<I", save, 8, zlib.crc32(save[16:]) & 0xFFFFFFFF)
    record = bytearray(52 + len(save))
    record[:4] = b"LRSY"
    struct.pack_into("<HHIIIIQ", record, 4, 1, 52, len(record), 0, 1, 2, nonce)
    record[32:48] = actor_id
    struct.pack_into("<I", record, 48, len(save))
    record[52:] = save
    struct.pack_into("<I", record, 12, zlib.crc32(record[16:]) & 0xFFFFFFFF)
    return bytes(record)


class HubSessionTests(unittest.TestCase):
    def test_download_retries_and_malformed_message_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            clock = [1000.0]
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("console-one")
            day, _ = store.realm_clock()
            record = lord_record(actor, 123)
            self.assertEqual(store.commit(actor, 0, 123, day, record), ("ok", 1))
            sent: list[bytes] = []
            hub = RealmHubSession(
                "console-one", store, sent.append, test_offer(),
                now=lambda: clock[0]
            )
            hub.session_id = 10
            hub.remote_peer_id = 20
            hub.connected = True
            hub._begin_game_sync(
                p4rm.Message(
                    p4rm.HELLO,
                    55,
                    0,
                    0,
                    p4rm.encode_hello(bytes(16), 0, 0, 0, 0),
                )
            )
            self.assertEqual(
                p4rm.decode_message(p4mp.decode_packet(sent[0]).payload).kind,
                p4rm.WELCOME,
            )
            begin = sent[1]
            clock[0] += 1.1
            hub.tick()
            self.assertEqual(
                p4mp.decode_packet(sent[-1]).payload,
                p4mp.decode_packet(begin).payload,
            )

            sent.clear()
            hub.transport_disconnected()
            hub.session_id = 10
            hub.remote_peer_id = 20
            hub.connected = True
            malformed = p4mp.encode_packet(
                p4mp.GAME_MESSAGE, 10, 20, 1, b"not-a-p4rm-frame"
            )
            hub.receive(malformed)
            error = p4rm.decode_message(p4mp.decode_packet(sent[-1]).payload)
            self.assertEqual(error.kind, p4rm.ERROR)

    def test_offline_reconnect_accepts_or_conflicts_without_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("console-one")
            day, _ = store.realm_clock()
            record = lord_record(actor, 123)
            self.assertEqual(
                store.commit(actor, 0, 123, day, record),
                ("ok", 1),
            )
            with sqlite3.connect(store.path) as database:
                database.execute(
                    "UPDATE heads SET last_day_id = ? WHERE actor_id = ?",
                    (day - 1, actor),
                )
            sent: list[bytes] = []
            hub = RealmHubSession(
                "console-one", store, sent.append, test_offer()
            )
            hub.session_id = 10
            hub.remote_peer_id = 20
            hub.connected = True

            dirty_flags = (
                p4rm.HELLO_HAS_LOCAL
                | p4rm.HELLO_HAS_SYNC_BASE
                | p4rm.HELLO_LOCAL_DIRTY
            )
            hub._begin_game_sync(
                p4rm.Message(
                    p4rm.HELLO,
                    60,
                    0,
                    0,
                    p4rm.encode_hello(actor, 1, 2, 3, dirty_flags),
                )
            )
            welcome = p4rm.decode_message(
                p4mp.decode_packet(sent.pop(0)).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(welcome.payload)[4],
                p4rm.WELCOME_ACCEPT_LOCAL | p4rm.WELCOME_ROLLOVER_PENDING,
            )
            self.assertEqual(sent, [])

            hub._begin_game_sync(
                p4rm.Message(
                    p4rm.HELLO,
                    61,
                    0,
                    0,
                    p4rm.encode_hello(actor, 2, 2, 3, dirty_flags),
                )
            )
            welcome = p4rm.decode_message(
                p4mp.decode_packet(sent.pop(0)).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(welcome.payload)[4],
                p4rm.WELCOME_LOCAL_CONFLICT,
            )
            self.assertFalse(hub.game_online)
            self.assertEqual(sent, [])

            clean_flags = p4rm.HELLO_HAS_LOCAL | p4rm.HELLO_HAS_SYNC_BASE
            hub._begin_game_sync(
                p4rm.Message(
                    p4rm.HELLO,
                    62,
                    0,
                    0,
                    p4rm.encode_hello(actor, 2, 3, 3, clean_flags),
                )
            )
            welcome = p4rm.decode_message(
                p4mp.decode_packet(sent.pop(0)).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(welcome.payload)[4],
                p4rm.WELCOME_HAS_SNAPSHOT | p4rm.WELCOME_ROLLOVER_PENDING,
            )
            self.assertGreaterEqual(len(sent), 1)

    def test_join_upload_commit_and_profile(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            sent: list[bytes] = []
            hub = RealmHubSession(
                "console-one", store, sent.append, test_offer()
            )
            session_id = hub.session_id
            host_peer_id = hub.peer_id
            console_peer_id = host_peer_id ^ 0xFFFFFFFF
            hub.receive(p4mp.encode_packet(p4mp.DISCOVER, 0, 0, 1))
            advertised = p4mp.decode_packet(sent.pop(0))
            self.assertEqual(advertised.packet_type, p4mp.OFFER)
            self.assertEqual(p4mp.decode_offer(advertised.payload), hub.offer)
            join = p4mp.encode_join(hub.offer.compatibility_sha256, 123)
            hub.receive(p4mp.encode_packet(
                p4mp.JOIN, session_id, console_peer_id, 2, join
            ))
            self.assertTrue(hub.connected)
            accepted = p4mp.decode_packet(sent.pop(0))
            self.assertEqual(accepted.packet_type, p4mp.ACCEPT)
            self.assertEqual(p4mp.decode_accept(accepted.payload)[:2], (1, 2))
            ready = p4mp.decode_packet(sent.pop(0))
            self.assertEqual(ready.packet_type, p4mp.PING)
            self.assertEqual(
                p4mp.decode_start_ready(ready.payload),
                p4mp.start_token(session_id, hub.offer.session_seed),
            )
            hello = p4rm.encode_message(
                p4rm.HELLO,
                77,
                p4rm.encode_hello(bytes(16), 0, 0, 0, 0),
            )
            hub.receive(p4mp.encode_packet(
                p4mp.GAME_MESSAGE, session_id, console_peer_id, 3, hello
            ))
            welcome = p4rm.decode_message(p4mp.decode_packet(sent.pop(0)).payload)
            self.assertEqual(welcome.kind, p4rm.WELCOME)
            day, _ = store.realm_clock()
            nonce = 99
            record = lord_record(hub.actor_id, nonce)
            chunks = p4rm.record_chunks(record)
            begin = p4rm.encode_message(
                p4rm.UPLOAD_BEGIN,
                88,
                p4rm.encode_upload_begin(0, record, nonce, day),
                p4rm.BEGIN_INDEX,
                len(chunks),
            )
            hub.receive(p4mp.encode_packet(
                p4mp.GAME_MESSAGE, session_id, console_peer_id, 4, begin
            ))
            self.assertEqual(
                p4rm.decode_message(p4mp.decode_packet(sent.pop(0)).payload).kind,
                p4rm.ACK,
            )
            sequence = 5
            for index, chunk in enumerate(chunks):
                wire = p4rm.encode_message(
                    p4rm.UPLOAD_CHUNK, 88, chunk, index, len(chunks)
                )
                hub.receive(
                    p4mp.encode_packet(
                        p4mp.GAME_MESSAGE, session_id, console_peer_id, sequence, wire
                    )
                )
                sequence += 1
                ack = p4rm.decode_message(p4mp.decode_packet(sent.pop(0)).payload)
                self.assertEqual(ack.kind, p4rm.ACK)
            result = p4rm.decode_message(p4mp.decode_packet(sent.pop(0)).payload)
            self.assertEqual(result.kind, p4rm.COMMIT_RESULT)
            self.assertEqual(result.payload[0], p4rm.COMMIT_OK)
            self.assertEqual(store.read_head(hub.actor_id).snapshot, record)

            profile = bytearray(48)
            profile[:9] = b"Test Hero"
            profile[20:24] = bytes([0, 1, 3, 1])
            struct.pack_into("<iiiiHHI", profile, 24, 20, 30, 12, 4, 2, 1, 500)
            wire = p4rm.encode_message(p4rm.PROFILE, 90, bytes(profile))
            hub.receive(
                p4mp.encode_packet(
                    p4mp.GAME_MESSAGE, session_id, console_peer_id, sequence, wire
                )
            )
            sequence += 1
            wire = p4rm.encode_message(
                p4rm.PROFILE_STATS, 90, struct.pack("<II", 1234, 4321)
            )
            hub.receive(
                p4mp.encode_packet(
                    p4mp.GAME_MESSAGE, session_id, console_peer_id, sequence, wire
                )
            )
            other = store.actor_for_profile("console-two")
            saved_profile = store.list_profiles(exclude_actor_id=other)[0]
            self.assertEqual(saved_profile.name, "Test Hero")
            self.assertEqual(saved_profile.chompcoin, 1234)
            self.assertEqual(saved_profile.bank, 4321)

    def test_two_node_mail_delivery_acknowledges_durable_event(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            clock = [1000.0]
            store = RealmStore(Path(directory) / "realm.sqlite3")
            first_actor = StoreTests.add_profile(
                store, "console-one", "First Hero", 200
            )
            second_actor = StoreTests.add_profile(
                store, "console-two", "Second Hero", 300
            )
            first_sent: list[bytes] = []
            second_sent: list[bytes] = []
            first = RealmHubSession(
                "console-one", store, first_sent.append, test_offer(),
                now=lambda: clock[0]
            )
            second = RealmHubSession(
                "console-two", store, second_sent.append, test_offer(),
                now=lambda: clock[0]
            )
            for session in (first, second):
                session.session_id = 10
                session.remote_peer_id = 20
                session.connected = True
                session.game_online = True
                session.next_directory_at = clock[0] + 60
                session.next_clock_at = clock[0] + 60

            body = b"MEET ME AT THE INN"
            begin = p4rm.Message(
                p4rm.ACTION_BEGIN,
                70,
                p4rm.BEGIN_INDEX,
                1,
                p4rm.encode_action_begin(
                    p4rm.ACTION_MAIL, 0, 0, second_actor, 700, body
                ),
            )
            first._receive_realm(begin)
            first._receive_realm(
                p4rm.Message(p4rm.ACTION_BODY, 70, 0, 1, body)
            )
            first_kinds = [
                p4rm.decode_message(p4mp.decode_packet(frame).payload).kind
                for frame in first_sent
            ]
            self.assertEqual(
                first_kinds,
                [p4rm.ACK, p4rm.ACK, p4rm.ACTION_RESULT],
            )

            second.tick()
            event_begin = p4rm.decode_message(
                p4mp.decode_packet(second_sent.pop()).payload
            )
            self.assertEqual(event_begin.kind, p4rm.EVENT_BEGIN)
            event_id = p4rm.decode_event_begin(event_begin.payload)[0]
            second._receive_realm(
                p4rm.Message(
                    p4rm.ACK,
                    event_begin.transaction_id,
                    p4rm.BEGIN_INDEX,
                    1,
                    bytes([p4rm.EVENT_BEGIN]),
                )
            )
            event_body = p4rm.decode_message(
                p4mp.decode_packet(second_sent.pop()).payload
            )
            self.assertEqual((event_body.kind, event_body.payload),
                             (p4rm.EVENT_BODY, body))
            second._receive_realm(
                p4rm.Message(
                    p4rm.EVENT_ACK,
                    event_begin.transaction_id,
                    0,
                    0,
                    struct.pack("<Q", event_id),
                )
            )
            self.assertIsNone(store.next_event(second_actor))
            self.assertNotEqual(first_actor, second_actor)


if __name__ == "__main__":
    unittest.main()
