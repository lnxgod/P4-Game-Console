// SPDX-License-Identifier: MIT

#ifndef P4_GAME_API_FEEDBACK_H
#define P4_GAME_API_FEEDBACK_H

#include <stdint.h>

#include "p4/audio_pack.h"
#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Original 4x4 ImageGen effect-atlas rows. */
typedef enum {
    P4_GAME_FX_ACTION = 0,
    P4_GAME_FX_IMPACT,
    P4_GAME_FX_REWARD,
    P4_GAME_FX_FAIL,
} p4_game_feedback_effect_t;

enum {
    P4_GAME_FEEDBACK_CELL_WIDTH = 40,
    P4_GAME_FEEDBACK_CELL_HEIGHT = 40,
};

/** Draw one transparent animation frame, centered and clipped to the surface. */
void p4_game_feedback_draw(p4_game_surface_t *surface,
                           p4_game_feedback_effect_t effect,
                           int center_x, int center_y,
                           uint32_t frame_index);

/** Draw the animation associated with an active shared audio effect. */
void p4_game_feedback_draw_audio_effect(
    p4_game_surface_t *surface,
    const p4_game_audio_effect_player_t *player,
    int center_x, int center_y);

#ifdef __cplusplus
}
#endif

#endif
