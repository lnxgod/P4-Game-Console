// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "esp_io_expander.h"

typedef enum {
    TAB5_PANEL_UNKNOWN = 0,
    TAB5_PANEL_ILI9881C,
    TAB5_PANEL_ST7123,
    TAB5_PANEL_ST7121,
} platform_tab5_panel_t;

/* Board service owns one I2C bus and its expander for the entire boot.
 * Display, touch and codec borrow it, including across the Doom handoff.
 * Calls that modify an expander are serialized within this service. */
esp_err_t platform_tab5_init(void);
i2c_master_bus_handle_t platform_tab5_i2c(void);
esp_err_t platform_tab5_display_reset(void);
esp_err_t platform_tab5_panel_detect(platform_tab5_panel_t *out);
const char *platform_tab5_panel_name(platform_tab5_panel_t panel);
esp_err_t platform_tab5_speaker_enable(bool enabled);
