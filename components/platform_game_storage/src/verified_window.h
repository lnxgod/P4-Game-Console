// SPDX-License-Identifier: MIT
#pragma once
#include "verified_reader.h"
enum { P4_VERIFIED_WINDOW_BYTES = 65536 };
/* Optional caller-owned scratch, scoped to one reader under its existing lease.
 * No heap ownership and no global changes to p4_verified_read. Caller resets on
 * lease change/close; each read also binds the current reader and generation. */
typedef struct {
    p4_verified_reader_t *reader;
    uint8_t *data;
    size_t base, bytes;
    uint32_t generation;
    bool cached;
} p4_verified_window_t;
void p4_verified_window_reset(p4_verified_window_t *window);
bool p4_verified_window_begin(p4_verified_window_t *window,
    p4_verified_reader_t *reader, uint32_t generation, void *buffer, size_t size);
bool p4_verified_window_read(p4_verified_window_t *window,
    p4_verified_reader_t *reader, uint32_t generation,
    bool (*batch_read)(void *, size_t, void *, size_t),
    size_t offset, void *out, size_t bytes);
