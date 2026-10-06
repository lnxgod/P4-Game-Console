// SPDX-License-Identifier: MIT
#ifndef P4_TRANSFER_FILE_H
#define P4_TRANSFER_FILE_H

#include <stdbool.h>
#include <unistd.h>

/* Always release the descriptor, including after a failed media sync. Do not
 * short-circuit close: terminal cleanup no longer owns the descriptor. */
static inline bool p4_transfer_sync_close(int *descriptor)
{
    if (*descriptor < 0) {
        return false;
    }
    const int owned = *descriptor;
    *descriptor = -1;
    const bool synced = fsync(owned) == 0;
    const bool closed = close(owned) == 0;
    return synced && closed;
}

#endif
