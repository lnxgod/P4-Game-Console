#ifndef PLATFORM_GAMEPAD_USB_USB_HOST_OPEN_GUARD_H
#define PLATFORM_GAMEPAD_USB_USB_HOST_OPEN_GUARD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*platform_gamepad_usb_open_delegate_t)(void *context);

/**
 * Clear an opaque handle result before invoking a device-open delegate.
 *
 * The pinned usb_host_hid 1.2.0 cleanup path closes its local handle even when
 * usb_host_device_open() fails without writing that result. Keeping the result
 * deterministically zero turns that erroneous close into ESP_ERR_INVALID_ARG
 * instead of allowing an indeterminate pointer to reach the USB Host library.
 */
int32_t platform_gamepad_usb_open_guard_call(
    void *output_storage,
    size_t output_bytes,
    platform_gamepad_usb_open_delegate_t delegate,
    void *delegate_context,
    int32_t invalid_argument_status);

#ifdef __cplusplus
}
#endif

#endif
