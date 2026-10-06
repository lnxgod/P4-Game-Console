// SPDX-License-Identifier: MIT
#ifndef BYTE_BUDDY_PRESENTATION_H
#define BYTE_BUDDY_PRESENTATION_H

#include "p4/presentation.h"
#include "generated/byte_buddy_city.inc"

/* The scene remains in the original touch coordinate space. Every primitive
 * goes directly to the negotiated framebuffer; no low-resolution frame cache. */
static inline bool bb_native(const p4_game_surface_t *s)
{
    return s->width == 768U && s->height == 480U;
}

static inline uint16_t bb_ink(const p4_game_surface_t *s, uint16_t color)
{
    if (!bb_native(s)) return color;
    switch (color) {
    case 0x000b: return 0x08a4; /* ink navy */
    case 0x080f: return 0x10e7;
    case 0x181f: return 0x192a;
    case 0x281f: return 0x216c;
    case 0x1025: return 0x1949;
    case 0x4208: return 0x218a;
    case 0x39e7: return 0x32ce;
    case 0x7bef: return 0x9cf5;
    case 0x9cf3: return 0xb5f8;
    case 0xbdf7: return 0xcebb;
    case 0x07ff: return 0x5e7e;
    case 0xf81f: return 0xc49f;
    case 0xffe0: return 0xfe8b;
    case 0xfd20: return 0xfdad;
    case 0x07e0: return 0x66d6;
    case 0xf800: return 0xfbaf;
    default: return color;
    }
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

static inline void bb_city(p4_game_surface_t *s, bool dim)
{
    /* Exactly 206,848 static bytes, no decode buffer, one palette read per
     * native pixel, row addresses outside the hot loop. */
    const uint16_t *palette=dim ? bb_city_dim_palette : bb_city_palette;
    for (unsigned y=0; y<268U; ++y) {
        const uint8_t *src=bb_city_indices + y*768U;
        uint16_t *dst=s->pixels + (size_t)(y+60U)*s->stride_pixels;
        for (unsigned x=0; x<768U; ++x) dst[x]=palette[src[x]];
    }
}
#endif
