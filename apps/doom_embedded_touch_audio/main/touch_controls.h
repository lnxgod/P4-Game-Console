// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOOM_EMBEDDED_TOUCH_AUDIO_TOUCH_CONTROLS_H
#define DOOM_EMBEDDED_TOUCH_AUDIO_TOUCH_CONTROLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "doom_touch/input.h"
#include "platform/touch.h"

bool doom_touch_audio_frame_from_platform(
    const platform_touch_frame_t *source,
    doom_touch_frame_t *destination
);

bool doom_touch_audio_action_key(uint8_t action, unsigned char *out_key);

/** Drop queued presses and publish immediate releases for every held action. */
bool doom_touch_audio_force_neutral(doom_touch_input_t *input);

bool doom_touch_audio_compose_frame(
    const uint32_t *source,
    size_t source_stride_pixels,
    uint32_t *destination,
    size_t destination_stride_pixels,
    const doom_touch_input_t *input
);

bool doom_touch_audio_compose_frame_sized(
    const uint32_t *source, size_t source_stride_pixels,
    uint32_t *destination, size_t destination_stride_pixels,
    size_t width, size_t height, const doom_touch_input_t *input);

#endif
