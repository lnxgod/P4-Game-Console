// SPDX-License-Identifier: MIT

#include "console/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const console_app_descriptor_t s_apps[] = {
    {.id = 1U, .title = "DOOM", .subtitle = "SHAREWARE 1.9",
     .folder_path = "GAMES/ACTION", .accent_rgb565 = UINT16_C(0xF904),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH |
         CONSOLE_CAPABILITY_AUDIO | CONSOLE_CAPABILITY_STORAGE,
     .page = CONSOLE_PAGE_EXTERNAL, .enabled = true},
    {.id = 100U, .title = "MAZE CHASE", .subtitle = "ORIGINAL GAME",
     .folder_path = "GAMES/ARCADE", .accent_rgb565 = UINT16_C(0x07E0),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH |
         CONSOLE_CAPABILITY_AUDIO,
     .page = CONSOLE_PAGE_EXTERNAL, .enabled = true},
    {.id = 101U, .title = "SPACE INVADERS", .subtitle = "DEFEND THE P4",
     .folder_path = "GAMES/ARCADE", .accent_rgb565 = UINT16_C(0x07FF),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH |
         CONSOLE_CAPABILITY_AUDIO,
     .page = CONSOLE_PAGE_EXTERNAL, .enabled = true},
    {.id = 2U, .title = "COLORS", .subtitle = "DISPLAY TEST",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0x5FFF),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY,
     .page = CONSOLE_PAGE_COLORS, .enabled = true},
    {.id = 3U, .title = "TOUCH", .subtitle = "GT911 CONTACTS",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0xFFE0),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH,
     .page = CONSOLE_PAGE_TOUCH, .enabled = true},
    {.id = 4U, .title = "SYSTEM", .subtitle = "RTOS STATUS",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0x5FEA),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH,
     .page = CONSOLE_PAGE_SYSTEM, .enabled = true},
    {.id = 5U, .title = "AUDIO", .subtitle = "DOOM SOUND PATH",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0xF81F),
     .capabilities = CONSOLE_CAPABILITY_AUDIO,
     .page = CONSOLE_PAGE_AUDIO, .enabled = true},
    {.id = 6U, .title = "GAME MANAGER", .subtitle = "BUILTINS + CARTS",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0x07FF),
     .capabilities = CONSOLE_CAPABILITY_STORAGE,
     .page = CONSOLE_PAGE_LIBRARY, .enabled = true},
    {.id = 8U, .title = "FILE MANAGER", .subtitle = "LIST / SORT / COPY",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0x07FF),
     .capabilities = CONSOLE_CAPABILITY_STORAGE,
     .page = CONSOLE_PAGE_FILES, .enabled = true},
    {.id = 9U, .title = "SAVE MANAGER", .subtitle = "GAME SAVE SLOTS",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0xFFE0),
     .capabilities = CONSOLE_CAPABILITY_STORAGE,
     .page = CONSOLE_PAGE_SAVES, .enabled = true},
    {.id = 10U, .title = "TERMINAL", .subtitle = "COMMANDS + SSH",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0x5FEA),
     .capabilities = CONSOLE_CAPABILITY_TOUCH,
     .page = CONSOLE_PAGE_TERMINAL, .enabled = true},
};

static bool select_page(console_shell_t *shell, const char *name)
{
    if (strcmp(name, "home") == 0) {
        return true;
    }
    if (strcmp(name, "all") == 0) {
        shell->home_all_programs = true;
        return true;
    }
    if (strcmp(name, "games") == 0) {
        (void)strcpy(shell->home_folder_path, "GAMES");
        return true;
    }
    if (strcmp(name, "arcade") == 0) {
        (void)strcpy(shell->home_folder_path, "GAMES/ARCADE");
        return true;
    }
    if (strcmp(name, "system-folder") == 0) {
        (void)strcpy(shell->home_folder_path, "SYSTEM");
        return true;
    }
    for (size_t i = 1U; i < sizeof(s_apps) / sizeof(s_apps[0]); ++i) {
        const char *expected = NULL;
        switch (s_apps[i].page) {
        case CONSOLE_PAGE_COLORS: expected = "colors"; break;
        case CONSOLE_PAGE_TOUCH: expected = "touch"; break;
        case CONSOLE_PAGE_SYSTEM: expected = "system"; break;
        case CONSOLE_PAGE_AUDIO: expected = "audio"; break;
        case CONSOLE_PAGE_LIBRARY: expected = "games-manager"; break;
        case CONSOLE_PAGE_FILES: expected = "files"; break;
        case CONSOLE_PAGE_SAVES: expected = "saves"; break;
        case CONSOLE_PAGE_TERMINAL: expected = "terminal"; break;
        default: break;
        }
        if (expected != NULL && strcmp(name, expected) == 0) {
            shell->page = s_apps[i].page;
            shell->active_app_id = s_apps[i].id;
            shell->dirty = true;
            return true;
        }
    }
    return false;
}

static bool write_ppm(const char *path, const uint16_t *pixels)
{
    FILE *const output = fopen(path, "wb");
    if (output == NULL) {
        return false;
    }
    if (fprintf(output, "P6\n640 400\n255\n") < 0) {
        (void)fclose(output);
        return false;
    }
    for (size_t y = 0U; y < CONSOLE_SHELL_HEIGHT; ++y) {
        for (unsigned duplicate_y = 0U; duplicate_y < 2U; ++duplicate_y) {
            for (size_t x = 0U; x < CONSOLE_SHELL_WIDTH; ++x) {
                const uint16_t pixel = pixels[y * CONSOLE_SHELL_WIDTH + x];
                const unsigned red5 = (unsigned)((pixel >> 11U) & UINT16_C(0x1F));
                const unsigned green6 = (unsigned)((pixel >> 5U) & UINT16_C(0x3F));
                const unsigned blue5 = (unsigned)(pixel & UINT16_C(0x1F));
                const unsigned red = (red5 * 255U + 15U) / 31U;
                const unsigned green = (green6 * 255U + 31U) / 63U;
                const unsigned blue = (blue5 * 255U + 15U) / 31U;
                for (unsigned duplicate_x = 0U; duplicate_x < 2U; ++duplicate_x) {
                    if (fputc((int)red, output) == EOF ||
                        fputc((int)green, output) == EOF ||
                        fputc((int)blue, output) == EOF) {
                        (void)fclose(output);
                        return false;
                    }
                }
            }
        }
    }
    return fclose(output) == 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr,
                "usage: %s home|all|games|arcade|system-folder|"
                "colors|touch|system|audio|games-manager|files|saves|terminal output.ppm\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    console_shell_t shell;
    if (!console_shell_init(
            &shell, s_apps, sizeof(s_apps) / sizeof(s_apps[0])) ||
        !select_page(&shell, argv[1])) {
        fputs("invalid preview page\n", stderr);
        return EXIT_FAILURE;
    }
    const console_shell_runtime_info_t runtime = {
        .uptime_seconds = 3723U,
        .internal_free_kib = 221U,
        .psram_free_kib = 30128U,
        .touch_ready = true,
        .audio_handoff_ready = true,
        .storage_ready = true,
        .content_scan_complete = true,
        .quake_shareware_ready = true,
        .usb_content_ready = true,
        .valid_cart_count = 2U,
        .builtin_game_count = 10U,
    };
    console_shell_set_runtime_info(&shell, &runtime);
    p4_file_list_t files;
    p4_file_list_init(&files);
    (void)p4_file_list_add(&files, "P4/GAMES", 0U,
                           P4_FILE_KIND_FOLDER, true);
    (void)p4_file_list_add(&files, "P4/SAVES", 0U,
                           P4_FILE_KIND_FOLDER, true);
    (void)p4_file_list_add(&files, "PAK0.PAK", UINT64_C(18689235),
                           P4_FILE_KIND_GAME_DATA, true);
    (void)p4_file_list_add(&files, "BOUNCE-LAB.P4CART", UINT64_C(18342),
                           P4_FILE_KIND_CARTRIDGE, true);
    console_shell_set_file_list(&shell, &files);
    if (shell.page == CONSOLE_PAGE_TERMINAL) {
        (void)console_shell_handle_text_key(&shell, 'h');
        (void)console_shell_handle_text_key(&shell, 'e');
        (void)console_shell_handle_text_key(&shell, 'l');
        (void)console_shell_handle_text_key(&shell, 'p');
        (void)console_shell_handle_text_key(&shell, '\n');
    }
    if (shell.page == CONSOLE_PAGE_TOUCH) {
        shell.contact_count = 2U;
        shell.contacts[0] = (console_shell_contact_t){.x = 360U, .y = 330U};
        shell.contacts[1] = (console_shell_contact_t){.x = 720U, .y = 450U};
    }

    uint16_t *const pixels = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*pixels));
    if (pixels == NULL) {
        return EXIT_FAILURE;
    }
    const bool rendered = console_shell_render_rgb565(
        &shell, pixels, CONSOLE_SHELL_WIDTH);
    const bool written = rendered && write_ppm(argv[2], pixels);
    free(pixels);
    return written ? EXIT_SUCCESS : EXIT_FAILURE;
}
