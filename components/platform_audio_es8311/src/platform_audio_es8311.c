#include "platform_audio_es8311/audio.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef PLATFORM_AUDIO_ES8311_HOST_TEST
#include "platform_audio_es8311_test_hal.h"
#else
#include "audio_codec_ctrl_if.h"
#include "audio_codec_data_if.h"
#include "audio_codec_if.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_types.h"
#include "es8311_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#endif

#include "platform_audio_es8311_policy.h"

#if CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE
#error "platform_audio_es8311 requires the pinned ESP-IDF 5.3+ I2C master API"
#endif

#if !CONFIG_CODEC_ES8311_SUPPORT
#error "platform_audio_es8311 requires CONFIG_CODEC_ES8311_SUPPORT"
#endif

#ifndef CONFIG_PLATFORM_AUDIO_ES8311_ELECROW_10_1_BUILD_ONLY
#define CONFIG_PLATFORM_AUDIO_ES8311_ELECROW_10_1_BUILD_ONLY 0
#endif
#ifndef CONFIG_PLATFORM_AUDIO_ES8311_WAVESHARE_4_3_BUILD_ONLY
#define CONFIG_PLATFORM_AUDIO_ES8311_WAVESHARE_4_3_BUILD_ONLY 0
#endif

_Static_assert(
    ES8311_CODEC_DEFAULT_ADDR ==
        PLATFORM_AUDIO_ES8311_CODEC_ADDRESS_WIRE,
    "pinned esp_codec_dev ES8311 address changed"
);
_Static_assert(
    PLATFORM_AUDIO_ES8311_CODEC_ADDRESS_7BIT ==
        (PLATFORM_AUDIO_ES8311_CODEC_ADDRESS_WIRE >> 1),
    "ES8311 address derivation changed"
);
_Static_assert(
    PLATFORM_AUDIO_ES8311_ZERO_PREROLL_FRAMES ==
        PLATFORM_AUDIO_ES8311_DMA_DESCRIPTOR_COUNT *
            PLATFORM_AUDIO_ES8311_DMA_FRAMES_PER_DESCRIPTOR,
    "zero preroll must cover the complete DMA ring"
);
_Static_assert(
    PLATFORM_AUDIO_ES8311_MAX_WRITE_FRAMES <=
        PLATFORM_AUDIO_ES8311_DMA_FRAMES_PER_DESCRIPTOR,
    "one public write must fit one DMA frame block"
);

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

typedef struct {
    audio_codec_ctrl_if_t base;
    i2c_master_dev_handle_t device;
    bool open;
} platform_audio_es8311_control_t;

typedef struct {
    audio_codec_data_if_t base;
    i2s_chan_handle_t tx_channel;
    bool open;
    bool enabled;
} platform_audio_es8311_data_t;

struct platform_audio_es8311 {
    i2s_chan_handle_t tx_channel;
    platform_audio_es8311_control_t control;
    platform_audio_es8311_data_t data;
    const audio_codec_if_t *codec_interface;
    esp_codec_dev_handle_t codec_device;
    platform_audio_es8311_state_t state;
    uint8_t volume_percent;
    bool codec_device_open;
    bool published;
    int16_t staging[
        PLATFORM_AUDIO_ES8311_MAX_WRITE_FRAMES *
        PLATFORM_AUDIO_ES8311_CHANNEL_COUNT
    ];
};

static platform_audio_es8311_t *s_owner;

static esp_err_t codec_status(int status)
{
    return status == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

static esp_err_t first_error(esp_err_t first, esp_err_t candidate)
{
    return first != ESP_OK ? first : candidate;
}

static int control_open(const audio_codec_ctrl_if_t *interface,
                        void *configuration,
                        int configuration_size)
{
    (void)configuration;
    (void)configuration_size;
    if (interface == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    const platform_audio_es8311_control_t *control =
        (const platform_audio_es8311_control_t *)interface;
    return control->open ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRONG_STATE;
}

static bool control_is_open(const audio_codec_ctrl_if_t *interface)
{
    if (interface == NULL) {
        return false;
    }
    const platform_audio_es8311_control_t *control =
        (const platform_audio_es8311_control_t *)interface;
    return control->open && control->device != NULL;
}

static int control_read_register(const audio_codec_ctrl_if_t *interface,
                                 int register_address,
                                 int register_bytes,
                                 void *data,
                                 int data_bytes)
{
    if (interface == NULL || data == NULL || register_address < 0 ||
        register_address > UINT8_MAX || register_bytes != 1 ||
        data_bytes != 1) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    const platform_audio_es8311_control_t *control =
        (const platform_audio_es8311_control_t *)interface;
    if (!control->open || control->device == NULL) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    const uint8_t address = (uint8_t)register_address;
    const esp_err_t result = i2c_master_transmit_receive(
        control->device, &address, sizeof(address), (uint8_t *)data,
        (size_t)data_bytes, PLATFORM_AUDIO_ES8311_I2C_TIMEOUT_MS
    );
    return result == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static int control_write_register(const audio_codec_ctrl_if_t *interface,
                                  int register_address,
                                  int register_bytes,
                                  void *data,
                                  int data_bytes)
{
    if (interface == NULL || data == NULL || register_address < 0 ||
        register_address > UINT8_MAX || register_bytes != 1 ||
        data_bytes != 1) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    const platform_audio_es8311_control_t *control =
        (const platform_audio_es8311_control_t *)interface;
    if (!control->open || control->device == NULL) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    const uint8_t transaction[2] = {
        (uint8_t)register_address,
        *(const uint8_t *)data,
    };
    const esp_err_t result = i2c_master_transmit(
        control->device, transaction, sizeof(transaction),
        PLATFORM_AUDIO_ES8311_I2C_TIMEOUT_MS
    );
    return result == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int control_close(const audio_codec_ctrl_if_t *interface)
{
    if (interface == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    platform_audio_es8311_control_t *control =
        (platform_audio_es8311_control_t *)interface;
    if (!control->open) {
        return ESP_CODEC_DEV_OK;
    }
    if (control->device == NULL) {
        control->open = false;
        return ESP_CODEC_DEV_OK;
    }
    const esp_err_t result = i2c_master_bus_rm_device(control->device);
    if (result == ESP_OK) {
        control->device = NULL;
        control->open = false;
    }
    return result == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static int data_open(const audio_codec_data_if_t *interface,
                     void *configuration,
                     int configuration_size)
{
    (void)configuration;
    (void)configuration_size;
    if (interface == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    platform_audio_es8311_data_t *data =
        (platform_audio_es8311_data_t *)interface;
    data->open = data->tx_channel != NULL;
    return data->open ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRONG_STATE;
}

static bool data_is_open(const audio_codec_data_if_t *interface)
{
    if (interface == NULL) {
        return false;
    }
    const platform_audio_es8311_data_t *data =
        (const platform_audio_es8311_data_t *)interface;
    return data->open && data->tx_channel != NULL;
}

static int data_enable(const audio_codec_data_if_t *interface,
                       esp_codec_dev_type_t device_type,
                       bool enable)
{
    if (interface == NULL || device_type != ESP_CODEC_DEV_TYPE_OUT) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    platform_audio_es8311_data_t *data =
        (platform_audio_es8311_data_t *)interface;
    if (!data->open || data->tx_channel == NULL) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    if (data->enabled == enable) {
        return ESP_CODEC_DEV_OK;
    }
    const esp_err_t result = enable
        ? i2s_channel_enable(data->tx_channel)
        : i2s_channel_disable(data->tx_channel);
    if (result == ESP_OK) {
        data->enabled = enable;
    }
    return result == ESP_OK ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_DRV_ERR;
}

static bool sample_format_exact(const esp_codec_dev_sample_info_t *format)
{
    return format != NULL &&
        format->sample_rate == PLATFORM_AUDIO_ES8311_SAMPLE_RATE_HZ &&
        format->channel == PLATFORM_AUDIO_ES8311_CHANNEL_COUNT &&
        format->bits_per_sample == PLATFORM_AUDIO_ES8311_BITS_PER_SAMPLE &&
        format->channel_mask == UINT16_C(0x0003) &&
        format->mclk_multiple == PLATFORM_AUDIO_ES8311_MCLK_MULTIPLE;
}

static int data_set_format(const audio_codec_data_if_t *interface,
                           esp_codec_dev_type_t device_type,
                           esp_codec_dev_sample_info_t *format)
{
    if (interface == NULL || device_type != ESP_CODEC_DEV_TYPE_OUT ||
        !sample_format_exact(format)) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    const platform_audio_es8311_data_t *data =
        (const platform_audio_es8311_data_t *)interface;
    return data->open && data->tx_channel != NULL
        ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRONG_STATE;
}

static int data_read(const audio_codec_data_if_t *interface,
                     uint8_t *data,
                     int byte_count)
{
    (void)interface;
    (void)data;
    (void)byte_count;
    return ESP_CODEC_DEV_NOT_SUPPORT;
}

static int data_write(const audio_codec_data_if_t *interface,
                      uint8_t *samples,
                      int byte_count)
{
    if (interface == NULL || samples == NULL || byte_count <= 0) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    const platform_audio_es8311_data_t *data =
        (const platform_audio_es8311_data_t *)interface;
    if (!data->open || !data->enabled || data->tx_channel == NULL) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    size_t bytes_written = 0U;
    const esp_err_t result = i2s_channel_write(
        data->tx_channel, samples, (size_t)byte_count, &bytes_written,
        PLATFORM_AUDIO_ES8311_WRITE_TIMEOUT_MS
    );
    if (result != ESP_OK) {
        return ESP_CODEC_DEV_DRV_ERR;
    }
    return bytes_written == (size_t)byte_count
        ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int data_close(const audio_codec_data_if_t *interface)
{
    if (interface == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }
    platform_audio_es8311_data_t *data =
        (platform_audio_es8311_data_t *)interface;
    if (!data->open) {
        return ESP_CODEC_DEV_OK;
    }
    if (!data->enabled) {
        data->open = false;
        return ESP_CODEC_DEV_OK;
    }
    const int result = data_enable(
        interface, ESP_CODEC_DEV_TYPE_OUT, false
    );
    if (result == ESP_CODEC_DEV_OK) {
        data->open = false;
    }
    return result;
}

static esp_err_t set_amplifier_enabled(bool enabled)
{
    const gpio_num_t pin =
        (gpio_num_t)PLATFORM_AUDIO_ES8311_GPIO_AMP_SHUTDOWN;
    const uint32_t expected = enabled
        ? (uint32_t)PLATFORM_AUDIO_ES8311_AMP_ACTIVE_LEVEL
        : (uint32_t)(1 - PLATFORM_AUDIO_ES8311_AMP_ACTIVE_LEVEL);
    const esp_err_t result = gpio_set_level(pin, expected);
    if (result != ESP_OK) {
        return result;
    }
    return gpio_get_level(pin) == (int)expected
        ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t platform_audio_es8311_force_safe_shutdown(void)
{
    const gpio_num_t pin =
        (gpio_num_t)PLATFORM_AUDIO_ES8311_GPIO_AMP_SHUTDOWN;
    const uint32_t safe_level =
        (uint32_t)(1 - PLATFORM_AUDIO_ES8311_AMP_ACTIVE_LEVEL);
    esp_err_t result = gpio_set_level(pin, safe_level);
    if (result != ESP_OK) {
        return result;
    }
    const gpio_config_t configuration = {
        .pin_bit_mask =
            UINT64_C(1) << PLATFORM_AUDIO_ES8311_GPIO_AMP_SHUTDOWN,
        /* INPUT_OUTPUT is required for a physical pad-level readback. */
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&configuration);
    if (result != ESP_OK) {
        return result;
    }
    result = gpio_set_level(pin, safe_level);
    if (result != ESP_OK) {
        return result;
    }
    return gpio_get_level(pin) == (int)safe_level
        ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static uint8_t expected_volume_register(uint8_t volume_percent)
{
    /* esp_codec_dev 1.3.4's default curve maps 1..100 to -49.5..0 dB. */
    return (uint8_t)(UINT8_C(91) +
        platform_audio_es8311_codec_volume_percent(volume_percent));
}

static esp_err_t read_codec_register(platform_audio_es8311_t *audio,
                                     int address,
                                     uint8_t *out_value)
{
    if (audio == NULL || audio->codec_interface == NULL ||
        audio->codec_interface->get_reg == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int value = 0;
    const esp_err_t result = codec_status(audio->codec_interface->get_reg(
        audio->codec_interface, address, &value
    ));
    if (result != ESP_OK) {
        return result;
    }
    if (value < 0 || value > UINT8_MAX) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *out_value = (uint8_t)value;
    return ESP_OK;
}

static esp_err_t verify_codec_clock_and_format(
    platform_audio_es8311_t *audio
)
{
    for (size_t index = 0U;
         index < sizeof(CLOCK_FORMAT_GUARDS) /
                     sizeof(CLOCK_FORMAT_GUARDS[0]);
         ++index) {
        const codec_register_guard_t guard = CLOCK_FORMAT_GUARDS[index];
        uint8_t value = 0U;
        const esp_err_t result = read_codec_register(
            audio, (int)guard.address, &value
        );
        if (result != ESP_OK) {
            return result;
        }
        if ((value & guard.mask) != guard.expected) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

static esp_err_t verify_codec_mute(platform_audio_es8311_t *audio,
                                   bool muted)
{
    uint8_t value = 0U;
    const esp_err_t result = read_codec_register(
        audio, ES8311_DAC_MUTE_REGISTER, &value
    );
    if (result != ESP_OK) {
        return result;
    }
    const uint8_t expected = muted ? ES8311_DAC_MUTE_MASK : UINT8_C(0);
    return (value & ES8311_DAC_MUTE_MASK) == expected
        ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t verify_codec_volume(platform_audio_es8311_t *audio)
{
    uint8_t value = 0U;
    const esp_err_t result = read_codec_register(
        audio, ES8311_DAC_VOLUME_REGISTER, &value
    );
    if (result != ESP_OK) {
        return result;
    }
    return value == expected_volume_register(audio->volume_percent)
        ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t verify_codec_output_guard(platform_audio_es8311_t *audio,
                                           bool muted)
{
    esp_err_t result = verify_codec_clock_and_format(audio);
    if (result == ESP_OK) {
        result = verify_codec_mute(audio, muted);
    }
    if (result == ESP_OK) {
        result = verify_codec_volume(audio);
    }
    return result;
}

static esp_err_t set_codec_mute_verified(platform_audio_es8311_t *audio,
                                         bool muted)
{
    if (audio == NULL || audio->codec_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = codec_status(esp_codec_dev_set_out_mute(
        audio->codec_device, muted
    ));
    return result == ESP_OK ? verify_codec_mute(audio, muted) : result;
}

static esp_err_t set_codec_volume_verified(platform_audio_es8311_t *audio)
{
    if (audio == NULL || audio->codec_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t codec_volume =
        platform_audio_es8311_codec_volume_percent(audio->volume_percent);
    const esp_err_t result = codec_status(esp_codec_dev_set_out_vol(
        audio->codec_device, (int)codec_volume
    ));
    return result == ESP_OK ? verify_codec_volume(audio) : result;
}

static esp_err_t configure_i2s(platform_audio_es8311_t *audio)
{
    i2s_chan_config_t channel_configuration = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)PLATFORM_AUDIO_ES8311_I2S_CONTROLLER,
        I2S_ROLE_MASTER
    );
    channel_configuration.dma_desc_num =
        PLATFORM_AUDIO_ES8311_DMA_DESCRIPTOR_COUNT;
    channel_configuration.dma_frame_num =
        PLATFORM_AUDIO_ES8311_DMA_FRAMES_PER_DESCRIPTOR;
    channel_configuration.auto_clear = true;
    esp_err_t result = i2s_new_channel(
        &channel_configuration, &audio->tx_channel, NULL
    );
    if (result != ESP_OK) {
        return result;
    }

    const i2s_std_config_t standard_configuration = {
        .clk_cfg = {
            .sample_rate_hz = PLATFORM_AUDIO_ES8311_SAMPLE_RATE_HZ,
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
            .mclk = (gpio_num_t)PLATFORM_AUDIO_ES8311_GPIO_MCLK,
            .bclk = (gpio_num_t)PLATFORM_AUDIO_ES8311_GPIO_BCLK,
            .ws = (gpio_num_t)PLATFORM_AUDIO_ES8311_GPIO_LRCLK,
            .dout = (gpio_num_t)PLATFORM_AUDIO_ES8311_GPIO_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    result = i2s_channel_init_std_mode(
        audio->tx_channel, &standard_configuration
    );
    if (result != ESP_OK) {
        return result;
    }

    audio->data = (platform_audio_es8311_data_t){
        .base = {
            .open = data_open,
            .is_open = data_is_open,
            .enable = data_enable,
            .set_fmt = data_set_format,
            .read = data_read,
            .write = data_write,
            .close = data_close,
        },
        .tx_channel = audio->tx_channel,
        .open = true,
        .enabled = false,
    };
    return ESP_OK;
}

static esp_err_t open_control_device(platform_audio_es8311_t *audio,
                                     void *borrowed_bus)
{
    i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)borrowed_bus;
    esp_err_t result = i2c_master_probe(
        bus, PLATFORM_AUDIO_ES8311_CODEC_ADDRESS_7BIT,
        PLATFORM_AUDIO_ES8311_I2C_TIMEOUT_MS
    );
    if (result != ESP_OK) {
        return result;
    }
    const i2c_device_config_t configuration = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PLATFORM_AUDIO_ES8311_CODEC_ADDRESS_7BIT,
        .scl_speed_hz = PLATFORM_AUDIO_ES8311_I2C_SPEED_HZ,
    };
    audio->control = (platform_audio_es8311_control_t){
        .base = {
            .open = control_open,
            .is_open = control_is_open,
            .read_reg = control_read_register,
            .write_reg = control_write_register,
            .close = control_close,
        },
    };
    result = i2c_master_bus_add_device(
        bus, &configuration, &audio->control.device
    );
    if (result == ESP_OK) {
        audio->control.open = true;
    }
    return result;
}

static esp_err_t write_exact(platform_audio_es8311_t *audio,
                             const int16_t *samples,
                             size_t frame_count)
{
    if (audio == NULL || samples == NULL || audio->tx_channel == NULL ||
        !audio->data.enabled || frame_count == 0U ||
        frame_count > PLATFORM_AUDIO_ES8311_MAX_WRITE_FRAMES) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t byte_count =
        frame_count * PLATFORM_AUDIO_ES8311_CHANNEL_COUNT * sizeof(*samples);
    size_t bytes_written = 0U;
    const esp_err_t result = i2s_channel_write(
        audio->tx_channel, samples, byte_count, &bytes_written,
        PLATFORM_AUDIO_ES8311_WRITE_TIMEOUT_MS
    );
    if (result != ESP_OK) {
        return result;
    }
    return bytes_written == byte_count
        ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t prime_exact_zero_ring(platform_audio_es8311_t *audio)
{
    memset(audio->staging, 0, sizeof(audio->staging));
    size_t frames_remaining = PLATFORM_AUDIO_ES8311_ZERO_PREROLL_FRAMES;
    while (frames_remaining > 0U) {
        const size_t chunk =
            frames_remaining > PLATFORM_AUDIO_ES8311_MAX_WRITE_FRAMES
                ? PLATFORM_AUDIO_ES8311_MAX_WRITE_FRAMES
                : frames_remaining;
        const esp_err_t result = write_exact(audio, audio->staging, chunk);
        if (result != ESP_OK) {
            return result;
        }
        frames_remaining -= chunk;
    }
    return ESP_OK;
}

static esp_err_t create_codec(platform_audio_es8311_t *audio)
{
    const es8311_codec_cfg_t codec_configuration = {
        .ctrl_if = &audio->control.base,
        .gpio_if = NULL,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        /* esp_codec_dev must never own the shared GPIO30 control net. */
        .pa_pin = -1,
        .pa_reverted = true,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = {
            .pa_voltage = 1.0F,
            .codec_dac_voltage = 1.0F,
            .pa_gain = 0.0F,
        },
        .no_dac_ref = false,
        .mclk_div = PLATFORM_AUDIO_ES8311_MCLK_MULTIPLE,
    };
    es8311_codec_cfg_t mutable_codec_configuration = codec_configuration;
    audio->codec_interface = es8311_codec_new(
        &mutable_codec_configuration
    );
    if (audio->codec_interface == NULL) {
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t device_configuration = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = audio->codec_interface,
        .data_if = &audio->data.base,
    };
    audio->codec_device = esp_codec_dev_new(&device_configuration);
    if (audio->codec_device == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_sample_info_t sample_configuration = {
        .sample_rate = PLATFORM_AUDIO_ES8311_SAMPLE_RATE_HZ,
        .channel = PLATFORM_AUDIO_ES8311_CHANNEL_COUNT,
        .bits_per_sample = PLATFORM_AUDIO_ES8311_BITS_PER_SAMPLE,
        .channel_mask = UINT16_C(0x0003),
        .mclk_multiple = PLATFORM_AUDIO_ES8311_MCLK_MULTIPLE,
    };
    esp_err_t result = codec_status(esp_codec_dev_open(
        audio->codec_device, &sample_configuration
    ));
    if (result == ESP_OK) {
        audio->codec_device_open = true;
    }
    if (result == ESP_OK && !audio->data.enabled) {
        result = codec_status(data_enable(
            &audio->data.base, ESP_CODEC_DEV_TYPE_OUT, true
        ));
    }
    if (result == ESP_OK) {
        result = set_codec_mute_verified(audio, true);
    }
    if (result == ESP_OK) {
        result = set_codec_volume_verified(audio);
    }
    if (result == ESP_OK) {
        result = verify_codec_output_guard(audio, true);
    }
    if (result == ESP_OK) {
        result = prime_exact_zero_ring(audio);
    }
    return result;
}

static bool resources_released(const platform_audio_es8311_t *audio)
{
    return audio->tx_channel == NULL && audio->codec_interface == NULL &&
        audio->codec_device == NULL && !audio->control.open &&
        audio->control.device == NULL;
}

static esp_err_t release_resources(platform_audio_es8311_t *audio)
{
    if (audio == NULL) {
        return ESP_OK;
    }
    esp_err_t result = platform_audio_es8311_force_safe_shutdown();
    if (result != ESP_OK) {
        return result;
    }

    if (audio->codec_device != NULL) {
        if (audio->codec_device_open) {
            const esp_err_t close_result = codec_status(
                esp_codec_dev_close(audio->codec_device)
            );
            if (close_result != ESP_OK) {
                return close_result;
            }
            audio->codec_device_open = false;
        }
        esp_codec_dev_delete(audio->codec_device);
        audio->codec_device = NULL;
    }
    if (audio->codec_interface != NULL) {
        const esp_err_t delete_result = codec_status(
            audio_codec_delete_codec_if(audio->codec_interface)
        );
        /* The pinned delete function frees even when close reports failure. */
        audio->codec_interface = NULL;
        result = first_error(result, delete_result);
    }

    if (audio->data.enabled) {
        const esp_err_t disable_result = codec_status(data_enable(
            &audio->data.base, ESP_CODEC_DEV_TYPE_OUT, false
        ));
        result = first_error(result, disable_result);
    }
    audio->data.open = false;

    if (audio->control.open) {
        const esp_err_t control_result = codec_status(control_close(
            &audio->control.base
        ));
        result = first_error(result, control_result);
    }

    if (audio->tx_channel != NULL && !audio->data.enabled) {
        const esp_err_t delete_result = i2s_del_channel(audio->tx_channel);
        if (delete_result == ESP_OK) {
            audio->tx_channel = NULL;
            audio->data.tx_channel = NULL;
        }
        result = first_error(result, delete_result);
    }
    return result;
}

static esp_err_t rollback_to_muted(platform_audio_es8311_t *audio,
                                   esp_err_t primary_result)
{
    if (audio == NULL) {
        return primary_result;
    }
    esp_err_t safety_result = platform_audio_es8311_force_safe_shutdown();
    if (audio->codec_device != NULL) {
        safety_result = first_error(
            safety_result, set_codec_mute_verified(audio, true)
        );
    }
    if (audio->data.enabled) {
        const esp_err_t zero_result = prime_exact_zero_ring(audio);
        safety_result = first_error(safety_result, zero_result);
        if (zero_result != ESP_OK) {
            const esp_err_t disable_result = codec_status(data_enable(
                &audio->data.base, ESP_CODEC_DEV_TYPE_OUT, false
            ));
            safety_result = first_error(safety_result, disable_result);
        }
    }
    audio->state = safety_result == ESP_OK
        ? PLATFORM_AUDIO_ES8311_STATE_READY_MUTED
        : PLATFORM_AUDIO_ES8311_STATE_FAILED_SAFE;
    return primary_result != ESP_OK ? primary_result : safety_result;
}

esp_err_t platform_audio_es8311_recover(void)
{
    if (s_owner != NULL && s_owner->published) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_owner == NULL) {
        return platform_audio_es8311_force_safe_shutdown();
    }
    const esp_err_t result = release_resources(s_owner);
    if (resources_released(s_owner)) {
        free(s_owner);
        s_owner = NULL;
    }
    return result;
}

static esp_err_t rollback_failed_create(esp_err_t primary_result)
{
    const esp_err_t cleanup_result = release_resources(s_owner);
    if (s_owner != NULL && resources_released(s_owner)) {
        free(s_owner);
        s_owner = NULL;
    }
    return primary_result != ESP_OK ? primary_result : cleanup_result;
}

esp_err_t platform_audio_es8311_create(
    const platform_audio_es8311_config_t *config,
    platform_audio_es8311_t **out_audio
)
{
#if !CONFIG_PLATFORM_AUDIO_ES8311_ELECROW_10_1_BUILD_ONLY && \
    !CONFIG_PLATFORM_AUDIO_ES8311_WAVESHARE_4_3_BUILD_ONLY
    (void)config;
    (void)out_audio;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (config == NULL || out_audio == NULL || *out_audio != NULL ||
        config->control_bus == NULL ||
        config->sample_rate_hz != PLATFORM_AUDIO_ES8311_SAMPLE_RATE_HZ ||
        !platform_audio_es8311_volume_supported(config->volume_percent)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_owner != NULL && s_owner->published) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = platform_audio_es8311_recover();
    if (result != ESP_OK) {
        return result;
    }

    s_owner = calloc(1U, sizeof(*s_owner));
    if (s_owner == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_owner->state = PLATFORM_AUDIO_ES8311_STATE_READY_MUTED;
    s_owner->volume_percent = config->volume_percent;

    result = platform_audio_es8311_force_safe_shutdown();
    if (result == ESP_OK) {
        result = open_control_device(s_owner, config->control_bus);
    }
    if (result == ESP_OK) {
        result = configure_i2s(s_owner);
    }
    if (result == ESP_OK) {
        result = create_codec(s_owner);
    }
    if (result != ESP_OK) {
        return rollback_failed_create(result);
    }

    s_owner->published = true;
    *out_audio = s_owner;
    return ESP_OK;
#endif
}

esp_err_t platform_audio_es8311_start(platform_audio_es8311_t *audio)
{
    if (audio == NULL || audio != s_owner || !audio->published ||
        audio->codec_device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state != PLATFORM_AUDIO_ES8311_STATE_READY_MUTED) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = platform_audio_es8311_force_safe_shutdown();
    if (result == ESP_OK) {
        result = prime_exact_zero_ring(audio);
    }
    if (result == ESP_OK) {
        result = verify_codec_output_guard(audio, true);
    }
    if (result == ESP_OK) {
        result = set_amplifier_enabled(true);
    }
    if (result != ESP_OK) {
        return rollback_to_muted(audio, result);
    }

    /*
     * No public nonzero write is accepted until state becomes RUNNING. Prime
     * exact zeros after GPIO30 goes low, then hold the auto-clearing DMA path
     * muted for strictly more than the reviewed 350 ms conservative bound.
     */
    result = prime_exact_zero_ring(audio);
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(
            PLATFORM_AUDIO_ES8311_STARTUP_ZERO_MS
        ) + 1U);
        result = prime_exact_zero_ring(audio);
    }
    if (result == ESP_OK) {
        result = verify_codec_output_guard(audio, true);
    }
    if (result == ESP_OK) {
        result = set_codec_mute_verified(audio, false);
    }
    if (result != ESP_OK) {
        return rollback_to_muted(audio, result);
    }
    audio->state = PLATFORM_AUDIO_ES8311_STATE_RUNNING;
    return ESP_OK;
}

esp_err_t platform_audio_es8311_write_frames(
    platform_audio_es8311_t *audio,
    const int16_t *interleaved_pcm,
    size_t frame_count
)
{
    if (audio == NULL || audio != s_owner || interleaved_pcm == NULL ||
        frame_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state != PLATFORM_AUDIO_ES8311_STATE_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame_count > PLATFORM_AUDIO_ES8311_MAX_WRITE_FRAMES) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t sample_count =
        frame_count * PLATFORM_AUDIO_ES8311_CHANNEL_COUNT;
    if (!platform_audio_es8311_attenuate_pcm16(
            interleaved_pcm, audio->staging, sample_count,
            audio->volume_percent)) {
        return rollback_to_muted(audio, ESP_ERR_INVALID_ARG);
    }
    const esp_err_t result = write_exact(
        audio, audio->staging, frame_count
    );
    return result == ESP_OK ? ESP_OK : rollback_to_muted(audio, result);
}

esp_err_t platform_audio_es8311_set_volume(
    platform_audio_es8311_t *audio, uint8_t volume_step)
{
    if (audio == NULL || audio != s_owner ||
        !platform_audio_es8311_volume_supported(volume_step)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state == PLATFORM_AUDIO_ES8311_STATE_FAILED_SAFE) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool was_running =
        audio->state == PLATFORM_AUDIO_ES8311_STATE_RUNNING;
    if (was_running) {
        esp_err_t result = set_codec_mute_verified(audio, true);
        if (result != ESP_OK) {
            audio->state = PLATFORM_AUDIO_ES8311_STATE_FAILED_SAFE;
            return result;
        }
        audio->volume_percent = volume_step;
        result = set_codec_volume_verified(audio);
        if (result != ESP_OK) {
            (void)rollback_to_muted(audio, result);
            return result;
        }
        result = set_codec_mute_verified(audio, false);
        if (result != ESP_OK) {
            (void)rollback_to_muted(audio, result);
            return result;
        }
    } else {
        audio->volume_percent = volume_step;
    }
    return ESP_OK;
}

esp_err_t platform_audio_es8311_get_volume(
    const platform_audio_es8311_t *audio, uint8_t *out_volume_step)
{
    if (audio == NULL || audio != s_owner || out_volume_step == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_volume_step = audio->volume_percent;
    return ESP_OK;
}

esp_err_t platform_audio_es8311_stop(platform_audio_es8311_t *audio)
{
    if (audio == NULL || audio != s_owner || !audio->published) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio->state != PLATFORM_AUDIO_ES8311_STATE_READY_MUTED &&
        audio->state != PLATFORM_AUDIO_ES8311_STATE_RUNNING &&
        audio->state != PLATFORM_AUDIO_ES8311_STATE_FAILED_SAFE) {
        return ESP_ERR_INVALID_STATE;
    }
    /*
     * GPIO30-high readback alone does not prove codec mute or a zero ring.
     * rollback_to_muted() is the sole authority that may publish READY_MUTED;
     * any failed sub-step retains FAILED_SAFE and rejects a later start.
     */
    return rollback_to_muted(audio, ESP_OK);
}

esp_err_t platform_audio_es8311_get_state(
    const platform_audio_es8311_t *audio,
    platform_audio_es8311_state_t *out_state
)
{
    if (audio == NULL || audio != s_owner || out_state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_state = audio->state;
    return ESP_OK;
}

esp_err_t platform_audio_es8311_destroy(platform_audio_es8311_t **audio)
{
    if (audio == NULL || *audio == NULL || *audio != s_owner ||
        !s_owner->published) {
        return ESP_ERR_INVALID_ARG;
    }
    platform_audio_es8311_t *instance = *audio;
    const esp_err_t mute_result = rollback_to_muted(instance, ESP_OK);
    (void)platform_audio_es8311_force_safe_shutdown();
    instance->published = false;
    const esp_err_t release_result = release_resources(instance);
    const esp_err_t result = first_error(mute_result, release_result);
    if (resources_released(instance)) {
        free(instance);
        s_owner = NULL;
        *audio = NULL;
    } else {
        instance->published = true;
        instance->state = PLATFORM_AUDIO_ES8311_STATE_FAILED_SAFE;
    }
    return result;
}
