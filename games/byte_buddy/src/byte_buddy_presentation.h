// SPDX-License-Identifier: MIT
#ifndef BYTE_BUDDY_PRESENTATION_H
#define BYTE_BUDDY_PRESENTATION_H

#include "p4/presentation.h"

/* The scene remains in the original touch coordinate space. Every primitive
 * goes directly to the negotiated framebuffer; no low-resolution frame cache. */
static inline bool bb_native(const p4_game_surface_t *s)
{
    return s->width == 768U && s->height == 480U;
}

static inline uint16_t bb_ink(const p4_game_surface_t *s, uint16_t color)
{
    (void)s;
    return color;
}

static inline void bb_clear(p4_game_surface_t *s, uint16_t color)
{
    p4_draw_clear(s, bb_ink(s, color));
}
static inline void bb_fill(p4_game_surface_t *s, int x, int y, int w, int h,
                           uint16_t color)
{
    p4_draw_fill_rect(s, p4_ui_x(s,x), p4_ui_y(s,y),
        p4_ui_x(s,x+w)-p4_ui_x(s,x), p4_ui_y(s,y+h)-p4_ui_y(s,y),
        bb_ink(s,color));
}
static inline void bb_rect(p4_game_surface_t *s, int x, int y, int w, int h,
                           uint16_t color)
{
    p4_draw_rect(s, p4_ui_x(s,x), p4_ui_y(s,y),
        p4_ui_x(s,x+w)-p4_ui_x(s,x), p4_ui_y(s,y+h)-p4_ui_y(s,y),
        bb_ink(s,color));
}
static inline void bb_pixel(p4_game_surface_t *s, int x, int y, uint16_t c)
{
    bb_fill(s,x,y,1,1,c);
}
static inline void bb_circle(p4_game_surface_t *s, int x, int y, int r, uint16_t c)
{
    p4_draw_fill_circle(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,r),bb_ink(s,c));
}
static inline void bb_text(p4_game_surface_t *s, int x, int y, const char *text,
                           uint16_t color, unsigned scale, size_t length)
{
    if (!bb_native(s)) {
        p4_draw_text(s,x,y,text,color,scale,length);
        return;
    }
    /* 22px AA cell fits the original 10px line spacing without sacrificing
     * dense genome/battle information. Primary buttons use larger text. */
    p4_ui_text(s,p4_ui_x(s,x),p4_ui_y(s,y)-3,text,bb_ink(s,color),
               22U*scale,length);
}

#endif
