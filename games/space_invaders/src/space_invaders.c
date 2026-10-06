// SPDX-License-Identifier: MIT
/* Original clean-room fixed-screen shooter for P4 Game API v1. */

#include "p4_games/space_invaders.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/input.h"
#include "hires.h"
#include "space_invaders_internal.h"
#include "generated/space_backdrop.inc"

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
    state->formation_from_x = state->formation_x;
    state->formation_from_y = state->formation_y;
    state->formation_tween_ms = 140U;
    state->impact_ms = 0U;
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
    state->intro = true;
    state->random_state = UINT32_C(0x51ace77d);
    state->lives = 3U;
    state->wave = 1U;
    state->player_x = 160;
    state->previous_player_x = 160;
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
    state->formation_from_x = state->formation_x;
    state->formation_from_y = state->formation_y;
    state->formation_tween_ms = 0U;
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
                .x = (int16_t)x, .y = (int16_t)y, .previous_y = (int16_t)y, .active = true,
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
                    state->impact_x = projectile->x;
                    state->impact_y = projectile->y;
                    state->impact_ms = 128U;
                    state->impact_frame = 15U;
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
    state->impact_x = (int16_t)(alien_x(state, column) + SPACE_ALIEN_WIDTH / 2);
    state->impact_y = (int16_t)(alien_y(state, row) + SPACE_ALIEN_HEIGHT / 2);
    state->impact_ms = 192U;
    state->impact_frame = 14U;
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
        projectile->previous_y = projectile->y;
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
    state->previous_player_x = 160;
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
        projectile->previous_y = projectile->y;
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
    state->previous_player_x = state->player_x;
    state->visual_clock_ms = (state->visual_clock_ms + SPACE_FIXED_STEP_MS) % 60000U;
    if (state->formation_tween_ms < 140U) {
        state->formation_tween_ms += SPACE_FIXED_STEP_MS;
    }
    subtract_bounded(&state->impact_ms, SPACE_FIXED_STEP_MS);
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
    /* Holding either fire action repeats at the original bounded cooldown.
     * Press edges still launch immediately; this never bypasses the two slots. */
    if ((state->held_buttons & (P4_BUTTON_A | P4_BUTTON_B)) != 0U)
        player_fire(context,state);
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
    const bool touch = input->touch_valid && input->touch_count != 0U;
    if (state->intro) {
        /* A title gesture belongs to the control where it began. The shared
         * mapper also synthesizes buttons, so hidden combat zones cannot start
         * a match and dragging toward Exit cannot quit one accidentally. */
        const bool fresh = touch && !state->title_touch_down;
        const bool released_touch = !touch && state->title_touch_down;
        state->title_touch_down = touch;
        const bool single = fresh && input->touch_count == 1U;
        const unsigned x = single ? input->touches[0].x : 320U;
        const unsigned y = single ? input->touches[0].y : 200U;
        const uint32_t pressed = touch || released_touch ? 0U : input->pressed;
        if ((pressed & P4_BUTTON_BACK) != 0U ||
            (single && x < 52U && y < 24U))
            return P4_GAME_EXIT_TO_LAUNCHER;
        if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U ||
            (single && x >= 191U && x < 297U && y >= 153U && y < 183U)) {
            state->intro = false;
            state->launch_input_blocked = true;
            state->title_touch_down = false;
            state->visual_clock_ms = 0U;
            play_tone(context, 659U, 90U, 3U, P4_WAVE_TRIANGLE);
        } else {
            state->visual_clock_ms = (state->visual_clock_ms + elapsed_ms % 60000U) % 60000U;
        }
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    /* A held Play/A/Start or starting touch must be released before it can
     * become combat fire or pause, including repeated host-service slices. */
    const uint32_t launch_buttons = P4_BUTTON_A | P4_BUTTON_B | P4_BUTTON_START;
    if (state->launch_input_blocked && !touch &&
        (input->held & launch_buttons) == 0U)
        state->launch_input_blocked = false;
    const uint32_t pressed = state->launch_input_blocked
        ? input->pressed & ~launch_buttons : input->pressed;
    state->held_buttons = state->launch_input_blocked
        ? input->held & ~launch_buttons : input->held;
    if (state->game_over) {
        if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            space_invaders_reset(state);
            state->intro = false;
            state->launch_input_blocked = true;
            play_tone(context, 523U, 120U, 4U, P4_WAVE_TRIANGLE);
        }
        return P4_GAME_CONTINUE;
    }
    if ((pressed & P4_BUTTON_START) != 0U) {
        state->paused = !state->paused;
        play_tone(context, state->paused ? 330U : 660U,
                  80U, 3U, P4_WAVE_SQUARE);
    }
    if (state->paused) {
        return P4_GAME_CONTINUE;
    }
    if ((pressed & (P4_BUTTON_A | P4_BUTTON_B)) != 0U) {
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

/* The rules retain their original compact world. Only presentation expands it. */
static int field_x(const p4_game_surface_t *surface, int world_q8)
{
    const int view_q8 = 16 * 256 + (world_q8 - 90 * 256) * 288 / 140;
    return view_q8 * (int)surface->width / (320 * 256);
}

static int field_y(const p4_game_surface_t *surface, int world_q8)
{
    const int view_q8 = 28 * 256 + (world_q8 - 27 * 256) * 117 / 119;
    return view_q8 * (int)surface->height / (200 * 256);
}

static void field_rect(p4_game_surface_t *surface, int x, int y,
                       int w, int h, uint16_t color)
{
    const int left = field_x(surface, x * 256);
    const int top = field_y(surface, y * 256);
    p4_draw_fill_rect(surface, left, top,
        field_x(surface, (x + w) * 256) - left,
        field_y(surface, (y + h) * 256) - top, color);
}

static void field_frame(p4_game_surface_t *surface, int x_q8, int y_q8,
                        int w, int h, unsigned frame)
{
    const int x = field_x(surface, x_q8), y = field_y(surface, y_q8);
    p4_ui_sprite(surface, x, y,
        field_x(surface, x_q8 + w * 256) - x,
        field_y(surface, y_q8 + h * 256) - y,
        hi_art[frame & 15U], HI_ART_W, HI_ART_H, true, 0U);
}

/* Interpolate consecutive fixed simulation samples. The 16 ms visual delay
 * removes the occasional double-step at 60 Hz without changing rule timing. */
int space_invaders_motion_visual_q8(int previous, int current, uint32_t remainder_ms)
{
    if (remainder_ms > SPACE_FIXED_STEP_MS) remainder_ms = SPACE_FIXED_STEP_MS;
    return previous * 256 + (current - previous) * (int)remainder_ms * 256 / SPACE_FIXED_STEP_MS;
}

/* Advance only rendering between authoritative formation steps. */
int space_invaders_formation_visual_q8(const space_invaders_state_t *state, bool horizontal)
{
    const int from = horizontal ? state->formation_from_x : state->formation_from_y;
    const int to = horizontal ? state->formation_x : state->formation_y;
    uint32_t time = state->formation_tween_ms + state->simulation_accumulator_ms;
    const uint32_t interval = formation_interval_ms(state);
    const uint32_t duration = interval > 156U ? 140U : interval - 16U;
    if (time > duration) time = duration;
    return from * 256 + (to - from) * 256 * (int)time / (int)duration;
}

static void draw_starfield(p4_game_surface_t *surface,
                           const space_invaders_state_t *state)
{
    /* One checked surface, row-wise writes, no per-pixel API validation. */
    /* Opaque HUD and controls cover the rest; avoid drawing it twice. */
    const unsigned first = (unsigned)p4_ui_y(surface,24), last = (unsigned)p4_ui_y(surface,150);
    for (unsigned y = first; y < last; ++y) {
        const uint16_t *src = &space_backdrop[(y * 192U / surface->height) * 384U];
        uint16_t *dst = surface->pixels + (size_t)y * surface->stride_pixels;
        if (surface->width == 768U) {
            for (unsigned x = 0U; x < 384U; ++x) {
                dst[x * 2U] = src[x];
                dst[x * 2U + 1U] = src[x];
            }
        } else {
            for (unsigned x = 0U; x < surface->width; ++x)
                dst[x] = src[x * 384U / surface->width];
        }
    }
    for (unsigned i = 0U; i < 28U; ++i) {
        const int x = (int)((i * 173U + 41U) % surface->width);
        const int y = p4_ui_y(surface, 28) + (int)((i * 97U + 31U) % (unsigned)p4_ui_y(surface, 116));
        const unsigned phase = ((state->visual_clock_ms / 90U) + i * 7U) % 32U;
        const unsigned light = phase < 16U ? phase : 31U - phase;
        p4_draw_pixel(surface, x, y, p4_ui_blend(0x294bU, 0xbdf7U, light));
        if (light > 13U && i % 7U == 0U)
            p4_draw_fill_rect(surface, x - 1, y, 3, 1, 0x8c73U);
    }
}

static void draw_alien(p4_game_surface_t *surface,
                       const space_invaders_state_t *state,
                       unsigned row, unsigned column)
{
    const unsigned frame = (state->visual_clock_ms / 90U + column / 2U + row) & 3U;
    field_frame(surface,
        space_invaders_formation_visual_q8(state, true) + ((int)column * SPACE_ALIEN_COLUMN_STEP - 1) * 256,
        space_invaders_formation_visual_q8(state, false) + ((int)row * SPACE_ALIEN_ROW_STEP - 4) * 256,
        13, 15, (row & 1U) * 4U + frame);
}

static void draw_player(p4_game_surface_t *surface,
                        const space_invaders_state_t *state)
{
    if (state->respawn_ms != 0U && (state->respawn_ms / 80U) % 2U == 0U) return;
    field_frame(surface, space_invaders_motion_visual_q8(state->previous_player_x,
        state->player_x,state->simulation_accumulator_ms) - 8 * 256,
        (SPACE_PLAYER_Y - 10) * 256, 16, 18,
        8U + ((state->visual_clock_ms / 70U) & 3U));
}

static void draw_shields(p4_game_surface_t *surface,
                         const space_invaders_state_t *state)
{
    for (unsigned shield = 0U; shield < SPACE_SHIELD_COUNT; ++shield) {
        const int left = field_x(surface, s_shield_x[shield] * 256);
        const int top = field_y(surface, 115 * 256);
        const int width = field_x(surface, (s_shield_x[shield] + 16) * 256) - left;
        const int height = field_y(surface, 124 * 256) - top;
        /* Build the bounded horizontal sampler once, outside the pixel loop. */
        uint8_t source_x[96];
        if (width <= 0 || width > (int)sizeof(source_x) || height <= 0) continue;
        for (int x=0; x<width; ++x) source_x[x]=(uint8_t)(x*HI_ART_W/width);
        /* The painted bunker is cropped to its metal hull. Missing cells stay
           fully transparent, exactly matching the twelve collision cells. */
        for (unsigned row = 0U; row < 3U; ++row) {
            const int y0 = field_y(surface, (115 + (int)row * 3) * 256);
            const int y1 = field_y(surface, (118 + (int)row * 3) * 256);
            for (unsigned col = 0U; col < 4U; ++col) {
                if ((state->shields[shield] & (UINT16_C(1) << (row * 4U + col))) == 0U) continue;
                const int x0 = field_x(surface, (s_shield_x[shield] + (int)col * 4) * 256);
                const int x1 = field_x(surface, (s_shield_x[shield] + (int)col * 4 + 4) * 256);
                for (int y = y0; y < y1; ++y) {
                    const unsigned sy = (unsigned)(5 * HI_ART_H / 32 + (y - top) * 22 * HI_ART_H / (32 * height));
                    uint16_t *dst = surface->pixels + (size_t)y * surface->stride_pixels;
                    for (int x = x0; x < x1; ++x) {
                        const uint16_t texture = hi_art[12][sy * HI_ART_W + source_x[x - left]];
                        if (texture != 0U) dst[x] = texture;
                    }
                }
            }
        }
        /* Exposed broken edges read as charred metal, not a second block type. */
        for (unsigned row = 0U; row < 3U; ++row) {
            for (unsigned col = 0U; col < 4U; ++col) {
                const unsigned bit = row * 4U + col;
                if ((state->shields[shield] & (UINT16_C(1) << bit)) == 0U) continue;
                if (row > 0U && (state->shields[shield] & (UINT16_C(1) << (bit - 4U))) == 0U)
                    field_rect(surface, s_shield_x[shield] + (int)col * 4, 115 + (int)row * 3, 4, 1, 0x72e6U);
            }
        }
    }
}

static void draw_laser(p4_game_surface_t *surface, int x, int y_q8, bool enemy)
{
    const int px=field_x(surface,x*256), py=field_y(surface,y_q8);
    const bool native=surface->width==768U;
    const int height=native?15:6, width=native?5:3;
    const int top=enemy?py-height+1:py;
    p4_draw_fill_rect(surface,px-width/2,top,width,height,enemy?0x880cU:0x12b1U);
    p4_draw_fill_rect(surface,px-(native?1:0),top+(native?2:1),native?3:1,
        height-(native?4:2),enemy?0xfbacU:0x5effU);
    p4_draw_fill_rect(surface,px,enemy?py-(native?3:1):py,1,native?4:2,0xffffU);
}

static void draw_projectiles(p4_game_surface_t *surface,
                             const space_invaders_state_t *state)
{
    for (size_t i = 0U; i < SPACE_PLAYER_PROJECTILE_COUNT; ++i) {
        const space_projectile_t *shot = &state->player_projectiles[i];
        if (!shot->active) continue;
        draw_laser(surface,shot->x,space_invaders_motion_visual_q8(shot->previous_y,
            shot->y,state->simulation_accumulator_ms),false);
    }
    for (size_t i = 0U; i < SPACE_ENEMY_PROJECTILE_COUNT; ++i) {
        const space_projectile_t *shot = &state->enemy_projectiles[i];
        if (!shot->active) continue;
        draw_laser(surface,shot->x,space_invaders_motion_visual_q8(shot->previous_y,
            shot->y,state->simulation_accumulator_ms),true);
    }
    if (state->impact_ms != 0U) {
        const int size = state->impact_ms > 96U ? 13 : 9;
        field_frame(surface, (state->impact_x - size / 2) * 256,
            (state->impact_y - size / 2) * 256, size, size, state->impact_frame);
    }
}

static void draw_controls(p4_game_surface_t *surface,
                          const space_invaders_state_t *state)
{
    hi_fill_rect(surface, 0, 150, 320, 50, 0x0844U);
    hi_fill_rect(surface, 0, 150, 320, 1, 0x21cbU);
    const uint16_t idle = 0x19abU, active = 0x34b5U;
    hi_fill_rect(surface, 8, 158, 26, 25, (state->held_buttons & P4_BUTTON_LEFT) ? active : idle);
    hi_fill_rect(surface, 58, 158, 26, 25, (state->held_buttons & P4_BUTTON_RIGHT) ? active : idle);
    hi_rect(surface, 8, 158, 26, 25, 0x5cf7U);
    hi_rect(surface, 58, 158, 26, 25, 0x5cf7U);
    hi_text(surface, 17, 165, "<", 0xdfffU, 1U, 1U);
    hi_text(surface, 67, 165, ">", 0xdfffU, 1U, 1U);
    hi_text(surface, 20, 189, "MOVE", 0x8c96U, 1U, 4U);
    hi_circle(surface, 286, 158, 22, (state->held_buttons & P4_BUTTON_A) ? active : idle);
    hi_circle(surface, 240, 176, 18, (state->held_buttons & P4_BUTTON_B) ? active : idle);
    hi_text(surface, 278, 155, "A", 0xafffU, 1U, 1U);
    hi_text(surface, 236, 173, "B", 0xafffU, 1U, 1U);
    hi_text(surface, 122, 169, "DEFEND THE LINE", 0x9d59U, 1U, 15U);
    hi_text(surface, 137, 184, "A / B : FIRE", 0x6bd0U, 1U, 12U);
    hi_fill_rect(surface, 0, 0, 52, 24, idle);
    hi_fill_rect(surface, 268, 0, 52, 24, idle);
    hi_text(surface, 12, 8, "EXIT", 0xbfffU, 1U, 4U);
    hi_text(surface, 276, 8, state->paused ? "PLAY" : "PAUSE", 0xbfffU, 1U, 5U);
}

static void draw_hud(p4_game_surface_t *surface,
                     const space_invaders_state_t *state)
{
    char value[11];
    hi_fill_rect(surface, 52, 0, 216, 24, 0x0844U);
    hi_text(surface, 111, 7, "SPACE INVADERS",
                 UINT16_C(0x07ff), 1U, 14U);
    hi_text(surface, 58, 7, "S", UINT16_C(0x9cf3), 1U, 1U);
    u32_text(state->score, value);
    hi_text(surface, 66, 7, value, UINT16_C(0xffff), 1U, 7U);
    hi_text(surface, 204, 7, "W", UINT16_C(0x9cf3), 1U, 1U);
    u32_text(state->wave, value);
    hi_text(surface, 212, 7, value, UINT16_C(0xffff), 1U, 3U);
    hi_text(surface, 232, 7, "L", UINT16_C(0x9cf3), 1U, 1U);
    u32_text(state->lives, value);
    hi_text(surface, 240, 7, value, UINT16_C(0xffff), 1U, 2U);
}

/* Title composition reuses the original native ship atlas and nebula. Text
 * stays exact; no extra full-screen raster, pixel blend pass or heap cache. */
static void title_text(p4_game_surface_t *surface, int x, int y,
                       const char *text, uint16_t color, unsigned height)
{
    unsigned cell=surface->width>320U?height:(height*5U+6U)/12U;
    if(cell<9U)cell=9U;
    p4_ui_text(surface,p4_ui_x(surface,x),p4_ui_y(surface,y),text,color,cell,40U);
}

static void draw_title(p4_game_surface_t *surface,
                       const space_invaders_state_t *state)
{
    draw_starfield(surface,state);
    hi_fill_rect(surface,0,0,320,26,0x0844U);
    hi_fill_rect(surface,0,150,320,50,0x0844U);
    hi_fill_rect(surface,0,25,320,1,0x2b71U);
    hi_fill_rect(surface,0,150,320,1,0x2b71U);
    hi_rect(surface,4,3,44,19,0x5cf7U);
    title_text(surface,13,5,"EXIT",0xcfffU,22U);
    title_text(surface,191,7,"ORBITAL DEFENSE",0x9d59U,18U);

    p4_ui_round_rect(surface,p4_ui_x(surface,16),p4_ui_y(surface,37),
        p4_ui_x(surface,168),p4_ui_y(surface,101),p4_ui_x(surface,4),0x0844U);
    hi_fill_rect(surface,23,44,26,2,0x5cf7U);
    title_text(surface,24,52,"SPACE",0xf7deU,49U);
    title_text(surface,24,73,"INVADERS",0x6f5fU,42U);
    title_text(surface,24,108,"HOLD THE LINE.",0xdfffU,22U);
    title_text(surface,24,123,"BREAK THE ALIEN WAVES.",0x9d59U,17U);

    const unsigned frame = (state->visual_clock_ms / 120U) & 3U;
    hi_frame(surface,194,40,38,21,frame);
    hi_frame(surface,254,60,38,21,4U+frame);
    hi_fill_rect(surface,221,94,1,10,0x7dfbU);
    hi_fill_rect(surface,255,103,1,8,0x7dfbU);
    hi_frame(surface,207,107,66,36,8U+frame);

    title_text(surface,20,157,"LEFT / RIGHT TO MOVE",0xbfffU,20U);
    title_text(surface,20,174,"HOLD A OR B TO FIRE",0x9d59U,20U);
    p4_ui_round_rect(surface,p4_ui_x(surface,191),p4_ui_y(surface,153),
        p4_ui_x(surface,106),p4_ui_y(surface,30),p4_ui_x(surface,4),0x5cf7U);
    const char *label="PLAY";
    const unsigned height=surface->width>320U?29U:12U;
    const int width=p4_ui_text_width(label,height,4U);
    p4_ui_text(surface,p4_ui_x(surface,244)-width/2,p4_ui_y(surface,160),
        label,0x0844U,height,4U);
    title_text(surface,209,187,"A / START",0x9d59U,17U);
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const space_invaders_state_t *const state = context->state;
    if (state->intro) {
        draw_title(surface,state);
        return true;
    }
    draw_starfield(surface, state);
    hi_fill_rect(surface, 16, 145, 288, 1, 0x4495U);
    draw_hud(surface, state);
    draw_controls(surface, state);
    for (unsigned row = 0U; row < SPACE_INVADER_ROWS; ++row) {
        for (unsigned column = 0U; column < SPACE_INVADER_COLUMNS; ++column) {
            if (alien_alive(state, row, column)) {
                draw_alien(surface, state, row, column);
            }
        }
    }
    draw_shields(surface, state);
    draw_projectiles(surface, state);
    draw_player(surface, state);
    if (state->paused || state->game_over || state->wave_delay_ms != 0U) {
        hi_fill_rect(surface, 94, 67, 138, 52, 0x0001U);
        p4_ui_round_rect(surface, p4_ui_x(surface, 91), p4_ui_y(surface, 64),
            p4_ui_x(surface, 138), p4_ui_y(surface, 52), p4_ui_x(surface, 4), 0x0844U);
        hi_fill_rect(surface, 95, 64, 130, 1, 0x7dfbU);
        hi_fill_rect(surface, 95, 115, 130, 1, 0x21cbU);
        hi_fill_rect(surface, 91, 68, 1, 44, 0x4cf5U);
        hi_fill_rect(surface, 228, 68, 1, 44, 0x4cf5U);
        hi_fill_rect(surface, 103, 89, 114, 1, 0x194aU);
        const char *const message = state->game_over ? "GAME OVER" :
            (state->paused ? "PAUSED" : "WAVE CLEAR");
        hi_text(surface,
                     state->paused ? 139 :
                     (state->game_over ? 133 : 127),
                     76, message, UINT16_C(0xbfff), 1U, 10U);
        hi_text(surface, state->game_over ? 116 : 122, 98,
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
    .title = "Space Invaders",
    .subtitle = "Defend against alien waves",
    .accent_rgb565 = UINT16_C(0x07ff),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM |
                             P4_GAME_CAP_VIDEO_HIGH_RES,
    .state_bytes = sizeof(space_invaders_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
