// SPDX-License-Identifier: GPL-2.0-or-later

#include "degraded_policy.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    assert(doom_touch_audio_failure_halts_dark(
        DOOM_TOUCH_AUDIO_FAILURE_DISPLAY));
    assert(doom_touch_audio_failure_halts_dark(
        DOOM_TOUCH_AUDIO_FAILURE_WAD));
    assert(doom_touch_audio_failure_halts_dark(
        DOOM_TOUCH_AUDIO_FAILURE_VIDEO));
    assert(!doom_touch_audio_failure_halts_dark(
        DOOM_TOUCH_AUDIO_FAILURE_SHARED_I2C));
    assert(!doom_touch_audio_failure_halts_dark(
        DOOM_TOUCH_AUDIO_FAILURE_TOUCH));
    assert(!doom_touch_audio_failure_halts_dark(
        DOOM_TOUCH_AUDIO_FAILURE_AUDIO));
    assert(doom_touch_audio_failure_halts_dark(
        DOOM_TOUCH_AUDIO_FAILURE_AUDIO_SAFETY));

    assert(!doom_touch_audio_retry_due(1000U, 0U, 1000U, false, true));
    assert(!doom_touch_audio_retry_due(1000U, 0U, 1000U, true, false));
    assert(!doom_touch_audio_retry_due(1000U, 0U, 0U, true, true));
    assert(!doom_touch_audio_retry_due(999U, 0U, 1000U, true, true));
    assert(doom_touch_audio_retry_due(1000U, 0U, 1000U, true, true));
    assert(doom_touch_audio_retry_due(5U, UINT32_MAX - 9U, 15U,
                                      true, true));

    doom_touch_audio_factory_start_witness_t witness = {
        .snapshot_valid = true,
        .state = 1U,
        .running = true,
        .pdm_created = true,
        .pdm_enabled = true,
        .tx_created = true,
        .tx_enabled = true,
        .zero_preload_frames = 1536U,
        .gpio30_low_attempts = 1U,
        .gpio30_low_successes = 1U,
        .gpio30_low_initial_readbacks = 1U,
        .measured_settle_us = 350000U,
        .gpio30_low_second_readbacks = 1U,
        .resources_retained = false,
        .resources_owned = 2U,
    };
    assert(doom_touch_audio_factory_start_proven(&witness));
    witness.snapshot_valid = false;
    assert(!doom_touch_audio_factory_start_proven(&witness));
    witness.snapshot_valid = true;
    witness.gpio30_low_second_readbacks = 0U;
    assert(!doom_touch_audio_factory_start_proven(&witness));
    witness.gpio30_low_second_readbacks = 1U;
    witness.measured_settle_us = 349999U;
    assert(!doom_touch_audio_factory_start_proven(&witness));
    assert(!doom_touch_audio_factory_start_proven(NULL));
    return 0;
}
