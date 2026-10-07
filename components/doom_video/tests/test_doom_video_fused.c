#include "doom/video_convert.h"
#include "tab5_game_prescale.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SOURCE_WIDTH = 320,
    SOURCE_HEIGHT = 200,
    OUTPUT_WIDTH = 384,
    OUTPUT_HEIGHT = 240,
    INTERMEDIATE_STRIDE = 327,
    TAIL_GUARD_WORDS = 7,
};

typedef struct {
    size_t source_stride;
    size_t output_stride;
    size_t source_count;
    size_t output_count;
    size_t source_prefix;
    size_t output_prefix;
    uint32_t *source_storage;
    uint32_t *source_snapshot;
    uint16_t *output_storage;
    uint16_t *reference_storage;
    uint16_t *intermediate;
    uint32_t *source;
    uint16_t *output;
    uint16_t *reference;
} fixture_t;

static unsigned checked_frames;

static void fixture_destroy(fixture_t *fixture)
{
    free(fixture->intermediate);
    free(fixture->reference_storage);
    free(fixture->output_storage);
    free(fixture->source_snapshot);
    free(fixture->source_storage);
}

static bool fixture_create(fixture_t *fixture, size_t source_stride,
                           size_t output_stride, size_t source_prefix,
                           size_t output_prefix)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->source_stride = source_stride;
    fixture->output_stride = output_stride;
    fixture->source_prefix = source_prefix;
    fixture->output_prefix = output_prefix;
    fixture->source_count = source_prefix + source_stride * SOURCE_HEIGHT +
                            TAIL_GUARD_WORDS;
    fixture->output_count = output_prefix + output_stride * OUTPUT_HEIGHT +
                            TAIL_GUARD_WORDS;
    fixture->source_storage = malloc(fixture->source_count * sizeof(uint32_t));
    fixture->source_snapshot = malloc(fixture->source_count * sizeof(uint32_t));
    fixture->output_storage = malloc(fixture->output_count * sizeof(uint16_t));
    fixture->reference_storage = malloc(fixture->output_count * sizeof(uint16_t));
    fixture->intermediate = malloc(INTERMEDIATE_STRIDE * SOURCE_HEIGHT *
                                   sizeof(uint16_t));
    if (fixture->source_storage == NULL || fixture->source_snapshot == NULL ||
        fixture->output_storage == NULL || fixture->reference_storage == NULL ||
        fixture->intermediate == NULL) {
        fprintf(stderr, "fused fixture allocation failed\n");
        fixture_destroy(fixture);
        return false;
    }
    fixture->source = fixture->source_storage + source_prefix;
    fixture->output = fixture->output_storage + output_prefix;
    fixture->reference = fixture->reference_storage + output_prefix;
    memset(fixture->source_storage, 0xdb,
           fixture->source_count * sizeof(uint32_t));
    memset(fixture->output_storage, 0xa5,
           fixture->output_count * sizeof(uint16_t));
    memset(fixture->reference_storage, 0xa5,
           fixture->output_count * sizeof(uint16_t));
    memset(fixture->intermediate, 0x5a,
           INTERMEDIATE_STRIDE * SOURCE_HEIGHT * sizeof(uint16_t));

    /* malloc aligns the allocation; odd offsets deliberately defeat packed
     * uint64_t source loads and uint32_t destination stores. Odd row strides
     * also alternate the natural alignment of consecutive rows. */
    if ((uintptr_t)fixture->source % sizeof(uint32_t) != 0U ||
        (uintptr_t)fixture->output % sizeof(uint16_t) != 0U ||
        (source_prefix == 1U && (uintptr_t)fixture->source % 8U != 4U) ||
        (output_prefix == 1U && (uintptr_t)fixture->output % 4U != 2U)) {
        fprintf(stderr, "fused fixture alignment precondition failed\n");
        fixture_destroy(fixture);
        return false;
    }
    return true;
}

/* Arithmetic channel quantization is independent of the implementation's
 * packed masks/shifts. Deliberately discard the top X byte first. */
static uint16_t oracle_rgb565(uint32_t pixel)
{
    const uint32_t rgb = pixel % UINT32_C(0x01000000);
    const uint32_t red = rgb / UINT32_C(65536);
    const uint32_t green = (rgb / UINT32_C(256)) % UINT32_C(256);
    const uint32_t blue = rgb % UINT32_C(256);
    return (uint16_t)((red / 8U) * 2048U + (green / 4U) * 32U + blue / 8U);
}

static bool check_frame(fixture_t *fixture, const char *label, uint32_t frame)
{
    memcpy(fixture->source_snapshot, fixture->source_storage,
           fixture->source_count * sizeof(uint32_t));
    if (!doom_video_convert_xrgb8888_to_rgb565(
            fixture->source, fixture->source_stride,
            fixture->intermediate, INTERMEDIATE_STRIDE) ||
        !tab5_game_prescale_rgb565(
            fixture->intermediate, INTERMEDIATE_STRIDE,
            fixture->reference, fixture->output_stride)) {
        fprintf(stderr, "two-pass reference rejected valid fixture\n");
        return false;
    }
    if (!doom_video_convert_xrgb8888_to_rgb565_384x240(
            fixture->source, fixture->source_stride,
            fixture->output, fixture->output_stride)) {
        fprintf(stderr, "%s frame=%" PRIu32 ": fused rejected valid strides %zu/%zu\n",
                label, frame, fixture->source_stride, fixture->output_stride);
        return false;
    }
    if (memcmp(fixture->source_storage, fixture->source_snapshot,
               fixture->source_count * sizeof(uint32_t)) != 0) {
        fprintf(stderr, "%s frame=%" PRIu32 ": source or source guards changed\n",
                label, frame);
        return false;
    }
    /* Whole-allocation comparison includes prefix/suffix guards and every
     * row's padding, including padding after the final active row. */
    if (memcmp(fixture->output_storage, fixture->reference_storage,
               fixture->output_count * sizeof(uint16_t)) != 0) {
        for (size_t index = 0; index < fixture->output_count; ++index) {
            if (fixture->output_storage[index] != fixture->reference_storage[index]) {
                fprintf(stderr,
                        "%s frame=%" PRIu32 " strides=%zu/%zu: word %zu "
                        "expected 0x%04x, got 0x%04x (includes guards/padding)\n",
                        label, frame, fixture->source_stride, fixture->output_stride,
                        index, (unsigned)fixture->reference_storage[index],
                        (unsigned)fixture->output_storage[index]);
                break;
            }
        }
        return false;
    }
    /* Independent nearest-neighbor oracle checks EVERY output pixel in
     * EVERY frame, including all 6:5 boundaries in both dimensions. */
    for (size_t y = 0; y < OUTPUT_HEIGHT; ++y) {
        const size_t source_y = y * SOURCE_HEIGHT / OUTPUT_HEIGHT;
        for (size_t x = 0; x < OUTPUT_WIDTH; ++x) {
            const size_t source_x = x * SOURCE_WIDTH / OUTPUT_WIDTH;
            const uint16_t expected = oracle_rgb565(
                fixture->source[source_y * fixture->source_stride + source_x]);
            const uint16_t actual = fixture->output[y * fixture->output_stride + x];
            if (actual != expected) {
                fprintf(stderr,
                        "%s frame=%" PRIu32 " oracle (%zu,%zu)<-(%zu,%zu): "
                        "expected 0x%04x, got 0x%04x\n",
                        label, frame, x, y, source_x, source_y,
                        (unsigned)expected, (unsigned)actual);
                return false;
            }
        }
    }
    ++checked_frames;
    return true;
}

static uint32_t next_random(uint32_t *state)
{
    *state ^= *state << 13U;
    *state ^= *state >> 17U;
    *state ^= *state << 5U;
    return *state;
}

static bool test_patterns_strides_alignment_and_reuse(void)
{
    static const size_t strides[][4] = {
        {320U, 384U, 1U, 1U},
        {321U, 385U, 1U, 1U},
        {325U, 391U, 2U, 2U},
        {337U, 417U, 1U, 2U},
        {320U, 389U, 2U, 1U},
    };
    uint32_t random_state = UINT32_C(0xc01df00d);
    for (size_t index = 0; index < sizeof(strides) / sizeof(strides[0]); ++index) {
        fixture_t fixture;
        if (!fixture_create(&fixture, strides[index][0], strides[index][1],
                            strides[index][2], strides[index][3]))
            return false;
        /* Reuse the SAME output without clearing between unrelated frames;
         * black/white transitions expose missed stores of repeated pixels. */
        for (uint32_t frame = 0; frame < 8U; ++frame) {
            for (size_t y = 0; y < SOURCE_HEIGHT; ++y) {
                for (size_t x = 0; x < SOURCE_WIDTH; ++x) {
                    uint32_t pixel;
                    if (frame == 0U) {
                        pixel = ((uint32_t)x * 131U + (uint32_t)y * 17U) * 65536U +
                                (uint32_t)y * 256U + (uint32_t)x;
                    } else if (frame == 1U || frame == 3U) {
                        pixel = UINT32_C(0xff000000);
                    } else if (frame == 2U) {
                        pixel = UINT32_C(0x00ffffff);
                    } else {
                        pixel = next_random(&random_state);
                    }
                    fixture.source[y * fixture.source_stride + x] = pixel;
                }
            }
            if (!check_frame(&fixture, "coordinate/reuse/random", frame)) {
                fixture_destroy(&fixture);
                return false;
            }
        }
        fixture_destroy(&fixture);
    }
    return true;
}

static bool test_every_rgb_and_x_byte(void)
{
    fixture_t fixture;
    if (!fixture_create(&fixture, 321U, 385U, 1U, 1U))
        return false;
    const uint32_t colors = UINT32_C(0x01000000);
    const uint32_t batch_pixels = SOURCE_WIDTH * SOURCE_HEIGHT;
    for (uint32_t base = 0; base < colors; base += batch_pixels) {
        for (unsigned x_pass = 0; x_pass < 2U; ++x_pass) {
            const uint32_t x_byte = x_pass == 0U ? 0U : UINT32_C(0xff000000);
            for (size_t y = 0; y < SOURCE_HEIGHT; ++y) {
                for (size_t x = 0; x < SOURCE_WIDTH; ++x) {
                    const uint32_t rgb = (base + (uint32_t)(y * SOURCE_WIDTH + x)) % colors;
                    fixture.source[y * fixture.source_stride + x] = x_byte + rgb;
                }
            }
            if (!check_frame(&fixture, x_pass == 0U ? "all-RGB X=00" : "all-RGB X=ff", base)) {
                fixture_destroy(&fixture);
                return false;
            }
        }
    }
    fixture_destroy(&fixture);
    return true;
}

static bool test_rejected_arguments_are_untouched(void)
{
    fixture_t fixture;
    if (!fixture_create(&fixture, 320U, 384U, 1U, 1U))
        return false;
    struct invalid_case {
        size_t source_stride;
        size_t output_stride;
        bool null_source;
        bool null_output;
    };
    const struct invalid_case cases[] = {
        {320U, 384U, true, false},
        {320U, 384U, false, true},
        {320U, 384U, true, true},
        {0U, 384U, false, false},
        {319U, 384U, false, false},
        {320U, 0U, false, false},
        {320U, 383U, false, false},
        {SIZE_MAX, 384U, false, false},
        {320U, SIZE_MAX, false, false},
        {SIZE_MAX / (SOURCE_HEIGHT * sizeof(uint32_t)) + 1U, 384U, false, false},
        {320U, SIZE_MAX / (OUTPUT_HEIGHT * sizeof(uint16_t)) + 1U, false, false},
        {SIZE_MAX / SOURCE_HEIGHT + 1U, 384U, false, false},
        {320U, SIZE_MAX / OUTPUT_HEIGHT + 1U, false, false},
    };
    memcpy(fixture.source_snapshot, fixture.source_storage,
           fixture.source_count * sizeof(uint32_t));
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const struct invalid_case *test = &cases[index];
        if (doom_video_convert_xrgb8888_to_rgb565_384x240(
                test->null_source ? NULL : fixture.source, test->source_stride,
                test->null_output ? NULL : fixture.output, test->output_stride)) {
            fprintf(stderr, "invalid case %zu unexpectedly accepted\n", index);
            fixture_destroy(&fixture);
            return false;
        }
        if (memcmp(fixture.source_storage, fixture.source_snapshot,
                   fixture.source_count * sizeof(uint32_t)) != 0 ||
            memcmp(fixture.output_storage, fixture.reference_storage,
                   fixture.output_count * sizeof(uint16_t)) != 0) {
            fprintf(stderr, "invalid case %zu modified a buffer\n", index);
            fixture_destroy(&fixture);
            return false;
        }
    }
    fixture_destroy(&fixture);
    return true;
}

int main(void)
{
    if (!test_rejected_arguments_are_untouched() ||
        !test_patterns_strides_alignment_and_reuse() ||
        !test_every_rgb_and_x_byte())
        return EXIT_FAILURE;
    printf("Fused RGB565/prescale: %u frames, all 16,777,216 RGB values "
           "with X=00 and X=ff, differential + arithmetic oracle passed.\n",
           checked_frames);
    return EXIT_SUCCESS;
}
