// SPDX-License-Identifier: MIT

#include "p4/audio.h"
#include "p4/achievements.h"
#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

typedef struct {
    uint32_t starts;
    uint32_t updates;
    uint32_t stops;
} fixture_state_t;

static bool fixture_start(p4_game_context_t *context)
{
    fixture_state_t *const state = context->state;
    ++state->starts;
    return true;
}

static p4_game_result_t fixture_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    fixture_state_t *const state = context->state;
    ++state->updates;
    CHECK(elapsed_ms <= P4_GAME_MAX_FRAME_DELTA_MS);
    return (input->pressed & P4_BUTTON_A) != 0U
        ? P4_GAME_EXIT_TO_LAUNCHER : P4_GAME_CONTINUE;
}

static bool fixture_render(p4_game_context_t *context,
                           p4_game_surface_t *surface)
{
    const fixture_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x1234));
    p4_draw_pixel(surface, (int)state->updates, 1, UINT16_C(0xabcd));
    return true;
}

static void fixture_stop(p4_game_context_t *context)
{
    fixture_state_t *const state = context->state;
    ++state->stops;
}

static const p4_game_descriptor_t s_fixture_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = 100U,
    .id = "org.p4.fixture",
    .title = "FIXTURE",
    .subtitle = "HOST TEST",
    .accent_rgb565 = UINT16_C(0x07e0),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM,
    .state_bytes = sizeof(fixture_state_t),
    .start = fixture_start,
    .update = fixture_update,
    .render = fixture_render,
    .stop = fixture_stop,
};

static p4_physical_touch_t physical(unsigned logical_x, unsigned logical_y)
{
    const p4_physical_touch_t point = {
        .x = (uint16_t)(P4_INPUT_VIEWPORT_LEFT +
                        ((2U * logical_x + 1U) *
                         P4_INPUT_VIEWPORT_WIDTH) /
                            (2U * P4_GAME_SURFACE_WIDTH)),
        .y = (uint16_t)(P4_INPUT_VIEWPORT_TOP +
                        ((2U * logical_y + 1U) *
                         P4_INPUT_VIEWPORT_HEIGHT) /
                            (2U * P4_GAME_SURFACE_HEIGHT)),
    };
    return point;
}

static void test_input_mapper(void)
{
    p4_game_point_t logical;
    CHECK(p4_game_map_physical_touch(P4_INPUT_VIEWPORT_LEFT,
                                     P4_INPUT_VIEWPORT_TOP, &logical));
    CHECK(logical.x == 0U && logical.y == 0U);
    CHECK(p4_game_map_physical_touch(
        P4_INPUT_VIEWPORT_LEFT + P4_INPUT_VIEWPORT_WIDTH - 1U,
        P4_INPUT_VIEWPORT_TOP + P4_INPUT_VIEWPORT_HEIGHT - 1U, &logical));
    CHECK(logical.x == 319U && logical.y == 199U);
    CHECK(!p4_game_map_physical_touch(P4_INPUT_VIEWPORT_LEFT - 1U,
                                      P4_INPUT_VIEWPORT_TOP, &logical));
    CHECK(!p4_game_map_physical_touch(
        P4_INPUT_VIEWPORT_LEFT + P4_INPUT_VIEWPORT_WIDTH,
        P4_INPUT_VIEWPORT_TOP, &logical));
    CHECK(!p4_game_map_physical_touch(
        P4_INPUT_VIEWPORT_LEFT,
        P4_INPUT_VIEWPORT_TOP + P4_INPUT_VIEWPORT_HEIGHT, &logical));

    p4_game_input_mapper_t mapper;
    p4_game_input_mapper_init(&mapper);
    p4_game_input_t input;
    p4_physical_touch_t touches[2] = {
        physical(45U, 145U),
        physical(286U, 158U),
    };
    p4_game_input_mapper_update(
        &mapper, true, touches, 2U, P4_BUTTON_B, &input);
    CHECK(input.touch_valid);
    CHECK(input.touch_count == 2U);
    CHECK((input.held & P4_BUTTON_UP) != 0U);
    CHECK((input.held & P4_BUTTON_A) != 0U);
    CHECK((input.held & P4_BUTTON_B) != 0U);
    CHECK(input.pressed == input.held);
    CHECK(input.released == 0U);

    p4_game_input_mapper_update(&mapper, false, NULL, 0U, 0U, &input);
    CHECK(!input.touch_valid);
    CHECK(input.touch_count == 0U);
    CHECK(input.held == 0U);
    CHECK((input.released & (P4_BUTTON_UP | P4_BUTTON_A | P4_BUTTON_B)) ==
          (P4_BUTTON_UP | P4_BUTTON_A | P4_BUTTON_B));

    p4_game_input_mapper_update(
        &mapper, true, NULL, 0U, UINT32_C(0xffffffff), &input);
    CHECK(input.held == P4_BUTTON_MASK);
}

static void test_draw_bounds(void)
{
    enum {
        GUARD = 23,
        STRIDE = P4_GAME_SURFACE_WIDTH + 9,
        WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
        TOTAL = GUARD + WORDS + GUARD,
    };
    uint16_t *const allocation = calloc(TOTAL, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation == NULL) {
        return;
    }
    for (size_t i = 0U; i < TOTAL; ++i) {
        allocation[i] = UINT16_C(0xa55a);
    }
    uint16_t *const pixels = allocation + GUARD;
    p4_game_surface_t surface = {
        .pixels = pixels,
        .stride_pixels = STRIDE,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    CHECK(p4_surface_valid(&surface));
    p4_draw_clear(&surface, UINT16_C(0x1111));
    p4_draw_fill_rect(&surface, -20, -20, 40, 40, UINT16_C(0x2222));
    p4_draw_fill_circle(&surface, 319, 199, 20, UINT16_C(0x3333));
    p4_draw_text(&surface, 4, 4, "P4 GAME API 1", UINT16_C(0xffff), 1U, 13U);
    const uint16_t sprite[] = {
        UINT16_C(1), UINT16_C(2),
        UINT16_C(3), UINT16_C(4),
    };
    p4_draw_sprite_rgb565(&surface, 100, 100, sprite, 2U, 2U, 2U,
                          true, UINT16_C(2));
    CHECK(pixels[100U * STRIDE + 100U] == UINT16_C(1));
    CHECK(pixels[100U * STRIDE + 101U] == UINT16_C(0x1111));
    CHECK(pixels[101U * STRIDE + 100U] == UINT16_C(3));
    for (size_t i = 0U; i < GUARD; ++i) {
        CHECK(allocation[i] == UINT16_C(0xa55a));
        CHECK(allocation[GUARD + WORDS + i] == UINT16_C(0xa55a));
    }
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_GAME_SURFACE_WIDTH; column < STRIDE; ++column) {
            CHECK(pixels[row * STRIDE + column] == UINT16_C(0xa55a));
        }
    }
    free(allocation);
}

static void test_audio_mixer(void)
{
    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    const p4_tone_t tone = {
        .frequency_hz = 440U,
        .duration_ms = 10U,
        .volume_step = 5U,
        .waveform = P4_WAVE_TRIANGLE,
    };
    CHECK(p4_audio_mixer_play_tone(&mixer, &tone));
    int16_t pcm[256U * 2U];
    CHECK(p4_audio_mixer_render(&mixer, pcm, 256U));
    bool nonzero = false;
    for (size_t frame = 0U; frame < 256U; ++frame) {
        CHECK(pcm[frame * 2U] == pcm[frame * 2U + 1U]);
        nonzero = nonzero || pcm[frame * 2U] != 0;
    }
    CHECK(nonzero);
    p4_audio_mixer_stats_t stats;
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.tones_started == 1U);
    CHECK(stats.frames_rendered == 256U);
    CHECK(stats.active_voices == 0U);
    CHECK(!p4_audio_mixer_render(&mixer, pcm, 257U));
    p4_tone_t invalid = tone;
    invalid.frequency_hz = 0U;
    CHECK(!p4_audio_mixer_play_tone(&mixer, &invalid));

    for (size_t i = 0U; i < P4_GAME_AUDIO_MAX_VOICES + 1U; ++i) {
        CHECK(p4_audio_mixer_play_tone(&mixer, &tone));
    }
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.voices_replaced == 1U);
    p4_audio_mixer_stop_all(&mixer);
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.active_voices == 0U);
}

static void test_audio_stream(void)
{
    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    int16_t source[P4_GAME_MAX_AUDIO_STREAM_FRAMES * 2U];
    int16_t output[P4_GAME_MAX_AUDIO_STREAM_FRAMES * 2U];
    for (size_t frame = 0U;
         frame < P4_GAME_MAX_AUDIO_STREAM_FRAMES; ++frame) {
        source[frame * 2U] = (int16_t)frame;
        source[frame * 2U + 1U] = (int16_t)(-(int16_t)frame);
    }
    CHECK(p4_audio_mixer_submit_pcm16_stereo(
        &mixer, source, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    memset(source, 0, sizeof(source));
    CHECK(p4_audio_mixer_render(
        &mixer, output, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    for (size_t frame = 0U;
         frame < P4_GAME_MAX_AUDIO_STREAM_FRAMES; ++frame) {
        CHECK(output[frame * 2U] == (int16_t)frame);
        CHECK(output[frame * 2U + 1U] == (int16_t)(-(int16_t)frame));
    }

    p4_audio_mixer_stats_t stats;
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.stream_blocks_submitted == 1U);
    CHECK(stats.stream_frames_submitted ==
          P4_GAME_MAX_AUDIO_STREAM_FRAMES);
    CHECK(stats.stream_queued_frames == 0U);
    CHECK(stats.stream_active);
    CHECK(stats.stream_underrun_frames == 0U);
    CHECK(p4_audio_mixer_render(&mixer, output, 1U));
    CHECK(output[0] == 0 && output[1] == 0);
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.stream_underrun_frames == 1U);

    for (size_t sample = 0U; sample < sizeof(source) / sizeof(source[0]);
         ++sample) {
        source[sample] = INT16_C(1234);
    }
    p4_audio_mixer_stop_all(&mixer);
    CHECK(p4_audio_mixer_submit_pcm16_stereo(
        &mixer, source, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    CHECK(p4_audio_mixer_submit_pcm16_stereo(
        &mixer, source, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    CHECK(!p4_audio_mixer_submit_pcm16_stereo(&mixer, source, 1U));
    CHECK(p4_audio_mixer_render(
        &mixer, output, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    CHECK(p4_audio_mixer_submit_pcm16_stereo(
        &mixer, source, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    CHECK(p4_audio_mixer_render(
        &mixer, output, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    CHECK(p4_audio_mixer_render(
        &mixer, output, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    for (size_t sample = 0U; sample < sizeof(output) / sizeof(output[0]);
         ++sample) {
        CHECK(output[sample] == INT16_C(1234));
    }
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.stream_blocks_rejected == 1U);
    CHECK(stats.stream_queued_frames == 0U);

    p4_audio_mixer_stop_all(&mixer);
    const int16_t loud[] = {INT16_C(30000), INT16_C(-32000)};
    const p4_tone_t tone = {
        .frequency_hz = 440U,
        .duration_ms = 10U,
        .volume_step = 10U,
        .waveform = P4_WAVE_SQUARE,
    };
    CHECK(p4_audio_mixer_submit_pcm16_stereo(&mixer, loud, 1U));
    CHECK(p4_audio_mixer_play_tone(&mixer, &tone));
    CHECK(p4_audio_mixer_render(&mixer, output, 1U));
    CHECK(output[0] == INT16_MAX);
    CHECK(output[1] == INT16_C(-28000));
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.clipped_samples == 1U);

    CHECK(!p4_audio_mixer_submit_pcm16_stereo(NULL, source, 1U));
    CHECK(!p4_audio_mixer_submit_pcm16_stereo(&mixer, NULL, 1U));
    CHECK(!p4_audio_mixer_submit_pcm16_stereo(&mixer, source, 0U));
    CHECK(!p4_audio_mixer_submit_pcm16_stereo(
        &mixer, source, P4_GAME_MAX_AUDIO_STREAM_FRAMES + 1U));
    p4_audio_mixer_stop_all(&mixer);
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(!stats.stream_active);
    CHECK(stats.stream_queued_frames == 0U);
}

static void test_game_runtime(void)
{
    CHECK(p4_game_descriptor_valid(&s_fixture_game));
    p4_game_descriptor_t invalid = s_fixture_game;
    invalid.api_version = 2U;
    CHECK(!p4_game_descriptor_valid(&invalid));
    invalid = s_fixture_game;
    invalid.required_capabilities |= P4_GAME_CAP_AUDIO_TONE;
    invalid.optional_capabilities |= P4_GAME_CAP_AUDIO_TONE;
    CHECK(!p4_game_descriptor_valid(&invalid));

    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    p4_achievement_catalog_t achievements;
    p4_achievement_catalog_init(&achievements);
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_AUDIO_TONE |
                                  P4_GAME_CAP_AUDIO_STREAM,
        .audio_context = &mixer,
        .game_id = s_fixture_game.id,
        .play_tone = p4_audio_mixer_service_play_tone,
        .submit_pcm16_stereo =
            p4_audio_mixer_service_submit_pcm16_stereo,
        .stop_audio = p4_audio_mixer_service_stop,
        .achievement_context = &achievements,
        .unlock_achievement = p4_achievement_catalog_service_unlock,
    };
    p4_game_services_t invalid_services = services;
    invalid_services.submit_pcm16_stereo = NULL;
    p4_game_instance_t rejected = {0};
    fixture_state_t rejected_state;
    CHECK(!p4_game_instance_start(
        &rejected, &s_fixture_game, &invalid_services,
        &rejected_state, sizeof(rejected_state)));
    fixture_state_t state;
    p4_game_instance_t instance = {0};
    CHECK(p4_game_instance_start(&instance, &s_fixture_game, &services,
                                 &state, sizeof(state)));
    CHECK(state.starts == 1U);
    CHECK(p4_game_unlock_achievement(
        &instance.context, "hello", "HELLO WORLD", "UNLOCK A BADGE"));
    CHECK(achievements.count == 1U);
    CHECK(strcmp(achievements.entries[0].game_id, s_fixture_game.id) == 0);
    CHECK(p4_game_unlock_achievement(
        &instance.context, "hello", "HELLO WORLD", "UNLOCK A BADGE"));
    CHECK(achievements.count == 1U);
    CHECK(achievements.duplicate_events == 1U);
    const int16_t pcm[] = {INT16_C(100), INT16_C(-100)};
    CHECK(p4_game_submit_pcm16_stereo(&instance.context, pcm, 1U));
    CHECK(!p4_game_submit_pcm16_stereo(
        &instance.context, pcm, P4_GAME_MAX_AUDIO_STREAM_FRAMES + 1U));
    p4_audio_mixer_stats_t audio_stats;
    p4_audio_mixer_get_stats(&mixer, &audio_stats);
    CHECK(audio_stats.stream_queued_frames == 1U);
    p4_game_input_t input = {
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 1000U) ==
          P4_GAME_CONTINUE);
    CHECK(instance.context.elapsed_ms == P4_GAME_MAX_FRAME_DELTA_MS);

    uint16_t *const pixels = calloc(
        (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT,
        sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels != NULL) {
        p4_game_surface_t surface = {
            .pixels = pixels,
            .stride_pixels = P4_GAME_SURFACE_WIDTH,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        CHECK(p4_game_instance_render(&instance, &surface));
        CHECK(pixels[0] == UINT16_C(0x1234));
        free(pixels);
    }
    input.pressed = P4_BUTTON_A;
    input.held = P4_BUTTON_A;
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    CHECK(state.stops == 1U);
    CHECK(!instance.active);
}

static void test_achievement_bounds(void)
{
    p4_achievement_catalog_t achievements;
    p4_achievement_catalog_init(&achievements);
    const p4_game_achievement_t invalid = {
        .game_id = "org.p4.fixture",
        .id = "",
        .title = "INVALID",
        .description = "NO ID",
    };
    CHECK(!p4_achievement_catalog_unlock(&achievements, &invalid));
    CHECK(achievements.rejected_events == 1U);
    CHECK(p4_achievement_catalog_get(&achievements, 0U) == NULL);
}

int main(void)
{
    test_input_mapper();
    test_draw_bounds();
    test_audio_mixer();
    test_audio_stream();
    test_game_runtime();
    test_achievement_bounds();
    if (s_failures != 0) {
        fprintf(stderr, "%d p4 game API test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 game API tests passed");
    return EXIT_SUCCESS;
}
