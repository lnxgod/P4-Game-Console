#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
#include "platform/audio.h"
#include "platform/audio_tone.h"

#define AUDIO_DIRECT_DIAG_LDO3_CHANNEL 3
#define AUDIO_DIRECT_DIAG_LDO3_MV 2500
#define AUDIO_DIRECT_DIAG_LDO4_CHANNEL 4
#define AUDIO_DIRECT_DIAG_LDO4_MV 3300
#define AUDIO_DIRECT_DIAG_POWER_SETTLE_MS 20U
#define AUDIO_DIRECT_DIAG_CAPTURE_ARM_MS 5000U
#define AUDIO_DIRECT_DIAG_SERIAL_ATTACH_MS 2000U
#define AUDIO_DIRECT_DIAG_HOST_ARM_TIMEOUT_MS 15000U
#define AUDIO_DIRECT_DIAG_HOST_ARM_DUPLICATE_GUARD_MS 100U
#define AUDIO_DIRECT_DIAG_HOST_ARM_NONCE_HEX_BYTES 64U
#define AUDIO_DIRECT_DIAG_HOST_ARM_LINE_BYTES 192U
#define AUDIO_DIRECT_DIAG_TONE_HZ 440U
#define AUDIO_DIRECT_DIAG_TONE_MS 400U
#define AUDIO_DIRECT_DIAG_FADE_MS 50U
#define AUDIO_DIRECT_DIAG_TONE_FRAMES 6400U
#define AUDIO_DIRECT_DIAG_FADE_FRAMES 800U
#define AUDIO_DIRECT_DIAG_CHUNK_FRAMES 128U
#define AUDIO_DIRECT_DIAG_TONE_CHUNKS 50U
#define AUDIO_DIRECT_DIAG_DMA_DESC_COUNT 6U
#define AUDIO_DIRECT_DIAG_DMA_FRAMES 256U
#define AUDIO_DIRECT_DIAG_POSTROLL_FRAMES \
    (AUDIO_DIRECT_DIAG_DMA_DESC_COUNT * AUDIO_DIRECT_DIAG_DMA_FRAMES)
#define AUDIO_DIRECT_DIAG_POSTROLL_CHUNKS \
    (AUDIO_DIRECT_DIAG_POSTROLL_FRAMES / AUDIO_DIRECT_DIAG_CHUNK_FRAMES)

_Static_assert(AUDIO_DIRECT_DIAG_TONE_FRAMES ==
                   ((PLATFORM_AUDIO_SAMPLE_RATE_HZ *
                     AUDIO_DIRECT_DIAG_TONE_MS) / 1000U),
               "D2.3 tone duration must remain exact");
_Static_assert(AUDIO_DIRECT_DIAG_FADE_FRAMES ==
                   ((PLATFORM_AUDIO_SAMPLE_RATE_HZ *
                     AUDIO_DIRECT_DIAG_FADE_MS) / 1000U),
               "D2.3 fade duration must remain exact");
_Static_assert(AUDIO_DIRECT_DIAG_TONE_FRAMES ==
                   (AUDIO_DIRECT_DIAG_TONE_CHUNKS *
                    AUDIO_DIRECT_DIAG_CHUNK_FRAMES),
               "D2.3 tone must use exact full writes");
_Static_assert(AUDIO_DIRECT_DIAG_POSTROLL_FRAMES ==
                   (AUDIO_DIRECT_DIAG_POSTROLL_CHUNKS *
                    AUDIO_DIRECT_DIAG_CHUNK_FRAMES),
               "D2.3 postroll must cover the complete DMA ring");
_Static_assert(AUDIO_DIRECT_DIAG_CHUNK_FRAMES ==
                   PLATFORM_AUDIO_MAX_WRITE_FRAMES,
               "D2.3 writes must exercise the public maximum");

static const char *const TAG = "audio_direct_diag";
static esp_ldo_channel_handle_t s_ldo3;
static esp_ldo_channel_handle_t s_ldo4;
static platform_audio_t *s_audio;
static int16_t *s_tone;
static bool s_power_owned;
static const char *const s_authorization_id =
    "audio-direct-diag-d23-one-shot-authorization-2026-08-13";
static const char s_host_arm_nonce[] =
    "0c47666f7da543e93831e0dfdcb7fecd6bb9887f0d0d0c72ee4422826b573562";
static const char s_host_arm_line[] =
    "P4_AUDIO_D23_ARM "
    "audio-direct-diag-d23-one-shot-authorization-2026-08-13 "
    "0c47666f7da543e93831e0dfdcb7fecd6bb9887f0d0d0c72ee4422826b573562";
_Static_assert(sizeof(s_host_arm_nonce) - 1U ==
                   AUDIO_DIRECT_DIAG_HOST_ARM_NONCE_HEX_BYTES,
               "host-arm nonce must remain exact");
_Static_assert(sizeof(s_host_arm_line) <= AUDIO_DIRECT_DIAG_HOST_ARM_LINE_BYTES,
               "host-arm line must fit its bounded parser");

static void remember_first_error(esp_err_t candidate, esp_err_t *first_error)
{
    if (candidate != ESP_OK && *first_error == ESP_OK) {
        *first_error = candidate;
    }
}

static esp_err_t wait_for_host_arm(char *nonce_hex, size_t nonce_bytes)
{
    if (nonce_hex == NULL ||
        nonce_bytes != AUDIO_DIRECT_DIAG_HOST_ARM_NONCE_HEX_BYTES + 1U) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t discarded = 0U;
    while (esp_rom_output_rx_one_char(&discarded) == 0) {
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.3 WAIT_ARM auth=%s timeout_ms=%u rails=off "
             "amp_shutdown=1",
             s_authorization_id,
             (unsigned)AUDIO_DIRECT_DIAG_HOST_ARM_TIMEOUT_MS);

    const int64_t deadline_us = esp_timer_get_time() +
        ((int64_t)AUDIO_DIRECT_DIAG_HOST_ARM_TIMEOUT_MS * 1000LL);
    char line[AUDIO_DIRECT_DIAG_HOST_ARM_LINE_BYTES] = {0};
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
    memcpy(nonce_hex, s_host_arm_nonce,
           AUDIO_DIRECT_DIAG_HOST_ARM_NONCE_HEX_BYTES);
    nonce_hex[AUDIO_DIRECT_DIAG_HOST_ARM_NONCE_HEX_BYTES] = '\0';

    const int64_t duplicate_deadline_us = esp_timer_get_time() +
        ((int64_t)AUDIO_DIRECT_DIAG_HOST_ARM_DUPLICATE_GUARD_MS * 1000LL);
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
        .chan_id = AUDIO_DIRECT_DIAG_LDO3_CHANNEL,
        .voltage_mv = AUDIO_DIRECT_DIAG_LDO3_MV,
    };
    esp_err_t result = esp_ldo_acquire_channel(&ldo3_config, &s_ldo3);
    if (result != ESP_OK) {
        return result;
    }
    const esp_ldo_channel_config_t ldo4_config = {
        .chan_id = AUDIO_DIRECT_DIAG_LDO4_CHANNEL,
        .voltage_mv = AUDIO_DIRECT_DIAG_LDO4_MV,
    };
    result = esp_ldo_acquire_channel(&ldo4_config, &s_ldo4);
    if (result != ESP_OK) {
        const esp_err_t release_result = esp_ldo_release_channel(s_ldo3);
        if (release_result == ESP_OK) {
            s_ldo3 = NULL;
        }
        s_power_owned = s_ldo3 != NULL;
        if (release_result != ESP_OK) {
            return release_result;
        }
        return result;
    }
    s_power_owned = true;
    vTaskDelay(pdMS_TO_TICKS(AUDIO_DIRECT_DIAG_POWER_SETTLE_MS));
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

static esp_err_t cleanup_runtime(void)
{
    esp_err_t result = platform_audio_force_safe_shutdown();
    if (result != ESP_OK) {
        return result;
    }
    if (s_audio != NULL) {
        const esp_err_t stop_result = platform_audio_stop(s_audio);
        if (stop_result != ESP_OK && stop_result != ESP_ERR_INVALID_STATE) {
            remember_first_error(stop_result, &result);
        }
        remember_first_error(platform_audio_destroy(&s_audio), &result);
    }
    const esp_err_t recover_result = platform_audio_recover();
    remember_first_error(recover_result, &result);
    if (s_audio == NULL && recover_result == ESP_OK) {
        remember_first_error(release_board_power(), &result);
    }
    free(s_tone);
    s_tone = NULL;
    return result;
}

static void halt_safe(const char *stage, esp_err_t error)
{
    esp_err_t cleanup_result = cleanup_runtime();
    ESP_LOGE(TAG,
             "P4_AUDIO D2.3 HALT stage=%s error=%s cleanup=%s "
             "amp_shutdown_requested=1 rails_retained=%u",
             stage, esp_err_to_name(error), esp_err_to_name(cleanup_result),
             s_power_owned ? 1U : 0U);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
        cleanup_result = cleanup_runtime();
        ESP_LOGE(TAG,
                 "P4_AUDIO D2.3 CLEANUP_RETRY result=%s "
                 "amp_shutdown_requested=1 rails_retained=%u",
                 esp_err_to_name(cleanup_result),
                 s_power_owned ? 1U : 0U);
    }
}

static esp_err_t write_tone(void)
{
    for (size_t chunk = 0U;
         chunk < (size_t)AUDIO_DIRECT_DIAG_TONE_CHUNKS; ++chunk) {
        const int16_t *const samples =
            &s_tone[chunk * AUDIO_DIRECT_DIAG_CHUNK_FRAMES *
                    PLATFORM_AUDIO_CHANNEL_COUNT];
        const esp_err_t result = platform_audio_write_frames(
            s_audio, samples, AUDIO_DIRECT_DIAG_CHUNK_FRAMES);
        if (result != ESP_OK) {
            return result;
        }
    }
    int16_t zeros[AUDIO_DIRECT_DIAG_CHUNK_FRAMES *
                  PLATFORM_AUDIO_CHANNEL_COUNT];
    memset(zeros, 0, sizeof(zeros));
    for (size_t chunk = 0U;
         chunk < (size_t)AUDIO_DIRECT_DIAG_POSTROLL_CHUNKS; ++chunk) {
        const esp_err_t result = platform_audio_write_frames(
            s_audio, zeros, AUDIO_DIRECT_DIAG_CHUNK_FRAMES);
        if (result != ESP_OK) {
            return result;
        }
    }
    return ESP_OK;
}

void app_main(void)
{
    /* The first hardware-facing operation keeps the active-low amp disabled. */
    esp_err_t result = platform_audio_force_safe_shutdown();
    if (result != ESP_OK) {
        halt_safe("initial-safe", result);
    }
    /*
     * Keep rails off while esptool hands the already-open serial stream back
     * to the capture transport.  No acceptance marker is emitted until the
     * ROM command reader can no longer retain it in its SLIP response buffer.
     */
    vTaskDelay(pdMS_TO_TICKS(AUDIO_DIRECT_DIAG_SERIAL_ATTACH_MS));
    ESP_LOGI(TAG,
             "P4_AUDIO D2.3 SERIAL_ATTACH wait_ms=%u complete=1 rails=off "
             "amp_shutdown=1",
             (unsigned)AUDIO_DIRECT_DIAG_SERIAL_ATTACH_MS);
    char arm_nonce[AUDIO_DIRECT_DIAG_HOST_ARM_NONCE_HEX_BYTES + 1U] = {0};
    result = wait_for_host_arm(arm_nonce, sizeof(arm_nonce));
    if (result != ESP_OK) {
        halt_safe("host-arm", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.3 ARM_ACCEPTED auth=%s nonce=%s tx_count=1",
             s_authorization_id, arm_nonce);
    ESP_LOGI(TAG,
             "P4_AUDIO D2.3 START backend=platform_audio input=full-scale "
             "output_peak_cap=%u codec=none i2c=unused mclk=unused",
             (unsigned)PLATFORM_AUDIO_MAX_OUTPUT_PEAK);
    ESP_LOGI(TAG, "P4_AUDIO D2.3 SAFE gpio30=1 active_low=true");

    result = acquire_board_power();
    if (result != ESP_OK) {
        halt_safe("board-power", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.3 POWER_READY ldo3_mv=%u ldo4_mv=%u settle_ms=%u",
             (unsigned)AUDIO_DIRECT_DIAG_LDO3_MV,
             (unsigned)AUDIO_DIRECT_DIAG_LDO4_MV,
             (unsigned)AUDIO_DIRECT_DIAG_POWER_SETTLE_MS);

    const size_t sample_count =
        (size_t)AUDIO_DIRECT_DIAG_TONE_FRAMES *
        PLATFORM_AUDIO_CHANNEL_COUNT;
    s_tone = calloc(sample_count, sizeof(*s_tone));
    if (s_tone == NULL) {
        halt_safe("tone-allocate", ESP_ERR_NO_MEM);
    }
    const platform_audio_tone_config_t tone_config = {
        .sample_rate_hz = PLATFORM_AUDIO_SAMPLE_RATE_HZ,
        .frequency_hz = AUDIO_DIRECT_DIAG_TONE_HZ,
        .peak_amplitude = (uint16_t)INT16_MAX,
        .frame_count = AUDIO_DIRECT_DIAG_TONE_FRAMES,
        .fade_frames = AUDIO_DIRECT_DIAG_FADE_FRAMES,
    };
    if (!platform_audio_generate_quiet_tone(
            &tone_config, s_tone, sample_count)) {
        halt_safe("tone-generate", ESP_ERR_INVALID_ARG);
    }

    const platform_audio_config_t audio_config = {
        .control_bus = NULL,
        .sample_rate_hz = PLATFORM_AUDIO_SAMPLE_RATE_HZ,
        .volume_percent = PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT,
    };
    result = platform_audio_create(&audio_config, &s_audio);
    if (result != ESP_OK) {
        halt_safe("audio-create", result);
    }
    platform_audio_state_t state = PLATFORM_AUDIO_STATE_RUNNING;
    result = platform_audio_get_state(s_audio, &state);
    if (result != ESP_OK || state != PLATFORM_AUDIO_STATE_READY_MUTED) {
        halt_safe("created-state", result != ESP_OK ? result
                                                     : ESP_ERR_INVALID_STATE);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.3 BACKEND_READY state=ready-muted i2s=1 "
             "lrclk=21 bclk=22 dout=23 write_frames=%u timeout_ms=100",
             (unsigned)PLATFORM_AUDIO_MAX_WRITE_FRAMES);

    ESP_LOGI(TAG,
             "P4_AUDIO D2.3 CAPTURE_ARM wait_ms=%u amp_shutdown=1",
             (unsigned)AUDIO_DIRECT_DIAG_CAPTURE_ARM_MS);
    vTaskDelay(pdMS_TO_TICKS(AUDIO_DIRECT_DIAG_CAPTURE_ARM_MS));

    result = platform_audio_start(s_audio);
    if (result != ESP_OK) {
        halt_safe("audio-start", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.3 TONE_BEGIN hz=%u bounded_ms=%u "
             "input_peak=%u output_peak_cap=%u chunks=%u",
             (unsigned)AUDIO_DIRECT_DIAG_TONE_HZ,
             (unsigned)AUDIO_DIRECT_DIAG_TONE_MS, (unsigned)INT16_MAX,
             (unsigned)PLATFORM_AUDIO_MAX_OUTPUT_PEAK,
             (unsigned)AUDIO_DIRECT_DIAG_TONE_CHUNKS);
    result = write_tone();
    if (result != ESP_OK) {
        halt_safe("audio-write", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.3 POSTROLL frames=%u chunks=%u ring_covered=1",
             (unsigned)AUDIO_DIRECT_DIAG_POSTROLL_FRAMES,
             (unsigned)AUDIO_DIRECT_DIAG_POSTROLL_CHUNKS);

    result = platform_audio_stop(s_audio);
    if (result != ESP_OK) {
        halt_safe("audio-stop", result);
    }
    state = PLATFORM_AUDIO_STATE_RUNNING;
    result = platform_audio_get_state(s_audio, &state);
    if (result != ESP_OK || state != PLATFORM_AUDIO_STATE_READY_MUTED) {
        halt_safe("stopped-state", result != ESP_OK ? result
                                                     : ESP_ERR_INVALID_STATE);
    }
    result = platform_audio_destroy(&s_audio);
    if (result != ESP_OK) {
        halt_safe("audio-destroy", result);
    }
    result = platform_audio_recover();
    if (result != ESP_OK) {
        halt_safe("audio-recover", result);
    }
    free(s_tone);
    s_tone = NULL;
    result = release_board_power();
    if (result != ESP_OK) {
        halt_safe("power-release", result);
    }
    ESP_LOGI(TAG,
             "P4_AUDIO D2.3 PASS backend=platform_audio tone_count=1 "
             "bounded_ms=%u exact_writes=%u partial_writes=0 "
             "amp_shutdown=1 ldo3_released=1 ldo4_released=1",
             (unsigned)AUDIO_DIRECT_DIAG_TONE_MS,
             (unsigned)(AUDIO_DIRECT_DIAG_TONE_CHUNKS +
                        AUDIO_DIRECT_DIAG_POSTROLL_CHUNKS));
    for (;;) {
        ESP_LOGI(TAG,
                 "P4_AUDIO D2.3 HEARTBEAT amp_shutdown=1 rails_released=1");
        vTaskDelay(pdMS_TO_TICKS(5000U));
    }
}
