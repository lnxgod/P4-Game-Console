// SPDX-License-Identifier: MIT
/*
 * Byte Buddy is an original dragon-raising virtual pet. Its bounded PixelLab
 * and ImageGen sprite sheets, compact frame bank, and optional validated SD
 * resource sidecar are documented beside the game.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "p4/audio_pack.h"
#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/game.h"
#include "p4/input.h"
#include "p4/visual.h"

#include "byte_buddy_internal.h"
#include "generated/byte_buddy_dragon_atlas.inc"

enum {
    STAT_MAX = 100,
    DECAY_INTERVAL_MS = 5000,
    MINI_GAME_DURATION_MS = 18000,
    MINI_GAME_CATCH_Y = 126,
    MINI_GAME_STAR_START_Y = 38,
    MINI_GAME_EFFECT_DURATION_MS = 480,
    MINI_GAME_CATCHER_MAX_SPEED = 190,
    MINI_GAME_CATCHER_RESPONSE_MS = 96,
    MINI_GAME_CONTROLLER_TARGET_SPEED = 260,
    MINI_GAME_STAR_BASE_SPEED = 58,
    MINI_GAME_STAR_STAGE_SPEED = 6,
    MINI_GAME_STAR_STREAK_SPEED = 3,
    MINI_GAME_STAR_MAX_STREAK = 6,
    MINI_GAME_STAR_MAX_SPEED = 100,
    PET_LEFT_MIN = 24,
    PET_LEFT_MAX = 296,
    ACTION_COUNT = 4,
    GROW_BABY_INTERACTIONS = 8,
    GROW_WINGED_INTERACTIONS = 28,
    GROW_FLYING_INTERACTIONS = 60,
    GROW_ELEMENTAL_INTERACTIONS = 104,
    CARE_CREDIT_COOLDOWN_MS = 650,
    REACTION_DURATION_MS = 1400,
    SIGNAL_HUNT_UNLOCK_RSSI = -65,
    SIGNAL_BATTLE_DURATION_MS = 12000,
    SIGNAL_WEAVE_DURATION_MS = 16000,
    SIGNAL_WEAVE_SETTLE_MS = 300,
    SIGNAL_HIT_DURATION_MS = 260,
    SIGNAL_TRACK_REFRESH_MS = 1600,
    SIGNAL_REQUEST_BUSY_MS = 1200,
    SIGNAL_CONTROLLER_CURSOR_SPEED = 125,
    SIGNAL_CONTROLLER_CURSOR_MIN_X = 96,
    SIGNAL_CONTROLLER_CURSOR_MAX_X = 224,
    SIGNAL_CONTROLLER_CURSOR_MIN_Y = 28,
    SIGNAL_CONTROLLER_CURSOR_MAX_Y = 132,
    SIGNAL_HABITAT_VARIANTS = 20,
    DRAGON_FRAME_WIDTH = 64,
    DRAGON_FRAME_HEIGHT = 64,
    DRAGON_FRAMES_PER_SHEET = 16,
    DRAGON_PALETTE_ENTRIES = 16,
    DRAGON_PALETTE_BYTES = 32,
    DRAGON_PACKED_FRAME_BYTES = 2048,
    DRAGON_ART_HEADER_BYTES = 64,
    DRAGON_ART_PALETTE_OFFSET_FIELD = 36,
    DRAGON_ART_PIXEL_OFFSET_FIELD = 40,
    DRAGON_ART_TOTAL_BYTES_FIELD = 44,
    DRAGON_RARE_SHEET = 5,
    DRAGON_MORPH_EGG_SHEET = 6,
    DRAGON_ELEMENT_EGG_SHEET = 7,
    DRAGON_BABY_REACTION_SHEET = 8,
    DRAGON_FLIGHT_CYCLE_SHEET = 9,
    DRAGON_ELEMENT_BREATH_SHEET = 10,
    DRAGON_MORPH_EGG_AMBIENT_SHEET = 11,
    DRAGON_ELEMENT_EGG_AMBIENT_SHEET = 12,
    DRAGON_BABY_IDLE_SHEET = 13,
    DRAGON_BABY_CARE_SHEET = 14,
    DRAGON_WINGED_IDLE_SHEET = 15,
    DRAGON_WINGED_CARE_SHEET = 16,
    DRAGON_FLIGHT_AEROBATICS_SHEET = 17,
    DRAGON_ELEMENT_MASTERY_SHEET = 18,
    DRAGON_ELEMENT_IMPACT_SHEET = 19,
    DRAGON_HATCH_TRANSITION_SHEET = 20,
    DRAGON_SIGNAL_GENETICS_SHEET = 21,
    STAR_CATCHER_REWARD_SHEET = 22,
    BYTE_BUDDY_ITEM_COMPONENT_SHEET = 23,
    BYTE_BUDDY_SIGNAL_GENOME_SHEET = 24,
    DRAGON_EXTENDED_SHEET_COUNT = 25,
    ACHIEVEMENT_FIRST_CARE = UINT32_C(1) << 0U,
    ACHIEVEMENT_CLEAN = UINT32_C(1) << 1U,
    ACHIEVEMENT_PLAY = UINT32_C(1) << 2U,
    ACHIEVEMENT_GROW = UINT32_C(1) << 3U,
    ACHIEVEMENT_FIRST_SIGNAL = UINT32_C(1) << 4U,
    ACHIEVEMENT_SIGNAL_CHORUS = UINT32_C(1) << 5U,
    ACHIEVEMENT_MYTHIC_LINEAGE = UINT32_C(1) << 6U,
    ACHIEVEMENT_ETERNAL_LINEAGE = UINT32_C(1) << 7U,
};

typedef enum {
    ACTION_FEED = 0,
    ACTION_PLAY,
    ACTION_CLEAN,
    ACTION_REST,
} buddy_action_t;

typedef enum {
    REACTION_IDLE = 0,
    REACTION_FEED,
    REACTION_PLAY,
    REACTION_CLEAN,
    REACTION_REST,
    REACTION_PET,
    REACTION_GROW,
    REACTION_SIGNAL,
} buddy_reaction_t;

typedef enum {
    STAR_KIND_GOLD = 0,
    STAR_KIND_CALM,
    STAR_KIND_HEART,
    STAR_KIND_COUNT,
} star_kind_t;

typedef struct {
    uint8_t hunger;
    uint8_t joy;
    uint8_t hygiene;
    uint8_t energy;
    uint8_t stage;
    uint8_t element;
    uint8_t wing_style;
    uint8_t reaction;
    uint8_t selected_action;
    uint8_t play_catches;
    uint8_t play_streak;
    uint8_t play_best_streak;
    uint8_t star_kind;
    uint8_t star_spawn_count;
    uint8_t signal_view;
    uint8_t signal_page;
    uint8_t signal_selected_index;
    uint8_t signal_consumed_count;
    uint8_t signal_feeds;
    uint8_t signal_hue;
    uint8_t signal_element_votes[3];
    uint8_t signal_battle_hp;
    uint8_t signal_battle_max_hp;
    uint8_t signal_battle_pattern;
    uint8_t signal_weave_step;
    uint8_t signal_guard_charges;
    uint8_t signal_focus_row;
    uint8_t menu_selection;
    uint16_t signal_weave_settle_ms;
    uint8_t upgrades[BYTE_BUDDY_UPGRADE_COUNT];
    uint8_t style_unlocked[BYTE_BUDDY_STYLE_COUNT];
    uint8_t style_selected[BYTE_BUDDY_STYLE_COUNT];
    uint16_t coins;
    uint16_t care_actions;
    uint16_t style_mix_count;
    uint16_t action_counts[ACTION_COUNT];
    uint16_t pet_actions;
    uint32_t decay_accumulator_ms;
    uint32_t care_credit_cooldown_ms;
    uint32_t animation_ms;
    uint32_t reaction_ms;
    uint32_t mini_elapsed_ms;
    uint32_t star_effect_ms;
    uint32_t signal_generation;
    uint32_t signal_battle_elapsed_ms;
    uint32_t signal_battle_bonus_ms;
    uint32_t signal_weave_charge_units;
    uint32_t signal_hit_ms;
    uint32_t signal_reward_ms;
    uint32_t signal_track_refresh_ms;
    uint32_t signal_request_busy_ms;
    p4_q16_t catcher_x_q16;
    p4_q16_t catcher_target_x_q16;
    p4_q16_t catcher_velocity_q16;
    p4_q16_t signal_cursor_x_q16;
    p4_q16_t signal_cursor_y_q16;
    int16_t star_x;
    p4_q16_t star_y_q16;
    int16_t star_effect_x;
    int16_t star_effect_y;
    int8_t signal_previous_rssi;
    int8_t signal_trend_db;
    uint8_t signal_samples;
    bool mini_game;
    bool upgrade_shop;
    bool style_shop;
    bool lineage_panel;
    bool signal_hunt;
    bool controller_active;
    bool touch_was_down;
    const uint8_t *art_data;
    size_t art_bytes;
    uint16_t art_sheets;
    uint32_t achievement_mask;
    uint64_t signal_selected_token;
    uint64_t signal_entropy;
    uint64_t signal_consumed[BYTE_BUDDY_SIGNAL_MAX_CONSUMED];
    byte_buddy_lineage_genes_t signal_genes;
    p4_game_signal_snapshot_t signal_snapshot;
    p4_game_audio_effect_player_t audio;
} byte_buddy_state_t;

static const char *const s_actions[ACTION_COUNT] = {
    "FEED", "PLAY", "CLEAN", "REST",
};

static const char *const s_stage_names[BYTE_BUDDY_STAGE_COUNT] = {
    "EGG", "BABY", "WINGED", "FLYING", "ELEMENTAL",
};

static const char *const s_element_names[BYTE_BUDDY_ELEMENT_COUNT] = {
    "MYSTERY", "FIRE", "ICE", "ACID",
};

static const char *const s_wing_names[BYTE_BUDDY_WING_STYLE_COUNT] = {
    "SHINY", "SPIKED",
};

static const char *const s_morph_names[BYTE_BUDDY_MORPH_COUNT] = {
    "NEBULA", "SUNGOLD", "JADE", "GLACIER",
};

static const char *const s_lineage_names[BYTE_BUDDY_LINEAGE_TIER_COUNT] = {
    "DORMANT", "SPARK", "CREST", "AURORA", "ASCENDED", "MYTHIC",
};

static const uint8_t s_lineage_name_lengths[
    BYTE_BUDDY_LINEAGE_TIER_COUNT] = {
    7U, 5U, 5U, 6U, 8U, 6U,
};

static const char *const s_lineage_family_names[4] = {
    "ARC", "PRISM", "THORN", "COMET",
};

static const char *const s_lineage_halo_names[4] = {
    "RADIO", "CROWN", "ORBIT", "PULSE",
};

static const char *const s_lineage_mark_names[4] = {
    "STAR", "FLAME", "FROST", "ACID",
};

static const char *const s_lineage_aura_names[4] = {
    "SPARK", "SHARD", "MOTES", "BITS",
};

static const uint8_t s_lineage_link_requirements[
    BYTE_BUDDY_LINEAGE_TIER_COUNT] = {
    0U, 1U, 3U, 5U, 8U, 12U,
};

static const uint8_t s_lineage_diversity_requirements[
    BYTE_BUDDY_LINEAGE_TIER_COUNT] = {
    0U, 0U, 11U, 15U, 19U, 22U,
};

static const char *const s_resonance_names[BYTE_BUDDY_RESONANCE_COUNT] = {
    "NONE", "NOVA", "GALAXY", "ETERNAL",
};

static const uint8_t s_resonance_name_lengths[
    BYTE_BUDDY_RESONANCE_COUNT] = {
    4U, 4U, 6U, 7U,
};

static const uint8_t s_resonance_link_requirements[
    BYTE_BUDDY_RESONANCE_COUNT] = {
    0U, 16U, 24U, 32U,
};

static const uint8_t s_resonance_diversity_requirements[
    BYTE_BUDDY_RESONANCE_COUNT] = {
    0U, 24U, 26U, 27U,
};

static const uint8_t s_morph_name_lengths[BYTE_BUDDY_MORPH_COUNT] = {
    6U, 7U, 4U, 7U,
};

static const uint8_t s_egg_frames[BYTE_BUDDY_ELEMENT_COUNT][4] = {
    {1U, 5U, 9U, 13U},
    {0U, 8U, 12U, 9U},
    {7U, 11U, 15U, 4U},
    {2U, 6U, 10U, 14U},
};

static const uint8_t s_elemental_idle_frames[3][2][2] = {
    {{1U, 3U}, {0U, 2U}},
    {{5U, 7U}, {4U, 6U}},
    {{9U, 11U}, {8U, 10U}},
};

static const uint8_t s_elemental_reaction_frames[3][2] = {
    {12U, 2U},
    {13U, 6U},
    {14U, 15U},
};

static const uint16_t s_growth_thresholds[BYTE_BUDDY_STAGE_COUNT] = {
    0U,
    GROW_BABY_INTERACTIONS,
    GROW_WINGED_INTERACTIONS,
    GROW_FLYING_INTERACTIONS,
    GROW_ELEMENTAL_INTERACTIONS,
};

static const uint8_t s_eased_motion[32] = {
    0U, 0U, 1U, 1U, 2U, 3U, 4U, 5U,
    6U, 7U, 8U, 9U, 9U, 10U, 10U, 10U,
    10U, 10U, 10U, 9U, 9U, 8U, 7U, 6U,
    5U, 4U, 3U, 2U, 1U, 1U, 0U, 0U,
};

static const uint8_t s_frame_sequence[8] = {
    0U, 1U, 2U, 3U, 2U, 1U, 0U, 3U,
};

static const uint8_t s_style_max[BYTE_BUDDY_STYLE_COUNT] = {
    7U, 5U, 4U, 4U,
};

static const char *const s_body_style_names[] = {
    "NURTURE", "RUBY", "SUNGOLD", "JADE",
    "GLACIER", "ACID", "ROSE", "OBSIDIAN",
};

static const char *const s_eye_style_names[] = {
    "CYAN", "GOLD", "PINK", "GREEN", "VIOLET", "RED",
};

static const char *const s_horn_style_names[] = {
    "CORAL", "IVORY", "GOLD", "CYAN", "ONYX",
};

static const char *const s_trail_style_names[] = {
    "ELEMENT", "STARS", "HEARTS", "FROST", "SPARKS",
};

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | (uint16_t)data[1] << 8U);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8U |
        (uint32_t)data[2] << 16U | (uint32_t)data[3] << 24U;
}

static bool art_bank_valid(const uint8_t *data, size_t bytes,
                           unsigned minimum_sheets,
                           uint16_t *out_sheets)
{
    if (data == NULL || out_sheets == NULL ||
        bytes < DRAGON_ART_HEADER_BYTES ||
        memcmp(data, "BBDART2\0", 8U) != 0 ||
        read_u32(data + 8U) != 2U) {
        return false;
    }
    const uint32_t sheets = read_u32(data + 12U);
    const uint32_t frames = read_u32(data + 32U);
    const uint32_t palette_offset = read_u32(
        data + DRAGON_ART_PALETTE_OFFSET_FIELD);
    const uint32_t pixel_offset = read_u32(
        data + DRAGON_ART_PIXEL_OFFSET_FIELD);
    const uint32_t total_bytes = read_u32(
        data + DRAGON_ART_TOTAL_BYTES_FIELD);
    if (sheets < minimum_sheets || sheets > UINT16_MAX ||
        read_u32(data + 16U) != DRAGON_FRAME_WIDTH ||
        read_u32(data + 20U) != DRAGON_FRAME_HEIGHT ||
        read_u32(data + 24U) != DRAGON_FRAMES_PER_SHEET ||
        read_u32(data + 28U) != DRAGON_PALETTE_ENTRIES ||
        frames != sheets * DRAGON_FRAMES_PER_SHEET ||
        palette_offset != DRAGON_ART_HEADER_BYTES ||
        pixel_offset != palette_offset + frames * DRAGON_PALETTE_BYTES ||
        total_bytes != pixel_offset +
            frames * DRAGON_PACKED_FRAME_BYTES ||
        total_bytes != bytes) {
        return false;
    }
    *out_sheets = (uint16_t)sheets;
    return true;
}

byte_buddy_stage_t byte_buddy_stage_for_interactions(uint16_t interactions)
{
    if (interactions >= s_growth_thresholds[BYTE_BUDDY_STAGE_ELEMENTAL]) {
        return BYTE_BUDDY_STAGE_ELEMENTAL;
    }
    if (interactions >= s_growth_thresholds[BYTE_BUDDY_STAGE_FLYING]) {
        return BYTE_BUDDY_STAGE_FLYING;
    }
    if (interactions >= s_growth_thresholds[BYTE_BUDDY_STAGE_WINGED]) {
        return BYTE_BUDDY_STAGE_WINGED;
    }
    if (interactions >= s_growth_thresholds[BYTE_BUDDY_STAGE_BABY]) {
        return BYTE_BUDDY_STAGE_BABY;
    }
    return BYTE_BUDDY_STAGE_EGG;
}

byte_buddy_element_t byte_buddy_element_for_nurture(
    uint16_t feed_actions,
    uint16_t play_actions,
    uint16_t clean_actions,
    uint16_t rest_actions,
    uint16_t pet_actions)
{
    if (feed_actions == 0U && play_actions == 0U && clean_actions == 0U &&
        rest_actions == 0U && pet_actions == 0U) {
        return BYTE_BUDDY_ELEMENT_MYSTERY;
    }
    const uint32_t shared = pet_actions;
    const uint32_t fire = (uint32_t)feed_actions * 3U + shared;
    const uint32_t ice = (uint32_t)clean_actions * 3U +
        (uint32_t)rest_actions * 2U + shared;
    const uint32_t acid = (uint32_t)play_actions * 3U + shared;
    if (fire >= ice && fire >= acid) {
        return BYTE_BUDDY_ELEMENT_FIRE;
    }
    return ice >= acid ? BYTE_BUDDY_ELEMENT_ICE : BYTE_BUDDY_ELEMENT_ACID;
}

byte_buddy_wing_style_t byte_buddy_wing_style_for_nurture(
    uint16_t feed_actions,
    uint16_t play_actions,
    uint16_t clean_actions,
    uint16_t rest_actions,
    uint16_t pet_actions)
{
    const uint32_t bold = (uint32_t)feed_actions +
        (uint32_t)play_actions * 3U;
    const uint32_t gentle = (uint32_t)clean_actions * 2U +
        (uint32_t)rest_actions * 2U + (uint32_t)pet_actions * 2U;
    return bold > gentle ? BYTE_BUDDY_WINGS_SPIKED
                         : BYTE_BUDDY_WINGS_SHINY;
}

byte_buddy_morph_t byte_buddy_morph_for_nurture(
    uint16_t feed_actions,
    uint16_t play_actions,
    uint16_t clean_actions,
    uint16_t rest_actions,
    uint16_t pet_actions)
{
    if (feed_actions == 0U && play_actions == 0U && clean_actions == 0U &&
        rest_actions == 0U && pet_actions == 0U) {
        return BYTE_BUDDY_MORPH_NEBULA;
    }
    const uint32_t nebula = (uint32_t)pet_actions * 4U;
    const uint32_t sungold = (uint32_t)feed_actions * 3U + rest_actions;
    const uint32_t jade = (uint32_t)play_actions * 3U;
    const uint32_t glacier = (uint32_t)clean_actions * 3U +
        (uint32_t)rest_actions * 2U;
    if (nebula >= sungold && nebula >= jade && nebula >= glacier) {
        return BYTE_BUDDY_MORPH_NEBULA;
    }
    if (sungold >= jade && sungold >= glacier) {
        return BYTE_BUDDY_MORPH_SUNGOLD;
    }
    return jade >= glacier ? BYTE_BUDDY_MORPH_JADE
                           : BYTE_BUDDY_MORPH_GLACIER;
}

uint16_t byte_buddy_upgrade_cost(uint8_t current_level)
{
    static const uint16_t costs[3] = {2U, 5U, 8U};
    return current_level < 3U ? costs[current_level] : UINT16_MAX;
}

uint16_t byte_buddy_style_cost(
    byte_buddy_style_t style, uint8_t unlocked_level)
{
    static const uint8_t base_costs[BYTE_BUDDY_STYLE_COUNT] = {
        2U, 1U, 2U, 2U,
    };
    if ((unsigned)style >= BYTE_BUDDY_STYLE_COUNT ||
        unlocked_level >= s_style_max[style]) {
        return UINT16_MAX;
    }
    return (uint16_t)(base_costs[style] + unlocked_level);
}

uint8_t byte_buddy_remix_choice(
    uint32_t seed, uint8_t previous, uint8_t unlocked_level)
{
    const uint8_t bounded_unlocked = unlocked_level > 7U
        ? 7U : unlocked_level;
    const uint8_t choices = (uint8_t)(bounded_unlocked + 1U);
    if (choices == 1U) {
        return 0U;
    }
    const uint8_t bounded_previous = previous < choices ? previous : 0U;
    uint8_t choice = (uint8_t)((seed >> 16U) % choices);
    if (choice == bounded_previous) {
        const uint8_t offset = (uint8_t)(1U + seed % (choices - 1U));
        choice = (uint8_t)((choice + offset) % choices);
    }
    return choice;
}

uint32_t byte_buddy_style_recipe_id(
    uint8_t body, uint8_t eyes, uint8_t horns, uint8_t trail,
    uint8_t wing_style, uint8_t mutation_hue)
{
    const uint32_t bounded_body = body < 8U ? body : 0U;
    const uint32_t bounded_eyes = eyes < 6U ? eyes : 0U;
    const uint32_t bounded_horns = horns < 5U ? horns : 0U;
    const uint32_t bounded_trail = trail < 5U ? trail : 0U;
    const uint32_t bounded_wings = wing_style < 2U ? wing_style : 0U;
    const uint32_t bounded_hue = mutation_hue < 8U ? mutation_hue : 0U;
    uint32_t recipe = bounded_body;
    recipe = recipe * 6U + bounded_eyes;
    recipe = recipe * 5U + bounded_horns;
    recipe = recipe * 5U + bounded_trail;
    recipe = recipe * 2U + bounded_wings;
    return recipe * 8U + bounded_hue;
}

uint8_t byte_buddy_level_for_interactions(uint16_t interactions)
{
    uint32_t remaining = interactions;
    uint8_t level = 1U;
    while (level < 99U) {
        uint32_t requirement = 6U + (uint32_t)level * 2U;
        if (requirement > 24U) {
            requirement = 24U;
        }
        if (remaining < requirement) {
            break;
        }
        remaining -= requirement;
        ++level;
    }
    return level;
}

uint16_t byte_buddy_star_fall_speed(uint8_t stage, uint8_t streak)
{
    const unsigned bounded_stage = stage < BYTE_BUDDY_STAGE_COUNT
        ? stage : BYTE_BUDDY_STAGE_COUNT - 1U;
    const unsigned bounded_streak = streak < MINI_GAME_STAR_MAX_STREAK
        ? streak : MINI_GAME_STAR_MAX_STREAK;
    unsigned speed = MINI_GAME_STAR_BASE_SPEED +
        bounded_stage * MINI_GAME_STAR_STAGE_SPEED +
        bounded_streak * MINI_GAME_STAR_STREAK_SPEED;
    if (speed > MINI_GAME_STAR_MAX_SPEED) {
        speed = MINI_GAME_STAR_MAX_SPEED;
    }
    return (uint16_t)speed;
}

static uint8_t battle_stat(uint32_t value)
{
    return (uint8_t)(value > 99U ? 99U : value);
}

byte_buddy_battle_stats_t byte_buddy_battle_stats(
    uint16_t feed_actions,
    uint16_t play_actions,
    uint16_t clean_actions,
    uint16_t rest_actions,
    uint16_t pet_actions,
    uint8_t wings_level,
    uint8_t aura_level,
    uint8_t nest_level,
    uint8_t magnet_level)
{
    uint32_t interactions = (uint32_t)feed_actions + play_actions +
        clean_actions + rest_actions + pet_actions;
    if (interactions > UINT16_MAX) {
        interactions = UINT16_MAX;
    }
    const uint8_t level = byte_buddy_level_for_interactions(
        (uint16_t)interactions);
    return (byte_buddy_battle_stats_t){
        .level = level,
        .power = battle_stat((uint32_t)level + feed_actions / 2U +
                             (uint32_t)wings_level * 3U),
        .speed = battle_stat((uint32_t)level + play_actions / 2U +
                             (uint32_t)magnet_level * 3U),
        .guard = battle_stat((uint32_t)level +
                             ((uint32_t)clean_actions + rest_actions) / 3U +
                             (uint32_t)nest_level * 3U),
        .magic = battle_stat((uint32_t)level + pet_actions / 2U +
                             (uint32_t)aura_level * 3U),
    };
}

byte_buddy_battle_stats_t byte_buddy_lineage_battle_stats(
    byte_buddy_battle_stats_t base,
    byte_buddy_signal_lineage_t lineage,
    const byte_buddy_lineage_genes_t *genes)
{
    const uint8_t protected_bonus = genes == NULL
        ? 0U : genes->protected_count > 3U
            ? 3U : genes->protected_count;
    const uint8_t phantom_bonus = genes != NULL &&
        genes->hidden_count != 0U ? 1U : 0U;
    base.power = battle_stat((uint32_t)base.power +
                             lineage.diversity / 6U);
    base.speed = battle_stat((uint32_t)base.speed +
                             lineage.channel_families);
    base.guard = battle_stat((uint32_t)base.guard + protected_bonus);
    base.magic = battle_stat((uint32_t)base.magic + lineage.tier +
                             phantom_bonus);
    return base;
}

byte_buddy_lineage_battle_traits_t byte_buddy_lineage_battle_traits(
    byte_buddy_signal_lineage_t lineage)
{
    const uint8_t tier = lineage.tier < BYTE_BUDDY_LINEAGE_TIER_COUNT
        ? lineage.tier : BYTE_BUDDY_LINEAGE_MYTHIC;
    const uint8_t families = lineage.channel_families < 4U
        ? lineage.channel_families : 4U;
    return (byte_buddy_lineage_battle_traits_t){
        .strike_damage = (uint8_t)(tier / 2U),
        .guard_charges = lineage.shielded ? 1U : 0U,
        .start_time_ms = (uint16_t)((uint16_t)families * 100U),
        .guard_time_ms = (uint16_t)(
            (uint16_t)tier * 60U + (lineage.phantom ? 120U : 0U)),
    };
}

byte_buddy_touch_target_t byte_buddy_touch_target(
    uint16_t x, uint16_t y, bool upgrade_shop,
    bool style_shop, bool mini_game)
{
    if (x < 44U && y < 24U) {
        return BYTE_BUDDY_TOUCH_EXIT;
    }
    if (mini_game) {
        if (x >= 260U && y < 24U) {
            return BYTE_BUDDY_TOUCH_DONE_PLAYING;
        }
        return y >= 28U && y < 164U
            ? BYTE_BUDDY_TOUCH_MOVE_DRAGON : BYTE_BUDDY_TOUCH_NONE;
    }
    if (upgrade_shop) {
        if (y >= 25U && y < 42U) {
            return x < 160U ? BYTE_BUDDY_TOUCH_SHOP_POWER
                            : BYTE_BUDDY_TOUCH_SHOP_STYLE;
        }
        if (style_shop && y >= 45U && y < 100U) {
            if (x < 160U) {
                return x >= 116U ? BYTE_BUDDY_TOUCH_STYLE_BODY_BUY
                                 : BYTE_BUDDY_TOUCH_STYLE_BODY_SELECT;
            }
            return x >= 276U ? BYTE_BUDDY_TOUCH_STYLE_EYES_BUY
                             : BYTE_BUDDY_TOUCH_STYLE_EYES_SELECT;
        }
        if (style_shop && y >= 105U && y < 160U) {
            if (x < 160U) {
                return x >= 116U ? BYTE_BUDDY_TOUCH_STYLE_HORNS_BUY
                                 : BYTE_BUDDY_TOUCH_STYLE_HORNS_SELECT;
            }
            return x >= 276U ? BYTE_BUDDY_TOUCH_STYLE_TRAIL_BUY
                             : BYTE_BUDDY_TOUCH_STYLE_TRAIL_SELECT;
        }
        if (style_shop && y >= 164U) {
            return x < 160U ? BYTE_BUDDY_TOUCH_STYLE_REMIX
                            : BYTE_BUDDY_TOUCH_CLOSE_SHOP;
        }
        if (!style_shop && y >= 45U && y < 100U) {
            return x < 160U ? BYTE_BUDDY_TOUCH_UPGRADE_WINGS
                            : BYTE_BUDDY_TOUCH_UPGRADE_AURA;
        }
        if (!style_shop && y >= 105U && y < 160U) {
            return x < 160U ? BYTE_BUDDY_TOUCH_UPGRADE_NEST
                            : BYTE_BUDDY_TOUCH_UPGRADE_MAGNET;
        }
        return y >= 164U
            ? BYTE_BUDDY_TOUCH_CLOSE_SHOP : BYTE_BUDDY_TOUCH_NONE;
    }
    if (x >= 108U && x < 212U && y >= 42U && y < 136U) {
        return BYTE_BUDDY_TOUCH_DRAGON;
    }
    if (y >= 138U && y < 165U) {
        if (x < 80U) {
            return BYTE_BUDDY_TOUCH_FEED;
        }
        if (x < 160U) {
            return BYTE_BUDDY_TOUCH_PLAY;
        }
        if (x < 240U) {
            return BYTE_BUDDY_TOUCH_CLEAN;
        }
        return BYTE_BUDDY_TOUCH_REST;
    }
    if (y >= 168U) {
        if (x < 146U) {
            return BYTE_BUDDY_TOUCH_SIGNAL_SCAN;
        }
        if (x < 246U) {
            return BYTE_BUDDY_TOUCH_SHOP;
        }
        return BYTE_BUDDY_TOUCH_PREVIEW;
    }
    return BYTE_BUDDY_TOUCH_NONE;
}

byte_buddy_touch_target_t byte_buddy_signal_touch_target(
    uint16_t x, uint16_t y, byte_buddy_signal_view_t view)
{
    if (x < 58U && y < 25U) {
        return BYTE_BUDDY_TOUCH_SIGNAL_BACK;
    }
    if (view == BYTE_BUDDY_SIGNAL_LIST) {
        if (y >= 32U && y < 157U) {
            const unsigned row = (unsigned)(y - 32U) / 25U;
            return (byte_buddy_touch_target_t)(
                BYTE_BUDDY_TOUCH_SIGNAL_ROW_0 + row);
        }
        if (y >= 162U) {
            if (x < 64U) {
                return BYTE_BUDDY_TOUCH_SIGNAL_PREVIOUS;
            }
            if (x >= 256U) {
                return BYTE_BUDDY_TOUCH_SIGNAL_NEXT;
            }
            return BYTE_BUDDY_TOUCH_SIGNAL_SCAN;
        }
        return BYTE_BUDDY_TOUCH_NONE;
    }
    if (view == BYTE_BUDDY_SIGNAL_TRACKER) {
        if (y >= 162U) {
            return x < 196U ? BYTE_BUDDY_TOUCH_SIGNAL_TRACK
                            : BYTE_BUDDY_TOUCH_SIGNAL_BATTLE;
        }
        return BYTE_BUDDY_TOUCH_NONE;
    }
    if (view == BYTE_BUDDY_SIGNAL_BATTLE && y >= 162U) {
        return x < 196U ? BYTE_BUDDY_TOUCH_SIGNAL_STRIKE
                        : BYTE_BUDDY_TOUCH_SIGNAL_GUARD;
    }
    return BYTE_BUDDY_TOUCH_NONE;
}

uint8_t byte_buddy_signal_page_count(uint8_t result_count)
{
    const uint8_t bounded = result_count > P4_GAME_SIGNAL_MAX_RESULTS
        ? P4_GAME_SIGNAL_MAX_RESULTS : result_count;
    return bounded == 0U ? 1U : (uint8_t)(
        (bounded + BYTE_BUDDY_SIGNAL_PAGE_ROWS - 1U) /
        BYTE_BUDDY_SIGNAL_PAGE_ROWS);
}

uint8_t byte_buddy_signal_clamp_page(
    uint8_t page, uint8_t result_count)
{
    const uint8_t pages = byte_buddy_signal_page_count(result_count);
    return page < pages ? page : (uint8_t)(pages - 1U);
}

uint8_t byte_buddy_signal_page_index(
    uint8_t page, uint8_t row, uint8_t result_count)
{
    const uint8_t bounded = result_count > P4_GAME_SIGNAL_MAX_RESULTS
        ? P4_GAME_SIGNAL_MAX_RESULTS : result_count;
    if (page >= byte_buddy_signal_page_count(bounded) ||
        row >= BYTE_BUDDY_SIGNAL_PAGE_ROWS) {
        return UINT8_MAX;
    }
    const unsigned index =
        (unsigned)page * BYTE_BUDDY_SIGNAL_PAGE_ROWS + row;
    return index < bounded ? (uint8_t)index : UINT8_MAX;
}

byte_buddy_signal_battle_pattern_t byte_buddy_signal_battle_pattern(
    byte_buddy_signal_genome_t genome)
{
    return ((genome.core ^ genome.halo ^ genome.sigil ^ genome.aura ^
             genome.hue) & 1U) == 0U
        ? BYTE_BUDDY_BATTLE_PULSE_RUSH
        : BYTE_BUDDY_BATTLE_RESONANCE_WEAVE;
}

uint8_t byte_buddy_signal_weave_node_index(
    byte_buddy_signal_genome_t genome, uint8_t step)
{
    static const uint8_t routes[4][6] = {
        {0U, 1U, 2U, 3U, 4U, 5U},
        {0U, 2U, 4U, 5U, 3U, 1U},
        {0U, 3U, 1U, 4U, 2U, 5U},
        {0U, 1U, 3U, 5U, 4U, 2U},
    };
    const uint8_t route = (uint8_t)(
        (genome.core + 2U * genome.halo + genome.sigil) & 3U);
    uint8_t position = (uint8_t)(step % 6U);
    if (((genome.aura ^ genome.rarity) & 1U) != 0U) {
        position = (uint8_t)(5U - position);
    }
    const uint8_t rotation = (uint8_t)(
        (genome.hue + genome.aura) % 6U);
    return (uint8_t)((routes[route][position] + rotation) % 6U);
}

byte_buddy_signal_weave_node_t byte_buddy_signal_weave_node(
    byte_buddy_signal_genome_t genome, uint8_t step)
{
    static const byte_buddy_signal_weave_node_t nodes[6] = {
        {140U, 47U}, {180U, 47U}, {202U, 80U},
        {180U, 113U}, {140U, 113U}, {118U, 80U},
    };
    return nodes[byte_buddy_signal_weave_node_index(genome, step)];
}

byte_buddy_signal_weave_rules_t byte_buddy_signal_weave_rules(
    byte_buddy_signal_genome_t genome,
    uint8_t strength, uint8_t magnet_level)
{
    const uint8_t rarity = genome.rarity < 4U ? genome.rarity : 3U;
    const uint8_t bounded_strength = strength < 101U ? strength : 100U;
    const uint8_t bounded_magnet = magnet_level < 4U
        ? magnet_level : 3U;
    return (byte_buddy_signal_weave_rules_t){
        .required_locks = (uint8_t)(4U + rarity),
        .touch_radius = (uint8_t)(17U + 2U * bounded_magnet),
        .hold_ms = (uint16_t)(600U + 2U * bounded_strength +
                              40U * rarity),
    };
}

uint32_t byte_buddy_signal_weave_charge(
    uint32_t charge_units, uint32_t elapsed_ms,
    bool inside_target, uint32_t threshold_units)
{
    if (threshold_units == 0U) {
        return 0U;
    }
    if (!inside_target) {
        return charge_units > elapsed_ms
            ? charge_units - elapsed_ms : 0U;
    }
    const uint32_t added = elapsed_ms > UINT32_MAX / 2U
        ? UINT32_MAX : elapsed_ms * 2U;
    if (charge_units >= threshold_units ||
        added >= threshold_units - charge_units) {
        return threshold_units;
    }
    return charge_units + added;
}

byte_buddy_signal_profile_t byte_buddy_signal_profile(
    uint64_t token, int8_t rssi_dbm)
{
    int strength = ((int)rssi_dbm + 100) * 100 / 70;
    if (strength < 0) {
        strength = 0;
    } else if (strength > 100) {
        strength = 100;
    }
    const unsigned roll = (unsigned)(token & UINT64_C(0x3ff));
    const uint8_t rarity = roll < 8U ? 4U : roll < 64U ? 3U
        : roll < 256U ? 2U : 1U;
    unsigned reward = 1U + rarity + (unsigned)strength / 18U;
    unsigned hp = 6U + (unsigned)strength / 5U + (unsigned)rarity * 2U;
    if (reward > UINT8_MAX) {
        reward = UINT8_MAX;
    }
    if (hp > UINT8_MAX) {
        hp = UINT8_MAX;
    }
    const unsigned sigil_bits =
        (unsigned)((token >> 27U) & UINT64_C(3));
    unsigned element_bits = sigil_bits == 0U
        ? (unsigned)((token >> 10U) & UINT64_C(3))
        : sigil_bits - 1U;
    if (element_bits >= 3U) {
        element_bits = 0U;
    }
    return (byte_buddy_signal_profile_t){
        .element = (byte_buddy_element_t)(
            BYTE_BUDDY_ELEMENT_FIRE + element_bits),
        .rarity = rarity,
        .hue = (uint8_t)((token >> 18U) & UINT64_C(7)),
        .strength = (uint8_t)strength,
        .reward_coins = (uint8_t)reward,
        .battle_hp = (uint8_t)hp,
    };
}

uint16_t byte_buddy_signal_recipe_id(
    uint8_t core, uint8_t halo, uint8_t sigil,
    uint8_t aura, uint8_t hue, uint8_t rarity)
{
    uint16_t recipe = core < 4U ? core : 0U;
    recipe = (uint16_t)(recipe * 4U + (halo < 4U ? halo : 0U));
    recipe = (uint16_t)(recipe * 4U + (sigil < 4U ? sigil : 0U));
    recipe = (uint16_t)(recipe * 4U + (aura < 4U ? aura : 0U));
    recipe = (uint16_t)(recipe * 8U + (hue < 8U ? hue : 0U));
    return (uint16_t)(recipe * 4U + (rarity < 4U ? rarity : 0U));
}

byte_buddy_signal_genome_t byte_buddy_signal_genome(uint64_t token)
{
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(token, -100);
    const uint8_t core = (uint8_t)((token >> 21U) & UINT64_C(3));
    const uint8_t halo = (uint8_t)((token >> 23U) & UINT64_C(3));
    const uint8_t aura = (uint8_t)((token >> 25U) & UINT64_C(3));
    const uint8_t sigil = (uint8_t)((token >> 27U) & UINT64_C(3));
    const uint8_t rarity = (uint8_t)(profile.rarity - 1U);
    return (byte_buddy_signal_genome_t){
        .core = core,
        .halo = halo,
        .sigil = sigil,
        .aura = aura,
        .hue = profile.hue,
        .rarity = rarity,
        .recipe_id = byte_buddy_signal_recipe_id(
            core, halo, sigil, aura, profile.hue, rarity),
    };
}

uint8_t byte_buddy_signal_channel_family(uint8_t channel)
{
    if (channel == 0U) {
        return UINT8_MAX;
    }
    if (channel <= 5U) {
        return 0U;
    }
    if (channel <= 10U) {
        return 1U;
    }
    if (channel <= 14U) {
        return 2U;
    }
    return 3U;
}

uint8_t byte_buddy_signal_habitat_id(uint8_t channel, uint8_t flags)
{
    const uint8_t family = byte_buddy_signal_channel_family(channel);
    const uint8_t bounded_family = family == UINT8_MAX ? 4U : family;
    const uint8_t environment = (uint8_t)(
        ((flags & P4_GAME_SIGNAL_PROTECTED) != 0U ? 1U : 0U) |
        ((flags & P4_GAME_SIGNAL_HIDDEN) != 0U ? 2U : 0U));
    return (uint8_t)(bounded_family + environment * 5U);
}

uint32_t byte_buddy_signal_form_id(
    uint16_t genome_recipe_id, uint8_t channel, uint8_t flags)
{
    const uint16_t bounded_recipe = genome_recipe_id < 8192U
        ? genome_recipe_id : 0U;
    return (uint32_t)bounded_recipe * SIGNAL_HABITAT_VARIANTS +
        byte_buddy_signal_habitat_id(channel, flags);
}

static uint64_t avalanche_lineage_value(uint64_t value)
{
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

uint64_t byte_buddy_signal_lineage_contribution(
    uint64_t token, uint8_t channel, uint8_t flags)
{
    const uint8_t family = byte_buddy_signal_channel_family(channel);
    uint64_t context = family == UINT8_MAX
        ? UINT64_C(0) : (uint64_t)family + UINT64_C(1);
    if ((flags & P4_GAME_SIGNAL_PROTECTED) != 0U) {
        context |= UINT64_C(1) << 8U;
    }
    if ((flags & P4_GAME_SIGNAL_HIDDEN) != 0U) {
        context |= UINT64_C(1) << 9U;
    }
    const uint64_t mixed = avalanche_lineage_value(
        token ^ UINT64_C(0x9e3779b97f4a7c15) ^
        context * UINT64_C(0xd6e8feb86659fd93));
    return mixed == 0U ? UINT64_C(0xa0761d6478bd642f) : mixed;
}

static void add_bounded_vote(uint8_t *vote, uint8_t amount)
{
    if (vote == NULL) {
        return;
    }
    *vote = *vote > UINT8_MAX - amount
        ? UINT8_MAX : (uint8_t)(*vote + amount);
}

void byte_buddy_lineage_add(
    byte_buddy_lineage_genes_t *genes,
    byte_buddy_signal_genome_t genome,
    uint8_t channel, uint8_t flags)
{
    if (genes == NULL || genome.core >= 4U || genome.halo >= 4U ||
        genome.sigil >= 4U || genome.aura >= 4U || genome.hue >= 8U ||
        genome.rarity >= 4U) {
        return;
    }
    genes->part_mask |= (uint16_t)(UINT16_C(1) << genome.core);
    genes->part_mask |= (uint16_t)(
        UINT16_C(1) << (4U + genome.halo));
    genes->part_mask |= (uint16_t)(
        UINT16_C(1) << (8U + genome.sigil));
    genes->part_mask |= (uint16_t)(
        UINT16_C(1) << (12U + genome.aura));
    genes->hue_mask |= (uint8_t)(UINT8_C(1) << genome.hue);
    genes->rarity_mask |= (uint8_t)(UINT8_C(1) << genome.rarity);
    const uint8_t family = byte_buddy_signal_channel_family(channel);
    if (family != UINT8_MAX) {
        genes->channel_mask |= (uint8_t)(UINT8_C(1) << family);
    }
    if ((flags & P4_GAME_SIGNAL_PROTECTED) != 0U &&
        genes->protected_count != UINT8_MAX) {
        ++genes->protected_count;
    }
    if ((flags & P4_GAME_SIGNAL_HIDDEN) != 0U &&
        genes->hidden_count != UINT8_MAX) {
        ++genes->hidden_count;
    }
    const uint8_t weight = (uint8_t)(genome.rarity + 1U);
    add_bounded_vote(&genes->core_votes[genome.core], weight);
    add_bounded_vote(&genes->halo_votes[genome.halo], weight);
    add_bounded_vote(&genes->sigil_votes[genome.sigil], weight);
    add_bounded_vote(&genes->aura_votes[genome.aura], weight);
    add_bounded_vote(&genes->hue_votes[genome.hue], weight);
}

static uint8_t lineage_popcount(uint32_t value)
{
    uint8_t count = 0U;
    while (value != 0U) {
        count = (uint8_t)(count + (uint8_t)(value & UINT32_C(1)));
        value >>= 1U;
    }
    return count;
}

static uint8_t lineage_u64_remainder(uint64_t value, uint8_t divisor)
{
    if (divisor == 0U) {
        return 0U;
    }
    const uint32_t words[2] = {
        (uint32_t)(value >> 32U),
        (uint32_t)value,
    };
    uint8_t remainder = 0U;
    for (uint8_t word = 0U; word < 2U; ++word) {
        for (uint8_t byte = 0U; byte < 4U; ++byte) {
            const uint8_t shift = (uint8_t)((3U - byte) * 8U);
            const uint16_t dividend = (uint16_t)(
                ((uint16_t)remainder << 8U) |
                (uint16_t)((words[word] >> shift) & UINT32_C(0xff)));
            remainder = (uint8_t)(dividend % divisor);
        }
    }
    return remainder;
}

static uint8_t lineage_vote_winner(
    const uint8_t *votes, uint8_t count, uint64_t tie_break)
{
    if (votes == NULL || count == 0U) {
        return 0U;
    }
    uint8_t maximum = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if (votes[index] > maximum) {
            maximum = votes[index];
        }
    }
    uint8_t tied = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if (votes[index] == maximum) {
            ++tied;
        }
    }
    uint8_t choice = lineage_u64_remainder(tie_break, tied);
    for (uint8_t index = 0U; index < count; ++index) {
        if (votes[index] == maximum) {
            if (choice == 0U) {
                return index;
            }
            --choice;
        }
    }
    return 0U;
}

byte_buddy_signal_lineage_t byte_buddy_signal_lineage(
    uint64_t entropy, uint8_t unique_count,
    const byte_buddy_lineage_genes_t *genes)
{
    if (genes == NULL) {
        return (byte_buddy_signal_lineage_t){0};
    }
    const uint8_t part_diversity = lineage_popcount(genes->part_mask);
    const uint8_t hue_diversity = lineage_popcount(genes->hue_mask);
    const uint8_t rarity_diversity = lineage_popcount(
        (uint32_t)(genes->rarity_mask & UINT8_C(0x0f)));
    const uint8_t diversity = (uint8_t)(
        part_diversity + hue_diversity + rarity_diversity);
    const uint8_t families = lineage_popcount(
        (uint32_t)(genes->channel_mask & UINT8_C(0x0f)));
    uint8_t tier = unique_count == 0U
        ? BYTE_BUDDY_LINEAGE_DORMANT : BYTE_BUDDY_LINEAGE_SPARK;
    if (unique_count >= 3U && diversity >= 11U) {
        tier = BYTE_BUDDY_LINEAGE_CREST;
    }
    if (unique_count >= 5U && diversity >= 15U) {
        tier = BYTE_BUDDY_LINEAGE_AURORA;
    }
    if (unique_count >= 8U && diversity >= 19U) {
        tier = BYTE_BUDDY_LINEAGE_ASCENDED;
    }
    if (unique_count >= 12U && diversity >= 22U) {
        tier = BYTE_BUDDY_LINEAGE_MYTHIC;
    }
    uint8_t resonance = BYTE_BUDDY_RESONANCE_NONE;
    if (unique_count >= 16U && diversity >= 24U) {
        resonance = BYTE_BUDDY_RESONANCE_NOVA;
    }
    if (unique_count >= 24U && diversity >= 26U) {
        resonance = BYTE_BUDDY_RESONANCE_GALAXY;
    }
    if ((unique_count >= 32U && diversity >= 27U) ||
        unique_count >= BYTE_BUDDY_SIGNAL_MAX_CONSUMED) {
        resonance = BYTE_BUDDY_RESONANCE_ETERNAL;
    }
    uint8_t adaptations = 0U;
    if (genes->protected_count >= 3U) {
        adaptations |= BYTE_BUDDY_ADAPTATION_SHIELD;
    }
    if (genes->hidden_count != 0U) {
        adaptations |= BYTE_BUDDY_ADAPTATION_PHANTOM;
    }
    if (families == 4U) {
        adaptations |= BYTE_BUDDY_ADAPTATION_WIDEBAND;
    }
    if (hue_diversity >= 6U) {
        adaptations |= BYTE_BUDDY_ADAPTATION_PRISMATIC;
    }
    if (part_diversity >= 14U) {
        adaptations |= BYTE_BUDDY_ADAPTATION_CHIMERA;
    }
    const uint8_t primary_hue = lineage_vote_winner(
        genes->hue_votes, 8U, entropy >> 17U);
    const uint8_t hue_step = (uint8_t)(
        1U + lineage_u64_remainder(entropy >> 45U, 7U));
    return (byte_buddy_signal_lineage_t){
        .tier = tier,
        .resonance = resonance,
        .adaptations = adaptations,
        .family = lineage_vote_winner(
            genes->core_votes, 4U, entropy),
        .halo = lineage_vote_winner(
            genes->halo_votes, 4U, entropy >> 7U),
        .marking = lineage_vote_winner(
            genes->sigil_votes, 4U, entropy >> 13U),
        .aura = lineage_vote_winner(
            genes->aura_votes, 4U, entropy >> 23U),
        .primary_hue = primary_hue,
        .secondary_hue = (uint8_t)((primary_hue + hue_step) & 7U),
        .part_diversity = part_diversity,
        .hue_diversity = hue_diversity,
        .diversity = diversity,
        .channel_families = families,
        .shielded = (adaptations & BYTE_BUDDY_ADAPTATION_SHIELD) != 0U,
        .phantom = (adaptations & BYTE_BUDDY_ADAPTATION_PHANTOM) != 0U,
    };
}

uint8_t byte_buddy_signal_growth_reward(
    uint8_t rarity, uint8_t unique_count)
{
    const uint8_t bounded_rarity = rarity < 1U
        ? 1U : rarity > 4U ? 4U : rarity;
    if (unique_count <= 4U) {
        return (uint8_t)(1U + bounded_rarity / 2U);
    }
    if (unique_count <= 12U && bounded_rarity == 4U) {
        return 2U;
    }
    return 1U;
}

bool byte_buddy_signal_collection_has_room(uint8_t unique_count)
{
    return unique_count < BYTE_BUDDY_SIGNAL_MAX_CONSUMED;
}

static uint8_t increase(uint8_t value, uint8_t amount)
{
    return value > STAT_MAX - amount ? STAT_MAX : (uint8_t)(value + amount);
}

static uint8_t decrease(uint8_t value, uint8_t amount)
{
    return value < amount ? 0U : (uint8_t)(value - amount);
}

static void play_tone(p4_game_context_t *context,
                      uint16_t frequency_hz, uint16_t duration_ms)
{
    (void)p4_game_play_tone(context, frequency_hz, duration_ms,
                            3U, P4_WAVE_TRIANGLE);
}

static void unlock(p4_game_context_t *context, byte_buddy_state_t *state,
                   uint32_t flag, const char *id, const char *title,
                   const char *description)
{
    if ((state->achievement_mask & flag) != 0U) {
        return;
    }
    state->achievement_mask |= flag;
    (void)p4_game_unlock_achievement(context, id, title, description);
}

static void trigger_reaction(byte_buddy_state_t *state,
                             buddy_reaction_t reaction)
{
    state->reaction = (uint8_t)reaction;
    state->reaction_ms = REACTION_DURATION_MS;
}

static byte_buddy_signal_lineage_t current_lineage(
    const byte_buddy_state_t *state)
{
    return byte_buddy_signal_lineage(
        state->signal_entropy, state->signal_consumed_count,
        &state->signal_genes);
}

static const char *lineage_badge_name(
    byte_buddy_signal_lineage_t lineage, size_t *length)
{
    if (lineage.resonance > BYTE_BUDDY_RESONANCE_NONE &&
        lineage.resonance < BYTE_BUDDY_RESONANCE_COUNT) {
        if (length != NULL) {
            *length = s_resonance_name_lengths[lineage.resonance];
        }
        return s_resonance_names[lineage.resonance];
    }
    const uint8_t tier = lineage.tier < BYTE_BUDDY_LINEAGE_TIER_COUNT
        ? lineage.tier : BYTE_BUDDY_LINEAGE_DORMANT;
    if (length != NULL) {
        *length = s_lineage_name_lengths[tier];
    }
    return s_lineage_names[tier];
}

static void update_dragon_traits(byte_buddy_state_t *state)
{
    state->element = (uint8_t)byte_buddy_element_for_nurture(
        state->action_counts[ACTION_FEED],
        state->action_counts[ACTION_PLAY],
        state->action_counts[ACTION_CLEAN],
        state->action_counts[ACTION_REST],
        state->pet_actions);
    state->wing_style = (uint8_t)byte_buddy_wing_style_for_nurture(
        state->action_counts[ACTION_FEED],
        state->action_counts[ACTION_PLAY],
        state->action_counts[ACTION_CLEAN],
        state->action_counts[ACTION_REST],
        state->pet_actions);
    if (state->signal_feeds != 0U) {
        unsigned winner = 0U;
        for (unsigned index = 1U; index < 3U; ++index) {
            if (state->signal_element_votes[index] >
                state->signal_element_votes[winner]) {
                winner = index;
            }
        }
        state->element = (uint8_t)(BYTE_BUDDY_ELEMENT_FIRE + winner);
    }
}

static void advance_growth(p4_game_context_t *context,
                           byte_buddy_state_t *state)
{
    const byte_buddy_stage_t natural_stage =
        byte_buddy_stage_for_interactions(state->care_actions);
    if ((unsigned)natural_stage <= state->stage) {
        return;
    }
    state->stage = (uint8_t)natural_stage;
    trigger_reaction(state, REACTION_GROW);
    play_tone(context, state->stage == BYTE_BUDDY_STAGE_ELEMENTAL
        ? 1047U : 880U, 180U);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
    if (state->stage == BYTE_BUDDY_STAGE_ELEMENTAL) {
        unlock(context, state, ACHIEVEMENT_GROW, "dragon-raised",
               "DRAGON RAISED", "RAISE AN ELEMENTAL DRAGON");
    }
}

static void record_action(p4_game_context_t *context,
                          byte_buddy_state_t *state,
                          buddy_action_t action,
                          buddy_reaction_t reaction)
{
    if ((unsigned)action >= ACTION_COUNT) {
        return;
    }
    if (state->care_credit_cooldown_ms == 0U) {
        if (state->action_counts[action] < UINT16_MAX) {
            ++state->action_counts[action];
        }
        if (state->care_actions < UINT16_MAX) {
            ++state->care_actions;
        }
        state->care_credit_cooldown_ms = CARE_CREDIT_COOLDOWN_MS;
    }
    update_dragon_traits(state);
    trigger_reaction(state, reaction);
    unlock(context, state, ACHIEVEMENT_FIRST_CARE, "first-care",
           "FIRST CARE", "NURTURE YOUR DRAGON EGG");
    if (state->action_counts[ACTION_CLEAN] >= 3U) {
        unlock(context, state, ACHIEVEMENT_CLEAN, "clean-sweep",
               "CLEAN SWEEP", "CLEAN YOUR DRAGON 3 TIMES");
    }
    advance_growth(context, state);
}

static void record_pet(p4_game_context_t *context,
                       byte_buddy_state_t *state)
{
    if (state->care_credit_cooldown_ms == 0U) {
        if (state->pet_actions < UINT16_MAX) {
            ++state->pet_actions;
        }
        if (state->care_actions < UINT16_MAX) {
            ++state->care_actions;
        }
        state->care_credit_cooldown_ms = CARE_CREDIT_COOLDOWN_MS;
    }
    update_dragon_traits(state);
    trigger_reaction(state, REACTION_PET);
    unlock(context, state, ACHIEVEMENT_FIRST_CARE, "first-care",
           "FIRST CARE", "NURTURE YOUR DRAGON EGG");
    advance_growth(context, state);
}

static uint8_t health(const byte_buddy_state_t *state)
{
    const uint16_t total = (uint16_t)(
        (uint16_t)state->hunger + (uint16_t)state->joy +
        (uint16_t)state->hygiene + (uint16_t)state->energy);
    return (uint8_t)(total / 4U);
}

static const char *mood(const byte_buddy_state_t *state)
{
    const uint8_t value = health(state);
    if (value >= 80U) {
        return "RAD";
    }
    if (value >= 50U) {
        return "OK";
    }
    return "NEEDS CARE";
}

static void reset_star(byte_buddy_state_t *state)
{
    const uint32_t seed = (uint32_t)state->coins * 37U +
        (uint32_t)state->care_actions * 19U + state->animation_ms +
        (uint32_t)state->star_spawn_count * 53U;
    state->star_x = (int16_t)(24 + seed % 272U);
    state->star_y_q16 = p4_q16_from_int(MINI_GAME_STAR_START_Y);
    const unsigned ordinal =
        (unsigned)(state->star_spawn_count % UINT8_C(45)) + 1U;
    state->star_spawn_count = (uint8_t)ordinal;
    state->star_kind = ordinal % 9U == 0U ? STAR_KIND_HEART :
        ordinal % 5U == 0U ? STAR_KIND_CALM : STAR_KIND_GOLD;
}

int32_t byte_buddy_catcher_step_q16(
    int32_t current_q16, int32_t target_q16,
    int32_t *velocity_q16, uint32_t elapsed_ms)
{
    if (velocity_q16 == NULL) {
        return current_q16;
    }
    const p4_q16_t minimum = p4_q16_from_int(PET_LEFT_MIN);
    const p4_q16_t maximum = p4_q16_from_int(PET_LEFT_MAX);
    p4_q16_t target = target_q16;
    if (target < minimum) {
        target = minimum;
    } else if (target > maximum) {
        target = maximum;
    }
    const p4_q16_t distance = target - current_q16;
    int64_t desired = (int64_t)distance * 5;
    const p4_q16_t maximum_speed =
        p4_q16_from_int(MINI_GAME_CATCHER_MAX_SPEED);
    if (desired > maximum_speed) {
        desired = maximum_speed;
    } else if (desired < -maximum_speed) {
        desired = -maximum_speed;
    }
    const uint32_t response_ms = elapsed_ms < MINI_GAME_CATCHER_RESPONSE_MS
        ? elapsed_ms : MINI_GAME_CATCHER_RESPONSE_MS;
    const int32_t velocity_delta = (int32_t)desired - *velocity_q16;
    const int32_t velocity_whole =
        velocity_delta / MINI_GAME_CATCHER_RESPONSE_MS;
    const int32_t velocity_remainder =
        velocity_delta % MINI_GAME_CATCHER_RESPONSE_MS;
    *velocity_q16 += velocity_whole * (int32_t)response_ms +
        velocity_remainder * (int32_t)response_ms /
            MINI_GAME_CATCHER_RESPONSE_MS;
    const p4_q16_t previous = current_q16;
    current_q16 = p4_q16_step(current_q16, *velocity_q16, elapsed_ms);
    if ((previous <= target && current_q16 >= target) ||
        (previous >= target && current_q16 <= target)) {
        current_q16 = target;
        *velocity_q16 = 0;
    }
    if (current_q16 < minimum) {
        current_q16 = minimum;
        *velocity_q16 = 0;
    } else if (current_q16 > maximum) {
        current_q16 = maximum;
        *velocity_q16 = 0;
    }
    return current_q16;
}

int32_t byte_buddy_controller_catcher_target_q16(
    int32_t current_q16, int32_t target_q16,
    uint32_t held_buttons, uint32_t elapsed_ms)
{
    const bool left = (held_buttons & P4_BUTTON_LEFT) != 0U;
    const bool right = (held_buttons & P4_BUTTON_RIGHT) != 0U;
    if (left == right) {
        return current_q16;
    }
    const p4_q16_t target_velocity = p4_q16_from_int(
        right ? MINI_GAME_CONTROLLER_TARGET_SPEED
              : -MINI_GAME_CONTROLLER_TARGET_SPEED);
    p4_q16_t target = p4_q16_step(
        target_q16, target_velocity, elapsed_ms);
    const p4_q16_t minimum = p4_q16_from_int(PET_LEFT_MIN);
    const p4_q16_t maximum = p4_q16_from_int(PET_LEFT_MAX);
    if (target < minimum) {
        target = minimum;
    } else if (target > maximum) {
        target = maximum;
    }
    return target;
}

int32_t byte_buddy_controller_cursor_axis_q16(
    int32_t current_q16, bool negative, bool positive,
    uint32_t elapsed_ms, int16_t minimum, int16_t maximum)
{
    if (minimum > maximum) {
        return current_q16;
    }
    const p4_q16_t minimum_q16 = p4_q16_from_int(minimum);
    const p4_q16_t maximum_q16 = p4_q16_from_int(maximum);
    p4_q16_t next = current_q16;
    if (negative != positive) {
        const p4_q16_t velocity = p4_q16_from_int(
            negative ? -SIGNAL_CONTROLLER_CURSOR_SPEED
                     : SIGNAL_CONTROLLER_CURSOR_SPEED);
        next = p4_q16_step(next, velocity, elapsed_ms);
    }
    if (next < minimum_q16) {
        next = minimum_q16;
    } else if (next > maximum_q16) {
        next = maximum_q16;
    }
    return next;
}

static void update_catcher_position(byte_buddy_state_t *state,
                                    uint32_t elapsed_ms)
{
    state->catcher_x_q16 = byte_buddy_catcher_step_q16(
        state->catcher_x_q16, state->catcher_target_x_q16,
        &state->catcher_velocity_q16, elapsed_ms);
}

static void start_play(byte_buddy_state_t *state)
{
    state->mini_game = true;
    state->mini_elapsed_ms = 0U;
    state->play_streak = 0U;
    state->star_effect_ms = 0U;
    state->catcher_x_q16 = p4_q16_from_int(160);
    state->catcher_target_x_q16 = p4_q16_from_int(160);
    state->catcher_velocity_q16 = 0;
    reset_star(state);
}

static void care_for_buddy(p4_game_context_t *context,
                           byte_buddy_state_t *state)
{
    const buddy_action_t action = (buddy_action_t)state->selected_action;
    const uint8_t care_bonus = (uint8_t)(
        state->upgrades[BYTE_BUDDY_UPGRADE_NEST] * 2U);
    buddy_reaction_t reaction = REACTION_IDLE;
    switch (action) {
    case ACTION_FEED:
        state->hunger = increase(state->hunger,
                                 (uint8_t)(24U + care_bonus));
        state->energy = increase(state->energy, 4U);
        play_tone(context, 523U, 100U);
        reaction = REACTION_FEED;
        break;
    case ACTION_PLAY:
        if (state->energy >= 10U) {
            state->energy = decrease(state->energy, 8U);
            start_play(state);
            record_action(context, state, ACTION_PLAY, REACTION_PLAY);
            play_tone(context, 659U, 100U);
            (void)p4_game_audio_effect_play(
                context, &state->audio, P4_GAME_AUDIO_EFFECT_ACTION);
        } else {
            play_tone(context, 196U, 120U);
            (void)p4_game_audio_effect_play(
                context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
        }
        return;
    case ACTION_CLEAN:
        state->hygiene = increase(state->hygiene,
                                  (uint8_t)(28U + care_bonus));
        state->joy = increase(state->joy, 4U);
        play_tone(context, 784U, 80U);
        reaction = REACTION_CLEAN;
        break;
    case ACTION_REST:
        state->energy = increase(state->energy,
                                 (uint8_t)(30U + care_bonus));
        state->joy = increase(state->joy, 6U);
        play_tone(context, 392U, 140U);
        reaction = REACTION_REST;
        break;
    default:
        return;
    }
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_ACTION);
    record_action(context, state, action, reaction);
}

static void apply_decay(byte_buddy_state_t *state, uint32_t elapsed_ms)
{
    state->decay_accumulator_ms += elapsed_ms;
    while (state->decay_accumulator_ms >= DECAY_INTERVAL_MS) {
        state->decay_accumulator_ms -= DECAY_INTERVAL_MS;
        state->hunger = decrease(state->hunger, 1U);
        state->joy = decrease(state->joy, 1U);
        state->hygiene = decrease(state->hygiene, 1U);
        state->energy = decrease(state->energy, 1U);
    }
}

static void update_reaction(byte_buddy_state_t *state, uint32_t elapsed_ms)
{
    if (state->reaction_ms > elapsed_ms) {
        state->reaction_ms -= elapsed_ms;
        return;
    }
    state->reaction_ms = 0U;
    state->reaction = REACTION_IDLE;
}

static void finish_play(byte_buddy_state_t *state)
{
    state->mini_game = false;
    state->joy = increase(state->joy, 8U);
    state->energy = decrease(state->energy, 3U);
}

static bool signal_consumed(const byte_buddy_state_t *state, uint64_t token)
{
    for (size_t index = 0U; index < state->signal_consumed_count; ++index) {
        if (state->signal_consumed[index] == token) {
            return true;
        }
    }
    return false;
}

static const p4_game_signal_t *selected_signal(
    const byte_buddy_state_t *state)
{
    for (size_t index = 0U; index < state->signal_snapshot.count; ++index) {
        if (state->signal_snapshot.results[index].token ==
            state->signal_selected_token) {
            return &state->signal_snapshot.results[index];
        }
    }
    return NULL;
}

static bool signal_is_simulated(const p4_game_signal_t *signal)
{
    return signal != NULL &&
        (signal->flags & P4_GAME_SIGNAL_SIMULATED) != 0U;
}

static uint8_t signal_rows_on_page(const byte_buddy_state_t *state)
{
    const uint8_t page = byte_buddy_signal_clamp_page(
        state->signal_page, state->signal_snapshot.count);
    const uint8_t first = (uint8_t)(
        page * BYTE_BUDDY_SIGNAL_PAGE_ROWS);
    const uint8_t remaining = state->signal_snapshot.count > first
        ? (uint8_t)(state->signal_snapshot.count - first) : 0U;
    return remaining < BYTE_BUDDY_SIGNAL_PAGE_ROWS
        ? remaining : BYTE_BUDDY_SIGNAL_PAGE_ROWS;
}

static void clamp_signal_focus(byte_buddy_state_t *state)
{
    state->signal_page = byte_buddy_signal_clamp_page(
        state->signal_page, state->signal_snapshot.count);
    const uint8_t rows = signal_rows_on_page(state);
    if (rows == 0U) {
        state->signal_focus_row = 0U;
    } else if (state->signal_focus_row >= rows) {
        state->signal_focus_row = (uint8_t)(rows - 1U);
    }
}

static bool select_signal_result(byte_buddy_state_t *state, uint8_t index)
{
    if (index >= state->signal_snapshot.count ||
        signal_consumed(
            state, state->signal_snapshot.results[index].token)) {
        return false;
    }
    state->signal_selected_index = index;
    state->signal_selected_token =
        state->signal_snapshot.results[index].token;
    state->signal_previous_rssi =
        state->signal_snapshot.results[index].rssi_dbm;
    state->signal_trend_db = 0;
    state->signal_samples = 1U;
    state->signal_track_refresh_ms = SIGNAL_TRACK_REFRESH_MS;
    state->signal_view = BYTE_BUDDY_SIGNAL_TRACKER;
    return true;
}

static bool request_signal_scan(p4_game_context_t *context,
                                byte_buddy_state_t *state,
                                uint64_t focus_token)
{
    const bool service_available = context != NULL &&
        context->services != NULL &&
        (context->services->available_capabilities &
         P4_GAME_CAP_SIGNAL_SCAN) != 0U;
    if (!service_available) {
        state->signal_snapshot = (p4_game_signal_snapshot_t){
            .status = P4_GAME_SIGNAL_UNAVAILABLE,
        };
        state->signal_request_busy_ms = 0U;
        return false;
    }
    if (state->signal_request_busy_ms != 0U) {
        return false;
    }
    if (focus_token != 0U) {
        state->signal_track_refresh_ms = 0U;
    }
    if (!p4_game_request_signal_scan(context, focus_token)) {
        state->signal_request_busy_ms = SIGNAL_REQUEST_BUSY_MS;
        return false;
    }
    state->signal_request_busy_ms = 0U;
    state->signal_snapshot.status = P4_GAME_SIGNAL_SCANNING;
    if (focus_token == 0U) {
        state->signal_snapshot.count = 0U;
    }
    return true;
}

static void poll_signal_scan(p4_game_context_t *context,
                             byte_buddy_state_t *state)
{
    p4_game_signal_snapshot_t snapshot;
    if (!p4_game_read_signal_scan(context, &snapshot)) {
        return;
    }
    if (snapshot.generation == state->signal_generation &&
        snapshot.status == state->signal_snapshot.status) {
        return;
    }
    const p4_game_signal_t *const previous = selected_signal(state);
    const int8_t previous_rssi = previous == NULL
        ? state->signal_previous_rssi : previous->rssi_dbm;
    state->signal_snapshot = snapshot;
    state->signal_generation = snapshot.generation;
    state->signal_request_busy_ms = 0U;
    if (snapshot.status == P4_GAME_SIGNAL_READY) {
        clamp_signal_focus(state);
    }
    for (size_t index = 0U; index < snapshot.count; ++index) {
        if (snapshot.results[index].token == state->signal_selected_token) {
            state->signal_selected_index = (uint8_t)index;
            const int difference = (int)snapshot.results[index].rssi_dbm -
                (int)previous_rssi;
            state->signal_trend_db = (int8_t)(
                difference > INT8_MAX ? INT8_MAX :
                difference < INT8_MIN ? INT8_MIN : difference);
            state->signal_previous_rssi =
                snapshot.results[index].rssi_dbm;
            if (state->signal_samples != UINT8_MAX) {
                ++state->signal_samples;
            }
            break;
        }
    }
}

static void return_to_signal_list(byte_buddy_state_t *state)
{
    state->signal_view = BYTE_BUDDY_SIGNAL_LIST;
    for (size_t index = 0U; index < state->signal_snapshot.count; ++index) {
        if (state->signal_snapshot.results[index].token ==
            state->signal_selected_token) {
            state->signal_selected_index = (uint8_t)index;
            state->signal_page = (uint8_t)(
                index / BYTE_BUDDY_SIGNAL_PAGE_ROWS);
            state->signal_focus_row = (uint8_t)(
                index % BYTE_BUDDY_SIGNAL_PAGE_ROWS);
            return;
        }
    }
    state->signal_page = byte_buddy_signal_clamp_page(
        state->signal_page, state->signal_snapshot.count);
    clamp_signal_focus(state);
}

static void update_signal_tracking(p4_game_context_t *context,
                                   byte_buddy_state_t *state,
                                   uint32_t elapsed_ms)
{
    if (state->signal_view != BYTE_BUDDY_SIGNAL_TRACKER) {
        return;
    }
    state->signal_track_refresh_ms =
        state->signal_track_refresh_ms > UINT32_MAX - elapsed_ms
            ? UINT32_MAX
            : state->signal_track_refresh_ms + elapsed_ms;
    if (state->signal_snapshot.status != P4_GAME_SIGNAL_SCANNING &&
        state->signal_request_busy_ms == 0U &&
        state->signal_track_refresh_ms >= SIGNAL_TRACK_REFRESH_MS) {
        (void)request_signal_scan(
            context, state, state->signal_selected_token);
    }
}

static void open_signal_hunt(p4_game_context_t *context,
                             byte_buddy_state_t *state)
{
    state->signal_hunt = true;
    state->signal_view = BYTE_BUDDY_SIGNAL_LIST;
    state->signal_page = 0U;
    state->signal_focus_row = 0U;
    state->signal_selected_token = 0U;
    (void)request_signal_scan(context, state, 0U);
}

static void start_signal_battle(p4_game_context_t *context,
                                byte_buddy_state_t *state)
{
    const p4_game_signal_t *const signal = selected_signal(state);
    if (signal == NULL || signal->rssi_dbm < SIGNAL_HUNT_UNLOCK_RSSI ||
        signal_consumed(state, signal->token) ||
        !byte_buddy_signal_collection_has_room(
            state->signal_consumed_count)) {
        play_tone(context, 196U, 100U);
        return;
    }
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(signal->token, signal->rssi_dbm);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(signal->token);
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    const byte_buddy_lineage_battle_traits_t battle_traits =
        byte_buddy_lineage_battle_traits(lineage);
    state->signal_view = BYTE_BUDDY_SIGNAL_BATTLE;
    state->signal_battle_hp = profile.battle_hp;
    state->signal_battle_max_hp = profile.battle_hp;
    state->signal_battle_pattern = (uint8_t)
        byte_buddy_signal_battle_pattern(genome);
    if (state->signal_battle_pattern ==
            BYTE_BUDDY_BATTLE_RESONANCE_WEAVE) {
        const byte_buddy_signal_weave_node_t first =
            byte_buddy_signal_weave_node(genome, 0U);
        state->signal_cursor_x_q16 = p4_q16_from_int(first.x);
        state->signal_cursor_y_q16 = p4_q16_from_int(first.y);
    }
    state->signal_weave_step = 0U;
    state->signal_weave_settle_ms = 0U;
    state->signal_weave_charge_units = 0U;
    state->signal_battle_elapsed_ms = 0U;
    state->signal_battle_bonus_ms = battle_traits.start_time_ms;
    state->signal_guard_charges = (uint8_t)(
        2U + state->upgrades[BYTE_BUDDY_UPGRADE_NEST] +
        battle_traits.guard_charges);
    state->signal_hit_ms = 0U;
    play_tone(context, 330U, 90U);
}

static void consume_signal(p4_game_context_t *context,
                           byte_buddy_state_t *state,
                           const p4_game_signal_t *signal)
{
    if (signal == NULL || signal_consumed(state, signal->token) ||
        !byte_buddy_signal_collection_has_room(
            state->signal_consumed_count)) {
        return;
    }
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(signal->token, signal->rssi_dbm);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(signal->token);
    state->signal_consumed[state->signal_consumed_count++] = signal->token;
    if (state->signal_feeds != UINT8_MAX) {
        ++state->signal_feeds;
    }
    const unsigned element_index =
        (unsigned)profile.element - BYTE_BUDDY_ELEMENT_FIRE;
    if (element_index < 3U &&
        state->signal_element_votes[element_index] <=
            UINT8_MAX - profile.rarity) {
        state->signal_element_votes[element_index] = (uint8_t)(
            state->signal_element_votes[element_index] + profile.rarity);
    }
    byte_buddy_lineage_add(
        &state->signal_genes, genome, signal->channel, signal->flags);
    state->signal_entropy ^= byte_buddy_signal_lineage_contribution(
        signal->token, signal->channel, signal->flags);
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    state->signal_hue = lineage.primary_hue;
    state->coins = state->coins > UINT16_MAX - profile.reward_coins
        ? UINT16_MAX : (uint16_t)(state->coins + profile.reward_coins);
    const uint16_t growth = byte_buddy_signal_growth_reward(
        profile.rarity, state->signal_consumed_count);
    state->care_actions = state->care_actions > UINT16_MAX - growth
        ? UINT16_MAX : (uint16_t)(state->care_actions + growth);
    state->hunger = increase(state->hunger, 15U);
    state->joy = increase(state->joy, 12U);
    update_dragon_traits(state);
    trigger_reaction(state, REACTION_SIGNAL);
    state->signal_reward_ms = REACTION_DURATION_MS;
    advance_growth(context, state);
    unlock(context, state, ACHIEVEMENT_FIRST_SIGNAL, "first-signal",
           "SIGNAL TAMER", "DEFEAT AND EAT A SIGNAL SEED");
    if (lineage.tier >= BYTE_BUDDY_LINEAGE_AURORA) {
        unlock(context, state, ACHIEVEMENT_SIGNAL_CHORUS,
               "signal-chorus", "SIGNAL CHORUS",
               "BUILD AN AURORA LINEAGE FROM FIVE SIGNALS");
    }
    if (lineage.tier >= BYTE_BUDDY_LINEAGE_MYTHIC) {
        unlock(context, state, ACHIEVEMENT_MYTHIC_LINEAGE,
               "mythic-lineage", "MYTHIC LINEAGE",
               "COMBINE TWELVE DIVERSE SIGNAL GENOMES");
    }
    if (lineage.resonance >= BYTE_BUDDY_RESONANCE_ETERNAL) {
        unlock(context, state, ACHIEVEMENT_ETERNAL_LINEAGE,
               "eternal-lineage", "ETERNAL LINEAGE",
               "COMBINE THIRTY-TWO DIVERSE SIGNAL LINKS");
    }
    play_tone(context, 1047U, 160U);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
}

static void strike_signal(p4_game_context_t *context,
                          byte_buddy_state_t *state)
{
    const p4_game_signal_t *const signal = selected_signal(state);
    if (signal == NULL || state->signal_battle_hp == 0U ||
        state->signal_battle_pattern != BYTE_BUDDY_BATTLE_PULSE_RUSH) {
        return;
    }
    const uint8_t level = byte_buddy_level_for_interactions(
        state->care_actions);
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    const byte_buddy_lineage_battle_traits_t battle_traits =
        byte_buddy_lineage_battle_traits(lineage);
    const unsigned damage = 2U +
        state->upgrades[BYTE_BUDDY_UPGRADE_AURA] + level / 8U +
        battle_traits.strike_damage;
    state->signal_battle_hp = damage >= state->signal_battle_hp
        ? 0U : (uint8_t)(state->signal_battle_hp - damage);
    state->signal_hit_ms = SIGNAL_HIT_DURATION_MS;
    play_tone(context, 740U, 45U);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_IMPACT);
    if (state->signal_battle_hp == 0U) {
        consume_signal(context, state, signal);
        return_to_signal_list(state);
    }
}

static void guard_signal(p4_game_context_t *context,
                         byte_buddy_state_t *state)
{
    if (state->signal_view != BYTE_BUDDY_SIGNAL_BATTLE ||
        state->signal_guard_charges == 0U) {
        return;
    }
    --state->signal_guard_charges;
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    const byte_buddy_lineage_battle_traits_t battle_traits =
        byte_buddy_lineage_battle_traits(lineage);
    const uint32_t guard_bonus = 900U + battle_traits.guard_time_ms;
    state->signal_battle_bonus_ms =
        state->signal_battle_bonus_ms > UINT32_MAX - guard_bonus
            ? UINT32_MAX
            : state->signal_battle_bonus_ms + guard_bonus;
    play_tone(context, 523U, 70U);
}

static p4_game_result_t activate_signal_touch(
    p4_game_context_t *context, byte_buddy_state_t *state,
    const p4_game_point_t *touch)
{
    const byte_buddy_signal_view_t view =
        (byte_buddy_signal_view_t)state->signal_view;
    const byte_buddy_touch_target_t target =
        byte_buddy_signal_touch_target(touch->x, touch->y, view);
    if (target == BYTE_BUDDY_TOUCH_SIGNAL_BACK) {
        if (view == BYTE_BUDDY_SIGNAL_LIST) {
            state->signal_hunt = false;
            state->signal_selected_token = 0U;
        } else {
            if (view == BYTE_BUDDY_SIGNAL_BATTLE) {
                state->signal_view = BYTE_BUDDY_SIGNAL_TRACKER;
            } else {
                return_to_signal_list(state);
            }
        }
        return P4_GAME_CONTINUE;
    }
    if (view == BYTE_BUDDY_SIGNAL_LIST) {
        if (target == BYTE_BUDDY_TOUCH_SIGNAL_SCAN) {
            (void)request_signal_scan(context, state, 0U);
        } else if (target == BYTE_BUDDY_TOUCH_SIGNAL_PREVIOUS) {
            if (state->signal_page != 0U) {
                --state->signal_page;
                clamp_signal_focus(state);
                play_tone(context, 440U, 35U);
            }
        } else if (target == BYTE_BUDDY_TOUCH_SIGNAL_NEXT) {
            const uint8_t pages = byte_buddy_signal_page_count(
                state->signal_snapshot.count);
            if ((uint8_t)(state->signal_page + 1U) < pages) {
                ++state->signal_page;
                clamp_signal_focus(state);
                play_tone(context, 554U, 35U);
            }
        } else if (target >= BYTE_BUDDY_TOUCH_SIGNAL_ROW_0 &&
                   target <= BYTE_BUDDY_TOUCH_SIGNAL_ROW_4) {
            const uint8_t row = (uint8_t)(
                target - BYTE_BUDDY_TOUCH_SIGNAL_ROW_0);
            const uint8_t index = byte_buddy_signal_page_index(
                state->signal_page, row, state->signal_snapshot.count);
            if (index != UINT8_MAX && select_signal_result(state, index)) {
                state->signal_focus_row = row;
            }
        }
        return P4_GAME_CONTINUE;
    }
    if (view == BYTE_BUDDY_SIGNAL_TRACKER) {
        if (target == BYTE_BUDDY_TOUCH_SIGNAL_TRACK) {
            (void)request_signal_scan(
                context, state, state->signal_selected_token);
        } else if (target == BYTE_BUDDY_TOUCH_SIGNAL_BATTLE) {
            start_signal_battle(context, state);
        }
        return P4_GAME_CONTINUE;
    }
    if (target == BYTE_BUDDY_TOUCH_SIGNAL_STRIKE &&
        state->signal_battle_pattern == BYTE_BUDDY_BATTLE_PULSE_RUSH) {
        strike_signal(context, state);
    } else if (target == BYTE_BUDDY_TOUCH_SIGNAL_GUARD) {
        guard_signal(context, state);
    }
    return P4_GAME_CONTINUE;
}

static void update_signal_controller_cursor(
    byte_buddy_state_t *state, const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    if (!state->controller_active ||
        state->signal_view != BYTE_BUDDY_SIGNAL_BATTLE ||
        state->signal_battle_pattern !=
            BYTE_BUDDY_BATTLE_RESONANCE_WEAVE) {
        return;
    }
    state->signal_cursor_x_q16 = byte_buddy_controller_cursor_axis_q16(
        state->signal_cursor_x_q16,
        (input->held & P4_BUTTON_LEFT) != 0U,
        (input->held & P4_BUTTON_RIGHT) != 0U,
        elapsed_ms, SIGNAL_CONTROLLER_CURSOR_MIN_X,
        SIGNAL_CONTROLLER_CURSOR_MAX_X);
    state->signal_cursor_y_q16 = byte_buddy_controller_cursor_axis_q16(
        state->signal_cursor_y_q16,
        (input->held & P4_BUTTON_UP) != 0U,
        (input->held & P4_BUTTON_DOWN) != 0U,
        elapsed_ms, SIGNAL_CONTROLLER_CURSOR_MIN_Y,
        SIGNAL_CONTROLLER_CURSOR_MAX_Y);
}

static void activate_signal_controller(
    p4_game_context_t *context, byte_buddy_state_t *state,
    const p4_game_input_t *input)
{
    const uint32_t pressed = input->pressed;
    const byte_buddy_signal_view_t view =
        (byte_buddy_signal_view_t)state->signal_view;
    if ((pressed & P4_BUTTON_B) != 0U) {
        if (view == BYTE_BUDDY_SIGNAL_LIST) {
            state->signal_hunt = false;
            state->signal_selected_token = 0U;
        } else if (view == BYTE_BUDDY_SIGNAL_TRACKER) {
            return_to_signal_list(state);
        } else {
            state->signal_view = BYTE_BUDDY_SIGNAL_TRACKER;
            state->signal_battle_hp = 0U;
            state->signal_weave_charge_units = 0U;
        }
        return;
    }
    if (view == BYTE_BUDDY_SIGNAL_LIST) {
        if ((pressed & P4_BUTTON_START) != 0U) {
            (void)request_signal_scan(context, state, 0U);
            return;
        }
        const bool previous = (pressed & P4_BUTTON_LEFT) != 0U;
        const bool next = (pressed & P4_BUTTON_RIGHT) != 0U;
        if (previous != next) {
            const uint8_t pages = byte_buddy_signal_page_count(
                state->signal_snapshot.count);
            if (previous && state->signal_page != 0U) {
                --state->signal_page;
                play_tone(context, 440U, 35U);
            } else if (next &&
                       (uint8_t)(state->signal_page + 1U) < pages) {
                ++state->signal_page;
                play_tone(context, 554U, 35U);
            }
            clamp_signal_focus(state);
            return;
        }
        const bool up = (pressed & P4_BUTTON_UP) != 0U;
        const bool down = (pressed & P4_BUTTON_DOWN) != 0U;
        const uint8_t rows = signal_rows_on_page(state);
        if (rows != 0U && up != down) {
            if (up) {
                state->signal_focus_row = state->signal_focus_row == 0U
                    ? (uint8_t)(rows - 1U)
                    : (uint8_t)(state->signal_focus_row - 1U);
            } else {
                state->signal_focus_row = (uint8_t)(
                    (state->signal_focus_row + 1U) % rows);
            }
            play_tone(context, 494U, 30U);
            return;
        }
        if ((pressed & P4_BUTTON_A) != 0U) {
            const uint8_t index = byte_buddy_signal_page_index(
                state->signal_page, state->signal_focus_row,
                state->signal_snapshot.count);
            if (index != UINT8_MAX) {
                (void)select_signal_result(state, index);
            }
        }
        return;
    }
    if (view == BYTE_BUDDY_SIGNAL_TRACKER) {
        if ((pressed & P4_BUTTON_START) != 0U) {
            (void)request_signal_scan(
                context, state, state->signal_selected_token);
        } else if ((pressed & P4_BUTTON_A) != 0U) {
            start_signal_battle(context, state);
        }
        return;
    }
    if ((pressed & P4_BUTTON_START) != 0U) {
        guard_signal(context, state);
    } else if ((pressed & P4_BUTTON_A) != 0U &&
               state->signal_battle_pattern ==
                   BYTE_BUDDY_BATTLE_PULSE_RUSH) {
        strike_signal(context, state);
    }
}

static uint32_t signal_battle_duration_ms(
    const byte_buddy_state_t *state)
{
    return state->signal_battle_pattern ==
            BYTE_BUDDY_BATTLE_RESONANCE_WEAVE
        ? SIGNAL_WEAVE_DURATION_MS : SIGNAL_BATTLE_DURATION_MS;
}

static bool signal_weave_touch_inside(
    const p4_game_point_t *touch,
    byte_buddy_signal_weave_node_t node, uint8_t radius)
{
    if (touch == NULL || touch->y >= 162U) {
        return false;
    }
    const int64_t dx = (int64_t)touch->x - node.x;
    const int64_t dy = (int64_t)touch->y - node.y;
    const int64_t squared_radius = (int64_t)radius * radius;
    return dx * dx + dy * dy <= squared_radius;
}

static void capture_signal_weave_node(
    p4_game_context_t *context, byte_buddy_state_t *state,
    const p4_game_signal_t *signal,
    byte_buddy_signal_weave_rules_t rules)
{
    static const uint16_t notes[6] = {
        440U, 523U, 587U, 659U, 784U, 880U,
    };
    if (state->signal_weave_step >= rules.required_locks) {
        return;
    }
    ++state->signal_weave_step;
    state->signal_weave_charge_units = 0U;
    state->signal_weave_settle_ms = SIGNAL_WEAVE_SETTLE_MS;
    state->signal_hit_ms = SIGNAL_HIT_DURATION_MS;
    if (state->signal_weave_step >= rules.required_locks) {
        state->signal_battle_hp = 0U;
    } else {
        const uint8_t level = byte_buddy_level_for_interactions(
            state->care_actions);
        const byte_buddy_signal_lineage_t lineage = current_lineage(state);
        const byte_buddy_lineage_battle_traits_t battle_traits =
            byte_buddy_lineage_battle_traits(lineage);
        const unsigned base =
            (state->signal_battle_max_hp + rules.required_locks - 1U) /
            rules.required_locks;
        const unsigned damage = base +
            state->upgrades[BYTE_BUDDY_UPGRADE_AURA] / 2U +
            level / 16U + battle_traits.strike_damage / 2U;
        const uint8_t minimum_remaining = (uint8_t)(
            rules.required_locks - state->signal_weave_step);
        const uint8_t damaged = damage >= state->signal_battle_hp
            ? 0U : (uint8_t)(state->signal_battle_hp - damage);
        state->signal_battle_hp = damaged < minimum_remaining
            ? minimum_remaining : damaged;
    }
    play_tone(context, notes[(state->signal_weave_step - 1U) % 6U], 70U);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_IMPACT);
    if (state->signal_battle_hp == 0U) {
        consume_signal(context, state, signal);
        return_to_signal_list(state);
    }
}

static void update_signal_battle(p4_game_context_t *context,
                                 byte_buddy_state_t *state,
                                 const p4_game_point_t *touch,
                                 uint32_t elapsed_ms)
{
    if (state->signal_view != BYTE_BUDDY_SIGNAL_BATTLE) {
        return;
    }
    state->signal_battle_elapsed_ms =
        state->signal_battle_elapsed_ms > UINT32_MAX - elapsed_ms
            ? UINT32_MAX : state->signal_battle_elapsed_ms + elapsed_ms;
    state->signal_hit_ms = state->signal_hit_ms > elapsed_ms
        ? state->signal_hit_ms - elapsed_ms : 0U;
    if (state->signal_battle_pattern ==
        BYTE_BUDDY_BATTLE_RESONANCE_WEAVE) {
        const p4_game_signal_t *const signal = selected_signal(state);
        if (signal != NULL) {
            const byte_buddy_signal_profile_t profile =
                byte_buddy_signal_profile(signal->token, signal->rssi_dbm);
            const byte_buddy_signal_genome_t genome =
                byte_buddy_signal_genome(signal->token);
            const byte_buddy_signal_weave_rules_t rules =
                byte_buddy_signal_weave_rules(
                    genome, profile.strength,
                    state->upgrades[BYTE_BUDDY_UPGRADE_MAGNET]);
            uint32_t charge_elapsed_ms = elapsed_ms;
            if (state->signal_weave_settle_ms != 0U) {
                if (charge_elapsed_ms >= state->signal_weave_settle_ms) {
                    charge_elapsed_ms -= state->signal_weave_settle_ms;
                    state->signal_weave_settle_ms = 0U;
                } else {
                    state->signal_weave_settle_ms = (uint16_t)(
                        state->signal_weave_settle_ms - charge_elapsed_ms);
                    charge_elapsed_ms = 0U;
                }
            }
            if (charge_elapsed_ms != 0U &&
                state->signal_weave_step < rules.required_locks) {
                const byte_buddy_signal_weave_node_t node =
                    byte_buddy_signal_weave_node(
                        genome, state->signal_weave_step);
                const uint32_t threshold_units =
                    (uint32_t)rules.hold_ms * 2U;
                state->signal_weave_charge_units =
                    byte_buddy_signal_weave_charge(
                        state->signal_weave_charge_units,
                        charge_elapsed_ms,
                        signal_weave_touch_inside(
                            touch, node, rules.touch_radius),
                        threshold_units);
                if (state->signal_weave_charge_units >= threshold_units) {
                    capture_signal_weave_node(
                        context, state, signal, rules);
                    if (state->signal_view != BYTE_BUDDY_SIGNAL_BATTLE) {
                        return;
                    }
                }
            }
        }
    }
    const uint32_t limit = signal_battle_duration_ms(state) +
        state->signal_battle_bonus_ms;
    if (state->signal_battle_elapsed_ms >= limit) {
        state->signal_view = BYTE_BUDDY_SIGNAL_TRACKER;
        state->signal_battle_hp = 0U;
        play_tone(context, 196U, 140U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
    }
}

static void update_play(p4_game_context_t *context, byte_buddy_state_t *state,
                        const p4_game_point_t *touch, bool touch_pressed,
                        uint32_t held_buttons, uint32_t elapsed_ms)
{
    if (touch != NULL) {
        const byte_buddy_touch_target_t target = byte_buddy_touch_target(
            touch->x, touch->y, false, false, true);
        if (touch_pressed && target == BYTE_BUDDY_TOUCH_DONE_PLAYING) {
            finish_play(state);
            return;
        }
        if (target == BYTE_BUDDY_TOUCH_MOVE_DRAGON) {
            const int16_t requested = (int16_t)touch->x;
            const int16_t bounded = requested < PET_LEFT_MIN
                ? PET_LEFT_MIN : requested > PET_LEFT_MAX
                    ? PET_LEFT_MAX : requested;
            state->catcher_target_x_q16 = p4_q16_from_int(bounded);
        }
    } else {
        state->catcher_target_x_q16 =
            byte_buddy_controller_catcher_target_q16(
                state->catcher_x_q16, state->catcher_target_x_q16,
                held_buttons, elapsed_ms);
    }
    update_catcher_position(state, elapsed_ms);
    state->mini_elapsed_ms += elapsed_ms;
    state->star_effect_ms = state->star_effect_ms > elapsed_ms
        ? state->star_effect_ms - elapsed_ms : 0U;
    const uint16_t fall_speed = byte_buddy_star_fall_speed(
        state->stage, state->play_streak);
    state->star_y_q16 = p4_q16_step(
        state->star_y_q16, p4_q16_from_int(fall_speed), elapsed_ms);
    const int16_t star_y = (int16_t)p4_q16_to_int_round(
        state->star_y_q16);
    const int16_t catcher_x = (int16_t)p4_q16_to_int_round(
        state->catcher_x_q16);
    if (star_y >= MINI_GAME_CATCH_Y) {
        const int16_t distance = state->star_x > catcher_x
            ? (int16_t)(state->star_x - catcher_x)
            : (int16_t)(catcher_x - state->star_x);
        const int16_t catch_radius = (int16_t)(18 +
            (int16_t)state->upgrades[BYTE_BUDDY_UPGRADE_MAGNET] * 5);
        state->star_effect_x = state->star_x;
        state->star_effect_y = star_y;
        state->star_effect_ms = MINI_GAME_EFFECT_DURATION_MS;
        if (distance <= catch_radius) {
            const uint16_t reward = state->star_kind == STAR_KIND_HEART
                ? 2U : 1U;
            state->coins = state->coins > UINT16_MAX - reward
                ? UINT16_MAX : (uint16_t)(state->coins + reward);
            if (state->play_catches < UINT8_MAX) {
                ++state->play_catches;
            }
            if (state->play_streak < UINT8_MAX) {
                ++state->play_streak;
            }
            if (state->star_kind == STAR_KIND_CALM) {
                state->play_streak = state->play_streak > 2U
                    ? (uint8_t)(state->play_streak - 2U) : 0U;
                state->energy = increase(state->energy, 8U);
            } else if (state->star_kind == STAR_KIND_HEART) {
                state->joy = increase(state->joy, 10U);
            } else {
                state->joy = increase(state->joy, 5U);
            }
            if (state->play_streak > state->play_best_streak) {
                state->play_best_streak = state->play_streak;
            }
            record_action(context, state, ACTION_PLAY, REACTION_PLAY);
            play_tone(context, 988U, 65U);
            (void)p4_game_audio_effect_play(
                context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
        } else {
            state->play_streak = 0U;
            play_tone(context, 220U, 45U);
            (void)p4_game_audio_effect_play(
                context, &state->audio, P4_GAME_AUDIO_EFFECT_IMPACT);
        }
        reset_star(state);
    }
    if (state->play_catches >= 3U) {
        unlock(context, state, ACHIEVEMENT_PLAY, "star-catcher",
               "STAR CATCHER", "CATCH 3 STARS WITH BUDDY");
    }
    if (state->mini_elapsed_ms >= MINI_GAME_DURATION_MS) {
        finish_play(state);
    }
}

static void try_upgrade(p4_game_context_t *context,
                        byte_buddy_state_t *state,
                        byte_buddy_upgrade_t upgrade)
{
    if ((unsigned)upgrade >= BYTE_BUDDY_UPGRADE_COUNT) {
        return;
    }
    const uint8_t level = state->upgrades[upgrade];
    const uint16_t cost = byte_buddy_upgrade_cost(level);
    if (cost == UINT16_MAX || state->coins < cost) {
        play_tone(context, 196U, 100U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
        return;
    }
    state->coins = (uint16_t)(state->coins - cost);
    state->upgrades[upgrade] = (uint8_t)(level + 1U);
    trigger_reaction(state, REACTION_GROW);
    play_tone(context, 1047U, 120U);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
}

static void cycle_style(p4_game_context_t *context,
                        byte_buddy_state_t *state,
                        byte_buddy_style_t style)
{
    if ((unsigned)style >= BYTE_BUDDY_STYLE_COUNT) {
        return;
    }
    const uint8_t choices = (uint8_t)(state->style_unlocked[style] + 1U);
    state->style_selected[style] = (uint8_t)(
        (state->style_selected[style] + 1U) % choices);
    trigger_reaction(state, REACTION_GROW);
    play_tone(context, 784U, 60U);
}

static void try_style_unlock(p4_game_context_t *context,
                             byte_buddy_state_t *state,
                             byte_buddy_style_t style)
{
    if ((unsigned)style >= BYTE_BUDDY_STYLE_COUNT) {
        return;
    }
    const uint8_t unlocked = state->style_unlocked[style];
    const uint16_t cost = byte_buddy_style_cost(style, unlocked);
    if (cost == UINT16_MAX || state->coins < cost) {
        play_tone(context, 196U, 100U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
        return;
    }
    state->coins = (uint16_t)(state->coins - cost);
    state->style_unlocked[style] = (uint8_t)(unlocked + 1U);
    state->style_selected[style] = (uint8_t)(unlocked + 1U);
    trigger_reaction(state, REACTION_GROW);
    play_tone(context, 1047U, 100U);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
}

static void remix_owned_styles(p4_game_context_t *context,
                               byte_buddy_state_t *state)
{
    state->style_mix_count = state->style_mix_count == UINT16_MAX
        ? 1U : (uint16_t)(state->style_mix_count + 1U);
    uint32_t seed = UINT32_C(0x9e3779b9) ^
        (uint32_t)state->style_mix_count * UINT32_C(0x85ebca6b) ^
        (uint32_t)state->care_actions * UINT32_C(0xc2b2ae35) ^
        (uint32_t)state->signal_entropy ^
        (uint32_t)(state->signal_entropy >> 32U);
    for (unsigned style = 0U; style < BYTE_BUDDY_STYLE_COUNT; ++style) {
        seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
        const uint8_t unlocked = state->style_unlocked[style] <
            s_style_max[style] ? state->style_unlocked[style]
                               : s_style_max[style];
        state->style_selected[style] = byte_buddy_remix_choice(
            seed, state->style_selected[style], unlocked);
    }
    trigger_reaction(state, REACTION_GROW);
    play_tone(context, 1175U, 110U);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
}

static p4_game_result_t activate_touch(
    p4_game_context_t *context, byte_buddy_state_t *state,
    const p4_game_point_t *touch)
{
    const byte_buddy_touch_target_t target = byte_buddy_touch_target(
        touch->x, touch->y, state->upgrade_shop,
        state->style_shop, false);
    if (target == BYTE_BUDDY_TOUCH_EXIT) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    if (state->upgrade_shop) {
        switch (target) {
        case BYTE_BUDDY_TOUCH_SHOP_POWER:
            state->style_shop = false;
            break;
        case BYTE_BUDDY_TOUCH_SHOP_STYLE:
            state->style_shop = true;
            break;
        case BYTE_BUDDY_TOUCH_UPGRADE_WINGS:
            try_upgrade(context, state, BYTE_BUDDY_UPGRADE_WINGS);
            break;
        case BYTE_BUDDY_TOUCH_UPGRADE_AURA:
            try_upgrade(context, state, BYTE_BUDDY_UPGRADE_AURA);
            break;
        case BYTE_BUDDY_TOUCH_UPGRADE_NEST:
            try_upgrade(context, state, BYTE_BUDDY_UPGRADE_NEST);
            break;
        case BYTE_BUDDY_TOUCH_UPGRADE_MAGNET:
            try_upgrade(context, state, BYTE_BUDDY_UPGRADE_MAGNET);
            break;
        case BYTE_BUDDY_TOUCH_STYLE_BODY_SELECT:
            cycle_style(context, state, BYTE_BUDDY_STYLE_BODY);
            break;
        case BYTE_BUDDY_TOUCH_STYLE_BODY_BUY:
            try_style_unlock(context, state, BYTE_BUDDY_STYLE_BODY);
            break;
        case BYTE_BUDDY_TOUCH_STYLE_EYES_SELECT:
            cycle_style(context, state, BYTE_BUDDY_STYLE_EYES);
            break;
        case BYTE_BUDDY_TOUCH_STYLE_EYES_BUY:
            try_style_unlock(context, state, BYTE_BUDDY_STYLE_EYES);
            break;
        case BYTE_BUDDY_TOUCH_STYLE_HORNS_SELECT:
            cycle_style(context, state, BYTE_BUDDY_STYLE_HORNS);
            break;
        case BYTE_BUDDY_TOUCH_STYLE_HORNS_BUY:
            try_style_unlock(context, state, BYTE_BUDDY_STYLE_HORNS);
            break;
        case BYTE_BUDDY_TOUCH_STYLE_TRAIL_SELECT:
            cycle_style(context, state, BYTE_BUDDY_STYLE_TRAIL);
            break;
        case BYTE_BUDDY_TOUCH_STYLE_TRAIL_BUY:
            try_style_unlock(context, state, BYTE_BUDDY_STYLE_TRAIL);
            break;
        case BYTE_BUDDY_TOUCH_STYLE_REMIX:
            remix_owned_styles(context, state);
            break;
        case BYTE_BUDDY_TOUCH_CLOSE_SHOP:
            state->upgrade_shop = false;
            state->style_shop = false;
            break;
        default:
            break;
        }
        return P4_GAME_CONTINUE;
    }
    switch (target) {
    case BYTE_BUDDY_TOUCH_DRAGON:
        state->joy = increase(state->joy, (uint8_t)(10U +
            state->upgrades[BYTE_BUDDY_UPGRADE_NEST] * 2U));
        record_pet(context, state);
        play_tone(context, 880U, 70U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_ACTION);
        break;
    case BYTE_BUDDY_TOUCH_FEED:
    case BYTE_BUDDY_TOUCH_PLAY:
    case BYTE_BUDDY_TOUCH_CLEAN:
    case BYTE_BUDDY_TOUCH_REST:
        state->selected_action = (uint8_t)(target - BYTE_BUDDY_TOUCH_FEED);
        care_for_buddy(context, state);
        break;
    case BYTE_BUDDY_TOUCH_SHOP:
        state->upgrade_shop = true;
        state->style_shop = false;
        state->menu_selection = 0U;
        break;
    case BYTE_BUDDY_TOUCH_SIGNAL_SCAN:
        open_signal_hunt(context, state);
        break;
    case BYTE_BUDDY_TOUCH_PREVIEW:
        state->lineage_panel = true;
        break;
    default:
        break;
    }
    return P4_GAME_CONTINUE;
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(byte_buddy_state_t)) {
        return false;
    }
    byte_buddy_state_t *const state = context->state;
    const uint8_t *art_data = s_byte_buddy_builtin_art;
    size_t art_bytes = sizeof(s_byte_buddy_builtin_art);
    uint16_t art_sheets = 0U;
    if (!art_bank_valid(art_data, art_bytes, 6U, &art_sheets)) {
        return false;
    }
    if (context->services != NULL &&
        (context->services->available_capabilities &
         P4_GAME_CAP_STORAGE) != 0U) {
        if (context->services->resource_format_version != 1U ||
            !art_bank_valid(
                context->services->resource_data,
                context->services->resource_bytes,
                DRAGON_EXTENDED_SHEET_COUNT, &art_sheets)) {
            return false;
        }
        art_data = context->services->resource_data;
        art_bytes = context->services->resource_bytes;
    }
    *state = (byte_buddy_state_t){
        .hunger = 72U,
        .joy = 68U,
        .hygiene = 75U,
        .energy = 70U,
        .coins = 4U,
        .catcher_x_q16 = INT32_C(160) * P4_Q16_ONE,
        .catcher_target_x_q16 = INT32_C(160) * P4_Q16_ONE,
        .art_data = art_data,
        .art_bytes = art_bytes,
        .art_sheets = art_sheets,
    };
    play_tone(context, 523U, 90U);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    if (context == NULL || input == NULL || context->state == NULL) {
        return P4_GAME_ERROR;
    }
    byte_buddy_state_t *const state = context->state;
    const uint32_t bounded_elapsed_ms =
        elapsed_ms > P4_GAME_MAX_FRAME_DELTA_MS
            ? P4_GAME_MAX_FRAME_DELTA_MS : elapsed_ms;
    (void)p4_game_audio_effect_service(context, &state->audio);
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    if (state->lineage_panel &&
        (input->pressed & P4_BUTTON_B) != 0U) {
        state->lineage_panel = false;
        state->touch_was_down =
            input->touch_valid && input->touch_count > 0U;
        return P4_GAME_CONTINUE;
    }
    state->animation_ms += bounded_elapsed_ms;
    update_reaction(state, bounded_elapsed_ms);
    state->care_credit_cooldown_ms =
        state->care_credit_cooldown_ms > bounded_elapsed_ms
            ? state->care_credit_cooldown_ms - bounded_elapsed_ms : 0U;
    state->signal_reward_ms = state->signal_reward_ms > bounded_elapsed_ms
        ? state->signal_reward_ms - bounded_elapsed_ms : 0U;
    state->signal_request_busy_ms =
        state->signal_request_busy_ms > bounded_elapsed_ms
            ? state->signal_request_busy_ms - bounded_elapsed_ms : 0U;
    apply_decay(state, bounded_elapsed_ms);
    const bool touch_now = input->touch_valid && input->touch_count > 0U;
    const bool touch_pressed = touch_now && !state->touch_was_down;
    const p4_game_point_t *const touch = touch_now
        ? &input->touches[0] : NULL;
    if (touch_now) {
        state->controller_active = false;
    } else if ((input->held | input->pressed) != 0U) {
        state->controller_active = true;
    }
    if (state->lineage_panel) {
        if (touch_pressed && touch != NULL &&
            ((touch->x < 58U && touch->y < 25U) || touch->y >= 168U)) {
            state->lineage_panel = false;
        }
        state->touch_was_down = touch_now;
        return P4_GAME_CONTINUE;
    }
    const uint8_t signal_view_before_update = state->signal_view;
    p4_game_point_t controller_touch = {0};
    const p4_game_point_t *battle_touch = touch;
    if (state->signal_hunt) {
        poll_signal_scan(context, state);
        update_signal_tracking(context, state, bounded_elapsed_ms);
        if (!touch_now) {
            update_signal_controller_cursor(
                state, input, bounded_elapsed_ms);
            if (state->controller_active &&
                state->signal_view == BYTE_BUDDY_SIGNAL_BATTLE &&
                state->signal_battle_pattern ==
                    BYTE_BUDDY_BATTLE_RESONANCE_WEAVE &&
                (input->held & P4_BUTTON_A) != 0U) {
                controller_touch = (p4_game_point_t){
                    .x = (uint16_t)p4_q16_to_int_round(
                        state->signal_cursor_x_q16),
                    .y = (uint16_t)p4_q16_to_int_round(
                        state->signal_cursor_y_q16),
                };
                battle_touch = &controller_touch;
            }
        }
        update_signal_battle(
            context, state, battle_touch, bounded_elapsed_ms);
    }
    const bool signal_view_changed_during_update =
        state->signal_hunt &&
        state->signal_view != signal_view_before_update;
    if (state->mini_game) {
        if ((input->pressed & P4_BUTTON_B) != 0U) {
            finish_play(state);
            state->touch_was_down = touch_now;
            return P4_GAME_CONTINUE;
        }
        if (touch_pressed && touch != NULL &&
            byte_buddy_touch_target(
                touch->x, touch->y, false, false, true) ==
                BYTE_BUDDY_TOUCH_EXIT) {
            state->touch_was_down = touch_now;
            return P4_GAME_EXIT_TO_LAUNCHER;
        }
        update_play(context, state, touch, touch_pressed,
                    input->held, bounded_elapsed_ms);
        state->touch_was_down = touch_now;
        return P4_GAME_CONTINUE;
    }
    if (state->signal_hunt) {
        if (!signal_view_changed_during_update &&
            touch_pressed && touch != NULL) {
            const p4_game_result_t result = activate_signal_touch(
                context, state, touch);
            state->touch_was_down = touch_now;
            return result;
        }
        if (!signal_view_changed_during_update && !touch_now &&
            input->pressed != 0U) {
            activate_signal_controller(context, state, input);
        }
        state->touch_was_down = touch_now;
        return P4_GAME_CONTINUE;
    }
    if (!touch_now && input->pressed != 0U) {
        if (state->upgrade_shop) {
            if ((input->pressed & P4_BUTTON_B) != 0U) {
                state->upgrade_shop = false;
                state->style_shop = false;
            } else if ((input->pressed & P4_BUTTON_LEFT) != 0U) {
                state->style_shop = false;
            } else if ((input->pressed & P4_BUTTON_RIGHT) != 0U) {
                state->style_shop = true;
            } else if ((input->pressed & P4_BUTTON_UP) != 0U) {
                state->menu_selection = state->menu_selection == 0U
                    ? BYTE_BUDDY_STYLE_COUNT - 1U
                    : (uint8_t)(state->menu_selection - 1U);
            } else if ((input->pressed & P4_BUTTON_DOWN) != 0U) {
                state->menu_selection = (uint8_t)(
                    (state->menu_selection + 1U) %
                    BYTE_BUDDY_STYLE_COUNT);
            } else if ((input->pressed & P4_BUTTON_A) != 0U) {
                if (state->style_shop) {
                    cycle_style(context, state,
                                (byte_buddy_style_t)state->menu_selection);
                } else {
                    try_upgrade(
                        context, state,
                        (byte_buddy_upgrade_t)state->menu_selection);
                }
            } else if (state->style_shop &&
                       (input->pressed & P4_BUTTON_START) != 0U) {
                try_style_unlock(
                    context, state,
                    (byte_buddy_style_t)state->menu_selection);
            }
            state->touch_was_down = false;
            return P4_GAME_CONTINUE;
        }
        if ((input->pressed & P4_BUTTON_START) != 0U) {
            state->selected_action = ACTION_PLAY;
            care_for_buddy(context, state);
        } else if ((input->pressed & P4_BUTTON_UP) != 0U) {
            open_signal_hunt(context, state);
        } else if ((input->pressed & P4_BUTTON_DOWN) != 0U) {
            state->upgrade_shop = true;
            state->style_shop = false;
            state->menu_selection = 0U;
        } else if ((input->pressed & P4_BUTTON_B) != 0U) {
            state->lineage_panel = true;
        } else if ((input->pressed & P4_BUTTON_LEFT) != 0U) {
            state->selected_action = state->selected_action == 0U
                ? ACTION_COUNT - 1U
                : (uint8_t)(state->selected_action - 1U);
        } else if ((input->pressed & P4_BUTTON_RIGHT) != 0U) {
            state->selected_action = (uint8_t)(
                (state->selected_action + 1U) % ACTION_COUNT);
        } else if ((input->pressed & P4_BUTTON_A) != 0U) {
            care_for_buddy(context, state);
        }
        state->touch_was_down = false;
        return P4_GAME_CONTINUE;
    }
    if (touch_pressed && touch != NULL) {
        const p4_game_result_t result = activate_touch(context, state, touch);
        state->touch_was_down = touch_now;
        return result;
    }
    state->touch_was_down = touch_now;
    return P4_GAME_CONTINUE;
}

static void draw_bar(p4_game_surface_t *surface, int top,
                     const char *label, uint8_t value, uint16_t color)
{
    p4_draw_text(surface, 8, top, label, UINT16_C(0xBDF7), 1U, 8U);
    p4_draw_rect(surface, 58, top, 72, 7, UINT16_C(0x7BEF));
    p4_draw_fill_rect(surface, 59, top + 1,
                      (int)((uint32_t)value * 70U / STAT_MAX), 5, color);
}

static void draw_number(p4_game_surface_t *surface, int x, int y,
                        uint32_t value, uint16_t color)
{
    char text[11];
    size_t position = sizeof(text) - 1U;
    text[position] = '\0';
    do {
        text[--position] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && position != 0U);
    p4_draw_text(surface, x, y, &text[position], color, 1U,
                 sizeof(text) - position);
}

static unsigned safe_stage(const byte_buddy_state_t *state)
{
    return state->stage < BYTE_BUDDY_STAGE_COUNT
        ? state->stage : BYTE_BUDDY_STAGE_EGG;
}

static unsigned safe_element(const byte_buddy_state_t *state)
{
    return state->element < BYTE_BUDDY_ELEMENT_COUNT
        ? state->element : BYTE_BUDDY_ELEMENT_MYSTERY;
}

static unsigned safe_wing_style(const byte_buddy_state_t *state)
{
    return state->wing_style < BYTE_BUDDY_WING_STYLE_COUNT
        ? state->wing_style : BYTE_BUDDY_WINGS_SHINY;
}

static uint16_t element_color(const byte_buddy_state_t *state)
{
    switch ((byte_buddy_element_t)safe_element(state)) {
    case BYTE_BUDDY_ELEMENT_FIRE:
        return UINT16_C(0xfd20);
    case BYTE_BUDDY_ELEMENT_ICE:
        return UINT16_C(0x07ff);
    case BYTE_BUDDY_ELEMENT_ACID:
        return UINT16_C(0x87e0);
    case BYTE_BUDDY_ELEMENT_MYSTERY:
    default:
        return UINT16_C(0xf81f);
    }
}

static uint16_t signal_color_for_hue(uint8_t hue)
{
    static const uint16_t colors[8] = {
        UINT16_C(0x07ff), UINT16_C(0xf81f), UINT16_C(0xffe0),
        UINT16_C(0x87e0), UINT16_C(0xfd20), UINT16_C(0x781f),
        UINT16_C(0x07f0), UINT16_C(0xfb5f),
    };
    return colors[hue & 7U];
}

static uint16_t signal_color(const byte_buddy_state_t *state)
{
    return signal_color_for_hue(state->signal_hue);
}

static uint16_t trail_color(const byte_buddy_state_t *state)
{
    switch (state->style_selected[BYTE_BUDDY_STYLE_TRAIL]) {
    case 1U:
        return UINT16_C(0xffe0);
    case 2U:
        return UINT16_C(0xf81f);
    case 3U:
        return UINT16_C(0x07ff);
    case 4U:
        return UINT16_C(0x87e0);
    default:
        return element_color(state);
    }
}

static unsigned safe_morph(const byte_buddy_state_t *state)
{
    return (unsigned)byte_buddy_morph_for_nurture(
        state->action_counts[ACTION_FEED],
        state->action_counts[ACTION_PLAY],
        state->action_counts[ACTION_CLEAN],
        state->action_counts[ACTION_REST],
        state->pet_actions);
}

static byte_buddy_battle_stats_t current_battle_stats(
    const byte_buddy_state_t *state)
{
    const byte_buddy_battle_stats_t base = byte_buddy_battle_stats(
        state->action_counts[ACTION_FEED],
        state->action_counts[ACTION_PLAY],
        state->action_counts[ACTION_CLEAN],
        state->action_counts[ACTION_REST],
        state->pet_actions,
        state->upgrades[BYTE_BUDDY_UPGRADE_WINGS],
        state->upgrades[BYTE_BUDDY_UPGRADE_AURA],
        state->upgrades[BYTE_BUDDY_UPGRADE_NEST],
        state->upgrades[BYTE_BUDDY_UPGRADE_MAGNET]);
    return byte_buddy_lineage_battle_stats(
        base, current_lineage(state), &state->signal_genes);
}

static bool rare_morph_unlocked(const byte_buddy_state_t *state)
{
    unsigned levels = 0U;
    for (unsigned index = 0U; index < BYTE_BUDDY_UPGRADE_COUNT; ++index) {
        levels += state->upgrades[index];
    }
    return safe_stage(state) == BYTE_BUDDY_STAGE_ELEMENTAL && levels >= 2U;
}

static unsigned reaction_frame_count(uint32_t reaction_ms,
                                     unsigned frame_count)
{
    const uint32_t elapsed = reaction_ms >= REACTION_DURATION_MS
        ? 0U : REACTION_DURATION_MS - reaction_ms;
    const unsigned frame = (unsigned)(elapsed * frame_count /
                                      REACTION_DURATION_MS);
    return frame >= frame_count ? frame_count - 1U : frame;
}

static unsigned reaction_row(const byte_buddy_state_t *state)
{
    if (state->reaction == REACTION_FEED) {
        return 0U;
    }
    if (state->reaction == REACTION_CLEAN) {
        return 2U;
    }
    if (state->reaction == REACTION_REST) {
        return 3U;
    }
    return 1U;
}

static unsigned elemental_row(const byte_buddy_state_t *state)
{
    const unsigned element = safe_element(state);
    return element == BYTE_BUDDY_ELEMENT_MYSTERY ? 3U : element - 1U;
}

static unsigned hatch_variant(const byte_buddy_state_t *state)
{
    static const uint8_t element_to_hatch[BYTE_BUDDY_ELEMENT_COUNT] = {
        0U, /* mystery -> nebula */
        1U, /* fire -> sungold */
        3U, /* ice -> glacier */
        2U, /* acid -> jade */
    };
    if (safe_wing_style(state) == BYTE_BUDDY_WINGS_SPIKED) {
        return element_to_hatch[safe_element(state)];
    }
    return safe_morph(state);
}

static uint32_t dragon_frame_interval_ms(const byte_buddy_state_t *state)
{
    static const uint16_t intervals[BYTE_BUDDY_STAGE_COUNT] = {
        220U, 190U, 175U, 150U, 165U,
    };
    return intervals[safe_stage(state)];
}

static void dragon_sheet_frame(const byte_buddy_state_t *state,
                               uint32_t animation_ms,
                               uint8_t reaction,
                               uint32_t reaction_ms,
                               unsigned *out_sheet,
                               unsigned *out_frame)
{
    const uint32_t frame_interval_ms = dragon_frame_interval_ms(state);
    const unsigned phase = (unsigned)(
        (animation_ms / frame_interval_ms) % 8U);
    const unsigned phase4 = (unsigned)(
        (animation_ms / frame_interval_ms) % 4U);
    const unsigned stage = safe_stage(state);
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    if (state->art_sheets >= DRAGON_EXTENDED_SHEET_COUNT &&
        stage == BYTE_BUDDY_STAGE_BABY &&
        reaction == REACTION_GROW) {
        *out_sheet = DRAGON_HATCH_TRANSITION_SHEET;
        *out_frame = (1U + reaction_frame_count(reaction_ms, 3U)) * 4U +
            hatch_variant(state);
        return;
    }
    if (state->art_sheets >= DRAGON_EXTENDED_SHEET_COUNT &&
        stage == BYTE_BUDDY_STAGE_EGG) {
        const bool spiked = safe_wing_style(state) ==
            BYTE_BUDDY_WINGS_SPIKED;
        const unsigned variant = spiked ? safe_element(state) :
            safe_morph(state);
        if (state->care_actions < 5U) {
            *out_sheet = spiked ? DRAGON_ELEMENT_EGG_AMBIENT_SHEET :
                DRAGON_MORPH_EGG_AMBIENT_SHEET;
            *out_frame = phase4 * 4U + variant;
            return;
        }
        if (state->care_actions == 5U) {
            *out_sheet = spiked ? DRAGON_ELEMENT_EGG_SHEET :
                DRAGON_MORPH_EGG_SHEET;
            *out_frame = 2U * 4U + variant;
            return;
        }
        *out_sheet = DRAGON_HATCH_TRANSITION_SHEET;
        *out_frame = (state->care_actions >= 7U ? 1U : 0U) * 4U +
            hatch_variant(state);
        return;
    }
    if (state->art_sheets >= DRAGON_EXTENDED_SHEET_COUNT &&
        stage == BYTE_BUDDY_STAGE_BABY) {
        if (reaction != REACTION_IDLE) {
            const unsigned frame = reaction_frame_count(reaction_ms, 8U);
            *out_sheet = frame < 4U ? DRAGON_BABY_REACTION_SHEET :
                DRAGON_BABY_CARE_SHEET;
            *out_frame = reaction_row(state) * 4U + frame % 4U;
            return;
        }
        unsigned row = (unsigned)((animation_ms / 2400U) % 2U);
        if (state->energy < 35U) {
            row = 3U;
        } else if (state->joy >= 80U) {
            row = 2U;
        }
        *out_sheet = DRAGON_BABY_IDLE_SHEET;
        *out_frame = row * 4U + phase4;
        return;
    }
    if (state->art_sheets >= DRAGON_EXTENDED_SHEET_COUNT &&
        stage == BYTE_BUDDY_STAGE_WINGED) {
        const unsigned style = safe_wing_style(state) ==
            BYTE_BUDDY_WINGS_SHINY ? 1U : 0U;
        if (reaction != REACTION_IDLE) {
            const unsigned frame = reaction_frame_count(reaction_ms, 8U);
            if (frame < 4U) {
                *out_sheet = BYTE_BUDDY_STAGE_WINGED;
                *out_frame = (style == 0U ? 0U : 8U) + 4U + frame;
            } else {
                const bool quiet = reaction == REACTION_CLEAN ||
                    reaction == REACTION_REST;
                *out_sheet = DRAGON_WINGED_CARE_SHEET;
                *out_frame = (style + (quiet ? 2U : 0U)) * 4U +
                    frame % 4U;
            }
            return;
        }
        const unsigned active_clip = (unsigned)(
            (animation_ms / 2240U) % 2U);
        *out_sheet = DRAGON_WINGED_IDLE_SHEET;
        *out_frame = (style + active_clip * 2U) * 4U + phase4;
        return;
    }
    if (state->art_sheets >= DRAGON_EXTENDED_SHEET_COUNT &&
        stage == BYTE_BUDDY_STAGE_FLYING) {
        const unsigned style = safe_wing_style(state) ==
            BYTE_BUDDY_WINGS_SHINY ? 1U : 0U;
        if (reaction != REACTION_IDLE) {
            const unsigned frame = reaction_frame_count(reaction_ms, 8U);
            *out_sheet = frame < 4U ? DRAGON_FLIGHT_CYCLE_SHEET :
                DRAGON_FLIGHT_AEROBATICS_SHEET;
            *out_frame = (style + 2U) * 4U + frame % 4U;
            return;
        }
        const bool aerobatics =
            (animation_ms / 2240U) % 2U != 0U;
        *out_sheet = aerobatics ? DRAGON_FLIGHT_AEROBATICS_SHEET :
            DRAGON_FLIGHT_CYCLE_SHEET;
        const unsigned row = aerobatics ? style + 2U : style;
        *out_frame = row * 4U + phase4;
        return;
    }
    if (state->art_sheets >= DRAGON_EXTENDED_SHEET_COUNT &&
        stage == BYTE_BUDDY_STAGE_ELEMENTAL &&
        reaction != REACTION_IDLE) {
        const unsigned frame = reaction_frame_count(reaction_ms, 8U);
        *out_sheet = frame < 4U ? DRAGON_ELEMENT_BREATH_SHEET :
            DRAGON_ELEMENT_IMPACT_SHEET;
        *out_frame = elemental_row(state) * 4U + frame % 4U;
        return;
    }
    if (stage == BYTE_BUDDY_STAGE_ELEMENTAL &&
        rare_morph_unlocked(state)) {
        if (state->art_sheets >= DRAGON_EXTENDED_SHEET_COUNT &&
            lineage.tier >= BYTE_BUDDY_LINEAGE_ASCENDED) {
            *out_sheet = DRAGON_SIGNAL_GENETICS_SHEET;
            *out_frame = phase4 * 4U + lineage.family;
            return;
        }
        *out_sheet = DRAGON_RARE_SHEET;
        *out_frame = safe_morph(state) + 4U * s_frame_sequence[phase];
        return;
    }
    if (state->art_sheets >= DRAGON_EXTENDED_SHEET_COUNT &&
        stage == BYTE_BUDDY_STAGE_ELEMENTAL) {
        if (lineage.tier >= BYTE_BUDDY_LINEAGE_ASCENDED) {
            *out_sheet = DRAGON_SIGNAL_GENETICS_SHEET;
            *out_frame = phase4 * 4U + lineage.family;
            return;
        }
        *out_sheet = DRAGON_ELEMENT_MASTERY_SHEET;
        *out_frame = elemental_row(state) * 4U + phase4;
        return;
    }
    if (stage == BYTE_BUDDY_STAGE_EGG) {
        const unsigned egg_phase = (unsigned)(
            (animation_ms / frame_interval_ms) % 8U);
        *out_sheet = BYTE_BUDDY_STAGE_EGG;
        *out_frame = s_egg_frames[safe_element(state)]
            [s_frame_sequence[egg_phase]];
        return;
    }
    if (stage == BYTE_BUDDY_STAGE_BABY) {
        unsigned row = 0U;
        if (reaction == REACTION_REST) {
            row = 2U;
        } else if (reaction == REACTION_PET ||
                   reaction == REACTION_PLAY ||
                   reaction == REACTION_GROW) {
            row = 3U;
        } else if (reaction != REACTION_IDLE) {
            row = 1U;
        }
        *out_sheet = BYTE_BUDDY_STAGE_BABY;
        *out_frame = row * 4U + s_frame_sequence[phase];
        return;
    }
    if (stage == BYTE_BUDDY_STAGE_WINGED ||
        stage == BYTE_BUDDY_STAGE_FLYING) {
        const unsigned family = safe_wing_style(state) ==
            BYTE_BUDDY_WINGS_SPIKED ? 0U : 8U;
        const unsigned reaction_offset = reaction == REACTION_IDLE
            ? 0U : 4U;
        *out_sheet = stage;
        *out_frame = family + (phase + reaction_offset) % 8U;
        return;
    }
    unsigned element = safe_element(state);
    if (element == BYTE_BUDDY_ELEMENT_MYSTERY) {
        element = 1U + (unsigned)((animation_ms / 1200U) % 3U);
    }
    const unsigned element_index = element - 1U;
    if (reaction != REACTION_IDLE) {
        *out_sheet = BYTE_BUDDY_STAGE_ELEMENTAL;
        *out_frame = s_elemental_reaction_frames[element_index]
            [s_frame_sequence[phase] % 2U];
        return;
    }
    *out_sheet = BYTE_BUDDY_STAGE_ELEMENTAL;
    *out_frame = s_elemental_idle_frames[element_index]
        [safe_wing_style(state)][s_frame_sequence[phase] % 2U];
}

static uint16_t rgb888_to_rgb565(unsigned red, unsigned green, unsigned blue)
{
    return (uint16_t)(((red >> 3U) << 11U) |
                      ((green >> 2U) << 5U) | (blue >> 3U));
}

static void rgb565_to_rgb888(uint16_t value, unsigned *red,
                             unsigned *green, unsigned *blue)
{
    *red = ((value >> 11U) & 31U) * 255U / 31U;
    *green = ((value >> 5U) & 63U) * 255U / 63U;
    *blue = (value & 31U) * 255U / 31U;
}

static uint16_t lift_sprite_color(uint16_t value)
{
    unsigned red = 0U;
    unsigned green = 0U;
    unsigned blue = 0U;
    rgb565_to_rgb888(value, &red, &green, &blue);
    const unsigned luminance = (red * 3U + green * 6U + blue) / 10U;
    if (luminance < 20U || luminance >= 190U) {
        return value;
    }
    const unsigned scale = luminance < 90U ? 150U :
        luminance < 145U ? 125U : 110U;
    red = red * scale / 100U;
    green = green * scale / 100U;
    blue = blue * scale / 100U;
    return rgb888_to_rgb565(red > 255U ? 255U : red,
                            green > 255U ? 255U : green,
                            blue > 255U ? 255U : blue);
}

static uint16_t shade_to(uint16_t value,
                         unsigned target_red,
                         unsigned target_green,
                         unsigned target_blue)
{
    unsigned red = 0U;
    unsigned green = 0U;
    unsigned blue = 0U;
    rgb565_to_rgb888(value, &red, &green, &blue);
    const unsigned luminance = (red * 3U + green * 6U + blue) / 10U;
    unsigned target_luminance =
        (target_red * 3U + target_green * 6U + target_blue) / 10U;
    if (target_luminance == 0U) {
        target_luminance = 1U;
    }
    red = target_red * luminance / target_luminance;
    green = target_green * luminance / target_luminance;
    blue = target_blue * luminance / target_luminance;
    return rgb888_to_rgb565(red > 255U ? 255U : red,
                            green > 255U ? 255U : green,
                            blue > 255U ? 255U : blue);
}

static uint16_t customize_color(const byte_buddy_state_t *state,
                                uint16_t value, int source_x, int source_y)
{
    unsigned red = 0U;
    unsigned green = 0U;
    unsigned blue = 0U;
    rgb565_to_rgb888(value, &red, &green, &blue);
    if (red + green + blue < 95U) {
        return value;
    }
    const unsigned eye = state->style_selected[BYTE_BUDDY_STYLE_EYES];
    if (eye != 0U &&
        source_y < DRAGON_FRAME_HEIGHT * 27 / 48 &&
        source_x >= DRAGON_FRAME_WIDTH * 10 / 48 &&
        source_x < DRAGON_FRAME_WIDTH * 39 / 48 &&
        green > red + 20U && blue > red + 25U) {
        static const uint8_t colors[5][3] = {
            {255U, 210U, 45U}, {255U, 80U, 190U},
            {75U, 240U, 110U}, {170U, 95U, 255U},
            {255U, 55U, 55U},
        };
        return shade_to(value, colors[eye - 1U][0],
                        colors[eye - 1U][1], colors[eye - 1U][2]);
    }
    const unsigned horns = state->style_selected[BYTE_BUDDY_STYLE_HORNS];
    if (horns != 0U &&
        source_y < DRAGON_FRAME_HEIGHT * 22 / 48 &&
        red > green + 35U &&
        red > blue + 25U) {
        static const uint8_t colors[4][3] = {
            {245U, 235U, 200U}, {255U, 195U, 45U},
            {60U, 220U, 245U}, {65U, 60U, 80U},
        };
        return shade_to(value, colors[horns - 1U][0],
                        colors[horns - 1U][1], colors[horns - 1U][2]);
    }
    if (safe_stage(state) != BYTE_BUDDY_STAGE_EGG &&
        blue > green + 12U && red > green + 8U) {
        static const uint8_t body_colors[7][3] = {
            {225U, 55U, 75U}, {235U, 175U, 45U},
            {55U, 190U, 100U}, {75U, 185U, 240U},
            {140U, 235U, 45U}, {240U, 90U, 190U},
            {70U, 65U, 100U},
        };
        const unsigned body = state->style_selected[BYTE_BUDDY_STYLE_BODY];
        if (body != 0U) {
            return shade_to(value, body_colors[body - 1U][0],
                            body_colors[body - 1U][1],
                            body_colors[body - 1U][2]);
        }
        if (state->signal_feeds != 0U) {
            static const uint8_t mutation_colors[8][3] = {
                {45U, 205U, 245U}, {230U, 75U, 220U},
                {235U, 190U, 45U}, {105U, 230U, 70U},
                {245U, 100U, 45U}, {125U, 90U, 240U},
                {45U, 225U, 170U}, {245U, 105U, 175U},
            };
            return shade_to(value,
                            mutation_colors[state->signal_hue & 7U][0],
                            mutation_colors[state->signal_hue & 7U][1],
                            mutation_colors[state->signal_hue & 7U][2]);
        }
        static const uint8_t nurture_colors[BYTE_BUDDY_MORPH_COUNT][3] = {
            {145U, 75U, 225U}, {235U, 175U, 45U},
            {55U, 190U, 100U}, {75U, 185U, 240U},
        };
        const unsigned morph = safe_morph(state);
        return shade_to(value, nurture_colors[morph][0],
                        nurture_colors[morph][1],
                        nurture_colors[morph][2]);
    }
    return value;
}

typedef struct {
    const uint8_t *palette;
    const uint8_t *pixels;
    bool valid;
} dragon_frame_view_t;

static dragon_frame_view_t dragon_frame_view(
    const byte_buddy_state_t *state, unsigned sheet, unsigned frame)
{
    if (state->art_data == NULL || sheet >= state->art_sheets ||
        frame >= DRAGON_FRAMES_PER_SHEET) {
        return (dragon_frame_view_t){0};
    }
    const size_t frame_index = (size_t)sheet * DRAGON_FRAMES_PER_SHEET +
        frame;
    const uint32_t palette_offset = read_u32(
        state->art_data + DRAGON_ART_PALETTE_OFFSET_FIELD);
    const uint32_t pixel_offset = read_u32(
        state->art_data + DRAGON_ART_PIXEL_OFFSET_FIELD);
    return (dragon_frame_view_t){
        .palette = state->art_data + palette_offset +
            frame_index * DRAGON_PALETTE_BYTES,
        .pixels = state->art_data + pixel_offset +
            frame_index * DRAGON_PACKED_FRAME_BYTES,
        .valid = true,
    };
}

static bool dragon_frame_color(const dragon_frame_view_t *view,
                               size_t source_index, uint16_t *out_color)
{
    if (!view->valid || out_color == NULL) {
        return false;
    }
    const uint8_t packed = view->pixels[source_index / 2U];
    const uint8_t palette_index = (source_index & 1U) == 0U
        ? (uint8_t)(packed >> 4U)
        : (uint8_t)(packed & UINT8_C(0x0f));
    if (palette_index == 0U) {
        return false;
    }
    *out_color = read_u16(
        view->palette + (size_t)palette_index * sizeof(uint16_t));
    return true;
}

static bool draw_art_frame_scaled(p4_game_surface_t *surface,
                                  const byte_buddy_state_t *state,
                                  unsigned sheet, unsigned frame,
                                  int center_x, int center_y,
                                  unsigned size)
{
    if (size == 0U || size > DRAGON_FRAME_WIDTH) {
        return false;
    }
    const dragon_frame_view_t view = dragon_frame_view(state, sheet, frame);
    if (!view.valid) {
        return false;
    }
    const int left = center_x - (int)size / 2;
    const int top = center_y - (int)size / 2;
    for (unsigned y = 0U; y < size; ++y) {
        const int destination_y = top + (int)y;
        if (destination_y < 0 || destination_y >= surface->height) {
            continue;
        }
        const unsigned source_y = y * DRAGON_FRAME_HEIGHT / size;
        for (unsigned x = 0U; x < size; ++x) {
            const int destination_x = left + (int)x;
            if (destination_x < 0 || destination_x >= surface->width) {
                continue;
            }
            const unsigned source_x = x * DRAGON_FRAME_WIDTH / size;
            const size_t source_index = (size_t)source_y *
                DRAGON_FRAME_WIDTH + source_x;
            uint16_t color = 0U;
            if (dragon_frame_color(&view, source_index, &color)) {
                surface->pixels[(size_t)destination_y *
                    surface->stride_pixels + (size_t)destination_x] =
                    lift_sprite_color(color);
            }
        }
    }
    return true;
}

static uint16_t signal_layer_color(uint16_t value, uint8_t hue)
{
    const unsigned red = ((unsigned)value >> 11U) & 31U;
    const unsigned green = ((unsigned)value >> 5U) & 63U;
    const unsigned blue = (unsigned)value & 31U;
    const unsigned maximum = red > blue ? red : blue;
    if ((maximum < 7U && green < 14U) ||
        (red >= 24U && green >= 38U && blue <= 13U) ||
        (red >= 27U && green >= 54U && blue >= 27U)) {
        return lift_sprite_color(value);
    }
    const uint16_t target = signal_color_for_hue(hue);
    const unsigned target_red = ((unsigned)target >> 11U) & 31U;
    const unsigned target_green = ((unsigned)target >> 5U) & 63U;
    const unsigned target_blue = (unsigned)target & 31U;
    const unsigned mixed_red = (red * 5U + target_red * 3U) / 8U;
    const unsigned mixed_green = (green * 5U + target_green * 3U) / 8U;
    const unsigned mixed_blue = (blue * 5U + target_blue * 3U) / 8U;
    return lift_sprite_color((uint16_t)(
        mixed_red << 11U | mixed_green << 5U | mixed_blue));
}

static bool draw_signal_layer_scaled(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    unsigned frame, uint8_t hue, int center_x, int center_y,
    unsigned size)
{
    if (size == 0U || size > DRAGON_FRAME_WIDTH) {
        return false;
    }
    const dragon_frame_view_t view = dragon_frame_view(
        state, BYTE_BUDDY_SIGNAL_GENOME_SHEET, frame);
    if (!view.valid) {
        return false;
    }
    const int left = center_x - (int)size / 2;
    const int top = center_y - (int)size / 2;
    for (unsigned y = 0U; y < size; ++y) {
        const int destination_y = top + (int)y;
        if (destination_y < 0 || destination_y >= surface->height) {
            continue;
        }
        const unsigned source_y = y * DRAGON_FRAME_HEIGHT / size;
        for (unsigned x = 0U; x < size; ++x) {
            const int destination_x = left + (int)x;
            if (destination_x < 0 || destination_x >= surface->width) {
                continue;
            }
            const unsigned source_x = x * DRAGON_FRAME_WIDTH / size;
            const size_t source_index = (size_t)source_y *
                DRAGON_FRAME_WIDTH + source_x;
            uint16_t color = 0U;
            if (dragon_frame_color(&view, source_index, &color)) {
                surface->pixels[(size_t)destination_y *
                    surface->stride_pixels + (size_t)destination_x] =
                    signal_layer_color(color, hue);
            }
        }
    }
    return true;
}

static void draw_item_component_icon(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    unsigned frame, int center_x, int center_y, unsigned size)
{
    if (frame < DRAGON_FRAMES_PER_SHEET) {
        (void)draw_art_frame_scaled(
            surface, state, BYTE_BUDDY_ITEM_COMPONENT_SHEET,
            frame, center_x, center_y, size);
    }
}

static uint16_t tween_rgb565(uint16_t first, uint16_t second,
                             unsigned blend)
{
    const unsigned inverse = 256U - blend;
    const unsigned red = ((((unsigned)first >> 11U) & 31U) * inverse +
                          (((unsigned)second >> 11U) & 31U) * blend) >> 8U;
    const unsigned green = ((((unsigned)first >> 5U) & 63U) * inverse +
                            (((unsigned)second >> 5U) & 63U) * blend) >> 8U;
    const unsigned blue = (((unsigned)first & 31U) * inverse +
                           ((unsigned)second & 31U) * blend) >> 8U;
    return (uint16_t)((red << 11U) | (green << 5U) | blue);
}

static void draw_dragon_sprite(p4_game_surface_t *surface,
                               const byte_buddy_state_t *state,
                               int left, int top)
{
    const uint32_t frame_interval_ms = dragon_frame_interval_ms(state);
    const uint32_t remainder =
        state->animation_ms % frame_interval_ms;
    const uint32_t first_animation_ms = state->animation_ms - remainder;
    const uint8_t first_reaction = state->reaction;
    uint32_t first_reaction_ms = state->reaction_ms;
    if (first_reaction_ms != 0U) {
        const uint32_t rewound = first_reaction_ms + remainder;
        first_reaction_ms = rewound > REACTION_DURATION_MS
            ? REACTION_DURATION_MS : rewound;
    }
    const uint32_t second_animation_ms =
        first_animation_ms + frame_interval_ms;
    uint8_t second_reaction = first_reaction;
    uint32_t second_reaction_ms = first_reaction_ms;
    if (second_reaction_ms > frame_interval_ms) {
        second_reaction_ms -= frame_interval_ms;
    } else if (second_reaction_ms != 0U) {
        second_reaction_ms = 0U;
        second_reaction = REACTION_IDLE;
    }
    unsigned first_sheet = 0U;
    unsigned first_frame = 0U;
    unsigned second_sheet = 0U;
    unsigned second_frame = 0U;
    dragon_sheet_frame(state, first_animation_ms, first_reaction,
                       first_reaction_ms, &first_sheet, &first_frame);
    dragon_sheet_frame(state, second_animation_ms, second_reaction,
                       second_reaction_ms, &second_sheet, &second_frame);
    const dragon_frame_view_t first = dragon_frame_view(
        state, first_sheet, first_frame);
    const dragon_frame_view_t second = dragon_frame_view(
        state, second_sheet, second_frame);
    if (!first.valid || !second.valid) {
        return;
    }
    const uint16_t progress = (uint16_t)(
        remainder * UINT16_MAX / frame_interval_ms);
    const unsigned blend = (unsigned)(
        p4_ease_smoothstep_u16(progress) >> 8U);
    for (int source_y = 0; source_y < DRAGON_FRAME_HEIGHT; ++source_y) {
        const int destination_y = top + source_y;
        if (destination_y < 0 || destination_y >= surface->height) {
            continue;
        }
        for (int source_x = 0; source_x < DRAGON_FRAME_WIDTH; ++source_x) {
            const int destination_x = left + source_x;
            if (destination_x < 0 || destination_x >= surface->width) {
                continue;
            }
            const size_t source_index = (size_t)source_y *
                DRAGON_FRAME_WIDTH + (size_t)source_x;
            uint16_t first_color = 0U;
            uint16_t second_color = 0U;
            const bool first_visible = dragon_frame_color(
                &first, source_index, &first_color);
            const bool second_visible = dragon_frame_color(
                &second, source_index, &second_color);
            if (!first_visible && !second_visible) {
                continue;
            }
            if (first_visible) {
                first_color = lift_sprite_color(first_color);
                first_color = customize_color(
                    state, first_color, source_x, source_y);
            }
            if (second_visible) {
                second_color = lift_sprite_color(second_color);
                second_color = customize_color(
                    state, second_color, source_x, source_y);
            }
            uint16_t value = 0U;
            if (first_visible && second_visible) {
                value = tween_rgb565(first_color, second_color, blend);
            } else {
                const bool use_second = blend >= 128U;
                if ((use_second && !second_visible) ||
                    (!use_second && !first_visible)) {
                    continue;
                }
                value = use_second ? second_color : first_color;
            }
            surface->pixels[(size_t)destination_y *
                surface->stride_pixels + (size_t)destination_x] = value;
        }
    }
}

static void draw_element_particles(p4_game_surface_t *surface,
                                   const byte_buddy_state_t *state,
                                   int left, int top)
{
    const unsigned stage = safe_stage(state);
    if (stage != BYTE_BUDDY_STAGE_EGG &&
        stage != BYTE_BUDDY_STAGE_ELEMENTAL) {
        return;
    }
    const int drift = (int)((state->animation_ms / 120U) % 6U);
    const uint16_t color = element_color(state);
    switch ((byte_buddy_element_t)safe_element(state)) {
    case BYTE_BUDDY_ELEMENT_FIRE:
        p4_draw_fill_rect(surface, left + 4, top + 35 - drift, 2, 4, color);
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 6,
                          top + 28 - drift, 2, 3,
                          UINT16_C(0xffe0));
        p4_draw_fill_rect(surface, left + 10, top + 18 + drift, 2, 2, color);
        break;
    case BYTE_BUDDY_ELEMENT_ICE:
        p4_draw_fill_rect(surface, left + 2, top + 14 + drift, 7, 1, color);
        p4_draw_fill_rect(surface, left + 5, top + 11 + drift, 1, 7, color);
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 7,
                          top + 30 - drift, 5, 1, color);
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 5,
                          top + 28 - drift, 1, 5, color);
        break;
    case BYTE_BUDDY_ELEMENT_ACID:
        p4_draw_fill_circle(surface, left + 5, top + 36 - drift, 2, color);
        p4_draw_fill_circle(surface, left + DRAGON_FRAME_WIDTH - 5,
                            top + 20 + drift, 2, color);
        p4_draw_fill_circle(surface, left + 10, top + 10 + drift, 1,
                            UINT16_C(0x07e0));
        break;
    case BYTE_BUDDY_ELEMENT_MYSTERY:
    default:
        p4_draw_fill_rect(surface, left + 5, top + 14 + drift, 3, 3, color);
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 7,
                          top + 31 - drift, 3, 3,
                          UINT16_C(0x07ff));
        break;
    }
    if (state->upgrades[BYTE_BUDDY_UPGRADE_AURA] >= 1U) {
        p4_draw_fill_rect(surface, left - 2, top + 23 - drift, 2, 2,
                          color);
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH,
                          top + 13 + drift, 2, 2,
                          color);
    }
    if (state->upgrades[BYTE_BUDDY_UPGRADE_AURA] >= 2U) {
        p4_draw_rect(surface, left - 3, top - 3,
                     DRAGON_FRAME_WIDTH + 6, DRAGON_FRAME_HEIGHT + 6,
                     color);
    }
    if (state->upgrades[BYTE_BUDDY_UPGRADE_AURA] >= 3U) {
        p4_draw_fill_circle(surface, left + 24, top - 5 - drift / 2, 2,
                            UINT16_C(0xffff));
    }
}

static void draw_custom_trail(p4_game_surface_t *surface,
                              const byte_buddy_state_t *state,
                              int left, int top)
{
    const unsigned style = state->style_selected[BYTE_BUDDY_STYLE_TRAIL];
    if (style == 0U || style > s_style_max[BYTE_BUDDY_STYLE_TRAIL] ||
        safe_stage(state) == BYTE_BUDDY_STAGE_EGG) {
        return;
    }
    const int drift = (int)((state->animation_ms / 70U) % 8U);
    const uint16_t color = trail_color(state);
    const int x1 = left + 2 - drift;
    const int y1 = top + 30 + drift / 2;
    const int x2 = left + DRAGON_FRAME_WIDTH - 5 - drift / 2;
    const int y2 = top + 8 + drift;
    switch (style) {
    case 1U: /* stars */
        p4_draw_fill_rect(surface, x1 - 3, y1, 7, 1, color);
        p4_draw_fill_rect(surface, x1, y1 - 3, 1, 7, color);
        p4_draw_fill_rect(surface, x2 - 2, y2, 5, 1, color);
        p4_draw_fill_rect(surface, x2, y2 - 2, 1, 5, color);
        break;
    case 2U: /* hearts */
        p4_draw_fill_rect(surface, x1 - 3, y1 - 2, 3, 3, color);
        p4_draw_fill_rect(surface, x1 + 1, y1 - 2, 3, 3, color);
        p4_draw_fill_rect(surface, x1 - 1, y1 + 1, 3, 3, color);
        p4_draw_fill_rect(surface, x2 - 2, y2 - 1, 2, 2, color);
        p4_draw_fill_rect(surface, x2 + 1, y2 - 1, 2, 2, color);
        p4_draw_fill_rect(surface, x2, y2 + 1, 1, 2, color);
        break;
    case 3U: /* frost */
        p4_draw_fill_rect(surface, x1 - 4, y1, 9, 1, color);
        p4_draw_fill_rect(surface, x1, y1 - 4, 1, 9, color);
        p4_draw_fill_rect(surface, x2 - 3, y2, 7, 1, color);
        p4_draw_fill_rect(surface, x2, y2 - 3, 1, 7, color);
        break;
    case 4U: /* sparks */
        p4_draw_fill_rect(surface, x1 - 4, y1 + 2, 5, 2, color);
        p4_draw_fill_rect(surface, x1 + 2, y1 - 2, 3, 2,
                          UINT16_C(0xffff));
        p4_draw_fill_rect(surface, x2 - 2, y2 + 2, 4, 2, color);
        break;
    default:
        break;
    }
    if (state->upgrades[BYTE_BUDDY_UPGRADE_WINGS] >= 2U) {
        p4_draw_fill_rect(surface, left - 7 - drift, top + 20, 4, 2, color);
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH + 1 + drift,
                          top + 34, 3, 2,
                          color);
    }
}

static void draw_lineage_back_layers(p4_game_surface_t *surface,
                                     const byte_buddy_state_t *state,
                                     int left, int top)
{
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    if (lineage.tier < BYTE_BUDDY_LINEAGE_AURORA ||
        state->art_sheets < DRAGON_EXTENDED_SHEET_COUNT) {
        return;
    }
    const int center_x = left + DRAGON_FRAME_WIDTH / 2;
    const int center_y = top + DRAGON_FRAME_HEIGHT / 2;
    const int drift = (int)((state->animation_ms / 180U) % 3U) - 1;
    (void)draw_signal_layer_scaled(
        surface, state, 12U + lineage.aura, lineage.primary_hue,
        center_x, center_y + drift, DRAGON_FRAME_WIDTH);
    (void)draw_signal_layer_scaled(
        surface, state, 4U + lineage.halo, lineage.primary_hue,
        center_x, center_y - drift, DRAGON_FRAME_WIDTH - 6U);
    if ((lineage.adaptations &
         BYTE_BUDDY_ADAPTATION_PRISMATIC) != 0U) {
        (void)draw_signal_layer_scaled(
            surface, state, 4U + ((lineage.halo + 2U) & 3U),
            lineage.secondary_hue, center_x, center_y + drift,
            DRAGON_FRAME_WIDTH - 12U);
    }
    if (lineage.tier >= BYTE_BUDDY_LINEAGE_MYTHIC) {
        (void)draw_signal_layer_scaled(
            surface, state, 12U + ((lineage.aura + 1U) & 3U),
            lineage.secondary_hue, center_x, center_y - drift,
            DRAGON_FRAME_WIDTH - 2U);
    }
    if (lineage.resonance >= BYTE_BUDDY_RESONANCE_GALAXY) {
        (void)draw_signal_layer_scaled(
            surface, state, 12U + ((lineage.aura + 2U) & 3U),
            (uint8_t)((lineage.secondary_hue + lineage.resonance) & 7U),
            center_x, center_y + drift, DRAGON_FRAME_WIDTH + 4U);
    }
}

static void draw_signal_mutation(p4_game_surface_t *surface,
                                 const byte_buddy_state_t *state,
                                 int left, int top)
{
    static const int8_t anchors[8][2] = {
        {-3, 12}, {10, -3}, {31, -4}, {53, 0},
        {66, 17}, {67, 46}, {46, 65}, {7, 60},
    };
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    if (lineage.tier == BYTE_BUDDY_LINEAGE_DORMANT) {
        return;
    }
    const int phase = (int)((state->animation_ms / 90U) % 16U);
    const int orbit = phase < 8 ? phase : 15 - phase;
    const uint16_t color = signal_color_for_hue(lineage.primary_hue);
    const uint16_t secondary = signal_color_for_hue(
        lineage.secondary_hue);
    p4_draw_fill_circle(surface, left - 3 + orbit, top + 9, 1, color);
    p4_draw_fill_circle(surface, left + DRAGON_FRAME_WIDTH + 2 - orbit,
                        top + 41, 1,
                        UINT16_C(0xffff));
    p4_draw_fill_circle(surface, left + 24 + orbit, top - 3, 1, color);
    unsigned spark_count = 1U + lineage.tier + lineage.channel_families +
        lineage.resonance;
    if (spark_count > 11U) {
        spark_count = 11U;
    }
    for (unsigned spark = 0U; spark < spark_count; ++spark) {
        const unsigned shift = (spark * 7U) & 63U;
        const unsigned anchor = (unsigned)(
            (state->signal_entropy >> shift) & UINT64_C(7));
        const int local_phase = (phase + (int)spark * 3) & 15;
        const int lift = local_phase < 8 ? local_phase : 15 - local_phase;
        const int sparkle_x = left + anchors[anchor][0] +
            (int)(spark & 1U) * 2 - 1;
        const int sparkle_y = top + anchors[anchor][1] - lift / 3;
        const uint16_t sparkle_color =
            (lineage.adaptations &
             BYTE_BUDDY_ADAPTATION_PRISMATIC) != 0U
                ? signal_color_for_hue((uint8_t)(
                    (lineage.primary_hue + spark) & 7U))
                : (spark & 1U) == 0U ? color : secondary;
        p4_draw_fill_circle(surface, sparkle_x, sparkle_y,
                            lineage.tier >= BYTE_BUDDY_LINEAGE_MYTHIC &&
                                spark >= 6U ? 2 : 1,
                            sparkle_color);
    }
    if (lineage.tier >= BYTE_BUDDY_LINEAGE_CREST &&
        state->art_sheets >= DRAGON_EXTENDED_SHEET_COUNT) {
        (void)draw_signal_layer_scaled(
            surface, state, 8U + lineage.marking,
            lineage.secondary_hue,
            left + DRAGON_FRAME_WIDTH / 2,
            top + DRAGON_FRAME_HEIGHT / 2 + 2, 42U);
        if ((lineage.adaptations &
             BYTE_BUDDY_ADAPTATION_CHIMERA) != 0U) {
            (void)draw_signal_layer_scaled(
                surface, state,
                8U + ((lineage.marking + lineage.family + 1U) & 3U),
                lineage.primary_hue,
                left + DRAGON_FRAME_WIDTH / 2,
                top + DRAGON_FRAME_HEIGHT / 2 + 1, 34U);
        }
    }
    if (lineage.tier >= BYTE_BUDDY_LINEAGE_AURORA ||
        state->upgrades[BYTE_BUDDY_UPGRADE_AURA] != 0U) {
        const int pulse = (int)((state->animation_ms / 240U) & 1U);
        p4_draw_rect(surface, left - 3 - pulse, top - 3 - pulse,
                     DRAGON_FRAME_WIDTH + 6 + pulse * 2,
                     DRAGON_FRAME_HEIGHT + 6 + pulse * 2, color);
    }
    if (lineage.shielded) {
        p4_draw_fill_rect(surface, left - 5, top + 9, 8, 2,
                          UINT16_C(0xffff));
        p4_draw_fill_rect(surface, left - 5, top + 9, 2, 8, color);
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 2,
                          top + 47, 8, 2, UINT16_C(0xffff));
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH + 4,
                          top + 41, 2, 8, color);
    }
    if (lineage.phantom) {
        p4_draw_fill_circle(surface, left + 58 - orbit / 2,
                            top + 7 + orbit / 3, 2, secondary);
        p4_draw_fill_circle(surface, left + 61 - orbit / 2,
                            top + 5 + orbit / 3, 1,
                            UINT16_C(0xffff));
    }
    if ((lineage.adaptations & BYTE_BUDDY_ADAPTATION_WIDEBAND) != 0U) {
        static const int8_t band_nodes[4][2] = {
            {5, 8}, {59, 7}, {62, 55}, {3, 54},
        };
        for (unsigned band = 0U; band < 4U; ++band) {
            const unsigned rotated =
                (band + (state->animation_ms / 260U)) & 3U;
            p4_draw_fill_circle(
                surface, left + band_nodes[rotated][0],
                top + band_nodes[rotated][1], 2,
                signal_color_for_hue((uint8_t)(
                    (lineage.primary_hue + band * 2U) & 7U)));
        }
    }
    if (lineage.tier >= BYTE_BUDDY_LINEAGE_MYTHIC) {
        const int crown_x = left + DRAGON_FRAME_WIDTH / 2;
        p4_draw_fill_rect(surface, crown_x - 7, top - 7, 15, 2,
                          secondary);
        p4_draw_fill_rect(surface, crown_x - 6, top - 11, 3, 5, color);
        p4_draw_fill_rect(surface, crown_x - 1, top - 13, 3, 7,
                          UINT16_C(0xffff));
        p4_draw_fill_rect(surface, crown_x + 4, top - 11, 3, 5,
                          color);
        for (uint8_t rank = 0U; rank < lineage.resonance; ++rank) {
            const int offset = 11 + (int)rank * 4;
            p4_draw_fill_circle(surface, crown_x - offset,
                                top - 8 - (int)(rank & 1U), 1,
                                signal_color_for_hue((uint8_t)(
                                    (lineage.primary_hue + rank * 2U) &
                                    7U)));
            p4_draw_fill_circle(surface, crown_x + offset,
                                top - 8 - (int)(rank & 1U), 1,
                                signal_color_for_hue((uint8_t)(
                                    (lineage.secondary_hue + rank * 2U) &
                                    7U)));
        }
    }
}

static void draw_reaction_effect(p4_game_surface_t *surface,
                                 const byte_buddy_state_t *state,
                                 int left, int top)
{
    if (state->reaction_ms == 0U) {
        return;
    }
    const int pulse = (int)((state->animation_ms / 100U) % 3U);
    switch ((buddy_reaction_t)state->reaction) {
    case REACTION_FEED:
        p4_draw_fill_circle(surface, left + DRAGON_FRAME_WIDTH - 3,
                            top + 41, 3,
                            UINT16_C(0xffe0));
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 6,
                          top + 37, 6, 2,
                          UINT16_C(0xfd20));
        break;
    case REACTION_PLAY:
        p4_draw_fill_rect(surface, left + 2, top + 6 + pulse, 7, 1,
                          UINT16_C(0xffe0));
        p4_draw_fill_rect(surface, left + 5, top + 3 + pulse, 1, 7,
                          UINT16_C(0xffe0));
        break;
    case REACTION_CLEAN:
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 8,
                          top + 8, 7, 1,
                          UINT16_C(0x07ff));
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 5,
                          top + 5, 1, 7,
                          UINT16_C(0x07ff));
        p4_draw_fill_rect(surface, left + 2, top + 31 + pulse, 4, 4,
                          UINT16_C(0xffff));
        break;
    case REACTION_REST:
        p4_draw_text(surface, left + DRAGON_FRAME_WIDTH - 9,
                     top + 3 - pulse, "Z",
                     UINT16_C(0x07ff), 1U, 1U);
        break;
    case REACTION_PET:
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 10,
                          top + 3 - pulse, 3, 3,
                          UINT16_C(0xf81f));
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 5,
                          top + 3 - pulse, 3, 3,
                          UINT16_C(0xf81f));
        p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 8,
                          top + 6 - pulse, 4, 3,
                          UINT16_C(0xf81f));
        break;
    case REACTION_GROW:
        p4_draw_rect(surface, left - 2 - pulse, top - 2 - pulse,
                     DRAGON_FRAME_WIDTH + 4 + pulse * 2,
                     DRAGON_FRAME_HEIGHT + 4 + pulse * 2,
                     element_color(state));
        break;
    case REACTION_SIGNAL:
        p4_draw_rect(surface, left - 4 - pulse, top - 4 - pulse,
                     DRAGON_FRAME_WIDTH + 8 + pulse * 2,
                     DRAGON_FRAME_HEIGHT + 8 + pulse * 2,
                     signal_color(state));
        p4_draw_fill_circle(surface, left + 4, top + 7 - pulse, 2,
                            signal_color(state));
        p4_draw_fill_circle(surface, left + DRAGON_FRAME_WIDTH - 3,
                            top + 41 + pulse, 2,
                            UINT16_C(0xffff));
        break;
    case REACTION_IDLE:
    default:
        break;
    }
}

static void draw_dragon(p4_game_surface_t *surface,
                        const byte_buddy_state_t *state,
                        int center_x, int top)
{
    const unsigned motion_phase = (unsigned)(
        (state->animation_ms / 40U) % 32U);
    int hover = (int)(s_eased_motion[motion_phase] / 2U);
    if (safe_stage(state) >= BYTE_BUDDY_STAGE_FLYING) {
        hover = (int)s_eased_motion[motion_phase];
        center_x += motion_phase < 16U
            ? (int)(motion_phase / 5U)
            : (int)((31U - motion_phase) / 5U);
    }
    const int left = center_x - DRAGON_FRAME_WIDTH / 2;
    top -= hover;
    const int shadow_width = safe_stage(state) >= BYTE_BUDDY_STAGE_FLYING
        ? DRAGON_FRAME_WIDTH * 3 / 8 - hover / 2
        : DRAGON_FRAME_WIDTH / 2 + 2 - hover;
    p4_draw_fill_rect(surface, center_x - shadow_width / 2,
                      top + DRAGON_FRAME_HEIGHT + hover - 2,
                      shadow_width, 2, UINT16_C(0x18c3));
    p4_draw_fill_rect(surface, center_x - shadow_width / 3,
                      top + DRAGON_FRAME_HEIGHT + hover,
                      shadow_width * 2 / 3, 1, UINT16_C(0x1082));
    if (state->upgrades[BYTE_BUDDY_UPGRADE_NEST] != 0U &&
        safe_stage(state) < BYTE_BUDDY_STAGE_FLYING) {
        const int nest_width = 46 +
            (int)state->upgrades[BYTE_BUDDY_UPGRADE_NEST] * 5;
        p4_draw_fill_rect(surface, center_x - nest_width / 2,
                          top + DRAGON_FRAME_HEIGHT - 5,
                          nest_width, 3, UINT16_C(0xa2a0));
        p4_draw_fill_rect(surface, center_x - nest_width / 2 + 3,
                          top + DRAGON_FRAME_HEIGHT - 8,
                          nest_width - 6, 3,
                          UINT16_C(0xfd20));
    }
    draw_element_particles(surface, state, left, top);
    draw_lineage_back_layers(surface, state, left, top);
    draw_custom_trail(surface, state, left, top);
    draw_dragon_sprite(surface, state, left, top);
    draw_signal_mutation(surface, state, left, top);
    if (safe_stage(state) >= BYTE_BUDDY_STAGE_WINGED) {
        if (safe_wing_style(state) == BYTE_BUDDY_WINGS_SHINY) {
            p4_draw_fill_rect(surface, left + 2, top + 13, 6, 1,
                              UINT16_C(0x07ff));
            p4_draw_fill_rect(surface, left + 5, top + 10, 1, 7,
                              UINT16_C(0x07ff));
            p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 7,
                              top + 25, 5, 1,
                              UINT16_C(0xffe0));
            p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 5,
                              top + 23, 1, 5,
                              UINT16_C(0xffe0));
        } else {
            p4_draw_fill_rect(surface, left, top + 15, 4, 2,
                              element_color(state));
            p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 4,
                              top + 20, 4, 2,
                              element_color(state));
            p4_draw_fill_rect(surface, left + 3, top + 10, 2, 4,
                              element_color(state));
            p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH - 5,
                              top + 14, 2, 4,
                              element_color(state));
        }
        if (state->upgrades[BYTE_BUDDY_UPGRADE_WINGS] >= 1U) {
            const int trail = (int)(
                state->upgrades[BYTE_BUDDY_UPGRADE_WINGS] * 2U);
            p4_draw_fill_rect(surface, left - trail, top + 22,
                              trail, 2, trail_color(state));
            p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH, top + 34,
                              trail, 2, trail_color(state));
        }
        if (state->upgrades[BYTE_BUDDY_UPGRADE_WINGS] >= 3U) {
            p4_draw_fill_rect(surface, left - 5, top + 10, 3, 3,
                              UINT16_C(0xffff));
            p4_draw_fill_rect(surface, left + DRAGON_FRAME_WIDTH + 2,
                              top + 20, 3, 3,
                              UINT16_C(0xffff));
        }
    }
    draw_reaction_effect(surface, state, left, top);
    if (safe_stage(state) == BYTE_BUDDY_STAGE_EGG &&
        state->care_actions >= GROW_BABY_INTERACTIONS / 2U) {
        p4_draw_fill_rect(surface, center_x, top + 20, 2, 6,
                          UINT16_C(0xffff));
        p4_draw_fill_rect(surface, center_x - 3, top + 25, 4, 2,
                          UINT16_C(0xffff));
    }
    if (health(state) < 35U) {
        p4_draw_text(surface, left + DRAGON_FRAME_WIDTH - 6,
                     top + DRAGON_FRAME_HEIGHT - 13, "!",
                     UINT16_C(0xf800), 2U, 1U);
    }
}

static void draw_growth_panel(p4_game_surface_t *surface,
                              const byte_buddy_state_t *state)
{
    const unsigned stage = safe_stage(state);
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    p4_draw_text(surface, 204, 34, "STAGE", UINT16_C(0x7bef), 1U, 5U);
    p4_draw_text(surface, 204, 44, s_stage_names[stage],
                 UINT16_C(0xffff), 1U, 9U);
    p4_draw_text(surface, 204, 58, "ELEMENT", UINT16_C(0x7bef), 1U, 7U);
    p4_draw_text(surface, 204, 68, s_element_names[safe_element(state)],
                 element_color(state), 1U, 7U);
    if (stage >= BYTE_BUDDY_STAGE_WINGED) {
        p4_draw_text(surface, 204, 82, "WINGS", UINT16_C(0x7bef), 1U, 5U);
        p4_draw_text(surface, 204, 92,
                     s_wing_names[safe_wing_style(state)],
                     UINT16_C(0xffff), 1U, 6U);
    }
    if (stage == BYTE_BUDDY_STAGE_ELEMENTAL &&
        lineage.tier != BYTE_BUDDY_LINEAGE_DORMANT) {
        p4_draw_text(surface, 204, 108, "LINEAGE", UINT16_C(0x7bef),
                     1U, 7U);
        p4_draw_text(surface, 204, 118,
                     s_lineage_names[lineage.tier],
                     signal_color(state), 1U,
                     s_lineage_name_lengths[lineage.tier]);
        p4_draw_text(surface, 204, 128, "LINK", UINT16_C(0x7bef),
                     1U, 4U);
        draw_number(surface, 238, 128,
                    state->signal_consumed_count, UINT16_C(0xffff));
        p4_draw_text(surface, 260, 128, "DNA", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 284, 128,
                    lineage.diversity, UINT16_C(0xffff));
    } else if (stage == BYTE_BUDDY_STAGE_ELEMENTAL &&
        rare_morph_unlocked(state)) {
        p4_draw_text(surface, 204, 108, "RARE MORPH", UINT16_C(0xffe0),
                     1U, 10U);
        const unsigned morph = safe_morph(state);
        p4_draw_text(surface, 204, 118, s_morph_names[morph],
                     element_color(state), 1U,
                     s_morph_name_lengths[morph]);
    } else if (stage + 1U < BYTE_BUDDY_STAGE_COUNT) {
        const uint16_t start = s_growth_thresholds[stage];
        const uint16_t end = s_growth_thresholds[stage + 1U];
        const uint16_t bounded_care = state->care_actions < start
            ? start : state->care_actions > end ? end : state->care_actions;
        const uint16_t progress = (uint16_t)(bounded_care - start);
        const uint16_t span = (uint16_t)(end - start);
        const uint16_t remaining = state->care_actions >= end
            ? 0U : (uint16_t)(end - state->care_actions);
        p4_draw_text(surface, 204, 108, "GROWTH", UINT16_C(0x7bef),
                     1U, 6U);
        p4_draw_rect(surface, 204, 118, 104, 7, UINT16_C(0x7bef));
        p4_draw_fill_rect(surface, 205, 119,
                          (int)((uint32_t)progress * 102U / span), 5,
                          element_color(state));
        p4_draw_text(surface, 204, 128, "NEXT", UINT16_C(0x7bef),
                     1U, 4U);
        draw_number(surface, 238, 128, remaining, UINT16_C(0xffff));
    } else {
        p4_draw_text(surface, 204, 108, "GROWTH", UINT16_C(0x7bef),
                     1U, 6U);
        p4_draw_text(surface, 204, 120, "MAX", element_color(state),
                     1U, 3U);
    }
}

static void draw_touch_button(p4_game_surface_t *surface,
                              int x, int y, int width, int height,
                              const char *label, size_t label_length,
                              uint16_t accent, bool active)
{
    p4_draw_fill_rect(surface, x, y, width, height,
                      active ? accent : UINT16_C(0x1025));
    p4_draw_rect(surface, x, y, width, height,
                 active ? UINT16_C(0xffff) : accent);
    const int text_width = (int)label_length * 6;
    p4_draw_text(surface, x + (width - text_width) / 2,
                 y + (height - 7) / 2, label,
                 active ? UINT16_C(0x0000) : UINT16_C(0xffff),
                 1U, label_length);
}

static void draw_coin_badge(p4_game_surface_t *surface,
                            const byte_buddy_state_t *state,
                            int x, int y, int width, int height)
{
    p4_draw_fill_rect(surface, x, y, width, height, UINT16_C(0x4208));
    p4_draw_rect(surface, x, y, width, height, UINT16_C(0xffe0));
    const bool drew_coin = draw_art_frame_scaled(
        surface, state, STAR_CATCHER_REWARD_SHEET, 0U,
        x + 10, y + height / 2, 14U);
    p4_draw_text(surface, x + (drew_coin ? 20 : 7), y + 6,
                 "COINS", UINT16_C(0xffe0), 1U, 5U);
    draw_number(surface, x + width - 25, y + 6, state->coins,
                UINT16_C(0xffff));
}

static void draw_battle_stats(p4_game_surface_t *surface,
                              const byte_buddy_state_t *state)
{
    const byte_buddy_battle_stats_t stats = current_battle_stats(state);
    p4_draw_text(surface, 5, 168, "PWR", UINT16_C(0xfd20), 1U, 3U);
    draw_number(surface, 28, 168, stats.power, UINT16_C(0xffff));
    p4_draw_text(surface, 5, 181, "GRD", UINT16_C(0x07ff), 1U, 3U);
    draw_number(surface, 28, 181, stats.guard, UINT16_C(0xffff));
    p4_draw_text(surface, 244, 168, "SPD", UINT16_C(0xffe0), 1U, 3U);
    draw_number(surface, 267, 168, stats.speed, UINT16_C(0xffff));
    p4_draw_text(surface, 244, 181, "MAG", UINT16_C(0xf81f), 1U, 3U);
    draw_number(surface, 267, 181, stats.magic, UINT16_C(0xffff));
}

static void draw_upgrade_card(p4_game_surface_t *surface,
                              const byte_buddy_state_t *state,
                              byte_buddy_upgrade_t upgrade,
                              int x, int y,
                              const char *label, size_t label_length,
                              const char *detail, size_t detail_length)
{
    const uint8_t level = state->upgrades[upgrade];
    const uint16_t cost = byte_buddy_upgrade_cost(level);
    const bool affordable = cost != UINT16_MAX && state->coins >= cost;
    const uint16_t accent = affordable
        ? element_color(state) : UINT16_C(0x7bef);
    p4_draw_fill_rect(surface, x, y, 148, 54, UINT16_C(0x1025));
    p4_draw_rect(surface, x, y, 148, 54,
                 state->controller_active &&
                         state->menu_selection == (uint8_t)upgrade
                     ? UINT16_C(0xffff) : accent);
    draw_item_component_icon(
        surface, state, 4U + (unsigned)upgrade, x + 18, y + 14, 20U);
    p4_draw_text(surface, x + 33, y + 7, label, UINT16_C(0xffff),
                 1U, label_length);
    p4_draw_text(surface, x + 33, y + 19, detail, UINT16_C(0x9cf3),
                 1U, detail_length);
    p4_draw_text(surface, x + 8, y + 36, "LV", UINT16_C(0x7bef), 1U, 2U);
    draw_number(surface, x + 25, y + 36, level, UINT16_C(0xffff));
    if (cost == UINT16_MAX) {
        p4_draw_text(surface, x + 101, y + 36, "MAX", UINT16_C(0xffe0),
                     1U, 3U);
    } else {
        p4_draw_text(surface, x + 77, y + 36, "COST", UINT16_C(0x7bef),
                     1U, 4U);
        draw_number(surface, x + 112, y + 36, cost,
                    affordable ? UINT16_C(0xffe0) : UINT16_C(0xf800));
    }
}

static const char *style_name(byte_buddy_style_t style, uint8_t value,
                              size_t *out_length)
{
    static const uint8_t body_lengths[] = {7U, 4U, 7U, 4U, 7U, 4U, 4U, 8U};
    static const uint8_t eye_lengths[] = {4U, 4U, 4U, 5U, 6U, 3U};
    static const uint8_t horn_lengths[] = {5U, 5U, 4U, 4U, 4U};
    static const uint8_t trail_lengths[] = {7U, 5U, 6U, 5U, 6U};
    if (out_length == NULL || (unsigned)style >= BYTE_BUDDY_STYLE_COUNT) {
        return NULL;
    }
    value = value <= s_style_max[style] ? value : 0U;
    switch (style) {
    case BYTE_BUDDY_STYLE_BODY:
        *out_length = body_lengths[value];
        return s_body_style_names[value];
    case BYTE_BUDDY_STYLE_EYES:
        *out_length = eye_lengths[value];
        return s_eye_style_names[value];
    case BYTE_BUDDY_STYLE_HORNS:
        *out_length = horn_lengths[value];
        return s_horn_style_names[value];
    case BYTE_BUDDY_STYLE_TRAIL:
        *out_length = trail_lengths[value];
        return s_trail_style_names[value];
    default:
        return NULL;
    }
}

static void draw_shop_tabs(p4_game_surface_t *surface, bool style_shop)
{
    draw_touch_button(surface, 6, 25, 148, 17, "POWER", 5U,
                      UINT16_C(0x07ff), !style_shop);
    draw_touch_button(surface, 166, 25, 148, 17, "STYLE", 5U,
                      UINT16_C(0xf81f), style_shop);
}

static void draw_style_card(p4_game_surface_t *surface,
                            const byte_buddy_state_t *state,
                            byte_buddy_style_t style,
                            int x, int y,
                            const char *label, size_t label_length)
{
    const uint8_t selected = state->style_selected[style];
    const uint8_t unlocked = state->style_unlocked[style];
    const uint16_t cost = byte_buddy_style_cost(style, unlocked);
    size_t name_length = 0U;
    const char *const name = style_name(style, selected, &name_length);
    p4_draw_fill_rect(surface, x, y, 148, 55, UINT16_C(0x1025));
    p4_draw_rect(surface, x, y, 148, 55,
                 state->controller_active &&
                         state->menu_selection == (uint8_t)style
                     ? UINT16_C(0xffff) : element_color(state));
    p4_draw_text(surface, x + 7, y + 6, label, UINT16_C(0x7bef),
                 1U, label_length);
    draw_item_component_icon(
        surface, state, 8U + (unsigned)style, x + 97, y + 15, 20U);
    if (name != NULL) {
        p4_draw_text(surface, x + 7, y + 20, name, UINT16_C(0xffff),
                     1U, name_length);
    }
    p4_draw_text(surface, x + 7, y + 39,
                 state->controller_active ? "A SELECT" : "TAP SELECT",
                 UINT16_C(0x9cf3), 1U,
                 state->controller_active ? 8U : 10U);
    p4_draw_fill_rect(surface, x + 110, y + 1, 37, 53,
                      cost != UINT16_MAX && state->coins >= cost
                          ? UINT16_C(0x39e7) : UINT16_C(0x2104));
    if (cost == UINT16_MAX) {
        p4_draw_text(surface, x + 119, y + 22, "MAX",
                     UINT16_C(0xffe0), 1U, 3U);
    } else {
        p4_draw_text(surface, x + 119, y + 12, "BUY",
                     UINT16_C(0xffff), 1U, 3U);
        draw_number(surface, x + 124, y + 29, cost, UINT16_C(0xffe0));
    }
}

static void draw_upgrade_shop(p4_game_surface_t *surface,
                              const byte_buddy_state_t *state)
{
    p4_draw_text(surface, 70, 8, "DRAGON UPGRADES", UINT16_C(0xffff),
                 1U, 15U);
    draw_coin_badge(surface, state, 236, 3, 80, 20);
    draw_shop_tabs(surface, false);
    draw_upgrade_card(surface, state, BYTE_BUDDY_UPGRADE_WINGS,
                      6, 45, "WINGS", 5U, "SMOOTH TRAILS", 13U);
    draw_upgrade_card(surface, state, BYTE_BUDDY_UPGRADE_AURA,
                      166, 45, "AURA", 4U, "MORE MAGIC", 10U);
    draw_upgrade_card(surface, state, BYTE_BUDDY_UPGRADE_NEST,
                      6, 105, "NEST", 4U, "CARE BOOST", 10U);
    draw_upgrade_card(surface, state, BYTE_BUDDY_UPGRADE_MAGNET,
                      166, 105, "MAGNET", 6U, "STAR REACH", 10U);
    draw_touch_button(surface, 86, 164, 148, 30, "BACK TO DRAGON", 14U,
                      element_color(state), false);
    draw_battle_stats(surface, state);
    p4_game_feedback_draw_audio_effect(surface, &state->audio, 160, 92);
}

static void draw_style_shop(p4_game_surface_t *surface,
                            const byte_buddy_state_t *state)
{
    p4_draw_text(surface, 70, 8, "DRAGON STYLES", UINT16_C(0xffff),
                 1U, 13U);
    p4_draw_text(surface, 158, 8, "LOOK", UINT16_C(0x7bef), 1U, 4U);
    draw_number(surface, 188, 8, byte_buddy_style_recipe_id(
                    state->style_selected[BYTE_BUDDY_STYLE_BODY],
                    state->style_selected[BYTE_BUDDY_STYLE_EYES],
                    state->style_selected[BYTE_BUDDY_STYLE_HORNS],
                    state->style_selected[BYTE_BUDDY_STYLE_TRAIL],
                    state->wing_style, state->signal_hue),
                UINT16_C(0xffff));
    draw_coin_badge(surface, state, 236, 3, 80, 20);
    draw_shop_tabs(surface, true);
    draw_style_card(surface, state, BYTE_BUDDY_STYLE_BODY,
                    6, 45, "BODY", 4U);
    draw_style_card(surface, state, BYTE_BUDDY_STYLE_EYES,
                    166, 45, "EYES", 4U);
    draw_style_card(surface, state, BYTE_BUDDY_STYLE_HORNS,
                    6, 105, "HORNS", 5U);
    draw_style_card(surface, state, BYTE_BUDDY_STYLE_TRAIL,
                    166, 105, "TRAIL", 5U);
    draw_touch_button(
        surface, 6, 164, 148, 30,
        state->controller_active ? "START BUY" : "REMIX",
        state->controller_active ? sizeof("START BUY") - 1U
                                 : sizeof("REMIX") - 1U,
        UINT16_C(0xf81f), false);
    draw_item_component_icon(
        surface, state, 12U + (unsigned)(state->animation_ms / 140U) % 4U,
        20, 179, 20U);
    draw_touch_button(surface, 166, 164, 148, 30, "BACK", 4U,
                      element_color(state), false);
}

static const char *star_pace_name(uint16_t speed, size_t *length)
{
    if (speed <= 70U) {
        *length = 5U;
        return "CHILL";
    }
    if (speed <= 86U) {
        *length = 6U;
        return "STEADY";
    }
    *length = 5U;
    return "BRISK";
}

static void draw_play_game(p4_game_surface_t *surface,
                           const byte_buddy_state_t *state)
{
    p4_draw_fill_rect(surface, 0, 25, P4_GAME_SURFACE_WIDTH, 112,
                      UINT16_C(0x0822));
    p4_draw_fill_rect(surface, 0, 121, P4_GAME_SURFACE_WIDTH, 16,
                      UINT16_C(0x101b));
    const int lane_drift = (int)((state->animation_ms / 18U) % 320U);
    for (int streak = 0; streak < 5; ++streak) {
        const int x = (lane_drift + streak * 71) % 320;
        p4_draw_fill_rect(surface, x, 48 + streak * 14, 9, 1,
                          streak % 2 == 0 ? trail_color(state)
                                          : UINT16_C(0x7bef));
    }
    p4_draw_text(surface, 66, 8, "CATCH THE STARS", UINT16_C(0xffff),
                 1U, 15U);
    draw_touch_button(surface, 260, 3, 56, 20, "DONE", 4U,
                      element_color(state), false);
    p4_draw_text(surface, 93, 29, "SWIPE OR LEFT/RIGHT", UINT16_C(0xbdf7),
                 1U, 19U);
    const int star_y = p4_q16_to_int_round(state->star_y_q16);
    const int catcher_x = p4_q16_to_int_round(state->catcher_x_q16);
    const uint16_t fall_speed = byte_buddy_star_fall_speed(
        state->stage, state->play_streak);
    const unsigned reward_frame = (unsigned)(
        (state->animation_ms / 140U) % 4U);
    const bool drew_reward = draw_art_frame_scaled(
        surface, state, STAR_CATCHER_REWARD_SHEET,
        (unsigned)state->star_kind * 4U + reward_frame,
        state->star_x, star_y, 22U);
    if (!drew_reward) {
        const uint16_t fallback_color = state->star_kind == STAR_KIND_CALM
            ? UINT16_C(0x07ff) : state->star_kind == STAR_KIND_HEART
                ? UINT16_C(0xf81f) : UINT16_C(0xffe0);
        p4_draw_fill_circle(surface, state->star_x, star_y, 5,
                            fallback_color);
        p4_draw_fill_rect(surface, state->star_x - 1, star_y - 8,
                          2, 4, UINT16_C(0xffff));
    }
    if (state->star_effect_ms != 0U) {
        const unsigned effect_frame = (unsigned)(
            (MINI_GAME_EFFECT_DURATION_MS - state->star_effect_ms) * 4U /
            MINI_GAME_EFFECT_DURATION_MS);
        (void)draw_art_frame_scaled(
            surface, state, STAR_CATCHER_REWARD_SHEET,
            12U + (effect_frame > 3U ? 3U : effect_frame),
            state->star_effect_x, state->star_effect_y, 30U);
    }
    const int catcher_velocity = p4_q16_to_int_round(
        state->catcher_velocity_q16);
    if (catcher_velocity != 0) {
        const int direction = catcher_velocity > 0 ? -1 : 1;
        int speed = catcher_velocity > 0
            ? catcher_velocity : -catcher_velocity;
        speed = speed > MINI_GAME_CATCHER_MAX_SPEED
            ? MINI_GAME_CATCHER_MAX_SPEED : speed;
        const int trail_length = 5 + speed / 24;
        p4_draw_fill_rect(surface,
                          catcher_x + direction * (25 + trail_length),
                          110, 8 + trail_length, 2, trail_color(state));
        p4_draw_fill_rect(surface,
                          catcher_x + direction * (21 + trail_length / 2),
                          119, 5 + trail_length / 2, 1, UINT16_C(0xffff));
    }
    draw_dragon(surface, state, catcher_x, 82);
    const int catch_radius = 18 +
        (int)state->upgrades[BYTE_BUDDY_UPGRADE_MAGNET] * 5;
    p4_draw_rect(surface, catcher_x - catch_radius, 129,
                 catch_radius * 2, 7, element_color(state));
    draw_coin_badge(surface, state, 6, 166, 84, 28);
    p4_draw_text(surface, 102, 169, "STREAK", UINT16_C(0x7bef),
                 1U, 6U);
    draw_number(surface, 146, 169, state->play_streak, UINT16_C(0xffff));
    p4_draw_text(surface, 178, 169, "PACE", UINT16_C(0x7bef), 1U, 4U);
    size_t pace_length = 0U;
    const char *const pace = star_pace_name(fall_speed, &pace_length);
    p4_draw_text(surface, 211, 169, pace, element_color(state),
                 1U, pace_length);
    const uint32_t remaining = state->mini_elapsed_ms >= MINI_GAME_DURATION_MS
        ? 0U : MINI_GAME_DURATION_MS - state->mini_elapsed_ms;
    p4_draw_rect(surface, 105, 184, 205, 6, UINT16_C(0x7bef));
    p4_draw_fill_rect(surface, 106, 185,
                      (int)(remaining * 203U / MINI_GAME_DURATION_MS), 4,
                      element_color(state));
}

static size_t signal_label_length(const char *label, size_t maximum)
{
    size_t length = 0U;
    while (length < maximum && label[length] != '\0') {
        ++length;
    }
    return length;
}

static void draw_rssi(p4_game_surface_t *surface, int x, int y,
                      int8_t rssi, uint16_t color)
{
    p4_draw_text(surface, x, y, "-", color, 1U, 1U);
    const uint32_t magnitude = rssi < 0
        ? (uint32_t)(-(int)rssi) : (uint32_t)rssi;
    draw_number(surface, x + 7, y, magnitude, color);
}

static void draw_signal_orb(p4_game_surface_t *surface,
                            int x, int y, int radius,
                            uint16_t color, uint32_t animation_ms)
{
    const int pulse = (int)((animation_ms / 120U) % 3U);
    p4_draw_fill_circle(surface, x, y, radius, UINT16_C(0x1025));
    p4_draw_rect(surface, x - radius - pulse, y - radius - pulse,
                 (radius + pulse) * 2 + 1, (radius + pulse) * 2 + 1,
                 color);
    p4_draw_fill_circle(surface, x, y, radius / 2, color);
    p4_draw_fill_rect(surface, x - 1, y - radius / 2 - 2, 3, 3,
                      UINT16_C(0xffff));
}

static void draw_signal_habitat_accents(
    p4_game_surface_t *surface, uint8_t channel, uint8_t flags,
    int x, int y, unsigned size, uint8_t hue, uint32_t animation_ms)
{
    static const int8_t directions[4][2] = {
        {0, -8}, {8, 0}, {0, 8}, {-8, 0},
    };
    const uint8_t family = byte_buddy_signal_channel_family(channel);
    const unsigned nodes = family == UINT8_MAX ? 0U : (unsigned)family + 1U;
    const unsigned rotation = (animation_ms / 260U) & 3U;
    const int radius = (int)size / 2 + 3;
    for (unsigned node = 0U; node < nodes; ++node) {
        const unsigned direction = (node + rotation) & 3U;
        const int node_x = x + directions[direction][0] * radius / 8;
        const int node_y = y + directions[direction][1] * radius / 8;
        p4_draw_fill_circle(
            surface, node_x, node_y, size >= 40U ? 2 : 1,
            signal_color_for_hue((uint8_t)((hue + node * 2U) & 7U)));
    }
    if ((flags & P4_GAME_SIGNAL_PROTECTED) != 0U) {
        const int extent = (int)size / 2 + 1;
        const int corner = size >= 32U ? 5 : 3;
        const uint16_t shield = UINT16_C(0xffff);
        p4_draw_fill_rect(surface, x - extent, y - extent,
                          corner, 1, shield);
        p4_draw_fill_rect(surface, x - extent, y - extent,
                          1, corner, shield);
        p4_draw_fill_rect(surface, x + extent - corner + 1,
                          y + extent, corner, 1, shield);
        p4_draw_fill_rect(surface, x + extent,
                          y + extent - corner + 1, 1, corner, shield);
    }
    if ((flags & P4_GAME_SIGNAL_HIDDEN) != 0U) {
        const int drift = (int)((animation_ms / 150U) % 4U);
        const uint16_t ghost = signal_color_for_hue(
            (uint8_t)((hue + 4U) & 7U));
        p4_draw_fill_circle(surface, x - (int)size / 3,
                            y + (int)size / 3 - drift, 2, ghost);
        p4_draw_fill_circle(surface, x + (int)size / 3,
                            y - (int)size / 3 + drift, 1,
                            UINT16_C(0xffff));
    }
}

static void draw_signal_seed(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    uint64_t token, uint8_t channel, uint8_t flags,
    int x, int y, unsigned size,
    uint32_t animation_ms)
{
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(token);
    if (!draw_signal_layer_scaled(
            surface, state, genome.core, genome.hue, x, y, size)) {
        draw_signal_orb(surface, x, y, (int)size / 4,
                        signal_color_for_hue(genome.hue), animation_ms);
    } else {
        (void)draw_signal_layer_scaled(
            surface, state, 4U + genome.halo, genome.hue,
            x, y, size);
        (void)draw_signal_layer_scaled(
            surface, state, 8U + genome.sigil, genome.hue,
            x, y, size);
        if (size >= 24U || genome.rarity >= 2U) {
            const int aura_y =
                y + (int)((animation_ms / 180U) % 3U) - 1;
            (void)draw_signal_layer_scaled(
                surface, state, 12U + genome.aura, genome.hue,
                x, aura_y, size);
            if (genome.rarity == 3U &&
                size <= DRAGON_FRAME_WIDTH - 2U) {
                (void)draw_signal_layer_scaled(
                    surface, state, 12U + genome.aura, genome.hue,
                    x, y, size + 2U);
            }
        }
    }
    draw_signal_habitat_accents(
        surface, channel, flags, x, y, size, genome.hue, animation_ms);
}

static void draw_signal_meter(p4_game_surface_t *surface,
                              int x, int y, int width,
                              uint8_t strength, uint16_t color)
{
    p4_draw_rect(surface, x, y, width, 7, UINT16_C(0x7bef));
    p4_draw_fill_rect(surface, x + 1, y + 1,
                      (int)((uint32_t)(width - 2) * strength / 100U),
                      5, color);
}

static void draw_signal_city(p4_game_surface_t *surface,
                             const byte_buddy_state_t *state)
{
    p4_draw_fill_rect(surface, 0, 25, 320, 112, UINT16_C(0x080f));
    p4_draw_fill_rect(surface, 0, 65, 320, 72, UINT16_C(0x181f));
    p4_draw_fill_rect(surface, 0, 101, 320, 36, UINT16_C(0x281f));
    const int drift = (int)((state->animation_ms / 300U) % 8U);
    for (int star = 0; star < 9; ++star) {
        const int x = (star * 43 + 17 + drift) % 320;
        const int y = 31 + (star * 19) % 58;
        p4_draw_fill_rect(surface, x, y, star % 3 == 0 ? 2 : 1, 1,
                          star % 2 == 0 ? UINT16_C(0x07ff)
                                        : UINT16_C(0xf81f));
    }
    for (int building = 0; building < 12; ++building) {
        const int width = 18 + (building * 7) % 17;
        const int height = 18 + (building * 13) % 42;
        const int x = building * 29 - 8;
        const int top = 137 - height;
        p4_draw_fill_rect(surface, x, top, width, height,
                          building % 2 == 0 ? UINT16_C(0x1025)
                                            : UINT16_C(0x182d));
        for (int window = 0; window < 3; ++window) {
            const int wx = x + 4 + window * 7;
            if (wx + 2 < x + width) {
                p4_draw_fill_rect(surface, wx, top + 7 + window * 9,
                                  2, 3, window % 2 == 0
                                      ? UINT16_C(0x07ff)
                                      : UINT16_C(0xfd20));
            }
        }
        if (building % 3 == 0) {
            p4_draw_fill_rect(surface, x + width / 2, top - 8, 1, 8,
                              UINT16_C(0x7bef));
            p4_draw_fill_rect(surface, x + width / 2 - 1, top - 10,
                              3, 3, signal_color(state));
        }
    }
    const int ring = (int)((state->animation_ms / 180U) % 5U);
    p4_draw_rect(surface, 252 - 8 - ring, 48 - 8 - ring,
                 17 + ring * 2, 17 + ring * 2, UINT16_C(0x07ff));
    p4_draw_rect(surface, 75 - 5 - ring, 58 - 5 - ring,
                 11 + ring * 2, 11 + ring * 2, UINT16_C(0x781f));
    p4_draw_fill_circle(surface, 160, 127, 38, UINT16_C(0x4208));
    p4_draw_fill_circle(surface, 160, 127, 30, UINT16_C(0x82a0));
    p4_draw_fill_rect(surface, 126, 126, 68, 11, UINT16_C(0x39e7));
    p4_draw_fill_rect(surface, 137, 130, 46, 2, UINT16_C(0xfd20));
}

static void draw_signal_header(p4_game_surface_t *surface,
                               const byte_buddy_state_t *state,
                               const char *title, size_t title_length)
{
    p4_draw_fill_rect(surface, 0, 0, 320, 25, UINT16_C(0x000b));
    draw_touch_button(surface, 4, 3, 52, 19, "BACK", 4U,
                      UINT16_C(0x07ff), false);
    p4_draw_text(surface, 66, 8, title, UINT16_C(0x07ff), 1U,
                 title_length);
    p4_draw_text(surface, 160, 8, "LINK", UINT16_C(0x7bef), 1U, 4U);
    draw_number(surface, 190, 8, state->signal_consumed_count,
                signal_color(state));
    const byte_buddy_battle_stats_t stats = current_battle_stats(state);
    p4_draw_text(surface, 215, 8, "LV", UINT16_C(0x7bef), 1U, 2U);
    draw_number(surface, 232, 8, stats.level, UINT16_C(0xffff));
    p4_draw_text(surface, 263, 8, "C", UINT16_C(0xffe0), 1U, 1U);
    draw_number(surface, 274, 8, state->coins, UINT16_C(0xffff));
}

static void draw_signal_list(p4_game_surface_t *surface,
                             const byte_buddy_state_t *state)
{
    draw_signal_header(surface, state, "SIGNAL HUNT", 11U);
    p4_draw_fill_rect(surface, 0, 25, 320, 175, UINT16_C(0x080f));
    if (state->signal_request_busy_ms != 0U &&
        state->signal_snapshot.status != P4_GAME_SIGNAL_READY) {
        p4_draw_text(surface, 78, 79, "SCANNER BUSY - TRY AGAIN",
                     UINT16_C(0xffe0), 1U, 24U);
        p4_draw_text(surface, 72, 97, "PRESS SCAN AFTER COOLDOWN",
                     UINT16_C(0x9cf3), 1U,
                     sizeof("PRESS SCAN AFTER COOLDOWN") - 1U);
    } else if (state->signal_snapshot.status == P4_GAME_SIGNAL_SCANNING) {
        p4_draw_text(surface, 108, 88, "SCANNING CITY",
                     UINT16_C(0x07ff), 1U, 13U);
    } else if (state->signal_snapshot.status == P4_GAME_SIGNAL_IDLE) {
        p4_draw_text(surface, 86, 88, "PRESS SCAN TO RETRY",
                     UINT16_C(0x07ff), 1U,
                     sizeof("PRESS SCAN TO RETRY") - 1U);
    } else if (state->signal_snapshot.status != P4_GAME_SIGNAL_READY) {
        p4_draw_text(surface, 91, 76, "SIGNAL RADIO OFFLINE",
                     UINT16_C(0xf81f), 1U, 20U);
        p4_draw_text(surface, 74, 94, "NO NETWORK DATA IS EXPOSED",
                     UINT16_C(0x7bef), 1U, 26U);
    } else {
        const uint8_t page = byte_buddy_signal_clamp_page(
            state->signal_page, state->signal_snapshot.count);
        const uint8_t first = (uint8_t)(
            page * BYTE_BUDDY_SIGNAL_PAGE_ROWS);
        const uint8_t remaining = state->signal_snapshot.count > first
            ? (uint8_t)(state->signal_snapshot.count - first) : 0U;
        const uint8_t rows = remaining < BYTE_BUDDY_SIGNAL_PAGE_ROWS
            ? remaining : BYTE_BUDDY_SIGNAL_PAGE_ROWS;
        for (uint8_t row = 0U; row < rows; ++row) {
            const uint8_t index = (uint8_t)(first + row);
            const p4_game_signal_t *const signal =
                &state->signal_snapshot.results[index];
            const byte_buddy_signal_profile_t profile =
                byte_buddy_signal_profile(signal->token, signal->rssi_dbm);
            const int top = 32 + (int)row * 25;
            const uint16_t color = signal_color_for_hue(profile.hue);
            p4_draw_fill_rect(surface, 4, top, 312, 22,
                              row % 2U == 0U ? UINT16_C(0x1025)
                                              : UINT16_C(0x181f));
            p4_draw_rect(surface, 4, top, 312, 22, color);
            if (state->controller_active &&
                row == state->signal_focus_row) {
                p4_draw_rect(surface, 5, top + 1, 310, 20,
                             UINT16_C(0xffff));
            }
            draw_signal_seed(
                surface, state, signal->token,
                signal->channel, signal->flags,
                17, top + 11, 21U,
                state->animation_ms + (uint32_t)index * 70U);
            p4_draw_text(surface, 29, top + 7, signal->label,
                         UINT16_C(0xffff), 1U,
                         signal_label_length(signal->label, 17U));
            draw_signal_meter(surface, 139, top + 7, 66,
                              profile.strength, color);
            draw_rssi(surface, 211, top + 7, signal->rssi_dbm,
                      UINT16_C(0xbdf7));
            if (signal_consumed(state, signal->token)) {
                p4_draw_text(surface, 270, top + 7, "EATEN",
                             UINT16_C(0x7bef), 1U, 5U);
            } else if (!byte_buddy_signal_collection_has_room(
                           state->signal_consumed_count)) {
                p4_draw_text(surface, 270, top + 7, "FULL",
                             UINT16_C(0xf81f), 1U, 4U);
            } else {
                p4_draw_text(surface, 270, top + 7, "+",
                             UINT16_C(0xffe0), 1U, 1U);
                draw_number(surface, 278, top + 7,
                            profile.reward_coins, UINT16_C(0xffe0));
            }
        }
        if (rows != 0U && signal_is_simulated(
                &state->signal_snapshot.results[first])) {
            p4_draw_text(surface, 241, 27, "SIM DATA",
                         UINT16_C(0xf81f), 1U, 8U);
        }
        if (state->controller_active) {
            p4_draw_text(surface, 60, 27, "DPAD+A SELECT  START SCAN",
                         UINT16_C(0x9cf3), 1U, 25U);
        }
    }
    if (state->signal_reward_ms != 0U) {
        const byte_buddy_signal_lineage_t lineage = current_lineage(state);
        size_t badge_length = 0U;
        const char *const badge = lineage_badge_name(
            lineage, &badge_length);
        p4_draw_fill_rect(surface, 72, 65, 176, 66, UINT16_C(0x000b));
        p4_draw_rect(surface, 72, 65, 176, 66, signal_color(state));
        p4_draw_text(surface, 104, 74, "LINEAGE UPDATED",
                     UINT16_C(0xffff), 1U, 15U);
        p4_draw_text(surface, 108, 91, badge,
                     signal_color(state), 1U, badge_length);
        p4_draw_text(surface, 183, 91, "DNA", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 207, 91,
                    lineage.diversity, UINT16_C(0xffff));
        p4_draw_text(surface, 88, 106, "BAND", UINT16_C(0x7bef),
                     1U, 4U);
        draw_number(surface, 122, 106, lineage.channel_families,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 132, 106, "/4", UINT16_C(0x7bef),
                     1U, 2U);
        p4_draw_text(surface, 165, 106, "HUE", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 191, 106,
                    lineage.hue_diversity < 6U
                        ? lineage.hue_diversity : 6U,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 201, 106, "/6", UINT16_C(0x7bef),
                     1U, 2U);
        p4_draw_text(surface, 120, 117, "MIX", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 147, 117,
                    lineage.part_diversity < 14U
                        ? lineage.part_diversity : 14U,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 163, 117, "/14", UINT16_C(0x7bef),
                     1U, 3U);
    }
    const uint8_t pages = byte_buddy_signal_page_count(
        state->signal_snapshot.count);
    const uint8_t page = byte_buddy_signal_clamp_page(
        state->signal_page, state->signal_snapshot.count);
    draw_touch_button(surface, 4, 162, 58, 33, "PREV", 4U,
                      page == 0U ? UINT16_C(0x4208) : UINT16_C(0x07ff),
                      false);
    draw_touch_button(surface, 64, 162, 192, 33,
                      state->signal_request_busy_ms != 0U
                          ? "SCANNER BUSY" : pages == 1U ? "SCAN CITY" :
                          page == 0U ? "SCAN 1/2" : "SCAN 2/2",
                      state->signal_request_busy_ms != 0U
                          ? 12U : pages == 1U ? 9U : 8U,
                      state->signal_request_busy_ms != 0U
                          ? UINT16_C(0xffe0) : UINT16_C(0x07ff),
                      state->signal_snapshot.status == P4_GAME_SIGNAL_SCANNING);
    draw_touch_button(surface, 258, 162, 58, 33, "NEXT", 4U,
                      page + 1U >= pages
                          ? UINT16_C(0x4208) : UINT16_C(0x07ff),
                      false);
}

static void draw_signal_tracker(p4_game_surface_t *surface,
                                const byte_buddy_state_t *state)
{
    draw_signal_header(surface, state, "TRACK SIGNAL", 12U);
    p4_draw_fill_rect(surface, 0, 25, 320, 175, UINT16_C(0x080f));
    const p4_game_signal_t *const signal = selected_signal(state);
    if (signal == NULL) {
        p4_draw_text(surface, 98, 88, "SIGNAL MOVED AWAY",
                     UINT16_C(0xf81f), 1U, 17U);
    } else {
        const byte_buddy_signal_profile_t profile =
            byte_buddy_signal_profile(signal->token, signal->rssi_dbm);
        const byte_buddy_signal_genome_t genome =
            byte_buddy_signal_genome(signal->token);
        const uint16_t color = signal_color_for_hue(profile.hue);
        const int rings = 8 + (int)profile.strength / 8;
        for (int ring = 0; ring < 3; ++ring) {
            const int radius = rings + ring * 7 +
                (int)((state->animation_ms / 140U) % 4U);
            p4_draw_rect(surface, 83 - radius, 82 - radius,
                         radius * 2 + 1, radius * 2 + 1, color);
        }
        draw_signal_seed(
            surface, state, signal->token,
            signal->channel, signal->flags,
            83, 82, 54U, state->animation_ms);
        draw_dragon(surface, state, 245, 55);
        p4_draw_text(surface, 14, 31, "FORM", UINT16_C(0x7bef),
                     1U, 4U);
        draw_number(surface, 45, 31, byte_buddy_signal_form_id(
                        genome.recipe_id, signal->channel, signal->flags),
                    UINT16_C(0xffff));
        p4_draw_text(surface, 121, 39, signal->label,
                     UINT16_C(0xffff), 1U,
                     signal_label_length(signal->label, 18U));
        p4_draw_text(surface, 121, 55,
                     signal->rssi_dbm >= -49 ? "VERY HOT" :
                     signal->rssi_dbm >= SIGNAL_HUNT_UNLOCK_RSSI
                         ? "BATTLE READY" : signal->rssi_dbm >= -78
                             ? "GETTING WARM" : "COLD",
                     signal->rssi_dbm >= SIGNAL_HUNT_UNLOCK_RSSI
                         ? UINT16_C(0xffe0) : UINT16_C(0x07ff),
                     1U, signal->rssi_dbm >= -49 ? 8U :
                         signal->rssi_dbm >= SIGNAL_HUNT_UNLOCK_RSSI
                             ? 12U : signal->rssi_dbm >= -78 ? 12U : 4U);
        p4_draw_text(surface, 121, 72, "RSSI", UINT16_C(0x7bef), 1U, 4U);
        draw_rssi(surface, 153, 72, signal->rssi_dbm, UINT16_C(0xffff));
        p4_draw_text(surface, 121, 89, "REWARD", UINT16_C(0x7bef), 1U, 6U);
        draw_number(surface, 166, 89, profile.reward_coins,
                    UINT16_C(0xffe0));
        p4_draw_text(surface, 121, 105,
                     signal_is_simulated(signal) ? "SIMULATED RSSI" :
                     state->signal_samples < 2U ? "LIVE RSSI ACQUIRING" :
                     state->signal_trend_db >= 3 ? "CLOSER" :
                     state->signal_trend_db <= -3 ? "FARTHER" : "STEADY",
                     signal_is_simulated(signal) ? UINT16_C(0xf81f) :
                         UINT16_C(0x07e0),
                     1U, signal_is_simulated(signal) ? 14U :
                         state->signal_samples < 2U ? 19U :
                         state->signal_trend_db >= 3 ? 6U :
                         state->signal_trend_db <= -3 ? 7U : 6U);
        draw_signal_meter(surface, 25, 126, 270, profile.strength, color);
        p4_draw_text(surface, state->controller_active ? 66 : 90, 138,
                     state->controller_active
                         ? "A BATTLE  START SCAN  B BACK"
                         : "AUTO REFRESH - WALK AROUND",
                     UINT16_C(0xbdf7), 1U,
                     state->controller_active
                         ? sizeof("A BATTLE  START SCAN  B BACK") - 1U
                         : sizeof("AUTO REFRESH - WALK AROUND") - 1U);
    }
    draw_touch_button(surface, 4, 162, 188, 33,
                      state->signal_request_busy_ms != 0U
                          ? "SCANNER BUSY" : "RESCAN NOW",
                      state->signal_request_busy_ms != 0U ? 12U : 10U,
                      state->signal_request_busy_ms != 0U
                          ? UINT16_C(0xffe0) : UINT16_C(0x07ff),
                      state->signal_snapshot.status == P4_GAME_SIGNAL_SCANNING);
    const bool full = signal != NULL &&
        !signal_consumed(state, signal->token) &&
        !byte_buddy_signal_collection_has_room(
            state->signal_consumed_count);
    const bool ready = signal != NULL && !full &&
        signal->rssi_dbm >= SIGNAL_HUNT_UNLOCK_RSSI;
    draw_touch_button(surface, 198, 162, 118, 33,
                      full ? "LINEAGE FULL" : ready ? "BATTLE" : "TOO FAR",
                      full ? 12U : ready ? 6U : 7U,
                      UINT16_C(0xfd20), false);
}

static void draw_signal_path_line(
    p4_game_surface_t *surface,
    int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    const int step_x = x0 < x1 ? 1 : -1;
    int dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    const int step_y = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        p4_draw_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += step_x;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

static void draw_signal_weave_arena(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    const p4_game_signal_t *signal,
    byte_buddy_signal_profile_t profile,
    byte_buddy_signal_genome_t genome,
    byte_buddy_signal_weave_rules_t rules)
{
    const uint16_t color = signal_color_for_hue(profile.hue);
    draw_signal_seed(
        surface, state, signal->token,
        signal->channel, signal->flags,
        57, 82, 44U, state->animation_ms);
    draw_dragon(surface, state, 266, 58);

    const uint8_t completed_segments = state->signal_weave_step < 7U
        ? state->signal_weave_step : 6U;
    for (uint8_t step = 1U; step <= completed_segments; ++step) {
        const byte_buddy_signal_weave_node_t from =
            byte_buddy_signal_weave_node(genome, (uint8_t)(step - 1U));
        const byte_buddy_signal_weave_node_t to =
            byte_buddy_signal_weave_node(genome, step);
        draw_signal_path_line(
            surface, from.x, from.y, to.x, to.y, color);
    }
    for (uint8_t step = 0U; step < 6U; ++step) {
        const byte_buddy_signal_weave_node_t node =
            byte_buddy_signal_weave_node(genome, step);
        p4_draw_fill_circle(surface, node.x, node.y, 5,
                            UINT16_C(0x2104));
        p4_draw_rect(surface, (int)node.x - 8, (int)node.y - 8,
                     17, 17, UINT16_C(0x4208));
    }
    const uint8_t completed_nodes = state->signal_weave_step < 6U
        ? state->signal_weave_step : 6U;
    for (uint8_t step = 0U; step < completed_nodes; ++step) {
        const byte_buddy_signal_weave_node_t node =
            byte_buddy_signal_weave_node(genome, step);
        p4_draw_fill_circle(surface, node.x, node.y, 5, color);
    }
    if (state->signal_weave_step < rules.required_locks) {
        const byte_buddy_signal_weave_node_t active =
            byte_buddy_signal_weave_node(genome, state->signal_weave_step);
        const int pulse = (int)((state->animation_ms / 120U) % 3U);
        (void)draw_signal_layer_scaled(
            surface, state, 4U + genome.halo, genome.hue,
            active.x, active.y, 28U);
        (void)draw_signal_layer_scaled(
            surface, state, 8U + genome.sigil, genome.hue,
            active.x, active.y, 28U);
        if (state->signal_hit_ms != 0U) {
            (void)draw_signal_layer_scaled(
                surface, state, 12U + genome.aura, genome.hue,
                active.x, active.y, 32U);
        }
        p4_draw_rect(surface,
                     (int)active.x - 13 - pulse,
                     (int)active.y - 13 - pulse,
                     27 + pulse * 2, 27 + pulse * 2,
                     state->signal_weave_settle_ms != 0U
                         ? UINT16_C(0xffff) : color);
    }
    if (state->controller_active) {
        const int cursor_x = p4_q16_to_int_round(
            state->signal_cursor_x_q16);
        const int cursor_y = p4_q16_to_int_round(
            state->signal_cursor_y_q16);
        p4_draw_rect(surface, cursor_x - 5, cursor_y - 5,
                     11, 11, UINT16_C(0xffff));
        p4_draw_fill_rect(surface, cursor_x - 8, cursor_y,
                          17, 1, color);
        p4_draw_fill_rect(surface, cursor_x, cursor_y - 8,
                          1, 17, color);
    }
}

static void draw_signal_battle(p4_game_surface_t *surface,
                               const byte_buddy_state_t *state)
{
    const bool weave = state->signal_battle_pattern ==
        BYTE_BUDDY_BATTLE_RESONANCE_WEAVE;
    draw_signal_header(surface, state,
                       weave ? "RESONANCE" : "SIGNAL BATTLE",
                       weave ? 9U : 13U);
    p4_draw_fill_rect(surface, 0, 25, 320, 175, UINT16_C(0x080f));
    const p4_game_signal_t *const signal = selected_signal(state);
    if (signal == NULL) {
        return;
    }
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(signal->token, signal->rssi_dbm);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(signal->token);
    const byte_buddy_signal_weave_rules_t rules =
        byte_buddy_signal_weave_rules(
            genome, profile.strength,
            state->upgrades[BYTE_BUDDY_UPGRADE_MAGNET]);
    const uint16_t color = signal_color_for_hue(profile.hue);
    if (weave) {
        draw_signal_weave_arena(
            surface, state, signal, profile, genome, rules);
    } else {
        const int pulse = state->signal_hit_ms != 0U ? 6 : 0;
        const int radius = 13 + (int)profile.strength / 8 + pulse;
        for (int ring = 0; ring < 3; ++ring) {
            const int r = radius + ring * 7;
            p4_draw_rect(surface, 79 - r, 84 - r, r * 2 + 1, r * 2 + 1,
                         ring == 1 ? UINT16_C(0xf81f) : color);
        }
        draw_signal_seed(
            surface, state, signal->token,
            signal->channel, signal->flags,
            79, 84, 60U, state->animation_ms);
        draw_dragon(surface, state, 246, 58);
    }
    p4_draw_text(surface, 13, 31, weave ? "WEAVE" : "STRONG",
                 weave ? color : UINT16_C(0xf81f), 1U,
                 weave ? 5U : 6U);
    draw_rssi(surface, 58, 31, signal->rssi_dbm, UINT16_C(0xffff));
    p4_draw_text(surface, 230, 31, "REWARD", UINT16_C(0x7bef), 1U, 6U);
    draw_number(surface, 277, 31, profile.reward_coins,
                UINT16_C(0xffe0));
    p4_draw_text(surface, 14, 134, "SIGNAL HP", UINT16_C(0xbdf7), 1U, 9U);
    p4_draw_rect(surface, 82, 134, 112, 8, UINT16_C(0x7bef));
    p4_draw_fill_rect(surface, 83, 135,
                      state->signal_battle_max_hp == 0U ? 0 :
                          (int)((uint32_t)state->signal_battle_hp * 110U /
                                state->signal_battle_max_hp),
                      6, UINT16_C(0xf81f));
    p4_draw_text(surface, 211, 134, "TIME", UINT16_C(0xbdf7), 1U, 4U);
    const uint32_t limit = signal_battle_duration_ms(state) +
        state->signal_battle_bonus_ms;
    const uint32_t remaining = state->signal_battle_elapsed_ms >= limit
        ? 0U : limit - state->signal_battle_elapsed_ms;
    p4_draw_rect(surface, 244, 134, 62, 8, UINT16_C(0x7bef));
    p4_draw_fill_rect(surface, 245, 135,
                      (int)(remaining * 60U / limit), 6,
                      UINT16_C(0xffe0));
    const char *const battle_hint = state->controller_active
        ? (weave ? "HOLD A + DPAD TO GLIDE" :
                   "A STRIKE - START GUARD")
        : (weave ? "PRESS, HOLD, THEN GLIDE" :
                   "TAP FAST - GUARD BUYS TIME");
    const size_t battle_hint_length = state->controller_active
        ? (weave ? 22U : 22U) : (weave ? 23U : 26U);
    p4_draw_text(surface,
                 state->controller_active ? 84 : weave ? 91 : 82, 149,
                 battle_hint,
                 UINT16_C(0xbdf7), 1U, battle_hint_length);
    if (weave) {
        const uint32_t threshold_units = (uint32_t)rules.hold_ms * 2U;
        p4_draw_fill_rect(surface, 4, 162, 188, 33, UINT16_C(0x1025));
        p4_draw_rect(surface, 4, 162, 188, 33, color);
        p4_draw_text(surface, 12, 167, "RUNE CHARGE",
                     UINT16_C(0xffff), 1U, 11U);
        p4_draw_rect(surface, 12, 182, 128, 7, UINT16_C(0x7bef));
        p4_draw_fill_rect(surface, 13, 183,
                          threshold_units == 0U ? 0 : (int)(
                              state->signal_weave_charge_units * 126U /
                              threshold_units),
                          5, color);
        p4_draw_text(surface, 150, 167, "STEP", UINT16_C(0x7bef),
                     1U, 4U);
        draw_number(surface, 151, 182,
                    state->signal_weave_step < rules.required_locks
                        ? (uint32_t)state->signal_weave_step + 1U
                        : rules.required_locks,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 165, 182, "/", UINT16_C(0x7bef), 1U, 1U);
        draw_number(surface, 174, 182, rules.required_locks,
                    UINT16_C(0xffff));
    } else {
        draw_touch_button(surface, 4, 162, 188, 33,
                          "PULSE STRIKE", 12U,
                          UINT16_C(0x07ff), state->signal_hit_ms != 0U);
    }
    draw_touch_button(surface, 198, 162, 118, 33, "AURA GUARD", 10U,
                      UINT16_C(0xffe0), false);
    draw_number(surface, 300, 166, state->signal_guard_charges,
                UINT16_C(0x0000));
}

static void draw_signal_hunt(p4_game_surface_t *surface,
                             const byte_buddy_state_t *state)
{
    switch ((byte_buddy_signal_view_t)state->signal_view) {
    case BYTE_BUDDY_SIGNAL_TRACKER:
        draw_signal_tracker(surface, state);
        break;
    case BYTE_BUDDY_SIGNAL_BATTLE:
        draw_signal_battle(surface, state);
        break;
    case BYTE_BUDDY_SIGNAL_LIST:
    default:
        draw_signal_list(surface, state);
        break;
    }
}

static void draw_lineage_panel(p4_game_surface_t *surface,
                               const byte_buddy_state_t *state)
{
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    draw_signal_header(surface, state, "DRAGON GENOME", 13U);
    p4_draw_fill_rect(surface, 0, 25, 320, 175, UINT16_C(0x080f));
    p4_draw_text(surface, 12, 32, "CURRENT LINEAGE",
                 UINT16_C(0x7bef), 1U, 15U);
    p4_draw_text(surface, 12, 44, s_lineage_names[lineage.tier],
                 signal_color(state), 1U,
                 s_lineage_name_lengths[lineage.tier]);
    if (lineage.resonance > BYTE_BUDDY_RESONANCE_NONE &&
        lineage.resonance < BYTE_BUDDY_RESONANCE_COUNT) {
        p4_draw_text(surface, 12, 57,
                     s_resonance_names[lineage.resonance],
                     signal_color_for_hue(lineage.secondary_hue), 1U,
                     s_resonance_name_lengths[lineage.resonance]);
    }
    draw_dragon(surface, state, 68, 96);
    const uint8_t lineage_flags = (uint8_t)(
        (lineage.shielded ? P4_GAME_SIGNAL_PROTECTED : 0U) |
        (lineage.phantom ? P4_GAME_SIGNAL_HIDDEN : 0U));
    draw_signal_seed(
        surface, state,
        state->signal_entropy == 0U
            ? UINT64_C(0x5349474e414c) : state->signal_entropy,
        0U, lineage_flags,
        118, 119, 28U, state->animation_ms);

    if (lineage.tier == BYTE_BUDDY_LINEAGE_DORMANT) {
        p4_draw_text(surface, 146, 47, "FIND YOUR FIRST SIGNAL",
                     UINT16_C(0x07ff), 1U, 22U);
        p4_draw_text(surface, 146, 67, "EACH OPAQUE TOKEN ADDS",
                     UINT16_C(0x9cf3), 1U, 22U);
        p4_draw_text(surface, 146, 79, "CORE HALO SIGIL AURA",
                     UINT16_C(0x9cf3), 1U, 21U);
        p4_draw_text(surface, 146, 99, "VARIETY BUILDS DNA",
                     UINT16_C(0xffe0), 1U, 18U);
    } else {
        p4_draw_text(surface, 146, 32, "FAMILY", UINT16_C(0x7bef),
                     1U, 6U);
        p4_draw_text(surface, 207, 32,
                     s_lineage_family_names[lineage.family & 3U],
                     UINT16_C(0xffff), 1U, 6U);
        p4_draw_text(surface, 146, 47, "HALO", UINT16_C(0x7bef),
                     1U, 4U);
        p4_draw_text(surface, 207, 47,
                     s_lineage_halo_names[lineage.halo & 3U],
                     signal_color_for_hue(lineage.primary_hue), 1U, 5U);
        p4_draw_text(surface, 146, 62, "SIGIL", UINT16_C(0x7bef),
                     1U, 5U);
        p4_draw_text(surface, 207, 62,
                     s_lineage_mark_names[lineage.marking & 3U],
                     UINT16_C(0xffff), 1U, 5U);
        p4_draw_text(surface, 146, 77, "AURA", UINT16_C(0x7bef),
                     1U, 4U);
        p4_draw_text(surface, 207, 77,
                     s_lineage_aura_names[lineage.aura & 3U],
                     signal_color_for_hue(lineage.secondary_hue), 1U, 5U);
        p4_draw_text(surface, 146, 92, "HUE", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 207, 92, lineage.primary_hue,
                    signal_color_for_hue(lineage.primary_hue));
        if (lineage.tier >= BYTE_BUDDY_LINEAGE_MYTHIC) {
            p4_draw_text(surface, 221, 92, "+", UINT16_C(0x7bef),
                         1U, 1U);
            draw_number(surface, 231, 92, lineage.secondary_hue,
                        signal_color_for_hue(lineage.secondary_hue));
        }
        p4_draw_text(surface, 146, 107, "BAND", UINT16_C(0x7bef),
                     1U, 4U);
        draw_number(surface, 180, 107, lineage.channel_families,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 190, 107, "/4", UINT16_C(0x7bef),
                     1U, 2U);
        p4_draw_text(surface, 222, 107, "HUE", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 248, 107,
                    lineage.hue_diversity < 6U
                        ? lineage.hue_diversity : 6U,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 258, 107, "/6", UINT16_C(0x7bef),
                     1U, 2U);
        p4_draw_text(surface, 146, 117, "MIX", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 174, 117,
                    lineage.part_diversity < 14U
                        ? lineage.part_diversity : 14U,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 190, 117, "/14", UINT16_C(0x7bef),
                     1U, 3U);
        if (lineage.shielded) {
            p4_draw_text(surface, 146, 126, "WARD",
                         UINT16_C(0x07ff), 1U, 4U);
        }
        if (lineage.phantom) {
            p4_draw_text(surface, 180, 126, "GHOST",
                         UINT16_C(0xf81f), 1U, 5U);
        }
        if ((lineage.adaptations &
             BYTE_BUDDY_ADAPTATION_WIDEBAND) != 0U) {
            p4_draw_text(surface, 222, 126, "WIDE",
                         UINT16_C(0xffe0), 1U, 4U);
        }
        if ((lineage.adaptations &
             BYTE_BUDDY_ADAPTATION_PRISMATIC) != 0U) {
            p4_draw_text(surface, 146, 133, "PRISM",
                         signal_color_for_hue(lineage.secondary_hue),
                         1U, 5U);
        }
        if ((lineage.adaptations &
             BYTE_BUDDY_ADAPTATION_CHIMERA) != 0U) {
            p4_draw_text(surface, 188, 133, "CHIMERA",
                         signal_color_for_hue(lineage.primary_hue),
                         1U, 7U);
        }
    }

    p4_draw_rect(surface, 4, 140, 312, 24, UINT16_C(0x4208));
    if (lineage.resonance >= BYTE_BUDDY_RESONANCE_ETERNAL) {
        p4_draw_text(surface, 72, 149, "ETERNAL GENOME COMPLETE",
                     UINT16_C(0xffe0), 1U, 23U);
    } else {
        const bool next_is_resonance =
            lineage.tier >= BYTE_BUDDY_LINEAGE_MYTHIC;
        const uint8_t next_rank = next_is_resonance
            ? (uint8_t)(lineage.resonance + 1U)
            : (uint8_t)(lineage.tier + 1U);
        const char *const next_name = next_is_resonance
            ? s_resonance_names[next_rank] : s_lineage_names[next_rank];
        const uint8_t next_name_length = next_is_resonance
            ? s_resonance_name_lengths[next_rank]
            : s_lineage_name_lengths[next_rank];
        const uint8_t next_links = next_is_resonance
            ? s_resonance_link_requirements[next_rank]
            : s_lineage_link_requirements[next_rank];
        const uint8_t next_diversity = next_is_resonance
            ? s_resonance_diversity_requirements[next_rank]
            : s_lineage_diversity_requirements[next_rank];
        p4_draw_text(surface, 12, 145, "NEXT", UINT16_C(0x7bef),
                     1U, 4U);
        p4_draw_text(surface, 46, 145, next_name,
                     signal_color(state), 1U, next_name_length);
        p4_draw_text(surface, 112, 145, "LINK", UINT16_C(0x7bef),
                     1U, 4U);
        draw_number(surface, 146, 145, state->signal_consumed_count,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 160, 145, "/", UINT16_C(0x7bef), 1U, 1U);
        draw_number(surface, 170, 145, next_links,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 201, 145, "DNA", UINT16_C(0x7bef),
                     1U, 3U);
        if (next_diversity == 0U) {
            p4_draw_text(surface, 225, 145, "OPEN",
                         UINT16_C(0xffff), 1U, 4U);
        } else {
            draw_number(surface, 225, 145, lineage.diversity,
                        UINT16_C(0xffff));
            p4_draw_text(surface, 239, 145, "/", UINT16_C(0x7bef),
                         1U, 1U);
            draw_number(surface, 249, 145, next_diversity,
                        UINT16_C(0xffff));
        }
        const bool eternal_fallback = next_is_resonance &&
            next_rank == BYTE_BUDDY_RESONANCE_ETERNAL &&
            state->signal_consumed_count >= next_links &&
            lineage.diversity < next_diversity;
        p4_draw_text(
            surface, 12, 155,
            eternal_fallback
                ? "48 LINKS GUARANTEES ETERNAL"
                : next_is_resonance
                    ? "DEEP VARIETY ADDS ORBITS"
                    : "NEW SIGNAL VARIETY UNLOCKS TRAITS",
            UINT16_C(0x9cf3), 1U,
            eternal_fallback
                ? sizeof("48 LINKS GUARANTEES ETERNAL") - 1U
                : next_is_resonance
                    ? sizeof("DEEP VARIETY ADDS ORBITS") - 1U
                    : sizeof("NEW SIGNAL VARIETY UNLOCKS TRAITS") - 1U);
    }
    draw_touch_button(surface, 4, 168, 312, 28,
                      "BACK TO BUDDY", 13U,
                      UINT16_C(0x07ff), false);
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (context == NULL || context->state == NULL || !p4_surface_valid(surface)) {
        return false;
    }
    const byte_buddy_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x000b));
    if (state->signal_hunt) {
        draw_signal_hunt(surface, state);
        return true;
    }
    if (state->lineage_panel) {
        draw_lineage_panel(surface, state);
        return true;
    }
    draw_signal_city(surface, state);
    p4_draw_fill_rect(surface, 0, 24, P4_GAME_SURFACE_WIDTH, 1,
                      signal_color(state));
    draw_touch_button(surface, 4, 3, 40, 18, "EXIT", 4U,
                      UINT16_C(0x7bef), false);
    if (state->upgrade_shop) {
        if (state->style_shop) {
            draw_style_shop(surface, state);
        } else {
            draw_upgrade_shop(surface, state);
        }
        return true;
    }
    if (state->mini_game) {
        draw_play_game(surface, state);
        p4_game_feedback_draw_audio_effect(
            surface, &state->audio, state->star_x,
            p4_q16_to_int_round(state->star_y_q16));
    } else {
        p4_draw_text(surface, 52, 7, "BYTE BUDDY",
                     UINT16_C(0xffff), 1U, 10U);
        const byte_buddy_battle_stats_t stats = current_battle_stats(state);
        p4_draw_text(surface, 127, 7, "LV", UINT16_C(0x7bef), 1U, 2U);
        draw_number(surface, 144, 7, stats.level, UINT16_C(0xffff));
        p4_draw_text(surface, 169, 7, mood(state), element_color(state),
                     1U, 10U);
        if (!draw_art_frame_scaled(
                surface, state, STAR_CATCHER_REWARD_SHEET, 0U,
                267, 11, 12U)) {
            p4_draw_text(surface, 263, 7, "C", UINT16_C(0xffe0), 1U, 1U);
        }
        draw_number(surface, 274, 7, state->coins, UINT16_C(0xffff));
        draw_bar(surface, 34, "FULL", state->hunger, UINT16_C(0x07E0));
        draw_bar(surface, 44, "JOY", state->joy, UINT16_C(0xFFE0));
        draw_bar(surface, 54, "CLEAN", state->hygiene, UINT16_C(0x07FF));
        draw_bar(surface, 64, "ENERGY", state->energy, UINT16_C(0xF81F));
        draw_growth_panel(surface, state);
        p4_game_feedback_draw_audio_effect(
            surface, &state->audio, 160, 96);
        draw_dragon(surface, state, 160, 58);
        const byte_buddy_signal_lineage_t lineage = current_lineage(state);
        if (lineage.tier == BYTE_BUDDY_LINEAGE_DORMANT) {
            p4_draw_text(surface, 128, 126, "TAP TO PET",
                         UINT16_C(0x9cf3), 1U, 10U);
        } else {
            size_t length = 0U;
            const char *const badge = lineage_badge_name(
                lineage, &length);
            const int label_x = 160 - (int)(length * 7U) / 2;
            p4_draw_text(surface, label_x, 126, badge,
                         signal_color(state), 1U, length);
        }
        draw_touch_button(surface, 4, 138, 74, 27, s_actions[ACTION_FEED],
                          4U, UINT16_C(0xfd20),
                          state->reaction == REACTION_FEED ||
                              (state->controller_active &&
                               state->selected_action == ACTION_FEED));
        draw_touch_button(surface, 82, 138, 74, 27, s_actions[ACTION_PLAY],
                          4U, UINT16_C(0xffe0),
                          state->reaction == REACTION_PLAY ||
                              (state->controller_active &&
                               state->selected_action == ACTION_PLAY));
        draw_touch_button(surface, 160, 138, 74, 27,
                          s_actions[ACTION_CLEAN], 5U, UINT16_C(0x07ff),
                          state->reaction == REACTION_CLEAN ||
                              (state->controller_active &&
                               state->selected_action == ACTION_CLEAN));
        draw_touch_button(surface, 238, 138, 78, 27,
                          s_actions[ACTION_REST], 4U, UINT16_C(0xf81f),
                          state->reaction == REACTION_REST ||
                              (state->controller_active &&
                               state->selected_action == ACTION_REST));
        draw_item_component_icon(surface, state, ACTION_FEED, 15, 151, 18U);
        draw_item_component_icon(surface, state, ACTION_PLAY, 93, 151, 18U);
        draw_item_component_icon(surface, state, ACTION_CLEAN, 171, 151, 18U);
        draw_item_component_icon(surface, state, ACTION_REST, 249, 151, 18U);
        draw_touch_button(surface, 4, 168, 142, 28, "SIGNAL HUNT", 11U,
                          UINT16_C(0x07ff), false);
        draw_signal_seed(
            surface, state, UINT64_C(0x5349474e414c),
            0U, 0U,
            17, 182, 22U, state->animation_ms);
        draw_touch_button(surface, 150, 168, 94, 28, "UPGRADES", 8U,
                          element_color(state), false);
        draw_touch_button(surface, 248, 168, 68, 28, "GENOME", 6U,
                          signal_color(state), false);
    }
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_byte_buddy_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(108),
    .id = "org.p4console.byte-buddy",
    .title = "BYTE BUDDY",
    .subtitle = "SIGNAL DRAGONS",
    .accent_rgb565 = UINT16_C(0xF81F),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM |
                             P4_GAME_CAP_STORAGE |
                             P4_GAME_CAP_SIGNAL_SCAN,
    .state_bytes = sizeof(byte_buddy_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
