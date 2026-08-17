// SPDX-License-Identifier: MIT
/* Original clean-room fixed-screen shooter for P4 Game API v1. */

#include "p4_games/space_invaders.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/input.h"
#include "space_invaders_internal.h"

enum {
    SPACE_FIELD_LEFT = 90,
    SPACE_FIELD_RIGHT = 230,
    SPACE_FIELD_TOP = 27,
    SPACE_FIELD_BOTTOM = 146,
    SPACE_PLAYER_Y = 136,
    SPACE_PLAYER_HALF_WIDTH = 7,
    SPACE_ALIEN_WIDTH = 11,
    SPACE_ALIEN_HEIGHT = 7,
    SPACE_ALIEN_COLUMN_STEP = 16,
    SPACE_ALIEN_ROW_STEP = 13,
    SPACE_FORMATION_STEP = 3,
    SPACE_FORMATION_DROP = 6,
    SPACE_FIXED_STEP_MS = 16,
    SPACE_PLAYER_COOLDOWN_MS = 220,
    SPACE_WAVE_DELAY_MS = 900,
    SPACE_RESPAWN_MS = 700,
    SPACE_SHIELD_CELL_WIDTH = 4,
    SPACE_SHIELD_CELL_HEIGHT = 3,
    SPACE_SHIELD_COLUMNS = 4,
    SPACE_SHIELD_ROWS = 3,
};

static const int16_t s_shield_x[SPACE_SHIELD_COUNT] = {112, 152, 192};
static const uint16_t s_alien_colors[SPACE_INVADER_ROWS] = {
    UINT16_C(0xf81f), UINT16_C(0x07ff),
    UINT16_C(0x07e0), UINT16_C(0xffe0),
};

static void play_tone(p4_game_context_t *context,
                      uint16_t frequency_hz,
                      uint16_t duration_ms,
                      uint8_t volume_step,
                      p4_waveform_t waveform)
{
    (void)p4_game_play_tone(context, frequency_hz, duration_ms,
                            volume_step, waveform);
}

static void clear_projectiles(space_invaders_state_t *state)
{
    memset(state->player_projectiles, 0, sizeof(state->player_projectiles));
    memset(state->enemy_projectiles, 0, sizeof(state->enemy_projectiles));
}

static void begin_wave(space_invaders_state_t *state)
{
    state->alive_mask = UINT32_MAX;
    state->invaders_remaining = SPACE_INVADER_COUNT;
    state->formation_x = 99;
    state->formation_y = 34;
    state->formation_direction = 1;
    state->formation_accumulator_ms = 0U;
    state->enemy_fire_accumulator_ms = 0U;
    state->player_cooldown_ms = 0U;
    state->wave_delay_ms = 0U;
    state->animation_phase = 0U;
    state->march_note = 0U;
    for (size_t i = 0U; i < SPACE_SHIELD_COUNT; ++i) {
        state->shields[i] = UINT16_C(0x0fff);
    }
    clear_projectiles(state);
}

void space_invaders_reset(space_invaders_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->random_state = UINT32_C(0x51ace77d);
    state->lives = 3U;
    state->wave = 1U;
    state->player_x = 160;
    begin_wave(state);
}

static uint32_t next_random(space_invaders_state_t *state)
{
    state->random_state = state->random_state * UINT32_C(1664525) +
        UINT32_C(1013904223);
    return state->random_state;
}

static bool alien_alive(const space_invaders_state_t *state,
                        unsigned row, unsigned column)
{
    const unsigned index = row * SPACE_INVADER_COLUMNS + column;
    return (state->alive_mask & (UINT32_C(1) << index)) != 0U;
}

static int alien_x(const space_invaders_state_t *state, unsigned column)
{
    return state->formation_x + (int)column * SPACE_ALIEN_COLUMN_STEP;
}

static int alien_y(const space_invaders_state_t *state, unsigned row)
{
    return state->formation_y + (int)row * SPACE_ALIEN_ROW_STEP;
}

static void living_column_bounds(const space_invaders_state_t *state,
                                 unsigned *first, unsigned *last)
{
    *first = 0U;
    *last = SPACE_INVADER_COLUMNS - 1U;
    bool found = false;
    for (unsigned column = 0U;
         column < SPACE_INVADER_COLUMNS && !found; ++column) {
        for (unsigned row = 0U; row < SPACE_INVADER_ROWS; ++row) {
            if (alien_alive(state, row, column)) {
                *first = column;
                found = true;
                break;
            }
        }
    }
    for (unsigned column = SPACE_INVADER_COLUMNS; column > 0U; --column) {
        const unsigned candidate = column - 1U;
        for (unsigned row = 0U; row < SPACE_INVADER_ROWS; ++row) {
            if (alien_alive(state, row, candidate)) {
                *last = candidate;
                return;
            }
        }
    }
}

static uint32_t formation_interval_ms(const space_invaders_state_t *state)
{
    int interval = 470 -
        (SPACE_INVADER_COUNT - (int)state->invaders_remaining) * 10 -
        ((int)state->wave - 1) * 18;
    if (interval < 85) {
        interval = 85;
    }
    return (uint32_t)interval;
}

static uint32_t enemy_fire_interval_ms(const space_invaders_state_t *state)
{
    int interval = 920 - ((int)state->wave - 1) * 55 -
        (SPACE_INVADER_COUNT - (int)state->invaders_remaining) * 8;
    if (interval < 260) {
        interval = 260;
    }
    return (uint32_t)interval;
}

static void move_formation(p4_game_context_t *context,
                           space_invaders_state_t *state)
{
    unsigned first = 0U;
    unsigned last = SPACE_INVADER_COLUMNS - 1U;
    living_column_bounds(state, &first, &last);
    const int next_x = state->formation_x +
        (int)state->formation_direction * SPACE_FORMATION_STEP;
    const int next_left = next_x + (int)first * SPACE_ALIEN_COLUMN_STEP;
    const int next_right = next_x + (int)last * SPACE_ALIEN_COLUMN_STEP +
        SPACE_ALIEN_WIDTH;
    if (next_left < SPACE_FIELD_LEFT || next_right > SPACE_FIELD_RIGHT) {
        state->formation_direction = (int8_t)-state->formation_direction;
        state->formation_y += SPACE_FORMATION_DROP;
        play_tone(context, 92U, 45U, 2U, P4_WAVE_TRIANGLE);
    } else {
        state->formation_x = (int16_t)next_x;
        if ((state->march_note & 1U) == 0U) {
            const uint16_t note = (uint16_t)(112U +
                (uint16_t)(state->march_note & 3U) * 14U);
            play_tone(context, note, 24U, 2U, P4_WAVE_SQUARE);
        }
        ++state->march_note;
    }
    state->animation_phase ^= 1U;
}

static bool spawn_projectile(space_projectile_t *projectiles,
                             size_t count, int x, int y)
{
    for (size_t i = 0U; i < count; ++i) {
        if (!projectiles[i].active) {
            projectiles[i] = (space_projectile_t){
                .x = (int16_t)x, .y = (int16_t)y, .active = true,
            };
            return true;
        }
    }
    return false;
}

static void player_fire(p4_game_context_t *context,
                        space_invaders_state_t *state)
{
    if (state->player_cooldown_ms != 0U || state->respawn_ms != 0U ||
        state->wave_delay_ms != 0U) {
        return;
    }
    if (spawn_projectile(state->player_projectiles,
                         SPACE_PLAYER_PROJECTILE_COUNT,
                         state->player_x, SPACE_PLAYER_Y - 6)) {
        state->player_cooldown_ms = SPACE_PLAYER_COOLDOWN_MS;
        play_tone(context, 880U, 55U, 4U, P4_WAVE_SQUARE);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_ACTION);
    }
}

static void enemy_fire(space_invaders_state_t *state)
{
    const unsigned start = next_random(state) % SPACE_INVADER_COLUMNS;
    for (unsigned offset = 0U; offset < SPACE_INVADER_COLUMNS; ++offset) {
        const unsigned column = (start + offset) % SPACE_INVADER_COLUMNS;
        for (unsigned row_value = SPACE_INVADER_ROWS;
             row_value > 0U; --row_value) {
            const unsigned row = row_value - 1U;
            if (alien_alive(state, row, column)) {
                (void)spawn_projectile(
                    state->enemy_projectiles, SPACE_ENEMY_PROJECTILE_COUNT,
                    alien_x(state, column) + SPACE_ALIEN_WIDTH / 2,
                    alien_y(state, row) + SPACE_ALIEN_HEIGHT + 1);
                return;
            }
        }
    }
}

static bool point_in_rect(int x, int y, int left, int top,
                          int width, int height)
{
    return x >= left && x < left + width &&
        y >= top && y < top + height;
}

static bool projectile_hits_shield(space_invaders_state_t *state,
                                   space_projectile_t *projectile)
{
    for (size_t shield = 0U; shield < SPACE_SHIELD_COUNT; ++shield) {
        for (unsigned row = 0U; row < SPACE_SHIELD_ROWS; ++row) {
            for (unsigned column = 0U; column < SPACE_SHIELD_COLUMNS;
                 ++column) {
                const unsigned bit = row * SPACE_SHIELD_COLUMNS + column;
                if ((state->shields[shield] &
                     (UINT16_C(1) << bit)) == 0U) {
                    continue;
                }
                const int left = s_shield_x[shield] +
                    (int)column * SPACE_SHIELD_CELL_WIDTH;
                const int top = 115 + (int)row * SPACE_SHIELD_CELL_HEIGHT;
                if (point_in_rect(projectile->x, projectile->y,
                                  left, top, SPACE_SHIELD_CELL_WIDTH,
                                  SPACE_SHIELD_CELL_HEIGHT)) {
                    state->shields[shield] &=
                        (uint16_t)~(UINT16_C(1) << bit);
                    projectile->active = false;
                    return true;
                }
            }
        }
    }
    return false;
}

static void hit_alien(p4_game_context_t *context,
                      space_invaders_state_t *state,
                      unsigned row, unsigned column)
{
    const unsigned index = row * SPACE_INVADER_COLUMNS + column;
    state->alive_mask &= ~(UINT32_C(1) << index);
    if (state->invaders_remaining != 0U) {
        --state->invaders_remaining;
    }
    state->score += (uint32_t)(SPACE_INVADER_ROWS - row) * 10U;
    play_tone(context, (uint16_t)(260U + row * 55U),
              75U, 4U, P4_WAVE_TRIANGLE);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_IMPACT);
    if (state->invaders_remaining == 0U) {
        state->wave_delay_ms = SPACE_WAVE_DELAY_MS;
        play_tone(context, 1047U, 320U, 5U, P4_WAVE_TRIANGLE);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
    }
}

static void update_player_projectiles(p4_game_context_t *context,
                                      space_invaders_state_t *state)
{
    for (size_t i = 0U; i < SPACE_PLAYER_PROJECTILE_COUNT; ++i) {
        space_projectile_t *const projectile = &state->player_projectiles[i];
        if (!projectile->active) {
            continue;
        }
        projectile->y -= 4;
        if (projectile->y < SPACE_FIELD_TOP) {
            projectile->active = false;
            continue;
        }
        if (projectile_hits_shield(state, projectile)) {
            continue;
        }
        bool hit = false;
        for (unsigned row = 0U; row < SPACE_INVADER_ROWS && !hit; ++row) {
            for (unsigned column = 0U; column < SPACE_INVADER_COLUMNS;
                 ++column) {
                if (alien_alive(state, row, column) &&
                    point_in_rect(projectile->x, projectile->y,
                                  alien_x(state, column), alien_y(state, row),
                                  SPACE_ALIEN_WIDTH, SPACE_ALIEN_HEIGHT)) {
                    projectile->active = false;
                    hit_alien(context, state, row, column);
                    hit = true;
                    break;
                }
            }
        }
    }
}

static void player_hit(p4_game_context_t *context,
                       space_invaders_state_t *state)
{
    if (state->respawn_ms != 0U || state->game_over) {
        return;
    }
    if (state->lives != 0U) {
        --state->lives;
    }
    play_tone(context, 82U, 420U, 6U, P4_WAVE_TRIANGLE);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
    clear_projectiles(state);
    if (state->lives == 0U) {
        state->game_over = true;
    } else {
        state->respawn_ms = SPACE_RESPAWN_MS;
        state->player_x = 160;
    }
}

static void update_enemy_projectiles(p4_game_context_t *context,
                                     space_invaders_state_t *state)
{
    for (size_t i = 0U; i < SPACE_ENEMY_PROJECTILE_COUNT; ++i) {
        space_projectile_t *const projectile = &state->enemy_projectiles[i];
        if (!projectile->active) {
            continue;
        }
        projectile->y += 2;
        if (projectile->y > SPACE_FIELD_BOTTOM) {
            projectile->active = false;
            continue;
        }
        if (projectile_hits_shield(state, projectile)) {
            continue;
        }
        if (state->respawn_ms == 0U &&
            point_in_rect(projectile->x, projectile->y,
                          state->player_x - SPACE_PLAYER_HALF_WIDTH,
                          SPACE_PLAYER_Y - 4,
                          SPACE_PLAYER_HALF_WIDTH * 2 + 1, 7)) {
            projectile->active = false;
            player_hit(context, state);
            return;
        }
    }
}

static void subtract_bounded(uint32_t *value, uint32_t amount)
{
    *value = *value > amount ? *value - amount : 0U;
}

static void simulate_step(p4_game_context_t *context,
                          space_invaders_state_t *state)
{
    subtract_bounded(&state->player_cooldown_ms, SPACE_FIXED_STEP_MS);
    subtract_bounded(&state->respawn_ms, SPACE_FIXED_STEP_MS);
    if ((state->held_buttons & P4_BUTTON_LEFT) != 0U &&
        (state->held_buttons & P4_BUTTON_RIGHT) == 0U &&
        state->player_x > SPACE_FIELD_LEFT + SPACE_PLAYER_HALF_WIDTH) {
        const int next = state->player_x - 2;
        state->player_x = (int16_t)(
            next < SPACE_FIELD_LEFT + SPACE_PLAYER_HALF_WIDTH
                ? SPACE_FIELD_LEFT + SPACE_PLAYER_HALF_WIDTH : next);
    } else if ((state->held_buttons & P4_BUTTON_RIGHT) != 0U &&
               (state->held_buttons & P4_BUTTON_LEFT) == 0U &&
               state->player_x < SPACE_FIELD_RIGHT - SPACE_PLAYER_HALF_WIDTH) {
        const int next = state->player_x + 2;
        state->player_x = (int16_t)(
            next > SPACE_FIELD_RIGHT - SPACE_PLAYER_HALF_WIDTH
                ? SPACE_FIELD_RIGHT - SPACE_PLAYER_HALF_WIDTH : next);
    }
    if (state->wave_delay_ms != 0U) {
        if (state->wave_delay_ms <= SPACE_FIXED_STEP_MS) {
            if (state->wave != UINT8_MAX) {
                ++state->wave;
            }
            begin_wave(state);
            play_tone(context, 659U, 180U, 4U, P4_WAVE_TRIANGLE);
        } else {
            state->wave_delay_ms -= SPACE_FIXED_STEP_MS;
        }
        return;
    }
    state->formation_accumulator_ms += SPACE_FIXED_STEP_MS;
    const uint32_t formation_interval = formation_interval_ms(state);
    if (state->formation_accumulator_ms >= formation_interval) {
        state->formation_accumulator_ms -= formation_interval;
        move_formation(context, state);
    }
    state->enemy_fire_accumulator_ms += SPACE_FIXED_STEP_MS;
    const uint32_t fire_interval = enemy_fire_interval_ms(state);
    if (state->enemy_fire_accumulator_ms >= fire_interval) {
        state->enemy_fire_accumulator_ms -= fire_interval;
        enemy_fire(state);
    }
    update_player_projectiles(context, state);
    update_enemy_projectiles(context, state);
    const int lowest = state->formation_y +
        (SPACE_INVADER_ROWS - 1) * SPACE_ALIEN_ROW_STEP + SPACE_ALIEN_HEIGHT;
    if (lowest >= SPACE_PLAYER_Y - 5) {
        state->lives = 0U;
        state->game_over = true;
        clear_projectiles(state);
        play_tone(context, 65U, 650U, 6U, P4_WAVE_TRIANGLE);
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(space_invaders_state_t)) {
        return false;
    }
    space_invaders_reset(context->state);
    play_tone(context, 523U, 120U, 4U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(p4_game_context_t *context,
                                    const p4_game_input_t *input,
                                    uint32_t elapsed_ms)
{
    space_invaders_state_t *const state = context->state;
    (void)p4_game_audio_effect_service(context, &state->audio);
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    if (state->game_over) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            space_invaders_reset(state);
            play_tone(context, 523U, 120U, 4U, P4_WAVE_TRIANGLE);
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
    if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_B)) != 0U) {
        player_fire(context, state);
    }
    if (UINT32_MAX - state->simulation_accumulator_ms < elapsed_ms) {
        state->simulation_accumulator_ms = UINT32_MAX;
    } else {
        state->simulation_accumulator_ms += elapsed_ms;
    }
    while (state->simulation_accumulator_ms >= SPACE_FIXED_STEP_MS) {
        state->simulation_accumulator_ms -= SPACE_FIXED_STEP_MS;
        simulate_step(context, state);
        if (state->game_over) {
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
    for (size_t i = 0U; i < count; ++i) {
        output[i] = reversed[count - i - 1U];
    }
    output[count] = '\0';
}

static void draw_starfield(p4_game_surface_t *surface)
{
    for (unsigned i = 0U; i < 42U; ++i) {
        const int x = 88 + (int)((i * 37U + 11U) % 146U);
        const int y = 26 + (int)((i * 29U + 7U) % 121U);
        const uint16_t color = (i % 5U) == 0U
            ? UINT16_C(0x8410) : UINT16_C(0x3186);
        p4_draw_pixel(surface, x, y, color);
    }
}

static void draw_alien(p4_game_surface_t *surface, int x, int y,
                       unsigned row, uint8_t animation_phase)
{
    const uint16_t color = s_alien_colors[row];
    p4_draw_fill_rect(surface, x + 2, y + 1, 7, 5, color);
    p4_draw_fill_rect(surface, x, y + 3, 11, 3, color);
    p4_draw_pixel(surface, x + 3, y + 2, UINT16_C(0x0000));
    p4_draw_pixel(surface, x + 7, y + 2, UINT16_C(0x0000));
    p4_draw_pixel(surface, x + 1, y, color);
    p4_draw_pixel(surface, x + 9, y, color);
    p4_draw_pixel(surface, animation_phase == 0U ? x + 2 : x, y + 6, color);
    p4_draw_pixel(surface, animation_phase == 0U ? x + 8 : x + 10,
                  y + 6, color);
}

static void draw_player(p4_game_surface_t *surface,
                        const space_invaders_state_t *state)
{
    if (state->respawn_ms != 0U &&
        (state->respawn_ms / 80U) % 2U == 0U) {
        return;
    }
    p4_draw_fill_rect(surface, state->player_x - 7,
                      SPACE_PLAYER_Y - 1, 15, 4, UINT16_C(0x07e0));
    p4_draw_fill_rect(surface, state->player_x - 4,
                      SPACE_PLAYER_Y - 4, 9, 3, UINT16_C(0x07ff));
    p4_draw_fill_rect(surface, state->player_x - 1,
                      SPACE_PLAYER_Y - 6, 3, 2, UINT16_C(0xffff));
}

static void draw_shields(p4_game_surface_t *surface,
                         const space_invaders_state_t *state)
{
    for (size_t shield = 0U; shield < SPACE_SHIELD_COUNT; ++shield) {
        for (unsigned row = 0U; row < SPACE_SHIELD_ROWS; ++row) {
            for (unsigned column = 0U; column < SPACE_SHIELD_COLUMNS;
                 ++column) {
                const unsigned bit = row * SPACE_SHIELD_COLUMNS + column;
                if ((state->shields[shield] &
                     (UINT16_C(1) << bit)) != 0U) {
                    p4_draw_fill_rect(
                        surface, s_shield_x[shield] +
                            (int)column * SPACE_SHIELD_CELL_WIDTH,
                        115 + (int)row * SPACE_SHIELD_CELL_HEIGHT,
                        SPACE_SHIELD_CELL_WIDTH, SPACE_SHIELD_CELL_HEIGHT,
                        UINT16_C(0x05e0));
                }
            }
        }
    }
}

static void draw_projectiles(p4_game_surface_t *surface,
                             const space_invaders_state_t *state)
{
    for (size_t i = 0U; i < SPACE_PLAYER_PROJECTILE_COUNT; ++i) {
        if (state->player_projectiles[i].active) {
            p4_draw_fill_rect(surface, state->player_projectiles[i].x,
                              state->player_projectiles[i].y,
                              1, 4, UINT16_C(0xffff));
        }
    }
    for (size_t i = 0U; i < SPACE_ENEMY_PROJECTILE_COUNT; ++i) {
        if (state->enemy_projectiles[i].active) {
            p4_draw_fill_rect(surface, state->enemy_projectiles[i].x,
                              state->enemy_projectiles[i].y,
                              2, 4, UINT16_C(0xf904));
        }
    }
}

static void draw_hud(p4_game_surface_t *surface,
                     const space_invaders_state_t *state)
{
    char value[11];
    p4_draw_text(surface, 118, 7, "SPACE INVADERS",
                 UINT16_C(0x07ff), 1U, 14U);
    p4_draw_text(surface, 58, 7, "S", UINT16_C(0x9cf3), 1U, 1U);
    u32_text(state->score, value);
    p4_draw_text(surface, 66, 7, value, UINT16_C(0xffff), 1U, 7U);
    p4_draw_text(surface, 204, 7, "W", UINT16_C(0x9cf3), 1U, 1U);
    u32_text(state->wave, value);
    p4_draw_text(surface, 212, 7, value, UINT16_C(0xffff), 1U, 3U);
    p4_draw_text(surface, 232, 7, "L", UINT16_C(0x9cf3), 1U, 1U);
    u32_text(state->lives, value);
    p4_draw_text(surface, 240, 7, value, UINT16_C(0xffff), 1U, 2U);
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const space_invaders_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x0004));
    draw_starfield(surface);
    p4_draw_rect(surface, SPACE_FIELD_LEFT - 2, SPACE_FIELD_TOP - 2,
                 SPACE_FIELD_RIGHT - SPACE_FIELD_LEFT + 5,
                 SPACE_FIELD_BOTTOM - SPACE_FIELD_TOP + 5,
                 UINT16_C(0x2104));
    p4_draw_fill_rect(surface, SPACE_FIELD_LEFT, SPACE_FIELD_BOTTOM,
                      SPACE_FIELD_RIGHT - SPACE_FIELD_LEFT + 1, 1,
                      UINT16_C(0x07e0));
    draw_hud(surface, state);
    for (unsigned row = 0U; row < SPACE_INVADER_ROWS; ++row) {
        for (unsigned column = 0U; column < SPACE_INVADER_COLUMNS; ++column) {
            if (alien_alive(state, row, column)) {
                draw_alien(surface, alien_x(state, column),
                           alien_y(state, row), row, state->animation_phase);
            }
        }
    }
    draw_shields(surface, state);
    draw_projectiles(surface, state);
    p4_game_feedback_draw(
        surface, state->game_over ? P4_GAME_FX_FAIL : P4_GAME_FX_ACTION,
        state->game_over ? 160 : state->player_x,
        state->game_over ? 88 : SPACE_PLAYER_Y - 4, context->frame_index);
    draw_player(surface, state);
    p4_game_draw_standard_controls(
        surface, UINT16_C(0x4208), UINT16_C(0x07ff), state->held_buttons);
    if (state->paused || state->game_over || state->wave_delay_ms != 0U) {
        p4_draw_fill_rect(surface, 105, 69, 110, 38, UINT16_C(0x1082));
        p4_draw_rect(surface, 105, 69, 110, 38, UINT16_C(0xffff));
        const char *const message = state->game_over ? "GAME OVER" :
            (state->paused ? "PAUSED" : "WAVE CLEAR");
        p4_draw_text(surface,
                     state->paused ? 139 :
                     (state->game_over ? 133 : 127),
                     78, message, UINT16_C(0xffff), 1U, 10U);
        p4_draw_text(surface, state->game_over ? 116 : 122, 93,
                     state->game_over ? "A TO RESTART" :
                     (state->paused ? "START TO RESUME" : "GET READY"),
                     UINT16_C(0x9cf3), 1U, 15U);
    }
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_space_invaders_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(101),
    .id = "org.p4console.space-invaders",
    .title = "SPACE INVADERS",
    .subtitle = "DEFEND THE P4",
    .accent_rgb565 = UINT16_C(0x07ff),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM,
    .state_bytes = sizeof(space_invaders_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
