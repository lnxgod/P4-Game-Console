// SPDX-License-Identifier: MIT
#ifndef P4_TRUSTED_WAD_TABLE_H
#define P4_TRUSTED_WAD_TABLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Firmware metadata only. Trust comes from generation after exact whole-file
 * verification and the firmware artifact binding, never from removable media. */
typedef struct {
    uint32_t schema, file_index, block_bytes;
    const char *symbol, *path;
    size_t size, block_count, digest_bytes;
    uint8_t content_sha256[32], whole_sha256[32], table_sha256[32];
    const uint8_t *digests;
} p4_trusted_wad_table_t;
typedef struct { const uint8_t *digests; size_t digest_bytes; } p4_trusted_wad_view_t;
bool p4_trusted_wad_table_validate(const p4_trusted_wad_table_t *table,
    unsigned file_index, const char *symbol, const char *path, size_t size,
    const char *whole_sha256_hex, const uint8_t content_sha256[32],
    bool (*sha256)(const void *,size_t,uint8_t[32]), p4_trusted_wad_view_t *out);
/* Defined by the Tab5 build's metadata-only generator. Never heap-owned. */
extern const p4_trusted_wad_table_t p4_arena_trusted_wad_tables[3];
#endif
