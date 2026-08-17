// SPDX-License-Identifier: MIT

#include "platform/game_storage.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "sdkconfig.h"
#define P4_GAME_STORAGE_SD_BACKEND \
    (CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B || \
     CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3)

#if P4_GAME_STORAGE_SD_BACKEND
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/task.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#else
#include "esp_partition.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "wear_levelling.h"
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
    GAME_STORAGE_STREAM_BUFFER_BYTES = 16384,
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
    GAME_STORAGE_SD_POWER_GPIO = 45,
    GAME_STORAGE_SD_D1_GPIO = 40,
    GAME_STORAGE_SD_D2_GPIO = 41,
    GAME_STORAGE_SD_D3_GPIO = 42,
#endif
#endif
};

#if P4_GAME_STORAGE_SD_BACKEND
static const char *TAG = "game_storage_sd";
#endif

static const uint8_t s_expected_doom_sha256[32] = {
    0x1d, 0x7d, 0x43, 0xbe, 0x50, 0x1e, 0x67, 0xd9,
    0x27, 0xe4, 0x15, 0xe0, 0xb8, 0xf3, 0xe2, 0x9c,
    0x3b, 0xf3, 0x30, 0x75, 0xe8, 0x59, 0x72, 0x18,
    0x16, 0xf6, 0x52, 0xa5, 0x26, 0xca, 0xc7, 0x71,
};

static game_storage_model_t s_model;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
#if P4_GAME_STORAGE_SD_BACKEND
static sdmmc_card_t *s_card;
static sd_pwr_ctrl_handle_t s_sd_power;
#else
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static tinyusb_msc_storage_handle_t s_storage;
#endif
static bool s_initialized;
static bool s_usb_attached;
static bool s_usb_driver_running;
static uint64_t s_capacity_bytes;
static uint32_t s_sector_size_bytes;
static uint32_t s_scans;
static uint32_t s_file_mutations;
static uint32_t s_usb_verified_writes;
static uint32_t s_usb_write_failures;
static esp_err_t s_last_error = ESP_ERR_INVALID_STATE;
static uint8_t s_hash_buffer[GAME_STORAGE_HASH_BUFFER_BYTES];
static bool s_maintenance;

#if !P4_GAME_STORAGE_SD_BACKEND
_Static_assert(GAME_STORAGE_HASH_BUFFER_BYTES >= CONFIG_TINYUSB_MSC_BUFSIZE,
               "USB write verification buffer must hold one MSC transfer");
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

#if !P4_GAME_STORAGE_SD_BACKEND
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
    if (event == NULL || !lock_storage()) {
        return;
    }
    s_usb_attached = event->id == TINYUSB_EVENT_ATTACHED;
    unlock_storage();
}

static esp_err_t install_usb_driver(void)
{
    tinyusb_config_t config =
        TINYUSB_DEFAULT_CONFIG(usb_event_callback, NULL);
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
#endif

static bool doom_header_valid(const uint8_t header[12], uint64_t size_bytes)
{
    if (memcmp(header, "IWAD", 4U) != 0) {
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

static game_storage_content_t inspect_doom_wad(esp_err_t *out_error)
{
    struct stat metadata;
    if (stat(PLATFORM_GAME_STORAGE_DOOM_WAD_PATH, &metadata) != 0) {
        *out_error = errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
        return errno == ENOENT
            ? GAME_STORAGE_CONTENT_MISSING : GAME_STORAGE_CONTENT_INVALID;
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_size < 0 ||
        (uint64_t)metadata.st_size != PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES) {
        *out_error = ESP_ERR_INVALID_SIZE;
        return GAME_STORAGE_CONTENT_INVALID;
    }

    FILE *file = fopen(PLATFORM_GAME_STORAGE_DOOM_WAD_PATH, "rb");
    if (file == NULL) {
        *out_error = ESP_FAIL;
        return GAME_STORAGE_CONTENT_INVALID;
    }
    uint8_t header[12] = {0};
    size_t header_bytes = 0U;
    uint64_t total_bytes = 0U;
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
    bool valid = crypto == 0 && ferror(file) == 0;
    if (valid) {
        crypto = mbedtls_sha256_finish(&sha, digest);
        valid = crypto == 0;
    }
    mbedtls_sha256_free(&sha);
    if (fclose(file) != 0) {
        valid = false;
    }
    valid = valid && total_bytes == PLATFORM_GAME_STORAGE_DOOM_WAD_BYTES &&
        header_bytes == sizeof(header) &&
        doom_header_valid(header, total_bytes) &&
        memcmp(digest, s_expected_doom_sha256, sizeof(digest)) == 0;
    *out_error = valid ? ESP_OK : ESP_ERR_INVALID_CRC;
    return valid ? GAME_STORAGE_CONTENT_READY
                 : GAME_STORAGE_CONTENT_INVALID;
}

static esp_err_t refresh_locked(void)
{
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
    esp_err_t scan_error = ESP_OK;
    const game_storage_content_t content = inspect_doom_wad(&scan_error);
    game_storage_model_finish_scan(&s_model, content);
    s_last_error = scan_error;
    return content == GAME_STORAGE_CONTENT_READY ? ESP_OK : scan_error;
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
        game_storage_model_fault(&s_model);
        s_last_error = error;
        unlock_storage();
    }
    return error;
}

#if P4_GAME_STORAGE_SD_BACKEND
static void sd_power_set(bool enabled)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    (void)enabled;
#else
    (void)gpio_set_level(GAME_STORAGE_SD_POWER_GPIO, enabled ? 0 : 1);
#endif
}

static void release_sd_resources(void)
{
    if (s_card != NULL) {
        (void)esp_vfs_fat_sdcard_unmount(
            PLATFORM_GAME_STORAGE_MOUNT_POINT, s_card);
        s_card = NULL;
    }
    if (s_sd_power != NULL) {
        (void)sd_pwr_ctrl_del_on_chip_ldo(s_sd_power);
        s_sd_power = NULL;
    }
    sd_power_set(false);
}

static esp_err_t mount_sd_at_frequency(uint32_t frequency_khz)
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
    if (result != ESP_OK) {
        if (s_sd_power != NULL) {
            (void)sd_pwr_ctrl_del_on_chip_ldo(s_sd_power);
            s_sd_power = NULL;
        }
        s_card = NULL;
    }
    return result;
}

static esp_err_t mount_sd_with_fallback(void)
{
    static const uint32_t frequencies_khz[] = {
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
        1000U, 400U,
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
        result = mount_sd_at_frequency(frequencies_khz[attempt]);
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
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    esp_err_t result = mount_sd_with_fallback();
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
        result = mount_sd_with_fallback();
    }
#endif
    if (result != ESP_OK || s_card == NULL) {
        release_sd_resources();
        return fail_initialization(result == ESP_OK ? ESP_FAIL : result);
    }
    if (s_card->csd.sector_size <= 0 || s_card->csd.capacity == 0U) {
        release_sd_resources();
        return fail_initialization(ESP_ERR_INVALID_RESPONSE);
    }
    s_sector_size_bytes = (uint32_t)s_card->csd.sector_size;
    s_capacity_bytes = (uint64_t)s_card->csd.capacity *
        (uint64_t)s_sector_size_bytes;

#if !CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    if (mkdir(PLATFORM_GAME_STORAGE_UPDATE_MOUNT_POINT, 0775) != 0) {
        struct stat update_directory;
        if (errno != EEXIST ||
            stat(PLATFORM_GAME_STORAGE_UPDATE_MOUNT_POINT,
                 &update_directory) != 0 ||
            !S_ISDIR(update_directory.st_mode)) {
            const int directory_error = errno;
            release_sd_resources();
            return fail_initialization(
                directory_error == ENOSPC ? ESP_ERR_NO_MEM : ESP_FAIL);
        }
    }
#endif
    if (lock_storage()) {
        game_storage_model_mount_complete(&s_model, GAME_STORAGE_OWNER_APP);
        (void)refresh_locked();
        unlock_storage();
    }
    ESP_LOGI(TAG,
             "P4_GAME_STORAGE READY backend=microSD capacity=%" PRIu64
             " usb_device_export=0 hot_remove=0",
             s_capacity_bytes);
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
    out_status->capacity_bytes = s_capacity_bytes;
    out_status->sector_size_bytes = s_sector_size_bytes;
    out_status->generation = s_model.generation;
    out_status->ownership_transfers = s_model.ownership_transfers;
    out_status->mount_failures = s_model.mount_failures;
    out_status->scans = s_scans;
    out_status->usb_verified_writes = s_usb_verified_writes;
    out_status->usb_write_failures = s_usb_write_failures;
    out_status->last_error = s_last_error;
    unlock_storage();
    return ESP_OK;
}

#if !P4_GAME_STORAGE_SD_BACKEND
esp_err_t platform_game_storage_msc_write10(
    uint8_t lun, uint32_t lba, uint32_t offset,
    const uint8_t *data, size_t size_bytes)
{
    if (data == NULL || !s_initialized || !lock_storage()) {
        return data == NULL ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_OK;
    size_t address = 0U;
    const int validation = msc_write_policy_validate(
        lun, lba, offset, size_bytes, s_sector_size_bytes,
        s_capacity_bytes, CONFIG_TINYUSB_MSC_BUFSIZE, &address);
    if (validation != 0) {
        result = validation == EOVERFLOW
            ? ESP_ERR_INVALID_SIZE : ESP_ERR_INVALID_ARG;
    } else if (s_model.owner != GAME_STORAGE_OWNER_USB || s_maintenance ||
               !s_usb_driver_running || s_wl_handle == WL_INVALID_HANDLE) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        result = wl_erase_range(s_wl_handle, address, size_bytes);
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
    const char *directory,
    platform_game_storage_file_listing_t *out_listing)
{
    if (directory == NULL || out_listing == NULL) {
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

    const int files_result = game_storage_files_list_root(
        directory, out_listing);
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
    return list_directory(
        PLATFORM_GAME_STORAGE_MOUNT_POINT, out_listing);
}

esp_err_t platform_game_storage_list_games(
    platform_game_storage_file_listing_t *out_listing)
{
    return list_directory(
        PLATFORM_GAME_STORAGE_GAMES_MOUNT_POINT, out_listing);
}

esp_err_t platform_game_storage_remove_root_file(const char *name)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    (void)name;
    return ESP_ERR_NOT_ALLOWED;
#else
    if (!game_storage_files_root_name_valid(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_maintenance || !game_storage_model_files_available(&s_model)) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }

    const int files_result = game_storage_files_remove_root_file(
        PLATFORM_GAME_STORAGE_MOUNT_POINT, name);
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

esp_err_t platform_game_storage_remove_game_file(const char *name)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    (void)name;
    return ESP_ERR_NOT_ALLOWED;
#else
    if (!game_storage_files_root_name_valid(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_maintenance || !game_storage_model_files_available(&s_model)) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }

    const int files_result = game_storage_files_remove_root_file(
        PLATFORM_GAME_STORAGE_GAMES_MOUNT_POINT, name);
    const esp_err_t result = storage_errno_to_esp(files_result);
    if (result == ESP_OK) {
        if (s_file_mutations != UINT32_MAX) {
            ++s_file_mutations;
        }
        s_last_error = ESP_OK;
    } else {
        s_last_error = result;
    }
    unlock_storage();
    return result;
#endif
}

esp_err_t platform_game_storage_remove_update_file(const char *name)
{
#if CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    (void)name;
    return ESP_ERR_NOT_ALLOWED;
#else
    if (!game_storage_files_root_name_valid(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_maintenance || !game_storage_model_files_available(&s_model)) {
        unlock_storage();
        return ESP_ERR_INVALID_STATE;
    }

    const int files_result = game_storage_files_remove_root_file(
        PLATFORM_GAME_STORAGE_UPDATE_MOUNT_POINT, name);
    const esp_err_t result = storage_errno_to_esp(files_result);
    if (result == ESP_OK) {
        if (s_file_mutations != UINT32_MAX) {
            ++s_file_mutations;
        }
        s_last_error = ESP_OK;
    } else {
        s_last_error = result;
    }
    unlock_storage();
    return result;
#endif
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

#if !P4_GAME_STORAGE_SD_BACKEND
    result = tinyusb_driver_uninstall();
    const bool usb_driver_stopped = result == ESP_OK;
    if (lock_storage()) {
        s_usb_driver_running = result != ESP_OK;
        s_usb_attached = false;
        unlock_storage();
    }
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

#if P4_GAME_STORAGE_SD_BACKEND
    const esp_err_t reinstall = ESP_OK;
#else
    const esp_err_t reinstall = usb_driver_stopped
        ? install_usb_driver() : ESP_OK;
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

esp_err_t platform_game_storage_lock_for_game(void)
{
    if (!s_initialized || !lock_storage()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!game_storage_model_begin_game_lock(&s_model)) {
        const esp_err_t error = s_last_error == ESP_OK
            ? ESP_ERR_INVALID_STATE : s_last_error;
        unlock_storage();
        return error;
    }
#if P4_GAME_STORAGE_SD_BACKEND
    s_model.content = GAME_STORAGE_CONTENT_UNKNOWN;
    const esp_err_t result = refresh_locked();
    const bool ready = result == ESP_OK &&
        s_model.owner == GAME_STORAGE_OWNER_APP &&
        s_model.content == GAME_STORAGE_CONTENT_READY;
    game_storage_model_finish_game_lock(&s_model, ready);
    s_last_error = ready ? ESP_OK : result;
    unlock_storage();
    return ready ? ESP_OK :
        (result == ESP_OK ? ESP_ERR_INVALID_STATE : result);
#else
    unlock_storage();

    esp_err_t result = tinyusb_driver_uninstall();
    if (lock_storage()) {
        s_usb_driver_running = result != ESP_OK;
        s_usb_attached = false;
        unlock_storage();
    }
    if (result != ESP_OK) {
        if (lock_storage()) {
            game_storage_model_finish_game_lock(&s_model, false);
            s_last_error = result;
            unlock_storage();
        }
        return result;
    }

    result = tinyusb_msc_set_storage_mount_point(
        s_storage, TINYUSB_MSC_STORAGE_MOUNT_APP);
    if (result == ESP_OK && lock_storage()) {
        const bool app_owned = s_model.owner == GAME_STORAGE_OWNER_APP &&
            !s_model.format_required;
        if (app_owned) {
            s_model.content = GAME_STORAGE_CONTENT_UNKNOWN;
            result = refresh_locked();
        } else {
            result = s_last_error == ESP_OK
                ? ESP_ERR_INVALID_STATE : s_last_error;
        }
        const bool ready = app_owned && result == ESP_OK &&
            s_model.content == GAME_STORAGE_CONTENT_READY;
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
    const esp_err_t reinstall = install_usb_driver();
    return result != ESP_OK ? result :
        (reinstall != ESP_OK ? reinstall : ESP_ERR_INVALID_STATE);
#endif
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
