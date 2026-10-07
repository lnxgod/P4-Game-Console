// SPDX-License-Identifier: MIT
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned read_calls, seek_calls;
static size_t largest_read, short_limit;
static unsigned read_interrupts, seek_interrupts;
static unsigned read_error_after;
static bool read_error, seek_error, return_eof, wrong_seek_position;

static ssize_t tracked_read(int fd, void *out, size_t bytes)
{
    ++read_calls;
    if (bytes > largest_read) largest_read = bytes;
    if (read_interrupts) { --read_interrupts; errno = EINTR; return -1; }
    if (read_error && read_calls > read_error_after) { errno = EIO; return -1; }
    if (return_eof) return 0;
    if (short_limit && bytes > short_limit) bytes = short_limit;
    return read(fd, out, bytes);
}

static off_t tracked_lseek(int fd, off_t offset, int whence)
{
    ++seek_calls;
    if (seek_interrupts) { --seek_interrupts; errno = EINTR; return -1; }
    if (seek_error) { errno = EIO; return -1; }
    if (wrong_seek_position) return offset + 1;
    return lseek(fd, offset, whence);
}

/* Run the production helper with observable descriptor I/O. Fault injection
 * is limited to system-call results; successful reads use a real file. */
#define read tracked_read
#define lseek tracked_lseek
#include "../src/storage_block_read.c"
#undef read
#undef lseek

static void reset(void)
{
    read_calls = seek_calls = read_interrupts = seek_interrupts = 0;
    read_error_after = 0;
    largest_read = short_limit = 0;
    read_error = seek_error = return_eof = wrong_seek_position = false;
}

int main(void)
{
    uint8_t source[8192], out[4096];
    for (size_t i = 0; i < sizeof(source); ++i) source[i] = (uint8_t)i;
    FILE *stream = tmpfile();
    assert(stream && fwrite(source, 1, sizeof(source), stream) == sizeof(source));
    assert(fflush(stream) == 0);

    assert(p4_storage_read_block(stream, 512, out, sizeof(out)));
    assert(memcmp(out, source + 512, sizeof(out)) == 0);
    /* A full block reaches the descriptor in one call, rather than thousands
     * of one-byte reads through pinned Newlib's unbuffered fread. */
    assert(read_calls == 1 && largest_read == sizeof(out) && seek_calls == 1);
    assert(lseek(fileno(stream), 0, SEEK_CUR) == 4608);

    reset(); short_limit = 701; read_interrupts = seek_interrupts = 1;
    assert(p4_storage_read_block(stream, 4096, out, sizeof(out)));
    assert(memcmp(out, source + 4096, sizeof(out)) == 0);
    assert(read_calls == 7 && seek_calls == 2 && largest_read == sizeof(out));

    reset(); return_eof = true;
    assert(!p4_storage_read_block(stream, 0, out, 1));
    assert(read_calls == 1);
    reset(); read_error = true;
    assert(!p4_storage_read_block(stream, 0, out, 1));
    assert(read_calls == 1);
    reset(); read_error = true; read_error_after = 1; short_limit = 2;
    assert(!p4_storage_read_block(stream, 0, out, 4));
    assert(read_calls == 2);
    reset(); seek_error = true;
    assert(!p4_storage_read_block(stream, 0, out, 1));
    assert(read_calls == 0);
    reset(); wrong_seek_position = true;
    assert(!p4_storage_read_block(stream, 0, out, 1));
    assert(read_calls == 0);
    reset(); read_interrupts = UINT_MAX;
    assert(!p4_storage_read_block(stream, 0, out, 1));
    assert(read_calls <= 9);
    reset(); seek_interrupts = UINT_MAX;
    assert(!p4_storage_read_block(stream, 0, out, 1));
    assert(seek_calls <= 9 && read_calls == 0);

    reset();
    assert(!p4_storage_read_block(NULL, 0, out, 1));
    assert(!p4_storage_read_block(stream, 0, NULL, 1));
    assert(!p4_storage_read_block(stream, SIZE_MAX, out, 1));
    assert(!p4_storage_read_block(stream, (size_t)LONG_MAX, out, 1));
    assert(!p4_storage_read_block(stream, 0, out, sizeof(out) + 1));
    assert(p4_storage_read_block(stream, 0, NULL, 0));
    assert(read_calls == 0 && seek_calls == 0);

    reset();
    assert(!p4_storage_read_block(stream, sizeof(source) - 2, out, 4));
    assert(read_calls == 2);
    assert(fclose(stream) == 0);
    puts("storage block reads passed");
    return 0;
}
