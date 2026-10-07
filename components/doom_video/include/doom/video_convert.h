#ifndef DOOM_VIDEO_CONVERT_H
#define DOOM_VIDEO_CONVERT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Convert a bounded 320x200 0x00RRGGBB surface to standard RGB565 words. */
bool doom_video_convert_xrgb8888_to_rgb565(const uint32_t *source,
                                           size_t source_stride_pixels,
                                           uint16_t *destination,
                                           size_t destination_stride_pixels);

/** Convert a 320x200 XRGB surface and nearest-neighbor prescale to 384x240.
 * This is exactly the ordinary RGB565 conversion followed by the Tab5 6:5
 * prescale. Source and destination must not overlap; strides count pixels.
 * Source pixels/padding and destination padding remain untouched. */
bool doom_video_convert_xrgb8888_to_rgb565_384x240(
    const uint32_t *source, size_t source_stride_pixels,
    uint16_t *destination, size_t destination_stride_pixels);

#ifdef __cplusplus
}
#endif

#endif
