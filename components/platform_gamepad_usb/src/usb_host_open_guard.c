#include "usb_host_open_guard.h"

#include <string.h>

int32_t platform_gamepad_usb_open_guard_call(
    void *output_storage,
    size_t output_bytes,
    platform_gamepad_usb_open_delegate_t delegate,
    void *delegate_context,
    int32_t invalid_argument_status)
{
    if (output_storage == NULL || output_bytes == 0U || delegate == NULL) {
        return invalid_argument_status;
    }
    memset(output_storage, 0, output_bytes);
    return delegate(delegate_context);
}
