// SPDX-License-Identifier: MIT
#ifndef P4_GAME_AUDIO_WORKER_H
#define P4_GAME_AUDIO_WORKER_H
#include "p4/platform.h"
#include "p4/audio.h"

typedef struct p4_game_audio_worker p4_game_audio_worker_t;
typedef struct {
    p4_audio_mixer_stats_t mixer;
    uint32_t frames_written, rejected_commands, write_failures, stack_remaining;
    int core;
    bool running;
} p4_game_audio_worker_stats_t;

/* OS-only service. Borrows the already-open board audio session and mixer.
 * On success the worker exclusively owns both until close joins it. Game
 * callbacks copy bounded commands without waiting; no cartridge tasks/handles. */
esp_err_t p4_game_audio_worker_open(p4_game_audio_worker_t **out,
    p4_game_platform_audio_t *audio,p4_audio_mixer_t *mixer);
bool p4_game_audio_worker_pcm(p4_game_audio_worker_t *worker,
    const int16_t *pcm,size_t frames);
bool p4_game_audio_worker_tone(p4_game_audio_worker_t *worker,const p4_tone_t *tone);
void p4_game_audio_worker_stop(p4_game_audio_worker_t *worker);
void p4_game_audio_worker_stats(p4_game_audio_worker_t *worker,
    p4_game_audio_worker_stats_t *out);
/* Stop and join BEFORE closing board audio or releasing mixer/context memory.
 * Timeout retains ownership and allocations; caller must not free them. */
esp_err_t p4_game_audio_worker_close(p4_game_audio_worker_t **worker);
#endif
