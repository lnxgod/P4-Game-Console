// SPDX-License-Identifier: MIT
/* Original integer desk calculator for P4 Game API v1. */

#include "p4_games/calculator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "calculator_internal.h"
#include "p4/draw.h"
#include "p4/input.h"

enum {
    CALCULATOR_MAX = 999999999,
    KEY_COLUMNS = 4,
    KEY_ROWS = 4,
    KEY_COUNT = KEY_COLUMNS * KEY_ROWS,
    KEY_LEFT = 12,
    KEY_TOP = 59,
    KEY_WIDTH = 44,
    KEY_HEIGHT = 20,
    KEY_GAP = 4,
};

static const char *const s_key_labels[KEY_COUNT] = {
    "7", "8", "9", "/",
    "4", "5", "6", "*",
    "1", "2", "3", "-",
    "C", "0", "=", "+",
};

static int32_t absolute_value(int32_t value)
{
    return value < 0 ? -value : value;
}

void p4_calculator_reset(p4_calculator_state_t *state)
{
    if (state != NULL) {
        *state = (p4_calculator_state_t){0};
    }
}

bool p4_calculator_digit(p4_calculator_state_t *state, uint8_t digit)
{
    if (state == NULL || digit > 9U) {
        return false;
    }
    if (state->error) {
        p4_calculator_reset(state);
    }
    if (!state->entering) {
        state->entry = 0;
        state->entering = true;
    }
    if (state->entry > (CALCULATOR_MAX - (int32_t)digit) / 10) {
        state->error = true;
        return false;
    }
    state->entry = state->entry * 10 + (int32_t)digit;
    return true;
}

static bool apply_operation(int32_t left, int32_t right,
                            p4_calculator_operation_t operation,
                            int32_t *result)
{
    if (result == NULL) {
        return false;
    }
    switch (operation) {
    case P4_CALC_ADD:
        if ((right > 0 && left > CALCULATOR_MAX - right) ||
            (right < 0 && left < -CALCULATOR_MAX - right)) {
            return false;
        }
        *result = left + right;
        return true;
    case P4_CALC_SUBTRACT:
        if ((right < 0 && left > CALCULATOR_MAX + right) ||
            (right > 0 && left < -CALCULATOR_MAX + right)) {
            return false;
        }
        *result = left - right;
        return true;
    case P4_CALC_MULTIPLY:
        if (left != 0 && absolute_value(right) >
                CALCULATOR_MAX / absolute_value(left)) {
            return false;
        }
        *result = left * right;
        return true;
    case P4_CALC_DIVIDE:
        if (right == 0) {
            return false;
        }
        *result = left / right;
        return true;
    case P4_CALC_NONE:
    default:
        *result = right;
        return true;
    }
}

bool p4_calculator_choose_operation(
    p4_calculator_state_t *state, p4_calculator_operation_t operation)
{
    if (state == NULL || operation == P4_CALC_NONE || state->error) {
        return false;
    }
    if (state->pending != P4_CALC_NONE && state->entering) {
        int32_t result = 0;
        if (!apply_operation(state->accumulator, state->entry,
                             state->pending, &result)) {
            state->error = true;
            return false;
        }
        state->accumulator = result;
        state->entry = result;
    } else if (state->pending == P4_CALC_NONE) {
        state->accumulator = state->entry;
    }
    state->pending = operation;
    state->entering = false;
    return true;
}

bool p4_calculator_equals(p4_calculator_state_t *state)
{
    if (state == NULL || state->error) {
        return false;
    }
    if (state->pending == P4_CALC_NONE) {
        state->accumulator = state->entry;
        state->entering = false;
        return true;
    }
    const int32_t right = state->entering
        ? state->entry : state->accumulator;
    int32_t result = 0;
    if (!apply_operation(state->accumulator, right,
                         state->pending, &result)) {
        state->error = true;
        return false;
    }
    state->accumulator = result;
    state->entry = result;
    state->pending = P4_CALC_NONE;
    state->entering = false;
    return true;
}

void p4_calculator_backspace(p4_calculator_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->error) {
        p4_calculator_reset(state);
        return;
    }
    if (state->entering) {
        state->entry /= 10;
    }
}

size_t p4_calculator_format(
    const p4_calculator_state_t *state, char *output, size_t output_bytes)
{
    if (state == NULL || output == NULL || output_bytes == 0U) {
        return 0U;
    }
    if (state->error) {
        static const char error[] = "ERROR";
        const size_t count = sizeof(error) <= output_bytes
            ? sizeof(error) - 1U : output_bytes - 1U;
        memcpy(output, error, count);
        output[count] = '\0';
        return count;
    }
    const int32_t value = state->entry;
    uint32_t magnitude = (uint32_t)absolute_value(value);
    char reverse[12];
    size_t digits = 0U;
    do {
        reverse[digits++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U && digits < sizeof(reverse));
    if (value < 0 && digits < sizeof(reverse)) {
        reverse[digits++] = '-';
    }
    const size_t count = digits < output_bytes ? digits : output_bytes - 1U;
    for (size_t index = 0U; index < count; ++index) {
        output[index] = reverse[digits - index - 1U];
    }
    output[count] = '\0';
    return count;
}

static void play_key_tone(p4_game_context_t *context, bool accepted)
{
    (void)p4_game_play_tone(
        context, accepted ? 660U : 150U, accepted ? 35U : 80U,
        accepted ? 2U : 3U, P4_WAVE_TRIANGLE);
}

static bool activate_key(p4_game_context_t *context,
                         p4_calculator_state_t *state, uint8_t key)
{
    bool accepted = false;
    if (key < 3U) {
        accepted = p4_calculator_digit(state, (uint8_t)(7U + key));
    } else if (key == 3U) {
        accepted = p4_calculator_choose_operation(state, P4_CALC_DIVIDE);
    } else if (key < 7U) {
        accepted = p4_calculator_digit(state, (uint8_t)(key));
    } else if (key == 7U) {
        accepted = p4_calculator_choose_operation(state, P4_CALC_MULTIPLY);
    } else if (key < 11U) {
        accepted = p4_calculator_digit(state, (uint8_t)(key - 7U));
    } else if (key == 11U) {
        accepted = p4_calculator_choose_operation(state, P4_CALC_SUBTRACT);
    } else if (key == 12U) {
        p4_calculator_reset(state);
        accepted = true;
    } else if (key == 13U) {
        accepted = p4_calculator_digit(state, 0U);
    } else if (key == 14U) {
        accepted = p4_calculator_equals(state);
    } else if (key == 15U) {
        accepted = p4_calculator_choose_operation(state, P4_CALC_ADD);
    }
    play_key_tone(context, accepted);
    return accepted;
}

static int touch_key(const p4_game_input_t *input)
{
    if (!input->touch_valid || input->touch_count == 0U) {
        return -1;
    }
    const int x = (int)input->touches[0].x;
    const int y = (int)input->touches[0].y;
    if (x < KEY_LEFT || y < KEY_TOP) {
        return -1;
    }
    const int column = (x - KEY_LEFT) / (KEY_WIDTH + KEY_GAP);
    const int row = (y - KEY_TOP) / (KEY_HEIGHT + KEY_GAP);
    if (column < 0 || column >= KEY_COLUMNS || row < 0 || row >= KEY_ROWS) {
        return -1;
    }
    const int local_x = (x - KEY_LEFT) % (KEY_WIDTH + KEY_GAP);
    const int local_y = (y - KEY_TOP) % (KEY_HEIGHT + KEY_GAP);
    if (local_x >= KEY_WIDTH || local_y >= KEY_HEIGHT) {
        return -1;
    }
    return row * KEY_COLUMNS + column;
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(p4_calculator_state_t)) {
        return false;
    }
    p4_calculator_reset(context->state);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context, const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    (void)elapsed_ms;
    p4_calculator_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_LEFT) != 0U &&
        state->cursor % KEY_COLUMNS > 0U) {
        --state->cursor;
    }
    if ((input->pressed & P4_BUTTON_RIGHT) != 0U &&
        state->cursor % KEY_COLUMNS + 1U < KEY_COLUMNS) {
        ++state->cursor;
    }
    if ((input->pressed & P4_BUTTON_UP) != 0U &&
        state->cursor >= KEY_COLUMNS) {
        state->cursor = (uint8_t)(state->cursor - KEY_COLUMNS);
    }
    if ((input->pressed & P4_BUTTON_DOWN) != 0U &&
        state->cursor + KEY_COLUMNS < KEY_COUNT) {
        state->cursor = (uint8_t)(state->cursor + KEY_COLUMNS);
    }
    if ((input->pressed & P4_BUTTON_A) != 0U) {
        (void)activate_key(context, state, state->cursor);
    }
    if ((input->pressed & P4_BUTTON_B) != 0U) {
        p4_calculator_backspace(state);
        play_key_tone(context, true);
    }
    if ((input->pressed & P4_BUTTON_START) != 0U) {
        (void)activate_key(context, state, 14U);
    }
    const bool touch_now = input->touch_valid && input->touch_count > 0U;
    if (touch_now && !state->touch_down) {
        const int key = touch_key(input);
        if (key >= 0) {
            state->cursor = (uint8_t)key;
            (void)activate_key(context, state, state->cursor);
        }
    }
    state->touch_down = touch_now;
    return P4_GAME_CONTINUE;
}

static const char *operation_label(p4_calculator_operation_t operation)
{
    switch (operation) {
    case P4_CALC_ADD: return "+";
    case P4_CALC_SUBTRACT: return "-";
    case P4_CALC_MULTIPLY: return "*";
    case P4_CALC_DIVIDE: return "/";
    case P4_CALC_NONE:
    default: return "";
    }
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const p4_calculator_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x18C3));
    p4_draw_fill_rect(surface, 0, 0, 320, 18, UINT16_C(0x0010));
    p4_draw_text(surface, 9, 6, "CALCULATOR", UINT16_C(0xFFE0), 1U, 10U);
    p4_draw_text(surface, 232, 6, "BACK EXITS", UINT16_C(0xBDF7), 1U, 10U);

    p4_draw_fill_rect(surface, 12, 24, 296, 28, UINT16_C(0x0000));
    p4_draw_rect(surface, 12, 24, 296, 28, UINT16_C(0xFFFF));
    char value[16];
    (void)p4_calculator_format(state, value, sizeof(value));
    const size_t length = strlen(value);
    const int value_x = 296 - (int)length * 12;
    p4_draw_text(surface, value_x, 31, value,
                 state->error ? UINT16_C(0xF800) : UINT16_C(0xFFFF),
                 2U, length);
    p4_draw_text(surface, 18, 33, operation_label(state->pending),
                 UINT16_C(0xFFE0), 2U, 1U);

    for (uint8_t key = 0U; key < KEY_COUNT; ++key) {
        const int column = (int)(key % KEY_COLUMNS);
        const int row = (int)(key / KEY_COLUMNS);
        const int x = KEY_LEFT + column * (KEY_WIDTH + KEY_GAP);
        const int y = KEY_TOP + row * (KEY_HEIGHT + KEY_GAP);
        const bool selected = key == state->cursor;
        const bool operation = column == 3 || key == 14U;
        const uint16_t fill = operation
            ? UINT16_C(0x3186) : UINT16_C(0x7BEF);
        p4_draw_fill_rect(surface, x, y, KEY_WIDTH, KEY_HEIGHT, fill);
        p4_draw_rect(surface, x, y, KEY_WIDTH, KEY_HEIGHT,
                     selected ? UINT16_C(0xFFE0) : UINT16_C(0xFFFF));
        p4_draw_text(surface, x + 18, y + 7, s_key_labels[key],
                     UINT16_C(0x0000), 1U, 1U);
    }
    p4_draw_text(surface, 214, 67, "DPAD MOVE", UINT16_C(0xFFFF), 1U, 9U);
    p4_draw_text(surface, 214, 82, "A SELECT", UINT16_C(0xFFFF), 1U, 8U);
    p4_draw_text(surface, 214, 97, "B ERASE", UINT16_C(0xFFFF), 1U, 7U);
    p4_draw_text(surface, 214, 112, "START =", UINT16_C(0xFFFF), 1U, 7U);
    p4_draw_text(surface, 214, 127, "TAP KEYS", UINT16_C(0x07FF), 1U, 8U);
    p4_game_draw_standard_controls(
        surface, UINT16_C(0xBDF7), UINT16_C(0xFFE0),
        state->held_buttons);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_calculator_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(109),
    .id = "org.p4console.calculator",
    .title = "CALCULATOR",
    .subtitle = "INTEGER DESK CALCULATOR",
    .accent_rgb565 = UINT16_C(0xFFE0),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE,
    .state_bytes = sizeof(p4_calculator_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
