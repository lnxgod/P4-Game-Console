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
