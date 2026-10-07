// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef P4_DOOM_CHECKPOINT_H
#define P4_DOOM_CHECKPOINT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "p4/doom_arena.h"

#define P4_DOOM_CHECKPOINT_MAX_BYTES (512U * 1024U)
#define P4_DOOM_CHECKPOINT_CONTENT_MAX 256U

/* Engine-owner task only, between tics; never concurrent/reentrant. Init
 * allocates bounded scratch in PSRAM on ESP32, outside the Doom zone. A failed
 * init disables checkpoints without affecting ordinary gameplay. Outputs are
 * published only on success. Set the exact OS-accepted session content identity
 * before use (currently P4CK2| followed by 64 compatibility SHA-256 hex digits).
 * The setter owns a bounded copy; Shutdown releases scratch and clears identity.
 * Capture can defer unsupported/tombstone boundaries; retain the last snapshot.
 * Restore is destructive after header validation: ANY failure requires the
 * coordinator to abort this cold guest; never continue its game or renderer.
 * The caller must authenticate metadata and verify SHA-256 before Restore.
 * Internal FNV is an integrity/schema check, not wire authentication. */
bool P4_DoomCheckpointInit(void);
void P4_DoomCheckpointShutdown(void);
bool P4_DoomCheckpointSetContentIdentity(const char *identity);
bool P4_DoomCheckpointCapture(uint8_t *buffer, size_t capacity, size_t *length,
                              uint32_t *next_tic, uint8_t *mask);
bool P4_DoomCheckpointRestore(const uint8_t *buffer, size_t length,
                              uint32_t *next_tic, uint8_t *mask);
const char *P4_DoomCheckpointReason(void);

/* Arena owner supplies these; default hooks reject unsupported applications. */
void p4_doom_gc_checkpoint_get_arena(p4_doom_arena_t *out);
bool p4_doom_gc_checkpoint_set_arena(const p4_doom_arena_t *in);
#endif
