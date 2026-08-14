#include "platform/storage.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "platform_storage_wad.h"
#include "sdmmc_cmd.h"

#define STORAGE_SDMMC_SLOT SDMMC_HOST_SLOT_0
#define STORAGE_SDMMC_FREQUENCY_KHZ 1000
#define STORAGE_SDMMC_BUS_WIDTH 1
#define STORAGE_SDMMC_CLK GPIO_NUM_43
#define STORAGE_SDMMC_CMD GPIO_NUM_44
#define STORAGE_SDMMC_D0 GPIO_NUM_39
#define STORAGE_HASH_BUFFER_BYTES 4096U
#define STORAGE_HASH_YIELD_BYTES (64U * 1024U)

static sdmmc_card_t *s_card;

static esp_err_t copy_path(char destination[PLATFORM_STORAGE_PATH_CAPACITY], const char *source)
{
    const size_t length = strlen(source);
    if (length >= PLATFORM_STORAGE_PATH_CAPACITY) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(destination, source, length + 1U);
    return ESP_OK;
}

esp_err_t platform_storage_init(void)
{
#if !CONFIG_PLATFORM_STORAGE_ELECROW_10_1_CROSS_REVISION_AUTHORIZED
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (s_card != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16U * 1024U,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = STORAGE_SDMMC_SLOT;
    host.max_freq_khz = STORAGE_SDMMC_FREQUENCY_KHZ;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = STORAGE_SDMMC_CLK;
    slot.cmd = STORAGE_SDMMC_CMD;
    slot.d0 = STORAGE_SDMMC_D0;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.d4 = GPIO_NUM_NC;
    slot.d5 = GPIO_NUM_NC;
    slot.d6 = GPIO_NUM_NC;
    slot.d7 = GPIO_NUM_NC;
    slot.cd = GPIO_NUM_NC;
    slot.wp = GPIO_NUM_NC;
    slot.width = STORAGE_SDMMC_BUS_WIDTH;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    sdmmc_card_t *mounted_card = NULL;
    const esp_err_t result = esp_vfs_fat_sdmmc_mount(
        PLATFORM_STORAGE_MOUNT_POINT,
        &host,
        &slot,
        &mount_config,
        &mounted_card
    );
    if (result != ESP_OK) {
        return result;
    }
    s_card = mounted_card;
    return ESP_OK;
#endif
}

esp_err_t platform_storage_get_card_info(platform_storage_card_info_t *out_info)
{
    if (out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_card->csd.sector_size <= 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const uint32_t sector_size = (uint32_t)s_card->csd.sector_size;
    out_info->capacity_bytes = (uint64_t)s_card->csd.capacity * (uint64_t)sector_size;
    out_info->sector_size_bytes = sector_size;
    out_info->configured_bus_width = STORAGE_SDMMC_BUS_WIDTH;
    out_info->configured_frequency_khz = STORAGE_SDMMC_FREQUENCY_KHZ;
    out_info->real_frequency_khz = s_card->real_freq_khz > 0
        ? (uint32_t)s_card->real_freq_khz
        : 0U;
    return ESP_OK;
}

esp_err_t platform_storage_inspect_wad(
    const char *path,
    platform_storage_wad_info_t *out_info
)
{
    if (path == NULL || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = copy_path(out_info->path, path);
    if (result != ESP_OK) {
        return result;
    }

    struct stat metadata;
    if (stat(path, &metadata) != 0) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_size < 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    out_info->size_bytes = (uint64_t)metadata.st_size;
    if (out_info->size_bytes != PLATFORM_STORAGE_DOOM_SHAREWARE_1_9_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    uint8_t *buffer = malloc(STORAGE_HASH_BUFFER_BYTES);
    if (buffer == NULL) {
        (void)fclose(file);
        return ESP_ERR_NO_MEM;
    }

    uint8_t header_bytes[12] = {0};
    size_t header_count = 0U;
    uint64_t bytes_hashed = 0U;
    size_t bytes_since_yield = 0U;
    uint8_t digest[32];
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    int crypto_result = mbedtls_sha256_starts(&context, 0);
    while (crypto_result == 0) {
        const size_t count = fread(buffer, 1U, STORAGE_HASH_BUFFER_BYTES, file);
        if (count == 0U) {
            break;
        }
        if (header_count < sizeof(header_bytes)) {
            size_t copy_count = sizeof(header_bytes) - header_count;
            if (copy_count > count) {
                copy_count = count;
            }
            memcpy(&header_bytes[header_count], buffer, copy_count);
            header_count += copy_count;
        }
        crypto_result = mbedtls_sha256_update(&context, buffer, count);
        bytes_hashed += (uint64_t)count;
        bytes_since_yield += count;
        if (bytes_since_yield >= STORAGE_HASH_YIELD_BYTES) {
            vTaskDelay(1U);
            bytes_since_yield = 0U;
        }
    }
    if (crypto_result == 0 && ferror(file) != 0) {
        result = ESP_FAIL;
    } else if (crypto_result == 0) {
        crypto_result = mbedtls_sha256_finish(&context, digest);
        result = crypto_result == 0 ? ESP_OK : ESP_FAIL;
    } else {
        result = ESP_FAIL;
    }
    mbedtls_sha256_free(&context);
    free(buffer);
    if (fclose(file) != 0 && result == ESP_OK) {
        result = ESP_FAIL;
    }
    if (result != ESP_OK) {
        return result;
    }
    if (bytes_hashed != out_info->size_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }

    platform_wad_header_t header;
    const platform_wad_header_status_t header_status = platform_wad_parse_header(
        header_bytes,
        header_count,
        out_info->size_bytes,
        &header
    );
    if (header_status != PLATFORM_WAD_HEADER_OK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    out_info->lump_count = header.lump_count;
    out_info->directory_offset = header.directory_offset;
    platform_wad_sha256_hex(digest, out_info->sha256);

    if (!platform_wad_matches_doom_shareware_1_9(out_info->size_bytes, digest)) {
        return ESP_ERR_INVALID_CRC;
    }
    out_info->identity = PLATFORM_STORAGE_WAD_ID_DOOM_SHAREWARE_1_9;
    return ESP_OK;
}

esp_err_t platform_storage_find_doom_shareware(platform_storage_wad_info_t *out_info)
{
    if (out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    static const char *const candidates[] = {
        PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH,
        PLATFORM_STORAGE_FALLBACK_DOOM_WAD_PATH,
    };
    for (size_t index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        struct stat metadata;
        if (stat(candidates[index], &metadata) != 0) {
            if (errno == ENOENT) {
                continue;
            }
            return ESP_FAIL;
        }
        return platform_storage_inspect_wad(candidates[index], out_info);
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t platform_storage_deinit(void)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    sdmmc_card_t *const mounted_card = s_card;
    s_card = NULL;
    const esp_err_t result = esp_vfs_fat_sdcard_unmount(
        PLATFORM_STORAGE_MOUNT_POINT,
        mounted_card
    );
    if (result != ESP_OK) {
        s_card = mounted_card;
    }
    return result;
}

const char *platform_storage_wad_id_name(platform_storage_wad_id_t identity)
{
    switch (identity) {
        case PLATFORM_STORAGE_WAD_ID_DOOM_SHAREWARE_1_9:
            return "doom-shareware-1.9";
        case PLATFORM_STORAGE_WAD_ID_UNKNOWN:
        default:
            return "unknown";
    }
}
