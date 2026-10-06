// SPDX-License-Identifier: GPL-2.0-or-later
/* Portable SMF player. Synthesis derived from doom_audio/doom_music_synth.c.
 * Immutable borrowed MIDI bytes, fixed storage, 16 kHz signed stereo. */
#ifndef P4_MIDI_H
#define P4_MIDI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { P4_MIDI_CHANNEL_COUNT=16, P4_MIDI_MAX_VOICES=16, P4_MIDI_MAX_TRACKS=32,
       P4_MIDI_MAX_BYTES=262144, P4_MIDI_SAMPLE_RATE=16000 };
typedef enum {
    P4_MIDI_WAVE_SINE = 0,
    P4_MIDI_WAVE_TRIANGLE,
    P4_MIDI_WAVE_SAW,
    P4_MIDI_WAVE_SQUARE,
    P4_MIDI_WAVE_NOISE,
} p4_midi_waveform_t;

typedef struct {
    uint8_t program;
    uint8_t volume;
    uint8_t expression;
    uint8_t pan;
    uint8_t bend;
    uint8_t last_velocity;
} p4_midi_channel_t;

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
    p4_midi_waveform_t waveform;
    bool active;
    bool releasing;
    bool sustained;
    bool percussion;
} p4_midi_voice_t;

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
} p4_midi_stats_t;


typedef struct {
    size_t begin, end, cursor;
    uint32_t next_tick;
    uint8_t running_status;
    bool ended;
} p4_midi_track_t;
typedef struct {
    const uint8_t *data;
    size_t bytes;
    p4_midi_track_t tracks[P4_MIDI_MAX_TRACKS];
    uint16_t track_count, division;
    uint32_t tick, tempo, samples_until_event;
    uint64_t timing_remainder;
    uint32_t voice_age, noise_state;
    uint8_t volume;
    bool playing, looping, gain_dirty;
    bool sustain[P4_MIDI_CHANNEL_COUNT];
    p4_midi_channel_t channels[P4_MIDI_CHANNEL_COUNT];
    p4_midi_voice_t voices[P4_MIDI_MAX_VOICES];
    p4_midi_stats_t stats;
} p4_midi_player_t;
/* Start validates chunk geometry; malformed events stop playback safely. */
bool p4_midi_start(p4_midi_player_t *, const void *, size_t, bool loop);
void p4_midi_stop(p4_midi_player_t *);
void p4_midi_volume(p4_midi_player_t *, uint8_t);
/* Adds to an existing stereo block. At most 256 frames per call. */
bool p4_midi_mix(p4_midi_player_t *, int16_t *, size_t);
#endif
