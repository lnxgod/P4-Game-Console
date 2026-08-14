#include "platform_display_layout.h"

#include <string.h>

enum {
    SOURCE_WIDTH = 320,
    SOURCE_HEIGHT = 200,
    SCALE = 3,
    DESTINATION_WIDTH = 1024,
    DESTINATION_HEIGHT = 600,
    VIEWPORT_WIDTH = SOURCE_WIDTH * SCALE,
    LEFT_MARGIN = (DESTINATION_WIDTH - VIEWPORT_WIDTH) / 2,
};

bool platform_display_layout_rgb565_320x200(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height)
{
    if (source == NULL || destination == NULL ||
        source_stride_pixels < SOURCE_WIDTH ||
        destination_stride_pixels < DESTINATION_WIDTH ||
        destination_height < DESTINATION_HEIGHT) {
        return false;
    }

    for (size_t source_y = 0; source_y < SOURCE_HEIGHT; ++source_y) {
        uint16_t *first_destination_row =
            destination + (source_y * SCALE * destination_stride_pixels);
        const uint16_t *source_row = source + (source_y * source_stride_pixels);

        memset(first_destination_row, 0,
               DESTINATION_WIDTH * sizeof(*first_destination_row));
        for (size_t source_x = 0; source_x < SOURCE_WIDTH; ++source_x) {
            const uint16_t pixel = source_row[source_x];
            const size_t destination_x = LEFT_MARGIN + (source_x * SCALE);
            first_destination_row[destination_x] = pixel;
            first_destination_row[destination_x + 1U] = pixel;
            first_destination_row[destination_x + 2U] = pixel;
        }

        for (size_t repeat = 1; repeat < SCALE; ++repeat) {
            memcpy(first_destination_row + (repeat * destination_stride_pixels),
                   first_destination_row,
                   DESTINATION_WIDTH * sizeof(*first_destination_row));
        }
    }

    return true;
}
