// SPDX-License-Identifier: MIT

#include "platform/game_loader.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_elf.h"
#include "mbedtls/sha256.h"
#include "p4/game_package.h"
#include "p4/game_resource.h"
#include "platform/game_storage.h"

static bool resource_file_name(const char *package_name,
                               char output[
                                   PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES])
{
    if (package_name == NULL || output == NULL) {
        return false;
    }
    const size_t length = strlen(package_name);
    if (length <= 4U || length >= PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES ||
        package_name[length - 4U] != '.' ||
        (package_name[length - 3U] != 'P' &&
         package_name[length - 3U] != 'p') ||
        package_name[length - 2U] != '4' ||
        (package_name[length - 1U] != 'G' &&
         package_name[length - 1U] != 'g')) {
        return false;
    }
    memcpy(output, package_name, length + 1U);
    output[length - 1U] = 'R';
    return true;
}

static bool package_matches_catalog(
    const p4_game_package_info_t *package,
    const platform_game_catalog_entry_t *entry)
{
    return package != NULL && entry != NULL && entry->valid &&
        package->launcher_id == entry->package.launcher_id &&
        package->payload_bytes == entry->package.payload_bytes &&
        package->required_capabilities ==
            entry->package.required_capabilities &&
        package->optional_capabilities ==
            entry->package.optional_capabilities &&
        strcmp(package->id, entry->package.id) == 0 &&
        memcmp(package->payload_sha256, entry->package.payload_sha256,
               sizeof(package->payload_sha256)) == 0;
}

esp_err_t platform_game_loader_run(
    const platform_game_catalog_entry_t *entry,
    p4_cartridge_host_v1_t *host)
{
    if (entry == NULL || host == NULL || !entry->valid ||
        host->magic != P4_CARTRIDGE_HOST_MAGIC ||
        host->api_version != P4_CARTRIDGE_HOST_API_VERSION ||
        host->struct_bytes < sizeof(*host)) {
        return ESP_ERR_INVALID_ARG;
    }
    host->available_capabilities &=
        (uint32_t)~(uint32_t)P4_GAME_CAP_STORAGE;
    host->resource_data = NULL;
    host->resource_bytes = 0U;
    host->resource_format_version = 0U;
    uint8_t *data = NULL;
    size_t size_bytes = 0U;
    esp_err_t result = entry->in_games_directory
        ? platform_game_storage_load_game_file(
            entry->file_name, P4_GAME_PACKAGE_MAX_BYTES,
            &data, &size_bytes)
        : platform_game_storage_load_root_file(
            entry->file_name, P4_GAME_PACKAGE_MAX_BYTES,
            &data, &size_bytes);
    p4_game_package_info_t package = {0};
    if (result == ESP_OK &&
        p4_game_package_parse(data, size_bytes, &package) !=
            P4_GAME_PACKAGE_VALID) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t digest[P4_GAME_PACKAGE_SHA256_BYTES];
    if (result == ESP_OK &&
        (mbedtls_sha256(data + package.payload_offset,
                        package.payload_bytes, digest, 0) != 0 ||
         memcmp(digest, package.payload_sha256, sizeof(digest)) != 0)) {
        result = ESP_ERR_INVALID_CRC;
    }
    if (result == ESP_OK && !package_matches_catalog(&package, entry)) {
        result = ESP_ERR_INVALID_STATE;
    }

    uint8_t *resource_data = NULL;
    size_t resource_size_bytes = 0U;
    p4_game_resource_info_t resource;
    const uint32_t package_capabilities = package.required_capabilities |
        package.optional_capabilities;
    if (result == ESP_OK &&
        (package_capabilities & P4_GAME_CAP_STORAGE) != 0U) {
        char resource_name[PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES];
        if (!resource_file_name(entry->file_name, resource_name)) {
            result = ESP_ERR_INVALID_ARG;
        } else {
            const esp_err_t loaded = entry->in_games_directory
                ? platform_game_storage_load_game_file(
                    resource_name, P4_GAME_RESOURCE_MAX_BYTES,
                    &resource_data, &resource_size_bytes)
                : platform_game_storage_load_root_file(
                    resource_name, P4_GAME_RESOURCE_MAX_BYTES,
                    &resource_data, &resource_size_bytes);
            if (loaded != ESP_OK && loaded != ESP_ERR_NOT_FOUND) {
                result = loaded;
            }
        }
    }
    if (result == ESP_OK && resource_data != NULL) {
        uint8_t resource_digest[P4_GAME_RESOURCE_SHA256_BYTES];
        if (p4_game_resource_parse(
                resource_data, resource_size_bytes, &resource) !=
                P4_GAME_RESOURCE_VALID ||
            strcmp(resource.game_id, package.id) != 0 ||
            mbedtls_sha256(
                resource_data + resource.payload_offset,
                resource.payload_bytes, resource_digest, 0) != 0 ||
            memcmp(resource_digest, resource.payload_sha256,
                   sizeof(resource_digest)) != 0) {
            result = ESP_ERR_INVALID_CRC;
        } else {
            host->available_capabilities |= P4_GAME_CAP_STORAGE;
            host->resource_data = resource_data + resource.payload_offset;
            host->resource_bytes = resource.payload_bytes;
            host->resource_format_version = resource.format_version;
        }
    }

    esp_elf_t elf;
    bool initialized = false;
    if (result == ESP_OK) {
        const int initialized_result = esp_elf_init(&elf);
        initialized = initialized_result == 0;
        result = initialized ? ESP_OK : ESP_FAIL;
    }
    if (result == ESP_OK && esp_elf_relocate(
            &elf, data + package.payload_offset) != 0) {
        result = ESP_ERR_INVALID_RESPONSE;
    }
    if (data != NULL) {
        platform_game_storage_release_file(data);
    }
    data = NULL;
    if (result == ESP_OK) {
        char *arguments[] = {(char *)(void *)host};
        if (esp_elf_request(&elf, 0, 1, arguments) != 0) {
            result = ESP_FAIL;
        }
    }
    if (initialized) {
        esp_elf_deinit(&elf);
    }
    host->available_capabilities &=
        (uint32_t)~(uint32_t)P4_GAME_CAP_STORAGE;
    host->resource_data = NULL;
    host->resource_bytes = 0U;
    host->resource_format_version = 0U;
    if (resource_data != NULL) {
        platform_game_storage_release_file(resource_data);
    }
    return result;
}
