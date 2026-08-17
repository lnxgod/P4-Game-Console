// SPDX-License-Identifier: MIT
/* Original clean-room Breakout game for P4 Game API v1. */

#include "p4_games/breakout.h"

#include <stddef.h>
#include <stdint.h>

#include "breakout_internal.h"
#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/input.h"

enum {
    BREAKOUT_FIELD_LEFT = 8,
    BREAKOUT_FIELD_RIGHT = 312,
    BREAKOUT_FIELD_TOP = 28,
    BREAKOUT_FIELD_BOTTOM = 194,
    BREAKOUT_BRICK_LEFT = 32,
    BREAKOUT_BRICK_TOP = 40,
    BREAKOUT_BRICK_WIDTH = 22,
    BREAKOUT_BRICK_HEIGHT = 6,
    BREAKOUT_BRICK_GAP = 4,
    BREAKOUT_PADDLE_Y = 178,
    BREAKOUT_PADDLE_WIDTH = 50,
    BREAKOUT_PADDLE_HEIGHT = 5,
    BREAKOUT_BALL_RADIUS = 3,
    BREAKOUT_FIXED_STEP_MS = 16,
    BREAKOUT_PADDLE_SPEED = 4,
};

static const uint16_t s_brick_colors[BREAKOUT_ROWS] = {
    UINT16_C(0xf800), UINT16_C(0xfd20), UINT16_C(0xffe0),
    UINT16_C(0x07e0), UINT16_C(0x07ff),
};

static bool brick_alive(const breakout_state_t *state, unsigned index)
{
    return (state->bricks & (UINT64_C(1) << index)) != 0U;
}

static void clear_brick(breakout_state_t *state, unsigned index)
{
    state->bricks &= ~(UINT64_C(1) << index);
}

static void add_score(breakout_state_t *state, uint32_t amount)
{
    if (UINT32_MAX - state->score < amount) {
        state->score = UINT32_MAX;
    } else {
        state->score += amount;
    }
}

static void clamp_paddle(breakout_state_t *state)
{
    const int minimum = BREAKOUT_FIELD_LEFT + BREAKOUT_PADDLE_WIDTH / 2;
    const int maximum = BREAKOUT_FIELD_RIGHT - BREAKOUT_PADDLE_WIDTH / 2;
    if (state->paddle_x < minimum) {
        state->paddle_x = (int16_t)minimum;
    } else if (state->paddle_x > maximum) {
        state->paddle_x = (int16_t)maximum;
    }
}

static void reset_ball(breakout_state_t *state)
{
    state->ball_x = 160;
    state->ball_y = 163;
    state->ball_dx = state->score % 2U == 0U ? 2 : -2;
    state->ball_dy = -3;
}

void breakout_reset(breakout_state_t *state)
{
    if (state == NULL) {
        return;
    }
    *state = (breakout_state_t){
        .bricks = (UINT64_C(1) << BREAKOUT_BRICK_COUNT) - 1U,
        .lives = 3U,
        .paddle_x = 160,
    };
    reset_ball(state);
}

static void play_tone(p4_game_context_t *context,
                      uint16_t frequency_hz,
                      uint16_t duration_ms,
                      uint8_t volume_step,
                      p4_waveform_t waveform)
{
    (void)p4_game_play_tone(context, frequency_hz, duration_ms,
                            volume_step, waveform);
}

static void lose_ball(p4_game_context_t *context, breakout_state_t *state)
{
    if (state->lives != 0U) {
        --state->lives;
    }
    play_tone(context, 110U, 220U, 5U, P4_WAVE_TRIANGLE);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
    if (state->lives == 0U) {
        state->game_over = true;
    } else {
        reset_ball(state);
    }
}

static bool point_in_rect(int x, int y, int left, int top,
                          int width, int height)
{
    return x >= left && x < left + width &&
        y >= top && y < top + height;
}

static void move_paddle_from_touch(breakout_state_t *state,
                                   const p4_game_input_t *input)
{
    if (!input->touch_valid) {
        return;
    }
    const size_t touch_count = input->touch_count > P4_INPUT_MAX_TOUCHES
        ? P4_INPUT_MAX_TOUCHES : input->touch_count;
    for (size_t index = 0U; index < touch_count; ++index) {
        const p4_game_point_t *const touch = &input->touches[index];
        if (touch->y >= BREAKOUT_FIELD_TOP && touch->y < 194U) {
            state->paddle_x = (int16_t)touch->x;
            clamp_paddle(state);
            return;
        }
    }
}

static void move_paddle(breakout_state_t *state)
{
    const bool left = (state->held_buttons & P4_BUTTON_LEFT) != 0U;
    const bool right = (state->held_buttons & P4_BUTTON_RIGHT) != 0U;
    if (left == right) {
        return;
    }
    const int direction = left ? -1 : 1;
    state->paddle_x = (int16_t)(state->paddle_x +
        direction * BREAKOUT_PADDLE_SPEED);
    clamp_paddle(state);
}

static void bounce_off_walls(breakout_state_t *state)
{
    const int minimum_x = BREAKOUT_FIELD_LEFT + BREAKOUT_BALL_RADIUS;
    const int maximum_x = BREAKOUT_FIELD_RIGHT - BREAKOUT_BALL_RADIUS;
    const int minimum_y = BREAKOUT_FIELD_TOP + BREAKOUT_BALL_RADIUS;
    if (state->ball_x <= minimum_x) {
        state->ball_x = (int16_t)minimum_x;
        if (state->ball_dx < 0) {
            state->ball_dx = (int8_t)-state->ball_dx;
        }
    } else if (state->ball_x >= maximum_x) {
        state->ball_x = (int16_t)maximum_x;
        if (state->ball_dx > 0) {
            state->ball_dx = (int8_t)-state->ball_dx;
        }
    }
    if (state->ball_y <= minimum_y) {
        state->ball_y = (int16_t)minimum_y;
        if (state->ball_dy < 0) {
            state->ball_dy = (int8_t)-state->ball_dy;
        }
    }
}

static bool hit_paddle(breakout_state_t *state)
{
    if (state->ball_dy <= 0 ||
        state->ball_y + BREAKOUT_BALL_RADIUS < BREAKOUT_PADDLE_Y ||
        state->ball_y - BREAKOUT_BALL_RADIUS >=
            BREAKOUT_PADDLE_Y + BREAKOUT_PADDLE_HEIGHT ||
        !point_in_rect(state->ball_x, BREAKOUT_PADDLE_Y,
                       state->paddle_x - BREAKOUT_PADDLE_WIDTH / 2,
                       BREAKOUT_PADDLE_Y,
                       BREAKOUT_PADDLE_WIDTH, BREAKOUT_PADDLE_HEIGHT)) {
        return false;
    }
    state->ball_y = (int16_t)(BREAKOUT_PADDLE_Y - BREAKOUT_BALL_RADIUS - 1);
    state->ball_dy = -3;
    const int offset = state->ball_x - state->paddle_x;
    int next_dx = offset / 8;
    if (next_dx < -3) {
        next_dx = -3;
    } else if (next_dx > 3) {
        next_dx = 3;
    }
    if (next_dx == 0) {
        next_dx = state->ball_dx < 0 ? -1 : 1;
    }
    state->ball_dx = (int8_t)next_dx;
    return true;
}

static bool hit_brick(p4_game_context_t *context, breakout_state_t *state)
{
    for (unsigned row = 0U; row < BREAKOUT_ROWS; ++row) {
        for (unsigned column = 0U; column < BREAKOUT_COLUMNS; ++column) {
            const unsigned index = row * BREAKOUT_COLUMNS + column;
            if (!brick_alive(state, index)) {
                continue;
            }
            const int left = BREAKOUT_BRICK_LEFT +
                (int)column * (BREAKOUT_BRICK_WIDTH + BREAKOUT_BRICK_GAP);
            const int top = BREAKOUT_BRICK_TOP +
                (int)row * (BREAKOUT_BRICK_HEIGHT + BREAKOUT_BRICK_GAP);
            if (state->ball_x + BREAKOUT_BALL_RADIUS < left ||
                state->ball_x - BREAKOUT_BALL_RADIUS >=
                    left + BREAKOUT_BRICK_WIDTH ||
                state->ball_y + BREAKOUT_BALL_RADIUS < top ||
                state->ball_y - BREAKOUT_BALL_RADIUS >=
                    top + BREAKOUT_BRICK_HEIGHT) {
                continue;
            }
            clear_brick(state, index);
            add_score(state, 10U);
            state->ball_dy = (int8_t)-state->ball_dy;
            play_tone(context, (uint16_t)(320U + row * 55U),
                      45U, 3U, P4_WAVE_SQUARE);
            if (state->bricks == 0U) {
                state->won = true;
                play_tone(context, 1047U, 300U, 5U, P4_WAVE_TRIANGLE);
                (void)p4_game_audio_effect_play(
                    context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
            } else {
                (void)p4_game_audio_effect_play(
                    context, &state->audio, P4_GAME_AUDIO_EFFECT_IMPACT);
            }
            return true;
        }
    }
    return false;
}

static void simulate_step(p4_game_context_t *context,
                           breakout_state_t *state)
{
    move_paddle(state);
    state->ball_x = (int16_t)(state->ball_x + state->ball_dx);
    state->ball_y = (int16_t)(state->ball_y + state->ball_dy);
    bounce_off_walls(state);
    if (hit_paddle(state)) {
        play_tone(context, 220U, 30U, 3U, P4_WAVE_SQUARE);
    }
    (void)hit_brick(context, state);
    if (state->ball_y - BREAKOUT_BALL_RADIUS > BREAKOUT_FIELD_BOTTOM) {
        lose_ball(context, state);
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(breakout_state_t)) {
        return false;
    }
    breakout_reset(context->state);
    play_tone(context, 523U, 100U, 4U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(p4_game_context_t *context,
                                    const p4_game_input_t *input,
                                    uint32_t elapsed_ms)
{
    breakout_state_t *const state = context->state;
    (void)p4_game_audio_effect_service(context, &state->audio);
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    if (state->game_over || state->won) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U ||
            (input->touch_valid && input->touch_count != 0U)) {
            breakout_reset(state);
            play_tone(context, 523U, 100U, 4U, P4_WAVE_TRIANGLE);
        }
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_START) != 0U) {
        state->paused = !state->paused;
        play_tone(context, state->paused ? 330U : 660U,
                  80U, 3U, P4_WAVE_SQUARE);
    }
    if (state->paused) {
        return P4_GAME_CONTINUE;
    }
    move_paddle_from_touch(state, input);
    if (UINT32_MAX - state->simulation_accumulator_ms < elapsed_ms) {
        state->simulation_accumulator_ms = UINT32_MAX;
    } else {
        state->simulation_accumulator_ms += elapsed_ms;
    }
    while (state->simulation_accumulator_ms >= BREAKOUT_FIXED_STEP_MS) {
        state->simulation_accumulator_ms -= BREAKOUT_FIXED_STEP_MS;
        simulate_step(context, state);
        if (state->game_over || state->won) {
            break;
        }
    }
    return P4_GAME_CONTINUE;
}

static void u32_text(uint32_t value, char output[11])
{
    char reversed[10];
    size_t count = 0U;
    do {
        reversed[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(reversed));
    for (size_t index = 0U; index < count; ++index) {
        output[index] = reversed[count - index - 1U];
    }
    output[count] = '\0';
}

static void draw_hud(p4_game_surface_t *surface,
                     const breakout_state_t *state)
{
    char value[11];
    p4_draw_text(surface, 58, 7, "S", UINT16_C(0x9cf3), 1U, 1U);
    u32_text(state->score, value);
    p4_draw_text(surface, 66, 7, value, UINT16_C(0xffff), 1U, 10U);
    p4_draw_text(surface, 125, 7, "BREAKOUT", UINT16_C(0xfbe0), 1U, 8U);
    p4_draw_text(surface, 215, 7, "L", UINT16_C(0x9cf3), 1U, 1U);
    u32_text(state->lives, value);
    p4_draw_text(surface, 223, 7, value, UINT16_C(0xffff), 1U, 2U);
}

static void draw_bricks(p4_game_surface_t *surface,
                        const breakout_state_t *state)
{
    for (unsigned row = 0U; row < BREAKOUT_ROWS; ++row) {
        for (unsigned column = 0U; column < BREAKOUT_COLUMNS; ++column) {
            const unsigned index = row * BREAKOUT_COLUMNS + column;
            if (!brick_alive(state, index)) {
                continue;
            }
            const int left = BREAKOUT_BRICK_LEFT +
                (int)column * (BREAKOUT_BRICK_WIDTH + BREAKOUT_BRICK_GAP);
            const int top = BREAKOUT_BRICK_TOP +
                (int)row * (BREAKOUT_BRICK_HEIGHT + BREAKOUT_BRICK_GAP);
            p4_draw_fill_rect(surface, left, top,
                              BREAKOUT_BRICK_WIDTH, BREAKOUT_BRICK_HEIGHT,
                              s_brick_colors[row]);
            p4_draw_rect(surface, left, top,
                         BREAKOUT_BRICK_WIDTH, BREAKOUT_BRICK_HEIGHT,
                         UINT16_C(0xffff));
        }
    }
}

static void draw_top_controls(p4_game_surface_t *surface,
                              uint32_t held_buttons)
{
    const uint16_t exit_color =
        (held_buttons & P4_BUTTON_BACK) != 0U
            ? UINT16_C(0xfbe0) : UINT16_C(0x4208);
    const uint16_t start_color =
        (held_buttons & P4_BUTTON_START) != 0U
            ? UINT16_C(0xfbe0) : UINT16_C(0x4208);
    p4_draw_rect(surface, 0, 0, 52, 24, exit_color);
    p4_draw_text(surface, 8, 8, "EXIT", exit_color, 1U, 4U);
    p4_draw_rect(surface, 268, 0, 52, 24, start_color);
    p4_draw_text(surface, 276, 8, "START", start_color, 1U, 5U);
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const breakout_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x0821));
    draw_hud(surface, state);
    p4_draw_fill_rect(surface, BREAKOUT_FIELD_LEFT, BREAKOUT_FIELD_TOP,
                      BREAKOUT_FIELD_RIGHT - BREAKOUT_FIELD_LEFT,
                      BREAKOUT_FIELD_BOTTOM - BREAKOUT_FIELD_TOP,
                      UINT16_C(0x0000));
    p4_draw_rect(surface, BREAKOUT_FIELD_LEFT, BREAKOUT_FIELD_TOP,
                 BREAKOUT_FIELD_RIGHT - BREAKOUT_FIELD_LEFT,
                 BREAKOUT_FIELD_BOTTOM - BREAKOUT_FIELD_TOP,
                 UINT16_C(0x4208));
    draw_bricks(surface, state);
    p4_game_feedback_draw(
        surface, state->game_over ? P4_GAME_FX_FAIL :
        (state->won ? P4_GAME_FX_REWARD : P4_GAME_FX_ACTION),
        state->game_over || state->won ? 160 : state->ball_x,
        state->game_over || state->won ? 88 : state->ball_y,
        context->frame_index);
    p4_draw_fill_rect(surface,
                      state->paddle_x - BREAKOUT_PADDLE_WIDTH / 2,
                      BREAKOUT_PADDLE_Y, BREAKOUT_PADDLE_WIDTH,
                      BREAKOUT_PADDLE_HEIGHT, UINT16_C(0x07ff));
    p4_draw_fill_circle(surface, state->ball_x, state->ball_y,
                        BREAKOUT_BALL_RADIUS, UINT16_C(0xffe0));
    draw_top_controls(surface, state->held_buttons);
    p4_draw_text(surface, 122, 18, "TOUCH TO MOVE",
                 UINT16_C(0x9cf3), 1U, 13U);

    if (state->paused || state->game_over || state->won) {
        p4_draw_fill_rect(surface, 103, 69, 114, 38, UINT16_C(0x1082));
        p4_draw_rect(surface, 103, 69, 114, 38, UINT16_C(0xffff));
        const char *const message = state->game_over ? "GAME OVER" :
            (state->won ? "BRICK CLEAR" : "PAUSED");
        const int message_x = state->game_over ? 124 :
            (state->won ? 121 : 136);
        p4_draw_text(surface, message_x, 78, message,
                     UINT16_C(0xffff), 1U, 10U);
        p4_draw_text(surface, state->paused ? 118 : 116, 93,
                     state->paused ? "START TO RESUME" : "A TO RESTART",
                     UINT16_C(0x9cf3), 1U, 15U);
    }
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_breakout_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(102),
    .id = "org.p4console.breakout",
    .title = "BREAKOUT",
    .subtitle = "TOUCH BRICK BREAKER",
    .accent_rgb565 = UINT16_C(0xfbe0),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM,
    .state_bytes = sizeof(breakout_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
