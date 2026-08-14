#include "mock_factory_runtime.h"

#include <string.h>

struct mock_i2s_channel {
    unsigned marker;
};

static struct mock_i2s_channel s_pdm_channel = {.marker = 1U};
static struct mock_i2s_channel s_tx_channel = {.marker = 2U};
mock_factory_runtime_t g_mock_factory;

static void record_event(mock_factory_event_t event)
{
    if (g_mock_factory.event_count < (size_t)MOCK_FACTORY_MAX_EVENTS) {
        g_mock_factory.events[g_mock_factory.event_count++] = event;
    }
}

bool mock_factory_dma_ring_is_zero(void)
{
    for (size_t index = 0U; index < sizeof(g_mock_factory.dma_ring); ++index) {
        if (g_mock_factory.dma_ring[index] != 0U) {
            return false;
        }
    }
    return true;
}

void mock_factory_reset(void)
{
    memset(&g_mock_factory, 0, sizeof(g_mock_factory));
    g_mock_factory.amp_level = UINT32_C(1);
}

esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{
    (void)pin;
    record_event(level == 0U ? MOCK_FACTORY_EVENT_GPIO_LOW
                             : MOCK_FACTORY_EVENT_GPIO_HIGH);
    const esp_err_t configured_result =
        level == 0U ? g_mock_factory.gpio_set_low_result
                    : g_mock_factory.gpio_set_high_result;
    if (configured_result == ESP_OK) {
        g_mock_factory.amp_level = level;
        if (level == 0U) {
            g_mock_factory.low_saw_pdm_enabled = g_mock_factory.pdm_enabled;
            g_mock_factory.low_saw_tx_enabled = g_mock_factory.tx_enabled;
            g_mock_factory.low_saw_zero_ring = mock_factory_dma_ring_is_zero();
        }
    }
    return configured_result;
}

int gpio_get_level(gpio_num_t pin)
{
    (void)pin;
    int level = (int)g_mock_factory.amp_level;
    if (level == 0 &&
        (g_mock_factory.force_low_readback_high ||
         (g_mock_factory.force_post_delay_low_readback_high &&
          g_mock_factory.delay_ticks > 0U))) {
        level = 1;
    } else if (level == 1 && g_mock_factory.force_high_readback_low) {
        level = 0;
    }
    record_event(level == 0 ? MOCK_FACTORY_EVENT_GPIO_GET_LOW
                            : MOCK_FACTORY_EVENT_GPIO_GET_HIGH);
    return level;
}

esp_err_t gpio_config(const gpio_config_t *configuration)
{
    record_event(MOCK_FACTORY_EVENT_GPIO_CONFIG);
    if (g_mock_factory.gpio_config_result != ESP_OK) {
        return g_mock_factory.gpio_config_result;
    }
    if (configuration == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    g_mock_factory.gpio_configuration = *configuration;
    g_mock_factory.gpio_configured = true;
    return ESP_OK;
}

esp_err_t i2s_new_channel(const i2s_chan_config_t *configuration,
                          i2s_chan_handle_t *out_tx,
                          i2s_chan_handle_t *out_rx)
{
    if (configuration == NULL || (out_tx == NULL) == (out_rx == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_rx != NULL) {
        record_event(MOCK_FACTORY_EVENT_PDM_NEW);
        ++g_mock_factory.pdm_new_calls;
        if (g_mock_factory.pdm_new_result != ESP_OK) {
            return g_mock_factory.pdm_new_result;
        }
        if (g_mock_factory.pdm_allocated) {
            return ESP_ERR_INVALID_STATE;
        }
        g_mock_factory.pdm_channel_configuration = *configuration;
        g_mock_factory.pdm_allocated = true;
        *out_rx = &s_pdm_channel;
        return ESP_OK;
    }

    record_event(MOCK_FACTORY_EVENT_TX_NEW);
    ++g_mock_factory.tx_new_calls;
    if (g_mock_factory.tx_new_result != ESP_OK) {
        return g_mock_factory.tx_new_result;
    }
    if (g_mock_factory.tx_allocated) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_factory.tx_channel_configuration = *configuration;
    g_mock_factory.tx_allocated = true;
    *out_tx = &s_tx_channel;
    return ESP_OK;
}

esp_err_t i2s_channel_init_pdm_rx_mode(
    i2s_chan_handle_t channel,
    const i2s_pdm_rx_config_t *configuration)
{
    record_event(MOCK_FACTORY_EVENT_PDM_INIT);
    if (g_mock_factory.pdm_init_result != ESP_OK) {
        return g_mock_factory.pdm_init_result;
    }
    if (channel != &s_pdm_channel || configuration == NULL ||
        !g_mock_factory.pdm_allocated) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_factory.pdm_configuration = *configuration;
    g_mock_factory.pdm_initialized = true;
    return ESP_OK;
}

esp_err_t i2s_channel_init_std_mode(
    i2s_chan_handle_t channel,
    const i2s_std_config_t *configuration)
{
    record_event(MOCK_FACTORY_EVENT_TX_INIT);
    if (g_mock_factory.tx_init_result != ESP_OK) {
        return g_mock_factory.tx_init_result;
    }
    if (channel != &s_tx_channel || configuration == NULL ||
        !g_mock_factory.tx_allocated) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_factory.tx_configuration = *configuration;
    g_mock_factory.tx_initialized = true;
    return ESP_OK;
}

esp_err_t i2s_channel_enable(i2s_chan_handle_t channel)
{
    if (channel == &s_pdm_channel) {
        record_event(MOCK_FACTORY_EVENT_PDM_ENABLE);
        if (g_mock_factory.pdm_enable_result != ESP_OK) {
            return g_mock_factory.pdm_enable_result;
        }
        if (!g_mock_factory.pdm_allocated || !g_mock_factory.pdm_initialized ||
            g_mock_factory.pdm_enabled) {
            return ESP_ERR_INVALID_STATE;
        }
        g_mock_factory.pdm_enabled = true;
        return ESP_OK;
    }
    record_event(MOCK_FACTORY_EVENT_TX_ENABLE);
    if (g_mock_factory.tx_enable_result != ESP_OK) {
        return g_mock_factory.tx_enable_result;
    }
    if (channel != &s_tx_channel || !g_mock_factory.tx_allocated ||
        !g_mock_factory.tx_initialized || g_mock_factory.tx_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_factory.tx_enabled = true;
    return ESP_OK;
}

esp_err_t i2s_channel_disable(i2s_chan_handle_t channel)
{
    if (channel == &s_pdm_channel) {
        record_event(MOCK_FACTORY_EVENT_PDM_DISABLE);
        if (g_mock_factory.pdm_disable_result != ESP_OK) {
            return g_mock_factory.pdm_disable_result;
        }
        if (!g_mock_factory.pdm_allocated || !g_mock_factory.pdm_enabled) {
            return ESP_ERR_INVALID_STATE;
        }
        g_mock_factory.pdm_enabled = false;
        return ESP_OK;
    }
    record_event(MOCK_FACTORY_EVENT_TX_DISABLE);
    if (g_mock_factory.tx_disable_result != ESP_OK) {
        return g_mock_factory.tx_disable_result;
    }
    if (channel != &s_tx_channel || !g_mock_factory.tx_allocated ||
        !g_mock_factory.tx_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_factory.tx_enabled = false;
    g_mock_factory.dma_preload_position = 0U;
    g_mock_factory.dma_write_position = 0U;
    return ESP_OK;
}

esp_err_t i2s_channel_preload_data(i2s_chan_handle_t channel,
                                   const void *data,
                                   size_t byte_count,
                                   size_t *out_loaded)
{
    record_event(MOCK_FACTORY_EVENT_TX_PRELOAD);
    ++g_mock_factory.preload_calls;
    if (g_mock_factory.tx_preload_result != ESP_OK) {
        return g_mock_factory.tx_preload_result;
    }
    if (channel != &s_tx_channel || data == NULL || out_loaded == NULL ||
        !g_mock_factory.tx_allocated || !g_mock_factory.tx_initialized ||
        g_mock_factory.tx_enabled ||
        g_mock_factory.dma_preload_position > sizeof(g_mock_factory.dma_ring)) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t available =
        sizeof(g_mock_factory.dma_ring) - g_mock_factory.dma_preload_position;
    size_t loaded = byte_count < available ? byte_count : available;
    if (g_mock_factory.force_partial_preload && loaded > 0U) {
        --loaded;
    }
    memcpy(&g_mock_factory.dma_ring[g_mock_factory.dma_preload_position],
           data, loaded);
    g_mock_factory.dma_preload_position += loaded;
    *out_loaded = loaded;
    return ESP_OK;
}

esp_err_t i2s_channel_write(i2s_chan_handle_t channel,
                            const void *data,
                            size_t byte_count,
                            size_t *out_written,
                            uint32_t timeout_ms)
{
    record_event(MOCK_FACTORY_EVENT_TX_WRITE);
    if (g_mock_factory.tx_write_result != ESP_OK) {
        return g_mock_factory.tx_write_result;
    }
    if (channel != &s_tx_channel || data == NULL || out_written == NULL ||
        !g_mock_factory.tx_allocated || !g_mock_factory.tx_enabled ||
        g_mock_factory.write_calls >= (unsigned)MOCK_FACTORY_MAX_WRITES ||
        byte_count > (size_t)MOCK_FACTORY_MAX_WRITE_BYTES) {
        return ESP_ERR_INVALID_STATE;
    }
    const unsigned index = g_mock_factory.write_calls++;
    g_mock_factory.write_timeouts[index] = timeout_ms;
    g_mock_factory.write_sizes[index] = byte_count;
    memcpy(g_mock_factory.write_data[index], data, byte_count);
    *out_written = g_mock_factory.force_partial_write && byte_count > 0U
                       ? byte_count - 1U
                       : byte_count;
    for (size_t byte = 0U; byte < *out_written; ++byte) {
        g_mock_factory.dma_ring[g_mock_factory.dma_write_position] =
            ((const uint8_t *)data)[byte];
        g_mock_factory.dma_write_position =
            (g_mock_factory.dma_write_position + 1U) %
            sizeof(g_mock_factory.dma_ring);
    }
    return ESP_OK;
}

esp_err_t i2s_del_channel(i2s_chan_handle_t channel)
{
    if (channel == &s_tx_channel) {
        record_event(MOCK_FACTORY_EVENT_TX_DELETE);
        ++g_mock_factory.tx_delete_calls;
        if (g_mock_factory.tx_delete_result != ESP_OK) {
            return g_mock_factory.tx_delete_result;
        }
        if (!g_mock_factory.tx_allocated || g_mock_factory.tx_enabled) {
            return ESP_ERR_INVALID_STATE;
        }
        g_mock_factory.tx_allocated = false;
        g_mock_factory.tx_initialized = false;
        return ESP_OK;
    }
    record_event(MOCK_FACTORY_EVENT_PDM_DELETE);
    ++g_mock_factory.pdm_delete_calls;
    if (g_mock_factory.pdm_delete_result != ESP_OK) {
        return g_mock_factory.pdm_delete_result;
    }
    if (channel != &s_pdm_channel || !g_mock_factory.pdm_allocated ||
        g_mock_factory.pdm_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_factory.pdm_allocated = false;
    g_mock_factory.pdm_initialized = false;
    return ESP_OK;
}

void vTaskDelay(TickType_t ticks)
{
    record_event(MOCK_FACTORY_EVENT_DELAY);
    g_mock_factory.delay_ticks = ticks;
    g_mock_factory.delay_saw_amp_enabled = g_mock_factory.amp_level == 0U;
    g_mock_factory.delay_saw_pdm_enabled = g_mock_factory.pdm_enabled;
    g_mock_factory.delay_saw_tx_enabled = g_mock_factory.tx_enabled;
    g_mock_factory.delay_saw_zero_ring = mock_factory_dma_ring_is_zero();
    const uint32_t elapsed_ms =
        g_mock_factory.force_short_settle && ticks > 1U ? ticks - 2U : ticks;
    g_mock_factory.monotonic_time_us +=
        (int64_t)elapsed_ms * INT64_C(1000);
}

int64_t esp_timer_get_time(void)
{
    return g_mock_factory.monotonic_time_us;
}
