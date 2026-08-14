#include "p4/runtime_core.h"

#include <stddef.h>

void p4_lifecycle_init(p4_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL) {
        return;
    }
    lifecycle->state = P4_LIFECYCLE_IDLE;
    lifecycle->generation = 0U;
    lifecycle->last_error = P4_STATUS_OK;
}

p4_status_t p4_lifecycle_begin_load(p4_lifecycle_t *lifecycle, uint32_t generation)
{
    if (lifecycle == NULL || generation == 0U) {
        return P4_STATUS_INVALID_ARGUMENT;
    }
    if (lifecycle->state != P4_LIFECYCLE_IDLE) {
        return P4_STATUS_INVALID_STATE;
    }
    lifecycle->state = P4_LIFECYCLE_LOADING;
    lifecycle->generation = generation;
    lifecycle->last_error = P4_STATUS_OK;
    return P4_STATUS_OK;
}

p4_status_t p4_lifecycle_mark_loaded(p4_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL) {
        return P4_STATUS_INVALID_ARGUMENT;
    }
    if (lifecycle->state != P4_LIFECYCLE_LOADING) {
        return P4_STATUS_INVALID_STATE;
    }
    lifecycle->state = P4_LIFECYCLE_RUNNING;
    return P4_STATUS_OK;
}

p4_status_t p4_lifecycle_request_stop(p4_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL) {
        return P4_STATUS_INVALID_ARGUMENT;
    }
    if (lifecycle->state == P4_LIFECYCLE_STOP_REQUESTED) {
        return P4_STATUS_OK;
    }
    if (lifecycle->state != P4_LIFECYCLE_LOADING &&
        lifecycle->state != P4_LIFECYCLE_RUNNING &&
        lifecycle->state != P4_LIFECYCLE_FAULTED) {
        return P4_STATUS_INVALID_STATE;
    }
    lifecycle->state = P4_LIFECYCLE_STOP_REQUESTED;
    return P4_STATUS_OK;
}

p4_status_t p4_lifecycle_mark_quiesced(p4_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL) {
        return P4_STATUS_INVALID_ARGUMENT;
    }
    if (lifecycle->state != P4_LIFECYCLE_STOP_REQUESTED) {
        return P4_STATUS_INVALID_STATE;
    }
    lifecycle->state = P4_LIFECYCLE_QUIESCED;
    return P4_STATUS_OK;
}

p4_status_t p4_lifecycle_begin_render_drain(p4_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL) {
        return P4_STATUS_INVALID_ARGUMENT;
    }
    if (lifecycle->state != P4_LIFECYCLE_QUIESCED) {
        return P4_STATUS_INVALID_STATE;
    }
    lifecycle->state = P4_LIFECYCLE_RENDER_DRAINING;
    return P4_STATUS_OK;
}

p4_status_t p4_lifecycle_begin_unload(p4_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL) {
        return P4_STATUS_INVALID_ARGUMENT;
    }
    if (lifecycle->state != P4_LIFECYCLE_RENDER_DRAINING) {
        return P4_STATUS_INVALID_STATE;
    }
    lifecycle->state = P4_LIFECYCLE_UNLOADING;
    return P4_STATUS_OK;
}

p4_status_t p4_lifecycle_finish_unload(p4_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL) {
        return P4_STATUS_INVALID_ARGUMENT;
    }
    if (lifecycle->state != P4_LIFECYCLE_UNLOADING) {
        return P4_STATUS_INVALID_STATE;
    }
    lifecycle->state = P4_LIFECYCLE_IDLE;
    lifecycle->generation = 0U;
    return P4_STATUS_OK;
}

void p4_lifecycle_fault(p4_lifecycle_t *lifecycle, p4_status_t error)
{
    if (lifecycle == NULL || lifecycle->state == P4_LIFECYCLE_IDLE) {
        return;
    }
    lifecycle->state = P4_LIFECYCLE_FAULTED;
    lifecycle->last_error = error != P4_STATUS_OK ? error : P4_STATUS_BACKEND_FAILED;
}

