// SPDX-License-Identifier: MIT

#ifndef PLATFORM_DISPLAY_REGION_H
#define PLATFORM_DISPLAY_REGION_H

#include <stdint.h>

/** A compact RGB565 source rectangle. Bounds are defined by its API. */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} platform_display_rgb565_region_t;

#endif
