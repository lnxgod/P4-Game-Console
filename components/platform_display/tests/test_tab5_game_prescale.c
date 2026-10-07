// SPDX-License-Identifier: Apache-2.0
#include "tab5_game_prescale.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Frozen production mapping before the grouped prescale optimization. */
static void reference(const uint16_t *source, size_t source_stride,
                      uint16_t *destination, size_t destination_stride)
{
    for (size_t y = 0; y < 240U; ++y) {
        const uint16_t *row = source + (y * 200U / 240U) * source_stride;
        for (size_t x = 0; x < 384U; ++x)
            destination[y * destination_stride + x] = row[x * 320U / 384U];
    }
}

static uint32_t random_state = UINT32_C(0x574a720d);
static uint32_t random_u32(void)
{
    random_state ^= random_state << 13U;
    random_state ^= random_state >> 17U;
    random_state ^= random_state << 5U;
    return random_state;
}

static void compare_frame(size_t source_stride, size_t destination_stride,
                          unsigned pattern, size_t alignment_offset)
{
    const size_t guard = 33U + alignment_offset;
    const size_t source_words = 2U * guard + 200U * source_stride;
    const size_t destination_words = 2U * guard + 240U * destination_stride;
    uint16_t *source = malloc(source_words * sizeof(*source));
    uint16_t *unchanged = malloc(source_words * sizeof(*unchanged));
    uint16_t *actual = malloc(destination_words * sizeof(*actual));
    uint16_t *expected = malloc(destination_words * sizeof(*expected));
    assert(source && unchanged && actual && expected);
    /* Padding and both guards are compared, not just the visible rectangle. */
    memset(source, 0xa5, source_words * sizeof(*source));
    memset(actual, 0x3c, destination_words * sizeof(*actual));
    memcpy(expected, actual, destination_words * sizeof(*actual));
    for (size_t y = 0; y < 200U; ++y) {
        for (size_t x = 0; x < 320U; ++x) {
            const uint16_t value = pattern == 0U
                ? (uint16_t)(y * 320U + x) /* Every source coordinate differs. */
                : pattern == 1U ? UINT16_MAX
                : pattern == 2U ? 0U : (uint16_t)random_u32();
            source[guard + y * source_stride + x] = value;
        }
    }
    memcpy(unchanged, source, source_words * sizeof(*source));
    reference(source + guard, source_stride, expected + guard, destination_stride);
    assert(tab5_game_prescale_rgb565(source + guard, source_stride,
                                     actual + guard, destination_stride));
    assert(memcmp(actual, expected, destination_words * sizeof(*actual)) == 0);
    assert(memcmp(source, unchanged, source_words * sizeof(*source)) == 0);
    /* Reuse the same output with different input: no stale duplicated rows. */
    for (size_t y = 0; y < 200U; ++y)
        for (size_t x = 0; x < 320U; ++x)
            source[guard + y * source_stride + x] ^= UINT16_MAX;
    memcpy(unchanged, source, source_words * sizeof(*source));
    reference(source + guard, source_stride, expected + guard, destination_stride);
    assert(tab5_game_prescale_rgb565(source + guard, source_stride,
                                     actual + guard, destination_stride));
    assert(memcmp(actual, expected, destination_words * sizeof(*actual)) == 0);
    assert(memcmp(source, unchanged, source_words * sizeof(*source)) == 0);
    free(expected);
    free(actual);
    free(unchanged);
    free(source);
}

static void invalid_arguments(void)
{
    uint16_t guard[4] = {0x1234, 0x5678, 0x9abc, 0xdef0};
    const uint16_t original[4] = {0x1234, 0x5678, 0x9abc, 0xdef0};
    assert(!tab5_game_prescale_rgb565(NULL, 320U, guard, 384U));
    assert(!tab5_game_prescale_rgb565(guard, 320U, NULL, 384U));
    assert(!tab5_game_prescale_rgb565(guard, 319U, guard, 384U));
    assert(!tab5_game_prescale_rgb565(guard, 320U, guard, 383U));
    assert(!tab5_game_prescale_rgb565(guard, SIZE_MAX, guard, 384U));
    assert(!tab5_game_prescale_rgb565(guard, 320U, guard, SIZE_MAX));
    assert(!tab5_game_prescale_rgb565(guard,
        SIZE_MAX / (200U * sizeof(*guard)) + 1U, guard, 384U));
    assert(!tab5_game_prescale_rgb565(guard, 320U, guard,
        SIZE_MAX / (240U * sizeof(*guard)) + 1U));
    assert(memcmp(guard, original, sizeof(guard)) == 0);
}

int main(void)
{
    static const size_t source_strides[] = {320U, 321U, 333U, 384U, 512U, 1024U};
    for (size_t i = 0; i < sizeof(source_strides) / sizeof(source_strides[0]); ++i)
        for (unsigned pattern = 0; pattern < 4U; ++pattern)
            for (size_t offset = 0; offset < 2U; ++offset)
                compare_frame(source_strides[i], 384U + offset * 17U, pattern, offset);
    for (unsigned i = 0; i < 100U; ++i) {
        const size_t source_stride = 320U + random_u32() % 701U;
        const size_t destination_stride = 384U + random_u32() % 701U;
        compare_frame(source_stride, destination_stride, 3U, i % 2U);
    }
    invalid_arguments();
    puts("Tab5 prescale: 296 complete-frame parity/guard checks passed");
    return 0;
}
