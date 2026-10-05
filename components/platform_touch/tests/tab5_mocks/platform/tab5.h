#pragma once
#include "driver/i2c_types.h"
#include "esp_err.h"
typedef enum {TAB5_PANEL_UNKNOWN, TAB5_PANEL_ILI9881C, TAB5_PANEL_ST7123, TAB5_PANEL_ST7121} platform_tab5_panel_t;
i2c_master_bus_handle_t platform_tab5_i2c(void);
esp_err_t platform_tab5_panel_detect(platform_tab5_panel_t *out);
