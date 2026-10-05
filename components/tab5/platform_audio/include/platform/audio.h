// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef P4_TAB5_PLATFORM_AUDIO_H
#define P4_TAB5_PLATFORM_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

/* GPIO30/PDM witness fields are legacy ABI fields and remain zero on Tab5. */
#define PLATFORM_AUDIO_CHANNEL_COUNT 2U
#define PLATFORM_AUDIO_BITS_PER_SAMPLE 16U
#define PLATFORM_AUDIO_SAMPLE_RATE_HZ 16000U
#define PLATFORM_AUDIO_MAX_WRITE_FRAMES 128U
#define PLATFORM_AUDIO_MAX_OUTPUT_PEAK 32768U
#define PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT 10U
typedef struct platform_audio platform_audio_t;
typedef enum {
    PLATFORM_AUDIO_STATE_READY_MUTED = 0,
    PLATFORM_AUDIO_STATE_RUNNING,
    PLATFORM_AUDIO_STATE_FAILED_SAFE,
} platform_audio_state_t;
typedef struct {
    uint32_t snapshot_sequence;

    uint32_t gpio30_high_attempts;
    uint32_t gpio30_high_successes;
    uint32_t gpio30_high_readback_successes;

    uint32_t pdm_create_successes;
    uint32_t pdm_enable_successes;
    bool pdm_created;
    bool pdm_enabled;

    uint32_t tx_create_successes;
    uint32_t tx_enable_successes;
    bool tx_created;
    bool tx_enabled;

    uint32_t zero_preload_frames;
    uint32_t gpio30_low_attempts;
    uint32_t gpio30_low_successes;
    uint32_t gpio30_low_initial_readback_successes;
    uint32_t measured_settle_us;
    uint32_t gpio30_low_second_readback_successes;

    platform_audio_state_t state;
    bool running;

    uint32_t write_successes;
    uint32_t write_failures;
    uint32_t frames_written;
    uint32_t samples_written;
    uint32_t nonzero_frames;
    uint32_t nonzero_samples;
    uint32_t maximum_absolute_magnitude;

    uint32_t rollback_attempts;
    uint32_t rollback_successes;
    uint32_t rollback_high_proofs;
    bool resources_retained;
    uint32_t resources_owned;
    bool codec_open;
    bool i2s_created;
    bool i2s_enabled;
} platform_audio_telemetry_t;

typedef struct {
    /** Optional borrowed Tab5 I2C handle; NULL selects the board-owned bus. */
    void *control_bus;
    uint32_t sample_rate_hz;
    uint8_t volume_percent;
} platform_audio_config_t;



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
esp_err_t platform_audio_set_volume(platform_audio_t *audio,
                                     uint8_t volume_step);
esp_err_t platform_audio_get_volume(const platform_audio_t *audio,
                                    uint8_t *out_volume_step);
esp_err_t platform_audio_stop(platform_audio_t *audio);
esp_err_t platform_audio_get_state(const platform_audio_t *audio,
                                   platform_audio_state_t *out_state);
/** Thread-safe coherent snapshot; excluded from the mutating-call counter. */
esp_err_t platform_audio_get_telemetry(platform_audio_telemetry_t *out_telemetry);
esp_err_t platform_audio_destroy(platform_audio_t **audio);

/** Mutating/control/data backend invocations, excluding telemetry snapshots. */
uint32_t platform_audio_invocation_count(void);

/**
 * Take a stable nonblocking snapshot without invoking the backend. While one
 * blocking I2S write is in flight, the snapshot publishes the last completed
 * write and excludes that unfinished invocation from the reported count.
 * Returns all zeros only if bounded lock-free publication retries are exhausted.
 */
void platform_audio_adapter_get_stats(platform_audio_adapter_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif
