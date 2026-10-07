// SPDX-License-Identifier: MIT
#include "platform_touch_tab5_io.h"
#include "esp_lcd_panel_io_interface.h"
#include <stdlib.h>

typedef struct {
    esp_lcd_panel_io_t base;
    i2c_master_dev_handle_t device;
    bool retiring;
} tab5_touch_io_t;

static esp_err_t touch_io_rx(esp_lcd_panel_io_t *io, int command,
    void *param, size_t param_size)
{
    if (!io || command < 0 || command > UINT16_MAX || !param ||
        param_size == 0 || param_size > PLATFORM_TOUCH_TAB5_IO_MAX_PARAM_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    tab5_touch_io_t *touch_io = (tab5_touch_io_t *)io;
    if (touch_io->retiring) return ESP_ERR_INVALID_STATE;
    const uint8_t reg[2] = {(uint8_t)((unsigned)command >> 8), (uint8_t)command};
    /* The existing board bus is synchronous: these borrowed buffers remain
     * live until this call returns. Never enable I2C async callbacks on it. */
    return i2c_master_transmit_receive(touch_io->device, reg, sizeof(reg),
        param, param_size, PLATFORM_TOUCH_TAB5_IO_TIMEOUT_MS);
}

static esp_err_t touch_io_tx(esp_lcd_panel_io_t *io, int command,
    const void *param, size_t param_size)
{
    if (!io || command < 0 || command > UINT16_MAX ||
        (param_size != 0 && !param) ||
        param_size > PLATFORM_TOUCH_TAB5_IO_MAX_PARAM_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    tab5_touch_io_t *touch_io = (tab5_touch_io_t *)io;
    if (touch_io->retiring) return ESP_ERR_INVALID_STATE;
    const uint8_t reg[2] = {(uint8_t)((unsigned)command >> 8), (uint8_t)command};
    i2c_master_transmit_multi_buffer_info_t buffers[2] = {
        {.write_buffer = reg, .buffer_size = sizeof(reg)},
        {.write_buffer = param, .buffer_size = param_size},
    };
    /* One transaction preserves register + payload without another allocation
     * or an extra STOP. There is no display control-phase byte. */
    return i2c_master_multi_buffer_transmit(touch_io->device, buffers,
        param_size != 0 ? 2U : 1U, PLATFORM_TOUCH_TAB5_IO_TIMEOUT_MS);
}

static esp_err_t touch_io_delete(esp_lcd_panel_io_t *io)
{
    if (!io) return ESP_ERR_INVALID_ARG;
    tab5_touch_io_t *touch_io = (tab5_touch_io_t *)io;
    touch_io->retiring = true;
    /* Do not assert, free, or forget the device after a failed removal.
     * The caller keeps this teardown-only owner and may retry deletion. */
    const esp_err_t result = i2c_master_bus_rm_device(touch_io->device);
    if (result != ESP_OK) return result;
    free(touch_io);
    return ESP_OK;
}

static esp_err_t touch_io_color(esp_lcd_panel_io_t *io, int command,
    const void *color, size_t color_size)
{
    (void)io; (void)command; (void)color; (void)color_size;
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t touch_io_callbacks(esp_lcd_panel_io_t *io,
    const esp_lcd_panel_io_callbacks_t *callbacks, void *user_context)
{
    (void)io; (void)callbacks; (void)user_context;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t platform_touch_tab5_io_create(i2c_master_bus_handle_t bus,
    uint8_t address_7bit, esp_lcd_panel_io_handle_t *out)
{
    if (!bus || !out || *out || (address_7bit != 0x14 && address_7bit != 0x55)) {
        return ESP_ERR_INVALID_ARG;
    }
    tab5_touch_io_t *touch_io = calloc(1, sizeof(*touch_io));
    if (!touch_io) return ESP_ERR_NO_MEM;
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address_7bit,
        .scl_speed_hz = PLATFORM_TOUCH_TAB5_IO_CLOCK_HZ,
    };
    const esp_err_t result = i2c_master_bus_add_device(bus, &device_config,
        &touch_io->device);
    if (result != ESP_OK) {
        free(touch_io);
        return result;
    }
    touch_io->base = (esp_lcd_panel_io_t){
        .rx_param = touch_io_rx,
        .tx_param = touch_io_tx,
        .tx_color = touch_io_color,
        .del = touch_io_delete,
        .register_event_callbacks = touch_io_callbacks,
    };
    *out = &touch_io->base;
    return ESP_OK;
}
