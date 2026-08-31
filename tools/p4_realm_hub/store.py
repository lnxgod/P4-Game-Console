"""SQLite persistence and hourly realm clock for the Mac hub."""

from __future__ import annotations

import dataclasses
import hashlib
import sqlite3
import threading
import time
import zlib
from pathlib import Path

from tools.p4_realm_hub import p4rm
from tools.p4_realm_hub.lord_snapshot import (
    MAX_CURRENCY,
    MAX_WEALTH_GAIN_FLAT,
    decode_lord_sync,
    profile_projection,
    validate_lord_transition,
)


MAX_REALM_PLAYERS = 100
MAX_PVP_FIGHTS_PER_REALM_DAY = 3
MAX_FRIENDSHIP_ACTIONS_PER_REALM_DAY = 3
GUILD_SEASON_DAYS = 24
GUILD_REJOIN_COOLDOWN_DAYS = 1
GUILD_CLASH_PAIR_COOLDOWN_DAYS = 3
GUILD_CLASH_PARTICIPATION_POINTS = 12
GUILD_CLASH_ROUTE_BONUS = 4
GUILD_CLASH_ROLL_SPAN = 7


class RealmFullError(RuntimeError):
    pass


@dataclasses.dataclass(frozen=True)
class RealmHead:
    actor_id: bytes
    revision: int
    last_day_id: int
    snapshot: bytes | None


@dataclasses.dataclass(frozen=True)
class RealmProfile:
    actor_id: bytes
    name: str
    hero_style: int
    hero_class: int
    level: int
    flags: int
    hit_points: int
    max_hit_points: int
    strength: int
    defense: int
    experience: int
    chompcoin: int
    pvp_wins: int
    pvp_losses: int
    dragon_kills: int
    online: bool
    bank: int = 0
    trust: int = 0
    teamed: bool = False
    guild_id: int = 0
    guild_name_code: int = p4rm.GUILD_NAME_NONE


@dataclasses.dataclass(frozen=True)
class GuildStatus:
    actor_id: bytes
    guild_id: int = 0
    name_code: int = p4rm.GUILD_NAME_NONE
    members: int = 0
    role: int = p4rm.GUILD_ROLE_NONE
    prestige: int = 0
    season_points: int = 0
    banner_stars: int = 0
    quest_progress: int = 0
    quest_goal: int = p4rm.GUILD_QUEST_GOAL
    wins: int = 0
    losses: int = 0
    draws: int = 0
    last_outcome: int = p4rm.GUILD_OUTCOME_NONE
    last_opponent_code: int = p4rm.GUILD_NAME_NONE
    daily_flags: int = 0


@dataclasses.dataclass(frozen=True)
class GuildSummary:
    guild_id: int
    name_code: int
    members: int
    banner_stars: int
    prestige: int
    season_points: int
    wins: int
    losses: int
    draws: int


@dataclasses.dataclass(frozen=True)
class RealmActionResult:
    status: int
    code: int = 0
    value: int = 0
    related_id: int = 0


@dataclasses.dataclass(frozen=True)
class RealmEvent:
    event_id: int
    target_actor_id: bytes
    source_actor_id: bytes
    source_name: str
    kind: int
    code: int
    value: int
    body: bytes


class RealmStore:
    def __init__(
        self,
        path: str | Path,
        *,
        epoch_seconds: int = 0,
        migrate_legacy_artifacts: bool = False,
    ) -> None:
        self.path = str(path)
        self.epoch_seconds = epoch_seconds
        self.migrate_legacy_artifacts = migrate_legacy_artifacts
        self._lock = threading.RLock()
        self._initialize()

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.path, timeout=10.0)
        connection.execute("PRAGMA foreign_keys = ON")
        connection.execute("PRAGMA journal_mode = WAL")
        return connection

    def _initialize(self) -> None:
        Path(self.path).parent.mkdir(parents=True, exist_ok=True)
        with self._connect() as database:
            database.executescript(
                """
                CREATE TABLE IF NOT EXISTS actors (
                    profile TEXT PRIMARY KEY,
                    actor_id BLOB NOT NULL UNIQUE CHECK(length(actor_id) = 16),
                    created_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS heads (
                    actor_id BLOB PRIMARY KEY REFERENCES actors(actor_id),
                    revision INTEGER NOT NULL,
                    last_day_id INTEGER NOT NULL,
                    snapshot BLOB,
                    snapshot_crc32 INTEGER,
                    updated_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS operations (
                    actor_id BLOB NOT NULL,
                    nonce INTEGER NOT NULL,
                    body_crc32 INTEGER NOT NULL,
                    body_sha256 BLOB CHECK(
                        body_sha256 IS NULL OR length(body_sha256) = 32
                    ),
                    revision INTEGER NOT NULL,
                    created_at INTEGER NOT NULL,
                    PRIMARY KEY(actor_id, nonce)
                );
                CREATE TABLE IF NOT EXISTS events (
                    event_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    actor_id BLOB NOT NULL,
                    kind TEXT NOT NULL,
                    revision INTEGER NOT NULL,
                    created_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS profiles (
                    actor_id BLOB PRIMARY KEY REFERENCES actors(actor_id),
                    name TEXT NOT NULL,
                    hero_style INTEGER NOT NULL,
                    hero_class INTEGER NOT NULL,
                    level INTEGER NOT NULL,
                    flags INTEGER NOT NULL,
                    hit_points INTEGER NOT NULL,
                    max_hit_points INTEGER NOT NULL,
                    strength INTEGER NOT NULL,
                    defense INTEGER NOT NULL,
                    experience INTEGER NOT NULL,
                    chompcoin INTEGER NOT NULL,
                    pvp_wins INTEGER NOT NULL,
                    pvp_losses INTEGER NOT NULL,
                    dragon_kills INTEGER NOT NULL DEFAULT 0,
                    bank INTEGER NOT NULL DEFAULT 0,
                    last_seen INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS action_operations (
                    source_actor_id BLOB NOT NULL,
                    nonce INTEGER NOT NULL,
                    request_sha256 BLOB NOT NULL CHECK(length(request_sha256) = 32),
                    status INTEGER NOT NULL,
                    result_code INTEGER NOT NULL,
                    result_value INTEGER NOT NULL,
                    related_id INTEGER NOT NULL,
                    created_at INTEGER NOT NULL,
                    admitted INTEGER NOT NULL DEFAULT 1
                        CHECK(admitted IN (0, 1)),
                    PRIMARY KEY(source_actor_id, nonce)
                );
                CREATE TABLE IF NOT EXISTS realm_events (
                    event_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    target_actor_id BLOB NOT NULL,
                    source_actor_id BLOB NOT NULL,
                    kind INTEGER NOT NULL,
                    code INTEGER NOT NULL,
                    value INTEGER NOT NULL,
                    body BLOB NOT NULL,
                    created_at INTEGER NOT NULL,
                    receipt_at INTEGER,
                    acknowledged_at INTEGER,
                    committed_at INTEGER,
                    admitted INTEGER NOT NULL DEFAULT 1
                        CHECK(admitted IN (0, 1))
                );
                CREATE INDEX IF NOT EXISTS realm_events_pending
                    ON realm_events(target_actor_id, acknowledged_at, event_id);
                CREATE TABLE IF NOT EXISTS friendships (
                    source_actor_id BLOB NOT NULL,
                    target_actor_id BLOB NOT NULL,
                    trust INTEGER NOT NULL CHECK(trust BETWEEN 0 AND 100),
                    updated_at INTEGER NOT NULL,
                    admitted INTEGER NOT NULL DEFAULT 1
                        CHECK(admitted IN (0, 1)),
                    PRIMARY KEY(source_actor_id, target_actor_id)
                );
                CREATE TABLE IF NOT EXISTS team_invites (
                    source_actor_id BLOB NOT NULL,
                    target_actor_id BLOB NOT NULL,
                    created_at INTEGER NOT NULL,
                    admitted INTEGER NOT NULL DEFAULT 1
                        CHECK(admitted IN (0, 1)),
                    PRIMARY KEY(source_actor_id, target_actor_id)
                );
                CREATE TABLE IF NOT EXISTS teams (
                    actor_low BLOB NOT NULL,
                    actor_high BLOB NOT NULL,
                    active INTEGER NOT NULL CHECK(active IN (0, 1)),
                    updated_at INTEGER NOT NULL,
                    admitted INTEGER NOT NULL DEFAULT 1
                        CHECK(admitted IN (0, 1)),
                    PRIMARY KEY(actor_low, actor_high)
                );
                CREATE TABLE IF NOT EXISTS pvp_leases (
                    lease_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    source_actor_id BLOB NOT NULL,
                    target_actor_id BLOB NOT NULL,
                    realm_day_id INTEGER NOT NULL,
                    status INTEGER NOT NULL CHECK(status IN (0, 1)),
                    outcome INTEGER,
                    created_at INTEGER NOT NULL,
                    resolved_at INTEGER,
                    admitted INTEGER NOT NULL DEFAULT 1
                        CHECK(admitted IN (0, 1))
                );
                CREATE TABLE IF NOT EXISTS realm_feed (
                    feed_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    source_actor_id BLOB NOT NULL,
                    kind INTEGER NOT NULL,
                    body BLOB NOT NULL,
                    created_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS adoption_grants (
                    grant_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    actor_id BLOB NOT NULL UNIQUE REFERENCES actors(actor_id),
                    granted_at INTEGER NOT NULL,
                    consumed_at INTEGER,
                    consumed_revision INTEGER
                );
                CREATE TABLE IF NOT EXISTS archived_heads (
                    archive_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    actor_id BLOB NOT NULL REFERENCES actors(actor_id),
                    revision INTEGER NOT NULL,
                    last_day_id INTEGER NOT NULL,
                    snapshot BLOB NOT NULL,
                    snapshot_sha256 BLOB NOT NULL
                        CHECK(length(snapshot_sha256) = 32),
                    reason TEXT NOT NULL,
                    created_at INTEGER NOT NULL
                );
                CREATE INDEX IF NOT EXISTS archived_heads_actor_revision
                    ON archived_heads(actor_id, revision, archive_id);
                CREATE TABLE IF NOT EXISTS realm_day_anchors (
                    actor_id BLOB PRIMARY KEY REFERENCES actors(actor_id),
                    realm_day_id INTEGER NOT NULL,
                    base_last_day_id INTEGER NOT NULL,
                    snapshot BLOB NOT NULL,
                    snapshot_sha256 BLOB NOT NULL
                        CHECK(length(snapshot_sha256) = 32),
                    created_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS realm_economy_events (
                    event_id INTEGER PRIMARY KEY REFERENCES realm_events(event_id),
                    actor_id BLOB NOT NULL REFERENCES actors(actor_id),
                    chompcoin_delta INTEGER NOT NULL,
                    bank_delta INTEGER NOT NULL,
                    applied_revision INTEGER,
                    created_at INTEGER NOT NULL
                );
                CREATE INDEX IF NOT EXISTS realm_economy_events_pending
                    ON realm_economy_events(actor_id, applied_revision, event_id);
                CREATE TABLE IF NOT EXISTS realm_daily_action_usage (
                    actor_id BLOB NOT NULL REFERENCES actors(actor_id),
                    realm_day_id INTEGER NOT NULL,
                    friendship_actions INTEGER NOT NULL DEFAULT 0,
                    PRIMARY KEY(actor_id, realm_day_id)
                );
                CREATE TABLE IF NOT EXISTS guilds (
                    guild_id INTEGER PRIMARY KEY AUTOINCREMENT
                        CHECK(guild_id BETWEEN 1 AND 4294967295),
                    name_code INTEGER NOT NULL UNIQUE
                        CHECK(name_code BETWEEN 1 AND 16),
                    leader_actor_id BLOB NOT NULL REFERENCES actors(actor_id)
                        CHECK(length(leader_actor_id) = 16),
                    created_day INTEGER NOT NULL,
                    prestige INTEGER NOT NULL DEFAULT 0
                        CHECK(prestige BETWEEN 0 AND 4294967295),
                    season_id INTEGER NOT NULL,
                    season_points INTEGER NOT NULL DEFAULT 0
                        CHECK(season_points BETWEEN 0 AND 4294967295),
                    banner_stars INTEGER NOT NULL DEFAULT 0
                        CHECK(banner_stars BETWEEN 0 AND 65535),
                    quest_day INTEGER NOT NULL,
                    quest_progress INTEGER NOT NULL DEFAULT 0
                        CHECK(quest_progress BETWEEN 0 AND 12),
                    quest_complete INTEGER NOT NULL DEFAULT 0
                        CHECK(quest_complete IN (0, 1)),
                    wins INTEGER NOT NULL DEFAULT 0
                        CHECK(wins BETWEEN 0 AND 65535),
                    losses INTEGER NOT NULL DEFAULT 0
                        CHECK(losses BETWEEN 0 AND 65535),
                    draws INTEGER NOT NULL DEFAULT 0
                        CHECK(draws BETWEEN 0 AND 65535),
                    last_outcome INTEGER NOT NULL DEFAULT 0
                        CHECK(last_outcome BETWEEN 0 AND 3),
                    last_opponent_code INTEGER NOT NULL DEFAULT 0
                        CHECK(last_opponent_code BETWEEN 0 AND 16)
                );
                CREATE TABLE IF NOT EXISTS guild_members (
                    actor_id BLOB PRIMARY KEY REFERENCES actors(actor_id)
                        CHECK(length(actor_id) = 16),
                    guild_id INTEGER NOT NULL REFERENCES guilds(guild_id)
                        ON DELETE CASCADE,
                    joined_day INTEGER NOT NULL,
                    role INTEGER NOT NULL CHECK(role IN (1, 2))
                );
                CREATE INDEX IF NOT EXISTS guild_members_by_guild
                    ON guild_members(guild_id, joined_day, actor_id);
                CREATE TABLE IF NOT EXISTS guild_rejoin_cooldowns (
                    actor_id BLOB PRIMARY KEY REFERENCES actors(actor_id)
                        CHECK(length(actor_id) = 16),
                    left_day INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS guild_rallies (
                    actor_id BLOB NOT NULL REFERENCES actors(actor_id)
                        CHECK(length(actor_id) = 16),
                    realm_day_id INTEGER NOT NULL,
                    guild_id INTEGER NOT NULL REFERENCES guilds(guild_id)
                        ON DELETE CASCADE,
                    route INTEGER NOT NULL CHECK(route BETWEEN 0 AND 2),
                    points INTEGER NOT NULL CHECK(points BETWEEN 2 AND 5),
                    PRIMARY KEY(actor_id, realm_day_id)
                );
                CREATE INDEX IF NOT EXISTS guild_rallies_by_guild_day
                    ON guild_rallies(guild_id, realm_day_id, route);
                CREATE TABLE IF NOT EXISTS guild_clashes (
                    clash_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    realm_day_id INTEGER NOT NULL,
                    source_guild_id INTEGER NOT NULL,
                    target_guild_id INTEGER NOT NULL,
                    pair_low INTEGER NOT NULL,
                    pair_high INTEGER NOT NULL,
                    source_score INTEGER NOT NULL,
                    target_score INTEGER NOT NULL,
                    outcome INTEGER NOT NULL CHECK(outcome BETWEEN 1 AND 3),
                    created_at INTEGER NOT NULL,
                    CHECK(source_guild_id != target_guild_id),
                    CHECK(pair_low < pair_high)
                );
                CREATE UNIQUE INDEX IF NOT EXISTS guild_clashes_outgoing_day
                    ON guild_clashes(source_guild_id, realm_day_id);
                CREATE UNIQUE INDEX IF NOT EXISTS guild_clashes_incoming_day
                    ON guild_clashes(target_guild_id, realm_day_id);
                CREATE INDEX IF NOT EXISTS guild_clashes_pair_day
                    ON guild_clashes(pair_low, pair_high, realm_day_id DESC);
                CREATE TABLE IF NOT EXISTS guild_cheers (
                    source_guild_id INTEGER NOT NULL,
                    realm_day_id INTEGER NOT NULL,
                    target_guild_id INTEGER NOT NULL,
                    actor_id BLOB NOT NULL REFERENCES actors(actor_id)
                        CHECK(length(actor_id) = 16),
                    created_at INTEGER NOT NULL,
                    PRIMARY KEY(source_guild_id, realm_day_id),
                    CHECK(source_guild_id != target_guild_id)
                );
                """
            )
            # The base CREATE script is idempotent.  Keep every additive
            # upgrade and its validation in one transaction so a fail-closed
            # migration never leaves a half-classified legacy database.
            database.execute("BEGIN IMMEDIATE")
            profile_columns = {
                str(row[1]) for row in database.execute("PRAGMA table_info(profiles)")
            }
            if "bank" not in profile_columns:
                database.execute(
                    "ALTER TABLE profiles ADD COLUMN bank INTEGER NOT NULL DEFAULT 0"
                )
            if "dragon_kills" not in profile_columns:
                database.execute(
                    "ALTER TABLE profiles ADD COLUMN dragon_kills "
                    "INTEGER NOT NULL DEFAULT 0"
                )
            operation_columns = {
                str(row[1])
                for row in database.execute("PRAGMA table_info(operations)")
            }
            if "body_sha256" not in operation_columns:
                database.execute(
                    "ALTER TABLE operations ADD COLUMN body_sha256 BLOB"
                )
            event_columns = {
                str(row[1])
                for row in database.execute("PRAGMA table_info(realm_events)")
            }
            if "receipt_at" not in event_columns:
                database.execute(
                    "ALTER TABLE realm_events ADD COLUMN receipt_at INTEGER"
                )
            if "committed_at" not in event_columns:
                database.execute(
                    "ALTER TABLE realm_events ADD COLUMN committed_at INTEGER"
                )
            legacy_events_migrated = self._migrate_legacy_admission(
                database, authorized=self.migrate_legacy_artifacts
            )
            self._reconcile_legacy_events_and_economy(
                database,
                authorized=self.migrate_legacy_artifacts,
                pre_admission_events=legacy_events_migrated,
            )
            database.execute(
                "CREATE INDEX IF NOT EXISTS realm_events_uncommitted "
                "ON realm_events(target_actor_id, committed_at, event_id)"
            )
            self._rebuild_authoritative_profiles(database)

    @staticmethod
    def _migrate_legacy_admission(
        database: sqlite3.Connection, *, authorized: bool
    ) -> bool:
        """Add reversible admission flags to pre-admission tables.

        An upgraded row is admitted only when all participating actors already
        have accepted snapshots.  Rows are preserved unchanged and remain
        inspectable with ``admitted = 0``.  A non-empty legacy table requires
        an explicit operator-authorized migration open, avoiding silent realm
        reclassification during an ordinary hub restart.
        """
        admission_predicates = {
            "realm_events": (
                "EXISTS(SELECT 1 FROM heads AS source_head "
                "WHERE source_head.actor_id = realm_events.source_actor_id "
                "AND source_head.snapshot IS NOT NULL) AND "
                "EXISTS(SELECT 1 FROM heads AS target_head "
                "WHERE target_head.actor_id = realm_events.target_actor_id "
                "AND target_head.snapshot IS NOT NULL)"
            ),
            "friendships": (
                "EXISTS(SELECT 1 FROM heads AS source_head "
                "WHERE source_head.actor_id = friendships.source_actor_id "
                "AND source_head.snapshot IS NOT NULL) AND "
                "EXISTS(SELECT 1 FROM heads AS target_head "
                "WHERE target_head.actor_id = friendships.target_actor_id "
                "AND target_head.snapshot IS NOT NULL)"
            ),
            "team_invites": (
                "EXISTS(SELECT 1 FROM heads AS source_head "
                "WHERE source_head.actor_id = team_invites.source_actor_id "
                "AND source_head.snapshot IS NOT NULL) AND "
                "EXISTS(SELECT 1 FROM heads AS target_head "
                "WHERE target_head.actor_id = team_invites.target_actor_id "
                "AND target_head.snapshot IS NOT NULL)"
            ),
            "teams": (
                "EXISTS(SELECT 1 FROM heads AS low_head "
                "WHERE low_head.actor_id = teams.actor_low "
                "AND low_head.snapshot IS NOT NULL) AND "
                "EXISTS(SELECT 1 FROM heads AS high_head "
                "WHERE high_head.actor_id = teams.actor_high "
                "AND high_head.snapshot IS NOT NULL)"
            ),
            "pvp_leases": (
                "EXISTS(SELECT 1 FROM heads AS source_head "
                "WHERE source_head.actor_id = pvp_leases.source_actor_id "
                "AND source_head.snapshot IS NOT NULL) AND "
                "EXISTS(SELECT 1 FROM heads AS target_head "
                "WHERE target_head.actor_id = pvp_leases.target_actor_id "
                "AND target_head.snapshot IS NOT NULL)"
            ),
            "action_operations": (
                "EXISTS(SELECT 1 FROM heads AS source_head "
                "WHERE source_head.actor_id = action_operations.source_actor_id "
                "AND source_head.snapshot IS NOT NULL)"
            ),
        }
        legacy_events_migrated = False
        for table, predicate in admission_predicates.items():
            columns = {
                str(row[1])
                for row in database.execute(f"PRAGMA table_info({table})")
            }
            if "admitted" in columns:
                continue
            row_count = int(
                database.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
            )
            if row_count and not authorized:
                raise RuntimeError(
                    "legacy LORD multiplayer artifacts require an explicit "
                    "authorized migration open"
                )
            if table == "realm_events" and row_count:
                legacy_events_migrated = True
            database.execute(
                f"ALTER TABLE {table} ADD COLUMN admitted INTEGER NOT NULL "
                "DEFAULT 0 CHECK(admitted IN (0, 1))"
            )
            if row_count:
                database.execute(
                    f"UPDATE {table} SET admitted = 1 WHERE {predicate}"
                )
        return legacy_events_migrated

    @staticmethod
    def _event_economy_delta(
        database: sqlite3.Connection,
        event_id: int,
        target_actor_id: bytes,
        source_actor_id: bytes,
        kind: int,
        code: int,
        value: int,
        body: bytes,
        created_at: int,
    ) -> tuple[tuple[bytes, int, int] | None, bool, bool]:
        """Return exact ledger policy and any legacy must-be-consumed rule."""
        if kind == p4rm.ACTION_TRANSFER:
            if code not in (0, 1) or value != 100 or body:
                raise RuntimeError(
                    f"malformed admitted transfer event {event_id}"
                )
            if code == 0:
                return (target_actor_id, value, 0), True, False
            return (target_actor_id, 0, -value), True, False
        if kind == p4rm.ACTION_FRIEND:
            if code == 0x81:
                if value != 30 or body:
                    raise RuntimeError(
                        f"malformed admitted friend-supplies event {event_id}"
                    )
                return (target_actor_id, -100, 0), True, False
            return None, False, False
        if kind == p4rm.ACTION_PVP_RESOLVE:
            if code not in (0, 1, 2) or not 0 <= value <= MAX_CURRENCY or body:
                raise RuntimeError(f"malformed admitted PvP event {event_id}")
            forward_outcomes = {
                int(row[0])
                for row in database.execute(
                    "SELECT outcome FROM pvp_leases WHERE admitted = 1 "
                    "AND status = 1 AND source_actor_id = ? "
                    "AND target_actor_id = ? AND resolved_at = ? "
                    "AND outcome IS NOT NULL",
                    (source_actor_id, target_actor_id, created_at),
                )
            }
            reverse_outcomes = {
                int(row[0])
                for row in database.execute(
                    "SELECT outcome FROM pvp_leases WHERE admitted = 1 "
                    "AND status = 1 AND source_actor_id = ? "
                    "AND target_actor_id = ? AND resolved_at = ? "
                    "AND outcome IS NOT NULL",
                    (target_actor_id, source_actor_id, created_at),
                )
            }
            if (
                bool(forward_outcomes) == bool(reverse_outcomes)
                or len(forward_outcomes) > 1
                or len(reverse_outcomes) > 1
            ):
                raise RuntimeError(
                    f"ambiguous admitted PvP lease for event {event_id}"
                )
            if forward_outcomes:
                outcome = next(iter(forward_outcomes))
                if outcome == 0 and code == 0 and value == 0:
                    return None, False, False
                if outcome == 1 and code == 1:
                    return (target_actor_id, -value, 0), True, False
                raise RuntimeError(
                    f"invalid target-directed PvP event {event_id}"
                )

            outcome = next(iter(reverse_outcomes))
            if outcome == 0:
                if code == 1 and value == 0:
                    return (target_actor_id, 0, 0), True, False
                if code == 0 and value == 0:
                    # The old wire code would replay with target semantics.
                    return None, False, True
            elif outcome == 1:
                if code == 2:
                    return (target_actor_id, value, 0), False, False
                if code == 1:
                    # Pre-fix source-win code 1 now means a loss/debit.  A
                    # cursor-proven historical row is safe; replay is not.
                    return (target_actor_id, value, 0), False, True
            raise RuntimeError(f"invalid source-directed PvP event {event_id}")
        return None, False, False

    @classmethod
    def _reconcile_legacy_events_and_economy(
        cls,
        database: sqlite3.Connection,
        *,
        authorized: bool,
        pre_admission_events: bool,
    ) -> None:
        """Derive legacy delivery/economy proofs from accepted v4+ heads.

        All rows are validated and the complete repair plan is assembled
        before anything is written.  An ordinary open fails closed if that
        plan is non-empty; the operator must use the explicit legacy migration
        option.  Re-running after a successful migration is a no-op.
        """
        orphan_ledger = database.execute(
            "SELECT ledger.event_id FROM realm_economy_events AS ledger "
            "LEFT JOIN realm_events AS event ON event.event_id = ledger.event_id "
            "WHERE event.event_id IS NULL LIMIT 1"
        ).fetchone()
        if orphan_ledger is not None:
            raise RuntimeError(
                f"orphan LORD economy ledger event {int(orphan_ledger[0])}"
            )

        receipt_repairs = [
            (int(row[0]), int(row[1]))
            for row in database.execute(
                "SELECT event_id, acknowledged_at FROM realm_events "
                "WHERE admitted = 1 AND acknowledged_at IS NOT NULL "
                "AND receipt_at IS NULL"
            )
        ]
        ledger_inserts: list[tuple[int, bytes, int, int, int]] = []
        ambiguous_legacy_credits: list[tuple[int, bytes]] = []
        must_be_consumed: list[tuple[int, bytes, str]] = []
        admitted_events = database.execute(
            "SELECT event_id, target_actor_id, source_actor_id, kind, code, "
            "value, body, "
            "created_at FROM realm_events WHERE admitted = 1 "
            "ORDER BY event_id"
        ).fetchall()
        for row in admitted_events:
            event_id = int(row[0])
            target_actor_id = bytes(row[1])
            source_actor_id = bytes(row[2])
            expected, may_backfill, requires_consumed = cls._event_economy_delta(
                database,
                event_id,
                target_actor_id,
                source_actor_id,
                int(row[3]),
                int(row[4]),
                int(row[5]),
                bytes(row[6]),
                int(row[7]),
            )
            if requires_consumed:
                must_be_consumed.append(
                    (event_id, target_actor_id, "wire-incompatible legacy PvP")
                )
            if pre_admission_events and int(row[3]) == p4rm.ACTION_PVP_RESOLVE:
                must_be_consumed.append(
                    (event_id, target_actor_id, "pre-admission PvP")
                )
            ledger = database.execute(
                "SELECT actor_id, chompcoin_delta, bank_delta "
                "FROM realm_economy_events WHERE event_id = ?",
                (event_id,),
            ).fetchone()
            if expected is None:
                if ledger is not None:
                    raise RuntimeError(
                        f"unexpected LORD economy ledger event {event_id}"
                    )
                continue
            if ledger is not None:
                actual = (bytes(ledger[0]), int(ledger[1]), int(ledger[2]))
                if actual != expected:
                    raise RuntimeError(
                        f"LORD economy ledger mismatch for event {event_id}"
                    )
            elif may_backfill:
                ledger_inserts.append(
                    (event_id, expected[0], expected[1], expected[2], int(row[7]))
                )
            else:
                ambiguous_legacy_credits.append((event_id, target_actor_id))

        decoded_heads = {}
        for actor_value, revision_value, snapshot_value in database.execute(
            "SELECT actor_id, revision, snapshot FROM heads "
            "WHERE snapshot IS NOT NULL"
        ):
            actor_id = bytes(actor_value)
            try:
                snapshot = decode_lord_sync(
                    bytes(snapshot_value),
                    expected_actor_id=actor_id,
                    allow_adoption_bridge=True,
                )
            except ValueError as error:
                raise RuntimeError(
                    "refusing to reconcile a malformed authoritative LORD head "
                    f"for actor {actor_id.hex()}"
                ) from error
            decoded_heads[actor_id] = (int(revision_value), snapshot)

        event_targets = {bytes(row[1]) for row in admitted_events}
        for target_actor_id in event_targets:
            head = decoded_heads.get(target_actor_id)
            if head is None:
                raise RuntimeError(
                    "admitted LORD event targets an actor without a head: "
                    f"{target_actor_id.hex()}"
                )
            if head[1].save_version < 4:
                raise RuntimeError(
                    "cannot infer LORD event consumption from a v3 head for "
                    f"actor {target_actor_id.hex()}"
                )

        for event_id, target_actor_id, reason in must_be_consumed:
            target_snapshot = decoded_heads[target_actor_id][1]
            if event_id > target_snapshot.last_realm_event_id:
                raise RuntimeError(
                    f"{reason} event remains pending: event {event_id} "
                    f"actor {target_actor_id.hex()}"
                )
        for event_id, target_actor_id in ambiguous_legacy_credits:
            target_snapshot = decoded_heads[target_actor_id][1]
            if event_id > target_snapshot.last_realm_event_id:
                raise RuntimeError(
                    "ambiguous legacy LORD PvP credit remains deliverable: "
                    f"event {event_id} actor {target_actor_id.hex()}"
                )

        committed_repairs: list[tuple[int, int]] = []
        applied_repairs: list[tuple[int, int]] = []
        for actor_id, (head_revision, snapshot) in decoded_heads.items():
            cursor = snapshot.last_realm_event_id
            event_proofs = database.execute(
                "SELECT event_id, committed_at, acknowledged_at, receipt_at, "
                "created_at FROM realm_events WHERE admitted = 1 "
                "AND target_actor_id = ? ORDER BY event_id",
                (actor_id,),
            ).fetchall()
            for event_row in event_proofs:
                event_id = int(event_row[0])
                if event_row[1] is not None and event_id > cursor:
                    raise RuntimeError(
                        "LORD event commitment is newer than the target save "
                        f"cursor: event {event_id} actor {actor_id.hex()}"
                    )
                if event_id <= cursor and event_row[1] is None:
                    proof_time = next(
                        int(value)
                        for value in event_row[2:]
                        if value is not None
                    )
                    committed_repairs.append((event_id, proof_time))

            ledger_rows = database.execute(
                "SELECT ledger.event_id, ledger.applied_revision "
                "FROM realm_economy_events AS ledger "
                "JOIN realm_events AS event ON event.event_id = ledger.event_id "
                "WHERE event.admitted = 1 AND event.target_actor_id = ?",
                (actor_id,),
            ).fetchall()
            for event_value, applied_value in ledger_rows:
                event_id = int(event_value)
                if applied_value is not None:
                    applied_revision = int(applied_value)
                    if (
                        applied_revision <= 0
                        or event_id > cursor
                        or applied_revision > head_revision
                    ):
                        raise RuntimeError(
                            "invalid LORD economy application proof for event "
                            f"{event_id} actor {actor_id.hex()}"
                        )
                elif event_id <= cursor:
                    applied_repairs.append((event_id, head_revision))

        # A newly backfilled row whose event is already in the persisted save
        # is inserted with that head's revision rather than briefly appearing
        # pending to the profile projection.
        adjusted_ledger_inserts: list[tuple[int, bytes, int, int, int, int | None]] = []
        for event_id, actor_id, coin_delta, bank_delta, created_at in ledger_inserts:
            head_revision, snapshot = decoded_heads[actor_id]
            applied_revision = (
                head_revision if event_id <= snapshot.last_realm_event_id else None
            )
            adjusted_ledger_inserts.append(
                (
                    event_id,
                    actor_id,
                    coin_delta,
                    bank_delta,
                    created_at,
                    applied_revision,
                )
            )

        repair_count = (
            len(receipt_repairs)
            + len(committed_repairs)
            + len(applied_repairs)
            + len(adjusted_ledger_inserts)
        )
        if repair_count and not authorized:
            raise RuntimeError(
                "legacy LORD event/economy state requires an explicit "
                "authorized migration open"
            )

        for event_id, receipt_at in receipt_repairs:
            database.execute(
                "UPDATE realm_events SET receipt_at = ? "
                "WHERE event_id = ? AND admitted = 1 AND receipt_at IS NULL",
                (receipt_at, event_id),
            )
        for event_id, committed_at in committed_repairs:
            database.execute(
                "UPDATE realm_events SET committed_at = ? "
                "WHERE event_id = ? AND admitted = 1 AND committed_at IS NULL",
                (committed_at, event_id),
            )
        for event_id, applied_revision in applied_repairs:
            database.execute(
                "UPDATE realm_economy_events SET applied_revision = ? "
                "WHERE event_id = ? AND applied_revision IS NULL",
                (applied_revision, event_id),
            )
        for row in adjusted_ledger_inserts:
            database.execute(
                "INSERT INTO realm_economy_events(event_id, actor_id, "
                "chompcoin_delta, bank_delta, created_at, applied_revision) "
                "VALUES(?, ?, ?, ?, ?, ?)",
                row,
            )

    def realm_clock(self, now: float | None = None) -> tuple[int, int]:
        unix_seconds = int(time.time() if now is None else now)
        elapsed = max(0, unix_seconds - self.epoch_seconds)
        day_id = elapsed // 3600 + 1
        remaining = 3600 - elapsed % 3600
        return day_id, remaining

    def actor_for_profile(self, profile: str) -> bytes:
        normalized = profile.strip()
        if not normalized or len(normalized.encode("utf-8")) > 64:
            raise ValueError("profile must contain 1..64 UTF-8 bytes")
        with self._lock, self._connect() as database:
            row = database.execute(
                "SELECT actor_id FROM actors WHERE profile = ?", (normalized,)
            ).fetchone()
            if row is not None:
                return bytes(row[0])
            actor_count = int(
                database.execute("SELECT COUNT(*) FROM actors").fetchone()[0]
            )
            if actor_count >= MAX_REALM_PLAYERS:
                raise RealmFullError(
                    f"LORD realm is full ({MAX_REALM_PLAYERS} players)"
                )
            salt = 0
            while True:
                material = f"p4-realm:{normalized}:{salt}".encode("utf-8")
                actor_id = hashlib.sha256(material).digest()[:16]
                if actor_id != b"\0" * 16:
                    occupied = database.execute(
                        "SELECT 1 FROM actors WHERE actor_id = ?", (actor_id,)
                    ).fetchone()
                    if occupied is None:
                        break
                salt += 1
            now = int(time.time())
            database.execute(
                "INSERT INTO actors(profile, actor_id, created_at) VALUES(?, ?, ?)",
                (normalized, actor_id, now),
            )
            database.execute(
                "INSERT INTO heads(actor_id, revision, last_day_id, snapshot, "
                "snapshot_crc32, updated_at) VALUES(?, 0, 0, NULL, NULL, ?)",
                (actor_id, now),
            )
            return actor_id

    def authorize_local_adoption(self, profile: str) -> int:
        """Create the actor's single lifetime parent-authorized adoption grant."""
        actor_id = self.actor_for_profile(profile)
        with self._lock, self._connect() as database:
            database.execute("BEGIN IMMEDIATE")
            existing = database.execute(
                "SELECT grant_id, consumed_at FROM adoption_grants "
                "WHERE actor_id = ?",
                (actor_id,),
            ).fetchone()
            if existing is not None and existing[1] is None:
                database.commit()
                return int(existing[0])
            if existing is not None:
                database.rollback()
                raise ValueError(
                    "local adoption was already authorized for this profile"
                )
            cursor = database.execute(
                "INSERT INTO adoption_grants(actor_id, granted_at, "
                "consumed_at, consumed_revision) VALUES(?, ?, NULL, NULL)",
                (actor_id, int(time.time())),
            )
            database.commit()
            return int(cursor.lastrowid)

    def pending_local_adoption(self, actor_id: bytes) -> int | None:
        if len(actor_id) != 16:
            raise ValueError("actor ID must be 16 bytes")
        with self._lock, self._connect() as database:
            row = database.execute(
                "SELECT grant_id FROM adoption_grants WHERE actor_id = ? "
                "AND consumed_at IS NULL",
                (actor_id,),
            ).fetchone()
        return None if row is None else int(row[0])

    def read_head(self, actor_id: bytes) -> RealmHead:
        if len(actor_id) != 16:
            raise ValueError("actor ID must be 16 bytes")
        with self._lock, self._connect() as database:
            row = database.execute(
                "SELECT revision, last_day_id, snapshot, snapshot_crc32 "
                "FROM heads WHERE actor_id = ?",
                (actor_id,),
            ).fetchone()
            if row is None:
                raise KeyError("unknown actor")
            snapshot = None if row[2] is None else bytes(row[2])
            if snapshot is not None and (zlib.crc32(snapshot) & 0xFFFFFFFF) != row[3]:
                raise RuntimeError("stored realm snapshot failed CRC")
            return RealmHead(actor_id, int(row[0]), int(row[1]), snapshot)

    def commit(
        self,
        actor_id: bytes,
        expected_revision: int,
        nonce: int,
        realm_day_id: int,
        snapshot: bytes,
        *,
        adoption_grant_id: int | None = None,
        now: float | None = None,
    ) -> tuple[str, int]:
        if len(actor_id) != 16 or not snapshot or nonce == 0 or realm_day_id == 0:
            raise ValueError("invalid realm commit")
        try:
            candidate = decode_lord_sync(
                snapshot,
                expected_actor_id=actor_id,
                expected_nonce=nonce,
                allow_adoption_bridge=adoption_grant_id is not None,
            )
        except ValueError:
            try:
                return "invalid", self.read_head(actor_id).revision
            except (KeyError, OSError, RuntimeError, sqlite3.Error):
                return "invalid", 0
        body_crc32 = zlib.crc32(snapshot) & 0xFFFFFFFF
        body_sha256 = hashlib.sha256(snapshot).digest()
        with self._lock, self._connect() as database:
            database.execute("BEGIN IMMEDIATE")
            row = database.execute(
                "SELECT revision, last_day_id, snapshot FROM heads "
                "WHERE actor_id = ?",
                (actor_id,),
            ).fetchone()
            if row is None:
                database.rollback()
                raise KeyError("unknown actor")
            current_revision = int(row[0])
            operation = database.execute(
                "SELECT body_crc32, body_sha256, revision FROM operations "
                "WHERE actor_id = ? AND nonce = ?",
                (actor_id, nonce),
            ).fetchone()
            if operation is not None:
                operation_hash = (
                    None if operation[1] is None else bytes(operation[1])
                )
                operation_revision = int(operation[2])
                if operation_hash is None:
                    current_snapshot = (
                        None if row[2] is None else bytes(row[2])
                    )
                    if (
                        operation_revision != current_revision
                        or current_snapshot is None
                        or hashlib.sha256(current_snapshot).digest()
                        != body_sha256
                    ):
                        database.rollback()
                        return "invalid", operation_revision
                    database.execute(
                        "UPDATE operations SET body_sha256 = ? "
                        "WHERE actor_id = ? AND nonce = ? "
                        "AND body_sha256 IS NULL",
                        (body_sha256, actor_id, nonce),
                    )
                if (
                    int(operation[0]) != body_crc32
                    or (
                        operation_hash is not None
                        and operation_hash != body_sha256
                    )
                ):
                    database.rollback()
                    return "invalid", operation_revision
                database.commit()
                return "ok", operation_revision
            if expected_revision != current_revision:
                database.rollback()
                return "conflict", current_revision
            current_day, _ = self.realm_clock(now)
            if int(row[1]) > current_day:
                database.rollback()
                raise RuntimeError("realm head is newer than the hub clock")
            if realm_day_id < current_day:
                database.rollback()
                return "stale-day", current_revision
            if realm_day_id > current_day:
                database.rollback()
                return "invalid", current_revision
            prior_snapshot = None if row[2] is None else bytes(row[2])
            prior_record = None
            if adoption_grant_id is not None:
                grant = database.execute(
                    "SELECT 1 FROM adoption_grants WHERE grant_id = ? "
                    "AND actor_id = ? AND consumed_at IS NULL",
                    (adoption_grant_id, actor_id),
                ).fetchone()
                if grant is None:
                    database.rollback()
                    return "invalid", current_revision
                if (
                    candidate.save_version < 5
                    or candidate.sync_actor_id != actor_id
                    or candidate.sync_server_revision != current_revision
                    or candidate.sync_committed_save_sequence != 0
                ):
                    database.rollback()
                    return "invalid", current_revision
            elif prior_snapshot is not None:
                try:
                    prior_is_adoption = database.execute(
                        "SELECT 1 FROM adoption_grants WHERE actor_id = ? "
                        "AND consumed_revision = ?",
                        (actor_id, current_revision),
                    ).fetchone()
                    prior_record = decode_lord_sync(
                        prior_snapshot,
                        expected_actor_id=actor_id,
                        allow_adoption_bridge=prior_is_adoption is not None,
                    )
                    if (
                        candidate.save_version < 5
                        or candidate.sync_actor_id != actor_id
                        or candidate.sync_server_revision != current_revision
                        or candidate.sync_committed_save_sequence
                        != prior_record.save_sequence
                    ):
                        raise ValueError("LORD snapshot has no valid server base")
                    validate_lord_transition(
                        prior_record,
                        candidate,
                        realm_day_advanced=int(row[1]) < realm_day_id,
                    )
                    if (
                        int(row[1]) < realm_day_id
                        and (
                            prior_record.player.day == 0xFFFF
                            or candidate.player.day
                            != prior_record.player.day + 1
                        )
                    ):
                        raise ValueError(
                            "LORD hourly rollover was not applied exactly once"
                        )
                except ValueError:
                    database.rollback()
                    return "invalid", current_revision
            elif (
                candidate.sync_actor_id != bytes(16)
                or candidate.sync_server_revision != 0
                or candidate.sync_committed_save_sequence != 0
            ):
                database.rollback()
                return "invalid", current_revision

            economy_event_cursor: int | None = None
            remaining_chompcoin_delta = 0
            remaining_bank_delta = 0
            pending_economy = database.execute(
                "SELECT ledger.event_id, ledger.chompcoin_delta, "
                "ledger.bank_delta FROM realm_economy_events AS ledger "
                "JOIN realm_events AS event ON event.event_id = ledger.event_id "
                "WHERE ledger.actor_id = ? AND ledger.applied_revision IS NULL "
                "AND event.admitted = 1 ORDER BY ledger.event_id ASC",
                (actor_id,),
            ).fetchall()
            if pending_economy:
                if prior_snapshot is None:
                    database.rollback()
                    return "invalid", current_revision
                if prior_record is None:
                    try:
                        prior_record = decode_lord_sync(
                            prior_snapshot,
                            expected_actor_id=actor_id,
                            allow_adoption_bridge=True,
                        )
                    except ValueError:
                        database.rollback()
                        return "invalid", current_revision
                eligible_economy = [
                    item
                    for item in pending_economy
                    if int(item[0]) <= candidate.last_realm_event_id
                ]
                remaining_chompcoin_delta = sum(
                    int(item[1])
                    for item in pending_economy
                    if int(item[0]) > candidate.last_realm_event_id
                )
                remaining_bank_delta = sum(
                    int(item[2])
                    for item in pending_economy
                    if int(item[0]) > candidate.last_realm_event_id
                )
                if eligible_economy:
                    economy_event_cursor = int(eligible_economy[-1][0])
                    chompcoin_delta = sum(
                        int(item[1]) for item in eligible_economy
                    )
                    bank_delta = sum(
                        int(item[2]) for item in eligible_economy
                    )
                    expected_chompcoin = (
                        prior_record.player.chompcoin + chompcoin_delta
                    )
                    expected_bank = prior_record.player.bank + bank_delta
                    # Economy events are applied by the stock client before it
                    # advances last_realm_event_id.  Check the affected account,
                    # not total wealth, so ordinary forest earnings in ChompCoin
                    # survive a bank debit.  A player who moves new earnings into
                    # that same affected account must sync the event first; a
                    # rejection preserves both the local save and server head.
                    if (
                        expected_chompcoin < 0
                        or expected_bank < 0
                        or (
                            chompcoin_delta < 0
                            and candidate.player.chompcoin > expected_chompcoin
                        )
                        or (
                            chompcoin_delta > 0
                            and candidate.player.chompcoin < expected_chompcoin
                        )
                        or (
                            bank_delta < 0
                            and candidate.player.bank > expected_bank
                        )
                        or (
                            bank_delta > 0
                            and candidate.player.bank < expected_bank
                        )
                    ):
                        database.rollback()
                        return "invalid", current_revision

            if prior_snapshot is not None and prior_record is None:
                try:
                    prior_record = decode_lord_sync(
                        prior_snapshot,
                        expected_actor_id=actor_id,
                        allow_adoption_bridge=True,
                    )
                except ValueError:
                    database.rollback()
                    return "invalid", current_revision
            prior_event_cursor = (
                0 if prior_record is None else prior_record.last_realm_event_id
            )
            received_cursor_row = database.execute(
                "SELECT MAX(event_id) FROM realm_events WHERE "
                "target_actor_id = ? AND admitted = 1",
                (actor_id,),
            ).fetchone()
            received_event_cursor = (
                0
                if received_cursor_row is None or received_cursor_row[0] is None
                else int(received_cursor_row[0])
            )
            if candidate.last_realm_event_id > max(
                prior_event_cursor, received_event_cursor
            ):
                database.rollback()
                return "invalid", current_revision

            if prior_record is not None and adoption_grant_id is None:
                # Once an actor has a realm head, PvP prestige is server-owned.
                # A stock cached duel mutates XP/ChompCoin and one of these
                # counters in the same snapshot; requiring the exact
                # cursor-proven counters rejects that whole unreceipted bundle
                # atomically while still allowing ordinary offline solo gains.

                if candidate.last_realm_event_id > prior_event_cursor:
                    cursor_event = database.execute(
                        "SELECT 1 FROM realm_events WHERE event_id = ? "
                        "AND target_actor_id = ? AND admitted = 1",
                        (candidate.last_realm_event_id, actor_id),
                    ).fetchone()
                    if cursor_event is None:
                        database.rollback()
                        return "invalid", current_revision

                pvp_outcomes = database.execute(
                    "SELECT source_actor_id, code, created_at "
                    "FROM realm_events WHERE target_actor_id = ? "
                    "AND admitted = 1 AND kind = ? AND event_id > ? "
                    "AND event_id <= ? ORDER BY event_id ASC",
                    (
                        actor_id,
                        p4rm.ACTION_PVP_RESOLVE,
                        prior_event_cursor,
                        candidate.last_realm_event_id,
                    ),
                ).fetchall()
                pvp_results: list[tuple[str, int]] = []
                for event_source, raw_code, resolved_at in pvp_outcomes:
                    code = int(raw_code)
                    source_actor_id = bytes(event_source)
                    forward = database.execute(
                        "SELECT outcome, realm_day_id FROM pvp_leases "
                        "WHERE admitted = 1 "
                        "AND status = 1 AND outcome IS NOT NULL "
                        "AND source_actor_id = ? "
                        "AND target_actor_id = ? AND resolved_at = ?",
                        (source_actor_id, actor_id, int(resolved_at)),
                    ).fetchall()
                    reverse = database.execute(
                        "SELECT outcome, realm_day_id FROM pvp_leases "
                        "WHERE admitted = 1 "
                        "AND status = 1 AND outcome IS NOT NULL "
                        "AND source_actor_id = ? "
                        "AND target_actor_id = ? AND resolved_at = ?",
                        (actor_id, source_actor_id, int(resolved_at)),
                    ).fetchall()
                    matches = [
                        ("forward", int(match[0]), int(match[1]))
                        for match in forward
                    ] + [
                        ("reverse", int(match[0]), int(match[1]))
                        for match in reverse
                    ]
                    if len(matches) != 1:
                        database.rollback()
                        return "invalid", current_revision
                    role, outcome, lease_day_id = matches[0]
                    semantic = {
                        ("forward", 0, 0): "win",
                        ("forward", 1, 1): "loss",
                        ("reverse", 0, 1): "loss",
                        ("reverse", 1, 2): "win",
                        # A cursor-proven pre-code-2 source win remains safe
                        # to consume once; restart reconciliation rejects it
                        # while pending and records it once already consumed.
                        ("reverse", 1, 1): "win",
                    }.get((role, outcome, code))
                    if semantic is None:
                        database.rollback()
                        return "invalid", current_revision
                    pvp_results.append((semantic, lease_day_id))
                wins_required = min(
                    0xFFFF,
                    prior_record.player.pvp_wins
                    + sum(
                        semantic == "win"
                        for semantic, _lease_day in pvp_results
                    ),
                )
                losses_required = min(
                    0xFFFF,
                    prior_record.player.pvp_losses
                    + sum(
                        semantic == "loss"
                        for semantic, _lease_day in pvp_results
                    ),
                )
                knockout_required = any(
                    semantic == "loss"
                    and not (
                        int(row[1]) < realm_day_id
                        and lease_day_id <= int(row[1])
                    )
                    for semantic, lease_day_id in pvp_results
                )
                if (
                    candidate.player.pvp_wins != wins_required
                    or candidate.player.pvp_losses != losses_required
                    or (
                        knockout_required
                        and candidate.player.hit_points != 0
                    )
                ):
                    database.rollback()
                    return "invalid", current_revision

            anchor_to_write: tuple[int, int, bytes] | None = None
            if adoption_grant_id is not None or prior_snapshot is None:
                anchor_to_write = (realm_day_id, realm_day_id, snapshot)
            else:
                anchor_row = database.execute(
                    "SELECT realm_day_id, base_last_day_id, snapshot, "
                    "snapshot_sha256 FROM realm_day_anchors WHERE actor_id = ?",
                    (actor_id,),
                ).fetchone()
                if anchor_row is not None and int(anchor_row[0]) > realm_day_id:
                    database.rollback()
                    raise RuntimeError("realm day anchor is newer than the hub clock")
                if anchor_row is not None and int(anchor_row[0]) == realm_day_id:
                    anchor_snapshot = bytes(anchor_row[2])
                    if hashlib.sha256(anchor_snapshot).digest() != bytes(anchor_row[3]):
                        database.rollback()
                        raise RuntimeError("realm day anchor failed SHA-256")
                    anchor_base_day = int(anchor_row[1])
                else:
                    anchor_snapshot = prior_snapshot
                    anchor_base_day = int(row[1])
                    anchor_to_write = (
                        realm_day_id,
                        anchor_base_day,
                        anchor_snapshot,
                    )
                try:
                    anchor = decode_lord_sync(
                        anchor_snapshot,
                        expected_actor_id=actor_id,
                        allow_adoption_bridge=True,
                    )
                    validate_lord_transition(
                        anchor,
                        candidate,
                        realm_day_advanced=anchor_base_day < realm_day_id,
                    )
                except ValueError:
                    database.rollback()
                    return "invalid", current_revision
            new_revision = current_revision + 1
            now = int(time.time())
            if adoption_grant_id is not None and prior_snapshot is not None:
                database.execute(
                    "INSERT INTO archived_heads(actor_id, revision, last_day_id, "
                    "snapshot, snapshot_sha256, reason, created_at) "
                    "VALUES(?, ?, ?, ?, ?, 'adopt-local', ?)",
                    (
                        actor_id,
                        current_revision,
                        int(row[1]),
                        prior_snapshot,
                        hashlib.sha256(prior_snapshot).digest(),
                        now,
                    ),
                )
            if anchor_to_write is not None:
                anchor_day, anchor_base_day, anchor_snapshot = anchor_to_write
                database.execute(
                    "INSERT INTO realm_day_anchors(actor_id, realm_day_id, "
                    "base_last_day_id, snapshot, snapshot_sha256, created_at) "
                    "VALUES(?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(actor_id) DO UPDATE SET "
                    "realm_day_id=excluded.realm_day_id, "
                    "base_last_day_id=excluded.base_last_day_id, "
                    "snapshot=excluded.snapshot, "
                    "snapshot_sha256=excluded.snapshot_sha256, "
                    "created_at=excluded.created_at",
                    (
                        actor_id,
                        anchor_day,
                        anchor_base_day,
                        anchor_snapshot,
                        hashlib.sha256(anchor_snapshot).digest(),
                        now,
                    ),
                )
            if economy_event_cursor is not None:
                database.execute(
                    "UPDATE realm_economy_events SET applied_revision = ? "
                    "WHERE actor_id = ? AND applied_revision IS NULL "
                    "AND event_id <= ? AND event_id IN (SELECT event_id "
                    "FROM realm_events WHERE admitted = 1)",
                    (new_revision, actor_id, economy_event_cursor),
                )
            database.execute(
                "UPDATE realm_events SET committed_at = ? WHERE "
                "target_actor_id = ? AND event_id <= ? "
                "AND admitted = 1 AND committed_at IS NULL",
                (now, actor_id, candidate.last_realm_event_id),
            )
            database.execute(
                "UPDATE heads SET revision = ?, last_day_id = ?, snapshot = ?, "
                "snapshot_crc32 = ?, updated_at = ? WHERE actor_id = ?",
                (new_revision, realm_day_id, snapshot, body_crc32, now, actor_id),
            )
            database.execute(
                "INSERT INTO operations(actor_id, nonce, body_crc32, "
                "body_sha256, revision, created_at) "
                "VALUES(?, ?, ?, ?, ?, ?)",
                (actor_id, nonce, body_crc32, body_sha256,
                 new_revision, now),
            )
            database.execute(
                "INSERT INTO events(actor_id, kind, revision, created_at) "
                "VALUES(?, 'snapshot', ?, ?)",
                (actor_id, new_revision, now),
            )
            projected = profile_projection(candidate)
            projected_chompcoin = (
                projected.chompcoin + remaining_chompcoin_delta
            )
            projected_bank = projected.bank + remaining_bank_delta
            if (
                not 0 <= projected_chompcoin <= MAX_CURRENCY
                or not 0 <= projected_bank <= MAX_CURRENCY
            ):
                database.rollback()
                return "invalid", current_revision
            self._write_profile(
                database,
                RealmProfile(
                    actor_id=actor_id,
                    name=projected.name,
                    hero_style=projected.hero_style,
                    hero_class=projected.hero_class,
                    level=projected.level,
                    flags=projected.flags,
                    hit_points=projected.hit_points,
                    max_hit_points=projected.max_hit_points,
                    strength=projected.strength,
                    defense=projected.defense,
                    experience=projected.experience,
                    chompcoin=projected_chompcoin,
                    pvp_wins=projected.pvp_wins,
                    pvp_losses=projected.pvp_losses,
                    dragon_kills=projected.dragon_kills,
                    online=True,
                    bank=projected_bank,
                ),
                now,
            )
            if adoption_grant_id is not None:
                consumed = database.execute(
                    "UPDATE adoption_grants SET consumed_at = ?, "
                    "consumed_revision = ? WHERE grant_id = ? AND actor_id = ? "
                    "AND consumed_at IS NULL",
                    (now, new_revision, adoption_grant_id, actor_id),
                )
                if consumed.rowcount != 1:
                    database.rollback()
                    return "invalid", current_revision
            database.commit()
            return "ok", new_revision

    @staticmethod
    def validate_profile(profile: RealmProfile) -> None:
        encoded_name = profile.name.encode("ascii", errors="strict")
        if (
            len(profile.actor_id) != 16
            or not 3 <= len(encoded_name) < 20
            or any(value < 0x20 or value > 0x7E for value in encoded_name)
            or profile.hero_style not in (0, 1)
            or profile.hero_class not in (0, 1, 2)
            or not 1 <= profile.level <= 12
            or profile.flags & ~0x03
            or profile.max_hit_points <= 0
            or not 0 <= profile.hit_points <= profile.max_hit_points
            or profile.strength <= 0
            or profile.defense < 0
            or profile.dragon_kills > 0xFF
            or min(
                profile.experience,
                profile.chompcoin,
                profile.pvp_wins,
                profile.pvp_losses,
                profile.dragon_kills,
                profile.bank,
            )
            < 0
        ):
            raise ValueError("invalid LORD realm profile")

    @classmethod
    def _effective_alive(
        cls,
        database: sqlite3.Connection,
        actor_id: bytes,
        *,
        decoded_alive: bool | None = None,
    ) -> bool:
        """Return decoded-head life after pending PvP knockouts are applied."""
        if decoded_alive is None:
            row = database.execute(
                "SELECT snapshot FROM heads WHERE actor_id = ? "
                "AND snapshot IS NOT NULL",
                (actor_id,),
            ).fetchone()
            if row is None:
                return False
            try:
                decoded = decode_lord_sync(
                    bytes(row[0]),
                    expected_actor_id=actor_id,
                    allow_adoption_bridge=True,
                )
            except ValueError:
                return False
            decoded_alive = (profile_projection(decoded).flags & 0x01) != 0
        if not decoded_alive:
            return False
        pending_knockout = database.execute(
            "SELECT 1 FROM realm_events WHERE target_actor_id = ? "
            "AND kind = ? AND code = 1 AND admitted = 1 "
            "AND committed_at IS NULL LIMIT 1",
            (actor_id, p4rm.ACTION_PVP_RESOLVE),
        ).fetchone()
        return pending_knockout is None

    @classmethod
    def _write_profile(
        cls,
        database: sqlite3.Connection,
        profile: RealmProfile,
        now: int,
    ) -> None:
        """Write a validated projection inside the caller's transaction."""
        if not cls._effective_alive(
            database,
            profile.actor_id,
            decoded_alive=(profile.flags & 0x01) != 0,
        ):
            profile = dataclasses.replace(profile, flags=profile.flags & ~0x01)
        cls.validate_profile(profile)
        database.execute(
            """
            INSERT INTO profiles(
                actor_id, name, hero_style, hero_class, level, flags,
                hit_points, max_hit_points, strength, defense, experience,
                chompcoin, pvp_wins, pvp_losses, dragon_kills, bank, last_seen
            ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(actor_id) DO UPDATE SET
                name=excluded.name,
                hero_style=excluded.hero_style,
                hero_class=excluded.hero_class,
                level=excluded.level,
                flags=excluded.flags,
                hit_points=excluded.hit_points,
                max_hit_points=excluded.max_hit_points,
                strength=excluded.strength,
                defense=excluded.defense,
                experience=excluded.experience,
                chompcoin=excluded.chompcoin,
                pvp_wins=excluded.pvp_wins,
                pvp_losses=excluded.pvp_losses,
                dragon_kills=excluded.dragon_kills,
                bank=excluded.bank,
                last_seen=excluded.last_seen
            """,
            (
                profile.actor_id,
                profile.name,
                profile.hero_style,
                profile.hero_class,
                profile.level,
                profile.flags,
                profile.hit_points,
                profile.max_hit_points,
                profile.strength,
                profile.defense,
                profile.experience,
                profile.chompcoin,
                profile.pvp_wins,
                profile.pvp_losses,
                profile.dragon_kills,
                profile.bank,
                now,
            ),
        )

    @classmethod
    def _rebuild_authoritative_profiles(
        cls, database: sqlite3.Connection
    ) -> None:
        """Rebuild protected directory state from heads on every startup.

        Older hubs accepted client-authored profile packets.  Keeping those
        rows across an upgrade would let an inflated legacy balance survive
        even though the accepted character head is sound.  Pending server
        economy events are part of the authoritative balance too, so replay
        their unapplied deltas over the head projection before publishing it.
        Presence and the at-inn hint are deliberately reset on restart.
        """
        rows = database.execute(
            "SELECT actor_id, snapshot FROM heads WHERE snapshot IS NOT NULL"
        ).fetchall()
        for actor_value, snapshot_value in rows:
            actor_id = bytes(actor_value)
            try:
                snapshot = decode_lord_sync(
                    bytes(snapshot_value),
                    expected_actor_id=actor_id,
                    allow_adoption_bridge=True,
                )
            except ValueError as error:
                raise RuntimeError(
                    "refusing to publish a malformed authoritative LORD head "
                    f"for actor {actor_id.hex()}"
                ) from error
            projected = profile_projection(snapshot)
            pending = database.execute(
                "SELECT COALESCE(SUM(chompcoin_delta), 0), "
                "COALESCE(SUM(bank_delta), 0) "
                "FROM realm_economy_events AS ledger JOIN realm_events AS event "
                "ON event.event_id = ledger.event_id WHERE ledger.actor_id = ? "
                "AND ledger.applied_revision IS NULL AND event.admitted = 1",
                (actor_id,),
            ).fetchone()
            chompcoin = projected.chompcoin + int(pending[0])
            bank = projected.bank + int(pending[1])
            if not 0 <= chompcoin <= MAX_CURRENCY or not 0 <= bank <= MAX_CURRENCY:
                raise RuntimeError(
                    "refusing to publish an invalid pending LORD economy "
                    f"balance for actor {actor_id.hex()}"
                )
            cls._write_profile(
                database,
                RealmProfile(
                    actor_id=actor_id,
                    name=projected.name,
                    hero_style=projected.hero_style,
                    hero_class=projected.hero_class,
                    level=projected.level,
                    flags=projected.flags,
                    hit_points=projected.hit_points,
                    max_hit_points=projected.max_hit_points,
                    strength=projected.strength,
                    defense=projected.defense,
                    experience=projected.experience,
                    chompcoin=chompcoin,
                    pvp_wins=projected.pvp_wins,
                    pvp_losses=projected.pvp_losses,
                    dragon_kills=projected.dragon_kills,
                    online=False,
                    bank=bank,
                ),
                0,
            )

    def update_profile(self, profile: RealmProfile) -> None:
        """Legacy compatibility hook; protected profile fields are ignored."""
        self.validate_profile(profile)
        self.update_presence(
            profile.actor_id, at_inn=(profile.flags & 0x02) != 0
        )

    def touch_profile(self, actor_id: bytes) -> None:
        if len(actor_id) != 16:
            raise ValueError("actor ID must be 16 bytes")
        with self._lock, self._connect() as database:
            database.execute(
                "UPDATE profiles SET last_seen = ? WHERE actor_id = ?",
                (int(time.time()), actor_id),
            )

    def update_presence(self, actor_id: bytes, *, at_inn: bool) -> None:
        """Update only ephemeral presence; protected fields remain projected."""
        if len(actor_id) != 16:
            raise ValueError("actor ID must be 16 bytes")
        with self._lock, self._connect() as database:
            database.execute(
                "UPDATE profiles SET flags = (flags & 1) | ?, last_seen = ? "
                "WHERE actor_id = ?",
                (2 if at_inn else 0, int(time.time()), actor_id),
            )

    def list_profiles(
        self, *, exclude_actor_id: bytes, limit: int = 8, offset: int = 0
    ) -> list[RealmProfile]:
        if (
            len(exclude_actor_id) != 16
            or not 1 <= limit <= 8
            or not 0 <= offset < MAX_REALM_PLAYERS
        ):
            raise ValueError("invalid directory query")
        now = int(time.time())
        with self._lock, self._connect() as database:
            rows = database.execute(
                """
                SELECT p.actor_id, p.name, p.hero_style, p.hero_class, p.level,
                       p.flags, p.hit_points, p.max_hit_points, p.strength,
                       p.defense, p.experience, p.chompcoin, p.pvp_wins,
                       p.pvp_losses, p.last_seen, p.bank,
                       p.dragon_kills,
                       COALESCE((
                           SELECT trust FROM friendships
                           WHERE source_actor_id = ?
                             AND target_actor_id = p.actor_id
                             AND admitted = 1
                       ), 0),
                       EXISTS(
                           SELECT 1 FROM teams
                           WHERE active = 1 AND admitted = 1 AND (
                               (actor_low = ? AND actor_high = p.actor_id)
                               OR
                               (actor_high = ? AND actor_low = p.actor_id)
                           )
                       ),
                       COALESCE(gm.guild_id, 0),
                       COALESCE(g.name_code, 0)
                FROM profiles AS p
                JOIN heads AS h ON h.actor_id = p.actor_id
                               AND h.snapshot IS NOT NULL
                LEFT JOIN guild_members AS gm ON gm.actor_id = p.actor_id
                LEFT JOIN guilds AS g ON g.guild_id = gm.guild_id
                WHERE p.actor_id != ?
                ORDER BY p.dragon_kills DESC, p.level DESC,
                         p.experience DESC, p.pvp_wins DESC,
                         p.pvp_losses ASC, p.name COLLATE NOCASE ASC,
                         p.actor_id ASC
                LIMIT ? OFFSET ?
                """,
                (
                    exclude_actor_id,
                    exclude_actor_id,
                    exclude_actor_id,
                    exclude_actor_id,
                    limit,
                    offset,
                ),
            ).fetchall()
        return [
            RealmProfile(
                actor_id=bytes(row[0]),
                name=str(row[1]),
                hero_style=int(row[2]),
                hero_class=int(row[3]),
                level=int(row[4]),
                flags=int(row[5]),
                hit_points=int(row[6]),
                max_hit_points=int(row[7]),
                strength=int(row[8]),
                defense=int(row[9]),
                experience=int(row[10]),
                chompcoin=int(row[11]),
                pvp_wins=int(row[12]),
                pvp_losses=int(row[13]),
                online=now - int(row[14]) <= 90,
                bank=int(row[15]),
                dragon_kills=int(row[16]),
                trust=int(row[17]),
                teamed=bool(row[18]),
                guild_id=int(row[19]),
                guild_name_code=int(row[20]),
            )
            for row in rows
        ]

    def profile_count(self, *, exclude_actor_id: bytes) -> int:
        if len(exclude_actor_id) != 16:
            raise ValueError("actor ID must be 16 bytes")
        with self._lock, self._connect() as database:
            return int(
                database.execute(
                    "SELECT COUNT(*) FROM profiles AS p JOIN heads AS h "
                    "ON h.actor_id = p.actor_id AND h.snapshot IS NOT NULL "
                    "WHERE p.actor_id != ?",
                    (exclude_actor_id,),
                ).fetchone()[0]
            )

    @staticmethod
    def _action_request_hash(
        kind: int,
        code: int,
        value: int,
        target_actor_id: bytes,
        body: bytes,
    ) -> bytes:
        return hashlib.sha256(
            bytes((kind, code))
            + value.to_bytes(2, "little")
            + target_actor_id
            + len(body).to_bytes(2, "little")
            + body
        ).digest()

    @staticmethod
    def _profile_name(database: sqlite3.Connection, actor_id: bytes) -> str | None:
        row = database.execute(
            "SELECT p.name FROM profiles AS p JOIN heads AS h "
            "ON h.actor_id = p.actor_id AND h.snapshot IS NOT NULL "
            "WHERE p.actor_id = ?",
            (actor_id,),
        ).fetchone()
        return None if row is None else str(row[0])

    @staticmethod
    def _queue_event(
        database: sqlite3.Connection,
        target_actor_id: bytes,
        source_actor_id: bytes,
        kind: int,
        code: int,
        value: int,
        body: bytes,
        now: int,
    ) -> int:
        cursor = database.execute(
            "INSERT INTO realm_events(target_actor_id, source_actor_id, kind, "
            "code, value, body, created_at, acknowledged_at, admitted) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, NULL, 1)",
            (target_actor_id, source_actor_id, kind, code, value, body, now),
        )
        return int(cursor.lastrowid)

    @staticmethod
    def _pair(first: bytes, second: bytes) -> tuple[bytes, bytes]:
        return (first, second) if first < second else (second, first)

    @staticmethod
    def _guild_season_id(realm_day_id: int) -> int:
        return (realm_day_id - 1) // GUILD_SEASON_DAYS

    @classmethod
    def _roll_guild_calendar(
        cls, database: sqlite3.Connection, realm_day_id: int
    ) -> None:
        season_id = cls._guild_season_id(realm_day_id)
        database.execute(
            "UPDATE guilds SET season_id = ?, season_points = 0 "
            "WHERE season_id < ?",
            (season_id, season_id),
        )
        database.execute(
            "UPDATE guilds SET quest_day = ?, quest_complete = 0 "
            "WHERE quest_day < ?",
            (realm_day_id, realm_day_id),
        )

    @staticmethod
    def _guild_membership(
        database: sqlite3.Connection, actor_id: bytes
    ) -> tuple[int, int, int, int] | None:
        row = database.execute(
            "SELECT gm.guild_id, gm.joined_day, gm.role, g.name_code "
            "FROM guild_members AS gm JOIN guilds AS g "
            "ON g.guild_id = gm.guild_id WHERE gm.actor_id = ?",
            (actor_id,),
        ).fetchone()
        if row is None:
            return None
        return int(row[0]), int(row[1]), int(row[2]), int(row[3])

    @staticmethod
    def _add_guild_points(
        database: sqlite3.Connection, guild_id: int, points: int
    ) -> None:
        database.execute(
            "UPDATE guilds SET prestige = MIN(4294967295, prestige + ?), "
            "season_points = MIN(4294967295, season_points + ?) "
            "WHERE guild_id = ?",
            (points, points, guild_id),
        )

    @staticmethod
    def _guild_roster_metrics(
        database: sqlite3.Connection, guild_id: int, realm_day_id: int
    ) -> tuple[int, int, int | None, int]:
        """Return average power, normalized participation, route, roster size.

        Power is derived only from the hub's last accepted profile: four per
        level, ten per dragon deed, plus at most fifty recorded PvP wins.
        Same-day participation contributes 0..12 regardless of club size.
        A tied route ballot has no majority and therefore no RPS bonus.
        """
        rows = database.execute(
            "SELECT p.level, p.dragon_kills, p.pvp_wins, r.route "
            "FROM guild_members AS gm JOIN profiles AS p "
            "ON p.actor_id = gm.actor_id JOIN heads AS h "
            "ON h.actor_id = p.actor_id AND h.snapshot IS NOT NULL "
            "LEFT JOIN guild_rallies AS r ON r.actor_id = gm.actor_id "
            "AND r.realm_day_id = ? AND r.guild_id = gm.guild_id "
            "WHERE gm.guild_id = ? AND gm.joined_day < ? "
            "ORDER BY gm.actor_id",
            (realm_day_id, guild_id, realm_day_id),
        ).fetchall()
        if not rows:
            return 0, 0, None, 0
        powers = [
            int(row[0]) * 4
            + int(row[1]) * 10
            + min(50, int(row[2]))
            for row in rows
        ]
        rallied = [int(row[3]) for row in rows if row[3] is not None]
        participation = (
            GUILD_CLASH_PARTICIPATION_POINTS * len(rallied) + len(rows) // 2
        ) // len(rows)
        counts = [rallied.count(route) for route in range(3)]
        maximum = max(counts, default=0)
        route = (
            counts.index(maximum)
            if maximum > 0 and counts.count(maximum) == 1
            else None
        )
        return sum(powers) // len(powers), participation, route, len(rows)

    @staticmethod
    def _guild_clash_rolls(
        realm_day_id: int,
        source_guild_id: int,
        target_guild_id: int,
    ) -> tuple[int, int]:
        low = min(source_guild_id, target_guild_id)
        high = max(source_guild_id, target_guild_id)
        digest = hashlib.sha256(
            b"p4-lord-clash-v1"
            + realm_day_id.to_bytes(8, "little")
            + low.to_bytes(4, "little")
            + high.to_bytes(4, "little")
        ).digest()
        low_roll = digest[0] % GUILD_CLASH_ROLL_SPAN - 3
        high_roll = digest[1] % GUILD_CLASH_ROLL_SPAN - 3
        if source_guild_id == low:
            return low_roll, high_roll
        return high_roll, low_roll

    def guild_status(self, actor_id: bytes) -> GuildStatus:
        if len(actor_id) != 16 or actor_id == b"\0" * 16:
            raise ValueError("actor ID must be nonzero and 16 bytes")
        with self._lock, self._connect() as database:
            database.execute("BEGIN IMMEDIATE")
            realm_day_id, _ = self.realm_clock()
            if self._profile_name(database, actor_id) is None:
                database.rollback()
                raise ValueError("guild status requires an accepted actor")
            self._roll_guild_calendar(database, realm_day_id)
            membership = self._guild_membership(database, actor_id)
            if membership is None:
                database.commit()
                return GuildStatus(actor_id=actor_id)
            guild_id, joined_day, role, name_code = membership
            row = database.execute(
                "SELECT prestige, season_points, banner_stars, quest_progress, "
                "wins, losses, draws, last_outcome, last_opponent_code, "
                "(SELECT COUNT(*) FROM guild_members WHERE guild_id = g.guild_id) "
                "FROM guilds AS g WHERE guild_id = ?",
                (guild_id,),
            ).fetchone()
            if row is None:
                database.rollback()
                raise RuntimeError("guild membership references no club")
            daily_flags = 0
            if database.execute(
                "SELECT 1 FROM guild_rallies WHERE actor_id = ? "
                "AND realm_day_id = ?",
                (actor_id, realm_day_id),
            ).fetchone() is not None:
                daily_flags |= p4rm.GUILD_DAILY_RALLIED
            if database.execute(
                "SELECT 1 FROM guild_clashes WHERE source_guild_id = ? "
                "AND realm_day_id = ?",
                (guild_id, realm_day_id),
            ).fetchone() is not None:
                daily_flags |= p4rm.GUILD_DAILY_OUTGOING
            if database.execute(
                "SELECT 1 FROM guild_cheers WHERE source_guild_id = ? "
                "AND realm_day_id = ?",
                (guild_id, realm_day_id),
            ).fetchone() is not None:
                daily_flags |= p4rm.GUILD_DAILY_CHEERED
            if joined_day < realm_day_id:
                daily_flags |= p4rm.GUILD_DAILY_ELIGIBLE
            database.commit()
        return GuildStatus(
            actor_id=actor_id,
            guild_id=guild_id,
            name_code=name_code,
            members=min(p4rm.GUILD_MAX_MEMBERS, int(row[9])),
            role=role,
            prestige=min(0xFFFFFFFF, int(row[0])),
            season_points=min(0xFFFFFFFF, int(row[1])),
            banner_stars=min(0xFFFF, int(row[2])),
            quest_progress=min(p4rm.GUILD_QUEST_GOAL, int(row[3])),
            wins=min(0xFFFF, int(row[4])),
            losses=min(0xFFFF, int(row[5])),
            draws=min(0xFFFF, int(row[6])),
            last_outcome=int(row[7]),
            last_opponent_code=int(row[8]),
            daily_flags=daily_flags,
        )

    def list_guilds(self, *, limit: int = 8, offset: int = 0) -> list[GuildSummary]:
        if not 1 <= limit <= 8 or not 0 <= offset <= p4rm.GUILD_NAME_COUNT:
            raise ValueError("invalid guild directory query")
        with self._lock, self._connect() as database:
            database.execute("BEGIN IMMEDIATE")
            realm_day_id, _ = self.realm_clock()
            self._roll_guild_calendar(database, realm_day_id)
            rows = database.execute(
                "SELECT g.guild_id, g.name_code, COUNT(gm.actor_id), "
                "g.banner_stars, g.prestige, g.season_points, g.wins, "
                "g.losses, g.draws FROM guilds AS g JOIN guild_members AS gm "
                "ON gm.guild_id = g.guild_id GROUP BY g.guild_id "
                "ORDER BY g.season_points DESC, g.prestige DESC, "
                "g.banner_stars DESC, g.name_code ASC LIMIT ? OFFSET ?",
                (limit, offset),
            ).fetchall()
            database.commit()
        return [
            GuildSummary(
                guild_id=int(row[0]),
                name_code=int(row[1]),
                members=min(p4rm.GUILD_MAX_MEMBERS, int(row[2])),
                banner_stars=min(0xFF, int(row[3])),
                prestige=min(0xFFFFFFFF, int(row[4])),
                season_points=min(0xFFFFFFFF, int(row[5])),
                wins=min(0xFFFF, int(row[6])),
                losses=min(0xFFFF, int(row[7])),
                draws=min(0xFFFF, int(row[8])),
            )
            for row in rows
        ]

    def guild_count(self) -> int:
        with self._lock, self._connect() as database:
            return int(database.execute("SELECT COUNT(*) FROM guilds").fetchone()[0])

    def _perform_guild_action(
        self,
        database: sqlite3.Connection,
        source_actor_id: bytes,
        code: int,
        value: int,
        target_actor_id: bytes,
        body: bytes,
        now: int,
    ) -> RealmActionResult:
        if code not in (
            p4rm.GUILD_CREATE,
            p4rm.GUILD_JOIN,
            p4rm.GUILD_LEAVE,
            p4rm.GUILD_RALLY,
            p4rm.GUILD_CLASH,
            p4rm.GUILD_CHEER,
        ) or body:
            return RealmActionResult(p4rm.ACTION_INVALID)
        realm_day_id, _ = self.realm_clock()
        self._roll_guild_calendar(database, realm_day_id)
        source = self._guild_membership(database, source_actor_id)
        target = self._guild_membership(database, target_actor_id)
        targetless = code in (
            p4rm.GUILD_CREATE,
            p4rm.GUILD_LEAVE,
            p4rm.GUILD_RALLY,
        )
        if targetless and target_actor_id != b"\0" * 16:
            return RealmActionResult(p4rm.ACTION_INVALID)
        if not targetless and target_actor_id == b"\0" * 16:
            return RealmActionResult(p4rm.ACTION_NOT_FOUND)

        if code == p4rm.GUILD_CREATE:
            if not 1 <= value <= p4rm.GUILD_NAME_COUNT:
                return RealmActionResult(p4rm.ACTION_INVALID)
            cooldown = database.execute(
                "SELECT left_day FROM guild_rejoin_cooldowns WHERE actor_id = ?",
                (source_actor_id,),
            ).fetchone()
            if source is not None:
                return RealmActionResult(p4rm.ACTION_DENIED)
            if cooldown is not None and realm_day_id <= int(cooldown[0]):
                return RealmActionResult(p4rm.ACTION_DENIED)
            if database.execute(
                "SELECT 1 FROM guilds WHERE name_code = ?", (value,)
            ).fetchone() is not None:
                return RealmActionResult(p4rm.ACTION_DENIED)
            cursor = database.execute(
                "INSERT INTO guilds(name_code, leader_actor_id, created_day, "
                "season_id, quest_day) VALUES(?, ?, ?, ?, ?)",
                (
                    value,
                    source_actor_id,
                    realm_day_id,
                    self._guild_season_id(realm_day_id),
                    realm_day_id,
                ),
            )
            guild_id = int(cursor.lastrowid)
            database.execute(
                "INSERT INTO guild_members(actor_id, guild_id, joined_day, role) "
                "VALUES(?, ?, ?, ?)",
                (source_actor_id, guild_id, realm_day_id, p4rm.GUILD_ROLE_LEADER),
            )
            return RealmActionResult(
                p4rm.ACTION_OK, code=code, value=value, related_id=guild_id
            )

        if code == p4rm.GUILD_JOIN:
            if value != 0:
                return RealmActionResult(p4rm.ACTION_INVALID)
            if source is not None:
                return RealmActionResult(p4rm.ACTION_DENIED)
            if target is None:
                return RealmActionResult(p4rm.ACTION_NOT_FOUND)
            cooldown = database.execute(
                "SELECT left_day FROM guild_rejoin_cooldowns WHERE actor_id = ?",
                (source_actor_id,),
            ).fetchone()
            if cooldown is not None and realm_day_id <= int(cooldown[0]):
                return RealmActionResult(p4rm.ACTION_DENIED)
            members = int(
                database.execute(
                    "SELECT COUNT(*) FROM guild_members WHERE guild_id = ?",
                    (target[0],),
                ).fetchone()[0]
            )
            if members >= p4rm.GUILD_MAX_MEMBERS:
                return RealmActionResult(p4rm.ACTION_BUSY)
            database.execute(
                "INSERT INTO guild_members(actor_id, guild_id, joined_day, role) "
                "VALUES(?, ?, ?, ?)",
                (source_actor_id, target[0], realm_day_id, p4rm.GUILD_ROLE_MEMBER),
            )
            return RealmActionResult(
                p4rm.ACTION_OK, code=code, value=target[3], related_id=target[0]
            )

        if code == p4rm.GUILD_LEAVE:
            if value != 0:
                return RealmActionResult(p4rm.ACTION_INVALID)
            if source is None:
                return RealmActionResult(p4rm.ACTION_NOT_FOUND)
            guild_id, _, role, _ = source
            database.execute(
                "DELETE FROM guild_members WHERE actor_id = ?", (source_actor_id,)
            )
            database.execute(
                "INSERT INTO guild_rejoin_cooldowns(actor_id, left_day) "
                "VALUES(?, ?) ON CONFLICT(actor_id) DO UPDATE SET "
                "left_day = excluded.left_day",
                (source_actor_id, realm_day_id),
            )
            successor = database.execute(
                "SELECT actor_id FROM guild_members WHERE guild_id = ? "
                "ORDER BY joined_day ASC, actor_id ASC LIMIT 1",
                (guild_id,),
            ).fetchone()
            if successor is None:
                database.execute("DELETE FROM guilds WHERE guild_id = ?", (guild_id,))
            elif role == p4rm.GUILD_ROLE_LEADER:
                successor_id = bytes(successor[0])
                database.execute(
                    "UPDATE guild_members SET role = ? WHERE actor_id = ?",
                    (p4rm.GUILD_ROLE_LEADER, successor_id),
                )
                database.execute(
                    "UPDATE guilds SET leader_actor_id = ? WHERE guild_id = ?",
                    (successor_id, guild_id),
                )
            return RealmActionResult(
                p4rm.ACTION_OK, code=code, related_id=guild_id
            )

        if source is None:
            return RealmActionResult(p4rm.ACTION_NOT_FOUND)
        guild_id, joined_day, _, _ = source
        if joined_day >= realm_day_id:
            return RealmActionResult(p4rm.ACTION_DENIED)

        if code == p4rm.GUILD_RALLY:
            if not 0 <= value <= 2:
                return RealmActionResult(p4rm.ACTION_INVALID)
            if database.execute(
                "SELECT 1 FROM guild_rallies WHERE actor_id = ? "
                "AND realm_day_id = ?",
                (source_actor_id, realm_day_id),
            ).fetchone() is not None:
                return RealmActionResult(p4rm.ACTION_DENIED)
            profile = database.execute(
                "SELECT hero_class, level, dragon_kills FROM profiles "
                "WHERE actor_id = ?",
                (source_actor_id,),
            ).fetchone()
            if profile is None:
                return RealmActionResult(p4rm.ACTION_NOT_FOUND)
            points = (
                2
                + int(int(profile[0]) == value)
                + int(int(profile[1]) >= 6)
                + int(int(profile[2]) > 0)
            )
            database.execute(
                "INSERT INTO guild_rallies(actor_id, realm_day_id, guild_id, "
                "route, points) VALUES(?, ?, ?, ?, ?)",
                (source_actor_id, realm_day_id, guild_id, value, points),
            )
            quest = database.execute(
                "SELECT quest_progress, quest_complete FROM guilds "
                "WHERE guild_id = ?",
                (guild_id,),
            ).fetchone()
            previous = int(quest[0])
            complete = int(quest[1]) != 0
            accumulated = previous + points
            bonus = (
                10
                if not complete and accumulated >= p4rm.GUILD_QUEST_GOAL
                else 0
            )
            if bonus:
                progress = min(
                    p4rm.GUILD_QUEST_GOAL - 1,
                    accumulated - p4rm.GUILD_QUEST_GOAL,
                )
            elif complete:
                progress = min(p4rm.GUILD_QUEST_GOAL - 1, accumulated)
            else:
                progress = accumulated
            database.execute(
                "UPDATE guilds SET quest_progress = ?, quest_complete = ?, "
                "banner_stars = MIN(65535, banner_stars + ?) WHERE guild_id = ?",
                (progress, int(complete or bonus != 0), int(bonus != 0), guild_id),
            )
            award = points + bonus
            self._add_guild_points(database, guild_id, award)
            return RealmActionResult(
                p4rm.ACTION_OK, code=code, value=award, related_id=guild_id
            )

        if target is None:
            return RealmActionResult(p4rm.ACTION_NOT_FOUND)
        target_guild_id, _, _, target_name_code = target
        if target_guild_id == guild_id:
            return RealmActionResult(p4rm.ACTION_DENIED)

        if code == p4rm.GUILD_CHEER:
            if value != 0:
                return RealmActionResult(p4rm.ACTION_INVALID)
            if database.execute(
                "SELECT 1 FROM guild_cheers WHERE source_guild_id = ? "
                "AND realm_day_id = ?",
                (guild_id, realm_day_id),
            ).fetchone() is not None:
                return RealmActionResult(p4rm.ACTION_DENIED)
            database.execute(
                "INSERT INTO guild_cheers(source_guild_id, realm_day_id, "
                "target_guild_id, actor_id, created_at) VALUES(?, ?, ?, ?, ?)",
                (guild_id, realm_day_id, target_guild_id, source_actor_id, now),
            )
            self._add_guild_points(database, guild_id, 1)
            self._add_guild_points(database, target_guild_id, 2)
            return RealmActionResult(
                p4rm.ACTION_OK, code=code, value=3, related_id=target_guild_id
            )

        if code != p4rm.GUILD_CLASH or value != 0:
            return RealmActionResult(p4rm.ACTION_INVALID)
        if database.execute(
            "SELECT 1 FROM guild_clashes WHERE source_guild_id = ? "
            "AND realm_day_id = ?",
            (guild_id, realm_day_id),
        ).fetchone() is not None:
            return RealmActionResult(p4rm.ACTION_DENIED)
        if database.execute(
            "SELECT 1 FROM guild_clashes WHERE target_guild_id = ? "
            "AND realm_day_id = ?",
            (target_guild_id, realm_day_id),
        ).fetchone() is not None:
            return RealmActionResult(p4rm.ACTION_DENIED)
        pair_low = min(guild_id, target_guild_id)
        pair_high = max(guild_id, target_guild_id)
        previous = database.execute(
            "SELECT realm_day_id FROM guild_clashes WHERE pair_low = ? "
            "AND pair_high = ? ORDER BY realm_day_id DESC LIMIT 1",
            (pair_low, pair_high),
        ).fetchone()
        if previous is not None and realm_day_id - int(previous[0]) < (
            GUILD_CLASH_PAIR_COOLDOWN_DAYS
        ):
            return RealmActionResult(p4rm.ACTION_DENIED)
        source_power, source_participation, source_route, source_roster = (
            self._guild_roster_metrics(database, guild_id, realm_day_id)
        )
        target_power, target_participation, target_route, target_roster = (
            self._guild_roster_metrics(database, target_guild_id, realm_day_id)
        )
        if source_roster == 0 or target_roster == 0:
            return RealmActionResult(p4rm.ACTION_DENIED)
        source_route_bonus = 0
        target_route_bonus = 0
        if source_route is not None and target_route is not None:
            if (source_route - target_route) % 3 == 1:
                source_route_bonus = GUILD_CLASH_ROUTE_BONUS
            elif (target_route - source_route) % 3 == 1:
                target_route_bonus = GUILD_CLASH_ROUTE_BONUS
        source_roll, target_roll = self._guild_clash_rolls(
            realm_day_id, guild_id, target_guild_id
        )
        source_score = max(
            0,
            source_power
            + source_participation
            + source_route_bonus
            + source_roll,
        )
        target_score = max(
            0,
            target_power
            + target_participation
            + target_route_bonus
            + target_roll,
        )
        if source_score > target_score:
            outcome = p4rm.GUILD_OUTCOME_WIN
            source_award, target_award = 6, 3
            source_column, target_column = "wins", "losses"
            target_outcome = p4rm.GUILD_OUTCOME_LOSS
        elif source_score < target_score:
            outcome = p4rm.GUILD_OUTCOME_LOSS
            source_award, target_award = 3, 6
            source_column, target_column = "losses", "wins"
            target_outcome = p4rm.GUILD_OUTCOME_WIN
        else:
            outcome = p4rm.GUILD_OUTCOME_DRAW
            source_award = target_award = 4
            source_column = target_column = "draws"
            target_outcome = p4rm.GUILD_OUTCOME_DRAW
        cursor = database.execute(
            "INSERT INTO guild_clashes(realm_day_id, source_guild_id, "
            "target_guild_id, pair_low, pair_high, source_score, target_score, "
            "outcome, created_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (
                realm_day_id,
                guild_id,
                target_guild_id,
                pair_low,
                pair_high,
                source_score,
                target_score,
                outcome,
                now,
            ),
        )
        self._add_guild_points(database, guild_id, source_award)
        self._add_guild_points(database, target_guild_id, target_award)
        database.execute(
            f"UPDATE guilds SET {source_column} = MIN(65535, "
            f"{source_column} + 1), last_outcome = ?, "
            "last_opponent_code = ? WHERE guild_id = ?",
            (outcome, target_name_code, guild_id),
        )
        database.execute(
            f"UPDATE guilds SET {target_column} = MIN(65535, "
            f"{target_column} + 1), last_outcome = ?, "
            "last_opponent_code = ? WHERE guild_id = ?",
            (target_outcome, source[3], target_guild_id),
        )
        return RealmActionResult(
            p4rm.ACTION_OK,
            code=code,
            value=outcome,
            related_id=int(cursor.lastrowid),
        )

    def perform_action(
        self,
        source_actor_id: bytes,
        nonce: int,
        kind: int,
        code: int,
        value: int,
        target_actor_id: bytes,
        body: bytes,
    ) -> RealmActionResult:
        if (
            len(source_actor_id) != 16
            or len(target_actor_id) != 16
            or not 1 <= nonce <= p4rm.MAX_ACTION_NONCE
            or not p4rm.ACTION_MAIL <= kind <= p4rm.ACTION_GUILD
            or not 0 <= code <= 0xFF
            or not 0 <= value <= 0xFFFF
            or len(body) > p4rm.MAX_PAYLOAD_BYTES
        ):
            raise ValueError("invalid realm action")
        request_hash = self._action_request_hash(
            kind, code, value, target_actor_id, body
        )
        if (
            kind in (p4rm.ACTION_MAIL, p4rm.ACTION_TAVERN, p4rm.ACTION_NEWS)
            and (len(body) > 47 or
                 (body and any(byte < 0x20 or byte > 0x7E for byte in body)))
        ):
            return RealmActionResult(p4rm.ACTION_INVALID)
        with self._lock, self._connect() as database:
            database.execute("BEGIN IMMEDIATE")
            prior = database.execute(
                "SELECT request_sha256, status, result_code, result_value, related_id "
                ", admitted FROM action_operations "
                "WHERE source_actor_id = ? AND nonce = ?",
                (source_actor_id, nonce),
            ).fetchone()
            if prior is not None:
                if int(prior[5]) != 1:
                    database.rollback()
                    return RealmActionResult(p4rm.ACTION_INVALID)
                if bytes(prior[0]) != request_hash:
                    database.rollback()
                    return RealmActionResult(p4rm.ACTION_INVALID)
                database.commit()
                return RealmActionResult(
                    int(prior[1]), int(prior[2]), int(prior[3]), int(prior[4])
                )

            now = int(time.time())
            source_name = self._profile_name(database, source_actor_id)
            target_name = self._profile_name(database, target_actor_id)
            result = RealmActionResult(p4rm.ACTION_INVALID)
            target_required = (
                code in (p4rm.GUILD_JOIN, p4rm.GUILD_CLASH, p4rm.GUILD_CHEER)
                if kind == p4rm.ACTION_GUILD
                else kind not in (p4rm.ACTION_TAVERN, p4rm.ACTION_NEWS)
            )
            friendship_action = kind in (
                p4rm.ACTION_FRIEND,
                p4rm.ACTION_TEAM,
                p4rm.ACTION_MENTOR,
            )
            action_day_id = 0
            friendship_actions_used = 0
            if friendship_action:
                action_day_id, _ = self.realm_clock()
                usage = database.execute(
                    "SELECT friendship_actions FROM realm_daily_action_usage "
                    "WHERE actor_id = ? AND realm_day_id = ?",
                    (source_actor_id, action_day_id),
                ).fetchone()
                friendship_actions_used = 0 if usage is None else int(usage[0])
            if source_name is None:
                result = RealmActionResult(p4rm.ACTION_NOT_FOUND)
            elif target_required and (
                target_name is None
                or target_actor_id == source_actor_id
                or target_actor_id == b"\0" * 16
            ):
                result = RealmActionResult(p4rm.ACTION_NOT_FOUND)
            elif (
                friendship_action
                and friendship_actions_used
                >= MAX_FRIENDSHIP_ACTIONS_PER_REALM_DAY
            ):
                result = RealmActionResult(p4rm.ACTION_DENIED)
            elif kind == p4rm.ACTION_MAIL:
                if code != 0 or value != 0 or not body:
                    result = RealmActionResult(p4rm.ACTION_INVALID)
                else:
                    self._queue_event(
                        database, target_actor_id, source_actor_id,
                        kind, code, 0, body, now,
                    )
                    result = RealmActionResult(p4rm.ACTION_OK)
            elif kind == p4rm.ACTION_TRANSFER:
                if code != 0 or value != 100 or body:
                    result = RealmActionResult(p4rm.ACTION_INVALID)
                else:
                    balance = database.execute(
                        "SELECT bank FROM profiles WHERE actor_id = ?",
                        (source_actor_id,),
                    ).fetchone()
                    target_balance = database.execute(
                        "SELECT chompcoin FROM profiles WHERE actor_id = ?",
                        (target_actor_id,),
                    ).fetchone()
                    if (
                        balance is None
                        or int(balance[0]) < value
                        or target_balance is None
                        or int(target_balance[0]) > MAX_CURRENCY - value
                    ):
                        result = RealmActionResult(p4rm.ACTION_DENIED)
                    else:
                        database.execute(
                            "UPDATE profiles SET bank = bank - ? "
                            "WHERE actor_id = ?",
                            (value, source_actor_id),
                        )
                        database.execute(
                            "UPDATE profiles SET chompcoin = chompcoin + ? "
                            "WHERE actor_id = ?",
                            (value, target_actor_id),
                        )
                        source_event_id = self._queue_event(
                            database, source_actor_id, target_actor_id,
                            kind, 1, value, b"", now,
                        )
                        target_event_id = self._queue_event(
                            database, target_actor_id, source_actor_id,
                            kind, 0, value, b"", now,
                        )
                        database.execute(
                            "INSERT INTO realm_economy_events(event_id, actor_id, "
                            "chompcoin_delta, bank_delta, applied_revision, "
                            "created_at) VALUES(?, ?, 0, ?, NULL, ?)",
                            (source_event_id, source_actor_id, -value, now),
                        )
                        database.execute(
                            "INSERT INTO realm_economy_events(event_id, actor_id, "
                            "chompcoin_delta, bank_delta, applied_revision, "
                            "created_at) VALUES(?, ?, ?, 0, NULL, ?)",
                            (target_event_id, target_actor_id, value, now),
                        )
                        result = RealmActionResult(p4rm.ACTION_OK, value=value)
            elif kind == p4rm.ACTION_FRIEND:
                gain = 15 if code == 0 else 30 if code == 1 else 0
                if gain == 0 or value != 0 or body:
                    result = RealmActionResult(p4rm.ACTION_INVALID)
                else:
                    source_coin = database.execute(
                        "SELECT chompcoin FROM profiles WHERE actor_id = ?",
                        (source_actor_id,),
                    ).fetchone()
                    if (
                        code == 1
                        and (source_coin is None or int(source_coin[0]) < 100)
                    ):
                        result = RealmActionResult(p4rm.ACTION_DENIED)
                    else:
                        database.execute(
                            "INSERT INTO friendships(source_actor_id, "
                            "target_actor_id, trust, updated_at, admitted) "
                            "VALUES(?, ?, ?, ?, 1) "
                            "ON CONFLICT(source_actor_id, target_actor_id) "
                            "DO UPDATE SET trust=CASE WHEN "
                            "friendships.admitted = 1 THEN MIN(100, "
                            "friendships.trust + excluded.trust) ELSE "
                            "excluded.trust END, "
                            "updated_at=excluded.updated_at, admitted=1",
                            (source_actor_id, target_actor_id, gain, now),
                        )
                        self._queue_event(
                            database,
                            target_actor_id,
                            source_actor_id,
                            kind,
                            code,
                            gain,
                            b"",
                            now,
                        )
                        source_event_id = self._queue_event(
                            database,
                            source_actor_id,
                            target_actor_id,
                            kind,
                            code | 0x80,
                            gain,
                            b"",
                            now,
                        )
                        if code == 1:
                            database.execute(
                                "UPDATE profiles SET chompcoin = chompcoin - 100 "
                                "WHERE actor_id = ?",
                                (source_actor_id,),
                            )
                            database.execute(
                                "INSERT INTO realm_economy_events(event_id, "
                                "actor_id, chompcoin_delta, bank_delta, "
                                "applied_revision, created_at) "
                                "VALUES(?, ?, -100, 0, NULL, ?)",
                                (source_event_id, source_actor_id, now),
                            )
                        result = RealmActionResult(
                            p4rm.ACTION_OK, code=code, value=gain
                        )
            elif kind == p4rm.ACTION_TEAM:
                if code != 0 or value != 0 or body:
                    result = RealmActionResult(p4rm.ACTION_INVALID)
                else:
                    low, high = self._pair(source_actor_id, target_actor_id)
                    active = database.execute(
                        "SELECT active FROM teams WHERE actor_low = ? "
                        "AND actor_high = ? AND admitted = 1",
                        (low, high),
                    ).fetchone()
                    if active is not None and int(active[0]) != 0:
                        database.execute(
                            "UPDATE teams SET active = 0, updated_at = ? "
                            "WHERE actor_low = ? AND actor_high = ? "
                            "AND admitted = 1",
                            (now, low, high),
                        )
                        database.execute(
                            "DELETE FROM team_invites WHERE "
                            "admitted = 1 AND ((source_actor_id = ? "
                            "AND target_actor_id = ?) OR "
                            "(source_actor_id = ? AND target_actor_id = ?))",
                            (source_actor_id, target_actor_id,
                             target_actor_id, source_actor_id),
                        )
                        self._queue_event(
                            database, target_actor_id, source_actor_id,
                            kind, 2, 0, b"", now,
                        )
                        self._queue_event(
                            database, source_actor_id, target_actor_id,
                            kind, 0x82, 0, b"", now,
                        )
                        result = RealmActionResult(p4rm.ACTION_OK, code=2)
                    else:
                        other_team = database.execute(
                            "SELECT 1 FROM teams WHERE active = 1 AND "
                            "admitted = 1 AND "
                            "(actor_low IN (?, ?) OR actor_high IN (?, ?)) AND "
                            "NOT (actor_low = ? AND actor_high = ?)",
                            (source_actor_id, target_actor_id,
                             source_actor_id, target_actor_id, low, high),
                        ).fetchone()
                        trust = database.execute(
                            "SELECT trust FROM friendships WHERE "
                            "source_actor_id = ? AND target_actor_id = ? "
                            "AND admitted = 1",
                            (source_actor_id, target_actor_id),
                        ).fetchone()
                        if other_team is not None:
                            result = RealmActionResult(p4rm.ACTION_BUSY)
                        elif trust is None or int(trust[0]) < 60:
                            result = RealmActionResult(p4rm.ACTION_DENIED)
                        else:
                            reciprocal = database.execute(
                                "SELECT 1 FROM team_invites WHERE "
                                "source_actor_id = ? AND target_actor_id = ? "
                                "AND admitted = 1",
                                (target_actor_id, source_actor_id),
                            ).fetchone()
                            if reciprocal is None:
                                database.execute(
                                    "INSERT OR REPLACE INTO team_invites("
                                    "source_actor_id, target_actor_id, created_at, "
                                    "admitted) VALUES(?, ?, ?, 1)",
                                    (source_actor_id, target_actor_id, now),
                                )
                                self._queue_event(
                                    database, target_actor_id, source_actor_id,
                                    kind, 0, 0, b"", now,
                                )
                                self._queue_event(
                                    database, source_actor_id, target_actor_id,
                                    kind, 0x80, 0, b"", now,
                                )
                                result = RealmActionResult(p4rm.ACTION_OK, code=0)
                            else:
                                database.execute(
                                    "INSERT INTO teams(actor_low, actor_high, active, "
                                    "updated_at, admitted) VALUES(?, ?, 1, ?, 1) "
                                    "ON CONFLICT(actor_low, actor_high) DO UPDATE SET "
                                    "active=1, updated_at=excluded.updated_at, "
                                    "admitted=1",
                                    (low, high, now),
                                )
                                database.execute(
                                    "DELETE FROM team_invites WHERE "
                                    "admitted = 1 AND ((source_actor_id = ? "
                                    "AND target_actor_id = ?) OR "
                                    "(source_actor_id = ? AND target_actor_id = ?))",
                                    (source_actor_id, target_actor_id,
                                     target_actor_id, source_actor_id),
                                )
                                self._queue_event(
                                    database, target_actor_id, source_actor_id,
                                    kind, 1, 0, b"", now,
                                )
                                self._queue_event(
                                    database, source_actor_id, target_actor_id,
                                    kind, 0x81, 0, b"", now,
                                )
                                result = RealmActionResult(p4rm.ACTION_OK, code=1)
            elif kind == p4rm.ACTION_MENTOR:
                low, high = self._pair(source_actor_id, target_actor_id)
                active = database.execute(
                    "SELECT active FROM teams WHERE actor_low = ? "
                    "AND actor_high = ? AND admitted = 1",
                    (low, high),
                ).fetchone()
                if code != 0 or value != 0 or body:
                    result = RealmActionResult(p4rm.ACTION_INVALID)
                elif active is None or int(active[0]) == 0:
                    result = RealmActionResult(p4rm.ACTION_DENIED)
                else:
                    self._queue_event(
                        database, target_actor_id, source_actor_id,
                        kind, 0, 0, b"", now,
                    )
                    self._queue_event(
                        database, source_actor_id, target_actor_id,
                        kind, 1, 0, b"", now,
                    )
                    result = RealmActionResult(p4rm.ACTION_OK)
            elif kind == p4rm.ACTION_PVP_BEGIN:
                day_id, _ = self.realm_clock()
                database.execute(
                    "UPDATE pvp_leases SET status = 1, resolved_at = ? "
                    "WHERE admitted = 1 AND status = 0 AND created_at < ?",
                    (now, now - 600),
                )
                busy = database.execute(
                    "SELECT 1 FROM pvp_leases WHERE admitted = 1 "
                    "AND status = 0 AND "
                    "realm_day_id = ? AND (source_actor_id IN (?, ?) OR "
                    "target_actor_id IN (?, ?))",
                    (day_id, source_actor_id, target_actor_id,
                     source_actor_id, target_actor_id),
                ).fetchone()
                fights_used = int(
                    database.execute(
                        "SELECT COUNT(*) FROM pvp_leases WHERE "
                        "source_actor_id = ? AND realm_day_id = ? "
                        "AND admitted = 1",
                        (source_actor_id, day_id),
                    ).fetchone()[0]
                )
                target_state = database.execute(
                    "SELECT flags FROM profiles WHERE actor_id = ?",
                    (target_actor_id,),
                ).fetchone()
                target_flags = (
                    0 if target_state is None else int(target_state[0])
                )
                source_alive = self._effective_alive(
                    database, source_actor_id
                )
                target_alive = self._effective_alive(
                    database, target_actor_id
                )
                source_guild = self._guild_membership(database, source_actor_id)
                target_guild = self._guild_membership(database, target_actor_id)
                same_guild = (
                    source_guild is not None
                    and target_guild is not None
                    and source_guild[0] == target_guild[0]
                )
                if code not in (0, 1) or value != 0 or body or busy is not None:
                    result = RealmActionResult(
                        p4rm.ACTION_BUSY if busy is not None else p4rm.ACTION_INVALID
                    )
                elif fights_used >= MAX_PVP_FIGHTS_PER_REALM_DAY:
                    result = RealmActionResult(p4rm.ACTION_DENIED)
                elif same_guild:
                    result = RealmActionResult(p4rm.ACTION_DENIED)
                elif (
                    not source_alive
                    or not target_alive
                    or (code == 1 and (target_flags & 0x02) == 0)
                ):
                    result = RealmActionResult(p4rm.ACTION_DENIED)
                else:
                    cursor = database.execute(
                        "INSERT INTO pvp_leases(source_actor_id, target_actor_id, "
                        "realm_day_id, status, created_at, admitted) "
                        "VALUES(?, ?, ?, 0, ?, 1)",
                        (source_actor_id, target_actor_id, day_id, now),
                    )
                    result = RealmActionResult(
                        p4rm.ACTION_OK, related_id=int(cursor.lastrowid)
                    )
            elif kind == p4rm.ACTION_PVP_RESOLVE:
                if len(body) != 9 or code not in (0, 1) or value != 0:
                    result = RealmActionResult(p4rm.ACTION_INVALID)
                else:
                    lease_id = int.from_bytes(body[:8], "little")
                    outcome = body[8]
                    lease = database.execute(
                        "SELECT source_actor_id, target_actor_id, status, "
                        "created_at FROM "
                        "pvp_leases WHERE lease_id = ? AND admitted = 1",
                        (lease_id,),
                    ).fetchone()
                    source_guild = self._guild_membership(
                        database, source_actor_id
                    )
                    target_guild = self._guild_membership(
                        database, target_actor_id
                    )
                    current_same_guild = (
                        source_guild is not None
                        and target_guild is not None
                        and source_guild[0] == target_guild[0]
                    )
                    if (
                        outcome not in (0, 1)
                        or outcome != code
                        or lease is None
                        or bytes(lease[0]) != source_actor_id
                        or bytes(lease[1]) != target_actor_id
                    ):
                        result = RealmActionResult(p4rm.ACTION_NOT_FOUND)
                    elif int(lease[2]) != 0:
                        result = RealmActionResult(p4rm.ACTION_DENIED)
                    elif int(lease[3]) < now - 600:
                        # Enforce expiry at settlement as well as at the next
                        # lease admission.  This transaction atomically closes
                        # the stale lease before any reward can be calculated.
                        database.execute(
                            "UPDATE pvp_leases SET status = 1, outcome = NULL, "
                            "resolved_at = ? WHERE lease_id = ? "
                            "AND admitted = 1 AND status = 0",
                            (now, lease_id),
                        )
                        result = RealmActionResult(p4rm.ACTION_DENIED)
                    elif current_same_guild:
                        # Membership is authoritative at settlement time too.
                        # Cancel the open lease so a new nonce cannot revive it
                        # after the club relationship changes again.  A canceled
                        # lease produces no events, economy rows, or player stats.
                        database.execute(
                            "UPDATE pvp_leases SET status = 1, outcome = NULL, "
                            "resolved_at = ? WHERE lease_id = ? "
                            "AND admitted = 1 AND status = 0",
                            (now, lease_id),
                        )
                        result = RealmActionResult(p4rm.ACTION_DENIED)
                    else:
                        prize = 0
                        if outcome == 1:
                            target_balance = database.execute(
                                "SELECT chompcoin FROM profiles WHERE actor_id = ?",
                                (target_actor_id,),
                            ).fetchone()
                            source_balance = database.execute(
                                "SELECT chompcoin FROM profiles WHERE actor_id = ?",
                                (source_actor_id,),
                            ).fetchone()
                            available = (
                                0
                                if target_balance is None
                                else int(target_balance[0]) // 2
                            )
                            capacity = (
                                0
                                if source_balance is None
                                else MAX_CURRENCY - int(source_balance[0])
                            )
                            prize = min(
                                available,
                                max(0, capacity),
                                MAX_WEALTH_GAIN_FLAT,
                            )
                            if prize:
                                database.execute(
                                    "UPDATE profiles SET chompcoin = "
                                    "chompcoin - ? WHERE actor_id = ?",
                                    (prize, target_actor_id),
                                )
                                database.execute(
                                    "UPDATE profiles SET chompcoin = "
                                    "chompcoin + ? WHERE actor_id = ?",
                                    (prize, source_actor_id),
                                )
                        database.execute(
                            "UPDATE pvp_leases SET status = 1, outcome = ?, "
                            "resolved_at = ? WHERE lease_id = ? "
                            "AND admitted = 1",
                            (outcome, now, lease_id),
                        )
                        target_event_id = self._queue_event(
                            database, target_actor_id, source_actor_id,
                            kind, outcome, prize, b"", now,
                        )
                        # Both duelists learn their own durable outcome from
                        # an event.  The cartridge removes every provisional
                        # local reward before asking us to resolve, so even a
                        # zero-prize win needs code 2 to award one PvP win.
                        source_event_id = self._queue_event(
                            database,
                            source_actor_id,
                            target_actor_id,
                            kind,
                            2 if outcome == 1 else 1,
                            prize if outcome == 1 else 0,
                            b"",
                            now,
                        )
                        loser_actor_id = (
                            target_actor_id if outcome == 1 else source_actor_id
                        )
                        database.execute(
                            "UPDATE profiles SET flags = flags & ~1 "
                            "WHERE actor_id = ?",
                            (loser_actor_id,),
                        )
                        if outcome == 1:
                            database.execute(
                                "INSERT INTO realm_economy_events(event_id, "
                                "actor_id, chompcoin_delta, bank_delta, "
                                "applied_revision, created_at) "
                                "VALUES(?, ?, ?, 0, NULL, ?)",
                                (target_event_id, target_actor_id, -prize, now),
                            )
                            database.execute(
                                "INSERT INTO realm_economy_events(event_id, "
                                "actor_id, chompcoin_delta, bank_delta, "
                                "applied_revision, created_at) "
                                "VALUES(?, ?, ?, 0, NULL, ?)",
                                (source_event_id, source_actor_id, prize, now),
                            )
                        else:
                            # Code 1 is the current source-loss wire event.
                            # Keep an exact zero ledger row so restart-time
                            # provenance cannot confuse it with legacy code 1.
                            database.execute(
                                "INSERT INTO realm_economy_events(event_id, "
                                "actor_id, chompcoin_delta, bank_delta, "
                                "applied_revision, created_at) "
                                "VALUES(?, ?, 0, 0, NULL, ?)",
                                (source_event_id, source_actor_id, now),
                            )
                        result = RealmActionResult(
                            p4rm.ACTION_OK, code=outcome,
                            value=0, related_id=lease_id,
                        )
            elif kind in (p4rm.ACTION_TAVERN, p4rm.ACTION_NEWS):
                if code != 0 or value != 0 or not body:
                    result = RealmActionResult(p4rm.ACTION_INVALID)
                else:
                    database.execute(
                        "INSERT INTO realm_feed(source_actor_id, kind, body, "
                        "created_at) VALUES(?, ?, ?, ?)",
                        (source_actor_id, kind, body, now),
                    )
                    rows = database.execute(
                        "SELECT p.actor_id FROM profiles AS p JOIN heads AS h "
                        "ON h.actor_id = p.actor_id AND h.snapshot IS NOT NULL "
                        "WHERE p.actor_id != ?",
                        (source_actor_id,),
                    ).fetchall()
                    for row in rows:
                        self._queue_event(
                            database, bytes(row[0]), source_actor_id,
                            kind, 0, 0, body, now,
                        )
                    result = RealmActionResult(p4rm.ACTION_OK)
            elif kind == p4rm.ACTION_GUILD:
                result = self._perform_guild_action(
                    database,
                    source_actor_id,
                    code,
                    value,
                    target_actor_id,
                    body,
                    now,
                )

            if friendship_action and result.status == p4rm.ACTION_OK:
                database.execute(
                    "INSERT INTO realm_daily_action_usage(actor_id, "
                    "realm_day_id, friendship_actions) VALUES(?, ?, 1) "
                    "ON CONFLICT(actor_id, realm_day_id) DO UPDATE SET "
                    "friendship_actions = friendship_actions + 1",
                    (source_actor_id, action_day_id),
                )
            database.execute(
                "INSERT INTO action_operations(source_actor_id, nonce, "
                "request_sha256, status, result_code, result_value, related_id, "
                "created_at, admitted) VALUES(?, ?, ?, ?, ?, ?, ?, ?, 1)",
                (source_actor_id, nonce, request_hash, result.status,
                 result.code, result.value, result.related_id, now),
            )
            database.commit()
            return result

    def next_event(
        self, actor_id: bytes, *, after_event_id: int = 0
    ) -> RealmEvent | None:
        if len(actor_id) != 16 or after_event_id < 0:
            raise ValueError("actor ID must be 16 bytes")
        with self._lock, self._connect() as database:
            row = database.execute(
                "SELECT e.event_id, e.target_actor_id, e.source_actor_id, "
                "p.name, e.kind, e.code, e.value, e.body "
                "FROM realm_events e JOIN profiles p ON "
                "p.actor_id = e.source_actor_id JOIN heads source_head ON "
                "source_head.actor_id = p.actor_id "
                "AND source_head.snapshot IS NOT NULL "
                "JOIN heads target_head ON "
                "target_head.actor_id = e.target_actor_id "
                "AND target_head.snapshot IS NOT NULL "
                "WHERE e.target_actor_id = ? AND e.committed_at IS NULL "
                "AND e.admitted = 1 AND e.event_id > ? "
                "ORDER BY e.event_id ASC LIMIT 1",
                (actor_id, after_event_id),
            ).fetchone()
        if row is None:
            return None
        return RealmEvent(
            int(row[0]), bytes(row[1]), bytes(row[2]), str(row[3]),
            int(row[4]), int(row[5]), int(row[6]), bytes(row[7]),
        )

    def acknowledge_event(self, actor_id: bytes, event_id: int) -> bool:
        """Record wire receipt; snapshot commit is the durable acknowledgement."""
        if len(actor_id) != 16 or event_id <= 0:
            raise ValueError("invalid event acknowledgement")
        with self._lock, self._connect() as database:
            cursor = database.execute(
                "UPDATE realm_events SET receipt_at = COALESCE(receipt_at, ?) WHERE "
                "event_id = ? AND target_actor_id = ? AND admitted = 1 "
                "AND EXISTS(SELECT 1 FROM heads WHERE actor_id = ? "
                "AND snapshot IS NOT NULL)",
                (int(time.time()), event_id, actor_id, actor_id),
            )
            return cursor.rowcount == 1
