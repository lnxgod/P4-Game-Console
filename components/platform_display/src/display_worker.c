/* Shared single-producer display service core. */
#include "platform/display_worker.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct display_worker {
    display_worker_ops_t ops;
    void *context, *task;
    void *pixels[2];
    size_t width;
    uint32_t submit_timeout_ms;
    atomic_int pending, error;
    atomic_bool closing, busy[2];
    atomic_uint completed, failures;
    /* Foreground-only fields; the worker never touches leases/counters. */
    int held;
    uint64_t generation;
    uint32_t accepted, acquire_waits, commit_waits;
};

static void run(void *argument)
{
    display_worker_t *w = argument;
    for (;;) {
        const int slot = atomic_load_explicit(&w->pending, memory_order_acquire);
        if (slot >= 0) {
            /* pending remains occupied throughout the synchronous backend
             * call. No second queued frame, no replacement/drop policy. */
            const int result = w->ops.submit(w->context, w->pixels[slot],
                                             w->width, w->submit_timeout_ms);
            if (result != DW_OK) {
                atomic_store_explicit(&w->error, result, memory_order_release);
                atomic_fetch_add_explicit(&w->failures, 1U, memory_order_relaxed);
            } else {
                atomic_fetch_add_explicit(&w->completed, 1U, memory_order_relaxed);
            }
            atomic_store_explicit(&w->busy[slot], false, memory_order_release);
            atomic_store_explicit(&w->pending, -1, memory_order_release);
            continue;
        }
#ifdef DISPLAY_WORKER_TEST_HOOKS
        extern void display_worker_test_after_idle_observation(void);
        display_worker_test_after_idle_observation();
#endif
        if (atomic_load_explicit(&w->closing, memory_order_acquire)) {
            /* Closing publishes after the final foreground commit. Re-read
             * pending after that acquire: an earlier empty observation may
             * predate both commit and close. Drain that accepted final frame. */
            if (atomic_load_explicit(&w->pending, memory_order_acquire) < 0) return;
            continue;
        }
        w->ops.pause();
    }
}

static int admission(display_worker_t *w)
{
    const int error = atomic_load_explicit(&w->error, memory_order_acquire);
    if (error != DW_OK) return error;
    return atomic_load_explicit(&w->closing, memory_order_acquire) ? DW_INVALID : DW_OK;
}

static bool expired(display_worker_t *w, uint64_t start, uint32_t timeout)
{
    return w->ops.now_ms() - start >= timeout;
}

int display_worker_create(const display_worker_ops_t *ops, void *context,
                          size_t width, size_t height, size_t pixel_bytes,
                          display_worker_t **out)
{
    if (!out || *out || !ops || !ops->pixels_alloc || !ops->pixels_free ||
        !ops->now_ms || !ops->pause || !ops->start || !ops->join || !ops->submit ||
        (pixel_bytes != 2U && pixel_bytes != 4U) || width == 0U || height == 0U ||
        width > SIZE_MAX / pixel_bytes / height)
        return DW_INVALID;
    display_worker_t *w = calloc(1U, sizeof(*w));
    if (!w) return DW_NO_MEMORY;
    w->ops = *ops; w->context = context; w->width = width;
    w->held = -1;
    atomic_init(&w->pending, -1); atomic_init(&w->error, DW_OK);
    atomic_init(&w->closing, false);
    atomic_init(&w->completed, 0U); atomic_init(&w->failures, 0U);
    for (unsigned i = 0U; i < 2U; ++i) {
        atomic_init(&w->busy[i], false);
        w->pixels[i] = ops->pixels_alloc(width * height * pixel_bytes);
        if (!w->pixels[i]) {
            for (unsigned j = 0U; j < i; ++j) ops->pixels_free(w->pixels[j]);
            free(w); return DW_NO_MEMORY;
        }
        memset(w->pixels[i], 0, width * height * pixel_bytes);
    }
    const int result = ops->start(run, w, &w->task);
    if (result != DW_OK) {
        ops->pixels_free(w->pixels[0]); ops->pixels_free(w->pixels[1]); free(w);
        return result;
    }
    *out = w;
    return DW_OK;
}

int display_worker_acquire(display_worker_t *w, display_worker_lease_t *lease,
                           uint32_t timeout)
{
    if (!w || !lease || w->held >= 0) return DW_INVALID;
    const uint64_t start = w->ops.now_ms();
    for (;;) {
        const int status = admission(w);
        if (status != DW_OK) return status;
        for (unsigned i = 0U; i < 2U; ++i) {
            if (!atomic_load_explicit(&w->busy[i], memory_order_acquire)) {
                atomic_store_explicit(&w->busy[i], true, memory_order_relaxed);
                w->held = (int)i;
                if (++w->generation == 0U) ++w->generation;
                *lease = (display_worker_lease_t){w->pixels[i], w->generation, i};
                return DW_OK;
            }
        }
        if (expired(w, start, timeout)) return DW_TIMEOUT;
        ++w->acquire_waits; w->ops.pause();
    }
}

static bool valid(display_worker_t *w, const display_worker_lease_t *lease)
{
    return w && lease && w->held >= 0 && lease->slot < 2U &&
        (unsigned)w->held == lease->slot && lease->generation == w->generation &&
        lease->pixels == w->pixels[lease->slot];
}

int display_worker_commit(display_worker_t *w, display_worker_lease_t *lease,
                          uint32_t timeout, uint32_t submit_timeout_ms)
{
    if (!valid(w, lease)) return DW_INVALID;
    const uint64_t start = w->ops.now_ms();
    for (;;) {
        const int status = admission(w);
        if (status != DW_OK) return status;
        if (atomic_load_explicit(&w->pending, memory_order_acquire) < 0) {
            /* The worker publishes an error before releasing pending. Reload
             * after observing completion so a failure cannot admit a frame. */
            const int latest = admission(w);
            if (latest != DW_OK) return latest;
            w->submit_timeout_ms = submit_timeout_ms;
            atomic_store_explicit(&w->pending, w->held, memory_order_release);
            w->held = -1; ++w->accepted;
            memset(lease, 0, sizeof(*lease));
            return DW_OK;
        }
        if (expired(w, start, timeout)) return DW_TIMEOUT;
        ++w->commit_waits; w->ops.pause();
    }
}

int display_worker_cancel(display_worker_t *w, display_worker_lease_t *lease)
{
    if (!valid(w, lease)) return DW_INVALID;
    atomic_store_explicit(&w->busy[lease->slot], false, memory_order_release);
    w->held = -1; memset(lease, 0, sizeof(*lease));
    return DW_OK;
}

int display_worker_flush(display_worker_t *w, uint32_t timeout)
{
    if (!w || w->held >= 0) return DW_INVALID;
    const uint64_t start = w->ops.now_ms();
    while (atomic_load_explicit(&w->pending, memory_order_acquire) >= 0) {
        if (expired(w, start, timeout)) return DW_TIMEOUT;
        w->ops.pause();
    }
    return atomic_load_explicit(&w->error, memory_order_acquire);
}

int display_worker_stats(display_worker_t *w, display_worker_stats_t *out)
{
    if (!w || !out) return DW_INVALID;
    *out = (display_worker_stats_t){
        w->accepted, atomic_load_explicit(&w->completed, memory_order_relaxed),
        atomic_load_explicit(&w->failures, memory_order_relaxed),
        w->acquire_waits, w->commit_waits,
        atomic_load_explicit(&w->error, memory_order_acquire),
        atomic_load_explicit(&w->closing, memory_order_acquire)};
    return DW_OK;
}

int display_worker_stop(display_worker_t **worker, uint32_t timeout)
{
    if (!worker || !*worker || (*worker)->held >= 0) return DW_INVALID;
    display_worker_t *w = *worker;
    atomic_store_explicit(&w->closing, true, memory_order_release);
    const int result = w->ops.join(w->task, timeout);
    if (result != DW_OK) return result;
    w->ops.pixels_free(w->pixels[0]); w->ops.pixels_free(w->pixels[1]);
    free(w); *worker = NULL;
    return DW_OK;
}
