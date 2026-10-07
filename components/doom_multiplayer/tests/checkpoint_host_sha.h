// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef P4_CHECKPOINT_HOST_SHA_H
#define P4_CHECKPOINT_HOST_SHA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Strong host-test implementation of the adapter's weak non-ESP SHA hook. */
bool p4_doom_checkpoint_sha256(const uint8_t *bytes, size_t length,
    uint8_t output[32]);

#endif
