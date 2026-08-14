#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform_storage_wad.h"

static void put_u32_le(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static void test_valid_iwad_header(void)
{
    uint8_t bytes[12] = {'I', 'W', 'A', 'D'};
    put_u32_le(&bytes[4], 1264U);
    put_u32_le(&bytes[8], 4175796U);
    platform_wad_header_t header = {0};

    assert(platform_wad_parse_header(bytes, sizeof(bytes), 4196020U, &header) == PLATFORM_WAD_HEADER_OK);
    assert(header.lump_count == 1264U);
    assert(header.directory_offset == 4175796U);
}

static void test_rejects_bad_headers(void)
{
    uint8_t bytes[12] = {'I', 'W', 'A', 'D'};
    platform_wad_header_t header = {0};

    assert(platform_wad_parse_header(bytes, 11U, 12U, &header) == PLATFORM_WAD_HEADER_TOO_SHORT);
    memcpy(bytes, "PWAD", 4U);
    assert(platform_wad_parse_header(bytes, sizeof(bytes), 12U, &header) == PLATFORM_WAD_HEADER_NOT_IWAD);
    memcpy(bytes, "IWAD", 4U);
    put_u32_le(&bytes[4], 2U);
    put_u32_le(&bytes[8], 12U);
    assert(platform_wad_parse_header(bytes, sizeof(bytes), 20U, &header) == PLATFORM_WAD_HEADER_BAD_DIRECTORY);
    put_u32_le(&bytes[4], UINT32_MAX);
    put_u32_le(&bytes[8], 0U);
    assert(platform_wad_parse_header(bytes, sizeof(bytes), UINT64_C(4294967295), &header) == PLATFORM_WAD_HEADER_BAD_DIRECTORY);
}

static void test_exact_identity(void)
{
    const uint8_t expected[32] = {
        0x1d, 0x7d, 0x43, 0xbe, 0x50, 0x1e, 0x67, 0xd9,
        0x27, 0xe4, 0x15, 0xe0, 0xb8, 0xf3, 0xe2, 0x9c,
        0x3b, 0xf3, 0x30, 0x75, 0xe8, 0x59, 0x72, 0x18,
        0x16, 0xf6, 0x52, 0xa5, 0x26, 0xca, 0xc7, 0x71,
    };
    uint8_t changed[32];
    memcpy(changed, expected, sizeof(changed));
    changed[31] ^= 1U;

    assert(platform_wad_matches_doom_shareware_1_9(4196020U, expected));
    assert(!platform_wad_matches_doom_shareware_1_9(4196019U, expected));
    assert(!platform_wad_matches_doom_shareware_1_9(4196020U, changed));
    assert(!platform_wad_matches_doom_shareware_1_9(4196020U, NULL));

    char hex[65];
    platform_wad_sha256_hex(expected, hex);
    assert(strcmp(hex, "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771") == 0);
}

int main(void)
{
    test_valid_iwad_header();
    test_rejects_bad_headers();
    test_exact_identity();
    puts("platform_storage_wad_tests: PASS");
    return 0;
}
