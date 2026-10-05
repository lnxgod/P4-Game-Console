// SPDX-License-Identifier: MIT
/* Original four-stage rooftop platform game using P4 Game API v1. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/audio_pack.h"
#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/game.h"
#include "p4/input.h"
#include "generated/skyline_leap_animation_atlas.inc"

enum {
    HUD_HEIGHT = 18,
    PLAY_TOP = HUD_HEIGHT + 2,
    FLOOR_TOP = 126,
    WORLD_LEFT = 4,
    WORLD_RIGHT = P4_GAME_SURFACE_WIDTH - 4,
    PLAYER_WIDTH = 14,
    PLAYER_HEIGHT = 18,
    BOT_WIDTH = 16,
    BOT_HEIGHT = 14,
    PULSE_SIZE = 8,
    PULSE_SPEED = 6,
    MAX_PLATFORMS = 7,
    MAX_BOTS = 3,
    SHARD_COUNT = 3,
    STAGE_COUNT = 4,
    TICK_MS = 16,
    MAX_FALL_SPEED = 7,
    JUMP_SPEED = -9,
    ATLAS_WIDTH = 160,
    FRAME_WIDTH = 40,
    FRAME_HEIGHT = 40,
    COURIER_RUN_ROW = 0,
    COURIER_STATE_ROW = 1,
    BOT_ROW = 2,
    ITEM_ROW = 3,
};

static const uint16_t COLOR_NAVY = UINT16_C(0x0011);
static const uint16_t COLOR_NIGHT = UINT16_C(0x0897);
static const uint16_t COLOR_DAWN = UINT16_C(0x30fb);
static const uint16_t COLOR_SUNSET = UINT16_C(0xd4b0);
static const uint16_t COLOR_SKY = UINT16_C(0x45ff);
static const uint16_t COLOR_TEAL = UINT16_C(0x05dd);
static const uint16_t COLOR_TEAL_LIGHT = UINT16_C(0x7fff);
static const uint16_t COLOR_CORAL = UINT16_C(0xf36f);
static const uint16_t COLOR_GOLD = UINT16_C(0xff20);
static const uint16_t COLOR_CREAM = UINT16_C(0xffde);
static const uint16_t COLOR_STEEL = UINT16_C(0x5ad6);
static const uint16_t COLOR_DARK_STEEL = UINT16_C(0x2104);
static const uint16_t COLOR_WHITE = UINT16_C(0xffff);

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
} platform_t;

typedef struct {
    int16_t x;
    int16_t y;
} point_t;

typedef struct {
    uint8_t platform_index;
    int16_t start_x;
    int8_t direction;
} bot_spawn_t;

typedef struct {
    int16_t player_x;
    int16_t player_y;
    int16_t pulse_x;
    int16_t pulse_y;
    int8_t velocity_y;
    int8_t facing;
    int8_t pulse_direction;
    int16_t bot_x[MAX_BOTS];
    int8_t bot_direction[MAX_BOTS];
    uint32_t simulation_ms;
    uint32_t held_buttons;
    uint32_t score;
    uint16_t transition_ms;
    uint16_t animation_ms;
    uint16_t pulse_cooldown_ms;
    uint8_t level;
    uint8_t lives;
    uint8_t shards;
    uint8_t animation_frame;
    p4_game_audio_effect_player_t audio;
    bool shard_collected[SHARD_COUNT];
    bool bot_active[MAX_BOTS];
    bool pulse_active;
    bool intro;
    bool paused;
    bool clearing;
    bool finale;
    bool game_over;
} skyline_leap_state_t;

static const platform_t s_platforms[STAGE_COUNT][MAX_PLATFORMS] = {
    {
        {4, FLOOR_TOP, 312}, {18, 104, 70}, {114, 92, 70},
        {216, 80, 82}, {108, 62, 62}, {18, 48, 52}, {0, 0, 0},
    },
    {
        {4, FLOOR_TOP, 312}, {202, 106, 96}, {94, 92, 72},
        {18, 76, 56}, {116, 62, 64}, {218, 46, 80}, {0, 0, 0},
    },
    {
        {4, FLOOR_TOP, 312}, {18, 106, 54}, {102, 108, 52},
        {198, 96, 98}, {116, 76, 58}, {28, 58, 70}, {0, 0, 0},
    },
    {
        {4, FLOOR_TOP, 312}, {220, 108, 76}, {124, 94, 62},
        {28, 84, 62}, {110, 64, 74}, {214, 48, 82}, {0, 0, 0},
    },
};

static const uint8_t s_platform_counts[STAGE_COUNT] = {6U, 6U, 6U, 6U};

static const point_t s_shards[STAGE_COUNT][SHARD_COUNT] = {
    {{52, 87}, {148, 75}, {255, 63}},
    {{242, 89}, {132, 75}, {246, 31}},
    {{42, 41}, {142, 59}, {236, 79}},
    {{54, 67}, {148, 45}, {252, 29}},
};

static const bot_spawn_t s_bot_spawns[STAGE_COUNT][MAX_BOTS] = {
    {{0U, 156, 1}, {1U, 46, -1}, {3U, 244, 1}},
    {{0U, 150, -1}, {2U, 126, 1}, {4U, 136, -1}},
    {{0U, 166, 1}, {3U, 234, -1}, {5U, 50, 1}},
    {{0U, 174, -1}, {2U, 142, 1}, {4U, 148, -1}},
};

static const int16_t s_start_x[STAGE_COUNT] = {16, 18, 22, 14};

static int stage_index(const skyline_leap_state_t *state)
{
    return state->level < STAGE_COUNT ? (int)state->level : 0;
}

static void play_tone(p4_game_context_t *context, uint16_t frequency_hz,
                      uint16_t duration_ms, uint8_t volume,
                      p4_waveform_t waveform)
{
    (void)p4_game_play_tone(context, frequency_hz, duration_ms, volume,
                            waveform);
}

static int clamp_int(int value, int lower, int upper)
{
    if (value < lower) {
        return lower;
    }
    return value > upper ? upper : value;
}

static bool ranges_overlap(int first_left, int first_width,
                           int second_left, int second_width)
{
    return first_left < second_left + second_width &&
        second_left < first_left + first_width;
}

static bool rectangles_overlap(int ax, int ay, int aw, int ah,
                               int bx, int by, int bw, int bh)
{
    return ranges_overlap(ax, aw, bx, bw) && ranges_overlap(ay, ah, by, bh);
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

static void draw_atlas_cell(p4_game_surface_t *surface, int x, int y,
                            unsigned row, unsigned frame)
{
    const size_t offset = (size_t)row * FRAME_HEIGHT * ATLAS_WIDTH +
        (size_t)(frame & 3U) * FRAME_WIDTH;
    p4_draw_sprite_rgb565(surface, x, y,
                          &s_skyline_leap_animation_pixels[offset],
                          FRAME_WIDTH, FRAME_HEIGHT, ATLAS_WIDTH,
                          true, UINT16_C(0x0000));
}

static void draw_courier(p4_game_surface_t *surface,
                         const skyline_leap_state_t *state)
{
    const unsigned row = state->game_over ? COURIER_STATE_ROW : COURIER_RUN_ROW;
    const unsigned frame = state->game_over ? 3U : state->animation_frame;
    draw_atlas_cell(surface, state->player_x - 13,
                    state->player_y - (FRAME_HEIGHT - PLAYER_HEIGHT),
                    row, frame);
}

static void draw_background(p4_game_surface_t *surface,
                            const skyline_leap_state_t *state)
{
    const uint16_t skies[STAGE_COUNT] = {
        COLOR_SKY, COLOR_DAWN, COLOR_SUNSET, COLOR_NIGHT,
    };
    const int stage = stage_index(state);
    p4_draw_clear(surface, skies[stage]);
    for (int x = 0; x < P4_GAME_SURFACE_WIDTH; x += 28) {
        const int height = 14 + ((x / 28 + stage * 3) % 4) * 9;
        p4_draw_fill_rect(surface, x, FLOOR_TOP - height, 20, height,
                          COLOR_DARK_STEEL);
        p4_draw_fill_rect(surface, x + 4, FLOOR_TOP - height + 5, 3, 4,
                          COLOR_TEAL_LIGHT);
        p4_draw_fill_rect(surface, x + 12, FLOOR_TOP - height + 13, 3, 4,
                          COLOR_TEAL_LIGHT);
    }
    if (stage == 3) {
        for (int x = 12; x < P4_GAME_SURFACE_WIDTH; x += 39) {
            p4_draw_fill_rect(surface, x, PLAY_TOP + (x % 17), 2, 2,
                              COLOR_CREAM);
        }
    }
}

static void draw_platform(p4_game_surface_t *surface,
                          const platform_t *platform)
{
    p4_draw_fill_rect(surface, platform->x, platform->y,
                      platform->width, 5, COLOR_STEEL);
    p4_draw_fill_rect(surface, platform->x, platform->y, platform->width, 1,
                      COLOR_CREAM);
    p4_draw_fill_rect(surface, platform->x + 3, platform->y + 5,
                      platform->width - 6, 3, COLOR_DARK_STEEL);
}

static void draw_hud(p4_game_surface_t *surface,
                     const skyline_leap_state_t *state)
{
    p4_draw_fill_rect(surface, 0, 0, P4_GAME_SURFACE_WIDTH, HUD_HEIGHT,
                      COLOR_NAVY);
    p4_draw_text(surface, 58, 5, "S", COLOR_TEAL_LIGHT, 1U, 1U);
    draw_number(surface, 66, 5, state->score, COLOR_WHITE);
    p4_draw_text(surface, 126, 5, "L", COLOR_TEAL_LIGHT, 1U, 1U);
    draw_number(surface, 134, 5, state->lives, COLOR_WHITE);
    p4_draw_text(surface, 170, 5, "C", COLOR_TEAL_LIGHT, 1U, 1U);
    draw_number(surface, 178, 5, state->shards, COLOR_GOLD);
    p4_draw_text(surface, 190, 5, "/3", COLOR_CREAM, 1U, 2U);
    p4_draw_text(surface, 216, 5, "ST", COLOR_TEAL_LIGHT, 1U, 2U);
    draw_number(surface, 233, 5, (uint32_t)state->level + 1U, COLOR_WHITE);
    p4_draw_text(surface, 241, 5, "/4", COLOR_CREAM, 1U, 2U);
}

static void draw_stage(p4_game_surface_t *surface,
                       const skyline_leap_state_t *state)
{
    const int stage = stage_index(state);
    draw_background(surface, state);
    for (uint8_t index = 0U; index < s_platform_counts[stage]; ++index) {
        draw_platform(surface, &s_platforms[stage][index]);
    }
    const bool exit_open = state->shards == SHARD_COUNT;
    draw_atlas_cell(surface, 278, 86, ITEM_ROW, exit_open ? 3U : 1U);
    if (!exit_open) {
        p4_draw_text(surface, 270, 83, "LOCK", COLOR_CORAL, 1U, 4U);
    }
    for (uint8_t item = 0U; item < SHARD_COUNT; ++item) {
        if (!state->shard_collected[item]) {
            draw_atlas_cell(surface, s_shards[stage][item].x - 20,
                            s_shards[stage][item].y - 20,
                            ITEM_ROW, 0U);
        }
    }
    for (uint8_t bot = 0U; bot < MAX_BOTS; ++bot) {
        if (!state->bot_active[bot]) {
            continue;
        }
        const bot_spawn_t *const spawn = &s_bot_spawns[stage][bot];
        const platform_t *const platform =
            &s_platforms[stage][spawn->platform_index];
        draw_atlas_cell(surface, state->bot_x[bot] - 12,
                        platform->y - FRAME_HEIGHT,
                        BOT_ROW, state->animation_frame);
    }
    if (state->pulse_active) {
        const int trail_x = state->pulse_direction > 0
            ? state->pulse_x - PULSE_SIZE - 3 : state->pulse_x + 3;
        p4_draw_fill_rect(surface, trail_x, state->pulse_y - 1,
                          PULSE_SIZE, 3, COLOR_TEAL_LIGHT);
        p4_draw_fill_circle(surface, state->pulse_x, state->pulse_y,
                            PULSE_SIZE / 2, COLOR_GOLD);
    }
    draw_courier(surface, state);
    draw_hud(surface, state);
}

static void draw_center_panel(p4_game_surface_t *surface, const char *title,
                              const char *detail, uint16_t border)
{
    p4_draw_fill_rect(surface, 58, 55, 204, 54, COLOR_NAVY);
    p4_draw_rect(surface, 58, 55, 204, 54, border);
    p4_draw_text(surface, 82, 68, title, COLOR_WHITE, 2U, 15U);
    p4_draw_text(surface, 76, 92, detail, COLOR_CREAM, 1U, 22U);
}

static void draw_title(p4_game_surface_t *surface,
                       const skyline_leap_state_t *state)
{
    p4_draw_clear(surface, COLOR_NIGHT);
    for (int x = 12; x < P4_GAME_SURFACE_WIDTH; x += 37) {
        p4_draw_fill_rect(surface, x, 20 + (x % 39), 2, 2, COLOR_CREAM);
    }
    p4_draw_text(surface, 76, 20, "SKYLINE LEAP", COLOR_WHITE, 2U, 12U);
    p4_draw_text(surface, 81, 47, "4 STAGE COURIER RUN", COLOR_TEAL_LIGHT,
                 1U, 19U);
    draw_atlas_cell(surface, 139, 65, COURIER_RUN_ROW,
                    state->animation_frame);
    draw_atlas_cell(surface, 218, 76, BOT_ROW,
                    (unsigned)(state->animation_frame + 1U));
    draw_atlas_cell(surface, 54, 76, ITEM_ROW, 0U);
    p4_draw_text(surface, 89, 111, "COLLECT 3 SHARDS", COLOR_TEAL_LIGHT,
                 1U, 16U);
    p4_draw_text(surface, 100, 121, "A/UP JUMP  B PULSE", COLOR_GOLD,
                 1U, 19U);
    p4_draw_text(surface, 100, 130, "START TO BEGIN", COLOR_WHITE, 1U, 14U);
    p4_game_draw_standard_controls(surface, COLOR_STEEL, COLOR_TEAL,
                                   state->held_buttons);
}

static void reset_stage(skyline_leap_state_t *state)
{
    const int stage = stage_index(state);
    state->player_x = s_start_x[stage];
    state->player_y = FLOOR_TOP - PLAYER_HEIGHT;
    state->velocity_y = 0;
    state->facing = 1;
    state->pulse_active = false;
    state->pulse_cooldown_ms = 0U;
    state->shards = 0U;
    state->simulation_ms = 0U;
    state->transition_ms = 0U;
    state->clearing = false;
    for (uint8_t item = 0U; item < SHARD_COUNT; ++item) {
        state->shard_collected[item] = false;
    }
    for (uint8_t bot = 0U; bot < MAX_BOTS; ++bot) {
        state->bot_x[bot] = s_bot_spawns[stage][bot].start_x;
        state->bot_direction[bot] = s_bot_spawns[stage][bot].direction;
        state->bot_active[bot] = true;
    }
}

static void reset_campaign(skyline_leap_state_t *state)
{
    state->level = 0U;
    state->lives = 3U;
    state->score = 0U;
    state->paused = false;
    state->clearing = false;
    state->finale = false;
    state->game_over = false;
    reset_stage(state);
}

static bool player_is_grounded(const skyline_leap_state_t *state)
{
    const int stage = stage_index(state);
    const int player_bottom = state->player_y + PLAYER_HEIGHT;
    for (uint8_t index = 0U; index < s_platform_counts[stage]; ++index) {
        const platform_t *const platform = &s_platforms[stage][index];
        if (player_bottom == platform->y &&
            ranges_overlap(state->player_x, PLAYER_WIDTH,
                           platform->x, platform->width)) {
            return true;
        }
    }
    return false;
}

static void update_bots(skyline_leap_state_t *state)
{
    const int stage = stage_index(state);
    const int speed = stage >= 2 ? 2 : 1;
    for (uint8_t bot = 0U; bot < MAX_BOTS; ++bot) {
        if (!state->bot_active[bot]) {
            continue;
        }
        const bot_spawn_t *const spawn = &s_bot_spawns[stage][bot];
        const platform_t *const platform =
            &s_platforms[stage][spawn->platform_index];
        int next_x = (int)state->bot_x[bot] +
            (int)state->bot_direction[bot] * speed;
        const int minimum_x = platform->x;
        const int maximum_x = platform->x + platform->width - BOT_WIDTH;
        if (next_x < minimum_x || next_x > maximum_x) {
            state->bot_direction[bot] = (int8_t)-state->bot_direction[bot];
            next_x = clamp_int(next_x, minimum_x, maximum_x);
        }
        state->bot_x[bot] = (int16_t)next_x;
    }
}

static void update_pulse(p4_game_context_t *context,
                         skyline_leap_state_t *state)
{
    if (!state->pulse_active) {
        return;
    }
    state->pulse_x = (int16_t)((int)state->pulse_x +
                                (int)state->pulse_direction * PULSE_SPEED);
    if (state->pulse_x < WORLD_LEFT || state->pulse_x > WORLD_RIGHT) {
        state->pulse_active = false;
        return;
    }
    const int stage = stage_index(state);
    for (uint8_t bot = 0U; bot < MAX_BOTS; ++bot) {
        if (!state->bot_active[bot]) {
            continue;
        }
        const bot_spawn_t *const spawn = &s_bot_spawns[stage][bot];
        const platform_t *const platform =
            &s_platforms[stage][spawn->platform_index];
        const int bot_y = platform->y - BOT_HEIGHT;
        if (!rectangles_overlap(state->pulse_x - PULSE_SIZE / 2,
                                state->pulse_y - PULSE_SIZE / 2,
                                PULSE_SIZE, PULSE_SIZE,
                                state->bot_x[bot], bot_y,
                                BOT_WIDTH, BOT_HEIGHT)) {
            continue;
        }
        state->bot_active[bot] = false;
        state->pulse_active = false;
        state->score += 50U;
        play_tone(context, 220U, 100U, 4U, P4_WAVE_SQUARE);
        return;
    }
}

static void start_pulse(p4_game_context_t *context,
                        skyline_leap_state_t *state)
{
    if (state->pulse_active || state->pulse_cooldown_ms != 0U) {
        return;
    }
    state->pulse_active = true;
    state->pulse_direction = state->facing == 0 ? 1 : state->facing;
    state->pulse_x = (int16_t)(state->player_x +
        (state->pulse_direction > 0 ? PLAYER_WIDTH + 2 : -2));
    state->pulse_y = (int16_t)(state->player_y + PLAYER_HEIGHT / 2);
    state->pulse_cooldown_ms = 260U;
    play_tone(context, 1040U, 55U, 4U, P4_WAVE_SQUARE);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_ACTION);
}

static void update_attack_cooldown(skyline_leap_state_t *state,
                                   uint32_t elapsed_ms)
{
    if (state->pulse_cooldown_ms > elapsed_ms) {
        state->pulse_cooldown_ms =
            (uint16_t)(state->pulse_cooldown_ms - elapsed_ms);
    } else {
        state->pulse_cooldown_ms = 0U;
    }
}

static void lose_life(p4_game_context_t *context, skyline_leap_state_t *state)
{
    if (state->clearing || state->game_over || state->finale) {
        return;
    }
    play_tone(context, 120U, 240U, 5U, P4_WAVE_SQUARE);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
    if (state->lives != 0U) {
        --state->lives;
    }
    if (state->lives == 0U) {
        state->game_over = true;
        return;
    }
    reset_stage(state);
}

static bool check_bot_contacts(p4_game_context_t *context,
                               skyline_leap_state_t *state,
                               int previous_bottom)
{
    const int stage = stage_index(state);
    const int player_bottom = state->player_y + PLAYER_HEIGHT;
    for (uint8_t bot = 0U; bot < MAX_BOTS; ++bot) {
        if (!state->bot_active[bot]) {
            continue;
        }
        const bot_spawn_t *const spawn = &s_bot_spawns[stage][bot];
        const platform_t *const platform =
            &s_platforms[stage][spawn->platform_index];
        const int bot_y = platform->y - BOT_HEIGHT;
        if (!rectangles_overlap(state->player_x, state->player_y,
                                PLAYER_WIDTH, PLAYER_HEIGHT,
                                state->bot_x[bot], bot_y,
                                BOT_WIDTH, BOT_HEIGHT)) {
            continue;
        }
        if (state->velocity_y >= 0 && previous_bottom <= bot_y + 3 &&
            player_bottom >= bot_y) {
            state->bot_active[bot] = false;
            state->player_y = (int16_t)(bot_y - PLAYER_HEIGHT);
            state->velocity_y = -5;
            state->score += 50U;
            play_tone(context, 180U, 90U, 4U, P4_WAVE_SQUARE);
            return true;
        }
        lose_life(context, state);
        return false;
    }
    return true;
}

static void resolve_platforms(skyline_leap_state_t *state, int previous_bottom)
{
    if (state->velocity_y < 0) {
        return;
    }
    const int stage = stage_index(state);
    const int player_bottom = state->player_y + PLAYER_HEIGHT;
    for (uint8_t index = 0U; index < s_platform_counts[stage]; ++index) {
        const platform_t *const platform = &s_platforms[stage][index];
        if (previous_bottom <= platform->y && player_bottom >= platform->y &&
            ranges_overlap(state->player_x, PLAYER_WIDTH,
                           platform->x, platform->width)) {
            state->player_y = (int16_t)(platform->y - PLAYER_HEIGHT);
            state->velocity_y = 0;
            return;
        }
    }
}

static bool move_player(p4_game_context_t *context,
                        skyline_leap_state_t *state)
{
    const int speed = 2;
    int next_x = state->player_x;
    if ((state->held_buttons & P4_BUTTON_LEFT) != 0U) {
        next_x -= speed;
        state->facing = -1;
    }
    if ((state->held_buttons & P4_BUTTON_RIGHT) != 0U) {
        next_x += speed;
        state->facing = 1;
    }
    state->player_x = (int16_t)clamp_int(next_x, WORLD_LEFT,
                                          WORLD_RIGHT - PLAYER_WIDTH);
    if (state->velocity_y < MAX_FALL_SPEED) {
        ++state->velocity_y;
    }
    const int previous_bottom = state->player_y + PLAYER_HEIGHT;
    state->player_y = (int16_t)((int)state->player_y + state->velocity_y);
    if (state->player_y < PLAY_TOP) {
        state->player_y = PLAY_TOP;
        state->velocity_y = 0;
    }
    if (!check_bot_contacts(context, state, previous_bottom)) {
        return false;
    }
    resolve_platforms(state, previous_bottom);
    return true;
}

static void collect_shards(p4_game_context_t *context,
                           skyline_leap_state_t *state)
{
    const int stage = stage_index(state);
    for (uint8_t item = 0U; item < SHARD_COUNT; ++item) {
        if (state->shard_collected[item] ||
            !rectangles_overlap(state->player_x, state->player_y,
                                PLAYER_WIDTH, PLAYER_HEIGHT,
                                s_shards[stage][item].x - 7,
                                s_shards[stage][item].y - 7, 14, 14)) {
            continue;
        }
        state->shard_collected[item] = true;
        ++state->shards;
        state->score += 100U;
        play_tone(context, 940U, 70U, 4U, P4_WAVE_TRIANGLE);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
    }
}

static void begin_stage_clear(p4_game_context_t *context,
                              skyline_leap_state_t *state)
{
    state->clearing = true;
    state->transition_ms = 900U;
    state->score += 300U;
    play_tone(context, 660U, 130U, 4U, P4_WAVE_TRIANGLE);
    play_tone(context, 990U, 180U, 5U, P4_WAVE_TRIANGLE);
}

static void update_stage_transition(p4_game_context_t *context,
                                    skyline_leap_state_t *state,
                                    uint32_t elapsed_ms)
{
    if (state->transition_ms > elapsed_ms) {
        state->transition_ms = (uint16_t)(state->transition_ms - elapsed_ms);
        return;
    }
    state->transition_ms = 0U;
    state->clearing = false;
    if (state->level + 1U == STAGE_COUNT) {
        state->finale = true;
        play_tone(context, 1175U, 220U, 5U, P4_WAVE_TRIANGLE);
        return;
    }
    ++state->level;
    reset_stage(state);
}

static void gameplay_tick(p4_game_context_t *context,
                          skyline_leap_state_t *state)
{
    update_bots(state);
    update_pulse(context, state);
    if (!move_player(context, state)) {
        return;
    }
    collect_shards(context, state);
    if (state->shards == SHARD_COUNT &&
        state->player_x + PLAYER_WIDTH >= 286 &&
        state->player_y + PLAYER_HEIGHT >= FLOOR_TOP - 2) {
        begin_stage_clear(context, state);
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(skyline_leap_state_t)) {
        return false;
    }
    skyline_leap_state_t *const state = context->state;
    *state = (skyline_leap_state_t){.intro = true, .lives = 3U};
    reset_stage(state);
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
    skyline_leap_state_t *const state = context->state;
    (void)p4_game_audio_effect_service(context, &state->audio);
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    state->animation_ms = (uint16_t)(state->animation_ms + elapsed_ms);
    if (state->animation_ms >= 100U) {
        state->animation_ms = 0U;
        state->animation_frame = (uint8_t)((state->animation_frame + 1U) & 3U);
    }
    if (state->intro) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            state->intro = false;
            reset_campaign(state);
            play_tone(context, 740U, 90U, 4U, P4_WAVE_TRIANGLE);
        }
        return P4_GAME_CONTINUE;
    }
    if (state->game_over) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            reset_campaign(state);
            play_tone(context, 523U, 90U, 4U, P4_WAVE_TRIANGLE);
        }
        return P4_GAME_CONTINUE;
    }
    if (state->finale) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            state->intro = true;
        }
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_START) != 0U && !state->clearing) {
        state->paused = !state->paused;
        play_tone(context, state->paused ? 330U : 660U, 70U, 3U,
                  P4_WAVE_SQUARE);
        return P4_GAME_CONTINUE;
    }
    if (state->paused) {
        return P4_GAME_CONTINUE;
    }
    if (state->clearing) {
        update_stage_transition(context, state, elapsed_ms);
        return P4_GAME_CONTINUE;
    }
    update_attack_cooldown(state, elapsed_ms);
    if ((input->pressed & P4_BUTTON_B) != 0U) {
        start_pulse(context, state);
    }
    if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_UP)) != 0U &&
        player_is_grounded(state)) {
        state->velocity_y = JUMP_SPEED;
        play_tone(context, 760U, 65U, 3U, P4_WAVE_TRIANGLE);
    }
    state->simulation_ms += elapsed_ms;
    while (state->simulation_ms >= TICK_MS) {
        state->simulation_ms -= TICK_MS;
        gameplay_tick(context, state);
        if (state->clearing || state->game_over || state->finale) {
            break;
        }
    }
    return P4_GAME_CONTINUE;
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (context == NULL || context->state == NULL || !p4_surface_valid(surface)) {
        return false;
    }
    const skyline_leap_state_t *const state = context->state;
    if (state->intro) {
        draw_title(surface, state);
        return true;
    }
    draw_stage(surface, state);
    p4_game_feedback_draw_audio_effect(
        surface, &state->audio,
        state->game_over || state->finale ? 160 : state->player_x,
        state->game_over || state->finale ? 94 : state->player_y + 9);
    if (state->paused) {
        draw_center_panel(surface, "PAUSED", "START TO RESUME", COLOR_TEAL);
    } else if (state->clearing) {
        draw_center_panel(surface, "SIGNAL LOCKED", "NEXT ROOF...", COLOR_GOLD);
    } else if (state->game_over) {
        draw_center_panel(surface, "ROUTE LOST", "A TO TRY AGAIN", COLOR_CORAL);
    } else if (state->finale) {
        draw_center_panel(surface, "SKYLINE CLEAR", "A FOR TITLE", COLOR_GOLD);
    }
    p4_game_draw_standard_controls(surface, COLOR_STEEL, COLOR_TEAL,
                                   state->held_buttons);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_skyline_leap_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(106),
    .id = "org.p4console.skyline-leap",
    .title = "Skyline Leap",
    .subtitle = "A rooftop courier adventure",
    .accent_rgb565 = UINT16_C(0x05dd),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM,
    .state_bytes = sizeof(skyline_leap_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
