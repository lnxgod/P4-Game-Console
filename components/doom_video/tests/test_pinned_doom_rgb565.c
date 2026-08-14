// SPDX-License-Identifier: GPL-2.0-or-later

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-non-prototype"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wunused-const-variable"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

/* Exercise the exact vendored conversion, including its private mode state. */
#include "i_video.c"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;

void I_Error(char *error, ...)
{
    (void)error;
    abort();
}

#define EXPECT_EQ(expected_, actual_)                                           \
    do {                                                                        \
        const uint16_t expected_value_ = (uint16_t)(expected_);                 \
        const uint16_t actual_value_ = (uint16_t)(actual_);                     \
        if (expected_value_ != actual_value_) {                                 \
            fprintf(stderr, "%s:%d: expected 0x%04x, got 0x%04x\n",       \
                    __FILE__, __LINE__, (unsigned)expected_value_,              \
                    (unsigned)actual_value_);                                   \
            failures++;                                                        \
        }                                                                       \
    } while (0)

static void set_color(unsigned index, uint8_t red, uint8_t green, uint8_t blue)
{
    colors[index].r = red;
    colors[index].g = green;
    colors[index].b = blue;
    colors[index].a = 0U;
}

int main(void)
{
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "ESP32-P4 framebuffer byte-order proof requires a little-endian host"
#endif
    const uint8_t source[] = {0U, 1U, 2U, 3U, 4U, 5U};
    uint16_t converted[12] = {0};

    s_Fb.bits_per_pixel = 16U;
    fb_scaling = 1;
    set_color(0U, 255U, 0U, 0U);
    set_color(1U, 0U, 255U, 0U);
    set_color(2U, 0U, 0U, 255U);
    set_color(3U, 255U, 255U, 255U);
    set_color(4U, 0U, 0U, 0U);
    set_color(5U, 18U, 52U, 86U);

    cmap_to_fb((uint8_t *)converted, (uint8_t *)source, 6);
    EXPECT_EQ(UINT16_C(0xf800), converted[0]);
    EXPECT_EQ(UINT16_C(0x07e0), converted[1]);
    EXPECT_EQ(UINT16_C(0x001f), converted[2]);
    EXPECT_EQ(UINT16_C(0xffff), converted[3]);
    EXPECT_EQ(UINT16_C(0x0000), converted[4]);
    EXPECT_EQ(UINT16_C(0x11aa), converted[5]);

    const uint8_t expected_bytes[] = {
        0x00U, 0xf8U, 0xe0U, 0x07U, 0x1fU, 0x00U, 0xffU, 0xffU,
    };
    if (memcmp(converted, expected_bytes, sizeof(expected_bytes)) != 0) {
        fputs("pinned RGB565 byte-order mismatch\n", stderr);
        failures++;
    }

    memset(converted, 0, sizeof(converted));
    fb_scaling = 2;
    cmap_to_fb((uint8_t *)converted, (uint8_t *)source, 2);
    EXPECT_EQ(UINT16_C(0xf800), converted[0]);
    EXPECT_EQ(UINT16_C(0xf800), converted[1]);
    EXPECT_EQ(UINT16_C(0x07e0), converted[2]);
    EXPECT_EQ(UINT16_C(0x07e0), converted[3]);

    if (failures != 0U) {
        fprintf(stderr, "Pinned Doom RGB565 tests failed: %u\n", failures);
        return 1;
    }
    puts("P4_DOOM_PINNED_RGB565 HOST PASS input=palette8 output=rgb565-le");
    return 0;
}
