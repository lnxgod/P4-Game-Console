// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOOM_MUSIC_SYNTH_H
#define DOOM_MUSIC_SYNTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DOOM_MUSIC_CHANNEL_COUNT = 16,
    DOOM_MUSIC_MAX_VOICES = 16,
    DOOM_MUSIC_TICKS_PER_SECOND = 140,
    DOOM_MUSIC_MAX_SONG_BYTES = 262144,
};

typedef struct doom_music_song doom_music_song_t;

typedef enum {
    DOOM_MUSIC_WAVE_SINE = 0,
    DOOM_MUSIC_WAVE_TRIANGLE,
    DOOM_MUSIC_WAVE_SAW,
    DOOM_MUSIC_WAVE_SQUARE,
    DOOM_MUSIC_WAVE_NOISE,
} doom_music_waveform_t;

typedef struct {
    uint8_t program;
    uint8_t volume;
    uint8_t expression;
    uint8_t pan;
    uint8_t bend;
    uint8_t last_velocity;
} doom_music_channel_t;

typedef struct {
    uint32_t phase;
    uint32_t phase_step;
    uint32_t age;
    uint16_t envelope_q15;
    uint16_t left_gain_q15;
    uint16_t right_gain_q15;
    uint8_t channel;
    uint8_t note;
    uint8_t velocity;
    doom_music_waveform_t waveform;
    bool active;
    bool releasing;
    bool percussion;
} doom_music_voice_t;

typedef struct {
    uint32_t songs_started;
    uint32_t events_processed;
    uint32_t notes_started;
    uint32_t loops_completed;
    uint32_t mixed_frames;
    uint32_t parse_failures;
    uint32_t maximum_absolute_mix;
    bool playing;
    bool paused;
} doom_music_stats_t;

/**
 * Worker-owned MUS/SMF MIDI sequencer and lightweight procedural synthesizer.
 *
 * All mutation and rendering stays on the single audio worker. The song is a
 * ref-counted immutable copy, so the Doom task may unregister its handle while
 * a queued or active worker reference remains live.
 */
typedef struct {
    doom_music_song_t *song;
    /* Fixed-size MIDI state lives off the audio task's bounded stack. */
    struct doom_music_midi *midi;
    const uint8_t *score;
    size_t score_bytes;
    size_t cursor;
    uint32_t samples_until_event;
    uint32_t timing_remainder;
    uint32_t voice_age;
    uint32_t noise_state;
    uint8_t volume;
    bool looping;
    bool playing;
    bool paused;
    bool gain_dirty;
    doom_music_channel_t channels[DOOM_MUSIC_CHANNEL_COUNT];
    doom_music_voice_t voices[DOOM_MUSIC_MAX_VOICES];
    doom_music_stats_t stats;
} doom_music_player_t;

/** Validate one complete MUS lump with every read bounded to the score range. */
bool doom_music_validate_mus(const uint8_t *data, size_t length);

/**
 * Copy a MUS or SMF type 0/1 song with one caller reference. MUS is validated
 * completely; MIDI chunk geometry is validated here and events while playing.
 */
doom_music_song_t *doom_music_song_create(const void *data, size_t length);

/** Add/drop a song reference. Release frees the copy when the count reaches 0. */
bool doom_music_song_retain(doom_music_song_t *song);
void doom_music_song_release(doom_music_song_t *song);
size_t doom_music_song_length(const doom_music_song_t *song);

void doom_music_player_init(doom_music_player_t *player);
bool doom_music_player_start(doom_music_player_t *player,
                             doom_music_song_t *song,
                             bool looping);
void doom_music_player_stop(doom_music_player_t *player);
void doom_music_player_pause(doom_music_player_t *player);
void doom_music_player_resume(doom_music_player_t *player);
bool doom_music_player_set_volume(doom_music_player_t *player,
                                  uint8_t volume);
bool doom_music_player_is_playing(const doom_music_player_t *player);

/**
 * Add music into existing interleaved PCM16 stereo frames with saturation.
 * The synth advances only while unpaused. An invalid runtime score fails quiet.
 */
bool doom_music_player_mix(doom_music_player_t *player,
                           int16_t *interleaved_pcm,
                           size_t frame_count);

void doom_music_player_get_stats(const doom_music_player_t *player,
                                 doom_music_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif
