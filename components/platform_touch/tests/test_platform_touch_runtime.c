#include "platform/touch.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"

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
    s_mock.driver_delete_result = ESP_OK;
    s_mock.io_delete_results[0] = ESP_OK;
    s_mock.io_delete_results[1] = ESP_OK;
    s_mock.io_delete_results[2] = ESP_OK;
    s_mock.now_us = 1000000;
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
    EXPECT_EQ(PLATFORM_TOUCH_WIDTH, s_mock.touch_configs[0].x_max);
    EXPECT_EQ(PLATFORM_TOUCH_HEIGHT, s_mock.touch_configs[0].y_max);
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
    EXPECT_EQ(1000000, frame.timestamp_us);
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
