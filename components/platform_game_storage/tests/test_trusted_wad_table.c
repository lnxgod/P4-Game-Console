// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "trusted_wad_table.h"
#include "verified_reader.h"
#include "verified_window.h"
#include "verified_wad.h"
#include "storage_block_read.h"
#include "sha256.h"
static unsigned hashes;
static bool hash_fails;
static bool digest(const void *p,size_t n,uint8_t out[32]) {
    ++hashes; if(hash_fails)return false;
    p4_sha256_t s;p4_sha256_init(&s);p4_sha256_update(&s,p,n);p4_sha256_finish(&s,out);return true;
}
static void hex(const uint8_t *p,char out[65]) {
    for(unsigned i=0;i<32;++i)(void)sprintf(out+i*2U,"%02x",p[i]);
}
static bool read_block(void *ctx,size_t off,void *out,size_t n) {
    return p4_storage_cursor_read_block(ctx,off,out,n);
}
static bool read_batch(void *ctx,size_t off,void *out,size_t n) {
    return p4_storage_cursor_read_batch(ctx,off,out,n);
}
int main(void) {
    enum { N=12299, LEAVES=4 };
    uint8_t data[N],table[LEAVES*32],content[32]={42};
    memset(data,0x6b,sizeof(data));memcpy(data,"IWAD",4);
    memset(data+4,0,24);data[4]=1;data[8]=12; /* one empty directory entry */
    for(size_t i=0;i<LEAVES;++i){size_t n=N-i*4096U;if(n>4096)n=4096;assert(digest(data+i*4096U,n,table+i*32U));}
    p4_trusted_wad_table_t t={.schema=1,.file_index=0,.symbol="BASE",.path="/game-data/FREEDOOM2.WAD",.size=N,.block_bytes=4096,.block_count=LEAVES,.digest_bytes=sizeof(table),.digests=table};
    memcpy(t.content_sha256,content,32);assert(digest(data,N,t.whole_sha256));assert(digest(table,sizeof(table),t.table_sha256));
    char expected[65];hex(t.whole_sha256,expected);
    p4_trusted_wad_view_t view={0};
#define VALID(x) p4_trusted_wad_table_validate(&(x),0,"BASE",t.path,N,expected,content,digest,&view)
    assert(VALID(t));assert(view.digests==table&&view.digest_bytes==sizeof(table));
    p4_trusted_wad_table_t bad=t;
#define REJECT(field,value) do {bad=t;bad.field=(value);view.digests=table;assert(!VALID(bad));assert(view.digests==NULL&&view.digest_bytes==0);}while(0)
    REJECT(schema,2);REJECT(file_index,1);REJECT(symbol,"PWAD");REJECT(path,"/game-data/OTHER.WAD");
    REJECT(size,N+1);REJECT(block_bytes,2048);REJECT(block_count,LEAVES+1);REJECT(digest_bytes,sizeof(table)-1);REJECT(digests,NULL);
    bad=t;bad.content_sha256[0]^=1;assert(!VALID(bad));bad=t;bad.whole_sha256[0]^=1;assert(!VALID(bad));bad=t;bad.table_sha256[0]^=1;assert(!VALID(bad));
    table[0]^=1;assert(!VALID(t));table[0]^=1;
    hash_fails=true;assert(!VALID(t));hash_fails=false;
    assert(!p4_trusted_wad_table_validate(&t,0,"BASE",t.path,N,"xyz",content,digest,&view));
    bad=t;bad.size=SIZE_MAX;bad.block_count=SIZE_MAX;assert(!p4_trusted_wad_table_validate(&bad,0,"BASE",t.path,SIZE_MAX,expected,content,digest,&view));
    /* Real cursor + reader + structure parser. Only block zero is consumed by
     * this minimal IWAD's header/directory; corruption elsewhere is lazy. */
    FILE *f=tmpfile();assert(f);assert(fwrite(data,1,N,f)==N);assert(fflush(f)==0);
    p4_storage_cursor_t cursor={.stream=f};
    p4_verified_reader_t r={.context=&cursor,.read=read_block,.sha256=digest,.digests=table,.size=N,.digest_bytes=sizeof(table)};
    assert(p4_verified_wad_validate(&r,P4_WAD_IWAD));
    assert(pwrite(fileno(f),"X",1,4096)==1);r.cached=false;
    uint8_t output[32];memset(output,0xaa,sizeof(output));
    assert(!p4_verified_read(&r,4096,output,sizeof(output)));assert(r.failed);
    for(size_t i=0;i<sizeof(output);++i)assert(output[i]==0xaa);
    assert(!p4_verified_read(&r,0,output,1)); /* terminal failure latch */
    r.failed=false;r.cached=false;cursor=(p4_storage_cursor_t){.stream=f};
    assert(p4_verified_wad_validate(&r,P4_WAD_IWAD)); /* honest lazy property */
    _Alignas(64) uint8_t window_data[P4_VERIFIED_WINDOW_BYTES];p4_verified_window_t window={0};
    assert(p4_verified_window_begin(&window,&r,1,window_data,sizeof(window_data)));
    memset(output,0xaa,sizeof(output));assert(!p4_verified_window_read(&window,&r,1,read_batch,0,output,1));
    assert(!window.cached&&r.failed);assert(output[0]==0xaa); /* bad prefetch not published */
    assert(pwrite(fileno(f),data+4096,1,4096)==1);r.failed=false;r.cached=false;cursor=(p4_storage_cursor_t){.stream=f};
    assert(p4_verified_read(&r,N-11,output,11));assert(memcmp(output,data+N-11,11)==0);
    assert(ftruncate(fileno(f),N-1)==0);r.cached=false;cursor=(p4_storage_cursor_t){.stream=f};
    memset(output,0xaa,sizeof(output));assert(!p4_verified_read(&r,N-11,output,11));assert(r.failed&&output[0]==0xaa);
    assert(fclose(f)==0);puts("trusted_wad_table: PASS descriptor, actual cursor/reader/window, tails, corruption, short read");return 0;
}
