#ifndef PLATFORM_AUDIO_ES8311_POLICY_H
#define PLATFORM_AUDIO_ES8311_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    PLATFORM_AUDIO_ES8311_I2S_CONTROLLER = 1,
    PLATFORM_AUDIO_ES8311_GPIO_LRCLK = 21,
    PLATFORM_AUDIO_ES8311_GPIO_BCLK = 22,
    PLATFORM_AUDIO_ES8311_GPIO_DOUT = 23,
    PLATFORM_AUDIO_ES8311_GPIO_MCLK = 24,
    PLATFORM_AUDIO_ES8311_GPIO_AMP_SHUTDOWN = 30,
    PLATFORM_AUDIO_ES8311_CODEC_ADDRESS_7BIT = 0x18,
    PLATFORM_AUDIO_ES8311_CODEC_ADDRESS_WIRE = 0x30,
    PLATFORM_AUDIO_ES8311_I2C_SPEED_HZ = 100000,
    PLATFORM_AUDIO_ES8311_I2C_TIMEOUT_MS = 50,
    PLATFORM_AUDIO_ES8311_WRITE_TIMEOUT_MS = 100,
    PLATFORM_AUDIO_ES8311_DMA_DESCRIPTOR_COUNT = 6,
    PLATFORM_AUDIO_ES8311_DMA_FRAMES_PER_DESCRIPTOR = 256,
    PLATFORM_AUDIO_ES8311_ZERO_PREROLL_FRAMES = 1536,
};

bool platform_audio_es8311_volume_supported(uint8_t volume_percent);

uint16_t platform_audio_es8311_peak_for_volume(uint8_t volume_percent);

bool platform_audio_es8311_attenuate_pcm16(
    const int16_t *input,
    int16_t *output,
    size_t sample_count,
    uint8_t volume_percent
);

#endif
