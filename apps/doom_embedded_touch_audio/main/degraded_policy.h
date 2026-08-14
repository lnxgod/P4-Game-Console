// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOOM_EMBEDDED_TOUCH_AUDIO_DEGRADED_POLICY_H
#define DOOM_EMBEDDED_TOUCH_AUDIO_DEGRADED_POLICY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DOOM_TOUCH_AUDIO_FAILURE_DISPLAY = 0,
    DOOM_TOUCH_AUDIO_FAILURE_WAD,
    DOOM_TOUCH_AUDIO_FAILURE_VIDEO,
    DOOM_TOUCH_AUDIO_FAILURE_SHARED_I2C,
    DOOM_TOUCH_AUDIO_FAILURE_TOUCH,
    DOOM_TOUCH_AUDIO_FAILURE_AUDIO,
    DOOM_TOUCH_AUDIO_FAILURE_AUDIO_SAFETY,
} doom_touch_audio_failure_t;

bool doom_touch_audio_failure_halts_dark(doom_touch_audio_failure_t failure);

bool doom_touch_audio_retry_due(uint32_t now_ms, uint32_t last_attempt_ms,
                                uint32_t retry_interval_ms,
                                bool cleanup_proven, bool bus_state_proven);

#endif
