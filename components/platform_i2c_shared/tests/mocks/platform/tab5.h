#pragma once
#include "driver/i2c_master.h"
esp_err_t platform_tab5_init(void);
i2c_master_bus_handle_t platform_tab5_i2c(void);
