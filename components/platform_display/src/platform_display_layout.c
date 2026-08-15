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

bool platform_display_layout_rgb565_to_rgb888_1280x720(
    const uint16_t *source, size_t source_stride_pixels,
    uint8_t *destination, size_t destination_stride_bytes,
    size_t destination_height)
{
    enum {
        HDMI_WIDTH = 1280,
        HDMI_HEIGHT = 720,
        HDMI_PIXEL_BYTES = 3,
        HDMI_LEFT_MARGIN = 160,
        HDMI_TOP_MARGIN = 60,
    };
    const size_t active_row_bytes = HDMI_WIDTH * HDMI_PIXEL_BYTES;
    if (source == NULL || destination == NULL ||
        source_stride_pixels < SOURCE_WIDTH ||
        destination_stride_bytes < active_row_bytes ||
        destination_height < HDMI_HEIGHT) {
        return false;
    }

    for (size_t y = 0; y < HDMI_HEIGHT; ++y) {
        memset(destination + (y * destination_stride_bytes), 0,
               active_row_bytes);
    }

    for (size_t source_y = 0; source_y < SOURCE_HEIGHT; ++source_y) {
        uint8_t *first_destination_row = destination +
            ((HDMI_TOP_MARGIN + (source_y * SCALE)) *
             destination_stride_bytes);
        const uint16_t *source_row = source +
            (source_y * source_stride_pixels);
        for (size_t source_x = 0; source_x < SOURCE_WIDTH; ++source_x) {
            const uint16_t pixel = source_row[source_x];
            const uint8_t red = (uint8_t)(((pixel >> 11U) & 0x1fU) * 255U /
                                           31U);
            const uint8_t green = (uint8_t)(((pixel >> 5U) & 0x3fU) * 255U /
                                             63U);
            const uint8_t blue = (uint8_t)((pixel & 0x1fU) * 255U / 31U);
            const size_t destination_x = HDMI_LEFT_MARGIN +
                (source_x * SCALE);
            for (size_t repeat = 0; repeat < SCALE; ++repeat) {
                uint8_t *out = first_destination_row +
                    ((destination_x + repeat) * HDMI_PIXEL_BYTES);
                out[0] = red;
                out[1] = green;
                out[2] = blue;
            }
        }

        for (size_t repeat = 1; repeat < SCALE; ++repeat) {
            memcpy(first_destination_row +
                       (repeat * destination_stride_bytes),
                   first_destination_row, active_row_bytes);
        }
    }

    return true;
}
