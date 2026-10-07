/* Included by the ordinary host test so parity runs in its existing target. */
#include "overlay_reference.c"

#include <limits.h>

enum { PARITY_GUARD_WORDS = 32 };

typedef struct {
    uint32_t *allocation;
    uint32_t *pixels;
    size_t stride;
    size_t words;
} parity_frame_t;

static uint32_t parity_guard_word(size_t index)
{
    return UINT32_C(0xa5c39e71) ^ (uint32_t)index * UINT32_C(0x9e3779b9);
}

static bool parity_frame_create(parity_frame_t *frame, size_t stride)
{
    frame->stride = stride;
    frame->words = stride * DOOM_TOUCH_FRAME_HEIGHT + 2U * PARITY_GUARD_WORDS;
    frame->allocation = malloc(frame->words * sizeof(uint32_t));
    frame->pixels = frame->allocation == NULL ? NULL :
        frame->allocation + PARITY_GUARD_WORDS;
    return frame->allocation != NULL;
}

static void parity_frame_fill(parity_frame_t *frame, int channel, unsigned value)
{
    for (size_t i = 0U; i < frame->words; ++i) {
        frame->allocation[i] = parity_guard_word(i);
    }
    for (size_t y = 0U; y < DOOM_TOUCH_FRAME_HEIGHT; ++y) {
        for (size_t x = 0U; x < DOOM_TOUCH_FRAME_WIDTH; ++x) {
            /* Each byte has a different spatial pattern, including nonzero X.
             * Uniform channel sweeps below exercise every rounding input at
             * every covered pixel, rather than only where a ramp meets a ring. */
            uint32_t pixel =
                (((uint32_t)(x * 13U + y * 37U + 193U) & 255U) << 24U) |
                (((uint32_t)(x * 29U + y * 11U + 17U) & 255U) << 16U) |
                (((uint32_t)(x * 7U + y * 43U + 83U) & 255U) << 8U) |
                 ((uint32_t)(x * 53U + y * 19U + 151U) & 255U);
            if (channel >= 0) {
                const unsigned shift = 8U * (unsigned)channel;
                pixel = (pixel & ~(UINT32_C(255) << shift)) |
                    ((uint32_t)value << shift);
            }
            frame->pixels[y * frame->stride + x] = pixel;
        }
    }
}

static bool parity_equal(const parity_frame_t *expected,
                         const parity_frame_t *actual,
                         const char *mode, uint32_t mask,
                         int channel, unsigned value)
{
    if (memcmp(expected->allocation, actual->allocation,
               expected->words * sizeof(uint32_t)) == 0) {
        return true;
    }
    for (size_t i = 0U; i < expected->words; ++i) {
        if (expected->allocation[i] != actual->allocation[i]) {
            fprintf(stderr,
                "overlay parity %s mask=0x%04x channel=%d value=%u "
                "word=%zu expected=%08x actual=%08x\n",
                mode, (unsigned)mask, channel, value, i,
                (unsigned)expected->allocation[i],
                (unsigned)actual->allocation[i]);
            ++failures;
            return false;
        }
    }
    return false;
}

static bool parity_guards_unchanged(const parity_frame_t *frame,
                                    const char *mode, uint32_t mask)
{
    /* Visit only padding and guards; scanning visible pixels here would add
     * billions of needless divisions to the exhaustive action-mask test. */
    for (size_t region = 0U; region < DOOM_TOUCH_FRAME_HEIGHT + 2U; ++region) {
        size_t begin;
        size_t end;
        if (region == 0U) {
            begin = 0U;
            end = PARITY_GUARD_WORDS;
        } else if (region == DOOM_TOUCH_FRAME_HEIGHT + 1U) {
            begin = frame->words - PARITY_GUARD_WORDS;
            end = frame->words;
        } else {
            begin = PARITY_GUARD_WORDS + (region - 1U) * frame->stride +
                DOOM_TOUCH_FRAME_WIDTH;
            end = PARITY_GUARD_WORDS + region * frame->stride;
        }
        for (size_t i = begin; i < end; ++i) {
            if (frame->allocation[i] != parity_guard_word(i)) {
                fprintf(stderr, "overlay %s changed guard/padding word=%zu mask=0x%04x\n",
                        mode, i, (unsigned)mask);
                ++failures;
                return false;
            }
        }
    }
    return true;
}

static bool parity_render_case(parity_frame_t frames[6], uint32_t mask,
                                int channel, unsigned value)
{
    parity_frame_t *const source = &frames[0];
    parity_frame_t *const source_before = &frames[1];
    parity_frame_t *const expected = &frames[2];
    parity_frame_t *const actual = &frames[3];
    parity_frame_t *const inplace_expected = &frames[4];
    parity_frame_t *const inplace_actual = &frames[5];
    /* In-place cases start from the source snapshot, including all guards. */
    memcpy(inplace_expected->allocation, source_before->allocation,
           source_before->words * sizeof(uint32_t));
    memcpy(inplace_actual->allocation, source_before->allocation,
           source_before->words * sizeof(uint32_t));
    if (!reference_doom_touch_overlay_render_xrgb8888(
            source->pixels, source->stride, expected->pixels, expected->stride, mask) ||
        !doom_touch_overlay_render_xrgb8888(
            source->pixels, source->stride, actual->pixels, actual->stride, mask) ||
        !reference_doom_touch_overlay_render_xrgb8888(
            inplace_expected->pixels, inplace_expected->stride,
            inplace_expected->pixels, inplace_expected->stride, mask) ||
        !doom_touch_overlay_render_xrgb8888(
            inplace_actual->pixels, inplace_actual->stride,
            inplace_actual->pixels, inplace_actual->stride, mask)) {
        fprintf(stderr, "overlay unexpectedly rejected valid mask=0x%04x\n",
                (unsigned)mask);
        ++failures;
        return false;
    }
    return parity_equal(expected, actual, "copy", mask, channel, value) &&
        parity_equal(inplace_expected, inplace_actual, "in-place", mask, channel, value) &&
        parity_equal(source_before, source, "source read-only", mask, channel, value) &&
        parity_guards_unchanged(actual, "copy", mask) &&
        parity_guards_unchanged(inplace_actual, "in-place", mask);
}

static void test_overlay_exact_parity(void)
{
    _Static_assert(DOOM_TOUCH_ACTION_COUNT == 14,
                   "Update exhaustive overlay parity masks for new actions");
    parity_frame_t frames[6] = {{0}};
    bool allocated = true;
    for (size_t i = 0U; i < 6U; ++i) {
        /* Copy reads and writes different padded strides; in-place retains
         * the source stride. Neither equals the logical frame width. */
        const size_t stride = DOOM_TOUCH_FRAME_WIDTH +
            ((i == 2U || i == 3U) ? 19U : 7U);
        if (!parity_frame_create(&frames[i], stride)) {
            allocated = false;
            break;
        }
        parity_frame_fill(&frames[i], -1, 0U);
    }
    EXPECT_TRUE(allocated);
    if (!allocated) goto cleanup;

    /* Every one of the 16,384 combinations, including unused action bits,
     * overlapping controls, and clipped circle/d-pad boundary pixels. */
    for (uint32_t mask = 0U; mask < (UINT32_C(1) << DOOM_TOUCH_ACTION_COUNT); ++mask) {
        if (!parity_render_case(frames, mask, -1, 0U)) goto cleanup;
    }

    /* At every geometry position, each B/G/R/X byte takes every value in both
     * blend weights and complementary mixed states (including overlap order). */
    static const uint32_t channel_masks[] = {
        UINT32_C(0), UINT32_C(0x3fff), UINT32_C(0x1555), UINT32_C(0x2aaa),
    };
    for (int channel = 0; channel < 4; ++channel) {
        for (unsigned value = 0U; value <= UINT8_MAX; ++value) {
            parity_frame_fill(&frames[0], channel, value);
            memcpy(frames[1].allocation, frames[0].allocation,
                   frames[0].words * sizeof(uint32_t));
            for (size_t i = 0U; i < sizeof(channel_masks) / sizeof(channel_masks[0]); ++i) {
                if (!parity_render_case(frames, channel_masks[i], channel, value))
                    goto cleanup;
            }
        }
    }
    puts("P4_DOOM_TOUCH_OVERLAY PARITY PASS masks=16384 channel_cases=4096 "
         "copy=true in_place=true source_readonly=true padding_guards=true");

cleanup:
    for (size_t i = 0U; i < 6U; ++i) free(frames[i].allocation);
}

static void test_overlay_stride_variants(void)
{
    static const uint32_t masks[] = {
        UINT32_C(0), UINT32_C(0x3fff), UINT32_C(0x1555), UINT32_C(0x2aaa),
    };
    for (unsigned variant = 0U; variant < 4U; ++variant) {
        const size_t source_stride = DOOM_TOUCH_FRAME_WIDTH +
            ((variant & 1U) != 0U ? 7U : 0U);
        const size_t destination_stride = DOOM_TOUCH_FRAME_WIDTH +
            ((variant & 2U) != 0U ? 19U : 0U);
        parity_frame_t frames[6] = {{0}};
        bool complete = true;
        for (size_t i = 0U; i < 6U; ++i) {
            const size_t stride = (i == 2U || i == 3U) ?
                destination_stride : source_stride;
            if (!parity_frame_create(&frames[i], stride)) {
                complete = false;
                break;
            }
            parity_frame_fill(&frames[i], -1, 0U);
        }
        EXPECT_TRUE(complete);
        if (complete) {
            for (size_t i = 0U; i < sizeof(masks) / sizeof(masks[0]); ++i) {
                if (!parity_render_case(frames, masks[i], -1, 0U)) {
                    complete = false;
                    break;
                }
            }
        }
        for (size_t i = 0U; i < 6U; ++i) free(frames[i].allocation);
        if (!complete) return;
    }
    puts("P4_DOOM_TOUCH_OVERLAY STRIDES PASS cases=16 "
         "contiguous=true mixed=true in_place=true");
}

static void test_overlay_invalid_ranges_are_atomic(void)
{
    const size_t stride = DOOM_TOUCH_FRAME_WIDTH + 7U;
    parity_frame_t frame = {0}, before = {0};
    const bool have_frame = parity_frame_create(&frame, stride);
    const bool have_before = parity_frame_create(&before, stride);
    EXPECT_TRUE(have_frame && have_before);
    if (!have_frame || !have_before) goto cleanup;
    parity_frame_fill(&frame, -1, 0U);
    memcpy(before.allocation, frame.allocation, frame.words * sizeof(uint32_t));
    const struct {
        const uint32_t *source;
        size_t source_stride;
        uint32_t *destination;
        size_t destination_stride;
        uint32_t mask;
    } cases[] = {
        {NULL, stride, frame.pixels, stride, 0U},
        {frame.pixels, stride, NULL, stride, 0U},
        {frame.pixels, DOOM_TOUCH_FRAME_WIDTH - 1U, frame.pixels, stride, 0U},
        {frame.pixels, stride, frame.pixels, DOOM_TOUCH_FRAME_WIDTH - 1U, 0U},
        {frame.pixels, stride, frame.pixels, stride + 1U, 0U},
        {frame.pixels, stride, frame.pixels + 1U, stride, 0U},
        {frame.pixels + 1U, stride, frame.pixels, stride, 0U},
        {frame.pixels, SIZE_MAX, frame.pixels, stride, 0U},
        {frame.pixels, stride, frame.pixels, SIZE_MAX, 0U},
        {frame.pixels, SIZE_MAX / DOOM_TOUCH_FRAME_HEIGHT,
         frame.pixels, SIZE_MAX / DOOM_TOUCH_FRAME_HEIGHT, 0U},
        {(const uint32_t *)(UINTPTR_MAX - 3U), stride, frame.pixels, stride, 0U},
        {frame.pixels, stride, (uint32_t *)(UINTPTR_MAX - 3U), stride, 0U},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        EXPECT_TRUE(!reference_doom_touch_overlay_render_xrgb8888(
            cases[i].source, cases[i].source_stride,
            cases[i].destination, cases[i].destination_stride, cases[i].mask));
        EXPECT_TRUE(!doom_touch_overlay_render_xrgb8888(
            cases[i].source, cases[i].source_stride,
            cases[i].destination, cases[i].destination_stride, cases[i].mask));
        if (!parity_equal(&before, &frame, "invalid-range atomicity", 0U, -1,
                          (unsigned)i)) goto cleanup;
    }
    for (unsigned bit = DOOM_TOUCH_ACTION_COUNT; bit < 32U; ++bit) {
        const uint32_t mask = UINT32_C(1) << bit;
        EXPECT_TRUE(!reference_doom_touch_overlay_render_xrgb8888(
            frame.pixels, stride, frame.pixels, stride, mask));
        EXPECT_TRUE(!doom_touch_overlay_render_xrgb8888(
            frame.pixels, stride, frame.pixels, stride, mask));
        if (!parity_equal(&before, &frame, "invalid-mask atomicity", mask, -1, 0U))
            goto cleanup;
    }
cleanup:
    free(frame.allocation);
    free(before.allocation);
}
