// SPDX-License-Identifier: Apache-2.0
#include "platform_display_layout.h"
#include "tab5_game_prescale.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SOURCE_WIDTH = 320, SOURCE_HEIGHT = 200,
       PRESCALED_WIDTH = 384, PRESCALED_HEIGHT = 240,
       PANEL_WIDTH = 720, PANEL_HEIGHT = 1280 };

static uint32_t random_state = UINT32_C(0x6da40783);
static unsigned frames_compared;

static uint32_t random_u32(void)
{
    random_state ^= random_state << 13U;
    random_state ^= random_state >> 17U;
    random_state ^= random_state << 5U;
    return random_state;
}

/* Original physical -> logical -> 320x200 mapping, independent of either
 * tiled implementation and of the inverse prescale lookup under test. */
static uint16_t expected_pixel(const uint16_t *source, size_t stride,
                               size_t x, size_t y)
{
    const size_t logical_x = 1279U - y;
    if (logical_x < 64U || logical_x >= 1216U) return 0U;
    return source[(x * 200U / 720U) * stride +
                  (logical_x - 64U) * 320U / 1152U];
}

static void compare_frame(size_t source_stride, size_t prescaled_stride,
                          size_t destination_stride, size_t extra_rows,
                          unsigned pattern, size_t alignment_offset)
{
    const size_t guard = 33U + alignment_offset;
    const size_t source_words = 2U * guard + SOURCE_HEIGHT * source_stride;
    const size_t prescaled_words = 2U * guard + PRESCALED_HEIGHT * prescaled_stride;
    const size_t destination_rows = PANEL_HEIGHT + extra_rows;
    const size_t destination_words = 2U * guard + destination_rows * destination_stride;
    uint16_t *source = malloc(source_words * sizeof(*source));
    uint16_t *source_before = malloc(source_words * sizeof(*source_before));
    uint16_t *prescaled = malloc(prescaled_words * sizeof(*prescaled));
    uint16_t *prescaled_before = malloc(prescaled_words * sizeof(*prescaled_before));
    uint16_t *expected = malloc(destination_words * sizeof(*expected));
    uint16_t *actual = malloc(destination_words * sizeof(*actual));
    assert(source && source_before && prescaled && prescaled_before && expected && actual);
    memset(source, 0xa5, source_words * sizeof(*source));
    memset(prescaled, 0xb6, prescaled_words * sizeof(*prescaled));
    memset(expected, 0x3c, destination_words * sizeof(*expected));
    memcpy(actual, expected, destination_words * sizeof(*actual));
    for (size_t y = 0; y < SOURCE_HEIGHT; ++y) {
        for (size_t x = 0; x < SOURCE_WIDTH; ++x) {
            const uint16_t value = pattern == 0U
                ? (uint16_t)(y * SOURCE_WIDTH + x) /* All 64000 coordinates differ. */
                : pattern == 1U ? UINT16_MAX
                : pattern == 2U ? 0U
                : pattern == 3U ? (uint16_t)(((x % 5U) * 257U) ^ ((y % 5U) * 4099U))
                : (uint16_t)random_u32();
            source[guard + y * source_stride + x] = value;
        }
    }
    /* Reuse both destinations for a different frame, including duplicated
     * prescale rows; stale pixels and writes into padding/guards must fail. */
    for (unsigned reuse = 0; reuse < 2U; ++reuse) {
        if (reuse != 0U) {
            for (size_t y = 0; y < SOURCE_HEIGHT; ++y)
                for (size_t x = 0; x < SOURCE_WIDTH; ++x)
                    source[guard + y * source_stride + x] ^= UINT16_MAX;
        }
        memcpy(source_before, source, source_words * sizeof(*source));
        assert(tab5_game_prescale_rgb565(source + guard, source_stride,
                                         prescaled + guard, prescaled_stride));
        assert(memcmp(source, source_before, source_words * sizeof(*source)) == 0);
        memcpy(prescaled_before, prescaled, prescaled_words * sizeof(*prescaled));
        assert(platform_display_layout_rgb565_320x200(source + guard, source_stride,
                   expected + guard, destination_stride, destination_rows));
        assert(platform_display_layout_rgb565_prescaled_game_384x240(
                   prescaled + guard, prescaled_stride, actual + guard,
                   destination_stride, destination_rows));
        assert(memcmp(actual, expected, destination_words * sizeof(*actual)) == 0);
        assert(memcmp(source, source_before, source_words * sizeof(*source)) == 0);
        assert(memcmp(prescaled, prescaled_before,
                      prescaled_words * sizeof(*prescaled)) == 0);
        for (size_t y = 0; y < destination_rows; ++y) {
            for (size_t x = 0; x < destination_stride; ++x) {
                const uint16_t value = y < PANEL_HEIGHT && x < PANEL_WIDTH
                    ? expected_pixel(source + guard, source_stride, x, y)
                    : UINT16_C(0x3c3c);
                assert(actual[guard + y * destination_stride + x] == value);
            }
        }
        for (size_t i = 0; i < guard; ++i) {
            assert(actual[i] == UINT16_C(0x3c3c));
            assert(actual[destination_words - 1U - i] == UINT16_C(0x3c3c));
        }
        ++frames_compared;
    }
    free(actual);
    free(expected);
    free(prescaled_before);
    free(prescaled);
    free(source_before);
    free(source);
}

static void invalid_arguments(void)
{
    uint16_t guard[4] = {0x1234, 0x5678, 0x9abc, 0xdef0};
    const uint16_t before[4] = {0x1234, 0x5678, 0x9abc, 0xdef0};
    assert(!platform_display_layout_rgb565_prescaled_game_384x240(
        NULL, 384U, guard, 720U, 1280U));
    assert(!platform_display_layout_rgb565_prescaled_game_384x240(
        guard, 384U, NULL, 720U, 1280U));
    assert(!platform_display_layout_rgb565_prescaled_game_384x240(
        guard, 383U, guard, 720U, 1280U));
    assert(!platform_display_layout_rgb565_prescaled_game_384x240(
        guard, 384U, guard, 719U, 1280U));
    assert(!platform_display_layout_rgb565_prescaled_game_384x240(
        guard, 384U, guard, 720U, 1279U));
    assert(!platform_display_layout_rgb565_prescaled_game_384x240(
        guard, SIZE_MAX, guard, 720U, 1280U));
    assert(!platform_display_layout_rgb565_prescaled_game_384x240(
        guard, 384U, guard, SIZE_MAX, 1280U));
    assert(!platform_display_layout_rgb565_prescaled_game_384x240(
        guard, SIZE_MAX / (240U * sizeof(*guard)) + 1U, guard, 720U, 1280U));
    assert(!platform_display_layout_rgb565_prescaled_game_384x240(
        guard, 384U, guard, SIZE_MAX / (1280U * sizeof(*guard)) + 1U, 1280U));
    assert(memcmp(guard, before, sizeof(guard)) == 0);
}

int main(void)
{
    static const size_t source_strides[] = {320U, 321U, 333U, 512U};
    for (size_t i = 0; i < sizeof(source_strides) / sizeof(source_strides[0]); ++i)
        for (unsigned pattern = 0; pattern < 5U; ++pattern)
            for (size_t offset = 0; offset < 2U; ++offset)
                compare_frame(source_strides[i], 384U + offset * 17U,
                              720U + offset * 13U, offset * 3U, pattern, offset);
    for (unsigned i = 0; i < 24U; ++i)
        compare_frame(320U + random_u32() % 113U,
                      384U + random_u32() % 117U,
                      720U + random_u32() % 29U, i % 4U, 4U, i % 2U);
    invalid_arguments();
    printf("Tab5 prescaled game fallback: %u complete 720x1280 frames match original "
           "320x200 CPU layout; padding/guards/input immutability/reuse/invalid args pass\n",
           frames_compared);
    return 0;
}
