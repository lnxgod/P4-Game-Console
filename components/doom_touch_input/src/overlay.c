#include "doom_touch/input.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    int16_t center_x;
    int16_t center_y;
    int16_t radius;
    doom_touch_action_t action;
} logical_control_t;

/* Screen coordinates mapped through the proven 32 + 3*x viewport. */
static const logical_control_t s_controls[] = {
    {49, 148, 50, DOOM_TOUCH_ACTION_UP},
    {289, 151, 35, DOOM_TOUCH_ACTION_FIRE},
    {237, 162, 19, DOOM_TOUCH_ACTION_USE},
    {246, 117, 18, DOOM_TOUCH_ACTION_RUN},
    {203, 143, 17, DOOM_TOUCH_ACTION_STRAFE},
    {16, 23, 14, DOOM_TOUCH_ACTION_MAP},
    {49, 23, 14, DOOM_TOUCH_ACTION_MENU_BACK},
    {197, 23, 14, DOOM_TOUCH_ACTION_WEAPON_PREVIOUS},
    {230, 23, 14, DOOM_TOUCH_ACTION_WEAPON_NEXT},
    {269, 23, 14, DOOM_TOUCH_ACTION_MENU_ACCEPT},
    {304, 23, 14, DOOM_TOUCH_ACTION_PAUSE},
};

static uint32_t action_bit(doom_touch_action_t action)
{
    return UINT32_C(1) << (unsigned)action;
}

static bool frame_range(
    const uint32_t *pixels,
    size_t stride,
    uintptr_t *out_begin,
    uintptr_t *out_end
)
{
    if (pixels == NULL || out_begin == NULL || out_end == NULL ||
        stride < DOOM_TOUCH_FRAME_WIDTH ||
        stride > (SIZE_MAX / DOOM_TOUCH_FRAME_HEIGHT) ||
        stride * DOOM_TOUCH_FRAME_HEIGHT > (SIZE_MAX / sizeof(*pixels))) {
        return false;
    }
    const size_t bytes =
        stride * DOOM_TOUCH_FRAME_HEIGHT * sizeof(*pixels);
    const uintptr_t begin = (uintptr_t)pixels;
    if (begin > UINTPTR_MAX - bytes) {
        return false;
    }
    *out_begin = begin;
    *out_end = begin + bytes;
    return true;
}

static uint8_t blend_channel(uint8_t base, uint8_t overlay, bool active)
{
    const unsigned base_weight = active ? 1U : 3U;
    const unsigned overlay_weight = active ? 2U : 1U;
    return (uint8_t)(((unsigned)base * base_weight +
                      (unsigned)overlay * overlay_weight) /
                     (base_weight + overlay_weight));
}

static uint32_t blend_pixel(uint32_t base, uint32_t overlay, bool active)
{
    const uint8_t red = blend_channel((uint8_t)(base >> 16U),
                                      (uint8_t)(overlay >> 16U), active);
    const uint8_t green = blend_channel((uint8_t)(base >> 8U),
                                        (uint8_t)(overlay >> 8U), active);
    const uint8_t blue = blend_channel((uint8_t)base,
                                       (uint8_t)overlay, active);
    return ((uint32_t)red << 16U) | ((uint32_t)green << 8U) | blue;
}

static void draw_circle(
    uint32_t *destination,
    size_t stride,
    const logical_control_t *control,
    bool active,
    uint32_t color
)
{
    const int radius = control->radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = (int)control->center_y + dy;
        if (y < 0 || y >= (int)DOOM_TOUCH_FRAME_HEIGHT) {
            continue;
        }
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = (int)control->center_x + dx;
            if (x < 0 || x >= (int)DOOM_TOUCH_FRAME_WIDTH) {
                continue;
            }
            const int distance = dx * dx + dy * dy;
            const int outer = radius * radius;
            const int inner_radius = radius > 3 ? radius - 3 : 0;
            const int inner = inner_radius * inner_radius;
            if (distance <= outer && (active || distance >= inner)) {
                uint32_t *const pixel = &destination[(size_t)y * stride +
                                                     (size_t)x];
                *pixel = blend_pixel(*pixel, color, active);
            }
        }
    }
}

static void draw_dpad(
    uint32_t *destination,
    size_t stride,
    uint32_t active_actions
)
{
    const int center_x = 49;
    const int center_y = 148;
    const uint32_t neutral_color = UINT32_C(0x00d0d0d0);
    for (int y = 103; y <= 193; ++y) {
        for (int x = 4; x <= 94; ++x) {
            const int dx = x - center_x;
            const int dy = y - center_y;
            const int abs_x = dx < 0 ? -dx : dx;
            const int abs_y = dy < 0 ? -dy : dy;
            const bool cross = (abs_x <= 14 && abs_y <= 44) ||
                               (abs_y <= 14 && abs_x <= 44);
            const bool border = cross &&
                (abs_x >= 11 || abs_y >= 11 || abs_x >= 41 || abs_y >= 41);
            if (!cross) {
                continue;
            }
            doom_touch_action_t action = DOOM_TOUCH_ACTION_UP;
            if (abs_x > abs_y) {
                action = dx < 0 ? DOOM_TOUCH_ACTION_LEFT
                                : DOOM_TOUCH_ACTION_RIGHT;
            } else {
                action = dy < 0 ? DOOM_TOUCH_ACTION_UP
                                : DOOM_TOUCH_ACTION_DOWN;
            }
            const bool active =
                (active_actions & action_bit(action)) != 0U;
            if (active || border) {
                uint32_t *const pixel = &destination[(size_t)y * stride +
                                                     (size_t)x];
                *pixel = blend_pixel(*pixel, neutral_color, active);
            }
        }
    }
}

bool doom_touch_overlay_render_xrgb8888(
    const uint32_t *source,
    size_t source_stride_pixels,
    uint32_t *destination,
    size_t destination_stride_pixels,
    uint32_t active_actions
)
{
    const uint32_t valid_action_mask =
        (UINT32_C(1) << DOOM_TOUCH_ACTION_COUNT) - UINT32_C(1);
    uintptr_t source_begin = 0U;
    uintptr_t source_end = 0U;
    uintptr_t destination_begin = 0U;
    uintptr_t destination_end = 0U;
    if (source == NULL || destination == NULL ||
        source_stride_pixels < DOOM_TOUCH_FRAME_WIDTH ||
        destination_stride_pixels < DOOM_TOUCH_FRAME_WIDTH ||
        (source == destination &&
         source_stride_pixels != destination_stride_pixels) ||
        !frame_range(source, source_stride_pixels,
                     &source_begin, &source_end) ||
        !frame_range(destination, destination_stride_pixels,
                     &destination_begin, &destination_end) ||
        (source != destination &&
         source_begin < destination_end &&
         destination_begin < source_end) ||
        (active_actions & ~valid_action_mask) != 0U) {
        return false;
    }

    if (source != destination || source_stride_pixels != destination_stride_pixels) {
        for (size_t y = 0U; y < DOOM_TOUCH_FRAME_HEIGHT; ++y) {
            memmove(&destination[y * destination_stride_pixels],
                    &source[y * source_stride_pixels],
                    DOOM_TOUCH_FRAME_WIDTH * sizeof(uint32_t));
        }
    }

    draw_dpad(destination, destination_stride_pixels, active_actions);
    for (size_t index = 1U;
         index < sizeof(s_controls) / sizeof(s_controls[0]);
         ++index) {
        const logical_control_t *const control = &s_controls[index];
        const bool active =
            (active_actions & action_bit(control->action)) != 0U;
        uint32_t color = UINT32_C(0x00d0d0d0);
        if (control->action == DOOM_TOUCH_ACTION_FIRE) {
            color = UINT32_C(0x00ff3030);
        } else if (control->action == DOOM_TOUCH_ACTION_USE) {
            color = UINT32_C(0x0030d0ff);
        } else if (control->action == DOOM_TOUCH_ACTION_RUN ||
                   control->action == DOOM_TOUCH_ACTION_STRAFE) {
            color = UINT32_C(0x00ffd030);
        }
        draw_circle(destination, destination_stride_pixels, control,
                    active, color);
    }
    return true;
}
