// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOOM_AUDIO_RUNTIME_H
#define DOOM_AUDIO_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doom/audio_mixer.h"
#include "doom/music_synth.h"
#include "esp_err.h"
#include "platform/audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Borrowed handle; the composite app retains create/destroy ownership. */
    platform_audio_t *platform_audio;
    /** Must be exactly DOOM_AUDIO_OUTPUT_RATE_HZ. */
    uint32_t sample_rate_hz;
    /**
     * Required ownership assertion for the composite runtime. The display
     * service must already own LDO3/LDO4. Doom audio never acquires or releases
     * either rail.
     */
    bool display_owns_ldo3_ldo4;
} doom_audio_runtime_config_t;

typedef struct {
    uint32_t commands_enqueued;
    uint32_t commands_dropped;
    uint32_t frames_rendered;
    uint32_t write_failures;
    /** Minimum worker stack headroom in bytes; UINT32_MAX until sampled. */
    uint32_t worker_stack_hwm_bytes;
    uint32_t music_songs_started;
    uint32_t music_events_processed;
    uint32_t music_notes_started;
    uint32_t music_loops_completed;
    uint32_t music_mixed_frames;
    uint32_t music_parse_failures;
    uint32_t music_maximum_absolute_mix;
    bool music_playing;
    bool music_paused;
} doom_audio_runtime_stats_t;

/**
 * Bind an already-created, muted platform_audio handle before Doom starts.
 * This performs no rail, codec, amplifier, or task side effects.
 */
esp_err_t doom_audio_runtime_bind(const doom_audio_runtime_config_t *config);

/** Start the worker and the bound platform audio path. */
esp_err_t doom_audio_runtime_start(void);

/**
 * Stop the worker with a bounded wait, mute/shut down platform audio, and
 * retain the borrowed handle in the bound state for optional restart.
 */
esp_err_t doom_audio_runtime_stop(void);

/** Unbind a stopped borrowed handle. */
esp_err_t doom_audio_runtime_unbind(void);

/** Nonblocking single-producer voice controls used by Doom's main task. */
bool doom_audio_runtime_start_voice(size_t voice_index,
                                    const doom_audio_sample_t *sample,
                                    uint8_t volume,
                                    uint8_t separation);
void doom_audio_runtime_stop_voice(size_t voice_index);
bool doom_audio_runtime_update_voice(size_t voice_index,
                                     uint8_t volume,
                                     uint8_t separation);
bool doom_audio_runtime_voice_active(size_t voice_index);

/** Nonblocking music controls used by the engine's MUS adapter. */
bool doom_audio_runtime_music_play(doom_music_song_t *song, bool looping);
bool doom_audio_runtime_music_stop(void);
bool doom_audio_runtime_music_pause(void);
bool doom_audio_runtime_music_resume(void);
bool doom_audio_runtime_music_set_volume(uint8_t volume);
bool doom_audio_runtime_music_is_playing(void);

/** Snapshot monotonic diagnostics without waiting for the worker. */
esp_err_t doom_audio_runtime_get_stats(doom_audio_runtime_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif
