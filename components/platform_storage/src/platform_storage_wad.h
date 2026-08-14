#ifndef PLATFORM_STORAGE_WAD_H
#define PLATFORM_STORAGE_WAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PLATFORM_WAD_HEADER_OK = 0,
    PLATFORM_WAD_HEADER_TOO_SHORT,
    PLATFORM_WAD_HEADER_NOT_IWAD,
    PLATFORM_WAD_HEADER_BAD_DIRECTORY,
} platform_wad_header_status_t;

typedef struct {
    uint32_t lump_count;
    uint32_t directory_offset;
} platform_wad_header_t;

platform_wad_header_status_t platform_wad_parse_header(
    const uint8_t *bytes,
    size_t byte_count,
    uint64_t file_size,
    platform_wad_header_t *out_header
);

bool platform_wad_matches_doom_shareware_1_9(
    uint64_t size_bytes,
    const uint8_t sha256[32]
);

void platform_wad_sha256_hex(const uint8_t sha256[32], char out_hex[65]);

#endif
