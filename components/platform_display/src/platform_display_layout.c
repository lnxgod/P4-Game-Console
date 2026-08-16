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

_Static_assert(VIEWPORT_WIDTH > 0 && VIEWPORT_HEIGHT > 0,
               "selected display is too small for Doom");
_Static_assert(VIEWPORT_WIDTH >= SOURCE_WIDTH &&
                   VIEWPORT_HEIGHT >= SOURCE_HEIGHT,
               "incremental scaler requires a non-shrinking viewport");
_Static_assert(LEFT_MARGIN + VIEWPORT_WIDTH <= LOGICAL_WIDTH &&
                   TOP_MARGIN + VIEWPORT_HEIGHT <= LOGICAL_HEIGHT,
               "game viewport must fit the landscape display");
_Static_assert(PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 0U ||
                   PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U,
               "display layout supports only native or clockwise-90 scanout");
#if PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U
_Static_assert(LOGICAL_WIDTH == DESTINATION_HEIGHT &&
                   LOGICAL_HEIGHT == DESTINATION_WIDTH,
               "clockwise rotation requires swapped logical/native geometry");
#else
_Static_assert(LOGICAL_WIDTH == DESTINATION_WIDTH &&
                   LOGICAL_HEIGHT == DESTINATION_HEIGHT,
               "native scanout requires matching logical/native geometry");
#endif

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

    /* The old generic implementation divided twice for every destination
     * pixel.  These board-specialized loops preserve the exact nearest-
     * neighbor mapping while advancing the scaled axis with a remainder. */
#if PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const destination_row =
            destination + (native_y * destination_stride_pixels);
        memset(destination_row, 0,
               (size_t)DESTINATION_WIDTH * sizeof(*destination_row));
        const size_t logical_x = (size_t)LOGICAL_WIDTH - 1U - native_y;
        const size_t viewport_x = logical_x - LEFT_MARGIN;
        if (viewport_x >= VIEWPORT_WIDTH) {
            continue;
        }
        const size_t source_x =
            (viewport_x * SOURCE_WIDTH) / VIEWPORT_WIDTH;
        size_t source_y = 0U;
        size_t remainder = 0U;
        for (size_t viewport_y = 0U;
             viewport_y < VIEWPORT_HEIGHT; ++viewport_y) {
            destination_row[TOP_MARGIN + viewport_y] =
                source[(source_y * source_stride_pixels) + source_x];
            remainder += SOURCE_HEIGHT;
            if (remainder >= VIEWPORT_HEIGHT) {
                remainder -= VIEWPORT_HEIGHT;
                ++source_y;
            }
        }
    }
#else
    for (size_t native_y = 0U; native_y < DESTINATION_HEIGHT; ++native_y) {
        uint16_t *const destination_row =
            destination + (native_y * destination_stride_pixels);
        memset(destination_row, 0,
               (size_t)DESTINATION_WIDTH * sizeof(*destination_row));
        const size_t viewport_y = native_y - TOP_MARGIN;
        if (viewport_y >= VIEWPORT_HEIGHT) {
            continue;
        }
        const size_t source_y =
            (viewport_y * SOURCE_HEIGHT) / VIEWPORT_HEIGHT;
        size_t source_x = 0U;
        size_t remainder = 0U;
        for (size_t viewport_x = 0U;
             viewport_x < VIEWPORT_WIDTH; ++viewport_x) {
            destination_row[LEFT_MARGIN + viewport_x] =
                source[(source_y * source_stride_pixels) + source_x];
            remainder += SOURCE_WIDTH;
            if (remainder >= VIEWPORT_WIDTH) {
                remainder -= VIEWPORT_WIDTH;
                ++source_x;
            }
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
    enum {
        CONTENT_WIDTH = 768,
        CONTENT_HEIGHT = 480,
    };
    if (source == NULL || destination == NULL ||
        source_stride_pixels < CONTENT_WIDTH ||
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
        const size_t source_x = viewport_x * CONTENT_WIDTH / VIEWPORT_WIDTH;
        for (size_t viewport_y = 0U;
             viewport_y < VIEWPORT_HEIGHT; ++viewport_y) {
            const size_t source_y =
                viewport_y * CONTENT_HEIGHT / VIEWPORT_HEIGHT;
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
        const size_t source_y =
            viewport_y * CONTENT_HEIGHT / VIEWPORT_HEIGHT;
        for (size_t viewport_x = 0U;
             viewport_x < VIEWPORT_WIDTH; ++viewport_x) {
            const size_t source_x =
                viewport_x * CONTENT_WIDTH / VIEWPORT_WIDTH;
            destination_row[LEFT_MARGIN + viewport_x] =
                source[source_y * source_stride_pixels + source_x];
        }
    }
#endif
    return true;
}
