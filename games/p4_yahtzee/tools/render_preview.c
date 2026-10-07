// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "p4/game.h"
#include "p4_yahtzee_internal.h"

extern const p4_game_descriptor_t p4_p4_yahtzee_game;

static unsigned expand5(unsigned value)
{
    return (value << 3U) | (value >> 2U);
}

static unsigned expand6(unsigned value)
{
    return (value << 2U) | (value >> 4U);
}

static bool update(p4_game_instance_t *instance, uint32_t pressed,
                   uint32_t elapsed_ms)
{
    const p4_game_input_t input = {
        .held = pressed,
        .pressed = pressed,
        .touch_valid = true,
    };
    return p4_game_instance_update(instance, &input, elapsed_ms) ==
        P4_GAME_CONTINUE;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT.ppm\n", argv[0]);
        return EXIT_FAILURE;
    }
    uint16_t *const pixels = calloc(
        (size_t)P4_GAME_SURFACE_HIGH_RES_WIDTH * P4_GAME_SURFACE_HIGH_RES_HEIGHT,
        sizeof(*pixels));
    p4_yahtzee_state_t *const state = calloc(1U, sizeof(*state));
    if (pixels == NULL || state == NULL) {
        free(state);
        free(pixels);
        return EXIT_FAILURE;
    }
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            P4_GAME_CAP_VIDEO_HIGH_RES,
    };
    p4_game_instance_t instance = {0};
    p4_game_surface_t surface = {
        .pixels = pixels,
        .stride_pixels = P4_GAME_SURFACE_HIGH_RES_WIDTH,
        .width = P4_GAME_SURFACE_HIGH_RES_WIDTH,
        .height = P4_GAME_SURFACE_HIGH_RES_HEIGHT,
    };
    if (!p4_game_instance_start(
            &instance, &p4_p4_yahtzee_game, &services,
            state, sizeof(*state)) ||
        !update(&instance, P4_BUTTON_RIGHT, 16U) ||
        !update(&instance, P4_BUTTON_RIGHT, 16U) ||
        !update(&instance, P4_BUTTON_A, 16U) ||
        !update(&instance, P4_BUTTON_START, 16U)) {
        free(state);
        free(pixels);
        return EXIT_FAILURE;
    }
    for (size_t step = 0U; step < 4U; ++step) {
        if (!update(&instance, 0U, 100U)) {
            free(state);
            free(pixels);
            return EXIT_FAILURE;
        }
    }
    state->held_mask = UINT8_C(0x05);
    state->focus = P4_YAHTZEE_FOCUS_SCORE;
    state->selected_category = P4_YAHTZEE_FULL_HOUSE;
    state->scores[0][P4_YAHTZEE_ONES] = 3;
    state->scores[0][P4_YAHTZEE_TWOS] = 6;
    state->scores[0][P4_YAHTZEE_THREES] = 9;
    state->scores[1][P4_YAHTZEE_CHANCE] = 22;
    state->scores[2][P4_YAHTZEE_FULL_HOUSE] = 25;
    state->scores[3][P4_YAHTZEE_CHANCE] = 18;
    if (!p4_game_instance_render(&instance, &surface)) {
        p4_game_instance_stop(&instance);
        free(state);
        free(pixels);
        return EXIT_FAILURE;
    }
    FILE *const output = fopen(argv[1], "wb");
    if (output == NULL) {
        p4_game_instance_stop(&instance);
        free(state);
        free(pixels);
        return EXIT_FAILURE;
    }
    fprintf(output, "P6\n%d %d\n255\n",
            P4_GAME_SURFACE_HIGH_RES_WIDTH, P4_GAME_SURFACE_HIGH_RES_HEIGHT);
    for (size_t index = 0U; index <
         (size_t)P4_GAME_SURFACE_HIGH_RES_WIDTH * P4_GAME_SURFACE_HIGH_RES_HEIGHT; ++index) {
        const uint16_t pixel = pixels[index];
        fputc((int)expand5((pixel >> 11U) & 31U), output);
        fputc((int)expand6((pixel >> 5U) & 63U), output);
        fputc((int)expand5(pixel & 31U), output);
    }
    const int close_result = fclose(output);
    p4_game_instance_stop(&instance);
    free(state);
    free(pixels);
    return close_result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
