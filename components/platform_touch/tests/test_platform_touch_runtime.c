#include "platform/touch.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"
#include "freertos/FreeRTOS.h"

#define MOCK_GT911_STATUS_REG UINT16_C(0x814e)
#define MOCK_GT911_CONFIG_REG UINT16_C(0x8047)
#define MOCK_GT911_IDENTITY_REG UINT16_C(0x8140)

struct i2c_master_bus_t {
    unsigned marker;
};

struct mock_lcd_panel_io {
    unsigned marker;
};

struct mock_lcd_touch {
    unsigned marker;
};

typedef struct {
    esp_err_t io_create_result;
    esp_err_t driver_create_results[2];
    esp_err_t read_result;
    esp_err_t rx_result;
    esp_err_t tx_results[3];
    esp_err_t driver_delete_result;
    esp_err_t io_delete_results[3];
    i2c_master_bus_handle_t created_on_bus;
    esp_lcd_panel_io_i2c_config_t io_configs[2];
    esp_lcd_touch_config_t touch_configs[2];
    uint8_t driver_addresses[2];
    unsigned io_deletes_before_driver_create[2];
    uint16_t x[PLATFORM_TOUCH_MAX_CONTACTS];
    uint16_t y[PLATFORM_TOUCH_MAX_CONTACTS];
    uint16_t strength[PLATFORM_TOUCH_MAX_CONTACTS];
    uint8_t reported_count;
    uint8_t requested_maximum;
    unsigned io_create_calls;
    unsigned driver_create_calls;
    unsigned read_calls;
    unsigned coordinates_calls;
    unsigned driver_delete_calls;
    unsigned io_delete_calls;
    int64_t now_us;
    uint8_t status;
    uint8_t config[PLATFORM_TOUCH_GT911_CONFIG_BYTES];
    uint8_t identity[PLATFORM_TOUCH_GT911_IDENTITY_BYTES];
    unsigned rx_calls;
    uint16_t rx_registers[4];
    size_t rx_lengths[4];
    unsigned tx_calls;
    uint16_t tx_registers[3];
    size_t tx_lengths[3];
    uint8_t tx_configs[3][PLATFORM_TOUCH_GT911_CONFIG_BYTES];
    unsigned corrupt_after_tx_call;
    unsigned corrupt_valid_config_after_tx_call;
    unsigned corrupt_identity_after_tx_call;
    unsigned fail_rx_call;
    esp_err_t fail_rx_result;
    bool clear_fresh_after_tx;
    bool invalidate_fresh_after_tx;
    unsigned delay_calls;
    TickType_t delay_ticks;
} mock_state_t;

static mock_state_t s_mock;
static struct i2c_master_bus_t s_bus = {.marker = 0xb055U};
static struct mock_lcd_panel_io s_io = {.marker = 0x10U};
static struct mock_lcd_touch s_driver = {.marker = 0x20U};
static unsigned failures;

#define EXPECT_TRUE(expression_) do { \
    if (!(expression_)) { \
        fprintf(stderr, "%s:%d expected true: %s\n", \
                __FILE__, __LINE__, #expression_); \
        ++failures; \
    } \
} while (0)

#define EXPECT_EQ(expected_, actual_) do { \
    const long long expected_value_ = (long long)(expected_); \
    const long long actual_value_ = (long long)(actual_); \
    if (expected_value_ != actual_value_) { \
        fprintf(stderr, "%s:%d expected %lld got %lld\n", \
                __FILE__, __LINE__, expected_value_, actual_value_); \
        ++failures; \
    } \
} while (0)

static void mock_reset(void)
{
    memset(&s_mock, 0, sizeof(s_mock));
    s_mock.io_create_result = ESP_OK;
    s_mock.driver_create_results[0] = ESP_OK;
    s_mock.driver_create_results[1] = ESP_OK;
    s_mock.read_result = ESP_OK;
    s_mock.rx_result = ESP_OK;
    s_mock.fail_rx_result = ESP_FAIL;
    s_mock.tx_results[0] = ESP_OK;
    s_mock.tx_results[1] = ESP_OK;
    s_mock.tx_results[2] = ESP_OK;
    s_mock.driver_delete_result = ESP_OK;
    s_mock.io_delete_results[0] = ESP_OK;
    s_mock.io_delete_results[1] = ESP_OK;
    s_mock.io_delete_results[2] = ESP_OK;
    s_mock.now_us = 1000000;
}

esp_err_t esp_lcd_panel_io_rx_param(
    esp_lcd_panel_io_handle_t io,
    int lcd_cmd,
    void *param,
    size_t param_size
)
{
    EXPECT_TRUE(io == &s_io);
    EXPECT_TRUE(param != NULL);
    const unsigned index = s_mock.rx_calls++;
    if (index < 4U) {
        s_mock.rx_registers[index] = (uint16_t)lcd_cmd;
        s_mock.rx_lengths[index] = param_size;
    }
    if (s_mock.rx_result != ESP_OK) {
        return s_mock.rx_result;
    }
    if (s_mock.fail_rx_call == index + 1U) {
        return s_mock.fail_rx_result;
    }
    if ((uint16_t)lcd_cmd == MOCK_GT911_STATUS_REG && param_size == 1U) {
        *(uint8_t *)param = s_mock.status;
    } else if ((uint16_t)lcd_cmd == MOCK_GT911_CONFIG_REG &&
               param_size == sizeof(s_mock.config)) {
        memcpy(param, s_mock.config, sizeof(s_mock.config));
    } else if ((uint16_t)lcd_cmd == MOCK_GT911_IDENTITY_REG &&
               param_size == sizeof(s_mock.identity)) {
        memcpy(param, s_mock.identity, sizeof(s_mock.identity));
    } else {
        memset(param, 0, param_size);
    }
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_tx_param(
    esp_lcd_panel_io_handle_t io,
    int lcd_cmd,
    const void *param,
    size_t param_size)
{
    EXPECT_TRUE(io == &s_io);
    EXPECT_TRUE(param != NULL);
    const unsigned index = s_mock.tx_calls++;
    if (index < 3U) {
        s_mock.tx_registers[index] = (uint16_t)lcd_cmd;
        s_mock.tx_lengths[index] = param_size;
        if (param_size == PLATFORM_TOUCH_GT911_CONFIG_BYTES) {
            memcpy(s_mock.tx_configs[index], param,
                   PLATFORM_TOUCH_GT911_CONFIG_BYTES);
        }
    }
    const esp_err_t result = index < 3U ? s_mock.tx_results[index] : ESP_FAIL;
    if (result != ESP_OK) {
        return result;
    }
    if ((uint16_t)lcd_cmd == MOCK_GT911_CONFIG_REG &&
        param_size == PLATFORM_TOUCH_GT911_CONFIG_BYTES) {
        memcpy(s_mock.config, param, PLATFORM_TOUCH_GT911_CONFIG_BYTES);
        if (s_mock.corrupt_after_tx_call == index + 1U) {
            s_mock.config[30] ^= UINT8_C(0x01);
        }
        if (s_mock.corrupt_valid_config_after_tx_call == index + 1U) {
            const uint8_t previous = s_mock.config[30];
            s_mock.config[30] ^= UINT8_C(0x01);
            if (s_mock.config[30] > previous) {
                --s_mock.config[31];
            } else {
                ++s_mock.config[31];
            }
        }
        if (s_mock.corrupt_identity_after_tx_call == index + 1U) {
            s_mock.identity[10] ^= UINT8_C(0x01);
        }
        if (s_mock.clear_fresh_after_tx) {
            s_mock.config[PLATFORM_TOUCH_GT911_CONFIG_BYTES - 1U] = 0U;
        }
        if (s_mock.invalidate_fresh_after_tx) {
            s_mock.config[PLATFORM_TOUCH_GT911_CONFIG_BYTES - 1U] = 2U;
        }
    }
    return ESP_OK;
}

esp_err_t esp_lcd_new_panel_io_i2c(
    i2c_master_bus_handle_t bus,
    const esp_lcd_panel_io_i2c_config_t *config,
    esp_lcd_panel_io_handle_t *out_io
)
{
    ++s_mock.io_create_calls;
    s_mock.created_on_bus = bus;
    const unsigned index = s_mock.io_create_calls - 1U;
    if (index < 2U) {
        s_mock.io_configs[index] = *config;
    }
    if (s_mock.io_create_result == ESP_OK) {
        *out_io = &s_io;
    }
    return s_mock.io_create_result;
}

esp_err_t esp_lcd_touch_new_i2c_gt911(
    esp_lcd_panel_io_handle_t io,
    const esp_lcd_touch_config_t *config,
    esp_lcd_touch_handle_t *out_touch
)
{
    ++s_mock.driver_create_calls;
    const unsigned index = s_mock.driver_create_calls - 1U;
    EXPECT_TRUE(io == &s_io);
    if (index < 2U) {
        s_mock.touch_configs[index] = *config;
        s_mock.io_deletes_before_driver_create[index] =
            s_mock.io_delete_calls;
    }
    if (index < 2U && config->driver_data != NULL) {
        const esp_lcd_touch_io_gt911_config_t *const driver_config =
            config->driver_data;
        s_mock.driver_addresses[index] = driver_config->dev_addr;
    }
    const esp_err_t result = index < 2U ?
        s_mock.driver_create_results[index] : ESP_FAIL;
    if (result == ESP_OK) {
        *out_touch = &s_driver;
    }
    return result;
}

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t touch)
{
    ++s_mock.read_calls;
    EXPECT_TRUE(touch == &s_driver);
    return s_mock.read_result;
}

bool esp_lcd_touch_get_coordinates(
    esp_lcd_touch_handle_t touch,
    uint16_t *x,
    uint16_t *y,
    uint16_t *strength,
    uint8_t *contact_count,
    uint8_t maximum_contacts
)
{
    ++s_mock.coordinates_calls;
    EXPECT_TRUE(touch == &s_driver);
    s_mock.requested_maximum = maximum_contacts;
    const uint8_t copied = s_mock.reported_count < maximum_contacts ?
        s_mock.reported_count : maximum_contacts;
    for (uint8_t index = 0U; index < copied; ++index) {
        x[index] = s_mock.x[index];
        y[index] = s_mock.y[index];
        strength[index] = s_mock.strength[index];
    }
    *contact_count = s_mock.reported_count;
    return s_mock.reported_count > 0U;
}

esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t touch)
{
    ++s_mock.driver_delete_calls;
    EXPECT_TRUE(touch == &s_driver);
    return s_mock.driver_delete_result;
}

esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io)
{
    ++s_mock.io_delete_calls;
    EXPECT_TRUE(io == &s_io);
    const unsigned index = s_mock.io_delete_calls - 1U;
    return index < 3U ? s_mock.io_delete_results[index] : ESP_FAIL;
}

int64_t esp_timer_get_time(void)
{
    return s_mock.now_us++;
}

void vTaskDelay(TickType_t ticks)
{
    ++s_mock.delay_calls;
    s_mock.delay_ticks = ticks;
}

static platform_touch_t *create_default(void)
{
    platform_touch_config_t config;
    platform_touch_config_init(&config, &s_bus);
    platform_touch_t *touch = NULL;
    EXPECT_EQ(ESP_OK, platform_touch_create(&config, &touch));
    EXPECT_TRUE(touch != NULL);
    return touch;
}

static void destroy_ok(platform_touch_t **touch)
{
    EXPECT_EQ(ESP_OK, platform_touch_destroy(touch));
    EXPECT_TRUE(*touch == NULL);
}

static void expect_frame_is_invalid_neutral(
    const platform_touch_frame_t *frame
)
{
    EXPECT_EQ(PLATFORM_TOUCH_VERSION, frame->version);
    EXPECT_EQ(sizeof(*frame), frame->size);
    EXPECT_EQ(0, frame->valid);
    EXPECT_EQ(0, frame->contact_count);
    for (size_t index = 0U; index < sizeof(frame->contacts); ++index) {
        EXPECT_EQ(0, ((const uint8_t *)frame->contacts)[index]);
    }
}

static void test_exact_vendor_configuration(void)
{
    mock_reset();
    platform_touch_t *touch = create_default();
    EXPECT_EQ(1, s_mock.io_create_calls);
    EXPECT_TRUE(s_mock.created_on_bus == &s_bus);
    EXPECT_EQ(PLATFORM_TOUCH_GT911_PRIMARY_ADDRESS,
              s_mock.io_configs[0].dev_addr);
    EXPECT_EQ(PLATFORM_TOUCH_I2C_CLOCK_HZ,
              s_mock.io_configs[0].scl_speed_hz);
    EXPECT_EQ(1, s_mock.io_configs[0].control_phase_bytes);
    EXPECT_EQ(16, s_mock.io_configs[0].lcd_cmd_bits);
    EXPECT_EQ(1, s_mock.io_configs[0].flags.disable_control_phase);
    EXPECT_EQ(PLATFORM_TOUCH_GT911_PRIMARY_ADDRESS,
              s_mock.driver_addresses[0]);
    EXPECT_EQ(PLATFORM_TOUCH_NATIVE_WIDTH, s_mock.touch_configs[0].x_max);
    EXPECT_EQ(PLATFORM_TOUCH_NATIVE_HEIGHT, s_mock.touch_configs[0].y_max);
    EXPECT_EQ(PLATFORM_TOUCH_RESET_GPIO,
              s_mock.touch_configs[0].rst_gpio_num);
    EXPECT_EQ(PLATFORM_TOUCH_INTERRUPT_GPIO,
              s_mock.touch_configs[0].int_gpio_num);
    EXPECT_EQ(0, s_mock.touch_configs[0].levels.reset);
    EXPECT_EQ(0, s_mock.touch_configs[0].levels.interrupt);
    EXPECT_EQ(0, s_mock.touch_configs[0].flags.swap_xy);
    EXPECT_EQ(0, s_mock.touch_configs[0].flags.mirror_x);
    EXPECT_EQ(0, s_mock.touch_configs[0].flags.mirror_y);
    EXPECT_TRUE(s_mock.touch_configs[0].interrupt_callback == NULL);
    destroy_ok(&touch);

    mock_reset();
    platform_touch_config_t config;
    platform_touch_config_init(&config, &s_bus);
    config.address_7bit = 0x14U;
    touch = NULL;
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_touch_create(&config, &touch));
    EXPECT_TRUE(touch == NULL);
    EXPECT_EQ(0, s_mock.io_create_calls);
}

static void test_primary_failure_cleans_before_backup(void)
{
    mock_reset();
    s_mock.driver_create_results[0] = ESP_FAIL;
    s_mock.driver_create_results[1] = ESP_OK;
    platform_touch_t *touch = create_default();
    EXPECT_EQ(2, s_mock.io_create_calls);
    EXPECT_EQ(2, s_mock.driver_create_calls);
    EXPECT_EQ(1, s_mock.io_delete_calls);
    EXPECT_EQ(1, s_mock.io_deletes_before_driver_create[1]);
    EXPECT_EQ(PLATFORM_TOUCH_GT911_PRIMARY_ADDRESS,
              s_mock.io_configs[0].dev_addr);
    EXPECT_EQ(PLATFORM_TOUCH_GT911_BACKUP_ADDRESS,
              s_mock.io_configs[1].dev_addr);
    EXPECT_EQ(PLATFORM_TOUCH_GT911_PRIMARY_ADDRESS,
              s_mock.driver_addresses[0]);
    EXPECT_EQ(PLATFORM_TOUCH_GT911_BACKUP_ADDRESS,
              s_mock.driver_addresses[1]);
    EXPECT_EQ(PLATFORM_TOUCH_I2C_CLOCK_HZ,
              s_mock.io_configs[1].scl_speed_hz);
    EXPECT_EQ(PLATFORM_TOUCH_RESET_GPIO,
              s_mock.touch_configs[1].rst_gpio_num);
    EXPECT_EQ(PLATFORM_TOUCH_INTERRUPT_GPIO,
              s_mock.touch_configs[1].int_gpio_num);
    EXPECT_TRUE(s_mock.touch_configs[1].interrupt_callback == NULL);
    destroy_ok(&touch);
    EXPECT_EQ(2, s_mock.io_delete_calls);
}

static void test_backup_failure_cleans_and_releases_owner(void)
{
    mock_reset();
    s_mock.driver_create_results[0] = ESP_FAIL;
    s_mock.driver_create_results[1] = ESP_ERR_INVALID_RESPONSE;
    platform_touch_config_t config;
    platform_touch_config_init(&config, &s_bus);
    platform_touch_t *touch = NULL;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_touch_create(&config, &touch));
    EXPECT_TRUE(touch == NULL);
    EXPECT_EQ(2, s_mock.io_create_calls);
    EXPECT_EQ(2, s_mock.driver_create_calls);
    EXPECT_EQ(2, s_mock.io_delete_calls);

    mock_reset();
    touch = create_default();
    destroy_ok(&touch);
}

static void test_backup_cleanup_failure_retains_owner(void)
{
    mock_reset();
    s_mock.driver_create_results[0] = ESP_FAIL;
    s_mock.driver_create_results[1] = ESP_FAIL;
    s_mock.io_delete_results[0] = ESP_OK;
    s_mock.io_delete_results[1] = ESP_ERR_TIMEOUT;
    platform_touch_config_t config;
    platform_touch_config_init(&config, &s_bus);
    platform_touch_t *touch = NULL;
    EXPECT_EQ(ESP_ERR_TIMEOUT, platform_touch_create(&config, &touch));
    EXPECT_TRUE(touch != NULL);
    EXPECT_EQ(2, s_mock.io_create_calls);
    EXPECT_EQ(2, s_mock.io_delete_calls);

    platform_touch_t *second = NULL;
    EXPECT_EQ(ESP_ERR_INVALID_STATE,
              platform_touch_create(&config, &second));
    s_mock.io_delete_results[2] = ESP_OK;
    destroy_ok(&touch);
    EXPECT_EQ(3, s_mock.io_delete_calls);
}

static void test_successful_five_contact_poll(void)
{
    mock_reset();
    platform_touch_t *touch = create_default();
    s_mock.reported_count = PLATFORM_TOUCH_MAX_CONTACTS;
    const uint16_t x[PLATFORM_TOUCH_MAX_CONTACTS] =
        {0U, 1023U, 32U, 400U, 900U};
    const uint16_t y[PLATFORM_TOUCH_MAX_CONTACTS] =
        {0U, 599U, 200U, 300U, 500U};
    for (uint8_t index = 0U; index < PLATFORM_TOUCH_MAX_CONTACTS; ++index) {
        s_mock.x[index] = x[index];
        s_mock.y[index] = y[index];
        s_mock.strength[index] = (uint16_t)(index + 1U);
    }
    platform_touch_frame_t frame;
    EXPECT_EQ(ESP_OK, platform_touch_poll(touch, &frame));
    EXPECT_EQ(1, frame.valid);
    EXPECT_EQ(5, frame.contact_count);
    EXPECT_EQ(5, s_mock.requested_maximum);
    EXPECT_EQ(1023, frame.contacts[1].x);
    EXPECT_EQ(599, frame.contacts[1].y);
    EXPECT_EQ(5, frame.contacts[4].strength);
    EXPECT_EQ(1, frame.sequence);
    /* With no pre-read data-ready flag, changed contents use the timestamp
     * immediately after the mocked driver's I2C acquisition. */
    EXPECT_EQ(1000001, frame.timestamp_us);
    destroy_ok(&touch);
}

static void test_gt911_info_read_and_checksum(void)
{
    mock_reset();
    for (size_t index = 0U;
         index < PLATFORM_TOUCH_GT911_CONFIG_BYTES - 2U; ++index) {
        s_mock.config[index] = (uint8_t)(index + 1U);
    }
    s_mock.config[0] = 0x66U;
    s_mock.config[1] = 0xe0U;
    s_mock.config[2] = 0x01U;
    s_mock.config[3] = 0x58U;
    s_mock.config[4] = 0x02U;
    s_mock.config[5] = 5U;
    s_mock.config[9] = 0x8aU;
    s_mock.config[15] = 0x14U;
    s_mock.config[16] = 3U;
    s_mock.config[17] = 4U;
    s_mock.config[22] = 5U;
    uint8_t sum = 0U;
    for (size_t index = 0U;
         index < PLATFORM_TOUCH_GT911_CONFIG_BYTES - 2U; ++index) {
        sum = (uint8_t)(sum + s_mock.config[index]);
    }
    s_mock.config[PLATFORM_TOUCH_GT911_CONFIG_BYTES - 2U] =
        (uint8_t)(0U - sum);
    s_mock.config[PLATFORM_TOUCH_GT911_CONFIG_BYTES - 1U] = 1U;
    s_mock.identity[0] = '9';
    s_mock.identity[1] = '1';
    s_mock.identity[2] = '1';
    s_mock.identity[3] = 0U;
    s_mock.identity[4] = 0x34U;
    s_mock.identity[5] = 0x12U;
    s_mock.identity[6] = 0xe0U;
    s_mock.identity[7] = 0x01U;
    s_mock.identity[8] = 0x58U;
    s_mock.identity[9] = 0x02U;
    s_mock.identity[10] = 0x05U;

    platform_touch_t *touch = create_default();
    platform_touch_gt911_info_t info;
    EXPECT_EQ(ESP_OK, platform_touch_gt911_read_info(touch, &info));
    EXPECT_EQ(2, s_mock.rx_calls);
    EXPECT_EQ(MOCK_GT911_CONFIG_REG, s_mock.rx_registers[0]);
    EXPECT_EQ(PLATFORM_TOUCH_GT911_CONFIG_BYTES, s_mock.rx_lengths[0]);
    EXPECT_EQ(MOCK_GT911_IDENTITY_REG, s_mock.rx_registers[1]);
    EXPECT_EQ(PLATFORM_TOUCH_GT911_IDENTITY_BYTES, s_mock.rx_lengths[1]);
    EXPECT_EQ(0x66, info.config_version);
    EXPECT_EQ(0x01e0, info.config_x_resolution);
    EXPECT_EQ(0x0258, info.config_y_resolution);
    EXPECT_EQ(5, info.max_touch_points);
    EXPECT_EQ(0x8a, info.filter);
    EXPECT_EQ(2, info.first_filter);
    EXPECT_EQ(10, info.normal_filter);
    EXPECT_EQ(0x14, info.refresh_rate);
    EXPECT_EQ(4, info.refresh_n);
    EXPECT_EQ(9, info.report_period_ms);
    EXPECT_EQ(3, info.x_threshold);
    EXPECT_EQ(4, info.y_threshold);
    EXPECT_EQ(5, info.mini_filter);
    EXPECT_EQ('9', info.product_id[0]);
    EXPECT_EQ(0x1234, info.firmware_version);
    EXPECT_EQ(0x01e0, info.identity_x_resolution);
    EXPECT_EQ(0x0258, info.identity_y_resolution);
    EXPECT_EQ(0x05, info.vendor_id);
    EXPECT_TRUE(info.config_checksum_valid);
    EXPECT_TRUE(info.config_fresh);

    s_mock.config[10] ^= 1U;
    EXPECT_EQ(ESP_OK, platform_touch_gt911_read_info(touch, &info));
    EXPECT_TRUE(!info.config_checksum_valid);
    destroy_ok(&touch);
}

static void test_gt911_info_read_error_clears_output(void)
{
    mock_reset();
    platform_touch_t *touch = create_default();
    platform_touch_gt911_info_t info;
    memset(&info, 0xa5, sizeof(info));
    s_mock.rx_result = ESP_FAIL;
    EXPECT_EQ(ESP_FAIL, platform_touch_gt911_read_info(touch, &info));
    for (size_t index = 0U; index < sizeof(info); ++index) {
        EXPECT_EQ(0, ((const uint8_t *)&info)[index]);
    }
    destroy_ok(&touch);
}

static uint8_t test_gt911_checksum(const uint8_t *config)
{
    uint8_t sum = 0U;
    for (size_t index = 0U; index < 184U; ++index) {
        sum = (uint8_t)(sum + config[index]);
    }
    return (uint8_t)(0U - sum);
}

static const uint8_t s_reviewed_restore_original[
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

static void load_restore_reviewed_original(void)
{
    memcpy(s_mock.config, s_reviewed_restore_original,
           sizeof(s_reviewed_restore_original));
    EXPECT_EQ(0x79, test_gt911_checksum(s_mock.config));
    const uint8_t identity[PLATFORM_TOUCH_GT911_IDENTITY_BYTES] = {
        '9', '1', '1', 0U, 0x60U, 0x10U, 0xe0U, 0x01U,
        0x20U, 0x03U, 0x00U,
    };
    memcpy(s_mock.identity, identity, sizeof(identity));
}

static platform_touch_gt911_restore_reviewed_baseline_request_t
reviewed_restore_request(void)
{
    platform_touch_gt911_restore_reviewed_baseline_request_t request = {0};
    memcpy(request.expected_identity, s_mock.identity,
           sizeof(request.expected_identity));
    memcpy(request.original_config, s_mock.config,
           sizeof(request.original_config));
    return request;
}

static void set_mock_to_reviewed_filter4(void)
{
    s_mock.config[9] = 0x04U;
    s_mock.config[184] = test_gt911_checksum(s_mock.config);
    EXPECT_EQ(0x7d, s_mock.config[184]);
}

static void expect_restore_request_rejected(
    const platform_touch_gt911_restore_reviewed_baseline_request_t *request)
{
    platform_touch_t *touch = create_default();
    platform_touch_gt911_restore_reviewed_baseline_result_t result;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_touch_gt911_restore_reviewed_baseline(
                  touch, request, &result));
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE, result.result);
    EXPECT_EQ(0, s_mock.rx_calls);
    EXPECT_EQ(0, s_mock.tx_calls);
    EXPECT_TRUE(!result.changed);
    EXPECT_TRUE(!result.already_original);
    EXPECT_TRUE(!result.may_have_changed);
    destroy_ok(&touch);
}

static void expect_restore_current_rejected(
    const platform_touch_gt911_restore_reviewed_baseline_request_t *request)
{
    platform_touch_t *touch = create_default();
    platform_touch_gt911_restore_reviewed_baseline_result_t result;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_touch_gt911_restore_reviewed_baseline(
                  touch, request, &result));
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE, result.result);
    EXPECT_EQ(2, s_mock.rx_calls);
    EXPECT_EQ(0, s_mock.tx_calls);
    EXPECT_TRUE(!result.changed);
    EXPECT_TRUE(!result.already_original);
    EXPECT_TRUE(!result.may_have_changed);
    destroy_ok(&touch);
}

static void test_gt911_restore_reviewed_baseline_exact_write(void)
{
    mock_reset();
    load_restore_reviewed_original();
    const platform_touch_gt911_restore_reviewed_baseline_request_t request =
        reviewed_restore_request();
    set_mock_to_reviewed_filter4();

    uint8_t expected_write[PLATFORM_TOUCH_GT911_CONFIG_BYTES];
    memcpy(expected_write, request.original_config, sizeof(expected_write));
    expected_write[185] = 1U;

    platform_touch_t *touch = create_default();
    platform_touch_gt911_restore_reviewed_baseline_result_t result;
    EXPECT_EQ(ESP_OK, platform_touch_gt911_restore_reviewed_baseline(
                          touch, &request, &result));
    EXPECT_EQ(ESP_OK, result.result);
    EXPECT_TRUE(result.changed);
    EXPECT_TRUE(!result.already_original);
    EXPECT_TRUE(result.may_have_changed);
    EXPECT_EQ(4, result.before.normal_filter);
    EXPECT_EQ(0x7d, result.before.config_checksum);
    EXPECT_EQ(8, result.observed.normal_filter);
    EXPECT_EQ(0x79, result.observed.config_checksum);
    EXPECT_TRUE(result.observed.config_fresh);
    EXPECT_EQ(1, s_mock.tx_calls);
    EXPECT_EQ(MOCK_GT911_CONFIG_REG, s_mock.tx_registers[0]);
    EXPECT_EQ(PLATFORM_TOUCH_GT911_CONFIG_BYTES, s_mock.tx_lengths[0]);
    EXPECT_TRUE(memcmp(expected_write, s_mock.tx_configs[0],
                       sizeof(expected_write)) == 0);
    EXPECT_EQ(1, s_mock.delay_calls);
    EXPECT_EQ(11, s_mock.delay_ticks);
    EXPECT_EQ(4, s_mock.rx_calls);
    destroy_ok(&touch);
}

static void test_gt911_restore_reviewed_baseline_accepts_cleared_fresh(void)
{
    mock_reset();
    load_restore_reviewed_original();
    const platform_touch_gt911_restore_reviewed_baseline_request_t request =
        reviewed_restore_request();
    set_mock_to_reviewed_filter4();
    s_mock.clear_fresh_after_tx = true;

    platform_touch_t *touch = create_default();
    platform_touch_gt911_restore_reviewed_baseline_result_t result;
    EXPECT_EQ(ESP_OK, platform_touch_gt911_restore_reviewed_baseline(
                          touch, &request, &result));
    EXPECT_EQ(ESP_OK, result.result);
    EXPECT_TRUE(result.changed);
    EXPECT_TRUE(result.may_have_changed);
    EXPECT_TRUE(!result.observed.config_fresh);
    EXPECT_EQ(1, s_mock.tx_configs[0][185]);
    EXPECT_EQ(0, s_mock.config[185]);
    EXPECT_EQ(1, s_mock.tx_calls);
    destroy_ok(&touch);
}

static void test_gt911_restore_reviewed_baseline_already_original_noop(void)
{
    mock_reset();
    load_restore_reviewed_original();
    const platform_touch_gt911_restore_reviewed_baseline_request_t request =
        reviewed_restore_request();

    platform_touch_t *touch = create_default();
    platform_touch_gt911_restore_reviewed_baseline_result_t result;
    EXPECT_EQ(ESP_OK, platform_touch_gt911_restore_reviewed_baseline(
                          touch, &request, &result));
    EXPECT_EQ(ESP_OK, result.result);
    EXPECT_TRUE(!result.changed);
    EXPECT_TRUE(result.already_original);
    EXPECT_TRUE(!result.may_have_changed);
    EXPECT_EQ(0, s_mock.tx_calls);
    EXPECT_EQ(0, s_mock.delay_calls);
    EXPECT_TRUE(memcmp(&result.before, &result.observed,
                       sizeof(result.before)) == 0);
    destroy_ok(&touch);
}

static void test_gt911_restore_reviewed_baseline_invalid_args_reported(void)
{
    mock_reset();
    load_restore_reviewed_original();
    const platform_touch_gt911_restore_reviewed_baseline_request_t request =
        reviewed_restore_request();
    platform_touch_gt911_restore_reviewed_baseline_result_t result;
    memset(&result, 0xa5, sizeof(result));
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_touch_gt911_restore_reviewed_baseline(
                  NULL, &request, &result));
    EXPECT_EQ(ESP_ERR_INVALID_ARG, result.result);
    EXPECT_TRUE(!result.may_have_changed);
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_touch_gt911_restore_reviewed_baseline(
                  NULL, &request, NULL));

    platform_touch_t *touch = create_default();
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_touch_gt911_restore_reviewed_baseline(
                  touch, NULL, &result));
    EXPECT_EQ(ESP_ERR_INVALID_ARG, result.result);
    EXPECT_EQ(0, s_mock.rx_calls);
    EXPECT_EQ(0, s_mock.tx_calls);
    destroy_ok(&touch);
}

static void test_gt911_restore_rejects_malformed_sealed_baseline(void)
{
    mock_reset();
    load_restore_reviewed_original();
    platform_touch_gt911_restore_reviewed_baseline_request_t request =
        reviewed_restore_request();
    request.original_config[30] ^= UINT8_C(0x01);
    request.original_config[184] =
        test_gt911_checksum(request.original_config);
    EXPECT_TRUE(request.original_config[184] != UINT8_C(0x79));
    expect_restore_request_rejected(&request);

    mock_reset();
    load_restore_reviewed_original();
    request = reviewed_restore_request();
    request.original_config[184] ^= UINT8_C(0x01);
    expect_restore_request_rejected(&request);

    mock_reset();
    load_restore_reviewed_original();
    request = reviewed_restore_request();
    request.original_config[185] = 1U;
    expect_restore_request_rejected(&request);

    mock_reset();
    load_restore_reviewed_original();
    request = reviewed_restore_request();
    request.original_config[9] = 0x48U;
    request.original_config[30] =
        (uint8_t)(request.original_config[30] - UINT8_C(0x40));
    EXPECT_EQ(0x79, test_gt911_checksum(request.original_config));
    expect_restore_request_rejected(&request);
}

static void test_gt911_restore_rejects_checksum_neutral_request_mutation(void)
{
    mock_reset();
    load_restore_reviewed_original();
    platform_touch_gt911_restore_reviewed_baseline_request_t request =
        reviewed_restore_request();
    ++request.original_config[30];
    --request.original_config[31];
    EXPECT_EQ(0x79, test_gt911_checksum(request.original_config));
    EXPECT_EQ(0x79, request.original_config[184]);
    EXPECT_EQ(0x08, request.original_config[9]);
    EXPECT_EQ(0, request.original_config[185]);
    expect_restore_request_rejected(&request);
}

static void test_gt911_restore_rejects_request_and_device_identity_comutation(
    void)
{
    mock_reset();
    load_restore_reviewed_original();
    platform_touch_gt911_restore_reviewed_baseline_request_t request =
        reviewed_restore_request();
    request.expected_identity[10] ^= UINT8_C(0x01);
    s_mock.identity[10] ^= UINT8_C(0x01);
    EXPECT_TRUE(memcmp(request.expected_identity, s_mock.identity,
                       sizeof(request.expected_identity)) == 0);
    expect_restore_request_rejected(&request);
}

static void test_gt911_restore_rejects_every_sealed_baseline_byte_change(void)
{
    for (size_t changed = 0U;
         changed < PLATFORM_TOUCH_GT911_CONFIG_BYTES; ++changed) {
        mock_reset();
        load_restore_reviewed_original();
        platform_touch_gt911_restore_reviewed_baseline_request_t request =
            reviewed_restore_request();
        request.original_config[changed] ^= UINT8_C(0x01);
        expect_restore_request_rejected(&request);
    }
}

static void test_gt911_restore_rejects_every_current_byte_change(void)
{
    for (size_t changed = 0U;
         changed < PLATFORM_TOUCH_GT911_CONFIG_BYTES - 2U; ++changed) {
        mock_reset();
        load_restore_reviewed_original();
        const platform_touch_gt911_restore_reviewed_baseline_request_t request =
            reviewed_restore_request();
        set_mock_to_reviewed_filter4();
        const size_t compensator = changed == 30U ? 31U : 30U;
        const uint8_t previous = s_mock.config[changed];
        s_mock.config[changed] ^= UINT8_C(0x01);
        if (s_mock.config[changed] > previous) {
            --s_mock.config[compensator];
        } else {
            ++s_mock.config[compensator];
        }
        EXPECT_EQ(0x7d, test_gt911_checksum(s_mock.config));
        expect_restore_current_rejected(&request);
    }

    for (size_t changed = PLATFORM_TOUCH_GT911_CONFIG_BYTES - 2U;
         changed < PLATFORM_TOUCH_GT911_CONFIG_BYTES; ++changed) {
        mock_reset();
        load_restore_reviewed_original();
        const platform_touch_gt911_restore_reviewed_baseline_request_t request =
            reviewed_restore_request();
        set_mock_to_reviewed_filter4();
        s_mock.config[changed] ^= UINT8_C(0x01);
        expect_restore_current_rejected(&request);
    }

    mock_reset();
    load_restore_reviewed_original();
    const platform_touch_gt911_restore_reviewed_baseline_request_t request =
        reviewed_restore_request();
    set_mock_to_reviewed_filter4();
    s_mock.config[9] ^= UINT8_C(0x40);
    s_mock.config[30] = (uint8_t)(s_mock.config[30] - UINT8_C(0x40));
    EXPECT_EQ(0x7d, test_gt911_checksum(s_mock.config));
    expect_restore_current_rejected(&request);
}

static void test_gt911_restore_rejects_every_identity_byte_change(void)
{
    for (size_t changed = 0U;
         changed < PLATFORM_TOUCH_GT911_IDENTITY_BYTES; ++changed) {
        mock_reset();
        load_restore_reviewed_original();
        const platform_touch_gt911_restore_reviewed_baseline_request_t request =
            reviewed_restore_request();
        set_mock_to_reviewed_filter4();
        s_mock.identity[changed] ^= UINT8_C(0x01);
        expect_restore_current_rejected(&request);
    }
}

static void test_gt911_restore_write_failure_never_rewrites_filter4(void)
{
    mock_reset();
    load_restore_reviewed_original();
    const platform_touch_gt911_restore_reviewed_baseline_request_t request =
        reviewed_restore_request();
    set_mock_to_reviewed_filter4();
    uint8_t rejected_filter4[PLATFORM_TOUCH_GT911_CONFIG_BYTES];
    memcpy(rejected_filter4, s_mock.config, sizeof(rejected_filter4));
    s_mock.tx_results[0] = ESP_FAIL;

    platform_touch_t *touch = create_default();
    platform_touch_gt911_restore_reviewed_baseline_result_t result;
    EXPECT_EQ(ESP_FAIL, platform_touch_gt911_restore_reviewed_baseline(
                            touch, &request, &result));
    EXPECT_EQ(ESP_FAIL, result.result);
    EXPECT_TRUE(result.may_have_changed);
    EXPECT_TRUE(!result.changed);
    EXPECT_TRUE(!result.already_original);
    EXPECT_EQ(1, s_mock.tx_calls);
    EXPECT_EQ(0, s_mock.delay_calls);
    EXPECT_TRUE(memcmp(rejected_filter4, s_mock.config,
                       sizeof(rejected_filter4)) == 0);
    destroy_ok(&touch);
}

static void test_gt911_restore_initial_read_failures_are_reported(void)
{
    for (unsigned fail_call = 1U; fail_call <= 2U; ++fail_call) {
        mock_reset();
        load_restore_reviewed_original();
        const platform_touch_gt911_restore_reviewed_baseline_request_t request =
            reviewed_restore_request();
        set_mock_to_reviewed_filter4();
        s_mock.fail_rx_call = fail_call;
        s_mock.fail_rx_result = ESP_ERR_TIMEOUT;

        platform_touch_t *touch = create_default();
        platform_touch_gt911_restore_reviewed_baseline_result_t result;
        EXPECT_EQ(ESP_ERR_TIMEOUT,
                  platform_touch_gt911_restore_reviewed_baseline(
                      touch, &request, &result));
        EXPECT_EQ(ESP_ERR_TIMEOUT, result.result);
        EXPECT_TRUE(!result.may_have_changed);
        EXPECT_TRUE(!result.changed);
        EXPECT_TRUE(!result.already_original);
        EXPECT_EQ(0, s_mock.tx_calls);
        EXPECT_EQ(fail_call, s_mock.rx_calls);
        for (size_t index = 0U; index < sizeof(result.before); ++index) {
            EXPECT_EQ(0, ((const uint8_t *)&result.before)[index]);
        }
        destroy_ok(&touch);
    }
}

static void test_gt911_restore_readback_failure_never_retries(void)
{
    for (unsigned fail_call = 3U; fail_call <= 4U; ++fail_call) {
        mock_reset();
        load_restore_reviewed_original();
        const platform_touch_gt911_restore_reviewed_baseline_request_t request =
            reviewed_restore_request();
        set_mock_to_reviewed_filter4();
        s_mock.fail_rx_call = fail_call;
        s_mock.fail_rx_result = ESP_ERR_TIMEOUT;

        platform_touch_t *touch = create_default();
        platform_touch_gt911_restore_reviewed_baseline_result_t result;
        EXPECT_EQ(ESP_ERR_TIMEOUT,
                  platform_touch_gt911_restore_reviewed_baseline(
                      touch, &request, &result));
        EXPECT_EQ(ESP_ERR_TIMEOUT, result.result);
        EXPECT_TRUE(result.may_have_changed);
        EXPECT_TRUE(!result.changed);
        EXPECT_EQ(1, s_mock.tx_calls);
        EXPECT_EQ(1, s_mock.delay_calls);
        EXPECT_EQ(fail_call, s_mock.rx_calls);
        for (size_t index = 0U; index < sizeof(result.observed); ++index) {
            EXPECT_EQ(0, ((const uint8_t *)&result.observed)[index]);
        }
        destroy_ok(&touch);
    }
}

static void test_gt911_restore_verification_failures_never_retry(void)
{
    mock_reset();
    load_restore_reviewed_original();
    platform_touch_gt911_restore_reviewed_baseline_request_t request =
        reviewed_restore_request();
    set_mock_to_reviewed_filter4();
    s_mock.corrupt_after_tx_call = 1U;

    platform_touch_t *touch = create_default();
    platform_touch_gt911_restore_reviewed_baseline_result_t result;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_touch_gt911_restore_reviewed_baseline(
                  touch, &request, &result));
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE, result.result);
    EXPECT_TRUE(result.may_have_changed);
    EXPECT_TRUE(!result.changed);
    EXPECT_EQ(1, s_mock.tx_calls);
    EXPECT_EQ(1, s_mock.delay_calls);
    destroy_ok(&touch);

    mock_reset();
    load_restore_reviewed_original();
    request = reviewed_restore_request();
    set_mock_to_reviewed_filter4();
    s_mock.corrupt_valid_config_after_tx_call = 1U;
    touch = create_default();
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_touch_gt911_restore_reviewed_baseline(
                  touch, &request, &result));
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE, result.result);
    EXPECT_TRUE(result.observed.config_checksum_valid);
    EXPECT_TRUE(result.may_have_changed);
    EXPECT_TRUE(!result.changed);
    EXPECT_EQ(1, s_mock.tx_calls);
    EXPECT_EQ(1, s_mock.delay_calls);
    destroy_ok(&touch);

    mock_reset();
    load_restore_reviewed_original();
    request = reviewed_restore_request();
    set_mock_to_reviewed_filter4();
    s_mock.corrupt_identity_after_tx_call = 1U;
    touch = create_default();
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_touch_gt911_restore_reviewed_baseline(
                  touch, &request, &result));
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE, result.result);
    EXPECT_TRUE(result.may_have_changed);
    EXPECT_TRUE(!result.changed);
    EXPECT_EQ(1, s_mock.tx_calls);
    EXPECT_EQ(1, s_mock.delay_calls);
    destroy_ok(&touch);

    mock_reset();
    load_restore_reviewed_original();
    request = reviewed_restore_request();
    set_mock_to_reviewed_filter4();
    s_mock.invalidate_fresh_after_tx = true;
    touch = create_default();
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE,
              platform_touch_gt911_restore_reviewed_baseline(
                  touch, &request, &result));
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE, result.result);
    EXPECT_TRUE(result.observed.config_checksum_valid);
    EXPECT_TRUE(result.may_have_changed);
    EXPECT_TRUE(!result.changed);
    EXPECT_EQ(1, s_mock.tx_calls);
    EXPECT_EQ(1, s_mock.delay_calls);
    destroy_ok(&touch);
}

static void test_report_timestamp_tracks_data_ready_event(void)
{
    mock_reset();
    platform_touch_t *touch = create_default();
    s_mock.reported_count = 1U;
    s_mock.x[0] = 100U;
    s_mock.y[0] = 200U;
    s_mock.strength[0] = 300U;
    s_mock.status = 0x81U;
    platform_touch_frame_t first;
    EXPECT_EQ(ESP_OK, platform_touch_poll(touch, &first));
    EXPECT_TRUE(first.timestamp_us > 0);
    const int64_t first_timestamp = first.timestamp_us;

    /* A repeated driver snapshot without a new GT911 data-ready report must
     * retain the original event timestamp. */
    s_mock.status = 0U;
    platform_touch_frame_t stationary;
    EXPECT_EQ(ESP_OK, platform_touch_poll(touch, &stationary));
    EXPECT_EQ(first_timestamp, stationary.timestamp_us);

    /* If a report arrives between the pre-read and the pinned driver's read,
     * changed content provides the race-safe fallback timestamp. */
    s_mock.x[0] = 101U;
    platform_touch_frame_t raced;
    EXPECT_EQ(ESP_OK, platform_touch_poll(touch, &raced));
    EXPECT_TRUE(raced.timestamp_us > stationary.timestamp_us);

    s_mock.status = 0x80U;
    s_mock.reported_count = 0U;
    platform_touch_frame_t released;
    EXPECT_EQ(ESP_OK, platform_touch_poll(touch, &released));
    EXPECT_EQ(1, released.valid);
    EXPECT_EQ(0, released.contact_count);
    EXPECT_EQ(0, released.timestamp_us);
    destroy_ok(&touch);
}

static void test_status_read_error_fail_closed(void)
{
    mock_reset();
    platform_touch_t *touch = create_default();
    platform_touch_frame_t frame;
    s_mock.rx_result = ESP_FAIL;
    memset(&frame, 0xa5, sizeof(frame));
    EXPECT_EQ(ESP_FAIL, platform_touch_poll(touch, &frame));
    expect_frame_is_invalid_neutral(&frame);
    EXPECT_EQ(0, s_mock.read_calls);
    destroy_ok(&touch);
}

static void test_read_error_fail_closed(void)
{
    mock_reset();
    platform_touch_t *touch = create_default();
    s_mock.read_result = ESP_FAIL;
    platform_touch_frame_t frame;
    memset(&frame, 0xa5, sizeof(frame));
    EXPECT_EQ(ESP_FAIL, platform_touch_poll(touch, &frame));
    expect_frame_is_invalid_neutral(&frame);
    EXPECT_EQ(0, s_mock.coordinates_calls);
    destroy_ok(&touch);
}

static void test_malformed_driver_output_rejected(void)
{
    mock_reset();
    platform_touch_t *touch = create_default();
    platform_touch_frame_t frame;

    s_mock.reported_count = 6U;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE, platform_touch_poll(touch, &frame));
    expect_frame_is_invalid_neutral(&frame);
    EXPECT_EQ(5, s_mock.requested_maximum);

    s_mock.reported_count = 1U;
    s_mock.x[0] = PLATFORM_TOUCH_WIDTH;
    s_mock.y[0] = 1U;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE, platform_touch_poll(touch, &frame));
    expect_frame_is_invalid_neutral(&frame);

    s_mock.x[0] = 1U;
    s_mock.y[0] = PLATFORM_TOUCH_HEIGHT;
    EXPECT_EQ(ESP_ERR_INVALID_RESPONSE, platform_touch_poll(touch, &frame));
    expect_frame_is_invalid_neutral(&frame);
    destroy_ok(&touch);
}

static void test_driver_create_failure_retains_failed_cleanup(void)
{
    mock_reset();
    s_mock.driver_create_results[0] = ESP_FAIL;
    s_mock.io_delete_results[0] = ESP_ERR_TIMEOUT;
    platform_touch_config_t config;
    platform_touch_config_init(&config, &s_bus);
    platform_touch_t *touch = NULL;
    EXPECT_EQ(ESP_ERR_TIMEOUT, platform_touch_create(&config, &touch));
    EXPECT_TRUE(touch != NULL);
    EXPECT_EQ(1, s_mock.io_delete_calls);

    platform_touch_frame_t frame;
    memset(&frame, 0xa5, sizeof(frame));
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_touch_poll(touch, &frame));
    expect_frame_is_invalid_neutral(&frame);

    platform_touch_t *second = NULL;
    EXPECT_EQ(ESP_ERR_INVALID_STATE,
              platform_touch_create(&config, &second));
    EXPECT_TRUE(second == NULL);

    s_mock.io_delete_results[1] = ESP_OK;
    destroy_ok(&touch);
    EXPECT_EQ(2, s_mock.io_delete_calls);
}

static void test_destroy_failure_retains_owner_and_bus_borrow(void)
{
    mock_reset();
    platform_touch_t *touch = create_default();
    platform_touch_t *const original = touch;

    s_mock.driver_delete_result = ESP_FAIL;
    EXPECT_EQ(ESP_FAIL, platform_touch_destroy(&touch));
    EXPECT_TRUE(touch == original);
    EXPECT_EQ(0, s_mock.io_delete_calls);

    s_mock.driver_delete_result = ESP_OK;
    s_mock.io_delete_results[0] = ESP_ERR_TIMEOUT;
    EXPECT_EQ(ESP_ERR_TIMEOUT, platform_touch_destroy(&touch));
    EXPECT_TRUE(touch == original);
    EXPECT_EQ(1, s_mock.io_delete_calls);

    platform_touch_config_t config;
    platform_touch_config_init(&config, &s_bus);
    platform_touch_t *second = NULL;
    EXPECT_EQ(ESP_ERR_INVALID_STATE,
              platform_touch_create(&config, &second));

    platform_touch_frame_t frame;
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_touch_poll(touch, &frame));
    expect_frame_is_invalid_neutral(&frame);

    s_mock.io_delete_results[1] = ESP_OK;
    destroy_ok(&touch);
    EXPECT_EQ(2, s_mock.io_delete_calls);

    mock_reset();
    touch = create_default();
    EXPECT_TRUE(s_mock.created_on_bus == &s_bus);
    destroy_ok(&touch);
}

int main(void)
{
    test_exact_vendor_configuration();
    test_primary_failure_cleans_before_backup();
    test_backup_failure_cleans_and_releases_owner();
    test_backup_cleanup_failure_retains_owner();
    test_successful_five_contact_poll();
    test_gt911_info_read_and_checksum();
    test_gt911_info_read_error_clears_output();
    test_gt911_restore_reviewed_baseline_exact_write();
    test_gt911_restore_reviewed_baseline_accepts_cleared_fresh();
    test_gt911_restore_reviewed_baseline_already_original_noop();
    test_gt911_restore_reviewed_baseline_invalid_args_reported();
    test_gt911_restore_rejects_malformed_sealed_baseline();
    test_gt911_restore_rejects_checksum_neutral_request_mutation();
    test_gt911_restore_rejects_request_and_device_identity_comutation();
    test_gt911_restore_rejects_every_sealed_baseline_byte_change();
    test_gt911_restore_rejects_every_current_byte_change();
    test_gt911_restore_rejects_every_identity_byte_change();
    test_gt911_restore_write_failure_never_rewrites_filter4();
    test_gt911_restore_initial_read_failures_are_reported();
    test_gt911_restore_readback_failure_never_retries();
    test_gt911_restore_verification_failures_never_retry();
    test_report_timestamp_tracks_data_ready_event();
    test_status_read_error_fail_closed();
    test_read_error_fail_closed();
    test_malformed_driver_output_rejected();
    test_driver_create_failure_retains_failed_cleanup();
    test_destroy_failure_retains_owner_and_bus_borrow();
    if (failures != 0U) {
        fprintf(stderr, "platform touch runtime failures: %u\n", failures);
        return 1;
    }
    puts("P4_TOUCH_RUNTIME HOST PASS addresses=0x5d,0x14 hz=400000 "
         "gpio=40,42 no_isr=true fallback_cleanup=true contacts=5 "
         "lifecycle=retained fail_closed=true bus=borrowed");
    return 0;
}
