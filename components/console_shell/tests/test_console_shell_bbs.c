// SPDX-License-Identifier: MIT

#include "console/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static const console_app_descriptor_t s_apps[] = {
    {
        .id = 1U,
        .title = "ASTEROIDS",
        .subtitle = "VECTOR REMIX",
        .folder_path = "GAMES/ARCADE",
        .accent_rgb565 = UINT16_C(0x07ff),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY,
        .page = CONSOLE_PAGE_EXTERNAL,
        .enabled = true,
    },
    {
        .id = 2U,
        .title = "APPEARANCE",
        .subtitle = "BBS OR WINDOWS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xfd20),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY,
        .page = CONSOLE_PAGE_COLORS,
        .enabled = true,
    },
};

static console_shell_action_t release_touch(console_shell_t *shell)
{
    return console_shell_handle_touch(shell, true, NULL, 0U);
}

static console_shell_action_t tap_surface(console_shell_t *shell,
                                          uint16_t surface_x,
                                          uint16_t surface_y)
{
    const console_shell_contact_t contact = {
        .x = (uint16_t)(CONSOLE_SHELL_VIEWPORT_LEFT + surface_x),
        .y = (uint16_t)(CONSOLE_SHELL_VIEWPORT_TOP + surface_y),
    };
    (void)console_shell_handle_touch(shell, true, &contact, 1U);
    return release_touch(shell);
}

static uint64_t frame_hash(const uint16_t *pixels, size_t count)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t index = 0U; index < count; ++index) {
        hash ^= pixels[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int main(void)
{
    CHECK(CONSOLE_SHELL_NATIVE_BBS == 1);
    console_shell_t shell;
    CHECK(console_shell_init(
        &shell, s_apps, sizeof(s_apps) / sizeof(s_apps[0])));
    const console_shell_runtime_info_t runtime = {
        .board_kind = CONSOLE_BOARD_WAVESHARE_4_3,
        .content_scan_complete = true,
        .valid_cart_count = 2U,
    };
    console_shell_set_runtime_info(&shell, &runtime);

    const size_t pixel_count =
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT;
    uint16_t *const pixels = calloc(pixel_count, sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels != NULL) {
        CHECK(console_shell_render_rgb565(
            &shell, pixels, CONSOLE_SHELL_WIDTH));
        if (shell.bbs_terminal.scroll_count != 0U) {
            fprintf(stderr, "unexpected BBS scrolls: %u\n",
                    (unsigned)shell.bbs_terminal.scroll_count);
        }
        CHECK(shell.bbs_terminal.scroll_count == 0U);
        CHECK(p4_ansi_cell(&shell.bbs_terminal, 1U, 0U)->character == 0xc9U);
        CHECK(p4_ansi_cell(&shell.bbs_terminal, 6U, 8U)->character == '[');
        const uint64_t bbs_hash = frame_hash(pixels, pixel_count);
        shell.color_mode = CONSOLE_COLOR_MODE_ARCADE;
        shell.dirty = true;
        CHECK(console_shell_render_rgb565(
            &shell, pixels, CONSOLE_SHELL_WIDTH));
        CHECK(frame_hash(pixels, pixel_count) != bbs_hash);
        free(pixels);
    }

    CHECK(console_shell_init(
        &shell, s_apps, sizeof(s_apps) / sizeof(s_apps[0])));
    console_shell_action_t action = tap_surface(&shell, 405U, 130U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "GAMES") == 0);

    console_shell_show_home(&shell);
    shell.home_folder_path[0] = '\0';
    shell.home_all_programs = false;
    shell.selected_home_item = 0U;
    (void)console_shell_handle_buttons(&shell, CONSOLE_BUTTON_RIGHT);
    (void)console_shell_handle_buttons(&shell, 0U);
    action = console_shell_handle_buttons(&shell, CONSOLE_BUTTON_ACCEPT);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "GAMES") == 0);

    if (s_failures != 0) {
        fprintf(stderr, "%d BBS shell test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("console shell BBS tests passed");
    return EXIT_SUCCESS;
}
