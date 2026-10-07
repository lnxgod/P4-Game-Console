/* SPDX-License-Identifier: MIT */
#include "p4/video_presenter.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum { COPY, QUEUE, BACKEND, REUSE, TRANSFORM, HANDOFF, PPA, PHASE_COUNT };
typedef struct { atomic_uint samples, total_us, maximum_us; } phase_t;
struct p4_game_video_presenter {
    p4_game_video_config_t config;
    display_worker_t *worker;
    phase_t phases[PHASE_COUNT];
    atomic_uint accepted, completed, backend_timeouts, wait_timeouts;
    atomic_int hard_error, backend_core;
    atomic_uint stack_free_bytes;
    atomic_bool saturated;
    /* Foreground-only lifetime and initial backend completion barrier. */
    bool closing, first_pending, first_complete;
};

static uint32_t bounded(p4_game_video_presenter_t *p, uint64_t value)
{
    if (value <= UINT32_MAX) return (uint32_t)value;
    atomic_store_explicit(&p->saturated, true, memory_order_relaxed);
    return UINT32_MAX;
}
/* Each counter has exactly one writer; readers only use atomic loads. */
static void add(p4_game_video_presenter_t *p, atomic_uint *counter, uint64_t value)
{
    const uint32_t before = atomic_load_explicit(counter, memory_order_relaxed);
    atomic_store_explicit(counter, bounded(p, (uint64_t)before + value),
                          memory_order_relaxed);
}
static void record(p4_game_video_presenter_t *p, unsigned index, uint64_t us)
{
    phase_t *phase = &p->phases[index];
    add(p, &phase->samples, 1U);
    add(p, &phase->total_us, us);
    const uint32_t maximum = atomic_load_explicit(&phase->maximum_us,
                                                 memory_order_relaxed);
    if (us > maximum)
        atomic_store_explicit(&phase->maximum_us, bounded(p, us),
                              memory_order_relaxed);
}
static int backend(void *opaque, const void *pixels, size_t stride, uint32_t timeout)
{
    p4_game_video_presenter_t *p = opaque;
    p4_game_video_backend_metrics_t metrics = {
        .core = -1, .stack_free_bytes = UINT32_MAX};
    const uint64_t start = p->config.now_us();
    const int result = p->config.backend_submit(p->config.backend_context,
        pixels, stride, timeout, &metrics);
    record(p, BACKEND, p->config.now_us() - start);
    atomic_store_explicit(&p->backend_core, metrics.core, memory_order_relaxed);
    const uint32_t stack_minimum = atomic_load_explicit(&p->stack_free_bytes,
                                                       memory_order_relaxed);
    if (metrics.stack_free_bytes < stack_minimum)
        atomic_store_explicit(&p->stack_free_bytes, metrics.stack_free_bytes,
                              memory_order_relaxed);
    if (metrics.valid) {
        record(p, REUSE, metrics.reuse_us);
        record(p, TRANSFORM, metrics.transform_us);
        record(p, HANDOFF, metrics.handoff_us);
        record(p, PPA, metrics.ppa_us);
    }
    if (result == p->config.backend_timeout_error) {
        /* Backend has consumed the source even on timeout. Preserve native
         * continuation, but never count this as a successful first frame. */
        add(p, &p->backend_timeouts, 1U);
        return DW_OK;
    }
    if (result != DW_OK) {
        atomic_store_explicit(&p->hard_error, result, memory_order_release);
        return result;
    }
    add(p, &p->completed, 1U);
    return DW_OK;
}

int p4_game_video_presenter_create(const p4_game_video_config_t *config,
                                   p4_game_video_presenter_t **out)
{
    if (!out || *out || !config || !config->backend_submit ||
        !config->worker_create || !config->now_us ||
        config->backend_timeout_error == DW_OK || !config->width ||
        !config->height || config->width > SIZE_MAX / sizeof(uint16_t) / config->height)
        return DW_INVALID;
    p4_game_video_presenter_t *p = calloc(1U, sizeof(*p));
    if (!p) return DW_NO_MEMORY;
    p->config = *config;
    for (unsigned i = 0U; i < PHASE_COUNT; ++i) {
        atomic_init(&p->phases[i].samples, 0U);
        atomic_init(&p->phases[i].total_us, 0U);
        atomic_init(&p->phases[i].maximum_us, 0U);
    }
    atomic_init(&p->accepted, 0U);
    atomic_init(&p->completed, 0U);
    atomic_init(&p->backend_timeouts, 0U);
    atomic_init(&p->wait_timeouts, 0U);
    atomic_init(&p->hard_error, DW_OK);
    atomic_init(&p->backend_core, -1);
    atomic_init(&p->stack_free_bytes, UINT32_MAX);
    atomic_init(&p->saturated, false);
    const int result = config->worker_create(backend, p, config->width,
        config->height, sizeof(uint16_t), &p->worker);
    if (result != DW_OK) { free(p); return result; }
    *out = p;
    return DW_OK;
}
static int wait_result(p4_game_video_presenter_t *p, int result)
{
    if (result == DW_TIMEOUT) add(p, &p->wait_timeouts, 1U);
    return result;
}
static uint32_t remaining(p4_game_video_presenter_t *p, uint64_t start,
                           uint32_t timeout)
{
    const uint64_t elapsed_ms = (p->config.now_us() - start) / 1000U;
    return elapsed_ms >= timeout ? 0U : timeout - (uint32_t)elapsed_ms;
}
int p4_game_video_presenter_flush(p4_game_video_presenter_t *p, uint32_t timeout)
{
    if (!p) return DW_INVALID;
    const int result = display_worker_flush(p->worker, timeout);
    if (result == DW_OK) {
        p->first_pending = false;
        if (atomic_load_explicit(&p->completed, memory_order_relaxed) != 0U)
            p->first_complete = true;
    }
    return wait_result(p, result);
}
int p4_game_video_presenter_present(p4_game_video_presenter_t *p,
    const uint16_t *source, size_t stride, uint32_t wait_ms, uint32_t submit_ms)
{
    if (!p || !source || p->closing || stride < p->config.width ||
        stride > SIZE_MAX / sizeof(*source) / p->config.height) return DW_INVALID;
    const uint64_t began = p->config.now_us();
    if (p->first_pending) {
        const int result = p4_game_video_presenter_flush(p, wait_ms);
        if (result != DW_OK) return result;
    }
    display_worker_lease_t lease = {0};
    uint64_t queue_start = p->config.now_us();
    int result = display_worker_acquire(p->worker, &lease,
                                        remaining(p, began, wait_ms));
    uint64_t queue_us = p->config.now_us() - queue_start;
    if (result != DW_OK) {
        record(p, QUEUE, queue_us);
        return wait_result(p, result);
    }
    const uint64_t copy_start = p->config.now_us();
    const size_t row_bytes = p->config.width * sizeof(*source);
    if (stride == p->config.width) {
        memcpy(lease.pixels, source, row_bytes * p->config.height);
    } else {
        uint16_t *dest = lease.pixels;
        for (size_t y = 0U; y < p->config.height; ++y)
            memcpy(dest + y * p->config.width, source + y * stride, row_bytes);
    }
    record(p, COPY, p->config.now_us() - copy_start);
    queue_start = p->config.now_us();
    result = display_worker_commit(p->worker, &lease,
        remaining(p, began, wait_ms), submit_ms);
    queue_us += p->config.now_us() - queue_start;
    record(p, QUEUE, queue_us);
    if (result != DW_OK) {
        const int cancelled = display_worker_cancel(p->worker, &lease);
        return cancelled == DW_OK ? wait_result(p, result) : cancelled;
    }
    add(p, &p->accepted, 1U);
    if (!p->first_complete) {
        p->first_pending = true;
        result = p4_game_video_presenter_flush(p, remaining(p, began, wait_ms));
        if (result != DW_OK) return result;
        /* A consumed-but-timed-out backend frame is not barrier success. */
        if (!p->first_complete) return DW_TIMEOUT;
    }
    return DW_OK;
}
static p4_game_video_phase_t phase_snapshot(const phase_t *phase)
{
    return (p4_game_video_phase_t){
        atomic_load_explicit(&phase->samples, memory_order_relaxed),
        atomic_load_explicit(&phase->total_us, memory_order_relaxed),
        atomic_load_explicit(&phase->maximum_us, memory_order_relaxed)};
}
int p4_game_video_presenter_stats(p4_game_video_presenter_t *p,
                                 p4_game_video_stats_t *out)
{
    if (!p || !out) return DW_INVALID;
    *out = (p4_game_video_stats_t){
        .accepted = atomic_load_explicit(&p->accepted, memory_order_relaxed),
        .completed = atomic_load_explicit(&p->completed, memory_order_relaxed),
        .backend_timeouts = atomic_load_explicit(&p->backend_timeouts, memory_order_relaxed),
        .wait_timeouts = atomic_load_explicit(&p->wait_timeouts, memory_order_relaxed),
        .hard_error = atomic_load_explicit(&p->hard_error, memory_order_acquire),
        .backend_core = atomic_load_explicit(&p->backend_core, memory_order_relaxed),
        .stack_free_bytes = atomic_load_explicit(&p->stack_free_bytes, memory_order_relaxed),
        .closing = p->closing,
        .saturated = atomic_load_explicit(&p->saturated, memory_order_relaxed),
        .copy = phase_snapshot(&p->phases[COPY]),
        .queue = phase_snapshot(&p->phases[QUEUE]),
        .backend = phase_snapshot(&p->phases[BACKEND]),
        .reuse = phase_snapshot(&p->phases[REUSE]),
        .transform = phase_snapshot(&p->phases[TRANSFORM]),
        .handoff = phase_snapshot(&p->phases[HANDOFF]),
        .ppa = phase_snapshot(&p->phases[PPA]),
    };
    return DW_OK;
}
int p4_game_video_presenter_stop(p4_game_video_presenter_t **presenter,
    uint32_t timeout, p4_game_video_stats_t *final_stats)
{
    if (!presenter || !*presenter) return DW_INVALID;
    p4_game_video_presenter_t *p = *presenter;
    p->closing = true;
    const int result = display_worker_stop(&p->worker, timeout);
    if (result != DW_OK) return result;
    if (final_stats) (void)p4_game_video_presenter_stats(p, final_stats);
    free(p);
    *presenter = NULL;
    return DW_OK;
}
