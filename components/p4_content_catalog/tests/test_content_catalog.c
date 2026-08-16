// SPDX-License-Identifier: MIT

#include "p4/content_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sha256.h"

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static void put_u16(uint8_t bytes[2], uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void put_u32(uint8_t bytes[4], uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t align4(uint32_t value)
{
    return (value + 3U) & ~UINT32_C(3);
}

static void hash_bytes(const uint8_t *bytes, size_t length, uint8_t digest[32])
{
    p4_sha256_t hash;
    p4_sha256_init(&hash);
    p4_sha256_update(&hash, bytes, length);
    p4_sha256_finish(&hash, digest);
}

static void write_entry(
    uint8_t entry[112],
    const char *path,
    uint8_t kind,
    uint32_t offset,
    const uint8_t *payload,
    uint32_t payload_length)
{
    memset(entry, 0, 112U);
    memcpy(entry, path, strlen(path));
    entry[64] = kind;
    put_u32(&entry[68], offset);
    put_u32(&entry[72], payload_length);
    hash_bytes(payload, payload_length, &entry[76]);
}

static bool write_fixture(const char *path, bool corrupt_payload)
{
    static const char manifest[] =
        "{\"format\":\"p4-cart-source-v1\",\"id\":\"p4-lua-5.4-v1\","
        "\"logical_height\":480,\"logical_width\":768,\"present_hz\":30,"
        "\"source_included\":true,\"update_hz\":60}\n";
    static const uint8_t license[] = "MIT\n";
    static const uint8_t readme[] = "# Test\n";
    static const uint8_t source[] = "function update() end\n";
    const uint32_t manifest_size = (uint32_t)(sizeof(manifest) - 1U);
    const uint32_t table_offset = align4(128U + manifest_size);
    const uint32_t payload_offset = align4(table_offset + 3U * 112U);
    const uint32_t license_offset = payload_offset;
    const uint32_t readme_offset = align4(
        license_offset + (uint32_t)(sizeof(license) - 1U));
    const uint32_t source_offset = align4(
        readme_offset + (uint32_t)(sizeof(readme) - 1U));
    const uint32_t total_size = source_offset + (uint32_t)(sizeof(source) - 1U);
    uint8_t *const bytes = calloc(total_size, 1U);
    if (bytes == NULL) {
        return false;
    }
    memcpy(bytes, "P4CART1\0", 8U);
    put_u16(&bytes[8], 128U);
    put_u16(&bytes[10], 1U);
    put_u32(&bytes[16], total_size);
    put_u32(&bytes[20], 128U);
    put_u32(&bytes[24], manifest_size);
    put_u16(&bytes[28], 3U);
    put_u16(&bytes[30], 112U);
    put_u32(&bytes[32], payload_offset);
    memcpy(&bytes[128], manifest, manifest_size);
    write_entry(
        &bytes[table_offset], "LICENSES/NOTICE.txt", 5U,
        license_offset, license, (uint32_t)(sizeof(license) - 1U));
    write_entry(
        &bytes[table_offset + 112U], "README.md", 4U,
        readme_offset, readme, (uint32_t)(sizeof(readme) - 1U));
    write_entry(
        &bytes[table_offset + 224U], "main.lua", 1U,
        source_offset, source, (uint32_t)(sizeof(source) - 1U));
    memcpy(&bytes[license_offset], license, sizeof(license) - 1U);
    memcpy(&bytes[readme_offset], readme, sizeof(readme) - 1U);
    memcpy(&bytes[source_offset], source, sizeof(source) - 1U);
    hash_bytes(bytes, total_size, &bytes[36]);
    if (corrupt_payload) {
        bytes[source_offset] ^= UINT8_C(0x40);
    }
    FILE *const file = fopen(path, "wb");
    bool success = file != NULL && fwrite(bytes, 1U, total_size, file) == total_size;
    if (file != NULL && fclose(file) != 0) {
        success = false;
    }
    free(bytes);
    return success;
}

static void test_sha256(void)
{
    static const uint8_t input[] = "abc";
    uint8_t digest[32];
    char hex[65];
    hash_bytes(input, sizeof(input) - 1U, digest);
    p4_content_sha256_hex(digest, hex);
    CHECK(strcmp(
              hex,
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
}

static void test_cart_and_catalog(void)
{
    char root_template[] = "/tmp/p4-content-XXXXXX";
    char *const root = mkdtemp(root_template);
    CHECK(root != NULL);
    if (root == NULL) {
        return;
    }
    char p4_dir[160];
    char games_dir[160];
    char good_path[160];
    char bad_path[160];
    (void)snprintf(p4_dir, sizeof(p4_dir), "%s/P4", root);
    (void)snprintf(games_dir, sizeof(games_dir), "%s/P4/GAMES", root);
    (void)snprintf(good_path, sizeof(good_path), "%s/z-good.p4cart", games_dir);
    (void)snprintf(bad_path, sizeof(bad_path), "%s/a-bad.P4CART", games_dir);
    CHECK(mkdir(p4_dir, 0700) == 0);
    CHECK(mkdir(games_dir, 0700) == 0);
    CHECK(write_fixture(good_path, false));
    CHECK(write_fixture(bad_path, true));

    p4_content_item_t item;
    CHECK(p4_content_validate_cart_file(good_path, &item) == P4_CONTENT_OK);
    CHECK(item.status == P4_CONTENT_OK && item.size_bytes > 128U);
    CHECK(strcmp(item.name, "z-good.p4cart") == 0);
    CHECK(p4_content_validate_cart_file(bad_path, &item) == P4_CONTENT_BAD_HASH);

    p4_content_catalog_t catalog;
    CHECK(p4_content_catalog_scan(root, &catalog) == P4_CONTENT_OK);
    CHECK(catalog.storage_available);
    CHECK(catalog.candidates_seen == 2U);
    CHECK(catalog.valid_cart_count == 1U);
    CHECK(catalog.invalid_cart_count == 1U);
    CHECK(strcmp(catalog.carts[0].name, "z-good.p4cart") == 0);
    CHECK(unlink(good_path) == 0);
    CHECK(unlink(bad_path) == 0);
    CHECK(rmdir(games_dir) == 0);
    CHECK(rmdir(p4_dir) == 0);
    CHECK(rmdir(root) == 0);
}

static void test_missing_and_wrong_quake(void)
{
    p4_content_catalog_t catalog;
    CHECK(p4_content_catalog_scan(
              "/definitely/not/a/p4/storage/root", &catalog) ==
          P4_CONTENT_NOT_FOUND);

    char path_template[] = "/tmp/p4-quake-XXXXXX";
    const int descriptor = mkstemp(path_template);
    CHECK(descriptor >= 0);
    if (descriptor < 0) {
        return;
    }
    static const uint8_t bytes[] = "not a pak";
    CHECK(write(descriptor, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
    CHECK(close(descriptor) == 0);
    p4_content_item_t item;
    CHECK(p4_content_validate_quake_shareware(path_template, &item) ==
          P4_CONTENT_BAD_FORMAT);
    CHECK(unlink(path_template) == 0);
}

int main(void)
{
    test_sha256();
    test_cart_and_catalog();
    test_missing_and_wrong_quake();
    if (s_failures != 0) {
        fprintf(stderr, "%d content catalog test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 content catalog tests passed");
    return EXIT_SUCCESS;
}
