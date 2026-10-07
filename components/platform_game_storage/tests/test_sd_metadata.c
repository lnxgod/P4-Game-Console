// SPDX-License-Identifier: MIT
#include "sd_metadata.h"
#include "sha256.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned hash_calls;
static uint8_t last_material[33];

static bool test_sha256(const void *data, size_t bytes, uint8_t digest[32])
{
    assert(bytes == sizeof(last_material));
    memcpy(last_material, data, bytes);
    ++hash_calls;
    p4_sha256_t context;
    p4_sha256_init(&context);
    p4_sha256_update(&context, data, bytes);
    p4_sha256_finish(&context, digest);
    return true;
}

static bool failing_sha256(const void *data, size_t bytes, uint8_t digest[32])
{
    (void)data;
    (void)bytes;
    memset(digest, 0xaa, 32U);
    return false;
}

static void test_cid(void)
{
    p4_sd_cid_t cid = {
        .manufacturer = 3U, .oem = 0x5344U,
        .product_name = {'A', 'B', 'C', 'D', 'E'},
        .revision = 0x21U, .serial = UINT32_C(0xdeadbeef), .date = 0x1a5U,
    };
    /* Independent Python hashlib/struct golden vector, fixed endian encoding. */
    static const uint8_t expected[33] = {
        0x50,0x34,0x53,0x44,0x43,0x49,0x44,0x31,
        0x00,0x00,0x00,0x03,0x00,0x00,0x53,0x44,
        0x41,0x42,0x43,0x44,0x45,0x00,0x00,0x00,
        0x21,0xde,0xad,0xbe,0xef,0x00,0x00,0x01,0xa5,
    };
    const char *golden =
        "58e423c5508e981c1aa1b241f943a350957e124982857f6828c783b824e0d3d4";
    char hex[65];
    assert(p4_sd_metadata_cid_hash(&cid, test_sha256, hex));
    assert(strcmp(hex, golden) == 0);
    assert(memcmp(last_material, expected, sizeof(expected)) == 0);
    assert(hex[64] == '\0');

    /* Signed int storage in the pinned IDF must retain all serial bits. */
    cid.serial = (uint32_t)INT32_C(-559038737);
    assert(p4_sd_metadata_cid_hash(&cid, test_sha256, hex));
    assert(strcmp(hex, golden) == 0);
    ++cid.serial;
    assert(p4_sd_metadata_cid_hash(&cid, test_sha256, hex));
    assert(strcmp(hex, golden) != 0);
    --cid.serial;
    cid.product_name[4] = 0U; /* Fixed-width data may contain a zero byte. */
    assert(p4_sd_metadata_cid_hash(&cid, test_sha256, hex));
    assert(strcmp(hex, golden) != 0);

    memset(hex, 'x', sizeof(hex));
    assert(!p4_sd_metadata_cid_hash(&cid, failing_sha256, hex));
    for (size_t i = 0U; i < sizeof(hex); ++i) assert(hex[i] == '\0');
    const unsigned before = hash_calls;
    cid.manufacturer = 256U;
    assert(!p4_sd_metadata_cid_hash(&cid, test_sha256, hex));
    cid.manufacturer = 3U; cid.oem = 65536U;
    assert(!p4_sd_metadata_cid_hash(&cid, test_sha256, hex));
    cid.oem = 0x5344U; cid.revision = 256U;
    assert(!p4_sd_metadata_cid_hash(&cid, test_sha256, hex));
    cid.revision = 0x21U; cid.date = 0x1000U;
    assert(!p4_sd_metadata_cid_hash(&cid, test_sha256, hex));
    assert(hash_calls == before);
    assert(!p4_sd_metadata_cid_hash(NULL, test_sha256, hex));
    assert(!p4_sd_metadata_cid_hash(&cid, NULL, hex));
    assert(!p4_sd_metadata_cid_hash(&cid, test_sha256, NULL));
}

static void expect_bad_geometry(p4_sd_fat_input_t input)
{
    p4_sd_fat_geometry_t geometry;
    memset(&geometry, 0xaa, sizeof(geometry));
    assert(!p4_sd_metadata_fat_geometry(&input, &geometry));
    const p4_sd_fat_geometry_t zero = {0};
    assert(memcmp(&geometry, &zero, sizeof(zero)) == 0);
}

static void test_geometry(void)
{
    const p4_sd_fat_input_t valid = {
        .fat_type = 3U, .sector_bytes = 512U, .sectors_per_cluster = 8U,
        .fat_entries = 64514U, .free_clusters = 64000U,
        .volume_lba = 2048U, .data_lba = 4096U,
        .physical_bytes = UINT64_C(16012804096),
    };
    p4_sd_fat_geometry_t geometry;
    assert(p4_sd_metadata_fat_geometry(&valid, &geometry));
    assert(geometry.cluster_bytes == 4096U);
    assert(geometry.data_clusters == 64512U);
    assert(geometry.data_bytes == UINT64_C(264241152));
    assert(geometry.free_bytes == UINT64_C(262144000));
    p4_sd_fat_input_t input = valid;
    input.physical_bytes = geometry.data_bytes + input.data_lba * 512U;
    assert(p4_sd_metadata_fat_geometry(&input, &geometry));
    input.physical_bytes -= 512U; expect_bad_geometry(input);
    input = valid; input.fat_type = 0U; expect_bad_geometry(input);
    input = valid; input.fat_type = 5U; expect_bad_geometry(input);
    input = valid; input.sector_bytes = 513U; expect_bad_geometry(input);
    input = valid; input.sector_bytes = 8192U; expect_bad_geometry(input);
    input = valid; input.sectors_per_cluster = 0U; expect_bad_geometry(input);
    input = valid; input.sectors_per_cluster = 3U; expect_bad_geometry(input);
    input = valid; input.sectors_per_cluster = 65536U; expect_bad_geometry(input);
    input = valid; input.fat_entries = 2U; expect_bad_geometry(input);
    input = valid; input.free_clusters = 64513U; expect_bad_geometry(input);
    input = valid; input.volume_lba = input.data_lba; expect_bad_geometry(input);
    input = valid; input.data_lba = UINT64_MAX; expect_bad_geometry(input);
    input = valid; ++input.physical_bytes; expect_bad_geometry(input);
    input = valid; input.physical_bytes = 0U; expect_bad_geometry(input);

    /* Largest accepted products remain exact, without 32-bit truncation. */
    input = valid;
    input.sector_bytes = 4096U; input.sectors_per_cluster = 32768U;
    input.fat_entries = UINT32_MAX; input.free_clusters = UINT32_MAX - 2U;
    input.volume_lba = 0U; input.data_lba = 1U;
    input.physical_bytes =
        (UINT64_C(4294967293) * 32768U + 1U) * 4096U;
    assert(p4_sd_metadata_fat_geometry(&input, &geometry));
    assert(geometry.cluster_bytes == UINT32_C(134217728));
    assert(geometry.data_bytes == UINT64_C(576460751900770304));
    assert(geometry.free_bytes == geometry.data_bytes);
    assert(!p4_sd_metadata_fat_geometry(NULL, &geometry));
    assert(!p4_sd_metadata_fat_geometry(&valid, NULL));
}

int main(void)
{
    test_cid();
    test_geometry();
    assert(p4_sd_metadata_ssr_width(0U) == 1U);
    assert(p4_sd_metadata_ssr_width(2U) == 4U);
    assert(p4_sd_metadata_ssr_width(1U) == 0U);
    assert(p4_sd_metadata_ssr_width(3U) == 0U);
    assert(p4_sd_metadata_ssr_width(UINT32_MAX) == 0U);
    puts("sd_metadata: CID golden/failure, geometry boundary, width tests passed");
    return 0;
}
