#ifndef PLATFORM_TOUCH_FRAME_H
#define PLATFORM_TOUCH_FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/touch.h"

void platform_touch_frame_fail_closed(platform_touch_frame_t *frame,
                                      uint32_t sequence,
                                      int64_t timestamp_us);

bool platform_touch_frame_from_raw(platform_touch_frame_t *frame,
                                   uint32_t sequence,
                                   int64_t timestamp_us,
                                   const uint16_t *x,
                                   const uint16_t *y,
                                   const uint16_t *strength,
                                   uint8_t contact_count);

#endif
