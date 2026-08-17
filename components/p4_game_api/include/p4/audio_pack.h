// SPDX-License-Identifier: MIT

#ifndef P4_GAME_API_AUDIO_PACK_H
#define P4_GAME_API_AUDIO_PACK_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Original reusable Console API effects supplied as PCM16 mono samples. */
typedef enum {
    P4_GAME_AUDIO_EFFECT_ACTION = 0,
    P4_GAME_AUDIO_EFFECT_IMPACT,
    P4_GAME_AUDIO_EFFECT_REWARD,
    P4_GAME_AUDIO_EFFECT_FAIL,
    P4_GAME_AUDIO_EFFECT_COUNT,
} p4_game_audio_effect_t;

/**
 * Game-owned state for one bounded effect. A newly triggered effect replaces
 * the prior one; call service once per update and never spin on rejection.
 */
typedef struct {
    uint32_t sample_offset;
    p4_game_audio_effect_t effect;
    bool active;
} p4_game_audio_effect_player_t;

void p4_game_audio_effect_player_init(p4_game_audio_effect_player_t *player);

/**
 * Start one original effect. Returns false without changing the player when
 * the optional PCM service is unavailable. The first block is queued here.
 */
bool p4_game_audio_effect_play(p4_game_context_t *context,
                               p4_game_audio_effect_player_t *player,
                               p4_game_audio_effect_t effect);

/** Queue at most one 1..256-frame PCM16 stereo block for the active effect. */
bool p4_game_audio_effect_service(p4_game_context_t *context,
                                  p4_game_audio_effect_player_t *player);

bool p4_game_audio_effect_active(const p4_game_audio_effect_player_t *player);

#ifdef __cplusplus
}
#endif

#endif
