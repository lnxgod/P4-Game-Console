// SPDX-License-Identifier: MIT
/* Original, code-rendered orbital defense game for P4 Game API v1. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/audio_pack.h"
#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/game.h"
#include "p4/input.h"

enum {
    SMASH_FIELD_LEFT = 88,
    SMASH_FIELD_RIGHT = 232,
    SMASH_FIELD_TOP = 28,
    SMASH_FIELD_BOTTOM = 128,
    SMASH_PLANET_X = 160,
    SMASH_PLANET_Y = 78,
    SMASH_PLANET_RADIUS = 16,
    SMASH_TURRET_RADIUS = 25,
    SMASH_METEOR_COUNT = 8,
    SMASH_BULLET_COUNT = 4,
    SMASH_WAVE_COUNT = 3,
    SMASH_FIXED_STEP_MS = 16,
};

typedef struct {
    int16_t x;
    int16_t y;
    int8_t vx;
    int8_t vy;
    uint8_t radius;
    bool active;
} meteor_t;

typedef struct {
    int16_t x;
    int16_t y;
    int8_t vx;
    int8_t vy;
    uint16_t life_ms;
    bool active;
} bullet_t;

typedef struct {
    meteor_t meteors[SMASH_METEOR_COUNT];
    bullet_t bullets[SMASH_BULLET_COUNT];
    int16_t feedback_x;
    int16_t feedback_y;
    uint32_t random_state;
    uint32_t held_buttons;
    uint32_t simulation_ms;
    uint16_t score;
    uint16_t fire_cooldown_ms;
    uint16_t spawn_delay_ms;
    uint8_t aim;
    uint8_t wave;
    uint8_t shield;
    uint8_t pending_meteors;
    bool won;
    bool game_over;
    p4_game_audio_effect_player_t audio;
} planet_smash_state_t;

static const int8_t s_cos[16] = {
    10, 9, 7, 4, 0, -4, -7, -9,
    -10, -9, -7, -4, 0, 4, 7, 9,
};

static const int8_t s_sin[16] = {
    0, 4, 7, 9, 10, 9, 7, 4,
    0, -4, -7, -9, -10, -9, -7, -4,
};

static uint32_t next_random(planet_smash_state_t *state)
{
    state->random_state = state->random_state * UINT32_C(1664525) +
        UINT32_C(1013904223);
    return state->random_state;
}

static void play_tone(p4_game_context_t *context, uint16_t frequency_hz,
                      uint16_t duration_ms, uint8_t volume_step,
                      p4_waveform_t waveform)
{
    (void)p4_game_play_tone(context, frequency_hz, duration_ms,
                            volume_step, waveform);
}

static void play_effect(p4_game_context_t *context,
                        planet_smash_state_t *state,
                        p4_game_audio_effect_t effect, int x, int y)
{
    state->feedback_x = (int16_t)x;
    state->feedback_y = (int16_t)y;
    (void)p4_game_audio_effect_play(context, &state->audio, effect);
}

static int8_t velocity_toward(int origin, int destination)
{
    const int difference = destination - origin;
    int velocity = difference / 30;
    if (velocity == 0 && difference != 0) {
        velocity = difference < 0 ? -1 : 1;
    }
    if (velocity < -3) {
        velocity = -3;
    } else if (velocity > 3) {
        velocity = 3;
    }
    return (int8_t)velocity;
}

static void spawn_meteor(planet_smash_state_t *state, unsigned index)
{
    meteor_t *const meteor = &state->meteors[index];
    const uint32_t random = next_random(state);
    switch ((random >> 4U) & UINT32_C(3)) {
    case 0U:
        meteor->x = (int16_t)(SMASH_FIELD_LEFT + 7);
        meteor->y = (int16_t)(SMASH_FIELD_TOP + 8 + (random % 84U));
        break;
    case 1U:
        meteor->x = (int16_t)(SMASH_FIELD_RIGHT - 7);
        meteor->y = (int16_t)(SMASH_FIELD_TOP + 8 + (random % 84U));
        break;
    case 2U:
        meteor->x = (int16_t)(SMASH_FIELD_LEFT + 8 + (random % 128U));
        meteor->y = (int16_t)(SMASH_FIELD_TOP + 6);
        break;
    default:
        meteor->x = (int16_t)(SMASH_FIELD_LEFT + 8 + (random % 128U));
        meteor->y = (int16_t)(SMASH_FIELD_BOTTOM - 6);
        break;
    }
    meteor->vx = velocity_toward(meteor->x, SMASH_PLANET_X);
    meteor->vy = velocity_toward(meteor->y, SMASH_PLANET_Y);
    meteor->radius = (uint8_t)(5U + ((random >> 9U) % 4U));
    meteor->active = true;
}

static uint8_t meteor_count_for_wave(uint8_t wave)
{
    return wave == SMASH_WAVE_COUNT ? SMASH_METEOR_COUNT :
        (uint8_t)(3U + wave * 2U);
}

static void start_wave(planet_smash_state_t *state, uint8_t wave)
{
    state->wave = wave;
    state->pending_meteors = meteor_count_for_wave(wave);
    state->spawn_delay_ms = 0U;
    for (unsigned index = 0U; index < SMASH_METEOR_COUNT; ++index) {
        state->meteors[index].active = false;
    }
}

static void reset_game(planet_smash_state_t *state)
{
    *state = (planet_smash_state_t){
        .random_state = UINT32_C(0x51a55e),
        .shield = 3U,
        .aim = 0U,
        .feedback_x = SMASH_PLANET_X,
        .feedback_y = SMASH_PLANET_Y,
    };
    start_wave(state, 1U);
}

static bool any_meteors(const planet_smash_state_t *state)
{
    for (unsigned index = 0U; index < SMASH_METEOR_COUNT; ++index) {
        if (state->meteors[index].active) {
            return true;
        }
    }
    return false;
}

static void spawn_pending_meteor(planet_smash_state_t *state)
{
    if (state->pending_meteors == 0U) {
        return;
    }
    if (state->spawn_delay_ms > SMASH_FIXED_STEP_MS) {
        state->spawn_delay_ms = (uint16_t)(state->spawn_delay_ms -
                                           SMASH_FIXED_STEP_MS);
        return;
    }
    state->spawn_delay_ms = 420U;
    for (unsigned index = 0U; index < SMASH_METEOR_COUNT; ++index) {
        if (!state->meteors[index].active) {
            spawn_meteor(state, index);
            --state->pending_meteors;
            return;
        }
    }
}

static void fire_bullet(p4_game_context_t *context,
                        planet_smash_state_t *state)
{
    if (state->fire_cooldown_ms != 0U || state->won || state->game_over) {
        return;
    }
    for (unsigned index = 0U; index < SMASH_BULLET_COUNT; ++index) {
        bullet_t *const bullet = &state->bullets[index];
        if (bullet->active) {
            continue;
        }
        const int aim = state->aim;
        bullet->x = (int16_t)(SMASH_PLANET_X + s_cos[aim] * 3);
        bullet->y = (int16_t)(SMASH_PLANET_Y + s_sin[aim] * 3);
        bullet->vx = (int8_t)(s_cos[aim] / 2);
        bullet->vy = (int8_t)(s_sin[aim] / 2);
        if (bullet->vx == 0 && bullet->vy == 0) {
            bullet->vx = 1;
        }
        bullet->life_ms = 740U;
        bullet->active = true;
        state->fire_cooldown_ms = 150U;
        play_tone(context, 880U, 45U, 4U, P4_WAVE_SQUARE);
        play_effect(context, state, P4_GAME_AUDIO_EFFECT_ACTION,
                    bullet->x, bullet->y);
        return;
    }
}

static void lose_shield(p4_game_context_t *context,
                        planet_smash_state_t *state, int x, int y)
{
    if (state->shield != 0U) {
        --state->shield;
    }
    play_tone(context, 130U, 180U, 5U, P4_WAVE_TRIANGLE);
    play_effect(context, state, P4_GAME_AUDIO_EFFECT_FAIL, x, y);
    if (state->shield == 0U) {
        state->game_over = true;
    }
}

static void score_meteor(p4_game_context_t *context,
                         planet_smash_state_t *state,
                         meteor_t *meteor, bullet_t *bullet)
{
    meteor->active = false;
    bullet->active = false;
    state->score = state->score > UINT16_MAX - 25U
        ? UINT16_MAX : (uint16_t)(state->score + 25U);
    play_tone(context, 360U, 90U, 4U, P4_WAVE_SQUARE);
    play_effect(context, state, P4_GAME_AUDIO_EFFECT_IMPACT,
                meteor->x, meteor->y);
}

static void move_bullets(planet_smash_state_t *state)
{
    for (unsigned index = 0U; index < SMASH_BULLET_COUNT; ++index) {
        bullet_t *const bullet = &state->bullets[index];
        if (!bullet->active) {
            continue;
        }
        if (bullet->life_ms <= SMASH_FIXED_STEP_MS) {
            bullet->active = false;
            continue;
        }
        bullet->life_ms = (uint16_t)(bullet->life_ms - SMASH_FIXED_STEP_MS);
        bullet->x = (int16_t)(bullet->x + bullet->vx);
        bullet->y = (int16_t)(bullet->y + bullet->vy);
        if (bullet->x < SMASH_FIELD_LEFT || bullet->x > SMASH_FIELD_RIGHT ||
            bullet->y < SMASH_FIELD_TOP || bullet->y > SMASH_FIELD_BOTTOM) {
            bullet->active = false;
        }
    }
}

static void move_meteors(p4_game_context_t *context,
                         planet_smash_state_t *state)
{
    for (unsigned index = 0U; index < SMASH_METEOR_COUNT; ++index) {
        meteor_t *const meteor = &state->meteors[index];
        if (!meteor->active) {
            continue;
        }
        meteor->x = (int16_t)(meteor->x + meteor->vx);
        meteor->y = (int16_t)(meteor->y + meteor->vy);
        const int dx = meteor->x - SMASH_PLANET_X;
        const int dy = meteor->y - SMASH_PLANET_Y;
        const int hit_radius = SMASH_PLANET_RADIUS + meteor->radius;
        if (dx * dx + dy * dy <= hit_radius * hit_radius) {
            meteor->active = false;
            lose_shield(context, state, meteor->x, meteor->y);
        }
    }
}

static void check_hits(p4_game_context_t *context,
                       planet_smash_state_t *state)
{
    for (unsigned bullet_index = 0U; bullet_index < SMASH_BULLET_COUNT;
         ++bullet_index) {
        bullet_t *const bullet = &state->bullets[bullet_index];
        if (!bullet->active) {
            continue;
        }
        for (unsigned meteor_index = 0U; meteor_index < SMASH_METEOR_COUNT;
             ++meteor_index) {
            meteor_t *const meteor = &state->meteors[meteor_index];
            if (!meteor->active) {
                continue;
            }
            const int dx = bullet->x - meteor->x;
            const int dy = bullet->y - meteor->y;
            const int hit_radius = meteor->radius + 3;
            if (dx * dx + dy * dy <= hit_radius * hit_radius) {
                score_meteor(context, state, meteor, bullet);
                break;
            }
        }
    }
}

static void simulate_step(p4_game_context_t *context,
                          planet_smash_state_t *state)
{
    if ((state->held_buttons & P4_BUTTON_LEFT) != 0U) {
        state->aim = (uint8_t)((state->aim + 15U) & 15U);
    }
    if ((state->held_buttons & P4_BUTTON_RIGHT) != 0U) {
        state->aim = (uint8_t)((state->aim + 1U) & 15U);
    }
    if (state->fire_cooldown_ms > SMASH_FIXED_STEP_MS) {
        state->fire_cooldown_ms = (uint16_t)(state->fire_cooldown_ms -
                                             SMASH_FIXED_STEP_MS);
    } else {
        state->fire_cooldown_ms = 0U;
    }
    spawn_pending_meteor(state);
    move_bullets(state);
    move_meteors(context, state);
    if (state->game_over) {
        return;
    }
    check_hits(context, state);
    if (state->pending_meteors == 0U && !any_meteors(state)) {
        if (state->wave == SMASH_WAVE_COUNT) {
            state->won = true;
            play_tone(context, 1319U, 260U, 5U, P4_WAVE_TRIANGLE);
            play_effect(context, state, P4_GAME_AUDIO_EFFECT_REWARD,
                        SMASH_PLANET_X, SMASH_PLANET_Y);
        } else {
            start_wave(state, (uint8_t)(state->wave + 1U));
            state->score = state->score > UINT16_MAX - 100U
                ? UINT16_MAX : (uint16_t)(state->score + 100U);
            play_tone(context, 1047U, 140U, 4U, P4_WAVE_TRIANGLE);
            play_effect(context, state, P4_GAME_AUDIO_EFFECT_REWARD,
                        SMASH_PLANET_X, SMASH_PLANET_Y);
        }
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(planet_smash_state_t)) {
        return false;
    }
    reset_game(context->state);
    play_tone(context, 523U, 90U, 4U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(p4_game_context_t *context,
                                    const p4_game_input_t *input,
                                    uint32_t elapsed_ms)
{
    planet_smash_state_t *const state = context->state;
    (void)p4_game_audio_effect_service(context, &state->audio);
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    if (state->won || state->game_over) {
        if ((input->pressed &
             (P4_BUTTON_A | P4_BUTTON_B | P4_BUTTON_START)) != 0U) {
            reset_game(state);
            play_tone(context, 523U, 90U, 4U, P4_WAVE_TRIANGLE);
        }
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_B) != 0U) {
        reset_game(state);
        play_tone(context, 330U, 80U, 3U, P4_WAVE_TRIANGLE);
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_A) != 0U) {
        fire_bullet(context, state);
    }
    if (UINT32_MAX - state->simulation_ms < elapsed_ms) {
        state->simulation_ms = UINT32_MAX;
    } else {
        state->simulation_ms += elapsed_ms;
    }
    while (state->simulation_ms >= SMASH_FIXED_STEP_MS) {
        state->simulation_ms -= SMASH_FIXED_STEP_MS;
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
    p4_draw_clear(surface, UINT16_C(0x0000));
    p4_draw_fill_rect(surface, SMASH_FIELD_LEFT, SMASH_FIELD_TOP,
                      SMASH_FIELD_RIGHT - SMASH_FIELD_LEFT,
                      SMASH_FIELD_BOTTOM - SMASH_FIELD_TOP,
                      UINT16_C(0x100b));
    p4_draw_rect(surface, SMASH_FIELD_LEFT, SMASH_FIELD_TOP,
                 SMASH_FIELD_RIGHT - SMASH_FIELD_LEFT,
                 SMASH_FIELD_BOTTOM - SMASH_FIELD_TOP, UINT16_C(0x7a1f));
    for (int y = SMASH_FIELD_TOP + 9; y < SMASH_FIELD_BOTTOM; y += 17) {
        for (int x = SMASH_FIELD_LEFT + 7; x < SMASH_FIELD_RIGHT; x += 19) {
            p4_draw_fill_circle(surface, x, y, 1, UINT16_C(0x7bef));
        }
    }
}

static void draw_planet(p4_game_surface_t *surface,
                        const planet_smash_state_t *state)
{
    const uint16_t shield_color = state->shield == 3U ? UINT16_C(0x07ff) :
        (state->shield == 2U ? UINT16_C(0xffe0) : UINT16_C(0xf800));
    p4_draw_fill_circle(surface, SMASH_PLANET_X, SMASH_PLANET_Y,
                        SMASH_PLANET_RADIUS + 5, shield_color);
    p4_draw_fill_circle(surface, SMASH_PLANET_X, SMASH_PLANET_Y,
                        SMASH_PLANET_RADIUS + 2, UINT16_C(0x100b));
    p4_draw_fill_circle(surface, SMASH_PLANET_X, SMASH_PLANET_Y,
                        SMASH_PLANET_RADIUS, UINT16_C(0x07ae));
    p4_draw_fill_circle(surface, SMASH_PLANET_X - 5, SMASH_PLANET_Y - 4,
                        5, UINT16_C(0x9fe0));
    p4_draw_fill_circle(surface, SMASH_PLANET_X + 6, SMASH_PLANET_Y + 5,
                        4, UINT16_C(0x0560));
}

static void draw_turret(p4_game_surface_t *surface,
                        const planet_smash_state_t *state)
{
    const int aim = state->aim;
    const int base_x = SMASH_PLANET_X + s_cos[aim] * 2;
    const int base_y = SMASH_PLANET_Y + s_sin[aim] * 2;
    p4_draw_fill_circle(surface, base_x, base_y, 4, UINT16_C(0xfbe0));
    for (int step = 2; step <= 4; ++step) {
        p4_draw_fill_circle(surface,
                            SMASH_PLANET_X + s_cos[aim] * step / 2,
                            SMASH_PLANET_Y + s_sin[aim] * step / 2,
                            2, UINT16_C(0xffff));
    }
}

static void draw_meteor(p4_game_surface_t *surface, const meteor_t *meteor)
{
    p4_draw_fill_circle(surface, meteor->x, meteor->y,
                        (int)meteor->radius + 2, UINT16_C(0x4208));
    p4_draw_fill_circle(surface, meteor->x, meteor->y,
                        meteor->radius, UINT16_C(0xfbe0));
    p4_draw_fill_circle(surface, meteor->x - 2, meteor->y - 1,
                        2, UINT16_C(0xad20));
    p4_draw_fill_circle(surface, meteor->x + 3, meteor->y + 2,
                        1, UINT16_C(0x630c));
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const planet_smash_state_t *const state = context->state;
    draw_background(surface);
    p4_draw_text(surface, 55, 7, "PLANET SMASH", UINT16_C(0xfbe0), 1U, 12U);
    p4_draw_text(surface, 132, 7, "W", UINT16_C(0x9cf3), 1U, 1U);
    draw_number(surface, 138, 7, state->wave, UINT16_C(0xffff));
    p4_draw_text(surface, 150, 7, "/3", UINT16_C(0x9cf3), 1U, 2U);
    p4_draw_text(surface, 169, 7, "SH", UINT16_C(0x9cf3), 1U, 2U);
    draw_number(surface, 183, 7, state->shield, UINT16_C(0xffff));
    p4_draw_text(surface, 202, 7, "S", UINT16_C(0x9cf3), 1U, 1U);
    draw_number(surface, 208, 7, state->score, UINT16_C(0xffff));

    draw_planet(surface, state);
    draw_turret(surface, state);
    for (unsigned index = 0U; index < SMASH_METEOR_COUNT; ++index) {
        if (state->meteors[index].active) {
            draw_meteor(surface, &state->meteors[index]);
        }
    }
    for (unsigned index = 0U; index < SMASH_BULLET_COUNT; ++index) {
        if (state->bullets[index].active) {
            p4_draw_fill_circle(surface, state->bullets[index].x,
                                state->bullets[index].y, 3,
                                UINT16_C(0xffff));
            p4_draw_fill_circle(surface, state->bullets[index].x,
                                state->bullets[index].y, 1,
                                UINT16_C(0x07ff));
        }
    }
    p4_game_feedback_draw_audio_effect(surface, &state->audio,
                                       state->feedback_x, state->feedback_y);
    p4_draw_text(surface, 104, 130, "L/R ROTATE  A FIRE",
                 UINT16_C(0x9cf3), 1U, 18U);
    p4_game_draw_standard_controls(surface, UINT16_C(0x4a69),
                                   UINT16_C(0xfbe0), state->held_buttons);
    if (state->won || state->game_over) {
        p4_draw_fill_rect(surface, 103, 66, 114, 32, UINT16_C(0x1082));
        p4_draw_rect(surface, 103, 66, 114, 32, UINT16_C(0xffff));
        p4_draw_text(surface, state->won ? 128 : 121, 74,
                     state->won ? "PLANET SAFE" : "SHIELD LOST",
                     state->won ? UINT16_C(0xffe0) : UINT16_C(0xf81f),
                     1U, 11U);
        p4_draw_text(surface, 128, 87, "A RESTART", UINT16_C(0x9cf3), 1U, 9U);
    }
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_planet_smash_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(110),
    .id = "org.p4console.planet-smash",
    .title = "PLANET SMASH",
    .subtitle = "ORIGINAL ORBIT DEFENSE",
    .accent_rgb565 = UINT16_C(0xfbe0),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM,
    .state_bytes = sizeof(planet_smash_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
