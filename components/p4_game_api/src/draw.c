// SPDX-License-Identifier: MIT

#include "p4/draw.h"

#include <stdint.h>
#include <string.h>

#include "p4/cp437.h"

bool p4_surface_valid(const p4_game_surface_t *surface)
{
    if (surface == NULL || surface->pixels == NULL ||
        surface->stride_pixels < surface->width) {
        return false;
    }
    return (surface->width == P4_GAME_SURFACE_WIDTH &&
            surface->height == P4_GAME_SURFACE_HEIGHT) ||
        (surface->width == P4_GAME_SURFACE_HIGH_RES_WIDTH &&
         surface->height == P4_GAME_SURFACE_HIGH_RES_HEIGHT);
}

void p4_draw_pixel(p4_game_surface_t *surface,
                   int x, int y, uint16_t color)
{
    if (p4_surface_valid(surface) && x >= 0 && y >= 0 &&
        x < surface->width && y < surface->height) {
        surface->pixels[(size_t)y * surface->stride_pixels + (size_t)x] = color;
    }
}

void p4_draw_fill_rect(p4_game_surface_t *surface,
                       int x, int y, int width, int height,
                       uint16_t color)
{
    if (!p4_surface_valid(surface) || width <= 0 || height <= 0) {
        return;
    }
    int64_t left = x;
    int64_t top = y;
    int64_t right = left + width;
    int64_t bottom = top + height;
    if (right <= 0 || bottom <= 0 || left >= surface->width ||
        top >= surface->height) {
        return;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > surface->width) {
        right = surface->width;
    }
    if (bottom > surface->height) {
        bottom = surface->height;
    }
    /* Clip in 64 bits to accept the full int input range, then walk bounded
     * native rows. RV32 must not repeat 64-bit comparisons or row address
     * multiplication for every pixel in a high-resolution clear. */
    uint16_t *const pixels = surface->pixels;
    const size_t stride = surface->stride_pixels;
    const size_t start_x = (size_t)left;
    const size_t count = (size_t)(right - left);
    const unsigned end_y = (unsigned)bottom;
    for (unsigned row = (unsigned)top; row < end_y; ++row) {
        uint16_t *pixel = pixels + (size_t)row * stride + start_x;
        size_t remaining = count;
        /* Peel one halfword so every wider store is naturally aligned. The
         * may_alias type permits accessing the uint16_t surface as packed
         * equal-color pairs without relying on strict-aliasing violations. */
        if (((uintptr_t)pixel & 3U) != 0U) {
            *pixel++ = color;
            --remaining;
        }
        typedef uint32_t pixel_pair_t __attribute__((__may_alias__));
        pixel_pair_t *pairs = (pixel_pair_t *)(void *)pixel;
        const uint32_t pair = (uint32_t)color | ((uint32_t)color << 16U);
        const size_t pair_count = remaining / 2U;
        pixel_pair_t *const pair_end = pairs + pair_count;
        if (pair_count != 0U) {
            do { *pairs++ = pair; } while (pairs != pair_end);
        }
        if ((remaining & 1U) != 0U) *(uint16_t *)(void *)pairs = color;
    }
}

void p4_draw_clear(p4_game_surface_t *surface, uint16_t color)
{
    if (!p4_surface_valid(surface)) {
        return;
    }
    p4_draw_fill_rect(surface, 0, 0,
                      surface->width, surface->height,
                      color);
}

void p4_draw_rect(p4_game_surface_t *surface,
                  int x, int y, int width, int height,
                  uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    p4_draw_fill_rect(surface, x, y, width, 1, color);
    p4_draw_fill_rect(surface, x, y + height - 1, width, 1, color);
    p4_draw_fill_rect(surface, x, y, 1, height, color);
    p4_draw_fill_rect(surface, x + width - 1, y, 1, height, color);
}

void p4_draw_fill_circle(p4_game_surface_t *surface,
                         int center_x, int center_y, int radius,
                         uint16_t color)
{
    if (!p4_surface_valid(surface) || radius < 0 || radius > 1024) {
        return;
    }
    /* Reject using wide bounds once. Any intersecting center is then within
     * 1024 pixels of the surface, so all row geometry fits signed 32 bits. */
    if ((int64_t)center_x + radius < 0 || (int64_t)center_y + radius < 0 ||
        (int64_t)center_x - radius >= surface->width ||
        (int64_t)center_y - radius >= surface->height) return;
    int top = center_y - radius, bottom = center_y + radius;
    if (top < 0) top = 0;
    if (bottom >= surface->height) bottom = (int)surface->height - 1;
    const int radius_squared = radius * radius;
    for (int row = top; row <= bottom; ++row) {
        const int dy = row - center_y;
        const int remaining = radius_squared - dy * dy;
        /* Find floor(sqrt(remaining)) with bounded integer comparisons;
         * the original circle includes exactly x*x + y*y <= r*r. */
        int low = 0, high = radius;
        while (low < high) {
            const int mid = (low + high + 1) >> 1;
            if (mid * mid <= remaining) low = mid;
            else high = mid - 1;
        }
        int left = center_x - low, right = center_x + low + 1;
        if (left < 0) left = 0;
        if (right > surface->width) right = surface->width;
        if (left >= right) continue;
        uint16_t *dst = surface->pixels + (size_t)row * surface->stride_pixels + (size_t)left;
        uint16_t *const end = dst + (size_t)(right - left);
        do { *dst++ = color; } while (dst != end);
    }
}

static void glyph_rows(char character, uint8_t rows[7])
{
    memset(rows, 0, 7U);
    char value = character;
    if (value >= 'a' && value <= 'z') {
        value = (char)(value - 'a' + 'A');
    }
#define GLYPH(a,b,c_,d,e,f,g) do { \
        rows[0] = (a); rows[1] = (b); rows[2] = (c_); rows[3] = (d); \
        rows[4] = (e); rows[5] = (f); rows[6] = (g); \
    } while (0)
    switch (value) {
    case 'A': GLYPH(14,17,17,31,17,17,17); break;
    case 'B': GLYPH(30,17,17,30,17,17,30); break;
    case 'C': GLYPH(14,17,16,16,16,17,14); break;
    case 'D': GLYPH(30,17,17,17,17,17,30); break;
    case 'E': GLYPH(31,16,16,30,16,16,31); break;
    case 'F': GLYPH(31,16,16,30,16,16,16); break;
    case 'G': GLYPH(14,17,16,23,17,17,15); break;
    case 'H': GLYPH(17,17,17,31,17,17,17); break;
    case 'I': GLYPH(31,4,4,4,4,4,31); break;
    case 'J': GLYPH(7,2,2,2,18,18,12); break;
    case 'K': GLYPH(17,18,20,24,20,18,17); break;
    case 'L': GLYPH(16,16,16,16,16,16,31); break;
    case 'M': GLYPH(17,27,21,21,17,17,17); break;
    case 'N': GLYPH(17,25,21,19,17,17,17); break;
    case 'O': GLYPH(14,17,17,17,17,17,14); break;
    case 'P': GLYPH(30,17,17,30,16,16,16); break;
    case 'Q': GLYPH(14,17,17,17,21,18,13); break;
    case 'R': GLYPH(30,17,17,30,20,18,17); break;
    case 'S': GLYPH(15,16,16,14,1,1,30); break;
    case 'T': GLYPH(31,4,4,4,4,4,4); break;
    case 'U': GLYPH(17,17,17,17,17,17,14); break;
    case 'V': GLYPH(17,17,17,17,17,10,4); break;
    case 'W': GLYPH(17,17,17,21,21,21,10); break;
    case 'X': GLYPH(17,17,10,4,10,17,17); break;
    case 'Y': GLYPH(17,17,10,4,4,4,4); break;
    case 'Z': GLYPH(31,1,2,4,8,16,31); break;
    case '0': GLYPH(14,17,19,21,25,17,14); break;
    case '1': GLYPH(4,12,4,4,4,4,14); break;
    case '2': GLYPH(14,17,1,2,4,8,31); break;
    case '3': GLYPH(30,1,1,14,1,1,30); break;
    case '4': GLYPH(2,6,10,18,31,2,2); break;
    case '5': GLYPH(31,16,16,30,1,1,30); break;
    case '6': GLYPH(14,16,16,30,17,17,14); break;
    case '7': GLYPH(31,1,2,4,8,8,8); break;
    case '8': GLYPH(14,17,17,14,17,17,14); break;
    case '9': GLYPH(14,17,17,15,1,1,14); break;
    case '-': GLYPH(0,0,0,31,0,0,0); break;
    case '.': GLYPH(0,0,0,0,0,12,12); break;
    case ':': GLYPH(0,12,12,0,12,12,0); break;
    case '/': GLYPH(1,2,2,4,8,8,16); break;
    case '+': GLYPH(0,4,4,31,4,4,0); break;
    case '<': GLYPH(2,4,8,16,8,4,2); break;
    case '>': GLYPH(8,4,2,1,2,4,8); break;
    case '?': GLYPH(14,17,1,2,4,0,4); break;
    default: break;
    }
#undef GLYPH
}

void p4_draw_text(p4_game_surface_t *surface,
                  int x, int y, const char *text,
                  uint16_t color, unsigned scale,
                  size_t max_characters)
{
    if (!p4_surface_valid(surface) || text == NULL ||
        scale == 0U || scale > 8U || max_characters > 256U) {
        return;
    }
    int cursor = x;
    for (size_t index = 0U;
         index < max_characters && text[index] != '\0'; ++index) {
        uint8_t rows[7];
        glyph_rows(text[index], rows);
        for (unsigned row = 0U; row < 7U; ++row) {
            for (unsigned column = 0U; column < 5U; ++column) {
                const uint8_t mask = (uint8_t)(
                    UINT8_C(1) << (4U - column));
                if ((rows[row] & mask) != 0U) {
                    p4_draw_fill_rect(
                        surface,
                        cursor + (int)(column * scale),
                        y + (int)(row * scale),
                        (int)scale, (int)scale, color);
                }
            }
        }
        cursor += (int)(6U * scale);
    }
}

void p4_draw_cp437_glyph(p4_game_surface_t *surface,
                         int x, int y, uint8_t character,
                         uint16_t foreground, uint16_t background,
                         unsigned cell_height)
{
    if (!p4_surface_valid(surface) ||
        (cell_height != P4_DRAW_CP437_COMPACT_HEIGHT &&
         cell_height != P4_DRAW_CP437_FULL_HEIGHT)) {
        return;
    }
    const size_t glyph = (size_t)character * P4_CP437_GLYPH_HEIGHT;
    for (unsigned row = 0U; row < cell_height; ++row) {
        uint8_t bits;
        if (cell_height == P4_DRAW_CP437_COMPACT_HEIGHT) {
            const size_t source_row = (size_t)row * 2U;
            bits = (uint8_t)(p4_cp437_font_8x16[glyph + source_row] |
                             p4_cp437_font_8x16[glyph + source_row + 1U]);
        } else {
            bits = p4_cp437_font_8x16[glyph + row];
        }
        const int64_t destination_y = (int64_t)y + row;
        if (destination_y < 0 || destination_y >= surface->height) {
            continue;
        }
        for (unsigned column = 0U;
             column < P4_DRAW_CP437_CELL_WIDTH; ++column) {
            const int64_t destination_x = (int64_t)x + column;
            if (destination_x < 0 || destination_x >= surface->width) {
                continue;
            }
            const uint8_t mask = (uint8_t)(UINT8_C(0x80) >> column);
            surface->pixels[(size_t)destination_y * surface->stride_pixels +
                            (size_t)destination_x] =
                (bits & mask) != 0U ? foreground : background;
        }
    }
}

void p4_draw_cp437_text(p4_game_surface_t *surface,
                        int x, int y,
                        const uint8_t *bytes, size_t byte_count,
                        uint16_t foreground, uint16_t background,
                        unsigned cell_height)
{
    if (!p4_surface_valid(surface) ||
        (byte_count > 0U && bytes == NULL) ||
        byte_count > P4_DRAW_CP437_MAX_TEXT_BYTES ||
        (cell_height != P4_DRAW_CP437_COMPACT_HEIGHT &&
         cell_height != P4_DRAW_CP437_FULL_HEIGHT)) {
        return;
    }
    for (size_t index = 0U; index < byte_count; ++index) {
        const int64_t cell_x = (int64_t)x +
            (int64_t)index * P4_DRAW_CP437_CELL_WIDTH;
        if (cell_x > INT32_MAX || cell_x < INT32_MIN) {
            continue;
        }
        p4_draw_cp437_glyph(surface, (int)cell_x, y, bytes[index],
                            foreground, background, cell_height);
    }
}

void p4_draw_sprite_rgb565(p4_game_surface_t *surface,
                           int x, int y,
                           const uint16_t *pixels,
                           size_t width, size_t height,
                           size_t stride_pixels,
                           bool use_transparency,
                           uint16_t transparent_color)
{
    if (!p4_surface_valid(surface) || pixels == NULL || width == 0U ||
        height == 0U || width > 4096U || height > 4096U ||
        stride_pixels < width) {
        return;
    }
    const int64_t right = (int64_t)x + (int64_t)width;
    const int64_t bottom = (int64_t)y + (int64_t)height;
    if (right <= 0 || bottom <= 0 || x >= surface->width || y >= surface->height) return;
    const size_t first_x = x < 0 ? (size_t)(-(int64_t)x) : 0U;
    const size_t first_y = y < 0 ? (size_t)(-(int64_t)y) : 0U;
    const size_t end_x = right > surface->width ? (size_t)((int)surface->width - x) : width;
    const size_t end_y = bottom > surface->height ? (size_t)((int)surface->height - y) : height;
    for (size_t row = first_y; row < end_y; ++row) {
        const uint16_t *src = pixels + row * stride_pixels + first_x;
        uint16_t *dst = surface->pixels + (size_t)(y + (int)row) * surface->stride_pixels + (size_t)(x + (int)first_x);
        uint16_t *const end = dst + end_x - first_x;
        /* Keep original forward read/write order, including aliased source
         * surfaces; memcpy would silently change that behavior. */
        do {
            const uint16_t value = *src++;
            if (!use_transparency || value != transparent_color) *dst = value;
            ++dst;
        } while (dst != end);
    }
}
