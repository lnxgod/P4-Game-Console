// SPDX-License-Identifier: MIT
/* Original Asteroids 2 game for P4 Game API v1; code-rendered only. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"

enum { FIELD_TOP = 23, FIELD_BOTTOM = 177, FIELD_WIDTH = 320,
       ROCK_COUNT = 10, SHOT_COUNT = 7, STEP_MS = 16 };

typedef struct { int16_t x, y; int8_t vx, vy; uint8_t radius, spin, active; } rock_t;
typedef struct { int16_t x, y; int8_t vx, vy; uint16_t ttl_ms; uint8_t active; } shot_t;
typedef struct { int16_t x, y; int8_t vx, vy; uint8_t ttl; } spark_t;
typedef struct {
    int16_t ship_x, ship_y;
    int8_t ship_vx, ship_vy;
    uint8_t heading, lives, level, paused, ended;
    uint32_t score, random, step_ms, cooldown_ms, shield_ms, held_buttons;
    uint16_t turn_ms, saucer_fire_ms;
    uint8_t rock_spin_ticks;
    uint32_t saucer_wait_ms;
    int16_t saucer_x, saucer_y;
    int8_t saucer_vx;
    uint8_t saucer_active;
    int16_t enemy_x, enemy_y;
    int8_t enemy_vx, enemy_vy;
    uint16_t enemy_ttl_ms;
    rock_t rocks[ROCK_COUNT];
    shot_t shots[SHOT_COUNT];
    spark_t sparks[18];
} asteroids_2_state_t;

static const int8_t s_dx[16] = {10,9,7,4,0,-4,-7,-9,-10,-9,-7,-4,0,4,7,9};
static const int8_t s_dy[16] = {0,4,7,9,10,9,7,4,0,-4,-7,-9,-10,-9,-7,-4};

static uint32_t random_next(asteroids_2_state_t *state)
{
    state->random = state->random * UINT32_C(1103515245) + UINT32_C(12345);
    return state->random;
}

static int16_t wrap_x(int value)
{
    if (value < 0) value += FIELD_WIDTH;
    if (value >= FIELD_WIDTH) value -= FIELD_WIDTH;
    return (int16_t)value;
}

static int16_t wrap_y(int value)
{
    const int height = FIELD_BOTTOM - FIELD_TOP;
    if (value < FIELD_TOP) value += height;
    if (value >= FIELD_BOTTOM) value -= height;
    return (int16_t)value;
}

static void tone(p4_game_context_t *context, uint16_t hz, uint16_t ms,
                 uint8_t volume, p4_waveform_t waveform)
{
    (void)p4_game_play_tone(context, hz, ms, volume, waveform);
}

static void add_score(asteroids_2_state_t *state, uint16_t amount)
{
    state->score = UINT32_MAX - state->score < amount ?
        UINT32_MAX : state->score + amount;
}

static void spawn_rock(asteroids_2_state_t *state, size_t index,
                       int x, int y, uint8_t radius)
{
    rock_t *const rock = &state->rocks[index];
    const uint32_t random = random_next(state);
    rock->x = (int16_t)x;
    rock->y = (int16_t)y;
    rock->vx = (int8_t)((int)(random % 3U) - 1);
    rock->vy = (int8_t)((int)((random >> 3U) % 3U) - 1);
    if (rock->vx == 0 && rock->vy == 0) rock->vx = 1;
    rock->radius = radius;
    rock->spin = (uint8_t)(random >> 8U) & 15U;
    rock->active = 1U;
}

static void reset_ship(asteroids_2_state_t *state)
{
    state->ship_x = 160; state->ship_y = 101;
    state->ship_vx = 0; state->ship_vy = 0;
    state->heading = 12U; state->shield_ms = 1100U;
}

static void begin_level(asteroids_2_state_t *state)
{
    const uint8_t count = (uint8_t)(3U + state->level);
    for (size_t index = 0U; index < ROCK_COUNT; ++index) state->rocks[index].active = 0U;
    for (uint8_t index = 0U; index < count && index < ROCK_COUNT; ++index) {
        const int x = (index & 1U) == 0U ? 18 + (int)(random_next(state) % 80U) :
            222 + (int)(random_next(state) % 80U);
        const int y = FIELD_TOP + 10 + (int)(random_next(state) % 125U);
        spawn_rock(state, index, x, y, (uint8_t)(14U + (index % 2U) * 4U));
    }
    reset_ship(state);
}

static void new_game(asteroids_2_state_t *state)
{
    *state = (asteroids_2_state_t){.lives = 3U, .level = 1U,
                                   .random = UINT32_C(0xA57E201)};
    begin_level(state);
}

static bool rocks_remain(const asteroids_2_state_t *state)
{
    for (size_t index = 0U; index < ROCK_COUNT; ++index)
        if (state->rocks[index].active) return true;
    return false;
}

static void fire(p4_game_context_t *context, asteroids_2_state_t *state)
{
    for (size_t index = 0U; index < SHOT_COUNT; ++index) {
        shot_t *const shot = &state->shots[index];
        if (shot->active) continue;
        shot->x = (int16_t)(state->ship_x + s_dx[state->heading]);
        shot->y = (int16_t)(state->ship_y + s_dy[state->heading]);
        shot->vx = (int8_t)(s_dx[state->heading] / 2 + state->ship_vx);
        shot->vy = (int8_t)(s_dy[state->heading] / 2 + state->ship_vy);
        shot->ttl_ms = 900U; shot->active = 1U; state->cooldown_ms = 190U;
        tone(context, 820U, 35U, 3U, P4_WAVE_SQUARE);
        return;
    }
}

static void split_rock(asteroids_2_state_t *state, const rock_t *destroyed)
{
    if (destroyed->radius < 12U) return;
    for (size_t index = 0U; index < ROCK_COUNT; ++index) {
        if (!state->rocks[index].active) {
            spawn_rock(state, index, destroyed->x, destroyed->y,
                       (uint8_t)(destroyed->radius - 6U));
            return;
        }
    }
}

static void burst(asteroids_2_state_t *state, int x, int y)
{
    for (size_t index = 0U; index < sizeof(state->sparks) / sizeof(state->sparks[0]); ++index) {
        spark_t *const spark = &state->sparks[index];
        const uint32_t random = random_next(state);
        spark->x = (int16_t)x;
        spark->y = (int16_t)y;
        spark->vx = (int8_t)((int)(random & 7U) - 3);
        spark->vy = (int8_t)((int)((random >> 4U) & 7U) - 3);
        spark->ttl = (uint8_t)(8U + (random >> 8U) % 12U);
    }
}

static void update_sparks(asteroids_2_state_t *state)
{
    for (size_t index = 0U; index < sizeof(state->sparks) / sizeof(state->sparks[0]); ++index) {
        spark_t *const spark = &state->sparks[index];
        if (spark->ttl == 0U) continue;
        spark->x = wrap_x(spark->x + spark->vx);
        spark->y = wrap_y(spark->y + spark->vy);
        --spark->ttl;
    }
}

static int8_t aim_speed(int delta)
{
    if (delta > 12) return 2;
    if (delta < -12) return -2;
    return 0;
}

static void update_saucer(asteroids_2_state_t *state)
{
    if (state->saucer_active) {
        state->saucer_x = wrap_x(state->saucer_x + state->saucer_vx);
        state->saucer_fire_ms += STEP_MS;
        if (state->saucer_fire_ms >= 1200U && state->enemy_ttl_ms == 0U) {
            state->saucer_fire_ms = 0U;
            state->enemy_x = state->saucer_x;
            state->enemy_y = state->saucer_y;
            state->enemy_vx = aim_speed(state->ship_x - state->saucer_x);
            state->enemy_vy = aim_speed(state->ship_y - state->saucer_y);
            if (state->enemy_vx == 0 && state->enemy_vy == 0) state->enemy_vx = 1;
            state->enemy_ttl_ms = 1600U;
        }
        if (state->saucer_x < 8 || state->saucer_x > FIELD_WIDTH - 8) state->saucer_active = 0U;
        return;
    }
    state->saucer_wait_ms += STEP_MS;
    if (state->saucer_wait_ms >= 6000U) {
        state->saucer_wait_ms = 0U;
        state->saucer_active = 1U;
        state->saucer_vx = (random_next(state) & 1U) == 0U ? 1 : -1;
        state->saucer_x = state->saucer_vx > 0 ? 4 : FIELD_WIDTH - 5;
        state->saucer_y = (int16_t)(FIELD_TOP + 18 + random_next(state) % 100U);
    }
}

static void update_shots(p4_game_context_t *context, asteroids_2_state_t *state)
{
    for (size_t shot_index = 0U; shot_index < SHOT_COUNT; ++shot_index) {
        shot_t *const shot = &state->shots[shot_index];
        if (!shot->active) continue;
        if (shot->ttl_ms <= STEP_MS) { shot->active = 0U; continue; }
        shot->ttl_ms = (uint16_t)(shot->ttl_ms - STEP_MS);
        shot->x = wrap_x(shot->x + shot->vx); shot->y = wrap_y(shot->y + shot->vy);
        if (state->saucer_active) {
            const int saucer_dx = shot->x - state->saucer_x;
            const int saucer_dy = shot->y - state->saucer_y;
            if (saucer_dx * saucer_dx + saucer_dy * saucer_dy < 100) {
                state->saucer_active = 0U; shot->active = 0U;
                add_score(state, 200U); burst(state, state->saucer_x, state->saucer_y);
                tone(context, 720U, 110U, 4U, P4_WAVE_TRIANGLE);
                continue;
            }
        }
        for (size_t rock_index = 0U; rock_index < ROCK_COUNT; ++rock_index) {
            rock_t *const rock = &state->rocks[rock_index];
            const int dx = shot->x - rock->x, dy = shot->y - rock->y;
            if (!rock->active || dx * dx + dy * dy > rock->radius * rock->radius) continue;
            const rock_t destroyed = *rock;
            rock->active = 0U; shot->active = 0U;
            add_score(state, destroyed.radius >= 14U ? 25U : 60U);
            burst(state, destroyed.x, destroyed.y);
            split_rock(state, &destroyed);
            tone(context, (uint16_t)(220U + destroyed.radius * 13U), 60U, 4U,
                 P4_WAVE_TRIANGLE);
            break;
        }
    }
}

static void update_ship(p4_game_context_t *context, asteroids_2_state_t *state)
{
    state->turn_ms += STEP_MS;
    if (state->turn_ms >= 48U) {
        state->turn_ms = 0U;
        if ((state->held_buttons & P4_BUTTON_LEFT) != 0U)
            state->heading = (uint8_t)((state->heading + 15U) & 15U);
        if ((state->held_buttons & P4_BUTTON_RIGHT) != 0U)
            state->heading = (uint8_t)((state->heading + 1U) & 15U);
    }
    if ((state->held_buttons & P4_BUTTON_UP) != 0U) {
        state->ship_vx = (int8_t)(state->ship_vx + s_dx[state->heading] / 10);
        state->ship_vy = (int8_t)(state->ship_vy + s_dy[state->heading] / 10);
    }
    if (state->ship_vx > 3) state->ship_vx = 3;
    if (state->ship_vx < -3) state->ship_vx = -3;
    if (state->ship_vy > 3) state->ship_vy = 3;
    if (state->ship_vy < -3) state->ship_vy = -3;
    state->ship_x = wrap_x(state->ship_x + state->ship_vx);
    state->ship_y = wrap_y(state->ship_y + state->ship_vy);
    if (state->shield_ms != 0U) return;
    for (size_t index = 0U; index < ROCK_COUNT; ++index) {
        rock_t *const rock = &state->rocks[index];
        const int dx = state->ship_x - rock->x, dy = state->ship_y - rock->y;
        const int hit_radius = (int)rock->radius + 5;
        if (!rock->active || dx * dx + dy * dy > hit_radius * hit_radius) continue;
        rock->active = 0U;
        if (state->lives != 0U) --state->lives;
        burst(state, state->ship_x, state->ship_y);
        tone(context, 90U, 180U, 5U, P4_WAVE_TRIANGLE);
        if (state->lives == 0U) state->ended = 1U; else reset_ship(state);
        return;
    }
}

static void step(p4_game_context_t *context, asteroids_2_state_t *state)
{
    state->cooldown_ms = state->cooldown_ms > STEP_MS ? state->cooldown_ms - STEP_MS : 0U;
    state->shield_ms = state->shield_ms > STEP_MS ? state->shield_ms - STEP_MS : 0U;
    update_shots(context, state);
    for (size_t index = 0U; index < ROCK_COUNT; ++index)
        if (state->rocks[index].active) {
            state->rocks[index].x = wrap_x(state->rocks[index].x + state->rocks[index].vx);
            state->rocks[index].y = wrap_y(state->rocks[index].y + state->rocks[index].vy);
        }
    if (++state->rock_spin_ticks == 3U) {
        state->rock_spin_ticks = 0U;
        for (size_t index = 0U; index < ROCK_COUNT; ++index)
            state->rocks[index].spin = (uint8_t)((state->rocks[index].spin + 1U) & 15U);
    }
    update_sparks(state);
    update_saucer(state);
    if (state->enemy_ttl_ms > STEP_MS) {
        state->enemy_ttl_ms -= STEP_MS;
        state->enemy_x = wrap_x(state->enemy_x + state->enemy_vx);
        state->enemy_y = wrap_y(state->enemy_y + state->enemy_vy);
        const int dx = state->ship_x - state->enemy_x;
        const int dy = state->ship_y - state->enemy_y;
        if (state->shield_ms == 0U && dx * dx + dy * dy < 49) {
            state->enemy_ttl_ms = 0U;
            burst(state, state->ship_x, state->ship_y);
            if (state->lives != 0U) --state->lives;
            if (state->lives == 0U) state->ended = 1U; else reset_ship(state);
            tone(context, 110U, 180U, 5U, P4_WAVE_TRIANGLE);
        }
    } else {
        state->enemy_ttl_ms = 0U;
    }
    update_ship(context, state);
    if (!rocks_remain(state) && !state->ended) {
        ++state->level; begin_level(state);
        tone(context, 1047U, 100U, 4U, P4_WAVE_TRIANGLE);
    }
}

static void line(p4_game_surface_t *surface, int x0, int y0, int x1, int y1,
                 uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (unsigned steps = 0U; steps < 321U; ++steps) {
        p4_draw_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) return;
        const int twice_error = error * 2;
        if (twice_error >= dy) { error += dy; x0 += sx; }
        if (twice_error <= dx) { error += dx; y0 += sy; }
    }
}

static void draw_rock(p4_game_surface_t *surface, const rock_t *rock)
{
    int first_x = 0, first_y = 0, previous_x = 0, previous_y = 0;
    for (unsigned vertex = 0U; vertex < 7U; ++vertex) {
        const unsigned heading = (rock->spin + vertex * 2U) & 15U;
        const int radius = (int)rock->radius - (int)((vertex + rock->spin) % 3U);
        const int x = rock->x + s_dx[heading] * radius / 10;
        const int y = rock->y + s_dy[heading] * radius / 10;
        if (vertex == 0U) { first_x = x; first_y = y; }
        else line(surface, previous_x, previous_y, x, y, UINT16_C(0xbdf7));
        previous_x = x; previous_y = y;
    }
    line(surface, previous_x, previous_y, first_x, first_y, UINT16_C(0xbdf7));
}

static void draw_ship(p4_game_surface_t *surface, const asteroids_2_state_t *state)
{
    const unsigned left = (state->heading + 5U) & 15U;
    const unsigned right = (state->heading + 11U) & 15U;
    const int nose_x = state->ship_x + s_dx[state->heading] * 2;
    const int nose_y = state->ship_y + s_dy[state->heading] * 2;
    const int left_x = state->ship_x + s_dx[left];
    const int left_y = state->ship_y + s_dy[left];
    const int right_x = state->ship_x + s_dx[right];
    const int right_y = state->ship_y + s_dy[right];
    line(surface, nose_x, nose_y, left_x, left_y, UINT16_C(0x07ff));
    line(surface, left_x, left_y, right_x, right_y, UINT16_C(0x07ff));
    line(surface, right_x, right_y, nose_x, nose_y, UINT16_C(0x07ff));
    if ((state->held_buttons & P4_BUTTON_UP) != 0U) {
        const int tail_x = state->ship_x - s_dx[state->heading];
        const int tail_y = state->ship_y - s_dy[state->heading];
        p4_draw_fill_circle(surface, tail_x, tail_y, 2, UINT16_C(0xffe0));
    }
}

static void draw_value(p4_game_surface_t *surface, int x, uint32_t value)
{
    char text[11]; size_t position = sizeof(text) - 1U; text[position] = '\0';
    do { text[--position] = (char)('0' + value % 10U); value /= 10U; }
    while (value != 0U && position != 0U);
    p4_draw_text(surface, x, 5, &text[position], UINT16_C(0xffff), 1U,
                 sizeof(text) - position);
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(asteroids_2_state_t)) return false;
    new_game(context->state); tone(context, 523U, 75U, 3U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(p4_game_context_t *context,
                                    const p4_game_input_t *input, uint32_t elapsed_ms)
{
    asteroids_2_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) return P4_GAME_EXIT_TO_LAUNCHER;
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_START) != 0U && !state->ended) {
        state->paused = (uint8_t)!state->paused;
        tone(context, state->paused ? 240U : 520U, 50U, 3U, P4_WAVE_SQUARE);
    }
    if (state->ended) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) new_game(state);
        return P4_GAME_CONTINUE;
    }
    if (!state->paused && (input->held & P4_BUTTON_A) != 0U && state->cooldown_ms == 0U)
        fire(context, state);
    if (state->paused) return P4_GAME_CONTINUE;
    state->step_ms += elapsed_ms;
    while (state->step_ms >= STEP_MS) { state->step_ms -= STEP_MS; step(context, state); }
    return P4_GAME_CONTINUE;
}

static bool game_render(p4_game_context_t *context, p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) return false;
    const asteroids_2_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x0000));
    for (unsigned star = 0U; star < 32U; ++star) {
        const int star_x = (int)((star * 73U + 19U) % 317U);
        const int star_y = FIELD_TOP + (int)((star * 41U + 7U) % 146U);
        p4_draw_pixel(surface, star_x, star_y, star % 3U == 0U ? UINT16_C(0x8410) : UINT16_C(0x4208));
    }
    p4_draw_text(surface, 6, 5, "ASTEROIDS 2", UINT16_C(0x5fea), 1U, 11U);
    p4_draw_text(surface, 80, 5, "S", UINT16_C(0x9cf3), 1U, 1U); draw_value(surface, 88, state->score);
    p4_draw_text(surface, 140, 5, "L", UINT16_C(0x9cf3), 1U, 1U); draw_value(surface, 148, state->lives);
    p4_draw_text(surface, 176, 5, "LVL", UINT16_C(0x9cf3), 1U, 3U); draw_value(surface, 198, state->level);
    for (size_t index = 0U; index < ROCK_COUNT; ++index) {
        const rock_t *const rock = &state->rocks[index]; if (!rock->active) continue;
        draw_rock(surface, rock);
    }
    for (size_t index = 0U; index < SHOT_COUNT; ++index) if (state->shots[index].active)
        p4_draw_fill_rect(surface, state->shots[index].x - 1, state->shots[index].y - 1, 3, 3, UINT16_C(0xffe0));
    if (state->enemy_ttl_ms != 0U)
        p4_draw_fill_circle(surface, state->enemy_x, state->enemy_y, 2, UINT16_C(0xf81f));
    for (size_t index = 0U; index < sizeof(state->sparks) / sizeof(state->sparks[0]); ++index)
        if (state->sparks[index].ttl != 0U)
            p4_draw_fill_rect(surface, state->sparks[index].x, state->sparks[index].y, 2, 2, UINT16_C(0xfd20));
    if (state->saucer_active) {
        line(surface, state->saucer_x - 9, state->saucer_y, state->saucer_x + 9, state->saucer_y, UINT16_C(0xf81f));
        line(surface, state->saucer_x - 6, state->saucer_y, state->saucer_x - 3, state->saucer_y - 4, UINT16_C(0xf81f));
        line(surface, state->saucer_x - 3, state->saucer_y - 4, state->saucer_x + 3, state->saucer_y - 4, UINT16_C(0xf81f));
        line(surface, state->saucer_x + 3, state->saucer_y - 4, state->saucer_x + 6, state->saucer_y, UINT16_C(0xf81f));
    }
    if (!state->ended && (state->shield_ms == 0U || (state->shield_ms / 100U) % 2U == 0U)) draw_ship(surface, state);
    if (state->paused) p4_draw_text(surface, 132, 83, "PAUSED", UINT16_C(0xffe0), 1U, 6U);
    if (state->ended) { p4_draw_text(surface, 116, 78, "GAME OVER", UINT16_C(0xf81f), 1U, 9U); p4_draw_text(surface, 99, 91, "A RESTART", UINT16_C(0xffff), 1U, 9U); }
    p4_draw_text(surface, 47, 184, "TURN THRUST A FIRE", UINT16_C(0x9cf3), 1U, 19U);
    p4_game_draw_standard_controls(surface, UINT16_C(0x7bef), UINT16_C(0x5fea), state->held_buttons);
    return true;
}

static void game_stop(p4_game_context_t *context) { p4_game_stop_audio(context); }

const p4_game_descriptor_t p4_asteroids_2_game = {
    .api_version = P4_GAME_API_VERSION, .launcher_id = UINT32_C(104),
    .id = "org.p4console.asteroids-2", .title = "ASTEROIDS 2",
    .subtitle = "P4 GAME API V1", .accent_rgb565 = UINT16_C(0x5fea),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE, .state_bytes = sizeof(asteroids_2_state_t),
    .start = game_start, .update = game_update, .render = game_render, .stop = game_stop,
};
