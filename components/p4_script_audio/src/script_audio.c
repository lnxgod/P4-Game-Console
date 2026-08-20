// SPDX-License-Identifier: MIT

#include "p4/script_audio.h"

#include <limits.h>
#include <string.h>

static int32_t voice_sample(p4_script_audio_voice_t *voice)
{
    int32_t sample;
    switch ((p4_lua_waveform_t)voice->waveform) {
    case P4_LUA_WAVE_TRIANGLE: {
        const uint32_t position = voice->phase >> 16U;
        sample = position < 32768U
            ? -32767 + (int32_t)(position * 2U)
            : 32767 - (int32_t)((position - 32768U) * 2U);
        break;
    }
    case P4_LUA_WAVE_SAW:
        sample = (int32_t)(voice->phase >> 16U) - 32768;
        break;
    case P4_LUA_WAVE_NOISE:
        voice->noise_state ^= voice->noise_state << 13U;
        voice->noise_state ^= voice->noise_state >> 17U;
        voice->noise_state ^= voice->noise_state << 5U;
        sample = (voice->noise_state & 1U) != 0U ? 32767 : -32767;
        break;
    case P4_LUA_WAVE_SQUARE:
    default:
        sample = (voice->phase & UINT32_C(0x80000000)) != 0U
            ? 32767 : -32767;
        break;
    }
    voice->phase += voice->phase_step;
    return sample * voice->volume / 255;
}

void p4_script_audio_init(p4_script_audio_t *audio)
{
    if (audio != NULL) {
        memset(audio, 0, sizeof(*audio));
    }
}

bool p4_script_audio_play(
    p4_script_audio_t *audio,
    uint8_t channel,
    uint16_t frequency_hz,
    uint16_t duration_ticks,
    uint8_t volume,
    p4_lua_waveform_t waveform)
{
    if (audio == NULL || channel == 0U ||
        channel > P4_SCRIPT_AUDIO_CHANNEL_COUNT ||
        frequency_hz < 40U || frequency_hz > 4000U ||
        duration_ticks == 0U || duration_ticks > 600U ||
        waveform < P4_LUA_WAVE_SQUARE || waveform > P4_LUA_WAVE_NOISE) {
        return false;
    }
    p4_script_audio_voice_t *voice = &audio->voices[channel - 1U];
    if (voice->active && audio->tones_replaced != UINT32_MAX) {
        ++audio->tones_replaced;
    }
    const uint64_t duration_frames =
        (uint64_t)duration_ticks * P4_SCRIPT_AUDIO_SAMPLE_RATE_HZ /
            P4_SCRIPT_TICK_HZ;
    voice->phase = 0U;
    voice->phase_step = (uint32_t)(
        (uint64_t)frequency_hz * UINT64_C(0x100000000) /
            P4_SCRIPT_AUDIO_SAMPLE_RATE_HZ);
    voice->frames_remaining = (uint32_t)duration_frames;
    voice->noise_state = UINT32_C(0x9e3779b9) ^
        ((uint32_t)channel << 24U) ^ frequency_hz;
    voice->volume = volume;
    voice->waveform = (uint8_t)waveform;
    voice->active = true;
    if (audio->tones_started != UINT32_MAX) {
        ++audio->tones_started;
    }
    return true;
}

void p4_script_audio_stop(p4_script_audio_t *audio, uint8_t channel)
{
    if (audio != NULL && channel != 0U &&
        channel <= P4_SCRIPT_AUDIO_CHANNEL_COUNT) {
        memset(&audio->voices[channel - 1U], 0,
               sizeof(audio->voices[channel - 1U]));
    }
}

void p4_script_audio_stop_all(p4_script_audio_t *audio)
{
    if (audio != NULL) {
        memset(audio->voices, 0, sizeof(audio->voices));
    }
}

bool p4_script_audio_render_stereo(
    p4_script_audio_t *audio,
    int16_t *interleaved_stereo,
    size_t frame_count)
{
    if (audio == NULL || interleaved_stereo == NULL || frame_count == 0U ||
        frame_count > 1024U) {
        return false;
    }
    for (size_t frame = 0U; frame < frame_count; ++frame) {
        int32_t mixed = 0;
        for (size_t channel = 0U;
             channel < P4_SCRIPT_AUDIO_CHANNEL_COUNT; ++channel) {
            p4_script_audio_voice_t *voice = &audio->voices[channel];
            if (!voice->active) {
                continue;
            }
            mixed += voice_sample(voice) / P4_SCRIPT_AUDIO_CHANNEL_COUNT;
            if (--voice->frames_remaining == 0U) {
                voice->active = false;
            }
        }
        if (mixed > INT16_MAX) {
            mixed = INT16_MAX;
            if (audio->clipped_samples != UINT32_MAX) {
                ++audio->clipped_samples;
            }
        } else if (mixed < INT16_MIN) {
            mixed = INT16_MIN;
            if (audio->clipped_samples != UINT32_MAX) {
                ++audio->clipped_samples;
            }
        }
        const int16_t sample = (int16_t)mixed;
        interleaved_stereo[frame * 2U] = sample;
        interleaved_stereo[frame * 2U + 1U] = sample;
    }
    if (audio->frames_rendered <= UINT32_MAX - frame_count) {
        audio->frames_rendered += (uint32_t)frame_count;
    } else {
        audio->frames_rendered = UINT32_MAX;
    }
    return true;
}
