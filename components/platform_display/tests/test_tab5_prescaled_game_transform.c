// SPDX-License-Identifier: Apache-2.0
#include "tab5_prescaled_game_transform.h"
#include "tab5_game_prescale.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Inject only the external accelerator result. Dispatch and CPU reconstruction
 * are the private production helper and production layout implementation. */
static const uint16_t *expected_source;
static uint16_t *expected_destination;
static size_t expected_source_stride, expected_destination_stride;
static int accelerator_status;
static unsigned accelerator_calls;
static unsigned accelerator_write_mode;

static int accelerator(const uint16_t *source, size_t stride, size_t width,
                       uint16_t *destination)
{
    ++accelerator_calls;
    assert(source == expected_source);
    assert(stride == expected_source_stride);
    assert(width == 384U); /* Direct exact-3x PPA input; no second prescale. */
    assert(destination == expected_destination);
    if (accelerator_write_mode == 1U) {
        /* A failed operation may have modified part of the panel, including
         * borders. CPU recovery must reconstruct the complete frame. */
        destination[0] = UINT16_C(0xf00d);
        destination[64U * expected_destination_stride] = UINT16_C(0xf00d);
        destination[641U * expected_destination_stride + 217U] = UINT16_C(0xf00d);
        destination[1279U * expected_destination_stride + 719U] = UINT16_C(0xf00d);
    } else if (accelerator_write_mode == 2U) {
        for (size_t y = 0; y < 1280U; ++y)
            for (size_t x = 0; x < 720U; ++x)
                destination[y * expected_destination_stride + x] = UINT16_C(0x69e7);
    }
    return accelerator_status;
}

static void configure_accelerator(const uint16_t *source, size_t source_stride,
    uint16_t *destination, size_t destination_stride, int status, unsigned write_mode)
{
    expected_source = source;
    expected_source_stride = source_stride;
    expected_destination = destination;
    expected_destination_stride = destination_stride;
    accelerator_status = status;
    accelerator_write_mode = write_mode;
    accelerator_calls = 0U;
}

static void failure_then_success(int failure_status, size_t padding)
{
    const size_t guard = 35U, source_stride = 323U;
    const size_t prescaled_stride = 384U + padding;
    const size_t destination_stride = 720U + padding;
    const size_t source_words = 200U * source_stride;
    const size_t prescaled_words = 2U * guard + 240U * prescaled_stride;
    const size_t destination_words = 2U * guard + 1282U * destination_stride;
    uint16_t *source = malloc(source_words * sizeof(*source));
    uint16_t *prescaled = malloc(prescaled_words * sizeof(*prescaled));
    uint16_t *prescaled_before = malloc(prescaled_words * sizeof(*prescaled_before));
    uint16_t *actual = malloc(destination_words * sizeof(*actual));
    uint16_t *expected = malloc(destination_words * sizeof(*expected));
    assert(source && prescaled && prescaled_before && actual && expected);
    memset(source, 0xa5, source_words * sizeof(*source));
    memset(prescaled, 0xb6, prescaled_words * sizeof(*prescaled));
    memset(actual, 0x3c, destination_words * sizeof(*actual));
    memcpy(expected, actual, destination_words * sizeof(*expected));
    for (size_t y = 0; y < 200U; ++y)
        for (size_t x = 0; x < 320U; ++x)
            source[y * source_stride + x] = (uint16_t)(y * 320U + x);
    assert(tab5_game_prescale_rgb565(source, source_stride,
                                    prescaled + guard, prescaled_stride));
    memcpy(prescaled_before, prescaled, prescaled_words * sizeof(*prescaled));
    assert(platform_display_layout_rgb565_320x200(source, source_stride,
               expected + guard, destination_stride, 1282U));
    configure_accelerator(prescaled + guard, prescaled_stride,
                         actual + guard, destination_stride, failure_status, 1U);
    tab5_prescaled_game_transform_result_t result = tab5_prescaled_game_transform(
        prescaled + guard, prescaled_stride, actual + guard,
        destination_stride, 1282U, accelerator);
    assert(accelerator_calls == 1U);
    assert(result.accelerator_result == failure_status);
    assert(result.complete);
    assert(memcmp(actual, expected, destination_words * sizeof(*actual)) == 0);
    assert(memcmp(prescaled, prescaled_before, prescaled_words * sizeof(*prescaled)) == 0);

    /* Reuse the same panel; success must keep accelerator output verbatim,
     * rather than run the fallback and silently overwrite a successful PPA. */
    configure_accelerator(prescaled + guard, prescaled_stride,
                         actual + guard, destination_stride, 0, 2U);
    for (size_t y = 0; y < 1280U; ++y)
        for (size_t x = 0; x < 720U; ++x)
            expected[guard + y * destination_stride + x] = UINT16_C(0x69e7);
    result = tab5_prescaled_game_transform(prescaled + guard, prescaled_stride,
        actual + guard, destination_stride, 1282U, accelerator);
    assert(accelerator_calls == 1U);
    assert(result.accelerator_result == 0);
    assert(result.complete);
    assert(memcmp(actual, expected, destination_words * sizeof(*actual)) == 0);
    assert(memcmp(prescaled, prescaled_before, prescaled_words * sizeof(*prescaled)) == 0);
    free(expected);
    free(actual);
    free(prescaled_before);
    free(prescaled);
    free(source);
}

static void rejected_fallback(const uint16_t *source, size_t source_stride,
                              uint16_t *destination, size_t destination_stride,
                              size_t destination_height)
{
    configure_accelerator(source, source_stride, destination,
                         destination_stride, 0x105, 0U);
    const tab5_prescaled_game_transform_result_t result = tab5_prescaled_game_transform(
        source, source_stride, destination, destination_stride,
        destination_height, accelerator);
    assert(accelerator_calls == 1U);
    assert(result.accelerator_result == 0x105);
    assert(!result.complete);
}

static void invalid_arguments(void)
{
    uint16_t guard[4] = {0x1234, 0x5678, 0x9abc, 0xdef0};
    const uint16_t before[4] = {0x1234, 0x5678, 0x9abc, 0xdef0};
    rejected_fallback(NULL, 384U, guard, 720U, 1280U);
    rejected_fallback(guard, 384U, NULL, 720U, 1280U);
    rejected_fallback(guard, 383U, guard, 720U, 1280U);
    rejected_fallback(guard, 384U, guard, 719U, 1280U);
    rejected_fallback(guard, 384U, guard, 720U, 1279U);
    rejected_fallback(guard, SIZE_MAX, guard, 720U, 1280U);
    rejected_fallback(guard, 384U, guard, SIZE_MAX, 1280U);
    assert(memcmp(guard, before, sizeof(guard)) == 0);
}

int main(void)
{
    static const int errors[] = {-1, 1, 0x105};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        failure_then_success(errors[i], 0U);
        failure_then_success(errors[i], 13U);
    }
    invalid_arguments();
    puts("Tab5 prescaled game dispatch: 6 failed/partial accelerations reconstructed; "
         "6 successes preserved; invalid fallback rejected; guards and input preserved");
    return 0;
}
