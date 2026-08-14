#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/audio.h"
#include "platform/audio_tone.h"
#include "sdkconfig.h"

#define AUDIO_DIAG_I2C_PORT I2C_NUM_1
#define AUDIO_DIAG_I2C_SDA GPIO_NUM_45
#define AUDIO_DIAG_I2C_SCL GPIO_NUM_46
#define AUDIO_DIAG_I2C_SCAN_FIRST_ADDRESS 0x08U
#define AUDIO_DIAG_I2C_SCAN_LAST_ADDRESS 0x77U
#define AUDIO_DIAG_I2C_SCAN_PROBE_TIMEOUT_MS 10
#define AUDIO_DIAG_ES8311_7BIT_ADDRESS 0x18U
#define AUDIO_DIAG_I2S_PORT I2S_NUM_1
#define AUDIO_DIAG_I2S_LRCLK GPIO_NUM_21
#define AUDIO_DIAG_I2S_BCLK GPIO_NUM_22
#define AUDIO_DIAG_I2S_DOUT GPIO_NUM_23
#define AUDIO_DIAG_AMP_SHUTDOWN GPIO_NUM_30
#define AUDIO_DIAG_SAMPLE_RATE_HZ 16000U
#define AUDIO_DIAG_TONE_HZ 440U
#define AUDIO_DIAG_TONE_DURATION_MS 400U
#define AUDIO_DIAG_FADE_DURATION_MS 50U
#define AUDIO_DIAG_PEAK_AMPLITUDE 512U
#define AUDIO_DIAG_VOLUME_PERCENT 5U
#define AUDIO_DIAG_CHUNK_FRAMES 128U
#define AUDIO_DIAG_ZERO_PREROLL_FRAMES 256U
#define AUDIO_DIAG_AMP_SETTLE_MS 20U
#define AUDIO_DIAG_POSTROLL_SETTLE_MS 20U
#define AUDIO_DIAG_I2S_WRITE_TIMEOUT_MS 100U
#define AUDIO_DIAG_MAIN_TASK_STACK_BYTES 3584U
#define AUDIO_DIAG_LDO3_CHANNEL 3
#define AUDIO_DIAG_LDO3_MV 2500
#define AUDIO_DIAG_LDO4_CHANNEL 4
#define AUDIO_DIAG_LDO4_MV 3300
#define AUDIO_DIAG_POWER_SETTLE_MS 20U

_Static_assert(
    CONFIG_ESP_MAIN_TASK_STACK_SIZE == AUDIO_DIAG_MAIN_TASK_STACK_BYTES,
    "D2.2 exact stack review requires the frozen main-task stack size"
);

static const char *const TAG = "audio_diag";
static esp_ldo_channel_handle_t s_ldo3;
static esp_ldo_channel_handle_t s_ldo4;
static i2c_master_bus_handle_t s_control_bus;
static i2s_chan_handle_t s_direct_tx;
static bool s_direct_tx_enabled;
static platform_audio_t *s_audio;
static int16_t s_direct_zeros[
    AUDIO_DIAG_ZERO_PREROLL_FRAMES * PLATFORM_AUDIO_CHANNEL_COUNT
];

static void remember_first_error(esp_err_t candidate, esp_err_t *first_error)
{
    if (candidate != ESP_OK && *first_error == ESP_OK) {
        *first_error = candidate;
    }
}

static esp_err_t release_board_power(void)
{
    esp_err_t result = ESP_OK;
    if (s_ldo4 != NULL) {
        const esp_err_t release_result = esp_ldo_release_channel(s_ldo4);
        s_ldo4 = NULL;
        remember_first_error(release_result, &result);
    }
    if (s_ldo3 != NULL) {
        const esp_err_t release_result = esp_ldo_release_channel(s_ldo3);
        s_ldo3 = NULL;
        remember_first_error(release_result, &result);
    }
    return result;
}

static esp_err_t delete_control_bus(void)
{
    if (s_control_bus == NULL) {
        return ESP_OK;
    }
    const esp_err_t result = i2c_del_master_bus(s_control_bus);
    if (result == ESP_OK) {
        s_control_bus = NULL;
    }
    return result;
}

static esp_err_t delete_direct_i2s(void)
{
    if (s_direct_tx == NULL) {
        s_direct_tx_enabled = false;
        return ESP_OK;
    }

    esp_err_t result = ESP_OK;
    if (s_direct_tx_enabled) {
        const esp_err_t disable_result = i2s_channel_disable(s_direct_tx);
        if (disable_result == ESP_OK || disable_result == ESP_ERR_INVALID_STATE) {
            s_direct_tx_enabled = false;
        } else {
            remember_first_error(disable_result, &result);
        }
    }
    if (!s_direct_tx_enabled) {
        const esp_err_t delete_result = i2s_del_channel(s_direct_tx);
        if (delete_result == ESP_OK) {
            s_direct_tx = NULL;
        }
        remember_first_error(delete_result, &result);
    }
    return result;
}

static esp_err_t cleanup_runtime(void)
{
    esp_err_t result = platform_audio_force_safe_shutdown();
    if (result != ESP_OK) {
        /* Do not stop clocks or remove rails while shutdown is unconfirmed. */
        return result;
    }
    if (s_audio != NULL) {
        const esp_err_t destroy_result = platform_audio_destroy(&s_audio);
        remember_first_error(destroy_result, &result);
    }
    remember_first_error(delete_control_bus(), &result);
    remember_first_error(delete_direct_i2s(), &result);
    remember_first_error(platform_audio_force_safe_shutdown(), &result);

    /* Retain pad power if a driver still owns any audio-facing GPIO. */
    if (s_control_bus == NULL && s_direct_tx == NULL && s_audio == NULL) {
        remember_first_error(release_board_power(), &result);
    }
    return result;
}

static void halt_safe(const char *stage, esp_err_t error)
{
    const esp_err_t cleanup_result = cleanup_runtime();
    ESP_LOGE(TAG,
             "P4_AUDIO D2.2 HALT stage=%s error=%s cleanup=%s "
             "amp_shutdown_requested=1",
             stage, esp_err_to_name(error), esp_err_to_name(cleanup_result));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

static esp_err_t acquire_board_power(void)
{
    const esp_ldo_channel_config_t ldo3_config = {
        .chan_id = AUDIO_DIAG_LDO3_CHANNEL,
        .voltage_mv = AUDIO_DIAG_LDO3_MV,
    };
    esp_err_t result = esp_ldo_acquire_channel(&ldo3_config, &s_ldo3);
    if (result != ESP_OK) {
        return result;
    }

    const esp_ldo_channel_config_t ldo4_config = {
        .chan_id = AUDIO_DIAG_LDO4_CHANNEL,
        .voltage_mv = AUDIO_DIAG_LDO4_MV,
    };
    result = esp_ldo_acquire_channel(&ldo4_config, &s_ldo4);
    if (result != ESP_OK) {
        (void)release_board_power();
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(AUDIO_DIAG_POWER_SETTLE_MS));
    return ESP_OK;
}

static esp_err_t create_control_bus(i2c_master_bus_handle_t *out_bus)
{
    if (out_bus == NULL || *out_bus != NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const i2c_master_bus_config_t config = {
        .i2c_port = AUDIO_DIAG_I2C_PORT,
        .sda_io_num = AUDIO_DIAG_I2C_SDA,
        .scl_io_num = AUDIO_DIAG_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7U,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&config, out_bus);
}

static esp_err_t scan_control_bus(bool *out_es8311_ack, uint32_t *out_ack_count)
{
    if (s_control_bus == NULL || out_es8311_ack == NULL ||
        out_ack_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_es8311_ack = false;
    *out_ack_count = 0U;
    for (uint32_t address = AUDIO_DIAG_I2C_SCAN_FIRST_ADDRESS;
         address <= AUDIO_DIAG_I2C_SCAN_LAST_ADDRESS; ++address) {
        const esp_err_t result = i2c_master_probe(
            s_control_bus, (uint16_t)address,
            AUDIO_DIAG_I2C_SCAN_PROBE_TIMEOUT_MS
        );
        if (result == ESP_OK) {
            ++(*out_ack_count);
            ESP_LOGI(TAG, "P4_AUDIO D2.2 I2C_ACK bus=1 addr7=0x%02x",
                     (unsigned)address);
            if (address == AUDIO_DIAG_ES8311_7BIT_ADDRESS) {
                *out_es8311_ack = true;
            }
        } else if (result != ESP_ERR_NOT_FOUND && result != ESP_ERR_TIMEOUT) {
            return result;
        }
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.2 I2C_SCAN_DONE bus=1 first=0x08 last=0x77 "
             "acks=%u es8311_ack=%u",
             (unsigned)*out_ack_count, *out_es8311_ack ? 1U : 0U);
    return ESP_OK;
}

static esp_err_t direct_i2s_write(
    const int16_t *samples,
    size_t frame_count
)
{
    if (s_direct_tx == NULL || !s_direct_tx_enabled || samples == NULL ||
        frame_count == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t bytes = frame_count * PLATFORM_AUDIO_CHANNEL_COUNT *
                         sizeof(*samples);
    size_t bytes_written = 0U;
    const esp_err_t result = i2s_channel_write(
        s_direct_tx, samples, bytes, &bytes_written,
        AUDIO_DIAG_I2S_WRITE_TIMEOUT_MS
    );
    if (result != ESP_OK) {
        return result;
    }
    return bytes_written == bytes ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t create_direct_i2s(void)
{
    if (s_direct_tx != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(
        AUDIO_DIAG_I2S_PORT, I2S_ROLE_MASTER
    );
    channel_config.dma_desc_num = 6U;
    channel_config.dma_frame_num = 128U;
    channel_config.auto_clear = true;
    esp_err_t result = i2s_new_channel(
        &channel_config, &s_direct_tx, NULL
    );
    if (result != ESP_OK) {
        return result;
    }

    const i2s_std_config_t standard_config = {
        .clk_cfg = {
            .sample_rate_hz = AUDIO_DIAG_SAMPLE_RATE_HZ,
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
            .bclk = AUDIO_DIAG_I2S_BCLK,
            .ws = AUDIO_DIAG_I2S_LRCLK,
            .dout = AUDIO_DIAG_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    result = i2s_channel_init_std_mode(s_direct_tx, &standard_config);
    if (result != ESP_OK) {
        return result;
    }
    result = i2s_channel_enable(s_direct_tx);
    if (result == ESP_OK) {
        s_direct_tx_enabled = true;
    }
    return result;
}

static esp_err_t run_guarded_codec_tone(
    int16_t *tone,
    size_t frame_count
)
{
    const platform_audio_config_t audio_config = {
        .control_bus = s_control_bus,
        .sample_rate_hz = AUDIO_DIAG_SAMPLE_RATE_HZ,
        .volume_percent = AUDIO_DIAG_VOLUME_PERCENT,
    };
    esp_err_t result = platform_audio_create(&audio_config, &s_audio);
    if (result != ESP_OK) {
        return result;
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.2 READY path=guarded-es8311 i2s=1 lrclk=21 "
             "bclk=22 dout=23 mclk=24 mclk_hz=4096000 i2c=1 "
             "sda=45 scl=46 addr7=0x18 volume=%u",
             AUDIO_DIAG_VOLUME_PERCENT);

    ESP_LOGI(TAG,
             "P4_AUDIO D2.2 TONE_BEGIN path=guarded-es8311 hz=%u "
             "duration_ms=%u peak=%u fade_ms=%u volume=%u",
             AUDIO_DIAG_TONE_HZ, AUDIO_DIAG_TONE_DURATION_MS,
             AUDIO_DIAG_PEAK_AMPLITUDE, AUDIO_DIAG_FADE_DURATION_MS,
             AUDIO_DIAG_VOLUME_PERCENT);
    result = platform_audio_start(s_audio);
    for (size_t offset = 0U; result == ESP_OK && offset < frame_count;
         offset += AUDIO_DIAG_CHUNK_FRAMES) {
        size_t chunk_frames = frame_count - offset;
        if (chunk_frames > AUDIO_DIAG_CHUNK_FRAMES) {
            chunk_frames = AUDIO_DIAG_CHUNK_FRAMES;
        }
        result = platform_audio_write_frames(
            s_audio, &tone[offset * PLATFORM_AUDIO_CHANNEL_COUNT], chunk_frames
        );
    }
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(AUDIO_DIAG_POSTROLL_SETTLE_MS));
        result = platform_audio_stop(s_audio);
    } else {
        (void)platform_audio_stop(s_audio);
    }
    return result;
}

static esp_err_t run_direct_i2s_tone(
    const int16_t *tone,
    size_t frame_count
)
{
    esp_err_t result = create_direct_i2s();
    if (result != ESP_OK) {
        return result;
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.2 READY path=factory-direct-i2s i2s=1 lrclk=21 "
             "bclk=22 dout=23 mclk=unused");

    memset(s_direct_zeros, 0, sizeof(s_direct_zeros));
    result = direct_i2s_write(
        s_direct_zeros, AUDIO_DIAG_ZERO_PREROLL_FRAMES
    );
    if (result != ESP_OK) {
        return result;
    }

    result = gpio_set_level(AUDIO_DIAG_AMP_SHUTDOWN, 0U);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(AUDIO_DIAG_AMP_SETTLE_MS));
    ESP_LOGI(TAG,
             "P4_AUDIO D2.2 TONE_BEGIN path=factory-direct-i2s hz=%u "
             "duration_ms=%u peak=%u fade_ms=%u zero_preroll_frames=%u",
             AUDIO_DIAG_TONE_HZ, AUDIO_DIAG_TONE_DURATION_MS,
             AUDIO_DIAG_PEAK_AMPLITUDE, AUDIO_DIAG_FADE_DURATION_MS,
             AUDIO_DIAG_ZERO_PREROLL_FRAMES);

    for (size_t offset = 0U; result == ESP_OK && offset < frame_count;
         offset += AUDIO_DIAG_CHUNK_FRAMES) {
        size_t chunk_frames = frame_count - offset;
        if (chunk_frames > AUDIO_DIAG_CHUNK_FRAMES) {
            chunk_frames = AUDIO_DIAG_CHUNK_FRAMES;
        }
        result = direct_i2s_write(
            &tone[offset * PLATFORM_AUDIO_CHANNEL_COUNT], chunk_frames
        );
    }
    if (result == ESP_OK) {
        result = direct_i2s_write(
            s_direct_zeros, AUDIO_DIAG_ZERO_PREROLL_FRAMES
        );
    }
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(AUDIO_DIAG_POSTROLL_SETTLE_MS));
    }

    const esp_err_t shutdown_result = platform_audio_force_safe_shutdown();
    remember_first_error(shutdown_result, &result);
    if (result == ESP_OK) {
        result = delete_direct_i2s();
    }
    return result;
}

void app_main(void)
{
    /* GPIO30 must be safe before logging, allocation, bus, or clock work. */
    esp_err_t result = platform_audio_force_safe_shutdown();
    ESP_LOGI(TAG,
             "P4_AUDIO D2.2 START amp=NS4263B factory_semantics=c5a4373 "
             "flash_authorized=false");
    if (result != ESP_OK) {
        halt_safe("amp-safe-first", result);
    }
    ESP_LOGI(TAG, "P4_AUDIO D2.2 SAFE gpio30=1 active_low=true");

    result = acquire_board_power();
    if (result != ESP_OK) {
        halt_safe("board-power", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.2 POWER_READY ldo3_mv=%d ldo4_mv=%d settle_ms=%u",
             AUDIO_DIAG_LDO3_MV, AUDIO_DIAG_LDO4_MV,
             AUDIO_DIAG_POWER_SETTLE_MS);

    result = create_control_bus(&s_control_bus);
    if (result != ESP_OK) {
        halt_safe("i2c1-bus", result);
    }
    bool es8311_ack = false;
    uint32_t ack_count = 0U;
    result = scan_control_bus(&es8311_ack, &ack_count);
    if (result != ESP_OK) {
        halt_safe("i2c1-scan", result);
    }

    const size_t frame_count =
        ((size_t)AUDIO_DIAG_SAMPLE_RATE_HZ * AUDIO_DIAG_TONE_DURATION_MS) /
        1000U;
    const size_t sample_count = frame_count * PLATFORM_AUDIO_CHANNEL_COUNT;
    int16_t *tone = calloc(sample_count, sizeof(*tone));
    if (tone == NULL) {
        halt_safe("tone-allocate", ESP_ERR_NO_MEM);
    }
    const platform_audio_tone_config_t tone_config = {
        .sample_rate_hz = AUDIO_DIAG_SAMPLE_RATE_HZ,
        .frequency_hz = AUDIO_DIAG_TONE_HZ,
        .peak_amplitude = AUDIO_DIAG_PEAK_AMPLITUDE,
        .frame_count = frame_count,
        .fade_frames =
            (AUDIO_DIAG_SAMPLE_RATE_HZ * AUDIO_DIAG_FADE_DURATION_MS) / 1000U,
    };
    if (!platform_audio_generate_quiet_tone(
            &tone_config, tone, sample_count)) {
        free(tone);
        halt_safe("tone-generate", ESP_ERR_INVALID_ARG);
    }

    const char *path = "guarded-es8311";
    if (es8311_ack) {
        ESP_LOGI(TAG, "P4_AUDIO D2.2 PATH guarded-es8311 addr7=0x18");
        result = run_guarded_codec_tone(tone, frame_count);
    } else {
        path = "factory-direct-i2s";
        ESP_LOGW(TAG,
                 "P4_AUDIO D2.2 PATH factory-direct-i2s reason=no-es8311-ack "
                 "scan_acks=%u",
                 (unsigned)ack_count);
        result = delete_control_bus();
        if (result == ESP_OK) {
            result = run_direct_i2s_tone(tone, frame_count);
        }
    }
    free(tone);
    if (result != ESP_OK) {
        halt_safe("tone-playback", result);
    }

    const esp_err_t cleanup_result = cleanup_runtime();
    if (cleanup_result != ESP_OK) {
        halt_safe("cleanup", cleanup_result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.2 PASS path=%s tone_count=1 bounded_ms=%u "
             "amp_shutdown=1 ldo3_released=1 ldo4_released=1",
             path, AUDIO_DIAG_TONE_DURATION_MS);
    while (true) {
        ESP_LOGI(TAG, "P4_AUDIO D2.2 HEARTBEAT amp_shutdown=1");
        vTaskDelay(pdMS_TO_TICKS(5000U));
    }
}
