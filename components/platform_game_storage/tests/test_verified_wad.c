// SPDX-License-Identifier: MIT
#include "verified_wad.h"
#include "sha256.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool hash(const void *p,size_t n,uint8_t d[32])
{ p4_sha256_t s; p4_sha256_init(&s); p4_sha256_update(&s,p,n); p4_sha256_finish(&s,d); return true; }
static bool read_memory(void *p,size_t offset,void *out,size_t n)
{ memcpy(out,(uint8_t *)p+offset,n); return true; }
static void put32(uint8_t *p,size_t n)
{ for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(i*8U)); }
typedef struct {
    const uint8_t *data;
    unsigned reads, progress, fail_read;
} progress_fixture_t;
static bool read_progress_fixture(void *context,size_t offset,void *out,size_t n)
{
    progress_fixture_t *fixture=context;
    /* Each read models one slow block: every previous read must have yielded
     * to the caller before the next SD read, including directory traversal. */
    assert(fixture->progress==fixture->reads);
    ++fixture->reads;
    if (fixture->reads==fixture->fail_read) return false;
    memcpy(out,fixture->data+offset,n);
    return true;
}
static void note_progress(void *context)
{
    progress_fixture_t *fixture=context;
    assert(fixture->progress+1==fixture->reads);
    ++fixture->progress;
}
static void test_validation_progress(void)
{
    uint8_t *data=calloc(4,P4_VERIFIED_BLOCK_BYTES); assert(data);
    const size_t size=4U*P4_VERIFIED_BLOCK_BYTES;
    uint8_t digests[4*P4_VERIFIED_DIGEST_BYTES];
    /* The header and three directory blocks force four separate SD reads.
     * Empty IWAD lumps are legal; the entire fixture is covered by hashes. */
    memcpy(data,"IWAD",4); put32(data+4,768); put32(data+8,P4_VERIFIED_BLOCK_BYTES);
    for (size_t i=0;i<4;++i)
        hash(data+i*P4_VERIFIED_BLOCK_BYTES,P4_VERIFIED_BLOCK_BYTES,digests+i*32);
    for (unsigned fail=0;fail<=4;++fail) {
        progress_fixture_t fixture={.data=data,.fail_read=fail};
        p4_verified_reader_t reader={.context=&fixture,.read=read_progress_fixture,
            .sha256=hash,.digests=digests,.size=size,.digest_bytes=sizeof(digests)};
        assert(p4_verified_wad_validate_with_progress(
            &reader,P4_WAD_IWAD,note_progress,&fixture)==(fail==0));
        assert(fixture.reads==(fail==0 ? 4 : fail));
        assert(fixture.progress==fixture.reads);
        assert(reader.context==&fixture && reader.read==read_progress_fixture);
    }
    p4_verified_reader_t reader={.context=data,.read=read_memory,.sha256=hash,
        .digests=digests,.size=size,.digest_bytes=sizeof(digests)};
    assert(p4_verified_wad_validate_with_progress(&reader,P4_WAD_IWAD,NULL,NULL));
    assert(reader.context==data && reader.read==read_memory);
    assert(!p4_verified_wad_validate_with_progress(NULL,P4_WAD_IWAD,note_progress,NULL));
    free(data);
}
static bool validate(uint8_t *data,size_t size,p4_wad_kind_t pwad)
{
    const size_t blocks=(size+4095U)/4096U;
    uint8_t *digests=malloc(blocks*32U); assert(digests);
    for (size_t b=0;b<blocks;++b) {
        size_t n=size-b*4096U; if (n>4096U) n=4096U;
        hash(data+b*4096U,n,digests+b*32U);
    }
    p4_verified_reader_t r={.context=data,.read=read_memory,.sha256=hash,
        .digests=digests,.size=size,.digest_bytes=blocks*32U};
    const bool result=p4_verified_wad_validate(&r,pwad);
    free(digests); return result;
}
int main(int argc,char **argv)
{
    test_validation_progress();
    if (argc==4) {
        for (int i=1;i<4;++i) {
            FILE *f=fopen(argv[i],"rb"); assert(f);
            assert(fseek(f,0,SEEK_END)==0);
            const long size=ftell(f); assert(size>0 && size<64*1024*1024);
            rewind(f); uint8_t *data=malloc((size_t)size); assert(data);
            assert(fread(data,1,(size_t)size,f)==(size_t)size && fclose(f)==0);
            assert(validate(data,(size_t)size,(p4_wad_kind_t)(i-1))); free(data);
        }
        puts("verified WAD: supplied IWAD/PWAD passed the production block reader and directory validator");
        return 0;
    }
    uint8_t data[8192]={0};
    static const char names[11][9]={"MAP01","THINGS","LINEDEFS","SIDEDEFS","VERTEXES","SEGS","SSECTORS","NODES","SECTORS","REJECT","BLOCKMAP"};
    static const char extras[16][9]={"D_RUNNIN","D_STALKS","D_COUNTD","D_BETWEE","D_DOOM","D_DM2TTL","D_DM2INT","DEHACKED","UMAPINFO","CWILV00","CWILV01","CWILV02","CWILV03","CWILV04","M_DOOM","TITLEPIC"};
    static const size_t sizes[11]={0,40,14,30,4,12,4,28,26,1,10};
    memcpy(data,"PWAD",4);put32(data+4,71);put32(data+8,4096);
    size_t at=12;
    for(size_t i=0;i<71;++i) {
        const size_t n=i<55?sizes[i%11]:14;
        uint8_t *entry=data+4096+i*16;
        put32(entry,at);put32(entry+4,n);if(i<55) {
            char name[9];if(i%11==0)snprintf(name,sizeof(name),"MAP%02u",(unsigned)(i/11+1));else memcpy(name,names[i%11],9);
            memcpy(entry+8,name,8);
        } else memcpy(entry+8,extras[i-55],8);
        if(i<55 && i%11==1) for(unsigned j=0;j<4;++j) data[at+j*10+6]=11;
        if(i>=55 && i<62) memcpy(data+at,"MThd",4);
        at+=n;
    }
    assert(validate(data,sizeof(data),P4_WAD_PURE_HADES));
    assert(!validate(data,sizeof(data),P4_WAD_IWAD));
    put32(data+4,UINT32_MAX); assert(!validate(data,sizeof(data),P4_WAD_PURE_HADES));put32(data+4,71);
    put32(data+8,8190);assert(!validate(data,sizeof(data),P4_WAD_PURE_HADES));put32(data+8,4096);
    put32(data+4096+16,8191);assert(!validate(data,sizeof(data),P4_WAD_PURE_HADES));put32(data+4096+16,12);
    data[18]=1;assert(!validate(data,sizeof(data),P4_WAD_PURE_HADES));data[18]=11;
    data[4096+11*16+8]='X';assert(!validate(data,sizeof(data),P4_WAD_PURE_HADES));data[4096+11*16+8]='M';
    put32(data+4096+2*16+4,13);assert(!validate(data,sizeof(data),P4_WAD_PURE_HADES));put32(data+4096+2*16+4,14);
    assert(validate(data,sizeof(data),P4_WAD_PURE_HADES));
    puts("verified WAD: bad magic/count/directory/lump bounds/record size/start count/map identity rejected");
    return 0;
}
