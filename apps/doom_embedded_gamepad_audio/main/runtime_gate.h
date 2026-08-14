// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOOM_EMBEDDED_GAMEPAD_AUDIO_RUNTIME_GATE_H
#define DOOM_EMBEDDED_GAMEPAD_AUDIO_RUNTIME_GATE_H

#include <stdint.h>

typedef struct {
    uint8_t composite_authorized;
    uint8_t usb_authorized;
    uint8_t audio_authorized;
    uint8_t reserved;
} doom_gamepad_audio_runtime_gate_t;

void doom_gamepad_audio_runtime_gate_read(
    doom_gamepad_audio_runtime_gate_t *out_gate);

#endif
