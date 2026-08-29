"""Four-console, four-realm-day LORD campaign through encoded P4MP/P4RM.

The SDL host does not expose the cartridge multiplayer callbacks, so this is
the executable multiplayer acceptance layer: four independent persisted
console models exchange only encoded datagrams with four Mac hub sessions
sharing one SQLite realm.  It covers offline branches, hourly revival, eight
duels, durable outcome delivery, a hub restart during an uncommitted receipt,
and an authoritative download after a committed sync is lost locally.
"""

from __future__ import annotations

import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.p4_realm_hub import p4rm
from tools.p4_realm_hub.hub import RealmHubSession
from tools.p4_realm_hub.lord_snapshot import decode_lord_sync
from tools.p4_realm_hub.store import RealmStore
from tools.p4_realm_hub.tests.test_two_client_e2e import (
    ProtocolConsole,
    realm_offer,
)


class FourPlayerCampaignTests(unittest.TestCase):
    def test_four_players_attack_and_sync_across_four_realm_days(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            database_path = Path(directory) / "lord-four-player.sqlite3"
            clock = [1_000.0]

            # PVP_BEGIN obtains its quota day from store.time.time(), while
            # RealmHubSession uses its injected clock.  Keep both clocks on
            # the same deterministic four-hour timeline.
            with mock.patch(
                "tools.p4_realm_hub.store.time.time",
                side_effect=lambda: clock[0],
            ):
                store = RealmStore(database_path, epoch_seconds=1_000)
                offer = realm_offer(0x4242424242424242)
                consoles = {
                    "A": ProtocolConsole("amber", "Amber Knight", 0xA001),
                    "B": ProtocolConsole("blue", "Blue Ranger", 0xB002),
                    "C": ProtocolConsole("coral", "Coral Mage", 0xC003),
                    "D": ProtocolConsole("gold", "Gold Guard", 0xD004),
                }
                for console in consoles.values():
                    console.chompcoin = 800
                    console.bank = 500
                    console.experience = 1_000
                    console.hit_points = 30
                    console.max_hit_points = 30
                    console.pvp_wins = 0
                    console.pvp_losses = 0

                session_number = [0]
                hub_restarts = 0
                directory_ids: dict[str, dict[str, bytes]] = {}

                def bind(console: ProtocolConsole) -> None:
                    session_number[0] += 1
                    console.bind(
                        RealmHubSession(
                            console.profile,
                            store,
                            console.receive_from_hub,
                            offer,
                            session_id=0xE000 + session_number[0],
                            now=lambda: clock[0],
                        )
                    )

                def bind_all() -> None:
                    for candidate in consoles.values():
                        bind(candidate)

                def restart_hub() -> None:
                    nonlocal store, hub_restarts
                    for candidate in consoles.values():
                        if candidate.hub is not None:
                            candidate.hub.transport_disconnected()
                    store = RealmStore(database_path, epoch_seconds=1_000)
                    bind_all()
                    hub_restarts += 1

                def connect_all() -> dict[str, int]:
                    flags: dict[str, int] = {}
                    for label, candidate in consoles.items():
                        candidate.connect()
                        flags[label] = candidate.hello()
                    return flags

                def spend_fight_and_begin(
                    attacker_label: str,
                    target_label: str,
                ) -> int:
                    attacker = consoles[attacker_label]
                    target = consoles[target_label]
                    before_fights = attacker.pvp_fights
                    status, kind, code, value, lease_id = attacker.action(
                        p4rm.ACTION_PVP_BEGIN,
                        0,
                        0,
                        directory_ids[attacker_label][target.name],
                    )
                    self.assertEqual(
                        (status, kind, code, value),
                        (p4rm.ACTION_OK, p4rm.ACTION_PVP_BEGIN, 0, 0),
                    )
                    self.assertGreater(lease_id, 0)
                    self.assertGreater(before_fights, 0)
                    attacker.pvp_fights -= 1
                    attacker.save_sequence += 1
                    attacker.upload_local()
                    return lease_id

                def resolve(
                    attacker_label: str,
                    target_label: str,
                    lease_id: int,
                    outcome: int,
                ) -> None:
                    attacker = consoles[attacker_label]
                    target = consoles[target_label]
                    body = lease_id.to_bytes(8, "little") + bytes((outcome,))
                    result = attacker.action(
                        p4rm.ACTION_PVP_RESOLVE,
                        outcome,
                        0,
                        directory_ids[attacker_label][target.name],
                        body,
                    )
                    self.assertEqual(
                        result,
                        (
                            p4rm.ACTION_OK,
                            p4rm.ACTION_PVP_RESOLVE,
                            outcome,
                            0,
                            lease_id,
                        ),
                    )

                def assert_knocked_out_target_is_denied(
                    winner_label: str,
                    loser_label: str,
                ) -> None:
                    winner = consoles[winner_label]
                    loser = consoles[loser_label]
                    before_fights = winner.pvp_fights
                    status, kind, _code, _value, lease_id = winner.action(
                        p4rm.ACTION_PVP_BEGIN,
                        0,
                        0,
                        directory_ids[winner_label][loser.name],
                    )
                    self.assertEqual(
                        (status, kind, lease_id),
                        (p4rm.ACTION_DENIED, p4rm.ACTION_PVP_BEGIN, 0),
                    )
                    self.assertEqual(winner.pvp_fights, before_fights)

                def receive_and_commit(label: str):
                    candidate = consoles[label]
                    event = candidate.receive_event()
                    self.assertEqual(event.kind, p4rm.ACTION_PVP_RESOLVE)
                    self.assertFalse(event.duplicate)
                    candidate.upload_local()
                    return event

                bind_all()
                for console in consoles.values():
                    console.connect()
                    self.assertEqual(console.hello(), 0)
                    console.create_local_character()
                    console.upload_local()
                    self.assertEqual(console.server_revision, 1)
                self.assertEqual(
                    len({console.actor_id for console in consoles.values()}),
                    4,
                )

                # Cache each console's complete three-player realm page. Every
                # action below must use the actor ID that source independently
                # learned from DIRECTORY_SUMMARY, never shared test memory.
                for label, console in consoles.items():
                    directory_ids[label] = console.request_directory()
                    self.assertEqual(
                        directory_ids[label],
                        {
                            other.name: other.actor_id
                            for other_label, other in consoles.items()
                            if other_label != label
                        },
                    )

                schedule = (
                    (("A", "B", 1), ("C", "D", 0)),
                    (("B", "C", 1), ("D", "A", 1)),
                    (("A", "C", 1), ("B", "D", 0)),
                    (("C", "A", 1), ("D", "B", 1)),
                )
                received_event_ids: list[int] = []
                offline_syncs = 0
                rollover_syncs = 0
                receipt_replays = 0
                authoritative_downloads = 0

                for day, duels in enumerate(schedule, start=1):
                    if day > 1:
                        clock[0] = 1_000.0 + (day - 1) * 3_600.0
                        restart_hub()
                        rollover_flags = connect_all()
                        for label, console in consoles.items():
                            self.assertTrue(
                                rollover_flags[label]
                                & p4rm.WELCOME_ACCEPT_LOCAL
                            )
                            self.assertTrue(
                                rollover_flags[label]
                                & p4rm.WELCOME_ROLLOVER_PENDING
                            )
                            self.assertEqual(console.player_day, day)
                            self.assertEqual(console.hit_points, 30)
                            self.assertEqual(console.pvp_fights, 3)
                            console.upload_local()
                            rollover_syncs += 1

                    # Every console plays an independent XP-only branch while
                    # detached, then reconnects and uploads that dirty branch.
                    for console in consoles.values():
                        console.leave()
                    for index, console in enumerate(
                        consoles.values(), start=1
                    ):
                        console.progress_offline(
                            coin_gain=0,
                            experience_gain=day * 10 + index,
                        )
                    for console in consoles.values():
                        console.connect()
                        flags = console.hello()
                        self.assertTrue(flags & p4rm.WELCOME_ACCEPT_LOCAL)
                        self.assertFalse(flags & p4rm.WELCOME_ROLLOVER_PENDING)
                        console.upload_local()
                        offline_syncs += 1

                    # Begin both disjoint fights before resolving either one,
                    # so two console sessions have outstanding multiplayer
                    # work at the same time.  The daily fight spend is synced
                    # before the durable outcome can be created.
                    leases: list[tuple[str, str, int, int]] = []
                    for attacker_label, target_label, outcome in duels:
                        lease_id = spend_fight_and_begin(
                            attacker_label, target_label
                        )
                        leases.append(
                            (attacker_label, target_label, outcome, lease_id)
                        )
                    for attacker_label, target_label, outcome, lease_id in leases:
                        resolve(
                            attacker_label, target_label, lease_id, outcome
                        )
                        winner_label, loser_label = (
                            (attacker_label, target_label)
                            if outcome == 1
                            else (target_label, attacker_label)
                        )
                        # Effective-alive includes admitted, uncommitted PvP
                        # events, so the loser cannot be attacked again even
                        # before either outcome reaches a cartridge.
                        assert_knocked_out_target_is_denied(
                            winner_label, loser_label
                        )

                    if day == 2:
                        # C ACKs its knockout but loses power before uploading
                        # the event cursor.  Receipt is not commitment; a new
                        # store and four new hub sessions must replay it.
                        console_c = consoles["C"]
                        before_receipt = console_c.snapshot_local_state()
                        first = console_c.receive_event()
                        applied_once = (
                            console_c.chompcoin,
                            console_c.hit_points,
                            console_c.pvp_losses,
                            console_c.last_realm_event_id,
                        )
                        with sqlite3.connect(database_path) as database:
                            receipt, committed = database.execute(
                                "SELECT receipt_at, committed_at FROM "
                                "realm_events WHERE event_id = ?",
                                (first.event_id,),
                            ).fetchone()
                        self.assertIsNotNone(receipt)
                        self.assertIsNone(committed)
                        console_c.restore_local_state(before_receipt)
                        restart_hub()
                        restarted_flags = connect_all()
                        for flags in restarted_flags.values():
                            self.assertTrue(flags & p4rm.WELCOME_ACCEPT_LOCAL)
                        replay = console_c.receive_event()
                        self.assertEqual(replay.event_id, first.event_id)
                        self.assertFalse(replay.duplicate)
                        self.assertEqual(
                            (
                                console_c.chompcoin,
                                console_c.hit_points,
                                console_c.pvp_losses,
                                console_c.last_realm_event_id,
                            ),
                            applied_once,
                        )
                        console_c.upload_local()
                        received_event_ids.append(replay.event_id)
                        receipt_replays += 1
                        for label in ("A", "B", "D"):
                            received_event_ids.append(
                                receive_and_commit(label).event_id
                            )
                    elif day == 3:
                        # B commits its source-loss event to the hub, then its
                        # local save reverts to the pre-event generation.  The
                        # next clean reconnect must download the newer head;
                        # it must not apply the zero-prize loss twice.
                        console_b = consoles["B"]
                        before_commit = console_b.snapshot_local_state()
                        event = console_b.receive_event()
                        console_b.upload_local()
                        canonical = console_b.snapshot_local_state()
                        received_event_ids.append(event.event_id)
                        console_b.restore_local_state(before_commit)
                        if console_b.hub is None:
                            raise AssertionError("B lost its hub session")
                        console_b.hub.transport_disconnected()
                        bind(console_b)
                        console_b.connect()
                        flags = console_b.hello()
                        self.assertTrue(flags & p4rm.WELCOME_HAS_SNAPSHOT)
                        self.assertEqual(
                            console_b.snapshot_local_state(), canonical
                        )
                        self.assertEqual(console_b.duplicate_events, 0)
                        authoritative_downloads += 1
                        for label in ("A", "C", "D"):
                            received_event_ids.append(
                                receive_and_commit(label).event_id
                            )
                    else:
                        for label in consoles:
                            received_event_ids.append(
                                receive_and_commit(label).event_id
                            )

                    self.assertEqual(len(received_event_ids), day * 4)
                    with sqlite3.connect(database_path) as database:
                        committed, total = database.execute(
                            "SELECT SUM(committed_at IS NOT NULL), COUNT(*) "
                            "FROM realm_events"
                        ).fetchone()
                    self.assertEqual((committed, total), (day * 4, day * 4))
                    for console in consoles.values():
                        console.leave()

                reopened = RealmStore(database_path, epoch_seconds=1_000)
                expected_final = {
                    "A": (400, 500, 1_104, 2, 2, 0, 13, 3),
                    "B": (400, 500, 1_108, 1, 3, 0, 15, 3),
                    "C": (600, 500, 1_112, 1, 3, 30, 14, 2),
                    "D": (1_800, 500, 1_116, 4, 0, 30, 16, 2),
                }
                decoded_heads = {}
                for label, console in consoles.items():
                    head = reopened.read_head(console.actor_id)
                    decoded = decode_lord_sync(
                        head.snapshot, expected_actor_id=console.actor_id
                    )
                    decoded_heads[console.actor_id] = decoded
                    self.assertEqual((head.revision, head.last_day_id), (14, 4))
                    self.assertEqual(console.server_revision, 14)
                    self.assertEqual(decoded.player.day, 4)
                    self.assertEqual(
                        (
                            decoded.player.chompcoin,
                            decoded.player.bank,
                            decoded.player.experience,
                            decoded.player.pvp_wins,
                            decoded.player.pvp_losses,
                            decoded.player.hit_points,
                            decoded.last_realm_event_id,
                            decoded.pvp_fights,
                        ),
                        expected_final[label],
                    )

                actor_labels = {
                    console.actor_id: label
                    for label, console in consoles.items()
                }
                expected_events = [
                    (1, "B", "A", 1, 400),
                    (2, "A", "B", 2, 400),
                    (3, "D", "C", 0, 0),
                    (4, "C", "D", 1, 0),
                    (5, "C", "B", 1, 400),
                    (6, "B", "C", 2, 400),
                    (7, "A", "D", 1, 600),
                    (8, "D", "A", 2, 600),
                    (9, "C", "A", 1, 200),
                    (10, "A", "C", 2, 200),
                    (11, "D", "B", 0, 0),
                    (12, "B", "D", 1, 0),
                    (13, "A", "C", 1, 400),
                    (14, "C", "A", 2, 400),
                    (15, "B", "D", 1, 400),
                    (16, "D", "B", 2, 400),
                ]
                with sqlite3.connect(database_path) as database:
                    event_rows = database.execute(
                        "SELECT event_id, target_actor_id, source_actor_id, "
                        "kind, code, value, receipt_at, acknowledged_at, "
                        "committed_at "
                        "FROM realm_events ORDER BY event_id"
                    ).fetchall()
                    actual_events = [
                        (
                            int(row[0]),
                            actor_labels[bytes(row[1])],
                            actor_labels[bytes(row[2])],
                            int(row[4]),
                            int(row[5]),
                        )
                        for row in event_rows
                    ]
                    self.assertEqual(actual_events, expected_events)
                    self.assertTrue(
                        all(
                            int(row[3]) == p4rm.ACTION_PVP_RESOLVE
                            for row in event_rows
                        )
                    )
                    self.assertTrue(
                        all(
                            row[6] is not None
                            and row[7] is None
                            and row[8] is not None
                            for row in event_rows
                        )
                    )
                    lease_days = database.execute(
                        "SELECT realm_day_id, status, admitted, COUNT(*) "
                        "FROM pvp_leases GROUP BY realm_day_id, status, admitted "
                        "ORDER BY realm_day_id, status, admitted"
                    ).fetchall()
                    economy = database.execute(
                        "SELECT SUM(applied_revision IS NOT NULL), COUNT(*) "
                        "FROM realm_economy_events"
                    ).fetchone()
                    economy_rows = database.execute(
                        "SELECT event_id, actor_id, chompcoin_delta, "
                        "bank_delta, applied_revision "
                        "FROM realm_economy_events ORDER BY event_id"
                    ).fetchall()
                    action_statuses = database.execute(
                        "SELECT status, COUNT(*) FROM action_operations "
                        "WHERE admitted = 1 GROUP BY status ORDER BY status"
                    ).fetchall()
                    total_coin = database.execute(
                        "SELECT SUM(chompcoin) FROM profiles"
                    ).fetchone()[0]
                    alive = database.execute(
                        "SELECT actor_id FROM profiles WHERE flags & 1 "
                        "ORDER BY actor_id"
                    ).fetchall()

                self.assertEqual(
                    lease_days,
                    [(1, 1, 1, 2), (2, 1, 1, 2), (3, 1, 1, 2), (4, 1, 1, 2)],
                )
                self.assertEqual(economy, (14, 14))
                self.assertEqual(
                    [
                        (
                            int(row[0]),
                            actor_labels[bytes(row[1])],
                            int(row[2]),
                            int(row[3]),
                            int(row[4]),
                        )
                        for row in economy_rows
                    ],
                    [
                        (1, "B", -400, 0, 3),
                        (2, "A", 400, 0, 4),
                        (4, "C", 0, 0, 4),
                        (5, "C", -400, 0, 7),
                        (6, "B", 400, 0, 7),
                        (7, "A", -600, 0, 7),
                        (8, "D", 600, 0, 7),
                        (9, "C", -200, 0, 10),
                        (10, "A", 200, 0, 11),
                        (12, "B", 0, 0, 11),
                        (13, "A", -400, 0, 14),
                        (14, "C", 400, 0, 14),
                        (15, "B", -400, 0, 14),
                        (16, "D", 400, 0, 14),
                    ],
                )
                self.assertEqual(
                    action_statuses,
                    [(p4rm.ACTION_OK, 16), (p4rm.ACTION_DENIED, 8)],
                )
                self.assertEqual(total_coin, 3_200)
                self.assertEqual(
                    {actor_labels[bytes(row[0])] for row in alive},
                    {"C", "D"},
                )
                profiles = reopened.list_profiles(
                    exclude_actor_id=bytes(16)
                )
                self.assertEqual(len(profiles), 4)
                for profile in profiles:
                    decoded = decoded_heads[profile.actor_id]
                    self.assertEqual(
                        (
                            profile.chompcoin,
                            profile.bank,
                            profile.experience,
                            profile.pvp_wins,
                            profile.pvp_losses,
                            profile.hit_points,
                            profile.max_hit_points,
                            profile.flags & 1,
                        ),
                        (
                            decoded.player.chompcoin,
                            decoded.player.bank,
                            decoded.player.experience,
                            decoded.player.pvp_wins,
                            decoded.player.pvp_losses,
                            decoded.player.hit_points,
                            decoded.player.max_hit_points,
                            int(decoded.player.hit_points > 0),
                        ),
                    )
                self.assertEqual(sum(c.pvp_wins for c in consoles.values()), 8)
                self.assertEqual(sum(c.pvp_losses for c in consoles.values()), 8)
                self.assertEqual(set(received_event_ids), set(range(1, 17)))

                print(
                    "four-console LORD campaign PASS: days=4 players=4 "
                    "offline_syncs=16 rollover_syncs=12 duels=8 "
                    "denied_KO_attacks=8 events=16/16 economy=14/14 "
                    f"hub_restarts={hub_restarts} receipt_replays="
                    f"{receipt_replays} downloads={authoritative_downloads} "
                    "heads=4x:r14 ChompCoin=3200"
                )


if __name__ == "__main__":
    unittest.main()
