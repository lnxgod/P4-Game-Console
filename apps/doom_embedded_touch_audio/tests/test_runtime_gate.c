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
    /* A saved mute or invalid volume must never enable codec/amp calls,
     * even with every electrical authorization present. Conversely volume
     * cannot turn any blocked/touch-only gate into an audio authorization. */
    for (unsigned composite = 0U; composite <= 2U; ++composite) {
        for (unsigned touch = 0U; touch <= 2U; ++touch) {
            for (unsigned audio = 0U; audio <= 2U; ++audio) {
                gate = (doom_touch_audio_runtime_gate_t){
                    .composite_authorized = (uint8_t)composite,
                    .touch_authorized = (uint8_t)touch,
                    .audio_authorized = (uint8_t)audio,
                    .reserved = 0U,
                };
                assert(!doom_touch_audio_runtime_sound_allowed(&gate, 0U));
                assert(!doom_touch_audio_runtime_sound_allowed(&gate, 11U));
                assert(!doom_touch_audio_runtime_sound_allowed(&gate, UINT8_MAX));
                for (uint8_t volume = 1U; volume <= 10U; ++volume) {
                    assert(doom_touch_audio_runtime_sound_allowed(&gate, volume) ==
                           (composite == 1U && touch == 1U && audio == 1U));
                }
                gate.reserved = 1U;
                assert(!doom_touch_audio_runtime_sound_allowed(&gate, 3U));
            }
        }
    }
    assert(!doom_touch_audio_runtime_sound_allowed(NULL, 3U));
    doom_touch_audio_runtime_gate_read(NULL);
    return 0;
}
