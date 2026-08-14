#include "platform_audio_factory/audio.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef PLATFORM_AUDIO_FACTORY_HOST_TEST
#include "platform_audio_factory_test_hal.h"
#else
#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#endif

#include "platform_audio_factory_policy.h"

#ifndef CONFIG_PLATFORM_AUDIO_FACTORY_ELECROW_10_1_BUILD_ONLY
#define CONFIG_PLATFORM_AUDIO_FACTORY_ELECROW_10_1_BUILD_ONLY 0
#endif

_Static_assert(
    PLATFORM_AUDIO_FACTORY_ZERO_PREROLL_FRAMES ==
        PLATFORM_AUDIO_FACTORY_DMA_DESCRIPTOR_COUNT *
            PLATFORM_AUDIO_FACTORY_DMA_FRAMES_PER_DESCRIPTOR,
    "zero preroll must cover the complete DMA ring"
);
_Static_assert(
    PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES <=
        PLATFORM_AUDIO_FACTORY_DMA_FRAMES_PER_DESCRIPTOR,
    "one public write must fit one DMA frame block"
);
_Static_assert(
    PLATFORM_AUDIO_FACTORY_WRITE_TIMEOUT_MS < 250,
    "audio write must remain within the caller's stop budget"
);
_Static_assert(
    PLATFORM_AUDIO_FACTORY_MAX_OUTPUT_ABS_MAGNITUDE ==
        PLATFORM_AUDIO_FACTORY_POLICY_MAX_OUTPUT_ABS_MAGNITUDE,
    "public factory-audio peak and attenuation policy differ"
);
_Static_assert(
    PLATFORM_AUDIO_FACTORY_PDM_CLOCK_HZ ==
        PLATFORM_AUDIO_FACTORY_SAMPLE_RATE_HZ *
            PLATFORM_AUDIO_FACTORY_PDM_DOWNSAMPLE_FACTOR * UINT32_C(8),
    "pinned DSR_8S PDM clock must be 1.024 MHz"
);

struct platform_audio_factory {
    i2s_chan_handle_t pdm_rx_channel;
    i2s_chan_handle_t tx_channel;
    platform_audio_factory_state_t state;
    uint8_t volume_percent;
    bool pdm_rx_enabled;
    bool tx_enabled;
    int16_t staging[
        PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES *
        PLATFORM_AUDIO_FACTORY_CHANNEL_COUNT
    ];
};

static platform_audio_factory_t *s_failed_create_owner;
static platform_audio_factory_t *s_live_owner;

/* A scheduler-aware critical section makes each snapshot coherent. */
static platform_audio_factory_telemetry_t s_telemetry;
static uint32_t s_telemetry_sequence;
#ifdef PLATFORM_AUDIO_FACTORY_HOST_TEST
static pthread_mutex_t s_telemetry_lock = PTHREAD_MUTEX_INITIALIZER;
#else
static portMUX_TYPE s_telemetry_lock = portMUX_INITIALIZER_UNLOCKED;
#endif

static void telemetry_write_begin(void)
{
#ifdef PLATFORM_AUDIO_FACTORY_HOST_TEST
    (void)pthread_mutex_lock(&s_telemetry_lock);
#else
    portENTER_CRITICAL(&s_telemetry_lock);
#endif
}

static void telemetry_write_end(void)
{
    ++s_telemetry_sequence;
#ifdef PLATFORM_AUDIO_FACTORY_HOST_TEST
    (void)pthread_mutex_unlock(&s_telemetry_lock);
#else
    portEXIT_CRITICAL(&s_telemetry_lock);
#endif
}

static void telemetry_publish_resources(
    const platform_audio_factory_t *audio,
    bool retained)
{
    s_telemetry.pdm_created =
        audio != NULL && audio->pdm_rx_channel != NULL;
    s_telemetry.pdm_enabled =
        audio != NULL && audio->pdm_rx_enabled;
    s_telemetry.tx_created =
        audio != NULL && audio->tx_channel != NULL;
    s_telemetry.tx_enabled = audio != NULL && audio->tx_enabled;
    s_telemetry.resources_owned =
        (s_telemetry.pdm_created ? UINT32_C(1) : UINT32_C(0)) +
        (s_telemetry.tx_created ? UINT32_C(1) : UINT32_C(0));
    s_telemetry.resources_retained =
        retained && s_telemetry.resources_owned != UINT32_C(0);
}

static void telemetry_publish_state(
    const platform_audio_factory_t *audio,
    bool retained)
{
    telemetry_write_begin();
    telemetry_publish_resources(audio, retained);
    s_telemetry.state = audio != NULL
                            ? audio->state
                            : PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED;
    s_telemetry.running =
        audio != NULL &&
        audio->state == PLATFORM_AUDIO_FACTORY_STATE_RUNNING;
    telemetry_write_end();
}

static uint32_t saturating_u32_from_u64(uint64_t value)
{
    return value > (uint64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static esp_err_t verify_amplifier_level(bool enabled)
{
    const gpio_num_t pin =
        (gpio_num_t)PLATFORM_AUDIO_FACTORY_GPIO_AMP_SHUTDOWN;
    const int expected =
        platform_audio_factory_amp_gpio_level(enabled) ? 1 : 0;
    return gpio_get_level(pin) == expected
               ? ESP_OK
               : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t request_amplifier_level(bool enabled, bool initial_low_read)
{
    const gpio_num_t pin =
        (gpio_num_t)PLATFORM_AUDIO_FACTORY_GPIO_AMP_SHUTDOWN;
    const uint32_t requested =
        platform_audio_factory_amp_gpio_level(enabled) ? UINT32_C(1)
                                                       : UINT32_C(0);
    telemetry_write_begin();
    if (enabled) {
        ++s_telemetry.gpio30_low_attempts;
    } else {
        ++s_telemetry.gpio30_high_attempts;
    }
    telemetry_write_end();

    const esp_err_t result = gpio_set_level(pin, requested);
    if (result != ESP_OK) {
        return result;
    }
    telemetry_write_begin();
    if (enabled) {
        ++s_telemetry.gpio30_low_successes;
    } else {
        ++s_telemetry.gpio30_high_successes;
    }
    telemetry_write_end();

    const esp_err_t read_result = verify_amplifier_level(enabled);
    if (read_result == ESP_OK) {
        telemetry_write_begin();
        if (enabled && initial_low_read) {
            ++s_telemetry.gpio30_low_initial_readback_successes;
        } else if (!enabled) {
            ++s_telemetry.gpio30_high_readback_successes;
        }
        telemetry_write_end();
    }
    return read_result;
}

esp_err_t platform_audio_factory_force_safe_shutdown(void)
{
    const gpio_num_t pin =
        (gpio_num_t)PLATFORM_AUDIO_FACTORY_GPIO_AMP_SHUTDOWN;

    /* Latch inactive-high before changing the pad direction. */
    telemetry_write_begin();
    ++s_telemetry.gpio30_high_attempts;
    telemetry_write_end();
    esp_err_t result = gpio_set_level(pin, UINT32_C(1));
    if (result != ESP_OK) {
        return result;
    }
    telemetry_write_begin();
    ++s_telemetry.gpio30_high_successes;
    telemetry_write_end();
    const gpio_config_t configuration = {
        .pin_bit_mask =
            UINT64_C(1) << PLATFORM_AUDIO_FACTORY_GPIO_AMP_SHUTDOWN,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&configuration);
    if (result != ESP_OK) {
        return result;
    }

    /* Rewrite after input enable, then prove the physical pad reads high. */
    return request_amplifier_level(false, false);
}

static esp_err_t configure_and_enable_pdm_rx(
    platform_audio_factory_t *audio)
{
    i2s_chan_config_t channel_configuration = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)PLATFORM_AUDIO_FACTORY_PDM_I2S_CONTROLLER,
        I2S_ROLE_MASTER
    );
    channel_configuration.dma_desc_num =
        PLATFORM_AUDIO_FACTORY_DMA_DESCRIPTOR_COUNT;
    channel_configuration.dma_frame_num =
        PLATFORM_AUDIO_FACTORY_DMA_FRAMES_PER_DESCRIPTOR;
    channel_configuration.auto_clear_after_cb = true;
    channel_configuration.auto_clear_before_cb = true;

    esp_err_t result = i2s_new_channel(
        &channel_configuration, NULL, &audio->pdm_rx_channel
    );
    if (result != ESP_OK) {
        return result;
    }
    telemetry_write_begin();
    ++s_telemetry.pdm_create_successes;
    telemetry_publish_resources(audio, false);
    telemetry_write_end();

    const i2s_pdm_rx_config_t pdm_configuration = {
        .clk_cfg = {
            .sample_rate_hz = PLATFORM_AUDIO_FACTORY_SAMPLE_RATE_HZ,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .dn_sample_mode = I2S_PDM_DSR_8S,
            .bclk_div = PLATFORM_AUDIO_FACTORY_PDM_BCLK_DIV,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_PDM_SLOT_LEFT,
            .data_fmt = I2S_PDM_DATA_FMT_PCM,
            .hp_en = true,
            .hp_cut_off_freq_hz = 35.5F,
            .amplify_num = UINT32_C(1),
        },
        .gpio_cfg = {
            .clk = (gpio_num_t)PLATFORM_AUDIO_FACTORY_PDM_GPIO_CLK,
            .din = (gpio_num_t)PLATFORM_AUDIO_FACTORY_PDM_GPIO_DIN,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    result = i2s_channel_init_pdm_rx_mode(
        audio->pdm_rx_channel, &pdm_configuration
    );
    if (result != ESP_OK) {
        return result;
    }
    result = i2s_channel_enable(audio->pdm_rx_channel);
    if (result == ESP_OK) {
        audio->pdm_rx_enabled = true;
        telemetry_write_begin();
        ++s_telemetry.pdm_enable_successes;
        telemetry_publish_resources(audio, false);
        telemetry_write_end();
    }
    return result;
}

static esp_err_t configure_tx(platform_audio_factory_t *audio)
{
    i2s_chan_config_t channel_configuration = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)PLATFORM_AUDIO_FACTORY_TX_I2S_CONTROLLER,
        I2S_ROLE_MASTER
    );
    channel_configuration.dma_desc_num =
        PLATFORM_AUDIO_FACTORY_DMA_DESCRIPTOR_COUNT;
    channel_configuration.dma_frame_num =
        PLATFORM_AUDIO_FACTORY_DMA_FRAMES_PER_DESCRIPTOR;
    channel_configuration.auto_clear = true;

    esp_err_t result = i2s_new_channel(
        &channel_configuration, &audio->tx_channel, NULL
    );
    if (result != ESP_OK) {
        return result;
    }
    telemetry_write_begin();
    ++s_telemetry.tx_create_successes;
    telemetry_publish_resources(audio, false);
    telemetry_write_end();

    const i2s_std_config_t standard_configuration = {
        .clk_cfg = {
            .sample_rate_hz = PLATFORM_AUDIO_FACTORY_SAMPLE_RATE_HZ,
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
            .bclk = (gpio_num_t)PLATFORM_AUDIO_FACTORY_GPIO_BCLK,
            .ws = (gpio_num_t)PLATFORM_AUDIO_FACTORY_GPIO_LRCLK,
            .dout = (gpio_num_t)PLATFORM_AUDIO_FACTORY_GPIO_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    return i2s_channel_init_std_mode(
        audio->tx_channel, &standard_configuration
    );
}

static esp_err_t write_exact_staging(platform_audio_factory_t *audio,
                                     size_t frame_count)
{
    if (audio == NULL || audio->tx_channel == NULL || !audio->tx_enabled ||
        frame_count == 0U ||
        frame_count > (size_t)PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t byte_count =
        frame_count * PLATFORM_AUDIO_FACTORY_CHANNEL_COUNT *
        sizeof(audio->staging[0]);
    size_t bytes_written = 0U;
    const esp_err_t result = i2s_channel_write(
        audio->tx_channel, audio->staging, byte_count, &bytes_written,
        PLATFORM_AUDIO_FACTORY_WRITE_TIMEOUT_MS
    );
    if (result != ESP_OK) {
        return result;
    }
    return bytes_written == byte_count ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t reset_channel_to_ready(platform_audio_factory_t *audio)
{
    if (audio == NULL || audio->tx_channel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Enable-then-disable also resets an initially READY channel's internal
     * queue. A running channel needs only the disable half.
     */
    if (!audio->tx_enabled) {
        const esp_err_t enable_result =
            i2s_channel_enable(audio->tx_channel);
        if (enable_result != ESP_OK) {
            return enable_result;
        }
        audio->tx_enabled = true;
    }
    const esp_err_t disable_result =
        i2s_channel_disable(audio->tx_channel);
    if (disable_result == ESP_OK) {
        audio->tx_enabled = false;
    }
    return disable_result;
}

static esp_err_t preload_exact_zero_dma_ring(platform_audio_factory_t *audio)
{
    memset(audio->staging, 0, sizeof(audio->staging));
    size_t frames_remaining =
        PLATFORM_AUDIO_FACTORY_ZERO_PREROLL_FRAMES;
    while (frames_remaining > 0U) {
        const size_t chunk_frames =
            frames_remaining >
                    (size_t)PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES
                ? (size_t)PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES
                : frames_remaining;
        const size_t requested_bytes =
            chunk_frames * PLATFORM_AUDIO_FACTORY_CHANNEL_COUNT *
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
    telemetry_write_begin();
    s_telemetry.zero_preload_frames =
        PLATFORM_AUDIO_FACTORY_ZERO_PREROLL_FRAMES;
    telemetry_write_end();
    return ESP_OK;
}

static esp_err_t rollback_to_muted(platform_audio_factory_t *audio)
{
    if (audio == NULL || audio->tx_channel == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    audio->state = PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE;
    telemetry_write_begin();
    ++s_telemetry.rollback_attempts;
    telemetry_publish_resources(audio, false);
    s_telemetry.state = PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE;
    s_telemetry.running = false;
    telemetry_write_end();

    esp_err_t result = platform_audio_factory_force_safe_shutdown();
    if (result == ESP_OK) {
        telemetry_write_begin();
        ++s_telemetry.rollback_high_proofs;
        telemetry_write_end();
    }
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
            telemetry_write_begin();
            ++s_telemetry.tx_enable_successes;
            telemetry_publish_resources(audio, false);
            telemetry_write_end();
        }
    }
    if (result == ESP_OK) {
        audio->state = PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED;
        telemetry_write_begin();
        ++s_telemetry.rollback_successes;
        telemetry_publish_resources(audio, false);
        s_telemetry.state = PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED;
        s_telemetry.running = false;
        telemetry_write_end();
    } else {
        telemetry_publish_state(audio, true);
    }
    return result;
}

/* GPIO30 must already have been proven high before calling this function. */
static esp_err_t release_tx(platform_audio_factory_t *audio)
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

/* GPIO30 must already have been proven high before calling this function. */
static esp_err_t release_pdm_rx(platform_audio_factory_t *audio)
{
    if (audio == NULL || audio->pdm_rx_channel == NULL) {
        return ESP_OK;
    }
    if (audio->pdm_rx_enabled) {
        const esp_err_t disable_result =
            i2s_channel_disable(audio->pdm_rx_channel);
        if (disable_result != ESP_OK &&
            disable_result != ESP_ERR_INVALID_STATE) {
            return disable_result;
        }
        audio->pdm_rx_enabled = false;
    }
    const esp_err_t delete_result =
        i2s_del_channel(audio->pdm_rx_channel);
    if (delete_result == ESP_OK) {
        audio->pdm_rx_channel = NULL;
    }
    return delete_result;
}

static esp_err_t release_all_i2s(platform_audio_factory_t *audio)
{
    esp_err_t result = release_tx(audio);
    if (result == ESP_OK) {
        result = release_pdm_rx(audio);
    }
    telemetry_publish_state(audio, result != ESP_OK);
    return result;
}

static esp_err_t recover_failed_create_after_shutdown(void)
{
    if (s_failed_create_owner == NULL) {
        return ESP_OK;
    }
    const esp_err_t result = release_all_i2s(s_failed_create_owner);
    if (result == ESP_OK) {
        free(s_failed_create_owner);
        s_failed_create_owner = NULL;
        telemetry_publish_state(NULL, false);
    }
    return result;
}

esp_err_t platform_audio_factory_recover(void)
{
    if (s_live_owner != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t shutdown_result =
        platform_audio_factory_force_safe_shutdown();
    if (shutdown_result != ESP_OK) {
        return shutdown_result;
    }
    const esp_err_t result = recover_failed_create_after_shutdown();
    if (result == ESP_OK) {
        telemetry_publish_state(NULL, false);
    }
    return result;
}

static esp_err_t rollback_failed_create(platform_audio_factory_t *audio,
                                        esp_err_t primary_result)
{
    if (audio == NULL) {
        return primary_result;
    }
    esp_err_t cleanup_result =
        platform_audio_factory_force_safe_shutdown();
    if (cleanup_result == ESP_OK) {
        cleanup_result = release_all_i2s(audio);
    }
    if (cleanup_result == ESP_OK) {
        free(audio);
    } else {
        s_failed_create_owner = audio;
        telemetry_publish_state(audio, true);
    }
    return primary_result != ESP_OK ? primary_result : cleanup_result;
}

esp_err_t platform_audio_factory_create(
    const platform_audio_factory_config_t *config,
    platform_audio_factory_t **out_audio)
{
#if !CONFIG_PLATFORM_AUDIO_FACTORY_ELECROW_10_1_BUILD_ONLY
    (void)config;
    (void)out_audio;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (config == NULL || out_audio == NULL || *out_audio != NULL ||
        !platform_audio_factory_sample_rate_supported(
            config->sample_rate_hz) ||
        !platform_audio_factory_volume_supported(config->volume_percent)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_live_owner != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = platform_audio_factory_recover();
    if (result != ESP_OK) {
        return result;
    }

    platform_audio_factory_t *audio = calloc(1U, sizeof(*audio));
    if (audio == NULL) {
        return ESP_ERR_NO_MEM;
    }
    audio->state = PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE;
    audio->volume_percent = config->volume_percent;

    result = configure_and_enable_pdm_rx(audio);
    if (result == ESP_OK) {
        result = configure_tx(audio);
    }
    if (result == ESP_OK) {
        result = rollback_to_muted(audio);
    }
    if (result != ESP_OK) {
        return rollback_failed_create(audio, result);
    }

    s_live_owner = audio;
    *out_audio = audio;
    return ESP_OK;
#endif
}

esp_err_t platform_audio_factory_start(platform_audio_factory_t *audio)
{
    if (audio == NULL || audio != s_live_owner ||
        audio->tx_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state != PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED) {
        return ESP_ERR_INVALID_STATE;
    }

    telemetry_write_begin();
    s_telemetry.zero_preload_frames = UINT32_C(0);
    s_telemetry.gpio30_low_attempts = UINT32_C(0);
    s_telemetry.gpio30_low_successes = UINT32_C(0);
    s_telemetry.gpio30_low_initial_readback_successes = UINT32_C(0);
    s_telemetry.measured_settle_us = UINT32_C(0);
    s_telemetry.gpio30_low_second_readback_successes = UINT32_C(0);
    telemetry_write_end();

    /* Re-prime after resetting per-start telemetry. */
    esp_err_t result = rollback_to_muted(audio);
    if (result != ESP_OK) {
        return result;
    }

    result = request_amplifier_level(true, true);
    if (result != ESP_OK) {
        const esp_err_t primary_result = result;
        (void)rollback_to_muted(audio);
        return primary_result;
    }

    /*
     * The six-descriptor ring contains exact zeros and auto-clear is enabled.
     * One extra RTOS tick makes the delay no shorter than the 350 ms bound
     * even when pdMS_TO_TICKS truncates.
     */
    const int64_t settle_begin_us = esp_timer_get_time();
    vTaskDelay(
        pdMS_TO_TICKS(PLATFORM_AUDIO_FACTORY_STARTUP_ZERO_MS) +
        (TickType_t)1U
    );
    const int64_t settle_end_us = esp_timer_get_time();
    const uint64_t settle_elapsed_us =
        settle_end_us >= settle_begin_us
            ? (uint64_t)(settle_end_us - settle_begin_us)
            : UINT64_C(0);
    telemetry_write_begin();
    s_telemetry.measured_settle_us =
        saturating_u32_from_u64(settle_elapsed_us);
    telemetry_write_end();
    if (settle_elapsed_us <
        (uint64_t)PLATFORM_AUDIO_FACTORY_STARTUP_ZERO_MS * UINT64_C(1000)) {
        (void)rollback_to_muted(audio);
        return ESP_ERR_TIMEOUT;
    }
    result = verify_amplifier_level(true);
    if (result != ESP_OK) {
        const esp_err_t primary_result = result;
        (void)rollback_to_muted(audio);
        return primary_result;
    }
    audio->state = PLATFORM_AUDIO_FACTORY_STATE_RUNNING;
    telemetry_write_begin();
    ++s_telemetry.gpio30_low_second_readback_successes;
    telemetry_publish_resources(audio, false);
    s_telemetry.state = PLATFORM_AUDIO_FACTORY_STATE_RUNNING;
    s_telemetry.running = true;
    telemetry_write_end();
    return ESP_OK;
}

esp_err_t platform_audio_factory_write_frames(
    platform_audio_factory_t *audio,
    const int16_t *interleaved_pcm,
    size_t frame_count)
{
    if (audio == NULL || audio != s_live_owner ||
        interleaved_pcm == NULL || frame_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state != PLATFORM_AUDIO_FACTORY_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame_count >
        (size_t)PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES) {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t sample_count =
        frame_count * PLATFORM_AUDIO_FACTORY_CHANNEL_COUNT;
    if (!platform_audio_factory_attenuate_pcm16(
            interleaved_pcm, audio->staging, sample_count,
            audio->volume_percent)) {
        audio->state = PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE;
        (void)rollback_to_muted(audio);
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t result = write_exact_staging(audio, frame_count);
    if (result != ESP_OK) {
        telemetry_write_begin();
        ++s_telemetry.write_failures;
        telemetry_write_end();
        (void)rollback_to_muted(audio);
        return result;
    }

    uint32_t nonzero_samples = UINT32_C(0);
    uint32_t nonzero_frames = UINT32_C(0);
    uint32_t maximum_absolute_magnitude = UINT32_C(0);
    for (size_t frame = 0U; frame < frame_count; ++frame) {
        bool frame_is_nonzero = false;
        for (size_t channel = 0U;
             channel < (size_t)PLATFORM_AUDIO_FACTORY_CHANNEL_COUNT;
             ++channel) {
            const int32_t sample =
                audio->staging[
                    frame * PLATFORM_AUDIO_FACTORY_CHANNEL_COUNT + channel
                ];
            const uint32_t magnitude =
                sample < 0 ? (uint32_t)(-sample) : (uint32_t)sample;
            if (magnitude != UINT32_C(0)) {
                ++nonzero_samples;
                frame_is_nonzero = true;
            }
            if (magnitude > maximum_absolute_magnitude) {
                maximum_absolute_magnitude = magnitude;
            }
        }
        if (frame_is_nonzero) {
            ++nonzero_frames;
        }
    }
    telemetry_write_begin();
    ++s_telemetry.write_successes;
    s_telemetry.frames_written = saturating_u32_from_u64(
        (uint64_t)s_telemetry.frames_written + (uint64_t)frame_count
    );
    s_telemetry.samples_written = saturating_u32_from_u64(
        (uint64_t)s_telemetry.samples_written + (uint64_t)sample_count
    );
    s_telemetry.nonzero_frames = saturating_u32_from_u64(
        (uint64_t)s_telemetry.nonzero_frames + (uint64_t)nonzero_frames
    );
    s_telemetry.nonzero_samples = saturating_u32_from_u64(
        (uint64_t)s_telemetry.nonzero_samples +
        (uint64_t)nonzero_samples
    );
    if (maximum_absolute_magnitude >
        s_telemetry.maximum_absolute_magnitude) {
        s_telemetry.maximum_absolute_magnitude =
            maximum_absolute_magnitude;
    }
    telemetry_write_end();
    return ESP_OK;
}

esp_err_t platform_audio_factory_stop(platform_audio_factory_t *audio)
{
    if (audio == NULL || audio != s_live_owner ||
        audio->tx_channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return rollback_to_muted(audio);
}

esp_err_t platform_audio_factory_get_state(
    const platform_audio_factory_t *audio,
    platform_audio_factory_state_t *out_state)
{
    if (audio == NULL || audio != s_live_owner || out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_state = audio->state;
    return ESP_OK;
}

esp_err_t platform_audio_factory_get_telemetry(
    platform_audio_factory_telemetry_t *out_telemetry)
{
    if (out_telemetry == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    telemetry_write_begin();
    memcpy(out_telemetry, &s_telemetry, sizeof(*out_telemetry));
    out_telemetry->snapshot_sequence = s_telemetry_sequence;
    telemetry_write_end();
    return ESP_OK;
}

esp_err_t platform_audio_factory_destroy(platform_audio_factory_t **audio)
{
    if (audio == NULL || *audio == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    platform_audio_factory_t *instance = *audio;
    if (instance != s_live_owner) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result;
    if (instance->tx_channel != NULL) {
        result = rollback_to_muted(instance);
        if (result != ESP_OK) {
            return result;
        }
    } else {
        /* A prior destroy may have released TX before PDM cleanup failed. */
        instance->state = PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE;
        result = platform_audio_factory_force_safe_shutdown();
        if (result != ESP_OK) {
            telemetry_publish_state(instance, true);
            return result;
        }
        telemetry_write_begin();
        ++s_telemetry.rollback_high_proofs;
        telemetry_write_end();
    }
    result = release_all_i2s(instance);
    if (result != ESP_OK) {
        instance->state = PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE;
        telemetry_publish_state(instance, true);
        return result;
    }

    s_live_owner = NULL;
    free(instance);
    *audio = NULL;
    telemetry_publish_state(NULL, false);
    return ESP_OK;
}
