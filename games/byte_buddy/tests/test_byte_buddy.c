// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/achievements.h"
#include "p4/audio.h"
#include "p4/game.h"

#include "byte_buddy_internal.h"

#ifndef BYTE_BUDDY_TEST_ART_PATH
#error "BYTE_BUDDY_TEST_ART_PATH must name the committed Byte Buddy art bank"
#endif

extern const p4_game_descriptor_t p4_byte_buddy_game;

static int s_failures;
static uint8_t *s_test_art;
static size_t s_test_art_bytes;

enum {
    TEST_SIGNAL_WEAVE_SETTLE_MS = 300,
    TEST_SIGNAL_ATTACK_TRAVEL_MS = 220,
};

typedef struct {
    p4_game_signal_snapshot_t snapshot;
    uint32_t requests;
    uint64_t focus_token;
    bool reject_requests;
    bool reject_reads;
} fake_signal_scan_t;

static fake_signal_scan_t s_signal_scan;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static uint32_t test_read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8U |
        (uint32_t)data[2] << 16U | (uint32_t)data[3] << 24U;
}

static bool load_test_art(void)
{
    enum {
        TEST_ART_MAX_BYTES = 8 * 1024 * 1024,
    };
    FILE *const file = fopen(BYTE_BUDDY_TEST_ART_PATH, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return false;
    }
    const long file_bytes = ftell(file);
    if (file_bytes <= 0L || file_bytes > (long)TEST_ART_MAX_BYTES ||
        fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return false;
    }
    uint8_t *const data = malloc((size_t)file_bytes);
    if (data == NULL) {
        (void)fclose(file);
        return false;
    }
    const bool read_ok = fread(data, (size_t)file_bytes, 1U, file) == 1U;
    const bool close_ok = fclose(file) == 0;
    if (!read_ok || !close_ok) {
        free(data);
        return false;
    }
    s_test_art = data;
    s_test_art_bytes = (size_t)file_bytes;
    return true;
}

static bool fake_request_signal_scan(void *context, uint64_t focus_token)
{
    fake_signal_scan_t *const scan = context;
    if (scan == NULL) {
        return false;
    }
    ++scan->requests;
    scan->focus_token = focus_token;
    if (scan->reject_requests) {
        return false;
    }
    scan->snapshot = (p4_game_signal_snapshot_t){
        .generation = scan->requests,
        .status = P4_GAME_SIGNAL_READY,
        .count = 8U,
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
            {
                .token = UINT64_C(0x1020304050607080),
                .label = "EMBER ARCADE",
                .rssi_dbm = -61,
                .channel = 1U,
                .flags = P4_GAME_SIGNAL_SIMULATED,
            },
            {
                .token = UINT64_C(0x8877665544332211),
                .label = "MOON WORKSHOP",
                .rssi_dbm = -63,
                .channel = 36U,
                .flags = P4_GAME_SIGNAL_PROTECTED |
                         P4_GAME_SIGNAL_SIMULATED,
            },
            {
                .token = UINT64_C(0x7a6b5c4d3e2f1098),
                .label = "PIXEL ROOST",
                .rssi_dbm = -66,
                .channel = 44U,
                .flags = P4_GAME_SIGNAL_SIMULATED,
            },
            {
                .token = UINT64_C(0x13579bdf2468ace0),
                .label = "COMET CAFE",
                .rssi_dbm = -58,
                .channel = 6U,
                .flags = P4_GAME_SIGNAL_PROTECTED |
                         P4_GAME_SIGNAL_SIMULATED,
            },
            {
                .token = UINT64_C(0x0fedcba987654321),
                .label = "AURORA LAB",
                .rssi_dbm = -60,
                .channel = 11U,
                .flags = P4_GAME_SIGNAL_SIMULATED,
            },
            {
                .token = UINT64_C(0x55aa33cc77ee0011),
                .label = "DRAGON DEN",
                .rssi_dbm = -52,
                .channel = 149U,
                .flags = P4_GAME_SIGNAL_PROTECTED |
                         P4_GAME_SIGNAL_SIMULATED,
            },
        },
    };
    if (focus_token != 0U) {
        for (size_t index = 0U; index < scan->snapshot.count; ++index) {
            if (scan->snapshot.results[index].token == focus_token) {
                const p4_game_signal_t focused =
                    scan->snapshot.results[index];
                scan->snapshot.results[index] = scan->snapshot.results[0];
                scan->snapshot.results[0] = focused;
                scan->snapshot.results[0].rssi_dbm = -45;
                break;
            }
        }
    }
    return true;
}

static bool fake_read_signal_scan(void *context,
                                  p4_game_signal_snapshot_t *snapshot)
{
    const fake_signal_scan_t *const scan = context;
    if (scan == NULL || snapshot == NULL || scan->reject_reads) {
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
                                  P4_GAME_CAP_STORAGE |
                                  P4_GAME_CAP_SIGNAL_SCAN,
        .audio_context = mixer,
        .game_id = p4_byte_buddy_game.id,
        .play_tone = p4_audio_mixer_service_play_tone,
        .stop_audio = p4_audio_mixer_service_stop,
        .achievement_context = achievements,
        .unlock_achievement = p4_achievement_catalog_service_unlock,
        .resource_data = s_test_art,
        .resource_bytes = s_test_art_bytes,
        .resource_format_version = 1U,
        .signal_scan_context = &s_signal_scan,
        .request_signal_scan = fake_request_signal_scan,
        .read_signal_scan = fake_read_signal_scan,
    };
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(
        instance, &p4_byte_buddy_game, &services,
        state, p4_byte_buddy_game.state_bytes);
}

static void test_required_art_contract(void)
{
    enum {
        TEST_ART_HEADER_BYTES = 64,
        TEST_ART_REQUIRED_SHEETS = 29,
    };
    CHECK((p4_byte_buddy_game.required_capabilities &
           P4_GAME_CAP_STORAGE) != 0U);
    CHECK((p4_byte_buddy_game.optional_capabilities &
           P4_GAME_CAP_STORAGE) == 0U);
    CHECK(s_test_art != NULL);
    CHECK(s_test_art_bytes >= TEST_ART_HEADER_BYTES);
    if (s_test_art == NULL || s_test_art_bytes < TEST_ART_HEADER_BYTES) {
        return;
    }
    CHECK(memcmp(s_test_art, "BBDART2\0", 8U) == 0);
    CHECK(test_read_u32(s_test_art + 8U) == 2U);
    CHECK(test_read_u32(s_test_art + 12U) >= TEST_ART_REQUIRED_SHEETS);
    CHECK((size_t)test_read_u32(s_test_art + 44U) == s_test_art_bytes);
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

static p4_game_result_t hold_touch(
    p4_game_instance_t *instance,
    uint16_t x, uint16_t y, uint32_t elapsed_ms)
{
    const p4_game_input_t input = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    return p4_game_instance_update(instance, &input, elapsed_ms);
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

static void advance_idle_ms(p4_game_instance_t *instance,
                            uint32_t elapsed_ms)
{
    while (elapsed_ms != 0U) {
        const uint32_t step_ms = elapsed_ms > P4_GAME_MAX_FRAME_DELTA_MS
            ? P4_GAME_MAX_FRAME_DELTA_MS : elapsed_ms;
        CHECK(buttons(instance, 0U, 0U, step_ms) == P4_GAME_CONTINUE);
        elapsed_ms -= step_ms;
    }
}

static uint16_t latest_tone_frequency(const p4_audio_mixer_t *mixer)
{
    uint32_t newest_serial = 0U;
    uint16_t frequency_hz = 0U;
    for (size_t voice = 0U; voice < P4_GAME_AUDIO_MAX_VOICES; ++voice) {
        if (mixer->voices[voice].serial >= newest_serial) {
            newest_serial = mixer->voices[voice].serial;
            frequency_hz = mixer->voices[voice].frequency_hz;
        }
    }
    return frequency_hz;
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

    for (unsigned clean = 0U; clean < 3U; ++clean) {
        for (unsigned wait = 0U; wait < 7U; ++wait) {
            CHECK(buttons(&instance, 0U, 0U, 100U) ==
                  P4_GAME_CONTINUE);
        }
        tap(&instance, 180U, 145U);
    }
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
    CHECK(p4_game_instance_render(&instance, &surface));
    tap(&instance, 160U, 180U);
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

    int32_t cursor_chunked = start_q16;
    for (unsigned frame = 0U; frame < 10U; ++frame) {
        cursor_chunked = byte_buddy_controller_cursor_axis_q16(
            cursor_chunked, false, true, 10U, 96, 224);
    }
    const int32_t cursor_single = byte_buddy_controller_cursor_axis_q16(
        start_q16, false, true, 100U, 96, 224);
    CHECK(cursor_chunked == cursor_single);
    CHECK(byte_buddy_controller_cursor_axis_q16(
              start_q16, true, true, 100U, 96, 224) == start_q16);
    CHECK(byte_buddy_controller_cursor_axis_q16(
              INT32_C(96) * INT32_C(65536), true, false,
              100U, 96, 224) == INT32_C(96) * INT32_C(65536));
    CHECK(byte_buddy_controller_cursor_axis_q16(
              INT32_C(224) * INT32_C(65536), false, true,
              100U, 96, 224) == INT32_C(224) * INT32_C(65536));
    CHECK(byte_buddy_controller_cursor_axis_q16(
              INT32_C(100) * INT32_C(65536), true, false,
              100U, 96, 224) == INT32_C(96) * INT32_C(65536));
    CHECK(byte_buddy_controller_cursor_axis_q16(
              INT32_C(220) * INT32_C(65536), false, true,
              100U, 96, 224) == INT32_C(224) * INT32_C(65536));

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

    static const uint8_t habitat_channels[5] = {1U, 6U, 11U, 36U, 0U};
    bool habitat_seen[20] = {false};
    for (uint8_t environment = 0U; environment < 4U; ++environment) {
        const uint8_t flags = (uint8_t)(
            ((environment & 1U) != 0U ? P4_GAME_SIGNAL_PROTECTED : 0U) |
            ((environment & 2U) != 0U ? P4_GAME_SIGNAL_HIDDEN : 0U));
        for (uint8_t family = 0U; family < 5U; ++family) {
            const uint8_t habitat = byte_buddy_signal_habitat_id(
                habitat_channels[family], flags);
            CHECK(habitat < 20U);
            if (habitat < 20U) {
                CHECK(!habitat_seen[habitat]);
                habitat_seen[habitat] = true;
            }
            CHECK(byte_buddy_signal_habitat_id(
                      habitat_channels[family],
                      (uint8_t)(flags | P4_GAME_SIGNAL_SIMULATED)) ==
                  habitat);
        }
    }
    CHECK(byte_buddy_signal_form_id(0U, 1U, 0U) == 0U);
    CHECK(byte_buddy_signal_form_id(
              8191U, 0U,
              P4_GAME_SIGNAL_PROTECTED | P4_GAME_SIGNAL_HIDDEN) ==
          163839U);
    CHECK(byte_buddy_signal_form_id(UINT16_MAX, 1U, 0U) == 0U);

    static bool form_seen[163840];
    uint32_t form_count = 0U;
    for (uint16_t recipe = 0U; recipe < 8192U; ++recipe) {
        for (uint8_t environment = 0U; environment < 4U; ++environment) {
            const uint8_t flags = (uint8_t)(
                ((environment & 1U) != 0U
                    ? P4_GAME_SIGNAL_PROTECTED : 0U) |
                ((environment & 2U) != 0U
                    ? P4_GAME_SIGNAL_HIDDEN : 0U));
            for (uint8_t family = 0U; family < 5U; ++family) {
                const uint32_t form = byte_buddy_signal_form_id(
                    recipe, habitat_channels[family], flags);
                CHECK(form < 163840U);
                if (form < 163840U) {
                    CHECK(!form_seen[form]);
                    form_seen[form] = true;
                    ++form_count;
                }
            }
        }
    }
    CHECK(form_count == 163840U);

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
    /* The encounter must retain its sanitized launch snapshot. */
    ++s_signal_scan.snapshot.generation;
    s_signal_scan.snapshot.count = 0U;
    advance_idle_ms(&instance, 500U);

    tap(&instance, 60U, 180U);
    const uint32_t tones_after_first_strike = mixer.tones_started;
    for (unsigned blocked_by_cooldown = 0U;
         blocked_by_cooldown < 6U; ++blocked_by_cooldown) {
        tap(&instance, 60U, 180U);
    }
    CHECK(mixer.tones_started == tones_after_first_strike);
    CHECK(achievements.count == 0U);
    bool victory_started = false;
    unsigned successful_strikes = 1U;
    while (!victory_started && successful_strikes < 16U) {
        advance_idle_ms(&instance, 400U);
        const uint32_t tones_before_strike = mixer.tones_started;
        CHECK(touch(&instance, 60U, 180U) == P4_GAME_CONTINUE);
        const uint32_t strike_tones =
            mixer.tones_started - tones_before_strike;
        CHECK(strike_tones >= 1U);
        ++successful_strikes;
        victory_started = latest_tone_frequency(&mixer) == 988U;
        if (!victory_started) {
            release_touch(&instance);
        }
        CHECK(achievements.count == 0U);
    }
    CHECK(victory_started);
    CHECK(successful_strikes == 9U);
    CHECK(achievements.count == 0U);
    /* The 16 ms strike frame plus 883 ms is still before 900 ms. */
    advance_idle_ms(&instance, 883U);
    CHECK(achievements.count == 0U);
    advance_idle_ms(&instance, 1U);
    CHECK(achievements.count == 1U);
    CHECK(strcmp(achievements.entries[0].id, "first-signal") == 0);
    p4_game_instance_stop(&instance);
    free(state);
}

static bool achievement_present(
    const p4_achievement_catalog_t *achievements, const char *id)
{
    if (achievements == NULL || id == NULL) {
        return false;
    }
    for (size_t index = 0U; index < achievements->count; ++index) {
        if (strcmp(achievements->entries[index].id, id) == 0) {
            return true;
        }
    }
    return false;
}

static void test_care_growth_cadence(void)
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
    tap(&instance, 260U, 180U);
    const p4_game_input_t close_genome_while_touching = {
        .pressed = P4_BUTTON_B,
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = 20U, .y = 145U}},
    };
    CHECK(p4_game_instance_update(
              &instance, &close_genome_while_touching, 16U) ==
          P4_GAME_CONTINUE);
    const p4_game_input_t held_after_close = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = 20U, .y = 145U}},
    };
    CHECK(p4_game_instance_update(
              &instance, &held_after_close, 16U) == P4_GAME_CONTINUE);
    CHECK(achievements.count == 0U);
    release_touch(&instance);
    for (unsigned rapid = 0U; rapid < 104U; ++rapid) {
        tap(&instance, 160U, 80U);
    }
    CHECK(!achievement_present(&achievements, "dragon-raised"));

    for (unsigned paced = 0U; paced < 104U; ++paced) {
        for (unsigned wait = 0U; wait < 7U; ++wait) {
            CHECK(buttons(&instance, 0U, 0U, 100U) ==
                  P4_GAME_CONTINUE);
        }
        tap(&instance, 160U, 80U);
    }
    CHECK(achievement_present(&achievements, "dragon-raised"));
    p4_game_instance_stop(&instance);
    free(state);
}

static void test_signal_paging(void)
{
    CHECK(byte_buddy_signal_touch_target(
              63U, 180U, BYTE_BUDDY_SIGNAL_LIST) ==
          BYTE_BUDDY_TOUCH_SIGNAL_PREVIOUS);
    CHECK(byte_buddy_signal_touch_target(
              64U, 180U, BYTE_BUDDY_SIGNAL_LIST) ==
          BYTE_BUDDY_TOUCH_SIGNAL_SCAN);
    CHECK(byte_buddy_signal_touch_target(
              255U, 180U, BYTE_BUDDY_SIGNAL_LIST) ==
          BYTE_BUDDY_TOUCH_SIGNAL_SCAN);
    CHECK(byte_buddy_signal_touch_target(
              256U, 180U, BYTE_BUDDY_SIGNAL_LIST) ==
          BYTE_BUDDY_TOUCH_SIGNAL_NEXT);
    CHECK(byte_buddy_signal_touch_target(
              80U, 32U, BYTE_BUDDY_SIGNAL_LIST) ==
          BYTE_BUDDY_TOUCH_SIGNAL_ROW_0);
    CHECK(byte_buddy_signal_touch_target(
              80U, 156U, BYTE_BUDDY_SIGNAL_LIST) ==
          BYTE_BUDDY_TOUCH_SIGNAL_ROW_4);
    CHECK(byte_buddy_signal_touch_target(
              80U, 157U, BYTE_BUDDY_SIGNAL_LIST) ==
          BYTE_BUDDY_TOUCH_NONE);

    for (uint8_t count = 0U; count <= P4_GAME_SIGNAL_MAX_RESULTS;
         ++count) {
        const uint8_t pages = byte_buddy_signal_page_count(count);
        CHECK(pages == (count <= BYTE_BUDDY_SIGNAL_PAGE_ROWS ? 1U : 2U));
        CHECK(byte_buddy_signal_clamp_page(UINT8_MAX, count) ==
              (uint8_t)(pages - 1U));
        uint8_t seen[P4_GAME_SIGNAL_MAX_RESULTS] = {0};
        for (uint8_t page = 0U; page < pages; ++page) {
            for (uint8_t row = 0U; row < BYTE_BUDDY_SIGNAL_PAGE_ROWS;
                 ++row) {
                const uint8_t index = byte_buddy_signal_page_index(
                    page, row, count);
                if (index != UINT8_MAX) {
                    CHECK(index < count);
                    if (index < P4_GAME_SIGNAL_MAX_RESULTS) {
                        ++seen[index];
                    }
                }
            }
        }
        for (uint8_t index = 0U; index < count; ++index) {
            CHECK(seen[index] == 1U);
        }
        CHECK(byte_buddy_signal_page_index(pages, 0U, count) ==
              UINT8_MAX);
        CHECK(byte_buddy_signal_page_index(
                  0U, BYTE_BUDDY_SIGNAL_PAGE_ROWS, count) == UINT8_MAX);
    }
    CHECK(byte_buddy_signal_page_count(UINT8_MAX) == 2U);
    CHECK(byte_buddy_signal_page_index(1U, 2U, UINT8_MAX) == 7U);
    CHECK(byte_buddy_signal_page_index(1U, 3U, UINT8_MAX) == UINT8_MAX);
}

static void test_signal_paging_integration(void)
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
    tap(&instance, 70U, 180U);
    CHECK(s_signal_scan.requests == 1U);
    tap(&instance, 280U, 180U);
    tap(&instance, 80U, 119U);
    CHECK(s_signal_scan.requests == 1U);
    tap(&instance, 80U, 94U);
    CHECK(s_signal_scan.requests == 2U);
    CHECK(s_signal_scan.focus_token == UINT64_C(0x55aa33cc77ee0011));

    tap(&instance, 20U, 12U);
    tap(&instance, 280U, 180U);
    const uint64_t expected_after_clamp =
        s_signal_scan.snapshot.results[0].token;
    s_signal_scan.snapshot.count = 3U;
    ++s_signal_scan.snapshot.generation;
    release_touch(&instance);
    tap(&instance, 80U, 42U);
    CHECK(s_signal_scan.requests == 3U);
    CHECK(s_signal_scan.focus_token == expected_after_clamp);

    p4_game_instance_stop(&instance);
    free(state);
}

static void test_signal_busy_preserves_results(void)
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
    tap(&instance, 70U, 180U);
    CHECK(s_signal_scan.requests == 1U);

    s_signal_scan.reject_requests = true;
    tap(&instance, 100U, 180U);
    CHECK(s_signal_scan.requests == 2U);
    tap(&instance, 80U, 67U);
    CHECK(s_signal_scan.requests == 2U);
    tap(&instance, 80U, 180U);
    CHECK(s_signal_scan.requests == 2U);
    CHECK(buttons(&instance, 0U, P4_BUTTON_START, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(s_signal_scan.requests == 2U);
    for (unsigned frame = 0U; frame < 11U; ++frame) {
        CHECK(buttons(&instance, 0U, 0U, 100U) == P4_GAME_CONTINUE);
    }
    CHECK(s_signal_scan.requests == 2U);

    CHECK(buttons(&instance, 0U, 0U, 100U) == P4_GAME_CONTINUE);
    CHECK(s_signal_scan.requests == 3U);
    CHECK(buttons(&instance, 0U, 0U, 100U) == P4_GAME_CONTINUE);
    CHECK(s_signal_scan.requests == 3U);
    for (unsigned frame = 0U; frame < 10U; ++frame) {
        CHECK(buttons(&instance, 0U, 0U, 100U) == P4_GAME_CONTINUE);
    }
    CHECK(s_signal_scan.requests == 3U);
    s_signal_scan.reject_requests = false;
    for (unsigned frame = 0U; frame < 4U; ++frame) {
        CHECK(buttons(&instance, 0U, 0U, 100U) == P4_GAME_CONTINUE);
    }
    CHECK(s_signal_scan.requests == 3U);
    CHECK(buttons(&instance, 0U, 0U, 100U) == P4_GAME_CONTINUE);
    CHECK(s_signal_scan.requests == 4U);
    CHECK(s_signal_scan.focus_token == UINT64_C(0x3ff0000000045678));

    p4_game_instance_stop(&instance);
    free(state);
}

static void test_initial_signal_busy_backoff(void)
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
    s_signal_scan.reject_requests = true;

    tap(&instance, 70U, 180U);
    CHECK(s_signal_scan.requests == 1U);
    tap(&instance, 100U, 180U);
    CHECK(s_signal_scan.requests == 1U);
    CHECK(buttons(&instance, 0U, P4_BUTTON_START, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(s_signal_scan.requests == 1U);
    for (unsigned frame = 0U; frame < 12U; ++frame) {
        CHECK(buttons(&instance, 0U, 0U, 100U) == P4_GAME_CONTINUE);
    }
    CHECK(s_signal_scan.requests == 1U);
    tap(&instance, 100U, 180U);
    CHECK(s_signal_scan.requests == 2U);

    p4_game_instance_stop(&instance);
    free(state);
}

static uint64_t encounter_test_token(uint16_t recipe)
{
    static const uint16_t rarity_rolls[4] = {256U, 64U, 8U, 0U};
    unsigned remaining = recipe;
    const uint8_t rarity = (uint8_t)(remaining % 4U);
    remaining /= 4U;
    const uint8_t hue = (uint8_t)(remaining % 8U);
    remaining /= 8U;
    const uint8_t aura = (uint8_t)(remaining % 4U);
    remaining /= 4U;
    const uint8_t sigil = (uint8_t)(remaining % 4U);
    remaining /= 4U;
    const uint8_t halo = (uint8_t)(remaining % 4U);
    remaining /= 4U;
    const uint8_t core = (uint8_t)(remaining % 4U);
    return (uint64_t)rarity_rolls[rarity] |
        (uint64_t)hue << 18U |
        (uint64_t)core << 21U |
        (uint64_t)halo << 23U |
        (uint64_t)aura << 25U |
        (uint64_t)sigil << 27U;
}

static bool signal_encounters_equal(
    byte_buddy_signal_encounter_t left,
    byte_buddy_signal_encounter_t right)
{
    return left.attack == right.attack &&
        left.passive == right.passive &&
        left.weakness == right.weakness &&
        left.arena == right.arena &&
        left.threat == right.threat &&
        left.max_hp == right.max_hp &&
        left.damage == right.damage &&
        left.starting_ward == right.starting_ward &&
        left.attack_period_ms == right.attack_period_ms &&
        left.telegraph_ms == right.telegraph_ms &&
        left.form_id == right.form_id &&
        left.hidden == right.hidden &&
        left.protected_signal == right.protected_signal;
}

static void test_signal_encounter_derivation(void)
{
    static const uint8_t channels[5] = {0U, 1U, 6U, 11U, 36U};
    static const uint8_t arenas[5] = {
        BYTE_BUDDY_SIGNAL_ARENA_STEADY,
        BYTE_BUDDY_SIGNAL_ARENA_HEAVY,
        BYTE_BUDDY_SIGNAL_ARENA_QUICK,
        BYTE_BUDDY_SIGNAL_ARENA_ECHO,
        BYTE_BUDDY_SIGNAL_ARENA_SHIFT,
    };
    uint32_t derivation_count = 0U;
    for (uint16_t recipe = 0U; recipe < 8192U; ++recipe) {
        const uint64_t token = encounter_test_token(recipe);
        const byte_buddy_signal_genome_t genome =
            byte_buddy_signal_genome(token);
        CHECK(genome.recipe_id == recipe);
        for (size_t channel_index = 0U;
             channel_index < sizeof(channels) / sizeof(channels[0]);
             ++channel_index) {
            for (uint8_t environment = 0U; environment < 4U;
                 ++environment) {
                const uint8_t flags = (uint8_t)(
                    ((environment & 1U) != 0U
                        ? P4_GAME_SIGNAL_PROTECTED : 0U) |
                    ((environment & 2U) != 0U
                        ? P4_GAME_SIGNAL_HIDDEN : 0U));
                const byte_buddy_signal_encounter_t encounter =
                    byte_buddy_signal_encounter(
                        token, -61, channels[channel_index], flags);
                const byte_buddy_signal_encounter_t repeated =
                    byte_buddy_signal_encounter(
                        token, -61, channels[channel_index], flags);
                const byte_buddy_signal_encounter_t simulated =
                    byte_buddy_signal_encounter(
                        token, -61, channels[channel_index],
                        (uint8_t)(flags | P4_GAME_SIGNAL_SIMULATED));
                CHECK(signal_encounters_equal(encounter, repeated));
                CHECK(signal_encounters_equal(encounter, simulated));
                CHECK(encounter.attack == genome.core);
                CHECK(encounter.passive == genome.aura);
                CHECK(byte_buddy_signal_attack_for(encounter, 0U) ==
                      (byte_buddy_signal_attack_t)encounter.attack);
                const byte_buddy_signal_attack_t alternate_attack =
                    byte_buddy_signal_attack_for(encounter, 1U);
                CHECK(alternate_attack < BYTE_BUDDY_SIGNAL_ATTACK_COUNT);
                CHECK((encounter.arena == BYTE_BUDDY_SIGNAL_ARENA_SHIFT) ==
                      (alternate_attack !=
                       (byte_buddy_signal_attack_t)encounter.attack));
                CHECK(encounter.weakness == (genome.sigil < 3U
                    ? (uint8_t)(BYTE_BUDDY_ELEMENT_FIRE + genome.sigil)
                    : BYTE_BUDDY_ELEMENT_MYSTERY));
                CHECK(encounter.arena == arenas[channel_index]);
                CHECK(encounter.threat >= 1U && encounter.threat <= 5U);
                CHECK(encounter.max_hp >= 9U && encounter.max_hp <= 60U);
                CHECK(encounter.damage >= 8U && encounter.damage <= 18U);
                CHECK(encounter.starting_ward <= 3U);
                CHECK(encounter.attack_period_ms >= 1900U &&
                      encounter.attack_period_ms <= 3400U);
                CHECK(encounter.telegraph_ms >= 650U &&
                      encounter.telegraph_ms <= 1200U);
                CHECK(encounter.form_id == byte_buddy_signal_form_id(
                          recipe, channels[channel_index], flags));
                CHECK(encounter.hidden ==
                      ((flags & P4_GAME_SIGNAL_HIDDEN) != 0U));
                CHECK(encounter.protected_signal ==
                      ((flags & P4_GAME_SIGNAL_PROTECTED) != 0U));
                ++derivation_count;
            }
        }

        const byte_buddy_signal_encounter_t weak =
            byte_buddy_signal_encounter(token, -100, 6U, 0U);
        const byte_buddy_signal_encounter_t medium =
            byte_buddy_signal_encounter(token, -65, 6U, 0U);
        const byte_buddy_signal_encounter_t strong =
            byte_buddy_signal_encounter(token, -30, 6U, 0U);
        CHECK(weak.max_hp <= medium.max_hp &&
              medium.max_hp <= strong.max_hp);
        CHECK(weak.damage <= medium.damage &&
              medium.damage <= strong.damage);
        CHECK(weak.threat <= medium.threat &&
              medium.threat <= strong.threat);
        CHECK(weak.attack_period_ms >= medium.attack_period_ms &&
              medium.attack_period_ms >= strong.attack_period_ms);
        CHECK(weak.telegraph_ms >= medium.telegraph_ms &&
              medium.telegraph_ms >= strong.telegraph_ms);
        CHECK(weak.form_id == medium.form_id &&
              medium.form_id == strong.form_id);
    }
    CHECK(derivation_count == UINT32_C(163840));
}

static void test_signal_combat_math(void)
{
    CHECK(byte_buddy_dragon_ability(BYTE_BUDDY_ELEMENT_MYSTERY) ==
          BYTE_BUDDY_ABILITY_NOVA_PARRY);
    CHECK(byte_buddy_dragon_ability(BYTE_BUDDY_ELEMENT_FIRE) ==
          BYTE_BUDDY_ABILITY_FLARE_COUNTER);
    CHECK(byte_buddy_dragon_ability(BYTE_BUDDY_ELEMENT_ICE) ==
          BYTE_BUDDY_ABILITY_GLACIER_WARD);
    CHECK(byte_buddy_dragon_ability(BYTE_BUDDY_ELEMENT_ACID) ==
          BYTE_BUDDY_ABILITY_JAM_FIELD);
    CHECK(byte_buddy_dragon_ability(BYTE_BUDDY_ELEMENT_COUNT) ==
          BYTE_BUDDY_ABILITY_NOVA_PARRY);

    const byte_buddy_signal_encounter_t steady_arc = {
        .attack = BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST,
        .arena = BYTE_BUDDY_SIGNAL_ARENA_STEADY,
    };
    CHECK(byte_buddy_signal_attack_for(steady_arc, 0U) ==
          BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST);
    CHECK(byte_buddy_signal_attack_for(steady_arc, 1U) ==
          BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST);
    byte_buddy_signal_encounter_t shifting = steady_arc;
    shifting.arena = BYTE_BUDDY_SIGNAL_ARENA_SHIFT;
    CHECK(byte_buddy_signal_attack_for(shifting, 0U) ==
          BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST);
    CHECK(byte_buddy_signal_attack_for(shifting, 1U) ==
          BYTE_BUDDY_SIGNAL_ATTACK_PRISM_LANCE);
    shifting.form_id = 32U;
    CHECK(byte_buddy_signal_attack_for(shifting, 1U) ==
          BYTE_BUDDY_SIGNAL_ATTACK_THORN_SNARE);
    shifting.form_id = 64U;
    CHECK(byte_buddy_signal_attack_for(shifting, 1U) ==
          BYTE_BUDDY_SIGNAL_ATTACK_COMET_CRASH);
    shifting.attack = UINT8_MAX;
    CHECK(byte_buddy_signal_attack_for(shifting, 0U) ==
          BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST);

    const byte_buddy_signal_encounter_t siphoning_thorns = {
        .attack = BYTE_BUDDY_SIGNAL_ATTACK_THORN_SNARE,
        .passive = BYTE_BUDDY_SIGNAL_PASSIVE_SIPHON,
        .arena = BYTE_BUDDY_SIGNAL_ARENA_STEADY,
        .threat = 5U,
        .damage = 12U,
    };
    const byte_buddy_signal_defense_t open_hit =
        byte_buddy_signal_defense(
            siphoning_thorns, BYTE_BUDDY_ABILITY_NOVA_PARRY,
            false, 0U);
    CHECK(open_hit.player_damage == 12U);
    CHECK(open_hit.counter_damage == 0U);
    CHECK(open_hit.enemy_heal == 2U);
    CHECK(open_hit.ward_damage == 0U);
    CHECK(open_hit.delay_ms == 0U);
    CHECK(open_hit.status_ms == 1000U);

    const byte_buddy_signal_encounter_t echoing_comet = {
        .attack = BYTE_BUDDY_SIGNAL_ATTACK_COMET_CRASH,
        .passive = BYTE_BUDDY_SIGNAL_PASSIVE_ECHO,
        .arena = BYTE_BUDDY_SIGNAL_ARENA_ECHO,
        .threat = 5U,
        .damage = 18U,
    };
    CHECK(byte_buddy_signal_defense(
              echoing_comet, BYTE_BUDDY_ABILITY_NOVA_PARRY,
              false, 2U).player_damage == 24U);
    CHECK(byte_buddy_signal_defense(
              echoing_comet, BYTE_BUDDY_ABILITY_NOVA_PARRY,
              false, 1U).player_damage == 18U);

    const byte_buddy_signal_defense_t nova =
        byte_buddy_signal_defense(
            siphoning_thorns, BYTE_BUDDY_ABILITY_NOVA_PARRY,
            true, 0U);
    CHECK(nova.player_damage == 0U && nova.counter_damage == 3U);
    CHECK(nova.enemy_heal == 0U && nova.ward_damage == 1U);
    CHECK(nova.delay_ms == 340U && nova.status_ms == 0U);
    const byte_buddy_signal_defense_t flare =
        byte_buddy_signal_defense(
            siphoning_thorns, BYTE_BUDDY_ABILITY_FLARE_COUNTER,
            true, 0U);
    CHECK(flare.player_damage == 4U && flare.counter_damage == 4U);
    CHECK(flare.enemy_heal == 0U && flare.ward_damage == 0U);
    CHECK(flare.delay_ms == 280U && flare.status_ms == 0U);
    const byte_buddy_signal_defense_t glacier =
        byte_buddy_signal_defense(
            siphoning_thorns, BYTE_BUDDY_ABILITY_GLACIER_WARD,
            true, 0U);
    CHECK(glacier.player_damage == 0U && glacier.counter_damage == 1U);
    CHECK(glacier.enemy_heal == 0U && glacier.ward_damage == 0U);
    CHECK(glacier.delay_ms == 420U && glacier.status_ms == 0U);
    const byte_buddy_signal_defense_t jam =
        byte_buddy_signal_defense(
            siphoning_thorns, BYTE_BUDDY_ABILITY_JAM_FIELD,
            true, 0U);
    CHECK(jam.player_damage == 8U && jam.counter_damage == 1U);
    CHECK(jam.enemy_heal == 0U && jam.ward_damage == 1U);
    CHECK(jam.delay_ms == 620U && jam.status_ms == 0U);

    byte_buddy_signal_encounter_t prism = siphoning_thorns;
    prism.attack = BYTE_BUDDY_SIGNAL_ATTACK_PRISM_LANCE;
    CHECK(byte_buddy_signal_defense(
              prism, BYTE_BUDDY_ABILITY_NOVA_PARRY,
              true, 0U).player_damage == 1U);
    CHECK(byte_buddy_signal_defense(
              prism, BYTE_BUDDY_ABILITY_GLACIER_WARD,
              true, 0U).player_damage == 1U);

    const byte_buddy_battle_stats_t base_stats = {0};
    CHECK(byte_buddy_signal_player_hp(
              base_stats, 0U,
              (byte_buddy_signal_lineage_t){0}) == 48U);
    const byte_buddy_battle_stats_t trained_stats = {
        .level = 10U,
        .guard = 20U,
    };
    CHECK(byte_buddy_signal_player_hp(
              trained_stats, 2U,
              (byte_buddy_signal_lineage_t){
                  .tier = BYTE_BUDDY_LINEAGE_CREST,
              }) == 75U);
    CHECK(byte_buddy_signal_player_hp(
              (byte_buddy_battle_stats_t){
                  .level = 99U,
                  .guard = 99U,
              }, UINT8_MAX,
              (byte_buddy_signal_lineage_t){
                  .tier = UINT8_MAX,
              }) == 99U);
    uint8_t previous_hp = byte_buddy_signal_player_hp(
        base_stats, 0U, (byte_buddy_signal_lineage_t){0});
    for (uint8_t nest = 1U; nest < 4U; ++nest) {
        const uint8_t hp = byte_buddy_signal_player_hp(
            base_stats, nest, (byte_buddy_signal_lineage_t){0});
        CHECK(hp > previous_hp);
        previous_hp = hp;
    }

    CHECK(byte_buddy_signal_strike_cooldown_ms(0U) == 360U);
    CHECK(byte_buddy_signal_strike_cooldown_ms(45U) == 270U);
    CHECK(byte_buddy_signal_strike_cooldown_ms(90U) == 180U);
    CHECK(byte_buddy_signal_strike_cooldown_ms(UINT8_MAX) == 180U);
    uint16_t previous_cooldown =
        byte_buddy_signal_strike_cooldown_ms(0U);
    for (uint8_t speed = 1U; speed <= 90U; ++speed) {
        const uint16_t cooldown =
            byte_buddy_signal_strike_cooldown_ms(speed);
        CHECK(cooldown <= previous_cooldown);
        CHECK(previous_cooldown - cooldown == 2U);
        previous_cooldown = cooldown;
    }
}

static void test_signal_battle_patterns(void)
{
    unsigned pattern_counts[4][2] = {{0}};
    for (uint8_t core = 0U; core < 4U; ++core) {
        for (uint8_t halo = 0U; halo < 4U; ++halo) {
            for (uint8_t sigil = 0U; sigil < 4U; ++sigil) {
                for (uint8_t aura = 0U; aura < 4U; ++aura) {
                    for (uint8_t hue = 0U; hue < 8U; ++hue) {
                        for (uint8_t rarity = 0U; rarity < 4U;
                             ++rarity) {
                            const byte_buddy_signal_genome_t genome = {
                                .core = core,
                                .halo = halo,
                                .sigil = sigil,
                                .aura = aura,
                                .hue = hue,
                                .rarity = rarity,
                            };
                            const byte_buddy_signal_battle_pattern_t pattern =
                                byte_buddy_signal_battle_pattern(genome);
                            CHECK(pattern <=
                                  BYTE_BUDDY_BATTLE_RESONANCE_WEAVE);
                            ++pattern_counts[rarity][pattern];
                            bool seen[6] = {false};
                            for (uint8_t step = 0U; step < 6U; ++step) {
                                const uint8_t node =
                                    byte_buddy_signal_weave_node_index(
                                        genome, step);
                                CHECK(node < 6U);
                                if (node < 6U) {
                                    CHECK(!seen[node]);
                                    seen[node] = true;
                                }
                                const byte_buddy_signal_weave_node_t point =
                                    byte_buddy_signal_weave_node(
                                        genome, step);
                                CHECK(point.x < P4_GAME_SURFACE_WIDTH);
                                CHECK(point.y < 162U);
                            }
                            CHECK(byte_buddy_signal_weave_node_index(
                                      genome, 6U) ==
                                  byte_buddy_signal_weave_node_index(
                                      genome, 0U));
                        }
                    }
                }
            }
        }
    }
    for (uint8_t rarity = 0U; rarity < 4U; ++rarity) {
        CHECK(pattern_counts[rarity][BYTE_BUDDY_BATTLE_PULSE_RUSH] ==
              1024U);
        CHECK(pattern_counts[rarity][
                  BYTE_BUDDY_BATTLE_RESONANCE_WEAVE] == 1024U);
    }

    const byte_buddy_signal_weave_rules_t easiest =
        byte_buddy_signal_weave_rules(
            (byte_buddy_signal_genome_t){.rarity = 0U}, 0U, 0U);
    CHECK(easiest.required_locks == 4U);
    CHECK(easiest.hold_ms == 600U);
    CHECK(easiest.touch_radius == 17U);
    const byte_buddy_signal_weave_rules_t hardest =
        byte_buddy_signal_weave_rules(
            (byte_buddy_signal_genome_t){.rarity = 3U}, 100U, 3U);
    CHECK(hardest.required_locks == 7U);
    CHECK(hardest.hold_ms == 920U);
    CHECK(hardest.touch_radius == 23U);
    const byte_buddy_signal_weave_rules_t bounded =
        byte_buddy_signal_weave_rules(
            (byte_buddy_signal_genome_t){.rarity = UINT8_MAX},
            UINT8_MAX, UINT8_MAX);
    CHECK(bounded.required_locks == 7U);
    CHECK(bounded.hold_ms == 920U);
    CHECK(bounded.touch_radius == 23U);

    uint32_t chunked = 0U;
    for (unsigned frame = 0U; frame < 10U; ++frame) {
        chunked = byte_buddy_signal_weave_charge(
            chunked, 16U, true, 1000U);
    }
    const uint32_t single = byte_buddy_signal_weave_charge(
        0U, 160U, true, 1000U);
    CHECK(chunked == single && single == 320U);
    CHECK(byte_buddy_signal_weave_charge(
              single, 100U, false, 1000U) == 220U);
    CHECK(byte_buddy_signal_weave_charge(
              50U, 100U, false, 1000U) == 0U);
    CHECK(byte_buddy_signal_weave_charge(
              900U, 100U, true, 1000U) == 1000U);
    CHECK(byte_buddy_signal_weave_charge(
              900U, 100U, true, 0U) == 0U);
}

static void test_resonance_weave_battle(void)
{
    const uint64_t weave_token = UINT64_C(0x1020304050607080);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(weave_token);
    CHECK(byte_buddy_signal_battle_pattern(genome) ==
          BYTE_BUDDY_BATTLE_RESONANCE_WEAVE);
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(weave_token, -45);
    const byte_buddy_signal_weave_rules_t rules =
        byte_buddy_signal_weave_rules(genome, profile.strength, 0U);

    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    uint16_t *const pixels = calloc(
        P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT,
        sizeof(*pixels));
    CHECK(state != NULL && pixels != NULL);
    if (state == NULL || pixels == NULL) {
        free(pixels);
        free(state);
        return;
    }
    p4_game_instance_t instance;
    p4_audio_mixer_t mixer;
    p4_achievement_catalog_t achievements;
    CHECK(start_game(&instance, state, &mixer, &achievements));
    tap(&instance, 70U, 180U);
    tap(&instance, 80U, 94U);
    CHECK(s_signal_scan.focus_token == weave_token);
    tap(&instance, 250U, 180U);
    advance_idle_ms(&instance, 500U);
    const byte_buddy_signal_weave_node_t first =
        byte_buddy_signal_weave_node(genome, 0U);
    tap(&instance, first.x, first.y);
    CHECK(achievements.count == 0U);

    for (uint8_t step = 0U; step < rules.required_locks; ++step) {
        const byte_buddy_signal_weave_node_t node =
            byte_buddy_signal_weave_node(genome, step);
        if ((uint8_t)(step + 1U) == rules.required_locks) {
            unsigned exact_ms = 0U;
            while (latest_tone_frequency(&mixer) != 988U &&
                   exact_ms < (unsigned)rules.hold_ms * 3U) {
                CHECK(hold_touch(&instance, node.x, node.y, 1U) ==
                      P4_GAME_CONTINUE);
                ++exact_ms;
            }
            CHECK(latest_tone_frequency(&mixer) == 988U);
        } else {
            const unsigned frames =
                (unsigned)(rules.hold_ms + 49U) / 50U;
            for (unsigned frame = 0U; frame < frames; ++frame) {
                CHECK(hold_touch(&instance, node.x, node.y, 50U) ==
                      P4_GAME_CONTINUE);
            }
        }
        if (step == 0U) {
            p4_game_surface_t surface = {
                .pixels = pixels,
                .stride_pixels = P4_GAME_SURFACE_WIDTH,
                .width = P4_GAME_SURFACE_WIDTH,
                .height = P4_GAME_SURFACE_HEIGHT,
            };
            CHECK(p4_game_instance_render(&instance, &surface));
        }
        if ((uint8_t)(step + 1U) < rules.required_locks) {
            advance_idle_ms(&instance, 400U);
        }
    }
    CHECK(achievements.count == 0U);
    advance_idle_ms(&instance, 899U);
    CHECK(achievements.count == 0U);
    advance_idle_ms(&instance, 1U);
    CHECK(achievements.count == 1U);
    CHECK(strcmp(achievements.entries[0].id, "first-signal") == 0);

    p4_game_instance_stop(&instance);
    free(pixels);
    free(state);
}

static void observe_comet_timing_tone(
    const p4_audio_mixer_t *mixer, uint32_t tones_before,
    unsigned *impact_count, bool *third_windup_seen)
{
    if (mixer->tones_started == tones_before) {
        return;
    }
    const uint16_t frequency = latest_tone_frequency(mixer);
    if (frequency == 385U) {
        ++*impact_count;
    } else if (frequency == 441U && *impact_count >= 2U) {
        *third_windup_seen = true;
    }
}

static bool prepare_weave_event_order_case(
    p4_game_instance_t *instance, void *state,
    p4_audio_mixer_t *mixer,
    p4_achievement_catalog_t *achievements,
    byte_buddy_signal_genome_t genome,
    byte_buddy_signal_weave_rules_t rules)
{
    if (!start_game(instance, state, mixer, achievements)) {
        return false;
    }
    tap(instance, 70U, 180U);
    tap(instance, 80U, 94U);
    if (s_signal_scan.focus_token != UINT64_C(0x1020304050607080)) {
        return false;
    }
    tap(instance, 250U, 180U);

    unsigned impact_count = 0U;
    bool third_windup_seen = false;
    for (unsigned intro_ms = 0U; intro_ms < 500U; ++intro_ms) {
        const uint32_t tones_before = mixer->tones_started;
        CHECK(buttons(instance, 0U, 0U, 1U) == P4_GAME_CONTINUE);
        observe_comet_timing_tone(
            mixer, tones_before, &impact_count, &third_windup_seen);
    }

    for (uint8_t step = 0U; step < 4U; ++step) {
        const byte_buddy_signal_weave_node_t node =
            byte_buddy_signal_weave_node(genome, step);
        for (uint32_t held_ms = 0U; held_ms < rules.hold_ms; ++held_ms) {
            const uint32_t tones_before = mixer->tones_started;
            CHECK(hold_touch(instance, node.x, node.y, 1U) ==
                  P4_GAME_CONTINUE);
            observe_comet_timing_tone(
                mixer, tones_before, &impact_count, &third_windup_seen);
        }
        for (unsigned settle_ms = 0U;
             settle_ms < TEST_SIGNAL_WEAVE_SETTLE_MS; ++settle_ms) {
            const uint32_t tones_before = mixer->tones_started;
            CHECK(buttons(instance, 0U, 0U, 1U) == P4_GAME_CONTINUE);
            observe_comet_timing_tone(
                mixer, tones_before, &impact_count, &third_windup_seen);
        }
    }

    for (unsigned wait_ms = 0U;
         !third_windup_seen && wait_ms < 10000U; ++wait_ms) {
        const uint32_t tones_before = mixer->tones_started;
        CHECK(buttons(instance, 0U, 0U, 1U) == P4_GAME_CONTINUE);
        observe_comet_timing_tone(
            mixer, tones_before, &impact_count, &third_windup_seen);
    }
    return impact_count == 2U && third_windup_seen;
}

static void hold_weave_exact_ms(
    p4_game_instance_t *instance,
    byte_buddy_signal_weave_node_t node, uint32_t elapsed_ms)
{
    while (elapsed_ms != 0U) {
        const uint32_t step_ms = elapsed_ms > P4_GAME_MAX_FRAME_DELTA_MS
            ? P4_GAME_MAX_FRAME_DELTA_MS : elapsed_ms;
        CHECK(hold_touch(instance, node.x, node.y, step_ms) ==
              P4_GAME_CONTINUE);
        elapsed_ms -= step_ms;
    }
}

static void test_signal_weave_event_order(void)
{
    const uint64_t token = UINT64_C(0x1020304050607080);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(token);
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(token, -45);
    const byte_buddy_signal_weave_rules_t rules =
        byte_buddy_signal_weave_rules(genome, profile.strength, 0U);
    const byte_buddy_signal_encounter_t encounter =
        byte_buddy_signal_encounter(
            token, -45, 1U, P4_GAME_SIGNAL_SIMULATED);
    CHECK(rules.required_locks == 5U);
    CHECK(rules.hold_ms == 796U);
    CHECK(encounter.attack == BYTE_BUDDY_SIGNAL_ATTACK_COMET_CRASH);
    CHECK(encounter.telegraph_ms == 916U);

    for (unsigned exact_tie = 0U; exact_tie < 2U; ++exact_tie) {
        void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
        CHECK(state != NULL);
        if (state == NULL) {
            continue;
        }
        p4_game_instance_t instance;
        p4_audio_mixer_t mixer;
        p4_achievement_catalog_t achievements;
        const bool ready = prepare_weave_event_order_case(
            &instance, state, &mixer, &achievements, genome, rules);
        CHECK(ready);
        if (ready) {
            const byte_buddy_signal_weave_node_t final_node =
                byte_buddy_signal_weave_node(genome, 4U);
            const uint32_t impact_offset_ms =
                (uint32_t)encounter.telegraph_ms +
                TEST_SIGNAL_ATTACK_TRAVEL_MS;
            const uint32_t capture_before_impact_ms = exact_tie != 0U
                ? 90U : 10U;
            const uint32_t idle_before_charge_ms =
                impact_offset_ms - rules.hold_ms +
                capture_before_impact_ms - 90U;
            advance_idle_ms(&instance, idle_before_charge_ms);
            hold_weave_exact_ms(
                &instance, final_node,
                (uint32_t)rules.hold_ms - capture_before_impact_ms);
            CHECK(hold_touch(
                      &instance, final_node.x, final_node.y, 100U) ==
                  P4_GAME_CONTINUE);

            if (exact_tie == 0U) {
                /* Capture at +10 ms must beat the lethal impact at +90 ms. */
                CHECK(latest_tone_frequency(&mixer) == 988U);
                CHECK(!achievement_present(&achievements, "first-signal"));
                advance_idle_ms(&instance, 809U);
                CHECK(!achievement_present(&achievements, "first-signal"));
                advance_idle_ms(&instance, 1U);
                CHECK(achievement_present(&achievements, "first-signal"));
            } else {
                /* At the same timestamp, the impact resolves first. */
                CHECK(latest_tone_frequency(&mixer) == 196U);
                advance_idle_ms(&instance, 1000U);
                CHECK(!achievement_present(&achievements, "first-signal"));
            }
        }
        p4_game_instance_stop(&instance);
        free(state);
    }
}

static void test_signal_loss_and_retreat(void)
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
    tap(&instance, 70U, 180U);
    tap(&instance, 80U, 42U);
    tap(&instance, 250U, 180U);
    advance_idle_ms(&instance, 14000U);
    CHECK(!achievement_present(&achievements, "first-signal"));

    tap(&instance, 250U, 180U);
    CHECK(buttons(&instance, 0U, P4_BUTTON_B, 16U) ==
          P4_GAME_CONTINUE);
    advance_idle_ms(&instance, 519U);
    CHECK(!achievement_present(&achievements, "first-signal"));
    advance_idle_ms(&instance, 1U);
    CHECK(!achievement_present(&achievements, "first-signal"));

    p4_game_instance_stop(&instance);
    free(state);
}

static void test_signal_guard_exact_expiry_boundary(void)
{
    const uint64_t token = UINT64_C(0x00123456789abcde);
    const byte_buddy_signal_encounter_t encounter =
        byte_buddy_signal_encounter(
            token, -45, 6U,
            P4_GAME_SIGNAL_PROTECTED | P4_GAME_SIGNAL_SIMULATED);
    CHECK(encounter.attack == BYTE_BUDDY_SIGNAL_ATTACK_ARC_BURST);

    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    CHECK(state != NULL);
    if (state == NULL) {
        return;
    }
    p4_game_instance_t instance;
    p4_audio_mixer_t mixer;
    p4_achievement_catalog_t achievements;
    CHECK(start_game(&instance, state, &mixer, &achievements));
    tap(&instance, 70U, 180U);
    tap(&instance, 80U, 42U);
    tap(&instance, 250U, 180U);

    advance_idle_ms(&instance, 434U);
    const uint32_t tones_before_windup = mixer.tones_started;
    unsigned open_ms = 0U;
    while (mixer.tones_started == tones_before_windup &&
           open_ms < 1400U) {
        advance_idle_ms(&instance, 1U);
        ++open_ms;
    }
    CHECK(open_ms > 0U && open_ms < 1400U);
    CHECK(mixer.tones_started > tones_before_windup);

    advance_idle_ms(&instance, encounter.telegraph_ms);
    advance_idle_ms(&instance, 184U);
    CHECK(buttons(&instance, 0U, P4_BUTTON_START, 16U) ==
          P4_GAME_CONTINUE);
    advance_idle_ms(&instance, 100U);

    bool victory_started = false;
    unsigned successful_strikes = 0U;
    while (!victory_started && successful_strikes < 16U) {
        const uint32_t tones_before_strike = mixer.tones_started;
        CHECK(touch(&instance, 60U, 180U) == P4_GAME_CONTINUE);
        CHECK(mixer.tones_started > tones_before_strike);
        ++successful_strikes;
        victory_started = latest_tone_frequency(&mixer) == 988U;
        if (!victory_started) {
            release_touch(&instance);
            advance_idle_ms(&instance, 400U);
        }
        CHECK(!achievement_present(&achievements, "first-signal"));
    }
    CHECK(victory_started);
    CHECK(successful_strikes == 7U);
    CHECK(!achievement_present(&achievements, "first-signal"));
    /* The 16 ms guarded strike frame counts toward the 900 ms result. */
    advance_idle_ms(&instance, 883U);
    CHECK(!achievement_present(&achievements, "first-signal"));
    advance_idle_ms(&instance, 1U);
    CHECK(achievement_present(&achievements, "first-signal"));

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
    CHECK(forward_lineage.part_diversity == 16U);
    CHECK(forward_lineage.hue_diversity == 8U);
    CHECK(forward_lineage.diversity == 28U);
    CHECK(forward_lineage.channel_families == 4U);
    CHECK(forward_lineage.shielded);
    CHECK(forward_lineage.phantom);
    CHECK((forward_lineage.adaptations &
           BYTE_BUDDY_ADAPTATION_SHIELD) != 0U);
    CHECK((forward_lineage.adaptations &
           BYTE_BUDDY_ADAPTATION_PHANTOM) != 0U);
    CHECK((forward_lineage.adaptations &
           BYTE_BUDDY_ADAPTATION_WIDEBAND) != 0U);
    CHECK((forward_lineage.adaptations &
           BYTE_BUDDY_ADAPTATION_PRISMATIC) != 0U);
    CHECK((forward_lineage.adaptations &
           BYTE_BUDDY_ADAPTATION_CHIMERA) != 0U);
    CHECK(forward_lineage.resonance == BYTE_BUDDY_RESONANCE_NONE);
    CHECK(forward_lineage.family == reverse_lineage.family);
    CHECK(forward_lineage.halo == reverse_lineage.halo);
    CHECK(forward_lineage.marking == reverse_lineage.marking);
    CHECK(forward_lineage.aura == reverse_lineage.aura);
    CHECK(forward_lineage.primary_hue == reverse_lineage.primary_hue);
    CHECK(forward_lineage.secondary_hue != forward_lineage.primary_hue);

    const byte_buddy_signal_lineage_t nova =
        byte_buddy_signal_lineage(forward_entropy, 16U, &forward);
    const byte_buddy_signal_lineage_t galaxy =
        byte_buddy_signal_lineage(forward_entropy, 24U, &forward);
    const byte_buddy_signal_lineage_t eternal =
        byte_buddy_signal_lineage(forward_entropy, 32U, &forward);
    CHECK(nova.resonance == BYTE_BUDDY_RESONANCE_NOVA);
    CHECK(galaxy.resonance == BYTE_BUDDY_RESONANCE_GALAXY);
    CHECK(eternal.resonance == BYTE_BUDDY_RESONANCE_ETERNAL);
    CHECK(byte_buddy_signal_lineage(
              forward_entropy, 15U, &forward).resonance ==
          BYTE_BUDDY_RESONANCE_NONE);
    CHECK(byte_buddy_signal_lineage(
              forward_entropy, 23U, &forward).resonance ==
          BYTE_BUDDY_RESONANCE_NOVA);
    CHECK(byte_buddy_signal_lineage(
              forward_entropy, 31U, &forward).resonance ==
          BYTE_BUDDY_RESONANCE_GALAXY);

    byte_buddy_lineage_genes_t boundary = {
        .part_mask = UINT16_C(0xffff),
        .hue_mask = UINT8_C(0x0f),
        .rarity_mask = UINT8_C(0x07),
    };
    CHECK(byte_buddy_signal_lineage(
              0U, 16U, &boundary).diversity == 23U);
    CHECK(byte_buddy_signal_lineage(
              0U, 16U, &boundary).resonance ==
          BYTE_BUDDY_RESONANCE_NONE);
    boundary.hue_mask = UINT8_C(0x1f);
    CHECK(byte_buddy_signal_lineage(
              0U, 16U, &boundary).resonance ==
          BYTE_BUDDY_RESONANCE_NOVA);
    boundary.hue_mask = UINT8_C(0x3f);
    CHECK(byte_buddy_signal_lineage(
              0U, 24U, &boundary).diversity == 25U);
    CHECK(byte_buddy_signal_lineage(
              0U, 24U, &boundary).resonance ==
          BYTE_BUDDY_RESONANCE_NOVA);
    boundary.hue_mask = UINT8_C(0x7f);
    CHECK(byte_buddy_signal_lineage(
              0U, 24U, &boundary).resonance ==
          BYTE_BUDDY_RESONANCE_GALAXY);
    CHECK(byte_buddy_signal_lineage(
              0U, 32U, &boundary).resonance ==
          BYTE_BUDDY_RESONANCE_GALAXY);
    boundary.hue_mask = UINT8_C(0xff);
    CHECK(byte_buddy_signal_lineage(
              0U, 32U, &boundary).resonance ==
          BYTE_BUDDY_RESONANCE_ETERNAL);
    boundary.hue_mask = UINT8_C(0x7f);
    byte_buddy_lineage_add(
        &boundary,
        (byte_buddy_signal_genome_t){
            .core = 0U, .halo = 0U, .sigil = 0U, .aura = 0U,
            .hue = 7U, .rarity = 0U,
        },
        1U, 0U);
    CHECK(byte_buddy_signal_lineage(
              0U, 33U, &boundary).diversity == 27U);
    CHECK(byte_buddy_signal_lineage(
              0U, 33U, &boundary).resonance ==
          BYTE_BUDDY_RESONANCE_ETERNAL);
    CHECK(BYTE_BUDDY_SIGNAL_MAX_CONSUMED == 48U);
    CHECK(byte_buddy_signal_collection_has_room(32U));
    CHECK(byte_buddy_signal_collection_has_room(47U));
    CHECK(!byte_buddy_signal_collection_has_room(48U));

    byte_buddy_lineage_genes_t adaptation = {.protected_count = 2U};
    CHECK(byte_buddy_signal_lineage(
              0U, 1U, &adaptation).adaptations == 0U);
    adaptation.protected_count = 3U;
    const byte_buddy_signal_lineage_t shield_lineage =
        byte_buddy_signal_lineage(0U, 1U, &adaptation);
    CHECK(shield_lineage.adaptations == BYTE_BUDDY_ADAPTATION_SHIELD);
    CHECK(shield_lineage.shielded);
    adaptation = (byte_buddy_lineage_genes_t){0};
    CHECK(byte_buddy_signal_lineage(
              0U, 1U, &adaptation).adaptations == 0U);
    adaptation.hidden_count = 1U;
    const byte_buddy_signal_lineage_t phantom_lineage =
        byte_buddy_signal_lineage(0U, 1U, &adaptation);
    CHECK(phantom_lineage.adaptations == BYTE_BUDDY_ADAPTATION_PHANTOM);
    CHECK(phantom_lineage.phantom);
    adaptation = (byte_buddy_lineage_genes_t){.channel_mask = 0x07U};
    CHECK(byte_buddy_signal_lineage(
              0U, 1U, &adaptation).adaptations == 0U);
    adaptation.channel_mask = UINT8_C(0x0f);
    CHECK(byte_buddy_signal_lineage(
              0U, 1U, &adaptation).adaptations ==
          BYTE_BUDDY_ADAPTATION_WIDEBAND);
    adaptation = (byte_buddy_lineage_genes_t){.hue_mask = 0x1fU};
    CHECK(byte_buddy_signal_lineage(
              0U, 1U, &adaptation).adaptations == 0U);
    adaptation.hue_mask = UINT8_C(0x3f);
    CHECK(byte_buddy_signal_lineage(
              0U, 1U, &adaptation).adaptations ==
          BYTE_BUDDY_ADAPTATION_PRISMATIC);
    adaptation = (byte_buddy_lineage_genes_t){
        .part_mask = UINT16_C(0x1fff),
    };
    CHECK(byte_buddy_signal_lineage(
              0U, 1U, &adaptation).adaptations == 0U);
    adaptation.part_mask = UINT16_C(0x3fff);
    CHECK(byte_buddy_signal_lineage(
              0U, 1U, &adaptation).adaptations ==
          BYTE_BUDDY_ADAPTATION_CHIMERA);

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
    CHECK(byte_buddy_signal_lineage(
              UINT64_C(0x1234), 32U, &repeated).resonance ==
          BYTE_BUDDY_RESONANCE_NONE);
    CHECK(byte_buddy_signal_lineage(
              UINT64_C(0x1234), 48U, &repeated).resonance ==
          BYTE_BUDDY_RESONANCE_ETERNAL);

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

static void move_weave_cursor_axis(
    p4_game_instance_t *instance, int *position, int target,
    uint32_t negative_button, uint32_t positive_button)
{
    while (*position != target) {
        const int distance = target - *position;
        const uint32_t direction = distance < 0
            ? negative_button : positive_button;
        CHECK(buttons(instance, direction, P4_BUTTON_START, 16U) ==
              P4_GAME_CONTINUE);
        const int step = distance < 0 ? -2 : 2;
        if ((step < 0 && *position + step < target) ||
            (step > 0 && *position + step > target)) {
            *position = target;
        } else {
            *position += step;
        }
        if ((*position - target == 1) || (*position - target == -1)) {
            break;
        }
    }
}

static void test_controller_signal_hunt(void)
{
    const uint64_t weave_token = UINT64_C(0x1020304050607080);
    const byte_buddy_signal_genome_t genome =
        byte_buddy_signal_genome(weave_token);
    CHECK(byte_buddy_signal_battle_pattern(genome) ==
          BYTE_BUDDY_BATTLE_RESONANCE_WEAVE);
    const byte_buddy_signal_profile_t profile =
        byte_buddy_signal_profile(weave_token, -45);
    const byte_buddy_signal_weave_rules_t rules =
        byte_buddy_signal_weave_rules(genome, profile.strength, 0U);

    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    CHECK(state != NULL);
    if (state == NULL) {
        return;
    }
    p4_game_instance_t instance;
    p4_audio_mixer_t mixer;
    p4_achievement_catalog_t achievements;
    CHECK(start_game(&instance, state, &mixer, &achievements));

    CHECK(buttons(&instance, 0U, P4_BUTTON_LEFT, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_A, 16U) == P4_GAME_CONTINUE);
    CHECK(achievement_present(&achievements, "first-care"));
    CHECK(buttons(&instance, 0U, P4_BUTTON_B, 16U) == P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_B, 16U) == P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_DOWN, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_RIGHT, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_START, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_A, 16U) == P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_LEFT, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_A, 16U) == P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_B, 16U) == P4_GAME_CONTINUE);

    CHECK(buttons(&instance, 0U, P4_BUTTON_UP, 16U) == P4_GAME_CONTINUE);
    CHECK(s_signal_scan.requests == 1U);
    CHECK(buttons(&instance, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_RIGHT, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_LEFT, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_START, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(s_signal_scan.requests == 2U);
    CHECK(buttons(&instance, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_DOWN, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_DOWN, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, P4_BUTTON_A, 16U) == P4_GAME_CONTINUE);
    CHECK(buttons(&instance, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(s_signal_scan.focus_token == weave_token);
    CHECK(buttons(&instance, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    tap(&instance, 250U, 180U);
    advance_idle_ms(&instance, 500U);

    byte_buddy_signal_weave_node_t cursor =
        byte_buddy_signal_weave_node(genome, 0U);
    int cursor_x = cursor.x;
    int cursor_y = cursor.y;
    for (uint8_t step = 0U; step < rules.required_locks; ++step) {
        const byte_buddy_signal_weave_node_t node =
            byte_buddy_signal_weave_node(genome, step);
        move_weave_cursor_axis(
            &instance, &cursor_x, node.x,
            P4_BUTTON_LEFT, P4_BUTTON_RIGHT);
        move_weave_cursor_axis(
            &instance, &cursor_y, node.y,
            P4_BUTTON_UP, P4_BUTTON_DOWN);
        if ((uint8_t)(step + 1U) == rules.required_locks) {
            unsigned exact_ms = 0U;
            while (latest_tone_frequency(&mixer) != 988U &&
                   exact_ms < (unsigned)rules.hold_ms * 3U) {
                CHECK(buttons(&instance, P4_BUTTON_A,
                              P4_BUTTON_START, 1U) ==
                      P4_GAME_CONTINUE);
                ++exact_ms;
            }
            CHECK(latest_tone_frequency(&mixer) == 988U);
        } else {
            const unsigned hold_frames =
                (unsigned)(rules.hold_ms + 49U) / 50U;
            for (unsigned frame = 0U; frame < hold_frames; ++frame) {
                CHECK(buttons(&instance, P4_BUTTON_A,
                              P4_BUTTON_START, 50U) ==
                      P4_GAME_CONTINUE);
            }
        }
        if ((uint8_t)(step + 1U) < rules.required_locks) {
            for (unsigned settle = 0U; settle < 4U; ++settle) {
                CHECK(buttons(&instance, 0U,
                              P4_BUTTON_START, 100U) ==
                      P4_GAME_CONTINUE);
            }
        }
    }
    CHECK(!achievement_present(&achievements, "first-signal"));
    advance_idle_ms(&instance, 899U);
    CHECK(!achievement_present(&achievements, "first-signal"));
    advance_idle_ms(&instance, 1U);
    CHECK(achievement_present(&achievements, "first-signal"));
    CHECK(buttons(&instance, 0U, P4_BUTTON_BACK, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);

    p4_game_instance_stop(&instance);
    free(state);
}

static void test_missing_extended_art_fails_closed(void)
{
    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    CHECK(state != NULL);
    if (state == NULL) {
        return;
    }
    const p4_game_services_t no_storage = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    CHECK(!p4_game_instance_start(
        &instance, &p4_byte_buddy_game, &no_storage, state,
        p4_byte_buddy_game.state_bytes));

    const p4_game_services_t missing_resource = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_STORAGE,
        .resource_format_version = 1U,
    };
    instance = (p4_game_instance_t){0};
    CHECK(!p4_game_instance_start(
        &instance, &p4_byte_buddy_game, &missing_resource, state,
        p4_byte_buddy_game.state_bytes));
    free(state);
}

static void test_malformed_extended_art_fails_closed(void)
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

    const p4_game_services_t truncated_resource = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_STORAGE,
        .resource_data = s_test_art,
        .resource_bytes = s_test_art_bytes - 1U,
        .resource_format_version = 1U,
    };
    instance = (p4_game_instance_t){0};
    CHECK(!p4_game_instance_start(
        &instance, &p4_byte_buddy_game, &truncated_resource, state,
        p4_byte_buddy_game.state_bytes));

    const p4_game_services_t wrong_resource_version = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_STORAGE,
        .resource_data = s_test_art,
        .resource_bytes = s_test_art_bytes,
        .resource_format_version = 2U,
    };
    instance = (p4_game_instance_t){0};
    CHECK(!p4_game_instance_start(
        &instance, &p4_byte_buddy_game, &wrong_resource_version, state,
        p4_byte_buddy_game.state_bytes));
    free(state);
}

int main(void)
{
    if (!load_test_art()) {
        fprintf(stderr, "Byte Buddy test art load failed: %s\n",
                BYTE_BUDDY_TEST_ART_PATH);
        return EXIT_FAILURE;
    }
    test_required_art_contract();
    test_care_achievements_and_exit();
    test_care_growth_cadence();
    test_render_bounds();
    test_dragon_growth_and_traits();
    test_controller_star_catcher();
    test_controller_signal_hunt();
    test_signal_lineage_genetics();
    test_signal_paging();
    test_signal_paging_integration();
    test_signal_busy_preserves_results();
    test_initial_signal_busy_backoff();
    test_signal_encounter_derivation();
    test_signal_combat_math();
    test_signal_battle_patterns();
    test_resonance_weave_battle();
    test_signal_weave_event_order();
    test_signal_loss_and_retreat();
    test_signal_guard_exact_expiry_boundary();
    test_signal_hunt_battle_and_reward();
    test_missing_extended_art_fails_closed();
    test_malformed_extended_art_fails_closed();
    free(s_test_art);
    s_test_art = NULL;
    s_test_art_bytes = 0U;
    if (s_failures != 0) {
        fprintf(stderr, "%d Byte Buddy test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("Byte Buddy tests passed");
    return EXIT_SUCCESS;
}
