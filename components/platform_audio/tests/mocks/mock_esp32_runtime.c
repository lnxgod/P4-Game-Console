#include "mock_esp32_runtime.h"

#include <string.h>

#include "driver/gpio.h"
#include "freertos/task.h"

struct mock_i2s_channel {
    unsigned marker;
};

static struct mock_i2s_channel s_channel;
mock_esp32_runtime_t g_mock_esp32;

static void record_event(mock_event_t event)
{
    if (g_mock_esp32.event_count < (size_t)MOCK_MAX_EVENTS) {
        g_mock_esp32.events[g_mock_esp32.event_count++] = event;
    }
}

void mock_esp32_reset(void)
{
    const bool allocated = g_mock_esp32.channel_allocated;
    const bool enabled = g_mock_esp32.channel_enabled;
    memset(&g_mock_esp32, 0, sizeof(g_mock_esp32));
    /* Hardware ownership persists across a software-observation reset. */
    g_mock_esp32.channel_allocated = allocated;
    g_mock_esp32.channel_enabled = enabled;
    g_mock_esp32.amp_level = UINT32_C(1);
}

esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level)
{
    (void)pin;
    record_event(level == 0U ? MOCK_EVENT_GPIO_LOW : MOCK_EVENT_GPIO_HIGH);
    if (g_mock_esp32.gpio_set_result == ESP_OK) {
        g_mock_esp32.amp_level = level;
    }
    return g_mock_esp32.gpio_set_result;
}

esp_err_t gpio_config(const gpio_config_t *config)
{
    (void)config;
    record_event(MOCK_EVENT_GPIO_CONFIG);
    return g_mock_esp32.gpio_config_result;
}

esp_err_t i2s_new_channel(const i2s_chan_config_t *config,
                          i2s_chan_handle_t *tx_handle,
                          i2s_chan_handle_t *rx_handle)
{
    (void)rx_handle;
    record_event(MOCK_EVENT_I2S_NEW);
    ++g_mock_esp32.new_calls;
    if (g_mock_esp32.i2s_new_result != ESP_OK) {
        return g_mock_esp32.i2s_new_result;
    }
    if (config == NULL || tx_handle == NULL ||
        g_mock_esp32.channel_allocated) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_esp32.channel_config = *config;
    g_mock_esp32.channel_allocated = true;
    *tx_handle = &s_channel;
    return ESP_OK;
}

esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t handle,
                                    const i2s_std_config_t *config)
{
    record_event(MOCK_EVENT_I2S_INIT);
    if (g_mock_esp32.i2s_init_result != ESP_OK) {
        return g_mock_esp32.i2s_init_result;
    }
    if (handle != &s_channel || config == NULL ||
        !g_mock_esp32.channel_allocated) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_esp32.standard_config = *config;
    return ESP_OK;
}

esp_err_t i2s_channel_enable(i2s_chan_handle_t handle)
{
    record_event(MOCK_EVENT_I2S_ENABLE);
    if (g_mock_esp32.i2s_enable_result != ESP_OK) {
        return g_mock_esp32.i2s_enable_result;
    }
    if (handle != &s_channel || !g_mock_esp32.channel_allocated) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_esp32.channel_enabled = true;
    return ESP_OK;
}

esp_err_t i2s_channel_preload_data(i2s_chan_handle_t handle,
                                   const void *src,
                                   size_t size,
                                   size_t *bytes_loaded)
{
    record_event(MOCK_EVENT_I2S_PRELOAD);
    ++g_mock_esp32.preload_calls;
    if (g_mock_esp32.i2s_preload_result != ESP_OK) {
        return g_mock_esp32.i2s_preload_result;
    }
    if (handle != &s_channel || src == NULL || bytes_loaded == NULL ||
        !g_mock_esp32.channel_allocated || g_mock_esp32.channel_enabled ||
        g_mock_esp32.dma_preload_position > sizeof(g_mock_esp32.dma_ring)) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t available = sizeof(g_mock_esp32.dma_ring) -
                       g_mock_esp32.dma_preload_position;
    size_t loaded = size < available ? size : available;
    if (g_mock_esp32.force_partial_preload && loaded > 0U) {
        --loaded;
    }
    memcpy(&g_mock_esp32.dma_ring[g_mock_esp32.dma_preload_position],
           src, loaded);
    g_mock_esp32.dma_preload_position += loaded;
    *bytes_loaded = loaded;
    return ESP_OK;
}

esp_err_t i2s_channel_write(i2s_chan_handle_t handle,
                            const void *src,
                            size_t size,
                            size_t *bytes_written,
                            uint32_t timeout_ms)
{
    record_event(MOCK_EVENT_I2S_WRITE);
    if (handle != &s_channel || src == NULL || bytes_written == NULL ||
        !g_mock_esp32.channel_enabled ||
        g_mock_esp32.write_calls >= (unsigned)MOCK_MAX_WRITES ||
        size > (size_t)MOCK_MAX_WRITE_BYTES) {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_mock_esp32.i2s_write_result != ESP_OK) {
        return g_mock_esp32.i2s_write_result;
    }
    const unsigned index = g_mock_esp32.write_calls++;
    g_mock_esp32.write_timeouts[index] = timeout_ms;
    g_mock_esp32.write_sizes[index] = size;
    memcpy(g_mock_esp32.write_data[index], src, size);
    *bytes_written = g_mock_esp32.force_partial_write && size > 0U
        ? size - 1U : size;
    for (size_t byte = 0U; byte < *bytes_written; ++byte) {
        g_mock_esp32.dma_ring[g_mock_esp32.dma_write_position] =
            ((const uint8_t *)src)[byte];
        g_mock_esp32.dma_write_position =
            (g_mock_esp32.dma_write_position + 1U) %
            sizeof(g_mock_esp32.dma_ring);
    }
    return ESP_OK;
}

esp_err_t i2s_channel_disable(i2s_chan_handle_t handle)
{
    record_event(MOCK_EVENT_I2S_DISABLE);
    if (g_mock_esp32.i2s_disable_result != ESP_OK) {
        return g_mock_esp32.i2s_disable_result;
    }
    if (handle != &s_channel || !g_mock_esp32.channel_allocated) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_esp32.channel_enabled = false;
    g_mock_esp32.dma_preload_position = 0U;
    g_mock_esp32.dma_write_position = 0U;
    return ESP_OK;
}

esp_err_t i2s_del_channel(i2s_chan_handle_t handle)
{
    record_event(MOCK_EVENT_I2S_DELETE);
    ++g_mock_esp32.delete_calls;
    if (g_mock_esp32.i2s_delete_result != ESP_OK) {
        return g_mock_esp32.i2s_delete_result;
    }
    if (handle != &s_channel || !g_mock_esp32.channel_allocated ||
        g_mock_esp32.channel_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    g_mock_esp32.channel_allocated = false;
    return ESP_OK;
}

void vTaskDelay(TickType_t ticks)
{
    record_event(MOCK_EVENT_DELAY);
    g_mock_esp32.delay_ticks = ticks;
}
