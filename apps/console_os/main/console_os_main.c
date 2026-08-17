// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * P4 Console OS: a small FreeRTOS-native foreground shell. Built-ins consume
 * platform services directly; validated P4G cartridges run through a bounded
 * host table and the pinned ELF loader. Doom remains an exclusive one-way
 * handoff because the imported engine has no reviewed reentrant teardown.
 */

#include <stdbool.h>
#include <inttypes.h>
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
#include "p4/cartridge.h"
#include "p4/content_catalog.h"
#include "p4/desktop.h"
#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"
#include "p4/multiplayer.h"
#include "p4/platform.h"
#include "platform/board.h"
#include "platform/display.h"
#include "platform/game_catalog.h"
#include "platform/game_loader.h"
#include "platform/game_storage.h"
#include "platform/os_update.h"
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
#include "gamepad/gamepad.h"
#include "platform_gamepad_usb/platform_gamepad_usb.h"
#include "platform_usb_host/platform_usb_host.h"
#else
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
    CONSOLE_BUILTIN_APP_ID_MAX = CONSOLE_APP_TERMINAL,
    CONSOLE_FRAME_INTERVAL_MS = 16,
    CONSOLE_SUBMIT_TIMEOUT_MS = 250,
    CONSOLE_BACKLIGHT_PERCENT = 25,
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    CONSOLE_GAMEPAD_STICK_THRESHOLD = 12000,
#endif
    CONSOLE_CLEANUP_ATTEMPTS = 3,
    CONSOLE_NATIVE_AUDIO_VOLUME_STEP = 6,
    CONSOLE_BOOT_CHIME_TICKS = 44,
    CONSOLE_BOOT_ANIMATION_STEPS = 5,
    CONSOLE_BOOT_LOGO_WIDTH = 298,
    CONSOLE_BOOT_LOGO_HEIGHT = 43,
    CONSOLE_P4CART_SCAN_STACK_BYTES = 12 * 1024,
    CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK =
        P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ *
        CONSOLE_FRAME_INTERVAL_MS / 1000,
};

static const char *const TAG = "p4_console_os";
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static platform_i2c_shared_t *s_shared_bus;
static platform_touch_t *s_touch;
#endif
static uint16_t *s_pixels;
static bool s_display_initialized;
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static bool s_gamepad_ready;
static bool s_gamepad_connected;
static bool s_keyboard_connected;
static bool s_mouse_connected;
static uint32_t s_terminal_keyboard_session;
static uint8_t s_previous_terminal_keys[PLATFORM_USB_KEYBOARD_BOOT_KEY_COUNT];
static uint32_t s_gamepad_polls;
static uint32_t s_gamepad_poll_failures;
static uint32_t s_aux_input_poll_failures;
static uint16_t s_mouse_x = CONSOLE_SHELL_WIDTH / 2U;
static uint16_t s_mouse_y = CONSOLE_SHELL_HEIGHT / 2U;
#else
static bool s_touch_ready;
static uint32_t s_touch_polls;
static uint32_t s_touch_poll_failures;
#endif
static uint32_t s_doom_handoff_count;
static uint32_t s_loop_count;
static bool s_game_storage_initialized;
static bool s_game_storage_status_seen;
static platform_game_storage_status_t s_game_storage_status;
static platform_game_storage_file_listing_t s_platform_file_listing;
static console_shell_file_listing_t s_shell_file_listing;
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
static platform_os_update_info_t s_update_staging;
static console_shell_t s_shell;
static p4_achievement_catalog_t s_achievements;
static p4_save_catalog_t s_saves;
static p4_mp_session_t s_multiplayer_session;
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
extern const uint8_t _binary_bytebud_p4g_start[];
extern const uint8_t _binary_bytebud_p4g_end[];

static esp_err_t present(console_shell_t *shell);
static console_shell_runtime_info_t runtime_info(void);
static bool storage_app_owned(void);

void console_os_launch_doom(void);

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
        .title = "COLORS",
        .subtitle = "OS COLOR MODES",
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

_Static_assert((int)CONSOLE_SHELL_WIDTH ==
                   (int)PLATFORM_DISPLAY_GAME_WIDTH,
               "console width must match the platform game surface");
_Static_assert((int)CONSOLE_SHELL_HEIGHT ==
                   (int)PLATFORM_DISPLAY_GAME_HEIGHT,
               "console height must match the platform game surface");
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
_Static_assert(CONSOLE_SHELL_PHYSICAL_WIDTH == PLATFORM_TOUCH_WIDTH,
               "console touch width must match GT911 coordinates");
_Static_assert(CONSOLE_SHELL_PHYSICAL_HEIGHT == PLATFORM_TOUCH_HEIGHT,
               "console touch height must match GT911 coordinates");
_Static_assert(CONSOLE_SHELL_MAX_CONTACTS == PLATFORM_TOUCH_MAX_CONTACTS,
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

static void sync_game_storage(void)
{
    platform_game_storage_status_t status = {
        .state = PLATFORM_GAME_STORAGE_FAULT,
        .last_error = ESP_ERR_INVALID_STATE,
    };
    if (s_game_storage_initialized) {
        (void)platform_game_storage_refresh();
        if (platform_game_storage_get_status(&status) != ESP_OK) {
            status.state = PLATFORM_GAME_STORAGE_FAULT;
            status.last_error = ESP_FAIL;
        }
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
    portENTER_CRITICAL(&s_p4cart_scan_lock);
    s_p4cart_scan_result = result;
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
             "mode=background generation=%lu writes=0",
             PLATFORM_GAME_STORAGE_MOUNT_POINT,
             P4_CONTENT_CART_DIRECTORY,
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
             "runtime=lua-pending writes=0",
             (unsigned)s_p4cart_catalog.valid_cart_count,
             (unsigned)s_p4cart_catalog.invalid_cart_count,
             (unsigned)s_p4cart_catalog.candidates_seen,
             s_p4cart_catalog.directory_truncated ? 1U : 0U,
             (unsigned long)scan_generation);
    return true;
}

static esp_err_t reload_game_catalog(void)
{
    const esp_err_t storage_result =
        platform_game_catalog_scan(&s_catalog_staging);
    const size_t default_game_bytes =
        (size_t)(_binary_bytebud_p4g_end - _binary_bytebud_p4g_start);
    const esp_err_t default_result =
        platform_game_catalog_add_embedded_fallback(
            &s_catalog_staging, "BYTEBUD.P4G", _binary_bytebud_p4g_start,
            default_game_bytes);
    memset(&s_update_staging, 0, sizeof(s_update_staging));
    const esp_err_t update_result =
        platform_os_update_inspect(&s_update_staging);
    const bool catalog_available =
        storage_result == ESP_OK || default_result == ESP_OK;
    if (catalog_available) {
        s_game_catalog = s_catalog_staging;
    } else {
        memset(&s_game_catalog, 0, sizeof(s_game_catalog));
    }
    s_os_update_info = s_update_staging;
    set_game_manager_update_state(s_os_update_info.state);
    s_catalog_seen = catalog_available;
    s_catalog_storage_generation = s_game_storage_status.generation;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS GAME_CATALOG available=%u packages=%u "
             "valid=%u omitted=%lu generation=%lu embedded_default=%u "
             "storage_result=%s update=%u result=%s",
             s_game_catalog.available ? 1U : 0U,
             (unsigned)s_game_catalog.entry_count,
             (unsigned)s_game_catalog.valid_count,
             (unsigned long)s_game_catalog.omitted_packages,
             (unsigned long)s_catalog_storage_generation,
             default_result == ESP_OK ? 1U : 0U,
             esp_err_to_name(storage_result),
             (unsigned)s_os_update_info.state,
             esp_err_to_name(!catalog_available
                ? (default_result != ESP_OK
                    ? default_result : storage_result)
                : update_result));
    return catalog_available ? ESP_OK :
        (default_result != ESP_OK ? default_result : storage_result);
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
    if (s_game_storage_initialized) {
        result = platform_game_storage_list_root(&s_platform_file_listing);
    }
    if (result == ESP_OK) {
        s_shell_file_listing.available = true;
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
            output->removable = !input->is_directory;
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
             "P4_CONSOLE_OS FILES_REFRESH storage=%s available=%u "
             "visible=%lu hidden=%lu omitted=%lu generation=%lu result=%s",
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
        target->removable = !source->embedded;
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
            result = platform_game_storage_remove_root_file(entry->name);
        }
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS FILES_DELETE file=%s result=%s",
             label, esp_err_to_name(result));
    sync_game_storage();
    (void)reload_file_listing(
        shell, result == ESP_OK
            ? CONSOLE_FILE_NOTICE_DELETED : CONSOLE_FILE_NOTICE_ERROR);
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
        .controller_ready = s_gamepad_connected,
        .keyboard_ready = s_keyboard_connected,
        .mouse_ready = s_mouse_connected,
        .sd_card_storage = true,
        .audio_handoff_ready = true,
#else
        .board_kind =
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
            CONSOLE_BOARD_WAVESHARE_4_3,
#else
            CONSOLE_BOARD_ELECROW_10,
#endif
        .touch_ready = s_touch_ready,
        .controller_ready = false,
        .keyboard_ready = false,
        .mouse_ready = false,
        .sd_card_storage =
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
            true,
#else
            false,
#endif
        /* Compiled handoff only; the shell itself never starts audio. */
        .audio_handoff_ready = true,
#endif
        .game_storage_usb_attached = s_game_storage_status.usb_attached,
        .doom_wad_ready =
            s_game_storage_status.state == PLATFORM_GAME_STORAGE_APP_READY,
        .content_scan_complete = s_catalog_seen,
        .usb_content_ready = false,
        .multiplayer_core_ready = true,
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
        .physical_keyboard_ready = s_keyboard_connected,
#else
        .physical_keyboard_ready = false,
#endif
        .valid_cart_count = s_game_catalog.valid_count > UINT16_MAX
            ? UINT16_MAX : (uint16_t)s_game_catalog.valid_count,
        .builtin_game_count = (uint16_t)(
            1U + sizeof(s_builtin_apps) / sizeof(s_builtin_apps[0])),
    };
    return info;
}

#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
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

static void create_usb_input_or_continue(void)
{
    esp_err_t result = platform_usb_host_start(NULL);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS USB_INPUT_DEGRADED stage=usb-host error=%s",
                 esp_err_to_name(result));
        return;
    }
    result = platform_gamepad_usb_start();
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS USB_INPUT_DEGRADED stage=hid error=%s",
                 esp_err_to_name(result));
        (void)platform_usb_host_stop(pdMS_TO_TICKS(1000U));
        return;
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
        return;
    }
    s_gamepad_ready = true;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS USB_INPUT_READY "
             "classes=gamepad,keyboard,mouse transport=usb-hid "
             "topology=integrated-powered-hub ports=4");
}

static console_shell_action_t poll_input(console_shell_t *shell)
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
            s_mouse_x, input.mouse.delta_x, CONSOLE_SHELL_WIDTH);
        s_mouse_y = move_pointer_axis(
            s_mouse_y, input.mouse.delta_y, CONSOLE_SHELL_HEIGHT);
        const bool left_down =
            (input.mouse.buttons & UINT8_C(0x01)) != 0U;
        console_shell_set_pointer(
            shell, true, s_mouse_x, s_mouse_y, left_down);
        const console_shell_contact_t contact = {
            .x = (uint16_t)((uint32_t)CONSOLE_SHELL_VIEWPORT_LEFT +
                (uint32_t)s_mouse_x *
                    (uint32_t)CONSOLE_SHELL_VIEWPORT_SCALE + UINT32_C(1)),
            .y = (uint16_t)((uint32_t)s_mouse_y *
                (uint32_t)CONSOLE_SHELL_VIEWPORT_SCALE + UINT32_C(1)),
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
#else
static void create_touch_or_continue(void)
{
    esp_err_t result = platform_i2c_shared_create(&s_shared_bus);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS TOUCH_DEGRADED stage=shared-bus error=%s",
                 esp_err_to_name(result));
        return;
    }

    platform_touch_config_t config;
    platform_touch_config_init(
        &config, platform_i2c_shared_handle(s_shared_bus));
    result = platform_touch_create(&config, &s_touch);
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

static console_shell_action_t poll_input(console_shell_t *shell)
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

static void log_runtime_stats(const console_shell_t *shell)
{
    platform_display_stats_t display = {0};
    const esp_err_t result = platform_display_get_stats(&display);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "P4_CONSOLE_OS STATS_UNAVAILABLE error=%s",
                 esp_err_to_name(result));
        return;
    }
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
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
    return platform_display_submit_rgb565(
        s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
}

static void draw_gamechangers_ai_logo(p4_game_surface_t *surface,
                                      int left, int top)
{
    if (surface == NULL) {
        return;
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

static esp_err_t present_boot_screen(unsigned animation_step)
{
    if (s_pixels == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    p4_game_surface_t surface = {
        .pixels = s_pixels,
        .stride_pixels = CONSOLE_SHELL_WIDTH,
        .width = CONSOLE_SHELL_WIDTH,
        .height = CONSOLE_SHELL_HEIGHT,
    };
    p4_draw_clear(&surface, UINT16_C(0x0000));
    draw_gamechangers_ai_logo(&surface, 11, 54);
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
    return platform_display_submit_rgb565(
        s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
}

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

static void wait_with_boot_animation(void)
{
    const unsigned ticks_per_step = CONSOLE_BOOT_CHIME_TICKS /
        CONSOLE_BOOT_ANIMATION_STEPS;
    for (unsigned tick = 0U; tick < CONSOLE_BOOT_CHIME_TICKS; ++tick) {
        if (tick != 0U && tick % ticks_per_step == 0U) {
            const esp_err_t result =
                present_boot_screen(tick / ticks_per_step);
            if (result != ESP_OK) {
                halt_dark("boot-animation-frame", result);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_FRAME_INTERVAL_MS));
    }
}

static void play_boot_chime(void)
{
    if (!native_audio_runtime_allowed()) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BOOT_CHIME status=silent reason=runtime-gate");
        wait_with_boot_animation();
        return;
    }
    p4_game_platform_audio_t audio;
    p4_game_platform_audio_init(&audio);
    const esp_err_t open_result = p4_game_platform_audio_open(
        &audio, true, native_audio_control_bus(), 5U);
    if (open_result != ESP_OK) {
        if (audio.hardware_touched &&
            (!audio.safe_high_proven || audio.backend != NULL)) {
            halt_dark("boot-chime-open-safety", open_result);
        }
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BOOT_CHIME status=silent stage=open error=%s",
                 esp_err_to_name(open_result));
        wait_with_boot_animation();
        return;
    }
    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    static const p4_tone_t chord[] = {
        {.frequency_hz = 440U, .duration_ms = 680U, .volume_step = 3U,
         .waveform = P4_WAVE_TRIANGLE},
        {.frequency_hz = 554U, .duration_ms = 680U, .volume_step = 3U,
         .waveform = P4_WAVE_TRIANGLE},
        {.frequency_hz = 659U, .duration_ms = 680U, .volume_step = 3U,
         .waveform = P4_WAVE_TRIANGLE},
    };
    for (size_t index = 0U; index < sizeof(chord) / sizeof(chord[0]);
         ++index) {
        (void)p4_audio_mixer_play_tone(&mixer, &chord[index]);
    }
    bool played = true;
    const unsigned ticks_per_step = CONSOLE_BOOT_CHIME_TICKS /
        CONSOLE_BOOT_ANIMATION_STEPS;
    for (unsigned tick = 0U; tick < CONSOLE_BOOT_CHIME_TICKS; ++tick) {
        if (!pump_native_audio(&audio, &mixer)) {
            played = false;
            break;
        }
        if (tick != 0U && tick % ticks_per_step == 0U) {
            const esp_err_t frame_result =
                present_boot_screen(tick / ticks_per_step);
            if (frame_result != ESP_OK) {
                halt_dark("boot-animation-frame", frame_result);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_FRAME_INTERVAL_MS));
    }
    close_native_audio_or_halt(&audio);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BOOT_CHIME status=%s notes=A4-C#5-E5 "
             "source=original-startup-chord",
             played ? "played" : "degraded");
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
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
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
    p4_game_input_mapper_update(
        &context->input_mapper, false, NULL, 0U,
        digital_buttons, out_input);
#else
    platform_touch_frame_t frame;
    const bool valid = read_touch_frame(&frame);
    p4_physical_touch_t touches[P4_INPUT_MAX_TOUCHES];
    const size_t touch_count = valid ? frame.contact_count : 0U;
    for (size_t index = 0U; index < touch_count; ++index) {
        touches[index].x = frame.contacts[index].x;
        touches[index].y = frame.contacts[index].y;
    }
    p4_game_input_mapper_update(
        &context->input_mapper, valid,
        touch_count == 0U ? NULL : touches,
        touch_count, 0U, out_input);
#endif
    if (context->audio_running) {
        context->audio_running = pump_native_audio(
            &context->audio, &context->mixer);
    }
    if (context->frames != UINT32_MAX) {
        ++context->frames;
    }
    if (context->frames != 0U && context->frames % 300U == 0U) {
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
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
            native_audio_control_bus(), CONSOLE_NATIVE_AUDIO_VOLUME_STEP);
        if (audio_result == ESP_OK) {
            context.audio_running = true;
        } else if (context.audio.hardware_touched &&
                   (!context.audio.safe_high_proven ||
                    context.audio.backend != NULL)) {
            halt_dark("cartridge-audio-open-safety", audio_result);
        }
    }
    p4_game_input_mapper_init(&context.input_mapper);
    p4_cartridge_host_v1_t host = {
        .magic = P4_CARTRIDGE_HOST_MAGIC,
        .api_version = P4_CARTRIDGE_HOST_API_VERSION,
        .struct_bytes = sizeof(host),
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS |
            (context.audio_running
                ? P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_AUDIO_STREAM : 0U),
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
    };
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS CARTRIDGE_START app=%s file=%s api=1 "
             "source=%s runtime=psram-elf audio=%s",
             game->package.id, game->file_name,
             game->embedded ? "embedded-default" : "removable-storage",
             context.audio_running ? "ready" : "silent");
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
    console_os_launch_doom();
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
    const esp_err_t storage_result = platform_game_storage_init();
    s_game_storage_initialized = storage_result == ESP_OK;
    if (!s_game_storage_initialized) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS GAME_STORAGE_DEGRADED error=%s "
                 "native_apps=available",
                 esp_err_to_name(storage_result));
    }
    sync_game_storage();
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MAIN_STACK stage=storage-ready "
             "low_water_bytes=%u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    if (storage_app_owned()) {
        (void)reload_game_catalog();
    } else {
        memset(&s_game_catalog, 0, sizeof(s_game_catalog));
        memset(&s_os_update_info, 0, sizeof(s_os_update_info));
        s_os_update_info.state = PLATFORM_OS_UPDATE_UNAVAILABLE;
    }
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

#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    create_usb_input_or_continue();
#else
    create_touch_or_continue();
#endif
    result = present_boot_screen(0U);
    if (result != ESP_OK) {
        halt_dark("boot-frame", result);
    }
    result = platform_display_set_brightness(CONSOLE_BACKLIGHT_PERCENT);
    if (result != ESP_OK) {
        halt_dark("boot-backlight", result);
    }
    play_boot_chime();
    const console_shell_runtime_info_t initial_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &initial_runtime);
    result = present(shell);
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
             "storage=microsd-read-only removal=power-off-first");
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

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        ++s_loop_count;
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
        const console_shell_action_t action = poll_input(shell);
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(shell, &current_runtime);
        if (action.type == CONSOLE_ACTION_COLOR_MODE_CHANGED) {
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS COLOR_MODE mode=%u persistence=session-only",
                     (unsigned)action.color_mode);
        } else if (action.type == CONSOLE_ACTION_PAGE_CHANGED &&
            action.app_id == CONSOLE_APP_FILES) {
            (void)reload_file_listing(
                shell, CONSOLE_FILE_NOTICE_NONE);
        } else if (action.type == CONSOLE_ACTION_PAGE_CHANGED &&
                   action.app_id == CONSOLE_APP_GAMES) {
            (void)reload_manager_listing(
                shell, CONSOLE_FILE_NOTICE_NONE);
        } else if (action.type == CONSOLE_ACTION_FILE_REFRESH ||
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
