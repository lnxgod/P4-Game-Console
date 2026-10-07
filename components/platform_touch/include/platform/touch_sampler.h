#ifndef PLATFORM_TOUCH_SAMPLER_H
#define PLATFORM_TOUCH_SAMPLER_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/touch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_TOUCH_SAMPLER_SAMPLE_HZ 60U
#define PLATFORM_TOUCH_SAMPLER_MAX_AGE_US 50000U
#define PLATFORM_TOUCH_SAMPLER_QUEUE_CAPACITY 8U

typedef struct platform_touch_sampler platform_touch_sampler_t;

typedef struct {
    /** Accepted publication attempts, including terminal fault publications. */
    uint64_t samples;
    /** Actual hardware poll invocations, including failed calls. */
    uint64_t poll_calls;
    uint64_t poll_total_us;
    uint32_t poll_max_us;
    uint32_t overflows;
    uint32_t stale_neutralizations;
    /** Episodes with no successful publication for more than MAX_AGE_US. */
    uint32_t producer_stalls;
    /** First successful publications after those episodes. */
    uint32_t recoveries;
    /** Completion time of the last successful sample, or zero before one. */
    int64_t last_completed_us;
    esp_err_t fault;
} platform_touch_sampler_stats_t;

/**
 * Start the Tab5 platform-owned 60 Hz touch sampler.
 *
 * The worker exclusively owns polling until stop succeeds. Direct touch poll,
 * destroy and other controller operations must not run while claimed. It uses
 * a 4096-byte, unaffined priority-2 task and the existing shared I2C service.
 * The caller continues to own the touch handle and its borrowed bus.
 * Initialize *out_sampler to NULL; a non-NULL handle is rejected unchanged.
 *
 * On ordinary failure, *out_sampler is NULL. If undoing a successful claim
 * fails, *out_sampler retains a teardown-only handle; call stop again before
 * destroying touch or its bus. No worker is active in that failure case.
 */
esp_err_t platform_touch_sampler_start(
    platform_touch_t *touch, platform_touch_sampler_t **out_sampler);

/**
 * Take one copied sample without waiting for hardware. Drain available frames
 * in order so brief presses/releases survive a slower rendering loop.
 * Stale scheduling/contact data and overflow emit a neutral release boundary.
 * Successful fresh samples under the same owner recover without lifecycle
 * work. Hardware or malformed-frame faults remain terminal until restart.
 * *available distinguishes a fresh frame/neutralization from no new input.
 */
esp_err_t platform_touch_sampler_read(
    platform_touch_sampler_t *sampler, platform_touch_frame_t *out_frame,
    bool *available);

/** Discard queued input, e.g. when a debug input source takes ownership. */
void platform_touch_sampler_discard(platform_touch_sampler_t *sampler);

/** Copy counters under the short queue lock; never accesses hardware. */
esp_err_t platform_touch_sampler_snapshot(
    platform_touch_sampler_t *sampler, platform_touch_sampler_stats_t *out_stats);

/**
 * Request stop and wait up to the supplied monotonic wait budget. Scheduler
 * dispatch can delay this call's return; the worker is never forcibly deleted.
 * On timeout or claim-release failure, *sampler remains owned and retryable.
 * Only success proves the worker has made its final context access and the
 * touch handle can be reused/destroyed; then *sampler is NULL.
 *
 * Lifecycle is caller-serialized: start/stop and touch lifecycle operations
 * must not overlap. The caller must also exclude read/discard/snapshot while
 * stop may free the sampler. Reads/counters themselves share only a short
 * copy lock with the worker and cannot block on I2C.
 */
esp_err_t platform_touch_sampler_stop(
    platform_touch_sampler_t **sampler, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
