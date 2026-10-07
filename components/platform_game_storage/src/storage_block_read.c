// SPDX-License-Identifier: MIT
#include "storage_block_read.h"
#include "verified_reader.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

static bool read_bounded(FILE *stream, size_t offset, void *out, size_t bytes,
                         size_t maximum, p4_storage_cursor_t *cursor)
{
    if (!stream || (!out && bytes) || bytes > maximum ||
        offset > (size_t)LONG_MAX || bytes > (size_t)LONG_MAX - offset)
        return false;
    if (!bytes) return true;
    const bool sequential = cursor && cursor->known && cursor->offset == offset;
    if (cursor) cursor->known = false;
    const int fd = fileno(stream);
    if (fd < 0) return false;

    /* The storage mutex owns this descriptor. Leave its cursor advanced so
     * FatFS can retain the current cluster during sequential validation.
     * Pinned Newlib's unbuffered fread refills one byte at a time; direct
     * descriptor reads retain the no-read-ahead contract in bounded requests. */
    enum { MAX_INTERRUPTS = 8 };
    unsigned interruptions = 0;
    if (!sequential) {
        off_t position;
        do {
            position = lseek(fd, (off_t)offset, SEEK_SET);
        } while (position < 0 && errno == EINTR && interruptions++ < MAX_INTERRUPTS);
        if (position != (off_t)offset) return false;
    }
    const size_t end = offset + bytes;

    uint8_t *dest = out;
    while (bytes) {
        interruptions = 0;
        ssize_t count;
        do {
            count = read(fd, dest, bytes);
        } while (count < 0 && errno == EINTR && interruptions++ < MAX_INTERRUPTS);
        if (count <= 0 || (size_t)count > bytes) return false;
        dest += (size_t)count;
        bytes -= (size_t)count;
    }
    if (cursor) {
        cursor->offset = end;
        cursor->known = true;
    }
    return true;
}

bool p4_storage_read_block(FILE *stream, size_t offset, void *out, size_t bytes)
{
    return read_bounded(stream, offset, out, bytes, P4_VERIFIED_BLOCK_BYTES, NULL);
}

bool p4_storage_read_batch(FILE *stream, size_t offset, void *out, size_t bytes)
{
    return read_bounded(stream, offset, out, bytes, P4_STORAGE_BATCH_BYTES, NULL);
}

bool p4_storage_cursor_read_block(p4_storage_cursor_t *cursor, size_t offset,
                                  void *out, size_t bytes)
{
    return cursor && read_bounded(cursor->stream, offset, out, bytes,
                                  P4_VERIFIED_BLOCK_BYTES, cursor);
}

bool p4_storage_cursor_read_batch(p4_storage_cursor_t *cursor, size_t offset,
                                  void *out, size_t bytes)
{
    return cursor && read_bounded(cursor->stream, offset, out, bytes,
                                  P4_STORAGE_BATCH_BYTES, cursor);
}
