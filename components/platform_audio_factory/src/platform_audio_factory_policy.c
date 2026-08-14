#include "platform_audio_factory_policy.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

bool platform_audio_factory_amp_gpio_level(bool amplifier_enabled)
{
    return !amplifier_enabled;
}

bool platform_audio_factory_sample_rate_supported(uint32_t sample_rate_hz)
{
    return sample_rate_hz == UINT32_C(16000);
}

bool platform_audio_factory_volume_supported(uint8_t volume_percent)
{
    return volume_percent >= UINT8_C(1) && volume_percent <= UINT8_C(10);
}

uint16_t platform_audio_factory_peak_for_volume(uint8_t volume_percent)
{
    if (!platform_audio_factory_volume_supported(volume_percent)) {
        return UINT16_C(0);
    }
    return (uint16_t)(
        ((uint32_t)PLATFORM_AUDIO_FACTORY_POLICY_MAX_OUTPUT_ABS_MAGNITUDE *
         (uint32_t)volume_percent) /
                      UINT32_C(10));
}

static bool ranges_overlap(const int16_t *input,
                           int16_t *output,
                           size_t sample_count)
{
    if (sample_count > (SIZE_MAX / sizeof(*input))) {
        return true;
    }
    const size_t byte_count = sample_count * sizeof(*input);
    const uintptr_t input_begin = (uintptr_t)input;
    const uintptr_t output_begin = (uintptr_t)output;
    if (input_begin > UINTPTR_MAX - byte_count ||
        output_begin > UINTPTR_MAX - byte_count) {
        return true;
    }
    const uintptr_t input_end = input_begin + byte_count;
    const uintptr_t output_end = output_begin + byte_count;
    return input_begin < output_end && output_begin < input_end;
}

static int16_t attenuate_sample(int16_t input, uint16_t peak)
{
    if (input >= 0) {
        const int32_t scaled =
            ((int32_t)input * (int32_t)peak) / INT32_C(32767);
        return (int16_t)scaled;
    }
    const int32_t magnitude = -(int32_t)input;
    const int32_t scaled =
        (magnitude * (int32_t)peak) / INT32_C(32768);
    return (int16_t)-scaled;
}

bool platform_audio_factory_attenuate_pcm16(const int16_t *input,
                                             int16_t *output,
                                             size_t sample_count,
                                             uint8_t volume_percent)
{
    const uint16_t peak =
        platform_audio_factory_peak_for_volume(volume_percent);
    if (input == NULL || output == NULL || sample_count == 0U || peak == 0U ||
        ranges_overlap(input, output, sample_count)) {
        return false;
    }
    if (volume_percent == UINT8_C(10)) {
        memcpy(output, input, sample_count * sizeof(*output));
        return true;
    }
    for (size_t index = 0U; index < sample_count; ++index) {
        output[index] = attenuate_sample(input[index], peak);
    }
    return true;
}
