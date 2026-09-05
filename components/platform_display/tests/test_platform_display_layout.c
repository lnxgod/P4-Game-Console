#include "platform_display_layout.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SOURCE_WIDTH = 320,
    SOURCE_HEIGHT = 200,
    SOURCE_STRIDE = 323,
#if defined(CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3) && \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    DESTINATION_WIDTH = 480,
    DESTINATION_HEIGHT = 800,
    DESTINATION_STRIDE = 487,
    DESTINATION_ROWS = 803,
    LEFT_MARGIN = 16,
#else
    DESTINATION_WIDTH = 1024,
    DESTINATION_HEIGHT = 600,
    DESTINATION_STRIDE = 1031,
    DESTINATION_ROWS = 603,
    LEFT_MARGIN = 32,
#endif
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
        for (size_t x = 0; x < DESTINATION_WIDTH; ++x) {
            uint16_t expected = 0;
#if defined(CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3) && \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
            const size_t logical_x = 799U - y;
            if (logical_x >= LEFT_MARGIN &&
                logical_x < LEFT_MARGIN + 768U) {
                const size_t source_x =
                    (logical_x - LEFT_MARGIN) * SOURCE_WIDTH / 768U;
                const size_t source_y = x * SOURCE_HEIGHT / 480U;
                expected = source_pixel(source_x, source_y);
            }
#else
            const size_t source_y = y / SCALE;
            if (x >= LEFT_MARGIN && x < LEFT_MARGIN + (SOURCE_WIDTH * SCALE)) {
                expected = source_pixel((x - LEFT_MARGIN) / SCALE, source_y);
            }
#endif
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

static void test_shell_bounds_and_exact_viewport(void)
{
    enum {
        SHELL_WIDTH = 384,
        SHELL_HEIGHT = 240,
        SHELL_STRIDE = 389,
    };
    const size_t source_count = SHELL_STRIDE * SHELL_HEIGHT;
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
    for (size_t y = 0U; y < SHELL_HEIGHT; ++y) {
        for (size_t x = 0U; x < SHELL_STRIDE; ++x) {
            source[y * SHELL_STRIDE + x] = x < SHELL_WIDTH
                ? source_pixel(x, y) : UINT16_C(0xdeed);
        }
    }
    for (size_t index = 0U; index < destination_count; ++index) {
        destination[index] = UINT16_C(0xa55a);
    }

    EXPECT_TRUE(platform_display_layout_rgb565_384x240(
        source, SHELL_STRIDE, destination, DESTINATION_STRIDE,
        DESTINATION_ROWS));
    for (size_t y = 0U; y < DESTINATION_HEIGHT; ++y) {
        for (size_t x = 0U; x < DESTINATION_WIDTH; ++x) {
            uint16_t expected = 0U;
#if defined(CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3) && \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
            const size_t logical_x = 799U - y;
            if (logical_x >= LEFT_MARGIN &&
                logical_x < LEFT_MARGIN + 768U) {
                const size_t source_x =
                    (logical_x - LEFT_MARGIN) * SHELL_WIDTH / 768U;
                const size_t source_y = x * SHELL_HEIGHT / 480U;
                expected = source_pixel(source_x, source_y);
            }
#else
            if (x >= LEFT_MARGIN && x < LEFT_MARGIN + 960U) {
                const size_t source_x =
                    (x - LEFT_MARGIN) * SHELL_WIDTH / 960U;
                const size_t source_y = y * SHELL_HEIGHT / 600U;
                expected = source_pixel(source_x, source_y);
            }
#endif
            EXPECT_EQ(expected, destination[y * DESTINATION_STRIDE + x]);
        }
        for (size_t x = DESTINATION_WIDTH; x < DESTINATION_STRIDE; ++x) {
            EXPECT_EQ(UINT16_C(0xa55a),
                      destination[y * DESTINATION_STRIDE + x]);
        }
    }
    for (size_t y = DESTINATION_HEIGHT; y < DESTINATION_ROWS; ++y) {
        for (size_t x = 0U; x < DESTINATION_STRIDE; ++x) {
            EXPECT_EQ(UINT16_C(0xa55a),
                      destination[y * DESTINATION_STRIDE + x]);
        }
    }

    EXPECT_TRUE(!platform_display_layout_rgb565_384x240(
        source, SHELL_WIDTH - 1U, destination, DESTINATION_STRIDE,
        DESTINATION_ROWS));
    EXPECT_TRUE(!platform_display_layout_rgb565_384x240(
        source, SHELL_STRIDE, destination, DESTINATION_WIDTH - 1U,
        DESTINATION_ROWS));
    EXPECT_TRUE(!platform_display_layout_rgb565_384x240(
        source, SHELL_STRIDE, destination, DESTINATION_STRIDE,
        DESTINATION_HEIGHT - 1U));
    EXPECT_TRUE(!platform_display_layout_rgb565_384x240(
        NULL, SHELL_STRIDE, destination, DESTINATION_STRIDE,
        DESTINATION_ROWS));

    free(destination);
    free(source);
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

static void test_hdmi_shell_rgb888_layout(void)
{
    enum {
        SHELL_WIDTH = 384,
        SHELL_HEIGHT = 240,
        SHELL_STRIDE = 389,
        HDMI_WIDTH = 1280,
        HDMI_HEIGHT = 720,
        HDMI_STRIDE = (HDMI_WIDTH * 3) + 7,
        HDMI_ROWS = HDMI_HEIGHT + 2,
        LEFT = 160,
        TOP = 60,
    };
    uint16_t *source = calloc(SHELL_STRIDE * SHELL_HEIGHT, sizeof(*source));
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
    source[SHELL_STRIDE] = UINT16_C(0x001f);

    EXPECT_TRUE(platform_display_layout_rgb565_384x240_to_rgb888_1280x720(
        source, SHELL_STRIDE, destination, HDMI_STRIDE, HDMI_ROWS));
    const size_t red = TOP * HDMI_STRIDE + LEFT * 3U;
    EXPECT_EQ(0xff, destination[red]);
    EXPECT_EQ(0x00, destination[red + 1U]);
    EXPECT_EQ(0x00, destination[red + 2U]);
    EXPECT_EQ(0xff, destination[red + 6U]);
    const size_t green = red + 9U;
    EXPECT_EQ(0x00, destination[green]);
    EXPECT_EQ(0xff, destination[green + 1U]);
    EXPECT_EQ(0x00, destination[green + 2U]);
    const size_t blue = (3U * HDMI_STRIDE) + red;
    EXPECT_EQ(0x00, destination[blue]);
    EXPECT_EQ(0x00, destination[blue + 1U]);
    EXPECT_EQ(0xff, destination[blue + 2U]);
    EXPECT_EQ(0x00, destination[red - 1U]);
    EXPECT_EQ(0x00, destination[red - (LEFT * 3U)]);
    EXPECT_EQ(0x00, destination[red + 960U * 3U]);
    EXPECT_EQ(0xa5, destination[HDMI_WIDTH * 3U]);
    EXPECT_EQ(0xa5, destination[HDMI_HEIGHT * HDMI_STRIDE]);

    EXPECT_TRUE(!platform_display_layout_rgb565_384x240_to_rgb888_1280x720(
        source, SHELL_WIDTH - 1U, destination, HDMI_STRIDE, HDMI_ROWS));
    EXPECT_TRUE(!platform_display_layout_rgb565_384x240_to_rgb888_1280x720(
        source, SHELL_STRIDE, destination, HDMI_WIDTH * 3U - 1U,
        HDMI_ROWS));
    EXPECT_TRUE(!platform_display_layout_rgb565_384x240_to_rgb888_1280x720(
        source, SHELL_STRIDE, destination, HDMI_STRIDE, HDMI_HEIGHT - 1U));

    free(destination);
    free(source);
}

#if defined(CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3) && \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
static void rotate_content_region(const uint16_t *source,
                                  uint16_t *destination,
                                  const platform_display_rgb565_region_t *region)
{
    platform_display_rgb565_region_t native;
    EXPECT_TRUE(platform_display_layout_map_content_region_ccw(region,
                                                                &native));
    for (uint16_t y = 0U; y < region->height; ++y) {
        for (uint16_t x = 0U; x < region->width; ++x) {
            const size_t source_index =
                ((size_t)region->y + y) * 768U + region->x + x;
            const size_t native_x = (size_t)native.x + y;
            const size_t native_y = (size_t)native.y +
                region->width - 1U - x;
            destination[native_y * 480U + native_x] = source[source_index];
        }
    }
}

static void test_waveshare_content_region_mapping_and_replay(void)
{
    enum {
        CONTENT_WIDTH = 768,
        CONTENT_HEIGHT = 480,
        NATIVE_WIDTH = 480,
        NATIVE_HEIGHT = 800,
    };
    const size_t source_words = (size_t)CONTENT_WIDTH * CONTENT_HEIGHT;
    const size_t native_words = (size_t)NATIVE_WIDTH * NATIVE_HEIGHT;
    uint16_t *source = malloc(source_words * sizeof(*source));
    uint16_t *reference = calloc(native_words, sizeof(*reference));
    uint16_t *frame0 = calloc(native_words, sizeof(*frame0));
    uint16_t *frame1 = calloc(native_words, sizeof(*frame1));
    EXPECT_TRUE(source != NULL && reference != NULL && frame0 != NULL &&
                frame1 != NULL);
    if (source == NULL || reference == NULL || frame0 == NULL ||
        frame1 == NULL) {
        free(frame1);
        free(frame0);
        free(reference);
        free(source);
        return;
    }
    for (size_t index = 0U; index < source_words; ++index) {
        source[index] = (uint16_t)(index ^ UINT16_C(0x5a5a));
    }
    const platform_display_rgb565_region_t full = {
        .x = 0U, .y = 0U, .width = CONTENT_WIDTH, .height = CONTENT_HEIGHT,
    };
    platform_display_rgb565_region_t native;
    EXPECT_TRUE(platform_display_layout_map_content_region_ccw(&full, &native));
    EXPECT_EQ(0U, native.x);
    EXPECT_EQ(16U, native.y);
    EXPECT_EQ(CONTENT_HEIGHT, native.width);
    EXPECT_EQ(CONTENT_WIDTH, native.height);
    const platform_display_rgb565_region_t edge = {
        .x = 700U, .y = 100U, .width = 68U, .height = 300U,
    };
    EXPECT_TRUE(platform_display_layout_map_content_region_ccw(&edge, &native));
    EXPECT_EQ(100U, native.x);
    EXPECT_EQ(16U, native.y);
    EXPECT_EQ(300U, native.width);
    EXPECT_EQ(68U, native.height);
    const platform_display_rgb565_region_t invalid = {
        .x = 767U, .y = 0U, .width = 2U, .height = 1U,
    };
    EXPECT_TRUE(!platform_display_layout_map_content_region_ccw(&invalid,
                                                                 &native));

    platform_display_rgb565_region_t compacted[4];
    size_t compacted_count = 0U;
    const platform_display_rgb565_region_t grid_strip = {
        .x = 26U, .y = 105U, .width = 674U, .height = 20U,
    };
    const platform_display_rgb565_region_t scrollbar_strip = {
        .x = 712U, .y = 103U, .width = 36U, .height = 120U,
    };
    const platform_display_rgb565_region_t shell_band = {
        .x = 26U, .y = 103U, .width = 722U, .height = 312U,
    };
    const platform_display_rgb565_region_t shell_band_with_status = {
        .x = 19U, .y = 103U, .width = 732U, .height = 360U,
    };
    EXPECT_TRUE(platform_display_layout_compact_content_region(
        compacted, &compacted_count, 4U, &grid_strip));
    EXPECT_TRUE(platform_display_layout_compact_content_region(
        compacted, &compacted_count, 4U, &scrollbar_strip));
    EXPECT_EQ(2U, compacted_count);
    EXPECT_TRUE(platform_display_layout_compact_content_region(
        compacted, &compacted_count, 4U, &shell_band));
    EXPECT_EQ(1U, compacted_count);
    EXPECT_EQ(shell_band.x, compacted[0].x);
    EXPECT_EQ(shell_band.y, compacted[0].y);
    EXPECT_EQ(shell_band.width, compacted[0].width);
    EXPECT_EQ(shell_band.height, compacted[0].height);
    /* Alternating-buffer replay commonly presents the same band twice. It
     * must stay one PPA operation, and a row-change band must supersede it. */
    EXPECT_TRUE(platform_display_layout_compact_content_region(
        compacted, &compacted_count, 4U, &shell_band));
    EXPECT_EQ(1U, compacted_count);
    EXPECT_TRUE(platform_display_layout_compact_content_region(
        compacted, &compacted_count, 4U, &shell_band_with_status));
    EXPECT_EQ(1U, compacted_count);
    EXPECT_EQ(shell_band_with_status.x, compacted[0].x);
    EXPECT_EQ(shell_band_with_status.y, compacted[0].y);
    EXPECT_EQ(shell_band_with_status.width, compacted[0].width);
    EXPECT_EQ(shell_band_with_status.height, compacted[0].height);

    EXPECT_TRUE(platform_display_layout_rgb565_768x480(
        source, CONTENT_WIDTH, reference, NATIVE_WIDTH, NATIVE_HEIGHT));
    rotate_content_region(source, frame0, &full);
    rotate_content_region(source, frame1, &full);
    EXPECT_TRUE(memcmp(frame0, reference, native_words * sizeof(*frame0)) == 0);
    EXPECT_TRUE(memcmp(frame1, reference, native_words * sizeof(*frame1)) == 0);

    /* Model a two-framebuffer replay: frame0 is one generation behind once
     * frame1 has received r1; before frame0 can scan out it must receive both
     * r1 and r2 from the current authoritative source. */
    const platform_display_rgb565_region_t r1 = {
        .x = 26U, .y = 105U, .width = 674U, .height = 20U,
    };
    const platform_display_rgb565_region_t r2 = {
        .x = 712U, .y = 103U, .width = 36U, .height = 120U,
    };
    for (uint16_t y = 0U; y < r1.height; ++y) {
        for (uint16_t x = 0U; x < r1.width; ++x) {
            source[((size_t)r1.y + y) * CONTENT_WIDTH + r1.x + x] =
                UINT16_C(0x1234);
        }
    }
    rotate_content_region(source, frame1, &r1);
    for (uint16_t y = 0U; y < r2.height; ++y) {
        for (uint16_t x = 0U; x < r2.width; ++x) {
            source[((size_t)r2.y + y) * CONTENT_WIDTH + r2.x + x] =
                UINT16_C(0xabcd);
        }
    }
    rotate_content_region(source, frame0, &r1);
    rotate_content_region(source, frame0, &r2);
    EXPECT_TRUE(platform_display_layout_rgb565_768x480(
        source, CONTENT_WIDTH, reference, NATIVE_WIDTH, NATIVE_HEIGHT));
    EXPECT_TRUE(memcmp(frame0, reference, native_words * sizeof(*frame0)) == 0);

    /* The next alternating target has generation one, so replay r2 and its
     * current r3 update. This catches stale-buffer artifacts at both edges. */
    const platform_display_rgb565_region_t r3 = {
        .x = 19U, .y = 427U, .width = 713U, .height = 36U,
    };
    for (uint16_t y = 0U; y < r3.height; ++y) {
        for (uint16_t x = 0U; x < r3.width; ++x) {
            source[((size_t)r3.y + y) * CONTENT_WIDTH + r3.x + x] =
                UINT16_C(0x0f0f);
        }
    }
    rotate_content_region(source, frame1, &r2);
    rotate_content_region(source, frame1, &r3);
    EXPECT_TRUE(platform_display_layout_rgb565_768x480(
        source, CONTENT_WIDTH, reference, NATIVE_WIDTH, NATIVE_HEIGHT));
    EXPECT_TRUE(memcmp(frame1, reference, native_words * sizeof(*frame1)) == 0);

    free(frame1);
    free(frame0);
    free(reference);
    free(source);
}
#endif

int main(void)
{
    test_bounds_and_scaling();
    test_shell_bounds_and_exact_viewport();
    test_standard_rgb565_target_byte_order();
    test_hdmi_rgb888_layout();
    test_hdmi_shell_rgb888_layout();
#if defined(CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3) && \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    test_waveshare_content_region_mapping_and_replay();
#endif
    if (failures != 0U) {
        fprintf(stderr, "platform display layout tests failed: %u\n", failures);
        return 1;
    }
#if defined(CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3) && \
    CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
    puts("P4_DISPLAY_LAYOUT HOST PASS rgb565=480x800 rotated viewport=768x480");
#else
    puts("P4_DISPLAY_LAYOUT HOST PASS rgb565=1024x600 rgb888=1280x720 viewport=960x600");
#endif
    return 0;
}
