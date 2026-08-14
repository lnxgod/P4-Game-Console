#include "platform_i2c_shared/bus.h"

#include <stdio.h>
#include <string.h>

struct mock_i2c_bus {
    unsigned token;
};

static int failures;
static struct mock_i2c_bus s_handle = {.token = 0x45U};
static i2c_master_bus_config_t s_last_config;
static esp_err_t s_create_result;
static esp_err_t s_delete_result;
static esp_err_t s_probe_result;
static unsigned s_create_calls;
static unsigned s_delete_calls;
static unsigned s_probe_calls;
static uint16_t s_probe_address;
static int s_probe_timeout;

#define EXPECT_TRUE(value_) do { \
    if (!(value_)) { \
        fprintf(stderr, "%s:%d expectation failed: %s\n", \
                __FILE__, __LINE__, #value_); \
        ++failures; \
    } \
} while (0)

#define EXPECT_EQ(expected_, actual_) do { \
    const long long expected_value = (long long)(expected_); \
    const long long actual_value = (long long)(actual_); \
    if (expected_value != actual_value) { \
        fprintf(stderr, "%s:%d expected %lld, got %lld\n", \
                __FILE__, __LINE__, expected_value, actual_value); \
        ++failures; \
    } \
} while (0)

esp_err_t i2c_new_master_bus(
    const i2c_master_bus_config_t *config,
    i2c_master_bus_handle_t *out_handle
)
{
    ++s_create_calls;
    s_last_config = *config;
    if (s_create_result == ESP_OK) {
        *out_handle = &s_handle;
    }
    return s_create_result;
}

esp_err_t i2c_master_probe(
    i2c_master_bus_handle_t handle,
    uint16_t address,
    int timeout_ms
)
{
    ++s_probe_calls;
    EXPECT_TRUE(handle == &s_handle);
    s_probe_address = address;
    s_probe_timeout = timeout_ms;
    return s_probe_result;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t handle)
{
    ++s_delete_calls;
    EXPECT_TRUE(handle == &s_handle);
    return s_delete_result;
}

static void reset_mocks(void)
{
    memset(&s_last_config, 0, sizeof(s_last_config));
    s_create_result = ESP_OK;
    s_delete_result = ESP_OK;
    s_probe_result = ESP_OK;
    s_create_calls = 0U;
    s_delete_calls = 0U;
    s_probe_calls = 0U;
    s_probe_address = 0U;
    s_probe_timeout = 0;
}

static void test_lifecycle_and_exact_config(void)
{
    reset_mocks();
    platform_i2c_shared_t *bus = NULL;
    EXPECT_EQ(ESP_OK, platform_i2c_shared_create(&bus));
    EXPECT_TRUE(bus != NULL);
    EXPECT_TRUE(platform_i2c_shared_handle(bus) == &s_handle);
    EXPECT_EQ(1, s_create_calls);
    EXPECT_EQ(I2C_NUM_1, s_last_config.i2c_port);
    EXPECT_EQ(45, s_last_config.sda_io_num);
    EXPECT_EQ(46, s_last_config.scl_io_num);
    EXPECT_EQ(7, s_last_config.glitch_ignore_cnt);
    EXPECT_EQ(1, s_last_config.flags.enable_internal_pullup);
    EXPECT_EQ(0, s_last_config.flags.allow_pd);

    platform_i2c_shared_t *second = NULL;
    EXPECT_EQ(ESP_ERR_INVALID_STATE,
              platform_i2c_shared_create(&second));
    EXPECT_TRUE(second == NULL);
    EXPECT_EQ(1, s_create_calls);

    EXPECT_EQ(ESP_OK, platform_i2c_shared_probe(bus, 0x5dU, 50));
    EXPECT_EQ(1, s_probe_calls);
    EXPECT_EQ(0x5d, s_probe_address);
    EXPECT_EQ(50, s_probe_timeout);

    EXPECT_EQ(ESP_OK, platform_i2c_shared_destroy(&bus));
    EXPECT_TRUE(bus == NULL);
    EXPECT_EQ(1, s_delete_calls);
}

static void test_failures_retain_truthful_owner(void)
{
    reset_mocks();
    platform_i2c_shared_t *bus = NULL;
    s_create_result = ESP_FAIL;
    EXPECT_EQ(ESP_FAIL, platform_i2c_shared_create(&bus));
    EXPECT_TRUE(bus == NULL);
    EXPECT_EQ(1, s_create_calls);

    s_create_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_i2c_shared_create(&bus));
    s_delete_result = ESP_FAIL;
    EXPECT_EQ(ESP_FAIL, platform_i2c_shared_destroy(&bus));
    EXPECT_TRUE(bus != NULL);
    EXPECT_TRUE(platform_i2c_shared_handle(bus) == &s_handle);
    platform_i2c_shared_t *second = NULL;
    EXPECT_EQ(ESP_ERR_INVALID_STATE,
              platform_i2c_shared_create(&second));

    s_delete_result = ESP_OK;
    EXPECT_EQ(ESP_OK, platform_i2c_shared_destroy(&bus));
    EXPECT_TRUE(bus == NULL);
}

static void test_argument_bounds(void)
{
    reset_mocks();
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_i2c_shared_create(NULL));
    platform_i2c_shared_t *garbage = (platform_i2c_shared_t *)&s_handle;
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_i2c_shared_create(&garbage));
    EXPECT_TRUE(platform_i2c_shared_handle(NULL) == NULL);
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_i2c_shared_probe(NULL, 0x5dU, 50));

    platform_i2c_shared_t *bus = NULL;
    EXPECT_EQ(ESP_OK, platform_i2c_shared_create(&bus));
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_i2c_shared_probe(bus, 0x07U, 50));
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_i2c_shared_probe(bus, 0x78U, 50));
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_i2c_shared_probe(bus, 0x5dU, 0));
    EXPECT_EQ(ESP_ERR_INVALID_ARG,
              platform_i2c_shared_probe(bus, 0x5dU, 1001));
    EXPECT_EQ(0, s_probe_calls);
    EXPECT_EQ(ESP_OK, platform_i2c_shared_destroy(&bus));
    EXPECT_EQ(ESP_ERR_INVALID_ARG, platform_i2c_shared_destroy(&bus));
}

int main(void)
{
    test_lifecycle_and_exact_config();
    test_failures_retain_truthful_owner();
    test_argument_bounds();
    if (failures != 0) {
        fprintf(stderr, "%d platform I2C shared test(s) failed\n", failures);
        return 1;
    }
    puts("P4_I2C_SHARED HOST PASS owner=single pins=45/46 borrowers=bounded");
    return 0;
}
