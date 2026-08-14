// SPDX-License-Identifier: MIT

#include "p4_games/maze_chase.h"

#include <stdio.h>
#include <stdlib.h>

#include "maze_chase_internal.h"
#include "p4/audio.h"

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static bool start_game(p4_game_instance_t *instance,
                       maze_chase_state_t *state,
                       p4_audio_mixer_t *mixer)
{
    p4_audio_mixer_init(mixer);
    static p4_game_services_t services;
    services = (p4_game_services_t){
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_AUDIO_TONE,
        .audio_context = mixer,
        .play_tone = p4_audio_mixer_service_play_tone,
        .submit_pcm16_stereo = NULL,
        .stop_audio = p4_audio_mixer_service_stop,
    };
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(
        instance, &p4_maze_chase_game, &services,
        state, sizeof(*state));
}

static void test_descriptor_and_reset(void)
{
    CHECK(p4_game_descriptor_valid(&p4_maze_chase_game));
    CHECK(p4_maze_chase_game.api_version == P4_GAME_API_VERSION);
    CHECK(p4_maze_chase_game.launcher_id == 100U);
    maze_chase_state_t state;
    maze_chase_reset(&state);
    CHECK(state.lives == 3U);
    CHECK(state.score == 0U);
    CHECK(state.pellets_remaining > 80U);
    CHECK(maze_chase_cell_open(state.player_x, state.player_y));
    for (size_t i = 0U; i < MAZE_CHASE_ENEMY_COUNT; ++i) {
        CHECK(maze_chase_cell_open(state.enemies[i].x, state.enemies[i].y));
    }
    CHECK(!maze_chase_cell_open(0, 0));
    CHECK(!maze_chase_cell_open(-1, 1));
    CHECK(!maze_chase_cell_open(MAZE_CHASE_WIDTH, 1));
}

static void test_movement_sound_and_exit(void)
{
    p4_game_instance_t instance;
    maze_chase_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game(&instance, &state, &mixer));
    const uint16_t pellets_before = state.pellets_remaining;
    p4_game_input_t input = {
        .held = P4_BUTTON_LEFT,
        .pressed = P4_BUTTON_LEFT,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 100U) ==
          P4_GAME_CONTINUE);
    input.pressed = 0U;
    CHECK(p4_game_instance_update(&instance, &input, 12U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player_x == 8U && state.player_y == 8U);
    CHECK(state.score == 10U);
    CHECK(state.pellets_remaining + 1U == pellets_before);
    p4_audio_mixer_stats_t stats;
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.tones_started >= 2U);

    state.player_x = 1U;
    state.player_y = 1U;
    state.player_direction = MAZE_DIRECTION_LEFT;
    state.desired_direction = MAZE_DIRECTION_LEFT;
    state.player_move_accumulator_ms = 0U;
    CHECK(p4_game_instance_update(&instance, &input, 100U) ==
          P4_GAME_CONTINUE);
    CHECK(p4_game_instance_update(&instance, &input, 12U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player_x == 1U && state.player_y == 1U);

    input = (p4_game_input_t){
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

static void test_render_bounds_and_fuzz(void)
{
    enum {
        GUARD = 31,
        STRIDE = P4_GAME_SURFACE_WIDTH + 5,
        WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
        TOTAL = GUARD + WORDS + GUARD,
    };
    uint16_t *const allocation = calloc(TOTAL, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation == NULL) {
        return;
    }
    for (size_t i = 0U; i < TOTAL; ++i) {
        allocation[i] = UINT16_C(0x5aa5);
    }
    p4_game_surface_t surface = {
        .pixels = allocation + GUARD,
        .stride_pixels = STRIDE,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    p4_game_instance_t instance;
    maze_chase_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game(&instance, &state, &mixer));
    CHECK(p4_game_instance_render(&instance, &surface));
    uint32_t random = UINT32_C(0xc0ffee01);
    for (size_t iteration = 0U; iteration < 5000U; ++iteration) {
        p4_game_input_t input = {
            .held = next_random(&random) & P4_BUTTON_MASK,
            .pressed = next_random(&random) & P4_BUTTON_MASK,
            .released = next_random(&random) & P4_BUTTON_MASK,
            .touch_valid = true,
        };
        const p4_game_result_t result = p4_game_instance_update(
            &instance, &input, next_random(&random) % 101U);
        CHECK(result >= P4_GAME_CONTINUE && result <= P4_GAME_ERROR);
        CHECK(maze_chase_cell_open(state.player_x, state.player_y));
        for (size_t i = 0U; i < MAZE_CHASE_ENEMY_COUNT; ++i) {
            CHECK(maze_chase_cell_open(
                state.enemies[i].x, state.enemies[i].y));
        }
        if (result == P4_GAME_EXIT_TO_LAUNCHER) {
            input.held = P4_BUTTON_A;
            input.pressed = P4_BUTTON_A;
            (void)p4_game_instance_update(&instance, &input, 16U);
        }
        if (iteration % 7U == 0U) {
            CHECK(p4_game_instance_render(&instance, &surface));
        }
    }
    for (size_t i = 0U; i < GUARD; ++i) {
        CHECK(allocation[i] == UINT16_C(0x5aa5));
        CHECK(allocation[GUARD + WORDS + i] == UINT16_C(0x5aa5));
    }
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_GAME_SURFACE_WIDTH; column < STRIDE; ++column) {
            CHECK(surface.pixels[row * STRIDE + column] == UINT16_C(0x5aa5));
        }
    }
    p4_game_instance_stop(&instance);
    free(allocation);
}

int main(void)
{
    test_descriptor_and_reset();
    test_movement_sound_and_exit();
    test_render_bounds_and_fuzz();
    if (s_failures != 0) {
        fprintf(stderr, "%d maze-chase test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("maze-chase tests passed");
    return EXIT_SUCCESS;
}
