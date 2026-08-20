// SPDX-License-Identifier: MIT

#include "p4/content_catalog.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "sha256.h"

enum {
    CART_HEADER_BYTES = 128,
    CART_ENTRY_BYTES = 112,
    CART_MAX_ENTRIES = 64,
    CART_MAX_MANIFEST_BYTES = 16384,
    CART_MAX_PAYLOAD_BYTES = 2 * 1024 * 1024,
    IO_BUFFER_BYTES = 4096,
};

typedef struct {
    uint32_t offset;
    uint32_t length;
    uint8_t sha256[32];
} cart_payload_t;

static const uint8_t QUAKE_SHAREWARE_SHA256[32] = {
    0x35, 0xa9, 0xc5, 0x5e, 0x5e, 0x5a, 0x28, 0x4a,
    0x15, 0x9a, 0xd2, 0xa6, 0x2e, 0x0e, 0x8d, 0xef,
    0x23, 0xd8, 0x29, 0x56, 0x1f, 0xe2, 0xf5, 0x4e,
    0xb4, 0x02, 0xdb, 0xc0, 0xa9, 0xa9, 0x46, 0xaf,
};

static uint16_t read_u16(const uint8_t bytes[2])
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_u32(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static uint32_t align4(uint32_t value)
{
    return (value + 3U) & ~UINT32_C(3);
}

static bool bytes_are_zero(const uint8_t *bytes, size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        if (bytes[index] != 0U) {
            return false;
        }
    }
    return true;
}

static bool copy_text(char *output, size_t capacity, const char *text)
{
    if (output == NULL || capacity == 0U || text == NULL) {
        return false;
    }
    const size_t length = strlen(text);
    if (length >= capacity) {
        return false;
    }
    memcpy(output, text, length + 1U);
    return true;
}

static bool join_path(
    char output[P4_CONTENT_PATH_BYTES],
    const char *left,
    const char *right)
{
    if (output == NULL || left == NULL || right == NULL) {
        return false;
    }
    const size_t left_length = strlen(left);
    const size_t right_length = strlen(right);
    const bool needs_slash = left_length != 0U && left[left_length - 1U] != '/';
    const size_t total = left_length + (needs_slash ? 1U : 0U) + right_length;
    if (total >= P4_CONTENT_PATH_BYTES) {
        return false;
    }
    memcpy(output, left, left_length);
    size_t offset = left_length;
    if (needs_slash) {
        output[offset++] = '/';
    }
    memcpy(&output[offset], right, right_length + 1U);
    return true;
}

static bool seek_file(FILE *file, uint32_t offset)
{
    return file != NULL && fseek(file, (long)offset, SEEK_SET) == 0;
}

static bool read_exact(FILE *file, uint8_t *bytes, size_t length)
{
    return length == 0U || fread(bytes, 1U, length, file) == length;
}

static p4_content_status_t file_size(
    const char *path,
    uint64_t *size_out)
{
    struct stat metadata;
    if (stat(path, &metadata) != 0) {
        return errno == ENOENT ? P4_CONTENT_NOT_FOUND : P4_CONTENT_IO_ERROR;
    }
    if (!S_ISREG(metadata.st_mode)) {
        return P4_CONTENT_NOT_REGULAR;
    }
    if (metadata.st_size < 0) {
        return P4_CONTENT_BAD_FORMAT;
    }
    *size_out = (uint64_t)metadata.st_size;
    return P4_CONTENT_OK;
}

static p4_content_status_t hash_range(
    FILE *file,
    uint32_t offset,
    uint32_t length,
    uint8_t digest[32])
{
    if (!seek_file(file, offset)) {
        return P4_CONTENT_IO_ERROR;
    }
    p4_sha256_t hash;
    p4_sha256_init(&hash);
    uint8_t buffer[IO_BUFFER_BYTES];
    uint32_t remaining = length;
    while (remaining != 0U) {
        size_t count = sizeof(buffer);
        if ((uint32_t)count > remaining) {
            count = remaining;
        }
        if (!read_exact(file, buffer, count)) {
            return P4_CONTENT_IO_ERROR;
        }
        p4_sha256_update(&hash, buffer, count);
        remaining -= (uint32_t)count;
    }
    p4_sha256_finish(&hash, digest);
    return P4_CONTENT_OK;
}

static p4_content_status_t hash_cart(
    FILE *file,
    uint32_t total_size,
    uint8_t digest[32])
{
    if (!seek_file(file, 0U)) {
        return P4_CONTENT_IO_ERROR;
    }
    uint8_t buffer[IO_BUFFER_BYTES];
    uint32_t offset = 0U;
    p4_sha256_t hash;
    p4_sha256_init(&hash);
    while (offset < total_size) {
        size_t count = sizeof(buffer);
        if ((uint32_t)count > total_size - offset) {
            count = total_size - offset;
        }
        if (!read_exact(file, buffer, count)) {
            return P4_CONTENT_IO_ERROR;
        }
        for (size_t index = 0U; index < count; ++index) {
            const uint32_t absolute = offset + (uint32_t)index;
            if (absolute >= 36U && absolute < 68U) {
                buffer[index] = 0U;
            }
        }
        p4_sha256_update(&hash, buffer, count);
        offset += (uint32_t)count;
    }
    p4_sha256_finish(&hash, digest);
    return P4_CONTENT_OK;
}

static bool range_is_zero(FILE *file, uint32_t offset, uint32_t length)
{
    if (!seek_file(file, offset)) {
        return false;
    }
    uint8_t buffer[256];
    uint32_t remaining = length;
    while (remaining != 0U) {
        size_t count = sizeof(buffer);
        if ((uint32_t)count > remaining) {
            count = remaining;
        }
        if (!read_exact(file, buffer, count) || !bytes_are_zero(buffer, count)) {
            return false;
        }
        remaining -= (uint32_t)count;
    }
    return true;
}

static bool range_contains(
    FILE *file,
    uint32_t offset,
    uint32_t length,
    const char *token)
{
    const size_t token_length = strlen(token);
    if (token_length == 0U || !seek_file(file, offset)) {
        return false;
    }
    size_t matched = 0U;
    for (uint32_t index = 0U; index < length; ++index) {
        const int value = fgetc(file);
        if (value == EOF) {
            return false;
        }
        const char character = (char)value;
        if (character == token[matched]) {
            ++matched;
            if (matched == token_length) {
                return true;
            }
        } else {
            matched = character == token[0] ? 1U : 0U;
        }
    }
    return false;
}

static bool manifest_markers_valid(
    FILE *file,
    uint32_t offset,
    uint32_t length)
{
    if (length == 0U || !seek_file(file, offset)) {
        return false;
    }
    const int first = fgetc(file);
    if (first != '{' || !seek_file(file, offset + length - 1U) ||
        fgetc(file) != '\n') {
        return false;
    }
    static const char *const required[] = {
        "\"format\":\"p4-cart-source-v1\"",
        "\"source_included\":true",
        "\"id\":\"p4-lua-5.4-v1\"",
        "\"logical_height\":480",
        "\"logical_width\":768",
        "\"present_hz\":30",
        "\"update_hz\":60",
    };
    for (size_t index = 0U; index < sizeof(required) / sizeof(required[0]); ++index) {
        if (!range_contains(file, offset, length, required[index])) {
            return false;
        }
    }
    return true;
}

static bool manifest_unique_token(
    const uint8_t *manifest,
    size_t manifest_bytes,
    const char *token,
    size_t *value_offset_out)
{
    const size_t token_bytes = strlen(token);
    size_t found = SIZE_MAX;
    if (manifest == NULL || token_bytes == 0U ||
        token_bytes > manifest_bytes || value_offset_out == NULL) {
        return false;
    }
    for (size_t offset = 0U; offset <= manifest_bytes - token_bytes; ++offset) {
        if (memcmp(&manifest[offset], token, token_bytes) == 0) {
            if (found != SIZE_MAX) {
                return false;
            }
            found = offset + token_bytes;
        }
    }
    if (found == SIZE_MAX) {
        return false;
    }
    *value_offset_out = found;
    return true;
}

static bool manifest_ascii_string(
    const uint8_t *manifest,
    size_t manifest_bytes,
    const char *token,
    char *output,
    size_t output_capacity)
{
    size_t offset = 0U;
    if (!manifest_unique_token(
            manifest, manifest_bytes, token, &offset) ||
        output == NULL || output_capacity == 0U) {
        return false;
    }
    size_t output_bytes = 0U;
    while (offset < manifest_bytes && manifest[offset] != (uint8_t)'"') {
        const uint8_t byte = manifest[offset++];
        if (byte < 0x21U || byte > 0x7eU || byte == (uint8_t)'\\' ||
            output_bytes + 1U >= output_capacity) {
            return false;
        }
        output[output_bytes++] = (char)byte;
    }
    if (offset >= manifest_bytes || output_bytes == 0U) {
        return false;
    }
    output[output_bytes] = '\0';
    return true;
}

static bool manifest_u32(
    const uint8_t *manifest,
    size_t manifest_bytes,
    const char *token,
    uint32_t *value_out)
{
    size_t offset = 0U;
    uint32_t value = 0U;
    size_t digits = 0U;
    if (!manifest_unique_token(
            manifest, manifest_bytes, token, &offset) || value_out == NULL) {
        return false;
    }
    while (offset < manifest_bytes &&
           manifest[offset] >= (uint8_t)'0' &&
           manifest[offset] <= (uint8_t)'9') {
        const uint32_t digit = (uint32_t)(manifest[offset] - (uint8_t)'0');
        if (value > (UINT32_MAX - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
        ++offset;
        ++digits;
    }
    if (digits == 0U || offset >= manifest_bytes ||
        (manifest[offset] != (uint8_t)',' &&
         manifest[offset] != (uint8_t)'}')) {
        return false;
    }
    *value_out = value;
    return true;
}

static bool canonical_entry_path(const uint8_t bytes[64], char output[64])
{
    size_t length = 0U;
    while (length < 64U && bytes[length] != 0U) {
        const uint8_t character = bytes[length];
        if (character < 0x21U || character > 0x7eU || character == '\\') {
            return false;
        }
        output[length] = (char)character;
        ++length;
    }
    if (length == 0U || length == 64U || bytes[0] == '/' ||
        !bytes_are_zero(&bytes[length + 1U], 64U - length - 1U)) {
        return false;
    }
    output[length] = '\0';
    size_t segment_start = 0U;
    for (size_t index = 0U; index <= length; ++index) {
        if (output[index] != '/' && output[index] != '\0') {
            continue;
        }
        const size_t segment_length = index - segment_start;
        if (segment_length == 0U ||
            (segment_length == 1U && output[segment_start] == '.') ||
            (segment_length == 2U && output[segment_start] == '.' &&
             output[segment_start + 1U] == '.')) {
            return false;
        }
        segment_start = index + 1U;
    }
    return true;
}

static bool has_cart_extension(const char *name)
{
    const size_t length = strlen(name);
    static const char extension[] = ".p4cart";
    if (length <= sizeof(extension) - 1U) {
        return false;
    }
    const char *const suffix = &name[length - (sizeof(extension) - 1U)];
    for (size_t index = 0U; index < sizeof(extension) - 1U; ++index) {
        if ((char)tolower((unsigned char)suffix[index]) != extension[index]) {
            return false;
        }
    }
    return true;
}

static void initialize_item(p4_content_item_t *item, const char *path)
{
    memset(item, 0, sizeof(*item));
    item->status = P4_CONTENT_INVALID_ARGUMENT;
    if (path == NULL || !copy_text(item->path, sizeof(item->path), path)) {
        return;
    }
    const char *name = strrchr(path, '/');
    name = name == NULL ? path : name + 1;
    (void)copy_text(item->name, sizeof(item->name), name);
}

p4_content_status_t p4_content_validate_cart_file(
    const char *path,
    p4_content_item_t *item_out)
{
    if (path == NULL || item_out == NULL) {
        return P4_CONTENT_INVALID_ARGUMENT;
    }
    initialize_item(item_out, path);
    if (item_out->path[0] == '\0' || item_out->name[0] == '\0') {
        return item_out->status;
    }
    p4_content_status_t status = file_size(path, &item_out->size_bytes);
    if (status != P4_CONTENT_OK) {
        item_out->status = status;
        return status;
    }
    if (item_out->size_bytes < CART_HEADER_BYTES ||
        item_out->size_bytes > P4_CONTENT_CART_MAX_BYTES) {
        status = item_out->size_bytes > P4_CONTENT_CART_MAX_BYTES
            ? P4_CONTENT_TOO_LARGE : P4_CONTENT_BAD_FORMAT;
        item_out->status = status;
        return status;
    }

    FILE *const file = fopen(path, "rb");
    if (file == NULL) {
        item_out->status = P4_CONTENT_IO_ERROR;
        return item_out->status;
    }
    uint8_t header[CART_HEADER_BYTES];
    if (!read_exact(file, header, sizeof(header))) {
        status = P4_CONTENT_IO_ERROR;
        goto done;
    }
    const uint32_t total_size = read_u32(&header[16]);
    const uint32_t manifest_offset = read_u32(&header[20]);
    const uint32_t manifest_size = read_u32(&header[24]);
    const uint16_t entry_count = read_u16(&header[28]);
    const uint16_t entry_size = read_u16(&header[30]);
    const uint32_t payload_offset = read_u32(&header[32]);
    if (memcmp(header, "P4CART1\0", 8U) != 0 ||
        read_u16(&header[8]) != CART_HEADER_BYTES ||
        read_u16(&header[10]) != 1U || read_u32(&header[12]) != 0U ||
        total_size != item_out->size_bytes || manifest_offset != CART_HEADER_BYTES ||
        manifest_size == 0U || manifest_size > CART_MAX_MANIFEST_BYTES ||
        entry_count == 0U || entry_count > CART_MAX_ENTRIES ||
        entry_size != CART_ENTRY_BYTES || !bytes_are_zero(&header[68], 60U)) {
        status = P4_CONTENT_BAD_FORMAT;
        goto done;
    }
    if (manifest_size > total_size - manifest_offset) {
        status = P4_CONTENT_BAD_FORMAT;
        goto done;
    }
    const uint32_t table_offset = align4(manifest_offset + manifest_size);
    const uint32_t table_bytes = (uint32_t)entry_count * CART_ENTRY_BYTES;
    if (table_offset > total_size || table_bytes > total_size - table_offset) {
        status = P4_CONTENT_BAD_FORMAT;
        goto done;
    }
    const uint32_t expected_payload_offset = align4(table_offset + table_bytes);
    if (payload_offset != expected_payload_offset || payload_offset > total_size ||
        !range_is_zero(file, manifest_offset + manifest_size,
                       table_offset - manifest_offset - manifest_size) ||
        !range_is_zero(file, table_offset + table_bytes,
                       payload_offset - table_offset - table_bytes) ||
        !manifest_markers_valid(file, manifest_offset, manifest_size)) {
        status = P4_CONTENT_BAD_FORMAT;
        goto done;
    }

    cart_payload_t payloads[CART_MAX_ENTRIES];
    char previous_path[64] = {0};
    bool runtime_source_found = false;
    bool readme_found = false;
    bool license_found = false;
    uint32_t previous_end = payload_offset;
    for (uint16_t index = 0U; index < entry_count; ++index) {
        uint8_t entry[CART_ENTRY_BYTES];
        if (!seek_file(file, table_offset + (uint32_t)index * CART_ENTRY_BYTES) ||
            !read_exact(file, entry, sizeof(entry))) {
            status = P4_CONTENT_IO_ERROR;
            goto done;
        }
        char entry_path[64];
        const uint8_t kind = entry[64];
        const uint32_t offset = read_u32(&entry[68]);
        const uint32_t length = read_u32(&entry[72]);
        if (!canonical_entry_path(entry, entry_path) ||
            (index != 0U && strcmp(previous_path, entry_path) >= 0) ||
            kind < 1U || kind > 5U || entry[65] != 0U ||
            !bytes_are_zero(&entry[66], 2U) ||
            !bytes_are_zero(&entry[108], 4U) || length == 0U ||
            length > CART_MAX_PAYLOAD_BYTES || (offset & 3U) != 0U ||
            offset < previous_end || offset > total_size ||
            length > total_size - offset ||
            !range_is_zero(file, previous_end, offset - previous_end)) {
            status = P4_CONTENT_BAD_FORMAT;
            goto done;
        }
        (void)copy_text(previous_path, sizeof(previous_path), entry_path);
        payloads[index].offset = offset;
        payloads[index].length = length;
        memcpy(payloads[index].sha256, &entry[76], 32U);
        previous_end = offset + length;
        runtime_source_found = runtime_source_found || kind == 1U;
        readme_found = readme_found ||
            (kind == 4U && strcmp(entry_path, "README.md") == 0);
        license_found = license_found || kind == 5U;
    }
    if (previous_end != total_size || !runtime_source_found || !readme_found ||
        !license_found) {
        status = P4_CONTENT_BAD_FORMAT;
        goto done;
    }

    status = hash_cart(file, total_size, item_out->sha256);
    if (status != P4_CONTENT_OK) {
        goto done;
    }
    if (memcmp(item_out->sha256, &header[36], 32U) != 0) {
        status = P4_CONTENT_BAD_HASH;
        goto done;
    }
    for (uint16_t index = 0U; index < entry_count; ++index) {
        uint8_t payload_hash[32];
        status = hash_range(
            file, payloads[index].offset, payloads[index].length, payload_hash);
        if (status != P4_CONTENT_OK) {
            goto done;
        }
        if (memcmp(payload_hash, payloads[index].sha256, sizeof(payload_hash)) != 0) {
            status = P4_CONTENT_BAD_HASH;
            goto done;
        }
    }
    status = P4_CONTENT_OK;

done:
    if (fclose(file) != 0 && status == P4_CONTENT_OK) {
        status = P4_CONTENT_IO_ERROR;
    }
    item_out->status = status;
    return status;
}

p4_content_status_t p4_content_load_cart_source(
    const char *path,
    uint8_t *source_out,
    size_t source_capacity,
    p4_content_cart_runtime_t *runtime_out)
{
    if (path == NULL || source_out == NULL || source_capacity == 0U ||
        runtime_out == NULL) {
        return P4_CONTENT_INVALID_ARGUMENT;
    }
    memset(runtime_out, 0, sizeof(*runtime_out));
    p4_content_item_t item;
    p4_content_status_t status = p4_content_validate_cart_file(path, &item);
    if (status != P4_CONTENT_OK) {
        return status;
    }

    FILE *const file = fopen(path, "rb");
    if (file == NULL) {
        return P4_CONTENT_IO_ERROR;
    }
    uint8_t header[CART_HEADER_BYTES];
    uint8_t *manifest = NULL;
    uint8_t selected_hash[32] = {0};
    uint32_t selected_offset = 0U;
    uint32_t selected_length = 0U;
    bool selected = false;
    if (!read_exact(file, header, sizeof(header)) ||
        memcmp(header, "P4CART1\0", 8U) != 0 ||
        memcmp(&header[36], item.sha256, sizeof(item.sha256)) != 0) {
        status = P4_CONTENT_IO_ERROR;
        goto done_loading;
    }
    const uint32_t manifest_offset = read_u32(&header[20]);
    const uint32_t manifest_size = read_u32(&header[24]);
    const uint16_t entry_count = read_u16(&header[28]);
    if (manifest_offset != CART_HEADER_BYTES || manifest_size == 0U ||
        manifest_size > CART_MAX_MANIFEST_BYTES || entry_count == 0U ||
        entry_count > CART_MAX_ENTRIES) {
        status = P4_CONTENT_BAD_FORMAT;
        goto done_loading;
    }
    manifest = malloc((size_t)manifest_size);
    if (manifest == NULL || !seek_file(file, manifest_offset) ||
        !read_exact(file, manifest, manifest_size)) {
        status = manifest == NULL
            ? P4_CONTENT_LIMIT_REACHED : P4_CONTENT_IO_ERROR;
        goto done_loading;
    }
    if (!manifest_ascii_string(
            manifest, manifest_size, "\"entry\":\"",
            runtime_out->entry_path, sizeof(runtime_out->entry_path)) ||
        !manifest_u32(
            manifest, manifest_size, "\"heap_bytes\":",
            &runtime_out->heap_bytes) ||
        !manifest_u32(
            manifest, manifest_size, "\"save_bytes\":",
            &runtime_out->save_bytes) ||
        runtime_out->heap_bytes < 64U * 1024U ||
        runtime_out->heap_bytes > 512U * 1024U ||
        (runtime_out->heap_bytes % 4096U) != 0U ||
        runtime_out->save_bytes > 4U * 1024U) {
        status = P4_CONTENT_BAD_FORMAT;
        goto done_loading;
    }

    const uint32_t table_offset = align4(manifest_offset + manifest_size);
    for (uint16_t index = 0U; index < entry_count; ++index) {
        uint8_t entry[CART_ENTRY_BYTES];
        char entry_path[P4_CONTENT_CART_ENTRY_PATH_BYTES];
        if (!seek_file(file, table_offset + (uint32_t)index * CART_ENTRY_BYTES) ||
            !read_exact(file, entry, sizeof(entry))) {
            status = P4_CONTENT_IO_ERROR;
            goto done_loading;
        }
        if (!canonical_entry_path(entry, entry_path)) {
            status = P4_CONTENT_BAD_FORMAT;
            goto done_loading;
        }
        if (strcmp(entry_path, runtime_out->entry_path) == 0) {
            if (selected || entry[64] != 1U) {
                status = P4_CONTENT_BAD_FORMAT;
                goto done_loading;
            }
            selected_offset = read_u32(&entry[68]);
            selected_length = read_u32(&entry[72]);
            memcpy(selected_hash, &entry[76], sizeof(selected_hash));
            selected = true;
        }
    }
    if (!selected || selected_length == 0U ||
        selected_length > P4_CONTENT_CART_SOURCE_MAX_BYTES ||
        selected_length > source_capacity) {
        status = selected && selected_length > source_capacity
            ? P4_CONTENT_LIMIT_REACHED : P4_CONTENT_BAD_FORMAT;
        goto done_loading;
    }
    if (!seek_file(file, selected_offset) ||
        !read_exact(file, source_out, selected_length)) {
        status = P4_CONTENT_IO_ERROR;
        goto done_loading;
    }
    uint8_t source_hash[32];
    p4_sha256_t source_hasher;
    p4_sha256_init(&source_hasher);
    p4_sha256_update(&source_hasher, source_out, selected_length);
    p4_sha256_finish(&source_hasher, source_hash);
    if (memcmp(source_hash, selected_hash, sizeof(source_hash)) != 0) {
        status = P4_CONTENT_BAD_HASH;
        goto done_loading;
    }
    runtime_out->source_bytes = selected_length;
    memcpy(runtime_out->cart_sha256, item.sha256, sizeof(item.sha256));
    status = P4_CONTENT_OK;

done_loading:
    free(manifest);
    if (fclose(file) != 0 && status == P4_CONTENT_OK) {
        status = P4_CONTENT_IO_ERROR;
    }
    if (status != P4_CONTENT_OK) {
        memset(runtime_out, 0, sizeof(*runtime_out));
    }
    return status;
}

p4_content_status_t p4_content_validate_quake_shareware(
    const char *path,
    p4_content_item_t *item_out)
{
    if (path == NULL || item_out == NULL) {
        return P4_CONTENT_INVALID_ARGUMENT;
    }
    initialize_item(item_out, path);
    p4_content_status_t status = file_size(path, &item_out->size_bytes);
    if (status != P4_CONTENT_OK) {
        item_out->status = status;
        return status;
    }
    if (item_out->size_bytes != P4_CONTENT_QUAKE_SHAREWARE_BYTES) {
        item_out->status = P4_CONTENT_BAD_FORMAT;
        return item_out->status;
    }
    FILE *const file = fopen(path, "rb");
    if (file == NULL) {
        item_out->status = P4_CONTENT_IO_ERROR;
        return item_out->status;
    }
    status = hash_range(
        file, 0U, P4_CONTENT_QUAKE_SHAREWARE_BYTES, item_out->sha256);
    if (fclose(file) != 0 && status == P4_CONTENT_OK) {
        status = P4_CONTENT_IO_ERROR;
    }
    if (status == P4_CONTENT_OK &&
        memcmp(item_out->sha256, QUAKE_SHAREWARE_SHA256, 32U) != 0) {
        status = P4_CONTENT_BAD_HASH;
    }
    item_out->status = status;
    return status;
}

static void sort_carts(p4_content_catalog_t *catalog)
{
    for (size_t index = 1U; index < catalog->valid_cart_count; ++index) {
        p4_content_item_t item = catalog->carts[index];
        size_t destination = index;
        while (destination > 0U &&
               strcmp(catalog->carts[destination - 1U].name, item.name) > 0) {
            catalog->carts[destination] = catalog->carts[destination - 1U];
            --destination;
        }
        catalog->carts[destination] = item;
    }
}

p4_content_status_t p4_content_catalog_scan(
    const char *storage_root,
    p4_content_catalog_t *catalog_out)
{
    if (storage_root == NULL || catalog_out == NULL) {
        return P4_CONTENT_INVALID_ARGUMENT;
    }
    memset(catalog_out, 0, sizeof(*catalog_out));
    struct stat root_metadata;
    if (stat(storage_root, &root_metadata) != 0) {
        return errno == ENOENT ? P4_CONTENT_NOT_FOUND : P4_CONTENT_IO_ERROR;
    }
    if (!S_ISDIR(root_metadata.st_mode)) {
        return P4_CONTENT_NOT_REGULAR;
    }
    catalog_out->storage_available = true;

    char game_directory[P4_CONTENT_PATH_BYTES];
    if (!join_path(game_directory, storage_root, P4_CONTENT_CART_DIRECTORY)) {
        return P4_CONTENT_LIMIT_REACHED;
    }
    DIR *const directory = opendir(game_directory);
    if (directory == NULL) {
        return errno == ENOENT ? P4_CONTENT_OK : P4_CONTENT_IO_ERROR;
    }
    p4_content_status_t result = P4_CONTENT_OK;
    for (;;) {
        errno = 0;
        const struct dirent *const entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) {
                result = P4_CONTENT_IO_ERROR;
            }
            break;
        }
        if (!has_cart_extension(entry->d_name)) {
            continue;
        }
        if (catalog_out->candidates_seen >=
            P4_CONTENT_MAX_DIRECTORY_CANDIDATES) {
            catalog_out->directory_truncated = true;
            break;
        }
        ++catalog_out->candidates_seen;
        char cart_path[P4_CONTENT_PATH_BYTES];
        if (!join_path(cart_path, game_directory, entry->d_name)) {
            ++catalog_out->invalid_cart_count;
            continue;
        }
        p4_content_item_t item;
        const p4_content_status_t status =
            p4_content_validate_cart_file(cart_path, &item);
        if (status != P4_CONTENT_OK) {
            ++catalog_out->invalid_cart_count;
            continue;
        }
        if (catalog_out->valid_cart_count >= P4_CONTENT_MAX_CARTS) {
            catalog_out->directory_truncated = true;
            continue;
        }
        catalog_out->carts[catalog_out->valid_cart_count++] = item;
    }
    if (closedir(directory) != 0 && result == P4_CONTENT_OK) {
        result = P4_CONTENT_IO_ERROR;
    }
    sort_carts(catalog_out);
    return result;
}

const char *p4_content_status_name(p4_content_status_t status)
{
    switch (status) {
        case P4_CONTENT_OK: return "ok";
        case P4_CONTENT_INVALID_ARGUMENT: return "invalid-argument";
        case P4_CONTENT_NOT_FOUND: return "not-found";
        case P4_CONTENT_IO_ERROR: return "io-error";
        case P4_CONTENT_NOT_REGULAR: return "not-regular";
        case P4_CONTENT_TOO_LARGE: return "too-large";
        case P4_CONTENT_BAD_FORMAT: return "bad-format";
        case P4_CONTENT_BAD_HASH: return "bad-hash";
        case P4_CONTENT_LIMIT_REACHED: return "limit-reached";
        default: return "unknown";
    }
}

void p4_content_sha256_hex(const uint8_t digest[32], char output[65])
{
    static const char HEX[] = "0123456789abcdef";
    if (digest == NULL || output == NULL) {
        return;
    }
    for (size_t index = 0U; index < 32U; ++index) {
        output[index * 2U] = HEX[digest[index] >> 4U];
        output[index * 2U + 1U] = HEX[digest[index] & 0x0fU];
    }
    output[64] = '\0';
}
