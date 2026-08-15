// SPDX-License-Identifier: MIT

#include "p4_games/space_invaders.h"

#include <stdio.h>
#include <stdlib.h>

#include "p4/audio.h"
#include "space_invaders_internal.h"

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
                       space_invaders_state_t *state,
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
        instance, &p4_space_invaders_game, &services,
        state, sizeof(*state));
}

static void test_descriptor_and_reset(void)
{
    CHECK(p4_game_descriptor_valid(&p4_space_invaders_game));
    CHECK(p4_space_invaders_game.launcher_id == 101U);
    space_invaders_state_t state;
    space_invaders_reset(&state);
    CHECK(state.alive_mask == UINT32_MAX);
    CHECK(state.invaders_remaining == SPACE_INVADER_COUNT);
    CHECK(state.lives == 3U);
    CHECK(state.wave == 1U);
    CHECK(state.player_x == 160);
    for (size_t i = 0U; i < SPACE_SHIELD_COUNT; ++i) {
        CHECK(state.shields[i] == UINT16_C(0x0fff));
    }
}

static void test_controls_sound_and_exit(void)
{
    p4_game_instance_t instance;
    space_invaders_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game(&instance, &state, &mixer));
    p4_game_input_t input = {
        .held = P4_BUTTON_A | P4_BUTTON_LEFT,
        .pressed = P4_BUTTON_A | P4_BUTTON_LEFT,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player_projectiles[0].active);
    CHECK(state.player_projectiles[0].y < 130);
    CHECK(state.player_x == 158);
    p4_audio_mixer_stats_t stats;
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.tones_started >= 2U);
    input = (p4_game_input_t){
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

static void test_hits_wave_and_player_damage(void)
{
    p4_game_instance_t instance;
    space_invaders_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game(&instance, &state, &mixer));
    state.alive_mask = UINT32_C(1);
    state.invaders_remaining = 1U;
    state.player_projectiles[0] = (space_projectile_t){
        .x = state.formation_x + 5,
        .y = state.formation_y + 6,
        .active = true,
    };
    p4_game_input_t input = {.touch_valid = true};
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.alive_mask == 0U);
    CHECK(state.invaders_remaining == 0U);
    CHECK(state.score == 40U);
    CHECK(state.wave_delay_ms != 0U);
    state.wave_delay_ms = 1U;
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.wave == 2U);
    CHECK(state.invaders_remaining == SPACE_INVADER_COUNT);
    state.enemy_projectiles[0] = (space_projectile_t){
        .x = state.player_x, .y = 131, .active = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.lives == 2U);
    CHECK(state.respawn_ms != 0U);
    p4_game_instance_stop(&instance);
}

static void test_render_bounds_and_fuzz(void)
{
    enum {
        GUARD = 37,
        STRIDE = P4_GAME_SURFACE_WIDTH + 7,
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
    space_invaders_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game(&instance, &state, &mixer));
    CHECK(p4_game_instance_render(&instance, &surface));
    uint32_t random = UINT32_C(0x5aace123);
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
        CHECK(state.player_x >= 97 && state.player_x <= 223);
        CHECK(state.invaders_remaining <= SPACE_INVADER_COUNT);
        CHECK(state.lives <= 3U);
        if (iteration % 7U == 0U) {
            CHECK(p4_game_instance_render(&instance, &surface));
        }
    }
    for (size_t i = 0U; i < GUARD; ++i) {
        CHECK(allocation[i] == UINT16_C(0x5aa5));
        CHECK(allocation[GUARD + WORDS + i] == UINT16_C(0x5aa5));
    }
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_GAME_SURFACE_WIDTH; column < STRIDE;
             ++column) {
            CHECK(surface.pixels[row * STRIDE + column] ==
                  UINT16_C(0x5aa5));
        }
    }
    p4_game_instance_stop(&instance);
    free(allocation);
}

int main(void)
{
    test_descriptor_and_reset();
    test_controls_sound_and_exit();
    test_hits_wave_and_player_damage();
    test_render_bounds_and_fuzz();
    if (s_failures != 0) {
        fprintf(stderr, "%d space-invaders test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("space-invaders tests passed");
    return EXIT_SUCCESS;
}
