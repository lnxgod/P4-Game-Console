// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_GAME_CATALOG_H
#define P4_PLATFORM_GAME_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "p4/game_package.h"
#include "platform/game_storage_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PLATFORM_GAME_CATALOG_MAX_ENTRIES = 16,
};

typedef struct {
    char file_name[PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES];
    uint64_t file_bytes;
    p4_game_package_info_t package;
    p4_game_package_result_t validation;
    const uint8_t *embedded_data;
    size_t embedded_bytes;
    bool embedded;
    bool valid;
} platform_game_catalog_entry_t;

typedef struct {
    platform_game_catalog_entry_t entries[PLATFORM_GAME_CATALOG_MAX_ENTRIES];
    size_t entry_count;
    size_t valid_count;
    uint32_t omitted_packages;
    uint32_t storage_generation;
    bool available;
} platform_game_catalog_t;

esp_err_t platform_game_catalog_scan(platform_game_catalog_t *out_catalog);

/**
 * Add a validated in-firmware fallback unless storage already provides the
 * same launcher ID or game ID. The package buffer must remain valid for the
 * lifetime of the catalog.
 */
esp_err_t platform_game_catalog_add_embedded_fallback(
    platform_game_catalog_t *catalog, const char *file_name,
    const uint8_t *data, size_t size_bytes);

const platform_game_catalog_entry_t *platform_game_catalog_find_launcher(
    const platform_game_catalog_t *catalog, uint32_t launcher_id);

esp_err_t platform_game_catalog_remove(
    const platform_game_catalog_t *catalog, size_t index);

#ifdef __cplusplus
}
#endif

#endif
