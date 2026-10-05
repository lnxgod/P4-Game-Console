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
    {.id = 6U, .title = "FILE MANAGER", .subtitle = "P4 GAMES USB",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0xFD20),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH |
         CONSOLE_CAPABILITY_STORAGE,
     .page = CONSOLE_PAGE_FILES, .enabled = true},
    {.id = 7U, .title = "GAME MANAGER", .subtitle = "USB GAMES + OS",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0x5FEA),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH |
         CONSOLE_CAPABILITY_STORAGE,
     .page = CONSOLE_PAGE_GAMES, .enabled = true},
    {.id = 17U, .title = "Battery", .subtitle = "Charge level and power use",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0x07E0),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY,
     .page = CONSOLE_PAGE_POWER, .enabled = true},
    {.id = 18U, .title = "Sensors", .subtitle = "Motion, temperature and clock",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0x07FF),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY,
     .page = CONSOLE_PAGE_SENSORS, .enabled = true},
    {.id = 8U, .title = "MULTIPLAYER", .subtitle = "LOCAL LINK",
     .folder_path = "SYSTEM", .accent_rgb565 = UINT16_C(0xFFE0),
     .capabilities = CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH,
     .page = CONSOLE_PAGE_MULTIPLAYER, .enabled = true},
};

static bool select_page(console_shell_t *shell, const char *name)
{
    if (strncmp(name, "panel", 5) == 0) {
        shell->page = CONSOLE_PAGE_CONTROL_PANEL;
        shell->control_panel_active = true;
        shell->runtime.control_panel_enabled = true;
        shell->control_panel_section = strcmp(name, "panel-preferences") == 0 ? 1U :
            strcmp(name, "panel-storage") == 0 ? 3U : strcmp(name, "panel-advanced") == 0 ? 5U : 0U;
        return true;
    }
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
    if (strcmp(name, "multiplayer") == 0 ||
        strcmp(name, "multiplayer-host") == 0 ||
        strcmp(name, "multiplayer-settings") == 0 ||
        strcmp(name, "multiplayer-dice") == 0 ||
        strcmp(name, "multiplayer-join") == 0) {
        shell->page = CONSOLE_PAGE_MULTIPLAYER;
        shell->active_app_id =
            s_apps[sizeof(s_apps) / sizeof(s_apps[0]) - 1U].id;
        if (strcmp(name, "multiplayer-host") == 0) {
            shell->multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_HOST;
            shell->multiplayer_selected_row =
                CONSOLE_MULTIPLAYER_OPTION_COUNT;
        } else if (strcmp(name, "multiplayer-dice") == 0) {
            shell->runtime.multiplayer_game_is_doom = false;
            shell->runtime.multiplayer_dice_available = true;
            shell->runtime.multiplayer_dice_enabled = true;
            strcpy(shell->runtime.multiplayer_game_title, "P4 YAHTZEE");
            shell->multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS;
            shell->multiplayer_selected_row = CONSOLE_MULTIPLAYER_OPTION_DICE;
        } else if (strcmp(name, "multiplayer-settings") == 0) {
            shell->multiplayer_view =
                CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS;
            shell->multiplayer_selected_row =
                CONSOLE_MULTIPLAYER_OPTION_MODE;
        } else if (strcmp(name, "multiplayer-join") == 0) {
            shell->multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_JOIN;
            shell->multiplayer_selected_row =
                CONSOLE_MULTIPLAYER_OPTION_LOBBY;
        } else {
            shell->multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_ROLE;
            shell->multiplayer_selected_row =
                CONSOLE_MULTIPLAYER_OPTION_COUNT;
        }
        shell->dirty = true;
        return true;
    }
    for (size_t i = 1U; i < sizeof(s_apps) / sizeof(s_apps[0]); ++i) {
        const char *expected = NULL;
        switch (s_apps[i].page) {
        case CONSOLE_PAGE_POWER: expected = "power"; break;
        case CONSOLE_PAGE_SENSORS: expected = "sensors"; break;
        case CONSOLE_PAGE_COLORS: expected = "colors"; break;
        case CONSOLE_PAGE_TOUCH: expected = "touch"; break;
        case CONSOLE_PAGE_SYSTEM: expected = "system"; break;
        case CONSOLE_PAGE_FILES: expected = "files"; break;
        case CONSOLE_PAGE_GAMES: expected = "manager"; break;
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

static bool write_ppm(const char *path,
                      const uint16_t *pixels,
                      size_t width,
                      size_t height,
                      unsigned scale)
{
    FILE *const output = fopen(path, "wb");
    if (output == NULL) {
        return false;
    }
    if (fprintf(output, "P6\n%u %u\n255\n",
                (unsigned)width * scale,
                (unsigned)height * scale) < 0) {
        (void)fclose(output);
        return false;
    }
    for (size_t y = 0U; y < height; ++y) {
        for (unsigned duplicate_y = 0U; duplicate_y < scale; ++duplicate_y) {
            for (size_t x = 0U; x < width; ++x) {
                const uint16_t pixel = pixels[y * width + x];
                const unsigned red5 = (unsigned)((pixel >> 11U) & UINT16_C(0x1F));
                const unsigned green6 = (unsigned)((pixel >> 5U) & UINT16_C(0x3F));
                const unsigned blue5 = (unsigned)(pixel & UINT16_C(0x1F));
                const unsigned red = (red5 * 255U + 15U) / 31U;
                const unsigned green = (green6 * 255U + 31U) / 63U;
                const unsigned blue = (blue5 * 255U + 15U) / 31U;
                for (unsigned duplicate_x = 0U;
                     duplicate_x < scale; ++duplicate_x) {
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
    const bool present = argc == 4 && strcmp(argv[1], "--present") == 0;
    const int page_argument = present ? 2 : 1;
    const int path_argument = present ? 3 : 2;
    if ((!present && argc != 3) || (present && argc != 4)) {
        fprintf(stderr,
                "usage: %s [--present] "
                "home|all|games|arcade|system-folder|"
                "power|sensors|colors|touch|system|files|manager|audio|multiplayer|"
                "multiplayer-host|multiplayer-settings|multiplayer-dice|multiplayer-join "
                "output.ppm\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    console_shell_t shell;
    if (!console_shell_init(
            &shell, s_apps, sizeof(s_apps) / sizeof(s_apps[0])) ||
        !select_page(&shell, argv[page_argument])) {
        fputs("invalid preview page\n", stderr);
        return EXIT_FAILURE;
    }
    const console_shell_runtime_info_t runtime = {
        .uptime_seconds = 3723U,
        .internal_free_kib = 221U,
        .psram_free_kib = 30128U,
        .game_storage_kib = 9052U,
        .game_storage_state = CONSOLE_STORAGE_READY,
        .touch_ready = true,
        .battery_supported = true,
        .battery_sample_valid = true,
        .battery_percent = 63U,
        .battery_millivolts = 7608U,
        .battery_current_valid = true, .battery_milliamps = -340,
        .motion_supported = true, .motion_valid = true,
        .accel_mg = {-11, 28, 997}, .gyro_mdps = {-120, 250, 310},
        .temperature_valid = true, .temperature_millicelsius = 28500,
        .rtc_supported = true, .rtc_valid = true, .rtc_datetime = "2026-10-04 22:30:00",
        .audio_handoff_ready = true,
        .boot_volume_step = 3U,
        .game_volume_step = 3U,
        .audio_settings_persistent = true,
        .doom_wad_ready = true,
        .multiplayer_core_ready = true,
        .multiplayer_transport_ready = true,
        .multiplayer_transport_encrypted = true,
        .multiplayer_transport_kind = 1U,
        .multiplayer_peer_seen = false,
        .multiplayer_lobby_ready = false,
        .multiplayer_lobby_is_host = false,
        .multiplayer_lobby_action_enabled = true,
        .multiplayer_can_start = false,
        .multiplayer_settings_editable = true,
        .multiplayer_game_ready = true,
        .multiplayer_game_is_doom = true,
        .multiplayer_game_selection = 0U,
        .multiplayer_game_count = 3U,
        .multiplayer_game_title = "DOOM",
        .multiplayer_lobby_phase = CONSOLE_MULTIPLAYER_LOBBY_BROWSING,
        .multiplayer_lobby_selection = 1U,
        .multiplayer_lobby_count = 3U,
        .multiplayer_lobby_session_id = UINT32_C(0x00007A3F),
        .multiplayer_lobbies = {
            {
                .session_id = UINT32_C(0x39c572c8),
                .rssi = -42,
                .players_present = 1U,
                .player_capacity = 2U,
                .game_available = true,
                .game_title = "DOOM",
            },
            {
                .session_id = UINT32_C(0xe40d1a67),
                .rssi = -61,
                .players_present = 1U,
                .player_capacity = 2U,
                .game_available = true,
                .game_title = "CHEX QUEST",
            },
            {
                .session_id = UINT32_C(0x104fe291),
                .rssi = -73,
                .players_present = 1U,
                .player_capacity = 2U,
                .game_available = true,
                .game_title = "P4 YAHTZEE",
            },
        },
        .multiplayer_player_slot = 0U,
        .multiplayer_game_mode = 1U,
        .multiplayer_episode = 1U,
        .multiplayer_map = 1U,
        .multiplayer_skill = 5U,
        .multiplayer_no_monsters = true,
    };
    console_shell_set_runtime_info(&shell, &runtime);
    if (shell.page == CONSOLE_PAGE_FILES) {
        const console_shell_file_listing_t files = {
            .entries = {
                {.source_index = 1U, .label = "DOOM1.WAD",
                 .size_kib = 4098U, .removable = true},
                {.source_index = 2U, .label = "README.TXT",
                 .size_kib = 1U, .removable = true},
                {.source_index = 3U, .label = "SAVES",
                 .is_directory = true, .removable = false},
                {.source_index = 4U, .label = "MODPACK.WAD",
                 .size_kib = 512U, .removable = true},
            },
            .entry_count = 4U,
            .total_visible_entries = 4U,
            .hidden_entries = 1U,
            .storage_generation = 3U,
            .revision = 1U,
            .available = true,
        };
        if (!console_shell_set_file_listing(&shell, &files)) {
            return EXIT_FAILURE;
        }
    }
    if (shell.page == CONSOLE_PAGE_GAMES) {
        const console_shell_file_listing_t games = {
            .entries = {
                {.source_index = 0U, .label = "MAZE CHASE 1.0.0",
                 .size_kib = 9U, .removable = true},
                {.source_index = 1U, .label = "SPACE INVADERS 1.0.0",
                 .size_kib = 9U, .removable = true},
                {.source_index = UINT32_MAX, .label = "OS 0.2.0",
                 .size_kib = 889U, .installable = true},
            },
            .entry_count = 3U,
            .total_visible_entries = 3U,
            .storage_generation = 4U,
            .revision = 1U,
            .available = true,
        };
        if (!console_shell_set_file_listing(&shell, &games)) {
            return EXIT_FAILURE;
        }
    }
    if (shell.page == CONSOLE_PAGE_TOUCH) {
        shell.contact_count = 2U;
        shell.contacts[0] = (console_shell_contact_t){.x = 360U, .y = 330U};
        shell.contacts[1] = (console_shell_contact_t){.x = 720U, .y = 450U};
    }

    if (strcmp(argv[page_argument], "multiplayer-dice") == 0) {
        shell.runtime.multiplayer_game_is_doom = false;
        shell.runtime.multiplayer_dice_available = true;
        shell.runtime.multiplayer_dice_enabled = true;
        strcpy(shell.runtime.multiplayer_game_title, "P4 YAHTZEE");
        shell.multiplayer_selected_row = CONSOLE_MULTIPLAYER_OPTION_DICE;
    }
    const size_t width = present
        ? CONSOLE_SHELL_PRESENT_WIDTH : CONSOLE_SHELL_WIDTH;
    const size_t height = present
        ? CONSOLE_SHELL_PRESENT_HEIGHT : CONSOLE_SHELL_HEIGHT;
    uint16_t *const pixels = calloc(width * height, sizeof(*pixels));
    if (pixels == NULL) {
        return EXIT_FAILURE;
    }
    const bool rendered = present
        ? console_shell_render_present_rgb565(&shell, pixels, width)
        : console_shell_render_rgb565(&shell, pixels, width);
    const unsigned output_scale = present &&
        CONSOLE_SHELL_PRESENT_WIDTH * 2U == CONSOLE_SHELL_WIDTH &&
        CONSOLE_SHELL_PRESENT_HEIGHT * 2U == CONSOLE_SHELL_HEIGHT
            ? 2U
            : (CONSOLE_SHELL_WIDTH == CONSOLE_SHELL_LAYOUT_WIDTH ? 2U : 1U);
    const bool written = rendered && write_ppm(
        argv[path_argument], pixels, width, height, output_scale);
    free(pixels);
    return written ? EXIT_SUCCESS : EXIT_FAILURE;
}
