// SPDX-License-Identifier: MIT
/* Normalized button and touch monitor for P4 Game API v1. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"
#include "p4/card_art.h"

/* Draw the canonical layout directly at native resolution; touch points remain
 * canonical input units. Legacy diagnostic surfaces retain their own path. */
#define p4_draw_fill_rect p4_card_fill
#define p4_draw_rect p4_card_outline
#define p4_draw_fill_circle p4_card_circle
#define p4_draw_text p4_card_text

typedef struct {
    p4_game_input_t input;
    uint32_t event_count;
    uint32_t frame_count;
} input_test_state_t;

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(input_test_state_t)) {
        return false;
    }
    *(input_test_state_t *)context->state = (input_test_state_t){0};
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context, const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    (void)elapsed_ms;
    input_test_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    if ((input->pressed & P4_BUTTON_START) != 0U) {
        state->event_count = 0U;
    }
    if ((input->pressed | input->released) != 0U ||
        input->touch_count != state->input.touch_count) {
        if (state->event_count != UINT32_MAX) {
            ++state->event_count;
        }
    }
    state->input = *input;
    if (state->frame_count != UINT32_MAX) {
        ++state->frame_count;
    }
    return P4_GAME_CONTINUE;
}

static void format_hex(uint32_t value, char output[9])
{
    static const char digits[] = "0123456789ABCDEF";
    for (size_t index = 0U; index < 8U; ++index) {
        const unsigned shift = (unsigned)((7U - index) * 4U);
        output[index] = digits[(value >> shift) & UINT32_C(0x0F)];
    }
    output[8] = '\0';
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

static void draw_button(p4_game_surface_t *surface, int x, int y,
                        const char *label, uint32_t mask,
                        const input_test_state_t *state)
{
    const bool held = (state->input.held & mask) != 0U;
    const bool pressed = (state->input.pressed & mask) != 0U;
    const bool released = (state->input.released & mask) != 0U;
    uint16_t fill = UINT16_C(0x2104);
    if (held) {
        fill = UINT16_C(0x07E0);
    } else if (pressed) {
        fill = UINT16_C(0xFFE0);
    } else if (released) {
        fill = UINT16_C(0xF800);
    }
    p4_draw_fill_rect(surface, x, y, 68, 22, fill);
    p4_draw_rect(surface, x, y, 68, 22, UINT16_C(0xFFFF));
    p4_draw_text(surface, x + 7, y + 8, label,
                 held ? UINT16_C(0x0000) : UINT16_C(0xFFFF), 1U, 7U);
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const input_test_state_t *const state = context->state;
    const bool native = surface->width == P4_GAME_SURFACE_HIGH_RES_WIDTH;
    p4_draw_clear(surface, UINT16_C(0x0008));
    p4_draw_fill_rect(surface, 0, 0, 320, 19, UINT16_C(0x001F));
    p4_draw_text(surface, native ? 109 : 8, 6, "INPUT MONITOR",
                 UINT16_C(0xFFFF), 1U, 13U);
    if (!native) {
        p4_draw_text(surface, 222, 6, "BACK EXITS",
                     UINT16_C(0xBDF7), 1U, 10U);
    }

    static const struct {
        const char *label;
        uint32_t mask;
    } buttons[8] = {
        {"UP", P4_BUTTON_UP}, {"DOWN", P4_BUTTON_DOWN},
        {"LEFT", P4_BUTTON_LEFT}, {"RIGHT", P4_BUTTON_RIGHT},
        {"A", P4_BUTTON_A}, {"B", P4_BUTTON_B},
        {"START", P4_BUTTON_START}, {"BACK", P4_BUTTON_BACK},
    };
    for (size_t index = 0U; index < 8U; ++index) {
        const int column = (int)(index % 2U);
        const int row = (int)(index / 2U);
        draw_button(surface, 8 + column * 74, 27 + row * 27,
                    buttons[index].label, buttons[index].mask, state);
    }

    char hex[9];
    format_hex(state->input.held, hex);
    p4_draw_text(surface, 160, 29, "HELD", UINT16_C(0x07FF), 1U, 4U);
    p4_draw_text(surface, 206, 29, hex, UINT16_C(0xFFFF), 1U, 8U);
    format_hex(state->input.pressed, hex);
    p4_draw_text(surface, 160, 45, "PRESS", UINT16_C(0xFFE0), 1U, 5U);
    p4_draw_text(surface, 206, 45, hex, UINT16_C(0xFFFF), 1U, 8U);
    format_hex(state->input.released, hex);
    p4_draw_text(surface, 160, 61, "REL", UINT16_C(0xF800), 1U, 3U);
    p4_draw_text(surface, 206, 61, hex, UINT16_C(0xFFFF), 1U, 8U);

    char number[11];
    size_t length = format_unsigned(state->event_count, number);
    p4_draw_text(surface, 160, 82, "EVENTS", UINT16_C(0x07FF), 1U, 6U);
    p4_draw_text(surface, 220, 82, number, UINT16_C(0xFFFF), 1U, length);
    length = format_unsigned(state->input.touch_count, number);
    p4_draw_text(surface, 160, 98, "TOUCHES", UINT16_C(0x07FF), 1U, 7U);
    p4_draw_text(surface, 226, 98, number, UINT16_C(0xFFFF), 1U, length);

    for (uint8_t index = 0U; index < state->input.touch_count && index < 5U;
         ++index) {
        const int x = (int)state->input.touches[index].x;
        const int y = (int)state->input.touches[index].y;
        p4_draw_fill_circle(surface, x, y, 8, UINT16_C(0xF81F));
        p4_draw_fill_circle(surface, x, y, 5, UINT16_C(0x0008));
        p4_draw_fill_circle(surface, x, y, 2, UINT16_C(0xFFFF));
    }
    if (native) {
        p4_draw_text(surface, 160, 116, "START CLEARS",
                     UINT16_C(0xBDF7), 1U, 12U);
        p4_draw_text(surface, 160, 129, "EVENT COUNT",
                     UINT16_C(0xBDF7), 1U, 11U);
    } else {
        p4_draw_text(surface, 8, 141, "START CLEARS EVENT COUNT",
                     UINT16_C(0xBDF7), 1U, 24U);
    }
    p4_game_draw_standard_controls(
        surface, UINT16_C(0x7BEF), UINT16_C(0x07FF),
        state->input.held);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_input_test_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(110),
    .id = "org.p4console.input-test",
    .title = "Input Monitor",
    .subtitle = "Check buttons and touch",
    .accent_rgb565 = UINT16_C(0x07FF),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                             P4_GAME_CAP_VIDEO_HIGH_RES,
    .optional_capabilities = 0U,
    .state_bytes = sizeof(input_test_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
