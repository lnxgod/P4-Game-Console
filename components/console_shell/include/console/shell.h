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

#if CONSOLE_SHELL_TARGET_WIDTH == 800U && \
    CONSOLE_SHELL_TARGET_HEIGHT == 480U
#define CONSOLE_SHELL_NATIVE_BBS 1
#include "p4/bbs_ui.h"
#else
#define CONSOLE_SHELL_NATIVE_BBS 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* Stable UI coordinate space used for layout and bounded input. */
    CONSOLE_SHELL_LAYOUT_WIDTH = 320,
    CONSOLE_SHELL_LAYOUT_HEIGHT = 200,
#if CONSOLE_SHELL_TARGET_WIDTH == 800U && \
    CONSOLE_SHELL_TARGET_HEIGHT == 480U
    /* Waveshare desktop renders directly into its centered 768x480 viewport. */
    CONSOLE_SHELL_WIDTH = 768,
    CONSOLE_SHELL_HEIGHT = 480,
#else
    CONSOLE_SHELL_WIDTH = CONSOLE_SHELL_LAYOUT_WIDTH,
    CONSOLE_SHELL_HEIGHT = CONSOLE_SHELL_LAYOUT_HEIGHT,
#endif
    CONSOLE_SHELL_PHYSICAL_WIDTH = CONSOLE_SHELL_TARGET_WIDTH,
    CONSOLE_SHELL_PHYSICAL_HEIGHT = CONSOLE_SHELL_TARGET_HEIGHT,
#if defined(ESP_PLATFORM)
    CONSOLE_SHELL_VIEWPORT_WIDTH = PLATFORM_BOARD_GAME_VIEWPORT_WIDTH,
    CONSOLE_SHELL_VIEWPORT_HEIGHT = PLATFORM_BOARD_GAME_VIEWPORT_HEIGHT,
#else
    CONSOLE_SHELL_VIEWPORT_WIDTH =
        CONSOLE_SHELL_PHYSICAL_WIDTH * CONSOLE_SHELL_LAYOUT_HEIGHT <=
                CONSOLE_SHELL_PHYSICAL_HEIGHT * CONSOLE_SHELL_LAYOUT_WIDTH
            ? CONSOLE_SHELL_PHYSICAL_WIDTH
            : CONSOLE_SHELL_PHYSICAL_HEIGHT * CONSOLE_SHELL_LAYOUT_WIDTH /
                CONSOLE_SHELL_LAYOUT_HEIGHT,
    CONSOLE_SHELL_VIEWPORT_HEIGHT =
        CONSOLE_SHELL_PHYSICAL_WIDTH * CONSOLE_SHELL_LAYOUT_HEIGHT <=
                CONSOLE_SHELL_PHYSICAL_HEIGHT * CONSOLE_SHELL_LAYOUT_WIDTH
            ? CONSOLE_SHELL_PHYSICAL_WIDTH * CONSOLE_SHELL_LAYOUT_HEIGHT /
                CONSOLE_SHELL_LAYOUT_WIDTH
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
    CONSOLE_SHELL_NODE_NAME_BYTES = 17,
    CONSOLE_SHELL_CONTROLLER_NAME_BYTES = 32,
    CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES = 16,
    CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES = 32,
    CONSOLE_SHELL_FILE_MAX_ENTRIES = 32,
    CONSOLE_SHELL_FILE_LABEL_MAX_BYTES = 25,
    CONSOLE_SHELL_FILE_PATH_LABEL_MAX_BYTES = 40,
    CONSOLE_SHELL_TRANSFER_NAME_MAX_BYTES = 40,
    CONSOLE_SHELL_FILE_VISIBLE_ROWS = 5,
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
    CONSOLE_PAGE_FILES,
    CONSOLE_PAGE_GAMES,
    /** Compatibility name used by the PC desktop host. */
    CONSOLE_PAGE_LIBRARY = CONSOLE_PAGE_GAMES,
    CONSOLE_PAGE_AUDIO,
    CONSOLE_PAGE_ACHIEVEMENTS,
    CONSOLE_PAGE_MULTIPLAYER,
    CONSOLE_PAGE_SAVES,
    CONSOLE_PAGE_USB_DRIVE,
    CONSOLE_PAGE_FILE_TRANSFER,
    CONSOLE_PAGE_TERMINAL,
    CONSOLE_PAGE_STORAGE,
    CONSOLE_PAGE_CONTROLLERS,
} console_page_t;

typedef enum {
    CONSOLE_CONTROLLER_TRANSPORT_NONE = 0,
    CONSOLE_CONTROLLER_TRANSPORT_USB_HID,
    CONSOLE_CONTROLLER_TRANSPORT_BLE_HID,
} console_controller_transport_t;

typedef enum {
    CONSOLE_CONTROLLER_MAPPING_A = 0,
    CONSOLE_CONTROLLER_MAPPING_B,
    CONSOLE_CONTROLLER_MAPPING_X,
    CONSOLE_CONTROLLER_MAPPING_Y,
    CONSOLE_CONTROLLER_MAPPING_START,
    CONSOLE_CONTROLLER_MAPPING_BACK,
    CONSOLE_CONTROLLER_MAPPING_COUNT,
} console_controller_mapping_slot_t;

/** Shell-only palettes; games retain full control of their own colors. */
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

typedef enum {
    CONSOLE_STORAGE_STARTING = 0,
    CONSOLE_STORAGE_READY,
    CONSOLE_STORAGE_USB_HOST,
    CONSOLE_STORAGE_FORMAT_REQUIRED,
    CONSOLE_STORAGE_MISSING,
    CONSOLE_STORAGE_INVALID,
    CONSOLE_STORAGE_LOCKED,
    CONSOLE_STORAGE_FAULT,
} console_shell_storage_state_t;

typedef enum {
    CONSOLE_BOARD_ELECROW_10 = 0,
    CONSOLE_BOARD_OLIMEX_P4_PC,
    CONSOLE_BOARD_WAVESHARE_4_3,
    CONSOLE_BOARD_HOST_PREVIEW,
} console_shell_board_kind_t;

typedef enum {
    CONSOLE_STORAGE_REPAIR_NOT_RUN = 0,
    CONSOLE_STORAGE_REPAIR_CLEAN,
    CONSOLE_STORAGE_REPAIR_REPAIRED,
    CONSOLE_STORAGE_REPAIR_NEEDS_HOST,
    CONSOLE_STORAGE_REPAIR_UNSUPPORTED,
    CONSOLE_STORAGE_REPAIR_FAILED,
} console_storage_repair_outcome_t;

typedef enum {
    CONSOLE_STORAGE_OPERATION_NONE = 0,
    CONSOLE_STORAGE_OPERATION_CHECK,
    CONSOLE_STORAGE_OPERATION_RETRY,
    CONSOLE_STORAGE_OPERATION_REPAIR,
} console_storage_operation_t;

typedef enum {
    CONSOLE_FILE_TRANSFER_IDLE = 0,
    CONSOLE_FILE_TRANSFER_RECEIVING,
    CONSOLE_FILE_TRANSFER_SENDING,
    CONSOLE_FILE_TRANSFER_COMPLETE,
    CONSOLE_FILE_TRANSFER_FAILED,
} console_file_transfer_state_t;

typedef enum {
    CONSOLE_FILE_TRANSFER_DIRECTION_NONE = 0,
    CONSOLE_FILE_TRANSFER_UPLOAD,
    CONSOLE_FILE_TRANSFER_DOWNLOAD,
} console_file_transfer_direction_t;

typedef enum {
    CONSOLE_FILE_TRANSFER_CLASS_NONE = 0,
    CONSOLE_FILE_TRANSFER_CLASS_P4G,
    CONSOLE_FILE_TRANSFER_CLASS_EXCHANGE,
} console_file_transfer_class_t;

typedef enum {
    /** Installed multiplayer-capable game selected before room discovery. */
    CONSOLE_MULTIPLAYER_OPTION_GAME = 0,
    CONSOLE_MULTIPLAYER_OPTION_MODE,
    CONSOLE_MULTIPLAYER_OPTION_MAP,
    CONSOLE_MULTIPLAYER_OPTION_SKILL,
    CONSOLE_MULTIPLAYER_OPTION_MONSTERS,
    CONSOLE_MULTIPLAYER_OPTION_FAST,
    CONSOLE_MULTIPLAYER_OPTION_RESPAWN,
    CONSOLE_MULTIPLAYER_OPTION_TIME_LIMIT,
    CONSOLE_MULTIPLAYER_OPTION_TRANSPORT,
    /** CREATE NEW followed by compatible discovered room choices. */
    CONSOLE_MULTIPLAYER_OPTION_LOBBY,
    CONSOLE_MULTIPLAYER_OPTION_COUNT,
} console_multiplayer_option_t;

typedef enum {
    CONSOLE_MULTIPLAYER_LOBBY_BROWSING = 0,
    CONSOLE_MULTIPLAYER_LOBBY_HOSTING,
    CONSOLE_MULTIPLAYER_LOBBY_JOINING,
    CONSOLE_MULTIPLAYER_LOBBY_CONNECTED,
} console_multiplayer_lobby_phase_t;

typedef enum {
    CONSOLE_MULTIPLAYER_VIEW_ROLE = 0,
    CONSOLE_MULTIPLAYER_VIEW_HOST,
    CONSOLE_MULTIPLAYER_VIEW_JOIN,
} console_multiplayer_view_t;

enum {
    CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX = 4,
};

typedef struct {
    uint32_t session_id;
    int8_t rssi;
    uint8_t players_present;
    uint8_t player_capacity;
    bool game_available;
    char game_title[CONSOLE_SHELL_TITLE_MAX_BYTES];
} console_multiplayer_lobby_display_t;

typedef struct {
    uint32_t uptime_seconds;
    char node_name[CONSOLE_SHELL_NODE_NAME_BYTES];
    uint32_t internal_free_kib;
    uint32_t psram_free_kib;
    uint32_t game_storage_kib;
    console_shell_storage_state_t game_storage_state;
    console_shell_board_kind_t board_kind;
    bool touch_ready;
    bool controller_ready;
    console_controller_transport_t controller_transport;
    bool ble_controller_supported;
    bool ble_controller_enabled;
    bool ble_controller_host_ready;
    bool ble_controller_bonded;
    bool ble_controller_connected;
    bool ble_controller_encrypted;
    bool ble_controller_busy;
    int8_t ble_controller_rssi;
    uint32_t ble_controller_reports_received;
    uint32_t ble_controller_reports_dropped;
    int ble_controller_last_error;
    char ble_controller_name[CONSOLE_SHELL_CONTROLLER_NAME_BYTES];
    bool ble_controller_multiplayer_ready;
    bool controller_mapping_active;
    bool controller_mapping_persistent;
    uint8_t controller_mapping_target;
    uint8_t controller_mapping[CONSOLE_CONTROLLER_MAPPING_COUNT];
    int controller_mapping_last_error;
    bool keyboard_ready;
    bool mouse_ready;
    bool sd_card_storage;
    bool audio_handoff_ready;
    bool game_storage_usb_attached;
    bool usb_storage_supported;
    bool usb_storage_eject_safe;
    bool usb_drive_active;
    bool usb_input_host_active;
    bool doom_wad_ready;
    bool content_scan_complete;
    bool usb_content_ready;
    bool file_transfer_ready;
    bool file_transfer_busy;
    console_file_transfer_state_t file_transfer_state;
    console_file_transfer_direction_t file_transfer_direction;
    console_file_transfer_class_t file_transfer_class;
    uint8_t file_transfer_progress_percent;
    uint8_t file_transfer_last_status;
    uint32_t file_transfer_bytes;
    uint32_t file_transfer_total_bytes;
    uint32_t file_transfer_generation;
    char file_transfer_name[CONSOLE_SHELL_TRANSFER_NAME_MAX_BYTES];
    bool content_validation_running;
    bool content_validation_complete;
    uint8_t content_validation_progress_percent;
    bool multiplayer_core_ready;
    bool multiplayer_transport_ready;
    bool multiplayer_transport_starting;
    bool multiplayer_transport_encrypted;
    /** 0 = direct/relay UART, 1 = opt-in BLE GATT. */
    uint8_t multiplayer_transport_kind;
    bool multiplayer_peer_seen;
    bool multiplayer_lobby_ready;
    bool multiplayer_lobby_is_host;
    bool multiplayer_lobby_scanning;
    bool multiplayer_lobby_action_enabled;
    bool multiplayer_can_start;
    bool multiplayer_launch_syncing;
    bool multiplayer_settings_editable;
    bool multiplayer_game_ready;
    bool multiplayer_game_is_doom;
    uint8_t multiplayer_game_selection;
    uint8_t multiplayer_game_count;
    char multiplayer_game_title[CONSOLE_SHELL_TITLE_MAX_BYTES];
    console_multiplayer_lobby_phase_t multiplayer_lobby_phase;
    /** Zero selects CREATE NEW; 1..count select discovered rooms. */
    uint8_t multiplayer_lobby_selection;
    uint8_t multiplayer_lobby_count;
    int8_t multiplayer_lobby_rssi;
    uint32_t multiplayer_lobby_session_id;
    console_multiplayer_lobby_display_t
        multiplayer_lobbies[CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX];
    uint8_t multiplayer_route_id;
    uint8_t multiplayer_player_slot;
    uint8_t multiplayer_game_mode;
    uint8_t multiplayer_episode;
    uint8_t multiplayer_map;
    uint8_t multiplayer_skill;
    uint8_t multiplayer_time_limit_minutes;
    bool multiplayer_no_monsters;
    bool multiplayer_fast_monsters;
    bool multiplayer_respawn_monsters;
    uint32_t multiplayer_rx_frames;
    uint32_t multiplayer_tx_frames;
    bool physical_keyboard_ready;
    uint16_t valid_cart_count;
    uint16_t builtin_game_count;
    uint8_t boot_volume_step;
    uint8_t game_volume_step;
    bool audio_settings_persistent;
    uint32_t game_storage_free_kib;
    uint32_t game_storage_sector_bytes;
    uint32_t game_storage_frequency_khz;
    uint32_t game_storage_root_entries;
    uint32_t game_storage_mount_failures;
    uint32_t game_storage_scans;
    uint32_t game_storage_checks;
    uint32_t game_storage_recovery_attempts;
    uint32_t game_storage_repair_attempts;
    uint32_t game_storage_repair_sectors;
    bool game_storage_card_ready;
    bool game_storage_filesystem_ready;
    bool game_storage_repair_supported;
    bool game_storage_last_check_ok;
    console_storage_repair_outcome_t game_storage_repair_outcome;
    console_storage_operation_t game_storage_operation;
} console_shell_runtime_info_t;

typedef struct {
    uint32_t source_index;
    char label[CONSOLE_SHELL_FILE_LABEL_MAX_BYTES];
    uint32_t size_kib;
    bool is_directory;
    bool removable;
    bool installable;
} console_shell_file_entry_t;

typedef struct {
    console_shell_file_entry_t entries[CONSOLE_SHELL_FILE_MAX_ENTRIES];
    size_t entry_count;
    uint32_t total_visible_entries;
    uint32_t hidden_entries;
    uint32_t omitted_entries;
    uint32_t storage_generation;
    uint32_t revision;
    char path_label[CONSOLE_SHELL_FILE_PATH_LABEL_MAX_BYTES];
    bool available;
    bool can_go_up;
} console_shell_file_listing_t;

typedef enum {
    CONSOLE_FILE_NOTICE_NONE = 0,
    CONSOLE_FILE_NOTICE_REFRESHED,
    CONSOLE_FILE_NOTICE_DELETED,
    CONSOLE_FILE_NOTICE_ERROR,
    CONSOLE_FILE_NOTICE_UPDATING,
} console_shell_file_notice_t;

typedef enum {
    CONSOLE_ACTION_NONE = 0,
    CONSOLE_ACTION_PAGE_CHANGED,
    CONSOLE_ACTION_LAUNCH,
    CONSOLE_ACTION_FILE_REFRESH,
    CONSOLE_ACTION_FILE_OPEN,
    CONSOLE_ACTION_FILE_UP,
    CONSOLE_ACTION_FILE_DELETE,
    CONSOLE_ACTION_GAME_REFRESH,
    CONSOLE_ACTION_GAME_REMOVE,
    CONSOLE_ACTION_OS_UPDATE_INSTALL,
    CONSOLE_ACTION_COLOR_MODE_CHANGED,
    CONSOLE_ACTION_USB_MODE_ENABLE,
    CONSOLE_ACTION_USB_MODE_DISABLE,
    CONSOLE_ACTION_BOOT_VOLUME_SET,
    CONSOLE_ACTION_GAME_VOLUME_SET,
    CONSOLE_ACTION_MULTIPLAYER_CONFIGURE,
    CONSOLE_ACTION_MULTIPLAYER_CREATE_LOBBY,
    CONSOLE_ACTION_MULTIPLAYER_JOIN_LOBBY,
    CONSOLE_ACTION_MULTIPLAYER_LOBBY_RESET,
    CONSOLE_ACTION_MULTIPLAYER_LOBBY_SELECT,
    CONSOLE_ACTION_MULTIPLAYER_LAUNCH_GAME,
    CONSOLE_ACTION_STORAGE_CHECK,
    CONSOLE_ACTION_STORAGE_RETRY,
    CONSOLE_ACTION_STORAGE_REPAIR,
    CONSOLE_ACTION_CONTROLLER_BLE_ENABLE,
    CONSOLE_ACTION_CONTROLLER_BLE_DISABLE,
    CONSOLE_ACTION_CONTROLLER_PAIR,
    CONSOLE_ACTION_CONTROLLER_DISCONNECT,
    CONSOLE_ACTION_CONTROLLER_FORGET,
    CONSOLE_ACTION_CONTROLLER_MAPPING_START,
    CONSOLE_ACTION_CONTROLLER_MAPPING_CANCEL,
    CONSOLE_ACTION_CONTROLLER_MAPPING_RESET,
} console_action_type_t;

typedef enum {
    CONSOLE_BUTTON_UP = UINT32_C(1) << 0U,
    CONSOLE_BUTTON_DOWN = UINT32_C(1) << 1U,
    CONSOLE_BUTTON_LEFT = UINT32_C(1) << 2U,
    CONSOLE_BUTTON_RIGHT = UINT32_C(1) << 3U,
    CONSOLE_BUTTON_ACCEPT = UINT32_C(1) << 4U,
    CONSOLE_BUTTON_BACK = UINT32_C(1) << 5U,
    CONSOLE_BUTTON_REFRESH = UINT32_C(1) << 6U,
} console_button_t;

#define CONSOLE_BUTTON_MASK UINT32_C(0x0000007f)

typedef struct {
    console_action_type_t type;
    uint32_t app_id;
    uint32_t file_source_index;
    console_color_mode_t color_mode;
    uint8_t volume_step;
    console_multiplayer_option_t multiplayer_option;
    int8_t multiplayer_delta;
    uint8_t multiplayer_lobby_selection;
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
    console_color_mode_t color_mode;
    p4_achievement_catalog_t achievements;
    p4_file_list_t desktop_files;
    p4_save_catalog_t saves;
    p4_terminal_t terminal;
#if CONSOLE_SHELL_NATIVE_BBS
    p4_ansi_terminal_t bbs_terminal;
#endif
    console_shell_file_listing_t files;
    size_t file_selected_index;
    size_t file_first_visible;
    size_t audio_selected_row;
    size_t multiplayer_selected_row;
    console_multiplayer_view_t multiplayer_view;
    uint8_t multiplayer_role_selection;
    size_t storage_selected_action;
    size_t controller_selected_action;
    console_shell_file_notice_t file_notice;
    uint16_t press_start_gui_x;
    uint16_t press_start_gui_y;
    char home_folder_path[CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES];
    bool contact_down;
    bool press_active;
    bool scroll_candidate;
    bool scroll_gesture;
    bool home_all_programs;
    bool file_delete_confirm;
    bool storage_repair_confirm;
    bool dirty;
    uint32_t previous_buttons;
    uint16_t pointer_x;
    uint16_t pointer_y;
    bool pointer_visible;
    bool pointer_pressed;
    uint32_t render_generation;
    console_shell_runtime_info_t runtime;
    console_shell_contact_t contacts[CONSOLE_SHELL_MAX_CONTACTS];
    size_t contact_count;
} console_shell_t;

/** Validate and initialize a static app registry. No memory is allocated. */
bool console_shell_init(console_shell_t *shell,
                        const console_app_descriptor_t *apps,
                        size_t app_count);

/**
 * Consume one complete physical 1024x600 touch snapshot.
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

/**
 * Consume one sanitized controller snapshot.
 *
 * Actions are edge-triggered. Calling with zero immediately neutralizes the
 * controller state, so a disconnect cannot leave navigation held.
 */
console_shell_action_t console_shell_handle_buttons(
    console_shell_t *shell, uint32_t held_buttons);

/** Show, move, or hide the bounded desktop pointer. */
void console_shell_set_pointer(console_shell_t *shell,
                               bool visible,
                               uint16_t x,
                               uint16_t y,
                               bool pressed);

/** Update non-sensitive runtime and game-storage status. */
void console_shell_set_runtime_info(
    console_shell_t *shell,
    const console_shell_runtime_info_t *runtime);

/** Replace the bounded File Manager snapshot. */
bool console_shell_set_file_listing(
    console_shell_t *shell,
    const console_shell_file_listing_t *listing);

/** Replace File Manager contents with a sorted desktop-service snapshot. */
bool console_shell_set_file_list(
    console_shell_t *shell,
    const p4_file_list_t *files);

/** Replace the bounded, OS-owned save-slot metadata snapshot. */
bool console_shell_set_save_catalog(
    console_shell_t *shell,
    const p4_save_catalog_t *saves);

/** Feed one sanitized physical or on-screen key to the Terminal page. */
bool console_shell_handle_text_key(console_shell_t *shell, char key);

/** Set the result banner after a refresh or confirmed file operation. */
void console_shell_set_file_notice(
    console_shell_t *shell,
    console_shell_file_notice_t notice);

/** Replace the bounded, OS-owned session achievement catalog. */
void console_shell_set_achievement_catalog(
    console_shell_t *shell,
    const p4_achievement_catalog_t *achievements);

/** Return to the launcher without synthesizing an app launch. */
void console_shell_show_home(console_shell_t *shell);

/** Return the selected shell palette. The setting is session-only. */
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
