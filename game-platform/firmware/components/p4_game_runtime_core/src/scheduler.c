#include "p4/runtime_core.h"

#include <limits.h>
#include <stddef.h>

p4_script_status_t p4_tick_scheduler_init(
    p4_tick_scheduler_t *scheduler,
    uint32_t clock_hz,
    uint32_t tick_hz)
{
    if (scheduler == NULL || clock_hz == 0U || tick_hz == 0U || clock_hz < tick_hz) {
        return P4_SCRIPT_STATUS_INVALID_ARGUMENT;
    }

    scheduler->phase = 0U;
    scheduler->clock_hz = clock_hz;
    scheduler->tick_hz = tick_hz;
    return P4_SCRIPT_STATUS_OK;
}

p4_script_status_t p4_tick_scheduler_next(
    p4_tick_scheduler_t *scheduler,
    uint32_t *interval_out)
{
    uint64_t interval;

    if (scheduler == NULL || interval_out == NULL || scheduler->clock_hz == 0U ||
        scheduler->tick_hz == 0U) {
        return P4_SCRIPT_STATUS_INVALID_ARGUMENT;
    }

    scheduler->phase += (uint64_t)scheduler->clock_hz;
    interval = scheduler->phase / (uint64_t)scheduler->tick_hz;
    scheduler->phase %= (uint64_t)scheduler->tick_hz;

    if (interval == 0U || interval > (uint64_t)UINT32_MAX) {
        return P4_SCRIPT_STATUS_LIMIT_REACHED;
    }

    *interval_out = (uint32_t)interval;
    return P4_SCRIPT_STATUS_OK;
}

