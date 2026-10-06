// SPDX-License-Identifier: MIT
#include "verified_reader.h"
#include <string.h>
bool p4_verified_read(p4_verified_reader_t *r,size_t offset,void *out,size_t bytes)
{
    if (!r || r->failed || !r->read || !r->sha256 || !r->digests ||
        (!out && bytes) || offset>r->size || bytes>r->size-offset ||
        r->size/P4_VERIFIED_BLOCK_BYTES+(r->size%P4_VERIFIED_BLOCK_BYTES!=0) >
            r->digest_bytes/P4_VERIFIED_DIGEST_BYTES) return false;
    uint8_t *dest=out;
    while (bytes) {
        const size_t block=offset/P4_VERIFIED_BLOCK_BYTES;
        const size_t base=block*P4_VERIFIED_BLOCK_BYTES;
        size_t n=r->size-base;
        if (n>P4_VERIFIED_BLOCK_BYTES) n=P4_VERIFIED_BLOCK_BYTES;
        if (!r->cached || r->cached_block!=block) {
            uint8_t digest[32];
            if (!r->read(r->context,base,r->cache,n) ||
                !r->sha256(r->cache,n,digest) ||
                memcmp(digest,r->digests+block*32,32)!=0) {
                r->failed=true; r->cached=false; return false;
            }
            r->cached=true; r->cached_block=block;
        }
        const size_t within=offset-base;
        n-=within;
        if (n>bytes) n=bytes;
        memcpy(dest,r->cache+within,n);
        dest+=n; bytes-=n; offset+=n;
    }
    return true;
}
