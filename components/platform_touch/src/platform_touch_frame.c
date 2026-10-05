#include "platform_touch_frame.h"

#include <stddef.h>
#include <string.h>

#include "sdkconfig.h"
#include "platform/board.h"

_Static_assert(PLATFORM_TOUCH_MAX_CONTACTS == 5U,
               "GT911 driver review is bounded to five contacts");
#if CONFIG_P4_BOARD_M5STACK_TAB5
_Static_assert(PLATFORM_TOUCH_WIDTH == 1280U && PLATFORM_TOUCH_HEIGHT == 720U &&
    PLATFORM_TOUCH_NATIVE_WIDTH == 720U && PLATFORM_TOUCH_NATIVE_HEIGHT == 1280U &&
    PLATFORM_TOUCH_ROTATION_CW_DEGREES == 90U, "Tab5 touch geometry");
#elif CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
_Static_assert(PLATFORM_TOUCH_WIDTH == 800U &&
                   PLATFORM_TOUCH_HEIGHT == 480U &&
                   PLATFORM_TOUCH_NATIVE_WIDTH == 480U &&
                   PLATFORM_TOUCH_NATIVE_HEIGHT == 800U &&
                   PLATFORM_TOUCH_ROTATION_CW_DEGREES == 90U,
               "Waveshare touch must map native portrait into landscape");
#else
_Static_assert(PLATFORM_TOUCH_WIDTH == 1024U &&
                   PLATFORM_TOUCH_HEIGHT == 600U &&
                   PLATFORM_TOUCH_NATIVE_WIDTH == 1024U &&
                   PLATFORM_TOUCH_NATIVE_HEIGHT == 600U &&
                   PLATFORM_TOUCH_ROTATION_CW_DEGREES == 0U,
               "touch coordinates must match the Elecrow panel geometry");
#endif

bool platform_touch_coordinates_native_to_logical(uint16_t *x,
                                                  uint16_t *y,
                                                  uint8_t contact_count)
{
    if (contact_count > PLATFORM_TOUCH_MAX_CONTACTS ||
        (contact_count > 0U && (x == NULL || y == NULL))) {
        return false;
    }
    for (uint8_t index = 0U; index < contact_count; ++index) {
        if (x[index] >= PLATFORM_TOUCH_NATIVE_WIDTH ||
            y[index] >= PLATFORM_TOUCH_NATIVE_HEIGHT) {
            return false;
        }
#if PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U
        const uint16_t native_x = x[index];
        const uint16_t native_y = y[index];
        x[index] = (uint16_t)(PLATFORM_TOUCH_WIDTH - 1U - native_y);
        y[index] = native_x;
#endif
    }
    return true;
}

void platform_touch_frame_fail_closed(platform_touch_frame_t *frame,
                                      uint32_t sequence,
                                      int64_t timestamp_us)
{
    if (frame == NULL) {
        return;
    }
    memset(frame, 0, sizeof(*frame));
    frame->version = PLATFORM_TOUCH_VERSION;
    frame->size = (uint16_t)sizeof(*frame);
    frame->sequence = sequence;
    frame->timestamp_us = timestamp_us;
}

void platform_touch_frame_neutral(platform_touch_frame_t *frame)
{
    platform_touch_frame_fail_closed(frame, 0U, 0);
    if (frame != NULL) {
        frame->valid = 1U;
    }
}

bool platform_touch_frame_from_raw(platform_touch_frame_t *frame,
                                   uint32_t sequence,
                                   int64_t timestamp_us,
                                   const uint16_t *x,
                                   const uint16_t *y,
                                   const uint16_t *strength,
                                   uint8_t contact_count)
{
    platform_touch_frame_fail_closed(frame, sequence, timestamp_us);
    if (frame == NULL || contact_count > PLATFORM_TOUCH_MAX_CONTACTS ||
        (contact_count > 0U && (x == NULL || y == NULL))) {
        return false;
    }
    for (uint8_t index = 0U; index < contact_count; ++index) {
        if (x[index] >= PLATFORM_TOUCH_WIDTH ||
            y[index] >= PLATFORM_TOUCH_HEIGHT) {
            return false;
        }
    }
    for (uint8_t index = 0U; index < contact_count; ++index) {
        frame->contacts[index].x = x[index];
        frame->contacts[index].y = y[index];
        frame->contacts[index].strength =
            strength == NULL ? 0U : strength[index];
    }
    frame->contact_count = contact_count;
    frame->valid = 1U;
    return true;
}
