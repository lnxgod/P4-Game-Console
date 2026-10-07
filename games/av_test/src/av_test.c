// SPDX-License-Identifier: MIT
/* Display, frame-pacing, and tone diagnostic for P4 Game API v1. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"
#include "p4/card_art.h"

/* Keep layout/input units canonical, but rasterize each primitive and glyph
 * directly into the negotiated native surface. The helpers retain 320x200
 * drawing for explicitly requested legacy diagnostics. */
#define p4_draw_fill_rect p4_card_fill
#define p4_draw_text p4_card_text

enum {
    AV_PATTERN_COUNT = 4,
    AV_MOTION_MIN_X = 4,
    AV_MOTION_MAX_X = 315,
};

typedef struct {
    uint32_t held_buttons;
    uint32_t frame_count;
    uint32_t motion_accumulator_ms;
    int16_t motion_x;
    int8_t motion_direction;
    uint8_t pattern;
    uint8_t tone_index;
    bool motion_enabled;
} av_test_state_t;

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(av_test_state_t)) {
        return false;
    }
    *(av_test_state_t *)context->state = (av_test_state_t){
        .motion_x = AV_MOTION_MIN_X,
        .motion_direction = 1,
        .motion_enabled = true,
    };
    return true;
}

static void play_next_tone(p4_game_context_t *context,
                           av_test_state_t *state)
{
    static const uint16_t frequencies[] = {220U, 440U, 880U, 1320U};
    const uint16_t frequency = frequencies[
        state->tone_index % (sizeof(frequencies) / sizeof(frequencies[0]))];
    (void)p4_game_play_tone(
        context, frequency, 180U, 5U, P4_WAVE_TRIANGLE);
    state->tone_index = (uint8_t)((state->tone_index + 1U) % 4U);
}

static p4_game_result_t game_update(
    p4_game_context_t *context, const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    av_test_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_LEFT) != 0U) {
        state->pattern = state->pattern == 0U
            ? AV_PATTERN_COUNT - 1U : (uint8_t)(state->pattern - 1U);
    }
    if ((input->pressed & P4_BUTTON_RIGHT) != 0U) {
        state->pattern = (uint8_t)((state->pattern + 1U) % AV_PATTERN_COUNT);
    }
    if ((input->pressed & P4_BUTTON_A) != 0U) {
        play_next_tone(context, state);
    }
    if ((input->pressed & P4_BUTTON_B) != 0U) {
        state->motion_enabled = !state->motion_enabled;
    }
    if ((input->pressed & P4_BUTTON_START) != 0U) {
        state->frame_count = 0U;
        state->motion_accumulator_ms = 0U;
        state->motion_x = AV_MOTION_MIN_X;
        state->motion_direction = 1;
    }
    if (state->frame_count != UINT32_MAX) {
        ++state->frame_count;
    }
    if (state->motion_enabled) {
        state->motion_accumulator_ms += elapsed_ms;
        while (state->motion_accumulator_ms >= 33U) {
            state->motion_accumulator_ms -= 33U;
            state->motion_x = (int16_t)(
                state->motion_x + state->motion_direction * 3);
            if (state->motion_x >= AV_MOTION_MAX_X) {
                state->motion_x = AV_MOTION_MAX_X;
                state->motion_direction = -1;
            } else if (state->motion_x <= AV_MOTION_MIN_X) {
                state->motion_x = AV_MOTION_MIN_X;
                state->motion_direction = 1;
            }
        }
    }
    return P4_GAME_CONTINUE;
}

static void draw_color_bars(p4_game_surface_t *surface)
{
    static const uint16_t colors[8] = {
        UINT16_C(0xFFFF), UINT16_C(0xFFE0), UINT16_C(0x07FF),
        UINT16_C(0x07E0), UINT16_C(0xF81F), UINT16_C(0xF800),
        UINT16_C(0x001F), UINT16_C(0x0000),
    };
    for (size_t index = 0U; index < 8U; ++index) {
        p4_draw_fill_rect(surface, (int)index * 40, 20, 40, 130,
                          colors[index]);
    }
}

static void draw_grid(p4_game_surface_t *surface)
{
    p4_draw_clear(surface, UINT16_C(0x0000));
    for (int x = 0; x < 320; x += 16) {
        p4_draw_fill_rect(surface, x, 20, x % 64 == 0 ? 2 : 1, 130,
                          x % 64 == 0 ? UINT16_C(0xFFFF) : UINT16_C(0x4208));
    }
    for (int y = 22; y < 150; y += 16) {
        p4_draw_fill_rect(surface, 0, y, 320, y % 64 == 0 ? 2 : 1,
                          y % 64 == 0 ? UINT16_C(0xFFFF) : UINT16_C(0x4208));
    }
}

static void draw_checker(p4_game_surface_t *surface)
{
    for (int row = 0; row < 9; ++row) {
        for (int column = 0; column < 20; ++column) {
            const uint16_t color = (row + column) % 2 == 0
                ? UINT16_C(0xFFFF) : UINT16_C(0x0000);
            p4_draw_fill_rect(surface, column * 16, 20 + row * 16,
                              16, 16, color);
        }
    }
}

static void draw_gradient(p4_game_surface_t *surface)
{
    for (int band = 0; band < 32; ++band) {
        const uint16_t red = (uint16_t)(band << 11);
        const uint16_t blue = (uint16_t)(31 - band);
        const uint16_t color = (uint16_t)(red | blue);
        p4_draw_fill_rect(surface, band * 10, 20, 10, 130, color);
    }
}

static size_t format_unsigned(uint32_t value, char output[11])
{
    char reverse[10];
    size_t count = 0U;
    do {
        reverse[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(reverse));
    for (size_t index = 0U; index < count; ++index) {
        output[index] = reverse[count - index - 1U];
    }
    output[count] = '\0';
    return count;
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const av_test_state_t *const state = context->state;
    const bool native = surface->width == P4_GAME_SURFACE_HIGH_RES_WIDTH;
    p4_draw_clear(surface, UINT16_C(0x0000));
    switch (state->pattern) {
    case 0U: draw_color_bars(surface); break;
    case 1U: draw_grid(surface); break;
    case 2U: draw_checker(surface); break;
    case 3U:
    default: draw_gradient(surface); break;
    }
    p4_draw_fill_rect(surface, 0, 0, 320, 19, UINT16_C(0x0010));
    p4_draw_text(surface, native ? 65 : 7, 6, "SOUND & MOTION",
                 UINT16_C(0xF81F), 1U, 14U);
    p4_draw_text(surface, native ? 191 : 80, 6,
                 native ? "START RESETS" : "LEFT/RIGHT PATTERN",
                 UINT16_C(0xFFFF), 1U, 18U);
    p4_draw_fill_rect(surface, state->motion_x - 2, 20, 5, 130,
                      UINT16_C(0xF81F));
    p4_draw_fill_rect(surface, 0, 150, 320, native ? 50 : 18,
                      UINT16_C(0x0000));
    if (native) {
        p4_draw_text(surface, 96, 154, "A TONE  B MOTION",
                     UINT16_C(0xFFFF), 1U, 16U);
        p4_draw_text(surface, 96, 169, "LEFT/RIGHT PATTERN",
                     UINT16_C(0xFFFF), 1U, 18U);
        p4_draw_text(surface, 96, 184, "FRAMES",
                     UINT16_C(0xBDF7), 1U, 6U);
    } else {
        p4_draw_text(surface, 8, 156, "A TONE  B MOTION  START RESET",
                     UINT16_C(0xFFFF), 1U, 31U);
    }
    char count[11];
    const size_t count_length = format_unsigned(state->frame_count, count);
    p4_draw_text(surface, native ? 151 : 244, native ? 184 : 177,
                 count, UINT16_C(0xFFE0), 1U,
                 count_length);
    p4_game_draw_standard_controls(
        surface, UINT16_C(0x7BEF), UINT16_C(0xF81F),
        state->held_buttons);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_av_test_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(111),
    .id = "org.p4console.av-test",
    .title = "Sound & Motion",
    .subtitle = "Check screen motion and tones",
    .accent_rgb565 = UINT16_C(0xF81F),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                             P4_GAME_CAP_VIDEO_HIGH_RES,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE,
    .state_bytes = sizeof(av_test_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
