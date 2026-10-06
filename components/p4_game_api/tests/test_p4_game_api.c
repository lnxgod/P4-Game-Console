// SPDX-License-Identifier: MIT

#include "p4/audio.h"
#include "p4/audio_pack.h"
#include "p4/achievements.h"
#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/game.h"
#include "p4/input.h"
#include "p4/visual.h"

#include <limits.h>
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

typedef struct {
    p4_game_signal_snapshot_t snapshot;
    uint64_t requested_focus;
} fixture_signal_t;

typedef struct {
    char slot_id[P4_GAME_SAVE_SLOT_ID_BYTES];
    uint8_t data[64];
    size_t data_bytes;
    uint32_t schema_version;
    uint32_t expected_sequence;
    p4_game_save_ticket_t ticket;
    p4_game_save_status_t status;
    uint32_t committed_sequence;
} fixture_save_t;

typedef struct {
    p4_game_multiplayer_status_t status;
    p4_game_multiplayer_message_t incoming;
    uint8_t sent[P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES];
    size_t sent_bytes;
    bool has_incoming;
} fixture_multiplayer_t;

static bool fixture_request_signal(void *context, uint64_t focus_token)
{
    fixture_signal_t *const signal = context;
    if (signal == NULL) {
        return false;
    }
    signal->requested_focus = focus_token;
    signal->snapshot = (p4_game_signal_snapshot_t){
        .generation = 1U,
        .status = P4_GAME_SIGNAL_READY,
        .count = 1U,
        .results = {{
            .token = UINT64_C(0x12345678),
            .label = "FIXTURE SIGNAL",
            .rssi_dbm = -55,
            .channel = 6U,
            .flags = P4_GAME_SIGNAL_PROTECTED,
        }},
    };
    return true;
}

static bool fixture_read_signal(void *context,
                                p4_game_signal_snapshot_t *snapshot)
{
    const fixture_signal_t *const signal = context;
    if (signal == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = signal->snapshot;
    return true;
}

static bool fixture_queue_save(
    void *context,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t expected_sequence,
    const uint8_t *data,
    size_t data_bytes,
    p4_game_save_ticket_t *ticket_out)
{
    fixture_save_t *const save = context;
    if (save == NULL || slot_id == NULL || data == NULL ||
        data_bytes == 0U || data_bytes > sizeof(save->data) ||
        ticket_out == NULL) {
        return false;
    }
    const size_t slot_bytes = strlen(slot_id);
    if (slot_bytes == 0U || slot_bytes >= sizeof(save->slot_id)) {
        return false;
    }
    memcpy(save->slot_id, slot_id, slot_bytes + 1U);
    memcpy(save->data, data, data_bytes);
    save->data_bytes = data_bytes;
    save->schema_version = schema_version;
    save->expected_sequence = expected_sequence;
    save->ticket = UINT32_C(7);
    save->status = P4_GAME_SAVE_QUEUED;
    save->committed_sequence = 0U;
    *ticket_out = save->ticket;
    return true;
}

static bool fixture_read_save_status(
    void *context,
    p4_game_save_ticket_t ticket,
    p4_game_save_status_t *status_out,
    uint32_t *committed_sequence_out)
{
    const fixture_save_t *const save = context;
    if (save == NULL || ticket == P4_GAME_SAVE_INVALID_TICKET ||
        ticket != save->ticket || status_out == NULL ||
        committed_sequence_out == NULL) {
        return false;
    }
    *status_out = save->status;
    *committed_sequence_out = save->committed_sequence;
    return true;
}

static bool fixture_multiplayer_status(
    void *context, p4_game_multiplayer_status_t *status_out)
{
    const fixture_multiplayer_t *const multiplayer = context;
    if (multiplayer == NULL || status_out == NULL) {
        return false;
    }
    *status_out = multiplayer->status;
    return true;
}

static bool fixture_multiplayer_send(
    void *context, const uint8_t *data, size_t data_bytes)
{
    fixture_multiplayer_t *const multiplayer = context;
    if (multiplayer == NULL || data == NULL || data_bytes == 0U ||
        data_bytes > sizeof(multiplayer->sent)) {
        return false;
    }
    memcpy(multiplayer->sent, data, data_bytes);
    multiplayer->sent_bytes = data_bytes;
    return true;
}

static bool fixture_multiplayer_receive(
    void *context, p4_game_multiplayer_message_t *message_out)
{
    fixture_multiplayer_t *const multiplayer = context;
    if (multiplayer == NULL || message_out == NULL ||
        !multiplayer->has_incoming) {
        return false;
    }
    *message_out = multiplayer->incoming;
    multiplayer->has_incoming = false;
    return true;
}

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
                             P4_GAME_CAP_AUDIO_STREAM |
                             P4_GAME_CAP_SAVE,
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
    p4_draw_cp437_glyph(&surface, -3, 32, UINT8_C(0xdb),
                        UINT16_C(0xf800), UINT16_C(0x001f),
                        P4_DRAW_CP437_COMPACT_HEIGHT);
    CHECK(pixels[32U * STRIDE] == UINT16_C(0xf800));
    p4_draw_cp437_glyph(&surface, 20, 32, (uint8_t)' ',
                        UINT16_C(0xf800), UINT16_C(0x001f),
                        P4_DRAW_CP437_FULL_HEIGHT);
    CHECK(pixels[32U * STRIDE + 20U] == UINT16_C(0x001f));
    const uint8_t box_text[] = {0xdaU, 0xc4U, 0xbfU};
    p4_draw_cp437_text(&surface, 36, 32, box_text, sizeof(box_text),
                       UINT16_C(0xffff), UINT16_C(0x0000),
                       P4_DRAW_CP437_COMPACT_HEIGHT);
    const uint16_t before_invalid = pixels[60U * STRIDE + 60U];
    p4_draw_cp437_glyph(&surface, 60, 60, UINT8_C(0xdb),
                        UINT16_C(0xffff), UINT16_C(0x0000), 7U);
    CHECK(pixels[60U * STRIDE + 60U] == before_invalid);
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

/* Independent pixel-membership oracle: exercise overflow-prone input values,
 * both negotiated sizes, unaligned rows, padding and allocation guards. */
static void test_fill_rect_exact_clipping(void)
{
    static const int rectangles[][4] = {
        {0, 0, INT_MAX, INT_MAX}, {-1, -1, INT_MAX, INT_MAX},
        {INT_MIN, INT_MIN, INT_MAX, INT_MAX},
        {INT_MIN, 0, INT_MAX, 1}, {0, INT_MIN, 1, INT_MAX},
        {INT_MAX, 0, INT_MAX, 1}, {0, INT_MAX, 1, INT_MAX},
        {INT_MAX - 1, INT_MAX - 1, INT_MAX, INT_MAX},
        {-20, -20, 40, 40}, {-1, -1, 1, 1}, {-1, -1, 2, 2},
        {319, 199, 2, 2}, {320, 200, 1, 1},
        {767, 479, 2, 2}, {768, 480, 1, 1},
        {767, -1, 100, 100}, {-1, 479, 100, 100},
        {0, 0, 0, INT_MAX}, {0, 0, INT_MAX, 0},
        {0, 0, INT_MIN, 1}, {0, 0, 1, INT_MIN},
        {0, 0, -1, -1}, {10, 20, 1, 1}, {10, 20, 3, 7},
        {0, 0, 320, 200}, {0, 0, 768, 480},
        {-769, -481, 1537, 961}, {1, 1, 766, 478},
    };
    enum { GUARD = 23 };
    const uint16_t untouched = UINT16_C(0xa55a);
    const uint16_t ink = UINT16_C(0x36cf);
    for (unsigned resolution = 0U; resolution < 2U; ++resolution) {
        const unsigned width = resolution == 0U ?
            P4_GAME_SURFACE_WIDTH : P4_GAME_SURFACE_HIGH_RES_WIDTH;
        const unsigned height = resolution == 0U ?
            P4_GAME_SURFACE_HEIGHT : P4_GAME_SURFACE_HIGH_RES_HEIGHT;
        const size_t stride = width + 9U;
        const size_t words = stride * height;
        const size_t total = words + 2U * GUARD;
        uint16_t *const allocation = malloc(total * sizeof(*allocation));
        CHECK(allocation != NULL);
        if (allocation == NULL) continue;
        p4_game_surface_t surface = {
            .pixels = allocation + GUARD,
            .stride_pixels = (uint32_t)stride,
            .width = (uint16_t)width,
            .height = (uint16_t)height,
        };
        for (size_t index = 0U;
             index < sizeof(rectangles) / sizeof(rectangles[0]); ++index) {
            for (size_t i = 0U; i < total; ++i) allocation[i] = untouched;
            const int *const rect = rectangles[index];
            p4_draw_fill_rect(&surface, rect[0], rect[1], rect[2], rect[3], ink);
            bool exact = true;
            for (unsigned row = 0U; row < height; ++row) {
                for (size_t col = 0U; col < stride; ++col) {
                    const int64_t dx = (int64_t)col - rect[0];
                    const int64_t dy = (int64_t)row - rect[1];
                    const bool inside = col < width && rect[2] > 0 && rect[3] > 0 &&
                        dx >= 0 && dx < rect[2] && dy >= 0 && dy < rect[3];
                    if (surface.pixels[(size_t)row * stride + col] !=
                        (inside ? ink : untouched)) exact = false;
                }
            }
            if (!exact) fprintf(stderr, "fill rect mismatch: resolution %u, case %zu\n",
                                resolution, index);
            CHECK(exact);
            for (size_t i = 0U; i < GUARD; ++i) {
                CHECK(allocation[i] == untouched);
                CHECK(allocation[GUARD + words + i] == untouched);
            }
        }
        for (size_t i = 0U; i < total; ++i) allocation[i] = untouched;
        p4_draw_fill_rect(NULL, 0, 0, INT_MAX, INT_MAX, ink);
        p4_game_surface_t invalid = surface;
        invalid.stride_pixels = width - 1U;
        p4_draw_fill_rect(&invalid, 0, 0, INT_MAX, INT_MAX, ink);
        invalid = surface;
        invalid.width = 1U;
        p4_draw_fill_rect(&invalid, 0, 0, INT_MAX, INT_MAX, ink);
        invalid = surface;
        invalid.pixels = NULL;
        p4_draw_fill_rect(&invalid, 0, 0, INT_MAX, INT_MAX, ink);
        bool untouched_invalid = true;
        for (size_t i = 0U; i < total; ++i) {
            if (allocation[i] != untouched) untouched_invalid = false;
        }
        CHECK(untouched_invalid);
        free(allocation);
    }
}

static void test_visual_helpers(void)
{
    CHECK(p4_q16_to_int_round(p4_q16_from_int(12)) == 12);
    CHECK(p4_q16_to_int_round(p4_q16_from_int(-12)) == -12);
    CHECK(p4_q16_to_int_round(p4_q16_step(
              p4_q16_from_int(10), p4_q16_from_int(60), 500U)) == 16);
    CHECK(p4_q16_to_int_round(p4_q16_step(
              p4_q16_from_int(10), p4_q16_from_int(60), 100U)) == 16);
    CHECK(p4_ease_smoothstep_u16(0U) == 0U);
    CHECK(p4_ease_smoothstep_u16(UINT16_MAX) == UINT16_MAX);
    CHECK(p4_ease_smoothstep_u16(UINT16_C(32768)) >= UINT16_C(32767));
    CHECK(p4_animation_frame(0U, 100U, 4U, false) == 0U);
    CHECK(p4_animation_frame(350U, 100U, 4U, false) == 3U);
    CHECK(p4_animation_frame(450U, 100U, 4U, true) == 2U);

    int shake_x = 99;
    int shake_y = 99;
    p4_camera_shake(7U, UINT32_C(1234), 5, &shake_x, &shake_y);
    CHECK(shake_x >= -5 && shake_x <= 5);
    CHECK(shake_y >= -5 && shake_y <= 5);
    int duplicate_x = 0;
    int duplicate_y = 0;
    p4_camera_shake(
        7U, UINT32_C(1234), 5, &duplicate_x, &duplicate_y);
    CHECK(duplicate_x == shake_x && duplicate_y == shake_y);

    uint16_t pixels[P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT];
    p4_game_surface_t surface = {
        .pixels = pixels,
        .stride_pixels = P4_GAME_SURFACE_WIDTH,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    p4_draw_clear(&surface, UINT16_C(0x1111));
    static const uint16_t sheet[] = {
        UINT16_C(1), UINT16_C(2), UINT16_C(3),
        UINT16_C(4), UINT16_C(5), UINT16_C(6),
    };
    const p4_sprite_t sprite = {
        .pixels = sheet,
        .sheet_width = 3U,
        .sheet_height = 2U,
        .stride_pixels = 3U,
        .source_x = 1U,
        .source_y = 0U,
        .width = 2U,
        .height = 2U,
        .transparent_color = UINT16_C(5),
        .scale = 2U,
        .flip = P4_SPRITE_FLIP_X,
        .use_transparency = true,
    };
    CHECK(p4_sprite_valid(&sprite));
    p4_sprite_t oversized = sprite;
    oversized.sheet_width = 2048U;
    oversized.sheet_height = 2048U;
    oversized.stride_pixels = 2048U;
    oversized.source_x = 0U;
    oversized.width = 2048U;
    oversized.height = 2048U;
    oversized.scale = 1U;
    CHECK(!p4_sprite_valid(&oversized));
    p4_draw_sprite(&surface, 10, 20, &sprite);
    CHECK(pixels[20U * P4_GAME_SURFACE_WIDTH + 10U] == UINT16_C(3));
    CHECK(pixels[20U * P4_GAME_SURFACE_WIDTH + 12U] == UINT16_C(2));
    CHECK(pixels[22U * P4_GAME_SURFACE_WIDTH + 10U] == UINT16_C(6));
    CHECK(pixels[22U * P4_GAME_SURFACE_WIDTH + 12U] == UINT16_C(0x1111));

    p4_particle_t particle;
    p4_particle_spawn(
        &particle, p4_q16_from_int(30), p4_q16_from_int(40),
        p4_q16_from_int(10), p4_q16_from_int(0), 200U,
        UINT16_C(0xffff), 2U);
    CHECK(particle.active);
    p4_particles_update(&particle, 1U, 100U, p4_q16_from_int(100));
    CHECK(particle.active && particle.age_ms == 100U);
    p4_particles_draw(&surface, &particle, 1U, 0, 0);
    CHECK(pixels[41U * P4_GAME_SURFACE_WIDTH + 31U] == UINT16_C(0xffff));
    p4_particles_update(&particle, 1U, 100U, p4_q16_from_int(100));
    CHECK(!particle.active);
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
    int16_t pcm[P4_GAME_AUDIO_MAX_RENDER_FRAMES * 2U];
    CHECK(p4_audio_mixer_render(&mixer, pcm, 267U));
    bool nonzero = false;
    for (size_t frame = 0U;
         frame < P4_GAME_AUDIO_MAX_RENDER_FRAMES; ++frame) {
        CHECK(pcm[frame * 2U] == pcm[frame * 2U + 1U]);
        nonzero = nonzero || pcm[frame * 2U] != 0;
    }
    CHECK(nonzero);
    p4_audio_mixer_stats_t stats;
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.tones_started == 1U);
    CHECK(stats.frames_rendered == 267U);
    CHECK(stats.active_voices == 0U);
    CHECK(!p4_audio_mixer_render(&mixer, pcm, 268U));
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
    for (size_t i = 0; i < P4_GAME_AUDIO_STREAM_BUFFER_FRAMES /
                              P4_GAME_MAX_AUDIO_STREAM_FRAMES; ++i) {
        CHECK(p4_audio_mixer_submit_pcm16_stereo(
            &mixer, source, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    }
    CHECK(!p4_audio_mixer_submit_pcm16_stereo(&mixer, source, 1U));
    CHECK(p4_audio_mixer_render(
        &mixer, output, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    CHECK(p4_audio_mixer_submit_pcm16_stereo(
        &mixer, source, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    for (size_t i = 0; i < P4_GAME_AUDIO_STREAM_BUFFER_FRAMES /
                              P4_GAME_MAX_AUDIO_STREAM_FRAMES; ++i) {
        CHECK(p4_audio_mixer_render(
            &mixer, output, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
    }
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

static void test_standard_feedback_pack(void)
{
    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_AUDIO_STREAM,
        .audio_context = &mixer,
        .submit_pcm16_stereo =
            p4_audio_mixer_service_submit_pcm16_stereo,
    };
    p4_game_context_t context = {
        .services = &services,
    };
    p4_game_audio_effect_player_t player;
    p4_game_audio_effect_player_init(&player);
    CHECK(!p4_game_audio_effect_active(&player));
    CHECK(p4_game_audio_effect_play(
        &context, &player, P4_GAME_AUDIO_EFFECT_ACTION));
    CHECK(p4_game_audio_effect_active(&player));
    int16_t output[P4_GAME_MAX_AUDIO_STREAM_FRAMES * 2U];
    bool nonzero = false;
    for (unsigned block = 0U;
         block < 64U && p4_game_audio_effect_active(&player); ++block) {
        CHECK(p4_audio_mixer_render(
            &mixer, output, P4_GAME_MAX_AUDIO_STREAM_FRAMES));
        for (size_t sample = 0U; sample < sizeof(output) / sizeof(output[0]);
             ++sample) {
            nonzero = nonzero || output[sample] != 0;
        }
        (void)p4_game_audio_effect_service(&context, &player);
    }
    CHECK(nonzero);
    CHECK(!p4_game_audio_effect_active(&player));
    CHECK(!p4_game_audio_effect_play(
        &context, &player, P4_GAME_AUDIO_EFFECT_COUNT));

    uint16_t *const pixels = calloc(
        (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT,
        sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels == NULL) {
        return;
    }
    p4_game_surface_t surface = {
        .pixels = pixels,
        .stride_pixels = P4_GAME_SURFACE_WIDTH,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    p4_draw_clear(&surface, UINT16_C(0x1234));
    p4_game_feedback_draw(
        &surface, P4_GAME_FX_ACTION, 160, 100, 0U);
    bool visible = false;
    for (size_t pixel = 0U;
         pixel < (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT;
         ++pixel) {
        visible = visible || pixels[pixel] != UINT16_C(0x1234);
    }
    CHECK(visible);
    p4_draw_clear(&surface, UINT16_C(0x1234));
    p4_game_feedback_draw(
        &surface, P4_GAME_FX_ACTION, 160, 100, 20U);
    bool idle_clear = true;
    for (size_t pixel = 0U;
         pixel < (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT;
         ++pixel) {
        idle_clear = idle_clear && pixels[pixel] == UINT16_C(0x1234);
    }
    CHECK(idle_clear);
    free(pixels);
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
    fixture_signal_t signal = {0};
    fixture_save_t save = {0};
    fixture_multiplayer_t multiplayer = {
        .status = {
            .generation = 3U,
            .session_seed = UINT64_C(0x123456789abcdef0),
            .state = P4_GAME_MULTIPLAYER_CONNECTED,
            .role = P4_GAME_MULTIPLAYER_ROLE_HOST,
            .local_player_slot = 0U,
            .player_count = 2U,
        },
        .incoming = {
            .sequence = 9U,
            .player_slot = 1U,
            .bytes = 3U,
            .data = {7U, 8U, 9U},
        },
        .has_incoming = true,
    };
    static const uint8_t initial_save[] = {1U, 2U, 3U};
    const p4_game_multiplayer_profile_t multiplayer_profile = {
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = 10U,
        .message_bytes = 4U,
        .protocol = 7U,
    };
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_AUDIO_TONE |
                                  P4_GAME_CAP_AUDIO_STREAM |
                                  P4_GAME_CAP_SIGNAL_SCAN |
                                  P4_GAME_CAP_SAVE |
                                  P4_GAME_CAP_MULTIPLAYER_SESSION,
        .audio_context = &mixer,
        .game_id = s_fixture_game.id,
        .play_tone = p4_audio_mixer_service_play_tone,
        .submit_pcm16_stereo =
            p4_audio_mixer_service_submit_pcm16_stereo,
        .stop_audio = p4_audio_mixer_service_stop,
        .achievement_context = &achievements,
        .unlock_achievement = p4_achievement_catalog_service_unlock,
        .signal_scan_context = &signal,
        .request_signal_scan = fixture_request_signal,
        .read_signal_scan = fixture_read_signal,
        .save_context = &save,
        .save_data = initial_save,
        .save_bytes = sizeof(initial_save),
        .save_schema_version = 3U,
        .save_sequence = 4U,
        .queue_save = fixture_queue_save,
        .read_save_status = fixture_read_save_status,
        .multiplayer_context = &multiplayer,
        .multiplayer_read_status = fixture_multiplayer_status,
        .multiplayer_send = fixture_multiplayer_send,
        .multiplayer_receive = fixture_multiplayer_receive,
        .multiplayer_profile = &multiplayer_profile,
    };
    p4_game_services_t invalid_services = services;
    invalid_services.submit_pcm16_stereo = NULL;
    p4_game_instance_t rejected = {0};
    fixture_state_t rejected_state;
    CHECK(!p4_game_instance_start(
        &rejected, &s_fixture_game, &invalid_services,
        &rejected_state, sizeof(rejected_state)));
    invalid_services = services;
    invalid_services.read_signal_scan = NULL;
    CHECK(!p4_game_instance_start(
        &rejected, &s_fixture_game, &invalid_services,
        &rejected_state, sizeof(rejected_state)));
    invalid_services = services;
    invalid_services.queue_save = NULL;
    CHECK(!p4_game_instance_start(
        &rejected, &s_fixture_game, &invalid_services,
        &rejected_state, sizeof(rejected_state)));
    invalid_services = services;
    invalid_services.save_data = NULL;
    CHECK(!p4_game_instance_start(
        &rejected, &s_fixture_game, &invalid_services,
        &rejected_state, sizeof(rejected_state)));
    p4_game_services_t empty_floor_services = services;
    empty_floor_services.save_data = NULL;
    empty_floor_services.save_bytes = 0U;
    empty_floor_services.save_schema_version = 0U;
    empty_floor_services.save_sequence = 17U;
    p4_game_instance_t empty_floor_instance = {0};
    fixture_state_t empty_floor_state;
    CHECK(p4_game_instance_start(
        &empty_floor_instance, &s_fixture_game, &empty_floor_services,
        &empty_floor_state, sizeof(empty_floor_state)));
    CHECK(empty_floor_instance.services.save_sequence == 17U);
    p4_game_instance_stop(&empty_floor_instance);
    invalid_services = services;
    invalid_services.multiplayer_receive = NULL;
    CHECK(!p4_game_instance_start(
        &rejected, &s_fixture_game, &invalid_services,
        &rejected_state, sizeof(rejected_state)));
    p4_game_multiplayer_profile_t invalid_profile = multiplayer_profile;
    invalid_profile.message_bytes = 0U;
    invalid_services = services;
    invalid_services.multiplayer_profile = &invalid_profile;
    CHECK(!p4_game_instance_start(
        &rejected, &s_fixture_game, &invalid_services,
        &rejected_state, sizeof(rejected_state)));
    invalid_services = services;
    invalid_services.available_capabilities |= P4_GAME_CAP_TEXT_INPUT;
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
    CHECK(p4_game_request_signal_scan(
        &instance.context, UINT64_C(0x12345678)));
    CHECK(signal.requested_focus == UINT64_C(0x12345678));
    p4_game_signal_snapshot_t signal_snapshot;
    CHECK(p4_game_read_signal_scan(&instance.context, &signal_snapshot));
    CHECK(signal_snapshot.count == 1U);
    CHECK(strcmp(signal_snapshot.results[0].label, "FIXTURE SIGNAL") == 0);
    signal.snapshot.results[0].flags |= P4_GAME_SIGNAL_SIMULATED;
    CHECK(p4_game_read_signal_scan(&instance.context, &signal_snapshot));
    CHECK((signal_snapshot.results[0].flags &
           P4_GAME_SIGNAL_SIMULATED) != 0U);
    signal.snapshot.results[0].flags |= UINT8_C(0x80);
    CHECK(!p4_game_read_signal_scan(&instance.context, &signal_snapshot));
    signal.snapshot.results[0].flags = P4_GAME_SIGNAL_PROTECTED;
    signal.snapshot.results[0].label[0] = '\0';
    CHECK(!p4_game_read_signal_scan(&instance.context, &signal_snapshot));
    uint8_t save_payload[] = {9U, 8U, 7U, 6U};
    p4_game_save_ticket_t save_ticket = UINT32_C(99);
    CHECK(!p4_game_queue_save(
        &instance.context, "_BAD", 3U, 4U, save_payload,
        sizeof(save_payload), &save_ticket));
    CHECK(save_ticket == P4_GAME_SAVE_INVALID_TICKET);
    CHECK(!p4_game_queue_save(
        &instance.context, "AUTO", 0U, 4U, save_payload,
        sizeof(save_payload), &save_ticket));
    CHECK(!p4_game_queue_save(
        &instance.context, "AUTO", 3U, 4U, save_payload, 0U,
        &save_ticket));
    CHECK(p4_game_queue_save(
        &instance.context, "AUTO", 3U, 4U, save_payload,
        sizeof(save_payload), &save_ticket));
    CHECK(save_ticket == UINT32_C(7));
    save_payload[0] = 0U;
    CHECK(save.data[0] == 9U);
    CHECK(strcmp(save.slot_id, "AUTO") == 0);
    CHECK(save.schema_version == 3U);
    CHECK(save.expected_sequence == 4U);
    p4_game_save_status_t save_status = P4_GAME_SAVE_ERROR;
    uint32_t committed_sequence = UINT32_MAX;
    CHECK(p4_game_read_save_status(
        &instance.context, save_ticket, &save_status, &committed_sequence));
    CHECK(save_status == P4_GAME_SAVE_QUEUED);
    CHECK(committed_sequence == 0U);
    p4_game_multiplayer_status_t multiplayer_status;
    CHECK(p4_game_multiplayer_read_status(
        &instance.context, &multiplayer_status));
    CHECK(multiplayer_status.state == P4_GAME_MULTIPLAYER_CONNECTED);
    CHECK(multiplayer_status.role == P4_GAME_MULTIPLAYER_ROLE_HOST);
    CHECK(multiplayer_status.player_count == 2U);
    p4_game_multiplayer_profile_t observed_profile;
    CHECK(p4_game_multiplayer_read_profile(
        &instance.context, &observed_profile));
    CHECK(observed_profile.style == P4_GAME_MULTIPLAYER_STYLE_TURN_BASED);
    CHECK(observed_profile.message_bytes == 4U);
    CHECK(observed_profile.protocol == 7U);
    const uint8_t multiplayer_payload[] = {1U, 2U, 3U, 4U};
    CHECK(p4_game_multiplayer_send(
        &instance.context, multiplayer_payload,
        sizeof(multiplayer_payload)));
    CHECK(multiplayer.sent_bytes == sizeof(multiplayer_payload));
    CHECK(memcmp(multiplayer.sent, multiplayer_payload,
                 sizeof(multiplayer_payload)) == 0);
    const uint8_t oversized_multiplayer_payload[] = {1U, 2U, 3U, 4U, 5U};
    CHECK(!p4_game_multiplayer_send(
        &instance.context, oversized_multiplayer_payload,
        sizeof(oversized_multiplayer_payload)));
    p4_game_multiplayer_message_t multiplayer_message;
    CHECK(p4_game_multiplayer_receive(
        &instance.context, &multiplayer_message));
    CHECK(multiplayer_message.sequence == 9U);
    CHECK(multiplayer_message.player_slot == 1U);
    CHECK(multiplayer_message.bytes == 3U);
    CHECK(!p4_game_multiplayer_receive(
        &instance.context, &multiplayer_message));
    multiplayer.status.player_count = 1U;
    CHECK(!p4_game_multiplayer_read_status(
        &instance.context, &multiplayer_status));
    multiplayer.status.player_count = 2U;
    multiplayer.incoming = (p4_game_multiplayer_message_t){
        .sequence = 0U,
        .player_slot = 1U,
        .bytes = 1U,
        .data = {1U},
    };
    multiplayer.has_incoming = true;
    CHECK(!p4_game_multiplayer_receive(
        &instance.context, &multiplayer_message));
    multiplayer.incoming = (p4_game_multiplayer_message_t){
        .sequence = 10U,
        .player_slot = 1U,
        .bytes = 5U,
        .data = {1U, 2U, 3U, 4U, 5U},
    };
    multiplayer.has_incoming = true;
    CHECK(!p4_game_multiplayer_receive(
        &instance.context, &multiplayer_message));
    save.status = P4_GAME_SAVE_COMMITTED;
    save.committed_sequence = 5U;
    CHECK(p4_game_read_save_status(
        &instance.context, save_ticket, &save_status, &committed_sequence));
    CHECK(save_status == P4_GAME_SAVE_COMMITTED);
    CHECK(committed_sequence == 5U);
    save.status = (p4_game_save_status_t)99;
    CHECK(!p4_game_read_save_status(
        &instance.context, save_ticket, &save_status, &committed_sequence));
    CHECK(save_status == P4_GAME_SAVE_NONE);
    CHECK(committed_sequence == 0U);
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

static void test_high_res_runtime(void)
{
    p4_game_descriptor_t game = s_fixture_game;
    game.optional_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
    fixture_state_t state;
    p4_game_instance_t instance = {0};
    p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS,
    };

    CHECK(p4_game_descriptor_valid(&game));
    CHECK(p4_game_instance_start(
        &instance, &game, &services, &state, sizeof(state)));
    uint16_t *pixels = calloc(
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
        free(pixels);
    }
    p4_game_instance_stop(&instance);

    services.available_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
    CHECK(p4_game_instance_start(
        &instance, &game, &services, &state, sizeof(state)));
    pixels = calloc(
        (size_t)P4_GAME_SURFACE_HIGH_RES_WIDTH *
            P4_GAME_SURFACE_HIGH_RES_HEIGHT,
        sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels != NULL) {
        p4_game_surface_t surface = {
            .pixels = pixels,
            .stride_pixels = P4_GAME_SURFACE_HIGH_RES_WIDTH,
            .width = P4_GAME_SURFACE_HIGH_RES_WIDTH,
            .height = P4_GAME_SURFACE_HIGH_RES_HEIGHT,
        };
        CHECK(p4_surface_valid(&surface));
        CHECK(p4_game_instance_render(&instance, &surface));
        CHECK(pixels[(size_t)P4_GAME_SURFACE_HIGH_RES_WIDTH *
                     P4_GAME_SURFACE_HIGH_RES_HEIGHT - 1U] ==
              UINT16_C(0x1234));
        surface.width = P4_GAME_SURFACE_WIDTH;
        surface.height = P4_GAME_SURFACE_HEIGHT;
        surface.stride_pixels = P4_GAME_SURFACE_WIDTH;
        CHECK(!p4_game_instance_render(&instance, &surface));
        free(pixels);
    }
    p4_game_input_t input = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{319U, 199U}},
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    input.touches[0].x = P4_GAME_SURFACE_WIDTH;
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_ERROR);
    p4_game_instance_stop(&instance);

    game.required_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
    game.optional_capabilities &=
        (uint32_t)~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
    services.available_capabilities &=
        (uint32_t)~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
    CHECK(!p4_game_instance_start(
        &instance, &game, &services, &state, sizeof(state)));
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
    test_fill_rect_exact_clipping();
    test_visual_helpers();
    test_audio_mixer();
    test_audio_stream();
    test_standard_feedback_pack();
    test_game_runtime();
    test_high_res_runtime();
    test_achievement_bounds();
    if (s_failures != 0) {
        fprintf(stderr, "%d p4 game API test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 game API tests passed");
    return EXIT_SUCCESS;
}
