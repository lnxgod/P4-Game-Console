// SPDX-License-Identifier: MIT
#ifndef P4_DOOM_REPLAY_H
#define P4_DOOM_REPLAY_H
#include "p4/doom_lockstep.h"

/* Storage is supplied/owned by the caller (PSRAM on Tab5). No allocation,
 * resizing, I/O, or coupling to live-peer acknowledgments occurs.
 * Each canonical tic costs 40 bytes. Linear mode retains the complete match
 * until capacity is exhausted (4 MiB: 104857 tics, ~49.9 min). Rolling mode
 * always retains the newest capacity tics (168000 bytes: 4200 tics, 120 sec).
 * The available packet range is [first_tick, next_tick). Rolling appends evict
 * the oldest tic automatically: the caller must abort any dependent replay
 * BEFORE appending that tic; this journal never pins storage or waits on peers.
 * A missing/noncanonical append, tic wrap, or linear capacity overflow disables
 * this journal; callers must keep the live match running regardless of result.
 * Initialize only for a new match; storage must outlive this object. */
typedef struct {
    uint8_t *storage;
    size_t capacity;
    uint32_t first_tick;
    uint32_t next_tick;
    uint8_t count;
    bool available;
    bool rolling;
} p4_doom_replay_journal_t;

bool p4_doom_replay_journal_init(p4_doom_replay_journal_t *, void *storage,
                                size_t storage_bytes, uint8_t count);
bool p4_doom_replay_journal_init_rolling(p4_doom_replay_journal_t *, void *storage,
                                        size_t storage_bytes, uint8_t count);
bool p4_doom_replay_journal_append(p4_doom_replay_journal_t *,
                                  const p4_doom_lockstep_frame_t *);
bool p4_doom_replay_journal_packet(const p4_doom_replay_journal_t *, uint32_t tick,
                                  uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES]);
bool p4_doom_replay_journal_available(const p4_doom_replay_journal_t *);
#endif
