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
                .flags = P4_GAME_SIGNAL_PROTECTED,
            },
            {
                .token = UINT64_C(0x3ff0000000045678),
                .label = "HIDDEN SIGNAL",
                .rssi_dbm = -68,
                .channel = 11U,
                .flags = P4_GAME_SIGNAL_HIDDEN,
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
    CHECK(byte_buddy_stage_for_interactions(23U) == BYTE_BUDDY_STAGE_BABY);
    CHECK(byte_buddy_stage_for_interactions(24U) == BYTE_BUDDY_STAGE_WINGED);
    CHECK(byte_buddy_stage_for_interactions(47U) == BYTE_BUDDY_STAGE_WINGED);
    CHECK(byte_buddy_stage_for_interactions(48U) == BYTE_BUDDY_STAGE_FLYING);
    CHECK(byte_buddy_stage_for_interactions(79U) == BYTE_BUDDY_STAGE_FLYING);
    CHECK(byte_buddy_stage_for_interactions(80U) ==
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

    int16_t slide_position = 160;
    int16_t slide_velocity = 0;
    for (unsigned frame = 0U; frame < 20U; ++frame) {
        slide_position = byte_buddy_slide_position(
            slide_position, 296, &slide_velocity);
    }
    CHECK(slide_position > 240 && slide_position <= 296);
    for (unsigned frame = 0U; frame < 40U; ++frame) {
        slide_position = byte_buddy_slide_position(
            slide_position, 24, &slide_velocity);
    }
    CHECK(slide_position >= 24 && slide_position < 100);

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

    CHECK(byte_buddy_level_for_interactions(0U) == 1U);
    CHECK(byte_buddy_level_for_interactions(7U) == 1U);
    CHECK(byte_buddy_level_for_interactions(8U) == 2U);
    CHECK(byte_buddy_level_for_interactions(UINT16_MAX) == 99U);
    const byte_buddy_battle_stats_t stats = byte_buddy_battle_stats(
        8U, 6U, 3U, 3U, 4U, 1U, 2U, 1U, 2U);
    CHECK(stats.level == 4U);
    CHECK(stats.power == 11U);
    CHECK(stats.speed == 13U);
    CHECK(stats.guard == 9U);
    CHECK(stats.magic == 12U);

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
    tap(&instance, 70U, 180U);
    CHECK(s_signal_scan.requests == 2U);
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
    test_signal_hunt_battle_and_reward();
    test_invalid_extended_art_fails_closed();
    if (s_failures != 0) {
        fprintf(stderr, "%d Byte Buddy test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("Byte Buddy tests passed");
    return EXIT_SUCCESS;
}
