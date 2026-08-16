// SPDX-License-Identifier: MIT

#include "p4_games/breakout.h"

#include <stdio.h>
#include <stdlib.h>

#include "breakout_internal.h"
#include "p4/audio.h"
#include "p4/draw.h"

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static bool start_game(p4_game_instance_t *instance,
                       breakout_state_t *state,
                       p4_audio_mixer_t *mixer)
{
    p4_audio_mixer_init(mixer);
    const p4_game_services_t services = {
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
        instance, &p4_breakout_game, &services, state, sizeof(*state));
}

static void test_descriptor_and_reset(void)
{
    breakout_state_t state;
    CHECK(p4_game_descriptor_valid(&p4_breakout_game));
    CHECK(p4_breakout_game.launcher_id == 102U);
    breakout_reset(&state);
    CHECK(state.bricks == (UINT64_C(1) << BREAKOUT_BRICK_COUNT) - 1U);
    CHECK(state.lives == 3U);
    CHECK(state.paddle_x == 160);
    CHECK(state.ball_dx == 2);
    CHECK(state.ball_dy == -3);
}

static void test_touch_paddle_and_brick_hit(void)
{
    p4_game_instance_t instance;
    breakout_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game(&instance, &state, &mixer));

    p4_game_input_t input = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = 250U, .y = 60U}},
    };
    CHECK(p4_game_instance_update(&instance, &input, 1U) ==
          P4_GAME_CONTINUE);
    CHECK(state.paddle_x == 250);

    state.bricks = UINT64_C(1);
    state.score = 0U;
    state.ball_x = 30;
    state.ball_y = 47;
    state.ball_dx = 1;
    state.ball_dy = -3;
    input = (p4_game_input_t){.touch_valid = true};
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.bricks == 0U);
    CHECK(state.score == 10U);
    CHECK(state.won);
    p4_game_instance_stop(&instance);
}

static void test_back_and_render_guards(void)
{
    enum {
        GUARD = 19,
        STRIDE = P4_GAME_SURFACE_WIDTH + 9,
        WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
        TOTAL = GUARD + WORDS + GUARD,
    };
    uint16_t *const allocation = calloc(TOTAL, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation == NULL) {
        return;
    }
    for (size_t index = 0U; index < TOTAL; ++index) {
        allocation[index] = UINT16_C(0x5aa5);
    }
    p4_game_surface_t surface = {
        .pixels = allocation + GUARD,
        .stride_pixels = STRIDE,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    p4_game_instance_t instance;
    breakout_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game(&instance, &state, &mixer));
    CHECK(p4_game_instance_render(&instance, &surface));
    for (size_t index = 0U; index < GUARD; ++index) {
        CHECK(allocation[index] == UINT16_C(0x5aa5));
        CHECK(allocation[GUARD + WORDS + index] == UINT16_C(0x5aa5));
    }
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_GAME_SURFACE_WIDTH; column < STRIDE;
             ++column) {
            CHECK(surface.pixels[row * STRIDE + column] ==
                  UINT16_C(0x5aa5));
        }
    }
    p4_game_input_t input = {
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    free(allocation);
}

int main(void)
{
    test_descriptor_and_reset();
    test_touch_paddle_and_brick_hit();
    test_back_and_render_guards();
    if (s_failures != 0) {
        fprintf(stderr, "%d breakout test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("breakout tests passed");
    return EXIT_SUCCESS;
}
