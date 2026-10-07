// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_lcd_types.h"

/* Private Tab5 adapter: do not use for other boards or arbitrary LCDs. */
#define PLATFORM_TOUCH_TAB5_IO_CLOCK_HZ 400000U
#define PLATFORM_TOUCH_TAB5_IO_TIMEOUT_MS 8
#define PLATFORM_TOUCH_TAB5_IO_MAX_PARAM_BYTES 70U

/*
 * Borrow the existing synchronous board-owned I2C bus. The caller must keep
 * that bus synchronous and serialize transactions against deletion. The only
 * accepted addresses are the existing Tab5 GT911 (0x14) and ST712x (0x55).
 * Every transfer uses a 16-bit big-endian register and no control byte.
 *
 * The finite timeout is passed to the pinned SDK, which applies it separately
 * to several internal waits. It is NOT an 8 ms total transaction deadline.
 * SDK add/remove-device operations have no timeout argument.
 *
 * *out must initially be NULL and remains NULL on creation failure. Delete
 * through esp_lcd_panel_io_del() only after the polling owner has joined.
 * A failed deletion retains the same teardown-only handle for a retry; do not
 * clear it or delete the borrowed bus until deletion returns ESP_OK.
 */
esp_err_t platform_touch_tab5_io_create(i2c_master_bus_handle_t bus,
    uint8_t address_7bit, esp_lcd_panel_io_handle_t *out);
