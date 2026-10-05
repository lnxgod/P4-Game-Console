// SPDX-License-Identifier: MIT

#include "p4/content_transfer.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "mbedtls/sha256.h"
#include "p4/content_catalog.h"

enum {
    MANIFEST_BYTES = 48,
    MANIFEST_CRC_OFFSET = 44,
    CHUNK_HEADER_BYTES = 16,
    TRANSFER_TIMEOUT_US = 15000000,
    RESTART_DELAY_US = 350000,
    CONTENT_KIND_QUAKE_SHAREWARE = 1,
    CONTENT_KIND_DOOM_SHAREWARE = 2,
    CONTENT_KIND_CHEX_QUEST_WAD = 3,
    CONTENT_KIND_CHEX_QUEST_DEH = 4,
    CONTENT_FLAG_REPLACE = 1,
    DOOM_SHAREWARE_BYTES = 4196020,
    CHEX_QUEST_WAD_BYTES = 12361532,
    CHEX_QUEST_DEH_BYTES = 20367,
};

typedef enum {
    WIRE_STATUS_OK = 0,
    WIRE_STATUS_BAD_MANIFEST = 1,
    WIRE_STATUS_STORAGE = 2,
    WIRE_STATUS_OCCUPIED = 3,
    WIRE_STATUS_IO = 4,
    WIRE_STATUS_SEQUENCE = 5,
    WIRE_STATUS_CRC = 6,
    WIRE_STATUS_HASH = 7,
    WIRE_STATUS_TIMEOUT = 8,
    WIRE_STATUS_UNSUPPORTED = 9,
    WIRE_STATUS_ALREADY_PRESENT = 10,
} wire_status_t;

typedef enum {
    PARSER_MANIFEST_MAGIC = 0,
    PARSER_MANIFEST_BODY,
    PARSER_CHUNK_MAGIC,
    PARSER_CHUNK_HEADER,
    PARSER_CHUNK_DATA,
    PARSER_TERMINAL,
} parser_state_t;

typedef struct {
    uint8_t kind;
    uint32_t bytes;
    const char *sha256_hex;
    const char *label;
    const char *directory_suffix;
    const char *target_name;
    const char *temporary_name;
    const char *const *required_directories;
    size_t required_directory_count;
} content_spec_t;

typedef struct {
    char storage_root[P4_CONTENT_PATH_BYTES];
    char target_path[P4_CONTENT_PATH_BYTES];
    char temp_path[P4_CONTENT_PATH_BYTES];
    uint8_t manifest[MANIFEST_BYTES];
    uint8_t chunk_header[CHUNK_HEADER_BYTES];
    uint8_t *chunk;
    mbedtls_sha256_context sha256;
    p4_content_transfer_transport_t transport;
    const content_spec_t *spec;
    parser_state_t parser;
    p4_content_transfer_state_t public_state;
    size_t parser_used;
    size_t chunk_used;
    uint32_t chunk_length;
    uint32_t expected_sequence;
    uint32_t expected_bytes;
    uint32_t received_bytes;
    int descriptor;
    int64_t last_activity_us;
    int64_t restart_at_us;
    uint8_t expected_digest[P4_CONTENT_SHA256_BYTES];
    uint8_t last_status;
    uint8_t magic_used;
    bool initialized;
    bool available;
    bool sha_started;
    bool replace_requested;
} transfer_service_t;

static const char *const TAG = "p4_usb_content";
static const uint8_t MANIFEST_MAGIC[4] = {'P', '4', 'M', '1'};
static const uint8_t CHUNK_MAGIC[4] = {'P', '4', 'C', '1'};
static const char QUAKE_SHA256_HEX[] =
    "35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af";
static const char DOOM_SHA256_HEX[] =
    "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771";
static const char CHEX_WAD_SHA256_HEX[] =
    "d8eb5277918883f490fb1a4be3c9a8588df2dbaee6dc4beb8df4929148bbffb1";
static const char CHEX_DEH_SHA256_HEX[] =
    "8c0345089fb227fa7f71c25a6c6e31ff5bd4bea0580f286cd74e05918d72dd40";
static const char *const QUAKE_DIRECTORIES[] = {
    "/GAMES", "/GAMES/QUAKE", "/GAMES/QUAKE/ID1",
};
static const content_spec_t CONTENT_SPECS[] = {
    {
        .kind = CONTENT_KIND_QUAKE_SHAREWARE,
        .bytes = P4_CONTENT_QUAKE_SHAREWARE_BYTES,
        .sha256_hex = QUAKE_SHA256_HEX,
        .label = "quake-shareware",
        .directory_suffix = "/GAMES/QUAKE/ID1",
        .target_name = "PAK0.PAK",
        .temporary_name = "P4Q.TMP",
        .required_directories = QUAKE_DIRECTORIES,
        .required_directory_count =
            sizeof(QUAKE_DIRECTORIES) / sizeof(QUAKE_DIRECTORIES[0]),
    },
    {
        .kind = CONTENT_KIND_DOOM_SHAREWARE,
        .bytes = DOOM_SHAREWARE_BYTES,
        .sha256_hex = DOOM_SHA256_HEX,
        .label = "doom-shareware",
        .directory_suffix = "",
        .target_name = "DOOM1.WAD",
        .temporary_name = "P4D1.TMP",
        .required_directories = NULL,
        .required_directory_count = 0U,
    },
    {
        .kind = CONTENT_KIND_CHEX_QUEST_WAD,
        .bytes = CHEX_QUEST_WAD_BYTES,
        .sha256_hex = CHEX_WAD_SHA256_HEX,
        .label = "chex-quest-wad",
        .directory_suffix = "",
        .target_name = "CHEX.WAD",
        .temporary_name = "P4CXW.TMP",
        .required_directories = NULL,
        .required_directory_count = 0U,
    },
    {
        .kind = CONTENT_KIND_CHEX_QUEST_DEH,
        .bytes = CHEX_QUEST_DEH_BYTES,
        .sha256_hex = CHEX_DEH_SHA256_HEX,
        .label = "chex-quest-deh",
        .directory_suffix = "",
        .target_name = "CHEX.DEH",
        .temporary_name = "P4CXD.TMP",
        .required_directories = NULL,
        .required_directory_count = 0U,
    },
};
static transfer_service_t s_transfer = {.descriptor = -1};

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

static bool parse_hex_digest(const char *hex, uint8_t digest[32])
{
    for (size_t index = 0U; index < 32U; ++index) {
        const char high = hex[index * 2U];
        const char low = hex[index * 2U + 1U];
        const int high_value = high >= '0' && high <= '9' ? high - '0' :
            high >= 'a' && high <= 'f' ? high - 'a' + 10 : -1;
        const int low_value = low >= '0' && low <= '9' ? low - '0' :
            low >= 'a' && low <= 'f' ? low - 'a' + 10 : -1;
        if (high_value < 0 || low_value < 0) {
            return false;
        }
        digest[index] = (uint8_t)((unsigned)high_value << 4U |
                                  (unsigned)low_value);
    }
    return hex[64] == '\0';
}

static bool transport_send(const uint8_t *bytes, size_t length)
{
    return s_transfer.transport.send != NULL &&
        s_transfer.transport.send(
            s_transfer.transport.context, bytes, length) == ESP_OK;
}

static void send_ready(wire_status_t status)
{
    uint8_t response[11] = {'P', '4', 'R', '1', (uint8_t)status};
    write_u32_le(&response[5], P4_CONTENT_TRANSFER_BAUD);
    write_u16_le(&response[9], P4_CONTENT_TRANSFER_CHUNK_BYTES);
    (void)transport_send(response, sizeof(response));
}

static void send_ack(uint32_t sequence, wire_status_t status)
{
    uint8_t response[9] = {'P', '4', 'A', '1'};
    write_u32_le(&response[4], sequence);
    response[8] = (uint8_t)status;
    (void)transport_send(response, sizeof(response));
}

static void send_done(wire_status_t status, const uint8_t digest[32])
{
    uint8_t response[41] = {'P', '4', 'D', '1', (uint8_t)status};
    write_u32_le(&response[5], s_transfer.received_bytes);
    if (digest != NULL) {
        memcpy(&response[9], digest, 32U);
    }
    (void)transport_send(response, sizeof(response));
}

static void reset_idle_parser(void)
{
    s_transfer.parser = PARSER_MANIFEST_MAGIC;
    s_transfer.parser_used = 0U;
    s_transfer.magic_used = 0U;
}

static void close_transfer_file(bool remove_temp)
{
    if (s_transfer.descriptor >= 0) {
        (void)close(s_transfer.descriptor);
        s_transfer.descriptor = -1;
    }
    if (remove_temp && s_transfer.temp_path[0] != '\0') {
        (void)unlink(s_transfer.temp_path);
    }
    if (s_transfer.sha_started) {
        mbedtls_sha256_free(&s_transfer.sha256);
        s_transfer.sha_started = false;
    }
}

static void terminal(wire_status_t status, const uint8_t digest[32])
{
    close_transfer_file(status != WIRE_STATUS_OK);
    s_transfer.last_status = (uint8_t)status;
    s_transfer.public_state = status == WIRE_STATUS_OK ?
        P4_CONTENT_TRANSFER_INSTALLED : P4_CONTENT_TRANSFER_FAILED;
    s_transfer.parser = PARSER_TERMINAL;
    send_done(status, digest);
    s_transfer.restart_at_us = esp_timer_get_time() + RESTART_DELAY_US;
}

static bool append_path(char *output, size_t output_size,
                        const char *root, const char *suffix)
{
    const int count = snprintf(output, output_size, "%s%s", root, suffix);
    return count >= 0 && (size_t)count < output_size;
}

static wire_status_t ensure_directory(const char *path)
{
    if (mkdir(path, 0755) == 0) {
        return WIRE_STATUS_OK;
    }
    if (errno != EEXIST) {
        return WIRE_STATUS_IO;
    }
    struct stat metadata;
    if (stat(path, &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        return WIRE_STATUS_STORAGE;
    }
    return WIRE_STATUS_OK;
}

static const content_spec_t *content_spec_for_kind(uint8_t kind)
{
    for (size_t index = 0U;
         index < sizeof(CONTENT_SPECS) / sizeof(CONTENT_SPECS[0]); ++index) {
        if (CONTENT_SPECS[index].kind == kind) {
            return &CONTENT_SPECS[index];
        }
    }
    return NULL;
}

static wire_status_t prepare_content_paths(const content_spec_t *spec)
{
    if (spec == NULL) {
        return WIRE_STATUS_UNSUPPORTED;
    }
    char path[P4_CONTENT_PATH_BYTES];
    for (size_t index = 0U;
         index < spec->required_directory_count; ++index) {
        if (!append_path(path, sizeof(path), s_transfer.storage_root,
                         spec->required_directories[index])) {
            return WIRE_STATUS_STORAGE;
        }
        const wire_status_t status = ensure_directory(path);
        if (status != WIRE_STATUS_OK) {
            return status;
        }
    }
    char directory[P4_CONTENT_PATH_BYTES];
    if (!append_path(directory, sizeof(directory), s_transfer.storage_root,
                     spec->directory_suffix)) {
        return WIRE_STATUS_STORAGE;
    }
    const int target_count = snprintf(
        s_transfer.target_path, sizeof(s_transfer.target_path),
        "%s/%s", directory, spec->target_name);
    const int temp_count = snprintf(
        s_transfer.temp_path, sizeof(s_transfer.temp_path),
        "%s/%s", directory, spec->temporary_name);
    if (target_count < 0 ||
        (size_t)target_count >= sizeof(s_transfer.target_path) ||
        temp_count < 0 ||
        (size_t)temp_count >= sizeof(s_transfer.temp_path)) {
        return WIRE_STATUS_STORAGE;
    }
    return WIRE_STATUS_OK;
}

static wire_status_t validate_exact_file(
    const char *path, const content_spec_t *spec)
{
    if (path == NULL || spec == NULL) {
        return WIRE_STATUS_STORAGE;
    }
    struct stat metadata;
    if (stat(path, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        metadata.st_size < 0 || (uint64_t)metadata.st_size != spec->bytes) {
        return WIRE_STATUS_HASH;
    }
    uint8_t expected[32];
    if (!parse_hex_digest(spec->sha256_hex, expected)) {
        return WIRE_STATUS_HASH;
    }
    const int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        return WIRE_STATUS_IO;
    }
    mbedtls_sha256_context sha256;
    mbedtls_sha256_init(&sha256);
    wire_status_t status = mbedtls_sha256_starts(&sha256, 0) == 0
        ? WIRE_STATUS_OK : WIRE_STATUS_HASH;
    uint32_t total = 0U;
    while (status == WIRE_STATUS_OK && total < spec->bytes) {
        const size_t remaining = (size_t)(spec->bytes - total);
        const size_t requested = remaining < P4_CONTENT_TRANSFER_CHUNK_BYTES
            ? remaining : P4_CONTENT_TRANSFER_CHUNK_BYTES;
        const ssize_t count = read(descriptor, s_transfer.chunk, requested);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0 || (size_t)count > requested ||
            mbedtls_sha256_update(
                &sha256, s_transfer.chunk, (size_t)count) != 0) {
            status = WIRE_STATUS_IO;
            break;
        }
        total += (uint32_t)count;
        (void)esp_task_wdt_reset();
    }
    uint8_t actual[32] = {0};
    if (status == WIRE_STATUS_OK &&
        mbedtls_sha256_finish(&sha256, actual) != 0) {
        status = WIRE_STATUS_HASH;
    }
    mbedtls_sha256_free(&sha256);
    if (close(descriptor) != 0 && status == WIRE_STATUS_OK) {
        status = WIRE_STATUS_IO;
    }
    if (status == WIRE_STATUS_OK &&
        (total != spec->bytes ||
         memcmp(actual, expected, sizeof(actual)) != 0)) {
        status = WIRE_STATUS_HASH;
    }
    return status;
}

static wire_status_t target_exists_case_insensitive(
    const content_spec_t *spec, bool *exists_out)
{
    if (spec == NULL || exists_out == NULL) {
        return WIRE_STATUS_STORAGE;
    }
    *exists_out = false;
    struct stat metadata;
    if (stat(s_transfer.target_path, &metadata) == 0) {
        if (!S_ISREG(metadata.st_mode)) {
            return WIRE_STATUS_STORAGE;
        }
        *exists_out = true;
        return WIRE_STATUS_OK;
    }
    if (errno == EINVAL) {
        char directory[P4_CONTENT_PATH_BYTES];
        if (!append_path(directory, sizeof(directory), s_transfer.storage_root,
                         spec->directory_suffix)) {
            return WIRE_STATUS_STORAGE;
        }
        DIR *const stream = opendir(directory);
        if (stream == NULL) {
            return WIRE_STATUS_STORAGE;
        }
        errno = 0;
        const struct dirent *entry = NULL;
        while ((entry = readdir(stream)) != NULL) {
            if (strcasecmp(entry->d_name, spec->target_name) == 0) {
                *exists_out = true;
                break;
            }
        }
        const int enumeration_errno = errno;
        if (closedir(stream) != 0 || enumeration_errno != 0) {
            return WIRE_STATUS_STORAGE;
        }
    } else if (errno != ENOENT) {
        return WIRE_STATUS_STORAGE;
    }
    return WIRE_STATUS_OK;
}

static wire_status_t open_staging_file(const content_spec_t *spec)
{
    bool target_exists = false;
    wire_status_t status = target_exists_case_insensitive(
        spec, &target_exists);
    if (status != WIRE_STATUS_OK) {
        return status;
    }
    if (target_exists) {
        if (validate_exact_file(
                s_transfer.target_path, spec) == WIRE_STATUS_OK) {
            return WIRE_STATUS_ALREADY_PRESENT;
        }
        if (!s_transfer.replace_requested) {
            return WIRE_STATUS_OCCUPIED;
        }
        /* Replacing an invalid target remains fail-closed until a recoverable
         * old-file journal exists. */
        return WIRE_STATUS_UNSUPPORTED;
    }
    s_transfer.descriptor = open(
        s_transfer.temp_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (s_transfer.descriptor < 0 && errno == EEXIST) {
        if (validate_exact_file(
                s_transfer.temp_path, spec) == WIRE_STATUS_OK &&
            rename(s_transfer.temp_path, s_transfer.target_path) == 0) {
            return WIRE_STATUS_ALREADY_PRESENT;
        }
        /* The fixed temp name is reserved and never runnable. A partial file
         * left by lost power is safe to discard before a clean retry. */
        if (unlink(s_transfer.temp_path) != 0) {
            return WIRE_STATUS_IO;
        }
        s_transfer.descriptor = open(
            s_transfer.temp_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    }
    return s_transfer.descriptor >= 0 ? WIRE_STATUS_OK : WIRE_STATUS_IO;
}

static void reject_manifest(wire_status_t status)
{
    s_transfer.last_status = (uint8_t)status;
    send_ready(status);
    reset_idle_parser();
}

static void accept_manifest(void)
{
    const uint32_t supplied_crc = read_u32_le(
        &s_transfer.manifest[MANIFEST_CRC_OFFSET]);
    const uint32_t actual_crc = crc32_bytes(
        s_transfer.manifest, MANIFEST_CRC_OFFSET);
    const uint8_t kind = s_transfer.manifest[4];
    const uint8_t flags = s_transfer.manifest[5];
    const uint16_t reserved = read_u16_le(&s_transfer.manifest[6]);
    const uint32_t size = read_u32_le(&s_transfer.manifest[8]);
    const content_spec_t *const spec = content_spec_for_kind(kind);
    uint8_t known_digest[32];
    if (supplied_crc != actual_crc || reserved != 0U ||
        (flags & (uint8_t)~CONTENT_FLAG_REPLACE) != 0U) {
        reject_manifest(WIRE_STATUS_BAD_MANIFEST);
        return;
    }
    if (spec == NULL) {
        reject_manifest(WIRE_STATUS_UNSUPPORTED);
        return;
    }
    if (!s_transfer.available) {
        reject_manifest(WIRE_STATUS_STORAGE);
        return;
    }
    if (!parse_hex_digest(spec->sha256_hex, known_digest) ||
        size != spec->bytes ||
        memcmp(&s_transfer.manifest[12], known_digest,
               sizeof(known_digest)) != 0) {
        reject_manifest(WIRE_STATUS_HASH);
        return;
    }
    s_transfer.replace_requested =
        (flags & CONTENT_FLAG_REPLACE) != 0U;
    s_transfer.spec = spec;
    wire_status_t status = prepare_content_paths(spec);
    if (status == WIRE_STATUS_OK) {
        status = open_staging_file(spec);
    }
    if (status == WIRE_STATUS_ALREADY_PRESENT) {
        s_transfer.expected_bytes = size;
        s_transfer.received_bytes = size;
        send_ready(status);
        s_transfer.last_status = (uint8_t)status;
        reset_idle_parser();
        return;
    }
    if (status != WIRE_STATUS_OK) {
        reject_manifest(status);
        return;
    }

    memcpy(s_transfer.expected_digest, known_digest, sizeof(known_digest));
    s_transfer.expected_bytes = size;
    s_transfer.received_bytes = 0U;
    s_transfer.expected_sequence = 0U;
    s_transfer.chunk_used = 0U;
    s_transfer.chunk_length = 0U;
    mbedtls_sha256_init(&s_transfer.sha256);
    if (mbedtls_sha256_starts(&s_transfer.sha256, 0) != 0) {
        close_transfer_file(true);
        reject_manifest(WIRE_STATUS_IO);
        return;
    }
    s_transfer.sha_started = true;
    send_ready(WIRE_STATUS_OK);
    if (s_transfer.transport.wait_tx(
            s_transfer.transport.context, 200U) != ESP_OK ||
        s_transfer.transport.set_baud(
            s_transfer.transport.context,
            P4_CONTENT_TRANSFER_BAUD) != ESP_OK) {
        terminal(WIRE_STATUS_IO, NULL);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100U));
    static const uint8_t high_speed_marker[4] = {'P', '4', 'H', '1'};
    if (!transport_send(high_speed_marker, sizeof(high_speed_marker))) {
        terminal(WIRE_STATUS_IO, NULL);
        return;
    }
    s_transfer.public_state = P4_CONTENT_TRANSFER_RECEIVING;
    s_transfer.parser = PARSER_CHUNK_MAGIC;
    s_transfer.magic_used = 0U;
    s_transfer.last_activity_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "P4_USB_CONTENT START kind=%s bytes=%lu "
             "transport=console-link baud=%u staging=fixed-path",
             spec->label,
             (unsigned long)s_transfer.expected_bytes,
             (unsigned)P4_CONTENT_TRANSFER_BAUD);
}

static wire_status_t write_all(int descriptor,
                               const uint8_t *bytes, size_t length)
{
    size_t offset = 0U;
    while (offset < length) {
        const ssize_t count = write(descriptor, &bytes[offset], length - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return WIRE_STATUS_IO;
        }
        offset += (size_t)count;
    }
    return WIRE_STATUS_OK;
}

static wire_status_t activate_staged_file(uint8_t digest[32])
{
    if (s_transfer.spec == NULL) {
        return WIRE_STATUS_BAD_MANIFEST;
    }
    if (fsync(s_transfer.descriptor) != 0 ||
        close(s_transfer.descriptor) != 0) {
        s_transfer.descriptor = -1;
        return WIRE_STATUS_IO;
    }
    s_transfer.descriptor = -1;
    if (mbedtls_sha256_finish(&s_transfer.sha256, digest) != 0) {
        return WIRE_STATUS_HASH;
    }
    mbedtls_sha256_free(&s_transfer.sha256);
    s_transfer.sha_started = false;
    if (memcmp(digest, s_transfer.expected_digest, 32U) != 0) {
        return WIRE_STATUS_HASH;
    }
    if (rename(s_transfer.temp_path, s_transfer.target_path) != 0) {
        return WIRE_STATUS_IO;
    }
    const wire_status_t readback = validate_exact_file(
        s_transfer.target_path, s_transfer.spec);
    if (readback != WIRE_STATUS_OK) {
        (void)unlink(s_transfer.target_path);
        return readback;
    }
    return WIRE_STATUS_OK;
}

static void finish_chunk(void)
{
    const uint32_t supplied_crc = read_u32_le(&s_transfer.chunk_header[12]);
    const uint32_t actual_crc = crc32_bytes(
        s_transfer.chunk, s_transfer.chunk_length);
    if (supplied_crc != actual_crc) {
        send_ack(s_transfer.expected_sequence, WIRE_STATUS_CRC);
        terminal(WIRE_STATUS_CRC, NULL);
        return;
    }
    wire_status_t status = write_all(
        s_transfer.descriptor, s_transfer.chunk, s_transfer.chunk_length);
    if (status == WIRE_STATUS_OK &&
        mbedtls_sha256_update(&s_transfer.sha256, s_transfer.chunk,
                              s_transfer.chunk_length) != 0) {
        status = WIRE_STATUS_HASH;
    }
    if (status != WIRE_STATUS_OK) {
        send_ack(s_transfer.expected_sequence, status);
        terminal(status, NULL);
        return;
    }
    s_transfer.received_bytes += s_transfer.chunk_length;
    send_ack(s_transfer.expected_sequence, WIRE_STATUS_OK);
    ++s_transfer.expected_sequence;
    s_transfer.last_activity_us = esp_timer_get_time();
    if (s_transfer.received_bytes == s_transfer.expected_bytes) {
        uint8_t digest[32];
        status = activate_staged_file(digest);
        terminal(status, status == WIRE_STATUS_OK ? digest : NULL);
        return;
    }
    s_transfer.parser = PARSER_CHUNK_MAGIC;
    s_transfer.magic_used = 0U;
    s_transfer.chunk_used = 0U;
}

static void parse_chunk_header(void)
{
    const uint32_t sequence = read_u32_le(&s_transfer.chunk_header[4]);
    const uint16_t length = read_u16_le(&s_transfer.chunk_header[8]);
    const uint16_t reserved = read_u16_le(&s_transfer.chunk_header[10]);
    const uint32_t remaining =
        s_transfer.expected_bytes - s_transfer.received_bytes;
    if (sequence != s_transfer.expected_sequence) {
        send_ack(sequence, WIRE_STATUS_SEQUENCE);
        terminal(WIRE_STATUS_SEQUENCE, NULL);
        return;
    }
    if (reserved != 0U || length == 0U ||
        length > P4_CONTENT_TRANSFER_CHUNK_BYTES || length > remaining) {
        send_ack(sequence, WIRE_STATUS_BAD_MANIFEST);
        terminal(WIRE_STATUS_BAD_MANIFEST, NULL);
        return;
    }
    s_transfer.chunk_length = length;
    s_transfer.chunk_used = 0U;
    s_transfer.parser = PARSER_CHUNK_DATA;
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

static void consume_byte(uint8_t byte)
{
    switch (s_transfer.parser) {
    case PARSER_MANIFEST_MAGIC:
        consume_magic(byte, MANIFEST_MAGIC, PARSER_MANIFEST_BODY,
                      s_transfer.manifest);
        break;
    case PARSER_MANIFEST_BODY:
        s_transfer.manifest[s_transfer.parser_used++] = byte;
        if (s_transfer.parser_used == MANIFEST_BYTES) {
            accept_manifest();
        }
        break;
    case PARSER_CHUNK_MAGIC:
        consume_magic(byte, CHUNK_MAGIC, PARSER_CHUNK_HEADER,
                      s_transfer.chunk_header);
        break;
    case PARSER_CHUNK_HEADER:
        s_transfer.chunk_header[s_transfer.parser_used++] = byte;
        if (s_transfer.parser_used == CHUNK_HEADER_BYTES) {
            parse_chunk_header();
        }
        break;
    case PARSER_CHUNK_DATA:
        s_transfer.chunk[s_transfer.chunk_used++] = byte;
        if (s_transfer.chunk_used == s_transfer.chunk_length) {
            finish_chunk();
        }
        break;
    case PARSER_TERMINAL:
        break;
    }
}

esp_err_t p4_content_transfer_init(
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
    s_transfer.chunk = heap_caps_malloc(
        P4_CONTENT_TRANSFER_CHUNK_BYTES,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_transfer.chunk == NULL) {
        s_transfer.chunk = heap_caps_malloc(
            P4_CONTENT_TRANSFER_CHUNK_BYTES, MALLOC_CAP_8BIT);
    }
    if (s_transfer.chunk == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(s_transfer.storage_root, mounted_storage_root,
           strlen(mounted_storage_root) + 1U);
    s_transfer.transport = *transport;
    s_transfer.initialized = true;
    s_transfer.available = false;
    s_transfer.public_state = P4_CONTENT_TRANSFER_IDLE;
    s_transfer.last_status = WIRE_STATUS_OK;
    reset_idle_parser();
    ESP_LOGI(TAG,
             "P4_USB_CONTENT READY transport=console-link idle_baud=%u "
             "transfer_baud=%u chunk=%u "
             "targets=doom-shareware-v1,quake-shareware-v1,"
             "chex-quest-wad-v1,chex-quest-deh-v1",
             (unsigned)transport->idle_baud,
             (unsigned)P4_CONTENT_TRANSFER_BAUD,
             (unsigned)P4_CONTENT_TRANSFER_CHUNK_BYTES);
    return ESP_OK;
}

void p4_content_transfer_set_available(bool available)
{
    if (s_transfer.initialized &&
        s_transfer.public_state == P4_CONTENT_TRANSFER_IDLE) {
        s_transfer.available = available;
    }
}

bool p4_content_transfer_consume(
    const uint8_t *bytes, size_t bytes_length)
{
    if (!s_transfer.initialized ||
        (bytes == NULL && bytes_length != 0U)) {
        return false;
    }
    const bool claimed_before =
        s_transfer.public_state != P4_CONTENT_TRANSFER_IDLE;
    for (size_t index = 0U; index < bytes_length; ++index) {
        consume_byte(bytes[index]);
        if (s_transfer.parser == PARSER_TERMINAL) {
            break;
        }
    }
    return claimed_before ||
        s_transfer.public_state != P4_CONTENT_TRANSFER_IDLE;
}

void p4_content_transfer_poll(void)
{
    if (!s_transfer.initialized) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (s_transfer.parser == PARSER_TERMINAL) {
        if (now >= s_transfer.restart_at_us) {
            esp_restart();
        }
        return;
    }
    if (s_transfer.public_state == P4_CONTENT_TRANSFER_RECEIVING &&
        now - s_transfer.last_activity_us > TRANSFER_TIMEOUT_US) {
        terminal(WIRE_STATUS_TIMEOUT, NULL);
        return;
    }
}

p4_content_transfer_info_t p4_content_transfer_info(void)
{
    uint8_t percent = 0U;
    if (s_transfer.expected_bytes != 0U) {
        const uint64_t scaled =
            (uint64_t)s_transfer.received_bytes * UINT64_C(100);
        percent = (uint8_t)(scaled / s_transfer.expected_bytes);
    }
    return (p4_content_transfer_info_t){
        .state = s_transfer.public_state,
        .received_bytes = s_transfer.received_bytes,
        .expected_bytes = s_transfer.expected_bytes,
        .progress_percent = percent,
        .last_status = s_transfer.last_status,
        .ready = s_transfer.initialized,
        .busy = s_transfer.public_state != P4_CONTENT_TRANSFER_IDLE,
    };
}
