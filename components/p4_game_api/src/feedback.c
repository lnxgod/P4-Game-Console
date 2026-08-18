// SPDX-License-Identifier: MIT

#include "p4/feedback.h"

#include <stddef.h>

#include "p4/draw.h"

enum {
    P4_GAME_FEEDBACK_ATLAS_COLUMNS = 4,
    P4_GAME_FEEDBACK_ATLAS_ROWS = 4,
    P4_GAME_FEEDBACK_ATLAS_WIDTH =
        P4_GAME_FEEDBACK_CELL_WIDTH * P4_GAME_FEEDBACK_ATLAS_COLUMNS,
    P4_GAME_FEEDBACK_ATLAS_HEIGHT =
        P4_GAME_FEEDBACK_CELL_HEIGHT * P4_GAME_FEEDBACK_ATLAS_ROWS,
    P4_GAME_FEEDBACK_FRAME_HOLD = 5,
    P4_GAME_FEEDBACK_CYCLE_FRAMES = 120,
};

#include "generated/feedback_atlas_v2.inc"

void p4_game_feedback_draw(p4_game_surface_t *surface,
                           p4_game_feedback_effect_t effect,
                           int center_x, int center_y,
                           uint32_t frame_index)
{
    if (surface == NULL || effect < P4_GAME_FX_ACTION ||
        effect > P4_GAME_FX_FAIL) {
        return;
    }
    const uint32_t cycle_frame = frame_index % P4_GAME_FEEDBACK_CYCLE_FRAMES;
    if (cycle_frame >= P4_GAME_FEEDBACK_FRAME_HOLD *
            P4_GAME_FEEDBACK_ATLAS_COLUMNS) {
        return;
    }
    const size_t column = cycle_frame / P4_GAME_FEEDBACK_FRAME_HOLD;
    const size_t row = (size_t)effect;
    const uint16_t *const pixels = s_p4_game_feedback_atlas +
        row * P4_GAME_FEEDBACK_CELL_HEIGHT * P4_GAME_FEEDBACK_ATLAS_WIDTH +
        column * P4_GAME_FEEDBACK_CELL_WIDTH;
    p4_draw_sprite_rgb565(
        surface,
        center_x - P4_GAME_FEEDBACK_CELL_WIDTH / 2,
        center_y - P4_GAME_FEEDBACK_CELL_HEIGHT / 2,
        pixels, P4_GAME_FEEDBACK_CELL_WIDTH, P4_GAME_FEEDBACK_CELL_HEIGHT,
        P4_GAME_FEEDBACK_ATLAS_WIDTH, true, UINT16_C(0x0000));
}

void p4_game_feedback_draw_audio_effect(
    p4_game_surface_t *surface,
    const p4_game_audio_effect_player_t *player,
    int center_x, int center_y)
{
    if (player == NULL || !player->active ||
        player->effect >= P4_GAME_AUDIO_EFFECT_COUNT) {
        return;
    }
    p4_game_feedback_draw(
        surface, (p4_game_feedback_effect_t)player->effect,
        center_x, center_y,
        player->sample_offset / P4_GAME_MAX_AUDIO_STREAM_FRAMES);
}
