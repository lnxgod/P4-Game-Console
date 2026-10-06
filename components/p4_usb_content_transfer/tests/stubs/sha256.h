// SPDX-License-Identifier: MIT

#ifndef P4_CONTENT_SHA256_H
#define P4_CONTENT_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t byte_count;
    uint8_t block[64];
    size_t block_used;
} p4_sha256_t;

void p4_sha256_init(p4_sha256_t *context);
void p4_sha256_update(p4_sha256_t *context, const uint8_t *bytes, size_t length);
void p4_sha256_finish(p4_sha256_t *context, uint8_t digest[32]);

#endif
