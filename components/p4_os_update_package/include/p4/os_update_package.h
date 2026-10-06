// SPDX-License-Identifier: MIT

#ifndef P4_OS_UPDATE_PACKAGE_H
#define P4_OS_UPDATE_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Existing boards retain their legacy target; new ports can bind packages. */
#ifndef P4_OS_UPDATE_ACCEPTED_TARGET
#define P4_OS_UPDATE_ACCEPTED_TARGET "esp32p4"
#endif

#ifndef P4_OS_UPDATE_SLOT_BYTES
#define P4_OS_UPDATE_SLOT_BYTES 0x370000
#endif

#define P4_OS_UPDATE_MAGIC "P4OSUP1\0"

enum {
    P4_OS_UPDATE_HEADER_BYTES = 256,
    P4_OS_UPDATE_SHA256_BYTES = 32,
    P4_OS_UPDATE_VERSION_BYTES = 32,
    P4_OS_UPDATE_BUILD_BYTES = 64,
    P4_OS_UPDATE_TARGET_BYTES = 16,
    P4_OS_UPDATE_MAX_IMAGE_BYTES = P4_OS_UPDATE_SLOT_BYTES,
    P4_OS_UPDATE_MAX_PACKAGE_BYTES =
        P4_OS_UPDATE_HEADER_BYTES + P4_OS_UPDATE_MAX_IMAGE_BYTES,
};

typedef struct {
    uint32_t package_bytes;
    uint32_t payload_offset;
    uint32_t payload_bytes;
    uint8_t payload_sha256[P4_OS_UPDATE_SHA256_BYTES];
    char version[P4_OS_UPDATE_VERSION_BYTES];
    char build[P4_OS_UPDATE_BUILD_BYTES];
    char target[P4_OS_UPDATE_TARGET_BYTES];
} p4_os_update_package_info_t;

typedef enum {
    P4_OS_UPDATE_PACKAGE_VALID = 0,
    P4_OS_UPDATE_PACKAGE_BAD_ARGUMENT,
    P4_OS_UPDATE_PACKAGE_BAD_SIZE,
    P4_OS_UPDATE_PACKAGE_BAD_MAGIC,
    P4_OS_UPDATE_PACKAGE_BAD_VERSION,
    P4_OS_UPDATE_PACKAGE_BAD_LAYOUT,
    P4_OS_UPDATE_PACKAGE_BAD_METADATA,
    P4_OS_UPDATE_PACKAGE_BAD_IMAGE,
} p4_os_update_package_result_t;

p4_os_update_package_result_t p4_os_update_package_parse(
    const uint8_t *data, size_t size_bytes,
    p4_os_update_package_info_t *out_info);

const char *p4_os_update_package_result_name(
    p4_os_update_package_result_t result);

#ifdef __cplusplus
}
#endif

#endif
