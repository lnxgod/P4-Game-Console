// SPDX-License-Identifier: MIT
// Inline presentation helpers and one cartridge-local font: no runtime imports.
#ifndef P4_PRESENTATION_H
#define P4_PRESENTATION_H
#include "p4/draw.h"
#include "p4/presentation_font.h"
#include <string.h>

/* Drawing uses actual surface pixels. Input always remains 320 x 200. */
static inline int p4_ui_x(const p4_game_surface_t *s, int x)
{
    return s == NULL || x < -32768 || x > 32767 ? 0 :
        x * (int)s->width / P4_GAME_SURFACE_WIDTH;
}
static inline int p4_ui_y(const p4_game_surface_t *s, int y)
{
    return s == NULL || y < -32768 || y > 32767 ? 0 :
        y * (int)s->height / P4_GAME_SURFACE_HEIGHT;
}
static inline uint16_t p4_ui_blend(uint16_t back, uint16_t front, unsigned a)
{
    if (a >= 15U) return front;
    const unsigned inv = 15U - a;
    /* Each numerator is 0..952. In that range (n * 2185) >> 15 is
     * exactly n / 15 (including the existing +7 rounding), using RV32 mul
     * and shift instead of three hardware divisions per covered pixel. */
    const unsigned r = ((((unsigned)(back >> 11U) * inv) +
        ((unsigned)(front >> 11U) * a) + 7U) * 2185U) >> 15U;
    const unsigned g = (((((unsigned)back >> 5U & 63U) * inv) +
        (((unsigned)front >> 5U & 63U) * a) + 7U) * 2185U) >> 15U;
    const unsigned b = (((((unsigned)back & 31U) * inv) +
        (((unsigned)front & 31U) * a) + 7U) * 2185U) >> 15U;
    return (uint16_t)((r << 11U) | (g << 5U) | b);
}
static inline unsigned p4_ui_font_height(unsigned height)
{
    return height < 8U ? 8U : (height > 128U ? 128U : height);
}
static inline int p4_ui_text_width(const char *text, unsigned height,
                                    size_t max_chars)
{
    if (text == NULL) return 0;
    height = p4_ui_font_height(height);
    if (max_chars > 256U) max_chars = 256U;
    int width = 0;
    for (size_t i = 0; i < max_chars && text[i] != '\0'; ++i) {
        unsigned ch = (unsigned char)text[i];
        if (ch < 32U || ch > 126U) ch = 63U;
        width += (int)(((unsigned)p4_ui_font_advance[ch - 32U] * height + 14U) / 28U);
    }
    return width;
}
static inline void p4_ui_text(p4_game_surface_t *s, int x, int y,
                               const char *text, uint16_t color,
                               unsigned height, size_t max_chars)
{
    if (!p4_surface_valid(s) || text == NULL ||
        x < -32768 || x > 32767 || y < -32768 || y > 32767) return;
    height = p4_ui_font_height(height);
    const unsigned width = (24U * height + 14U) / 28U;
    if (max_chars > 256U) max_chars = 256U;
    const int start_y = y < 0 ? -y : 0;
    const int end_y = y + (int)height > (int)s->height
        ? (int)s->height - y : (int)height;
    if (start_y >= end_y || max_chars == 0U) return;
    /* Bounded per-call sampling maps: share them across all glyphs instead
     * of doing two divisions for every scanned destination pixel. */
    uint8_t source_x[110];
    uint8_t source_y[128];
    for (unsigned px = 0; px < width; ++px)
        source_x[px] = (uint8_t)(px * 24U / width);
    for (int py = start_y; py < end_y; ++py)
        source_y[py] = (uint8_t)((unsigned)py * 28U / height);
    for (size_t i = 0; i < max_chars && text[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 32U || ch > 126U) ch = 63U;
        const int start_x = x < 0 ? -x : 0;
        const int end_x = x + (int)width > (int)s->width
            ? (int)s->width - x : (int)width;
        const uint8_t *glyph = p4_ui_font_alpha + ((unsigned)ch - 32U) * 336U;
        if (start_x < end_x) {
            for (int py = start_y; py < end_y; ++py) {
                const uint8_t *row = glyph + (unsigned)source_y[py] * 12U;
                uint16_t *dst = s->pixels + (size_t)(y + py) * s->stride_pixels + (size_t)(x + start_x);
                for (int px = start_x; px < end_x; ++px, ++dst) {
                    const unsigned sx = source_x[px];
                    const unsigned packed = row[sx >> 1U];
                    const unsigned a = (sx & 1U) == 0U ? packed >> 4U : packed & 15U;
                    if (a == 15U) *dst = color;
                    else if (a != 0U) *dst = p4_ui_blend(*dst, color, a);
                }
            }
        }
        x += (int)(((unsigned)p4_ui_font_advance[ch - 32U] * height + 14U) / 28U);
        if (x >= (int)s->width) break;
    }
}
static inline void p4_ui_round_rect(p4_game_surface_t *s, int x, int y,
                                    int width, int height, int radius,
                                    uint16_t color)
{
    if (!p4_surface_valid(s) || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096 || x < -8192 || x > 8192 ||
        y < -8192 || y > 8192) return;
    if (radius < 0) radius = 0;
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    const int start_y = y < 0 ? -y : 0;
    const int end_y = y + height > (int)s->height ? (int)s->height - y : height;
    for (int row = start_y; row < end_y; ++row) {
        int inset = 0;
        const int edge = row < height / 2 ? row : height - 1 - row;
        if (edge < radius) {
            const int dy = radius - 1 - edge;
            while (inset < radius &&
                   (radius - inset) * (radius - inset) + dy * dy > radius * radius) ++inset;
        }
        int left = x + inset, right = x + width - inset;
        if (left < 0) left = 0;
        if (right > (int)s->width) right = (int)s->width;
        if (left >= right) continue;
        uint16_t *dst = s->pixels + (size_t)(y + row) * s->stride_pixels + (size_t)left;
        uint16_t *const end = dst + (size_t)(right - left);
        do { *dst++ = color; } while (dst != end);
    }
}
/* Contiguous source atlas rectangle; transparency is explicit (RGB565 key). */
static inline void p4_ui_sprite(p4_game_surface_t *s, int x, int y,
                                int width, int height, const uint16_t *pixels,
                                int source_width, int source_height,
                                bool key_enabled, uint16_t key)
{
    if (!p4_surface_valid(s) || pixels == NULL || width <= 0 || height <= 0 ||
        source_width <= 0 || source_height <= 0 || width > 4096 || height > 4096 ||
        source_width > 4096 || source_height > 4096 ||
        x < -8192 || x > 8192 || y < -8192 || y > 8192) return;
    const int start_x = x < 0 ? -x : 0;
    const int start_y = y < 0 ? -y : 0;
    const int end_x = x + width > (int)s->width ? (int)s->width - x : width;
    const int end_y = y + height > (int)s->height ? (int)s->height - y : height;
    if (start_x >= end_x || start_y >= end_y) return;
    /* Exact nearest-neighbor quotient/remainder stepping; no division or
     * modulo in the destination-pixel loop, even for fractional scales. */
    const int step_x = source_width / width;
    const int remainder_x = source_width % width;
    const int first_x = start_x * source_width / width;
    const int first_error = start_x * source_width % width;
    for (int row = start_y; row < end_y; ++row) {
        const size_t sy = (size_t)(row * source_height / height);
        const uint16_t *src = pixels + sy * (size_t)source_width;
        uint16_t *dst = s->pixels + (size_t)(y + row) * s->stride_pixels + (size_t)(x + start_x);
        int sx = first_x, error = first_error;
        for (int col = start_x; col < end_x; ++col, ++dst) {
            const uint16_t value = src[sx];
            if (!key_enabled || value != key) *dst = value;
            sx += step_x;
            error += remainder_x;
            if (error >= width) { error -= width; ++sx; }
        }
    }
}
/* Offline-expanded material: size rows, 2*size columns. Each row already
 * contains its horizontal reflection; reflect only the row index at runtime.
 * Coordinates are actual surface pixels and texture phase is screen-anchored.
 * Source artwork must be separate from the destination surface (memcpy spans). */
static inline void p4_ui_material_mirrored_rows(p4_game_surface_t *s,
    int x, int y, int width, int height, const uint16_t *pixels, int size)
{
    if (!p4_surface_valid(s) || pixels == NULL || size < 1 || size > 128 ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        x < -8192 || x > 8192 || y < -8192 || y > 8192) return;
    int left = x, top = y, right = x + width, bottom = y + height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int)s->width) right = (int)s->width;
    if (bottom > (int)s->height) bottom = (int)s->height;
    if (left >= right || top >= bottom) return;
    const int period = size * 2;
    const int first_offset = left % period;
    int phase_y = top % period;
    for (int row = top; row < bottom; ++row) {
        const int sy = phase_y < size ? phase_y : period - phase_y - 1;
        const uint16_t *src = pixels + sy * period;
        uint16_t *dst = s->pixels + (size_t)row * s->stride_pixels + (size_t)left;
        int remaining = right - left, offset = first_offset;
        while (remaining > 0) {
            int count = period - offset;
            if (count > remaining) count = remaining;
            memcpy(dst, src + offset, (size_t)count * sizeof(*dst));
            dst += count;
            remaining -= count;
            offset = 0;
        }
        if (++phase_y == period) phase_y = 0;
    }
}
#endif
