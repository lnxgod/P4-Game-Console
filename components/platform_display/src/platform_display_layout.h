#ifndef PLATFORM_DISPLAY_LAYOUT_H
#define PLATFORM_DISPLAY_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Pure, host-testable layout primitive behind platform_display_submit_rgb565().
 * Pixels are standard RGB565 numeric words: R[15:11], G[10:5], B[4:0].
 */
bool platform_display_layout_rgb565_320x200(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height);

#endif
