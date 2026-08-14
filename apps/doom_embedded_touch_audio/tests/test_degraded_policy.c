// SPDX-License-Identifier: GPL-2.0-or-later

#include "degraded_policy.h"

#include <assert.h>
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
    return 0;
}
