// SPDX-License-Identifier: GPL-2.0-or-later
/* Host tests reuse the repository's portable SHA-256 implementation. Firmware
 * continues to use its pinned mbedTLS provider through the actual adapter. */
#include "checkpoint_host_sha.h"
#include "sha256.h"

bool p4_doom_checkpoint_sha256(const uint8_t *bytes, size_t length,
    uint8_t output[32])
{
    if (!bytes || !output) return false;
    p4_game_save_sha256_t context;
    p4_game_save_sha256_init(&context);
    p4_game_save_sha256_update(&context, bytes, length);
    p4_game_save_sha256_finish(&context, output);
    return true;
}
