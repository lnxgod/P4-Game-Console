/* SPDX-License-Identifier: MIT */
#ifndef P4_VIDEO_PRESENTER_H
#define P4_VIDEO_PRESENTER_H
#include "platform/display_worker.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct p4_game_video_presenter p4_game_video_presenter_t;
typedef struct {
    bool valid;
    uint32_t reuse_us, transform_us, handoff_us, ppa_us;
    int core;
    uint32_t stack_free_bytes; /* UINT32_MAX means unavailable. */
} p4_game_video_backend_metrics_t;
typedef struct {
    uint32_t samples, total_us, maximum_us;
} p4_game_video_phase_t;
typedef struct {
    uint32_t accepted, completed, backend_timeouts, wait_timeouts;
    int hard_error, backend_core;
    uint32_t stack_free_bytes; /* Minimum observed; UINT32_MAX until measured. */
    bool closing, saturated;
    p4_game_video_phase_t copy, queue, backend, reuse, transform, handoff, ppa;
} p4_game_video_stats_t;
typedef struct {
    size_t width, height;
    void *backend_context;
    /* Synchronous source consumption, on ALL results. Runs only on worker.
     * Metrics must describe this submission. No cartridge callbacks allowed. */
    int (*backend_submit)(void *, const uint16_t *, size_t, uint32_t,
                          p4_game_video_backend_metrics_t *);
    int backend_timeout_error; /* Only this backend result is nonfatal. */
    uint64_t (*now_us)(void);
    int (*worker_create)(int (*)(void *, const void *, size_t, uint32_t),
                         void *, size_t, size_t, size_t, display_worker_t **);
} p4_game_video_config_t;

/* Single foreground owner, like display_worker. Config and source dimensions
 * are immutable. Creation failure starts no worker and leaves *out NULL;
 * callers may safely retain their original synchronous path in that case. */
int p4_game_video_presenter_create(const p4_game_video_config_t *,
                                   p4_game_video_presenter_t **out);
/* Borrow a writable native frame from the bounded worker. The presenter owns
 * the lease; pixels/stride are valid only until commit or cancel. Render the
 * complete frame, and never cache or write pixels after successful commit.
 * Failure clears both outputs. Only one lease can be held. A pending initial
 * completion barrier is resolved before another writable frame is returned. */
int p4_game_video_presenter_acquire(p4_game_video_presenter_t *,
    uint16_t **pixels, size_t *stride, uint32_t wait_ms);
/* Publish the held frame without copying. Commit backpressure failure retains
 * the writable lease; a subsequent first-frame completion timeout may occur
 * after ownership was transferred. Call cancel before recovering either kind
 * of timeout. First successful backend submission is a completion barrier. */
int p4_game_video_presenter_commit(p4_game_video_presenter_t *,
    uint32_t wait_ms, uint32_t submit_ms);
/* Cancel an unpublished frame; succeeds when no writable lease remains. */
int p4_game_video_presenter_cancel(p4_game_video_presenter_t *);
/* Copies the complete frame before returning; never retains source. First
 * successful backend submission is a completion barrier. A timeout leaves
 * that barrier pending; it is NOT treated as first-frame success. Normal
 * frames use bounded backpressure, not replacement of an in-flight frame.
 * DW_TIMEOUT is nonfatal; other errors must stop the cartridge. */
int p4_game_video_presenter_present(p4_game_video_presenter_t *,
    const uint16_t *source, size_t stride, uint32_t wait_ms, uint32_t submit_ms);
int p4_game_video_presenter_flush(p4_game_video_presenter_t *, uint32_t timeout_ms);
/* Lock-free individual counters; a live snapshot can span completion of one
 * frame. Totals saturate at UINT32_MAX and set saturated. Final stop snapshot
 * is coherent after join. Never acquires the hardware display mutex. */
int p4_game_video_presenter_stats(p4_game_video_presenter_t *, p4_game_video_stats_t *);
/* Cancels any unpublished writable frame, closes admission, drains and joins
 * before freeing anything. Borrowed pixels become invalid. On timeout,
 * retains *presenter and its complete context; caller must retry or hold
 * without drawing, freeing the context, or handing ownership to the launcher.
 * Optional final_stats is filled only after successful join. A prior backend
 * hard error does not prevent joining; it remains in final_stats. */
int p4_game_video_presenter_stop(p4_game_video_presenter_t **presenter,
    uint32_t timeout_ms, p4_game_video_stats_t *final_stats);
#endif
