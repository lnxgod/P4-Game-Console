// SPDX-License-Identifier: MIT
#pragma once
#include "driver/i2c_master.h"
#include <stdint.h>
esp_err_t platform_tab5_rtc_write(i2c_master_dev_handle_t device, uint32_t seconds);
