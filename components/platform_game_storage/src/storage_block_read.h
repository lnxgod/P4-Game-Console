// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* The caller serializes the stream's descriptor, reads and close. Do not mix
 * these reads with stdio reads or seeks on the same stream. */
bool p4_storage_read_block(FILE *stream, size_t offset, void *out, size_t bytes);

enum { P4_STORAGE_BATCH_BYTES = 65536 };

/* Startup integrity scans may use larger bounded descriptor requests. This
 * does not change the 4096-byte block/cache contract above. The caller supplies
 * bytes of output capacity; both APIs share the stream ownership rules above.
 * A failure may leave a copied prefix and an advanced descriptor cursor. */
bool p4_storage_read_batch(FILE *stream, size_t offset, void *out, size_t bytes);

/* An exclusively owned descriptor may avoid a redundant seek before a read at
 * its known next byte. Initialize with {.stream = newly_opened_stream}, and
 * reset the entire cursor on close/reopen or before any external descriptor or
 * stdio operation. Do not combine cursor and stateless reads on that stream.
 * The caller serializes this state with every read and close. Successful reads
 * update it; any attempted-I/O failure invalidates it so the next read seeks.
 * Invalid arguments and zero-length requests perform no I/O or state change.
 * Each nonempty request still performs read(), including its storage checks. */
typedef struct {
    FILE *stream;
    size_t offset;
    bool known;
} p4_storage_cursor_t;

bool p4_storage_cursor_read_block(p4_storage_cursor_t *cursor, size_t offset,
                                  void *out, size_t bytes);
bool p4_storage_cursor_read_batch(p4_storage_cursor_t *cursor, size_t offset,
                                  void *out, size_t bytes);
