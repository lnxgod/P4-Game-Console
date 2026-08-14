#ifndef PLATFORM_AUDIO_H
#define PLATFORM_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_AUDIO_CHANNEL_COUNT 2U
#define PLATFORM_AUDIO_BITS_PER_SAMPLE 16U
#define PLATFORM_AUDIO_MCLK_MULTIPLE 256U
#define PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT 10U

typedef struct platform_audio platform_audio_t;

typedef struct {
    /** Borrowed ESP-IDF i2c_master_bus_handle_t; never deleted by audio. */
    void *control_bus;
    uint32_t sample_rate_hz;
    uint8_t volume_percent;
} platform_audio_config_t;

typedef enum {
    PLATFORM_AUDIO_STATE_READY_MUTED = 0,
    PLATFORM_AUDIO_STATE_RUNNING,
} platform_audio_state_t;

/*
 * A platform_audio_t is a single-owner task object. Calls using the same
 * handle must be externally serialized and must not be made from an ISR.
 */

/**
 * Immediately configure GPIO30 as the inactive-high NS4263B shutdown line.
 *
 * Call this before creating the shared I2C bus or any I2S/codec objects. The
 * function is also the last-resort error path and is safe to call without a
 * platform_audio_t handle.
 */
esp_err_t platform_audio_force_safe_shutdown(void);

/**
 * Create the I2S1/ES8311 playback path while the external amplifier stays off.
 *
 * The caller owns the shared I2C bus. On success the codec is open, muted, and
 * clocked, a zero preroll has been submitted, and GPIO30 remains high.
 */
esp_err_t platform_audio_create(
    const platform_audio_config_t *config,
    platform_audio_t **out_audio
);

/** Enable the active-low amplifier, settle it while muted, then unmute. */
esp_err_t platform_audio_start(platform_audio_t *audio);

/** Write interleaved signed PCM16 stereo frames while running. */
esp_err_t platform_audio_write_frames(
    platform_audio_t *audio,
    int16_t *interleaved_pcm,
    size_t frame_count
);

/** Mute the codec first, then return the active-low amplifier to shutdown. */
esp_err_t platform_audio_stop(platform_audio_t *audio);

/** Return the software lifecycle state. */
esp_err_t platform_audio_get_state(
    const platform_audio_t *audio,
    platform_audio_state_t *out_state
);

/**
 * Shut down and delete all owned audio objects. The borrowed I2C bus remains
 * the caller's responsibility. The pointer is cleared even if cleanup fails.
 */
esp_err_t platform_audio_destroy(platform_audio_t **audio);

#ifdef __cplusplus
}
#endif

#endif
