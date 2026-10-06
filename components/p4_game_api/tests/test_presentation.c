// SPDX-License-Identifier: MIT
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include "p4/presentation.h"

static void verify(unsigned width, unsigned height)
{
    const size_t stride = (size_t)width + 7U;
    const size_t count = stride * height + 2U;
    uint16_t *memory = malloc(count * sizeof(*memory));
    assert(memory != NULL);
    for (size_t i = 0; i < count; ++i) memory[i] = 0x1234U;
    p4_game_surface_t s = { .pixels = memory + 1, .stride_pixels = stride,
        .width = (uint16_t)width, .height = (uint16_t)height };
    p4_draw_clear(&s, 0U);
    const uint16_t sprite[4] = {0xffffU, 0xf800U, 0x07e0U, 0U};
    p4_ui_sprite(&s, -2, -2, 4, 4, sprite, 2, 2, true, 0U);
    assert(s.pixels[0] == 0U); // clipped source bottom-right is transparent
    p4_ui_sprite(&s, 0, 0, 4, 4, sprite, 2, 2, false, 0U);
    assert(s.pixels[0] == 0xffffU && s.pixels[2] == 0xf800U);
    assert(s.pixels[2U * stride] == 0x07e0U && s.pixels[2U * stride + 2U] == 0U);
    p4_ui_round_rect(&s, (int)width - 4, (int)height - 4, 8, 8, 2, 0xffffU);
    p4_ui_round_rect(&s, -4, -4, 8, 8, 50, 0xf800U);
    p4_ui_text(&s, -4, -4, "Clipped", 0xffffU, 28U, 7U);
    p4_ui_text(&s, (int)width - 4, (int)height - 4, "Edge", 0xffffU, 28U, 4U);
    p4_ui_text(&s, 20, 20, "Sharp A9", 0xffffU, 28U, 8U);
    bool blended = false;
    for (size_t y = 20U; y < 48U; ++y)
        for (size_t x = 20U; x < 150U; ++x)
            if (s.pixels[y * stride + x] != 0U && s.pixels[y * stride + x] != 0xffffU) blended = true;
    assert(blended);
    p4_ui_sprite(&s, INT_MAX, INT_MIN, INT_MAX, 1, sprite, 2, 2, false, 0U);
    p4_ui_round_rect(&s, INT_MIN, INT_MAX, INT_MAX, INT_MAX, INT_MAX, 0U);
    p4_ui_text(&s, INT_MAX, INT_MIN, "reject", 0xffffU, UINT_MAX, SIZE_MAX);
    assert(memory[0] == 0x1234U && memory[count - 1U] == 0x1234U);
    for (size_t y = 0; y < height; ++y)
        for (size_t x = width; x < stride; ++x) assert(s.pixels[y * stride + x] == 0x1234U);
    assert(p4_ui_x(&s, 320) == (int)width && p4_ui_y(&s, 200) == (int)height);
    free(memory);
}
int main(void)
{
    assert(p4_ui_text_width(NULL, 28U, 9U) == 0);
    assert(p4_ui_text_width("AB", 28U, 1U) == p4_ui_text_width("A", 28U, 9U));
    assert(p4_ui_text_width("AB", 28U, 2U) == p4_ui_text_width("A", 28U, 1U) + p4_ui_text_width("B", 28U, 1U));
    assert(p4_ui_blend(0U, 0xffffU, 0U) == 0U);
    assert(p4_ui_blend(0U, 0xffffU, 15U) == 0xffffU);
    verify(320U, 200U);
    verify(768U, 480U);
    return 0;
}
