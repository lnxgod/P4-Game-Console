// SPDX-License-Identifier: LicenseRef-LORD-Permission

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lord_internal.h"
#include "generated/lord_monsters.h"
#include "p4/game.h"
#include "p4/input.h"

extern const p4_game_descriptor_t p4_lord_game;

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static void enter_town(lord_state_t *state, lord_class_t hero_class)
{
    lord_initialize(state, UINT32_C(0x12345678));
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_NAME);
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_TEXT_EDITOR);
    (void)strcpy(state->editor_text, "Test Hero");
    state->selection = 42U;
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_HERO_STYLE);
    state->selection = 1U;
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_CLASS);
    state->selection = (uint8_t)hero_class;
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_MESSAGE);
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_TOWN);
}

static void test_character_creation_and_core_menu(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_MYSTICAL);
    CHECK(strcmp(state.player.name, "Test Hero") == 0);
    CHECK(state.player.hero_style == LORD_HERO_STYLE_HEROINE);
    CHECK(state.player.hero_class == LORD_CLASS_MYSTICAL);
    CHECK(state.player.level == 1U);
    CHECK(state.player.hit_points == 20);
    CHECK(state.player.gold == 500U);
    CHECK(state.player.charm == 10U);
    CHECK(state.player.skill[LORD_CLASS_MYSTICAL] == 5U);
    CHECK(state.player.skill_uses[LORD_CLASS_MYSTICAL] == 3U);
    CHECK(lord_menu_count(&state) == 16U);
    lord_move_selection(&state, -1);
    CHECK(state.selection == 15U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_GUILD);
    CHECK(lord_cancel(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_TOWN);
    state.selection = 14U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_SKILLS);
    CHECK(lord_cancel(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_TOWN);
    state.selection = 15U;
    lord_move_selection(&state, 1);
    CHECK(state.selection == 0U);

    for (uint16_t code = 1U; code <= LORD_GUILD_NAME_COUNT; ++code) {
        CHECK(strcmp(lord_guild_name(code), "No Club") != 0);
        for (uint16_t other = (uint16_t)(code + 1U);
             other <= LORD_GUILD_NAME_COUNT; ++other) {
            CHECK(strcmp(lord_guild_name(code), lord_guild_name(other)) != 0);
        }
    }
    CHECK(strcmp(lord_guild_name(0U), "No Club") == 0);
    CHECK(strcmp(lord_guild_name(17U), "No Club") == 0);

    state.guild_status.supported = true;
    state.guild_status.guild_id = 0U;
    state.screen = LORD_SCREEN_GUILD;
    CHECK(lord_menu_count(&state) == 5U);
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_GUILD_TARGET);
    CHECK(state.guild_target == LORD_GUILD_TARGET_JOIN);
    CHECK(lord_cancel(&state) == LORD_EVENT_CONFIRM);
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_GUILD_CREATE);
    CHECK(lord_menu_count(&state) == LORD_GUILD_NAME_COUNT + 1U);
    state.selection = LORD_GUILD_NAME_COUNT;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_GUILD);
    state.selection = 3U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(strstr(state.message_line_1, "Charge") != NULL);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);

    state.guild_status.guild_id = 1U;
    state.guild_status.name_code = 1U;
    CHECK(lord_menu_count(&state) == 8U);
    state.selection = 3U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_GUILD_TARGET);
    CHECK(state.guild_target == LORD_GUILD_TARGET_CLASH);
    CHECK(lord_cancel(&state) == LORD_EVENT_CONFIRM);
    state.selection = 4U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.guild_target == LORD_GUILD_TARGET_CHEER);
    CHECK(lord_cancel(&state) == LORD_EVENT_CONFIRM);
    state.selection = 5U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_GUILD_STANDINGS);
    CHECK(lord_cancel(&state) == LORD_EVENT_CONFIRM);
    state.selection = 7U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_TOWN);

    state.screen = LORD_SCREEN_TEXT_EDITOR;
    state.selection = 0U;
    lord_move_selection(&state, -6);
    CHECK(state.selection == 38U);
    CHECK(strcmp(lord_keyboard_label(42U), "DONE") == 0);
    CHECK(strcmp(lord_keyboard_label(43U), "DEL") == 0);
}

static void test_shops_bank_and_transfer(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_DEATH_KNIGHT);
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.weapon == 1U);
    CHECK(state.player.gold == 300U);
    CHECK(state.player.strength == 15);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_BANK;
    state.selection = 0U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.gold == 0U);
    CHECK(state.player.bank == 300U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_BANK_TRANSFER);
    const uint32_t old_gold = state.realm[0].gold;
    state.selection = 0U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.bank == 200U);
    CHECK(state.realm[0].gold == old_gold + 100U);
}

static void test_forest_training_skills_and_dragon(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_THIEF);
    state.screen = LORD_SCREEN_FOREST;
    state.selection = 0U;
    state.rng_state = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_BATTLE);
    CHECK(state.battle_kind == LORD_BATTLE_FOREST);
    CHECK(state.player.forest_fights == LORD_FOREST_FIGHTS_PER_DAY - 1U);
    CHECK(state.enemy.name[0] != '\0');
    CHECK(state.enemy.weapon[0] != '\0');
    state.player.strength = 100000;
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(state.player.experience > 0U);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_TRAINING;
    state.selection = 0U;
    state.player.experience = 100U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.player.strength = 100000;
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(state.player.level == 2U);
    CHECK(state.player.skill[LORD_CLASS_THIEF] == 7U);
    CHECK(state.player.skill_uses[LORD_CLASS_THIEF] == 4U);

    state.screen = LORD_SCREEN_BATTLE;
    state.battle_kind = LORD_BATTLE_FOREST;
    state.player.strength = 10;
    state.player.hero_class = LORD_CLASS_DEATH_KNIGHT;
    state.player.skill[LORD_CLASS_DEATH_KNIGHT] = 5U;
    state.player.skill_uses[LORD_CLASS_DEATH_KNIGHT] = 1U;
    state.enemy = (lord_enemy_t){
        .hit_points = 1000, .max_hit_points = 1000,
        .strength = 1, .death_text = "done",
    };
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_HIT);
    CHECK(state.player.skill_uses[LORD_CLASS_DEATH_KNIGHT] == 0U);

    state.player.level = LORD_MAX_LEVEL;
    state.player.strength = 100000;
    state.player.hit_points = 5000;
    state.player.max_hit_points = 5000;
    state.player.forest_fights = 1U;
    state.player.seen_dragon = false;
    state.screen = LORD_SCREEN_FOREST;
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.battle_kind == LORD_BATTLE_DRAGON);
    const uint32_t sequence_before_victory = state.save_sequence;
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(state.screen == LORD_SCREEN_DRAGON_VICTORY);
    CHECK(state.battle_kind == LORD_BATTLE_NONE);
    CHECK(state.player.dragon_kills == 1U);
    CHECK(state.player.level == 1U);
    CHECK(state.player.hit_points == 25);
    CHECK(state.player.max_hit_points == 25);
    CHECK(state.player.strength == 12);
    CHECK(state.player.defense == 2);
    CHECK(state.player.gold == 500U);
    CHECK(state.player.amulet);
    CHECK(!state.player.seen_dragon);
    CHECK(state.save_sequence > sequence_before_victory);

    const uint8_t dragon_actor[LORD_SYNC_ACTOR_ID_BYTES] = {
        0xd0U, 0xd1U, 0xd2U, 0xd3U,
        0xd4U, 0xd5U, 0xd6U, 0xd7U,
        0xd8U, 0xd9U, 0xdaU, 0xdbU,
        0xdcU, 0xddU, 0xdeU, 0xdfU,
    };
    uint8_t dragon_record[LORD_SYNC_MAX_BYTES];
    const size_t dragon_record_bytes = lord_sync_encode(
        &state, dragon_actor, UINT64_C(0x55667788),
        dragon_record, sizeof(dragon_record));
    CHECK(dragon_record_bytes > 0U);
    lord_state_t synced_dragon;
    lord_sync_metadata_t dragon_metadata;
    CHECK(lord_sync_decode(&synced_dragon, &dragon_metadata,
                           dragon_actor, 0U,
                           dragon_record, dragon_record_bytes));
    CHECK(synced_dragon.player.dragon_kills == 1U);
    CHECK(synced_dragon.player.level == 1U);
    CHECK(synced_dragon.player.max_hit_points == 25);
    CHECK(!synced_dragon.player.seen_dragon);

    const uint32_t sequence_after_victory = state.save_sequence;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(state.return_screen == LORD_SCREEN_TOWN);
    CHECK(state.save_sequence == sequence_after_victory);
}

static void prepare_tactical_battle(lord_state_t *state,
                                    lord_class_t hero_class,
                                    lord_enemy_intent_t intent)
{
    enter_town(state, hero_class);
    state->screen = LORD_SCREEN_BATTLE;
    state->battle_kind = LORD_BATTLE_FOREST;
    state->enemy_intent = intent;
    state->selection = 0U;
    state->rng_state = UINT32_C(0x13579bdf);
    state->player.hit_points = 1000;
    state->player.max_hit_points = 1000;
    state->player.strength = 100;
    state->player.defense = 0;
    state->player.gold = 100U;
    state->enemy = (lord_enemy_t){
        .name = "Training target",
        .weapon = "Practice blade",
        .death_text = "The target yields.",
        .hit_points = 10000,
        .max_hit_points = 10000,
        .strength = 100,
        .defense = 0,
        .gold = 800U,
        .experience = 10U,
    };
}

static void test_tactical_combat_counters_and_classes(void)
{
    lord_state_t intent_a;
    lord_state_t intent_b;
    enter_town(&intent_a, LORD_CLASS_DEATH_KNIGHT);
    enter_town(&intent_b, LORD_CLASS_DEATH_KNIGHT);
    intent_a.screen = LORD_SCREEN_FOREST;
    intent_b.screen = LORD_SCREEN_FOREST;
    intent_a.selection = 0U;
    intent_b.selection = 0U;
    intent_a.rng_state = 1U;
    intent_b.rng_state = 1U;
    CHECK(lord_activate(&intent_a) == LORD_EVENT_CONFIRM);
    CHECK(lord_activate(&intent_b) == LORD_EVENT_CONFIRM);
    CHECK(intent_a.screen == LORD_SCREEN_BATTLE);
    CHECK(intent_b.screen == LORD_SCREEN_BATTLE);
    CHECK(intent_a.enemy_intent == intent_b.enemy_intent);
    CHECK(intent_a.enemy_intent <= LORD_ENEMY_INTENT_QUICK);

    lord_state_t guarded_power;
    prepare_tactical_battle(&guarded_power, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_POWER);
    CHECK(lord_menu_count(&guarded_power) == 6U);
    guarded_power.selection = 1U;
    CHECK(lord_activate(&guarded_power) == LORD_EVENT_HIT);
    const int32_t guarded_power_damage =
        1000 - guarded_power.player.hit_points;
    CHECK(guarded_power_damage > 0);

    lord_state_t guarded_quick;
    prepare_tactical_battle(&guarded_quick, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_QUICK);
    guarded_quick.selection = 1U;
    CHECK(lord_activate(&guarded_quick) == LORD_EVENT_HIT);
    const int32_t guarded_quick_damage =
        1000 - guarded_quick.player.hit_points;
    CHECK(guarded_quick_damage >= guarded_power_damage);

    lord_state_t open_power;
    prepare_tactical_battle(&open_power, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_POWER);
    open_power.selection = 0U;
    CHECK(lord_activate(&open_power) == LORD_EVENT_HIT);
    const int32_t open_power_damage = 1000 - open_power.player.hit_points;
    CHECK(guarded_power_damage * 2 < open_power_damage);

    lord_state_t strike_into_guard;
    prepare_tactical_battle(&strike_into_guard, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_GUARD);
    strike_into_guard.selection = 0U;
    CHECK(lord_activate(&strike_into_guard) == LORD_EVENT_HIT);
    const int32_t guarded_strike =
        10000 - strike_into_guard.enemy.hit_points;

    lord_state_t feint_into_guard;
    prepare_tactical_battle(&feint_into_guard, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_GUARD);
    feint_into_guard.selection = 3U;
    CHECK(lord_activate(&feint_into_guard) == LORD_EVENT_HIT);
    const int32_t guard_break = 10000 - feint_into_guard.enemy.hit_points;
    CHECK(guarded_strike > 0);
    CHECK(guard_break > guarded_strike * 4);

    lord_state_t reliable_strike;
    prepare_tactical_battle(&reliable_strike, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_STRIKE);
    reliable_strike.selection = 0U;
    CHECK(lord_activate(&reliable_strike) == LORD_EVENT_HIT);
    const int32_t strike_damage = 10000 - reliable_strike.enemy.hit_points;

    lord_state_t weak_feint;
    prepare_tactical_battle(&weak_feint, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_STRIKE);
    weak_feint.selection = 3U;
    CHECK(lord_activate(&weak_feint) == LORD_EVENT_HIT);
    const int32_t weak_feint_damage = 10000 - weak_feint.enemy.hit_points;
    CHECK(strike_damage > weak_feint_damage);

    lord_state_t quick_feint;
    prepare_tactical_battle(&quick_feint, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_QUICK);
    quick_feint.selection = 3U;
    CHECK(lord_activate(&quick_feint) == LORD_EVENT_HIT);
    const int32_t quick_feint_damage = 10000 - quick_feint.enemy.hit_points;
    CHECK(weak_feint_damage > quick_feint_damage);

    lord_state_t death_knight;
    prepare_tactical_battle(&death_knight, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_STRIKE);
    const uint8_t death_knight_uses =
        death_knight.player.skill_uses[LORD_CLASS_DEATH_KNIGHT];
    death_knight.selection = 2U;
    CHECK(lord_activate(&death_knight) == LORD_EVENT_HIT);
    CHECK(death_knight.player.skill_uses[LORD_CLASS_DEATH_KNIGHT] ==
          (uint8_t)(death_knight_uses - 1U));
    CHECK(1000 - death_knight.player.hit_points >
          1000 - reliable_strike.player.hit_points);
    const int32_t open_technique_damage =
        10000 - death_knight.enemy.hit_points;

    lord_state_t guarded_technique;
    prepare_tactical_battle(&guarded_technique, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_GUARD);
    guarded_technique.selection = 2U;
    CHECK(lord_activate(&guarded_technique) == LORD_EVENT_HIT);
    const int32_t guarded_technique_damage =
        10000 - guarded_technique.enemy.hit_points;
    CHECK(open_technique_damage > guarded_technique_damage * 2);

    lord_state_t mystic;
    prepare_tactical_battle(&mystic, LORD_CLASS_MYSTICAL,
                            LORD_ENEMY_INTENT_GUARD);
    mystic.player.hit_points = 500;
    const uint8_t mystic_uses =
        mystic.player.skill_uses[LORD_CLASS_MYSTICAL];
    mystic.selection = 2U;
    CHECK(lord_activate(&mystic) == LORD_EVENT_HIT);
    CHECK(mystic.player.hit_points > 500);
    CHECK(mystic.enemy.hit_points < 10000);
    CHECK(mystic.player.skill_uses[LORD_CLASS_MYSTICAL] ==
          (uint8_t)(mystic_uses - 1U));

    lord_state_t thief;
    prepare_tactical_battle(&thief, LORD_CLASS_THIEF,
                            LORD_ENEMY_INTENT_POWER);
    const uint8_t thief_uses = thief.player.skill_uses[LORD_CLASS_THIEF];
    thief.selection = 2U;
    CHECK(lord_activate(&thief) == LORD_EVENT_HIT);
    CHECK(thief.player.hit_points == 1000);
    CHECK(thief.player.gold == 100U);
    CHECK(thief.enemy.gold == 901U);
    CHECK(thief.enemy.hit_points < 10000);
    CHECK(thief.player.skill_uses[LORD_CLASS_THIEF] ==
          (uint8_t)(thief_uses - 1U));

    const uint32_t shadow_purse = thief.enemy.gold;
    thief.player.skill_uses[LORD_CLASS_THIEF] = 1U;
    thief.selection = 2U;
    CHECK(lord_activate(&thief) == LORD_EVENT_HIT);
    CHECK(thief.player.gold == 100U);
    CHECK(thief.enemy.gold == shadow_purse);
    thief.player.strength = LORD_COMBAT_STAT_MAX;
    thief.selection = 0U;
    CHECK(lord_activate(&thief) == LORD_EVENT_WIN);
    CHECK(thief.player.gold == 100U + shadow_purse);

    lord_state_t pvp_thief;
    prepare_tactical_battle(&pvp_thief, LORD_CLASS_THIEF,
                            LORD_ENEMY_INTENT_POWER);
    pvp_thief.battle_kind = LORD_BATTLE_PVP;
    pvp_thief.selected_player = 0U;
    pvp_thief.selection = 2U;
    CHECK(lord_activate(&pvp_thief) == LORD_EVENT_HIT);
    CHECK(pvp_thief.player.gold == 100U);
    CHECK(pvp_thief.enemy.gold == 800U);
}

static void test_lethal_failed_escape_is_persistable(void)
{
    lord_state_t base;
    prepare_tactical_battle(&base, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_POWER);
    base.player.hit_points = 1;
    base.player.high_spirits = true;
    base.player.gold = 1234U;
    base.player.experience = 1000U;
    base.player.forest_fights = 7U;
    base.enemy.strength = LORD_COMBAT_STAT_MAX;
    base.selection = 4U;

    bool found_failed_escape = false;
    for (uint32_t seed = 1U; seed < 256U && !found_failed_escape; ++seed) {
        lord_state_t attempt = base;
        attempt.rng_state = seed;
        attempt.save_dirty = false;
        const lord_event_t event = lord_activate(&attempt);
        if (event != LORD_EVENT_LOSE) {
            continue;
        }
        found_failed_escape = true;
        CHECK(attempt.screen == LORD_SCREEN_DEAD);
        CHECK(attempt.battle_kind == LORD_BATTLE_NONE);
        CHECK(attempt.player.hit_points == attempt.player.max_hit_points);
        CHECK(attempt.player.gold == 0U);
        CHECK(attempt.player.experience == 900U);
        CHECK(attempt.player.forest_fights == 0U);
        CHECK(!attempt.player.high_spirits);
        CHECK(attempt.save_dirty);

        uint8_t encoded[LORD_SAVE_MAX_BYTES];
        const size_t encoded_length = lord_save_encode(
            &attempt, encoded, sizeof(encoded));
        CHECK(encoded_length > 0U);
        lord_state_t restored;
        CHECK(lord_save_decode(&restored, encoded, encoded_length));
        CHECK(restored.player.hit_points == restored.player.max_hit_points);
        CHECK(restored.player.gold == 0U);
        CHECK(restored.player.experience == 900U);
        CHECK(restored.player.forest_fights == 0U);
        CHECK(restored.screen == LORD_SCREEN_TOWN);
        CHECK(restored.battle_kind == LORD_BATTLE_NONE);
    }
    CHECK(found_failed_escape);
}

static void test_progression_pacing_and_dormant_rewards(void)
{
    static const uint32_t expected_thresholds[LORD_MAX_LEVEL - 1U] = {
        50U, 200U, 650U, 1800U, 6000U, 16000U,
        34000U, 110000U, 300000U, 625000U, 1250000U,
    };
    /* Exact XP sums for each pinned eleven-monster tier. */
    static const uint32_t tier_experience[LORD_MAX_LEVEL - 1U] = {
        45U, 121U, 259U, 498U, 1821U, 4854U,
        8168U, 36681U, 88943U, 149140U, 304408U,
    };
    lord_state_t curve;
    enter_town(&curve, LORD_CLASS_MYSTICAL);
    uint32_t prior_threshold = 0U;
    uint32_t estimated_wins = 0U;
    for (uint8_t level = 1U; level < LORD_MAX_LEVEL; ++level) {
        curve.player.level = level;
        const lord_trainer_t *const trainer = lord_current_trainer(&curve);
        CHECK(trainer != NULL);
        if (trainer == NULL) {
            continue;
        }
        const uint32_t threshold = expected_thresholds[level - 1U];
        CHECK(trainer->experience_needed == threshold);
        const uint32_t needed = threshold - prior_threshold;
        const uint32_t numerator = needed * 11U;
        estimated_wins += (numerator + tier_experience[level - 1U] - 1U) /
            tier_experience[level - 1U];
        prior_threshold = threshold;
    }
    const uint32_t estimated_days = (estimated_wins + 11U) / 12U;
    CHECK(estimated_wins == 241U);
    CHECK(estimated_days >= 18U && estimated_days <= 25U);
    CHECK(lord_weapon(15U)->price == 8000000U);
    CHECK(lord_armor(15U)->price == 8000000U);

    lord_state_t plain;
    lord_state_t spirited;
    prepare_tactical_battle(&plain, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_STRIKE);
    prepare_tactical_battle(&spirited, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_STRIKE);
    spirited.player.high_spirits = true;
    CHECK(lord_activate(&plain) == LORD_EVENT_HIT);
    CHECK(lord_activate(&spirited) == LORD_EVENT_HIT);
    CHECK(10000 - spirited.enemy.hit_points >
          10000 - plain.enemy.hit_points);

    lord_state_t no_badges;
    lord_state_t badge_guard;
    prepare_tactical_battle(&no_badges, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_STRIKE);
    prepare_tactical_battle(&badge_guard, LORD_CLASS_DEATH_KNIGHT,
                            LORD_ENEMY_INTENT_STRIKE);
    badge_guard.player.friendship_badges = 20U;
    CHECK(lord_activate(&no_badges) == LORD_EVENT_HIT);
    CHECK(lord_activate(&badge_guard) == LORD_EVENT_HIT);
    CHECK((1000 - no_badges.player.hit_points) -
          (1000 - badge_guard.player.hit_points) == 8);

    lord_state_t lore;
    prepare_tactical_battle(&lore, LORD_CLASS_MYSTICAL,
                            LORD_ENEMY_INTENT_STRIKE);
    lore.player.fairy_lore = true;
    lore.selection = 4U;
    CHECK(lord_activate(&lore) == LORD_EVENT_CONFIRM);
    CHECK(lore.battle_kind == LORD_BATTLE_NONE);
    CHECK(lore.screen == LORD_SCREEN_MESSAGE);

    lord_state_t rebirth;
    enter_town(&rebirth, LORD_CLASS_DEATH_KNIGHT);
    rebirth.partner_index = 1;
    rebirth.realm[1].teamed = true;
    rebirth.player.max_hit_points += 5;
    rebirth.player.hit_points += 5;
    rebirth.screen = LORD_SCREEN_BATTLE;
    rebirth.battle_kind = LORD_BATTLE_DRAGON;
    rebirth.enemy = (lord_enemy_t){
        .name = "Red Dragon",
        .weapon = "Dragon Fire",
        .death_text = "The dragon yields.",
        .hit_points = 1,
        .max_hit_points = 1,
        .strength = 1,
        .defense = 0,
    };
    rebirth.player.strength = 100000;
    rebirth.selection = 0U;
    CHECK(lord_activate(&rebirth) == LORD_EVENT_WIN);
    CHECK(rebirth.player.dragon_kills == 1U);
    CHECK(rebirth.player.max_hit_points == 30);
    CHECK(rebirth.player.hit_points == 30);
    CHECK(rebirth.partner_index == 1);
    CHECK(rebirth.player.amulet);

    lord_state_t dragon;
    enter_town(&dragon, LORD_CLASS_MYSTICAL);
    dragon.player.level = LORD_MAX_LEVEL;
    dragon.player.forest_fights = 1U;
    dragon.screen = LORD_SCREEN_FOREST;
    dragon.selection = 1U;
    CHECK(lord_activate(&dragon) == LORD_EVENT_CONFIRM);
    CHECK(dragon.enemy.max_hit_points == 10000);
    CHECK(dragon.enemy.strength == 1500);
    CHECK(dragon.enemy.defense == 200);
}

static void test_mail_pvp_friendship_and_mentoring(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_DEATH_KNIGHT);
    CHECK(state.mail_count == 2U);
    CHECK(lord_mail_unread_count(&state) == 2U);

    state.screen = LORD_SCREEN_MAIL_COMPOSE;
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_TEXT_EDITOR);
    (void)strcpy(state.editor_text, "MEET ME AT THE INN");
    state.selection = 42U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.mail_count == 4U);
    CHECK(state.mail[2].outgoing);
    CHECK(strcmp(state.mail[2].body, "MEET ME AT THE INN") == 0);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_PLAYER_DETAIL;
    state.selected_player = 0U;
    state.selection = 0U;
    state.player.strength = 100000;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.save_dirty = false;
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(!state.realm[0].alive);
    CHECK(state.player.pvp_wins == 1U);
    CHECK(state.save_dirty);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_FRIENDSHIP_ACTION;
    state.selected_player = 1U;
    state.realm[1].trust = 70U;
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.partner_index == 1);
    CHECK(state.realm[1].teamed);
    CHECK(state.player.max_hit_points >= 25);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_FRIENDSHIP_ACTION;
    state.selected_player = 1U;
    state.friendship_actions = 1U;
    state.selection = 3U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.young_heroes_helped == 1U);
}

static void test_realm_bound_inn_sleep(void)
{
    lord_state_t local;
    enter_town(&local, LORD_CLASS_THIEF);
    local.player.bank = 100U;
    const uint16_t local_day = local.player.day;
    local.screen = LORD_SCREEN_INN;
    local.selection = 0U;
    CHECK(lord_activate(&local) == LORD_EVENT_CONFIRM);
    CHECK(local.screen == LORD_SCREEN_MESSAGE);
    CHECK(strstr(local.message_line_1, "Finish today's") != NULL);
    CHECK(local.player.day == local_day);
    CHECK(local.player.bank == 100U);

    local.player.forest_fights = 0U;
    local.player.high_spirits = true;
    local.screen = LORD_SCREEN_INN;
    local.selection = 0U;
    CHECK(lord_activate(&local) == LORD_EVENT_CONFIRM);
    CHECK(local.player.day == (uint16_t)(local_day + 1U));
    CHECK(local.player.bank == 100U);
    CHECK(!local.player.high_spirits);
    local.screen = LORD_SCREEN_INN;
    local.selection = 0U;
    CHECK(lord_activate(&local) == LORD_EVENT_CONFIRM);
    CHECK(local.player.day == (uint16_t)(local_day + 1U));

    lord_state_t bound;
    enter_town(&bound, LORD_CLASS_THIEF);
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        bound.sync_actor_id[index] = (uint8_t)(index + 1U);
    }
    bound.sync_server_revision = 0U;
    bound.sync_committed_save_sequence = 0U;
    bound.player.bank = 100U;
    bound.player.forest_fights = 0U;
    const uint16_t bound_day = bound.player.day;
    const uint32_t bound_sequence = bound.save_sequence;
    bound.screen = LORD_SCREEN_INN;
    bound.selection = 0U;
    CHECK(lord_activate(&bound) == LORD_EVENT_CONFIRM);
    CHECK(bound.screen == LORD_SCREEN_MESSAGE);
    CHECK(bound.return_screen == LORD_SCREEN_INN);
    CHECK(strstr(bound.message_line_1, "Mac realm") != NULL);
    CHECK(bound.player.day == bound_day);
    CHECK(bound.player.bank == 100U);
    CHECK(bound.player.forest_fights == 0U);
    CHECK(bound.save_sequence == bound_sequence);
}

static void test_friendship_hp_bonus_reversible(void)
{
    lord_state_t team;
    enter_town(&team, LORD_CLASS_DEATH_KNIGHT);
    const int32_t team_base_hp = team.player.max_hit_points;
    team.realm[1].trust = 70U;
    team.friendship_actions = 3U;
    team.screen = LORD_SCREEN_FRIENDSHIP_ACTION;
    team.selected_player = 1U;
    team.selection = 2U;
    CHECK(lord_activate(&team) == LORD_EVENT_CONFIRM);
    CHECK(team.player.max_hit_points == team_base_hp + 5);
    CHECK(team.partner_index == 1);

    CHECK(lord_activate(&team) == LORD_EVENT_CONFIRM);
    team.screen = LORD_SCREEN_FRIENDSHIP_ACTION;
    team.selected_player = 1U;
    team.selection = 2U;
    team.player.hit_points = team.player.max_hit_points;
    CHECK(lord_activate(&team) == LORD_EVENT_CONFIRM);
    CHECK(team.player.max_hit_points == team_base_hp);
    CHECK(team.player.hit_points == team_base_hp);
    CHECK(team.partner_index == -1);

    CHECK(lord_activate(&team) == LORD_EVENT_CONFIRM);
    team.screen = LORD_SCREEN_FRIENDSHIP_ACTION;
    team.selected_player = 1U;
    team.selection = 2U;
    CHECK(lord_activate(&team) == LORD_EVENT_CONFIRM);
    CHECK(team.player.max_hit_points == team_base_hp + 5);

    lord_state_t npc;
    enter_town(&npc, LORD_CLASS_MYSTICAL);
    const int32_t npc_base_hp = npc.player.max_hit_points;
    npc.player.charm = 40U;
    npc.friendship_actions = 2U;
    npc.screen = LORD_SCREEN_SETH;
    npc.selection = 4U;
    CHECK(lord_activate(&npc) == LORD_EVENT_CONFIRM);
    CHECK(npc.npc_friend == 0);
    CHECK(npc.player.max_hit_points == npc_base_hp + 5);

    CHECK(lord_activate(&npc) == LORD_EVENT_CONFIRM);
    npc.screen = LORD_SCREEN_SETH;
    npc.selection = 4U;
    npc.player.hit_points = npc.player.max_hit_points;
    CHECK(lord_activate(&npc) == LORD_EVENT_CONFIRM);
    CHECK(npc.npc_friend == -1);
    CHECK(npc.player.max_hit_points == npc_base_hp);
    CHECK(npc.player.hit_points == npc_base_hp);
}

static void test_bartender_riddle_budget(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_MYSTICAL);
    state.friendship_actions = 1U;
    state.screen = LORD_SCREEN_BARTENDER;
    state.selection = 4U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.friendship_actions == 0U);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_BARTENDER;
    state.selection = 4U;
    const uint32_t rng_before = state.rng_state;
    const uint16_t charm_before = state.player.charm;
    const uint32_t sequence_before = state.save_sequence;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.friendship_actions == 0U);
    CHECK(state.rng_state == rng_before);
    CHECK(state.player.charm == charm_before);
    CHECK(state.save_sequence == sequence_before);
    CHECK(strstr(state.message_line_1, "No friendship games") != NULL);
}

static void test_forest_action_cost_and_real_rankings(void)
{
    lord_state_t base;
    enter_town(&base, LORD_CLASS_THIEF);
    bool found_old_man = false;
    for (uint32_t seed = 1U; seed < 10000U && !found_old_man; ++seed) {
        lord_state_t attempt = base;
        attempt.screen = LORD_SCREEN_FOREST;
        attempt.selection = 0U;
        attempt.player.forest_fights = 7U;
        attempt.rng_state = seed;
        CHECK(lord_activate(&attempt) == LORD_EVENT_CONFIRM);
        if (attempt.screen == LORD_SCREEN_MESSAGE &&
            strstr(attempt.message_line_1, "Old man rewards") != NULL) {
            found_old_man = true;
            CHECK(attempt.player.forest_fights == 6U);
        }
    }
    CHECK(found_old_man);

    lord_state_t rankings;
    enter_town(&rankings, LORD_CLASS_MYSTICAL);
    CHECK(lord_ranked_slot(&rankings, 0U) == 7);
    CHECK(lord_ranked_slot(&rankings, LORD_RANKING_COUNT) == -2);
    rankings.player.level = LORD_MAX_LEVEL;
    rankings.player.experience = 1250000U;
    CHECK(lord_ranked_slot(&rankings, 0U) == -1);
    uint8_t previous_level = UINT8_MAX;
    for (size_t rank = 0U; rank < LORD_RANKING_COUNT; ++rank) {
        const int8_t slot = lord_ranked_slot(&rankings, rank);
        const uint8_t level = slot < 0 ? rankings.player.level :
            rankings.realm[(size_t)slot].level;
        CHECK(level <= previous_level);
        previous_level = level;
    }

    /* Main-quest deeds outrank raw level: winning the Dragon and rebirthing
     * must never make a hero fall behind a level-12 camper. */
    rankings.realm[0].dragon_kills = 1U;
    rankings.realm[0].level = 1U;
    CHECK(lord_ranked_slot(&rankings, 0U) == 0);
    rankings.player.dragon_kills = 2U;
    rankings.player.level = 1U;
    rankings.player.experience = 0U;
    CHECK(lord_ranked_slot(&rankings, 0U) == -1);

    /* Realm directory pages may contain fewer than eight entries. Empty
     * actor-ID slots are not fake ranked players. */
    rankings.sync_actor_id[0] = 1U;
    rankings.realm_actor_ids[3][0] = 2U;
    rankings.realm[3].level = 7U;
    CHECK(lord_ranked_slot(&rankings, 0U) == -1);
    CHECK(lord_ranked_slot(&rankings, 1U) == 3);
    CHECK(lord_ranked_slot(&rankings, 2U) == -2);
}

static void test_full_inn_and_igms(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_MYSTICAL);
    state.player.gold = 1000000U;
    state.player.gems = 10U;
    state.player.charm = 40U;

    state.screen = LORD_SCREEN_BARTENDER;
    state.selection = 0U;
    state.player.hit_points = 10;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.gold == 999990U);
    CHECK(state.player.hit_points == 12);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_SETH;
    state.selection = 4U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.npc_friend == 0);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_DRAGON_DICE;
    state.selection = 0U;
    const uint32_t chomp_before = state.player.gold;
    const uint16_t badges_before = state.player.friendship_badges;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.gold == chomp_before - 5U);
    CHECK(state.dice_active);
    CHECK(state.dice_rolls == 2U);
    CHECK(state.dice_player >= 2U && state.dice_player <= 12U);
    CHECK(state.player.friendship_badges == badges_before);
    CHECK(lord_menu_count(&state) == 3U);

    state.screen = LORD_SCREEN_INN;
    state.selection = 8U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    (void)strcpy(state.editor_text, "DRAGON AT MIDNIGHT");
    state.selection = 42U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(strcmp(state.announcement, "DRAGON AT MIDNIGHT") == 0);

    for (uint8_t module = 0U; module < LORD_IGM_COUNT; ++module) {
        if (state.screen == LORD_SCREEN_MESSAGE) {
            (void)lord_activate(&state);
        }
        state.screen = LORD_SCREEN_IGM_DETAIL;
        state.selected_igm = module;
        state.selection = 0U;
        state.player.gold = 1000000U;
        state.player.forest_fights = 10U;
        CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
        CHECK((state.igm_used_mask & (uint8_t)(1U << module)) != 0U);
    }
    CHECK(state.igm_used_mask == UINT8_C(0x7f));

    state.screen = LORD_SCREEN_INN;
    state.player.forest_fights = 0U;
    state.selection = 0U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.igm_used_mask == 0U);
    CHECK(state.player.forest_fights >= LORD_FOREST_FIGHTS_PER_DAY);
}

static uint16_t expected_quiz_answer(const lord_state_t *state)
{
    if (state->quiz_operator == 0U) {
        return (uint16_t)(state->quiz_left + state->quiz_right);
    }
    if (state->quiz_operator == 1U) {
        return (uint16_t)(state->quiz_left - state->quiz_right);
    }
    return (uint16_t)(state->quiz_left * state->quiz_right);
}

static void test_player_driven_tavern_games(void)
{
    lord_state_t quiz;
    enter_town(&quiz, LORD_CLASS_MYSTICAL);
    quiz.player.gold = 100U;
    quiz.screen = LORD_SCREEN_IGM_DETAIL;
    quiz.selected_igm = 0U;
    quiz.selection = 0U;
    CHECK(lord_activate(&quiz) == LORD_EVENT_CONFIRM);
    CHECK(quiz.screen == LORD_SCREEN_ARAGORN_QUIZ);
    CHECK(quiz.player.gold == 95U);
    CHECK(quiz.quiz_wager == 5U);
    CHECK((quiz.igm_used_mask & 1U) != 0U);
    CHECK(lord_menu_count(&quiz) == 4U);
    CHECK(quiz.quiz_correct < 4U);
    CHECK(quiz.quiz_answers[quiz.quiz_correct] ==
          expected_quiz_answer(&quiz));
    for (size_t left = 0U; left < 4U; ++left) {
        for (size_t right = left + 1U; right < 4U; ++right) {
            CHECK(quiz.quiz_answers[left] != quiz.quiz_answers[right]);
        }
    }
    quiz.selection = quiz.quiz_correct;
    CHECK(lord_activate(&quiz) == LORD_EVENT_CONFIRM);
    CHECK(quiz.screen == LORD_SCREEN_MESSAGE);
    CHECK(quiz.player.gold == 105U);
    CHECK(quiz.player.high_spirits);

    lord_state_t wrong;
    enter_town(&wrong, LORD_CLASS_THIEF);
    wrong.player.gold = 100U;
    wrong.screen = LORD_SCREEN_IGM_DETAIL;
    wrong.selected_igm = 0U;
    wrong.selection = 1U;
    CHECK(lord_activate(&wrong) == LORD_EVENT_CONFIRM);
    CHECK(wrong.player.gold == 80U);
    wrong.selection = (uint8_t)((wrong.quiz_correct + 1U) % 4U);
    CHECK(lord_activate(&wrong) == LORD_EVENT_CONFIRM);
    CHECK(wrong.player.gold == 80U);
    CHECK(!wrong.player.high_spirits);

    lord_state_t poor;
    enter_town(&poor, LORD_CLASS_DEATH_KNIGHT);
    poor.player.gold = 4U;
    poor.screen = LORD_SCREEN_IGM_DETAIL;
    poor.selected_igm = 0U;
    poor.selection = 0U;
    const uint32_t poor_rng = poor.rng_state;
    const uint32_t poor_sequence = poor.save_sequence;
    CHECK(lord_activate(&poor) == LORD_EVENT_CONFIRM);
    CHECK(poor.player.gold == 4U);
    CHECK(poor.rng_state == poor_rng);
    CHECK(poor.igm_used_mask == 0U);
    CHECK(poor.save_sequence == poor_sequence);

    lord_state_t interrupted;
    enter_town(&interrupted, LORD_CLASS_MYSTICAL);
    interrupted.player.gold = 100U;
    interrupted.screen = LORD_SCREEN_IGM_DETAIL;
    interrupted.selected_igm = 0U;
    interrupted.selection = 0U;
    CHECK(lord_activate(&interrupted) == LORD_EVENT_CONFIRM);
    uint8_t save[LORD_SAVE_MAX_BYTES];
    const size_t save_bytes = lord_save_encode(
        &interrupted, save, sizeof(save));
    CHECK(save_bytes > 0U);
    lord_state_t restored;
    CHECK(lord_save_decode(&restored, save, save_bytes));
    CHECK(restored.screen == LORD_SCREEN_TOWN);
    CHECK(restored.player.gold == 95U);
    CHECK((restored.igm_used_mask & 1U) != 0U);
    CHECK(restored.quiz_wager == 0U);
    CHECK(lord_cancel(&interrupted) == LORD_EVENT_CONFIRM);
    CHECK(interrupted.screen == LORD_SCREEN_IGM);
    CHECK(interrupted.player.gold == 95U);

    lord_state_t dice;
    enter_town(&dice, LORD_CLASS_DEATH_KNIGHT);
    dice.player.gold = 100U;
    dice.screen = LORD_SCREEN_INN;
    dice.selection = 6U;
    CHECK(lord_activate(&dice) == LORD_EVENT_CONFIRM);
    CHECK(dice.screen == LORD_SCREEN_DRAGON_DICE);
    CHECK(lord_menu_count(&dice) == 3U);
    dice.selection = 0U;
    CHECK(lord_activate(&dice) == LORD_EVENT_CONFIRM);
    CHECK(dice.player.gold == 95U);
    CHECK(dice.dice_active);
    CHECK(dice.dice_rolls == 2U);
    const uint16_t dice_badges = dice.player.friendship_badges;
    dice.dice_player = 2U;
    dice.rng_state = 1U;
    dice.selection = 0U;
    CHECK(lord_activate(&dice) == LORD_EVENT_CONFIRM);
    CHECK(dice.player.gold == 95U);
    CHECK(dice.dice_player == 6U);
    CHECK(dice.dice_active);
    dice.dice_player = 17U;
    dice.selection = 1U;
    CHECK(lord_activate(&dice) == LORD_EVENT_CONFIRM);
    CHECK(!dice.dice_active);
    CHECK(dice.player.gold == 95U || dice.player.gold == 100U ||
          dice.player.gold == 103U);
    CHECK(dice.player.friendship_badges == dice_badges);

    lord_state_t bust;
    enter_town(&bust, LORD_CLASS_THIEF);
    bust.player.gold = 95U;
    bust.screen = LORD_SCREEN_DRAGON_DICE;
    bust.dice_active = true;
    bust.dice_player = 18U;
    bust.dice_rolls = 3U;
    bust.rng_state = 1U;
    bust.selection = 0U;
    CHECK(lord_activate(&bust) == LORD_EVENT_CONFIRM);
    CHECK(!bust.dice_active);
    CHECK(bust.player.gold == 95U);
    CHECK(strstr(bust.battle_line, "Bust") != NULL);

    lord_state_t forfeit;
    enter_town(&forfeit, LORD_CLASS_THIEF);
    forfeit.player.gold = 100U;
    forfeit.screen = LORD_SCREEN_INN;
    forfeit.selection = 6U;
    CHECK(lord_activate(&forfeit) == LORD_EVENT_CONFIRM);
    forfeit.selection = 0U;
    CHECK(lord_activate(&forfeit) == LORD_EVENT_CONFIRM);
    CHECK(lord_cancel(&forfeit) == LORD_EVENT_CONFIRM);
    CHECK(forfeit.screen == LORD_SCREEN_INN);
    CHECK(forfeit.player.gold == 95U);
    CHECK(!forfeit.dice_active);
}

static uint32_t find_dragon_dice_win_seed(const lord_state_t *state)
{
    for (uint32_t seed = 1U; seed < 4096U; ++seed) {
        lord_state_t probe = *state;
        probe.screen = LORD_SCREEN_DRAGON_DICE;
        probe.selection = 1U;
        probe.dice_active = true;
        probe.dice_player = 18U;
        probe.dice_host = 0U;
        probe.dice_rolls = 2U;
        probe.rng_state = seed;
        const uint32_t gold_before = probe.player.gold;
        if (lord_activate(&probe) == LORD_EVENT_CONFIRM &&
            probe.player.gold == gold_before + 8U) {
            return seed;
        }
    }
    CHECK(false);
    return 1U;
}

static void force_dragon_dice_win(lord_state_t *state, uint32_t seed)
{
    state->screen = LORD_SCREEN_DRAGON_DICE;
    state->selection = 1U;
    state->dice_active = true;
    state->dice_player = 18U;
    state->dice_host = 0U;
    state->dice_rolls = 2U;
    state->rng_state = seed;
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(!state->dice_active);
}

static void test_dragon_dice_daily_friendship_cap(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_DEATH_KNIGHT);
    state.player.gold = 95U; /* A five-coin stake has already been paid. */
    const uint16_t charm_before = state.player.charm;
    const uint32_t winning_seed = find_dragon_dice_win_seed(&state);

    force_dragon_dice_win(&state, winning_seed);
    CHECK(state.player.gold == 103U);
    CHECK(state.player.charm == (uint16_t)(charm_before + 1U));
    CHECK(state.player.high_spirits);
    CHECK((state.igm_used_mask & LORD_DAILY_DICE_FRIENDSHIP_MASK) != 0U);
    CHECK(strstr(state.battle_line, "today's friendship cheer") != NULL);

    /* The once-per-day marker must survive a cartridge save/relaunch. */
    uint8_t save[LORD_SAVE_MAX_BYTES];
    const size_t save_bytes = lord_save_encode(&state, save, sizeof(save));
    CHECK(save_bytes > 0U);
    lord_state_t restored;
    CHECK(lord_save_decode(&restored, save, save_bytes));
    CHECK((restored.igm_used_mask & LORD_DAILY_DICE_FRIENDSHIP_MASK) != 0U);
    CHECK(restored.player.charm == (uint16_t)(charm_before + 1U));

    const uint32_t second_gold_before = restored.player.gold;
    force_dragon_dice_win(&restored, winning_seed);
    CHECK(restored.player.gold == second_gold_before + 8U);
    CHECK(restored.player.charm == (uint16_t)(charm_before + 1U));
    CHECK(strstr(restored.battle_line, "already earned") != NULL);

    const uint16_t day_before = restored.player.day;
    restored.screen = LORD_SCREEN_INN;
    restored.selection = 0U;
    restored.player.forest_fights = 0U;
    CHECK(lord_activate(&restored) == LORD_EVENT_CONFIRM);
    CHECK(restored.player.day == (uint16_t)(day_before + 1U));
    CHECK((restored.igm_used_mask & LORD_DAILY_DICE_FRIENDSHIP_MASK) == 0U);
    CHECK(!restored.player.high_spirits);

    const uint32_t third_gold_before = restored.player.gold;
    force_dragon_dice_win(&restored, winning_seed);
    CHECK(restored.player.gold == third_gold_before + 8U);
    CHECK(restored.player.charm == (uint16_t)(charm_before + 2U));
    CHECK(restored.player.high_spirits);
    CHECK((restored.igm_used_mask & LORD_DAILY_DICE_FRIENDSHIP_MASK) != 0U);
}

static uint32_t test_save_crc32(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = UINT32_C(0) - (crc & UINT32_C(1));
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static void test_save_header(uint8_t *bytes, size_t length, uint16_t version)
{
    bytes[4] = (uint8_t)version;
    bytes[5] = (uint8_t)(version >> 8U);
    bytes[6] = (uint8_t)length;
    bytes[7] = (uint8_t)(length >> 8U);
    const uint32_t crc = test_save_crc32(bytes + 16U, length - 16U);
    bytes[8] = (uint8_t)crc;
    bytes[9] = (uint8_t)(crc >> 8U);
    bytes[10] = (uint8_t)(crc >> 16U);
    bytes[11] = (uint8_t)(crc >> 24U);
}

static void test_save_round_trip(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_THIEF);
    state.player.gold = 12345U;
    state.player.bank = 67890U;
    state.player.gems = 9U;
    state.player.young_heroes_helped = 3U;
    state.player.horse = true;
    state.player.fairy = true;
    state.player.amulet = true;
    state.player.skill[LORD_CLASS_MYSTICAL] = 17U;
    state.player.skill_uses[LORD_CLASS_MYSTICAL] = 4U;
    state.realm[1].trust = 55U;
    state.partner_index = 1;
    state.realm[1].teamed = true;
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        state.realm_actor_ids[1][index] = (uint8_t)(index + 1U);
        state.partner_actor_id[index] = (uint8_t)(index + 1U);
        state.sync_actor_id[index] = (uint8_t)(0xa0U + index);
    }
    state.last_realm_event_id = UINT64_C(123456789);
    state.sync_server_revision = 17U;
    state.sync_committed_save_sequence = 40U;
    (void)strcpy(state.conversation, "THE DRAGON IS AWAKE");
    (void)strcpy(state.announcement, "MEET IN THE FOREST");
    state.save_sequence = 42U;

    uint8_t encoded[LORD_SAVE_MAX_BYTES];
    uint8_t duplicate[LORD_SAVE_MAX_BYTES];
    const size_t encoded_length = lord_save_encode(
        &state, encoded, sizeof(encoded));
    const size_t duplicate_length = lord_save_encode(
        &state, duplicate, sizeof(duplicate));
    CHECK(encoded_length > 2000U);
    CHECK(encoded_length < sizeof(encoded));
    CHECK(encoded_length == duplicate_length);
    CHECK(memcmp(encoded, duplicate, encoded_length) == 0);

    lord_state_t restored;
    CHECK(lord_save_decode(&restored, encoded, encoded_length));
    CHECK(restored.screen == LORD_SCREEN_TOWN);
    CHECK(strcmp(restored.player.name, "Test Hero") == 0);
    CHECK(restored.player.gold == 12345U);
    CHECK(restored.player.bank == 67890U);
    CHECK(restored.player.gems == 9U);
    CHECK(restored.player.young_heroes_helped == 3U);
    CHECK(restored.player.horse && restored.player.fairy);
    CHECK(restored.player.amulet);
    CHECK(restored.player.skill[LORD_CLASS_MYSTICAL] == 17U);
    CHECK(restored.partner_index == 1);
    CHECK(memcmp(restored.realm_actor_ids[1], state.realm_actor_ids[1],
                 LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(memcmp(restored.partner_actor_id, state.partner_actor_id,
                 LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(restored.last_realm_event_id == UINT64_C(123456789));
    CHECK(memcmp(restored.sync_actor_id, state.sync_actor_id,
                 LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(restored.sync_server_revision == 17U);
    CHECK(restored.sync_committed_save_sequence == 40U);
    CHECK(strcmp(restored.conversation, "THE DRAGON IS AWAKE") == 0);
    CHECK(restored.save_sequence == 42U);
    CHECK(!restored.save_dirty);

    enum {
        LORD_V4_SAVE_EXTENSION_BYTES = 4 + 8 * 16 + 16 + 8,
        LORD_V5_SAVE_EXTENSION_BYTES = 16 + 4 + 4,
    };
    const size_t v4_length = encoded_length - LORD_V5_SAVE_EXTENSION_BYTES;
    const size_t v4_marker = v4_length - LORD_V4_SAVE_EXTENSION_BYTES;
    encoded[v4_marker + 3U] = '4';
    test_save_header(encoded, v4_length, 4U);
    CHECK(lord_save_decode(&restored, encoded, v4_length));
    CHECK(restored.last_realm_event_id == UINT64_C(123456789));
    CHECK(restored.partner_actor_id[0] == 1U);
    CHECK(restored.sync_actor_id[0] == 0U);
    CHECK(restored.sync_server_revision == 0U);
    CHECK(restored.sync_committed_save_sequence == 0U);

    memcpy(encoded, duplicate, encoded_length);
    const size_t v3_length = encoded_length - LORD_V4_SAVE_EXTENSION_BYTES -
        LORD_V5_SAVE_EXTENSION_BYTES;
    test_save_header(encoded, v3_length, LORD_SAVE_MINIMUM_VERSION);
    CHECK(lord_save_decode(&restored, encoded, v3_length));
    CHECK(restored.last_realm_event_id == 0U);
    CHECK(restored.partner_actor_id[0] == 0U);
    memcpy(encoded, duplicate, encoded_length);

    encoded[encoded_length - 1U] ^= UINT8_C(0x80);
    CHECK(!lord_save_decode(&restored, encoded, encoded_length));
    encoded[encoded_length - 1U] ^= UINT8_C(0x80);
    memset(encoded + encoded_length - 4U, 0, 4U);
    test_save_header(encoded, encoded_length, LORD_SAVE_FORMAT_VERSION);
    CHECK(lord_save_decode(&restored, encoded, encoded_length));
    CHECK(restored.sync_server_revision == 17U);
    CHECK(restored.sync_committed_save_sequence == 0U);
    memcpy(encoded, duplicate, encoded_length);
    CHECK(!lord_save_decode(&restored, encoded, encoded_length - 1U));
    CHECK(lord_save_encode(&state, encoded, 16U) == 0U);
}

static void test_save_migrations_and_combat_bounds(void)
{
    enum {
        LORD_V4_SAVE_EXTENSION_BYTES = 4 + 8 * 16 + 16 + 8,
        LORD_V5_SAVE_EXTENSION_BYTES = 16 + 4 + 4,
    };
    lord_state_t legacy;
    enter_town(&legacy, LORD_CLASS_MYSTICAL);
    legacy.player.dragon_kills = 1U;
    legacy.player.amulet = false;
    legacy.save_sequence = 77U;

    uint8_t original[LORD_SAVE_MAX_BYTES];
    uint8_t candidate[LORD_SAVE_MAX_BYTES];
    const size_t original_length = lord_save_encode(
        &legacy, original, sizeof(original));
    CHECK(original_length > 0U);

    lord_state_t restored;
    memcpy(candidate, original, original_length);
    CHECK(lord_save_decode(&restored, candidate, original_length));
    CHECK(restored.player.amulet);
    CHECK(restored.save_dirty);
    CHECK(restored.save_sequence == 78U);

    const size_t v4_length = original_length - LORD_V5_SAVE_EXTENSION_BYTES;
    const size_t v4_marker = v4_length - LORD_V4_SAVE_EXTENSION_BYTES;
    memcpy(candidate, original, original_length);
    candidate[v4_marker + 3U] = '4';
    test_save_header(candidate, v4_length, 4U);
    CHECK(lord_save_decode(&restored, candidate, v4_length));
    CHECK(restored.player.amulet);
    CHECK(restored.save_dirty);
    CHECK(restored.save_sequence == 78U);

    const size_t v3_length = original_length -
        LORD_V4_SAVE_EXTENSION_BYTES - LORD_V5_SAVE_EXTENSION_BYTES;
    memcpy(candidate, original, original_length);
    test_save_header(candidate, v3_length, LORD_SAVE_MINIMUM_VERSION);
    CHECK(lord_save_decode(&restored, candidate, v3_length));
    CHECK(restored.player.amulet);
    CHECK(restored.save_dirty);
    CHECK(restored.save_sequence == 78U);

    legacy.player.dragon_kills = 0U;
    legacy.player.amulet = false;
    legacy.player.strength = LORD_COMBAT_STAT_MAX + 1;
    const size_t oversized_length = lord_save_encode(
        &legacy, candidate, sizeof(candidate));
    CHECK(oversized_length > 0U);
    CHECK(!lord_save_decode(&restored, candidate, oversized_length));
}

static void make_legacy_empty_realm_slots(lord_state_t *state)
{
    state->partner_index = -1;
    memset(state->partner_actor_id, 0, sizeof(state->partner_actor_id));
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        memset(&state->realm[index], 0, sizeof(state->realm[index]));
        (void)strcpy(state->realm[index].name, "Empty record");
        state->realm[index].max_hit_points = 1;
        state->realm[index].strength = 1;
        memset(state->realm_actor_ids[index], 0,
               LORD_SYNC_ACTOR_ID_BYTES);
    }
}

static void test_legacy_empty_realm_slot_migration(void)
{
    lord_state_t legacy;
    enter_town(&legacy, LORD_CLASS_THIEF);
    make_legacy_empty_realm_slots(&legacy);
    legacy.save_sequence = 34U;
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        legacy.sync_actor_id[index] = (uint8_t)(0x70U + index);
    }
    legacy.sync_server_revision = 17U;
    legacy.sync_committed_save_sequence = 34U;

    uint8_t saved[LORD_SAVE_MAX_BYTES];
    const size_t saved_bytes = lord_save_encode(
        &legacy, saved, sizeof(saved));
    CHECK(saved_bytes > 0U);
    lord_state_t restored;
    CHECK(lord_save_decode(&restored, saved, saved_bytes));
    CHECK(restored.save_sequence == 35U);
    CHECK(restored.save_dirty);
    CHECK(restored.save_local_generation == 1U);
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        CHECK(strcmp(restored.realm[index].name, "Empty record") == 0);
        CHECK(restored.realm[index].level == 1U);
        CHECK(restored.realm[index].max_hit_points == 1);
        CHECK(restored.realm[index].strength == 1);
    }

    uint8_t record[LORD_SYNC_MAX_BYTES];
    const size_t record_bytes = lord_sync_encode(
        &legacy, legacy.sync_actor_id, UINT64_C(0x1234),
        record, sizeof(record));
    CHECK(record_bytes > 0U);
    lord_sync_metadata_t metadata;
    CHECK(lord_sync_decode(&restored, &metadata, legacy.sync_actor_id,
                           1U, record, record_bytes));
    CHECK(metadata.save_sequence == 34U);
    CHECK(restored.save_sequence == 35U);
    CHECK(restored.sync_committed_save_sequence == 34U);

    legacy.realm[0].gold = 1U;
    const size_t near_match_bytes = lord_save_encode(
        &legacy, saved, sizeof(saved));
    CHECK(near_match_bytes > 0U);
    CHECK(!lord_save_decode(&restored, saved, near_match_bytes));
    legacy.realm[0].gold = 0U;

    legacy.realm_actor_ids[0][0] = 1U;
    const size_t actor_bound_bytes = lord_save_encode(
        &legacy, saved, sizeof(saved));
    CHECK(actor_bound_bytes > 0U);
    CHECK(!lord_save_decode(&restored, saved, actor_bound_bytes));
    legacy.realm_actor_ids[0][0] = 0U;

    legacy.partner_index = 0;
    const size_t partnered_bytes = lord_save_encode(
        &legacy, saved, sizeof(saved));
    CHECK(partnered_bytes > 0U);
    CHECK(!lord_save_decode(&restored, saved, partnered_bytes));
}

static void test_backend_sync_envelope(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_MYSTICAL);
    state.realm_revision = 27U;
    state.save_sequence = 44U;
    state.player.gold = 1234U;
    state.realm[2].trust = 73U;
    const uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES] = {
        0x10U, 0x32U, 0x54U, 0x76U, 0x98U, 0xbaU, 0xdcU, 0xfeU,
        0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xabU, 0xcdU, 0xefU,
    };
    uint8_t record[LORD_SYNC_MAX_BYTES];
    const size_t record_bytes = lord_sync_encode(
        &state, actor_id, UINT64_C(0x1122334455667788),
        record, sizeof(record));
    CHECK(record_bytes > LORD_SAVE_MAX_BYTES / 2U);
    CHECK(record_bytes <= sizeof(record));

    lord_state_t restored;
    lord_sync_metadata_t metadata;
    CHECK(lord_sync_decode(&restored, &metadata, actor_id, 27U,
                           record, record_bytes));
    CHECK(metadata.realm_revision == 27U);
    CHECK(metadata.save_sequence == 44U);
    CHECK(metadata.operation_nonce == UINT64_C(0x1122334455667788));
    CHECK(restored.player.gold == 1234U);
    CHECK(restored.realm[2].trust == 73U);

    CHECK(!lord_sync_decode(&restored, &metadata, actor_id, 28U,
                            record, record_bytes));
    uint8_t wrong_actor[LORD_SYNC_ACTOR_ID_BYTES];
    (void)memcpy(wrong_actor, actor_id, sizeof(wrong_actor));
    wrong_actor[0] ^= UINT8_C(0xff);
    CHECK(!lord_sync_decode(&restored, &metadata, wrong_actor, 0U,
                            record, record_bytes));
    record[record_bytes - 1U] ^= UINT8_C(0x40);
    CHECK(!lord_sync_decode(&restored, &metadata, actor_id, 0U,
                            record, record_bytes));
    record[record_bytes - 1U] ^= UINT8_C(0x40);
    CHECK(lord_sync_encode(&state, actor_id, 0U,
                           record, sizeof(record)) == 0U);
}

typedef struct {
    uint8_t payload[LORD_SAVE_MAX_BYTES];
    size_t bytes;
    uint32_t expected_sequence;
    uint32_t sequence;
    p4_game_save_ticket_t ticket;
    bool defer_commit;
    bool reject_queue;
    bool reject_status;
    p4_game_save_status_t forced_status;
} save_mock_t;

enum {
    TEST_P4RM_HEADER_BYTES = 16,
    TEST_P4RM_HELLO = 1,
    TEST_P4RM_WELCOME = 2,
    TEST_P4RM_ACK = 5,
    TEST_P4RM_UPLOAD_BEGIN = 6,
    TEST_P4RM_UPLOAD_CHUNK = 7,
    TEST_P4RM_COMMIT_RESULT = 8,
    TEST_P4RM_CLOCK = 9,
    TEST_P4RM_PROFILE = 11,
    TEST_P4RM_DIRECTORY_SUMMARY = 12,
    TEST_P4RM_DIRECTORY_STATS = 13,
    TEST_P4RM_PROFILE_STATS = 14,
    TEST_P4RM_ACTION_BEGIN = 15,
    TEST_P4RM_ACTION_BODY = 16,
    TEST_P4RM_ACTION_RESULT = 17,
    TEST_P4RM_EVENT_BEGIN = 18,
    TEST_P4RM_EVENT_BODY = 19,
    TEST_P4RM_EVENT_ACK = 20,
    TEST_P4RM_DIRECTORY_PAGE = 21,
    TEST_P4RM_DIRECTORY_DEEDS = 22,
    TEST_P4RM_GUILD_STATUS = 23,
    TEST_P4RM_DIRECTORY_GUILD = 24,
    TEST_P4RM_GUILD_PAGE = 25,
    TEST_P4RM_GUILD_SUMMARY = 26,
    TEST_P4RM_BEGIN_INDEX = UINT16_MAX,
    TEST_P4RM_WELCOME_HAS_SNAPSHOT = 1U << 0U,
    TEST_P4RM_WELCOME_ACCEPT_LOCAL = 1U << 2U,
    TEST_P4RM_WELCOME_LOCAL_CONFLICT = 1U << 3U,
    TEST_P4RM_WELCOME_ADOPT_LOCAL = 1U << 4U,
    TEST_P4RM_HELLO_HAS_LOCAL = 1U << 0U,
    TEST_P4RM_HELLO_HAS_SYNC_BASE = 1U << 1U,
    TEST_P4RM_HELLO_LOCAL_DIRTY = 1U << 2U,
    TEST_P4RM_COMMIT_STALE_DAY = 4U,
};

typedef struct {
    p4_game_multiplayer_message_t incoming[8];
    size_t incoming_head;
    size_t incoming_count;
    uint8_t outgoing[P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES];
    size_t outgoing_bytes;
    uint32_t next_sequence;
    bool disconnected;
} realm_mock_t;

static void test_store_u16(uint8_t *bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
}

static void test_store_u32(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
    bytes[offset + 2U] = (uint8_t)(value >> 16U);
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

static void test_store_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    test_store_u32(bytes, offset, (uint32_t)value);
    test_store_u32(bytes, offset + 4U, (uint32_t)(value >> 32U));
}

static uint16_t test_load_u16(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)((uint16_t)bytes[offset] |
        (uint16_t)((uint16_t)bytes[offset + 1U] << 8U));
}

static uint32_t test_load_u32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
        (uint32_t)bytes[offset + 1U] << 8U |
        (uint32_t)bytes[offset + 2U] << 16U |
        (uint32_t)bytes[offset + 3U] << 24U;
}

static uint64_t test_load_u64(const uint8_t *bytes, size_t offset)
{
    return (uint64_t)test_load_u32(bytes, offset) |
        (uint64_t)test_load_u32(bytes, offset + 4U) << 32U;
}

static bool realm_mock_status(
    void *context, p4_game_multiplayer_status_t *status_out)
{
    const realm_mock_t *const mock = context;
    *status_out = (p4_game_multiplayer_status_t){
        .generation = 1U,
        .session_seed = UINT64_C(0x123456789abcdef0),
        .state = mock->disconnected ? P4_GAME_MULTIPLAYER_OFFLINE :
                                      P4_GAME_MULTIPLAYER_CONNECTED,
        .role = P4_GAME_MULTIPLAYER_ROLE_HOST,
        .local_player_slot = 0U,
        .player_count = 2U,
    };
    return true;
}

static bool realm_mock_send(
    void *context, const uint8_t *data, size_t data_bytes)
{
    realm_mock_t *const mock = context;
    if (mock->outgoing_bytes != 0U || data == NULL || data_bytes == 0U ||
        data_bytes > sizeof(mock->outgoing)) {
        return false;
    }
    memcpy(mock->outgoing, data, data_bytes);
    mock->outgoing_bytes = data_bytes;
    return true;
}

static bool realm_mock_receive(
    void *context, p4_game_multiplayer_message_t *message_out)
{
    realm_mock_t *const mock = context;
    if (mock->incoming_count == 0U) {
        return false;
    }
    *message_out = mock->incoming[mock->incoming_head];
    mock->incoming_head = (mock->incoming_head + 1U) % 8U;
    --mock->incoming_count;
    return true;
}

static void realm_mock_queue(
    realm_mock_t *mock,
    uint8_t kind,
    uint32_t transaction,
    uint16_t chunk_index,
    uint16_t chunk_count,
    const uint8_t *payload,
    size_t payload_bytes)
{
    CHECK(mock->incoming_count < 8U);
    CHECK(payload_bytes <=
          P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES - TEST_P4RM_HEADER_BYTES);
    if (mock->incoming_count >= 8U || payload_bytes >
            P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES -
                TEST_P4RM_HEADER_BYTES) {
        return;
    }
    const size_t tail = (mock->incoming_head + mock->incoming_count) % 8U;
    p4_game_multiplayer_message_t *const message = &mock->incoming[tail];
    *message = (p4_game_multiplayer_message_t){
        .sequence = ++mock->next_sequence,
        .player_slot = 1U,
        .bytes = (uint8_t)(TEST_P4RM_HEADER_BYTES + payload_bytes),
    };
    memcpy(message->data, "P4RM", 4U);
    message->data[4] = 3U;
    message->data[5] = kind;
    message->data[6] = 0U;
    message->data[7] = TEST_P4RM_HEADER_BYTES;
    test_store_u32(message->data, 8U, transaction);
    test_store_u16(message->data, 12U, chunk_index);
    test_store_u16(message->data, 14U, chunk_count);
    if (payload_bytes != 0U) {
        memcpy(message->data + TEST_P4RM_HEADER_BYTES,
               payload, payload_bytes);
    }
    ++mock->incoming_count;
}

static uint8_t realm_mock_take_kind(realm_mock_t *mock)
{
    CHECK(mock->outgoing_bytes >= TEST_P4RM_HEADER_BYTES);
    const uint8_t kind = mock->outgoing_bytes >= TEST_P4RM_HEADER_BYTES
        ? mock->outgoing[5] : 0U;
    mock->outgoing_bytes = 0U;
    return kind;
}

static bool mock_queue_save(void *context, const char *slot_id,
                            uint32_t schema_version,
                            uint32_t expected_sequence,
                            const uint8_t *data, size_t data_bytes,
                            p4_game_save_ticket_t *ticket_out)
{
    save_mock_t *const mock = context;
    if (mock->reject_queue || strcmp(slot_id, "AUTO") != 0 ||
        schema_version != LORD_SAVE_FORMAT_VERSION ||
        data_bytes > sizeof(mock->payload)) {
        return false;
    }
    memcpy(mock->payload, data, data_bytes);
    mock->bytes = data_bytes;
    mock->expected_sequence = expected_sequence;
    mock->sequence = 0U;
    mock->ticket = 7U;
    *ticket_out = mock->ticket;
    return true;
}

static bool mock_read_save(void *context, p4_game_save_ticket_t ticket,
                           p4_game_save_status_t *status_out,
                           uint32_t *sequence_out)
{
    save_mock_t *const mock = context;
    if (ticket != mock->ticket) {
        return false;
    }
    if (mock->reject_status) {
        return false;
    }
    if (mock->forced_status != P4_GAME_SAVE_NONE) {
        *status_out = mock->forced_status;
        *sequence_out = 0U;
        return true;
    }
    if (mock->defer_commit) {
        *status_out = P4_GAME_SAVE_QUEUED;
        *sequence_out = 0U;
        return true;
    }
    if (mock->sequence == 0U) {
        mock->sequence = mock->expected_sequence + 1U;
    }
    *status_out = P4_GAME_SAVE_COMMITTED;
    *sequence_out = mock->sequence;
    return true;
}

static bool start_game(p4_game_instance_t *instance, lord_state_t *state,
                       const p4_game_services_t *services)
{
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(instance, &p4_lord_game, services,
                                  state, sizeof(*state));
}

static void realm_mock_connect_bound(
    realm_mock_t *realm,
    p4_game_instance_t *instance,
    const lord_state_t *state)
{
    const p4_game_input_t idle = {0};
    CHECK(p4_game_instance_update(instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm->outgoing_bytes >= TEST_P4RM_HEADER_BYTES);
    CHECK(realm->outgoing[5] == TEST_P4RM_HELLO);
    const uint32_t hello_transaction = test_load_u32(realm->outgoing, 8U);
    (void)realm_mock_take_kind(realm);

    uint8_t welcome[36] = {0};
    memcpy(welcome, state->sync_actor_id, LORD_SYNC_ACTOR_ID_BYTES);
    test_store_u32(welcome, 16U, state->sync_server_revision);
    test_store_u64(welcome, 20U, UINT64_C(900));
    test_store_u32(welcome, 28U, 1800U);
    welcome[32] = TEST_P4RM_WELCOME_ACCEPT_LOCAL;
    test_store_u16(welcome, 33U, state->player.day);
    realm_mock_queue(realm, TEST_P4RM_WELCOME, hello_transaction,
                     0U, 0U, welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    const char *const label = lord_realm_net_label();
    CHECK(label != NULL && strcmp(label, "MAC REALM") == 0);

    /* A bound character publishes its derived directory projection after the
     * welcome.  Drain those two packets before injecting an event. */
    CHECK(realm->outgoing[5] == TEST_P4RM_PROFILE);
    (void)realm_mock_take_kind(realm);
    CHECK(p4_game_instance_update(instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm->outgoing[5] == TEST_P4RM_PROFILE_STATS);
    (void)realm_mock_take_kind(realm);
}

static void realm_mock_finish_action(
    realm_mock_t *realm,
    p4_game_instance_t *instance,
    uint32_t transaction,
    uint8_t status,
    uint8_t result_code,
    uint32_t result_value)
{
    const p4_game_input_t idle = {0};
    const uint8_t begin_ack = TEST_P4RM_ACTION_BEGIN;
    realm_mock_queue(realm, TEST_P4RM_ACK, transaction,
                     TEST_P4RM_BEGIN_INDEX, 0U, &begin_ack, 1U);
    CHECK(p4_game_instance_update(instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm->outgoing_bytes == 0U);

    uint8_t result[16] = {0};
    result[0] = status;
    result[1] = 10U;
    result[2] = result_code;
    test_store_u32(result, 4U, result_value);
    if (status == 0U) {
        test_store_u64(result, 8U, 1U);
    }
    realm_mock_queue(realm, TEST_P4RM_ACTION_RESULT, transaction,
                     0U, 0U, result, sizeof(result));
    CHECK(p4_game_instance_update(instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
}

static void test_adventure_club_client_protocol(void)
{
    static const p4_game_multiplayer_profile_t profile = {
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = 30U,
        .message_bytes = P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
        .protocol = UINT16_C(0x4c53),
    };
    const p4_game_input_t idle = {0};
    const p4_game_input_t activate = {
        .held = P4_BUTTON_A, .pressed = P4_BUTTON_A,
    };
    realm_mock_t realm = {0};
    save_mock_t save = {0};
    lord_state_t seed;
    enter_town(&seed, LORD_CLASS_THIEF);
    const uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES] = {
        0x31U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U,
    };
    const uint8_t rival_id[LORD_SYNC_ACTOR_ID_BYTES] = {
        0x81U, 0x82U, 0x83U, 0x84U, 0x85U, 0x86U, 0x87U, 0x88U,
        0x89U, 0x8aU, 0x8bU, 0x8cU, 0x8dU, 0x8eU, 0x8fU, 0x90U,
    };
    memcpy(seed.sync_actor_id, actor_id, sizeof(actor_id));
    seed.sync_server_revision = 1U;
    seed.sync_committed_save_sequence = seed.save_sequence;
    save.bytes = lord_save_encode(&seed, save.payload, sizeof(save.payload));
    CHECK(save.bytes != 0U);
    save.sequence = 1U;

    p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_SAVE |
            P4_GAME_CAP_MULTIPLAYER_SESSION,
        .save_context = &save,
        .save_data = save.payload,
        .save_bytes = save.bytes,
        .save_schema_version = LORD_SAVE_FORMAT_VERSION,
        .save_sequence = save.sequence,
        .queue_save = mock_queue_save,
        .read_save_status = mock_read_save,
        .multiplayer_context = &realm,
        .multiplayer_read_status = realm_mock_status,
        .multiplayer_send = realm_mock_send,
        .multiplayer_receive = realm_mock_receive,
        .multiplayer_profile = &profile,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    CHECK(start_game(&instance, &state, &services));
    realm_mock_connect_bound(&realm, &instance, &state);
    const lord_player_t personal_before = state.player;
    const uint32_t save_sequence_before = state.save_sequence;

    /* A new cartridge connected to an old hub can browse the hall, but it
     * must never emit ACTION_GUILD until an actor-bound STATUS proves support. */
    state.screen = LORD_SCREEN_GUILD_CREATE;
    state.selection = 0U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing_bytes == 0U);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(strstr(state.message_line_2, "upgrade") != NULL);

    uint8_t guild_status[48] = {0};
    memcpy(guild_status, actor_id, sizeof(actor_id));
    test_store_u16(guild_status, 36U, LORD_GUILD_QUEST_GOAL);
    guild_status[47] = 0x08U;

    /* Wrong actor, malformed singleton metadata, and a nine-member payload
     * are ignored without destroying or enabling status. */
    guild_status[0] ^= UINT8_C(0x01);
    realm_mock_queue(&realm, TEST_P4RM_GUILD_STATUS, UINT32_C(0x8000),
                     0U, 0U, guild_status, sizeof(guild_status));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.guild_status.supported);
    guild_status[0] ^= UINT8_C(0x01);
    realm_mock_queue(&realm, TEST_P4RM_GUILD_STATUS, UINT32_C(0x8001),
                     0U, 1U, guild_status, sizeof(guild_status));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.guild_status.supported);
    test_store_u32(guild_status, 16U, 77U);
    test_store_u16(guild_status, 20U, 1U);
    guild_status[22] = 9U;
    guild_status[23] = 2U;
    realm_mock_queue(&realm, TEST_P4RM_GUILD_STATUS, UINT32_C(0x8002),
                     0U, 0U, guild_status, sizeof(guild_status));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.guild_status.supported);

    memset(guild_status + 16U, 0, sizeof(guild_status) - 16U);
    test_store_u16(guild_status, 36U, LORD_GUILD_QUEST_GOAL);
    guild_status[47] = 0x08U;
    realm_mock_queue(&realm, TEST_P4RM_GUILD_STATUS, UINT32_C(0x8003),
                     0U, 0U, guild_status, sizeof(guild_status));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.guild_status.supported);
    guild_status[47] = 0U;
    realm_mock_queue(&realm, TEST_P4RM_GUILD_STATUS, UINT32_C(0x8004),
                     0U, 0U, guild_status, sizeof(guild_status));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.guild_status.supported);
    CHECK(state.guild_status.guild_id == 0U);

    /* CREATE carries only the fixed 1..16 name code and a zero target/body. */
    state.screen = LORD_SCREEN_GUILD_CREATE;
    state.selection = LORD_GUILD_NAME_COUNT - 1U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 10U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES + 1U] == 0U);
    CHECK(test_load_u16(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 2U) == 16U);
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES + 4U + index] == 0U);
    }
    CHECK(test_load_u16(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 28U) == 0U);
    CHECK(test_load_u32(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 32U) == 0U);
    uint32_t transaction = test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);
    realm_mock_finish_action(&realm, &instance, transaction, 0U, 0U, 16U);
    CHECK(strstr(state.message_line_1, "founded") != NULL);

    /* A valid maximum-sized member status is transient and render-safe. */
    memset(guild_status + 16U, 0, sizeof(guild_status) - 16U);
    test_store_u32(guild_status, 16U, 77U);
    test_store_u16(guild_status, 20U, LORD_GUILD_NAME_COUNT);
    guild_status[22] = 8U;
    guild_status[23] = 1U;
    test_store_u32(guild_status, 24U, UINT32_MAX);
    test_store_u32(guild_status, 28U, UINT32_MAX);
    test_store_u16(guild_status, 32U, UINT16_MAX);
    test_store_u16(guild_status, 34U, LORD_GUILD_QUEST_GOAL);
    test_store_u16(guild_status, 36U, LORD_GUILD_QUEST_GOAL);
    test_store_u16(guild_status, 38U, UINT16_MAX);
    test_store_u16(guild_status, 40U, UINT16_MAX);
    test_store_u16(guild_status, 42U, UINT16_MAX);
    guild_status[44] = 1U;
    test_store_u16(guild_status, 45U, 15U);
    guild_status[47] = 0x08U;
    realm_mock_queue(&realm, TEST_P4RM_GUILD_STATUS, UINT32_C(0x8005),
                     0U, 0U, guild_status, sizeof(guild_status));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.guild_status.supported);
    CHECK(state.guild_status.member_count == 8U);
    CHECK(state.guild_status.name_code == LORD_GUILD_NAME_COUNT);

    uint8_t directory_summary[44] = {0};
    memcpy(directory_summary, rival_id, sizeof(rival_id));
    memcpy(directory_summary + 16U, "Club Rival", 11U);
    directory_summary[36] = (uint8_t)LORD_HERO_STYLE_HERO;
    directory_summary[37] = (uint8_t)LORD_CLASS_MYSTICAL;
    directory_summary[38] = 5U;
    directory_summary[39] = 0x05U;
    uint8_t directory_guild[24] = {0};
    memcpy(directory_guild, rival_id, sizeof(rival_id));
    test_store_u32(directory_guild, 16U, 88U);
    test_store_u16(directory_guild, 20U, 15U);
    test_store_u16(directory_guild, 22U, 1U);
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_SUMMARY,
                     UINT32_C(0x8010), 0U, 1U,
                     directory_summary, sizeof(directory_summary));
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_GUILD,
                     UINT32_C(0x8011), 0U, 1U,
                     directory_guild, sizeof(directory_guild));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.realm[0].guild_id == 0U);
    test_store_u16(directory_guild, 22U, 0U);
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_GUILD,
                     UINT32_C(0x8012), 0U, 1U,
                     directory_guild, sizeof(directory_guild));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.realm[0].guild_id == 88U);
    CHECK(state.realm[0].guild_name_code == 15U);

    /* Opening standings emits the independent two-byte club page request. */
    state.screen = LORD_SCREEN_GUILD;
    state.selection = 5U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_GUILD_STANDINGS);
    CHECK(!state.guild_page_received);
    CHECK(realm.outgoing[5] == TEST_P4RM_GUILD_PAGE);
    CHECK(realm.outgoing_bytes == TEST_P4RM_HEADER_BYTES + 2U);
    CHECK(test_load_u16(realm.outgoing, TEST_P4RM_HEADER_BYTES) == 0U);
    (void)realm_mock_take_kind(&realm);

    uint8_t guild_page[4] = {0};
    test_store_u16(guild_page, 2U, 1U);
    realm_mock_queue(&realm, TEST_P4RM_GUILD_PAGE, UINT32_C(0x8020),
                     0U, 1U, guild_page, sizeof(guild_page));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.guild_page_total == 0U);
    CHECK(!state.guild_page_received);
    memset(guild_page, 0, sizeof(guild_page));
    realm_mock_queue(&realm, TEST_P4RM_GUILD_PAGE, UINT32_C(0x8025),
                     0U, 0U, guild_page, sizeof(guild_page));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.guild_page_received);
    CHECK(state.guild_page_total == 0U);
    for (size_t index = 0U; index < LORD_GUILD_SUMMARY_COUNT; ++index) {
        CHECK(!state.guild_summaries[index].valid);
    }
    test_store_u16(guild_page, 2U, 1U);
    realm_mock_queue(&realm, TEST_P4RM_GUILD_PAGE, UINT32_C(0x8021),
                     0U, 0U, guild_page, sizeof(guild_page));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.guild_page_received);
    CHECK(state.guild_page_total == 1U);

    uint8_t guild_summary[24] = {0};
    test_store_u32(guild_summary, 0U, 77U);
    test_store_u16(guild_summary, 4U, LORD_GUILD_NAME_COUNT);
    guild_summary[6] = 9U;
    guild_summary[7] = UINT8_MAX;
    test_store_u32(guild_summary, 8U, UINT32_MAX);
    test_store_u32(guild_summary, 12U, UINT32_MAX);
    test_store_u16(guild_summary, 16U, UINT16_MAX);
    test_store_u16(guild_summary, 18U, UINT16_MAX);
    test_store_u16(guild_summary, 20U, UINT16_MAX);
    realm_mock_queue(&realm, TEST_P4RM_GUILD_SUMMARY,
                     UINT32_C(0x8022), 0U, 1U,
                     guild_summary, sizeof(guild_summary));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.guild_summaries[0].valid);
    guild_summary[6] = 8U;
    test_store_u16(guild_summary, 22U, 1U);
    realm_mock_queue(&realm, TEST_P4RM_GUILD_SUMMARY,
                     UINT32_C(0x8023), 0U, 1U,
                     guild_summary, sizeof(guild_summary));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.guild_summaries[0].valid);
    test_store_u16(guild_summary, 22U, 0U);
    realm_mock_queue(&realm, TEST_P4RM_GUILD_SUMMARY,
                     UINT32_C(0x8024), 0U, 1U,
                     guild_summary, sizeof(guild_summary));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.guild_summaries[0].valid);
    CHECK(state.guild_summaries[0].member_count == 8U);
    CHECK(state.guild_summaries[0].prestige == UINT32_MAX);

    /* RALLY is targetless. Its shared points never touch the hero save. */
    state.screen = LORD_SCREEN_GUILD;
    state.selection = 0U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 10U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES + 1U] == 3U);
    CHECK(test_load_u16(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 2U) == 0U);
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES + 4U + index] == 0U);
    }
    transaction = test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);
    realm_mock_finish_action(&realm, &instance, transaction, 0U, 3U, 15U);
    CHECK(strstr(state.message_line_1, "15") != NULL);
    CHECK(memcmp(&state.player, &personal_before,
                 sizeof(state.player)) == 0);
    CHECK(state.save_sequence == save_sequence_before);

    /* A hub-side daily denial returns code/value/id zero and clears the
     * request with club-specific feedback, still without local spending. */
    state.screen = LORD_SCREEN_GUILD;
    state.selection = 1U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    transaction = test_load_u32(realm.outgoing, 8U);
    CHECK(realm_mock_take_kind(&realm) == TEST_P4RM_ACTION_BEGIN);
    realm_mock_finish_action(&realm, &instance, transaction, 3U, 0U, 0U);
    CHECK(strstr(state.message_line_1, "declined") != NULL);
    CHECK(memcmp(&state.player, &personal_before,
                 sizeof(state.player)) == 0);

    /* CLASH targets the selected member actor and maps result 1 to a win. */
    state.screen = LORD_SCREEN_GUILD_TARGET;
    state.guild_target = LORD_GUILD_TARGET_CLASH;
    state.selection = 0U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 10U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES + 1U] == 4U);
    CHECK(test_load_u16(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 2U) == 0U);
    CHECK(memcmp(realm.outgoing + TEST_P4RM_HEADER_BYTES + 4U,
                 rival_id, sizeof(rival_id)) == 0);
    transaction = test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);
    realm_mock_finish_action(&realm, &instance, transaction, 0U, 4U, 1U);
    CHECK(strstr(state.message_line_1, "won") != NULL);
    CHECK(memcmp(&state.player, &personal_before,
                 sizeof(state.player)) == 0);
    CHECK(state.save_sequence == save_sequence_before);

    /* Club projections do not enter LDSV5. Decode resumes with no transient
     * membership, directory annotation, or club page state. */
    uint8_t encoded[LORD_SAVE_MAX_BYTES];
    const size_t encoded_bytes = lord_save_encode(
        &state, encoded, sizeof(encoded));
    lord_state_t restored;
    CHECK(encoded_bytes == save.bytes);
    CHECK(lord_save_decode(&restored, encoded, encoded_bytes));
    CHECK(!restored.guild_status.supported);
    CHECK(restored.guild_status.guild_id == 0U);
    CHECK(restored.realm[0].guild_id == 0U);
    CHECK(restored.guild_page_total == 0U);
    CHECK(!restored.guild_page_received);
    CHECK(memcmp(&restored.player, &state.player,
                 sizeof(state.player)) == 0);

    realm.disconnected = true;
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.guild_status.supported);
    CHECK(!state.guild_page_received);
    state.screen = LORD_SCREEN_GUILD_CREATE;
    state.selection = 0U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing_bytes == 0U);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(strstr(state.message_line_1, "Connect") != NULL);
    CHECK(memcmp(&state.player, &personal_before,
                 sizeof(state.player)) == 0);
    p4_game_instance_stop(&instance);
}

static void test_pending_realm_debits_fail_offline_and_retry_once(void)
{
    static const p4_game_multiplayer_profile_t profile = {
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = 30U,
        .message_bytes = P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
        .protocol = UINT16_C(0x4c53),
    };
    static const struct {
        uint8_t kind;
        uint8_t code;
        uint32_t value;
        bool banked;
    } cases[] = {
        {2U, 1U, 100U, true},
        {3U, UINT8_C(0x81), 30U, false},
        {7U, 1U, 100U, false},
    };
    const p4_game_input_t idle = {0};
    const p4_game_input_t activate = {
        .held = P4_BUTTON_A, .pressed = P4_BUTTON_A,
    };

    for (size_t case_index = 0U;
         case_index < sizeof(cases) / sizeof(cases[0]); ++case_index) {
        realm_mock_t realm = {0};
        save_mock_t save = {0};
        lord_state_t seed;
        enter_town(&seed, LORD_CLASS_MYSTICAL);
        const uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES] = {
            (uint8_t)(0x20U + case_index), 2U, 3U, 4U,
            5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U,
        };
        const uint8_t source_id[LORD_SYNC_ACTOR_ID_BYTES] = {
            (uint8_t)(0x40U + case_index), 18U, 19U, 20U,
            21U, 22U, 23U, 24U, 25U, 26U, 27U, 28U,
            29U, 30U, 31U, 32U,
        };
        memcpy(seed.sync_actor_id, actor_id, sizeof(actor_id));
        memcpy(seed.realm_actor_ids[0], source_id, sizeof(source_id));
        seed.sync_server_revision = 1U;
        seed.sync_committed_save_sequence = seed.save_sequence;
        seed.player.gold = 99U;
        seed.player.bank = cases[case_index].banked ? 99U : 500U;
        save.bytes = lord_save_encode(&seed, save.payload,
                                      sizeof(save.payload));
        CHECK(save.bytes != 0U);
        save.sequence = 1U;

        p4_game_services_t services = {
            .available_capabilities = P4_GAME_CAP_VIDEO |
                P4_GAME_CAP_CONTROLS | P4_GAME_CAP_SAVE |
                P4_GAME_CAP_MULTIPLAYER_SESSION,
            .save_context = &save,
            .save_data = save.payload,
            .save_bytes = save.bytes,
            .save_schema_version = LORD_SAVE_FORMAT_VERSION,
            .save_sequence = save.sequence,
            .queue_save = mock_queue_save,
            .read_save_status = mock_read_save,
            .multiplayer_context = &realm,
            .multiplayer_read_status = realm_mock_status,
            .multiplayer_send = realm_mock_send,
            .multiplayer_receive = realm_mock_receive,
            .multiplayer_profile = &profile,
        };
        p4_game_instance_t instance;
        lord_state_t state;
        CHECK(start_game(&instance, &state, &services));
        realm_mock_connect_bound(&realm, &instance, &state);

        uint8_t event[48] = {0};
        const uint64_t event_id = UINT64_C(700) + case_index;
        test_store_u64(event, 0U, event_id);
        event[8] = cases[case_index].kind;
        event[9] = cases[case_index].code;
        test_store_u32(event, 12U, cases[case_index].value);
        memcpy(event + 16U, source_id, sizeof(source_id));
        memcpy(event + 32U, "Realm Friend", 12U);
        const uint32_t sequence_before = state.save_sequence;
        const uint64_t cursor_before = state.last_realm_event_id;
        realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN,
                         UINT32_C(0x7100) + (uint32_t)case_index,
                         TEST_P4RM_BEGIN_INDEX, 0U,
                         event, sizeof(event));
        CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
              P4_GAME_CONTINUE);
        CHECK(state.player.gold == 99U);
        CHECK(state.player.bank ==
              (cases[case_index].banked ? 99U : 500U));
        CHECK(state.save_sequence == sequence_before);
        CHECK(state.last_realm_event_id == cursor_before);
        CHECK(realm.outgoing_bytes == 0U);
        CHECK(lord_realm_net_label() == NULL);
        CHECK(strcmp(state.message_line_1,
                     "A realm ChompCoin payment is waiting.") == 0);
        CHECK(strcmp(state.message_line_2,
                     cases[case_index].banked ?
                        "Restore the banked amount, then reconnect." :
                        "Restore the carried amount, then reconnect.") == 0);

        /* OFFLINE means the retry cannot trap the player in the modal. */
        CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
              P4_GAME_CONTINUE);
        CHECK(state.screen == LORD_SCREEN_TOWN);

        if (cases[case_index].banked) {
            state.player.bank = 150U;
        } else {
            state.player.gold = 150U;
        }
        save.bytes = lord_save_encode(&state, save.payload,
                                      sizeof(save.payload));
        CHECK(save.bytes != 0U);
        services.save_bytes = save.bytes;
        services.save_sequence = 2U;
        save.sequence = 2U;
        save.ticket = P4_GAME_SAVE_INVALID_TICKET;
        p4_game_instance_stop(&instance);
        realm = (realm_mock_t){0};
        CHECK(start_game(&instance, &state, &services));
        realm_mock_connect_bound(&realm, &instance, &state);

        const uint8_t friendship_before = state.friendship_actions;
        const uint16_t charm_before = state.player.charm;
        const uint16_t losses_before = state.player.pvp_losses;
        realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN,
                         UINT32_C(0x7200) + (uint32_t)case_index,
                         TEST_P4RM_BEGIN_INDEX, 0U,
                         event, sizeof(event));
        CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
              P4_GAME_CONTINUE);
        CHECK(state.last_realm_event_id == event_id);
        CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
        if (cases[case_index].banked) {
            CHECK(state.player.bank == 50U);
        } else {
            CHECK(state.player.gold == 50U);
        }
        if (cases[case_index].kind == 3U) {
            CHECK(state.friendship_actions ==
                  (uint8_t)(friendship_before - 1U));
            CHECK(state.player.charm == (uint16_t)(charm_before + 1U));
        } else if (cases[case_index].kind == 7U) {
            CHECK(state.player.pvp_losses ==
                  (uint16_t)(losses_before + 1U));
            CHECK(state.player.hit_points == 0);
        }
        const uint32_t applied_sequence = state.save_sequence;
        const uint8_t applied_mail_count = state.mail_count;
        (void)realm_mock_take_kind(&realm);

        realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN,
                         UINT32_C(0x7300) + (uint32_t)case_index,
                         TEST_P4RM_BEGIN_INDEX, 0U,
                         event, sizeof(event));
        CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
              P4_GAME_CONTINUE);
        CHECK(state.last_realm_event_id == event_id);
        CHECK(state.save_sequence == applied_sequence);
        CHECK(state.mail_count == applied_mail_count);
        CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
        if (cases[case_index].banked) {
            CHECK(state.player.bank == 50U);
        } else {
            CHECK(state.player.gold == 50U);
        }
        (void)realm_mock_take_kind(&realm);
        p4_game_instance_stop(&instance);
    }

    /* A zero-value victim outcome is still a durable loss/KO event, not an
     * underfunded debit.  It must apply and de-duplicate with no ChompCoin. */
    realm_mock_t realm = {0};
    save_mock_t save = {0};
    lord_state_t seed;
    enter_town(&seed, LORD_CLASS_MYSTICAL);
    const uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES] = {
        0x60U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U,
    };
    const uint8_t source_id[LORD_SYNC_ACTOR_ID_BYTES] = {
        0x70U, 18U, 19U, 20U, 21U, 22U, 23U, 24U,
        25U, 26U, 27U, 28U, 29U, 30U, 31U, 32U,
    };
    memcpy(seed.sync_actor_id, actor_id, sizeof(actor_id));
    memcpy(seed.realm_actor_ids[0], source_id, sizeof(source_id));
    seed.sync_server_revision = 1U;
    seed.sync_committed_save_sequence = seed.save_sequence;
    seed.player.gold = 0U;
    save.bytes = lord_save_encode(&seed, save.payload, sizeof(save.payload));
    save.sequence = 1U;
    p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            P4_GAME_CAP_SAVE | P4_GAME_CAP_MULTIPLAYER_SESSION,
        .save_context = &save,
        .save_data = save.payload,
        .save_bytes = save.bytes,
        .save_schema_version = LORD_SAVE_FORMAT_VERSION,
        .save_sequence = save.sequence,
        .queue_save = mock_queue_save,
        .read_save_status = mock_read_save,
        .multiplayer_context = &realm,
        .multiplayer_read_status = realm_mock_status,
        .multiplayer_send = realm_mock_send,
        .multiplayer_receive = realm_mock_receive,
        .multiplayer_profile = &profile,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    CHECK(start_game(&instance, &state, &services));
    realm_mock_connect_bound(&realm, &instance, &state);
    uint8_t event[48] = {0};
    test_store_u64(event, 0U, UINT64_C(799));
    event[8] = 7U;
    event[9] = 1U;
    memcpy(event + 16U, source_id, sizeof(source_id));
    memcpy(event + 32U, "Realm Friend", 12U);
    const uint16_t losses_before = state.player.pvp_losses;
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x7400),
                     TEST_P4RM_BEGIN_INDEX, 0U, event, sizeof(event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == 0U);
    CHECK(state.player.pvp_losses == (uint16_t)(losses_before + 1U));
    CHECK(state.last_realm_event_id == UINT64_C(799));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    const uint32_t applied_sequence = state.save_sequence;
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x7401),
                     TEST_P4RM_BEGIN_INDEX, 0U, event, sizeof(event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == 0U);
    CHECK(state.player.pvp_losses == (uint16_t)(losses_before + 1U));
    CHECK(state.save_sequence == applied_sequence);
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    p4_game_instance_stop(&instance);
}

static uint32_t realm_mock_ack_current_upload(
    realm_mock_t *realm, p4_game_instance_t *instance)
{
    const p4_game_input_t idle = {0};
    CHECK(realm->outgoing_bytes >= TEST_P4RM_HEADER_BYTES);
    CHECK(realm->outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    if (realm->outgoing_bytes < TEST_P4RM_HEADER_BYTES ||
        realm->outgoing[5] != TEST_P4RM_UPLOAD_BEGIN) {
        return 0U;
    }
    const uint32_t transaction = test_load_u32(realm->outgoing, 8U);
    const uint16_t chunks = test_load_u16(realm->outgoing, 14U);
    CHECK(transaction != 0U && chunks > 0U);
    (void)realm_mock_take_kind(realm);
    const uint8_t begin_ack = TEST_P4RM_UPLOAD_BEGIN;
    realm_mock_queue(realm, TEST_P4RM_ACK, transaction,
                     TEST_P4RM_BEGIN_INDEX, chunks, &begin_ack, 1U);
    CHECK(p4_game_instance_update(instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    for (uint16_t chunk = 0U; chunk < chunks; ++chunk) {
        CHECK(realm->outgoing[5] == TEST_P4RM_UPLOAD_CHUNK);
        CHECK(test_load_u32(realm->outgoing, 8U) == transaction);
        CHECK(test_load_u16(realm->outgoing, 12U) == chunk);
        (void)realm_mock_take_kind(realm);
        const uint8_t chunk_ack = TEST_P4RM_UPLOAD_CHUNK;
        realm_mock_queue(realm, TEST_P4RM_ACK, transaction,
                         chunk, chunks, &chunk_ack, 1U);
        CHECK(p4_game_instance_update(instance, &idle, 16U) ==
              P4_GAME_CONTINUE);
    }
    CHECK(realm->outgoing_bytes == 0U);
    return transaction;
}

static void realm_mock_commit_current_upload(
    realm_mock_t *realm,
    p4_game_instance_t *instance,
    lord_state_t *state,
    uint64_t realm_day_id)
{
    const p4_game_input_t idle = {0};
    const uint32_t transaction =
        realm_mock_ack_current_upload(realm, instance);
    CHECK(transaction != 0U);
    uint8_t committed[17] = {0};
    test_store_u32(committed, 1U, state->sync_server_revision + 1U);
    test_store_u64(committed, 5U, realm_day_id);
    test_store_u32(committed, 13U, 1800U);
    realm_mock_queue(realm, TEST_P4RM_COMMIT_RESULT, transaction,
                     0U, 0U, committed, sizeof(committed));
    CHECK(p4_game_instance_update(instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state->sync_server_revision ==
          test_load_u32(committed, 1U));
    CHECK(state->sync_committed_save_sequence == state->save_sequence);
    CHECK(strcmp(lord_realm_net_label(), "MAC REALM") == 0);
    if (realm->outgoing_bytes != 0U) {
        CHECK(realm_mock_take_kind(realm) == TEST_P4RM_PROFILE);
        CHECK(p4_game_instance_update(instance, &idle, 16U) ==
              P4_GAME_CONTINUE);
        CHECK(realm_mock_take_kind(realm) == TEST_P4RM_PROFILE_STATS);
    }
    CHECK(realm->outgoing_bytes == 0U);
}

static void test_p4mp_mac_realm_hourly_sync(void)
{
    realm_mock_t realm = {0};
    static const p4_game_multiplayer_profile_t profile = {
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = 30U,
        .message_bytes = P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
        .protocol = UINT16_C(0x4c53),
    };
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_MULTIPLAYER_SESSION,
        .multiplayer_context = &realm,
        .multiplayer_read_status = realm_mock_status,
        .multiplayer_send = realm_mock_send,
        .multiplayer_receive = realm_mock_receive,
        .multiplayer_profile = &profile,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    CHECK(start_game(&instance, &state, &services));
    const p4_game_input_t idle = {0};
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing_bytes == TEST_P4RM_HEADER_BYTES + 32U);
    CHECK(realm.outgoing[4] == 3U);
    CHECK(test_load_u32(realm.outgoing, TEST_P4RM_HEADER_BYTES + 16U) == 0U);
    CHECK(test_load_u32(realm.outgoing, TEST_P4RM_HEADER_BYTES + 20U) == 0U);
    CHECK(test_load_u32(realm.outgoing, TEST_P4RM_HEADER_BYTES + 24U) == 0U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES + 28U] == 0U);
    CHECK(realm_mock_take_kind(&realm) == 1U);

    const uint32_t hello_transaction =
        test_load_u32(realm.outgoing, 8U);
    /* take_kind cleared only the length; the copied bytes remain available. */
    CHECK(hello_transaction != 0U);
    const uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES] = {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U,
    };
    uint8_t welcome[36] = {0};
    memcpy(welcome, actor_id, sizeof(actor_id));
    test_store_u32(welcome, 16U, 0U);
    test_store_u64(welcome, 20U, UINT64_C(100));
    test_store_u32(welcome, 28U, 1800U);
    realm_mock_queue(&realm, TEST_P4RM_WELCOME, hello_transaction,
                     0U, 0U, welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);

    enter_town(&state, LORD_CLASS_MYSTICAL);
    state.player.bank = 1000U;
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    const uint32_t upload_transaction = test_load_u32(realm.outgoing, 8U);
    const uint16_t upload_chunks = test_load_u16(realm.outgoing, 14U);
    CHECK(upload_chunks > 1U);
    (void)realm_mock_take_kind(&realm);

    const uint8_t begin_ack = TEST_P4RM_UPLOAD_BEGIN;
    realm_mock_queue(&realm, TEST_P4RM_ACK, upload_transaction,
                     TEST_P4RM_BEGIN_INDEX, upload_chunks,
                     &begin_ack, 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    for (uint16_t chunk = 0U; chunk < upload_chunks; ++chunk) {
        CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_CHUNK);
        CHECK(test_load_u16(realm.outgoing, 12U) == chunk);
        (void)realm_mock_take_kind(&realm);
        const uint8_t chunk_ack = TEST_P4RM_UPLOAD_CHUNK;
        realm_mock_queue(&realm, TEST_P4RM_ACK, upload_transaction,
                         chunk, upload_chunks, &chunk_ack, 1U);
        CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
              P4_GAME_CONTINUE);
    }
    CHECK(realm.outgoing_bytes == 0U);
    uint8_t committed[17] = {0};
    committed[0] = 0U;
    test_store_u32(committed, 1U, 1U);
    test_store_u64(committed, 5U, UINT64_C(100));
    test_store_u32(committed, 13U, 1700U);
    realm_mock_queue(&realm, TEST_P4RM_COMMIT_RESULT, upload_transaction,
                     0U, 0U, committed, sizeof(committed));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(memcmp(state.sync_actor_id, actor_id,
                 LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(state.sync_server_revision == 1U);
    CHECK(state.sync_committed_save_sequence == state.save_sequence);
    CHECK(realm_mock_take_kind(&realm) == TEST_P4RM_PROFILE);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_PROFILE_STATS);
    CHECK(test_load_u32(realm.outgoing, TEST_P4RM_HEADER_BYTES) ==
          state.player.gold);
    (void)realm_mock_take_kind(&realm);

    const uint16_t online_sleep_day = state.player.day;
    const uint32_t online_sleep_sequence = state.save_sequence;
    const p4_game_input_t online_activate = {
        .held = P4_BUTTON_A, .pressed = P4_BUTTON_A,
    };
    state.screen = LORD_SCREEN_INN;
    state.selection = 0U;
    CHECK(p4_game_instance_update(&instance, &online_activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(state.return_screen == LORD_SCREEN_INN);
    CHECK(strstr(state.message_line_1, "Mac realm") != NULL);
    CHECK(state.player.day == online_sleep_day);
    CHECK(state.save_sequence == online_sleep_sequence);

    uint8_t directory_summary[44] = {0};
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        directory_summary[index] = (uint8_t)(0x80U + index);
    }
    memcpy(directory_summary + 16U, "Other Hero", 10U);
    directory_summary[36] = (uint8_t)LORD_HERO_STYLE_HEROINE;
    directory_summary[37] = (uint8_t)LORD_CLASS_THIEF;
    directory_summary[38] = 4U;
    directory_summary[39] = 0x05U;
    test_store_u16(directory_summary, 40U, 7U);
    test_store_u16(directory_summary, 42U, 2U);
    uint8_t directory_stats[44] = {0};
    memcpy(directory_stats, directory_summary, LORD_SYNC_ACTOR_ID_BYTES);
    test_store_u32(directory_stats, 16U, 35U);
    test_store_u32(directory_stats, 20U, 40U);
    test_store_u32(directory_stats, 24U, 18U);
    test_store_u32(directory_stats, 28U, 6U);
    test_store_u32(directory_stats, 32U, 1500U);
    test_store_u32(directory_stats, 36U, 250U);
    directory_stats[40] = 55U;
    directory_stats[41] = 1U;
    uint8_t directory_deeds[18] = {0};
    memcpy(directory_deeds, directory_summary, LORD_SYNC_ACTOR_ID_BYTES);
    directory_deeds[16] = 3U;
    uint8_t directory_page[4] = {0};
    test_store_u16(directory_page, 0U, 0U);
    test_store_u16(directory_page, 2U, 17U);
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_PAGE,
                     UINT32_C(0x5000), 0U, 0U,
                     directory_page, sizeof(directory_page));
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_SUMMARY,
                     UINT32_C(0x5001), 0U, 1U,
                     directory_summary, sizeof(directory_summary));
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_STATS,
                     UINT32_C(0x5002), 0U, 1U,
                     directory_stats, sizeof(directory_stats));
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_DEEDS,
                     UINT32_C(0x5003), 0U, 1U,
                     directory_deeds, sizeof(directory_deeds));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(strcmp(state.realm[0].name, "Other Hero") == 0);
    CHECK(state.realm[0].level == 4U);
    CHECK(state.realm[0].hit_points == 35);
    CHECK(state.realm[0].pvp_wins == 7U);
    CHECK(state.realm[0].dragon_kills == 3U);
    CHECK(state.realm[0].trust == 55U);
    CHECK(state.realm[0].teamed);
    CHECK(strstr(state.realm[0].saying, "ONLINE") != NULL);
    CHECK(strcmp(state.realm[1].name, "Empty record") == 0);
    CHECK(state.realm[1].level == 1U);

    /* The additive deeds sidecar is identity-bound and reserves its final
     * byte. Malformed packets cannot borrow another profile's prestige. */
    directory_deeds[16] = 9U;
    directory_deeds[17] = 1U;
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_DEEDS,
                     UINT32_C(0x5004), 0U, 1U,
                     directory_deeds, sizeof(directory_deeds));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.realm[0].dragon_kills == 3U);
    directory_deeds[17] = 0U;
    directory_deeds[0] ^= UINT8_C(0x01);
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_DEEDS,
                     UINT32_C(0x5005), 0U, 1U,
                     directory_deeds, sizeof(directory_deeds));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.realm[0].dragon_kills == 3U);
    uint8_t directory_saved[LORD_SAVE_MAX_BYTES];
    const size_t directory_saved_bytes = lord_save_encode(
        &state, directory_saved, sizeof(directory_saved));
    lord_state_t directory_restored;
    CHECK(directory_saved_bytes > 0U);
    CHECK(lord_save_decode(&directory_restored,
                           directory_saved, directory_saved_bytes));

    const uint16_t old_day = state.player.day;
    const uint32_t old_bank = state.player.bank;
    uint8_t clock[16] = {0};
    test_store_u64(clock, 0U, UINT64_C(101));
    test_store_u32(clock, 8U, 3600U);
    clock[12] = 1U;
    realm_mock_queue(&realm, TEST_P4RM_CLOCK, UINT32_C(0x7777),
                     0U, 0U, clock, sizeof(clock));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.day == (uint16_t)(old_day + 1U));
    CHECK(state.player.bank == old_bank);
    CHECK(state.player.forest_fights == LORD_FOREST_FIGHTS_PER_DAY);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    const uint32_t day_101_transaction =
        test_load_u32(realm.outgoing, 8U);
    CHECK(test_load_u64(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 20U) == UINT64_C(101));
    const size_t day_101_begin_bytes = realm.outgoing_bytes;
    uint8_t day_101_begin[P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES];
    memcpy(day_101_begin, realm.outgoing, day_101_begin_bytes);
    (void)realm_mock_take_kind(&realm);

    /* CLOCK cannot tell whether a missing result was accepted. Preserve the
     * exact old-day record/nonce until OK or STALE_DAY disambiguates it. */
    test_store_u64(clock, 0U, UINT64_C(102));
    test_store_u32(clock, 8U, 3600U);
    realm_mock_queue(&realm, TEST_P4RM_CLOCK, UINT32_C(0x7778),
                     0U, 0U, clock, sizeof(clock));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.day == (uint16_t)(old_day + 1U));
    CHECK(state.player.bank == old_bank);
    CHECK(realm.outgoing_bytes == 0U);
    for (unsigned tick = 0U; tick < 10U; ++tick) {
        CHECK(p4_game_instance_update(&instance, &idle, 100U) ==
              P4_GAME_CONTINUE);
    }
    CHECK(realm.outgoing_bytes == day_101_begin_bytes);
    CHECK(memcmp(realm.outgoing, day_101_begin,
                 day_101_begin_bytes) == 0);
    CHECK(realm_mock_ack_current_upload(&realm, &instance) ==
          day_101_transaction);

    /* The explicit boundary status is retryable and preserves the first
     * rollover while regenerating the transaction for the current hour. */
    uint8_t stale_day[17] = {0};
    stale_day[0] = TEST_P4RM_COMMIT_STALE_DAY;
    test_store_u32(stale_day, 1U, 1U);
    test_store_u64(stale_day, 5U, UINT64_C(102));
    test_store_u32(stale_day, 13U, 3599U);
    realm_mock_queue(&realm, TEST_P4RM_COMMIT_RESULT,
                     day_101_transaction, 0U, 0U,
                     stale_day, sizeof(stale_day));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.day == (uint16_t)(old_day + 1U));
    CHECK(state.player.bank == old_bank);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    CHECK(test_load_u64(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 20U) == UINT64_C(102));

    /* If the commit itself succeeds just before the next boundary, its
     * result advances the head anchor and applies exactly one new refresh. */
    const uint32_t day_102_transaction =
        realm_mock_ack_current_upload(&realm, &instance);
    uint8_t crossed_commit[17] = {0};
    crossed_commit[0] = 0U;
    test_store_u32(crossed_commit, 1U, 2U);
    test_store_u64(crossed_commit, 5U, UINT64_C(103));
    test_store_u32(crossed_commit, 13U, 3598U);
    realm_mock_queue(&realm, TEST_P4RM_COMMIT_RESULT,
                     day_102_transaction, 0U, 0U,
                     crossed_commit, sizeof(crossed_commit));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.sync_server_revision == 2U);
    CHECK(state.player.day == (uint16_t)(old_day + 2U));
    CHECK(state.player.bank == old_bank);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    CHECK(test_load_u64(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 20U) == UINT64_C(103));
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    const p4_game_input_t activate = {
        .held = P4_BUTTON_A, .pressed = P4_BUTTON_A,
    };
    state.screen = LORD_SCREEN_PLAYERS;
    state.selection = LORD_REALM_PLAYER_COUNT + 1U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_DIRECTORY_PAGE);
    CHECK(test_load_u16(realm.outgoing, TEST_P4RM_HEADER_BYTES) == 8U);
    (void)realm_mock_take_kind(&realm);

    state.screen = LORD_SCREEN_BANK_TRANSFER;
    state.selection = 0U;
    const uint32_t bank_before = state.player.bank;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    const uint32_t action_transaction = test_load_u32(realm.outgoing, 8U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 2U);
    CHECK(test_load_u16(realm.outgoing, TEST_P4RM_HEADER_BYTES + 2U) == 100U);
    CHECK(memcmp(realm.outgoing + TEST_P4RM_HEADER_BYTES + 4U,
                 directory_summary, LORD_SYNC_ACTOR_ID_BYTES) == 0);
    (void)realm_mock_take_kind(&realm);
    const uint8_t action_ack = TEST_P4RM_ACTION_BEGIN;
    realm_mock_queue(&realm, TEST_P4RM_ACK, action_transaction,
                     TEST_P4RM_BEGIN_INDEX, 0U, &action_ack, 1U);
    uint8_t action_result[16] = {0};
    action_result[1] = 2U;
    test_store_u32(action_result, 4U, 100U);
    realm_mock_queue(&realm, TEST_P4RM_ACTION_RESULT, action_transaction,
                     0U, 0U, action_result, sizeof(action_result));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.bank == bank_before);
    CHECK(realm.outgoing_bytes == 0U);

    uint8_t debit_event[48] = {0};
    test_store_u64(debit_event, 0U, UINT64_C(40));
    debit_event[8] = 2U;
    debit_event[9] = 1U;
    test_store_u32(debit_event, 12U, 100U);
    memcpy(debit_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(debit_event + 32U, "Other Hero", 10U);

    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6500),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     debit_event, sizeof(debit_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.bank == bank_before - 100U);
    CHECK(state.last_realm_event_id == UINT64_C(40));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6501),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     debit_event, sizeof(debit_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.bank == bank_before - 100U);
    CHECK(state.last_realm_event_id == UINT64_C(40));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    const uint32_t gold_before_event = state.player.gold;
    uint8_t transfer_event[48] = {0};
    test_store_u64(transfer_event, 0U, UINT64_C(41));
    transfer_event[8] = 2U;
    test_store_u32(transfer_event, 12U, 100U);
    memcpy(transfer_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(transfer_event + 32U, "Other Hero", 10U);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6600),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     transfer_event, sizeof(transfer_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == gold_before_event + 100U);
    CHECK(state.last_realm_event_id == UINT64_C(41));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    state.screen = LORD_SCREEN_TEXT_EDITOR;
    state.editor_target = LORD_EDITOR_MAIL;
    state.editor_return_screen = LORD_SCREEN_MAILBOX;
    state.selected_player = 0U;
    state.selection = 42U;
    (void)strcpy(state.editor_text, "MEET AT THE INN");
    const uint8_t mail_count_before = state.mail_count;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    const uint32_t mail_transaction = test_load_u32(realm.outgoing, 8U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 1U);
    CHECK(test_load_u16(realm.outgoing, 14U) == 1U);
    (void)realm_mock_take_kind(&realm);
    const uint8_t mail_begin_ack = TEST_P4RM_ACTION_BEGIN;
    realm_mock_queue(&realm, TEST_P4RM_ACK, mail_transaction,
                     TEST_P4RM_BEGIN_INDEX, 1U, &mail_begin_ack, 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BODY);
    CHECK(memcmp(realm.outgoing + TEST_P4RM_HEADER_BYTES,
                 "MEET AT THE INN", 15U) == 0);
    (void)realm_mock_take_kind(&realm);
    const uint8_t mail_body_ack = TEST_P4RM_ACTION_BODY;
    realm_mock_queue(&realm, TEST_P4RM_ACK, mail_transaction,
                     0U, 1U, &mail_body_ack, 1U);
    uint8_t mail_result[16] = {0};
    mail_result[1] = 1U;
    realm_mock_queue(&realm, TEST_P4RM_ACTION_RESULT, mail_transaction,
                     0U, 0U, mail_result, sizeof(mail_result));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.mail_count == (uint8_t)(mail_count_before + 1U));
    CHECK(state.mail[state.mail_count - 1U].outgoing);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    static const uint8_t incoming_mail[] = "WELCOME FRIEND";
    uint8_t mail_event[48] = {0};
    test_store_u64(mail_event, 0U, UINT64_C(42));
    mail_event[8] = 1U;
    mail_event[10] = (uint8_t)(sizeof(incoming_mail) - 1U);
    memcpy(mail_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(mail_event + 32U, "Other Hero", 10U);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6700),
                     TEST_P4RM_BEGIN_INDEX, 1U,
                     mail_event, sizeof(mail_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACK);
    (void)realm_mock_take_kind(&realm);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BODY, UINT32_C(0x6700),
                     0U, 1U, incoming_mail, sizeof(incoming_mail) - 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.last_realm_event_id == UINT64_C(42));
    CHECK(strcmp(state.mail[state.mail_count - 1U].body,
                 "WELCOME FRIEND") == 0);
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    const uint8_t mail_count_after_event = state.mail_count;
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BODY, UINT32_C(0x6700),
                     0U, 1U, incoming_mail, sizeof(incoming_mail) - 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.last_realm_event_id == UINT64_C(42));
    CHECK(state.mail_count == mail_count_after_event);
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    const uint8_t friendship_before = state.friendship_actions;
    const uint32_t supplies_before = state.player.gold;
    const uint16_t charm_before = state.player.charm;
    const uint8_t trust_before = state.realm[0].trust;
    uint8_t friend_event[48] = {0};
    test_store_u64(friend_event, 0U, UINT64_C(43));
    friend_event[8] = 3U;
    friend_event[9] = UINT8_C(0x81);
    test_store_u32(friend_event, 12U, 30U);
    memcpy(friend_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(friend_event + 32U, "Other Hero", 10U);

    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6800),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     friend_event, sizeof(friend_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.friendship_actions == (uint8_t)(friendship_before - 1U));
    CHECK(state.player.gold == supplies_before - 100U);
    CHECK(state.player.charm == (uint16_t)(charm_before + 1U));
    CHECK(state.realm[0].trust == (uint8_t)(trust_before + 30U));
    CHECK(state.last_realm_event_id == UINT64_C(43));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6801),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     friend_event, sizeof(friend_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.friendship_actions == (uint8_t)(friendship_before - 1U));
    CHECK(state.player.gold == supplies_before - 100U);
    CHECK(state.player.charm == (uint16_t)(charm_before + 1U));
    CHECK(state.realm[0].trust == (uint8_t)(trust_before + 30U));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    state.screen = LORD_SCREEN_PLAYER_DETAIL;
    state.selection = 0U;
    state.selected_player = 0U;
    state.player.strength = 500;
    state.player.gold = 700U;
    state.realm[0].gold = 251U;
    state.realm[0].hit_points = 1;
    state.realm[0].max_hit_points = 1;
    state.realm[0].defense = 0;
    state.realm[0].alive = true;
    const uint32_t pvp_gold_before = state.player.gold;
    const uint32_t pvp_experience_before = state.player.experience;
    const uint16_t pvp_wins_before = state.player.pvp_wins;
    const uint16_t pvp_losses_before = state.player.pvp_losses;
    const uint8_t pvp_mail_before = state.mail_count;
    const uint8_t pvp_log_before = state.log_count;
    const lord_realm_player_t pvp_target_before = state.realm[0];
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    const uint32_t pvp_begin_transaction =
        test_load_u32(realm.outgoing, 8U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 6U);
    (void)realm_mock_take_kind(&realm);
    realm_mock_queue(&realm, TEST_P4RM_ACK, pvp_begin_transaction,
                     TEST_P4RM_BEGIN_INDEX, 0U, &action_ack, 1U);
    uint8_t pvp_begin_result[16] = {0};
    pvp_begin_result[1] = 6U;
    test_store_u64(pvp_begin_result, 8U, UINT64_C(7001));
    realm_mock_queue(&realm, TEST_P4RM_ACTION_RESULT,
                     pvp_begin_transaction, 0U, 0U,
                     pvp_begin_result, sizeof(pvp_begin_result));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_BATTLE);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == pvp_gold_before);
    CHECK(state.player.experience == pvp_experience_before);
    CHECK(state.player.pvp_wins == pvp_wins_before);
    CHECK(state.player.pvp_losses == pvp_losses_before);
    CHECK(state.mail_count == pvp_mail_before);
    CHECK(state.log_count == pvp_log_before);
    CHECK(memcmp(&state.realm[0], &pvp_target_before,
                 sizeof(pvp_target_before)) == 0);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    const uint32_t pvp_resolve_transaction =
        test_load_u32(realm.outgoing, 8U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 7U);
    (void)realm_mock_take_kind(&realm);
    realm_mock_queue(&realm, TEST_P4RM_ACK, pvp_resolve_transaction,
                     TEST_P4RM_BEGIN_INDEX, 1U, &action_ack, 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BODY);
    (void)realm_mock_take_kind(&realm);
    realm_mock_queue(&realm, TEST_P4RM_ACK, pvp_resolve_transaction,
                     0U, 1U, &mail_body_ack, 1U);
    uint8_t pvp_resolve_result[16] = {0};
    pvp_resolve_result[1] = 7U;
    pvp_resolve_result[2] = 1U;
    test_store_u64(pvp_resolve_result, 8U, UINT64_C(7001));
    realm_mock_queue(&realm, TEST_P4RM_ACTION_RESULT,
                     pvp_resolve_transaction, 0U, 0U,
                     pvp_resolve_result, sizeof(pvp_resolve_result));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == pvp_gold_before);

    uint8_t pvp_prize_event[48] = {0};
    test_store_u64(pvp_prize_event, 0U, UINT64_C(44));
    pvp_prize_event[8] = 7U;
    pvp_prize_event[9] = 2U;
    test_store_u32(pvp_prize_event, 12U, 37U);
    memcpy(pvp_prize_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(pvp_prize_event + 32U, "Other Hero", 10U);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6880),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     pvp_prize_event, sizeof(pvp_prize_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == pvp_gold_before + 37U);
    CHECK(state.player.pvp_wins == (uint16_t)(pvp_wins_before + 1U));
    CHECK(state.last_realm_event_id == UINT64_C(44));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    const uint8_t mail_after_prize = state.mail_count;
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6881),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     pvp_prize_event, sizeof(pvp_prize_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == pvp_gold_before + 37U);
    CHECK(state.player.pvp_wins == (uint16_t)(pvp_wins_before + 1U));
    CHECK(state.mail_count == mail_after_prize);
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    test_store_u64(pvp_prize_event, 0U, UINT64_C(45));
    test_store_u32(pvp_prize_event, 12U, 0U);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6882),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     pvp_prize_event, sizeof(pvp_prize_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == pvp_gold_before + 37U);
    CHECK(state.player.pvp_wins == (uint16_t)(pvp_wins_before + 2U));
    CHECK(state.last_realm_event_id == UINT64_C(45));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    uint8_t pvp_loss_event[48] = {0};
    test_store_u64(pvp_loss_event, 0U, UINT64_C(46));
    pvp_loss_event[8] = 7U;
    pvp_loss_event[9] = 1U;
    test_store_u32(pvp_loss_event, 12U, 100U);
    memcpy(pvp_loss_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(pvp_loss_event + 32U, "Other Hero", 10U);

    const uint32_t pvp_loss_gold_before = state.player.gold;
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6883),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     pvp_loss_event, sizeof(pvp_loss_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == pvp_loss_gold_before - 100U);
    CHECK(state.player.pvp_losses == (uint16_t)(pvp_losses_before + 1U));
    CHECK(state.player.hit_points == 0);
    CHECK(state.last_realm_event_id == UINT64_C(46));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    const uint8_t mail_after_loss = state.mail_count;
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6884),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     pvp_loss_event, sizeof(pvp_loss_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == pvp_loss_gold_before - 100U);
    CHECK(state.player.pvp_losses == (uint16_t)(pvp_losses_before + 1U));
    CHECK(state.mail_count == mail_after_loss);
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    const int32_t team_base_hp = state.player.max_hit_points;
    uint8_t team_event[48] = {0};
    test_store_u64(team_event, 0U, UINT64_C(47));
    team_event[8] = 4U;
    team_event[9] = 1U;
    memcpy(team_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(team_event + 32U, "Other Hero", 10U);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6900),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     team_event, sizeof(team_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.partner_index == 0);
    CHECK(state.player.max_hit_points == team_base_hp + 5);
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    test_store_u64(team_event, 0U, UINT64_C(48));
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6901),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     team_event, sizeof(team_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.partner_index == 0);
    CHECK(state.player.max_hit_points == team_base_hp + 5);
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    test_store_u64(team_event, 0U, UINT64_C(49));
    team_event[9] = 2U;
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6902),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     team_event, sizeof(team_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.partner_index == -1);
    CHECK(state.player.max_hit_points == team_base_hp);
    CHECK(state.player.hit_points <= state.player.max_hit_points);
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(103));

    /* A cable drop during a leased duel aborts on the next activation.  The
     * fight and received HP damage may be spent, but no cached target or
     * source reward/counter is minted without a durable resolve event. */
    state.screen = LORD_SCREEN_PLAYER_DETAIL;
    state.selection = 0U;
    state.selected_player = 0U;
    state.pvp_fights = 1U;
    state.player.hit_points = state.player.max_hit_points;
    state.player.gold = 900U;
    state.player.experience = 1234U;
    state.realm[0].alive = true;
    state.realm[0].hit_points = 1;
    state.realm[0].max_hit_points = 1;
    state.realm[0].defense = 0;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    const uint32_t dropped_pvp_begin = test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);
    realm_mock_queue(&realm, TEST_P4RM_ACK, dropped_pvp_begin,
                     TEST_P4RM_BEGIN_INDEX, 0U, &action_ack, 1U);
    memset(pvp_begin_result, 0, sizeof(pvp_begin_result));
    pvp_begin_result[1] = 6U;
    test_store_u64(pvp_begin_result, 8U, UINT64_C(7002));
    realm_mock_queue(&realm, TEST_P4RM_ACTION_RESULT,
                     dropped_pvp_begin, 0U, 0U,
                     pvp_begin_result, sizeof(pvp_begin_result));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_BATTLE);
    const uint32_t dropped_gold = state.player.gold;
    const uint32_t dropped_experience = state.player.experience;
    const uint16_t dropped_wins = state.player.pvp_wins;
    const uint16_t dropped_losses = state.player.pvp_losses;
    const lord_realm_player_t dropped_target = state.realm[0];
    realm.disconnected = true;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(strstr(state.message_line_1, "Connect to the Mac realm") != NULL);
    CHECK(state.battle_kind == LORD_BATTLE_NONE);
    CHECK(state.player.gold == dropped_gold);
    CHECK(state.player.experience == dropped_experience);
    CHECK(state.player.pvp_wins == dropped_wins);
    CHECK(state.player.pvp_losses == dropped_losses);
    CHECK(memcmp(&state.realm[0], &dropped_target,
                 sizeof(dropped_target)) == 0);

    /* Bound offline consoles may view cached realm data, but every shared
     * mutation is intercepted.  Local banking and solo forest play remain. */
    state.player.bank = 500U;
    state.player.gold = 500U;
    state.friendship_actions = 3U;
    state.pvp_fights = 2U;
    state.realm[0].gold = 222U;
    state.realm[0].trust = 10U;
    const uint32_t offline_bank = state.player.bank;
    const uint32_t offline_gold = state.player.gold;
    const uint32_t offline_target_gold = state.realm[0].gold;
    const uint8_t offline_trust = state.realm[0].trust;
    const uint8_t offline_friendship = state.friendship_actions;
    const uint8_t offline_pvp = state.pvp_fights;
    const uint8_t offline_mail = state.mail_count;
    state.screen = LORD_SCREEN_BANK_TRANSFER;
    state.selection = 0U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.bank == offline_bank);
    CHECK(state.realm[0].gold == offline_target_gold);
    CHECK(strstr(state.message_line_1, "Connect to the Mac realm") != NULL);
    for (uint8_t selection = 0U; selection < 4U; ++selection) {
        state.screen = selection == 0U ? LORD_SCREEN_PLAYER_DETAIL :
                                        LORD_SCREEN_FRIENDSHIP_ACTION;
        state.selection = selection;
        state.selected_player = 0U;
        CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
              P4_GAME_CONTINUE);
        CHECK(state.screen == LORD_SCREEN_MESSAGE);
    }
    state.screen = LORD_SCREEN_MAIL_COMPOSE;
    state.selection = 0U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    state.screen = LORD_SCREEN_TEXT_EDITOR;
    state.editor_target = LORD_EDITOR_MAIL;
    state.editor_return_screen = LORD_SCREEN_MAILBOX;
    state.selection = 42U;
    (void)strcpy(state.editor_text, "OFFLINE LETTER");
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    state.screen = LORD_SCREEN_INN;
    state.selection = 7U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    state.screen = LORD_SCREEN_INN;
    state.selection = 8U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    state.screen = LORD_SCREEN_CONVERSE;
    state.selection = 1U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(state.player.bank == offline_bank);
    CHECK(state.player.gold == offline_gold);
    CHECK(state.realm[0].gold == offline_target_gold);
    CHECK(state.realm[0].trust == offline_trust);
    CHECK(state.friendship_actions == offline_friendship);
    CHECK(state.pvp_fights == offline_pvp);
    CHECK(state.mail_count == offline_mail);

    state.screen = LORD_SCREEN_FOREST;
    state.selection = 0U;
    state.player.forest_fights = 2U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_BATTLE);
    CHECK(state.battle_kind == LORD_BATTLE_FOREST);
    CHECK(state.player.forest_fights == 1U);

    memset(state.sync_actor_id, 0, sizeof(state.sync_actor_id));
    state.screen = LORD_SCREEN_BANK_TRANSFER;
    state.selection = 0U;
    state.player.bank = 500U;
    state.realm[0].gold = 222U;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.bank == 400U);
    CHECK(state.realm[0].gold == 322U);

    p4_game_instance_stop(&instance);
}

static void test_p4rm_commit_result_retry(void)
{
    realm_mock_t realm = {0};
    static const p4_game_multiplayer_profile_t profile = {
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = 30U,
        .message_bytes = P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
        .protocol = UINT16_C(0x4c53),
    };
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_MULTIPLAYER_SESSION,
        .multiplayer_context = &realm,
        .multiplayer_read_status = realm_mock_status,
        .multiplayer_send = realm_mock_send,
        .multiplayer_receive = realm_mock_receive,
        .multiplayer_profile = &profile,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    const p4_game_input_t idle = {0};
    CHECK(start_game(&instance, &state, &services));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == 1U);
    const uint32_t hello_transaction = test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);

    const uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES] = {
        0x31U, 0x32U, 0x33U, 0x34U,
        0x35U, 0x36U, 0x37U, 0x38U,
        0x39U, 0x3aU, 0x3bU, 0x3cU,
        0x3dU, 0x3eU, 0x3fU, 0x40U,
    };
    uint8_t welcome[36] = {0};
    memcpy(welcome, actor_id, sizeof(actor_id));
    test_store_u64(welcome, 20U, UINT64_C(500));
    test_store_u32(welcome, 28U, 1800U);
    realm_mock_queue(&realm, TEST_P4RM_WELCOME, hello_transaction,
                     0U, 0U, welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing_bytes == 0U);

    enter_town(&state, LORD_CLASS_THIEF);
    state.player.gold = 777U;
    const uint32_t uploaded_save_sequence = state.save_sequence;
    const uint32_t uploaded_gold = state.player.gold;
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    const size_t original_begin_bytes = realm.outgoing_bytes;
    uint8_t original_begin[P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES];
    memcpy(original_begin, realm.outgoing, original_begin_bytes);
    const uint32_t upload_transaction = test_load_u32(realm.outgoing, 8U);
    const uint16_t upload_chunks = test_load_u16(realm.outgoing, 14U);
    const uint32_t upload_record_bytes = test_load_u32(
        realm.outgoing, TEST_P4RM_HEADER_BYTES + 4U);
    const uint64_t operation_nonce = test_load_u64(
        realm.outgoing, TEST_P4RM_HEADER_BYTES + 12U);
    CHECK(upload_chunks > 1U);
    CHECK(upload_record_bytes > 0U &&
          upload_record_bytes <= LORD_SYNC_MAX_BYTES);
    CHECK(operation_nonce != 0U);
    (void)realm_mock_take_kind(&realm);

    const uint8_t begin_ack = TEST_P4RM_UPLOAD_BEGIN;
    realm_mock_queue(&realm, TEST_P4RM_ACK, upload_transaction,
                     TEST_P4RM_BEGIN_INDEX, upload_chunks,
                     &begin_ack, 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    uint8_t original_record[LORD_SYNC_MAX_BYTES] = {0};
    size_t original_record_bytes = 0U;
    for (uint16_t chunk = 0U; chunk < upload_chunks; ++chunk) {
        CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_CHUNK);
        CHECK(test_load_u32(realm.outgoing, 8U) == upload_transaction);
        CHECK(test_load_u16(realm.outgoing, 12U) == chunk);
        const size_t chunk_bytes = realm.outgoing_bytes >=
                TEST_P4RM_HEADER_BYTES
            ? realm.outgoing_bytes - TEST_P4RM_HEADER_BYTES
            : 0U;
        const size_t offset =
            (size_t)chunk * (P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES -
                              TEST_P4RM_HEADER_BYTES);
        CHECK(offset <= upload_record_bytes &&
              chunk_bytes <= upload_record_bytes - offset);
        if (offset <= sizeof(original_record) &&
            chunk_bytes <= sizeof(original_record) - offset) {
            memcpy(original_record + offset,
                   realm.outgoing + TEST_P4RM_HEADER_BYTES,
                   chunk_bytes);
            original_record_bytes = offset + chunk_bytes;
        }
        (void)realm_mock_take_kind(&realm);
        const uint8_t chunk_ack = TEST_P4RM_UPLOAD_CHUNK;
        realm_mock_queue(&realm, TEST_P4RM_ACK, upload_transaction,
                         chunk, upload_chunks, &chunk_ack, 1U);
        CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
              P4_GAME_CONTINUE);
    }
    CHECK(original_record_bytes == upload_record_bytes);
    lord_state_t decoded;
    lord_sync_metadata_t metadata;
    CHECK(lord_sync_decode(&decoded, &metadata, actor_id, 0U,
                           original_record, original_record_bytes));
    CHECK(metadata.operation_nonce == operation_nonce);
    CHECK(metadata.save_sequence == uploaded_save_sequence);
    CHECK(decoded.player.gold == uploaded_gold);

    /* Drop the first COMMIT_RESULT. The retained upload must restart whole. */
    for (unsigned tick = 0U; tick < 10U; ++tick) {
        CHECK(p4_game_instance_update(&instance, &idle, 100U) ==
              P4_GAME_CONTINUE);
    }
    CHECK(realm.outgoing_bytes == original_begin_bytes);
    CHECK(memcmp(realm.outgoing, original_begin,
                 original_begin_bytes) == 0);
    CHECK(test_load_u64(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 12U) == operation_nonce);
    (void)realm_mock_take_kind(&realm);

    realm_mock_queue(&realm, TEST_P4RM_ACK, upload_transaction,
                     TEST_P4RM_BEGIN_INDEX, upload_chunks,
                     &begin_ack, 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    for (uint16_t chunk = 0U; chunk < upload_chunks; ++chunk) {
        CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_CHUNK);
        CHECK(test_load_u32(realm.outgoing, 8U) == upload_transaction);
        CHECK(test_load_u16(realm.outgoing, 12U) == chunk);
        const size_t chunk_bytes = realm.outgoing_bytes >=
                TEST_P4RM_HEADER_BYTES
            ? realm.outgoing_bytes - TEST_P4RM_HEADER_BYTES
            : 0U;
        const size_t offset =
            (size_t)chunk * (P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES -
                              TEST_P4RM_HEADER_BYTES);
        CHECK(offset <= original_record_bytes &&
              chunk_bytes <= original_record_bytes - offset);
        if (offset <= original_record_bytes &&
            chunk_bytes <= original_record_bytes - offset) {
            CHECK(memcmp(realm.outgoing + TEST_P4RM_HEADER_BYTES,
                         original_record + offset, chunk_bytes) == 0);
        }
        (void)realm_mock_take_kind(&realm);
        const uint8_t chunk_ack = TEST_P4RM_UPLOAD_CHUNK;
        realm_mock_queue(&realm, TEST_P4RM_ACK, upload_transaction,
                         chunk, upload_chunks, &chunk_ack, 1U);
        CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
              P4_GAME_CONTINUE);
    }

    uint8_t committed[17] = {0};
    test_store_u32(committed, 1U, 1U);
    test_store_u64(committed, 5U, UINT64_C(500));
    test_store_u32(committed, 13U, 1700U);
    realm_mock_queue(&realm, TEST_P4RM_COMMIT_RESULT, upload_transaction,
                     0U, 0U, committed, sizeof(committed));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.sync_server_revision == 1U);
    CHECK(state.sync_committed_save_sequence == uploaded_save_sequence);
    CHECK(state.save_sequence == uploaded_save_sequence);
    CHECK(state.player.gold == uploaded_gold);
    CHECK(realm_mock_take_kind(&realm) == TEST_P4RM_PROFILE);

    /* A late duplicate result is inert and cannot start another upload. */
    realm_mock_queue(&realm, TEST_P4RM_COMMIT_RESULT, upload_transaction,
                     0U, 0U, committed, sizeof(committed));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.sync_server_revision == 1U);
    CHECK(state.sync_committed_save_sequence == uploaded_save_sequence);
    CHECK(realm.outgoing[5] == TEST_P4RM_PROFILE_STATS);
    CHECK(realm.outgoing[5] != TEST_P4RM_UPLOAD_BEGIN);
    CHECK(realm.outgoing[5] != TEST_P4RM_UPLOAD_CHUNK);
    (void)realm_mock_take_kind(&realm);
    p4_game_instance_stop(&instance);
}

static void capture_frame_if_requested(const p4_game_surface_t *surface,
                                       lord_screen_t screen)
{
    const char *const directory = getenv("LORD_CAPTURE_DIR");
    if (directory == NULL || directory[0] == '\0') {
        return;
    }
    char path[512];
    const int path_length = snprintf(path, sizeof(path),
                                     "%s/screen-%02d.ppm", directory,
                                     (int)screen);
    CHECK(path_length > 0 && (size_t)path_length < sizeof(path));
    if (path_length <= 0 || (size_t)path_length >= sizeof(path)) {
        return;
    }
    FILE *const file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    (void)fprintf(file, "P6\n%u %u\n255\n", surface->width,
                  surface->height);
    for (uint16_t y = 0U; y < surface->height; ++y) {
        for (uint16_t x = 0U; x < surface->width; ++x) {
            const uint16_t pixel = surface->pixels[
                (size_t)y * surface->stride_pixels + x];
            const uint8_t rgb[3] = {
                (uint8_t)((((uint32_t)(pixel >> 11U) & 31U) * 255U + 15U) /
                          31U),
                (uint8_t)((((uint32_t)(pixel >> 5U) & 63U) * 255U + 31U) /
                          63U),
                (uint8_t)(((uint32_t)(pixel & 31U) * 255U + 15U) / 31U),
            };
            CHECK(fwrite(rgb, sizeof(rgb), 1U, file) == 1U);
        }
    }
    CHECK(fclose(file) == 0);
}

static void capture_monster_frame_if_requested(
    const p4_game_surface_t *surface, size_t monster_index,
    unsigned animation_frame)
{
    const char *const directory = getenv("LORD_MONSTER_CAPTURE_DIR");
    if (directory == NULL || directory[0] == '\0') {
        return;
    }
    char path[512];
    const int path_length = snprintf(path, sizeof(path),
                                     "%s/monster-%03zu-f%u.ppm", directory,
                                     monster_index, animation_frame);
    CHECK(path_length > 0 && (size_t)path_length < sizeof(path));
    if (path_length <= 0 || (size_t)path_length >= sizeof(path)) {
        return;
    }
    FILE *const file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    (void)fprintf(file, "P6\n%u %u\n255\n", surface->width,
                  surface->height);
    for (uint16_t y = 0U; y < surface->height; ++y) {
        for (uint16_t x = 0U; x < surface->width; ++x) {
            const uint16_t pixel = surface->pixels[
                (size_t)y * surface->stride_pixels + x];
            const uint8_t rgb[3] = {
                (uint8_t)((((uint32_t)(pixel >> 11U) & 31U) * 255U + 15U) /
                          31U),
                (uint8_t)((((uint32_t)(pixel >> 5U) & 63U) * 255U + 31U) /
                          63U),
                (uint8_t)(((uint32_t)(pixel & 31U) * 255U + 15U) / 31U),
            };
            CHECK(fwrite(rgb, sizeof(rgb), 1U, file) == 1U);
        }
    }
    CHECK(fclose(file) == 0);
}

typedef struct {
    size_t monster_index;
    const char *monster_name;
    const char *profile_name;
    uint16_t expected_color;
    uint16_t unoverridden_color;
} lord_named_palette_expectation_t;

/* These are the source table's deliberately named semantic colors. Keep the
 * exact row name beside its index so an upstream table reorder fails loudly
 * instead of silently testing a different encounter. */
static const lord_named_palette_expectation_t
    s_named_palette_expectations[] = {
    {3U, "Large Green Rat", "green", UINT16_C(0x57ea),
     UINT16_C(0xa540)},
    {11U, "Green Python", "green", UINT16_C(0x57ea),
     UINT16_C(0xa540)},
    {24U, "Purple Monchichi", "purple", UINT16_C(0xfabf),
     UINT16_C(0xa540)},
    {28U, "Black Owl", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0x57ff)},
    {35U, "Dark Elf", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0x57ff)},
    {38U, "Huge Black Bear", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0xa540)},
    {52U, "Black Alligator", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0x57ea)},
    {54U, "Black Sorcerer", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0xfabf)},
    {56U, "Black Soul", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0xc618)},
    {57U, "Gold Man", "gold-yellow", UINT16_C(0xffe0),
     UINT16_C(0xa540)},
    {82U, "Fire Ork", "fire-red", UINT16_C(0xfaaa),
     UINT16_C(0x57ea)},
    {88U, "Pink Elephant", "pink-magenta", UINT16_C(0xfabf),
     UINT16_C(0xa540)},
    {95U, "Hemo-Glob", "hemo-red", UINT16_C(0xfaaa),
     UINT16_C(0x0540)},
    {107U, "Adult Gold Dragon", "gold-yellow", UINT16_C(0xffe0),
     UINT16_C(0xfaaa)},
    {108U, "Black Sorcerer", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0xfabf)},
    {114U, "White Bear Of Lore", "white", UINT16_C(0xffff),
     UINT16_C(0xa540)},
    {117U, "ShadowStormWarrior", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0xc618)},
    {123U, "Black Warlock", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0xfabf)},
    {125U, "The Mighty Shadow", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0xc618)},
    {126U, "Black Unicorn", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0xa540)},
    {127U, "Mutated Black Widow", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0x7bef)},
    {128U, "Humongous Black Wyre", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0xfaaa)},
    {129U, "The Wizard Of Darkness", "dark-gray", UINT16_C(0x7bef),
     UINT16_C(0xfabf)},
};

static const lord_named_palette_expectation_t *
named_palette_expectation(size_t monster_index)
{
    for (size_t index = 0U;
         index < sizeof(s_named_palette_expectations) /
             sizeof(s_named_palette_expectations[0]); ++index) {
        if (s_named_palette_expectations[index].monster_index ==
            monster_index) {
            return &s_named_palette_expectations[index];
        }
    }
    return NULL;
}

static FILE *open_monster_manifest_if_requested(void)
{
    const char *const directory = getenv("LORD_MONSTER_CAPTURE_DIR");
    if (directory == NULL || directory[0] == '\0') {
        return NULL;
    }
    char path[512];
    const int path_length = snprintf(path, sizeof(path),
                                     "%s/monster-manifest.tsv", directory);
    CHECK(path_length > 0 && (size_t)path_length < sizeof(path));
    if (path_length <= 0 || (size_t)path_length >= sizeof(path)) {
        return NULL;
    }
    FILE *const file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL) {
        return NULL;
    }
    CHECK(fprintf(file,
                  "capture_index\tkind\texact_name\tweapon_or_detail\t"
                  "palette_profile\tframe_0\tframe_1\n") > 0);
    return file;
}

static void append_monster_manifest_row(
    FILE *file, size_t capture_index, const char *kind,
    const char *exact_name, const char *weapon_or_detail,
    const char *palette_profile)
{
    if (file == NULL) {
        return;
    }
    CHECK(fprintf(file, "%zu\t%s\t%s\t%s\t%s\t"
                  "monster-%03zu-f0.ppm\tmonster-%03zu-f1.ppm\n",
                  capture_index, kind, exact_name, weapon_or_detail,
                  palette_profile, capture_index, capture_index) > 0);
}

static uint32_t surface_region_hash(const p4_game_surface_t *surface,
                                    size_t x, size_t y,
                                    size_t width, size_t height)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t row = y; row < y + height; ++row) {
        for (size_t column = x; column < x + width; ++column) {
            hash ^= surface->pixels[row * surface->stride_pixels + column];
            hash *= UINT32_C(16777619);
        }
    }
    return hash;
}

static size_t surface_region_non_color_count(
    const p4_game_surface_t *surface,
    size_t x, size_t y, size_t width, size_t height,
    uint16_t color)
{
    size_t count = 0U;
    for (size_t row = y; row < y + height; ++row) {
        for (size_t column = x; column < x + width; ++column) {
            if (surface->pixels[
                    row * surface->stride_pixels + column] != color) {
                ++count;
            }
        }
    }
    return count;
}

static size_t surface_region_color_count(
    const p4_game_surface_t *surface,
    size_t x, size_t y, size_t width, size_t height,
    uint16_t color)
{
    size_t count = 0U;
    for (size_t row = y; row < y + height; ++row) {
        for (size_t column = x; column < x + width; ++column) {
            if (surface->pixels[
                    row * surface->stride_pixels + column] == color) {
                ++count;
            }
        }
    }
    return count;
}

static size_t surface_region_dense_color_cell_count(
    const p4_game_surface_t *surface,
    size_t x, size_t y, size_t width, size_t height,
    uint16_t color)
{
    enum {
        LORD_TEST_ART_CELL_WIDTH = 8,
        LORD_TEST_ART_CELL_HEIGHT = 16,
        LORD_TEST_DENSE_GLYPH_PIXELS = 40,
    };
    CHECK(width % LORD_TEST_ART_CELL_WIDTH == 0U);
    CHECK(height % LORD_TEST_ART_CELL_HEIGHT == 0U);
    size_t dense_cells = 0U;
    for (size_t cell_y = 0U; cell_y < height;
         cell_y += LORD_TEST_ART_CELL_HEIGHT) {
        for (size_t cell_x = 0U; cell_x < width;
             cell_x += LORD_TEST_ART_CELL_WIDTH) {
            const size_t pixels = surface_region_color_count(
                surface, x + cell_x, y + cell_y,
                LORD_TEST_ART_CELL_WIDTH, LORD_TEST_ART_CELL_HEIGHT,
                color);
            if (pixels >= LORD_TEST_DENSE_GLYPH_PIXELS) {
                ++dense_cells;
            }
        }
    }
    return dense_cells;
}

static void check_named_monster_palette(
    const p4_game_surface_t *surface, size_t monster_index,
    const char *monster_name)
{
    const lord_named_palette_expectation_t *const expectation =
        named_palette_expectation(monster_index);
    if (expectation == NULL) {
        return;
    }
    CHECK(strcmp(monster_name, expectation->monster_name) == 0);
    const size_t expected_count = surface_region_color_count(
        surface, 16U, 32U, 80U, 80U, expectation->expected_color);
    CHECK(expected_count >= 64U);
    CHECK(surface_region_dense_color_cell_count(
        surface, 16U, 32U, 80U, 80U,
        expectation->expected_color) > 0U);
    if (expectation->unoverridden_color != expectation->expected_color) {
        CHECK(surface_region_dense_color_cell_count(
            surface, 16U, 32U, 80U, 80U,
            expectation->unoverridden_color) == 0U);
    }
}

static void test_offline_save_reconnect_reconciliation(void)
{
    lord_state_t offline;
    enter_town(&offline, LORD_CLASS_THIEF);
    offline.player.bank = 4321U;
    offline.save_sequence = 42U;
    offline.sync_server_revision = 9U;
    offline.sync_committed_save_sequence = 40U;
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        offline.sync_actor_id[index] = (uint8_t)(index + 1U);
    }
    uint8_t saved[LORD_SAVE_MAX_BYTES];
    const size_t saved_bytes = lord_save_encode(
        &offline, saved, sizeof(saved));
    CHECK(saved_bytes > 0U);

    static const p4_game_multiplayer_profile_t profile = {
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = 30U,
        .message_bytes = P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
        .protocol = UINT16_C(0x4c53),
    };
    save_mock_t save = {0};
    realm_mock_t realm = {0};
    p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_SAVE |
            P4_GAME_CAP_MULTIPLAYER_SESSION,
        .save_context = &save,
        .save_data = saved,
        .save_bytes = saved_bytes,
        .save_schema_version = LORD_SAVE_FORMAT_VERSION,
        .save_sequence = 42U,
        .queue_save = mock_queue_save,
        .read_save_status = mock_read_save,
        .multiplayer_context = &realm,
        .multiplayer_read_status = realm_mock_status,
        .multiplayer_send = realm_mock_send,
        .multiplayer_receive = realm_mock_receive,
        .multiplayer_profile = &profile,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    const p4_game_input_t idle = {0};
    CHECK(start_game(&instance, &state, &services));
    CHECK(state.player.bank == 4321U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing_bytes == TEST_P4RM_HEADER_BYTES + 32U);
    CHECK(memcmp(realm.outgoing + TEST_P4RM_HEADER_BYTES,
                 offline.sync_actor_id, LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(test_load_u32(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 16U) == 9U);
    CHECK(test_load_u32(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 20U) == 40U);
    CHECK(test_load_u32(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 24U) == 42U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES + 28U] ==
          (TEST_P4RM_HELLO_HAS_LOCAL |
           TEST_P4RM_HELLO_HAS_SYNC_BASE |
           TEST_P4RM_HELLO_LOCAL_DIRTY));
    const uint32_t transaction = test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);

    uint8_t welcome[36] = {0};
    memcpy(welcome, offline.sync_actor_id, LORD_SYNC_ACTOR_ID_BYTES);
    test_store_u32(welcome, 16U, 9U);
    test_store_u64(welcome, 20U, UINT64_C(100));
    test_store_u32(welcome, 28U, 1800U);
    welcome[32] = TEST_P4RM_WELCOME_ACCEPT_LOCAL | (1U << 1U);
    const uint16_t day_before_sync = state.player.day;
    test_store_u16(welcome, 33U, day_before_sync);
    realm_mock_queue(&realm, TEST_P4RM_WELCOME, transaction,
                     0U, 0U, welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    CHECK(test_load_u32(realm.outgoing, TEST_P4RM_HEADER_BYTES) == 9U);
    CHECK(state.player.bank == 4321U);
    CHECK(state.player.day == day_before_sync + 1U);
    const uint16_t rolled_day = state.player.day;
    uint8_t rolled_saved[LORD_SAVE_MAX_BYTES];
    const size_t rolled_saved_bytes = lord_save_encode(
        &state, rolled_saved, sizeof(rolled_saved));
    CHECK(rolled_saved_bytes > 0U);
    p4_game_instance_stop(&instance);

    /* Simulate power loss after the local rollover save but before the realm
     * accepts it.  The same WELCOME anchor must not advance the day twice. */
    save = (save_mock_t){0};
    realm = (realm_mock_t){0};
    services.save_context = &save;
    services.save_data = rolled_saved;
    services.save_bytes = rolled_saved_bytes;
    services.save_sequence = 43U;
    services.multiplayer_context = &realm;
    CHECK(start_game(&instance, &state, &services));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    const uint32_t resumed_transaction =
        test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);
    welcome[32] = TEST_P4RM_WELCOME_ACCEPT_LOCAL | (1U << 1U);
    test_store_u32(welcome, 16U, 9U);
    realm_mock_queue(&realm, TEST_P4RM_WELCOME, resumed_transaction,
                     0U, 0U, welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.day == rolled_day);
    CHECK(state.player.bank == 4321U);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    CHECK(test_load_u64(realm.outgoing,
                        TEST_P4RM_HEADER_BYTES + 20U) == UINT64_C(100));
    p4_game_instance_stop(&instance);

    save = (save_mock_t){0};
    realm = (realm_mock_t){0};
    services.save_context = &save;
    services.save_data = saved;
    services.save_bytes = saved_bytes;
    services.save_sequence = 42U;
    services.multiplayer_context = &realm;
    CHECK(start_game(&instance, &state, &services));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    const uint32_t conflict_transaction =
        test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);
    welcome[32] = TEST_P4RM_WELCOME_LOCAL_CONFLICT;
    test_store_u32(welcome, 16U, 0U);
    realm_mock_queue(&realm, TEST_P4RM_WELCOME, conflict_transaction,
                     0U, 0U, welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing_bytes == 0U);
    CHECK(strcmp(lord_realm_net_label(), "SYNC CONFLICT") == 0);
    CHECK(state.player.bank == 4321U);
    CHECK(state.sync_server_revision == 9U);
    p4_game_instance_stop(&instance);
}

static void test_authorized_local_adoption(void)
{
    lord_state_t local;
    enter_town(&local, LORD_CLASS_THIEF);
    local.player.bank = 2468U;
    local.screen = LORD_SCREEN_BANK;
    local.selection = 0U;
    local.save_sequence = 42U;
    local.sync_server_revision = 0U;
    local.sync_committed_save_sequence = 0U;
    memset(local.sync_actor_id, 0, sizeof(local.sync_actor_id));
    uint8_t saved[LORD_SAVE_MAX_BYTES];
    const size_t saved_bytes = lord_save_encode(
        &local, saved, sizeof(saved));
    CHECK(saved_bytes > 0U);

    static const p4_game_multiplayer_profile_t profile = {
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = 30U,
        .message_bytes = P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
        .protocol = UINT16_C(0x4c53),
    };
    save_mock_t save = {0};
    realm_mock_t realm = {0};
    p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_SAVE |
            P4_GAME_CAP_MULTIPLAYER_SESSION,
        .save_context = &save,
        .save_data = saved,
        .save_bytes = saved_bytes,
        .save_schema_version = LORD_SAVE_FORMAT_VERSION,
        .save_sequence = 42U,
        .queue_save = mock_queue_save,
        .read_save_status = mock_read_save,
        .multiplayer_context = &realm,
        .multiplayer_read_status = realm_mock_status,
        .multiplayer_send = realm_mock_send,
        .multiplayer_receive = realm_mock_receive,
        .multiplayer_profile = &profile,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    const p4_game_input_t idle = {0};
    CHECK(start_game(&instance, &state, &services));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == 1U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES + 28U] ==
          (TEST_P4RM_HELLO_HAS_LOCAL | TEST_P4RM_HELLO_LOCAL_DIRTY));
    const uint32_t transaction = test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);

    const uint8_t adopted_actor[LORD_SYNC_ACTOR_ID_BYTES] = {
        0xa1U, 0xa2U, 0xa3U, 0xa4U,
        0xa5U, 0xa6U, 0xa7U, 0xa8U,
        0xa9U, 0xaaU, 0xabU, 0xacU,
        0xadU, 0xaeU, 0xafU, 0xb0U,
    };
    uint8_t welcome[36] = {0};
    memcpy(welcome, adopted_actor, sizeof(adopted_actor));
    test_store_u32(welcome, 16U, 0U);
    test_store_u64(welcome, 20U, UINT64_C(300));
    test_store_u32(welcome, 28U, 1200U);
    welcome[32] = TEST_P4RM_WELCOME_ADOPT_LOCAL;
    state.screen = LORD_SCREEN_BANK;
    state.selection = 0U;
    realm_mock_queue(&realm, TEST_P4RM_WELCOME, transaction,
                     0U, 0U, welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    CHECK(test_load_u32(realm.outgoing, TEST_P4RM_HEADER_BYTES) == 0U);
    CHECK(strcmp(lord_realm_net_label(), "SYNCING") == 0);
    CHECK(memcmp(state.sync_actor_id, adopted_actor,
                 LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(state.sync_server_revision == 0U);
    CHECK(state.sync_committed_save_sequence == 0U);
    CHECK(save.bytes > 0U);
    lord_state_t adopted_save;
    CHECK(lord_save_decode(&adopted_save, save.payload, save.bytes));
    CHECK(memcmp(adopted_save.sync_actor_id, adopted_actor,
                 LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(adopted_save.sync_server_revision == 0U);
    CHECK(adopted_save.sync_committed_save_sequence == 0U);
    uint8_t adopted_payload[LORD_SAVE_MAX_BYTES];
    const size_t adopted_bytes = save.bytes;
    memcpy(adopted_payload, save.payload, adopted_bytes);

    /* The encoded upload is immutable: local banking input and a raced
     * durable event cannot mutate it.  The blocked update still services the
     * adopted-identity save above. */
    const p4_game_input_t activate = {
        .held = P4_BUTTON_A, .pressed = P4_BUTTON_A,
    };
    const uint32_t frozen_gold = state.player.gold;
    const uint32_t frozen_bank = state.player.bank;
    const uint32_t frozen_sequence = state.save_sequence;
    const uint64_t frozen_cursor = state.last_realm_event_id;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == frozen_gold);
    CHECK(state.player.bank == frozen_bank);
    CHECK(state.save_sequence == frozen_sequence);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    CHECK(strcmp(lord_realm_net_label(), "SYNCING") == 0);

    const uint8_t event_source[LORD_SYNC_ACTOR_ID_BYTES] = {
        0xc1U, 0xc2U, 0xc3U, 0xc4U,
        0xc5U, 0xc6U, 0xc7U, 0xc8U,
        0xc9U, 0xcaU, 0xcbU, 0xccU,
        0xcdU, 0xceU, 0xcfU, 0xd0U,
    };
    uint8_t transfer_event[48] = {0};
    test_store_u64(transfer_event, 0U, UINT64_C(900));
    transfer_event[8] = 2U;
    test_store_u32(transfer_event, 12U, 100U);
    memcpy(transfer_event + 16U, event_source, sizeof(event_source));
    memcpy(transfer_event + 32U, "Realm Friend", 12U);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN,
                     UINT32_C(0x7500), TEST_P4RM_BEGIN_INDEX, 0U,
                     transfer_event, sizeof(transfer_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == frozen_gold);
    CHECK(state.last_realm_event_id == frozen_cursor);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);

    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(300));
    CHECK(state.player.gold == frozen_gold);
    CHECK(state.last_realm_event_id == frozen_cursor);

    /* The unacknowledged event is retried after COMMIT_OK and applies once.
     * Its resulting cursor/economy state is committed before input resumes. */
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN,
                     UINT32_C(0x7501), TEST_P4RM_BEGIN_INDEX, 0U,
                     transfer_event, sizeof(transfer_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == frozen_gold + 100U);
    CHECK(state.last_realm_event_id == UINT64_C(900));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    realm_mock_commit_current_upload(
        &realm, &instance, &state, UINT64_C(300));

    const uint32_t spendable_gold = state.player.gold;
    const uint32_t bank_before_deposit = state.player.bank;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == 0U);
    CHECK(state.player.bank == bank_before_deposit + spendable_gold);
    p4_game_instance_stop(&instance);

    save = (save_mock_t){0};
    realm = (realm_mock_t){0};
    services.save_context = &save;
    services.multiplayer_context = &realm;
    CHECK(start_game(&instance, &state, &services));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    const uint32_t invalid_transaction =
        test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);
    welcome[32] = TEST_P4RM_WELCOME_ADOPT_LOCAL |
        TEST_P4RM_WELCOME_ACCEPT_LOCAL;
    realm_mock_queue(&realm, TEST_P4RM_WELCOME, invalid_transaction,
                     0U, 0U, welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing_bytes == 0U);
    CHECK(strcmp(lord_realm_net_label(), "SYNC ERROR") == 0);
    CHECK(state.sync_server_revision == 0U);
    CHECK(state.sync_committed_save_sequence == 0U);
    p4_game_instance_stop(&instance);

    save = (save_mock_t){0};
    realm = (realm_mock_t){0};
    services.save_context = &save;
    services.save_data = adopted_payload;
    services.save_bytes = adopted_bytes;
    services.multiplayer_context = &realm;
    CHECK(start_game(&instance, &state, &services));
    CHECK(memcmp(state.sync_actor_id, adopted_actor,
                 LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(state.sync_server_revision == 0U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    const uint32_t zero_accept_transaction =
        test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);
    welcome[32] = TEST_P4RM_WELCOME_ACCEPT_LOCAL;
    realm_mock_queue(&realm, TEST_P4RM_WELCOME,
                     zero_accept_transaction, 0U, 0U,
                     welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing_bytes == 0U);
    CHECK(strcmp(lord_realm_net_label(), "SYNC ERROR") == 0);
    p4_game_instance_stop(&instance);

    save = (save_mock_t){0};
    realm = (realm_mock_t){0};
    services.save_context = &save;
    services.save_data = saved;
    services.save_bytes = saved_bytes;
    services.multiplayer_context = &realm;
    CHECK(start_game(&instance, &state, &services));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    const uint32_t zero_snapshot_transaction =
        test_load_u32(realm.outgoing, 8U);
    (void)realm_mock_take_kind(&realm);
    welcome[32] = TEST_P4RM_WELCOME_HAS_SNAPSHOT;
    realm_mock_queue(&realm, TEST_P4RM_WELCOME,
                     zero_snapshot_transaction, 0U, 0U,
                     welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing_bytes == 0U);
    CHECK(strcmp(lord_realm_net_label(), "SYNC ERROR") == 0);
    p4_game_instance_stop(&instance);
}

static void test_runtime_save_render_and_exit(void)
{
    save_mock_t save = {0};
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            P4_GAME_CAP_SAVE,
        .save_context = &save,
        .queue_save = mock_queue_save,
        .read_save_status = mock_read_save,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    CHECK(start_game(&instance, &state, &services));
    const p4_game_input_t activate = {
        .held = P4_BUTTON_A, .pressed = P4_BUTTON_A,
    };
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_NAME);

    enum {
        GUARD = 17,
        STRIDE = P4_GAME_SURFACE_WIDTH + 7,
        WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
        TOTAL = GUARD + WORDS + GUARD,
    };
    uint16_t *const allocation = calloc(TOTAL, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation != NULL) {
        for (size_t index = 0U; index < TOTAL; ++index) {
            allocation[index] = UINT16_C(0x5aa5);
        }
        p4_game_surface_t surface = {
            .pixels = allocation + GUARD,
            .stride_pixels = STRIDE,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        (void)strcpy(state.player.name, "Test Hero");
        state.player.hero_style = LORD_HERO_STYLE_HEROINE;
        state.player.hero_class = LORD_CLASS_MYSTICAL;
        state.player.level = 3U;
        state.player.hit_points = 54;
        state.player.max_hit_points = 70;
        state.player.strength = 38;
        state.player.defense = 9;
        state.player.gold = 4321U;
        state.player.bank = 9876U;
        state.player.experience = 1120U;
        state.player.forest_fights = 11U;
        state.player.day = 4U;
        state.player.charm = 22U;
        state.player.gems = 3U;
        state.player.young_heroes_helped = 1U;
        state.player.skill[LORD_CLASS_MYSTICAL] = 12U;
        state.player.skill_uses[LORD_CLASS_MYSTICAL] = 3U;
        state.selected_player = 1U;
        state.realm[1].trust = 70U;
        state.dice_player = 9U;
        state.dice_host = 6U;
        state.quiz_wager = 20U;
        state.quiz_operator = 2U;
        state.quiz_left = 7U;
        state.quiz_right = 8U;
        state.quiz_correct = 2U;
        state.quiz_answers[0] = 54U;
        state.quiz_answers[1] = 55U;
        state.quiz_answers[2] = 56U;
        state.quiz_answers[3] = 58U;
        state.realm[1].guild_id = UINT32_MAX;
        state.realm[1].guild_name_code = LORD_GUILD_NAME_COUNT;
        (void)strcpy(state.realm[0].name, "Longest Player Name 123");
        state.realm[0].level = 12U;
        state.realm[0].guild_id = UINT32_MAX;
        state.realm[0].guild_name_code = LORD_GUILD_NAME_COUNT;
        state.guild_status = (lord_guild_status_t){
            .supported = true,
            .guild_id = UINT32_MAX,
            .name_code = LORD_GUILD_NAME_COUNT,
            .member_count = 8U,
            .role = 1U,
            .prestige = UINT32_MAX,
            .season_points = UINT32_MAX,
            .banner_stars = UINT16_MAX,
            .quest_progress = LORD_GUILD_QUEST_GOAL,
            .quest_goal = LORD_GUILD_QUEST_GOAL,
            .wins = UINT16_MAX,
            .losses = UINT16_MAX,
            .draws = UINT16_MAX,
            .last_outcome = 1U,
            .last_opponent_name_code = LORD_GUILD_NAME_COUNT - 1U,
            .daily_flags = 0x0fU,
        };
        state.guild_summaries[0] = (lord_guild_summary_t){
            .valid = true,
            .guild_id = UINT32_MAX,
            .name_code = LORD_GUILD_NAME_COUNT,
            .member_count = 8U,
            .banner_stars = UINT8_MAX,
            .prestige = UINT32_MAX,
            .season_points = UINT32_MAX,
            .wins = UINT16_MAX,
            .losses = UINT16_MAX,
            .draws = UINT16_MAX,
        };
        state.guild_page_total = LORD_GUILD_NAME_COUNT;
        state.guild_target = LORD_GUILD_TARGET_CHEER;
        (void)strcpy(state.battle_line,
                     "You win 10 ChompCoin! The table cheers.");
        (void)strcpy(state.conversation, "THE DRAGON IS RESTLESS TONIGHT");
        (void)strcpy(state.editor_text, "MEET ME AT THE INN");
        for (int screen = LORD_SCREEN_TITLE;
             screen <= LORD_SCREEN_GUILD_STANDINGS; ++screen) {
            state.screen = (lord_screen_t)screen;
            state.selection = 0U;
            state.menu_scroll = 0U;
            state.selected_igm = 0U;
            CHECK(p4_game_instance_render(&instance, &surface));
            capture_frame_if_requested(&surface, state.screen);
        }

        /* Every imported forest monster must render a substantial animated
         * portrait. 5600 ms is phase one for every supported cadence:
         * floor(5600 / {160,192,224,256,288,320,352}) is always odd. */
        enum { LORD_TEST_CREATURE_PHASE_ONE_MS = 5600 };
        uint32_t monster_art_hashes[LORD_MONSTER_COUNT];
        FILE *const monster_manifest =
            open_monster_manifest_if_requested();
        for (size_t monster_index = 0U;
             monster_index < LORD_MONSTER_COUNT; ++monster_index) {
            const lord_monster_t *const monster = &s_monsters[monster_index];
            const lord_named_palette_expectation_t *const palette =
                named_palette_expectation(monster_index);
            append_monster_manifest_row(
                monster_manifest, monster_index, "monster", monster->name,
                monster->weapon,
                palette != NULL ? palette->profile_name : "default");
            state.screen = LORD_SCREEN_BATTLE;
            state.battle_kind = LORD_BATTLE_FOREST;
            state.enemy_intent = LORD_ENEMY_INTENT_STRIKE;
            state.selection = 0U;
            (void)strcpy(state.enemy.name, monster->name);
            (void)strcpy(state.enemy.weapon, monster->weapon);
            state.enemy.death_text = monster->death;
            state.enemy.hit_points = monster->hit_points;
            state.enemy.max_hit_points = monster->hit_points;
            state.enemy.strength = monster->strength;
            state.enemy.defense = 0;
            state.enemy.gold = monster->gold;
            state.enemy.experience = monster->experience;
            (void)strcpy(state.battle_line, "The creature blocks the path.");

            state.creature_animation_ms = 0U;
            const lord_state_t render_before = state;
            CHECK(p4_game_instance_render(&instance, &surface));
            if (monster_index == 0U) {
                CHECK(memcmp(&state, &render_before, sizeof(state)) == 0);
                const uint32_t first_render_hash = surface_region_hash(
                    &surface, 16U, 32U, 80U, 80U);
                CHECK(p4_game_instance_render(&instance, &surface));
                CHECK(surface_region_hash(
                    &surface, 16U, 32U, 80U, 80U) == first_render_hash);
                CHECK(memcmp(&state, &render_before, sizeof(state)) == 0);
            }
            CHECK(surface_region_non_color_count(
                &surface, 16U, 32U, 80U, 80U,
                UINT16_C(0x108a)) >= 128U);
            check_named_monster_palette(
                &surface, monster_index, monster->name);
            monster_art_hashes[monster_index] = surface_region_hash(
                &surface, 16U, 32U, 80U, 80U);
            capture_monster_frame_if_requested(
                &surface, monster_index, 0U);

            state.creature_animation_ms =
                LORD_TEST_CREATURE_PHASE_ONE_MS;
            CHECK(p4_game_instance_render(&instance, &surface));
            CHECK(surface_region_non_color_count(
                &surface, 16U, 32U, 80U, 80U,
                UINT16_C(0x108a)) >= 128U);
            check_named_monster_palette(
                &surface, monster_index, monster->name);
            CHECK(surface_region_hash(
                &surface, 16U, 32U, 80U, 80U) !=
                monster_art_hashes[monster_index]);
            capture_monster_frame_if_requested(
                &surface, monster_index, 1U);
        }
        size_t unique_monster_art = 0U;
        for (size_t monster_index = 0U;
             monster_index < LORD_MONSTER_COUNT; ++monster_index) {
            bool seen = false;
            for (size_t earlier = 0U; earlier < monster_index; ++earlier) {
                if (monster_art_hashes[earlier] ==
                    monster_art_hashes[monster_index]) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                ++unique_monster_art;
            }
        }
        CHECK(unique_monster_art >= 48U);

        /* Every message recognizer branch must animate too; ordinary realm
         * messages remain a stable text panel when only the clock changes. */
        static const char *const narrative_creatures[][2] = {
            {"A troll lunges for your ChompCoin pouch!",
             "You dodge behind an ancient oak."},
            {"A grave guardian catches you.",
             "Its lantern burns with cold fire."},
            {"A furious forest fairy circles overhead.",
             "Tiny wings sparkle in the moonlight."},
            {"Your horse greets the forest herd.",
             "The trail fills with friendly hoofbeats."},
            {"The hag sees your empty gem pouch.",
             "Her crooked staff glows green."},
            {"The Red Dragon waits beyond the trees.",
             "A warm red glow fills the horizon."},
            {"A strange creature blocks the old road.",
             "It watches from beneath a hood."},
        };
        for (size_t encounter = 0U;
             encounter < sizeof(narrative_creatures) /
                 sizeof(narrative_creatures[0]); ++encounter) {
            append_monster_manifest_row(
                monster_manifest, LORD_MONSTER_COUNT + encounter,
                "narrative", narrative_creatures[encounter][0],
                narrative_creatures[encounter][1], "default");
            state.screen = LORD_SCREEN_MESSAGE;
            (void)strcpy(state.message_line_1,
                         narrative_creatures[encounter][0]);
            (void)strcpy(state.message_line_2,
                         narrative_creatures[encounter][1]);
            state.creature_animation_ms = 0U;
            CHECK(p4_game_instance_render(&instance, &surface));
            CHECK(surface_region_non_color_count(
                &surface, 116U, 32U, 80U, 80U,
                UINT16_C(0x108a)) >= 128U);
            const uint32_t narrative_frame_zero = surface_region_hash(
                &surface, 116U, 32U, 80U, 80U);
            capture_monster_frame_if_requested(
                &surface, LORD_MONSTER_COUNT + encounter, 0U);
            state.creature_animation_ms =
                LORD_TEST_CREATURE_PHASE_ONE_MS;
            CHECK(p4_game_instance_render(&instance, &surface));
            CHECK(surface_region_non_color_count(
                &surface, 116U, 32U, 80U, 80U,
                UINT16_C(0x108a)) >= 128U);
            CHECK(surface_region_hash(
                &surface, 116U, 32U, 80U, 80U) !=
                narrative_frame_zero);
            capture_monster_frame_if_requested(
                &surface, LORD_MONSTER_COUNT + encounter, 1U);
        }
        if (monster_manifest != NULL) {
            CHECK(fclose(monster_manifest) == 0);
        }

        state.screen = LORD_SCREEN_MESSAGE;
        (void)strcpy(state.message_line_1, "The realm record is ready.");
        (void)strcpy(state.message_line_2, "Nothing unusual happens here.");
        state.creature_animation_ms = 0U;
        CHECK(p4_game_instance_render(&instance, &surface));
        const uint32_t ordinary_message_frame = surface_region_hash(
            &surface, 0U, 0U, P4_GAME_SURFACE_WIDTH,
            P4_GAME_SURFACE_HEIGHT);
        state.creature_animation_ms = LORD_TEST_CREATURE_PHASE_ONE_MS;
        CHECK(p4_game_instance_render(&instance, &surface));
        CHECK(surface_region_hash(
            &surface, 0U, 0U, P4_GAME_SURFACE_WIDTH,
            P4_GAME_SURFACE_HEIGHT) == ordinary_message_frame);

        /* The animation clock is global presentation time, wraps at one
         * minute without overflow, and advances before early-return gates. */
        const p4_game_input_t animation_idle = {0};
        state.screen = LORD_SCREEN_TOWN;
        state.save_error = false;
        state.creature_animation_ms = 0U;
        CHECK(p4_game_instance_update(
            &instance, &animation_idle, 17U) == P4_GAME_CONTINUE);
        CHECK(state.creature_animation_ms == 17U);
        state.creature_animation_ms = 59950U;
        CHECK(p4_game_instance_update(
            &instance, &animation_idle, 100U) == P4_GAME_CONTINUE);
        CHECK(state.creature_animation_ms == 50U);
        state.creature_animation_ms = UINT32_MAX;
        const uint32_t reduced_max = UINT32_MAX % UINT32_C(60000);
        const uint32_t until_wrap = UINT32_C(60000) - reduced_max;
        const uint32_t expected_after_max = reduced_max >= until_wrap ?
            reduced_max - until_wrap : reduced_max + reduced_max;
        CHECK(p4_lord_game.update(
            &instance.context, &animation_idle, UINT32_MAX) ==
            P4_GAME_CONTINUE);
        CHECK(state.creature_animation_ms == expected_after_max);
        state.save_error = true;
        state.creature_animation_ms = 10U;
        CHECK(p4_game_instance_update(
            &instance, &animation_idle, 16U) == P4_GAME_CONTINUE);
        CHECK(state.creature_animation_ms == 26U);
        state.save_error = false;

        /* Presentation time is absent from LDSV5 and realm sync byte-for-byte
         * and therefore cannot dirty or fork a player's durable progress. */
        CHECK(LORD_SAVE_FORMAT_VERSION == 5);
        CHECK(LORD_SYNC_FORMAT_VERSION == 1);
        uint8_t animation_encoded_before[LORD_SYNC_MAX_BYTES];
        uint8_t animation_encoded_after[LORD_SYNC_MAX_BYTES];
        state.creature_animation_ms = 0U;
        const size_t animation_save_before = lord_save_encode(
            &state, animation_encoded_before,
            sizeof(animation_encoded_before));
        state.creature_animation_ms = LORD_TEST_CREATURE_PHASE_ONE_MS;
        const size_t animation_save_after = lord_save_encode(
            &state, animation_encoded_after,
            sizeof(animation_encoded_after));
        CHECK(animation_save_before > 0U);
        CHECK(animation_save_before == animation_save_after);
        CHECK(memcmp(animation_encoded_before, animation_encoded_after,
                     animation_save_before) == 0);
        lord_state_t animation_restored;
        CHECK(lord_save_decode(&animation_restored,
                               animation_encoded_after,
                               animation_save_after));
        CHECK(animation_restored.creature_animation_ms == 0U);

        uint8_t animation_actor[LORD_SYNC_ACTOR_ID_BYTES];
        for (size_t index = 0U; index < sizeof(animation_actor); ++index) {
            animation_actor[index] = (uint8_t)(0x40U + index);
        }
        state.creature_animation_ms = 0U;
        const size_t animation_sync_before = lord_sync_encode(
            &state, animation_actor, UINT64_C(0x1122334455667788),
            animation_encoded_before, sizeof(animation_encoded_before));
        state.creature_animation_ms = LORD_TEST_CREATURE_PHASE_ONE_MS;
        const size_t animation_sync_after = lord_sync_encode(
            &state, animation_actor, UINT64_C(0x1122334455667788),
            animation_encoded_after, sizeof(animation_encoded_after));
        CHECK(animation_sync_before > 0U);
        CHECK(animation_sync_before == animation_sync_after);
        CHECK(memcmp(animation_encoded_before, animation_encoded_after,
                     animation_sync_before) == 0);

        state.screen = LORD_SCREEN_TOWN;
        state.save_error = false;
        CHECK(p4_game_instance_render(&instance, &surface));
        const uint32_t ordinary_town_hash = surface_region_hash(
            &surface, 40U, 40U, 240U, 72U);
        state.save_error = true;
        CHECK(p4_game_instance_render(&instance, &surface));
        const uint32_t save_error_overlay_hash = surface_region_hash(
            &surface, 40U, 40U, 240U, 72U);
        CHECK(save_error_overlay_hash != ordinary_town_hash);
        const p4_game_input_t blocked_activate = {
            .held = P4_BUTTON_A, .pressed = P4_BUTTON_A,
        };
        CHECK(p4_game_instance_update(&instance, &blocked_activate, 16U) ==
              P4_GAME_CONTINUE);
        CHECK(state.screen == LORD_SCREEN_TOWN);
        state.save_error = false;
        /* The member Hall owns a touch-safe three-row viewport. It must keep
         * every one of its eight actions visible regardless of the generic
         * seven-row menu_scroll state maintained by the shared navigator. */
        state.screen = LORD_SCREEN_GUILD;
        for (size_t selection = 0U; selection < 8U; ++selection) {
            state.selection = (uint8_t)selection;
            state.menu_scroll = (uint8_t)(7U - selection);
            CHECK(p4_game_instance_render(&instance, &surface));
            size_t first = selection > 1U ? selection - 1U : 0U;
            if (first + 3U > 8U) {
                first = 5U;
            }
            const size_t selected_y = 96U + (selection - first) * 8U;
            CHECK(surface.pixels[selected_y * STRIDE + 302U] ==
                  UINT16_C(0xa800));
        }
        memset(state.guild_summaries, 0, sizeof(state.guild_summaries));
        state.screen = LORD_SCREEN_GUILD_STANDINGS;
        state.guild_page_offset = 0U;
        state.guild_page_total = 0U;
        state.guild_page_received = false;
        CHECK(p4_game_instance_render(&instance, &surface));
        const uint32_t pending_standings_hash = surface_region_hash(
            &surface, 16U, 34U, 200U, 7U);
        state.guild_page_received = true;
        CHECK(p4_game_instance_render(&instance, &surface));
        const uint32_t empty_standings_hash = surface_region_hash(
            &surface, 16U, 34U, 200U, 7U);
        CHECK(empty_standings_hash != pending_standings_hash);
        for (size_t index = 0U; index < GUARD; ++index) {
            CHECK(allocation[index] == UINT16_C(0x5aa5));
            CHECK(allocation[GUARD + WORDS + index] == UINT16_C(0x5aa5));
        }
        for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
            for (size_t column = P4_GAME_SURFACE_WIDTH;
                 column < STRIDE; ++column) {
                CHECK(surface.pixels[row * STRIDE + column] ==
                      UINT16_C(0x5aa5));
            }
        }
        free(allocation);
    }
    state.player.level = 1U;
    (void)strcpy(state.player.name, "Saved Hero");
    state.player.max_hit_points = 20;
    state.player.hit_points = 20;
    state.player.strength = 10;
    state.player.day = 1U;
    state.save_dirty = true;
    ++state.save_sequence;
    uint8_t save_probe[LORD_SAVE_MAX_BYTES];
    CHECK(state.save_available);
    CHECK(state.save_ticket == P4_GAME_SAVE_INVALID_TICKET);
    CHECK(lord_save_encode(&state, save_probe, sizeof(save_probe)) > 0U);
    const p4_game_input_t idle = {0};
    (void)p4_game_instance_update(&instance, &idle, 16U);
    CHECK(save.bytes > 0U);
    (void)p4_game_instance_update(&instance, &idle, 16U);
    CHECK(!state.save_dirty);

    save.defer_commit = true;
    state.save_dirty = true;
    ++state.save_local_generation;
    ++state.save_sequence;
    (void)p4_game_instance_update(&instance, &idle, 16U);
    CHECK(state.save_ticket != P4_GAME_SAVE_INVALID_TICKET);
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        state.sync_actor_id[index] = (uint8_t)(0xb0U + index);
    }
    state.sync_server_revision = 8U;
    state.sync_committed_save_sequence = state.save_sequence;
    state.save_dirty = true;
    ++state.save_local_generation;
    (void)p4_game_instance_update(&instance, &idle, 16U);
    CHECK(state.save_dirty);
    save.defer_commit = false;
    (void)p4_game_instance_update(&instance, &idle, 16U);
    CHECK(state.save_dirty);
    CHECK(state.save_ticket != P4_GAME_SAVE_INVALID_TICKET);
    lord_state_t persisted_sync;
    CHECK(lord_save_decode(&persisted_sync, save.payload, save.bytes));
    CHECK(persisted_sync.sync_server_revision == 8U);
    (void)p4_game_instance_update(&instance, &idle, 16U);
    CHECK(!state.save_dirty);

    const p4_game_input_t back = {
        .held = P4_BUTTON_BACK, .pressed = P4_BUTTON_BACK,
    };
    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);

    p4_game_services_t relaunch_services = services;
    relaunch_services.save_data = save.payload;
    relaunch_services.save_bytes = save.bytes;
    relaunch_services.save_schema_version = LORD_SAVE_FORMAT_VERSION;
    relaunch_services.save_sequence = save.sequence;
    lord_state_t relaunched;
    CHECK(start_game(&instance, &relaunched, &relaunch_services));
    CHECK(strcmp(relaunched.player.name, "Saved Hero") == 0);
    CHECK(relaunched.sync_server_revision == 8U);
    CHECK(memcmp(relaunched.sync_actor_id, state.sync_actor_id,
                 LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(!relaunched.guild_page_received);
    CHECK(!relaunched.save_dirty);
    p4_game_instance_stop(&instance);
}

static void test_paid_minigame_save_barriers(void)
{
    save_mock_t save = {
        .defer_commit = true,
    };
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            P4_GAME_CAP_SAVE,
        .save_context = &save,
        .queue_save = mock_queue_save,
        .read_save_status = mock_read_save,
    };
    const p4_game_input_t idle = {0};
    const p4_game_input_t activate = {
        .held = P4_BUTTON_A, .pressed = P4_BUTTON_A,
    };
    const p4_game_input_t back = {
        .held = P4_BUTTON_BACK, .pressed = P4_BUTTON_BACK,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    CHECK(start_game(&instance, &state, &services));

    /* Build a normal hero, then restore the runtime-owned save fields that
     * lord_initialize intentionally clears.  A deferred host commit must hide
     * every random die value and prevent Back from turning a paid roll into a
     * free preview. */
    enter_town(&state, LORD_CLASS_THIEF);
    state.save_available = true;
    state.save_dirty = false;
    state.save_error = false;
    state.save_ticket = P4_GAME_SAVE_INVALID_TICKET;
    state.save_local_generation = 0U;
    state.save_queued_generation = 0U;
    state.host_save_sequence = 0U;
    state.player.gold = 100U;
    state.screen = LORD_SCREEN_DRAGON_DICE;
    state.selection = 0U;

    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == 95U);
    CHECK(state.minigame_save_barrier);
    CHECK(state.minigame_pending == LORD_MINIGAME_PENDING_DICE);
    CHECK(!state.dice_active);
    CHECK(state.dice_player == 0U);
    CHECK(state.dice_host == 0U);
    CHECK(state.dice_rolls == 0U);
    CHECK(state.save_ticket != P4_GAME_SAVE_INVALID_TICKET);
    CHECK(save.bytes > 0U);

    lord_state_t persisted;
    CHECK(lord_save_decode(&persisted, save.payload, save.bytes));
    CHECK(persisted.player.gold == 95U);
    CHECK(persisted.screen == LORD_SCREEN_TOWN);

    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_DRAGON_DICE);
    CHECK(state.minigame_save_barrier);
    CHECK(!state.dice_active);
    CHECK(state.dice_player == 0U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.minigame_save_barrier);
    CHECK(!state.dice_active);

    save.defer_commit = false;
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.minigame_save_barrier);
    CHECK(state.minigame_pending == LORD_MINIGAME_PENDING_NONE);
    CHECK(state.dice_active);
    CHECK(state.dice_rolls == 2U);
    CHECK(state.dice_player >= 2U && state.dice_player <= 12U);
    CHECK(state.dice_host == 0U);
    CHECK(state.save_dirty);

    /* Commit the newly generated round before reusing the same save host for
     * Aragorn's challenge. */
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.save_dirty);
    CHECK(state.save_ticket == P4_GAME_SAVE_INVALID_TICKET);

    save.defer_commit = true;
    state.player.gold = 100U;
    state.igm_used_mask = 0U;
    state.screen = LORD_SCREEN_IGM_DETAIL;
    state.selected_igm = 0U;
    state.selection = 0U;
    state.quiz_wager = 0U;
    state.quiz_left = 0U;
    state.quiz_right = 0U;
    state.quiz_correct = 0U;
    memset(state.quiz_answers, 0, sizeof(state.quiz_answers));

    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_ARAGORN_QUIZ);
    CHECK(state.player.gold == 95U);
    CHECK((state.igm_used_mask & 1U) != 0U);
    CHECK(state.quiz_wager == 5U);
    CHECK(state.minigame_save_barrier);
    CHECK(state.minigame_pending == LORD_MINIGAME_PENDING_QUIZ);
    CHECK(state.quiz_left == 0U);
    CHECK(state.quiz_right == 0U);
    for (size_t index = 0U; index < 4U; ++index) {
        CHECK(state.quiz_answers[index] == 0U);
    }
    CHECK(state.save_ticket != P4_GAME_SAVE_INVALID_TICKET);
    CHECK(lord_save_decode(&persisted, save.payload, save.bytes));
    CHECK(persisted.player.gold == 95U);
    CHECK((persisted.igm_used_mask & 1U) != 0U);

    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_ARAGORN_QUIZ);
    CHECK(state.minigame_save_barrier);
    CHECK(state.quiz_left == 0U);
    CHECK(state.quiz_right == 0U);

    save.defer_commit = false;
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.minigame_save_barrier);
    CHECK(state.minigame_pending == LORD_MINIGAME_PENDING_NONE);
    CHECK(state.quiz_correct < 4U);
    CHECK(state.quiz_answers[state.quiz_correct] ==
          expected_quiz_answer(&state));
    for (size_t left = 0U; left < 4U; ++left) {
        for (size_t right = left + 1U; right < 4U; ++right) {
            CHECK(state.quiz_answers[left] != state.quiz_answers[right]);
        }
    }
    CHECK(state.save_dirty);
    p4_game_instance_stop(&instance);

    /* If durable storage rejects the initial debit, cancel without exposing
     * a puzzle, restore the stake, and reopen Aragorn's once-daily visit. */
    save = (save_mock_t){
        .reject_queue = true,
    };
    CHECK(start_game(&instance, &state, &services));
    enter_town(&state, LORD_CLASS_MYSTICAL);
    state.save_available = true;
    state.save_dirty = false;
    state.save_error = false;
    state.save_ticket = P4_GAME_SAVE_INVALID_TICKET;
    state.save_local_generation = 0U;
    state.save_queued_generation = 0U;
    state.host_save_sequence = 0U;
    state.player.gold = 100U;
    state.igm_used_mask = LORD_DAILY_DICE_FRIENDSHIP_MASK;
    state.screen = LORD_SCREEN_IGM_DETAIL;
    state.selected_igm = 0U;
    state.selection = 0U;

    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == 95U);
    CHECK(state.minigame_save_barrier);
    CHECK(state.minigame_pending == LORD_MINIGAME_PENDING_QUIZ);
    CHECK(!state.save_available);
    CHECK(state.save_error);
    CHECK(state.quiz_left == 0U);
    CHECK(state.quiz_right == 0U);

    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(!state.minigame_save_barrier);
    CHECK(state.minigame_pending == LORD_MINIGAME_PENDING_NONE);
    CHECK(state.player.gold == 100U);
    CHECK(state.quiz_wager == 0U);
    CHECK((state.igm_used_mask & 1U) == 0U);
    CHECK((state.igm_used_mask & LORD_DAILY_DICE_FRIENDSHIP_MASK) != 0U);
    CHECK(state.quiz_left == 0U);
    CHECK(state.quiz_right == 0U);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(state.return_screen == LORD_SCREEN_IGM);
    CHECK(strstr(state.message_line_1, "canceled and refunded") != NULL);
    p4_game_instance_stop(&instance);

    /* A broken status adapter and an explicit terminal ERROR are both safe
     * cancellations. Neither may hang input or disclose a die roll. */
    for (size_t failure = 0U; failure < 2U; ++failure) {
        save = (save_mock_t){0};
        save.reject_status = failure == 0U;
        save.forced_status = failure == 1U ? P4_GAME_SAVE_ERROR :
                                             P4_GAME_SAVE_NONE;
        CHECK(start_game(&instance, &state, &services));
        enter_town(&state, LORD_CLASS_THIEF);
        state.save_available = true;
        state.save_dirty = false;
        state.save_error = false;
        state.save_ticket = P4_GAME_SAVE_INVALID_TICKET;
        state.save_local_generation = 0U;
        state.save_queued_generation = 0U;
        state.host_save_sequence = 0U;
        state.player.gold = 100U;
        state.screen = LORD_SCREEN_DRAGON_DICE;
        state.selection = 0U;

        CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
              P4_GAME_CONTINUE);
        CHECK(state.player.gold == 95U);
        CHECK(state.minigame_save_barrier);
        CHECK(!state.dice_active);
        CHECK(state.dice_player == 0U);
        CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
              P4_GAME_CONTINUE);
        CHECK(!state.minigame_save_barrier);
        CHECK(!state.save_available);
        CHECK(state.save_error);
        CHECK(state.player.gold == 100U);
        CHECK(!state.dice_active);
        CHECK(state.dice_player == 0U);
        CHECK(state.screen == LORD_SCREEN_MESSAGE);
        CHECK(state.return_screen == LORD_SCREEN_INN);
        p4_game_instance_stop(&instance);
    }
}

static p4_physical_touch_t physical_touch_for(uint16_t logical_x,
                                              uint16_t logical_y)
{
    return (p4_physical_touch_t){
        .x = (uint16_t)(P4_INPUT_VIEWPORT_LEFT +
            ((uint32_t)logical_x * P4_INPUT_VIEWPORT_WIDTH) /
                P4_GAME_SURFACE_WIDTH + 1U),
        .y = (uint16_t)(P4_INPUT_VIEWPORT_TOP +
            ((uint32_t)logical_y * P4_INPUT_VIEWPORT_HEIGHT) /
                P4_GAME_SURFACE_HEIGHT + 1U),
    };
}

static void test_ansi_touch_lifecycle(void)
{
    static const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    p4_game_input_mapper_t mapper;
    p4_game_input_t input;
    CHECK(start_game(&instance, &state, &services));
    p4_game_input_mapper_init(&mapper);

    /* The removed A/B/D-pad artwork must not leave invisible active zones. */
    p4_physical_touch_t touch = physical_touch_for(286U, 158U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK((input.pressed & P4_BUTTON_A) != 0U);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_TITLE);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);

    /* The text command footer replaces the joystick and buttons. */
    touch = physical_touch_for(280U, 188U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_NAME);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);

    touch = physical_touch_for(280U, 188U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_TEXT_EDITOR);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);

    /* Drawn keyboard keys activate directly and holding never repeats. */
    touch = physical_touch_for(30U, 60U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(strcmp(state.editor_text, "A") == 0);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(strcmp(state.editor_text, "A") == 0);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);

    touch = physical_touch_for(80U, 120U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.editor_text[0] == '\0');
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);

    /* Direct battle-cell taps choose the visible command, not a ghost pad. */
    state.screen = LORD_SCREEN_BATTLE;
    state.battle_kind = LORD_BATTLE_FOREST;
    state.player.hit_points = 20;
    state.player.max_hit_points = 20;
    state.enemy.hit_points = 20;
    state.enemy.max_hit_points = 20;
    state.enemy.strength = 1;
    state.selection = 0U;
    touch = physical_touch_for(200U, 163U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_STATS);
    CHECK(state.return_screen == LORD_SCREEN_BATTLE);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);

    state.screen = LORD_SCREEN_NAME;
    touch = physical_touch_for(200U, 188U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_TITLE);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);

    touch = physical_touch_for(26U, 12U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK((input.pressed & P4_BUTTON_BACK) != 0U);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

int main(void)
{
    CHECK(p4_game_descriptor_valid(&p4_lord_game));
    CHECK(p4_lord_game.launcher_id == 112U);
    CHECK(p4_lord_game.state_bytes == sizeof(lord_state_t));
    CHECK(sizeof(lord_state_t) <= P4_GAME_MAX_STATE_BYTES);
    test_character_creation_and_core_menu();
    test_shops_bank_and_transfer();
    test_forest_training_skills_and_dragon();
    test_tactical_combat_counters_and_classes();
    test_lethal_failed_escape_is_persistable();
    test_progression_pacing_and_dormant_rewards();
    test_mail_pvp_friendship_and_mentoring();
    test_realm_bound_inn_sleep();
    test_friendship_hp_bonus_reversible();
    test_bartender_riddle_budget();
    test_forest_action_cost_and_real_rankings();
    test_full_inn_and_igms();
    test_player_driven_tavern_games();
    test_dragon_dice_daily_friendship_cap();
    test_save_round_trip();
    test_save_migrations_and_combat_bounds();
    test_legacy_empty_realm_slot_migration();
    test_backend_sync_envelope();
    test_adventure_club_client_protocol();
    test_pending_realm_debits_fail_offline_and_retry_once();
    test_p4mp_mac_realm_hourly_sync();
    test_p4rm_commit_result_retry();
    test_offline_save_reconnect_reconciliation();
    test_authorized_local_adoption();
    test_runtime_save_render_and_exit();
    test_paid_minigame_save_barriers();
    test_ansi_touch_lifecycle();
    if (s_failures != 0) {
        fprintf(stderr, "%d LORD test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("LORD full-port tests passed");
    return EXIT_SUCCESS;
}
