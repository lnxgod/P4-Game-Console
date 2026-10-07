// SPDX-License-Identifier: Apache-2.0
#ifndef TAB5_PRESCALED_GAME_TRANSFORM_H
#define TAB5_PRESCALED_GAME_TRANSFORM_H

#include "platform_display_layout.h"

typedef int (*tab5_game_accelerate_fn)(const uint16_t *, size_t, size_t,
                                      uint16_t *);
typedef struct {
    int accelerator_result;
    bool complete;
} tab5_prescaled_game_transform_result_t;

/* Accelerator consumes the source synchronously and returns zero on success.
 * Failed partial output is completely replaced by the original game's CPU
 * fallback mapping. This helper owns no scanout state or buffer lifetime. */
static inline tab5_prescaled_game_transform_result_t
    tab5_prescaled_game_transform(
        const uint16_t *source, size_t source_stride,
        uint16_t *destination, size_t destination_stride,
        size_t destination_height, tab5_game_accelerate_fn accelerator)
{
    const int result = accelerator(source, source_stride, 384U, destination);
    return (tab5_prescaled_game_transform_result_t){
        .accelerator_result = result,
        .complete = result == 0 ||
            platform_display_layout_rgb565_prescaled_game_384x240(
                source, source_stride, destination, destination_stride,
                destination_height),
    };
}

#endif
