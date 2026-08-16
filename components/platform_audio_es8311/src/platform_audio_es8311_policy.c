#include "platform_audio_es8311_policy.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

bool platform_audio_es8311_volume_supported(uint8_t volume_percent)
{
    return volume_percent >= UINT8_C(1) && volume_percent <= UINT8_C(10);
}

uint8_t platform_audio_es8311_codec_volume_percent(uint8_t volume_step)
{
    if (!platform_audio_es8311_volume_supported(volume_step)) {
        return UINT8_C(0);
    }
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    return (uint8_t)(volume_step * UINT8_C(10));
#else
    return volume_step;
#endif
}

uint16_t platform_audio_es8311_peak_for_volume(uint8_t volume_percent)
{
    if (!platform_audio_es8311_volume_supported(volume_percent)) {
        return UINT16_C(0);
    }
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    return UINT16_C(32768);
#else
    return (uint16_t)((UINT32_C(512) * (uint32_t)volume_percent) /
                      UINT32_C(10));
#endif
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

#if !defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
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
#endif

bool platform_audio_es8311_attenuate_pcm16(const int16_t *input,
                                           int16_t *output,
                                           size_t sample_count,
                                           uint8_t volume_percent)
{
    const uint16_t peak =
        platform_audio_es8311_peak_for_volume(volume_percent);
    if (input == NULL || output == NULL || sample_count == 0U || peak == 0U ||
        ranges_overlap(input, output, sample_count)) {
        return false;
    }
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    memcpy(output, input, sample_count * sizeof(*input));
#else
    for (size_t index = 0U; index < sample_count; ++index) {
        output[index] = attenuate_sample(input[index], peak);
    }
#endif
    return true;
}
