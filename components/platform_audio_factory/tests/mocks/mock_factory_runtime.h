#ifndef MOCK_FACTORY_RUNTIME_H
#define MOCK_FACTORY_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform_audio_factory_test_hal.h"

enum {
    MOCK_FACTORY_MAX_WRITES = 2048,
    MOCK_FACTORY_MAX_WRITE_BYTES = 1024,
    MOCK_FACTORY_MAX_EVENTS = 1024,
    MOCK_FACTORY_DMA_BYTES = 6144,
};

typedef enum {
    MOCK_FACTORY_EVENT_GPIO_HIGH = 1,
    MOCK_FACTORY_EVENT_GPIO_LOW,
    MOCK_FACTORY_EVENT_GPIO_CONFIG,
    MOCK_FACTORY_EVENT_GPIO_GET_HIGH,
    MOCK_FACTORY_EVENT_GPIO_GET_LOW,
    MOCK_FACTORY_EVENT_PDM_NEW,
    MOCK_FACTORY_EVENT_PDM_INIT,
    MOCK_FACTORY_EVENT_PDM_ENABLE,
    MOCK_FACTORY_EVENT_TX_NEW,
    MOCK_FACTORY_EVENT_TX_INIT,
    MOCK_FACTORY_EVENT_TX_ENABLE,
    MOCK_FACTORY_EVENT_TX_PRELOAD,
    MOCK_FACTORY_EVENT_TX_WRITE,
    MOCK_FACTORY_EVENT_DELAY,
    MOCK_FACTORY_EVENT_TX_DISABLE,
    MOCK_FACTORY_EVENT_TX_DELETE,
    MOCK_FACTORY_EVENT_PDM_DISABLE,
    MOCK_FACTORY_EVENT_PDM_DELETE,
} mock_factory_event_t;

typedef struct {
    esp_err_t gpio_set_high_result;
    esp_err_t gpio_set_low_result;
    esp_err_t gpio_config_result;
    esp_err_t pdm_new_result;
    esp_err_t pdm_init_result;
    esp_err_t pdm_enable_result;
    esp_err_t tx_new_result;
    esp_err_t tx_init_result;
    esp_err_t tx_enable_result;
    esp_err_t tx_preload_result;
    esp_err_t tx_write_result;
    esp_err_t tx_disable_result;
    esp_err_t tx_delete_result;
    esp_err_t pdm_disable_result;
    esp_err_t pdm_delete_result;
    bool force_high_readback_low;
    bool force_low_readback_high;
    bool force_post_delay_low_readback_high;
    bool force_partial_write;
    bool force_partial_preload;
    bool force_short_settle;

    bool gpio_configured;
    gpio_config_t gpio_configuration;
    uint32_t amp_level;
    bool pdm_allocated;
    bool pdm_initialized;
    bool pdm_enabled;
    bool tx_allocated;
    bool tx_initialized;
    bool tx_enabled;
    i2s_chan_config_t pdm_channel_configuration;
    i2s_pdm_rx_config_t pdm_configuration;
    i2s_chan_config_t tx_channel_configuration;
    i2s_std_config_t tx_configuration;
    unsigned pdm_new_calls;
    unsigned pdm_delete_calls;
    unsigned tx_new_calls;
    unsigned tx_delete_calls;
    unsigned preload_calls;
    unsigned write_calls;
    uint32_t write_timeouts[MOCK_FACTORY_MAX_WRITES];
    size_t write_sizes[MOCK_FACTORY_MAX_WRITES];
    uint8_t write_data[MOCK_FACTORY_MAX_WRITES]
                      [MOCK_FACTORY_MAX_WRITE_BYTES];
    uint8_t dma_ring[MOCK_FACTORY_DMA_BYTES];
    size_t dma_preload_position;
    size_t dma_write_position;
    TickType_t delay_ticks;
    int64_t monotonic_time_us;
    bool low_saw_pdm_enabled;
    bool low_saw_tx_enabled;
    bool low_saw_zero_ring;
    bool delay_saw_amp_enabled;
    bool delay_saw_pdm_enabled;
    bool delay_saw_tx_enabled;
    bool delay_saw_zero_ring;
    mock_factory_event_t events[MOCK_FACTORY_MAX_EVENTS];
    size_t event_count;
} mock_factory_runtime_t;

extern mock_factory_runtime_t g_mock_factory;

void mock_factory_reset(void);
bool mock_factory_dma_ring_is_zero(void);

#endif
