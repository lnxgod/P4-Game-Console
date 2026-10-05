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

static const console_app_descriptor_t s_paged_apps[] = {
    {1U, "ONE", "DOOR", "ONE", UINT16_C(0x07ff),
     CONSOLE_CAPABILITY_DISPLAY, CONSOLE_PAGE_EXTERNAL, true},
    {2U, "TWO", "DOOR", "TWO", UINT16_C(0xffe0),
     CONSOLE_CAPABILITY_DISPLAY, CONSOLE_PAGE_EXTERNAL, true},
    {3U, "THREE", "DOOR", "THREE", UINT16_C(0x07e0),
     CONSOLE_CAPABILITY_DISPLAY, CONSOLE_PAGE_EXTERNAL, true},
    {4U, "FOUR", "DOOR", "FOUR", UINT16_C(0xf81f),
     CONSOLE_CAPABILITY_DISPLAY, CONSOLE_PAGE_EXTERNAL, true},
    {5U, "FIVE", "DOOR", "FIVE", UINT16_C(0xf800),
     CONSOLE_CAPABILITY_DISPLAY, CONSOLE_PAGE_EXTERNAL, true},
    {6U, "SIX", "DOOR", "SIX", UINT16_C(0x001f),
     CONSOLE_CAPABILITY_DISPLAY, CONSOLE_PAGE_EXTERNAL, true},
    {7U, "SEVEN", "DOOR", "SEVEN", UINT16_C(0xffff),
     CONSOLE_CAPABILITY_DISPLAY, CONSOLE_PAGE_EXTERNAL, true},
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
        .x = (uint16_t)(CONSOLE_SHELL_VIEWPORT_LEFT +
            (uint32_t)surface_x * CONSOLE_SHELL_VIEWPORT_WIDTH / CONSOLE_SHELL_WIDTH),
        .y = (uint16_t)(CONSOLE_SHELL_VIEWPORT_TOP +
            (uint32_t)surface_y * CONSOLE_SHELL_VIEWPORT_HEIGHT / CONSOLE_SHELL_HEIGHT),
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

static bool terminal_contains(const p4_ansi_terminal_t *terminal,
                              const char *text)
{
    const size_t length = strlen(text);
    for (size_t row = 0U; row < P4_ANSI_ROWS; ++row) {
        for (size_t column = 0U;
             column + length <= P4_ANSI_COLUMNS; ++column) {
            size_t matched = 0U;
            while (matched < length &&
                   p4_ansi_cell(terminal, column + matched, row)->character ==
                       (uint8_t)text[matched]) {
                ++matched;
            }
            if (matched == length) {
                return true;
            }
        }
    }
    return false;
}

static void select_bbs(console_shell_t *shell)
{
    shell->color_mode = CONSOLE_COLOR_MODE_GAMECHANGERS;
    shell->dirty = true;
}

static void render_bbs_reference(console_shell_t *shell,
                                 uint16_t *output,
                                 uint16_t *scratch,
                                 size_t target_row,
                                 size_t base_row,
                                 unsigned fraction_pixels)
{
    const size_t pixels = (size_t)CONSOLE_SHELL_WIDTH *
        CONSOLE_SHELL_HEIGHT;
    shell->home_scroll_row = target_row;
    shell->home_scroll_visual_q16 = (int32_t)(target_row << 16);
    CHECK(console_shell_render_rgb565(
        shell, output, CONSOLE_SHELL_WIDTH));
    shell->home_scroll_row = base_row;
    shell->home_scroll_visual_q16 = (int32_t)(base_row << 16);
    CHECK(console_shell_render_rgb565(
        shell, scratch, CONSOLE_SHELL_WIDTH));
    const uint16_t *const base = scratch;
    uint16_t *const adjacent = calloc(pixels, sizeof(*adjacent));
    CHECK(adjacent != NULL);
    if (adjacent == NULL) {
        return;
    }
    shell->home_scroll_row = base_row + 1U;
    shell->home_scroll_visual_q16 = (int32_t)((base_row + 1U) << 16);
    CHECK(console_shell_render_rgb565(
        shell, adjacent, CONSOLE_SHELL_WIDTH));
    shell->home_scroll_row = target_row;
    shell->home_scroll_visual_q16 = (int32_t)(
        (base_row << 16) + fraction_pixels * (1U << 16) / 48U);
    for (unsigned y = 144U; y < 368U; ++y) {
        const int base_source_y = (int)y + (int)fraction_pixels;
        if (base_source_y >= 144 && base_source_y < 368) {
            memcpy(output + (size_t)y * CONSOLE_SHELL_WIDTH,
                   base + (size_t)base_source_y * CONSOLE_SHELL_WIDTH,
                   CONSOLE_SHELL_WIDTH * sizeof(*output));
        }
        const int adjacent_source_y = (int)y - 48 +
            (int)fraction_pixels;
        if (adjacent_source_y >= 144 && adjacent_source_y < 368) {
            memcpy(output + (size_t)y * CONSOLE_SHELL_WIDTH,
                   adjacent + (size_t)adjacent_source_y *
                       CONSOLE_SHELL_WIDTH,
                   CONSOLE_SHELL_WIDTH * sizeof(*output));
        }
    }
    free(adjacent);
}

int main(void)
{
    CHECK(CONSOLE_SHELL_NATIVE_BBS == 1);
    console_shell_t shell;
    CHECK(console_shell_init(
        &shell, s_apps, sizeof(s_apps) / sizeof(s_apps[0])));
    CHECK(console_shell_color_mode(&shell) == CONSOLE_COLOR_MODE_ARCADE);
    CHECK(!console_shell_uses_native_bbs_launcher(&shell));
    const console_shell_runtime_info_t runtime = {
        .board_kind = CONSOLE_BOARD_WAVESHARE_4_3,
        .content_scan_complete = true,
        .valid_cart_count = 2U,
        .battery_supported = true,
        .battery_sample_valid = true,
        .battery_percent = 63U,
    };
    console_shell_set_runtime_info(&shell, &runtime);

    const size_t pixel_count =
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT;
    uint16_t *const pixels = calloc(pixel_count, sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels != NULL) {
        CHECK(console_shell_render_rgb565(
            &shell, pixels, CONSOLE_SHELL_WIDTH));
        const uint64_t windows_hash = frame_hash(pixels, pixel_count);
        select_bbs(&shell);
        CHECK(console_shell_uses_native_bbs_launcher(&shell));
        shell.page = CONSOLE_PAGE_SYSTEM;
        CHECK(!console_shell_uses_native_bbs_launcher(&shell));
        shell.page = CONSOLE_PAGE_HOME;
        CHECK(console_shell_render_rgb565(
            &shell, pixels, CONSOLE_SHELL_WIDTH));
        if (shell.bbs_terminal.scroll_count != 0U) {
            fprintf(stderr, "unexpected BBS scrolls: %u\n",
                    (unsigned)shell.bbs_terminal.scroll_count);
        }
        CHECK(shell.bbs_terminal.scroll_count == 0U);
        CHECK(p4_ansi_cell(&shell.bbs_terminal, 1U, 0U)->character == 0xc9U);
        CHECK(p4_ansi_cell(&shell.bbs_terminal, 5U, 9U)->character == '>');
        CHECK(terminal_contains(&shell.bbs_terminal, "CONTROL PANEL"));
        CHECK(terminal_contains(&shell.bbs_terminal, "[##] 63%"));
        const uint64_t bbs_hash = frame_hash(pixels, pixel_count);
        CHECK(windows_hash != bbs_hash);
        uint16_t *const present = calloc(
            (size_t)CONSOLE_SHELL_PRESENT_WIDTH *
                CONSOLE_SHELL_PRESENT_HEIGHT,
            sizeof(*present));
        CHECK(present != NULL);
        if (present != NULL) {
            CHECK(!console_shell_render_present_rgb565(
                &shell, present, CONSOLE_SHELL_PRESENT_WIDTH));
            free(present);
        }
        free(pixels);
    }

    CHECK(console_shell_init(
        &shell, s_apps, sizeof(s_apps) / sizeof(s_apps[0])));
    select_bbs(&shell);
    console_shell_action_t action = tap_surface(&shell, 405U, 212U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "GAMES") == 0);
    CHECK(console_shell_uses_native_bbs_launcher(&shell));

    console_shell_show_home(&shell);
    CHECK(console_shell_uses_native_bbs_launcher(&shell));
    shell.home_folder_path[0] = '\0';
    shell.home_all_programs = false;
    shell.selected_home_item = 0U;
    (void)console_shell_handle_buttons(&shell, CONSOLE_BUTTON_DOWN);
    (void)console_shell_handle_buttons(&shell, 0U);
    action = console_shell_handle_buttons(&shell, CONSOLE_BUTTON_ACCEPT);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "GAMES") == 0);

    CHECK(console_shell_init(
        &shell, s_paged_apps,
        sizeof(s_paged_apps) / sizeof(s_paged_apps[0])));
    select_bbs(&shell);
    uint16_t *const transition_pixels = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*transition_pixels));
    CHECK(transition_pixels != NULL);
    uint64_t first_page_hash = 0U;
    if (transition_pixels != NULL) {
        CHECK(console_shell_render_rgb565(
            &shell, transition_pixels, CONSOLE_SHELL_WIDTH));
        first_page_hash = frame_hash(
            transition_pixels,
            (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT);
    }
    CHECK(shell.home_scroll_row == 0U);
    action = tap_surface(&shell, 620U, 408U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.home_scroll_row == 1U);
    CHECK(console_shell_advance(&shell, 32U));
    if (transition_pixels != NULL) {
        CHECK(console_shell_render_rgb565(
            &shell, transition_pixels, CONSOLE_SHELL_WIDTH));
        const uint64_t transition_hash = frame_hash(
            transition_pixels,
            (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT);
        CHECK(transition_hash != first_page_hash);
        uint16_t *const reference_pixels = calloc(
            pixel_count, sizeof(*reference_pixels));
        uint16_t *const reference_scratch = calloc(
            pixel_count, sizeof(*reference_scratch));
        CHECK(reference_pixels != NULL);
        CHECK(reference_scratch != NULL);
        if (reference_pixels != NULL && reference_scratch != NULL) {
            for (size_t target_row = 1U; target_row <= 2U; ++target_row) {
                for (unsigned offset = 1U; offset < 48U;
                     offset += 23U) {
                    render_bbs_reference(
                        &shell, reference_pixels, reference_scratch,
                        target_row, target_row - 1U, offset);
                    shell.home_scroll_row = target_row;
                    shell.home_scroll_visual_q16 = (int32_t)(
                        ((target_row - 1U) << 16) +
                        (offset * (1U << 16)) / 48U);
                    shell.dirty = true;
                    CHECK(console_shell_render_rgb565(
                        &shell, transition_pixels, CONSOLE_SHELL_WIDTH));
                    CHECK(memcmp(transition_pixels, reference_pixels,
                                 pixel_count * sizeof(*reference_pixels)) == 0);
                }
            }
        }
        shell.home_scroll_row = 1U;
        shell.home_scroll_visual_q16 = 1 << 16;
        shell.dirty = true;
        free(reference_pixels);
        free(reference_scratch);
        uint16_t *const repeat_pixels = calloc(
            (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
            sizeof(*repeat_pixels));
        CHECK(repeat_pixels != NULL);
        if (repeat_pixels != NULL) {
            memset(transition_pixels, 0xffff, pixel_count *
                   sizeof(*transition_pixels));
            memset(repeat_pixels, 0x5a, pixel_count *
                   sizeof(*repeat_pixels));
            CHECK(console_shell_render_rgb565(
                &shell, repeat_pixels, CONSOLE_SHELL_WIDTH));
            CHECK(console_shell_render_rgb565(
                &shell, transition_pixels, CONSOLE_SHELL_WIDTH));
            CHECK(memcmp(transition_pixels, repeat_pixels,
                         pixel_count * sizeof(*repeat_pixels)) == 0);
            free(repeat_pixels);
        }
        for (unsigned frame = 0U; frame < 20U; ++frame) {
            (void)console_shell_advance(&shell, 16U);
        }
        CHECK(console_shell_render_rgb565(
            &shell, transition_pixels, CONSOLE_SHELL_WIDTH));
        CHECK(frame_hash(
            transition_pixels,
            (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT) !=
            transition_hash);
    }
    action = tap_surface(&shell, 80U, 408U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.home_scroll_row == 0U);
    free(transition_pixels);

    if (s_failures != 0) {
        fprintf(stderr, "%d BBS shell test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("console shell BBS tests passed");
    return EXIT_SUCCESS;
}
