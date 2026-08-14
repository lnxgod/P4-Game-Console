#include "platform_i2c_shared/bus.h"

#include <stdbool.h>
#include <stdlib.h>

#include "driver/gpio.h"

struct platform_i2c_shared {
    i2c_master_bus_handle_t handle;
};

static platform_i2c_shared_t *s_owner;

esp_err_t platform_i2c_shared_create(platform_i2c_shared_t **out_bus)
{
    if (out_bus == NULL || *out_bus != NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_owner != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    platform_i2c_shared_t *const bus = calloc(1U, sizeof(*bus));
    if (bus == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const i2c_master_bus_config_t config = {
        .i2c_port = PLATFORM_I2C_SHARED_PORT,
        .sda_io_num = (gpio_num_t)PLATFORM_I2C_SHARED_SDA_GPIO,
        .scl_io_num = (gpio_num_t)PLATFORM_I2C_SHARED_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7U,
        .intr_priority = 0,
        .trans_queue_depth = 0U,
        .flags = {
            .enable_internal_pullup = 1U,
            .allow_pd = 0U,
        },
    };
    const esp_err_t result = i2c_new_master_bus(&config, &bus->handle);
    if (result != ESP_OK) {
        free(bus);
        return result;
    }
    s_owner = bus;
    *out_bus = bus;
    return ESP_OK;
}

i2c_master_bus_handle_t platform_i2c_shared_handle(
    const platform_i2c_shared_t *bus
)
{
    return bus != NULL && bus == s_owner ? bus->handle : NULL;
}

esp_err_t platform_i2c_shared_probe(
    const platform_i2c_shared_t *bus,
    uint8_t address_7bit,
    int timeout_ms
)
{
    if (bus == NULL || bus != s_owner || bus->handle == NULL ||
        address_7bit < UINT8_C(0x08) ||
        address_7bit > UINT8_C(0x77) || timeout_ms <= 0 ||
        timeout_ms > 1000) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_probe(bus->handle, address_7bit, timeout_ms);
}

esp_err_t platform_i2c_shared_destroy(platform_i2c_shared_t **bus)
{
    if (bus == NULL || *bus == NULL || *bus != s_owner ||
        (*bus)->handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = i2c_del_master_bus((*bus)->handle);
    if (result != ESP_OK) {
        return result;
    }
    (*bus)->handle = NULL;
    free(*bus);
    *bus = NULL;
    s_owner = NULL;
    return ESP_OK;
}
