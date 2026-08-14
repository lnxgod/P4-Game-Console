#include "platform_usb_host/model.h"

#include <string.h>

static bool model_valid(const platform_usb_host_model_t *model)
{
    if (model == NULL || model->initialized != 1U ||
        model->state > PLATFORM_USB_HOST_FAULT ||
        model->root_port_enabled > 1U ||
        (model->lease_mask & ~((1U << PLATFORM_USB_CLASS_COUNT) - 1U)) != 0U) {
        return false;
    }

    switch ((platform_usb_host_state_t)model->state) {
    case PLATFORM_USB_HOST_STOPPED:
    case PLATFORM_USB_HOST_STARTING:
    case PLATFORM_USB_HOST_READY:
    case PLATFORM_USB_HOST_ENABLING_ROOT_PORT:
    case PLATFORM_USB_HOST_STOPPING:
        return model->root_port_enabled == 0U;
    case PLATFORM_USB_HOST_RUNNING:
        return model->root_port_enabled == 1U;
    case PLATFORM_USB_HOST_QUIESCING:
    case PLATFORM_USB_HOST_FAULT:
        return true;
    default:
        return false;
    }
}

void platform_usb_host_model_init(platform_usb_host_model_t *model)
{
    if (model == NULL) {
        return;
    }
    memset(model, 0, sizeof(*model));
    model->initialized = 1U;
    model->state = PLATFORM_USB_HOST_STOPPED;
}

platform_usb_status_t platform_usb_host_model_begin_start(
    platform_usb_host_model_t *model,
    const platform_usb_fixture_evidence_t *evidence)
{
    if (!model_valid(model)) {
        return PLATFORM_USB_STATUS_INVALID_ARGUMENT;
    }
    const platform_usb_status_t evidence_status =
        platform_usb_fixture_evidence_validate(evidence);
    if (evidence_status != PLATFORM_USB_STATUS_OK) {
        return evidence_status;
    }
    if (model->state != PLATFORM_USB_HOST_STOPPED || model->lease_mask != 0U) {
        return PLATFORM_USB_STATUS_INVALID_STATE;
    }

    model->generation++;
    if (model->generation == 0U) {
        model->generation = 1U;
    }
    model->state = PLATFORM_USB_HOST_STARTING;
    return PLATFORM_USB_STATUS_OK;
}

platform_usb_status_t platform_usb_host_model_complete_start(
    platform_usb_host_model_t *model,
    bool success)
{
    if (!model_valid(model) || model->state != PLATFORM_USB_HOST_STARTING) {
        return PLATFORM_USB_STATUS_INVALID_STATE;
    }
    model->state = success ? PLATFORM_USB_HOST_READY
                           : PLATFORM_USB_HOST_STOPPED;
    return PLATFORM_USB_STATUS_OK;
}

platform_usb_status_t platform_usb_host_model_begin_root_port_enable(
    platform_usb_host_model_t *model)
{
    if (!model_valid(model)) {
        return PLATFORM_USB_STATUS_INVALID_ARGUMENT;
    }
    if (model->state != PLATFORM_USB_HOST_READY || model->lease_mask == 0U) {
        return PLATFORM_USB_STATUS_INVALID_STATE;
    }
    model->state = PLATFORM_USB_HOST_ENABLING_ROOT_PORT;
    return PLATFORM_USB_STATUS_OK;
}

platform_usb_status_t platform_usb_host_model_complete_root_port_enable(
    platform_usb_host_model_t *model,
    bool success)
{
    if (!model_valid(model) ||
        model->state != PLATFORM_USB_HOST_ENABLING_ROOT_PORT) {
        return PLATFORM_USB_STATUS_INVALID_STATE;
    }
    model->root_port_enabled = success ? 1U : 0U;
    model->state = success ? PLATFORM_USB_HOST_RUNNING
                           : PLATFORM_USB_HOST_READY;
    return PLATFORM_USB_STATUS_OK;
}

platform_usb_status_t platform_usb_host_model_acquire(
    platform_usb_host_model_t *model,
    platform_usb_class_t class_id,
    platform_usb_class_lease_t *lease)
{
    if (!model_valid(model) || lease == NULL ||
        class_id < 0 || class_id >= PLATFORM_USB_CLASS_COUNT) {
        return PLATFORM_USB_STATUS_INVALID_ARGUMENT;
    }
    if (model->state != PLATFORM_USB_HOST_READY) {
        return PLATFORM_USB_STATUS_INVALID_STATE;
    }

    const uint32_t class_bit = 1U << (unsigned)class_id;
    if ((model->lease_mask & class_bit) != 0U) {
        return PLATFORM_USB_STATUS_CLASS_BUSY;
    }

    model->lease_mask |= class_bit;
    *lease = (platform_usb_class_lease_t){
        .version = PLATFORM_USB_CLASS_LEASE_VERSION,
        .class_id = (uint8_t)class_id,
        .host_generation = model->generation,
    };
    return PLATFORM_USB_STATUS_OK;
}

platform_usb_status_t platform_usb_host_model_release(
    platform_usb_host_model_t *model,
    platform_usb_class_lease_t *lease)
{
    if (!model_valid(model) || lease == NULL) {
        return PLATFORM_USB_STATUS_INVALID_ARGUMENT;
    }
    if ((model->state != PLATFORM_USB_HOST_READY &&
         model->state != PLATFORM_USB_HOST_QUIESCING) ||
        lease->version != PLATFORM_USB_CLASS_LEASE_VERSION ||
        lease->class_id >= PLATFORM_USB_CLASS_COUNT ||
        lease->host_generation != model->generation) {
        return PLATFORM_USB_STATUS_STALE_LEASE;
    }

    const uint32_t class_bit = 1U << lease->class_id;
    if ((model->lease_mask & class_bit) == 0U) {
        return PLATFORM_USB_STATUS_STALE_LEASE;
    }

    model->lease_mask &= ~class_bit;
    memset(lease, 0, sizeof(*lease));
    return PLATFORM_USB_STATUS_OK;
}

platform_usb_status_t platform_usb_host_model_begin_quiesce(
    platform_usb_host_model_t *model)
{
    if (!model_valid(model)) {
        return PLATFORM_USB_STATUS_INVALID_ARGUMENT;
    }
    if (model->state != PLATFORM_USB_HOST_READY &&
        model->state != PLATFORM_USB_HOST_RUNNING) {
        return PLATFORM_USB_STATUS_INVALID_STATE;
    }
    model->state = PLATFORM_USB_HOST_QUIESCING;
    return PLATFORM_USB_STATUS_OK;
}

platform_usb_status_t platform_usb_host_model_complete_quiesce(
    platform_usb_host_model_t *model,
    bool success)
{
    if (!model_valid(model) || model->state != PLATFORM_USB_HOST_QUIESCING) {
        return PLATFORM_USB_STATUS_INVALID_STATE;
    }
    if (success) {
        model->root_port_enabled = 0U;
    } else {
        model->state = PLATFORM_USB_HOST_FAULT;
    }
    return PLATFORM_USB_STATUS_OK;
}

platform_usb_status_t platform_usb_host_model_begin_stop(
    platform_usb_host_model_t *model)
{
    if (!model_valid(model)) {
        return PLATFORM_USB_STATUS_INVALID_ARGUMENT;
    }
    if (model->state != PLATFORM_USB_HOST_QUIESCING ||
        model->root_port_enabled != 0U) {
        return PLATFORM_USB_STATUS_INVALID_STATE;
    }
    if (model->lease_mask != 0U) {
        return PLATFORM_USB_STATUS_CLASS_BUSY;
    }
    model->state = PLATFORM_USB_HOST_STOPPING;
    return PLATFORM_USB_STATUS_OK;
}

platform_usb_status_t platform_usb_host_model_complete_stop(
    platform_usb_host_model_t *model,
    bool success)
{
    if (!model_valid(model) || model->state != PLATFORM_USB_HOST_STOPPING) {
        return PLATFORM_USB_STATUS_INVALID_STATE;
    }
    model->state = success ? PLATFORM_USB_HOST_STOPPED : PLATFORM_USB_HOST_FAULT;
    return PLATFORM_USB_STATUS_OK;
}

platform_usb_status_t platform_usb_host_model_mark_fault(
    platform_usb_host_model_t *model)
{
    if (!model_valid(model) || model->state == PLATFORM_USB_HOST_STOPPED) {
        return PLATFORM_USB_STATUS_INVALID_STATE;
    }
    model->state = PLATFORM_USB_HOST_FAULT;
    return PLATFORM_USB_STATUS_OK;
}
