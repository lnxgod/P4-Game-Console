#include "platform_display_layout.h"
#include "platform/board.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SOURCE_WIDTH = 320,
    SOURCE_HEIGHT = 200,
    SOURCE_STRIDE = 323,
    LOGICAL_WIDTH = PLATFORM_BOARD_DISPLAY_WIDTH,
    LOGICAL_HEIGHT = PLATFORM_BOARD_DISPLAY_HEIGHT,
    DESTINATION_WIDTH = PLATFORM_BOARD_DISPLAY_NATIVE_WIDTH,
    DESTINATION_HEIGHT = PLATFORM_BOARD_DISPLAY_NATIVE_HEIGHT,
    DESTINATION_STRIDE = DESTINATION_WIDTH + 7,
    DESTINATION_ROWS = DESTINATION_HEIGHT + 3,
    VIEWPORT_WIDTH = PLATFORM_BOARD_GAME_VIEWPORT_WIDTH,
    VIEWPORT_HEIGHT = PLATFORM_BOARD_GAME_VIEWPORT_HEIGHT,
    LEFT_MARGIN = PLATFORM_BOARD_GAME_MARGIN_LEFT,
    TOP_MARGIN = PLATFORM_BOARD_GAME_MARGIN_TOP,
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

static uint16_t source_pixel(size_t x, size_t y)
{
    return (uint16_t)(((x * 31U) ^ (y * 257U) ^ 0x4321U) & 0xffffU);
}

static uint16_t content_pixel(size_t x, size_t y)
{
    return (uint16_t)(((x * 17U) ^ (y * 129U) ^ 0x2468U) & 0xffffU);
}

static void native_to_logical(size_t native_x, size_t native_y,
                              size_t *logical_x, size_t *logical_y)
{
#if PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES == 90U
    *logical_x = (size_t)LOGICAL_WIDTH - 1U - native_y;
    *logical_y = native_x;
#else
    *logical_x = native_x;
    *logical_y = native_y;
#endif
}

static void test_bounds_and_scaling(void)
{
    const size_t source_count = SOURCE_STRIDE * SOURCE_HEIGHT;
    const size_t destination_count = DESTINATION_STRIDE * DESTINATION_ROWS;
    uint16_t *source = malloc(source_count * sizeof(*source));
    uint16_t *destination = malloc(destination_count * sizeof(*destination));
    EXPECT_TRUE(source != NULL);
    EXPECT_TRUE(destination != NULL);
    if (source == NULL || destination == NULL) {
        free(destination);
        free(source);
        return;
    }

    for (size_t y = 0; y < SOURCE_HEIGHT; ++y) {
        for (size_t x = 0; x < SOURCE_STRIDE; ++x) {
            source[(y * SOURCE_STRIDE) + x] =
                x < SOURCE_WIDTH ? source_pixel(x, y) : UINT16_C(0xdeed);
        }
    }
    for (size_t index = 0; index < destination_count; ++index) {
        destination[index] = UINT16_C(0xa55a);
    }

    EXPECT_TRUE(platform_display_layout_rgb565_320x200(
        source, SOURCE_STRIDE, destination, DESTINATION_STRIDE,
        DESTINATION_ROWS));

    for (size_t y = 0; y < DESTINATION_HEIGHT; ++y) {
        for (size_t x = 0; x < DESTINATION_WIDTH; ++x) {
            size_t logical_x = 0U;
            size_t logical_y = 0U;
            native_to_logical(x, y, &logical_x, &logical_y);
            uint16_t expected = 0;
            if (logical_x >= LEFT_MARGIN &&
                logical_x < LEFT_MARGIN + VIEWPORT_WIDTH &&
                logical_y >= TOP_MARGIN &&
                logical_y < TOP_MARGIN + VIEWPORT_HEIGHT) {
                expected = source_pixel(
                    ((logical_x - LEFT_MARGIN) * SOURCE_WIDTH) /
                        VIEWPORT_WIDTH,
                    ((logical_y - TOP_MARGIN) * SOURCE_HEIGHT) /
                        VIEWPORT_HEIGHT);
            }
            EXPECT_EQ(expected, destination[(y * DESTINATION_STRIDE) + x]);
        }
        for (size_t x = DESTINATION_WIDTH; x < DESTINATION_STRIDE; ++x) {
            EXPECT_EQ(UINT16_C(0xa55a),
                      destination[(y * DESTINATION_STRIDE) + x]);
        }
    }
    for (size_t y = DESTINATION_HEIGHT; y < DESTINATION_ROWS; ++y) {
        for (size_t x = 0; x < DESTINATION_STRIDE; ++x) {
            EXPECT_EQ(UINT16_C(0xa55a),
                      destination[(y * DESTINATION_STRIDE) + x]);
        }
    }

    EXPECT_TRUE(!platform_display_layout_rgb565_320x200(
        source, SOURCE_WIDTH - 1U, destination, DESTINATION_STRIDE,
        DESTINATION_ROWS));
    EXPECT_TRUE(!platform_display_layout_rgb565_320x200(
        source, SOURCE_STRIDE, destination, DESTINATION_WIDTH - 1U,
        DESTINATION_ROWS));
    EXPECT_TRUE(!platform_display_layout_rgb565_320x200(
        source, SOURCE_STRIDE, destination, DESTINATION_STRIDE,
        DESTINATION_HEIGHT - 1U));
    EXPECT_TRUE(!platform_display_layout_rgb565_320x200(
        NULL, SOURCE_STRIDE, destination, DESTINATION_STRIDE,
        DESTINATION_ROWS));

    free(destination);
    free(source);
}

static void test_standard_rgb565_target_byte_order(void)
{
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "ESP32-P4 framebuffer byte-order proof requires a little-endian test host"
#endif
    const uint16_t pixels[] = {
        UINT16_C(0xf800), UINT16_C(0x07e0), UINT16_C(0x001f),
        UINT16_C(0xffff), UINT16_C(0x0000),
    };
    const uint8_t expected_bytes[] = {
        0x00, 0xf8, 0xe0, 0x07, 0x1f, 0x00, 0xff, 0xff, 0x00, 0x00,
    };
    uint8_t actual_bytes[sizeof(pixels)];
    memcpy(actual_bytes, pixels, sizeof(actual_bytes));
    EXPECT_TRUE(memcmp(expected_bytes, actual_bytes, sizeof(actual_bytes)) == 0);
}

static void test_content_768x480_layout(void)
{
    enum {
        CONTENT_WIDTH = 768,
        CONTENT_HEIGHT = 480,
        CONTENT_STRIDE = 771,
    };
    const size_t source_count = CONTENT_STRIDE * CONTENT_HEIGHT;
    const size_t destination_count = DESTINATION_STRIDE * DESTINATION_ROWS;
    uint16_t *const source = malloc(source_count * sizeof(*source));
    uint16_t *const destination = malloc(destination_count * sizeof(*destination));
    EXPECT_TRUE(source != NULL);
    EXPECT_TRUE(destination != NULL);
    if (source == NULL || destination == NULL) {
        free(destination);
        free(source);
        return;
    }
    for (size_t y = 0U; y < CONTENT_HEIGHT; ++y) {
        for (size_t x = 0U; x < CONTENT_STRIDE; ++x) {
            source[y * CONTENT_STRIDE + x] =
                x < CONTENT_WIDTH ? content_pixel(x, y) : UINT16_C(0xdead);
        }
    }
    for (size_t index = 0U; index < destination_count; ++index) {
        destination[index] = UINT16_C(0xa55a);
    }
    EXPECT_TRUE(platform_display_layout_rgb565_768x480(
        source, CONTENT_STRIDE, destination, DESTINATION_STRIDE,
        DESTINATION_ROWS));
    for (size_t y = 0U; y < DESTINATION_HEIGHT; ++y) {
        for (size_t x = 0U; x < DESTINATION_WIDTH; ++x) {
            size_t logical_x = 0U;
            size_t logical_y = 0U;
            native_to_logical(x, y, &logical_x, &logical_y);
            uint16_t expected = 0U;
            if (logical_x >= LEFT_MARGIN &&
                logical_x < LEFT_MARGIN + VIEWPORT_WIDTH &&
                logical_y >= TOP_MARGIN &&
                logical_y < TOP_MARGIN + VIEWPORT_HEIGHT) {
                expected = content_pixel(
                    (logical_x - LEFT_MARGIN) * CONTENT_WIDTH / VIEWPORT_WIDTH,
                    (logical_y - TOP_MARGIN) * CONTENT_HEIGHT / VIEWPORT_HEIGHT);
            }
            EXPECT_EQ(expected, destination[y * DESTINATION_STRIDE + x]);
        }
    }
    EXPECT_TRUE(!platform_display_layout_rgb565_768x480(
        source, CONTENT_WIDTH - 1U, destination, DESTINATION_STRIDE,
        DESTINATION_ROWS));
    free(destination);
    free(source);
}

int main(void)
{
    test_bounds_and_scaling();
    test_content_768x480_layout();
    test_standard_rgb565_target_byte_order();
    if (failures != 0U) {
        fprintf(stderr, "platform display layout tests failed: %u\n", failures);
        return 1;
    }
    printf("P4_DISPLAY_LAYOUT HOST PASS logical=%ux%u native=%ux%u "
           "rotation_cw=%u viewport=%ux%u margins=%u/%u "
           "byte_order=rgb565-le\n",
           (unsigned)LOGICAL_WIDTH, (unsigned)LOGICAL_HEIGHT,
           (unsigned)DESTINATION_WIDTH, (unsigned)DESTINATION_HEIGHT,
           (unsigned)PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES,
           (unsigned)VIEWPORT_WIDTH, (unsigned)VIEWPORT_HEIGHT,
           (unsigned)LEFT_MARGIN,
           (unsigned)TOP_MARGIN);
    return 0;
}
