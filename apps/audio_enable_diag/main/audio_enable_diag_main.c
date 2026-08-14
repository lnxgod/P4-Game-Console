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

#define D25_LDO3_CHANNEL 3
#define D25_LDO3_MV 2500
#define D25_LDO4_CHANNEL 4
#define D25_LDO4_MV 3300
#define D25_POWER_SETTLE_MS 20U
#define D25_SERIAL_ATTACH_MS 2000U
#define D25_HOST_ARM_TIMEOUT_MS 15000U
#define D25_HOST_ARM_DUPLICATE_GUARD_MS 100U
#define D25_HOST_ARM_NONCE_HEX_BYTES 64U
#define D25_HOST_ARM_LINE_BYTES 192U
#define D25_CAPTURE_ARM_MS 5000U
#define D25_DIRECT_CTRL_SETTLE_MS 350U

#define D25_I2S_PORT I2S_NUM_1
#define D25_GPIO_LRCLK GPIO_NUM_21
#define D25_GPIO_BCLK GPIO_NUM_22
#define D25_GPIO_DOUT GPIO_NUM_23
#define D25_GPIO_AUDIO_OUT_SD GPIO_NUM_30
#define D25_SAMPLE_RATE_HZ 16000U
#define D25_CHANNEL_COUNT 2U
#define D25_BITS_PER_SAMPLE 16U
#define D25_DMA_DESCRIPTOR_COUNT 6U
#define D25_DMA_FRAMES_PER_DESCRIPTOR 256U
#define D25_DMA_RING_FRAMES \
    (D25_DMA_DESCRIPTOR_COUNT * D25_DMA_FRAMES_PER_DESCRIPTOR)
#define D25_WRITE_FRAMES 128U
#define D25_WRITE_TIMEOUT_MS 100U
#define D25_ZERO_CHUNKS (D25_DMA_RING_FRAMES / D25_WRITE_FRAMES)
#define D25_ZERO_DRAIN_MS 120U

#define D25_TONE_HZ 440U
#define D25_TONE_MS 400U
#define D25_TONE_PEAK 4096U
#define D25_VENDOR_MAX_PEAK 32767U
#define D25_FADE_MS 80U
#define D25_TONE_FRAMES ((D25_SAMPLE_RATE_HZ * D25_TONE_MS) / 1000U)
#define D25_FADE_FRAMES ((D25_SAMPLE_RATE_HZ * D25_FADE_MS) / 1000U)
#define D25_TONE_CHUNKS (D25_TONE_FRAMES / D25_WRITE_FRAMES)

_Static_assert(D25_BITS_PER_SAMPLE == 16U,
               "D2.5 sample width changed");
_Static_assert(D25_DMA_RING_FRAMES == 1536U,
               "D2.5 exact DMA ring changed");
_Static_assert(D25_DMA_RING_FRAMES % D25_WRITE_FRAMES == 0U,
               "D2.5 zero ring must use full writes");
_Static_assert(D25_TONE_FRAMES == 6400U,
               "D2.5 tone duration must remain exact");
_Static_assert(D25_FADE_FRAMES == 1280U,
               "D2.5 fade duration must remain exact");
_Static_assert(D25_TONE_FRAMES % D25_WRITE_FRAMES == 0U,
               "D2.5 tone must use full writes");
_Static_assert(D25_TONE_PEAK < D25_VENDOR_MAX_PEAK,
               "D2.5 diagnostic must remain below vendor full scale");
_Static_assert(D25_WRITE_TIMEOUT_MS < 250U,
               "D2.5 writes must remain bounded below Doom stop budget");

static const char *const TAG = "audio_enable_diag";
static const char s_authorization_id[] =
    "audio-enable-diag-d25-one-shot-authorization-2026-08-13";
static const char s_host_arm_nonce[] =
    "e2826d2166a862e476ec183e190a2ab8b3ba42bc4bb11d9cf232ccba19faca4c";
static const char s_host_arm_line[] =
    "P4_AUDIO_D25_ARM "
    "audio-enable-diag-d25-one-shot-authorization-2026-08-13 "
    "e2826d2166a862e476ec183e190a2ab8b3ba42bc4bb11d9cf232ccba19faca4c";

_Static_assert(sizeof(s_host_arm_nonce) - 1U ==
                   D25_HOST_ARM_NONCE_HEX_BYTES,
               "D2.5 host-arm nonce must remain exact");
_Static_assert(sizeof(s_host_arm_line) <= D25_HOST_ARM_LINE_BYTES,
               "D2.5 host-arm frame must fit parser");
_Static_assert(sizeof(s_host_arm_line) - 1U == 137U,
               "D2.5 host-arm frame identity changed");

static esp_ldo_channel_handle_t s_ldo3;
static esp_ldo_channel_handle_t s_ldo4;
static i2s_chan_handle_t s_tx;
static bool s_tx_enabled;
static bool s_power_owned;
static bool s_ctrl_state_known;
static bool s_ctrl_enabled;
static int16_t *s_tone;
static int16_t s_io[D25_WRITE_FRAMES * D25_CHANNEL_COUNT];

static void remember_first_error(esp_err_t candidate, esp_err_t *first_error)
{
    if (candidate != ESP_OK && *first_error == ESP_OK) {
        *first_error = candidate;
    }
}

/*
 * GPIO30 high shuts down populated analog U4 and requests optional Q10 off.
 * GPIO30 low follows Elecrow's factory playback sequence: U4 leaves shutdown
 * and optional Q10 may pull AUDIO_CTRL high through its NC population path.
 */
static esp_err_t set_audio_output_enabled(bool enabled)
{
    const uint32_t level = enabled ? 0U : 1U;
    s_ctrl_state_known = false;

    /* On the first call this gpio_set_level is the first hardware action. */
    esp_err_t result = gpio_set_level(D25_GPIO_AUDIO_OUT_SD, level);
    if (result != ESP_OK) {
        return result;
    }
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << D25_GPIO_AUDIO_OUT_SD,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&config);
    if (result != ESP_OK) {
        return result;
    }
    result = gpio_set_level(D25_GPIO_AUDIO_OUT_SD, level);
    if (result != ESP_OK) {
        return result;
    }
    if ((uint32_t)gpio_get_level(D25_GPIO_AUDIO_OUT_SD) != level) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    s_ctrl_state_known = true;
    s_ctrl_enabled = enabled;
    return ESP_OK;
}

static esp_err_t wait_for_host_arm(char *nonce_hex, size_t nonce_bytes)
{
    if (nonce_hex == NULL || nonce_bytes !=
        D25_HOST_ARM_NONCE_HEX_BYTES + 1U) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t discarded = 0U;
    while (esp_rom_output_rx_one_char(&discarded) == 0) {
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 WAIT_ARM auth=%s timeout_ms=%u ldo3_ldo4=off "
             "gpio30=1 output_request=inactive direct_output=unclocked",
             s_authorization_id, (unsigned)D25_HOST_ARM_TIMEOUT_MS);

    const int64_t deadline_us = esp_timer_get_time() +
        ((int64_t)D25_HOST_ARM_TIMEOUT_MS * INT64_C(1000));
    char line[D25_HOST_ARM_LINE_BYTES] = {0};
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
    memcpy(nonce_hex, s_host_arm_nonce, D25_HOST_ARM_NONCE_HEX_BYTES);
    nonce_hex[D25_HOST_ARM_NONCE_HEX_BYTES] = '\0';

    const int64_t duplicate_deadline_us = esp_timer_get_time() +
        ((int64_t)D25_HOST_ARM_DUPLICATE_GUARD_MS * INT64_C(1000));
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
        .chan_id = D25_LDO3_CHANNEL,
        .voltage_mv = D25_LDO3_MV,
    };
    esp_err_t result = esp_ldo_acquire_channel(&ldo3_config, &s_ldo3);
    if (result != ESP_OK) {
        return result;
    }
    const esp_ldo_channel_config_t ldo4_config = {
        .chan_id = D25_LDO4_CHANNEL,
        .voltage_mv = D25_LDO4_MV,
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
    vTaskDelay(pdMS_TO_TICKS(D25_POWER_SETTLE_MS));
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
    for (size_t chunk = 0U; chunk < (size_t)D25_ZERO_CHUNKS; ++chunk) {
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
        D25_I2S_PORT, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = D25_DMA_DESCRIPTOR_COUNT;
    channel_config.dma_frame_num = D25_DMA_FRAMES_PER_DESCRIPTOR;
    channel_config.auto_clear = true;
    esp_err_t result = i2s_new_channel(&channel_config, &s_tx, NULL);
    if (result != ESP_OK) {
        return result;
    }
    const i2s_std_config_t standard_config = {
        .clk_cfg = {
            .sample_rate_hz = D25_SAMPLE_RATE_HZ,
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
            .bclk = D25_GPIO_BCLK,
            .ws = D25_GPIO_LRCLK,
            .dout = D25_GPIO_DOUT,
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
        frame_count != D25_WRITE_FRAMES) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t byte_count = frame_count * D25_CHANNEL_COUNT *
                              sizeof(samples[0]);
    size_t written = 0U;
    const esp_err_t result = i2s_channel_write(
        s_tx, samples, byte_count, &written, D25_WRITE_TIMEOUT_MS);
    if (result != ESP_OK) {
        return result;
    }
    return written == byte_count ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t write_zero_ring_and_drain(void)
{
    memset(s_io, 0, sizeof(s_io));
    for (size_t chunk = 0U; chunk < (size_t)D25_ZERO_CHUNKS; ++chunk) {
        const esp_err_t result = write_exact(s_io, D25_WRITE_FRAMES);
        if (result != ESP_OK) {
            return result;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(D25_ZERO_DRAIN_MS));
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
    /* Emergency cleanup requests and verifies GPIO30 high before any wait. */
    esp_err_t result = set_audio_output_enabled(false);
    const bool disable_confirmed =
        result == ESP_OK && s_ctrl_state_known && !s_ctrl_enabled;
    bool zero_confirmed = s_tx == NULL || !s_tx_enabled;

    /* Still clear and drain every active DMA descriptor after the high request. */
    if (s_tx != NULL && s_tx_enabled) {
        const esp_err_t zero_result = write_zero_ring_and_drain();
        remember_first_error(zero_result, &result);
        zero_confirmed = zero_result == ESP_OK;
    }

    if (!disable_confirmed || !zero_confirmed) {
        /* Retain clocks, I2S ownership, and rails unless both gates passed. */
        return result != ESP_OK ? result : ESP_ERR_INVALID_STATE;
    }

    const esp_err_t i2s_result = release_i2s();
    remember_first_error(i2s_result, &result);
    if (s_tx != NULL) {
        return result != ESP_OK ? result : ESP_ERR_INVALID_STATE;
    }
    remember_first_error(release_board_power(), &result);
    if (!s_power_owned) {
        free(s_tone);
        s_tone = NULL;
    }
    return result;
}

static void halt_safe(const char *stage, esp_err_t error)
{
    esp_err_t cleanup_result = cleanup_runtime();
    ESP_LOGE(TAG,
             "P4_AUDIO D2.5 HALT stage=%s error=%s cleanup=%s "
             "zero_requested=1 gpio30_high_requested=1 resources_retained=%u",
             stage, esp_err_to_name(error), esp_err_to_name(cleanup_result),
             (s_tx != NULL || s_power_owned) ? 1U : 0U);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
        cleanup_result = cleanup_runtime();
        ESP_LOGE(TAG,
                 "P4_AUDIO D2.5 CLEANUP_RETRY result=%s "
                 "resources_retained=%u",
                 esp_err_to_name(cleanup_result),
                 (s_tx != NULL || s_power_owned) ? 1U : 0U);
    }
}

static bool generate_tone(void)
{
    const size_t sample_count = (size_t)D25_TONE_FRAMES * D25_CHANNEL_COUNT;
    s_tone = calloc(sample_count, sizeof(s_tone[0]));
    if (s_tone == NULL) {
        return false;
    }
    const uint32_t phase_step = (uint32_t)(
        ((uint64_t)D25_TONE_HZ << 32U) / (uint64_t)D25_SAMPLE_RATE_HZ);
    uint32_t phase = 0U;
    for (size_t frame = 0U; frame < (size_t)D25_TONE_FRAMES; ++frame) {
        const uint32_t position = phase >> 16U;
        const int32_t triangle = position < UINT32_C(32768)
            ? -INT32_C(32768) + (int32_t)(position * 2U)
            : INT32_C(98302) - (int32_t)(position * 2U);
        const int32_t scaled =
            (triangle * (int32_t)D25_TONE_PEAK) / INT32_C(32768);
        uint32_t envelope = D25_FADE_FRAMES;
        if (frame < (size_t)D25_FADE_FRAMES) {
            envelope = (uint32_t)frame;
        } else {
            const size_t after = (size_t)D25_TONE_FRAMES - 1U - frame;
            if (after < (size_t)D25_FADE_FRAMES) {
                envelope = (uint32_t)after;
            }
        }
        const int16_t sample = (int16_t)(
            ((int64_t)scaled * (int64_t)envelope) /
            (int64_t)D25_FADE_FRAMES);
        s_tone[frame * D25_CHANNEL_COUNT] = sample;
        s_tone[(frame * D25_CHANNEL_COUNT) + 1U] = sample;
        phase += phase_step;
    }
    return s_tone[0] == 0 &&
           s_tone[sample_count - D25_CHANNEL_COUNT] == 0;
}

static esp_err_t write_tone(void)
{
    for (size_t chunk = 0U; chunk < (size_t)D25_TONE_CHUNKS; ++chunk) {
        const int16_t *const samples =
            &s_tone[chunk * D25_WRITE_FRAMES * D25_CHANNEL_COUNT];
        const esp_err_t result = write_exact(samples, D25_WRITE_FRAMES);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}

void app_main(void)
{
    /* Literal first hardware action: shut down U4 and request optional Q10 off. */
    esp_err_t result = set_audio_output_enabled(false);
    if (result != ESP_OK) {
        halt_safe("initial-output-disable", result);
    }
    vTaskDelay(pdMS_TO_TICKS(D25_SERIAL_ATTACH_MS));
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 SERIAL_ATTACH wait_ms=%u complete=1 "
             "ldo3_ldo4=off gpio30=1 output_request=inactive "
             "direct_output=unclocked",
             (unsigned)D25_SERIAL_ATTACH_MS);

    char arm_nonce[D25_HOST_ARM_NONCE_HEX_BYTES + 1U] = {0};
    result = wait_for_host_arm(arm_nonce, sizeof(arm_nonce));
    if (result != ESP_OK) {
        halt_safe("host-arm", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 ARM_ACCEPTED auth=%s nonce=%s tx_count=1",
             s_authorization_id, arm_nonce);
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 START backend=diagnostic-local-direct-i2s "
             "waveform=triangle output_peak=%u vendor_max_peak=%u "
             "codec=none i2c=unused mclk=unused",
             (unsigned)D25_TONE_PEAK, (unsigned)D25_VENDOR_MAX_PEAK);
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 SAFE gpio30=1 u4_shutdown=1 "
             "optional_q10_ns4168_disable_if_populated=1 "
             "candidate_amp_vdd5v_unswitched=1");

    if (!generate_tone()) {
        halt_safe("tone-generate", ESP_ERR_NO_MEM);
    }
    /* A complete zero ring and running clocks precede both standalone LDOs. */
    result = create_zeroed_i2s();
    if (result != ESP_OK) {
        halt_safe("i2s-create-zero", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 BACKEND_READY state=zero-data-clocks-running "
             "i2s=1 lrclk=21 bclk=22 dout=23 dma_desc=6 dma_frames=256 "
             "zero_preload_frames=1536 write_frames=128 timeout_ms=100");

    result = acquire_board_power();
    if (result != ESP_OK) {
        halt_safe("board-power", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 POWER_READY ldo3_mv=%u ldo4_mv=%u "
             "settle_ms=%u gpio30=1 direct_output=zero "
             "candidate_amp_vdd5v_unswitched=1",
             (unsigned)D25_LDO3_MV, (unsigned)D25_LDO4_MV,
             (unsigned)D25_POWER_SETTLE_MS);
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 CAPTURE_ARM wait_ms=%u gpio30=1 "
             "output_request=inactive direct_output=zero",
             (unsigned)D25_CAPTURE_ARM_MS);
    vTaskDelay(pdMS_TO_TICKS(D25_CAPTURE_ARM_MS));

    result = set_audio_output_enabled(true);
    if (result != ESP_OK) {
        halt_safe("direct-ctrl-enable", result);
    }
    vTaskDelay(pdMS_TO_TICKS(D25_DIRECT_CTRL_SETTLE_MS));
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 GPIO30_ACTIVE requested=1 level=0 "
             "gpio_readback=0 u4_mode=ab_request "
             "optional_q10_ns4168_enable_if_populated=1 settle_ms=%u "
             "zero_data=1",
             (unsigned)D25_DIRECT_CTRL_SETTLE_MS);
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 TONE_BEGIN hz=%u bounded_ms=%u peak=%u "
             "fade_ms=%u chunks=%u",
             (unsigned)D25_TONE_HZ, (unsigned)D25_TONE_MS,
             (unsigned)D25_TONE_PEAK, (unsigned)D25_FADE_MS,
             (unsigned)D25_TONE_CHUNKS);
    result = write_tone();
    if (result != ESP_OK) {
        halt_safe("tone-write", result);
    }
    result = write_zero_ring_and_drain();
    if (result != ESP_OK) {
        halt_safe("zero-postroll", result);
    }
    /* Disable immediately after the verified zero/drain boundary. */
    result = set_audio_output_enabled(false);
    if (result != ESP_OK) {
        halt_safe("direct-ctrl-disable", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 POSTROLL frames=%u chunks=%u drain_ms=%u "
             "ring_covered=1 direct_output_zeroed=1",
             (unsigned)D25_DMA_RING_FRAMES, (unsigned)D25_ZERO_CHUNKS,
             (unsigned)D25_ZERO_DRAIN_MS);
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 GPIO30_INACTIVE requested=1 level=1 "
             "gpio_readback=1 u4_shutdown=1 "
             "optional_q10_ns4168_disable_if_populated=1 "
             "before_i2s_teardown=1");

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
    ESP_LOGI(TAG,
             "P4_AUDIO D2.5 PASS path=diagnostic-local-direct-i2s "
             "tone_count=1 bounded_ms=%u peak=%u exact_writes=%u "
             "partial_writes=0 direct_output_zeroed=1 gpio30=1 "
             "output_inactive_requested=1 i2s_released=1 ldo3_released=1 "
             "ldo4_released=1",
             (unsigned)D25_TONE_MS, (unsigned)D25_TONE_PEAK,
             (unsigned)(D25_TONE_CHUNKS + D25_ZERO_CHUNKS));
    for (;;) {
        ESP_LOGI(TAG,
                 "P4_AUDIO D2.5 HEARTBEAT direct_output=unclocked gpio30=1 "
                 "u4_shutdown=1 optional_q10_ns4168_disable_if_populated=1 "
                 "ldo3_released=1 ldo4_released=1 "
                 "candidate_amp_vdd5v_unswitched=1");
        vTaskDelay(pdMS_TO_TICKS(5000U));
    }
}
