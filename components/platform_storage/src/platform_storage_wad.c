#include "platform_storage_wad.h"

#include <string.h>

#define DOOM_SHAREWARE_1_9_SIZE UINT64_C(4196020)

static const uint8_t s_doom_shareware_1_9_sha256[32] = {
    0x1d, 0x7d, 0x43, 0xbe, 0x50, 0x1e, 0x67, 0xd9,
    0x27, 0xe4, 0x15, 0xe0, 0xb8, 0xf3, 0xe2, 0x9c,
    0x3b, 0xf3, 0x30, 0x75, 0xe8, 0x59, 0x72, 0x18,
    0x16, 0xf6, 0x52, 0xa5, 0x26, 0xca, 0xc7, 0x71,
};

static uint32_t read_u32_le(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

platform_wad_header_status_t platform_wad_parse_header(
    const uint8_t *bytes,
    size_t byte_count,
    uint64_t file_size,
    platform_wad_header_t *out_header
)
{
    if (bytes == NULL || out_header == NULL || byte_count < 12U || file_size < 12U) {
        return PLATFORM_WAD_HEADER_TOO_SHORT;
    }
    if (memcmp(bytes, "IWAD", 4U) != 0) {
        return PLATFORM_WAD_HEADER_NOT_IWAD;
    }

    const uint32_t lump_count = read_u32_le(&bytes[4]);
    const uint32_t directory_offset = read_u32_le(&bytes[8]);
    const uint64_t directory_size = (uint64_t)lump_count * UINT64_C(16);
    if (lump_count == 0U
        || (uint64_t)directory_offset > file_size
        || directory_size > file_size - (uint64_t)directory_offset) {
        return PLATFORM_WAD_HEADER_BAD_DIRECTORY;
    }

    out_header->lump_count = lump_count;
    out_header->directory_offset = directory_offset;
    return PLATFORM_WAD_HEADER_OK;
}

bool platform_wad_matches_doom_shareware_1_9(
    uint64_t size_bytes,
    const uint8_t sha256[32]
)
{
    return sha256 != NULL
        && size_bytes == DOOM_SHAREWARE_1_9_SIZE
        && memcmp(sha256, s_doom_shareware_1_9_sha256, sizeof(s_doom_shareware_1_9_sha256)) == 0;
}

void platform_wad_sha256_hex(const uint8_t sha256[32], char out_hex[65])
{
    static const char s_hex[] = "0123456789abcdef";

    if (sha256 == NULL || out_hex == NULL) {
        return;
    }
    for (size_t index = 0U; index < 32U; ++index) {
        out_hex[index * 2U] = s_hex[sha256[index] >> 4U];
        out_hex[index * 2U + 1U] = s_hex[sha256[index] & 0x0fU];
    }
    out_hex[64] = '\0';
}
