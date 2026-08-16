// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * P4 Console OS MVP: a small FreeRTOS-native foreground shell. Applications
 * are statically registered and consume platform services; this is not a
 * dynamic executable loader. The first Doom integration is an exclusive,
 * one-way handoff because the imported Doom engine has no reviewed reentrant
 * teardown path yet.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "console/shell.h"
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
#include "p4/achievements.h"
#include "p4/audio.h"
#include "p4/content_catalog.h"
#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"
#include "p4/multiplayer.h"
#include "p4/quake.h"
#include "p4/content_transfer.h"
#include "p4/platform.h"
#include "p4_game_registry.h"
#include "platform/board.h"
#include "platform/display.h"
#include "platform/storage.h"
#include "platform/touch.h"
#include "platform_i2c_shared/bus.h"
#include "runtime_gate.h"

enum {
    CONSOLE_APP_DOOM = 1,
    CONSOLE_APP_COLORS = 2,
    CONSOLE_APP_TOUCH = 3,
    CONSOLE_APP_SYSTEM = 4,
    CONSOLE_APP_AUDIO = 5,
    CONSOLE_APP_ACHIEVEMENTS = 12,
    CONSOLE_APP_LIBRARY = 6,
    CONSOLE_APP_MULTIPLAYER = 7,
    CONSOLE_APP_QUAKE = 8,
    CONSOLE_APP_FILES = 9,
    CONSOLE_APP_SAVES = 10,
    CONSOLE_APP_TERMINAL = 11,
    /* Keep input and 16 kHz audio serviced every 16 ms.  Rendering every
     * second service tick gives native games a steady 31.25 FPS target. */
    CONSOLE_SERVICE_INTERVAL_MS = 16,
    CONSOLE_RENDER_DIVISOR = 2,
    CONSOLE_RENDER_TARGET_FPS =
        1000 / (CONSOLE_SERVICE_INTERVAL_MS * CONSOLE_RENDER_DIVISOR),
    CONSOLE_SUBMIT_TIMEOUT_MS = 250,
    CONSOLE_BACKLIGHT_PERCENT = 25,
    CONSOLE_CLEANUP_ATTEMPTS = 3,
    CONSOLE_CONTENT_SCAN_STACK_BYTES = 12 * 1024,
    CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK =
        P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ *
        CONSOLE_SERVICE_INTERVAL_MS / 1000,
    CONSOLE_BOOT_CHIME_TICKS = 44,
    CONSOLE_BOOT_ANIMATION_STEPS = 5,
    CONSOLE_BOOT_LOGO_WIDTH = 298,
    CONSOLE_BOOT_LOGO_HEIGHT = 43,
};

static const char *const TAG = "p4_console_os";
static platform_i2c_shared_t *s_shared_bus;
static platform_touch_t *s_touch;
static uint16_t *s_pixels;
static bool s_display_initialized;
static bool s_touch_ready;
static uint32_t s_loop_count;
static uint32_t s_touch_polls;
static uint32_t s_touch_poll_failures;
static uint32_t s_doom_handoff_count;
static bool s_storage_mounted;
static bool s_content_scan_complete;
static p4_content_catalog_t s_content_catalog;
static p4_content_catalog_t s_content_scan_staging;
static p4_content_status_t s_content_scan_result;
static TaskHandle_t s_content_scan_task;
static portMUX_TYPE s_content_scan_lock = portMUX_INITIALIZER_UNLOCKED;
typedef enum {
    CONTENT_SCAN_IDLE = 0,
    CONTENT_SCAN_RUNNING,
    CONTENT_SCAN_DONE,
} content_scan_state_t;
static content_scan_state_t s_content_scan_state;
static p4_mp_session_t s_multiplayer_session;
static p4_achievement_catalog_t s_achievements;
static int16_t s_native_audio_pcm[
    CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK *
    P4_GAME_PLATFORM_AUDIO_CHANNEL_COUNT];

extern const uint8_t _binary_gamechangers_ai_logo_rgb565_start[];

void console_os_launch_doom(uint8_t master_volume_step);

static console_app_descriptor_t s_apps[CONSOLE_SHELL_MAX_APPS];
static size_t s_app_count;

static const console_app_descriptor_t s_doom_app = {
    .id = CONSOLE_APP_DOOM,
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
};

static const console_app_descriptor_t s_quake_app = {
    .id = CONSOLE_APP_QUAKE,
    .title = "QUAKE",
    .subtitle = "SD SHAREWARE 1.06",
    .folder_path = "GAMES/ACTION",
    .accent_rgb565 = UINT16_C(0xFD20),
    .capabilities = CONSOLE_CAPABILITY_DISPLAY |
                    CONSOLE_CAPABILITY_TOUCH |
                    CONSOLE_CAPABILITY_AUDIO |
                    CONSOLE_CAPABILITY_STORAGE,
    .page = CONSOLE_PAGE_EXTERNAL,
    .enabled = false,
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
    {
        .id = CONSOLE_APP_SYSTEM,
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
        .id = CONSOLE_APP_AUDIO,
        .title = "AUDIO",
        .subtitle = "MASTER VOLUME",
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
        .accent_rgb565 = UINT16_C(0xFFE0),
        .capabilities = CONSOLE_CAPABILITY_DISPLAY,
        .page = CONSOLE_PAGE_ACHIEVEMENTS,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_LIBRARY,
        .title = "GAME MANAGER",
        .subtitle = "BUILTINS + CARTS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x07FF),
        .capabilities = CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_LIBRARY,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_MULTIPLAYER,
        .title = "MULTIPLAYER",
        .subtitle = "LOCAL LOBBY",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xFFE0),
        .capabilities = 0U,
        .page = CONSOLE_PAGE_MULTIPLAYER,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_FILES,
        .title = "FILE MANAGER",
        .subtitle = "LIST / SORT / COPY",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x07FF),
        .capabilities = CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_FILES,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_SAVES,
        .title = "SAVE MANAGER",
        .subtitle = "GAME SAVE SLOTS",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xFFE0),
        .capabilities = CONSOLE_CAPABILITY_STORAGE,
        .page = CONSOLE_PAGE_SAVES,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_TERMINAL,
        .title = "TERMINAL",
        .subtitle = "COMMANDS + SSH",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0x5FEA),
        .capabilities = CONSOLE_CAPABILITY_TOUCH,
        .page = CONSOLE_PAGE_TERMINAL,
        .enabled = true,
    },
};

_Static_assert(CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK == 256,
               "16 ms must produce exactly 256 16 kHz frames");
_Static_assert((int)CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK ==
                   (int)P4_GAME_MAX_AUDIO_STREAM_FRAMES,
               "one game stream block must cover one console audio tick");

_Static_assert((int)CONSOLE_SHELL_WIDTH ==
                   (int)PLATFORM_DISPLAY_GAME_WIDTH,
               "console width must match the platform game surface");
_Static_assert((int)CONSOLE_SHELL_HEIGHT ==
                   (int)PLATFORM_DISPLAY_GAME_HEIGHT,
               "console height must match the platform game surface");
_Static_assert(CONSOLE_SHELL_PHYSICAL_WIDTH == PLATFORM_TOUCH_WIDTH,
               "console touch width must match GT911 coordinates");
_Static_assert(CONSOLE_SHELL_PHYSICAL_HEIGHT == PLATFORM_TOUCH_HEIGHT,
               "console touch height must match GT911 coordinates");
_Static_assert(CONSOLE_SHELL_MAX_CONTACTS == PLATFORM_TOUCH_MAX_CONTACTS,
               "console contact bound must match the touch service");

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
        capabilities |= CONSOLE_CAPABILITY_TOUCH;
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
    if (!append_app(&s_quake_app)) {
        return false;
    }
    for (size_t i = 0U; i < p4_generated_game_count; ++i) {
        const p4_game_descriptor_t *const game = p4_generated_games[i];
        if (!p4_game_descriptor_valid(game) ||
            game->launcher_id <= CONSOLE_APP_ACHIEVEMENTS) {
            return false;
        }
        const console_app_descriptor_t launcher = {
            .id = game->launcher_id,
            .title = game->title,
            .subtitle = game->subtitle,
            .folder_path = p4_generated_game_folders[i],
            .accent_rgb565 = game->accent_rgb565,
            .capabilities = shell_capabilities(
                game->required_capabilities | game->optional_capabilities),
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

static console_shell_runtime_info_t runtime_info(void)
{
    const p4_content_transfer_info_t transfer =
        p4_content_transfer_info();
    const console_shell_runtime_info_t info = {
        .uptime_seconds = uptime_seconds(),
        .internal_free_kib = free_kib(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .psram_free_kib = free_kib(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .touch_ready = s_touch_ready,
        /* Compiled handoff only; the shell itself never starts audio. */
        .audio_handoff_ready = true,
        .storage_ready = s_storage_mounted &&
                         s_content_catalog.storage_available,
        .storage_writable = false,
        .content_scan_complete = s_content_scan_complete,
        .content_truncated = s_content_catalog.directory_truncated,
        .quake_shareware_ready = s_content_catalog.quake_shareware_ready,
        .usb_content_ready = transfer.ready,
        .usb_content_busy = transfer.busy,
        .usb_content_progress_percent = transfer.progress_percent,
        .valid_cart_count = s_content_catalog.valid_cart_count,
        .invalid_cart_count = s_content_catalog.invalid_cart_count,
        .builtin_game_count = (uint16_t)(p4_generated_game_count + 2U),
        .save_slot_count = 0U,
        .save_total_bytes = 0U,
        .save_management_ready = false,
        .usb_export_ready = false,
        .ssh_transport_ready = false,
        .physical_keyboard_ready = false,
        .multiplayer_core_ready = true,
        .multiplayer_transport_ready = false,
        .multiplayer_peer_count = (uint8_t)p4_mp_session_peer_count(
            &s_multiplayer_session),
    };
    return info;
}

static void update_desktop_catalog(console_shell_t *shell)
{
    p4_file_list_t files;
    p4_file_list_init(&files);
    if (s_content_catalog.storage_available) {
        (void)p4_file_list_add(&files, "P4/GAMES", 0U,
                               P4_FILE_KIND_FOLDER, true);
        (void)p4_file_list_add(&files, "P4/SAVES", 0U,
                               P4_FILE_KIND_FOLDER, true);
    }
    if (s_content_catalog.quake_shareware_ready) {
        (void)p4_file_list_add(
            &files, "PAK0.PAK",
            s_content_catalog.quake_shareware.size_bytes,
            P4_FILE_KIND_GAME_DATA, true);
    }
    const size_t cart_count = s_content_catalog.valid_cart_count <
            P4_CONTENT_MAX_CARTS
        ? s_content_catalog.valid_cart_count : P4_CONTENT_MAX_CARTS;
    for (size_t index = 0U; index < cart_count; ++index) {
        char name[P4_DESKTOP_FILE_NAME_BYTES];
        (void)snprintf(name, sizeof(name), "%.*s",
                       (int)sizeof(name) - 1,
                       s_content_catalog.carts[index].name);
        (void)p4_file_list_add(
            &files, name, s_content_catalog.carts[index].size_bytes,
            P4_FILE_KIND_CARTRIDGE, true);
    }
    console_shell_set_file_list(shell, &files);

    p4_save_catalog_t saves;
    p4_save_catalog_init(&saves, false);
    console_shell_set_save_catalog(shell, &saves);
}

static void set_quake_enabled(bool enabled)
{
    for (size_t index = 0U; index < s_app_count; ++index) {
        if (s_apps[index].id == CONSOLE_APP_QUAKE) {
            s_apps[index].enabled = enabled;
            return;
        }
    }
}

static bool content_scan_running(void)
{
    portENTER_CRITICAL(&s_content_scan_lock);
    const bool running = s_content_scan_state == CONTENT_SCAN_RUNNING;
    portEXIT_CRITICAL(&s_content_scan_lock);
    return running;
}

static void content_scan_worker(void *unused)
{
    (void)unused;
    const p4_content_status_t result = p4_content_catalog_scan(
        PLATFORM_STORAGE_MOUNT_POINT, &s_content_scan_staging);
    portENTER_CRITICAL(&s_content_scan_lock);
    s_content_scan_result = result;
    s_content_scan_state = CONTENT_SCAN_DONE;
    portEXIT_CRITICAL(&s_content_scan_lock);
    vTaskSuspend(NULL);
}

static bool finish_content_catalog_scan(void)
{
    p4_content_status_t scan_result = P4_CONTENT_INVALID_ARGUMENT;
    TaskHandle_t completed_task = NULL;
    portENTER_CRITICAL(&s_content_scan_lock);
    if (s_content_scan_state != CONTENT_SCAN_DONE) {
        portEXIT_CRITICAL(&s_content_scan_lock);
        return false;
    }
    scan_result = s_content_scan_result;
    s_content_catalog = s_content_scan_staging;
    completed_task = s_content_scan_task;
    s_content_scan_task = NULL;
    s_content_scan_state = CONTENT_SCAN_IDLE;
    portEXIT_CRITICAL(&s_content_scan_lock);
    if (completed_task != NULL) {
        vTaskDeleteWithCaps(completed_task);
    }
    if (scan_result != P4_CONTENT_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS CONTENT_SCAN_DEGRADED status=%s writes=0",
                 p4_content_status_name(scan_result));
        return true;
    }
    s_content_scan_complete = true;
    set_quake_enabled(s_content_catalog.quake_shareware_ready);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS CONTENT_READY carts=%u rejected=%u "
             "candidates=%u truncated=%u quake_shareware=%u writes=0",
             (unsigned)s_content_catalog.valid_cart_count,
             (unsigned)s_content_catalog.invalid_cart_count,
             (unsigned)s_content_catalog.candidates_seen,
             s_content_catalog.directory_truncated ? 1U : 0U,
             s_content_catalog.quake_shareware_ready ? 1U : 0U);
    return true;
}

static void refresh_content_catalog(void)
{
    if (p4_content_transfer_info().busy) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS CONTENT_SCAN_SKIPPED reason=usb-copy-active");
        return;
    }
    if (content_scan_running()) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS CONTENT_SCAN_SKIPPED reason=already-running");
        return;
    }
    s_content_scan_complete = false;
    memset(&s_content_catalog, 0, sizeof(s_content_catalog));
    memset(&s_content_scan_staging, 0, sizeof(s_content_scan_staging));
    set_quake_enabled(false);
    if (!s_storage_mounted) {
        const esp_err_t mount_result = platform_storage_init();
        if (mount_result != ESP_OK) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS STORAGE_DEGRADED stage=mount error=%s "
                     "format_attempted=0",
                     esp_err_to_name(mount_result));
            return;
        }
        s_storage_mounted = true;
    }
    portENTER_CRITICAL(&s_content_scan_lock);
    s_content_scan_state = CONTENT_SCAN_RUNNING;
    portEXIT_CRITICAL(&s_content_scan_lock);
    const BaseType_t task_result = xTaskCreateWithCaps(
        content_scan_worker, "p4_content_scan",
        CONSOLE_CONTENT_SCAN_STACK_BYTES, NULL, tskIDLE_PRIORITY + 1U,
        &s_content_scan_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (task_result != pdPASS) {
        portENTER_CRITICAL(&s_content_scan_lock);
        s_content_scan_state = CONTENT_SCAN_IDLE;
        portEXIT_CRITICAL(&s_content_scan_lock);
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS CONTENT_SCAN_DEGRADED status=task-create-failed "
                 "writes=0");
        return;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS CONTENT_SCAN_BEGIN mode=background writes=0");
}

static void start_usb_content_transfer(void)
{
    if (!s_storage_mounted || content_scan_running() ||
        p4_content_transfer_info().ready) {
        return;
    }
    const esp_err_t result = p4_content_transfer_init(
        PLATFORM_STORAGE_MOUNT_POINT);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS USB_CONTENT_DEGRADED error=%s",
                 esp_err_to_name(result));
    }
}

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
             "P4_CONSOLE_OS TOUCH_READY controller=gt911 contacts_max=%u "
             "logical=%ux%u native=%ux%u rotation_cw=%u",
             (unsigned)PLATFORM_TOUCH_MAX_CONTACTS,
             (unsigned)PLATFORM_TOUCH_WIDTH,
             (unsigned)PLATFORM_TOUCH_HEIGHT,
             (unsigned)PLATFORM_TOUCH_NATIVE_WIDTH,
             (unsigned)PLATFORM_TOUCH_NATIVE_HEIGHT,
             (unsigned)PLATFORM_TOUCH_ROTATION_CW_DEGREES);
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

static console_shell_action_t poll_touch(console_shell_t *shell)
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

static void log_runtime_stats(const console_shell_t *shell)
{
    platform_display_stats_t display = {0};
    const esp_err_t result = platform_display_get_stats(&display);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "P4_CONSOLE_OS STATS_UNAVAILABLE error=%s",
                 esp_err_to_name(result));
        return;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS STATS loops=%lu page=%u renders=%lu "
             "touch_ready=%u touch_polls=%lu touch_failures=%lu "
             "display_submits=%lu display_completions=%lu "
             "display_timeouts=%lu display_failures=%lu "
             "storage_ready=%u carts=%u quake=%u multiplayer_peers=%u "
             "amp_energized=0 doom_handoffs=%lu",
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
             s_storage_mounted ? 1U : 0U,
             (unsigned)s_content_catalog.valid_cart_count,
             s_content_catalog.quake_shareware_ready ? 1U : 0U,
             (unsigned)p4_mp_session_peer_count(&s_multiplayer_session),
             (unsigned long)s_doom_handoff_count);
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
        p4_draw_fill_rect(&surface, 122 + (int)segment * 16, 176,
                          10, 2,
                          segment < animation_step ? UINT16_C(0xFD20) :
                              UINT16_C(0x39C7));
    }
    return platform_display_submit_rgb565(
        s_pixels, CONSOLE_SHELL_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
}

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

static bool native_audio_runtime_allowed(void)
{
    doom_touch_audio_runtime_gate_t gate = {0};
    doom_touch_audio_runtime_gate_read(&gate);
    return doom_touch_audio_runtime_gate_mode(&gate) ==
        DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_AND_AUDIO;
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

static void close_native_audio_or_halt(p4_game_platform_audio_t *audio);

static void play_boot_chime(void)
{
    if (!native_audio_runtime_allowed()) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS BOOT_CHIME status=silent reason=runtime-gate");
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_BOOT_CHIME_TICKS *
                                 CONSOLE_SERVICE_INTERVAL_MS));
        return;
    }
    p4_game_platform_audio_t audio;
    p4_game_platform_audio_init(&audio);
    void *const control_bus = platform_board_kind() ==
            PLATFORM_BOARD_WAVESHARE_4_3
        ? (void *)platform_i2c_shared_handle(s_shared_bus) : NULL;
    const esp_err_t open_result = p4_game_platform_audio_open(
        &audio, true, control_bus, 5U);
    if (open_result != ESP_OK) {
        if (audio.hardware_touched &&
            (!audio.safe_high_proven || audio.backend != NULL)) {
            halt_dark("boot-chime-open-safety", open_result);
        }
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS BOOT_CHIME status=silent stage=open error=%s",
                 esp_err_to_name(open_result));
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_BOOT_CHIME_TICKS *
                                 CONSOLE_SERVICE_INTERVAL_MS));
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
    for (size_t index = 0U; index < sizeof(chord) / sizeof(chord[0]); ++index) {
        (void)p4_audio_mixer_play_tone(&mixer, &chord[index]);
    }
    bool played = true;
    for (unsigned tick = 0U; tick < CONSOLE_BOOT_CHIME_TICKS; ++tick) {
        if (!pump_native_audio(&audio, &mixer)) {
            played = false;
            break;
        }
        const unsigned ticks_per_step = CONSOLE_BOOT_CHIME_TICKS /
            CONSOLE_BOOT_ANIMATION_STEPS;
        if (tick != 0U && tick % ticks_per_step == 0U) {
            const esp_err_t frame_result =
                present_boot_screen(tick / ticks_per_step);
            if (frame_result != ESP_OK) {
                halt_dark("boot-animation-frame", frame_result);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONSOLE_SERVICE_INTERVAL_MS));
    }
    close_native_audio_or_halt(&audio);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS BOOT_CHIME status=%s notes=A4-C#5-E5 "
             "source=original-startup-chord",
             played ? "played" : "degraded");
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

static void *allocate_game_state(size_t bytes)
{
    void *state = heap_caps_calloc(
        1U, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (state == NULL) {
        state = heap_caps_calloc(1U, bytes, MALLOC_CAP_8BIT);
    }
    return state;
}

static esp_err_t run_native_game(console_shell_t *shell,
                                 const p4_game_descriptor_t *game,
                                 uint8_t master_volume_step)
{
    if (shell == NULL || !p4_game_descriptor_valid(game) ||
        s_pixels == NULL || !s_display_initialized ||
        master_volume_step < CONSOLE_SHELL_MASTER_VOLUME_MIN ||
        master_volume_step > CONSOLE_SHELL_MASTER_VOLUME_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    void *const game_state = allocate_game_state(game->state_bytes);
    if (game_state == NULL) {
        return ESP_ERR_NO_MEM;
    }

    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    p4_game_platform_audio_t audio;
    p4_game_platform_audio_init(&audio);
    bool audio_running = false;
    const uint32_t requested_audio_capabilities =
        (game->required_capabilities | game->optional_capabilities) &
        (P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_AUDIO_STREAM);
    if (requested_audio_capabilities != 0U) {
        void *const control_bus =
            platform_board_kind() == PLATFORM_BOARD_WAVESHARE_4_3
                ? (void *)platform_i2c_shared_handle(s_shared_bus)
                : NULL;
        const esp_err_t audio_result = p4_game_platform_audio_open(
            &audio, native_audio_runtime_allowed(),
            control_bus,
            master_volume_step);
        if (audio_result == ESP_OK) {
            audio_running = true;
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS NATIVE_SOUND_READY app=%s "
                     "rate_hz=16000 format=pcm16-stereo volume_step=%u/10",
                     game->id, (unsigned)master_volume_step);
        } else if (audio.hardware_touched &&
                   (!audio.safe_high_proven || audio.backend != NULL)) {
            heap_caps_free(game_state);
            halt_dark("native-audio-open-safety", audio_result);
        } else {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS NATIVE_SOUND_DEGRADED app=%s "
                     "stage=open error=%s fallback=silent",
                     game->id, esp_err_to_name(audio_result));
        }
    }

    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS |
            (audio_running ? requested_audio_capabilities : 0U),
        .audio_context = &mixer,
        .game_id = game->id,
        .play_tone = audio_running &&
            (requested_audio_capabilities & P4_GAME_CAP_AUDIO_TONE) != 0U
            ? p4_audio_mixer_service_play_tone : NULL,
        .submit_pcm16_stereo = audio_running &&
            (requested_audio_capabilities & P4_GAME_CAP_AUDIO_STREAM) != 0U
            ? p4_audio_mixer_service_submit_pcm16_stereo : NULL,
        .stop_audio = audio_running
            ? p4_audio_mixer_service_stop : NULL,
        .achievement_context = &s_achievements,
        .unlock_achievement = p4_achievement_catalog_service_unlock,
    };
    p4_game_instance_t instance = {0};
    if (!p4_game_instance_start(
            &instance, game, &services, game_state, game->state_bytes)) {
        close_native_audio_or_halt(&audio);
        heap_caps_free(game_state);
        return ESP_ERR_INVALID_STATE;
    }
    p4_game_surface_t surface = {
        .pixels = s_pixels,
        .stride_pixels = P4_GAME_SURFACE_WIDTH,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    if (!p4_game_instance_render(&instance, &surface)) {
        p4_game_instance_stop(&instance);
        close_native_audio_or_halt(&audio);
        heap_caps_free(game_state);
        return ESP_ERR_INVALID_RESPONSE;
    }
    esp_err_t result = platform_display_submit_rgb565(
        s_pixels, P4_GAME_SURFACE_WIDTH, CONSOLE_SUBMIT_TIMEOUT_MS);
    if (result != ESP_OK) {
        halt_dark("native-first-frame", result);
    }
    if (audio_running) {
        audio_running = pump_native_audio(&audio, &mixer);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS NATIVE_GAME_START app=%s api=%lu "
             "display_owner=console touch_owner=console audio=%s",
             game->id, (unsigned long)game->api_version,
             audio_running ? "ready" : "silent");

    p4_game_input_mapper_t input_mapper;
    p4_game_input_mapper_init(&input_mapper);
    p4_game_result_t game_result = P4_GAME_CONTINUE;
    uint32_t updates = 0U;
    uint32_t renders = 1U;
    unsigned render_phase = 0U;
    TickType_t last_wake = xTaskGetTickCount();
    while (game_result == P4_GAME_CONTINUE) {
        platform_touch_frame_t frame;
        const bool valid = read_touch_frame(&frame);
        p4_physical_touch_t touches[P4_INPUT_MAX_TOUCHES];
        const size_t touch_count = valid ? frame.contact_count : 0U;
        for (size_t i = 0U; i < touch_count; ++i) {
            touches[i].x = frame.contacts[i].x;
            touches[i].y = frame.contacts[i].y;
        }
        p4_game_input_t input;
        p4_game_input_mapper_update(
            &input_mapper, valid,
            touch_count == 0U ? NULL : touches,
            touch_count, 0U, &input);
        game_result = p4_game_instance_update(
            &instance, &input, CONSOLE_SERVICE_INTERVAL_MS);
        if (game_result == P4_GAME_ERROR) {
            result = ESP_FAIL;
            break;
        }
        if (game_result == P4_GAME_CONTINUE) {
            if (audio_running) {
                audio_running = pump_native_audio(&audio, &mixer);
            }
            if (updates != UINT32_MAX) {
                ++updates;
            }
            ++render_phase;
            if (render_phase >= CONSOLE_RENDER_DIVISOR) {
                render_phase = 0U;
                if (!p4_game_instance_render(&instance, &surface)) {
                    result = ESP_ERR_INVALID_RESPONSE;
                    break;
                }
                result = platform_display_submit_rgb565(
                    s_pixels, P4_GAME_SURFACE_WIDTH,
                    CONSOLE_SUBMIT_TIMEOUT_MS);
                if (result != ESP_OK) {
                    halt_dark("native-frame-submit", result);
                }
                if (renders != UINT32_MAX) {
                    ++renders;
                }
            }
            if (updates != 0U && updates % 300U == 0U) {
                p4_audio_mixer_stats_t stats = {0};
                p4_audio_mixer_get_stats(&mixer, &stats);
                ESP_LOGI(TAG,
                         "P4_CONSOLE_OS NATIVE_GAME_STATS app=%s "
                         "updates=%lu renders=%lu target_fps=%u "
                         "touch_polls=%lu touch_failures=%lu "
                         "audio_running=%u tones=%lu pcm_blocks=%lu "
                         "pcm_frames=%lu pcm_rejected=%lu underrun=%lu "
                         "audio_frames=%lu",
                         game->id, (unsigned long)updates,
                         (unsigned long)renders,
                         (unsigned)CONSOLE_RENDER_TARGET_FPS,
                         (unsigned long)s_touch_polls,
                         (unsigned long)s_touch_poll_failures,
                         audio_running ? 1U : 0U,
                         (unsigned long)stats.tones_started,
                         (unsigned long)stats.stream_blocks_submitted,
                         (unsigned long)stats.stream_frames_submitted,
                         (unsigned long)stats.stream_blocks_rejected,
                         (unsigned long)stats.stream_underrun_frames,
                         (unsigned long)audio.frames_written);
            }
        }
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(CONSOLE_SERVICE_INTERVAL_MS));
    }

    p4_game_instance_stop(&instance);
    close_native_audio_or_halt(&audio);
    heap_caps_free(game_state);
    console_shell_set_achievement_catalog(shell, &s_achievements);
    console_shell_show_home(shell);
    const esp_err_t home_result = present(shell);
    if (home_result != ESP_OK) {
        halt_dark("native-return-home", home_result);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS NATIVE_GAME_STOP app=%s result=%s "
             "return=launcher amp_safe=%u",
             game->id,
             game_result == P4_GAME_EXIT_TO_LAUNCHER ? "user-exit" : "error",
             audio.hardware_touched ? (audio.safe_high_proven ? 1U : 0U) : 1U);
    return game_result == P4_GAME_EXIT_TO_LAUNCHER ? ESP_OK : result;
}

static void launch_doom_exclusive(uint8_t master_volume_step)
{
    ++s_doom_handoff_count;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS HANDOFF_BEGIN app=doom mode=exclusive-one-way "
             "audio_owner=doom volume_step=%u/10",
             (unsigned)master_volume_step);
    esp_err_t result = platform_display_set_brightness(0U);
    if (result != ESP_OK) {
        halt_dark("handoff-backlight", result);
    }
    if (s_storage_mounted) {
        result = platform_storage_deinit();
        if (result != ESP_OK) {
            halt_dark("handoff-storage-deinit", result);
        }
        s_storage_mounted = false;
    }
    result = destroy_touch_for_handoff();
    if (result != ESP_OK) {
        halt_dark("handoff-touch-destroy", result);
    }
    result = destroy_bus_for_handoff();
    if (result != ESP_OK) {
        halt_dark("handoff-bus-destroy", result);
    }
    result = platform_display_deinit();
    if (result != ESP_OK) {
        halt_dark("handoff-display-deinit", result);
    }
    s_display_initialized = false;
    heap_caps_free(s_pixels);
    s_pixels = NULL;

    ESP_LOGI(TAG,
             "P4_CONSOLE_OS HANDOFF_COMPLETE app=doom shell_services=released");
    console_os_launch_doom(master_volume_step);
    halt_dark("doom-returned-without-reentrant-teardown",
              ESP_ERR_INVALID_STATE);
}

static void launch_quake_exclusive(uint8_t master_volume_step)
{
    if (!s_storage_mounted || !s_content_catalog.quake_shareware_ready) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS QUAKE_REJECTED reason=shareware-not-validated");
        return;
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS HANDOFF_BEGIN app=quake mode=exclusive-restart "
             "canvas=768x480 storage=read-only volume_step=%u/10",
             (unsigned)master_volume_step);
    const p4_quake_config_t config = {
        .basedir = PLATFORM_STORAGE_MOUNT_POINT "/GAMES/QUAKE",
        .touch = s_touch,
        .audio_control_bus = platform_i2c_shared_handle(s_shared_bus),
        .master_volume_step = master_volume_step,
        .audio_runtime_authorized = native_audio_runtime_allowed(),
    };
    const esp_err_t result = p4_quake_run(&config);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS HANDOFF_RETURN app=quake result=%s action=restart",
             esp_err_to_name(result));
    if (s_storage_mounted) {
        const esp_err_t storage_result = platform_storage_deinit();
        if (storage_result != ESP_OK) {
            halt_dark("quake-storage-deinit", storage_result);
        }
        s_storage_mounted = false;
    }
    esp_restart();
    halt_dark("quake-restart-returned", ESP_ERR_INVALID_STATE);
}

void app_main(void)
{
    p4_mp_session_init(&s_multiplayer_session);
    p4_achievement_catalog_init(&s_achievements);
    if (!build_app_registry()) {
        halt_dark("app-registry", ESP_ERR_INVALID_ARG);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS START shell=freertos-native apps=%u "
             "surface=rgb565-768x480 native_compat=320x200 touch=gt911 "
             "native_game_api=1 native_format=p4-native-static-v1 "
             "execution=build-candidate",
             (unsigned)s_app_count);

    console_shell_t shell;
    if (!console_shell_init(
            &shell, s_apps, s_app_count)) {
        halt_dark("shell-init", ESP_ERR_INVALID_ARG);
    }
    console_shell_set_achievement_catalog(&shell, &s_achievements);
    update_desktop_catalog(&shell);

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

    create_touch_or_continue();
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
    console_shell_set_runtime_info(&shell, &initial_runtime);
    result = present(&shell);
    if (result != ESP_OK) {
        halt_dark("first-frame", result);
    }
    refresh_content_catalog();
    update_desktop_catalog(&shell);
    start_usb_content_transfer();
    const console_shell_runtime_info_t content_runtime = runtime_info();
    console_shell_set_runtime_info(&shell, &content_runtime);
    if (console_shell_is_dirty(&shell)) {
        result = present(&shell);
        if (result != ESP_OK) {
            halt_dark("content-ready-frame", result);
        }
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS VIEWPORT_READY source=%ux%u viewport=%ux%u "
             "margins=%u/%u/%u/%u fit=aspect-preserving",
             (unsigned)PLATFORM_DISPLAY_GAME_WIDTH,
             (unsigned)PLATFORM_DISPLAY_GAME_HEIGHT,
             (unsigned)PLATFORM_DISPLAY_GAME_VIEWPORT_WIDTH,
             (unsigned)PLATFORM_DISPLAY_GAME_VIEWPORT_HEIGHT,
             (unsigned)PLATFORM_DISPLAY_GAME_MARGIN_LEFT,
             (unsigned)PLATFORM_DISPLAY_GAME_MARGIN_RIGHT,
             (unsigned)PLATFORM_DISPLAY_GAME_MARGIN_TOP,
             (unsigned)PLATFORM_DISPLAY_GAME_MARGIN_BOTTOM);
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS READY page=home amp_energized=0 "
             "master_volume=%u/10 native_render_target_fps=%u "
             "native_service_interval_ms=%u "
             "doom_audio=deferred-until-exclusive-handoff",
             (unsigned)console_shell_master_volume_step(&shell),
             (unsigned)CONSOLE_RENDER_TARGET_FPS,
             (unsigned)CONSOLE_SERVICE_INTERVAL_MS);

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        ++s_loop_count;
        if (finish_content_catalog_scan()) {
            update_desktop_catalog(&shell);
            start_usb_content_transfer();
        }
        const bool scan_running = content_scan_running();
        if (!scan_running) {
            p4_content_transfer_poll();
        }
        const p4_content_transfer_info_t transfer =
            p4_content_transfer_info();
        console_shell_action_t action = transfer.busy ?
            (console_shell_action_t){.type = CONSOLE_ACTION_NONE} :
            poll_touch(&shell);
        if (scan_running &&
            (action.type == CONSOLE_ACTION_LAUNCH ||
             action.type == CONSOLE_ACTION_LIBRARY_REFRESH)) {
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS ACTION_DEFERRED reason=content-scan "
                     "action=%u",
                     (unsigned)action.type);
            action = (console_shell_action_t){.type = CONSOLE_ACTION_NONE};
        }
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(&shell, &current_runtime);
        if (action.type == CONSOLE_ACTION_VOLUME_CHANGED) {
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS MASTER_VOLUME step=%u/10 "
                     "applies=next-game amp_energized=0",
                     (unsigned)action.volume_step);
        } else if (action.type == CONSOLE_ACTION_COLOR_MODE_CHANGED) {
            ESP_LOGI(TAG,
                     "P4_CONSOLE_OS COLOR_MODE mode=%u persistence=session-only",
                     (unsigned)action.color_mode);
        } else if (action.type == CONSOLE_ACTION_LIBRARY_REFRESH) {
            refresh_content_catalog();
            update_desktop_catalog(&shell);
            start_usb_content_transfer();
            const console_shell_runtime_info_t refreshed = runtime_info();
            console_shell_set_runtime_info(&shell, &refreshed);
        } else if (action.type == CONSOLE_ACTION_USB_EXPORT) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS USB_EXPORT_REJECTED reason=transport-not-authorized item=%u",
                     (unsigned)action.item_index);
        } else if (action.type == CONSOLE_ACTION_SSH_CONNECT) {
            ESP_LOGW(TAG,
                     "P4_CONSOLE_OS SSH_REJECTED reason=wifi-transport-not-qualified");
        } else if (action.type == CONSOLE_ACTION_LAUNCH &&
            action.app_id == CONSOLE_APP_DOOM) {
            launch_doom_exclusive(
                console_shell_master_volume_step(&shell));
        } else if (action.type == CONSOLE_ACTION_LAUNCH &&
                   action.app_id == CONSOLE_APP_QUAKE) {
            launch_quake_exclusive(
                console_shell_master_volume_step(&shell));
        } else if (action.type == CONSOLE_ACTION_LAUNCH) {
            const p4_game_descriptor_t *const game =
                p4_generated_game_by_launcher_id(action.app_id);
            if (game == NULL) {
                ESP_LOGE(TAG,
                         "P4_CONSOLE_OS NATIVE_GAME_REJECTED id=%lu "
                         "reason=not-registered",
                         (unsigned long)action.app_id);
            } else {
                const esp_err_t game_result = run_native_game(
                    &shell, game,
                    console_shell_master_volume_step(&shell));
                if (game_result != ESP_OK) {
                    ESP_LOGW(TAG,
                             "P4_CONSOLE_OS NATIVE_GAME_DEGRADED app=%s "
                             "error=%s return=launcher",
                             game->id, esp_err_to_name(game_result));
                }
                /* Do not make the shell catch up every tick spent in-game. */
                last_wake = xTaskGetTickCount();
            }
        }
        if (console_shell_is_dirty(&shell)) {
            result = present(&shell);
            if (result != ESP_OK) {
                halt_dark("frame-submit", result);
            }
        }
        if (!transfer.busy && s_loop_count % 300U == 0U) {
            log_runtime_stats(&shell);
        }
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(CONSOLE_SERVICE_INTERVAL_MS));
    }
}
