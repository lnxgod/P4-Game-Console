// SPDX-License-Identifier: MIT
#include "p4/doom_replay.h"
#include <string.h>

bool p4_doom_replay_journal_init(p4_doom_replay_journal_t *j, void *storage,
                                size_t storage_bytes, uint8_t count)
{
    if (!j) return false;
    *j = (p4_doom_replay_journal_t){0};
    if (!storage || storage_bytes < P4_DOOM_LOCKSTEP_BYTES ||
        count < 2 || count > P4_MP_MAX_PLAYERS) return false;
    j->storage = storage;
    j->capacity = storage_bytes / P4_DOOM_LOCKSTEP_BYTES;
    if (j->capacity > UINT32_MAX) j->capacity = UINT32_MAX;
    j->count = count;
    j->available = true;
    return true;
}

bool p4_doom_replay_journal_init_rolling(p4_doom_replay_journal_t *j, void *storage,
                                        size_t storage_bytes, uint8_t count)
{
    if (!p4_doom_replay_journal_init(j, storage, storage_bytes, count)) return false;
    j->rolling = true;
    return true;
}

bool p4_doom_replay_journal_append(p4_doom_replay_journal_t *j,
                                  const p4_doom_lockstep_frame_t *frame)
{
    if (!j || !j->available) return false;
    uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES];
    if (!frame || frame->tick != j->next_tick || j->next_tick == UINT32_MAX ||
        (!j->rolling && (size_t)j->next_tick >= j->capacity) ||
        !p4_doom_lockstep_frame_encode(frame, j->count, bytes)) {
        j->available = false;
        return false;
    }
    const size_t index = j->rolling ? (size_t)j->next_tick % j->capacity
                                    : (size_t)j->next_tick;
    memcpy(j->storage + index * P4_DOOM_LOCKSTEP_BYTES,
           bytes, sizeof(bytes));
    if (j->rolling && (size_t)(j->next_tick - j->first_tick) == j->capacity)
        ++j->first_tick;
    ++j->next_tick;
    return true;
}

bool p4_doom_replay_journal_packet(const p4_doom_replay_journal_t *j, uint32_t tick,
                                  uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES])
{
    if (!j || !j->available || !bytes || tick < j->first_tick || tick >= j->next_tick)
        return false;
    const size_t index = j->rolling ? (size_t)tick % j->capacity : (size_t)tick;
    memcpy(bytes, j->storage + index * P4_DOOM_LOCKSTEP_BYTES,
           P4_DOOM_LOCKSTEP_BYTES);
    return true;
}

bool p4_doom_replay_journal_available(const p4_doom_replay_journal_t *j)
{ return j && j->available; }
