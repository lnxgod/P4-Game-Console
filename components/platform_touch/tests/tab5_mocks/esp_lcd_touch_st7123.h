#pragma once
#include "esp_lcd_touch_gt911.h"
#define ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS (0x55)
#define ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG() { .dev_addr=0x55, .lcd_cmd_bits=16 }
esp_err_t esp_lcd_touch_new_i2c_st7123(esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out);
