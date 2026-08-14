#ifndef PLATFORM_GAMEPAD_USB_PLATFORM_GAMEPAD_USB_H
#define PLATFORM_GAMEPAD_USB_PLATFORM_GAMEPAD_USB_H

#include "platform_gamepad_usb/model.h"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t interfaces_seen;
    uint32_t interfaces_rejected;
    uint32_t connections;
    uint32_t disconnections;
    uint32_t reports_committed;
    uint32_t reports_dropped;
    uint32_t malformed_reports;
    uint32_t callback_faults;
} platform_gamepad_usb_stats_t;

/** Install the HID class transport on the already-running platform USB host. */
esp_err_t platform_gamepad_usb_start(void);

/**
 * Stop HID input, neutralize, finish usb_host_hid's close handshake, uninstall,
 * and release the lease. The shared host must be QUIESCING first so no device
 * can remain attached or enumerate during class teardown.
 */
esp_err_t platform_gamepad_usb_stop(TickType_t timeout_ticks);

/** Copy a mutex-protected complete canonical snapshot. */
esp_err_t platform_gamepad_usb_get_snapshot(
    platform_gamepad_snapshot_t *snapshot);

/** Copy bounded transport diagnostics without exposing USB handles. */
esp_err_t platform_gamepad_usb_get_stats(platform_gamepad_usb_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
