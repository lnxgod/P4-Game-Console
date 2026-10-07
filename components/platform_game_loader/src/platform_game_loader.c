// SPDX-License-Identifier: MIT

#include "platform/game_loader.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_elf.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include "p4/game_package.h"
#include "p4/game_resource.h"
#include "platform/game_storage.h"

static const char *TAG = "game_loader";

/*
 * Keep the cartridge import allow-list and the on-device resolver in lockstep.
 * Espressif ELF Loader 1.3.1 exposes most string primitives but omits memcmp
 * from its built-in libc table. Register the missing bounded primitive only
 * while a cartridge is being relocated/executed.
 */
static const struct esp_elfsym s_cartridge_runtime_symbols[] = {
    ESP_ELFSYM_EXPORT(memcmp),
    ESP_ELFSYM_END,
};

static bool host_field_present(const p4_cartridge_host_v1_t *host,
                               size_t offset, size_t bytes)
{
    return host != NULL && offset <= SIZE_MAX - bytes &&
        (size_t)host->struct_bytes >= offset + bytes;
}

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

static bool multiplayer_profiles_equal(
    const p4_game_multiplayer_profile_t *left,
    const p4_game_multiplayer_profile_t *right)
{
    return left != NULL && right != NULL &&
        left->schema == right->schema && left->style == right->style &&
        left->min_players == right->min_players &&
        left->max_players == right->max_players &&
        left->tick_rate_hz == right->tick_rate_hz &&
        left->input_delay_ticks == right->input_delay_ticks &&
        left->message_bytes == right->message_bytes &&
        left->protocol == right->protocol && left->flags == right->flags;
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
        package->multiplayer_profile_declared ==
            entry->package.multiplayer_profile_declared &&
        multiplayer_profiles_equal(
            &package->multiplayer_profile,
            &entry->package.multiplayer_profile) &&
        strcmp(package->id, entry->package.id) == 0 &&
        memcmp(package->payload_sha256, entry->package.payload_sha256,
               sizeof(package->payload_sha256)) == 0;
}

esp_err_t platform_game_loader_run(
    const platform_game_catalog_entry_t *entry,
    p4_cartridge_host_v1_t *host)
{
    return platform_game_loader_run_with_progress(entry, host, NULL, NULL);
}

esp_err_t platform_game_loader_run_with_progress(
    const platform_game_catalog_entry_t *entry,
    p4_cartridge_host_v1_t *host,
    platform_game_loader_progress_fn_t progress, void *context)
{
    if (entry == NULL || host == NULL || !entry->valid ||
        host->magic != P4_CARTRIDGE_HOST_MAGIC ||
        host->api_version != P4_CARTRIDGE_HOST_API_VERSION ||
        !host_field_present(
            host, offsetof(p4_cartridge_host_v1_t, finished),
            sizeof(host->finished))) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool has_resource_fields = host_field_present(
        host, offsetof(p4_cartridge_host_v1_t, resource_format_version),
        sizeof(host->resource_format_version));
    host->available_capabilities &=
        (uint32_t)~(uint32_t)P4_GAME_CAP_STORAGE;
    if (has_resource_fields) {
        host->resource_data = NULL;
        host->resource_bytes = 0U;
        host->resource_format_version = 0U;
    }
    uint8_t *data = NULL;
    size_t size_bytes = 0U;
    const char *failure_stage = NULL;
    int cartridge_exit = 0;
    ESP_LOGI(TAG,
             "P4_CARTRIDGE_LOAD_BEGIN app=%s file=%s source=%s",
             entry->package.id, entry->file_name,
             entry->in_games_directory ? "games-directory" : "root");
    esp_err_t result = entry->in_games_directory
        ? platform_game_storage_load_game_file_with_progress(
            entry->file_name, P4_GAME_PACKAGE_MAX_BYTES,
            &data, &size_bytes, progress, context)
        : platform_game_storage_load_root_file_with_progress(
            entry->file_name, P4_GAME_PACKAGE_MAX_BYTES,
            &data, &size_bytes, progress, context);
    if (result != ESP_OK) {
        failure_stage = "package-read";
    } else {
        ESP_LOGI(TAG,
                 "P4_CARTRIDGE_PACKAGE_READ app=%s bytes=%u",
                 entry->package.id, (unsigned)size_bytes);
    }
    p4_game_package_info_t package = {0};
    if (result == ESP_OK &&
        p4_game_package_parse(data, size_bytes, &package) !=
            P4_GAME_PACKAGE_VALID) {
        result = ESP_ERR_INVALID_RESPONSE;
        failure_stage = "package-parse";
    }
    uint8_t digest[P4_GAME_PACKAGE_SHA256_BYTES];
    if (result == ESP_OK &&
        (mbedtls_sha256(data + package.payload_offset,
                        package.payload_bytes, digest, 0) != 0 ||
         memcmp(digest, package.payload_sha256, sizeof(digest)) != 0)) {
        result = ESP_ERR_INVALID_CRC;
        failure_stage = "package-hash";
    }
    if (result == ESP_OK && !package_matches_catalog(&package, entry)) {
        result = ESP_ERR_INVALID_STATE;
        failure_stage = "catalog-match";
    }
    if (result == ESP_OK) {
        ESP_LOGI(TAG,
                 "P4_CARTRIDGE_PACKAGE_VERIFIED app=%s payload_bytes=%u",
                 entry->package.id, (unsigned)package.payload_bytes);
    }

    if (result == ESP_OK && progress != NULL) progress(context);

    uint8_t *resource_data = NULL;
    size_t resource_size_bytes = 0U;
    p4_game_resource_info_t resource;
    const uint32_t package_capabilities = package.required_capabilities |
        package.optional_capabilities;
    if (result == ESP_OK && has_resource_fields &&
        (package_capabilities & P4_GAME_CAP_STORAGE) != 0U) {
        char resource_name[PLATFORM_GAME_STORAGE_FILE_NAME_MAX_BYTES];
        if (!resource_file_name(entry->file_name, resource_name)) {
            result = ESP_ERR_INVALID_ARG;
            failure_stage = "resource-name";
        } else {
            const esp_err_t loaded = entry->in_games_directory
                ? platform_game_storage_load_game_file_with_progress(
                    resource_name, P4_GAME_RESOURCE_MAX_BYTES,
                    &resource_data, &resource_size_bytes, progress, context)
                : platform_game_storage_load_root_file_with_progress(
                    resource_name, P4_GAME_RESOURCE_MAX_BYTES,
                    &resource_data, &resource_size_bytes, progress, context);
            if (loaded != ESP_OK && loaded != ESP_ERR_NOT_FOUND) {
                result = loaded;
                failure_stage = "resource-read";
            } else if (loaded == ESP_OK) {
                ESP_LOGI(TAG,
                         "P4_CARTRIDGE_RESOURCE_READ app=%s file=%s "
                         "bytes=%u",
                         entry->package.id, resource_name,
                         (unsigned)resource_size_bytes);
            } else {
                ESP_LOGI(TAG,
                         "P4_CARTRIDGE_RESOURCE_ABSENT app=%s file=%s "
                         "fallback=cartridge",
                         entry->package.id, resource_name);
            }
        }
    }
    if (result == ESP_OK && has_resource_fields && resource_data != NULL) {
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
            failure_stage = "resource-verify";
        } else {
            host->available_capabilities |= P4_GAME_CAP_STORAGE;
            host->resource_data = resource_data + resource.payload_offset;
            host->resource_bytes = resource.payload_bytes;
            host->resource_format_version = resource.format_version;
            ESP_LOGI(TAG,
                     "P4_CARTRIDGE_RESOURCE_VERIFIED app=%s "
                     "format=%lu payload_bytes=%u",
                     entry->package.id,
                     (unsigned long)resource.format_version,
                     (unsigned)resource.payload_bytes);
        }
    }

    if (result == ESP_OK && progress != NULL) progress(context);

    esp_elf_t elf;
    bool initialized = false;
    bool runtime_symbols_registered = false;
    if (result == ESP_OK) {
        const int registered =
            esp_elf_register_symbol(s_cartridge_runtime_symbols);
        runtime_symbols_registered = registered == 0;
        if (registered != 0 && registered != -EEXIST) {
            result = ESP_FAIL;
            failure_stage = "symbol-register";
        }
    }
    if (result == ESP_OK) {
        const int initialized_result = esp_elf_init(&elf);
        initialized = initialized_result == 0;
        result = initialized ? ESP_OK : ESP_FAIL;
        if (!initialized) {
            failure_stage = "elf-init";
        }
    }
    if (result == ESP_OK && esp_elf_relocate(
            &elf, data + package.payload_offset) != 0) {
        result = ESP_ERR_INVALID_RESPONSE;
        failure_stage = "elf-relocate";
    } else if (result == ESP_OK) {
        ESP_LOGI(TAG,
                 "P4_CARTRIDGE_ELF_READY app=%s runtime=psram-relocated",
                 entry->package.id);
    }
    if (data != NULL) {
        platform_game_storage_release_file(data);
    }
    data = NULL;
    if (result == ESP_OK) {
        if (progress != NULL) progress(context);
        char *arguments[] = {(char *)(void *)host};
        ESP_LOGI(TAG, "P4_CARTRIDGE_ENTRY_BEGIN app=%s",
                 entry->package.id);
        /* esp_elf_request() in the pinned loader always returns zero and
         * discards the entry point's result. Call its public entry pointer so
         * ABI, capability, allocation, and render failures remain observable.
         */
        cartridge_exit = elf.entry(1, arguments);
        if (cartridge_exit != 0) {
            result = ESP_FAIL;
            failure_stage = "cartridge-entry";
        }
    }
    if (initialized) {
        esp_elf_deinit(&elf);
    }
    if (runtime_symbols_registered) {
        (void)esp_elf_unregister_symbol(s_cartridge_runtime_symbols);
    }
    host->available_capabilities &=
        (uint32_t)~(uint32_t)P4_GAME_CAP_STORAGE;
    if (has_resource_fields) {
        host->resource_data = NULL;
        host->resource_bytes = 0U;
        host->resource_format_version = 0U;
    }
    if (resource_data != NULL) {
        platform_game_storage_release_file(resource_data);
    }
    if (result == ESP_OK) {
        ESP_LOGI(TAG,
                 "P4_CARTRIDGE_LOAD_DONE app=%s cartridge_exit=%d",
                 entry->package.id, cartridge_exit);
    } else {
        ESP_LOGE(TAG,
                 "P4_CARTRIDGE_LOAD_FAIL app=%s stage=%s error=%s "
                 "cartridge_exit=%d",
                 entry->package.id,
                 failure_stage == NULL ? "unknown" : failure_stage,
                 esp_err_to_name(result), cartridge_exit);
    }
    return result;
}
