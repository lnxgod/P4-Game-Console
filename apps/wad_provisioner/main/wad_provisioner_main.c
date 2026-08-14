#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "platform/storage.h"

#define PROVISION_TEMP_PATH PLATFORM_STORAGE_MOUNT_POINT "/P4WAD.TMP"
#define PROVISION_WRITE_CHUNK_BYTES 16384U

static const char *const TAG = "p4_wad_provision";

extern const uint8_t p4_provision_wad_start[] asm("_binary_p4_provision_wad_start");
extern const uint8_t p4_provision_wad_end[] asm("_binary_p4_provision_wad_end");

static esp_err_t verify_embedded_wad(size_t *out_size)
{
    if (out_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const uintptr_t start = (uintptr_t)p4_provision_wad_start;
    const uintptr_t end = (uintptr_t)p4_provision_wad_end;
    if (end < start || end - start != PLATFORM_STORAGE_DOOM_SHAREWARE_1_9_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t digest[32];
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    int crypto_result = mbedtls_sha256_starts(&context, 0);
    size_t hashed = 0U;
    while (crypto_result == 0 && hashed < (size_t)(end - start)) {
        size_t count = (size_t)(end - start) - hashed;
        if (count > PROVISION_WRITE_CHUNK_BYTES) {
            count = PROVISION_WRITE_CHUNK_BYTES;
        }
        crypto_result = mbedtls_sha256_update(
            &context,
            &p4_provision_wad_start[hashed],
            count
        );
        hashed += count;
        vTaskDelay(1U);
    }
    if (crypto_result == 0) {
        crypto_result = mbedtls_sha256_finish(&context, digest);
    }
    mbedtls_sha256_free(&context);
    if (crypto_result != 0) {
        return ESP_FAIL;
    }
    char digest_hex[PLATFORM_STORAGE_SHA256_HEX_LENGTH + 1U];
    static const char hex[] = "0123456789abcdef";
    for (size_t index = 0U; index < sizeof(digest); ++index) {
        digest_hex[index * 2U] = hex[digest[index] >> 4U];
        digest_hex[index * 2U + 1U] = hex[digest[index] & 0x0fU];
    }
    digest_hex[PLATFORM_STORAGE_SHA256_HEX_LENGTH] = '\0';
    if (strcmp(digest_hex, PLATFORM_STORAGE_DOOM_SHAREWARE_1_9_SHA256) != 0) {
        return ESP_ERR_INVALID_CRC;
    }
    *out_size = (size_t)(end - start);
    return ESP_OK;
}

static esp_err_t path_absent(const char *path, bool *out_absent)
{
    if (path == NULL || out_absent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    struct stat metadata;
    if (stat(path, &metadata) == 0) {
        *out_absent = false;
        return ESP_OK;
    }
    if (errno == ENOENT) {
        *out_absent = true;
        return ESP_OK;
    }
    if (errno == EINVAL) {
        const char *expected_name = NULL;
        if (strcmp(path, PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH) == 0) {
            expected_name = "DOOM1.WAD";
        } else if (strcmp(path, PROVISION_TEMP_PATH) == 0) {
            expected_name = "P4WAD.TMP";
        } else {
            return ESP_FAIL;
        }

        DIR *root = opendir(PLATFORM_STORAGE_MOUNT_POINT);
        if (root == NULL) {
            return ESP_FAIL;
        }
        bool found = false;
        errno = 0;
        const struct dirent *entry = NULL;
        while ((entry = readdir(root)) != NULL) {
            if (strcasecmp(entry->d_name, expected_name) == 0) {
                found = true;
                break;
            }
        }
        const int enumeration_errno = errno;
        const int close_result = closedir(root);
        if (enumeration_errno != 0 || close_result != 0) {
            return ESP_FAIL;
        }
        *out_absent = !found;
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t write_new_temp_file(size_t wad_size)
{
    const int descriptor = open(PROVISION_TEMP_PATH, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) {
        return errno == EEXIST ? ESP_ERR_INVALID_STATE : ESP_FAIL;
    }

    size_t written = 0U;
    esp_err_t result = ESP_OK;
    while (written < wad_size) {
        size_t chunk_size = wad_size - written;
        if (chunk_size > PROVISION_WRITE_CHUNK_BYTES) {
            chunk_size = PROVISION_WRITE_CHUNK_BYTES;
        }
        const ssize_t count = write(descriptor, &p4_provision_wad_start[written], chunk_size);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            result = ESP_FAIL;
            break;
        }
        written += (size_t)count;
        vTaskDelay(1U);
    }
    if (result == ESP_OK && fsync(descriptor) != 0) {
        result = ESP_FAIL;
    }
    if (close(descriptor) != 0 && result == ESP_OK) {
        result = ESP_FAIL;
    }
    return result;
}

static void log_wad_pass(const char *action, const platform_storage_wad_info_t *wad)
{
    ESP_LOGI(
        TAG,
        "P4_WAD_PROVISION D1 PASS action=%s path=%s bytes=%" PRIu64 " sha256=%s",
        action,
        wad->path,
        wad->size_bytes,
        wad->sha256
    );
}

void app_main(void)
{
    ESP_LOGW(
        TAG,
        "P4_WAD_PROVISION D1 START local-development-only non-redistributable=true"
    );

    size_t embedded_size = 0U;
    esp_err_t result = verify_embedded_wad(&embedded_size);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "P4_WAD_PROVISION D1 FAIL stage=embedded-gate error=%s", esp_err_to_name(result));
        return;
    }

    result = platform_storage_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "P4_WAD_PROVISION D1 FAIL stage=mount error=%s", esp_err_to_name(result));
        return;
    }

    bool target_absent = false;
    result = path_absent(PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH, &target_absent);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "P4_WAD_PROVISION D1 FAIL stage=target-stat error=%s", esp_err_to_name(result));
        (void)platform_storage_deinit();
        return;
    }
    if (!target_absent) {
        platform_storage_wad_info_t existing;
        result = platform_storage_inspect_wad(PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH, &existing);
        if (result == ESP_OK) {
            result = platform_storage_deinit();
            if (result == ESP_OK) {
                log_wad_pass("already-present-no-write", &existing);
            } else {
                ESP_LOGE(
                    TAG,
                    "P4_WAD_PROVISION D1 FAIL stage=unmount error=%s",
                    esp_err_to_name(result)
                );
            }
        } else {
            ESP_LOGE(
                TAG,
                "P4_WAD_PROVISION D1 REFUSE reason=existing-target-invalid path=%s error=%s",
                PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH,
                esp_err_to_name(result)
            );
            (void)platform_storage_deinit();
        }
        return;
    }

    bool temp_absent = false;
    result = path_absent(PROVISION_TEMP_PATH, &temp_absent);
    if (result != ESP_OK || !temp_absent) {
        ESP_LOGE(
            TAG,
            "P4_WAD_PROVISION D1 REFUSE reason=temp-path-not-clean path=%s error=%s",
            PROVISION_TEMP_PATH,
            esp_err_to_name(result)
        );
        (void)platform_storage_deinit();
        return;
    }

    result = write_new_temp_file(embedded_size);
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4_WAD_PROVISION D1 FAIL stage=temp-write error=%s retained=%s",
            esp_err_to_name(result),
            PROVISION_TEMP_PATH
        );
        (void)platform_storage_deinit();
        return;
    }

    platform_storage_wad_info_t temp_info;
    result = platform_storage_inspect_wad(PROVISION_TEMP_PATH, &temp_info);
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4_WAD_PROVISION D1 FAIL stage=temp-readback error=%s retained=%s",
            esp_err_to_name(result),
            PROVISION_TEMP_PATH
        );
        (void)platform_storage_deinit();
        return;
    }

    result = path_absent(PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH, &target_absent);
    if (result != ESP_OK || !target_absent) {
        ESP_LOGE(
            TAG,
            "P4_WAD_PROVISION D1 REFUSE reason=target-appeared-before-rename retained=%s",
            PROVISION_TEMP_PATH
        );
        (void)platform_storage_deinit();
        return;
    }
    if (rename(PROVISION_TEMP_PATH, PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH) != 0) {
        ESP_LOGE(
            TAG,
            "P4_WAD_PROVISION D1 FAIL stage=rename errno=%d retained=%s",
            errno,
            PROVISION_TEMP_PATH
        );
        (void)platform_storage_deinit();
        return;
    }

    platform_storage_wad_info_t final_info;
    result = platform_storage_inspect_wad(PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH, &final_info);
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4_WAD_PROVISION D1 FAIL stage=final-readback error=%s",
            esp_err_to_name(result)
        );
        (void)platform_storage_deinit();
        return;
    }
    const esp_err_t unmount_result = platform_storage_deinit();
    if (unmount_result != ESP_OK) {
        ESP_LOGE(TAG, "P4_WAD_PROVISION D1 FAIL stage=unmount error=%s", esp_err_to_name(unmount_result));
        return;
    }
    log_wad_pass("created-fsync-readback-renamed", &final_info);
}
