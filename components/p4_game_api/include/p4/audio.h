// SPDX-License-Identifier: MIT

#ifndef P4_GAME_API_AUDIO_H
#define P4_GAME_API_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_GAME_AUDIO_SAMPLE_RATE_HZ = 16000,
    P4_GAME_AUDIO_CHANNEL_COUNT = 2,
    P4_GAME_AUDIO_MAX_VOICES = 8,
    P4_GAME_AUDIO_MAX_RENDER_FRAMES = 256,
};

typedef struct {
    bool active;
    p4_waveform_t waveform;
    uint16_t frequency_hz;
    uint16_t volume_step;
    uint32_t phase;
    uint32_t frames_remaining;
    uint32_t serial;
} p4_audio_voice_t;

typedef struct {
    p4_audio_voice_t voices[P4_GAME_AUDIO_MAX_VOICES];
    uint32_t next_serial;
    uint32_t tones_started;
    uint32_t voices_replaced;
    uint32_t frames_rendered;
    uint32_t clipped_samples;
} p4_audio_mixer_t;

typedef struct {
    uint32_t tones_started;
    uint32_t voices_replaced;
    uint32_t frames_rendered;
    uint32_t clipped_samples;
    uint8_t active_voices;
} p4_audio_mixer_stats_t;

void p4_audio_mixer_init(p4_audio_mixer_t *mixer);
bool p4_audio_mixer_play_tone(p4_audio_mixer_t *mixer,
                              const p4_tone_t *tone);
void p4_audio_mixer_stop_all(p4_audio_mixer_t *mixer);
bool p4_audio_mixer_render(p4_audio_mixer_t *mixer,
                           int16_t *interleaved_stereo,
                           size_t frame_count);
void p4_audio_mixer_get_stats(const p4_audio_mixer_t *mixer,
                              p4_audio_mixer_stats_t *out_stats);

bool p4_audio_mixer_service_play_tone(void *context,
                                      const p4_tone_t *tone);
void p4_audio_mixer_service_stop(void *context);

#ifdef __cplusplus
}
#endif

#endif
