// SPDX-License-Identifier: MIT

#include "p4/draw.h"

#include <stdint.h>
#include <string.h>

bool p4_surface_valid(const p4_game_surface_t *surface)
{
    return surface != NULL && surface->pixels != NULL &&
        surface->width == P4_GAME_SURFACE_WIDTH &&
        surface->height == P4_GAME_SURFACE_HEIGHT &&
        surface->stride_pixels >= P4_GAME_SURFACE_WIDTH;
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
    for (int64_t row = top; row < bottom; ++row) {
        for (int64_t column = left; column < right; ++column) {
            surface->pixels[(size_t)row * surface->stride_pixels +
                            (size_t)column] = color;
        }
    }
}

void p4_draw_clear(p4_game_surface_t *surface, uint16_t color)
{
    p4_draw_fill_rect(surface, 0, 0,
                      P4_GAME_SURFACE_WIDTH, P4_GAME_SURFACE_HEIGHT,
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
    const int64_t radius_squared = (int64_t)radius * radius;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if ((int64_t)x * x + (int64_t)y * y <= radius_squared) {
                p4_draw_pixel(surface, center_x + x, center_y + y, color);
            }
        }
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
    for (size_t row = 0U; row < height; ++row) {
        for (size_t column = 0U; column < width; ++column) {
            const uint16_t color = pixels[row * stride_pixels + column];
            if (!use_transparency || color != transparent_color) {
                p4_draw_pixel(surface,
                              x + (int)column,
                              y + (int)row,
                              color);
            }
        }
    }
}
