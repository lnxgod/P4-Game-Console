// SPDX-License-Identifier: GPL-2.0-or-later
#include "doom/video_metrics.h"
#include <stdatomic.h>
#include <stddef.h>
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "video metrics require lock-free atomics");
/* SPSC triple buffer: producer owns back, consumer owns front, middle is
 * exchanged with acquire/release. Only the middle slot may be superseded.
 * Static initialization is once per boot: never memset across game visits. */
enum { DIRTY = 4U, INDEX_MASK = 3U };
static doom_video_metrics_t slots[3];
static atomic_uint middle = ATOMIC_VAR_INIT(1U);
static unsigned back = 2U; /* Producer-only. */
static unsigned front;    /* Consumer-only. */
static uint32_t generation, seen_failures;
static uint64_t sequence, lifetime_failures, last_fault_capture_us;
static uint32_t last_fault_generation;
static int last_fault_error;
static bool exhausted;

uint32_t doom_video_metrics_begin(void)
{
    if (generation == UINT32_MAX) { exhausted = true; return 0U; }
    ++generation;
    seen_failures = 0U;
    return exhausted ? 0U : generation;
}
void doom_video_metrics_publish(const doom_video_metrics_t *snapshot)
{
    if (!snapshot || generation == 0U || exhausted) return;
    if (sequence == UINT64_MAX) { exhausted = true; return; }
    if (snapshot->failures > seen_failures) {
        const uint64_t delta = snapshot->failures - seen_failures;
        lifetime_failures = UINT64_MAX - lifetime_failures < delta
            ? UINT64_MAX : lifetime_failures + delta;
        seen_failures = snapshot->failures;
        last_fault_generation = generation;
        last_fault_capture_us = snapshot->captured_us;
        last_fault_error = snapshot->error;
    }
    doom_video_metrics_t *const record = &slots[back];
    *record = *snapshot;
    record->generation = generation;
    record->sequence = ++sequence;
    record->lifetime_failures = lifetime_failures;
    record->last_fault_generation = last_fault_generation;
    record->last_fault_capture_us = last_fault_capture_us;
    record->last_fault_error = last_fault_error;
    back = atomic_exchange_explicit(&middle, back | DIRTY,
                                   memory_order_acq_rel) & INDEX_MASK;
}
bool doom_video_metrics_take(doom_video_metrics_t *out)
{
    if (!out || !(atomic_load_explicit(&middle, memory_order_acquire) & DIRTY))
        return false;
    front = atomic_exchange_explicit(&middle, front, memory_order_acq_rel)
        & INDEX_MASK;
    *out = slots[front];
    return true;
}
