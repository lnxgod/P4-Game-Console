#include "doom_touch/input.h"

#include <stdint.h>
#include <string.h>

enum {
    DPAD_CENTER_Y = 148,
    DPAD_RADIUS = 44,
    LETTER_CENTER_Y = 23,
    LETTER_HALF_HEIGHT = 7,
};

typedef struct {
    int8_t outer;
    int8_t hole;
} circle_span_t;

/* For each absolute y: outer = floor(sqrt(r*r - y*y)), and hole =
 * floor(sqrt((r-3)*(r-3) - 1 - y*y)), or -1 when there is no hole.
 * The minus one preserves the original inclusive inner ring boundary. */
static const circle_span_t s_circle_35[36] = {
    {35, 31}, {34, 31}, {34, 31}, {34, 31}, {34, 31}, {34, 31},
    {34, 31}, {34, 31}, {34, 30}, {33, 30}, {33, 30}, {33, 30},
    {32, 29}, {32, 29}, {32, 28}, {31, 28}, {31, 27}, {30, 27},
    {30, 26}, {29, 25}, {28, 24}, {28, 24}, {27, 23}, {26, 22},
    {25, 21}, {24, 19}, {23, 18}, {22, 17}, {21, 15}, {19, 13},
    {18, 11}, {16, 7}, {14, -1}, {11, -1}, {8, -1}, {0, -1},
};

static const circle_span_t s_circle_19[20] = {
    {19, 15}, {18, 15}, {18, 15}, {18, 15}, {18, 15}, {18, 15},
    {18, 14}, {17, 14}, {17, 13}, {16, 13}, {16, 12}, {15, 11},
    {14, 10}, {13, 9}, {12, 7}, {11, 5}, {10, -1}, {8, -1},
    {6, -1}, {0, -1},
};

static const circle_span_t s_circle_18[19] = {
    {18, 14}, {17, 14}, {17, 14}, {17, 14}, {17, 14}, {17, 14},
    {16, 13}, {16, 13}, {16, 12}, {15, 11}, {14, 11}, {14, 10},
    {13, 8}, {12, 7}, {11, 5}, {9, -1}, {8, -1}, {5, -1},
    {0, -1},
};

static const circle_span_t s_circle_17[18] = {
    {17, 13}, {16, 13}, {16, 13}, {16, 13}, {16, 13}, {16, 13},
    {15, 12}, {15, 12}, {15, 11}, {14, 10}, {13, 9}, {12, 8},
    {12, 7}, {10, 5}, {9, -1}, {8, -1}, {5, -1}, {0, -1},
};

static const circle_span_t s_circle_14[15] = {
    {14, 10}, {13, 10}, {13, 10}, {13, 10}, {13, 10}, {13, 9},
    {12, 9}, {12, 8}, {11, 7}, {10, 6}, {9, 4}, {8, -1},
    {7, -1}, {5, -1}, {0, -1},
};

typedef struct {
    int16_t center_x;
    int16_t center_y;
    int16_t radius;
    doom_touch_action_t action;
    const circle_span_t *spans;
    uint32_t color;
} logical_control_t;

/* Controls live in the shared 320x200 surface and map through its viewport. */
static const logical_control_t s_controls[] = {
    {289, 151, 35, DOOM_TOUCH_ACTION_FIRE, s_circle_35, UINT32_C(0x00ff3030)},
    {237, 162, 19, DOOM_TOUCH_ACTION_USE, s_circle_19, UINT32_C(0x0030d0ff)},
    {246, 117, 18, DOOM_TOUCH_ACTION_RUN, s_circle_18, UINT32_C(0x00ffd030)},
    {203, 143, 17, DOOM_TOUCH_ACTION_STRAFE, s_circle_17, UINT32_C(0x00ffd030)},
    {16, 23, 14, DOOM_TOUCH_ACTION_MAP, s_circle_14, UINT32_C(0x00d0d0d0)},
    {49, 23, 14, DOOM_TOUCH_ACTION_MENU_BACK, s_circle_14, UINT32_C(0x00d0d0d0)},
    {197, 23, 14, DOOM_TOUCH_ACTION_WEAPON_PREVIOUS, s_circle_14, UINT32_C(0x00d0d0d0)},
    {230, 23, 14, DOOM_TOUCH_ACTION_WEAPON_NEXT, s_circle_14, UINT32_C(0x00d0d0d0)},
    {269, 23, 14, DOOM_TOUCH_ACTION_MENU_ACCEPT, s_circle_14, UINT32_C(0x00d0d0d0)},
    {304, 23, 14, DOOM_TOUCH_ACTION_PAUSE, s_circle_14, UINT32_C(0x00d0d0d0)},
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

static void draw_span(
    uint32_t *destination,
    size_t stride,
    int y,
    int left,
    int right,
    bool active,
    uint32_t color
)
{
    if (left < 0) {
        left = 0;
    }
    if (right >= (int)DOOM_TOUCH_FRAME_WIDTH) {
        right = (int)DOOM_TOUCH_FRAME_WIDTH - 1;
    }
    if (left > right) {
        return;
    }
    uint32_t *pixel = &destination[(size_t)y * stride + (size_t)left];
    uint32_t *const end = pixel + (size_t)(right - left + 1);
    if (active) {
        const uint32_t red = ((color >> 16U) & UINT32_C(0xff)) * 2U;
        const uint32_t green = ((color >> 8U) & UINT32_C(0xff)) * 2U;
        const uint32_t blue = (color & UINT32_C(0xff)) * 2U;
        for (; pixel != end; ++pixel) {
            const uint32_t base = *pixel;
            /* Constant divisors avoid three variable RV32 divisions per pixel. */
            *pixel = (((((base >> 16U) & UINT32_C(0xff)) + red) / 3U) << 16U) |
                     (((((base >> 8U) & UINT32_C(0xff)) + green) / 3U) << 8U) |
                     (((base & UINT32_C(0xff)) + blue) / 3U);
        }
    } else {
        const uint32_t red_blue = color & UINT32_C(0x00ff00ff);
        const uint32_t green = color & UINT32_C(0x0000ff00);
        for (; pixel != end; ++pixel) {
            const uint32_t base = *pixel;
            /* Two separated 16-bit lanes cannot carry: 3*255 + 255 <= 1020. */
            *pixel = ((((base & UINT32_C(0x00ff00ff)) * 3U + red_blue) >> 2U) &
                      UINT32_C(0x00ff00ff)) |
                     ((((base & UINT32_C(0x0000ff00)) * 3U + green) >> 2U) &
                      UINT32_C(0x0000ff00));
        }
    }
}

static void draw_circle(
    uint32_t *destination,
    size_t stride,
    const logical_control_t *control,
    bool active
)
{
    const int radius = control->radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = (int)control->center_y + dy;
        if (y < 0 || y >= (int)DOOM_TOUCH_FRAME_HEIGHT) {
            continue;
        }
        const circle_span_t span = control->spans[dy < 0 ? -dy : dy];
        const int left = (int)control->center_x - span.outer;
        const int right = (int)control->center_x + span.outer;
        if (active || span.hole < 0) {
            draw_span(destination, stride, y, left, right, active, control->color);
        } else {
            draw_span(destination, stride, y, left,
                      (int)control->center_x - span.hole - 1, false, control->color);
            draw_span(destination, stride, y,
                      (int)control->center_x + span.hole + 1, right, false,
                      control->color);
        }
    }
}

static void draw_letter_5x7(
    uint32_t *destination,
    size_t stride,
    int center_x,
    int center_y,
    const uint8_t rows[7]
)
{
    const int left = center_x - 5;
    const int top = center_y - LETTER_HALF_HEIGHT;
    for (int row = 0; row < LETTER_HALF_HEIGHT; ++row) {
        for (int column = 0; column < 5; ++column) {
            if ((rows[row] & (UINT8_C(1) << (4 - column))) == 0U) {
                continue;
            }
            for (int scale_y = 0; scale_y < 2; ++scale_y) {
                for (int scale_x = 0; scale_x < 2; ++scale_x) {
                    const int x = left + column * 2 + scale_x;
                    const int y = top + row * 2 + scale_y;
                    if (x >= 0 && x < (int)DOOM_TOUCH_FRAME_WIDTH &&
                        y >= 0 && y < (int)DOOM_TOUCH_FRAME_HEIGHT) {
                        destination[(size_t)y * stride + (size_t)x] =
                            UINT32_C(0x00ffffff);
                    }
                }
            }
        }
    }
}

static void draw_dpad_span(
    uint32_t *destination,
    size_t stride,
    int dy,
    int left,
    int right,
    bool active
)
{
    const int center_x = 49;
    const int center_y = DPAD_CENTER_Y;
    const uint32_t color = UINT32_C(0x00d0d0d0);
    if (active || dy <= -11 || dy >= 11) {
        draw_span(destination, stride, center_y + dy,
                  center_x + left, center_x + right, active, color);
    } else {
        /* The inactive cross omits only its central 21x21 square. */
        if (left <= -11) {
            draw_span(destination, stride, center_y + dy, center_x + left,
                      center_x + (right < -11 ? right : -11), false, color);
        }
        if (right >= 11) {
            draw_span(destination, stride, center_y + dy,
                      center_x + (left > 11 ? left : 11), center_x + right,
                      false, color);
        }
    }
}

static void draw_dpad(
    uint32_t *destination,
    size_t stride,
    uint32_t active_actions
)
{
    const bool up = (active_actions & action_bit(DOOM_TOUCH_ACTION_UP)) != 0U;
    const bool down = (active_actions & action_bit(DOOM_TOUCH_ACTION_DOWN)) != 0U;
    const bool left = (active_actions & action_bit(DOOM_TOUCH_ACTION_LEFT)) != 0U;
    const bool right = (active_actions & action_bit(DOOM_TOUCH_ACTION_RIGHT)) != 0U;
    for (int dy = -DPAD_RADIUS; dy <= DPAD_RADIUS; ++dy) {
        const int abs_y = dy < 0 ? -dy : dy;
        const int extent = abs_y <= 14 ? DPAD_RADIUS : 14;
        const int middle = abs_y < extent ? abs_y : extent;
        /* Ties belong to the vertical direction, including DOWN at the center. */
        draw_dpad_span(destination, stride, dy, -extent, -middle - 1, left);
        draw_dpad_span(destination, stride, dy, -middle, middle,
                       dy < 0 ? up : down);
        draw_dpad_span(destination, stride, dy, middle + 1, extent, right);
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

    if (source != destination) {
        /* Validation above excludes overlap, including row padding. */
        if (source_stride_pixels == DOOM_TOUCH_FRAME_WIDTH &&
            destination_stride_pixels == DOOM_TOUCH_FRAME_WIDTH) {
            memcpy(destination, source,
                   DOOM_TOUCH_FRAME_WIDTH * DOOM_TOUCH_FRAME_HEIGHT *
                       sizeof(uint32_t));
        } else {
            for (size_t y = 0U; y < DOOM_TOUCH_FRAME_HEIGHT; ++y) {
                memcpy(&destination[y * destination_stride_pixels],
                       &source[y * source_stride_pixels],
                       DOOM_TOUCH_FRAME_WIDTH * sizeof(uint32_t));
            }
        }
    }

    draw_dpad(destination, destination_stride_pixels, active_actions);
    for (size_t index = 0U;
         index < sizeof(s_controls) / sizeof(s_controls[0]);
         ++index) {
        const logical_control_t *const control = &s_controls[index];
        const bool active =
            (active_actions & action_bit(control->action)) != 0U;
        draw_circle(destination, destination_stride_pixels, control, active);
    }
    static const uint8_t yes_rows[7] = {
        UINT8_C(0x11), UINT8_C(0x11), UINT8_C(0x0a), UINT8_C(0x04),
        UINT8_C(0x04), UINT8_C(0x04), UINT8_C(0x04),
    };
    static const uint8_t no_rows[7] = {
        UINT8_C(0x11), UINT8_C(0x19), UINT8_C(0x15), UINT8_C(0x13),
        UINT8_C(0x11), UINT8_C(0x11), UINT8_C(0x11),
    };
    draw_letter_5x7(destination, destination_stride_pixels,
                    269, LETTER_CENTER_Y, yes_rows);
    draw_letter_5x7(destination, destination_stride_pixels,
                    49, LETTER_CENTER_Y, no_rows);
    return true;
}

/* Row-local geometry deliberately reuses the full renderer's span blending,
 * circle tables and control order. No pointer to a fictitious full frame is
 * formed: draw_span receives this row with a logical storage y of zero. */
static void draw_row_circle(uint32_t *row, size_t row_pixels, size_t y,
                            const logical_control_t *control, bool active)
{
    const int dy = (int)y - (int)control->center_y;
    const int absolute_y = dy < 0 ? -dy : dy;
    if (absolute_y > control->radius) {
        return;
    }
    const circle_span_t span = control->spans[absolute_y];
    const int left = (int)control->center_x - span.outer;
    const int right = (int)control->center_x + span.outer;
    if (active || span.hole < 0) {
        draw_span(row, row_pixels, 0, left, right, active, control->color);
    } else {
        draw_span(row, row_pixels, 0, left,
                  (int)control->center_x - span.hole - 1, false, control->color);
        draw_span(row, row_pixels, 0,
                  (int)control->center_x + span.hole + 1, right, false,
                  control->color);
    }
}

static void draw_row_letter_5x7(uint32_t *row, size_t y, int center_x,
                                int center_y, const uint8_t glyph_rows[7])
{
    const int glyph_y = (int)y - (center_y - LETTER_HALF_HEIGHT);
    if (glyph_y < 0 || glyph_y >= 2 * LETTER_HALF_HEIGHT) {
        return;
    }
    const int left = center_x - 5;
    const uint8_t bits = glyph_rows[glyph_y / 2];
    for (int column = 0; column < 5; ++column) {
        if ((bits & (UINT8_C(1) << (4 - column))) == 0U) {
            continue;
        }
        for (int scale_x = 0; scale_x < 2; ++scale_x) {
            const int x = left + column * 2 + scale_x;
            if (x >= 0 && x < (int)DOOM_TOUCH_FRAME_WIDTH) {
                row[(size_t)x] = UINT32_C(0x00ffffff);
            }
        }
    }
}

static void draw_row_dpad_span(uint32_t *row, size_t row_pixels, int dy,
                               int left, int right, bool active)
{
    const int center_x = 49;
    const uint32_t color = UINT32_C(0x00d0d0d0);
    if (active || dy <= -11 || dy >= 11) {
        draw_span(row, row_pixels, 0,
                  center_x + left, center_x + right, active, color);
    } else {
        if (left <= -11) {
            draw_span(row, row_pixels, 0, center_x + left,
                      center_x + (right < -11 ? right : -11), false, color);
        }
        if (right >= 11) {
            draw_span(row, row_pixels, 0,
                      center_x + (left > 11 ? left : 11), center_x + right,
                      false, color);
        }
    }
}

static void draw_row_dpad(uint32_t *row, size_t row_pixels, size_t y,
                          uint32_t active_actions)
{
    const int dy = (int)y - DPAD_CENTER_Y;
    if (dy < -DPAD_RADIUS || dy > DPAD_RADIUS) {
        return;
    }
    const bool up = (active_actions & action_bit(DOOM_TOUCH_ACTION_UP)) != 0U;
    const bool down = (active_actions & action_bit(DOOM_TOUCH_ACTION_DOWN)) != 0U;
    const bool left = (active_actions & action_bit(DOOM_TOUCH_ACTION_LEFT)) != 0U;
    const bool right = (active_actions & action_bit(DOOM_TOUCH_ACTION_RIGHT)) != 0U;
    const int abs_y = dy < 0 ? -dy : dy;
    const int extent = abs_y <= 14 ? DPAD_RADIUS : 14;
    const int middle = abs_y < extent ? abs_y : extent;
    draw_row_dpad_span(row, row_pixels, dy, -extent, -middle - 1, left);
    draw_row_dpad_span(row, row_pixels, dy, -middle, middle, dy < 0 ? up : down);
    draw_row_dpad_span(row, row_pixels, dy, middle + 1, extent, right);
}

bool doom_touch_overlay_row_may_draw(size_t y)
{
    if (y >= DOOM_TOUCH_FRAME_HEIGHT) {
        return true;
    }
    const int row = (int)y;
    if ((row >= DPAD_CENTER_Y - DPAD_RADIUS &&
         row <= DPAD_CENTER_Y + DPAD_RADIUS) ||
        (row >= LETTER_CENTER_Y - LETTER_HALF_HEIGHT &&
         row < LETTER_CENTER_Y + LETTER_HALF_HEIGHT)) {
        return true;
    }
    for (size_t index = 0U;
         index < sizeof(s_controls) / sizeof(s_controls[0]); ++index) {
        const logical_control_t *const control = &s_controls[index];
        if (row >= control->center_y - control->radius &&
            row <= control->center_y + control->radius) {
            return true;
        }
    }
    return false;
}

bool doom_touch_overlay_render_row_xrgb8888(
    uint32_t *row,
    size_t row_pixels,
    size_t y,
    uint32_t active_actions
)
{
    const uint32_t valid_action_mask =
        (UINT32_C(1) << DOOM_TOUCH_ACTION_COUNT) - UINT32_C(1);
    if (row == NULL || row_pixels < DOOM_TOUCH_FRAME_WIDTH ||
        y >= DOOM_TOUCH_FRAME_HEIGHT ||
        row_pixels > SIZE_MAX / sizeof(*row) ||
        (uintptr_t)row % _Alignof(uint32_t) != 0U ||
        (uintptr_t)row > UINTPTR_MAX - row_pixels * sizeof(*row) ||
        (active_actions & ~valid_action_mask) != 0U) {
        return false;
    }
    draw_row_dpad(row, row_pixels, y, active_actions);
    for (size_t index = 0U;
         index < sizeof(s_controls) / sizeof(s_controls[0]); ++index) {
        const logical_control_t *const control = &s_controls[index];
        const bool active = (active_actions & action_bit(control->action)) != 0U;
        draw_row_circle(row, row_pixels, y, control, active);
    }
    static const uint8_t yes_rows[7] = {
        UINT8_C(0x11), UINT8_C(0x11), UINT8_C(0x0a), UINT8_C(0x04),
        UINT8_C(0x04), UINT8_C(0x04), UINT8_C(0x04),
    };
    static const uint8_t no_rows[7] = {
        UINT8_C(0x11), UINT8_C(0x19), UINT8_C(0x15), UINT8_C(0x13),
        UINT8_C(0x11), UINT8_C(0x11), UINT8_C(0x11),
    };
    draw_row_letter_5x7(row, y, 269, LETTER_CENTER_Y, yes_rows);
    draw_row_letter_5x7(row, y, 49, LETTER_CENTER_Y, no_rows);
    return true;
}
