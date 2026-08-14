#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include "esp_rom_uart.h"
#pragma GCC diagnostic pop
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define D24_LDO3_CHANNEL 3
#define D24_LDO3_MV 2500
#define D24_LDO4_CHANNEL 4
#define D24_LDO4_MV 3300
#define D24_POWER_SETTLE_MS 20U
#define D24_SERIAL_ATTACH_MS 2000U
#define D24_HOST_ARM_TIMEOUT_MS 15000U
#define D24_HOST_ARM_DUPLICATE_GUARD_MS 100U
#define D24_HOST_ARM_NONCE_HEX_BYTES 64U
#define D24_HOST_ARM_LINE_BYTES 192U
#define D24_CAPTURE_ARM_MS 5000U

#define D24_I2S_PORT I2S_NUM_1
#define D24_GPIO_LRCLK GPIO_NUM_21
#define D24_GPIO_BCLK GPIO_NUM_22
#define D24_GPIO_DOUT GPIO_NUM_23
#define D24_GPIO_ANALOG_U4_SHUTDOWN GPIO_NUM_30
#define D24_SAMPLE_RATE_HZ 16000U
#define D24_CHANNEL_COUNT 2U
#define D24_BITS_PER_SAMPLE 16U
#define D24_DMA_DESCRIPTOR_COUNT 6U
#define D24_DMA_FRAMES_PER_DESCRIPTOR 256U
#define D24_DMA_RING_FRAMES \
    (D24_DMA_DESCRIPTOR_COUNT * D24_DMA_FRAMES_PER_DESCRIPTOR)
#define D24_WRITE_FRAMES 128U
#define D24_WRITE_TIMEOUT_MS 100U
#define D24_ZERO_CHUNKS (D24_DMA_RING_FRAMES / D24_WRITE_FRAMES)
#define D24_ZERO_DRAIN_MS 120U

#define D24_TONE_HZ 440U
#define D24_TONE_MS 400U
#define D24_TONE_PEAK 4096U
#define D24_VENDOR_MAX_PEAK 32767U
#define D24_FADE_MS 80U
#define D24_TONE_FRAMES ((D24_SAMPLE_RATE_HZ * D24_TONE_MS) / 1000U)
#define D24_FADE_FRAMES ((D24_SAMPLE_RATE_HZ * D24_FADE_MS) / 1000U)
#define D24_TONE_CHUNKS (D24_TONE_FRAMES / D24_WRITE_FRAMES)

_Static_assert(D24_DMA_RING_FRAMES == 1536U,
               "D2.4 exact DMA ring changed");
_Static_assert(D24_DMA_RING_FRAMES % D24_WRITE_FRAMES == 0U,
               "D2.4 zero ring must use full writes");
_Static_assert(D24_TONE_FRAMES == 6400U,
               "D2.4 tone duration must remain exact");
_Static_assert(D24_FADE_FRAMES == 1280U,
               "D2.4 fade duration must remain exact");
_Static_assert(D24_TONE_FRAMES % D24_WRITE_FRAMES == 0U,
               "D2.4 tone must use full writes");
_Static_assert(D24_TONE_PEAK < D24_VENDOR_MAX_PEAK,
               "D2.4 diagnostic must remain below vendor full scale");
_Static_assert(D24_WRITE_TIMEOUT_MS < 250U,
               "D2.4 writes must remain bounded below Doom stop budget");

static const char *const TAG = "audio_level_diag";
static const char s_authorization_id[] =
    "audio-level-diag-d24-one-shot-authorization-2026-08-13";
static const char s_host_arm_nonce[] =
    "ff9175223ffd30ffa0fbf940aa5c705d40a7fd1f6d6bbf9adec6a95d22edb3f3";
static const char s_host_arm_line[] =
    "P4_AUDIO_D24_ARM "
    "audio-level-diag-d24-one-shot-authorization-2026-08-13 "
    "ff9175223ffd30ffa0fbf940aa5c705d40a7fd1f6d6bbf9adec6a95d22edb3f3";

_Static_assert(sizeof(s_host_arm_nonce) - 1U ==
                   D24_HOST_ARM_NONCE_HEX_BYTES,
               "D2.4 host-arm nonce must remain exact");
_Static_assert(sizeof(s_host_arm_line) <= D24_HOST_ARM_LINE_BYTES,
               "D2.4 host-arm frame must fit parser");
_Static_assert(sizeof(s_host_arm_line) - 1U == 136U,
               "D2.4 host-arm frame identity changed");

static esp_ldo_channel_handle_t s_ldo3;
static esp_ldo_channel_handle_t s_ldo4;
static i2s_chan_handle_t s_tx;
static bool s_tx_enabled;
static bool s_power_owned;
static int16_t *s_tone;
static int16_t s_io[D24_WRITE_FRAMES * D24_CHANNEL_COUNT];

static void remember_first_error(esp_err_t candidate, esp_err_t *first_error)
{
    if (candidate != ESP_OK && *first_error == ESP_OK) {
        *first_error = candidate;
    }
}

static esp_err_t force_analog_u4_shutdown(void)
{
    /* GPIO30 reaches only the alternate analog U4 path, not direct amps. */
    esp_err_t result = gpio_set_level(D24_GPIO_ANALOG_U4_SHUTDOWN, 1U);
    if (result != ESP_OK) {
        return result;
    }
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << D24_GPIO_ANALOG_U4_SHUTDOWN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&config);
    return result == ESP_OK
        ? gpio_set_level(D24_GPIO_ANALOG_U4_SHUTDOWN, 1U)
        : result;
}

static esp_err_t wait_for_host_arm(char *nonce_hex, size_t nonce_bytes)
{
    if (nonce_hex == NULL || nonce_bytes !=
        D24_HOST_ARM_NONCE_HEX_BYTES + 1U) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t discarded = 0U;
    while (esp_rom_output_rx_one_char(&discarded) == 0) {
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 WAIT_ARM auth=%s timeout_ms=%u ldo3_ldo4=off "
             "direct_output=unclocked",
             s_authorization_id, (unsigned)D24_HOST_ARM_TIMEOUT_MS);

    const int64_t deadline_us = esp_timer_get_time() +
        ((int64_t)D24_HOST_ARM_TIMEOUT_MS * INT64_C(1000));
    char line[D24_HOST_ARM_LINE_BYTES] = {0};
    size_t used = 0U;
    bool complete = false;
    while (esp_timer_get_time() < deadline_us) {
        uint8_t received = 0U;
        if (esp_rom_output_rx_one_char(&received) != 0) {
            vTaskDelay(pdMS_TO_TICKS(1U));
            continue;
        }
        if (received == (uint8_t)'\n') {
            complete = true;
            break;
        }
        if (received < 0x20U || received > 0x7eU ||
            used + 1U >= sizeof(line)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        line[used++] = (char)received;
    }
    if (!complete) {
        return ESP_ERR_TIMEOUT;
    }
    line[used] = '\0';
    if (used != sizeof(s_host_arm_line) - 1U ||
        memcmp(line, s_host_arm_line, sizeof(s_host_arm_line) - 1U) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    memcpy(nonce_hex, s_host_arm_nonce, D24_HOST_ARM_NONCE_HEX_BYTES);
    nonce_hex[D24_HOST_ARM_NONCE_HEX_BYTES] = '\0';

    const int64_t duplicate_deadline_us = esp_timer_get_time() +
        ((int64_t)D24_HOST_ARM_DUPLICATE_GUARD_MS * INT64_C(1000));
    while (esp_timer_get_time() < duplicate_deadline_us) {
        uint8_t duplicate = 0U;
        if (esp_rom_output_rx_one_char(&duplicate) == 0) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    return ESP_OK;
}

static esp_err_t acquire_board_power(void)
{
    const esp_ldo_channel_config_t ldo3_config = {
        .chan_id = D24_LDO3_CHANNEL,
        .voltage_mv = D24_LDO3_MV,
    };
    esp_err_t result = esp_ldo_acquire_channel(&ldo3_config, &s_ldo3);
    if (result != ESP_OK) {
        return result;
    }
    const esp_ldo_channel_config_t ldo4_config = {
        .chan_id = D24_LDO4_CHANNEL,
        .voltage_mv = D24_LDO4_MV,
    };
    result = esp_ldo_acquire_channel(&ldo4_config, &s_ldo4);
    if (result != ESP_OK) {
        const esp_err_t release_result = esp_ldo_release_channel(s_ldo3);
        if (release_result == ESP_OK) {
            s_ldo3 = NULL;
        }
        s_power_owned = s_ldo3 != NULL;
        return release_result == ESP_OK ? result : release_result;
    }
    s_power_owned = true;
    vTaskDelay(pdMS_TO_TICKS(D24_POWER_SETTLE_MS));
    return ESP_OK;
}

static esp_err_t release_board_power(void)
{
    esp_err_t result = ESP_OK;
    if (s_ldo4 != NULL) {
        const esp_err_t release_result = esp_ldo_release_channel(s_ldo4);
        if (release_result == ESP_OK) {
            s_ldo4 = NULL;
        }
        remember_first_error(release_result, &result);
    }
    if (s_ldo3 != NULL && s_ldo4 == NULL) {
        const esp_err_t release_result = esp_ldo_release_channel(s_ldo3);
        if (release_result == ESP_OK) {
            s_ldo3 = NULL;
        }
        remember_first_error(release_result, &result);
    }
    s_power_owned = s_ldo3 != NULL || s_ldo4 != NULL;
    return result;
}

static esp_err_t preload_zero_ring(void)
{
    if (s_tx == NULL || s_tx_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(s_io, 0, sizeof(s_io));
    for (size_t chunk = 0U; chunk < (size_t)D24_ZERO_CHUNKS; ++chunk) {
        size_t loaded = 0U;
        const esp_err_t result = i2s_channel_preload_data(
            s_tx, s_io, sizeof(s_io), &loaded);
        if (result != ESP_OK) {
            return result;
        }
        if (loaded != sizeof(s_io)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

static esp_err_t create_zeroed_i2s(void)
{
    if (s_tx != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(
        D24_I2S_PORT, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = D24_DMA_DESCRIPTOR_COUNT;
    channel_config.dma_frame_num = D24_DMA_FRAMES_PER_DESCRIPTOR;
    channel_config.auto_clear = true;
    esp_err_t result = i2s_new_channel(&channel_config, &s_tx, NULL);
    if (result != ESP_OK) {
        return result;
    }
    const i2s_std_config_t standard_config = {
        .clk_cfg = {
            .sample_rate_hz = D24_SAMPLE_RATE_HZ,
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
            .bclk = D24_GPIO_BCLK,
            .ws = D24_GPIO_LRCLK,
            .dout = D24_GPIO_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    result = i2s_channel_init_std_mode(s_tx, &standard_config);
    if (result == ESP_OK) {
        result = preload_zero_ring();
    }
    if (result == ESP_OK) {
        result = i2s_channel_enable(s_tx);
        if (result == ESP_OK) {
            s_tx_enabled = true;
        }
    }
    return result;
}

static esp_err_t write_exact(const int16_t *samples, size_t frame_count)
{
    if (s_tx == NULL || !s_tx_enabled || samples == NULL ||
        frame_count != D24_WRITE_FRAMES) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t byte_count = frame_count * D24_CHANNEL_COUNT *
                              sizeof(samples[0]);
    size_t written = 0U;
    const esp_err_t result = i2s_channel_write(
        s_tx, samples, byte_count, &written, D24_WRITE_TIMEOUT_MS);
    if (result != ESP_OK) {
        return result;
    }
    return written == byte_count ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t write_zero_ring_and_drain(void)
{
    memset(s_io, 0, sizeof(s_io));
    for (size_t chunk = 0U; chunk < (size_t)D24_ZERO_CHUNKS; ++chunk) {
        const esp_err_t result = write_exact(s_io, D24_WRITE_FRAMES);
        if (result != ESP_OK) {
            return result;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(D24_ZERO_DRAIN_MS));
    return ESP_OK;
}

static esp_err_t release_i2s(void)
{
    if (s_tx == NULL) {
        s_tx_enabled = false;
        return ESP_OK;
    }
    if (s_tx_enabled) {
        const esp_err_t disable_result = i2s_channel_disable(s_tx);
        if (disable_result != ESP_OK &&
            disable_result != ESP_ERR_INVALID_STATE) {
            return disable_result;
        }
        s_tx_enabled = false;
    }
    const esp_err_t delete_result = i2s_del_channel(s_tx);
    if (delete_result == ESP_OK) {
        s_tx = NULL;
    }
    return delete_result;
}

static esp_err_t cleanup_runtime(void)
{
    esp_err_t result = force_analog_u4_shutdown();
    /*
     * GPIO30 does not control the candidate direct amplifiers. Even if the
     * unrelated U4 shutdown cannot be confirmed, continue zeroing the real
     * direct path before deciding which resources may be released.
     */
    if (s_tx != NULL && s_tx_enabled) {
        const esp_err_t zero_result = write_zero_ring_and_drain();
        remember_first_error(zero_result, &result);
        if (zero_result != ESP_OK) {
            /* Retain clocks, owned LDO3/LDO4, and handle; auto-clear limits stale PCM. */
            return result;
        }
    }
    remember_first_error(release_i2s(), &result);
    if (s_tx == NULL) {
        remember_first_error(release_board_power(), &result);
        free(s_tone);
        s_tone = NULL;
    }
    return result;
}

static void halt_safe(const char *stage, esp_err_t error)
{
    esp_err_t cleanup_result = cleanup_runtime();
    ESP_LOGE(TAG,
             "P4_AUDIO D2.4 HALT stage=%s error=%s cleanup=%s "
             "u4_shutdown_requested=1 direct_zero_requested=1 "
             "resources_retained=%u",
             stage, esp_err_to_name(error), esp_err_to_name(cleanup_result),
             (s_tx != NULL || s_power_owned) ? 1U : 0U);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
        cleanup_result = cleanup_runtime();
        ESP_LOGE(TAG,
                 "P4_AUDIO D2.4 CLEANUP_RETRY result=%s "
                 "resources_retained=%u",
                 esp_err_to_name(cleanup_result),
                 (s_tx != NULL || s_power_owned) ? 1U : 0U);
    }
}

static bool generate_tone(void)
{
    const size_t sample_count = (size_t)D24_TONE_FRAMES * D24_CHANNEL_COUNT;
    s_tone = calloc(sample_count, sizeof(s_tone[0]));
    if (s_tone == NULL) {
        return false;
    }
    const uint32_t phase_step = (uint32_t)(
        ((uint64_t)D24_TONE_HZ << 32U) / (uint64_t)D24_SAMPLE_RATE_HZ);
    uint32_t phase = 0U;
    for (size_t frame = 0U; frame < (size_t)D24_TONE_FRAMES; ++frame) {
        const uint32_t position = phase >> 16U;
        const int32_t triangle = position < UINT32_C(32768)
            ? -INT32_C(32768) + (int32_t)(position * 2U)
            : INT32_C(98302) - (int32_t)(position * 2U);
        const int32_t scaled =
            (triangle * (int32_t)D24_TONE_PEAK) / INT32_C(32768);
        uint32_t envelope = D24_FADE_FRAMES;
        if (frame < (size_t)D24_FADE_FRAMES) {
            envelope = (uint32_t)frame;
        } else {
            const size_t after = (size_t)D24_TONE_FRAMES - 1U - frame;
            if (after < (size_t)D24_FADE_FRAMES) {
                envelope = (uint32_t)after;
            }
        }
        const int16_t sample = (int16_t)(
            ((int64_t)scaled * (int64_t)envelope) /
            (int64_t)D24_FADE_FRAMES);
        s_tone[frame * D24_CHANNEL_COUNT] = sample;
        s_tone[(frame * D24_CHANNEL_COUNT) + 1U] = sample;
        phase += phase_step;
    }
    return s_tone[0] == 0 &&
           s_tone[(sample_count - D24_CHANNEL_COUNT)] == 0;
}

static esp_err_t write_tone(void)
{
    for (size_t chunk = 0U; chunk < (size_t)D24_TONE_CHUNKS; ++chunk) {
        const int16_t *const samples =
            &s_tone[chunk * D24_WRITE_FRAMES * D24_CHANNEL_COUNT];
        const esp_err_t result = write_exact(samples, D24_WRITE_FRAMES);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}

void app_main(void)
{
    /* Literal first hardware action: shut down only the alternate analog U4. */
    esp_err_t result = force_analog_u4_shutdown();
    if (result != ESP_OK) {
        halt_safe("initial-u4-safe", result);
    }
    /* Give the one-open-port capture transport time to leave the ROM reader. */
    vTaskDelay(pdMS_TO_TICKS(D24_SERIAL_ATTACH_MS));
    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 SERIAL_ATTACH wait_ms=%u complete=1 ldo3_ldo4=off "
             "u4_shutdown=1 direct_output=unclocked",
             (unsigned)D24_SERIAL_ATTACH_MS);

    char arm_nonce[D24_HOST_ARM_NONCE_HEX_BYTES + 1U] = {0};
    result = wait_for_host_arm(arm_nonce, sizeof(arm_nonce));
    if (result != ESP_OK) {
        halt_safe("host-arm", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 ARM_ACCEPTED auth=%s nonce=%s tx_count=1",
             s_authorization_id, arm_nonce);
    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 START backend=diagnostic-local-direct-i2s "
             "waveform=triangle output_peak=%u vendor_max_peak=%u "
             "codec=none i2c=unused mclk=unused",
             (unsigned)D24_TONE_PEAK, (unsigned)D24_VENDOR_MAX_PEAK);
    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 SAFE gpio30=1 scope=analog-u4-only "
             "direct_output=not-muted");

    if (!generate_tone()) {
        halt_safe("tone-generate", ESP_ERR_NO_MEM);
    }
    /* Establish a complete zero-data ring before acquiring board LDOs. */
    result = create_zeroed_i2s();
    if (result != ESP_OK) {
        halt_safe("i2s-create-zero", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 BACKEND_READY state=zero-data-clocks-running i2s=1 "
             "lrclk=21 bclk=22 dout=23 dma_desc=6 dma_frames=256 "
             "zero_preload_frames=1536 write_frames=128 timeout_ms=100");
    result = acquire_board_power();
    if (result != ESP_OK) {
        halt_safe("board-power", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 POWER_READY ldo3_mv=%u ldo4_mv=%u settle_ms=%u "
             "direct_output=zero",
             (unsigned)D24_LDO3_MV, (unsigned)D24_LDO4_MV,
             (unsigned)D24_POWER_SETTLE_MS);
    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 CAPTURE_ARM wait_ms=%u direct_output=zero",
             (unsigned)D24_CAPTURE_ARM_MS);
    vTaskDelay(pdMS_TO_TICKS(D24_CAPTURE_ARM_MS));

    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 TONE_BEGIN hz=%u bounded_ms=%u peak=%u "
             "fade_ms=%u chunks=%u",
             (unsigned)D24_TONE_HZ, (unsigned)D24_TONE_MS,
             (unsigned)D24_TONE_PEAK, (unsigned)D24_FADE_MS,
             (unsigned)D24_TONE_CHUNKS);
    result = write_tone();
    if (result != ESP_OK) {
        halt_safe("tone-write", result);
    }
    result = write_zero_ring_and_drain();
    if (result != ESP_OK) {
        halt_safe("zero-postroll", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 POSTROLL frames=%u chunks=%u drain_ms=%u "
             "ring_covered=1 direct_output_zeroed=1",
             (unsigned)D24_DMA_RING_FRAMES, (unsigned)D24_ZERO_CHUNKS,
             (unsigned)D24_ZERO_DRAIN_MS);

    result = release_i2s();
    if (result != ESP_OK) {
        halt_safe("i2s-release", result);
    }
    free(s_tone);
    s_tone = NULL;
    result = release_board_power();
    if (result != ESP_OK) {
        halt_safe("power-release", result);
    }
    result = force_analog_u4_shutdown();
    if (result != ESP_OK) {
        halt_safe("final-u4-safe", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.4 PASS path=diagnostic-local-direct-i2s "
             "tone_count=1 bounded_ms=%u peak=%u exact_writes=%u "
             "partial_writes=0 direct_output_zeroed=1 i2s_released=1 "
             "u4_shutdown=1 ldo3_released=1 ldo4_released=1",
             (unsigned)D24_TONE_MS, (unsigned)D24_TONE_PEAK,
             (unsigned)(D24_TONE_CHUNKS + D24_ZERO_CHUNKS));
    for (;;) {
        ESP_LOGI(TAG,
                 "P4_AUDIO D2.4 HEARTBEAT direct_output=unclocked "
                 "u4_shutdown=1 ldo3_released=1 ldo4_released=1 "
                 "direct_amp_vdd5v_unswitched=1");
        vTaskDelay(pdMS_TO_TICKS(5000U));
    }
}
