// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>

#include "p4/achievements.h"
#include "p4/audio.h"
#include "p4/game.h"

extern const p4_game_descriptor_t p4_byte_buddy_game;

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static bool start_game(p4_game_instance_t *instance, void *state,
                       p4_audio_mixer_t *mixer,
                       p4_achievement_catalog_t *achievements)
{
    p4_audio_mixer_init(mixer);
    p4_achievement_catalog_init(achievements);
    static p4_game_services_t services;
    services = (p4_game_services_t){
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_AUDIO_TONE,
        .audio_context = mixer,
        .game_id = p4_byte_buddy_game.id,
        .play_tone = p4_audio_mixer_service_play_tone,
        .stop_audio = p4_audio_mixer_service_stop,
        .achievement_context = achievements,
        .unlock_achievement = p4_achievement_catalog_service_unlock,
    };
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(
        instance, &p4_byte_buddy_game, &services,
        state, p4_byte_buddy_game.state_bytes);
}

static void press(p4_game_instance_t *instance, uint32_t button)
{
    const p4_game_input_t input = {
        .held = button,
        .pressed = button,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(instance, &input, 16U) ==
          P4_GAME_CONTINUE);
}

static void test_care_achievements_and_exit(void)
{
    CHECK(p4_game_descriptor_valid(&p4_byte_buddy_game));
    CHECK(p4_byte_buddy_game.launcher_id == 108U);
    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    CHECK(state != NULL);
    if (state == NULL) {
        return;
    }
    p4_game_instance_t instance;
    p4_audio_mixer_t mixer;
    p4_achievement_catalog_t achievements;
    CHECK(start_game(&instance, state, &mixer, &achievements));

    press(&instance, P4_BUTTON_A);
    CHECK(achievements.count == 1U);
    CHECK(achievements.entries[0].game_id[0] != '\0');
    CHECK(achievements.entries[0].id[0] != '\0');

    press(&instance, P4_BUTTON_RIGHT);
    press(&instance, P4_BUTTON_RIGHT);
    press(&instance, P4_BUTTON_A);
    press(&instance, P4_BUTTON_A);
    press(&instance, P4_BUTTON_A);
    CHECK(achievements.count == 2U);

    const p4_game_input_t back = {
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    free(state);
}

static void test_render_bounds(void)
{
    enum {
        GUARD = 31,
        STRIDE = P4_GAME_SURFACE_WIDTH + 7,
        WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
        TOTAL = GUARD + WORDS + GUARD,
    };
    uint16_t *const allocation = calloc(TOTAL, sizeof(*allocation));
    void *const state = calloc(1U, p4_byte_buddy_game.state_bytes);
    CHECK(allocation != NULL && state != NULL);
    if (allocation == NULL || state == NULL) {
        free(allocation);
        free(state);
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
    p4_audio_mixer_t mixer;
    p4_achievement_catalog_t achievements;
    CHECK(start_game(&instance, state, &mixer, &achievements));
    CHECK(p4_game_instance_render(&instance, &surface));
    for (size_t index = 0U; index < GUARD; ++index) {
        CHECK(allocation[index] == UINT16_C(0x5aa5));
        CHECK(allocation[GUARD + WORDS + index] == UINT16_C(0x5aa5));
    }
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_GAME_SURFACE_WIDTH;
             column < STRIDE; ++column) {
            CHECK(surface.pixels[row * STRIDE + column] ==
                  UINT16_C(0x5aa5));
        }
    }
    p4_game_instance_stop(&instance);
    free(state);
    free(allocation);
}

int main(void)
{
    test_care_achievements_and_exit();
    test_render_bounds();
    if (s_failures != 0) {
        fprintf(stderr, "%d Byte Buddy test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("Byte Buddy tests passed");
    return EXIT_SUCCESS;
}
