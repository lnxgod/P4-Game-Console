#ifndef HOST_ESP_LCD_TOUCH_GT911_H
#define HOST_ESP_LCD_TOUCH_GT911_H

#include <stdint.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

typedef struct {
    uint8_t dev_addr;
} esp_lcd_touch_io_gt911_config_t;

#define ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() \
    { \
        .dev_addr = 0x5dU, \
        .control_phase_bytes = 1U, \
        .dc_bit_offset = 0U, \
        .lcd_cmd_bits = 16, \
        .flags = { .disable_control_phase = 1U }, \
    }

esp_err_t esp_lcd_touch_new_i2c_gt911(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *config,
    esp_lcd_touch_handle_t *out_touch
);

#endif
