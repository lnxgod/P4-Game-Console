from __future__ import annotations

import hashlib
import importlib.util
import sqlite3
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from tools.p4_realm_hub import p4mp, p4rm
from tools.p4_realm_hub.lord_snapshot import decode_lord_sync
from tools.p4_realm_hub.store import MAX_REALM_PLAYERS, RealmFullError, RealmStore
from tools.p4_realm_hub.hub import (
    LORD_GAME_ID,
    LORD_P4RM_PROTOCOL,
    RealmHubSession,
    validate_lord_sync,
)
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
    def test_directory_sidecars_are_bounded_v3_extensions(self) -> None:
        actor = bytes(range(1, 17))
        payload = actor + bytes((7, 0))
        message = p4rm.decode_message(
            p4rm.encode_message(
                p4rm.DIRECTORY_DEEDS,
                6,
                payload,
                chunk_index=2,
                chunk_count=3,
            )
        )
        self.assertEqual(
            (message.kind, message.chunk_index, message.chunk_count),
            (p4rm.DIRECTORY_DEEDS, 2, 3),
        )
        self.assertEqual(message.payload, payload)
        with self.assertRaises(ValueError):
            p4rm.encode_message(p4rm.GUILD_SUMMARY + 1, 7)

        status = p4rm.encode_guild_status(
            actor,
            21,
            16,
            8,
            p4rm.GUILD_ROLE_LEADER,
            1000,
            44,
            3,
            11,
            12,
            7,
            6,
            5,
            p4rm.GUILD_OUTCOME_WIN,
            1,
            p4rm.GUILD_DAILY_RALLIED | p4rm.GUILD_DAILY_ELIGIBLE,
        )
        self.assertEqual(len(status), 48)
        self.assertEqual(p4rm.decode_guild_status(status)[1:5], (21, 16, 8, 1))
        directory = p4rm.encode_directory_guild(actor, 21, 16)
        self.assertEqual(len(directory), 24)
        self.assertEqual(
            p4rm.decode_directory_guild(directory), (actor, 21, 16)
        )
        self.assertEqual(
            p4rm.decode_directory_guild(
                p4rm.encode_directory_guild(actor, 0, 0)
            ),
            (actor, 0, 0),
        )
        self.assertEqual(
            p4rm.decode_guild_page(p4rm.encode_guild_page(8, 16)), (8, 16)
        )
        self.assertEqual(
            p4rm.decode_guild_page_request(p4rm.encode_guild_page_request(8)), 8
        )
        summary = p4rm.encode_guild_summary(21, 16, 8, 255, 1000, 44, 7, 6, 5)
        self.assertEqual(len(summary), 24)
        self.assertEqual(
            p4rm.decode_guild_summary(summary),
            (21, 16, 8, 255, 1000, 44, 7, 6, 5),
        )
        empty = p4rm.encode_guild_status(
            actor, 0, 0, 0, 0, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0
        )
        self.assertEqual(p4rm.decode_guild_status(empty)[1], 0)
        with self.assertRaises(ValueError):
            p4rm.encode_guild_summary(22, 0, 1, 0, 0, 0, 0, 0, 0)
        with self.assertRaises(ValueError):
            p4rm.encode_guild_status(
                actor, 21, 1, 1, 1, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0x10
            )

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
        bridge_flags = p4rm.HELLO_HAS_LOCAL | p4rm.HELLO_LOCAL_DIRTY
        bridge = p4rm.encode_hello(actor, 0, 0, 7, bridge_flags)
        self.assertEqual(
            p4rm.decode_hello(bridge),
            (actor, 0, 0, 7, bridge_flags),
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
            actor, 4, 1234, 300, p4rm.WELCOME_HAS_SNAPSHOT, 17
        )
        self.assertEqual(
            p4rm.decode_welcome(welcome),
            (actor, 4, 1234, 300, p4rm.WELCOME_HAS_SNAPSHOT, 17),
        )
        adoption = p4rm.encode_welcome(
            actor, 4, 1234, 300, p4rm.WELCOME_ADOPT_LOCAL
        )
        self.assertEqual(
            p4rm.decode_welcome(adoption)[4], p4rm.WELCOME_ADOPT_LOCAL
        )
        with self.assertRaises(ValueError):
            p4rm.encode_welcome(
                actor,
                4,
                1234,
                300,
                p4rm.WELCOME_ADOPT_LOCAL | p4rm.WELCOME_HAS_SNAPSHOT,
            )
        with self.assertRaises(ValueError):
            p4rm.encode_welcome(
                actor, 4, 1234, 300, p4rm.WELCOME_ROLLOVER_PENDING
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
        largest = p4rm.encode_action_begin(
            p4rm.ACTION_MAIL,
            0,
            0,
            actor,
            p4rm.MAX_ACTION_NONCE,
            b"",
        )
        self.assertEqual(
            p4rm.decode_action_begin(largest)[4], p4rm.MAX_ACTION_NONCE
        )
        with self.assertRaises(ValueError):
            p4rm.encode_action_begin(
                p4rm.ACTION_MAIL,
                0,
                0,
                actor,
                p4rm.MAX_ACTION_NONCE + 1,
                b"",
            )
        unsigned_max = struct.pack(
            "<BBH16sQHHI",
            p4rm.ACTION_MAIL,
            0,
            0,
            actor,
            0xFFFFFFFFFFFFFFFF,
            0,
            0,
            0,
        )
        with self.assertRaises(ValueError):
            p4rm.decode_action_begin(unsigned_max)
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


class CommandLineTests(unittest.TestCase):
    @staticmethod
    def cli_module():
        script = Path(__file__).resolve().parents[3] / "scripts/p4-realm-hub.py"
        spec = importlib.util.spec_from_file_location("p4_realm_hub_cli", script)
        if spec is None or spec.loader is None:
            raise AssertionError("could not load realm hub CLI")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_duplicate_bindings_ports_and_adoptions_are_rejected(self) -> None:
        cli = self.cli_module()
        with self.assertRaises(ValueError):
            cli.validate_bindings(
                [("pink", "/dev/cu.A"), ("pink", "/dev/cu.B")], None, []
            )
        with self.assertRaises(ValueError):
            cli.validate_bindings(
                [("pink", "/dev/cu.A"), ("green", "/dev/cu.A")], None, []
            )
        with self.assertRaises(ValueError):
            cli.validate_bindings([("pink", "/dev/cu.A")], "pink", [])
        with self.assertRaises(ValueError):
            cli.validate_bindings(
                [("pink", "/dev/cu.A")], None, ["green"]
            )
        with self.assertRaises(ValueError):
            cli.validate_bindings(
                [("pink", "/dev/cu.A")], None, ["pink", "pink"]
            )
        self.assertEqual(
            cli.validate_bindings(
                [("pink", "/dev/cu.A")], "green", ["pink"]
            ),
            ["pink", "green"],
        )

    def test_legacy_migration_requires_an_explicit_cli_flag(self) -> None:
        cli = self.cli_module()
        ordinary = cli.parser().parse_args(["--usb", "pink=/dev/cu.TEST"])
        self.assertFalse(ordinary.migrate_legacy_artifacts)
        migration = cli.parser().parse_args(
            [
                "--usb",
                "pink=/dev/cu.TEST",
                "--migrate-legacy-artifacts",
            ]
        )
        self.assertTrue(migration.migrate_legacy_artifacts)


class StoreTests(unittest.TestCase):
    @staticmethod
    def add_profile(
        store: RealmStore, profile: str, name: str, coin: int, bank: int = 500
    ) -> bytes:
        actor = store.actor_for_profile(profile)
        day, _ = store.realm_clock()
        result = store.commit(
            actor,
            0,
            1,
            day,
            lord_record(actor, 1, name=name, chompcoin=coin, bank=bank),
        )
        if result != ("ok", 1):
            raise AssertionError(f"could not seed authoritative profile: {result}")
        return actor

    def drain_events(self, store: RealmStore, actor: bytes) -> list:
        cursors = getattr(self, "_event_cursors", {})
        key = (store.path, actor)
        cursor = cursors.get(key, 0)
        events = []
        while (
            event := store.next_event(actor, after_event_id=cursor)
        ) is not None:
            events.append(event)
            store.acknowledge_event(actor, event.event_id)
            cursor = event.event_id
        cursors[key] = cursor
        self._event_cursors = cursors
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
            snapshot = lord_record(actor, 7)
            status, revision = store.commit(actor, 0, 7, day, snapshot)
            self.assertEqual((status, revision), ("ok", 1))
            self.assertEqual(
                store.commit(actor, 0, 7, day, snapshot), ("ok", 1)
            )
            with sqlite3.connect(store.path) as database:
                stored_hash = database.execute(
                    "SELECT body_sha256 FROM operations "
                    "WHERE actor_id = ? AND nonce = 7",
                    (actor,),
                ).fetchone()[0]
            self.assertEqual(bytes(stored_hash), hashlib.sha256(snapshot).digest())

            # A legacy NULL hash may be backfilled only while the operation's
            # exact body is still the current head.
            with sqlite3.connect(store.path) as database:
                database.execute(
                    "UPDATE operations SET body_sha256 = NULL "
                    "WHERE actor_id = ? AND nonce = 7",
                    (actor,),
                )
            self.assertEqual(
                store.commit(actor, 0, 7, day, snapshot), ("ok", 1)
            )
            changed = lord_record(actor, 7, chompcoin=501)
            self.assertEqual(
                store.commit(actor, 0, 7, day, changed)[0], "invalid"
            )
            # Model a CRC32 collision by making the legacy CRC column match a
            # different valid body.  SHA-256 still binds nonce to exact bytes.
            with sqlite3.connect(store.path) as database:
                database.execute(
                    "UPDATE operations SET body_crc32 = ? "
                    "WHERE actor_id = ? AND nonce = 7",
                    (zlib.crc32(changed) & 0xFFFFFFFF, actor),
                )
            self.assertEqual(
                store.commit(actor, 0, 7, day, changed)[0], "invalid"
            )
            with sqlite3.connect(store.path) as database:
                database.execute(
                    "UPDATE operations SET body_crc32 = ? "
                    "WHERE actor_id = ? AND nonce = 7",
                    (zlib.crc32(snapshot) & 0xFFFFFFFF, actor),
                )
            other_operation = lord_record(actor, 8, save_sequence=3)
            self.assertEqual(
                store.commit(actor, 0, 8, day, other_operation)[0], "conflict"
            )
            self.assertEqual(store.read_head(actor).snapshot, snapshot)
            other = store.actor_for_profile("console-two")
            profiles = store.list_profiles(exclude_actor_id=other)
            self.assertEqual(profiles[0].name, "Test Hero")
            self.assertEqual(profiles[0].chompcoin, 500)
            self.assertEqual(profiles[0].bank, 500)

            advanced = lord_record(
                actor,
                9,
                save_sequence=3,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(actor, 1, 9, day, advanced), ("ok", 2)
            )
            with sqlite3.connect(store.path) as database:
                database.execute(
                    "UPDATE operations SET body_sha256 = NULL "
                    "WHERE actor_id = ? AND nonce = 7",
                    (actor,),
                )
            self.assertEqual(
                store.commit(actor, 0, 7, day, snapshot)[0], "invalid"
            )

    def test_startup_rebuilds_profiles_from_heads_and_pending_economy(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "realm.sqlite3"
            store = RealmStore(path)
            source = self.add_profile(store, "source", "Source Hero", 500)
            target = self.add_profile(store, "target", "Target Hero", 500)
            self.assertEqual(
                store.perform_action(
                    source,
                    2,
                    p4rm.ACTION_TRANSFER,
                    0,
                    100,
                    target,
                    b"",
                ).status,
                p4rm.ACTION_OK,
            )
            orphan = store.actor_for_profile("legacy-orphan")
            with sqlite3.connect(path) as database:
                database.execute(
                    "UPDATE profiles SET name = 'Inflated Hero', "
                    "flags = 3, chompcoin = 900000000, bank = 900000000, "
                    "last_seen = 9999999999 WHERE actor_id = ?",
                    (source,),
                )
                database.execute(
                    "UPDATE profiles SET chompcoin = 900000000, "
                    "bank = 900000000 WHERE actor_id = ?",
                    (target,),
                )
                database.execute(
                    "INSERT INTO profiles(actor_id, name, hero_style, "
                    "hero_class, level, flags, hit_points, max_hit_points, "
                    "strength, defense, experience, chompcoin, pvp_wins, "
                    "pvp_losses, bank, last_seen) VALUES(?, 'Orphan Hero', "
                    "0, 1, 3, 3, 20, 30, 12, 4, 500, 999999999, 2, 1, "
                    "999999999, 9999999999)",
                    (orphan,),
                )

            reopened = RealmStore(path)
            source_profile = reopened.list_profiles(
                exclude_actor_id=target
            )[0]
            target_profile = reopened.list_profiles(
                exclude_actor_id=source
            )[0]
            self.assertEqual(source_profile.name, "Source Hero")
            self.assertEqual(
                (source_profile.chompcoin, source_profile.bank), (500, 400)
            )
            self.assertEqual(
                (target_profile.chompcoin, target_profile.bank), (600, 500)
            )
            self.assertFalse(source_profile.online)
            self.assertEqual(source_profile.flags, 1)
            self.assertEqual(reopened.profile_count(exclude_actor_id=orphan), 2)
            self.assertEqual(
                reopened.perform_action(
                    orphan,
                    3,
                    p4rm.ACTION_MAIL,
                    0,
                    0,
                    target,
                    b"NOT AUTHORIZED",
                ).status,
                p4rm.ACTION_NOT_FOUND,
            )

            # The migration is deterministic and preserves the same pending
            # balances on every subsequent open.
            reopened_again = RealmStore(path)
            self.assertEqual(
                (
                    reopened_again.list_profiles(
                        exclude_actor_id=target
                    )[0].chompcoin,
                    reopened_again.list_profiles(
                        exclude_actor_id=target
                    )[0].bank,
                ),
                (500, 400),
            )

    def test_startup_rejects_a_malformed_authoritative_head(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "realm.sqlite3"
            store = RealmStore(path)
            actor = self.add_profile(store, "source", "Source Hero", 500)
            with sqlite3.connect(path) as database:
                row = database.execute(
                    "SELECT snapshot FROM heads WHERE actor_id = ?", (actor,)
                ).fetchone()
                damaged = bytearray(row[0])
                damaged[-1] ^= 0x01
                database.execute(
                    "UPDATE heads SET snapshot = ? WHERE actor_id = ?",
                    (bytes(damaged), actor),
                )
            with self.assertRaisesRegex(
                RuntimeError, "malformed authoritative LORD head"
            ):
                RealmStore(path)

    def test_hour_boundary_is_retryable_and_rollover_is_exactly_once(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3", epoch_seconds=0)
            actor = store.actor_for_profile("hourly-console")
            base = lord_record(actor, 50)
            self.assertEqual(
                store.commit(actor, 0, 50, 1, base, now=0), ("ok", 1)
            )

            # A lost result remains idempotently successful even after the hour.
            self.assertEqual(
                store.commit(actor, 0, 50, 1, base, now=3600), ("ok", 1)
            )
            candidate_binding = {
                "save_sequence": 3,
                "sync_actor_id": actor,
                "sync_server_revision": 1,
                "sync_committed_save_sequence": 2,
            }
            rolled = lord_record(
                actor, 51, player_day=2, **candidate_binding
            )
            self.assertEqual(
                store.commit(actor, 1, 51, 1, rolled, now=3600),
                ("stale-day", 1),
            )
            skipped = lord_record(
                actor, 52, player_day=1, **candidate_binding
            )
            self.assertEqual(
                store.commit(actor, 1, 52, 2, skipped, now=3600),
                ("invalid", 1),
            )
            self.assertEqual(
                store.commit(actor, 1, 51, 2, rolled, now=3600),
                ("ok", 2),
            )
            doubled = lord_record(
                actor,
                53,
                save_sequence=4,
                player_day=3,
                sync_actor_id=actor,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            self.assertEqual(
                store.commit(actor, 2, 53, 2, doubled, now=3600),
                ("invalid", 2),
            )

    def test_legacy_empty_realm_head_can_commit_corrected_snapshot(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("legacy-pink")
            day, _ = store.realm_clock()
            legacy_head = lord_record(
                actor,
                30,
                version=4,
                legacy_empty_slots=tuple(range(8)),
            )
            self.assertEqual(
                store.commit(actor, 0, 30, day, legacy_head), ("ok", 1)
            )
            corrected = lord_record(
                actor,
                31,
                save_sequence=3,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(actor, 1, 31, day, corrected), ("ok", 2)
            )

    def test_legacy_event_ack_upgrade_uses_snapshot_commit_proof(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "realm.sqlite3"
            store = RealmStore(path)
            source = store.actor_for_profile("legacy-source")
            target = store.actor_for_profile("legacy-target")
            day, _ = store.realm_clock()
            self.assertEqual(
                store.commit(
                    source,
                    0,
                    60,
                    day,
                    lord_record(source, 60, name="Source Hero"),
                ),
                ("ok", 1),
            )
            self.assertEqual(
                store.commit(
                    target,
                    0,
                    61,
                    day,
                    lord_record(target, 61, name="Target Hero"),
                ),
                ("ok", 1),
            )
            first_result = store.perform_action(
                source,
                62,
                p4rm.ACTION_MAIL,
                0,
                0,
                target,
                b"FIRST",
            )
            self.assertEqual(first_result.status, p4rm.ACTION_OK)
            first = store.next_event(target)
            self.assertIsNotNone(first)
            committed = lord_record(
                target,
                63,
                save_sequence=3,
                name="Target Hero",
                last_realm_event_id=first.event_id,
                sync_actor_id=target,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(target, 1, 63, day, committed), ("ok", 2)
            )
            second_result = store.perform_action(
                source,
                64,
                p4rm.ACTION_MAIL,
                0,
                0,
                target,
                b"SECOND",
            )
            self.assertEqual(second_result.status, p4rm.ACTION_OK)
            second = store.next_event(target, after_event_id=first.event_id)
            self.assertIsNotNone(second)

            # Recreate the exact older one-phase schema without deleting its
            # acknowledgement history.
            with sqlite3.connect(path) as database:
                database.execute(
                    "UPDATE realm_events SET acknowledged_at = CASE event_id "
                    "WHEN ? THEN 111 WHEN ? THEN 222 END",
                    (first.event_id, second.event_id),
                )
                database.execute("DROP INDEX realm_events_uncommitted")
                database.execute("ALTER TABLE realm_events DROP COLUMN committed_at")
                database.execute("ALTER TABLE realm_events DROP COLUMN receipt_at")

            migrated = RealmStore(path, migrate_legacy_artifacts=True)
            with sqlite3.connect(path) as database:
                rows = database.execute(
                    "SELECT event_id, receipt_at, acknowledged_at, committed_at "
                    "FROM realm_events ORDER BY event_id"
                ).fetchall()
            self.assertEqual(rows[0], (first.event_id, 111, 111, 111))
            self.assertEqual(rows[1], (second.event_id, 222, 222, None))
            replay = migrated.next_event(target)
            self.assertIsNotNone(replay)
            self.assertEqual(replay.event_id, second.event_id)

            # Also repair the briefly shipped intermediate schema where the
            # additive column existed but a proven legacy row was left NULL.
            with sqlite3.connect(path) as database:
                database.execute(
                    "UPDATE realm_events SET committed_at = NULL "
                    "WHERE event_id = ?",
                    (first.event_id,),
                )
            RealmStore(path, migrate_legacy_artifacts=True)
            RealmStore(path)
            with sqlite3.connect(path) as database:
                repaired_rows = database.execute(
                    "SELECT event_id, receipt_at, acknowledged_at, committed_at "
                    "FROM realm_events ORDER BY event_id"
                ).fetchall()
            self.assertEqual(
                repaired_rows[0], (first.event_id, 111, 111, 111)
            )
            self.assertEqual(
                repaired_rows[1], (second.event_id, 222, 222, None)
            )

    def test_legacy_orphan_artifacts_stay_quarantined_after_first_head(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "realm.sqlite3"
            store = RealmStore(path)
            source = self.add_profile(store, "source", "Source Hero", 500)
            orphan = store.actor_for_profile("legacy-orphan")
            low, high = sorted((source, orphan))
            with sqlite3.connect(path) as database:
                database.execute(
                    "INSERT INTO realm_events(target_actor_id, source_actor_id, "
                    "kind, code, value, body, created_at, admitted) "
                    "VALUES(?, ?, ?, 0, 0, 'OLD', 10, 1)",
                    (orphan, source, p4rm.ACTION_MAIL),
                )
                event_id = int(database.execute(
                    "SELECT last_insert_rowid()"
                ).fetchone()[0])
                database.execute(
                    "INSERT INTO friendships(source_actor_id, target_actor_id, "
                    "trust, updated_at, admitted) VALUES(?, ?, 100, 10, 1)",
                    (source, orphan),
                )
                database.execute(
                    "INSERT INTO team_invites(source_actor_id, target_actor_id, "
                    "created_at, admitted) VALUES(?, ?, 10, 1)",
                    (source, orphan),
                )
                database.execute(
                    "INSERT INTO teams(actor_low, actor_high, active, updated_at, "
                    "admitted) VALUES(?, ?, 1, 10, 1)",
                    (low, high),
                )
                database.execute(
                    "INSERT INTO pvp_leases(source_actor_id, target_actor_id, "
                    "realm_day_id, status, created_at, admitted) "
                    "VALUES(?, ?, 1, 0, 10, 1)",
                    (source, orphan),
                )
                database.execute(
                    "INSERT INTO action_operations(source_actor_id, nonce, "
                    "request_sha256, status, result_code, result_value, "
                    "related_id, created_at, admitted) "
                    "VALUES(?, 77, ?, 0, 0, 0, 0, 10, 1)",
                    (orphan, bytes(32)),
                )
                for table in (
                    "realm_events",
                    "friendships",
                    "team_invites",
                    "teams",
                    "pvp_leases",
                    "action_operations",
                ):
                    database.execute(
                        f"ALTER TABLE {table} DROP COLUMN admitted"
                    )

            with self.assertRaisesRegex(RuntimeError, "explicit authorized"):
                RealmStore(path)
            migrated = RealmStore(path, migrate_legacy_artifacts=True)
            with sqlite3.connect(path) as database:
                self.assertEqual(
                    [
                        int(database.execute(
                            f"SELECT admitted FROM {table}"
                        ).fetchone()[0])
                        for table in (
                            "realm_events",
                            "friendships",
                            "team_invites",
                            "teams",
                            "pvp_leases",
                            "action_operations",
                        )
                    ],
                    [0, 0, 0, 0, 0, 0],
                )

            day, _ = migrated.realm_clock()
            self.assertEqual(
                migrated.commit(
                    orphan,
                    0,
                    80,
                    day,
                    lord_record(orphan, 80, name="Orphan Hero"),
                ),
                ("ok", 1),
            )
            self.assertIsNone(migrated.next_event(orphan))
            self.assertFalse(migrated.acknowledge_event(orphan, event_id))
            self.assertEqual(
                migrated.perform_action(
                    orphan,
                    77,
                    p4rm.ACTION_MAIL,
                    0,
                    0,
                    source,
                    b"REPLAY",
                ).status,
                p4rm.ACTION_INVALID,
            )
            profile = migrated.list_profiles(exclude_actor_id=source)[0]
            self.assertEqual(profile.trust, 0)
            self.assertFalse(profile.teamed)

            # The zero admission decisions survive later opens.  A genuinely
            # new authorized action may replace just its relationship row.
            reopened = RealmStore(path)
            self.assertEqual(
                reopened.perform_action(
                    source,
                    81,
                    p4rm.ACTION_FRIEND,
                    0,
                    0,
                    orphan,
                    b"",
                ).status,
                p4rm.ACTION_OK,
            )
            with sqlite3.connect(path) as database:
                self.assertEqual(
                    database.execute(
                        "SELECT admitted, trust FROM friendships WHERE "
                        "source_actor_id = ? AND target_actor_id = ?",
                        (source, orphan),
                    ).fetchone(),
                    (1, 15),
                )
                self.assertEqual(
                    database.execute(
                        "SELECT admitted FROM realm_events WHERE event_id = ?",
                        (event_id,),
                    ).fetchone()[0],
                    0,
                )

    def test_legacy_economy_backfill_uses_head_cursor_exactly_once(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "realm.sqlite3"
            store = RealmStore(path)
            source = self.add_profile(store, "source", "Source Hero", 500)
            target = self.add_profile(store, "target", "Target Hero", 500)
            self.assertEqual(
                store.perform_action(
                    source,
                    90,
                    p4rm.ACTION_TRANSFER,
                    0,
                    100,
                    target,
                    b"",
                ).status,
                p4rm.ACTION_OK,
            )
            with sqlite3.connect(path) as database:
                source_event_id = int(database.execute(
                    "SELECT event_id FROM realm_events WHERE "
                    "target_actor_id = ? AND kind = ? AND code = 1",
                    (source, p4rm.ACTION_TRANSFER),
                ).fetchone()[0])
                target_event_id = int(database.execute(
                    "SELECT event_id FROM realm_events WHERE "
                    "target_actor_id = ? AND kind = ? AND code = 0",
                    (target, p4rm.ACTION_TRANSFER),
                ).fetchone()[0])
                database.execute("DELETE FROM realm_economy_events")

            day, _ = store.realm_clock()
            reflected = lord_record(
                source,
                91,
                save_sequence=3,
                name="Source Hero",
                bank=400,
                last_realm_event_id=source_event_id,
                sync_actor_id=source,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(source, 1, 91, day, reflected), ("ok", 2)
            )

            with self.assertRaisesRegex(RuntimeError, "explicit authorized"):
                RealmStore(path)
            migrated = RealmStore(path, migrate_legacy_artifacts=True)
            with sqlite3.connect(path) as database:
                rows = database.execute(
                    "SELECT event_id, actor_id, chompcoin_delta, bank_delta, "
                    "applied_revision FROM realm_economy_events "
                    "ORDER BY event_id"
                ).fetchall()
            self.assertEqual(
                rows,
                [
                    (source_event_id, source, 0, -100, 2),
                    (target_event_id, target, 100, 0, None),
                ],
            )
            source_profile = migrated.list_profiles(exclude_actor_id=target)[0]
            target_profile = migrated.list_profiles(exclude_actor_id=source)[0]
            self.assertEqual(
                (source_profile.chompcoin, source_profile.bank), (500, 400)
            )
            self.assertEqual(
                (target_profile.chompcoin, target_profile.bank), (600, 500)
            )

            # A normal open is now a no-op: neither the debit nor credit is
            # applied twice, and the target's unapplied credit stays pending.
            reopened = RealmStore(path)
            self.assertEqual(
                (reopened.list_profiles(exclude_actor_id=target)[0].bank,
                 reopened.list_profiles(exclude_actor_id=source)[0].chompcoin),
                (400, 600),
            )

    def test_event_migration_rejects_v3_heads_and_mismatched_ledgers(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "v3.sqlite3"
            store = RealmStore(path)
            source = self.add_profile(store, "source", "Source Hero", 500)
            target = store.actor_for_profile("v3-target")
            day, _ = store.realm_clock()
            self.assertEqual(
                store.commit(
                    target,
                    0,
                    100,
                    day,
                    lord_record(
                        target, 100, version=3, name="Target Hero"
                    ),
                ),
                ("ok", 1),
            )
            self.assertEqual(
                store.perform_action(
                    source,
                    101,
                    p4rm.ACTION_MAIL,
                    0,
                    0,
                    target,
                    b"HELLO",
                ).status,
                p4rm.ACTION_OK,
            )
            with self.assertRaisesRegex(RuntimeError, "v3 head"):
                RealmStore(path, migrate_legacy_artifacts=True)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ledger.sqlite3"
            store = RealmStore(path)
            source = self.add_profile(store, "source", "Source Hero", 500)
            target = self.add_profile(store, "target", "Target Hero", 500)
            self.assertEqual(
                store.perform_action(
                    source,
                    102,
                    p4rm.ACTION_TRANSFER,
                    0,
                    100,
                    target,
                    b"",
                ).status,
                p4rm.ACTION_OK,
            )
            with sqlite3.connect(path) as database:
                database.execute(
                    "UPDATE realm_economy_events SET chompcoin_delta = 101 "
                    "WHERE actor_id = ?",
                    (target,),
                )
            with self.assertRaisesRegex(RuntimeError, "ledger mismatch"):
                RealmStore(path, migrate_legacy_artifacts=True)

    def test_zero_value_current_pvp_events_survive_ordinary_restart(self) -> None:
        cases = ((0, 500, 1), (1, 0, 2))
        for outcome, target_coin, expected_source_code in cases:
            with self.subTest(outcome=outcome), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "realm.sqlite3"
                store = RealmStore(path)
                source = self.add_profile(
                    store, "source", "Source Hero", 500
                )
                target = self.add_profile(
                    store, "target", "Target Hero", target_coin
                )
                lease = store.perform_action(
                    source,
                    110,
                    p4rm.ACTION_PVP_BEGIN,
                    0,
                    0,
                    target,
                    b"",
                )
                self.assertEqual(lease.status, p4rm.ACTION_OK)
                self.assertEqual(
                    store.perform_action(
                        source,
                        111,
                        p4rm.ACTION_PVP_RESOLVE,
                        outcome,
                        0,
                        target,
                        lease.related_id.to_bytes(8, "little")
                        + bytes((outcome,)),
                    ).status,
                    p4rm.ACTION_OK,
                )
                reopened = RealmStore(path)
                source_event = reopened.next_event(source)
                self.assertIsNotNone(source_event)
                self.assertEqual(
                    (source_event.code, source_event.value),
                    (expected_source_code, 0),
                )
                with sqlite3.connect(path) as database:
                    self.assertEqual(
                        database.execute(
                            "SELECT chompcoin_delta, bank_delta FROM "
                            "realm_economy_events WHERE event_id = ?",
                            (source_event.event_id,),
                        ).fetchone(),
                        (0, 0),
                    )

    def test_pending_pvp_knockout_survives_projection_until_revival(self) -> None:
        def profile_alive(store: RealmStore, actor_id: bytes) -> bool:
            with sqlite3.connect(store.path) as database:
                row = database.execute(
                    "SELECT flags FROM profiles WHERE actor_id = ?",
                    (actor_id,),
                ).fetchone()
            self.assertIsNotNone(row)
            return (int(row[0]) & 0x01) != 0

        for outcome in (1, 0):
            with self.subTest(outcome=outcome), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "realm.sqlite3"
                store = RealmStore(path)
                source = self.add_profile(
                    store, "source", "Source Hero", 0
                )
                target = self.add_profile(
                    store, "target", "Target Hero", 0
                )
                donor = self.add_profile(
                    store, "donor", "Donor Hero", 0
                )
                loser = target if outcome == 1 else source
                loser_name = "Target Hero" if outcome == 1 else "Source Hero"
                nonce = 140 + outcome * 20
                day, _ = store.realm_clock()

                # Give the eventual loser one earlier economy event.  Committing
                # only this event after the duel exercises a valid projection
                # while the later knockout remains pending.
                self.assertEqual(
                    store.perform_action(
                        donor,
                        nonce,
                        p4rm.ACTION_TRANSFER,
                        0,
                        100,
                        loser,
                        b"",
                    ).status,
                    p4rm.ACTION_OK,
                )
                credit = store.next_event(loser)
                self.assertIsNotNone(credit)
                self.assertEqual(
                    (credit.kind, credit.code, credit.value),
                    (p4rm.ACTION_TRANSFER, 0, 100),
                )

                lease = store.perform_action(
                    source,
                    nonce + 1,
                    p4rm.ACTION_PVP_BEGIN,
                    0,
                    0,
                    target,
                    b"",
                )
                self.assertEqual(lease.status, p4rm.ACTION_OK)
                self.assertEqual(
                    store.perform_action(
                        source,
                        nonce + 2,
                        p4rm.ACTION_PVP_RESOLVE,
                        outcome,
                        0,
                        target,
                        lease.related_id.to_bytes(8, "little")
                        + bytes((outcome,)),
                    ).status,
                    p4rm.ACTION_OK,
                )
                loss_event = store.next_event(
                    loser, after_event_id=credit.event_id
                )
                self.assertIsNotNone(loss_event)
                self.assertEqual(
                    (loss_event.kind, loss_event.code),
                    (p4rm.ACTION_PVP_RESOLVE, 1),
                )

                # Resolve marks the actual loser dead immediately.  The same
                # orientation covers a dead target and a dead source.
                self.assertFalse(profile_alive(store, loser))
                self.assertEqual(
                    store.perform_action(
                        source,
                        nonce + 3,
                        p4rm.ACTION_PVP_BEGIN,
                        0,
                        0,
                        target,
                        b"",
                    ).status,
                    p4rm.ACTION_DENIED,
                )

                reopened = RealmStore(path)
                self.assertFalse(profile_alive(reopened, loser))

                pre_loss = lord_record(
                    loser,
                    nonce + 4,
                    save_sequence=3,
                    name=loser_name,
                    chompcoin=100,
                    last_realm_event_id=credit.event_id,
                    sync_actor_id=loser,
                    sync_server_revision=1,
                    sync_committed_save_sequence=2,
                )
                self.assertEqual(
                    reopened.commit(loser, 1, nonce + 4, day, pre_loss),
                    ("ok", 2),
                )
                self.assertFalse(profile_alive(reopened, loser))

                post_loss_coin = 50 if outcome == 1 else 100
                reflected_loss = lord_record(
                    loser,
                    nonce + 5,
                    save_sequence=4,
                    name=loser_name,
                    chompcoin=post_loss_coin,
                    hit_points=0,
                    pvp_losses=2,
                    last_realm_event_id=loss_event.event_id,
                    sync_actor_id=loser,
                    sync_server_revision=2,
                    sync_committed_save_sequence=3,
                )
                self.assertEqual(
                    reopened.commit(loser, 2, nonce + 5, day, reflected_loss),
                    ("ok", 3),
                )
                self.assertFalse(profile_alive(reopened, loser))

                reopened.epoch_seconds -= 3600
                next_day, _ = reopened.realm_clock()
                revived = lord_record(
                    loser,
                    nonce + 6,
                    save_sequence=5,
                    name=loser_name,
                    chompcoin=post_loss_coin,
                    hit_points=30,
                    pvp_losses=2,
                    player_day=2,
                    last_realm_event_id=loss_event.event_id,
                    sync_actor_id=loser,
                    sync_server_revision=3,
                    sync_committed_save_sequence=4,
                )
                self.assertEqual(
                    reopened.commit(loser, 3, nonce + 6, next_day, revived),
                    ("ok", 4),
                )
                self.assertTrue(profile_alive(reopened, loser))
                self.assertEqual(
                    reopened.perform_action(
                        source,
                        nonce + 7,
                        p4rm.ACTION_PVP_BEGIN,
                        0,
                        0,
                        target,
                        b"",
                    ).status,
                    p4rm.ACTION_OK,
                )

    def test_pvp_event_cursor_requires_the_protected_outcome(self) -> None:
        cases = (
            ("source-loss", 0, "source", 1, 800, {},
             {"hit_points": 0, "pvp_losses": 2}),
            ("defender-win", 0, "target", 0, 800, {},
             {"pvp_wins": 3}),
            ("source-win", 1, "source", 2, 1_200, {},
             {"pvp_wins": 3}),
            ("target-loss", 1, "target", 1, 400, {},
             {"hit_points": 0, "pvp_losses": 2}),
        )
        for (
            label,
            outcome,
            recipient_role,
            expected_code,
            reflected_coin,
            malicious_fields,
            honest_fields,
        ) in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory() as directory:
                store = RealmStore(Path(directory) / "realm.sqlite3")
                source = self.add_profile(
                    store, "source", "Source Hero", 800
                )
                target = self.add_profile(
                    store, "target", "Target Hero", 800
                )
                recipient = source if recipient_role == "source" else target
                recipient_name = (
                    "Source Hero" if recipient_role == "source" else "Target Hero"
                )
                day, _ = store.realm_clock()
                lease = store.perform_action(
                    source,
                    201,
                    p4rm.ACTION_PVP_BEGIN,
                    0,
                    0,
                    target,
                    b"",
                )
                self.assertEqual(lease.status, p4rm.ACTION_OK)
                resolution = store.perform_action(
                    source,
                    202,
                    p4rm.ACTION_PVP_RESOLVE,
                    outcome,
                    0,
                    target,
                    lease.related_id.to_bytes(8, "little")
                    + bytes((outcome,)),
                )
                self.assertEqual(resolution.status, p4rm.ACTION_OK)
                event = store.next_event(recipient)
                self.assertIsNotNone(event)
                self.assertEqual(
                    (event.kind, event.code),
                    (p4rm.ACTION_PVP_RESOLVE, expected_code),
                )

                forged = lord_record(
                    recipient,
                    203,
                    save_sequence=3,
                    name=recipient_name,
                    chompcoin=reflected_coin,
                    last_realm_event_id=event.event_id,
                    sync_actor_id=recipient,
                    sync_server_revision=1,
                    sync_committed_save_sequence=2,
                    **malicious_fields,
                )
                self.assertEqual(
                    store.commit(recipient, 1, 203, day, forged),
                    ("invalid", 1),
                )

                honest = lord_record(
                    recipient,
                    204,
                    save_sequence=3,
                    name=recipient_name,
                    chompcoin=reflected_coin,
                    last_realm_event_id=event.event_id,
                    sync_actor_id=recipient,
                    sync_server_revision=1,
                    sync_committed_save_sequence=2,
                    **honest_fields,
                )
                self.assertEqual(
                    store.commit(recipient, 1, 204, day, honest),
                    ("ok", 2),
                )

    def test_pvp_counters_cannot_change_without_a_durable_receipt(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = self.add_profile(store, "player", "Player Hero", 800)
            day, _ = store.realm_clock()
            for nonce, fields in (
                (301, {"pvp_wins": 1}),
                (302, {"pvp_losses": 0}),
                (305, {"pvp_wins": 3}),
                (306, {"pvp_losses": 2}),
            ):
                with self.subTest(fields=fields):
                    candidate = lord_record(
                        actor,
                        nonce,
                        save_sequence=3,
                        name="Player Hero",
                        sync_actor_id=actor,
                        sync_server_revision=1,
                        sync_committed_save_sequence=2,
                        **fields,
                    )
                    self.assertEqual(
                        store.commit(actor, 1, nonce, day, candidate),
                        ("invalid", 1),
                    )

    def test_cached_duel_bundle_requires_a_durable_realm_receipt(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = self.add_profile(store, "player", "Player Hero", 800)
            day, _ = store.realm_clock()
            binding = {
                "save_sequence": 3,
                "name": "Player Hero",
                "sync_actor_id": actor,
                "sync_server_revision": 1,
                "sync_committed_save_sequence": 2,
            }

            # This is the stock local cached-duel shape: prize, classic local
            # XP, and one win arrive in one save without a realm event cursor.
            cached_duel = lord_record(
                actor,
                303,
                chompcoin=1_200,
                experience=5_000,
                pvp_wins=3,
                **binding,
            )
            self.assertEqual(
                store.commit(actor, 1, 303, day, cached_duel),
                ("invalid", 1),
            )
            self.assertEqual(store.read_head(actor).revision, 1)
            profile = store.list_profiles(
                exclude_actor_id=store.actor_for_profile("viewer")
            )[0]
            self.assertEqual(
                (profile.chompcoin, profile.experience, profile.pvp_wins),
                (800, 500, 2),
            )

            # The exact same offline solo economy/progression remains valid
            # when it does not claim an unreceipted shared-player result.
            solo_progress = lord_record(
                actor,
                304,
                chompcoin=1_200,
                experience=5_000,
                pvp_wins=2,
                **binding,
            )
            self.assertEqual(
                store.commit(actor, 1, 304, day, solo_progress),
                ("ok", 2),
            )
            profile = store.list_profiles(
                exclude_actor_id=store.actor_for_profile("viewer")
            )[0]
            self.assertEqual(
                (profile.chompcoin, profile.experience, profile.pvp_wins),
                (1_200, 5_000, 2),
            )

    def test_pvp_loss_can_be_consumed_during_hourly_revival(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            source = self.add_profile(
                store, "source", "Source Hero", 800
            )
            target = self.add_profile(
                store, "target", "Target Hero", 800
            )
            lease = store.perform_action(
                source,
                401,
                p4rm.ACTION_PVP_BEGIN,
                0,
                0,
                target,
                b"",
            )
            self.assertEqual(lease.status, p4rm.ACTION_OK)
            self.assertEqual(
                store.perform_action(
                    source,
                    402,
                    p4rm.ACTION_PVP_RESOLVE,
                    0,
                    0,
                    target,
                    lease.related_id.to_bytes(8, "little") + b"\0",
                ).status,
                p4rm.ACTION_OK,
            )
            loss = store.next_event(source)
            self.assertEqual((loss.kind, loss.code), (p4rm.ACTION_PVP_RESOLVE, 1))

            store.epoch_seconds -= 3_600
            next_day, _ = store.realm_clock()
            revived = lord_record(
                source,
                403,
                save_sequence=3,
                name="Source Hero",
                chompcoin=800,
                hit_points=30,
                player_day=2,
                pvp_losses=2,
                last_realm_event_id=loss.event_id,
                sync_actor_id=source,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(source, 1, 403, next_day, revived),
                ("ok", 2),
            )

        # A duel created after the clock advances is a current-day knockout,
        # even when the loser's old head still needs its hourly rollover.
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            source = self.add_profile(
                store, "new-source", "New Source", 800
            )
            target = self.add_profile(
                store, "new-target", "New Target", 800
            )
            store.epoch_seconds -= 3_600
            next_day, _ = store.realm_clock()
            lease = store.perform_action(
                source,
                404,
                p4rm.ACTION_PVP_BEGIN,
                0,
                0,
                target,
                b"",
            )
            self.assertEqual(lease.status, p4rm.ACTION_OK)
            self.assertEqual(
                store.perform_action(
                    source,
                    405,
                    p4rm.ACTION_PVP_RESOLVE,
                    0,
                    0,
                    target,
                    lease.related_id.to_bytes(8, "little") + b"\0",
                ).status,
                p4rm.ACTION_OK,
            )
            current_day_loss = store.next_event(source)
            forged_revival = lord_record(
                source,
                406,
                save_sequence=3,
                name="New Source",
                chompcoin=800,
                hit_points=30,
                player_day=2,
                pvp_losses=2,
                last_realm_event_id=current_day_loss.event_id,
                sync_actor_id=source,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(source, 1, 406, next_day, forged_revival),
                ("invalid", 1),
            )
            honest_loss = lord_record(
                source,
                407,
                save_sequence=3,
                name="New Source",
                chompcoin=800,
                hit_points=0,
                player_day=2,
                pvp_losses=2,
                last_realm_event_id=current_day_loss.event_id,
                sync_actor_id=source,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(source, 1, 407, next_day, honest_loss),
                ("ok", 2),
            )

            # LORD's stock dead-screen confirmation can restore HP while
            # removing the rest of today's solo fights.  The pending KO is
            # durable and immediate, not a server-enforced day-long latch.
            recovered = lord_record(
                source,
                408,
                save_sequence=4,
                name="New Source",
                chompcoin=800,
                hit_points=30,
                player_day=2,
                pvp_losses=2,
                forest_fights=0,
                last_realm_event_id=current_day_loss.event_id,
                sync_actor_id=source,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            self.assertEqual(
                store.commit(source, 2, 408, next_day, recovered),
                ("ok", 3),
            )
            self.assertEqual(
                store.perform_action(
                    source,
                    409,
                    p4rm.ACTION_PVP_BEGIN,
                    0,
                    0,
                    target,
                    b"",
                ).status,
                p4rm.ACTION_OK,
            )

    def test_pending_legacy_source_pvp_codes_fail_closed(self) -> None:
        cases = ((0, 0, "wire-incompatible"), (1, 1, "wire-incompatible"))
        for outcome, legacy_code, error_text in cases:
            with self.subTest(outcome=outcome), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "realm.sqlite3"
                store = RealmStore(path)
                source = self.add_profile(
                    store, "source", "Source Hero", 500
                )
                target = self.add_profile(
                    store, "target", "Target Hero", 100
                )
                lease = store.perform_action(
                    source,
                    120,
                    p4rm.ACTION_PVP_BEGIN,
                    0,
                    0,
                    target,
                    b"",
                )
                self.assertEqual(lease.status, p4rm.ACTION_OK)
                self.assertEqual(
                    store.perform_action(
                        source,
                        121,
                        p4rm.ACTION_PVP_RESOLVE,
                        outcome,
                        0,
                        target,
                        lease.related_id.to_bytes(8, "little")
                        + bytes((outcome,)),
                    ).status,
                    p4rm.ACTION_OK,
                )
                with sqlite3.connect(path) as database:
                    source_event_id = int(database.execute(
                        "SELECT event_id FROM realm_events WHERE "
                        "target_actor_id = ? AND kind = ?",
                        (source, p4rm.ACTION_PVP_RESOLVE),
                    ).fetchone()[0])
                    database.execute(
                        "UPDATE realm_events SET code = ? WHERE event_id = ?",
                        (legacy_code, source_event_id),
                    )
                    database.execute(
                        "DELETE FROM realm_economy_events WHERE event_id = ?",
                        (source_event_id,),
                    )
                with self.assertRaisesRegex(RuntimeError, error_text):
                    RealmStore(path, migrate_legacy_artifacts=True)

    def test_cursor_proven_legacy_source_win_is_not_credited_twice(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "realm.sqlite3"
            store = RealmStore(path)
            source = self.add_profile(store, "source", "Source Hero", 500)
            target = self.add_profile(store, "target", "Target Hero", 100)
            lease = store.perform_action(
                source,
                130,
                p4rm.ACTION_PVP_BEGIN,
                0,
                0,
                target,
                b"",
            )
            self.assertEqual(lease.status, p4rm.ACTION_OK)
            self.assertEqual(
                store.perform_action(
                    source,
                    131,
                    p4rm.ACTION_PVP_RESOLVE,
                    1,
                    0,
                    target,
                    lease.related_id.to_bytes(8, "little") + b"\x01",
                ).status,
                p4rm.ACTION_OK,
            )
            with sqlite3.connect(path) as database:
                source_event = database.execute(
                    "SELECT event_id, value FROM realm_events WHERE "
                    "target_actor_id = ? AND kind = ?",
                    (source, p4rm.ACTION_PVP_RESOLVE),
                ).fetchone()
                source_event_id, prize = int(source_event[0]), int(source_event[1])
                database.execute(
                    "UPDATE realm_events SET code = 1 WHERE event_id = ?",
                    (source_event_id,),
                )
                database.execute(
                    "DELETE FROM realm_economy_events WHERE event_id = ?",
                    (source_event_id,),
                )
            day, _ = store.realm_clock()
            consumed = lord_record(
                source,
                132,
                save_sequence=3,
                name="Source Hero",
                chompcoin=500 + prize,
                pvp_wins=3,
                last_realm_event_id=source_event_id,
                sync_actor_id=source,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(source, 1, 132, day, consumed), ("ok", 2)
            )
            reopened = RealmStore(path)
            profile = reopened.list_profiles(exclude_actor_id=target)[0]
            self.assertEqual(profile.chompcoin, 500 + prize)
            with sqlite3.connect(path) as database:
                self.assertIsNone(database.execute(
                    "SELECT event_id FROM realm_economy_events "
                    "WHERE event_id = ?",
                    (source_event_id,),
                ).fetchone())

    def test_strict_real_layout_and_conservative_transitions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("console-one")
            for version, expected_size in ((3, 2338), (4, 2494), (5, 2518)):
                record = lord_record(actor, version, version=version)
                decoded = decode_lord_sync(
                    record, expected_actor_id=actor, expected_nonce=version
                )
                self.assertEqual(decoded.save_version, version)
                self.assertEqual(decoded.player.name, "Test Hero")
                self.assertEqual(len(record), expected_size)

            legacy_slots = tuple(range(8))
            legacy = lord_record(
                actor, 5, version=4, legacy_empty_slots=legacy_slots
            )
            decode_lord_sync(legacy, expected_actor_id=actor)
            with self.assertRaises(ValueError):
                decode_lord_sync(
                    lord_record(
                        actor,
                        4,
                        version=4,
                        legacy_empty_slots=legacy_slots,
                        realm_actor_ids=(b"E" * 16,) + (bytes(16),) * 7,
                    ),
                    expected_actor_id=actor,
                )
            with self.assertRaises(ValueError):
                decode_lord_sync(
                    lord_record(
                        actor,
                        3,
                        version=4,
                        partner_code=1,
                        legacy_empty_slots=legacy_slots,
                    ),
                    expected_actor_id=actor,
                )
            with self.assertRaises(ValueError):
                decode_lord_sync(
                    lord_record(
                        actor,
                        2,
                        version=4,
                        legacy_empty_slots=legacy_slots,
                        legacy_empty_chompcoin=1,
                    ),
                    expected_actor_id=actor,
                )

            teammate_actor = b"T" * 16
            actor_slots = (teammate_actor,) + (bytes(16),) * 7
            valid_team = lord_record(
                actor,
                8,
                partner_code=1,
                teamed_index=0,
                realm_actor_ids=actor_slots,
                partner_actor_id=teammate_actor,
            )
            decode_lord_sync(valid_team, expected_actor_id=actor)
            invalid_team = lord_record(
                actor,
                9,
                partner_code=1,
                realm_actor_ids=actor_slots,
                partner_actor_id=teammate_actor,
            )
            duplicate_actors = lord_record(
                actor,
                7,
                realm_actor_ids=(teammate_actor, teammate_actor)
                + (bytes(16),) * 6,
            )
            self_actor = lord_record(
                actor,
                6,
                realm_actor_ids=(actor,) + (bytes(16),) * 7,
            )
            for malformed_binding in (
                invalid_team,
                duplicate_actors,
                self_actor,
            ):
                with self.assertRaises(ValueError):
                    decode_lord_sync(malformed_binding, expected_actor_id=actor)

            day, _ = store.realm_clock()
            base = lord_record(actor, 10)
            self.assertEqual(store.commit(actor, 0, 10, day, base), ("ok", 1))

            fake_save = bytearray(24)
            fake_save[:4] = b"LDSV"
            struct.pack_into("<HHII", fake_save, 4, 3, len(fake_save), 0, 2)
            struct.pack_into("<II", fake_save, 16, 0x12345678, 1)
            struct.pack_into(
                "<I", fake_save, 8, zlib.crc32(fake_save[16:]) & 0xFFFFFFFF
            )
            fake = bytearray(52 + len(fake_save))
            fake[:4] = b"LRSY"
            struct.pack_into(
                "<HHIIIIQ", fake, 4, 1, 52, len(fake), 0, 1, 2, 11
            )
            fake[32:48] = actor
            struct.pack_into("<I", fake, 48, len(fake_save))
            fake[52:] = fake_save
            struct.pack_into(
                "<I", fake, 12, zlib.crc32(fake[16:]) & 0xFFFFFFFF
            )
            self.assertFalse(validate_lord_sync(bytes(fake), actor, 11))
            self.assertFalse(validate_lord_sync(base[:-1], actor, 10))
            self.assertEqual(
                store.commit(actor, 1, 11, day, bytes(fake)), ("invalid", 1)
            )

            binding = {
                "sync_actor_id": actor,
                "sync_server_revision": 1,
                "sync_committed_save_sequence": 2,
            }
            unbound = lord_record(actor, 17, save_sequence=3, chompcoin=501)
            stale = lord_record(
                actor,
                18,
                save_sequence=3,
                chompcoin=501,
                sync_actor_id=actor,
                sync_server_revision=99,
                sync_committed_save_sequence=2,
            )
            forged_committed = lord_record(
                actor,
                19,
                save_sequence=3,
                chompcoin=501,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=1,
            )
            legacy_unbound = lord_record(
                actor, 20, version=4, save_sequence=3, chompcoin=501
            )
            for nonce, candidate in (
                (17, unbound),
                (18, stale),
                (19, forged_committed),
                (20, legacy_unbound),
            ):
                self.assertEqual(
                    store.commit(actor, 1, nonce, day, candidate),
                    ("invalid", 1),
                )
            huge_coin = lord_record(
                actor,
                12,
                save_sequence=3,
                chompcoin=1_000_000_001,
                **binding,
            )
            huge_xp = lord_record(
                actor,
                13,
                save_sequence=3,
                experience=1_000_000_001,
                **binding,
            )
            huge_stats = lord_record(
                actor,
                14,
                save_sequence=3,
                hit_points=100_001,
                max_hit_points=100_001,
                strength=100_001,
                **binding,
            )
            for nonce, candidate in (
                (12, huge_coin),
                (13, huge_xp),
                (14, huge_stats),
            ):
                self.assertEqual(
                    store.commit(actor, 1, nonce, day, candidate),
                    ("invalid", 1),
                )

            extra_day = lord_record(
                actor, 15, save_sequence=3, player_day=2, **binding
            )
            self.assertEqual(
                store.commit(actor, 1, 15, day, extra_day), ("invalid", 1)
            )
            poisoned_event_cursor = lord_record(
                actor,
                21,
                save_sequence=3,
                last_realm_event_id=0xFFFFFFFFFFFFFFFF,
                **binding,
            )
            self.assertEqual(
                store.commit(actor, 1, 21, day, poisoned_event_cursor),
                ("invalid", 1),
            )

            store.epoch_seconds -= 3600
            next_day, _ = store.realm_clock()
            self.assertEqual(next_day, day + 1)
            legal = lord_record(
                actor,
                16,
                save_sequence=3,
                realm_revision=2,
                hit_points=25,
                max_hit_points=35,
                strength=13,
                defense=5,
                chompcoin=1_500,
                experience=1_500,
                forest_fights=14,
                skill_uses=(0, 4, 0),
                player_day=2,
                pvp_wins=2,
                **binding,
            )
            self.assertEqual(
                store.commit(actor, 1, 16, next_day, legal), ("ok", 2)
            )
            projected = store.list_profiles(
                exclude_actor_id=store.actor_for_profile("console-two")
            )[0]
            self.assertEqual(
                (projected.level, projected.chompcoin, projected.experience),
                (3, 1_500, 1_500),
            )

    def test_two_phase_dragon_victory_reset_is_exactly_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("dragon-hero")
            day, _ = store.realm_clock()
            victory_screen = lord_record(
                actor,
                30,
                level=12,
                dragon_kills=1,
                seen_dragon=True,
            )
            self.assertEqual(
                store.commit(actor, 0, 30, day, victory_screen), ("ok", 1)
            )
            binding = {
                "save_sequence": 3,
                "level": 1,
                "hit_points": 25,
                "max_hit_points": 25,
                "strength": 12,
                "defense": 2,
                "bank": 0,
                "experience": 0,
                "dragon_kills": 1,
                "amulet": True,
                "sync_actor_id": actor,
                "sync_server_revision": 1,
                "sync_committed_save_sequence": 2,
            }
            forged_reset = lord_record(actor, 31, chompcoin=501, **binding)
            self.assertEqual(
                store.commit(actor, 1, 31, day, forged_reset), ("invalid", 1)
            )
            canonical_reset = lord_record(actor, 32, **binding)
            self.assertEqual(
                store.commit(actor, 1, 32, day, canonical_reset), ("ok", 2)
            )

    def test_dragon_deed_requires_exact_level_twelve_rebirth(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            day, _ = store.realm_clock()

            forged_actor = store.actor_for_profile("forged-dragon")
            forged_base = lord_record(forged_actor, 33)
            self.assertEqual(
                store.commit(forged_actor, 0, 33, day, forged_base),
                ("ok", 1),
            )
            forged = lord_record(
                forged_actor,
                34,
                save_sequence=3,
                dragon_kills=1,
                sync_actor_id=forged_actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(forged_actor, 1, 34, day, forged),
                ("invalid", 1),
            )

            actor = store.actor_for_profile("direct-dragon")
            dragon_fight = lord_record(
                actor, 35, level=12, seen_dragon=True
            )
            self.assertEqual(
                store.commit(actor, 0, 35, day, dragon_fight), ("ok", 1)
            )
            canonical = lord_record(
                actor,
                36,
                save_sequence=3,
                level=1,
                hit_points=25,
                max_hit_points=25,
                strength=12,
                defense=2,
                bank=0,
                experience=0,
                dragon_kills=1,
                amulet=True,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(actor, 1, 36, day, canonical), ("ok", 2)
            )
            repeated = lord_record(
                actor,
                37,
                save_sequence=4,
                level=1,
                hit_points=30,
                max_hit_points=30,
                strength=14,
                defense=3,
                bank=0,
                experience=0,
                dragon_kills=2,
                amulet=True,
                sync_actor_id=actor,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            self.assertEqual(
                store.commit(actor, 2, 37, day, repeated), ("invalid", 2)
            )

            offline_actor = store.actor_for_profile("offline-dragon")
            offline_level_twelve = lord_record(
                offline_actor, 38, level=12, seen_dragon=False
            )
            self.assertEqual(
                store.commit(
                    offline_actor, 0, 38, day, offline_level_twelve
                ),
                ("ok", 1),
            )
            offline_rebirth_with_friend = lord_record(
                offline_actor,
                39,
                save_sequence=3,
                npc_friend_code=1,
                level=1,
                hit_points=30,
                max_hit_points=30,
                strength=12,
                defense=2,
                bank=0,
                experience=0,
                dragon_kills=1,
                amulet=True,
                sync_actor_id=offline_actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(
                    offline_actor, 1, 39, day, offline_rebirth_with_friend
                ),
                ("ok", 2),
            )

    def test_friendship_guard_and_rebirth_bonus_are_projected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            guarded_actor = store.actor_for_profile("badge-guard")
            day, _ = store.realm_clock()
            guarded = lord_record(
                guarded_actor,
                40,
                defense=4,
                friendship_badges=20,
            )
            self.assertEqual(
                store.commit(guarded_actor, 0, 40, day, guarded), ("ok", 1)
            )
            other = store.actor_for_profile("ranking-viewer")
            projected = store.list_profiles(exclude_actor_id=other)
            badge_profile = next(
                profile for profile in projected if profile.actor_id == guarded_actor
            )
            self.assertEqual(badge_profile.defense, 12)

            friend_actor = store.actor_for_profile("dragon-friends")
            victory = lord_record(
                friend_actor,
                41,
                level=12,
                dragon_kills=1,
                seen_dragon=True,
                amulet=True,
                partner_code=1,
                teamed_index=0,
            )
            self.assertEqual(
                store.commit(friend_actor, 0, 41, day, victory), ("ok", 1)
            )
            reset = lord_record(
                friend_actor,
                42,
                save_sequence=3,
                level=1,
                hit_points=30,
                max_hit_points=30,
                strength=12,
                defense=2,
                bank=0,
                experience=0,
                dragon_kills=1,
                amulet=True,
                partner_code=1,
                teamed_index=0,
                sync_actor_id=friend_actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(friend_actor, 1, 42, day, reset), ("ok", 2)
            )

    def test_realm_directory_is_ranked_by_authoritative_dragon_deeds(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "prestige.sqlite3"
            store = RealmStore(path)
            day, _ = store.realm_clock()
            viewer = store.actor_for_profile("prestige-viewer")
            cases = (
                ("level-champion", "Level Champion", 12, 1_250_000, 8, 1, 0),
                ("two-deeds", "Two Deeds", 1, 0, 0, 0, 2),
                ("one-deed-xp", "One Deed XP", 8, 300_000, 2, 0, 1),
                ("one-deed-pvp", "One Deed PvP", 8, 110_000, 20, 0, 1),
            )
            actors: dict[str, bytes] = {}
            for index, (
                profile,
                name,
                level,
                experience,
                wins,
                losses,
                deeds,
            ) in enumerate(cases, start=70):
                actor = store.actor_for_profile(profile)
                actors[name] = actor
                record = lord_record(
                    actor,
                    index,
                    name=name,
                    level=level,
                    hit_points=20 + deeds * 5,
                    max_hit_points=20 + deeds * 5,
                    strength=10 + deeds * 2,
                    defense=1 + deeds,
                    experience=experience,
                    pvp_wins=wins,
                    pvp_losses=losses,
                    dragon_kills=deeds,
                    amulet=deeds != 0,
                )
                self.assertEqual(
                    store.commit(actor, 0, index, day, record), ("ok", 1)
                )

            ranked = store.list_profiles(exclude_actor_id=viewer)
            self.assertEqual(
                [profile.name for profile in ranked],
                ["Two Deeds", "One Deed XP", "One Deed PvP", "Level Champion"],
            )
            self.assertEqual(
                [profile.dragon_kills for profile in ranked], [2, 1, 1, 0]
            )

            # Startup rebuilds the directory projection from accepted heads,
            # so a stale or hand-edited cache cannot forge prestige.
            with sqlite3.connect(path) as database:
                database.execute(
                    "UPDATE profiles SET dragon_kills = 255 WHERE actor_id = ?",
                    (actors["Level Champion"],),
                )
            reopened = RealmStore(path)
            rebuilt = reopened.list_profiles(exclude_actor_id=viewer)
            self.assertEqual(
                [profile.name for profile in rebuilt],
                [profile.name for profile in ranked],
            )
            self.assertEqual(
                [profile.dragon_kills for profile in rebuilt], [2, 1, 1, 0]
            )

            sent: list[bytes] = []
            hub = RealmHubSession(
                "prestige-viewer", reopened, sent.append, test_offer()
            )
            hub._queue_directory()
            self.assertEqual(hub.directory_pending[0][0], p4rm.DIRECTORY_PAGE)
            sidecars = [
                packet
                for packet in hub.directory_pending
                if packet[0] == p4rm.DIRECTORY_DEEDS
            ]
            self.assertEqual(len(sidecars), 4)
            self.assertEqual(
                [packet[3][16] for packet in sidecars], [2, 1, 1, 0]
            )
            self.assertTrue(
                all(
                    len(packet[3]) == 18
                    and packet[3][17] == 0
                    and packet[1] < packet[2] == 4
                    for packet in sidecars
                )
            )

    def test_gameplay_progression_bounds_match_lord_1_7(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            day, _ = store.realm_clock()

            actor = store.actor_for_profile("level-bounds")
            base = lord_record(actor, 50)
            self.assertEqual(store.commit(actor, 0, 50, day, base), ("ok", 1))
            binding = {
                "save_sequence": 3,
                "sync_actor_id": actor,
                "sync_server_revision": 1,
                "sync_committed_save_sequence": 2,
            }
            too_soon = lord_record(
                actor, 51, level=4, experience=649, **binding
            )
            self.assertEqual(
                store.commit(actor, 1, 51, day, too_soon), ("invalid", 1)
            )
            legal = lord_record(actor, 52, level=4, experience=650, **binding)
            self.assertEqual(
                store.commit(actor, 1, 52, day, legal), ("ok", 2)
            )

            xp_actor = store.actor_for_profile("xp-bounds")
            xp_base = lord_record(xp_actor, 53)
            self.assertEqual(
                store.commit(xp_actor, 0, 53, day, xp_base), ("ok", 1)
            )
            honest_high_roll = lord_record(
                xp_actor,
                54,
                save_sequence=3,
                experience=2_031_494,
                sync_actor_id=xp_actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(xp_actor, 1, 54, day, honest_high_roll),
                ("ok", 2),
            )

            excessive_actor = store.actor_for_profile("excessive-xp")
            excessive_base = lord_record(excessive_actor, 55)
            self.assertEqual(
                store.commit(excessive_actor, 0, 55, day, excessive_base),
                ("ok", 1),
            )
            excessive_xp = lord_record(
                excessive_actor,
                56,
                save_sequence=3,
                experience=2_500_501,
                sync_actor_id=excessive_actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(excessive_actor, 1, 56, day, excessive_xp),
                ("invalid", 1),
            )

            badge_actor = store.actor_for_profile("badge-bounds")
            badge_base = lord_record(badge_actor, 57)
            self.assertEqual(
                store.commit(badge_actor, 0, 57, day, badge_base), ("ok", 1)
            )
            badge_binding = {
                "save_sequence": 3,
                "sync_actor_id": badge_actor,
                "sync_server_revision": 1,
                "sync_committed_save_sequence": 2,
            }
            too_many_badges = lord_record(
                badge_actor, 58, friendship_badges=5, **badge_binding
            )
            self.assertEqual(
                store.commit(badge_actor, 1, 58, day, too_many_badges),
                ("invalid", 1),
            )
            legal_badges = lord_record(
                badge_actor, 59, friendship_badges=4, **badge_binding
            )
            self.assertEqual(
                store.commit(badge_actor, 1, 59, day, legal_badges),
                ("ok", 2),
            )

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

    def test_one_time_local_adoption_archives_and_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("pink")
            day, _ = store.realm_clock()
            prior = lord_record(actor, 20)
            self.assertEqual(store.commit(actor, 0, 20, day, prior), ("ok", 1))

            grant_id = store.authorize_local_adoption("pink")
            self.assertEqual(store.authorize_local_adoption("pink"), grant_id)
            adopted = lord_record(
                actor,
                21,
                save_sequence=10,
                name="Legacy Hero",
                chompcoin=5_000,
                bank=10_000,
                experience=20_000,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=0,
            )
            with self.assertRaises(ValueError):
                decode_lord_sync(adopted, expected_actor_id=actor)
            decode_lord_sync(
                adopted,
                expected_actor_id=actor,
                allow_adoption_bridge=True,
            )
            self.assertEqual(
                store.commit(
                    actor,
                    1,
                    21,
                    day,
                    adopted,
                    adoption_grant_id=grant_id,
                ),
                ("ok", 2),
            )
            with sqlite3.connect(store.path) as database:
                grant = database.execute(
                    "SELECT consumed_at, consumed_revision FROM adoption_grants "
                    "WHERE grant_id = ?",
                    (grant_id,),
                ).fetchone()
                archive = database.execute(
                    "SELECT revision, snapshot, snapshot_sha256, reason "
                    "FROM archived_heads WHERE actor_id = ?",
                    (actor,),
                ).fetchone()
            self.assertIsNotNone(grant[0])
            self.assertEqual(grant[1], 2)
            self.assertEqual(archive[0], 1)
            self.assertEqual(bytes(archive[1]), prior)
            self.assertEqual(bytes(archive[2]), hashlib.sha256(prior).digest())
            self.assertEqual(archive[3], "adopt-local")

            self.assertEqual(
                store.commit(
                    actor,
                    1,
                    21,
                    day,
                    adopted,
                    adoption_grant_id=grant_id,
                ),
                ("ok", 2),
            )
            divergent = lord_record(
                actor,
                22,
                save_sequence=11,
                name="Legacy Hero",
                chompcoin=6_000,
                bank=10_000,
                experience=20_000,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=0,
            )
            self.assertEqual(
                store.commit(
                    actor,
                    2,
                    22,
                    day,
                    divergent,
                    adoption_grant_id=grant_id,
                ),
                ("invalid", 2),
            )
            with self.assertRaises(ValueError):
                store.authorize_local_adoption("pink")

            normal = lord_record(
                actor,
                23,
                save_sequence=11,
                name="Legacy Hero",
                chompcoin=5_001,
                bank=10_000,
                experience=20_001,
                sync_actor_id=actor,
                sync_server_revision=2,
                sync_committed_save_sequence=10,
            )
            self.assertEqual(store.commit(actor, 2, 23, day, normal), ("ok", 3))
            with sqlite3.connect(store.path) as database:
                archive_count = database.execute(
                    "SELECT COUNT(*) FROM archived_heads WHERE actor_id = ?",
                    (actor,),
                ).fetchone()[0]
            self.assertEqual(archive_count, 1)

    def test_progression_budgets_are_cumulative_per_realm_day(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            day, _ = store.realm_clock()
            cases = (
                (
                    "strength",
                    {"strength": 5_012},
                    {"strength": 10_012},
                ),
                (
                    "economy",
                    {"chompcoin": 25_000_500},
                    {"chompcoin": 50_000_500},
                ),
            )
            for index, (label, first_gain, second_gain) in enumerate(cases):
                with self.subTest(label=label):
                    actor = store.actor_for_profile(f"console-{label}")
                    first_nonce = 1_000 + index * 10
                    base = lord_record(actor, first_nonce)
                    self.assertEqual(
                        store.commit(actor, 0, first_nonce, day, base),
                        ("ok", 1),
                    )
                    first = lord_record(
                        actor,
                        first_nonce + 1,
                        save_sequence=3,
                        sync_actor_id=actor,
                        sync_server_revision=1,
                        sync_committed_save_sequence=2,
                        **first_gain,
                    )
                    self.assertEqual(
                        store.commit(actor, 1, first_nonce + 1, day, first),
                        ("ok", 2),
                    )
                    second = lord_record(
                        actor,
                        first_nonce + 2,
                        save_sequence=4,
                        sync_actor_id=actor,
                        sync_server_revision=2,
                        sync_committed_save_sequence=3,
                        **second_gain,
                    )
                    self.assertEqual(
                        store.commit(actor, 2, first_nonce + 2, day, second),
                        ("invalid", 2),
                    )

    def test_daily_counters_cannot_reset_twice_in_one_realm_day(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            day, _ = store.realm_clock()
            cases = (
                (
                    "pvp-fights",
                    {"pvp_fights_remaining": 1},
                    {"pvp_fights_remaining": 3},
                ),
                (
                    "friend-actions",
                    {"friendship_actions_remaining": 1},
                    {"friendship_actions_remaining": 3},
                ),
                (
                    "igm-mask",
                    {"igm_used_mask": 1},
                    {"igm_used_mask": 0},
                ),
            )
            for index, (label, base_values, reset_values) in enumerate(cases):
                with self.subTest(label=label):
                    actor = store.actor_for_profile(label)
                    nonce = 3_000 + index * 10
                    base = lord_record(actor, nonce, **base_values)
                    self.assertEqual(
                        store.commit(actor, 0, nonce, day, base), ("ok", 1)
                    )
                    reset = lord_record(
                        actor,
                        nonce + 1,
                        save_sequence=3,
                        sync_actor_id=actor,
                        sync_server_revision=1,
                        sync_committed_save_sequence=2,
                        **reset_values,
                    )
                    self.assertEqual(
                        store.commit(actor, 1, nonce + 1, day, reset),
                        ("invalid", 1),
                    )

    def test_pending_debit_waits_for_exact_stock_client_reflection(self) -> None:
        for index, label in enumerate(("transfer", "supplies", "pvp-victim")):
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                store = RealmStore(Path(directory) / "realm.sqlite3")
                source = store.actor_for_profile(f"{label}-source")
                target = store.actor_for_profile(f"{label}-target")
                day, _ = store.realm_clock()
                nonce = 3_100 + index * 100
                self.assertEqual(
                    store.commit(source, 0, nonce, day, lord_record(source, nonce)),
                    ("ok", 1),
                )
                self.assertEqual(
                    store.commit(
                        target,
                        0,
                        nonce + 1,
                        day,
                        lord_record(target, nonce + 1, name="Target Hero"),
                    ),
                    ("ok", 1),
                )

                if label == "transfer":
                    result = store.perform_action(
                        source,
                        nonce + 2,
                        p4rm.ACTION_TRANSFER,
                        0,
                        100,
                        target,
                        b"",
                    )
                    debtor = source
                    insufficient_values = {"bank": 99}
                    reflected_values = {"bank": 400}
                elif label == "supplies":
                    result = store.perform_action(
                        source,
                        nonce + 2,
                        p4rm.ACTION_FRIEND,
                        1,
                        0,
                        target,
                        b"",
                    )
                    debtor = source
                    insufficient_values = {"chompcoin": 99}
                    reflected_values = {
                        "chompcoin": 400,
                        "charm": 11,
                        "friendship_actions_remaining": 2,
                    }
                else:
                    lease = store.perform_action(
                        source,
                        nonce + 2,
                        p4rm.ACTION_PVP_BEGIN,
                        0,
                        0,
                        target,
                        b"",
                    )
                    self.assertEqual(lease.status, p4rm.ACTION_OK)
                    result = store.perform_action(
                        source,
                        nonce + 3,
                        p4rm.ACTION_PVP_RESOLVE,
                        1,
                        0,
                        target,
                        lease.related_id.to_bytes(8, "little") + b"\x01",
                    )
                    debtor = target
                    insufficient_values = {
                        "name": "Target Hero",
                        "chompcoin": 249,
                    }
                    reflected_values = {
                        "name": "Target Hero",
                        "chompcoin": 250,
                        "hit_points": 0,
                        "pvp_losses": 2,
                    }
                self.assertEqual(result.status, p4rm.ACTION_OK)

                pending = store.next_event(debtor)
                self.assertIsNotNone(pending)
                self.assertEqual(store.next_event(debtor).event_id, pending.event_id)

                # This is the unplug/offline-spend shape produced before the
                # stock client can pay the pending event.  Without an advanced
                # cursor it cannot become the authoritative head.
                rejected = lord_record(
                    debtor,
                    nonce + 10,
                    save_sequence=3,
                    sync_actor_id=debtor,
                    sync_server_revision=1,
                    sync_committed_save_sequence=2,
                    **insufficient_values,
                )
                self.assertEqual(
                    store.commit(debtor, 1, nonce + 10, day, rejected),
                    ("invalid", 1),
                )
                self.assertEqual(store.read_head(debtor).revision, 1)
                self.assertEqual(store.next_event(debtor).event_id, pending.event_id)

                # A wire ACK is only a receipt and cannot hide or consume the
                # debit.  A later stock-client snapshot applies it exactly once.
                self.assertTrue(store.acknowledge_event(debtor, pending.event_id))
                self.assertEqual(store.next_event(debtor).event_id, pending.event_id)
                reflected = lord_record(
                    debtor,
                    nonce + 11,
                    save_sequence=3,
                    last_realm_event_id=pending.event_id,
                    sync_actor_id=debtor,
                    sync_server_revision=1,
                    sync_committed_save_sequence=2,
                    **reflected_values,
                )
                self.assertEqual(
                    store.commit(debtor, 1, nonce + 11, day, reflected),
                    ("ok", 2),
                )
                self.assertEqual(
                    store.commit(debtor, 1, nonce + 11, day, reflected),
                    ("ok", 2),
                )
                self.assertIsNone(store.next_event(debtor))
                with sqlite3.connect(store.path) as database:
                    economy = database.execute(
                        "SELECT applied_revision FROM realm_economy_events "
                        "WHERE event_id = ?",
                        (pending.event_id,),
                    ).fetchone()
                    receipt, committed = database.execute(
                        "SELECT receipt_at, committed_at FROM realm_events "
                        "WHERE event_id = ?",
                        (pending.event_id,),
                    ).fetchone()
                self.assertEqual(economy, (2,))
                self.assertIsNotNone(receipt)
                self.assertIsNotNone(committed)

    def test_non_economy_cursor_before_later_credit_can_commit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            source = self.add_profile(
                store, "source", "Source Hero", 500
            )
            target = self.add_profile(
                store, "target", "Target Hero", 500
            )
            day, _ = store.realm_clock()
            self.assertEqual(
                store.perform_action(
                    source,
                    3_900,
                    p4rm.ACTION_MAIL,
                    0,
                    0,
                    target,
                    b"FIRST EVENT",
                ).status,
                p4rm.ACTION_OK,
            )
            mail = store.next_event(target)
            self.assertIsNotNone(mail)
            self.assertEqual(mail.kind, p4rm.ACTION_MAIL)
            self.assertEqual(
                store.perform_action(
                    source,
                    3_901,
                    p4rm.ACTION_TRANSFER,
                    0,
                    100,
                    target,
                    b"",
                ).status,
                p4rm.ACTION_OK,
            )
            self.assertTrue(store.acknowledge_event(target, mail.event_id))

            mail_only = lord_record(
                target,
                3_902,
                save_sequence=3,
                name="Target Hero",
                last_realm_event_id=mail.event_id,
                sync_actor_id=target,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(target, 1, 3_902, day, mail_only),
                ("ok", 2),
            )
            with sqlite3.connect(store.path) as database:
                projected_coin = database.execute(
                    "SELECT chompcoin FROM profiles WHERE actor_id = ?",
                    (target,),
                ).fetchone()[0]
            self.assertEqual(projected_coin, 600)

            credit = store.next_event(target)
            self.assertIsNotNone(credit)
            self.assertEqual(
                (credit.kind, credit.code, credit.value),
                (p4rm.ACTION_TRANSFER, 0, 100),
            )
            reflected_credit = lord_record(
                target,
                3_903,
                save_sequence=4,
                name="Target Hero",
                chompcoin=600,
                last_realm_event_id=credit.event_id,
                sync_actor_id=target,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            self.assertEqual(
                store.commit(target, 2, 3_903, day, reflected_credit),
                ("ok", 3),
            )
            self.assertEqual(
                store.commit(target, 2, 3_903, day, reflected_credit),
                ("ok", 3),
            )
            self.assertIsNone(store.next_event(target))
            with sqlite3.connect(store.path) as database:
                applied_revision = database.execute(
                    "SELECT applied_revision FROM realm_economy_events "
                    "WHERE event_id = ?",
                    (credit.event_id,),
                ).fetchone()[0]
            self.assertEqual(applied_revision, 3)

    def test_dirty_offline_account_progress_precedes_pending_debit(self) -> None:
        for label in ("supplies", "transfer"):
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                store = RealmStore(Path(directory) / "realm.sqlite3")
                source = self.add_profile(
                    store, f"{label}-source", "Source Hero", 500
                )
                target = self.add_profile(
                    store, f"{label}-target", "Target Hero", 500
                )
                day, _ = store.realm_clock()
                nonce = 3_950 if label == "supplies" else 3_960
                if label == "supplies":
                    action = store.perform_action(
                        source,
                        nonce,
                        p4rm.ACTION_FRIEND,
                        1,
                        0,
                        target,
                        b"",
                    )
                    dirty_values = {"chompcoin": 600}
                    projected_values = (500, 500)
                    laundered_values = {
                        "chompcoin": 600,
                        "charm": 11,
                        "friendship_actions_remaining": 2,
                    }
                    reflected_values = {
                        "chompcoin": 500,
                        "charm": 11,
                        "friendship_actions_remaining": 2,
                    }
                else:
                    action = store.perform_action(
                        source,
                        nonce,
                        p4rm.ACTION_TRANSFER,
                        0,
                        100,
                        target,
                        b"",
                    )
                    dirty_values = {"chompcoin": 400, "bank": 600}
                    projected_values = (400, 500)
                    laundered_values = {"chompcoin": 400, "bank": 600}
                    reflected_values = {"chompcoin": 400, "bank": 500}
                self.assertEqual(action.status, p4rm.ACTION_OK)

                dirty_first = lord_record(
                    source,
                    nonce + 1,
                    save_sequence=3,
                    name="Source Hero",
                    sync_actor_id=source,
                    sync_server_revision=1,
                    sync_committed_save_sequence=2,
                    **dirty_values,
                )
                self.assertEqual(
                    store.commit(source, 1, nonce + 1, day, dirty_first),
                    ("ok", 2),
                )
                with sqlite3.connect(store.path) as database:
                    projection = database.execute(
                        "SELECT chompcoin, bank FROM profiles "
                        "WHERE actor_id = ?",
                        (source,),
                    ).fetchone()
                self.assertEqual(projection, projected_values)

                debit = store.next_event(source)
                self.assertIsNotNone(debit)
                self.assertEqual(debit.code & 0x01, 1)
                laundered = lord_record(
                    source,
                    nonce + 2,
                    save_sequence=4,
                    name="Source Hero",
                    last_realm_event_id=debit.event_id,
                    sync_actor_id=source,
                    sync_server_revision=2,
                    sync_committed_save_sequence=3,
                    **laundered_values,
                )
                self.assertEqual(
                    store.commit(source, 2, nonce + 2, day, laundered),
                    ("invalid", 2),
                )
                reflected = lord_record(
                    source,
                    nonce + 3,
                    save_sequence=4,
                    name="Source Hero",
                    last_realm_event_id=debit.event_id,
                    sync_actor_id=source,
                    sync_server_revision=2,
                    sync_committed_save_sequence=3,
                    **reflected_values,
                )
                self.assertEqual(
                    store.commit(source, 2, nonce + 3, day, reflected),
                    ("ok", 3),
                )

    def test_partial_economy_commit_preserves_later_reservation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            source = store.actor_for_profile("source")
            target = store.actor_for_profile("target")
            day, _ = store.realm_clock()
            self.assertEqual(
                store.commit(source, 0, 4_000, day, lord_record(source, 4_000)),
                ("ok", 1),
            )
            self.assertEqual(
                store.commit(target, 0, 4_001, day, lord_record(target, 4_001)),
                ("ok", 1),
            )
            for nonce in (4_002, 4_003):
                self.assertEqual(
                    store.perform_action(
                        source,
                        nonce,
                        p4rm.ACTION_TRANSFER,
                        0,
                        100,
                        target,
                        b"",
                    ).status,
                    p4rm.ACTION_OK,
                )
            first_event = store.next_event(source)
            second_event = store.next_event(
                source, after_event_id=first_event.event_id
            )
            first_reflection = lord_record(
                source,
                4_004,
                save_sequence=3,
                bank=400,
                last_realm_event_id=first_event.event_id,
                sync_actor_id=source,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(source, 1, 4_004, day, first_reflection), ("ok", 2)
            )
            # The accepted head contains only the first debit, but the profile
            # retains the second server reservation until its event is saved.
            self.assertEqual(
                store.list_profiles(exclude_actor_id=target)[0].bank, 300
            )
            second_reflection = lord_record(
                source,
                4_005,
                save_sequence=4,
                bank=300,
                last_realm_event_id=second_event.event_id,
                sync_actor_id=source,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            self.assertEqual(
                store.commit(source, 2, 4_005, day, second_reflection), ("ok", 3)
            )
            self.assertEqual(
                store.list_profiles(exclude_actor_id=target)[0].bank, 300
            )

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
                for friend_code in (0, 0, 1):
                    self.assertEqual(
                        store.perform_action(
                            source,
                            nonce,
                            p4rm.ACTION_FRIEND,
                            friend_code,
                            0,
                            target,
                            b"",
                        ).status,
                        p4rm.ACTION_OK,
                    )
                    nonce += 1
                self.assertEqual(
                    store.perform_action(
                        source,
                        nonce,
                        p4rm.ACTION_FRIEND,
                        0,
                        0,
                        target,
                        b"",
                    ).status,
                    p4rm.ACTION_DENIED,
                )
                nonce += 1
            duplicate_supplies = store.perform_action(
                first, 12, p4rm.ACTION_FRIEND, 1, 0, second, b""
            )
            self.assertEqual(duplicate_supplies.status, p4rm.ACTION_OK)
            self.assertEqual(
                store.list_profiles(exclude_actor_id=second)[0].chompcoin, 100
            )
            self.assertEqual(
                store.list_profiles(exclude_actor_id=first)[0].chompcoin, 1_000
            )
            with sqlite3.connect(store.path) as database:
                first_usage = database.execute(
                    "SELECT friendship_actions FROM realm_daily_action_usage "
                    "WHERE actor_id = ?",
                    (first,),
                ).fetchone()[0]
                supply_debits = database.execute(
                    "SELECT COUNT(*) FROM realm_economy_events "
                    "WHERE actor_id = ? AND chompcoin_delta = -100",
                    (first,),
                ).fetchone()[0]
            self.assertEqual((first_usage, supply_debits), (3, 1))
            self.drain_events(store, first)
            self.drain_events(store, second)
            store.epoch_seconds -= 3600
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

            second_prize_target = self.add_profile(
                store, "pvp-target-two", "Prize Hero Two", 500
            )
            third_prize_target = self.add_profile(
                store, "pvp-target-three", "Prize Hero Three", 250
            )
            fourth_target = self.add_profile(
                store, "pvp-target-four", "Prize Hero Four", 100
            )
            for target_actor, action_nonce, expected_prize in (
                (second, 40, 500),
                (second_prize_target, 42, 250),
                (third_prize_target, 44, 125),
            ):
                lease = store.perform_action(
                    first,
                    action_nonce,
                    p4rm.ACTION_PVP_BEGIN,
                    0,
                    0,
                    target_actor,
                    b"",
                )
                self.assertEqual(lease.status, p4rm.ACTION_OK)
                resolution = store.perform_action(
                    first,
                    action_nonce + 1,
                    p4rm.ACTION_PVP_RESOLVE,
                    1,
                    0,
                    target_actor,
                    lease.related_id.to_bytes(8, "little") + b"\x01",
                )
                self.assertEqual(
                    (resolution.status, resolution.value),
                    (p4rm.ACTION_OK, 0),
                )
                duel_event = self.drain_events(store, target_actor)[0]
                self.assertEqual(
                    (duel_event.code, duel_event.value),
                    (1, expected_prize),
                )
            denied_fourth = store.perform_action(
                first, 46, p4rm.ACTION_PVP_BEGIN, 0, 0, fourth_target, b""
            )
            self.assertEqual(denied_fourth.status, p4rm.ACTION_DENIED)
            rewards = [
                event
                for event in self.drain_events(store, first)
                if event.kind == p4rm.ACTION_PVP_RESOLVE and event.code == 2
            ]
            self.assertEqual([event.value for event in rewards], [500, 250, 125])
            with sqlite3.connect(store.path) as database:
                winner_coin = database.execute(
                    "SELECT chompcoin FROM profiles WHERE actor_id = ?",
                    (first,),
                ).fetchone()[0]
            self.assertEqual(winner_coin, 975)

            zero_source = self.add_profile(
                store, "zero-source", "Zero Source", 100
            )
            zero_target = self.add_profile(
                store, "zero-target", "Zero Target", 0
            )
            zero_lease = store.perform_action(
                zero_source, 70, p4rm.ACTION_PVP_BEGIN, 0, 0, zero_target, b""
            )
            self.assertEqual(zero_lease.status, p4rm.ACTION_OK)
            zero_body = zero_lease.related_id.to_bytes(8, "little") + b"\x01"
            zero_win = store.perform_action(
                zero_source,
                71,
                p4rm.ACTION_PVP_RESOLVE,
                1,
                0,
                zero_target,
                zero_body,
            )
            self.assertEqual(zero_win.status, p4rm.ACTION_OK)
            zero_target_event = self.drain_events(store, zero_target)
            zero_source_event = self.drain_events(store, zero_source)
            self.assertEqual(
                [(event.code, event.value) for event in zero_target_event],
                [(1, 0)],
            )
            self.assertEqual(
                [(event.code, event.value) for event in zero_source_event],
                [(2, 0)],
            )
            self.assertEqual(
                store.perform_action(
                    zero_source,
                    71,
                    p4rm.ACTION_PVP_RESOLVE,
                    1,
                    0,
                    zero_target,
                    zero_body,
                ),
                zero_win,
            )
            self.assertEqual(self.drain_events(store, zero_source), [])
            self.assertEqual(self.drain_events(store, zero_target), [])

            loss_target = self.add_profile(
                store, "loss-target", "Loss Target", 0
            )
            loss_lease = store.perform_action(
                zero_source, 72, p4rm.ACTION_PVP_BEGIN, 0, 0, loss_target, b""
            )
            self.assertEqual(loss_lease.status, p4rm.ACTION_OK)
            loss = store.perform_action(
                zero_source,
                73,
                p4rm.ACTION_PVP_RESOLVE,
                0,
                0,
                loss_target,
                loss_lease.related_id.to_bytes(8, "little") + b"\x00",
            )
            self.assertEqual(loss.status, p4rm.ACTION_OK)
            self.assertEqual(
                [(event.code, event.value)
                 for event in self.drain_events(store, zero_source)],
                [(1, 0)],
            )
            self.assertEqual(
                [(event.code, event.value)
                 for event in self.drain_events(store, loss_target)],
                [(0, 0)],
            )

            post = store.perform_action(
                first, 50, p4rm.ACTION_TAVERN, 0, 0, b"\0" * 16,
                b"DRAGON AT MIDNIGHT",
            )
            self.assertEqual(post.status, p4rm.ACTION_OK)
            self.assertEqual(self.drain_events(store, second)[0].body,
                             b"DRAGON AT MIDNIGHT")
            poor = self.add_profile(store, "poor-console", "Poor Hero", 50)
            self.assertEqual(
                store.perform_action(
                    poor, 60, p4rm.ACTION_FRIEND, 1, 0, second, b""
                ).status,
                p4rm.ACTION_DENIED,
            )

    def test_guild_migration_cap_succession_and_rejoin_cooldown(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "realm.sqlite3"
            legacy = RealmStore(path)
            preserved = self.add_profile(
                legacy, "guild-leader", "Guild Leader", 500
            )
            # Model the immediately preceding schema without club tables.
            with sqlite3.connect(path) as database:
                for table in (
                    "guild_cheers",
                    "guild_clashes",
                    "guild_rallies",
                    "guild_rejoin_cooldowns",
                    "guild_members",
                    "guilds",
                ):
                    database.execute(f"DROP TABLE {table}")
            store = RealmStore(path)
            self.assertEqual(
                store.list_profiles(exclude_actor_id=bytes(reversed(preserved)))[0].name,
                "Guild Leader",
            )
            with sqlite3.connect(path) as database:
                tables = {
                    row[0]
                    for row in database.execute(
                        "SELECT name FROM sqlite_master WHERE type = 'table'"
                    )
                }
            self.assertTrue(
                {
                    "guilds",
                    "guild_members",
                    "guild_rejoin_cooldowns",
                    "guild_rallies",
                    "guild_clashes",
                    "guild_cheers",
                }.issubset(tables)
            )

            realm_day = [1]
            store.realm_clock = lambda now=None: (realm_day[0], 3600)
            created = store.perform_action(
                preserved,
                100,
                p4rm.ACTION_GUILD,
                p4rm.GUILD_CREATE,
                1,
                bytes(16),
                b"",
            )
            self.assertEqual(created.status, p4rm.ACTION_OK)
            self.assertEqual(store.guild_status(preserved).role, 1)
            self.assertFalse(
                store.guild_status(preserved).daily_flags
                & p4rm.GUILD_DAILY_ELIGIBLE
            )

            joiners = [
                self.add_profile(store, f"joiner-{index}", f"Joiner {index}", 500)
                for index in range(8)
            ]
            for index, actor in enumerate(joiners[:7]):
                joined = store.perform_action(
                    actor,
                    200 + index,
                    p4rm.ACTION_GUILD,
                    p4rm.GUILD_JOIN,
                    0,
                    preserved,
                    b"",
                )
                self.assertEqual(joined.status, p4rm.ACTION_OK)
            self.assertEqual(store.guild_status(preserved).members, 8)
            self.assertEqual(
                store.perform_action(
                    joiners[7],
                    299,
                    p4rm.ACTION_GUILD,
                    p4rm.GUILD_JOIN,
                    0,
                    preserved,
                    b"",
                ).status,
                p4rm.ACTION_BUSY,
            )
            self.assertEqual(
                store.perform_action(
                    joiners[7],
                    300,
                    p4rm.ACTION_GUILD,
                    p4rm.GUILD_CREATE,
                    1,
                    bytes(16),
                    b"",
                ).status,
                p4rm.ACTION_DENIED,
            )

            left = store.perform_action(
                preserved,
                301,
                p4rm.ACTION_GUILD,
                p4rm.GUILD_LEAVE,
                0,
                bytes(16),
                b"",
            )
            self.assertEqual(left.status, p4rm.ACTION_OK)
            successor = min(joiners[:7])
            self.assertEqual(store.guild_status(successor).role, 1)
            self.assertEqual(
                store.perform_action(
                    preserved,
                    302,
                    p4rm.ACTION_GUILD,
                    p4rm.GUILD_JOIN,
                    0,
                    successor,
                    b"",
                ).status,
                p4rm.ACTION_DENIED,
            )
            realm_day[0] = 2
            self.assertEqual(
                store.perform_action(
                    preserved,
                    303,
                    p4rm.ACTION_GUILD,
                    p4rm.GUILD_JOIN,
                    0,
                    successor,
                    b"",
                ).status,
                p4rm.ACTION_OK,
            )

            solo = self.add_profile(store, "solo", "Solo", 500)
            replacement = self.add_profile(store, "replacement", "Replacement", 500)
            self.assertEqual(
                store.perform_action(
                    solo, 400, p4rm.ACTION_GUILD, p4rm.GUILD_CREATE,
                    2, bytes(16), b"",
                ).status,
                p4rm.ACTION_OK,
            )
            self.assertEqual(
                store.perform_action(
                    solo, 401, p4rm.ACTION_GUILD, p4rm.GUILD_LEAVE,
                    0, bytes(16), b"",
                ).status,
                p4rm.ACTION_OK,
            )
            self.assertEqual(
                store.perform_action(
                    replacement, 402, p4rm.ACTION_GUILD, p4rm.GUILD_CREATE,
                    2, bytes(16), b"",
                ).status,
                p4rm.ACTION_OK,
            )

    def test_guild_quest_carries_and_idempotency_survives_season_rollover(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            realm_day = [1]
            store.realm_clock = lambda now=None: (realm_day[0], 3600)
            leader = self.add_profile(store, "quest-leader", "Quest Leader", 500)
            member = self.add_profile(store, "quest-member", "Quest Member", 500)
            self.assertEqual(
                store.perform_action(
                    leader, 10, p4rm.ACTION_GUILD, p4rm.GUILD_CREATE,
                    3, bytes(16), b"",
                ).status,
                p4rm.ACTION_OK,
            )
            self.assertEqual(
                store.perform_action(
                    member, 11, p4rm.ACTION_GUILD, p4rm.GUILD_JOIN,
                    0, leader, b"",
                ).status,
                p4rm.ACTION_OK,
            )
            for day, nonce in ((2, 20), (3, 21), (4, 22)):
                realm_day[0] = day
                result = store.perform_action(
                    leader, nonce, p4rm.ACTION_GUILD, p4rm.GUILD_RALLY,
                    1, bytes(16), b"",
                )
                self.assertEqual((result.status, result.value), (p4rm.ACTION_OK, 3))
            self.assertEqual(store.guild_status(leader).quest_progress, 9)

            realm_day[0] = 5
            completion = store.perform_action(
                leader, 23, p4rm.ACTION_GUILD, p4rm.GUILD_RALLY,
                1, bytes(16), b"",
            )
            self.assertEqual(completion.value, 13)
            after_completion = store.guild_status(leader)
            self.assertEqual(
                (after_completion.banner_stars, after_completion.quest_progress),
                (1, 0),
            )
            banked = store.perform_action(
                member, 24, p4rm.ACTION_GUILD, p4rm.GUILD_RALLY,
                1, bytes(16), b"",
            )
            self.assertEqual(banked.value, 3)
            self.assertEqual(
                (store.guild_status(leader).banner_stars,
                 store.guild_status(leader).quest_progress),
                (1, 3),
            )

            # The original operation is returned without re-evaluating the
            # current day or resetting the new season.
            realm_day[0] = 25
            replay = store.perform_action(
                leader, 20, p4rm.ACTION_GUILD, p4rm.GUILD_RALLY,
                1, bytes(16), b"",
            )
            self.assertEqual(replay.value, 3)
            with sqlite3.connect(store.path) as database:
                self.assertEqual(
                    database.execute(
                        "SELECT COUNT(*) FROM guild_rallies "
                        "WHERE actor_id = ? AND realm_day_id = 25",
                        (leader,),
                    ).fetchone()[0],
                    0,
                )
            status = store.guild_status(leader)
            self.assertGreater(status.prestige, 0)
            self.assertEqual(status.season_points, 0)
            self.assertEqual(status.quest_progress, 3)

    def test_guild_clashes_are_directional_cooldown_safe_and_no_economy(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            realm_day = [1]
            store.realm_clock = lambda now=None: (realm_day[0], 3600)
            a = self.add_profile(store, "club-a", "Club A", 700)
            a_member = self.add_profile(store, "club-a-member", "A Member", 600)
            b = self.add_profile(store, "club-b", "Club B", 800)
            b_new = self.add_profile(store, "club-b-new", "B New", 900)
            c = self.add_profile(store, "club-c", "Club C", 1000)
            for actor, nonce, name_code in ((a, 1, 4), (b, 2, 5), (c, 3, 6)):
                self.assertEqual(
                    store.perform_action(
                        actor, nonce, p4rm.ACTION_GUILD, p4rm.GUILD_CREATE,
                        name_code, bytes(16), b"",
                    ).status,
                    p4rm.ACTION_OK,
                )
            self.assertEqual(
                store.perform_action(
                    a_member, 4, p4rm.ACTION_GUILD, p4rm.GUILD_JOIN,
                    0, a, b"",
                ).status,
                p4rm.ACTION_OK,
            )
            before = None
            with sqlite3.connect(store.path) as database:
                before = database.execute(
                    "SELECT actor_id, hit_points, max_hit_points, strength, "
                    "defense, experience, chompcoin, bank, pvp_wins, "
                    "pvp_losses, dragon_kills FROM profiles ORDER BY actor_id"
                ).fetchall()

            realm_day[0] = 2
            # This new member is only a pointer to Club B. Its older accepted
            # roster makes the club a valid clash target today.
            self.assertEqual(
                store.perform_action(
                    b_new, 5, p4rm.ACTION_GUILD, p4rm.GUILD_JOIN,
                    0, b, b"",
                ).status,
                p4rm.ACTION_OK,
            )
            for actor, nonce, route in ((a, 10, 0), (b, 11, 1), (c, 12, 2)):
                self.assertEqual(
                    store.perform_action(
                        actor, nonce, p4rm.ACTION_GUILD, p4rm.GUILD_RALLY,
                        route, bytes(16), b"",
                    ).status,
                    p4rm.ACTION_OK,
                )
            first = store.perform_action(
                a, 20, p4rm.ACTION_GUILD, p4rm.GUILD_CLASH, 0, b_new, b""
            )
            self.assertEqual(first.status, p4rm.ACTION_OK)
            self.assertEqual(
                store.perform_action(
                    a, 21, p4rm.ACTION_GUILD, p4rm.GUILD_CLASH, 0, c, b""
                ).status,
                p4rm.ACTION_DENIED,
            )
            self.assertEqual(
                store.perform_action(
                    c, 22, p4rm.ACTION_GUILD, p4rm.GUILD_CLASH, 0, b, b""
                ).status,
                p4rm.ACTION_DENIED,
            )
            # One outgoing plus one incoming for the same club is allowed.
            self.assertEqual(
                store.perform_action(
                    c, 23, p4rm.ACTION_GUILD, p4rm.GUILD_CLASH, 0, a, b""
                ).status,
                p4rm.ACTION_OK,
            )
            self.assertEqual(
                store.perform_action(
                    a, 24, p4rm.ACTION_PVP_BEGIN, 0, 0, a_member, b""
                ).status,
                p4rm.ACTION_DENIED,
            )
            self.assertEqual(
                store.perform_action(
                    b, 25, p4rm.ACTION_GUILD, p4rm.GUILD_CHEER, 0, c, b""
                ).status,
                p4rm.ACTION_OK,
            )

            for day, nonce, expected in (
                (3, 30, p4rm.ACTION_DENIED),
                (4, 31, p4rm.ACTION_DENIED),
                (5, 32, p4rm.ACTION_OK),
            ):
                realm_day[0] = day
                self.assertEqual(
                    store.perform_action(
                        b, nonce, p4rm.ACTION_GUILD, p4rm.GUILD_CLASH,
                        0, a, b"",
                    ).status,
                    expected,
                )
            with sqlite3.connect(store.path) as database:
                after = database.execute(
                    "SELECT actor_id, hit_points, max_hit_points, strength, "
                    "defense, experience, chompcoin, bank, pvp_wins, "
                    "pvp_losses, dragon_kills FROM profiles ORDER BY actor_id"
                ).fetchall()
                self.assertEqual(
                    database.execute(
                        "SELECT COUNT(*) FROM realm_economy_events"
                    ).fetchone()[0],
                    0,
                )
                clash = database.execute(
                    "SELECT source_score, target_score FROM guild_clashes "
                    "WHERE clash_id = ?",
                    (first.related_id,),
                ).fetchone()
            self.assertEqual(after, before)
            self.assertTrue(all(int(score) >= 0 for score in clash))

    def test_pvp_lease_is_canceled_if_actors_become_clubmates(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            source = self.add_profile(store, "lease-source", "Lease Source", 500)
            target = self.add_profile(store, "lease-target", "Lease Target", 500)
            with self.assertRaises(ValueError):
                store.perform_action(
                    source,
                    p4rm.MAX_ACTION_NONCE + 1,
                    p4rm.ACTION_MAIL,
                    0,
                    0,
                    target,
                    b"NO SQLITE OVERFLOW",
                )
            opened = store.perform_action(
                source, 10, p4rm.ACTION_PVP_BEGIN, 0, 0, target, b""
            )
            self.assertEqual(opened.status, p4rm.ACTION_OK)
            self.assertEqual(
                store.perform_action(
                    source, 11, p4rm.ACTION_GUILD, p4rm.GUILD_CREATE,
                    7, bytes(16), b"",
                ).status,
                p4rm.ACTION_OK,
            )
            self.assertEqual(
                store.perform_action(
                    target, 12, p4rm.ACTION_GUILD, p4rm.GUILD_JOIN,
                    0, source, b"",
                ).status,
                p4rm.ACTION_OK,
            )
            with sqlite3.connect(store.path) as database:
                before = database.execute(
                    "SELECT actor_id, flags, chompcoin, bank, pvp_wins, "
                    "pvp_losses FROM profiles ORDER BY actor_id"
                ).fetchall()
            body = opened.related_id.to_bytes(8, "little") + b"\x01"
            blocked = store.perform_action(
                source, 13, p4rm.ACTION_PVP_RESOLVE, 1, 0, target, body
            )
            self.assertEqual(blocked.status, p4rm.ACTION_DENIED)
            self.assertEqual(
                store.perform_action(
                    source, 13, p4rm.ACTION_PVP_RESOLVE, 1, 0, target, body
                ),
                blocked,
            )
            self.assertEqual(
                store.perform_action(
                    target, 14, p4rm.ACTION_GUILD, p4rm.GUILD_LEAVE,
                    0, bytes(16), b"",
                ).status,
                p4rm.ACTION_OK,
            )
            self.assertEqual(
                store.perform_action(
                    source, 15, p4rm.ACTION_PVP_RESOLVE, 1, 0, target, body
                ).status,
                p4rm.ACTION_DENIED,
            )
            expired_open = store.perform_action(
                source, 16, p4rm.ACTION_PVP_BEGIN, 0, 0, target, b""
            )
            self.assertEqual(expired_open.status, p4rm.ACTION_OK)
            with sqlite3.connect(store.path) as database:
                database.execute(
                    "UPDATE pvp_leases SET created_at = created_at - 601 "
                    "WHERE lease_id = ?",
                    (expired_open.related_id,),
                )
            expired_body = (
                expired_open.related_id.to_bytes(8, "little") + b"\x01"
            )
            self.assertEqual(
                store.perform_action(
                    source,
                    17,
                    p4rm.ACTION_PVP_RESOLVE,
                    1,
                    0,
                    target,
                    expired_body,
                ).status,
                p4rm.ACTION_DENIED,
            )
            with sqlite3.connect(store.path) as database:
                lease = database.execute(
                    "SELECT status, outcome, resolved_at FROM pvp_leases "
                    "WHERE lease_id = ?",
                    (opened.related_id,),
                ).fetchone()
                expired_lease = database.execute(
                    "SELECT status, outcome, resolved_at FROM pvp_leases "
                    "WHERE lease_id = ?",
                    (expired_open.related_id,),
                ).fetchone()
                after = database.execute(
                    "SELECT actor_id, flags, chompcoin, bank, pvp_wins, "
                    "pvp_losses FROM profiles ORDER BY actor_id"
                ).fetchall()
                self.assertEqual(
                    database.execute("SELECT COUNT(*) FROM realm_events").fetchone()[0],
                    0,
                )
                self.assertEqual(
                    database.execute(
                        "SELECT COUNT(*) FROM realm_economy_events"
                    ).fetchone()[0],
                    0,
                )
            self.assertEqual((int(lease[0]), lease[1] is None), (1, True))
            self.assertIsNotNone(lease[2])
            self.assertEqual(
                (int(expired_lease[0]), expired_lease[1] is None), (1, True)
            )
            self.assertIsNotNone(expired_lease[2])
            self.assertEqual(after, before)

    def test_stale_guild_reader_cannot_roll_calendar_backward(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            realm_day = [1]
            store.realm_clock = lambda now=None: (realm_day[0], 3600)

            actors = []
            for index in range(4):
                actor = store.actor_for_profile(f"calendar-{index}")
                record = lord_record(
                    actor,
                    index + 1,
                    name=f"Calendar {index}",
                    hero_class=1,
                    level=6,
                    dragon_kills=1,
                )
                self.assertEqual(
                    store.commit(actor, 0, index + 1, 1, record), ("ok", 1)
                )
                actors.append(actor)
            created = store.perform_action(
                actors[0], 100, p4rm.ACTION_GUILD, p4rm.GUILD_CREATE,
                8, bytes(16), b"",
            )
            self.assertEqual(created.status, p4rm.ACTION_OK)
            for index, actor in enumerate(actors[1:], start=1):
                self.assertEqual(
                    store.perform_action(
                        actor, 100 + index, p4rm.ACTION_GUILD,
                        p4rm.GUILD_JOIN, 0, actors[0], b"",
                    ).status,
                    p4rm.ACTION_OK,
                )

            # This is a valid end-of-day-24 state: eleven quest points are
            # banked and the next rally can complete the first day-25 banner.
            with sqlite3.connect(store.path) as database:
                database.execute(
                    "UPDATE guilds SET prestige = 7, season_id = 0, "
                    "season_points = 7, quest_day = 24, quest_progress = 11, "
                    "quest_complete = 0 WHERE guild_id = ?",
                    (created.related_id,),
                )

            realm_day[0] = 25
            awards = []
            for index in range(3):
                awards.append(
                    store.perform_action(
                        actors[index],
                        200 + index,
                        p4rm.ACTION_GUILD,
                        p4rm.GUILD_RALLY,
                        1,
                        bytes(16),
                        b"",
                    ).value
                )
            self.assertEqual(awards, [15, 5, 5])
            current = store.guild_status(actors[0])
            self.assertEqual(
                (current.banner_stars, current.quest_progress,
                 current.season_points),
                (1, 11, 25),
            )

            # A reader that sampled day 24 before blocking on the write lock
            # must not reset season two or reopen the day-25 banner quest.
            realm_day[0] = 24
            stale = store.guild_status(actors[0])
            self.assertEqual(
                (stale.banner_stars, stale.quest_progress, stale.season_points),
                (1, 11, 25),
            )
            with sqlite3.connect(store.path) as database:
                persisted = database.execute(
                    "SELECT season_id, season_points, quest_day, "
                    "quest_progress, quest_complete FROM guilds "
                    "WHERE guild_id = ?",
                    (created.related_id,),
                ).fetchone()
            self.assertEqual(tuple(map(int, persisted)), (1, 25, 25, 11, 1))

            realm_day[0] = 25
            final = store.perform_action(
                actors[3], 203, p4rm.ACTION_GUILD, p4rm.GUILD_RALLY,
                1, bytes(16), b"",
            )
            self.assertEqual(final.value, 5)
            status = store.guild_status(actors[0])
            self.assertEqual(
                (status.banner_stars, status.quest_progress,
                 status.season_points),
                (1, 11, 30),
            )


def _fixed_text(value: str, size: int) -> bytes:
    encoded = value.encode("ascii")
    if len(encoded) >= size:
        raise ValueError("test fixture text is too long")
    return encoded + bytes(size - len(encoded))


def lord_record(
    actor_id: bytes,
    nonce: int,
    *,
    version: int = 5,
    save_sequence: int = 2,
    realm_revision: int = 1,
    partner_code: int = 0,
    npc_friend_code: int = 0,
    pvp_fights_remaining: int = 3,
    friendship_actions_remaining: int = 3,
    igm_used_mask: int = 0,
    name: str = "Test Hero",
    hero_style: int = 0,
    hero_class: int = 1,
    level: int = 3,
    hit_points: int = 20,
    max_hit_points: int = 30,
    strength: int = 12,
    defense: int = 4,
    chompcoin: int = 500,
    bank: int = 500,
    experience: int = 500,
    forest_fights: int = 15,
    skill: tuple[int, int, int] = (0, 5, 0),
    skill_uses: tuple[int, int, int] = (0, 3, 0),
    dragon_kills: int = 0,
    player_day: int = 1,
    pvp_wins: int = 2,
    pvp_losses: int = 1,
    charm: int = 10,
    gems: int = 0,
    young_heroes_helped: int = 0,
    friendship_badges: int = 0,
    horse: bool = False,
    fairy: bool = False,
    fairy_lore: bool = False,
    amulet: bool = False,
    high_spirits: bool = False,
    seen_dragon: bool = False,
    sync_actor_id: bytes = bytes(16),
    sync_server_revision: int = 0,
    sync_committed_save_sequence: int = 0,
    last_realm_event_id: int = 0,
    realm_actor_ids: tuple[bytes, ...] | None = None,
    partner_actor_id: bytes = bytes(16),
    teamed_index: int | None = None,
    legacy_empty_slots: tuple[int, ...] = (),
    legacy_empty_chompcoin: int = 0,
) -> bytes:
    """Encode the real fixed C layout written by lord_save_encode()."""
    if version not in (3, 4, 5) or len(actor_id) != 16:
        raise ValueError("invalid test LORD record identity")
    if len(sync_actor_id) != 16:
        raise ValueError("invalid test sync actor")
    if realm_actor_ids is None:
        realm_actor_ids = (bytes(16),) * 8
    if len(realm_actor_ids) != 8 or any(
        len(value) != 16 for value in realm_actor_ids
    ):
        raise ValueError("invalid test realm actors")
    if len(partner_actor_id) != 16:
        raise ValueError("invalid test partner actor")

    save = bytearray(b"LDSV" + struct.pack("<HHII", version, 0, 0, save_sequence))
    save.extend(struct.pack("<II", 0x12345678, realm_revision))
    save.extend(
        struct.pack(
            "<8B",
            partner_code,
            npc_friend_code,
            pvp_fights_remaining,
            friendship_actions_remaining,
            igm_used_mask,
            0,
            0,
            0,
        )
    )
    save.extend(_fixed_text("", 48))
    save.extend(_fixed_text("", 48))

    save.extend(_fixed_text(name, 20))
    save.extend(struct.pack("<5B", hero_style, hero_class, level, 0, 0))
    save.extend(
        struct.pack(
            "<iiiiIIIH",
            hit_points,
            max_hit_points,
            strength,
            defense,
            chompcoin,
            bank,
            experience,
            forest_fights,
        )
    )
    for mastery, uses in zip(skill, skill_uses, strict=True):
        save.extend(struct.pack("<BB", mastery, uses))
    save.extend(
        struct.pack(
            "<B7H6B",
            dragon_kills,
            player_day,
            pvp_wins,
            pvp_losses,
            charm,
            gems,
            young_heroes_helped,
            friendship_badges,
            int(horse),
            int(fairy),
            int(fairy_lore),
            int(amulet),
            int(high_spirits),
            int(seen_dragon),
        )
    )

    for index in range(8):
        if index in legacy_empty_slots:
            save.extend(_fixed_text("Empty record", 24))
            save.extend(_fixed_text("", 40))
            save.extend(struct.pack("<7B", 0, 0, 0, 0, 0, 0, 0))
            save.extend(
                struct.pack(
                    "<iiiiIIHH",
                    0,
                    1,
                    1,
                    0,
                    legacy_empty_chompcoin,
                    0,
                    0,
                    0,
                )
            )
            continue
        save.extend(_fixed_text(f"Realm Hero {index + 1}", 24))
        save.extend(_fixed_text("Ready for adventure.", 40))
        save.extend(
            struct.pack(
                "<7B",
                index & 1,
                index % 3,
                3,
                1,
                0,
                int(index == teamed_index),
                25,
            )
        )
        save.extend(struct.pack("<iiiiIIHH", 20, 30, 12, 4, 500, 500, 0, 0))
    for _index in range(12):
        save.extend(struct.pack("<5B", 0, 0, 0, 0, 0))
        save.extend(_fixed_text("", 48))
    for _index in range(12):
        save.extend(struct.pack("<H", 0))
        save.extend(_fixed_text("", 52))

    if version >= 4:
        save.extend(b"MPV4" if version == 4 else b"MPV5")
        save.extend(b"".join(realm_actor_ids))
        save.extend(partner_actor_id)
        save.extend(struct.pack("<Q", last_realm_event_id))
    if version >= 5:
        save.extend(sync_actor_id)
        save.extend(
            struct.pack(
                "<II", sync_server_revision, sync_committed_save_sequence
            )
        )

    expected_lengths = {3: 2286, 4: 2442, 5: 2466}
    if len(save) != expected_lengths[version]:
        raise AssertionError(f"bad schema-{version} fixture length {len(save)}")
    struct.pack_into("<H", save, 6, len(save))
    struct.pack_into("<I", save, 8, zlib.crc32(save[16:]) & 0xFFFFFFFF)

    record = bytearray(b"LRSY")
    record.extend(
        struct.pack(
            "<HHIIIIQ",
            1,
            52,
            52 + len(save),
            0,
            realm_revision,
            save_sequence,
            nonce,
        )
    )
    record.extend(actor_id)
    record.extend(struct.pack("<I", len(save)))
    record.extend(save)
    struct.pack_into("<I", record, 12, zlib.crc32(record[16:]) & 0xFFFFFFFF)
    return bytes(record)


class HubSessionTests(unittest.TestCase):
    def test_unsigned_action_nonce_fails_closed_without_killing_session(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = StoreTests.add_profile(
                store, "nonce-wire", "Nonce Wire", 500
            )
            sent: list[bytes] = []
            hub = RealmHubSession(
                "nonce-wire", store, sent.append, test_offer(), session_id=77
            )
            hub.remote_peer_id = 99
            hub.connected = True
            hub.game_online = True
            unsigned_action = struct.pack(
                "<BBH16sQHHI",
                p4rm.ACTION_MAIL,
                0,
                0,
                actor,
                0xFFFFFFFFFFFFFFFF,
                0,
                0,
                0,
            )
            datagram = p4mp.encode_packet(
                p4mp.GAME_MESSAGE,
                77,
                99,
                1,
                p4rm.encode_message(
                    p4rm.ACTION_BEGIN,
                    900,
                    unsigned_action,
                    p4rm.BEGIN_INDEX,
                    0,
                ),
            )
            hub.receive(datagram)
            error_packet = p4mp.decode_packet(sent[-1])
            error = p4rm.decode_message(error_packet.payload)
            self.assertEqual(error.kind, p4rm.ERROR)
            self.assertTrue(hub.connected)
            self.assertTrue(hub.game_online)
            self.assertIsNone(hub.action_upload)
            with sqlite3.connect(store.path) as database:
                self.assertEqual(
                    database.execute(
                        "SELECT COUNT(*) FROM action_operations"
                    ).fetchone()[0],
                    0,
                )

            sent.clear()
            heartbeat = struct.pack("<Q", 1234)
            hub.receive(
                p4mp.encode_packet(p4mp.PING, 77, 99, 2, heartbeat)
            )
            pong = p4mp.decode_packet(sent[-1])
            self.assertEqual((pong.packet_type, pong.payload), (p4mp.PONG, heartbeat))

    def test_guild_status_and_page_requests_are_singletons(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = StoreTests.add_profile(
                store, "guild-wire", "Guild Wire", 500
            )
            sent: list[bytes] = []
            hub = RealmHubSession(
                "guild-wire", store, sent.append, test_offer(), session_id=77
            )
            self.assertEqual(hub.actor_id, actor)
            self.assertTrue(hub._send_guild_status())
            packet = p4mp.decode_packet(sent[-1])
            message = p4rm.decode_message(packet.payload)
            self.assertEqual(
                (message.kind, message.chunk_index, message.chunk_count),
                (p4rm.GUILD_STATUS, 0, 0),
            )
            self.assertEqual(p4rm.decode_guild_status(message.payload)[0], actor)

            malformed = p4rm.Message(
                p4rm.GUILD_PAGE, 80, 1, 1, p4rm.encode_guild_page_request(0)
            )
            hub._receive_guild_page(malformed)
            self.assertEqual(hub.guild_directory_pending, [])
            valid = p4rm.Message(
                p4rm.GUILD_PAGE, 81, 0, 0, p4rm.encode_guild_page_request(0)
            )
            hub._receive_guild_page(valid)
            self.assertEqual(len(hub.guild_directory_pending), 1)
            kind, index, count, payload = hub.guild_directory_pending[0]
            self.assertEqual((kind, index, count), (p4rm.GUILD_PAGE, 0, 0))
            self.assertEqual(p4rm.decode_guild_page(payload), (0, 0))

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

    def test_clean_save_bound_to_another_actor_never_downloads_or_adopts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            alice = store.actor_for_profile("alice")
            bob = store.actor_for_profile("bob")
            day, _ = store.realm_clock()
            alice_head = lord_record(alice, 40, name="Alice Hero")
            bob_head = lord_record(bob, 41, name="Bob Hero")
            self.assertEqual(
                store.commit(alice, 0, 40, day, alice_head), ("ok", 1)
            )
            self.assertEqual(
                store.commit(bob, 0, 41, day, bob_head), ("ok", 1)
            )
            sent: list[bytes] = []
            hub = RealmHubSession("bob", store, sent.append, test_offer())
            hub.session_id = 10
            hub.remote_peer_id = 20
            hub.connected = True
            clean_flags = p4rm.HELLO_HAS_LOCAL | p4rm.HELLO_HAS_SYNC_BASE
            hello = p4rm.Message(
                p4rm.HELLO,
                42,
                0,
                0,
                p4rm.encode_hello(alice, 1, 2, 2, clean_flags),
            )
            hub._begin_game_sync(hello)
            welcome = p4rm.decode_message(
                p4mp.decode_packet(sent.pop(0)).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(welcome.payload)[4],
                p4rm.WELCOME_LOCAL_CONFLICT,
            )
            self.assertIsNone(hub.download)
            self.assertFalse(hub.game_online)
            self.assertEqual(store.read_head(bob).snapshot, bob_head)

            # An operator grant cannot clone an already bound foreign actor.
            grant_id = store.authorize_local_adoption("bob")
            hub._begin_game_sync(hello)
            welcome = p4rm.decode_message(
                p4mp.decode_packet(sent.pop(0)).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(welcome.payload)[4],
                p4rm.WELCOME_LOCAL_CONFLICT,
            )
            self.assertIsNone(hub.adoption_grant_id)
            self.assertEqual(store.pending_local_adoption(bob), grant_id)
            self.assertEqual(store.read_head(bob).snapshot, bob_head)

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
            self.assertEqual(p4rm.decode_welcome(welcome.payload)[5], 1)
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
            self.assertEqual(p4rm.decode_welcome(welcome.payload)[5], 1)
            self.assertGreaterEqual(len(sent), 1)

    def test_upload_crossing_hour_returns_retryable_stale_day(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            clock = [0.0]
            store = RealmStore(
                Path(directory) / "realm.sqlite3", epoch_seconds=0
            )
            actor = store.actor_for_profile("console-one")
            base = lord_record(actor, 80)
            self.assertEqual(
                store.commit(actor, 0, 80, 1, base, now=clock[0]),
                ("ok", 1),
            )
            sent: list[bytes] = []
            hub = RealmHubSession(
                "console-one",
                store,
                sent.append,
                test_offer(),
                now=lambda: clock[0],
            )
            hub.session_id = 10
            hub.remote_peer_id = 20
            hub.connected = True
            hub.game_online = True
            hub.awaiting_local_commit = True
            hub.awaiting_local_save_sequence = 3

            nonce = 81
            dirty = lord_record(
                actor,
                nonce,
                save_sequence=3,
                chompcoin=600,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            chunks = p4rm.record_chunks(dirty)
            hub._begin_upload(
                p4rm.Message(
                    p4rm.UPLOAD_BEGIN,
                    900,
                    p4rm.BEGIN_INDEX,
                    len(chunks),
                    p4rm.encode_upload_begin(1, dirty, nonce, 1),
                )
            )
            begin_ack = p4rm.decode_message(
                p4mp.decode_packet(sent.pop(0)).payload
            )
            self.assertEqual(begin_ack.kind, p4rm.ACK)

            clock[0] = 3600.0
            for index, chunk in enumerate(chunks):
                hub._receive_upload_chunk(
                    p4rm.Message(
                        p4rm.UPLOAD_CHUNK,
                        900,
                        index,
                        len(chunks),
                        chunk,
                    )
                )
                ack = p4rm.decode_message(
                    p4mp.decode_packet(sent.pop(0)).payload
                )
                self.assertEqual(ack.kind, p4rm.ACK)
            result = p4rm.decode_message(
                p4mp.decode_packet(sent.pop(0)).payload
            )
            self.assertEqual(result.kind, p4rm.COMMIT_RESULT)
            self.assertEqual(result.payload[0], p4rm.COMMIT_STALE_DAY)
            self.assertEqual(struct.unpack_from("<I", result.payload, 1)[0], 1)
            self.assertEqual(struct.unpack_from("<Q", result.payload, 5)[0], 2)
            self.assertTrue(hub.game_online)
            self.assertTrue(hub.awaiting_local_commit)
            self.assertTrue(hub.awaiting_local_rollover_allowed)
            self.assertEqual(store.read_head(actor).snapshot, base)

            rolled = lord_record(
                actor,
                82,
                save_sequence=4,
                chompcoin=600,
                player_day=2,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            chunks = p4rm.record_chunks(rolled)
            hub._begin_upload(
                p4rm.Message(
                    p4rm.UPLOAD_BEGIN,
                    901,
                    p4rm.BEGIN_INDEX,
                    len(chunks),
                    p4rm.encode_upload_begin(1, rolled, 82, 2),
                )
            )
            self.assertEqual(
                p4rm.decode_message(
                    p4mp.decode_packet(sent.pop(0)).payload
                ).kind,
                p4rm.ACK,
            )
            for index, chunk in enumerate(chunks):
                hub._receive_upload_chunk(
                    p4rm.Message(
                        p4rm.UPLOAD_CHUNK,
                        901,
                        index,
                        len(chunks),
                        chunk,
                    )
                )
                self.assertEqual(
                    p4rm.decode_message(
                        p4mp.decode_packet(sent.pop(0)).payload
                    ).kind,
                    p4rm.ACK,
                )
            result = p4rm.decode_message(
                p4mp.decode_packet(sent.pop(0)).payload
            )
            self.assertEqual(result.payload[0], p4rm.COMMIT_OK)
            self.assertFalse(hub.awaiting_local_commit)
            self.assertFalse(hub.awaiting_local_rollover_allowed)
            self.assertEqual(store.read_head(actor).snapshot, rolled)

    def test_dirty_welcome_rollover_releases_expected_generation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            clock = [0.0]
            store = RealmStore(
                Path(directory) / "realm.sqlite3", epoch_seconds=0
            )
            actor = store.actor_for_profile("console-one")
            base = lord_record(actor, 90)
            self.assertEqual(
                store.commit(actor, 0, 90, 1, base, now=clock[0]),
                ("ok", 1),
            )
            clock[0] = 3_600.0
            sent: list[bytes] = []
            hub = RealmHubSession(
                "console-one",
                store,
                sent.append,
                test_offer(),
                now=lambda: clock[0],
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
                    91,
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
                p4rm.WELCOME_ACCEPT_LOCAL
                | p4rm.WELCOME_ROLLOVER_PENDING,
            )
            self.assertTrue(hub.awaiting_local_commit)
            self.assertTrue(hub.awaiting_local_rollover_allowed)

            rolled = lord_record(
                actor,
                92,
                save_sequence=4,
                chompcoin=600,
                player_day=2,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            chunks = p4rm.record_chunks(rolled)
            hub._begin_upload(
                p4rm.Message(
                    p4rm.UPLOAD_BEGIN,
                    92,
                    p4rm.BEGIN_INDEX,
                    len(chunks),
                    p4rm.encode_upload_begin(1, rolled, 92, 2),
                )
            )
            sent.pop(0)
            for index, chunk in enumerate(chunks):
                hub._receive_upload_chunk(
                    p4rm.Message(
                        p4rm.UPLOAD_CHUNK,
                        92,
                        index,
                        len(chunks),
                        chunk,
                    )
                )
                sent.pop(0)
            result = p4rm.decode_message(
                p4mp.decode_packet(sent.pop(0)).payload
            )
            self.assertEqual(result.payload[0], p4rm.COMMIT_OK)
            self.assertFalse(hub.awaiting_local_commit)
            self.assertFalse(hub.awaiting_local_rollover_allowed)
            self.assertEqual(store.read_head(actor).snapshot, rolled)

    def test_lost_normal_commit_result_safely_downloads_exact_head(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("console-one")
            day, _ = store.realm_clock()
            base = lord_record(actor, 70)
            self.assertEqual(store.commit(actor, 0, 70, day, base), ("ok", 1))
            uploaded = lord_record(
                actor,
                71,
                save_sequence=3,
                chompcoin=600,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(actor, 1, 71, day, uploaded), ("ok", 2)
            )

            sent: list[bytes] = []
            hub = RealmHubSession(
                "console-one", store, sent.append, test_offer()
            )
            hub.session_id = 10
            hub.remote_peer_id = 20
            hub.connected = True
            flags = (
                p4rm.HELLO_HAS_LOCAL
                | p4rm.HELLO_HAS_SYNC_BASE
                | p4rm.HELLO_LOCAL_DIRTY
            )
            hub._begin_game_sync(
                p4rm.Message(
                    p4rm.HELLO,
                    72,
                    0,
                    0,
                    p4rm.encode_hello(actor, 1, 2, 3, flags),
                )
            )
            welcome = p4rm.decode_message(
                p4mp.decode_packet(sent[0]).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(welcome.payload)[4],
                p4rm.WELCOME_HAS_SNAPSHOT,
            )
            self.assertIsNotNone(hub.download)
            self.assertTrue(hub.game_online)

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
            self.assertEqual(saved_profile.chompcoin, 500)
            self.assertEqual(saved_profile.bank, 500)

    def test_profile_packets_cannot_restore_transfer_debit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            database_path = Path(directory) / "realm.sqlite3"
            store = RealmStore(database_path)
            sent: list[bytes] = []
            hub = RealmHubSession("console-one", store, sent.append, test_offer())
            day, _ = store.realm_clock()
            snapshot = lord_record(hub.actor_id, 100)
            self.assertEqual(
                store.commit(hub.actor_id, 0, 100, day, snapshot), ("ok", 1)
            )
            target = store.actor_for_profile("console-two")
            self.assertEqual(
                store.commit(
                    target,
                    0,
                    99,
                    day,
                    lord_record(
                        target, 99, name="Second Hero", chompcoin=100
                    ),
                ),
                ("ok", 1),
            )
            transfer = store.perform_action(
                hub.actor_id,
                101,
                p4rm.ACTION_TRANSFER,
                0,
                100,
                target,
                b"",
            )
            self.assertEqual(transfer.status, p4rm.ACTION_OK)

            forged_profile = bytearray(48)
            forged_profile[:11] = b"Forged Hero"
            forged_profile[20:24] = bytes([1, 2, 12, 2])
            struct.pack_into(
                "<iiiiHHI",
                forged_profile,
                24,
                99_999,
                99_999,
                99_999,
                99_999,
                9_999,
                9_999,
                999_999_999,
            )
            hub._receive_profile(
                p4rm.Message(p4rm.PROFILE, 102, 0, 0, bytes(forged_profile))
            )
            hub._receive_profile_stats(
                p4rm.Message(
                    p4rm.PROFILE_STATS,
                    102,
                    0,
                    0,
                    struct.pack("<II", 999_999_999, 500),
                )
            )
            profile = store.list_profiles(exclude_actor_id=target)[0]
            self.assertEqual(profile.name, "Test Hero")
            self.assertEqual(profile.level, 3)
            self.assertEqual(profile.flags, 3)
            self.assertEqual(profile.chompcoin, 500)
            self.assertEqual(profile.bank, 400)
            target_profile = store.list_profiles(exclude_actor_id=hub.actor_id)[0]
            self.assertEqual(target_profile.chompcoin, 200)

            transfer_event = store.next_event(hub.actor_id)
            self.assertIsNotNone(transfer_event)
            ignored_event = lord_record(
                hub.actor_id,
                103,
                save_sequence=3,
                sync_actor_id=hub.actor_id,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(hub.actor_id, 1, 103, day, ignored_event),
                ("ok", 2),
            )
            self.assertEqual(
                store.list_profiles(exclude_actor_id=target)[0].bank, 400
            )
            self.assertEqual(
                store.next_event(hub.actor_id).event_id,
                transfer_event.event_id,
            )

            # A matching durable snapshot is stronger than the lossy wire ACK.
            # After a hub restart, a forged cursor still cannot launder the
            # debit, while the stock-client reflection completes without ACK.
            store = RealmStore(database_path)
            laundered_debit = lord_record(
                hub.actor_id,
                104,
                save_sequence=4,
                bank=500,
                last_realm_event_id=transfer_event.event_id,
                sync_actor_id=hub.actor_id,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            self.assertEqual(
                store.commit(hub.actor_id, 2, 104, day, laundered_debit),
                ("invalid", 2),
            )
            unacknowledged_reflection = lord_record(
                hub.actor_id,
                105,
                save_sequence=4,
                bank=400,
                last_realm_event_id=transfer_event.event_id,
                sync_actor_id=hub.actor_id,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            self.assertEqual(
                store.commit(
                    hub.actor_id, 2, 105, day, unacknowledged_reflection
                ),
                ("ok", 3),
            )
            self.assertEqual(
                store.list_profiles(exclude_actor_id=target)[0].bank, 400
            )
            # A late ACK races safely with already-terminal snapshot proof.
            self.assertTrue(
                store.acknowledge_event(hub.actor_id, transfer_event.event_id)
            )
            with sqlite3.connect(store.path) as database:
                applied_revision = database.execute(
                    "SELECT applied_revision FROM realm_economy_events "
                    "WHERE event_id = ?",
                    (transfer_event.event_id,),
                ).fetchone()[0]
            self.assertEqual(applied_revision, 3)

            credit_event = store.next_event(target)
            self.assertIsNotNone(credit_event)
            self.assertTrue(store.acknowledge_event(target, credit_event.event_id))
            laundered_credit = lord_record(
                target,
                107,
                save_sequence=3,
                name="Second Hero",
                chompcoin=100,
                last_realm_event_id=credit_event.event_id,
                sync_actor_id=target,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(target, 1, 107, day, laundered_credit),
                ("invalid", 1),
            )
            reflected_credit = lord_record(
                target,
                108,
                save_sequence=3,
                name="Second Hero",
                chompcoin=200,
                last_realm_event_id=credit_event.event_id,
                sync_actor_id=target,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(target, 1, 108, day, reflected_credit), ("ok", 2)
            )
            self.assertEqual(
                store.list_profiles(exclude_actor_id=hub.actor_id)[0].chompcoin,
                200,
            )

            lease = store.perform_action(
                hub.actor_id,
                109,
                p4rm.ACTION_PVP_BEGIN,
                0,
                0,
                target,
                b"",
            )
            self.assertEqual(lease.status, p4rm.ACTION_OK)
            resolution = store.perform_action(
                hub.actor_id,
                110,
                p4rm.ACTION_PVP_RESOLVE,
                1,
                0,
                target,
                lease.related_id.to_bytes(8, "little") + b"\x01",
            )
            self.assertEqual(
                (resolution.status, resolution.value), (p4rm.ACTION_OK, 0)
            )
            target_duel = store.next_event(target)
            winner_reward = store.next_event(hub.actor_id)
            self.assertEqual((target_duel.code, target_duel.value), (1, 100))
            self.assertEqual((winner_reward.code, winner_reward.value), (2, 100))

            stale_winner = lord_record(
                hub.actor_id,
                111,
                save_sequence=5,
                bank=400,
                chompcoin=500,
                pvp_wins=3,
                last_realm_event_id=winner_reward.event_id,
                sync_actor_id=hub.actor_id,
                sync_server_revision=3,
                sync_committed_save_sequence=4,
            )
            self.assertEqual(
                store.commit(hub.actor_id, 3, 111, day, stale_winner),
                ("invalid", 3),
            )
            reflected_winner = lord_record(
                hub.actor_id,
                112,
                save_sequence=5,
                bank=400,
                chompcoin=600,
                pvp_wins=3,
                last_realm_event_id=winner_reward.event_id,
                sync_actor_id=hub.actor_id,
                sync_server_revision=3,
                sync_committed_save_sequence=4,
            )
            self.assertEqual(
                store.commit(hub.actor_id, 3, 112, day, reflected_winner),
                ("ok", 4),
            )

            stale_target = lord_record(
                target,
                113,
                save_sequence=4,
                name="Second Hero",
                chompcoin=200,
                hit_points=0,
                pvp_losses=2,
                last_realm_event_id=target_duel.event_id,
                sync_actor_id=target,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            self.assertEqual(
                store.commit(target, 2, 113, day, stale_target),
                ("invalid", 2),
            )
            reflected_target = lord_record(
                target,
                114,
                save_sequence=4,
                name="Second Hero",
                chompcoin=100,
                hit_points=0,
                pvp_losses=2,
                last_realm_event_id=target_duel.event_id,
                sync_actor_id=target,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            self.assertEqual(
                store.commit(target, 2, 114, day, reflected_target), ("ok", 3)
            )

    def test_conflicting_local_save_requires_and_consumes_adoption_grant(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("pink")
            day, _ = store.realm_clock()
            head = lord_record(
                actor, 200, pvp_wins=10, pvp_losses=8
            )
            self.assertEqual(store.commit(actor, 0, 200, day, head), ("ok", 1))
            sent: list[bytes] = []
            hub = RealmHubSession("pink", store, sent.append, test_offer())
            hub.session_id = 10
            hub.remote_peer_id = 20
            hub.connected = True
            dirty_flags = (
                p4rm.HELLO_HAS_LOCAL
                | p4rm.HELLO_HAS_SYNC_BASE
                | p4rm.HELLO_LOCAL_DIRTY
            )
            hello = p4rm.Message(
                p4rm.HELLO,
                201,
                0,
                0,
                p4rm.encode_hello(actor, 99, 2, 10, dirty_flags),
            )
            hub._begin_game_sync(hello)
            conflict = p4rm.decode_message(
                p4mp.decode_packet(sent.pop()).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(conflict.payload)[4],
                p4rm.WELCOME_LOCAL_CONFLICT,
            )
            self.assertEqual(store.read_head(actor).snapshot, head)

            grant_id = store.authorize_local_adoption("pink")
            hub._begin_game_sync(hello)
            welcome = p4rm.decode_message(
                p4mp.decode_packet(sent.pop()).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(welcome.payload)[4],
                p4rm.WELCOME_ADOPT_LOCAL,
            )
            self.assertEqual(hub.adoption_grant_id, grant_id)
            self.assertTrue(hub.game_online)
            self.assertIsNone(hub.download)

            adopted = lord_record(
                actor,
                202,
                save_sequence=10,
                name="Pink Hero",
                chompcoin=5_000,
                pvp_wins=0,
                pvp_losses=0,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=0,
            )
            chunks = p4rm.record_chunks(adopted)
            hub._begin_upload(
                p4rm.Message(
                    p4rm.UPLOAD_BEGIN,
                    203,
                    p4rm.BEGIN_INDEX,
                    len(chunks),
                    p4rm.encode_upload_begin(1, adopted, 202, day),
                )
            )
            for index, chunk in enumerate(chunks):
                hub._receive_upload_chunk(
                    p4rm.Message(
                        p4rm.UPLOAD_CHUNK,
                        203,
                        index,
                        len(chunks),
                        chunk,
                    )
                )
            result = p4rm.decode_message(
                p4mp.decode_packet(sent[-1]).payload
            )
            self.assertEqual(result.kind, p4rm.COMMIT_RESULT)
            self.assertEqual(result.payload[0], p4rm.COMMIT_OK)
            self.assertEqual(store.read_head(actor).revision, 2)
            self.assertEqual(store.read_head(actor).snapshot, adopted)
            self.assertIsNone(store.pending_local_adoption(actor))
            self.assertIsNone(hub.adoption_grant_id)

            retry_sent: list[bytes] = []
            retry = RealmHubSession(
                "pink", store, retry_sent.append, test_offer()
            )
            retry.session_id = 11
            retry.remote_peer_id = 21
            retry.connected = True
            retry._begin_game_sync(
                p4rm.Message(
                    p4rm.HELLO,
                    204,
                    0,
                    0,
                    p4rm.encode_hello(actor, 1, 0, 10, dirty_flags),
                )
            )
            retry_welcome = p4rm.decode_message(
                p4mp.decode_packet(retry_sent[0]).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(retry_welcome.payload)[4],
                p4rm.WELCOME_HAS_SNAPSHOT,
            )
            self.assertIsNotNone(retry.download)

    def test_empty_head_adoption_and_rev0_lost_ack_bridge(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("pink")
            grant_id = store.authorize_local_adoption("pink")
            day, _ = store.realm_clock()
            sent: list[bytes] = []
            hub = RealmHubSession("pink", store, sent.append, test_offer())
            hub.session_id = 30
            hub.remote_peer_id = 40
            hub.connected = True
            local_dirty = p4rm.HELLO_HAS_LOCAL | p4rm.HELLO_LOCAL_DIRTY
            hub._begin_game_sync(
                p4rm.Message(
                    p4rm.HELLO,
                    300,
                    0,
                    0,
                    p4rm.encode_hello(bytes(16), 0, 0, 5, local_dirty),
                )
            )
            welcome = p4rm.decode_message(
                p4mp.decode_packet(sent.pop()).payload
            )
            _actor, revision, _day, _remaining, flags, _head_day = p4rm.decode_welcome(
                welcome.payload
            )
            self.assertEqual(revision, 0)
            self.assertEqual(flags, p4rm.WELCOME_ADOPT_LOCAL)
            self.assertEqual(hub.adoption_grant_id, grant_id)

            adopted = lord_record(
                actor,
                301,
                save_sequence=5,
                name="Pink Hero",
                sync_actor_id=actor,
                sync_server_revision=0,
                sync_committed_save_sequence=0,
            )
            with self.assertRaises(ValueError):
                decode_lord_sync(adopted, expected_actor_id=actor)
            decode_lord_sync(
                adopted,
                expected_actor_id=actor,
                allow_adoption_bridge=True,
            )
            chunks = p4rm.record_chunks(adopted)
            hub._begin_upload(
                p4rm.Message(
                    p4rm.UPLOAD_BEGIN,
                    302,
                    p4rm.BEGIN_INDEX,
                    len(chunks),
                    p4rm.encode_upload_begin(0, adopted, 301, day),
                )
            )
            for index, chunk in enumerate(chunks):
                hub._receive_upload_chunk(
                    p4rm.Message(
                        p4rm.UPLOAD_CHUNK,
                        302,
                        index,
                        len(chunks),
                        chunk,
                    )
                )
            result = p4rm.decode_message(
                p4mp.decode_packet(sent[-1]).payload
            )
            self.assertEqual(result.payload[0], p4rm.COMMIT_OK)
            self.assertEqual(store.read_head(actor).revision, 1)

            sent.clear()
            hub._begin_upload(
                p4rm.Message(
                    p4rm.UPLOAD_BEGIN,
                    304,
                    p4rm.BEGIN_INDEX,
                    len(chunks),
                    p4rm.encode_upload_begin(0, adopted, 301, day),
                )
            )
            for index, chunk in enumerate(chunks):
                hub._receive_upload_chunk(
                    p4rm.Message(
                        p4rm.UPLOAD_CHUNK,
                        304,
                        index,
                        len(chunks),
                        chunk,
                    )
                )
            duplicate = p4rm.decode_message(
                p4mp.decode_packet(sent[-1]).payload
            )
            self.assertEqual(duplicate.payload[0], p4rm.COMMIT_OK)
            self.assertEqual(struct.unpack_from("<I", duplicate.payload, 1)[0], 1)

            divergent = lord_record(
                actor,
                301,
                save_sequence=5,
                name="Pink Hero",
                chompcoin=501,
                sync_actor_id=actor,
                sync_server_revision=0,
                sync_committed_save_sequence=0,
            )
            divergent_chunks = p4rm.record_chunks(divergent)
            sent.clear()
            hub._begin_upload(
                p4rm.Message(
                    p4rm.UPLOAD_BEGIN,
                    305,
                    p4rm.BEGIN_INDEX,
                    len(divergent_chunks),
                    p4rm.encode_upload_begin(0, divergent, 301, day),
                )
            )
            for index, chunk in enumerate(divergent_chunks):
                hub._receive_upload_chunk(
                    p4rm.Message(
                        p4rm.UPLOAD_CHUNK,
                        305,
                        index,
                        len(divergent_chunks),
                        chunk,
                    )
                )
            rejected = p4rm.decode_message(
                p4mp.decode_packet(sent[-1]).payload
            )
            self.assertEqual(rejected.payload[0], p4rm.COMMIT_INVALID)
            self.assertEqual(store.read_head(actor).revision, 1)

            retry_sent: list[bytes] = []
            retry = RealmHubSession(
                "pink", store, retry_sent.append, test_offer()
            )
            retry.session_id = 31
            retry.remote_peer_id = 41
            retry.connected = True
            retry._begin_game_sync(
                p4rm.Message(
                    p4rm.HELLO,
                    303,
                    0,
                    0,
                    p4rm.encode_hello(actor, 0, 0, 5, local_dirty),
                )
            )
            retry_welcome = p4rm.decode_message(
                p4mp.decode_packet(retry_sent[0]).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(retry_welcome.payload)[4],
                p4rm.WELCOME_HAS_SNAPSHOT,
            )
            self.assertIsNotNone(retry.download)

    def test_dirty_upload_defers_events_actions_and_next_delivery(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("console-one")
            target = store.actor_for_profile("console-two")
            day, _ = store.realm_clock()
            self.assertEqual(
                store.commit(
                    actor,
                    0,
                    500,
                    day,
                    lord_record(actor, 500, name="First Hero"),
                ),
                ("ok", 1),
            )
            self.assertEqual(
                store.commit(
                    target,
                    0,
                    501,
                    day,
                    lord_record(target, 501, name="Second Hero"),
                ),
                ("ok", 1),
            )
            self.assertEqual(
                store.perform_action(
                    actor,
                    502,
                    p4rm.ACTION_FRIEND,
                    1,
                    0,
                    target,
                    b"",
                ).status,
                p4rm.ACTION_OK,
            )

            clock = [float((day - 1) * 3600 + 1)]
            sent: list[bytes] = []
            hub = RealmHubSession(
                "console-one",
                store,
                sent.append,
                test_offer(),
                now=lambda: clock[0],
            )
            hub.session_id = 10
            hub.remote_peer_id = 20
            hub.connected = True

            def realm_messages() -> list[p4rm.Message]:
                messages = []
                for datagram in sent:
                    packet = p4mp.decode_packet(datagram)
                    if packet.packet_type == p4mp.GAME_MESSAGE:
                        messages.append(p4rm.decode_message(packet.payload))
                return messages

            def upload(record: bytes, expected: int, nonce: int, tx: int) -> None:
                chunks = p4rm.record_chunks(record)
                hub._begin_upload(
                    p4rm.Message(
                        p4rm.UPLOAD_BEGIN,
                        tx,
                        p4rm.BEGIN_INDEX,
                        len(chunks),
                        p4rm.encode_upload_begin(
                            expected, record, nonce, day
                        ),
                    )
                )
                for index, chunk in enumerate(chunks):
                    hub._receive_upload_chunk(
                        p4rm.Message(
                            p4rm.UPLOAD_CHUNK,
                            tx,
                            index,
                            len(chunks),
                            chunk,
                        )
                    )

            dirty_flags = (
                p4rm.HELLO_HAS_LOCAL
                | p4rm.HELLO_HAS_SYNC_BASE
                | p4rm.HELLO_LOCAL_DIRTY
            )
            hub._begin_game_sync(
                p4rm.Message(
                    p4rm.HELLO,
                    503,
                    0,
                    0,
                    p4rm.encode_hello(actor, 1, 2, 3, dirty_flags),
                )
            )
            welcome = realm_messages()[0]
            self.assertEqual(welcome.kind, p4rm.WELCOME)
            self.assertEqual(
                p4rm.decode_welcome(welcome.payload)[4],
                p4rm.WELCOME_ACCEPT_LOCAL,
            )
            self.assertTrue(hub.awaiting_local_commit)
            for _ in range(3):
                clock[0] += 1.0
                hub.tick()
            self.assertFalse(
                any(
                    message.kind
                    in (
                        p4rm.EVENT_BEGIN,
                        p4rm.DIRECTORY_PAGE,
                        p4rm.DIRECTORY_SUMMARY,
                        p4rm.DIRECTORY_STATS,
                        p4rm.DIRECTORY_DEEDS,
                        p4rm.CLOCK,
                    )
                    for message in realm_messages()
                )
            )

            blocked_nonce = 504
            hub._receive_realm(
                p4rm.Message(
                    p4rm.ACTION_BEGIN,
                    504,
                    p4rm.BEGIN_INDEX,
                    1,
                    p4rm.encode_action_begin(
                        p4rm.ACTION_MAIL,
                        0,
                        0,
                        target,
                        blocked_nonce,
                        b"BLOCKED",
                    ),
                )
            )
            blocked = realm_messages()[-1]
            self.assertEqual(blocked.kind, p4rm.ACTION_RESULT)
            self.assertEqual(
                p4rm.decode_action_result(blocked.payload)[0],
                p4rm.ACTION_BUSY,
            )
            with sqlite3.connect(store.path) as database:
                self.assertEqual(
                    database.execute(
                        "SELECT COUNT(*) FROM action_operations "
                        "WHERE source_actor_id = ? AND nonce = ?",
                        (actor, blocked_nonce),
                    ).fetchone()[0],
                    0,
                )

            dirty = lord_record(
                actor,
                505,
                save_sequence=3,
                name="First Hero",
                chompcoin=600,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            upload(dirty, 1, 505, 505)
            self.assertFalse(hub.awaiting_local_commit)
            self.assertEqual(store.read_head(actor).revision, 2)
            hub.tick()
            self.assertEqual(realm_messages()[-1].kind, p4rm.EVENT_BEGIN)
            outgoing = hub.outgoing_event
            self.assertIsNotNone(outgoing)
            debit_id = outgoing.event.event_id
            hub._receive_event_ack(
                p4rm.Message(
                    p4rm.EVENT_ACK,
                    outgoing.transaction_id,
                    0,
                    0,
                    struct.pack("<Q", debit_id),
                )
            )
            self.assertEqual(hub.awaiting_event_commit, debit_id)

            self.assertEqual(
                store.perform_action(
                    target,
                    506,
                    p4rm.ACTION_MAIL,
                    0,
                    0,
                    actor,
                    b"SECOND EVENT",
                ).status,
                p4rm.ACTION_OK,
            )
            event_count = sum(
                message.kind == p4rm.EVENT_BEGIN
                for message in realm_messages()
            )
            clock[0] += 1.0
            hub.tick()
            self.assertEqual(
                sum(
                    message.kind == p4rm.EVENT_BEGIN
                    for message in realm_messages()
                ),
                event_count,
            )

            reflected = lord_record(
                actor,
                507,
                save_sequence=4,
                name="First Hero",
                chompcoin=500,
                charm=11,
                friendship_actions_remaining=2,
                last_realm_event_id=debit_id,
                sync_actor_id=actor,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            upload(reflected, 2, 507, 507)
            self.assertEqual(hub.awaiting_event_commit, 0)
            hub.tick()
            self.assertEqual(realm_messages()[-1].kind, p4rm.EVENT_BEGIN)
            self.assertGreater(hub.outgoing_event.event.event_id, debit_id)

    def test_idempotent_upload_replay_cannot_release_dirty_barrier(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            store = RealmStore(Path(directory) / "realm.sqlite3")
            actor = store.actor_for_profile("console-one")
            day, _ = store.realm_clock()
            original = lord_record(actor, 600, name="First Hero")
            self.assertEqual(
                store.commit(actor, 0, 600, day, original), ("ok", 1)
            )
            current = lord_record(
                actor,
                601,
                save_sequence=3,
                name="First Hero",
                chompcoin=600,
                sync_actor_id=actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(actor, 1, 601, day, current), ("ok", 2)
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
                    602,
                    0,
                    0,
                    p4rm.encode_hello(actor, 2, 3, 4, dirty_flags),
                )
            )
            welcome = p4rm.decode_message(
                p4mp.decode_packet(sent.pop()).payload
            )
            self.assertEqual(
                p4rm.decode_welcome(welcome.payload)[4],
                p4rm.WELCOME_ACCEPT_LOCAL,
            )
            self.assertTrue(hub.awaiting_local_commit)
            self.assertEqual(hub.awaiting_local_save_sequence, 4)

            def upload(
                record: bytes, expected: int, nonce: int, transaction: int
            ) -> tuple[int, int]:
                sent.clear()
                chunks = p4rm.record_chunks(record)
                hub._begin_upload(
                    p4rm.Message(
                        p4rm.UPLOAD_BEGIN,
                        transaction,
                        p4rm.BEGIN_INDEX,
                        len(chunks),
                        p4rm.encode_upload_begin(
                            expected, record, nonce, day
                        ),
                    )
                )
                for index, chunk in enumerate(chunks):
                    hub._receive_upload_chunk(
                        p4rm.Message(
                            p4rm.UPLOAD_CHUNK,
                            transaction,
                            index,
                            len(chunks),
                            chunk,
                        )
                    )
                result = p4rm.decode_message(
                    p4mp.decode_packet(sent[-1]).payload
                )
                return result.payload[0], struct.unpack_from(
                    "<I", result.payload, 1
                )[0]

            # The store returns the original successful result for exact
            # operation replays.  Neither an ancient operation nor the
            # already-current head represents the generation accepted above.
            self.assertEqual(upload(original, 0, 600, 603), (p4rm.COMMIT_OK, 1))
            self.assertTrue(hub.awaiting_local_commit)
            self.assertEqual(upload(current, 1, 601, 604), (p4rm.COMMIT_OK, 2))
            self.assertTrue(hub.awaiting_local_commit)
            self.assertEqual(store.read_head(actor).revision, 2)

            dirty = lord_record(
                actor,
                605,
                save_sequence=4,
                name="First Hero",
                chompcoin=650,
                sync_actor_id=actor,
                sync_server_revision=2,
                sync_committed_save_sequence=3,
            )
            self.assertEqual(upload(dirty, 2, 605, 605), (p4rm.COMMIT_OK, 3))
            self.assertFalse(hub.awaiting_local_commit)
            self.assertIsNone(hub.awaiting_local_save_sequence)
            self.assertEqual(store.read_head(actor).snapshot, dirty)

    def test_two_node_mail_delivery_acknowledges_durable_event(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            clock = [1000.0]
            store = RealmStore(Path(directory) / "realm.sqlite3")
            first_actor = store.actor_for_profile("console-one")
            second_actor = store.actor_for_profile("console-two")
            day, _ = store.realm_clock()
            self.assertEqual(
                store.commit(
                    first_actor,
                    0,
                    680,
                    day,
                    lord_record(
                        first_actor, 680, name="First Hero", chompcoin=200
                    ),
                ),
                ("ok", 1),
            )
            self.assertEqual(
                store.commit(
                    second_actor,
                    0,
                    690,
                    day,
                    lord_record(
                        second_actor, 690, name="Second Hero", chompcoin=300
                    ),
                ),
                ("ok", 1),
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
            self.assertEqual(second.received_event_cursor, event_id)
            self.assertEqual(store.next_event(second_actor).event_id, event_id)
            self.assertIsNone(
                store.next_event(second_actor, after_event_id=event_id)
            )

            # A fresh session has no volatile delivery cursor, so an ACK that
            # preceded a power loss is replayed.  The client's persisted event
            # cursor makes that replay idempotent.
            replay_sent: list[bytes] = []
            replay = RealmHubSession(
                "console-two",
                store,
                replay_sent.append,
                test_offer(),
                now=lambda: clock[0],
            )
            replay.session_id = 11
            replay.remote_peer_id = 21
            replay.connected = True
            replay.game_online = True
            replay.next_directory_at = clock[0] + 60
            replay.next_clock_at = clock[0] + 60
            replay.tick()
            replayed = p4rm.decode_message(
                p4mp.decode_packet(replay_sent.pop()).payload
            )
            self.assertEqual(p4rm.decode_event_begin(replayed.payload)[0], event_id)
            replay._receive_realm(
                p4rm.Message(
                    p4rm.ACK,
                    replayed.transaction_id,
                    p4rm.BEGIN_INDEX,
                    1,
                    bytes([p4rm.EVENT_BEGIN]),
                )
            )
            replayed_body = p4rm.decode_message(
                p4mp.decode_packet(replay_sent.pop()).payload
            )
            self.assertEqual(replayed_body.payload, body)

            saved_event = lord_record(
                second_actor,
                701,
                save_sequence=3,
                name="Second Hero",
                chompcoin=300,
                last_realm_event_id=event_id,
                sync_actor_id=second_actor,
                sync_server_revision=1,
                sync_committed_save_sequence=2,
            )
            self.assertEqual(
                store.commit(second_actor, 1, 701, day, saved_event), ("ok", 2)
            )
            replay._receive_realm(
                p4rm.Message(
                    p4rm.EVENT_ACK,
                    replayed.transaction_id,
                    0,
                    0,
                    struct.pack("<Q", event_id),
                )
            )
            self.assertIsNone(replay.outgoing_event)
            self.assertEqual(replay.awaiting_event_commit, 0)
            self.assertIsNone(store.next_event(second_actor))

            # The cursor snapshot won the race with the late duplicate ACK.
            # That ACK must not re-arm a session barrier after durability is
            # already proven, or the next event stalls until reconnect.
            self.assertEqual(
                store.perform_action(
                    first_actor,
                    702,
                    p4rm.ACTION_MAIL,
                    0,
                    0,
                    second_actor,
                    b"NEXT MESSAGE",
                ).status,
                p4rm.ACTION_OK,
            )
            replay.tick()
            next_event = p4rm.decode_message(
                p4mp.decode_packet(replay_sent.pop()).payload
            )
            self.assertEqual(next_event.kind, p4rm.EVENT_BEGIN)
            self.assertGreater(
                p4rm.decode_event_begin(next_event.payload)[0], event_id
            )
            self.assertNotEqual(first_actor, second_actor)


if __name__ == "__main__":
    unittest.main()
