#include "platform_display_layout.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SOURCE_WIDTH = 320,
    SOURCE_HEIGHT = 200,
    SOURCE_STRIDE = 323,
    DESTINATION_WIDTH = 1024,
    DESTINATION_HEIGHT = 600,
    DESTINATION_STRIDE = 1031,
    DESTINATION_ROWS = 603,
    LEFT_MARGIN = 32,
    SCALE = 3,
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
        const size_t source_y = y / SCALE;
        for (size_t x = 0; x < DESTINATION_WIDTH; ++x) {
            uint16_t expected = 0;
            if (x >= LEFT_MARGIN && x < LEFT_MARGIN + (SOURCE_WIDTH * SCALE)) {
                expected = source_pixel((x - LEFT_MARGIN) / SCALE, source_y);
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

static void test_hdmi_rgb888_layout(void)
{
    enum {
        HDMI_WIDTH = 1280,
        HDMI_HEIGHT = 720,
        HDMI_STRIDE = (HDMI_WIDTH * 3) + 7,
        HDMI_ROWS = HDMI_HEIGHT + 2,
        LEFT = 160,
        TOP = 60,
    };
    uint16_t *source = calloc(SOURCE_STRIDE * SOURCE_HEIGHT,
                              sizeof(*source));
    uint8_t *destination = malloc(HDMI_STRIDE * HDMI_ROWS);
    EXPECT_TRUE(source != NULL);
    EXPECT_TRUE(destination != NULL);
    if (source == NULL || destination == NULL) {
        free(destination);
        free(source);
        return;
    }
    memset(destination, 0xa5, HDMI_STRIDE * HDMI_ROWS);
    source[0] = UINT16_C(0xf800);
    source[1] = UINT16_C(0x07e0);
    source[SOURCE_STRIDE] = UINT16_C(0x001f);

    EXPECT_TRUE(platform_display_layout_rgb565_to_rgb888_1280x720(
        source, SOURCE_STRIDE, destination, HDMI_STRIDE, HDMI_ROWS));
    const size_t red = (TOP * HDMI_STRIDE) + (LEFT * 3U);
    EXPECT_EQ(0xff, destination[red]);
    EXPECT_EQ(0x00, destination[red + 1U]);
    EXPECT_EQ(0x00, destination[red + 2U]);
    EXPECT_EQ(0xff, destination[red + 3U]);
    const size_t green = red + 9U;
    EXPECT_EQ(0x00, destination[green]);
    EXPECT_EQ(0xff, destination[green + 1U]);
    EXPECT_EQ(0x00, destination[green + 2U]);
    const size_t blue = ((TOP + 3U) * HDMI_STRIDE) + (LEFT * 3U);
    EXPECT_EQ(0x00, destination[blue]);
    EXPECT_EQ(0x00, destination[blue + 1U]);
    EXPECT_EQ(0xff, destination[blue + 2U]);
    EXPECT_EQ(0x00, destination[0]);
    EXPECT_EQ(0x00, destination[(HDMI_HEIGHT - 1U) * HDMI_STRIDE]);
    EXPECT_EQ(0xa5, destination[HDMI_WIDTH * 3U]);
    EXPECT_EQ(0xa5, destination[HDMI_HEIGHT * HDMI_STRIDE]);

    EXPECT_TRUE(!platform_display_layout_rgb565_to_rgb888_1280x720(
        source, SOURCE_WIDTH - 1U, destination, HDMI_STRIDE, HDMI_ROWS));
    EXPECT_TRUE(!platform_display_layout_rgb565_to_rgb888_1280x720(
        source, SOURCE_STRIDE, destination, (HDMI_WIDTH * 3U) - 1U,
        HDMI_ROWS));
    EXPECT_TRUE(!platform_display_layout_rgb565_to_rgb888_1280x720(
        source, SOURCE_STRIDE, destination, HDMI_STRIDE, HDMI_HEIGHT - 1U));

    free(destination);
    free(source);
}

int main(void)
{
    test_bounds_and_scaling();
    test_standard_rgb565_target_byte_order();
    test_hdmi_rgb888_layout();
    if (failures != 0U) {
        fprintf(stderr, "platform display layout tests failed: %u\n", failures);
        return 1;
    }
    puts("P4_DISPLAY_LAYOUT HOST PASS rgb565=1024x600 rgb888=1280x720 viewport=960x600");
    return 0;
}
