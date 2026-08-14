#ifndef MOCK_DRIVER_I2C_MASTER_H
#define MOCK_DRIVER_I2C_MASTER_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"

typedef int i2c_port_num_t;
typedef int i2c_clock_source_t;
typedef struct mock_i2c_bus *i2c_master_bus_handle_t;

#define I2C_NUM_1 1
#define I2C_CLK_SRC_DEFAULT 0

typedef struct {
    i2c_port_num_t i2c_port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    i2c_clock_source_t clk_source;
    uint8_t glitch_ignore_cnt;
    int intr_priority;
    size_t trans_queue_depth;
    struct {
        uint32_t enable_internal_pullup : 1;
        uint32_t allow_pd : 1;
    } flags;
} i2c_master_bus_config_t;

esp_err_t i2c_new_master_bus(
    const i2c_master_bus_config_t *config,
    i2c_master_bus_handle_t *out_handle
);
esp_err_t i2c_master_probe(
    i2c_master_bus_handle_t handle,
    uint16_t address,
    int timeout_ms
);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t handle);

#endif
