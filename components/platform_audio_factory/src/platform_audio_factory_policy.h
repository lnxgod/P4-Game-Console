#ifndef PLATFORM_AUDIO_FACTORY_POLICY_H
#define PLATFORM_AUDIO_FACTORY_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    PLATFORM_AUDIO_FACTORY_PDM_I2S_CONTROLLER = 0,
    PLATFORM_AUDIO_FACTORY_PDM_GPIO_CLK = 24,
    PLATFORM_AUDIO_FACTORY_PDM_GPIO_DIN = 26,
    PLATFORM_AUDIO_FACTORY_PDM_DOWNSAMPLE_FACTOR = 8,
    PLATFORM_AUDIO_FACTORY_PDM_BCLK_DIV = 8,
    PLATFORM_AUDIO_FACTORY_TX_I2S_CONTROLLER = 1,
    /* Compatibility name for the speaker-TX controller. */
    PLATFORM_AUDIO_FACTORY_I2S_CONTROLLER =
        PLATFORM_AUDIO_FACTORY_TX_I2S_CONTROLLER,
    PLATFORM_AUDIO_FACTORY_GPIO_LRCLK = 21,
    PLATFORM_AUDIO_FACTORY_GPIO_BCLK = 22,
    PLATFORM_AUDIO_FACTORY_GPIO_DOUT = 23,
    PLATFORM_AUDIO_FACTORY_GPIO_AMP_SHUTDOWN = 30,
    PLATFORM_AUDIO_FACTORY_DMA_DESCRIPTOR_COUNT = 6,
    PLATFORM_AUDIO_FACTORY_DMA_FRAMES_PER_DESCRIPTOR = 256,
    PLATFORM_AUDIO_FACTORY_ZERO_PREROLL_FRAMES = 1536,
    PLATFORM_AUDIO_FACTORY_WRITE_TIMEOUT_MS = 100,
    PLATFORM_AUDIO_FACTORY_POLICY_MAX_OUTPUT_ABS_MAGNITUDE = 32768,
    PLATFORM_AUDIO_FACTORY_POLICY_MAX_OUTPUT_PEAK =
        PLATFORM_AUDIO_FACTORY_POLICY_MAX_OUTPUT_ABS_MAGNITUDE,
};

bool platform_audio_factory_amp_gpio_level(bool amplifier_enabled);
bool platform_audio_factory_sample_rate_supported(uint32_t sample_rate_hz);
bool platform_audio_factory_volume_supported(uint8_t volume_percent);
uint16_t platform_audio_factory_peak_for_volume(uint8_t volume_percent);

bool platform_audio_factory_attenuate_pcm16(
    const int16_t *input,
    int16_t *output,
    size_t sample_count,
    uint8_t volume_percent
);

#endif
