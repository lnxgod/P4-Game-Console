// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime_gate.h"

#include <stddef.h>

/*
 * These independent flash-resident bytes are deliberately source-bound rather
 * than Kconfig-controlled. This exact 1/1/1 successor selects the reviewed
 * touch-and-factory-audio code path. Repository policy still denies flashing
 * or running this artifact until the separate GPIO30-low electrical release
 * and an exact-artifact authorization are recorded.
 */
static const volatile uint8_t s_composite_authorized = 1U;
static const volatile uint8_t s_touch_authorized = 1U;
static const volatile uint8_t s_audio_authorized = 1U;

void doom_touch_audio_runtime_gate_read(
    doom_touch_audio_runtime_gate_t *out_gate)
{
    if (out_gate == NULL) {
        return;
    }
    *out_gate = (doom_touch_audio_runtime_gate_t){
        .composite_authorized = s_composite_authorized,
        .touch_authorized = s_touch_authorized,
        .audio_authorized = s_audio_authorized,
        .reserved = 0U,
    };
}

doom_touch_audio_runtime_mode_t doom_touch_audio_runtime_gate_mode(
    const doom_touch_audio_runtime_gate_t *gate)
{
    if (gate == NULL || gate->reserved != 0U ||
        gate->composite_authorized != 1U || gate->touch_authorized != 1U ||
        gate->audio_authorized > 1U) {
        return DOOM_TOUCH_AUDIO_RUNTIME_BLOCKED;
    }
    return gate->audio_authorized == 1U
        ? DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_AND_AUDIO
        : DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_ONLY;
}
