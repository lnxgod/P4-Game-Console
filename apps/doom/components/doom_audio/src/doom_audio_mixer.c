// SPDX-License-Identifier: GPL-2.0-or-later

#include "doom/audio_mixer.h"

#include <limits.h>
#include <string.h>

#define DOOM_AUDIO_DMX_HEADER_BYTES ((size_t)8U)
#define DOOM_AUDIO_DMX_SKIP_HEAD_SAMPLES ((size_t)16U)
#define DOOM_AUDIO_DMX_SKIP_TAIL_SAMPLES ((size_t)16U)
#define DOOM_AUDIO_DMX_MIN_DECLARED_SAMPLES ((size_t)49U)
#define DOOM_AUDIO_PHASE_ONE (UINT64_C(1) << 32U)
#define DOOM_AUDIO_GAIN_ONE UINT32_C(32767)
#define DOOM_AUDIO_SEPARATION_MAX UINT32_C(254)

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8U)
        | ((uint32_t)bytes[2] << 16U)
        | ((uint32_t)bytes[3] << 24U);
}

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

bool doom_audio_parse_dmx_lump(const uint8_t *lump,
                               size_t lump_bytes,
                               doom_audio_sample_t *out_sample)
{
    if (lump == NULL || out_sample == NULL ||
        lump_bytes < DOOM_AUDIO_DMX_HEADER_BYTES) {
        return false;
    }
    if (lump[0] != UINT8_C(0x03) || lump[1] != UINT8_C(0x00)) {
        return false;
    }

    const uint32_t sample_rate_hz = read_u16_le(&lump[2]);
    const uint32_t declared_samples = read_u32_le(&lump[4]);
    if (sample_rate_hz < DOOM_AUDIO_MIN_DMX_SAMPLE_RATE_HZ ||
        sample_rate_hz > DOOM_AUDIO_MAX_DMX_SAMPLE_RATE_HZ ||
        declared_samples < DOOM_AUDIO_DMX_MIN_DECLARED_SAMPLES ||
        declared_samples > DOOM_AUDIO_MAX_DMX_SAMPLE_BYTES ||
        (size_t)declared_samples > lump_bytes - DOOM_AUDIO_DMX_HEADER_BYTES) {
        return false;
    }

    const size_t playable_samples =
        (size_t)declared_samples - DOOM_AUDIO_DMX_SKIP_HEAD_SAMPLES -
        DOOM_AUDIO_DMX_SKIP_TAIL_SAMPLES;
    out_sample->samples = lump + DOOM_AUDIO_DMX_HEADER_BYTES +
                          DOOM_AUDIO_DMX_SKIP_HEAD_SAMPLES;
    out_sample->sample_count = playable_samples;
    out_sample->sample_rate_hz = sample_rate_hz;
    return true;
}

void doom_audio_mixer_init(doom_audio_mixer_t *mixer)
{
    if (mixer != NULL) {
        memset(mixer, 0, sizeof(*mixer));
    }
}

static uint16_t gain_q15(uint8_t volume, uint32_t pan_numerator)
{
    const uint32_t channel_gain =
        (pan_numerator * (uint32_t)volume) / UINT32_C(127);
    return (uint16_t)((channel_gain * DOOM_AUDIO_GAIN_ONE +
                       (DOOM_AUDIO_SEPARATION_MAX / 2U)) /
                      DOOM_AUDIO_SEPARATION_MAX);
}

static bool valid_voice(size_t voice_index)
{
    return voice_index < (size_t)DOOM_AUDIO_MAX_VOICES;
}

static void set_voice_gains(doom_audio_voice_t *voice,
                            uint8_t volume,
                            uint8_t separation)
{
    voice->left_gain_q15 = gain_q15(
        volume, DOOM_AUDIO_SEPARATION_MAX - (uint32_t)separation);
    voice->right_gain_q15 = gain_q15(volume, (uint32_t)separation);
}

bool doom_audio_mixer_start(doom_audio_mixer_t *mixer,
                            size_t voice_index,
                            const doom_audio_sample_t *sample,
                            uint8_t volume,
                            uint8_t separation)
{
    if (mixer == NULL || !valid_voice(voice_index) || sample == NULL ||
        sample->samples == NULL || sample->sample_count == 0U ||
        sample->sample_count > (size_t)DOOM_AUDIO_MAX_DMX_SAMPLE_BYTES ||
        sample->sample_rate_hz < DOOM_AUDIO_MIN_DMX_SAMPLE_RATE_HZ ||
        sample->sample_rate_hz > DOOM_AUDIO_MAX_DMX_SAMPLE_RATE_HZ ||
        volume > UINT8_C(127) || separation > UINT8_C(254)) {
        return false;
    }

    doom_audio_voice_t *const voice = &mixer->voices[voice_index];
    memset(voice, 0, sizeof(*voice));
    voice->sample = *sample;
    voice->step_q32 =
        ((uint64_t)sample->sample_rate_hz * DOOM_AUDIO_PHASE_ONE) /
        (uint64_t)DOOM_AUDIO_OUTPUT_RATE_HZ;
    if (voice->step_q32 == 0U) {
        memset(voice, 0, sizeof(*voice));
        return false;
    }
    set_voice_gains(voice, volume, separation);
    voice->active = true;
    return true;
}

bool doom_audio_mixer_stop(doom_audio_mixer_t *mixer, size_t voice_index)
{
    if (mixer == NULL || !valid_voice(voice_index)) {
        return false;
    }
    memset(&mixer->voices[voice_index], 0, sizeof(mixer->voices[voice_index]));
    return true;
}

bool doom_audio_mixer_update(doom_audio_mixer_t *mixer,
                             size_t voice_index,
                             uint8_t volume,
                             uint8_t separation)
{
    if (mixer == NULL || !valid_voice(voice_index) ||
        volume > UINT8_C(127) || separation > UINT8_C(254) ||
        !mixer->voices[voice_index].active) {
        return false;
    }
    set_voice_gains(&mixer->voices[voice_index], volume, separation);
    return true;
}

bool doom_audio_mixer_voice_active(const doom_audio_mixer_t *mixer,
                                   size_t voice_index)
{
    return mixer != NULL && valid_voice(voice_index) &&
           mixer->voices[voice_index].active;
}

static int16_t saturate_pcm16(int64_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

bool doom_audio_mixer_render(doom_audio_mixer_t *mixer,
                             int16_t *interleaved_pcm,
                             size_t frame_count)
{
    if (mixer == NULL || interleaved_pcm == NULL || frame_count == 0U ||
        frame_count > (SIZE_MAX / (size_t)DOOM_AUDIO_CHANNEL_COUNT)) {
        return false;
    }

    for (size_t frame = 0U; frame < frame_count; ++frame) {
        int64_t left = 0;
        int64_t right = 0;
        for (size_t index = 0U; index < (size_t)DOOM_AUDIO_MAX_VOICES;
             ++index) {
            doom_audio_voice_t *const voice = &mixer->voices[index];
            if (!voice->active) {
                continue;
            }
            const uint64_t source_index_q32 = voice->phase_q32 >> 32U;
            if (source_index_q32 >= (uint64_t)voice->sample.sample_count) {
                voice->active = false;
                continue;
            }
            const size_t source_index = (size_t)source_index_q32;
            const int32_t mono =
                ((int32_t)voice->sample.samples[source_index] - INT32_C(128)) *
                INT32_C(256);
            left += ((int64_t)mono * (int64_t)voice->left_gain_q15) /
                    (int64_t)DOOM_AUDIO_GAIN_ONE;
            right += ((int64_t)mono * (int64_t)voice->right_gain_q15) /
                     (int64_t)DOOM_AUDIO_GAIN_ONE;
            voice->phase_q32 += voice->step_q32;
        }
        interleaved_pcm[frame * (size_t)DOOM_AUDIO_CHANNEL_COUNT] =
            saturate_pcm16(left);
        interleaved_pcm[frame * (size_t)DOOM_AUDIO_CHANNEL_COUNT + 1U] =
            saturate_pcm16(right);
    }
    return true;
}
