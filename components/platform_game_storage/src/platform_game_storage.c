// SPDX-License-Identifier: MIT

#include "platform/game_storage.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#ifndef CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
#define CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B 0
#endif
#ifndef CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
#define CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 0
#endif
#ifndef CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE
#define CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE 0
#endif
#ifndef CONFIG_P4_WAVESHARE_H2_RUNTIME_ROLE_SWITCH
#define CONFIG_P4_WAVESHARE_H2_RUNTIME_ROLE_SWITCH 0
#endif
#ifndef CONFIG_P4_BOARD_M5STACK_TAB5
#define CONFIG_P4_BOARD_M5STACK_TAB5 0
#endif
#define P4_GAME_STORAGE_SD_BACKEND \
    (CONFIG_P4_BOARD_M5STACK_TAB5 || CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
     CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3)
#define P4_GAME_STORAGE_RUNTIME_H2_SWITCH \
    (CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
     CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE && \
     CONFIG_P4_WAVESHARE_H2_RUNTIME_ROLE_SWITCH)
#define P4_GAME_STORAGE_USB_EXPORT \
    (!P4_GAME_STORAGE_SD_BACKEND || \
     (CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
      (!CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE || \
       CONFIG_P4_WAVESHARE_H2_RUNTIME_ROLE_SWITCH)))
#define P4_GAME_STORAGE_EAGER_USB_STORAGE \
    (P4_GAME_STORAGE_USB_EXPORT && !P4_GAME_STORAGE_RUNTIME_H2_SWITCH)
#define P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN \
    (CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || \
     CONFIG_P4_BOARD_M5STACK_TAB5)

#if P4_GAME_STORAGE_SD_BACKEND
#include "ff.h"
#include "diskio_sdmmc.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "freertos/task.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "fat_repair.h"
#endif
#if !P4_GAME_STORAGE_SD_BACKEND
#include "esp_partition.h"
#include "wear_levelling.h"
#endif
#if P4_GAME_STORAGE_USB_EXPORT
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
#include "esp_mac.h"
#endif
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "game_storage_files.h"
#include "game_storage_model.h"
#include "msc_write_policy.h"
#include "platform_game_storage_internal.h"
#include "mbedtls/sha256.h"

enum {
    GAME_STORAGE_MAX_FILES = 8,
    GAME_STORAGE_HASH_BUFFER_BYTES = 8192,
    GAME_STORAGE_SD_TRANSFER_BYTES = 4096,
    GAME_STORAGE_STREAM_BUFFER_BYTES = 16384,
    GAME_STORAGE_CONTENT_SCAN_STACK_BYTES = 6144,
    GAME_STORAGE_PATH_BYTES =
        sizeof(PLATFORM_GAME_STORAGE_UPDATE_MOUNT_POINT) +
        PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES + 1,
#if P4_GAME_STORAGE_SD_BACKEND
    GAME_STORAGE_SD_SLOT = SDMMC_HOST_SLOT_0,
    GAME_STORAGE_SD_LDO_CHANNEL = 4,
    GAME_STORAGE_SD_CLK_GPIO = 43,
    GAME_STORAGE_SD_CMD_GPIO = 44,
    GAME_STORAGE_SD_D0_GPIO = 39,
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    GAME_STORAGE_SD_BUS_WIDTH = 1,
    GAME_STORAGE_SD_POWER_GPIO = -1,
    GAME_STORAGE_SD_D1_GPIO = GPIO_NUM_NC,
    GAME_STORAGE_SD_D2_GPIO = GPIO_NUM_NC,
    GAME_STORAGE_SD_D3_GPIO = GPIO_NUM_NC,
#else
    GAME_STORAGE_SD_BUS_WIDTH = 4,
#if CONFIG_P4_BOARD_M5STACK_TAB5
    GAME_STORAGE_SD_POWER_GPIO = -1,
#else
    GAME_STORAGE_SD_POWER_GPIO = 45,
#endif
    GAME_STORAGE_SD_D1_GPIO = 40,
    GAME_STORAGE_SD_D2_GPIO = 41,
    GAME_STORAGE_SD_D3_GPIO = 42,
#endif
#endif
};

static const char *TAG = "game_storage";

static const uint8_t s_expected_doom_sha256[32] = {
    0x1d, 0x7d, 0x43, 0xbe, 0x50, 0x1e, 0x67, 0xd9,
    0x27, 0xe4, 0x15, 0xe0, 0xb8, 0xf3, 0xe2, 0x9c,
    0x3b, 0xf3, 0x30, 0x75, 0xe8, 0x59, 0x72, 0x18,
    0x16, 0xf6, 0x52, 0xa5, 0x26, 0xca, 0xc7, 0x71,
};

static const uint8_t s_expected_chex_sha256[32] = {
    0xd8, 0xeb, 0x52, 0x77, 0x91, 0x88, 0x83, 0xf4,
    0x90, 0xfb, 0x1a, 0x4b, 0xe3, 0xc9, 0xa8, 0x58,
    0x8d, 0xf2, 0xdb, 0xae, 0xe6, 0xdc, 0x4b, 0xeb,
    0x8d, 0xf4, 0x92, 0x91, 0x48, 0xbb, 0xff, 0xb1,
};

static const uint8_t s_expected_chex_deh_sha256[32] = {
    0x8c, 0x03, 0x45, 0x08, 0x9f, 0xb2, 0x27, 0xfa,
    0x7f, 0x71, 0xc2, 0x5a, 0x6c, 0x6e, 0x31, 0xff,
    0x5b, 0xd4, 0xbe, 0xa0, 0x58, 0x0f, 0x28, 0x6c,
    0xd7, 0x4e, 0x05, 0x91, 0x8d, 0x72, 0xdd, 0x40,
};

static game_storage_model_t s_model;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
#if P4_GAME_STORAGE_SD_BACKEND
static sdmmc_card_t *s_card;
static sd_pwr_ctrl_handle_t s_sd_power;
static bool s_sd_vfs_mounted;
#endif
#if !P4_GAME_STORAGE_SD_BACKEND
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
#endif
#if P4_GAME_STORAGE_USB_EXPORT
static tinyusb_msc_storage_handle_t s_storage;
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
static const char s_usb_language[] = {0x09, 0x04};
static char s_usb_serial[20];
static const char *s_usb_strings[] = {
    s_usb_language,
    "Game Changers AI",
    "P4 Game Storage",
    s_usb_serial,
    "P4 Game Storage",
};
#endif
#endif
static bool s_initialized;
static bool s_usb_attached;
static bool s_usb_driver_running;
static bool s_usb_host_ejected;
static uint64_t s_capacity_bytes;
static uint32_t s_sector_size_bytes;
static uint32_t s_scans;
static uint32_t s_file_mutations;
static uint32_t s_usb_verified_writes;
static uint32_t s_usb_write_failures;
static uint64_t s_free_bytes;
static uint32_t s_root_entries;
static uint32_t s_checks;
static uint32_t s_recovery_attempts;
static uint32_t s_repair_attempts;
static uint32_t s_repair_sectors_rewritten;
static platform_game_storage_repair_outcome_t s_last_repair_outcome =
    PLATFORM_GAME_STORAGE_REPAIR_NOT_RUN;
static esp_err_t s_last_check_error = ESP_ERR_INVALID_STATE;
static esp_err_t s_last_recovery_error = ESP_ERR_INVALID_STATE;
static esp_err_t s_last_repair_error = ESP_ERR_INVALID_STATE;
static esp_err_t s_last_error = ESP_ERR_INVALID_STATE;
static DRAM_ATTR uint8_t s_hash_buffer[GAME_STORAGE_HASH_BUFFER_BYTES]
    __attribute__((aligned(64)));
static bool s_maintenance;
static game_storage_content_t s_doom_content = GAME_STORAGE_CONTENT_UNKNOWN;
static game_storage_content_t s_chex_content = GAME_STORAGE_CONTENT_UNKNOWN;
static uint8_t *s_locked_wad_data;
static size_t s_locked_wad_size_bytes;
static uint8_t *s_locked_deh_data;
static size_t s_locked_deh_size_bytes;
static platform_game_storage_doom_title_t s_locked_snapshot_title =
    PLATFORM_GAME_STORAGE_DOOM_TITLE_COUNT;
#if P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
static TaskHandle_t s_content_validation_task;
static bool s_content_validation_running;
static bool s_content_validation_complete;
static uint8_t s_content_validation_progress_percent;
static uint64_t s_content_validation_progress_bytes;
static uint32_t s_content_validation_generation;
#endif

#if P4_GAME_STORAGE_USB_EXPORT
_Static_assert(GAME_STORAGE_HASH_BUFFER_BYTES >= CONFIG_TINYUSB_MSC_BUFSIZE,
               "USB write verification buffer must hold one MSC transfer");
#if P4_GAME_STORAGE_SD_BACKEND
_Static_assert(GAME_STORAGE_HASH_BUFFER_BYTES >=
                   2U * GAME_STORAGE_SD_TRANSFER_BYTES,
               "SD write and readback buffers must not overlap");
#endif
#endif

static bool lock_storage(void)
{
    return s_lock != NULL &&
        xSemaphoreTakeRecursive(s_lock, portMAX_DELAY) == pdTRUE;
}

static void unlock_storage(void)
{
    (void)xSemaphoreGiveRecursive(s_lock);
}

static void release_locked_doom_snapshot(void)
{
    heap_caps_free(s_locked_deh_data);
    heap_caps_free(s_locked_wad_data);
    s_locked_deh_data = NULL;
    s_locked_deh_size_bytes = 0U;
    s_locked_wad_data = NULL;
    s_locked_wad_size_bytes = 0U;
    s_locked_snapshot_title = PLATFORM_GAME_STORAGE_DOOM_TITLE_COUNT;
}

#if P4_GAME_STORAGE_USB_EXPORT
static game_storage_owner_t owner_from_mount(
    tinyusb_msc_mount_point_t mount_point)
{
    return mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP
        ? GAME_STORAGE_OWNER_APP : GAME_STORAGE_OWNER_USB;
}

static void storage_event_callback(tinyusb_msc_storage_handle_t handle,
                                   tinyusb_msc_event_t *event,
                                   void *argument)
{
    (void)handle;
    (void)argument;
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    if (event != NULL) {
        ESP_LOGI(TAG, "P4_GAME_STORAGE MSC_EVENT id=%u mount_point=%u",
                 (unsigned)event->id, (unsigned)event->mount_point);
    }
#endif
    if (event == NULL || !lock_storage()) {
        return;
    }
    switch (event->id) {
    case TINYUSB_MSC_EVENT_MOUNT_START:
        game_storage_model_mount_start(
            &s_model, owner_from_mount(event->mount_point));
        break;
    case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
        game_storage_model_mount_complete(
            &s_model, owner_from_mount(event->mount_point));
        s_last_error = ESP_OK;
        break;
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        game_storage_model_mount_failed(&s_model, true);
        s_last_error = ESP_ERR_NOT_FOUND;
        break;
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
    case TINYUSB_MSC_EVENT_FORMAT_FAILED:
    default:
        game_storage_model_mount_failed(&s_model, false);
        s_last_error = ESP_FAIL;
        break;
    }
    unlock_storage();
}

static void usb_event_callback(tinyusb_event_t *event, void *argument)
{
    (void)argument;
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    if (event != NULL) {
        ESP_LOGI(TAG, "P4_GAME_STORAGE USB_EVENT id=%u rhport=%u",
                 (unsigned)event->id, (unsigned)event->rhport);
    }
#endif
    if (event == NULL || !lock_storage()) {
        return;
    }
    s_usb_attached = event->id == TINYUSB_EVENT_ATTACHED;
    if (s_model.owner == GAME_STORAGE_OWNER_USB) {
        /* A new attachment can claim the exported medium again. */
        s_usb_host_ejected = event->id != TINYUSB_EVENT_ATTACHED;
    }
    unlock_storage();
}

static esp_err_t install_usb_driver(void)
{
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool already_running = s_usb_driver_running;
    unlock_storage();
    if (already_running) {
        return ESP_OK;
    }

    tinyusb_config_t config =
        TINYUSB_DEFAULT_CONFIG(usb_event_callback, NULL);
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    uint8_t mac[6] = {0};
    const esp_err_t mac_result = esp_read_mac(mac, ESP_MAC_BASE);
    if (mac_result == ESP_OK) {
        (void)snprintf(s_usb_serial, sizeof(s_usb_serial),
                       "P4-%02X%02X%02X%02X%02X%02X",
                       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        (void)snprintf(s_usb_serial, sizeof(s_usb_serial), "P4-WS43");
    }
    config.descriptor.string = s_usb_strings;
    config.descriptor.string_count =
        (uint8_t)(sizeof(s_usb_strings) / sizeof(s_usb_strings[0]));
    ESP_LOGI(TAG, "P4_GAME_STORAGE USB_ID serial=%s mac_result=%s",
             s_usb_serial, esp_err_to_name(mac_result));
#endif
    const esp_err_t result = tinyusb_driver_install(&config);
    if (lock_storage()) {
        s_usb_driver_running = result == ESP_OK;
        if (result != ESP_OK) {
            s_last_error = result;
        }
        unlock_storage();
    }
    return result;
}

static esp_err_t uninstall_usb_driver_if_running(bool *out_was_running)
{
    if (out_was_running == NULL || !s_initialized || !lock_storage()) {
        return out_was_running == NULL
            ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
    }
    *out_was_running = s_usb_driver_running;
    unlock_storage();
    if (!*out_was_running) {
        return ESP_OK;
    }

    const esp_err_t result = tinyusb_driver_uninstall();
    if (lock_storage()) {
        if (result == ESP_OK) {
            s_usb_driver_running = false;
            s_usb_attached = false;
        }
        s_last_error = result;
        unlock_storage();
    }
    return result;
}
#endif

static bool doom_header_valid(const uint8_t header[12], uint64_t size_bytes,
                              bool allow_pwad)
{
    if (memcmp(header, "IWAD", 4U) != 0 &&
        (!allow_pwad || memcmp(header, "PWAD", 4U) != 0)) {
        return false;
    }
    const uint32_t lumps = (uint32_t)header[4] |
        ((uint32_t)header[5] << 8U) |
        ((uint32_t)header[6] << 16U) |
        ((uint32_t)header[7] << 24U);
    const uint32_t directory = (uint32_t)header[8] |
        ((uint32_t)header[9] << 8U) |
        ((uint32_t)header[10] << 16U) |
        ((uint32_t)header[11] << 24U);
    const uint64_t directory_bytes = (uint64_t)lumps * UINT64_C(16);
    return lumps != 0U && (uint64_t)directory <= size_bytes &&
        directory_bytes <= size_bytes - (uint64_t)directory;
}

static void content_validation_note_bytes(size_t count)
{
#if P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
    if (count == 0U) {
        return;
    }
    if (s_content_validation_running) {
        const uint64_t total = PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES +
            PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES +
            PLATFORM_GAME_STORAGE_CHEX_DEH_BYTES;
        if (s_content_validation_progress_bytes <= total - count) {
            s_content_validation_progress_bytes += count;
        } else {
            s_content_validation_progress_bytes = total;
        }
        const uint64_t percent =
            s_content_validation_progress_bytes * UINT64_C(100) / total;
        s_content_validation_progress_percent =
            percent > UINT8_MAX ? UINT8_MAX : (uint8_t)percent;
    }
    /*
     * On-demand validation runs in the launcher task. Yield even when the
     * background progress flag is false so ESP-Hosted/NimBLE can service a
     * bonded controller while a multi-megabyte WAD is hashed.
     */
    taskYIELD();
#else
    (void)count;
#endif
}

static game_storage_content_t inspect_exact_file(
    const char *path,
    uint64_t expected_bytes,
    const uint8_t expected_sha256[32],
    bool require_wad_header,
    bool allow_pwad,
    esp_err_t *out_error,
    uint8_t **out_data)
{
    if (out_data != NULL) {
        *out_data = NULL;
    }
    struct stat metadata;
    if (path == NULL || expected_sha256 == NULL || out_error == NULL) {
        if (out_error != NULL) {
            *out_error = ESP_ERR_INVALID_ARG;
        }
        return GAME_STORAGE_CONTENT_INVALID;
    }
    if (stat(path, &metadata) != 0) {
        *out_error = errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
        return errno == ENOENT
            ? GAME_STORAGE_CONTENT_MISSING : GAME_STORAGE_CONTENT_INVALID;
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_size < 0 ||
        (uint64_t)metadata.st_size != expected_bytes) {
        *out_error = ESP_ERR_INVALID_SIZE;
        return GAME_STORAGE_CONTENT_INVALID;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        *out_error = ESP_FAIL;
        return GAME_STORAGE_CONTENT_INVALID;
    }
    uint8_t *captured_data = NULL;
    if (out_data != NULL) {
        if (expected_bytes == 0U || expected_bytes > SIZE_MAX) {
            (void)fclose(file);
            *out_error = ESP_ERR_INVALID_SIZE;
            return GAME_STORAGE_CONTENT_INVALID;
        }
        captured_data = heap_caps_malloc(
            (size_t)expected_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (captured_data == NULL) {
            (void)fclose(file);
            *out_error = ESP_ERR_NO_MEM;
            ESP_LOGE(TAG,
                     "P4_GAME_STORAGE LOCKED_SNAPSHOT_ALLOC_FAILED "
                     "file=%s bytes=%llu psram_free=%u psram_largest=%u",
                     path, (unsigned long long)expected_bytes,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)heap_caps_get_largest_free_block(
                         MALLOC_CAP_SPIRAM));
            return GAME_STORAGE_CONTENT_INVALID;
        }
    }
    uint8_t header[12] = {0};
    size_t header_bytes = 0U;
    uint64_t total_bytes = 0U;
    bool capture_bounds_valid = true;
    uint8_t digest[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    int crypto = mbedtls_sha256_starts(&sha, 0);
    while (crypto == 0) {
        const size_t count = fread(
            s_hash_buffer, 1U, sizeof(s_hash_buffer), file);
        if (count == 0U) {
            break;
        }
        if (captured_data != NULL) {
            if (total_bytes > expected_bytes ||
                (uint64_t)count > expected_bytes - total_bytes) {
                capture_bounds_valid = false;
                break;
            }
            memcpy(captured_data + (size_t)total_bytes,
                   s_hash_buffer, count);
        }
        content_validation_note_bytes(count);
        if (header_bytes < sizeof(header)) {
            size_t copy = sizeof(header) - header_bytes;
            if (copy > count) {
                copy = count;
            }
            memcpy(&header[header_bytes], s_hash_buffer, copy);
            header_bytes += copy;
        }
        crypto = mbedtls_sha256_update(&sha, s_hash_buffer, count);
        total_bytes += (uint64_t)count;
    }
    bool valid = crypto == 0 && capture_bounds_valid && ferror(file) == 0;
    if (valid) {
        crypto = mbedtls_sha256_finish(&sha, digest);
        valid = crypto == 0;
    }
    mbedtls_sha256_free(&sha);
    if (fclose(file) != 0) {
        valid = false;
    }
    valid = valid && total_bytes == expected_bytes &&
        (!require_wad_header ||
         (header_bytes == sizeof(header) &&
          doom_header_valid(header, total_bytes, allow_pwad))) &&
        memcmp(digest, expected_sha256, sizeof(digest)) == 0;
    if (valid && out_data != NULL) {
        *out_data = captured_data;
    } else {
        heap_caps_free(captured_data);
    }
    *out_error = valid ? ESP_OK : ESP_ERR_INVALID_CRC;
    return valid ? GAME_STORAGE_CONTENT_READY
                 : GAME_STORAGE_CONTENT_INVALID;
}

static game_storage_content_t inspect_doom_wad(esp_err_t *out_error)
{
    return inspect_exact_file(
        PLATFORM_GAME_STORAGE_DOOM_WAD_PATH,
        PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES,
        s_expected_doom_sha256, true, false, out_error, NULL);
}

static game_storage_content_t inspect_chex_data(esp_err_t *out_error)
{
    esp_err_t wad_error = ESP_OK;
    const game_storage_content_t wad = inspect_exact_file(
        PLATFORM_GAME_STORAGE_CHEX_WAD_PATH,
        PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES,
        s_expected_chex_sha256, true, true, &wad_error, NULL);
    esp_err_t deh_error = ESP_OK;
    const game_storage_content_t deh = inspect_exact_file(
        PLATFORM_GAME_STORAGE_CHEX_DEH_PATH,
        PLATFORM_GAME_STORAGE_CHEX_DEH_BYTES,
        s_expected_chex_deh_sha256, false, false, &deh_error, NULL);
    if (wad == GAME_STORAGE_CONTENT_READY &&
        deh == GAME_STORAGE_CONTENT_READY) {
        *out_error = ESP_OK;
        return GAME_STORAGE_CONTENT_READY;
    }
    if (wad == GAME_STORAGE_CONTENT_INVALID ||
        deh == GAME_STORAGE_CONTENT_INVALID) {
        *out_error = wad == GAME_STORAGE_CONTENT_INVALID
            ? wad_error : deh_error;
        return GAME_STORAGE_CONTENT_INVALID;
    }
    *out_error = wad != GAME_STORAGE_CONTENT_READY ? wad_error : deh_error;
    return GAME_STORAGE_CONTENT_MISSING;
}

static game_storage_content_t inspect_doom_title_snapshot(
    platform_game_storage_doom_title_t title,
    esp_err_t *out_error)
{
    if (out_error == NULL ||
        title >= PLATFORM_GAME_STORAGE_DOOM_TITLE_COUNT) {
        if (out_error != NULL) {
            *out_error = ESP_ERR_INVALID_ARG;
        }
        return GAME_STORAGE_CONTENT_INVALID;
    }

    release_locked_doom_snapshot();
    const int64_t started_us = esp_timer_get_time();
    game_storage_content_t selected = GAME_STORAGE_CONTENT_INVALID;
    if (title == PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM) {
        selected = inspect_exact_file(
            PLATFORM_GAME_STORAGE_DOOM_WAD_PATH,
            PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES,
            s_expected_doom_sha256, true, false, out_error,
            &s_locked_wad_data);
        if (selected == GAME_STORAGE_CONTENT_READY) {
            s_locked_wad_size_bytes =
                (size_t)PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES;
        }
    } else {
        esp_err_t wad_error = ESP_OK;
        const game_storage_content_t wad = inspect_exact_file(
            PLATFORM_GAME_STORAGE_CHEX_WAD_PATH,
            PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES,
            s_expected_chex_sha256, true, true, &wad_error,
            &s_locked_wad_data);
        esp_err_t deh_error = ESP_OK;
        game_storage_content_t deh = GAME_STORAGE_CONTENT_MISSING;
        if (wad == GAME_STORAGE_CONTENT_READY) {
            s_locked_wad_size_bytes =
                (size_t)PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES;
            deh = inspect_exact_file(
                PLATFORM_GAME_STORAGE_CHEX_DEH_PATH,
                PLATFORM_GAME_STORAGE_CHEX_DEH_BYTES,
                s_expected_chex_deh_sha256, false, false, &deh_error,
                &s_locked_deh_data);
        }
        if (wad == GAME_STORAGE_CONTENT_READY &&
            deh == GAME_STORAGE_CONTENT_READY) {
            s_locked_deh_size_bytes =
                (size_t)PLATFORM_GAME_STORAGE_CHEX_DEH_BYTES;
            *out_error = ESP_OK;
            selected = GAME_STORAGE_CONTENT_READY;
        } else if (wad == GAME_STORAGE_CONTENT_INVALID ||
                   deh == GAME_STORAGE_CONTENT_INVALID) {
            *out_error = wad == GAME_STORAGE_CONTENT_INVALID
                ? wad_error : deh_error;
            selected = GAME_STORAGE_CONTENT_INVALID;
        } else {
            *out_error = wad != GAME_STORAGE_CONTENT_READY
                ? wad_error : deh_error;
            selected = GAME_STORAGE_CONTENT_MISSING;
        }
    }

    if (selected != GAME_STORAGE_CONTENT_READY || *out_error != ESP_OK) {
        release_locked_doom_snapshot();
        return selected;
    }
    s_locked_snapshot_title = title;
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE LOCKED_SNAPSHOT_READY title=%s "
             "wad_bytes=%u deh_bytes=%u source=single-pass-exact-sha256 "
             "target=psram elapsed_ms=%lld",
             title == PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM
                 ? "doom" : "chex",
             (unsigned)s_locked_wad_size_bytes,
             (unsigned)s_locked_deh_size_bytes,
             (long long)((esp_timer_get_time() - started_us) /
                 INT64_C(1000)));
    return GAME_STORAGE_CONTENT_READY;
}

static esp_err_t refresh_locked(void)
{
#if P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
    if (!game_storage_model_begin_scan(&s_model)) {
        if (s_model.owner != GAME_STORAGE_OWNER_APP) {
            return ESP_ERR_INVALID_STATE;
        }
        if (s_model.content == GAME_STORAGE_CONTENT_SCANNING) {
            return ESP_OK;
        }
        return s_model.content == GAME_STORAGE_CONTENT_READY
            ? ESP_OK : s_last_error;
    }
    if (s_scans != UINT32_MAX) {
        ++s_scans;
    }
    s_doom_content = GAME_STORAGE_CONTENT_UNKNOWN;
    s_chex_content = GAME_STORAGE_CONTENT_UNKNOWN;
    s_content_validation_complete = false;
    s_content_validation_progress_percent = 0U;
    s_content_validation_progress_bytes = 0U;
    s_last_error = ESP_OK;
    /* General storage readiness is independent of optional Doom-engine data.
     * Exact WAD validation is explicitly started by a Doom/Chex launch or by
     * the Multiplayer page, never by ordinary boot. */
    game_storage_model_finish_scan(&s_model, GAME_STORAGE_CONTENT_READY);
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE CONTENT_VALIDATION_DEFERRED generation=%lu "
             "bytes=%llu trigger=doom-chex-demand full_sha256=required",
             (unsigned long)s_model.generation,
             (unsigned long long)(PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES +
                 PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES +
                 PLATFORM_GAME_STORAGE_CHEX_DEH_BYTES));
    return ESP_OK;
#else
    if (!game_storage_model_begin_scan(&s_model)) {
        if (s_model.owner != GAME_STORAGE_OWNER_APP) {
            return ESP_ERR_INVALID_STATE;
        }
        return s_model.content == GAME_STORAGE_CONTENT_READY
            ? ESP_OK : s_last_error;
    }
    if (s_scans != UINT32_MAX) {
        ++s_scans;
    }
    esp_err_t doom_error = ESP_OK;
    esp_err_t chex_error = ESP_OK;
    s_doom_content = inspect_doom_wad(&doom_error);
    s_chex_content = inspect_chex_data(&chex_error);
    game_storage_content_t content = GAME_STORAGE_CONTENT_MISSING;
    esp_err_t scan_error = doom_error;
    if (s_doom_content == GAME_STORAGE_CONTENT_READY ||
        s_chex_content == GAME_STORAGE_CONTENT_READY) {
        content = GAME_STORAGE_CONTENT_READY;
        scan_error = ESP_OK;
    } else if (s_doom_content == GAME_STORAGE_CONTENT_INVALID ||
               s_chex_content == GAME_STORAGE_CONTENT_INVALID) {
        content = GAME_STORAGE_CONTENT_INVALID;
        scan_error = s_doom_content == GAME_STORAGE_CONTENT_INVALID
            ? doom_error : chex_error;
    } else if (scan_error == ESP_OK) {
        scan_error = chex_error;
    }
    game_storage_model_finish_scan(&s_model, content);
    s_last_error = scan_error;
    return content == GAME_STORAGE_CONTENT_READY ? ESP_OK : scan_error;
#endif
}

static platform_game_storage_state_t public_state_locked(void)
{
    if (!s_initialized) {
        return PLATFORM_GAME_STORAGE_UNINITIALIZED;
    }
    if (s_model.owner == GAME_STORAGE_OWNER_FAULT) {
        return PLATFORM_GAME_STORAGE_FAULT;
    }
    if (s_model.format_required) {
        return PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED;
    }
    if (s_maintenance || s_model.owner == GAME_STORAGE_OWNER_TRANSITION ||
        s_model.launch_pending) {
        return PLATFORM_GAME_STORAGE_TRANSITION;
    }
    if (s_model.owner == GAME_STORAGE_OWNER_GAME) {
        return PLATFORM_GAME_STORAGE_GAME_LOCKED;
    }
    if (s_model.owner == GAME_STORAGE_OWNER_USB) {
        return PLATFORM_GAME_STORAGE_USB_HOST;
    }
    if (s_model.owner != GAME_STORAGE_OWNER_APP) {
        return PLATFORM_GAME_STORAGE_FAULT;
    }
    switch (s_model.content) {
    case GAME_STORAGE_CONTENT_SCANNING:
    case GAME_STORAGE_CONTENT_UNKNOWN:
        return PLATFORM_GAME_STORAGE_APP_SCANNING;
    case GAME_STORAGE_CONTENT_READY:
        return PLATFORM_GAME_STORAGE_APP_READY;
    case GAME_STORAGE_CONTENT_MISSING:
        return PLATFORM_GAME_STORAGE_APP_MISSING;
    case GAME_STORAGE_CONTENT_INVALID:
    default:
        return PLATFORM_GAME_STORAGE_APP_INVALID;
    }
}

static esp_err_t fail_initialization(esp_err_t error)
{
    if (lock_storage()) {
        game_storage_model_mount_failed(&s_model, false);
        s_last_error = error;
        unlock_storage();
    }
    return error;
}

#if P4_GAME_STORAGE_SD_BACKEND
static esp_err_t read_fat_free_space_without_fsinfo_write(
    uint64_t *out_free_bytes)
{
    if (out_free_bytes == NULL || s_card == NULL) {
        return out_free_bytes == NULL
            ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
    }
    *out_free_bytes = 0U;
    const BYTE drive = ff_diskio_get_pdrv_card(s_card);
    if (drive >= FF_VOLUMES || drive > 9U) {
        return ESP_ERR_INVALID_STATE;
    }
    TCHAR path[3] = {(TCHAR)('0' + drive), ':', '\0'};
    FATFS *filesystem = NULL;
    DWORD free_clusters = 0U;
    const FRESULT fat_result =
        f_getfree(path, &free_clusters, &filesystem);
    if (fat_result != FR_OK || filesystem == NULL) {
        return ESP_FAIL;
    }
    *out_free_bytes = (uint64_t)free_clusters *
        (uint64_t)filesystem->csize * s_sector_size_bytes;
    /* f_getfree marks a freshly counted FAT32 FSInfo cache dirty. This
     * diagnostic is explicitly read-only, so discard only that hint update. */
    filesystem->fsi_flag &= (BYTE)~UINT8_C(1);
    return ESP_OK;
}

static void sd_power_set(bool enabled)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    (void)enabled;
#else
    (void)gpio_set_level(GAME_STORAGE_SD_POWER_GPIO, enabled ? 0 : 1);
#endif
}

static esp_err_t release_sd_resources(void)
{
    esp_err_t result = ESP_OK;
    if (s_card != NULL) {
        if (s_sd_vfs_mounted) {
            result = esp_vfs_fat_sdcard_unmount(
                PLATFORM_GAME_STORAGE_MOUNT_POINT, s_card);
        } else {
            /* Slot 1 belongs to the C6 ESP-Hosted transport. A global
             * deinit tears that live transport down underneath its worker
             * tasks, so storage may release only its own slot 0. */
            result = sdmmc_host_deinit_slot(GAME_STORAGE_SD_SLOT);
            heap_caps_free(s_card);
        }
        s_card = NULL;
        s_sd_vfs_mounted = false;
    }
    if (s_sd_power != NULL) {
        const esp_err_t power_result =
            sd_pwr_ctrl_del_on_chip_ldo(s_sd_power);
        if (result == ESP_OK) {
            result = power_result;
        }
        s_sd_power = NULL;
    }
    sd_power_set(false);
    return result;
}

static esp_err_t mount_sd_at_frequency(uint32_t frequency_khz,
                                       bool mount_app_vfs)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = GAME_STORAGE_SD_SLOT;
    host.flags = GAME_STORAGE_SD_BUS_WIDTH == 4
        ? SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_1BIT
        : SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = (int)frequency_khz;

    const sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = GAME_STORAGE_SD_LDO_CHANNEL,
    };
    esp_err_t result =
        sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_sd_power);
    if (result != ESP_OK) {
        return result;
    }
    host.pwr_ctrl_handle = s_sd_power;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = GAME_STORAGE_SD_CLK_GPIO;
    slot.cmd = GAME_STORAGE_SD_CMD_GPIO;
    slot.d0 = GAME_STORAGE_SD_D0_GPIO;
    slot.d1 = GAME_STORAGE_SD_D1_GPIO;
    slot.d2 = GAME_STORAGE_SD_D2_GPIO;
    slot.d3 = GAME_STORAGE_SD_D3_GPIO;
    slot.d4 = GPIO_NUM_NC;
    slot.d5 = GPIO_NUM_NC;
    slot.d6 = GPIO_NUM_NC;
    slot.d7 = GPIO_NUM_NC;
    slot.cd = GPIO_NUM_NC;
    slot.wp = GPIO_NUM_NC;
    slot.width = GAME_STORAGE_SD_BUS_WIDTH;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
    P4_GAME_STORAGE_USB_EXPORT
    if (!mount_app_vfs) {
    bool slot_initialized = false;
    s_card = heap_caps_calloc(
        1U, sizeof(*s_card), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_card == NULL) {
        result = ESP_ERR_NO_MEM;
    }
    if (result == ESP_OK) {
        result = sdmmc_host_init();
    }
    if (result == ESP_OK) {
        result = sdmmc_host_init_slot(host.slot, &slot);
        slot_initialized = result == ESP_OK;
    }
    if (result == ESP_OK) {
        result = sdmmc_card_init(&host, s_card);
    }
    if (result != ESP_OK) {
        if (slot_initialized) {
            (void)sdmmc_host_deinit_slot(host.slot);
        }
        heap_caps_free(s_card);
        s_card = NULL;
        if (s_sd_power != NULL) {
            (void)sd_pwr_ctrl_del_on_chip_ldo(s_sd_power);
            s_sd_power = NULL;
        }
    }
    } else {
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = GAME_STORAGE_MAX_FILES,
        .allocation_unit_size = 16U * 1024U,
        .disk_status_check_enable = true,
        .use_one_fat = false,
    };
    result = esp_vfs_fat_sdmmc_mount(
        PLATFORM_GAME_STORAGE_MOUNT_POINT, &host, &slot, &mount_config,
        &s_card);
    s_sd_vfs_mounted = result == ESP_OK;
    if (result != ESP_OK) {
        if (s_sd_power != NULL) {
            (void)sd_pwr_ctrl_del_on_chip_ldo(s_sd_power);
            s_sd_power = NULL;
        }
        s_card = NULL;
    }
    }
#else
    (void)mount_app_vfs;
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = GAME_STORAGE_MAX_FILES,
        .allocation_unit_size = 16U * 1024U,
        .disk_status_check_enable = true,
        .use_one_fat = false,
    };
    result = esp_vfs_fat_sdmmc_mount(
        PLATFORM_GAME_STORAGE_MOUNT_POINT, &host, &slot, &mount_config,
        &s_card);
    s_sd_vfs_mounted = result == ESP_OK;
    if (result != ESP_OK) {
        if (s_sd_power != NULL) {
            (void)sd_pwr_ctrl_del_on_chip_ldo(s_sd_power);
            s_sd_power = NULL;
        }
        s_card = NULL;
    }
#endif
    return result;
}

static esp_err_t mount_sd_with_fallback(bool mount_app_vfs)
{
    static const uint32_t frequencies_khz[] = {
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        10000U, 5000U, 1000U, 400U,
#else
        20000U, 10000U, 1000U, 400U,
#endif
    };
    esp_err_t result = ESP_FAIL;
    for (size_t attempt = 0U;
         attempt < sizeof(frequencies_khz) / sizeof(frequencies_khz[0]);
         ++attempt) {
        if (attempt != 0U) {
            sd_power_set(false);
            vTaskDelay(pdMS_TO_TICKS(150));
            sd_power_set(true);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        result = mount_sd_at_frequency(
            frequencies_khz[attempt], mount_app_vfs);
        if (result == ESP_OK) {
            ESP_LOGI(TAG,
                     "P4_GAME_STORAGE SD_READY frequency_khz=%" PRIu32
                     " fallback=%u",
                     frequencies_khz[attempt], (unsigned)attempt);
            return ESP_OK;
        }
        ESP_LOGW(TAG,
                 "P4_GAME_STORAGE SD_RETRY frequency_khz=%" PRIu32
                 " error=%s",
                 frequencies_khz[attempt], esp_err_to_name(result));
    }
    return result;
}

#if P4_GAME_STORAGE_RUNTIME_H2_SWITCH
static esp_err_t runtime_mount_app_storage(void)
{
    esp_err_t result = mount_sd_with_fallback(true);
    if (result != ESP_OK || s_card == NULL) {
        return result == ESP_OK ? ESP_FAIL : result;
    }
    if (s_card->csd.sector_size <= 0 || s_card->csd.capacity == 0U) {
        (void)release_sd_resources();
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_sector_size_bytes = (uint32_t)s_card->csd.sector_size;
    s_capacity_bytes = (uint64_t)s_card->csd.capacity *
        (uint64_t)s_sector_size_bytes;
    return ESP_OK;
}

static esp_err_t runtime_release_usb_storage(void)
{
    bool driver_was_running = false;
    esp_err_t result = uninstall_usb_driver_if_running(
        &driver_was_running);
    if (result != ESP_OK) {
        return result;
    }
    if (s_storage != NULL) {
        result = tinyusb_msc_delete_storage(s_storage);
        if (result != ESP_OK) {
            return result;
        }
        s_storage = NULL;
    }
    result = tinyusb_msc_uninstall_driver();
    if (result != ESP_OK && result != ESP_ERR_NOT_SUPPORTED) {
        return result;
    }
    return release_sd_resources();
}

static esp_err_t runtime_create_usb_storage(void)
{
    esp_err_t result = release_sd_resources();
    if (result != ESP_OK) {
        return result;
    }
    result = mount_sd_with_fallback(false);
    if (result != ESP_OK || s_card == NULL) {
        return result == ESP_OK ? ESP_FAIL : result;
    }

    const tinyusb_msc_driver_config_t msc_driver = {
        .user_flags = {
            .auto_mount_off = 1U,
        },
        .callback = storage_event_callback,
        .callback_arg = NULL,
    };
    result = tinyusb_msc_install_driver(&msc_driver);
    if (result != ESP_OK) {
        return result;
    }
    const tinyusb_msc_storage_config_t storage_config = {
        .medium.card = s_card,
        .fat_fs = {
            .base_path = (char *)PLATFORM_GAME_STORAGE_MOUNT_POINT,
            .config = {
                .max_files = GAME_STORAGE_MAX_FILES,
                .allocation_unit_size = 16U * 1024U,
                .disk_status_check_enable = true,
                .use_one_fat = false,
            },
            .do_not_format = true,
            .format_flags = 0,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    result = tinyusb_msc_new_storage_sdmmc(
        &storage_config, &s_storage);
    if (result != ESP_OK || s_storage == NULL) {
        return result == ESP_OK ? ESP_FAIL : result;
    }

    uint32_t sectors = 0U;
    result = tinyusb_msc_get_storage_sector_size(
        s_storage, &s_sector_size_bytes);
    if (result == ESP_OK) {
        result = tinyusb_msc_get_storage_capacity(s_storage, &sectors);
    }
    if (result != ESP_OK) {
        return result;
    }
    s_capacity_bytes = (uint64_t)sectors * s_sector_size_bytes;
    result = install_usb_driver();
    if (result == ESP_OK) {
        ESP_LOGI(TAG,
                 "P4_GAME_STORAGE MSC_STORAGE_READY allocation=on-demand "
                 "buffer_bytes=%u dma_free=%u dma_largest=%u",
                 (unsigned)CONFIG_TINYUSB_MSC_BUFSIZE,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    }
    return result;
}
#endif

#endif

esp_err_t platform_game_storage_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    game_storage_model_init(&s_model);
    s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lock_storage);
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_initialized = true;

#if P4_GAME_STORAGE_SD_BACKEND
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || CONFIG_P4_BOARD_M5STACK_TAB5
    esp_err_t result =
#if P4_GAME_STORAGE_RUNTIME_H2_SWITCH
        runtime_mount_app_storage();
#else
        mount_sd_with_fallback(P4_GAME_STORAGE_USB_EXPORT == 0);
#endif
#else
    const gpio_config_t power_gpio = {
        .pin_bit_mask = UINT64_C(1) << GAME_STORAGE_SD_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&power_gpio);
    if (result == ESP_OK) {
        sd_power_set(true);
        vTaskDelay(pdMS_TO_TICKS(100));
        result = mount_sd_with_fallback(true);
    }
#endif
    if (result != ESP_OK || s_card == NULL) {
        (void)release_sd_resources();
        return fail_initialization(result == ESP_OK ? ESP_FAIL : result);
    }
    if (s_card->csd.sector_size <= 0 || s_card->csd.capacity == 0U) {
        (void)release_sd_resources();
        return fail_initialization(ESP_ERR_INVALID_RESPONSE);
    }
    s_sector_size_bytes = (uint32_t)s_card->csd.sector_size;
    s_capacity_bytes = (uint64_t)s_card->csd.capacity *
        (uint64_t)s_sector_size_bytes;

#if P4_GAME_STORAGE_EAGER_USB_STORAGE
    const tinyusb_msc_driver_config_t msc_driver = {
        .user_flags = {
            /* H2 ownership is controlled only by the Console OS button. */
            .auto_mount_off = 1U,
        },
        .callback = storage_event_callback,
        .callback_arg = NULL,
    };
    result = tinyusb_msc_install_driver(&msc_driver);
    if (result != ESP_OK) {
        (void)release_sd_resources();
        return fail_initialization(result);
    }
    const tinyusb_msc_storage_config_t storage_config = {
        .medium.card = s_card,
        .fat_fs = {
            .base_path = (char *)PLATFORM_GAME_STORAGE_MOUNT_POINT,
            .config = {
                .max_files = GAME_STORAGE_MAX_FILES,
                .allocation_unit_size = 16U * 1024U,
                .disk_status_check_enable = true,
                .use_one_fat = false,
            },
            .do_not_format = true,
            .format_flags = 0,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,
    };
    result = tinyusb_msc_new_storage_sdmmc(
        &storage_config, &s_storage);
    if (result != ESP_OK || s_storage == NULL) {
        (void)release_sd_resources();
        return fail_initialization(
            result == ESP_OK ? ESP_FAIL : result);
    }
    uint32_t sectors = 0U;
    result = tinyusb_msc_get_storage_sector_size(
        s_storage, &s_sector_size_bytes);
    if (result == ESP_OK) {
        result = tinyusb_msc_get_storage_capacity(s_storage, &sectors);
    }
    if (result != ESP_OK) {
        return fail_initialization(result);
    }
    s_capacity_bytes = (uint64_t)sectors * s_sector_size_bytes;
#else
    if (mkdir(PLATFORM_GAME_STORAGE_UPDATE_MOUNT_POINT, 0775) != 0) {
        struct stat update_directory;
        if (errno != EEXIST ||
            stat(PLATFORM_GAME_STORAGE_UPDATE_MOUNT_POINT,
                 &update_directory) != 0 ||
            !S_ISDIR(update_directory.st_mode)) {
            const int directory_error = errno;
            (void)release_sd_resources();
            return fail_initialization(
                directory_error == ENOSPC ? ESP_ERR_NO_MEM : ESP_FAIL);
        }
    }
#endif
#if P4_GAME_STORAGE_EAGER_USB_STORAGE
    bool expose_for_recovery = false;
#endif
    if (lock_storage()) {
        if (s_model.owner == GAME_STORAGE_OWNER_NONE) {
            game_storage_model_mount_complete(
                &s_model, GAME_STORAGE_OWNER_APP);
        }
#if P4_GAME_STORAGE_EAGER_USB_STORAGE
        expose_for_recovery = s_model.format_required;
        if (!expose_for_recovery) {
            (void)refresh_locked();
        }
#else
        (void)refresh_locked();
#endif
        unlock_storage();
    }
#if P4_GAME_STORAGE_EAGER_USB_STORAGE
    if (expose_for_recovery) {
        result = tinyusb_msc_set_storage_mount_point(
            s_storage, TINYUSB_MSC_STORAGE_MOUNT_USB);
        if (result != ESP_OK) {
            return fail_initialization(result);
        }
        if (lock_storage()) {
            s_model.owner = GAME_STORAGE_OWNER_USB;
            s_model.transition_target = GAME_STORAGE_OWNER_NONE;
            s_model.format_required = true;
            s_usb_host_ejected = false;
            s_last_error = ESP_ERR_NOT_FOUND;
            unlock_storage();
        }
        ESP_LOGW(TAG,
                 "P4_GAME_STORAGE RECOVERY_EXPORT reason=format-required "
                 "route=h2-usb-device formatting=host-only");
    }
    result = install_usb_driver();
    if (result != ESP_OK) {
        return fail_initialization(result);
    }
#endif
#if P4_GAME_STORAGE_RUNTIME_H2_SWITCH
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE USB_DEVICE_DEFERRED "
             "default_role=controller-host app=usb-drive "
             "msc_storage=on-demand");
#endif
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE READY backend=microSD capacity=%" PRIu64
             " usb_device_export=%u hot_remove=0",
             s_capacity_bytes,
             (unsigned)P4_GAME_STORAGE_USB_EXPORT);
    return ESP_OK;
#else
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
        PLATFORM_GAME_STORAGE_PARTITION_LABEL);
    if (partition == NULL || partition->size <=
        PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES) {
        return fail_initialization(ESP_ERR_NOT_FOUND);
    }
    esp_err_t result = wl_mount(partition, &s_wl_handle);
    if (result != ESP_OK) {
        return fail_initialization(result);
    }

    const tinyusb_msc_driver_config_t msc_driver = {
        .user_flags = {
            .auto_mount_off = 0U,
        },
        .callback = storage_event_callback,
        .callback_arg = NULL,
    };
    result = tinyusb_msc_install_driver(&msc_driver);
    if (result != ESP_OK) {
        return fail_initialization(result);
    }

    const tinyusb_msc_storage_config_t storage_config = {
        .medium.wl_handle = s_wl_handle,
        .fat_fs = {
            .base_path = (char *)PLATFORM_GAME_STORAGE_MOUNT_POINT,
            .config = {
                .max_files = GAME_STORAGE_MAX_FILES,
                .allocation_unit_size = 4096U,
            },
            .do_not_format = true,
            .format_flags = 0,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,
    };
    result = tinyusb_msc_new_storage_spiflash(
        &storage_config, &s_storage);
    if (result != ESP_OK || s_storage == NULL) {
        return fail_initialization(
            result == ESP_OK ? ESP_FAIL : result);
    }

    uint32_t sectors = 0U;
    result = tinyusb_msc_get_storage_sector_size(
        s_storage, &s_sector_size_bytes);
    if (result == ESP_OK) {
        result = tinyusb_msc_get_storage_capacity(s_storage, &sectors);
    }
    if (result != ESP_OK) {
        return fail_initialization(result);
    }
    s_capacity_bytes = (uint64_t)sectors * s_sector_size_bytes;

    if (lock_storage()) {
        if (s_model.owner == GAME_STORAGE_OWNER_NONE) {
            game_storage_model_mount_complete(
                &s_model, GAME_STORAGE_OWNER_APP);
        }
        if (s_model.format_required) {
            unlock_storage();
            (void)tinyusb_msc_set_storage_mount_point(
                s_storage, TINYUSB_MSC_STORAGE_MOUNT_USB);
            if (lock_storage()) {
                s_model.owner = GAME_STORAGE_OWNER_USB;
                s_model.transition_target = GAME_STORAGE_OWNER_NONE;
                /* A USB mount transition must not hide the repair state. */
                s_model.format_required = true;
                unlock_storage();
            }
        } else {
            (void)refresh_locked();
            unlock_storage();
        }
    }

    result = install_usb_driver();
    return result == ESP_OK ? ESP_OK : fail_initialization(result);
#endif
}

esp_err_t platform_game_storage_refresh(void)
{
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = refresh_locked();
    unlock_storage();
    return result;
}

#if P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
static void content_validation_worker(void *unused)
{
    (void)unused;
    const int64_t started_us = esp_timer_get_time();
    const uint32_t generation = s_content_validation_generation;
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE CONTENT_VALIDATION_BEGIN generation=%lu "
             "doom_bytes=%llu chex_bytes=%llu worker=low-priority",
             (unsigned long)generation,
             (unsigned long long)PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES,
             (unsigned long long)(PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES +
                 PLATFORM_GAME_STORAGE_CHEX_DEH_BYTES));

    esp_err_t doom_error = ESP_OK;
    esp_err_t chex_error = ESP_OK;
    const game_storage_content_t doom = inspect_doom_wad(&doom_error);
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE CONTENT_VALIDATION_TITLE title=doom "
             "result=%s progress=%u",
             esp_err_to_name(doom_error),
             (unsigned)s_content_validation_progress_percent);
    const game_storage_content_t chex = inspect_chex_data(&chex_error);

    esp_err_t scan_error = doom_error;
    if (doom == GAME_STORAGE_CONTENT_READY ||
        chex == GAME_STORAGE_CONTENT_READY) {
        scan_error = ESP_OK;
    } else if (doom == GAME_STORAGE_CONTENT_INVALID ||
               chex == GAME_STORAGE_CONTENT_INVALID) {
        scan_error = doom == GAME_STORAGE_CONTENT_INVALID
            ? doom_error : chex_error;
    } else if (scan_error == ESP_OK) {
        scan_error = chex_error;
    }

    bool applied = false;
    if (lock_storage()) {
        applied = s_content_validation_running &&
            s_model.owner == GAME_STORAGE_OWNER_APP &&
            s_model.generation == generation &&
            s_model.content == GAME_STORAGE_CONTENT_READY;
        if (applied) {
            s_doom_content = doom;
            s_chex_content = chex;
            s_last_error = scan_error;
            s_content_validation_complete = true;
            s_content_validation_progress_percent = 100U;
        }
        s_content_validation_running = false;
        s_content_validation_task = NULL;
        unlock_storage();
    }
    const unsigned low_water_bytes =
        (unsigned)uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE CONTENT_VALIDATION_COMPLETE applied=%u "
             "generation=%lu doom=%s chex=%s result=%s elapsed_ms=%lld "
             "worker_low_water_bytes=%u",
             applied ? 1U : 0U, (unsigned long)generation,
             doom == GAME_STORAGE_CONTENT_READY ? "ready" : "not-ready",
             chex == GAME_STORAGE_CONTENT_READY ? "ready" : "not-ready",
             esp_err_to_name(scan_error),
             (long long)((esp_timer_get_time() - started_us) /
                 INT64_C(1000)),
             low_water_bytes);
    vTaskDelete(NULL);
}
#endif

esp_err_t platform_game_storage_start_content_validation(void)
{
#if !P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
    return platform_game_storage_refresh();
#else
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_content_validation_running || s_content_validation_complete) {
        unlock_storage();
        return ESP_OK;
    }
    if (s_model.owner != GAME_STORAGE_OWNER_APP ||
        s_model.content != GAME_STORAGE_CONTENT_READY) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }
    s_content_validation_running = true;
    s_content_validation_progress_percent = 0U;
    s_content_validation_progress_bytes = 0U;
    s_content_validation_generation = s_model.generation;
    const BaseType_t created = xTaskCreate(
        content_validation_worker, "wad_verify",
        GAME_STORAGE_CONTENT_SCAN_STACK_BYTES, NULL,
        tskIDLE_PRIORITY + 1U, &s_content_validation_task);
    if (created != pdPASS) {
        s_content_validation_running = false;
        s_content_validation_complete = true;
        s_content_validation_task = NULL;
        s_last_error = ESP_ERR_NO_MEM;
        unlock_storage();
        ESP_LOGE(TAG,
                 "P4_GAME_STORAGE CONTENT_VALIDATION_START_FAILED "
                 "error=%s",
                 esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    unlock_storage();
    return ESP_OK;
#endif
}

esp_err_t platform_game_storage_check_card(void)
{
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_maintenance ||
#if P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
        s_content_validation_running ||
#endif
        s_model.owner != GAME_STORAGE_OWNER_APP ||
        s_model.launch_pending) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }
    s_maintenance = true;
    s_model.content = GAME_STORAGE_CONTENT_UNKNOWN;
    if (!game_storage_model_begin_scan(&s_model)) {
        s_maintenance = false;
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }
    unlock_storage();

    esp_err_t result = ESP_OK;
#if P4_GAME_STORAGE_SD_BACKEND
    if (s_card == NULL || !s_sd_vfs_mounted) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        result = sdmmc_get_status(s_card);
    }
#endif
    uint64_t free_bytes = 0U;
    if (result == ESP_OK) {
        result = read_fat_free_space_without_fsinfo_write(&free_bytes);
    }
    uint32_t root_entries = 0U;
    if (result == ESP_OK) {
        DIR *const root = opendir(PLATFORM_GAME_STORAGE_MOUNT_POINT);
        if (root == NULL) {
            result = ESP_FAIL;
        } else {
            errno = 0;
            for (struct dirent *entry = readdir(root);
                 entry != NULL; entry = readdir(root)) {
                if ((strcmp(entry->d_name, ".") == 0) ||
                    (strcmp(entry->d_name, "..") == 0)) {
                    continue;
                }
                if (root_entries != UINT32_MAX) {
                    ++root_entries;
                }
            }
            if (errno != 0 || closedir(root) != 0) {
                result = ESP_FAIL;
            }
        }
    }
    esp_err_t content_error = result;
    game_storage_content_t content = GAME_STORAGE_CONTENT_INVALID;
    if (result == ESP_OK) {
        content = inspect_doom_wad(&content_error);
    }
    esp_err_t release_result = ESP_OK;
#if P4_GAME_STORAGE_SD_BACKEND
    if (result != ESP_OK) {
        release_result = release_sd_resources();
    }
#endif

    if (lock_storage()) {
        if (s_checks != UINT32_MAX) {
            ++s_checks;
        }
        s_root_entries = root_entries;
        s_free_bytes = result == ESP_OK ? free_bytes : 0U;
        s_last_check_error = result;
        s_maintenance = false;
        if (result == ESP_OK) {
            if (s_scans != UINT32_MAX) {
                ++s_scans;
            }
            game_storage_model_finish_scan(&s_model, content);
            s_last_error = content_error;
        } else {
            game_storage_model_fault(&s_model);
            s_last_error = result != ESP_OK ? result : release_result;
        }
        unlock_storage();
    }
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE CHECK result=%s entries=%" PRIu32
             " free_bytes=%" PRIu64 " content=%s writes=0",
             esp_err_to_name(result), root_entries, s_free_bytes,
             esp_err_to_name(content_error));
    return result;
}

esp_err_t platform_game_storage_retry_card(void)
{
#if !P4_GAME_STORAGE_SD_BACKEND || \
    (P4_GAME_STORAGE_USB_EXPORT && !P4_GAME_STORAGE_RUNTIME_H2_SWITCH)
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_maintenance ||
#if P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
        s_content_validation_running ||
#endif
        !game_storage_model_begin_recovery(
            &s_model, GAME_STORAGE_OWNER_APP)) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_recovery_attempts != UINT32_MAX) {
        ++s_recovery_attempts;
    }
    s_maintenance = true;
    unlock_storage();

    esp_err_t result = release_sd_resources();
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
#if P4_GAME_STORAGE_RUNTIME_H2_SWITCH
        result = runtime_mount_app_storage();
#else
        result = mount_sd_with_fallback(true);
        if (result == ESP_OK && s_card != NULL &&
            s_card->csd.sector_size > 0 && s_card->csd.capacity > 0U) {
            s_sector_size_bytes = (uint32_t)s_card->csd.sector_size;
            s_capacity_bytes = (uint64_t)s_card->csd.capacity *
                s_sector_size_bytes;
        }
#endif
    }
    if (result == ESP_OK && (s_card == NULL || !s_sd_vfs_mounted)) {
        result = ESP_FAIL;
    }
    if (lock_storage()) {
        s_maintenance = false;
        s_last_recovery_error = result;
        game_storage_model_finish_recovery(
            &s_model, GAME_STORAGE_OWNER_APP, result == ESP_OK);
        if (result == ESP_OK) {
            (void)refresh_locked();
        } else {
            s_last_error = result;
        }
        unlock_storage();
    }
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE RETRY result=%s attempt=%" PRIu32
             " destructive=0 format=0",
             esp_err_to_name(result), s_recovery_attempts);
    return result;
#endif
}

esp_err_t platform_game_storage_repair_fat(void)
{
#if !P4_GAME_STORAGE_RUNTIME_H2_SWITCH
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool recovering_fault =
        s_model.owner == GAME_STORAGE_OWNER_FAULT;
    if (s_maintenance || s_model.launch_pending ||
#if P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
        s_content_validation_running ||
#endif
        (s_model.owner != GAME_STORAGE_OWNER_APP && !recovering_fault) ||
        (recovering_fault && !game_storage_model_begin_recovery(
            &s_model, GAME_STORAGE_OWNER_APP))) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_repair_attempts != UINT32_MAX) {
        ++s_repair_attempts;
    }
    s_maintenance = true;
    unlock_storage();

    p4_fat_repair_report_t report;
    memset(&report, 0, sizeof(report));
    esp_err_t result = release_sd_resources();
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        result = mount_sd_with_fallback(false);
    }
    if (result == ESP_OK && s_card != NULL) {
        result = p4_fat_repair_run(s_card, &report);
    } else if (result == ESP_OK) {
        result = ESP_FAIL;
    }
    const esp_err_t repair_result = result;
    const esp_err_t raw_release = release_sd_resources();
    if (result == ESP_OK && raw_release != ESP_OK) {
        result = raw_release;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    const esp_err_t remount_result = runtime_mount_app_storage();
    if (result == ESP_OK && remount_result != ESP_OK) {
        result = remount_result;
    }

    if (lock_storage()) {
        s_maintenance = false;
        s_last_repair_error = result;
        s_repair_sectors_rewritten = report.sectors_rewritten;
        switch (report.outcome) {
        case P4_FAT_REPAIR_CLEAN:
            s_last_repair_outcome = PLATFORM_GAME_STORAGE_REPAIR_CLEAN;
            break;
        case P4_FAT_REPAIR_REPAIRED:
            s_last_repair_outcome = PLATFORM_GAME_STORAGE_REPAIR_REPAIRED;
            break;
        case P4_FAT_REPAIR_NEEDS_HOST:
            s_last_repair_outcome =
                PLATFORM_GAME_STORAGE_REPAIR_NEEDS_HOST;
            break;
        case P4_FAT_REPAIR_UNSUPPORTED:
            s_last_repair_outcome =
                PLATFORM_GAME_STORAGE_REPAIR_UNSUPPORTED;
            break;
        case P4_FAT_REPAIR_FAILED:
        case P4_FAT_REPAIR_NOT_RUN:
        default:
            s_last_repair_outcome = PLATFORM_GAME_STORAGE_REPAIR_FAILED;
            break;
        }
        if (remount_result == ESP_OK) {
            if (recovering_fault) {
                game_storage_model_finish_recovery(
                    &s_model, GAME_STORAGE_OWNER_APP, true);
            } else {
                s_model.content = GAME_STORAGE_CONTENT_UNKNOWN;
            }
            (void)refresh_locked();
            if (repair_result != ESP_OK) {
                s_last_repair_error = repair_result;
            }
        } else {
            if (recovering_fault) {
                game_storage_model_finish_recovery(
                    &s_model, GAME_STORAGE_OWNER_APP, false);
            } else {
                game_storage_model_fault(&s_model);
            }
            s_last_error = remount_result;
        }
        unlock_storage();
    }
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE FAT_REPAIR result=%s remount=%s "
             "outcome=%u sectors_rewritten=%" PRIu32
             " fat_mismatch=%" PRIu32 " format=0",
             esp_err_to_name(repair_result),
             esp_err_to_name(remount_result), (unsigned)report.outcome,
             report.sectors_rewritten, report.fat_mismatch_sectors);
    return result;
#endif
}

esp_err_t platform_game_storage_get_status(
    platform_game_storage_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_status, 0, sizeof(*out_status));
    if (!s_initialized || !lock_storage()) {
        out_status->state = PLATFORM_GAME_STORAGE_UNINITIALIZED;
        out_status->last_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }
    out_status->state = public_state_locked();
    out_status->usb_attached = s_usb_attached;
    out_status->usb_driver_running = s_usb_driver_running;
    out_status->usb_mode_supported =
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 && \
    P4_GAME_STORAGE_USB_EXPORT
        true;
#else
        false;
#endif
    out_status->usb_host_ejected = s_usb_host_ejected;
    out_status->capacity_bytes = s_capacity_bytes;
    out_status->sector_size_bytes = s_sector_size_bytes;
    out_status->generation = s_model.generation;
    out_status->ownership_transfers = s_model.ownership_transfers;
    out_status->mount_failures = s_model.mount_failures;
    out_status->scans = s_scans;
    out_status->usb_verified_writes = s_usb_verified_writes;
    out_status->usb_write_failures = s_usb_write_failures;
    out_status->free_bytes = s_free_bytes;
    out_status->real_frequency_khz =
#if P4_GAME_STORAGE_SD_BACKEND
        s_card != NULL ? (uint32_t)s_card->real_freq_khz : 0U;
#else
        0U;
#endif
    out_status->root_entries = s_root_entries;
    out_status->checks = s_checks;
    out_status->recovery_attempts = s_recovery_attempts;
    out_status->repair_attempts = s_repair_attempts;
    out_status->repair_sectors_rewritten = s_repair_sectors_rewritten;
    out_status->card_ready =
#if P4_GAME_STORAGE_SD_BACKEND
        s_card != NULL;
#else
        true;
#endif
    out_status->filesystem_ready =
#if P4_GAME_STORAGE_SD_BACKEND
        s_sd_vfs_mounted;
#else
        s_model.owner == GAME_STORAGE_OWNER_APP;
#endif
    out_status->doom_wad_ready =
        s_doom_content == GAME_STORAGE_CONTENT_READY;
    out_status->chex_quest_ready =
        s_chex_content == GAME_STORAGE_CONTENT_READY;
#if P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
    out_status->content_validation_running =
        s_content_validation_running;
    out_status->content_validation_complete =
        s_content_validation_complete;
    out_status->content_validation_progress_percent =
        s_content_validation_progress_percent;
#else
    out_status->content_validation_running = false;
    out_status->content_validation_complete = true;
    out_status->content_validation_progress_percent = 100U;
#endif
    out_status->last_repair_outcome = s_last_repair_outcome;
    out_status->last_check_error = s_last_check_error;
    out_status->last_recovery_error = s_last_recovery_error;
    out_status->last_repair_error = s_last_repair_error;
    out_status->last_error = s_last_error;
    unlock_storage();
    return ESP_OK;
}

#if P4_GAME_STORAGE_USB_EXPORT
void platform_game_storage_msc_start_stop(
    uint8_t lun, bool start, bool load_eject)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    if (lun != 0U || !load_eject || !s_initialized || !lock_storage()) {
        return;
    }
    if (s_model.owner == GAME_STORAGE_OWNER_USB) {
        s_usb_host_ejected = !start;
        ESP_LOGI(TAG,
                 "P4_GAME_STORAGE USB_HOST_MEDIA state=%s lun=%u",
                 start ? "loaded" : "ejected", (unsigned)lun);
    }
    unlock_storage();
#else
    (void)lun;
    (void)start;
    (void)load_eject;
#endif
}

esp_err_t platform_game_storage_msc_write10(
    uint8_t lun, uint32_t lba, uint32_t offset,
    const uint8_t *data, size_t size_bytes)
{
    if (data == NULL || !s_initialized || !lock_storage()) {
        return data == NULL ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_OK;
    uint64_t byte_address = 0U;
    const int validation = msc_write_policy_validate(
        lun, lba, offset, size_bytes, s_sector_size_bytes,
        s_capacity_bytes, CONFIG_TINYUSB_MSC_BUFSIZE, &byte_address);
    if (validation != 0) {
        result = validation == EOVERFLOW
            ? ESP_ERR_INVALID_SIZE : ESP_ERR_INVALID_ARG;
    } else if (s_model.owner != GAME_STORAGE_OWNER_USB || s_maintenance ||
               !s_usb_driver_running
#if P4_GAME_STORAGE_SD_BACKEND
               || s_card == NULL
#else
               || s_wl_handle == WL_INVALID_HANDLE
#endif
               ) {
        result = ESP_ERR_INVALID_STATE;
    } else {
#if P4_GAME_STORAGE_SD_BACKEND
        if (s_sector_size_bytes > GAME_STORAGE_SD_TRANSFER_BYTES ||
            GAME_STORAGE_SD_TRANSFER_BYTES % s_sector_size_bytes != 0U) {
            result = ESP_ERR_INVALID_SIZE;
        }
        size_t completed_bytes = 0U;
        while (result == ESP_OK && completed_bytes < size_bytes) {
            const size_t remaining_bytes = size_bytes - completed_bytes;
            const size_t transfer_bytes =
                remaining_bytes < GAME_STORAGE_SD_TRANSFER_BYTES
                    ? remaining_bytes : GAME_STORAGE_SD_TRANSFER_BYTES;
            const size_t transfer_sectors =
                transfer_bytes / s_sector_size_bytes;
            const uint32_t transfer_lba = lba +
                (uint32_t)(completed_bytes / s_sector_size_bytes);
            uint8_t *const readback =
                s_hash_buffer + GAME_STORAGE_SD_TRANSFER_BYTES;
            memcpy(s_hash_buffer, data + completed_bytes, transfer_bytes);
            result = sdmmc_write_sectors(
                s_card, s_hash_buffer, transfer_lba, transfer_sectors);
            if (result == ESP_OK) {
                result = sdmmc_read_sectors(
                    s_card, readback, transfer_lba, transfer_sectors);
            }
            if (result == ESP_OK &&
                memcmp(data + completed_bytes, readback,
                       transfer_bytes) != 0) {
                result = ESP_ERR_INVALID_CRC;
            }
            completed_bytes += transfer_bytes;
        }
#else
        if (byte_address > SIZE_MAX) {
            result = ESP_ERR_INVALID_SIZE;
        }
        const size_t address = (size_t)byte_address;
        if (result == ESP_OK) {
            result = wl_erase_range(s_wl_handle, address, size_bytes);
        }
        if (result == ESP_OK) {
            result = wl_write(s_wl_handle, address, data, size_bytes);
        }
        if (result == ESP_OK) {
            result = wl_read(
                s_wl_handle, address, s_hash_buffer, size_bytes);
        }
        if (result == ESP_OK &&
            memcmp(data, s_hash_buffer, size_bytes) != 0) {
            result = ESP_ERR_INVALID_CRC;
        }
#endif
    }

    if (result == ESP_OK) {
        if (s_usb_verified_writes != UINT32_MAX) {
            ++s_usb_verified_writes;
        }
    } else {
        if (s_usb_write_failures != UINT32_MAX) {
            ++s_usb_write_failures;
        }
        s_last_error = result;
    }
    unlock_storage();
    return result;
}
#endif

static esp_err_t storage_errno_to_esp(int error)
{
    switch (error) {
    case 0:
        return ESP_OK;
    case EINVAL:
    case EISDIR:
        return ESP_ERR_INVALID_ARG;
    case ENOENT:
        return ESP_ERR_NOT_FOUND;
    case ENAMETOOLONG:
        return ESP_ERR_INVALID_SIZE;
    case ENOMEM:
        return ESP_ERR_NO_MEM;
    case EBUSY:
        return ESP_ERR_INVALID_STATE;
    default:
        return ESP_FAIL;
    }
}

static esp_err_t list_directory(
    const char *relative_path,
    platform_game_storage_file_listing_t *out_listing)
{
    if (relative_path == NULL || out_listing == NULL ||
        !game_storage_files_relative_path_valid(relative_path)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_listing, 0, sizeof(*out_listing));
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_maintenance || !game_storage_model_files_available(&s_model)) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }

    const int files_result = game_storage_files_list_directory(
        PLATFORM_GAME_STORAGE_MOUNT_POINT, relative_path, out_listing);
    const esp_err_t result = storage_errno_to_esp(files_result);
    if (result == ESP_OK) {
        out_listing->storage_generation = s_model.generation;
        out_listing->mutation_count = s_file_mutations;
    }
    unlock_storage();
    return result;
}

esp_err_t platform_game_storage_list_root(
    platform_game_storage_file_listing_t *out_listing)
{
    return list_directory("", out_listing);
}

esp_err_t platform_game_storage_list_directory(
    const char *relative_path,
    platform_game_storage_file_listing_t *out_listing)
{
    return list_directory(relative_path, out_listing);
}

esp_err_t platform_game_storage_list_games(
    platform_game_storage_file_listing_t *out_listing)
{
    return list_directory(
        PLATFORM_GAME_STORAGE_GAMES_DIRECTORY_NAME, out_listing);
}

esp_err_t platform_game_storage_remove_file(
    const char *relative_path, const char *name)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    (void)relative_path;
    (void)name;
    return ESP_ERR_NOT_ALLOWED;
#else
    if (!game_storage_files_relative_path_valid(relative_path) ||
        !game_storage_files_root_name_valid(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_maintenance || !game_storage_model_files_available(&s_model)) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }

    const int files_result = game_storage_files_remove_file(
        PLATFORM_GAME_STORAGE_MOUNT_POINT, relative_path, name);
    const esp_err_t result = storage_errno_to_esp(files_result);
    if (result == ESP_OK) {
        if (s_file_mutations != UINT32_MAX) {
            ++s_file_mutations;
        }
        s_model.content = GAME_STORAGE_CONTENT_UNKNOWN;
        s_last_error = ESP_OK;
    } else {
        s_last_error = result;
    }
    unlock_storage();
    return result;
#endif
}

esp_err_t platform_game_storage_remove_root_file(const char *name)
{
    return platform_game_storage_remove_file("", name);
}

esp_err_t platform_game_storage_remove_game_file(const char *name)
{
    return platform_game_storage_remove_file(
        PLATFORM_GAME_STORAGE_GAMES_DIRECTORY_NAME, name);
}

esp_err_t platform_game_storage_remove_update_file(const char *name)
{
    return platform_game_storage_remove_file(
        PLATFORM_GAME_STORAGE_UPDATE_DIRECTORY_NAME, name);
}

static esp_err_t compose_file_path(
    const char *directory, const char *name,
    char path[GAME_STORAGE_PATH_BYTES])
{
    if (directory == NULL || !game_storage_files_root_name_valid(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    const int written = snprintf(
        path, GAME_STORAGE_PATH_BYTES, "%s/%s", directory, name);
    return written > 0 && (size_t)written < GAME_STORAGE_PATH_BYTES
        ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t regular_file_size(const char *path, size_t maximum_bytes,
                                   size_t *out_size_bytes)
{
    struct stat metadata;
    if (path == NULL || out_size_bytes == NULL || maximum_bytes == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (stat(path, &metadata) != 0) {
        return storage_errno_to_esp(errno);
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_size <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint64_t bytes = (uint64_t)metadata.st_size;
    if (bytes > maximum_bytes || bytes > SIZE_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    *out_size_bytes = (size_t)bytes;
    return ESP_OK;
}

static esp_err_t load_regular_file(
    const char *path, size_t maximum_bytes,
    uint8_t **out_data, size_t *out_size_bytes)
{
    if (out_data == NULL || out_size_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_data = NULL;
    *out_size_bytes = 0U;
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_maintenance || !game_storage_model_files_available(&s_model)) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }
    size_t file_bytes = 0U;
    esp_err_t result = regular_file_size(path, maximum_bytes, &file_bytes);
    uint8_t *data = NULL;
    FILE *file = NULL;
    if (result == ESP_OK) {
        data = heap_caps_malloc(
            file_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (data == NULL) {
            data = heap_caps_malloc(file_bytes, MALLOC_CAP_8BIT);
        }
        if (data == NULL) {
            result = ESP_ERR_NO_MEM;
        }
    }
    if (result == ESP_OK) {
        file = fopen(path, "rb");
        if (file == NULL || fread(data, 1U, file_bytes, file) != file_bytes ||
            ferror(file) != 0 || fgetc(file) != EOF) {
            result = ESP_FAIL;
        }
    }
    if (file != NULL && fclose(file) != 0 && result == ESP_OK) {
        result = ESP_FAIL;
    }
    if (result == ESP_OK) {
        *out_data = data;
        *out_size_bytes = file_bytes;
    } else {
        heap_caps_free(data);
    }
    /* Optional catalog/update probes use NOT_FOUND as ordinary absence. */
    s_last_error = result == ESP_ERR_NOT_FOUND ? ESP_OK : result;
    unlock_storage();
    return result;
}

esp_err_t platform_game_storage_load_root_file(
    const char *name, size_t maximum_bytes,
    uint8_t **out_data, size_t *out_size_bytes)
{
    if (out_data == NULL || out_size_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_data = NULL;
    *out_size_bytes = 0U;
    char path[GAME_STORAGE_PATH_BYTES];
    const esp_err_t result = compose_file_path(
        PLATFORM_GAME_STORAGE_MOUNT_POINT, name, path);
    if (result != ESP_OK) {
        return result;
    }
    return load_regular_file(
        path, maximum_bytes, out_data, out_size_bytes);
}

esp_err_t platform_game_storage_load_game_file(
    const char *name, size_t maximum_bytes,
    uint8_t **out_data, size_t *out_size_bytes)
{
    if (out_data == NULL || out_size_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_data = NULL;
    *out_size_bytes = 0U;
    char path[GAME_STORAGE_PATH_BYTES];
    const esp_err_t result = compose_file_path(
        PLATFORM_GAME_STORAGE_GAMES_MOUNT_POINT, name, path);
    if (result != ESP_OK) {
        return result;
    }
    return load_regular_file(
        path, maximum_bytes, out_data, out_size_bytes);
}

esp_err_t platform_game_storage_load_update_file(
    const char *name, size_t maximum_bytes,
    uint8_t **out_data, size_t *out_size_bytes)
{
    if (out_data == NULL || out_size_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_data = NULL;
    *out_size_bytes = 0U;
    char path[GAME_STORAGE_PATH_BYTES];
    const esp_err_t result = compose_file_path(
        PLATFORM_GAME_STORAGE_UPDATE_MOUNT_POINT, name, path);
    if (result != ESP_OK) {
        return result;
    }
    return load_regular_file(
        path, maximum_bytes, out_data, out_size_bytes);
}

void platform_game_storage_release_file(uint8_t *data)
{
    heap_caps_free(data);
}

static esp_err_t stream_regular_file_exclusive(
    const char *path, size_t maximum_bytes,
    platform_game_storage_stream_fn consume, void *context,
    size_t *out_size_bytes)
{
    if (consume == NULL || out_size_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_size_bytes = 0U;
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_maintenance || !game_storage_model_files_available(&s_model)) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }
    size_t file_bytes = 0U;
    esp_err_t result = regular_file_size(path, maximum_bytes, &file_bytes);
    if (result == ESP_OK) {
        s_maintenance = true;
    }
    unlock_storage();
    if (result != ESP_OK) {
        return result;
    }

#if P4_GAME_STORAGE_USB_EXPORT
    bool usb_driver_was_running = false;
    result = uninstall_usb_driver_if_running(&usb_driver_was_running);
#endif
    uint8_t *buffer = NULL;
    FILE *file = NULL;
    if (result == ESP_OK) {
        buffer = heap_caps_malloc(
            GAME_STORAGE_STREAM_BUFFER_BYTES,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        result = buffer == NULL ? ESP_ERR_NO_MEM : ESP_OK;
    }
    if (result == ESP_OK) {
        file = fopen(path, "rb");
        result = file == NULL ? ESP_FAIL : ESP_OK;
    }
    uint64_t offset = 0U;
    while (result == ESP_OK && offset < file_bytes) {
        size_t requested = GAME_STORAGE_STREAM_BUFFER_BYTES;
        if ((uint64_t)requested > file_bytes - offset) {
            requested = (size_t)(file_bytes - offset);
        }
        const size_t count = fread(buffer, 1U, requested, file);
        if (count != requested) {
            result = ESP_FAIL;
            break;
        }
        result = consume(context, buffer, count, offset);
        offset += count;
    }
    if (file != NULL && fclose(file) != 0 && result == ESP_OK) {
        result = ESP_FAIL;
    }
    heap_caps_free(buffer);

#if P4_GAME_STORAGE_USB_EXPORT
    const esp_err_t reinstall = usb_driver_was_running
        ? install_usb_driver() : ESP_OK;
#else
    const esp_err_t reinstall = ESP_OK;
#endif
    if (lock_storage()) {
        s_maintenance = false;
        s_last_error = result != ESP_OK ? result : reinstall;
        unlock_storage();
    }
    if (result == ESP_OK && reinstall == ESP_OK) {
        *out_size_bytes = file_bytes;
        return ESP_OK;
    }
    return result != ESP_OK ? result : reinstall;
}

esp_err_t platform_game_storage_stream_root_file_exclusive(
    const char *name, size_t maximum_bytes,
    platform_game_storage_stream_fn consume, void *context,
    size_t *out_size_bytes)
{
    if (out_size_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_size_bytes = 0U;
    char path[GAME_STORAGE_PATH_BYTES];
    const esp_err_t result = compose_file_path(
        PLATFORM_GAME_STORAGE_MOUNT_POINT, name, path);
    if (result != ESP_OK) {
        return result;
    }
    return stream_regular_file_exclusive(
        path, maximum_bytes, consume, context, out_size_bytes);
}

esp_err_t platform_game_storage_stream_update_file_exclusive(
    const char *name, size_t maximum_bytes,
    platform_game_storage_stream_fn consume, void *context,
    size_t *out_size_bytes)
{
    if (out_size_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_size_bytes = 0U;
    char path[GAME_STORAGE_PATH_BYTES];
    const esp_err_t result = compose_file_path(
        PLATFORM_GAME_STORAGE_UPDATE_MOUNT_POINT, name, path);
    if (result != ESP_OK) {
        return result;
    }
    return stream_regular_file_exclusive(
        path, maximum_bytes, consume, context, out_size_bytes);
}

esp_err_t platform_game_storage_set_usb_mode(bool enabled)
{
#if !CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3 || \
    !P4_GAME_STORAGE_USB_EXPORT
    (void)enabled;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }

    const game_storage_owner_t owner = s_model.owner;
#if P4_GAME_STORAGE_RUNTIME_H2_SWITCH
    if ((enabled && owner == GAME_STORAGE_OWNER_USB &&
         s_usb_driver_running && s_storage != NULL) ||
        (!enabled && owner == GAME_STORAGE_OWNER_APP &&
         !s_usb_driver_running && s_storage == NULL &&
         s_sd_vfs_mounted)) {
#else
    if ((enabled && owner == GAME_STORAGE_OWNER_USB) ||
        (!enabled && owner == GAME_STORAGE_OWNER_APP)) {
#endif
        unlock_storage();
        return ESP_OK;
    }
    const bool recovery_export = enabled &&
#if P4_GAME_STORAGE_RUNTIME_H2_SWITCH
        owner == GAME_STORAGE_OWNER_FAULT;
#else
        false;
#endif
    if (s_maintenance || s_model.launch_pending ||
#if P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
        s_content_validation_running ||
#endif
        (enabled && owner != GAME_STORAGE_OWNER_APP &&
         owner != GAME_STORAGE_OWNER_USB && !recovery_export) ||
        (!enabled && owner != GAME_STORAGE_OWNER_USB)) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }
    if (!enabled && s_usb_attached && !s_usb_host_ejected) {
        s_last_error = ESP_ERR_INVALID_STATE;
        unlock_storage();
        ESP_LOGW(TAG,
                 "P4_GAME_STORAGE USB_MODE_REJECT requested=app "
                 "reason=host-not-ejected");
        return ESP_ERR_INVALID_STATE;
    }

    /* Block local operations and verified host writes during the handoff. */
    s_maintenance = true;
    if (enabled) {
        s_usb_host_ejected = false;
    }
#if P4_GAME_STORAGE_RUNTIME_H2_SWITCH
    if (recovery_export) {
        if (!game_storage_model_begin_recovery(
                &s_model, GAME_STORAGE_OWNER_USB)) {
            s_maintenance = false;
            unlock_storage();
            return ESP_ERR_INVALID_STATE;
        }
    } else {
        game_storage_model_mount_start(&s_model, owner);
    }
#endif
    unlock_storage();

    esp_err_t result = ESP_OK;
    esp_err_t scan_result = ESP_OK;
#if P4_GAME_STORAGE_RUNTIME_H2_SWITCH
    esp_err_t rollback_result = ESP_OK;
    if (enabled) {
        result = runtime_create_usb_storage();
        if (result != ESP_OK) {
            rollback_result = runtime_release_usb_storage();
            if (rollback_result == ESP_OK) {
                rollback_result = runtime_mount_app_storage();
            }
            ESP_LOGE(TAG,
                     "P4_GAME_STORAGE USB_ROLE_ROLLBACK "
                     "target=controller-host cause=%s rollback=%s",
                     esp_err_to_name(result),
                     esp_err_to_name(rollback_result));
        }
    } else {
        result = runtime_release_usb_storage();
        if (result == ESP_OK) {
            result = runtime_mount_app_storage();
        }
    }

    if (lock_storage()) {
        s_maintenance = false;
        if (result == ESP_OK) {
            if (recovery_export) {
                game_storage_model_finish_recovery(
                    &s_model, GAME_STORAGE_OWNER_USB, true);
            } else {
                game_storage_model_mount_complete(
                    &s_model, enabled
                        ? GAME_STORAGE_OWNER_USB : GAME_STORAGE_OWNER_APP);
            }
            s_last_error = ESP_OK;
            if (!enabled) {
                scan_result = refresh_locked();
            }
        } else if (enabled && rollback_result == ESP_OK) {
            game_storage_model_mount_complete(
                &s_model, GAME_STORAGE_OWNER_APP);
            scan_result = refresh_locked();
            s_last_error = result;
        } else {
            if (recovery_export) {
                game_storage_model_finish_recovery(
                    &s_model, GAME_STORAGE_OWNER_USB, false);
            } else {
                game_storage_model_fault(&s_model);
            }
            s_last_error = result;
        }
        unlock_storage();
    }
#else
    const tinyusb_msc_mount_point_t target = enabled
        ? TINYUSB_MSC_STORAGE_MOUNT_USB
        : TINYUSB_MSC_STORAGE_MOUNT_APP;
    result = tinyusb_msc_set_storage_mount_point(s_storage, target);
    if (lock_storage()) {
        s_maintenance = false;
        if (!enabled && result == ESP_OK &&
            s_model.owner == GAME_STORAGE_OWNER_APP) {
            s_model.content = GAME_STORAGE_CONTENT_UNKNOWN;
            scan_result = refresh_locked();
        } else if (enabled && result != ESP_OK &&
                   s_model.owner == GAME_STORAGE_OWNER_APP) {
            s_model.content = GAME_STORAGE_CONTENT_UNKNOWN;
            scan_result = refresh_locked();
        }
        if (result != ESP_OK) {
            s_last_error = result;
        }
        unlock_storage();
    }
#endif

    ESP_LOGI(TAG,
             "P4_GAME_STORAGE USB_MODE requested=%s result=%s "
             "content_scan=%s runtime_switch=%u",
             enabled ? "usb" : "app", esp_err_to_name(result),
             esp_err_to_name(scan_result),
             (unsigned)P4_GAME_STORAGE_RUNTIME_H2_SWITCH);
    /* A missing optional Doom WAD does not make returning to the OS fail. */
    return result;
#endif
}

esp_err_t platform_game_storage_lock_for_game(void)
{
    return platform_game_storage_lock_for_doom_title(
        PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM);
}

esp_err_t platform_game_storage_lock_for_doom_title(
    platform_game_storage_doom_title_t title)
{
    if (title >= PLATFORM_GAME_STORAGE_DOOM_TITLE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
#if P4_GAME_STORAGE_BACKGROUND_CONTENT_SCAN
    if (s_content_validation_running) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }
#endif
    if (!game_storage_model_begin_game_lock(&s_model)) {
        const esp_err_t error = s_last_error == ESP_OK
            ? ESP_ERR_INVALID_STATE : s_last_error;
        unlock_storage();
        return error;
    }
    release_locked_doom_snapshot();
#if !P4_GAME_STORAGE_USB_EXPORT
    esp_err_t result = ESP_OK;
    const game_storage_content_t selected =
        inspect_doom_title_snapshot(title, &result);
    if (title == PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM) {
        s_doom_content = selected;
    } else {
        s_chex_content = selected;
    }
    const bool ready = selected == GAME_STORAGE_CONTENT_READY &&
        result == ESP_OK && s_model.owner == GAME_STORAGE_OWNER_APP;
    game_storage_model_finish_game_lock(&s_model, ready);
    s_last_error = ready ? ESP_OK : result;
    unlock_storage();
    return ready ? ESP_OK :
        (result == ESP_OK ? ESP_ERR_INVALID_STATE : result);
#else
    unlock_storage();

    bool usb_driver_was_running = false;
    esp_err_t result =
        uninstall_usb_driver_if_running(&usb_driver_was_running);
    if (result != ESP_OK) {
        if (lock_storage()) {
            game_storage_model_finish_game_lock(&s_model, false);
            s_last_error = result;
            unlock_storage();
        }
        return result;
    }

    if (lock_storage()) {
        const bool app_owned = s_model.owner == GAME_STORAGE_OWNER_APP &&
            !s_model.format_required;
        if (app_owned) {
            ESP_LOGI(TAG,
                     "P4_GAME_STORAGE CONTENT_VALIDATION_ON_DEMAND "
                     "title=%s full_sha256=required snapshot=single-pass",
                     title == PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM
                         ? "doom" : "chex");
            const game_storage_content_t selected =
                inspect_doom_title_snapshot(title, &result);
            if (title == PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM) {
                s_doom_content = selected;
            } else {
                s_chex_content = selected;
            }
            if (selected != GAME_STORAGE_CONTENT_READY && result == ESP_OK) {
                result = ESP_ERR_INVALID_STATE;
            }
        } else {
            result = s_last_error == ESP_OK
                ? ESP_ERR_INVALID_STATE : s_last_error;
        }
        const bool ready = app_owned && result == ESP_OK;
        game_storage_model_finish_game_lock(&s_model, ready);
        unlock_storage();
        if (ready) {
            return ESP_OK;
        }
    }

    if (lock_storage()) {
        game_storage_model_finish_game_lock(&s_model, false);
        s_last_error = result == ESP_OK ? ESP_ERR_INVALID_STATE : result;
        unlock_storage();
    }
    const esp_err_t reinstall = usb_driver_was_running
        ? install_usb_driver() : ESP_OK;
    return result != ESP_OK ? result :
        (reinstall != ESP_OK ? reinstall : ESP_ERR_INVALID_STATE);
#endif
}

esp_err_t platform_game_storage_get_locked_doom_snapshot(
    platform_game_storage_doom_title_t title,
    platform_game_storage_doom_snapshot_t *out_snapshot)
{
    if (title >= PLATFORM_GAME_STORAGE_DOOM_TITLE_COUNT ||
        out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t expected_wad_size = title ==
        PLATFORM_GAME_STORAGE_DOOM_TITLE_DOOM
            ? (size_t)PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES
            : (size_t)PLATFORM_GAME_STORAGE_CHEX_WAD_BYTES;
    const size_t expected_deh_size = title ==
        PLATFORM_GAME_STORAGE_DOOM_TITLE_CHEX_QUEST
            ? (size_t)PLATFORM_GAME_STORAGE_CHEX_DEH_BYTES : 0U;
    const bool valid = s_model.owner == GAME_STORAGE_OWNER_GAME &&
        s_model.content == GAME_STORAGE_CONTENT_READY &&
        s_locked_snapshot_title == title &&
        s_locked_wad_data != NULL &&
        s_locked_wad_size_bytes == expected_wad_size &&
        s_locked_deh_size_bytes == expected_deh_size &&
        (expected_deh_size == 0U || s_locked_deh_data != NULL);
    if (valid) {
        out_snapshot->wad_data = s_locked_wad_data;
        out_snapshot->wad_size_bytes = s_locked_wad_size_bytes;
        out_snapshot->deh_data = s_locked_deh_data;
        out_snapshot->deh_size_bytes = s_locked_deh_size_bytes;
    }
    unlock_storage();
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool platform_game_storage_game_locked(void)
{
    if (!s_initialized || !lock_storage()) {
        return false;
    }
    const bool locked = s_model.owner == GAME_STORAGE_OWNER_GAME &&
        s_model.content == GAME_STORAGE_CONTENT_READY &&
        !s_usb_driver_running;
    unlock_storage();
    return locked;
}

const char *platform_game_storage_state_name(
    platform_game_storage_state_t state)
{
    switch (state) {
    case PLATFORM_GAME_STORAGE_UNINITIALIZED: return "uninitialized";
    case PLATFORM_GAME_STORAGE_APP_SCANNING: return "app-scanning";
    case PLATFORM_GAME_STORAGE_APP_READY: return "app-ready";
    case PLATFORM_GAME_STORAGE_APP_MISSING: return "app-missing";
    case PLATFORM_GAME_STORAGE_APP_INVALID: return "app-invalid";
    case PLATFORM_GAME_STORAGE_USB_HOST: return "usb-host";
    case PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED: return "format-required";
    case PLATFORM_GAME_STORAGE_TRANSITION: return "transition";
    case PLATFORM_GAME_STORAGE_GAME_LOCKED: return "game-locked";
    case PLATFORM_GAME_STORAGE_FAULT: return "fault";
    default: return "unknown";
    }
}
