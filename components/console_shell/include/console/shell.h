// SPDX-License-Identifier: MIT

#ifndef P4_CONSOLE_SHELL_H
#define P4_CONSOLE_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/achievements.h"
#include "p4/desktop.h"

#if defined(ESP_PLATFORM) && !defined(CONSOLE_SHELL_TARGET_WIDTH)
#include "sdkconfig.h"
#include "platform/board.h"
#define CONSOLE_SHELL_TARGET_WIDTH PLATFORM_BOARD_DISPLAY_WIDTH
#define CONSOLE_SHELL_TARGET_HEIGHT PLATFORM_BOARD_DISPLAY_HEIGHT
#elif !defined(CONSOLE_SHELL_TARGET_WIDTH)
#define CONSOLE_SHELL_TARGET_WIDTH 1024U
#define CONSOLE_SHELL_TARGET_HEIGHT 600U
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CONSOLE_SHELL_WIDTH = 320,
    CONSOLE_SHELL_HEIGHT = 200,
    CONSOLE_SHELL_PHYSICAL_WIDTH = CONSOLE_SHELL_TARGET_WIDTH,
    CONSOLE_SHELL_PHYSICAL_HEIGHT = CONSOLE_SHELL_TARGET_HEIGHT,
#if defined(ESP_PLATFORM)
    CONSOLE_SHELL_VIEWPORT_WIDTH =
        PLATFORM_BOARD_GAME_VIEWPORT_WIDTH,
    CONSOLE_SHELL_VIEWPORT_HEIGHT =
        PLATFORM_BOARD_GAME_VIEWPORT_HEIGHT,
#else
    CONSOLE_SHELL_VIEWPORT_WIDTH =
        CONSOLE_SHELL_PHYSICAL_WIDTH * CONSOLE_SHELL_HEIGHT <=
                CONSOLE_SHELL_PHYSICAL_HEIGHT * CONSOLE_SHELL_WIDTH
            ? CONSOLE_SHELL_PHYSICAL_WIDTH
            : CONSOLE_SHELL_PHYSICAL_HEIGHT * CONSOLE_SHELL_WIDTH /
                CONSOLE_SHELL_HEIGHT,
    CONSOLE_SHELL_VIEWPORT_HEIGHT =
        CONSOLE_SHELL_PHYSICAL_WIDTH * CONSOLE_SHELL_HEIGHT <=
                CONSOLE_SHELL_PHYSICAL_HEIGHT * CONSOLE_SHELL_WIDTH
            ? CONSOLE_SHELL_PHYSICAL_WIDTH * CONSOLE_SHELL_HEIGHT /
                CONSOLE_SHELL_WIDTH
            : CONSOLE_SHELL_PHYSICAL_HEIGHT,
#endif
    CONSOLE_SHELL_VIEWPORT_LEFT =
        (CONSOLE_SHELL_PHYSICAL_WIDTH - CONSOLE_SHELL_VIEWPORT_WIDTH) / 2,
    CONSOLE_SHELL_VIEWPORT_TOP =
        (CONSOLE_SHELL_PHYSICAL_HEIGHT - CONSOLE_SHELL_VIEWPORT_HEIGHT) / 2,
    CONSOLE_SHELL_APP_COLUMNS = 3,
    CONSOLE_SHELL_VISIBLE_APP_ROWS = 2,
    CONSOLE_SHELL_APPS_PER_VIEW =
        CONSOLE_SHELL_APP_COLUMNS * CONSOLE_SHELL_VISIBLE_APP_ROWS,
    CONSOLE_SHELL_MAX_APPS = 32,
    CONSOLE_SHELL_MAX_CONTACTS = 5,
    CONSOLE_SHELL_TITLE_MAX_BYTES = 16,
    CONSOLE_SHELL_SUBTITLE_MAX_BYTES = 32,
    CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES = 16,
    CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES = 32,
    CONSOLE_SHELL_MASTER_VOLUME_MIN = 1,
    CONSOLE_SHELL_MASTER_VOLUME_MAX = 10,
    CONSOLE_SHELL_MASTER_VOLUME_DEFAULT = 8,
};

typedef enum {
    CONSOLE_CAPABILITY_DISPLAY = UINT32_C(1) << 0U,
    CONSOLE_CAPABILITY_TOUCH = UINT32_C(1) << 1U,
    CONSOLE_CAPABILITY_AUDIO = UINT32_C(1) << 2U,
    CONSOLE_CAPABILITY_STORAGE = UINT32_C(1) << 3U,
} console_capability_t;

typedef enum {
    CONSOLE_PAGE_HOME = 0,
    CONSOLE_PAGE_EXTERNAL,
    CONSOLE_PAGE_COLORS,
    CONSOLE_PAGE_TOUCH,
    CONSOLE_PAGE_SYSTEM,
    CONSOLE_PAGE_AUDIO,
    CONSOLE_PAGE_ACHIEVEMENTS,
    CONSOLE_PAGE_LIBRARY,
    CONSOLE_PAGE_MULTIPLAYER,
    CONSOLE_PAGE_FILES,
    CONSOLE_PAGE_SAVES,
    CONSOLE_PAGE_TERMINAL,
} console_page_t;

/** OS-only palette choices. Games keep control of their own presentation. */
typedef enum {
    CONSOLE_COLOR_MODE_GAMECHANGERS = 0,
    CONSOLE_COLOR_MODE_ARCADE,
    CONSOLE_COLOR_MODE_OCEAN,
    CONSOLE_COLOR_MODE_SUNSET,
    CONSOLE_COLOR_MODE_COUNT,
} console_color_mode_t;

typedef struct {
    uint32_t id;
    const char *title;
    const char *subtitle;
    /** One or two uppercase path segments, for example GAMES/ARCADE. */
    const char *folder_path;
    uint16_t accent_rgb565;
    uint32_t capabilities;
    console_page_t page;
    bool enabled;
} console_app_descriptor_t;

typedef struct {
    uint16_t x;
    uint16_t y;
} console_shell_contact_t;

typedef struct {
    uint32_t uptime_seconds;
    uint32_t internal_free_kib;
    uint32_t psram_free_kib;
    bool touch_ready;
    bool audio_handoff_ready;
    bool storage_ready;
    bool storage_writable;
    bool content_scan_complete;
    bool content_truncated;
    bool quake_shareware_ready;
    bool usb_content_ready;
    bool usb_content_busy;
    uint8_t usb_content_progress_percent;
    uint16_t valid_cart_count;
    uint16_t invalid_cart_count;
    uint16_t builtin_game_count;
    uint16_t save_slot_count;
    uint64_t save_total_bytes;
    bool save_management_ready;
    bool usb_export_ready;
    bool ssh_transport_ready;
    bool physical_keyboard_ready;
    bool multiplayer_core_ready;
    bool multiplayer_transport_ready;
    uint8_t multiplayer_peer_count;
} console_shell_runtime_info_t;

typedef enum {
    CONSOLE_ACTION_NONE = 0,
    CONSOLE_ACTION_PAGE_CHANGED,
    CONSOLE_ACTION_LAUNCH,
    CONSOLE_ACTION_VOLUME_CHANGED,
    CONSOLE_ACTION_COLOR_MODE_CHANGED,
    CONSOLE_ACTION_LIBRARY_REFRESH,
    CONSOLE_ACTION_FILE_SORT_CHANGED,
    CONSOLE_ACTION_FILE_SELECTED,
    CONSOLE_ACTION_USB_EXPORT,
    CONSOLE_ACTION_SSH_CONNECT,
} console_action_type_t;

typedef struct {
    console_action_type_t type;
    uint32_t app_id;
    uint8_t volume_step;
    console_color_mode_t color_mode;
    size_t item_index;
    p4_file_sort_t file_sort;
} console_shell_action_t;

typedef struct {
    const console_app_descriptor_t *apps;
    size_t app_count;
    size_t selected_index;
    size_t selected_home_item;
    size_t home_scroll_row;
    size_t pressed_index;
    size_t press_start_scroll_row;
    console_page_t page;
    uint32_t active_app_id;
    uint8_t master_volume_step;
    console_color_mode_t color_mode;
    size_t selected_file_index;
    uint16_t press_start_gui_x;
    uint16_t press_start_gui_y;
    char home_folder_path[CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES];
    bool contact_down;
    bool press_active;
    bool scroll_candidate;
    bool scroll_gesture;
    bool home_all_programs;
    bool dirty;
    uint32_t render_generation;
    console_shell_runtime_info_t runtime;
    p4_file_list_t files;
    p4_save_catalog_t saves;
    p4_achievement_catalog_t achievements;
    p4_terminal_t terminal;
    console_shell_contact_t contacts[CONSOLE_SHELL_MAX_CONTACTS];
    size_t contact_count;
} console_shell_t;

/** Validate and initialize a static app registry. No memory is allocated. */
bool console_shell_init(console_shell_t *shell,
                        const console_app_descriptor_t *apps,
                        size_t app_count);

/**
 * Consume one complete board-logical landscape touch snapshot.
 *
 * Invalid frames, more than one contact, and out-of-viewport coordinates
 * cancel the pending press. On the home view, a bounded one-finger vertical
 * drag scrolls whole app rows and suppresses launch. Otherwise, movement
 * between controls cancels the pending press. A launch or page change is
 * emitted only after a valid release.
 */
console_shell_action_t console_shell_handle_touch(
    console_shell_t *shell,
    bool valid,
    const console_shell_contact_t *contacts,
    size_t contact_count);

/** Update non-sensitive runtime counters displayed by the System page. */
void console_shell_set_runtime_info(
    console_shell_t *shell,
    const console_shell_runtime_info_t *runtime);

/** Replace the bounded File Manager snapshot and preserve its sort choice. */
void console_shell_set_file_list(console_shell_t *shell,
                                 const p4_file_list_t *files);

/** Replace the bounded Save Manager snapshot. */
void console_shell_set_save_catalog(console_shell_t *shell,
                                    const p4_save_catalog_t *saves);

/** Replace the bounded, OS-owned session achievement catalog. */
void console_shell_set_achievement_catalog(
    console_shell_t *shell,
    const p4_achievement_catalog_t *achievements);

/** Feed one sanitized printable, backspace, or enter key to Terminal. */
console_shell_action_t console_shell_handle_text_key(
    console_shell_t *shell, char key);

/** Return to the launcher without synthesizing an app launch. */
void console_shell_show_home(console_shell_t *shell);

/** Return the OS-owned master output level in steps 1..10. */
uint8_t console_shell_master_volume_step(const console_shell_t *shell);

/** Return the selected OS palette; it is retained for this shell session. */
console_color_mode_t console_shell_color_mode(const console_shell_t *shell);

/** True when input or runtime state changed since the most recent render. */
bool console_shell_is_dirty(const console_shell_t *shell);

/**
 * Render one standard RGB565 320x200 frame into caller-owned memory.
 * `stride_pixels` must be at least 320. Rendering clears the dirty flag.
 */
bool console_shell_render_rgb565(console_shell_t *shell,
                                 uint16_t *pixels,
                                 size_t stride_pixels);

#ifdef __cplusplus
}
#endif

#endif
