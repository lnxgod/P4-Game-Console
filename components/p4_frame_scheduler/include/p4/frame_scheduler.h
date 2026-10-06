// SPDX-License-Identifier: MIT
#ifndef P4_FRAME_SCHEDULER_H
#define P4_FRAME_SCHEDULER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Rational clock division shared by Console OS, native video and PCM pacing.
 * State layout and status values are stable; no game-runtime dependency. */
typedef int32_t p4_scheduler_status_t;
#define P4_SCHEDULER_OK ((p4_scheduler_status_t)0)
#define P4_SCHEDULER_INVALID_ARGUMENT ((p4_scheduler_status_t)-1)
#define P4_SCHEDULER_LIMIT_REACHED ((p4_scheduler_status_t)-3)
typedef struct {
    uint64_t phase;
    uint32_t clock_hz;
    uint32_t tick_hz;
} p4_tick_scheduler_t;
p4_scheduler_status_t p4_tick_scheduler_init(
    p4_tick_scheduler_t *scheduler, uint32_t clock_hz, uint32_t tick_hz);
p4_scheduler_status_t p4_tick_scheduler_next(
    p4_tick_scheduler_t *scheduler, uint32_t *interval_out);
#ifdef __cplusplus
}
#endif
#endif
