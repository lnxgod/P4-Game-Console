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
    uint8_t data[4096]={0};
    static const char names[24][9]={"MAP01","THINGS","LINEDEFS","SIDEDEFS","VERTEXES","SEGS","SSECTORS","NODES","SECTORS","REJECT","BLOCKMAP",
        "MAP02","THINGS","LINEDEFS","SIDEDEFS","VERTEXES","SEGS","SSECTORS","NODES","SECTORS","REJECT","BLOCKMAP","D_RUNNIN","D_STALKS"};
    static const size_t sizes[11]={0,40,14,30,4,12,4,28,26,1,10};
    memcpy(data,"PWAD",4);put32(data+4,24);put32(data+8,2048);
    size_t at=12;
    for(size_t i=0;i<24;++i) {
        const size_t n=i<22?sizes[i%11]:14;
        uint8_t *entry=data+2048+i*16;
        put32(entry,at);put32(entry+4,n);memcpy(entry+8,names[i],8);
        if(i<22 && i%11==1) for(unsigned j=0;j<4;++j) data[at+j*10+6]=11;
        if(i>=22) memcpy(data+at,"MThd",4);
        at+=n;
    }
    assert(validate(data,sizeof(data),true));
    assert(!validate(data,sizeof(data),false));
    put32(data+4,UINT32_MAX); assert(!validate(data,sizeof(data),true));put32(data+4,24);
    put32(data+8,4090);assert(!validate(data,sizeof(data),true));put32(data+8,2048);
    put32(data+2048+16,4095);assert(!validate(data,sizeof(data),true));put32(data+2048+16,12);
    data[18]=1;assert(!validate(data,sizeof(data),true));data[18]=11;
    data[2048+11*16+8]='X';assert(!validate(data,sizeof(data),true));data[2048+11*16+8]='M';
    put32(data+2048+2*16+4,13);assert(!validate(data,sizeof(data),true));put32(data+2048+2*16+4,14);
    assert(validate(data,sizeof(data),true));
    puts("verified WAD: bad magic/count/directory/lump bounds/record size/start count/map identity rejected");
    return 0;
}
