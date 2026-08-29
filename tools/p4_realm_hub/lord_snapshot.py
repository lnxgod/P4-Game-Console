"""Strict LORD LRSY/LDSV decoding and conservative Phase-A policy.

The local cartridge is still playable without the hub, so this module does not
claim proof of play.  It does make the Mac head materially harder to inflate:
every accepted snapshot has the complete fixed C layout, practical absolute
bounds, and a conservative transition from the prior authoritative head.
"""

from __future__ import annotations

import dataclasses
import struct
import zlib


LORD_SYNC_HEADER_BYTES = 52
LORD_SAVE_HEADER_BYTES = 16
LORD_SAVE_LENGTHS = {3: 2286, 4: 2442, 5: 2466}
LORD_ACTOR_ID_BYTES = 16
LORD_REALM_PLAYER_COUNT = 8
LORD_MAIL_COUNT = 12
LORD_LOG_COUNT = 12
LORD_SKILL_COUNT = 3

# These are deliberately wider than one honest LORD day.  They reject obvious
# edited saves without turning this Phase-A check into a brittle game replay.
MAX_CURRENCY = 1_000_000_000
MAX_EXPERIENCE = 1_000_000_000
MAX_COMBAT_STAT = 100_000
MAX_FOREST_FIGHTS = 255
MAX_SKILL_USES = 255
MAX_WEALTH_GAIN_FLAT = 25_000_000
MAX_EXPERIENCE_GAIN = 25_000_000
MAX_HIT_POINT_GAIN = 5_000
MAX_STRENGTH_GAIN = 5_000
MAX_DEFENSE_GAIN = 5_000
MAX_PVP_WIN_GAIN = 3
MAX_PVP_LOSS_GAIN = 100
MAX_FOREST_FIGHT_GAIN = 64
MAX_SKILL_MASTERY_GAIN = 40
MAX_SKILL_USE_GAIN = 64
MAX_SMALL_COUNTER_GAIN = 500


@dataclasses.dataclass(frozen=True)
class LordPlayer:
    name: str
    hero_style: int
    hero_class: int
    level: int
    weapon: int
    armor: int
    hit_points: int
    max_hit_points: int
    strength: int
    defense: int
    chompcoin: int
    bank: int
    experience: int
    forest_fights: int
    skill: tuple[int, int, int]
    skill_uses: tuple[int, int, int]
    dragon_kills: int
    day: int
    pvp_wins: int
    pvp_losses: int
    charm: int
    gems: int
    young_heroes_helped: int
    friendship_badges: int
    horse: bool
    fairy: bool
    fairy_lore: bool
    amulet: bool
    high_spirits: bool
    seen_dragon: bool


@dataclasses.dataclass(frozen=True)
class LordSnapshot:
    actor_id: bytes
    operation_nonce: int
    server_envelope_revision: int
    save_version: int
    save_sequence: int
    rng_state: int
    realm_revision: int
    pvp_fights: int
    friendship_actions: int
    igm_used_mask: int
    player: LordPlayer
    sync_actor_id: bytes
    sync_server_revision: int
    sync_committed_save_sequence: int
    last_realm_event_id: int


@dataclasses.dataclass(frozen=True)
class ProfileProjection:
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
    bank: int
    pvp_wins: int
    pvp_losses: int


class _Reader:
    def __init__(self, data: bytes, offset: int = 0) -> None:
        self.data = data
        self.offset = offset

    def _take(self, size: int) -> bytes:
        if size < 0 or self.offset + size > len(self.data):
            raise ValueError("truncated LORD save")
        result = self.data[self.offset : self.offset + size]
        self.offset += size
        return result

    def u8(self) -> int:
        return self._take(1)[0]

    def u16(self) -> int:
        return struct.unpack("<H", self._take(2))[0]

    def u32(self) -> int:
        return struct.unpack("<I", self._take(4))[0]

    def u64(self) -> int:
        return struct.unpack("<Q", self._take(8))[0]

    def i32(self) -> int:
        return struct.unpack("<i", self._take(4))[0]

    def boolean(self) -> bool:
        value = self.u8()
        if value > 1:
            raise ValueError("invalid LORD boolean")
        return value != 0

    def raw(self, size: int) -> bytes:
        return self._take(size)

    def text(self, size: int, *, allow_empty: bool, label: str) -> str:
        raw = self._take(size)
        terminator = raw.find(b"\0")
        if terminator < 0 or (terminator == 0 and not allow_empty):
            raise ValueError(f"invalid LORD {label}")
        encoded = raw[:terminator]
        if any(value < 0x20 or value > 0x7E for value in encoded):
            raise ValueError(f"non-printable LORD {label}")
        return encoded.decode("ascii")


def _validate_player(player: LordPlayer) -> None:
    if (
        len(player.name) < 3
        or player.hero_style not in (0, 1)
        or player.hero_class not in (0, 1, 2)
        or not 1 <= player.level <= 12
        or not 0 <= player.weapon < 16
        or not 0 <= player.armor < 16
        or not 0 <= player.hit_points <= player.max_hit_points
        or not 1 <= player.max_hit_points <= MAX_COMBAT_STAT
        or not 1 <= player.strength <= MAX_COMBAT_STAT
        or not 0 <= player.defense <= MAX_COMBAT_STAT
        or player.chompcoin > MAX_CURRENCY
        or player.bank > MAX_CURRENCY
        or player.experience > MAX_EXPERIENCE
        or player.forest_fights > MAX_FOREST_FIGHTS
        or player.day == 0
        or any(value > 40 for value in player.skill)
        or any(value > MAX_SKILL_USES for value in player.skill_uses)
    ):
        raise ValueError("LORD protected player is out of range")


def _read_player(reader: _Reader) -> LordPlayer:
    name = reader.text(20, allow_empty=False, label="player name")
    hero_style = reader.u8()
    hero_class = reader.u8()
    level = reader.u8()
    weapon = reader.u8()
    armor = reader.u8()
    hit_points = reader.i32()
    max_hit_points = reader.i32()
    strength = reader.i32()
    defense = reader.i32()
    chompcoin = reader.u32()
    bank = reader.u32()
    experience = reader.u32()
    forest_fights = reader.u16()
    skill: list[int] = []
    skill_uses: list[int] = []
    for _ in range(LORD_SKILL_COUNT):
        skill.append(reader.u8())
        skill_uses.append(reader.u8())
    player = LordPlayer(
        name=name,
        hero_style=hero_style,
        hero_class=hero_class,
        level=level,
        weapon=weapon,
        armor=armor,
        hit_points=hit_points,
        max_hit_points=max_hit_points,
        strength=strength,
        defense=defense,
        chompcoin=chompcoin,
        bank=bank,
        experience=experience,
        forest_fights=forest_fights,
        skill=(skill[0], skill[1], skill[2]),
        skill_uses=(skill_uses[0], skill_uses[1], skill_uses[2]),
        dragon_kills=reader.u8(),
        day=reader.u16(),
        pvp_wins=reader.u16(),
        pvp_losses=reader.u16(),
        charm=reader.u16(),
        gems=reader.u16(),
        young_heroes_helped=reader.u16(),
        friendship_badges=reader.u16(),
        horse=reader.boolean(),
        fairy=reader.boolean(),
        fairy_lore=reader.boolean(),
        amulet=reader.boolean(),
        high_spirits=reader.boolean(),
        seen_dragon=reader.boolean(),
    )
    _validate_player(player)
    return player


def _fixed_text(raw: bytes, *, allow_empty: bool, label: str) -> str:
    terminator = raw.find(b"\0")
    if terminator < 0 or (terminator == 0 and not allow_empty):
        raise ValueError(f"invalid LORD {label}")
    encoded = raw[:terminator]
    if any(value < 0x20 or value > 0x7E for value in encoded):
        raise ValueError(f"non-printable LORD {label}")
    return encoded.decode("ascii")


def _read_realm(reader: _Reader) -> tuple[tuple[bool, ...], tuple[bool, ...]]:
    teamed_slots: list[bool] = []
    legacy_empty_slots: list[bool] = []
    for index in range(LORD_REALM_PLAYER_COUNT):
        name_raw = reader.raw(24)
        saying_raw = reader.raw(40)
        _fixed_text(name_raw, allow_empty=False, label=f"realm name {index}")
        _fixed_text(saying_raw, allow_empty=True, label=f"realm saying {index}")
        style = reader.u8()
        hero_class = reader.u8()
        level = reader.u8()
        alive = reader.boolean()
        at_inn = reader.boolean()
        teamed = reader.boolean()
        teamed_slots.append(teamed)
        trust = reader.u8()
        hit_points = reader.i32()
        max_hit_points = reader.i32()
        strength = reader.i32()
        defense = reader.i32()
        chompcoin = reader.u32()
        experience = reader.u32()
        pvp_wins = reader.u16()
        pvp_losses = reader.u16()
        legacy_empty = (
            name_raw == b"Empty record\0" + bytes(11)
            and saying_raw == bytes(40)
            and style == 0
            and hero_class == 0
            and level == 0
            and not alive
            and not at_inn
            and not teamed
            and trust == 0
            and hit_points == 0
            and max_hit_points == 1
            and strength == 1
            and defense == 0
            and chompcoin == 0
            and experience == 0
            and pvp_wins == 0
            and pvp_losses == 0
        )
        legacy_empty_slots.append(legacy_empty)
        if not legacy_empty and (
            style not in (0, 1)
            or hero_class not in (0, 1, 2)
            or not 1 <= level <= 12
            or trust > 100
            or not 0 <= hit_points <= max_hit_points
            or not 1 <= max_hit_points <= MAX_COMBAT_STAT
            or not 1 <= strength <= MAX_COMBAT_STAT
            or not 0 <= defense <= MAX_COMBAT_STAT
            or chompcoin > MAX_CURRENCY
            or experience > MAX_EXPERIENCE
        ):
            raise ValueError("LORD realm player is out of range")
    return tuple(teamed_slots), tuple(legacy_empty_slots)


def _read_mail_and_log(reader: _Reader) -> None:
    for index in range(LORD_MAIL_COUNT):
        sender = reader.u8()
        recipient = reader.u8()
        kind = reader.u8()
        reader.boolean()
        reader.boolean()
        reader.text(48, allow_empty=True, label=f"mail body {index}")
        if sender > 10 or recipient > 10 or kind > 9:
            raise ValueError("invalid LORD mail record")
    for index in range(LORD_LOG_COUNT):
        reader.u16()
        reader.text(52, allow_empty=True, label=f"log text {index}")


def decode_lord_sync(
    record: bytes,
    *,
    expected_actor_id: bytes | None = None,
    expected_nonce: int | None = None,
    allow_adoption_bridge: bool = False,
) -> LordSnapshot:
    """Decode the complete fixed schema-3/4/5 C save layout."""
    if not LORD_SYNC_HEADER_BYTES <= len(record) <= 4148 or record[:4] != b"LRSY":
        raise ValueError("invalid LORD sync envelope")
    version, header_bytes = struct.unpack_from("<HH", record, 4)
    total, stored_crc, realm_revision, save_sequence = struct.unpack_from(
        "<IIII", record, 8
    )
    operation_nonce = struct.unpack_from("<Q", record, 24)[0]
    actor_id = record[32:48]
    save_bytes = struct.unpack_from("<I", record, 48)[0]
    if (
        version != 1
        or header_bytes != LORD_SYNC_HEADER_BYTES
        or total != len(record)
        or realm_revision == 0
        or save_sequence == 0
        or operation_nonce == 0
        or actor_id == bytes(LORD_ACTOR_ID_BYTES)
        or save_bytes != len(record) - LORD_SYNC_HEADER_BYTES
        or stored_crc != zlib.crc32(record[16:]) & 0xFFFFFFFF
        or (expected_actor_id is not None and actor_id != expected_actor_id)
        or (expected_nonce is not None and operation_nonce != expected_nonce)
    ):
        raise ValueError("invalid LORD sync metadata")

    save = record[LORD_SYNC_HEADER_BYTES:]
    if len(save) < LORD_SAVE_HEADER_BYTES or save[:4] != b"LDSV":
        raise ValueError("invalid LORD save envelope")
    save_version, stored_length, save_crc, nested_sequence = struct.unpack_from(
        "<HHII", save, 4
    )
    if (
        save_version not in LORD_SAVE_LENGTHS
        or len(save) != LORD_SAVE_LENGTHS[save_version]
        or stored_length != len(save)
        or save_crc != zlib.crc32(save[LORD_SAVE_HEADER_BYTES:]) & 0xFFFFFFFF
        or nested_sequence != save_sequence
    ):
        raise ValueError("invalid LORD save metadata")

    reader = _Reader(save, LORD_SAVE_HEADER_BYTES)
    rng_state = reader.u32()
    nested_realm_revision = reader.u32()
    partner_code = reader.u8()
    npc_friend_code = reader.u8()
    pvp_fights = reader.u8()
    friendship_actions = reader.u8()
    igm_used_mask = reader.u8()
    rip_scene = reader.u8()
    mail_count = reader.u8()
    log_count = reader.u8()
    reader.text(48, allow_empty=True, label="conversation")
    reader.text(48, allow_empty=True, label="announcement")
    player = _read_player(reader)
    teamed_slots, legacy_empty_slots = _read_realm(reader)
    _read_mail_and_log(reader)

    sync_actor_id = bytes(LORD_ACTOR_ID_BYTES)
    sync_server_revision = 0
    sync_committed_save_sequence = 0
    last_realm_event_id = 0
    realm_actor_ids: tuple[bytes, ...] = ()
    partner_actor_id = bytes(LORD_ACTOR_ID_BYTES)
    if save_version >= 4:
        expected_marker = b"MPV4" if save_version == 4 else b"MPV5"
        if reader.raw(4) != expected_marker:
            raise ValueError("invalid LORD multiplayer extension")
        realm_actor_ids = tuple(
            reader.raw(LORD_ACTOR_ID_BYTES)
            for _ in range(LORD_REALM_PLAYER_COUNT)
        )
        partner_actor_id = reader.raw(LORD_ACTOR_ID_BYTES)
        last_realm_event_id = reader.u64()
        if save_version >= 5:
            sync_actor_id = reader.raw(LORD_ACTOR_ID_BYTES)
            sync_server_revision = reader.u32()
            sync_committed_save_sequence = reader.u32()

    for index, legacy_empty in enumerate(legacy_empty_slots):
        if legacy_empty and (
            partner_code == index + 1
            or (
                realm_actor_ids
                and realm_actor_ids[index] != bytes(LORD_ACTOR_ID_BYTES)
            )
        ):
            raise ValueError("legacy LORD empty slot is actor-bound or partnered")

    if (
        reader.offset != len(save)
        or rng_state == 0
        or nested_realm_revision != realm_revision
        or partner_code > LORD_REALM_PLAYER_COUNT
        or npc_friend_code > 2
        or pvp_fights > 3
        or friendship_actions > 3
        or igm_used_mask & ~0x7F
        or rip_scene >= 12
        or mail_count > LORD_MAIL_COUNT
        or log_count > LORD_LOG_COUNT
        or (partner_code != 0 and not teamed_slots[partner_code - 1])
        or (sync_actor_id != bytes(16) and sync_actor_id != actor_id)
        or (
            sync_server_revision == 0
            and sync_actor_id != bytes(16)
            and not allow_adoption_bridge
        )
        or (sync_server_revision != 0 and sync_actor_id == bytes(16))
        or (sync_server_revision == 0 and sync_committed_save_sequence != 0)
        or (
            sync_server_revision != 0
            and sync_committed_save_sequence == 0
            and not allow_adoption_bridge
        )
        or sync_committed_save_sequence > save_sequence
    ):
        raise ValueError("invalid LORD save state")
    if realm_actor_ids:
        populated_actor_ids = [
            value for value in realm_actor_ids if value != bytes(LORD_ACTOR_ID_BYTES)
        ]
        if (
            len(populated_actor_ids) != len(set(populated_actor_ids))
            or actor_id in populated_actor_ids
            or (
                partner_code != 0
                and partner_actor_id != bytes(LORD_ACTOR_ID_BYTES)
                and realm_actor_ids[partner_code - 1]
                != bytes(LORD_ACTOR_ID_BYTES)
                and partner_actor_id != realm_actor_ids[partner_code - 1]
            )
        ):
            raise ValueError("invalid LORD multiplayer actor bindings")

    return LordSnapshot(
        actor_id=actor_id,
        operation_nonce=operation_nonce,
        server_envelope_revision=realm_revision,
        save_version=save_version,
        save_sequence=save_sequence,
        rng_state=rng_state,
        realm_revision=realm_revision,
        pvp_fights=pvp_fights,
        friendship_actions=friendship_actions,
        igm_used_mask=igm_used_mask,
        player=player,
        sync_actor_id=sync_actor_id,
        sync_server_revision=sync_server_revision,
        sync_committed_save_sequence=sync_committed_save_sequence,
        last_realm_event_id=last_realm_event_id,
    )


def _bounded_gain(current: int, prior: int, limit: int, label: str) -> None:
    if current > prior and current - prior > limit:
        raise ValueError(f"implausible LORD {label} gain")


def _is_post_dragon_victory_reset(old: LordPlayer, new: LordPlayer) -> bool:
    """Recognize the game's exact second half of a dragon-victory reset.

    LORD marks the dragon kill dirty on the victory screen, so that transient
    level-12 state can reach the hub.  Confirming the screen then calls
    initialize_player() without incrementing the kill count a second time.
    Keep this exception deliberately exact so it cannot disguise an arbitrary
    same-day level rollback.
    """
    expected_skill = tuple(5 if index == new.hero_class else 0 for index in range(3))
    expected_uses = tuple(3 if index == new.hero_class else 0 for index in range(3))
    return (
        old.level == 12
        and old.seen_dragon
        and old.dragon_kills > 0
        and new.dragon_kills == old.dragon_kills
        and new.level == 1
        and new.weapon == 0
        and new.armor == 0
        and new.hit_points == 20 + new.dragon_kills * 5
        and new.max_hit_points == 20 + new.dragon_kills * 5
        and new.strength == 10 + new.dragon_kills * 2
        and new.defense == 1 + new.dragon_kills
        and new.chompcoin == 500
        and new.bank == 0
        and new.experience == 0
        and new.forest_fights == 15
        and new.skill == expected_skill
        and new.skill_uses == expected_uses
        and new.day == old.day
        and new.pvp_wins == old.pvp_wins
        and new.pvp_losses == old.pvp_losses
        and new.charm == 10
        and new.gems == 0
        and new.young_heroes_helped == 0
        and new.friendship_badges == 0
        and not new.horse
        and not new.fairy
        and not new.fairy_lore
        and not new.amulet
        and not new.high_spirits
        and not new.seen_dragon
    )


def validate_lord_transition(
    prior: LordSnapshot,
    current: LordSnapshot,
    *,
    realm_day_advanced: bool,
) -> None:
    """Apply conservative, named Phase-A progression bounds."""
    old = prior.player
    new = current.player
    if (
        new.name != old.name
        or new.hero_style != old.hero_style
        or new.hero_class != old.hero_class
    ):
        raise ValueError("LORD identity or class changed")
    if current.save_sequence < prior.save_sequence:
        raise ValueError("LORD save sequence moved backwards")
    if current.realm_revision < prior.realm_revision:
        raise ValueError("LORD realm revision moved backwards")
    if current.last_realm_event_id < prior.last_realm_event_id:
        raise ValueError("LORD realm event cursor moved backwards")
    if new.day < old.day or new.day - old.day > 1:
        raise ValueError("LORD local day advanced too far")
    if new.day != old.day and not realm_day_advanced:
        raise ValueError("LORD local day advanced before the realm clock")
    post_dragon_reset = _is_post_dragon_victory_reset(old, new)
    if new.dragon_kills < old.dragon_kills or new.dragon_kills - old.dragon_kills > 1:
        raise ValueError("LORD dragon count changed too far")
    dragon_reset = new.dragon_kills == old.dragon_kills + 1
    daily_counter_reset = (
        current.pvp_fights > prior.pvp_fights
        or current.friendship_actions > prior.friendship_actions
        or current.igm_used_mask & prior.igm_used_mask != prior.igm_used_mask
        or (
            old.seen_dragon
            and not new.seen_dragon
            and not post_dragon_reset
            and not dragon_reset
        )
    )
    if daily_counter_reset and (
        not realm_day_advanced or new.day != old.day + 1
    ):
        raise ValueError("LORD daily counters reset without a new realm day")
    if not dragon_reset and not post_dragon_reset and new.level < old.level:
        raise ValueError("LORD level moved backwards without a dragon reset")

    old_wealth = old.chompcoin + old.bank
    new_wealth = new.chompcoin + new.bank
    wealth_limit = MAX_WEALTH_GAIN_FLAT + old_wealth // 5
    _bounded_gain(new_wealth, old_wealth, wealth_limit, "wealth")
    _bounded_gain(new.experience, old.experience, MAX_EXPERIENCE_GAIN, "experience")
    _bounded_gain(
        new.max_hit_points, old.max_hit_points, MAX_HIT_POINT_GAIN, "maximum HP"
    )
    _bounded_gain(new.strength, old.strength, MAX_STRENGTH_GAIN, "strength")
    _bounded_gain(new.defense, old.defense, MAX_DEFENSE_GAIN, "defense")
    _bounded_gain(new.pvp_wins, old.pvp_wins, MAX_PVP_WIN_GAIN, "PvP wins")
    _bounded_gain(new.pvp_losses, old.pvp_losses, MAX_PVP_LOSS_GAIN, "PvP losses")
    _bounded_gain(
        new.forest_fights,
        old.forest_fights,
        MAX_FOREST_FIGHT_GAIN,
        "forest fights",
    )
    for index, (old_skill, new_skill, old_uses, new_uses) in enumerate(
        zip(old.skill, new.skill, old.skill_uses, new.skill_uses, strict=True)
    ):
        _bounded_gain(
            new_skill,
            old_skill,
            MAX_SKILL_MASTERY_GAIN,
            f"skill {index} mastery",
        )
        _bounded_gain(
            new_uses,
            old_uses,
            MAX_SKILL_USE_GAIN,
            f"skill {index} uses",
        )
    for label, before, after in (
        ("charm", old.charm, new.charm),
        ("gems", old.gems, new.gems),
        ("young heroes", old.young_heroes_helped, new.young_heroes_helped),
        ("friendship badges", old.friendship_badges, new.friendship_badges),
    ):
        _bounded_gain(after, before, MAX_SMALL_COUNTER_GAIN, label)


def profile_projection(snapshot: LordSnapshot) -> ProfileProjection:
    player = snapshot.player
    return ProfileProjection(
        name=player.name,
        hero_style=player.hero_style,
        hero_class=player.hero_class,
        level=player.level,
        flags=0x01 if player.hit_points > 0 else 0,
        hit_points=player.hit_points,
        max_hit_points=player.max_hit_points,
        strength=player.strength,
        defense=player.defense,
        experience=player.experience,
        chompcoin=player.chompcoin,
        bank=player.bank,
        pvp_wins=player.pvp_wins,
        pvp_losses=player.pvp_losses,
    )
