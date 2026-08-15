// SPDX-License-Identifier: MIT

#ifndef P4_OLIMEX_PLATFORM_AUDIO_H
#define P4_OLIMEX_PLATFORM_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_AUDIO_CHANNEL_COUNT 2U
#define PLATFORM_AUDIO_BITS_PER_SAMPLE 16U
#define PLATFORM_AUDIO_SAMPLE_RATE_HZ 16000U
#define PLATFORM_AUDIO_MAX_WRITE_FRAMES 128U
#define PLATFORM_AUDIO_MAX_OUTPUT_PEAK 32768U
#define PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT 10U

typedef struct platform_audio platform_audio_t;

typedef struct {
    /** Reserved for platform services; applications must pass NULL. */
    void *control_bus;
    uint32_t sample_rate_hz;
    /** Console volume step 1..10. */
    uint8_t volume_percent;
} platform_audio_config_t;

typedef enum {
    PLATFORM_AUDIO_STATE_READY_MUTED = 0,
    PLATFORM_AUDIO_STATE_RUNNING,
    PLATFORM_AUDIO_STATE_FAILED_SAFE,
} platform_audio_state_t;

typedef struct {
    uint32_t snapshot_sequence;
    platform_audio_state_t state;
    bool running;
    bool codec_open;
    bool i2s_created;
    bool i2s_enabled;
    bool resources_retained;
    uint32_t resources_owned;
    uint32_t write_successes;
    uint32_t write_failures;
    uint32_t frames_written;
    uint32_t nonzero_frames;
    uint32_t maximum_absolute_magnitude;
} platform_audio_telemetry_t;

esp_err_t platform_audio_force_safe_shutdown(void);
esp_err_t platform_audio_recover(void);
esp_err_t platform_audio_create(const platform_audio_config_t *config,
                                platform_audio_t **out_audio);
esp_err_t platform_audio_start(platform_audio_t *audio);
esp_err_t platform_audio_write_frames(platform_audio_t *audio,
                                      const int16_t *interleaved_pcm,
                                      size_t frame_count);
esp_err_t platform_audio_stop(platform_audio_t *audio);
esp_err_t platform_audio_get_state(const platform_audio_t *audio,
                                   platform_audio_state_t *out_state);
esp_err_t platform_audio_get_telemetry(platform_audio_telemetry_t *out);
esp_err_t platform_audio_destroy(platform_audio_t **audio);

#ifdef __cplusplus
}
#endif

#endif
