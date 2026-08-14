// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime_gate.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    doom_touch_audio_runtime_gate_t gate = {
        .composite_authorized = UINT8_MAX,
        .touch_authorized = UINT8_MAX,
        .audio_authorized = UINT8_MAX,
        .reserved = UINT8_MAX,
    };
    doom_touch_audio_runtime_gate_read(&gate);
    assert(gate.composite_authorized == 1U);
    assert(gate.touch_authorized == 1U);
    assert(gate.audio_authorized == 1U);
    assert(gate.reserved == 0U);
    assert(doom_touch_audio_runtime_gate_mode(&gate) ==
           DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_AND_AUDIO);

    gate.composite_authorized = 1U;
    gate.touch_authorized = 1U;
    gate.audio_authorized = 0U;
    assert(doom_touch_audio_runtime_gate_mode(&gate) ==
           DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_ONLY);
    gate.audio_authorized = 1U;
    assert(doom_touch_audio_runtime_gate_mode(&gate) ==
           DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_AND_AUDIO);
    gate.audio_authorized = 2U;
    assert(doom_touch_audio_runtime_gate_mode(&gate) ==
           DOOM_TOUCH_AUDIO_RUNTIME_BLOCKED);
    gate.audio_authorized = 0U;
    gate.reserved = 1U;
    assert(doom_touch_audio_runtime_gate_mode(&gate) ==
           DOOM_TOUCH_AUDIO_RUNTIME_BLOCKED);

    assert(doom_touch_audio_runtime_gate_mode(NULL) ==
           DOOM_TOUCH_AUDIO_RUNTIME_BLOCKED);
    doom_touch_audio_runtime_gate_read(NULL);
    return 0;
}
