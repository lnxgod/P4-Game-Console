#ifndef PLATFORM_AUDIO_TONE_H
#define PLATFORM_AUDIO_TONE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate_hz;
    uint32_t frequency_hz;
    uint16_t peak_amplitude;
    size_t frame_count;
    size_t fade_frames;
} platform_audio_tone_config_t;

/**
 * Generate bounded, DC-centered PCM16 stereo triangle-wave samples.
 *
 * Both channels receive the same value. The first and last sample are zero and
 * a linear fade is applied at both ends. No floating-point or libm dependency
 * is used, so the diagnostic waveform is deterministic on host and ESP32-P4.
 */
bool platform_audio_generate_quiet_tone(
    const platform_audio_tone_config_t *config,
    int16_t *interleaved_stereo,
    size_t sample_capacity
);

#ifdef __cplusplus
}
#endif

#endif
