#include "doom/video_convert.h"

#include <string.h>

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
        /* Four independent pixels reduce loop overhead on the P4 while
         * retaining halfword stores for two-byte-aligned, odd-stride rows. */
        for (size_t x = 0; x < FRAME_WIDTH; x += 4U) {
            destination_row[x] = xrgb8888_to_rgb565(source_row[x]);
            destination_row[x + 1U] =
                xrgb8888_to_rgb565(source_row[x + 1U]);
            destination_row[x + 2U] =
                xrgb8888_to_rgb565(source_row[x + 2U]);
            destination_row[x + 3U] =
                xrgb8888_to_rgb565(source_row[x + 3U]);
        }
    }
    return true;
}

/* Fold the existing 6:5 nearest-neighbor prescale into conversion. Every
 * source pixel is converted once; the first pixel/row in each group of five
 * is repeated. This avoids a complete intermediate RGB565 write/read pass. */
bool doom_video_convert_xrgb8888_to_rgb565_384x240(
    const uint32_t *source, size_t source_stride_pixels,
    uint16_t *destination, size_t destination_stride_pixels)
{
    if (source == NULL || destination == NULL ||
        source_stride_pixels < 320U || destination_stride_pixels < 384U ||
        source_stride_pixels > SIZE_MAX / (200U * sizeof(*source)) ||
        destination_stride_pixels > SIZE_MAX / (240U * sizeof(*destination)))
        return false;

    for (unsigned group = 0U; group < 40U; ++group) {
        for (unsigned row = 0U; row < 5U; ++row) {
            const uint32_t *input = source;
            uint16_t *output = destination;
            for (unsigned block = 0U; block < 64U; ++block) {
                const uint16_t first = xrgb8888_to_rgb565(input[0]);
                output[0] = first;
                output[1] = first;
                output[2] = xrgb8888_to_rgb565(input[1]);
                output[3] = xrgb8888_to_rgb565(input[2]);
                output[4] = xrgb8888_to_rgb565(input[3]);
                output[5] = xrgb8888_to_rgb565(input[4]);
                input += 5U;
                output += 6U;
            }
            source += source_stride_pixels;
            destination += destination_stride_pixels;
            if (row == 0U) {
                memcpy(destination, destination - destination_stride_pixels,
                       384U * sizeof(*destination));
                destination += destination_stride_pixels;
            }
        }
    }
    return true;
}
