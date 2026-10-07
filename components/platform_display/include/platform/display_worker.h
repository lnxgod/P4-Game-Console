/* Shared, bounded single-producer frame ownership service. */
#ifndef PLATFORM_DISPLAY_WORKER_H
#define PLATFORM_DISPLAY_WORKER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct display_worker display_worker_t;
enum { DW_OK = 0, DW_INVALID = -1001, DW_NO_MEMORY = -1002, DW_TIMEOUT = -1003 };

/* Single foreground owner. No public call may overlap another public call.
 * submit must consume its entire source before returning, including errors.
 * start failure guarantees entry never runs; success returns a valid join handle.
 * join must return OK only after entry has made its final access to worker
 * memory; on timeout it retains its task resources and can be retried.
 * pause yields at least one scheduler tick, never spins. All ops are required.
 */
typedef struct {
    void *(*pixels_alloc)(size_t bytes);
    void (*pixels_free)(void *memory);
    uint64_t (*now_ms)(void);
    void (*pause)(void);
    int (*start)(void (*entry)(void *), void *argument, void **task);
    int (*join)(void *task, uint32_t timeout_ms);
    int (*submit)(void *context, const void *pixels, size_t stride,
                  uint32_t timeout_ms);
} display_worker_ops_t;

typedef struct {
    void *pixels;
    uint64_t generation;
    unsigned slot;
} display_worker_lease_t;

typedef struct {
    uint32_t accepted, completed, failures, acquire_waits, commit_waits;
    int error;
    bool closing;
} display_worker_stats_t;

int display_worker_create(const display_worker_ops_t *ops, void *context,
                          size_t width, size_t height, size_t pixel_bytes,
                          display_worker_t **out);
/* Exactly one writable lease may be held. Writing is permitted only until a
 * successful commit/cancel. Copies of old tokens cannot cancel later leases.
 */
int display_worker_acquire(display_worker_t *, display_worker_lease_t *, uint32_t);
/* Timeout/error leaves lease owned by producer; caller must cancel or retry.
 * submit_timeout_ms is published with this frame, preserving startup/gameplay
 * deadlines. Successful commit transfers ownership and zeroes the lease.
 */
int display_worker_commit(display_worker_t *, display_worker_lease_t *,
                          uint32_t wait_timeout_ms, uint32_t submit_timeout_ms);
int display_worker_cancel(display_worker_t *, display_worker_lease_t *);
int display_worker_flush(display_worker_t *, uint32_t);
int display_worker_stats(display_worker_t *, display_worker_stats_t *);
/* Rejects an outstanding writable lease. Closes admission, drains the sole
 * pending frame, joins, then frees buffers. Timeout retains *worker and all
 * resources, rejects new frames, and permits a subsequent stop retry. A prior
 * render error does not prevent safe destruction once the worker has joined.
 */
int display_worker_stop(display_worker_t **worker, uint32_t timeout_ms);
#endif
