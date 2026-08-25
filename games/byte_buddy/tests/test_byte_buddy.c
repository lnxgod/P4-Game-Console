// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/achievements.h"
#include "p4/audio.h"
#include "p4/game.h"

#include "byte_buddy_internal.h"

extern const p4_game_descriptor_t p4_byte_buddy_game;

static int s_failures;

typedef struct {
    p4_game_signal_snapshot_t snapshot;
    uint32_t requests;
    uint64_t focus_token;
} fake_signal_scan_t;

static fake_signal_scan_t s_signal_scan;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static bool fake_request_signal_scan(void *context, uint64_t focus_token)
{
    fake_signal_scan_t *const scan = context;
    if (scan == NULL) {
        return false;
    }
    ++scan->requests;
    scan->focus_token = focus_token;
    scan->snapshot = (p4_game_signal_snapshot_t){
        .generation = scan->requests,
        .status = P4_GAME_SIGNAL_READY,
        .count = 2U,
        .results = {
            {
                .token = UINT64_C(0x00123456789abcde),
                .label = "SKY GARDEN",
                .rssi_dbm = focus_token == 0U ? -82 : -45,
                .channel = 6U,
                .flags = P4_GAME_SIGNAL_PROTECTED |
                         P4_GAME_SIGNAL_SIMULATED,
            },
            {
                .token = UINT64_C(0x3ff0000000045678),
                .label = "HIDDEN SIGNAL",
                .rssi_dbm = -68,
                .channel = 11U,
                .flags = P4_GAME_SIGNAL_HIDDEN |
                         P4_GAME_SIGNAL_SIMULATED,
            },
        },
    };
    return true;
}

static bool fake_read_signal_scan(void *context,
                                  p4_game_signal_snapshot_t *snapshot)
{
    const fake_signal_scan_t *const scan = context;
    if (scan == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = scan->snapshot;
    return true;
}

static bool start_game(p4_game_instance_t *instance, void *state,
                       p4_audio_mixer_t *mixer,
                       p4_achievement_catalog_t *achievements)
{
    p4_audio_mixer_init(mixer);
    p4_achievement_catalog_init(achievements);
    static p4_game_services_t services;
    s_signal_scan = (fake_signal_scan_t){0};
    services = (p4_game_services_t){
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_AUDIO_TONE |
                                  P4_GAME_CAP_SIGNAL_SCAN,
        .audio_context = mixer,
        .game_id = p4_byte_buddy_game.id,
        .play_tone = p4_audio_mixer_service_play_tone,
        .stop_audio = p4_audio_mixer_service_stop,
        .achievement_context = achievements,
        .unlock_achievement = p4_achievement_catalog_service_unlock,
        .signal_scan_context = &s_signal_scan,
        .request_signal_scan = fake_request_signal_scan,
        .read_signal_scan = fake_read_signal_scan,
    };
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(
        instance, &p4_byte_buddy_game, &services,
        state, p4_byte_buddy_game.state_bytes);
}

static p4_game_result_t touch(p4_game_instance_t *instance,
                              uint16_t x, uint16_t y)
{
    const p4_game_input_t input = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    return p4_game_instance_update(instance, &input, 16U);
}

static void release_touch(p4_game_instance_t *instance)
{
    const p4_game_input_t input = {.touch_valid = true};
    CHECK(p4_game_instance_update(instance, &input, 16U) ==
          P4_GAME_CONTINUE);
}

static void tap(p4_game_instance_t *instance, uint16_t x, uint16_t y)
{
    CHECK(touch(instance, x, y) == P4_GAME_CONTINUE);
    release_touch(instance);
}

static p4_game_result_t buttons(p4_game_instance_t *instance,
                                uint32_t held, uint32_t pressed,
                                uint32_t elapsed_ms)
{
    const p4_game_input_t input = {
        .held = held,
        .pressed = pressed,
    };
    return p4_game_instance_update(instance, &input, elapsed_ms);
}

static void test_care_achievements_and_exit(void)
{
    CHECK(p4_game_descriptor_valid(&p4_byte_buddy_game));
    CHECK(p4_byte_buddy_game.launcher_id == 108U);
    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    CHECK(state != NULL);
    if (state == NULL) {
        return;
    }
    p4_game_instance_t instance;
    p4_audio_mixer_t mixer;
    p4_achievement_catalog_t achievements;
    CHECK(start_game(&instance, state, &mixer, &achievements));

    tap(&instance, 20U, 145U);
    CHECK(achievements.count == 1U);
    CHECK(achievements.entries[0].game_id[0] != '\0');
    CHECK(achievements.entries[0].id[0] != '\0');

    tap(&instance, 180U, 145U);
    tap(&instance, 180U, 145U);
    tap(&instance, 180U, 145U);
    CHECK(achievements.count == 2U);

    CHECK(touch(&instance, 20U, 12U) == P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    free(state);
}

static void test_render_bounds(void)
{
    enum {
        GUARD = 31,
        STRIDE = P4_GAME_SURFACE_WIDTH + 7,
        WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
        TOTAL = GUARD + WORDS + GUARD,
    };
    uint16_t *const allocation = calloc(TOTAL, sizeof(*allocation));
    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    CHECK(allocation != NULL && state != NULL);
    if (allocation == NULL || state == NULL) {
        free(allocation);
        free(state);
        return;
    }
    for (size_t index = 0U; index < TOTAL; ++index) {
        allocation[index] = UINT16_C(0x5aa5);
    }
    p4_game_surface_t surface = {
        .pixels = allocation + GUARD,
        .stride_pixels = STRIDE,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    p4_game_instance_t instance;
    p4_audio_mixer_t mixer;
    p4_achievement_catalog_t achievements;
    CHECK(start_game(&instance, state, &mixer, &achievements));
    CHECK(p4_game_instance_render(&instance, &surface));
    tap(&instance, 260U, 180U);
    tap(&instance, 260U, 180U);
    tap(&instance, 260U, 180U);
    tap(&instance, 260U, 180U);
    tap(&instance, 150U, 180U);
    tap(&instance, 50U, 60U);
    tap(&instance, 220U, 60U);
    CHECK(p4_game_instance_render(&instance, &surface));
    tap(&instance, 160U, 180U);
    tap(&instance, 110U, 145U);
    tap(&instance, 280U, 90U);
    CHECK(p4_game_instance_render(&instance, &surface));
    tap(&instance, 280U, 12U);
    CHECK(p4_game_instance_render(&instance, &surface));
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
    p4_game_instance_stop(&instance);
    free(state);
    free(allocation);
}

static void test_dragon_growth_and_traits(void)
{
    CHECK(byte_buddy_stage_for_interactions(0U) == BYTE_BUDDY_STAGE_EGG);
    CHECK(byte_buddy_stage_for_interactions(7U) == BYTE_BUDDY_STAGE_EGG);
    CHECK(byte_buddy_stage_for_interactions(8U) == BYTE_BUDDY_STAGE_BABY);
    CHECK(byte_buddy_stage_for_interactions(27U) == BYTE_BUDDY_STAGE_BABY);
    CHECK(byte_buddy_stage_for_interactions(28U) == BYTE_BUDDY_STAGE_WINGED);
    CHECK(byte_buddy_stage_for_interactions(59U) == BYTE_BUDDY_STAGE_WINGED);
    CHECK(byte_buddy_stage_for_interactions(60U) == BYTE_BUDDY_STAGE_FLYING);
    CHECK(byte_buddy_stage_for_interactions(103U) == BYTE_BUDDY_STAGE_FLYING);
    CHECK(byte_buddy_stage_for_interactions(104U) ==
          BYTE_BUDDY_STAGE_ELEMENTAL);
    CHECK(byte_buddy_stage_for_interactions(UINT16_MAX) ==
          BYTE_BUDDY_STAGE_ELEMENTAL);

    CHECK(byte_buddy_element_for_nurture(0U, 0U, 0U, 0U, 0U) ==
          BYTE_BUDDY_ELEMENT_MYSTERY);
    CHECK(byte_buddy_element_for_nurture(3U, 0U, 0U, 0U, 1U) ==
          BYTE_BUDDY_ELEMENT_FIRE);
    CHECK(byte_buddy_element_for_nurture(0U, 0U, 2U, 2U, 0U) ==
          BYTE_BUDDY_ELEMENT_ICE);
    CHECK(byte_buddy_element_for_nurture(0U, 4U, 0U, 0U, 1U) ==
          BYTE_BUDDY_ELEMENT_ACID);

    CHECK(byte_buddy_wing_style_for_nurture(0U, 0U, 2U, 2U, 2U) ==
          BYTE_BUDDY_WINGS_SHINY);
    CHECK(byte_buddy_wing_style_for_nurture(2U, 5U, 0U, 0U, 0U) ==
          BYTE_BUDDY_WINGS_SPIKED);

    CHECK(byte_buddy_morph_for_nurture(0U, 0U, 0U, 0U, 0U) ==
          BYTE_BUDDY_MORPH_NEBULA);
    CHECK(byte_buddy_morph_for_nurture(3U, 0U, 0U, 0U, 0U) ==
          BYTE_BUDDY_MORPH_SUNGOLD);
    CHECK(byte_buddy_morph_for_nurture(0U, 3U, 0U, 0U, 0U) ==
          BYTE_BUDDY_MORPH_JADE);
    CHECK(byte_buddy_morph_for_nurture(0U, 0U, 2U, 2U, 0U) ==
          BYTE_BUDDY_MORPH_GLACIER);
    CHECK(byte_buddy_morph_for_nurture(0U, 0U, 0U, 0U, 3U) ==
          BYTE_BUDDY_MORPH_NEBULA);

    CHECK(byte_buddy_star_fall_speed(BYTE_BUDDY_STAGE_EGG, 0U) == 58U);
    CHECK(byte_buddy_star_fall_speed(BYTE_BUDDY_STAGE_BABY, 0U) >
          byte_buddy_star_fall_speed(BYTE_BUDDY_STAGE_EGG, 0U));
    CHECK(byte_buddy_star_fall_speed(BYTE_BUDDY_STAGE_ELEMENTAL, 0U) ==
          82U);
    CHECK(byte_buddy_star_fall_speed(BYTE_BUDDY_STAGE_ELEMENTAL, 6U) ==
          100U);
    CHECK(byte_buddy_star_fall_speed(UINT8_MAX, UINT8_MAX) == 100U);

    const int32_t start_q16 = INT32_C(160) * INT32_C(65536);
    const int32_t target_q16 = INT32_C(296) * INT32_C(65536);
    int32_t fast_frames = start_q16;
    int32_t fast_velocity = 0;
    for (unsigned frame = 0U; frame < 10U; ++frame) {
        fast_frames = byte_buddy_catcher_step_q16(
            fast_frames, target_q16, &fast_velocity, 16U);
    }
    int32_t slow_frames = start_q16;
    int32_t slow_velocity = 0;
    for (unsigned frame = 0U; frame < 5U; ++frame) {
        slow_frames = byte_buddy_catcher_step_q16(
            slow_frames, target_q16, &slow_velocity, 32U);
    }
    int64_t frame_difference = (int64_t)fast_frames - slow_frames;
    if (frame_difference < 0) {
        frame_difference = -frame_difference;
    }
    CHECK(fast_frames > start_q16 && fast_frames <= target_q16);
    CHECK(slow_frames > start_q16 && slow_frames <= target_q16);
    CHECK(frame_difference < INT32_C(4) * INT32_C(65536));
    CHECK(byte_buddy_catcher_step_q16(
              start_q16, target_q16, NULL, 16U) == start_q16);

    const int32_t controller_right =
        byte_buddy_controller_catcher_target_q16(
            start_q16, start_q16, P4_BUTTON_RIGHT, 160U);
    const int32_t controller_left =
        byte_buddy_controller_catcher_target_q16(
            start_q16, start_q16, P4_BUTTON_LEFT, 160U);
    CHECK(controller_right > start_q16);
    CHECK(controller_left < start_q16);
    CHECK(byte_buddy_controller_catcher_target_q16(
              start_q16, controller_right, 0U, 16U) == start_q16);
    CHECK(byte_buddy_controller_catcher_target_q16(
              start_q16, start_q16, P4_BUTTON_LEFT | P4_BUTTON_RIGHT,
              16U) == start_q16);
    CHECK(byte_buddy_controller_catcher_target_q16(
              target_q16, target_q16, P4_BUTTON_RIGHT, 100U) ==
          target_q16);

    CHECK(byte_buddy_upgrade_cost(0U) == 2U);
    CHECK(byte_buddy_upgrade_cost(1U) == 5U);
    CHECK(byte_buddy_upgrade_cost(2U) == 8U);
    CHECK(byte_buddy_upgrade_cost(3U) == UINT16_MAX);

    CHECK(byte_buddy_style_cost(BYTE_BUDDY_STYLE_BODY, 0U) == 2U);
    CHECK(byte_buddy_style_cost(BYTE_BUDDY_STYLE_EYES, 0U) == 1U);
    CHECK(byte_buddy_style_cost(BYTE_BUDDY_STYLE_HORNS, 4U) ==
          UINT16_MAX);
    CHECK(byte_buddy_style_cost(BYTE_BUDDY_STYLE_TRAIL, 4U) ==
          UINT16_MAX);
    CHECK(byte_buddy_remix_choice(UINT32_C(0x12345678), 0U, 0U) == 0U);
    const uint8_t remix = byte_buddy_remix_choice(
        UINT32_C(0x12345678), 2U, 4U);
    CHECK(remix <= 4U);
    CHECK(remix != 2U);
    CHECK(byte_buddy_remix_choice(
              UINT32_C(0x12345678), 2U, 4U) == remix);
    CHECK(byte_buddy_style_recipe_id(0U, 0U, 0U, 0U, 0U, 0U) == 0U);
    CHECK(byte_buddy_style_recipe_id(7U, 5U, 4U, 4U, 1U, 7U) ==
          19199U);
    CHECK(byte_buddy_style_recipe_id(
              UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX,
              UINT8_MAX, UINT8_MAX) == 0U);
    static bool recipe_seen[19200];
    uint32_t recipe_count = 0U;
    for (uint8_t body = 0U; body < 8U; ++body) {
        for (uint8_t eyes = 0U; eyes < 6U; ++eyes) {
            for (uint8_t horns = 0U; horns < 5U; ++horns) {
                for (uint8_t trail = 0U; trail < 5U; ++trail) {
                    for (uint8_t wings = 0U; wings < 2U; ++wings) {
                        for (uint8_t hue = 0U; hue < 8U; ++hue) {
                            const uint32_t recipe_id =
                                byte_buddy_style_recipe_id(
                                    body, eyes, horns, trail, wings, hue);
                            CHECK(recipe_id < 19200U);
                            if (recipe_id < 19200U) {
                                CHECK(!recipe_seen[recipe_id]);
                                recipe_seen[recipe_id] = true;
                                ++recipe_count;
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(recipe_count == 19200U);

    CHECK(byte_buddy_signal_recipe_id(0U, 0U, 0U, 0U, 0U, 0U) == 0U);
    CHECK(byte_buddy_signal_recipe_id(3U, 3U, 3U, 3U, 7U, 3U) ==
          8191U);
    CHECK(byte_buddy_signal_recipe_id(
              UINT8_MAX, UINT8_MAX, UINT8_MAX, UINT8_MAX,
              UINT8_MAX, UINT8_MAX) == 0U);
    static bool signal_recipe_seen[8192];
    static bool derived_signal_recipe_seen[8192];
    static const uint16_t rarity_rolls[4] = {256U, 64U, 8U, 0U};
    uint32_t signal_recipe_count = 0U;
    uint32_t derived_signal_recipe_count = 0U;
    for (uint8_t core = 0U; core < 4U; ++core) {
        for (uint8_t halo = 0U; halo < 4U; ++halo) {
            for (uint8_t sigil = 0U; sigil < 4U; ++sigil) {
                for (uint8_t aura = 0U; aura < 4U; ++aura) {
                    for (uint8_t hue = 0U; hue < 8U; ++hue) {
                        for (uint8_t rarity = 0U; rarity < 4U; ++rarity) {
                            const uint16_t signal_recipe =
                                byte_buddy_signal_recipe_id(
                                    core, halo, sigil, aura, hue, rarity);
                            CHECK(signal_recipe < 8192U);
                            if (signal_recipe < 8192U) {
                                CHECK(!signal_recipe_seen[signal_recipe]);
                                signal_recipe_seen[signal_recipe] = true;
                                ++signal_recipe_count;
                            }
                            const uint64_t token = rarity_rolls[rarity] |
                                (uint64_t)hue << 18U |
                                (uint64_t)core << 21U |
                                (uint64_t)halo << 23U |
                                (uint64_t)aura << 25U |
                                (uint64_t)sigil << 27U;
                            const byte_buddy_signal_genome_t derived =
                                byte_buddy_signal_genome(token);
                            CHECK(derived.core == core &&
                                  derived.halo == halo &&
                                  derived.sigil == sigil &&
                                  derived.aura == aura &&
                                  derived.hue == hue &&
                                  derived.rarity == rarity &&
                                  derived.recipe_id == signal_recipe);
                            CHECK(derived.recipe_id < 8192U);
                            if (derived.recipe_id < 8192U) {
                                CHECK(!derived_signal_recipe_seen[
                                    derived.recipe_id]);
                                derived_signal_recipe_seen[
                                    derived.recipe_id] = true;
                                ++derived_signal_recipe_count;
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(signal_recipe_count == 8192U);
    CHECK(derived_signal_recipe_count == 8192U);
    const uint64_t genome_token = UINT64_C(0x00123456789abcde);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(genome_token);
    const byte_buddy_signal_genome_t repeated_genome =
        byte_buddy_signal_genome(genome_token);
    CHECK(genome.core == repeated_genome.core &&
          genome.halo == repeated_genome.halo &&
          genome.sigil == repeated_genome.sigil &&
          genome.aura == repeated_genome.aura &&
          genome.hue == repeated_genome.hue &&
          genome.rarity == repeated_genome.rarity &&
          genome.recipe_id == repeated_genome.recipe_id);
    CHECK(genome.core < 4U && genome.halo < 4U &&
          genome.sigil < 4U && genome.aura < 4U && genome.hue < 8U &&
          genome.rarity < 4U);
    CHECK(genome.recipe_id == byte_buddy_signal_recipe_id(
              genome.core, genome.halo, genome.sigil,
              genome.aura, genome.hue, genome.rarity));
    const byte_buddy_signal_genome_t changed_genome =
        byte_buddy_signal_genome(genome_token ^ (UINT64_C(1) << 21U));
    CHECK(changed_genome.core != genome.core);
    CHECK(changed_genome.recipe_id != genome.recipe_id);

    CHECK(byte_buddy_level_for_interactions(0U) == 1U);
    CHECK(byte_buddy_level_for_interactions(7U) == 1U);
    CHECK(byte_buddy_level_for_interactions(8U) == 2U);
    CHECK(byte_buddy_level_for_interactions(17U) == 2U);
    CHECK(byte_buddy_level_for_interactions(18U) == 3U);
    CHECK(byte_buddy_level_for_interactions(29U) == 3U);
    CHECK(byte_buddy_level_for_interactions(30U) == 4U);
    CHECK(byte_buddy_level_for_interactions(UINT16_MAX) == 99U);
    const byte_buddy_battle_stats_t stats = byte_buddy_battle_stats(
        8U, 6U, 3U, 3U, 4U, 1U, 2U, 1U, 2U);
    CHECK(stats.level == 3U);
    CHECK(stats.power == 10U);
    CHECK(stats.speed == 12U);
    CHECK(stats.guard == 8U);
    CHECK(stats.magic == 11U);

    CHECK(byte_buddy_touch_target(20U, 12U, false, false, false) ==
          BYTE_BUDDY_TOUCH_EXIT);
    CHECK(byte_buddy_touch_target(160U, 80U, false, false, false) ==
          BYTE_BUDDY_TOUCH_DRAGON);
    CHECK(byte_buddy_touch_target(20U, 145U, false, false, false) ==
          BYTE_BUDDY_TOUCH_FEED);
    CHECK(byte_buddy_touch_target(180U, 180U, false, false, false) ==
          BYTE_BUDDY_TOUCH_SHOP);
    CHECK(byte_buddy_touch_target(260U, 180U, false, false, false) ==
          BYTE_BUDDY_TOUCH_PREVIEW);
    CHECK(byte_buddy_touch_target(80U, 180U, false, false, false) ==
          BYTE_BUDDY_TOUCH_SIGNAL_SCAN);
    CHECK(byte_buddy_touch_target(50U, 60U, true, false, false) ==
          BYTE_BUDDY_TOUCH_UPGRADE_WINGS);
    CHECK(byte_buddy_touch_target(220U, 120U, true, false, false) ==
          BYTE_BUDDY_TOUCH_UPGRADE_MAGNET);
    CHECK(byte_buddy_touch_target(220U, 32U, true, false, false) ==
          BYTE_BUDDY_TOUCH_SHOP_STYLE);
    CHECK(byte_buddy_touch_target(200U, 60U, true, true, false) ==
          BYTE_BUDDY_TOUCH_STYLE_EYES_SELECT);
    CHECK(byte_buddy_touch_target(300U, 60U, true, true, false) ==
          BYTE_BUDDY_TOUCH_STYLE_EYES_BUY);
    CHECK(byte_buddy_touch_target(50U, 120U, true, true, false) ==
          BYTE_BUDDY_TOUCH_STYLE_HORNS_SELECT);
    CHECK(byte_buddy_touch_target(130U, 120U, true, true, false) ==
          BYTE_BUDDY_TOUCH_STYLE_HORNS_BUY);
    CHECK(byte_buddy_touch_target(80U, 180U, true, true, false) ==
          BYTE_BUDDY_TOUCH_STYLE_REMIX);
    CHECK(byte_buddy_touch_target(220U, 180U, true, true, false) ==
          BYTE_BUDDY_TOUCH_CLOSE_SHOP);
    CHECK(byte_buddy_touch_target(200U, 80U, false, false, true) ==
          BYTE_BUDDY_TOUCH_MOVE_DRAGON);
    CHECK(byte_buddy_touch_target(280U, 12U, false, false, true) ==
          BYTE_BUDDY_TOUCH_DONE_PLAYING);
    CHECK(byte_buddy_signal_touch_target(
              80U, 40U, BYTE_BUDDY_SIGNAL_LIST) ==
          BYTE_BUDDY_TOUCH_SIGNAL_ROW_0);
    CHECK(byte_buddy_signal_touch_target(
              40U, 180U, BYTE_BUDDY_SIGNAL_TRACKER) ==
          BYTE_BUDDY_TOUCH_SIGNAL_TRACK);
    CHECK(byte_buddy_signal_touch_target(
              250U, 180U, BYTE_BUDDY_SIGNAL_TRACKER) ==
          BYTE_BUDDY_TOUCH_SIGNAL_BATTLE);
    CHECK(byte_buddy_signal_touch_target(
              40U, 180U, BYTE_BUDDY_SIGNAL_BATTLE) ==
          BYTE_BUDDY_TOUCH_SIGNAL_STRIKE);

    const byte_buddy_signal_profile_t weak = byte_buddy_signal_profile(
        UINT64_C(0x00123456789abcde), -90);
    const byte_buddy_signal_profile_t strong = byte_buddy_signal_profile(
        UINT64_C(0x00123456789abcde), -35);
    CHECK(strong.strength > weak.strength);
    CHECK(strong.battle_hp > weak.battle_hp);
    CHECK(strong.reward_coins > weak.reward_coins);
    CHECK(strong.element >= BYTE_BUDDY_ELEMENT_FIRE &&
          strong.element <= BYTE_BUDDY_ELEMENT_ACID);
}

static void test_signal_hunt_battle_and_reward(void)
{
    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    CHECK(state != NULL);
    if (state == NULL) {
        return;
    }
    p4_game_instance_t instance;
    p4_audio_mixer_t mixer;
    p4_achievement_catalog_t achievements;
    CHECK(start_game(&instance, state, &mixer, &achievements));
    CHECK((p4_byte_buddy_game.optional_capabilities &
           P4_GAME_CAP_SIGNAL_SCAN) != 0U);
    tap(&instance, 70U, 180U);
    CHECK(s_signal_scan.requests == 1U);
    release_touch(&instance);
    tap(&instance, 70U, 42U);
    CHECK(s_signal_scan.requests == 2U);
    CHECK(s_signal_scan.focus_token == UINT64_C(0x00123456789abcde));
    tap(&instance, 70U, 180U);
    CHECK(s_signal_scan.requests == 3U);
    CHECK(s_signal_scan.focus_token == UINT64_C(0x00123456789abcde));
    release_touch(&instance);
    tap(&instance, 250U, 180U);
    for (unsigned strike = 0U; strike < 32U && achievements.count == 0U;
         ++strike) {
        tap(&instance, 60U, 180U);
    }
    CHECK(achievements.count == 1U);
    CHECK(strcmp(achievements.entries[0].id, "first-signal") == 0);
    p4_game_instance_stop(&instance);
    free(state);
}

static uint8_t lineage_test_channel(unsigned index)
{
    static const uint8_t channels[4] = {1U, 6U, 11U, 36U};
    return channels[index & 3U];
}

static byte_buddy_signal_genome_t lineage_test_genome(unsigned index)
{
    static const uint8_t halo_order[4] = {0U, 2U, 3U, 1U};
    static const uint8_t sigil_order[4] = {0U, 3U, 1U, 2U};
    static const uint8_t aura_order[4] = {0U, 2U, 1U, 3U};
    const uint8_t family = (uint8_t)(index & 3U);
    return (byte_buddy_signal_genome_t){
        .core = family,
        .halo = halo_order[family],
        .sigil = sigil_order[family],
        .aura = aura_order[family],
        .hue = (uint8_t)(index & 7U),
        .rarity = (uint8_t)(index & 3U),
    };
}

static void test_signal_lineage_genetics(void)
{
    CHECK(byte_buddy_signal_channel_family(0U) == UINT8_MAX);
    CHECK(byte_buddy_signal_channel_family(1U) == 0U);
    CHECK(byte_buddy_signal_channel_family(5U) == 0U);
    CHECK(byte_buddy_signal_channel_family(6U) == 1U);
    CHECK(byte_buddy_signal_channel_family(10U) == 1U);
    CHECK(byte_buddy_signal_channel_family(11U) == 2U);
    CHECK(byte_buddy_signal_channel_family(14U) == 2U);
    CHECK(byte_buddy_signal_channel_family(36U) == 3U);
    CHECK(byte_buddy_signal_channel_family(196U) == 3U);

    const uint64_t token_a = UINT64_C(0x1020304050607080);
    const uint64_t token_b = UINT64_C(0x8877665544332211);
    const uint8_t protected_flag = P4_GAME_SIGNAL_PROTECTED;
    const uint64_t contribution_a =
        byte_buddy_signal_lineage_contribution(
            token_a, 1U, protected_flag);
    const uint64_t contribution_b =
        byte_buddy_signal_lineage_contribution(token_b, 36U, 0U);
    CHECK(contribution_a != 0U && contribution_b != 0U);
    CHECK((contribution_a ^ contribution_b) ==
          (contribution_b ^ contribution_a));
    CHECK(contribution_a == byte_buddy_signal_lineage_contribution(
              token_a, 1U,
              protected_flag | P4_GAME_SIGNAL_SIMULATED));
    CHECK(contribution_a != byte_buddy_signal_lineage_contribution(
              token_a, 6U, protected_flag));
    CHECK(contribution_a != byte_buddy_signal_lineage_contribution(
              token_a, 1U, 0U));

    byte_buddy_lineage_genes_t forward = {0};
    byte_buddy_lineage_genes_t reverse = {0};
    uint64_t forward_entropy = 0U;
    uint64_t reverse_entropy = 0U;
    for (unsigned index = 0U; index < 12U; ++index) {
        const uint8_t flags = (uint8_t)(
            P4_GAME_SIGNAL_SIMULATED |
            (index < 3U ? P4_GAME_SIGNAL_PROTECTED : 0U) |
            (index == 4U ? P4_GAME_SIGNAL_HIDDEN : 0U));
        const uint64_t token = UINT64_C(0x4000000000000000) + index;
        byte_buddy_lineage_add(
            &forward, lineage_test_genome(index),
            lineage_test_channel(index), flags);
        forward_entropy ^= byte_buddy_signal_lineage_contribution(
            token, lineage_test_channel(index), flags);

        const unsigned reverse_index = 11U - index;
        const uint8_t reverse_flags = (uint8_t)(
            P4_GAME_SIGNAL_SIMULATED |
            (reverse_index < 3U ? P4_GAME_SIGNAL_PROTECTED : 0U) |
            (reverse_index == 4U ? P4_GAME_SIGNAL_HIDDEN : 0U));
        const uint64_t reverse_token =
            UINT64_C(0x4000000000000000) + reverse_index;
        byte_buddy_lineage_add(
            &reverse, lineage_test_genome(reverse_index),
            lineage_test_channel(reverse_index), reverse_flags);
        reverse_entropy ^= byte_buddy_signal_lineage_contribution(
            reverse_token, lineage_test_channel(reverse_index),
            reverse_flags);

        const byte_buddy_signal_lineage_t lineage =
            byte_buddy_signal_lineage(
                forward_entropy, (uint8_t)(index + 1U), &forward);
        const uint8_t expected_tier = index + 1U >= 12U
            ? BYTE_BUDDY_LINEAGE_MYTHIC : index + 1U >= 8U
                ? BYTE_BUDDY_LINEAGE_ASCENDED : index + 1U >= 5U
                    ? BYTE_BUDDY_LINEAGE_AURORA : index + 1U >= 3U
                        ? BYTE_BUDDY_LINEAGE_CREST
                        : BYTE_BUDDY_LINEAGE_SPARK;
        CHECK(lineage.tier == expected_tier);
    }
    CHECK(forward_entropy == reverse_entropy);
    CHECK(forward.part_mask == reverse.part_mask);
    CHECK(forward.hue_mask == reverse.hue_mask);
    CHECK(forward.rarity_mask == reverse.rarity_mask);
    CHECK(forward.channel_mask == reverse.channel_mask);
    CHECK(forward.protected_count == reverse.protected_count);
    CHECK(forward.hidden_count == reverse.hidden_count);
    CHECK(memcmp(forward.core_votes, reverse.core_votes,
                 sizeof(forward.core_votes)) == 0);
    CHECK(memcmp(forward.halo_votes, reverse.halo_votes,
                 sizeof(forward.halo_votes)) == 0);
    CHECK(memcmp(forward.sigil_votes, reverse.sigil_votes,
                 sizeof(forward.sigil_votes)) == 0);
    CHECK(memcmp(forward.aura_votes, reverse.aura_votes,
                 sizeof(forward.aura_votes)) == 0);
    CHECK(memcmp(forward.hue_votes, reverse.hue_votes,
                 sizeof(forward.hue_votes)) == 0);
    const byte_buddy_signal_lineage_t forward_lineage =
        byte_buddy_signal_lineage(forward_entropy, 12U, &forward);
    const byte_buddy_signal_lineage_t reverse_lineage =
        byte_buddy_signal_lineage(reverse_entropy, 12U, &reverse);
    CHECK(forward_lineage.tier == BYTE_BUDDY_LINEAGE_MYTHIC);
    CHECK(forward_lineage.diversity == 28U);
    CHECK(forward_lineage.channel_families == 4U);
    CHECK(forward_lineage.shielded);
    CHECK(forward_lineage.phantom);
    CHECK(forward_lineage.family == reverse_lineage.family);
    CHECK(forward_lineage.halo == reverse_lineage.halo);
    CHECK(forward_lineage.marking == reverse_lineage.marking);
    CHECK(forward_lineage.aura == reverse_lineage.aura);
    CHECK(forward_lineage.primary_hue == reverse_lineage.primary_hue);
    CHECK(forward_lineage.secondary_hue != forward_lineage.primary_hue);

    byte_buddy_lineage_genes_t repeated = {0};
    const byte_buddy_signal_genome_t repeated_genome =
        lineage_test_genome(0U);
    for (unsigned index = 0U; index < 12U; ++index) {
        byte_buddy_lineage_add(
            &repeated, repeated_genome, 1U, 0U);
    }
    const byte_buddy_signal_lineage_t low_diversity =
        byte_buddy_signal_lineage(UINT64_C(0x1234), 12U, &repeated);
    CHECK(low_diversity.diversity == 6U);
    CHECK(low_diversity.tier == BYTE_BUDDY_LINEAGE_SPARK);

    byte_buddy_lineage_genes_t weighted = {0};
    byte_buddy_signal_genome_t light = lineage_test_genome(1U);
    byte_buddy_signal_genome_t rare = lineage_test_genome(2U);
    light.rarity = 0U;
    rare.rarity = 3U;
    byte_buddy_lineage_add(&weighted, light, 6U, 0U);
    byte_buddy_lineage_add(&weighted, rare, 11U, 0U);
    const byte_buddy_signal_lineage_t weighted_lineage =
        byte_buddy_signal_lineage(0U, 2U, &weighted);
    CHECK(weighted_lineage.family == rare.core);

    const byte_buddy_battle_stats_t boosted =
        byte_buddy_lineage_battle_stats(
            (byte_buddy_battle_stats_t){
                .level = 10U, .power = 10U, .speed = 10U,
                .guard = 10U, .magic = 10U,
            }, forward_lineage, &forward);
    CHECK(boosted.level == 10U);
    CHECK(boosted.power == 14U);
    CHECK(boosted.speed == 14U);
    CHECK(boosted.guard == 13U);
    CHECK(boosted.magic == 16U);
    const byte_buddy_battle_stats_t capped =
        byte_buddy_lineage_battle_stats(
            (byte_buddy_battle_stats_t){
                .level = 99U, .power = 98U, .speed = 98U,
                .guard = 98U, .magic = 98U,
            }, forward_lineage, &forward);
    CHECK(capped.level == 99U);
    CHECK(capped.power == 99U);
    CHECK(capped.speed == 99U);
    CHECK(capped.guard == 99U);
    CHECK(capped.magic == 99U);

    const byte_buddy_lineage_battle_traits_t battle_traits =
        byte_buddy_lineage_battle_traits(forward_lineage);
    CHECK(battle_traits.strike_damage == 2U);
    CHECK(battle_traits.guard_charges == 1U);
    CHECK(battle_traits.start_time_ms == 400U);
    CHECK(battle_traits.guard_time_ms == 420U);
    const byte_buddy_lineage_battle_traits_t dormant_traits =
        byte_buddy_lineage_battle_traits(
            (byte_buddy_signal_lineage_t){0});
    CHECK(dormant_traits.strike_damage == 0U);
    CHECK(dormant_traits.guard_charges == 0U);
    CHECK(dormant_traits.start_time_ms == 0U);
    CHECK(dormant_traits.guard_time_ms == 0U);

    CHECK(byte_buddy_signal_growth_reward(1U, 1U) == 1U);
    CHECK(byte_buddy_signal_growth_reward(4U, 4U) == 3U);
    CHECK(byte_buddy_signal_growth_reward(4U, 5U) == 2U);
    CHECK(byte_buddy_signal_growth_reward(3U, 5U) == 1U);
    CHECK(byte_buddy_signal_growth_reward(4U, 13U) == 1U);
}

static void test_controller_star_catcher(void)
{
    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    CHECK(state != NULL);
    if (state == NULL) {
        return;
    }
    p4_game_instance_t instance;
    p4_audio_mixer_t mixer;
    p4_achievement_catalog_t achievements;
    CHECK(start_game(&instance, state, &mixer, &achievements));
    CHECK(buttons(&instance, P4_BUTTON_START, P4_BUTTON_START, 16U) ==
          P4_GAME_CONTINUE);
    for (unsigned frame = 0U; frame < 60U; ++frame) {
        CHECK(buttons(&instance, P4_BUTTON_RIGHT, 0U, 16U) ==
              P4_GAME_CONTINUE);
    }
    for (unsigned frame = 0U; frame < 20U; ++frame) {
        CHECK(buttons(&instance, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    }
    CHECK(buttons(&instance, P4_BUTTON_B, P4_BUTTON_B, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(buttons(&instance, P4_BUTTON_BACK, P4_BUTTON_BACK, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    free(state);
}

static void test_invalid_extended_art_fails_closed(void)
{
    uint8_t malformed_art[64] = {0};
    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    CHECK(state != NULL);
    if (state == NULL) {
        return;
    }
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_STORAGE,
        .resource_data = malformed_art,
        .resource_bytes = sizeof(malformed_art),
        .resource_format_version = 1U,
    };
    p4_game_instance_t instance = {0};
    CHECK(!p4_game_instance_start(
        &instance, &p4_byte_buddy_game, &services, state,
        p4_byte_buddy_game.state_bytes));
    free(state);
}

int main(void)
{
    test_care_achievements_and_exit();
    test_render_bounds();
    test_dragon_growth_and_traits();
    test_controller_star_catcher();
    test_signal_lineage_genetics();
    test_signal_hunt_battle_and_reward();
    test_invalid_extended_art_fails_closed();
    if (s_failures != 0) {
        fprintf(stderr, "%d Byte Buddy test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("Byte Buddy tests passed");
    return EXIT_SUCCESS;
}
