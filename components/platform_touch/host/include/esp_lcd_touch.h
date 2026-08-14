#ifndef HOST_ESP_LCD_TOUCH_H
#define HOST_ESP_LCD_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct mock_lcd_touch *esp_lcd_touch_handle_t;
typedef void (*esp_lcd_touch_interrupt_callback_t)(esp_lcd_touch_handle_t);

typedef struct {
    uint16_t x_max;
    uint16_t y_max;
    gpio_num_t rst_gpio_num;
    gpio_num_t int_gpio_num;
    struct {
        unsigned int reset : 1;
        unsigned int interrupt : 1;
    } levels;
    struct {
        unsigned int swap_xy : 1;
        unsigned int mirror_x : 1;
        unsigned int mirror_y : 1;
    } flags;
    void (*process_coordinates)(esp_lcd_touch_handle_t, uint16_t *,
                                uint16_t *, uint16_t *, uint8_t *, uint8_t);
    esp_lcd_touch_interrupt_callback_t interrupt_callback;
    void *user_data;
    void *driver_data;
} esp_lcd_touch_config_t;

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t touch);
bool esp_lcd_touch_get_coordinates(
    esp_lcd_touch_handle_t touch,
    uint16_t *x,
    uint16_t *y,
    uint16_t *strength,
    uint8_t *contact_count,
    uint8_t maximum_contacts
);
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t touch);

#endif
