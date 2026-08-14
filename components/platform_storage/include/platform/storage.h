#ifndef PLATFORM_STORAGE_H
#define PLATFORM_STORAGE_H

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_STORAGE_MOUNT_POINT "/sdcard"
#define PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH "/sdcard/DOOM1.WAD"
#define PLATFORM_STORAGE_FALLBACK_DOOM_WAD_PATH "/sdcard/DOOM/DOOM1.WAD"
#define PLATFORM_STORAGE_PATH_CAPACITY 64U
#define PLATFORM_STORAGE_SHA256_HEX_LENGTH 64U
#define PLATFORM_STORAGE_DOOM_SHAREWARE_1_9_SIZE UINT64_C(4196020)
#define PLATFORM_STORAGE_DOOM_SHAREWARE_1_9_SHA256 \
    "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"

typedef enum {
    PLATFORM_STORAGE_WAD_ID_UNKNOWN = 0,
    PLATFORM_STORAGE_WAD_ID_DOOM_SHAREWARE_1_9,
} platform_storage_wad_id_t;

typedef struct {
    uint64_t capacity_bytes;
    uint32_t sector_size_bytes;
    uint32_t configured_bus_width;
    uint32_t configured_frequency_khz;
    uint32_t real_frequency_khz;
} platform_storage_card_info_t;

typedef struct {
    char path[PLATFORM_STORAGE_PATH_CAPACITY];
    uint64_t size_bytes;
    char sha256[PLATFORM_STORAGE_SHA256_HEX_LENGTH + 1U];
    uint32_t lump_count;
    uint32_t directory_offset;
    platform_storage_wad_id_t identity;
} platform_storage_wad_info_t;

/**
 * Mount the board's SD card at PLATFORM_STORAGE_MOUNT_POINT.
 *
 * The mount is deliberately configured with format_if_mount_failed=false.
 * The reusable service exposes only read/inspect operations, although the
 * underlying ESP-IDF FAT VFS is not a hardware-enforced read-only mount.
 */
esp_err_t platform_storage_init(void);

/** Return basic information for the currently mounted card. */
esp_err_t platform_storage_get_card_info(platform_storage_card_info_t *out_info);

/**
 * Inspect and hash one WAD without modifying it.
 *
 * Returns ESP_ERR_INVALID_RESPONSE for a malformed WAD, ESP_ERR_INVALID_SIZE
 * for the wrong shareware length, or ESP_ERR_INVALID_CRC for a hash mismatch.
 */
esp_err_t platform_storage_inspect_wad(
    const char *path,
    platform_storage_wad_info_t *out_info
);

/**
 * Find the exact Doom 1.9 shareware IWAD.
 *
 * Search order is /sdcard/DOOM1.WAD, then /sdcard/DOOM/DOOM1.WAD. Both use
 * uppercase 8.3-compatible FAT names. If a
 * candidate exists but is malformed or does not match the exact identity,
 * discovery fails closed instead of silently trying another file.
 */
esp_err_t platform_storage_find_doom_shareware(
    platform_storage_wad_info_t *out_info
);

/** Unmount the card and release the SDMMC host. */
esp_err_t platform_storage_deinit(void);

const char *platform_storage_wad_id_name(platform_storage_wad_id_t identity);

#ifdef __cplusplus
}
#endif

#endif
