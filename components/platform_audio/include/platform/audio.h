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
#define PLATFORM_AUDIO_SAMPLE_RATE_HZ 16000U
#define PLATFORM_AUDIO_MAX_WRITE_FRAMES 128U
#define PLATFORM_AUDIO_MAX_OUTPUT_PEAK 512U
#define PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT 10U

typedef struct platform_audio platform_audio_t;

typedef struct {
    /** Reserved for a future backend. The direct-I2S backend requires NULL. */
    void *control_bus;
    uint32_t sample_rate_hz;
    uint8_t volume_percent;
} platform_audio_config_t;

typedef enum {
    PLATFORM_AUDIO_STATE_READY_MUTED = 0,
    PLATFORM_AUDIO_STATE_RUNNING,
} platform_audio_state_t;

/*
 * This component is a module-global singleton because it owns I2S1 and a
 * module-global failed-create recovery slot. All create, recover, handle, and
 * destroy calls must be externally serialized and must not be made from an
 * ISR, including calls that use different arguments or no handle. Only one
 * successfully created handle may exist; a second create returns
 * ESP_ERR_INVALID_STATE without muting or changing the live handle.
 */

/**
 * Immediately configure GPIO30 as the inactive-high amplifier shutdown line.
 *
 * Call this before creating any I2S objects. The function is also the
 * last-resort error path and is safe to call without a platform_audio_t
 * handle. It never acquires or changes the display-owned LDO3/LDO4 rails.
 */
esp_err_t platform_audio_force_safe_shutdown(void);

/**
 * Assert GPIO30 high and retry cleanup retained by a failed create.
 *
 * Call this after every failed platform_audio_create(), before the caller
 * releases display-owned LDO3/LDO4. ESP_OK proves that no failed-create I2S
 * owner remains. Any error means the rails and process lifetime must be
 * retained and recovery retried; this function never loses a driver handle.
 * Calling it while a live handle exists returns ESP_ERR_INVALID_STATE without
 * changing that handle or GPIO30.
 */
esp_err_t platform_audio_recover(void);

/**
 * Create the direct I2S1 playback path while the amplifier stays off.
 *
 * The only supported format is 16 kHz interleaved signed PCM16 stereo.
 * control_bus must be NULL and volume_percent must be in 1..10. On success,
 * I2S is enabled, all six 256-frame DMA descriptors contain exact zeros,
 * GPIO30 remains high, and the state is PLATFORM_AUDIO_STATE_READY_MUTED. The caller/display
 * owns already-on LDO3/LDO4; this component never touches either rail.
 * On failure, out_audio remains NULL; call platform_audio_recover() and do not
 * release LDO3/LDO4 until it returns ESP_OK.
 */
esp_err_t platform_audio_create(
    const platform_audio_config_t *config,
    platform_audio_t **out_audio
);

/** Re-prime the complete zero DMA ring, enable the amplifier, then settle. */
esp_err_t platform_audio_start(platform_audio_t *audio);

/**
 * Write 1..128 interleaved signed PCM16 stereo frames while running.
 *
 * Input is immutable. Samples are attenuated into component-owned staging;
 * at volume 10 the absolute hardware sample is capped at 512, and lower
 * volumes scale that cap proportionally. Each bounded driver write has a
 * finite 100 ms timeout and succeeds only when every requested byte was sent.
 */
esp_err_t platform_audio_write_frames(
    platform_audio_t *audio,
    const int16_t *interleaved_pcm,
    size_t frame_count
);

/**
 * Drive GPIO30 high first, stop I2S, overwrite the complete DMA ring with
 * zeros, resume zero clocks, and return to READY_MUTED.
 */
esp_err_t platform_audio_stop(platform_audio_t *audio);

/** Return the software lifecycle state. */
esp_err_t platform_audio_get_state(
    const platform_audio_t *audio,
    platform_audio_state_t *out_state
);

/**
 * Shut down and delete all owned audio objects.
 *
 * GPIO30 is driven high before I2S is stopped. If safe shutdown or I2S cleanup
 * cannot be confirmed, the handle and owned resources are retained so the
 * caller can retry; the pointer is cleared only after complete cleanup.
 */
esp_err_t platform_audio_destroy(platform_audio_t **audio);

#ifdef __cplusplus
}
#endif

#endif
