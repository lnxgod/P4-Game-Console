// SPDX-License-Identifier: MIT

#include "p4/bbs_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool write_ppm(const char *path, const uint16_t *pixels)
{
    FILE *const output = fopen(path, "wb");
    if (output == NULL || fprintf(
            output, "P6\n%d %d\n255\n",
            P4_ANSI_SURFACE_WIDTH, P4_ANSI_SURFACE_HEIGHT) < 0) {
        if (output != NULL) {
            (void)fclose(output);
        }
        return false;
    }
    for (size_t index = 0U;
         index < (size_t)P4_ANSI_SURFACE_WIDTH * P4_ANSI_SURFACE_HEIGHT;
         ++index) {
        const uint16_t pixel = pixels[index];
        const uint8_t rgb[3] = {
            (uint8_t)((((pixel >> 11U) & 0x1fU) * 255U + 15U) / 31U),
            (uint8_t)((((pixel >> 5U) & 0x3fU) * 255U + 31U) / 63U),
            (uint8_t)(((pixel & 0x1fU) * 255U + 15U) / 31U),
        };
        if (fwrite(rgb, sizeof(rgb), 1U, output) != 1U) {
            (void)fclose(output);
            return false;
        }
    }
    return fclose(output) == 0;
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s [boot] output.ppm\n", argv[0]);
        return EXIT_FAILURE;
    }
    const bool boot_preview = argc == 3 && strcmp(argv[1], "boot") == 0;
    if (argc == 3 && !boot_preview) {
        fprintf(stderr, "usage: %s [boot] output.ppm\n", argv[0]);
        return EXIT_FAILURE;
    }
    const p4_bbs_launcher_model_t model = {
        .board_name = "GAME CHANGERS AI BBS",
        .node_name = "GC-P4-B289",
        .section = "[ DOOR GAMES ]",
        .connection = "ONLINE",
        .doors = {
            {.number = 1U, .title = "ASTEROIDS",
             .subtitle = "VECTOR REMIX", .accent = P4_ANSI_COLOR_BRIGHT_CYAN,
             .enabled = true},
            {.number = 2U, .title = "MAZE CHASE",
             .subtitle = "CHASE THE BYTE", .accent = P4_ANSI_COLOR_YELLOW,
             .enabled = true},
            {.number = 3U, .title = "BREAKOUT",
             .subtitle = "BRICK LAB", .accent = P4_ANSI_COLOR_BRIGHT_GREEN,
             .enabled = true},
            {.number = 4U, .title = "BYTE BUDDY",
             .subtitle = "CARE + CREATE", .accent = P4_ANSI_COLOR_BRIGHT_MAGENTA,
             .enabled = true},
            {.number = 5U, .title = "SOLITAIRE",
             .subtitle = "CARD TABLE", .accent = P4_ANSI_COLOR_BRIGHT_RED,
             .enabled = true},
        },
        .door_count = 5U,
        .selected_door = 0U,
        .page = 1U,
        .page_count = 2U,
        .uploads = 3U,
        .trades = 1U,
        .local_board = true,
    };
    p4_ansi_terminal_t terminal;
    const p4_bbs_boot_model_t boot = {
        .phase = P4_BBS_BOOT_TRAINING,
        .node_name = "GC-P4-B289",
        .status = "V.22BIS TRAINING / 2400 BAUD",
        .detail = "NEGOTIATING LOCAL BBS SESSION",
        .progress_step = 3U,
        .progress_total = 5U,
    };
    if (boot_preview
            ? !p4_bbs_build_boot_screen(&terminal, &boot)
            : !p4_bbs_build_launcher(&terminal, &model)) {
        return EXIT_FAILURE;
    }
    uint16_t *const pixels = calloc(
        (size_t)P4_ANSI_SURFACE_WIDTH * P4_ANSI_SURFACE_HEIGHT,
        sizeof(*pixels));
    if (pixels == NULL || !p4_ansi_render_rgb565(
            &terminal, pixels, P4_ANSI_SURFACE_WIDTH,
            P4_ANSI_SURFACE_WIDTH, P4_ANSI_SURFACE_HEIGHT) ||
        !write_ppm(argv[argc - 1], pixels)) {
        free(pixels);
        return EXIT_FAILURE;
    }
    free(pixels);
    return EXIT_SUCCESS;
}
