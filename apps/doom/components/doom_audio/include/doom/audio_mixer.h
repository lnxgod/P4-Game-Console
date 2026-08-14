// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOOM_AUDIO_MIXER_H
#define DOOM_AUDIO_MIXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DOOM_AUDIO_OUTPUT_RATE_HZ = 16000,
    DOOM_AUDIO_CHANNEL_COUNT = 2,
    DOOM_AUDIO_MAX_VOICES = 8,
    DOOM_AUDIO_MAX_DMX_SAMPLE_RATE_HZ = 48000,
    DOOM_AUDIO_MIN_DMX_SAMPLE_RATE_HZ = 4000,
    DOOM_AUDIO_MAX_DMX_SAMPLE_BYTES = 262144,
};

/**
 * Immutable view of the playable bytes in a Doom DMX sound lump.
 *
 * Samples are unsigned 8-bit mono centered on 128. The view excludes the
 * 8-byte DMX header and the 16 leading/trailing samples skipped by the
 * original DMX-compatible mixer. The caller owns the backing bytes.
 */
typedef struct {
    const uint8_t *samples;
    size_t sample_count;
    uint32_t sample_rate_hz;
} doom_audio_sample_t;

typedef struct {
    doom_audio_sample_t sample;
    uint64_t phase_q32;
    uint64_t step_q32;
    uint16_t left_gain_q15;
    uint16_t right_gain_q15;
    bool active;
} doom_audio_voice_t;

typedef struct {
    doom_audio_voice_t voices[DOOM_AUDIO_MAX_VOICES];
} doom_audio_mixer_t;

/** Parse and bound-check one complete DMX-format sound lump. */
bool doom_audio_parse_dmx_lump(const uint8_t *lump,
                               size_t lump_bytes,
                               doom_audio_sample_t *out_sample);

/** Reset every voice to silence. */
void doom_audio_mixer_init(doom_audio_mixer_t *mixer);

/**
 * Start or replace one voice. Volume is 0..127 and stereo separation 0..254.
 * The sample backing bytes must stay immutable until the voice stops.
 */
bool doom_audio_mixer_start(doom_audio_mixer_t *mixer,
                            size_t voice_index,
                            const doom_audio_sample_t *sample,
                            uint8_t volume,
                            uint8_t separation);

/** Stop one voice immediately. */
bool doom_audio_mixer_stop(doom_audio_mixer_t *mixer, size_t voice_index);

/** Update one active voice's volume/pan without changing playback position. */
bool doom_audio_mixer_update(doom_audio_mixer_t *mixer,
                             size_t voice_index,
                             uint8_t volume,
                             uint8_t separation);

/** Return whether a bounded voice is still producing samples. */
bool doom_audio_mixer_voice_active(const doom_audio_mixer_t *mixer,
                                   size_t voice_index);

/**
 * Fill exactly frame_count interleaved signed PCM16 stereo frames.
 *
 * Inactive voices produce zeroes. Active voices are nearest-neighbor
 * resampled to 16 kHz, mixed in a wide accumulator, and saturated to PCM16.
 */
bool doom_audio_mixer_render(doom_audio_mixer_t *mixer,
                             int16_t *interleaved_pcm,
                             size_t frame_count);

#ifdef __cplusplus
}
#endif

#endif
