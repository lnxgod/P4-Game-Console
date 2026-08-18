// SPDX-License-Identifier: MIT
/*
 * Original clean-room P4 interpretation of a peg-launch puzzle game.
 * No GameChangers source code, art, audio, or level data is included here.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/audio_pack.h"
#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/game.h"
#include "p4/input.h"

enum {
    ORBLE_LEVEL_COUNT = 3,
    ORBLE_TARGET_COUNT = 12,
    ORBLE_FIELD_LEFT = 88,
    ORBLE_FIELD_RIGHT = 232,
    ORBLE_FIELD_TOP = 28,
    ORBLE_FIELD_BOTTOM = 128,
    ORBLE_BALL_RADIUS = 4,
    ORBLE_BUCKET_Y = 119,
    ORBLE_BUCKET_WIDTH = 46,
    ORBLE_FIXED_STEP_MS = 16,
    ORBLE_MAX_BALLS = 6,
};

typedef struct {
    int16_t ball_x;
    int16_t ball_y;
    int8_t ball_vx;
    int8_t ball_vy;
    int8_t aim;
    int16_t bucket_x;
    int8_t bucket_velocity;
    int16_t feedback_x;
    int16_t feedback_y;
    uint16_t targets;
    uint16_t score;
    uint32_t held_buttons;
    uint32_t simulation_ms;
    uint8_t level;
    uint8_t balls;
    uint8_t gravity_steps;
    bool ball_flying;
    bool won;
    bool game_over;
    p4_game_audio_effect_player_t audio;
} orble_state_t;

static const int16_t s_target_x[ORBLE_LEVEL_COUNT][ORBLE_TARGET_COUNT] = {
    {108, 132, 156, 180, 204, 120, 144, 168, 192, 132, 160, 188},
    {102, 126, 150, 174, 198, 114, 138, 162, 186, 126, 162, 198},
    {108, 136, 164, 192, 120, 148, 176, 204, 108, 136, 164, 192},
};

static const int16_t s_target_y[ORBLE_LEVEL_COUNT][ORBLE_TARGET_COUNT] = {
    {48, 54, 44, 54, 48, 72, 78, 70, 78, 96, 92, 98},
    {46, 40, 52, 40, 46, 68, 76, 64, 76, 98, 90, 98},
    {42, 58, 42, 58, 76, 70, 76, 70, 100, 94, 100, 94},
};

static int absolute_value(int value)
{
    return value < 0 ? -value : value;
}

static uint16_t target_mask(void)
{
    return (uint16_t)((UINT16_C(1) << ORBLE_TARGET_COUNT) - UINT16_C(1));
}

static void reset_ball(orble_state_t *state)
{
    state->ball_x = 160;
    state->ball_y = 36;
    state->ball_vx = 0;
    state->ball_vy = 1;
    state->gravity_steps = 0U;
    state->ball_flying = false;
}

static void begin_level(orble_state_t *state, uint8_t level)
{
    state->level = level;
    state->targets = target_mask();
    state->balls = ORBLE_MAX_BALLS;
    state->bucket_x = 160;
    state->bucket_velocity = 2;
    reset_ball(state);
}

static void reset_campaign(orble_state_t *state)
{
    *state = (orble_state_t){
        .aim = 0,
        .feedback_x = 160,
        .feedback_y = 82,
    };
    begin_level(state, 0U);
}

static void play_tone(p4_game_context_t *context, uint16_t frequency_hz,
                      uint16_t duration_ms, uint8_t volume_step,
                      p4_waveform_t waveform)
{
    (void)p4_game_play_tone(context, frequency_hz, duration_ms,
                            volume_step, waveform);
}

static void play_effect(p4_game_context_t *context, orble_state_t *state,
                        p4_game_audio_effect_t effect, int x, int y)
{
    state->feedback_x = (int16_t)x;
    state->feedback_y = (int16_t)y;
    (void)p4_game_audio_effect_play(context, &state->audio, effect);
}

static bool target_active(const orble_state_t *state, unsigned index)
{
    return (state->targets & (UINT16_C(1) << index)) != 0U;
}

static void move_bucket(orble_state_t *state)
{
    state->bucket_x = (int16_t)(state->bucket_x + state->bucket_velocity);
    if (state->bucket_x - ORBLE_BUCKET_WIDTH / 2 <= ORBLE_FIELD_LEFT + 2) {
        state->bucket_x = (int16_t)(ORBLE_FIELD_LEFT + 2 +
                                    ORBLE_BUCKET_WIDTH / 2);
        state->bucket_velocity = 2;
    } else if (state->bucket_x + ORBLE_BUCKET_WIDTH / 2 >=
               ORBLE_FIELD_RIGHT - 2) {
        state->bucket_x = (int16_t)(ORBLE_FIELD_RIGHT - 2 -
                                    ORBLE_BUCKET_WIDTH / 2);
        state->bucket_velocity = -2;
    }
}

static void bounce_off_target(p4_game_context_t *context,
                              orble_state_t *state)
{
    for (unsigned index = 0U; index < ORBLE_TARGET_COUNT; ++index) {
        if (!target_active(state, index)) {
            continue;
        }
        const int dx = state->ball_x - s_target_x[state->level][index];
        const int dy = state->ball_y - s_target_y[state->level][index];
        if (dx * dx + dy * dy > 144) {
            continue;
        }
        state->targets &= (uint16_t)~(UINT16_C(1) << index);
        state->score = state->score > UINT16_MAX - 10U
            ? UINT16_MAX : (uint16_t)(state->score + 10U);
        state->ball_vy = (int8_t)(state->ball_vy <= 0 ? 2 :
                                  -state->ball_vy);
        if (state->ball_vy == 0) {
            state->ball_vy = -2;
        }
        if (dx != 0) {
            state->ball_vx = (int8_t)(dx < 0
                ? -absolute_value(state->ball_vx) - 1
                : absolute_value(state->ball_vx) + 1);
        }
        if (state->ball_vx < -5) {
            state->ball_vx = -5;
        } else if (state->ball_vx > 5) {
            state->ball_vx = 5;
        }
        play_tone(context, (uint16_t)(440U + index * 24U), 42U, 3U,
                  P4_WAVE_SQUARE);
        play_effect(context, state, P4_GAME_AUDIO_EFFECT_IMPACT,
                    state->ball_x, state->ball_y);
        return;
    }
}

static void launch_ball(p4_game_context_t *context, orble_state_t *state)
{
    if (state->ball_flying || state->balls == 0U || state->won ||
        state->game_over) {
        return;
    }
    state->ball_vx = state->aim;
    state->ball_vy = 1;
    state->ball_flying = true;
    play_tone(context, 784U, 70U, 4U, P4_WAVE_TRIANGLE);
    play_effect(context, state, P4_GAME_AUDIO_EFFECT_ACTION,
                state->ball_x, state->ball_y);
}

static void finish_ball(p4_game_context_t *context, orble_state_t *state)
{
    const int bucket_left = state->bucket_x - ORBLE_BUCKET_WIDTH / 2;
    if (state->ball_x >= bucket_left &&
        state->ball_x <= bucket_left + ORBLE_BUCKET_WIDTH) {
        state->score = state->score > UINT16_MAX - 25U
            ? UINT16_MAX : (uint16_t)(state->score + 25U);
        play_tone(context, 1047U, 130U, 4U, P4_WAVE_TRIANGLE);
        play_effect(context, state, P4_GAME_AUDIO_EFFECT_REWARD,
                    state->ball_x, ORBLE_BUCKET_Y);
        reset_ball(state);
        return;
    }
    if (state->balls != 0U) {
        --state->balls;
    }
    play_tone(context, 180U, 140U, 4U, P4_WAVE_TRIANGLE);
    play_effect(context, state, P4_GAME_AUDIO_EFFECT_FAIL,
                state->ball_x, ORBLE_FIELD_BOTTOM - 4);
    reset_ball(state);
    if (state->balls == 0U) {
        state->game_over = true;
    }
}

static void simulate_step(p4_game_context_t *context, orble_state_t *state)
{
    move_bucket(state);
    if (!state->ball_flying) {
        return;
    }
    state->ball_x = (int16_t)(state->ball_x + state->ball_vx);
    state->ball_y = (int16_t)(state->ball_y + state->ball_vy);
    if (state->ball_x <= ORBLE_FIELD_LEFT + ORBLE_BALL_RADIUS) {
        state->ball_x = (int16_t)(ORBLE_FIELD_LEFT + ORBLE_BALL_RADIUS);
        state->ball_vx = (int8_t)absolute_value(state->ball_vx);
    } else if (state->ball_x >= ORBLE_FIELD_RIGHT - ORBLE_BALL_RADIUS) {
        state->ball_x = (int16_t)(ORBLE_FIELD_RIGHT - ORBLE_BALL_RADIUS);
        state->ball_vx = (int8_t)-absolute_value(state->ball_vx);
    }
    if (state->ball_y <= ORBLE_FIELD_TOP + ORBLE_BALL_RADIUS) {
        state->ball_y = (int16_t)(ORBLE_FIELD_TOP + ORBLE_BALL_RADIUS);
        state->ball_vy = (int8_t)absolute_value(state->ball_vy);
    }
    ++state->gravity_steps;
    if (state->gravity_steps == 3U) {
        state->gravity_steps = 0U;
        if (state->ball_vy < 5) {
            ++state->ball_vy;
        }
    }
    bounce_off_target(context, state);
    if (state->targets == 0U) {
        if (state->level + 1U == ORBLE_LEVEL_COUNT) {
            state->won = true;
            state->ball_flying = false;
            play_tone(context, 1319U, 260U, 5U, P4_WAVE_TRIANGLE);
            play_effect(context, state, P4_GAME_AUDIO_EFFECT_REWARD,
                        160, 80);
        } else {
            const uint8_t next_level = (uint8_t)(state->level + 1U);
            state->score = state->score > UINT16_MAX - 100U
                ? UINT16_MAX : (uint16_t)(state->score + 100U);
            begin_level(state, next_level);
            play_tone(context, 988U, 150U, 4U, P4_WAVE_TRIANGLE);
            play_effect(context, state, P4_GAME_AUDIO_EFFECT_REWARD,
                        160, 80);
        }
        return;
    }
    if (state->ball_y >= ORBLE_FIELD_BOTTOM) {
        finish_ball(context, state);
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(orble_state_t)) {
        return false;
    }
    reset_campaign(context->state);
    play_tone(context, 523U, 90U, 4U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(p4_game_context_t *context,
                                    const p4_game_input_t *input,
                                    uint32_t elapsed_ms)
{
    orble_state_t *const state = context->state;
    (void)p4_game_audio_effect_service(context, &state->audio);
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    if (state->won || state->game_over) {
        if ((input->pressed &
             (P4_BUTTON_A | P4_BUTTON_B | P4_BUTTON_START)) != 0U) {
            reset_campaign(state);
            play_tone(context, 523U, 90U, 4U, P4_WAVE_TRIANGLE);
        }
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_B) != 0U) {
        reset_campaign(state);
        play_tone(context, 330U, 80U, 3U, P4_WAVE_TRIANGLE);
        return P4_GAME_CONTINUE;
    }
    if (!state->ball_flying) {
        if ((input->pressed & P4_BUTTON_LEFT) != 0U && state->aim > -5) {
            --state->aim;
        }
        if ((input->pressed & P4_BUTTON_RIGHT) != 0U && state->aim < 5) {
            ++state->aim;
        }
    }
    if ((input->pressed & P4_BUTTON_A) != 0U) {
        launch_ball(context, state);
    }
    if (UINT32_MAX - state->simulation_ms < elapsed_ms) {
        state->simulation_ms = UINT32_MAX;
    } else {
        state->simulation_ms += elapsed_ms;
    }
    while (state->simulation_ms >= ORBLE_FIXED_STEP_MS) {
        state->simulation_ms -= ORBLE_FIXED_STEP_MS;
        simulate_step(context, state);
        if (state->won || state->game_over) {
            break;
        }
    }
    return P4_GAME_CONTINUE;
}

static void draw_number(p4_game_surface_t *surface, int x, int y,
                        uint16_t number, uint16_t color)
{
    char digits[6];
    size_t length = 0U;
    do {
        digits[length++] = (char)('0' + number % 10U);
        number = (uint16_t)(number / 10U);
    } while (number != 0U && length < sizeof(digits));
    while (length != 0U) {
        --length;
        p4_draw_text(surface, x, y, &digits[length], color, 1U, 1U);
        x += 6;
    }
}

static void draw_background(p4_game_surface_t *surface)
{
    p4_draw_clear(surface, UINT16_C(0x080f));
    p4_draw_fill_rect(surface, ORBLE_FIELD_LEFT, ORBLE_FIELD_TOP,
                      ORBLE_FIELD_RIGHT - ORBLE_FIELD_LEFT,
                      ORBLE_FIELD_BOTTOM - ORBLE_FIELD_TOP,
                      UINT16_C(0x0824));
    p4_draw_rect(surface, ORBLE_FIELD_LEFT, ORBLE_FIELD_TOP,
                 ORBLE_FIELD_RIGHT - ORBLE_FIELD_LEFT,
                 ORBLE_FIELD_BOTTOM - ORBLE_FIELD_TOP, UINT16_C(0x07ff));
    for (int y = ORBLE_FIELD_TOP + 12; y < ORBLE_FIELD_BOTTOM; y += 18) {
        for (int x = ORBLE_FIELD_LEFT + 9; x < ORBLE_FIELD_RIGHT; x += 23) {
            p4_draw_fill_circle(surface, x, y, 1, UINT16_C(0x31ae));
        }
    }
}

static void draw_target(p4_game_surface_t *surface, int x, int y,
                        unsigned index)
{
    static const uint16_t colors[4] = {
        UINT16_C(0xf81f), UINT16_C(0x07ff), UINT16_C(0xffe0), UINT16_C(0xfbe0),
    };
    const uint16_t color = colors[index % 4U];
    p4_draw_fill_circle(surface, x, y, 7, UINT16_C(0x2104));
    p4_draw_fill_circle(surface, x, y, 5, color);
    p4_draw_fill_circle(surface, x - 2, y - 2, 1, UINT16_C(0xffff));
}

static void draw_aim(p4_game_surface_t *surface, const orble_state_t *state)
{
    if (state->ball_flying) {
        return;
    }
    for (int distance = 1; distance <= 5; ++distance) {
        const int x = state->ball_x + state->aim * distance * 2;
        const int y = state->ball_y + distance * 5;
        p4_draw_fill_circle(surface, x, y, 1, UINT16_C(0x7bef));
    }
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const orble_state_t *const state = context->state;
    draw_background(surface);
    p4_draw_text(surface, 62, 7, "ORBLE", UINT16_C(0x07ff), 1U, 5U);
    p4_draw_text(surface, 118, 7, "L", UINT16_C(0x9cf3), 1U, 1U);
    draw_number(surface, 124, 7, (uint16_t)(state->level + 1U),
                UINT16_C(0xffff));
    p4_draw_text(surface, 136, 7, "/3", UINT16_C(0x9cf3), 1U, 2U);
    p4_draw_text(surface, 156, 7, "BALL", UINT16_C(0x9cf3), 1U, 4U);
    draw_number(surface, 184, 7, state->balls, UINT16_C(0xffff));
    p4_draw_text(surface, 202, 7, "S", UINT16_C(0x9cf3), 1U, 1U);
    draw_number(surface, 208, 7, state->score, UINT16_C(0xffff));

    for (unsigned index = 0U; index < ORBLE_TARGET_COUNT; ++index) {
        if (target_active(state, index)) {
            draw_target(surface, s_target_x[state->level][index],
                        s_target_y[state->level][index], index);
        }
    }
    p4_draw_fill_rect(surface, state->bucket_x - ORBLE_BUCKET_WIDTH / 2,
                      ORBLE_BUCKET_Y, ORBLE_BUCKET_WIDTH, 5,
                      UINT16_C(0xfbe0));
    p4_draw_rect(surface, state->bucket_x - ORBLE_BUCKET_WIDTH / 2,
                 ORBLE_BUCKET_Y - 2, ORBLE_BUCKET_WIDTH, 8,
                 UINT16_C(0xffff));
    draw_aim(surface, state);
    p4_draw_fill_circle(surface, state->ball_x, state->ball_y,
                        ORBLE_BALL_RADIUS + 2, UINT16_C(0x2104));
    p4_draw_fill_circle(surface, state->ball_x, state->ball_y,
                        ORBLE_BALL_RADIUS, UINT16_C(0xffff));
    p4_draw_fill_circle(surface, state->ball_x - 1, state->ball_y - 1,
                        1, UINT16_C(0x07ff));
    p4_game_feedback_draw_audio_effect(surface, &state->audio,
                                       state->feedback_x, state->feedback_y);
    p4_draw_text(surface, 102, 130, "L/R AIM  A LAUNCH",
                 UINT16_C(0x9cf3), 1U, 17U);
    p4_game_draw_standard_controls(surface, UINT16_C(0x4a69),
                                   UINT16_C(0x07ff), state->held_buttons);
    if (state->won || state->game_over) {
        p4_draw_fill_rect(surface, 102, 68, 116, 30, UINT16_C(0x1082));
        p4_draw_rect(surface, 102, 68, 116, 30, UINT16_C(0xffff));
        p4_draw_text(surface, state->won ? 129 : 121, 75,
                     state->won ? "ALL CLEAR" : "OUT OF ORBLES",
                     state->won ? UINT16_C(0xffe0) : UINT16_C(0xf81f),
                     1U, state->won ? 9U : 13U);
        p4_draw_text(surface, 128, 87, "A RESTART", UINT16_C(0x9cf3), 1U, 9U);
    }
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_orble_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(109),
    .id = "org.p4console.orble",
    .title = "ORBLE",
    .subtitle = "ORIGINAL PEG PUZZLE",
    .accent_rgb565 = UINT16_C(0x07ff),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM,
    .state_bytes = sizeof(orble_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
