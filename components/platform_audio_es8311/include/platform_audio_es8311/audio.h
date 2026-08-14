#ifndef PLATFORM_AUDIO_ES8311_AUDIO_H
#define PLATFORM_AUDIO_ES8311_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_AUDIO_ES8311_CHANNEL_COUNT 2U
#define PLATFORM_AUDIO_ES8311_BITS_PER_SAMPLE 16U
#define PLATFORM_AUDIO_ES8311_SAMPLE_RATE_HZ 16000U
#define PLATFORM_AUDIO_ES8311_MCLK_MULTIPLE 256U
#define PLATFORM_AUDIO_ES8311_MAX_WRITE_FRAMES 128U
#define PLATFORM_AUDIO_ES8311_MAX_OUTPUT_PEAK 512U
#define PLATFORM_AUDIO_ES8311_MAX_VOLUME_PERCENT 10U
#define PLATFORM_AUDIO_ES8311_STARTUP_ZERO_MS 350U

typedef struct platform_audio_es8311 platform_audio_es8311_t;

typedef struct {
    /**
     * Borrowed ESP-IDF i2c_master_bus_handle_t shared with touch. The audio
     * component adds and removes only its own address-0x18 device; it never
     * creates or deletes the bus.
     */
    void *control_bus;
    uint32_t sample_rate_hz;
    uint8_t volume_percent;
} platform_audio_es8311_config_t;

typedef enum {
    PLATFORM_AUDIO_ES8311_STATE_READY_MUTED = 0,
    PLATFORM_AUDIO_ES8311_STATE_RUNNING,
    /** GPIO30 was requested high, but a complete muted rollback is unproven. */
    PLATFORM_AUDIO_ES8311_STATE_FAILED_SAFE,
} platform_audio_es8311_state_t;

/*
 * This component is a task-context singleton because it owns I2S1 and keeps a
 * failed-create recovery slot. Serialize every API call externally. No API is
 * ISR-safe. The borrowed I2C bus may still be used by other bus clients.
 */

/** Assert GPIO30 high before any codec, clock, or bus-side audio operation. */
esp_err_t platform_audio_es8311_force_safe_shutdown(void);

/**
 * Retry cleanup retained by a failed create. ESP_OK proves that this component
 * retains neither an I2S channel nor an ES8311 device on the borrowed bus.
 */
esp_err_t platform_audio_es8311_recover(void);

/**
 * Probe address 0x18, then create the MCLK/I2S/ES8311 path while GPIO30 stays
 * high. The exact supported format is 16 kHz signed PCM16 stereo. A missing
 * codec is a hard error; this component has no direct-I2S fallback.
 */
esp_err_t platform_audio_es8311_create(
    const platform_audio_es8311_config_t *config,
    platform_audio_es8311_t **out_audio
);

/**
 * Verify codec clocks/mute/volume, enable GPIO30 low, transmit only zeros for
 * at least 350 ms, verify again, and only then unmute the ES8311.
 */
esp_err_t platform_audio_es8311_start(platform_audio_es8311_t *audio);

/**
 * Write 1..128 frames. Input remains immutable; component-owned staging caps
 * the PCM peak at 512 before the separately bounded codec volume is applied.
 */
esp_err_t platform_audio_es8311_write_frames(
    platform_audio_es8311_t *audio,
    const int16_t *interleaved_pcm,
    size_t frame_count
);

/** Mute and verify the codec, then drive GPIO30 high and prime exact zeros. */
esp_err_t platform_audio_es8311_stop(platform_audio_es8311_t *audio);

esp_err_t platform_audio_es8311_get_state(
    const platform_audio_es8311_t *audio,
    platform_audio_es8311_state_t *out_state
);

/**
 * Shut down and release all owned objects without touching the borrowed bus.
 * The pointer is retained if I2S or the address-0x18 bus device cannot be
 * released, allowing a later retry without losing ownership.
 */
esp_err_t platform_audio_es8311_destroy(
    platform_audio_es8311_t **audio
);

#ifdef __cplusplus
}
#endif

#endif
