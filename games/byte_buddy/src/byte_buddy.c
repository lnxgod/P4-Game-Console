// SPDX-License-Identifier: MIT
/*
 * Byte Buddy is an original dragon-raising virtual pet. Its bounded PixelLab
 * and ImageGen sprite sheets, compact frame bank, and required validated SD
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
#include "byte_buddy_save.h"

enum {
    STAT_MAX = 100,
    DECAY_INTERVAL_MS = 5000,
    MINI_GAME_DURATION_MS = 18000,
    SCENE_TRANSITION_MS = 240,
    MINI_GAME_INTRO_MS = 1200,
    MINI_GAME_READY_VISIBLE_MS =
        MINI_GAME_INTRO_MS - SCENE_TRANSITION_MS,
    MINI_GAME_SUMMARY_MS = 900,
    MINI_GAME_CATCH_Y = 126,
    MINI_GAME_STAR_START_Y = 38,
    MINI_GAME_EFFECT_DURATION_MS = 480,
    MINI_GAME_STAR_SPAWN_MS = 180,
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
    SIGNAL_BATTLE_INTRO_MS = 450,
    SIGNAL_ATTACK_TRAVEL_MS = 220,
    SIGNAL_ATTACK_IMPACT_MS = 280,
    SIGNAL_ATTACK_RECOVERY_MS = 420,
    SIGNAL_VICTORY_MS = 900,
    SIGNAL_DEFEAT_MS = 760,
    SIGNAL_RETREAT_MS = 520,
    SIGNAL_GUARD_FX_MS = 360,
    SIGNAL_PASSIVE_FX_MS = 520,
    EVOLUTION_FX_MS = REACTION_DURATION_MS,
    SHOP_FEEDBACK_MS = 650,
    SIGNAL_TRACK_REFRESH_MS = 1600,
    SIGNAL_REQUEST_BUSY_MS = 1200,
    SIGNAL_SCAN_TIMEOUT_MS = 5000,
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
    BYTE_BUDDY_SIGNAL_COUNTER_FX_SHEET = 0,
    BYTE_BUDDY_SIGNAL_OUTCOME_FX_SHEET = 1,
    BYTE_BUDDY_SIGNAL_PASSIVE_FX_SHEET = 2,
    BYTE_BUDDY_SIGNAL_SCAN_FX_SHEET = 3,
    BYTE_BUDDY_EVOLUTION_FX_SHEET = 4,
    DRAGON_RARE_SHEET = 5,
    BYTE_BUDDY_NEED_FX_SHEET = 6,
    BYTE_BUDDY_ACTIVITY_FX_SHEET = 7,
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
    BYTE_BUDDY_SIGNAL_CITY_SHEET = 25,
    BYTE_BUDDY_REACTION_FX_SHEET = 26,
    BYTE_BUDDY_LINEAGE_BADGE_SHEET = 27,
    BYTE_BUDDY_SIGNAL_ATTACK_SHEET = 28,
    BYTE_BUDDY_ENVIRONMENT_CHROME_SHEET = 29,
    BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET = 30,
    DRAGON_FEED_ACTION_SHEET = 31,
    DRAGON_PLAY_ACTION_SHEET = 32,
    DRAGON_CLEAN_ACTION_SHEET = 33,
    DRAGON_REST_ACTION_SHEET = 34,
    DRAGON_PET_ACTION_SHEET = 35,
    DRAGON_GROW_ACTION_SHEET = 36,
    DRAGON_SIGNAL_ACTION_SHEET = 37,
    DRAGON_HATCH_NEBULA_SHEET = 38,
    DRAGON_HATCH_SUNGOLD_SHEET = 39,
    DRAGON_HATCH_JADE_SHEET = 40,
    DRAGON_HATCH_GLACIER_SHEET = 41,
    DRAGON_EXTENDED_SHEET_COUNT = 42,
    ACHIEVEMENT_FIRST_CARE = UINT32_C(1) << 0U,
    ACHIEVEMENT_CLEAN = UINT32_C(1) << 1U,
    ACHIEVEMENT_PLAY = UINT32_C(1) << 2U,
    ACHIEVEMENT_GROW = UINT32_C(1) << 3U,
    ACHIEVEMENT_FIRST_SIGNAL = UINT32_C(1) << 4U,
    ACHIEVEMENT_SIGNAL_CHORUS = UINT32_C(1) << 5U,
    ACHIEVEMENT_MYTHIC_LINEAGE = UINT32_C(1) << 6U,
    ACHIEVEMENT_ETERNAL_LINEAGE = UINT32_C(1) << 7U,
};

_Static_assert(DRAGON_FEED_ACTION_SHEET ==
                   BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET + REACTION_FEED,
               "Feed action sheet must follow the signature FX sheet");
_Static_assert(DRAGON_PLAY_ACTION_SHEET ==
                   BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET + REACTION_PLAY,
               "Play action sheet order changed");
_Static_assert(DRAGON_CLEAN_ACTION_SHEET ==
                   BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET + REACTION_CLEAN,
               "Clean action sheet order changed");
_Static_assert(DRAGON_REST_ACTION_SHEET ==
                   BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET + REACTION_REST,
               "Rest action sheet order changed");
_Static_assert(DRAGON_PET_ACTION_SHEET ==
                   BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET + REACTION_PET,
               "Pet action sheet order changed");
_Static_assert(DRAGON_GROW_ACTION_SHEET ==
                   BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET + REACTION_GROW,
               "Grow action sheet order changed");
_Static_assert(DRAGON_SIGNAL_ACTION_SHEET ==
                   BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET + REACTION_SIGNAL,
               "Signal action sheet order changed");
_Static_assert(DRAGON_HATCH_GLACIER_SHEET -
                   DRAGON_HATCH_NEBULA_SHEET + 1 ==
                   BYTE_BUDDY_MORPH_COUNT,
               "Hatch sheets must cover every morph contiguously");
_Static_assert(EVOLUTION_FX_MS == REACTION_DURATION_MS,
               "Grow and hatch FX must span the full action timeline");

typedef enum {
    ACTION_FEED = 0,
    ACTION_PLAY,
    ACTION_CLEAN,
    ACTION_REST,
} buddy_action_t;

_Static_assert((unsigned)ACTION_FEED == (unsigned)BYTE_BUDDY_CARE_FEED,
               "care action values must stay aligned");
_Static_assert((unsigned)ACTION_PLAY == (unsigned)BYTE_BUDDY_CARE_PLAY,
               "care action values must stay aligned");
_Static_assert((unsigned)ACTION_CLEAN == (unsigned)BYTE_BUDDY_CARE_CLEAN,
               "care action values must stay aligned");
_Static_assert((unsigned)ACTION_REST == (unsigned)BYTE_BUDDY_CARE_REST,
               "care action values must stay aligned");
_Static_assert((unsigned)ACTION_COUNT + 1U ==
                   (unsigned)BYTE_BUDDY_CARE_COUNT,
               "pet is the fifth care action");

typedef enum {
    STAR_KIND_GOLD = 0,
    STAR_KIND_CALM,
    STAR_KIND_HEART,
    STAR_KIND_COUNT,
} star_kind_t;

typedef enum {
    STAR_EFFECT_NONE = 0,
    STAR_EFFECT_CAUGHT,
    STAR_EFFECT_MISSED,
} star_effect_kind_t;

typedef enum {
    SHOP_FEEDBACK_ONSET = 0,
    SHOP_FEEDBACK_UNLOCKED,
    SHOP_FEEDBACK_EQUIPPED,
    SHOP_FEEDBACK_UNAVAILABLE,
    SHOP_FEEDBACK_POWER_GROWTH,
} shop_feedback_kind_t;

typedef enum {
    SIGNAL_PHASE_INTRO = 0,
    SIGNAL_PHASE_OPEN,
    SIGNAL_PHASE_WINDUP,
    SIGNAL_PHASE_TRAVEL,
    SIGNAL_PHASE_IMPACT,
    SIGNAL_PHASE_RECOVERY,
    SIGNAL_PHASE_VICTORY,
    SIGNAL_PHASE_DEFEAT,
} signal_battle_phase_t;

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
    uint8_t star_effect_kind;
    uint8_t star_spawn_count;
    uint8_t signal_view;
    uint8_t signal_page;
    uint8_t signal_selected_index;
    uint8_t signal_consumed_count;
    uint8_t signal_training_completed_mask;
    uint8_t signal_training_completed_count;
    uint8_t signal_feeds;
    uint8_t signal_hue;
    uint8_t signal_element_votes[3];
    uint8_t signal_battle_hp;
    uint8_t signal_battle_max_hp;
    uint8_t signal_player_hp;
    uint8_t signal_player_max_hp;
    uint8_t signal_enemy_ward;
    uint8_t signal_battle_pattern;
    uint8_t signal_battle_phase;
    uint8_t signal_battle_outcome;
    uint8_t signal_attack_index;
    uint8_t signal_weave_step;
    uint8_t signal_guard_charges;
    uint8_t signal_focus_row;
    uint8_t menu_selection;
    uint8_t shop_feedback_kind;
    uint8_t last_care_credit;
    uint8_t evolution_pending_mask;
    uint8_t evolution_active_stage;
    uint8_t play_start_pending;
    uint16_t mini_intro_ms;
    uint16_t mini_summary_ms;
    uint16_t evolution_fx_ms;
    uint16_t shop_feedback_ms;
    uint16_t scene_transition_ms;
    uint16_t signal_weave_settle_ms;
    uint16_t star_spawn_ms;
    uint16_t signal_phase_ms;
    uint16_t signal_phase_total_ms;
    uint16_t signal_strike_cooldown_ms;
    uint16_t signal_guard_window_ms;
    uint16_t signal_guard_fx_ms;
    uint16_t signal_passive_fx_ms;
    uint16_t signal_snare_ms;
    uint16_t signal_recovery_bonus_ms;
    uint16_t signal_shown_enemy_hp_q8;
    uint16_t signal_shown_player_hp_q8;
    uint16_t save_retry_ms;
    uint16_t save_exit_wait_ms;
    uint8_t upgrades[BYTE_BUDDY_UPGRADE_COUNT];
    uint8_t style_unlocked[BYTE_BUDDY_STYLE_COUNT];
    uint8_t style_selected[BYTE_BUDDY_STYLE_COUNT];
    uint16_t coins;
    uint16_t care_actions;
    uint16_t signal_session_coins;
    uint16_t signal_session_growth;
    uint16_t style_mix_count;
    uint16_t action_counts[ACTION_COUNT];
    uint16_t pet_actions;
    uint32_t decay_accumulator_ms;
    uint32_t care_credit_cooldown_ms;
    uint32_t animation_ms;
    uint32_t dragon_idle_epoch_ms;
    uint32_t reaction_ms;
    uint32_t mini_elapsed_ms;
    uint32_t star_effect_ms;
    uint32_t signal_generation;
    uint32_t signal_battle_elapsed_ms;
    uint32_t signal_battle_bonus_ms;
    uint32_t signal_weave_charge_units;
    uint32_t signal_hit_ms;
    uint32_t signal_player_hit_ms;
    uint32_t signal_reward_ms;
    uint32_t signal_track_refresh_ms;
    uint32_t signal_request_busy_ms;
    uint32_t signal_scan_timeout_ms;
    uint32_t save_host_sequence;
    uint32_t save_ticket;
    uint32_t save_local_generation;
    uint32_t save_queued_generation;
    uint32_t save_progress_signature;
    uint32_t save_captured_needs;
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
    bool signal_training_mode;
    bool signal_hunt_start_pending;
    bool signal_battle_locked;
    bool signal_guard_armed;
    bool controller_active;
    bool touch_was_down;
    bool save_available;
    bool save_dirty;
    bool save_error;
    bool exit_pending;
    const uint8_t *art_data;
    size_t art_bytes;
    uint16_t art_sheets;
    uint32_t achievement_mask;
    uint64_t signal_selected_token;
    uint64_t signal_entropy;
    uint64_t signal_consumed[BYTE_BUDDY_SIGNAL_MAX_CONSUMED];
    byte_buddy_lineage_genes_t signal_genes;
    p4_game_signal_t signal_battle_signal;
    p4_game_signal_snapshot_t signal_snapshot;
    p4_game_audio_effect_player_t audio;
} byte_buddy_state_t;

static byte_buddy_battle_stats_t current_battle_stats(
    const byte_buddy_state_t *state);
static void mark_save_dirty(byte_buddy_state_t *state);

static uint32_t save_needs_signature(const byte_buddy_state_t *state)
{
    return (uint32_t)state->hunger |
        (uint32_t)state->joy << 8U |
        (uint32_t)state->hygiene << 16U |
        (uint32_t)state->energy << 24U;
}

static p4_game_result_t request_game_exit(byte_buddy_state_t *state)
{
    if (!state->save_available) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    if (state->save_captured_needs != save_needs_signature(state)) {
        mark_save_dirty(state);
    }
    if (!state->save_dirty &&
        state->save_ticket == P4_GAME_SAVE_INVALID_TICKET) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->exit_pending = true;
    state->save_exit_wait_ms = 0U;
    return P4_GAME_CONTINUE;
}

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

static const char *const s_signal_attack_names[
    BYTE_BUDDY_SIGNAL_ATTACK_COUNT] = {
    "ARC BURST", "PRISM LANCE", "THORN SNARE", "COMET CRASH",
};

static const uint8_t s_signal_attack_name_lengths[
    BYTE_BUDDY_SIGNAL_ATTACK_COUNT] = {9U, 11U, 11U, 11U};

static const char *const s_signal_passive_names[
    BYTE_BUDDY_SIGNAL_PASSIVE_COUNT] = {
    "WARD", "ECHO", "SIPHON", "OVERCLOCK",
};

static const uint8_t s_signal_passive_name_lengths[
    BYTE_BUDDY_SIGNAL_PASSIVE_COUNT] = {4U, 4U, 6U, 9U};

static const char *const s_signal_arena_names[
    BYTE_BUDDY_SIGNAL_ARENA_COUNT] = {
    "STEADY", "HEAVY", "QUICK", "ECHO", "SHIFT",
};

static const uint8_t s_signal_arena_name_lengths[
    BYTE_BUDDY_SIGNAL_ARENA_COUNT] = {6U, 5U, 5U, 4U, 5U};

static const char *const s_dragon_ability_names[
    BYTE_BUDDY_ABILITY_COUNT] = {
    "NOVA PARRY", "FLARE COUNTER", "GLACIER WARD", "JAM FIELD",
};

static const uint8_t s_dragon_ability_name_lengths[
    BYTE_BUDDY_ABILITY_COUNT] = {10U, 13U, 12U, 9U};

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
                           unsigned required_sheets,
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
    if (sheets != required_sheets || sheets > UINT16_MAX ||
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

buddy_reaction_t byte_buddy_growth_reaction(
    byte_buddy_stage_t previous_stage, byte_buddy_stage_t next_stage)
{
    return previous_stage == BYTE_BUDDY_STAGE_EGG &&
                   next_stage == BYTE_BUDDY_STAGE_BABY
        ? REACTION_HATCH : REACTION_GROW;
}

bool byte_buddy_reaction_can_advance(
    bool evolution_active, bool scene_transition_active,
    bool upgrade_shop, bool signal_hunt,
    byte_buddy_signal_view_t signal_view)
{
    if (evolution_active || scene_transition_active || upgrade_shop) {
        return false;
    }
    return !signal_hunt || signal_view == BYTE_BUDDY_SIGNAL_TRACKER ||
        signal_view == BYTE_BUDDY_SIGNAL_BATTLE;
}

bool byte_buddy_play_start_ready(
    bool play_start_pending, bool mini_game,
    buddy_reaction_t reaction, uint32_t reaction_ms,
    uint16_t evolution_fx_ms, uint8_t evolution_pending_mask)
{
    return play_start_pending && !mini_game &&
        reaction == REACTION_IDLE && reaction_ms == 0U &&
        evolution_fx_ms == 0U && evolution_pending_mask == 0U;
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

uint8_t byte_buddy_star_run_reward(uint8_t catches)
{
    if (catches == 0U) {
        return 0U;
    }
    const uint8_t bonus = (uint8_t)(catches / 4U);
    return (uint8_t)(2U + (bonus > 4U ? 4U : bonus));
}

static uint8_t battle_stat(uint32_t value)
{
    return (uint8_t)(value > 99U ? 99U : value);
}

bool byte_buddy_care_earns_growth(
    byte_buddy_care_kind_t care,
    uint8_t need_before,
    byte_buddy_care_kind_t previous_credit)
{
    if ((unsigned)care >= BYTE_BUDDY_CARE_COUNT || need_before > STAT_MAX) {
        return false;
    }

    /*
     * Deep needs always deserve care.  A varied action receives a gentler
     * threshold, while tapping an already-full need still plays its reaction
     * without becoming the fastest growth strategy.
     */
    if (need_before <= 88U) {
        return true;
    }
    return need_before <= 98U && care != previous_credit;
}

byte_buddy_battle_stats_t byte_buddy_battle_stats_for_growth(
    uint16_t growth_credit,
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
    const uint8_t level = byte_buddy_level_for_interactions(growth_credit);
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
    return byte_buddy_battle_stats_for_growth(
        (uint16_t)interactions,
        feed_actions, play_actions, clean_actions, rest_actions, pet_actions,
        wings_level, aura_level, nest_level, magnet_level);
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
    return byte_buddy_signal_weave_rules_for_magic(
        genome, strength, magnet_level, 0U);
}

byte_buddy_signal_weave_rules_t byte_buddy_signal_weave_rules_for_magic(
    byte_buddy_signal_genome_t genome,
    uint8_t strength, uint8_t magnet_level, uint8_t magic)
{
    const uint8_t rarity = genome.rarity < 4U ? genome.rarity : 3U;
    const uint8_t bounded_strength = strength < 101U ? strength : 100U;
    const uint8_t bounded_magnet = magnet_level < 4U
        ? magnet_level : 3U;
    const unsigned base_hold_ms = 600U + 2U * bounded_strength +
        40U * rarity;
    const unsigned magic_percent = magic / 5U < 15U
        ? magic / 5U : 15U;
    const unsigned hold_ms = base_hold_ms -
        base_hold_ms * magic_percent / 100U;
    return (byte_buddy_signal_weave_rules_t){
        .required_locks = (uint8_t)(4U + rarity),
        .touch_radius = (uint8_t)(17U + 2U * bounded_magnet),
        .hold_ms = (uint16_t)hold_ms,
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

static uint8_t signal_attack_deck_size_for_form(uint32_t form_id)
{
    return (uint8_t)(2U + form_id % 3U);
}

static unsigned signal_encounter_modifier_score(
    uint8_t attack, uint8_t passive, uint8_t arena,
    uint8_t starting_ward, uint8_t deck_size,
    bool protected_signal, bool hidden)
{
    unsigned score = (unsigned)starting_ward * 2U;
    score += protected_signal ? 6U : 0U;
    score += hidden ? 5U : 0U;
    score += attack == BYTE_BUDDY_SIGNAL_ATTACK_PRISM_LANCE ? 1U
        : attack == BYTE_BUDDY_SIGNAL_ATTACK_THORN_SNARE ? 2U
        : attack == BYTE_BUDDY_SIGNAL_ATTACK_COMET_CRASH ? 3U : 0U;
    score += passive == BYTE_BUDDY_SIGNAL_PASSIVE_WARD ? 1U
        : passive == BYTE_BUDDY_SIGNAL_PASSIVE_ECHO ? 3U
        : passive == BYTE_BUDDY_SIGNAL_PASSIVE_SIPHON ? 4U
        : passive == BYTE_BUDDY_SIGNAL_PASSIVE_OVERCLOCK ? 5U : 0U;
    score += arena == BYTE_BUDDY_SIGNAL_ARENA_HEAVY ? 2U
        : arena == BYTE_BUDDY_SIGNAL_ARENA_QUICK ? 4U
        : arena == BYTE_BUDDY_SIGNAL_ARENA_ECHO ? 3U
        : arena == BYTE_BUDDY_SIGNAL_ARENA_SHIFT ? 4U : 0U;
    if (deck_size > 2U) {
        score += (unsigned)(deck_size - 2U) * 2U;
    }
    return score;
}

byte_buddy_signal_encounter_t byte_buddy_signal_encounter(
    uint64_t token, int8_t rssi_dbm, uint8_t channel, uint8_t flags)
{
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(token, rssi_dbm);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(token);
    const uint8_t family = byte_buddy_signal_channel_family(channel);
    const bool protected_signal =
        (flags & P4_GAME_SIGNAL_PROTECTED) != 0U;
    const bool hidden = (flags & P4_GAME_SIGNAL_HIDDEN) != 0U;
    const uint8_t arena = family == UINT8_MAX
        ? BYTE_BUDDY_SIGNAL_ARENA_STEADY
        : (uint8_t)(BYTE_BUDDY_SIGNAL_ARENA_HEAVY + family);
    const uint8_t attack = genome.core < BYTE_BUDDY_SIGNAL_ATTACK_COUNT
        ? genome.core : BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST;
    const uint8_t passive = genome.aura < BYTE_BUDDY_SIGNAL_PASSIVE_COUNT
        ? genome.aura : BYTE_BUDDY_SIGNAL_PASSIVE_WARD;
    const uint32_t form_id = byte_buddy_signal_form_id(
        genome.recipe_id, channel, flags);
    unsigned ward = passive == BYTE_BUDDY_SIGNAL_PASSIVE_WARD ? 1U : 0U;
    if (protected_signal) {
        ward += profile.rarity >= 4U ? 2U : 1U;
    }
    const uint8_t starting_ward = (uint8_t)(ward > 3U ? 3U : ward);
    const unsigned modifier_score = signal_encounter_modifier_score(
        attack, passive, arena, starting_ward,
        signal_attack_deck_size_for_form(form_id),
        protected_signal, hidden);
    const uint8_t threat_score = (uint8_t)(
        ((unsigned)profile.strength +
         (unsigned)(profile.rarity - 1U) * 12U + modifier_score) / 25U);
    const uint8_t threat = (uint8_t)(
        1U + (threat_score > 4U ? 4U : threat_score));

    unsigned hp = profile.battle_hp + profile.rarity +
        (unsigned)threat * 2U;
    if (protected_signal) {
        hp += 2U;
    }
    if (hp > 60U) {
        hp = 60U;
    }

    unsigned damage = 8U + profile.strength / 18U +
        (unsigned)(profile.rarity - 1U) * 2U;
    if (arena == BYTE_BUDDY_SIGNAL_ARENA_HEAVY ||
        genome.core == BYTE_BUDDY_SIGNAL_ATTACK_COMET_CRASH) {
        ++damage;
    }
    if (damage > 18U) {
        damage = 18U;
    }

    int period_ms = 3400 - (int)profile.strength * 8 -
        (int)(profile.rarity - 1U) * 120;
    static const int16_t halo_timing_ms[4] = {
        120, 0, -100, 60,
    };
    period_ms += halo_timing_ms[genome.halo & 3U];
    if (genome.aura == BYTE_BUDDY_SIGNAL_PASSIVE_OVERCLOCK) {
        period_ms -= 300;
    }
    if (arena == BYTE_BUDDY_SIGNAL_ARENA_QUICK) {
        period_ms -= 180;
    }
    if (period_ms < 1900) {
        period_ms = 1900;
    } else if (period_ms > 3400) {
        period_ms = 3400;
    }

    int telegraph_ms = 1150 - (int)profile.strength * 3;
    if (arena == BYTE_BUDDY_SIGNAL_ARENA_HEAVY) {
        telegraph_ms += 100;
    }
    if (hidden) {
        telegraph_ms -= 150;
    }
    if (genome.core == BYTE_BUDDY_SIGNAL_ATTACK_COMET_CRASH) {
        telegraph_ms -= 100;
    }
    if (telegraph_ms < 650) {
        telegraph_ms = 650;
    } else if (telegraph_ms > 1200) {
        telegraph_ms = 1200;
    }

    return (byte_buddy_signal_encounter_t){
        .attack = attack,
        .passive = passive,
        .weakness = genome.sigil < 3U
            ? (uint8_t)(BYTE_BUDDY_ELEMENT_FIRE + genome.sigil)
            : BYTE_BUDDY_ELEMENT_MYSTERY,
        .arena = arena,
        .threat = threat,
        .max_hp = (uint8_t)hp,
        .damage = (uint8_t)damage,
        .starting_ward = starting_ward,
        .attack_period_ms = (uint16_t)period_ms,
        .telegraph_ms = (uint16_t)telegraph_ms,
        .form_id = form_id,
        .hidden = hidden,
        .protected_signal = protected_signal,
    };
}

byte_buddy_dragon_ability_t byte_buddy_dragon_ability(
    byte_buddy_element_t element)
{
    return element >= BYTE_BUDDY_ELEMENT_MYSTERY &&
            element < BYTE_BUDDY_ELEMENT_COUNT
        ? (byte_buddy_dragon_ability_t)element
        : BYTE_BUDDY_ABILITY_NOVA_PARRY;
}

byte_buddy_signal_attack_t byte_buddy_signal_attack_for(
    byte_buddy_signal_encounter_t encounter, uint8_t attack_index)
{
    const uint8_t base = encounter.attack < BYTE_BUDDY_SIGNAL_ATTACK_COUNT
        ? encounter.attack : BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST;
    const uint8_t deck_size = byte_buddy_signal_attack_deck_size(encounter);
    uint8_t position = (uint8_t)(attack_index % deck_size);
    const uint8_t cycle = (uint8_t)(attack_index / deck_size);
    if (encounter.arena == BYTE_BUDDY_SIGNAL_ARENA_SHIFT &&
        (cycle & 1U) != 0U) {
        position = (uint8_t)(deck_size - 1U - position);
    }
    const uint8_t stride = ((encounter.form_id / 3U) & 1U) != 0U
        ? 3U : 1U;
    return (byte_buddy_signal_attack_t)(
        (base + position * stride) % BYTE_BUDDY_SIGNAL_ATTACK_COUNT);
}

uint8_t byte_buddy_signal_attack_deck_size(
    byte_buddy_signal_encounter_t encounter)
{
    return signal_attack_deck_size_for_form(encounter.form_id);
}

uint8_t byte_buddy_signal_reward_coins(
    byte_buddy_signal_profile_t profile,
    byte_buddy_signal_encounter_t encounter)
{
    const unsigned modifier_score = signal_encounter_modifier_score(
        encounter.attack, encounter.passive, encounter.arena,
        encounter.starting_ward,
        byte_buddy_signal_attack_deck_size(encounter),
        encounter.protected_signal, encounter.hidden);
    unsigned bonus = (modifier_score + 7U) / 8U;
    if (bonus > 4U) {
        bonus = 4U;
    }
    const unsigned reward = (unsigned)profile.reward_coins + bonus;
    return (uint8_t)(reward > UINT8_MAX ? UINT8_MAX : reward);
}

static uint8_t signal_attack_occurrence(
    byte_buddy_signal_encounter_t encounter,
    uint8_t attack_index, byte_buddy_signal_attack_t attack)
{
    uint8_t occurrence = 0U;
    for (unsigned index = 0U; index <= attack_index; ++index) {
        if (byte_buddy_signal_attack_for(
                encounter, (uint8_t)index) == attack &&
            occurrence != UINT8_MAX) {
            ++occurrence;
        }
    }
    return occurrence;
}

byte_buddy_signal_defense_t byte_buddy_signal_defense(
    byte_buddy_signal_encounter_t encounter,
    byte_buddy_dragon_ability_t ability,
    bool guarded, uint8_t attack_index)
{
    const byte_buddy_signal_attack_t attack =
        byte_buddy_signal_attack_for(encounter, attack_index);
    unsigned damage = encounter.damage;
    if (encounter.passive == BYTE_BUDDY_SIGNAL_PASSIVE_ECHO &&
        attack_index % 3U == 2U) {
        damage += 2U;
    }
    if (encounter.arena == BYTE_BUDDY_SIGNAL_ARENA_ECHO &&
        attack_index % 3U == 2U) {
        ++damage;
    }
    if (attack == BYTE_BUDDY_SIGNAL_ATTACK_COMET_CRASH &&
        signal_attack_occurrence(
            encounter, attack_index, attack) % 3U == 0U) {
        damage += 3U;
    }
    if (damage > 24U) {
        damage = 24U;
    }
    byte_buddy_signal_defense_t result = {
        .player_damage = (uint8_t)damage,
        .enemy_heal = !guarded &&
                encounter.passive == BYTE_BUDDY_SIGNAL_PASSIVE_SIPHON
            ? (uint8_t)(1U + encounter.threat / 3U) : 0U,
        .status_ms = !guarded &&
                attack == BYTE_BUDDY_SIGNAL_ATTACK_THORN_SNARE
            ? 1000U : 0U,
    };
    if (!guarded) {
        return result;
    }
    result.enemy_heal = 0U;
    result.status_ms = 0U;
    switch (ability) {
    case BYTE_BUDDY_ABILITY_FLARE_COUNTER:
        result.player_damage = (uint8_t)(damage / 3U);
        result.counter_damage = (uint8_t)(2U + encounter.threat / 2U);
        result.delay_ms = 280U;
        break;
    case BYTE_BUDDY_ABILITY_GLACIER_WARD:
        result.player_damage = 0U;
        result.counter_damage = 1U;
        result.delay_ms = 420U;
        break;
    case BYTE_BUDDY_ABILITY_JAM_FIELD:
        result.player_damage = damage > 4U
            ? (uint8_t)(damage - 4U) : 0U;
        result.counter_damage = 1U;
        result.ward_damage = 1U;
        result.delay_ms = 620U;
        break;
    case BYTE_BUDDY_ABILITY_NOVA_PARRY:
    default:
        result.player_damage = 0U;
        result.counter_damage = (uint8_t)(1U + encounter.threat / 2U);
        result.ward_damage = 1U;
        result.delay_ms = 340U;
        break;
    }
    if (attack == BYTE_BUDDY_SIGNAL_ATTACK_PRISM_LANCE &&
        result.player_damage == 0U) {
        result.player_damage = 1U;
    }
    return result;
}

static uint8_t bounded_fx_frame(uint8_t row, uint8_t phase)
{
    const uint8_t bounded_row = row < 4U ? row : 0U;
    const uint8_t bounded_phase = phase < 4U ? phase : 3U;
    return (uint8_t)(bounded_row * 4U + bounded_phase);
}

uint8_t byte_buddy_counter_fx_frame(
    byte_buddy_dragon_ability_t ability, uint8_t phase)
{
    return bounded_fx_frame(
        ability < BYTE_BUDDY_ABILITY_COUNT ? (uint8_t)ability : 0U,
        phase);
}

uint8_t byte_buddy_outcome_fx_frame(
    byte_buddy_combat_outcome_t outcome, uint8_t phase)
{
    const uint8_t row = outcome >= BYTE_BUDDY_COMBAT_VICTORY &&
            outcome <= BYTE_BUDDY_COMBAT_RETREATED
        ? (uint8_t)(outcome - BYTE_BUDDY_COMBAT_VICTORY) : 0U;
    return bounded_fx_frame(row, phase);
}

uint8_t byte_buddy_passive_fx_frame(
    byte_buddy_signal_passive_t passive, uint8_t phase)
{
    return bounded_fx_frame(
        passive < BYTE_BUDDY_SIGNAL_PASSIVE_COUNT
            ? (uint8_t)passive : 0U,
        phase);
}

uint8_t byte_buddy_scan_fx_frame(
    byte_buddy_scan_fx_t state, uint8_t phase)
{
    return bounded_fx_frame(
        state < BYTE_BUDDY_SCAN_FX_COUNT ? (uint8_t)state : 0U,
        phase);
}

uint8_t byte_buddy_evolution_fx_frame(
    byte_buddy_evolution_fx_t evolution, uint8_t phase)
{
    return bounded_fx_frame(
        evolution < BYTE_BUDDY_EVOLUTION_FX_COUNT
            ? (uint8_t)evolution : 0U,
        phase);
}

uint8_t byte_buddy_need_fx_frame(
    byte_buddy_need_fx_t need, uint8_t phase)
{
    return bounded_fx_frame(
        need < BYTE_BUDDY_NEED_FX_COUNT ? (uint8_t)need : 0U,
        phase);
}

uint8_t byte_buddy_activity_fx_frame(
    byte_buddy_activity_fx_t activity, uint8_t phase)
{
    return bounded_fx_frame(
        activity < BYTE_BUDDY_ACTIVITY_FX_COUNT
            ? (uint8_t)activity : 0U,
        phase);
}

uint8_t byte_buddy_signal_player_hp(
    byte_buddy_battle_stats_t stats, uint8_t nest_level,
    byte_buddy_signal_lineage_t lineage)
{
    const uint8_t bounded_nest = nest_level < 4U ? nest_level : 3U;
    const uint8_t bounded_tier = lineage.tier <
            BYTE_BUDDY_LINEAGE_TIER_COUNT
        ? lineage.tier : BYTE_BUDDY_LINEAGE_MYTHIC;
    unsigned hp = 48U + stats.guard / 2U + stats.level / 2U +
        (unsigned)bounded_nest * 4U + (unsigned)bounded_tier * 2U;
    return (uint8_t)(hp > 99U ? 99U : hp);
}

uint16_t byte_buddy_signal_strike_cooldown_ms(uint8_t speed)
{
    const uint8_t bounded = speed < 91U ? speed : 90U;
    const unsigned cooldown = 360U - (unsigned)bounded * 100U / 90U;
    return (uint16_t)(cooldown < SIGNAL_HIT_DURATION_MS
        ? SIGNAL_HIT_DURATION_MS : cooldown);
}

uint16_t byte_buddy_signal_parry_window_ms(
    byte_buddy_lineage_battle_traits_t traits)
{
    const uint16_t bounded_bonus = traits.guard_time_ms < 481U
        ? traits.guard_time_ms : 480U;
    return (uint16_t)(300U + bounded_bonus);
}

bool byte_buddy_signal_parry_ready(
    uint16_t time_to_impact_ms,
    byte_buddy_lineage_battle_traits_t traits)
{
    return time_to_impact_ms != 0U &&
        time_to_impact_ms <= byte_buddy_signal_parry_window_ms(traits);
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
        .rarity_diversity = rarity_diversity,
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

static void rehydrate_achievements(
    p4_game_context_t *context, const byte_buddy_state_t *state)
{
    static const struct {
        uint32_t flag;
        const char *id;
        const char *title;
        const char *description;
    } achievements[] = {
        {ACHIEVEMENT_FIRST_CARE, "first-care", "FIRST CARE",
         "NURTURE YOUR DRAGON EGG"},
        {ACHIEVEMENT_CLEAN, "clean-sweep", "CLEAN SWEEP",
         "CLEAN YOUR DRAGON 3 TIMES"},
        {ACHIEVEMENT_PLAY, "star-catcher", "STAR CATCHER",
         "CATCH 3 STARS WITH BUDDY"},
        {ACHIEVEMENT_GROW, "dragon-raised", "DRAGON RAISED",
         "RAISE AN ELEMENTAL DRAGON"},
        {ACHIEVEMENT_FIRST_SIGNAL, "first-signal", "SIGNAL TAMER",
         "DEFEAT AND EAT A SIGNAL SEED"},
        {ACHIEVEMENT_SIGNAL_CHORUS, "signal-chorus", "SIGNAL CHORUS",
         "BUILD AN AURORA LINEAGE FROM FIVE SIGNALS"},
        {ACHIEVEMENT_MYTHIC_LINEAGE, "mythic-lineage", "MYTHIC LINEAGE",
         "COMBINE TWELVE DIVERSE SIGNAL GENOMES"},
        {ACHIEVEMENT_ETERNAL_LINEAGE, "eternal-lineage",
         "ETERNAL LINEAGE", "COMBINE THIRTY-TWO DIVERSE SIGNAL LINKS"},
    };
    for (size_t index = 0U;
         index < sizeof(achievements) / sizeof(achievements[0]); ++index) {
        if ((state->achievement_mask & achievements[index].flag) != 0U) {
            (void)p4_game_unlock_achievement(
                context, achievements[index].id, achievements[index].title,
                achievements[index].description);
        }
    }
}

static void trigger_reaction(byte_buddy_state_t *state,
                             buddy_reaction_t reaction)
{
    state->reaction = (uint8_t)reaction;
    state->reaction_ms = REACTION_DURATION_MS;
}

static void begin_scene_transition(byte_buddy_state_t *state)
{
    state->scene_transition_ms = SCENE_TRANSITION_MS;
}

static uint8_t evolution_stage_bit(unsigned stage)
{
    if (stage < BYTE_BUDDY_STAGE_BABY ||
        stage > BYTE_BUDDY_STAGE_ELEMENTAL) {
        return 0U;
    }
    return (uint8_t)(1U << (stage - BYTE_BUDDY_STAGE_BABY));
}

static void queue_evolution_milestones(
    byte_buddy_state_t *state, unsigned previous_stage,
    unsigned next_stage)
{
    for (unsigned stage = BYTE_BUDDY_STAGE_BABY;
         stage <= BYTE_BUDDY_STAGE_ELEMENTAL; ++stage) {
        if (previous_stage < stage && next_stage >= stage) {
            state->evolution_pending_mask |= evolution_stage_bit(stage);
        }
    }
}

static bool begin_next_evolution(byte_buddy_state_t *state,
                                 uint16_t lead_in_ms)
{
    for (unsigned stage = BYTE_BUDDY_STAGE_BABY;
         stage <= BYTE_BUDDY_STAGE_ELEMENTAL; ++stage) {
        const uint8_t bit = evolution_stage_bit(stage);
        if ((state->evolution_pending_mask & bit) == 0U) {
            continue;
        }
        state->evolution_pending_mask &= (uint8_t)~bit;
        state->evolution_active_stage = (uint8_t)stage;
        const uint32_t duration =
            (uint32_t)EVOLUTION_FX_MS + lead_in_ms;
        state->evolution_fx_ms = duration > UINT16_MAX
            ? UINT16_MAX : (uint16_t)duration;
        return true;
    }
    state->evolution_active_stage = BYTE_BUDDY_STAGE_COUNT;
    return false;
}

static void begin_home_evolution_sequence(byte_buddy_state_t *state)
{
    if (state->evolution_fx_ms == 0U) {
        (void)begin_next_evolution(state, SCENE_TRANSITION_MS);
    }
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
    const byte_buddy_stage_t previous_stage =
        (byte_buddy_stage_t)state->stage;
    queue_evolution_milestones(
        state, (unsigned)previous_stage, (unsigned)natural_stage);
    state->stage = (uint8_t)natural_stage;
    if (!state->signal_hunt && !state->mini_game &&
        state->evolution_fx_ms == 0U) {
        (void)begin_next_evolution(state, 0U);
    }
    play_tone(context, state->stage == BYTE_BUDDY_STAGE_ELEMENTAL
        ? 1047U : 880U, 180U);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
    if (state->stage == BYTE_BUDDY_STAGE_ELEMENTAL) {
        unlock(context, state, ACHIEVEMENT_GROW, "dragon-raised",
               "DRAGON RAISED", "RAISE AN ELEMENTAL DRAGON");
    }
}

static bool record_care_credit(byte_buddy_state_t *state,
                               byte_buddy_care_kind_t care,
                               uint8_t need_before,
                               uint16_t *specialization_count)
{
    if (state->care_credit_cooldown_ms != 0U ||
        specialization_count == NULL ||
        !byte_buddy_care_earns_growth(
            care, need_before,
            (byte_buddy_care_kind_t)state->last_care_credit)) {
        return false;
    }
    if (*specialization_count < UINT16_MAX) {
        ++*specialization_count;
    }
    if (state->care_actions < UINT16_MAX) {
        ++state->care_actions;
    }
    state->last_care_credit = (uint8_t)care;
    state->care_credit_cooldown_ms = CARE_CREDIT_COOLDOWN_MS;
    return true;
}

static void record_action(p4_game_context_t *context,
                          byte_buddy_state_t *state,
                          buddy_action_t action,
                          buddy_reaction_t reaction,
                          uint8_t need_before)
{
    if ((unsigned)action >= ACTION_COUNT) {
        return;
    }
    (void)record_care_credit(
        state, (byte_buddy_care_kind_t)action, need_before,
        &state->action_counts[action]);
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
                       byte_buddy_state_t *state,
                       uint8_t need_before)
{
    (void)record_care_credit(
        state, BYTE_BUDDY_CARE_PET, need_before, &state->pet_actions);
    update_dragon_traits(state);
    trigger_reaction(state, REACTION_PET);
    unlock(context, state, ACHIEVEMENT_FIRST_CARE, "first-care",
           "FIRST CARE", "NURTURE YOUR DRAGON EGG");
    advance_growth(context, state);
    if (state->joy != need_before) {
        mark_save_dirty(state);
    }
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
    state->star_spawn_ms = MINI_GAME_STAR_SPAWN_MS;
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
    state->play_start_pending = 0U;
    state->mini_game = true;
    begin_scene_transition(state);
    state->mini_elapsed_ms = 0U;
    state->mini_intro_ms = MINI_GAME_INTRO_MS;
    state->mini_summary_ms = 0U;
    state->play_catches = 0U;
    state->play_streak = 0U;
    state->star_effect_ms = 0U;
    state->star_effect_kind = STAR_EFFECT_NONE;
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
    const uint8_t need_before = action == ACTION_FEED ? state->hunger
        : action == ACTION_PLAY ? state->joy
        : action == ACTION_CLEAN ? state->hygiene
        : action == ACTION_REST ? state->energy : STAT_MAX;
    const uint8_t hunger_before = state->hunger;
    const uint8_t joy_before = state->joy;
    const uint8_t hygiene_before = state->hygiene;
    const uint8_t energy_before = state->energy;
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
            record_action(
                context, state, ACTION_PLAY, REACTION_PLAY, need_before);
            /* Let every stage finish its authored Play motion on Home before
             * the Star Catcher scene takes over the dragon. */
            state->play_start_pending = 1U;
            play_tone(context, 659U, 100U);
            (void)p4_game_audio_effect_play(
                context, &state->audio, P4_GAME_AUDIO_EFFECT_ACTION);
            if (state->hunger != hunger_before || state->joy != joy_before ||
                state->hygiene != hygiene_before ||
                state->energy != energy_before) {
                mark_save_dirty(state);
            }
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
    record_action(context, state, action, reaction, need_before);
    if (state->hunger != hunger_before || state->joy != joy_before ||
        state->hygiene != hygiene_before || state->energy != energy_before) {
        mark_save_dirty(state);
    }
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

static void finish_reaction_now(byte_buddy_state_t *state)
{
    state->reaction_ms = 0U;
    state->reaction = REACTION_IDLE;
    if (state->stage != BYTE_BUDDY_STAGE_EGG) {
        state->dragon_idle_epoch_ms = state->animation_ms;
    }
}

static void update_reaction(byte_buddy_state_t *state, uint32_t elapsed_ms)
{
    if (state->reaction_ms == 0U) {
        return;
    }
    if (state->reaction_ms > elapsed_ms) {
        state->reaction_ms -= elapsed_ms;
        return;
    }
    finish_reaction_now(state);
}

static void complete_play(byte_buddy_state_t *state)
{
    state->mini_game = false;
    begin_scene_transition(state);
    state->mini_intro_ms = 0U;
    state->mini_summary_ms = 0U;
    state->star_effect_kind = STAR_EFFECT_NONE;
    state->joy = increase(state->joy, 8U);
    state->energy = decrease(state->energy, 3U);
    mark_save_dirty(state);
    begin_home_evolution_sequence(state);
}

static void finish_play(byte_buddy_state_t *state)
{
    if (!state->mini_game || state->mini_summary_ms != 0U) {
        return;
    }
    state->mini_intro_ms = 0U;
    state->mini_summary_ms = MINI_GAME_SUMMARY_MS;
    const uint16_t reward = byte_buddy_star_run_reward(
        state->play_catches);
    state->coins = state->coins > UINT16_MAX - reward
        ? UINT16_MAX : (uint16_t)(state->coins + reward);
}

static bool signal_consumed(const byte_buddy_state_t *state, uint64_t token)
{
    if (state->signal_training_mode) {
        for (uint8_t index = 0U; index < state->signal_snapshot.count;
             ++index) {
            if (state->signal_snapshot.results[index].token == token) {
                return (state->signal_training_completed_mask &
                        (uint8_t)(1U << index)) != 0U;
            }
        }
        return false;
    }
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
    if (state->signal_battle_locked &&
        state->signal_view == BYTE_BUDDY_SIGNAL_BATTLE &&
        state->signal_battle_signal.token ==
            state->signal_selected_token) {
        return &state->signal_battle_signal;
    }
    for (size_t index = 0U; index < state->signal_snapshot.count; ++index) {
        if (state->signal_snapshot.results[index].token ==
            state->signal_selected_token) {
            return &state->signal_snapshot.results[index];
        }
    }
    if (state->signal_battle_locked &&
        state->signal_battle_signal.token ==
            state->signal_selected_token) {
        return &state->signal_battle_signal;
    }
    return NULL;
}

static bool signal_is_simulated(const p4_game_signal_t *signal)
{
    return signal != NULL &&
        (signal->flags & P4_GAME_SIGNAL_SIMULATED) != 0U;
}

static void clamp_signal_focus(byte_buddy_state_t *state);

static void load_training_signals(byte_buddy_state_t *state)
{
    static const p4_game_signal_t training_signals[] = {
        {
            .token = UINT64_C(0x00123456789abcde),
            .label = "PULSE GROVE",
            .rssi_dbm = -62,
            .channel = 1U,
            .flags = P4_GAME_SIGNAL_SIMULATED,
        },
        {
            .token = UINT64_C(0x3ff0000002445678),
            .label = "THORN SCHOOL",
            .rssi_dbm = -59,
            .channel = 6U,
            .flags = P4_GAME_SIGNAL_PROTECTED |
                     P4_GAME_SIGNAL_SIMULATED,
        },
        {
            .token = UINT64_C(0x1020304050607080),
            .label = "EMBER ARCADE",
            .rssi_dbm = -56,
            .channel = 11U,
            .flags = P4_GAME_SIGNAL_SIMULATED,
        },
        {
            .token = UINT64_C(0x8877665544332211),
            .label = "MOON WORKSHOP",
            .rssi_dbm = -52,
            .channel = 36U,
            .flags = P4_GAME_SIGNAL_PROTECTED |
                     P4_GAME_SIGNAL_SIMULATED,
        },
        {
            .token = UINT64_C(0x55aa33cc77ee0011),
            .label = "DRAGON DRILL",
            .rssi_dbm = -48,
            .channel = 149U,
            .flags = P4_GAME_SIGNAL_HIDDEN |
                     P4_GAME_SIGNAL_SIMULATED,
        },
    };
    state->signal_generation = state->signal_generation == UINT32_MAX
        ? 1U : state->signal_generation + 1U;
    state->signal_snapshot = (p4_game_signal_snapshot_t){
        .generation = state->signal_generation,
        .status = P4_GAME_SIGNAL_READY,
        .count = (uint8_t)(sizeof(training_signals) /
                           sizeof(training_signals[0])),
    };
    memcpy(state->signal_snapshot.results, training_signals,
           sizeof(training_signals));
    state->signal_request_busy_ms = 0U;
    state->signal_scan_timeout_ms = 0U;
    state->signal_training_mode = true;
    state->signal_training_completed_mask = 0U;
    state->signal_training_completed_count = 0U;
    clamp_signal_focus(state);
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
    state->signal_battle_locked = false;
    state->signal_selected_token =
        state->signal_snapshot.results[index].token;
    state->signal_previous_rssi =
        state->signal_snapshot.results[index].rssi_dbm;
    state->signal_trend_db = 0;
    state->signal_samples = 1U;
    state->signal_track_refresh_ms = SIGNAL_TRACK_REFRESH_MS;
    state->signal_view = BYTE_BUDDY_SIGNAL_TRACKER;
    begin_scene_transition(state);
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
        load_training_signals(state);
        return true;
    }
    state->signal_training_mode = false;
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
    state->signal_scan_timeout_ms = SIGNAL_SCAN_TIMEOUT_MS;
    state->signal_snapshot.status = P4_GAME_SIGNAL_SCANNING;
    if (focus_token == 0U) {
        state->signal_snapshot.count = 0U;
    }
    return true;
}

static void poll_signal_scan(p4_game_context_t *context,
                             byte_buddy_state_t *state)
{
    if (state->signal_training_mode) {
        return;
    }
    p4_game_signal_snapshot_t snapshot;
    if (!p4_game_read_signal_scan(context, &snapshot)) {
        return;
    }
    if (state->signal_snapshot.status == P4_GAME_SIGNAL_ERROR &&
        state->signal_scan_timeout_ms == 0U &&
        snapshot.status == P4_GAME_SIGNAL_SCANNING) {
        /* A late provider snapshot must not resurrect an expired request.
         * Only an explicit retry rearms the deadline. */
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
    if (snapshot.status != P4_GAME_SIGNAL_SCANNING) {
        state->signal_scan_timeout_ms = 0U;
    }
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
    begin_scene_transition(state);
    for (size_t index = 0U; index < state->signal_snapshot.count; ++index) {
        if (state->signal_snapshot.results[index].token ==
            state->signal_selected_token) {
            state->signal_selected_index = (uint8_t)index;
            state->signal_page = (uint8_t)(
                index / BYTE_BUDDY_SIGNAL_PAGE_ROWS);
            state->signal_focus_row = (uint8_t)(
                index % BYTE_BUDDY_SIGNAL_PAGE_ROWS);
            state->signal_battle_locked = false;
            return;
        }
    }
    state->signal_page = byte_buddy_signal_clamp_page(
        state->signal_page, state->signal_snapshot.count);
    clamp_signal_focus(state);
    state->signal_battle_locked = false;
}

static void update_signal_tracking(p4_game_context_t *context,
                                   byte_buddy_state_t *state,
                                   uint32_t elapsed_ms)
{
    if (state->signal_view != BYTE_BUDDY_SIGNAL_TRACKER) {
        return;
    }
    if (state->signal_training_mode) {
        return;
    }
    state->signal_track_refresh_ms =
        state->signal_track_refresh_ms > UINT32_MAX - elapsed_ms
            ? UINT32_MAX
            : state->signal_track_refresh_ms + elapsed_ms;
    if (state->signal_snapshot.status == P4_GAME_SIGNAL_READY &&
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
    begin_scene_transition(state);
    state->signal_view = BYTE_BUDDY_SIGNAL_LIST;
    state->signal_page = 0U;
    state->signal_focus_row = 0U;
    state->signal_selected_token = 0U;
    state->signal_battle_locked = false;
    (void)request_signal_scan(context, state, 0U);
}

static void set_signal_phase(byte_buddy_state_t *state,
                             signal_battle_phase_t phase,
                             uint16_t duration_ms)
{
    state->signal_battle_phase = (uint8_t)phase;
    state->signal_phase_ms = duration_ms;
    state->signal_phase_total_ms = duration_ms;
}

static void trigger_signal_passive_fx(byte_buddy_state_t *state)
{
    state->signal_passive_fx_ms = SIGNAL_PASSIVE_FX_MS;
}

static uint16_t signal_attack_open_ms(
    byte_buddy_signal_encounter_t encounter)
{
    const unsigned committed = (unsigned)encounter.telegraph_ms +
        SIGNAL_ATTACK_TRAVEL_MS + SIGNAL_ATTACK_IMPACT_MS +
        SIGNAL_ATTACK_RECOVERY_MS;
    unsigned open_ms = encounter.attack_period_ms > committed
        ? encounter.attack_period_ms - committed : 300U;
    if (encounter.hidden) {
        open_ms = open_ms > 120U ? open_ms - 120U : 180U;
    }
    if (open_ms < 180U) {
        open_ms = 180U;
    } else if (open_ms > 1300U) {
        open_ms = 1300U;
    }
    return (uint16_t)open_ms;
}

static void begin_signal_result(p4_game_context_t *context,
                                byte_buddy_state_t *state,
                                byte_buddy_combat_outcome_t outcome)
{
    if (state->signal_battle_outcome != BYTE_BUDDY_COMBAT_ACTIVE) {
        return;
    }
    state->signal_battle_outcome = (uint8_t)outcome;
    state->signal_guard_window_ms = 0U;
    state->signal_guard_armed = false;
    state->signal_passive_fx_ms = 0U;
    state->signal_weave_charge_units = 0U;
    if (outcome == BYTE_BUDDY_COMBAT_VICTORY) {
        set_signal_phase(state, SIGNAL_PHASE_VICTORY, SIGNAL_VICTORY_MS);
        play_tone(context, 988U, 120U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
    } else {
        const uint16_t duration = outcome == BYTE_BUDDY_COMBAT_RETREATED
            ? SIGNAL_RETREAT_MS : SIGNAL_DEFEAT_MS;
        set_signal_phase(state, SIGNAL_PHASE_DEFEAT, duration);
        play_tone(context, 196U, 140U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
    }
}

static void start_signal_battle(p4_game_context_t *context,
                                byte_buddy_state_t *state)
{
    const p4_game_signal_t *const signal = selected_signal(state);
    if (signal == NULL || signal->rssi_dbm < SIGNAL_HUNT_UNLOCK_RSSI ||
        signal_consumed(state, signal->token) ||
        (!state->signal_training_mode &&
         !byte_buddy_signal_collection_has_room(
            state->signal_consumed_count))) {
        play_tone(context, 196U, 100U);
        return;
    }
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(signal->token);
    const byte_buddy_signal_encounter_t encounter =
        byte_buddy_signal_encounter(
            signal->token, signal->rssi_dbm,
            signal->channel, signal->flags);
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    const byte_buddy_battle_stats_t stats = current_battle_stats(state);
    const byte_buddy_lineage_battle_traits_t battle_traits =
        byte_buddy_lineage_battle_traits(lineage);
    state->signal_view = BYTE_BUDDY_SIGNAL_BATTLE;
    begin_scene_transition(state);
    state->signal_battle_signal = *signal;
    state->signal_battle_locked = true;
    state->signal_battle_hp = encounter.max_hp;
    state->signal_battle_max_hp = encounter.max_hp;
    state->signal_player_max_hp = byte_buddy_signal_player_hp(
        stats, state->upgrades[BYTE_BUDDY_UPGRADE_NEST], lineage);
    state->signal_player_hp = state->signal_player_max_hp;
    state->signal_enemy_ward = encounter.starting_ward;
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
    state->signal_battle_outcome = BYTE_BUDDY_COMBAT_ACTIVE;
    state->signal_attack_index = 0U;
    set_signal_phase(
        state, SIGNAL_PHASE_INTRO, SIGNAL_BATTLE_INTRO_MS);
    state->signal_guard_charges = (uint8_t)(
        2U + state->upgrades[BYTE_BUDDY_UPGRADE_NEST] +
        battle_traits.guard_charges);
    state->signal_hit_ms = 0U;
    state->signal_player_hit_ms = 0U;
    state->signal_strike_cooldown_ms = 0U;
    state->signal_guard_window_ms = 0U;
    state->signal_guard_fx_ms = 0U;
    state->signal_passive_fx_ms = 0U;
    state->signal_guard_armed = false;
    state->signal_snare_ms = 0U;
    state->signal_recovery_bonus_ms = 0U;
    state->signal_shown_enemy_hp_q8 = (uint16_t)(
        (uint16_t)state->signal_battle_hp << 8U);
    state->signal_shown_player_hp_q8 = (uint16_t)(
        (uint16_t)state->signal_player_hp << 8U);
    play_tone(context, 330U, 90U);
}

static void consume_signal(p4_game_context_t *context,
                           byte_buddy_state_t *state,
                           const p4_game_signal_t *signal)
{
    if (signal == NULL || signal_consumed(state, signal->token) ||
        (!state->signal_training_mode &&
         !byte_buddy_signal_collection_has_room(
            state->signal_consumed_count))) {
        return;
    }
    if (state->signal_training_mode) {
        for (uint8_t index = 0U; index < state->signal_snapshot.count;
             ++index) {
            if (state->signal_snapshot.results[index].token !=
                signal->token) {
                continue;
            }
            state->signal_training_completed_mask |=
                (uint8_t)(1U << index);
            if (state->signal_training_completed_count != UINT8_MAX) {
                ++state->signal_training_completed_count;
            }
            break;
        }
        return;
    }
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(signal->token, signal->rssi_dbm);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(signal->token);
    const byte_buddy_signal_encounter_t encounter =
        byte_buddy_signal_encounter(
            signal->token, signal->rssi_dbm,
            signal->channel, signal->flags);
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
    const uint8_t reward_coins =
        byte_buddy_signal_reward_coins(profile, encounter);
    state->signal_session_coins =
        state->signal_session_coins > UINT16_MAX - reward_coins
            ? UINT16_MAX
            : (uint16_t)(state->signal_session_coins + reward_coins);
    const uint16_t growth = byte_buddy_signal_growth_reward(
        profile.rarity, state->signal_consumed_count);
    const uint16_t growth_before = state->care_actions;
    state->care_actions = state->care_actions > UINT16_MAX - growth
        ? UINT16_MAX : (uint16_t)(state->care_actions + growth);
    const uint16_t growth_added = (uint16_t)(
        state->care_actions - growth_before);
    state->signal_session_growth =
        state->signal_session_growth > UINT16_MAX - growth_added
            ? UINT16_MAX
            : (uint16_t)(state->signal_session_growth + growth_added);
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
        state->signal_battle_pattern != BYTE_BUDDY_BATTLE_PULSE_RUSH ||
        state->signal_battle_outcome != BYTE_BUDDY_COMBAT_ACTIVE ||
        state->signal_battle_phase == SIGNAL_PHASE_INTRO ||
        state->signal_battle_phase >= SIGNAL_PHASE_VICTORY ||
        state->signal_strike_cooldown_ms != 0U) {
        return;
    }
    const byte_buddy_signal_encounter_t encounter =
        byte_buddy_signal_encounter(
            signal->token, signal->rssi_dbm,
            signal->channel, signal->flags);
    const byte_buddy_battle_stats_t stats = current_battle_stats(state);
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    const byte_buddy_lineage_battle_traits_t battle_traits =
        byte_buddy_lineage_battle_traits(lineage);
    const byte_buddy_element_t element = state->element <
            BYTE_BUDDY_ELEMENT_COUNT
        ? (byte_buddy_element_t)state->element
        : BYTE_BUDDY_ELEMENT_MYSTERY;
    unsigned damage = 3U +
        state->upgrades[BYTE_BUDDY_UPGRADE_AURA] + stats.power / 14U +
        battle_traits.strike_damage;
    if (encounter.weakness == element) {
        damage += 2U;
    }
    if (state->signal_enemy_ward != 0U) {
        --state->signal_enemy_ward;
        damage = (damage + 1U) / 2U;
        if (encounter.passive == BYTE_BUDDY_SIGNAL_PASSIVE_WARD) {
            trigger_signal_passive_fx(state);
        }
    }
    if (damage > 12U) {
        damage = 12U;
    }
    state->signal_battle_hp = damage >= state->signal_battle_hp
        ? 0U : (uint8_t)(state->signal_battle_hp - damage);
    state->signal_hit_ms = SIGNAL_HIT_DURATION_MS;
    state->signal_strike_cooldown_ms =
        byte_buddy_signal_strike_cooldown_ms(stats.speed);
    if (state->signal_snare_ms != 0U &&
        state->signal_strike_cooldown_ms <= UINT16_MAX - 120U) {
        state->signal_strike_cooldown_ms = (uint16_t)(
            state->signal_strike_cooldown_ms + 120U);
    }
    play_tone(context, 740U, 45U);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_IMPACT);
    if (state->signal_battle_hp == 0U) {
        begin_signal_result(
            context, state, BYTE_BUDDY_COMBAT_VICTORY);
    }
}

static uint16_t signal_time_to_impact_ms(const byte_buddy_state_t *state)
{
    if (state->signal_battle_phase == SIGNAL_PHASE_WINDUP) {
        return (uint16_t)(state->signal_phase_ms +
                          SIGNAL_ATTACK_TRAVEL_MS);
    }
    return state->signal_battle_phase == SIGNAL_PHASE_TRAVEL
        ? state->signal_phase_ms : 0U;
}

static void guard_signal(p4_game_context_t *context,
                         byte_buddy_state_t *state)
{
    if (state->signal_view != BYTE_BUDDY_SIGNAL_BATTLE ||
        state->signal_guard_charges == 0U ||
        state->signal_battle_outcome != BYTE_BUDDY_COMBAT_ACTIVE ||
        (state->signal_battle_phase != SIGNAL_PHASE_WINDUP &&
         state->signal_battle_phase != SIGNAL_PHASE_TRAVEL) ||
        state->signal_guard_armed) {
        return;
    }
    const byte_buddy_lineage_battle_traits_t battle_traits =
        byte_buddy_lineage_battle_traits(current_lineage(state));
    const uint16_t time_to_impact_ms = signal_time_to_impact_ms(state);
    if (!byte_buddy_signal_parry_ready(
            time_to_impact_ms, battle_traits)) {
        return;
    }
    --state->signal_guard_charges;
    state->signal_guard_window_ms = (uint16_t)(time_to_impact_ms + 1U);
    state->signal_guard_fx_ms = 0U;
    state->signal_guard_armed = true;
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
            begin_scene_transition(state);
            begin_home_evolution_sequence(state);
        } else {
            if (view == BYTE_BUDDY_SIGNAL_BATTLE) {
                begin_signal_result(
                    context, state, BYTE_BUDDY_COMBAT_RETREATED);
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
            begin_scene_transition(state);
            begin_home_evolution_sequence(state);
        } else if (view == BYTE_BUDDY_SIGNAL_TRACKER) {
            return_to_signal_list(state);
        } else {
            begin_signal_result(
                context, state, BYTE_BUDDY_COMBAT_RETREATED);
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
    byte_buddy_signal_weave_rules_t rules)
{
    static const uint16_t notes[6] = {
        440U, 523U, 587U, 659U, 784U, 880U,
    };
    if (state->signal_weave_step >= rules.required_locks ||
        state->signal_battle_outcome != BYTE_BUDDY_COMBAT_ACTIVE) {
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
        begin_signal_result(
            context, state, BYTE_BUDDY_COMBAT_VICTORY);
    }
}

static bool signal_weave_can_advance(
    const byte_buddy_state_t *state,
    byte_buddy_signal_weave_rules_t rules)
{
    return state->signal_battle_outcome == BYTE_BUDDY_COMBAT_ACTIVE &&
        state->signal_battle_pattern == BYTE_BUDDY_BATTLE_RESONANCE_WEAVE &&
        state->signal_battle_phase != SIGNAL_PHASE_INTRO &&
        state->signal_weave_step < rules.required_locks;
}

static uint32_t signal_weave_next_event_ms(
    const byte_buddy_state_t *state,
    const p4_game_point_t *touch,
    byte_buddy_signal_genome_t genome,
    byte_buddy_signal_weave_rules_t rules)
{
    if (!signal_weave_can_advance(state, rules)) {
        return UINT32_MAX;
    }
    if (state->signal_weave_settle_ms != 0U) {
        return state->signal_weave_settle_ms;
    }
    const byte_buddy_signal_weave_node_t node =
        byte_buddy_signal_weave_node(genome, state->signal_weave_step);
    if (!signal_weave_touch_inside(touch, node, rules.touch_radius)) {
        return UINT32_MAX;
    }
    const uint32_t threshold_units = (uint32_t)rules.hold_ms * 2U;
    if (state->signal_weave_charge_units >= threshold_units) {
        return 0U;
    }
    const uint32_t rate_units_per_ms =
        state->signal_snare_ms != 0U ? 1U : 2U;
    const uint32_t remaining_units =
        threshold_units - state->signal_weave_charge_units;
    return (remaining_units + rate_units_per_ms - 1U) /
        rate_units_per_ms;
}

static void advance_signal_weave_segment(
    byte_buddy_state_t *state,
    const p4_game_point_t *touch,
    byte_buddy_signal_genome_t genome,
    byte_buddy_signal_weave_rules_t rules,
    uint32_t elapsed_ms)
{
    if (elapsed_ms == 0U || !signal_weave_can_advance(state, rules)) {
        return;
    }
    if (state->signal_weave_settle_ms != 0U) {
        state->signal_weave_settle_ms =
            state->signal_weave_settle_ms > elapsed_ms
                ? (uint16_t)(state->signal_weave_settle_ms - elapsed_ms)
                : 0U;
        return;
    }
    const byte_buddy_signal_weave_node_t node =
        byte_buddy_signal_weave_node(genome, state->signal_weave_step);
    const bool inside = signal_weave_touch_inside(
        touch, node, rules.touch_radius);
    const uint32_t threshold_units = (uint32_t)rules.hold_ms * 2U;
    if (!inside) {
        /* Snare slows progress, never the penalty for leaving the rune. */
        state->signal_weave_charge_units =
            state->signal_weave_charge_units > elapsed_ms
                ? state->signal_weave_charge_units - elapsed_ms : 0U;
        return;
    }
    const uint32_t rate_units_per_ms =
        state->signal_snare_ms != 0U ? 1U : 2U;
    const uint32_t remaining_units =
        state->signal_weave_charge_units < threshold_units
            ? threshold_units - state->signal_weave_charge_units : 0U;
    if (remaining_units == 0U ||
        elapsed_ms >=
            (remaining_units + rate_units_per_ms - 1U) /
                rate_units_per_ms) {
        state->signal_weave_charge_units = threshold_units;
    } else {
        state->signal_weave_charge_units +=
            elapsed_ms * rate_units_per_ms;
    }
}

static uint16_t tick_signal_timer(uint16_t timer, uint32_t elapsed_ms)
{
    return timer > elapsed_ms ? (uint16_t)(timer - elapsed_ms) : 0U;
}

static uint16_t approach_signal_bar(uint16_t shown, uint16_t target,
                                    uint32_t elapsed_ms)
{
    const uint32_t raw_step = elapsed_ms > UINT32_MAX / 18U
        ? UINT32_MAX : elapsed_ms * 18U;
    const uint16_t step = raw_step > UINT16_MAX
        ? UINT16_MAX : (uint16_t)raw_step;
    if (shown < target) {
        const uint16_t difference = (uint16_t)(target - shown);
        return difference <= step ? target : (uint16_t)(shown + step);
    }
    const uint16_t difference = (uint16_t)(shown - target);
    return difference <= step ? target : (uint16_t)(shown - step);
}

static void resolve_signal_attack(p4_game_context_t *context,
                                  byte_buddy_state_t *state,
                                  byte_buddy_signal_encounter_t encounter)
{
    const byte_buddy_signal_attack_t attack =
        byte_buddy_signal_attack_for(
            encounter, state->signal_attack_index);
    const byte_buddy_element_t element = state->element <
            BYTE_BUDDY_ELEMENT_COUNT
        ? (byte_buddy_element_t)state->element
        : BYTE_BUDDY_ELEMENT_MYSTERY;
    const bool guarded = state->signal_guard_armed;
    const byte_buddy_signal_defense_t defense =
        byte_buddy_signal_defense(
            encounter, byte_buddy_dragon_ability(element), guarded,
            state->signal_attack_index);
    state->signal_guard_window_ms = 0U;
    state->signal_guard_armed = false;
    if (guarded) {
        state->signal_guard_fx_ms = SIGNAL_GUARD_FX_MS;
    }
    state->signal_recovery_bonus_ms = defense.delay_ms;
    if (defense.status_ms > state->signal_snare_ms) {
        state->signal_snare_ms = defense.status_ms;
    }
    unsigned ward_damage = defense.ward_damage;
    const uint8_t ward_before_defense = state->signal_enemy_ward;
    while (ward_damage != 0U && state->signal_enemy_ward != 0U) {
        --state->signal_enemy_ward;
        --ward_damage;
    }
    unsigned counter_damage = defense.counter_damage;
    if (counter_damage != 0U && state->signal_enemy_ward != 0U) {
        --state->signal_enemy_ward;
        counter_damage = (counter_damage + 1U) / 2U;
    }
    if (encounter.passive == BYTE_BUDDY_SIGNAL_PASSIVE_WARD &&
        state->signal_enemy_ward < ward_before_defense) {
        trigger_signal_passive_fx(state);
    }
    state->signal_battle_hp = counter_damage >= state->signal_battle_hp
        ? 0U
        : (uint8_t)(state->signal_battle_hp - counter_damage);
    if (counter_damage != 0U) {
        state->signal_hit_ms = SIGNAL_HIT_DURATION_MS;
    }
    state->signal_player_hp =
        defense.player_damage >= state->signal_player_hp
            ? 0U
            : (uint8_t)(state->signal_player_hp -
                        defense.player_damage);
    if (defense.player_damage != 0U) {
        state->signal_player_hit_ms = SIGNAL_HIT_DURATION_MS;
    }
    if (defense.enemy_heal != 0U && state->signal_battle_hp != 0U) {
        const uint8_t hp_before_heal = state->signal_battle_hp;
        const unsigned healed = state->signal_battle_hp +
            defense.enemy_heal;
        state->signal_battle_hp = (uint8_t)(
            healed > state->signal_battle_max_hp
                ? state->signal_battle_max_hp : healed);
        if (state->signal_battle_hp > hp_before_heal) {
            trigger_signal_passive_fx(state);
        }
    }
    play_tone(context,
              (uint16_t)(220U + (unsigned)attack * 55U), 75U);
    (void)p4_game_audio_effect_play(
        context, &state->audio,
        guarded ? P4_GAME_AUDIO_EFFECT_ACTION
                : P4_GAME_AUDIO_EFFECT_IMPACT);
    if (state->signal_player_hp == 0U) {
        begin_signal_result(
            context, state, BYTE_BUDDY_COMBAT_DEFEAT_HP);
    } else if (state->signal_battle_hp == 0U) {
        begin_signal_result(
            context, state, BYTE_BUDDY_COMBAT_VICTORY);
    }
}

static void advance_signal_phase(p4_game_context_t *context,
                                 byte_buddy_state_t *state,
                                 byte_buddy_signal_encounter_t encounter)
{
    switch ((signal_battle_phase_t)state->signal_battle_phase) {
    case SIGNAL_PHASE_INTRO:
        set_signal_phase(
            state, SIGNAL_PHASE_OPEN, signal_attack_open_ms(encounter));
        break;
    case SIGNAL_PHASE_OPEN:
        set_signal_phase(
            state, SIGNAL_PHASE_WINDUP, encounter.telegraph_ms);
        if (encounter.passive == BYTE_BUDDY_SIGNAL_PASSIVE_OVERCLOCK ||
            (encounter.passive == BYTE_BUDDY_SIGNAL_PASSIVE_ECHO &&
             state->signal_attack_index % 3U == 2U)) {
            trigger_signal_passive_fx(state);
        }
        {
            const byte_buddy_signal_attack_t attack =
                byte_buddy_signal_attack_for(
                    encounter, state->signal_attack_index);
            play_tone(context,
                      (uint16_t)(294U + (unsigned)attack * 49U),
                      55U);
        }
        break;
    case SIGNAL_PHASE_WINDUP:
        set_signal_phase(
            state, SIGNAL_PHASE_TRAVEL, SIGNAL_ATTACK_TRAVEL_MS);
        break;
    case SIGNAL_PHASE_TRAVEL:
        set_signal_phase(
            state, SIGNAL_PHASE_IMPACT, SIGNAL_ATTACK_IMPACT_MS);
        resolve_signal_attack(context, state, encounter);
        break;
    case SIGNAL_PHASE_IMPACT:
        if (state->signal_battle_outcome == BYTE_BUDDY_COMBAT_ACTIVE) {
            const unsigned recovery = SIGNAL_ATTACK_RECOVERY_MS +
                state->signal_recovery_bonus_ms;
            set_signal_phase(
                state, SIGNAL_PHASE_RECOVERY,
                (uint16_t)(recovery > UINT16_MAX
                    ? UINT16_MAX : recovery));
        }
        break;
    case SIGNAL_PHASE_RECOVERY:
        if (state->signal_attack_index != UINT8_MAX) {
            ++state->signal_attack_index;
        }
        state->signal_recovery_bonus_ms = 0U;
        set_signal_phase(
            state, SIGNAL_PHASE_OPEN, signal_attack_open_ms(encounter));
        break;
    case SIGNAL_PHASE_VICTORY: {
        const p4_game_signal_t *const signal = selected_signal(state);
        consume_signal(context, state, signal);
        return_to_signal_list(state);
        break;
    }
    case SIGNAL_PHASE_DEFEAT:
        state->signal_view = BYTE_BUDDY_SIGNAL_TRACKER;
        begin_scene_transition(state);
        state->signal_weave_charge_units = 0U;
        break;
    default:
        set_signal_phase(
            state, SIGNAL_PHASE_OPEN, signal_attack_open_ms(encounter));
        break;
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
    const p4_game_signal_t *const signal = selected_signal(state);
    if (signal == NULL) {
        state->signal_view = BYTE_BUDDY_SIGNAL_TRACKER;
        begin_scene_transition(state);
        return;
    }
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(signal->token, signal->rssi_dbm);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(signal->token);
    const byte_buddy_signal_encounter_t encounter =
        byte_buddy_signal_encounter(
            signal->token, signal->rssi_dbm,
            signal->channel, signal->flags);
    const byte_buddy_battle_stats_t battle_stats =
        current_battle_stats(state);
    const byte_buddy_signal_weave_rules_t weave_rules =
        byte_buddy_signal_weave_rules_for_magic(
            genome, profile.strength,
            state->upgrades[BYTE_BUDDY_UPGRADE_MAGNET],
            battle_stats.magic);
    const uint32_t limit = signal_battle_duration_ms(state) +
        state->signal_battle_bonus_ms;

    /*
     * Resolve continuous charge and discrete attacks in timestamp order.  A
     * phase boundary wins an exact tie (so simultaneous lethal events remain
     * a defeat), then a completed rune, then the inclusive battle deadline.
     */
    uint32_t simulation_remaining_ms = elapsed_ms;
    for (unsigned event = 0U;
         event < 64U &&
             state->signal_view == BYTE_BUDDY_SIGNAL_BATTLE;
         ++event) {
        if (state->signal_phase_ms == 0U) {
            advance_signal_phase(context, state, encounter);
        }
        if (state->signal_view != BYTE_BUDDY_SIGNAL_BATTLE) {
            break;
        }
        const uint32_t weave_threshold_units =
            (uint32_t)weave_rules.hold_ms * 2U;
        if (signal_weave_can_advance(state, weave_rules) &&
            state->signal_weave_settle_ms == 0U &&
            state->signal_weave_charge_units >= weave_threshold_units) {
            capture_signal_weave_node(context, state, weave_rules);
        }
        if (state->signal_battle_outcome == BYTE_BUDDY_COMBAT_ACTIVE &&
            state->signal_battle_phase != SIGNAL_PHASE_INTRO &&
            state->signal_battle_elapsed_ms >= limit) {
            begin_signal_result(
                context, state, BYTE_BUDDY_COMBAT_DEFEAT_TIMEOUT);
        }
        if (simulation_remaining_ms == 0U ||
            state->signal_view != BYTE_BUDDY_SIGNAL_BATTLE) {
            break;
        }

        uint32_t step_ms = simulation_remaining_ms;
        if ((uint32_t)state->signal_phase_ms < step_ms) {
            step_ms = state->signal_phase_ms;
        }
        if (state->signal_snare_ms != 0U &&
            (uint32_t)state->signal_snare_ms < step_ms) {
            step_ms = state->signal_snare_ms;
        }
        if (state->signal_battle_outcome == BYTE_BUDDY_COMBAT_ACTIVE &&
            state->signal_battle_phase != SIGNAL_PHASE_INTRO) {
            const uint32_t deadline_ms =
                state->signal_battle_elapsed_ms < limit
                    ? limit - state->signal_battle_elapsed_ms : 0U;
            if (deadline_ms < step_ms) {
                step_ms = deadline_ms;
            }
            const uint32_t weave_event_ms = signal_weave_next_event_ms(
                state, touch, genome, weave_rules);
            if (weave_event_ms < step_ms) {
                step_ms = weave_event_ms;
            }
        }
        if (step_ms == 0U) {
            continue;
        }

        advance_signal_weave_segment(
            state, touch, genome, weave_rules, step_ms);
        state->signal_hit_ms = state->signal_hit_ms > step_ms
            ? state->signal_hit_ms - step_ms : 0U;
        state->signal_player_hit_ms =
            state->signal_player_hit_ms > step_ms
                ? state->signal_player_hit_ms - step_ms : 0U;
        state->signal_strike_cooldown_ms = tick_signal_timer(
            state->signal_strike_cooldown_ms, step_ms);
        state->signal_guard_window_ms = tick_signal_timer(
            state->signal_guard_window_ms, step_ms);
        if (state->signal_guard_armed &&
            state->signal_guard_window_ms == 0U) {
            state->signal_guard_armed = false;
        }
        state->signal_guard_fx_ms = tick_signal_timer(
            state->signal_guard_fx_ms, step_ms);
        state->signal_passive_fx_ms = tick_signal_timer(
            state->signal_passive_fx_ms, step_ms);
        state->signal_phase_ms = (uint16_t)(
            state->signal_phase_ms - step_ms);
        state->signal_snare_ms = tick_signal_timer(
            state->signal_snare_ms, step_ms);
        if (state->signal_battle_outcome == BYTE_BUDDY_COMBAT_ACTIVE &&
            state->signal_battle_phase != SIGNAL_PHASE_INTRO) {
            state->signal_battle_elapsed_ms += step_ms;
        }
        simulation_remaining_ms -= step_ms;
    }

    if (state->signal_view != BYTE_BUDDY_SIGNAL_BATTLE) {
        return;
    }
    state->signal_shown_enemy_hp_q8 = approach_signal_bar(
        state->signal_shown_enemy_hp_q8,
        (uint16_t)((uint16_t)state->signal_battle_hp << 8U), elapsed_ms);
    state->signal_shown_player_hp_q8 = approach_signal_bar(
        state->signal_shown_player_hp_q8,
        (uint16_t)((uint16_t)state->signal_player_hp << 8U), elapsed_ms);
}

static void update_play(p4_game_context_t *context, byte_buddy_state_t *state,
                        const p4_game_point_t *touch, bool touch_pressed,
                        uint32_t held_buttons, uint32_t elapsed_ms)
{
    if (state->mini_summary_ms != 0U) {
        if (state->mini_summary_ms > elapsed_ms) {
            state->mini_summary_ms = (uint16_t)(
                state->mini_summary_ms - elapsed_ms);
        } else {
            complete_play(state);
        }
        return;
    }
    if (state->mini_intro_ms != 0U) {
        state->mini_intro_ms = tick_signal_timer(
            state->mini_intro_ms, elapsed_ms);
        return;
    }
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
    state->star_spawn_ms = tick_signal_timer(
        state->star_spawn_ms, elapsed_ms);
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
            state->star_effect_kind = STAR_EFFECT_CAUGHT;
            const uint8_t joy_before = state->joy;
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
            record_action(
                context, state, ACTION_PLAY, REACTION_PLAY, joy_before);
            play_tone(context, 988U, 65U);
            (void)p4_game_audio_effect_play(
                context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
        } else {
            state->star_effect_kind = STAR_EFFECT_MISSED;
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
        state->shop_feedback_kind = SHOP_FEEDBACK_UNAVAILABLE;
        state->shop_feedback_ms = SHOP_FEEDBACK_MS;
        play_tone(context, 196U, 100U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
        return;
    }
    state->coins = (uint16_t)(state->coins - cost);
    state->upgrades[upgrade] = (uint8_t)(level + 1U);
    state->shop_feedback_kind = SHOP_FEEDBACK_POWER_GROWTH;
    state->shop_feedback_ms = SHOP_FEEDBACK_MS;
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
    state->shop_feedback_kind = SHOP_FEEDBACK_EQUIPPED;
    state->shop_feedback_ms = SHOP_FEEDBACK_MS;
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
        state->shop_feedback_kind = SHOP_FEEDBACK_UNAVAILABLE;
        state->shop_feedback_ms = SHOP_FEEDBACK_MS;
        play_tone(context, 196U, 100U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
        return;
    }
    state->coins = (uint16_t)(state->coins - cost);
    state->style_unlocked[style] = (uint8_t)(unlocked + 1U);
    state->style_selected[style] = (uint8_t)(unlocked + 1U);
    state->shop_feedback_kind = SHOP_FEEDBACK_UNLOCKED;
    state->shop_feedback_ms = SHOP_FEEDBACK_MS;
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
    state->shop_feedback_kind = SHOP_FEEDBACK_EQUIPPED;
    state->shop_feedback_ms = SHOP_FEEDBACK_MS;
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
        return request_game_exit(state);
    }
    if (state->upgrade_shop) {
        switch (target) {
        case BYTE_BUDDY_TOUCH_SHOP_POWER:
            if (state->style_shop) {
                state->style_shop = false;
                begin_scene_transition(state);
            }
            break;
        case BYTE_BUDDY_TOUCH_SHOP_STYLE:
            if (!state->style_shop) {
                state->style_shop = true;
                begin_scene_transition(state);
            }
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
            begin_scene_transition(state);
            break;
        default:
            break;
        }
        return P4_GAME_CONTINUE;
    }
    switch (target) {
    case BYTE_BUDDY_TOUCH_DRAGON: {
        const uint8_t joy_before = state->joy;
        state->joy = increase(state->joy, (uint8_t)(10U +
            state->upgrades[BYTE_BUDDY_UPGRADE_NEST] * 2U));
        record_pet(context, state, joy_before);
        play_tone(context, 880U, 70U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_ACTION);
        break;
    }
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
        begin_scene_transition(state);
        state->shop_feedback_ms = 0U;
        break;
    case BYTE_BUDDY_TOUCH_SIGNAL_SCAN:
        open_signal_hunt(context, state);
        break;
    case BYTE_BUDDY_TOUCH_PREVIEW:
        state->lineage_panel = true;
        begin_scene_transition(state);
        break;
    default:
        break;
    }
    return P4_GAME_CONTINUE;
}

static byte_buddy_save_profile_t capture_save_profile(
    const byte_buddy_state_t *state)
{
    const uint16_t durable_growth =
        state->care_actions >= state->signal_session_growth
            ? (uint16_t)(state->care_actions - state->signal_session_growth)
            : 0U;
    uint32_t durable_achievements = state->achievement_mask &
        (ACHIEVEMENT_FIRST_CARE | ACHIEVEMENT_CLEAN |
         ACHIEVEMENT_PLAY | ACHIEVEMENT_GROW);
    if (durable_growth < GROW_ELEMENTAL_INTERACTIONS) {
        durable_achievements &= ~(uint32_t)ACHIEVEMENT_GROW;
    }
    byte_buddy_save_profile_t profile = {
        .hunger = state->hunger,
        .joy = state->joy,
        .hygiene = state->hygiene,
        .energy = state->energy,
        .coins = state->coins,
        .care_actions = durable_growth,
        .style_mix_count = state->style_mix_count,
        .pet_actions = state->pet_actions,
        .achievement_mask = durable_achievements,
    };
    for (unsigned index = 0U; index < ACTION_COUNT; ++index) {
        profile.action_counts[index] = state->action_counts[index];
    }
    for (unsigned index = 0U; index < BYTE_BUDDY_UPGRADE_COUNT; ++index) {
        profile.upgrades[index] = state->upgrades[index];
    }
    for (unsigned index = 0U; index < BYTE_BUDDY_STYLE_COUNT; ++index) {
        profile.style_unlocked[index] = state->style_unlocked[index];
        profile.style_selected[index] = state->style_selected[index];
    }
    return profile;
}

static void apply_save_profile(
    byte_buddy_state_t *state,
    const byte_buddy_save_profile_t *profile)
{
    state->hunger = profile->hunger;
    state->joy = profile->joy;
    state->hygiene = profile->hygiene;
    state->energy = profile->energy;
    state->coins = profile->coins;
    state->care_actions = profile->care_actions;
    state->style_mix_count = profile->style_mix_count;
    state->pet_actions = profile->pet_actions;
    state->achievement_mask = profile->achievement_mask;
    for (unsigned index = 0U; index < ACTION_COUNT; ++index) {
        state->action_counts[index] = profile->action_counts[index];
    }
    for (unsigned index = 0U; index < BYTE_BUDDY_UPGRADE_COUNT; ++index) {
        state->upgrades[index] = profile->upgrades[index];
    }
    for (unsigned index = 0U; index < BYTE_BUDDY_STYLE_COUNT; ++index) {
        state->style_unlocked[index] = profile->style_unlocked[index];
        state->style_selected[index] = profile->style_selected[index];
    }
    state->stage = (uint8_t)byte_buddy_stage_for_interactions(
        state->care_actions);
    update_dragon_traits(state);
}

static bool save_progress_signature(
    byte_buddy_save_profile_t profile,
    uint32_t *signature)
{
    /* Passive need decay should not write to SD every five seconds. Needs are
     * still captured opportunistically whenever durable progress changes. */
    profile.hunger = 0U;
    profile.joy = 0U;
    profile.hygiene = 0U;
    profile.energy = 0U;
    uint8_t payload[BYTE_BUDDY_SAVE_PAYLOAD_BYTES];
    if (signature == NULL ||
        byte_buddy_save_encode(&profile, payload, sizeof(payload)) == 0U) {
        return false;
    }
    *signature = read_u32(
        payload + BYTE_BUDDY_SAVE_PAYLOAD_BYTES - 4U);
    return true;
}

static void refresh_save_dirty(byte_buddy_state_t *state)
{
    if (!state->save_available) {
        return;
    }
    uint32_t signature = 0U;
    if (!save_progress_signature(
            capture_save_profile(state), &signature)) {
        state->save_error = true;
        state->save_available = false;
        return;
    }
    if (signature == state->save_progress_signature) {
        return;
    }
    state->save_progress_signature = signature;
    state->save_local_generation =
        state->save_local_generation == UINT32_MAX
            ? 1U : state->save_local_generation + 1U;
    state->save_dirty = true;
}

static void mark_save_dirty(byte_buddy_state_t *state)
{
    if (!state->save_available) {
        return;
    }
    state->save_local_generation =
        state->save_local_generation == UINT32_MAX
            ? 1U : state->save_local_generation + 1U;
    state->save_dirty = true;
}

static void service_save(
    p4_game_context_t *context, byte_buddy_state_t *state)
{
    if (!state->save_available) {
        return;
    }
    if (state->save_ticket != P4_GAME_SAVE_INVALID_TICKET) {
        p4_game_save_status_t status = P4_GAME_SAVE_NONE;
        uint32_t committed_sequence = 0U;
        if (!p4_game_read_save_status(
                context, state->save_ticket, &status,
                &committed_sequence)) {
            /* The Console OS save worker may briefly hold its status mutex.
             * Retain the accepted ticket and poll again next frame. */
            return;
        }
        if (status == P4_GAME_SAVE_NONE ||
            status == P4_GAME_SAVE_READY ||
            status == P4_GAME_SAVE_QUEUED) {
            return;
        }
        state->save_ticket = P4_GAME_SAVE_INVALID_TICKET;
        if (status != P4_GAME_SAVE_COMMITTED || committed_sequence == 0U) {
            state->save_error = true;
            if (status == P4_GAME_SAVE_UNAVAILABLE ||
                status == P4_GAME_SAVE_CONFLICT ||
                status == P4_GAME_SAVE_ERROR) {
                state->save_available = false;
            } else {
                state->save_retry_ms = 1000U;
            }
            return;
        }
        state->save_host_sequence = committed_sequence;
        state->save_error = false;
        if (state->save_local_generation ==
                state->save_queued_generation) {
            state->save_dirty = false;
        }
    }
    if (state->save_retry_ms != 0U || !state->save_dirty ||
        state->save_ticket != P4_GAME_SAVE_INVALID_TICKET) {
        return;
    }
    const byte_buddy_save_profile_t profile = capture_save_profile(state);
    uint8_t payload[BYTE_BUDDY_SAVE_PAYLOAD_BYTES];
    const size_t payload_bytes = byte_buddy_save_encode(
        &profile, payload, sizeof(payload));
    p4_game_save_ticket_t ticket = P4_GAME_SAVE_INVALID_TICKET;
    if (payload_bytes == 0U || !p4_game_queue_save(
            context, "AUTO", BYTE_BUDDY_SAVE_SCHEMA_VERSION,
            state->save_host_sequence, payload, payload_bytes, &ticket)) {
        state->save_error = true;
        state->save_retry_ms = 1000U;
        return;
    }
    state->save_ticket = ticket;
    state->save_queued_generation = state->save_local_generation;
    state->save_captured_needs = save_needs_signature(state);
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(byte_buddy_state_t) ||
        context->services == NULL ||
        (context->services->available_capabilities &
         P4_GAME_CAP_STORAGE) == 0U ||
        context->services->resource_format_version != 1U) {
        return false;
    }
    byte_buddy_state_t *const state = context->state;
    uint16_t art_sheets = 0U;
    if (!art_bank_valid(
            context->services->resource_data,
            context->services->resource_bytes,
            DRAGON_EXTENDED_SHEET_COUNT, &art_sheets)) {
        return false;
    }
    *state = (byte_buddy_state_t){
        .hunger = 72U,
        .joy = 68U,
        .hygiene = 75U,
        .energy = 70U,
        .coins = 4U,
        .last_care_credit = BYTE_BUDDY_CARE_NONE,
        .evolution_active_stage = BYTE_BUDDY_STAGE_COUNT,
        .catcher_x_q16 = INT32_C(160) * P4_Q16_ONE,
        .catcher_target_x_q16 = INT32_C(160) * P4_Q16_ONE,
        .art_data = context->services->resource_data,
        .art_bytes = context->services->resource_bytes,
        .art_sheets = art_sheets,
    };
    state->save_available =
        (context->services->available_capabilities & P4_GAME_CAP_SAVE) != 0U;
    if (state->save_available) {
        state->save_host_sequence = context->services->save_sequence;
        if (context->services->save_bytes != 0U) {
            byte_buddy_save_profile_t profile;
            if (context->services->save_schema_version !=
                BYTE_BUDDY_SAVE_SCHEMA_VERSION) {
                state->save_error = true;
                state->save_available = false;
            } else if (context->services->save_sequence == 0U ||
                       !byte_buddy_save_decode(
                           &profile, context->services->save_data,
                           context->services->save_bytes)) {
                state->save_error = true;
            } else {
                apply_save_profile(state, &profile);
            }
        }
        if (state->save_available) {
            if (!save_progress_signature(
                    capture_save_profile(state),
                    &state->save_progress_signature)) {
                state->save_error = true;
                state->save_available = false;
            }
            state->save_captured_needs = save_needs_signature(state);
            rehydrate_achievements(context, state);
        }
    }
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
    state->save_retry_ms = tick_signal_timer(
        state->save_retry_ms, bounded_elapsed_ms);
    refresh_save_dirty(state);
    service_save(context, state);
    if (state->exit_pending) {
        /* Keep the authored saving indicator alive while scene input stays
         * latched.  The underlying scene deliberately does not advance. */
        state->animation_ms += bounded_elapsed_ms;
        if (!state->save_available ||
            (!state->save_dirty &&
             state->save_ticket == P4_GAME_SAVE_INVALID_TICKET)) {
            return P4_GAME_EXIT_TO_LAUNCHER;
        }
        const unsigned waited = (unsigned)state->save_exit_wait_ms +
            bounded_elapsed_ms;
        state->save_exit_wait_ms = (uint16_t)(
            waited > 3000U ? 3000U : waited);
        return state->save_exit_wait_ms >= 3000U
            ? P4_GAME_EXIT_TO_LAUNCHER : P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return request_game_exit(state);
    }
    if (state->lineage_panel && state->scene_transition_ms == 0U &&
        (input->pressed & P4_BUTTON_B) != 0U) {
        state->lineage_panel = false;
        begin_scene_transition(state);
        state->touch_was_down =
            input->touch_valid && input->touch_count > 0U;
        return P4_GAME_CONTINUE;
    }
    state->animation_ms += bounded_elapsed_ms;
    if (byte_buddy_reaction_can_advance(
            state->evolution_fx_ms != 0U,
            state->scene_transition_ms != 0U,
            state->upgrade_shop, state->signal_hunt,
            (byte_buddy_signal_view_t)state->signal_view)) {
        update_reaction(state, bounded_elapsed_ms);
    }
    state->care_credit_cooldown_ms =
        state->care_credit_cooldown_ms > bounded_elapsed_ms
            ? state->care_credit_cooldown_ms - bounded_elapsed_ms : 0U;
    state->signal_reward_ms = state->signal_reward_ms > bounded_elapsed_ms
        ? state->signal_reward_ms - bounded_elapsed_ms : 0U;
    state->signal_request_busy_ms =
        state->signal_request_busy_ms > bounded_elapsed_ms
            ? state->signal_request_busy_ms - bounded_elapsed_ms : 0U;
    const bool signal_scan_was_pending =
        state->signal_snapshot.status == P4_GAME_SIGNAL_SCANNING &&
        state->signal_scan_timeout_ms != 0U;
    state->signal_scan_timeout_ms =
        state->signal_scan_timeout_ms > bounded_elapsed_ms
            ? state->signal_scan_timeout_ms - bounded_elapsed_ms : 0U;
    if (signal_scan_was_pending && state->signal_scan_timeout_ms == 0U &&
        state->signal_snapshot.status == P4_GAME_SIGNAL_SCANNING) {
        state->signal_snapshot.status = P4_GAME_SIGNAL_ERROR;
    }
    const bool evolution_was_active = state->evolution_fx_ms != 0U;
    state->evolution_fx_ms = tick_signal_timer(
        state->evolution_fx_ms, bounded_elapsed_ms);
    state->shop_feedback_ms = tick_signal_timer(
        state->shop_feedback_ms, bounded_elapsed_ms);
    state->scene_transition_ms = tick_signal_timer(
        state->scene_transition_ms, bounded_elapsed_ms);
    apply_decay(state, bounded_elapsed_ms);
    const bool touch_now = input->touch_valid && input->touch_count > 0U;
    const bool pending_play_signal_button =
        state->play_start_pending != 0U &&
        (input->pressed & P4_BUTTON_UP) != 0U;
    const bool pending_play_signal_touch =
        state->play_start_pending != 0U && touch_now &&
        !state->touch_was_down &&
        byte_buddy_touch_target(
            input->touches[0].x, input->touches[0].y,
            false, false, false) == BYTE_BUDDY_TOUCH_SIGNAL_SCAN;
    if (pending_play_signal_button || pending_play_signal_touch) {
        /* Play care is already committed; this only skips its queued mini-game
         * so a deliberate Signal Hunt navigation input is never lost. */
        state->play_start_pending = 0U;
        state->signal_hunt_start_pending = true;
        state->controller_active = !pending_play_signal_touch;
        finish_reaction_now(state);
    }
    if (evolution_was_active && state->evolution_fx_ms == 0U) {
        state->evolution_active_stage = BYTE_BUDDY_STAGE_COUNT;
        const bool next_evolution = begin_next_evolution(state, 0U);
        if (!next_evolution && state->signal_hunt_start_pending) {
            state->signal_hunt_start_pending = false;
            open_signal_hunt(context, state);
        }
        /* Never leak the completion-frame edge into the newly revealed scene. */
        state->touch_was_down = touch_now;
        return P4_GAME_CONTINUE;
    }
    if (state->signal_hunt_start_pending &&
        state->evolution_fx_ms == 0U &&
        state->evolution_pending_mask == 0U) {
        state->signal_hunt_start_pending = false;
        open_signal_hunt(context, state);
        state->touch_was_down = touch_now;
        return P4_GAME_CONTINUE;
    }
    if (byte_buddy_play_start_ready(
            state->play_start_pending != 0U, state->mini_game,
            (buddy_reaction_t)state->reaction, state->reaction_ms,
            state->evolution_fx_ms, state->evolution_pending_mask)) {
        start_play(state);
        state->touch_was_down = touch_now;
        return P4_GAME_CONTINUE;
    }
    if (state->play_start_pending != 0U) {
        /* Keep Play exclusive until its Home reaction (and any queued growth)
         * has completed, and scrub the release edge before scene hand-off. */
        state->touch_was_down = touch_now;
        return P4_GAME_CONTINUE;
    }
    const bool scene_input_locked = state->scene_transition_ms != 0U;
    const bool raw_touch_pressed =
        touch_now && !state->touch_was_down;
    if (scene_input_locked && state->mini_game) {
        bool finish_requested =
            (input->pressed & P4_BUTTON_B) != 0U;
        if (!finish_requested && raw_touch_pressed &&
            byte_buddy_touch_target(
                input->touches[0].x, input->touches[0].y,
                false, false, true) ==
                BYTE_BUDDY_TOUCH_DONE_PLAYING) {
            finish_requested = true;
        }
        if (finish_requested) {
            finish_play(state);
            state->touch_was_down = touch_now;
            return P4_GAME_CONTINUE;
        }
    }
    p4_game_input_t scene_input;
    if (scene_input_locked) {
        scene_input = *input;
        scene_input.held = 0U;
        scene_input.pressed = 0U;
        scene_input.released = 0U;
        input = &scene_input;
    }
    const bool touch_pressed = !scene_input_locked &&
        touch_now && !state->touch_was_down;
    const p4_game_point_t *const touch =
        !scene_input_locked && touch_now ? &input->touches[0] : NULL;
    if (!scene_input_locked && touch_now) {
        state->controller_active = false;
    } else if ((input->held | input->pressed) != 0U) {
        state->controller_active = true;
    }
    if (state->evolution_fx_ms != 0U) {
        state->touch_was_down = touch_now;
        return P4_GAME_CONTINUE;
    }
    if (state->lineage_panel) {
        if (touch_pressed && touch != NULL &&
            ((touch->x < 58U && touch->y < 25U) || touch->y >= 168U)) {
            state->lineage_panel = false;
            begin_scene_transition(state);
        }
        state->touch_was_down = touch_now;
        return P4_GAME_CONTINUE;
    }
    const uint8_t signal_view_before_update = state->signal_view;
    bool signal_input_handled_before_update = false;
    p4_game_point_t controller_touch = {0};
    const p4_game_point_t *battle_touch = touch;
    if (state->signal_hunt) {
        poll_signal_scan(context, state);
        update_signal_tracking(context, state, bounded_elapsed_ms);
        if (state->signal_view == BYTE_BUDDY_SIGNAL_BATTLE) {
            if (touch_pressed && touch != NULL) {
                (void)activate_signal_touch(context, state, touch);
                signal_input_handled_before_update = true;
            } else if (!touch_now && input->pressed != 0U) {
                activate_signal_controller(context, state, input);
                signal_input_handled_before_update = true;
            }
        }
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
        if (touch_pressed && touch != NULL) {
            const byte_buddy_touch_target_t mini_target =
                byte_buddy_touch_target(
                    touch->x, touch->y, false, false, true);
            if (mini_target == BYTE_BUDDY_TOUCH_EXIT) {
                state->touch_was_down = touch_now;
                return request_game_exit(state);
            }
            if (mini_target == BYTE_BUDDY_TOUCH_DONE_PLAYING) {
                finish_play(state);
                state->touch_was_down = touch_now;
                return P4_GAME_CONTINUE;
            }
        }
        if (state->mini_intro_ms != 0U ||
            state->mini_summary_ms != 0U) {
            update_play(context, state, NULL, false, 0U,
                        bounded_elapsed_ms);
            state->touch_was_down = touch_now;
            return P4_GAME_CONTINUE;
        }
        update_play(context, state, touch, touch_pressed,
                    input->held, bounded_elapsed_ms);
        state->touch_was_down = touch_now;
        return P4_GAME_CONTINUE;
    }
    if (state->signal_hunt) {
        if (!signal_input_handled_before_update &&
            !signal_view_changed_during_update &&
            touch_pressed && touch != NULL) {
            const p4_game_result_t result = activate_signal_touch(
                context, state, touch);
            state->touch_was_down = touch_now;
            return result;
        }
        if (!signal_input_handled_before_update &&
            !signal_view_changed_during_update && !touch_now &&
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
                begin_scene_transition(state);
            } else if ((input->pressed & P4_BUTTON_LEFT) != 0U) {
                if (state->style_shop) {
                    state->style_shop = false;
                    begin_scene_transition(state);
                }
            } else if ((input->pressed & P4_BUTTON_RIGHT) != 0U) {
                if (!state->style_shop) {
                    state->style_shop = true;
                    begin_scene_transition(state);
                }
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
            begin_scene_transition(state);
            state->shop_feedback_ms = 0U;
        } else if ((input->pressed & P4_BUTTON_B) != 0U) {
            state->lineage_panel = true;
            begin_scene_transition(state);
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
    const byte_buddy_battle_stats_t base =
        byte_buddy_battle_stats_for_growth(
            state->care_actions,
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

bool byte_buddy_dragon_reaction_frame(
    byte_buddy_stage_t stage, byte_buddy_morph_t hatch_morph,
    buddy_reaction_t reaction, uint16_t care_actions,
    uint32_t reaction_ms, byte_buddy_animation_cell_t *out_cell)
{
    if (out_cell == NULL) {
        return false;
    }
    const uint8_t phase = (uint8_t)reaction_frame_count(reaction_ms, 4U);
    if (stage == BYTE_BUDDY_STAGE_EGG) {
        if (care_actions < 5U || hatch_morph >= BYTE_BUDDY_MORPH_COUNT) {
            return false;
        }
        const uint8_t milestone = care_actions >= 7U
            ? 2U : (uint8_t)(care_actions - 5U);
        *out_cell = (byte_buddy_animation_cell_t){
            .sheet = (uint8_t)(DRAGON_HATCH_NEBULA_SHEET + hatch_morph),
            .frame = (uint8_t)(milestone * 4U + phase),
        };
        return true;
    }
    if (stage == BYTE_BUDDY_STAGE_BABY && reaction == REACTION_HATCH) {
        if (hatch_morph >= BYTE_BUDDY_MORPH_COUNT) {
            return false;
        }
        *out_cell = (byte_buddy_animation_cell_t){
            .sheet = (uint8_t)(DRAGON_HATCH_NEBULA_SHEET + hatch_morph),
            .frame = (uint8_t)(3U * 4U + phase),
        };
        return true;
    }
    if (stage < BYTE_BUDDY_STAGE_BABY ||
        stage > BYTE_BUDDY_STAGE_ELEMENTAL ||
        reaction < REACTION_FEED || reaction > REACTION_SIGNAL) {
        return false;
    }
    *out_cell = (byte_buddy_animation_cell_t){
        .sheet = (uint8_t)(BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET + reaction),
        .frame = (uint8_t)(
            ((unsigned)stage - BYTE_BUDDY_STAGE_BABY) * 4U + phase),
    };
    return true;
}

bool byte_buddy_reaction_effect_frame(
    buddy_reaction_t reaction, uint32_t reaction_ms,
    byte_buddy_animation_cell_t *out_cell)
{
    if (out_cell == NULL) {
        return false;
    }
    uint8_t sheet = BYTE_BUDDY_REACTION_FX_SHEET;
    uint8_t row = 0U;
    switch (reaction) {
    case REACTION_FEED:
        sheet = BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET;
        row = 0U;
        break;
    case REACTION_PLAY:
        sheet = BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET;
        row = 1U;
        break;
    case REACTION_CLEAN:
        row = 1U;
        break;
    case REACTION_REST:
        row = 2U;
        break;
    case REACTION_PET:
        sheet = BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET;
        row = 2U;
        break;
    case REACTION_GROW:
        row = 3U;
        break;
    case REACTION_SIGNAL:
        sheet = BYTE_BUDDY_ACTION_SIGNATURE_FX_SHEET;
        row = 3U;
        break;
    case REACTION_IDLE:
    case REACTION_HATCH:
    default:
        return false;
    }
    *out_cell = (byte_buddy_animation_cell_t){
        .sheet = sheet,
        .frame = (uint8_t)(
            row * 4U + reaction_frame_count(reaction_ms, 4U)),
    };
    return true;
}

static unsigned elemental_mastery_row(const byte_buddy_state_t *state)
{
    /* Mastery is authored Mystery, Fire, Ice, Acid. */
    return safe_element(state);
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

static bool evolution_reaction_active(const byte_buddy_state_t *state)
{
    return state->evolution_fx_ms != 0U &&
        state->evolution_active_stage >= BYTE_BUDDY_STAGE_BABY &&
        state->evolution_active_stage <= BYTE_BUDDY_STAGE_ELEMENTAL;
}

static unsigned visual_dragon_stage(const byte_buddy_state_t *state)
{
    return evolution_reaction_active(state)
        ? state->evolution_active_stage : safe_stage(state);
}

static buddy_reaction_t visual_dragon_reaction(
    const byte_buddy_state_t *state, buddy_reaction_t fallback)
{
    if (!evolution_reaction_active(state)) {
        return fallback;
    }
    const byte_buddy_stage_t next_stage =
        (byte_buddy_stage_t)state->evolution_active_stage;
    const byte_buddy_stage_t previous_stage = next_stage ==
            BYTE_BUDDY_STAGE_BABY
        ? BYTE_BUDDY_STAGE_EGG
        : (byte_buddy_stage_t)(next_stage - 1U);
    return byte_buddy_growth_reaction(previous_stage, next_stage);
}

static uint32_t visual_reaction_remaining_ms(
    const byte_buddy_state_t *state, uint32_t fallback_ms)
{
    if (!evolution_reaction_active(state)) {
        return fallback_ms;
    }
    return state->evolution_fx_ms > REACTION_DURATION_MS
        ? REACTION_DURATION_MS : state->evolution_fx_ms;
}

static uint32_t dragon_frame_interval_ms(unsigned stage)
{
    static const uint16_t intervals[BYTE_BUDDY_STAGE_COUNT] = {
        220U, 190U, 175U, 150U, 165U,
    };
    return intervals[stage < BYTE_BUDDY_STAGE_COUNT
        ? stage : BYTE_BUDDY_STAGE_EGG];
}

static void dragon_sheet_frame(const byte_buddy_state_t *state,
                               uint32_t animation_ms,
                               uint8_t reaction,
                               uint32_t reaction_ms,
                               unsigned *out_sheet,
                               unsigned *out_frame)
{
    const unsigned stage = visual_dragon_stage(state);
    const buddy_reaction_t displayed_reaction = visual_dragon_reaction(
        state, (buddy_reaction_t)reaction);
    const uint32_t displayed_reaction_ms = visual_reaction_remaining_ms(
        state, reaction_ms);
    const uint32_t frame_interval_ms = dragon_frame_interval_ms(stage);
    const uint32_t idle_animation_ms =
        animation_ms - state->dragon_idle_epoch_ms;
    const unsigned phase4 = (unsigned)(
        (idle_animation_ms / frame_interval_ms) % 4U);
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    byte_buddy_animation_cell_t reaction_cell;
    if (byte_buddy_dragon_reaction_frame(
            (byte_buddy_stage_t)stage,
            (byte_buddy_morph_t)hatch_variant(state),
            displayed_reaction, state->care_actions,
            displayed_reaction_ms, &reaction_cell)) {
        *out_sheet = reaction_cell.sheet;
        *out_frame = reaction_cell.frame;
        return;
    }
    if (stage == BYTE_BUDDY_STAGE_EGG) {
        const bool spiked = safe_wing_style(state) ==
            BYTE_BUDDY_WINGS_SPIKED;
        if (state->care_actions < 5U) {
            unsigned variant = safe_morph(state);
            if (state->care_actions == 0U) {
                /* The untouched egg begins as the authored Mystery loop. */
                *out_sheet = DRAGON_ELEMENT_EGG_AMBIENT_SHEET;
                variant = BYTE_BUDDY_ELEMENT_MYSTERY;
            } else if (spiked &&
                       safe_element(state) == BYTE_BUDDY_ELEMENT_ACID &&
                       ((animation_ms /
                         (frame_interval_ms * 4U)) & 1U) != 0U) {
                /* Switch only at loop boundaries: acid can reveal Jade DNA. */
                *out_sheet = DRAGON_MORPH_EGG_AMBIENT_SHEET;
                variant = BYTE_BUDDY_MORPH_JADE;
            } else if (spiked) {
                *out_sheet = DRAGON_ELEMENT_EGG_AMBIENT_SHEET;
                variant = safe_element(state);
            } else {
                *out_sheet = DRAGON_MORPH_EGG_AMBIENT_SHEET;
            }
            *out_frame = phase4 * 4U + variant;
            return;
        }
        /* The validated reaction resolver owns every care-five-plus egg. */
        *out_sheet = DRAGON_HATCH_NEBULA_SHEET;
        *out_frame = 3U;
        return;
    }
    if (stage == BYTE_BUDDY_STAGE_BABY) {
        const uint32_t clip_ms = frame_interval_ms * 4U * 3U;
        unsigned row = (unsigned)((idle_animation_ms / clip_ms) % 2U);
        if (state->energy < 35U) {
            row = 3U;
        } else if (state->joy >= 80U) {
            row = 2U;
        }
        *out_sheet = DRAGON_BABY_IDLE_SHEET;
        *out_frame = row * 4U + phase4;
        return;
    }
    if (stage == BYTE_BUDDY_STAGE_WINGED) {
        const unsigned style = safe_wing_style(state) ==
            BYTE_BUDDY_WINGS_SHINY ? 1U : 0U;
        const uint32_t clip_ms = frame_interval_ms * 4U * 3U;
        const unsigned active_clip = (unsigned)(
            (idle_animation_ms / clip_ms) % 2U);
        *out_sheet = DRAGON_WINGED_IDLE_SHEET;
        *out_frame = (style + active_clip * 2U) * 4U + phase4;
        return;
    }
    if (stage == BYTE_BUDDY_STAGE_FLYING) {
        const unsigned style = safe_wing_style(state) ==
            BYTE_BUDDY_WINGS_SHINY ? 1U : 0U;
        const uint32_t clip_ms = frame_interval_ms * 4U * 3U;
        const unsigned clip = (unsigned)(
            (idle_animation_ms / clip_ms) % 3U);
        *out_sheet = clip == 0U ? DRAGON_FLIGHT_CYCLE_SHEET :
            DRAGON_FLIGHT_AEROBATICS_SHEET;
        const unsigned row = clip < 2U ? style : style + 2U;
        *out_frame = row * 4U + phase4;
        return;
    }
    if (stage == BYTE_BUDDY_STAGE_ELEMENTAL &&
        rare_morph_unlocked(state)) {
        if (lineage.tier >= BYTE_BUDDY_LINEAGE_ASCENDED) {
            *out_sheet = DRAGON_SIGNAL_GENETICS_SHEET;
            *out_frame = phase4 * 4U + lineage.family;
            return;
        }
        *out_sheet = DRAGON_RARE_SHEET;
        *out_frame = safe_morph(state) + 4U * (unsigned)p4_animation_frame(
            idle_animation_ms, frame_interval_ms, 4U, true);
        return;
    }
    if (stage == BYTE_BUDDY_STAGE_ELEMENTAL) {
        if (lineage.tier >= BYTE_BUDDY_LINEAGE_ASCENDED) {
            *out_sheet = DRAGON_SIGNAL_GENETICS_SHEET;
            *out_frame = phase4 * 4U + lineage.family;
            return;
        }
        *out_sheet = DRAGON_ELEMENT_MASTERY_SHEET;
        *out_frame = elemental_mastery_row(state) * 4U + phase4;
        return;
    }

    /* All five stages above have authored v4 routes; stay valid if corrupt. */
    *out_sheet = DRAGON_BABY_IDLE_SHEET;
    *out_frame = phase4;
}

static uint16_t rgb888_to_rgb565(unsigned red, unsigned green, unsigned blue)
{
    return (uint16_t)(((red >> 3U) << 11U) |
                      ((green >> 2U) << 5U) | (blue >> 3U));
}

static int eased_ping_pong_offset(uint32_t animation_ms,
                                  uint32_t step_ms,
                                  int minimum, int maximum)
{
    if (step_ms == 0U || maximum <= minimum) {
        return minimum;
    }
    const unsigned phase = (unsigned)(animation_ms / step_ms) & 31U;
    const int range = maximum - minimum;
    return minimum + ((int)s_eased_motion[phase] * range + 5) / 10;
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
    if (size == 0U || size > DRAGON_FRAME_WIDTH * 2U) {
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

static bool draw_customized_frame_scaled(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    unsigned sheet, unsigned frame,
    int center_x, int center_y, unsigned size)
{
    if (size == 0U || size > DRAGON_FRAME_WIDTH * 2U) {
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
                color = lift_sprite_color(color);
                color = customize_color(
                    state, color, (int)source_x, (int)source_y);
                surface->pixels[(size_t)destination_y *
                    surface->stride_pixels + (size_t)destination_x] = color;
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

static void draw_signal_city_icon(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    unsigned frame, int center_x, int center_y, unsigned size)
{
    if (frame < DRAGON_FRAMES_PER_SHEET) {
        (void)draw_art_frame_scaled(
            surface, state, BYTE_BUDDY_SIGNAL_CITY_SHEET,
            frame, center_x, center_y, size);
    }
}

static void draw_environment_chrome_icon(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    unsigned frame, int center_x, int center_y, unsigned size)
{
    if (frame < DRAGON_FRAMES_PER_SHEET) {
        (void)draw_art_frame_scaled(
            surface, state, BYTE_BUDDY_ENVIRONMENT_CHROME_SHEET,
            frame, center_x, center_y, size);
    }
}

static void draw_reaction_fx_frame(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    unsigned frame, int center_x, int center_y, unsigned size)
{
    if (frame < DRAGON_FRAMES_PER_SHEET) {
        (void)draw_art_frame_scaled(
            surface, state, BYTE_BUDDY_REACTION_FX_SHEET,
            frame, center_x, center_y, size);
    }
}

static void draw_signal_attack_frame(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    byte_buddy_signal_attack_t attack, unsigned phase,
    int center_x, int center_y, unsigned size)
{
    if (attack < BYTE_BUDDY_SIGNAL_ATTACK_COUNT && phase < 4U) {
        (void)draw_art_frame_scaled(
            surface, state, BYTE_BUDDY_SIGNAL_ATTACK_SHEET,
            (unsigned)attack * 4U + phase,
            center_x, center_y, size);
    }
}

static uint8_t fx_loop_phase(uint32_t animation_ms, uint32_t interval_ms)
{
    return interval_ms == 0U ? 0U :
        (uint8_t)((animation_ms / interval_ms) % 4U);
}

static uint8_t fx_timeline_phase(uint32_t remaining_ms,
                                 uint32_t total_ms)
{
    if (total_ms == 0U || remaining_ms >= total_ms) {
        return 0U;
    }
    const uint32_t phase =
        (total_ms - remaining_ms) * 4U / total_ms;
    return (uint8_t)(phase > 3U ? 3U : phase);
}

static uint8_t signal_passive_intro_phase(
    const byte_buddy_state_t *state)
{
    if (state->signal_phase_total_ms == 0U) {
        return 1U;
    }
    const uint16_t elapsed = (uint16_t)(
        state->signal_phase_total_ms - state->signal_phase_ms);
    return elapsed < state->signal_phase_total_ms * 2U / 3U ? 0U : 1U;
}

static uint8_t signal_passive_trigger_phase(
    const byte_buddy_state_t *state)
{
    if (state->signal_passive_fx_ms == 0U) {
        return 1U;
    }
    return state->signal_passive_fx_ms > SIGNAL_PASSIVE_FX_MS * 2U / 5U
        ? 2U : 3U;
}

static void draw_counter_fx_frame(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    byte_buddy_dragon_ability_t ability, uint8_t phase,
    int center_x, int center_y, unsigned size)
{
    (void)draw_art_frame_scaled(
        surface, state, BYTE_BUDDY_SIGNAL_COUNTER_FX_SHEET,
        byte_buddy_counter_fx_frame(ability, phase),
        center_x, center_y, size);
}

static void draw_outcome_fx_frame(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    byte_buddy_combat_outcome_t outcome, uint8_t phase,
    int center_x, int center_y, unsigned size)
{
    (void)draw_art_frame_scaled(
        surface, state, BYTE_BUDDY_SIGNAL_OUTCOME_FX_SHEET,
        byte_buddy_outcome_fx_frame(outcome, phase),
        center_x, center_y, size);
}

static void draw_passive_fx_frame(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    byte_buddy_signal_passive_t passive, uint8_t phase,
    int center_x, int center_y, unsigned size)
{
    (void)draw_art_frame_scaled(
        surface, state, BYTE_BUDDY_SIGNAL_PASSIVE_FX_SHEET,
        byte_buddy_passive_fx_frame(passive, phase),
        center_x, center_y, size);
}

static void draw_scan_fx_frame(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    byte_buddy_scan_fx_t scan, uint8_t phase,
    int center_x, int center_y, unsigned size)
{
    (void)draw_art_frame_scaled(
        surface, state, BYTE_BUDDY_SIGNAL_SCAN_FX_SHEET,
        byte_buddy_scan_fx_frame(scan, phase),
        center_x, center_y, size);
}

static void draw_evolution_fx_frame(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    byte_buddy_evolution_fx_t evolution, uint8_t phase,
    int center_x, int center_y, unsigned size)
{
    (void)draw_art_frame_scaled(
        surface, state, BYTE_BUDDY_EVOLUTION_FX_SHEET,
        byte_buddy_evolution_fx_frame(evolution, phase),
        center_x, center_y, size);
}

static void draw_need_fx_frame(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    byte_buddy_need_fx_t need, uint8_t phase,
    int center_x, int center_y, unsigned size)
{
    (void)draw_art_frame_scaled(
        surface, state, BYTE_BUDDY_NEED_FX_SHEET,
        byte_buddy_need_fx_frame(need, phase),
        center_x, center_y, size);
}

static void draw_activity_fx_frame(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    byte_buddy_activity_fx_t activity, uint8_t phase,
    int center_x, int center_y, unsigned size)
{
    (void)draw_art_frame_scaled(
        surface, state, BYTE_BUDDY_ACTIVITY_FX_SHEET,
        byte_buddy_activity_fx_frame(activity, phase),
        center_x, center_y, size);
}

static unsigned smooth_signal_pulse(uint32_t animation_ms,
                                    unsigned base, unsigned range,
                                    uint32_t period_ms)
{
    if (period_ms < 2U || range == 0U) {
        return base;
    }
    const uint32_t half = period_ms / 2U;
    const uint32_t phase = animation_ms % period_ms;
    const uint32_t rising = phase <= half
        ? phase : period_ms - phase;
    const uint16_t progress = (uint16_t)(
        rising * UINT16_MAX / half);
    return base + (unsigned)(
        (uint32_t)p4_ease_smoothstep_u16(progress) * range /
        UINT16_MAX);
}

static unsigned lineage_badge_frame(
    byte_buddy_signal_lineage_t lineage)
{
    if (lineage.resonance > BYTE_BUDDY_RESONANCE_NONE &&
        lineage.resonance < BYTE_BUDDY_RESONANCE_COUNT) {
        return 4U + lineage.resonance;
    }
    if (lineage.tier >= BYTE_BUDDY_LINEAGE_SPARK &&
        lineage.tier < BYTE_BUDDY_LINEAGE_TIER_COUNT) {
        return (unsigned)lineage.tier - 1U;
    }
    return 15U;
}

static void draw_lineage_badge(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    unsigned frame, int center_x, int center_y, unsigned size)
{
    if (frame < DRAGON_FRAMES_PER_SHEET) {
        (void)draw_art_frame_scaled(
            surface, state, BYTE_BUDDY_LINEAGE_BADGE_SHEET,
            frame, center_x, center_y, size);
    }
}

static void draw_dragon_sprite(p4_game_surface_t *surface,
                               const byte_buddy_state_t *state,
                               int left, int top)
{
    unsigned sheet = 0U;
    unsigned frame = 0U;
    dragon_sheet_frame(
        state, state->animation_ms, state->reaction,
        state->reaction_ms, &sheet, &frame);
    (void)draw_customized_frame_scaled(
        surface, state, sheet, frame,
        left + DRAGON_FRAME_WIDTH / 2,
        top + DRAGON_FRAME_HEIGHT / 2,
        DRAGON_FRAME_WIDTH);
}

static void draw_element_particles(p4_game_surface_t *surface,
                                   const byte_buddy_state_t *state,
                                   int left, int top)
{
    const unsigned stage = visual_dragon_stage(state);
    if (stage != BYTE_BUDDY_STAGE_EGG &&
        stage != BYTE_BUDDY_STAGE_ELEMENTAL) {
        return;
    }
    const int drift = eased_ping_pong_offset(
        state->animation_ms, 40U, 0, 5);
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
        visual_dragon_stage(state) == BYTE_BUDDY_STAGE_EGG) {
        return;
    }
    const int drift = eased_ping_pong_offset(
        state->animation_ms, 40U, 0, 7);
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
    const int drift = eased_ping_pong_offset(
        state->animation_ms, 40U, -1, 1);
    (void)draw_signal_layer_scaled(
        surface, state, 12U + lineage.aura, lineage.primary_hue,
        center_x, center_y + drift, DRAGON_FRAME_WIDTH);
    if ((lineage.adaptations &
         BYTE_BUDDY_ADAPTATION_PRISMATIC) != 0U) {
        (void)draw_signal_layer_scaled(
            surface, state, 4U + lineage.halo,
            lineage.secondary_hue, center_x, center_y + drift,
            DRAGON_FRAME_WIDTH - 12U);
    } else {
        (void)draw_signal_layer_scaled(
            surface, state, 4U + lineage.halo, lineage.primary_hue,
            center_x, center_y - drift, DRAGON_FRAME_WIDTH - 6U);
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
    const uint16_t color = signal_color_for_hue(lineage.primary_hue);
    const uint16_t secondary = signal_color_for_hue(
        lineage.secondary_hue);
    unsigned spark_count = 1U + lineage.tier + lineage.resonance;
    if (spark_count > 5U) {
        spark_count = 5U;
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
        p4_draw_fill_circle(surface, sparkle_x, sparkle_y, 1,
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
    unsigned regalia_frame = DRAGON_FRAMES_PER_SHEET;
    if ((lineage.adaptations & BYTE_BUDDY_ADAPTATION_CHIMERA) != 0U) {
        regalia_frame = 12U;
    } else if ((lineage.adaptations &
                BYTE_BUDDY_ADAPTATION_PRISMATIC) != 0U) {
        regalia_frame = 11U;
    } else if ((lineage.adaptations &
                BYTE_BUDDY_ADAPTATION_WIDEBAND) != 0U) {
        regalia_frame = 10U;
    } else if (lineage.phantom) {
        regalia_frame = 9U;
    } else if (lineage.shielded) {
        regalia_frame = 8U;
    } else if (lineage.tier >= BYTE_BUDDY_LINEAGE_MYTHIC) {
        regalia_frame = lineage_badge_frame(lineage);
    }
    if (regalia_frame < DRAGON_FRAMES_PER_SHEET) {
        const int crown_x = left + DRAGON_FRAME_WIDTH / 2;
        const unsigned badge_size = lineage.resonance >=
                BYTE_BUDDY_RESONANCE_GALAXY
            ? 22U : 18U;
        draw_lineage_badge(
            surface, state, regalia_frame,
            crown_x, top - 7, badge_size);
    }
}

static void draw_reaction_effect(p4_game_surface_t *surface,
                                 const byte_buddy_state_t *state,
                                 int left, int top)
{
    const buddy_reaction_t reaction = visual_dragon_reaction(
        state, (buddy_reaction_t)state->reaction);
    const uint32_t reaction_ms = visual_reaction_remaining_ms(
        state, state->reaction_ms);
    if (reaction_ms == 0U) {
        return;
    }
    byte_buddy_animation_cell_t effect_cell;
    if (!byte_buddy_reaction_effect_frame(
            reaction, reaction_ms, &effect_cell)) {
        return;
    }
    int center_x = left + DRAGON_FRAME_WIDTH - 8;
    int center_y = top + 16;
    unsigned size = 34U;
    switch (reaction) {
    case REACTION_FEED:
        center_y = top + 40;
        break;
    case REACTION_PLAY:
    case REACTION_PET:
        center_x = left + DRAGON_FRAME_WIDTH - 5;
        center_y = top + 9;
        break;
    case REACTION_CLEAN:
        center_x = left + DRAGON_FRAME_WIDTH - 6;
        center_y = top + 17;
        break;
    case REACTION_REST:
        center_x = left + DRAGON_FRAME_WIDTH - 7;
        center_y = top + 7;
        break;
    case REACTION_GROW:
    case REACTION_SIGNAL:
        center_x = left + DRAGON_FRAME_WIDTH / 2;
        center_y = top + DRAGON_FRAME_HEIGHT / 2;
        size = 50U;
        break;
    case REACTION_IDLE:
    case REACTION_HATCH:
    default:
        return;
    }
    (void)draw_art_frame_scaled(
        surface, state, effect_cell.sheet, effect_cell.frame,
        center_x, center_y, size);
}

static void draw_dragon(p4_game_surface_t *surface,
                        const byte_buddy_state_t *state,
                        int center_x, int top)
{
    const unsigned stage = visual_dragon_stage(state);
    const unsigned motion_phase = (unsigned)(
        (state->animation_ms / 40U) % 32U);
    int hover = (int)(s_eased_motion[motion_phase] / 2U);
    if (stage >= BYTE_BUDDY_STAGE_FLYING) {
        hover = (int)s_eased_motion[motion_phase];
        center_x += motion_phase < 16U
            ? (int)(motion_phase / 5U)
            : (int)((31U - motion_phase) / 5U);
    }
    const int left = center_x - DRAGON_FRAME_WIDTH / 2;
    top -= hover;
    const int shadow_width = stage >= BYTE_BUDDY_STAGE_FLYING
        ? DRAGON_FRAME_WIDTH * 3 / 8 - hover / 2
        : DRAGON_FRAME_WIDTH / 2 + 2 - hover;
    p4_draw_fill_rect(surface, center_x - shadow_width / 2,
                      top + DRAGON_FRAME_HEIGHT + hover - 2,
                      shadow_width, 2, UINT16_C(0x18c3));
    p4_draw_fill_rect(surface, center_x - shadow_width / 3,
                      top + DRAGON_FRAME_HEIGHT + hover,
                      shadow_width * 2 / 3, 1, UINT16_C(0x1082));
    draw_element_particles(surface, state, left, top);
    draw_lineage_back_layers(surface, state, left, top);
    draw_custom_trail(surface, state, left, top);
    draw_dragon_sprite(surface, state, left, top);
    if (state->evolution_fx_ms != 0U &&
        state->evolution_active_stage >= BYTE_BUDDY_STAGE_WINGED &&
        state->evolution_active_stage <= BYTE_BUDDY_STAGE_ELEMENTAL) {
        const byte_buddy_evolution_fx_t evolution =
            (byte_buddy_evolution_fx_t)(
                state->evolution_active_stage -
                BYTE_BUDDY_STAGE_WINGED);
        draw_evolution_fx_frame(
            surface, state, evolution,
            fx_timeline_phase(state->evolution_fx_ms, EVOLUTION_FX_MS),
            center_x, top + DRAGON_FRAME_HEIGHT / 2, 76U);
    }
    draw_signal_mutation(surface, state, left, top);
    if (stage >= BYTE_BUDDY_STAGE_WINGED) {
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
    if (stage == BYTE_BUDDY_STAGE_EGG &&
        state->care_actions >= GROW_BABY_INTERACTIONS / 2U) {
        p4_draw_fill_rect(surface, center_x, top + 20, 2, 6,
                          UINT16_C(0xffff));
        p4_draw_fill_rect(surface, center_x - 3, top + 25, 4, 2,
                          UINT16_C(0xffff));
    }
}

static void draw_lowest_need(p4_game_surface_t *surface,
                             const byte_buddy_state_t *state,
                             int center_x, int center_y)
{
    uint8_t lowest = state->hunger;
    byte_buddy_need_fx_t need = BYTE_BUDDY_NEED_FX_HUNGER;
    if (state->joy < lowest) {
        lowest = state->joy;
        need = BYTE_BUDDY_NEED_FX_JOY;
    }
    if (state->hygiene < lowest) {
        lowest = state->hygiene;
        need = BYTE_BUDDY_NEED_FX_HYGIENE;
    }
    if (state->energy < lowest) {
        lowest = state->energy;
        need = BYTE_BUDDY_NEED_FX_ENERGY;
    }
    if (lowest >= 55U || state->reaction_ms != 0U) {
        return;
    }
    draw_need_fx_frame(
        surface, state, need,
        fx_loop_phase(state->animation_ms, 190U),
        center_x, center_y, 34U);
}

static void draw_growth_panel(p4_game_surface_t *surface,
                              const byte_buddy_state_t *state)
{
    const unsigned stage = visual_dragon_stage(state);
    const byte_buddy_signal_lineage_t lineage = current_lineage(state);
    p4_draw_fill_rect(surface, 198, 29, 116, 106, UINT16_C(0x080f));
    p4_draw_rect(surface, 198, 29, 116, 106, UINT16_C(0x39e7));
    p4_draw_fill_rect(surface, 200, 31, 112, 1, signal_color(state));
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
        draw_lineage_badge(
            surface, state, lineage_badge_frame(lineage), 296, 116, 18U);
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
    if (width > 4 && height > 4) {
        p4_draw_fill_rect(surface, x + 2, y + 2, width - 4, 1,
                          active ? UINT16_C(0xffff) : UINT16_C(0x39e7));
        p4_draw_fill_rect(surface, x + 2, y + height - 2,
                          width - 4, 1, UINT16_C(0x000b));
    }
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
    (void)draw_art_frame_scaled(
        surface, state, STAR_CATCHER_REWARD_SHEET, 0U,
        x + 10, y + height / 2, 14U);
    p4_draw_text(surface, x + 20, y + 6,
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

static void draw_shop_backdrop(p4_game_surface_t *surface,
                               const byte_buddy_state_t *state)
{
    p4_draw_fill_rect(surface, 0, 25, P4_GAME_SURFACE_WIDTH, 175,
                      UINT16_C(0x080f));
    p4_draw_fill_rect(surface, 0, 98, P4_GAME_SURFACE_WIDTH, 102,
                      UINT16_C(0x181f));
    draw_signal_city_icon(surface, state, 7U, 160, 108, 104U);
    draw_signal_city_icon(surface, state, 0U, 296, 71, 44U);
    draw_signal_city_icon(surface, state, 2U, 23, 149, 38U);
    draw_signal_city_icon(surface, state, 3U, 298, 145, 42U);
    draw_environment_chrome_icon(surface, state, 10U, 286, 64, 44U);
}

static void draw_shop_dragon_preview(p4_game_surface_t *surface,
                                     const byte_buddy_state_t *state)
{
    unsigned sheet = 0U;
    unsigned frame = 0U;
    dragon_sheet_frame(
        state, state->animation_ms, REACTION_IDLE,
        0U, &sheet, &frame);
    (void)draw_customized_frame_scaled(
        surface, state, sheet, frame, 56, 13, 24U);
    if (state->shop_feedback_ms != 0U) {
        uint8_t feedback = state->shop_feedback_kind;
        if (feedback == SHOP_FEEDBACK_POWER_GROWTH) {
            draw_reaction_fx_frame(
                surface, state,
                12U + fx_timeline_phase(
                    state->shop_feedback_ms, SHOP_FEEDBACK_MS),
                56, 13, 32U);
            return;
        }
        if (feedback == SHOP_FEEDBACK_UNLOCKED &&
            state->shop_feedback_ms > SHOP_FEEDBACK_MS / 2U) {
            feedback = SHOP_FEEDBACK_ONSET;
        }
        draw_activity_fx_frame(
            surface, state, BYTE_BUDDY_ACTIVITY_FX_SHOP,
            feedback,
            56, 13, 32U);
    }
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
        surface, state, 8U + (unsigned)style, x + 96, y + 16, 26U);
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
    draw_shop_backdrop(surface, state);
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
    draw_shop_dragon_preview(surface, state);
}

static void draw_style_shop(p4_game_surface_t *surface,
                            const byte_buddy_state_t *state)
{
    draw_shop_backdrop(surface, state);
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
    draw_shop_dragon_preview(surface, state);
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
    draw_signal_city_icon(surface, state, 7U, 278, 91, 74U);
    draw_signal_city_icon(surface, state, 0U, 57, 93, 50U);
    draw_signal_city_icon(surface, state, 3U, 235, 111, 38U);
    const int lane_drift = (int)((state->animation_ms / 18U) % 320U);
    for (int streak = 0; streak < 5; ++streak) {
        const int x = (lane_drift + streak * 71) % 320;
        const uint16_t streak_color = streak % 2 == 0
            ? trail_color(state) : UINT16_C(0x7bef);
        p4_draw_fill_rect(surface, x, 48 + streak * 14, 9, 1,
                          streak_color);
        if (x > (int)P4_GAME_SURFACE_WIDTH - 9) {
            p4_draw_fill_rect(surface, x - (int)P4_GAME_SURFACE_WIDTH,
                              48 + streak * 14, 9, 1, streak_color);
        }
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
    const uint16_t spawn_progress = state->star_spawn_ms == 0U
        ? UINT16_MAX : (uint16_t)(
            (MINI_GAME_STAR_SPAWN_MS - state->star_spawn_ms) *
            UINT16_MAX / MINI_GAME_STAR_SPAWN_MS);
    const unsigned star_size = 8U + (unsigned)(
        (uint32_t)p4_ease_smoothstep_u16(spawn_progress) * 14U /
        UINT16_MAX);
    (void)draw_art_frame_scaled(
        surface, state, STAR_CATCHER_REWARD_SHEET,
        (unsigned)state->star_kind * 4U + reward_frame,
        state->star_x, star_y, star_size);
    if (state->star_effect_ms != 0U) {
        const uint8_t effect_phase = fx_timeline_phase(
            state->star_effect_ms, MINI_GAME_EFFECT_DURATION_MS);
        if (state->star_effect_kind == STAR_EFFECT_MISSED) {
            draw_activity_fx_frame(
                surface, state, BYTE_BUDDY_ACTIVITY_FX_STAR_MISS,
                effect_phase,
                state->star_effect_x, state->star_effect_y, 34U);
        } else {
            (void)draw_art_frame_scaled(
                surface, state, STAR_CATCHER_REWARD_SHEET,
                12U + effect_phase,
                state->star_effect_x, state->star_effect_y, 30U);
        }
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
    if (state->mini_intro_ms != 0U) {
        const uint8_t phase = fx_timeline_phase(
            state->mini_intro_ms, MINI_GAME_READY_VISIBLE_MS);
        p4_draw_fill_rect(surface, 92, 48, 136, 76, UINT16_C(0x000b));
        p4_draw_rect(surface, 92, 48, 136, 76, UINT16_C(0xffe0));
        draw_activity_fx_frame(
            surface, state, BYTE_BUDDY_ACTIVITY_FX_STAR_READY,
            phase, 160, 80, 62U);
        p4_draw_text(surface, 127, 109, "GET READY",
                     UINT16_C(0xffff), 1U, 9U);
    } else if (state->mini_summary_ms != 0U) {
        const uint8_t phase = fx_timeline_phase(
            state->mini_summary_ms, MINI_GAME_SUMMARY_MS);
        p4_draw_fill_rect(surface, 82, 44, 156, 86, UINT16_C(0x000b));
        p4_draw_rect(surface, 82, 44, 156, 86, UINT16_C(0xffe0));
        draw_activity_fx_frame(
            surface, state, BYTE_BUDDY_ACTIVITY_FX_STAR_SUMMARY,
            phase, 121, 81, 58U);
        p4_draw_text(surface, 153, 58, "STAR RUN",
                     UINT16_C(0xffff), 1U, 8U);
        p4_draw_text(surface, 153, 76, "CAUGHT",
                     UINT16_C(0x7bef), 1U, 6U);
        draw_number(surface, 201, 76, state->play_catches,
                    UINT16_C(0xffe0));
        p4_draw_text(surface, 153, 94, "BEST",
                     UINT16_C(0x7bef), 1U, 4U);
        draw_number(surface, 190, 94, state->play_best_streak,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 153, 112, "COINS",
                     UINT16_C(0x7bef), 1U, 5U);
        p4_draw_text(surface, 190, 112, "+",
                     UINT16_C(0xffe0), 1U, 1U);
        draw_number(surface, 199, 112,
                    byte_buddy_star_run_reward(state->play_catches),
                    UINT16_C(0xffe0));
    }
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

typedef struct {
    uint8_t core;
    uint8_t halo;
    uint8_t sigil;
    uint8_t aura;
    uint8_t core_hue;
    uint8_t halo_hue;
    uint8_t sigil_hue;
    uint8_t aura_hue;
    uint8_t rarity_marks;
    uint8_t habitat_nodes;
    uint8_t flags;
} signal_seed_visual_t;

static void draw_signal_habitat_accents(
    p4_game_surface_t *surface, uint8_t habitat_nodes, uint8_t flags,
    int x, int y, unsigned size, uint8_t hue, uint32_t animation_ms)
{
    static const int8_t directions[16][2] = {
        {0, -8}, {3, -7}, {6, -6}, {7, -3},
        {8, 0}, {7, 3}, {6, 6}, {3, 7},
        {0, 8}, {-3, 7}, {-6, 6}, {-7, 3},
        {-8, 0}, {-7, -3}, {-6, -6}, {-3, -7},
    };
    const unsigned nodes = habitat_nodes < 5U ? habitat_nodes : 4U;
    const unsigned rotation = (animation_ms / 50U) & 15U;
    const int radius = size < 32U
        ? (int)size / 2 - 1 : (int)size / 2 + 3;
    for (unsigned node = 0U; node < nodes; ++node) {
        const unsigned direction = (
            rotation + node * 16U / nodes) & 15U;
        const int node_x = x + directions[direction][0] * radius / 8;
        const int node_y = y + directions[direction][1] * radius / 8;
        p4_draw_fill_circle(
            surface, node_x, node_y, size >= 40U ? 2 : 1,
            signal_color_for_hue((uint8_t)((hue + node * 2U) & 7U)));
    }
    if ((flags & P4_GAME_SIGNAL_PROTECTED) != 0U) {
        const int extent = (int)size / 2;
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
        const int drift = eased_ping_pong_offset(
            animation_ms, 40U, 0, 3);
        const uint16_t ghost = signal_color_for_hue(
            (uint8_t)((hue + 4U) & 7U));
        p4_draw_fill_circle(surface, x - (int)size / 3,
                            y + (int)size / 3 - drift, 2, ghost);
        p4_draw_fill_circle(surface, x + (int)size / 3,
                            y - (int)size / 3 + drift, 1,
                            UINT16_C(0xffff));
    }
}

static void draw_signal_rarity_marks(
    p4_game_surface_t *surface, uint8_t rarity_marks,
    int x, int y, unsigned size, uint8_t hue, uint32_t animation_ms)
{
    static const int8_t corners[4][2] = {
        {-1, -1}, {1, -1}, {1, 1}, {-1, 1},
    };
    const unsigned marks = rarity_marks < 5U ? rarity_marks : 4U;
    const unsigned bright = marks == 0U
        ? 0U : (animation_ms / 180U) % marks;
    const int extent = size < 32U ? 7 : (int)size / 2 - 4;
    for (unsigned mark = 0U; mark < marks; ++mark) {
        const int mark_x = x + corners[mark][0] * extent;
        const int mark_y = y + corners[mark][1] * extent;
        const uint16_t color = mark == bright ? UINT16_C(0xffff) :
            signal_color_for_hue((uint8_t)((hue + mark * 2U) & 7U));
        if (size >= 32U) {
            p4_draw_fill_rect(surface, mark_x - 2, mark_y, 5, 1, color);
            p4_draw_fill_rect(surface, mark_x, mark_y - 2, 1, 5, color);
        } else {
            p4_draw_pixel(surface, mark_x, mark_y, color);
        }
    }
}

static signal_seed_visual_t signal_visual_for_token(
    uint64_t token, uint8_t channel, uint8_t flags)
{
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(token);
    const uint8_t family = byte_buddy_signal_channel_family(channel);
    return (signal_seed_visual_t){
        .core = genome.core,
        .halo = genome.halo,
        .sigil = genome.sigil,
        .aura = genome.aura,
        .core_hue = genome.hue,
        .halo_hue = genome.hue,
        .sigil_hue = genome.hue,
        .aura_hue = genome.hue,
        .rarity_marks = (uint8_t)(genome.rarity + 1U),
        .habitat_nodes = family == UINT8_MAX
            ? 0U : (uint8_t)(family + 1U),
        .flags = flags,
    };
}

static signal_seed_visual_t signal_visual_for_lineage(
    byte_buddy_signal_lineage_t lineage)
{
    const bool prismatic = (lineage.adaptations &
        BYTE_BUDDY_ADAPTATION_PRISMATIC) != 0U;
    return (signal_seed_visual_t){
        .core = (uint8_t)(lineage.family & 3U),
        .halo = (uint8_t)(lineage.halo & 3U),
        .sigil = (uint8_t)(lineage.marking & 3U),
        .aura = (uint8_t)(lineage.aura & 3U),
        .core_hue = (uint8_t)(lineage.primary_hue & 7U),
        .halo_hue = (uint8_t)(
            (prismatic ? lineage.secondary_hue : lineage.primary_hue) & 7U),
        .sigil_hue = (uint8_t)(lineage.secondary_hue & 7U),
        .aura_hue = (uint8_t)(lineage.secondary_hue & 7U),
        .rarity_marks = lineage.rarity_diversity,
        .habitat_nodes = lineage.channel_families,
        .flags = (uint8_t)(
            (lineage.shielded ? P4_GAME_SIGNAL_PROTECTED : 0U) |
            (lineage.phantom ? P4_GAME_SIGNAL_HIDDEN : 0U)),
    };
}

static void draw_signal_seed_visual(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    signal_seed_visual_t visual, int x, int y, unsigned size,
    uint32_t animation_ms)
{
    const int aura_y = y + eased_ping_pong_offset(
        animation_ms, 40U, -1, 1);
    (void)draw_signal_layer_scaled(
        surface, state, 12U + (visual.aura & 3U),
        (uint8_t)(visual.aura_hue & 7U), x, aura_y, size);
    (void)draw_signal_layer_scaled(
        surface, state, 4U + (visual.halo & 3U),
        (uint8_t)(visual.halo_hue & 7U), x, y, size);
    (void)draw_signal_layer_scaled(
        surface, state, visual.core & 3U,
        (uint8_t)(visual.core_hue & 7U), x, y, size);
    (void)draw_signal_layer_scaled(
        surface, state, 8U + (visual.sigil & 3U),
        (uint8_t)(visual.sigil_hue & 7U), x, y, size);
    draw_signal_habitat_accents(
        surface, visual.habitat_nodes, visual.flags,
        x, y, size, visual.core_hue, animation_ms);
    draw_signal_rarity_marks(
        surface, visual.rarity_marks, x, y, size,
        visual.core_hue, animation_ms);
}

static void draw_signal_seed(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    uint64_t token, uint8_t channel, uint8_t flags,
    int x, int y, unsigned size, uint32_t animation_ms)
{
    draw_signal_seed_visual(
        surface, state, signal_visual_for_token(token, channel, flags),
        x, y, size, animation_ms);
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
    const int drift = (int)((state->animation_ms / 240U) % 320U);
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
    draw_environment_chrome_icon(surface, state, 1U, 238, 74, 94U);
    draw_environment_chrome_icon(surface, state, 2U, 291, 47, 42U);
    draw_environment_chrome_icon(surface, state, 0U, 48, 106, 56U);
    draw_environment_chrome_icon(
        surface, state, 4U + (state->signal_hue & 3U),
        101, 49 + eased_ping_pong_offset(
            state->animation_ms, 55U, -1, 1), 42U);
    draw_signal_city_icon(surface, state, 1U, 75, 68, 50U);
    draw_signal_city_icon(surface, state, 2U, 22, 119, 38U);
    draw_signal_city_icon(surface, state, 3U, 302, 120, 40U);
    draw_signal_city_icon(surface, state, 5U, 281, 126, 36U);
    draw_signal_city_icon(surface, state, 6U, 123, 125, 40U);
    const unsigned nest_size = 56U +
        (unsigned)state->upgrades[BYTE_BUDDY_UPGRADE_NEST] * 4U;
    draw_environment_chrome_icon(
        surface, state, 3U, 160, 124, nest_size);
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
    p4_draw_text(surface, 160, 8,
                 state->signal_training_mode ? "DRILL" : "LINK",
                 UINT16_C(0x7bef), 1U,
                 state->signal_training_mode ? 5U : 4U);
    draw_number(surface, state->signal_training_mode ? 198 : 190, 8,
                state->signal_training_mode
                    ? state->signal_training_completed_count
                    : state->signal_consumed_count,
                signal_color(state));
    const byte_buddy_battle_stats_t stats = current_battle_stats(state);
    p4_draw_text(surface, 215, 8, "LV", UINT16_C(0x7bef), 1U, 2U);
    draw_number(surface, 232, 8, stats.level, UINT16_C(0xffff));
    p4_draw_text(surface, 263, 8, "SP", UINT16_C(0xffe0), 1U, 2U);
    draw_number(surface, 281, 8, state->signal_session_coins,
                UINT16_C(0xffff));
}

static void draw_signal_list(p4_game_surface_t *surface,
                             const byte_buddy_state_t *state)
{
    draw_signal_header(surface, state, "SIGNAL HUNT", 11U);
    p4_draw_fill_rect(surface, 0, 25, 320, 175, UINT16_C(0x080f));
    if (state->signal_request_busy_ms != 0U &&
        state->signal_snapshot.status != P4_GAME_SIGNAL_READY) {
        draw_scan_fx_frame(
            surface, state, BYTE_BUDDY_SCAN_FX_BUSY,
            fx_loop_phase(state->animation_ms, 180U),
            160, 65, 58U);
        p4_draw_text(surface, 78, 96, "SCANNER BUSY - TRY AGAIN",
                     UINT16_C(0xffe0), 1U, 24U);
        p4_draw_text(surface, 72, 112, "PRESS SCAN AFTER COOLDOWN",
                     UINT16_C(0x9cf3), 1U,
                     sizeof("PRESS SCAN AFTER COOLDOWN") - 1U);
    } else if (state->signal_snapshot.status == P4_GAME_SIGNAL_SCANNING) {
        draw_scan_fx_frame(
            surface, state, BYTE_BUDDY_SCAN_FX_SCANNING,
            fx_loop_phase(state->animation_ms, 180U),
            160, 68, 62U);
        p4_draw_text(surface, 108, 109, "SCANNING CITY",
                     UINT16_C(0x07ff), 1U, 13U);
    } else if (state->signal_snapshot.status == P4_GAME_SIGNAL_IDLE) {
        draw_scan_fx_frame(
            surface, state, BYTE_BUDDY_SCAN_FX_EMPTY,
            fx_loop_phase(state->animation_ms, 190U),
            160, 68, 58U);
        p4_draw_text(surface, 86, 108, "PRESS SCAN TO RETRY",
                     UINT16_C(0x07ff), 1U,
                     sizeof("PRESS SCAN TO RETRY") - 1U);
    } else if (state->signal_snapshot.status == P4_GAME_SIGNAL_ERROR) {
        draw_scan_fx_frame(
            surface, state, BYTE_BUDDY_SCAN_FX_OFFLINE,
            fx_loop_phase(state->animation_ms, 190U),
            160, 63, 58U);
        p4_draw_fill_rect(surface, 70, 90, 180, 42, UINT16_C(0x000b));
        p4_draw_rect(surface, 70, 90, 180, 42, UINT16_C(0xf81f));
        p4_draw_text(surface, 108, 99, "SCAN TIMED OUT",
                     UINT16_C(0xf81f), 1U, 14U);
        p4_draw_text(surface, 91, 115, "PRESS SCAN TO RETRY",
                     UINT16_C(0x9cf3), 1U,
                     sizeof("PRESS SCAN TO RETRY") - 1U);
    } else if (state->signal_snapshot.status != P4_GAME_SIGNAL_READY) {
        draw_signal_city_icon(surface, state, 10U, 160, 63, 50U);
        draw_scan_fx_frame(
            surface, state, BYTE_BUDDY_SCAN_FX_OFFLINE,
            fx_loop_phase(state->animation_ms, 190U),
            160, 63, 58U);
        p4_draw_text(surface, 91, 101, "SIGNAL RADIO OFFLINE",
                     UINT16_C(0xf81f), 1U, 20U);
        p4_draw_text(surface, 74, 117, "NO NETWORK DATA IS EXPOSED",
                     UINT16_C(0x7bef), 1U, 26U);
    } else {
        if (state->signal_snapshot.count == 0U) {
            draw_scan_fx_frame(
                surface, state, BYTE_BUDDY_SCAN_FX_EMPTY,
                fx_loop_phase(state->animation_ms, 190U),
                160, 68, 58U);
            p4_draw_text(surface, 101, 108, "NO SIGNALS FOUND",
                         UINT16_C(0x7bef), 1U, 16U);
        }
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
            const byte_buddy_signal_encounter_t encounter =
                byte_buddy_signal_encounter(
                    signal->token, signal->rssi_dbm,
                    signal->channel, signal->flags);
            const int top = 36 + (int)row * 25;
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
                p4_draw_text(surface, 270, top + 7,
                             state->signal_training_mode ? "DONE" : "EATEN",
                             UINT16_C(0x7bef), 1U,
                             state->signal_training_mode ? 4U : 5U);
            } else if (state->signal_training_mode) {
                p4_draw_text(surface, 270, top + 7, "DRILL",
                             UINT16_C(0xffe0), 1U, 5U);
            } else if (!byte_buddy_signal_collection_has_room(
                           state->signal_consumed_count)) {
                p4_draw_text(surface, 270, top + 7, "FULL",
                             UINT16_C(0xf81f), 1U, 4U);
            } else {
                p4_draw_text(surface, 270, top + 7, "S+",
                             UINT16_C(0xffe0), 1U, 2U);
                draw_number(surface, 284, top + 7,
                            byte_buddy_signal_reward_coins(
                                profile, encounter),
                            UINT16_C(0xffe0));
            }
        }
        if (state->signal_training_mode) {
            p4_draw_text(surface, 229, 27, "TRAINING",
                         UINT16_C(0xffe0), 1U, 8U);
        } else if (rows != 0U && signal_is_simulated(
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
        draw_evolution_fx_frame(
            surface, state, BYTE_BUDDY_EVOLUTION_FX_GENOME,
            fx_timeline_phase(
                state->signal_reward_ms, REACTION_DURATION_MS),
            94, 100, 46U);
        p4_draw_text(surface, 104, 74, "LINEAGE UPDATED",
                     UINT16_C(0xffff), 1U, 15U);
        draw_lineage_badge(
            surface, state, lineage_badge_frame(lineage), 94, 100, 32U);
        p4_draw_text(surface, 115, 91, badge,
                     signal_color(state), 1U, badge_length);
        p4_draw_text(surface, 189, 91, "DNA", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 213, 91,
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
                          ? "SCANNER BUSY" : state->signal_training_mode
                              ? "RESET DRILL" : pages == 1U ? "SCAN CITY" :
                          page == 0U ? "SCAN 1/2" : "SCAN 2/2",
                      state->signal_request_busy_ms != 0U
                          ? 12U : state->signal_training_mode
                              ? 11U : pages == 1U ? 9U : 8U,
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
        draw_signal_city_icon(surface, state, 11U, 160, 70, 52U);
        draw_scan_fx_frame(
            surface, state, BYTE_BUDDY_SCAN_FX_EMPTY,
            fx_loop_phase(state->animation_ms, 190U),
            160, 70, 60U);
        p4_draw_text(surface, 98, 109, "SIGNAL MOVED AWAY",
                     UINT16_C(0xf81f), 1U, 17U);
    } else {
        const byte_buddy_signal_profile_t profile =
            byte_buddy_signal_profile(signal->token, signal->rssi_dbm);
        const byte_buddy_signal_genome_t genome =
            byte_buddy_signal_genome(signal->token);
        const byte_buddy_signal_encounter_t encounter =
            byte_buddy_signal_encounter(
                signal->token, signal->rssi_dbm,
                signal->channel, signal->flags);
        const uint16_t color = signal_color_for_hue(profile.hue);
        if (state->signal_snapshot.status == P4_GAME_SIGNAL_SCANNING) {
            draw_scan_fx_frame(
                surface, state, BYTE_BUDDY_SCAN_FX_SCANNING,
                fx_loop_phase(state->animation_ms, 180U),
                83, 82, 70U);
        } else {
            draw_signal_city_icon(surface, state, 9U, 83, 82, 70U);
            draw_signal_city_icon(
                surface, state, 12U, 83, 82,
                smooth_signal_pulse(state->animation_ms, 62U, 6U, 960U));
        }
        draw_signal_seed(
            surface, state, signal->token,
            signal->channel, signal->flags,
            83, 82, 50U, state->animation_ms);
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
        p4_draw_text(surface, 121, 89,
                     state->signal_training_mode ? "REWARD" : "SPARKS",
                     UINT16_C(0x7bef), 1U, 6U);
        if (state->signal_training_mode) {
            p4_draw_text(surface, 166, 89, "PRACTICE",
                         UINT16_C(0xffe0), 1U, 8U);
        } else {
            draw_number(surface, 166, 89,
                        byte_buddy_signal_reward_coins(profile, encounter),
                        UINT16_C(0xffe0));
        }
        p4_draw_text(surface, 121, 105,
                     state->signal_snapshot.status == P4_GAME_SIGNAL_ERROR
                         ? "SCAN TIMED OUT" :
                     state->signal_training_mode ? "PRACTICE MODE" :
                     signal_is_simulated(signal) ? "SIMULATED RSSI" :
                     state->signal_samples < 2U ? "LIVE RSSI ACQUIRING" :
                     state->signal_trend_db >= 3 ? "CLOSER" :
                     state->signal_trend_db <= -3 ? "FARTHER" : "STEADY",
                     state->signal_snapshot.status == P4_GAME_SIGNAL_ERROR
                         ? UINT16_C(0xf81f) :
                     state->signal_training_mode ? UINT16_C(0xffe0) :
                     signal_is_simulated(signal) ? UINT16_C(0xf81f) :
                         UINT16_C(0x07e0),
                     1U,
                         state->signal_snapshot.status == P4_GAME_SIGNAL_ERROR
                             ? 14U :
                         state->signal_training_mode ? 13U :
                         signal_is_simulated(signal) ? 14U :
                         state->signal_samples < 2U ? 19U :
                         state->signal_trend_db >= 3 ? 6U :
                         state->signal_trend_db <= -3 ? 7U : 6U);
        draw_signal_meter(surface, 25, 126, 270, profile.strength, color);
        p4_draw_text(surface, state->controller_active ? 66 : 90, 138,
                     state->signal_training_mode
                         ? "OFFLINE TRAINING - B BACK"
                         : state->controller_active
                         ? "A BATTLE  START SCAN  B BACK"
                         : "AUTO REFRESH - WALK AROUND",
                     UINT16_C(0xbdf7), 1U,
                     state->signal_training_mode
                         ? sizeof("OFFLINE TRAINING - B BACK") - 1U
                         : state->controller_active
                         ? sizeof("A BATTLE  START SCAN  B BACK") - 1U
                         : sizeof("AUTO REFRESH - WALK AROUND") - 1U);
    }
    draw_touch_button(surface, 4, 162, 188, 33,
                      state->signal_request_busy_ms != 0U
                          ? "SCANNER BUSY" : state->signal_training_mode
                              ? "RESET DRILL" : "RESCAN NOW",
                      state->signal_request_busy_ms != 0U ? 12U :
                          state->signal_training_mode ? 11U : 10U,
                      state->signal_request_busy_ms != 0U
                          ? UINT16_C(0xffe0) : UINT16_C(0x07ff),
                      state->signal_snapshot.status == P4_GAME_SIGNAL_SCANNING);
    const bool full = !state->signal_training_mode && signal != NULL &&
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
    byte_buddy_signal_weave_rules_t rules,
    int signal_offset_x, int dragon_offset_x, int dragon_offset_y)
{
    const uint16_t color = signal_color_for_hue(profile.hue);
    draw_signal_seed(
        surface, state, signal->token,
        signal->channel, signal->flags,
        57 + signal_offset_x, 82, 44U, state->animation_ms);
    draw_dragon(surface, state, 266 + dragon_offset_x,
                58 + dragon_offset_y);

    const uint8_t completed_segments = state->signal_weave_step == 0U
        ? 0U : state->signal_weave_step <= 7U
            ? (uint8_t)(state->signal_weave_step - 1U) : 6U;
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
        draw_signal_city_icon(surface, state, 14U,
                              node.x, node.y, 20U);
    }
    const uint8_t completed_nodes = state->signal_weave_step < 6U
        ? state->signal_weave_step : 6U;
    for (uint8_t step = 0U; step < completed_nodes; ++step) {
        const byte_buddy_signal_weave_node_t node =
            byte_buddy_signal_weave_node(genome, step);
        (void)draw_signal_layer_scaled(
            surface, state, 8U + genome.sigil, genome.hue,
            node.x, node.y, 18U);
    }
    if (state->signal_weave_step < rules.required_locks) {
        const byte_buddy_signal_weave_node_t active =
            byte_buddy_signal_weave_node(genome, state->signal_weave_step);
        draw_signal_city_icon(
            surface, state, 14U, active.x, active.y,
            smooth_signal_pulse(state->animation_ms, 30U, 4U, 720U));
        (void)draw_signal_layer_scaled(
            surface, state, 4U + genome.halo, genome.hue,
            active.x, active.y, 28U);
        (void)draw_signal_layer_scaled(
            surface, state, 8U + genome.sigil, genome.hue,
            active.x, active.y, 28U);
    }
    if (state->signal_hit_ms != 0U && state->signal_weave_step != 0U) {
        const byte_buddy_signal_weave_node_t captured =
            byte_buddy_signal_weave_node(
                genome, (uint8_t)(state->signal_weave_step - 1U));
        draw_reaction_fx_frame(
            surface, state, 15U, captured.x, captured.y, 36U);
    }
    if (state->controller_active) {
        const int cursor_x = p4_q16_to_int_round(
            state->signal_cursor_x_q16);
        const int cursor_y = p4_q16_to_int_round(
            state->signal_cursor_y_q16);
        draw_signal_city_icon(
            surface, state, 14U, cursor_x, cursor_y, 16U);
        p4_draw_fill_rect(surface, cursor_x - 8, cursor_y,
                          17, 1, color);
        p4_draw_fill_rect(surface, cursor_x, cursor_y - 8,
                          1, 17, color);
    }
}

static uint16_t signal_phase_progress(const byte_buddy_state_t *state)
{
    if (state->signal_phase_total_ms == 0U ||
        state->signal_phase_ms >= state->signal_phase_total_ms) {
        return 0U;
    }
    return (uint16_t)(
        (uint32_t)(state->signal_phase_total_ms - state->signal_phase_ms) *
        UINT16_MAX / state->signal_phase_total_ms);
}

static void draw_signal_attack_motion(
    p4_game_surface_t *surface, const byte_buddy_state_t *state,
    byte_buddy_signal_encounter_t encounter,
    int signal_x, int dragon_x, int center_y)
{
    const uint16_t progress = signal_phase_progress(state);
    const byte_buddy_signal_attack_t attack =
        byte_buddy_signal_attack_for(
            encounter, state->signal_attack_index);
    switch ((signal_battle_phase_t)state->signal_battle_phase) {
    case SIGNAL_PHASE_WINDUP: {
        const unsigned frame = progress < UINT16_MAX / 2U ? 0U : 1U;
        const unsigned size = 38U + (unsigned)(
            (uint32_t)progress * 16U / UINT16_MAX);
        draw_signal_attack_frame(
            surface, state, attack,
            frame, signal_x, center_y, size);
        break;
    }
    case SIGNAL_PHASE_TRAVEL: {
        const uint16_t eased = p4_ease_smoothstep_u16(progress);
        const int x = signal_x + (int)(
            (int32_t)(dragon_x - signal_x) * (int32_t)eased /
                (int32_t)UINT16_MAX);
        draw_signal_attack_frame(
            surface, state, attack,
            2U, x, center_y, 52U);
        break;
    }
    case SIGNAL_PHASE_IMPACT:
        draw_signal_attack_frame(
            surface, state, attack,
            3U, dragon_x, center_y,
            smooth_signal_pulse(
                SIGNAL_ATTACK_IMPACT_MS - state->signal_phase_ms,
                54U, 12U, SIGNAL_ATTACK_IMPACT_MS * 2U));
        break;
    default:
        break;
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
    const byte_buddy_signal_encounter_t encounter =
        byte_buddy_signal_encounter(
            signal->token, signal->rssi_dbm,
            signal->channel, signal->flags);
    const byte_buddy_signal_attack_t current_attack =
        byte_buddy_signal_attack_for(
            encounter, state->signal_attack_index);
    const byte_buddy_dragon_ability_t ability =
        byte_buddy_dragon_ability(
            state->element < BYTE_BUDDY_ELEMENT_COUNT
                ? (byte_buddy_element_t)state->element
                : BYTE_BUDDY_ELEMENT_MYSTERY);
    const byte_buddy_signal_passive_t passive =
        encounter.passive < BYTE_BUDDY_SIGNAL_PASSIVE_COUNT
            ? (byte_buddy_signal_passive_t)encounter.passive
            : BYTE_BUDDY_SIGNAL_PASSIVE_WARD;
    const byte_buddy_battle_stats_t battle_stats =
        current_battle_stats(state);
    const byte_buddy_lineage_battle_traits_t battle_traits =
        byte_buddy_lineage_battle_traits(current_lineage(state));
    const byte_buddy_signal_weave_rules_t rules =
        byte_buddy_signal_weave_rules_for_magic(
            genome, profile.strength,
            state->upgrades[BYTE_BUDDY_UPGRADE_MAGNET],
            battle_stats.magic);
    const uint16_t color = signal_color_for_hue(profile.hue);
    const unsigned arena_frame =
        encounter.arena == BYTE_BUDDY_SIGNAL_ARENA_STEADY
            ? 7U : 11U + encounter.arena;
    draw_environment_chrome_icon(
        surface, state, arena_frame, 160, 83,
        weave ? 82U : 94U);
    int shake_x = 0;
    int shake_y = 0;
    if (state->signal_player_hit_ms != 0U) {
        const int amplitude = 1 + (int)(
            state->signal_player_hit_ms * 2U /
            SIGNAL_HIT_DURATION_MS);
        p4_camera_shake(
            state->animation_ms / 33U,
            (uint32_t)(signal->token ^ (signal->token >> 32U)),
            amplitude, &shake_x, &shake_y);
    }
    const int signal_recoil = state->signal_hit_ms == 0U ? 0 :
        -(int)(state->signal_hit_ms * 5U / SIGNAL_HIT_DURATION_MS);
    const int signal_x = (weave ? 57 : 79) + signal_recoil;
    const int dragon_x = (weave ? 266 : 246) + shake_x;
    if (state->signal_passive_fx_ms == 0U) {
        draw_passive_fx_frame(
            surface, state, passive, 1U,
            signal_x, weave ? 82 : 84, 62U);
    }
    if (weave) {
        draw_signal_weave_arena(
            surface, state, signal, profile, genome, rules,
            signal_recoil, shake_x, shake_y);
    } else {
        draw_signal_city_icon(
            surface, state, 12U, signal_x, 84,
            smooth_signal_pulse(state->animation_ms, 62U, 7U, 840U));
        if (state->signal_hit_ms != 0U) {
            const unsigned hit_frame = 12U + (unsigned)(
                (SIGNAL_HIT_DURATION_MS - state->signal_hit_ms) * 4U /
                SIGNAL_HIT_DURATION_MS);
            draw_reaction_fx_frame(
                surface, state, hit_frame > 15U ? 15U : hit_frame,
                signal_x, 84, 58U);
        }
        draw_signal_seed(
            surface, state, signal->token,
            signal->channel, signal->flags,
            signal_x, 84, 52U, state->animation_ms);
        draw_dragon(surface, state, dragon_x, 58 + shake_y);
    }
    if (state->signal_passive_fx_ms != 0U) {
        draw_passive_fx_frame(
            surface, state, passive,
            signal_passive_trigger_phase(state),
            signal_x, weave ? 82 : 84, 64U);
    }
    draw_signal_attack_motion(
        surface, state, encounter, signal_x, dragon_x, 84 + shake_y);
    if (state->signal_hit_ms != 0U) {
        const uint16_t strike_progress = (uint16_t)(
            (SIGNAL_HIT_DURATION_MS - state->signal_hit_ms) *
            UINT16_MAX / SIGNAL_HIT_DURATION_MS);
        const uint16_t eased = p4_ease_smoothstep_u16(strike_progress);
        const int strike_x = dragon_x + (int)(
            (int32_t)(signal_x - dragon_x) * (int32_t)eased /
                (int32_t)UINT16_MAX);
        draw_signal_city_icon(surface, state, 12U, strike_x, 84, 30U);
    }
    if (state->signal_guard_armed) {
        uint8_t counter_phase = 1U;
        if (state->signal_battle_phase == SIGNAL_PHASE_WINDUP) {
            counter_phase = signal_phase_progress(state) <
                    UINT16_MAX / 2U
                ? 0U : 1U;
        } else if (state->signal_battle_phase == SIGNAL_PHASE_TRAVEL) {
            counter_phase = 2U;
        }
        draw_signal_city_icon(
            surface, state, 13U, dragon_x, 86 + shake_y, 58U);
        draw_counter_fx_frame(
            surface, state, ability, counter_phase,
            dragon_x, 86 + shake_y, 70U);
    } else if (state->signal_guard_fx_ms != 0U) {
        draw_signal_city_icon(
            surface, state, 13U, dragon_x, 86 + shake_y, 58U);
        draw_counter_fx_frame(
            surface, state, ability, 3U,
            dragon_x, 86 + shake_y,
            smooth_signal_pulse(
                SIGNAL_GUARD_FX_MS - state->signal_guard_fx_ms,
                64U, 8U, SIGNAL_GUARD_FX_MS * 2U));
    }

    p4_draw_text(surface, 8, 29,
                 s_signal_attack_names[current_attack], color, 1U,
                 s_signal_attack_name_lengths[current_attack]);
    p4_draw_text(surface, 82, 29,
                 s_signal_passive_names[encounter.passive],
                 UINT16_C(0xfd20), 1U,
                 s_signal_passive_name_lengths[encounter.passive]);
    p4_draw_text(surface, 146, 29,
                 s_signal_arena_names[encounter.arena],
                 UINT16_C(0x9cf3), 1U,
                 s_signal_arena_name_lengths[encounter.arena]);
    p4_draw_text(surface, 193, 29, "WEAK", UINT16_C(0x7bef), 1U, 4U);
    p4_draw_text(surface, 222, 29,
                 s_element_names[encounter.weakness],
                 UINT16_C(0xffff), 1U,
                 encounter.weakness == BYTE_BUDDY_ELEMENT_MYSTERY
                    ? 7U : encounter.weakness == BYTE_BUDDY_ELEMENT_FIRE
                        ? 4U : encounter.weakness == BYTE_BUDDY_ELEMENT_ICE
                            ? 3U : 4U);
    p4_draw_text(surface, 272, 29, "T", UINT16_C(0xf81f), 1U, 1U);
    draw_number(surface, 282, 29, encounter.threat, UINT16_C(0xffff));
    if (!state->signal_training_mode) {
        p4_draw_text(surface, 294, 29, "S+", UINT16_C(0xffe0), 1U, 2U);
        draw_number(surface, 307, 29,
                    byte_buddy_signal_reward_coins(profile, encounter),
                    UINT16_C(0xffe0));
    }

    p4_draw_text(surface, 8, 126, "YOU", UINT16_C(0xbdf7), 1U, 3U);
    p4_draw_rect(surface, 31, 126, 83, 8, UINT16_C(0x7bef));
    const uint32_t shown_player = state->signal_shown_player_hp_q8;
    const uint32_t player_max_q8 =
        (uint32_t)state->signal_player_max_hp << 8U;
    p4_draw_fill_rect(surface, 32, 127,
                      player_max_q8 == 0U ? 0 :
                          (int)(shown_player * 81U / player_max_q8),
                      6, UINT16_C(0x07e0));
    p4_draw_text(surface, 121, 126, "FOE", UINT16_C(0xbdf7), 1U, 3U);
    p4_draw_rect(surface, 144, 126, 83, 8, UINT16_C(0x7bef));
    const uint32_t shown_enemy = state->signal_shown_enemy_hp_q8;
    const uint32_t enemy_max_q8 =
        (uint32_t)state->signal_battle_max_hp << 8U;
    p4_draw_fill_rect(surface, 145, 127,
                      enemy_max_q8 == 0U ? 0 :
                          (int)(shown_enemy * 81U / enemy_max_q8),
                      6, UINT16_C(0xf81f));
    if (state->signal_enemy_ward != 0U) {
        p4_draw_text(surface, 231, 126, "W", UINT16_C(0x07ff), 1U, 1U);
        draw_number(surface, 240, 126, state->signal_enemy_ward,
                    UINT16_C(0x07ff));
    }
    p4_draw_text(surface, 259, 126, "TIME", UINT16_C(0xbdf7), 1U, 4U);
    const uint32_t limit = signal_battle_duration_ms(state) +
        state->signal_battle_bonus_ms;
    const uint32_t remaining = state->signal_battle_elapsed_ms >= limit
        ? 0U : limit - state->signal_battle_elapsed_ms;
    p4_draw_rect(surface, 287, 126, 29, 8, UINT16_C(0x7bef));
    p4_draw_fill_rect(surface, 288, 127,
                      limit == 0U ? 0 : (int)(remaining * 27U / limit), 6,
                      UINT16_C(0xffe0));
    const bool guard_ready = state->signal_guard_charges != 0U &&
        state->signal_battle_outcome == BYTE_BUDDY_COMBAT_ACTIVE &&
        !state->signal_guard_armed &&
        (state->signal_battle_phase == SIGNAL_PHASE_WINDUP ||
         state->signal_battle_phase == SIGNAL_PHASE_TRAVEL) &&
        byte_buddy_signal_parry_ready(
            signal_time_to_impact_ms(state), battle_traits);
    if ((state->signal_battle_phase == SIGNAL_PHASE_WINDUP ||
         state->signal_battle_phase == SIGNAL_PHASE_TRAVEL) &&
        state->signal_battle_outcome == BYTE_BUDDY_COMBAT_ACTIVE &&
        !state->signal_guard_armed) {
        if (guard_ready) {
            p4_draw_text(surface, 62, 145, "GUARD NOW:",
                         UINT16_C(0xffe0), 1U, 10U);
            p4_draw_text(surface, 129, 145,
                         s_dragon_ability_names[ability],
                         UINT16_C(0xffff), 1U,
                         s_dragon_ability_name_lengths[ability]);
        } else {
            const char *const parry_hint =
                state->signal_guard_charges == 0U
                    ? "NO GUARDS LEFT" : "WAIT FOR THE FLASH";
            p4_draw_text(
                surface, state->signal_guard_charges == 0U ? 112 : 93,
                145, parry_hint, UINT16_C(0x9cf3), 1U,
                state->signal_guard_charges == 0U ? 14U : 18U);
        }
    } else if (state->signal_snare_ms != 0U) {
        p4_draw_text(surface, 92, 145, "THORN SNARE - MOVE SLOWED",
                     UINT16_C(0x87e0), 1U, 25U);
    } else {
        const char *const battle_hint = state->controller_active
            ? (weave ? "HOLD A + DPAD - START GUARDS" :
                       "A STRIKE - START GUARDS")
            : (weave ? "HOLD THE RUNE - TAP GUARD" :
                       "TAP STRIKE - GUARD THE TELL");
        const size_t battle_hint_length = state->controller_active
            ? (weave ? 28U : 23U) : (weave ? 25U : 27U);
        p4_draw_text(surface, weave ? 76 : 72, 145,
                     battle_hint, UINT16_C(0xbdf7), 1U,
                     battle_hint_length);
    }
    if (weave) {
        const uint32_t threshold_units = (uint32_t)rules.hold_ms * 2U;
        p4_draw_fill_rect(surface, 4, 162, 188, 33, UINT16_C(0x1025));
        p4_draw_rect(surface, 4, 162, 188, 33, color);
        draw_signal_city_icon(surface, state, 14U, 25, 178, 24U);
        p4_draw_text(surface, 43, 167, "RUNE CHARGE",
                     UINT16_C(0xffff), 1U, 11U);
        p4_draw_rect(surface, 43, 182, 97, 7, UINT16_C(0x7bef));
        p4_draw_fill_rect(surface, 44, 183,
                          threshold_units == 0U ? 0 : (int)(
                              state->signal_weave_charge_units * 95U /
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
        const bool strike_ready =
            state->signal_battle_outcome == BYTE_BUDDY_COMBAT_ACTIVE &&
            state->signal_battle_phase != SIGNAL_PHASE_INTRO &&
            state->signal_battle_phase < SIGNAL_PHASE_VICTORY &&
            state->signal_strike_cooldown_ms == 0U;
        draw_touch_button(surface, 4, 162, 188, 33,
                          strike_ready ? "PULSE STRIKE" : "RECHARGING",
                          strike_ready ? 12U : 10U,
                          strike_ready ? UINT16_C(0x07ff)
                                       : UINT16_C(0x4208),
                          state->signal_hit_ms != 0U);
        draw_signal_city_icon(surface, state, 12U, 23, 178, 24U);
    }
    const char *const guard_label = state->signal_guard_armed
        ? "PARRY ARMED" : state->signal_guard_charges == 0U
            ? "GUARD EMPTY" : guard_ready ? "GUARD NOW" : "AURA GUARD";
    const size_t guard_label_length = state->signal_guard_armed
        ? 11U : state->signal_guard_charges == 0U
            ? 11U : guard_ready ? 9U : 10U;
    draw_touch_button(surface, 198, 162, 118, 33,
                      guard_label, guard_label_length,
                      state->signal_guard_charges == 0U
                          ? UINT16_C(0x4208) : UINT16_C(0xffe0),
                      state->signal_guard_armed);
    draw_signal_city_icon(surface, state, 13U, 211, 178, 20U);
    draw_counter_fx_frame(
        surface, state, ability, 0U, 211, 178, 22U);
    draw_number(surface, 300, 166, state->signal_guard_charges,
                state->signal_guard_armed
                    ? UINT16_C(0x0000) : UINT16_C(0xffff));

    if (state->signal_battle_phase == SIGNAL_PHASE_INTRO) {
        p4_draw_fill_rect(surface, 50, 48, 220, 69, UINT16_C(0x000b));
        p4_draw_rect(surface, 50, 48, 220, 69, color);
        p4_draw_text(surface, 104, 57, "ENCOUNTER GENOME",
                     UINT16_C(0xffff), 1U, 16U);
        draw_passive_fx_frame(
            surface, state, passive,
            signal_passive_intro_phase(state),
            78, 87, 48U);
        draw_signal_attack_frame(
            surface, state, current_attack,
            (unsigned)(state->animation_ms / 130U) % 2U,
            245, 87, 38U);
        p4_draw_text(surface, 110, 77,
                     s_signal_attack_names[current_attack], color, 1U,
                     s_signal_attack_name_lengths[current_attack]);
        p4_draw_text(surface, 110, 93,
                     s_signal_passive_names[encounter.passive],
                     UINT16_C(0xfd20), 1U,
                     s_signal_passive_name_lengths[encounter.passive]);
        p4_draw_text(surface, 176, 93,
                     s_signal_arena_names[encounter.arena],
                     UINT16_C(0x9cf3), 1U,
                     s_signal_arena_name_lengths[encounter.arena]);
    } else if (state->signal_battle_outcome !=
               BYTE_BUDDY_COMBAT_ACTIVE) {
        const byte_buddy_combat_outcome_t outcome =
            (byte_buddy_combat_outcome_t)state->signal_battle_outcome;
        const bool victory = outcome == BYTE_BUDDY_COMBAT_VICTORY;
        p4_draw_fill_rect(surface, 44, 48, 232, 74, UINT16_C(0x000b));
        p4_draw_rect(surface, 44, 48, 232, 74,
                     victory ? UINT16_C(0xffe0) : UINT16_C(0xf81f));
        if (victory) {
            draw_signal_city_icon(
                surface, state, 15U, 78, 84,
                smooth_signal_pulse(
                    SIGNAL_VICTORY_MS - state->signal_phase_ms,
                    52U, 10U, SIGNAL_VICTORY_MS * 2U));
            draw_outcome_fx_frame(
                surface, state, outcome,
                fx_timeline_phase(
                    state->signal_phase_ms, SIGNAL_VICTORY_MS),
                78, 84, 58U);
            p4_draw_text(surface, state->signal_training_mode ? 110 : 116,
                         63,
                         state->signal_training_mode
                             ? "DRILL COMPLETE" : "SIGNAL TAMED",
                         UINT16_C(0xffe0), 1U,
                         state->signal_training_mode ? 14U : 12U);
            p4_draw_text(surface, state->signal_training_mode ? 89 : 98,
                         87,
                         state->signal_training_mode
                             ? "NO DNA OR SPARKS AWARDED"
                             : "DNA + SPARKS SECURED",
                         UINT16_C(0xffff), 1U,
                         state->signal_training_mode ? 24U : 20U);
        } else {
            const uint16_t defeat_duration =
                state->signal_battle_outcome == BYTE_BUDDY_COMBAT_RETREATED
                    ? SIGNAL_RETREAT_MS : SIGNAL_DEFEAT_MS;
            draw_outcome_fx_frame(
                surface, state, outcome,
                fx_timeline_phase(
                    state->signal_phase_ms, defeat_duration),
                78, 84, 54U);
            const bool timeout = outcome ==
                BYTE_BUDDY_COMBAT_DEFEAT_TIMEOUT;
            const bool retreated = outcome ==
                BYTE_BUDDY_COMBAT_RETREATED;
            const char *const title = retreated ? "RETREATING" :
                timeout ? "LINK TIMED OUT" : "DRAGON DOWN";
            const size_t title_length = retreated ? 10U :
                timeout ? 14U : 11U;
            p4_draw_text(surface, 112, 63, title,
                         UINT16_C(0xf81f), 1U, title_length);
            p4_draw_text(surface, 98, 87, "NO DNA OR SPARKS",
                         UINT16_C(0xffff), 1U, 16U);
            p4_draw_text(surface, 98, 102, "REMATCH AT TRACKER",
                         UINT16_C(0x9cf3), 1U, 18U);
        }
    }
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
    const uint16_t primary_color = signal_color_for_hue(
        (uint8_t)(lineage.primary_hue & 7U));
    const uint16_t secondary_color = signal_color_for_hue(
        (uint8_t)(lineage.secondary_hue & 7U));
    const uint16_t halo_color = (lineage.adaptations &
            BYTE_BUDDY_ADAPTATION_PRISMATIC) != 0U
        ? secondary_color : primary_color;
    draw_signal_header(surface, state, "DRAGON GENOME", 13U);
    p4_draw_fill_rect(surface, 0, 25, 320, 175, UINT16_C(0x080f));
    p4_draw_text(surface, 12, 32, "CURRENT LINEAGE",
                 UINT16_C(0x7bef), 1U, 15U);
    p4_draw_text(surface, 12, 44, s_lineage_names[lineage.tier],
                 primary_color, 1U,
                 s_lineage_name_lengths[lineage.tier]);
    if (lineage.resonance > BYTE_BUDDY_RESONANCE_NONE &&
        lineage.resonance < BYTE_BUDDY_RESONANCE_COUNT) {
        p4_draw_text(surface, 12, 57,
                     s_resonance_names[lineage.resonance],
                     signal_color_for_hue(lineage.secondary_hue), 1U,
                     s_resonance_name_lengths[lineage.resonance]);
    }
    if (lineage.tier == BYTE_BUDDY_LINEAGE_DORMANT) {
        draw_signal_city_icon(surface, state, 8U, 116, 52, 34U);
    } else {
        draw_lineage_badge(
            surface, state, lineage_badge_frame(lineage), 116, 52, 34U);
    }
    draw_dragon(surface, state, 68, 96);
    if (lineage.tier == BYTE_BUDDY_LINEAGE_DORMANT) {
        draw_scan_fx_frame(
            surface, state, BYTE_BUDDY_SCAN_FX_EMPTY,
            fx_loop_phase(state->animation_ms, 190U),
            118, 119, 36U);
    } else {
        draw_signal_seed_visual(
            surface, state, signal_visual_for_lineage(lineage),
            118, 119, 36U, state->animation_ms);
    }

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
                     primary_color, 1U, 6U);
        p4_draw_text(surface, 146, 47, "HALO", UINT16_C(0x7bef),
                     1U, 4U);
        p4_draw_text(surface, 207, 47,
                     s_lineage_halo_names[lineage.halo & 3U],
                     halo_color, 1U, 5U);
        p4_draw_text(surface, 146, 62, "SIGIL", UINT16_C(0x7bef),
                     1U, 5U);
        p4_draw_text(surface, 207, 62,
                     s_lineage_mark_names[lineage.marking & 3U],
                     secondary_color, 1U, 5U);
        p4_draw_text(surface, 146, 77, "AURA", UINT16_C(0x7bef),
                     1U, 4U);
        p4_draw_text(surface, 207, 77,
                     s_lineage_aura_names[lineage.aura & 3U],
                     secondary_color, 1U, 5U);
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
        draw_lineage_badge(surface, state, 13U, 138, 109, 8U);
        p4_draw_text(surface, 146, 107, "BAND", UINT16_C(0x7bef),
                     1U, 4U);
        draw_number(surface, 180, 107, lineage.channel_families,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 190, 107, "/4", UINT16_C(0x7bef),
                     1U, 2U);
        draw_lineage_badge(surface, state, 14U, 214, 109, 8U);
        p4_draw_text(surface, 222, 107, "HUE", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 248, 107,
                    lineage.hue_diversity < 6U
                        ? lineage.hue_diversity : 6U,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 258, 107, "/6", UINT16_C(0x7bef),
                     1U, 2U);
        draw_lineage_badge(surface, state, 15U, 138, 119, 8U);
        p4_draw_text(surface, 146, 117, "MIX", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 174, 117,
                    lineage.part_diversity < 14U
                        ? lineage.part_diversity : 14U,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 190, 117, "/14", UINT16_C(0x7bef),
                     1U, 3U);
        p4_draw_text(surface, 222, 117, "RAR", UINT16_C(0x7bef),
                     1U, 3U);
        draw_number(surface, 248, 117,
                    lineage.rarity_diversity < 4U
                        ? lineage.rarity_diversity : 4U,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 258, 117, "/4", UINT16_C(0x7bef),
                     1U, 2U);
        if (lineage.shielded) {
            draw_lineage_badge(surface, state, 8U, 138, 128, 8U);
            p4_draw_text(surface, 146, 126, "WARD",
                         UINT16_C(0x07ff), 1U, 4U);
        }
        if (lineage.phantom) {
            draw_lineage_badge(surface, state, 9U, 175, 128, 8U);
            p4_draw_text(surface, 183, 126, "GHOST",
                         UINT16_C(0xf81f), 1U, 5U);
        }
        if ((lineage.adaptations &
             BYTE_BUDDY_ADAPTATION_WIDEBAND) != 0U) {
            draw_lineage_badge(surface, state, 10U, 220, 128, 8U);
            p4_draw_text(surface, 228, 126, "WIDE",
                         UINT16_C(0xffe0), 1U, 4U);
        }
        if ((lineage.adaptations &
             BYTE_BUDDY_ADAPTATION_PRISMATIC) != 0U) {
            draw_lineage_badge(surface, state, 11U, 138, 136, 8U);
            p4_draw_text(surface, 146, 133, "PRISM",
                         signal_color_for_hue(lineage.secondary_hue),
                         1U, 5U);
        }
        if ((lineage.adaptations &
             BYTE_BUDDY_ADAPTATION_CHIMERA) != 0U) {
            draw_lineage_badge(surface, state, 12U, 184, 136, 8U);
            p4_draw_text(surface, 192, 133, "CHIMERA",
                         signal_color_for_hue(lineage.primary_hue),
                         1U, 7U);
        }
    }

    p4_draw_rect(surface, 4, 140, 312, 24, UINT16_C(0x4208));
    if (lineage.resonance >= BYTE_BUDDY_RESONANCE_ETERNAL) {
        draw_lineage_badge(surface, state, 7U, 59, 152, 20U);
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
        const unsigned next_badge_frame = next_is_resonance
            ? 4U + next_rank : (unsigned)next_rank - 1U;
        draw_lineage_badge(
            surface, state, next_badge_frame, 18, 151, 18U);
        p4_draw_text(surface, 30, 145, "NEXT", UINT16_C(0x7bef),
                     1U, 4U);
        p4_draw_text(surface, 64, 145, next_name,
                     primary_color, 1U, next_name_length);
        p4_draw_text(surface, 126, 145, "LINK", UINT16_C(0x7bef),
                     1U, 4U);
        draw_number(surface, 160, 145, state->signal_consumed_count,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 174, 145, "/", UINT16_C(0x7bef), 1U, 1U);
        draw_number(surface, 184, 145, next_links,
                    UINT16_C(0xffff));
        p4_draw_text(surface, 215, 145, "DNA", UINT16_C(0x7bef),
                     1U, 3U);
        if (next_diversity == 0U) {
            p4_draw_text(surface, 239, 145, "OPEN",
                         UINT16_C(0xffff), 1U, 4U);
        } else {
            draw_number(surface, 239, 145, lineage.diversity,
                        UINT16_C(0xffff));
            p4_draw_text(surface, 253, 145, "/", UINT16_C(0x7bef),
                         1U, 1U);
            draw_number(surface, 263, 145, next_diversity,
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

static void draw_scene_transition(p4_game_surface_t *surface,
                                  const byte_buddy_state_t *state)
{
    if (state->scene_transition_ms == 0U) {
        return;
    }
    const uint16_t progress = (uint16_t)(
        (uint32_t)(SCENE_TRANSITION_MS - state->scene_transition_ms) *
        UINT16_MAX / SCENE_TRANSITION_MS);
    const uint16_t eased = p4_ease_smoothstep_u16(progress);
    const int cover = (int)(
        (uint32_t)(UINT16_MAX - eased) *
        (P4_GAME_SURFACE_WIDTH / 2U) / UINT16_MAX);
    if (cover > 0) {
        p4_draw_fill_rect(surface, 0, 0, cover,
                          P4_GAME_SURFACE_HEIGHT, UINT16_C(0x000b));
        p4_draw_fill_rect(surface, P4_GAME_SURFACE_WIDTH - cover, 0,
                          cover, P4_GAME_SURFACE_HEIGHT,
                          UINT16_C(0x000b));
        p4_draw_fill_rect(surface, cover - 1, 0, 2,
                          P4_GAME_SURFACE_HEIGHT, signal_color(state));
        p4_draw_fill_rect(surface,
                          P4_GAME_SURFACE_WIDTH - cover - 1, 0, 2,
                          P4_GAME_SURFACE_HEIGHT, signal_color(state));
    }
    const uint32_t remaining = (uint32_t)(UINT16_MAX - eased);
    const int marker = (int)(remaining * 12U / UINT16_MAX);
    const int center_x = P4_GAME_SURFACE_WIDTH / 2;
    const int center_y = P4_GAME_SURFACE_HEIGHT / 2;
    const uint16_t accent = signal_color(state);
    const unsigned icon_size = (unsigned)(
        remaining * 38U / UINT16_MAX);
    if (icon_size >= 4U) {
        const unsigned transition_frame = state->signal_hunt ? 9U :
            state->lineage_panel ? 11U : state->upgrade_shop ? 10U :
            state->mini_game ? 6U : 8U;
        draw_environment_chrome_icon(
            surface, state, transition_frame,
            center_x, center_y, icon_size);
    }
    if (marker >= 2) {
        p4_draw_rect(surface, center_x - marker, center_y - marker,
                     marker * 2 + 1, marker * 2 + 1, accent);
        p4_draw_fill_rect(surface, center_x - 1, center_y - marker - 3,
                          3, 2, UINT16_C(0xffff));
        p4_draw_fill_rect(surface, center_x - 1, center_y + marker + 2,
                          3, 2, UINT16_C(0xffff));
        p4_draw_fill_rect(surface, center_x - marker - 3, center_y - 1,
                          2, 3, UINT16_C(0xffff));
        p4_draw_fill_rect(surface, center_x + marker + 2, center_y - 1,
                          2, 3, UINT16_C(0xffff));
    }
}

static void draw_save_exit_overlay(p4_game_surface_t *surface,
                                   const byte_buddy_state_t *state)
{
    if (!state->exit_pending) {
        return;
    }
    const uint16_t accent = signal_color(state);
    p4_draw_fill_rect(surface, 82, 69, 156, 62, UINT16_C(0x000b));
    p4_draw_rect(surface, 82, 69, 156, 62, accent);
    p4_draw_rect(surface, 84, 71, 152, 58, UINT16_C(0x39e7));
    draw_scan_fx_frame(
        surface, state, BYTE_BUDDY_SCAN_FX_BUSY,
        fx_loop_phase(state->animation_ms, 120U), 108, 100, 38U);
    p4_draw_text(surface, 135, 84, "SAVING", UINT16_C(0xffff),
                 2U, sizeof("SAVING") - 1U);
    p4_draw_text(surface, 139, 108, "BUDDY SAFE",
                 UINT16_C(0x9cf3), 1U, sizeof("BUDDY SAFE") - 1U);
}

static void draw_save_error_badge(p4_game_surface_t *surface,
                                  const byte_buddy_state_t *state)
{
    if (!state->save_error || state->exit_pending) {
        return;
    }
    p4_draw_fill_rect(surface, 202, 2, 116, 21, UINT16_C(0x000b));
    p4_draw_rect(surface, 202, 2, 116, 21, UINT16_C(0xf800));
    draw_scan_fx_frame(
        surface, state, BYTE_BUDDY_SCAN_FX_OFFLINE,
        fx_loop_phase(state->animation_ms, 180U), 213, 12, 16U);
    p4_draw_text(surface, 225, 8, "SESSION ONLY", UINT16_C(0xffdf),
                 1U, sizeof("SESSION ONLY") - 1U);
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
        draw_scene_transition(surface, state);
        draw_save_error_badge(surface, state);
        draw_save_exit_overlay(surface, state);
        return true;
    }
    if (state->lineage_panel) {
        draw_lineage_panel(surface, state);
        draw_scene_transition(surface, state);
        draw_save_error_badge(surface, state);
        draw_save_exit_overlay(surface, state);
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
        draw_scene_transition(surface, state);
        draw_save_error_badge(surface, state);
        draw_save_exit_overlay(surface, state);
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
        (void)draw_art_frame_scaled(
            surface, state, STAR_CATCHER_REWARD_SHEET, 0U,
            267, 11, 12U);
        draw_number(surface, 274, 7, state->coins, UINT16_C(0xffff));
        draw_bar(surface, 34, "FULL", state->hunger, UINT16_C(0x07E0));
        draw_bar(surface, 44, "JOY", state->joy, UINT16_C(0xFFE0));
        draw_bar(surface, 54, "CLEAN", state->hygiene, UINT16_C(0x07FF));
        draw_bar(surface, 64, "ENERGY", state->energy, UINT16_C(0xF81F));
        draw_growth_panel(surface, state);
        p4_game_feedback_draw_audio_effect(
            surface, &state->audio, 160, 96);
        draw_dragon(surface, state, 160, 58);
        draw_lowest_need(surface, state, 126, 102);
        const byte_buddy_signal_lineage_t lineage = current_lineage(state);
        p4_draw_fill_rect(surface, 8, 109, 106, 24, UINT16_C(0x000b));
        p4_draw_rect(surface, 8, 109, 106, 24, signal_color(state));
        if (lineage.tier == BYTE_BUDDY_LINEAGE_DORMANT) {
            draw_reaction_fx_frame(surface, state, 0U, 20, 121, 18U);
            p4_draw_text(surface, 32, 118, "TAP TO PET",
                         UINT16_C(0x9cf3), 1U, 10U);
        } else {
            size_t length = 0U;
            const char *const badge = lineage_badge_name(
                lineage, &length);
            draw_lineage_badge(
                surface, state, lineage_badge_frame(lineage),
                20, 121, 18U);
            p4_draw_text(surface, 32, 118, badge,
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
    draw_scene_transition(surface, state);
    draw_save_error_badge(surface, state);
    draw_save_exit_overlay(surface, state);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    if (context != NULL && context->state != NULL) {
        byte_buddy_state_t *const state = context->state;
        refresh_save_dirty(state);
        service_save(context, state);
    }
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_byte_buddy_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(108),
    .id = "org.p4console.byte-buddy",
    .title = "Byte Buddy",
    .subtitle = "Raise a signal dragon",
    .accent_rgb565 = UINT16_C(0xF81F),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                             P4_GAME_CAP_STORAGE,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM |
                             P4_GAME_CAP_SIGNAL_SCAN |
                             P4_GAME_CAP_SAVE,
    .state_bytes = sizeof(byte_buddy_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
