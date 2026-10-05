#ifndef HOST_DRIVER_GPIO_H
#define HOST_DRIVER_GPIO_H

typedef int gpio_num_t;

#include <stdint.h>
#include "esp_err.h"
#define GPIO_NUM_NC (-1)
#define GPIO_NUM_23 23
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_PULLUP_ENABLE 1
#define GPIO_INTR_DISABLE 0
typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t pin, unsigned level);

#endif
