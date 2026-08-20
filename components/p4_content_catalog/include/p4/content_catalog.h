// SPDX-License-Identifier: MIT

#ifndef P4_CONTENT_CATALOG_H
#define P4_CONTENT_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_CONTENT_MAX_DIRECTORY_CANDIDATES = 128,
    P4_CONTENT_MAX_CARTS = 16,
    P4_CONTENT_NAME_BYTES = 64,
    P4_CONTENT_PATH_BYTES = 160,
    P4_CONTENT_SHA256_BYTES = 32,
    P4_CONTENT_CART_ENTRY_PATH_BYTES = 64,
    P4_CONTENT_CART_MAX_BYTES = 8 * 1024 * 1024,
    P4_CONTENT_CART_SOURCE_MAX_BYTES = 256 * 1024,
    P4_CONTENT_QUAKE_SHAREWARE_BYTES = 18689235,
};

#define P4_CONTENT_CART_DIRECTORY "P4/GAMES"
#define P4_CONTENT_QUAKE_SHAREWARE_RELATIVE_PATH \
    "GAMES/QUAKE/ID1/PAK0.PAK"

typedef enum {
    P4_CONTENT_OK = 0,
    P4_CONTENT_INVALID_ARGUMENT,
    P4_CONTENT_NOT_FOUND,
    P4_CONTENT_IO_ERROR,
    P4_CONTENT_NOT_REGULAR,
    P4_CONTENT_TOO_LARGE,
    P4_CONTENT_BAD_FORMAT,
    P4_CONTENT_BAD_HASH,
    P4_CONTENT_LIMIT_REACHED,
} p4_content_status_t;

typedef struct {
    char name[P4_CONTENT_NAME_BYTES];
    char path[P4_CONTENT_PATH_BYTES];
    uint64_t size_bytes;
    uint8_t sha256[P4_CONTENT_SHA256_BYTES];
    p4_content_status_t status;
} p4_content_item_t;

typedef struct {
    bool storage_available;
    bool directory_truncated;
    uint16_t candidates_seen;
    uint16_t valid_cart_count;
    uint16_t invalid_cart_count;
    p4_content_item_t carts[P4_CONTENT_MAX_CARTS];
} p4_content_catalog_t;

typedef struct {
    char entry_path[P4_CONTENT_CART_ENTRY_PATH_BYTES];
    uint32_t source_bytes;
    uint32_t heap_bytes;
    uint32_t save_bytes;
    uint8_t cart_sha256[P4_CONTENT_SHA256_BYTES];
} p4_content_cart_runtime_t;

/**
 * Validate a deterministic P4 Cart v1 file without executing it.
 *
 * This checks all bounded container geometry, canonical entry paths, overall
 * SHA-256, every payload SHA-256, zero padding, and the frozen 768x480 runtime
 * markers. It establishes catalog eligibility only; a future sandbox loader
 * must still perform its full manifest/schema and Lua-source validation.
 */
p4_content_status_t p4_content_validate_cart_file(
    const char *path,
    p4_content_item_t *item_out);

/**
 * Revalidate and copy the declared Lua entry source from one P4 Cart.
 *
 * The caller owns the destination buffer. No source bytes are executed here.
 * The selected entry must be a hashed runtime-source payload and the runtime
 * heap/save requests must remain inside the frozen v1 bounds.
 */
p4_content_status_t p4_content_load_cart_source(
    const char *path,
    uint8_t *source_out,
    size_t source_capacity,
    p4_content_cart_runtime_t *runtime_out);

/**
 * Validate the exact Quake v1.06 shareware PAK identity for retired tooling.
 *
 * The active Console OS catalog does not call this compatibility helper.
 */
p4_content_status_t p4_content_validate_quake_shareware(
    const char *path,
    p4_content_item_t *item_out);

/**
 * Scan one mounted SD root. The scan never recurses and never writes.
 *
 * The root is `/sdcard` on device and may be a temporary directory in host
 * tests. Missing optional game directories produce an empty successful
 * catalog as long as the root itself exists.
 */
p4_content_status_t p4_content_catalog_scan(
    const char *storage_root,
    p4_content_catalog_t *catalog_out);

const char *p4_content_status_name(p4_content_status_t status);
void p4_content_sha256_hex(
    const uint8_t digest[P4_CONTENT_SHA256_BYTES],
    char output[65]);

#ifdef __cplusplus
}
#endif

#endif
