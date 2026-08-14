// SPDX-License-Identifier: MIT

#include "p4/audio.h"

#include <limits.h>
#include <string.h>

static void saturating_increment(uint32_t *value)
{
    if (*value != UINT32_MAX) {
        ++*value;
    }
}

static void saturating_add(uint32_t *value, size_t amount)
{
    const uint32_t bounded = amount > UINT32_MAX
        ? UINT32_MAX : (uint32_t)amount;
    if (UINT32_MAX - *value < bounded) {
        *value = UINT32_MAX;
    } else {
        *value += bounded;
    }
}

void p4_audio_mixer_init(p4_audio_mixer_t *mixer)
{
    if (mixer != NULL) {
        memset(mixer, 0, sizeof(*mixer));
    }
}

static bool tone_valid(const p4_tone_t *tone)
{
    return tone != NULL &&
        tone->frequency_hz >= 40U && tone->frequency_hz <= 4000U &&
        tone->duration_ms > 0U && tone->duration_ms <= 5000U &&
        tone->volume_step > 0U && tone->volume_step <= 10U &&
        tone->waveform >= P4_WAVE_SQUARE &&
        tone->waveform <= P4_WAVE_TRIANGLE;
}

bool p4_audio_mixer_play_tone(p4_audio_mixer_t *mixer,
                              const p4_tone_t *tone)
{
    if (mixer == NULL || !tone_valid(tone)) {
        return false;
    }
    size_t selected = P4_GAME_AUDIO_MAX_VOICES;
    for (size_t i = 0U; i < P4_GAME_AUDIO_MAX_VOICES; ++i) {
        if (!mixer->voices[i].active) {
            selected = i;
            break;
        }
    }
    if (selected == P4_GAME_AUDIO_MAX_VOICES) {
        selected = 0U;
        for (size_t i = 1U; i < P4_GAME_AUDIO_MAX_VOICES; ++i) {
            if (mixer->voices[i].frames_remaining <
                mixer->voices[selected].frames_remaining) {
                selected = i;
            }
        }
        saturating_increment(&mixer->voices_replaced);
    }
    const uint64_t product =
        (uint64_t)P4_GAME_AUDIO_SAMPLE_RATE_HZ * tone->duration_ms;
    uint32_t frames = (uint32_t)((product + UINT64_C(999)) / UINT64_C(1000));
    if (frames == 0U) {
        frames = 1U;
    }
    saturating_increment(&mixer->next_serial);
    mixer->voices[selected] = (p4_audio_voice_t){
        .active = true,
        .waveform = tone->waveform,
        .frequency_hz = tone->frequency_hz,
        .volume_step = tone->volume_step,
        .phase = 0U,
        .frames_remaining = frames,
        .serial = mixer->next_serial,
    };
    saturating_increment(&mixer->tones_started);
    return true;
}

void p4_audio_mixer_stop_all(p4_audio_mixer_t *mixer)
{
    if (mixer == NULL) {
        return;
    }
    for (size_t i = 0U; i < P4_GAME_AUDIO_MAX_VOICES; ++i) {
        mixer->voices[i].active = false;
        mixer->voices[i].frames_remaining = 0U;
    }
}

static int32_t voice_sample(const p4_audio_voice_t *voice)
{
    const int32_t amplitude = (int32_t)voice->volume_step * INT32_C(400);
    if (voice->waveform == P4_WAVE_SQUARE) {
        return voice->phase < P4_GAME_AUDIO_SAMPLE_RATE_HZ / 2U
            ? amplitude : -amplitude;
    }
    const uint32_t folded = voice->phase < P4_GAME_AUDIO_SAMPLE_RATE_HZ / 2U
        ? voice->phase : P4_GAME_AUDIO_SAMPLE_RATE_HZ - voice->phase;
    return (int32_t)(
        (INT64_C(4) * amplitude * (int64_t)folded) /
        P4_GAME_AUDIO_SAMPLE_RATE_HZ) - amplitude;
}

static int16_t clip_sample(p4_audio_mixer_t *mixer, int32_t sample)
{
    if (sample > INT16_MAX) {
        saturating_add(&mixer->clipped_samples, 2U);
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        saturating_add(&mixer->clipped_samples, 2U);
        return INT16_MIN;
    }
    return (int16_t)sample;
}

bool p4_audio_mixer_render(p4_audio_mixer_t *mixer,
                           int16_t *interleaved_stereo,
                           size_t frame_count)
{
    if (mixer == NULL || interleaved_stereo == NULL || frame_count == 0U ||
        frame_count > P4_GAME_AUDIO_MAX_RENDER_FRAMES) {
        return false;
    }
    for (size_t frame = 0U; frame < frame_count; ++frame) {
        int32_t mixed = 0;
        for (size_t i = 0U; i < P4_GAME_AUDIO_MAX_VOICES; ++i) {
            p4_audio_voice_t *const voice = &mixer->voices[i];
            if (!voice->active || voice->frames_remaining == 0U) {
                continue;
            }
            mixed += voice_sample(voice);
            voice->phase += voice->frequency_hz;
            if (voice->phase >= P4_GAME_AUDIO_SAMPLE_RATE_HZ) {
                voice->phase -= P4_GAME_AUDIO_SAMPLE_RATE_HZ;
            }
            --voice->frames_remaining;
            if (voice->frames_remaining == 0U) {
                voice->active = false;
            }
        }
        const int16_t sample = clip_sample(mixer, mixed);
        interleaved_stereo[frame * 2U] = sample;
        interleaved_stereo[frame * 2U + 1U] = sample;
    }
    saturating_add(&mixer->frames_rendered, frame_count);
    return true;
}

void p4_audio_mixer_get_stats(const p4_audio_mixer_t *mixer,
                              p4_audio_mixer_stats_t *out_stats)
{
    if (mixer == NULL || out_stats == NULL) {
        return;
    }
    uint8_t active = 0U;
    for (size_t i = 0U; i < P4_GAME_AUDIO_MAX_VOICES; ++i) {
        if (mixer->voices[i].active && active != UINT8_MAX) {
            ++active;
        }
    }
    *out_stats = (p4_audio_mixer_stats_t){
        .tones_started = mixer->tones_started,
        .voices_replaced = mixer->voices_replaced,
        .frames_rendered = mixer->frames_rendered,
        .clipped_samples = mixer->clipped_samples,
        .active_voices = active,
    };
}

bool p4_audio_mixer_service_play_tone(void *context,
                                      const p4_tone_t *tone)
{
    return p4_audio_mixer_play_tone((p4_audio_mixer_t *)context, tone);
}

void p4_audio_mixer_service_stop(void *context)
{
    p4_audio_mixer_stop_all((p4_audio_mixer_t *)context);
}
