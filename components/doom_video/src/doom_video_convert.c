#include "doom/video_convert.h"

enum {
    FRAME_WIDTH = 320,
    FRAME_HEIGHT = 200,
};

static uint16_t xrgb8888_to_rgb565(uint32_t pixel)
{
    const uint16_t red = (uint16_t)((pixel >> 19U) & UINT32_C(0x1f));
    const uint16_t green = (uint16_t)((pixel >> 10U) & UINT32_C(0x3f));
    const uint16_t blue = (uint16_t)((pixel >> 3U) & UINT32_C(0x1f));
    return (uint16_t)((uint16_t)(red << 11U) |
                      (uint16_t)(green << 5U) | blue);
}

bool doom_video_convert_xrgb8888_to_rgb565(const uint32_t *source,
                                           size_t source_stride_pixels,
                                           uint16_t *destination,
                                           size_t destination_stride_pixels)
{
    if (source == NULL || destination == NULL ||
        source_stride_pixels < FRAME_WIDTH ||
        destination_stride_pixels < FRAME_WIDTH) {
        return false;
    }

    for (size_t y = 0; y < FRAME_HEIGHT; ++y) {
        const uint32_t *source_row = source + (y * source_stride_pixels);
        uint16_t *destination_row =
            destination + (y * destination_stride_pixels);
        for (size_t x = 0; x < FRAME_WIDTH; ++x) {
            destination_row[x] = xrgb8888_to_rgb565(source_row[x]);
        }
    }
    return true;
}
