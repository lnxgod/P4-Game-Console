#ifndef MOCK_ESP32_RUNTIME_H
#define MOCK_ESP32_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2s_std.h"
#include "esp_err.h"

enum {
    MOCK_MAX_WRITES = 16,
    MOCK_MAX_WRITE_BYTES = 1024,
    MOCK_MAX_EVENTS = 512,
};

typedef enum {
    MOCK_EVENT_GPIO_HIGH = 1,
    MOCK_EVENT_GPIO_LOW,
    MOCK_EVENT_GPIO_CONFIG,
    MOCK_EVENT_I2S_NEW,
    MOCK_EVENT_I2S_INIT,
    MOCK_EVENT_I2S_ENABLE,
    MOCK_EVENT_I2S_PRELOAD,
    MOCK_EVENT_I2S_WRITE,
    MOCK_EVENT_DELAY,
    MOCK_EVENT_I2S_DISABLE,
    MOCK_EVENT_I2S_DELETE,
} mock_event_t;

typedef struct {
    esp_err_t gpio_set_result;
    esp_err_t gpio_config_result;
    esp_err_t i2s_new_result;
    esp_err_t i2s_init_result;
    esp_err_t i2s_enable_result;
    esp_err_t i2s_preload_result;
    esp_err_t i2s_write_result;
    esp_err_t i2s_disable_result;
    esp_err_t i2s_delete_result;
    bool force_partial_write;
    bool force_partial_preload;

    bool channel_allocated;
    bool channel_enabled;
    uint32_t amp_level;
    uint32_t delay_ticks;
    unsigned new_calls;
    unsigned delete_calls;
    unsigned write_calls;
    unsigned preload_calls;
    i2s_chan_config_t channel_config;
    i2s_std_config_t standard_config;
    uint32_t write_timeouts[MOCK_MAX_WRITES];
    size_t write_sizes[MOCK_MAX_WRITES];
    uint8_t write_data[MOCK_MAX_WRITES][MOCK_MAX_WRITE_BYTES];
    uint8_t dma_ring[6144];
    size_t dma_preload_position;
    size_t dma_write_position;
    mock_event_t events[MOCK_MAX_EVENTS];
    size_t event_count;
} mock_esp32_runtime_t;

extern mock_esp32_runtime_t g_mock_esp32;

void mock_esp32_reset(void);

#endif
