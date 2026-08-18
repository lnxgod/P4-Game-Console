// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * P4 Console OS: a small FreeRTOS-native foreground shell. Built-ins consume
 * platform services directly; validated P4G cartridges run through a bounded
 * host table and the pinned ELF loader. Doom remains an exclusive one-way
 * handoff because the imported engine has no reviewed reentrant teardown.
 */

#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "console/shell.h"
#include "sdkconfig.h"
#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "p4/audio.h"
#include "p4/achievements.h"
#include "p4/bbs_ui.h"
#include "p4/cartridge.h"
#include "p4/content_catalog.h"
#include "p4/desktop.h"
#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"
#include "p4/multiplayer.h"
#include "p4/platform.h"
#include "platform/board.h"
#include "platform/console_settings.h"
#include "platform/display.h"
#include "platform/game_catalog.h"
#include "platform/game_loader.h"
#include "platform/game_storage.h"
#include "platform/os_update.h"
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
    !CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE
#define P4_CONSOLE_SIGNAL_SCAN 1
#include "platform/signal_scan.h"
#else
#define P4_CONSOLE_SIGNAL_SCAN 0
#endif
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
    (CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
     CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE)
#define P4_CONSOLE_USB_INPUT 1
#else
#define P4_CONSOLE_USB_INPUT 0
#endif

#if P4_CONSOLE_USB_INPUT
#include "gamepad/gamepad.h"
#include "platform_gamepad_usb/platform_gamepad_usb.h"
#include "platform_usb_host/platform_usb_host.h"
#endif
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
#include "platform/touch.h"
#include "platform_i2c_shared/bus.h"
#include "runtime_gate.h"
#endif

enum {
    CONSOLE_APP_DOOM = 1,
    CONSOLE_APP_COLORS = 2,
    CONSOLE_APP_TOUCH = 3,
    CONSOLE_APP_SYSTEM = 4,
    CONSOLE_APP_AUDIO = 5,
    CONSOLE_APP_FILES = 6,
    CONSOLE_APP_GAMES = 7,
    CONSOLE_APP_ACHIEVEMENTS = 8,
    CONSOLE_APP_MULTIPLAYER = 9,
    CONSOLE_APP_SAVES = 10,
    CONSOLE_APP_TERMINAL = 11,
    CONSOLE_APP_USB_DRIVE = 12,
    CONSOLE_BUILTIN_APP_ID_MAX = CONSOLE_APP_USB_DRIVE,
    CONSOLE_FRAME_INTERVAL_MS = 16,
    CONSOLE_SUBMIT_TIMEOUT_MS = 250,
    CONSOLE_BACKLIGHT_PERCENT = 25,
#if P4_CONSOLE_USB_INPUT
    CONSOLE_GAMEPAD_STICK_THRESHOLD = 12000,
    CONSOLE_USB_ENUM_GUARD_CONFIRM_LOOPS = 600,
#endif
    CONSOLE_CLEANUP_ATTEMPTS = 3,
    CONSOLE_BOOT_POST_BEEP_MS = 220,
    CONSOLE_BOOT_POST_GAP_MS = 180,
    CONSOLE_BOOT_DTMF_TONE_MS = 160,
    CONSOLE_BOOT_DTMF_GAP_MS = 90,
    CONSOLE_BOOT_DTMF_DASH_MS = 220,
    CONSOLE_BOOT_ANIMATION_STEPS = 5,
    CONSOLE_STORAGE_LOADING_FRAME_MS = 160,
    CONSOLE_BBS_CONNECT_HOLD_MS = 260,
    CONSOLE_BBS_HOME_REVEAL_STEPS = 6,
    CONSOLE_BBS_HOME_REVEAL_DELAY_MS = 45,
    CONSOLE_BOOT_SUBMIT_ATTEMPTS = 3,
    CONSOLE_BOOT_SUBMIT_RETRY_MS = 20,
    CONSOLE_BOOT_LOGO_WIDTH = 112,
    CONSOLE_BOOT_LOGO_HEIGHT = 112,
    CONSOLE_STORAGE_INIT_STACK_BYTES = 12 * 1024,
    CONSOLE_P4CART_SCAN_STACK_BYTES = 24 * 1024,
    CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK =
        P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ *
        CONSOLE_FRAME_INTERVAL_MS / 1000,
};

typedef struct {
    const char *name;
    uint16_t low_hz;
    uint16_t high_hz;
    uint16_t duration_ms;
    uint16_t gap_ms;
    uint8_t low_volume_step;
    uint8_t high_volume_step;
    p4_waveform_t waveform;
} console_boot_modem_phase_t;

typedef struct {
    char digit;
    uint16_t low_hz;
    uint16_t high_hz;
} console_boot_dial_digit_t;

/*
 * A compact, standards-shaped V.25/V.22bis call sequence. This is a boot
 * effect rather than a data modem, but its frequencies and the important
 * V.22bis training intervals are real: 1300 Hz calling tone, 2100 Hz answer
 * tone, 1200/2400 Hz channel carriers, and the low-level 1800 Hz guard tone.
 */
static const console_boot_modem_phase_t s_boot_modem_phases[] = {
    {"calling-tone", 1300U, 0U, 180U, 90U, 6U, 0U,
     P4_WAVE_TRIANGLE},
    {"answer-tone", 2100U, 0U, 260U, 30U, 6U, 0U,
     P4_WAVE_TRIANGLE},
    {"channel-carriers", 2400U, 1800U, 130U, 55U, 6U, 2U,
     P4_WAVE_TRIANGLE},
    {"00-11-training", 1200U, 2400U, 140U, 24U, 6U, 6U,
     P4_WAVE_SQUARE},
    {"2400-bps-scrambled-ones", 1200U, 2400U, 260U, 24U, 5U, 7U,
     P4_WAVE_SQUARE},
    {"carrier-lock", 1200U, 2400U, 180U, 0U, 4U, 6U,
     P4_WAVE_TRIANGLE},
};

/* Deliberately separated pulses sound like discrete hard-disk head seeks. */
static const console_boot_modem_phase_t s_boot_hdd_phases[] = {
    {"seek-1", 105U, 2700U, 34U, 72U, 9U, 4U, P4_WAVE_SQUARE},
    {"seek-2", 145U, 3150U, 30U, 84U, 9U, 4U, P4_WAVE_SQUARE},
    {"seek-3", 88U, 2400U, 38U, 68U, 9U, 4U, P4_WAVE_SQUARE},
    {"seek-4", 170U, 3300U, 28U, 92U, 9U, 4U, P4_WAVE_SQUARE},
    {"seek-5", 120U, 2850U, 36U, 120U, 9U, 4U, P4_WAVE_SQUARE},
};

static const console_boot_dial_digit_t s_boot_dial[] = {
    {'6', 770U, 1477U},
    {'1', 697U, 1209U},
    {'4', 770U, 1209U},
    {'2', 697U, 1336U},
    {'7', 852U, 1209U},
    {'6', 770U, 1477U},
    {'3', 697U, 1477U},
    {'6', 770U, 1477U},
    {'3', 697U, 1477U},
    {'9', 852U, 1477U},
};

static const char *const TAG = "p4_console_os";
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static platform_i2c_shared_t *s_shared_bus;
static platform_touch_t *s_touch;
#endif
static uint16_t *s_pixels;
static bool s_display_initialized;
#if P4_CONSOLE_USB_INPUT
static bool s_gamepad_ready;
static bool s_gamepad_connected;
static bool s_keyboard_connected;
static bool s_mouse_connected;
static uint32_t s_terminal_keyboard_session;
static uint8_t s_previous_terminal_keys[PLATFORM_USB_KEYBOARD_BOOT_KEY_COUNT];
static uint32_t s_gamepad_polls;
static uint32_t s_gamepad_poll_failures;
static uint32_t s_aux_input_poll_failures;
static uint16_t s_mouse_x = CONSOLE_SHELL_LAYOUT_WIDTH / 2U;
static uint16_t s_mouse_y = CONSOLE_SHELL_LAYOUT_HEIGHT / 2U;
static bool s_usb_enum_probe_allowed;
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
    CONFIG_P4_WAVESHARE_H2_FORCE_FULL_SPEED_HOST
static bool s_usb_enum_probe_started;
static bool s_usb_enum_probe_confirm_attempted;
#endif
#endif
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static bool s_touch_ready;
static uint32_t s_touch_polls;
static uint32_t s_touch_poll_failures;
#endif
static uint32_t s_doom_handoff_count;
static uint32_t s_loop_count;
static bool s_game_storage_status_seen;
static platform_game_storage_status_t s_game_storage_status;
typedef enum {
    CONSOLE_STORAGE_INIT_NOT_STARTED = 0,
    CONSOLE_STORAGE_INIT_RUNNING,
    CONSOLE_STORAGE_INIT_READY,
    CONSOLE_STORAGE_INIT_FAILED,
} console_storage_init_state_t;
static console_storage_init_state_t s_game_storage_init_state;
static esp_err_t s_game_storage_init_result = ESP_ERR_INVALID_STATE;
static int64_t s_game_storage_init_started_us;
static portMUX_TYPE s_game_storage_init_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_game_storage_file_listing_t s_platform_file_listing;
static console_shell_file_listing_t s_shell_file_listing;
static char s_file_directory[
    PLATFORM_GAME_STORAGE_RELATIVE_PATH_MAX_BYTES];
static console_shell_file_listing_t s_manager_listing;
static platform_game_catalog_t s_game_catalog;
static platform_game_catalog_t s_catalog_staging;
static p4_content_catalog_t s_p4cart_catalog;
static p4_content_catalog_t s_p4cart_scan_staging;
static p4_content_status_t s_p4cart_scan_result;
static TaskHandle_t s_p4cart_scan_task;
static portMUX_TYPE s_p4cart_scan_lock = portMUX_INITIALIZER_UNLOCKED;
typedef enum {
    P4CART_SCAN_IDLE = 0,
    P4CART_SCAN_RUNNING,
    P4CART_SCAN_DONE,
} p4cart_scan_state_t;
static p4cart_scan_state_t s_p4cart_scan_state;
static bool s_p4cart_scan_seen;
static uint32_t s_p4cart_scan_generation;
static uint32_t s_p4cart_catalog_generation;
static unsigned s_p4cart_scan_low_water_bytes;
static platform_os_update_info_t s_update_staging;
static console_shell_t s_shell;
static p4_achievement_catalog_t s_achievements;
static p4_save_catalog_t s_saves;
static p4_mp_session_t s_multiplayer_session;
static platform_console_settings_t s_console_settings = {
    .version = PLATFORM_CONSOLE_SETTINGS_VERSION,
    .size = (uint16_t)sizeof(platform_console_settings_t),
    .boot_volume_step = PLATFORM_CONSOLE_BOOT_VOLUME_DEFAULT,
    .game_volume_step = PLATFORM_CONSOLE_GAME_VOLUME_DEFAULT,
    .persistent = false,
};
static platform_os_update_info_t s_os_update_info;
static bool s_catalog_seen;
static uint32_t s_catalog_storage_generation;
static bool s_file_listing_seen;
static platform_game_storage_state_t s_file_listing_storage_state;
static uint32_t s_file_listing_storage_generation;
static uint32_t s_file_listing_revision;
static uint32_t s_manager_listing_revision;
static char s_doom_subtitle[CONSOLE_SHELL_SUBTITLE_MAX_BYTES] =
    "STORAGE CHECKING";
static char s_game_manager_subtitle[CONSOLE_SHELL_SUBTITLE_MAX_BYTES] =
    "P4G + P4CART + OS";
static int16_t s_native_audio_pcm[
    CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK *
    P4_GAME_PLATFORM_AUDIO_CHANNEL_COUNT];

extern const uint8_t _binary_gamechangers_ai_logo_rgb565_start[];
extern const uint8_t _binary_gamechangers_ai_logo_rgb565_end[];

static esp_err_t present(console_shell_t *shell);
static console_shell_runtime_info_t runtime_info(void);
static bool storage_app_owned(void);
#if P4_CONSOLE_USB_INPUT
static esp_err_t start_usb_input(void);
static esp_err_t stop_usb_input_for_role_switch(void);
#endif

#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
void console_os_launch_doom(void);
#else
void console_os_launch_doom(uint8_t master_volume_step);
#endif

static console_app_descriptor_t s_apps[CONSOLE_SHELL_MAX_APPS];
static size_t s_app_count;

static const console_app_descriptor_t s_doom_app = {
    .id = CONSOLE_APP_DOOM,
    .title = "DOOM",
    .subtitle = s_doom_subtitle,
    .folder_path = "GAMES/ACTION",
    .accent_rgb565 = UINT16_C(0xF904),
    .capabilities = CONSOLE_CAPABILITY_DISPLAY |
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
                    CONSOLE_CAPABILITY_TOUCH |
#endif
                    CONSOLE_CAPABILITY_AUDIO |
                    CONSOLE_CAPABILITY_STORAGE,
    .page = CONSOLE_PAGE_EXTERNAL,
    .enabled = true,
};

static const console_app_descriptor_t s_builtin_apps[] = {
    {
        .id = CONSOLE_APP_COLORS,
        .title = "APPEARANCE",
        .subtitle = "BBS / WINDOWS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x5FFF),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY,
        .page = CONSOLE_PAGE_COLORS,
        .enabled = true,
    },
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    {
        .id = CONSOLE_APP_TOUCH,
        .title = "TOUCH",
        .subtitle = "GT911 CONTACTS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xFFE0),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH,
        .page = CONSOLE_PAGE_TOUCH,
        .enabled = true,
    },
#endif
    {
        .id = CONSOLE_APP_SYSTEM,
        .title = "SYSTEM",
        .subtitle = "RTOS STATUS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x5FEA),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
                        | CONSOLE_CAPABILITY_TOUCH
#endif
                        ,
        .page = CONSOLE_PAGE_SYSTEM,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_AUDIO,
        .title = "AUDIO",
        .subtitle = "DOOM SOUND PATH",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xF81F),
        .capabilities = CONSOLE_CAPABILITY_AUDIO,
        .page = CONSOLE_PAGE_AUDIO,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_ACHIEVEMENTS,
        .title = "ACHIEVEMENTS",
        .subtitle = "SESSION BADGES",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xFD20),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY,
        .page = CONSOLE_PAGE_ACHIEVEMENTS,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_FILES,
        .title = "FILE MANAGER",
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        .subtitle = "MICROSD GAMES",
#else
        .subtitle = "P4 GAMES USB",
#endif
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xFD20),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
                        CONSOLE_CAPABILITY_TOUCH |
#endif
                        CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_FILES,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_GAMES,
        .title = "GAME MANAGER",
        .subtitle = s_game_manager_subtitle,
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x5FEA),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
                        CONSOLE_CAPABILITY_TOUCH |
#endif
                        CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_GAMES,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_MULTIPLAYER,
        .title = "MULTIPLAYER",
        .subtitle = "LOCAL SESSION CORE",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xFFE0),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
                        CONSOLE_CAPABILITY_TOUCH,
#else
                        0U,
#endif
        .page = CONSOLE_PAGE_MULTIPLAYER,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_SAVES,
        .title = "SAVE MANAGER",
        .subtitle = "OS OWNED SAVE SLOTS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x07FF),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_STORAGE |
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
                        CONSOLE_CAPABILITY_TOUCH,
#else
                        0U,
#endif
        .page = CONSOLE_PAGE_SAVES,
        .enabled = true,
    },
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    {
        .id = CONSOLE_APP_USB_DRIVE,
        .title = "USB DRIVE",
        .subtitle = "SD CARD TO MAC",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x07FF),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH |
                        CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_USB_DRIVE,
        .enabled = true,
    },
#endif
    {
        .id = CONSOLE_APP_TERMINAL,
        .title = "TERMINAL",
        .subtitle = "COMMANDS + SSH STATUS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x5FEA),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
                        CONSOLE_CAPABILITY_TOUCH,
#else
                        0U,
#endif
        .page = CONSOLE_PAGE_TERMINAL,
        .enabled = true,
    },
};

_Static_assert(CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK == 256,
               "16 ms must produce exactly 256 16 kHz frames");

_Static_assert((int)CONSOLE_SHELL_LAYOUT_WIDTH ==
                   (int)PLATFORM_DISPLAY_GAME_WIDTH,
               "console input layout must match the game width");
_Static_assert((int)CONSOLE_SHELL_LAYOUT_HEIGHT ==
                   (int)PLATFORM_DISPLAY_GAME_HEIGHT,
               "console input layout must match the game height");
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
_Static_assert((int)CONSOLE_SHELL_WIDTH ==
                   (int)PLATFORM_DISPLAY_CONTENT_WIDTH &&
                   (int)CONSOLE_SHELL_HEIGHT ==
                   (int)PLATFORM_DISPLAY_CONTENT_HEIGHT,
               "Waveshare Console OS must render native 768x480 content");
#endif
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
_Static_assert((int)CONSOLE_SHELL_PHYSICAL_WIDTH ==
                   (int)PLATFORM_TOUCH_WIDTH,
               "console touch width must match GT911 coordinates");
_Static_assert((int)CONSOLE_SHELL_PHYSICAL_HEIGHT ==
                   (int)PLATFORM_TOUCH_HEIGHT,
               "console touch height must match GT911 coordinates");
_Static_assert((int)CONSOLE_SHELL_MAX_CONTACTS ==
                   (int)PLATFORM_TOUCH_MAX_CONTACTS,
               "console contact bound must match the touch service");
#endif

static bool append_app(const console_app_descriptor_t *app)
{
    if (app == NULL || s_app_count >= CONSOLE_SHELL_MAX_APPS) {
        return false;
    }
    s_apps[s_app_count++] = *app;
    return true;
}

static uint32_t shell_capabilities(uint32_t game_capabilities)
{
    uint32_t capabilities = 0U;
    if ((game_capabilities & P4_GAME_CAP_VIDEO) != 0U) {
        capabilities |= CONSOLE_CAPABILITY_DISPLAY;
    }
    if ((game_capabilities & P4_GAME_CAP_CONTROLS) != 0U) {
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
        capabilities |= CONSOLE_CAPABILITY_TOUCH;
#endif
    }
    if ((game_capabilities &
         (P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_AUDIO_STREAM)) != 0U) {
        capabilities |= CONSOLE_CAPABILITY_AUDIO;
    }
    if ((game_capabilities & P4_GAME_CAP_STORAGE) != 0U) {
        capabilities |= CONSOLE_CAPABILITY_STORAGE;
    }
    return capabilities;
}

static bool build_app_registry(void)
{
    s_app_count = 0U;
    if (!append_app(&s_doom_app)) {
        return false;
    }
    for (size_t i = 0U; i < s_game_catalog.entry_count; ++i) {
        const platform_game_catalog_entry_t *const game =
            &s_game_catalog.entries[i];
        if (!game->valid ||
            game->package.launcher_id <= CONSOLE_BUILTIN_APP_ID_MAX) {
            continue;
        }
        if (game->package.title[0] == '\0' ||
            game->package.folder[0] == '\0') {
            return false;
        }
        const console_app_descriptor_t launcher = {
            .id = game->package.launcher_id,
            .title = game->package.title,
            .subtitle = game->package.subtitle,
            .folder_path = game->package.folder,
            .accent_rgb565 = game->package.accent_rgb565,
            .capabilities = shell_capabilities(
                game->package.required_capabilities |
                game->package.optional_capabilities),
            .page = CONSOLE_PAGE_EXTERNAL,
            .enabled = true,
        };
        if (!append_app(&launcher)) {
            return false;
        }
    }
    for (size_t i = 0U;
         i < sizeof(s_builtin_apps) / sizeof(s_builtin_apps[0]); ++i) {
        if (!append_app(&s_builtin_apps[i])) {
            return false;
        }
    }
    return true;
}

static void halt_dark(const char *stage, esp_err_t error)
{
    if (s_display_initialized) {
        (void)platform_display_set_brightness(0U);
    }
    ESP_LOGE(TAG, "P4_CONSOLE_OS HALT stage=%s error=%s",
             stage, esp_err_to_name(error));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

static uint32_t uptime_seconds(void)
{
    const int64_t microseconds = esp_timer_get_time();
    if (microseconds <= 0) {
        return 0U;
    }
    const uint64_t seconds = (uint64_t)microseconds / UINT64_C(1000000);
    return seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
}

static uint32_t free_kib(uint32_t capabilities)
{
    const size_t bytes = heap_caps_get_free_size(capabilities);
    return (uint32_t)(bytes / 1024U);
}

static console_shell_storage_state_t shell_storage_state(
    platform_game_storage_state_t state)
{
    switch (state) {
    case PLATFORM_GAME_STORAGE_APP_READY:
        return CONSOLE_STORAGE_READY;
    case PLATFORM_GAME_STORAGE_USB_HOST:
        return CONSOLE_STORAGE_USB_HOST;
    case PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED:
        return CONSOLE_STORAGE_FORMAT_REQUIRED;
    case PLATFORM_GAME_STORAGE_APP_MISSING:
        return CONSOLE_STORAGE_MISSING;
    case PLATFORM_GAME_STORAGE_APP_INVALID:
        return CONSOLE_STORAGE_INVALID;
    case PLATFORM_GAME_STORAGE_GAME_LOCKED:
        return CONSOLE_STORAGE_LOCKED;
    case PLATFORM_GAME_STORAGE_FAULT:
        return CONSOLE_STORAGE_FAULT;
    case PLATFORM_GAME_STORAGE_UNINITIALIZED:
    case PLATFORM_GAME_STORAGE_APP_SCANNING:
    case PLATFORM_GAME_STORAGE_TRANSITION:
    default:
        return CONSOLE_STORAGE_STARTING;
    }
}

static void set_doom_storage_state(platform_game_storage_state_t state)
{
    const char *subtitle = "STORAGE CHECKING";
    const bool ready = state == PLATFORM_GAME_STORAGE_APP_READY;
    switch (state) {
    case PLATFORM_GAME_STORAGE_APP_READY:
        subtitle = "SHAREWARE 1.9 / READY";
        break;
    case PLATFORM_GAME_STORAGE_USB_HOST:
        subtitle = "USB STORAGE ACTIVE";
        break;
    case PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED:
        subtitle = "HOST FORMAT REQUIRED";
        break;
    case PLATFORM_GAME_STORAGE_APP_MISSING:
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        subtitle = "COPY DOOM1.WAD TO MICROSD";
#else
        subtitle = "COPY DOOM1.WAD OVER USB";
#endif
        break;
    case PLATFORM_GAME_STORAGE_APP_INVALID:
        subtitle = "DOOM1.WAD INVALID";
        break;
    case PLATFORM_GAME_STORAGE_GAME_LOCKED:
        subtitle = "GAME STORAGE LOCKED";
        break;
    case PLATFORM_GAME_STORAGE_FAULT:
        subtitle = "STORAGE OFFLINE";
        break;
    case PLATFORM_GAME_STORAGE_UNINITIALIZED:
    case PLATFORM_GAME_STORAGE_APP_SCANNING:
    case PLATFORM_GAME_STORAGE_TRANSITION:
    default:
        break;
    }
    const size_t length = strlen(subtitle);
    const size_t copy = length < sizeof(s_doom_subtitle) - 1U
        ? length : sizeof(s_doom_subtitle) - 1U;
    memcpy(s_doom_subtitle, subtitle, copy);
    s_doom_subtitle[copy] = '\0';
    for (size_t index = 0U; index < s_app_count; ++index) {
        if (s_apps[index].id == CONSOLE_APP_DOOM) {
            s_apps[index].enabled = ready;
            break;
        }
    }
}

static void set_game_manager_update_state(platform_os_update_state_t state)
{
    const char *subtitle = "P4G + P4CART + OS";
    switch (state) {
    case PLATFORM_OS_UPDATE_READY:
        subtitle = "OS UPDATE READY - OPEN";
        break;
    case PLATFORM_OS_UPDATE_INVALID:
        subtitle = "BAD UPDATE - REMOVE";
        break;
    case PLATFORM_OS_UPDATE_UNAVAILABLE:
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        subtitle = "CHECK MICROSD UPDATE";
#else
        subtitle = "EJECT USB TO CHECK";
#endif
        break;
    case PLATFORM_OS_UPDATE_ABSENT:
    default:
        break;
    }
    const size_t length = strlen(subtitle);
    const size_t copy = length < sizeof(s_game_manager_subtitle) - 1U
        ? length : sizeof(s_game_manager_subtitle) - 1U;
    memcpy(s_game_manager_subtitle, subtitle, copy);
    s_game_manager_subtitle[copy] = '\0';
}

static void game_storage_init_snapshot(
    console_storage_init_state_t *out_state, esp_err_t *out_result)
{
    portENTER_CRITICAL(&s_game_storage_init_lock);
    *out_state = s_game_storage_init_state;
    *out_result = s_game_storage_init_result;
    portEXIT_CRITICAL(&s_game_storage_init_lock);
}

static void game_storage_init_publish(console_storage_init_state_t state,
                                      esp_err_t result)
{
    portENTER_CRITICAL(&s_game_storage_init_lock);
    s_game_storage_init_state = state;
    s_game_storage_init_result = result;
    portEXIT_CRITICAL(&s_game_storage_init_lock);
}

static void game_storage_init_worker(void *unused)
{
    (void)unused;
    const esp_err_t result = platform_game_storage_init();
    const int64_t elapsed_us =
        esp_timer_get_time() - s_game_storage_init_started_us;
    const unsigned low_water_bytes =
        (unsigned)uxTaskGetStackHighWaterMark(NULL);
    game_storage_init_publish(
        result == ESP_OK ? CONSOLE_STORAGE_INIT_READY
                         : CONSOLE_STORAGE_INIT_FAILED,
        result);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS STORAGE_INIT status=%s result=%s "
             "elapsed_ms=%" PRIi64 " worker_low_water_bytes=%u",
             result == ESP_OK ? "ready" : "degraded",
             esp_err_to_name(result), elapsed_us / INT64_C(1000),
             low_water_bytes);
    vTaskDelete(NULL);
}

static esp_err_t start_game_storage_initialization(void)
{
    console_storage_init_state_t state;
    esp_err_t previous_result;
    game_storage_init_snapshot(&state, &previous_result);
    (void)previous_result;
    if (state != CONSOLE_STORAGE_INIT_NOT_STARTED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_game_storage_init_started_us = esp_timer_get_time();
    game_storage_init_publish(CONSOLE_STORAGE_INIT_RUNNING, ESP_OK);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS STORAGE_INIT_BEGIN mode=background "
             "boot_screen=active writes=0");
    const BaseType_t created = xTaskCreate(
        game_storage_init_worker, "storage_init",
        CONSOLE_STORAGE_INIT_STACK_BYTES, NULL, tskIDLE_PRIORITY + 1U,
        NULL);
    if (created != pdPASS) {
        game_storage_init_publish(
            CONSOLE_STORAGE_INIT_FAILED, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void sync_game_storage(void)
{
    platform_game_storage_status_t status = {
        .state = PLATFORM_GAME_STORAGE_UNINITIALIZED,
        .last_error = ESP_OK,
    };
    console_storage_init_state_t init_state;
    esp_err_t init_result;
    game_storage_init_snapshot(&init_state, &init_result);
    if (init_state == CONSOLE_STORAGE_INIT_READY) {
        (void)platform_game_storage_refresh();
        if (platform_game_storage_get_status(&status) != ESP_OK) {
            status.state = PLATFORM_GAME_STORAGE_FAULT;
            status.last_error = ESP_FAIL;
        }
    } else if (init_state == CONSOLE_STORAGE_INIT_FAILED) {
        status.state = PLATFORM_GAME_STORAGE_FAULT;
        status.last_error = init_result;
    }
    const bool changed = !s_game_storage_status_seen ||
        status.state != s_game_storage_status.state ||
        status.usb_attached != s_game_storage_status.usb_attached ||
        status.generation != s_game_storage_status.generation ||
        status.last_error != s_game_storage_status.last_error;
    s_game_storage_status = status;
    s_game_storage_status_seen = true;
    set_doom_storage_state(status.state);
    const bool stored_games_ready = storage_app_owned();
    if (!stored_games_ready) {
        set_game_manager_update_state(PLATFORM_OS_UPDATE_UNAVAILABLE);
    }
    for (size_t index = 0U; index < s_app_count; ++index) {
        if (s_apps[index].id >= 100U) {
            s_apps[index].enabled = stored_games_ready;
        }
    }
    if (changed) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS GAME_STORAGE state=%s usb_attached=%u "
                 "generation=%lu capacity=%llu verified_writes=%lu "
                 "write_failures=%lu last_error=%s",
                 platform_game_storage_state_name(status.state),
                 status.usb_attached ? 1U : 0U,
                 (unsigned long)status.generation,
                 (unsigned long long)status.capacity_bytes,
                 (unsigned long)status.usb_verified_writes,
                 (unsigned long)status.usb_write_failures,
                 esp_err_to_name(status.last_error));
    }
}

static bool storage_app_owned(void)
{
    return s_game_storage_status.state == PLATFORM_GAME_STORAGE_APP_READY ||
        s_game_storage_status.state == PLATFORM_GAME_STORAGE_APP_MISSING ||
        s_game_storage_status.state == PLATFORM_GAME_STORAGE_APP_INVALID ||
        s_game_storage_status.state == PLATFORM_GAME_STORAGE_APP_SCANNING;
}

static bool catalog_needs_reload(void)
{
    return storage_app_owned() &&
        (!s_catalog_seen || s_catalog_storage_generation !=
            s_game_storage_status.generation);
}

static p4cart_scan_state_t p4cart_scan_state(void)
{
    portENTER_CRITICAL(&s_p4cart_scan_lock);
    const p4cart_scan_state_t state = s_p4cart_scan_state;
    portEXIT_CRITICAL(&s_p4cart_scan_lock);
    return state;
}

static bool p4cart_scan_running(void)
{
    return p4cart_scan_state() == P4CART_SCAN_RUNNING;
}

static bool p4cart_scan_needs_reload(void)
{
    return storage_app_owned() &&
        p4cart_scan_state() == P4CART_SCAN_IDLE &&
        (!s_p4cart_scan_seen || s_p4cart_catalog_generation !=
            s_game_storage_status.generation);
}

static void p4cart_scan_worker(void *unused)
{
    (void)unused;
    const p4_content_status_t result = p4_content_catalog_scan(
        PLATFORM_GAME_STORAGE_MOUNT_POINT, &s_p4cart_scan_staging);
    const unsigned low_water_bytes =
        (unsigned)uxTaskGetStackHighWaterMark(NULL);
    portENTER_CRITICAL(&s_p4cart_scan_lock);
    s_p4cart_scan_result = result;
    s_p4cart_scan_low_water_bytes = low_water_bytes;
    s_p4cart_scan_state = P4CART_SCAN_DONE;
    portEXIT_CRITICAL(&s_p4cart_scan_lock);
    vTaskSuspend(NULL);
}

static esp_err_t start_p4cart_scan(void)
{
    if (!storage_app_owned()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (p4cart_scan_state() != P4CART_SCAN_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_p4cart_scan_staging, 0, sizeof(s_p4cart_scan_staging));
    portENTER_CRITICAL(&s_p4cart_scan_lock);
    if (s_p4cart_scan_state != P4CART_SCAN_IDLE) {
        portEXIT_CRITICAL(&s_p4cart_scan_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_p4cart_scan_generation = s_game_storage_status.generation;
    s_p4cart_scan_state = P4CART_SCAN_RUNNING;
    portEXIT_CRITICAL(&s_p4cart_scan_lock);

    const BaseType_t created = xTaskCreateWithCaps(
        p4cart_scan_worker, "p4cart_scan",
        CONSOLE_P4CART_SCAN_STACK_BYTES, NULL, tskIDLE_PRIORITY + 1U,
        &s_p4cart_scan_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        portENTER_CRITICAL(&s_p4cart_scan_lock);
        s_p4cart_scan_state = P4CART_SCAN_IDLE;
        portEXIT_CRITICAL(&s_p4cart_scan_lock);
        s_p4cart_scan_seen = true;
        s_p4cart_catalog_generation = s_game_storage_status.generation;
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS P4CART_SCAN_DEGRADED "
                 "status=task-create-failed writes=0");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS P4CART_SCAN_BEGIN directory=%s/%s "
             "mode=background stack_bytes=%u generation=%lu writes=0",
             PLATFORM_GAME_STORAGE_MOUNT_POINT,
             P4_CONTENT_CART_DIRECTORY,
             (unsigned)CONSOLE_P4CART_SCAN_STACK_BYTES,
             (unsigned long)s_p4cart_scan_generation);
    return ESP_OK;
}

static bool finish_p4cart_scan(void)
{
    p4_content_status_t scan_result = P4_CONTENT_INVALID_ARGUMENT;
    uint32_t scan_generation = 0U;
    TaskHandle_t completed_task = NULL;
    portENTER_CRITICAL(&s_p4cart_scan_lock);
    if (s_p4cart_scan_state != P4CART_SCAN_DONE) {
        portEXIT_CRITICAL(&s_p4cart_scan_lock);
        return false;
    }
    scan_result = s_p4cart_scan_result;
    scan_generation = s_p4cart_scan_generation;
    completed_task = s_p4cart_scan_task;
    s_p4cart_scan_task = NULL;
    s_p4cart_scan_state = P4CART_SCAN_IDLE;
    portEXIT_CRITICAL(&s_p4cart_scan_lock);

    s_p4cart_catalog = s_p4cart_scan_staging;
    s_p4cart_scan_seen = true;
    s_p4cart_catalog_generation = scan_generation;
    if (completed_task != NULL) {
        vTaskDeleteWithCaps(completed_task);
    }
    if (scan_result != P4_CONTENT_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS P4CART_SCAN_DEGRADED status=%s "
                 "generation=%lu writes=0",
                 p4_content_status_name(scan_result),
                 (unsigned long)scan_generation);
        return true;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS P4CART_READY valid=%u rejected=%u "
             "candidates=%u truncated=%u generation=%lu "
             "worker_low_water_bytes=%u runtime=lua-pending writes=0",
             (unsigned)s_p4cart_catalog.valid_cart_count,
             (unsigned)s_p4cart_catalog.invalid_cart_count,
             (unsigned)s_p4cart_catalog.candidates_seen,
             s_p4cart_catalog.directory_truncated ? 1U : 0U,
             (unsigned long)scan_generation,
             s_p4cart_scan_low_water_bytes);
    return true;
}

static esp_err_t reload_game_catalog(void)
{
    const esp_err_t storage_result =
        platform_game_catalog_scan(&s_catalog_staging);
    memset(&s_update_staging, 0, sizeof(s_update_staging));
    const esp_err_t update_result =
        platform_os_update_inspect(&s_update_staging);
    const bool catalog_available = storage_result == ESP_OK;
    if (catalog_available) {
        s_game_catalog = s_catalog_staging;
    } else {
        memset(&s_game_catalog, 0, sizeof(s_game_catalog));
    }
    s_os_update_info = s_update_staging;
    set_game_manager_update_state(s_os_update_info.state);
    /* One scan attempt completes this storage generation. A card ownership or
     * mount change increments the generation and triggers the next attempt. */
    s_catalog_seen = true;
    s_catalog_storage_generation = s_game_storage_status.generation;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS GAME_CATALOG available=%u packages=%u "
             "valid=%u omitted=%lu generation=%lu source=microsd-only "
             "storage_result=%s update=%u result=%s",
             s_game_catalog.available ? 1U : 0U,
             (unsigned)s_game_catalog.entry_count,
             (unsigned)s_game_catalog.valid_count,
             (unsigned long)s_game_catalog.omitted_packages,
             (unsigned long)s_catalog_storage_generation,
             esp_err_to_name(storage_result),
             (unsigned)s_os_update_info.state,
             esp_err_to_name(catalog_available
                ? update_result : storage_result));
    return storage_result;
}

static void make_file_label(
    const char *name,
    char label[CONSOLE_SHELL_FILE_LABEL_MAX_BYTES])
{
    size_t output = 0U;
    bool truncated = false;
    while (name != NULL && name[output] != '\0' &&
           output + 1U < CONSOLE_SHELL_FILE_LABEL_MAX_BYTES) {
        const unsigned char byte = (unsigned char)name[output];
        const bool supported =
            (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z') ||
            (byte >= (unsigned char)'a' && byte <= (unsigned char)'z') ||
            (byte >= (unsigned char)'0' && byte <= (unsigned char)'9') ||
            byte == (unsigned char)' ' || byte == (unsigned char)'-' ||
            byte == (unsigned char)'.' || byte == (unsigned char)'_';
        label[output] = supported ? (char)byte : '?';
        ++output;
    }
    if (name != NULL && name[output] != '\0') {
        truncated = true;
    }
    if (output == 0U) {
        memcpy(label, "UNNAMED", sizeof("UNNAMED"));
        return;
    }
    if (truncated) {
        label[output - 1U] = '>';
    }
    label[output] = '\0';
}

static uint32_t file_size_kib(uint64_t bytes)
{
    uint64_t kib = bytes / UINT64_C(1024);
    if (bytes % UINT64_C(1024) != 0U) {
        ++kib;
    }
    return kib > UINT32_MAX ? UINT32_MAX : (uint32_t)kib;
}

static void make_file_path_label(
    const char *relative_path,
    char label[CONSOLE_SHELL_FILE_PATH_LABEL_MAX_BYTES])
{
    static const char prefix[] = "SD:/";
    memcpy(label, prefix, sizeof(prefix));
    if (relative_path == NULL || relative_path[0] == '\0') {
        return;
    }

    const size_t path_length = strnlen(
        relative_path, PLATFORM_GAME_STORAGE_RELATIVE_PATH_MAX_BYTES);
    const size_t prefix_length = sizeof(prefix) - 1U;
    const size_t available =
        CONSOLE_SHELL_FILE_PATH_LABEL_MAX_BYTES - prefix_length - 1U;
    const char *source = relative_path;
    size_t source_length = path_length;
    bool abbreviated = false;
    if (source_length > available) {
        const char *leaf = strrchr(relative_path, '/');
        source = leaf == NULL ? relative_path : leaf + 1U;
        source_length = strlen(source);
        memcpy(label + prefix_length, ".../", sizeof(".../") - 1U);
        const size_t marker_length = sizeof(".../") - 1U;
        const size_t leaf_available = available - marker_length;
        if (source_length > leaf_available) {
            source_length = leaf_available;
            abbreviated = true;
        }
        size_t output = prefix_length + marker_length;
        for (size_t index = 0U; index < source_length; ++index) {
            const unsigned char byte = (unsigned char)source[index];
            label[output++] = byte >= UINT8_C(0x20) &&
                    byte < UINT8_C(0x7f)
                ? (char)byte : '?';
        }
        if (abbreviated && output > prefix_length + marker_length) {
            label[output - 1U] = '>';
        }
        label[output] = '\0';
        return;
    }

    size_t output = prefix_length;
    for (size_t index = 0U; index < source_length; ++index) {
        const unsigned char byte = (unsigned char)source[index];
        label[output++] = byte >= UINT8_C(0x20) && byte < UINT8_C(0x7f)
            ? (char)byte : '?';
    }
    label[output] = '\0';
}

static esp_err_t reload_file_listing(
    console_shell_t *shell,
    console_shell_file_notice_t requested_notice)
{
    memset(&s_platform_file_listing, 0, sizeof(s_platform_file_listing));
    memset(&s_shell_file_listing, 0, sizeof(s_shell_file_listing));
    if (s_file_listing_revision != UINT32_MAX) {
        ++s_file_listing_revision;
    }
    s_shell_file_listing.revision = s_file_listing_revision;
    s_shell_file_listing.storage_generation =
        s_game_storage_status.generation;

    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (storage_app_owned()) {
        result = platform_game_storage_list_directory(
            s_file_directory, &s_platform_file_listing);
    }
    if (result == ESP_OK) {
        s_shell_file_listing.available = true;
        s_shell_file_listing.can_go_up = s_file_directory[0] != '\0';
        make_file_path_label(
            s_file_directory, s_shell_file_listing.path_label);
        s_shell_file_listing.storage_generation =
            s_platform_file_listing.storage_generation;
        s_shell_file_listing.hidden_entries =
            s_platform_file_listing.hidden_entries;
        s_shell_file_listing.omitted_entries =
            s_platform_file_listing.omitted_entries;
        for (size_t source = 0U;
             source < s_platform_file_listing.entry_count; ++source) {
            const platform_game_storage_file_entry_t *const input =
                &s_platform_file_listing.entries[source];
            if (s_shell_file_listing.entry_count >=
                CONSOLE_SHELL_FILE_MAX_ENTRIES) {
                if (s_shell_file_listing.omitted_entries != UINT32_MAX) {
                    ++s_shell_file_listing.omitted_entries;
                }
                continue;
            }
            console_shell_file_entry_t *const output =
                &s_shell_file_listing.entries[
                    s_shell_file_listing.entry_count++];
            output->source_index = (uint32_t)source;
            make_file_label(input->name, output->label);
            output->size_kib = file_size_kib(input->size_bytes);
            output->is_directory = input->is_directory;
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
            output->removable = false;
#else
            output->removable = !input->is_directory;
#endif
        }
        s_shell_file_listing.total_visible_entries =
            s_platform_file_listing.total_entries >=
                    s_platform_file_listing.hidden_entries
                ? s_platform_file_listing.total_entries -
                    s_platform_file_listing.hidden_entries
                : 0U;
    }

    if (!console_shell_set_file_listing(shell, &s_shell_file_listing)) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    console_shell_set_file_notice(
        shell, result == ESP_OK ? requested_notice : CONSOLE_FILE_NOTICE_ERROR);
    s_file_listing_seen = true;
    s_file_listing_storage_state = s_game_storage_status.state;
    s_file_listing_storage_generation = s_game_storage_status.generation;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS FILES_REFRESH directory=%s storage=%s available=%u "
             "visible=%lu hidden=%lu omitted=%lu generation=%lu result=%s",
             s_file_directory[0] != '\0' ? s_file_directory : "/",
             platform_game_storage_state_name(s_game_storage_status.state),
             s_shell_file_listing.available ? 1U : 0U,
             (unsigned long)s_shell_file_listing.total_visible_entries,
             (unsigned long)s_shell_file_listing.hidden_entries,
             (unsigned long)s_shell_file_listing.omitted_entries,
             (unsigned long)s_shell_file_listing.storage_generation,
             esp_err_to_name(result));
    return result;
}

static bool file_listing_needs_reload(void)
{
    return !s_file_listing_seen ||
        s_file_listing_storage_state != s_game_storage_status.state ||
        s_file_listing_storage_generation != s_game_storage_status.generation;
}

static esp_err_t reload_manager_listing(
    console_shell_t *shell,
    console_shell_file_notice_t requested_notice)
{
    memset(&s_manager_listing, 0, sizeof(s_manager_listing));
    if (s_manager_listing_revision != UINT32_MAX) {
        ++s_manager_listing_revision;
    }
    s_manager_listing.revision = s_manager_listing_revision;
    s_manager_listing.storage_generation =
        s_game_storage_status.generation;
    s_manager_listing.available = s_game_catalog.available;
    for (size_t index = 0U; index < s_game_catalog.entry_count; ++index) {
        if (s_manager_listing.entry_count >=
            CONSOLE_SHELL_FILE_MAX_ENTRIES) {
            ++s_manager_listing.omitted_entries;
            continue;
        }
        const platform_game_catalog_entry_t *const source =
            &s_game_catalog.entries[index];
        console_shell_file_entry_t *const target =
            &s_manager_listing.entries[s_manager_listing.entry_count++];
        target->source_index = (uint32_t)index;
        target->size_kib = file_size_kib(source->file_bytes);
        target->removable = true;
        if (source->valid) {
            const int written = snprintf(
                target->label, sizeof(target->label), "%s %s",
                source->package.title, source->package.version);
            if (written <= 0 || (size_t)written >= sizeof(target->label)) {
                make_file_label(source->package.title, target->label);
            }
        } else {
            char invalid[PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES + 5U];
            const int written = snprintf(
                invalid, sizeof(invalid), "BAD %s", source->file_name);
            make_file_label(
                written > 0 ? invalid : source->file_name, target->label);
        }
    }
    if (p4cart_scan_running()) {
        if (s_manager_listing.entry_count <
            CONSOLE_SHELL_FILE_MAX_ENTRIES) {
            console_shell_file_entry_t *const pending =
                &s_manager_listing.entries[s_manager_listing.entry_count++];
            pending->source_index = UINT32_MAX - 1U;
            memcpy(pending->label, "P4CART CHECKING",
                   sizeof("P4CART CHECKING"));
        } else {
            ++s_manager_listing.omitted_entries;
        }
    } else if (s_p4cart_scan_seen) {
        for (size_t index = 0U;
             index < s_p4cart_catalog.valid_cart_count; ++index) {
            if (s_manager_listing.entry_count >=
                CONSOLE_SHELL_FILE_MAX_ENTRIES) {
                ++s_manager_listing.omitted_entries;
                continue;
            }
            const p4_content_item_t *const source =
                &s_p4cart_catalog.carts[index];
            console_shell_file_entry_t *const target =
                &s_manager_listing.entries[s_manager_listing.entry_count++];
            target->source_index = UINT32_MAX - 2U - (uint32_t)index;
            target->size_kib = file_size_kib(source->size_bytes);
            char name[P4_CONTENT_NAME_BYTES + 6U];
            const int written = snprintf(
                name, sizeof(name), "CART %s", source->name);
            make_file_label(written > 0 ? name : source->name, target->label);
        }
        if (s_p4cart_catalog.invalid_cart_count > 0U &&
            s_manager_listing.entry_count <
                CONSOLE_SHELL_FILE_MAX_ENTRIES) {
            console_shell_file_entry_t *const rejected =
                &s_manager_listing.entries[s_manager_listing.entry_count++];
            rejected->source_index = UINT32_MAX - 1U;
            const int written = snprintf(
                rejected->label, sizeof(rejected->label),
                "BAD P4CARTS %u",
                (unsigned)s_p4cart_catalog.invalid_cart_count);
            if (written <= 0 ||
                (size_t)written >= sizeof(rejected->label)) {
                memcpy(rejected->label, "BAD P4CART",
                       sizeof("BAD P4CART"));
            }
        }
        if (s_p4cart_catalog.directory_truncated &&
            s_manager_listing.omitted_entries != UINT32_MAX) {
            ++s_manager_listing.omitted_entries;
        }
    }
    if (s_os_update_info.state == PLATFORM_OS_UPDATE_READY ||
        s_os_update_info.state == PLATFORM_OS_UPDATE_INVALID) {
        if (s_manager_listing.entry_count <
            CONSOLE_SHELL_FILE_MAX_ENTRIES) {
            console_shell_file_entry_t *const update =
                &s_manager_listing.entries[s_manager_listing.entry_count++];
            update->source_index = UINT32_MAX;
            update->size_kib = file_size_kib(
                s_os_update_info.image_bytes);
            update->installable =
                s_os_update_info.state == PLATFORM_OS_UPDATE_READY;
            update->removable = !update->installable;
            if (update->installable) {
                const int written = snprintf(
                    update->label, sizeof(update->label), "OS %s",
                    s_os_update_info.version);
                if (written <= 0 ||
                    (size_t)written >= sizeof(update->label)) {
                    memcpy(update->label, "OS UPDATE READY",
                           sizeof("OS UPDATE READY"));
                }
            } else {
                memcpy(update->label, "BAD OS UPDATE",
                       sizeof("BAD OS UPDATE"));
            }
        } else {
            ++s_manager_listing.omitted_entries;
        }
    }
    s_manager_listing.total_visible_entries =
        (uint32_t)s_manager_listing.entry_count;
    if (!console_shell_set_file_listing(shell, &s_manager_listing)) {
        console_shell_set_file_notice(shell, CONSOLE_FILE_NOTICE_ERROR);
        return ESP_ERR_INVALID_RESPONSE;
    }
    console_shell_set_file_notice(shell, requested_notice);
    return ESP_OK;
}

static void rebuild_shell_registry(console_shell_t *shell)
{
    const console_page_t previous_page = shell->page;
    const uint32_t previous_app = shell->active_app_id;
    const bool previous_all_programs = shell->home_all_programs;
    const p4_terminal_t previous_terminal = shell->terminal;
    char previous_folder[CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES];
    memcpy(previous_folder, shell->home_folder_path,
           sizeof(previous_folder));
    if (!build_app_registry() ||
        !console_shell_init(shell, s_apps, s_app_count)) {
        halt_dark("catalog-app-registry", ESP_ERR_INVALID_ARG);
    }
    memcpy(shell->home_folder_path, previous_folder,
           sizeof(shell->home_folder_path));
    shell->home_all_programs = previous_all_programs;
    shell->terminal = previous_terminal;
    console_shell_set_achievement_catalog(shell, &s_achievements);
    (void)console_shell_set_save_catalog(shell, &s_saves);
    if (previous_page != CONSOLE_PAGE_HOME &&
        previous_page != CONSOLE_PAGE_EXTERNAL) {
        for (size_t index = 0U; index < s_app_count; ++index) {
            if (s_apps[index].id == previous_app &&
                s_apps[index].page == previous_page) {
                shell->page = previous_page;
                shell->active_app_id = previous_app;
                shell->dirty = true;
                break;
            }
        }
    }
    if (shell->page == CONSOLE_PAGE_FILES) {
        s_file_listing_seen = false;
    }
    const console_shell_runtime_info_t current_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &current_runtime);
}

static void handle_manager_action(
    console_shell_t *shell,
    const console_shell_action_t *action)
{
    if (p4cart_scan_running()) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS GAME_ACTION_DEFERRED "
                 "reason=p4cart-scan action=%u",
                 (unsigned)action->type);
        console_shell_set_file_notice(shell, CONSOLE_FILE_NOTICE_ERROR);
        return;
    }
    if (action->type == CONSOLE_ACTION_GAME_REFRESH) {
        sync_game_storage();
        (void)reload_game_catalog();
        s_p4cart_scan_seen = false;
        (void)start_p4cart_scan();
        rebuild_shell_registry(shell);
        (void)reload_manager_listing(
            shell, CONSOLE_FILE_NOTICE_REFRESHED);
        return;
    }
    if (action->type == CONSOLE_ACTION_OS_UPDATE_INSTALL) {
        console_shell_set_file_notice(shell, CONSOLE_FILE_NOTICE_UPDATING);
        const esp_err_t shown = present(shell);
        if (shown != ESP_OK) {
            halt_dark("update-status-frame", shown);
        }
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS UPDATE_BEGIN version=%s bytes=%lu "
                 "source=%s target=inactive-ota",
                 s_os_update_info.version,
                 (unsigned long)s_os_update_info.image_bytes,
                 PLATFORM_OS_UPDATE_RELATIVE_PATH);
        const esp_err_t result =
            platform_os_update_install(&s_os_update_info);
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS UPDATE_END version=%s result=%s "
                 "reboot=%u",
                 s_os_update_info.version, esp_err_to_name(result),
                 result == ESP_OK ? 1U : 0U);
        if (result == ESP_OK) {
            const esp_err_t cleanup =
                platform_game_storage_remove_update_file(
                    PLATFORM_OS_UPDATE_FILE_NAME);
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS UPDATE_PACKAGE_REMOVE result=%s",
                     esp_err_to_name(cleanup));
            vTaskDelay(pdMS_TO_TICKS(400U));
            esp_restart();
        }
        console_shell_set_file_notice(shell, CONSOLE_FILE_NOTICE_ERROR);
        return;
    }
    if (action->type != CONSOLE_ACTION_GAME_REMOVE) {
        return;
    }
    esp_err_t result;
    if (action->file_source_index == UINT32_MAX) {
        result = platform_game_storage_remove_update_file(
            PLATFORM_OS_UPDATE_FILE_NAME);
    } else {
        result = platform_game_catalog_remove(
            &s_game_catalog, action->file_source_index);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS GAME_REMOVE source=%lu result=%s",
             (unsigned long)action->file_source_index,
             esp_err_to_name(result));
    sync_game_storage();
    (void)reload_game_catalog();
    rebuild_shell_registry(shell);
    (void)reload_manager_listing(
        shell, result == ESP_OK
            ? CONSOLE_FILE_NOTICE_DELETED : CONSOLE_FILE_NOTICE_ERROR);
}

static void handle_file_action(
    console_shell_t *shell,
    const console_shell_action_t *action)
{
    if (p4cart_scan_running()) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS FILE_ACTION_DEFERRED "
                 "reason=p4cart-scan action=%u",
                 (unsigned)action->type);
        console_shell_set_file_notice(shell, CONSOLE_FILE_NOTICE_ERROR);
        return;
    }
    if (action->type == CONSOLE_ACTION_FILE_REFRESH) {
        sync_game_storage();
        (void)reload_file_listing(shell, CONSOLE_FILE_NOTICE_REFRESHED);
        return;
    }
    if (action->type == CONSOLE_ACTION_FILE_UP) {
        if (s_file_directory[0] == '\0') {
            console_shell_set_file_notice(shell, CONSOLE_FILE_NOTICE_ERROR);
            return;
        }
        char *const slash = strrchr(s_file_directory, '/');
        if (slash == NULL) {
            s_file_directory[0] = '\0';
        } else {
            *slash = '\0';
        }
        shell->file_selected_index = 0U;
        shell->file_first_visible = 0U;
        (void)reload_file_listing(shell, CONSOLE_FILE_NOTICE_NONE);
        return;
    }
    if (action->type == CONSOLE_ACTION_FILE_OPEN) {
        esp_err_t result = ESP_ERR_INVALID_ARG;
        const size_t source = action->file_source_index;
        if (source < s_platform_file_listing.entry_count &&
            s_platform_file_listing.entries[source].is_directory) {
            const char *const name =
                s_platform_file_listing.entries[source].name;
            char candidate[
                PLATFORM_GAME_STORAGE_RELATIVE_PATH_MAX_BYTES] = {0};
            const int written = s_file_directory[0] == '\0'
                ? snprintf(candidate, sizeof(candidate), "%s", name)
                : snprintf(candidate, sizeof(candidate), "%s/%s",
                           s_file_directory, name);
            if (written > 0 && (size_t)written < sizeof(candidate)) {
                char previous[PLATFORM_GAME_STORAGE_RELATIVE_PATH_MAX_BYTES];
                memcpy(previous, s_file_directory, sizeof(previous));
                memcpy(s_file_directory, candidate, sizeof(candidate));
                shell->file_selected_index = 0U;
                shell->file_first_visible = 0U;
                result = reload_file_listing(
                    shell, CONSOLE_FILE_NOTICE_NONE);
                if (result != ESP_OK) {
                    memcpy(s_file_directory, previous, sizeof(previous));
                    (void)reload_file_listing(
                        shell, CONSOLE_FILE_NOTICE_ERROR);
                }
            }
        }
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS FILES_OPEN source=%lu directory=%s result=%s",
                 (unsigned long)source,
                 s_file_directory[0] != '\0' ? s_file_directory : "/",
                 esp_err_to_name(result));
        return;
    }
    if (action->type != CONSOLE_ACTION_FILE_DELETE) {
        return;
    }

    esp_err_t result = ESP_ERR_INVALID_ARG;
    char label[CONSOLE_SHELL_FILE_LABEL_MAX_BYTES] = "INVALID";
    const size_t source = action->file_source_index;
    if (source < s_platform_file_listing.entry_count) {
        const platform_game_storage_file_entry_t *const entry =
            &s_platform_file_listing.entries[source];
        make_file_label(entry->name, label);
        if (!entry->is_hidden && !entry->is_directory) {
            result = platform_game_storage_remove_file(
                s_file_directory, entry->name);
        }
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS FILES_DELETE directory=%s file=%s result=%s",
             s_file_directory[0] != '\0' ? s_file_directory : "/",
             label, esp_err_to_name(result));
    sync_game_storage();
    (void)reload_file_listing(
        shell, result == ESP_OK
            ? CONSOLE_FILE_NOTICE_DELETED : CONSOLE_FILE_NOTICE_ERROR);
}

static void handle_usb_mode_action(
    console_shell_t *shell,
    const console_shell_action_t *action)
{
    const bool enabled =
        action->type == CONSOLE_ACTION_USB_MODE_ENABLE;
    if (enabled && p4cart_scan_running()) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS USB_MODE_DEFERRED reason=p4cart-scan");
        return;
    }
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
    CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE && \
    CONFIG_P4_WAVESHARE_H2_RUNTIME_ROLE_SWITCH
    esp_err_t result = ESP_OK;
    if (enabled) {
        result = stop_usb_input_for_role_switch();
        (void)console_shell_handle_buttons(shell, 0U);
        if (result == ESP_OK) {
            result = platform_game_storage_set_usb_mode(true);
        }
        if (result != ESP_OK) {
            platform_game_storage_status_t failed_status;
            memset(&failed_status, 0, sizeof(failed_status));
            const esp_err_t status_result =
                platform_game_storage_get_status(&failed_status);
            const esp_err_t recovery =
                status_result == ESP_OK && !failed_status.usb_driver_running
                ? start_usb_input() : ESP_ERR_INVALID_STATE;
            ESP_LOGE(TAG,
                     "P4_CONSOLE_OS USB_ROLE_FAIL target=usb-drive "
                     "error=%s controller_recovery=%s",
                     esp_err_to_name(result), esp_err_to_name(recovery));
        }
    } else {
        result = platform_game_storage_set_usb_mode(false);
        if (result == ESP_OK) {
            result = start_usb_input();
        }
    }
#else
    const esp_err_t result = platform_game_storage_set_usb_mode(enabled);
#endif
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS USB_MODE requested=%s result=%s "
             "target_role=%s",
             enabled ? "on" : "off", esp_err_to_name(result),
             enabled ? "usb-drive" : "controller-host");
    s_file_listing_seen = false;
    if (result == ESP_OK && !enabled) {
        s_catalog_seen = false;
        s_p4cart_scan_seen = false;
    }
    sync_game_storage();
    const console_shell_runtime_info_t current_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &current_runtime);
}

static void handle_audio_volume_action(
    console_shell_t *shell,
    const console_shell_action_t *action)
{
    if (shell == NULL || action == NULL) {
        return;
    }
    esp_err_t result = ESP_ERR_INVALID_ARG;
    const char *setting = "invalid";
    if (action->type == CONSOLE_ACTION_BOOT_VOLUME_SET) {
        setting = "boot";
        result = platform_console_settings_set_boot_volume(
            &s_console_settings, action->volume_step);
    } else if (action->type == CONSOLE_ACTION_GAME_VOLUME_SET) {
        setting = "game";
        result = platform_console_settings_set_game_volume(
            &s_console_settings, action->volume_step);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS AUDIO_SETTING name=%s level=%u "
             "persistent=%u result=%s",
             setting, (unsigned)action->volume_step,
             s_console_settings.persistent ? 1U : 0U,
             esp_err_to_name(result));
    const console_shell_runtime_info_t current_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &current_runtime);
}

static console_shell_runtime_info_t runtime_info(void)
{
    const uint64_t capacity_kib = s_game_storage_status.capacity_bytes / 1024U;
    const console_shell_runtime_info_t info = {
        .uptime_seconds = uptime_seconds(),
        .internal_free_kib = free_kib(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .psram_free_kib = free_kib(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .game_storage_kib = capacity_kib > UINT32_MAX
            ? UINT32_MAX : (uint32_t)capacity_kib,
        .game_storage_state = shell_storage_state(
            s_game_storage_status.state),
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
        .board_kind = CONSOLE_BOARD_OLIMEX_P4_PC,
        .touch_ready = false,
#else
        .board_kind =
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
            CONSOLE_BOARD_WAVESHARE_4_3,
#else
            CONSOLE_BOARD_ELECROW_10,
#endif
        .touch_ready = s_touch_ready,
#endif
#if P4_CONSOLE_USB_INPUT
        .controller_ready = s_gamepad_connected,
        .keyboard_ready = s_keyboard_connected,
        .mouse_ready = s_mouse_connected,
#else
        .controller_ready = false,
        .keyboard_ready = false,
        .mouse_ready = false,
#endif
        .sd_card_storage =
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
            true,
#else
            false,
#endif
        /* Compiled handoff only; the shell itself never starts audio. */
        .audio_handoff_ready = true,
        .game_storage_usb_attached = s_game_storage_status.usb_attached,
        .usb_storage_supported =
            s_game_storage_status.usb_mode_supported,
        .usb_storage_eject_safe =
            s_game_storage_status.usb_host_ejected,
        .usb_drive_active = s_game_storage_status.usb_driver_running,
#if P4_CONSOLE_USB_INPUT
        .usb_input_host_active = s_gamepad_ready,
#else
        .usb_input_host_active = false,
#endif
        .doom_wad_ready =
            s_game_storage_status.state == PLATFORM_GAME_STORAGE_APP_READY,
        .content_scan_complete = s_catalog_seen,
        .usb_content_ready = false,
        .multiplayer_core_ready = true,
#if P4_CONSOLE_USB_INPUT
        .physical_keyboard_ready = s_keyboard_connected,
#else
        .physical_keyboard_ready = false,
#endif
        .valid_cart_count = s_game_catalog.valid_count > UINT16_MAX
            ? UINT16_MAX : (uint16_t)s_game_catalog.valid_count,
        .builtin_game_count = (uint16_t)(
            1U + sizeof(s_builtin_apps) / sizeof(s_builtin_apps[0])),
        .boot_volume_step = s_console_settings.boot_volume_step,
        .game_volume_step = s_console_settings.game_volume_step,
        .audio_settings_persistent = s_console_settings.persistent,
    };
    return info;
}

#if P4_CONSOLE_USB_INPUT
static bool gamepad_button_pressed(const gamepad_state_t *state,
                                   gamepad_button_t button)
{
    return state != NULL &&
        (state->buttons & GAMEPAD_BUTTON_MASK(button)) != 0U;
}

static uint32_t gamepad_p4_buttons(const gamepad_state_t *state)
{
    if (state == NULL || state->connected == 0U) {
        return 0U;
    }
    uint32_t buttons = 0U;
    const bool up = (state->dpad & GAMEPAD_DPAD_UP) != 0U ||
        state->left_y <= -CONSOLE_GAMEPAD_STICK_THRESHOLD;
    const bool down = (state->dpad & GAMEPAD_DPAD_DOWN) != 0U ||
        state->left_y >= CONSOLE_GAMEPAD_STICK_THRESHOLD;
    const bool left = (state->dpad & GAMEPAD_DPAD_LEFT) != 0U ||
        state->left_x <= -CONSOLE_GAMEPAD_STICK_THRESHOLD;
    const bool right = (state->dpad & GAMEPAD_DPAD_RIGHT) != 0U ||
        state->left_x >= CONSOLE_GAMEPAD_STICK_THRESHOLD;
    if (up != down) {
        buttons |= up ? P4_BUTTON_UP : P4_BUTTON_DOWN;
    }
    if (left != right) {
        buttons |= left ? P4_BUTTON_LEFT : P4_BUTTON_RIGHT;
    }
    if (gamepad_button_pressed(state, GAMEPAD_BUTTON_SOUTH)) {
        buttons |= P4_BUTTON_A;
    }
    if (gamepad_button_pressed(state, GAMEPAD_BUTTON_EAST)) {
        buttons |= P4_BUTTON_B;
    }
    if (gamepad_button_pressed(state, GAMEPAD_BUTTON_START)) {
        buttons |= P4_BUTTON_START;
    }
    if (gamepad_button_pressed(state, GAMEPAD_BUTTON_BACK)) {
        buttons |= P4_BUTTON_BACK;
    }
    return buttons;
}

static bool read_gamepad_snapshot(platform_gamepad_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_gamepad_ready) {
        s_gamepad_connected = false;
        return false;
    }
    if (s_gamepad_polls != UINT32_MAX) {
        ++s_gamepad_polls;
    }
    const esp_err_t result = platform_gamepad_usb_get_snapshot(snapshot);
    const bool valid = result == ESP_OK &&
        snapshot->version == PLATFORM_GAMEPAD_SNAPSHOT_VERSION &&
        snapshot->size == sizeof(*snapshot) &&
        snapshot->state.version == GAMEPAD_STATE_VERSION &&
        snapshot->state.size == sizeof(snapshot->state);
    if (!valid) {
        s_gamepad_connected = false;
        if (s_gamepad_poll_failures != UINT32_MAX) {
            ++s_gamepad_poll_failures;
        }
        if (s_gamepad_poll_failures == 1U ||
            s_gamepad_poll_failures % 120U == 0U) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS GAMEPAD_POLL_FAIL count=%lu error=%s",
                     (unsigned long)s_gamepad_poll_failures,
                     esp_err_to_name(result));
        }
        memset(snapshot, 0, sizeof(*snapshot));
        return false;
    }
    s_gamepad_connected = snapshot->state.connected != 0U;
    return s_gamepad_connected;
}

static uint32_t gamepad_shell_buttons(const gamepad_state_t *state)
{
    const uint32_t game = gamepad_p4_buttons(state);
    uint32_t shell = 0U;
    if ((game & P4_BUTTON_UP) != 0U) {
        shell |= CONSOLE_BUTTON_UP;
    }
    if ((game & P4_BUTTON_DOWN) != 0U) {
        shell |= CONSOLE_BUTTON_DOWN;
    }
    if ((game & P4_BUTTON_LEFT) != 0U) {
        shell |= CONSOLE_BUTTON_LEFT;
    }
    if ((game & P4_BUTTON_RIGHT) != 0U) {
        shell |= CONSOLE_BUTTON_RIGHT;
    }
    if ((game & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
        shell |= CONSOLE_BUTTON_ACCEPT;
    }
    if ((game & (P4_BUTTON_B | P4_BUTTON_BACK)) != 0U) {
        shell |= CONSOLE_BUTTON_BACK;
    }
    if (gamepad_button_pressed(state, GAMEPAD_BUTTON_NORTH)) {
        shell |= CONSOLE_BUTTON_REFRESH;
    }
    return shell;
}

static bool read_aux_input_snapshot(platform_usb_input_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_gamepad_ready) {
        s_keyboard_connected = false;
        s_mouse_connected = false;
        return false;
    }
    const esp_err_t result =
        platform_gamepad_usb_get_input_snapshot(snapshot);
    const bool valid = result == ESP_OK &&
        snapshot->version == PLATFORM_USB_INPUT_SNAPSHOT_VERSION &&
        snapshot->size == sizeof(*snapshot) &&
        snapshot->keyboard.connected <= 1U &&
        snapshot->mouse.connected <= 1U;
    if (!valid) {
        s_keyboard_connected = false;
        s_mouse_connected = false;
        if (s_aux_input_poll_failures != UINT32_MAX) {
            ++s_aux_input_poll_failures;
        }
        if (s_aux_input_poll_failures == 1U ||
            s_aux_input_poll_failures % 120U == 0U) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS USB_INPUT_POLL_FAIL count=%lu error=%s",
                     (unsigned long)s_aux_input_poll_failures,
                     esp_err_to_name(result));
        }
        memset(snapshot, 0, sizeof(*snapshot));
        return false;
    }
    s_keyboard_connected = snapshot->keyboard.connected != 0U;
    s_mouse_connected = snapshot->mouse.connected != 0U;
    return true;
}

static bool key_down(const platform_usb_keyboard_state_t *keyboard,
                     platform_usb_key_usage_t key)
{
    return platform_usb_keyboard_key_down(keyboard, (uint8_t)key);
}

static bool keyboard_contains_usage(
    const uint8_t keys[PLATFORM_USB_KEYBOARD_BOOT_KEY_COUNT], uint8_t usage)
{
    for (size_t index = 0U;
         index < PLATFORM_USB_KEYBOARD_BOOT_KEY_COUNT; ++index) {
        if (keys[index] == usage) {
            return true;
        }
    }
    return false;
}

static char keyboard_usage_character(uint8_t usage)
{
    if (usage >= UINT8_C(0x04) && usage <= UINT8_C(0x1d)) {
        return (char)('A' + (char)(usage - UINT8_C(0x04)));
    }
    if (usage >= UINT8_C(0x1e) && usage <= UINT8_C(0x26)) {
        return (char)('1' + (char)(usage - UINT8_C(0x1e)));
    }
    if (usage == UINT8_C(0x27)) {
        return '0';
    }
    if (usage == (uint8_t)PLATFORM_USB_KEY_SPACE) {
        return ' ';
    }
    if (usage == (uint8_t)PLATFORM_USB_KEY_BACKSPACE) {
        return '\b';
    }
    if (usage == (uint8_t)PLATFORM_USB_KEY_ENTER) {
        return '\n';
    }
    return '\0';
}

static void service_terminal_keyboard(
    console_shell_t *shell,
    const platform_usb_keyboard_state_t *keyboard)
{
    if (keyboard == NULL || keyboard->connected == 0U) {
        memset(s_previous_terminal_keys, 0,
               sizeof(s_previous_terminal_keys));
        s_terminal_keyboard_session = 0U;
        return;
    }
    if (s_terminal_keyboard_session != keyboard->session) {
        memset(s_previous_terminal_keys, 0,
               sizeof(s_previous_terminal_keys));
        s_terminal_keyboard_session = keyboard->session;
    }
    if (shell != NULL && shell->page == CONSOLE_PAGE_TERMINAL) {
        for (size_t index = 0U;
             index < PLATFORM_USB_KEYBOARD_BOOT_KEY_COUNT; ++index) {
            const uint8_t usage = keyboard->keys[index];
            if (usage == 0U || keyboard_contains_usage(
                    s_previous_terminal_keys, usage)) {
                continue;
            }
            const char character = keyboard_usage_character(usage);
            if (character != '\0') {
                (void)console_shell_handle_text_key(shell, character);
            }
        }
    }
    memcpy(s_previous_terminal_keys, keyboard->keys,
           sizeof(s_previous_terminal_keys));
}

static uint32_t keyboard_p4_buttons(
    const platform_usb_keyboard_state_t *keyboard)
{
    if (keyboard == NULL || keyboard->connected == 0U) {
        return 0U;
    }
    uint32_t buttons = 0U;
    const bool up = key_down(keyboard, PLATFORM_USB_KEY_UP) ||
        key_down(keyboard, PLATFORM_USB_KEY_W);
    const bool down = key_down(keyboard, PLATFORM_USB_KEY_DOWN) ||
        key_down(keyboard, PLATFORM_USB_KEY_S);
    const bool left = key_down(keyboard, PLATFORM_USB_KEY_LEFT) ||
        key_down(keyboard, PLATFORM_USB_KEY_A);
    const bool right = key_down(keyboard, PLATFORM_USB_KEY_RIGHT) ||
        key_down(keyboard, PLATFORM_USB_KEY_D);
    if (up != down) {
        buttons |= up ? P4_BUTTON_UP : P4_BUTTON_DOWN;
    }
    if (left != right) {
        buttons |= left ? P4_BUTTON_LEFT : P4_BUTTON_RIGHT;
    }
    if (key_down(keyboard, PLATFORM_USB_KEY_Z) ||
        key_down(keyboard, PLATFORM_USB_KEY_SPACE)) {
        buttons |= P4_BUTTON_A;
    }
    if (key_down(keyboard, PLATFORM_USB_KEY_X)) {
        buttons |= P4_BUTTON_B;
    }
    if (key_down(keyboard, PLATFORM_USB_KEY_ENTER)) {
        buttons |= P4_BUTTON_START;
    }
    if (key_down(keyboard, PLATFORM_USB_KEY_ESCAPE) ||
        key_down(keyboard, PLATFORM_USB_KEY_BACKSPACE)) {
        buttons |= P4_BUTTON_BACK;
    }
    return buttons;
}

static uint32_t keyboard_shell_buttons(
    const platform_usb_keyboard_state_t *keyboard)
{
    const uint32_t game = keyboard_p4_buttons(keyboard);
    uint32_t shell = 0U;
    if ((game & P4_BUTTON_UP) != 0U) {
        shell |= CONSOLE_BUTTON_UP;
    }
    if ((game & P4_BUTTON_DOWN) != 0U) {
        shell |= CONSOLE_BUTTON_DOWN;
    }
    if ((game & P4_BUTTON_LEFT) != 0U) {
        shell |= CONSOLE_BUTTON_LEFT;
    }
    if ((game & P4_BUTTON_RIGHT) != 0U) {
        shell |= CONSOLE_BUTTON_RIGHT;
    }
    if ((game & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
        shell |= CONSOLE_BUTTON_ACCEPT;
    }
    if ((game & (P4_BUTTON_B | P4_BUTTON_BACK)) != 0U) {
        shell |= CONSOLE_BUTTON_BACK;
    }
    if (key_down(keyboard, PLATFORM_USB_KEY_F5) ||
        key_down(keyboard, PLATFORM_USB_KEY_R)) {
        shell |= CONSOLE_BUTTON_REFRESH;
    }
    return shell;
}

static uint16_t move_pointer_axis(uint16_t current, int32_t delta,
                                  uint16_t limit)
{
    const int64_t moved = (int64_t)current + delta;
    if (moved <= 0) {
        return 0U;
    }
    if (moved >= (int64_t)limit) {
        return (uint16_t)(limit - 1U);
    }
    return (uint16_t)moved;
}

static void neutralize_usb_input_state(void)
{
    s_gamepad_ready = false;
    s_gamepad_connected = false;
    s_keyboard_connected = false;
    s_mouse_connected = false;
    s_terminal_keyboard_session = 0U;
    memset(s_previous_terminal_keys, 0, sizeof(s_previous_terminal_keys));
    s_mouse_x = CONSOLE_SHELL_LAYOUT_WIDTH / 2U;
    s_mouse_y = CONSOLE_SHELL_LAYOUT_HEIGHT / 2U;
}

/*
 * The generated external-port overlay references this symbol weakly. Keeping
 * ownership in Console OS lets the application arm durable recovery before
 * Host/HID starts and fail closed if the prior boot did not reach stability.
 */
bool p4_usb_ext_port_enum_retry_allowed(void)
{
    return s_usb_enum_probe_allowed;
}

/* The generated HCD overlay uses the same durable one-boot decision. */
bool p4_usb_hs_fsls_reapply_allowed(void)
{
    return s_usb_enum_probe_allowed;
}

static void begin_usb_enum_probe_or_suppress(void)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
    CONFIG_P4_WAVESHARE_H2_FORCE_FULL_SPEED_HOST
    bool allowed = false;
    bool prior_boot_incomplete = false;
    const esp_err_t result =
        platform_console_settings_begin_usb_enum_probe(
            &s_console_settings, &allowed, &prior_boot_incomplete);
    s_usb_enum_probe_allowed = result == ESP_OK && allowed;
    s_usb_enum_probe_started = result == ESP_OK;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS USB_ENUM_GUARD state=%s allowed=%u "
             "prior_boot_incomplete=%u result=%s",
             result != ESP_OK ? "fail-closed" :
                 (prior_boot_incomplete ? "safe-mode" : "armed"),
             s_usb_enum_probe_allowed ? 1U : 0U,
             prior_boot_incomplete ? 1U : 0U,
             esp_err_to_name(result));
#else
    s_usb_enum_probe_allowed = false;
#endif
}

static void confirm_usb_enum_probe_after_stable_runtime(void)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
    CONFIG_P4_WAVESHARE_H2_FORCE_FULL_SPEED_HOST
    if (!s_usb_enum_probe_started || s_usb_enum_probe_confirm_attempted ||
        s_loop_count < CONSOLE_USB_ENUM_GUARD_CONFIRM_LOOPS) {
        return;
    }
    s_usb_enum_probe_confirm_attempted = true;
    const esp_err_t result =
        platform_console_settings_confirm_usb_enum_probe(
            &s_console_settings);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS USB_ENUM_GUARD state=%s loops=%lu result=%s",
             result == ESP_OK ? "confirmed" : "confirm-failed",
             (unsigned long)s_loop_count, esp_err_to_name(result));
#endif
}

static esp_err_t start_usb_input(void)
{
    if (s_gamepad_ready) {
        return ESP_OK;
    }
    esp_err_t result = platform_usb_host_start(NULL);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS USB_INPUT_DEGRADED stage=usb-host error=%s",
                 esp_err_to_name(result));
        neutralize_usb_input_state();
        return result;
    }
    result = platform_gamepad_usb_start();
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS USB_INPUT_DEGRADED stage=hid error=%s",
                 esp_err_to_name(result));
        (void)platform_usb_host_stop(pdMS_TO_TICKS(1000U));
        neutralize_usb_input_state();
        return result;
    }
    result = platform_usb_host_enable_root_port();
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS USB_INPUT_DEGRADED stage=root-port error=%s",
                 esp_err_to_name(result));
        if (platform_usb_host_quiesce() == ESP_OK) {
            (void)platform_gamepad_usb_stop(pdMS_TO_TICKS(1000U));
            (void)platform_usb_host_stop(pdMS_TO_TICKS(1000U));
        }
        neutralize_usb_input_state();
        return result;
    }
    s_gamepad_ready = true;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS USB_INPUT_READY "
             "classes=gamepad transport=usb-hid topology=%s "
             "firmware_vbus_source=0",
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
             "integrated-powered-hub"
#else
             "waveshare-h2-self-powered-test"
#endif
    );
    return ESP_OK;
}

static esp_err_t stop_usb_input_for_role_switch(void)
{
    if (!s_gamepad_ready) {
        neutralize_usb_input_state();
        return ESP_OK;
    }

    esp_err_t result = platform_usb_host_quiesce();
    /* No stale key/button/mouse state survives even a failed teardown. */
    neutralize_usb_input_state();
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS USB_ROLE_STOP_FAIL stage=quiesce error=%s",
                 esp_err_to_name(result));
        return result;
    }
    result = platform_gamepad_usb_stop(pdMS_TO_TICKS(1500U));
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS USB_ROLE_STOP_FAIL stage=hid error=%s",
                 esp_err_to_name(result));
        return result;
    }
    result = platform_usb_host_stop(pdMS_TO_TICKS(1500U));
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS USB_ROLE_STOP_FAIL stage=host error=%s",
                 esp_err_to_name(result));
        return result;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS USB_ROLE_STOPPED old=controller-host "
             "input=neutral h2_owner=none");
    return ESP_OK;
}

static void create_usb_input_or_continue(void)
{
    (void)start_usb_input();
}

static console_shell_action_t poll_usb_input(console_shell_t *shell)
{
    platform_gamepad_snapshot_t gamepad;
    uint32_t buttons = read_gamepad_snapshot(&gamepad)
        ? gamepad_shell_buttons(&gamepad.state) : 0U;
    platform_usb_input_snapshot_t input;
    const bool input_valid = read_aux_input_snapshot(&input);
    if (input_valid) {
        service_terminal_keyboard(shell, &input.keyboard);
        if (shell->page == CONSOLE_PAGE_TERMINAL) {
            if (key_down(&input.keyboard, PLATFORM_USB_KEY_ESCAPE)) {
                buttons |= CONSOLE_BUTTON_BACK;
            }
        } else {
            buttons |= keyboard_shell_buttons(&input.keyboard);
        }
        if (input.mouse.connected != 0U) {
            if ((input.mouse.buttons & UINT8_C(0x02)) != 0U) {
                buttons |= CONSOLE_BUTTON_BACK;
            }
            if ((input.mouse.buttons & UINT8_C(0x04)) != 0U) {
                buttons |= CONSOLE_BUTTON_REFRESH;
            }
            if (input.mouse.wheel < 0) {
                buttons |= CONSOLE_BUTTON_DOWN;
            } else if (input.mouse.wheel > 0) {
                buttons |= CONSOLE_BUTTON_UP;
            }
        }
    } else {
        service_terminal_keyboard(shell, NULL);
    }
    const console_shell_action_t button_action =
        console_shell_handle_buttons(shell, buttons);

    console_shell_action_t pointer_action = {0};
    if (input_valid && input.mouse.connected != 0U) {
        s_mouse_x = move_pointer_axis(
            s_mouse_x, input.mouse.delta_x,
            CONSOLE_SHELL_LAYOUT_WIDTH);
        s_mouse_y = move_pointer_axis(
            s_mouse_y, input.mouse.delta_y,
            CONSOLE_SHELL_LAYOUT_HEIGHT);
        const bool left_down =
            (input.mouse.buttons & UINT8_C(0x01)) != 0U;
        console_shell_set_pointer(
            shell, true, s_mouse_x, s_mouse_y, left_down);
        const console_shell_contact_t contact = {
            .x = (uint16_t)((uint32_t)CONSOLE_SHELL_VIEWPORT_LEFT +
                (uint32_t)s_mouse_x *
                    (uint32_t)CONSOLE_SHELL_VIEWPORT_WIDTH /
                    (uint32_t)CONSOLE_SHELL_LAYOUT_WIDTH),
            .y = (uint16_t)((uint32_t)CONSOLE_SHELL_VIEWPORT_TOP +
                (uint32_t)s_mouse_y *
                    (uint32_t)CONSOLE_SHELL_VIEWPORT_HEIGHT /
                    (uint32_t)CONSOLE_SHELL_LAYOUT_HEIGHT),
        };
        pointer_action = console_shell_handle_touch(
            shell, true, left_down ? &contact : NULL,
            left_down ? 1U : 0U);
    } else {
        console_shell_set_pointer(shell, false, 0U, 0U, false);
        pointer_action = console_shell_handle_touch(
            shell, false, NULL, 0U);
    }
    return button_action.type != CONSOLE_ACTION_NONE
        ? button_action : pointer_action;
}
#endif

#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static bool create_shared_control_bus_or_continue(void)
{
    if (s_shared_bus != NULL) {
        return true;
    }
    esp_err_t result = platform_i2c_shared_create(&s_shared_bus);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS CONTROL_BUS_DEGRADED error=%s",
                 esp_err_to_name(result));
        return false;
    }
    ESP_LOGI(TAG, "P4_CONSOLE_OS CONTROL_BUS_READY clients=audio,touch");
    return true;
}

static void create_touch_or_continue(void)
{
    if (!create_shared_control_bus_or_continue()) {
        return;
    }

    platform_touch_config_t config;
    platform_touch_config_init(
        &config, platform_i2c_shared_handle(s_shared_bus));
    const esp_err_t result = platform_touch_create(&config, &s_touch);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS TOUCH_DEGRADED stage=create error=%s",
                 esp_err_to_name(result));
        return;
    }
    s_touch_ready = true;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS TOUCH_READY controller=gt911 contacts_max=%u",
             (unsigned)PLATFORM_TOUCH_MAX_CONTACTS);
}

static bool read_touch_frame(platform_touch_frame_t *frame)
{
    if (frame == NULL) {
        return false;
    }
    platform_touch_frame_neutral(frame);
    if (!s_touch_ready || s_touch == NULL) {
        return false;
    }
    ++s_touch_polls;
    const esp_err_t result = platform_touch_poll(s_touch, frame);
    if (result != ESP_OK || frame->valid == 0U ||
        frame->contact_count > PLATFORM_TOUCH_MAX_CONTACTS) {
        ++s_touch_poll_failures;
        if (s_touch_poll_failures == 1U ||
            s_touch_poll_failures % 120U == 0U) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS TOUCH_POLL_FAIL count=%lu error=%s",
                     (unsigned long)s_touch_poll_failures,
                     esp_err_to_name(result));
        }
        platform_touch_frame_neutral(frame);
        return false;
    }
    return true;
}

static console_shell_action_t poll_touch_input(console_shell_t *shell)
{
    platform_touch_frame_t frame;
    if (!read_touch_frame(&frame)) {
        return console_shell_handle_touch(shell, false, NULL, 0U);
    }
    console_shell_contact_t contacts[CONSOLE_SHELL_MAX_CONTACTS];
    const size_t count = frame.contact_count;
    for (size_t i = 0U; i < count; ++i) {
        contacts[i].x = frame.contacts[i].x;
        contacts[i].y = frame.contacts[i].y;
    }
    return console_shell_handle_touch(
        shell, true, count == 0U ? NULL : contacts, count);
}
#endif

static console_shell_action_t poll_input(console_shell_t *shell)
{
#if P4_CONSOLE_USB_INPUT
    const console_shell_action_t usb_action = poll_usb_input(shell);
    if (usb_action.type != CONSOLE_ACTION_NONE) {
        return usb_action;
    }
#endif
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    return poll_touch_input(shell);
#else
    const console_shell_action_t no_action = {0};
    return no_action;
#endif
}

static void log_runtime_stats(const console_shell_t *shell)
{
    platform_display_stats_t display = {0};
    const esp_err_t result = platform_display_get_stats(&display);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "P4_CONSOLE_OS STATS_UNAVAILABLE error=%s",
                 esp_err_to_name(result));
        return;
    }
#if P4_CONSOLE_USB_INPUT
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS STATS loops=%lu page=%u renders=%lu "
             "usb_input_ready=%u gamepad=%u keyboard=%u mouse=%u "
             "input_polls=%lu gamepad_failures=%lu aux_failures=%lu "
             "display_submits=%lu "
             "display_completions=%lu display_timeouts=%lu "
             "display_failures=%lu audio=es8311-ready storage=%s "
             "storage_generation=%lu storage_media=microsd "
             "p4cart_scan=%u p4cart_valid=%u p4cart_rejected=%u",
             (unsigned long)s_loop_count,
             (unsigned)shell->page,
             (unsigned long)shell->render_generation,
             s_gamepad_ready ? 1U : 0U,
             s_gamepad_connected ? 1U : 0U,
             s_keyboard_connected ? 1U : 0U,
             s_mouse_connected ? 1U : 0U,
             (unsigned long)s_gamepad_polls,
             (unsigned long)s_gamepad_poll_failures,
             (unsigned long)s_aux_input_poll_failures,
             (unsigned long)display.submits_started,
             (unsigned long)display.submits_completed,
             (unsigned long)display.submit_timeouts,
             (unsigned long)display.submit_failures,
             platform_game_storage_state_name(s_game_storage_status.state),
             (unsigned long)s_game_storage_status.generation,
             (unsigned)p4cart_scan_state(),
             (unsigned)s_p4cart_catalog.valid_cart_count,
             (unsigned)s_p4cart_catalog.invalid_cart_count);
#else
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS STATS loops=%lu page=%u renders=%lu "
             "touch_ready=%u touch_polls=%lu touch_failures=%lu "
             "display_submits=%lu display_completions=%lu "
             "display_timeouts=%lu display_failures=%lu "
             "amp_energized=0 doom_handoffs=%lu storage=%s "
             "storage_generation=%lu usb_attached=%u "
             "p4cart_scan=%u p4cart_valid=%u p4cart_rejected=%u",
             (unsigned long)s_loop_count,
             (unsigned)shell->page,
             (unsigned long)shell->render_generation,
             s_touch_ready ? 1U : 0U,
             (unsigned long)s_touch_polls,
             (unsigned long)s_touch_poll_failures,
             (unsigned long)display.submits_started,
             (unsigned long)display.submits_completed,
             (unsigned long)display.submit_timeouts,
             (unsigned long)display.submit_failures,
             (unsigned long)s_doom_handoff_count,
             platform_game_storage_state_name(s_game_storage_status.state),
             (unsigned long)s_game_storage_status.generation,
             s_game_storage_status.usb_attached ? 1U : 0U,
             (unsigned)p4cart_scan_state(),
             (unsigned)s_p4cart_catalog.valid_cart_count,
             (unsigned)s_p4cart_catalog.invalid_cart_count);
#endif
}

static esp_err_t present(console_shell_t *shell)
{
    if (!console_shell_render_rgb565(
            shell, s_pixels, CONSOLE_SHELL_WIDTH)) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    return platform_display_submit_content_rgb565(
        s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
#else
    return platform_display_submit_rgb565(
        s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
#endif
}

#if !CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
static void draw_gamechangers_ai_logo(p4_game_surface_t *surface,
                                      int left, int top)
{
    if (surface == NULL) {
        return;
    }
    const size_t expected_bytes =
        (size_t)CONSOLE_BOOT_LOGO_WIDTH * CONSOLE_BOOT_LOGO_HEIGHT *
        sizeof(uint16_t);
    const size_t embedded_bytes = (size_t)(
        _binary_gamechangers_ai_logo_rgb565_end -
        _binary_gamechangers_ai_logo_rgb565_start);
    if (embedded_bytes != expected_bytes) {
        halt_dark("boot-logo-size", ESP_ERR_INVALID_SIZE);
    }
    for (size_t y = 0U; y < CONSOLE_BOOT_LOGO_HEIGHT; ++y) {
        for (size_t x = 0U; x < CONSOLE_BOOT_LOGO_WIDTH; ++x) {
            const size_t byte_index =
                (y * CONSOLE_BOOT_LOGO_WIDTH + x) * sizeof(uint16_t);
            const uint16_t pixel =
                (uint16_t)_binary_gamechangers_ai_logo_rgb565_start[byte_index] |
                (uint16_t)((uint16_t)
                    _binary_gamechangers_ai_logo_rgb565_start[byte_index + 1U]
                    << 8U);
            p4_draw_pixel(surface, left + (int)x, top + (int)y, pixel);
        }
    }
}
#endif

#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
static p4_bbs_boot_phase_t bbs_boot_phase_for_status(const char *status)
{
    if (status == NULL || strstr(status, "START") != NULL ||
        strstr(status, "POST") != NULL) {
        return P4_BBS_BOOT_POST;
    }
    if (strstr(status, "DISK") != NULL) {
        return P4_BBS_BOOT_DISK;
    }
    if (strstr(status, "DIAL") != NULL) {
        return P4_BBS_BOOT_DIALING;
    }
    if (strstr(status, "V.22") != NULL ||
        strstr(status, "2400 BAUD") != NULL) {
        return P4_BBS_BOOT_TRAINING;
    }
    if (strstr(status, "DEGRADED") != NULL) {
        return P4_BBS_BOOT_DEGRADED;
    }
    if (strstr(status, "READY") != NULL ||
        strstr(status, "CONNECT 2400") != NULL ||
        strstr(status, "CARRIER DETECT") != NULL) {
        return P4_BBS_BOOT_CONNECTED;
    }
    return P4_BBS_BOOT_SYNCING;
}
#endif

static esp_err_t present_boot_screen(unsigned animation_step,
                                     const char *status)
{
    if (s_pixels == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    const p4_bbs_boot_phase_t phase = bbs_boot_phase_for_status(status);
    const p4_bbs_boot_model_t model = {
        .phase = phase,
        .progress_step = phase == P4_BBS_BOOT_CONNECTED
            ? CONSOLE_BOOT_ANIMATION_STEPS
            : (uint8_t)(animation_step > CONSOLE_BOOT_ANIMATION_STEPS
                ? CONSOLE_BOOT_ANIMATION_STEPS : animation_step),
        .progress_total = CONSOLE_BOOT_ANIMATION_STEPS,
    };
    if (!p4_bbs_build_boot_screen(&s_shell.bbs_terminal, &model) ||
        !p4_ansi_render_rgb565(
            &s_shell.bbs_terminal, s_pixels, CONSOLE_SHELL_WIDTH,
            CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_FAIL;
    for (unsigned attempt = 0U;
         attempt < CONSOLE_BOOT_SUBMIT_ATTEMPTS; ++attempt) {
        result = platform_display_submit_content_rgb565(
            s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
        if (result != ESP_ERR_TIMEOUT) {
            break;
        }
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BOOT_FRAME_RETRY status=%s attempt=%u",
                 status == NULL ? "STARTING" : status, attempt + 1U);
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_BOOT_SUBMIT_RETRY_MS));
    }
    return result;
#else
    p4_game_surface_t surface = {
        .pixels = s_pixels,
        .stride_pixels = P4_GAME_SURFACE_WIDTH,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    p4_draw_clear(&surface, UINT16_C(0x0000));
    draw_gamechangers_ai_logo(&surface, 104, 0);
    p4_draw_fill_rect(&surface, 84, 113, 152, 1, UINT16_C(0x39C7));
    p4_draw_text(&surface, 100, 124, "CONSOLE OS", UINT16_C(0xFFFF),
                 2U, 10U);
    p4_draw_text(&surface, 127, 146, "BUILT TO WIN", UINT16_C(0xFD20),
                 1U, 12U);
    for (unsigned segment = 0U;
         segment < CONSOLE_BOOT_ANIMATION_STEPS; ++segment) {
        p4_draw_fill_rect(&surface, 122 + (int)segment * 16, 176, 10, 2,
                          segment < animation_step ? UINT16_C(0xFD20) :
                              UINT16_C(0x39C7));
    }
    p4_draw_text(&surface, 116, 186,
                 status == NULL ? "STARTING..." : status,
                 UINT16_C(0xBDF7), 1U, 8U);
    return platform_display_submit_rgb565(
        s_pixels, P4_GAME_SURFACE_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
#endif
}

#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
static esp_err_t present_bbs_home_reveal(console_shell_t *shell)
{
    if (shell == NULL || s_pixels == NULL ||
        !console_shell_render_rgb565(
            shell, s_pixels, CONSOLE_SHELL_WIDTH)) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t pixel_count =
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT;
    uint16_t *const reveal_pixels = heap_caps_calloc(
        pixel_count, sizeof(*reveal_pixels),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (reveal_pixels == NULL) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BBS_REVEAL status=skipped reason=no-memory");
        return platform_display_submit_content_rgb565(
            s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
    }

    esp_err_t result = ESP_OK;
    size_t previous_rows = 0U;
    for (unsigned step = 1U;
         step <= CONSOLE_BBS_HOME_REVEAL_STEPS; ++step) {
        const size_t visible_rows =
            (size_t)CONSOLE_SHELL_HEIGHT * step /
            CONSOLE_BBS_HOME_REVEAL_STEPS;
        const size_t added_rows = visible_rows - previous_rows;
        memcpy(reveal_pixels + previous_rows * CONSOLE_SHELL_WIDTH,
               s_pixels + previous_rows * CONSOLE_SHELL_WIDTH,
               added_rows * CONSOLE_SHELL_WIDTH * sizeof(*s_pixels));
        result = platform_display_submit_content_rgb565(
            reveal_pixels, CONSOLE_SHELL_WIDTH,
            CONSOLE_SUBMIT_TIMEOUT_MS);
        if (result != ESP_OK) {
            break;
        }
        previous_rows = visible_rows;
        if (step < CONSOLE_BBS_HOME_REVEAL_STEPS) {
            vTaskDelay(pdMS_TO_TICKS(CONSOLE_BBS_HOME_REVEAL_DELAY_MS));
        }
    }
    heap_caps_free(reveal_pixels);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BBS_REVEAL status=%s rows=%u steps=%u",
             result == ESP_OK ? "complete" : "failed",
             (unsigned)previous_rows,
             (unsigned)CONSOLE_BBS_HOME_REVEAL_STEPS);
    return result;
}
#endif

#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static esp_err_t destroy_touch_for_handoff(void)
{
    if (s_touch == NULL) {
        s_touch_ready = false;
        return ESP_OK;
    }
    esp_err_t result = ESP_FAIL;
    for (unsigned attempt = 0U;
         attempt < CONSOLE_CLEANUP_ATTEMPTS && s_touch != NULL; ++attempt) {
        result = platform_touch_destroy(&s_touch);
        if (result != ESP_OK) {
            vTaskDelay(1U);
        }
    }
    s_touch_ready = false;
    return s_touch == NULL ? ESP_OK : result;
}

static esp_err_t destroy_bus_for_handoff(void)
{
    if (s_shared_bus == NULL) {
        return ESP_OK;
    }
    esp_err_t result = ESP_FAIL;
    for (unsigned attempt = 0U;
         attempt < CONSOLE_CLEANUP_ATTEMPTS && s_shared_bus != NULL;
         ++attempt) {
        result = platform_i2c_shared_destroy(&s_shared_bus);
        if (result != ESP_OK) {
            vTaskDelay(1U);
        }
    }
    return s_shared_bus == NULL ? ESP_OK : result;
}
#endif

static bool native_audio_runtime_allowed(void)
{
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    return true;
#else
    doom_touch_audio_runtime_gate_t gate = {0};
    doom_touch_audio_runtime_gate_read(&gate);
    return doom_touch_audio_runtime_gate_mode(&gate) ==
        DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_AND_AUDIO;
#endif
}

static void *native_audio_control_bus(void)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    return s_shared_bus == NULL
        ? NULL : (void *)platform_i2c_shared_handle(s_shared_bus);
#else
    return NULL;
#endif
}

static bool pump_native_audio(p4_game_platform_audio_t *audio,
                              p4_audio_mixer_t *mixer)
{
    if (!p4_game_platform_audio_running(audio)) {
        return false;
    }
    if (!p4_audio_mixer_render(
            mixer, s_native_audio_pcm,
            CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK)) {
        halt_dark("native-audio-mix", ESP_ERR_INVALID_SIZE);
    }
    for (size_t offset = 0U;
         offset < CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK;
         offset += P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES) {
        const esp_err_t result = p4_game_platform_audio_write(
            audio,
            &s_native_audio_pcm[
                offset * P4_GAME_PLATFORM_AUDIO_CHANNEL_COUNT],
            P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES);
        if (result != ESP_OK) {
            if (!audio->safe_high_proven || audio->backend != NULL) {
                halt_dark("native-audio-write-safety", result);
            }
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS NATIVE_SOUND_DEGRADED "
                     "stage=write error=%s fallback=silent",
                     esp_err_to_name(result));
            p4_audio_mixer_stop_all(mixer);
            return false;
        }
    }
    return true;
}

static void close_native_audio_or_halt(p4_game_platform_audio_t *audio)
{
    if (audio == NULL || !audio->hardware_touched) {
        return;
    }
    const esp_err_t result = p4_game_platform_audio_close(audio);
    if (result != ESP_OK || !audio->safe_high_proven ||
        audio->backend != NULL) {
        halt_dark("native-audio-close-safety",
                  result == ESP_OK ? ESP_ERR_INVALID_STATE : result);
    }
}

static void present_boot_dial_frame(size_t digit_index)
{
    const esp_err_t result = present_boot_screen(
        (unsigned)(digit_index % CONSOLE_BOOT_ANIMATION_STEPS) + 1U,
        "DIALING 614-276-3639");
    if (result != ESP_OK) {
        halt_dark("boot-dial-frame", result);
    }
}

static void present_boot_modem_frame(size_t phase_index)
{
    const esp_err_t result = present_boot_screen(
        (unsigned)(phase_index % CONSOLE_BOOT_ANIMATION_STEPS) + 1U,
        "V.22BIS 2400 BAUD");
    if (result != ESP_OK) {
        halt_dark("boot-modem-frame", result);
    }
}

static void present_boot_hdd_frame(size_t phase_index)
{
    const esp_err_t result = present_boot_screen(
        (unsigned)(phase_index % CONSOLE_BOOT_ANIMATION_STEPS) + 1U,
        "CHECKING DISK");
    if (result != ESP_OK) {
        halt_dark("boot-hdd-frame", result);
    }
}

static void wait_with_boot_audio_animation(void)
{
    (void)present_boot_screen(1U, "POST OK");
    vTaskDelay(pdMS_TO_TICKS(
        CONSOLE_BOOT_POST_BEEP_MS + CONSOLE_BOOT_POST_GAP_MS));
    for (size_t phase = 0U;
         phase < sizeof(s_boot_hdd_phases) /
                     sizeof(s_boot_hdd_phases[0]); ++phase) {
        present_boot_hdd_frame(phase);
        vTaskDelay(pdMS_TO_TICKS(
            (unsigned)s_boot_hdd_phases[phase].duration_ms +
            (unsigned)s_boot_hdd_phases[phase].gap_ms));
    }
    for (size_t digit = 0U;
         digit < sizeof(s_boot_dial) / sizeof(s_boot_dial[0]); ++digit) {
        present_boot_dial_frame(digit);
        const unsigned gap = digit == 2U || digit == 5U
            ? CONSOLE_BOOT_DTMF_DASH_MS : CONSOLE_BOOT_DTMF_GAP_MS;
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_BOOT_DTMF_TONE_MS + gap));
    }
    for (size_t phase = 0U;
         phase < sizeof(s_boot_modem_phases) /
                     sizeof(s_boot_modem_phases[0]); ++phase) {
        present_boot_modem_frame(phase);
        vTaskDelay(pdMS_TO_TICKS(
            (unsigned)s_boot_modem_phases[phase].duration_ms +
            (unsigned)s_boot_modem_phases[phase].gap_ms));
    }
}

static bool play_boot_tone_pair(p4_game_platform_audio_t *audio,
                                p4_audio_mixer_t *mixer,
                                uint16_t low_hz, uint16_t high_hz,
                                uint16_t duration_ms, uint16_t gap_ms,
                                uint8_t low_volume_step,
                                uint8_t high_volume_step,
                                p4_waveform_t waveform)
{
    p4_audio_mixer_stop_all(mixer);
    bool played = true;
    if (low_hz != 0U) {
        const p4_tone_t low = {
            .frequency_hz = low_hz,
            .duration_ms = duration_ms,
            .volume_step = low_volume_step,
            .waveform = waveform,
        };
        played = p4_audio_mixer_play_tone(mixer, &low);
    }
    if (played && high_hz != 0U) {
        const p4_tone_t high = {
            .frequency_hz = high_hz,
            .duration_ms = duration_ms,
            .volume_step = high_volume_step,
            .waveform = waveform,
        };
        played = p4_audio_mixer_play_tone(mixer, &high);
    }
    const unsigned tone_ticks =
        ((unsigned)duration_ms + CONSOLE_FRAME_INTERVAL_MS - 1U) /
        CONSOLE_FRAME_INTERVAL_MS;
    for (unsigned tick = 0U; played && tick < tone_ticks; ++tick) {
        played = pump_native_audio(audio, mixer);
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_FRAME_INTERVAL_MS));
    }
    p4_audio_mixer_stop_all(mixer);
    const unsigned gap_ticks =
        ((unsigned)gap_ms + CONSOLE_FRAME_INTERVAL_MS - 1U) /
        CONSOLE_FRAME_INTERVAL_MS;
    for (unsigned tick = 0U; played && tick < gap_ticks; ++tick) {
        played = pump_native_audio(audio, mixer);
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_FRAME_INTERVAL_MS));
    }
    return played;
}

static void play_boot_chime(void)
{
    if (!native_audio_runtime_allowed()) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BOOT_POST status=silent reason=runtime-gate");
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BOOT_HDD status=silent reason=runtime-gate");
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BOOT_DIAL status=silent digits=614-276-3639 "
                 "reason=runtime-gate");
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BOOT_MODEM status=silent "
                 "reason=runtime-gate");
        wait_with_boot_audio_animation();
        return;
    }
    p4_game_platform_audio_t audio;
    p4_game_platform_audio_init(&audio);
    const esp_err_t open_result = p4_game_platform_audio_open(
        &audio, true, native_audio_control_bus(),
        s_console_settings.boot_volume_step);
    if (open_result != ESP_OK) {
        if (audio.hardware_touched &&
            (!audio.safe_high_proven || audio.backend != NULL)) {
            halt_dark("boot-chime-open-safety", open_result);
        }
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BOOT_DIAL status=silent digits=614-276-3639 "
                 "stage=open error=%s",
                 esp_err_to_name(open_result));
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BOOT_MODEM status=silent "
                 "stage=open error=%s",
                 esp_err_to_name(open_result));
        wait_with_boot_audio_animation();
        return;
    }
    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    bool played = true;
    (void)present_boot_screen(1U, "POST OK");
    played = play_boot_tone_pair(
        &audio, &mixer, 880U, 0U, CONSOLE_BOOT_POST_BEEP_MS,
        CONSOLE_BOOT_POST_GAP_MS,
        s_console_settings.boot_volume_step, 0U, P4_WAVE_SQUARE);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BOOT_POST status=%s beep_hz=880 duration_ms=%u "
             "codec_volume=%u tone_volume=%u",
             played ? "played" : "degraded",
             (unsigned)CONSOLE_BOOT_POST_BEEP_MS,
             (unsigned)s_console_settings.boot_volume_step,
             (unsigned)s_console_settings.boot_volume_step);

    size_t hdd_phases = 0U;
    for (; played && hdd_phases < sizeof(s_boot_hdd_phases) /
                                     sizeof(s_boot_hdd_phases[0]);
         ++hdd_phases) {
        const console_boot_modem_phase_t *const phase =
            &s_boot_hdd_phases[hdd_phases];
        present_boot_hdd_frame(hdd_phases);
        played = play_boot_tone_pair(
            &audio, &mixer, phase->low_hz, phase->high_hz,
            phase->duration_ms, phase->gap_ms,
            phase->low_volume_step, phase->high_volume_step,
            phase->waveform);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BOOT_HDD status=%s phases=%u profile=seek-pulses",
             played ? "played" : "degraded", (unsigned)hdd_phases);

    const int64_t dial_started_us = esp_timer_get_time();
    for (size_t digit = 0U;
         digit < sizeof(s_boot_dial) / sizeof(s_boot_dial[0]);
         ++digit) {
        present_boot_dial_frame(digit);
        const unsigned gap_ms = digit == 2U || digit == 5U
            ? CONSOLE_BOOT_DTMF_DASH_MS : CONSOLE_BOOT_DTMF_GAP_MS;
        played = play_boot_tone_pair(
            &audio, &mixer, s_boot_dial[digit].low_hz,
            s_boot_dial[digit].high_hz,
            CONSOLE_BOOT_DTMF_TONE_MS, (uint16_t)gap_ms,
            s_console_settings.boot_volume_step,
            s_console_settings.boot_volume_step,
            P4_WAVE_TRIANGLE);
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BOOT_DIAL_DIGIT index=%u digit=%c "
                 "low_hz=%u high_hz=%u",
                 (unsigned)digit, s_boot_dial[digit].digit,
                 (unsigned)s_boot_dial[digit].low_hz,
                 (unsigned)s_boot_dial[digit].high_hz);
        if (!played) {
            break;
        }
    }
    const int64_t dial_elapsed_ms =
        (esp_timer_get_time() - dial_started_us) / INT64_C(1000);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BOOT_DIAL status=%s digits=614-276-3639 "
             "mode=dual-tone-dtmf codec_volume=%u tone_volume=%u "
             "digit_tone_ms=%u digit_gap_ms=%u group_gap_ms=%u "
             "elapsed_ms=%" PRIi64,
             played ? "played" : "degraded",
             (unsigned)s_console_settings.boot_volume_step,
             (unsigned)s_console_settings.boot_volume_step,
             (unsigned)CONSOLE_BOOT_DTMF_TONE_MS,
             (unsigned)CONSOLE_BOOT_DTMF_GAP_MS,
             (unsigned)CONSOLE_BOOT_DTMF_DASH_MS, dial_elapsed_ms);

    const int64_t modem_started_us = esp_timer_get_time();
    size_t modem_phases = 0U;
    for (; played && modem_phases < sizeof(s_boot_modem_phases) /
                                   sizeof(s_boot_modem_phases[0]);
         ++modem_phases) {
        const console_boot_modem_phase_t *const phase =
            &s_boot_modem_phases[modem_phases];
        present_boot_modem_frame(modem_phases);
        played = play_boot_tone_pair(
            &audio, &mixer, phase->low_hz, phase->high_hz,
            phase->duration_ms, phase->gap_ms,
            phase->low_volume_step, phase->high_volume_step,
            phase->waveform);
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BOOT_MODEM_PHASE index=%u name=%s "
                 "low_hz=%u high_hz=%u duration_ms=%u gap_ms=%u",
                 (unsigned)modem_phases, phase->name,
                 (unsigned)phase->low_hz, (unsigned)phase->high_hz,
                 (unsigned)phase->duration_ms, (unsigned)phase->gap_ms);
    }
    close_native_audio_or_halt(&audio);
    const int64_t modem_elapsed_ms =
        (esp_timer_get_time() - modem_started_us) / INT64_C(1000);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BOOT_MODEM status=%s profile=v22bis-2400 "
             "phases=%u codec_volume=%u tone_volume=%u elapsed_ms=%" PRIi64,
             played ? "played" : "degraded", (unsigned)modem_phases,
             (unsigned)s_console_settings.boot_volume_step,
             (unsigned)s_console_settings.boot_volume_step,
             modem_elapsed_ms);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS AUDIO_PROFILE boot_volume=%u runtime_volume=%u "
             "runtime_restore=on-next-open",
             (unsigned)s_console_settings.boot_volume_step,
             (unsigned)s_console_settings.game_volume_step);
}

static esp_err_t wait_for_game_storage_with_boot_animation(void)
{
    console_storage_init_state_t initial_state;
    esp_err_t initial_result;
    game_storage_init_snapshot(&initial_state, &initial_result);
    if (initial_state == CONSOLE_STORAGE_INIT_NOT_STARTED) {
        const esp_err_t start_result = start_game_storage_initialization();
        if (start_result != ESP_OK) {
            return start_result;
        }
    } else if (initial_state == CONSOLE_STORAGE_INIT_FAILED) {
        return initial_result;
    }

    unsigned animation_frame = 0U;
    for (;;) {
        console_storage_init_state_t state;
        esp_err_t result;
        game_storage_init_snapshot(&state, &result);
        if (state == CONSOLE_STORAGE_INIT_READY ||
            state == CONSOLE_STORAGE_INIT_FAILED) {
            const esp_err_t frame_result = present_boot_screen(
                CONSOLE_BOOT_ANIMATION_STEPS,
                state == CONSOLE_STORAGE_INIT_READY
                    ? "GAMES READY" : "SD CARD DEGRADED");
            if (frame_result != ESP_OK) {
                halt_dark("storage-loading-frame", frame_result);
            }
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS LOADING_SCREEN status=%s result=%s "
                     "frames=%u storage_elapsed_ms=%" PRIi64,
                     state == CONSOLE_STORAGE_INIT_READY
                         ? "complete" : "degraded",
                     esp_err_to_name(result), animation_frame,
                     (esp_timer_get_time() - s_game_storage_init_started_us) /
                         INT64_C(1000));
            return result;
        }

        const unsigned step =
            animation_frame % CONSOLE_BOOT_ANIMATION_STEPS + 1U;
        const esp_err_t frame_result = present_boot_screen(
            step, "READING SD CARD");
        if (frame_result != ESP_OK) {
            halt_dark("storage-loading-frame", frame_result);
        }
        if (animation_frame != UINT_MAX) {
            ++animation_frame;
        }
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_STORAGE_LOADING_FRAME_MS));
    }
}

typedef struct {
    const char *game_id;
    p4_achievement_catalog_t *achievements;
    p4_audio_mixer_t mixer;
    p4_game_platform_audio_t audio;
    p4_game_input_mapper_t input_mapper;
    TickType_t last_wake;
    uint32_t frames;
    p4_game_result_t game_result;
    bool audio_running;
    bool finished;
} cartridge_run_context_t;

static bool cartridge_unlock_achievement(
    void *opaque, const p4_game_achievement_t *achievement)
{
    cartridge_run_context_t *const context = opaque;
    if (context == NULL || context->achievements == NULL ||
        context->game_id == NULL || achievement == NULL ||
        achievement->game_id == NULL ||
        strcmp(context->game_id, achievement->game_id) != 0) {
        if (context != NULL && context->achievements != NULL &&
            context->achievements->rejected_events != UINT32_MAX) {
            ++context->achievements->rejected_events;
        }
        return false;
    }
    return p4_achievement_catalog_unlock(
        context->achievements, achievement);
}

#if P4_CONSOLE_SIGNAL_SCAN
static bool cartridge_request_signal_scan(void *opaque, uint64_t focus_token)
{
    (void)opaque;
    return platform_signal_scan_request(NULL, focus_token);
}

static bool cartridge_read_signal_scan(
    void *opaque, p4_game_signal_snapshot_t *snapshot)
{
    (void)opaque;
    return platform_signal_scan_read(NULL, snapshot);
}
#endif

static bool cartridge_play_tone(void *opaque, const p4_tone_t *tone)
{
    cartridge_run_context_t *const context = opaque;
    return context != NULL && context->audio_running &&
        p4_audio_mixer_service_play_tone(&context->mixer, tone);
}

static bool cartridge_submit_pcm16_stereo(
    void *opaque, const int16_t *interleaved_stereo, size_t frame_count)
{
    cartridge_run_context_t *const context = opaque;
    return context != NULL && context->audio_running &&
        p4_audio_mixer_service_submit_pcm16_stereo(
            &context->mixer, interleaved_stereo, frame_count);
}

static void cartridge_stop_audio(void *opaque)
{
    cartridge_run_context_t *const context = opaque;
    if (context != NULL) {
        p4_audio_mixer_service_stop(&context->mixer);
    }
}

static bool cartridge_present(void *opaque)
{
    cartridge_run_context_t *const context = opaque;
    if (context == NULL) {
        return false;
    }
    const esp_err_t result = platform_display_submit_rgb565(
        s_pixels, P4_GAME_SURFACE_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
    return result == ESP_OK;
}

static bool cartridge_poll_frame(void *opaque,
                                 p4_game_input_t *out_input,
                                 uint32_t *out_elapsed_ms)
{
    cartridge_run_context_t *const context = opaque;
    if (context == NULL || out_input == NULL || out_elapsed_ms == NULL) {
        return false;
    }
#if P4_CONSOLE_USB_INPUT
    platform_gamepad_snapshot_t gamepad;
    uint32_t digital_buttons = read_gamepad_snapshot(&gamepad)
        ? gamepad_p4_buttons(&gamepad.state) : 0U;
    platform_usb_input_snapshot_t input;
    if (read_aux_input_snapshot(&input)) {
        digital_buttons |= keyboard_p4_buttons(&input.keyboard);
        if (input.mouse.connected != 0U) {
            if ((input.mouse.buttons & UINT8_C(0x01)) != 0U) {
                digital_buttons |= P4_BUTTON_A;
            }
            if ((input.mouse.buttons & UINT8_C(0x02)) != 0U) {
                digital_buttons |= P4_BUTTON_B;
            }
        }
    }
#else
    const uint32_t digital_buttons = 0U;
#endif
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    platform_touch_frame_t frame;
    const bool valid = read_touch_frame(&frame);
    p4_physical_touch_t touches[P4_INPUT_MAX_TOUCHES];
    const size_t touch_count = valid ? frame.contact_count : 0U;
    for (size_t index = 0U; index < touch_count; ++index) {
        touches[index].x = frame.contacts[index].x;
        touches[index].y = frame.contacts[index].y;
    }
#else
    const bool valid = false;
    const size_t touch_count = 0U;
#endif
    p4_game_input_mapper_update(
        &context->input_mapper, valid,
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
        touch_count == 0U ? NULL : touches,
#else
        NULL,
#endif
        touch_count, digital_buttons, out_input);
    if (context->audio_running) {
        context->audio_running = pump_native_audio(
            &context->audio, &context->mixer);
    }
    if (context->frames != UINT32_MAX) {
        ++context->frames;
    }
    if (context->frames != 0U && context->frames % 300U == 0U) {
#if P4_CONSOLE_USB_INPUT
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS CARTRIDGE_STATS app=%s frames=%lu "
                 "gamepad=%u keyboard=%u mouse=%u input_polls=%lu "
                 "gamepad_failures=%lu audio_running=%u audio_frames=%lu",
                 context->game_id, (unsigned long)context->frames,
                 s_gamepad_connected ? 1U : 0U,
                 s_keyboard_connected ? 1U : 0U,
                 s_mouse_connected ? 1U : 0U,
                 (unsigned long)s_gamepad_polls,
                 (unsigned long)s_gamepad_poll_failures,
                 context->audio_running ? 1U : 0U,
                 (unsigned long)context->audio.frames_written);
#else
        p4_audio_mixer_stats_t stats = {0};
        p4_audio_mixer_get_stats(&context->mixer, &stats);
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS CARTRIDGE_STATS app=%s frames=%lu "
                 "touch_polls=%lu touch_failures=%lu audio_running=%u "
                 "tones=%lu audio_frames=%lu",
                 context->game_id, (unsigned long)context->frames,
                 (unsigned long)s_touch_polls,
                 (unsigned long)s_touch_poll_failures,
                 context->audio_running ? 1U : 0U,
                 (unsigned long)stats.tones_started,
                 (unsigned long)context->audio.frames_written);
#endif
    }
    *out_elapsed_ms = CONSOLE_FRAME_INTERVAL_MS;
    vTaskDelayUntil(&context->last_wake,
                    pdMS_TO_TICKS(CONSOLE_FRAME_INTERVAL_MS));
    return true;
}

static void cartridge_finished(void *opaque, p4_game_result_t result)
{
    cartridge_run_context_t *const context = opaque;
    if (context != NULL) {
        context->game_result = result;
        context->finished = true;
    }
}

static esp_err_t run_stored_game(
    console_shell_t *shell,
    const platform_game_catalog_entry_t *game)
{
    if (shell == NULL || game == NULL || !game->valid ||
        s_pixels == NULL || !s_display_initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    cartridge_run_context_t context = {
        .game_id = game->package.id,
        .achievements = &s_achievements,
        .game_result = P4_GAME_ERROR,
        .last_wake = xTaskGetTickCount(),
    };
    p4_audio_mixer_init(&context.mixer);
    p4_game_platform_audio_init(&context.audio);
    const uint32_t capabilities = game->package.required_capabilities |
        game->package.optional_capabilities;
    if ((capabilities &
         (P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_AUDIO_STREAM)) != 0U) {
        const esp_err_t audio_result = p4_game_platform_audio_open(
            &context.audio, native_audio_runtime_allowed(),
            native_audio_control_bus(),
            s_console_settings.game_volume_step);
        if (audio_result == ESP_OK) {
            context.audio_running = true;
        } else if (context.audio.hardware_touched &&
                   (!context.audio.safe_high_proven ||
                    context.audio.backend != NULL)) {
            halt_dark("cartridge-audio-open-safety", audio_result);
        }
    }
    p4_game_input_mapper_init(&context.input_mapper);
#if P4_CONSOLE_SIGNAL_SCAN
    const bool signal_scan_ready = platform_signal_scan_ready();
#else
    const bool signal_scan_ready = false;
#endif
    p4_cartridge_host_v1_t host = {
        .magic = P4_CARTRIDGE_HOST_MAGIC,
        .api_version = P4_CARTRIDGE_HOST_API_VERSION,
        .struct_bytes = sizeof(host),
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS |
            (context.audio_running
                ? P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_AUDIO_STREAM : 0U) |
            (signal_scan_ready ? P4_GAME_CAP_SIGNAL_SCAN : 0U),
        .expected_game_id = game->package.id,
        .surface = {
            .pixels = s_pixels,
            .stride_pixels = P4_GAME_SURFACE_WIDTH,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        },
        .context = &context,
        .poll_frame = cartridge_poll_frame,
        .present = cartridge_present,
        .play_tone = context.audio_running ? cartridge_play_tone : NULL,
        .submit_pcm16_stereo = context.audio_running
            ? cartridge_submit_pcm16_stereo : NULL,
        .stop_audio = context.audio_running ? cartridge_stop_audio : NULL,
        .finished = cartridge_finished,
        .unlock_achievement = cartridge_unlock_achievement,
#if P4_CONSOLE_SIGNAL_SCAN
        .request_signal_scan = signal_scan_ready
            ? cartridge_request_signal_scan : NULL,
        .read_signal_scan = signal_scan_ready
            ? cartridge_read_signal_scan : NULL,
#endif
    };
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS CARTRIDGE_START app=%s file=%s api=1 "
             "source=%s runtime=psram-elf audio=%s signals=%s",
             game->package.id, game->file_name,
             game->in_games_directory
                ? "microsd-games-directory" : "microsd-root-compat",
             context.audio_running ? "ready" : "silent",
             signal_scan_ready ? "ready" : "offline");
    esp_err_t result = platform_game_loader_run(game, &host);
    close_native_audio_or_halt(&context.audio);
    if (result == ESP_OK &&
        (!context.finished ||
         context.game_result != P4_GAME_EXIT_TO_LAUNCHER)) {
        result = ESP_FAIL;
    }
    console_shell_set_achievement_catalog(shell, &s_achievements);
    console_shell_show_home(shell);
    const esp_err_t home_result = present(shell);
    if (home_result != ESP_OK) {
        halt_dark("cartridge-return-home", home_result);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS CARTRIDGE_STOP app=%s result=%s frames=%lu "
             "return=launcher amp_safe=%u",
             game->package.id, esp_err_to_name(result),
             (unsigned long)context.frames,
             context.audio.hardware_touched
                ? (context.audio.safe_high_proven ? 1U : 0U) : 1U);
    return result;
}

static void launch_doom_exclusive(console_shell_t *shell)
{
    esp_err_t result = platform_game_storage_lock_for_game();
    sync_game_storage();
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS HANDOFF_REJECTED app=doom storage=%s "
                 "error=%s action=eject-usb-and-retry",
                 platform_game_storage_state_name(
                     s_game_storage_status.state),
                 esp_err_to_name(result));
        console_shell_show_home(shell);
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(shell, &current_runtime);
        return;
    }
    ++s_doom_handoff_count;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS HANDOFF_BEGIN app=doom mode=exclusive-one-way "
             "audio_owner=doom storage=game-locked input_owner=platform");
    result = platform_display_set_brightness(0U);
    if (result != ESP_OK) {
        halt_dark("handoff-backlight", result);
    }
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    result = destroy_touch_for_handoff();
    if (result != ESP_OK) {
        halt_dark("handoff-touch-destroy", result);
    }
    result = destroy_bus_for_handoff();
    if (result != ESP_OK) {
        halt_dark("handoff-bus-destroy", result);
    }
#endif
    result = platform_display_deinit();
    if (result != ESP_OK) {
        halt_dark("handoff-display-deinit", result);
    }
    s_display_initialized = false;
    heap_caps_free(s_pixels);
    s_pixels = NULL;

    ESP_LOGI(TAG,
             "P4_CONSOLE_OS HANDOFF_COMPLETE app=doom shell_services=released");
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    console_os_launch_doom();
#else
    console_os_launch_doom(s_console_settings.game_volume_step);
#endif
    halt_dark("doom-returned-without-reentrant-teardown",
              ESP_ERR_INVALID_STATE);
}

void app_main(void)
{
    p4_achievement_catalog_init(&s_achievements);
    p4_save_catalog_init(&s_saves, false);
    p4_mp_session_init(&s_multiplayer_session);
    const platform_board_descriptor_t *const board = platform_board_get();
    if (board == NULL) {
        halt_dark("board-descriptor", ESP_ERR_INVALID_STATE);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BOARD_ID vendor=%s product=%s revision=%s "
             "profile=%s flash_bytes=%" PRIu32 " psram_bytes=%" PRIu32,
             board->vendor, board->product, board->revision, board->slug,
             board->flash_bytes, board->psram_bytes);
    memset(&s_game_catalog, 0, sizeof(s_game_catalog));
    memset(&s_os_update_info, 0, sizeof(s_os_update_info));
    s_os_update_info.state = PLATFORM_OS_UPDATE_UNAVAILABLE;
    sync_game_storage();
    if (!build_app_registry()) {
        halt_dark("app-registry", ESP_ERR_INVALID_ARG);
    }
    sync_game_storage();
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS START shell=freertos-native apps=%u "
             "board=%s surface=rgb565-320x200 input=%s "
             "storage_media=%s "
             "native_game_api=1 native_format=p4-native-elf-v1 "
             "game_storage=%s execution=build-candidate",
             (unsigned)s_app_count,
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
             board->slug, "usb-hid-pad+kbd+mouse", "microsd",
#elif CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
      CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE
             board->slug, "gt911-touch+usb-hid-gamepad", "microsd",
#elif CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
             board->slug, "gt911-touch", "microsd",
#else
             board->slug, "gt911-touch", "internal-fat",
#endif
             platform_game_storage_state_name(s_game_storage_status.state));

    console_shell_t *const shell = &s_shell;
    if (!console_shell_init(
            shell, s_apps, s_app_count)) {
        halt_dark("shell-init", ESP_ERR_INVALID_ARG);
    }
    console_shell_set_achievement_catalog(shell, &s_achievements);
    (void)console_shell_set_save_catalog(shell, &s_saves);

    esp_err_t result = platform_display_init();
    if (result != ESP_OK) {
        halt_dark("display-init", result);
    }
    s_display_initialized = true;
    s_pixels = heap_caps_calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT,
        sizeof(*s_pixels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_pixels == NULL) {
        halt_dark("framebuffer-allocation", ESP_ERR_NO_MEM);
    }

    result = present_boot_screen(0U, "STARTING...");
    if (result != ESP_OK) {
        halt_dark("boot-frame", result);
    }
    result = platform_display_set_brightness(CONSOLE_BACKLIGHT_PERCENT);
    if (result != ESP_OK) {
        halt_dark("boot-backlight", result);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BOOT_SCREEN status=visible "
             "brand=gamechangers-ai format=ansi-cp437 "
             "source=official-mark-adaptation storage=pending");
    const esp_err_t settings_result =
        platform_console_settings_init(&s_console_settings);
    if (settings_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS SETTINGS_DEGRADED defaults=1 error=%s",
                 esp_err_to_name(settings_result));
    }
#if P4_CONSOLE_SIGNAL_SCAN
    const esp_err_t signal_scan_result = platform_signal_scan_start();
    if (signal_scan_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS SIGNAL_SCAN_DEGRADED error=%s",
                 esp_err_to_name(signal_scan_result));
    }
#endif
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    (void)create_shared_control_bus_or_continue();
#endif
    const esp_err_t storage_start_result =
        start_game_storage_initialization();
    if (storage_start_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS STORAGE_INIT_START_DEGRADED error=%s",
                 esp_err_to_name(storage_start_result));
    }
    play_boot_chime();
#if P4_CONSOLE_USB_INPUT
    begin_usb_enum_probe_or_suppress();
    create_usb_input_or_continue();
#endif
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    create_touch_or_continue();
#endif
    const esp_err_t storage_result =
        wait_for_game_storage_with_boot_animation();
    if (storage_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS GAME_STORAGE_DEGRADED error=%s "
                 "native_apps=available",
                 esp_err_to_name(storage_result));
    }
    sync_game_storage();
    if (catalog_needs_reload()) {
        (void)present_boot_screen(
            CONSOLE_BOOT_ANIMATION_STEPS, "CONNECTING DOORS");
        (void)reload_game_catalog();
        rebuild_shell_registry(shell);
    }
    result = present_boot_screen(
        CONSOLE_BOOT_ANIMATION_STEPS, "CONNECT 2400");
    if (result != ESP_OK) {
        halt_dark("bbs-connect-frame", result);
    }
    vTaskDelay(pdMS_TO_TICKS(CONSOLE_BBS_CONNECT_HOLD_MS));
    const console_shell_runtime_info_t initial_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &initial_runtime);
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    result = present_bbs_home_reveal(shell);
#else
    result = present(shell);
#endif
    if (result != ESP_OK) {
        halt_dark("first-frame", result);
    }
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS READY page=home display=hdmi "
             "audio=es8311-deferred-until-app "
             "storage=microsd removal=power-off-first");
#elif CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS READY page=home display=mipi-dsi "
             "storage=microsd-read-only removal=power-off-first "
             "h2_role=%s",
#if CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE
             "usb-host-external-vbus"
#else
             "usb-device-storage"
#endif
    );
#else
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS READY page=home amp_energized=0 "
             "doom_audio=deferred-until-exclusive-handoff");
#endif
    bool ota_was_pending = false;
    const esp_err_t ota_valid = platform_os_update_mark_running_valid(
        &ota_was_pending);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS OTA_BOOT_VALID result=%s was_pending=%u",
             esp_err_to_name(ota_valid), ota_was_pending ? 1U : 0U);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MAIN_STACK stage=core-ready "
             "low_water_bytes=%u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        ++s_loop_count;
#if P4_CONSOLE_USB_INPUT
        confirm_usb_enum_probe_after_stable_runtime();
#endif
        sync_game_storage();
        if (finish_p4cart_scan() &&
            shell->page == CONSOLE_PAGE_GAMES) {
            (void)reload_manager_listing(
                shell, CONSOLE_FILE_NOTICE_NONE);
        }
        if (catalog_needs_reload()) {
            (void)reload_game_catalog();
            rebuild_shell_registry(shell);
            if (shell->page == CONSOLE_PAGE_GAMES) {
                (void)reload_manager_listing(
                    shell, CONSOLE_FILE_NOTICE_NONE);
            }
        }
        if (p4cart_scan_needs_reload()) {
            (void)start_p4cart_scan();
            if (shell->page == CONSOLE_PAGE_GAMES) {
                (void)reload_manager_listing(
                    shell, CONSOLE_FILE_NOTICE_NONE);
            }
        }
        if (shell->page == CONSOLE_PAGE_FILES &&
            file_listing_needs_reload()) {
            (void)reload_file_listing(
                shell, CONSOLE_FILE_NOTICE_NONE);
        }
        const console_page_t page_before_input = shell->page;
        const console_shell_action_t action = poll_input(shell);
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(shell, &current_runtime);
        if (action.type == CONSOLE_ACTION_COLOR_MODE_CHANGED) {
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS COLOR_MODE mode=%u persistence=session-only",
                     (unsigned)action.color_mode);
        } else if (action.type == CONSOLE_ACTION_USB_MODE_ENABLE ||
                   action.type == CONSOLE_ACTION_USB_MODE_DISABLE) {
            handle_usb_mode_action(shell, &action);
        } else if (action.type == CONSOLE_ACTION_BOOT_VOLUME_SET ||
                   action.type == CONSOLE_ACTION_GAME_VOLUME_SET) {
            handle_audio_volume_action(shell, &action);
        } else if (action.type == CONSOLE_ACTION_PAGE_CHANGED &&
            action.app_id == CONSOLE_APP_FILES) {
            if (page_before_input != CONSOLE_PAGE_FILES) {
                s_file_directory[0] = '\0';
                shell->file_selected_index = 0U;
                shell->file_first_visible = 0U;
                (void)reload_file_listing(
                    shell, CONSOLE_FILE_NOTICE_NONE);
            }
        } else if (action.type == CONSOLE_ACTION_PAGE_CHANGED &&
                   action.app_id == CONSOLE_APP_GAMES) {
            (void)reload_manager_listing(
                shell, CONSOLE_FILE_NOTICE_NONE);
        } else if (action.type == CONSOLE_ACTION_FILE_REFRESH ||
                   action.type == CONSOLE_ACTION_FILE_OPEN ||
                   action.type == CONSOLE_ACTION_FILE_UP ||
                   action.type == CONSOLE_ACTION_FILE_DELETE) {
            handle_file_action(shell, &action);
        } else if (action.type == CONSOLE_ACTION_GAME_REFRESH ||
                   action.type == CONSOLE_ACTION_GAME_REMOVE ||
                   action.type == CONSOLE_ACTION_OS_UPDATE_INSTALL) {
            handle_manager_action(shell, &action);
        } else if (action.type == CONSOLE_ACTION_LAUNCH &&
                   p4cart_scan_running()) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS LAUNCH_DEFERRED id=%lu "
                     "reason=p4cart-scan",
                     (unsigned long)action.app_id);
        } else if (action.type == CONSOLE_ACTION_LAUNCH &&
            action.app_id == CONSOLE_APP_DOOM) {
            launch_doom_exclusive(shell);
        } else if (action.type == CONSOLE_ACTION_LAUNCH) {
            const platform_game_catalog_entry_t *const game =
                platform_game_catalog_find_launcher(
                    &s_game_catalog, action.app_id);
            if (game == NULL) {
                ESP_LOGE(TAG,
                         "P4_CONSOLE_OS NATIVE_GAME_REJECTED id=%lu "
                         "reason=not-registered",
                         (unsigned long)action.app_id);
            } else {
                const esp_err_t game_result = run_stored_game(shell, game);
                if (game_result != ESP_OK) {
                    ESP_LOGW(TAG,
                             "P4_CONSOLE_OS NATIVE_GAME_DEGRADED app=%s "
                             "error=%s return=launcher",
                             game->package.id, esp_err_to_name(game_result));
                }
                /* Do not make the shell catch up every tick spent in-game. */
                last_wake = xTaskGetTickCount();
            }
        }
        if (console_shell_is_dirty(shell)) {
            result = present(shell);
            if (result != ESP_OK) {
                halt_dark("frame-submit", result);
            }
        }
        if (s_loop_count % 300U == 0U) {
            log_runtime_stats(shell);
        }
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(CONSOLE_FRAME_INTERVAL_MS));
    }
}
