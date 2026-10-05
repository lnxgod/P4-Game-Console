// SPDX-License-Identifier: MIT

#include "p4/os_update_package.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    UPDATE_MAGIC = 0,
    UPDATE_HEADER_BYTES = 8,
    UPDATE_PACKAGE_BYTES = 12,
    UPDATE_PAYLOAD_OFFSET = 16,
    UPDATE_PAYLOAD_BYTES = 20,
    UPDATE_FORMAT_VERSION = 24,
    UPDATE_FLAGS = 28,
    UPDATE_SHA256 = 32,
    UPDATE_VERSION_TEXT = 64,
    UPDATE_BUILD_TEXT = 96,
    UPDATE_TARGET_TEXT = 160,
    UPDATE_RESERVED = 176,
    UPDATE_RESERVED_BYTES = 80,
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

static bool copy_text(char *destination, size_t bytes,
                      const uint8_t *source)
{
    const void *const terminator = memchr(source, 0, bytes);
    if (destination == NULL || terminator == NULL) {
        return false;
    }
    const size_t length = (size_t)((const uint8_t *)terminator - source);
    if (length == 0U ||
        !all_zero(source + length + 1U, bytes - length - 1U)) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        if (source[index] < UINT8_C(0x20) ||
            source[index] > UINT8_C(0x7e)) {
            return false;
        }
    }
    memcpy(destination, source, bytes);
    return true;
}

p4_os_update_package_result_t p4_os_update_package_parse(
    const uint8_t *data, size_t size_bytes,
    p4_os_update_package_info_t *out_info)
{
    if (data == NULL || out_info == NULL) {
        return P4_OS_UPDATE_PACKAGE_BAD_ARGUMENT;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (size_bytes < P4_OS_UPDATE_HEADER_BYTES ||
        size_bytes > P4_OS_UPDATE_MAX_PACKAGE_BYTES) {
        return P4_OS_UPDATE_PACKAGE_BAD_SIZE;
    }
    if (memcmp(data + UPDATE_MAGIC, P4_OS_UPDATE_MAGIC, 8U) != 0) {
        return P4_OS_UPDATE_PACKAGE_BAD_MAGIC;
    }
    if (read_u32(data + UPDATE_HEADER_BYTES) !=
            P4_OS_UPDATE_HEADER_BYTES ||
        read_u32(data + UPDATE_FORMAT_VERSION) != 1U ||
        read_u32(data + UPDATE_FLAGS) != 0U) {
        return P4_OS_UPDATE_PACKAGE_BAD_VERSION;
    }
    out_info->package_bytes = read_u32(data + UPDATE_PACKAGE_BYTES);
    out_info->payload_offset = read_u32(data + UPDATE_PAYLOAD_OFFSET);
    out_info->payload_bytes = read_u32(data + UPDATE_PAYLOAD_BYTES);
    if (out_info->package_bytes != size_bytes ||
        out_info->payload_offset != P4_OS_UPDATE_HEADER_BYTES ||
        out_info->payload_bytes == 0U ||
        out_info->payload_bytes > P4_OS_UPDATE_MAX_IMAGE_BYTES ||
        out_info->payload_bytes !=
            out_info->package_bytes - out_info->payload_offset ||
        !all_zero(data + UPDATE_RESERVED, UPDATE_RESERVED_BYTES)) {
        return P4_OS_UPDATE_PACKAGE_BAD_LAYOUT;
    }
    memcpy(out_info->payload_sha256, data + UPDATE_SHA256,
           sizeof(out_info->payload_sha256));
    if (!copy_text(out_info->version, sizeof(out_info->version),
                   data + UPDATE_VERSION_TEXT) ||
        !copy_text(out_info->build, sizeof(out_info->build),
                   data + UPDATE_BUILD_TEXT) ||
        !copy_text(out_info->target, sizeof(out_info->target),
                   data + UPDATE_TARGET_TEXT) ||
        strcmp(out_info->target, P4_OS_UPDATE_ACCEPTED_TARGET) != 0) {
        return P4_OS_UPDATE_PACKAGE_BAD_METADATA;
    }
    if (data[out_info->payload_offset] != UINT8_C(0xe9)) {
        return P4_OS_UPDATE_PACKAGE_BAD_IMAGE;
    }
    return P4_OS_UPDATE_PACKAGE_VALID;
}

const char *p4_os_update_package_result_name(
    p4_os_update_package_result_t result)
{
    switch (result) {
    case P4_OS_UPDATE_PACKAGE_VALID: return "valid";
    case P4_OS_UPDATE_PACKAGE_BAD_ARGUMENT: return "bad-argument";
    case P4_OS_UPDATE_PACKAGE_BAD_SIZE: return "bad-size";
    case P4_OS_UPDATE_PACKAGE_BAD_MAGIC: return "bad-magic";
    case P4_OS_UPDATE_PACKAGE_BAD_VERSION: return "bad-version";
    case P4_OS_UPDATE_PACKAGE_BAD_LAYOUT: return "bad-layout";
    case P4_OS_UPDATE_PACKAGE_BAD_METADATA: return "bad-metadata";
    case P4_OS_UPDATE_PACKAGE_BAD_IMAGE: return "bad-image";
    default: return "unknown";
    }
}
