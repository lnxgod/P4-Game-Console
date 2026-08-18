// SPDX-License-Identifier: MIT

#ifndef P4_GAME_RESOURCE_H
#define P4_GAME_RESOURCE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_GAME_RESOURCE_HEADER_BYTES = 128,
    P4_GAME_RESOURCE_SHA256_BYTES = 32,
    P4_GAME_RESOURCE_ID_BYTES = 48,
    P4_GAME_RESOURCE_MAX_BYTES = 8 * 1024 * 1024,
};

#define P4_GAME_RESOURCE_MAGIC "P4RES01\0"
#define P4_GAME_RESOURCE_FORMAT_VERSION UINT32_C(1)

typedef struct {
    uint32_t package_bytes;
    uint32_t payload_offset;
    uint32_t payload_bytes;
    uint32_t format_version;
    uint8_t payload_sha256[P4_GAME_RESOURCE_SHA256_BYTES];
    char game_id[P4_GAME_RESOURCE_ID_BYTES];
} p4_game_resource_info_t;

typedef enum {
    P4_GAME_RESOURCE_VALID = 0,
    P4_GAME_RESOURCE_BAD_ARGUMENT,
    P4_GAME_RESOURCE_BAD_SIZE,
    P4_GAME_RESOURCE_BAD_MAGIC,
    P4_GAME_RESOURCE_BAD_VERSION,
    P4_GAME_RESOURCE_BAD_LAYOUT,
    P4_GAME_RESOURCE_BAD_METADATA,
    P4_GAME_RESOURCE_BAD_DIGEST,
} p4_game_resource_result_t;

p4_game_resource_result_t p4_game_resource_parse(
    const uint8_t *data, size_t size_bytes,
    p4_game_resource_info_t *out_info);

const char *p4_game_resource_result_name(
    p4_game_resource_result_t result);

#ifdef __cplusplus
}
#endif

#endif
