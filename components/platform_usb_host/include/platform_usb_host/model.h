#ifndef PLATFORM_USB_HOST_MODEL_H
#define PLATFORM_USB_HOST_MODEL_H

#include "platform_usb_host/policy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_USB_CLASS_LEASE_VERSION 1U

typedef enum {
    PLATFORM_USB_HOST_STOPPED = 0,
    PLATFORM_USB_HOST_STARTING,
    /** Host library and daemon are ready; the root data port remains off. */
    PLATFORM_USB_HOST_READY,
    PLATFORM_USB_HOST_ENABLING_ROOT_PORT,
    PLATFORM_USB_HOST_RUNNING,
    PLATFORM_USB_HOST_QUIESCING,
    PLATFORM_USB_HOST_STOPPING,
    PLATFORM_USB_HOST_FAULT,
} platform_usb_host_state_t;

/** Singleton class owners supported by the shared host service. */
typedef enum {
    PLATFORM_USB_CLASS_HID = 0,
    PLATFORM_USB_CLASS_MASS_STORAGE,
    PLATFORM_USB_CLASS_AUDIO,
    PLATFORM_USB_CLASS_DIAGNOSTIC,
    PLATFORM_USB_CLASS_GAMEPAD_VENDOR,
    PLATFORM_USB_CLASS_COUNT,
} platform_usb_class_t;

typedef struct {
    uint16_t version;
    uint8_t class_id;
    uint8_t reserved;
    uint32_t host_generation;
} platform_usb_class_lease_t;

/** Pure lifecycle state machine. The firmware wrapper supplies synchronization. */
typedef struct {
    uint32_t generation;
    uint32_t lease_mask;
    uint8_t state;
    uint8_t initialized;
    uint8_t root_port_enabled;
    uint8_t reserved;
} platform_usb_host_model_t;

void platform_usb_host_model_init(platform_usb_host_model_t *model);
platform_usb_status_t platform_usb_host_model_begin_start(
    platform_usb_host_model_t *model,
    const platform_usb_fixture_evidence_t *evidence);
/**
 * Begin a host start after the wrapper has matched a compiled, source-reviewed
 * integrated board path. This bypasses only the external-fixture record; all
 * lifecycle and lease checks remain identical.
 */
platform_usb_status_t platform_usb_host_model_begin_integrated_start(
    platform_usb_host_model_t *model);
platform_usb_status_t platform_usb_host_model_complete_start(
    platform_usb_host_model_t *model,
    bool success);
/** Begin enabling only after at least one class driver owns a lease. */
platform_usb_status_t platform_usb_host_model_begin_root_port_enable(
    platform_usb_host_model_t *model);
platform_usb_status_t platform_usb_host_model_complete_root_port_enable(
    platform_usb_host_model_t *model,
    bool success);
platform_usb_status_t platform_usb_host_model_acquire(
    platform_usb_host_model_t *model,
    platform_usb_class_t class_id,
    platform_usb_class_lease_t *lease);
platform_usb_status_t platform_usb_host_model_release(
    platform_usb_host_model_t *model,
    platform_usb_class_lease_t *lease);
/** Block new leases before the wrapper cuts root-port power. */
platform_usb_status_t platform_usb_host_model_begin_quiesce(
    platform_usb_host_model_t *model);
platform_usb_status_t platform_usb_host_model_complete_quiesce(
    platform_usb_host_model_t *model,
    bool success);
platform_usb_status_t platform_usb_host_model_begin_stop(
    platform_usb_host_model_t *model);
platform_usb_status_t platform_usb_host_model_complete_stop(
    platform_usb_host_model_t *model,
    bool success);
/** Enter a terminal fault state while retaining uncertain live resources. */
platform_usb_status_t platform_usb_host_model_mark_fault(
    platform_usb_host_model_t *model);

#ifdef __cplusplus
}
#endif

#endif
