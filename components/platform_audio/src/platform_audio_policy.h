#ifndef PLATFORM_AUDIO_POLICY_H
#define PLATFORM_AUDIO_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    PLATFORM_AUDIO_I2S_CONTROLLER = 1,
    PLATFORM_AUDIO_GPIO_LRCLK = 21,
    PLATFORM_AUDIO_GPIO_BCLK = 22,
    PLATFORM_AUDIO_GPIO_DOUT = 23,
    PLATFORM_AUDIO_GPIO_AMP_SHUTDOWN = 30,
    PLATFORM_AUDIO_DMA_DESCRIPTOR_COUNT = 6,
    PLATFORM_AUDIO_DMA_FRAMES_PER_DESCRIPTOR = 256,
    PLATFORM_AUDIO_ZERO_PREROLL_FRAMES = 1536,
    PLATFORM_AUDIO_WRITE_TIMEOUT_MS = 100,
};

/** Factory-population amplifier control is active-low. */
bool platform_audio_amp_gpio_level(bool amplifier_enabled);

bool platform_audio_sample_rate_supported(uint32_t sample_rate_hz);

bool platform_audio_bringup_volume_supported(uint8_t volume_percent);

/** Peak sent to I2S for a validated 1..10 volume. */
uint16_t platform_audio_peak_for_volume(uint8_t volume_percent);

/**
 * Attenuate immutable PCM16 into disjoint, caller-owned output storage.
 * Returns false for invalid volume, NULL, zero length, or overlapping ranges.
 */
bool platform_audio_attenuate_pcm16(
    const int16_t *input,
    int16_t *output,
    size_t sample_count,
    uint8_t volume_percent
);

#endif
