#include "platform/audio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform_audio_policy.h"
#include "sdkconfig.h"

#define AUDIO_DMA_DESCRIPTOR_COUNT 6U
#define AUDIO_DMA_FRAMES_PER_DESCRIPTOR 256U
#define AUDIO_DMA_RING_FRAMES \
    (AUDIO_DMA_DESCRIPTOR_COUNT * AUDIO_DMA_FRAMES_PER_DESCRIPTOR)
#define AUDIO_AMP_SETTLE_MS 20U

_Static_assert(PLATFORM_AUDIO_MAX_WRITE_FRAMES <=
                   AUDIO_DMA_FRAMES_PER_DESCRIPTOR,
               "one public write must fit one DMA frame block");
_Static_assert(PLATFORM_AUDIO_DMA_DESCRIPTOR_COUNT ==
                   AUDIO_DMA_DESCRIPTOR_COUNT,
               "tested DMA descriptor count changed");
_Static_assert(PLATFORM_AUDIO_DMA_FRAMES_PER_DESCRIPTOR ==
                   AUDIO_DMA_FRAMES_PER_DESCRIPTOR,
               "tested DMA descriptor frame count changed");
_Static_assert(PLATFORM_AUDIO_ZERO_PREROLL_FRAMES == AUDIO_DMA_RING_FRAMES,
               "zero preroll must overwrite the complete DMA ring");
_Static_assert(PLATFORM_AUDIO_WRITE_TIMEOUT_MS < 250,
               "audio write must remain below Doom's stop budget");
_Static_assert(PLATFORM_AUDIO_CHANNEL_COUNT == 2U,
               "direct factory path is stereo");
_Static_assert(PLATFORM_AUDIO_BITS_PER_SAMPLE == 16U,
               "direct factory path is PCM16");

struct platform_audio {
    i2s_chan_handle_t tx_channel;
    platform_audio_state_t state;
    uint8_t volume_percent;
    bool tx_enabled;
    int16_t staging[
        PLATFORM_AUDIO_MAX_WRITE_FRAMES * PLATFORM_AUDIO_CHANNEL_COUNT
    ];
};

/*
 * If driver rollback fails during create, retain sole ownership here. A later
 * create retries cleanup before acquiring anything new. This intentionally
 * prefers a bounded retained object over losing a live I2S channel handle.
 */
static platform_audio_t *s_failed_create_owner;
static platform_audio_t *s_live_owner;

static esp_err_t set_amplifier_enabled(bool enabled)
{
    return gpio_set_level(
        (gpio_num_t)PLATFORM_AUDIO_GPIO_AMP_SHUTDOWN,
        platform_audio_amp_gpio_level(enabled) ? 1U : 0U
    );
}

esp_err_t platform_audio_force_safe_shutdown(void)
{
    const gpio_num_t pin = (gpio_num_t)PLATFORM_AUDIO_GPIO_AMP_SHUTDOWN;

    /* Assert the inactive level before changing pad direction. */
    esp_err_t result = gpio_set_level(pin, 1U);
    if (result != ESP_OK) {
        return result;
    }
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << PLATFORM_AUDIO_GPIO_AMP_SHUTDOWN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&config);
    if (result != ESP_OK) {
        return result;
    }
    return gpio_set_level(pin, 1U);
}

static esp_err_t configure_i2s(platform_audio_t *audio)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)PLATFORM_AUDIO_I2S_CONTROLLER,
        I2S_ROLE_MASTER
    );
    channel_config.dma_desc_num = AUDIO_DMA_DESCRIPTOR_COUNT;
    channel_config.dma_frame_num = AUDIO_DMA_FRAMES_PER_DESCRIPTOR;
    channel_config.auto_clear = true;

    esp_err_t result = i2s_new_channel(
        &channel_config, &audio->tx_channel, NULL
    );
    if (result != ESP_OK) {
        return result;
    }

    const i2s_std_config_t standard_config = {
        .clk_cfg = {
            .sample_rate_hz = PLATFORM_AUDIO_SAMPLE_RATE_HZ,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PLATFORM_AUDIO_GPIO_BCLK,
            .ws = (gpio_num_t)PLATFORM_AUDIO_GPIO_LRCLK,
            .dout = (gpio_num_t)PLATFORM_AUDIO_GPIO_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    result = i2s_channel_init_std_mode(audio->tx_channel, &standard_config);
    if (result != ESP_OK) {
        return result;
    }
    return result;
}

static esp_err_t write_exact_staging(platform_audio_t *audio,
                                     size_t frame_count)
{
    if (audio == NULL || audio->tx_channel == NULL || !audio->tx_enabled ||
        frame_count == 0U ||
        frame_count > (size_t)PLATFORM_AUDIO_MAX_WRITE_FRAMES) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t byte_count = frame_count * PLATFORM_AUDIO_CHANNEL_COUNT *
                              sizeof(audio->staging[0]);
    size_t bytes_written = 0U;
    const esp_err_t result = i2s_channel_write(
        audio->tx_channel, audio->staging, byte_count, &bytes_written,
        PLATFORM_AUDIO_WRITE_TIMEOUT_MS
    );
    if (result != ESP_OK) {
        return result;
    }
    return bytes_written == byte_count ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t reset_channel_to_ready(platform_audio_t *audio)
{
    if (audio == NULL || audio->tx_channel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /*
     * A READY channel can contain a partial prior preload. Enable then disable
     * while GPIO30 is high so ESP-IDF resets curr_ptr, rw_pos, and the TX
     * queue. A RUNNING channel needs only the disable half of that sequence.
     */
    if (!audio->tx_enabled) {
        const esp_err_t enable_result =
            i2s_channel_enable(audio->tx_channel);
        if (enable_result != ESP_OK) {
            return enable_result;
        }
        audio->tx_enabled = true;
    }
    const esp_err_t disable_result = i2s_channel_disable(audio->tx_channel);
    if (disable_result == ESP_OK) {
        audio->tx_enabled = false;
    }
    return disable_result;
}

static esp_err_t preload_exact_zero_dma_ring(platform_audio_t *audio)
{
    memset(audio->staging, 0, sizeof(audio->staging));
    size_t frames_remaining = AUDIO_DMA_RING_FRAMES;
    while (frames_remaining > 0U) {
        const size_t chunk_frames =
            frames_remaining > (size_t)PLATFORM_AUDIO_MAX_WRITE_FRAMES
                ? (size_t)PLATFORM_AUDIO_MAX_WRITE_FRAMES
                : frames_remaining;
        const size_t requested_bytes =
            chunk_frames * PLATFORM_AUDIO_CHANNEL_COUNT *
            sizeof(audio->staging[0]);
        size_t loaded_bytes = 0U;
        const esp_err_t result = i2s_channel_preload_data(
            audio->tx_channel, audio->staging, requested_bytes, &loaded_bytes
        );
        if (result != ESP_OK) {
            return result;
        }
        if (loaded_bytes != requested_bytes) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        frames_remaining -= chunk_frames;
    }
    return ESP_OK;
}

static esp_err_t reprime_zero_dma_ring_amp_off(platform_audio_t *audio)
{
    esp_err_t result = platform_audio_force_safe_shutdown();
    if (result == ESP_OK) {
        result = reset_channel_to_ready(audio);
    }
    if (result == ESP_OK) {
        result = preload_exact_zero_dma_ring(audio);
    }
    if (result == ESP_OK) {
        result = i2s_channel_enable(audio->tx_channel);
        if (result == ESP_OK) {
            audio->tx_enabled = true;
        }
    }
    return result;
}

/* GPIO30 must already be confirmed high before this function is called. */
static esp_err_t release_i2s(platform_audio_t *audio)
{
    if (audio == NULL || audio->tx_channel == NULL) {
        return ESP_OK;
    }
    if (audio->tx_enabled) {
        const esp_err_t disable_result =
            i2s_channel_disable(audio->tx_channel);
        if (disable_result != ESP_OK &&
            disable_result != ESP_ERR_INVALID_STATE) {
            return disable_result;
        }
        audio->tx_enabled = false;
    }
    const esp_err_t delete_result = i2s_del_channel(audio->tx_channel);
    if (delete_result == ESP_OK) {
        audio->tx_channel = NULL;
    }
    return delete_result;
}

static esp_err_t recover_failed_create_owner_after_shutdown(void)
{
    if (s_failed_create_owner == NULL) {
        return ESP_OK;
    }
    const esp_err_t result = release_i2s(s_failed_create_owner);
    if (result == ESP_OK) {
        free(s_failed_create_owner);
        s_failed_create_owner = NULL;
    }
    return result;
}

esp_err_t platform_audio_recover(void)
{
    if (s_live_owner != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t shutdown_result = platform_audio_force_safe_shutdown();
    if (shutdown_result != ESP_OK) {
        return shutdown_result;
    }
    return recover_failed_create_owner_after_shutdown();
}

static esp_err_t rollback_failed_create(platform_audio_t *audio,
                                        esp_err_t primary_result)
{
    if (audio == NULL) {
        return primary_result;
    }
    esp_err_t cleanup_result = platform_audio_force_safe_shutdown();
    if (cleanup_result == ESP_OK) {
        cleanup_result = release_i2s(audio);
    }
    if (cleanup_result == ESP_OK) {
        free(audio);
    } else {
        /* No concurrent create is allowed by the public serialization rule. */
        s_failed_create_owner = audio;
    }
    return primary_result != ESP_OK ? primary_result : cleanup_result;
}

esp_err_t platform_audio_create(const platform_audio_config_t *config,
                                platform_audio_t **out_audio)
{
#if !CONFIG_PLATFORM_AUDIO_ELECROW_10_1_REVIEWED_BUILD_ONLY
    (void)config;
    (void)out_audio;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (config == NULL || out_audio == NULL || *out_audio != NULL ||
        config->control_bus != NULL ||
        !platform_audio_sample_rate_supported(config->sample_rate_hz) ||
        !platform_audio_bringup_volume_supported(config->volume_percent)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_live_owner != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = platform_audio_recover();
    if (result != ESP_OK) {
        return result;
    }

    platform_audio_t *audio = calloc(1U, sizeof(*audio));
    if (audio == NULL) {
        return ESP_ERR_NO_MEM;
    }
    audio->state = PLATFORM_AUDIO_STATE_READY_MUTED;
    audio->volume_percent = config->volume_percent;

    result = configure_i2s(audio);
    if (result == ESP_OK) {
        result = reprime_zero_dma_ring_amp_off(audio);
    }
    if (result != ESP_OK) {
        return rollback_failed_create(audio, result);
    }

    s_live_owner = audio;
    *out_audio = audio;
    return ESP_OK;
#endif
}

esp_err_t platform_audio_start(platform_audio_t *audio)
{
    if (audio == NULL || audio->tx_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state != PLATFORM_AUDIO_STATE_READY_MUTED) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = reprime_zero_dma_ring_amp_off(audio);
    if (result == ESP_OK) {
        result = set_amplifier_enabled(true);
    }
    if (result != ESP_OK) {
        (void)platform_audio_force_safe_shutdown();
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(AUDIO_AMP_SETTLE_MS));
    audio->state = PLATFORM_AUDIO_STATE_RUNNING;
    return ESP_OK;
}

esp_err_t platform_audio_write_frames(platform_audio_t *audio,
                                      const int16_t *interleaved_pcm,
                                      size_t frame_count)
{
    if (audio == NULL || interleaved_pcm == NULL || frame_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state != PLATFORM_AUDIO_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame_count > (size_t)PLATFORM_AUDIO_MAX_WRITE_FRAMES) {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t sample_count = frame_count * PLATFORM_AUDIO_CHANNEL_COUNT;
    if (!platform_audio_attenuate_pcm16(
            interleaved_pcm, audio->staging, sample_count,
            audio->volume_percent)) {
        (void)platform_audio_force_safe_shutdown();
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result = write_exact_staging(audio, frame_count);
    if (result != ESP_OK) {
        if (reprime_zero_dma_ring_amp_off(audio) == ESP_OK) {
            audio->state = PLATFORM_AUDIO_STATE_READY_MUTED;
        }
    }
    return result;
}

esp_err_t platform_audio_stop(platform_audio_t *audio)
{
    if (audio == NULL || audio->tx_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state != PLATFORM_AUDIO_STATE_READY_MUTED &&
        audio->state != PLATFORM_AUDIO_STATE_RUNNING) {
        (void)platform_audio_force_safe_shutdown();
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Disable the amplifier first, stop the channel, overwrite every DMA
     * descriptor with zero, then resume zero clocks. No stale PCM survives a
     * stop/restart or write-failure boundary.
     */
    const esp_err_t result = reprime_zero_dma_ring_amp_off(audio);
    if (result == ESP_OK) {
        audio->state = PLATFORM_AUDIO_STATE_READY_MUTED;
    }
    return result;
}

esp_err_t platform_audio_get_state(const platform_audio_t *audio,
                                   platform_audio_state_t *out_state)
{
    if (audio == NULL || out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_state = audio->state;
    return ESP_OK;
}

esp_err_t platform_audio_destroy(platform_audio_t **audio)
{
    if (audio == NULL || *audio == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    platform_audio_t *instance = *audio;
    if (instance != s_live_owner) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Retain the handle and clocks if amplifier shutdown is uncertain. */
    esp_err_t result = platform_audio_force_safe_shutdown();
    if (result != ESP_OK) {
        return result;
    }
    instance->state = PLATFORM_AUDIO_STATE_READY_MUTED;

    result = release_i2s(instance);
    if (result != ESP_OK) {
        return result;
    }
    s_live_owner = NULL;
    free(instance);
    *audio = NULL;
    return ESP_OK;
}
