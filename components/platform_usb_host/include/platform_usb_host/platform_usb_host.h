#ifndef PLATFORM_USB_HOST_PLATFORM_USB_HOST_H
#define PLATFORM_USB_HOST_PLATFORM_USB_HOST_H

#include "platform_usb_host/model.h"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t generation;
    uint32_t class_lease_mask;
    platform_usb_host_state_t state;
    bool root_port_enabled;
} platform_usb_host_info_t;

/**
 * Install the singleton ESP-IDF USB Host stack on ESP32-P4 peripheral 0 (HS).
 *
 * The library and daemon are installed with the root data port disabled. A
 * successful return leaves the service in PLATFORM_USB_HOST_READY so class
 * drivers can register without racing enumeration. Elecrow builds require a
 * reviewed external-fixture record. The Olimex ESP32-P4-PC build instead
 * requires NULL and holds its source-reviewed onboard hub in reset until the
 * class driver is ready.
 */
esp_err_t platform_usb_host_start(
    const platform_usb_fixture_evidence_t *fixture_evidence);

/**
 * Enable enumeration after every required class driver has registered.
 *
 * At least one class lease must already be held. New class leases are rejected
 * after this call succeeds, preventing a cold-plugged device from enumerating
 * before its class client exists.
 */
esp_err_t platform_usb_host_enable_root_port(void);

/**
 * Block new class leases and disable the P4 root port.
 *
 * Existing class owners retain valid leases so they can drain disconnects,
 * uninstall, and release their leases. The external fixture still owns its
 * physical VBUS switch. On Olimex, this call asserts the onboard hub reset;
 * board-supplied, current-limited VBUS remains physically present.
 */
esp_err_t platform_usb_host_quiesce(void);

/**
 * Free devices, stop the daemon, and uninstall after quiesce and lease release.
 * If there are no leases, this call performs the quiesce step itself.
 */
esp_err_t platform_usb_host_stop(TickType_t timeout_ticks);

/** Acquire exclusive ownership for one USB class driver. */
esp_err_t platform_usb_host_class_acquire(platform_usb_class_t class_id,
                                          platform_usb_class_lease_t *lease);

/** Release a class lease exactly once; stale copies are rejected. */
esp_err_t platform_usb_host_class_release(platform_usb_class_lease_t *lease);

/** Copy current singleton lifecycle state. */
esp_err_t platform_usb_host_get_info(platform_usb_host_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
