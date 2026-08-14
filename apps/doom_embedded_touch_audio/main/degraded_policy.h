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

typedef struct {
    bool snapshot_valid;
    uint32_t state;
    bool running;
    bool pdm_created;
    bool pdm_enabled;
    bool tx_created;
    bool tx_enabled;
    uint32_t zero_preload_frames;
    uint32_t gpio30_low_attempts;
    uint32_t gpio30_low_successes;
    uint32_t gpio30_low_initial_readbacks;
    uint32_t measured_settle_us;
    uint32_t gpio30_low_second_readbacks;
    bool resources_retained;
    uint32_t resources_owned;
} doom_touch_audio_factory_start_witness_t;

bool doom_touch_audio_failure_halts_dark(doom_touch_audio_failure_t failure);

bool doom_touch_audio_retry_due(uint32_t now_ms, uint32_t last_attempt_ms,
                                uint32_t retry_interval_ms,
                                bool cleanup_proven, bool bus_state_proven);

/** Validate the backend's lock-independent proof of a completed safe start. */
bool doom_touch_audio_factory_start_proven(
    const doom_touch_audio_factory_start_witness_t *witness);

#endif
