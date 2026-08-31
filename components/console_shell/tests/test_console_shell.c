// SPDX-License-Identifier: MIT

#include "console/shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    APP_DOOM = 1,
    APP_COLORS = 2,
    APP_TOUCH = 3,
    APP_SYSTEM = 4,
    APP_AUDIO = 5,
    APP_FILES = 6,
    APP_GAMES = 7,
    APP_MAZE = 100,
    APP_SPACE = 101,
};

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
        .id = APP_DOOM,
        .title = "DOOM",
        .subtitle = "SHAREWARE 1.9",
        .folder_path = "GAMES/ACTION",
        .accent_rgb565 = UINT16_C(0xF904),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH |
                        CONSOLE_CAPABILITY_AUDIO |
                        CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_EXTERNAL,
        .enabled = true,
    },
    {
        .id = APP_MAZE,
        .title = "MAZE CHASE",
        .subtitle = "ORIGINAL GAME",
        .folder_path = "GAMES/ARCADE",
        .accent_rgb565 = UINT16_C(0x07E0),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH |
                        CONSOLE_CAPABILITY_AUDIO,
        .page = CONSOLE_PAGE_EXTERNAL,
        .enabled = true,
    },
    {
        .id = APP_SPACE,
        .title = "SPACE INVADERS",
        .subtitle = "DEFEND THE P4",
        .folder_path = "GAMES/ARCADE",
        .accent_rgb565 = UINT16_C(0x07FF),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH |
                        CONSOLE_CAPABILITY_AUDIO,
        .page = CONSOLE_PAGE_EXTERNAL,
        .enabled = true,
    },
    {
        .id = APP_COLORS,
        .title = "COLORS",
        .subtitle = "DISPLAY TEST",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x5FFF),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY,
        .page = CONSOLE_PAGE_COLORS,
        .enabled = true,
    },
    {
        .id = APP_TOUCH,
        .title = "TOUCH",
        .subtitle = "GT911 CONTACTS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xFFE0),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH,
        .page = CONSOLE_PAGE_TOUCH,
        .enabled = true,
    },
    {
        .id = APP_SYSTEM,
        .title = "SYSTEM",
        .subtitle = "RTOS STATUS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x5FEA),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH,
        .page = CONSOLE_PAGE_SYSTEM,
        .enabled = true,
    },
    {
        .id = APP_AUDIO,
        .title = "AUDIO",
        .subtitle = "DOOM SOUND PATH",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xF81F),
        .capabilities = CONSOLE_CAPABILITY_AUDIO,
        .page = CONSOLE_PAGE_AUDIO,
        .enabled = true,
    },
    {
        .id = APP_FILES,
        .title = "FILE MANAGER",
        .subtitle = "P4 GAMES USB",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xFD20),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH |
                        CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_FILES,
        .enabled = true,
    },
    {
        .id = APP_GAMES,
        .title = "GAME MANAGER",
        .subtitle = "USB GAMES + OS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x5FEA),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH |
                        CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_GAMES,
        .enabled = true,
    },
};

enum {
    TEST_APP_COUNT = sizeof(s_apps) / sizeof(s_apps[0]),
};

static console_shell_contact_t physical_point(unsigned gui_x, unsigned gui_y)
{
    const console_shell_contact_t point = {
        .x = (uint16_t)(CONSOLE_SHELL_VIEWPORT_LEFT +
                        gui_x * CONSOLE_SHELL_VIEWPORT_WIDTH /
                            CONSOLE_SHELL_LAYOUT_WIDTH),
        .y = (uint16_t)(CONSOLE_SHELL_VIEWPORT_TOP +
                        gui_y * CONSOLE_SHELL_VIEWPORT_HEIGHT /
                            CONSOLE_SHELL_LAYOUT_HEIGHT),
    };
    return point;
}

static console_shell_action_t tap(console_shell_t *shell,
                                  unsigned gui_x,
                                  unsigned gui_y)
{
    const console_shell_contact_t point = physical_point(gui_x, gui_y);
    const console_shell_action_t down =
        console_shell_handle_touch(shell, true, &point, 1U);
    CHECK(down.type == CONSOLE_ACTION_NONE);
    return console_shell_handle_touch(shell, true, NULL, 0U);
}

static console_shell_action_t drag(console_shell_t *shell,
                                   unsigned start_x,
                                   unsigned start_y,
                                   unsigned end_x,
                                   unsigned end_y)
{
    const console_shell_contact_t start = physical_point(start_x, start_y);
    const console_shell_contact_t end = physical_point(end_x, end_y);
    CHECK(console_shell_handle_touch(shell, true, &start, 1U).type ==
          CONSOLE_ACTION_NONE);
    CHECK(console_shell_handle_touch(shell, true, &end, 1U).type ==
          CONSOLE_ACTION_NONE);
    return console_shell_handle_touch(shell, true, NULL, 0U);
}

static console_shell_action_t press_button(console_shell_t *shell,
                                           uint32_t button)
{
    const console_shell_action_t action =
        console_shell_handle_buttons(shell, button);
    CHECK(console_shell_handle_buttons(shell, 0U).type ==
          CONSOLE_ACTION_NONE);
    return action;
}

static void test_registry_validation(void)
{
    console_shell_t shell;
    CHECK(!console_shell_init(NULL, s_apps, TEST_APP_COUNT));
    CHECK(!console_shell_init(&shell, NULL, TEST_APP_COUNT));
    CHECK(!console_shell_init(&shell, s_apps, 0U));
    CHECK(!console_shell_init(
        &shell, s_apps, CONSOLE_SHELL_MAX_APPS + 1U));
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    CHECK(shell.page == CONSOLE_PAGE_HOME);
    CHECK(shell.dirty);
    CHECK(shell.pressed_index == SIZE_MAX);

    console_app_descriptor_t invalid[2] = {s_apps[0], s_apps[1]};
    invalid[1].id = invalid[0].id;
    CHECK(!console_shell_init(&shell, invalid, 2U));
    invalid[1] = s_apps[1];
    invalid[1].id = 0U;
    CHECK(!console_shell_init(&shell, invalid, 2U));
    invalid[1] = s_apps[1];
    invalid[1].page = CONSOLE_PAGE_HOME;
    CHECK(!console_shell_init(&shell, invalid, 2U));
    invalid[1] = s_apps[1];
    invalid[1].capabilities = UINT32_C(0x80000000);
    CHECK(!console_shell_init(&shell, invalid, 2U));
    invalid[1] = s_apps[1];
    invalid[1].title = "1234567890123456";
    CHECK(!console_shell_init(&shell, invalid, 2U));
    invalid[1] = s_apps[1];
    invalid[1].subtitle = NULL;
    CHECK(!console_shell_init(&shell, invalid, 2U));
    invalid[1] = s_apps[1];
    invalid[1].folder_path = NULL;
    CHECK(!console_shell_init(&shell, invalid, 2U));
    invalid[1] = s_apps[1];
    invalid[1].folder_path = "";
    CHECK(console_shell_init(&shell, invalid, 2U));
    invalid[1].folder_path = "/GAMES";
    CHECK(!console_shell_init(&shell, invalid, 2U));
    invalid[1].folder_path = "GAMES//ARCADE";
    CHECK(!console_shell_init(&shell, invalid, 2U));
    invalid[1].folder_path = "GAMES/ARCADE/MAZE";
    CHECK(!console_shell_init(&shell, invalid, 2U));
    invalid[1].folder_path = "games/arcade";
    CHECK(!console_shell_init(&shell, invalid, 2U));
}

static void test_launcher_scrolling(void)
{
    console_app_descriptor_t apps[7];
    for (size_t i = 0U; i < 7U; ++i) {
        apps[i] = s_apps[i % TEST_APP_COUNT];
        apps[i].id = (uint32_t)(100U + i);
    }
    apps[6].title = "PAGE TWO";
    apps[6].subtitle = "SEVENTH APP";

    console_shell_t shell;
    CHECK(console_shell_init(&shell, apps, 7U));
    CHECK(shell.home_scroll_row == 0U);
    CHECK(tap(&shell, 20U, 50U).type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.home_all_programs);
    CHECK(tap(&shell, 304U, 165U).type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.home_scroll_row == 1U);
    console_shell_action_t action = tap(&shell, 20U, 120U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(action.app_id == apps[6].id);

    console_shell_show_home(&shell);
    CHECK(shell.home_scroll_row == 1U);
    CHECK(tap(&shell, 304U, 50U).type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.home_scroll_row == 0U);
    CHECK(tap(&shell, 304U, 50U).type == CONSOLE_ACTION_NONE);

    console_app_descriptor_t more_apps[10];
    for (size_t i = 0U; i < 10U; ++i) {
        more_apps[i] = s_apps[i % TEST_APP_COUNT];
        more_apps[i].id = (uint32_t)(200U + i);
    }
    CHECK(console_shell_init(&shell, more_apps, 10U));
    CHECK(tap(&shell, 20U, 50U).type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.home_all_programs);
    action = drag(&shell, 50U, 130U, 50U, 100U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(action.app_id == 0U);
    CHECK(shell.home_scroll_row == 1U);
    action = drag(&shell, 50U, 80U, 50U, 110U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.home_scroll_row == 0U);
}

static void test_render_bounds_and_stride(void)
{
    enum {
        GUARD = 19,
        STRIDE = CONSOLE_SHELL_WIDTH + 7,
        FRAME_WORDS = STRIDE * CONSOLE_SHELL_HEIGHT,
        TOTAL_WORDS = GUARD + FRAME_WORDS + GUARD,
    };
    uint16_t *const allocation = calloc(TOTAL_WORDS, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation == NULL) {
        return;
    }
    for (size_t i = 0U; i < TOTAL_WORDS; ++i) {
        allocation[i] = UINT16_C(0xA55A);
    }
    uint16_t *const frame = allocation + GUARD;
    for (size_t row = 0U; row < CONSOLE_SHELL_HEIGHT; ++row) {
        for (size_t column = 0U; column < STRIDE; ++column) {
            frame[row * STRIDE + column] = UINT16_C(0xBEEF);
        }
    }

    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    CHECK(!console_shell_render_rgb565(&shell, frame,
                                       CONSOLE_SHELL_WIDTH - 1U));
    CHECK(console_shell_is_dirty(&shell));
    CHECK(console_shell_render_rgb565(&shell, frame, STRIDE));
    CHECK(!console_shell_is_dirty(&shell));
    CHECK(shell.render_generation == 1U);
    CHECK(frame[0] != UINT16_C(0xBEEF));

    for (size_t i = 0U; i < GUARD; ++i) {
        CHECK(allocation[i] == UINT16_C(0xA55A));
        CHECK(allocation[GUARD + FRAME_WORDS + i] == UINT16_C(0xA55A));
    }
    for (size_t row = 0U; row < CONSOLE_SHELL_HEIGHT; ++row) {
        for (size_t column = CONSOLE_SHELL_WIDTH; column < STRIDE; ++column) {
            CHECK(frame[row * STRIDE + column] == UINT16_C(0xBEEF));
        }
    }
    free(allocation);
}

static uint64_t frame_hash(const uint16_t *frame)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t index = 0U;
         index < (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT;
         ++index) {
        hash ^= frame[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void check_frame_hash(uint64_t actual, uint64_t expected,
                             const char *label)
{
    if (actual != expected) {
        fprintf(stderr,
                "FAIL %s frame changed: actual=0x%016llx expected=0x%016llx\n",
                label, (unsigned long long)actual,
                (unsigned long long)expected);
        ++s_failures;
    }
}

static void test_window_manager_visual_contract(void)
{
#if CONSOLE_SHELL_TARGET_WIDTH == 800U && \
    CONSOLE_SHELL_TARGET_HEIGHT == 480U
    const uint64_t desktop_hash = UINT64_C(0xe7cdcfc1289c9ee2);
    const uint64_t elecrow_system_hash = UINT64_C(0x369ef07fbdd629b9);
    const uint64_t olimex_system_hash = UINT64_C(0x4fb45571a91cd3bf);
#else
    const uint64_t desktop_hash = UINT64_C(0x58a00d0a613faf10);
    const uint64_t elecrow_system_hash = UINT64_C(0x53524c0bf4408d3d);
    const uint64_t olimex_system_hash = UINT64_C(0x7ebade9b2ebd4438);
#endif
    uint16_t *const frame = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*frame));
    CHECK(frame != NULL);
    if (frame == NULL) {
        return;
    }
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    CHECK(console_shell_render_rgb565(&shell, frame, CONSOLE_SHELL_WIDTH));
    check_frame_hash(frame_hash(frame), desktop_hash, "desktop");

    shell.page = CONSOLE_PAGE_SYSTEM;
    shell.active_app_id = APP_SYSTEM;
    shell.runtime = (console_shell_runtime_info_t){
        .uptime_seconds = 123U,
        .internal_free_kib = 456U,
        .psram_free_kib = 789U,
        .game_storage_kib = 8192U,
        .game_storage_state = CONSOLE_STORAGE_READY,
        .touch_ready = true,
        .audio_handoff_ready = true,
        .doom_wad_ready = true,
    };
    shell.dirty = true;
    CHECK(console_shell_render_rgb565(&shell, frame, CONSOLE_SHELL_WIDTH));
    check_frame_hash(frame_hash(frame), elecrow_system_hash,
                     "Elecrow system window");

    shell.runtime.touch_ready = false;
    shell.runtime.board_kind = CONSOLE_BOARD_OLIMEX_P4_PC;
    shell.runtime.controller_ready = true;
    shell.runtime.keyboard_ready = true;
    shell.runtime.mouse_ready = true;
    shell.runtime.sd_card_storage = true;
    shell.dirty = true;
    CHECK(console_shell_render_rgb565(&shell, frame, CONSOLE_SHELL_WIDTH));
    check_frame_hash(frame_hash(frame), olimex_system_hash,
                     "Olimex system window");
    free(frame);
}

static void test_color_modes_and_achievements(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    shell.page = CONSOLE_PAGE_COLORS;
    shell.active_app_id = APP_COLORS;
    const console_shell_action_t color =
        press_button(&shell, CONSOLE_BUTTON_RIGHT);
    CHECK(color.type == CONSOLE_ACTION_COLOR_MODE_CHANGED);
    CHECK(color.color_mode == CONSOLE_COLOR_MODE_ARCADE);
    CHECK(console_shell_color_mode(&shell) == CONSOLE_COLOR_MODE_ARCADE);

    p4_achievement_catalog_t achievements;
    p4_achievement_catalog_init(&achievements);
    const p4_game_achievement_t first_care = {
        .game_id = "org.p4console.byte-buddy",
        .id = "first-care",
        .title = "FIRST CARE",
        .description = "GIVE BYTE BUDDY SOME LOVE",
        .unlocked_at_elapsed_ms = 16U,
    };
    CHECK(p4_achievement_catalog_unlock(&achievements, &first_care));
    shell.page = CONSOLE_PAGE_ACHIEVEMENTS;
    shell.dirty = false;
    console_shell_set_achievement_catalog(&shell, &achievements);
    CHECK(shell.achievements.count == 1U);
    CHECK(shell.dirty);

    uint16_t *const frame = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*frame));
    CHECK(frame != NULL);
    if (frame != NULL) {
        CHECK(console_shell_render_rgb565(
            &shell, frame, CONSOLE_SHELL_WIDTH));
        free(frame);
    }
}

static void test_usb_mode_button(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    shell.page = CONSOLE_PAGE_SYSTEM;
    shell.active_app_id = APP_SYSTEM;
    shell.runtime = (console_shell_runtime_info_t){
        .game_storage_state = CONSOLE_STORAGE_READY,
        .usb_storage_supported = true,
    };

    console_shell_action_t action = tap(&shell, 160U, 182U);
    CHECK(action.type == CONSOLE_ACTION_USB_MODE_ENABLE);
    CHECK(action.app_id == APP_SYSTEM);

    shell.runtime.game_storage_state = CONSOLE_STORAGE_USB_HOST;
    shell.runtime.usb_drive_active = true;
    shell.runtime.game_storage_usb_attached = true;
    shell.runtime.usb_storage_eject_safe = false;
    CHECK(tap(&shell, 160U, 182U).type == CONSOLE_ACTION_NONE);
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_NONE);

    shell.runtime.usb_storage_eject_safe = true;
    action = tap(&shell, 160U, 182U);
    CHECK(action.type == CONSOLE_ACTION_USB_MODE_DISABLE);
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_USB_MODE_DISABLE);

    shell.runtime.game_storage_usb_attached = false;
    shell.runtime.usb_storage_eject_safe = false;
    CHECK(tap(&shell, 160U, 182U).type ==
          CONSOLE_ACTION_USB_MODE_DISABLE);

    shell.page = CONSOLE_PAGE_USB_DRIVE;
    shell.active_app_id = 12U;
    shell.runtime.usb_drive_active = false;
    shell.runtime.game_storage_state = CONSOLE_STORAGE_READY;
    action = tap(&shell, 160U, 182U);
    CHECK(action.type == CONSOLE_ACTION_USB_MODE_ENABLE);
    CHECK(action.app_id == 12U);
}

static void test_storage_diagnostics_and_repair_confirmation(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    shell.page = CONSOLE_PAGE_STORAGE;
    shell.active_app_id = APP_SYSTEM;
    shell.runtime = (console_shell_runtime_info_t){
        .game_storage_state = CONSOLE_STORAGE_READY,
        .sd_card_storage = true,
        .game_storage_repair_supported = true,
        .game_storage_card_ready = true,
        .game_storage_filesystem_ready = true,
    };

    console_shell_action_t action = tap(&shell, 50U, 182U);
    CHECK(action.type == CONSOLE_ACTION_STORAGE_CHECK);
    CHECK(tap(&shell, 150U, 182U).type == CONSOLE_ACTION_NONE);

    action = tap(&shell, 255U, 182U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.storage_repair_confirm);
    action = tap(&shell, 255U, 182U);
    CHECK(action.type == CONSOLE_ACTION_STORAGE_REPAIR);
    CHECK(!shell.storage_repair_confirm);

    shell.runtime.game_storage_state = CONSOLE_STORAGE_FAULT;
    action = tap(&shell, 150U, 182U);
    CHECK(action.type == CONSOLE_ACTION_STORAGE_RETRY);
    CHECK(tap(&shell, 50U, 182U).type == CONSOLE_ACTION_NONE);

    action = tap(&shell, 255U, 182U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.storage_repair_confirm);
    action = press_button(&shell, CONSOLE_BUTTON_BACK);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.page == CONSOLE_PAGE_STORAGE);
    CHECK(!shell.storage_repair_confirm);
}

static void test_controller_setup_actions(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    shell.page = CONSOLE_PAGE_CONTROLLERS;
    shell.active_app_id = APP_SYSTEM;
    shell.runtime = (console_shell_runtime_info_t){
        .controller_ready = true,
        .controller_transport = CONSOLE_CONTROLLER_TRANSPORT_BLE_HID,
        .ble_controller_supported = true,
        .ble_controller_enabled = true,
        .ble_controller_host_ready = true,
        .ble_controller_bonded = true,
        .ble_controller_connected = true,
        .ble_controller_encrypted = true,
        .ble_controller_multiplayer_ready = true,
        .controller_mapping_persistent = true,
        .controller_mapping = {0U, 1U, 2U, 3U, 7U, 6U},
        .ble_controller_name = "XBOX WIRELESS",
    };

    CHECK(tap(&shell, 31U, 182U).type ==
          CONSOLE_ACTION_CONTROLLER_BLE_DISABLE);
    CHECK(tap(&shell, 131U, 182U).type ==
          CONSOLE_ACTION_CONTROLLER_DISCONNECT);
    CHECK(tap(&shell, 181U, 182U).type ==
          CONSOLE_ACTION_CONTROLLER_MAPPING_START);
    shell.runtime.controller_mapping_active = true;
    CHECK(tap(&shell, 181U, 182U).type ==
          CONSOLE_ACTION_CONTROLLER_MAPPING_CANCEL);
    shell.runtime.controller_mapping_active = false;
    CHECK(tap(&shell, 231U, 182U).type ==
          CONSOLE_ACTION_CONTROLLER_MAPPING_RESET);
    CHECK(tap(&shell, 285U, 182U).type ==
          CONSOLE_ACTION_CONTROLLER_FORGET);

    shell.runtime.ble_controller_connected = false;
    CHECK(tap(&shell, 81U, 182U).type ==
          CONSOLE_ACTION_CONTROLLER_PAIR);
    shell.runtime.ble_controller_enabled = false;
    CHECK(tap(&shell, 31U, 182U).type ==
          CONSOLE_ACTION_CONTROLLER_BLE_ENABLE);
    CHECK(tap(&shell, 81U, 182U).type == CONSOLE_ACTION_NONE);

    uint16_t *const frame = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*frame));
    CHECK(frame != NULL);
    if (frame != NULL) {
        CHECK(console_shell_render_rgb565(
            &shell, frame, CONSOLE_SHELL_WIDTH));
        free(frame);
    }
}

static void test_multiplayer_start_lockout(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    shell.page = CONSOLE_PAGE_MULTIPLAYER;
    shell.active_app_id = APP_DOOM;
    shell.runtime = (console_shell_runtime_info_t){
        .game_storage_state = CONSOLE_STORAGE_READY,
        .doom_wad_ready = true,
        .multiplayer_lobby_ready = true,
        .multiplayer_lobby_is_host = true,
        .multiplayer_can_start = true,
        .multiplayer_settings_editable = true,
        .multiplayer_game_ready = true,
        .multiplayer_game_is_doom = true,
        .multiplayer_game_selection = 0U,
        .multiplayer_game_count = 3U,
        .multiplayer_game_title = "DOOM",
        .multiplayer_game_mode = 1U,
        .multiplayer_episode = 1U,
        .multiplayer_map = 1U,
        .multiplayer_skill = 5U,
        .multiplayer_no_monsters = true,
    };

    CHECK(shell.multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_ROLE);
    console_shell_action_t action =
        press_button(&shell, CONSOLE_BUTTON_ACCEPT);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST);
    CHECK(shell.multiplayer_selected_row ==
          CONSOLE_MULTIPLAYER_OPTION_COUNT);
    action = press_button(&shell, CONSOLE_BUTTON_ACCEPT);
    CHECK(action.type == CONSOLE_ACTION_MULTIPLAYER_LAUNCH_GAME);
    CHECK(action.app_id == APP_DOOM);
    action = tap(&shell, 160U, 185U);
    CHECK(action.type == CONSOLE_ACTION_MULTIPLAYER_LAUNCH_GAME);

    action = press_button(&shell, CONSOLE_BUTTON_DOWN);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_selected_row ==
          CONSOLE_MULTIPLAYER_OPTION_GAME);
    action = press_button(&shell, CONSOLE_BUTTON_LEFT);
    CHECK(action.type == CONSOLE_ACTION_MULTIPLAYER_CONFIGURE);
    CHECK(action.multiplayer_option ==
          CONSOLE_MULTIPLAYER_OPTION_GAME);
    CHECK(action.multiplayer_delta == -1);
    action = press_button(&shell, CONSOLE_BUTTON_DOWN);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_selected_row >
          CONSOLE_MULTIPLAYER_OPTION_COUNT);
    const size_t settings_row = shell.multiplayer_selected_row;
    console_shell_runtime_info_t refreshed_runtime = shell.runtime;
    refreshed_runtime.multiplayer_game_is_doom = false;
    console_shell_set_runtime_info(&shell, &refreshed_runtime);
    CHECK(shell.multiplayer_selected_row == settings_row);
    refreshed_runtime.multiplayer_game_is_doom = true;
    console_shell_set_runtime_info(&shell, &refreshed_runtime);
    action = press_button(&shell, CONSOLE_BUTTON_ACCEPT);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_view ==
          CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS);
    CHECK(shell.multiplayer_selected_row ==
          CONSOLE_MULTIPLAYER_OPTION_MODE);

    action = press_button(&shell, CONSOLE_BUTTON_BACK);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST);
    CHECK(shell.multiplayer_selected_row ==
          CONSOLE_MULTIPLAYER_OPTION_COUNT);
    action = tap(&shell, 160U, 160U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_view ==
          CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS);
    action = tap(&shell, 280U, 115U);
    CHECK(action.type == CONSOLE_ACTION_MULTIPLAYER_CONFIGURE);
    CHECK(action.multiplayer_option == CONSOLE_MULTIPLAYER_OPTION_MAP);
    CHECK(action.multiplayer_delta == 1);
    action = tap(&shell, 280U, 65U);
    CHECK(action.type == CONSOLE_ACTION_MULTIPLAYER_CONFIGURE);
    CHECK(action.multiplayer_option ==
          CONSOLE_MULTIPLAYER_OPTION_TRANSPORT);
    CHECK(action.multiplayer_delta == 1);
    action = tap(&shell, 280U, 165U);
    CHECK(action.type == CONSOLE_ACTION_MULTIPLAYER_CONFIGURE);
    CHECK(action.multiplayer_option ==
          CONSOLE_MULTIPLAYER_OPTION_TIME_LIMIT);

    shell.runtime.multiplayer_settings_editable = false;
    CHECK(tap(&shell, 280U, 115U).type == CONSOLE_ACTION_NONE);
    shell.runtime.multiplayer_settings_editable = true;

    action = tap(&shell, 20U, 15U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST);
    action = tap(&shell, 160U, 160U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_view ==
          CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS);
    action = tap(&shell, 160U, 185U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST);
    CHECK(shell.multiplayer_selected_row ==
          CONSOLE_MULTIPLAYER_OPTION_COUNT);

    shell.runtime.multiplayer_launch_syncing = true;
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_NONE);
    CHECK(tap(&shell, 160U, 185U).type == CONSOLE_ACTION_NONE);

    shell.runtime.multiplayer_launch_syncing = false;
    shell.runtime.multiplayer_can_start = false;
    shell.runtime.multiplayer_lobby_ready = false;
    shell.runtime.multiplayer_lobby_action_enabled = true;
    shell.runtime.multiplayer_lobby_phase =
        CONSOLE_MULTIPLAYER_LOBBY_BROWSING;
    shell.runtime.multiplayer_lobby_scanning = true;
    shell.runtime.multiplayer_lobby_action_enabled = false;
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_NONE);
    shell.runtime.multiplayer_lobby_scanning = false;
    shell.runtime.multiplayer_lobby_action_enabled = true;
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_MULTIPLAYER_CREATE_LOBBY);
    CHECK(tap(&shell, 160U, 185U).type ==
          CONSOLE_ACTION_MULTIPLAYER_CREATE_LOBBY);

    action = press_button(&shell, CONSOLE_BUTTON_BACK);
    CHECK(action.type == CONSOLE_ACTION_MULTIPLAYER_LOBBY_RESET);
    CHECK(shell.multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_ROLE);
    shell.runtime.multiplayer_transport_kind = 1U;
    shell.runtime.multiplayer_lobby_count = 2U;
    shell.runtime.multiplayer_lobby_selection = 1U;
    shell.runtime.multiplayer_lobbies[0] =
        (console_multiplayer_lobby_display_t){
            .session_id = UINT32_C(0x39c572c8),
            .rssi = -42,
            .players_present = 1U,
            .player_capacity = 2U,
            .game_available = true,
            .game_title = "DOOM",
        };
    shell.runtime.multiplayer_lobbies[1] =
        (console_multiplayer_lobby_display_t){
            .session_id = UINT32_C(0x8a104f21),
            .rssi = -61,
            .players_present = 1U,
            .player_capacity = 2U,
            .game_available = true,
            .game_title = "CHEX QUEST",
        };
    action = press_button(&shell, CONSOLE_BUTTON_RIGHT);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_role_selection == 1U);
    action = press_button(&shell, CONSOLE_BUTTON_ACCEPT);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_JOIN);
    CHECK(shell.multiplayer_selected_row ==
          CONSOLE_MULTIPLAYER_OPTION_LOBBY);
    action = press_button(&shell, CONSOLE_BUTTON_RIGHT);
    CHECK(action.type == CONSOLE_ACTION_MULTIPLAYER_CONFIGURE);
    CHECK(action.multiplayer_option == CONSOLE_MULTIPLAYER_OPTION_LOBBY);
    CHECK(action.multiplayer_delta == 1);
    action = tap(&shell, 160U, 90U);
    CHECK(action.type == CONSOLE_ACTION_MULTIPLAYER_LOBBY_SELECT);
    CHECK(action.multiplayer_lobby_selection == 1U);
    shell.runtime.multiplayer_lobby_selection = 1U;
    action = tap(&shell, 160U, 185U);
    CHECK(action.type == CONSOLE_ACTION_MULTIPLAYER_JOIN_LOBBY);
    action = tap(&shell, 280U, 79U);
    CHECK(action.type == CONSOLE_ACTION_NONE);

    shell.runtime.multiplayer_game_is_doom = false;
    shell.runtime.multiplayer_game_selection = 2U;
    memcpy(shell.runtime.multiplayer_game_title, "P4 YAHTZEE",
           sizeof("P4 YAHTZEE"));
    shell.multiplayer_selected_row = CONSOLE_MULTIPLAYER_OPTION_LOBBY;
    action = press_button(&shell, CONSOLE_BUTTON_DOWN);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.multiplayer_selected_row ==
          CONSOLE_MULTIPLAYER_OPTION_COUNT);

    shell.runtime.multiplayer_lobby_action_enabled = false;
    shell.runtime.doom_wad_ready = false;
    shell.runtime.multiplayer_game_ready = false;
    shell.runtime.game_storage_state = CONSOLE_STORAGE_FAULT;
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_NONE);
    CHECK(tap(&shell, 160U, 185U).type == CONSOLE_ACTION_NONE);

    uint16_t *const frame = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*frame));
    CHECK(frame != NULL);
    if (frame != NULL) {
        shell.multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_ROLE;
        CHECK(console_shell_render_rgb565(
            &shell, frame, CONSOLE_SHELL_WIDTH));
        shell.multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_HOST;
        CHECK(console_shell_render_rgb565(
            &shell, frame, CONSOLE_SHELL_WIDTH));
        shell.multiplayer_view =
            CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS;
        CHECK(console_shell_render_rgb565(
            &shell, frame, CONSOLE_SHELL_WIDTH));
        shell.multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_JOIN;
        CHECK(console_shell_render_rgb565(
            &shell, frame, CONSOLE_SHELL_WIDTH));
        free(frame);
    }
}

static void test_desktop_pages(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));

    p4_file_list_t files;
    p4_file_list_init(&files);
    CHECK(p4_file_list_add(&files, "GAMES", 0U,
                           P4_FILE_KIND_FOLDER, true));
    CHECK(p4_file_list_add(&files, "GAMES/MAZE.P4G", 4097U,
                           P4_FILE_KIND_CARTRIDGE, true));
    p4_file_list_set_sort(&files, P4_FILE_SORT_SIZE, true);
    CHECK(console_shell_set_file_list(&shell, &files));
    CHECK(shell.desktop_files.sort == P4_FILE_SORT_SIZE);
    CHECK(shell.files.entry_count == 2U);
    CHECK(shell.files.entries[0].size_kib == 5U);

    p4_save_catalog_t saves;
    p4_save_catalog_init(&saves, false);
    CHECK(p4_save_catalog_add(
        &saves, "ORG.P4CONSOLE.SOLITAIRE", "AUTO", 512U, 1U));
    CHECK(console_shell_set_save_catalog(&shell, &saves));
    CHECK(shell.saves.count == 1U);

    shell.page = CONSOLE_PAGE_TERMINAL;
    shell.active_app_id = 11U;
    CHECK(console_shell_handle_text_key(&shell, 'h'));
    CHECK(console_shell_handle_text_key(&shell, 'e'));
    CHECK(console_shell_handle_text_key(&shell, 'l'));
    CHECK(console_shell_handle_text_key(&shell, 'p'));
    CHECK(console_shell_handle_text_key(&shell, '\n'));
    CHECK(shell.terminal.line_count > 2U);
    CHECK(strstr(shell.terminal.lines[shell.terminal.line_count - 1U],
                 "STATUS") != NULL);
    CHECK(tap(&shell, 10U, 112U).type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.terminal.input, "Q") == 0);

    uint16_t *const frame = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*frame));
    CHECK(frame != NULL);
    if (frame != NULL) {
        CHECK(console_shell_render_rgb565(
            &shell, frame, CONSOLE_SHELL_WIDTH));
        shell.page = CONSOLE_PAGE_SAVES;
        CHECK(console_shell_render_rgb565(
            &shell, frame, CONSOLE_SHELL_WIDTH));
        shell.page = CONSOLE_PAGE_MULTIPLAYER;
        CHECK(console_shell_render_rgb565(
            &shell, frame, CONSOLE_SHELL_WIDTH));
        shell.page = CONSOLE_PAGE_USB_DRIVE;
        CHECK(console_shell_render_rgb565(
            &shell, frame, CONSOLE_SHELL_WIDTH));
        free(frame);
    }
}

static void test_navigation_and_launch(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));

    /* Root is a bounded synthetic view: All Programs, Games, System. */
    console_shell_action_t action = tap(&shell, 120U, 50U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "GAMES") == 0);
    CHECK(!shell.home_all_programs);

    /* Games contains type folders; Arcade contains the two native games. */
    action = tap(&shell, 120U, 50U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "GAMES/ARCADE") == 0);
    action = tap(&shell, 20U, 50U);
    CHECK(action.type == CONSOLE_ACTION_LAUNCH);
    CHECK(action.app_id == APP_MAZE);
    CHECK(shell.page == CONSOLE_PAGE_HOME);

    action = tap(&shell, 20U, 31U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "GAMES") == 0);
    action = tap(&shell, 20U, 31U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.home_folder_path[0] == '\0');

    /* A built-in page returns to the System folder, not the root. */
    action = tap(&shell, 220U, 50U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "SYSTEM") == 0);
    action = tap(&shell, 20U, 50U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(action.app_id == APP_COLORS);
    CHECK(shell.page == CONSOLE_PAGE_COLORS);
    action = tap(&shell, 10U, 10U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.page == CONSOLE_PAGE_HOME);
    CHECK(strcmp(shell.home_folder_path, "SYSTEM") == 0);
}

static void test_system_cartridge_folders(void)
{
    static const console_app_descriptor_t system_apps[] = {
        {
            .id = 109U,
            .title = "CALCULATOR",
            .subtitle = "INTEGER DESK CALC",
            .folder_path = "SYSTEM/TOOLS",
            .accent_rgb565 = UINT16_C(0x07FF),
            .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                            CONSOLE_CAPABILITY_TOUCH,
            .page = CONSOLE_PAGE_EXTERNAL,
            .enabled = true,
        },
        {
            .id = 110U,
            .title = "INPUT TEST",
            .subtitle = "BUTTONS + TOUCH",
            .folder_path = "SYSTEM/TESTS",
            .accent_rgb565 = UINT16_C(0xFFE0),
            .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                            CONSOLE_CAPABILITY_TOUCH,
            .page = CONSOLE_PAGE_EXTERNAL,
            .enabled = true,
        },
        {
            .id = 111U,
            .title = "AV TEST",
            .subtitle = "VIDEO + TONES",
            .folder_path = "SYSTEM/TESTS",
            .accent_rgb565 = UINT16_C(0xF81F),
            .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                            CONSOLE_CAPABILITY_TOUCH |
                            CONSOLE_CAPABILITY_AUDIO,
            .page = CONSOLE_PAGE_EXTERNAL,
            .enabled = true,
        },
    };
    console_shell_t shell;
    CHECK(console_shell_init(
        &shell, system_apps,
        sizeof(system_apps) / sizeof(system_apps[0])));

    /* Root: All Programs, then System. */
    CHECK(press_button(&shell, CONSOLE_BUTTON_RIGHT).type ==
          CONSOLE_ACTION_NONE);
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "SYSTEM") == 0);

    /* Child folders retain manifest order: Tools, then Tests. */
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "SYSTEM/TOOLS") == 0);
    console_shell_action_t action =
        press_button(&shell, CONSOLE_BUTTON_ACCEPT);
    CHECK(action.type == CONSOLE_ACTION_LAUNCH);
    CHECK(action.app_id == 109U);

    CHECK(press_button(&shell, CONSOLE_BUTTON_BACK).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "SYSTEM") == 0);
    CHECK(press_button(&shell, CONSOLE_BUTTON_RIGHT).type ==
          CONSOLE_ACTION_NONE);
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "SYSTEM/TESTS") == 0);
    action = press_button(&shell, CONSOLE_BUTTON_ACCEPT);
    CHECK(action.type == CONSOLE_ACTION_LAUNCH);
    CHECK(action.app_id == 110U);
}

static void test_controller_navigation(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));

    /* The root controller focus starts on All Programs. */
    CHECK(press_button(&shell, CONSOLE_BUTTON_RIGHT).type ==
          CONSOLE_ACTION_NONE);
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "GAMES") == 0);

    CHECK(press_button(&shell, CONSOLE_BUTTON_RIGHT).type ==
          CONSOLE_ACTION_NONE);
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "GAMES/ARCADE") == 0);
    console_shell_action_t action = press_button(&shell,
                                                  CONSOLE_BUTTON_ACCEPT);
    CHECK(action.type == CONSOLE_ACTION_LAUNCH);
    CHECK(action.app_id == APP_MAZE);

    CHECK(press_button(&shell, CONSOLE_BUTTON_BACK).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "GAMES") == 0);
    CHECK(press_button(&shell, CONSOLE_BUTTON_BACK).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.home_folder_path[0] == '\0');

    /* Held buttons are edge-triggered, and unknown bits are ignored. */
    action = console_shell_handle_buttons(
        &shell, CONSOLE_BUTTON_RIGHT | UINT32_C(0xffff0000));
    CHECK(action.type == CONSOLE_ACTION_NONE);
    const size_t selected = shell.selected_home_item;
    CHECK(console_shell_handle_buttons(
              &shell, CONSOLE_BUTTON_RIGHT | UINT32_C(0xffff0000)).type ==
          CONSOLE_ACTION_NONE);
    CHECK(shell.selected_home_item == selected);
    CHECK(console_shell_handle_buttons(&shell, 0U).type ==
          CONSOLE_ACTION_NONE);
}

static void test_fail_closed_gestures(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    const console_shell_contact_t doom = physical_point(20U, 50U);
    const console_shell_contact_t colors = physical_point(120U, 50U);
    console_shell_contact_t pair[2] = {doom, colors};

    CHECK(console_shell_handle_touch(&shell, true, &doom, 1U).type ==
          CONSOLE_ACTION_NONE);
    CHECK(console_shell_handle_touch(&shell, true, &colors, 1U).type ==
          CONSOLE_ACTION_NONE);
    CHECK(console_shell_handle_touch(&shell, true, NULL, 0U).type ==
          CONSOLE_ACTION_NONE);

    CHECK(console_shell_handle_touch(&shell, true, &doom, 1U).type ==
          CONSOLE_ACTION_NONE);
    CHECK(console_shell_handle_touch(&shell, true, pair, 2U).type ==
          CONSOLE_ACTION_NONE);
    CHECK(console_shell_handle_touch(&shell, true, NULL, 0U).type ==
          CONSOLE_ACTION_NONE);

    CHECK(console_shell_handle_touch(&shell, true, &doom, 1U).type ==
          CONSOLE_ACTION_NONE);
    CHECK(console_shell_handle_touch(&shell, false, NULL, 0U).type ==
          CONSOLE_ACTION_NONE);
    /* A contact still present after an invalid frame cannot become a tap. */
    CHECK(console_shell_handle_touch(&shell, true, &doom, 1U).type ==
          CONSOLE_ACTION_NONE);
    CHECK(console_shell_handle_touch(&shell, true, NULL, 0U).type ==
          CONSOLE_ACTION_NONE);

    const console_shell_contact_t margin = {.x = 10U, .y = 100U};
    CHECK(console_shell_handle_touch(&shell, true, &margin, 1U).type ==
          CONSOLE_ACTION_NONE);
    CHECK(console_shell_handle_touch(&shell, true, NULL, 0U).type ==
          CONSOLE_ACTION_NONE);

    CHECK(console_shell_handle_touch(&shell, true, pair, 6U).type ==
          CONSOLE_ACTION_NONE);
    CHECK(!shell.press_active);
}

static void test_touch_page_and_runtime(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    CHECK(tap(&shell, 220U, 50U).type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(tap(&shell, 120U, 50U).app_id == APP_TOUCH);

    console_shell_contact_t contacts[CONSOLE_SHELL_MAX_CONTACTS];
    for (size_t i = 0U; i < CONSOLE_SHELL_MAX_CONTACTS; ++i) {
        contacts[i] = physical_point(80U + (unsigned)i * 20U,
                                     80U + (unsigned)i * 10U);
    }
    shell.dirty = false;
    CHECK(console_shell_handle_touch(&shell, true, contacts,
                                     CONSOLE_SHELL_MAX_CONTACTS).type ==
          CONSOLE_ACTION_NONE);
    CHECK(shell.contact_count == CONSOLE_SHELL_MAX_CONTACTS);
    CHECK(shell.dirty);

    uint16_t *const frame = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*frame));
    CHECK(frame != NULL);
    if (frame != NULL) {
        CHECK(console_shell_render_rgb565(&shell, frame,
                                           CONSOLE_SHELL_WIDTH));
        free(frame);
    }

    console_shell_show_home(&shell);
    shell.dirty = false;
    const console_shell_runtime_info_t runtime = {
        .uptime_seconds = 123U,
        .internal_free_kib = 456U,
        .psram_free_kib = 789U,
        .game_storage_kib = 8192U,
        .game_storage_state = CONSOLE_STORAGE_READY,
        .touch_ready = true,
        .controller_ready = false,
        .keyboard_ready = false,
        .mouse_ready = false,
        .sd_card_storage = false,
        .audio_handoff_ready = true,
        .game_storage_usb_attached = false,
        .doom_wad_ready = true,
    };
    console_shell_set_runtime_info(&shell, &runtime);
    CHECK(shell.dirty);
    CHECK(shell.runtime.uptime_seconds == 123U);
    shell.dirty = false;

    CHECK(tap(&shell, 220U, 50U).app_id == APP_SYSTEM);
    shell.dirty = false;
    console_shell_set_runtime_info(&shell, &runtime);
    CHECK(!shell.dirty);
    console_shell_runtime_info_t changed = runtime;
    changed.uptime_seconds = 124U;
    console_shell_set_runtime_info(&shell, &changed);
    CHECK(shell.dirty);

    console_shell_show_home(&shell);
    shell.dirty = false;
    changed.game_storage_state = CONSOLE_STORAGE_USB_HOST;
    changed.game_storage_usb_attached = true;
    changed.doom_wad_ready = false;
    console_shell_set_runtime_info(&shell, &changed);
    CHECK(shell.dirty);

    shell.dirty = false;
    console_shell_set_pointer(&shell, true, 400U, 300U, false);
    CHECK(shell.pointer_visible);
    CHECK(shell.pointer_x == CONSOLE_SHELL_LAYOUT_WIDTH - 1U);
    CHECK(shell.pointer_y == CONSOLE_SHELL_LAYOUT_HEIGHT - 1U);
    CHECK(shell.dirty);
    shell.dirty = false;
    console_shell_set_pointer(&shell, false, 0U, 0U, false);
    CHECK(!shell.pointer_visible);
    CHECK(shell.dirty);
}

static void test_file_manager(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    CHECK(tap(&shell, 220U, 50U).type == CONSOLE_ACTION_PAGE_CHANGED);
    console_shell_action_t action = tap(&shell, 120U, 120U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(action.app_id == APP_FILES);
    CHECK(shell.page == CONSOLE_PAGE_FILES);

    console_shell_file_listing_t listing = {
        .entry_count = 7U,
        .total_visible_entries = 7U,
        .hidden_entries = 1U,
        .omitted_entries = 0U,
        .storage_generation = 3U,
        .revision = 1U,
        .path_label = "SD:/",
        .available = true,
    };
    for (size_t i = 0U; i < listing.entry_count; ++i) {
        listing.entries[i].source_index = (uint32_t)(10U + i);
        (void)snprintf(listing.entries[i].label,
                       sizeof(listing.entries[i].label),
                       "FILE%u.WAD", (unsigned)i);
        listing.entries[i].size_kib = (uint32_t)(100U + i);
        listing.entries[i].removable = true;
    }
    (void)strcpy(listing.entries[1].label, "SAVES");
    listing.entries[1].is_directory = true;
    listing.entries[1].removable = false;
    CHECK(console_shell_set_file_listing(&shell, &listing));
    CHECK(shell.file_selected_index == 0U);
    CHECK(shell.file_first_visible == 0U);

    uint16_t *const frame = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*frame));
    CHECK(frame != NULL);
    if (frame != NULL) {
        CHECK(console_shell_render_rgb565(&shell, frame,
                                           CONSOLE_SHELL_WIDTH));
        free(frame);
    }

    action = tap(&shell, 250U, 180U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.file_delete_confirm);
    action = tap(&shell, 160U, 180U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(!shell.file_delete_confirm);

    CHECK(tap(&shell, 250U, 180U).type == CONSOLE_ACTION_PAGE_CHANGED);
    action = tap(&shell, 250U, 180U);
    CHECK(action.type == CONSOLE_ACTION_FILE_DELETE);
    CHECK(action.file_source_index == 10U);
    CHECK(!shell.file_delete_confirm);

    action = tap(&shell, 50U, 72U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.file_selected_index == 1U);
    action = tap(&shell, 250U, 180U);
    CHECK(action.type == CONSOLE_ACTION_FILE_OPEN);
    CHECK(action.file_source_index == 11U);

    (void)strcpy(listing.path_label, "SD:/SAVES");
    listing.can_go_up = true;
    ++listing.revision;
    CHECK(console_shell_set_file_listing(&shell, &listing));
    action = press_button(&shell, CONSOLE_BUTTON_BACK);
    CHECK(action.type == CONSOLE_ACTION_FILE_UP);
    CHECK(shell.page == CONSOLE_PAGE_FILES);
    (void)strcpy(listing.path_label, "SD:/");
    listing.can_go_up = false;
    ++listing.revision;
    CHECK(console_shell_set_file_listing(&shell, &listing));

    action = tap(&shell, 80U, 180U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.file_first_visible == 5U);
    CHECK(shell.file_selected_index == 5U);
    CHECK(tap(&shell, 250U, 180U).type == CONSOLE_ACTION_PAGE_CHANGED);
    action = tap(&shell, 250U, 180U);
    CHECK(action.type == CONSOLE_ACTION_FILE_DELETE);
    CHECK(action.file_source_index == 15U);

    action = tap(&shell, 160U, 180U);
    CHECK(action.type == CONSOLE_ACTION_FILE_REFRESH);
    CHECK(action.file_source_index == UINT32_MAX);

    console_shell_set_file_notice(&shell, CONSOLE_FILE_NOTICE_DELETED);
    CHECK(shell.file_notice == CONSOLE_FILE_NOTICE_DELETED);
    listing.entry_count = 0U;
    listing.total_visible_entries = 0U;
    listing.available = false;
    ++listing.revision;
    CHECK(console_shell_set_file_listing(&shell, &listing));
    CHECK(shell.file_first_visible == 0U);
    CHECK(shell.file_selected_index == 0U);
    CHECK(tap(&shell, 160U, 180U).type == CONSOLE_ACTION_FILE_REFRESH);

    console_shell_file_listing_t invalid = listing;
    invalid.entry_count = CONSOLE_SHELL_FILE_MAX_ENTRIES + 1U;
    CHECK(!console_shell_set_file_listing(&shell, &invalid));
    invalid = (console_shell_file_listing_t){
        .entry_count = 1U,
        .total_visible_entries = 1U,
        .available = true,
    };
    CHECK(!console_shell_set_file_listing(&shell, &invalid));
    (void)strcpy(invalid.entries[0].label, "FOLDER");
    invalid.entries[0].is_directory = true;
    invalid.entries[0].removable = true;
    CHECK(!console_shell_set_file_listing(&shell, &invalid));
}

static void test_game_manager(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    CHECK(tap(&shell, 220U, 50U).type == CONSOLE_ACTION_PAGE_CHANGED);
    console_shell_action_t action = tap(&shell, 220U, 120U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(action.app_id == APP_GAMES);
    CHECK(shell.page == CONSOLE_PAGE_GAMES);

    console_shell_file_listing_t listing = {
        .entry_count = 2U,
        .total_visible_entries = 2U,
        .storage_generation = 4U,
        .revision = 1U,
        .available = true,
    };
    listing.entries[0].source_index = 3U;
    (void)strcpy(listing.entries[0].label, "MAZE CHASE 1.0.0");
    listing.entries[0].size_kib = 9U;
    listing.entries[0].removable = true;
    listing.entries[1].source_index = UINT32_MAX;
    (void)strcpy(listing.entries[1].label, "OS 0.2.0");
    listing.entries[1].size_kib = 889U;
    listing.entries[1].installable = true;
    CHECK(console_shell_set_file_listing(&shell, &listing));

    CHECK(tap(&shell, 250U, 180U).type == CONSOLE_ACTION_PAGE_CHANGED);
    action = tap(&shell, 250U, 180U);
    CHECK(action.type == CONSOLE_ACTION_GAME_REMOVE);
    CHECK(action.file_source_index == 3U);

    action = tap(&shell, 50U, 72U);
    CHECK(action.type == CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.file_selected_index == 1U);
    CHECK(tap(&shell, 250U, 180U).type == CONSOLE_ACTION_PAGE_CHANGED);
    action = tap(&shell, 250U, 180U);
    CHECK(action.type == CONSOLE_ACTION_OS_UPDATE_INSTALL);
    CHECK(action.file_source_index == UINT32_MAX);
    action = tap(&shell, 160U, 180U);
    CHECK(action.type == CONSOLE_ACTION_GAME_REFRESH);

    listing.entries[0].installable = true;
    CHECK(!console_shell_set_file_listing(&shell, &listing));
}

static void test_controller_game_manager(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));

    /* Root -> System -> Game Manager using only normalized buttons. */
    CHECK(press_button(&shell, CONSOLE_BUTTON_RIGHT).type ==
          CONSOLE_ACTION_NONE);
    CHECK(press_button(&shell, CONSOLE_BUTTON_RIGHT).type ==
          CONSOLE_ACTION_NONE);
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(strcmp(shell.home_folder_path, "SYSTEM") == 0);
    (void)press_button(&shell, CONSOLE_BUTTON_DOWN);
    (void)press_button(&shell, CONSOLE_BUTTON_RIGHT);
    (void)press_button(&shell, CONSOLE_BUTTON_RIGHT);
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.page == CONSOLE_PAGE_GAMES);

    console_shell_file_listing_t listing = {
        .entry_count = 7U,
        .total_visible_entries = 7U,
        .storage_generation = 8U,
        .revision = 1U,
        .available = true,
    };
    for (size_t i = 0U; i < listing.entry_count; ++i) {
        listing.entries[i].source_index = (uint32_t)(20U + i);
        (void)snprintf(listing.entries[i].label,
                       sizeof(listing.entries[i].label),
                       "GAME %u", (unsigned)i);
        listing.entries[i].removable = true;
    }
    CHECK(console_shell_set_file_listing(&shell, &listing));

    for (unsigned i = 0U; i < 6U; ++i) {
        (void)press_button(&shell, CONSOLE_BUTTON_DOWN);
    }
    CHECK(shell.file_selected_index == 6U);
    CHECK(shell.file_first_visible == 5U);
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.file_delete_confirm);
    CHECK(press_button(&shell, CONSOLE_BUTTON_BACK).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(!shell.file_delete_confirm);
    CHECK(press_button(&shell, CONSOLE_BUTTON_ACCEPT).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    console_shell_action_t action =
        press_button(&shell, CONSOLE_BUTTON_ACCEPT);
    CHECK(action.type == CONSOLE_ACTION_GAME_REMOVE);
    CHECK(action.file_source_index == 26U);

    action = press_button(&shell, CONSOLE_BUTTON_REFRESH);
    CHECK(action.type == CONSOLE_ACTION_GAME_REFRESH);
    CHECK(action.file_source_index == UINT32_MAX);
    CHECK(press_button(&shell, CONSOLE_BUTTON_BACK).type ==
          CONSOLE_ACTION_PAGE_CHANGED);
    CHECK(shell.page == CONSOLE_PAGE_HOME);
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void test_input_fuzz(void)
{
    console_shell_t shell;
    CHECK(console_shell_init(&shell, s_apps, TEST_APP_COUNT));
    uint16_t *const frame = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*frame));
    CHECK(frame != NULL);
    if (frame == NULL) {
        return;
    }

    uint32_t state = UINT32_C(0xC001D00D);
    console_shell_contact_t contacts[CONSOLE_SHELL_MAX_CONTACTS];
    for (size_t iteration = 0U; iteration < 2000U; ++iteration) {
        for (size_t i = 0U; i < CONSOLE_SHELL_MAX_CONTACTS; ++i) {
            contacts[i].x = (uint16_t)next_random(&state);
            contacts[i].y = (uint16_t)next_random(&state);
        }
        const size_t count = (size_t)(next_random(&state) % 7U);
        const bool valid = (next_random(&state) & 3U) != 0U;
        const console_shell_contact_t *const pointer =
            (next_random(&state) & 7U) == 0U ? NULL : contacts;
        (void)console_shell_handle_touch(&shell, valid, pointer, count);
        (void)console_shell_handle_buttons(&shell, next_random(&state));
        if ((iteration & 7U) == 0U) {
            (void)console_shell_handle_buttons(&shell, 0U);
        }
        if (console_shell_is_dirty(&shell)) {
            CHECK(console_shell_render_rgb565(&shell, frame,
                                               CONSOLE_SHELL_WIDTH));
        }
    }
    free(frame);
}

int main(void)
{
    test_registry_validation();
    test_render_bounds_and_stride();
    test_window_manager_visual_contract();
    test_color_modes_and_achievements();
    test_usb_mode_button();
    test_storage_diagnostics_and_repair_confirmation();
    test_controller_setup_actions();
    test_multiplayer_start_lockout();
    test_desktop_pages();
    test_navigation_and_launch();
    test_system_cartridge_folders();
    test_controller_navigation();
    test_launcher_scrolling();
    test_fail_closed_gestures();
    test_touch_page_and_runtime();
    test_file_manager();
    test_game_manager();
    test_controller_game_manager();
    test_input_fuzz();
    if (s_failures != 0) {
        fprintf(stderr, "%d console shell test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("console shell tests passed");
    return EXIT_SUCCESS;
}
