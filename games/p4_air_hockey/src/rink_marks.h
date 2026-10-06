// SPDX-License-Identifier: MIT
#ifndef P4_AIR_HOCKEY_RINK_MARKS_H
#define P4_AIR_HOCKEY_RINK_MARKS_H
#include "p4/presentation.h"
/* Exact annulus scan conversion for this rink: the old definition was
 * inner^2 <= dx^2+dy^2 <= radius^2. Edge cursors only move toward the center,
 * so finding the spans costs O(radius), rather than testing the whole square.
 * Shared clipped row fills keep clipping and framebuffer ownership unchanged. */
static inline void hockey_rink_ring(p4_game_surface_t *surface,
                                    int cx, int cy, int radius, uint16_t color)
{
    if (!p4_surface_valid(surface) || radius < 1 || radius > 128) return;
    const int x = p4_ui_x(surface, cx), y = p4_ui_y(surface, cy);
    const int r = p4_ui_x(surface, radius);
    if (r < 1 || r > 1024) return;
    const int inner = r - (surface->width == 768U ? 2 : 1);
    const int outer_squared = r * r, inner_squared = inner * inner;
    int outer = r, hole = inner - 1;
    for (int dy = 0; dy <= r; ++dy) {
        const int dy_squared = dy * dy;
        while (outer > 0 && outer * outer + dy_squared > outer_squared) --outer;
        while (hole >= 0 && hole * hole + dy_squared >= inner_squared) --hole;
        for (int side = 0; side < (dy == 0 ? 1 : 2); ++side) {
            const int row = y + (side == 0 ? dy : -dy);
            if (hole < 0) {
                p4_draw_fill_rect(surface, x - outer, row, outer * 2 + 1, 1, color);
            } else {
                p4_draw_fill_rect(surface, x - outer, row, outer - hole, 1, color);
                p4_draw_fill_rect(surface, x + hole + 1, row, outer - hole, 1, color);
            }
        }
    }
}
#endif
