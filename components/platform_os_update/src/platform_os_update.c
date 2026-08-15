// SPDX-License-Identifier: MIT

#include "platform/os_update.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "p4/os_update_package.h"
#include "platform/game_storage.h"

enum {
    UPDATE_PAYLOAD_BYTES = 20,
    UPDATE_SHA256 = 32,
};

typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    platform_os_update_info_t expected;
    mbedtls_sha256_context sha;
    size_t image_written;
    bool sha_started;
} update_stream_t;

static esp_err_t parse_update(const uint8_t *data, size_t size_bytes,
                              platform_os_update_info_t *out_info)
{
    if (data == NULL || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_info, 0, sizeof(*out_info));
    out_info->state = PLATFORM_OS_UPDATE_INVALID;
    p4_os_update_package_info_t package;
    if (p4_os_update_package_parse(data, size_bytes, &package) !=
        P4_OS_UPDATE_PACKAGE_VALID) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t digest[32];
    if (mbedtls_sha256(
            data + package.payload_offset, package.payload_bytes,
            digest, 0) != 0 ||
        memcmp(digest, package.payload_sha256, sizeof(digest)) != 0) {
        return ESP_ERR_INVALID_CRC;
    }
    const esp_partition_t *const target =
        esp_ota_get_next_update_partition(NULL);
    if (target == NULL || target->size < package.payload_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out_info->version, package.version, sizeof(out_info->version));
    memcpy(out_info->build, package.build, sizeof(out_info->build));
    out_info->image_bytes = package.payload_bytes;
    memcpy(out_info->image_sha256, digest, sizeof(digest));
    out_info->state = PLATFORM_OS_UPDATE_READY;
    out_info->last_error = ESP_OK;
    return ESP_OK;
}

esp_err_t platform_os_update_inspect(platform_os_update_info_t *out_info)
{
    if (out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_info, 0, sizeof(*out_info));
    uint8_t *data = NULL;
    size_t size_bytes = 0U;
    const esp_err_t loaded = platform_game_storage_load_update_file(
        PLATFORM_OS_UPDATE_FILE_NAME, PLATFORM_OS_UPDATE_MAX_PACKAGE_BYTES,
        &data, &size_bytes);
    if (loaded == ESP_ERR_NOT_FOUND) {
        out_info->state = PLATFORM_OS_UPDATE_ABSENT;
        out_info->last_error = loaded;
        return ESP_OK;
    }
    if (loaded != ESP_OK) {
        out_info->state = loaded == ESP_ERR_INVALID_STATE
            ? PLATFORM_OS_UPDATE_UNAVAILABLE : PLATFORM_OS_UPDATE_INVALID;
        out_info->last_error = loaded;
        return loaded;
    }
    const esp_err_t result = parse_update(data, size_bytes, out_info);
    platform_game_storage_release_file(data);
    if (result != ESP_OK) {
        out_info->state = PLATFORM_OS_UPDATE_INVALID;
        out_info->last_error = result;
    }
    return result;
}

static esp_err_t consume_update(void *opaque, const uint8_t *data,
                                size_t size_bytes, uint64_t file_offset)
{
    update_stream_t *const stream = opaque;
    if (stream == NULL || data == NULL || size_bytes == 0U ||
        file_offset > SIZE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t source = 0U;
    if (file_offset < PLATFORM_OS_UPDATE_HEADER_BYTES) {
        const size_t header_offset = (size_t)file_offset;
        size_t header_bytes = PLATFORM_OS_UPDATE_HEADER_BYTES - header_offset;
        if (header_bytes > size_bytes) {
            header_bytes = size_bytes;
        }
        if (header_offset == 0U && header_bytes >= UPDATE_SHA256 + 32U &&
            (memcmp(data, P4_OS_UPDATE_MAGIC, 8U) != 0 ||
             ((uint32_t)data[UPDATE_PAYLOAD_BYTES] |
              (uint32_t)data[UPDATE_PAYLOAD_BYTES + 1U] << 8U |
              (uint32_t)data[UPDATE_PAYLOAD_BYTES + 2U] << 16U |
              (uint32_t)data[UPDATE_PAYLOAD_BYTES + 3U] << 24U) !=
                stream->expected.image_bytes ||
             memcmp(data + UPDATE_SHA256,
                    stream->expected.image_sha256, 32U) != 0)) {
            return ESP_ERR_INVALID_STATE;
        }
        source = header_bytes;
    }
    if (source == size_bytes) {
        return ESP_OK;
    }
    const size_t payload_bytes = size_bytes - source;
    if (stream->image_written > stream->expected.image_bytes ||
        payload_bytes > stream->expected.image_bytes - stream->image_written ||
        mbedtls_sha256_update(&stream->sha, data + source, payload_bytes) != 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_err_t result = esp_ota_write(
        stream->handle, data + source, payload_bytes);
    if (result == ESP_OK) {
        stream->image_written += payload_bytes;
    }
    return result;
}

esp_err_t platform_os_update_install(
    const platform_os_update_info_t *expected_info)
{
    if (expected_info == NULL ||
        expected_info->state != PLATFORM_OS_UPDATE_READY ||
        expected_info->image_bytes == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_partition_t *const target =
        esp_ota_get_next_update_partition(NULL);
    if (target == NULL || target == esp_ota_get_running_partition() ||
        target->size < expected_info->image_bytes) {
        return ESP_ERR_INVALID_STATE;
    }
    update_stream_t stream = {
        .partition = target,
        .expected = *expected_info,
    };
    esp_err_t result = esp_ota_begin(
        target, expected_info->image_bytes, &stream.handle);
    if (result != ESP_OK) {
        return result;
    }
    mbedtls_sha256_init(&stream.sha);
    if (mbedtls_sha256_starts(&stream.sha, 0) != 0) {
        (void)esp_ota_abort(stream.handle);
        mbedtls_sha256_free(&stream.sha);
        return ESP_FAIL;
    }
    stream.sha_started = true;
    size_t streamed_bytes = 0U;
    result = platform_game_storage_stream_update_file_exclusive(
        PLATFORM_OS_UPDATE_FILE_NAME,
        PLATFORM_OS_UPDATE_MAX_PACKAGE_BYTES,
        consume_update, &stream, &streamed_bytes);
    uint8_t digest[32];
    if (result == ESP_OK &&
        (streamed_bytes != expected_info->image_bytes +
             PLATFORM_OS_UPDATE_HEADER_BYTES ||
         stream.image_written != expected_info->image_bytes ||
         mbedtls_sha256_finish(&stream.sha, digest) != 0 ||
         memcmp(digest, expected_info->image_sha256, sizeof(digest)) != 0)) {
        result = ESP_ERR_INVALID_CRC;
    }
    mbedtls_sha256_free(&stream.sha);
    stream.sha_started = false;
    if (result == ESP_OK) {
        result = esp_ota_end(stream.handle);
    } else {
        (void)esp_ota_abort(stream.handle);
    }
    if (result == ESP_OK) {
        result = esp_ota_set_boot_partition(target);
    }
    return result;
}

esp_err_t platform_os_update_mark_running_valid(bool *out_was_pending)
{
    if (out_was_pending == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_was_pending = false;
    const esp_partition_t *const running = esp_ota_get_running_partition();
    if (running == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_ota_img_states_t state;
    const esp_err_t state_result = esp_ota_get_state_partition(running, &state);
    if (state_result == ESP_ERR_NOT_SUPPORTED) {
        return ESP_OK;
    }
    if (state_result != ESP_OK) {
        return state_result;
    }
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_OK;
    }
    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    *out_was_pending = result == ESP_OK;
    return result;
}
