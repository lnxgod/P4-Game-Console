// SPDX-License-Identifier: Apache-2.0
#ifndef TAB5_GAME_PRESCALE_H
#define TAB5_GAME_PRESCALE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Exact nearest-neighbor 320x200 -> 384x240. The non-overlapping buffers
 * contain 200 source-stride and 240 destination-stride uint16_t rows.
 * Five input pixels become [a,a,b,c,d,e], exactly floor(x*320/384).
 * The same vertical pattern repeats the first expanded row of each group.
 * Pointer increments replace per-pixel multiply/divide/address arithmetic. */
static inline bool tab5_game_prescale_rgb565(
    const uint16_t *source, size_t source_stride,
    uint16_t *destination, size_t destination_stride)
{
    if (source == NULL || destination == NULL || source_stride < 320U ||
        destination_stride < 384U ||
        source_stride > SIZE_MAX / (200U * sizeof(*source)) ||
        destination_stride > SIZE_MAX / (240U * sizeof(*destination)))
        return false;

    for (unsigned group = 0; group < 40U; ++group) {
        for (unsigned row = 0; row < 5U; ++row) {
            const uint16_t *input = source;
            uint16_t *output = destination;
            for (unsigned block = 0; block < 64U; ++block) {
                const uint16_t a = input[0];
                output[0] = a;
                output[1] = a;
                output[2] = input[1];
                output[3] = input[2];
                output[4] = input[3];
                output[5] = input[4];
                input += 5;
                output += 6;
            }
            source += source_stride;
            destination += destination_stride;
            if (row == 0U) {
                memcpy(destination, destination - destination_stride,
                       384U * sizeof(*destination));
                destination += destination_stride;
            }
        }
    }
    return true;
}

#endif
