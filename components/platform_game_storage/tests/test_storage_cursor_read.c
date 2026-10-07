// SPDX-License-Identifier: MIT
/* Actual provider source with observable descriptor calls. Successful I/O uses
 * a real temporary file. Synthetic syscall results cover LONG_MAX boundaries
 * without creating huge files. No storage service/cache is substituted. */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"CHECK failed at %d: %s\n",__LINE__,#expr); exit(1); \
} } while (0)

static unsigned read_calls, seek_calls, fileno_calls, read_interrupts, seek_interrupts;
static unsigned read_error_after, repeat_interrupts, repeat_remaining;
static size_t largest_read, short_limit;
static bool read_error, seek_error, return_eof, wrong_seek_position;
static bool invalid_descriptor, oversized_count, synthetic_position;
static unsigned cases;

static int tracked_fileno(FILE *stream)
{
    ++fileno_calls;
    return invalid_descriptor ? -1 : fileno(stream);
}

static ssize_t tracked_read(int fd, void *out, size_t bytes)
{
    ++read_calls;
    if (bytes > largest_read) largest_read = bytes;
    if (read_interrupts) { --read_interrupts; errno = EINTR; return -1; }
    if (repeat_remaining) { --repeat_remaining; errno = EINTR; return -1; }
    if (read_error && read_calls > read_error_after) { errno = EIO; return -1; }
    if (return_eof) return 0;
    if (oversized_count) return (ssize_t)(bytes + 1U);
    if (short_limit && bytes > short_limit) bytes = short_limit;
    if (synthetic_position) { memset(out,0x5a,bytes); return (ssize_t)bytes; }
    const ssize_t count = read(fd,out,bytes);
    if (count > 0) repeat_remaining = repeat_interrupts;
    return count;
}

static off_t tracked_lseek(int fd, off_t offset, int whence)
{
    ++seek_calls;
    if (seek_interrupts) { --seek_interrupts; errno = EINTR; return -1; }
    if (seek_error) { errno = EIO; return -1; }
    if (wrong_seek_position) return offset + 1;
    return synthetic_position ? offset : lseek(fd,offset,whence);
}

#define fileno tracked_fileno
#define read tracked_read
#define lseek tracked_lseek
#include "storage_block_read.c"
#undef fileno
#undef read
#undef lseek

#ifdef P4_TEST_CURSOR_BASELINE
/* Same observable contract bound to old stateless APIs for the red control. */
typedef struct { FILE *stream; size_t offset; bool known; } p4_storage_cursor_t;
static bool p4_storage_cursor_read_block(p4_storage_cursor_t *c,size_t o,void *p,size_t n)
{ return c && p4_storage_read_block(c->stream,o,p,n); }
static bool p4_storage_cursor_read_batch(p4_storage_cursor_t *c,size_t o,void *p,size_t n)
{ return c && p4_storage_read_batch(c->stream,o,p,n); }
#endif

static void reset(void)
{
    read_calls=seek_calls=fileno_calls=read_interrupts=seek_interrupts=0;
    read_error_after=repeat_interrupts=repeat_remaining=0;
    largest_read=short_limit=0;
    read_error=seek_error=return_eof=wrong_seek_position=false;
    invalid_descriptor=oversized_count=synthetic_position=false;
}

static void pass(const char *name)
{
    ++cases;
    printf("PASS %s\n",name);
}

struct guarded_output {
    uint8_t before[32];
    uint8_t data[P4_STORAGE_BATCH_BYTES];
    uint8_t after[32];
};

static void guards(const struct guarded_output *out)
{
    for (size_t i=0;i<sizeof(out->before);++i) CHECK(out->before[i]==0xcc);
    for (size_t i=0;i<sizeof(out->after);++i) CHECK(out->after[i]==0xcc);
}

int main(void)
{
    static uint8_t source[131071];
    struct guarded_output out;
    memset(&out,0xcc,sizeof(out));
    for (size_t i=0;i<sizeof(source);++i)
        source[i]=(uint8_t)((i*29U+(i>>8U)+17U)&255U);
    FILE *stream=tmpfile();
    CHECK(stream && fwrite(source,1,sizeof(source),stream)==sizeof(source));
    CHECK(fflush(stream)==0);
    p4_storage_cursor_t cursor={.stream=stream};

    CHECK(p4_storage_cursor_read_batch(&cursor,0,out.data,65536));
    CHECK(memcmp(out.data,source,65536)==0);
    CHECK(p4_storage_cursor_read_batch(&cursor,65536,out.data,65535));
    CHECK(memcmp(out.data,source+65536,65535)==0);
    CHECK(read_calls==2 && fileno_calls==2 && seek_calls==1);
    CHECK(cursor.known && cursor.offset==sizeof(source));
    CHECK(lseek(fileno(stream),0,SEEK_CUR)==(off_t)sizeof(source));
    guards(&out);pass("two sequential full scan batches keep one seek but both reads/status checks");

    reset();
    CHECK(p4_storage_cursor_read_block(&cursor,4096,out.data,4096));
    CHECK(p4_storage_cursor_read_block(&cursor,8192,out.data,4096));
    CHECK(memcmp(out.data,source+8192,4096)==0);
    CHECK(p4_storage_cursor_read_batch(&cursor,12288,out.data,8193));
    CHECK(memcmp(out.data,source+12288,8193)==0);
    CHECK(seek_calls==1 && read_calls==3 && cursor.offset==20481);
    guards(&out);pass("scan to runtime random seek followed by contiguous block and batch reads");

    reset();
    CHECK(p4_storage_cursor_read_block(&cursor,5,out.data,17));
    CHECK(p4_storage_cursor_read_block(&cursor,5,out.data,17));
    CHECK(p4_storage_cursor_read_block(&cursor,4,out.data,17));
    CHECK(seek_calls==3 && read_calls==3 && memcmp(out.data,source+4,17)==0);
    pass("same and backwards offsets seek rather than confuse cached block data");

    reset();const size_t saved=cursor.offset;
    CHECK(p4_storage_cursor_read_batch(&cursor,999,NULL,0));
    CHECK(p4_storage_cursor_read_block(&cursor,(size_t)LONG_MAX,NULL,0));
    CHECK(!p4_storage_cursor_read_batch(&cursor,0,NULL,1));
    CHECK(!p4_storage_cursor_read_batch(&cursor,0,out.data,65537));
    CHECK(!p4_storage_cursor_read_block(&cursor,0,out.data,4097));
    CHECK(!p4_storage_cursor_read_batch(&cursor,SIZE_MAX,out.data,0));
    CHECK(!p4_storage_cursor_read_batch(&cursor,(size_t)LONG_MAX,out.data,1));
    CHECK(!p4_storage_cursor_read_batch(&cursor,(size_t)LONG_MAX-65535U,out.data,65536));
    CHECK(!p4_storage_cursor_read_block(NULL,0,out.data,1));
    p4_storage_cursor_t empty={0};
    CHECK(!p4_storage_cursor_read_batch(&empty,0,NULL,0));
    CHECK(cursor.known && cursor.offset==saved && !empty.known);
    CHECK(read_calls==0 && seek_calls==0 && fileno_calls==0);
    pass("invalid and zero requests preserve state, avoid I/O, reject overflow and nulls");

    reset();synthetic_position=true;cursor.known=false;
    CHECK(p4_storage_cursor_read_batch(&cursor,(size_t)LONG_MAX-65536U,out.data,65536));
    CHECK(cursor.known && cursor.offset==(size_t)LONG_MAX && seek_calls==1);
    CHECK(!p4_storage_cursor_read_block(&cursor,cursor.offset,out.data,1));
    CHECK(read_calls==1 && seek_calls==1);
    pass("exact LONG_MAX end succeeds but next-byte overflow never wraps");

    reset();cursor=(p4_storage_cursor_t){.stream=stream};short_limit=997;
    CHECK(p4_storage_cursor_read_batch(&cursor,123,out.data,65536));
    CHECK(cursor.known && cursor.offset==65659 && read_calls==66 && seek_calls==1);
    CHECK(memcmp(out.data,source+123,65536)==0);
    reset();CHECK(p4_storage_cursor_read_block(&cursor,65659,out.data,17));
    CHECK(!seek_calls && read_calls==1 && memcmp(out.data,source+65659,17)==0);
    pass("positive short reads complete and publish exact next offset");

    for (unsigned fault=0;fault<6;++fault) {
        reset();cursor=(p4_storage_cursor_t){.stream=stream};
        CHECK(p4_storage_cursor_read_block(&cursor,80,out.data,20));
        reset();
        if(fault==0)read_error=true;
        if(fault==1)return_eof=true;
        if(fault==2){read_error=true;read_error_after=1;short_limit=2;}
        if(fault==3)read_interrupts=9;
        if(fault==4)oversized_count=true;
        if(fault==5)invalid_descriptor=true;
        CHECK(!p4_storage_cursor_read_block(&cursor,100,out.data,8));
        CHECK(!cursor.known && seek_calls==0);
        if(fault==2)CHECK(lseek(fileno(stream),0,SEEK_CUR)==102);
        reset();CHECK(p4_storage_cursor_read_block(&cursor,100,out.data,8));
        CHECK(seek_calls==1 && read_calls==1 && cursor.known && cursor.offset==108);
        CHECK(memcmp(out.data,source+100,8)==0);guards(&out);
    }
    pass("all sequential I/O failures invalidate; retry seeks even after partial progress");

    for(unsigned fault=0;fault<3;++fault){
        reset();
        if(fault==0)seek_error=true;
        if(fault==1)wrong_seek_position=true;
        if(fault==2)seek_interrupts=9;
        CHECK(!p4_storage_cursor_read_block(&cursor,0,out.data,1));
        CHECK(!cursor.known && read_calls==0);
        reset();CHECK(p4_storage_cursor_read_block(&cursor,0,out.data,1));
        CHECK(seek_calls==1 && read_calls==1 && out.data[0]==source[0]);
    }
    pass("failed or wrong seeks and exhausted seek interruptions never publish a cursor");

    reset();seek_interrupts=8;read_interrupts=8;
    CHECK(p4_storage_cursor_read_block(&cursor,70,out.data,3));
    CHECK(seek_calls==9 && read_calls==9 && cursor.offset==73);
    reset();read_interrupts=8;
    CHECK(p4_storage_cursor_read_block(&cursor,73,out.data,3));
    CHECK(seek_calls==0 && read_calls==9 && cursor.offset==76);
    pass("bounded eight EINTR retries remain valid on both positioned and sequential I/O");

    reset();short_limit=2;repeat_interrupts=8;repeat_remaining=8;
    CHECK(p4_storage_cursor_read_block(&cursor,76,out.data,6));
    CHECK(seek_calls==0 && read_calls==27 && cursor.offset==82);
    CHECK(memcmp(out.data,source+76,6)==0);
    pass("each successful short read renews only the bounded EINTR allowance");

    reset();
    CHECK(!p4_storage_cursor_read_block(&cursor,sizeof(source)-2,out.data,4));
    CHECK(!cursor.known && read_calls==2);
    reset();CHECK(p4_storage_cursor_read_block(&cursor,sizeof(source)-2,out.data,2));
    CHECK(seek_calls==1 && cursor.offset==sizeof(source));
    CHECK(memcmp(out.data,source+sizeof(source)-2,2)==0);
    pass("real partial EOF invalidates then exact-tail retry repositions");

    FILE *second=tmpfile();CHECK(second);
    uint8_t other[64];memset(other,0x39,sizeof(other));
    CHECK(fwrite(other,1,sizeof(other),second)==sizeof(other) && fflush(second)==0);
    p4_storage_cursor_t cursor2={.stream=second};
    reset();
    CHECK(p4_storage_cursor_read_block(&cursor,0,out.data,16));
    CHECK(p4_storage_cursor_read_block(&cursor2,0,out.data,16) && out.data[0]==0x39);
    CHECK(p4_storage_cursor_read_block(&cursor,16,out.data,16));
    CHECK(memcmp(out.data,source+16,16)==0);
    CHECK(p4_storage_cursor_read_block(&cursor2,16,out.data,16) && out.data[0]==0x39);
    CHECK(seek_calls==2 && read_calls==4 && cursor.offset==32 && cursor2.offset==32);
    CHECK(fclose(second)==0);memset(&cursor2,0,sizeof(cursor2));
    CHECK(!p4_storage_cursor_read_block(&cursor2,32,out.data,1));
    pass("independent retained streams never share cursor state; cleared close rejects reads");

    /* The caller explicitly invalidates after an external descriptor operation. */
    CHECK(lseek(fileno(stream),500,SEEK_SET)==500);
    cursor=(p4_storage_cursor_t){.stream=stream};reset();
    CHECK(p4_storage_cursor_read_block(&cursor,32,out.data,16));
    CHECK(seek_calls==1 && memcmp(out.data,source+32,16)==0);
    guards(&out);pass("explicit cursor reset after external operation restores positioning");

    CHECK(fclose(stream)==0);
    printf("PASS %u focused cursor groups\n",cases);
    return 0;
}
