#include "doom/video_indexed.h"

#include "doom_touch/input.h"

#include <string.h>

typedef struct {
    uintptr_t begin;
    uintptr_t end;
} indexed_buffer_range_t;

static bool indexed_buffer_range(const void *buffer, size_t stride,
                                 size_t rows, size_t element_size,
                                 size_t alignment,
                                 indexed_buffer_range_t *range)
{
    if (buffer == NULL || stride > SIZE_MAX / rows ||
        stride * rows > SIZE_MAX / element_size) {
        return false;
    }
    const size_t bytes = stride * rows * element_size;
    const uintptr_t begin = (uintptr_t)buffer;
    if (begin % alignment != 0U || begin > UINTPTR_MAX - bytes) {
        return false;
    }
    range->begin = begin;
    range->end = begin + bytes;
    return true;
}

static uint16_t indexed_pack_rgb565(uint32_t pixel)
{
    const uint16_t red = (uint16_t)((pixel >> 19U) & UINT32_C(0x1f));
    const uint16_t green = (uint16_t)((pixel >> 10U) & UINT32_C(0x3f));
    const uint16_t blue = (uint16_t)((pixel >> 3U) & UINT32_C(0x1f));
    return (uint16_t)((uint16_t)(red << 11U) |
                      (uint16_t)(green << 5U) | blue);
}

bool doom_video_convert_indexed_touch_to_rgb565(
    const uint8_t *source,
    size_t stride,
    const uint32_t palette[256],
    uint32_t active_actions,
    uint16_t *dest,
    size_t pitch,
    uint32_t *row_scratch,
    bool prescale
)
{
    const size_t width = prescale ? 384U : DOOM_TOUCH_FRAME_WIDTH;
    const size_t height = prescale ? 240U : DOOM_TOUCH_FRAME_HEIGHT;
    const uint32_t valid_action_mask =
        (UINT32_C(1) << DOOM_TOUCH_ACTION_COUNT) - UINT32_C(1);
    indexed_buffer_range_t ranges[4];
    if (stride < DOOM_TOUCH_FRAME_WIDTH || pitch < width ||
        (active_actions & ~valid_action_mask) != 0U ||
        !indexed_buffer_range(source, stride, DOOM_TOUCH_FRAME_HEIGHT,
                              sizeof(*source), _Alignof(uint8_t), &ranges[0]) ||
        !indexed_buffer_range(palette, 256U, 1U, sizeof(*palette),
                              _Alignof(uint32_t), &ranges[1]) ||
        !indexed_buffer_range(dest, pitch, height, sizeof(*dest),
                              _Alignof(uint16_t), &ranges[2]) ||
        !indexed_buffer_range(row_scratch, DOOM_TOUCH_FRAME_WIDTH, 1U,
                              sizeof(*row_scratch), _Alignof(uint32_t),
                              &ranges[3])) {
        return false;
    }
    for (size_t first = 0U; first < 4U; ++first) {
        for (size_t second = first + 1U; second < 4U; ++second) {
            if (ranges[first].begin < ranges[second].end &&
                ranges[second].begin < ranges[first].end) {
                return false;
            }
        }
    }

    /* Palette belongs to this immutable packet. Rebuild each call so palette
     * and gamma changes are reflected immediately, without shared state. */
    uint16_t palette565[256];
    for (size_t index = 0U; index < 256U; ++index) {
        palette565[index] = indexed_pack_rgb565(palette[index]);
    }

    size_t destination_y = 0U;
    for (size_t y = 0U; y < DOOM_TOUCH_FRAME_HEIGHT; ++y) {
        const uint8_t *const input = source + y * stride;
        uint16_t *const output = dest + destination_y * pitch;
        if (!doom_touch_overlay_row_may_draw(y)) {
            if (prescale) {
                for (size_t block = 0U; block < 64U; ++block) {
                    const uint8_t *const in = input + block * 5U;
                    uint16_t *const out = output + block * 6U;
                    out[0] = palette565[in[0]];
                    out[1] = out[0];
                    out[2] = palette565[in[1]];
                    out[3] = palette565[in[2]];
                    out[4] = palette565[in[3]];
                    out[5] = palette565[in[4]];
                }
            } else {
                for (size_t x = 0U; x < DOOM_TOUCH_FRAME_WIDTH; ++x) {
                    output[x] = palette565[input[x]];
                }
            }
        } else {
            for (size_t x = 0U; x < DOOM_TOUCH_FRAME_WIDTH; ++x) {
                row_scratch[x] = palette[input[x]];
            }
            /* All row API rejection conditions were checked above; y is
             * bounded. Preserve XRGB blending before RGB565 quantization. */
            (void)doom_touch_overlay_render_row_xrgb8888(
                row_scratch, DOOM_TOUCH_FRAME_WIDTH, y, active_actions);
            if (prescale) {
                for (size_t block = 0U; block < 64U; ++block) {
                    const uint32_t *const in = row_scratch + block * 5U;
                    uint16_t *const out = output + block * 6U;
                    out[0] = indexed_pack_rgb565(in[0]);
                    out[1] = out[0];
                    out[2] = indexed_pack_rgb565(in[1]);
                    out[3] = indexed_pack_rgb565(in[2]);
                    out[4] = indexed_pack_rgb565(in[3]);
                    out[5] = indexed_pack_rgb565(in[4]);
                }
            } else {
                for (size_t x = 0U; x < DOOM_TOUCH_FRAME_WIDTH; ++x) {
                    output[x] = indexed_pack_rgb565(row_scratch[x]);
                }
            }
        }
        ++destination_y;
        if (prescale && y % 5U == 0U) {
            memcpy(dest + destination_y * pitch, output,
                   384U * sizeof(*dest));
            ++destination_y;
        }
    }
    return true;
}
