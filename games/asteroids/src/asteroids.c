// SPDX-License-Identifier: MIT
/* Original clean-room Asteroids-style game for P4 Game API v1. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"
#include "generated/space_background.inc"
#include "generated/asteroids_sprites.inc"

typedef struct {
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint8_t radius;
    uint8_t active;
    uint8_t kind;
} asteroid_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint16_t life_ms;
    uint8_t active;
} bullet_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t vx;
    int16_t vy;
    uint8_t angle;
    uint8_t lives;
    uint8_t wave;
    uint8_t game_over;
    uint32_t score;
    uint32_t random_state;
    uint32_t held_buttons;
    uint32_t simulation_ms;
    uint32_t fire_cooldown_ms;
    uint32_t respawn_safe_ms;
    uint32_t explosion_ms;
    int16_t explosion_x;
    int16_t explosion_y;
    asteroid_t asteroids[12];
    bullet_t bullets[5];
} asteroids_state_t;

enum { FIELD_W = 320, FIELD_H = 170, FIELD_TOP = 25, FIXED_MS = 16 };

static const int8_t s_cos[16] = {10, 9, 7, 4, 0, -4, -7, -9,
                                 -10, -9, -7, -4, 0, 4, 7, 9};
static const int8_t s_sin[16] = {0, 4, 7, 9, 10, 9, 7, 4,
                                 0, -4, -7, -9, -10, -9, -7, -4};

static uint32_t next_random(asteroids_state_t *state)
{
    state->random_state = state->random_state * UINT32_C(1664525) +
        UINT32_C(1013904223);
    return state->random_state;
}

static int16_t wrap_x(int value)
{
    while (value < 0) value += FIELD_W;
    while (value >= FIELD_W) value -= FIELD_W;
    return (int16_t)value;
}

static int16_t wrap_y(int value)
{
    while (value < FIELD_TOP) value += FIELD_H - FIELD_TOP;
    while (value >= FIELD_H) value -= FIELD_H - FIELD_TOP;
    return (int16_t)value;
}

static void play_tone(p4_game_context_t *context, uint16_t hz, uint16_t ms,
                      uint8_t volume, p4_waveform_t wave)
{
    (void)p4_game_play_tone(context, hz, ms, volume, wave);
}

static void spawn_asteroid(asteroids_state_t *state, int index, int x, int y,
                           int radius, int kind)
{
    asteroid_t *asteroid = &state->asteroids[index];
    const uint32_t random = next_random(state);
    asteroid->x = (int16_t)x;
    asteroid->y = (int16_t)y;
    asteroid->vx = (int16_t)((int)(random % 5U) - 2);
    asteroid->vy = (int16_t)((int)((random >> 4U) % 5U) - 2);
    if (asteroid->vx == 0 && asteroid->vy == 0) asteroid->vx = 1;
    asteroid->radius = (uint8_t)radius;
    asteroid->kind = (uint8_t)kind;
    asteroid->active = 1U;
}

static void reset_game(asteroids_state_t *state)
{
    *state = (asteroids_state_t){
        .x = 160, .y = 105, .lives = 3U,
        .random_state = UINT32_C(0x7157e),
    };
    for (int i = 0; i < 6; ++i) {
        spawn_asteroid(state, i, 25 + i * 52, 48 + (i % 3) * 32,
                       14 + (i % 2) * 5, i % 2);
    }
}

static bool asteroids_remaining(const asteroids_state_t *state)
{
    for (size_t i = 0U; i < sizeof(state->asteroids) / sizeof(state->asteroids[0]); ++i) {
        if (state->asteroids[i].active) {
            return true;
        }
    }
    return false;
}

static void start_next_wave(asteroids_state_t *state)
{
    ++state->wave;
    for (size_t i = 0U; i < 6U; ++i) {
        const int x = (int)((next_random(state) % 280U) + 20U);
        const int y = (int)((next_random(state) % 115U) + FIELD_TOP + 8U);
        spawn_asteroid(state, (int)i, x, y, 13 + (int)(i % 2U) * 5, 0);
    }
    state->respawn_safe_ms = 900U;
}

static bool spawn_bullet(asteroids_state_t *state)
{
    for (size_t i = 0U; i < sizeof(state->bullets) / sizeof(state->bullets[0]); ++i) {
        bullet_t *bullet = &state->bullets[i];
        if (!bullet->active) {
            bullet->x = state->x + (int16_t)(s_cos[state->angle] * 2);
            bullet->y = state->y + (int16_t)(s_sin[state->angle] * 2);
            bullet->vx = (int16_t)(s_cos[state->angle] * 2 + state->vx);
            bullet->vy = (int16_t)(s_sin[state->angle] * 2 + state->vy);
            bullet->life_ms = 800U;
            bullet->active = 1U;
            return true;
        }
    }
    return false;
}

static void update_physics(p4_game_context_t *context, asteroids_state_t *state)
{
    if ((state->held_buttons & P4_BUTTON_LEFT) != 0U) state->angle = (uint8_t)((state->angle + 15U) & 15U);
    if ((state->held_buttons & P4_BUTTON_RIGHT) != 0U) state->angle = (uint8_t)((state->angle + 1U) & 15U);
    if ((state->held_buttons & P4_BUTTON_UP) != 0U) {
        state->vx = (int16_t)(state->vx + s_cos[state->angle] / 5);
        state->vy = (int16_t)(state->vy + s_sin[state->angle] / 5);
    }
    if (state->vx > 4) state->vx = 4;
    if (state->vx < -4) state->vx = -4;
    if (state->vy > 4) state->vy = 4;
    if (state->vy < -4) state->vy = -4;
    state->x = wrap_x(state->x + state->vx);
    state->y = wrap_y(state->y + state->vy);
    if (state->fire_cooldown_ms > FIXED_MS) state->fire_cooldown_ms -= FIXED_MS;
    else state->fire_cooldown_ms = 0U;
    if (state->respawn_safe_ms > FIXED_MS) state->respawn_safe_ms -= FIXED_MS;
    else state->respawn_safe_ms = 0U;
    if (state->explosion_ms > FIXED_MS) state->explosion_ms -= FIXED_MS;
    else state->explosion_ms = 0U;

    for (size_t i = 0U; i < sizeof(state->bullets) / sizeof(state->bullets[0]); ++i) {
        bullet_t *bullet = &state->bullets[i];
        if (!bullet->active) continue;
        if (bullet->life_ms <= FIXED_MS) {
            bullet->active = 0U;
            continue;
        }
        bullet->life_ms = (uint16_t)(bullet->life_ms - FIXED_MS);
        bullet->x = wrap_x(bullet->x + bullet->vx);
        bullet->y = wrap_y(bullet->y + bullet->vy);
        for (size_t j = 0U; j < sizeof(state->asteroids) / sizeof(state->asteroids[0]); ++j) {
            asteroid_t *asteroid = &state->asteroids[j];
            const int dx = bullet->x - asteroid->x;
            const int dy = bullet->y - asteroid->y;
            if (asteroid->active && dx * dx + dy * dy < asteroid->radius * asteroid->radius) {
                bullet->active = 0U;
                asteroid->active = 0U;
                state->explosion_x = asteroid->x;
                state->explosion_y = asteroid->y;
                state->explosion_ms = 160U;
                state->score += (uint32_t)(asteroid->kind == 0U ? 20U : 50U);
                play_tone(context,
                          (uint16_t)(180U + (uint16_t)asteroid->radius * 12U),
                          70U, 4U, P4_WAVE_TRIANGLE);
                if (asteroid->radius > 10U) {
                    for (size_t k = 0U; k < sizeof(state->asteroids) / sizeof(state->asteroids[0]); ++k) {
                        if (!state->asteroids[k].active) {
                            spawn_asteroid(state, (int)k, asteroid->x, asteroid->y,
                                           (int)asteroid->radius - 6, 1);
                            break;
                        }
                    }
                }
                break;
            }
        }
    }
    for (size_t i = 0U; i < sizeof(state->asteroids) / sizeof(state->asteroids[0]); ++i) {
        asteroid_t *asteroid = &state->asteroids[i];
        if (!asteroid->active) continue;
        asteroid->x = wrap_x(asteroid->x + asteroid->vx);
        asteroid->y = wrap_y(asteroid->y + asteroid->vy);
        const int dx = state->x - asteroid->x;
        const int dy = state->y - asteroid->y;
        const int collision_radius = (int)asteroid->radius + 5;
        if (state->respawn_safe_ms == 0U &&
            dx * dx + dy * dy < collision_radius * collision_radius) {
            asteroid->active = 0U;
            if (state->lives > 0U) --state->lives;
            state->x = 160; state->y = 105; state->vx = 0; state->vy = 0;
            state->respawn_safe_ms = 1100U;
            play_tone(context, 90U, 180U, 5U, P4_WAVE_SQUARE);
            if (state->lives == 0U) state->game_over = 1U;
        }
    }
    if (!asteroids_remaining(state)) {
        start_next_wave(state);
        play_tone(context, 880U, 100U, 4U, P4_WAVE_TRIANGLE);
    }
}

static void draw_space_background(p4_game_surface_t *surface)
{
    for (int y = 0; y < 43; ++y) {
        for (int x = 0; x < 80; ++x) {
            const uint16_t color = s_deep_space_pixels[y * 80 + x];
            if (color != UINT16_C(0x0000)) {
                p4_draw_fill_rect(surface, x * 4, FIELD_TOP + y * 4,
                                  4, 4, color);
            }
        }
    }
}

static void draw_sprite(p4_game_surface_t *surface, int x, int y,
                        size_t cell)
{
    /* The four 48x48 cells share a 192-pixel-wide row stride. */
    const size_t offset = cell * 48U;
    p4_draw_sprite_rgb565(surface, x - 24, y - 24,
                          &s_asteroids_sprite_pixels[offset],
                          48U, 48U, 192U, true, UINT16_C(0x0000));
}

static void draw_number(p4_game_surface_t *surface, int x, int y,
                        uint32_t value, uint16_t color)
{
    char text[11];
    size_t pos = sizeof(text) - 1U;
    text[pos] = '\0';
    do {
        text[--pos] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && pos != 0U);
    p4_draw_text(surface, x, y, &text[pos], color, 1U, sizeof(text) - pos);
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(asteroids_state_t)) {
        return false;
    }
    asteroids_state_t *const state = context->state;
    reset_game(state);
    play_tone(context, 523U, 80U, 3U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    asteroids_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    if (state->game_over) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) reset_game(state);
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_START) != 0U) state->game_over = 1U;
    if ((input->pressed & P4_BUTTON_A) != 0U && state->fire_cooldown_ms == 0U && spawn_bullet(state)) {
        state->fire_cooldown_ms = 180U;
        play_tone(context, 740U, 45U, 3U, P4_WAVE_SQUARE);
    }
    state->simulation_ms += elapsed_ms;
    while (state->simulation_ms >= FIXED_MS) {
        state->simulation_ms -= FIXED_MS;
        update_physics(context, state);
    }
    return P4_GAME_CONTINUE;
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const asteroids_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x0000));
    draw_space_background(surface);
    p4_draw_text(surface, 8, 5, "ASTEROIDS", UINT16_C(0xffff), 1U, 9U);
    p4_draw_text(surface, 90, 5, "SCORE", UINT16_C(0x9cf3), 1U, 5U);
    draw_number(surface, 126, 5, state->score, UINT16_C(0xffff));
    p4_draw_text(surface, 170, 5, "LIVES", UINT16_C(0x9cf3), 1U, 5U);
    draw_number(surface, 206, 5, state->lives, UINT16_C(0xffff));
    p4_draw_text(surface, 230, 5, "WAVE", UINT16_C(0x9cf3), 1U, 4U);
    draw_number(surface, 260, 5, state->wave + 1U, UINT16_C(0xffff));
    for (size_t i = 0U; i < sizeof(state->asteroids) / sizeof(state->asteroids[0]); ++i) {
        const asteroid_t *asteroid = &state->asteroids[i];
        if (asteroid->active) {
            draw_sprite(surface, asteroid->x, asteroid->y, 1U);
        }
    }
    for (size_t i = 0U; i < sizeof(state->bullets) / sizeof(state->bullets[0]); ++i) {
        if (state->bullets[i].active) {
            draw_sprite(surface, state->bullets[i].x, state->bullets[i].y, 2U);
        }
    }
    if (state->explosion_ms != 0U) {
        draw_sprite(surface, state->explosion_x, state->explosion_y, 3U);
    }
    if (!state->game_over && (state->respawn_safe_ms == 0U ||
                              (state->respawn_safe_ms / 100U) % 2U == 0U)) {
        draw_sprite(surface, state->x, state->y, 0U);
    } else {
        p4_draw_text(surface, 116, 78, "GAME OVER", UINT16_C(0xf81f), 1U, 9U);
        p4_draw_text(surface, 94, 91, "A TO RESTART", UINT16_C(0xffff), 1U, 12U);
    }
    p4_draw_text(surface, 49, 184, "ARROWS MOVE  A FIRE", UINT16_C(0x9cf3), 1U, 19U);
    p4_game_draw_standard_controls(
        surface, UINT16_C(0x7bef), UINT16_C(0x5fea),
        state->held_buttons);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_asteroids_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(103),
    .id = "org.p4console.asteroids",
    .title = "ASTEROIDS",
    .subtitle = "P4 GAME API V1",
    .accent_rgb565 = UINT16_C(0x5fea),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE,
    .state_bytes = sizeof(asteroids_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
