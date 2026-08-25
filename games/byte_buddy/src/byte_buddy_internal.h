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
    uint8_t family;
    uint8_t halo;
    uint8_t marking;
    uint8_t aura;
    uint8_t primary_hue;
    uint8_t secondary_hue;
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

byte_buddy_signal_profile_t byte_buddy_signal_profile(
    uint64_t token, int8_t rssi_dbm);

uint16_t byte_buddy_signal_recipe_id(
    uint8_t core, uint8_t halo, uint8_t sigil,
    uint8_t aura, uint8_t hue, uint8_t rarity);

byte_buddy_signal_genome_t byte_buddy_signal_genome(uint64_t token);

uint8_t byte_buddy_signal_channel_family(uint8_t channel);

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

#endif
