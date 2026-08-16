// SPDX-License-Identifier: MIT

#include "platform/game_loader.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_elf.h"
#include "mbedtls/sha256.h"
#include "p4/game_package.h"
#include "platform/game_storage.h"

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
    uint8_t *data = NULL;
    size_t size_bytes = 0U;
    esp_err_t result = platform_game_storage_load_root_file(
        entry->file_name, P4_GAME_PACKAGE_MAX_BYTES, &data, &size_bytes);
    p4_game_package_info_t package;
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
    platform_game_storage_release_file(data);
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
    return result;
}
