#include "doom/video_convert.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    WIDTH = 320,
    HEIGHT = 200,
    SOURCE_STRIDE = 325,
    DESTINATION_STRIDE = 327,
};

static unsigned failures;

#define EXPECT_TRUE(expression_)                                                \
    do {                                                                        \
        if (!(expression_)) {                                                   \
            fprintf(stderr, "%s:%d: expected true: %s\n", __FILE__,         \
                    __LINE__, #expression_);                                    \
            failures++;                                                        \
        }                                                                       \
    } while (0)

#define EXPECT_EQ(expected_, actual_)                                           \
    do {                                                                        \
        const uint64_t expected_value_ = (uint64_t)(expected_);                 \
        const uint64_t actual_value_ = (uint64_t)(actual_);                     \
        if (expected_value_ != actual_value_) {                                 \
            fprintf(stderr, "%s:%d: expected 0x%llx, got 0x%llx: %s\n",     \
                    __FILE__, __LINE__,                                         \
                    (unsigned long long)expected_value_,                        \
                    (unsigned long long)actual_value_, #actual_);               \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static void test_channels_stride_and_bounds(void)
{
    const size_t source_count = SOURCE_STRIDE * HEIGHT;
    const size_t destination_count = DESTINATION_STRIDE * HEIGHT;
    uint32_t *source = calloc(source_count, sizeof(*source));
    uint16_t *destination = malloc(destination_count * sizeof(*destination));
    EXPECT_TRUE(source != NULL);
    EXPECT_TRUE(destination != NULL);
    if (source == NULL || destination == NULL) {
        free(destination);
        free(source);
        return;
    }
    for (size_t index = 0; index < destination_count; ++index) {
        destination[index] = UINT16_C(0xa55a);
    }

    source[0] = UINT32_C(0x00ff0000);
    source[1] = UINT32_C(0x0000ff00);
    source[2] = UINT32_C(0x000000ff);
    source[3] = UINT32_C(0x00ffffff);
    source[4] = UINT32_C(0x00000000);
    source[5] = UINT32_C(0xab123456); /* X is ignored. */
    source[SOURCE_STRIDE + (WIDTH - 1U)] = UINT32_C(0x00f81c07);
    source[(HEIGHT - 1U) * SOURCE_STRIDE] = UINT32_C(0x00808080);

    EXPECT_TRUE(doom_video_convert_xrgb8888_to_rgb565(
        source, SOURCE_STRIDE, destination, DESTINATION_STRIDE));
    EXPECT_EQ(UINT16_C(0xf800), destination[0]);
    EXPECT_EQ(UINT16_C(0x07e0), destination[1]);
    EXPECT_EQ(UINT16_C(0x001f), destination[2]);
    EXPECT_EQ(UINT16_C(0xffff), destination[3]);
    EXPECT_EQ(UINT16_C(0x0000), destination[4]);
    EXPECT_EQ(UINT16_C(0x11aa), destination[5]);
    EXPECT_EQ(UINT16_C(0xf8e0),
              destination[DESTINATION_STRIDE + (WIDTH - 1U)]);
    EXPECT_EQ(UINT16_C(0x8410),
              destination[(HEIGHT - 1U) * DESTINATION_STRIDE]);

    for (size_t y = 0; y < HEIGHT; ++y) {
        for (size_t x = WIDTH; x < DESTINATION_STRIDE; ++x) {
            EXPECT_EQ(UINT16_C(0xa55a),
                      destination[(y * DESTINATION_STRIDE) + x]);
        }
    }
    EXPECT_TRUE(!doom_video_convert_xrgb8888_to_rgb565(
        source, WIDTH - 1U, destination, DESTINATION_STRIDE));
    EXPECT_TRUE(!doom_video_convert_xrgb8888_to_rgb565(
        source, SOURCE_STRIDE, destination, WIDTH - 1U));
    EXPECT_TRUE(!doom_video_convert_xrgb8888_to_rgb565(
        NULL, SOURCE_STRIDE, destination, DESTINATION_STRIDE));

    free(destination);
    free(source);
}

static void test_rgb565_little_endian_bytes(void)
{
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "ESP32-P4 framebuffer byte-order proof requires a little-endian test host"
#endif
    const uint32_t source[] = {
        UINT32_C(0x00ff0000), UINT32_C(0x0000ff00),
        UINT32_C(0x000000ff), UINT32_C(0x00ffffff),
    };
    uint32_t full_source[WIDTH * HEIGHT] = {0};
    uint16_t converted[WIDTH * HEIGHT];
    memcpy(full_source, source, sizeof(source));
    EXPECT_TRUE(doom_video_convert_xrgb8888_to_rgb565(
        full_source, WIDTH, converted, WIDTH));
    const uint8_t expected[] = {0x00, 0xf8, 0xe0, 0x07,
                                0x1f, 0x00, 0xff, 0xff};
    uint8_t actual[sizeof(expected)];
    memcpy(actual, converted, sizeof(actual));
    EXPECT_TRUE(memcmp(expected, actual, sizeof(actual)) == 0);
}

int main(void)
{
    test_channels_stride_and_bounds();
    test_rgb565_little_endian_bytes();
    if (failures != 0U) {
        fprintf(stderr, "Doom video tests failed: %u\n", failures);
        return 1;
    }
    puts("P4_DOOM_VIDEO HOST PASS input=0x00RRGGBB output=rgb565-le size=320x200");
    return 0;
}
