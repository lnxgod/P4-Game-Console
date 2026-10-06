// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef bool (*tab5_usb_read_reg_fn)(void *context, uint8_t reg, uint8_t *value);
typedef bool (*tab5_usb_write_reg_fn)(void *context, uint8_t reg, uint8_t value);

typedef enum {
    TAB5_USB_POWER_OK = 0,
    TAB5_USB_POWER_INVALID_ARGUMENT,
    TAB5_USB_POWER_IO_ERROR,
    TAB5_USB_POWER_VERIFY_ERROR,
} tab5_usb_power_result_t;

/* Caller serializes access to expander 0x44. Never resets the expander or
 * changes pins other than P3; an enable failure attempts to clear P3. */
tab5_usb_power_result_t tab5_usb_power_apply(
    void *context, tab5_usb_read_reg_fn read_reg,
    tab5_usb_write_reg_fn write_reg, bool enabled);
