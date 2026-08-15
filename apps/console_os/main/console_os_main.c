// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * P4 Console OS: a small FreeRTOS-native foreground shell. Built-ins consume
 * platform services directly; validated P4G cartridges run through a bounded
 * host table and the pinned ELF loader. Doom remains an exclusive one-way
 * handoff because the imported engine has no reviewed reentrant teardown.
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
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "p4/audio.h"
#include "p4/cartridge.h"
#include "p4/game.h"
#include "p4/input.h"
#include "p4/platform.h"
#include "platform/display.h"
#include "platform/game_catalog.h"
#include "platform/game_loader.h"
#include "platform/game_storage.h"
#include "platform/os_update.h"
#include "platform/touch.h"
#include "platform_i2c_shared/bus.h"
#include "runtime_gate.h"

enum {
    CONSOLE_APP_DOOM = 1,
    CONSOLE_APP_COLORS = 2,
    CONSOLE_APP_TOUCH = 3,
    CONSOLE_APP_SYSTEM = 4,
    CONSOLE_APP_AUDIO = 5,
    CONSOLE_APP_FILES = 6,
    CONSOLE_APP_GAMES = 7,
    CONSOLE_FRAME_INTERVAL_MS = 16,
    CONSOLE_SUBMIT_TIMEOUT_MS = 250,
    CONSOLE_BACKLIGHT_PERCENT = 25,
    CONSOLE_CLEANUP_ATTEMPTS = 3,
    CONSOLE_NATIVE_AUDIO_VOLUME_STEP = 6,
    CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK =
        P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ *
        CONSOLE_FRAME_INTERVAL_MS / 1000,
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
static bool s_game_storage_initialized;
static bool s_game_storage_status_seen;
static platform_game_storage_status_t s_game_storage_status;
static platform_game_storage_file_listing_t s_platform_file_listing;
static console_shell_file_listing_t s_shell_file_listing;
static console_shell_file_listing_t s_manager_listing;
static platform_game_catalog_t s_game_catalog;
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
static int16_t s_native_audio_pcm[
    CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK *
    P4_GAME_PLATFORM_AUDIO_CHANNEL_COUNT];

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
                    CONSOLE_CAPABILITY_TOUCH |
                    CONSOLE_CAPABILITY_AUDIO |
                    CONSOLE_CAPABILITY_STORAGE,
    .page = CONSOLE_PAGE_EXTERNAL,
    .enabled = true,
};

static const console_app_descriptor_t s_builtin_apps[] = {
    {
        .id = CONSOLE_APP_COLORS,
        .title = "COLORS",
        .subtitle = "DISPLAY TEST",
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
        .subtitle = "DOOM SOUND PATH",
        .folder_path = "SYSTEM",
        .accent_rgb565 = UINT16_C(0xF81F),
        .capabilities = CONSOLE_CAPABILITY_AUDIO,
        .page = CONSOLE_PAGE_AUDIO,
        .enabled = true,
    },
    {
        .id = CONSOLE_APP_FILES,
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
        .id = CONSOLE_APP_GAMES,
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

_Static_assert(CONSOLE_NATIVE_AUDIO_FRAMES_PER_TICK == 256,
               "16 ms must produce exactly 256 16 kHz frames");

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
    for (size_t i = 0U; i < s_game_catalog.entry_count; ++i) {
        const platform_game_catalog_entry_t *const game =
            &s_game_catalog.entries[i];
        if (!game->valid || game->package.launcher_id <= CONSOLE_APP_GAMES) {
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
        subtitle = "COPY DOOM1.WAD OVER USB";
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
    for (size_t index = 0U; index < s_app_count; ++index) {
        if (s_apps[index].id >= 100U) {
            s_apps[index].enabled = stored_games_ready;
        }
    }
    if (changed) {
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS GAME_STORAGE state=%s usb_attached=%u "
                 "generation=%lu capacity=%llu last_error=%s",
                 platform_game_storage_state_name(status.state),
                 status.usb_attached ? 1U : 0U,
                 (unsigned long)status.generation,
                 (unsigned long long)status.capacity_bytes,
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

static esp_err_t reload_game_catalog(void)
{
    platform_game_catalog_t catalog;
    const esp_err_t catalog_result = platform_game_catalog_scan(&catalog);
    platform_os_update_info_t update;
    const esp_err_t update_result = platform_os_update_inspect(&update);
    if (catalog_result == ESP_OK) {
        s_game_catalog = catalog;
    } else {
        memset(&s_game_catalog, 0, sizeof(s_game_catalog));
    }
    s_os_update_info = update;
    s_catalog_seen = catalog_result == ESP_OK;
    s_catalog_storage_generation = s_game_storage_status.generation;
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS GAME_CATALOG available=%u packages=%u "
             "valid=%u omitted=%lu generation=%lu update=%u result=%s",
             s_game_catalog.available ? 1U : 0U,
             (unsigned)s_game_catalog.entry_count,
             (unsigned)s_game_catalog.valid_count,
             (unsigned long)s_game_catalog.omitted_packages,
             (unsigned long)s_catalog_storage_generation,
             (unsigned)s_os_update_info.state,
             esp_err_to_name(catalog_result != ESP_OK
                ? catalog_result : update_result));
    return catalog_result;
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
    const bool previous_all_programs = shell->home_all_programs;
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
        const esp_err_t shown = present(shell);
        if (shown != ESP_OK) {
            halt_dark("update-status-frame", shown);
        }
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS UPDATE_BEGIN version=%s bytes=%lu "
                 "source=usb-game-storage target=inactive-ota",
                 s_os_update_info.version,
                 (unsigned long)s_os_update_info.image_bytes);
        const esp_err_t result =
            platform_os_update_install(&s_os_update_info);
        ESP_LOGI(TAG,
                 "P4_CONSOLE_OS UPDATE_END version=%s result=%s "
                 "reboot=%u",
                 s_os_update_info.version, esp_err_to_name(result),
                 result == ESP_OK ? 1U : 0U);
        if (result == ESP_OK) {
            const esp_err_t cleanup =
                platform_game_storage_remove_root_file(
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
        result = platform_game_storage_remove_root_file(
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
        .touch_ready = s_touch_ready,
        /* Compiled handoff only; the shell itself never starts audio. */
        .audio_handoff_ready = true,
        .game_storage_usb_attached = s_game_storage_status.usb_attached,
        .doom_wad_ready =
            s_game_storage_status.state == PLATFORM_GAME_STORAGE_APP_READY,
    };
    return info;
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
             "amp_energized=0 doom_handoffs=%lu storage=%s "
             "storage_generation=%lu usb_attached=%u",
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
             s_game_storage_status.usb_attached ? 1U : 0U);
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

typedef struct {
    const char *game_id;
    p4_audio_mixer_t mixer;
    p4_game_platform_audio_t audio;
    p4_game_input_mapper_t input_mapper;
    TickType_t last_wake;
    uint32_t frames;
    p4_game_result_t game_result;
    bool audio_running;
    bool finished;
} cartridge_run_context_t;

static bool cartridge_play_tone(void *opaque, const p4_tone_t *tone)
{
    cartridge_run_context_t *const context = opaque;
    return context != NULL && context->audio_running &&
        p4_audio_mixer_service_play_tone(&context->mixer, tone);
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
    if (context->audio_running) {
        context->audio_running = pump_native_audio(
            &context->audio, &context->mixer);
    }
    if (context->frames != UINT32_MAX) {
        ++context->frames;
    }
    if (context->frames != 0U && context->frames % 300U == 0U) {
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
        .game_result = P4_GAME_ERROR,
        .last_wake = xTaskGetTickCount(),
    };
    p4_audio_mixer_init(&context.mixer);
    p4_game_platform_audio_init(&context.audio);
    const uint32_t capabilities = game->package.required_capabilities |
        game->package.optional_capabilities;
    if ((capabilities & P4_GAME_CAP_AUDIO_TONE) != 0U) {
        const esp_err_t audio_result = p4_game_platform_audio_open(
            &context.audio, native_audio_runtime_allowed(),
            CONSOLE_NATIVE_AUDIO_VOLUME_STEP);
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
            (context.audio_running ? P4_GAME_CAP_AUDIO_TONE : 0U),
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
        .stop_audio = context.audio_running ? cartridge_stop_audio : NULL,
        .finished = cartridge_finished,
    };
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS CARTRIDGE_START app=%s file=%s api=1 "
             "storage=psram-elf audio=%s",
             game->package.id, game->file_name,
             context.audio_running ? "ready" : "silent");
    esp_err_t result = platform_game_loader_run(game, &host);
    close_native_audio_or_halt(&context.audio);
    if (result == ESP_OK &&
        (!context.finished ||
         context.game_result != P4_GAME_EXIT_TO_LAUNCHER)) {
        result = ESP_FAIL;
    }
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
             "audio_owner=doom volume_step=6/10 storage=game-locked "
             "usb_device=stopped");
    result = platform_display_set_brightness(0U);
    if (result != ESP_OK) {
        halt_dark("handoff-backlight", result);
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
    console_os_launch_doom();
    halt_dark("doom-returned-without-reentrant-teardown",
              ESP_ERR_INVALID_STATE);
}

void app_main(void)
{
    const esp_err_t storage_result = platform_game_storage_init();
    s_game_storage_initialized = storage_result == ESP_OK;
    if (!s_game_storage_initialized) {
        ESP_LOGW(TAG,
                 "P4_CONSOLE_OS GAME_STORAGE_DEGRADED error=%s "
                 "native_apps=available doom=disabled",
                 esp_err_to_name(storage_result));
    }
    sync_game_storage();
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
             "surface=rgb565-320x200 touch=gt911 "
             "native_game_api=1 native_format=p4-native-elf-v1 "
             "game_storage=%s execution=build-candidate",
             (unsigned)s_app_count,
             platform_game_storage_state_name(s_game_storage_status.state));

    console_shell_t shell;
    if (!console_shell_init(
            &shell, s_apps, s_app_count)) {
        halt_dark("shell-init", ESP_ERR_INVALID_ARG);
    }

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
    const console_shell_runtime_info_t initial_runtime = runtime_info();
    console_shell_set_runtime_info(&shell, &initial_runtime);
    result = present(&shell);
    if (result != ESP_OK) {
        halt_dark("first-frame", result);
    }
    result = platform_display_set_brightness(CONSOLE_BACKLIGHT_PERCENT);
    if (result != ESP_OK) {
        halt_dark("backlight", result);
    }
    ESP_LOGI(TAG,
             "P4_CONSOLE_OS READY page=home amp_energized=0 "
             "doom_audio=deferred-until-exclusive-handoff");
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
        if (catalog_needs_reload()) {
            (void)reload_game_catalog();
            rebuild_shell_registry(&shell);
            if (shell.page == CONSOLE_PAGE_GAMES) {
                (void)reload_manager_listing(
                    &shell, CONSOLE_FILE_NOTICE_NONE);
            }
        }
        if (shell.page == CONSOLE_PAGE_FILES &&
            file_listing_needs_reload()) {
            (void)reload_file_listing(
                &shell, CONSOLE_FILE_NOTICE_NONE);
        }
        const console_shell_action_t action = poll_touch(&shell);
        const console_shell_runtime_info_t current_runtime = runtime_info();
        console_shell_set_runtime_info(&shell, &current_runtime);
        if (action.type == CONSOLE_ACTION_PAGE_CHANGED &&
            action.app_id == CONSOLE_APP_FILES) {
            (void)reload_file_listing(
                &shell, CONSOLE_FILE_NOTICE_NONE);
        } else if (action.type == CONSOLE_ACTION_PAGE_CHANGED &&
                   action.app_id == CONSOLE_APP_GAMES) {
            (void)reload_manager_listing(
                &shell, CONSOLE_FILE_NOTICE_NONE);
        } else if (action.type == CONSOLE_ACTION_FILE_REFRESH ||
                   action.type == CONSOLE_ACTION_FILE_DELETE) {
            handle_file_action(&shell, &action);
        } else if (action.type == CONSOLE_ACTION_GAME_REFRESH ||
                   action.type == CONSOLE_ACTION_GAME_REMOVE ||
                   action.type == CONSOLE_ACTION_OS_UPDATE_INSTALL) {
            handle_manager_action(&shell, &action);
        } else if (action.type == CONSOLE_ACTION_LAUNCH &&
            action.app_id == CONSOLE_APP_DOOM) {
            launch_doom_exclusive(&shell);
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
                const esp_err_t game_result = run_stored_game(&shell, game);
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
        if (console_shell_is_dirty(&shell)) {
            result = present(&shell);
            if (result != ESP_OK) {
                halt_dark("frame-submit", result);
            }
        }
        if (s_loop_count % 300U == 0U) {
            log_runtime_stats(&shell);
        }
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(CONSOLE_FRAME_INTERVAL_MS));
    }
}
