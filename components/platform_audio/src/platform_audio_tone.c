#include "platform/audio_tone.h"

#include <limits.h>
#include <stdint.h>

static bool tone_config_valid(const platform_audio_tone_config_t *config)
{
    if (config == NULL || config->sample_rate_hz == 0U ||
        config->frequency_hz == 0U || config->frame_count < 2U ||
        config->peak_amplitude == 0U ||
        config->peak_amplitude > (uint16_t)INT16_MAX ||
        config->frequency_hz >= (config->sample_rate_hz / 2U)) {
        return false;
    }
    if (config->fade_frames == 0U ||
        config->fade_frames > (config->frame_count / 2U)) {
        return false;
    }
    return true;
}

static uint32_t envelope_for_frame(
    size_t frame,
    size_t frame_count,
    size_t fade_frames
)
{
    if (frame < fade_frames) {
        return (uint32_t)frame;
    }
    const size_t frames_after = frame_count - 1U - frame;
    if (frames_after < fade_frames) {
        return (uint32_t)frames_after;
    }
    return (uint32_t)fade_frames;
}

bool platform_audio_generate_quiet_tone(
    const platform_audio_tone_config_t *config,
    int16_t *interleaved_stereo,
    size_t sample_capacity
)
{
    if (!tone_config_valid(config) || interleaved_stereo == NULL ||
        config->frame_count > (SIZE_MAX / 2U) ||
        sample_capacity < (config->frame_count * 2U) ||
        config->fade_frames > UINT32_MAX) {
        return false;
    }

    const uint32_t phase_step = (uint32_t)(
        ((uint64_t)config->frequency_hz << 32U) /
        (uint64_t)config->sample_rate_hz
    );
    uint32_t phase = 0U;
    for (size_t frame = 0U; frame < config->frame_count; ++frame) {
        const uint32_t position = phase >> 16U;
        const int32_t triangle = position < UINT32_C(32768)
            ? -INT32_C(32768) + (int32_t)(position * 2U)
            : INT32_C(98302) - (int32_t)(position * 2U);
        const int32_t scaled = (
            triangle * (int32_t)config->peak_amplitude
        ) / INT32_C(32768);
        const uint32_t envelope = envelope_for_frame(
            frame, config->frame_count, config->fade_frames
        );
        const int32_t faded = (int32_t)(
            ((int64_t)scaled * (int64_t)envelope) /
            (int64_t)config->fade_frames
        );
        const int16_t sample = (int16_t)faded;
        interleaved_stereo[frame * 2U] = sample;
        interleaved_stereo[(frame * 2U) + 1U] = sample;
        phase += phase_step;
    }
    return true;
}
