"""Ten-console, twelve-day LORD campaign with recoverable sync faults.

This is the encoded P4MP/P4RM acceptance layer for the backend-hosted realm.
Ten independent console models keep separate local saves and communicate only
through their bound ``RealmHubSession`` objects.  A deterministic round-robin
campaign supplies an independent state/event/economy oracle while deliberate
transport, power, conflict, and broken-client faults prove that recovery never
duplicates a duel, a snapshot revision, or ChompCoin.
"""

from __future__ import annotations

import dataclasses
import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.p4_realm_hub import p4rm
from tools.p4_realm_hub.hub import RealmHubSession
from tools.p4_realm_hub.lord_snapshot import (
    MAX_CURRENCY,
    MAX_WEALTH_GAIN_FLAT,
    decode_lord_sync,
)
from tools.p4_realm_hub.store import RealmStore
from tools.p4_realm_hub.tests.test_two_client_e2e import (
    LocalPersistentState,
    ProtocolConsole,
    ReceivedEvent,
    realm_offer,
)


@dataclasses.dataclass
class PlayerOracle:
    name: str
    actor_id: bytes = bytes(16)
    chompcoin: int = 800
    bank: int = 500
    experience: int = 1_000
    hit_points: int = 30
    max_hit_points: int = 30
    player_day: int = 1
    pvp_fights: int = 3
    pvp_wins: int = 0
    pvp_losses: int = 0
    save_sequence: int = 2
    revision: int = 1
    last_event_id: int = 0
    attacks: int = 0
    targets: int = 0


@dataclasses.dataclass(frozen=True)
class EventOracle:
    event_id: int
    target_label: str
    source_label: str
    code: int
    value: int


@dataclasses.dataclass(frozen=True)
class LedgerOracle:
    event_id: int
    actor_label: str
    chompcoin_delta: int
    bank_delta: int = 0


def tournament_rounds(
    labels: tuple[str, ...],
) -> tuple[tuple[tuple[str, str], ...], ...]:
    """Return the nine rounds of a ten-player circle tournament."""
    rotation = list(labels)
    rounds: list[tuple[tuple[str, str], ...]] = []
    for _round in range(len(labels) - 1):
        rounds.append(
            tuple(
                (rotation[index], rotation[-1 - index])
                for index in range(len(labels) // 2)
            )
        )
        rotation = [rotation[0], rotation[-1], *rotation[1:-1]]
    return tuple(rounds)


class TenPlayerChaosTests(unittest.TestCase):
    def test_ten_players_recover_across_twelve_realm_days(self) -> None:
        labels = tuple(f"P{index}" for index in range(10))
        names = {label: f"Player {index:02d}" for index, label in enumerate(labels)}
        rounds = tournament_rounds(labels)
        first_nine_pairs = {
            frozenset(pair)
            for round_pairs in rounds
            for pair in round_pairs
        }
        self.assertEqual(len(first_nine_pairs), 45)

        with tempfile.TemporaryDirectory() as directory:
            database_path = Path(directory) / "lord-ten-player-chaos.sqlite3"
            clock = [1_000.0]
            with mock.patch(
                "tools.p4_realm_hub.store.time.time",
                side_effect=lambda: clock[0],
            ):
                store = RealmStore(database_path, epoch_seconds=1_000)
                offer = realm_offer(0x5152535455565758)
                consoles = {
                    label: ProtocolConsole(
                        f"chaos-{label.lower()}", names[label], 0xA000 + index
                    )
                    for index, label in enumerate(labels)
                }
                oracle = {
                    label: PlayerOracle(names[label]) for label in labels
                }
                for console in consoles.values():
                    console.chompcoin = 800
                    console.bank = 500
                    console.experience = 1_000
                    console.hit_points = 30
                    console.max_hit_points = 30
                    console.pvp_wins = 0
                    console.pvp_losses = 0

                session_number = 0
                hub_restarts = 0
                event_receipt_replays = 0
                canonical_downloads = 0
                corrupt_uploads = 0
                revision_conflicts = 0
                rejected_forged_outcomes = 0
                exact_nonce_retries = 0
                stale_nonce_replays = 0
                directory_ids: dict[str, dict[str, bytes]] = {}

                def bind(label: str) -> None:
                    nonlocal session_number
                    session_number += 1
                    console = consoles[label]
                    console.bind(
                        RealmHubSession(
                            console.profile,
                            store,
                            console.receive_from_hub,
                            offer,
                            session_id=0xE000 + session_number,
                            now=lambda: clock[0],
                        )
                    )

                def bind_all() -> None:
                    for label in labels:
                        bind(label)

                def restart_all() -> None:
                    nonlocal store, hub_restarts
                    for console in consoles.values():
                        if console.hub is not None:
                            console.hub.transport_disconnected()
                    store = RealmStore(database_path, epoch_seconds=1_000)
                    bind_all()
                    hub_restarts += 1

                def reconnect(label: str) -> int:
                    console = consoles[label]
                    if console.hub is not None:
                        console.hub.transport_disconnected()
                    bind(label)
                    console.connect()
                    return console.hello()

                def assert_console_matches(label: str) -> None:
                    console = consoles[label]
                    expected = oracle[label]
                    self.assertEqual(console.actor_id, expected.actor_id)
                    self.assertEqual(console.server_revision, expected.revision)
                    self.assertEqual(
                        (
                            console.chompcoin,
                            console.bank,
                            console.experience,
                            console.hit_points,
                            console.max_hit_points,
                            console.player_day,
                            console.pvp_fights,
                            console.pvp_wins,
                            console.pvp_losses,
                            console.save_sequence,
                            console.committed_save_sequence,
                            console.last_realm_event_id,
                        ),
                        (
                            expected.chompcoin,
                            expected.bank,
                            expected.experience,
                            expected.hit_points,
                            expected.max_hit_points,
                            expected.player_day,
                            expected.pvp_fights,
                            expected.pvp_wins,
                            expected.pvp_losses,
                            expected.save_sequence,
                            expected.save_sequence,
                            expected.last_event_id,
                        ),
                    )

                def assert_event(
                    actual: ReceivedEvent, expected: EventOracle
                ) -> None:
                    self.assertEqual(
                        (
                            actual.event_id,
                            actual.kind,
                            actual.code,
                            actual.value,
                            actual.source_actor_id,
                            actual.source_name,
                            actual.body,
                            actual.duplicate,
                        ),
                        (
                            expected.event_id,
                            p4rm.ACTION_PVP_RESOLVE,
                            expected.code,
                            expected.value,
                            oracle[expected.source_label].actor_id,
                            oracle[expected.source_label].name,
                            b"",
                            False,
                        ),
                    )

                def apply_event(label: str, expected: EventOracle) -> None:
                    state = oracle[label]
                    if expected.code == 1:
                        state.chompcoin -= expected.value
                        state.hit_points = 0
                        state.pvp_losses += 1
                    elif expected.code == 2:
                        state.chompcoin += expected.value
                        state.pvp_wins += 1
                    elif expected.code == 0:
                        state.pvp_wins += 1
                    else:
                        raise AssertionError("unexpected PvP event code")
                    state.last_event_id = expected.event_id
                    state.save_sequence += 1

                bind_all()
                for label in labels:
                    console = consoles[label]
                    console.connect()
                    self.assertEqual(console.hello(), 0)
                    console.create_local_character()
                    console.upload_local()
                    oracle[label].actor_id = console.actor_id
                    assert_console_matches(label)
                self.assertEqual(
                    len({state.actor_id for state in oracle.values()}), 10
                )

                # Ten players force the real two-page directory path: every
                # console independently learns eight peers at offset zero and
                # the ninth at offset eight.
                for label in labels:
                    directory_ids[label] = consoles[label].request_directory()
                    self.assertEqual(
                        directory_ids[label],
                        {
                            oracle[other].name: oracle[other].actor_id
                            for other in labels
                            if other != label
                        },
                    )

                expected_events: list[EventOracle] = []
                expected_ledgers: list[LedgerOracle] = []
                expected_leases: list[
                    tuple[int, str, str, int, int]
                ] = []
                next_event_id = 1
                next_lease_id = 1
                lcg_state = 59
                attacker_wins = 0
                attacker_losses = 0
                old_replay_state: LocalPersistentState | None = None
                old_replay_nonce: int | None = None
                old_replay_revision: int | None = None

                for day in range(1, 13):
                    if day > 1:
                        clock[0] = 1_000.0 + (day - 1) * 3_600.0
                        restart_all()
                        for label in labels:
                            console = consoles[label]
                            console.connect()
                            flags = console.hello()
                            self.assertTrue(flags & p4rm.WELCOME_ACCEPT_LOCAL)
                            self.assertTrue(
                                flags & p4rm.WELCOME_ROLLOVER_PENDING
                            )
                            state = oracle[label]
                            state.player_day += 1
                            state.hit_points = state.max_hit_points
                            state.pvp_fights = 3
                            state.save_sequence += 1
                            console.upload_local()
                            state.revision += 1
                            assert_console_matches(label)

                    # Every player creates and then syncs one independent
                    # offline branch per realm day.
                    for console in consoles.values():
                        console.leave()
                    for index, label in enumerate(labels):
                        gain = day * 10 + index + 1
                        consoles[label].progress_offline(
                            coin_gain=0, experience_gain=gain
                        )
                        oracle[label].experience += gain
                        oracle[label].save_sequence += 1

                    for label in labels:
                        console = consoles[label]
                        console.connect()
                        flags = console.hello()
                        self.assertTrue(flags & p4rm.WELCOME_ACCEPT_LOCAL)
                        self.assertFalse(
                            flags & p4rm.WELCOME_ROLLOVER_PENDING
                        )

                        if day == 2 and label == "P0":
                            # Lose the first result, then repeat the exact
                            # frozen operation.  The head advances once.
                            old_replay_state = console.snapshot_local_state()
                            old_replay_nonce = 0xD2000001
                            first = console.upload_local_result(
                                nonce=old_replay_nonce, lose_result=True
                            )
                            self.assertEqual(first.status, p4rm.COMMIT_OK)
                            self.assertFalse(first.result_received)
                            oracle[label].revision += 1
                            old_replay_revision = first.revision
                            self.assertEqual(
                                store.read_head(console.actor_id).revision,
                                oracle[label].revision,
                            )
                            retry = console.upload_local_result(
                                nonce=old_replay_nonce
                            )
                            self.assertEqual(
                                (retry.status, retry.revision),
                                (p4rm.COMMIT_OK, first.revision),
                            )
                            self.assertEqual(retry.record, first.record)
                            exact_nonce_retries += 1
                        elif day == 3 and label == "P0":
                            # A broken client replays that exact old operation
                            # while a newer dirty branch is awaiting its first
                            # commit.  Store idempotency may report the old OK,
                            # but the hub must not open the dirty-first barrier.
                            self.assertIsNotNone(old_replay_state)
                            self.assertIsNotNone(old_replay_nonce)
                            self.assertIsNotNone(old_replay_revision)
                            current_dirty = console.snapshot_local_state()
                            console.restore_local_state(old_replay_state)
                            stale = console.upload_local_result(
                                nonce=old_replay_nonce
                            )
                            self.assertEqual(
                                (stale.status, stale.revision),
                                (p4rm.COMMIT_OK, old_replay_revision),
                            )
                            console.restore_local_state(current_dirty)
                            self.assertIsNotNone(console.hub)
                            self.assertTrue(console.hub.awaiting_local_commit)
                            blocked_nonce = 0xD3000001
                            blocked_transaction = console._transaction()
                            console._send_realm(
                                p4rm.ACTION_BEGIN,
                                blocked_transaction,
                                p4rm.encode_action_begin(
                                    p4rm.ACTION_PVP_BEGIN,
                                    0,
                                    0,
                                    directory_ids[label][oracle["P1"].name],
                                    blocked_nonce,
                                    b"",
                                ),
                                chunk_index=p4rm.BEGIN_INDEX,
                                chunk_count=0,
                            )
                            blocked = p4rm.decode_action_result(
                                console._take_realm(
                                    p4rm.ACTION_RESULT,
                                    blocked_transaction,
                                ).payload
                            )
                            self.assertEqual(
                                blocked,
                                (
                                    p4rm.ACTION_BUSY,
                                    p4rm.ACTION_PVP_BEGIN,
                                    0,
                                    0,
                                    0,
                                ),
                            )
                            stale_nonce_replays += 1
                            console.upload_local()
                            oracle[label].revision += 1
                        elif day == 4 and label == "P1":
                            head_revision = store.read_head(
                                console.actor_id
                            ).revision
                            damaged = console.upload_local_result(
                                corrupt_chunk_index=0
                            )
                            self.assertEqual(
                                damaged.status, p4rm.COMMIT_INVALID
                            )
                            self.assertEqual(
                                store.read_head(console.actor_id).revision,
                                head_revision,
                            )
                            corrupt_uploads += 1
                            recovery_flags = reconnect(label)
                            self.assertTrue(
                                recovery_flags & p4rm.WELCOME_ACCEPT_LOCAL
                            )
                            console.upload_local()
                            oracle[label].revision += 1
                        elif day == 7 and label == "P2":
                            before_revision = oracle[label].revision
                            lost = console.upload_local_result(
                                lose_result=True
                            )
                            self.assertEqual(
                                (lost.status, lost.result_received),
                                (p4rm.COMMIT_OK, False),
                            )
                            oracle[label].revision += 1
                            self.assertEqual(
                                store.read_head(console.actor_id).revision,
                                before_revision + 1,
                            )
                            recovery_flags = reconnect(label)
                            self.assertTrue(
                                recovery_flags & p4rm.WELCOME_HAS_SNAPSHOT
                            )
                            canonical_downloads += 1
                        elif day == 8 and label == "P3":
                            head_revision = store.read_head(
                                console.actor_id
                            ).revision
                            conflicted = console.upload_local_result(
                                expected_revision=head_revision - 1
                            )
                            self.assertEqual(
                                (conflicted.status, conflicted.revision),
                                (p4rm.COMMIT_CONFLICT, head_revision),
                            )
                            self.assertEqual(
                                store.read_head(console.actor_id).revision,
                                head_revision,
                            )
                            revision_conflicts += 1
                            recovery_flags = reconnect(label)
                            self.assertTrue(
                                recovery_flags & p4rm.WELCOME_ACCEPT_LOCAL
                            )
                            console.upload_local()
                            oracle[label].revision += 1
                        else:
                            console.upload_local()
                            oracle[label].revision += 1
                        assert_console_matches(label)

                    round_pairs = rounds[(day - 1) % len(rounds)]
                    pending_resolutions: list[
                        tuple[str, str, int, int, int]
                    ] = []
                    for left, right in round_pairs:
                        attacker, target = (
                            (left, right) if (day - 1) % 2 == 0 else (right, left)
                        )
                        attacker_console = consoles[attacker]
                        action_result = attacker_console.action(
                            p4rm.ACTION_PVP_BEGIN,
                            0,
                            0,
                            directory_ids[attacker][oracle[target].name],
                        )
                        self.assertEqual(
                            action_result[:4],
                            (p4rm.ACTION_OK, p4rm.ACTION_PVP_BEGIN, 0, 0),
                        )
                        lease_id = action_result[4]
                        self.assertEqual(lease_id, next_lease_id)
                        attacker_console.pvp_fights -= 1
                        attacker_console.save_sequence += 1
                        oracle[attacker].pvp_fights -= 1
                        oracle[attacker].save_sequence += 1
                        oracle[attacker].attacks += 1
                        oracle[target].targets += 1
                        attacker_console.upload_local()
                        oracle[attacker].revision += 1
                        assert_console_matches(attacker)

                        lcg_state = (
                            1_664_525 * lcg_state + 1_013_904_223
                        ) & 0xFFFFFFFF
                        outcome = (lcg_state >> 31) & 1
                        pending_resolutions.append(
                            (attacker, target, outcome, lease_id, day)
                        )
                        expected_leases.append(
                            (lease_id, attacker, target, day, outcome)
                        )
                        next_lease_id += 1

                    day_events: dict[str, EventOracle] = {}
                    day_losers: list[str] = []
                    for attacker, target, outcome, lease_id, _lease_day in (
                        pending_resolutions
                    ):
                        attacker_state = oracle[attacker]
                        target_state = oracle[target]
                        prize = 0
                        if outcome == 1:
                            prize = min(
                                target_state.chompcoin // 2,
                                MAX_CURRENCY - attacker_state.chompcoin,
                                MAX_WEALTH_GAIN_FLAT,
                            )
                            attacker_wins += 1
                            target_event = EventOracle(
                                next_event_id, target, attacker, 1, prize
                            )
                            source_event = EventOracle(
                                next_event_id + 1,
                                attacker,
                                target,
                                2,
                                prize,
                            )
                            expected_ledgers.extend(
                                (
                                    LedgerOracle(
                                        target_event.event_id, target, -prize
                                    ),
                                    LedgerOracle(
                                        source_event.event_id, attacker, prize
                                    ),
                                )
                            )
                            day_losers.append(target)
                        else:
                            attacker_losses += 1
                            target_event = EventOracle(
                                next_event_id, target, attacker, 0, 0
                            )
                            source_event = EventOracle(
                                next_event_id + 1,
                                attacker,
                                target,
                                1,
                                0,
                            )
                            expected_ledgers.append(
                                LedgerOracle(source_event.event_id, attacker, 0)
                            )
                            day_losers.append(attacker)
                        result = consoles[attacker].action(
                            p4rm.ACTION_PVP_RESOLVE,
                            outcome,
                            0,
                            directory_ids[attacker][target_state.name],
                            lease_id.to_bytes(8, "little")
                            + bytes((outcome,)),
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
                        expected_events.extend((target_event, source_event))
                        day_events[target] = target_event
                        day_events[attacker] = source_event
                        next_event_id += 2

                    self.assertEqual(len(day_events), 10)
                    self.assertEqual(len(set(day_losers)), 5)

                    if day == 3:
                        # All ten outcome events and five KOs are durable, but
                        # no cartridge has received one yet.  Reopening both
                        # store and sessions must preserve that projection.
                        with sqlite3.connect(database_path) as database:
                            alive_before = database.execute(
                                "SELECT COUNT(*) FROM profiles WHERE flags & 1"
                            ).fetchone()[0]
                        self.assertEqual(alive_before, 5)
                        restart_all()
                        for label in labels:
                            consoles[label].connect()
                            flags = consoles[label].hello()
                            self.assertTrue(
                                flags & p4rm.WELCOME_ACCEPT_LOCAL
                            )
                        with sqlite3.connect(database_path) as database:
                            alive_after = database.execute(
                                "SELECT COUNT(*) FROM profiles WHERE flags & 1"
                            ).fetchone()[0]
                        self.assertEqual(alive_after, 5)

                    crash_label = "P0" if day == 5 else None
                    forged_label = day_losers[0] if day == 9 else None
                    for label in labels:
                        console = consoles[label]
                        expected = day_events[label]
                        if label == crash_label:
                            before = console.snapshot_local_state()
                            first = console.receive_event()
                            assert_event(first, expected)
                            honest = console.snapshot_local_state()
                            with sqlite3.connect(database_path) as database:
                                receipt, committed = database.execute(
                                    "SELECT receipt_at, committed_at FROM "
                                    "realm_events WHERE event_id = ?",
                                    (first.event_id,),
                                ).fetchone()
                            self.assertIsNotNone(receipt)
                            self.assertIsNone(committed)
                            console.restore_local_state(before)
                            recovery_flags = reconnect(label)
                            self.assertTrue(
                                recovery_flags & p4rm.WELCOME_ACCEPT_LOCAL
                            )
                            replay = console.receive_event()
                            assert_event(replay, expected)
                            self.assertEqual(
                                console.snapshot_local_state(), honest
                            )
                            apply_event(label, expected)
                            console.upload_local()
                            oracle[label].revision += 1
                            event_receipt_replays += 1
                        elif label == forged_label:
                            before = console.snapshot_local_state()
                            received = console.receive_event()
                            assert_event(received, expected)
                            honest = console.snapshot_local_state()
                            self.assertEqual(expected.code, 1)
                            apply_event(label, expected)

                            # Reflect the exact debit/cursor but omit the KO
                            # and lifetime loss.  This was the concrete bypass
                            # that once resurrected a defeated profile.
                            console.hit_points = before.hit_points
                            console.pvp_losses = before.pvp_losses
                            head_revision = store.read_head(
                                console.actor_id
                            ).revision
                            forged = console.upload_local_result()
                            self.assertEqual(
                                forged.status, p4rm.COMMIT_INVALID
                            )
                            self.assertEqual(
                                store.read_head(console.actor_id).revision,
                                head_revision,
                            )
                            with sqlite3.connect(database_path) as database:
                                alive = database.execute(
                                    "SELECT flags & 1 FROM profiles "
                                    "WHERE actor_id = ?",
                                    (console.actor_id,),
                                ).fetchone()[0]
                            self.assertEqual(alive, 0)
                            console.restore_local_state(honest)
                            recovery_flags = reconnect(label)
                            self.assertTrue(
                                recovery_flags & p4rm.WELCOME_ACCEPT_LOCAL
                            )
                            console.upload_local()
                            oracle[label].revision += 1
                            rejected_forged_outcomes += 1
                        else:
                            received = console.receive_event()
                            assert_event(received, expected)
                            apply_event(label, expected)
                            console.upload_local()
                            oracle[label].revision += 1
                        assert_console_matches(label)

                    with sqlite3.connect(database_path) as database:
                        committed, total = database.execute(
                            "SELECT SUM(committed_at IS NOT NULL), COUNT(*) "
                            "FROM realm_events"
                        ).fetchone()
                    self.assertEqual((committed, total), (day * 10, day * 10))
                    for console in consoles.values():
                        console.leave()

                self.assertEqual((attacker_wins, attacker_losses), (31, 29))
                self.assertEqual(len(expected_events), 120)
                self.assertEqual(len(expected_ledgers), 91)
                self.assertEqual(len(expected_leases), 60)
                self.assertEqual(next_event_id, 121)
                self.assertEqual(next_lease_id, 61)

                reopened = RealmStore(database_path, epoch_seconds=1_000)
                actor_labels = {
                    state.actor_id: label for label, state in oracle.items()
                }
                head_revisions: dict[bytes, int] = {}
                for label in labels:
                    expected = oracle[label]
                    self.assertEqual(
                        expected.revision, 36 + expected.attacks
                    )
                    self.assertEqual(expected.save_sequence, expected.revision + 1)
                    self.assertTrue(5 <= expected.attacks <= 7)
                    self.assertEqual(expected.attacks + expected.targets, 12)
                    head = reopened.read_head(expected.actor_id)
                    self.assertEqual(
                        (head.revision, head.last_day_id),
                        (expected.revision, 12),
                    )
                    decoded = decode_lord_sync(
                        head.snapshot, expected_actor_id=expected.actor_id
                    )
                    self.assertEqual(
                        (
                            decoded.save_sequence,
                            decoded.player.name,
                            decoded.player.chompcoin,
                            decoded.player.bank,
                            decoded.player.experience,
                            decoded.player.hit_points,
                            decoded.player.max_hit_points,
                            decoded.player.day,
                            decoded.pvp_fights,
                            decoded.player.pvp_wins,
                            decoded.player.pvp_losses,
                            decoded.last_realm_event_id,
                        ),
                        (
                            expected.save_sequence,
                            expected.name,
                            expected.chompcoin,
                            expected.bank,
                            expected.experience,
                            expected.hit_points,
                            expected.max_hit_points,
                            expected.player_day,
                            expected.pvp_fights,
                            expected.pvp_wins,
                            expected.pvp_losses,
                            expected.last_event_id,
                        ),
                    )
                    head_revisions[expected.actor_id] = expected.revision

                with sqlite3.connect(database_path) as database:
                    self.assertEqual(
                        database.execute("PRAGMA quick_check").fetchall(),
                        [("ok",)],
                    )
                    self.assertEqual(
                        database.execute("PRAGMA foreign_key_check").fetchall(),
                        [],
                    )
                    event_rows = database.execute(
                        "SELECT event_id, target_actor_id, source_actor_id, "
                        "kind, code, value, receipt_at, committed_at, admitted "
                        "FROM realm_events ORDER BY event_id"
                    ).fetchall()
                    ledger_rows = database.execute(
                        "SELECT event_id, actor_id, chompcoin_delta, "
                        "bank_delta, applied_revision "
                        "FROM realm_economy_events ORDER BY event_id"
                    ).fetchall()
                    lease_rows = database.execute(
                        "SELECT lease_id, source_actor_id, target_actor_id, "
                        "realm_day_id, status, outcome, admitted "
                        "FROM pvp_leases ORDER BY lease_id"
                    ).fetchall()
                    profile_rows = database.execute(
                        "SELECT actor_id, name, flags, hit_points, "
                        "max_hit_points, experience, chompcoin, bank, "
                        "pvp_wins, pvp_losses FROM profiles"
                    ).fetchall()
                    action_rows = database.execute(
                        "SELECT status, admitted FROM action_operations"
                    ).fetchall()
                    operation_rows = database.execute(
                        "SELECT actor_id, revision FROM operations "
                        "ORDER BY actor_id, revision"
                    ).fetchall()
                    snapshot_rows = database.execute(
                        "SELECT actor_id, revision FROM events "
                        "WHERE kind = 'snapshot' ORDER BY actor_id, revision"
                    ).fetchall()
                    anchor_rows = database.execute(
                        "SELECT actor_id, realm_day_id, base_last_day_id "
                        "FROM realm_day_anchors ORDER BY actor_id"
                    ).fetchall()

                self.assertEqual(
                    [
                        (
                            int(row[0]),
                            actor_labels[bytes(row[1])],
                            actor_labels[bytes(row[2])],
                            int(row[3]),
                            int(row[4]),
                            int(row[5]),
                        )
                        for row in event_rows
                    ],
                    [
                        (
                            event.event_id,
                            event.target_label,
                            event.source_label,
                            p4rm.ACTION_PVP_RESOLVE,
                            event.code,
                            event.value,
                        )
                        for event in expected_events
                    ],
                )
                self.assertTrue(
                    all(
                        row[6] is not None
                        and row[7] is not None
                        and int(row[8]) == 1
                        for row in event_rows
                    )
                )
                self.assertEqual(
                    [
                        (
                            int(row[0]),
                            actor_labels[bytes(row[1])],
                            int(row[2]),
                            int(row[3]),
                        )
                        for row in ledger_rows
                    ],
                    [
                        (
                            ledger.event_id,
                            ledger.actor_label,
                            ledger.chompcoin_delta,
                            ledger.bank_delta,
                        )
                        for ledger in expected_ledgers
                    ],
                )
                self.assertTrue(
                    all(
                        row[4] is not None
                        and 1 <= int(row[4]) <= head_revisions[bytes(row[1])]
                        for row in ledger_rows
                    )
                )
                self.assertEqual(
                    [
                        (
                            int(row[0]),
                            actor_labels[bytes(row[1])],
                            actor_labels[bytes(row[2])],
                            int(row[3]),
                            int(row[4]),
                            int(row[5]),
                            int(row[6]),
                        )
                        for row in lease_rows
                    ],
                    [
                        (
                            lease_id,
                            attacker,
                            target,
                            day,
                            1,
                            outcome,
                            1,
                        )
                        for lease_id, attacker, target, day, outcome in expected_leases
                    ],
                )
                self.assertEqual(
                    [
                        (
                            actor_labels[bytes(row[0])],
                            str(row[1]),
                            int(row[2]) & 1,
                            int(row[3]),
                            int(row[4]),
                            int(row[5]),
                            int(row[6]),
                            int(row[7]),
                            int(row[8]),
                            int(row[9]),
                        )
                        for row in sorted(
                            profile_rows,
                            key=lambda row: actor_labels[bytes(row[0])],
                        )
                    ],
                    [
                        (
                            label,
                            oracle[label].name,
                            int(oracle[label].hit_points > 0),
                            oracle[label].hit_points,
                            oracle[label].max_hit_points,
                            oracle[label].experience,
                            oracle[label].chompcoin,
                            oracle[label].bank,
                            oracle[label].pvp_wins,
                            oracle[label].pvp_losses,
                        )
                        for label in labels
                    ],
                )
                self.assertEqual(len(action_rows), 120)
                self.assertTrue(
                    all(
                        int(status) == p4rm.ACTION_OK and int(admitted) == 1
                        for status, admitted in action_rows
                    )
                )
                expected_revision_rows = sorted(
                    (
                        state.actor_id,
                        revision,
                    )
                    for state in oracle.values()
                    for revision in range(1, state.revision + 1)
                )
                self.assertEqual(
                    [(bytes(row[0]), int(row[1])) for row in operation_rows],
                    expected_revision_rows,
                )
                self.assertEqual(
                    [(bytes(row[0]), int(row[1])) for row in snapshot_rows],
                    expected_revision_rows,
                )
                self.assertEqual(
                    sorted(
                        (
                            actor_labels[bytes(row[0])],
                            int(row[1]),
                            int(row[2]),
                        )
                        for row in anchor_rows
                    ),
                    [(label, 12, 11) for label in labels],
                )
                self.assertEqual(
                    sum(state.chompcoin for state in oracle.values()), 8_000
                )
                self.assertEqual(
                    sum(state.pvp_wins for state in oracle.values()), 60
                )
                self.assertEqual(
                    sum(state.pvp_losses for state in oracle.values()), 60
                )
                self.assertEqual(
                    sum(state.hit_points > 0 for state in oracle.values()), 5
                )
                self.assertEqual(
                    {event.target_label for event in expected_events}, set(labels)
                )
                for label in labels:
                    self.assertEqual(
                        sum(event.target_label == label for event in expected_events),
                        12,
                    )
                    self.assertEqual(
                        sum(event.source_label == label for event in expected_events),
                        12,
                    )

                print(
                    "ten-console LORD chaos PASS: players=10 days=12 "
                    "duels=60 events=120/120 economy=91/91 "
                    "ChompCoin=8000 wins=60 losses=60 "
                    f"hub_restarts={hub_restarts} exact_nonce_retries="
                    f"{exact_nonce_retries} stale_nonce_replays="
                    f"{stale_nonce_replays} receipt_replays="
                    f"{event_receipt_replays} downloads={canonical_downloads} "
                    f"corrupt_uploads={corrupt_uploads} conflicts="
                    f"{revision_conflicts} forged_outcomes_rejected="
                    f"{rejected_forged_outcomes}"
                )


if __name__ == "__main__":
    unittest.main()
