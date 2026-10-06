// SPDX-License-Identifier: MIT
#include "verified_reader.h"
#include "sha256.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t data[9001],digests[96];
static size_t available=sizeof(data),reads;
static bool read_block(void *context,size_t offset,void *out,size_t n)
{
    (void)context; ++reads;
    if (offset>available || n>available-offset) return false;
    memcpy(out,data+offset,n); return true;
}
static bool hash(const void *p,size_t n,uint8_t d[32])
{ p4_sha256_t s; p4_sha256_init(&s); p4_sha256_update(&s,p,n); p4_sha256_finish(&s,d); return true; }
int main(void)
{
    for (size_t i=0;i<sizeof(data);++i) data[i]=(uint8_t)i;
    for (size_t i=0;i<3;++i) {
        const size_t n=i==2?sizeof(data)-8192:4096;
        assert(hash(data+i*4096,n,digests+i*32));
    }
    p4_verified_reader_t r={.read=read_block,.sha256=hash,.digests=digests,
        .digest_bytes=sizeof(digests),.size=sizeof(data)};
    uint8_t out[4500];
    assert(p4_verified_read(&r,4000,out,sizeof(out)));
    assert(memcmp(out,data+4000,sizeof(out))==0 && reads==3);
    assert(p4_verified_read(&r,9001,NULL,0));
    assert(!p4_verified_read(&r,9001,out,1));
    assert(!p4_verified_read(&r,SIZE_MAX,out,2));
    assert(!p4_verified_read(&r,0,NULL,1));
    assert(p4_verified_read(&r,8999,out,2) && reads==3);
    data[0]^=1;
    assert(!p4_verified_read(&r,0,out,2) && r.failed);
    data[0]^=1;
    assert(!p4_verified_read(&r,0,out,2));
    r.failed=false; available=8000;
    assert(!p4_verified_read(&r,4096,out,1));
    puts("verified reader: boundary, cross-block, cache, mutation, short-read and fail-closed checks passed");
    return 0;
}
