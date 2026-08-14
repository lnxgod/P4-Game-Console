// SPDX-License-Identifier: MIT
/*
 * Original clean-room maze-chase sample for P4 Game API v1. It uses no arcade
 * ROM, map, sprite, sound, font, or other third-party game asset.
 */

#include "p4_games/maze_chase.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "maze_chase_internal.h"
#include "p4/draw.h"
#include "p4/input.h"

enum {
    MAZE_TILE_SIZE = 8,
    MAZE_ORIGIN_X = 84,
    MAZE_ORIGIN_Y = 26,
    PLAYER_MOVE_INTERVAL_MS = 112,
    ENEMY_MOVE_INTERVAL_MS = 176,
    FRIGHTENED_ENEMY_INTERVAL_MS = 240,
    FRIGHTENED_DURATION_MS = 6000,
};

static const char s_maze[MAZE_CHASE_HEIGHT][MAZE_CHASE_WIDTH + 1] = {
    "###################",
    "#........#........#",
    "#.##.##.....##.##.#",
    "#....##..#..##....#",
    "###.####.#.####.###",
    "#.......###.......#",
    "#.##.#.......#.##.#",
    "#....#.##.##.#....#",
    "#.##.#.......#.##.#",
    "#.......###.......#",
    "###.####.#.####.###",
    "#....##..#..##....#",
    "###################",
};

_Static_assert(sizeof(s_maze[0]) == MAZE_CHASE_WIDTH + 1U,
               "maze row width changed");

static const uint16_t s_enemy_colors[MAZE_CHASE_ENEMY_COUNT] = {
    UINT16_C(0xf904), UINT16_C(0xf81f), UINT16_C(0xfd20),
};

bool maze_chase_cell_open(int x, int y)
{
    return x >= 0 && x < MAZE_CHASE_WIDTH &&
        y >= 0 && y < MAZE_CHASE_HEIGHT && s_maze[y][x] != '#';
}

static void clear_pellet(maze_chase_state_t *state, unsigned x, unsigned y)
{
    if (state->pellets[y][x] != 0U) {
        state->pellets[y][x] = 0U;
        if (state->pellets_remaining != 0U) {
            --state->pellets_remaining;
        }
    }
}

static void reset_positions(maze_chase_state_t *state)
{
    state->player_x = 9U;
    state->player_y = 8U;
    state->player_direction = MAZE_DIRECTION_LEFT;
    state->desired_direction = MAZE_DIRECTION_LEFT;
    state->enemies[0] = (maze_enemy_t){
        .x = 7U, .y = 6U, .direction = MAZE_DIRECTION_LEFT,
        .color_index = 0U,
    };
    state->enemies[1] = (maze_enemy_t){
        .x = 9U, .y = 6U, .direction = MAZE_DIRECTION_UP,
        .color_index = 1U,
    };
    state->enemies[2] = (maze_enemy_t){
        .x = 11U, .y = 6U, .direction = MAZE_DIRECTION_RIGHT,
        .color_index = 2U,
    };
    state->player_move_accumulator_ms = 0U;
    state->enemy_move_accumulator_ms = 0U;
}

void maze_chase_reset(maze_chase_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->lives = 3U;
    for (unsigned y = 0U; y < MAZE_CHASE_HEIGHT; ++y) {
        for (unsigned x = 0U; x < MAZE_CHASE_WIDTH; ++x) {
            if (maze_chase_cell_open((int)x, (int)y)) {
                state->pellets[y][x] = 1U;
                ++state->pellets_remaining;
            }
        }
    }
    const unsigned power[][2] = {
        {1U, 1U}, {17U, 1U}, {1U, 11U}, {17U, 11U},
    };
    for (size_t i = 0U; i < sizeof(power) / sizeof(power[0]); ++i) {
        state->pellets[power[i][1]][power[i][0]] = 2U;
    }
    reset_positions(state);
    clear_pellet(state, state->player_x, state->player_y);
    for (size_t i = 0U; i < MAZE_CHASE_ENEMY_COUNT; ++i) {
        clear_pellet(state, state->enemies[i].x, state->enemies[i].y);
    }
}

static void direction_delta(maze_direction_t direction, int *dx, int *dy)
{
    *dx = 0;
    *dy = 0;
    switch (direction) {
    case MAZE_DIRECTION_UP: *dy = -1; break;
    case MAZE_DIRECTION_DOWN: *dy = 1; break;
    case MAZE_DIRECTION_LEFT: *dx = -1; break;
    case MAZE_DIRECTION_RIGHT: *dx = 1; break;
    case MAZE_DIRECTION_NONE:
    default: break;
    }
}

static maze_direction_t reverse_direction(maze_direction_t direction)
{
    switch (direction) {
    case MAZE_DIRECTION_UP: return MAZE_DIRECTION_DOWN;
    case MAZE_DIRECTION_DOWN: return MAZE_DIRECTION_UP;
    case MAZE_DIRECTION_LEFT: return MAZE_DIRECTION_RIGHT;
    case MAZE_DIRECTION_RIGHT: return MAZE_DIRECTION_LEFT;
    case MAZE_DIRECTION_NONE:
    default: return MAZE_DIRECTION_NONE;
    }
}

static bool can_move(unsigned x, unsigned y, maze_direction_t direction)
{
    int dx = 0;
    int dy = 0;
    direction_delta(direction, &dx, &dy);
    return direction != MAZE_DIRECTION_NONE &&
        maze_chase_cell_open((int)x + dx, (int)y + dy);
}

static void move_position(uint8_t *x, uint8_t *y,
                          maze_direction_t direction)
{
    int dx = 0;
    int dy = 0;
    direction_delta(direction, &dx, &dy);
    *x = (uint8_t)((int)*x + dx);
    *y = (uint8_t)((int)*y + dy);
}

static maze_direction_t requested_direction(uint32_t buttons)
{
    if ((buttons & P4_BUTTON_UP) != 0U) {
        return MAZE_DIRECTION_UP;
    }
    if ((buttons & P4_BUTTON_DOWN) != 0U) {
        return MAZE_DIRECTION_DOWN;
    }
    if ((buttons & P4_BUTTON_LEFT) != 0U) {
        return MAZE_DIRECTION_LEFT;
    }
    if ((buttons & P4_BUTTON_RIGHT) != 0U) {
        return MAZE_DIRECTION_RIGHT;
    }
    return MAZE_DIRECTION_NONE;
}

static void play_tone(p4_game_context_t *context,
                      uint16_t frequency, uint16_t duration,
                      uint8_t volume, p4_waveform_t waveform)
{
    (void)p4_game_play_tone(
        context, frequency, duration, volume, waveform);
}

static void collect_pellet(p4_game_context_t *context,
                           maze_chase_state_t *state)
{
    const uint8_t pellet = state->pellets[state->player_y][state->player_x];
    if (pellet == 0U) {
        return;
    }
    state->pellets[state->player_y][state->player_x] = 0U;
    if (state->pellets_remaining != 0U) {
        --state->pellets_remaining;
    }
    if (pellet == 2U) {
        state->score += 50U;
        state->frightened_ms = FRIGHTENED_DURATION_MS;
        play_tone(context, 392U, 100U, 5U, P4_WAVE_TRIANGLE);
    } else {
        state->score += 10U;
        const uint16_t note = (state->score & UINT32_C(0x10)) != 0U
            ? 784U : 988U;
        play_tone(context, note, 24U, 3U, P4_WAVE_SQUARE);
    }
    if (state->pellets_remaining == 0U) {
        state->won = true;
        play_tone(context, 1047U, 500U, 6U, P4_WAVE_TRIANGLE);
    }
}

static int absolute_difference(int left, int right)
{
    const int difference = left - right;
    return difference < 0 ? -difference : difference;
}

static maze_direction_t choose_enemy_direction(
    const maze_chase_state_t *state,
    const maze_enemy_t *enemy,
    size_t enemy_index)
{
    static const maze_direction_t directions[] = {
        MAZE_DIRECTION_UP, MAZE_DIRECTION_LEFT,
        MAZE_DIRECTION_DOWN, MAZE_DIRECTION_RIGHT,
    };
    maze_direction_t candidates[4];
    size_t candidate_count = 0U;
    for (size_t offset = 0U; offset < 4U; ++offset) {
        const size_t index = (offset + enemy_index + state->enemy_step) % 4U;
        const maze_direction_t direction = directions[index];
        if (can_move(enemy->x, enemy->y, direction)) {
            candidates[candidate_count++] = direction;
        }
    }
    if (candidate_count == 0U) {
        return MAZE_DIRECTION_NONE;
    }
    const maze_direction_t reverse = reverse_direction(enemy->direction);
    maze_direction_t best = candidates[0];
    int best_distance = state->frightened_ms == 0U ? INT_MAX : INT_MIN;
    for (size_t i = 0U; i < candidate_count; ++i) {
        const maze_direction_t direction = candidates[i];
        if (candidate_count > 1U && direction == reverse) {
            continue;
        }
        int dx = 0;
        int dy = 0;
        direction_delta(direction, &dx, &dy);
        const int distance =
            absolute_difference((int)enemy->x + dx, state->player_x) +
            absolute_difference((int)enemy->y + dy, state->player_y);
        const bool improve = state->frightened_ms == 0U
            ? distance < best_distance : distance > best_distance;
        if (improve) {
            best = direction;
            best_distance = distance;
        }
    }
    return best;
}

static void reset_enemy(maze_enemy_t *enemy, size_t index)
{
    static const uint8_t starts[MAZE_CHASE_ENEMY_COUNT][2] = {
        {7U, 6U}, {9U, 6U}, {11U, 6U},
    };
    enemy->x = starts[index][0];
    enemy->y = starts[index][1];
    enemy->direction = index == 0U ? MAZE_DIRECTION_LEFT :
        (index == 1U ? MAZE_DIRECTION_UP : MAZE_DIRECTION_RIGHT);
}

static void resolve_collisions(p4_game_context_t *context,
                               maze_chase_state_t *state)
{
    for (size_t i = 0U; i < MAZE_CHASE_ENEMY_COUNT; ++i) {
        maze_enemy_t *const enemy = &state->enemies[i];
        if (enemy->x != state->player_x || enemy->y != state->player_y) {
            continue;
        }
        if (state->frightened_ms != 0U) {
            state->score += 200U;
            reset_enemy(enemy, i);
            play_tone(context, 1319U, 140U, 5U, P4_WAVE_TRIANGLE);
            continue;
        }
        if (state->lives != 0U) {
            --state->lives;
        }
        play_tone(context, 110U, 500U, 6U, P4_WAVE_TRIANGLE);
        if (state->lives == 0U) {
            state->game_over = true;
        } else {
            reset_positions(state);
        }
        return;
    }
}

static void add_bounded(uint32_t *value, uint32_t amount)
{
    if (UINT32_MAX - *value < amount) {
        *value = UINT32_MAX;
    } else {
        *value += amount;
    }
}

static bool maze_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(maze_chase_state_t)) {
        return false;
    }
    maze_chase_reset(context->state);
    play_tone(context, 523U, 100U, 4U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t maze_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    maze_chase_state_t *const state = context->state;
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    if (state->game_over || state->won) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            maze_chase_reset(state);
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

    const maze_direction_t requested = requested_direction(input->held);
    if (requested != MAZE_DIRECTION_NONE) {
        state->desired_direction = requested;
    }
    if (state->frightened_ms > elapsed_ms) {
        state->frightened_ms -= elapsed_ms;
    } else {
        state->frightened_ms = 0U;
    }
    add_bounded(&state->player_move_accumulator_ms, elapsed_ms);
    add_bounded(&state->enemy_move_accumulator_ms, elapsed_ms);

    while (state->player_move_accumulator_ms >= PLAYER_MOVE_INTERVAL_MS) {
        state->player_move_accumulator_ms -= PLAYER_MOVE_INTERVAL_MS;
        if (can_move(state->player_x, state->player_y,
                     state->desired_direction)) {
            state->player_direction = state->desired_direction;
        }
        if (can_move(state->player_x, state->player_y,
                     state->player_direction)) {
            move_position(&state->player_x, &state->player_y,
                          state->player_direction);
            collect_pellet(context, state);
            resolve_collisions(context, state);
        }
        if (state->game_over || state->won) {
            return P4_GAME_CONTINUE;
        }
    }

    const uint32_t enemy_interval = state->frightened_ms == 0U
        ? ENEMY_MOVE_INTERVAL_MS : FRIGHTENED_ENEMY_INTERVAL_MS;
    while (state->enemy_move_accumulator_ms >= enemy_interval) {
        state->enemy_move_accumulator_ms -= enemy_interval;
        for (size_t i = 0U; i < MAZE_CHASE_ENEMY_COUNT; ++i) {
            maze_enemy_t *const enemy = &state->enemies[i];
            enemy->direction = choose_enemy_direction(state, enemy, i);
            if (can_move(enemy->x, enemy->y, enemy->direction)) {
                move_position(&enemy->x, &enemy->y, enemy->direction);
            }
        }
        if (state->enemy_step != UINT32_MAX) {
            ++state->enemy_step;
        }
        resolve_collisions(context, state);
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

static void draw_status(const maze_chase_state_t *state,
                        p4_game_surface_t *surface)
{
    char value[11];
    p4_draw_text(surface, 4, 34, "SCORE", UINT16_C(0x9cf3), 1U, 5U);
    u32_text(state->score, value);
    p4_draw_text(surface, 4, 46, value, UINT16_C(0xffff), 1U, 10U);
    p4_draw_text(surface, 244, 34, "LIVES", UINT16_C(0x9cf3), 1U, 5U);
    u32_text(state->lives, value);
    p4_draw_text(surface, 244, 46, value, UINT16_C(0xffff), 1U, 2U);
    p4_draw_text(surface, 244, 66, "LEFT", UINT16_C(0x9cf3), 1U, 4U);
    u32_text(state->pellets_remaining, value);
    p4_draw_text(surface, 244, 78, value, UINT16_C(0xffff), 1U, 4U);
}

static void draw_enemy(p4_game_surface_t *surface,
                       const maze_enemy_t *enemy,
                       bool frightened)
{
    const int center_x = MAZE_ORIGIN_X + (int)enemy->x * MAZE_TILE_SIZE + 4;
    const int center_y = MAZE_ORIGIN_Y + (int)enemy->y * MAZE_TILE_SIZE + 4;
    const uint16_t color = frightened
        ? UINT16_C(0x029f) : s_enemy_colors[enemy->color_index];
    p4_draw_fill_circle(surface, center_x, center_y - 1, 4, color);
    p4_draw_fill_rect(surface, center_x - 4, center_y - 1, 9, 5, color);
    p4_draw_pixel(surface, center_x - 2, center_y - 2, UINT16_C(0xffff));
    p4_draw_pixel(surface, center_x + 2, center_y - 2, UINT16_C(0xffff));
}

static void draw_player(p4_game_surface_t *surface,
                        const maze_chase_state_t *state)
{
    const int center_x = MAZE_ORIGIN_X +
        (int)state->player_x * MAZE_TILE_SIZE + 4;
    const int center_y = MAZE_ORIGIN_Y +
        (int)state->player_y * MAZE_TILE_SIZE + 4;
    p4_draw_fill_circle(surface, center_x, center_y, 4, UINT16_C(0xffe0));
    int mouth_x = center_x + 2;
    int mouth_y = center_y - 1;
    int mouth_width = 3;
    int mouth_height = 3;
    if (state->player_direction == MAZE_DIRECTION_LEFT) {
        mouth_x = center_x - 4;
    } else if (state->player_direction == MAZE_DIRECTION_UP) {
        mouth_x = center_x - 1;
        mouth_y = center_y - 4;
        mouth_width = 3;
    } else if (state->player_direction == MAZE_DIRECTION_DOWN) {
        mouth_x = center_x - 1;
        mouth_y = center_y + 2;
        mouth_width = 3;
    }
    p4_draw_fill_rect(surface, mouth_x, mouth_y,
                      mouth_width, mouth_height, UINT16_C(0x0000));
}

static bool maze_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    const maze_chase_state_t *const state = context->state;
    if (!p4_surface_valid(surface)) {
        return false;
    }
    p4_draw_clear(surface, UINT16_C(0x0000));
    p4_draw_text(surface, 130, 8, "MAZE CHASE",
                 UINT16_C(0xffe0), 1U, 10U);
    draw_status(state, surface);

    for (unsigned y = 0U; y < MAZE_CHASE_HEIGHT; ++y) {
        for (unsigned x = 0U; x < MAZE_CHASE_WIDTH; ++x) {
            const int left = MAZE_ORIGIN_X + (int)x * MAZE_TILE_SIZE;
            const int top = MAZE_ORIGIN_Y + (int)y * MAZE_TILE_SIZE;
            if (s_maze[y][x] == '#') {
                p4_draw_fill_rect(surface, left, top,
                                  MAZE_TILE_SIZE, MAZE_TILE_SIZE,
                                  UINT16_C(0x0010));
                p4_draw_rect(surface, left + 1, top + 1,
                             MAZE_TILE_SIZE - 2, MAZE_TILE_SIZE - 2,
                             UINT16_C(0x045f));
            } else if (state->pellets[y][x] != 0U) {
                p4_draw_fill_circle(
                    surface, left + 4, top + 4,
                    state->pellets[y][x] == 2U ? 3 : 1,
                    UINT16_C(0xffdf));
            }
        }
    }
    draw_player(surface, state);
    for (size_t i = 0U; i < MAZE_CHASE_ENEMY_COUNT; ++i) {
        draw_enemy(surface, &state->enemies[i], state->frightened_ms != 0U);
    }
    p4_game_draw_standard_controls(
        surface, UINT16_C(0x4208), UINT16_C(0x07ff), state->held_buttons);

    if (state->paused || state->game_over || state->won) {
        p4_draw_fill_rect(surface, 102, 67, 116, 40, UINT16_C(0x1082));
        p4_draw_rect(surface, 102, 67, 116, 40, UINT16_C(0xffff));
        const char *const message = state->game_over ? "GAME OVER" :
            (state->won ? "MAZE CLEAR" : "PAUSED");
        p4_draw_text(surface,
                     state->paused ? 139 : 130, 77,
                     message, UINT16_C(0xffff), 1U, 10U);
        p4_draw_text(surface, 119, 93,
                     state->paused ? "START TO RESUME" : "A TO RESTART",
                     UINT16_C(0x9cf3), 1U, 15U);
    }
    return true;
}

static void maze_stop(p4_game_context_t *context)
{
    (void)context;
}

const p4_game_descriptor_t p4_maze_chase_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = 100U,
    .id = "org.p4console.maze-chase",
    .title = "MAZE CHASE",
    .subtitle = "P4 GAME API V1",
    .accent_rgb565 = UINT16_C(0xffe0),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE,
    .state_bytes = sizeof(maze_chase_state_t),
    .start = maze_start,
    .update = maze_update,
    .render = maze_render,
    .stop = maze_stop,
};
