#include "platform_audio_policy.h"

bool platform_audio_amp_gpio_level(bool amplifier_enabled)
{
    return !amplifier_enabled;
}

bool platform_audio_sample_rate_supported(uint32_t sample_rate_hz)
{
    switch (sample_rate_hz) {
        case 8000U:
        case 11025U:
        case 12000U:
        case 16000U:
        case 22050U:
        case 24000U:
        case 32000U:
        case 44100U:
        case 48000U:
            return true;
        default:
            return false;
    }
}

bool platform_audio_bringup_volume_supported(uint8_t volume_percent)
{
    return volume_percent <= 10U;
}
