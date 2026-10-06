// SPDX-License-Identifier: MIT

#ifndef PLATFORM_DISPLAY_LAYOUT_H
#define PLATFORM_DISPLAY_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/display_region.h"

bool platform_display_layout_rgb565_320x200(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height);

bool platform_display_layout_rgb565_384x240(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height);

bool platform_display_layout_rgb565_768x480(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height);

bool platform_display_layout_rgb565_1280x720(const uint16_t *source,
    size_t source_stride_pixels, uint16_t *destination,
    size_t destination_stride_pixels, size_t destination_height);

bool platform_display_layout_rgb565_1152x720(const uint16_t *source,
    size_t source_stride_pixels, uint16_t *destination,
    size_t destination_stride_pixels, size_t destination_height);

/* Map a compact 768x480 logical content rectangle to the centered native
 * 480x800 Waveshare framebuffer after the same counter-clockwise rotation as
 * the PPA content path. Returns false on non-Waveshare targets or invalid
 * input. Kept host-pure so exact mapping can be regression tested. */
bool platform_display_layout_map_content_region_ccw(
    const platform_display_rgb565_region_t *source,
    platform_display_rgb565_region_t *destination);

/* Tab5 native UI damage, including replay of the previous generation into
 * the alternating panel buffer. NULL previous means that buffer needs all pixels. */
bool platform_display_layout_tab5_damage(
    const platform_display_rgb565_region_t *current,
    const platform_display_rgb565_region_t *previous,
    platform_display_rgb565_region_t *source,
    platform_display_rgb565_region_t *destination);

/** Add one source region while coalescing overlapping/contained damage. */
bool platform_display_layout_compact_content_region(
    platform_display_rgb565_region_t *regions,
    size_t *region_count,
    size_t region_capacity,
    const platform_display_rgb565_region_t *candidate);

bool platform_display_layout_rgb565_to_rgb888_1280x720(
    const uint16_t *source, size_t source_stride_pixels,
    uint8_t *destination, size_t destination_stride_bytes,
    size_t destination_height);

bool platform_display_layout_rgb565_384x240_to_rgb888_1280x720(
    const uint16_t *source, size_t source_stride_pixels,
    uint8_t *destination, size_t destination_stride_bytes,
    size_t destination_height);

#endif
