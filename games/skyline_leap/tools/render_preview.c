// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "p4/game.h"

extern const p4_game_descriptor_t p4_skyline_leap_game;

static bool write_ppm(const char *path, const p4_game_surface_t *surface)
{
    if (path == NULL || surface == NULL || surface->pixels == NULL) {
        return false;
    }
    FILE *const file = fopen(path, "wb");
    if (file == NULL || fprintf(file, "P6\n%u %u\n255\n", surface->width,
                                surface->height) < 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return false;
    }
    for (uint16_t y = 0U; y < surface->height; ++y) {
        for (uint16_t x = 0U; x < surface->width; ++x) {
            const uint16_t pixel = surface->pixels[
                (size_t)y * surface->stride_pixels + x];
            const unsigned red = ((pixel >> 11U) & 31U) * 255U / 31U;
            const unsigned green = ((pixel >> 5U) & 63U) * 255U / 63U;
            const unsigned blue = (pixel & 31U) * 255U / 31U;
            const unsigned char rgb[3] = {
                (unsigned char)red, (unsigned char)green,
                (unsigned char)blue,
            };
            if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
                (void)fclose(file);
                return false;
            }
        }
    }
    return fclose(file) == 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s TITLE.ppm GAMEPLAY.ppm\n", argv[0]);
        return EXIT_FAILURE;
    }
    uint16_t *const pixels = calloc(
        (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT,
        sizeof(*pixels));
    void *const state = calloc(1U, p4_skyline_leap_game.state_bytes);
    if (pixels == NULL || state == NULL) {
        free(state);
        free(pixels);
        return EXIT_FAILURE;
    }
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    p4_game_surface_t surface = {
        .pixels = pixels,
        .stride_pixels = P4_GAME_SURFACE_WIDTH,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    bool success = p4_game_instance_start(
        &instance, &p4_skyline_leap_game, &services, state,
        p4_skyline_leap_game.state_bytes) &&
        p4_game_instance_render(&instance, &surface) &&
        write_ppm(argv[1], &surface);
    const p4_game_input_t start = {
        .held = P4_BUTTON_A,
        .pressed = P4_BUTTON_A,
        .touch_valid = true,
    };
    success = success && p4_game_instance_update(&instance, &start, 16U) ==
        P4_GAME_CONTINUE;
    for (unsigned frame = 0U; success && frame < 4U; ++frame) {
        const p4_game_input_t input = {
            .held = P4_BUTTON_RIGHT | (frame == 0U ? P4_BUTTON_B : 0U),
            .pressed = frame == 0U ? P4_BUTTON_B : 0U,
            .touch_valid = true,
        };
        success = p4_game_instance_update(&instance, &input, 16U) ==
            P4_GAME_CONTINUE;
    }
    success = success && p4_game_instance_render(&instance, &surface) &&
        write_ppm(argv[2], &surface);
    p4_game_instance_stop(&instance);
    free(state);
    free(pixels);
    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
