// SPDX-License-Identifier: GPL-2.0-or-later

#include "degraded_policy.h"

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
