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

/* The red/negative-control build deliberately binds the proposed batch API
 * to the unchanged baseline 4 KiB reader, proving the 64 KiB case detects the
 * missing capability. The green build calls the actual candidate batch API. */
#ifdef P4_TEST_BASELINE_BATCH
#define P4_STORAGE_BATCH_BYTES 65536U
#define p4_storage_read_batch p4_storage_read_block
#endif
#define fileno tracked_fileno
#define read tracked_read
#define lseek tracked_lseek
#include "storage_block_read.c"
#undef fileno
#undef read
#undef lseek

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

    CHECK(p4_storage_read_batch(stream,512,out.data,P4_STORAGE_BATCH_BYTES));
    CHECK(memcmp(out.data,source+512,P4_STORAGE_BATCH_BYTES)==0);
    CHECK(read_calls==1 && seek_calls==1 && fileno_calls==1);
    CHECK(largest_read==P4_STORAGE_BATCH_BYTES);
    CHECK(lseek(fileno(stream),0,SEEK_CUR)==(off_t)(512U+P4_STORAGE_BATCH_BYTES));
    guards(&out); pass("maximum 65536 bytes, exact contents, one read, cursor and canaries");

    const size_t tails[]={1,17,4097,8193,65535};
    for (size_t i=0;i<sizeof(tails)/sizeof(tails[0]);++i) {
        reset(); memset(out.data,0xcc,sizeof(out.data));
        CHECK(p4_storage_read_batch(stream,4095,out.data,tails[i]));
        CHECK(memcmp(out.data,source+4095,tails[i])==0);
        CHECK(out.data[tails[i]]==0xcc);
        CHECK(read_calls==1 && seek_calls==1 && largest_read==tails[i]);
        CHECK(lseek(fileno(stream),0,SEEK_CUR)==(off_t)(4095U+tails[i]));
        guards(&out);
    }
    pass("one-byte and unaligned tails cross 4 KiB boundaries exactly");

    reset();
    CHECK(!p4_storage_read_batch(stream,0,out.data,P4_STORAGE_BATCH_BYTES+1U));
    CHECK(!p4_storage_read_batch(stream,0,out.data,SIZE_MAX));
    CHECK(!p4_storage_read_block(stream,0,out.data,4097));
    CHECK(!p4_storage_read_block(stream,0,out.data,P4_STORAGE_BATCH_BYTES));
    CHECK(read_calls==0 && seek_calls==0 && fileno_calls==0);
    pass("batch max+1 and SIZE_MAX rejected; block remains at most 4096");

    reset();
    CHECK(!p4_storage_read_batch(NULL,0,out.data,1));
    CHECK(!p4_storage_read_batch(NULL,0,NULL,0));
    CHECK(!p4_storage_read_batch(stream,0,NULL,1));
    CHECK(!p4_storage_read_block(NULL,0,NULL,0));
    CHECK(read_calls==0 && seek_calls==0 && fileno_calls==0);
    pass("null stream/output preconditions reject without I/O");

    reset();
    CHECK(p4_storage_read_batch(stream,0,NULL,0));
    CHECK(p4_storage_read_batch(stream,(size_t)LONG_MAX,NULL,0));
    CHECK(p4_storage_read_block(stream,(size_t)LONG_MAX,NULL,0));
    CHECK(read_calls==0 && seek_calls==0 && fileno_calls==0);
    pass("valid zero requests including LONG_MAX avoid descriptor access");

    reset();
    CHECK(!p4_storage_read_batch(stream,SIZE_MAX,out.data,0));
    CHECK(!p4_storage_read_batch(stream,SIZE_MAX,out.data,1));
    CHECK(!p4_storage_read_batch(stream,(size_t)LONG_MAX,out.data,1));
    CHECK(!p4_storage_read_batch(stream,(size_t)LONG_MAX-65535U,out.data,65536));
    CHECK(!p4_storage_read_block(stream,(size_t)LONG_MAX,out.data,1));
    CHECK(read_calls==0 && seek_calls==0 && fileno_calls==0);
    pass("LONG_MAX offset and end arithmetic overflow reject without I/O");

    reset(); synthetic_position=true;
    CHECK(p4_storage_read_batch(stream,(size_t)LONG_MAX-65536U,out.data,65536));
    CHECK(read_calls==1 && seek_calls==1 && largest_read==65536);
    for (size_t i=0;i<sizeof(out.data);++i) CHECK(out.data[i]==0x5a);
    guards(&out); pass("end exactly LONG_MAX accepted with synthetic syscall positions");

    reset(); invalid_descriptor=true;
    CHECK(!p4_storage_read_batch(stream,0,out.data,1));
    CHECK(fileno_calls==1 && seek_calls==0 && read_calls==0);
    pass("invalid descriptor fails before seek/read");

    reset(); short_limit=997;
    CHECK(p4_storage_read_batch(stream,123,out.data,sizeof(out.data)));
    CHECK(memcmp(out.data,source+123,sizeof(out.data))==0);
    CHECK(read_calls==66 && seek_calls==1 && largest_read==sizeof(out.data));
    guards(&out); pass("positive short reads complete full batch without read-ahead");

    reset(); seek_interrupts=8;
    CHECK(p4_storage_read_batch(stream,0,out.data,1));
    CHECK(seek_calls==9 && read_calls==1);
    pass("eight seek EINTR retries then success");

    reset(); seek_interrupts=9;
    CHECK(!p4_storage_read_batch(stream,0,out.data,1));
    CHECK(seek_calls==9 && read_calls==0);
    pass("ninth consecutive seek EINTR fails");

    reset(); read_interrupts=8;
    CHECK(p4_storage_read_batch(stream,0,out.data,1));
    CHECK(seek_calls==1 && read_calls==9);
    pass("eight read EINTR retries then success");

    reset(); read_interrupts=9;
    CHECK(!p4_storage_read_batch(stream,0,out.data,1));
    CHECK(seek_calls==1 && read_calls==9);
    pass("ninth consecutive read EINTR fails");

    reset(); short_limit=2;repeat_interrupts=8;repeat_remaining=8;
    CHECK(p4_storage_read_batch(stream,5,out.data,6));
    CHECK(read_calls==27 && seek_calls==1 && memcmp(out.data,source+5,6)==0);
    pass("EINTR retry allowance resets after each positive short read");

    reset(); return_eof=true;
    CHECK(!p4_storage_read_batch(stream,0,out.data,1));
    CHECK(read_calls==1 && seek_calls==1);
    pass("immediate EOF fails");

    reset(); memset(out.data,0xcc,sizeof(out.data));
    CHECK(!p4_storage_read_batch(stream,sizeof(source)-2U,out.data,4));
    CHECK(read_calls==2 && seek_calls==1);
    CHECK(memcmp(out.data,source+sizeof(source)-2U,2)==0 && out.data[2]==0xcc);
    CHECK(lseek(fileno(stream),0,SEEK_CUR)==(off_t)sizeof(source));
    guards(&out);pass("EOF after partial read preserves copied prefix and cursor");

    reset(); read_error=true;
    CHECK(!p4_storage_read_batch(stream,0,out.data,1));
    CHECK(read_calls==1 && seek_calls==1);
    pass("non-EINTR read error fails immediately");

    reset(); read_error=true;read_error_after=1;short_limit=2;
    memset(out.data,0xcc,sizeof(out.data));
    CHECK(!p4_storage_read_batch(stream,11,out.data,4));
    CHECK(read_calls==2 && memcmp(out.data,source+11,2)==0 && out.data[2]==0xcc);
    CHECK(lseek(fileno(stream),0,SEEK_CUR)==13);
    guards(&out);pass("read error after partial read preserves prefix and cursor");

    reset(); seek_error=true;
    CHECK(!p4_storage_read_batch(stream,0,out.data,1));
    CHECK(seek_calls==1 && read_calls==0);
    pass("non-EINTR seek error prevents read");

    reset(); wrong_seek_position=true;
    CHECK(!p4_storage_read_batch(stream,0,out.data,1));
    CHECK(seek_calls==1 && read_calls==0);
    pass("wrong seek position prevents read");

    reset(); oversized_count=true;
    CHECK(!p4_storage_read_batch(stream,0,out.data,1));
    CHECK(read_calls==1 && seek_calls==1);
    guards(&out);pass("impossible returned read count above remaining fails");

    reset();
    CHECK(p4_storage_read_block(stream,512,out.data,4096));
    CHECK(memcmp(out.data,source+512,4096)==0);
    CHECK(read_calls==1 && seek_calls==1 && largest_read==4096);
    guards(&out);pass("block maximum retains exact one-read contents");

    CHECK(fclose(stream)==0);
    printf("PASS %u focused provider cases\n",cases);
    return 0;
}
