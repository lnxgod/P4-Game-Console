// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>

#include "p4/audio.h"
#include "p4/draw.h"
#include "p4/game.h"

extern const p4_game_descriptor_t p4_frog_hop_game;

enum {
    GUARD_WORDS = 29,
    STRIDE = P4_GAME_SURFACE_WIDTH + 11,
    FRAME_WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
    TOTAL_WORDS = GUARD_WORDS + FRAME_WORDS + GUARD_WORDS,
};

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

static bool start_game(p4_game_instance_t *instance, void *state_memory,
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
    return p4_game_instance_start(instance, &p4_frog_hop_game, &services,
                                  state_memory,
                                  p4_frog_hop_game.state_bytes);
}

static void test_lifecycle_render_and_controls(void)
{
    uint16_t *const allocation = calloc(TOTAL_WORDS, sizeof(*allocation));
    void *const state_memory = calloc(1U, p4_frog_hop_game.state_bytes);
    CHECK(allocation != NULL);
    CHECK(state_memory != NULL);
    if (allocation == NULL || state_memory == NULL) {
        free(state_memory);
        free(allocation);
        return;
    }
    for (size_t index = 0U; index < TOTAL_WORDS; ++index) {
        allocation[index] = UINT16_C(0x5aa5);
    }
    p4_game_surface_t surface = {
        .pixels = allocation + GUARD_WORDS,
        .stride_pixels = STRIDE,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    p4_game_instance_t instance;
    p4_audio_mixer_t mixer;
    CHECK(p4_game_descriptor_valid(&p4_frog_hop_game));
    CHECK(p4_frog_hop_game.launcher_id == 105U);
    CHECK(start_game(&instance, state_memory, &mixer));
    CHECK(p4_game_instance_render(&instance, &surface));

    p4_game_input_t input = {
        .held = P4_BUTTON_A,
        .pressed = P4_BUTTON_A,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    uint32_t random = UINT32_C(0x6f726f67);
    const uint32_t playable_buttons = P4_BUTTON_MASK ^ P4_BUTTON_BACK;
    for (size_t iteration = 0U; iteration < 1000U; ++iteration) {
        input = (p4_game_input_t){
            .held = next_random(&random) & playable_buttons,
            .pressed = next_random(&random) & playable_buttons,
            .released = next_random(&random) & playable_buttons,
            .touch_valid = true,
        };
        CHECK(p4_game_instance_update(&instance, &input,
                                      next_random(&random) % 101U) ==
              P4_GAME_CONTINUE);
        if (iteration % 3U == 0U) {
            CHECK(p4_game_instance_render(&instance, &surface));
        }
    }
    p4_audio_mixer_stats_t audio_stats;
    p4_audio_mixer_get_stats(&mixer, &audio_stats);
    CHECK(audio_stats.tones_started != 0U);

    input = (p4_game_input_t){
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        CHECK(allocation[index] == UINT16_C(0x5aa5));
        CHECK(allocation[GUARD_WORDS + FRAME_WORDS + index] ==
              UINT16_C(0x5aa5));
    }
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_GAME_SURFACE_WIDTH; column < STRIDE;
             ++column) {
            CHECK(surface.pixels[row * STRIDE + column] ==
                  UINT16_C(0x5aa5));
        }
    }
    p4_game_instance_stop(&instance);
    free(state_memory);
    free(allocation);
}

int main(void)
{
    test_lifecycle_render_and_controls();
    if (s_failures != 0) {
        fprintf(stderr, "%d Frog Hop test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("frog_hop tests passed");
    return EXIT_SUCCESS;
}
