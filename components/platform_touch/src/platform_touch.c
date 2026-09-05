#include "platform/touch.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform_touch_frame.h"
#include "sdkconfig.h"

#define PLATFORM_TOUCH_GT911_STATUS_REG UINT16_C(0x814e)
#define PLATFORM_TOUCH_GT911_CONFIG_REG UINT16_C(0x8047)
#define PLATFORM_TOUCH_GT911_PRODUCT_ID_REG UINT16_C(0x8140)
#define PLATFORM_TOUCH_GT911_CONFIG_CHECKSUM_OFFSET 184U
#define PLATFORM_TOUCH_GT911_CONFIG_FRESH_OFFSET 185U
#define PLATFORM_TOUCH_GT911_CONFIG_CHECKSUM_INPUT_BYTES 184U
#define PLATFORM_TOUCH_GT911_NORMAL_FILTER_OFFSET 9U
#define PLATFORM_TOUCH_GT911_NORMAL_FILTER_MASK UINT8_C(0x3f)
#define PLATFORM_TOUCH_GT911_NORMAL_FILTER_SOURCE UINT8_C(8)
#define PLATFORM_TOUCH_GT911_NORMAL_FILTER_TARGET UINT8_C(4)
#define PLATFORM_TOUCH_GT911_REVIEWED_ORIGINAL_CHECKSUM UINT8_C(0x79)
#define PLATFORM_TOUCH_GT911_REVIEWED_FILTER4_CHECKSUM UINT8_C(0x7d)
#define PLATFORM_TOUCH_GT911_CONFIG_APPLY_DELAY_MS 11U

static const uint8_t s_gt911_reviewed_unit3_identity[
    PLATFORM_TOUCH_GT911_IDENTITY_BYTES] = {
    0x39U, 0x31U, 0x31U, 0x00U, 0x60U, 0x10U,
    0xe0U, 0x01U, 0x20U, 0x03U, 0x00U,
};

static const uint8_t s_gt911_reviewed_unit3_original_config[
    PLATFORM_TOUCH_GT911_CONFIG_BYTES] = {
    0x41U, 0xe0U, 0x01U, 0x20U, 0x03U, 0x05U, 0x35U, 0x20U,
    0x22U, 0x08U, 0x28U, 0x05U, 0x5aU, 0x3cU, 0x03U, 0x05U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x18U,
    0x1aU, 0x1eU, 0x14U, 0x87U, 0x27U, 0x09U, 0xcdU, 0xcfU,
    0xb5U, 0x06U, 0x00U, 0x00U, 0x00U, 0x20U, 0x02U, 0x10U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0xb4U, 0xefU, 0x94U, 0xd5U, 0x02U,
    0x08U, 0x00U, 0x00U, 0x04U, 0x87U, 0xb9U, 0x00U, 0x82U,
    0xc4U, 0x00U, 0x7eU, 0xcfU, 0x00U, 0x7bU, 0xdbU, 0x00U,
    0x78U, 0xe8U, 0x00U, 0x78U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x12U, 0x10U, 0x0eU, 0x0cU, 0x0aU, 0x08U, 0x06U, 0x04U,
    0x02U, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
    0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
    0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0x00U, 0x02U,
    0x04U, 0x06U, 0x08U, 0x0aU, 0x0cU, 0x24U, 0x22U, 0x21U,
    0x20U, 0x1fU, 0x1eU, 0x1dU, 0xffU, 0xffU, 0xffU, 0xffU,
    0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
    0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
    0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU,
    0x79U, 0x00U,
};

_Static_assert(CONFIG_ESP_LCD_TOUCH_MAX_POINTS ==
                   PLATFORM_TOUCH_MAX_CONTACTS,
               "esp_lcd_touch point buffer must be exactly five");
_Static_assert(CONFIG_ESP_LCD_TOUCH_MAX_BUTTONS == 0,
               "CrowPanel GT911 has no authorized touch buttons");
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
#ifndef CONFIG_PLATFORM_TOUCH_WAVESHARE_4_3_BUILD_ONLY
#error "Waveshare touch requires its explicit build-only authorization"
#endif
#else
#ifndef CONFIG_PLATFORM_TOUCH_ELECROW_10_1_CROSS_REVISION_AUTHORIZED
#error "platform_touch requires the reviewed Elecrow 10.1-inch GT911 authorization"
#endif
#endif

struct platform_touch {
    i2c_master_bus_handle_t borrowed_bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t driver;
    esp_lcd_touch_io_gt911_config_t driver_config;
    uint32_t sequence;
    bool has_last_report;
    uint8_t last_report_count;
    platform_touch_contact_t last_report_contacts[PLATFORM_TOUCH_MAX_CONTACTS];
    int64_t last_report_timestamp_us;
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
        .x_max = PLATFORM_TOUCH_NATIVE_WIDTH,
        .y_max = PLATFORM_TOUCH_NATIVE_HEIGHT,
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

static uint16_t little_endian_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8U);
}

static uint8_t gt911_config_checksum(const uint8_t *config)
{
    uint8_t sum = 0U;
    for (size_t index = 0U;
         index < PLATFORM_TOUCH_GT911_CONFIG_CHECKSUM_INPUT_BYTES; ++index) {
        sum = (uint8_t)(sum + config[index]);
    }
    return (uint8_t)(0U - sum);
}

static bool gt911_config_checksum_valid(const uint8_t *config)
{
    return config[PLATFORM_TOUCH_GT911_CONFIG_CHECKSUM_OFFSET] ==
           gt911_config_checksum(config);
}

esp_err_t platform_touch_gt911_read_info(
    platform_touch_t *touch,
    platform_touch_gt911_info_t *out_info)
{
    if (out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_info = (platform_touch_gt911_info_t){0};
    if (touch == NULL || touch != s_live_touch || touch->io == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = esp_lcd_panel_io_rx_param(
        touch->io, PLATFORM_TOUCH_GT911_CONFIG_REG, out_info->config,
        PLATFORM_TOUCH_GT911_CONFIG_BYTES);
    if (result != ESP_OK) {
        *out_info = (platform_touch_gt911_info_t){0};
        return result;
    }
    result = esp_lcd_panel_io_rx_param(
        touch->io, PLATFORM_TOUCH_GT911_PRODUCT_ID_REG, out_info->identity,
        PLATFORM_TOUCH_GT911_IDENTITY_BYTES);
    if (result != ESP_OK) {
        *out_info = (platform_touch_gt911_info_t){0};
        return result;
    }

    memcpy(out_info->product_id, out_info->identity,
           sizeof(out_info->product_id));
    out_info->firmware_version = little_endian_u16(&out_info->identity[4]);
    out_info->identity_x_resolution =
        little_endian_u16(&out_info->identity[6]);
    out_info->identity_y_resolution =
        little_endian_u16(&out_info->identity[8]);
    out_info->vendor_id = out_info->identity[10];

    out_info->config_version = out_info->config[0];
    out_info->config_x_resolution = little_endian_u16(&out_info->config[1]);
    out_info->config_y_resolution = little_endian_u16(&out_info->config[3]);
    out_info->max_touch_points = out_info->config[5] & UINT8_C(0x0f);
    out_info->module_switch_1 = out_info->config[6];
    out_info->module_switch_2 = out_info->config[7];
    out_info->shake_count = out_info->config[8];
    out_info->filter = out_info->config[9];
    out_info->first_filter = (out_info->config[9] >> 6U) & UINT8_C(0x03);
    out_info->normal_filter = out_info->config[9] & UINT8_C(0x3f);
    out_info->large_touch = out_info->config[10];
    out_info->noise_reduction = out_info->config[11];
    out_info->screen_touch_level = out_info->config[12];
    out_info->screen_release_level = out_info->config[13];
    out_info->low_power_control = out_info->config[14];
    out_info->refresh_rate = out_info->config[15];
    out_info->refresh_n = out_info->config[15] & UINT8_C(0x0f);
    out_info->report_period_ms = (uint8_t)(5U + out_info->refresh_n);
    out_info->x_threshold = out_info->config[16];
    out_info->y_threshold = out_info->config[17];
    out_info->mini_filter = out_info->config[22];
    out_info->config_checksum =
        out_info->config[PLATFORM_TOUCH_GT911_CONFIG_CHECKSUM_OFFSET];
    out_info->config_checksum_calculated =
        gt911_config_checksum(out_info->config);
    out_info->config_checksum_valid =
        out_info->config_checksum == out_info->config_checksum_calculated;
    out_info->config_fresh =
        out_info->config[PLATFORM_TOUCH_GT911_CONFIG_FRESH_OFFSET] == 1U;
    return ESP_OK;
}

static esp_err_t gt911_write_config_apply(platform_touch_t *touch,
                                          const uint8_t *config)
{
    return esp_lcd_panel_io_tx_param(
        touch->io, PLATFORM_TOUCH_GT911_CONFIG_REG, config,
        PLATFORM_TOUCH_GT911_CONFIG_BYTES);
}

static bool gt911_restore_request_is_reviewed(
    const platform_touch_gt911_restore_reviewed_baseline_request_t *request)
{
    const uint8_t *const original = request->original_config;
    return memcmp(request->expected_identity,
                  s_gt911_reviewed_unit3_identity,
                  sizeof(s_gt911_reviewed_unit3_identity)) == 0 &&
           memcmp(original, s_gt911_reviewed_unit3_original_config,
                  sizeof(s_gt911_reviewed_unit3_original_config)) == 0 &&
           original[PLATFORM_TOUCH_GT911_NORMAL_FILTER_OFFSET] ==
               PLATFORM_TOUCH_GT911_NORMAL_FILTER_SOURCE &&
           original[PLATFORM_TOUCH_GT911_CONFIG_CHECKSUM_OFFSET] ==
               PLATFORM_TOUCH_GT911_REVIEWED_ORIGINAL_CHECKSUM &&
           original[PLATFORM_TOUCH_GT911_CONFIG_FRESH_OFFSET] == 0U &&
           gt911_config_checksum_valid(original);
}

static bool gt911_restore_current_is_exact_filter4(
    const platform_touch_gt911_info_t *info)
{
    uint8_t expected[PLATFORM_TOUCH_GT911_CONFIG_BYTES];
    memcpy(expected, s_gt911_reviewed_unit3_original_config,
           sizeof(expected));
    expected[PLATFORM_TOUCH_GT911_NORMAL_FILTER_OFFSET] =
        (uint8_t)((expected[PLATFORM_TOUCH_GT911_NORMAL_FILTER_OFFSET] &
                  (uint8_t)~PLATFORM_TOUCH_GT911_NORMAL_FILTER_MASK) |
                  PLATFORM_TOUCH_GT911_NORMAL_FILTER_TARGET);
    expected[PLATFORM_TOUCH_GT911_CONFIG_CHECKSUM_OFFSET] =
        PLATFORM_TOUCH_GT911_REVIEWED_FILTER4_CHECKSUM;
    return info->config_checksum_valid &&
           memcmp(info->config, expected, sizeof(expected)) == 0;
}

static bool gt911_restore_readback_is_exact_original(
    const platform_touch_gt911_info_t *info)
{
    return memcmp(info->identity, s_gt911_reviewed_unit3_identity,
                  sizeof(s_gt911_reviewed_unit3_identity)) == 0 &&
           info->config_checksum_valid &&
           memcmp(info->config, s_gt911_reviewed_unit3_original_config,
                  PLATFORM_TOUCH_GT911_CONFIG_FRESH_OFFSET) == 0 &&
           (info->config[PLATFORM_TOUCH_GT911_CONFIG_FRESH_OFFSET] == 0U ||
            info->config[PLATFORM_TOUCH_GT911_CONFIG_FRESH_OFFSET] == 1U);
}

esp_err_t platform_touch_gt911_restore_reviewed_baseline(
    platform_touch_t *touch,
    const platform_touch_gt911_restore_reviewed_baseline_request_t *request,
    platform_touch_gt911_restore_reviewed_baseline_result_t *out_result)
{
    if (out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_result = (platform_touch_gt911_restore_reviewed_baseline_result_t){
        .result = ESP_ERR_INVALID_STATE,
    };
    if (touch == NULL || touch != s_live_touch || touch->io == NULL ||
        request == NULL) {
        out_result->result = ESP_ERR_INVALID_ARG;
        return out_result->result;
    }
    if (!gt911_restore_request_is_reviewed(request)) {
        out_result->result = ESP_ERR_INVALID_RESPONSE;
        return out_result->result;
    }

    out_result->result = platform_touch_gt911_read_info(
        touch, &out_result->before);
    if (out_result->result != ESP_OK) {
        return out_result->result;
    }
    if (memcmp(out_result->before.identity, s_gt911_reviewed_unit3_identity,
               sizeof(s_gt911_reviewed_unit3_identity)) != 0) {
        out_result->result = ESP_ERR_INVALID_RESPONSE;
        return out_result->result;
    }
    if (memcmp(out_result->before.config,
               s_gt911_reviewed_unit3_original_config,
               sizeof(s_gt911_reviewed_unit3_original_config)) == 0) {
        out_result->observed = out_result->before;
        out_result->already_original = true;
        out_result->result = ESP_OK;
        return out_result->result;
    }
    if (!gt911_restore_current_is_exact_filter4(&out_result->before)) {
        out_result->result = ESP_ERR_INVALID_RESPONSE;
        return out_result->result;
    }

    uint8_t restore[PLATFORM_TOUCH_GT911_CONFIG_BYTES];
    memcpy(restore, s_gt911_reviewed_unit3_original_config,
           sizeof(restore));
    restore[PLATFORM_TOUCH_GT911_CONFIG_FRESH_OFFSET] = 1U;
    out_result->may_have_changed = true;
    out_result->result = gt911_write_config_apply(touch, restore);
    if (out_result->result != ESP_OK) {
        return out_result->result;
    }
    vTaskDelay(pdMS_TO_TICKS(PLATFORM_TOUCH_GT911_CONFIG_APPLY_DELAY_MS));
    out_result->result = platform_touch_gt911_read_info(
        touch, &out_result->observed);
    if (out_result->result != ESP_OK) {
        return out_result->result;
    }
    if (!gt911_restore_readback_is_exact_original(&out_result->observed)) {
        out_result->result = ESP_ERR_INVALID_RESPONSE;
        return out_result->result;
    }
    out_result->changed = true;
    out_result->result = ESP_OK;
    return out_result->result;
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
    const uint32_t sequence =
        touch != NULL && touch == s_live_touch ? ++touch->sequence : 0U;
    platform_touch_frame_fail_closed(out_frame, sequence, 0);
    if (touch == NULL || touch != s_live_touch || touch->driver == NULL ||
        out_frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t status = 0U;
    esp_err_t result = esp_lcd_panel_io_rx_param(
        touch->io, PLATFORM_TOUCH_GT911_STATUS_REG, &status, sizeof(status));
    if (result != ESP_OK) {
        return result;
    }
    const int64_t status_observed_us = esp_timer_get_time();

    result = esp_lcd_touch_read_data(touch->driver);
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
    if (!platform_touch_coordinates_native_to_logical(x, y, count)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    /* Keep both sides of the tiny race window: data-ready was observed just
     * before the driver read, while changed contents can first become visible
     * when that read completes. */
    const int64_t sample_received_us = esp_timer_get_time();
    if (!platform_touch_frame_from_raw(
            out_frame, sequence, 0,
            x, y, strength, count)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool changed = !touch->has_last_report ||
        count != touch->last_report_count;
    for (uint8_t index = 0U; !changed && index < count; ++index) {
        if (touch->last_report_contacts[index].x != x[index] ||
            touch->last_report_contacts[index].y != y[index] ||
            touch->last_report_contacts[index].strength != strength[index]) {
            changed = true;
        }
    }
    const bool data_ready = (status & UINT8_C(0x80)) != 0U;
    if (count == 0U) {
        out_frame->timestamp_us = 0;
    } else if (data_ready || changed) {
        out_frame->timestamp_us = data_ready
            ? status_observed_us : sample_received_us;
    } else if (touch->has_last_report) {
        out_frame->timestamp_us = touch->last_report_timestamp_us;
    }
    touch->has_last_report = true;
    touch->last_report_count = count;
    for (uint8_t index = 0U; index < count; ++index) {
        touch->last_report_contacts[index] = (platform_touch_contact_t){
            .x = x[index], .y = y[index], .strength = strength[index],
        };
    }
    for (uint8_t index = count; index < PLATFORM_TOUCH_MAX_CONTACTS; ++index) {
        touch->last_report_contacts[index] = (platform_touch_contact_t){0};
    }
    touch->last_report_timestamp_us = out_frame->timestamp_us;
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
