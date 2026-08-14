// SPDX-License-Identifier: MIT

#include "console/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const console_app_descriptor_t s_apps[] = {
    {1U, "DOOM", "SHAREWARE 1.9", UINT16_C(0xF904),
     CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH |
         CONSOLE_CAPABILITY_AUDIO | CONSOLE_CAPABILITY_STORAGE,
     CONSOLE_PAGE_EXTERNAL, true},
    {2U, "COLORS", "DISPLAY TEST", UINT16_C(0x5FFF),
     CONSOLE_CAPABILITY_DISPLAY, CONSOLE_PAGE_COLORS, true},
    {3U, "TOUCH", "GT911 CONTACTS", UINT16_C(0xFFE0),
     CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH,
     CONSOLE_PAGE_TOUCH, true},
    {4U, "SYSTEM", "RTOS STATUS", UINT16_C(0x5FEA),
     CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH,
     CONSOLE_PAGE_SYSTEM, true},
    {5U, "AUDIO", "DOOM SOUND PATH", UINT16_C(0xF81F),
     CONSOLE_CAPABILITY_AUDIO, CONSOLE_PAGE_AUDIO, true},
};

static bool select_page(console_shell_t *shell, const char *name)
{
    if (strcmp(name, "home") == 0) {
        return true;
    }
    for (size_t i = 1U; i < sizeof(s_apps) / sizeof(s_apps[0]); ++i) {
        const char *expected = NULL;
        switch (s_apps[i].page) {
        case CONSOLE_PAGE_COLORS: expected = "colors"; break;
        case CONSOLE_PAGE_TOUCH: expected = "touch"; break;
        case CONSOLE_PAGE_SYSTEM: expected = "system"; break;
        case CONSOLE_PAGE_AUDIO: expected = "audio"; break;
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
                "usage: %s home|colors|touch|system|audio output.ppm\n",
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
    };
    console_shell_set_runtime_info(&shell, &runtime);
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
