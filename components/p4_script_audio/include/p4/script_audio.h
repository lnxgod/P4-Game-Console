// SPDX-License-Identifier: MIT

#ifndef P4_SCRIPT_AUDIO_H
#define P4_SCRIPT_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/lua_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_SCRIPT_AUDIO_SAMPLE_RATE_HZ = 16000,
    P4_SCRIPT_AUDIO_CHANNEL_COUNT = 4,
};

typedef struct {
    uint32_t phase;
    uint32_t phase_step;
    uint32_t frames_remaining;
    uint32_t noise_state;
    uint8_t volume;
    uint8_t waveform;
    bool active;
} p4_script_audio_voice_t;

typedef struct {
    p4_script_audio_voice_t voices[P4_SCRIPT_AUDIO_CHANNEL_COUNT];
    uint32_t tones_started;
    uint32_t tones_replaced;
    uint32_t frames_rendered;
    uint32_t clipped_samples;
} p4_script_audio_t;

void p4_script_audio_init(p4_script_audio_t *audio);
bool p4_script_audio_play(
    p4_script_audio_t *audio,
    uint8_t channel,
    uint16_t frequency_hz,
    uint16_t duration_ticks,
    uint8_t volume,
    p4_lua_waveform_t waveform);
void p4_script_audio_stop(p4_script_audio_t *audio, uint8_t channel);
void p4_script_audio_stop_all(p4_script_audio_t *audio);
bool p4_script_audio_render_stereo(
    p4_script_audio_t *audio,
    int16_t *interleaved_stereo,
    size_t frame_count);

#ifdef __cplusplus
}
#endif

#endif
