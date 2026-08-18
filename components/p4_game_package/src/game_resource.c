// SPDX-License-Identifier: MIT

#include "p4/game_resource.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    HEADER_MAGIC = 0,
    HEADER_BYTES = 8,
    HEADER_PACKAGE_BYTES = 12,
    HEADER_PAYLOAD_OFFSET = 16,
    HEADER_PAYLOAD_BYTES = 20,
    HEADER_FORMAT_VERSION = 24,
    HEADER_FLAGS = 28,
    HEADER_SHA256 = 32,
    HEADER_GAME_ID = 64,
    HEADER_RESERVED = 112,
    HEADER_RESERVED_BYTES = 16,
};

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8U |
        (uint32_t)data[2] << 16U | (uint32_t)data[3] << 24U;
}

static bool all_zero(const uint8_t *data, size_t bytes)
{
    for (size_t index = 0U; index < bytes; ++index) {
        if (data[index] != 0U) {
            return false;
        }
    }
    return true;
}

static bool id_valid(const uint8_t *field)
{
    const void *const terminator = memchr(
        field, 0, P4_GAME_RESOURCE_ID_BYTES);
    if (terminator == NULL) {
        return false;
    }
    const size_t length = (size_t)((const uint8_t *)terminator - field);
    if (length < 3U || field[0] < 'a' || field[0] > 'z' ||
        !all_zero(field + length + 1U,
                  P4_GAME_RESOURCE_ID_BYTES - length - 1U)) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const uint8_t byte = field[index];
        if (!((byte >= 'a' && byte <= 'z') ||
              (byte >= '0' && byte <= '9') || byte == '.' ||
              byte == '-')) {
            return false;
        }
    }
    return true;
}

p4_game_resource_result_t p4_game_resource_parse(
    const uint8_t *data, size_t size_bytes,
    p4_game_resource_info_t *out_info)
{
    if (data == NULL || out_info == NULL) {
        return P4_GAME_RESOURCE_BAD_ARGUMENT;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (size_bytes <= P4_GAME_RESOURCE_HEADER_BYTES ||
        size_bytes > P4_GAME_RESOURCE_MAX_BYTES) {
        return P4_GAME_RESOURCE_BAD_SIZE;
    }
    if (memcmp(data + HEADER_MAGIC, P4_GAME_RESOURCE_MAGIC, 8U) != 0) {
        return P4_GAME_RESOURCE_BAD_MAGIC;
    }
    if (read_u32(data + HEADER_BYTES) != P4_GAME_RESOURCE_HEADER_BYTES ||
        read_u32(data + HEADER_FORMAT_VERSION) !=
            P4_GAME_RESOURCE_FORMAT_VERSION) {
        return P4_GAME_RESOURCE_BAD_VERSION;
    }
    out_info->package_bytes = read_u32(data + HEADER_PACKAGE_BYTES);
    out_info->payload_offset = read_u32(data + HEADER_PAYLOAD_OFFSET);
    out_info->payload_bytes = read_u32(data + HEADER_PAYLOAD_BYTES);
    out_info->format_version = read_u32(data + HEADER_FORMAT_VERSION);
    if (out_info->package_bytes != size_bytes ||
        out_info->payload_offset != P4_GAME_RESOURCE_HEADER_BYTES ||
        out_info->payload_bytes != size_bytes - out_info->payload_offset ||
        read_u32(data + HEADER_FLAGS) != 0U ||
        !all_zero(data + HEADER_RESERVED, HEADER_RESERVED_BYTES)) {
        return P4_GAME_RESOURCE_BAD_LAYOUT;
    }
    if (!id_valid(data + HEADER_GAME_ID)) {
        return P4_GAME_RESOURCE_BAD_METADATA;
    }
    memcpy(out_info->payload_sha256, data + HEADER_SHA256,
           sizeof(out_info->payload_sha256));
    memcpy(out_info->game_id, data + HEADER_GAME_ID,
           sizeof(out_info->game_id));
    return P4_GAME_RESOURCE_VALID;
}

const char *p4_game_resource_result_name(
    p4_game_resource_result_t result)
{
    switch (result) {
    case P4_GAME_RESOURCE_VALID: return "valid";
    case P4_GAME_RESOURCE_BAD_ARGUMENT: return "bad-argument";
    case P4_GAME_RESOURCE_BAD_SIZE: return "bad-size";
    case P4_GAME_RESOURCE_BAD_MAGIC: return "bad-magic";
    case P4_GAME_RESOURCE_BAD_VERSION: return "bad-version";
    case P4_GAME_RESOURCE_BAD_LAYOUT: return "bad-layout";
    case P4_GAME_RESOURCE_BAD_METADATA: return "bad-metadata";
    case P4_GAME_RESOURCE_BAD_DIGEST: return "bad-digest";
    default: return "unknown";
    }
}
