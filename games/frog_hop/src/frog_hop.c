// SPDX-License-Identifier: MIT
/* Original frog-crossing game using the P4 Game API v1 and original art. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"
#include "generated/frog_hop_animation_atlas.inc"

enum {
    HUD_HEIGHT = 18,
    GRID_TOP = HUD_HEIGHT,
    GRID_ROWS = 10,
    ROW_HEIGHT = 15,
    START_ROW = GRID_ROWS - 1,
    WATER_FIRST_ROW = 1,
    WATER_ROW_COUNT = 4,
    SAFE_ROW = 5,
    ROAD_FIRST_ROW = 6,
    ROAD_ROW_COUNT = 3,
    GRID_STEP_X = 20,
    FROG_MIN_X = 20,
    FROG_MAX_X = 300,
    /* Keep the moving lanes readable at level 1 (100 px/s maximum). */
    WORLD_STEP_MS = 20,
    ATLAS_WIDTH = 160,
    FRAME_WIDTH = 40,
    FRAME_HEIGHT = 25,
    FROG_ROW = 0,
    CAR_ROW = 1,
    LOG_ROW = 2,
    PAD_ROW = 3,
    GOAL_COUNT = 5,
    HOME_MASK = (1U << GOAL_COUNT) - 1U,
};

static const uint16_t COLOR_NAVY = UINT16_C(0x0011);
static const uint16_t COLOR_SKY = UINT16_C(0x4e7f);
static const uint16_t COLOR_GRASS = UINT16_C(0x4d47);
static const uint16_t COLOR_GRASS_LIGHT = UINT16_C(0x7e0d);
static const uint16_t COLOR_WATER = UINT16_C(0x04bf);
static const uint16_t COLOR_WATER_LIGHT = UINT16_C(0x3eff);
static const uint16_t COLOR_ROAD = UINT16_C(0x2104);
static const uint16_t COLOR_LANE = UINT16_C(0xffe0);
static const uint16_t COLOR_WHITE = UINT16_C(0xffff);
static const uint16_t COLOR_TEXT = UINT16_C(0xbdf7);
static const int s_goal_x[GOAL_COUNT] = {32, 96, 160, 224, 288};

typedef struct {
    int16_t spacing;
    int8_t speed;
} moving_lane_t;

typedef struct {
    int16_t frog_x;
    int16_t splash_x;
    int16_t road_offsets[ROAD_ROW_COUNT];
    int16_t log_offsets[WATER_ROW_COUNT];
    uint32_t score;
    uint32_t simulation_ms;
    uint32_t held_buttons;
    uint16_t respawn_ms;
    uint16_t splash_ms;
    uint16_t animation_ms;
    uint8_t frog_row;
    uint8_t splash_row;
    uint8_t lives;
    uint8_t level;
    uint8_t homes;
    uint8_t animation_frame;
    bool intro;
    bool paused;
    bool game_over;
} frog_hop_state_t;

static const moving_lane_t s_road_lanes[ROAD_ROW_COUNT] = {
    {.spacing = 112, .speed = 2},
    {.spacing = 132, .speed = -1},
    {.spacing = 96, .speed = 1},
};

static const moving_lane_t s_log_lanes[WATER_ROW_COUNT] = {
    {.spacing = 94, .speed = 1},
    {.spacing = 122, .speed = -1},
    {.spacing = 106, .speed = 1},
    {.spacing = 138, .speed = -1},
};

static int row_top(unsigned row)
{
    return GRID_TOP + (int)row * ROW_HEIGHT;
}

static int wrap_offset(int value, int spacing)
{
    while (value >= 0) {
        value -= spacing;
    }
    while (value < -spacing) {
        value += spacing;
    }
    return value;
}

static uint16_t tick_down(uint16_t value, uint16_t amount)
{
    return value > amount ? (uint16_t)(value - amount) : 0U;
}

static void play_tone(p4_game_context_t *context, uint16_t frequency_hz,
                      uint16_t duration_ms, uint8_t volume,
                      p4_waveform_t waveform)
{
    (void)p4_game_play_tone(context, frequency_hz, duration_ms, volume,
                            waveform);
}

static void reset_frog(frog_hop_state_t *state)
{
    state->frog_x = 160;
    state->frog_row = START_ROW;
}

static void reset_round(frog_hop_state_t *state)
{
    *state = (frog_hop_state_t){
        .frog_x = 160,
        .splash_x = 160,
        .frog_row = START_ROW,
        .splash_row = START_ROW,
        .lives = 3U,
        .level = 1U,
        .intro = true,
        .road_offsets = {-22, -79, -51},
        .log_offsets = {-37, -88, -24, -110},
    };
}

static void draw_frame(p4_game_surface_t *surface, int x, int y,
                       unsigned atlas_row, unsigned frame)
{
    const size_t offset = (size_t)atlas_row * FRAME_HEIGHT * ATLAS_WIDTH +
        (size_t)frame * FRAME_WIDTH;
    p4_draw_sprite_rgb565(surface, x, y,
                          &s_frog_hop_animation_pixels[offset],
                          FRAME_WIDTH, FRAME_HEIGHT, ATLAS_WIDTH,
                          true, UINT16_C(0x0000));
}

static void draw_number(p4_game_surface_t *surface, int x, int y,
                        uint32_t value, uint16_t color)
{
    char text[11];
    size_t position = sizeof(text) - 1U;
    text[position] = '\0';
    do {
        text[--position] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && position != 0U);
    p4_draw_text(surface, x, y, &text[position], color, 1U,
                 sizeof(text) - position);
}

static void draw_water_row(p4_game_surface_t *surface,
                           const frog_hop_state_t *state, unsigned row)
{
    const int top = row_top(row);
    const int phase = (int)state->animation_frame * 7;
    p4_draw_fill_rect(surface, 0, top, P4_GAME_SURFACE_WIDTH, ROW_HEIGHT,
                      COLOR_WATER);
    for (int x = -phase; x < P4_GAME_SURFACE_WIDTH; x += 36) {
        p4_draw_fill_rect(surface, x, top + 3, 15, 2, COLOR_WATER_LIGHT);
        p4_draw_fill_rect(surface, x + 20, top + 10, 9, 1,
                          COLOR_WATER_LIGHT);
    }
}

static void draw_course(p4_game_surface_t *surface,
                        const frog_hop_state_t *state)
{
    p4_draw_clear(surface, COLOR_SKY);
    p4_draw_fill_rect(surface, 0, row_top(0), P4_GAME_SURFACE_WIDTH,
                      ROW_HEIGHT, COLOR_GRASS);
    p4_draw_fill_rect(surface, 0, row_top(0) + ROW_HEIGHT - 2,
                      P4_GAME_SURFACE_WIDTH, 2, COLOR_GRASS_LIGHT);
    for (unsigned row = WATER_FIRST_ROW;
         row < WATER_FIRST_ROW + WATER_ROW_COUNT; ++row) {
        draw_water_row(surface, state, row);
    }
    p4_draw_fill_rect(surface, 0, row_top(SAFE_ROW), P4_GAME_SURFACE_WIDTH,
                      ROW_HEIGHT, COLOR_GRASS_LIGHT);
    for (unsigned row = ROAD_FIRST_ROW;
         row < ROAD_FIRST_ROW + ROAD_ROW_COUNT; ++row) {
        const int top = row_top(row);
        p4_draw_fill_rect(surface, 0, top, P4_GAME_SURFACE_WIDTH, ROW_HEIGHT,
                          COLOR_ROAD);
        p4_draw_fill_rect(surface, 0, top, P4_GAME_SURFACE_WIDTH, 1,
                          COLOR_NAVY);
        for (int x = -10 + (int)state->animation_frame * 4;
             x < P4_GAME_SURFACE_WIDTH; x += 44) {
            p4_draw_fill_rect(surface, x, top + 7, 22, 1, COLOR_LANE);
        }
    }
    p4_draw_fill_rect(surface, 0, row_top(START_ROW), P4_GAME_SURFACE_WIDTH,
                      ROW_HEIGHT, COLOR_GRASS);
}

static void draw_homes(p4_game_surface_t *surface,
                       const frog_hop_state_t *state)
{
    for (unsigned home = 0U; home < GOAL_COUNT; ++home) {
        const int x = s_goal_x[home] - FRAME_WIDTH / 2;
        draw_frame(surface, x, row_top(0) - 5, PAD_ROW,
                   state->animation_frame);
        if ((state->homes & (uint8_t)(UINT8_C(1) << home)) != 0U) {
            draw_frame(surface, x, row_top(0) - 6, FROG_ROW, 0U);
        }
    }
}

static void draw_traffic(p4_game_surface_t *surface,
                         const frog_hop_state_t *state)
{
    for (unsigned lane = 0U; lane < ROAD_ROW_COUNT; ++lane) {
        const int top = row_top(ROAD_FIRST_ROW + lane) - 5;
        const int spacing = s_road_lanes[lane].spacing;
        for (int item = 0; item < 5; ++item) {
            const int x = state->road_offsets[lane] + item * spacing;
            draw_frame(surface, x, top, CAR_ROW,
                       (state->animation_frame + lane + (unsigned)item) & 3U);
        }
    }
}

static void draw_logs(p4_game_surface_t *surface,
                      const frog_hop_state_t *state)
{
    for (unsigned lane = 0U; lane < WATER_ROW_COUNT; ++lane) {
        const int top = row_top(WATER_FIRST_ROW + lane) - 5;
        const int spacing = s_log_lanes[lane].spacing;
        for (int item = 0; item < 5; ++item) {
            const int x = state->log_offsets[lane] + item * spacing;
            draw_frame(surface, x, top, LOG_ROW,
                       (state->animation_frame + lane + (unsigned)item) & 3U);
        }
    }
}

static void draw_hud(p4_game_surface_t *surface,
                     const frog_hop_state_t *state)
{
    p4_draw_fill_rect(surface, 0, 0, P4_GAME_SURFACE_WIDTH, HUD_HEIGHT,
                      COLOR_NAVY);
    p4_draw_text(surface, 6, 5, "S", COLOR_TEXT, 1U, 1U);
    draw_number(surface, 14, 5, state->score, COLOR_WHITE);
    p4_draw_text(surface, 116, 5, "L", COLOR_TEXT, 1U, 1U);
    draw_number(surface, 124, 5, state->lives, COLOR_WHITE);
    p4_draw_text(surface, 196, 5, "LV", COLOR_TEXT, 1U, 2U);
    draw_number(surface, 212, 5, state->level, COLOR_WHITE);
    p4_draw_text(surface, 265, 5, "H", COLOR_TEXT, 1U, 1U);
    draw_number(surface, 273, 5,
                (uint32_t)((state->homes & UINT8_C(1)) != 0U) +
                    (uint32_t)((state->homes & UINT8_C(2)) != 0U) +
                    (uint32_t)((state->homes & UINT8_C(4)) != 0U) +
                    (uint32_t)((state->homes & UINT8_C(8)) != 0U) +
                    (uint32_t)((state->homes & UINT8_C(16)) != 0U),
                COLOR_WHITE);
}

static void draw_title(p4_game_surface_t *surface,
                       const frog_hop_state_t *state)
{
    p4_draw_clear(surface, COLOR_NAVY);
    p4_draw_fill_rect(surface, 0, 66, P4_GAME_SURFACE_WIDTH, 34,
                      COLOR_WATER);
    p4_draw_fill_rect(surface, 0, 100, P4_GAME_SURFACE_WIDTH, 18,
                      COLOR_GRASS_LIGHT);
    p4_draw_fill_rect(surface, 0, 118, P4_GAME_SURFACE_WIDTH, 20,
                      COLOR_ROAD);
    p4_draw_text(surface, 105, 12, "FROG HOP", COLOR_WHITE, 2U, 8U);
    p4_draw_text(surface, 84, 35, "8-BIT RIVER RUN", COLOR_TEXT, 1U, 14U);
    draw_frame(surface, 139, 76, FROG_ROW, state->animation_frame);
    draw_frame(surface, 35, 60, LOG_ROW, state->animation_frame);
    draw_frame(surface, 242, 113, CAR_ROW, state->animation_frame);
    draw_frame(surface, 143, 130, PAD_ROW, state->animation_frame);
    p4_draw_text(surface, 76, 159, "A OR START TO PLAY", COLOR_LANE, 1U, 18U);
    p4_game_draw_standard_controls(surface, COLOR_TEXT, UINT16_C(0x6e4f),
                                   state->held_buttons);
}

static void advance_lanes(frog_hop_state_t *state)
{
    const int multiplier = (int)state->level;
    for (unsigned lane = 0U; lane < ROAD_ROW_COUNT; ++lane) {
        const int speed = (int)s_road_lanes[lane].speed * multiplier;
        state->road_offsets[lane] = (int16_t)wrap_offset(
            (int)state->road_offsets[lane] + speed,
            (int)s_road_lanes[lane].spacing);
    }
    for (unsigned lane = 0U; lane < WATER_ROW_COUNT; ++lane) {
        const int speed = (int)s_log_lanes[lane].speed * multiplier;
        state->log_offsets[lane] = (int16_t)wrap_offset(
            (int)state->log_offsets[lane] + speed,
            (int)s_log_lanes[lane].spacing);
    }
}

static bool frog_on_log(const frog_hop_state_t *state, int *out_speed)
{
    if (state->frog_row < WATER_FIRST_ROW ||
        state->frog_row >= WATER_FIRST_ROW + WATER_ROW_COUNT ||
        out_speed == NULL) {
        return false;
    }
    const unsigned lane = state->frog_row - WATER_FIRST_ROW;
    const int spacing = s_log_lanes[lane].spacing;
    for (int item = 0; item < 5; ++item) {
        const int x = state->log_offsets[lane] + item * spacing;
        if (state->frog_x >= x + 5 &&
            state->frog_x <= x + FRAME_WIDTH - 5) {
            *out_speed = (int)s_log_lanes[lane].speed * (int)state->level;
            return true;
        }
    }
    return false;
}

static bool frog_hit_car(const frog_hop_state_t *state)
{
    if (state->frog_row < ROAD_FIRST_ROW ||
        state->frog_row >= ROAD_FIRST_ROW + ROAD_ROW_COUNT) {
        return false;
    }
    const unsigned lane = state->frog_row - ROAD_FIRST_ROW;
    const int spacing = s_road_lanes[lane].spacing;
    for (int item = 0; item < 5; ++item) {
        const int x = state->road_offsets[lane] + item * spacing;
        if (state->frog_x >= x + 5 &&
            state->frog_x <= x + FRAME_WIDTH - 5) {
            return true;
        }
    }
    return false;
}

static void lose_life(p4_game_context_t *context, frog_hop_state_t *state)
{
    if (state->respawn_ms != 0U || state->game_over) {
        return;
    }
    state->splash_x = state->frog_x;
    state->splash_row = state->frog_row;
    state->splash_ms = 450U;
    if (state->lives != 0U) {
        --state->lives;
    }
    reset_frog(state);
    state->respawn_ms = 550U;
    play_tone(context, 110U, 220U, 5U, P4_WAVE_SQUARE);
    if (state->lives == 0U) {
        state->game_over = true;
    }
}

static void reach_home(p4_game_context_t *context, frog_hop_state_t *state)
{
    for (unsigned home = 0U; home < GOAL_COUNT; ++home) {
        int distance = state->frog_x - s_goal_x[home];
        if (distance < 0) {
            distance = -distance;
        }
        if (distance > 15) {
            continue;
        }
        const uint8_t home_bit = (uint8_t)(UINT8_C(1) << home);
        if ((state->homes & home_bit) != 0U) {
            lose_life(context, state);
            return;
        }
        state->homes = (uint8_t)(state->homes | home_bit);
        state->score += 100U + (uint32_t)state->level * 20U;
        play_tone(context, 880U, 90U, 4U, P4_WAVE_TRIANGLE);
        reset_frog(state);
        state->respawn_ms = 350U;
        if (state->homes == HOME_MASK) {
            state->homes = 0U;
            ++state->level;
            state->score += 500U;
            play_tone(context, 1175U, 160U, 5U, P4_WAVE_TRIANGLE);
        }
        return;
    }
    lose_life(context, state);
}

static void update_frog_position(p4_game_context_t *context,
                                 frog_hop_state_t *state)
{
    if (state->frog_row == 0U) {
        reach_home(context, state);
        return;
    }
    int log_speed = 0;
    if (state->frog_row >= WATER_FIRST_ROW &&
        state->frog_row < WATER_FIRST_ROW + WATER_ROW_COUNT) {
        if (!frog_on_log(state, &log_speed)) {
            lose_life(context, state);
            return;
        }
        state->frog_x = (int16_t)((int)state->frog_x + log_speed);
        if (state->frog_x < -8 || state->frog_x > 328) {
            lose_life(context, state);
        }
        return;
    }
    if (frog_hit_car(state)) {
        lose_life(context, state);
    }
}

static void move_frog(p4_game_context_t *context, frog_hop_state_t *state,
                      uint32_t pressed)
{
    bool moved = false;
    if ((pressed & P4_BUTTON_UP) != 0U && state->frog_row != 0U) {
        --state->frog_row;
        state->score += 10U;
        moved = true;
    } else if ((pressed & P4_BUTTON_DOWN) != 0U &&
               state->frog_row < START_ROW) {
        ++state->frog_row;
        moved = true;
    } else if ((pressed & P4_BUTTON_LEFT) != 0U &&
               state->frog_x > FROG_MIN_X) {
        state->frog_x = (int16_t)(state->frog_x - GRID_STEP_X);
        moved = true;
    } else if ((pressed & P4_BUTTON_RIGHT) != 0U &&
               state->frog_x < FROG_MAX_X) {
        state->frog_x = (int16_t)(state->frog_x + GRID_STEP_X);
        moved = true;
    }
    if (moved) {
        state->animation_frame = 2U;
        play_tone(context, 440U, 45U, 3U, P4_WAVE_TRIANGLE);
    }
}

static void tick_game(p4_game_context_t *context, frog_hop_state_t *state)
{
    advance_lanes(state);
    state->splash_ms = tick_down(state->splash_ms, WORLD_STEP_MS);
    if (state->respawn_ms != 0U) {
        state->respawn_ms = tick_down(state->respawn_ms, WORLD_STEP_MS);
        return;
    }
    update_frog_position(context, state);
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(frog_hop_state_t)) {
        return false;
    }
    frog_hop_state_t *const state = context->state;
    reset_round(state);
    play_tone(context, 523U, 90U, 3U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    if (context == NULL || context->state == NULL || input == NULL) {
        return P4_GAME_ERROR;
    }
    frog_hop_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    state->animation_ms = (uint16_t)(state->animation_ms + elapsed_ms);
    if (state->animation_ms >= 96U) {
        state->animation_ms = 0U;
        state->animation_frame = (uint8_t)((state->animation_frame + 1U) & 3U);
    }
    if (state->intro) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            state->intro = false;
            play_tone(context, 660U, 90U, 4U, P4_WAVE_TRIANGLE);
        }
        return P4_GAME_CONTINUE;
    }
    if (state->game_over) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            reset_round(state);
            state->intro = false;
            play_tone(context, 523U, 90U, 4U, P4_WAVE_TRIANGLE);
        }
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_START) != 0U) {
        state->paused = !state->paused;
        play_tone(context, state->paused ? 330U : 660U, 70U, 3U,
                  P4_WAVE_SQUARE);
        return P4_GAME_CONTINUE;
    }
    if (state->paused || state->respawn_ms != 0U) {
        return P4_GAME_CONTINUE;
    }
    move_frog(context, state, input->pressed);
    state->simulation_ms += elapsed_ms;
    while (state->simulation_ms >= WORLD_STEP_MS) {
        state->simulation_ms -= WORLD_STEP_MS;
        tick_game(context, state);
    }
    return P4_GAME_CONTINUE;
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (context == NULL || context->state == NULL || !p4_surface_valid(surface)) {
        return false;
    }
    const frog_hop_state_t *const state = context->state;
    if (state->intro) {
        draw_title(surface, state);
        return true;
    }
    draw_course(surface, state);
    draw_homes(surface, state);
    draw_logs(surface, state);
    draw_traffic(surface, state);
    if (state->splash_ms != 0U) {
        draw_frame(surface, state->splash_x - FRAME_WIDTH / 2,
                   row_top(state->splash_row) - 5, PAD_ROW, 3U);
    }
    if (state->respawn_ms == 0U ||
        ((state->respawn_ms / 100U) & 1U) == 0U) {
        draw_frame(surface, state->frog_x - FRAME_WIDTH / 2,
                   row_top(state->frog_row) - 5, FROG_ROW,
                   state->animation_frame);
    }
    draw_hud(surface, state);
    if (state->paused) {
        p4_draw_fill_rect(surface, 107, 75, 106, 30, COLOR_NAVY);
        p4_draw_rect(surface, 107, 75, 106, 30, COLOR_WHITE);
        p4_draw_text(surface, 133, 84, "PAUSED", COLOR_WHITE, 1U, 6U);
    }
    if (state->game_over) {
        p4_draw_fill_rect(surface, 90, 69, 140, 45, COLOR_NAVY);
        p4_draw_rect(surface, 90, 69, 140, 45, UINT16_C(0xf945));
        p4_draw_text(surface, 116, 79, "GAME OVER", COLOR_WHITE, 1U, 9U);
        p4_draw_text(surface, 100, 94, "A TO RESTART", COLOR_LANE, 1U, 12U);
    }
    p4_game_draw_standard_controls(surface, COLOR_TEXT, UINT16_C(0x6e4f),
                                   state->held_buttons);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_frog_hop_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(105),
    .id = "org.p4console.frog-hop",
    .title = "FROG HOP",
    .subtitle = "P4 GAME API V1",
    .accent_rgb565 = UINT16_C(0x6e4f),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE,
    .state_bytes = sizeof(frog_hop_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
