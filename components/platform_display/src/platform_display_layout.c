// SPDX-License-Identifier: MIT

#include "platform_display_layout.h"

#include <string.h>

#include "platform/board.h"

enum {
    SOURCE_WIDTH = 320,
    SOURCE_HEIGHT = 200,
    LOGICAL_WIDTH = PLATFORM_BOARD_DISPLAY_WIDTH,
    LOGICAL_HEIGHT = PLATFORM_BOARD_DISPLAY_HEIGHT,
    DESTINATION_WIDTH = PLATFORM_BOARD_DISPLAY_NATIVE_WIDTH,
    DESTINATION_HEIGHT = PLATFORM_BOARD_DISPLAY_NATIVE_HEIGHT,
    VIEWPORT_WIDTH = PLATFORM_BOARD_GAME_VIEWPORT_WIDTH,
    VIEWPORT_HEIGHT = PLATFORM_BOARD_GAME_VIEWPORT_HEIGHT,
    LEFT_MARGIN = PLATFORM_BOARD_GAME_MARGIN_LEFT,
    TOP_MARGIN = PLATFORM_BOARD_GAME_MARGIN_TOP,
};

_Static_assert(VIEWPORT_WIDTH >= SOURCE_WIDTH &&
                   VIEWPORT_HEIGHT >= SOURCE_HEIGHT,
               "display scaler requires a non-shrinking viewport");
_Static_assert(LEFT_MARGIN + VIEWPORT_WIDTH <= LOGICAL_WIDTH &&
                   TOP_MARGIN + VIEWPORT_HEIGHT <= LOGICAL_HEIGHT,
               "game viewport must fit the logical display");
_Static_assert(PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 0U ||
                   PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U,
               "display layout supports native or clockwise-90 scanout");

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

#if PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const destination_row =
            destination + native_y * destination_stride_pixels;
        memset(destination_row, 0,
               (size_t)DESTINATION_WIDTH * sizeof(*destination_row));
        const size_t logical_x = (size_t)LOGICAL_WIDTH - 1U - native_y;
        const size_t viewport_x = logical_x - LEFT_MARGIN;
        if (viewport_x >= VIEWPORT_WIDTH) {
            continue;
        }
        const size_t source_x = viewport_x * SOURCE_WIDTH / VIEWPORT_WIDTH;
        for (size_t viewport_y = 0U; viewport_y < VIEWPORT_HEIGHT;
             ++viewport_y) {
            const size_t source_y =
                viewport_y * SOURCE_HEIGHT / VIEWPORT_HEIGHT;
            destination_row[TOP_MARGIN + viewport_y] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#else
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const destination_row =
            destination + native_y * destination_stride_pixels;
        memset(destination_row, 0,
               (size_t)DESTINATION_WIDTH * sizeof(*destination_row));
        const size_t viewport_y = native_y - TOP_MARGIN;
        if (viewport_y >= VIEWPORT_HEIGHT) {
            continue;
        }
        const size_t source_y = viewport_y * SOURCE_HEIGHT / VIEWPORT_HEIGHT;
        for (size_t viewport_x = 0U; viewport_x < VIEWPORT_WIDTH;
             ++viewport_x) {
            const size_t source_x =
                viewport_x * SOURCE_WIDTH / VIEWPORT_WIDTH;
            destination_row[LEFT_MARGIN + viewport_x] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#endif
    return true;
}

bool platform_display_layout_rgb565_768x480(const uint16_t *source,
                                            size_t source_stride_pixels,
                                            uint16_t *destination,
                                            size_t destination_stride_pixels,
                                            size_t destination_height)
{
    enum { CONTENT_WIDTH = 768, CONTENT_HEIGHT = 480 };
    if (source == NULL || destination == NULL ||
        source_stride_pixels < CONTENT_WIDTH ||
        destination_stride_pixels < DESTINATION_WIDTH ||
        destination_height < DESTINATION_HEIGHT) {
        return false;
    }
#if PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const row = destination + native_y * destination_stride_pixels;
        memset(row, 0, (size_t)DESTINATION_WIDTH * sizeof(*row));
        const size_t logical_x = (size_t)LOGICAL_WIDTH - 1U - native_y;
        const size_t viewport_x = logical_x - LEFT_MARGIN;
        if (viewport_x >= VIEWPORT_WIDTH) {
            continue;
        }
        const size_t source_x = viewport_x * CONTENT_WIDTH / VIEWPORT_WIDTH;
        for (size_t viewport_y = 0U; viewport_y < VIEWPORT_HEIGHT;
             ++viewport_y) {
            const size_t source_y =
                viewport_y * CONTENT_HEIGHT / VIEWPORT_HEIGHT;
            row[TOP_MARGIN + viewport_y] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#else
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const row = destination + native_y * destination_stride_pixels;
        memset(row, 0, (size_t)DESTINATION_WIDTH * sizeof(*row));
        const size_t viewport_y = native_y - TOP_MARGIN;
        if (viewport_y >= VIEWPORT_HEIGHT) {
            continue;
        }
        const size_t source_y = viewport_y * CONTENT_HEIGHT / VIEWPORT_HEIGHT;
        for (size_t viewport_x = 0U; viewport_x < VIEWPORT_WIDTH;
             ++viewport_x) {
            const size_t source_x =
                viewport_x * CONTENT_WIDTH / VIEWPORT_WIDTH;
            row[LEFT_MARGIN + viewport_x] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#endif
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
        HDMI_SCALE = 3,
    };
    const size_t active_row_bytes = HDMI_WIDTH * HDMI_PIXEL_BYTES;
    if (source == NULL || destination == NULL ||
        source_stride_pixels < SOURCE_WIDTH ||
        destination_stride_bytes < active_row_bytes ||
        destination_height < HDMI_HEIGHT) {
        return false;
    }
    for (size_t y = 0U; y < HDMI_HEIGHT; ++y) {
        memset(destination + y * destination_stride_bytes, 0,
               active_row_bytes);
    }
    for (size_t source_y = 0U; source_y < SOURCE_HEIGHT; ++source_y) {
        uint8_t *const first_row = destination +
            (HDMI_TOP_MARGIN + source_y * HDMI_SCALE) *
                destination_stride_bytes;
        const uint16_t *const source_row =
            source + source_y * source_stride_pixels;
        for (size_t source_x = 0U; source_x < SOURCE_WIDTH; ++source_x) {
            const uint16_t pixel = source_row[source_x];
            const uint8_t red = (uint8_t)(
                ((pixel >> 11U) & 0x1fU) * 255U / 31U);
            const uint8_t green = (uint8_t)(
                ((pixel >> 5U) & 0x3fU) * 255U / 63U);
            const uint8_t blue = (uint8_t)((pixel & 0x1fU) * 255U / 31U);
            const size_t destination_x =
                HDMI_LEFT_MARGIN + source_x * HDMI_SCALE;
            for (size_t repeat = 0U; repeat < HDMI_SCALE; ++repeat) {
                uint8_t *const out = first_row +
                    (destination_x + repeat) * HDMI_PIXEL_BYTES;
                out[0] = red;
                out[1] = green;
                out[2] = blue;
            }
        }
        for (size_t repeat = 1U; repeat < HDMI_SCALE; ++repeat) {
            memcpy(first_row + repeat * destination_stride_bytes,
                   first_row, active_row_bytes);
        }
    }
    return true;
}
