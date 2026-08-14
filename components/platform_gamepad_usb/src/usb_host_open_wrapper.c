#include "usb_host_open_guard.h"

#include "esp_err.h"
#include "usb/usb_host.h"

typedef struct {
    usb_host_client_handle_t client;
    uint8_t device_address;
    usb_device_handle_t *device_handle;
} usb_host_open_call_t;

/* Supplied by GNU ld's --wrap=usb_host_device_open link transformation. */
esp_err_t __real_usb_host_device_open(usb_host_client_handle_t client_hdl,
                                      uint8_t dev_addr,
                                      usb_device_handle_t *dev_hdl_ret);

static int32_t call_real_usb_host_device_open(void *context)
{
    usb_host_open_call_t *const call = context;
    return __real_usb_host_device_open(call->client, call->device_address,
                                       call->device_handle);
}

esp_err_t __wrap_usb_host_device_open(usb_host_client_handle_t client_hdl,
                                      uint8_t dev_addr,
                                      usb_device_handle_t *dev_hdl_ret)
{
    usb_host_open_call_t call = {
        .client = client_hdl,
        .device_address = dev_addr,
        .device_handle = dev_hdl_ret,
    };
    return (esp_err_t)platform_gamepad_usb_open_guard_call(
        dev_hdl_ret, dev_hdl_ret == NULL ? 0U : sizeof(*dev_hdl_ret),
        call_real_usb_host_device_open, &call, ESP_ERR_INVALID_ARG);
}
