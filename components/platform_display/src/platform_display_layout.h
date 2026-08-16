// SPDX-License-Identifier: MIT

#ifndef PLATFORM_DISPLAY_LAYOUT_H
#define PLATFORM_DISPLAY_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool platform_display_layout_rgb565_320x200(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height);

bool platform_display_layout_rgb565_768x480(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height);

bool platform_display_layout_rgb565_to_rgb888_1280x720(
    const uint16_t *source, size_t source_stride_pixels,
    uint8_t *destination, size_t destination_stride_bytes,
    size_t destination_height);

#endif
