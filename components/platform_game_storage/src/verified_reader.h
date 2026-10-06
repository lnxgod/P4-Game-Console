// SPDX-License-Identifier: MIT
#ifndef P4_VERIFIED_READER_H
#define P4_VERIFIED_READER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { P4_VERIFIED_BLOCK_BYTES=4096, P4_VERIFIED_DIGEST_BYTES=32 };
/* Caller supplies a trusted per-block table made during whole-file SHA-256
 * verification under its storage lease. All callbacks are serialized by caller. */
typedef struct {
    void *context;
    bool (*read)(void *, size_t offset, void *, size_t bytes);
    bool (*sha256)(const void *,size_t,uint8_t digest[32]);
    const uint8_t *digests;
    size_t size, digest_bytes, cached_block;
    uint8_t cache[P4_VERIFIED_BLOCK_BYTES];
    bool cached, failed;
} p4_verified_reader_t;
bool p4_verified_read(p4_verified_reader_t *,size_t offset,void *,size_t bytes);
#endif
