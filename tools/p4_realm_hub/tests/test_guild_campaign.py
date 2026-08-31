"""Thirty-day adversarial Adventure Club campaign.

This test intentionally drives only RealmStore's public actor/head/action and
club projection APIs.  Direct SQLite reads are acceptance oracles: they never
create, repair, or mutate game state.
"""

from __future__ import annotations

import hashlib
import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.p4_realm_hub import p4rm
from tools.p4_realm_hub.store import (
    GUILD_CLASH_PAIR_COOLDOWN_DAYS,
    GUILD_CLASH_PARTICIPATION_POINTS,
    GUILD_CLASH_ROLL_SPAN,
    GUILD_CLASH_ROUTE_BONUS,
    GUILD_SEASON_DAYS,
    RealmStore,
)
from tools.p4_realm_hub.tests.test_protocol import lord_record


ZERO_ACTOR = bytes(16)


class AdventureClubCampaignTests(unittest.TestCase):
    def test_fourteen_actors_survive_a_thirty_day_club_campaign(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "adventure-club-campaign.sqlite3"
            clock = [10.0]
            with mock.patch(
                "tools.p4_realm_hub.store.time.time",
                side_effect=lambda: clock[0],
            ):
                store = RealmStore(path, epoch_seconds=0)
                labels = tuple(f"P{index:02d}" for index in range(14))
                actors = {
                    label: store.actor_for_profile(f"club-{label.lower()}")
                    for label in labels
                }

                # Every accepted profile has identical clash power.  Unequal
                # club sizes therefore exercise participation normalization,
                # not a hidden level/deed/PvP advantage.
                for index, label in enumerate(labels):
                    actor = actors[label]
                    record = lord_record(
                        actor,
                        index + 1,
                        name=f"Club Hero {index:02d}",
                        hero_class=index % 3,
                        level=6,
                        hit_points=80,
                        max_hit_points=80,
                        strength=20,
                        defense=10,
                        chompcoin=5_000,
                        bank=1_000,
                        experience=16_000,
                        dragon_kills=1,
                        pvp_wins=0,
                        pvp_losses=0,
                    )
                    self.assertEqual(
                        store.commit(actor, 0, index + 1, 1, record),
                        ("ok", 1),
                    )

                personal_heads = {
                    label: (
                        store.read_head(actor).revision,
                        hashlib.sha256(store.read_head(actor).snapshot or b"").digest(),
                    )
                    for label, actor in actors.items()
                }
                with sqlite3.connect(path) as database:
                    personal_profiles = {
                        bytes(row[0]): tuple(int(value) for value in row[1:])
                        for row in database.execute(
                            "SELECT actor_id, chompcoin, bank, experience, "
                            "hit_points, max_hit_points, strength, defense, "
                            "dragon_kills, pvp_wins, pvp_losses FROM profiles"
                        )
                    }

                nonces = {label: 10_000 + index * 10_000
                          for index, label in enumerate(labels)}

                def set_day(day: int) -> None:
                    clock[0] = float((day - 1) * 3_600 + 10)
                    self.assertEqual(store.realm_clock()[0], day)

                def action(
                    label: str,
                    code: int,
                    *,
                    value: int = 0,
                    target: str | None = None,
                    body: bytes = b"",
                    nonce: int | None = None,
                ) -> tuple[object, int]:
                    if nonce is None:
                        nonces[label] += 1
                        nonce = nonces[label]
                    result = store.perform_action(
                        actors[label],
                        nonce,
                        p4rm.ACTION_GUILD,
                        code,
                        value,
                        ZERO_ACTOR if target is None else actors[target],
                        body,
                    )
                    return result, nonce

                def direct_action(
                    label: str,
                    kind: int,
                    code: int,
                    target: str,
                    *,
                    value: int = 0,
                    body: bytes = b"",
                ) -> object:
                    nonces[label] += 1
                    return store.perform_action(
                        actors[label], nonces[label], kind, code, value,
                        actors[target], body,
                    )

                set_day(1)
                club_members = {
                    "A": ["P00", "P01", "P02"],
                    "B": ["P03", "P04", "P05", "P06"],
                    "C": ["P07", "P08", "P09", "P10", "P11"],
                }
                name_codes = {"A": 1, "B": 2, "C": 3}
                routes = {"A": 0, "B": 2, "C": 1}

                for club, members in club_members.items():
                    created, _ = action(
                        members[0], p4rm.GUILD_CREATE,
                        value=name_codes[club],
                    )
                    self.assertEqual(created.status, p4rm.ACTION_OK)
                    for member in members[1:]:
                        joined, _ = action(
                            member, p4rm.GUILD_JOIN, target=members[0]
                        )
                        self.assertEqual(joined.status, p4rm.ACTION_OK)

                self.assertEqual(store.guild_count(), 3)
                self.assertEqual(
                    {summary.name_code for summary in store.list_guilds(limit=8)},
                    {1, 2, 3},
                )

                # Curated names are unique while occupied.  Deleting the final
                # member deletes the club and immediately releases its code to
                # a different actor that is not under a rejoin cooldown.
                temporary, _ = action("P12", p4rm.GUILD_CREATE, value=4)
                self.assertEqual(temporary.status, p4rm.ACTION_OK)
                duplicate, _ = action("P13", p4rm.GUILD_CREATE, value=4)
                self.assertEqual(duplicate.status, p4rm.ACTION_DENIED)
                left, _ = action("P12", p4rm.GUILD_LEAVE)
                self.assertEqual(left.status, p4rm.ACTION_OK)
                self.assertEqual(store.guild_count(), 3)
                reused, _ = action("P13", p4rm.GUILD_CREATE, value=4)
                self.assertEqual(reused.status, p4rm.ACTION_OK)
                self.assertEqual(store.guild_count(), 4)
                left, _ = action("P13", p4rm.GUILD_LEAVE)
                self.assertEqual(left.status, p4rm.ACTION_OK)
                self.assertEqual(store.guild_count(), 3)

                invalid_name, _ = action("P12", p4rm.GUILD_CREATE, value=17)
                self.assertEqual(invalid_name.status, p4rm.ACTION_INVALID)
                occupied_name, _ = action("P12", p4rm.GUILD_CREATE, value=1)
                self.assertEqual(occupied_name.status, p4rm.ACTION_DENIED)

                # New members cannot rally, cheer, or clash until a later hub
                # day.  Direct PvP inside a club is blocked independently.
                for code, target in (
                    (p4rm.GUILD_RALLY, None),
                    (p4rm.GUILD_CHEER, "P03"),
                    (p4rm.GUILD_CLASH, "P03"),
                ):
                    result, _ = action(
                        "P00", code,
                        value=0,
                        target=target,
                    )
                    self.assertEqual(result.status, p4rm.ACTION_DENIED)
                same_club_pvp = direct_action(
                    "P01", p4rm.ACTION_PVP_BEGIN, 0, "P02"
                )
                self.assertEqual(same_club_pvp.status, p4rm.ACTION_DENIED)

                # Malformed and forged shapes are rejected through the public
                # action API and never become club state.
                malformed, _ = action(
                    "P00", p4rm.GUILD_RALLY, value=0, body=b"FORGED"
                )
                self.assertEqual(malformed.status, p4rm.ACTION_INVALID)
                forged_target, _ = action(
                    "P00", p4rm.GUILD_RALLY, value=0, target="P03"
                )
                self.assertEqual(forged_target.status, p4rm.ACTION_INVALID)
                bad_code, _ = action("P00", 0xFF)
                self.assertEqual(bad_code.status, p4rm.ACTION_INVALID)

                expected_successor = min(
                    (actors["P01"], "P01"), (actors["P02"], "P02")
                )[1]
                day24_lifetime: dict[str, tuple[int, ...]] = {}
                first_clash_nonce = 0
                first_clash_result = None
                first_clash_scores: tuple[int, int] | None = None

                for day in range(2, 31):
                    set_day(day)

                    if day == 25:
                        # The first read in season two resets only season
                        # points.  Check this before any day-25 activity can
                        # legitimately add new season points.
                        for club, members in club_members.items():
                            status = store.guild_status(actors[members[0]])
                            self.assertEqual(status.season_points, 0)
                            self.assertEqual(
                                (
                                    status.prestige,
                                    status.banner_stars,
                                    status.wins,
                                    status.losses,
                                    status.draws,
                                ),
                                day24_lifetime[club],
                            )

                    if day == 2:
                        # A cooldown prevents same-day hopping.  Leadership
                        # passes to oldest joined_day, then lowest actor ID.
                        left, _ = action("P00", p4rm.GUILD_LEAVE)
                        self.assertEqual(left.status, p4rm.ACTION_OK)
                        club_members["A"].remove("P00")
                        self.assertEqual(
                            store.guild_status(actors[expected_successor]).role,
                            p4rm.GUILD_ROLE_LEADER,
                        )
                        denied, _ = action(
                            "P00", p4rm.GUILD_JOIN, target=expected_successor
                        )
                        self.assertEqual(denied.status, p4rm.ACTION_DENIED)

                        # The now-expired P12 cooldown permits the released
                        # curated name to be reused.  Keep this one-member club
                        # for three eligible days to prove quest carry-over.
                        recreated, _ = action(
                            "P12", p4rm.GUILD_CREATE, value=4
                        )
                        self.assertEqual(recreated.status, p4rm.ACTION_OK)
                        self.assertEqual(store.guild_count(), 4)
                        delayed_solo, _ = action(
                            "P12", p4rm.GUILD_RALLY, value=0
                        )
                        self.assertEqual(
                            delayed_solo.status, p4rm.ACTION_DENIED
                        )

                    if day == 3:
                        joined, _ = action(
                            "P00", p4rm.GUILD_JOIN, target=expected_successor
                        )
                        self.assertEqual(joined.status, p4rm.ACTION_OK)
                        club_members["A"].append("P00")
                        self.assertEqual(
                            store.guild_status(actors["P00"]).role,
                            p4rm.GUILD_ROLE_MEMBER,
                        )
                        delayed, _ = action(
                            "P00", p4rm.GUILD_RALLY, value=routes["A"]
                        )
                        self.assertEqual(delayed.status, p4rm.ACTION_DENIED)

                    if day in (3, 4, 5):
                        # P12 earns five points per rally.  Progress survives
                        # day rolls: 5, then 10, then one star plus remainder 3.
                        solo, _ = action(
                            "P12", p4rm.GUILD_RALLY, value=0
                        )
                        self.assertEqual(solo.status, p4rm.ACTION_OK)
                        solo_status = store.guild_status(actors["P12"])
                        self.assertEqual(
                            (solo.value, solo_status.quest_progress,
                             solo_status.banner_stars),
                            {
                                3: (5, 5, 0),
                                4: (5, 10, 0),
                                5: (15, 3, 1),
                            }[day],
                        )
                        if day == 5:
                            departed, _ = action("P12", p4rm.GUILD_LEAVE)
                            self.assertEqual(
                                departed.status, p4rm.ACTION_OK
                            )
                            self.assertEqual(store.guild_count(), 3)

                    # Each eligible actor rallies exactly once.  The first
                    # day P00 is eligible also proves exact nonce replay and
                    # changed-request rejection without duplicate progress.
                    per_club_points = {club: 0 for club in club_members}
                    stars_before = {
                        club: store.guild_status(actors[members[0]]).banner_stars
                        for club, members in club_members.items()
                    }
                    progress_before = {
                        club: store.guild_status(
                            actors[members[0]]
                        ).quest_progress
                        for club, members in club_members.items()
                    }
                    for club, members in club_members.items():
                        for member in tuple(members):
                            status = store.guild_status(actors[member])
                            if not (
                                status.daily_flags & p4rm.GUILD_DAILY_ELIGIBLE
                            ):
                                continue
                            if day == 4 and member == "P00":
                                result, replay_nonce = action(
                                    member,
                                    p4rm.GUILD_RALLY,
                                    value=routes[club],
                                )
                                self.assertEqual(result.status, p4rm.ACTION_OK)
                                after_first = store.guild_status(actors[member])
                                replay, _ = action(
                                    member,
                                    p4rm.GUILD_RALLY,
                                    value=routes[club],
                                    nonce=replay_nonce,
                                )
                                self.assertEqual(replay, result)
                                self.assertEqual(
                                    store.guild_status(actors[member]), after_first
                                )
                                changed, _ = action(
                                    member,
                                    p4rm.GUILD_RALLY,
                                    value=(routes[club] + 1) % 3,
                                    nonce=replay_nonce,
                                )
                                self.assertEqual(
                                    changed.status, p4rm.ACTION_INVALID
                                )
                                duplicate_rally, _ = action(
                                    member,
                                    p4rm.GUILD_RALLY,
                                    value=routes[club],
                                )
                                self.assertEqual(
                                    duplicate_rally.status, p4rm.ACTION_DENIED
                                )
                            else:
                                result, _ = action(
                                    member,
                                    p4rm.GUILD_RALLY,
                                    value=routes[club],
                                )
                                self.assertEqual(result.status, p4rm.ACTION_OK)
                            # The result includes the one-time ten-point quest
                            # completion bonus.  Strip it to recover rally steps.
                            per_club_points[club] += (
                                result.value - 10 if result.value > 5 else result.value
                            )

                    for club, members in club_members.items():
                        status = store.guild_status(actors[members[0]])
                        accumulated = (
                            progress_before[club] + per_club_points[club]
                        )
                        expected_star = int(accumulated >= p4rm.GUILD_QUEST_GOAL)
                        expected_progress = (
                            min(
                                p4rm.GUILD_QUEST_GOAL - 1,
                                accumulated - p4rm.GUILD_QUEST_GOAL,
                            )
                            if expected_star
                            else accumulated
                        )
                        self.assertEqual(
                            status.banner_stars,
                            stars_before[club] + expected_star,
                        )
                        self.assertEqual(
                            status.quest_progress,
                            expected_progress,
                        )

                    # Day four proves independent outgoing/incoming limits,
                    # normalized unequal rosters, and deterministic scores.
                    if day == 4:
                        first_clash_result, first_clash_nonce = action(
                            "P00", p4rm.GUILD_CLASH, target="P03"
                        )
                        self.assertEqual(
                            first_clash_result.status, p4rm.ACTION_OK
                        )
                        denied_outgoing, _ = action(
                            "P00", p4rm.GUILD_CLASH, target="P07"
                        )
                        self.assertEqual(
                            denied_outgoing.status, p4rm.ACTION_DENIED
                        )
                        denied_incoming, _ = action(
                            "P07", p4rm.GUILD_CLASH, target="P03"
                        )
                        self.assertEqual(
                            denied_incoming.status, p4rm.ACTION_DENIED
                        )
                        allowed_both_roles, _ = action(
                            "P03", p4rm.GUILD_CLASH, target="P07"
                        )
                        self.assertEqual(
                            allowed_both_roles.status, p4rm.ACTION_OK
                        )

                        with sqlite3.connect(path) as database:
                            row = database.execute(
                                "SELECT source_guild_id, target_guild_id, "
                                "source_score, target_score FROM guild_clashes "
                                "WHERE clash_id = ?",
                                (first_clash_result.related_id,),
                            ).fetchone()
                        self.assertIsNotNone(row)
                        source_guild, target_guild = int(row[0]), int(row[1])
                        digest = hashlib.sha256(
                            b"p4-lord-clash-v1"
                            + day.to_bytes(8, "little")
                            + min(source_guild, target_guild).to_bytes(4, "little")
                            + max(source_guild, target_guild).to_bytes(4, "little")
                        ).digest()
                        low_roll = digest[0] % GUILD_CLASH_ROLL_SPAN - 3
                        high_roll = digest[1] % GUILD_CLASH_ROLL_SPAN - 3
                        source_roll, target_roll = (
                            (low_roll, high_roll)
                            if source_guild < target_guild
                            else (high_roll, low_roll)
                        )
                        common = 34 + GUILD_CLASH_PARTICIPATION_POINTS
                        expected_scores = (
                            max(0, common + GUILD_CLASH_ROUTE_BONUS + source_roll),
                            max(0, common + target_roll),
                        )
                        first_clash_scores = (int(row[2]), int(row[3]))
                        self.assertEqual(first_clash_scores, expected_scores)

                    elif day in (5, 6):
                        pair_cooldown, _ = action(
                            "P00", p4rm.GUILD_CLASH, target="P03"
                        )
                        self.assertEqual(
                            pair_cooldown.status, p4rm.ACTION_DENIED
                        )
                        if day == 5:
                            rotated, _ = action(
                                "P07", p4rm.GUILD_CLASH, target="P00"
                            )
                            self.assertEqual(rotated.status, p4rm.ACTION_OK)
                    elif day >= 7:
                        phase = (day - 7) % GUILD_CLASH_PAIR_COOLDOWN_DAYS
                        if phase == 0:
                            ab, _ = action(
                                "P00", p4rm.GUILD_CLASH, target="P03"
                            )
                            bc, _ = action(
                                "P03", p4rm.GUILD_CLASH, target="P07"
                            )
                            self.assertEqual(ab.status, p4rm.ACTION_OK)
                            self.assertEqual(bc.status, p4rm.ACTION_OK)
                        elif phase == 1:
                            ca, _ = action(
                                "P07", p4rm.GUILD_CLASH, target="P00"
                            )
                            self.assertEqual(ca.status, p4rm.ACTION_OK)

                    # Cheers are positive-sum but exactly once per source club
                    # per day.  Day five checks their precise +1/+2 settlement
                    # and idempotent replay; other days maintain campaign load.
                    if day == 5:
                        before_a = store.guild_status(actors["P00"])
                        before_c = store.guild_status(actors["P07"])
                        cheered, cheer_nonce = action(
                            "P00", p4rm.GUILD_CHEER, target="P07"
                        )
                        self.assertEqual(cheered.status, p4rm.ACTION_OK)
                        replayed, _ = action(
                            "P00",
                            p4rm.GUILD_CHEER,
                            target="P07",
                            nonce=cheer_nonce,
                        )
                        self.assertEqual(replayed, cheered)
                        after_a = store.guild_status(actors["P00"])
                        after_c = store.guild_status(actors["P07"])
                        self.assertEqual(after_a.prestige, before_a.prestige + 1)
                        self.assertEqual(after_c.prestige, before_c.prestige + 2)
                        second_cheer, _ = action(
                            "P00", p4rm.GUILD_CHEER, target="P03"
                        )
                        self.assertEqual(
                            second_cheer.status, p4rm.ACTION_DENIED
                        )
                        same_club_cheer, _ = action(
                            "P03", p4rm.GUILD_CHEER, target="P04"
                        )
                        self.assertEqual(
                            same_club_cheer.status, p4rm.ACTION_DENIED
                        )
                    elif day >= 4:
                        cheer_order = (
                            ("P00", "P03"),
                            ("P03", "P07"),
                            ("P07", "P00"),
                        )
                        source, target = cheer_order[day % len(cheer_order)]
                        cheered, _ = action(
                            source, p4rm.GUILD_CHEER, target=target
                        )
                        self.assertEqual(cheered.status, p4rm.ACTION_OK)

                    if day == 10:
                        status_before = {
                            club: store.guild_status(actors[members[0]])
                            for club, members in club_members.items()
                        }
                        store = RealmStore(path, epoch_seconds=0)
                        self.assertEqual(
                            {
                                club: store.guild_status(actors[members[0]])
                                for club, members in club_members.items()
                            },
                            status_before,
                        )
                        with sqlite3.connect(path) as database:
                            clash_count = int(
                                database.execute(
                                    "SELECT COUNT(*) FROM guild_clashes"
                                ).fetchone()[0]
                            )
                        replay = store.perform_action(
                            actors["P00"],
                            first_clash_nonce,
                            p4rm.ACTION_GUILD,
                            p4rm.GUILD_CLASH,
                            0,
                            actors["P03"],
                            b"",
                        )
                        self.assertEqual(replay, first_clash_result)
                        with sqlite3.connect(path) as database:
                            self.assertEqual(
                                int(database.execute(
                                    "SELECT COUNT(*) FROM guild_clashes"
                                ).fetchone()[0]),
                                clash_count,
                            )
                        changed = store.perform_action(
                            actors["P00"],
                            first_clash_nonce,
                            p4rm.ACTION_GUILD,
                            p4rm.GUILD_CLASH,
                            0,
                            actors["P07"],
                            b"",
                        )
                        self.assertEqual(changed.status, p4rm.ACTION_INVALID)

                    if day == 24:
                        day24_lifetime = {
                            club: (
                                status.prestige,
                                status.banner_stars,
                                status.wins,
                                status.losses,
                                status.draws,
                            )
                            for club, members in club_members.items()
                            for status in (
                                store.guild_status(actors[members[0]]),
                            )
                        }
                        self.assertTrue(all(
                            store.guild_status(actors[members[0]]).season_points
                            > 0
                            for members in club_members.values()
                        ))
                        store = RealmStore(path, epoch_seconds=0)

                self.assertIsNotNone(first_clash_scores)
                self.assertEqual(store.guild_count(), 3)
                self.assertEqual(
                    {summary.members for summary in store.list_guilds(limit=8)},
                    {3, 4, 5},
                )
                self.assertTrue(all(
                    store.guild_status(actors[members[0]]).season_points > 0
                    for members in club_members.values()
                ))

                # SQL is inspection-only here.  These constraints must hold
                # over the entire 30-day campaign, including restarts.
                with sqlite3.connect(path) as database:
                    pair_days: dict[tuple[int, int], list[int]] = {}
                    for low, high, day in database.execute(
                        "SELECT pair_low, pair_high, realm_day_id "
                        "FROM guild_clashes ORDER BY pair_low, pair_high, realm_day_id"
                    ):
                        pair_days.setdefault((int(low), int(high)), []).append(int(day))
                    for days in pair_days.values():
                        self.assertTrue(all(
                            current - prior >= GUILD_CLASH_PAIR_COOLDOWN_DAYS
                            for prior, current in zip(days, days[1:])
                        ))
                    self.assertEqual(
                        int(database.execute(
                            "SELECT COUNT(*) FROM (SELECT source_guild_id, "
                            "realm_day_id, COUNT(*) AS n FROM guild_clashes "
                            "GROUP BY source_guild_id, realm_day_id HAVING n > 1)"
                        ).fetchone()[0]),
                        0,
                    )
                    self.assertEqual(
                        int(database.execute(
                            "SELECT COUNT(*) FROM (SELECT target_guild_id, "
                            "realm_day_id, COUNT(*) AS n FROM guild_clashes "
                            "GROUP BY target_guild_id, realm_day_id HAVING n > 1)"
                        ).fetchone()[0]),
                        0,
                    )
                    self.assertEqual(
                        int(database.execute(
                            "SELECT COUNT(*) FROM (SELECT actor_id, realm_day_id, "
                            "COUNT(*) AS n FROM guild_rallies GROUP BY actor_id, "
                            "realm_day_id HAVING n > 1)"
                        ).fetchone()[0]),
                        0,
                    )
                    self.assertEqual(
                        int(database.execute(
                            "SELECT COUNT(*) FROM pvp_leases"
                        ).fetchone()[0]),
                        0,
                    )
                    self.assertEqual(
                        int(database.execute(
                            "SELECT COUNT(*) FROM realm_economy_events"
                        ).fetchone()[0]),
                        0,
                    )
                    current_profiles = {
                        bytes(row[0]): tuple(int(value) for value in row[1:])
                        for row in database.execute(
                            "SELECT actor_id, chompcoin, bank, experience, "
                            "hit_points, max_hit_points, strength, defense, "
                            "dragon_kills, pvp_wins, pvp_losses FROM profiles"
                        )
                    }
                self.assertEqual(current_profiles, personal_profiles)
                for label, actor in actors.items():
                    head = store.read_head(actor)
                    self.assertEqual(head.revision, personal_heads[label][0])
                    self.assertEqual(
                        hashlib.sha256(head.snapshot or b"").digest(),
                        personal_heads[label][1],
                    )


if __name__ == "__main__":
    unittest.main()
