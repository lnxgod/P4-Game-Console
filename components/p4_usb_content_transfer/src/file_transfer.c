// SPDX-License-Identifier: MIT

#include "p4/file_transfer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "mbedtls/sha256.h"
#include "p4/content_catalog.h"
#include "p4/game_package.h"
#include "p4/game_resource.h"

enum {
    REQUEST_BYTES = 88,
    REQUEST_CRC_OFFSET = 84,
    READY_BYTES = 52,
    CHUNK_HEADER_BYTES = 16,
    ACK_BYTES = 9,
    DONE_BYTES = 41,
    WIRE_CHUNK_BYTES = CHUNK_HEADER_BYTES + P4_FILE_TRANSFER_CHUNK_BYTES,
    TRANSFER_TIMEOUT_US = 15000000,
    TERMINAL_HOLD_US = 1000000,
    DOWNLOAD_RETRY_LIMIT = 3,
};

typedef enum {
    PARSER_REQUEST_MAGIC = 0,
    PARSER_REQUEST_BODY,
    PARSER_UPLOAD_CHUNK_MAGIC,
    PARSER_UPLOAD_CHUNK_HEADER,
    PARSER_UPLOAD_CHUNK_DATA,
    PARSER_DOWNLOAD_ACK_MAGIC,
    PARSER_DOWNLOAD_ACK_BODY,
    PARSER_TERMINAL,
} parser_state_t;

typedef struct {
    char storage_root[P4_CONTENT_PATH_BYTES];
    char target_path[P4_CONTENT_PATH_BYTES];
    char temp_path[P4_CONTENT_PATH_BYTES];
    char backup_path[P4_CONTENT_PATH_BYTES];
    char file_name[P4_FILE_TRANSFER_NAME_BYTES];
    p4_content_transfer_transport_t transport;
    uint8_t request[REQUEST_BYTES];
    uint8_t chunk_header[CHUNK_HEADER_BYTES];
    uint8_t ack[ACK_BYTES];
    uint8_t *wire_chunk;
    mbedtls_sha256_context sha256;
    parser_state_t parser;
    p4_file_transfer_state_t public_state;
    p4_file_transfer_direction_t direction;
    p4_file_transfer_class_t file_class;
    size_t parser_used;
    size_t chunk_used;
    uint32_t chunk_length;
    uint32_t expected_sequence;
    uint32_t expected_bytes;
    uint32_t transferred_bytes;
    uint32_t generation;
    unsigned download_retries;
    int descriptor;
    int64_t last_activity_us;
    int64_t terminal_until_us;
    uint8_t expected_digest[32];
    uint8_t last_status;
    uint8_t magic_used;
    bool initialized;
    bool available;
    bool sha_started;
    bool replace_requested;
    bool waiting_ack;
    bool high_speed;
} file_transfer_service_t;

static const char *const TAG = "p4_file_transfer";
static const uint8_t REQUEST_MAGIC[4] = {'P', '4', 'F', '1'};
static const uint8_t CHUNK_MAGIC[4] = {'P', '4', 'C', '2'};
static const uint8_t ACK_MAGIC[4] = {'P', '4', 'A', '2'};
static file_transfer_service_t s_transfer = {.descriptor = -1};

static uint16_t read_u16_le(const uint8_t bytes[2])
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static void write_u16_le(uint8_t bytes[2], uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t crc32_bytes(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static bool all_zero(const uint8_t *bytes, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        if (bytes[index] != 0U) {
            return false;
        }
    }
    return true;
}

static bool transport_send(const uint8_t *bytes, size_t length)
{
    return s_transfer.transport.send != NULL &&
        s_transfer.transport.send(
            s_transfer.transport.context, bytes, length) == ESP_OK;
}

static void send_ready(p4_file_transfer_status_t status,
                       uint32_t size, const uint8_t digest[32])
{
    uint8_t response[READY_BYTES] = {
        'P', '4', 'R', '2', (uint8_t)status,
        (uint8_t)s_transfer.direction, (uint8_t)s_transfer.file_class, 0U,
    };
    write_u32_le(&response[8], P4_CONTENT_TRANSFER_BAUD);
    write_u16_le(&response[12], P4_FILE_TRANSFER_CHUNK_BYTES);
    write_u16_le(&response[14], 0U);
    write_u32_le(&response[16], size);
    if (digest != NULL) {
        memcpy(&response[20], digest, 32U);
    }
    (void)transport_send(response, sizeof(response));
}

static void send_ack(uint32_t sequence, p4_file_transfer_status_t status)
{
    uint8_t response[ACK_BYTES] = {'P', '4', 'A', '2'};
    write_u32_le(&response[4], sequence);
    response[8] = (uint8_t)status;
    (void)transport_send(response, sizeof(response));
}

static void send_done(p4_file_transfer_status_t status,
                      const uint8_t digest[32])
{
    uint8_t response[DONE_BYTES] = {
        'P', '4', 'D', '2', (uint8_t)status,
    };
    write_u32_le(&response[5], s_transfer.transferred_bytes);
    if (digest != NULL) {
        memcpy(&response[9], digest, 32U);
    }
    (void)transport_send(response, sizeof(response));
}

static void close_descriptor(void)
{
    if (s_transfer.descriptor >= 0) {
        (void)close(s_transfer.descriptor);
        s_transfer.descriptor = -1;
    }
}

static void free_sha256(void)
{
    if (s_transfer.sha_started) {
        mbedtls_sha256_free(&s_transfer.sha256);
        s_transfer.sha_started = false;
    }
}

static void reset_request_parser(void)
{
    s_transfer.parser = PARSER_REQUEST_MAGIC;
    s_transfer.parser_used = 0U;
    s_transfer.magic_used = 0U;
}

static void restore_idle_baud(void)
{
    if (!s_transfer.high_speed) {
        return;
    }
    (void)s_transfer.transport.wait_tx(
        s_transfer.transport.context, 1000U);
    (void)s_transfer.transport.set_baud(
        s_transfer.transport.context, s_transfer.transport.idle_baud);
    s_transfer.high_speed = false;
}

static void finish_terminal(p4_file_transfer_status_t status,
                            const uint8_t digest[32])
{
    close_descriptor();
    free_sha256();
    if (status != P4_FILE_TRANSFER_STATUS_OK &&
        s_transfer.temp_path[0] != '\0') {
        (void)unlink(s_transfer.temp_path);
    }
    s_transfer.last_status = (uint8_t)status;
    send_done(status, digest);
    restore_idle_baud();
    s_transfer.public_state = status == P4_FILE_TRANSFER_STATUS_OK
        ? P4_FILE_TRANSFER_COMPLETE : P4_FILE_TRANSFER_FAILED;
    if (status == P4_FILE_TRANSFER_STATUS_OK &&
        s_transfer.generation != UINT32_MAX) {
        ++s_transfer.generation;
    }
    s_transfer.parser = PARSER_TERMINAL;
    s_transfer.terminal_until_us =
        esp_timer_get_time() + TERMINAL_HOLD_US;
}

static bool append_path(char *output, size_t output_size,
                        const char *left, const char *right)
{
    const int count = snprintf(output, output_size, "%s%s", left, right);
    return count >= 0 && (size_t)count < output_size;
}

static p4_file_transfer_status_t ensure_directory(const char *path)
{
    if (mkdir(path, 0755) == 0) {
        return P4_FILE_TRANSFER_STATUS_OK;
    }
    if (errno != EEXIST) {
        return P4_FILE_TRANSFER_STATUS_IO;
    }
    struct stat metadata;
    if (stat(path, &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        return P4_FILE_TRANSFER_STATUS_STORAGE;
    }
    return P4_FILE_TRANSFER_STATUS_OK;
}

static bool request_name(char output[P4_FILE_TRANSFER_NAME_BYTES])
{
    const uint8_t *const source = &s_transfer.request[44];
    const uint8_t *const terminator = memchr(
        source, 0, P4_FILE_TRANSFER_NAME_BYTES);
    if (terminator == NULL || terminator == source) {
        return false;
    }
    const size_t length = (size_t)(terminator - source);
    if (!all_zero(terminator + 1U,
                  P4_FILE_TRANSFER_NAME_BYTES - length - 1U)) {
        return false;
    }
    memcpy(output, source, P4_FILE_TRANSFER_NAME_BYTES);
    return true;
}

static bool game_name_valid(const char *name, const char *extension)
{
    const size_t length = strlen(name);
    const size_t suffix_length = strlen(extension);
    if (length <= suffix_length || length >= P4_FILE_TRANSFER_NAME_BYTES ||
        strcmp(name + length - suffix_length, extension) != 0) {
        return false;
    }
    const size_t base_length = length - suffix_length;
    if (base_length > 32U ||
        !((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= '0' && name[0] <= '9'))) {
        return false;
    }
    for (size_t index = 0U; index < base_length; ++index) {
        const char byte = name[index];
        if (!((byte >= 'A' && byte <= 'Z') ||
              (byte >= '0' && byte <= '9') ||
              byte == '_' || byte == '-')) {
            return false;
        }
    }
    return true;
}

static bool exchange_name_valid(const char *name)
{
    const size_t length = strlen(name);
    if (length == 0U || length >= P4_FILE_TRANSFER_NAME_BYTES ||
        !((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= '0' && name[0] <= '9')) ||
        strncmp(name, "P4FT.", 5U) == 0 || strstr(name, "..") != NULL) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const char byte = name[index];
        if (!((byte >= 'A' && byte <= 'Z') ||
              (byte >= 'a' && byte <= 'z') ||
              (byte >= '0' && byte <= '9') ||
              byte == '_' || byte == '-' || byte == '.')) {
            return false;
        }
    }
    return true;
}

static p4_file_transfer_status_t prepare_paths(void)
{
    const char *const suffix =
        s_transfer.file_class == P4_FILE_TRANSFER_CLASS_EXCHANGE ? "/TRANSFER" :
        s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4CART ? "/P4/GAMES" : "/GAMES";
    if (s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4CART) {
        char parent[P4_CONTENT_PATH_BYTES];
        if (!append_path(parent, sizeof(parent), s_transfer.storage_root, "/P4")) {
            return P4_FILE_TRANSFER_STATUS_STORAGE;
        }
        const p4_file_transfer_status_t parent_status = ensure_directory(parent);
        if (parent_status != P4_FILE_TRANSFER_STATUS_OK) return parent_status;
    }
    char directory[P4_CONTENT_PATH_BYTES];
    if (!append_path(directory, sizeof(directory),
                     s_transfer.storage_root, suffix)) {
        return P4_FILE_TRANSFER_STATUS_STORAGE;
    }
    p4_file_transfer_status_t status = ensure_directory(directory);
    if (status != P4_FILE_TRANSFER_STATUS_OK) {
        return status;
    }
    const int target_count = snprintf(
        s_transfer.target_path, sizeof(s_transfer.target_path),
        "%s/%s", directory, s_transfer.file_name);
    if (target_count < 0 ||
        (size_t)target_count >= sizeof(s_transfer.target_path)) {
        return P4_FILE_TRANSFER_STATUS_STORAGE;
    }
    const int temp_count = snprintf(
        s_transfer.temp_path, sizeof(s_transfer.temp_path),
        "%s.P4T", s_transfer.target_path);
    const int backup_count = snprintf(
        s_transfer.backup_path, sizeof(s_transfer.backup_path),
        "%s.P4B", s_transfer.target_path);
    if (temp_count < 0 ||
        (size_t)temp_count >= sizeof(s_transfer.temp_path) ||
        backup_count < 0 ||
        (size_t)backup_count >= sizeof(s_transfer.backup_path)) {
        return P4_FILE_TRANSFER_STATUS_STORAGE;
    }
    return P4_FILE_TRANSFER_STATUS_OK;
}

static p4_file_transfer_status_t regular_file_exists(
    const char *path, bool *exists_out)
{
    if (exists_out == NULL) {
        return P4_FILE_TRANSFER_STATUS_STORAGE;
    }
    *exists_out = false;
    struct stat metadata;
    if (stat(path, &metadata) == 0) {
        if (!S_ISREG(metadata.st_mode)) {
            return P4_FILE_TRANSFER_STATUS_STORAGE;
        }
        *exists_out = true;
        return P4_FILE_TRANSFER_STATUS_OK;
    }
    return errno == ENOENT
        ? P4_FILE_TRANSFER_STATUS_OK : P4_FILE_TRANSFER_STATUS_IO;
}

static p4_file_transfer_status_t recover_paths(void)
{
    bool target_exists = false;
    bool backup_exists = false;
    p4_file_transfer_status_t status = regular_file_exists(
        s_transfer.target_path, &target_exists);
    if (status == P4_FILE_TRANSFER_STATUS_OK) {
        status = regular_file_exists(
            s_transfer.backup_path, &backup_exists);
    }
    if (status != P4_FILE_TRANSFER_STATUS_OK) {
        return status;
    }
    if (backup_exists && !target_exists) {
        if (rename(s_transfer.backup_path,
                   s_transfer.target_path) != 0) {
            return P4_FILE_TRANSFER_STATUS_IO;
        }
    } else if (backup_exists &&
               unlink(s_transfer.backup_path) != 0) {
        return P4_FILE_TRANSFER_STATUS_IO;
    }
    if (unlink(s_transfer.temp_path) != 0 && errno != ENOENT) {
        return P4_FILE_TRANSFER_STATUS_IO;
    }
    return P4_FILE_TRANSFER_STATUS_OK;
}

static p4_file_transfer_status_t hash_file(
    const char *path, uint32_t maximum_bytes,
    uint32_t *size_out, uint8_t digest_out[32])
{
    struct stat metadata;
    if (stat(path, &metadata) != 0) {
        return errno == ENOENT
            ? P4_FILE_TRANSFER_STATUS_NOT_FOUND
            : P4_FILE_TRANSFER_STATUS_IO;
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_size <= 0) {
        return P4_FILE_TRANSFER_STATUS_STORAGE;
    }
    if ((uint64_t)metadata.st_size > maximum_bytes ||
        (uint64_t)metadata.st_size > UINT32_MAX) {
        return P4_FILE_TRANSFER_STATUS_TOO_LARGE;
    }
    const int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        return P4_FILE_TRANSFER_STATUS_IO;
    }
    mbedtls_sha256_context sha256;
    mbedtls_sha256_init(&sha256);
    p4_file_transfer_status_t status =
        mbedtls_sha256_starts(&sha256, 0) == 0
            ? P4_FILE_TRANSFER_STATUS_OK
            : P4_FILE_TRANSFER_STATUS_HASH;
    uint32_t total = 0U;
    while (status == P4_FILE_TRANSFER_STATUS_OK &&
           total < (uint32_t)metadata.st_size) {
        const uint32_t remaining =
            (uint32_t)metadata.st_size - total;
        const size_t requested = remaining < P4_FILE_TRANSFER_CHUNK_BYTES
            ? (size_t)remaining : (size_t)P4_FILE_TRANSFER_CHUNK_BYTES;
        const ssize_t count = read(
            descriptor, s_transfer.wire_chunk + CHUNK_HEADER_BYTES,
            requested);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0 || (size_t)count > requested ||
            mbedtls_sha256_update(
                &sha256,
                s_transfer.wire_chunk + CHUNK_HEADER_BYTES,
                (size_t)count) != 0) {
            status = P4_FILE_TRANSFER_STATUS_IO;
            break;
        }
        total += (uint32_t)count;
        (void)esp_task_wdt_reset();
    }
    if (status == P4_FILE_TRANSFER_STATUS_OK &&
        mbedtls_sha256_finish(&sha256, digest_out) != 0) {
        status = P4_FILE_TRANSFER_STATUS_HASH;
    }
    mbedtls_sha256_free(&sha256);
    if (close(descriptor) != 0 &&
        status == P4_FILE_TRANSFER_STATUS_OK) {
        status = P4_FILE_TRANSFER_STATUS_IO;
    }
    if (status == P4_FILE_TRANSFER_STATUS_OK &&
        total != (uint32_t)metadata.st_size) {
        status = P4_FILE_TRANSFER_STATUS_IO;
    }
    if (status == P4_FILE_TRANSFER_STATUS_OK && size_out != NULL) {
        *size_out = total;
    }
    return status;
}

static p4_file_transfer_status_t digest_memory(
    const uint8_t *bytes, size_t length, uint8_t digest[32])
{
    mbedtls_sha256_context sha256;
    mbedtls_sha256_init(&sha256);
    p4_file_transfer_status_t status =
        mbedtls_sha256_starts(&sha256, 0) == 0 &&
        mbedtls_sha256_update(&sha256, bytes, length) == 0 &&
        mbedtls_sha256_finish(&sha256, digest) == 0
            ? P4_FILE_TRANSFER_STATUS_OK
            : P4_FILE_TRANSFER_STATUS_HASH;
    mbedtls_sha256_free(&sha256);
    return status;
}

static p4_file_transfer_status_t validate_native(
    const char *path, uint32_t size)
{
    uint8_t *package = heap_caps_malloc(
        size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (package == NULL) {
        package = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    if (package == NULL) {
        return P4_FILE_TRANSFER_STATUS_IO;
    }
    const int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        heap_caps_free(package);
        return P4_FILE_TRANSFER_STATUS_IO;
    }
    size_t offset = 0U;
    while (offset < size) {
        const ssize_t count = read(
            descriptor, package + offset, (size_t)size - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        offset += (size_t)count;
        (void)esp_task_wdt_reset();
    }
    const bool close_ok = close(descriptor) == 0;
    p4_file_transfer_status_t status = P4_FILE_TRANSFER_STATUS_BAD_PACKAGE;
    uint32_t payload_offset = 0U, payload_bytes = 0U;
    uint8_t expected_digest[32];
    if (offset == size && close_ok) {
        if (s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4R) {
            p4_game_resource_info_t info;
            if (p4_game_resource_parse(package, size, &info) == P4_GAME_RESOURCE_VALID) {
                payload_offset = info.payload_offset;
                payload_bytes = info.payload_bytes;
                memcpy(expected_digest, info.payload_sha256, 32U);
                status = P4_FILE_TRANSFER_STATUS_OK;
            }
        } else {
            p4_game_package_info_t info;
            if (p4_game_package_parse(package, size, &info) == P4_GAME_PACKAGE_VALID) {
                payload_offset = info.payload_offset;
                payload_bytes = info.payload_bytes;
                memcpy(expected_digest, info.payload_sha256, 32U);
                status = P4_FILE_TRANSFER_STATUS_OK;
            }
        }
    }
    uint8_t payload_digest[32];
    if (status == P4_FILE_TRANSFER_STATUS_OK) {
        status = digest_memory(package + payload_offset, payload_bytes, payload_digest);
        if (status == P4_FILE_TRANSFER_STATUS_OK &&
            memcmp(payload_digest, expected_digest, sizeof(payload_digest)) != 0) {
            status = P4_FILE_TRANSFER_STATUS_BAD_PACKAGE;
        }
    }
    heap_caps_free(package);
    return status;
}

static p4_file_transfer_status_t validate_path(
    const char *path, uint32_t required_size,
    const uint8_t required_digest[32],
    uint32_t *size_out, uint8_t digest_out[32])
{
    const uint32_t maximum =
        s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4G
            ? P4_GAME_PACKAGE_MAX_BYTES
            : P4_FILE_TRANSFER_EXCHANGE_MAX_BYTES;
    uint32_t actual_size = 0U;
    uint8_t actual_digest[32];
    p4_file_transfer_status_t status = hash_file(
        path, maximum, &actual_size, actual_digest);
    if (status == P4_FILE_TRANSFER_STATUS_OK &&
        required_size != 0U && actual_size != required_size) {
        status = P4_FILE_TRANSFER_STATUS_HASH;
    }
    if (status == P4_FILE_TRANSFER_STATUS_OK &&
        required_digest != NULL &&
        memcmp(actual_digest, required_digest, 32U) != 0) {
        status = P4_FILE_TRANSFER_STATUS_HASH;
    }
    if (status == P4_FILE_TRANSFER_STATUS_OK &&
        (s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4G ||
         s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4R)) {
        status = validate_native(path, actual_size);
    }
    if (status == P4_FILE_TRANSFER_STATUS_OK &&
        s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4CART) {
        p4_content_item_t item;
        if (p4_content_validate_cart_file(path, &item) != P4_CONTENT_OK) {
            status = P4_FILE_TRANSFER_STATUS_BAD_PACKAGE;
        }
    }
    if (status == P4_FILE_TRANSFER_STATUS_OK) {
        if (size_out != NULL) {
            *size_out = actual_size;
        }
        if (digest_out != NULL) {
            memcpy(digest_out, actual_digest, 32U);
        }
    }
    return status;
}

static p4_file_transfer_status_t write_all(
    int descriptor, const uint8_t *bytes, size_t length)
{
    size_t offset = 0U;
    while (offset < length) {
        const ssize_t count = write(
            descriptor, bytes + offset, length - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return P4_FILE_TRANSFER_STATUS_IO;
        }
        offset += (size_t)count;
    }
    return P4_FILE_TRANSFER_STATUS_OK;
}

static p4_file_transfer_status_t activate_upload(uint8_t digest[32])
{
    const int64_t started_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "P4_FILE_TRANSFER ACTIVATE stage=fsync-begin name=%s bytes=%lu",
             s_transfer.file_name,
             (unsigned long)s_transfer.expected_bytes);
    if (s_transfer.descriptor < 0 || fsync(s_transfer.descriptor) != 0) {
        s_transfer.descriptor = -1;
        return P4_FILE_TRANSFER_STATUS_IO;
    }
    ESP_LOGI(TAG, "P4_FILE_TRANSFER ACTIVATE stage=fsync-pass elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - started_us) / 1000));
    if (close(s_transfer.descriptor) != 0) {
        s_transfer.descriptor = -1;
        return P4_FILE_TRANSFER_STATUS_IO;
    }
    s_transfer.descriptor = -1;
    ESP_LOGI(TAG, "P4_FILE_TRANSFER ACTIVATE stage=close-pass elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - started_us) / 1000));
    if (mbedtls_sha256_finish(&s_transfer.sha256, digest) != 0) {
        free_sha256();
        return P4_FILE_TRANSFER_STATUS_HASH;
    }
    mbedtls_sha256_free(&s_transfer.sha256);
    s_transfer.sha_started = false;
    if (memcmp(digest, s_transfer.expected_digest, 32U) != 0) {
        return P4_FILE_TRANSFER_STATUS_HASH;
    }
    ESP_LOGI(TAG, "P4_FILE_TRANSFER ACTIVATE stage=stream-hash-pass");
    p4_file_transfer_status_t status = validate_path(
        s_transfer.temp_path, s_transfer.expected_bytes,
        s_transfer.expected_digest, NULL, NULL);
    if (status != P4_FILE_TRANSFER_STATUS_OK) {
        return status;
    }
    ESP_LOGI(TAG,
             "P4_FILE_TRANSFER ACTIVATE stage=staged-package-pass elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - started_us) / 1000));
    bool target_exists = false;
    status = regular_file_exists(s_transfer.target_path, &target_exists);
    if (status != P4_FILE_TRANSFER_STATUS_OK) {
        return status;
    }
    if (target_exists) {
        if (unlink(s_transfer.backup_path) != 0 && errno != ENOENT) {
            return P4_FILE_TRANSFER_STATUS_IO;
        }
        if (rename(s_transfer.target_path,
                   s_transfer.backup_path) != 0) {
            return P4_FILE_TRANSFER_STATUS_IO;
        }
        ESP_LOGI(TAG, "P4_FILE_TRANSFER ACTIVATE stage=backup-pass");
    }
    if (rename(s_transfer.temp_path, s_transfer.target_path) != 0) {
        if (target_exists) {
            (void)rename(s_transfer.backup_path, s_transfer.target_path);
        }
        return P4_FILE_TRANSFER_STATUS_IO;
    }
    ESP_LOGI(TAG, "P4_FILE_TRANSFER ACTIVATE stage=rename-pass");
    status = validate_path(
        s_transfer.target_path, s_transfer.expected_bytes,
        s_transfer.expected_digest, NULL, NULL);
    if (status != P4_FILE_TRANSFER_STATUS_OK) {
        (void)unlink(s_transfer.target_path);
        if (target_exists) {
            (void)rename(s_transfer.backup_path, s_transfer.target_path);
        }
        return status;
    }
    ESP_LOGI(TAG,
             "P4_FILE_TRANSFER ACTIVATE stage=target-package-pass elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - started_us) / 1000));
    if (target_exists && unlink(s_transfer.backup_path) != 0) {
        return P4_FILE_TRANSFER_STATUS_IO;
    }
    ESP_LOGI(TAG,
             "P4_FILE_TRANSFER ACTIVATE stage=complete elapsed_ms=%lld",
             (long long)((esp_timer_get_time() - started_us) / 1000));
    return P4_FILE_TRANSFER_STATUS_OK;
}

static bool switch_to_high_speed(void)
{
    if (s_transfer.transport.wait_tx(
            s_transfer.transport.context, 500U) != ESP_OK ||
        s_transfer.transport.set_baud(
            s_transfer.transport.context,
            P4_CONTENT_TRANSFER_BAUD) != ESP_OK) {
        return false;
    }
    s_transfer.high_speed = true;
    vTaskDelay(pdMS_TO_TICKS(100U));
    static const uint8_t marker[4] = {'P', '4', 'H', '2'};
    return transport_send(marker, sizeof(marker));
}

static void reject_request(p4_file_transfer_status_t status)
{
    s_transfer.last_status = (uint8_t)status;
    send_ready(status, 0U, NULL);
    reset_request_parser();
}

static void start_upload(void)
{
    p4_file_transfer_status_t status = prepare_paths();
    if (status == P4_FILE_TRANSFER_STATUS_OK) {
        status = recover_paths();
    }
    bool target_exists = false;
    if (status == P4_FILE_TRANSFER_STATUS_OK) {
        status = regular_file_exists(
            s_transfer.target_path, &target_exists);
    }
    if (status == P4_FILE_TRANSFER_STATUS_OK && target_exists) {
        const p4_file_transfer_status_t existing = validate_path(
            s_transfer.target_path, s_transfer.expected_bytes,
            s_transfer.expected_digest, NULL, NULL);
        if (existing == P4_FILE_TRANSFER_STATUS_OK) {
            send_ready(P4_FILE_TRANSFER_STATUS_ALREADY_PRESENT,
                       s_transfer.expected_bytes,
                       s_transfer.expected_digest);
            s_transfer.last_status =
                P4_FILE_TRANSFER_STATUS_ALREADY_PRESENT;
            reset_request_parser();
            return;
        }
        if (existing == P4_FILE_TRANSFER_STATUS_IO ||
            existing == P4_FILE_TRANSFER_STATUS_STORAGE) {
            status = existing;
        } else if (!s_transfer.replace_requested) {
            status = P4_FILE_TRANSFER_STATUS_OCCUPIED;
        }
    }
    if (status != P4_FILE_TRANSFER_STATUS_OK) {
        reject_request(status);
        return;
    }
    s_transfer.descriptor = open(
        s_transfer.temp_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (s_transfer.descriptor < 0) {
        reject_request(P4_FILE_TRANSFER_STATUS_IO);
        return;
    }
    mbedtls_sha256_init(&s_transfer.sha256);
    if (mbedtls_sha256_starts(&s_transfer.sha256, 0) != 0) {
        close_descriptor();
        (void)unlink(s_transfer.temp_path);
        reject_request(P4_FILE_TRANSFER_STATUS_HASH);
        return;
    }
    s_transfer.sha_started = true;
    s_transfer.transferred_bytes = 0U;
    s_transfer.expected_sequence = 0U;
    s_transfer.chunk_length = 0U;
    s_transfer.chunk_used = 0U;
    send_ready(P4_FILE_TRANSFER_STATUS_OK,
               s_transfer.expected_bytes, s_transfer.expected_digest);
    if (!switch_to_high_speed()) {
        finish_terminal(P4_FILE_TRANSFER_STATUS_IO, NULL);
        return;
    }
    s_transfer.public_state = P4_FILE_TRANSFER_RECEIVING;
    s_transfer.parser = PARSER_UPLOAD_CHUNK_MAGIC;
    s_transfer.magic_used = 0U;
    s_transfer.last_activity_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "P4_FILE_TRANSFER START direction=upload class=%u "
             "name=%s bytes=%lu baud=%u",
             (unsigned)s_transfer.file_class,
             s_transfer.file_name,
             (unsigned long)s_transfer.expected_bytes,
             (unsigned)P4_CONTENT_TRANSFER_BAUD);
}

static void start_download(void)
{
    p4_file_transfer_status_t status = prepare_paths();
    if (status == P4_FILE_TRANSFER_STATUS_OK) {
        status = recover_paths();
    }
    uint8_t digest[32];
    uint32_t size = 0U;
    if (status == P4_FILE_TRANSFER_STATUS_OK) {
        status = validate_path(
            s_transfer.target_path, 0U, NULL, &size, digest);
    }
    if (status != P4_FILE_TRANSFER_STATUS_OK) {
        reject_request(status);
        return;
    }
    s_transfer.descriptor = open(s_transfer.target_path, O_RDONLY);
    if (s_transfer.descriptor < 0) {
        reject_request(P4_FILE_TRANSFER_STATUS_IO);
        return;
    }
    s_transfer.expected_bytes = size;
    memcpy(s_transfer.expected_digest, digest, sizeof(digest));
    s_transfer.transferred_bytes = 0U;
    s_transfer.expected_sequence = 0U;
    s_transfer.chunk_length = 0U;
    s_transfer.download_retries = 0U;
    s_transfer.waiting_ack = false;
    send_ready(P4_FILE_TRANSFER_STATUS_OK, size, digest);
    if (!switch_to_high_speed()) {
        finish_terminal(P4_FILE_TRANSFER_STATUS_IO, NULL);
        return;
    }
    s_transfer.public_state = P4_FILE_TRANSFER_SENDING;
    s_transfer.parser = PARSER_DOWNLOAD_ACK_MAGIC;
    s_transfer.magic_used = 0U;
    s_transfer.last_activity_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "P4_FILE_TRANSFER START direction=download class=%u "
             "name=%s bytes=%lu baud=%u",
             (unsigned)s_transfer.file_class,
             s_transfer.file_name,
             (unsigned long)s_transfer.expected_bytes,
             (unsigned)P4_CONTENT_TRANSFER_BAUD);
}

static void accept_request(void)
{
    const uint32_t supplied_crc = read_u32_le(
        &s_transfer.request[REQUEST_CRC_OFFSET]);
    const uint32_t actual_crc = crc32_bytes(
        s_transfer.request, REQUEST_CRC_OFFSET);
    s_transfer.direction =
        (p4_file_transfer_direction_t)s_transfer.request[4];
    s_transfer.file_class =
        (p4_file_transfer_class_t)s_transfer.request[5];
    const uint16_t flags = read_u16_le(&s_transfer.request[6]);
    const uint32_t size = read_u32_le(&s_transfer.request[8]);
    if (supplied_crc != actual_crc ||
        !request_name(s_transfer.file_name)) {
        reject_request(P4_FILE_TRANSFER_STATUS_BAD_REQUEST);
        return;
    }
    if (s_transfer.direction != P4_FILE_TRANSFER_UPLOAD &&
        s_transfer.direction != P4_FILE_TRANSFER_DOWNLOAD) {
        reject_request(P4_FILE_TRANSFER_STATUS_UNSUPPORTED);
        return;
    }
    if (s_transfer.file_class != P4_FILE_TRANSFER_CLASS_P4G &&
        s_transfer.file_class != P4_FILE_TRANSFER_CLASS_EXCHANGE &&
        s_transfer.file_class != P4_FILE_TRANSFER_CLASS_P4R &&
        s_transfer.file_class != P4_FILE_TRANSFER_CLASS_P4CART) {
        reject_request(P4_FILE_TRANSFER_STATUS_UNSUPPORTED);
        return;
    }
    if ((s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4G &&
         !game_name_valid(s_transfer.file_name, ".P4G")) ||
        (s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4R &&
         !game_name_valid(s_transfer.file_name, ".P4R")) ||
        (s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4CART &&
         !game_name_valid(s_transfer.file_name, ".P4CART")) ||
        (s_transfer.file_class == P4_FILE_TRANSFER_CLASS_EXCHANGE &&
         !exchange_name_valid(s_transfer.file_name))) {
        reject_request(P4_FILE_TRANSFER_STATUS_BAD_NAME);
        return;
    }
    if (!s_transfer.available) {
        reject_request(P4_FILE_TRANSFER_STATUS_STORAGE);
        return;
    }
    if (s_transfer.direction == P4_FILE_TRANSFER_UPLOAD) {
        const uint32_t maximum =
            s_transfer.file_class == P4_FILE_TRANSFER_CLASS_P4G
                ? P4_GAME_PACKAGE_MAX_BYTES
                : P4_FILE_TRANSFER_EXCHANGE_MAX_BYTES;
        if ((flags & (uint16_t)~P4_FILE_TRANSFER_FLAG_REPLACE) != 0U ||
            size == 0U || size > maximum) {
            reject_request(size > maximum
                ? P4_FILE_TRANSFER_STATUS_TOO_LARGE
                : P4_FILE_TRANSFER_STATUS_BAD_REQUEST);
            return;
        }
        s_transfer.replace_requested =
            (flags & P4_FILE_TRANSFER_FLAG_REPLACE) != 0U;
        s_transfer.expected_bytes = size;
        memcpy(s_transfer.expected_digest, &s_transfer.request[12], 32U);
        start_upload();
        return;
    }
    if (flags != 0U || size != 0U ||
        !all_zero(&s_transfer.request[12], 32U)) {
        reject_request(P4_FILE_TRANSFER_STATUS_BAD_REQUEST);
        return;
    }
    s_transfer.replace_requested = false;
    start_download();
}

static void finish_upload_chunk(void)
{
    const uint32_t supplied_crc = read_u32_le(
        &s_transfer.chunk_header[12]);
    const uint32_t actual_crc = crc32_bytes(
        s_transfer.wire_chunk, s_transfer.chunk_length);
    if (supplied_crc != actual_crc) {
        send_ack(s_transfer.expected_sequence,
                 P4_FILE_TRANSFER_STATUS_CRC);
        finish_terminal(P4_FILE_TRANSFER_STATUS_CRC, NULL);
        return;
    }
    p4_file_transfer_status_t status = write_all(
        s_transfer.descriptor, s_transfer.wire_chunk,
        s_transfer.chunk_length);
    if (status == P4_FILE_TRANSFER_STATUS_OK &&
        mbedtls_sha256_update(
            &s_transfer.sha256, s_transfer.wire_chunk,
            s_transfer.chunk_length) != 0) {
        status = P4_FILE_TRANSFER_STATUS_HASH;
    }
    if (status != P4_FILE_TRANSFER_STATUS_OK) {
        send_ack(s_transfer.expected_sequence, status);
        finish_terminal(status, NULL);
        return;
    }
    s_transfer.transferred_bytes += s_transfer.chunk_length;
    send_ack(s_transfer.expected_sequence, P4_FILE_TRANSFER_STATUS_OK);
    ++s_transfer.expected_sequence;
    s_transfer.last_activity_us = esp_timer_get_time();
    if (s_transfer.transferred_bytes == s_transfer.expected_bytes) {
        uint8_t digest[32];
        status = activate_upload(digest);
        finish_terminal(status,
            status == P4_FILE_TRANSFER_STATUS_OK ? digest : NULL);
        return;
    }
    s_transfer.parser = PARSER_UPLOAD_CHUNK_MAGIC;
    s_transfer.magic_used = 0U;
    s_transfer.chunk_used = 0U;
}

static void parse_upload_header(void)
{
    const uint32_t sequence = read_u32_le(&s_transfer.chunk_header[4]);
    const uint16_t length = read_u16_le(&s_transfer.chunk_header[8]);
    const uint16_t reserved = read_u16_le(&s_transfer.chunk_header[10]);
    const uint32_t remaining =
        s_transfer.expected_bytes - s_transfer.transferred_bytes;
    if (sequence != s_transfer.expected_sequence) {
        send_ack(sequence, P4_FILE_TRANSFER_STATUS_SEQUENCE);
        finish_terminal(P4_FILE_TRANSFER_STATUS_SEQUENCE, NULL);
        return;
    }
    if (reserved != 0U || length == 0U ||
        length > P4_FILE_TRANSFER_CHUNK_BYTES || length > remaining) {
        send_ack(sequence, P4_FILE_TRANSFER_STATUS_BAD_REQUEST);
        finish_terminal(P4_FILE_TRANSFER_STATUS_BAD_REQUEST, NULL);
        return;
    }
    s_transfer.chunk_length = length;
    s_transfer.chunk_used = 0U;
    s_transfer.parser = PARSER_UPLOAD_CHUNK_DATA;
}

static void parse_download_ack(void)
{
    const uint32_t sequence = read_u32_le(&s_transfer.ack[4]);
    const uint8_t status = s_transfer.ack[8];
    if (!s_transfer.waiting_ack ||
        sequence != s_transfer.expected_sequence) {
        finish_terminal(P4_FILE_TRANSFER_STATUS_SEQUENCE, NULL);
        return;
    }
    if (status == P4_FILE_TRANSFER_STATUS_CRC &&
        s_transfer.download_retries < DOWNLOAD_RETRY_LIMIT) {
        ++s_transfer.download_retries;
        s_transfer.waiting_ack = false;
        s_transfer.parser = PARSER_DOWNLOAD_ACK_MAGIC;
        s_transfer.magic_used = 0U;
        s_transfer.last_activity_us = esp_timer_get_time();
        return;
    }
    if (status != P4_FILE_TRANSFER_STATUS_OK) {
        finish_terminal(status <= P4_FILE_TRANSFER_STATUS_BUSY
            ? (p4_file_transfer_status_t)status
            : P4_FILE_TRANSFER_STATUS_BAD_REQUEST, NULL);
        return;
    }
    s_transfer.transferred_bytes += s_transfer.chunk_length;
    ++s_transfer.expected_sequence;
    s_transfer.chunk_length = 0U;
    s_transfer.download_retries = 0U;
    s_transfer.waiting_ack = false;
    s_transfer.parser = PARSER_DOWNLOAD_ACK_MAGIC;
    s_transfer.magic_used = 0U;
    s_transfer.last_activity_us = esp_timer_get_time();
    if (s_transfer.transferred_bytes == s_transfer.expected_bytes) {
        finish_terminal(P4_FILE_TRANSFER_STATUS_OK,
                        s_transfer.expected_digest);
    }
}

static void consume_magic(uint8_t byte, const uint8_t magic[4],
                          parser_state_t next_state, uint8_t *storage)
{
    if (byte == magic[s_transfer.magic_used]) {
        storage[s_transfer.magic_used] = byte;
        ++s_transfer.magic_used;
        if (s_transfer.magic_used == 4U) {
            s_transfer.parser_used = 4U;
            s_transfer.magic_used = 0U;
            s_transfer.parser = next_state;
        }
        return;
    }
    s_transfer.magic_used = byte == magic[0] ? 1U : 0U;
    if (s_transfer.magic_used == 1U) {
        storage[0] = byte;
    }
}

static bool consume_byte(uint8_t byte)
{
    switch (s_transfer.parser) {
    case PARSER_REQUEST_MAGIC:
        consume_magic(byte, REQUEST_MAGIC, PARSER_REQUEST_BODY,
                      s_transfer.request);
        return false;
    case PARSER_REQUEST_BODY:
        s_transfer.request[s_transfer.parser_used++] = byte;
        if (s_transfer.parser_used == REQUEST_BYTES) {
            accept_request();
            return true;
        }
        return false;
    case PARSER_UPLOAD_CHUNK_MAGIC:
        consume_magic(byte, CHUNK_MAGIC, PARSER_UPLOAD_CHUNK_HEADER,
                      s_transfer.chunk_header);
        return true;
    case PARSER_UPLOAD_CHUNK_HEADER:
        s_transfer.chunk_header[s_transfer.parser_used++] = byte;
        if (s_transfer.parser_used == CHUNK_HEADER_BYTES) {
            parse_upload_header();
        }
        return true;
    case PARSER_UPLOAD_CHUNK_DATA:
        s_transfer.wire_chunk[s_transfer.chunk_used++] = byte;
        if (s_transfer.chunk_used == s_transfer.chunk_length) {
            finish_upload_chunk();
        }
        return true;
    case PARSER_DOWNLOAD_ACK_MAGIC:
        consume_magic(byte, ACK_MAGIC, PARSER_DOWNLOAD_ACK_BODY,
                      s_transfer.ack);
        return true;
    case PARSER_DOWNLOAD_ACK_BODY:
        s_transfer.ack[s_transfer.parser_used++] = byte;
        if (s_transfer.parser_used == ACK_BYTES) {
            parse_download_ack();
        }
        return true;
    case PARSER_TERMINAL:
        return true;
    default:
        return false;
    }
}

static void poll_download(void)
{
    if (s_transfer.waiting_ack || s_transfer.descriptor < 0) {
        return;
    }
    if (s_transfer.chunk_length == 0U) {
        const uint32_t remaining =
            s_transfer.expected_bytes - s_transfer.transferred_bytes;
        const size_t requested = remaining < P4_FILE_TRANSFER_CHUNK_BYTES
            ? (size_t)remaining : (size_t)P4_FILE_TRANSFER_CHUNK_BYTES;
        const ssize_t count = read(
            s_transfer.descriptor,
            s_transfer.wire_chunk + CHUNK_HEADER_BYTES,
            requested);
        if (count <= 0 || (size_t)count > requested) {
            finish_terminal(P4_FILE_TRANSFER_STATUS_IO, NULL);
            return;
        }
        s_transfer.chunk_length = (uint32_t)count;
        memcpy(s_transfer.wire_chunk, CHUNK_MAGIC, 4U);
        write_u32_le(&s_transfer.wire_chunk[4],
                     s_transfer.expected_sequence);
        write_u16_le(&s_transfer.wire_chunk[8],
                     (uint16_t)s_transfer.chunk_length);
        write_u16_le(&s_transfer.wire_chunk[10], 0U);
        write_u32_le(
            &s_transfer.wire_chunk[12],
            crc32_bytes(
                s_transfer.wire_chunk + CHUNK_HEADER_BYTES,
                s_transfer.chunk_length));
    }
    if (!transport_send(
            s_transfer.wire_chunk,
            CHUNK_HEADER_BYTES + s_transfer.chunk_length)) {
        finish_terminal(P4_FILE_TRANSFER_STATUS_IO, NULL);
        return;
    }
    s_transfer.waiting_ack = true;
    s_transfer.last_activity_us = esp_timer_get_time();
}

esp_err_t p4_file_transfer_init(
    const char *mounted_storage_root,
    const p4_content_transfer_transport_t *transport)
{
    if (mounted_storage_root == NULL || mounted_storage_root[0] != '/' ||
        strlen(mounted_storage_root) >= sizeof(s_transfer.storage_root) ||
        transport == NULL || transport->send == NULL ||
        transport->wait_tx == NULL || transport->set_baud == NULL ||
        transport->idle_baud < 9600U || transport->idle_baud > 2000000U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_transfer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    s_transfer.wire_chunk = heap_caps_malloc(
        WIRE_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_transfer.wire_chunk == NULL) {
        s_transfer.wire_chunk = heap_caps_malloc(
            WIRE_CHUNK_BYTES, MALLOC_CAP_8BIT);
    }
    if (s_transfer.wire_chunk == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(s_transfer.storage_root, mounted_storage_root,
           strlen(mounted_storage_root) + 1U);
    s_transfer.transport = *transport;
    s_transfer.initialized = true;
    s_transfer.available = false;
    s_transfer.public_state = P4_FILE_TRANSFER_IDLE;
    s_transfer.last_status = P4_FILE_TRANSFER_STATUS_OK;
    reset_request_parser();
    ESP_LOGI(TAG,
             "P4_FILE_TRANSFER READY protocol=%u transport=console-link "
             "idle_baud=%u transfer_baud=%u chunk=%u "
             "p4g_max=%u exchange_max=%u",
             (unsigned)P4_FILE_TRANSFER_PROTOCOL_VERSION,
             (unsigned)transport->idle_baud,
             (unsigned)P4_CONTENT_TRANSFER_BAUD,
             (unsigned)P4_FILE_TRANSFER_CHUNK_BYTES,
             (unsigned)P4_GAME_PACKAGE_MAX_BYTES,
             (unsigned)P4_FILE_TRANSFER_EXCHANGE_MAX_BYTES);
    return ESP_OK;
}

void p4_file_transfer_set_available(bool available)
{
    if (s_transfer.initialized &&
        s_transfer.public_state == P4_FILE_TRANSFER_IDLE) {
        s_transfer.available = available;
    }
}

bool p4_file_transfer_consume(const uint8_t *bytes, size_t bytes_length)
{
    if (!s_transfer.initialized ||
        (bytes == NULL && bytes_length != 0U)) {
        return false;
    }
    bool claimed =
        s_transfer.public_state != P4_FILE_TRANSFER_IDLE;
    for (size_t index = 0U; index < bytes_length; ++index) {
        claimed = consume_byte(bytes[index]) || claimed;
        if (s_transfer.parser == PARSER_TERMINAL) {
            break;
        }
    }
    return claimed;
}

void p4_file_transfer_poll(void)
{
    if (!s_transfer.initialized) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (s_transfer.parser == PARSER_TERMINAL) {
        if (now >= s_transfer.terminal_until_us) {
            s_transfer.public_state = P4_FILE_TRANSFER_IDLE;
            s_transfer.expected_bytes = 0U;
            s_transfer.transferred_bytes = 0U;
            s_transfer.chunk_length = 0U;
            s_transfer.waiting_ack = false;
            reset_request_parser();
        }
        return;
    }
    if ((s_transfer.public_state == P4_FILE_TRANSFER_RECEIVING ||
         s_transfer.public_state == P4_FILE_TRANSFER_SENDING) &&
        now - s_transfer.last_activity_us > TRANSFER_TIMEOUT_US) {
        finish_terminal(P4_FILE_TRANSFER_STATUS_TIMEOUT, NULL);
        return;
    }
    if (s_transfer.public_state == P4_FILE_TRANSFER_SENDING) {
        poll_download();
    }
}

p4_file_transfer_info_t p4_file_transfer_info(void)
{
    uint8_t percent = 0U;
    if (s_transfer.expected_bytes != 0U) {
        const uint64_t scaled =
            (uint64_t)s_transfer.transferred_bytes * UINT64_C(100);
        percent = (uint8_t)(scaled / s_transfer.expected_bytes);
    }
    p4_file_transfer_info_t info = {
        .state = s_transfer.public_state,
        .direction = s_transfer.direction,
        .file_class = s_transfer.file_class,
        .transferred_bytes = s_transfer.transferred_bytes,
        .total_bytes = s_transfer.expected_bytes,
        .generation = s_transfer.generation,
        .progress_percent = percent,
        .last_status = s_transfer.last_status,
        .ready = s_transfer.initialized,
        .busy = s_transfer.public_state == P4_FILE_TRANSFER_RECEIVING ||
                s_transfer.public_state == P4_FILE_TRANSFER_SENDING,
    };
    memcpy(info.file_name, s_transfer.file_name, sizeof(info.file_name));
    return info;
}
