// SPDX-License-Identifier: MIT

#include "p4_games/space_invaders.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "p4/audio.h"

static unsigned expand5(unsigned value)
{
    return (value << 3U) | (value >> 2U);
}

static unsigned expand6(unsigned value)
{
    return (value << 2U) | (value >> 4U);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT.ppm\n", argv[0]);
        return EXIT_FAILURE;
    }
    uint16_t *const pixels = calloc(
        (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT,
        sizeof(*pixels));
    void *const state = calloc(1U, p4_space_invaders_game.state_bytes);
    if (pixels == NULL || state == NULL) {
        free(pixels);
        free(state);
        return EXIT_FAILURE;
    }
    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_AUDIO_TONE,
        .audio_context = &mixer,
        .play_tone = p4_audio_mixer_service_play_tone,
        .submit_pcm16_stereo = NULL,
        .stop_audio = p4_audio_mixer_service_stop,
    };
    p4_game_instance_t instance = {0};
    p4_game_surface_t surface = {
        .pixels = pixels,
        .stride_pixels = P4_GAME_SURFACE_WIDTH,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    if (!p4_game_instance_start(
            &instance, &p4_space_invaders_game, &services,
            state, p4_space_invaders_game.state_bytes)) {
        free(pixels);
        free(state);
        return EXIT_FAILURE;
    }
    p4_game_input_t input = {
        .held = P4_BUTTON_A | P4_BUTTON_RIGHT,
        .pressed = P4_BUTTON_A | P4_BUTTON_RIGHT,
        .touch_valid = true,
    };
    (void)p4_game_instance_update(&instance, &input, 16U);
    input=(p4_game_input_t){0};
    (void)p4_game_instance_update(&instance,&input,0U);
    input=(p4_game_input_t){.held=P4_BUTTON_A|P4_BUTTON_RIGHT,.pressed=P4_BUTTON_A};
    (void)p4_game_instance_update(&instance,&input,16U);
    input.pressed = 0U;
    for (unsigned frame = 0U; frame < 24U; ++frame) {
        (void)p4_game_instance_update(&instance, &input, 16U);
    }
    if (!p4_game_instance_render(&instance, &surface)) {
        p4_game_instance_stop(&instance);
        free(pixels);
        free(state);
        return EXIT_FAILURE;
    }
    FILE *const output = fopen(argv[1], "wb");
    if (output == NULL) {
        p4_game_instance_stop(&instance);
        free(pixels);
        free(state);
        return EXIT_FAILURE;
    }
    fprintf(output, "P6\n%d %d\n255\n",
            P4_GAME_SURFACE_WIDTH, P4_GAME_SURFACE_HEIGHT);
    for (size_t i = 0U;
         i < (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT; ++i) {
        const uint16_t pixel = pixels[i];
        const unsigned red = expand5((pixel >> 11U) & 31U);
        const unsigned green = expand6((pixel >> 5U) & 63U);
        const unsigned blue = expand5(pixel & 31U);
        fputc((int)red, output);
        fputc((int)green, output);
        fputc((int)blue, output);
    }
    const int close_result = fclose(output);
    p4_game_instance_stop(&instance);
    free(pixels);
    free(state);
    return close_result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
