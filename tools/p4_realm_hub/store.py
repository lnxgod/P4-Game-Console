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
    online: bool
    bank: int = 0


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
    def __init__(self, path: str | Path, *, epoch_seconds: int = 0) -> None:
        self.path = str(path)
        self.epoch_seconds = epoch_seconds
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
                    acknowledged_at INTEGER
                );
                CREATE INDEX IF NOT EXISTS realm_events_pending
                    ON realm_events(target_actor_id, acknowledged_at, event_id);
                CREATE TABLE IF NOT EXISTS friendships (
                    source_actor_id BLOB NOT NULL,
                    target_actor_id BLOB NOT NULL,
                    trust INTEGER NOT NULL CHECK(trust BETWEEN 0 AND 100),
                    updated_at INTEGER NOT NULL,
                    PRIMARY KEY(source_actor_id, target_actor_id)
                );
                CREATE TABLE IF NOT EXISTS team_invites (
                    source_actor_id BLOB NOT NULL,
                    target_actor_id BLOB NOT NULL,
                    created_at INTEGER NOT NULL,
                    PRIMARY KEY(source_actor_id, target_actor_id)
                );
                CREATE TABLE IF NOT EXISTS teams (
                    actor_low BLOB NOT NULL,
                    actor_high BLOB NOT NULL,
                    active INTEGER NOT NULL CHECK(active IN (0, 1)),
                    updated_at INTEGER NOT NULL,
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
                    resolved_at INTEGER
                );
                CREATE TABLE IF NOT EXISTS realm_feed (
                    feed_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    source_actor_id BLOB NOT NULL,
                    kind INTEGER NOT NULL,
                    body BLOB NOT NULL,
                    created_at INTEGER NOT NULL
                );
                """
            )
            profile_columns = {
                str(row[1]) for row in database.execute("PRAGMA table_info(profiles)")
            }
            if "bank" not in profile_columns:
                database.execute(
                    "ALTER TABLE profiles ADD COLUMN bank INTEGER NOT NULL DEFAULT 0"
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
    ) -> tuple[str, int]:
        if len(actor_id) != 16 or not snapshot or nonce == 0 or realm_day_id == 0:
            raise ValueError("invalid realm commit")
        body_crc32 = zlib.crc32(snapshot) & 0xFFFFFFFF
        with self._lock, self._connect() as database:
            database.execute("BEGIN IMMEDIATE")
            prior = database.execute(
                "SELECT body_crc32, revision FROM operations "
                "WHERE actor_id = ? AND nonce = ?",
                (actor_id, nonce),
            ).fetchone()
            if prior is not None:
                if int(prior[0]) != body_crc32:
                    database.rollback()
                    return "invalid", int(prior[1])
                database.commit()
                return "ok", int(prior[1])
            row = database.execute(
                "SELECT revision, last_day_id FROM heads WHERE actor_id = ?",
                (actor_id,),
            ).fetchone()
            if row is None:
                database.rollback()
                raise KeyError("unknown actor")
            current_revision = int(row[0])
            if expected_revision != current_revision:
                database.rollback()
                return "conflict", current_revision
            current_day, _ = self.realm_clock()
            if realm_day_id != current_day:
                database.rollback()
                return "conflict", current_revision
            new_revision = current_revision + 1
            now = int(time.time())
            database.execute(
                "UPDATE heads SET revision = ?, last_day_id = ?, snapshot = ?, "
                "snapshot_crc32 = ?, updated_at = ? WHERE actor_id = ?",
                (new_revision, realm_day_id, snapshot, body_crc32, now, actor_id),
            )
            database.execute(
                "INSERT INTO operations(actor_id, nonce, body_crc32, revision, "
                "created_at) VALUES(?, ?, ?, ?, ?)",
                (actor_id, nonce, body_crc32, new_revision, now),
            )
            database.execute(
                "INSERT INTO events(actor_id, kind, revision, created_at) "
                "VALUES(?, 'snapshot', ?, ?)",
                (actor_id, new_revision, now),
            )
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
            or min(
                profile.experience,
                profile.chompcoin,
                profile.pvp_wins,
                profile.pvp_losses,
                profile.bank,
            )
            < 0
        ):
            raise ValueError("invalid LORD realm profile")

    def update_profile(self, profile: RealmProfile) -> None:
        self.validate_profile(profile)
        now = int(time.time())
        with self._lock, self._connect() as database:
            database.execute(
                """
                INSERT INTO profiles(
                    actor_id, name, hero_style, hero_class, level, flags,
                    hit_points, max_hit_points, strength, defense, experience,
                    chompcoin, pvp_wins, pvp_losses, bank, last_seen
                ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
                    profile.bank,
                    now,
                ),
            )

    def touch_profile(self, actor_id: bytes) -> None:
        if len(actor_id) != 16:
            raise ValueError("actor ID must be 16 bytes")
        with self._lock, self._connect() as database:
            database.execute(
                "UPDATE profiles SET last_seen = ? WHERE actor_id = ?",
                (int(time.time()), actor_id),
            )

    def list_profiles(
        self, *, exclude_actor_id: bytes, limit: int = 8
    ) -> list[RealmProfile]:
        if len(exclude_actor_id) != 16 or not 1 <= limit <= 8:
            raise ValueError("invalid directory query")
        now = int(time.time())
        with self._lock, self._connect() as database:
            rows = database.execute(
                """
                SELECT actor_id, name, hero_style, hero_class, level, flags,
                       hit_points, max_hit_points, strength, defense,
                       experience, chompcoin, pvp_wins, pvp_losses, last_seen,
                       bank
                FROM profiles
                WHERE actor_id != ?
                ORDER BY last_seen DESC, name COLLATE NOCASE ASC
                LIMIT ?
                """,
                (exclude_actor_id, limit),
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
            )
            for row in rows
        ]

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
            "SELECT name FROM profiles WHERE actor_id = ?", (actor_id,)
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
            "code, value, body, created_at, acknowledged_at) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, NULL)",
            (target_actor_id, source_actor_id, kind, code, value, body, now),
        )
        return int(cursor.lastrowid)

    @staticmethod
    def _pair(first: bytes, second: bytes) -> tuple[bytes, bytes]:
        return (first, second) if first < second else (second, first)

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
            or nonce == 0
            or not p4rm.ACTION_MAIL <= kind <= p4rm.ACTION_NEWS
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
                "FROM action_operations WHERE source_actor_id = ? AND nonce = ?",
                (source_actor_id, nonce),
            ).fetchone()
            if prior is not None:
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
            target_required = kind not in (p4rm.ACTION_TAVERN, p4rm.ACTION_NEWS)
            if source_name is None:
                result = RealmActionResult(p4rm.ACTION_NOT_FOUND)
            elif target_required and (
                target_name is None
                or target_actor_id == source_actor_id
                or target_actor_id == b"\0" * 16
            ):
                result = RealmActionResult(p4rm.ACTION_NOT_FOUND)
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
                    if balance is None or int(balance[0]) < value:
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
                        self._queue_event(
                            database, source_actor_id, target_actor_id,
                            kind, 1, value, b"", now,
                        )
                        self._queue_event(
                            database, target_actor_id, source_actor_id,
                            kind, 0, value, b"", now,
                        )
                        result = RealmActionResult(p4rm.ACTION_OK, value=value)
            elif kind == p4rm.ACTION_FRIEND:
                gain = 15 if code == 0 else 30 if code == 1 else 0
                if gain == 0 or value != 0 or body:
                    result = RealmActionResult(p4rm.ACTION_INVALID)
                else:
                    database.execute(
                        "INSERT INTO friendships(source_actor_id, target_actor_id, "
                        "trust, updated_at) VALUES(?, ?, ?, ?) "
                        "ON CONFLICT(source_actor_id, target_actor_id) DO UPDATE SET "
                        "trust=MIN(100, friendships.trust + excluded.trust), "
                        "updated_at=excluded.updated_at",
                        (source_actor_id, target_actor_id, gain, now),
                    )
                    self._queue_event(
                        database, target_actor_id, source_actor_id,
                        kind, code, gain, b"", now,
                    )
                    self._queue_event(
                        database, source_actor_id, target_actor_id,
                        kind, code | 0x80, gain, b"", now,
                    )
                    result = RealmActionResult(p4rm.ACTION_OK, code=code, value=gain)
            elif kind == p4rm.ACTION_TEAM:
                if code != 0 or value != 0 or body:
                    result = RealmActionResult(p4rm.ACTION_INVALID)
                else:
                    low, high = self._pair(source_actor_id, target_actor_id)
                    active = database.execute(
                        "SELECT active FROM teams WHERE actor_low = ? AND actor_high = ?",
                        (low, high),
                    ).fetchone()
                    if active is not None and int(active[0]) != 0:
                        database.execute(
                            "UPDATE teams SET active = 0, updated_at = ? "
                            "WHERE actor_low = ? AND actor_high = ?",
                            (now, low, high),
                        )
                        database.execute(
                            "DELETE FROM team_invites WHERE "
                            "(source_actor_id = ? AND target_actor_id = ?) OR "
                            "(source_actor_id = ? AND target_actor_id = ?)",
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
                            "(actor_low IN (?, ?) OR actor_high IN (?, ?)) AND "
                            "NOT (actor_low = ? AND actor_high = ?)",
                            (source_actor_id, target_actor_id,
                             source_actor_id, target_actor_id, low, high),
                        ).fetchone()
                        trust = database.execute(
                            "SELECT trust FROM friendships WHERE "
                            "source_actor_id = ? AND target_actor_id = ?",
                            (source_actor_id, target_actor_id),
                        ).fetchone()
                        if other_team is not None:
                            result = RealmActionResult(p4rm.ACTION_BUSY)
                        elif trust is None or int(trust[0]) < 60:
                            result = RealmActionResult(p4rm.ACTION_DENIED)
                        else:
                            reciprocal = database.execute(
                                "SELECT 1 FROM team_invites WHERE "
                                "source_actor_id = ? AND target_actor_id = ?",
                                (target_actor_id, source_actor_id),
                            ).fetchone()
                            if reciprocal is None:
                                database.execute(
                                    "INSERT OR REPLACE INTO team_invites("
                                    "source_actor_id, target_actor_id, created_at) "
                                    "VALUES(?, ?, ?)",
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
                                    "updated_at) VALUES(?, ?, 1, ?) "
                                    "ON CONFLICT(actor_low, actor_high) DO UPDATE SET "
                                    "active=1, updated_at=excluded.updated_at",
                                    (low, high, now),
                                )
                                database.execute(
                                    "DELETE FROM team_invites WHERE "
                                    "(source_actor_id = ? AND target_actor_id = ?) OR "
                                    "(source_actor_id = ? AND target_actor_id = ?)",
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
                    "SELECT active FROM teams WHERE actor_low = ? AND actor_high = ?",
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
                    "WHERE status = 0 AND created_at < ?",
                    (now, now - 600),
                )
                busy = database.execute(
                    "SELECT 1 FROM pvp_leases WHERE status = 0 AND "
                    "realm_day_id = ? AND (source_actor_id IN (?, ?) OR "
                    "target_actor_id IN (?, ?))",
                    (day_id, source_actor_id, target_actor_id,
                     source_actor_id, target_actor_id),
                ).fetchone()
                target_alive = database.execute(
                    "SELECT flags FROM profiles WHERE actor_id = ?",
                    (target_actor_id,),
                ).fetchone()
                flags = 0 if target_alive is None else int(target_alive[0])
                if code not in (0, 1) or value != 0 or body or busy is not None:
                    result = RealmActionResult(
                        p4rm.ACTION_BUSY if busy is not None else p4rm.ACTION_INVALID
                    )
                elif (flags & 0x01) == 0 or (code == 1 and (flags & 0x02) == 0):
                    result = RealmActionResult(p4rm.ACTION_DENIED)
                else:
                    cursor = database.execute(
                        "INSERT INTO pvp_leases(source_actor_id, target_actor_id, "
                        "realm_day_id, status, created_at) VALUES(?, ?, ?, 0, ?)",
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
                        "SELECT source_actor_id, target_actor_id, status FROM "
                        "pvp_leases WHERE lease_id = ?",
                        (lease_id,),
                    ).fetchone()
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
                    else:
                        prize = 0
                        if outcome == 1:
                            row = database.execute(
                                "SELECT chompcoin FROM profiles WHERE actor_id = ?",
                                (target_actor_id,),
                            ).fetchone()
                            prize = 0 if row is None else int(row[0]) // 2
                        database.execute(
                            "UPDATE pvp_leases SET status = 1, outcome = ?, "
                            "resolved_at = ? WHERE lease_id = ?",
                            (outcome, now, lease_id),
                        )
                        self._queue_event(
                            database, target_actor_id, source_actor_id,
                            kind, outcome, prize, b"", now,
                        )
                        result = RealmActionResult(
                            p4rm.ACTION_OK, code=outcome,
                            value=prize, related_id=lease_id,
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
                        "SELECT actor_id FROM profiles WHERE actor_id != ?",
                        (source_actor_id,),
                    ).fetchall()
                    for row in rows:
                        self._queue_event(
                            database, bytes(row[0]), source_actor_id,
                            kind, 0, 0, body, now,
                        )
                    result = RealmActionResult(p4rm.ACTION_OK)

            database.execute(
                "INSERT INTO action_operations(source_actor_id, nonce, "
                "request_sha256, status, result_code, result_value, related_id, "
                "created_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?)",
                (source_actor_id, nonce, request_hash, result.status,
                 result.code, result.value, result.related_id, now),
            )
            database.commit()
            return result

    def next_event(self, actor_id: bytes) -> RealmEvent | None:
        if len(actor_id) != 16:
            raise ValueError("actor ID must be 16 bytes")
        with self._lock, self._connect() as database:
            row = database.execute(
                "SELECT e.event_id, e.target_actor_id, e.source_actor_id, "
                "p.name, e.kind, e.code, e.value, e.body "
                "FROM realm_events e JOIN profiles p ON "
                "p.actor_id = e.source_actor_id "
                "WHERE e.target_actor_id = ? AND e.acknowledged_at IS NULL "
                "ORDER BY e.event_id ASC LIMIT 1",
                (actor_id,),
            ).fetchone()
        if row is None:
            return None
        return RealmEvent(
            int(row[0]), bytes(row[1]), bytes(row[2]), str(row[3]),
            int(row[4]), int(row[5]), int(row[6]), bytes(row[7]),
        )

    def acknowledge_event(self, actor_id: bytes, event_id: int) -> bool:
        if len(actor_id) != 16 or event_id <= 0:
            raise ValueError("invalid event acknowledgement")
        with self._lock, self._connect() as database:
            cursor = database.execute(
                "UPDATE realm_events SET acknowledged_at = ? WHERE "
                "event_id = ? AND target_actor_id = ? AND acknowledged_at IS NULL",
                (int(time.time()), event_id, actor_id),
            )
            return cursor.rowcount == 1
