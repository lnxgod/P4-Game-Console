// SPDX-License-Identifier: MIT

#include "platform/audio.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "audio_codec_ctrl_if.h"
#include "audio_codec_data_if.h"
#include "audio_codec_gpio_if.h"
#include "audio_codec_if.h"
#include "platform/tab5.h"
#include "platform/board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8388_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

enum {
    TAB5_AUDIO_I2C_PORT = 1,
    TAB5_AUDIO_I2S_PORT = 1,
    TAB5_AUDIO_MCLK_GPIO = 30,
    TAB5_AUDIO_BCLK_GPIO = 27,
    TAB5_AUDIO_LRCLK_GPIO = 29,
    TAB5_AUDIO_DOUT_GPIO = 26,
    TAB5_AUDIO_AMP_GPIO = -1,
    TAB5_AUDIO_DMA_DESCRIPTORS = 6,
    TAB5_AUDIO_DMA_FRAMES = 128,
};

struct platform_audio {
    i2s_chan_handle_t tx;
    const audio_codec_data_if_t *data_if;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t *codec_if;
    esp_codec_dev_handle_t codec;
    platform_audio_state_t state;
    uint8_t volume_step;
    bool codec_open;
    int16_t staging[
        PLATFORM_AUDIO_MAX_WRITE_FRAMES * PLATFORM_AUDIO_CHANNEL_COUNT];
};

static platform_audio_t *s_live_owner;
static platform_audio_t *s_recovery_owner;
static uint32_t s_invocations;
static atomic_flag s_api_active = ATOMIC_FLAG_INIT;
static portMUX_TYPE s_telemetry_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_audio_telemetry_t s_telemetry;

static bool try_api(void)
{
    return !atomic_flag_test_and_set_explicit(
        &s_api_active, memory_order_acquire);
}

static void finish_api(void)
{
    __atomic_add_fetch(&s_invocations,1U,__ATOMIC_RELEASE);
    atomic_flag_clear_explicit(&s_api_active, memory_order_release);
}

static void telemetry_publish(const platform_audio_t *audio)
{
    portENTER_CRITICAL(&s_telemetry_lock);
    ++s_telemetry.snapshot_sequence;
    if (audio != NULL) {
        s_telemetry.state = audio->state;
        s_telemetry.running = audio->state == PLATFORM_AUDIO_STATE_RUNNING;
        s_telemetry.codec_open = audio->codec_open;
        s_telemetry.i2s_created = audio->tx != NULL;
        s_telemetry.i2s_enabled = audio->codec_open;
        s_telemetry.tx_created = audio->tx != NULL;
        s_telemetry.tx_enabled = audio->codec_open;
        s_telemetry.resources_owned =
            (audio->tx != NULL ? 1U : 0U) +
            (audio->codec != NULL ? 1U : 0U) +
            (audio->codec_if != NULL ? 1U : 0U) +
            (audio->ctrl_if != NULL ? 1U : 0U) +
            (audio->gpio_if != NULL ? 1U : 0U) +
            (audio->data_if != NULL ? 1U : 0U);
        s_telemetry.resources_retained =
            audio == s_recovery_owner ||
            audio->state == PLATFORM_AUDIO_STATE_FAILED_SAFE;
    } else {
        s_telemetry.state = PLATFORM_AUDIO_STATE_READY_MUTED;
        s_telemetry.running = false;
        s_telemetry.codec_open = false;
        s_telemetry.i2s_created = false;
        s_telemetry.i2s_enabled = false;
        s_telemetry.tx_created = false;
        s_telemetry.tx_enabled = false;
        s_telemetry.resources_retained = false;
        s_telemetry.resources_owned = 0U;
    }
    portEXIT_CRITICAL(&s_telemetry_lock);
}

static esp_err_t amplifier_off(void)
{
    return platform_tab5_speaker_enable(false);
}

static uint32_t absolute_sample(int16_t sample)
{
    if (sample == INT16_MIN) {
        return UINT32_C(32768);
    }
    return (uint32_t)(sample < 0 ? -sample : sample);
}

static esp_err_t release_resources(platform_audio_t *audio)
{
    if (audio == NULL) {
        return ESP_OK;
    }
    esp_err_t result = amplifier_off();
    if (audio->codec_open && audio->codec != NULL) {
        const int mute_result = esp_codec_dev_set_out_mute(
            audio->codec, true);
        if (mute_result != ESP_CODEC_DEV_OK && result == ESP_OK) {
            result = (esp_err_t)mute_result;
        }
        const int close_result = esp_codec_dev_close(audio->codec);
        if (close_result == ESP_CODEC_DEV_OK) {
            audio->codec_open = false;
        } else if (result == ESP_OK) {
            result = (esp_err_t)close_result;
        }
    }
    if (audio->codec_open || result != ESP_OK) {
        audio->state = PLATFORM_AUDIO_STATE_FAILED_SAFE;
        telemetry_publish(audio);
        return result == ESP_OK ? ESP_FAIL : result;
    }
    if (audio->codec != NULL) {
        esp_codec_dev_delete(audio->codec);
        audio->codec = NULL;
    }
    if (audio->codec_if != NULL) {
        audio_codec_delete_codec_if(audio->codec_if);
        audio->codec_if = NULL;
    }
    if (audio->ctrl_if != NULL) {
        audio_codec_delete_ctrl_if(audio->ctrl_if);
        audio->ctrl_if = NULL;
    }
    if (audio->gpio_if != NULL) {
        audio_codec_delete_gpio_if(audio->gpio_if);
        audio->gpio_if = NULL;
    }
    if (audio->data_if != NULL) {
        audio_codec_delete_data_if(audio->data_if);
        audio->data_if = NULL;
    }
    if (audio->tx != NULL) {
        const esp_err_t delete_result = i2s_del_channel(audio->tx);
        if (delete_result == ESP_OK) {
            audio->tx = NULL;
        } else if (result == ESP_OK) {
            result = delete_result;
        }
    }
    audio->state = audio->tx == NULL
        ? PLATFORM_AUDIO_STATE_READY_MUTED
        : PLATFORM_AUDIO_STATE_FAILED_SAFE;
    telemetry_publish(audio);
    return result;
}

esp_err_t platform_audio_force_safe_shutdown(void)
{
    return amplifier_off();
}

esp_err_t platform_audio_recover(void)
{
    if (!try_api()) {
        return ESP_ERR_TIMEOUT;
    }
    platform_audio_t *const recovery = s_recovery_owner;
    if (recovery == NULL) {
        finish_api();
        return amplifier_off();
    }
    const esp_err_t result = release_resources(recovery);
    if (result == ESP_OK && recovery->tx == NULL && !recovery->codec_open) {
        free(recovery);
        s_recovery_owner = NULL;
        telemetry_publish(NULL);
    }
    finish_api();
    return result;
}

static esp_err_t configure_i2s(platform_audio_t *audio)
{
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)TAB5_AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = TAB5_AUDIO_DMA_DESCRIPTORS;
    channel_config.dma_frame_num = TAB5_AUDIO_DMA_FRAMES;
    channel_config.auto_clear = true;
    esp_err_t result = i2s_new_channel(
        &channel_config, &audio->tx, NULL);
    if (result != ESP_OK) {
        return result;
    }
    const i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PLATFORM_AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)TAB5_AUDIO_MCLK_GPIO,
            .bclk = (gpio_num_t)TAB5_AUDIO_BCLK_GPIO,
            .ws = (gpio_num_t)TAB5_AUDIO_LRCLK_GPIO,
            .dout = (gpio_num_t)TAB5_AUDIO_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    return i2s_channel_init_std_mode(audio->tx, &standard_config);
}

static esp_err_t configure_codec(platform_audio_t *audio)
{
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t result = i2c_master_get_bus_handle(
        (i2c_port_num_t)TAB5_AUDIO_I2C_PORT, &bus);
    if (result != ESP_OK || bus == NULL) {
        return result == ESP_OK ? ESP_ERR_INVALID_STATE : result;
    }
    audio_codec_i2s_cfg_t i2s_config = {
        .port = TAB5_AUDIO_I2S_PORT,
        .rx_handle = NULL,
        .tx_handle = audio->tx,
        .clk_src = 0,
    };
    audio->data_if = audio_codec_new_i2s_data(&i2s_config);
    if (audio->data_if == NULL) {
        return ESP_ERR_NO_MEM;
    }
    audio_codec_i2c_cfg_t i2c_config = {
        .port = TAB5_AUDIO_I2C_PORT,
        .addr = ES8388_CODEC_DEFAULT_ADDR,
        .bus_handle = bus,
    };
    audio->ctrl_if = audio_codec_new_i2c_ctrl(&i2c_config);
    audio->gpio_if = audio_codec_new_gpio();
    if (audio->ctrl_if == NULL || audio->gpio_if == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0F,
        .codec_dac_voltage = 3.3F,
    };
    es8388_codec_cfg_t codec_config = {
        .ctrl_if = audio->ctrl_if,
        .gpio_if = audio->gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = TAB5_AUDIO_AMP_GPIO,
        .pa_reverted = false,
        .master_mode = false,
        .hw_gain = gain,
    };
    audio->codec_if = es8388_codec_new(&codec_config);
    if (audio->codec_if == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = audio->codec_if,
        .data_if = audio->data_if,
    };
    audio->codec = esp_codec_dev_new(&device_config);
    return audio->codec != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t platform_audio_create(const platform_audio_config_t *config,
                                platform_audio_t **out_audio)
{
    if (config == NULL || out_audio == NULL || *out_audio != NULL ||
        (config->control_bus != NULL && config->control_bus != platform_tab5_i2c()) ||
        config->sample_rate_hz != PLATFORM_AUDIO_SAMPLE_RATE_HZ ||
        config->volume_percent > PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!try_api()) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_live_owner != NULL || s_recovery_owner != NULL) {
        finish_api();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = amplifier_off();
    platform_audio_t *audio = NULL;
    if (result == ESP_OK) {
        audio = calloc(1U, sizeof(*audio));
        result = audio != NULL ? ESP_OK : ESP_ERR_NO_MEM;
    }
    if (result == ESP_OK) {
        audio->state = PLATFORM_AUDIO_STATE_READY_MUTED;
        audio->volume_step = config->volume_percent;
        result = configure_i2s(audio);
    }
    if (result == ESP_OK) {
        result = configure_codec(audio);
    }
    if (result != ESP_OK && audio != NULL) {
        const esp_err_t cleanup = release_resources(audio);
        if (cleanup == ESP_OK && audio->tx == NULL && !audio->codec_open) {
            free(audio);
        } else {
            audio->state = PLATFORM_AUDIO_STATE_FAILED_SAFE;
            s_recovery_owner = audio;
            telemetry_publish(audio);
        }
        finish_api();
        return cleanup == ESP_OK ? result : cleanup;
    }
    if (result != ESP_OK) { finish_api(); return result; }
    s_live_owner = audio;
    *out_audio = audio;
    telemetry_publish(audio);
    finish_api();
    return ESP_OK;
}

esp_err_t platform_audio_start(platform_audio_t *audio)
{
    if (audio == NULL || audio != s_live_owner) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!try_api()) {
        return ESP_ERR_TIMEOUT;
    }
    if (audio->state != PLATFORM_AUDIO_STATE_READY_MUTED ||
        audio->codec == NULL) {
        finish_api();
        return ESP_ERR_INVALID_STATE;
    }
    int codec_result = amplifier_off();
    const esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = PLATFORM_AUDIO_BITS_PER_SAMPLE,
        .channel = PLATFORM_AUDIO_CHANNEL_COUNT,
        .channel_mask = 0U,
        .sample_rate = PLATFORM_AUDIO_SAMPLE_RATE_HZ,
        .mclk_multiple = 0,
    };
    if (codec_result == ESP_CODEC_DEV_OK) {
        codec_result = esp_codec_dev_open(audio->codec,
                                          (esp_codec_dev_sample_info_t *)&sample_info);
        audio->codec_open = codec_result == ESP_CODEC_DEV_OK;
    }
    if (codec_result == ESP_CODEC_DEV_OK) codec_result = esp_codec_dev_set_out_mute(audio->codec, true);
    if (codec_result == ESP_CODEC_DEV_OK) codec_result = esp_codec_dev_set_out_vol(audio->codec, 60);
    memset(audio->staging, 0, sizeof(audio->staging));
    for (unsigned i=0;i<TAB5_AUDIO_DMA_DESCRIPTORS && codec_result==ESP_CODEC_DEV_OK;++i) {
        codec_result=esp_codec_dev_write(audio->codec,audio->staging,(int)sizeof(audio->staging));
    }
    if (codec_result == ESP_CODEC_DEV_OK) {
        codec_result = esp_codec_dev_set_out_mute(audio->codec, false);
    }
    if (codec_result == ESP_CODEC_DEV_OK) codec_result = platform_tab5_speaker_enable(true);
    if (codec_result != ESP_CODEC_DEV_OK) {
        (void)release_resources(audio);
        audio->state = PLATFORM_AUDIO_STATE_FAILED_SAFE;
        telemetry_publish(audio);
        finish_api();
        return (esp_err_t)codec_result;
    }
    audio->state = PLATFORM_AUDIO_STATE_RUNNING;
    telemetry_publish(audio);
    finish_api();
    return ESP_OK;
}

esp_err_t platform_audio_write_frames(platform_audio_t *audio,
                                      const int16_t *interleaved_pcm,
                                      size_t frame_count)
{
    if (audio == NULL || audio != s_live_owner || interleaved_pcm == NULL ||
        frame_count == 0U ||
        frame_count > (size_t)PLATFORM_AUDIO_MAX_WRITE_FRAMES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!try_api()) {
        return ESP_ERR_TIMEOUT;
    }
    if (audio->state != PLATFORM_AUDIO_STATE_RUNNING ||
        !audio->codec_open) {
        finish_api();
        return ESP_ERR_INVALID_STATE;
    }
    const size_t sample_count =
        frame_count * (size_t)PLATFORM_AUDIO_CHANNEL_COUNT;
    for (size_t i=0;i<sample_count;++i) audio->staging[i]=(int16_t)((int32_t)interleaved_pcm[i]*audio->volume_step/10);
    uint32_t nonzero_frames = 0U;
    uint32_t peak = 0U;
    for (size_t frame = 0U; frame < frame_count; ++frame) {
        bool nonzero = false;
        for (size_t channel = 0U;
             channel < (size_t)PLATFORM_AUDIO_CHANNEL_COUNT; ++channel) {
            const int16_t sample = audio->staging[
                frame * PLATFORM_AUDIO_CHANNEL_COUNT + channel];
            nonzero = nonzero || sample != 0;
            const uint32_t magnitude = absolute_sample(sample);
            peak = magnitude > peak ? magnitude : peak;
        }
        nonzero_frames += nonzero ? 1U : 0U;
    }
    const int byte_count = (int)(sample_count * sizeof(audio->staging[0]));
    const int codec_result = esp_codec_dev_write(
        audio->codec, audio->staging, byte_count);
    portENTER_CRITICAL(&s_telemetry_lock);
    ++s_telemetry.snapshot_sequence;
    if (codec_result == ESP_CODEC_DEV_OK) {
        ++s_telemetry.write_successes;
        s_telemetry.frames_written += (uint32_t)frame_count;
        s_telemetry.nonzero_frames += nonzero_frames;
        if (peak > s_telemetry.maximum_absolute_magnitude) {
            s_telemetry.maximum_absolute_magnitude = peak;
        }
    } else {
        ++s_telemetry.write_failures;
    }
    portEXIT_CRITICAL(&s_telemetry_lock);
    finish_api();
    return (esp_err_t)codec_result;
}

esp_err_t platform_audio_stop(platform_audio_t *audio)
{
    if (audio == NULL || audio != s_live_owner) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!try_api()) {
        return ESP_ERR_TIMEOUT;
    }
    if (audio->state == PLATFORM_AUDIO_STATE_READY_MUTED) {
        finish_api();
        return ESP_OK;
    }
    if (audio->state != PLATFORM_AUDIO_STATE_RUNNING) {
        finish_api();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = amplifier_off();
    const int mute_result = esp_codec_dev_set_out_mute(audio->codec, true);
    if (mute_result != ESP_CODEC_DEV_OK && result == ESP_OK) {
        result = (esp_err_t)mute_result;
    }
    const int close_result = esp_codec_dev_close(audio->codec);
    if (close_result == ESP_CODEC_DEV_OK) {
        audio->codec_open = false;
    } else if (result == ESP_OK) {
        result = (esp_err_t)close_result;
    }
    audio->state = result == ESP_OK
        ? PLATFORM_AUDIO_STATE_READY_MUTED
        : PLATFORM_AUDIO_STATE_FAILED_SAFE;
    telemetry_publish(audio);
    finish_api();
    return result;
}

esp_err_t platform_audio_get_state(const platform_audio_t *audio,
                                   platform_audio_state_t *out_state)
{
    if (audio == NULL || audio != s_live_owner || out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_state = audio->state;
    return ESP_OK;
}

esp_err_t platform_audio_get_telemetry(platform_audio_telemetry_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_telemetry_lock);
    *out = s_telemetry;
    portEXIT_CRITICAL(&s_telemetry_lock);
    return ESP_OK;
}

esp_err_t platform_audio_destroy(platform_audio_t **audio)
{
    if (audio == NULL || *audio == NULL || *audio != s_live_owner) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!try_api()) {
        return ESP_ERR_TIMEOUT;
    }
    platform_audio_t *const owner = *audio;
    const esp_err_t result = release_resources(owner);
    if (result == ESP_OK && owner->tx == NULL && !owner->codec_open) {
        s_live_owner = NULL;
        *audio = NULL;
        free(owner);
        telemetry_publish(NULL);
    }
    finish_api();
    return result;
}

esp_err_t platform_audio_set_volume(platform_audio_t *audio,uint8_t step)
{
    if (!audio || audio!=s_live_owner || step>10) return ESP_ERR_INVALID_ARG;
    if (!try_api()) return ESP_ERR_TIMEOUT;
    audio->volume_step=step;
    finish_api(); return ESP_OK;
}
esp_err_t platform_audio_get_volume(const platform_audio_t *audio,uint8_t *out)
{
    if (!audio || audio!=s_live_owner || !out) return ESP_ERR_INVALID_ARG;
    if (!try_api()) return ESP_ERR_TIMEOUT;
    *out=audio->volume_step;
    finish_api(); return ESP_OK;
}
uint32_t platform_audio_invocation_count(void)
{ return __atomic_load_n(&s_invocations,__ATOMIC_ACQUIRE); }
void platform_audio_adapter_get_stats(platform_audio_adapter_stats_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_telemetry_lock);
    *out=(platform_audio_adapter_stats_t){
        .invocations=platform_audio_invocation_count(),
        .write_calls_succeeded=s_telemetry.write_successes,
        .frames_forwarded=s_telemetry.frames_written,
        .nonzero_frames_forwarded=s_telemetry.nonzero_frames,
        .observed_absolute_peak=(uint16_t)s_telemetry.maximum_absolute_magnitude,
    };
    /* Factory GPIO30/zero-DMA witnesses intentionally never asserted. */
    portEXIT_CRITICAL(&s_telemetry_lock);
}
