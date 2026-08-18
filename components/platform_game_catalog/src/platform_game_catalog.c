// SPDX-License-Identifier: MIT

#include "platform/game_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mbedtls/sha256.h"
#include "p4/game.h"
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

static bool resource_file_name(
    const char *package_name,
    char output[PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES])
{
    if (!package_file_name(package_name) || output == NULL) {
        return false;
    }
    const size_t length = strlen(package_name);
    if (length >= PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES) {
        return false;
    }
    memcpy(output, package_name, length + 1U);
    output[length - 1U] = 'R';
    return true;
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
    const char *name, bool in_games_directory,
    p4_game_package_info_t *out_info)
{
    uint8_t *data = NULL;
    size_t size_bytes = 0U;
    const esp_err_t loaded = in_games_directory
        ? platform_game_storage_load_game_file(
            name, P4_GAME_PACKAGE_MAX_BYTES, &data, &size_bytes)
        : platform_game_storage_load_root_file(
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
        /* GAMES is scanned first and is the canonical install location. */
        current->valid = false;
        current->validation = P4_GAME_PACKAGE_BAD_METADATA;
        return;
    }
}

static void scan_listing(
    platform_game_catalog_t *catalog,
    const platform_game_storage_file_listing_t *files,
    bool in_games_directory)
{
    for (size_t index = 0U; index < files->entry_count; ++index) {
        const platform_game_storage_file_entry_t *const file =
            &files->entries[index];
        if (file->is_directory || !package_file_name(file->name)) {
            continue;
        }
        if (catalog->entry_count >= PLATFORM_GAME_CATALOG_MAX_ENTRIES) {
            if (catalog->omitted_packages != UINT32_MAX) {
                ++catalog->omitted_packages;
            }
            continue;
        }
        platform_game_catalog_entry_t *const entry =
            &catalog->entries[catalog->entry_count];
        memcpy(entry->file_name, file->name, strlen(file->name) + 1U);
        entry->file_bytes = file->size_bytes;
        entry->in_games_directory = in_games_directory;
        entry->validation = validate_file(
            file->name, in_games_directory, &entry->package);
        entry->valid = entry->validation == P4_GAME_PACKAGE_VALID;
        invalidate_duplicates(catalog, catalog->entry_count);
        ++catalog->entry_count;
    }
}

esp_err_t platform_game_catalog_scan(platform_game_catalog_t *out_catalog)
{
    if (out_catalog == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_catalog, 0, sizeof(*out_catalog));
    platform_game_storage_file_listing_t root_files;
    const esp_err_t root_listed =
        platform_game_storage_list_root(&root_files);
    if (root_listed != ESP_OK) {
        return root_listed;
    }
    out_catalog->available = true;
    out_catalog->storage_generation = root_files.storage_generation;

    platform_game_storage_file_listing_t game_files;
    const esp_err_t games_listed =
        platform_game_storage_list_games(&game_files);
    if (games_listed == ESP_OK) {
        scan_listing(out_catalog, &game_files, true);
    } else if (games_listed != ESP_ERR_NOT_FOUND) {
        return games_listed;
    }
    /* Root packages remain readable for cards created by Console OS 0.3. */
    scan_listing(out_catalog, &root_files, false);
    for (size_t index = 0U; index < out_catalog->entry_count; ++index) {
        if (out_catalog->entries[index].valid) {
            ++out_catalog->valid_count;
        }
    }
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
    const platform_game_catalog_entry_t *const entry =
        &catalog->entries[index];
    const uint32_t capabilities = entry->package.required_capabilities |
        entry->package.optional_capabilities;
    if ((capabilities & P4_GAME_CAP_STORAGE) != 0U) {
        char resource_name[PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES];
        if (!resource_file_name(entry->file_name, resource_name)) {
            return ESP_ERR_INVALID_ARG;
        }
        const esp_err_t resource_removed = entry->in_games_directory
            ? platform_game_storage_remove_game_file(resource_name)
            : platform_game_storage_remove_root_file(resource_name);
        if (resource_removed != ESP_OK &&
            resource_removed != ESP_ERR_NOT_FOUND) {
            return resource_removed;
        }
    }
    return entry->in_games_directory
        ? platform_game_storage_remove_game_file(entry->file_name)
        : platform_game_storage_remove_root_file(entry->file_name);
}
