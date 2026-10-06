// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * P4 Console OS: a small FreeRTOS-native foreground shell. Built-ins consume
 * platform services directly; validated P4G cartridges run through a bounded
 * host table and the pinned ELF loader. Doom remains an exclusive one-way
 * handoff because the imported engine has no reviewed reentrant teardown.
 */

#include "cartridge_timing.h"
#include <stdbool.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "console/shell.h"
#include "console/game_art.h"
#include "console/startup.h"
#include "doom_p4mp_adapter.h"
#include "sdkconfig.h"
#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "p4/audio.h"
#include "p4/achievements.h"
#include "p4/bbs_ui.h"
#include "p4/ble_radio_handoff.h"
#include "p4/cartridge.h"
#include "p4/content_transfer.h"
#include "p4/desktop.h"
#include "p4/doom_multiplayer.h"
#include "p4/draw.h"
#include "p4/file_transfer.h"
#include "p4/game.h"
#include "p4/game_save_service.h"
#include "p4/h1_usb_drive_control.h"
#include "p4/input.h"
#include "p4/multiplayer.h"
#include "p4/multiplayer_group.h"
#include "p4/multiplayer_ble.h"
#include "p4/multiplayer_registry.h"
#include "p4/multiplayer_uart.h"
#include "p4/platform.h"
#include "p4/audio_worker.h"
#include "p4/frame_scheduler.h"
#include "mbedtls/sha256.h"
#include "platform/board.h"
#if CONFIG_P4_BOARD_M5STACK_TAB5
#include "platform/tab5_sensors.h"
#include "platform/tab5_game_motion.h"
#include "p4/clock_control.h"
#endif
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
#include "platform_battery/battery.h"
#endif
#include "platform/console_settings.h"
#include "platform/display.h"

/* Runtime telemetry is useful during bring-up, but its multi-line log burst
 * is too expensive to schedule during normal interactive use. Diagnostic
 * builds opt in with -DP4_CONSOLE_RUNTIME_STATS_BUILD=ON. */
#ifndef CONSOLE_OS_ENABLE_RUNTIME_STATS
#define CONSOLE_OS_ENABLE_RUNTIME_STATS 0
#endif
#include "platform/game_catalog.h"
#include "platform/game_loader.h"
#include "platform/game_storage.h"
#include "platform/os_update.h"
#include "platform/save_seal.h"
#include "p4_protected_game_lineage.h"
#if CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#define P4_CONSOLE_LARGE_BSS EXT_RAM_BSS_ATTR
#else
#define P4_CONSOLE_LARGE_BSS
#endif
#ifndef P4_CONSOLE_WIFI_MULTIPLAYER
#define P4_CONSOLE_WIFI_MULTIPLAYER 0
#endif
#if P4_CONSOLE_WIFI_MULTIPLAYER
#include "platform/multiplayer_wifi.h"
#endif
#ifndef P4_CONSOLE_SIGNAL_SCAN
#define P4_CONSOLE_SIGNAL_SCAN 0
#endif
#ifndef P4_CONSOLE_BLE_MULTIPLAYER
#define P4_CONSOLE_BLE_MULTIPLAYER 0
#endif
#ifndef P4_CONSOLE_BLE_GAMEPAD
#define P4_CONSOLE_BLE_GAMEPAD 0
#endif
#if P4_CONSOLE_SIGNAL_SCAN
#include "platform/signal_scan.h"
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
#include "p4/multiplayer_ble.h"
#include "platform/multiplayer_ble.h"
#include "platform/dice_ble.h"
#endif
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
    (CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
     CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE) || \
    (CONFIG_P4_BOARD_M5STACK_TAB5 && CONFIG_P4_TAB5_USB_HOST)
#define P4_CONSOLE_USB_INPUT 1
#else
#define P4_CONSOLE_USB_INPUT 0
#endif
#if P4_CONSOLE_USB_INPUT || P4_CONSOLE_BLE_GAMEPAD
#define P4_CONSOLE_GAMEPAD_INPUT 1
#else
#define P4_CONSOLE_GAMEPAD_INPUT 0
#endif
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
    CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE && \
    CONFIG_P4_WAVESHARE_H2_RUNTIME_ROLE_SWITCH
#define P4_CONSOLE_H1_USB_DRIVE_CONTROL 1
#else
#define P4_CONSOLE_H1_USB_DRIVE_CONTROL 0
#endif


typedef enum {
    P4_PROTECTED_GAME_UNPROTECTED = 0,
    P4_PROTECTED_GAME_TRUSTED,
    P4_PROTECTED_GAME_REJECTED,
} p4_protected_game_result_t;

static p4_protected_game_result_t protected_game_lineage_check(
    const p4_game_package_info_t *package)
{
    if (package == NULL) {
        return P4_PROTECTED_GAME_REJECTED;
    }
    bool protected_id = false;
    for (size_t index = 0U;
         index < (size_t)P4_PROTECTED_GAME_LINEAGE_COUNT; ++index) {
        const p4_protected_game_lineage_t *const lineage =
            &s_protected_game_lineages[index];
        if (strcmp(package->id, lineage->game_id) != 0) {
            continue;
        }
        protected_id = true;
        if (memcmp(package->payload_sha256, lineage->payload_sha256,
                   sizeof(package->payload_sha256)) == 0) {
            return P4_PROTECTED_GAME_TRUSTED;
        }
    }
    return protected_id
        ? P4_PROTECTED_GAME_REJECTED : P4_PROTECTED_GAME_UNPROTECTED;
}

#if P4_CONSOLE_GAMEPAD_INPUT
#include "gamepad/gamepad.h"
#include "platform/gamepad.h"
#endif
#if P4_CONSOLE_BLE_GAMEPAD
#include "platform/gamepad_ble.h"
#endif
#if P4_CONSOLE_USB_INPUT
#include "platform_gamepad_usb/platform_gamepad_usb.h"
#include "platform_usb_host/platform_usb_host.h"
#if CONFIG_P4_BOARD_M5STACK_TAB5 && CONFIG_P4_TAB5_USB_HOST
#include "platform_gamepad_xusb/xusb.h"
#endif
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
    CONSOLE_APP_STORAGE = 13,
    CONSOLE_APP_CHEX_QUEST = 14,
    CONSOLE_APP_FILE_TRANSFER = 15,
    CONSOLE_APP_CONTROLLERS = 16,
    CONSOLE_APP_POWER = 17,
    CONSOLE_APP_SENSORS = 18,
    CONSOLE_BUILTIN_APP_ID_MAX = CONSOLE_APP_SENSORS,
    CONSOLE_GAME_UPDATE_HZ = 60,
    CONSOLE_SUBMIT_TIMEOUT_MS = 250,
    CONSOLE_BACKLIGHT_PERCENT = 25,
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    /* The launcher consumes newest GT911 input at 60 Hz, but the sampler
     * runs independently so a PPA/display wait cannot defer acquisition. */
    CONSOLE_TOUCH_MAILBOX_HZ = 120,
    CONSOLE_TOUCH_MAILBOX_STACK_BYTES = 4096,
    CONSOLE_TOUCH_MAILBOX_STOP_TIMEOUT_MS = 500,
    CONSOLE_TOUCH_MAILBOX_MAX_AGE_US = 50000,
#endif
#if P4_CONSOLE_GAMEPAD_INPUT
    CONSOLE_GAMEPAD_STICK_THRESHOLD = 12000,
#endif
#if P4_CONSOLE_USB_INPUT
    CONSOLE_USB_ENUM_GUARD_CONFIRM_MS = 10000,
#endif
    CONSOLE_RUNTIME_INFO_INTERVAL_MS = 200,
    CONSOLE_BATTERY_SAMPLE_INTERVAL_MS = 5000,
    CONSOLE_BATTERY_DISPLAY_MV_HYSTERESIS = 50,
    CONSOLE_STORAGE_SYNC_INTERVAL_MS = 200,
    CONSOLE_STATS_INTERVAL_MS = 3000,
    CONSOLE_RUNTIME_HEALTH_CONFIRM_MS = 10000,
    CONSOLE_CLEANUP_ATTEMPTS = 3,
    CONSOLE_BOOT_POST_BEEP_MS = 220,
    CONSOLE_BOOT_POST_GAP_MS = 180,
    CONSOLE_BOOT_DTMF_TONE_MS = 160,
    CONSOLE_BOOT_DTMF_GAP_MS = 90,
    CONSOLE_BOOT_DTMF_DASH_MS = 220,
    CONSOLE_BOOT_ANIMATION_STEPS = 5,
    CONSOLE_STORAGE_LOADING_FRAME_MS = 160,
    CONSOLE_BBS_CONNECT_HOLD_MS = 260,
    CONSOLE_HOME_REVEAL_STEPS = 6,
    CONSOLE_HOME_REVEAL_DELAY_MS = 45,
    CONSOLE_BOOT_SUBMIT_ATTEMPTS = 3,
    CONSOLE_BOOT_SUBMIT_RETRY_MS = 20,
    CONSOLE_BOOT_LOGO_WIDTH = 112,
    CONSOLE_BOOT_LOGO_HEIGHT = 112,
    CONSOLE_MULTIPLAYER_DISCOVERY_INTERVAL_MS = 1000,
    CONSOLE_MULTIPLAYER_BLE_BROWSER_SETTLE_MS = 1800,
    CONSOLE_MULTIPLAYER_KEEPALIVE_INTERVAL_MS = 1000,
    CONSOLE_MULTIPLAYER_START_READY_INTERVAL_MS = 100,
    CONSOLE_MULTIPLAYER_START_HOLD_MS = 1500,
    CONSOLE_MULTIPLAYER_START_TIMEOUT_MS = 15000,
    CONSOLE_MULTIPLAYER_PEER_TIMEOUT_MS = 15000,
    CONSOLE_DOOM_MULTIPLAYER_PROTOCOL = 3,
    CONSOLE_NATIVE_MULTIPLAYER_RUNTIME_PLAYERS = 4,
    CONSOLE_NATIVE_MULTIPLAYER_QUEUE_DEPTH = 8,
    CONSOLE_STORAGE_INIT_STACK_BYTES = 12 * 1024,
    /* Exact P4G inspection has deep FAT/VFS + manifest call chains. Keep the
     * background worker comfortably above the measured 16 KiB overflow. */
    CONSOLE_GAME_CATALOG_STACK_BYTES = 32 * 1024,
    CONSOLE_GAME_SAVE_STACK_BYTES = 8 * 1024,
    CONSOLE_GAME_SAVE_STOP_TIMEOUT_MS = 5000,
    CONSOLE_NATIVE_AUDIO_FRAMES_MAX =
        (P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ +
         CONSOLE_GAME_UPDATE_HZ - 1) / CONSOLE_GAME_UPDATE_HZ,
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
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
/* Sealed from waveshare-4.3-unit3's read-only 0.5.10 GT911 capture, with the
 * vendor byte confirmed by the 0.5.11 experiment. This is intentionally not a
 * generic GT911 profile: restoration refuses every state except this complete
 * original block or the exact rejected filter-4 derivative. */
enum {
    CONSOLE_GT911_ORIGINAL_NORMAL_FILTER = 8U,
};

static const uint8_t s_waveshare_gt911_expected_identity[
    PLATFORM_TOUCH_GT911_IDENTITY_BYTES] = {
    0x39U, 0x31U, 0x31U, 0x00U, 0x60U,
    0x10U, 0xe0U, 0x01U, 0x20U, 0x03U, 0x00U,
};

static const uint8_t s_waveshare_gt911_original_config[
    PLATFORM_TOUCH_GT911_CONFIG_BYTES] = {
    0x41U, 0xe0U, 0x01U, 0x20U, 0x03U, 0x05U, 0x35U, 0x20U, 0x22U, 0x08U, 0x28U, 0x05U, 0x5aU, 0x3cU, 0x03U, 0x05U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x18U, 0x1aU, 0x1eU, 0x14U, 0x87U, 0x27U, 0x09U, 0xcdU, 0xcfU,
    0xb5U, 0x06U, 0x00U, 0x00U, 0x00U, 0x20U, 0x02U, 0x10U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0xb4U, 0xefU, 0x94U, 0xd5U, 0x02U, 0x08U, 0x00U, 0x00U, 0x04U, 0x87U, 0xb9U, 0x00U, 0x82U,
    0xc4U, 0x00U, 0x7eU, 0xcfU, 0x00U, 0x7bU, 0xdbU, 0x00U, 0x78U, 0xe8U, 0x00U, 0x78U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x12U, 0x10U, 0x0eU, 0x0cU, 0x0aU, 0x08U, 0x06U, 0x04U, 0x02U, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
    0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0x00U, 0x02U,
    0x04U, 0x06U, 0x08U, 0x0aU, 0x0cU, 0x24U, 0x22U, 0x21U, 0x20U, 0x1fU, 0x1eU, 0x1dU, 0xffU, 0xffU, 0xffU, 0xffU,
    0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
    0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0x79U, 0x00U,
};

_Static_assert(sizeof(s_waveshare_gt911_original_config) ==
                   PLATFORM_TOUCH_GT911_CONFIG_BYTES,
               "GT911 original configuration must be complete");

static void log_waveshare_gt911_restore(
    const char *action,
    const platform_touch_gt911_restore_reviewed_baseline_result_t *result,
    esp_err_t status)
{
    const platform_touch_gt911_info_t empty = {0};
    const platform_touch_gt911_restore_reviewed_baseline_result_t
        empty_result = {.result = ESP_ERR_INVALID_STATE};
    if (result == NULL) {
        result = &empty_result;
    }
    const platform_touch_gt911_info_t *before = &result->before;
    const platform_touch_gt911_info_t *observed = &result->observed;
    if (result == &empty_result) {
        before = &empty;
        observed = &empty;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS GT911_CONFIG_RESTORE target_normal_filter=%u "
             "result=%s action=%s vendor=0x%02x "
             "before_filter=%u before_checksum=0x%02x "
             "after_filter=%u after_checksum=0x%02x "
             "changed=%u already_original=%u may_have_changed=%u "
             "restore_result=%s",
             (unsigned)CONSOLE_GT911_ORIGINAL_NORMAL_FILTER,
             status == ESP_OK ? "ready" : "not-ready", action,
             (unsigned)before->vendor_id,
             (unsigned)before->normal_filter,
             (unsigned)before->config_checksum,
             (unsigned)observed->normal_filter,
             (unsigned)observed->config_checksum,
             result->changed ? 1U : 0U,
             result->already_original ? 1U : 0U,
             result->may_have_changed ? 1U : 0U,
             esp_err_to_name(status));
}
#endif
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static platform_i2c_shared_t *s_shared_bus;
static platform_touch_t *s_touch;
#endif
static uint16_t *s_pixels;
static bool s_display_initialized;
#if CONFIG_P4_BOARD_M5STACK_TAB5
/* Only the boot worker owns s_pixels until its completion semaphore is
 * acquired. Stop/join before Home or a fatal diagnostic reuses the buffer. */
static TaskHandle_t s_boot_task;
static SemaphoreHandle_t s_boot_done;
static portMUX_TYPE s_boot_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_boot_stop;
static char s_boot_status[64] = "Starting your console";
static unsigned s_boot_frames;
static unsigned s_boot_max_frame_ms;
static unsigned s_boot_max_render_ms;
static unsigned s_boot_max_submit_ms;
static esp_err_t s_boot_animation_error;
#endif
#if P4_CONSOLE_GAMEPAD_INPUT
static bool s_gamepad_connected;
static platform_gamepad_transport_t s_gamepad_transport =
    PLATFORM_GAMEPAD_TRANSPORT_NONE;
static uint32_t s_gamepad_polls;
static uint32_t s_gamepad_poll_failures;
static gamepad_button_mapping_t s_controller_mapping_current;
static gamepad_button_mapping_t s_controller_mapping_pending;
static bool s_controller_mapping_active;
static bool s_controller_mapping_wait_neutral;
static bool s_controller_mapping_input_suppressed;
static bool s_controller_mapping_persistent;
static uint8_t s_controller_mapping_target;
static int s_controller_mapping_last_error;
#endif
#if P4_CONSOLE_USB_INPUT
static bool s_gamepad_ready;
static bool s_keyboard_connected;
static bool s_mouse_connected;
static bool s_mouse_pointer_active;
static uint32_t s_terminal_keyboard_session;
static uint8_t s_previous_terminal_keys[PLATFORM_USB_KEYBOARD_BOOT_KEY_COUNT];
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
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
/* Only the mailbox worker calls platform_touch_poll while it is running.
 * Games stop and join it before retaining their established direct polling
 * contract. The short spinlock section publishes/copies a whole frame, so a
 * consumer cannot observe a torn contact list. */
static platform_touch_frame_t s_touch_mailbox_frame;
static uint32_t s_touch_mailbox_samples;
static uint32_t s_touch_mailbox_failures;
static uint32_t s_touch_mailbox_stale_reads;
static uint32_t s_touch_mailbox_age_last_us;
static uint32_t s_touch_mailbox_age_max_us;
static uint32_t s_touch_mailbox_unique_reports;
static uint32_t s_touch_mailbox_report_interval_samples;
static uint32_t s_touch_mailbox_report_interval_total_us;
static uint32_t s_touch_mailbox_report_interval_min_us;
static uint32_t s_touch_mailbox_report_interval_max_us;
/* Owned by the mailbox worker; zero also separates independent contact runs. */
static int64_t s_touch_mailbox_last_report_timestamp_us;
static TaskHandle_t s_touch_mailbox_task;
static bool s_touch_mailbox_stop_requested;
/* Set only by a touch sample that changes launcher state.  It is consumed by
 * the next interactive present, rather than repeatedly attributing idle
 * mailbox snapshots to unrelated frames. */
static int64_t s_interactive_touch_pending_timestamp_us;
static StaticSemaphore_t s_touch_mailbox_stopped_storage;
static SemaphoreHandle_t s_touch_mailbox_stopped;
static portMUX_TYPE s_touch_mailbox_lock = portMUX_INITIALIZER_UNLOCKED;
#endif
static uint32_t s_doom_handoff_count;
static uint32_t s_loop_count;
static int64_t s_runtime_services_ready_us;
/* UI timing remains task-owned: these bounded counters are diagnostic only. */
static uint32_t s_ui_timing_dirty_frames;
static uint32_t s_ui_timing_animation_frames;
static uint32_t s_ui_timing_last_render_us;
static uint32_t s_ui_timing_max_render_us;
static uint32_t s_ui_timing_last_submit_us;
static uint32_t s_ui_timing_max_submit_us;
static uint32_t s_ui_timing_last_dirty_interval_us;
static uint32_t s_ui_timing_max_dirty_interval_us;
static int64_t s_ui_timing_last_dirty_us;
static uint32_t s_ui_timing_animation_interval_last_us;
static uint32_t s_ui_timing_animation_interval_max_us;
static uint32_t s_ui_timing_animation_missed_frames;
static uint32_t s_ui_timing_animation_burst_frames;
static uint32_t s_ui_timing_animation_burst_interval_sum_us;
static int64_t s_ui_timing_last_animation_us;
static bool s_ota_validation_attempted;
static bool s_game_storage_status_seen;
static platform_game_storage_status_t s_game_storage_status;
static console_storage_operation_t s_storage_operation =
    CONSOLE_STORAGE_OPERATION_NONE;
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
/* Keep large catalog snapshots in PSRAM so the 32 KiB internal reserve remains
 * available for USB/DMA and other latency-sensitive runtime allocations. */
static P4_CONSOLE_LARGE_BSS platform_game_catalog_t s_game_catalog;
static P4_CONSOLE_LARGE_BSS platform_game_catalog_t s_catalog_staging;
typedef enum {
    GAME_CATALOG_SCAN_IDLE = 0,
    GAME_CATALOG_SCAN_RUNNING,
    GAME_CATALOG_SCAN_DONE,
} game_catalog_scan_state_t;
static game_catalog_scan_state_t s_game_catalog_scan_state;
static TaskHandle_t s_game_catalog_scan_task;
static portMUX_TYPE s_game_catalog_scan_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_err_t s_game_catalog_scan_result = ESP_ERR_INVALID_STATE;
static unsigned s_game_catalog_scan_low_water_bytes;
static int64_t s_game_catalog_scan_started_us;
static p4_mp_game_registry_t s_multiplayer_game_registry;
static uint32_t s_file_transfer_generation_seen;
#if CONFIG_P4_BOARD_M5STACK_TAB5
static p4_clock_control_t s_clock_control;
#endif
#if P4_CONSOLE_H1_USB_DRIVE_CONTROL
static p4_h1_usb_drive_control_t s_h1_usb_drive_control;
#endif
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
static platform_battery_sample_t s_battery_sample;
static platform_battery_sample_t s_battery_raw_sample;
static esp_err_t s_battery_last_error = ESP_ERR_INVALID_STATE;
static bool s_battery_initialized;
static bool s_battery_sample_valid;
static bool s_gt911_reviewed_baseline_verified;
#endif

static platform_os_update_info_t s_update_staging;
static console_shell_t s_shell;
static p4_achievement_catalog_t s_achievements;
static p4_save_catalog_t s_saves;
static p4_mp_session_t s_multiplayer_session;
typedef enum {
    CONSOLE_MP_LOBBY_IDLE = 0,
    CONSOLE_MP_LOBBY_BROWSING,
    CONSOLE_MP_LOBBY_HOSTING,
    CONSOLE_MP_LOBBY_JOINING,
    CONSOLE_MP_LOBBY_CONNECTED,
} console_mp_lobby_state_t;
static console_mp_lobby_state_t s_multiplayer_lobby_state;
#if P4_CONSOLE_BLE_GAMEPAD && P4_CONSOLE_BLE_MULTIPLAYER
static bool s_ble_gamepad_suspended_lobby_browser;
static p4_ble_radio_handoff_t s_ble_radio_handoff;
#endif
static p4_mp_lobby_offer_t s_multiplayer_local_offer;
static p4_mp_lobby_offer_t s_multiplayer_remote_offer;
static bool s_multiplayer_shared_dice;
static bool s_multiplayer_launch_shared_dice;
#if P4_CONSOLE_BLE_MULTIPLAYER
static bool s_dice_ready;
#endif
typedef enum {
    CONSOLE_MP_LAUNCH_NONE = 0,
    CONSOLE_MP_LAUNCH_DOOM,
    CONSOLE_MP_LAUNCH_NATIVE,
} console_mp_launch_kind_t;
static console_mp_launch_kind_t s_multiplayer_launch_kind;
static size_t s_multiplayer_game_selection;
static uint8_t s_multiplayer_local_player_slot = P4_MP_PLAYER_SLOT_ANY;
static uint8_t s_multiplayer_player_count;
static uint64_t s_multiplayer_launch_route_id;
static uint64_t s_multiplayer_launch_session_seed;
static uint32_t s_multiplayer_native_launcher_id;
static p4_doom_mp_launch_config_t s_doom_multiplayer_launch;
static p4_doom_mp_setup_t s_multiplayer_local_setup = {
    .game = P4_DOOM_MP_GAME_DOOM,
    .mode = P4_DOOM_MP_MODE_DEATHMATCH,
    .episode = 1U,
    .map = 1U,
    .skill = 5U,
    .no_monsters = true,
};
static uint32_t s_multiplayer_local_peer_id;
static uint32_t s_multiplayer_local_session_id;
static uint32_t s_multiplayer_remote_peer_id;
static uint32_t s_multiplayer_remote_session_id;
static uint64_t s_multiplayer_remote_route_id;
static int64_t s_multiplayer_remote_offer_seen_us;
typedef struct {
    uint64_t lobby_id;
    uint64_t route_id;
    uint32_t session_id;
    uint32_t host_peer_id;
    uint16_t game_token;
    int8_t rssi;
    uint8_t players_present;
    uint8_t player_capacity;
    size_t game_selection;
    bool game_available;
} console_mp_lobby_candidate_t;
enum {
    CONSOLE_MP_MAX_LOBBY_CANDIDATES = 4,
};
static console_mp_lobby_candidate_t
    s_multiplayer_lobby_candidates[CONSOLE_MP_MAX_LOBBY_CANDIDATES];
static size_t s_multiplayer_lobby_candidate_count;
/** Zero means CREATE NEW; 1..count selects a discovered room. */
static size_t s_multiplayer_lobby_selection;
/** Keep the user's choice stable when RSSI sorting reorders nearby rooms. */
static uint64_t s_multiplayer_lobby_selected_id;
static uint32_t s_multiplayer_lobby_selected_session_id;
static int64_t s_multiplayer_lobby_browser_ready_us;
static bool s_multiplayer_lobby_create_explicit;
static uint32_t s_multiplayer_host_collision_seen_session;
static uint32_t s_multiplayer_target_session_id;
static uint64_t s_multiplayer_target_lobby_id;
static int64_t s_multiplayer_next_control_us;
static bool s_multiplayer_transport_ready;
typedef enum {
    CONSOLE_MP_TRANSPORT_WIRED = 0,
    CONSOLE_MP_TRANSPORT_BLE = 1,
    CONSOLE_MP_TRANSPORT_WIFI = 2,
} console_mp_transport_kind_t;
typedef struct {
    bool available;
    bool ready;
    bool starting;
    bool encrypted;
    uint64_t active_route_id;
    uint32_t rx_frames;
    uint32_t tx_frames;
} console_mp_transport_status_t;
#if P4_CONSOLE_WIFI_MULTIPLAYER
static int64_t s_wifi_join_deadline;
#endif
static console_mp_transport_kind_t s_multiplayer_transport =
    CONSOLE_MP_TRANSPORT_WIRED;
static bool s_multiplayer_uart_ready;
#if P4_CONSOLE_BLE_MULTIPLAYER
static bool s_multiplayer_ble_enable_pending;
#endif
static uint32_t s_multiplayer_discovery_sequence = 1U;
static int64_t s_multiplayer_next_discovery_us;
static int64_t s_multiplayer_peer_last_seen_us;
static bool s_multiplayer_discovery_reply_pending;
static p4_mp_start_barrier_t s_multiplayer_start_barrier;
static p4_mp_group_start_t s_multiplayer_group_start;
static bool multiplayer_group_enabled(void)
{
    return s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI &&
        s_multiplayer_local_offer.player_capacity > 2;
}
static uint8_t multiplayer_members(void)
{
    uint8_t count=1;
    for(unsigned i=0;i<P4_MP_MAX_REMOTE_PEERS;++i)
        if(s_multiplayer_session.peers[i].connected)++count;
    return count;
}
static bool multiplayer_accepting_members(void)
{
    return s_multiplayer_lobby_state==CONSOLE_MP_LOBBY_HOSTING ||
        (multiplayer_group_enabled() && s_multiplayer_session.role==P4_MP_ROLE_HOST &&
         s_multiplayer_lobby_state==CONSOLE_MP_LOBBY_CONNECTED &&
         s_multiplayer_group_start.phase==P4_MP_GROUP_IDLE);
}
static int64_t s_multiplayer_next_start_ready_us;
static bool s_multiplayer_launch_due;
typedef struct {
    p4_game_multiplayer_message_t
        queue[CONSOLE_NATIVE_MULTIPLAYER_QUEUE_DEPTH];
    size_t queue_head;
    size_t queue_count;
    uint32_t generation;
    uint32_t dropped_messages;
    uint64_t session_seed;
    uint64_t route_id;
    int64_t next_keepalive_us;
    p4_game_multiplayer_state_t state;
    p4_game_multiplayer_role_t role;
    uint8_t local_player_slot;
    uint8_t player_count;
    p4_game_multiplayer_profile_t profile;
    bool active;
} console_native_multiplayer_t;
static console_native_multiplayer_t s_native_multiplayer;
static uint32_t s_native_multiplayer_generation;
static platform_console_settings_t s_console_settings = {
    .version = PLATFORM_CONSOLE_SETTINGS_VERSION,
    .size = (uint16_t)sizeof(platform_console_settings_t),
    .boot_volume_step = PLATFORM_CONSOLE_BOOT_VOLUME_DEFAULT,
    .game_volume_step = PLATFORM_CONSOLE_GAME_VOLUME_DEFAULT,
    .node_name = "GC-P4-LOCAL",
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
static char s_chex_subtitle[CONSOLE_SHELL_SUBTITLE_MAX_BYTES] =
    "STORAGE CHECKING";
static char s_game_manager_subtitle[CONSOLE_SHELL_SUBTITLE_MAX_BYTES] =
    "P4G + OS";
static int16_t s_native_audio_pcm[
    CONSOLE_NATIVE_AUDIO_FRAMES_MAX *
    P4_GAME_PLATFORM_AUDIO_CHANNEL_COUNT];

extern const uint8_t _binary_gamechangers_boot_mark_rgb565a8_start[];
extern const uint8_t _binary_gamechangers_boot_mark_rgb565a8_end[];
extern const uint8_t _binary_gamechangers_ai_logo_rgb565_start[];
extern const uint8_t _binary_gamechangers_ai_logo_rgb565_end[];

static void multiplayer_frame_received(
    void *context,
    uint64_t route_id,
    const uint8_t *datagram,
    size_t datagram_length);

static console_mp_transport_status_t multiplayer_transport_status(void)
{
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI) {
        const platform_multiplayer_wifi_status_t status = platform_multiplayer_wifi_status();
        return (console_mp_transport_status_t){.available=status.available,.ready=status.ready,
            .starting=status.starting,.active_route_id=status.route_id,
            .rx_frames=status.rx_frames,.tx_frames=status.tx_frames};
    }
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        const platform_multiplayer_ble_status_t status =
            platform_multiplayer_ble_status();
        return (console_mp_transport_status_t){
            .available = status.host_ready,
            .ready = status.ready,
            .starting = status.enabled && !status.ready &&
                status.state != PLATFORM_MULTIPLAYER_BLE_OFF &&
                status.state != PLATFORM_MULTIPLAYER_BLE_ERROR,
            .encrypted = status.encrypted,
            .active_route_id = status.route_id,
            .rx_frames = status.rx_frames,
            .tx_frames = status.tx_frames,
        };
    }
#endif
    const p4_mp_uart_status_t status = p4_mp_uart_endpoint_status();
    return (console_mp_transport_status_t){
        .available = status.ready,
        .ready = status.ready,
        .active_route_id = status.active_route_id,
        .rx_frames = status.rx_frames,
        .tx_frames = status.tx_frames,
    };
}

static bool multiplayer_route_matches_transport(uint64_t route_id)
{
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI)
        return (route_id & UINT64_C(0xffff000000000000)) == P4_MP_WIFI_ROUTE_PREFIX;
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        return (route_id & UINT64_C(0xffff000000000000)) ==
            P4_MP_BLE_ROUTE_PREFIX;
    }
#endif
    return route_id == P4_MP_UART_RELAY_ROUTE_ID ||
        route_id == P4_MP_UART_DIRECT_ROUTE_ID;
}

static esp_err_t multiplayer_transport_send(
    const uint8_t *datagram,
    size_t datagram_length)
{
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI)
        return platform_multiplayer_wifi_send(datagram, datagram_length);
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        return platform_multiplayer_ble_send(datagram, datagram_length);
    }
#endif
    return p4_mp_uart_endpoint_send(datagram, datagram_length);
}

static void multiplayer_transport_poll(void)
{
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI) {
        platform_multiplayer_wifi_poll(); return;
    }
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        platform_multiplayer_ble_poll();
        return;
    }
#endif
    p4_mp_uart_endpoint_poll();
}

static esp_err_t multiplayer_transport_set_handler(
    p4_doom_p4mp_frame_handler_t handler,
    void *handler_context)
{
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI)
        return platform_multiplayer_wifi_set_handler(handler, handler_context);
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        return platform_multiplayer_ble_set_handler(
            handler, handler_context);
    }
#endif
    return p4_mp_uart_endpoint_set_handler(handler, handler_context);
}

static void multiplayer_transport_reset_route(void)
{
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI) {
        platform_multiplayer_wifi_reset_route(); return;
    }
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        platform_multiplayer_ble_reset_route();
        return;
    }
#endif
    p4_mp_uart_endpoint_reset_route();
}

static bool multiplayer_transport_connected(uint64_t route_id)
{
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI)
        return platform_multiplayer_wifi_route_connected(route_id);
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        return platform_multiplayer_ble_route_connected(route_id);
    }
#endif
    const p4_mp_uart_status_t status = p4_mp_uart_endpoint_status();
    return status.ready && route_id != P4_MP_UART_ROUTE_NONE &&
        status.active_route_id == route_id;
}

static const char *multiplayer_transport_route_name(uint64_t route_id)
{
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI) return "wifi-local";
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        return platform_multiplayer_ble_route_name(route_id);
    }
#endif
    return p4_mp_uart_route_name(route_id);
}

static esp_err_t doom_transport_set_handler(
    void *context,
    p4_doom_p4mp_frame_handler_t handler,
    void *handler_context)
{
    (void)context;
    return multiplayer_transport_set_handler(handler, handler_context);
}

static void doom_transport_poll(void *context)
{
    (void)context;
    multiplayer_transport_poll();
#if P4_CONSOLE_BLE_MULTIPLAYER
    static int64_t next_stats_us;
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        const int64_t now_us = esp_timer_get_time();
        if (next_stats_us == 0 || now_us >= next_stats_us) {
            const platform_multiplayer_ble_status_t status =
                platform_multiplayer_ble_status();
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS DOOM_BLE_TRANSPORT ready=%u "
                     "encrypted=%u mtu=%u tx=%lu rx=%lu drops=%lu "
                     "last_error=%d",
                     status.ready ? 1U : 0U,
                     status.encrypted ? 1U : 0U,
                     (unsigned)status.att_mtu,
                     (unsigned long)status.tx_frames,
                     (unsigned long)status.rx_frames,
                     (unsigned long)status.dropped_frames,
                     status.last_ble_error);
            next_stats_us = now_us + INT64_C(10000000);
        }
    }
#endif
}

static esp_err_t doom_transport_send(
    void *context,
    const uint8_t *datagram,
    size_t datagram_length)
{
    (void)context;
    return multiplayer_transport_send(datagram, datagram_length);
}

static esp_err_t doom_transport_send_to(void *context, uint64_t route_id,
                                         const uint8_t *datagram, size_t length)
{
    (void)context;
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI)
        return platform_multiplayer_wifi_send_to(route_id, datagram, length);
#endif
    (void)route_id;
    return multiplayer_transport_send(datagram, length);
}

static bool doom_transport_connected(void *context, uint64_t route_id)
{
    (void)context;
    return multiplayer_transport_connected(route_id);
}

static const char *doom_transport_route_name(
    void *context,
    uint64_t route_id)
{
    (void)context;
    return multiplayer_transport_route_name(route_id);
}

static const p4_doom_p4mp_transport_t s_doom_multiplayer_transport = {
    .set_handler = doom_transport_set_handler,
    .poll = doom_transport_poll,
    .send = doom_transport_send,
    .send_to = doom_transport_send_to,
    .connected = doom_transport_connected,
    .route_name = doom_transport_route_name,
};

static esp_err_t present(console_shell_t *shell);
static esp_err_t present_interactive(console_shell_t *shell);
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
static void arm_interactive_touch_timestamp_for_present(void);
static void clear_interactive_touch_timestamp_after_present(void);
#endif
static esp_err_t present_boot_screen(unsigned animation_step,
                                     const char *status);
#if CONFIG_P4_BOARD_M5STACK_TAB5
static esp_err_t stop_boot_animation(void);
#endif
static console_shell_runtime_info_t runtime_info(void);
static bool multiplayer_settings_editable(void);
static p4_doom_mp_setup_t multiplayer_display_setup(void);
static bool multiplayer_content_ready_for(size_t selection);
static bool multiplayer_local_content_ready(void);
static bool multiplayer_start_prerequisites_ready(void);
static size_t multiplayer_game_count(void);
static void rebuild_multiplayer_game_registry(void);
static bool multiplayer_selected_game_is_doom(void);
static bool multiplayer_selected_game_is_chex(void);
static const char *multiplayer_selected_game_title(void);
static const platform_game_catalog_entry_t *
    multiplayer_selected_native_game(void);
static bool native_multiplayer_content_identity(
    const platform_game_catalog_entry_t *game,
    uint8_t identity[P4_MP_SHA256_BYTES]);
static uint16_t multiplayer_game_token(void);
static esp_err_t configure_multiplayer_local_offer(void);
static void reset_multiplayer_lobby(const char *reason);
static bool storage_app_owned(void);
#if P4_CONSOLE_USB_INPUT
static esp_err_t start_usb_input(void);
#if P4_CONSOLE_H1_USB_DRIVE_CONTROL
static esp_err_t stop_usb_input_for_role_switch(void);
#endif
#endif

#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
void console_os_launch_doom(
    platform_game_storage_doom_title_t title,
    const p4_doom_mp_launch_config_t *multiplayer);
#else
void console_os_launch_doom(
    uint8_t master_volume_step,
    platform_game_storage_doom_title_t title,
    const p4_doom_mp_launch_config_t *multiplayer);
#endif

static P4_CONSOLE_LARGE_BSS
    console_app_descriptor_t s_apps[CONSOLE_SHELL_MAX_APPS];
static size_t s_app_count;

static const console_app_descriptor_t s_doom_app = {
    .icon_pixels = console_doom_icon_pixels,
    .icon_palette = console_doom_icon_palette,
#if CONFIG_P4_BOARD_M5STACK_TAB5
    .cover = &console_doom_cover,
#endif
    .id = CONSOLE_APP_DOOM,
    .title = "Doom",
    .subtitle = s_doom_subtitle,
    .folder_path = "GAMES/SHOOTERS",
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

static const console_app_descriptor_t s_chex_app = {
    .icon_pixels = console_chex_icon_pixels,
    .icon_palette = console_chex_icon_palette,
#if CONFIG_P4_BOARD_M5STACK_TAB5
    .cover = &console_chex_cover,
#endif
    .id = CONSOLE_APP_CHEX_QUEST,
    .title = "Chex Quest",
    .subtitle = s_chex_subtitle,
    .folder_path = "GAMES/OPTIONAL",
    .accent_rgb565 = UINT16_C(0xFFE0),
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
        .title = "Appearance",
        .subtitle = "Choose a desktop style",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x5FFF),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY,
        .page = CONSOLE_PAGE_COLORS,
        .enabled = true,
    },
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    {
        .id = CONSOLE_APP_TOUCH,
        .title = "Touch",
        .subtitle = "Check touch response",
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
        .title = "System Info",
        .subtitle = "Memory and device status",
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
        .title = "Sound",
        .subtitle = "Startup and game volume",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xF81F),
        .capabilities = CONSOLE_CAPABILITY_AUDIO,
        .page = CONSOLE_PAGE_AUDIO,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_ACHIEVEMENTS,
        .title = "Achievements",
        .subtitle = "Badges earned this session",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xFD20),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY,
        .page = CONSOLE_PAGE_ACHIEVEMENTS,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_FILES,
        .title = "Files",
#if CONFIG_P4_BOARD_M5STACK_TAB5 || CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        .subtitle = "Browse your SD card",
#else
        .subtitle = "Browse game storage",
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
        .title = "Game Manager",
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
        .title = "Multiplayer",
        .subtitle = "Host or join a game",
#if CONFIG_P4_BOARD_M5STACK_TAB5
        .folder_path = "SYSTEM",
#else
        .folder_path = "",
#endif
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
        .id = CONSOLE_APP_FILE_TRANSFER,
        .title = "File Transfer",
        .subtitle = "Send files over USB",
#if CONFIG_P4_BOARD_M5STACK_TAB5
        .folder_path = "SYSTEM",
#else
        .folder_path = "",
#endif
        .accent_rgb565 = UINT16_C(0xF81F),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
                        CONSOLE_CAPABILITY_TOUCH |
#endif
                        CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_FILE_TRANSFER,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_SAVES,
        .title = "Saved Games",
        .subtitle = "Manage game progress",
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
#if CONFIG_P4_BOARD_M5STACK_TAB5 || CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    {
        .id = CONSOLE_APP_STORAGE,
        .title = "SD Card",
        .subtitle = "Storage health and repair",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xFFE0),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_STORAGE |
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
                        CONSOLE_CAPABILITY_TOUCH,
#else
                        0U,
#endif
        .page = CONSOLE_PAGE_STORAGE,
        .enabled = true,
    },
#endif
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    {
        .id = CONSOLE_APP_POWER,
        .title = "Battery",
        .subtitle = "Charge level and power use",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x07E0),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH,
        .page = CONSOLE_PAGE_POWER,
        .enabled = true,
    },
#endif
#if CONFIG_P4_BOARD_M5STACK_TAB5
    {
        .id = CONSOLE_APP_SENSORS,
        .title = "Sensors",
        .subtitle = "Motion, temperature and clock",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x07FF),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH,
        .page = CONSOLE_PAGE_SENSORS,
        .enabled = true,
    },
#endif
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    {
        .id = CONSOLE_APP_USB_DRIVE,
        .title = "USB Drive",
        .subtitle = "Open SD card on your computer",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x07FF),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH |
                        CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_USB_DRIVE,
        .enabled = true,
    },
#endif
#if P4_CONSOLE_BLE_GAMEPAD
    {
        .id = CONSOLE_APP_CONTROLLERS,
        .title = "Controllers",
        .subtitle = "Connect and check gamepads",
#if CONFIG_P4_BOARD_M5STACK_TAB5
        .folder_path = "SYSTEM",
#else
        .folder_path = "",
#endif
        .accent_rgb565 = UINT16_C(0x5FFF),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                        CONSOLE_CAPABILITY_TOUCH,
        .page = CONSOLE_PAGE_CONTROLLERS,
        .enabled = true,
    },
#endif
    {
        .id = CONSOLE_APP_TERMINAL,
        .title = "Terminal",
        .subtitle = "Console commands",
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

_Static_assert(CONSOLE_NATIVE_AUDIO_FRAMES_MAX == 267,
               "60 Hz audio pacing must hold the 267-frame high phase");

#if P4_CONSOLE_GAMEPAD_INPUT
_Static_assert((int)CONSOLE_CONTROLLER_MAPPING_COUNT ==
                   (int)GAMEPAD_MAPPING_COUNT,
               "shell and gamepad mapping counts must match");
_Static_assert((int)CONSOLE_CONTROLLER_MAPPING_A ==
                   (int)GAMEPAD_MAPPING_A &&
                   (int)CONSOLE_CONTROLLER_MAPPING_B ==
                   (int)GAMEPAD_MAPPING_B &&
                   (int)CONSOLE_CONTROLLER_MAPPING_X ==
                   (int)GAMEPAD_MAPPING_X &&
                   (int)CONSOLE_CONTROLLER_MAPPING_Y ==
                   (int)GAMEPAD_MAPPING_Y &&
                   (int)CONSOLE_CONTROLLER_MAPPING_START ==
                   (int)GAMEPAD_MAPPING_START &&
                   (int)CONSOLE_CONTROLLER_MAPPING_BACK ==
                   (int)GAMEPAD_MAPPING_BACK,
               "shell and gamepad mapping slot order must match");
#endif

_Static_assert((int)CONSOLE_SHELL_LAYOUT_WIDTH ==
                   (int)PLATFORM_DISPLAY_GAME_WIDTH,
               "console input layout must match the game width");
_Static_assert((int)CONSOLE_SHELL_LAYOUT_HEIGHT ==
                   (int)PLATFORM_DISPLAY_GAME_HEIGHT,
               "console input layout must match the game height");
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
#if CONFIG_P4_BOARD_M5STACK_TAB5
_Static_assert(CONSOLE_SHELL_WIDTH == 1280 && CONSOLE_SHELL_HEIGHT == 720,
               "Tab5 UI must render at native viewport resolution");
#else
_Static_assert((int)CONSOLE_SHELL_WIDTH ==
                   (int)PLATFORM_DISPLAY_CONTENT_WIDTH &&
                   (int)CONSOLE_SHELL_HEIGHT ==
                   (int)PLATFORM_DISPLAY_CONTENT_HEIGHT,
               "Waveshare Console OS must render native 768x480 content");
#endif
_Static_assert((int)CONSOLE_SHELL_PRESENT_WIDTH ==
                   (int)PLATFORM_DISPLAY_SHELL_WIDTH &&
                   (int)CONSOLE_SHELL_PRESENT_HEIGHT ==
                   (int)PLATFORM_DISPLAY_SHELL_HEIGHT,
               "Waveshare shell source must match the exact PPA viewport");
_Static_assert((int)P4_GAME_SURFACE_HIGH_RES_WIDTH ==
                   (int)PLATFORM_DISPLAY_CONTENT_WIDTH &&
                   (int)P4_GAME_SURFACE_HIGH_RES_HEIGHT ==
                   (int)PLATFORM_DISPLAY_CONTENT_HEIGHT,
               "Game API high-res mode must match Waveshare content");
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
    if (!append_app(&s_doom_app) || !append_app(&s_chex_app)) {
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
            .icon_pixels = game->icon_valid ? game->icon.pixels : NULL,
            .icon_palette = game->icon_valid ? game->icon.palette : NULL,
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
    const size_t builtin_count =
        sizeof(s_builtin_apps) / sizeof(s_builtin_apps[0]);
    for (size_t i = 0U;
         i < builtin_count; ++i) {
        if (!append_app(&s_builtin_apps[i])) {
            return false;
        }
    }
    return true;
}

static void halt_dark(const char *stage, esp_err_t error)
{
    bool framebuffer_available = true;
#if CONFIG_P4_BOARD_M5STACK_TAB5
    framebuffer_available = stop_boot_animation() == ESP_OK;
#endif
    if (framebuffer_available && s_display_initialized && s_pixels != NULL) {
        for (size_t y = 0U; y < CONSOLE_SHELL_HEIGHT; ++y) {
            const uint16_t color = ((y / 16U) & 1U) == 0U
                ? UINT16_C(0x7800) : UINT16_C(0x4000);
            for (size_t x = 0U; x < CONSOLE_SHELL_WIDTH; ++x) {
                s_pixels[y * CONSOLE_SHELL_WIDTH + x] = color;
            }
        }
#if CONFIG_P4_BOARD_M5STACK_TAB5
        (void)platform_display_submit_ui_rgb565(
            s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
#else
        (void)platform_display_submit_content_rgb565(
            s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
#endif
        (void)platform_display_set_brightness(15U);
    }
    ESP_LOGE(TAG,
             "P4_CONSOLE_OS FATAL_HOLD stage=%s error=%s "
             "internal_free=%u internal_largest=%u stack_low_water=%u "
             "screen=red-recovery action=power-cycle-or-flash",
             stage, esp_err_to_name(error),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
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

static console_storage_repair_outcome_t shell_repair_outcome(
    platform_game_storage_repair_outcome_t outcome)
{
    switch (outcome) {
    case PLATFORM_GAME_STORAGE_REPAIR_CLEAN:
        return CONSOLE_STORAGE_REPAIR_CLEAN;
    case PLATFORM_GAME_STORAGE_REPAIR_REPAIRED:
        return CONSOLE_STORAGE_REPAIR_REPAIRED;
    case PLATFORM_GAME_STORAGE_REPAIR_NEEDS_HOST:
        return CONSOLE_STORAGE_REPAIR_NEEDS_HOST;
    case PLATFORM_GAME_STORAGE_REPAIR_UNSUPPORTED:
        return CONSOLE_STORAGE_REPAIR_UNSUPPORTED;
    case PLATFORM_GAME_STORAGE_REPAIR_FAILED:
        return CONSOLE_STORAGE_REPAIR_FAILED;
    case PLATFORM_GAME_STORAGE_REPAIR_NOT_RUN:
    default:
        return CONSOLE_STORAGE_REPAIR_NOT_RUN;
    }
}

static void set_doom_title_launcher_state(
    uint32_t app_id,
    char subtitle_buffer[CONSOLE_SHELL_SUBTITLE_MAX_BYTES],
    const char *subtitle,
    bool ready)
{
    if (subtitle == NULL) {
        subtitle = "STORAGE CHECKING";
    }
    const size_t length = strlen(subtitle);
    const size_t copy = length < CONSOLE_SHELL_SUBTITLE_MAX_BYTES - 1U
        ? length : CONSOLE_SHELL_SUBTITLE_MAX_BYTES - 1U;
    memcpy(subtitle_buffer, subtitle, copy);
    subtitle_buffer[copy] = '\0';
    for (size_t index = 0U; index < s_app_count; ++index) {
        if (s_apps[index].id == app_id) {
            s_apps[index].enabled = ready;
            return;
        }
    }
}

static void set_doom_storage_state(
    const platform_game_storage_status_t *status)
{
    if (status == NULL) {
        return;
    }
    const bool storage_ready =
        status->state == PLATFORM_GAME_STORAGE_APP_READY;
    const bool doom_ready = storage_ready && status->doom_wad_ready;
    const bool chex_ready = storage_ready && status->chex_quest_ready;
    const bool validation_running =
        status->content_validation_running;
    const char *doom_subtitle = doom_ready
        ? "SHAREWARE 1.9 / READY" : "STORAGE CHECKING";
    const char *chex_subtitle = chex_ready
        ? "FREEWARE 1.0 / READY" : "STORAGE CHECKING";
    switch (status->state) {
    case PLATFORM_GAME_STORAGE_APP_READY:
    case PLATFORM_GAME_STORAGE_APP_MISSING:
        if (validation_running) {
            doom_subtitle = doom_ready
                ? "SHAREWARE 1.9 / READY" : "VERIFYING SHA-256";
            chex_subtitle = chex_ready
                ? "FREEWARE 1.0 / READY" : "VERIFYING SHA-256";
            break;
        }
        if (!status->content_validation_complete) {
            doom_subtitle = doom_ready
                ? "SHAREWARE 1.9 / READY" : "PRESS TO VERIFY";
            chex_subtitle = chex_ready
                ? "FREEWARE 1.0 / READY" : "PRESS TO VERIFY";
            break;
        }
#if CONFIG_P4_BOARD_M5STACK_TAB5 || CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        if (!doom_ready) {
            doom_subtitle = "COPY DOOM1.WAD TO MICROSD";
        }
        if (!chex_ready) {
            chex_subtitle = "COPY CHEX.WAD + CHEX.DEH";
        }
#else
        if (!doom_ready) {
            doom_subtitle = "COPY DOOM1.WAD OVER USB";
        }
        if (!chex_ready) {
            chex_subtitle = "COPY CHEX.WAD + CHEX.DEH";
        }
#endif
        break;
    case PLATFORM_GAME_STORAGE_APP_INVALID:
        if (!doom_ready) {
            doom_subtitle = "DOOM1.WAD CHECK FAILED";
        }
        if (!chex_ready) {
            chex_subtitle = "CHEX DATA CHECK FAILED";
        }
        break;
    case PLATFORM_GAME_STORAGE_USB_HOST:
        doom_subtitle = "USB STORAGE ACTIVE";
        chex_subtitle = "USB STORAGE ACTIVE";
        break;
    case PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED:
        doom_subtitle = "HOST FORMAT REQUIRED";
        chex_subtitle = "HOST FORMAT REQUIRED";
        break;
    case PLATFORM_GAME_STORAGE_GAME_LOCKED:
        doom_subtitle = "GAME STORAGE LOCKED";
        chex_subtitle = "GAME STORAGE LOCKED";
        break;
    case PLATFORM_GAME_STORAGE_FAULT:
        doom_subtitle = "STORAGE OFFLINE";
        chex_subtitle = "STORAGE OFFLINE";
        break;
    case PLATFORM_GAME_STORAGE_UNINITIALIZED:
    case PLATFORM_GAME_STORAGE_APP_SCANNING:
    case PLATFORM_GAME_STORAGE_TRANSITION:
    default:
        break;
    }
    set_doom_title_launcher_state(
        CONSOLE_APP_DOOM, s_doom_subtitle, doom_subtitle,
        storage_ready && (doom_ready ||
            !status->content_validation_complete));
    set_doom_title_launcher_state(
        CONSOLE_APP_CHEX_QUEST, s_chex_subtitle,
        chex_subtitle, storage_ready && (chex_ready ||
            !status->content_validation_complete));
}

static void set_game_manager_update_state(platform_os_update_state_t state)
{
    const char *subtitle = "P4G + OS";
    switch (state) {
    case PLATFORM_OS_UPDATE_READY:
        subtitle = "OS UPDATE READY - OPEN";
        break;
    case PLATFORM_OS_UPDATE_INVALID:
        subtitle = "BAD UPDATE - REMOVE";
        break;
    case PLATFORM_OS_UPDATE_UNAVAILABLE:
#if CONFIG_P4_BOARD_M5STACK_TAB5 || CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
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
    }
    if (init_state == CONSOLE_STORAGE_INIT_READY ||
        init_state == CONSOLE_STORAGE_INIT_FAILED) {
        if (platform_game_storage_get_status(&status) != ESP_OK) {
            status.state = PLATFORM_GAME_STORAGE_FAULT;
            status.last_error = init_state == CONSOLE_STORAGE_INIT_FAILED
                ? init_result : ESP_FAIL;
        }
    }
    const bool changed = !s_game_storage_status_seen ||
        status.state != s_game_storage_status.state ||
        status.usb_attached != s_game_storage_status.usb_attached ||
        status.doom_wad_ready != s_game_storage_status.doom_wad_ready ||
        status.chex_quest_ready !=
            s_game_storage_status.chex_quest_ready ||
        status.content_validation_running !=
            s_game_storage_status.content_validation_running ||
        status.content_validation_complete !=
            s_game_storage_status.content_validation_complete ||
        status.content_validation_progress_percent !=
            s_game_storage_status.content_validation_progress_percent ||
        status.generation != s_game_storage_status.generation ||
        status.last_error != s_game_storage_status.last_error;
    const bool validation_just_completed =
        status.content_validation_complete &&
        (!s_game_storage_status_seen ||
         !s_game_storage_status.content_validation_complete);
    s_game_storage_status = status;
    s_game_storage_status_seen = true;
    set_doom_storage_state(&status);
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
                 "generation=%lu capacity=%llu doom=%s chex=%s "
                 "validation=%s progress=%u "
                 "verified_writes=%lu "
                 "write_failures=%lu last_error=%s",
                 platform_game_storage_state_name(status.state),
                 status.usb_attached ? 1U : 0U,
                 (unsigned long)status.generation,
                 (unsigned long long)status.capacity_bytes,
                 status.doom_wad_ready ? "ready" : "not-ready",
                 status.chex_quest_ready ? "ready" : "not-ready",
                 status.content_validation_running
                    ? "running"
                    : status.content_validation_complete
                        ? "complete" : "deferred",
                 (unsigned)status.content_validation_progress_percent,
                 (unsigned long)status.usb_verified_writes,
                 (unsigned long)status.usb_write_failures,
                 esp_err_to_name(status.last_error));
    }
    if (validation_just_completed) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS WAD_VALIDATION_COMPLETE generation=%lu "
                 "doom=%s chex=%s full_sha256=1",
                 (unsigned long)status.generation,
                 status.doom_wad_ready ? "ready" : "not-ready",
                 status.chex_quest_ready ? "ready" : "not-ready");
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

static esp_err_t publish_game_catalog_scan(
    esp_err_t storage_result, esp_err_t update_result,
    const char *scan_mode, unsigned worker_low_water_bytes)
{
    const bool catalog_available = storage_result == ESP_OK;
    if (catalog_available) {
        s_game_catalog = s_catalog_staging;
    } else {
        memset(&s_game_catalog, 0, sizeof(s_game_catalog));
    }
    if (!s_catalog_seen && catalog_available) {
        for (size_t i = 0U; i < s_game_catalog.entry_count; ++i) {
            const platform_game_catalog_entry_t *const game = &s_game_catalog.entries[i];
            if (game->valid) ESP_LOGI(TAG, "GAME_ENTRY file=%s id=%s title=%s folder=%s",
                game->file_name, game->package.id, game->package.title, game->package.folder);
        }
    }
    rebuild_multiplayer_game_registry();
    s_os_update_info = s_update_staging;
    const size_t game_count = multiplayer_game_count();
    bool multiplayer_offer_stale =
        s_multiplayer_game_selection >= game_count;
    if (multiplayer_offer_stale) {
        s_multiplayer_game_selection = 0U;
    } else if (!multiplayer_selected_game_is_doom()) {
        const platform_game_catalog_entry_t *const selected =
            multiplayer_selected_native_game();
        uint8_t identity[P4_MP_SHA256_BYTES];
        multiplayer_offer_stale = selected == NULL ||
            !native_multiplayer_content_identity(selected, identity) ||
            strcmp(selected->package.id,
                   s_multiplayer_local_offer.game_id) != 0 ||
            memcmp(identity,
                   s_multiplayer_local_offer.content_sha256,
                   P4_MP_SHA256_BYTES) != 0;
    }
    if (multiplayer_offer_stale &&
        s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_IDLE) {
        if (configure_multiplayer_local_offer() == ESP_OK) {
            reset_multiplayer_lobby("catalog-changed");
        }
    }
    set_game_manager_update_state(s_os_update_info.state);
    /* One scan attempt completes this storage generation. A card ownership or
     * mount change increments the generation and triggers the next attempt. */
    s_catalog_seen = true;
    s_catalog_storage_generation = s_game_storage_status.generation;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS GAME_CATALOG available=%u packages=%u "
             "valid=%u omitted=%lu generation=%lu source=microsd-only "
             "scan=%s worker_low_water_bytes=%u "
             "storage_result=%s update=%u result=%s",
             s_game_catalog.available ? 1U : 0U,
             (unsigned)s_game_catalog.entry_count,
             (unsigned)s_game_catalog.valid_count,
             (unsigned long)s_game_catalog.omitted_packages,
             (unsigned long)s_catalog_storage_generation,
             scan_mode == NULL ? "unknown" : scan_mode,
             worker_low_water_bytes,
             esp_err_to_name(storage_result),
             (unsigned)s_os_update_info.state,
             esp_err_to_name(catalog_available
                ? update_result : storage_result));
    return storage_result;
}

static esp_err_t reload_game_catalog(void)
{
    memset(&s_catalog_staging, 0, sizeof(s_catalog_staging));
    const esp_err_t storage_result =
        platform_game_catalog_scan(&s_catalog_staging);
    memset(&s_update_staging, 0, sizeof(s_update_staging));
    const esp_err_t update_result =
        platform_os_update_inspect(&s_update_staging);
    return publish_game_catalog_scan(
        storage_result, update_result, "foreground", 0U);
}

static void game_catalog_scan_worker(void *unused)
{
    (void)unused;
    console_storage_init_state_t init_state =
        CONSOLE_STORAGE_INIT_NOT_STARTED;
    esp_err_t init_result = ESP_ERR_INVALID_STATE;
    do {
        game_storage_init_snapshot(&init_state, &init_result);
        if (init_state == CONSOLE_STORAGE_INIT_RUNNING) {
            vTaskDelay(1U);
        }
    } while (init_state == CONSOLE_STORAGE_INIT_RUNNING);

    esp_err_t storage_result = init_result;
    if (init_state == CONSOLE_STORAGE_INIT_READY) {
        storage_result = platform_game_catalog_scan(&s_catalog_staging);
    }
    const unsigned low_water_bytes =
        (unsigned)uxTaskGetStackHighWaterMark(NULL);
    portENTER_CRITICAL(&s_game_catalog_scan_lock);
    s_game_catalog_scan_result = storage_result;
    s_game_catalog_scan_low_water_bytes = low_water_bytes;
    s_game_catalog_scan_state = GAME_CATALOG_SCAN_DONE;
    portEXIT_CRITICAL(&s_game_catalog_scan_lock);
    vTaskSuspend(NULL);
}

static esp_err_t start_game_catalog_scan(void)
{
    portENTER_CRITICAL(&s_game_catalog_scan_lock);
    if (s_game_catalog_scan_state != GAME_CATALOG_SCAN_IDLE) {
        portEXIT_CRITICAL(&s_game_catalog_scan_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_game_catalog_scan_state = GAME_CATALOG_SCAN_RUNNING;
    portEXIT_CRITICAL(&s_game_catalog_scan_lock);

    memset(&s_catalog_staging, 0, sizeof(s_catalog_staging));
    memset(&s_update_staging, 0, sizeof(s_update_staging));
    s_game_catalog_scan_started_us = esp_timer_get_time();
    const BaseType_t created = xTaskCreateWithCaps(
        game_catalog_scan_worker, "game_catalog",
        CONSOLE_GAME_CATALOG_STACK_BYTES, NULL, tskIDLE_PRIORITY + 1U,
        &s_game_catalog_scan_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        portENTER_CRITICAL(&s_game_catalog_scan_lock);
        s_game_catalog_scan_state = GAME_CATALOG_SCAN_IDLE;
        portEXIT_CRITICAL(&s_game_catalog_scan_lock);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS GAME_CATALOG_SCAN_BEGIN mode=background "
             "boot_audio=parallel stack_bytes=%u "
             "ota_inspect=foreground-internal",
             (unsigned)CONSOLE_GAME_CATALOG_STACK_BYTES);
    return ESP_OK;
}

static bool finish_game_catalog_scan(void)
{
    esp_err_t storage_result = ESP_ERR_INVALID_STATE;
    esp_err_t update_result = ESP_ERR_INVALID_STATE;
    unsigned low_water_bytes = 0U;
    TaskHandle_t completed_task = NULL;
    portENTER_CRITICAL(&s_game_catalog_scan_lock);
    if (s_game_catalog_scan_state != GAME_CATALOG_SCAN_DONE) {
        portEXIT_CRITICAL(&s_game_catalog_scan_lock);
        return false;
    }
    storage_result = s_game_catalog_scan_result;
    low_water_bytes = s_game_catalog_scan_low_water_bytes;
    completed_task = s_game_catalog_scan_task;
    s_game_catalog_scan_task = NULL;
    s_game_catalog_scan_state = GAME_CATALOG_SCAN_IDLE;
    portEXIT_CRITICAL(&s_game_catalog_scan_lock);

    if (completed_task != NULL) {
        vTaskDeleteWithCaps(completed_task);
    }
    /* esp_ota_get_running_partition() can disable the flash/PSRAM cache.
     * Keep OTA inspection on app_main's internal stack; the PSRAM-backed
     * worker is intentionally limited to removable-SD catalog work. */
    memset(&s_update_staging, 0, sizeof(s_update_staging));
    update_result = platform_os_update_inspect(&s_update_staging);
    (void)publish_game_catalog_scan(
        storage_result, update_result, "background", low_water_bytes);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS GAME_CATALOG_SCAN_COMPLETE elapsed_ms=%" PRIi64
             " launcher_gate=ready",
             (esp_timer_get_time() - s_game_catalog_scan_started_us) /
                 INT64_C(1000));
    return true;
}

static void wait_for_game_catalog_with_boot_animation(void)
{
    unsigned animation_step = 0U;
    while (!finish_game_catalog_scan()) {
        const esp_err_t frame_result = present_boot_screen(
            animation_step % CONSOLE_BOOT_ANIMATION_STEPS + 1U,
            "Loading games...");
        if (frame_result != ESP_OK) {
            halt_dark("catalog-loading-frame", frame_result);
        }
        ++animation_step;
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_STORAGE_LOADING_FRAME_MS));
    }
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
    const console_color_mode_t previous_color_mode =
        console_shell_color_mode(shell);
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
    shell->color_mode = previous_color_mode;
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
    if (action->type == CONSOLE_ACTION_GAME_REFRESH) {
        sync_game_storage();
        (void)reload_game_catalog();
        rebuild_shell_registry(shell);
        (void)reload_manager_listing(
            shell, CONSOLE_FILE_NOTICE_REFRESHED);
        return;
    }
    if (action->type == CONSOLE_ACTION_OS_UPDATE_INSTALL) {
        console_shell_set_file_notice(shell, CONSOLE_FILE_NOTICE_UPDATING);
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
        arm_interactive_touch_timestamp_for_present();
#endif
        const esp_err_t shown = present(shell);
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
        clear_interactive_touch_timestamp_after_present();
#endif
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
    if (result == ESP_OK) {
        game_storage_init_publish(CONSOLE_STORAGE_INIT_READY, ESP_OK);
    }
    s_file_listing_seen = false;
    if (result == ESP_OK && !enabled) {
        s_catalog_seen = false;
    }
    sync_game_storage();
    const console_shell_runtime_info_t current_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &current_runtime);
}

static void handle_storage_action(
    console_shell_t *shell,
    const console_shell_action_t *action)
{
    if (shell == NULL || action == NULL) {
        return;
    }
    esp_err_t result = ESP_ERR_INVALID_ARG;
    const char *operation = "invalid";
    switch (action->type) {
    case CONSOLE_ACTION_STORAGE_CHECK:
        operation = "check";
        s_storage_operation = CONSOLE_STORAGE_OPERATION_CHECK;
        break;
    case CONSOLE_ACTION_STORAGE_RETRY:
        operation = "retry";
        s_storage_operation = CONSOLE_STORAGE_OPERATION_RETRY;
        break;
    case CONSOLE_ACTION_STORAGE_REPAIR:
        operation = "fat-repair";
        s_storage_operation = CONSOLE_STORAGE_OPERATION_REPAIR;
        break;
    default:
        return;
    }

    const console_shell_runtime_info_t running_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &running_runtime);
    const esp_err_t progress_frame = present_interactive(shell);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS STORAGE_OPERATION_BEGIN name=%s "
             "progress_frame=%s",
             operation, esp_err_to_name(progress_frame));

    if (action->type == CONSOLE_ACTION_STORAGE_CHECK) {
        result = platform_game_storage_check_card();
    } else if (action->type == CONSOLE_ACTION_STORAGE_RETRY) {
        result = platform_game_storage_retry_card();
    } else {
        result = platform_game_storage_repair_fat();
    }
    s_storage_operation = CONSOLE_STORAGE_OPERATION_NONE;

    platform_game_storage_status_t final_status;
    memset(&final_status, 0, sizeof(final_status));
    if (platform_game_storage_get_status(&final_status) == ESP_OK &&
        final_status.state != PLATFORM_GAME_STORAGE_FAULT &&
        final_status.state != PLATFORM_GAME_STORAGE_UNINITIALIZED) {
        game_storage_init_publish(CONSOLE_STORAGE_INIT_READY, ESP_OK);
    }
    s_file_listing_seen = false;
    s_catalog_seen = false;
    sync_game_storage();
    const console_shell_runtime_info_t current_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &current_runtime);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS STORAGE_OPERATION_DONE name=%s result=%s "
             "state=%s repair_outcome=%u rewritten=%lu",
             operation, esp_err_to_name(result),
             platform_game_storage_state_name(s_game_storage_status.state),
             (unsigned)s_game_storage_status.last_repair_outcome,
             (unsigned long)s_game_storage_status.repair_sectors_rewritten);
}

#if P4_CONSOLE_GAMEPAD_INPUT
static esp_err_t apply_and_persist_controller_mapping(
    const gamepad_button_mapping_t *mapping)
{
    esp_err_t result = platform_gamepad_set_mapping(mapping);
    if (result != ESP_OK) {
        s_controller_mapping_last_error = result;
        s_controller_mapping_persistent = false;
        return result;
    }
    s_controller_mapping_current = *mapping;
    s_controller_mapping_pending = *mapping;
    result = platform_console_settings_set_controller_mapping(
        &s_console_settings, mapping);
    s_controller_mapping_persistent = result == ESP_OK;
    s_controller_mapping_last_error = result;
    return result;
}

static esp_err_t start_controller_mapping(void)
{
    platform_gamepad_snapshot_t raw;
    memset(&raw, 0, sizeof(raw));
    const esp_err_t result = platform_gamepad_get_raw_snapshot(&raw);
    if (result != ESP_OK ||
        raw.version != PLATFORM_GAMEPAD_SNAPSHOT_VERSION ||
        raw.size != sizeof(raw) ||
        raw.state.version != GAMEPAD_STATE_VERSION ||
        raw.state.size != sizeof(raw.state) ||
        raw.state.connected == 0U) {
        s_controller_mapping_last_error = result == ESP_OK
            ? ESP_ERR_INVALID_STATE : result;
        return s_controller_mapping_last_error;
    }
    s_controller_mapping_pending = s_controller_mapping_current;
    s_controller_mapping_active = true;
    s_controller_mapping_wait_neutral = true;
    s_controller_mapping_input_suppressed = true;
    s_controller_mapping_target = 0U;
    s_controller_mapping_last_error = ESP_OK;
    return ESP_OK;
}

static void cancel_controller_mapping(void)
{
    s_controller_mapping_active = false;
    s_controller_mapping_wait_neutral = false;
    s_controller_mapping_input_suppressed = true;
    s_controller_mapping_target = 0U;
    s_controller_mapping_pending = s_controller_mapping_current;
    s_controller_mapping_last_error = ESP_OK;
}

static esp_err_t reset_controller_mapping(void)
{
    gamepad_button_mapping_t defaults;
    gamepad_button_mapping_default(&defaults);
    cancel_controller_mapping();
    return apply_and_persist_controller_mapping(&defaults);
}
#endif

#if P4_CONSOLE_BLE_GAMEPAD
static bool ble_gamepad_status_busy(
    const platform_gamepad_ble_status_t *status)
{
    return status != NULL &&
        status->state >= PLATFORM_GAMEPAD_BLE_STARTING_HOST &&
        status->state <= PLATFORM_GAMEPAD_BLE_SUBSCRIBING;
}

static esp_err_t start_ble_gamepad_connection(void)
{
    if (!s_console_settings.ble_controller_enabled ||
        s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_BROWSING) {
        return ESP_ERR_INVALID_STATE;
    }
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        const esp_err_t pause_result =
            platform_multiplayer_ble_set_lobby_mode(
                PLATFORM_MULTIPLAYER_BLE_LOBBY_IDLE, 0U, 0U);
        if (pause_result != ESP_OK) {
            return pause_result;
        }
        s_ble_gamepad_suspended_lobby_browser = true;
    }
#endif
    return platform_gamepad_ble_connect_or_pair();
}

#endif /* P4_CONSOLE_BLE_GAMEPAD */

#if P4_CONSOLE_GAMEPAD_INPUT
static void handle_controller_action(
    console_shell_t *shell,
    const console_shell_action_t *action)
{
    if (shell == NULL || action == NULL) {
        return;
    }
    esp_err_t result = ESP_ERR_INVALID_ARG;
    const char *operation = "invalid";
#if P4_CONSOLE_BLE_GAMEPAD
    if (action->type == CONSOLE_ACTION_CONTROLLER_BLE_ENABLE) {
        operation = "ble-pad-enable";
        result = platform_console_settings_set_ble_controller_enabled(
            &s_console_settings, true);
        if (result == ESP_OK) {
            const platform_gamepad_ble_status_t status =
                platform_gamepad_ble_status();
            if (status.bonded &&
                s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_BROWSING) {
                const esp_err_t reconnect_result =
                    start_ble_gamepad_connection();
                ESP_LOGI(TAG,
                         "P4_CONSOLE_OS BLE_CONTROLLER_RECONNECT "
                         "trigger=mode-enable result=%s",
                         esp_err_to_name(reconnect_result));
            }
        }
    } else if (action->type == CONSOLE_ACTION_CONTROLLER_BLE_DISABLE) {
        operation = "ble-pad-disable";
        result = platform_console_settings_set_ble_controller_enabled(
            &s_console_settings, false);
        if (result == ESP_OK) {
            platform_gamepad_ble_disconnect();
#if P4_CONSOLE_GAMEPAD_INPUT
            if (s_controller_mapping_active) {
                cancel_controller_mapping();
            }
#endif
        }
    } else if (action->type == CONSOLE_ACTION_CONTROLLER_PAIR) {
        operation = "pair-or-connect";
        result = start_ble_gamepad_connection();
    } else if (action->type ==
               CONSOLE_ACTION_CONTROLLER_DISCONNECT) {
        operation = "disconnect";
        platform_gamepad_ble_disconnect();
        result = ESP_OK;
    } else if (action->type == CONSOLE_ACTION_CONTROLLER_FORGET) {
        operation = "forget";
        result = platform_gamepad_ble_forget();
    } else
#endif
    if (action->type ==
               CONSOLE_ACTION_CONTROLLER_MAPPING_START) {
        operation = "mapping-start";
        result = start_controller_mapping();
    } else if (action->type ==
               CONSOLE_ACTION_CONTROLLER_MAPPING_CANCEL) {
        operation = "mapping-cancel";
        cancel_controller_mapping();
        result = ESP_OK;
    } else if (action->type ==
               CONSOLE_ACTION_CONTROLLER_MAPPING_RESET) {
        operation = "mapping-reset";
        result = reset_controller_mapping();
    }
    ESP_LOGI(TAG,
#if P4_CONSOLE_BLE_GAMEPAD
             "P4_CONSOLE_OS BLE_CONTROLLER_ACTION name=%s result=%s",
#else
             "P4_CONSOLE_OS CONTROLLER_ACTION name=%s result=%s",
#endif
             operation, esp_err_to_name(result));
    const console_shell_runtime_info_t current_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &current_runtime);
}

#endif /* P4_CONSOLE_GAMEPAD_INPUT */

#if P4_CONSOLE_BLE_GAMEPAD
#if P4_CONSOLE_BLE_MULTIPLAYER
static void poll_ble_gamepad_lobby_restore(void)
{
    if (!s_ble_gamepad_suspended_lobby_browser) {
        return;
    }
    const platform_gamepad_ble_status_t status =
        platform_gamepad_ble_status();
    if (ble_gamepad_status_busy(&status)) {
        return;
    }
    s_ble_gamepad_suspended_lobby_browser = false;
    if (s_multiplayer_transport != CONSOLE_MP_TRANSPORT_BLE ||
        s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_BROWSING) {
        return;
    }
    const esp_err_t result = platform_multiplayer_ble_set_lobby_mode(
        PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER,
        0U, 0U);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BLE_CONTROLLER_RADIO_RELEASE "
             "lobby_browser_restore=%s",
             esp_err_to_name(result));
}
#endif
#endif

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

static bool multiplayer_settings_editable(void)
{
    const bool launch_syncing =
        s_multiplayer_start_barrier.state == P4_MP_START_WAITING ||
        s_multiplayer_start_barrier.state == P4_MP_START_ARMED ||
        s_multiplayer_group_start.phase == P4_MP_GROUP_WAITING ||
        s_multiplayer_group_start.phase == P4_MP_GROUP_COMMITTED;
    return !launch_syncing && !s_multiplayer_launch_due &&
        s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_BROWSING;
}

static bool native_game_supports_multiplayer(
    const platform_game_catalog_entry_t *game)
{
    return game != NULL && game->valid &&
        p4_mp_game_package_is_registerable(
            &game->package,
            CONSOLE_NATIVE_MULTIPLAYER_RUNTIME_PLAYERS);
}

static void rebuild_multiplayer_game_registry(void)
{
    p4_mp_game_registry_init(&s_multiplayer_game_registry);
    for (size_t index = 0U; index < s_game_catalog.entry_count; ++index) {
        const platform_game_catalog_entry_t *const game =
            &s_game_catalog.entries[index];
        if (!game->valid) {
            continue;
        }
        if (protected_game_lineage_check(&game->package) ==
            P4_PROTECTED_GAME_REJECTED) {
            ESP_LOGE(TAG,
                     "P4_CONSOLE_OS PROTECTED_GAME_REJECTED id=%s "
                     "operation=multiplayer-register reason=payload-lineage",
                     game->package.id);
            continue;
        }
        const p4_mp_registration_result_t result =
            p4_mp_game_registry_register_package(
                &s_multiplayer_game_registry,
                &game->package,
                CONSOLE_NATIVE_MULTIPLAYER_RUNTIME_PLAYERS);
        if (result == P4_MP_REGISTRATION_ACCEPTED) {
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS MULTIPLAYER_GAME_REGISTERED "
                     "id=%s title=%s launcher=%lu style=%u protocol=%u "
                     "source=validated-p4g-manifest",
                     game->package.id,
                     game->package.title,
                     (unsigned long)game->package.launcher_id,
                     (unsigned)game->package.multiplayer_profile.style,
                     (unsigned)game->package.multiplayer_profile.protocol);
        } else if (result != P4_MP_REGISTRATION_NOT_MULTIPLAYER) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS MULTIPLAYER_GAME_REJECTED "
                     "id=%s launcher=%lu reason=%s",
                     game->package.id,
                     (unsigned long)game->package.launcher_id,
                     p4_mp_registration_result_name(result));
        }
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MULTIPLAYER_REGISTRY_READY games=%u "
             "runtime_players=%u owner=console-os",
             (unsigned)s_multiplayer_game_registry.game_count,
             (unsigned)CONSOLE_NATIVE_MULTIPLAYER_RUNTIME_PLAYERS);
}

static p4_mp_game_mode_t native_multiplayer_mode(
    const p4_game_multiplayer_profile_t *profile)
{
    return profile != NULL &&
        profile->style == P4_GAME_MULTIPLAYER_STYLE_LOCKSTEP
        ? P4_MP_GAME_MODE_LOCKSTEP
        : P4_MP_GAME_MODE_HOST_AUTHORITATIVE;
}

static bool native_multiplayer_content_identity(
    const platform_game_catalog_entry_t *game,
    uint8_t identity[P4_MP_SHA256_BYTES])
{
    if (!native_game_supports_multiplayer(game) || identity == NULL) {
        return false;
    }
    const p4_game_multiplayer_profile_t *const profile =
        &game->package.multiplayer_profile;
    uint8_t material[P4_MP_SHA256_BYTES + 16U] = {0};
    memcpy(material, game->package.payload_sha256, P4_MP_SHA256_BYTES);
    memcpy(material + P4_MP_SHA256_BYTES, "P4MP", 4U);
    material[36] = profile->schema;
    material[37] = (uint8_t)profile->style;
    material[38] = profile->min_players;
    material[39] = profile->max_players;
    material[40] = (uint8_t)profile->tick_rate_hz;
    material[41] = (uint8_t)(profile->tick_rate_hz >> 8U);
    material[42] = profile->input_delay_ticks;
    material[43] = profile->message_bytes;
    material[44] = (uint8_t)profile->protocol;
    material[45] = (uint8_t)(profile->protocol >> 8U);
    material[46] = (uint8_t)profile->flags;
    material[47] = (uint8_t)(profile->flags >> 8U);
    return mbedtls_sha256(material, sizeof(material), identity, 0) == 0;
}

static size_t multiplayer_game_count(void)
{
    return P4_DOOM_MP_GAME_COUNT +
        s_multiplayer_game_registry.game_count;
}

static const platform_game_catalog_entry_t *multiplayer_native_game_at(
    size_t native_index)
{
    const p4_mp_registered_game_t *const registration =
        p4_mp_game_registry_at(
            &s_multiplayer_game_registry, native_index);
    return registration == NULL ? NULL :
        platform_game_catalog_find_launcher(
            &s_game_catalog, registration->launcher_id);
}

static const platform_game_catalog_entry_t *multiplayer_native_game_for(
    size_t selection)
{
    return selection < P4_DOOM_MP_GAME_COUNT ? NULL :
        multiplayer_native_game_at(selection - P4_DOOM_MP_GAME_COUNT);
}

static const platform_game_catalog_entry_t *
multiplayer_selected_native_game(void)
{
    return multiplayer_native_game_for(s_multiplayer_game_selection);
}

static bool multiplayer_selected_game_is_doom(void)
{
    /* Both built-ins share Doom's lockstep engine and setup panel. */
    return s_multiplayer_game_selection < P4_DOOM_MP_GAME_COUNT;
}

static bool multiplayer_selected_game_is_chex(void)
{
    return s_multiplayer_game_selection ==
        (size_t)P4_DOOM_MP_GAME_CHEX_QUEST;
}

static bool multiplayer_selected_game_is_arena(void)
{ return s_multiplayer_game_selection == P4_DOOM_MP_GAME_GAME_CHANGERS_AI; }

static const char *multiplayer_game_title_at(size_t selection)
{
    if (selection == P4_DOOM_MP_GAME_GAME_CHANGERS_AI) return "GAME CHANGERS AI";
    if (selection == (size_t)P4_DOOM_MP_GAME_DOOM) {
        return "DOOM";
    }
    if (selection == (size_t)P4_DOOM_MP_GAME_CHEX_QUEST) {
        return "CHEX QUEST";
    }
    const platform_game_catalog_entry_t *const game =
        multiplayer_native_game_for(selection);
    return game == NULL ? "NOT INSTALLED" : game->package.title;
}

static const char *multiplayer_selected_game_title(void)
{
    return multiplayer_game_title_at(s_multiplayer_game_selection);
}

static bool multiplayer_dice_available(void)
{
#if P4_CONSOLE_BLE_MULTIPLAYER
    const platform_game_catalog_entry_t *const game = multiplayer_selected_native_game();
    return s_dice_ready && game != NULL && p4_mp_game_supports_dice(&game->package);
#else
    return false;
#endif
}

static p4_doom_mp_setup_t multiplayer_display_setup(void)
{
    if (s_doom_multiplayer_launch.enabled &&
        p4_doom_mp_launch_config_valid(&s_doom_multiplayer_launch)) {
        return s_doom_multiplayer_launch.setup;
    }
    p4_doom_mp_setup_t remote;
    if (s_multiplayer_session.role == P4_MP_ROLE_CLIENT &&
        p4_doom_mp_setup_decode(
            s_multiplayer_remote_offer.game_settings,
            sizeof(s_multiplayer_remote_offer.game_settings),
            &remote)) {
        return remote;
    }
    return s_multiplayer_local_setup;
}

static uint16_t multiplayer_game_token(void)
{
    return p4_mp_ble_game_token(
        s_multiplayer_local_offer.compatibility_sha256);
}

static bool multiplayer_game_selection_for_token(
    uint16_t token, size_t *selection_out)
{
    if (token == 0U || selection_out == NULL) {
        return false;
    }
    const size_t saved_selection = s_multiplayer_game_selection;
    const p4_mp_lobby_offer_t saved_offer = s_multiplayer_local_offer;
    const p4_doom_mp_setup_t saved_setup = s_multiplayer_local_setup;
    bool found = false;
    const size_t game_count = multiplayer_game_count();
    for (size_t selection = 0U; selection < game_count; ++selection) {
        s_multiplayer_game_selection = selection;
        s_multiplayer_local_setup = saved_setup;
        if (configure_multiplayer_local_offer() == ESP_OK &&
            multiplayer_game_token() == token) {
            *selection_out = selection;
            found = true;
            break;
        }
    }
    s_multiplayer_game_selection = saved_selection;
    s_multiplayer_local_offer = saved_offer;
    s_multiplayer_local_setup = saved_setup;
    return found;
}

static void refresh_multiplayer_lobby_candidates(void)
{
    s_multiplayer_lobby_candidate_count = 0U;
    memset(s_multiplayer_lobby_candidates, 0,
           sizeof(s_multiplayer_lobby_candidates));
    if (s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_BROWSING) {
        s_multiplayer_lobby_selection = 0U;
        return;
    }
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI) {
        platform_multiplayer_wifi_lobby_t found[PLATFORM_MULTIPLAYER_WIFI_MAX_LOBBIES];
        const size_t count = platform_multiplayer_wifi_list_lobbies(found, PLATFORM_MULTIPLAYER_WIFI_MAX_LOBBIES);
        for (size_t i=0; i<count && i<CONSOLE_MP_MAX_LOBBY_CANDIDATES; ++i) {
            size_t game_selection=0;
            const bool available=multiplayer_game_selection_for_token(found[i].game_token,&game_selection);
            s_multiplayer_lobby_candidates[i]=(console_mp_lobby_candidate_t){
                .lobby_id=found[i].lobby_id,.session_id=found[i].session_id,.game_token=found[i].game_token,
                .rssi=found[i].rssi,.players_present=found[i].players_present,.player_capacity=found[i].player_capacity,
                .game_selection=game_selection,.game_available=available};
            ++s_multiplayer_lobby_candidate_count;
        }
    } else
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        platform_multiplayer_ble_lobby_t discovered[
            PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES];
        const size_t count = platform_multiplayer_ble_list_lobbies(
            discovered,
            sizeof(discovered) / sizeof(discovered[0]));
        for (size_t index = 0U;
             index < count &&
             index < CONSOLE_MP_MAX_LOBBY_CANDIDATES; ++index) {
            size_t game_selection = 0U;
            const bool game_available =
                multiplayer_game_selection_for_token(
                    discovered[index].game_token, &game_selection);
            s_multiplayer_lobby_candidates[index] =
                (console_mp_lobby_candidate_t){
                    .lobby_id = discovered[index].lobby_id,
                    .session_id = discovered[index].session_id,
                    .game_token = discovered[index].game_token,
                    .rssi = discovered[index].rssi,
                    .players_present = discovered[index].players_present,
                    .player_capacity = discovered[index].player_capacity,
                    .game_selection = game_selection,
                    .game_available = game_available,
                };
            ++s_multiplayer_lobby_candidate_count;
        }
    } else
#endif
    {
        const int64_t now_us = esp_timer_get_time();
        if (s_multiplayer_remote_session_id != 0U &&
            s_multiplayer_remote_offer.session_seed != 0U &&
            s_multiplayer_remote_offer_seen_us > 0 &&
            now_us >= s_multiplayer_remote_offer_seen_us &&
            now_us - s_multiplayer_remote_offer_seen_us <=
                (int64_t)CONSOLE_MULTIPLAYER_PEER_TIMEOUT_MS * 1000) {
            const uint16_t game_token = p4_mp_ble_game_token(
                s_multiplayer_remote_offer.compatibility_sha256);
            size_t game_selection = 0U;
            const bool game_available =
                multiplayer_game_selection_for_token(
                    game_token, &game_selection);
            s_multiplayer_lobby_candidates[0] =
                (console_mp_lobby_candidate_t){
                    .lobby_id = s_multiplayer_remote_route_id,
                    .route_id = s_multiplayer_remote_route_id,
                    .session_id = s_multiplayer_remote_session_id,
                    .host_peer_id = s_multiplayer_remote_peer_id,
                    .game_token = game_token,
                    .rssi = 0,
                    .players_present =
                        s_multiplayer_remote_offer.players_present,
                    .player_capacity =
                        s_multiplayer_remote_offer.player_capacity,
                    .game_selection = game_selection,
                    .game_available = game_available,
                };
            s_multiplayer_lobby_candidate_count = 1U;
        }
    }
    if (s_multiplayer_lobby_selected_id != 0U &&
        s_multiplayer_lobby_selected_session_id != 0U) {
        for (size_t index = 0U;
             index < s_multiplayer_lobby_candidate_count; ++index) {
            if (s_multiplayer_lobby_candidates[index].lobby_id ==
                    s_multiplayer_lobby_selected_id &&
                s_multiplayer_lobby_candidates[index].session_id ==
                    s_multiplayer_lobby_selected_session_id) {
                s_multiplayer_lobby_selection = index + 1U;
                return;
            }
        }
    }
    s_multiplayer_lobby_selected_id = 0U;
    s_multiplayer_lobby_selected_session_id = 0U;
    s_multiplayer_lobby_selection = 0U;
}

static const console_mp_lobby_candidate_t *selected_multiplayer_lobby(void)
{
    if (s_multiplayer_lobby_selected_id == 0U ||
        s_multiplayer_lobby_selected_session_id == 0U) {
        return NULL;
    }
    for (size_t index = 0U;
         index < s_multiplayer_lobby_candidate_count; ++index) {
        if (s_multiplayer_lobby_candidates[index].lobby_id ==
                s_multiplayer_lobby_selected_id &&
            s_multiplayer_lobby_candidates[index].session_id ==
                s_multiplayer_lobby_selected_session_id) {
            return &s_multiplayer_lobby_candidates[index];
        }
    }
    return NULL;
}

#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
static void sample_battery(bool initial_sample)
{
    if (!s_battery_initialized) {
        return;
    }
    platform_battery_sample_t sample;
    memset(&sample, 0, sizeof(sample));
    const bool was_valid = s_battery_sample_valid;
    const uint8_t previous_percent = s_battery_sample.percent;
    const esp_err_t result = platform_battery_read(&sample);
    s_battery_last_error = result;
    if (result != ESP_OK) {
        s_battery_sample_valid = false;
        if (initial_sample || was_valid) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS BATTERY_SAMPLE valid=0 error=%s",
                     esp_err_to_name(result));
        }
        return;
    }
    s_battery_raw_sample = sample;
    s_battery_sample_valid = true;
    const uint32_t display_mv = s_battery_sample.battery_mv;
    const uint32_t delta_mv = sample.battery_mv > display_mv
        ? sample.battery_mv - display_mv : display_mv - sample.battery_mv;
    if (initial_sample || !was_valid ||
        delta_mv >= CONSOLE_BATTERY_DISPLAY_MV_HYSTERESIS) {
        /* Keep the shell-facing sample stable while retaining the raw ADC
         * result above for diagnostics and future detail views. */
        s_battery_sample = sample;
    }
    if (initial_sample || !was_valid ||
        s_battery_sample.percent != previous_percent) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BATTERY_SAMPLE valid=1 adc_mv=%" PRIu32
                 " battery_mv=%" PRIu32 " percent=%u estimate=linear",
                 s_battery_raw_sample.adc_mv, s_battery_raw_sample.battery_mv,
                 (unsigned)s_battery_sample.percent);
    }
}

static void initialize_battery(void)
{
    s_battery_last_error = platform_battery_init(NULL);
    s_battery_initialized = s_battery_last_error == ESP_OK;
    s_battery_sample_valid = false;
    if (!s_battery_initialized) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BATTERY_DEGRADED gpio=%u error=%s",
                 PLATFORM_BATTERY_ADC_GPIO,
                 esp_err_to_name(s_battery_last_error));
        return;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BATTERY_READY gpio=%u adc=1 channel=%u "
             "divider=%u:%u calibrated=1",
             PLATFORM_BATTERY_ADC_GPIO, PLATFORM_BATTERY_ADC1_CHANNEL,
             PLATFORM_BATTERY_DIVIDER_TOP_OHMS,
             PLATFORM_BATTERY_DIVIDER_BOTTOM_OHMS);
    sample_battery(true);
}
#endif

static console_shell_runtime_info_t runtime_info(void)
{
#if CONFIG_P4_BOARD_M5STACK_TAB5
    const platform_tab5_telemetry_t sensors = platform_tab5_sensors_snapshot();
#endif
    const console_mp_transport_status_t multiplayer =
        multiplayer_transport_status();
    const int64_t now_us = esp_timer_get_time();
    const bool multiplayer_peer_seen =
        s_multiplayer_peer_last_seen_us > 0 &&
        now_us - s_multiplayer_peer_last_seen_us <=
            (int64_t)CONSOLE_MULTIPLAYER_PEER_TIMEOUT_MS * 1000;
    const uint64_t capacity_kib = (s_game_storage_status.space_valid
        ? s_game_storage_status.filesystem_bytes
        : s_game_storage_status.capacity_bytes) / 1024U;
    const uint64_t storage_free_kib =
        s_game_storage_status.free_bytes / 1024U;
    const p4_doom_mp_setup_t multiplayer_setup =
        multiplayer_display_setup();
    const p4_file_transfer_info_t file_transfer =
        p4_file_transfer_info();
    const console_mp_lobby_candidate_t *const selected_lobby =
        selected_multiplayer_lobby();
    const bool multiplayer_is_host =
        s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_HOSTING ||
        (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_CONNECTED &&
         s_multiplayer_session.role == P4_MP_ROLE_HOST);
    console_multiplayer_lobby_phase_t lobby_phase =
        CONSOLE_MULTIPLAYER_LOBBY_BROWSING;
    if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_HOSTING) {
        lobby_phase = CONSOLE_MULTIPLAYER_LOBBY_HOSTING;
    } else if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_JOINING) {
        lobby_phase = CONSOLE_MULTIPLAYER_LOBBY_JOINING;
    } else if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_CONNECTED) {
        lobby_phase = CONSOLE_MULTIPLAYER_LOBBY_CONNECTED;
    }
    uint32_t lobby_session_id = 0U;
    int8_t lobby_rssi = 0;
    if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_HOSTING ||
        s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_CONNECTED) {
        lobby_session_id = s_multiplayer_session.session_id;
    } else if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_JOINING) {
        lobby_session_id = s_multiplayer_target_session_id;
    } else if (selected_lobby != NULL) {
        lobby_session_id = selected_lobby->session_id;
        lobby_rssi = selected_lobby->rssi;
    }
    const bool lobby_choice_valid = s_multiplayer_lobby_selection == 0U ||
        selected_lobby != NULL;
    const bool selected_game_ready = selected_lobby == NULL
        ? multiplayer_local_content_ready()
        : selected_lobby->game_available &&
            multiplayer_content_ready_for(selected_lobby->game_selection);
    const bool lobby_browser_settling =
        s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE &&
        s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_BROWSING &&
        selected_lobby == NULL &&
        !s_multiplayer_lobby_create_explicit &&
        (s_multiplayer_lobby_browser_ready_us == 0 ||
         now_us - s_multiplayer_lobby_browser_ready_us <
            (int64_t)CONSOLE_MULTIPLAYER_BLE_BROWSER_SETTLE_MS * 1000);
    const bool lobby_action_enabled =
        s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_BROWSING &&
        selected_game_ready && lobby_choice_valid &&
        (multiplayer.available || multiplayer.starting) &&
        !lobby_browser_settling;
    const bool multiplayer_can_start = multiplayer_is_host &&
        multiplayer_start_prerequisites_ready() &&
        s_multiplayer_group_start.phase == P4_MP_GROUP_IDLE;
    const size_t selectable_game_count = multiplayer_game_count();
#if P4_CONSOLE_BLE_GAMEPAD
    const platform_gamepad_ble_status_t ble_controller =
        platform_gamepad_ble_status();
    const bool ble_controller_busy =
        ble_controller.state >= PLATFORM_GAMEPAD_BLE_STARTING_HOST &&
        ble_controller.state <= PLATFORM_GAMEPAD_BLE_SUBSCRIBING;
#endif
    console_shell_runtime_info_t info = {
        .uptime_seconds = uptime_seconds(),
        .internal_free_kib = free_kib(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .psram_free_kib = free_kib(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .game_storage_kib = capacity_kib > UINT32_MAX
            ? UINT32_MAX : (uint32_t)capacity_kib,
        .game_storage_space_valid = s_game_storage_status.space_valid,
        .game_storage_free_kib = storage_free_kib > UINT32_MAX
            ? UINT32_MAX : (uint32_t)storage_free_kib,
        .game_storage_sector_bytes =
            s_game_storage_status.sector_size_bytes,
        .game_storage_frequency_khz =
            s_game_storage_status.real_frequency_khz,
        .game_storage_root_entries =
            s_game_storage_status.root_entries,
        .game_storage_mount_failures =
            s_game_storage_status.mount_failures,
        .game_storage_scans = s_game_storage_status.scans,
        .game_storage_checks = s_game_storage_status.checks,
        .game_storage_recovery_attempts =
            s_game_storage_status.recovery_attempts,
        .game_storage_repair_attempts =
            s_game_storage_status.repair_attempts,
        .game_storage_repair_sectors =
            s_game_storage_status.repair_sectors_rewritten,
        .game_storage_card_ready = s_game_storage_status.card_ready,
        .game_storage_filesystem_ready =
            s_game_storage_status.filesystem_ready,
        .game_storage_repair_supported =
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
    CONFIG_P4_WAVESHARE_H2_RUNTIME_ROLE_SWITCH
            true,
#else
            false,
#endif
        .game_storage_last_check_ok =
            s_game_storage_status.checks != 0U &&
            s_game_storage_status.last_check_error == ESP_OK,
        .game_storage_repair_outcome = shell_repair_outcome(
            s_game_storage_status.last_repair_outcome),
        .game_storage_operation = s_storage_operation,
        .game_storage_state = shell_storage_state(
            s_game_storage_status.state),
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
        .board_kind = CONSOLE_BOARD_OLIMEX_P4_PC,
        .touch_ready = false,
#else
        .board_kind =
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
            CONSOLE_BOARD_WAVESHARE_4_3,
#elif CONFIG_P4_BOARD_M5STACK_TAB5
            CONSOLE_BOARD_M5STACK_TAB5,
#else
            CONSOLE_BOARD_ELECROW_10,
#endif
        .touch_ready = s_touch_ready,
#endif
#if P4_CONSOLE_GAMEPAD_INPUT
        .controller_ready = s_gamepad_connected,
        .controller_transport =
            (console_controller_transport_t)s_gamepad_transport,
#else
        .controller_ready = false,
        .controller_transport = CONSOLE_CONTROLLER_TRANSPORT_NONE,
#endif
#if P4_CONSOLE_BLE_GAMEPAD
        .ble_controller_supported = ble_controller.supported,
        .ble_controller_enabled =
            s_console_settings.ble_controller_enabled,
        .ble_controller_host_ready = ble_controller.host_ready,
        .ble_controller_bonded = ble_controller.bonded,
        .ble_controller_connected = ble_controller.connected,
        .ble_controller_encrypted = ble_controller.encrypted,
        .ble_controller_busy = ble_controller_busy,
        .ble_controller_rssi = ble_controller.rssi,
        .ble_controller_reports_received =
            ble_controller.reports_received,
        .ble_controller_reports_dropped = ble_controller.reports_dropped,
        .ble_controller_last_error = ble_controller.last_error,
        .ble_controller_multiplayer_ready =
            s_console_settings.ble_controller_enabled &&
            ble_controller.host_ready && ble_controller.connected &&
            ble_controller.encrypted,
#else
        .ble_controller_supported = false,
#endif
#if P4_CONSOLE_GAMEPAD_INPUT
        .controller_mapping_active = s_controller_mapping_active,
        .controller_mapping_persistent =
            s_controller_mapping_persistent,
        .controller_mapping_target = s_controller_mapping_target,
        .controller_mapping_last_error =
            s_controller_mapping_last_error,
#endif
#if P4_CONSOLE_USB_INPUT
        .keyboard_ready = s_keyboard_connected,
        .mouse_ready = s_mouse_connected,
#else
        .keyboard_ready = false,
        .mouse_ready = false,
#endif
        .sd_card_storage =
#if CONFIG_P4_BOARD_M5STACK_TAB5 || CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
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
        /* The TinyUSB driver also runs while the app owns the SD card so it
         * can detect H2 attachment. Only USB ownership means Drive mode. */
        .usb_drive_active =
            s_game_storage_status.state == PLATFORM_GAME_STORAGE_USB_HOST ||
            s_game_storage_status.state ==
                PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED,
#if P4_CONSOLE_USB_INPUT
        .usb_input_host_active = s_gamepad_ready,
#else
        .usb_input_host_active = false,
#endif
        .doom_wad_ready =
            s_game_storage_status.state == PLATFORM_GAME_STORAGE_APP_READY &&
            s_game_storage_status.doom_wad_ready,
        .content_scan_complete = s_catalog_seen,
        .usb_content_ready = p4_content_transfer_info().ready,
        .file_transfer_ready = file_transfer.ready,
        .file_transfer_busy = file_transfer.busy,
        .file_transfer_state =
            (console_file_transfer_state_t)file_transfer.state,
        .file_transfer_direction =
            (console_file_transfer_direction_t)file_transfer.direction,
        .file_transfer_class =
            (console_file_transfer_class_t)file_transfer.file_class,
        .file_transfer_progress_percent =
            file_transfer.progress_percent,
        .file_transfer_last_status = file_transfer.last_status,
        .file_transfer_bytes = file_transfer.transferred_bytes,
        .file_transfer_total_bytes = file_transfer.total_bytes,
        .file_transfer_generation = file_transfer.generation,
        .content_validation_running =
            s_game_storage_status.content_validation_running,
        .content_validation_complete =
            s_game_storage_status.content_validation_complete,
        .content_validation_progress_percent =
            s_game_storage_status.content_validation_progress_percent,
        .multiplayer_core_ready = true,
        .multiplayer_transport_ready = multiplayer.available,
        .multiplayer_transport_starting = multiplayer.starting,
        .multiplayer_transport_encrypted = multiplayer.encrypted,
        .multiplayer_transport_kind =
            (uint8_t)s_multiplayer_transport,
        .multiplayer_peer_seen = multiplayer_peer_seen,
        .multiplayer_lobby_ready =
            s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_CONNECTED &&
            s_multiplayer_launch_kind != CONSOLE_MP_LAUNCH_NONE,
        .multiplayer_lobby_is_host = multiplayer_is_host,
        .multiplayer_lobby_scanning = lobby_browser_settling ||
            (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI && s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_BROWSING),
        .multiplayer_lobby_action_enabled = lobby_action_enabled,
        .multiplayer_can_start = multiplayer_can_start,
        .multiplayer_launch_syncing =
            s_multiplayer_start_barrier.state == P4_MP_START_WAITING ||
            s_multiplayer_start_barrier.state == P4_MP_START_ARMED ||
            s_multiplayer_group_start.phase == P4_MP_GROUP_WAITING ||
            s_multiplayer_group_start.phase == P4_MP_GROUP_COMMITTED,
        .multiplayer_settings_editable = multiplayer_settings_editable(),
        .multiplayer_game_ready = selected_game_ready,
        .multiplayer_game_is_arena = multiplayer_selected_game_is_arena(),
        .multiplayer_game_is_doom =
            multiplayer_selected_game_is_doom(),
        .multiplayer_dice_available = multiplayer_dice_available(),
        .multiplayer_dice_enabled = s_multiplayer_launch_kind == CONSOLE_MP_LAUNCH_NATIVE
            ? s_multiplayer_launch_shared_dice : s_multiplayer_shared_dice,
        .multiplayer_game_selection =
            s_multiplayer_game_selection > UINT8_MAX
                ? UINT8_MAX : (uint8_t)s_multiplayer_game_selection,
        .multiplayer_game_count =
            selectable_game_count > UINT8_MAX
                ? UINT8_MAX : (uint8_t)selectable_game_count,
        .multiplayer_lobby_phase = lobby_phase,
        .multiplayer_lobby_selection =
            s_multiplayer_lobby_selection > UINT8_MAX
                ? UINT8_MAX : (uint8_t)s_multiplayer_lobby_selection,
        .multiplayer_lobby_count =
            s_multiplayer_lobby_candidate_count > UINT8_MAX
                ? UINT8_MAX : (uint8_t)s_multiplayer_lobby_candidate_count,
        .multiplayer_lobby_rssi = lobby_rssi,
        .multiplayer_lobby_session_id = lobby_session_id,
        .multiplayer_route_id =
            s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIRED
                ? (uint8_t)multiplayer.active_route_id : 0U,
        .multiplayer_player_slot =
            s_multiplayer_launch_kind != CONSOLE_MP_LAUNCH_NONE
                ? s_multiplayer_local_player_slot
                : P4_MP_PLAYER_SLOT_ANY,
        .multiplayer_game_mode = (uint8_t)multiplayer_setup.mode,
        .multiplayer_episode = multiplayer_setup.episode,
        .multiplayer_map = multiplayer_setup.map,
        .multiplayer_skill = multiplayer_setup.skill,
        .multiplayer_time_limit_minutes =
            multiplayer_setup.time_limit_minutes,
        .multiplayer_no_monsters = multiplayer_setup.no_monsters,
        .multiplayer_fast_monsters = multiplayer_setup.fast_monsters,
        .multiplayer_respawn_monsters =
            multiplayer_setup.respawn_monsters,
        .multiplayer_rx_frames = multiplayer.rx_frames,
        .multiplayer_tx_frames = multiplayer.tx_frames,
#if P4_CONSOLE_USB_INPUT
        .physical_keyboard_ready = s_keyboard_connected,
#else
        .physical_keyboard_ready = false,
#endif
        .valid_cart_count = s_game_catalog.valid_count > UINT16_MAX
            ? UINT16_MAX : (uint16_t)s_game_catalog.valid_count,
        .builtin_game_count = (uint16_t)(
            2U + sizeof(s_builtin_apps) / sizeof(s_builtin_apps[0])),
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        .battery_supported = s_battery_initialized,
        .battery_sample_valid = s_battery_sample_valid,
        .battery_calibrated = s_battery_initialized,
        .battery_millivolts = s_battery_raw_sample.battery_mv > UINT16_MAX
            ? UINT16_MAX : (uint16_t)s_battery_raw_sample.battery_mv,
        .battery_percent = s_battery_sample.percent,
        .battery_last_error = s_battery_last_error,
#elif CONFIG_P4_BOARD_M5STACK_TAB5
        .control_panel_enabled = true,
        .battery_supported = sensors.battery_ready,
        .battery_sample_valid = sensors.battery_valid,
        .battery_calibrated = false,
        .battery_millivolts = sensors.battery_mv,
        .battery_percent = sensors.battery_percent,
        .battery_last_error = sensors.battery_error,
        .battery_current_valid = sensors.battery_valid,
        .battery_milliamps = sensors.battery_ma,
        .motion_supported = sensors.imu_ready,
        .motion_valid = sensors.imu_valid,
        .rtc_supported = sensors.rtc_present,
        .rtc_valid = sensors.rtc_valid,
        .accel_mg = {sensors.accel_mg[0], sensors.accel_mg[1], sensors.accel_mg[2]},
        .gyro_mdps = {sensors.gyro_mdps[0], sensors.gyro_mdps[1], sensors.gyro_mdps[2]},
        .temperature_valid = sensors.temperature_valid,
        .temperature_millicelsius = sensors.temperature_mc,
#else
        .battery_supported = false,
        .battery_sample_valid = false,
        .battery_calibrated = false,
        .battery_last_error = ESP_ERR_NOT_SUPPORTED,
#endif
        .boot_volume_step = s_console_settings.boot_volume_step,
        .game_volume_step = s_console_settings.game_volume_step,
        .audio_settings_persistent = s_console_settings.persistent,
    };
#if CONFIG_P4_BOARD_M5STACK_TAB5
    if (sensors.rtc_valid) {
        (void)snprintf(info.rtc_datetime, sizeof(info.rtc_datetime),
            "%04u-%02u-%02u %02u:%02u:%02u UTC", sensors.rtc.year,
            sensors.rtc.month, sensors.rtc.day, sensors.rtc.hour, sensors.rtc.minute, sensors.rtc.second);
    }
#endif
    (void)snprintf(
        info.multiplayer_game_title,
        sizeof(info.multiplayer_game_title), "%s",
        multiplayer_selected_game_title());
    (void)snprintf(
        info.node_name, sizeof(info.node_name), "%s",
        s_console_settings.node_name);
#if P4_CONSOLE_BLE_GAMEPAD
    (void)snprintf(
        info.ble_controller_name, sizeof(info.ble_controller_name), "%s",
        ble_controller.name);
#endif
#if P4_CONSOLE_GAMEPAD_INPUT
    memcpy(info.controller_mapping,
           s_controller_mapping_active
               ? s_controller_mapping_pending.source
               : s_controller_mapping_current.source,
           sizeof(info.controller_mapping));
#endif
    (void)snprintf(
        info.file_transfer_name, sizeof(info.file_transfer_name), "%s",
        file_transfer.file_name);
    const size_t lobby_display_count =
        s_multiplayer_lobby_candidate_count <
                CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX
            ? s_multiplayer_lobby_candidate_count
            : CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX;
    for (size_t index = 0U; index < lobby_display_count; ++index) {
        const console_mp_lobby_candidate_t *const candidate =
            &s_multiplayer_lobby_candidates[index];
        info.multiplayer_lobbies[index] =
            (console_multiplayer_lobby_display_t){
                .session_id = candidate->session_id,
                .rssi = candidate->rssi,
                .players_present = candidate->players_present,
                .player_capacity = candidate->player_capacity,
                .game_available = candidate->game_available,
            };
        (void)snprintf(
            info.multiplayer_lobbies[index].game_title,
            sizeof(info.multiplayer_lobbies[index].game_title), "%s",
            candidate->game_available
                ? multiplayer_game_title_at(candidate->game_selection)
                : "NOT INSTALLED");
    }
    return info;
}

static const uint8_t s_arena_content_sha256[32] = P4_GCA_CONTENT_SHA256;

static const uint8_t s_doom_shareware_sha256[P4_MP_SHA256_BYTES] = {
    0x1d, 0x7d, 0x43, 0xbe, 0x50, 0x1e, 0x67, 0xd9,
    0x27, 0xe4, 0x15, 0xe0, 0xb8, 0xf3, 0xe2, 0x9c,
    0x3b, 0xf3, 0x30, 0x75, 0xe8, 0x59, 0x72, 0x18,
    0x16, 0xf6, 0x52, 0xa5, 0x26, 0xca, 0xc7, 0x71,
};

static const uint8_t s_chex_quest_sha256[P4_MP_SHA256_BYTES] = {
    0xd8, 0xeb, 0x52, 0x77, 0x91, 0x88, 0x83, 0xf4,
    0x90, 0xfb, 0x1a, 0x4b, 0xe3, 0xc9, 0xa8, 0x58,
    0x8d, 0xf2, 0xdb, 0xae, 0xe6, 0xdc, 0x4b, 0xeb,
    0x8d, 0xf4, 0x92, 0x91, 0x48, 0xbb, 0xff, 0xb1,
};

static uint8_t s_multiplayer_tx_datagram[P4_MP_MAX_DATAGRAM_BYTES];

#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
static esp_err_t content_uart_send(
    void *context, const uint8_t *bytes, size_t bytes_length)
{
    (void)context;
    return p4_mp_uart_endpoint_send_raw(bytes, bytes_length);
}

static esp_err_t content_uart_wait_tx(
    void *context, uint32_t timeout_ms)
{
    (void)context;
    return p4_mp_uart_endpoint_wait_tx_done(timeout_ms);
}

static esp_err_t content_uart_set_baud(
    void *context, uint32_t baudrate)
{
    (void)context;
    return p4_mp_uart_endpoint_set_baudrate(baudrate);
}

#if CONFIG_P4_BOARD_M5STACK_TAB5
static bool clock_usb_send(void *context, const uint8_t *bytes, size_t size)
{
    return content_uart_send(context, bytes, size) == ESP_OK;
}
static p4_clock_status_t clock_usb_status(void *context)
{
    (void)context;
    const platform_tab5_telemetry_t sensors = platform_tab5_sensors_snapshot();
    p4_clock_status_t status = {
        .present=sensors.rtc_present, .pending=sensors.rtc_set_pending,
        .valid=sensors.rtc_valid,
        .error=sensors.rtc_set_error ? sensors.rtc_set_error : sensors.rtc_error,
    };
    if (status.valid && !platform_tab5_datetime_to_unix(&sensors.rtc, &status.unix_seconds))
        status.valid=false;
    return status;
}
static int clock_usb_set(void *context, uint32_t seconds)
{
    (void)context;
    const esp_err_t result=platform_tab5_clock_set(seconds);
    return result==ESP_OK ? 0 : result==ESP_ERR_NOT_FINISHED ? 2 :
        result==ESP_ERR_INVALID_STATE ? 3 : result==ESP_ERR_INVALID_ARG ? 1 : 4;
}
#endif

#if P4_CONSOLE_H1_USB_DRIVE_CONTROL
static bool h1_usb_drive_send(
    void *context, const uint8_t *bytes, size_t bytes_length)
{
    return content_uart_send(context, bytes, bytes_length) == ESP_OK;
}

static bool h1_usb_drive_status(
    void *context, p4_h1_usb_drive_status_t *out_status)
{
    (void)context;
    if (out_status == NULL) {
        return false;
    }
    platform_game_storage_status_t storage;
    memset(&storage, 0, sizeof(storage));
    if (platform_game_storage_get_status(&storage) != ESP_OK) {
        return false;
    }
    const bool storage_eligible =
        storage.state == PLATFORM_GAME_STORAGE_APP_READY ||
        storage.state == PLATFORM_GAME_STORAGE_APP_MISSING ||
        storage.state == PLATFORM_GAME_STORAGE_APP_INVALID ||
        storage.state == PLATFORM_GAME_STORAGE_APP_SCANNING ||
        storage.state == PLATFORM_GAME_STORAGE_USB_HOST ||
        storage.state == PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED;
    const p4_content_transfer_info_t content = p4_content_transfer_info();
    const p4_file_transfer_info_t file = p4_file_transfer_info();
    *out_status = (p4_h1_usb_drive_status_t){
        .storage_state = (uint8_t)storage.state,
        .storage_generation = storage.generation,
        .supported = storage.usb_mode_supported,
        .usb_drive_active =
            storage.state == PLATFORM_GAME_STORAGE_USB_HOST ||
            storage.state == PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED,
        .usb_attached = storage.usb_attached,
        .usb_host_ejected = storage.usb_host_ejected,
        .usb_driver_running = storage.usb_driver_running,
        .control_available = storage.usb_mode_supported && storage_eligible &&
            content.state == P4_CONTENT_TRANSFER_IDLE &&
            file.state == P4_FILE_TRANSFER_IDLE,
    };
    return true;
}

static p4_h1_usb_drive_transition_result_t h1_usb_drive_set_mode(
    void *context, bool enable_usb_drive)
{
    (void)context;
    const p4_content_transfer_info_t content = p4_content_transfer_info();
    const p4_file_transfer_info_t file = p4_file_transfer_info();
    if (content.state != P4_CONTENT_TRANSFER_IDLE ||
        file.state != P4_FILE_TRANSFER_IDLE) {
        return P4_H1_USB_DRIVE_TRANSITION_DENIED;
    }

    esp_err_t result = ESP_OK;
    bool storage_transition_complete = false;
    if (enable_usb_drive) {
        result = stop_usb_input_for_role_switch();
        if (result == ESP_OK) {
            result = platform_game_storage_set_usb_mode(true);
            storage_transition_complete = result == ESP_OK;
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
                     "P4_CONSOLE_OS H1_USB_DRIVE_FAIL target=usb-drive "
                     "error=%s controller_recovery=%s",
                     esp_err_to_name(result), esp_err_to_name(recovery));
        }
    } else {
        /* Storage owns the eject gate and does not release MSC on refusal. */
        result = platform_game_storage_set_usb_mode(false);
        if (result == ESP_OK) {
            storage_transition_complete = true;
            result = start_usb_input();
        }
    }

    if (storage_transition_complete) {
        game_storage_init_publish(CONSOLE_STORAGE_INIT_READY, ESP_OK);
        s_file_listing_seen = false;
        if (!enable_usb_drive) {
            s_catalog_seen = false;
        }
        /* Match the UI's post-handoff status refresh. The storage service
         * itself rejects local FAT access while MSC owns the card. */
        sync_game_storage();
    }
    if (result == ESP_OK) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS H1_USB_DRIVE_MODE requested=%s result=ok "
                 "target_role=%s",
                 enable_usb_drive ? "on" : "off",
                 enable_usb_drive ? "usb-drive" : "controller-host");
        return P4_H1_USB_DRIVE_TRANSITION_OK;
    }
    ESP_LOGW(TAG,
             "P4_CONSOLE_OS H1_USB_DRIVE_MODE requested=%s result=%s "
             "target_role=%s",
             enable_usb_drive ? "on" : "off", esp_err_to_name(result),
             enable_usb_drive ? "usb-drive" : "controller-host");
    return result == ESP_ERR_INVALID_STATE
        ? P4_H1_USB_DRIVE_TRANSITION_DENIED
        : P4_H1_USB_DRIVE_TRANSITION_FAILED;
}
#endif

static bool content_uart_consume(
    void *context, const uint8_t *bytes, size_t bytes_length)
{
    (void)context;
    const p4_file_transfer_info_t file = p4_file_transfer_info();
    const p4_content_transfer_info_t content = p4_content_transfer_info();
    if (file.state != P4_FILE_TRANSFER_IDLE) {
        return p4_file_transfer_consume(bytes, bytes_length);
    }
    if (content.state != P4_CONTENT_TRANSFER_IDLE) {
        return p4_content_transfer_consume(bytes, bytes_length);
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    if (p4_clock_control_consume(&s_clock_control, bytes, bytes_length,
            (uint64_t)esp_timer_get_time() / 1000U)) return true;
#endif
#if P4_CONSOLE_H1_USB_DRIVE_CONTROL
    if (p4_h1_usb_drive_control_consume(
            &s_h1_usb_drive_control, bytes, bytes_length,
            (uint64_t)esp_timer_get_time() / UINT64_C(1000))) {
        return true;
    }
#endif
    if (p4_file_transfer_consume(bytes, bytes_length)) {
        return true;
    }
    return p4_content_transfer_consume(bytes, bytes_length);
}
#endif

static uint32_t random_nonzero(void)
{
    uint32_t value = 0U;
    while (value == 0U) {
        value = esp_random();
    }
    return value;
}

static uint32_t next_discovery_sequence(void)
{
    const uint32_t sequence = s_multiplayer_discovery_sequence;
    ++s_multiplayer_discovery_sequence;
    if (s_multiplayer_discovery_sequence == 0U) {
        s_multiplayer_discovery_sequence = 1U;
    }
    return sequence;
}

static uint16_t multiplayer_start_token(void)
{
    const uint64_t seed = s_multiplayer_launch_session_seed;
    uint32_t mixed = s_multiplayer_session.session_id ^
        (uint32_t)seed ^ (uint32_t)(seed >> 32U);
    mixed ^= mixed >> 16U;
    uint16_t token = (uint16_t)mixed;
    if (token == 0U) {
        token = 1U;
    }
    return token;
}

static bool multiplayer_content_ready_for(size_t selection)
{
    if (s_game_storage_status.state != PLATFORM_GAME_STORAGE_APP_READY) {
        return false;
    }
    if (selection == P4_DOOM_MP_GAME_GAME_CHANGERS_AI) {
        return s_multiplayer_transport==CONSOLE_MP_TRANSPORT_WIFI &&
            !s_game_storage_status.content_validation_running &&
            platform_game_storage_arena_present();
    }
    if (selection < P4_DOOM_MP_GAME_COUNT) {
        /*
         * Opening or joining a lobby must not hash every Doom-family asset.
         * The selected title is exact-hashed and captured in one pass by
         * lock_for_doom_title() at terminal launch.  A separately requested
         * background validation still owns storage until it finishes.
        */
        return !s_game_storage_status.content_validation_running;
    }
    return multiplayer_native_game_for(selection) != NULL;
}

static bool multiplayer_local_content_ready(void)
{
    return multiplayer_content_ready_for(s_multiplayer_game_selection);
}

static bool multiplayer_start_prerequisites_ready(void)
{
    return s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_CONNECTED &&
        s_multiplayer_launch_kind != CONSOLE_MP_LAUNCH_NONE &&
        s_multiplayer_session.state == P4_MP_SESSION_CONNECTED &&
        multiplayer_local_content_ready();
}

static esp_err_t configure_multiplayer_local_offer(void)
{
    p4_mp_lobby_offer_t offer = {
        .game_api_major = 1U,
        .game_api_minor = 0U,
        .players_present = 1U,
        .player_capacity = 2U,
        .session_seed =
            ((uint64_t)random_nonzero() << 32U) | random_nonzero(),
    };
    if (multiplayer_selected_game_is_doom()) {
        p4_doom_mp_setup_t setup = s_multiplayer_local_setup;
        setup.game = (p4_doom_mp_game_t)s_multiplayer_game_selection;
        if (multiplayer_selected_game_is_arena()) {
            offer.player_capacity=4;
            setup.mode=P4_DOOM_MP_MODE_ALTDEATH;
            setup.episode=1;
            if (setup.game!=s_multiplayer_local_setup.game || setup.map<1 || setup.map>26) setup.map=1;
            setup.no_monsters=true;
            setup.fast_monsters=false;
            setup.respawn_monsters=false;
            setup.time_limit_minutes=0;
        } else if (setup.map>P4_DOOM_MP_MAX_MAP) setup.map=1;
        offer.mode = P4_MP_GAME_MODE_LOCKSTEP;
        offer.input_delay_tics = 2U;
        offer.tick_rate_hz = P4_DOOM_MP_TICK_RATE_HZ;
        offer.game_protocol = multiplayer_selected_game_is_arena() ? 4U : CONSOLE_DOOM_MULTIPLAYER_PROTOCOL;
        strcpy(offer.game_id, multiplayer_selected_game_is_arena() ? "org.p4console.gamechangersai" : multiplayer_selected_game_is_chex()
            ? "org.p4console.chexquest" : "org.p4console.doom");
        const uint8_t *const content_sha256 =
            multiplayer_selected_game_is_arena() ? s_arena_content_sha256 : multiplayer_selected_game_is_chex()
                ? s_chex_quest_sha256 : s_doom_shareware_sha256;
        memcpy(offer.content_sha256, content_sha256,
               P4_MP_SHA256_BYTES);
        if (!p4_doom_mp_setup_encode(
                &setup, offer.game_settings)) {
            return ESP_ERR_INVALID_STATE;
        }
        s_multiplayer_local_setup = setup;
    } else {
        const platform_game_catalog_entry_t *const game =
            multiplayer_selected_native_game();
        if (!native_game_supports_multiplayer(game)) {
            return ESP_ERR_NOT_FOUND;
        }
        if (!p4_mp_game_dice_settings_encode(&game->package,
                multiplayer_dice_available() && s_multiplayer_shared_dice,
                offer.game_settings)) return ESP_ERR_INVALID_STATE;
        const p4_game_multiplayer_profile_t *const profile =
            &game->package.multiplayer_profile;
        offer.mode = native_multiplayer_mode(profile);
        uint8_t capacity=s_multiplayer_transport==CONSOLE_MP_TRANSPORT_WIFI?4U:2U;
        offer.player_capacity=profile->max_players<capacity?profile->max_players:capacity;
        offer.input_delay_tics = profile->input_delay_ticks;
        offer.tick_rate_hz = profile->tick_rate_hz;
        offer.game_protocol = profile->protocol;
        strcpy(offer.game_id, game->package.id);
        if (!native_multiplayer_content_identity(
                game, offer.content_sha256)) {
            return ESP_FAIL;
        }
    }
    uint8_t material[P4_MP_COMPATIBILITY_MATERIAL_BYTES];
    if (p4_mp_lobby_compatibility_material(
            &offer, material) != P4_MP_OK ||
        mbedtls_sha256(
            material, sizeof(material),
            offer.compatibility_sha256, 0) != 0) {
        return ESP_FAIL;
    }
    uint8_t payload[P4_MP_OFFER_PAYLOAD_BYTES];
    if (p4_mp_lobby_offer_encode(&offer, payload) != P4_MP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    s_multiplayer_local_offer = offer;
    return ESP_OK;
}

static esp_err_t initialize_multiplayer_lobby(void)
{
    s_multiplayer_local_peer_id = random_nonzero();
    s_multiplayer_local_session_id = random_nonzero();
    s_multiplayer_game_selection = 0U;
    const esp_err_t configured = configure_multiplayer_local_offer();
    if (configured != ESP_OK) {
        return configured;
    }
    s_multiplayer_lobby_state = CONSOLE_MP_LOBBY_BROWSING;
    s_multiplayer_lobby_selection = 0U;
    s_multiplayer_lobby_selected_id = 0U;
    s_multiplayer_lobby_selected_session_id = 0U;
    s_multiplayer_lobby_candidate_count = 0U;
    s_multiplayer_lobby_browser_ready_us = 0;
    s_multiplayer_lobby_create_explicit = false;
    s_multiplayer_host_collision_seen_session = 0U;
    return ESP_OK;
}

static void reset_multiplayer_lobby(const char *reason)
{
    const bool was_active =
        s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_IDLE &&
        s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_BROWSING;
    p4_mp_session_init(&s_multiplayer_session);
    s_multiplayer_lobby_state = CONSOLE_MP_LOBBY_BROWSING;
    s_multiplayer_remote_offer = (p4_mp_lobby_offer_t){0};
    s_doom_multiplayer_launch = (p4_doom_mp_launch_config_t){0};
    s_multiplayer_launch_kind = CONSOLE_MP_LAUNCH_NONE;
    s_multiplayer_launch_shared_dice = false;
    s_multiplayer_local_player_slot = P4_MP_PLAYER_SLOT_ANY;
    s_multiplayer_player_count = 0U;
    s_multiplayer_launch_route_id = 0U;
    s_multiplayer_launch_session_seed = 0U;
    s_multiplayer_native_launcher_id = 0U;
    s_native_multiplayer = (console_native_multiplayer_t){0};
    s_multiplayer_remote_peer_id = 0U;
    s_multiplayer_remote_session_id = 0U;
    s_multiplayer_remote_route_id = 0U;
    s_multiplayer_remote_offer_seen_us = 0;
    s_multiplayer_target_session_id = 0U;
    s_multiplayer_target_lobby_id = 0U;
    s_multiplayer_lobby_selection = 0U;
    s_multiplayer_lobby_selected_id = 0U;
    s_multiplayer_lobby_selected_session_id = 0U;
    s_multiplayer_lobby_candidate_count = 0U;
    s_multiplayer_lobby_browser_ready_us = 0;
    s_multiplayer_lobby_create_explicit = false;
    s_multiplayer_host_collision_seen_session = 0U;
    memset(s_multiplayer_lobby_candidates, 0,
           sizeof(s_multiplayer_lobby_candidates));
    s_multiplayer_next_control_us = 0;
    p4_mp_start_barrier_cancel(&s_multiplayer_start_barrier);
    s_multiplayer_group_start=(p4_mp_group_start_t){0};
    s_multiplayer_next_start_ready_us = 0;
    s_multiplayer_launch_due = false;
    multiplayer_transport_reset_route();
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI) (void)platform_multiplayer_wifi_browse();
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        const esp_err_t browser = platform_multiplayer_ble_set_lobby_mode(
            PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER,
            0U, 0U);
        if (browser != ESP_OK) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS BLE_LOBBY_BROWSER_DEGRADED error=%s",
                     esp_err_to_name(browser));
        }
    }
#endif
    if (was_active) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS MULTIPLAYER_LOBBY_RESET reason=%s",
                 reason == NULL ? "unknown" : reason);
    }
}

static esp_err_t select_multiplayer_transport(
    console_mp_transport_kind_t transport)
{
    if (transport == s_multiplayer_transport) {
        return ESP_OK;
    }
#if P4_CONSOLE_WIFI_MULTIPLAYER
    platform_multiplayer_wifi_disable();
    if (transport == CONSOLE_MP_TRANSPORT_WIFI) {
#if P4_CONSOLE_BLE_MULTIPLAYER
        platform_multiplayer_ble_disable();
#endif
        p4_mp_uart_endpoint_reset_route();
        const esp_err_t result = platform_multiplayer_wifi_enable(multiplayer_frame_received, NULL);
        if (result != ESP_OK) return result;
        s_multiplayer_transport = transport;
        s_multiplayer_transport_ready = false;
        return ESP_OK;
    }
#endif
    if (transport == CONSOLE_MP_TRANSPORT_BLE) {
#if P4_CONSOLE_BLE_MULTIPLAYER
        p4_mp_uart_endpoint_reset_route();
        s_multiplayer_transport = CONSOLE_MP_TRANSPORT_BLE;
        const esp_err_t result = platform_multiplayer_ble_enable(
            multiplayer_frame_received, NULL);
        if (result != ESP_OK) {
            s_multiplayer_transport = CONSOLE_MP_TRANSPORT_WIRED;
            (void)p4_mp_uart_endpoint_set_handler(
                multiplayer_frame_received, NULL);
            s_multiplayer_transport_ready = s_multiplayer_uart_ready;
            return result;
        }
        const esp_err_t browser = platform_multiplayer_ble_set_lobby_mode(
            PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER,
            0U, 0U);
        if (browser != ESP_OK) {
            platform_multiplayer_ble_disable();
            s_multiplayer_transport = CONSOLE_MP_TRANSPORT_WIRED;
            (void)p4_mp_uart_endpoint_set_handler(
                multiplayer_frame_received, NULL);
            s_multiplayer_transport_ready = s_multiplayer_uart_ready;
            return browser;
        }
        s_multiplayer_transport_ready = false;
        return ESP_OK;
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    }
#if P4_CONSOLE_BLE_MULTIPLAYER
    platform_multiplayer_ble_disable();
#endif
    s_multiplayer_transport = CONSOLE_MP_TRANSPORT_WIRED;
    p4_mp_uart_endpoint_reset_route();
    const esp_err_t result = s_multiplayer_uart_ready
        ? p4_mp_uart_endpoint_set_handler(multiplayer_frame_received, NULL)
        : ESP_ERR_INVALID_STATE;
    s_multiplayer_transport_ready = result == ESP_OK;
    return result;
}

#if P4_CONSOLE_BLE_GAMEPAD && P4_CONSOLE_BLE_MULTIPLAYER
static void release_ble_radio_handoff_to_gamepad(const char *reason)
{
    const platform_gamepad_ble_status_t status =
        platform_gamepad_ble_status();
    const bool resume = p4_ble_radio_handoff_release(
        &s_ble_radio_handoff,
        s_console_settings.ble_controller_enabled,
        status.connected,
        status.bonded);
    if (!resume) {
        return;
    }
    const esp_err_t result = start_ble_gamepad_connection();
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BLE_RADIO_HANDOFF owner=controller "
             "state=resume reason=%s result=%s",
             reason == NULL ? "unknown" : reason,
             esp_err_to_name(result));
}
#endif

#if P4_CONSOLE_BLE_MULTIPLAYER
static esp_err_t request_ble_multiplayer_transport(void)
{
#if P4_CONSOLE_BLE_GAMEPAD
    p4_ble_radio_handoff_action_t handoff_action;
    if (s_ble_radio_handoff.waiting_for_gamepad) {
        handoff_action = p4_ble_radio_handoff_poll(
            &s_ble_radio_handoff,
            platform_gamepad_ble_radio_idle());
    } else {
        const platform_gamepad_ble_status_t status =
            platform_gamepad_ble_status();
        handoff_action = p4_ble_radio_handoff_request(
            &s_ble_radio_handoff,
            status.connected,
            status.bonded,
            ble_gamepad_status_busy(&status));
    }
    if (handoff_action == P4_BLE_RADIO_HANDOFF_CANCEL_GAMEPAD) {
        platform_gamepad_ble_cancel();
        s_multiplayer_ble_enable_pending = true;
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BLE_RADIO_HANDOFF owner=multiplayer "
                 "state=cancel-controller-reconnect");
        return ESP_ERR_NOT_FINISHED;
    }
    if (handoff_action == P4_BLE_RADIO_HANDOFF_WAIT) {
        s_multiplayer_ble_enable_pending = true;
        return ESP_ERR_NOT_FINISHED;
    }
#endif
    s_multiplayer_ble_enable_pending = false;
    const esp_err_t result = select_multiplayer_transport(
        CONSOLE_MP_TRANSPORT_BLE);
    if (result == ESP_OK) {
        reset_multiplayer_lobby("transport-changed");
        s_multiplayer_peer_last_seen_us = 0;
        s_multiplayer_next_discovery_us = 0;
#if P4_CONSOLE_BLE_GAMEPAD
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BLE_RADIO_HANDOFF owner=multiplayer "
                 "state=ready");
#endif
    }
#if P4_CONSOLE_BLE_GAMEPAD
    else {
        release_ble_radio_handoff_to_gamepad("ble-start-failed");
    }
#endif
    return result;
}

static esp_err_t release_multiplayer_ble_transport(const char *reason)
{
    s_multiplayer_ble_enable_pending = false;
    esp_err_t result = ESP_OK;
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        result = select_multiplayer_transport(
            CONSOLE_MP_TRANSPORT_WIRED);
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BLE_RADIO_HANDOFF owner=wired "
                 "state=multiplayer-release reason=%s result=%s",
                 reason == NULL ? "unknown" : reason,
                 esp_err_to_name(result));
    }
#if P4_CONSOLE_BLE_GAMEPAD
    release_ble_radio_handoff_to_gamepad(reason);
#endif
    return result;
}
#endif

static uint8_t multiplayer_cycle_range(
    uint8_t value, uint8_t minimum, uint8_t maximum, int delta)
{
    if (delta < 0) {
        return value <= minimum ? maximum : (uint8_t)(value - 1U);
    }
    return value >= maximum ? minimum : (uint8_t)(value + 1U);
}

static bool set_multiplayer_lobby_selection(
    console_shell_t *shell, size_t selection, bool explicit_host)
{
    refresh_multiplayer_lobby_candidates();
    if (selection > s_multiplayer_lobby_candidate_count) {
        return false;
    }
    s_multiplayer_lobby_selection = selection;
    if (selection == 0U) {
        s_multiplayer_lobby_selected_id = 0U;
        s_multiplayer_lobby_selected_session_id = 0U;
        s_multiplayer_lobby_create_explicit = explicit_host;
    } else {
        const console_mp_lobby_candidate_t *const choice =
            &s_multiplayer_lobby_candidates[selection - 1U];
        s_multiplayer_lobby_selected_id = choice->lobby_id;
        s_multiplayer_lobby_selected_session_id = choice->session_id;
        s_multiplayer_lobby_create_explicit = false;
        if (choice->game_available &&
            choice->game_selection != s_multiplayer_game_selection) {
            const size_t previous_selection =
                s_multiplayer_game_selection;
            const p4_mp_lobby_offer_t previous_offer =
                s_multiplayer_local_offer;
            const p4_doom_mp_setup_t previous_setup =
                s_multiplayer_local_setup;
            s_multiplayer_game_selection = choice->game_selection;
            if (configure_multiplayer_local_offer() != ESP_OK) {
                s_multiplayer_game_selection = previous_selection;
                s_multiplayer_local_offer = previous_offer;
                s_multiplayer_local_setup = previous_setup;
                return false;
            }
        }
    }
    const console_mp_lobby_candidate_t *const selected =
        selected_multiplayer_lobby();
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MULTIPLAYER_LOBBY_SELECTION "
             "choice=%s session=%08" PRIx32 " game=%s found=%u",
             selected == NULL ? "host" : "join",
             selected == NULL ? 0U : selected->session_id,
             selected == NULL
                ? multiplayer_selected_game_title()
                : selected->game_available
                    ? multiplayer_game_title_at(selected->game_selection)
                    : "not-installed",
             (unsigned)s_multiplayer_lobby_candidate_count);
    if (shell != NULL) {
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(shell, &current_runtime);
    }
    return true;
}

static void handle_multiplayer_config_action(
    console_shell_t *shell,
    const console_shell_action_t *action)
{
    if (shell == NULL || action == NULL ||
        action->type != CONSOLE_ACTION_MULTIPLAYER_CONFIGURE ||
        action->multiplayer_option >= CONSOLE_MULTIPLAYER_OPTION_COUNT ||
        action->multiplayer_delta == 0 ||
        !multiplayer_settings_editable()) {
        return;
    }
    if (action->multiplayer_option == CONSOLE_MULTIPLAYER_OPTION_DICE) {
        if (shell->multiplayer_view != CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS ||
            !multiplayer_dice_available()) return;
        const bool previous = s_multiplayer_shared_dice;
        const p4_mp_lobby_offer_t previous_offer = s_multiplayer_local_offer;
        s_multiplayer_shared_dice = !previous;
        if (configure_multiplayer_local_offer() != ESP_OK) {
            s_multiplayer_shared_dice = previous;
            s_multiplayer_local_offer = previous_offer;
            return;
        }
        reset_multiplayer_lobby("dice-option-changed");
        s_multiplayer_next_discovery_us = 0;
        ESP_LOGI(TAG, "P4_CONSOLE_OS DICE_OPTION shared=%u",
                 s_multiplayer_shared_dice ? 1U : 0U);
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(shell, &current_runtime);
        return;
    }
    p4_doom_mp_setup_t requested = s_multiplayer_local_setup;
    const int delta = action->multiplayer_delta;
    if (action->multiplayer_option ==
        CONSOLE_MULTIPLAYER_OPTION_GAME) {
        const size_t game_count = multiplayer_game_count();
        if (game_count == 0U) {
            return;
        }
        const size_t previous_selection = s_multiplayer_game_selection;
        const p4_mp_lobby_offer_t previous_offer =
            s_multiplayer_local_offer;
        const p4_doom_mp_setup_t previous_setup =
            s_multiplayer_local_setup;
        if (delta < 0) {
            s_multiplayer_game_selection =
                (s_multiplayer_game_selection + game_count - 1U) %
                    game_count;
        } else {
            s_multiplayer_game_selection =
                (s_multiplayer_game_selection + 1U) % game_count;
        }
        if (multiplayer_selected_game_is_chex() &&
            s_multiplayer_local_setup.map > P4_DOOM_MP_MAX_CHEX_MAP) {
            s_multiplayer_local_setup.map = P4_DOOM_MP_MAX_CHEX_MAP;
        }
        const esp_err_t configured = configure_multiplayer_local_offer();
        if (configured != ESP_OK) {
            s_multiplayer_game_selection = previous_selection;
            s_multiplayer_local_offer = previous_offer;
            s_multiplayer_local_setup = previous_setup;
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS MULTIPLAYER_GAME_REJECTED "
                     "error=%s",
                     esp_err_to_name(configured));
            return;
        }
        reset_multiplayer_lobby("game-changed");
        s_multiplayer_next_discovery_us = 0;
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS MULTIPLAYER_GAME selected=%s "
                 "index=%u count=%u mode=%u",
                 s_multiplayer_local_offer.game_id,
                 (unsigned)s_multiplayer_game_selection,
                 (unsigned)game_count,
                 (unsigned)s_multiplayer_local_offer.mode);
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(shell, &current_runtime);
        return;
    }
    if (action->multiplayer_option ==
        CONSOLE_MULTIPLAYER_OPTION_LOBBY) {
        refresh_multiplayer_lobby_candidates();
        size_t requested_selection = s_multiplayer_lobby_selection;
        if (shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_JOIN) {
            const size_t choices = s_multiplayer_lobby_candidate_count;
            if (choices == 0U) {
                return;
            }
            if (requested_selection == 0U ||
                requested_selection > choices) {
                requested_selection = 1U;
            } else if (delta < 0) {
                requested_selection = requested_selection <= 1U
                    ? choices : requested_selection - 1U;
            } else {
                requested_selection = requested_selection >= choices
                    ? 1U : requested_selection + 1U;
            }
        } else {
            const size_t choices = s_multiplayer_lobby_candidate_count + 1U;
            requested_selection = delta < 0
                ? (requested_selection + choices - 1U) % choices
                : (requested_selection + 1U) % choices;
        }
        (void)set_multiplayer_lobby_selection(
            shell, requested_selection,
            shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST);
        return;
    }
    if (action->multiplayer_option ==
        CONSOLE_MULTIPLAYER_OPTION_TRANSPORT) {
        const unsigned choices = P4_CONSOLE_WIFI_MULTIPLAYER ? 3U : (P4_CONSOLE_BLE_MULTIPLAYER ? 2U : 1U);
        const console_mp_transport_kind_t requested_transport = (console_mp_transport_kind_t)(
            ((unsigned)s_multiplayer_transport + (delta < 0 ? choices - 1U : 1U)) % choices);
        esp_err_t result = ESP_OK;
        if (requested_transport == CONSOLE_MP_TRANSPORT_BLE) {
#if P4_CONSOLE_BLE_MULTIPLAYER
            result = request_ble_multiplayer_transport();
            if (result == ESP_ERR_NOT_FINISHED) {
                ESP_LOGI(TAG,
                         "P4_CONSOLE_OS MULTIPLAYER_TRANSPORT_DEFERRED "
                         "requested=ble reason=radio-handoff");
                return;
            }
#else
            result = ESP_ERR_NOT_SUPPORTED;
#endif
        } else {
#if P4_CONSOLE_BLE_MULTIPLAYER
            result = release_multiplayer_ble_transport("transport-changed");
            if (result == ESP_OK) result = select_multiplayer_transport(requested_transport);
#else
            result = select_multiplayer_transport(
                requested_transport);
#endif
            if (result == ESP_OK) {
                reset_multiplayer_lobby("transport-changed");
                s_multiplayer_peer_last_seen_us = 0;
                s_multiplayer_next_discovery_us = 0;
            }
        }
        if (result != ESP_OK) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS MULTIPLAYER_TRANSPORT_REJECTED "
                     "requested=%s error=%s",
                     requested_transport == CONSOLE_MP_TRANSPORT_BLE
                        ? "ble" : (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI ? "wifi-local" : "wired"),
                     esp_err_to_name(result));
            return;
        }
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS MULTIPLAYER_TRANSPORT selected=%s "
                 "radio_start=lazy",
                 requested_transport == CONSOLE_MP_TRANSPORT_BLE
                    ? "ble" : (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI ? "wifi-local" : "wired"));
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(shell, &current_runtime);
        return;
    }
    if (!multiplayer_selected_game_is_doom() ||
        (multiplayer_selected_game_is_arena() && action->multiplayer_option!=CONSOLE_MULTIPLAYER_OPTION_MAP)) {
        return;
    }
    switch (action->multiplayer_option) {
    case CONSOLE_MULTIPLAYER_OPTION_MODE:
        requested.mode = (p4_doom_mp_mode_t)multiplayer_cycle_range(
            (uint8_t)requested.mode,
            (uint8_t)P4_DOOM_MP_MODE_COOPERATIVE,
            (uint8_t)P4_DOOM_MP_MODE_ALTDEATH, delta);
        break;
    case CONSOLE_MULTIPLAYER_OPTION_MAP:
        requested.map = multiplayer_cycle_range(
            requested.map, 1U,
            multiplayer_selected_game_is_arena() ? 26U : multiplayer_selected_game_is_chex()
                ? P4_DOOM_MP_MAX_CHEX_MAP : P4_DOOM_MP_MAX_MAP,
            delta);
        break;
    case CONSOLE_MULTIPLAYER_OPTION_SKILL:
        requested.skill = multiplayer_cycle_range(
            requested.skill, 1U, P4_DOOM_MP_MAX_SKILL, delta);
        break;
    case CONSOLE_MULTIPLAYER_OPTION_MONSTERS:
        requested.no_monsters = !requested.no_monsters;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_FAST:
        requested.fast_monsters = !requested.fast_monsters;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_RESPAWN:
        requested.respawn_monsters = !requested.respawn_monsters;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_TIME_LIMIT: {
        static const uint8_t limits[] = {0U, 5U, 10U, 15U, 20U, 30U, 60U};
        size_t index = 0U;
        while (index + 1U < sizeof(limits) &&
               limits[index] != requested.time_limit_minutes) {
            ++index;
        }
        if (delta < 0) {
            index = index == 0U ? sizeof(limits) - 1U : index - 1U;
        } else {
            index = (index + 1U) % sizeof(limits);
        }
        requested.time_limit_minutes = limits[index];
        break;
    }
    case CONSOLE_MULTIPLAYER_OPTION_TRANSPORT:
    case CONSOLE_MULTIPLAYER_OPTION_LOBBY:
    case CONSOLE_MULTIPLAYER_OPTION_GAME:
        return;
    default:
        return;
    }
    if (!p4_doom_mp_setup_valid(&requested)) {
        return;
    }
    const p4_doom_mp_setup_t previous_setup = s_multiplayer_local_setup;
    const p4_mp_lobby_offer_t previous_offer = s_multiplayer_local_offer;
    s_multiplayer_local_setup = requested;
    const esp_err_t configured = configure_multiplayer_local_offer();
    if (configured != ESP_OK) {
        s_multiplayer_local_setup = previous_setup;
        s_multiplayer_local_offer = previous_offer;
        return;
    }
    reset_multiplayer_lobby("settings-changed");
    s_multiplayer_next_discovery_us = 0;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MULTIPLAYER_SETUP game=%u mode=%u "
             "episode=%u map=%u "
             "skill=%u monsters=%u fast=%u respawn=%u limit=%u",
             (unsigned)requested.game, (unsigned)requested.mode,
             (unsigned)requested.episode,
             (unsigned)requested.map, (unsigned)requested.skill,
             requested.no_monsters ? 0U : 1U,
             requested.fast_monsters ? 1U : 0U,
             requested.respawn_monsters ? 1U : 0U,
             (unsigned)requested.time_limit_minutes);
    const console_shell_runtime_info_t current_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &current_runtime);
}

static esp_err_t send_raw_multiplayer_packet(
    p4_mp_packet_type_t type,
    uint32_t session_id,
    uint32_t peer_id,
    const uint8_t *payload,
    uint16_t payload_length)
{
    size_t datagram_length = 0U;
    const p4_mp_status_t encoded = p4_mp_packet_encode(
        type, session_id, peer_id, next_discovery_sequence(), 0U,
        payload, payload_length,
        s_multiplayer_tx_datagram, sizeof(s_multiplayer_tx_datagram),
        &datagram_length);
    if (encoded != P4_MP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return multiplayer_transport_send(
        s_multiplayer_tx_datagram, datagram_length);
}

static esp_err_t send_session_multiplayer_to(
    uint64_t route_id, p4_mp_packet_type_t type,
    uint32_t ack,
    const uint8_t *payload,
    uint16_t payload_length)
{
    size_t datagram_length = 0U;
    const p4_mp_status_t encoded = p4_mp_session_encode(
        &s_multiplayer_session, type, ack, payload, payload_length,
        s_multiplayer_tx_datagram, sizeof(s_multiplayer_tx_datagram),
        &datagram_length);
    if (encoded != P4_MP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if(s_multiplayer_transport==CONSOLE_MP_TRANSPORT_WIFI)
        return platform_multiplayer_wifi_send_to(route_id,s_multiplayer_tx_datagram,datagram_length);
#endif
    (void)route_id;
    return multiplayer_transport_send(
        s_multiplayer_tx_datagram, datagram_length);
}

static esp_err_t send_session_multiplayer_packet(p4_mp_packet_type_t type,uint32_t ack,
    const uint8_t *payload,uint16_t payload_length)
{ return send_session_multiplayer_to(0,type,ack,payload,payload_length); }

static esp_err_t send_multiplayer_start_ready(p4_mp_packet_type_t type)
{
    if (type != P4_MP_PACKET_PING && type != P4_MP_PACKET_PONG) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t payload[P4_MP_START_PAYLOAD_BYTES];
    if (p4_mp_start_ready_encode(
            multiplayer_start_token(), payload) != P4_MP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return send_session_multiplayer_packet(
        type, 0U, payload, sizeof(payload));
}

static esp_err_t send_multiplayer_offer(void)
{
    uint8_t payload[P4_MP_OFFER_PAYLOAD_BYTES];
    if (p4_mp_lobby_offer_encode(
            &s_multiplayer_local_offer, payload) != P4_MP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return send_raw_multiplayer_packet(
        P4_MP_PACKET_OFFER,
        s_multiplayer_local_session_id,
        s_multiplayer_local_peer_id,
        payload,
        sizeof(payload));
}

static esp_err_t send_multiplayer_join(void)
{
    p4_mp_lobby_join_t join = {
        .requested_player_slot = P4_MP_PLAYER_SLOT_ANY,
        .join_nonce =
            s_multiplayer_local_peer_id ^ s_multiplayer_remote_peer_id ^
            s_multiplayer_session.session_id,
    };
    if (join.join_nonce == 0U) {
        join.join_nonce = 1U;
    }
    memcpy(join.compatibility_sha256,
           s_multiplayer_local_offer.compatibility_sha256,
           P4_MP_SHA256_BYTES);
    uint8_t payload[P4_MP_JOIN_PAYLOAD_BYTES];
    if (p4_mp_lobby_join_encode(&join, payload) != P4_MP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return send_session_multiplayer_packet(
        P4_MP_PACKET_JOIN, 0U, payload, sizeof(payload));
}

static void clear_remote_multiplayer_offer(void)
{
    s_multiplayer_remote_offer = (p4_mp_lobby_offer_t){0};
    s_multiplayer_remote_peer_id = 0U;
    s_multiplayer_remote_session_id = 0U;
    s_multiplayer_remote_route_id = 0U;
    s_multiplayer_remote_offer_seen_us = 0;
}

static esp_err_t create_multiplayer_lobby(void)
{
    if (s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_BROWSING ||
        !multiplayer_local_content_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    p4_mp_session_init(&s_multiplayer_session);
    clear_remote_multiplayer_offer();
    s_multiplayer_local_session_id = random_nonzero();
    s_multiplayer_local_offer.players_present = 1U;
    s_multiplayer_local_offer.session_seed =
        ((uint64_t)random_nonzero() << 32U) | random_nonzero();
    uint8_t encoded[P4_MP_OFFER_PAYLOAD_BYTES];
    if (p4_mp_lobby_offer_encode(
            &s_multiplayer_local_offer, encoded) != P4_MP_OK ||
        p4_mp_session_host_start(
            &s_multiplayer_session,
            s_multiplayer_local_session_id,
            s_multiplayer_local_peer_id,
            CONSOLE_MULTIPLAYER_PEER_TIMEOUT_MS) != P4_MP_OK) {
        p4_mp_session_init(&s_multiplayer_session);
        return ESP_ERR_INVALID_STATE;
    }
    s_multiplayer_lobby_state = CONSOLE_MP_LOBBY_HOSTING;
    s_multiplayer_target_session_id = s_multiplayer_local_session_id;
    s_multiplayer_target_lobby_id = 0U;
    s_multiplayer_lobby_selection = 0U;
    s_multiplayer_lobby_selected_id = 0U;
    s_multiplayer_lobby_selected_session_id = 0U;
    s_multiplayer_next_discovery_us = 0;
    multiplayer_transport_reset_route();
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI) {
        const esp_err_t result=platform_multiplayer_wifi_host(s_multiplayer_session.session_id,multiplayer_game_token());
        if (result!=ESP_OK) { reset_multiplayer_lobby("wifi-host-failed"); return result; }
    }
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        const esp_err_t result = platform_multiplayer_ble_set_lobby_mode(
            PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST,
            s_multiplayer_local_session_id,
            multiplayer_game_token());
        if (result != ESP_OK) {
            reset_multiplayer_lobby("ble-host-start-failed");
            return result;
        }
    }
#endif
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MULTIPLAYER_LOBBY_CREATED "
             "session=%08" PRIx32 " transport=%s",
             s_multiplayer_local_session_id,
             s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE
                ? "ble" : (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI ? "wifi-local" : "wired"));
    return ESP_OK;
}

static esp_err_t reopen_multiplayer_host_lobby(void)
{
    const uint32_t session_id = s_multiplayer_session.session_id;
    if (session_id == 0U ||
        s_multiplayer_session.role != P4_MP_ROLE_HOST) {
        return ESP_ERR_INVALID_STATE;
    }
    p4_mp_session_init(&s_multiplayer_session);
    if (p4_mp_session_host_start(
            &s_multiplayer_session,
            session_id,
            s_multiplayer_local_peer_id,
            CONSOLE_MULTIPLAYER_PEER_TIMEOUT_MS) != P4_MP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    clear_remote_multiplayer_offer();
    s_multiplayer_local_offer.players_present = 1U;
    s_doom_multiplayer_launch = (p4_doom_mp_launch_config_t){0};
    s_multiplayer_launch_kind = CONSOLE_MP_LAUNCH_NONE;
    s_multiplayer_launch_shared_dice = false;
    s_multiplayer_local_player_slot = P4_MP_PLAYER_SLOT_ANY;
    s_multiplayer_player_count = 0U;
    s_multiplayer_launch_route_id = 0U;
    s_multiplayer_launch_session_seed = 0U;
    s_multiplayer_native_launcher_id = 0U;
    s_multiplayer_lobby_state = CONSOLE_MP_LOBBY_HOSTING;
    s_multiplayer_target_session_id = session_id;
    s_multiplayer_target_lobby_id = 0U;
    s_multiplayer_peer_last_seen_us = 0;
    s_multiplayer_next_control_us = 0;
    s_multiplayer_next_discovery_us = 0;
    p4_mp_start_barrier_cancel(&s_multiplayer_start_barrier);
    s_multiplayer_group_start=(p4_mp_group_start_t){0};
    s_multiplayer_next_start_ready_us = 0;
    s_multiplayer_launch_due = false;
    multiplayer_transport_reset_route();
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI) {
        const esp_err_t result=platform_multiplayer_wifi_host(s_multiplayer_session.session_id,multiplayer_game_token());
        if (result!=ESP_OK) { reset_multiplayer_lobby("wifi-host-failed"); return result; }
    }
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        const esp_err_t result = platform_multiplayer_ble_set_lobby_mode(
            PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST,
            session_id, multiplayer_game_token());
        if (result != ESP_OK) {
            return result;
        }
    }
#endif
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MULTIPLAYER_LOBBY_REOPENED "
             "session=%08" PRIx32 " reason=guest-disconnected",
             session_id);
    return ESP_OK;
}

static esp_err_t start_multiplayer_client_for_offer(
    uint32_t session_id,
    uint32_t host_peer_id,
    uint64_t route_id)
{
    if (session_id == 0U || host_peer_id == 0U || route_id == 0U ||
        s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_JOINING) {
        return ESP_ERR_INVALID_ARG;
    }
    p4_mp_session_init(&s_multiplayer_session);
    if (p4_mp_session_client_start(
            &s_multiplayer_session,
            session_id,
            s_multiplayer_local_peer_id,
            host_peer_id,
            route_id,
            (uint64_t)esp_timer_get_time() / UINT64_C(1000),
            CONSOLE_MULTIPLAYER_PEER_TIMEOUT_MS) != P4_MP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    s_multiplayer_remote_peer_id = host_peer_id;
    s_multiplayer_remote_session_id = session_id;
    s_multiplayer_remote_route_id = route_id;
    return send_multiplayer_join();
}

static esp_err_t join_selected_multiplayer_lobby(void)
{
    if (s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_BROWSING) {
        return ESP_ERR_INVALID_STATE;
    }
    refresh_multiplayer_lobby_candidates();
    const console_mp_lobby_candidate_t *const selected =
        selected_multiplayer_lobby();
    if (selected == NULL || selected->session_id == 0U ||
        !selected->game_available) {
        return ESP_ERR_NOT_FOUND;
    }
    const console_mp_lobby_candidate_t target = *selected;
    const size_t previous_selection = s_multiplayer_game_selection;
    const p4_mp_lobby_offer_t previous_offer = s_multiplayer_local_offer;
    const p4_doom_mp_setup_t previous_setup = s_multiplayer_local_setup;
    s_multiplayer_game_selection = target.game_selection;
    if (configure_multiplayer_local_offer() != ESP_OK ||
        !multiplayer_local_content_ready()) {
        s_multiplayer_game_selection = previous_selection;
        s_multiplayer_local_offer = previous_offer;
        s_multiplayer_local_setup = previous_setup;
        return ESP_ERR_NOT_FOUND;
    }
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIRED &&
        !p4_mp_lobby_offers_compatible(
            &s_multiplayer_local_offer, &s_multiplayer_remote_offer)) {
        s_multiplayer_game_selection = previous_selection;
        s_multiplayer_local_offer = previous_offer;
        s_multiplayer_local_setup = previous_setup;
        return ESP_ERR_NOT_SUPPORTED;
    }
#else
    if (!p4_mp_lobby_offers_compatible(
            &s_multiplayer_local_offer, &s_multiplayer_remote_offer)) {
        s_multiplayer_game_selection = previous_selection;
        s_multiplayer_local_offer = previous_offer;
        s_multiplayer_local_setup = previous_setup;
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    s_multiplayer_target_session_id = target.session_id;
    s_multiplayer_target_lobby_id = target.lobby_id;
    s_multiplayer_lobby_state = CONSOLE_MP_LOBBY_JOINING;
    s_multiplayer_next_discovery_us = 0;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MULTIPLAYER_LOBBY_JOIN_SELECTED "
             "session=%08" PRIx32 " lobby=%" PRIu64 " transport=%s",
             s_multiplayer_target_session_id,
             s_multiplayer_target_lobby_id,
             s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE
                ? "ble" : (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI ? "wifi-local" : "wired"));
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI) {
        const esp_err_t result=platform_multiplayer_wifi_join(target.lobby_id);
        s_wifi_join_deadline=esp_timer_get_time()+INT64_C(15000000);
        if(result!=ESP_OK) reset_multiplayer_lobby("wifi-join-failed");
        return result;
    }
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        const esp_err_t result =
            platform_multiplayer_ble_join_lobby(target.lobby_id);
        if (result != ESP_OK) {
            s_multiplayer_lobby_state = CONSOLE_MP_LOBBY_BROWSING;
            s_multiplayer_target_session_id = 0U;
            s_multiplayer_target_lobby_id = 0U;
        }
        return result;
    }
#endif
    const esp_err_t result = start_multiplayer_client_for_offer(
        target.session_id, target.host_peer_id, target.route_id);
    if (result != ESP_OK) {
        reset_multiplayer_lobby("wired-join-start-failed");
    }
    return result;
}

#if P4_CONSOLE_BLE_MULTIPLAYER
static void resolve_ble_host_collision(void)
{
    if (s_multiplayer_transport != CONSOLE_MP_TRANSPORT_BLE ||
        s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_HOSTING) {
        return;
    }
    platform_multiplayer_ble_lobby_t lobbies[
        PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES];
    const size_t count = platform_multiplayer_ble_list_lobbies(
        lobbies, sizeof(lobbies) / sizeof(lobbies[0]));
    const platform_multiplayer_ble_lobby_t *target = NULL;
    for (size_t index = 0U; index < count; ++index) {
        if (lobbies[index].session_id == 0U ||
            lobbies[index].session_id == s_multiplayer_local_session_id) {
            continue;
        }
        if (target == NULL ||
            lobbies[index].session_id < target->session_id) {
            target = &lobbies[index];
        }
    }
    if (target == NULL) {
        return;
    }
    if (s_multiplayer_local_session_id < target->session_id) {
        if (s_multiplayer_host_collision_seen_session !=
                target->session_id) {
            s_multiplayer_host_collision_seen_session = target->session_id;
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS BLE_HOST_COLLISION action=keep-host "
                     "local=%08" PRIx32 " remote=%08" PRIx32,
                     s_multiplayer_local_session_id,
                     target->session_id);
        }
        return;
    }
    const uint64_t target_lobby_id = target->lobby_id;
    const uint32_t target_session_id = target->session_id;
    ESP_LOGW(TAG,
             "P4_CONSOLE_OS BLE_HOST_COLLISION action=yield-and-join "
             "local=%08" PRIx32 " remote=%08" PRIx32,
             s_multiplayer_local_session_id, target_session_id);
    reset_multiplayer_lobby("ble-host-collision-yield");
    refresh_multiplayer_lobby_candidates();
    for (size_t index = 0U;
         index < s_multiplayer_lobby_candidate_count; ++index) {
        const console_mp_lobby_candidate_t *const candidate =
            &s_multiplayer_lobby_candidates[index];
        if (candidate->lobby_id == target_lobby_id &&
            candidate->session_id == target_session_id) {
            s_multiplayer_lobby_selection = index + 1U;
            s_multiplayer_lobby_selected_id = target_lobby_id;
            s_multiplayer_lobby_selected_session_id = target_session_id;
            s_multiplayer_lobby_create_explicit = false;
            break;
        }
    }
    const esp_err_t result = join_selected_multiplayer_lobby();
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BLE_HOST_COLLISION_JOIN "
             "session=%08" PRIx32 " result=%s",
             target_session_id, esp_err_to_name(result));
}
#endif

static void configure_multiplayer_launch(
    p4_mp_role_t role,
    uint64_t route_id,
    uint32_t remote_peer_id,
    uint8_t local_player_slot,
    const p4_mp_lobby_accept_t *accept)
{
    if (accept == NULL || route_id == 0U ||
        accept->player_count < 2U || accept->player_count > s_multiplayer_local_offer.player_capacity ||
        (accept->player_count>2U && !multiplayer_group_enabled()) ||
        accept->assigned_player_slot >= accept->player_count ||
        accept->session_seed == 0U) {
        return;
    }
    p4_doom_mp_setup_t setup = s_multiplayer_local_setup;
    if (multiplayer_selected_game_is_doom()) {
        const p4_doom_mp_game_t expected_game = (p4_doom_mp_game_t)s_multiplayer_game_selection;
        if (!p4_doom_mp_setup_decode(
                accept->game_settings, sizeof(accept->game_settings),
                &setup) || setup.episode != 1U ||
            setup.game != expected_game) {
            reset_multiplayer_lobby("invalid-doom-setup");
            return;
        }
        const p4_doom_mp_launch_config_t launch = {
            .enabled = true,
            .role = role,
            .session_id = s_multiplayer_session.session_id,
            .self_peer_id = s_multiplayer_local_peer_id,
            .remote_peer_id = remote_peer_id,
            .route_id = route_id,
            .local_player_slot = local_player_slot,
            .player_count = accept->player_count,
            .input_delay_tics = accept->input_delay_tics,
            .start_tic = accept->start_tic,
            .session_seed = accept->session_seed,
            .setup = setup,
        };
        if (!p4_doom_mp_launch_config_valid(&launch)) {
            reset_multiplayer_lobby("invalid-doom-launch-config");
            return;
        }
        s_doom_multiplayer_launch = launch;
        s_multiplayer_launch_kind = CONSOLE_MP_LAUNCH_DOOM;
        s_multiplayer_native_launcher_id = 0U;
    } else {
        const platform_game_catalog_entry_t *const game =
            multiplayer_selected_native_game();
        uint8_t identity[P4_MP_SHA256_BYTES];
        if (!native_game_supports_multiplayer(game) ||
            !native_multiplayer_content_identity(game, identity) ||
            strcmp(game->package.id,
                   s_multiplayer_local_offer.game_id) != 0 ||
            memcmp(identity,
                   s_multiplayer_local_offer.content_sha256,
                   P4_MP_SHA256_BYTES) != 0 ||
            s_multiplayer_local_offer.mode !=
                native_multiplayer_mode(
                    &game->package.multiplayer_profile)) {
            reset_multiplayer_lobby("invalid-native-launch-config");
            return;
        }
        bool shared_dice = false;
        if (!p4_mp_game_dice_settings_decode(&game->package,
                accept->game_settings, &shared_dice) ||
            (role == P4_MP_ROLE_HOST && shared_dice && !multiplayer_dice_available())) {
            reset_multiplayer_lobby("invalid-native-dice-setting");
            return;
        }
        s_multiplayer_launch_shared_dice = shared_dice;
        ESP_LOGI(TAG, "P4_CONSOLE_OS DICE_MATCH shared=%u owner=%s",
                 shared_dice ? 1U : 0U, role == P4_MP_ROLE_HOST ? "host" : "remote-host");
        s_doom_multiplayer_launch = (p4_doom_mp_launch_config_t){0};
        s_multiplayer_launch_kind = CONSOLE_MP_LAUNCH_NATIVE;
        s_multiplayer_native_launcher_id = game->package.launcher_id;
    }
    s_multiplayer_local_player_slot = local_player_slot;
    s_multiplayer_player_count = accept->player_count;
    s_multiplayer_launch_route_id = route_id;
    s_multiplayer_launch_session_seed = accept->session_seed;
    if (role == P4_MP_ROLE_HOST) {
        s_multiplayer_local_offer.players_present = accept->player_count;
    }
    s_multiplayer_remote_peer_id = remote_peer_id;
    s_multiplayer_lobby_state = CONSOLE_MP_LOBBY_CONNECTED;
    s_multiplayer_next_control_us = 0;
    p4_mp_start_barrier_cancel(&s_multiplayer_start_barrier);
    s_multiplayer_group_start=(p4_mp_group_start_t){0};
    s_multiplayer_next_start_ready_us = 0;
    s_multiplayer_launch_due = false;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MULTIPLAYER_LOBBY_READY "
             "game=%s mode=%u episode=%u map=%u skill=%u "
             "players=%u "
             "local_slot=%u role=%s tick_hz=%u input_delay=%u "
             "transport=%s route=%" PRIu64,
             s_multiplayer_local_offer.game_id,
             (unsigned)setup.mode, (unsigned)setup.episode,
             (unsigned)setup.map, (unsigned)setup.skill,
             (unsigned)accept->player_count,
             (unsigned)local_player_slot,
             role == P4_MP_ROLE_HOST ? "host" : "client",
             (unsigned)s_multiplayer_local_offer.tick_rate_hz,
             (unsigned)accept->input_delay_tics,
             multiplayer_transport_route_name(route_id), route_id);
}

static esp_err_t begin_multiplayer_start_sync(void)
{
    if (!multiplayer_start_prerequisites_ready() ||
        s_multiplayer_session.role != P4_MP_ROLE_HOST) {
        return ESP_ERR_INVALID_STATE;
    }
    if(multiplayer_group_enabled()) {
        if(!p4_mp_group_begin(&s_multiplayer_group_start,multiplayer_start_token(),
            multiplayer_members(),(uint64_t)esp_timer_get_time()/1000U))return ESP_ERR_INVALID_STATE;
        s_multiplayer_player_count=s_multiplayer_group_start.count;
        if (s_doom_multiplayer_launch.enabled) s_doom_multiplayer_launch.player_count=s_multiplayer_player_count;
        return ESP_OK;
    }
    const uint64_t now_ms =
        (uint64_t)esp_timer_get_time() / UINT64_C(1000);
    const p4_mp_status_t status = p4_mp_start_barrier_begin(
        &s_multiplayer_start_barrier,
        multiplayer_start_token(), now_ms,
        CONSOLE_MULTIPLAYER_START_HOLD_MS,
        CONSOLE_MULTIPLAYER_START_TIMEOUT_MS);
    if (status != P4_MP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    s_multiplayer_next_start_ready_us = 0;
    const esp_err_t send_result =
        send_multiplayer_start_ready(P4_MP_PACKET_PING);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MULTIPLAYER_START_SYNC state=waiting "
             "hold_ms=%u trigger=local",
             (unsigned)CONSOLE_MULTIPLAYER_START_HOLD_MS);
    return send_result;
}

static void native_multiplayer_mark_peer_left(void)
{
    if (!s_native_multiplayer.active) {
        return;
    }
    s_native_multiplayer.state = P4_GAME_MULTIPLAYER_PEER_LEFT;
    s_native_multiplayer.next_keepalive_us = 0;
}

static void native_multiplayer_queue_message(const p4_mp_event_t *event)
{
    if (!s_native_multiplayer.active || event == NULL ||
        event->type != P4_MP_EVENT_GAME_MESSAGE ||
        event->packet.payload == NULL || event->packet.payload_length == 0U ||
        event->packet.payload_length >
            s_native_multiplayer.profile.message_bytes) {
        return;
    }
    if (s_native_multiplayer.queue_count >=
        CONSOLE_NATIVE_MULTIPLAYER_QUEUE_DEPTH) {
        if (s_native_multiplayer.dropped_messages != UINT32_MAX) {
            ++s_native_multiplayer.dropped_messages;
        }
        return;
    }
    const size_t tail = (s_native_multiplayer.queue_head +
        s_native_multiplayer.queue_count) %
        CONSOLE_NATIVE_MULTIPLAYER_QUEUE_DEPTH;
    p4_game_multiplayer_message_t *const message =
        &s_native_multiplayer.queue[tail];
    *message = (p4_game_multiplayer_message_t){
        .sequence = event->packet.sequence,
        .player_slot = event->player_slot,
        .bytes = (uint8_t)event->packet.payload_length,
    };
    memcpy(message->data, event->packet.payload,
           event->packet.payload_length);
    ++s_native_multiplayer.queue_count;
}

static void multiplayer_frame_received(
    void *context,
    uint64_t route_id,
    const uint8_t *datagram,
    size_t datagram_length)
{
    (void)context;
    if (!multiplayer_route_matches_transport(route_id)) {
        return;
    }
    p4_mp_packet_view_t packet;
    const p4_mp_status_t status = p4_mp_packet_decode(
        datagram, datagram_length, &packet);
    if (status != P4_MP_OK) {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    const bool new_peer = s_multiplayer_peer_last_seen_us == 0 ||
        now_us - s_multiplayer_peer_last_seen_us >
            (int64_t)CONSOLE_MULTIPLAYER_PEER_TIMEOUT_MS * 1000;
    s_multiplayer_peer_last_seen_us = now_us;
    if (new_peer) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS MULTIPLAYER_PEER_SEEN "
                 "transport=%s route=%" PRIu64,
                 multiplayer_transport_route_name(route_id), route_id);
    }
    if (packet.type == P4_MP_PACKET_DISCOVER) {
        if (new_peer) {
            s_multiplayer_discovery_reply_pending = true;
        }
        return;
    }
    if (packet.type == P4_MP_PACKET_OFFER) {
        p4_mp_lobby_offer_t remote;
        if (p4_mp_lobby_offer_decode(
                packet.payload, packet.payload_length, &remote) != P4_MP_OK ||
            packet.peer_id == s_multiplayer_local_peer_id ||
            s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_CONNECTED) {
            return;
        }
        if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_HOSTING) {
            return;
        }
        if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_BROWSING) {
#if P4_CONSOLE_BLE_MULTIPLAYER
            if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
                return;
            }
#endif
            const bool changed =
                s_multiplayer_remote_session_id != packet.session_id ||
                s_multiplayer_remote_peer_id != packet.peer_id ||
                s_multiplayer_remote_route_id != route_id;
            s_multiplayer_remote_offer = remote;
            s_multiplayer_remote_session_id = packet.session_id;
            s_multiplayer_remote_peer_id = packet.peer_id;
            s_multiplayer_remote_route_id = route_id;
            s_multiplayer_remote_offer_seen_us = now_us;
            if (changed) {
                ESP_LOGI(TAG,
                         "P4_CONSOLE_OS MULTIPLAYER_LOBBY_DISCOVERED "
                         "session=%08" PRIx32 " transport=%s",
                         packet.session_id,
                         multiplayer_transport_route_name(route_id));
            }
            refresh_multiplayer_lobby_candidates();
            return;
        }
        if (s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_JOINING ||
            packet.session_id != s_multiplayer_target_session_id ||
            !multiplayer_local_content_ready() ||
            !p4_mp_lobby_offers_compatible(
                &s_multiplayer_local_offer, &remote)) {
            return;
        }
        s_multiplayer_remote_offer = remote;
        s_multiplayer_remote_session_id = packet.session_id;
        s_multiplayer_remote_peer_id = packet.peer_id;
        s_multiplayer_remote_route_id = route_id;
        s_multiplayer_remote_offer_seen_us = now_us;
        if (s_multiplayer_session.role != P4_MP_ROLE_CLIENT ||
            s_multiplayer_session.session_id != packet.session_id ||
            s_multiplayer_session.peers[0].peer_id != packet.peer_id) {
            const esp_err_t joined = start_multiplayer_client_for_offer(
                packet.session_id, packet.peer_id, route_id);
            if (joined != ESP_OK) {
                reset_multiplayer_lobby("client-start-failed");
            }
        } else {
            (void)send_multiplayer_join();
        }
        return;
    }

    if (packet.type == P4_MP_PACKET_JOIN) {
        p4_mp_lobby_join_t join;
        if ((!multiplayer_accepting_members() || s_native_multiplayer.active) ||
            p4_mp_lobby_join_decode(
                packet.payload, packet.payload_length, &join) != P4_MP_OK ||
            !p4_mp_lobby_join_matches_offer(
                &s_multiplayer_local_offer, &join)) {
            return;
        }
    } else if (packet.type == P4_MP_PACKET_ACCEPT) {
        p4_mp_lobby_accept_t accept;
        if (s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_JOINING ||
            p4_mp_lobby_accept_decode(
                packet.payload, packet.payload_length, &accept) != P4_MP_OK ||
            accept.session_seed != s_multiplayer_remote_offer.session_seed ||
            accept.input_delay_tics !=
                s_multiplayer_remote_offer.input_delay_tics ||
            accept.player_count < 2U || accept.player_count>s_multiplayer_remote_offer.player_capacity ||
            (accept.player_count>2U&&!multiplayer_group_enabled()) ||
            memcmp(accept.game_settings,
                   s_multiplayer_remote_offer.game_settings,
                   sizeof(accept.game_settings)) != 0) {
            return;
        }
    }

    p4_mp_event_t event;
    if (p4_mp_session_receive(
            &s_multiplayer_session, route_id,
            (uint64_t)now_us / 1000U,
            datagram, datagram_length, &event) != P4_MP_OK) {
        return;
    }
    if (s_native_multiplayer.active) {
        if (event.type == P4_MP_EVENT_GAME_MESSAGE) {
            native_multiplayer_queue_message(&event);
            return;
        }
        if (event.type == P4_MP_EVENT_PING) {
            (void)send_session_multiplayer_packet(
                P4_MP_PACKET_PONG, event.packet.sequence,
                event.packet.payload, event.packet.payload_length);
            return;
        }
        if (event.type == P4_MP_EVENT_PONG) {
            return;
        }
        if (event.type == P4_MP_EVENT_PEER_LEFT ||
            event.type == P4_MP_EVENT_REJECTED) {
            native_multiplayer_mark_peer_left();
            return;
        }
    }
    if(multiplayer_group_enabled() && event.type==P4_MP_EVENT_GAME_MESSAGE &&
       event.packet.payload_length==P4_MP_GROUP_BYTES && !memcmp(event.packet.payload,"P4GS",4)) {
        if(s_multiplayer_lobby_state==CONSOLE_MP_LOBBY_CONNECTED &&
           p4_mp_group_receive(&s_multiplayer_group_start,s_multiplayer_local_player_slot,event.player_slot,
            multiplayer_start_token(),event.packet.payload,event.packet.payload_length,(uint64_t)now_us/1000U)) {
            s_multiplayer_player_count=s_multiplayer_group_start.count;
            if (s_doom_multiplayer_launch.enabled)
                s_doom_multiplayer_launch.player_count=s_multiplayer_player_count;
        }
        return;
    }
    if (!multiplayer_group_enabled() && (event.type == P4_MP_EVENT_PING ||
        event.type == P4_MP_EVENT_PONG)) {
        uint16_t token = 0U;
        if (p4_mp_start_ready_decode(
                event.packet.payload, event.packet.payload_length,
                &token) == P4_MP_OK) {
            if (token != multiplayer_start_token() ||
                !multiplayer_start_prerequisites_ready()) {
                return;
            }
            const bool was_idle =
                s_multiplayer_start_barrier.state == P4_MP_START_IDLE;
            if (p4_mp_start_barrier_observe_ready(
                    &s_multiplayer_start_barrier, token,
                    (uint64_t)now_us / UINT64_C(1000),
                    CONSOLE_MULTIPLAYER_START_HOLD_MS,
                    CONSOLE_MULTIPLAYER_START_TIMEOUT_MS) == P4_MP_OK) {
                s_multiplayer_next_start_ready_us = 0;
                if (event.type == P4_MP_EVENT_PING) {
                    (void)send_multiplayer_start_ready(P4_MP_PACKET_PONG);
                }
                if (was_idle) {
                    ESP_LOGI(TAG,
                             "P4_CONSOLE_OS MULTIPLAYER_START_SYNC "
                             "state=armed trigger=peer hold_ms=%u",
                             (unsigned)CONSOLE_MULTIPLAYER_START_HOLD_MS);
                }
            }
            return;
        }
    }
    if (event.type == P4_MP_EVENT_JOIN_REQUEST && multiplayer_accepting_members()) {
        uint8_t slot=0;
        for(unsigned i=0;i<P4_MP_MAX_REMOTE_PEERS;++i)
            if(s_multiplayer_session.peers[i].connected && s_multiplayer_session.peers[i].peer_id==event.peer_id)
                slot=s_multiplayer_session.peers[i].player_slot;
        if(!slot)for(uint8_t candidate=1;candidate<s_multiplayer_local_offer.player_capacity;++candidate) {
            bool used=false;
            for(unsigned i=0;i<P4_MP_MAX_REMOTE_PEERS;++i)
                if(s_multiplayer_session.peers[i].connected&&s_multiplayer_session.peers[i].player_slot==candidate)used=true;
            if(!used){slot=candidate;break;}
        }
        if(!slot || p4_mp_session_accept_peer(&s_multiplayer_session,event.peer_id,event.route_id,
            slot,event.packet.sequence,(uint64_t)now_us/1000U)!=P4_MP_OK)return;
        p4_mp_lobby_accept_t accept = {
            .assigned_player_slot = slot,
            .player_count = multiplayer_members(),
            .input_delay_tics =
                s_multiplayer_local_offer.input_delay_tics,
            .start_tic = 0U,
            .session_seed = s_multiplayer_local_offer.session_seed,
        };
        memcpy(accept.game_settings,
               s_multiplayer_local_offer.game_settings,
               sizeof(accept.game_settings));
        uint8_t payload[P4_MP_ACCEPT_PAYLOAD_BYTES];
        if (p4_mp_lobby_accept_encode(&accept, payload) == P4_MP_OK &&
            send_session_multiplayer_to(
                event.route_id,P4_MP_PACKET_ACCEPT, event.packet.sequence,
                payload, sizeof(payload)) == ESP_OK) {
            configure_multiplayer_launch(
                P4_MP_ROLE_HOST, event.route_id,
                event.peer_id, 0U, &accept);
        }
    } else if (event.type == P4_MP_EVENT_ACCEPTED &&
               s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_JOINING) {
        p4_mp_lobby_accept_t accept;
        if (p4_mp_lobby_accept_decode(
                event.packet.payload, event.packet.payload_length,
                &accept) == P4_MP_OK) {
            configure_multiplayer_launch(
                P4_MP_ROLE_CLIENT, event.route_id, event.peer_id,
                accept.assigned_player_slot, &accept);
        }
    } else if (event.type == P4_MP_EVENT_PING) {
        (void)send_session_multiplayer_packet(
            P4_MP_PACKET_PONG, event.packet.sequence,
            event.packet.payload, event.packet.payload_length);
    }
}

static void poll_multiplayer_link(const console_shell_t *shell)
{
    const bool storage_available =
        storage_app_owned();
    const p4_file_transfer_info_t file_before = p4_file_transfer_info();
    const p4_content_transfer_info_t content_before =
        p4_content_transfer_info();
    p4_content_transfer_set_available(
        storage_available && !file_before.busy &&
        !s_game_storage_status.content_validation_running);
    p4_file_transfer_set_available(
        storage_available && !content_before.busy);
    p4_content_transfer_poll();
    p4_file_transfer_poll();
#if P4_CONSOLE_H1_USB_DRIVE_CONTROL
    p4_h1_usb_drive_control_poll(
        &s_h1_usb_drive_control,
        (uint64_t)esp_timer_get_time() / UINT64_C(1000));
#endif
    const p4_file_transfer_info_t file_after = p4_file_transfer_info();
    if (file_after.generation != s_file_transfer_generation_seen) {
        s_file_transfer_generation_seen = file_after.generation;
        if ((file_after.direction == P4_FILE_TRANSFER_UPLOAD ||
             file_after.direction == P4_FILE_TRANSFER_REMOVE) &&
            (file_after.file_class == P4_FILE_TRANSFER_CLASS_P4G ||
             file_after.file_class == P4_FILE_TRANSFER_CLASS_P4R)) {
            /* Native package/resource activation invalidates the catalogue. */
            s_catalog_seen = false;
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS P4G_TRANSFER_ACTIVATED name=%s "
                     "generation=%lu action=native-catalog-rescan",
                     file_after.file_name,
                     (unsigned long)file_after.generation);
        }
    }
#if P4_CONSOLE_WIFI_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI) {
        platform_multiplayer_wifi_poll();
        const platform_multiplayer_wifi_status_t wifi=platform_multiplayer_wifi_status();
        if (s_multiplayer_lobby_state==CONSOLE_MP_LOBBY_JOINING && !wifi.starting &&
            (wifi.last_error!=ESP_OK || esp_timer_get_time()>s_wifi_join_deadline))
            reset_multiplayer_lobby("wifi-join-ended");
    }
#endif
    /* H1 remains live for content uploads even while BLE owns game traffic. */
    p4_mp_uart_endpoint_poll();
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE) {
        platform_multiplayer_ble_poll();
        resolve_ble_host_collision();
    }
#endif
    refresh_multiplayer_lobby_candidates();
    const console_mp_transport_status_t transport =
        multiplayer_transport_status();
    s_multiplayer_transport_ready = transport.ready;
    const int64_t lobby_now_us = esp_timer_get_time();
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE &&
        s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_BROWSING &&
        transport.available) {
        if (s_multiplayer_lobby_browser_ready_us == 0) {
            s_multiplayer_lobby_browser_ready_us = lobby_now_us;
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS MULTIPLAYER_LOBBY_SCAN_READY "
                     "settle_ms=%u",
                     (unsigned)CONSOLE_MULTIPLAYER_BLE_BROWSER_SETTLE_MS);
        }
    } else {
        s_multiplayer_lobby_browser_ready_us = 0;
    }
#if P4_CONSOLE_BLE_MULTIPLAYER
    if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_BLE &&
        s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_JOINING) {
        const platform_multiplayer_ble_status_t ble =
            platform_multiplayer_ble_status();
        if (!ble.connected &&
            ble.lobby_mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER) {
            reset_multiplayer_lobby("ble-join-ended");
        }
    }
#endif
    if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_CONNECTED &&
        s_multiplayer_launch_kind != CONSOLE_MP_LAUNCH_NONE &&
        !multiplayer_transport_connected(
            s_multiplayer_launch_route_id)) {
        if (s_multiplayer_session.role == P4_MP_ROLE_HOST &&
            !s_multiplayer_launch_due &&
            reopen_multiplayer_host_lobby() == ESP_OK) {
            return;
        }
        p4_mp_event_t disconnected;
        (void)p4_mp_session_route_disconnected(
            &s_multiplayer_session,
            s_multiplayer_launch_route_id,
            &disconnected);
        reset_multiplayer_lobby("transport-disconnected");
    }
    if (p4_content_transfer_info().busy ||
        p4_file_transfer_info().busy) {
        return;
    }
    if (!s_multiplayer_transport_ready || shell == NULL) {
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_CONNECTED &&
        !multiplayer_local_content_ready()) {
        reset_multiplayer_lobby("local-game-data-not-ready");
    }
    p4_mp_event_t timeout_event;
    if (p4_mp_session_tick(
            &s_multiplayer_session,
            (uint64_t)now_us / 1000U,
            &timeout_event)) {
        reset_multiplayer_lobby("peer-timeout");
    }
    if(s_multiplayer_lobby_state==CONSOLE_MP_LOBBY_CONNECTED && multiplayer_group_enabled()) {
        uint8_t group_payload[P4_MP_GROUP_BYTES];
        if(p4_mp_group_poll(&s_multiplayer_group_start,(uint64_t)now_us/1000U,group_payload))
            (void)send_session_multiplayer_packet(P4_MP_GROUP_PACKET_TYPE,0,group_payload,sizeof(group_payload));
        if(s_multiplayer_group_start.phase==P4_MP_GROUP_DUE){s_multiplayer_launch_due=true;return;}
        if(s_multiplayer_group_start.phase==P4_MP_GROUP_FAILED){reset_multiplayer_lobby("group-start-timeout");return;}
        if(s_multiplayer_group_start.phase!=P4_MP_GROUP_IDLE)return;
        if(multiplayer_accepting_members() && multiplayer_members()<s_multiplayer_local_offer.player_capacity &&
           (s_multiplayer_next_discovery_us==0||now_us>=s_multiplayer_next_discovery_us)) {
            (void)send_multiplayer_offer();s_multiplayer_next_discovery_us=now_us+500000;
        }
    }
    if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_CONNECTED) {
        const p4_mp_start_state_t start_state =
            p4_mp_start_barrier_poll(
                &s_multiplayer_start_barrier,
                (uint64_t)now_us / UINT64_C(1000));
        if (start_state == P4_MP_START_DUE) {
            s_multiplayer_launch_due = true;
            s_multiplayer_next_start_ready_us = 0;
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS MULTIPLAYER_START_SYNC state=due");
            return;
        }
        if (start_state == P4_MP_START_TIMED_OUT) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS MULTIPLAYER_START_SYNC "
                     "state=timeout action=stay-in-lobby");
            p4_mp_start_barrier_cancel(&s_multiplayer_start_barrier);
    s_multiplayer_group_start=(p4_mp_group_start_t){0};
            s_multiplayer_next_start_ready_us = 0;
        } else if (start_state == P4_MP_START_WAITING ||
                   start_state == P4_MP_START_ARMED) {
            if (s_multiplayer_next_start_ready_us == 0 ||
                now_us >= s_multiplayer_next_start_ready_us) {
                (void)send_multiplayer_start_ready(P4_MP_PACKET_PING);
                s_multiplayer_next_start_ready_us = now_us +
                    (int64_t)CONSOLE_MULTIPLAYER_START_READY_INTERVAL_MS *
                        1000;
            }
            return;
        }
        if (s_multiplayer_next_control_us == 0 ||
            now_us >= s_multiplayer_next_control_us) {
            uint8_t keepalive[8];
            const uint64_t stamp = (uint64_t)now_us / 1000U;
            for (unsigned index = 0U; index < 8U; ++index) {
                keepalive[index] = (uint8_t)(stamp >> (index * 8U));
            }
            (void)send_session_multiplayer_packet(
                P4_MP_PACKET_PING, 0U, keepalive, sizeof(keepalive));
            s_multiplayer_next_control_us = now_us +
                (int64_t)CONSOLE_MULTIPLAYER_KEEPALIVE_INTERVAL_MS * 1000;
        }
        return;
    }
    const bool page_active = shell->page == CONSOLE_PAGE_MULTIPLAYER;
    const bool lobby_active =
        s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_HOSTING ||
        s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_JOINING;
    if (!page_active && !lobby_active &&
        !s_multiplayer_discovery_reply_pending) {
        s_multiplayer_next_discovery_us = 0;
        return;
    }
    if (!s_multiplayer_discovery_reply_pending &&
        s_multiplayer_next_discovery_us != 0 &&
        now_us < s_multiplayer_next_discovery_us) {
        return;
    }
    if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_HOSTING) {
        if (multiplayer_local_content_ready()) {
            (void)send_multiplayer_offer();
        }
        s_multiplayer_discovery_reply_pending = false;
    } else if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_JOINING) {
        if (s_multiplayer_session.role == P4_MP_ROLE_CLIENT) {
            (void)send_multiplayer_join();
        } else {
            (void)send_raw_multiplayer_packet(
                P4_MP_PACKET_DISCOVER, 0U, 0U, NULL, 0U);
        }
    } else if (s_multiplayer_lobby_state == CONSOLE_MP_LOBBY_BROWSING) {
        (void)send_raw_multiplayer_packet(
            P4_MP_PACKET_DISCOVER, 0U, 0U, NULL, 0U);
    }
    s_multiplayer_next_discovery_us = (page_active || lobby_active)
        ? now_us +
            (int64_t)CONSOLE_MULTIPLAYER_DISCOVERY_INTERVAL_MS * 1000
        : 0;
}

#if P4_CONSOLE_GAMEPAD_INPUT
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
    if (s_gamepad_polls != UINT32_MAX) {
        ++s_gamepad_polls;
    }
    const esp_err_t result = platform_gamepad_get_snapshot(snapshot);
    const bool valid = result == ESP_OK &&
        snapshot->version == PLATFORM_GAMEPAD_SNAPSHOT_VERSION &&
        snapshot->size == sizeof(*snapshot) &&
        snapshot->state.version == GAMEPAD_STATE_VERSION &&
        snapshot->state.size == sizeof(snapshot->state);
    if (!valid) {
        s_gamepad_connected = false;
        s_gamepad_transport = PLATFORM_GAMEPAD_TRANSPORT_NONE;
        const bool report_failure =
            result != ESP_ERR_INVALID_STATE &&
            result != ESP_ERR_INVALID_RESPONSE;
        if (report_failure && s_gamepad_poll_failures != UINT32_MAX) {
            ++s_gamepad_poll_failures;
        }
        if (report_failure && (s_gamepad_poll_failures == 1U ||
            s_gamepad_poll_failures % 120U == 0U)) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS GAMEPAD_POLL_FAIL count=%lu error=%s",
                     (unsigned long)s_gamepad_poll_failures,
                     esp_err_to_name(result));
        }
        memset(snapshot, 0, sizeof(*snapshot));
        return false;
    }
    s_gamepad_connected = snapshot->state.connected != 0U;
    s_gamepad_transport = s_gamepad_connected
        ? (platform_gamepad_transport_t)snapshot->identity.transport
        : PLATFORM_GAMEPAD_TRANSPORT_NONE;
    return s_gamepad_connected;
}

static bool single_controller_button(uint64_t buttons, uint8_t *source)
{
    if (source == NULL || buttons == 0U ||
        (buttons & (buttons - UINT64_C(1))) != 0U) {
        return false;
    }
    for (size_t index = 0U; index < GAMEPAD_BUTTON_COUNT; ++index) {
        if ((buttons & GAMEPAD_BUTTON_MASK(index)) != 0U) {
            *source = (uint8_t)index;
            return true;
        }
    }
    return false;
}

static void poll_controller_mapping_capture(void)
{
    if (!s_controller_mapping_active &&
        !s_controller_mapping_input_suppressed) {
        return;
    }

    platform_gamepad_snapshot_t raw;
    memset(&raw, 0, sizeof(raw));
    const esp_err_t snapshot_result =
        platform_gamepad_get_raw_snapshot(&raw);
    const bool connected = snapshot_result == ESP_OK &&
        raw.version == PLATFORM_GAMEPAD_SNAPSHOT_VERSION &&
        raw.size == sizeof(raw) &&
        raw.state.version == GAMEPAD_STATE_VERSION &&
        raw.state.size == sizeof(raw.state) &&
        raw.state.connected != 0U;
    if (!connected) {
        if (s_controller_mapping_active) {
            s_controller_mapping_last_error = snapshot_result == ESP_OK
                ? ESP_ERR_INVALID_STATE : snapshot_result;
        } else {
            s_controller_mapping_input_suppressed = false;
        }
        return;
    }

    if (raw.state.buttons == 0U) {
        s_controller_mapping_input_suppressed = false;
        if (s_controller_mapping_active) {
            s_controller_mapping_wait_neutral = false;
            s_controller_mapping_last_error = ESP_OK;
        }
        return;
    }

    s_controller_mapping_input_suppressed = true;
    if (!s_controller_mapping_active ||
        s_controller_mapping_wait_neutral) {
        return;
    }

    uint8_t source = 0U;
    if (!single_controller_button(raw.state.buttons, &source)) {
        s_controller_mapping_last_error = ESP_ERR_INVALID_ARG;
        s_controller_mapping_wait_neutral = true;
        return;
    }
    for (size_t slot = 0U; slot < s_controller_mapping_target; ++slot) {
        if (s_controller_mapping_pending.source[slot] == source) {
            s_controller_mapping_last_error = ESP_ERR_INVALID_STATE;
            s_controller_mapping_wait_neutral = true;
            return;
        }
    }

    s_controller_mapping_pending.source[s_controller_mapping_target] =
        source;
    ++s_controller_mapping_target;
    s_controller_mapping_wait_neutral = true;
    s_controller_mapping_last_error = ESP_OK;
    if (s_controller_mapping_target < GAMEPAD_MAPPING_COUNT) {
        return;
    }

    s_controller_mapping_active = false;
    const esp_err_t result = apply_and_persist_controller_mapping(
        &s_controller_mapping_pending);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS CONTROLLER_MAPPING_COMPLETE "
             "result=%s persistent=%u",
             esp_err_to_name(result),
             s_controller_mapping_persistent ? 1U : 0U);
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

#endif

static void confirm_ota_after_stable_runtime(void)
{
    if (s_ota_validation_attempted || s_runtime_services_ready_us <= 0 ||
        esp_timer_get_time() - s_runtime_services_ready_us <
            (int64_t)CONSOLE_RUNTIME_HEALTH_CONFIRM_MS * INT64_C(1000)) {
        return;
    }
    s_ota_validation_attempted = true;
    bool ota_was_pending = false;
    const esp_err_t ota_valid = platform_os_update_mark_running_valid(
        &ota_was_pending);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS OTA_BOOT_VALID result=%s was_pending=%u "
             "stable_ms=%u services=initialized",
             esp_err_to_name(ota_valid), ota_was_pending ? 1U : 0U,
             (unsigned)CONSOLE_RUNTIME_HEALTH_CONFIRM_MS);
}

#if P4_CONSOLE_USB_INPUT

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
    s_keyboard_connected = false;
    s_mouse_connected = false;
    s_mouse_pointer_active = false;
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
        s_runtime_services_ready_us <= 0 ||
        esp_timer_get_time() - s_runtime_services_ready_us <
            (int64_t)CONSOLE_USB_ENUM_GUARD_CONFIRM_MS * INT64_C(1000)) {
        return;
    }
    s_usb_enum_probe_confirm_attempted = true;
    const esp_err_t result =
        platform_console_settings_confirm_usb_enum_probe(
            &s_console_settings);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS USB_ENUM_GUARD state=%s stable_ms=%u "
             "loops=%lu result=%s",
             result == ESP_OK ? "confirmed" : "confirm-failed",
             (unsigned)CONSOLE_USB_ENUM_GUARD_CONFIRM_MS,
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
#if CONFIG_P4_BOARD_M5STACK_TAB5 && CONFIG_P4_TAB5_USB_HOST
    result = platform_gamepad_xusb_start();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "P4_CONSOLE_OS USB_INPUT_DEGRADED stage=xusb error=%s",
                 esp_err_to_name(result));
        if (platform_usb_host_quiesce() == ESP_OK) {
            (void)platform_gamepad_xusb_stop(pdMS_TO_TICKS(1000U));
            (void)platform_gamepad_usb_stop(pdMS_TO_TICKS(1000U));
            (void)platform_usb_host_stop(pdMS_TO_TICKS(1000U));
        }
        neutralize_usb_input_state();
        return result;
    }
#endif
    result = platform_usb_host_enable_root_port();
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS USB_INPUT_DEGRADED stage=root-port error=%s",
                 esp_err_to_name(result));
        if (platform_usb_host_quiesce() == ESP_OK) {
#if CONFIG_P4_BOARD_M5STACK_TAB5 && CONFIG_P4_TAB5_USB_HOST
            (void)platform_gamepad_xusb_stop(pdMS_TO_TICKS(1000U));
#endif
            (void)platform_gamepad_usb_stop(pdMS_TO_TICKS(1000U));
            (void)platform_usb_host_stop(pdMS_TO_TICKS(1000U));
        }
        neutralize_usb_input_state();
        return result;
    }
    s_gamepad_ready = true;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS USB_INPUT_READY "
             "classes=gamepad transport=usb topology=%s "
             "firmware_vbus_source=%u",
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
             "integrated-powered-hub", 0U
#elif CONFIG_P4_BOARD_M5STACK_TAB5
             "tab5-usb-a", 1U
#else
             "waveshare-h2-self-powered-test", 0U
#endif
    );
    return ESP_OK;
}

#if P4_CONSOLE_H1_USB_DRIVE_CONTROL
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

#endif /* P4_CONSOLE_H1_USB_DRIVE_CONTROL */

static void create_usb_input_or_continue(void)
{
    (void)start_usb_input();
}

static console_shell_action_t poll_usb_input(
    console_shell_t *shell, bool *pointer_owned, uint32_t controller_buttons)
{
    if (pointer_owned != NULL) {
        *pointer_owned = false;
    }
    uint32_t buttons = controller_buttons;
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
        s_mouse_pointer_active = true;
        if (pointer_owned != NULL) {
            *pointer_owned = true;
        }
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
#if CONFIG_P4_BOARD_M5STACK_TAB5
            .x = (uint16_t)((uint32_t)s_mouse_x * 1280U / CONSOLE_SHELL_LAYOUT_WIDTH),
            .y = (uint16_t)((uint32_t)s_mouse_y * 720U / CONSOLE_SHELL_LAYOUT_HEIGHT),
#else
            .x = (uint16_t)((uint32_t)CONSOLE_SHELL_VIEWPORT_LEFT +
                (uint32_t)s_mouse_x *
                    (uint32_t)CONSOLE_SHELL_VIEWPORT_WIDTH /
                    (uint32_t)CONSOLE_SHELL_LAYOUT_WIDTH),
            .y = (uint16_t)((uint32_t)CONSOLE_SHELL_VIEWPORT_TOP +
                (uint32_t)s_mouse_y *
                    (uint32_t)CONSOLE_SHELL_VIEWPORT_HEIGHT /
                    (uint32_t)CONSOLE_SHELL_LAYOUT_HEIGHT),
#endif
        };
        pointer_action = console_shell_handle_touch(
            shell, true, left_down ? &contact : NULL,
            left_down ? 1U : 0U);
    } else {
        console_shell_set_pointer(shell, false, 0U, 0U, false);
        if (s_mouse_pointer_active) {
            /* Release a departing mouse once, then return touch ownership. */
            s_mouse_pointer_active = false;
            if (pointer_owned != NULL) {
                *pointer_owned = true;
            }
            pointer_action = console_shell_handle_touch(
                shell, true, NULL, 0U);
        }
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
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    s_gt911_reviewed_baseline_verified = false;
    platform_touch_gt911_info_t gt911_info;
    const esp_err_t gt911_result = platform_touch_gt911_read_info(
        s_touch, &gt911_info);
    if (gt911_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS GT911_CONFIG mode=read-only result=error error=%s",
                 esp_err_to_name(gt911_result));
        log_waveshare_gt911_restore("error", NULL, gt911_result);
        /* A restoration attempt without a read-back baseline is unsafe. */
        s_touch_ready = false;
    } else {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS GT911_CONFIG mode=read-only "
                 "product=%02x%02x%02x%02x firmware=0x%04x "
                 "identity_resolution=%ux%u config_version=%u "
                 "config_resolution=%ux%u max_points=%u "
                 "module_switch_1=0x%02x module_switch_2=0x%02x "
                 "shake_raw=0x%02x filter_raw=0x%02x first_filter=%u "
                 "normal_filter=%u large_touch=0x%02x noise_reduction=0x%02x "
                 "screen_touch_level=0x%02x screen_release_level=0x%02x "
                 "low_power_control=0x%02x refresh_raw=0x%02x refresh_n=%u "
                 "report_period_ms=%u x_threshold=%u y_threshold=%u "
                 "mini_filter=0x%02x checksum=0x%02x "
                 "checksum_calculated=0x%02x checksum_valid=%u fresh=%u",
                 (unsigned)gt911_info.product_id[0],
                 (unsigned)gt911_info.product_id[1],
                 (unsigned)gt911_info.product_id[2],
                 (unsigned)gt911_info.product_id[3],
                 (unsigned)gt911_info.firmware_version,
                 (unsigned)gt911_info.identity_x_resolution,
                 (unsigned)gt911_info.identity_y_resolution,
                 (unsigned)gt911_info.config_version,
                 (unsigned)gt911_info.config_x_resolution,
                 (unsigned)gt911_info.config_y_resolution,
                 (unsigned)gt911_info.max_touch_points,
                 (unsigned)gt911_info.module_switch_1,
                 (unsigned)gt911_info.module_switch_2,
                 (unsigned)gt911_info.shake_count,
                 (unsigned)gt911_info.filter,
                 (unsigned)gt911_info.first_filter,
                 (unsigned)gt911_info.normal_filter,
                 (unsigned)gt911_info.large_touch,
                 (unsigned)gt911_info.noise_reduction,
                 (unsigned)gt911_info.screen_touch_level,
                 (unsigned)gt911_info.screen_release_level,
                 (unsigned)gt911_info.low_power_control,
                 (unsigned)gt911_info.refresh_rate,
                 (unsigned)gt911_info.refresh_n,
                 (unsigned)gt911_info.report_period_ms,
                 (unsigned)gt911_info.x_threshold,
                 (unsigned)gt911_info.y_threshold,
                 (unsigned)gt911_info.mini_filter,
                 (unsigned)gt911_info.config_checksum,
                 (unsigned)gt911_info.config_checksum_calculated,
                 gt911_info.config_checksum_valid ? 1U : 0U,
                 gt911_info.config_fresh ? 1U : 0U);
        for (size_t offset = 0U;
             offset < PLATFORM_TOUCH_GT911_CONFIG_BYTES;
             offset += 32U) {
            const size_t length =
                (PLATFORM_TOUCH_GT911_CONFIG_BYTES - offset) < 32U
                    ? PLATFORM_TOUCH_GT911_CONFIG_BYTES - offset : 32U;
            char hex[65];
            for (size_t index = 0U; index < length; ++index) {
                (void)snprintf(&hex[index * 2U], 3U, "%02x",
                               (unsigned)gt911_info.config[offset + index]);
            }
            hex[length * 2U] = '\0';
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS GT911_CONFIG_HEX mode=read-only "
                     "offset=%u length=%u data=%s",
                     (unsigned)offset, (unsigned)length, hex);
        }

        platform_touch_gt911_restore_reviewed_baseline_request_t request = {
            0,
        };
        memcpy(request.expected_identity,
               s_waveshare_gt911_expected_identity,
               sizeof(request.expected_identity));
        memcpy(request.original_config,
               s_waveshare_gt911_original_config,
               sizeof(request.original_config));
        platform_touch_gt911_restore_reviewed_baseline_result_t
            restore_result;
        const esp_err_t restore_status =
            platform_touch_gt911_restore_reviewed_baseline(
                s_touch, &request, &restore_result);
        const bool restore_verified = restore_status == ESP_OK &&
            (restore_result.changed || restore_result.already_original) &&
            restore_result.observed.config_checksum_valid &&
            restore_result.observed.normal_filter ==
                CONSOLE_GT911_ORIGINAL_NORMAL_FILTER;
        if (!restore_verified) {
            log_waveshare_gt911_restore(
                "error", &restore_result,
                restore_status == ESP_OK
                    ? ESP_ERR_INVALID_RESPONSE : restore_status);
            /* The OS still boots, but no task may consume touch until the
             * complete reviewed baseline has verified. A later boot can
             * safely re-evaluate either exact accepted controller state. */
            s_touch_ready = false;
        } else {
            s_gt911_reviewed_baseline_verified = true;
            log_waveshare_gt911_restore(
                restore_result.already_original
                    ? "already-original" : "restored",
                &restore_result, ESP_OK);
        }
    }
#endif
    if (s_touch_ready) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS TOUCH_READY controller=gt911 contacts_max=%u",
                 (unsigned)PLATFORM_TOUCH_MAX_CONTACTS);
    }
}

static void saturating_atomic_increment_u32(uint32_t *value)
{
    uint32_t observed = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (observed != UINT32_MAX &&
           !__atomic_compare_exchange_n(
               value, &observed, observed + 1U, false,
               __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void saturating_atomic_add_u32(uint32_t *value, uint32_t amount)
{
    uint32_t observed = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (observed != UINT32_MAX) {
        const uint32_t updated = UINT32_MAX - observed < amount
            ? UINT32_MAX : observed + amount;
        if (__atomic_compare_exchange_n(
                value, &observed, updated, false,
                __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            return;
        }
    }
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
    saturating_atomic_increment_u32(&s_touch_polls);
    const esp_err_t result = platform_touch_poll(s_touch, frame);
    if (result != ESP_OK || frame->valid == 0U ||
        frame->contact_count > PLATFORM_TOUCH_MAX_CONTACTS) {
        saturating_atomic_increment_u32(&s_touch_poll_failures);
        const uint32_t failures = __atomic_load_n(
            &s_touch_poll_failures, __ATOMIC_RELAXED);
        if (failures == 1U || failures % 120U == 0U) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS TOUCH_POLL_FAIL count=%lu error=%s",
                     (unsigned long)failures,
                     esp_err_to_name(result));
        }
        platform_touch_frame_neutral(frame);
        return false;
    }
    return true;
}

#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
static void update_touch_mailbox_age(uint32_t age_us)
{
    __atomic_store_n(&s_touch_mailbox_age_last_us, age_us,
                     __ATOMIC_RELAXED);
    uint32_t observed = __atomic_load_n(
        &s_touch_mailbox_age_max_us, __ATOMIC_RELAXED);
    while (age_us > observed &&
           !__atomic_compare_exchange_n(
               &s_touch_mailbox_age_max_us, &observed, age_us, false,
               __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void publish_touch_mailbox_frame(const platform_touch_frame_t *frame,
                                        bool successful)
{
    if (frame == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_touch_mailbox_lock);
    s_touch_mailbox_frame = *frame;
    taskEXIT_CRITICAL(&s_touch_mailbox_lock);
    saturating_atomic_increment_u32(&s_touch_mailbox_samples);
    if (!successful) {
        saturating_atomic_increment_u32(&s_touch_mailbox_failures);
        s_touch_mailbox_last_report_timestamp_us = 0;
    } else if (frame->valid != 0U && frame->contact_count > 0U &&
               frame->timestamp_us > 0) {
        const int64_t timestamp_us = frame->timestamp_us;
        const int64_t previous_timestamp_us =
            s_touch_mailbox_last_report_timestamp_us;
        if (timestamp_us != previous_timestamp_us) {
            saturating_atomic_increment_u32(&s_touch_mailbox_unique_reports);
            if (previous_timestamp_us > 0U &&
                timestamp_us > previous_timestamp_us) {
                const uint64_t elapsed_us = (uint64_t)(timestamp_us -
                    previous_timestamp_us);
                const uint32_t interval_us = elapsed_us > UINT32_MAX
                    ? UINT32_MAX : (uint32_t)elapsed_us;
                saturating_atomic_increment_u32(
                    &s_touch_mailbox_report_interval_samples);
                saturating_atomic_add_u32(
                    &s_touch_mailbox_report_interval_total_us, interval_us);
                uint32_t minimum = __atomic_load_n(
                    &s_touch_mailbox_report_interval_min_us,
                    __ATOMIC_RELAXED);
                while ((minimum == 0U || interval_us < minimum) &&
                       !__atomic_compare_exchange_n(
                           &s_touch_mailbox_report_interval_min_us,
                           &minimum, interval_us, false,
                           __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                }
                uint32_t maximum = __atomic_load_n(
                    &s_touch_mailbox_report_interval_max_us,
                    __ATOMIC_RELAXED);
                while (interval_us > maximum &&
                       !__atomic_compare_exchange_n(
                           &s_touch_mailbox_report_interval_max_us,
                           &maximum, interval_us, false,
                           __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
                }
            }
            s_touch_mailbox_last_report_timestamp_us = timestamp_us;
        }
    } else if (successful && frame->valid != 0U &&
               frame->contact_count == 0U) {
        /* Neutral samples intentionally have timestamp zero. */
        s_touch_mailbox_last_report_timestamp_us = 0;
    }
}

static bool read_touch_mailbox_frame(platform_touch_frame_t *frame)
{
    if (frame == NULL) {
        return false;
    }
    taskENTER_CRITICAL(&s_touch_mailbox_lock);
    const platform_touch_frame_t snapshot = s_touch_mailbox_frame;
    taskEXIT_CRITICAL(&s_touch_mailbox_lock);
    uint32_t age_us = 0U;
    const int64_t now_us = esp_timer_get_time();
    const bool contact_frame = snapshot.valid != 0U &&
        snapshot.contact_count > 0U;
    if (contact_frame) {
        if (snapshot.timestamp_us <= 0) {
            /* A contact without provenance is unsafe to retain. */
            age_us = UINT32_MAX;
        } else if (now_us > snapshot.timestamp_us) {
            const uint64_t elapsed = (uint64_t)(now_us -
                snapshot.timestamp_us);
            age_us = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
        }
    }
    update_touch_mailbox_age(age_us);
    if (contact_frame && age_us > CONSOLE_TOUCH_MAILBOX_MAX_AGE_US) {
        saturating_atomic_increment_u32(&s_touch_mailbox_stale_reads);
        platform_touch_frame_neutral(frame);
        return false;
    }
    *frame = snapshot;
    if (snapshot.valid == 0U ||
        snapshot.contact_count > PLATFORM_TOUCH_MAX_CONTACTS) {
        platform_touch_frame_neutral(frame);
        return false;
    }
    return true;
}

static void touch_mailbox_worker(void *context)
{
    (void)context;
    p4_tick_scheduler_t scheduler;
    const bool scheduler_ready = p4_tick_scheduler_init(
        &scheduler, configTICK_RATE_HZ, CONSOLE_TOUCH_MAILBOX_HZ) ==
        P4_SCHEDULER_OK;
    TickType_t last_wake = xTaskGetTickCount();
    while (!__atomic_load_n(&s_touch_mailbox_stop_requested,
                            __ATOMIC_ACQUIRE)) {
        platform_touch_frame_t frame;
        const bool successful = read_touch_frame(&frame);
        publish_touch_mailbox_frame(&frame, successful);
        uint32_t interval_ticks = 0U;
        if (!scheduler_ready || p4_tick_scheduler_next(
                &scheduler, &interval_ticks) != P4_SCHEDULER_OK ||
            interval_ticks == 0U) {
            vTaskDelay(1U);
            last_wake = xTaskGetTickCount();
        } else {
            vTaskDelayUntil(&last_wake, (TickType_t)interval_ticks);
        }
    }
    taskENTER_CRITICAL(&s_touch_mailbox_lock);
    s_touch_mailbox_task = NULL;
    taskEXIT_CRITICAL(&s_touch_mailbox_lock);
    if (s_touch_mailbox_stopped != NULL) {
        (void)xSemaphoreGive(s_touch_mailbox_stopped);
    }
    vTaskDelete(NULL);
}

static esp_err_t start_touch_mailbox(void)
{
    if (!s_touch_ready || s_touch == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    taskENTER_CRITICAL(&s_touch_mailbox_lock);
    const bool already_running = s_touch_mailbox_task != NULL;
    const bool stopping = __atomic_load_n(
        &s_touch_mailbox_stop_requested, __ATOMIC_ACQUIRE);
    taskEXIT_CRITICAL(&s_touch_mailbox_lock);
    if (already_running) {
        return stopping ? ESP_ERR_INVALID_STATE : ESP_OK;
    }
    if (s_touch_mailbox_stopped == NULL) {
        s_touch_mailbox_stopped = xSemaphoreCreateBinaryStatic(
            &s_touch_mailbox_stopped_storage);
        if (s_touch_mailbox_stopped == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    while (xSemaphoreTake(s_touch_mailbox_stopped, 0U) == pdTRUE) {
    }
    taskENTER_CRITICAL(&s_touch_mailbox_lock);
    platform_touch_frame_neutral(&s_touch_mailbox_frame);
    s_touch_mailbox_last_report_timestamp_us = 0;
    taskEXIT_CRITICAL(&s_touch_mailbox_lock);
    __atomic_store_n(&s_touch_mailbox_stop_requested, false,
                     __ATOMIC_RELEASE);
    TaskHandle_t worker = NULL;
    const BaseType_t created = xTaskCreate(
        touch_mailbox_worker, "touch_mailbox",
        CONSOLE_TOUCH_MAILBOX_STACK_BYTES, NULL, tskIDLE_PRIORITY + 1U,
        &worker);
    if (created != pdPASS || worker == NULL) {
        __atomic_store_n(&s_touch_mailbox_stop_requested, true,
                         __ATOMIC_RELEASE);
        return ESP_ERR_NO_MEM;
    }
    taskENTER_CRITICAL(&s_touch_mailbox_lock);
    s_touch_mailbox_task = worker;
    taskEXIT_CRITICAL(&s_touch_mailbox_lock);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS TOUCH_MAILBOX_READY sample_hz=%u mode=latest",
             (unsigned)CONSOLE_TOUCH_MAILBOX_HZ);
    return ESP_OK;
}

static esp_err_t stop_touch_mailbox(void)
{
    taskENTER_CRITICAL(&s_touch_mailbox_lock);
    const TaskHandle_t worker = s_touch_mailbox_task;
    if (worker != NULL) {
        __atomic_store_n(&s_touch_mailbox_stop_requested, true,
                         __ATOMIC_RELEASE);
    }
    taskEXIT_CRITICAL(&s_touch_mailbox_lock);
    if (worker == NULL) {
        return ESP_OK;
    }
    if (s_touch_mailbox_stopped == NULL ||
        xSemaphoreTake(s_touch_mailbox_stopped,
                       pdMS_TO_TICKS(CONSOLE_TOUCH_MAILBOX_STOP_TIMEOUT_MS))
            != pdTRUE) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS TOUCH_MAILBOX_STOP_TIMEOUT timeout_ms=%u",
                 (unsigned)CONSOLE_TOUCH_MAILBOX_STOP_TIMEOUT_MS);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static bool touch_mailbox_running(void)
{
    bool running;
    taskENTER_CRITICAL(&s_touch_mailbox_lock);
    running = s_touch_mailbox_task != NULL;
    taskEXIT_CRITICAL(&s_touch_mailbox_lock);
    return running;
}

static void note_interactive_touch_sample(
    const console_shell_t *shell,
    const console_shell_action_t *action,
    int64_t timestamp_us,
    int32_t scroll_visual_before_q16,
    size_t scroll_row_before,
    size_t pressed_index_before,
    bool press_active_before,
    bool dirty_before)
{
    if (shell == NULL || action == NULL ||
        timestamp_us <= 0) {
        s_interactive_touch_pending_timestamp_us = 0;
        return;
    }
    /* A held contact produces repeated GT911 frames.  Attribute only a
     * sample which actually moved the launcher, changed a touch visual, or
     * returned a shell action; this avoids idle/repeated-coordinate noise. */
    const bool correlated = action->type != CONSOLE_ACTION_NONE ||
        scroll_visual_before_q16 != shell->home_scroll_visual_q16 ||
        scroll_row_before != shell->home_scroll_row ||
        press_active_before != shell->press_active ||
        pressed_index_before != shell->pressed_index ||
        dirty_before != shell->dirty;
    s_interactive_touch_pending_timestamp_us = correlated
        ? timestamp_us : 0;
}
#endif

static console_shell_action_t poll_touch_input(console_shell_t *shell)
{
    platform_touch_frame_t frame;
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    s_interactive_touch_pending_timestamp_us = 0;
    if (touch_mailbox_running()) {
        if (!read_touch_mailbox_frame(&frame)) {
            return console_shell_handle_touch(shell, false, NULL, 0U);
        }
    } else
#endif
    if (!read_touch_frame(&frame)) {
        return console_shell_handle_touch(shell, false, NULL, 0U);
    }
    console_shell_contact_t contacts[CONSOLE_SHELL_MAX_CONTACTS];
    const size_t count = frame.contact_count;
    for (size_t i = 0U; i < count; ++i) {
        contacts[i].x = frame.contacts[i].x;
        contacts[i].y = frame.contacts[i].y;
    }
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    const int32_t scroll_visual_before_q16 = shell->home_scroll_visual_q16;
    const size_t scroll_row_before = shell->home_scroll_row;
    const size_t pressed_index_before = shell->pressed_index;
    const bool press_active_before = shell->press_active;
    const bool dirty_before = shell->dirty;
    const console_shell_action_t action = console_shell_handle_touch(
        shell, true, count == 0U ? NULL : contacts, count);
    note_interactive_touch_sample(
        shell, &action, frame.timestamp_us, scroll_visual_before_q16,
        scroll_row_before, pressed_index_before, press_active_before,
        dirty_before);
    return action;
#else
    return console_shell_handle_touch(
        shell, true, count == 0U ? NULL : contacts, count);
#endif
}
#endif

static console_shell_action_t poll_input(console_shell_t *shell)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    /* A USB-owned early return has no following touch-originated present. */
    s_interactive_touch_pending_timestamp_us = 0;
#endif
#if P4_CONSOLE_GAMEPAD_INPUT
    platform_gamepad_snapshot_t gamepad;
    uint32_t controller_buttons = read_gamepad_snapshot(&gamepad)
        ? gamepad_shell_buttons(&gamepad.state) : 0U;
    poll_controller_mapping_capture();
    if (s_controller_mapping_active ||
        s_controller_mapping_input_suppressed) {
        controller_buttons = 0U;
    }
#endif
#if P4_CONSOLE_USB_INPUT
    bool usb_pointer_owned = false;
    const console_shell_action_t usb_action =
        poll_usb_input(shell, &usb_pointer_owned, controller_buttons);
    if (usb_action.type != CONSOLE_ACTION_NONE) {
        return usb_action;
    }
    if (usb_pointer_owned) {
        const console_shell_action_t no_action = {0};
        return no_action;
    }
#elif P4_CONSOLE_GAMEPAD_INPUT
    const console_shell_action_t controller_action =
        console_shell_handle_buttons(shell, controller_buttons);
    if (controller_action.type != CONSOLE_ACTION_NONE) {
        return controller_action;
    }
#endif
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    return poll_touch_input(shell);
#else
    const console_shell_action_t no_action = {0};
    return no_action;
#endif
}

static void log_memory_health(const char *stage)
{
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MEMORY stage=%s main_stack_low_water_bytes=%u "
             "internal_free=%u internal_min=%u internal_largest=%u "
             "dma_free=%u dma_largest=%u psram_free=%u psram_min=%u "
             "psram_largest=%u",
             stage == NULL ? "unknown" : stage,
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)heap_caps_get_free_size(internal_caps),
             (unsigned)heap_caps_get_minimum_free_size(internal_caps),
             (unsigned)heap_caps_get_largest_free_block(internal_caps),
             (unsigned)heap_caps_get_free_size(dma_caps),
             (unsigned)heap_caps_get_largest_free_block(dma_caps),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

#if CONSOLE_OS_ENABLE_RUNTIME_STATS
static uint32_t telemetry_average_us(uint32_t total_us, uint32_t samples)
{
    return samples == 0U ? 0U : total_us / samples;
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
             "display_failures=%lu display_accelerated=%lu "
             "display_accel_failures=%lu "
             "audio=es8311-ready storage=%s "
             "storage_generation=%lu storage_media=microsd",
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
             (unsigned long)display.accelerated_submits,
             (unsigned long)display.accelerator_failures,
             platform_game_storage_state_name(s_game_storage_status.state),
             (unsigned long)s_game_storage_status.generation);
#else
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS STATS loops=%lu page=%u renders=%lu "
             "touch_ready=%u touch_polls=%lu touch_failures=%lu "
             "display_submits=%lu display_completions=%lu "
             "display_timeouts=%lu display_failures=%lu "
             "display_accelerated=%lu display_accel_failures=%lu "
             "amp_energized=0 doom_handoffs=%lu storage=%s "
             "storage_generation=%lu usb_attached=%u",
             (unsigned long)s_loop_count,
             (unsigned)shell->page,
             (unsigned long)shell->render_generation,
             s_touch_ready ? 1U : 0U,
             (unsigned long)__atomic_load_n(
                 &s_touch_polls, __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(
                 &s_touch_poll_failures, __ATOMIC_RELAXED),
             (unsigned long)display.submits_started,
             (unsigned long)display.submits_completed,
             (unsigned long)display.submit_timeouts,
             (unsigned long)display.submit_failures,
             (unsigned long)display.accelerated_submits,
             (unsigned long)display.accelerator_failures,
             (unsigned long)s_doom_handoff_count,
             platform_game_storage_state_name(s_game_storage_status.state),
             (unsigned long)s_game_storage_status.generation,
             s_game_storage_status.usb_attached ? 1U : 0U);
#endif
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    const uint32_t report_interval_samples = __atomic_load_n(
        &s_touch_mailbox_report_interval_samples, __ATOMIC_RELAXED);
    const uint32_t report_interval_total_us = __atomic_load_n(
        &s_touch_mailbox_report_interval_total_us, __ATOMIC_RELAXED);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS TOUCH_MAILBOX running=%u samples=%lu "
             "failures=%lu age_us=%lu age_max_us=%lu stale=%lu "
             "unique_reports=%lu report_interval_avg_us=%lu "
             "report_interval_min_us=%lu report_interval_max_us=%lu",
             touch_mailbox_running() ? 1U : 0U,
             (unsigned long)__atomic_load_n(
                 &s_touch_mailbox_samples, __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(
                 &s_touch_mailbox_failures, __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(
                 &s_touch_mailbox_age_last_us, __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(
                 &s_touch_mailbox_age_max_us, __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(
                 &s_touch_mailbox_stale_reads, __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(
                 &s_touch_mailbox_unique_reports, __ATOMIC_RELAXED),
             (unsigned long)telemetry_average_us(
                 report_interval_total_us, report_interval_samples),
             (unsigned long)__atomic_load_n(
                 &s_touch_mailbox_report_interval_min_us, __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(
                 &s_touch_mailbox_report_interval_max_us, __ATOMIC_RELAXED));
#endif
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS UI_TIMING dirty_frames=%lu "
             "animation_frames=%lu dirty_interval_us=%lu "
             "dirty_interval_max_us=%lu render_us=%lu render_max_us=%lu "
             "submit_us=%lu submit_max_us=%lu "
             "animation_interval_us=%lu animation_interval_max_us=%lu "
             "animation_missed=%lu animation_burst_frames=%lu "
             "animation_burst_avg_us=%lu "
             "reuse_wait_us=%lu reuse_wait_max_us=%lu "
             "transform_us=%lu transform_max_us=%lu "
             "handoff_us=%lu handoff_max_us=%lu reserved_refreshes=%lu "
             "refresh_interval_us=%lu refresh_interval_min_us=%lu "
             "refresh_interval_max_us=%lu refresh_events=%lu "
             "partial_submits=%lu partial_pixels=%lu "
             "partial_full_fallbacks=%lu "
             "interactive_samples=%lu "
             "interactive_input_refresh_avg_us=%lu "
             "interactive_input_refresh_max_us=%lu "
             "interactive_input_refresh_last_us=%lu "
             "interactive_handoff_refresh_avg_us=%lu "
             "interactive_handoff_refresh_max_us=%lu "
             "interactive_handoff_refresh_last_us=%lu "
             "interactive_partial=%lu interactive_full=%lu "
             "interactive_kind=%u interactive_replay_regions=%u "
             "shell_full_frames=%lu shell_dynamic_frames=%lu "
             "shell_scroll_blits=%lu shell_shifted_pixels=%" PRIu64,
             (unsigned long)s_ui_timing_dirty_frames,
             (unsigned long)s_ui_timing_animation_frames,
             (unsigned long)s_ui_timing_last_dirty_interval_us,
             (unsigned long)s_ui_timing_max_dirty_interval_us,
             (unsigned long)s_ui_timing_last_render_us,
             (unsigned long)s_ui_timing_max_render_us,
             (unsigned long)s_ui_timing_last_submit_us,
             (unsigned long)s_ui_timing_max_submit_us,
             (unsigned long)s_ui_timing_animation_interval_last_us,
             (unsigned long)s_ui_timing_animation_interval_max_us,
             (unsigned long)s_ui_timing_animation_missed_frames,
             (unsigned long)s_ui_timing_animation_burst_frames,
             (unsigned long)(s_ui_timing_animation_burst_frames <= 1U
                 ? 0U : s_ui_timing_animation_burst_interval_sum_us /
                     (s_ui_timing_animation_burst_frames - 1U)),
             (unsigned long)display.pipeline_reuse_wait_last_us,
             (unsigned long)display.pipeline_reuse_wait_max_us,
             (unsigned long)display.pipeline_transform_last_us,
             (unsigned long)display.pipeline_transform_max_us,
             (unsigned long)display.pipeline_handoff_last_us,
             (unsigned long)display.pipeline_handoff_max_us,
             (unsigned long)display.pipeline_reserved_refreshes,
             (unsigned long)display.pipeline_refresh_interval_last_us,
             (unsigned long)display.pipeline_refresh_interval_min_us,
             (unsigned long)display.pipeline_refresh_interval_max_us,
             (unsigned long)display.pipeline_refresh_events,
             (unsigned long)display.partial_content_submits,
             (unsigned long)display.partial_content_source_pixels,
             (unsigned long)display.partial_content_full_fallbacks,
             (unsigned long)display.interactive_latency_samples,
             (unsigned long)telemetry_average_us(
                 display.interactive_input_to_refresh_total_us,
                 display.interactive_latency_samples),
             (unsigned long)display.interactive_input_to_refresh_max_us,
             (unsigned long)display.interactive_input_to_refresh_last_us,
             (unsigned long)telemetry_average_us(
                 display.interactive_handoff_to_refresh_total_us,
                 display.interactive_latency_samples),
             (unsigned long)display.interactive_handoff_to_refresh_max_us,
             (unsigned long)display.interactive_handoff_to_refresh_last_us,
             (unsigned long)display.interactive_partial_presentations,
             (unsigned long)display.interactive_full_presentations,
             (unsigned)display.interactive_present_kind,
             (unsigned)display.interactive_replay_region_count,
             (unsigned long)shell->native_home_full_frames,
             (unsigned long)shell->native_home_dynamic_frames,
             (unsigned long)shell->native_home_scroll_blit_frames,
             shell->native_home_shifted_pixels);
    log_memory_health("periodic");
}
#endif

static void wait_for_console_tick(p4_tick_scheduler_t *scheduler,
                                  TickType_t *last_wake)
{
    uint32_t interval_ticks = 0U;
    if (p4_tick_scheduler_next(scheduler, &interval_ticks) !=
            P4_SCHEDULER_OK || interval_ticks == 0U) {
        vTaskDelay(1U);
        *last_wake = xTaskGetTickCount();
        return;
    }
    /* Do not let an over-budget frame create a catch-up burst.  Keep the
     * scheduler's fractional interval phase, but restart the FreeRTOS wake
     * anchor from now whenever the requested deadline was already missed. */
    if (xTaskDelayUntil(last_wake, (TickType_t)interval_ticks) != pdTRUE) {
        *last_wake = xTaskGetTickCount();
    }
}

static void note_animation_frame(int64_t timestamp_us)
{
    enum {
        UI_ANIMATION_BURST_IDLE_US = 250000,
        UI_ANIMATION_MISSED_FRAME_US = 25000,
    };
    if (s_ui_timing_last_animation_us <= 0 ||
        timestamp_us <= s_ui_timing_last_animation_us ||
        timestamp_us - s_ui_timing_last_animation_us >
            UI_ANIMATION_BURST_IDLE_US) {
        s_ui_timing_animation_burst_frames = 1U;
        s_ui_timing_animation_burst_interval_sum_us = 0U;
        s_ui_timing_animation_missed_frames = 0U;
        s_ui_timing_animation_interval_last_us = 0U;
        s_ui_timing_animation_interval_max_us = 0U;
    } else {
        const uint64_t interval_us = (uint64_t)(
            timestamp_us - s_ui_timing_last_animation_us);
        s_ui_timing_animation_interval_last_us = interval_us > UINT32_MAX
            ? UINT32_MAX : (uint32_t)interval_us;
        if (s_ui_timing_animation_interval_last_us >
            s_ui_timing_animation_interval_max_us) {
            s_ui_timing_animation_interval_max_us =
                s_ui_timing_animation_interval_last_us;
        }
        if (s_ui_timing_animation_burst_interval_sum_us <=
            UINT32_MAX - s_ui_timing_animation_interval_last_us) {
            s_ui_timing_animation_burst_interval_sum_us +=
                s_ui_timing_animation_interval_last_us;
        } else {
            s_ui_timing_animation_burst_interval_sum_us = UINT32_MAX;
        }
        if (s_ui_timing_animation_burst_frames != UINT32_MAX) {
            ++s_ui_timing_animation_burst_frames;
        }
        if (s_ui_timing_animation_interval_last_us >
            UI_ANIMATION_MISSED_FRAME_US &&
            s_ui_timing_animation_missed_frames != UINT32_MAX) {
            ++s_ui_timing_animation_missed_frames;
        }
    }
    s_ui_timing_last_animation_us = timestamp_us;
    if (s_ui_timing_animation_frames != UINT32_MAX) {
        ++s_ui_timing_animation_frames;
    }
}

static esp_err_t present(console_shell_t *shell)
{
    const int64_t dirty_started_us = esp_timer_get_time();
    if (s_ui_timing_last_dirty_us > 0 &&
        dirty_started_us > s_ui_timing_last_dirty_us) {
        const uint64_t interval_us = (uint64_t)(
            dirty_started_us - s_ui_timing_last_dirty_us);
        s_ui_timing_last_dirty_interval_us = interval_us > UINT32_MAX
            ? UINT32_MAX : (uint32_t)interval_us;
        if (s_ui_timing_last_dirty_interval_us >
            s_ui_timing_max_dirty_interval_us) {
            s_ui_timing_max_dirty_interval_us =
                s_ui_timing_last_dirty_interval_us;
        }
    }
    s_ui_timing_last_dirty_us = dirty_started_us;
    if (s_ui_timing_dirty_frames != UINT32_MAX) {
        ++s_ui_timing_dirty_frames;
    }
    const int64_t render_started_us = dirty_started_us;
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    /* Tab5 renders the native 1280x720 interface; Waveshare retains 768x480. Windows
     * Home reuses its persistent chrome and updates only moving scroll bands. */
    const bool rendered = console_shell_uses_native_bbs_launcher(shell)
        ? console_shell_render_rgb565(
              shell, s_pixels, CONSOLE_SHELL_WIDTH)
        : console_shell_render_native_cached_rgb565(
              shell, s_pixels, CONSOLE_SHELL_WIDTH);
    if (!rendered) {
        return ESP_ERR_INVALID_STATE;
    }
    console_shell_native_update_t native_update = {
        .kind = CONSOLE_SHELL_NATIVE_UPDATE_FULL,
    };
    if (!console_shell_get_native_update(shell, &native_update)) {
        return ESP_ERR_INVALID_STATE;
    }
#else
    if (!console_shell_render_rgb565(
            shell, s_pixels, CONSOLE_SHELL_WIDTH)) {
        return ESP_ERR_INVALID_STATE;
    }
#endif
    const int64_t submit_started_us = esp_timer_get_time();
    const uint64_t render_us = (uint64_t)(submit_started_us -
                                          render_started_us);
    s_ui_timing_last_render_us = render_us > UINT32_MAX
        ? UINT32_MAX : (uint32_t)render_us;
    if (s_ui_timing_last_render_us > s_ui_timing_max_render_us) {
        s_ui_timing_max_render_us = s_ui_timing_last_render_us;
    }
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    esp_err_t result;
#if CONFIG_P4_BOARD_M5STACK_TAB5
    if(native_update.kind==CONSOLE_SHELL_NATIVE_UPDATE_REGION){
        const platform_display_rgb565_region_t region={native_update.x,native_update.y,native_update.width,native_update.height};
        result=platform_display_submit_ui_region_rgb565(s_pixels,CONSOLE_SHELL_WIDTH,&region,CONSOLE_SUBMIT_TIMEOUT_MS);
    }else result=platform_display_submit_ui_rgb565(s_pixels,CONSOLE_SHELL_WIDTH,CONSOLE_SUBMIT_TIMEOUT_MS);
#else
    if (native_update.kind == CONSOLE_SHELL_NATIVE_UPDATE_REGION) {
        const platform_display_rgb565_region_t region = {
            .x = native_update.x,
            .y = native_update.y,
            .width = native_update.width,
            .height = native_update.height,
        };
        result = platform_display_submit_content_regions_rgb565(
            s_pixels, CONSOLE_SHELL_WIDTH, &region, 1U,
            CONSOLE_SUBMIT_TIMEOUT_MS);
    } else {
        result = platform_display_submit_content_rgb565(
            s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
    }
#endif
#else
    const esp_err_t result = platform_display_submit_rgb565(
        s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
#endif
    const int64_t submit_finished_us = esp_timer_get_time();
    const uint64_t submit_us = submit_finished_us > submit_started_us
        ? (uint64_t)(submit_finished_us - submit_started_us) : 0U;
    s_ui_timing_last_submit_us = submit_us > UINT32_MAX
        ? UINT32_MAX : (uint32_t)submit_us;
    if (s_ui_timing_last_submit_us > s_ui_timing_max_submit_us) {
        s_ui_timing_max_submit_us = s_ui_timing_last_submit_us;
    }
    return result;
}

/*
 * The Waveshare DPI driver can finish copying a frame while its refresh
 * callback lands outside the submitter's qualification window.  The frame
 * remains valid and the backlight is deliberately preserved, so a timeout is
 * a dropped acknowledgement rather than a fatal display fault.  Interactive
 * input must never turn that recoverable condition into a black-screen halt.
 */
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
static void arm_interactive_touch_timestamp_for_present(void)
{
    if (s_interactive_touch_pending_timestamp_us > 0) {
        (void)platform_display_record_interactive_input_timestamp(
            s_interactive_touch_pending_timestamp_us);
    }
}

static void clear_interactive_touch_timestamp_after_present(void)
{
    if (s_interactive_touch_pending_timestamp_us > 0) {
        (void)platform_display_record_interactive_input_timestamp(0);
        s_interactive_touch_pending_timestamp_us = 0;
    }
}
#endif

#if CONFIG_P4_BOARD_M5STACK_TAB5
static void report_scroll_timing(const console_shell_t *shell,bool presented)
{
    static uint32_t frames,render_max,submit_max;
    static uint64_t render_sum,submit_sum;
    static int64_t start,last;
    static uint32_t blits;
    const int64_t now=esp_timer_get_time();
    const bool moving=(shell->ng_scroll_kind&&!shell->press_active)||shell->ng_glide_kind;
    if(moving&&presented){
        if(!frames){start=now;blits=shell->native_home_scroll_blit_frames;}
        ++frames;last=now;render_sum+=s_ui_timing_last_render_us;submit_sum+=s_ui_timing_last_submit_us;
        if(s_ui_timing_last_render_us>render_max)render_max=s_ui_timing_last_render_us;
        if(s_ui_timing_last_submit_us>submit_max)submit_max=s_ui_timing_last_submit_us;
    }else if(frames&&!moving&&now-last>250000){
        ESP_LOGI(TAG,"P4_CONSOLE_OS SCROLL frames=%lu elapsed_ms=%lu render_avg_us=%lu render_max_us=%lu submit_avg_us=%lu submit_max_us=%lu cached_frames=%lu",
            (unsigned long)frames,(unsigned long)((last-start)/1000),(unsigned long)(render_sum/frames),(unsigned long)render_max,
            (unsigned long)(submit_sum/frames),(unsigned long)submit_max,(unsigned long)(shell->native_home_scroll_blit_frames-blits));
        frames=0;render_sum=0;submit_sum=0;render_max=0;submit_max=0;
    }
}
#endif

static esp_err_t present_interactive(console_shell_t *shell)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    /* Arm only the next native shell handoff, immediately before rendering.
     * The display owns the timestamp after this call; clear it after this
     * present so later animation/service frames cannot inherit it. */
    arm_interactive_touch_timestamp_for_present();
#endif
    const esp_err_t result = present(shell);
#if CONFIG_P4_BOARD_M5STACK_TAB5
    report_scroll_timing(shell,true);
#endif
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    clear_interactive_touch_timestamp_after_present();
    if (result == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS FRAME_ACK_MISSED action=continue "
                 "backlight_preserved=1 render=%lu",
                 shell == NULL ? 0UL :
                     (unsigned long)shell->render_generation);
        return ESP_OK;
    }
#endif
    return result;
}

#if !CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && !CONFIG_P4_BOARD_M5STACK_TAB5
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

#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
static esp_err_t submit_boot_ui(const uint16_t *pixels, size_t stride, uint32_t timeout)
{
#if CONFIG_P4_BOARD_M5STACK_TAB5
    return platform_display_submit_ui_rgb565(pixels, stride, timeout);
#else
    return platform_display_submit_content_rgb565(pixels, stride, timeout);
#endif
}
#endif

static esp_err_t draw_boot_screen(unsigned animation_step,
                                  const char *status, bool repaint)
{
    (void)repaint;
    if (s_pixels == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    static bool background_ready;
    static int64_t animation_started_us;
    const int64_t now_us=esp_timer_get_time();
    if(!background_ready)animation_started_us=now_us;
    const size_t logo_bytes = (size_t)(_binary_gamechangers_boot_mark_rgb565a8_end -
        _binary_gamechangers_boot_mark_rgb565a8_start);
#if CONFIG_P4_BOARD_M5STACK_TAB5
    const uint32_t elapsed_ms = (uint32_t)((uint64_t)(now_us - animation_started_us) / 1000U);
    const bool rendered = console_startup_render_flight(
        s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT,
        _binary_gamechangers_boot_mark_rgb565a8_start, logo_bytes, elapsed_ms,
        status, animation_step == CONSOLE_STARTUP_COMPLETE,
        !background_ready || repaint);
#else
    const unsigned native_animation = animation_step == CONSOLE_STARTUP_COMPLETE
        ? CONSOLE_STARTUP_COMPLETE
        : (unsigned)(((uint64_t)(now_us-animation_started_us)/40000U)%62U);
    const bool rendered = console_startup_render(
        s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT,
        _binary_gamechangers_boot_mark_rgb565a8_start, logo_bytes,
        native_animation, status, !background_ready);
#endif
    if (!rendered) {
        return ESP_ERR_INVALID_SIZE;
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    const int64_t rendered_us = esp_timer_get_time();
    if (!repaint) {
        const unsigned render_ms = (unsigned)((rendered_us-now_us)/1000);
        if (render_ms > s_boot_max_render_ms) s_boot_max_render_ms = render_ms;
    }
#endif
    background_ready = true;
    esp_err_t result = ESP_FAIL;
    for (unsigned attempt = 0U;
         attempt < CONSOLE_BOOT_SUBMIT_ATTEMPTS; ++attempt) {
#if CONFIG_P4_BOARD_M5STACK_TAB5
        /* All animated pixels, including bounded status text, stay in this
         * central band. The reviewed display service replays per-slot damage. */
        const platform_display_rgb565_region_t damage = {256, 0, 768, 720};
        result = repaint ? submit_boot_ui(s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS)
            : platform_display_submit_ui_region_rgb565(s_pixels, CONSOLE_SHELL_WIDTH,
                                                       &damage, CONSOLE_SUBMIT_TIMEOUT_MS);
#else
        result = submit_boot_ui(
            s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
#endif
        if (result != ESP_ERR_TIMEOUT) {
            break;
        }
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BOOT_FRAME_RETRY status=%s attempt=%u",
                 status == NULL ? "STARTING" : status, attempt + 1U);
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_BOOT_SUBMIT_RETRY_MS));
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    if (!repaint) {
        const unsigned submit_ms = (unsigned)((esp_timer_get_time()-rendered_us)/1000);
        if (submit_ms > s_boot_max_submit_ms) s_boot_max_submit_ms = submit_ms;
    }
#endif
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

#if CONFIG_P4_BOARD_M5STACK_TAB5
static void boot_animation_task(void *context)
{
    (void)context;
    for (;;) {
        char status[sizeof(s_boot_status)];
        portENTER_CRITICAL(&s_boot_lock);
        const bool stop = s_boot_stop;
        memcpy(status, s_boot_status, sizeof(status));
        portEXIT_CRITICAL(&s_boot_lock);
        if (stop) break;
        const int64_t started = esp_timer_get_time();
        const esp_err_t result = draw_boot_screen(0U, status, false);
        const unsigned elapsed = (unsigned)((esp_timer_get_time() - started) / 1000);
        if (elapsed > s_boot_max_frame_ms) s_boot_max_frame_ms = elapsed;
        if (result != ESP_OK) {
            s_boot_animation_error = result;
            break;
        }
        ++s_boot_frames;
        /* A notification interrupts the cadence wait when Home is ready. */
        const TickType_t wait = pdMS_TO_TICKS(elapsed < 60U ? 60U-elapsed : 1U);
        (void)ulTaskNotifyTake(pdTRUE, wait == 0U ? 1U : wait);
    }
    (void)xSemaphoreGive(s_boot_done);
    /* Parent owns deletion; the handle stays valid while it signals stop. */
    for (;;) vTaskSuspend(NULL);
}

static void start_boot_animation(void)
{
    s_boot_done = xSemaphoreCreateBinary();
    if (s_boot_done == NULL) return;
    s_boot_stop = false;
    s_boot_animation_error = ESP_OK;
    if (xTaskCreate(boot_animation_task, "boot-flight", 4096U, NULL, 1U,
                    &s_boot_task) != pdPASS) {
        vSemaphoreDelete(s_boot_done);
        s_boot_done = NULL;
        s_boot_task = NULL;
        ESP_LOGW(TAG, "P4_CONSOLE_OS BOOT_FLIGHT mode=static reason=no-task-memory");
    }
}

static esp_err_t stop_boot_animation(void)
{
    if (s_boot_task == NULL) return ESP_OK;
    portENTER_CRITICAL(&s_boot_lock);
    s_boot_stop = true;
    portEXIT_CRITICAL(&s_boot_lock);
    xTaskNotifyGive(s_boot_task);
    /* A failed join must never allow another renderer to reuse s_pixels. */
    if (xSemaphoreTake(s_boot_done, pdMS_TO_TICKS(2000U)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    const unsigned stack_remaining = (unsigned)uxTaskGetStackHighWaterMark(s_boot_task);
    vTaskDelete(s_boot_task);
    s_boot_task = NULL;
    vSemaphoreDelete(s_boot_done);
    s_boot_done = NULL;
    ESP_LOGI(TAG, "P4_CONSOLE_OS BOOT_FLIGHT frames=%u max_frame_ms=%u "
             "max_render_ms=%u max_submit_ms=%u stack_remaining=%u result=%s animation_hold_ms=0",
             s_boot_frames, s_boot_max_frame_ms, s_boot_max_render_ms, s_boot_max_submit_ms, stack_remaining,
             esp_err_to_name(s_boot_animation_error));
    return ESP_OK;
}
#endif

static esp_err_t present_boot_screen(unsigned animation_step, const char *status)
{
#if CONFIG_P4_BOARD_M5STACK_TAB5
    if (s_boot_task != NULL) {
        portENTER_CRITICAL(&s_boot_lock);
        (void)snprintf(s_boot_status, sizeof(s_boot_status), "%s",
                       status == NULL ? "Starting your console" : status);
        portEXIT_CRITICAL(&s_boot_lock);
        return ESP_OK;
    }
#endif
    return draw_boot_screen(animation_step, status, true);
}

#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
static esp_err_t present_home_reveal(console_shell_t *shell)
{
    if (shell == NULL || s_pixels == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    const esp_err_t stop_result = stop_boot_animation();
    if (stop_result != ESP_OK) return stop_result;
#else
    const esp_err_t ready_result=present_boot_screen(CONSOLE_STARTUP_COMPLETE,"Ready");
    if(ready_result!=ESP_OK)return ready_result;
    ESP_LOGI(TAG,"P4_CONSOLE_OS BOOT_PROGRESS state=complete track=full-width");
#endif
    const size_t surface_width = CONSOLE_SHELL_WIDTH;
    const size_t surface_height = CONSOLE_SHELL_HEIGHT;
    const bool rendered = console_shell_uses_native_bbs_launcher(shell)
        ? console_shell_render_rgb565(shell, s_pixels, surface_width)
        : console_shell_render_native_cached_rgb565(
              shell, s_pixels, surface_width);
    if (!rendered) {
        return ESP_ERR_INVALID_STATE;
    }

#if CONFIG_P4_BOARD_M5STACK_TAB5
    /* Present the finished desktop immediately; no decorative boot delay. */
    const esp_err_t result = submit_boot_ui(
        s_pixels, surface_width, CONSOLE_SUBMIT_TIMEOUT_MS);
    ESP_LOGI(TAG, "P4_CONSOLE_OS HOME_REVEAL status=%s rows=%u steps=1 mode=immediate",
             result == ESP_OK ? "complete" : "failed", (unsigned)surface_height);
    return result;
#else
    const size_t pixel_count = surface_width * surface_height;
    uint16_t *const reveal_pixels = heap_caps_calloc(
        pixel_count, sizeof(*reveal_pixels),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (reveal_pixels == NULL) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS HOME_REVEAL status=skipped reason=no-memory");
        return submit_boot_ui(
            s_pixels, surface_width, CONSOLE_SUBMIT_TIMEOUT_MS);
    }

    esp_err_t result = ESP_OK;
    size_t previous_rows = 0U;
    for (unsigned step = 1U;
         step <= CONSOLE_HOME_REVEAL_STEPS; ++step) {
        const size_t visible_rows =
            surface_height * step /
            CONSOLE_HOME_REVEAL_STEPS;
        const size_t added_rows = visible_rows - previous_rows;
        memcpy(reveal_pixels + previous_rows * surface_width,
               s_pixels + previous_rows * surface_width,
               added_rows * surface_width * sizeof(*s_pixels));
        for (unsigned attempt = 0U;
             attempt < CONSOLE_BOOT_SUBMIT_ATTEMPTS; ++attempt) {
            result = submit_boot_ui(
                reveal_pixels, surface_width, CONSOLE_SUBMIT_TIMEOUT_MS);
            if (result != ESP_ERR_TIMEOUT) {
                break;
            }
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS HOME_REVEAL_RETRY step=%u attempt=%u",
                     step, attempt + 1U);
            vTaskDelay(pdMS_TO_TICKS(CONSOLE_BOOT_SUBMIT_RETRY_MS));
        }
        if (result != ESP_OK) {
            break;
        }
        previous_rows = visible_rows;
        if (step < CONSOLE_HOME_REVEAL_STEPS) {
            vTaskDelay(pdMS_TO_TICKS(CONSOLE_HOME_REVEAL_DELAY_MS));
        }
    }
    heap_caps_free(reveal_pixels);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS HOME_REVEAL status=%s rows=%u steps=%u",
             result == ESP_OK ? "complete" : "failed",
             (unsigned)previous_rows,
             (unsigned)CONSOLE_HOME_REVEAL_STEPS);
    return result;
#endif
}
#endif

#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static esp_err_t destroy_touch_for_handoff(void)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    const esp_err_t mailbox_result = stop_touch_mailbox();
    if (mailbox_result != ESP_OK) {
        /* Do not destroy the GT911 handle until its sole poller has joined. */
        return mailbox_result;
    }
#endif
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
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    return s_shared_bus == NULL
        ? NULL : (void *)platform_i2c_shared_handle(s_shared_bus);
#else
    return NULL;
#endif
}

static bool pump_native_audio(p4_game_platform_audio_t *audio,
                              p4_audio_mixer_t *mixer,
                              uint32_t frame_count)
{
    if (!p4_game_platform_audio_running(audio) || frame_count == 0U ||
        frame_count > CONSOLE_NATIVE_AUDIO_FRAMES_MAX) {
        return false;
    }
    if (!p4_audio_mixer_render(
            mixer, s_native_audio_pcm, frame_count)) {
        halt_dark("native-audio-mix", ESP_ERR_INVALID_SIZE);
    }
    for (uint32_t offset = 0U; offset < frame_count;) {
        uint32_t frames = frame_count - offset;
        if (frames > P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES) {
            frames = P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES;
        }
        const esp_err_t result = p4_game_platform_audio_write(
            audio,
            &s_native_audio_pcm[
                (size_t)offset * P4_GAME_PLATFORM_AUDIO_CHANNEL_COUNT],
            frames);
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
        offset += frames;
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
                                p4_tick_scheduler_t *frame_scheduler,
                                p4_tick_scheduler_t *audio_scheduler,
                                TickType_t *last_wake,
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
        ((unsigned)duration_ms * CONSOLE_GAME_UPDATE_HZ + 999U) / 1000U;
    for (unsigned tick = 0U; played && tick < tone_ticks; ++tick) {
        uint32_t audio_frames = 0U;
        uint32_t interval_ticks = 0U;
        played = p4_tick_scheduler_next(audio_scheduler, &audio_frames) ==
                P4_SCHEDULER_OK &&
            p4_tick_scheduler_next(frame_scheduler, &interval_ticks) ==
                P4_SCHEDULER_OK &&
            pump_native_audio(audio, mixer, audio_frames);
        if (played) {
            vTaskDelayUntil(last_wake, (TickType_t)interval_ticks);
        }
    }
    p4_audio_mixer_stop_all(mixer);
    const unsigned gap_ticks =
        ((unsigned)gap_ms * CONSOLE_GAME_UPDATE_HZ + 999U) / 1000U;
    for (unsigned tick = 0U; played && tick < gap_ticks; ++tick) {
        uint32_t audio_frames = 0U;
        uint32_t interval_ticks = 0U;
        played = p4_tick_scheduler_next(audio_scheduler, &audio_frames) ==
                P4_SCHEDULER_OK &&
            p4_tick_scheduler_next(frame_scheduler, &interval_ticks) ==
                P4_SCHEDULER_OK &&
            pump_native_audio(audio, mixer, audio_frames);
        if (played) {
            vTaskDelayUntil(last_wake, (TickType_t)interval_ticks);
        }
    }
    return played;
}

#if CONFIG_P4_BOARD_M5STACK_TAB5
static void play_tab5_boot_fanfare(void)
{
    if (s_console_settings.boot_volume_step == 0U) {
        ESP_LOGI(TAG, "P4_CONSOLE_OS BOOT_AUDIO status=muted animation=skipped");
        return;
    }
    if (!native_audio_runtime_allowed()) {
        ESP_LOGI(TAG, "P4_CONSOLE_OS BOOT_AUDIO status=silent reason=runtime-gate");
        return;
    }
    const int64_t started_us = esp_timer_get_time();
    p4_game_platform_audio_t audio;
    p4_game_platform_audio_init(&audio);
    esp_err_t result = p4_game_platform_audio_open(&audio, true,
        native_audio_control_bus(), s_console_settings.boot_volume_step);
    if (result != ESP_OK) {
        close_native_audio_or_halt(&audio);
        ESP_LOGW(TAG, "P4_CONSOLE_OS BOOT_AUDIO status=silent error=%s", esp_err_to_name(result));
        return;
    }
    const size_t total = console_startup_audio_frames();
    size_t frame = 0U;
    /* The logo is already visible; avoid display fences in this short cue. */
    for (; frame < total;) {
        size_t count = total - frame;
        if (count > P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES)
            count = P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES;
        if (!console_startup_audio_render(s_native_audio_pcm, frame, count)) {
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        result = p4_game_platform_audio_write(&audio, s_native_audio_pcm, (uint32_t)count);
        if (result != ESP_OK) break;
        frame += count;
    }
    close_native_audio_or_halt(&audio);
    ESP_LOGI(TAG, "P4_CONSOLE_OS BOOT_AUDIO status=%s profile=treasure-chords "
        "sample_rate=16000 frames=%u volume=%u nominal_ms=%u elapsed_ms=%" PRIi64,
        result == ESP_OK ? "played" : "degraded", (unsigned)frame,
        (unsigned)s_console_settings.boot_volume_step,
        (unsigned)(total * 1000U / CONSOLE_STARTUP_SAMPLE_RATE),
        (esp_timer_get_time() - started_us) / INT64_C(1000));
}
#endif

static void play_boot_chime(void)
{
#if CONFIG_P4_BOARD_M5STACK_TAB5
    play_tab5_boot_fanfare();
    return;
#endif
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
    p4_tick_scheduler_t frame_scheduler;
    p4_tick_scheduler_t audio_scheduler;
    TickType_t last_wake = xTaskGetTickCount();
    if (p4_tick_scheduler_init(
            &frame_scheduler, configTICK_RATE_HZ,
            CONSOLE_GAME_UPDATE_HZ) != P4_SCHEDULER_OK ||
        p4_tick_scheduler_init(
            &audio_scheduler, P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ,
            CONSOLE_GAME_UPDATE_HZ) != P4_SCHEDULER_OK) {
        close_native_audio_or_halt(&audio);
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BOOT_SOUND_DEGRADED "
                 "reason=audio-scheduler fallback=silent");
        wait_with_boot_audio_animation();
        return;
    }
    bool played = true;
    (void)present_boot_screen(1U, "POST OK");
    played = play_boot_tone_pair(
        &audio, &mixer, &frame_scheduler, &audio_scheduler, &last_wake,
        880U, 0U, CONSOLE_BOOT_POST_BEEP_MS,
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
            &audio, &mixer, &frame_scheduler, &audio_scheduler, &last_wake,
            phase->low_hz, phase->high_hz,
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
            &audio, &mixer, &frame_scheduler, &audio_scheduler, &last_wake,
            s_boot_dial[digit].low_hz,
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
            &audio, &mixer, &frame_scheduler, &audio_scheduler, &last_wake,
            phase->low_hz, phase->high_hz,
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
                    ? "SD READY" : "SD CARD DEGRADED");
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

enum {
    CARTRIDGE_SAVE_NOTIFY_WORK = UINT32_C(1) << 0U,
    CARTRIDGE_SAVE_NOTIFY_STOP = UINT32_C(1) << 1U,
};

typedef struct {
    p4_game_save_service_t service;
    StaticSemaphore_t lock_storage;
    StaticSemaphore_t stopped_storage;
    SemaphoreHandle_t lock;
    SemaphoreHandle_t stopped;
    TaskHandle_t worker;
    uint8_t *queue_workspace;
    uint8_t *object_workspace;
    uint8_t *launch_snapshot;
    uint32_t processed_requests;
    uint32_t callback_busy_rejections;
    unsigned worker_low_water_bytes;
} cartridge_save_runtime_t;

static void save_catalog_record(const char *game_id, const char *slot_id,
                                size_t payload_bytes, uint32_t sequence)
{
    if (game_id == NULL || slot_id == NULL || payload_bytes == 0U ||
        sequence == 0U) {
        return;
    }
    s_saves.writable = storage_app_owned();
    for (size_t index = 0U; index < s_saves.count; ++index) {
        p4_save_slot_t *const slot = &s_saves.slots[index];
        if (slot->valid && strcmp(slot->game_id, game_id) == 0 &&
            strcmp(slot->slot_name, slot_id) == 0) {
            s_saves.total_bytes -= slot->size_bytes;
            slot->size_bytes = payload_bytes;
            slot->sequence = sequence;
            s_saves.total_bytes += payload_bytes;
            return;
        }
    }
    (void)p4_save_catalog_add(
        &s_saves, game_id, slot_id, payload_bytes, sequence);
}

static p4_game_save_storage_mode_t cartridge_save_storage_mode(void)
{
    platform_game_storage_status_t status;
    if (platform_game_storage_get_status(&status) != ESP_OK) {
        return P4_GAME_SAVE_STORAGE_UNAVAILABLE;
    }
    if (status.state == PLATFORM_GAME_STORAGE_APP_READY) {
        /* Game saves use the OS-owned, journaled /SAVES subtree only. The
         * store stages, fsyncs, validates, backs up, atomically renames, and
         * recovers each bounded slot while the app exclusively owns FAT. */
        return P4_GAME_SAVE_STORAGE_WRITABLE;
    }
    if (status.state == PLATFORM_GAME_STORAGE_USB_HOST ||
        status.state == PLATFORM_GAME_STORAGE_TRANSITION) {
        return P4_GAME_SAVE_STORAGE_HOST_OWNED;
    }
    return P4_GAME_SAVE_STORAGE_UNAVAILABLE;
}

static bool cartridge_save_legacy_query(
    void *context, const char *game_id, const char *slot_id,
    bool *allowed_out)
{
    (void)context;
    if (allowed_out == NULL) {
        return false;
    }
    *allowed_out = false;
    bool closed = false;
    const esp_err_t result = platform_save_seal_legacy_is_closed(
        game_id, slot_id, &closed);
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS SAVE_LEGACY_MARKER app=%s slot=%s "
                 "operation=query result=%s capability=withheld",
                 game_id, slot_id, esp_err_to_name(result));
        return false;
    }
    *allowed_out = !closed;
    return true;
}

static bool cartridge_save_legacy_close(
    void *context, const char *game_id, const char *slot_id)
{
    (void)context;
    const esp_err_t result = platform_save_seal_close_legacy(
        game_id, slot_id);
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS SAVE_LEGACY_MARKER app=%s slot=%s "
                 "operation=close result=%s capability=withheld",
                 game_id, slot_id, esp_err_to_name(result));
        return false;
    }
    return true;
}

static bool cartridge_save_object_query(
    void *context, const char *game_id, const char *slot_id,
    uint32_t sequence,
    const uint8_t object_sha256[P4_GAME_SAVE_SHA256_BYTES],
    bool *allowed_out)
{
    (void)context;
    if (allowed_out == NULL) {
        return false;
    }
    *allowed_out = false;
    const esp_err_t result = platform_save_seal_object_is_allowed(
        game_id, slot_id, sequence, object_sha256, allowed_out);
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS SAVE_FRESHNESS app=%s slot=%s "
                 "operation=query sequence=%lu result=%s "
                 "capability=withheld",
                 game_id, slot_id, (unsigned long)sequence,
                 esp_err_to_name(result));
        return false;
    }
    return true;
}

static bool cartridge_save_object_advance(
    void *context, const char *game_id, const char *slot_id,
    uint32_t sequence,
    const uint8_t object_sha256[P4_GAME_SAVE_SHA256_BYTES])
{
    (void)context;
    const esp_err_t result = platform_save_seal_advance_object(
        game_id, slot_id, sequence, object_sha256);
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS SAVE_FRESHNESS app=%s slot=%s "
                 "operation=advance sequence=%lu result=%s "
                 "capability=withheld",
                 game_id, slot_id, (unsigned long)sequence,
                 esp_err_to_name(result));
        return false;
    }
    return true;
}

static bool cartridge_save_object_sequence(
    void *context, const char *game_id, const char *slot_id,
    uint32_t *sequence_out)
{
    (void)context;
    if (sequence_out == NULL) {
        return false;
    }
    *sequence_out = 0U;
    const esp_err_t result = platform_save_seal_object_sequence(
        game_id, slot_id, sequence_out);
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS SAVE_FRESHNESS app=%s slot=%s "
                 "operation=sequence result=%s capability=withheld",
                 game_id, slot_id, esp_err_to_name(result));
        return false;
    }
    return true;
}

static void cartridge_save_worker(void *opaque)
{
    cartridge_save_runtime_t *const runtime = opaque;
    if (runtime == NULL) {
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        uint32_t notifications = 0U;
        (void)xTaskNotifyWait(
            0U, UINT32_MAX, &notifications, portMAX_DELAY);
        if (xSemaphoreTake(runtime->lock, portMAX_DELAY) == pdTRUE) {
            const size_t processed = p4_game_save_service_process(
                &runtime->service, P4_GAME_SAVE_MAX_SLOTS);
            if (UINT32_MAX - runtime->processed_requests < processed) {
                runtime->processed_requests = UINT32_MAX;
            } else {
                runtime->processed_requests += (uint32_t)processed;
            }
            (void)xSemaphoreGive(runtime->lock);
        }
        if ((notifications & CARTRIDGE_SAVE_NOTIFY_STOP) != 0U) {
            runtime->worker_low_water_bytes =
                (unsigned)uxTaskGetStackHighWaterMark(NULL);
            (void)xSemaphoreGive(runtime->stopped);
            vTaskDelete(NULL);
            return;
        }
    }
}

static void cartridge_save_release_allocations(
    cartridge_save_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    p4_game_save_service_clear(&runtime->service);
    heap_caps_free(runtime->launch_snapshot);
    heap_caps_free(runtime->object_workspace);
    heap_caps_free(runtime->queue_workspace);
    heap_caps_free(runtime);
}

static cartridge_save_runtime_t *cartridge_save_open(
    const p4_game_package_info_t *package)
{
    if (package == NULL || protected_game_lineage_check(package) ==
            P4_PROTECTED_GAME_REJECTED) {
        if (package != NULL) {
            ESP_LOGE(TAG,
                     "P4_CONSOLE_OS PROTECTED_GAME_REJECTED id=%s "
                     "operation=save-open reason=payload-lineage",
                     package->id);
        }
        return NULL;
    }
    const char *const game_id = package->id;
    p4_game_save_protection_t protection = {0};
    _Static_assert((unsigned)PLATFORM_SAVE_SEAL_KEY_BYTES ==
                       (unsigned)P4_GAME_SAVE_KEY_BYTES,
                   "save seal key sizes must match");
    const esp_err_t seal_result = platform_save_seal_load_key(
        protection.key);
    if (seal_result != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS SAVE_SEAL_UNAVAILABLE app=%s nvs=%s "
                 "capability=withheld",
                 game_id, esp_err_to_name(seal_result));
        return NULL;
    }
    cartridge_save_runtime_t *const runtime = heap_caps_calloc(
        1U, sizeof(*runtime), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (runtime == NULL) {
        p4_game_save_protection_clear(&protection);
        return NULL;
    }
    runtime->queue_workspace = heap_caps_malloc(
        P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    runtime->object_workspace = heap_caps_malloc(
        P4_GAME_SAVE_MAX_FILE_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    runtime->launch_snapshot = heap_caps_malloc(
        P4_GAME_SAVE_MAX_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    runtime->lock = xSemaphoreCreateMutexStatic(&runtime->lock_storage);
    runtime->stopped = xSemaphoreCreateBinaryStatic(
        &runtime->stopped_storage);
    const p4_game_save_legacy_policy_t legacy_policy = {
        .query = cartridge_save_legacy_query,
        .close = cartridge_save_legacy_close,
        .object_query = cartridge_save_object_query,
        .object_advance = cartridge_save_object_advance,
        .object_sequence = cartridge_save_object_sequence,
        .context = NULL,
    };
    const bool initialized = runtime->queue_workspace != NULL &&
        runtime->object_workspace != NULL &&
        runtime->launch_snapshot != NULL && runtime->lock != NULL &&
        runtime->stopped != NULL &&
        p4_game_save_service_init(
            &runtime->service, PLATFORM_GAME_STORAGE_MOUNT_POINT,
            cartridge_save_storage_mode(), &protection, &legacy_policy,
            game_id, "AUTO",
            runtime->queue_workspace,
            P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES,
            runtime->object_workspace, P4_GAME_SAVE_MAX_FILE_BYTES,
            runtime->launch_snapshot, P4_GAME_SAVE_MAX_BYTES);
    p4_game_save_protection_clear(&protection);
    if (!initialized) {
        cartridge_save_release_allocations(runtime);
        return NULL;
    }
    if (!p4_game_save_service_available(&runtime->service)) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS SAVE_DEGRADED app=%s startup=%s "
                 "capability=unavailable",
                 game_id, p4_game_save_store_result_name(
                     runtime->service.startup_result));
        return runtime;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS SAVE_SEAL_READY app=%s format=P4SAVE2 "
             "legacy_migrated=%u sequence=%lu",
             game_id,
             runtime->service.launch_migrated_legacy ? 1U : 0U,
             (unsigned long)runtime->service.launch_sequence);
    const BaseType_t created = xTaskCreateWithCaps(
        cartridge_save_worker, "game_save",
        CONSOLE_GAME_SAVE_STACK_BYTES, runtime, tskIDLE_PRIORITY + 1U,
        &runtime->worker, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        p4_game_save_service_set_storage_mode(
            &runtime->service, P4_GAME_SAVE_STORAGE_UNAVAILABLE);
        runtime->worker = NULL;
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS SAVE_DEGRADED app=%s "
                 "startup=worker-allocation capability=unavailable",
                 game_id);
    }
    return runtime;
}

static bool cartridge_save_close(cartridge_save_runtime_t *runtime,
                                 const char *game_id)
{
    if (runtime == NULL) {
        return true;
    }
    if (runtime->worker != NULL) {
        (void)xTaskNotify(
            runtime->worker, CARTRIDGE_SAVE_NOTIFY_STOP, eSetBits);
        if (xSemaphoreTake(
                runtime->stopped,
                pdMS_TO_TICKS(CONSOLE_GAME_SAVE_STOP_TIMEOUT_MS)) !=
            pdTRUE) {
            ESP_LOGE(TAG,
                     "P4_CONSOLE_OS SAVE_WORKER_TIMEOUT app=%s "
                     "action=retain-runtime",
                     game_id);
            return false;
        }
    }
    size_t payload_bytes = 0U;
    uint32_t schema_version = 0U;
    uint32_t sequence = 0U;
    if (p4_game_save_memory_copy_snapshot(
            &runtime->service.queue, "AUTO", runtime->launch_snapshot,
            P4_GAME_SAVE_MAX_BYTES, &payload_bytes, &schema_version,
            &sequence)) {
        (void)schema_version;
        save_catalog_record(game_id, "AUTO", payload_bytes, sequence);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS SAVE_WORKER_STOP app=%s requests=%lu "
             "callback_busy=%lu low_water_bytes=%u snapshot_bytes=%u "
             "sequence=%lu",
             game_id, (unsigned long)runtime->processed_requests,
             (unsigned long)runtime->callback_busy_rejections,
             runtime->worker_low_water_bytes, (unsigned)payload_bytes,
             (unsigned long)sequence);
    cartridge_save_release_allocations(runtime);
    return true;
}

typedef struct {
    const char *game_id;
    p4_achievement_catalog_t *achievements;
    cartridge_save_runtime_t *save;
    p4_audio_mixer_t mixer;
    p4_game_platform_audio_t audio;
    p4_game_audio_worker_t *audio_worker;
    p4_game_input_mapper_t input_mapper;
    p4_tick_scheduler_t frame_scheduler;
    cartridge_audio_clock_t audio_clock;
    TickType_t last_wake;
    cartridge_frame_clock_t frame_clock;
    cartridge_phase_t input_phase,audio_phase,render_phase,present_phase;
    cartridge_phase_t display_reuse_phase,display_transform_phase,display_handoff_phase;
    int64_t poll_completed_us;
    uint32_t deadline_rebases;
    uint32_t frames;
    uint32_t frame_overruns;
    uint32_t max_frame_ms;
    uint32_t max_frame_late_ms;
    uint32_t display_ack_misses;
    p4_game_result_t game_result;
    bool audio_running;
    bool multiplayer_running;
    bool high_res_video;
    bool finished;
} cartridge_run_context_t;

static bool native_multiplayer_begin(void)
{
    const platform_game_catalog_entry_t *const game =
        platform_game_catalog_find_launcher(
            &s_game_catalog, s_multiplayer_native_launcher_id);
    if (s_multiplayer_launch_kind != CONSOLE_MP_LAUNCH_NATIVE ||
        !native_game_supports_multiplayer(game) ||
        s_multiplayer_session.state != P4_MP_SESSION_CONNECTED ||
        s_multiplayer_local_player_slot >= s_multiplayer_player_count ||
        s_multiplayer_player_count < 2U ||
        s_multiplayer_player_count > P4_GAME_MULTIPLAYER_MAX_PLAYERS ||
        s_multiplayer_launch_session_seed == 0U ||
        s_multiplayer_launch_route_id == 0U) {
        return false;
    }
    ++s_native_multiplayer_generation;
    if (s_native_multiplayer_generation == 0U) {
        s_native_multiplayer_generation = 1U;
    }
    s_native_multiplayer = (console_native_multiplayer_t){
        .generation = s_native_multiplayer_generation,
        .session_seed = s_multiplayer_launch_session_seed,
        .route_id = s_multiplayer_launch_route_id,
        .state = P4_GAME_MULTIPLAYER_CONNECTED,
        .role = s_multiplayer_session.role == P4_MP_ROLE_HOST
            ? P4_GAME_MULTIPLAYER_ROLE_HOST
            : P4_GAME_MULTIPLAYER_ROLE_CLIENT,
        .local_player_slot = s_multiplayer_local_player_slot,
        .player_count = s_multiplayer_player_count,
        .profile = game->package.multiplayer_profile,
        .active = true,
    };
    return true;
}

static void native_multiplayer_poll(void)
{
    if (!s_native_multiplayer.active ||
        s_native_multiplayer.state != P4_GAME_MULTIPLAYER_CONNECTED) {
        return;
    }
    multiplayer_transport_poll();
    const int64_t now_us = esp_timer_get_time();
    if (!multiplayer_transport_connected(s_native_multiplayer.route_id)) {
        p4_mp_event_t disconnected;
        (void)p4_mp_session_route_disconnected(
            &s_multiplayer_session, s_native_multiplayer.route_id,
            &disconnected);
        native_multiplayer_mark_peer_left();
        return;
    }
    p4_mp_event_t timeout;
    if (p4_mp_session_tick(
            &s_multiplayer_session, (uint64_t)now_us / UINT64_C(1000),
            &timeout)) {
        native_multiplayer_mark_peer_left();
        return;
    }
    if (s_native_multiplayer.next_keepalive_us == 0 ||
        now_us >= s_native_multiplayer.next_keepalive_us) {
        uint8_t keepalive[8];
        const uint64_t stamp = (uint64_t)now_us / UINT64_C(1000);
        for (unsigned index = 0U; index < sizeof(keepalive); ++index) {
            keepalive[index] = (uint8_t)(stamp >> (index * 8U));
        }
        (void)send_session_multiplayer_packet(
            P4_MP_PACKET_PING, 0U, keepalive, sizeof(keepalive));
        s_native_multiplayer.next_keepalive_us = now_us +
            (int64_t)CONSOLE_MULTIPLAYER_KEEPALIVE_INTERVAL_MS * 1000;
    }
}

static bool cartridge_multiplayer_read_status(
    void *opaque, p4_game_multiplayer_status_t *status_out)
{
    const cartridge_run_context_t *const context = opaque;
    if (context == NULL || !context->multiplayer_running ||
        !s_native_multiplayer.active || status_out == NULL) {
        return false;
    }
    *status_out = (p4_game_multiplayer_status_t){
        .generation = s_native_multiplayer.generation,
        .session_seed = s_native_multiplayer.session_seed,
        .state = s_native_multiplayer.state,
        .role = s_native_multiplayer.role,
        .local_player_slot = s_native_multiplayer.local_player_slot,
        .player_count = s_native_multiplayer.player_count,
    };
    return true;
}

static bool cartridge_multiplayer_send(
    void *opaque, const uint8_t *data, size_t data_bytes)
{
    const cartridge_run_context_t *const context = opaque;
    return context != NULL && context->multiplayer_running &&
        s_native_multiplayer.active &&
        s_native_multiplayer.state == P4_GAME_MULTIPLAYER_CONNECTED &&
        data != NULL && data_bytes != 0U &&
        data_bytes <= s_native_multiplayer.profile.message_bytes &&
        send_session_multiplayer_packet(
            P4_MP_PACKET_GAME_MESSAGE, 0U, data,
            (uint16_t)data_bytes) == ESP_OK;
}

static bool cartridge_multiplayer_receive(
    void *opaque, p4_game_multiplayer_message_t *message_out)
{
    const cartridge_run_context_t *const context = opaque;
    if (context == NULL || !context->multiplayer_running ||
        !s_native_multiplayer.active || message_out == NULL ||
        s_native_multiplayer.queue_count == 0U) {
        return false;
    }
    *message_out =
        s_native_multiplayer.queue[s_native_multiplayer.queue_head];
    s_native_multiplayer.queue_head =
        (s_native_multiplayer.queue_head + 1U) %
            CONSOLE_NATIVE_MULTIPLAYER_QUEUE_DEPTH;
    --s_native_multiplayer.queue_count;
    return true;
}

static void native_multiplayer_end(void)
{
    if (!s_native_multiplayer.active) {
        return;
    }
    if (s_native_multiplayer.state == P4_GAME_MULTIPLAYER_CONNECTED) {
        const uint8_t leave[2] = {0U, 0U};
        (void)send_session_multiplayer_packet(
            P4_MP_PACKET_LEAVE, 0U, leave, sizeof(leave));
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS NATIVE_MULTIPLAYER_STOP game=%s "
             "state=%u dropped=%lu",
             s_multiplayer_local_offer.game_id,
             (unsigned)s_native_multiplayer.state,
             (unsigned long)s_native_multiplayer.dropped_messages);
    s_native_multiplayer.active = false;
}

static bool cartridge_queue_save(
    void *opaque, const char *slot_id, uint32_t schema_version,
    uint32_t expected_sequence, const uint8_t *data, size_t data_bytes,
    p4_game_save_ticket_t *ticket_out)
{
    cartridge_run_context_t *const context = opaque;
    if (ticket_out != NULL) {
        *ticket_out = P4_GAME_SAVE_INVALID_TICKET;
    }
    if (context == NULL || context->save == NULL ||
        context->save->worker == NULL || ticket_out == NULL ||
        xSemaphoreTake(context->save->lock, 0U) != pdTRUE) {
        if (context != NULL && context->save != NULL &&
            context->save->callback_busy_rejections != UINT32_MAX) {
            ++context->save->callback_busy_rejections;
        }
        return false;
    }
    const bool accepted = p4_game_save_service_queue(
        &context->save->service, slot_id, schema_version,
        expected_sequence, data, data_bytes, ticket_out);
    (void)xSemaphoreGive(context->save->lock);
    if (accepted) {
        (void)xTaskNotify(
            context->save->worker, CARTRIDGE_SAVE_NOTIFY_WORK, eSetBits);
    }
    return accepted;
}

static bool cartridge_read_save_status(
    void *opaque, p4_game_save_ticket_t ticket,
    p4_game_save_status_t *status_out,
    uint32_t *committed_sequence_out)
{
    cartridge_run_context_t *const context = opaque;
    if (context == NULL || context->save == NULL ||
        xSemaphoreTake(context->save->lock, 0U) != pdTRUE) {
        return false;
    }
    const bool read = p4_game_save_service_read_status(
        &context->save->service, ticket, status_out,
        committed_sequence_out);
    (void)xSemaphoreGive(context->save->lock);
    return read;
}

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

#if CONFIG_P4_BOARD_M5STACK_TAB5
static bool cartridge_read_motion(void *opaque, p4_game_motion_t *out)
{
    (void)opaque;
    const platform_tab5_telemetry_t sample = platform_tab5_sensors_snapshot();
    return platform_tab5_game_motion(&sample, (uint64_t)esp_timer_get_time(), out);
}
#endif

static bool cartridge_play_tone(void *opaque, const p4_tone_t *tone)
{
    cartridge_run_context_t *const context = opaque;
    if(!context || !context->audio_running)return false;
    return context->audio_worker ? p4_game_audio_worker_tone(context->audio_worker,tone) :
        p4_audio_mixer_service_play_tone(&context->mixer,tone);
}
static bool cartridge_submit_pcm16_stereo(void *opaque,
    const int16_t *pcm,size_t frames)
{
    cartridge_run_context_t *const context = opaque;
    if(!context || !context->audio_running)return false;
    return context->audio_worker ? p4_game_audio_worker_pcm(context->audio_worker,pcm,frames) :
        p4_audio_mixer_service_submit_pcm16_stereo(&context->mixer,pcm,frames);
}
static void cartridge_stop_audio(void *opaque)
{
    cartridge_run_context_t *const context = opaque;
    if(!context)return;
    if(context->audio_worker)p4_game_audio_worker_stop(context->audio_worker);
    else p4_audio_mixer_service_stop(&context->mixer);
}
static uint32_t cartridge_audio_frames(cartridge_run_context_t *context)
{
    if(!context->audio_worker)return context->audio.frames_written;
    p4_game_audio_worker_stats_t stats={0};
    p4_game_audio_worker_stats(context->audio_worker,&stats);
    return stats.frames_written;
}
static void cartridge_audio_close(cartridge_run_context_t *context)
{
    if(context->audio_worker) {
        p4_game_audio_worker_stats_t stats={0};
        p4_game_audio_worker_stats(context->audio_worker,&stats);
        ESP_LOGI(TAG,"P4_CONSOLE_OS AUDIO_WORKER_STOP core=%d frames=%lu underrun=%lu rejected=%lu stack=%lu",
            stats.core,(unsigned long)stats.frames_written,
            (unsigned long)stats.mixer.stream_underrun_frames,
            (unsigned long)stats.rejected_commands,(unsigned long)stats.stack_remaining);
        if(p4_game_audio_worker_close(&context->audio_worker)!=ESP_OK)
            halt_dark("audio-worker-join",ESP_ERR_TIMEOUT);
    }
    close_native_audio_or_halt(&context->audio);
}

static void cartridge_log_timing(const cartridge_run_context_t *context)
{
    if(context->audio_worker) {
        p4_game_audio_worker_stats_t audio={0};
        p4_game_audio_worker_stats(context->audio_worker,&audio);
        ESP_LOGI(TAG,"P4_CONSOLE_OS AUDIO_WORKER app=%s game_core=%d audio_core=%d running=%u queued=%u rejected=%lu underrun=%lu clipped=%lu frames=%lu write_failures=%lu stack=%lu",
            context->game_id,xPortGetCoreID(),audio.core,audio.running?1U:0U,
            (unsigned)audio.mixer.stream_queued_frames,(unsigned long)audio.rejected_commands,
            (unsigned long)audio.mixer.stream_underrun_frames,(unsigned long)audio.mixer.clipped_samples,
            (unsigned long)audio.frames_written,(unsigned long)audio.write_failures,
            (unsigned long)audio.stack_remaining);
    }

    ESP_LOGI(TAG,
        "P4_CONSOLE_OS CARTRIDGE_TIMING app=%s samples=%lu wall_total_us=%llu "
        "wall_avg_us=%lu wall_max_us=%lu update_render_avg_us=%lu update_render_max_us=%lu "
        "input_avg_us=%lu input_max_us=%lu audio_avg_us=%lu audio_max_us=%lu "
        "present_avg_us=%lu present_max_us=%lu reuse_avg_us=%lu reuse_max_us=%lu "
        "transform_avg_us=%lu transform_max_us=%lu handoff_avg_us=%lu handoff_max_us=%lu "
        "deadline_rebases=%lu audio_discarded_us=%llu",
        context->game_id,(unsigned long)context->frame_clock.wall.samples,
        (unsigned long long)context->frame_clock.wall.total_us,
        (unsigned long)cartridge_phase_average(&context->frame_clock.wall),(unsigned long)context->frame_clock.wall.maximum_us,
        (unsigned long)cartridge_phase_average(&context->render_phase),(unsigned long)context->render_phase.maximum_us,
        (unsigned long)cartridge_phase_average(&context->input_phase),(unsigned long)context->input_phase.maximum_us,
        (unsigned long)cartridge_phase_average(&context->audio_phase),(unsigned long)context->audio_phase.maximum_us,
        (unsigned long)cartridge_phase_average(&context->present_phase),(unsigned long)context->present_phase.maximum_us,
        (unsigned long)cartridge_phase_average(&context->display_reuse_phase),(unsigned long)context->display_reuse_phase.maximum_us,
        (unsigned long)cartridge_phase_average(&context->display_transform_phase),(unsigned long)context->display_transform_phase.maximum_us,
        (unsigned long)cartridge_phase_average(&context->display_handoff_phase),(unsigned long)context->display_handoff_phase.maximum_us,
        (unsigned long)context->deadline_rebases,
        (unsigned long long)context->audio_clock.discarded_us);
}

static bool cartridge_present(void *opaque)
{
    cartridge_run_context_t *const context = opaque;
    if (context == NULL) {
        return false;
    }
    const int64_t present_start=esp_timer_get_time();
    if(context->poll_completed_us!=0){
        cartridge_phase_record(&context->render_phase,present_start-context->poll_completed_us);
        context->poll_completed_us=0;
    }
    esp_err_t result;
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    if (context->high_res_video) {
        result = platform_display_submit_game_content_rgb565(
            s_pixels, P4_GAME_SURFACE_HIGH_RES_WIDTH,
            CONSOLE_SUBMIT_TIMEOUT_MS);
    } else
#endif
    {
        result = platform_display_submit_rgb565(
            s_pixels, P4_GAME_SURFACE_WIDTH,
            CONSOLE_SUBMIT_TIMEOUT_MS);
    }
    if(context->frame_clock.started){
        cartridge_phase_record(&context->present_phase,esp_timer_get_time()-present_start);
        platform_display_stats_t display={0};
        if(platform_display_get_stats(&display)==ESP_OK){
            cartridge_phase_record(&context->display_reuse_phase,display.pipeline_reuse_wait_last_us);
            cartridge_phase_record(&context->display_transform_phase,display.pipeline_transform_last_us);
            cartridge_phase_record(&context->display_handoff_phase,display.pipeline_handoff_last_us);
        }
    }
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    if (result == ESP_ERR_TIMEOUT) {
        if (context->display_ack_misses != UINT32_MAX) {
            ++context->display_ack_misses;
        }
        if (context->display_ack_misses == 1U ||
            context->display_ack_misses % 300U == 0U) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS CARTRIDGE_FRAME_ACK_MISSED "
                     "app=%s action=continue backlight_preserved=1 "
                     "count=%lu",
                     context->game_id,
                     (unsigned long)context->display_ack_misses);
        }
        return true;
    }
#endif
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
    if (context->multiplayer_running) {
        native_multiplayer_poll();
    }
    /* Loading and the initial presentation are outside the gameplay clock. */
    if(!context->frame_clock.started){
        context->last_wake=xTaskGetTickCount();
        const int64_t clock_start = esp_timer_get_time();
        cartridge_frame_clock_start(&context->frame_clock, clock_start);
        cartridge_audio_clock_start(&context->audio_clock, clock_start);
    }
    uint32_t interval_ticks = 0U;
    if (p4_tick_scheduler_next(
            &context->frame_scheduler, &interval_ticks) !=
            P4_SCHEDULER_OK || interval_ticks == 0U) {
        return false;
    }
    vTaskDelayUntil(&context->last_wake, (TickType_t)interval_ticks);
    const TickType_t after_wake = xTaskGetTickCount();
    const uint32_t late_ticks = cartridge_deadline_late_ticks(
        (uint32_t)after_wake,(uint32_t)context->last_wake);
    if (late_ticks != 0U) {
        if (context->frame_overruns != UINT32_MAX) {
            ++context->frame_overruns;
        }
        const uint64_t late_ms =
            (uint64_t)late_ticks * UINT64_C(1000) / configTICK_RATE_HZ;
        if (late_ms > context->max_frame_late_ms) {
            context->max_frame_late_ms = late_ms > UINT32_MAX
                ? UINT32_MAX : (uint32_t)late_ms;
        }
    }
    if(late_ticks!=0U){
        context->last_wake=after_wake;
        if(context->deadline_rebases!=UINT32_MAX)++context->deadline_rebases;
        vTaskDelay(1U); /* Yield after a missed deadline; never spin to catch up. */
    }
    *out_elapsed_ms=cartridge_frame_clock_delta(&context->frame_clock,
        esp_timer_get_time(),P4_GAME_MAX_FRAME_DELTA_MS);
    if (*out_elapsed_ms > context->max_frame_ms) {
        context->max_frame_ms = *out_elapsed_ms;
    }
    const int64_t input_start=esp_timer_get_time();
#if P4_CONSOLE_GAMEPAD_INPUT
    platform_gamepad_snapshot_t gamepad;
    uint32_t digital_buttons = read_gamepad_snapshot(&gamepad)
        ? gamepad_p4_buttons(&gamepad.state) : 0U;
#else
    uint32_t digital_buttons = 0U;
#endif
#if P4_CONSOLE_USB_INPUT
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
    cartridge_phase_record(&context->input_phase,esp_timer_get_time()-input_start);
    const int64_t audio_start=esp_timer_get_time();
    if (context->audio_running && !context->audio_worker) {
        uint32_t remaining = cartridge_audio_clock_budget(&context->audio_clock,
            audio_start, P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ);
        while (remaining != 0U && context->audio_running) {
            const uint32_t chunk = cartridge_audio_take_chunk(&remaining,
                CONSOLE_NATIVE_AUDIO_FRAMES_MAX);
            context->audio_running = pump_native_audio(
                &context->audio, &context->mixer, chunk);
        }
    }
    cartridge_phase_record(&context->audio_phase,esp_timer_get_time()-audio_start);
    if (context->frames != UINT32_MAX) {
        ++context->frames;
    }
    if (context->frames != 0U && context->frames % 300U == 0U) {
        cartridge_log_timing(context);
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
                 (unsigned long)cartridge_audio_frames(context));
#elif P4_CONSOLE_GAMEPAD_INPUT
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS CARTRIDGE_STATS app=%s frames=%lu "
                 "gamepad=%u input_polls=%lu gamepad_failures=%lu "
                 "audio_running=%u audio_frames=%lu",
                 context->game_id, (unsigned long)context->frames,
                 s_gamepad_connected ? 1U : 0U,
                 (unsigned long)s_gamepad_polls,
                 (unsigned long)s_gamepad_poll_failures,
                 context->audio_running ? 1U : 0U,
                 (unsigned long)cartridge_audio_frames(context));
#else
        p4_audio_mixer_stats_t stats = {0};
        if(context->audio_worker){
            p4_game_audio_worker_stats_t worker={0};p4_game_audio_worker_stats(context->audio_worker,&worker);stats=worker.mixer;
        } else p4_audio_mixer_get_stats(&context->mixer, &stats);
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS CARTRIDGE_STATS app=%s frames=%lu "
                 "touch_polls=%lu touch_failures=%lu audio_running=%u "
                 "tones=%lu audio_frames=%lu",
                 context->game_id, (unsigned long)context->frames,
                 (unsigned long)__atomic_load_n(
                     &s_touch_polls, __ATOMIC_RELAXED),
                 (unsigned long)__atomic_load_n(
                     &s_touch_poll_failures, __ATOMIC_RELAXED),
                 context->audio_running ? 1U : 0U,
                 (unsigned long)stats.tones_started,
                 (unsigned long)cartridge_audio_frames(context));
#endif
    }
    context->poll_completed_us=esp_timer_get_time();
    return true;
}

static void cartridge_finished(void *opaque, p4_game_result_t result)
{
#if P4_CONSOLE_BLE_MULTIPLAYER
    platform_dice_ble_close();
#endif
    cartridge_run_context_t *const context = opaque;
    if (context != NULL) {
        context->game_result = result;
        context->finished = true;
    }
}

/* Show identity during real loading work, without a cosmetic delay. Restore
 * navigation immediately so every rejected/failed launch retains its page. */
static void present_game_loading(console_shell_t *shell, uint32_t app_id)
{
#if CONFIG_P4_BOARD_M5STACK_TAB5
    const console_page_t previous_page = shell->page;
    const uint32_t previous_app = shell->active_app_id;
    const bool previous_pointer = shell->pointer_visible;
    const uint16_t previous_focus = shell->ng_focus;
    const uint16_t previous_focus_ms = shell->ng_focus_ms;
    shell->page = CONSOLE_PAGE_EXTERNAL;
    shell->active_app_id = app_id;
    shell->pointer_visible = false;
    shell->dirty = true;
    const esp_err_t frame_result = present_interactive(shell);
    shell->page = previous_page;
    shell->active_app_id = previous_app;
    shell->pointer_visible = previous_pointer;
    shell->ng_focus = previous_focus;
    shell->ng_focus_ms = previous_focus_ms;
    shell->dirty = true;
    console_shell_invalidate_native_cache(shell);
    if (frame_result != ESP_OK) {
        ESP_LOGW(TAG, "P4_CONSOLE_OS GAME_COVER_DEGRADED id=%lu error=%s",
                 (unsigned long)app_id, esp_err_to_name(frame_result));
    }
#else
    (void)shell;
    (void)app_id;
#endif
}

static esp_err_t run_stored_game(
    console_shell_t *shell,
    const platform_game_catalog_entry_t *game)
{
    if (shell == NULL || game == NULL || !game->valid ||
        s_pixels == NULL || !s_display_initialized) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint32_t capabilities = game->package.required_capabilities |
        game->package.optional_capabilities;
    const bool high_res_requested =
        (capabilities & P4_GAME_CAP_VIDEO_HIGH_RES) != 0U;
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    const bool high_res_video = high_res_requested;
#else
    if ((game->package.required_capabilities &
         P4_GAME_CAP_VIDEO_HIGH_RES) != 0U) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS CARTRIDGE_UNSUPPORTED app=%s "
                 "capability=video-highres",
                 game->package.id);
        return ESP_ERR_NOT_SUPPORTED;
    }
    const bool high_res_video = false;
    (void)high_res_requested;
#endif
    const uint16_t surface_width = high_res_video
        ? P4_GAME_SURFACE_HIGH_RES_WIDTH : P4_GAME_SURFACE_WIDTH;
    const uint16_t surface_height = high_res_video
        ? P4_GAME_SURFACE_HIGH_RES_HEIGHT : P4_GAME_SURFACE_HEIGHT;
    if (protected_game_lineage_check(&game->package) ==
        P4_PROTECTED_GAME_REJECTED) {
        ESP_LOGE(TAG,
                 "P4_CONSOLE_OS PROTECTED_GAME_REJECTED id=%s "
                 "operation=launch reason=payload-lineage",
                 game->package.id);
        return ESP_ERR_INVALID_STATE;
    }
    present_game_loading(shell, game->package.launcher_id);
    const bool multiplayer_ready = s_native_multiplayer.active &&
        s_multiplayer_native_launcher_id == game->package.launcher_id &&
        strcmp(s_multiplayer_local_offer.game_id, game->package.id) == 0;
    cartridge_run_context_t *const context = heap_caps_calloc(
        1U, sizeof(*context), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (context == NULL) {
        return ESP_ERR_NO_MEM;
    }
    *context = (cartridge_run_context_t){
        .game_id = game->package.id,
        .achievements = &s_achievements,
        .game_result = P4_GAME_ERROR,
        .last_wake = xTaskGetTickCount(),
        .multiplayer_running = multiplayer_ready,
        .high_res_video = high_res_video,
    };
    if (p4_tick_scheduler_init(
            &context->frame_scheduler, configTICK_RATE_HZ,
            CONSOLE_GAME_UPDATE_HZ) != P4_SCHEDULER_OK) {
        heap_caps_free(context);
        return ESP_ERR_INVALID_STATE;
    }
    p4_audio_mixer_init(&context->mixer);
    p4_game_platform_audio_init(&context->audio);
    if ((capabilities & P4_GAME_CAP_SAVE) != 0U) {
        context->save = cartridge_save_open(&game->package);
    }
    if ((capabilities &
         (P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_AUDIO_STREAM)) != 0U) {
        const esp_err_t audio_result = p4_game_platform_audio_open(
            &context->audio, native_audio_runtime_allowed(),
            native_audio_control_bus(),
            s_console_settings.game_volume_step);
        if (audio_result == ESP_OK) {
            context->audio_running = true;
#if CONFIG_P4_BOARD_M5STACK_TAB5
            const esp_err_t worker_result=p4_game_audio_worker_open(
                &context->audio_worker,&context->audio,&context->mixer);
            if(worker_result!=ESP_OK) {
                ESP_LOGW(TAG,"P4_CONSOLE_OS AUDIO_WORKER_UNAVAILABLE error=%s fallback=silent",esp_err_to_name(worker_result));
                cartridge_audio_close(context);
                context->audio_running=false;
            }
#endif
        } else if (context->audio.hardware_touched &&
                   (!context->audio.safe_high_proven ||
                    context->audio.backend != NULL)) {
            halt_dark("cartridge-audio-open-safety", audio_result);
        }
    }
    p4_game_input_mapper_init(&context->input_mapper);
#if P4_CONSOLE_SIGNAL_SCAN
    const bool signal_scan_ready = platform_signal_scan_ready();
#else
    const bool signal_scan_ready = false;
#endif
    const bool save_ready = context->save != NULL &&
        context->save->worker != NULL &&
        p4_game_save_service_available(&context->save->service);
#if P4_CONSOLE_BLE_MULTIPLAYER
    const bool dice_ready = p4_mp_game_dice_service_allowed(&game->package,
        s_dice_ready, multiplayer_ready,
        s_multiplayer_session.role == P4_MP_ROLE_HOST, s_multiplayer_launch_shared_dice);
#endif
    p4_cartridge_host_v1_t host = {
        .magic = P4_CARTRIDGE_HOST_MAGIC,
        .api_version = P4_CARTRIDGE_HOST_API_VERSION,
        .struct_bytes = sizeof(host),
        .available_capabilities = P4_GAME_CAP_VIDEO |
#if CONFIG_P4_BOARD_M5STACK_TAB5
            P4_GAME_CAP_MOTION |
#endif
            P4_GAME_CAP_CONTROLS |
            (high_res_video ? P4_GAME_CAP_VIDEO_HIGH_RES : 0U) |
#if P4_CONSOLE_BLE_MULTIPLAYER
            (dice_ready ? P4_GAME_CAP_DICE_ACCESSORY : 0U) |
#endif
            (context->audio_running
                ? P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_AUDIO_STREAM : 0U) |
            (signal_scan_ready ? P4_GAME_CAP_SIGNAL_SCAN : 0U) |
            (save_ready ? P4_GAME_CAP_SAVE : 0U) |
            (multiplayer_ready
                ? P4_GAME_CAP_MULTIPLAYER_SESSION : 0U),
#if P4_CONSOLE_BLE_MULTIPLAYER
        .dice_exchange_v2 = dice_ready ? platform_dice_ble_exchange : NULL,
#endif
#if CONFIG_P4_BOARD_M5STACK_TAB5
        .read_motion = cartridge_read_motion,
#endif
        .expected_game_id = game->package.id,
        .surface = {
            .pixels = s_pixels,
            .stride_pixels = surface_width,
            .width = surface_width,
            .height = surface_height,
        },
        .context = context,
        .poll_frame = cartridge_poll_frame,
        .present = cartridge_present,
        .play_tone = context->audio_running ? cartridge_play_tone : NULL,
        .submit_pcm16_stereo = context->audio_running
            ? cartridge_submit_pcm16_stereo : NULL,
        .stop_audio = context->audio_running ? cartridge_stop_audio : NULL,
        .finished = cartridge_finished,
        .unlock_achievement = cartridge_unlock_achievement,
#if P4_CONSOLE_SIGNAL_SCAN
        .request_signal_scan = signal_scan_ready
            ? cartridge_request_signal_scan : NULL,
        .read_signal_scan = signal_scan_ready
            ? cartridge_read_signal_scan : NULL,
#endif
        .save_data = save_ready &&
            context->save->service.launch_snapshot_bytes != 0U
            ? context->save->service.launch_snapshot : NULL,
        .save_bytes = save_ready
            ? context->save->service.launch_snapshot_bytes : 0U,
        .save_schema_version = save_ready
            ? context->save->service.launch_schema_version : 0U,
        .save_sequence = save_ready
            ? context->save->service.launch_sequence : 0U,
        .queue_save = save_ready ? cartridge_queue_save : NULL,
        .read_save_status = save_ready
            ? cartridge_read_save_status : NULL,
        .multiplayer_read_status = multiplayer_ready
            ? cartridge_multiplayer_read_status : NULL,
        .multiplayer_send = multiplayer_ready
            ? cartridge_multiplayer_send : NULL,
        .multiplayer_receive = multiplayer_ready
            ? cartridge_multiplayer_receive : NULL,
        .multiplayer_profile = multiplayer_ready
            ? &game->package.multiplayer_profile : NULL,
    };
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS CARTRIDGE_START app=%s file=%s api=1 "
             "source=%s runtime=psram-elf audio=%s signals=%s saves=%s "
             "multiplayer=%s surface=%ux%u "
             "save_bytes=%u save_sequence=%lu",
             game->package.id, game->file_name,
             game->in_games_directory
                ? "microsd-games-directory" : "microsd-root-compat",
             context->audio_running ? "ready" : "silent",
             signal_scan_ready ? "ready" : "offline",
             save_ready ? "ready" : "session-only",
             multiplayer_ready ? "connected" : "offline",
             (unsigned)surface_width, (unsigned)surface_height,
             save_ready
                 ? (unsigned)context->save->service.launch_snapshot_bytes : 0U,
             save_ready
                 ? (unsigned long)context->save->service.launch_sequence : 0UL);
    log_memory_health("cartridge-start");
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    /* Cartridges retain their direct 60 Hz GT911 reads. Join the launcher
     * sampler first so no two tasks ever access the driver concurrently. */
    const esp_err_t touch_mailbox_stop_result = stop_touch_mailbox();
    if (touch_mailbox_stop_result != ESP_OK) {
        if (multiplayer_ready) {
            native_multiplayer_end();
        }
        cartridge_audio_close(context);
        (void)cartridge_save_close(context->save, game->package.id);
        heap_caps_free(context);
        return touch_mailbox_stop_result;
    }
#endif
    esp_err_t result = platform_game_loader_run(game, &host);
    if (multiplayer_ready) {
        native_multiplayer_end();
    }
    cartridge_audio_close(context);
    (void)cartridge_save_close(context->save, game->package.id);
    if (result == ESP_OK &&
        (!context->finished ||
         context->game_result != P4_GAME_EXIT_TO_LAUNCHER)) {
        result = ESP_FAIL;
    }
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    const esp_err_t touch_mailbox_start_result = start_touch_mailbox();
    if (touch_mailbox_start_result != ESP_OK) {
        /* Launcher falls back to serialized foreground polling. The direct
         * game contract already completed, so this is non-fatal. */
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS TOUCH_MAILBOX_DEGRADED stage=cartridge-return "
                 "fallback=launcher-direct error=%s",
                 esp_err_to_name(touch_mailbox_start_result));
    }
#endif
    console_shell_set_achievement_catalog(shell, &s_achievements);
    (void)console_shell_set_save_catalog(shell, &s_saves);
    console_shell_show_home(shell);
    const esp_err_t home_result = present_interactive(shell);
    if (home_result != ESP_OK) {
        halt_dark("cartridge-return-home", home_result);
    }
    p4_audio_mixer_stats_t mixer_stats = {0};
    p4_audio_mixer_get_stats(&context->mixer, &mixer_stats);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS CARTRIDGE_STOP app=%s result=%s frames=%lu "
             "deadline_misses=%lu max_frame_ms=%lu max_late_ms=%lu "
             "display_ack_misses=%lu return=launcher audio_frames=%lu "
             "audio_stream_underrun_frames=%lu amp_safe=%u",
             game->package.id, esp_err_to_name(result),
             (unsigned long)context->frames,
             (unsigned long)context->frame_overruns,
             (unsigned long)context->max_frame_ms,
             (unsigned long)context->max_frame_late_ms,
             (unsigned long)context->display_ack_misses,
             (unsigned long)context->audio.frames_written,
             (unsigned long)mixer_stats.stream_underrun_frames,
             context->audio.hardware_touched
                ? (context->audio.safe_high_proven ? 1U : 0U) : 1U);
    cartridge_log_timing(context);
    heap_caps_free(context);
    log_memory_health("cartridge-stop");
    return result;
}

static void launch_doom_exclusive(
    console_shell_t *shell,
    platform_game_storage_doom_title_t title,
    const p4_doom_mp_launch_config_t *multiplayer)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    /* Doom recreates the shared I2C/touch stack after the one-way handoff.
     * Never let a controller or USB input bypass a failed persistent-config
     * restoration and make that independent runtime poll an unknown state. */
    if (!s_gt911_reviewed_baseline_verified) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS HANDOFF_REJECTED app=doom-or-chex "
                 "reason=gt911-baseline-not-verified");
        return;
    }
#endif
    if (title >= PLATFORM_GAME_STORAGE_DOOM_TITLE_COUNT ||
        (multiplayer != NULL &&
        (!multiplayer->enabled ||
         !p4_doom_mp_launch_config_valid(multiplayer) ||
         s_multiplayer_session.state != P4_MP_SESSION_CONNECTED ||
         (multiplayer->setup.game == P4_DOOM_MP_GAME_GAME_CHANGERS_AI) !=
             (title == PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI) ||
         (multiplayer->setup.game == P4_DOOM_MP_GAME_CHEX_QUEST) !=
             (title ==
              PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST)))) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS MULTIPLAYER_LAUNCH_REJECTED "
                 "reason=session-not-ready");
        return;
    }
    present_game_loading(shell,
        title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
            ? CONSOLE_APP_CHEX_QUEST : CONSOLE_APP_DOOM);
    const bool title_already_verified =
        title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
            ? s_game_storage_status.chex_quest_ready
            : s_game_storage_status.doom_wad_ready;
    if (!title_already_verified) {
#if !CONFIG_P4_BOARD_M5STACK_TAB5
        const char *const label =
            title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
                ? "VERIFYING CHEX SHA-256" : "VERIFYING DOOM SHA-256";
        const esp_err_t frame_result = present_boot_screen(
            CONSOLE_BOOT_ANIMATION_STEPS, label);
        if (frame_result != ESP_OK) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS WAD_VERIFY_FRAME_DEGRADED error=%s",
                     esp_err_to_name(frame_result));
        }
#endif
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS WAD_VALIDATION_ON_DEMAND title=%s "
                 "trigger=launch full_sha256=1 boot_scan=0",
                 title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
                    ? "chex" : "doom");
    }
    esp_err_t result = p4_doom_p4mp_prepare(
        multiplayer != NULL ? &s_multiplayer_session : NULL,
        multiplayer,
        multiplayer != NULL ? &s_doom_multiplayer_transport : NULL);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS MULTIPLAYER_LAUNCH_REJECTED "
                 "reason=engine-adapter error=%s",
                 esp_err_to_name(result));
        return;
    }
    result = platform_game_storage_lock_for_doom_title(title);
    sync_game_storage();
    if (result != ESP_OK) {
        if (multiplayer != NULL) {
            (void)p4_doom_p4mp_prepare(NULL, NULL, NULL);
            if (multiplayer_transport_set_handler(
                    multiplayer_frame_received, NULL) != ESP_OK) {
                reset_multiplayer_lobby("handoff-storage-handler-restore");
            }
        }
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS HANDOFF_REJECTED app=%s storage=%s "
                 "error=%s action=eject-usb-and-retry",
                 title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
                    ? "chex-quest" : "doom",
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
             "P4_CONSOLE_OS HANDOFF_BEGIN app=%s mode=exclusive-one-way "
             "audio_owner=doom storage=game-locked input_owner=platform "
             "multiplayer=%u",
             title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
                ? "chex-quest" : "doom",
             multiplayer != NULL ? 1U : 0U);
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
             "P4_CONSOLE_OS HANDOFF_COMPLETE app=%s shell_services=released",
             title == PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
                ? "chex-quest" : "doom");
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    console_os_launch_doom(title, multiplayer);
#else
    console_os_launch_doom(
        s_console_settings.game_volume_step,
        title,
        multiplayer);
#endif
    halt_dark("doom-returned-without-reentrant-teardown",
              ESP_ERR_INVALID_STATE);
}

void app_main(void)
{
    p4_achievement_catalog_init(&s_achievements);
    p4_save_catalog_init(&s_saves, false);
    p4_mp_session_init(&s_multiplayer_session);
    p4_mp_start_barrier_init(&s_multiplayer_start_barrier);
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
#elif CONFIG_P4_BOARD_M5STACK_TAB5
             board->slug, "auto-detected-multitouch", "microsd",
#elif CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
      CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE
             board->slug, "gt911-touch+usb-hid+ble-hid", "microsd",
#elif CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
             board->slug, "gt911-touch+ble-hid", "microsd",
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
             "brand=gamechangers-ai format=rgb565 "
             "source=reviewed-joystick-mark product=GameChangersAI-OS version=" CONSOLE_PRODUCT_VERSION " storage=pending");
    const esp_err_t settings_result =
        platform_console_settings_init(&s_console_settings);
    if (settings_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS SETTINGS_DEGRADED defaults=1 error=%s",
                 esp_err_to_name(settings_result));
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    start_boot_animation();
#endif
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    initialize_battery();
#endif
#if P4_CONSOLE_GAMEPAD_INPUT
    gamepad_button_mapping_default(&s_controller_mapping_current);
    s_controller_mapping_pending = s_controller_mapping_current;
    const esp_err_t mapping_result = platform_gamepad_set_mapping(
        &s_console_settings.controller_mapping);
    if (mapping_result == ESP_OK) {
        s_controller_mapping_current =
            s_console_settings.controller_mapping;
        s_controller_mapping_pending = s_controller_mapping_current;
        s_controller_mapping_persistent =
            s_console_settings.persistent;
        s_controller_mapping_last_error = ESP_OK;
    } else {
        (void)platform_gamepad_set_mapping(
            &s_controller_mapping_current);
        s_controller_mapping_persistent = false;
        s_controller_mapping_last_error = mapping_result;
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS CONTROLLER_MAPPING_DEGRADED "
                 "fallback=default error=%s",
                 esp_err_to_name(mapping_result));
    }
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
    const esp_err_t ble_multiplayer_prepare =
        platform_multiplayer_ble_prepare();
    const esp_err_t dice_prepare = platform_dice_ble_prepare();
    s_dice_ready = dice_prepare == ESP_OK;
    if (dice_prepare != ESP_OK) {
        ESP_LOGW(TAG, "DICE_PREPARE_DEGRADED error=%s", esp_err_to_name(dice_prepare));
    }
    if (ble_multiplayer_prepare != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BLE_PREPARE_DEGRADED client=multiplayer "
                 "error=%s",
                 esp_err_to_name(ble_multiplayer_prepare));
    }
#endif
#if P4_CONSOLE_BLE_GAMEPAD
    const esp_err_t ble_gamepad_prepare = platform_gamepad_ble_prepare();
    if (ble_gamepad_prepare != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BLE_PREPARE_DEGRADED client=gamepad "
                 "error=%s",
                 esp_err_to_name(ble_gamepad_prepare));
    }
#endif
#if P4_CONSOLE_BLE_GAMEPAD && P4_CONSOLE_BLE_MULTIPLAYER
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BLE_LINK_BUDGET "
             "controller_links=1 multiplayer_peer_links=1 "
             "host=shared order=pair-controller-before-lobby");
#endif
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    (void)create_shared_control_bus_or_continue();
#endif
    const esp_err_t storage_start_result =
        start_game_storage_initialization();
    if (storage_start_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS STORAGE_INIT_START_DEGRADED error=%s",
                 esp_err_to_name(storage_start_result));
    }
    const esp_err_t catalog_start_result = storage_start_result == ESP_OK
        ? start_game_catalog_scan() : storage_start_result;
    if (catalog_start_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS GAME_CATALOG_SCAN_DEGRADED "
                 "stage=start error=%s fallback=foreground",
                 esp_err_to_name(catalog_start_result));
    }
    play_boot_chime();
#if P4_CONSOLE_USB_INPUT
    begin_usb_enum_probe_or_suppress();
    create_usb_input_or_continue();
#endif
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    create_touch_or_continue();
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    if (s_touch_ready) {
        const esp_err_t touch_mailbox_result = start_touch_mailbox();
        if (touch_mailbox_result != ESP_OK) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS TOUCH_MAILBOX_DEGRADED fallback=launcher-direct "
                     "error=%s",
                     esp_err_to_name(touch_mailbox_result));
        }
    }
#endif
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
    if (catalog_start_result == ESP_OK) {
        wait_for_game_catalog_with_boot_animation();
        rebuild_shell_registry(shell);
    } else if (catalog_needs_reload()) {
        (void)present_boot_screen(
            CONSOLE_BOOT_ANIMATION_STEPS, "CONNECTING DOORS");
        (void)reload_game_catalog();
        rebuild_shell_registry(shell);
    }
#if !CONFIG_P4_BOARD_M5STACK_TAB5
    result = present_boot_screen(
        CONSOLE_BOOT_ANIMATION_STEPS, "CONNECT 2400");
    if (result != ESP_OK) {
        halt_dark("bbs-connect-frame", result);
    }
    vTaskDelay(pdMS_TO_TICKS(CONSOLE_BBS_CONNECT_HOLD_MS));
#endif
    const console_shell_runtime_info_t initial_runtime = runtime_info();
    console_shell_set_runtime_info(shell, &initial_runtime);
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    result = present_home_reveal(shell);
#else
    result = present(shell);
#endif
    if (result != ESP_OK) {
        halt_dark("first-frame", result);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS LAUNCHER_READY_BEFORE_WAD_SCAN_COMPLETE "
             "native_catalog_valid=%u wad_policy=verify-on-demand "
             "doom=%s chex=%s",
             (unsigned)s_game_catalog.valid_count,
             s_game_storage_status.doom_wad_ready
                ? "ready" : "not-verified",
             s_game_storage_status.chex_quest_ready
                ? "ready" : "not-verified");
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS READY page=home display=hdmi "
             "audio=es8311-deferred-until-app "
             "storage=microsd removal=power-off-first");
#elif CONFIG_P4_BOARD_M5STACK_TAB5
    ESP_LOGI(TAG, "P4_CONSOLE_OS INTERFACE version=" CONSOLE_PRODUCT_VERSION " renderer=nextgen native=1280x720 legacy_theme=0");
    ESP_LOGI(TAG, "P4_CONSOLE_OS READY board=m5stack-tab5 display=mipi-dsi audio=es8388 storage=microsd removal=power-off-first hardware_acceptance=pending");
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
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS OTA_BOOT_VALID state=deferred stable_ms=%u "
             "reason=post-service-health-gate",
             (unsigned)CONSOLE_RUNTIME_HEALTH_CONFIRM_MS);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS MAIN_STACK stage=core-ready low_water_bytes=%u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    log_memory_health("core-ready");
    const esp_err_t multiplayer_result = p4_mp_uart_endpoint_init(
        multiplayer_frame_received, NULL);
    s_multiplayer_uart_ready = multiplayer_result == ESP_OK;
    s_multiplayer_transport_ready = multiplayer_result == ESP_OK;
    const esp_err_t lobby_result = initialize_multiplayer_lobby();
    if (lobby_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS MULTIPLAYER_LOBBY_DEGRADED error=%s",
                 esp_err_to_name(lobby_result));
    }
#if P4_CONSOLE_BLE_GAMEPAD && P4_CONSOLE_BLE_MULTIPLAYER
    p4_ble_radio_handoff_init(&s_ble_radio_handoff);
#endif
#if P4_CONSOLE_BLE_GAMEPAD
    const platform_gamepad_ble_status_t saved_pad =
        platform_gamepad_ble_status();
    if (s_console_settings.ble_controller_enabled && saved_pad.bonded) {
        const esp_err_t reconnect_result =
            start_ble_gamepad_connection();
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BLE_GAMEPAD_RECONNECT queued=%u result=%s",
                 reconnect_result == ESP_OK ? 1U : 0U,
                 esp_err_to_name(reconnect_result));
    }
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER && P4_BLE_DIAGNOSTIC_AUTOSTART
    ESP_LOGW(TAG,
             "P4_CONSOLE_OS BLE_DIAGNOSTIC_AUTOSTART stage=queued");
    s_multiplayer_ble_enable_pending = true;
#endif
    if (multiplayer_result == ESP_OK) {
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
        const p4_content_transfer_transport_t content_transport = {
            .send = content_uart_send,
            .wait_tx = content_uart_wait_tx,
            .set_baud = content_uart_set_baud,
            .context = NULL,
#if CONFIG_P4_BOARD_M5STACK_TAB5
            .idle_baud = 115200U,
#else
            .idle_baud = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
#endif
        };
        esp_err_t content_result = p4_content_transfer_init(
            PLATFORM_GAME_STORAGE_MOUNT_POINT, &content_transport);
        if (content_result == ESP_OK) {
            content_result = p4_file_transfer_init(
                PLATFORM_GAME_STORAGE_MOUNT_POINT, &content_transport);
        }
#if P4_CONSOLE_H1_USB_DRIVE_CONTROL
        if (content_result == ESP_OK && !p4_h1_usb_drive_control_init(
                &s_h1_usb_drive_control, h1_usb_drive_send,
                h1_usb_drive_status, h1_usb_drive_set_mode, NULL)) {
            content_result = ESP_FAIL;
        }
#endif
#if CONFIG_P4_BOARD_M5STACK_TAB5
        s_clock_control=(p4_clock_control_t){
            .send=clock_usb_send, .status=clock_usb_status, .set=clock_usb_set,
        };
#endif
        if (content_result == ESP_OK) {
            content_result = p4_mp_uart_endpoint_set_raw_handler(
                content_uart_consume, NULL);
        }
        if (content_result != ESP_OK) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS H1_CONTENT_DEGRADED error=%s",
                     esp_err_to_name(content_result));
        } else {
#if P4_CONSOLE_H1_USB_DRIVE_CONTROL
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS H1_USB_DRIVE_CONTROL_READY "
                     "protocol=%u transport=h1-ch343 "
                     "auth=physical-local-only screen=unchanged",
                     (unsigned)P4_H1_USB_DRIVE_PROTOCOL_VERSION);
#endif
        }
#endif
    } else {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS MULTIPLAYER_LINK_DEGRADED "
                 "transport=direct-uart+h1-relay error=%s",
                 esp_err_to_name(multiplayer_result));
    }
#if P4_CONSOLE_SIGNAL_SCAN
    const esp_err_t signal_scan_result = platform_signal_scan_start();
    if (signal_scan_result == ESP_OK) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS SIGNAL_SCAN_START stage=post-ready "
                 "mode=passive-only");
    } else {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS SIGNAL_SCAN_DEGRADED error=%s",
                 esp_err_to_name(signal_scan_result));
    }
#endif

    p4_tick_scheduler_t console_scheduler;
    if (p4_tick_scheduler_init(
            &console_scheduler, configTICK_RATE_HZ,
            CONSOLE_GAME_UPDATE_HZ) != P4_SCHEDULER_OK) {
        halt_dark("console-scheduler", ESP_ERR_INVALID_STATE);
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    const esp_err_t sensors_start = platform_tab5_sensors_start();
    if (sensors_start != ESP_OK) ESP_LOGW(TAG, "SENSORS_START error=%s", esp_err_to_name(sensors_start));
#endif
    s_runtime_services_ready_us = esp_timer_get_time();
    TickType_t last_wake = xTaskGetTickCount();
    int64_t next_storage_sync_us = s_runtime_services_ready_us;
    int64_t next_runtime_info_us = s_runtime_services_ready_us;
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    int64_t next_battery_sample_us = s_runtime_services_ready_us +
        (int64_t)CONSOLE_BATTERY_SAMPLE_INTERVAL_MS * INT64_C(1000);
#endif
#if CONSOLE_OS_ENABLE_RUNTIME_STATS
    int64_t next_stats_us = s_runtime_services_ready_us +
        (int64_t)CONSOLE_STATS_INTERVAL_MS * INT64_C(1000);
#endif
    int64_t shell_animation_last_us = s_runtime_services_ready_us;
    for (;;) {
        ++s_loop_count;
#if P4_CONSOLE_BLE_GAMEPAD && P4_CONSOLE_BLE_MULTIPLAYER
        poll_ble_gamepad_lobby_restore();
#endif
        poll_multiplayer_link(shell);
        if (p4_content_transfer_info().busy ||
            p4_file_transfer_info().busy) {
            /* H1 provisioning owns UART and the mounted FAT volume until its
             * verified staging transaction finishes. */
            wait_for_console_tick(&console_scheduler, &last_wake);
            continue;
        }
        if (s_multiplayer_launch_due) {
            s_multiplayer_launch_due = false;
            p4_mp_start_barrier_cancel(&s_multiplayer_start_barrier);
    s_multiplayer_group_start=(p4_mp_group_start_t){0};
            s_multiplayer_next_start_ready_us = 0;
            if (s_multiplayer_launch_kind == CONSOLE_MP_LAUNCH_DOOM) {
                launch_doom_exclusive(
                    shell,
                    s_doom_multiplayer_launch.setup.game == P4_DOOM_MP_GAME_GAME_CHANGERS_AI
                        ? PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI
                        : s_doom_multiplayer_launch.setup.game ==
                            P4_DOOM_MP_GAME_CHEX_QUEST
                        ? PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
                        : PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM,
                    &s_doom_multiplayer_launch);
            } else if (s_multiplayer_launch_kind ==
                       CONSOLE_MP_LAUNCH_NATIVE) {
                const platform_game_catalog_entry_t *const game =
                    platform_game_catalog_find_launcher(
                        &s_game_catalog,
                        s_multiplayer_native_launcher_id);
                if (game == NULL || !native_multiplayer_begin()) {
                    ESP_LOGW(TAG,
                             "P4_CONSOLE_OS NATIVE_MULTIPLAYER_REJECTED "
                             "reason=launch-state");
                    reset_multiplayer_lobby("native-launch-invalid");
                } else {
                    const esp_err_t game_result =
                        run_stored_game(shell, game);
                    if (game_result != ESP_OK) {
                        ESP_LOGW(TAG,
                                 "P4_CONSOLE_OS NATIVE_GAME_DEGRADED "
                                 "app=%s error=%s return=launcher",
                                 game->package.id,
                                 esp_err_to_name(game_result));
                    }
                    reset_multiplayer_lobby("native-game-ended");
                }
            } else {
                reset_multiplayer_lobby("launch-kind-missing");
            }
            last_wake = xTaskGetTickCount();
            continue;
        }
#if P4_CONSOLE_USB_INPUT
        confirm_usb_enum_probe_after_stable_runtime();
#endif
        confirm_ota_after_stable_runtime();
        const int64_t service_now_us = esp_timer_get_time();
        if (service_now_us >= next_storage_sync_us) {
            sync_game_storage();
            next_storage_sync_us = service_now_us +
                (int64_t)CONSOLE_STORAGE_SYNC_INTERVAL_MS * INT64_C(1000);
        }
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        if (service_now_us >= next_battery_sample_us) {
            sample_battery(false);
            next_battery_sample_us = service_now_us +
                (int64_t)CONSOLE_BATTERY_SAMPLE_INTERVAL_MS * INT64_C(1000);
        }
#endif
        if (catalog_needs_reload()) {
            (void)reload_game_catalog();
            rebuild_shell_registry(shell);
            if (shell->page == CONSOLE_PAGE_GAMES) {
                (void)reload_manager_listing(
                    shell, CONSOLE_FILE_NOTICE_NONE);
            }
        }
#if P4_CONSOLE_BLE_MULTIPLAYER
        if (s_multiplayer_ble_enable_pending) {
            if (shell->page != CONSOLE_PAGE_MULTIPLAYER) {
                (void)release_multiplayer_ble_transport(
                    "deferred-page-left");
            } else {
                const esp_err_t ble_start_result =
                    request_ble_multiplayer_transport();
                if (ble_start_result != ESP_ERR_NOT_FINISHED) {
                    ESP_LOGI(
                        TAG,
                        "P4_CONSOLE_OS "
                        "MULTIPLAYER_TRANSPORT_DEFERRED_COMPLETE "
                        "requested=ble result=%s",
                        esp_err_to_name(ble_start_result));
                    const console_shell_runtime_info_t current_runtime =
                        runtime_info();
                    console_shell_set_runtime_info(
                        shell, &current_runtime);
                }
            }
        }
#endif
        if (shell->page == CONSOLE_PAGE_FILES &&
            file_listing_needs_reload()) {
            (void)reload_file_listing(
                shell, CONSOLE_FILE_NOTICE_NONE);
        }
        const int64_t shell_animation_now_us = esp_timer_get_time();
        uint32_t shell_elapsed_ms = 0U;
        if (shell_animation_now_us > shell_animation_last_us) {
            const uint64_t elapsed_us = (uint64_t)(
                shell_animation_now_us - shell_animation_last_us);
            shell_elapsed_ms = elapsed_us / UINT64_C(1000) > UINT32_MAX
                ? UINT32_MAX
                : (uint32_t)(elapsed_us / UINT64_C(1000));
        }
        shell_animation_last_us = shell_animation_now_us;
        bool animation_changed =
            console_shell_advance(shell, shell_elapsed_ms);
        const int32_t scroll_after_advance_q16 =
            shell->home_scroll_visual_q16;
        const console_page_t page_before_input = shell->page;
        const console_shell_action_t action = poll_input(shell);
        animation_changed = animation_changed ||
            shell->home_scroll_visual_q16 != scroll_after_advance_q16;
        if (animation_changed) {
            note_animation_frame(esp_timer_get_time());
        }
        const int64_t runtime_now_us = esp_timer_get_time();
        if (runtime_now_us >= next_runtime_info_us) {
            const console_shell_runtime_info_t current_runtime =
                runtime_info();
            console_shell_set_runtime_info(shell, &current_runtime);
            next_runtime_info_us = runtime_now_us +
                (int64_t)CONSOLE_RUNTIME_INFO_INTERVAL_MS * INT64_C(1000);
        }
        if (action.type == CONSOLE_ACTION_PAGE_CHANGED &&
            page_before_input == CONSOLE_PAGE_MULTIPLAYER &&
            shell->page != CONSOLE_PAGE_MULTIPLAYER) {
            if (s_multiplayer_lobby_state != CONSOLE_MP_LOBBY_BROWSING) {
                reset_multiplayer_lobby("multiplayer-page-left");
            }
#if P4_CONSOLE_WIFI_MULTIPLAYER
            if (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI)
                (void)select_multiplayer_transport(CONSOLE_MP_TRANSPORT_WIRED);
#endif
#if P4_CONSOLE_BLE_MULTIPLAYER
            (void)release_multiplayer_ble_transport(
                "multiplayer-page-left");
#endif
        }
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
        /* Launches leave the shell loop before a corresponding launcher
         * present. Do not let their touch time be consumed by a later return
         * frame after the game or lobby flow completes. */
        if (action.type == CONSOLE_ACTION_LAUNCH ||
            action.type == CONSOLE_ACTION_MULTIPLAYER_LAUNCH_GAME) {
            s_interactive_touch_pending_timestamp_us = 0;
        }
#endif
        if (action.type == CONSOLE_ACTION_COLOR_MODE_CHANGED) {
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS COLOR_MODE mode=%u persistence=session-only",
                     (unsigned)action.color_mode);
        } else if (action.type == CONSOLE_ACTION_STORAGE_CHECK ||
                   action.type == CONSOLE_ACTION_STORAGE_RETRY ||
                   action.type == CONSOLE_ACTION_STORAGE_REPAIR) {
            handle_storage_action(shell, &action);
#if P4_CONSOLE_GAMEPAD_INPUT
        } else if (action.type == CONSOLE_ACTION_CONTROLLER_BLE_ENABLE ||
                   action.type == CONSOLE_ACTION_CONTROLLER_BLE_DISABLE ||
                   action.type == CONSOLE_ACTION_CONTROLLER_PAIR ||
                   action.type == CONSOLE_ACTION_CONTROLLER_DISCONNECT ||
                   action.type == CONSOLE_ACTION_CONTROLLER_FORGET ||
                   action.type == CONSOLE_ACTION_CONTROLLER_MAPPING_START ||
                   action.type == CONSOLE_ACTION_CONTROLLER_MAPPING_CANCEL ||
                   action.type == CONSOLE_ACTION_CONTROLLER_MAPPING_RESET) {
            handle_controller_action(shell, &action);
#endif
        } else if (action.type == CONSOLE_ACTION_USB_MODE_ENABLE ||
                   action.type == CONSOLE_ACTION_USB_MODE_DISABLE) {
            handle_usb_mode_action(shell, &action);
        } else if (action.type == CONSOLE_ACTION_BOOT_VOLUME_SET ||
                   action.type == CONSOLE_ACTION_GAME_VOLUME_SET) {
            handle_audio_volume_action(shell, &action);
        } else if (action.type ==
                   CONSOLE_ACTION_MULTIPLAYER_CONFIGURE) {
            handle_multiplayer_config_action(shell, &action);
        } else if (action.type ==
                   CONSOLE_ACTION_MULTIPLAYER_LOBBY_SELECT) {
            (void)set_multiplayer_lobby_selection(
                shell, action.multiplayer_lobby_selection, false);
        } else if (action.type ==
                       CONSOLE_ACTION_MULTIPLAYER_CREATE_LOBBY ||
                   action.type ==
                       CONSOLE_ACTION_MULTIPLAYER_JOIN_LOBBY) {
            const bool create = action.type ==
                CONSOLE_ACTION_MULTIPLAYER_CREATE_LOBBY;
            esp_err_t lobby_action;
#if P4_CONSOLE_BLE_MULTIPLAYER
            if (s_multiplayer_ble_enable_pending) {
                lobby_action = ESP_ERR_NOT_FINISHED;
            } else
#endif
            {
                lobby_action = create
                    ? create_multiplayer_lobby()
                    : join_selected_multiplayer_lobby();
            }
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS MULTIPLAYER_LOBBY_ACTION "
                     "role=%s result=%s",
                     create ? "host" : "join",
                     esp_err_to_name(lobby_action));
        } else if (action.type ==
                   CONSOLE_ACTION_MULTIPLAYER_LOBBY_RESET) {
            reset_multiplayer_lobby("role-menu");
        } else if (action.type == CONSOLE_ACTION_PAGE_CHANGED &&
                   action.app_id == CONSOLE_APP_MULTIPLAYER &&
                   page_before_input != CONSOLE_PAGE_MULTIPLAYER) {
#if P4_CONSOLE_BLE_MULTIPLAYER
            if (s_multiplayer_transport !=
                    CONSOLE_MP_TRANSPORT_BLE) {
                const esp_err_t ble_default_result =
                    request_ble_multiplayer_transport();
                if (ble_default_result == ESP_ERR_NOT_FINISHED) {
                    ESP_LOGI(
                        TAG,
                        "P4_CONSOLE_OS MULTIPLAYER_DEFAULT "
                        "requested=ble active=wired result=deferred");
                } else {
                    ESP_LOGI(
                        TAG,
                        "P4_CONSOLE_OS MULTIPLAYER_DEFAULT "
                        "requested=ble active=%s result=%s",
                        s_multiplayer_transport ==
                                CONSOLE_MP_TRANSPORT_BLE
                            ? "ble" : (s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI ? "wifi-local" : "wired"),
                        esp_err_to_name(ble_default_result));
                }
                const console_shell_runtime_info_t current_runtime =
                    runtime_info();
                console_shell_set_runtime_info(
                    shell, &current_runtime);
            }
#endif
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS MULTIPLAYER_READY "
                     "wad_validation=deferred-until-selected-title-launch "
                     "native_validation=catalog-authoritative");
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
        } else if (action.type ==
                       CONSOLE_ACTION_MULTIPLAYER_LAUNCH_GAME) {
            const esp_err_t sync_result =
                begin_multiplayer_start_sync();
            if (sync_result != ESP_OK) {
                ESP_LOGW(TAG,
                         "P4_CONSOLE_OS MULTIPLAYER_START_SYNC "
                         "state=retrying error=%s",
                         esp_err_to_name(sync_result));
            }
        } else if (action.type == CONSOLE_ACTION_LAUNCH &&
            action.app_id == CONSOLE_APP_DOOM) {
            launch_doom_exclusive(
                shell, PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM, NULL);
        } else if (action.type == CONSOLE_ACTION_LAUNCH &&
                   action.app_id == CONSOLE_APP_CHEX_QUEST) {
            launch_doom_exclusive(
                shell, PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST, NULL);
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
            result = present_interactive(shell);
            if (result != ESP_OK) {
                halt_dark("frame-submit", result);
            }
        }
#if CONSOLE_OS_ENABLE_RUNTIME_STATS
        const int64_t stats_now_us = esp_timer_get_time();
        if (stats_now_us >= next_stats_us) {
            log_runtime_stats(shell);
            next_stats_us = stats_now_us +
                (int64_t)CONSOLE_STATS_INTERVAL_MS * INT64_C(1000);
        }
#endif
#if CONFIG_P4_BOARD_M5STACK_TAB5
        report_scroll_timing(shell,false);
#endif
        wait_for_console_tick(&console_scheduler, &last_wake);
    }
}
