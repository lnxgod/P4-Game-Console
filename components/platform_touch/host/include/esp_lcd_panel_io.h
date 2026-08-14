#ifndef HOST_ESP_LCD_PANEL_IO_H
#define HOST_ESP_LCD_PANEL_IO_H

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_types.h"
#include "esp_err.h"

typedef struct mock_lcd_panel_io *esp_lcd_panel_io_handle_t;

typedef struct {
    uint32_t dev_addr;
    void *on_color_trans_done;
    void *user_ctx;
    size_t control_phase_bytes;
    unsigned int dc_bit_offset;
    int lcd_cmd_bits;
    int lcd_param_bits;
    struct {
        unsigned int dc_low_on_data : 1;
        unsigned int disable_control_phase : 1;
    } flags;
    uint32_t scl_speed_hz;
} esp_lcd_panel_io_i2c_config_t;

esp_err_t esp_lcd_new_panel_io_i2c(
    i2c_master_bus_handle_t bus,
    const esp_lcd_panel_io_i2c_config_t *config,
    esp_lcd_panel_io_handle_t *out_io
);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io);

#endif
