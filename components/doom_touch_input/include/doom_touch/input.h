#ifndef DOOM_TOUCH_INPUT_H
#define DOOM_TOUCH_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "platform/board.h"
#define DOOM_TOUCH_SCREEN_WIDTH PLATFORM_BOARD_DISPLAY_WIDTH
#define DOOM_TOUCH_SCREEN_HEIGHT PLATFORM_BOARD_DISPLAY_HEIGHT
#else
#ifndef DOOM_TOUCH_SCREEN_WIDTH
#define DOOM_TOUCH_SCREEN_WIDTH 1024U
#define DOOM_TOUCH_SCREEN_HEIGHT 600U
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DOOM_TOUCH_INPUT_VERSION 1U
#define DOOM_TOUCH_MAX_CONTACTS 5U
#define DOOM_TOUCH_EVENT_CAPACITY 32U
#define DOOM_TOUCH_FRAME_WIDTH 320U
#define DOOM_TOUCH_FRAME_HEIGHT 200U

#ifdef ESP_PLATFORM
#define DOOM_TOUCH_VIEWPORT_WIDTH PLATFORM_BOARD_GAME_VIEWPORT_WIDTH
#define DOOM_TOUCH_VIEWPORT_HEIGHT PLATFORM_BOARD_GAME_VIEWPORT_HEIGHT
#define DOOM_TOUCH_VIEWPORT_LEFT PLATFORM_BOARD_GAME_MARGIN_LEFT
#define DOOM_TOUCH_VIEWPORT_TOP PLATFORM_BOARD_GAME_MARGIN_TOP
#else
#define DOOM_TOUCH_VIEWPORT_WIDTH \
    ((DOOM_TOUCH_SCREEN_WIDTH * DOOM_TOUCH_FRAME_HEIGHT) <= \
             (DOOM_TOUCH_SCREEN_HEIGHT * DOOM_TOUCH_FRAME_WIDTH) \
         ? DOOM_TOUCH_SCREEN_WIDTH \
         : (DOOM_TOUCH_SCREEN_HEIGHT * DOOM_TOUCH_FRAME_WIDTH) / \
               DOOM_TOUCH_FRAME_HEIGHT)
#define DOOM_TOUCH_VIEWPORT_HEIGHT \
    ((DOOM_TOUCH_SCREEN_WIDTH * DOOM_TOUCH_FRAME_HEIGHT) <= \
             (DOOM_TOUCH_SCREEN_HEIGHT * DOOM_TOUCH_FRAME_WIDTH) \
         ? (DOOM_TOUCH_SCREEN_WIDTH * DOOM_TOUCH_FRAME_HEIGHT) / \
               DOOM_TOUCH_FRAME_WIDTH \
         : DOOM_TOUCH_SCREEN_HEIGHT)
#define DOOM_TOUCH_VIEWPORT_LEFT \
    ((DOOM_TOUCH_SCREEN_WIDTH - DOOM_TOUCH_VIEWPORT_WIDTH) / 2U)
#define DOOM_TOUCH_VIEWPORT_TOP \
    ((DOOM_TOUCH_SCREEN_HEIGHT - DOOM_TOUCH_VIEWPORT_HEIGHT) / 2U)
#endif

typedef enum {
    DOOM_TOUCH_ACTION_UP = 0,
    DOOM_TOUCH_ACTION_DOWN,
    DOOM_TOUCH_ACTION_LEFT,
    DOOM_TOUCH_ACTION_RIGHT,
    DOOM_TOUCH_ACTION_FIRE,
    DOOM_TOUCH_ACTION_USE,
    DOOM_TOUCH_ACTION_RUN,
    DOOM_TOUCH_ACTION_STRAFE,
    DOOM_TOUCH_ACTION_MENU_ACCEPT,
    DOOM_TOUCH_ACTION_MENU_BACK,
    DOOM_TOUCH_ACTION_MAP,
    DOOM_TOUCH_ACTION_WEAPON_NEXT,
    DOOM_TOUCH_ACTION_WEAPON_PREVIOUS,
    DOOM_TOUCH_ACTION_PAUSE,
    DOOM_TOUCH_ACTION_COUNT
} doom_touch_action_t;

typedef struct {
    uint16_t x;
    uint16_t y;
} doom_touch_contact_t;

typedef struct {
    uint16_t version;
    uint16_t size;
    uint8_t contact_count;
    uint8_t valid;
    uint8_t reserved[2];
    doom_touch_contact_t contacts[DOOM_TOUCH_MAX_CONTACTS];
} doom_touch_frame_t;

typedef struct {
    uint8_t action;
    uint8_t pressed;
    uint8_t reserved[2];
} doom_touch_event_t;

typedef struct {
    uint16_t version;
    uint16_t size;
    uint32_t active_actions;
    doom_touch_event_t events[DOOM_TOUCH_EVENT_CAPACITY];
    uint8_t read_index;
    uint8_t event_count;
    uint8_t reserved[2];
} doom_touch_input_t;

void doom_touch_frame_init(doom_touch_frame_t *frame);
void doom_touch_input_init(doom_touch_input_t *input);

/**
 * Convert one complete touch snapshot into bounded Doom action transitions.
 *
 * Call only after draining prior events. Releases are queued before presses.
 * A NULL, invalid, out-of-range, or malformed frame is treated as neutral so
 * a failed touch read cannot leave a Doom key held.
 */
bool doom_touch_input_update(
    doom_touch_input_t *input,
    const doom_touch_frame_t *frame
);

bool doom_touch_input_next(
    doom_touch_input_t *input,
    doom_touch_event_t *event
);

bool doom_touch_input_idle(const doom_touch_input_t *input);

/**
 * Copy one 320x200 XRGB8888 Doom frame and add the touch-control overlay.
 *
 * Source and destination may be identical. Strides are measured in pixels and
 * must be at least 320. The overlay is intentionally rendered in the game's
 * logical surface so every board uses the same aspect-fit display adapter.
 */
bool doom_touch_overlay_render_xrgb8888(
    const uint32_t *source,
    size_t source_stride_pixels,
    uint32_t *destination,
    size_t destination_stride_pixels,
    uint32_t active_actions
);

/**
 * Add the exact full-frame touch overlay to logical row y in place.
 *
 * row must address at least row_pixels aligned uint32_t words. row_pixels
 * must be at least 320; only the first 320 words are modified. y is in
 * [0, 200), and active_actions may contain only defined action bits.
 * Invalid arguments return false before any write. The caller owns the row
 * for the entire call; this function neither retains nor allocates storage.
 */
bool doom_touch_overlay_render_row_xrgb8888(
    uint32_t *row,
    size_t row_pixels,
    size_t y,
    uint32_t active_actions
);

/**
 * Conservative row coverage for the overlay, independent of active actions.
 * False guarantees that neither row nor full-frame rendering changes this
 * logical row for any valid action mask. True may include untouched pixels
 * or rows. Invalid y values return true so callers cannot skip validation.
 */
bool doom_touch_overlay_row_may_draw(size_t y);

#ifdef __cplusplus
}
#endif

#endif
