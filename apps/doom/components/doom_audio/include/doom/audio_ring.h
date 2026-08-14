// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOOM_AUDIO_RING_H
#define DOOM_AUDIO_RING_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "doom/audio_mixer.h"
#include "doom/music_synth.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DOOM_AUDIO_COMMAND_RING_CAPACITY = 32,
};

typedef enum {
    DOOM_AUDIO_COMMAND_START = 1,
    DOOM_AUDIO_COMMAND_UPDATE = 2,
    DOOM_AUDIO_COMMAND_MUSIC_PLAY = 3,
    DOOM_AUDIO_COMMAND_MUSIC_STOP = 4,
    DOOM_AUDIO_COMMAND_MUSIC_PAUSE = 5,
    DOOM_AUDIO_COMMAND_MUSIC_RESUME = 6,
} doom_audio_command_type_t;

typedef struct {
    doom_audio_command_type_t type;
    uint8_t voice_index;
    uint8_t volume;
    uint8_t separation;
    uint32_t desired_state;
    const doom_audio_sample_t *sample;
    doom_music_song_t *music_song;
    bool music_looping;
} doom_audio_command_t;

/**
 * Fixed-capacity single-producer/single-consumer command ring.
 *
 * Push and pop are bounded and never wait. The producer owns write_index and
 * the consumer owns read_index; release/acquire publication protects slots.
 */
typedef struct {
    atomic_uint_least32_t write_index;
    atomic_uint_least32_t read_index;
    doom_audio_command_t commands[DOOM_AUDIO_COMMAND_RING_CAPACITY];
} doom_audio_command_ring_t;

void doom_audio_command_ring_init(doom_audio_command_ring_t *ring);
bool doom_audio_command_ring_push(doom_audio_command_ring_t *ring,
                                  const doom_audio_command_t *command);
bool doom_audio_command_ring_pop(doom_audio_command_ring_t *ring,
                                 doom_audio_command_t *out_command);

#ifdef __cplusplus
}
#endif

#endif
