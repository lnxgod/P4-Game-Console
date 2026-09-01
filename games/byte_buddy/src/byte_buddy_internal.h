// SPDX-License-Identifier: MIT

#ifndef P4_BYTE_BUDDY_INTERNAL_H
#define P4_BYTE_BUDDY_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BYTE_BUDDY_STAGE_EGG = 0,
    BYTE_BUDDY_STAGE_BABY,
    BYTE_BUDDY_STAGE_WINGED,
    BYTE_BUDDY_STAGE_FLYING,
    BYTE_BUDDY_STAGE_ELEMENTAL,
    BYTE_BUDDY_STAGE_COUNT,
} byte_buddy_stage_t;

typedef enum {
    BYTE_BUDDY_ELEMENT_MYSTERY = 0,
    BYTE_BUDDY_ELEMENT_FIRE,
    BYTE_BUDDY_ELEMENT_ICE,
    BYTE_BUDDY_ELEMENT_ACID,
    BYTE_BUDDY_ELEMENT_COUNT,
} byte_buddy_element_t;

typedef enum {
    BYTE_BUDDY_WINGS_SHINY = 0,
    BYTE_BUDDY_WINGS_SPIKED,
    BYTE_BUDDY_WING_STYLE_COUNT,
} byte_buddy_wing_style_t;

typedef enum {
    BYTE_BUDDY_UPGRADE_WINGS = 0,
    BYTE_BUDDY_UPGRADE_AURA,
    BYTE_BUDDY_UPGRADE_NEST,
    BYTE_BUDDY_UPGRADE_MAGNET,
    BYTE_BUDDY_UPGRADE_COUNT,
} byte_buddy_upgrade_t;

typedef enum {
    BYTE_BUDDY_STYLE_BODY = 0,
    BYTE_BUDDY_STYLE_EYES,
    BYTE_BUDDY_STYLE_HORNS,
    BYTE_BUDDY_STYLE_TRAIL,
    BYTE_BUDDY_STYLE_COUNT,
} byte_buddy_style_t;

typedef enum {
    BYTE_BUDDY_MORPH_NEBULA = 0,
    BYTE_BUDDY_MORPH_SUNGOLD,
    BYTE_BUDDY_MORPH_JADE,
    BYTE_BUDDY_MORPH_GLACIER,
    BYTE_BUDDY_MORPH_COUNT,
} byte_buddy_morph_t;

typedef enum {
    BYTE_BUDDY_TOUCH_NONE = 0,
    BYTE_BUDDY_TOUCH_EXIT,
    BYTE_BUDDY_TOUCH_DRAGON,
    BYTE_BUDDY_TOUCH_FEED,
    BYTE_BUDDY_TOUCH_PLAY,
    BYTE_BUDDY_TOUCH_CLEAN,
    BYTE_BUDDY_TOUCH_REST,
    BYTE_BUDDY_TOUCH_SHOP,
    BYTE_BUDDY_TOUCH_PREVIEW,
    BYTE_BUDDY_TOUCH_UPGRADE_WINGS,
    BYTE_BUDDY_TOUCH_UPGRADE_AURA,
    BYTE_BUDDY_TOUCH_UPGRADE_NEST,
    BYTE_BUDDY_TOUCH_UPGRADE_MAGNET,
    BYTE_BUDDY_TOUCH_SHOP_POWER,
    BYTE_BUDDY_TOUCH_SHOP_STYLE,
    BYTE_BUDDY_TOUCH_STYLE_BODY_SELECT,
    BYTE_BUDDY_TOUCH_STYLE_BODY_BUY,
    BYTE_BUDDY_TOUCH_STYLE_EYES_SELECT,
    BYTE_BUDDY_TOUCH_STYLE_EYES_BUY,
    BYTE_BUDDY_TOUCH_STYLE_HORNS_SELECT,
    BYTE_BUDDY_TOUCH_STYLE_HORNS_BUY,
    BYTE_BUDDY_TOUCH_STYLE_TRAIL_SELECT,
    BYTE_BUDDY_TOUCH_STYLE_TRAIL_BUY,
    BYTE_BUDDY_TOUCH_STYLE_REMIX,
    BYTE_BUDDY_TOUCH_CLOSE_SHOP,
    BYTE_BUDDY_TOUCH_MOVE_DRAGON,
    BYTE_BUDDY_TOUCH_DONE_PLAYING,
    BYTE_BUDDY_TOUCH_SIGNAL_BACK,
    BYTE_BUDDY_TOUCH_SIGNAL_SCAN,
    BYTE_BUDDY_TOUCH_SIGNAL_ROW_0,
    BYTE_BUDDY_TOUCH_SIGNAL_ROW_1,
    BYTE_BUDDY_TOUCH_SIGNAL_ROW_2,
    BYTE_BUDDY_TOUCH_SIGNAL_ROW_3,
    BYTE_BUDDY_TOUCH_SIGNAL_ROW_4,
    BYTE_BUDDY_TOUCH_SIGNAL_PREVIOUS,
    BYTE_BUDDY_TOUCH_SIGNAL_NEXT,
    BYTE_BUDDY_TOUCH_SIGNAL_TRACK,
    BYTE_BUDDY_TOUCH_SIGNAL_BATTLE,
    BYTE_BUDDY_TOUCH_SIGNAL_STRIKE,
    BYTE_BUDDY_TOUCH_SIGNAL_GUARD,
} byte_buddy_touch_target_t;

typedef enum {
    BYTE_BUDDY_SIGNAL_LIST = 0,
    BYTE_BUDDY_SIGNAL_TRACKER,
    BYTE_BUDDY_SIGNAL_BATTLE,
} byte_buddy_signal_view_t;

enum {
    BYTE_BUDDY_SIGNAL_PAGE_ROWS = 5,
};

typedef enum {
    BYTE_BUDDY_BATTLE_PULSE_RUSH = 0,
    BYTE_BUDDY_BATTLE_RESONANCE_WEAVE,
} byte_buddy_signal_battle_pattern_t;

typedef enum {
    BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST = 0,
    BYTE_BUDDY_SIGNAL_ATTACK_PRISM_LANCE,
    BYTE_BUDDY_SIGNAL_ATTACK_THORN_SNARE,
    BYTE_BUDDY_SIGNAL_ATTACK_COMET_CRASH,
    BYTE_BUDDY_SIGNAL_ATTACK_COUNT,
} byte_buddy_signal_attack_t;

typedef enum {
    BYTE_BUDDY_SIGNAL_PASSIVE_WARD = 0,
    BYTE_BUDDY_SIGNAL_PASSIVE_ECHO,
    BYTE_BUDDY_SIGNAL_PASSIVE_SIPHON,
    BYTE_BUDDY_SIGNAL_PASSIVE_OVERCLOCK,
    BYTE_BUDDY_SIGNAL_PASSIVE_COUNT,
} byte_buddy_signal_passive_t;

typedef enum {
    BYTE_BUDDY_SIGNAL_ARENA_STEADY = 0,
    BYTE_BUDDY_SIGNAL_ARENA_HEAVY,
    BYTE_BUDDY_SIGNAL_ARENA_QUICK,
    BYTE_BUDDY_SIGNAL_ARENA_ECHO,
    BYTE_BUDDY_SIGNAL_ARENA_SHIFT,
    BYTE_BUDDY_SIGNAL_ARENA_COUNT,
} byte_buddy_signal_arena_t;

typedef enum {
    BYTE_BUDDY_ABILITY_NOVA_PARRY = 0,
    BYTE_BUDDY_ABILITY_FLARE_COUNTER,
    BYTE_BUDDY_ABILITY_GLACIER_WARD,
    BYTE_BUDDY_ABILITY_JAM_FIELD,
    BYTE_BUDDY_ABILITY_COUNT,
} byte_buddy_dragon_ability_t;

typedef enum {
    BYTE_BUDDY_COMBAT_ACTIVE = 0,
    BYTE_BUDDY_COMBAT_VICTORY,
    BYTE_BUDDY_COMBAT_DEFEAT_HP,
    BYTE_BUDDY_COMBAT_DEFEAT_TIMEOUT,
    BYTE_BUDDY_COMBAT_RETREATED,
} byte_buddy_combat_outcome_t;

typedef enum {
    BYTE_BUDDY_SCAN_FX_SCANNING = 0,
    BYTE_BUDDY_SCAN_FX_BUSY,
    BYTE_BUDDY_SCAN_FX_OFFLINE,
    BYTE_BUDDY_SCAN_FX_EMPTY,
    BYTE_BUDDY_SCAN_FX_COUNT,
} byte_buddy_scan_fx_t;

typedef enum {
    BYTE_BUDDY_EVOLUTION_FX_BABY_TO_WINGED = 0,
    BYTE_BUDDY_EVOLUTION_FX_WINGED_TO_FLYING,
    BYTE_BUDDY_EVOLUTION_FX_FLYING_TO_ELEMENTAL,
    BYTE_BUDDY_EVOLUTION_FX_GENOME,
    BYTE_BUDDY_EVOLUTION_FX_COUNT,
} byte_buddy_evolution_fx_t;

typedef enum {
    BYTE_BUDDY_NEED_FX_HUNGER = 0,
    BYTE_BUDDY_NEED_FX_JOY,
    BYTE_BUDDY_NEED_FX_HYGIENE,
    BYTE_BUDDY_NEED_FX_ENERGY,
    BYTE_BUDDY_NEED_FX_COUNT,
} byte_buddy_need_fx_t;

typedef enum {
    BYTE_BUDDY_ACTIVITY_FX_STAR_READY = 0,
    BYTE_BUDDY_ACTIVITY_FX_STAR_MISS,
    BYTE_BUDDY_ACTIVITY_FX_STAR_SUMMARY,
    BYTE_BUDDY_ACTIVITY_FX_SHOP,
    BYTE_BUDDY_ACTIVITY_FX_COUNT,
} byte_buddy_activity_fx_t;

typedef struct {
    uint8_t attack;
    uint8_t passive;
    uint8_t weakness;
    uint8_t arena;
    uint8_t threat;
    uint8_t max_hp;
    uint8_t damage;
    uint8_t starting_ward;
    uint16_t attack_period_ms;
    uint16_t telegraph_ms;
    uint32_t form_id;
    bool hidden;
    bool protected_signal;
} byte_buddy_signal_encounter_t;

typedef struct {
    uint8_t player_damage;
    uint8_t counter_damage;
    uint8_t enemy_heal;
    uint8_t ward_damage;
    uint16_t delay_ms;
    uint16_t status_ms;
} byte_buddy_signal_defense_t;

typedef struct {
    uint16_t x;
    uint16_t y;
} byte_buddy_signal_weave_node_t;

typedef struct {
    uint8_t required_locks;
    uint8_t touch_radius;
    uint16_t hold_ms;
} byte_buddy_signal_weave_rules_t;

typedef struct {
    byte_buddy_element_t element;
    uint8_t rarity;
    uint8_t hue;
    uint8_t strength;
    uint8_t reward_coins;
    uint8_t battle_hp;
} byte_buddy_signal_profile_t;

typedef struct {
    uint8_t core;
    uint8_t halo;
    uint8_t sigil;
    uint8_t aura;
    uint8_t hue;
    uint8_t rarity;
    uint16_t recipe_id;
} byte_buddy_signal_genome_t;

typedef enum {
    BYTE_BUDDY_LINEAGE_DORMANT = 0,
    BYTE_BUDDY_LINEAGE_SPARK,
    BYTE_BUDDY_LINEAGE_CREST,
    BYTE_BUDDY_LINEAGE_AURORA,
    BYTE_BUDDY_LINEAGE_ASCENDED,
    BYTE_BUDDY_LINEAGE_MYTHIC,
    BYTE_BUDDY_LINEAGE_TIER_COUNT,
} byte_buddy_lineage_tier_t;

typedef enum {
    BYTE_BUDDY_RESONANCE_NONE = 0,
    BYTE_BUDDY_RESONANCE_NOVA,
    BYTE_BUDDY_RESONANCE_GALAXY,
    BYTE_BUDDY_RESONANCE_ETERNAL,
    BYTE_BUDDY_RESONANCE_COUNT,
} byte_buddy_resonance_t;

enum {
    BYTE_BUDDY_SIGNAL_MAX_CONSUMED = 48,
    BYTE_BUDDY_ADAPTATION_SHIELD = UINT8_C(1) << 0U,
    BYTE_BUDDY_ADAPTATION_PHANTOM = UINT8_C(1) << 1U,
    BYTE_BUDDY_ADAPTATION_WIDEBAND = UINT8_C(1) << 2U,
    BYTE_BUDDY_ADAPTATION_PRISMATIC = UINT8_C(1) << 3U,
    BYTE_BUDDY_ADAPTATION_CHIMERA = UINT8_C(1) << 4U,
};

typedef struct {
    uint16_t part_mask;
    uint8_t hue_mask;
    uint8_t rarity_mask;
    uint8_t channel_mask;
    uint8_t protected_count;
    uint8_t hidden_count;
    uint8_t core_votes[4];
    uint8_t halo_votes[4];
    uint8_t sigil_votes[4];
    uint8_t aura_votes[4];
    uint8_t hue_votes[8];
} byte_buddy_lineage_genes_t;

typedef struct {
    uint8_t tier;
    uint8_t resonance;
    uint8_t adaptations;
    uint8_t family;
    uint8_t halo;
    uint8_t marking;
    uint8_t aura;
    uint8_t primary_hue;
    uint8_t secondary_hue;
    uint8_t part_diversity;
    uint8_t hue_diversity;
    uint8_t rarity_diversity;
    uint8_t diversity;
    uint8_t channel_families;
    bool shielded;
    bool phantom;
} byte_buddy_signal_lineage_t;

typedef struct {
    uint8_t level;
    uint8_t power;
    uint8_t speed;
    uint8_t guard;
    uint8_t magic;
} byte_buddy_battle_stats_t;

typedef struct {
    uint8_t strike_damage;
    uint8_t guard_charges;
    uint16_t start_time_ms;
    uint16_t guard_time_ms;
} byte_buddy_lineage_battle_traits_t;

byte_buddy_stage_t byte_buddy_stage_for_interactions(uint16_t interactions);

byte_buddy_element_t byte_buddy_element_for_nurture(
    uint16_t feed_actions,
    uint16_t play_actions,
    uint16_t clean_actions,
    uint16_t rest_actions,
    uint16_t pet_actions);

byte_buddy_wing_style_t byte_buddy_wing_style_for_nurture(
    uint16_t feed_actions,
    uint16_t play_actions,
    uint16_t clean_actions,
    uint16_t rest_actions,
    uint16_t pet_actions);

byte_buddy_morph_t byte_buddy_morph_for_nurture(
    uint16_t feed_actions,
    uint16_t play_actions,
    uint16_t clean_actions,
    uint16_t rest_actions,
    uint16_t pet_actions);

uint16_t byte_buddy_upgrade_cost(uint8_t current_level);

uint16_t byte_buddy_style_cost(
    byte_buddy_style_t style, uint8_t unlocked_level);

uint8_t byte_buddy_remix_choice(
    uint32_t seed, uint8_t previous, uint8_t unlocked_level);

uint32_t byte_buddy_style_recipe_id(
    uint8_t body, uint8_t eyes, uint8_t horns, uint8_t trail,
    uint8_t wing_style, uint8_t mutation_hue);

uint8_t byte_buddy_level_for_interactions(uint16_t interactions);

uint16_t byte_buddy_star_fall_speed(uint8_t stage, uint8_t streak);

int32_t byte_buddy_catcher_step_q16(
    int32_t current_q16, int32_t target_q16,
    int32_t *velocity_q16, uint32_t elapsed_ms);

int32_t byte_buddy_controller_catcher_target_q16(
    int32_t current_q16, int32_t target_q16,
    uint32_t held_buttons, uint32_t elapsed_ms);

byte_buddy_battle_stats_t byte_buddy_battle_stats(
    uint16_t feed_actions,
    uint16_t play_actions,
    uint16_t clean_actions,
    uint16_t rest_actions,
    uint16_t pet_actions,
    uint8_t wings_level,
    uint8_t aura_level,
    uint8_t nest_level,
    uint8_t magnet_level);

byte_buddy_touch_target_t byte_buddy_touch_target(
    uint16_t x, uint16_t y, bool upgrade_shop,
    bool style_shop, bool mini_game);

byte_buddy_touch_target_t byte_buddy_signal_touch_target(
    uint16_t x, uint16_t y, byte_buddy_signal_view_t view);

uint8_t byte_buddy_signal_page_count(uint8_t result_count);

uint8_t byte_buddy_signal_clamp_page(
    uint8_t page, uint8_t result_count);

uint8_t byte_buddy_signal_page_index(
    uint8_t page, uint8_t row, uint8_t result_count);

byte_buddy_signal_battle_pattern_t byte_buddy_signal_battle_pattern(
    byte_buddy_signal_genome_t genome);

byte_buddy_signal_encounter_t byte_buddy_signal_encounter(
    uint64_t token, int8_t rssi_dbm, uint8_t channel, uint8_t flags);

byte_buddy_dragon_ability_t byte_buddy_dragon_ability(
    byte_buddy_element_t element);

byte_buddy_signal_attack_t byte_buddy_signal_attack_for(
    byte_buddy_signal_encounter_t encounter, uint8_t attack_index);

byte_buddy_signal_defense_t byte_buddy_signal_defense(
    byte_buddy_signal_encounter_t encounter,
    byte_buddy_dragon_ability_t ability,
    bool guarded, uint8_t attack_index);

uint8_t byte_buddy_counter_fx_frame(
    byte_buddy_dragon_ability_t ability, uint8_t phase);
uint8_t byte_buddy_outcome_fx_frame(
    byte_buddy_combat_outcome_t outcome, uint8_t phase);
uint8_t byte_buddy_passive_fx_frame(
    byte_buddy_signal_passive_t passive, uint8_t phase);
uint8_t byte_buddy_scan_fx_frame(
    byte_buddy_scan_fx_t state, uint8_t phase);
uint8_t byte_buddy_evolution_fx_frame(
    byte_buddy_evolution_fx_t evolution, uint8_t phase);
uint8_t byte_buddy_need_fx_frame(
    byte_buddy_need_fx_t need, uint8_t phase);
uint8_t byte_buddy_activity_fx_frame(
    byte_buddy_activity_fx_t activity, uint8_t phase);

uint8_t byte_buddy_signal_player_hp(
    byte_buddy_battle_stats_t stats, uint8_t nest_level,
    byte_buddy_signal_lineage_t lineage);

uint16_t byte_buddy_signal_strike_cooldown_ms(uint8_t speed);

uint8_t byte_buddy_signal_weave_node_index(
    byte_buddy_signal_genome_t genome, uint8_t step);

byte_buddy_signal_weave_node_t byte_buddy_signal_weave_node(
    byte_buddy_signal_genome_t genome, uint8_t step);

byte_buddy_signal_weave_rules_t byte_buddy_signal_weave_rules(
    byte_buddy_signal_genome_t genome,
    uint8_t strength, uint8_t magnet_level);

uint32_t byte_buddy_signal_weave_charge(
    uint32_t charge_units, uint32_t elapsed_ms,
    bool inside_target, uint32_t threshold_units);

byte_buddy_signal_profile_t byte_buddy_signal_profile(
    uint64_t token, int8_t rssi_dbm);

uint16_t byte_buddy_signal_recipe_id(
    uint8_t core, uint8_t halo, uint8_t sigil,
    uint8_t aura, uint8_t hue, uint8_t rarity);

byte_buddy_signal_genome_t byte_buddy_signal_genome(uint64_t token);

uint8_t byte_buddy_signal_channel_family(uint8_t channel);

uint8_t byte_buddy_signal_habitat_id(uint8_t channel, uint8_t flags);

uint32_t byte_buddy_signal_form_id(
    uint16_t genome_recipe_id, uint8_t channel, uint8_t flags);

int32_t byte_buddy_controller_cursor_axis_q16(
    int32_t current_q16, bool negative, bool positive,
    uint32_t elapsed_ms, int16_t minimum, int16_t maximum);

uint64_t byte_buddy_signal_lineage_contribution(
    uint64_t token, uint8_t channel, uint8_t flags);

void byte_buddy_lineage_add(
    byte_buddy_lineage_genes_t *genes,
    byte_buddy_signal_genome_t genome,
    uint8_t channel, uint8_t flags);

byte_buddy_signal_lineage_t byte_buddy_signal_lineage(
    uint64_t entropy, uint8_t unique_count,
    const byte_buddy_lineage_genes_t *genes);

byte_buddy_battle_stats_t byte_buddy_lineage_battle_stats(
    byte_buddy_battle_stats_t base,
    byte_buddy_signal_lineage_t lineage,
    const byte_buddy_lineage_genes_t *genes);

byte_buddy_lineage_battle_traits_t byte_buddy_lineage_battle_traits(
    byte_buddy_signal_lineage_t lineage);

uint8_t byte_buddy_signal_growth_reward(
    uint8_t rarity, uint8_t unique_count);

bool byte_buddy_signal_collection_has_room(uint8_t unique_count);

#endif
