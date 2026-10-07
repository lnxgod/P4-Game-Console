// SPDX-License-Identifier: MIT
#include "verified_window.h"
#include <string.h>
void p4_verified_window_reset(p4_verified_window_t *w)
{
    if (w) memset(w, 0, sizeof(*w));
}
bool p4_verified_window_begin(p4_verified_window_t *w,
    p4_verified_reader_t *r, uint32_t generation, void *buffer, size_t size)
{
    if (!w) return false;
    p4_verified_window_reset(w);
    if (!r || r->failed || !buffer || size != P4_VERIFIED_WINDOW_BYTES ||
        (uintptr_t)buffer % 64U != 0) return false;
    w->reader = r; w->generation = generation; w->data = buffer;
    return true;
}
bool p4_verified_window_read(p4_verified_window_t *w,
    p4_verified_reader_t *r, uint32_t generation,
    bool (*batch)(void *, size_t, void *, size_t),
    size_t offset, void *out, size_t bytes)
{
    if (!w || !r || r != w->reader || generation != w->generation ||
        !w->data || r->failed || !batch || !r->sha256 || !r->digests ||
        (!out && bytes) || offset > r->size || bytes > r->size - offset ||
        r->size / P4_VERIFIED_BLOCK_BYTES +
            (r->size % P4_VERIFIED_BLOCK_BYTES != 0) >
            r->digest_bytes / P4_VERIFIED_DIGEST_BYTES) return false;
    uint8_t *dest = out;
    while (bytes) {
        const size_t base = offset / P4_VERIFIED_WINDOW_BYTES * P4_VERIFIED_WINDOW_BYTES;
        if (!w->cached || w->base != base) {
            w->cached = false;
            size_t n = r->size - base;
            if (n > P4_VERIFIED_WINDOW_BYTES) n = P4_VERIFIED_WINDOW_BYTES;
            if (!batch(r->context, base, w->data, n)) goto failure;
            /* Verify every prefetched block before publishing the window. */
            for (size_t i = 0; i < n; i += P4_VERIFIED_BLOCK_BYTES) {
                size_t block_bytes = n - i;
                uint8_t digest[P4_VERIFIED_DIGEST_BYTES];
                if (block_bytes > P4_VERIFIED_BLOCK_BYTES)
                    block_bytes = P4_VERIFIED_BLOCK_BYTES;
                if (!r->sha256(w->data + i, block_bytes, digest) ||
                    memcmp(digest, r->digests +
                        (base + i) / P4_VERIFIED_BLOCK_BYTES * P4_VERIFIED_DIGEST_BYTES,
                        sizeof(digest)) != 0) goto failure;
            }
            w->base = base; w->bytes = n; w->cached = true;
        }
        const size_t within = offset - w->base;
        size_t n = w->bytes - within;
        if (n > bytes) n = bytes;
        memcpy(dest, w->data + within, n);
        dest += n; offset += n; bytes -= n;
    }
    return true;
failure:
    w->cached = false;
    r->failed = true;
    r->cached = false;
    return false;
}
