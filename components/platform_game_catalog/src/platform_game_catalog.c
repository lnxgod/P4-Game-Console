// SPDX-License-Identifier: MIT

#include "platform/game_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mbedtls/sha256.h"
#include "platform/game_storage.h"

static bool package_file_name(const char *name)
{
    if (name == NULL) {
        return false;
    }
    const size_t length = strlen(name);
    return length > 4U &&
        (name[length - 4U] == '.' &&
         (name[length - 3U] == 'P' || name[length - 3U] == 'p') &&
         name[length - 2U] == '4' &&
         (name[length - 1U] == 'G' || name[length - 1U] == 'g'));
}

static p4_game_package_result_t validate_data(
    const uint8_t *data, size_t size_bytes, p4_game_package_info_t *out_info)
{
    p4_game_package_result_t result =
        p4_game_package_parse(data, size_bytes, out_info);
    if (result == P4_GAME_PACKAGE_VALID) {
        uint8_t digest[P4_GAME_PACKAGE_SHA256_BYTES];
        const int crypto = mbedtls_sha256(
            data + out_info->payload_offset, out_info->payload_bytes,
            digest, 0);
        if (crypto != 0 || memcmp(
                digest, out_info->payload_sha256, sizeof(digest)) != 0) {
            result = P4_GAME_PACKAGE_BAD_DIGEST;
        }
    }
    return result;
}

static p4_game_package_result_t validate_file(
    const char *name, p4_game_package_info_t *out_info)
{
    uint8_t *data = NULL;
    size_t size_bytes = 0U;
    const esp_err_t loaded = platform_game_storage_load_root_file(
        name, P4_GAME_PACKAGE_MAX_BYTES, &data, &size_bytes);
    if (loaded != ESP_OK) {
        return loaded == ESP_ERR_INVALID_SIZE
            ? P4_GAME_PACKAGE_BAD_SIZE : P4_GAME_PACKAGE_BAD_LAYOUT;
    }
    const p4_game_package_result_t result =
        validate_data(data, size_bytes, out_info);
    platform_game_storage_release_file(data);
    return result;
}

static void invalidate_duplicates(platform_game_catalog_t *catalog,
                                  size_t candidate)
{
    platform_game_catalog_entry_t *const current =
        &catalog->entries[candidate];
    if (!current->valid) {
        return;
    }
    for (size_t index = 0U; index < candidate; ++index) {
        platform_game_catalog_entry_t *const previous =
            &catalog->entries[index];
        if (!previous->valid ||
            (previous->package.launcher_id != current->package.launcher_id &&
             strcmp(previous->package.id, current->package.id) != 0)) {
            continue;
        }
        previous->valid = false;
        previous->validation = P4_GAME_PACKAGE_BAD_METADATA;
        current->valid = false;
        current->validation = P4_GAME_PACKAGE_BAD_METADATA;
        return;
    }
}

esp_err_t platform_game_catalog_scan(platform_game_catalog_t *out_catalog)
{
    if (out_catalog == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_catalog, 0, sizeof(*out_catalog));
    platform_game_storage_file_listing_t files;
    const esp_err_t listed = platform_game_storage_list_root(&files);
    if (listed != ESP_OK) {
        return listed;
    }
    out_catalog->available = true;
    out_catalog->storage_generation = files.storage_generation;
    for (size_t index = 0U; index < files.entry_count; ++index) {
        const platform_game_storage_file_entry_t *const file =
            &files.entries[index];
        if (file->is_directory || !package_file_name(file->name)) {
            continue;
        }
        if (out_catalog->entry_count >= PLATFORM_GAME_CATALOG_MAX_ENTRIES) {
            if (out_catalog->omitted_packages != UINT32_MAX) {
                ++out_catalog->omitted_packages;
            }
            continue;
        }
        platform_game_catalog_entry_t *const entry =
            &out_catalog->entries[out_catalog->entry_count];
        memcpy(entry->file_name, file->name, strlen(file->name) + 1U);
        entry->file_bytes = file->size_bytes;
        entry->validation = validate_file(file->name, &entry->package);
        entry->valid = entry->validation == P4_GAME_PACKAGE_VALID;
        invalidate_duplicates(out_catalog, out_catalog->entry_count);
        ++out_catalog->entry_count;
    }
    for (size_t index = 0U; index < out_catalog->entry_count; ++index) {
        if (out_catalog->entries[index].valid) {
            ++out_catalog->valid_count;
        }
    }
    return ESP_OK;
}

esp_err_t platform_game_catalog_add_embedded_fallback(
    platform_game_catalog_t *catalog, const char *file_name,
    const uint8_t *data, size_t size_bytes)
{
    if (catalog == NULL || file_name == NULL || data == NULL ||
        !package_file_name(file_name) ||
        strlen(file_name) >= PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES ||
        size_bytes == 0U || size_bytes > P4_GAME_PACKAGE_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    p4_game_package_info_t package;
    const p4_game_package_result_t validation =
        validate_data(data, size_bytes, &package);
    if (validation != P4_GAME_PACKAGE_VALID) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    for (size_t index = 0U; index < catalog->entry_count; ++index) {
        const platform_game_catalog_entry_t *const existing =
            &catalog->entries[index];
        if (existing->valid &&
            (existing->package.launcher_id == package.launcher_id ||
             strcmp(existing->package.id, package.id) == 0)) {
            return ESP_OK;
        }
    }
    size_t target_index = catalog->entry_count;
    if (target_index >= PLATFORM_GAME_CATALOG_MAX_ENTRIES) {
        if (catalog->omitted_packages != UINT32_MAX) {
            ++catalog->omitted_packages;
        }
        target_index = PLATFORM_GAME_CATALOG_MAX_ENTRIES - 1U;
        if (catalog->entries[target_index].valid && catalog->valid_count > 0U) {
            --catalog->valid_count;
        }
    } else {
        ++catalog->entry_count;
    }
    platform_game_catalog_entry_t *const entry =
        &catalog->entries[target_index];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->file_name, file_name, strlen(file_name) + 1U);
    entry->file_bytes = size_bytes;
    entry->package = package;
    entry->validation = validation;
    entry->embedded_data = data;
    entry->embedded_bytes = size_bytes;
    entry->embedded = true;
    entry->valid = true;
    ++catalog->valid_count;
    catalog->available = true;
    return ESP_OK;
}

const platform_game_catalog_entry_t *platform_game_catalog_find_launcher(
    const platform_game_catalog_t *catalog, uint32_t launcher_id)
{
    if (catalog == NULL || !catalog->available) {
        return NULL;
    }
    for (size_t index = 0U; index < catalog->entry_count; ++index) {
        if (catalog->entries[index].valid &&
            catalog->entries[index].package.launcher_id == launcher_id) {
            return &catalog->entries[index];
        }
    }
    return NULL;
}

esp_err_t platform_game_catalog_remove(
    const platform_game_catalog_t *catalog, size_t index)
{
    if (catalog == NULL || !catalog->available ||
        index >= catalog->entry_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (catalog->entries[index].embedded) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return platform_game_storage_remove_root_file(
        catalog->entries[index].file_name);
}
