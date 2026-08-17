// SPDX-License-Identifier: MIT

#include "p4/audio_pack.h"

#include <stddef.h>
#include <string.h>

enum {
    P4_GAME_AUDIO_PACK_RENDER_FRAMES = P4_GAME_MAX_AUDIO_STREAM_FRAMES,
};

typedef struct {
    const int16_t *samples;
    uint32_t sample_count;
} p4_game_audio_clip_t;

#include "generated/audio_pack.inc"

static const p4_game_audio_clip_t s_clips[P4_GAME_AUDIO_EFFECT_COUNT] = {
    [P4_GAME_AUDIO_EFFECT_ACTION] = {
        .samples = s_p4_action_samples,
        .sample_count = sizeof(s_p4_action_samples) /
            sizeof(s_p4_action_samples[0]),
    },
    [P4_GAME_AUDIO_EFFECT_IMPACT] = {
        .samples = s_p4_impact_samples,
        .sample_count = sizeof(s_p4_impact_samples) /
            sizeof(s_p4_impact_samples[0]),
    },
    [P4_GAME_AUDIO_EFFECT_REWARD] = {
        .samples = s_p4_reward_samples,
        .sample_count = sizeof(s_p4_reward_samples) /
            sizeof(s_p4_reward_samples[0]),
    },
    [P4_GAME_AUDIO_EFFECT_FAIL] = {
        .samples = s_p4_fail_samples,
        .sample_count = sizeof(s_p4_fail_samples) /
            sizeof(s_p4_fail_samples[0]),
    },
};

static bool effect_valid(p4_game_audio_effect_t effect)
{
    return effect >= P4_GAME_AUDIO_EFFECT_ACTION &&
        effect < P4_GAME_AUDIO_EFFECT_COUNT;
}

void p4_game_audio_effect_player_init(p4_game_audio_effect_player_t *player)
{
    if (player != NULL) {
        *player = (p4_game_audio_effect_player_t){0};
    }
}

bool p4_game_audio_effect_service(p4_game_context_t *context,
                                  p4_game_audio_effect_player_t *player)
{
    if (context == NULL || player == NULL || !player->active ||
        !effect_valid(player->effect)) {
        return false;
    }
    const p4_game_audio_clip_t *const clip = &s_clips[player->effect];
    if (player->sample_offset >= clip->sample_count) {
        player->active = false;
        return false;
    }
    const uint32_t remaining = clip->sample_count - player->sample_offset;
    const size_t frame_count = remaining > P4_GAME_AUDIO_PACK_RENDER_FRAMES
        ? P4_GAME_AUDIO_PACK_RENDER_FRAMES : (size_t)remaining;
    int16_t stereo[P4_GAME_AUDIO_PACK_RENDER_FRAMES * 2U];
    for (size_t frame = 0U; frame < frame_count; ++frame) {
        const int16_t sample = clip->samples[player->sample_offset + frame];
        stereo[frame * 2U] = sample;
        stereo[frame * 2U + 1U] = sample;
    }
    if (!p4_game_submit_pcm16_stereo(context, stereo, frame_count)) {
        return false;
    }
    player->sample_offset += (uint32_t)frame_count;
    if (player->sample_offset >= clip->sample_count) {
        player->active = false;
    }
    return true;
}

bool p4_game_audio_effect_play(p4_game_context_t *context,
                               p4_game_audio_effect_player_t *player,
                               p4_game_audio_effect_t effect)
{
    if (context == NULL || player == NULL || !effect_valid(effect) ||
        context->services == NULL ||
        (context->services->available_capabilities &
         P4_GAME_CAP_AUDIO_STREAM) == 0U ||
        context->services->submit_pcm16_stereo == NULL) {
        return false;
    }
    *player = (p4_game_audio_effect_player_t){
        .effect = effect,
        .active = true,
    };
    return p4_game_audio_effect_service(context, player);
}

bool p4_game_audio_effect_active(const p4_game_audio_effect_player_t *player)
{
    return player != NULL && player->active;
}
