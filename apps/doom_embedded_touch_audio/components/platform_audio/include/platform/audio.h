// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOOM_TOUCH_AUDIO_PLATFORM_AUDIO_ADAPTER_H
#define DOOM_TOUCH_AUDIO_PLATFORM_AUDIO_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "platform_audio_factory/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_AUDIO_CHANNEL_COUNT PLATFORM_AUDIO_FACTORY_CHANNEL_COUNT
#define PLATFORM_AUDIO_BITS_PER_SAMPLE PLATFORM_AUDIO_FACTORY_BITS_PER_SAMPLE
#define PLATFORM_AUDIO_SAMPLE_RATE_HZ PLATFORM_AUDIO_FACTORY_SAMPLE_RATE_HZ
#define PLATFORM_AUDIO_MAX_WRITE_FRAMES \
    PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES
#define PLATFORM_AUDIO_MAX_OUTPUT_PEAK PLATFORM_AUDIO_FACTORY_MAX_OUTPUT_PEAK
#define PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT \
    PLATFORM_AUDIO_FACTORY_MAX_VOLUME_PERCENT

typedef platform_audio_factory_t platform_audio_t;
typedef platform_audio_factory_telemetry_t platform_audio_telemetry_t;

typedef struct {
    /** Must remain NULL. Factory audio has no I2C control client. */
    void *control_bus;
    uint32_t sample_rate_hz;
    uint8_t volume_percent;
} platform_audio_config_t;

typedef enum {
    PLATFORM_AUDIO_STATE_READY_MUTED = 0,
    PLATFORM_AUDIO_STATE_RUNNING,
    PLATFORM_AUDIO_STATE_FAILED_SAFE,
} platform_audio_state_t;

typedef struct {
    /** Saturating mutating/control/data adapter calls; snapshots excluded. */
    uint32_t invocations;
    uint32_t write_calls_succeeded;
    uint32_t frames_forwarded;
    uint32_t nonzero_frames_forwarded;
    uint32_t nonzero_samples_forwarded;
    uint16_t observed_absolute_peak;
    /** Proof latched only after backend start returned ESP_OK. */
    bool running_low_readback_proven_at_start;
    /** Last proven backend transition ended READY_MUTED or fully released. */
    bool ready_muted_zero_dma_proven;
} platform_audio_adapter_stats_t;

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
/** Thread-safe coherent snapshot; excluded from the mutating-call counter. */
esp_err_t platform_audio_get_telemetry(platform_audio_telemetry_t *out_telemetry);
esp_err_t platform_audio_destroy(platform_audio_t **audio);

/** Mutating/control/data backend invocations, excluding telemetry snapshots. */
uint32_t platform_audio_invocation_count(void);

/**
 * Take a coherent nonblocking snapshot without invoking the backend.
 * Returns an all-zero fail-closed snapshot if a mutating call is in progress.
 */
void platform_audio_adapter_get_stats(platform_audio_adapter_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif
