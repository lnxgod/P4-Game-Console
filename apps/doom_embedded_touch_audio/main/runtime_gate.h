// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOOM_EMBEDDED_TOUCH_AUDIO_RUNTIME_GATE_H
#define DOOM_EMBEDDED_TOUCH_AUDIO_RUNTIME_GATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t composite_authorized;
    uint8_t touch_authorized;
    uint8_t audio_authorized;
    uint8_t reserved;
} doom_touch_audio_runtime_gate_t;

typedef enum {
    DOOM_TOUCH_AUDIO_RUNTIME_BLOCKED = 0,
    DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_ONLY,
    DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_AND_AUDIO,
} doom_touch_audio_runtime_mode_t;

void doom_touch_audio_runtime_gate_read(
    doom_touch_audio_runtime_gate_t *out_gate);

doom_touch_audio_runtime_mode_t doom_touch_audio_runtime_gate_mode(
    const doom_touch_audio_runtime_gate_t *gate);

/** User mute is an additional restriction; it never grants hardware access. */
bool doom_touch_audio_runtime_sound_allowed(
    const doom_touch_audio_runtime_gate_t *gate, uint8_t volume_step);

#endif
