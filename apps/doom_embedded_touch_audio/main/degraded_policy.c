// SPDX-License-Identifier: GPL-2.0-or-later

#include "degraded_policy.h"

#include <stddef.h>

bool doom_touch_audio_failure_halts_dark(doom_touch_audio_failure_t failure)
{
    return failure == DOOM_TOUCH_AUDIO_FAILURE_DISPLAY ||
        failure == DOOM_TOUCH_AUDIO_FAILURE_WAD ||
        failure == DOOM_TOUCH_AUDIO_FAILURE_VIDEO ||
        failure == DOOM_TOUCH_AUDIO_FAILURE_AUDIO_SAFETY;
}

bool doom_touch_audio_retry_due(uint32_t now_ms, uint32_t last_attempt_ms,
                                uint32_t retry_interval_ms,
                                bool cleanup_proven, bool bus_state_proven)
{
    if (!cleanup_proven || !bus_state_proven || retry_interval_ms == 0U) {
        return false;
    }
    return (uint32_t)(now_ms - last_attempt_ms) >= retry_interval_ms;
}

bool doom_touch_audio_factory_start_proven(
    const doom_touch_audio_factory_start_witness_t *witness)
{
    return witness != NULL && witness->snapshot_valid &&
        witness->state == UINT32_C(1) && witness->running &&
        witness->pdm_created && witness->pdm_enabled &&
        witness->tx_created && witness->tx_enabled &&
        witness->zero_preload_frames == UINT32_C(1536) &&
        witness->gpio30_low_attempts == UINT32_C(1) &&
        witness->gpio30_low_successes == UINT32_C(1) &&
        witness->gpio30_low_initial_readbacks == UINT32_C(1) &&
        witness->measured_settle_us >= UINT32_C(350000) &&
        witness->gpio30_low_second_readbacks == UINT32_C(1) &&
        !witness->resources_retained &&
        witness->resources_owned == UINT32_C(2);
}
