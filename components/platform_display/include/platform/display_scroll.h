// SPDX-License-Identifier: MIT
#ifndef PLATFORM_DISPLAY_SCROLL_H
#define PLATFORM_DISPLAY_SCROLL_H

#include <stdint.h>
#include "platform/display_region.h"

/* A stationary context includes every pixel outside viewport/scrollbar,
 * including focus/selection, chrome and list endpoint controls. */
typedef struct {
    uint64_t context;
    uint64_t previous_context;
    int32_t previous_offset;
    int32_t current_offset;
    platform_display_rgb565_region_t viewport;
    platform_display_rgb565_region_t scrollbar;
    /* Complete changed stationary pixels outside viewport/scrollbar. Empty
     * (width=height=0) requires previous_context==context. A nonempty patch
     * promises every stationary difference between these contexts is inside. */
    platform_display_rgb565_region_t stationary_damage;
} platform_display_ui_scroll_t;

#endif
