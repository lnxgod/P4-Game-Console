#include "platform/touch.h"

#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_timer.h"
#include "platform_touch_frame.h"
#include "sdkconfig.h"

_Static_assert(CONFIG_ESP_LCD_TOUCH_MAX_POINTS ==
                   PLATFORM_TOUCH_MAX_CONTACTS,
               "esp_lcd_touch point buffer must be exactly five");
_Static_assert(CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS == 0,
               "CrowPanel GT911 has no authorized touch buttons");
#ifndef CONFIG_PLATFORM_TOUCH_ELECROW_10_1_CROSS_REVISION_AUTHORIZED
#error "platform_touch requires the reviewed Elecrow 10.1-inch GT911 authorization"
#endif

struct platform_touch {
    i2c_master_bus_handle_t borrowed_bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t driver;
    esp_lcd_touch_io_gt911_config_t driver_config;
    uint32_t sequence;
};

static platform_touch_t *s_live_touch;

void platform_touch_config_init(platform_touch_config_t *config,
                                i2c_master_bus_handle_t borrowed_bus)
{
    if (config == NULL) {
        return;
    }
    *config = (platform_touch_config_t){
        .bus = borrowed_bus,
        .address_7bit = PLATFORM_TOUCH_GT911_PRIMARY_ADDRESS,
    };
}

static esp_err_t release_touch(platform_touch_t *touch)
{
    if (touch == NULL) {
        return ESP_OK;
    }
    if (touch->driver != NULL) {
        const esp_err_t result = esp_lcd_touch_del(touch->driver);
        if (result != ESP_OK) {
            return result;
        }
        touch->driver = NULL;
    }
    if (touch->io != NULL) {
        const esp_err_t result = esp_lcd_panel_io_del(touch->io);
        if (result != ESP_OK) {
            return result;
        }
        touch->io = NULL;
    }
    return ESP_OK;
}

static esp_err_t create_driver_at_address(platform_touch_t *touch,
                                          uint8_t address_7bit,
                                          bool *out_driver_failed_cleanly)
{
    *out_driver_failed_cleanly = false;
    esp_lcd_panel_io_i2c_config_t io_config =
        ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_config.dev_addr = address_7bit;
    io_config.scl_speed_hz = PLATFORM_TOUCH_I2C_CLOCK_HZ;
    esp_err_t result = esp_lcd_new_panel_io_i2c(
        touch->borrowed_bus, &io_config, &touch->io
    );
    if (result != ESP_OK) {
        return result;
    }

    touch->driver_config.dev_addr = address_7bit;
    const esp_lcd_touch_config_t touch_config = {
        .x_max = PLATFORM_TOUCH_WIDTH,
        .y_max = PLATFORM_TOUCH_HEIGHT,
        .rst_gpio_num = (gpio_num_t)PLATFORM_TOUCH_RESET_GPIO,
        .int_gpio_num = (gpio_num_t)PLATFORM_TOUCH_INTERRUPT_GPIO,
        .levels = {
            .reset = 0U,
            .interrupt = 0U,
        },
        .flags = {
            .swap_xy = 0U,
            .mirror_x = 0U,
            .mirror_y = 0U,
        },
        .process_coordinates = NULL,
        .interrupt_callback = NULL,
        .user_data = NULL,
        .driver_data = &touch->driver_config,
    };
    result = esp_lcd_touch_new_i2c_gt911(
        touch->io, &touch_config, &touch->driver
    );
    if (result == ESP_OK) {
        return ESP_OK;
    }

    const esp_err_t cleanup_result = esp_lcd_panel_io_del(touch->io);
    if (cleanup_result != ESP_OK) {
        return cleanup_result;
    }
    touch->io = NULL;
    *out_driver_failed_cleanly = true;
    return result;
}

esp_err_t platform_touch_create(const platform_touch_config_t *config,
                                platform_touch_t **out_touch)
{
    if (config == NULL || out_touch == NULL || *out_touch != NULL ||
        config->bus == NULL ||
        config->address_7bit != PLATFORM_TOUCH_GT911_PRIMARY_ADDRESS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_live_touch != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    platform_touch_t *const touch = calloc(1U, sizeof(*touch));
    if (touch == NULL) {
        return ESP_ERR_NO_MEM;
    }
    touch->borrowed_bus = config->bus;

    bool driver_failed_cleanly = false;
    esp_err_t result = create_driver_at_address(
        touch, config->address_7bit, &driver_failed_cleanly
    );
    if (result != ESP_OK && driver_failed_cleanly) {
        result = create_driver_at_address(
            touch, PLATFORM_TOUCH_GT911_BACKUP_ADDRESS,
            &driver_failed_cleanly
        );
    }
    if (result != ESP_OK) {
        if (touch->io == NULL && touch->driver == NULL) {
            free(touch);
        } else {
            s_live_touch = touch;
            *out_touch = touch;
        }
        return result;
    }

    s_live_touch = touch;
    *out_touch = touch;
    return ESP_OK;
}

esp_err_t platform_touch_poll(platform_touch_t *touch,
                              platform_touch_frame_t *out_frame)
{
    const int64_t timestamp_us = esp_timer_get_time();
    const uint32_t sequence =
        touch != NULL && touch == s_live_touch ? ++touch->sequence : 0U;
    platform_touch_frame_fail_closed(out_frame, sequence, timestamp_us);
    if (touch == NULL || touch != s_live_touch || touch->driver == NULL ||
        out_frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = esp_lcd_touch_read_data(touch->driver);
    if (result != ESP_OK) {
        return result;
    }
    uint16_t x[PLATFORM_TOUCH_MAX_CONTACTS] = {0U};
    uint16_t y[PLATFORM_TOUCH_MAX_CONTACTS] = {0U};
    uint16_t strength[PLATFORM_TOUCH_MAX_CONTACTS] = {0U};
    uint8_t count = 0U;
    (void)esp_lcd_touch_get_coordinates(
        touch->driver, x, y, strength, &count,
        PLATFORM_TOUCH_MAX_CONTACTS
    );
    if (!platform_touch_frame_from_raw(
            out_frame, sequence, timestamp_us, x, y, strength, count)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t platform_touch_destroy(platform_touch_t **touch)
{
    if (touch == NULL || *touch == NULL || *touch != s_live_touch) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = release_touch(*touch);
    if (result != ESP_OK) {
        return result;
    }
    (*touch)->borrowed_bus = NULL;
    free(*touch);
    *touch = NULL;
    s_live_touch = NULL;
    return ESP_OK;
}
