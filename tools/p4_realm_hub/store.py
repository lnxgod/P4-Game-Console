"""SQLite persistence and hourly realm clock for the Mac hub."""

from __future__ import annotations

import dataclasses
import hashlib
import sqlite3
import threading
import time
import zlib
from pathlib import Path


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
                    last_seen INTEGER NOT NULL
                );
                """
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
                    chompcoin, pvp_wins, pvp_losses, last_seen
                ) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
                       experience, chompcoin, pvp_wins, pvp_losses, last_seen
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
            )
            for row in rows
        ]
