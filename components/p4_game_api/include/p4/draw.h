// SPDX-License-Identifier: MIT

#ifndef P4_GAME_API_DRAW_H
#define P4_GAME_API_DRAW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_DRAW_CP437_CELL_WIDTH = 8,
    P4_DRAW_CP437_COMPACT_HEIGHT = 8,
    P4_DRAW_CP437_FULL_HEIGHT = 16,
    P4_DRAW_CP437_MAX_TEXT_BYTES = 256,
};

/** Accept either the standard 320x200 or negotiated 768x480 RGB565 surface. */
bool p4_surface_valid(const p4_game_surface_t *surface);
void p4_draw_clear(p4_game_surface_t *surface, uint16_t color);
void p4_draw_pixel(p4_game_surface_t *surface,
                   int x, int y, uint16_t color);
void p4_draw_fill_rect(p4_game_surface_t *surface,
                       int x, int y, int width, int height,
                       uint16_t color);
void p4_draw_rect(p4_game_surface_t *surface,
                  int x, int y, int width, int height,
                  uint16_t color);
void p4_draw_fill_circle(p4_game_surface_t *surface,
                         int center_x, int center_y, int radius,
                         uint16_t color);
void p4_draw_text(p4_game_surface_t *surface,
                  int x, int y, const char *text,
                  uint16_t color, unsigned scale,
                  size_t max_characters);
/**
 * Draw one IBM PC Code Page 437 cell with explicit foreground/background.
 * cell_height must be 8 (vertically compacted) or 16 (native font rows).
 */
void p4_draw_cp437_glyph(p4_game_surface_t *surface,
                         int x, int y, uint8_t character,
                         uint16_t foreground, uint16_t background,
                         unsigned cell_height);
/** Draw at most 256 raw CP437 bytes as adjacent 8-pixel cells. */
void p4_draw_cp437_text(p4_game_surface_t *surface,
                        int x, int y,
                        const uint8_t *bytes, size_t byte_count,
                        uint16_t foreground, uint16_t background,
                        unsigned cell_height);
void p4_draw_sprite_rgb565(p4_game_surface_t *surface,
                           int x, int y,
                           const uint16_t *pixels,
                           size_t width, size_t height,
                           size_t stride_pixels,
                           bool use_transparency,
                           uint16_t transparent_color);

#ifdef __cplusplus
}
#endif

#endif
