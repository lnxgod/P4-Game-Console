// SPDX-License-Identifier: MIT

#include "p4/audio.h"

#include <limits.h>
#include <string.h>

_Static_assert((int)P4_GAME_AUDIO_STREAM_BUFFER_FRAMES >=
                   (int)P4_GAME_MAX_AUDIO_STREAM_FRAMES,
               "audio FIFO must hold one maximum API stream block");
_Static_assert((int)P4_GAME_AUDIO_STREAM_BUFFER_FRAMES <= (int)UINT16_MAX,
               "audio FIFO stats expose a uint16_t queued-frame count");

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

bool p4_audio_mixer_submit_pcm16_stereo(
    p4_audio_mixer_t *mixer,
    const int16_t *interleaved_stereo,
    size_t frame_count)
{
    if (mixer == NULL) {
        return false;
    }
    if (interleaved_stereo == NULL || frame_count == 0U ||
        frame_count > P4_GAME_MAX_AUDIO_STREAM_FRAMES ||
        frame_count > P4_GAME_AUDIO_STREAM_BUFFER_FRAMES -
                          mixer->stream_queued_frames) {
        saturating_increment(&mixer->stream_blocks_rejected);
        return false;
    }
    for (size_t frame = 0U; frame < frame_count; ++frame) {
        const size_t destination =
            mixer->stream_write_frame * P4_GAME_AUDIO_CHANNEL_COUNT;
        const size_t source = frame * P4_GAME_AUDIO_CHANNEL_COUNT;
        mixer->stream_pcm[destination] = interleaved_stereo[source];
        mixer->stream_pcm[destination + 1U] =
            interleaved_stereo[source + 1U];
        mixer->stream_write_frame =
            (mixer->stream_write_frame + 1U) %
            P4_GAME_AUDIO_STREAM_BUFFER_FRAMES;
    }
    mixer->stream_queued_frames += frame_count;
    mixer->stream_active = true;
    saturating_increment(&mixer->stream_blocks_submitted);
    saturating_add(&mixer->stream_frames_submitted, frame_count);
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
    mixer->stream_read_frame = 0U;
    mixer->stream_write_frame = 0U;
    mixer->stream_queued_frames = 0U;
    mixer->stream_active = false;
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
        saturating_increment(&mixer->clipped_samples);
        return INT16_MAX;
    }
    if (sample < INT16_MIN) {
        saturating_increment(&mixer->clipped_samples);
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
        int32_t mixed_left = 0;
        int32_t mixed_right = 0;
        if (mixer->stream_queued_frames > 0U) {
            const size_t source =
                mixer->stream_read_frame * P4_GAME_AUDIO_CHANNEL_COUNT;
            mixed_left = mixer->stream_pcm[source];
            mixed_right = mixer->stream_pcm[source + 1U];
            mixer->stream_read_frame =
                (mixer->stream_read_frame + 1U) %
                P4_GAME_AUDIO_STREAM_BUFFER_FRAMES;
            --mixer->stream_queued_frames;
        } else if (mixer->stream_active) {
            saturating_increment(&mixer->stream_underrun_frames);
        }
        for (size_t i = 0U; i < P4_GAME_AUDIO_MAX_VOICES; ++i) {
            p4_audio_voice_t *const voice = &mixer->voices[i];
            if (!voice->active || voice->frames_remaining == 0U) {
                continue;
            }
            const int32_t sample = voice_sample(voice);
            mixed_left += sample;
            mixed_right += sample;
            voice->phase += voice->frequency_hz;
            if (voice->phase >= P4_GAME_AUDIO_SAMPLE_RATE_HZ) {
                voice->phase -= P4_GAME_AUDIO_SAMPLE_RATE_HZ;
            }
            --voice->frames_remaining;
            if (voice->frames_remaining == 0U) {
                voice->active = false;
            }
        }
        interleaved_stereo[frame * 2U] =
            clip_sample(mixer, mixed_left);
        interleaved_stereo[frame * 2U + 1U] =
            clip_sample(mixer, mixed_right);
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
        .stream_blocks_submitted = mixer->stream_blocks_submitted,
        .stream_frames_submitted = mixer->stream_frames_submitted,
        .stream_blocks_rejected = mixer->stream_blocks_rejected,
        .stream_underrun_frames = mixer->stream_underrun_frames,
        .frames_rendered = mixer->frames_rendered,
        .clipped_samples = mixer->clipped_samples,
        .stream_queued_frames = (uint16_t)mixer->stream_queued_frames,
        .active_voices = active,
        .stream_active = mixer->stream_active,
    };
}

bool p4_audio_mixer_service_play_tone(void *context,
                                      const p4_tone_t *tone)
{
    return p4_audio_mixer_play_tone((p4_audio_mixer_t *)context, tone);
}

bool p4_audio_mixer_service_submit_pcm16_stereo(
    void *context,
    const int16_t *interleaved_stereo,
    size_t frame_count)
{
    return p4_audio_mixer_submit_pcm16_stereo(
        (p4_audio_mixer_t *)context, interleaved_stereo, frame_count);
}

void p4_audio_mixer_service_stop(void *context)
{
    p4_audio_mixer_stop_all((p4_audio_mixer_t *)context);
}
