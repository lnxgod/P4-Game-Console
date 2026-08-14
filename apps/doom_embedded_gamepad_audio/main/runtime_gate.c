// SPDX-License-Identifier: GPL-2.0-or-later

#include "runtime_gate.h"

#include <stddef.h>

/*
 * Volatile flash-resident bytes keep the fully guarded production branch in
 * the inactive build. They are intentionally independent: USB fixture
 * authorization can never imply speaker authorization, or vice versa. A
 * future authorization requires an explicit source and evidence change; no
 * Kconfig toggle can turn this compile/link image into a runtime image.
 */
static const volatile uint8_t s_composite_authorized = 0U;
static const volatile uint8_t s_usb_authorized = 0U;
static const volatile uint8_t s_audio_authorized = 0U;

void doom_gamepad_audio_runtime_gate_read(
    doom_gamepad_audio_runtime_gate_t *out_gate)
{
    if (out_gate == NULL) {
        return;
    }
    *out_gate = (doom_gamepad_audio_runtime_gate_t){
        .composite_authorized = s_composite_authorized,
        .usb_authorized = s_usb_authorized,
        .audio_authorized = s_audio_authorized,
        .reserved = 0U,
    };
}
