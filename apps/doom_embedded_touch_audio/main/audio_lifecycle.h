// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOOM_E6_AUDIO_LIFECYCLE_H
#define DOOM_E6_AUDIO_LIFECYCLE_H

#include <stdbool.h>

#include "esp_err.h"
#include "platform/audio.h"

typedef struct {
    platform_audio_t *audio;
    bool hardware_touched;
    bool safe_high_proven;
    bool runtime_bound;
    bool released;
} doom_e6_audio_lifecycle_t;

typedef struct {
    esp_err_t stop_result;
    esp_err_t unbind_result;
    esp_err_t destroy_result;
    esp_err_t recover_result;
    bool complete;
} doom_e6_audio_release_result_t;

/*
 * Stop/join the Doom worker before any backend cleanup.  INVALID_STATE may
 * mean the engine's sound Init failed after returning the runtime to BOUND;
 * only a successful direct unbind proves that stopped state.  Any timeout or
 * failed unbind retains the backend and rejects concurrent cleanup.
 */
esp_err_t doom_e6_audio_release(
    doom_e6_audio_lifecycle_t *lifecycle,
    doom_e6_audio_release_result_t *out_result
);

#endif
