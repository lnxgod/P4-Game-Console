#ifndef PLATFORM_AUDIO_FACTORY_AUDIO_H
#define PLATFORM_AUDIO_FACTORY_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_AUDIO_FACTORY_CHANNEL_COUNT 2U
#define PLATFORM_AUDIO_FACTORY_BITS_PER_SAMPLE 16U
#define PLATFORM_AUDIO_FACTORY_SAMPLE_RATE_HZ 16000U
#define PLATFORM_AUDIO_FACTORY_PDM_CLOCK_HZ 1024000U
#define PLATFORM_AUDIO_FACTORY_MAX_WRITE_FRAMES 128U
#define PLATFORM_AUDIO_FACTORY_MAX_OUTPUT_ABS_MAGNITUDE 32768U
/* Compatibility alias; this is an absolute magnitude, not int16_t max. */
#define PLATFORM_AUDIO_FACTORY_MAX_OUTPUT_PEAK \
    PLATFORM_AUDIO_FACTORY_MAX_OUTPUT_ABS_MAGNITUDE
#define PLATFORM_AUDIO_FACTORY_MAX_VOLUME_PERCENT 10U
#define PLATFORM_AUDIO_FACTORY_STARTUP_ZERO_MS 350U

typedef struct platform_audio_factory platform_audio_factory_t;

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t volume_percent;
} platform_audio_factory_config_t;

typedef enum {
    PLATFORM_AUDIO_FACTORY_STATE_READY_MUTED = 0,
    PLATFORM_AUDIO_FACTORY_STATE_RUNNING,
    /** A complete amplifier-off, zero-DMA rollback is not proven. */
    PLATFORM_AUDIO_FACTORY_STATE_FAILED_SAFE,
} platform_audio_factory_state_t;

/**
 * Thread-safe task-context snapshot of the complete factory-audio lifecycle.
 *
 * GPIO request counters count physical gpio_set_level attempts.  A safe-high
 * operation normally accounts for two: the high latch before configuring the
 * pad and the verified high rewrite after input/output mode is active. Startup
 * low counters and zero_preload_frames describe the most recent start attempt;
 * every other counter is cumulative since boot.  `resources_retained` means a
 * failed cleanup deliberately kept at least one owned channel for recovery.
 */
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

    platform_audio_factory_state_t state;
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
} platform_audio_factory_telemetry_t;

/*
 * This component is a task-context singleton because it owns PDM RX I2S0,
 * speaker TX I2S1, and a failed-create recovery slot. Serialize every mutating
 * API call externally. The telemetry snapshot may run concurrently with those
 * calls. No API is ISR-safe.
 */

/** Latch GPIO30 high, configure input/output readback, and prove it is high. */
esp_err_t platform_audio_factory_force_safe_shutdown(void);

/**
 * Retry cleanup retained by a failed create. ESP_OK proves that this component
 * retains no I2S channel. This API never touches an I2C bus or codec.
 */
esp_err_t platform_audio_factory_recover(void);

/**
 * Reproduce Elecrow's complete factory audio initializer while GPIO30 stays
 * high: create and enable PDM RX I2S0 first (16 kHz mono DSR_8S, GPIO24 clock,
 * GPIO26 input), then create speaker TX I2S1 (16 kHz signed PCM16 stereo on
 * GPIO21/22/23, MCLK unused). Microphone data is never consumed. There is no
 * ES8311 probe, external codec transaction, or automatic fallback.
 */
esp_err_t platform_audio_factory_create(
    const platform_audio_factory_config_t *config,
    platform_audio_factory_t **out_audio
);

/**
 * Re-prime the complete DMA ring with exact zeros, prove GPIO30 low, keep zero
 * clocks flowing for at least 350 ms, and re-prove low before entering RUNNING.
 */
esp_err_t platform_audio_factory_start(platform_audio_factory_t *audio);

/**
 * Write 1..128 frames. Input remains immutable; volume step 10/10 preserves
 * the complete signed PCM16 range [-32768, 32767] bit-for-bit without
 * amplification (maximum absolute magnitude 32768), while lower steps
 * attenuate proportionally in component-owned staging.
 */
esp_err_t platform_audio_factory_write_frames(
    platform_audio_factory_t *audio,
    const int16_t *interleaved_pcm,
    size_t frame_count
);

/**
 * Prove GPIO30 high, overwrite the complete DMA ring with zeros, and return to
 * READY_MUTED. FAILED_SAFE may call stop again to retry the exact rollback.
 */
esp_err_t platform_audio_factory_stop(platform_audio_factory_t *audio);

esp_err_t platform_audio_factory_get_state(
    const platform_audio_factory_t *audio,
    platform_audio_factory_state_t *out_state
);

/**
 * Obtain one coherent, thread-safe telemetry snapshot without taking the
 * caller's audio lock. This read-only observability call is outside the
 * serialized mutating/control/data invocation count. It is task-context only
 * and never touches hardware.
 */
esp_err_t platform_audio_factory_get_telemetry(
    platform_audio_factory_telemetry_t *out_telemetry
);

/**
 * Shut down and release I2S. On any unproven shutdown or cleanup, ownership and
 * the caller's pointer are retained so destroy can be retried.
 */
esp_err_t platform_audio_factory_destroy(platform_audio_factory_t **audio);

#ifdef __cplusplus
}
#endif

#endif
