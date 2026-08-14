#include "platform/audio.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "audio_codec_ctrl_if.h"
#include "audio_codec_data_if.h"
#include "audio_codec_if.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_types.h"
#include "es8311_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform_audio_policy.h"
#include "sdkconfig.h"

#if CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE
#error "platform_audio requires the pinned ESP-IDF 5.3+ I2C master API"
#endif

#if !CONFIG_CODEC_ES8311_SUPPORT
#error "platform_audio requires CONFIG_CODEC_ES8311_SUPPORT"
#endif

_Static_assert(
    ES8311_CODEC_DEFAULT_ADDR == PLATFORM_AUDIO_ES8311_WIRE_ADDRESS,
    "pinned esp_codec_dev ES8311 address changed"
);
_Static_assert(
    PLATFORM_AUDIO_ES8311_7BIT_ADDRESS ==
        (PLATFORM_AUDIO_ES8311_WIRE_ADDRESS >> 1),
    "ES8311 7-bit address derivation changed"
);

#define AUDIO_DMA_DESCRIPTOR_COUNT 6U
#define AUDIO_DMA_FRAMES_PER_DESCRIPTOR 128U
#define AUDIO_ZERO_PREROLL_FRAMES 256U
#define AUDIO_AMP_SETTLE_MS 20U
#define AUDIO_CODEC_MUTE_SETTLE_MS 10U
#define ES8311_CLOCK_SOURCE_REGISTER 0x01
#define ES8311_CLOCK_PRE_DIV_REGISTER 0x02
#define ES8311_ADC_OSR_REGISTER 0x03
#define ES8311_DAC_OSR_REGISTER 0x04
#define ES8311_ADC_DAC_DIV_REGISTER 0x05
#define ES8311_BCLK_DIV_REGISTER 0x06
#define ES8311_LRCLK_DIV_HIGH_REGISTER 0x07
#define ES8311_LRCLK_DIV_LOW_REGISTER 0x08
#define ES8311_SERIAL_DATA_REGISTER 0x09
#define ES8311_DAC_MUTE_REGISTER 0x31
#define ES8311_DAC_MUTE_MASK 0x60
#define ES8311_DAC_VOLUME_REGISTER 0x32

typedef struct {
    uint8_t address;
    uint8_t mask;
    uint8_t expected;
} codec_register_guard_t;

static const codec_register_guard_t CLOCK_FORMAT_GUARDS[] = {
    {ES8311_CLOCK_SOURCE_REGISTER, UINT8_C(0xff), UINT8_C(0x3f)},
    {ES8311_CLOCK_PRE_DIV_REGISTER, UINT8_C(0xff), UINT8_C(0x00)},
    {ES8311_ADC_OSR_REGISTER, UINT8_C(0x7f), UINT8_C(0x10)},
    {ES8311_DAC_OSR_REGISTER, UINT8_C(0x7f), UINT8_C(0x20)},
    {ES8311_ADC_DAC_DIV_REGISTER, UINT8_C(0xff), UINT8_C(0x00)},
    {ES8311_BCLK_DIV_REGISTER, UINT8_C(0x3f), UINT8_C(0x03)},
    {ES8311_LRCLK_DIV_HIGH_REGISTER, UINT8_C(0x3f), UINT8_C(0x00)},
    {ES8311_LRCLK_DIV_LOW_REGISTER, UINT8_C(0xff), UINT8_C(0xff)},
    {ES8311_SERIAL_DATA_REGISTER, UINT8_C(0x1f), UINT8_C(0x0c)},
};

struct platform_audio {
    i2s_chan_handle_t tx_channel;
    const audio_codec_ctrl_if_t *control_interface;
    const audio_codec_data_if_t *data_interface;
    const audio_codec_if_t *codec_interface;
    esp_codec_dev_handle_t codec_device;
    platform_audio_state_t state;
    uint32_t sample_rate_hz;
    uint8_t volume_percent;
};

static esp_err_t codec_status(int status)
{
    return status == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

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

static esp_err_t fail_with_amplifier_shutdown(esp_err_t result)
{
    const esp_err_t shutdown_result = platform_audio_force_safe_shutdown();
    return result != ESP_OK ? result : shutdown_result;
}

static esp_err_t read_codec_register(
    platform_audio_t *audio,
    int address,
    uint8_t *out_value
)
{
    if (audio == NULL || audio->codec_interface == NULL ||
        audio->codec_interface->get_reg == NULL || out_value == NULL) {
        return fail_with_amplifier_shutdown(ESP_ERR_INVALID_STATE);
    }
    int value = 0;
    const esp_err_t result = codec_status(audio->codec_interface->get_reg(
        audio->codec_interface, address, &value
    ));
    if (result != ESP_OK || value < 0 || value > UINT8_MAX) {
        return fail_with_amplifier_shutdown(
            result != ESP_OK ? result : ESP_ERR_INVALID_RESPONSE
        );
    }
    *out_value = (uint8_t)value;
    return ESP_OK;
}

static uint8_t expected_es8311_volume_register(uint8_t volume_percent)
{
    /*
     * esp_codec_dev 1.3.4 maps volume 0 to -96 dB and 1..100 to
     * -49.5..0 dB. ES8311 REG32 maps -95.5..+32 dB to 0x00..0xff,
     * so the reviewed 1..10 percent range maps exactly to 0x5c..0x65.
     */
    return volume_percent == 0U ? 0U : (uint8_t)(91U + volume_percent);
}

static esp_err_t verify_codec_clock_and_format(platform_audio_t *audio)
{
    for (size_t index = 0U;
         index < (sizeof(CLOCK_FORMAT_GUARDS) /
                  sizeof(CLOCK_FORMAT_GUARDS[0]));
         ++index) {
        const codec_register_guard_t guard = CLOCK_FORMAT_GUARDS[index];
        uint8_t value = 0U;
        const esp_err_t result = read_codec_register(
            audio, (int)guard.address, &value
        );
        if (result != ESP_OK || (value & guard.mask) != guard.expected) {
            return fail_with_amplifier_shutdown(
                result != ESP_OK ? result : ESP_ERR_INVALID_RESPONSE
            );
        }
    }
    return ESP_OK;
}

static esp_err_t verify_codec_mute_register(
    platform_audio_t *audio,
    bool muted
)
{
    uint8_t mute_register = 0U;
    esp_err_t result = read_codec_register(
        audio, ES8311_DAC_MUTE_REGISTER, &mute_register
    );
    const uint8_t expected_mute = muted ? ES8311_DAC_MUTE_MASK : 0U;
    if (result != ESP_OK ||
        (mute_register & ES8311_DAC_MUTE_MASK) != expected_mute) {
        return fail_with_amplifier_shutdown(
            result != ESP_OK ? result : ESP_ERR_INVALID_RESPONSE
        );
    }
    return ESP_OK;
}

static esp_err_t verify_codec_volume_register(platform_audio_t *audio)
{
    uint8_t volume_register = 0U;
    const esp_err_t result = read_codec_register(
        audio, ES8311_DAC_VOLUME_REGISTER, &volume_register
    );
    if (result != ESP_OK || volume_register !=
        expected_es8311_volume_register(audio->volume_percent)) {
        return fail_with_amplifier_shutdown(
            result != ESP_OK ? result : ESP_ERR_INVALID_RESPONSE
        );
    }
    return ESP_OK;
}

static esp_err_t verify_codec_output_guard(
    platform_audio_t *audio,
    bool muted
)
{
    esp_err_t result = verify_codec_clock_and_format(audio);
    if (result == ESP_OK) {
        result = verify_codec_mute_register(audio, muted);
    }
    return result == ESP_OK ? verify_codec_volume_register(audio) : result;
}

static esp_err_t set_codec_mute_verified(
    platform_audio_t *audio,
    bool muted
)
{
    if (audio == NULL || audio->codec_device == NULL) {
        return fail_with_amplifier_shutdown(ESP_ERR_INVALID_STATE);
    }
    const esp_err_t result = codec_status(esp_codec_dev_set_out_mute(
        audio->codec_device, muted
    ));
    if (result != ESP_OK) {
        return fail_with_amplifier_shutdown(result);
    }
    return verify_codec_mute_register(audio, muted);
}

static esp_err_t set_codec_volume_verified(platform_audio_t *audio)
{
    if (audio == NULL || audio->codec_device == NULL) {
        return fail_with_amplifier_shutdown(ESP_ERR_INVALID_STATE);
    }
    const esp_err_t result = codec_status(esp_codec_dev_set_out_vol(
        audio->codec_device, (int)audio->volume_percent
    ));
    if (result != ESP_OK) {
        return fail_with_amplifier_shutdown(result);
    }

    return verify_codec_volume_register(audio);
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

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(audio->sample_rate_hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO
        ),
        .gpio_cfg = {
            .mclk = (gpio_num_t)PLATFORM_AUDIO_GPIO_MCLK,
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
    standard_config.clk_cfg.mclk_multiple =
        (i2s_mclk_multiple_t)PLATFORM_AUDIO_MCLK_MULTIPLE;
    result = i2s_channel_init_std_mode(audio->tx_channel, &standard_config);
    if (result != ESP_OK) {
        return result;
    }
    return i2s_channel_enable(audio->tx_channel);
}

static esp_err_t write_zero_preroll(platform_audio_t *audio)
{
    int16_t zeros[AUDIO_ZERO_PREROLL_FRAMES * PLATFORM_AUDIO_CHANNEL_COUNT];
    memset(zeros, 0, sizeof(zeros));
    return codec_status(esp_codec_dev_write(
        audio->codec_device, zeros, (int)sizeof(zeros)
    ));
}

static void release_resources(platform_audio_t *audio)
{
    if (audio == NULL) {
        return;
    }
    (void)platform_audio_force_safe_shutdown();
    if (audio->codec_device != NULL) {
        esp_codec_dev_delete(audio->codec_device);
        audio->codec_device = NULL;
    }
    if (audio->codec_interface != NULL) {
        (void)audio_codec_delete_codec_if(audio->codec_interface);
        audio->codec_interface = NULL;
    }
    if (audio->data_interface != NULL) {
        (void)audio_codec_delete_data_if(audio->data_interface);
        audio->data_interface = NULL;
    }
    if (audio->control_interface != NULL) {
        (void)audio_codec_delete_ctrl_if(audio->control_interface);
        audio->control_interface = NULL;
    }
    if (audio->tx_channel != NULL) {
        const esp_err_t disable_result = i2s_channel_disable(audio->tx_channel);
        if (disable_result != ESP_OK && disable_result != ESP_ERR_INVALID_STATE) {
            (void)platform_audio_force_safe_shutdown();
        }
        (void)i2s_del_channel(audio->tx_channel);
        audio->tx_channel = NULL;
    }
}

esp_err_t platform_audio_create(
    const platform_audio_config_t *config,
    platform_audio_t **out_audio
)
{
#if !CONFIG_PLATFORM_AUDIO_ELECROW_10_1_REVIEWED_BUILD_ONLY
    (void)config;
    (void)out_audio;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (config == NULL || out_audio == NULL || *out_audio != NULL ||
        config->control_bus == NULL ||
        !platform_audio_sample_rate_supported(config->sample_rate_hz) ||
        !platform_audio_bringup_volume_supported(config->volume_percent)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = platform_audio_force_safe_shutdown();
    if (result != ESP_OK) {
        return result;
    }

    platform_audio_t *audio = calloc(1U, sizeof(*audio));
    if (audio == NULL) {
        return ESP_ERR_NO_MEM;
    }
    audio->sample_rate_hz = config->sample_rate_hz;
    audio->volume_percent = config->volume_percent;
    audio->state = PLATFORM_AUDIO_STATE_READY_MUTED;

    result = configure_i2s(audio);
    if (result != ESP_OK) {
        release_resources(audio);
        free(audio);
        return result;
    }

    audio_codec_i2c_cfg_t i2c_config = {
        .port = 0U,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = config->control_bus,
    };
    audio->control_interface = audio_codec_new_i2c_ctrl(&i2c_config);
    if (audio->control_interface == NULL) {
        release_resources(audio);
        free(audio);
        return ESP_FAIL;
    }

    audio_codec_i2s_cfg_t i2s_config = {
        .port = PLATFORM_AUDIO_I2S_CONTROLLER,
        .rx_handle = NULL,
        .tx_handle = audio->tx_channel,
    };
    audio->data_interface = audio_codec_new_i2s_data(&i2s_config);
    if (audio->data_interface == NULL) {
        release_resources(audio);
        free(audio);
        return ESP_FAIL;
    }

    es8311_codec_cfg_t codec_config = {
        .ctrl_if = audio->control_interface,
        .gpio_if = NULL,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .pa_reverted = true,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {
            /* Neutral model: the board's analog PA gain is not measured. */
            .pa_voltage = 1.0F,
            .codec_dac_voltage = 1.0F,
            .pa_gain = 0.0F,
        },
        .no_dac_ref = false,
        .mclk_div = PLATFORM_AUDIO_MCLK_MULTIPLE,
    };
    audio->codec_interface = es8311_codec_new(&codec_config);
    if (audio->codec_interface == NULL) {
        release_resources(audio);
        free(audio);
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = audio->codec_interface,
        .data_if = audio->data_interface,
    };
    audio->codec_device = esp_codec_dev_new(&device_config);
    if (audio->codec_device == NULL) {
        release_resources(audio);
        free(audio);
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t sample_config = {
        .sample_rate = audio->sample_rate_hz,
        .channel = PLATFORM_AUDIO_CHANNEL_COUNT,
        .bits_per_sample = PLATFORM_AUDIO_BITS_PER_SAMPLE,
        .channel_mask = UINT16_C(0x0003),
        .mclk_multiple = PLATFORM_AUDIO_MCLK_MULTIPLE,
    };
    result = codec_status(esp_codec_dev_open(
        audio->codec_device, &sample_config
    ));
    if (result == ESP_OK) {
        result = set_codec_mute_verified(audio, true);
    }
    if (result == ESP_OK) {
        result = set_codec_volume_verified(audio);
    }
    if (result == ESP_OK) {
        result = write_zero_preroll(audio);
    }
    if (result != ESP_OK) {
        release_resources(audio);
        free(audio);
        return result;
    }

    *out_audio = audio;
    return ESP_OK;
#endif
}

esp_err_t platform_audio_start(platform_audio_t *audio)
{
    if (audio == NULL || audio->codec_device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state != PLATFORM_AUDIO_STATE_READY_MUTED) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = write_zero_preroll(audio);
    if (result == ESP_OK) {
        /* Re-read both safety registers immediately before enabling GPIO30. */
        result = verify_codec_output_guard(audio, true);
    }
    if (result == ESP_OK) {
        result = set_amplifier_enabled(true);
    }
    if (result != ESP_OK) {
        (void)platform_audio_force_safe_shutdown();
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(AUDIO_AMP_SETTLE_MS));
    result = set_codec_mute_verified(audio, false);
    if (result != ESP_OK) {
        (void)platform_audio_force_safe_shutdown();
        return result;
    }
    audio->state = PLATFORM_AUDIO_STATE_RUNNING;
    return ESP_OK;
}

esp_err_t platform_audio_write_frames(
    platform_audio_t *audio,
    int16_t *interleaved_pcm,
    size_t frame_count
)
{
    if (audio == NULL || interleaved_pcm == NULL || frame_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state != PLATFORM_AUDIO_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame_count > (SIZE_MAX / PLATFORM_AUDIO_CHANNEL_COUNT)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t sample_count = frame_count * PLATFORM_AUDIO_CHANNEL_COUNT;
    if (sample_count > ((size_t)INT_MAX / sizeof(*interleaved_pcm))) {
        return ESP_ERR_INVALID_SIZE;
    }
    const int byte_count = (int)(sample_count * sizeof(*interleaved_pcm));
    return codec_status(esp_codec_dev_write(
        audio->codec_device, interleaved_pcm, byte_count
    ));
}

esp_err_t platform_audio_stop(platform_audio_t *audio)
{
    if (audio == NULL || audio->codec_device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state == PLATFORM_AUDIO_STATE_READY_MUTED) {
        return platform_audio_force_safe_shutdown();
    }
    if (audio->state != PLATFORM_AUDIO_STATE_RUNNING) {
        (void)platform_audio_force_safe_shutdown();
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t mute_result = set_codec_mute_verified(audio, true);
    if (mute_result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(AUDIO_CODEC_MUTE_SETTLE_MS));
    }
    const esp_err_t shutdown_result = platform_audio_force_safe_shutdown();
    if (shutdown_result == ESP_OK) {
        audio->state = PLATFORM_AUDIO_STATE_READY_MUTED;
    }
    return mute_result != ESP_OK ? mute_result : shutdown_result;
}

esp_err_t platform_audio_get_state(
    const platform_audio_t *audio,
    platform_audio_state_t *out_state
)
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
    *audio = NULL;
    esp_err_t result = ESP_OK;
    if (instance->codec_device != NULL &&
        instance->state == PLATFORM_AUDIO_STATE_RUNNING) {
        result = platform_audio_stop(instance);
    } else {
        result = platform_audio_force_safe_shutdown();
    }
    release_resources(instance);
    free(instance);
    return result;
}
