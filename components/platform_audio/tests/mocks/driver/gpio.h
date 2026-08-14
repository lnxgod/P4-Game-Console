#ifndef MOCK_DRIVER_GPIO_H
#define MOCK_DRIVER_GPIO_H

#include <stdint.h>

#include "esp_err.h"

typedef int gpio_num_t;

enum {
    GPIO_MODE_OUTPUT = 1,
    GPIO_PULLUP_DISABLE = 0,
    GPIO_PULLDOWN_DISABLE = 0,
    GPIO_INTR_DISABLE = 0,
};

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;

esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
esp_err_t gpio_config(const gpio_config_t *config);

#endif
