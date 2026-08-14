#include "platform_touch_frame.h"

#include <stddef.h>
#include <string.h>

_Static_assert(PLATFORM_TOUCH_MAX_CONTACTS == 5U,
               "GT911 driver review is bounded to five contacts");
_Static_assert(PLATFORM_TOUCH_WIDTH == 1024U &&
                   PLATFORM_TOUCH_HEIGHT == 600U,
               "touch coordinates must match the proven panel geometry");

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
