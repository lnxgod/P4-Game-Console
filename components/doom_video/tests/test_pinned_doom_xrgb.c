// SPDX-License-Identifier: GPL-2.0-or-later
/* Exercises the exact vendored palette conversion and actual gamma tables.
 * CMake builds fast-path, CMAP256, and unknown-byte-order variants.
 */

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-non-prototype"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wstrict-prototypes"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wunused-const-variable"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "i_video.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TEST_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define TEST_ASAN 1
#endif
#ifdef TEST_ASAN
#include <sanitizer/asan_interface.h>
#define POISON(p, n) __asan_poison_memory_region((p), (n))
#define UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#else
#define POISON(p, n) ((void)0)
#define UNPOISON(p, n) ((void)0)
#endif

static uint64_t converted_rows;
static uint64_t distinct_colors;
static unsigned fallback_cases;
static const char *phase;
static int case_gamma;
static uint32_t case_base;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s; phase=%s gamma=%d base=%u\n", \
                __FILE__, __LINE__, #condition, phase, case_gamma, case_base); \
        abort(); \
    } \
} while (0)

void I_Error(char *error, ...)
{
    fprintf(stderr, "Unexpected I_Error: %s\n", error);
    abort();
}

/* Generic conversion before the packed-palette fast path; body unchanged. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wsign-conversion"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
static void baseline_cmap_to_fb(uint8_t *out, uint8_t *in, int in_pixels)
{
    int i, k;
    struct color c;
    uint32_t pix;

    for (i = 0; i < in_pixels; i++)
    {
        c = colors[*in];  // R:8 G:8 B:8

        if (s_Fb.bits_per_pixel == 16)
        {
            // RGB565 packing
            uint16_t p = ((c.r & 0xF8) << 8) |
                         ((c.g & 0xFC) << 3) |
                         (c.b >> 3);

#ifdef SYS_BIG_ENDIAN
            p = swapeLE16(p); // can't use SHORT() because this needs to stay unsigned
#endif
            for (k = 0; k < fb_scaling; k++) {
                *(uint16_t *)out = p;
                out += 2;
            }
        }
        else if (s_Fb.bits_per_pixel == 32)
        {
            // Assuming RGBA8888
            pix = (c.r << s_Fb.red.offset) |
                  (c.g << s_Fb.green.offset) |
                  (c.b << s_Fb.blue.offset);

#ifdef SYS_BIG_ENDIAN
            pix = swapLE32(pix);
#endif
            for (k = 0; k < fb_scaling; k++) {
                *(uint32_t *)out = pix;
                out += 4;
            }
        }
        else {
            // no clue how to convert this
            I_Error("No idea how to convert %d bpp pixels", s_Fb.bits_per_pixel);
        }

        in++;
    }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void standard_mode(void)
{
    memset(&s_Fb, 0, sizeof(s_Fb));
    s_Fb.bits_per_pixel = 32;
    s_Fb.red.offset = 16;
    s_Fb.green.offset = 8;
    s_Fb.blue.offset = 0;
    s_Fb.red.length = s_Fb.green.length = s_Fb.blue.length = 8;
    s_Fb.transp.offset = 24;
    s_Fb.transp.length = 8;
    fb_scaling = 1;
}

static uint32_t independent_pixel(const byte *palette, unsigned index)
{
    uint32_t red = gammatable[usegamma][palette[3U * index]];
    uint32_t green = gammatable[usegamma][palette[3U * index + 1U]];
    uint32_t blue = gammatable[usegamma][palette[3U * index + 2U]];
    if (s_Fb.bits_per_pixel == 16)
        return (red / 8U) * 2048U + (green / 4U) * 32U + blue / 8U;
    return (red << s_Fb.red.offset) |
           (green << s_Fb.green.offset) |
           (blue << s_Fb.blue.offset);
}

static void verify_palette_install(byte *palette)
{
    byte saved[768];
    memcpy(saved, palette, sizeof(saved));
    I_SetPalette(palette);
    CHECK(memcmp(saved, palette, sizeof(saved)) == 0);
    for (unsigned i = 0; i < 256; ++i) {
        CHECK(colors[i].r == gammatable[usegamma][palette[3U * i]]);
        CHECK(colors[i].g == gammatable[usegamma][palette[3U * i + 1U]]);
        CHECK(colors[i].b == gammatable[usegamma][palette[3U * i + 2U]]);
        CHECK(colors[i].a == 0);
    }
#ifdef CMAP256
    CHECK(palette_changed);
    palette_changed = false;
#endif
}

/* Exactly 320 source indices, including all 256 palette slots in varying order.
 * Guard poisoning catches reads outside the source as well as writes outside
 * the output. Separate independent expected words prevent a shared palette
 * installation bug from passing just because both conversion paths share it. */
static void verify_fast_row(const byte *palette, unsigned shift)
{
    uint8_t input[16 + 320 + 16];
    uint8_t original[sizeof(input)];
    uint32_t actual[1 + 320 + 1];
    uint32_t reference[1 + 320 + 1];
    memset(input, 0x69, sizeof(input));
    for (unsigned i = 0; i < 320; ++i)
        input[16 + i] = (uint8_t)(i * 197U + shift);
    memcpy(original, input, sizeof(input));
    memset(actual, 0xa5, sizeof(actual));
    memset(reference, 0xa5, sizeof(reference));
    POISON(input, 16);
    POISON(input + 16 + 320, 16);
    cmap_to_fb((uint8_t *)(actual + 1), input + 16, 320);
    baseline_cmap_to_fb((uint8_t *)(reference + 1), input + 16, 320);
    UNPOISON(input, sizeof(input));
    CHECK(memcmp(input, original, sizeof(input)) == 0);
    CHECK(memcmp(actual, reference, sizeof(actual)) == 0);
    CHECK(actual[0] == UINT32_C(0xa5a5a5a5));
    CHECK(actual[321] == UINT32_C(0xa5a5a5a5));
    for (unsigned i = 0; i < 320; ++i)
        CHECK(actual[i + 1] == independent_pixel(palette, input[16 + i]));
    ++converted_rows;
}

static void test_full_rgb_gamma_sweep(void)
{
    byte palette[768];
    phase = "exhaustive RGB/gamma/index/palette-refresh";
    standard_mode();
    for (usegamma = 0; usegamma < 5; ++usegamma) {
        case_gamma = usegamma;
        for (uint32_t base = 0; base < UINT32_C(0x1000000); base += 256) {
            case_base = base;
            /* Odd multiplication modulo 2^24 is a bijection. The rotating
             * slot mapping is separately a permutation modulo 256. */
            for (unsigned i = 0; i < 256; ++i) {
                uint32_t rgb = ((base + i) * UINT32_C(0x005bd1e9) +
                                UINT32_C(0x00139aff)) & UINT32_C(0xffffff);
                unsigned slot = (i * 73U + (base >> 8)) & 255U;
                palette[3U * slot] = (byte)(rgb >> 16);
                palette[3U * slot + 1U] = (byte)(rgb >> 8);
                palette[3U * slot + 2U] = (byte)rgb;
            }
            verify_palette_install(palette);
            verify_fast_row(palette, base >> 8);
            distinct_colors += 256;
        }
        printf("RGB sweep gamma=%d PASS colors=16777216 palette_slots=256\n", usegamma);
        fflush(stdout);
    }
}

static void pattern_palette(byte *palette, unsigned seed)
{
    for (unsigned i = 0; i < 768; ++i)
        palette[i] = (byte)(i * 151U + i / 3U * 37U + seed * 83U);
}

static void test_palette_and_gamma_changes(void)
{
    byte palette[768];
    phase = "palette/gamma refresh";
    standard_mode();
    for (unsigned change = 0; change < 12; ++change) {
        usegamma = (int)((change * 3U) % 5U);
        case_gamma = usegamma;
        case_base = change;
        pattern_palette(palette, change);
        verify_palette_install(palette);
        verify_fast_row(palette, change * 53U);
        /* Reinstall the identical raw palette at another gamma level. */
        usegamma = (usegamma + 1) % 5;
        case_gamma = usegamma;
        verify_palette_install(palette);
        verify_fast_row(palette, change * 17U);
    }
}

static bool fast_mode_matches(int count)
{
    return count == 320 && fb_scaling == 1 && s_Fb.bits_per_pixel == 32 &&
           s_Fb.red.offset == 16 && s_Fb.green.offset == 8 &&
           s_Fb.blue.offset == 0 && s_Fb.red.length == 8 &&
           s_Fb.green.length == 8 && s_Fb.blue.length == 8;
}

static void verify_generic_case(byte *palette, int count, unsigned offset)
{
    const size_t prefix = 16U + offset;
    const size_t bytes_per_pixel = s_Fb.bits_per_pixel / 8U;
    const size_t output_size = (size_t)count * (size_t)fb_scaling * bytes_per_pixel;
    const size_t allocation_size = prefix + output_size + 16U;
    uint8_t *actual = malloc(allocation_size);
    uint8_t *reference = malloc(allocation_size);
    uint8_t *input = malloc(16U + (size_t)count + 16U);
    uint8_t *original = malloc(16U + (size_t)count + 16U);
    CHECK(actual && reference && input && original);
    memset(actual, 0xa5, allocation_size);
    memset(reference, 0xa5, allocation_size);
    memset(input, 0x69, 16U + (size_t)count + 16U);
    for (int i = 0; i < count; ++i)
        input[16 + i] = (uint8_t)((unsigned)i * 197U + 71U);
    memcpy(original, input, 16U + (size_t)count + 16U);
    verify_palette_install(palette);
#ifdef P4_DOOM_XRGB_FASTPATH
    /* Distinguish correct fallback from an over-broad gate even when the
     * generic format happens to produce the same bytes as standard XRGB. */
    if (!fast_mode_matches(count)) {
        for (unsigned i = 0; i < 256; ++i)
            xrgb8888_palette[i] = UINT32_C(0xffaabbcc);
    }
#else
    (void)fast_mode_matches;
#endif
    POISON(input, 16);
    POISON(input + 16U + (size_t)count, 16);
    cmap_to_fb(actual + prefix, input + 16, count);
    baseline_cmap_to_fb(reference + prefix, input + 16, count);
    UNPOISON(input, 16U + (size_t)count + 16U);
    CHECK(memcmp(input, original, 16U + (size_t)count + 16U) == 0);
    CHECK(memcmp(actual, reference, allocation_size) == 0);
    for (size_t i = 0; i < prefix; ++i) CHECK(actual[i] == 0xa5);
    for (size_t i = prefix + output_size; i < allocation_size; ++i)
        CHECK(actual[i] == 0xa5);
    size_t position = prefix;
    for (int i = 0; i < count; ++i) {
        uint32_t expected = independent_pixel(palette, input[16 + i]);
        for (int scale = 0; scale < fb_scaling; ++scale) {
            for (size_t byte_index = 0; byte_index < bytes_per_pixel; ++byte_index)
                CHECK(actual[position++] == (uint8_t)(expected >> (byte_index * 8U)));
        }
    }
    CHECK(position == prefix + output_size);
    free(original);
    free(input);
    free(reference);
    free(actual);
    ++fallback_cases;
}

static void test_format_scale_count_gates(void)
{
    static const int counts[] = {0, 1, 3, 319, 320, 321};
    static const int scales[] = {0, 1, 2, 3};
    byte palette[768];
    phase = "format/scale/count/alignment gates";
    for (usegamma = 0; usegamma < 5; ++usegamma) {
        case_gamma = usegamma;
        pattern_palette(palette, (unsigned)usegamma + 91U);
        for (unsigned mode = 0; mode < 12; ++mode) {
            standard_mode();
            switch (mode) {
            case 0: break;
            case 1: s_Fb.bits_per_pixel = 16; break;
            case 2: s_Fb.red.offset = 0; s_Fb.blue.offset = 16; break;
            case 3: s_Fb.red.offset = 8; s_Fb.green.offset = 16; break;
            case 4: s_Fb.blue.offset = 1; break;
            case 5: s_Fb.red.length = 7; break;
            case 6: s_Fb.green.length = 6; break;
            case 7: s_Fb.blue.length = 5; break;
            case 8: s_Fb.red.length = 0; break;
            case 9: s_Fb.transp.length = 0; s_Fb.transp.offset = 0; break;
            case 10: s_Fb.red.offset = 17; break;
            case 11: s_Fb.green.offset = 9; break;
            }
            for (unsigned scale = 0; scale < sizeof(scales) / sizeof(scales[0]); ++scale) {
                fb_scaling = scales[scale];
                for (unsigned n = 0; n < sizeof(counts) / sizeof(counts[0]); ++n) {
                    case_base = mode * 100U + scale * 10U + n;
                    verify_generic_case(palette, counts[n], 0);
                    if (s_Fb.bits_per_pixel == 16)
                        verify_generic_case(palette, counts[n], 2);
                }
            }
        }
    }
}

int main(void)
{
    const uint32_t one = 1;
    CHECK(*(const uint8_t *)&one == 1);
#if defined(CMAP256) || defined(P4_TEST_NO_BYTE_ORDER)
#ifdef P4_DOOM_XRGB_FASTPATH
#error "Fast path must not compile for CMAP256 or unknown compiler byte order"
#endif
#else
#ifndef P4_DOOM_XRGB_FASTPATH
#error "Expected little-endian XRGB fast path to compile"
#endif
#endif
    test_palette_and_gamma_changes();
    test_format_scale_count_gates();
    test_full_rgb_gamma_sweep();
    CHECK(distinct_colors == UINT64_C(83886080));
    printf("P4_PALETTE_FAST HOST PASS colors=%llu gamma_tables=5 rows=%llu "
           "format_scale_count_cases=%u guards=input+output palette=input-unchanged "
#ifdef P4_DOOM_XRGB_FASTPATH
           "compiled_fastpath=yes\n",
#else
           "compiled_fastpath=no\n",
#endif
           (unsigned long long)distinct_colors,
           (unsigned long long)converted_rows, fallback_cases);
    return 0;
}
