#ifndef PLATFORM_AUDIO_POLICY_H
#define PLATFORM_AUDIO_POLICY_H

#include <stdbool.h>
#include <stdint.h>

enum {
    PLATFORM_AUDIO_I2S_CONTROLLER = 1,
    PLATFORM_AUDIO_GPIO_LRCLK = 21,
    PLATFORM_AUDIO_GPIO_BCLK = 22,
    PLATFORM_AUDIO_GPIO_DOUT = 23,
    PLATFORM_AUDIO_GPIO_MCLK = 24,
    PLATFORM_AUDIO_GPIO_AMP_SHUTDOWN = 30,
    PLATFORM_AUDIO_GPIO_I2C_SDA = 45,
    PLATFORM_AUDIO_GPIO_I2C_SCL = 46,
    PLATFORM_AUDIO_ES8311_WIRE_ADDRESS = 0x30,
    PLATFORM_AUDIO_ES8311_7BIT_ADDRESS = 0x18,
};

/** NS4263B control is active-low: false enables, true shuts down. */
bool platform_audio_amp_gpio_level(bool amplifier_enabled);

bool platform_audio_sample_rate_supported(uint32_t sample_rate_hz);

bool platform_audio_bringup_volume_supported(uint8_t volume_percent);

#endif
