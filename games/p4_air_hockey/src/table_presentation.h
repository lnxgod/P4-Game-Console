// SPDX-License-Identifier: MIT
#ifndef GAME_TABLE_PRESENTATION_H
#define GAME_TABLE_PRESENTATION_H
#include "p4/presentation.h"
#include <string.h>
/* Game geometry/input remains canonical; each primitive is rasterized directly
 * into the negotiated surface, using native-resolution sprites and typography. */
static inline void table_fill(p4_game_surface_t *s,int x,int y,int w,int h,uint16_t c)
{
    p4_draw_fill_rect(s,p4_ui_x(s,x),p4_ui_y(s,y),
        p4_ui_x(s,x+w)-p4_ui_x(s,x),p4_ui_y(s,y+h)-p4_ui_y(s,y),c);
}
static inline void table_rect(p4_game_surface_t *s,int x,int y,int w,int h,uint16_t c)
{
    const int px=p4_ui_x(s,x),py=p4_ui_y(s,y);
    const int pw=p4_ui_x(s,x+w)-px,ph=p4_ui_y(s,y+h)-py;
    p4_draw_rect(s,px,py,pw,ph,c);
    if(s->width==768U && pw>3 && ph>3) p4_draw_rect(s,px+1,py+1,pw-2,ph-2,c);
}
static inline void table_circle(p4_game_surface_t *s,int x,int y,int r,uint16_t c)
{
    p4_draw_fill_circle(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,r),c);
}
static inline void table_text(p4_game_surface_t *s,int x,int y,const char *t,
                              uint16_t c,unsigned scale,size_t max_chars)
{
    if(s->width==320U) { p4_draw_text(s,x,y,t,c,scale,max_chars); return; }
    const unsigned height=scale>1U?38U:24U;
    p4_ui_text(s,p4_ui_x(s,x),p4_ui_y(s,y)-3,t,c,height,max_chars);
}
static inline void table_sprite(p4_game_surface_t *s,int x,int y,int w,int h,
                                 const uint16_t *pixels,int size,bool transparent)
{
    p4_ui_sprite(s,p4_ui_x(s,x),p4_ui_y(s,y),
        p4_ui_x(s,x+w)-p4_ui_x(s,x),p4_ui_y(s,y+h)-p4_ui_y(s,y),
        pixels,size,size,transparent,0xf81fU);
}
static inline void table_material(p4_game_surface_t *s,int x,int y,int w,int h,
                                    const uint16_t *pixels,int size)
{
    const int x0=p4_ui_x(s,x),y0=p4_ui_y(s,y);
    p4_ui_material_mirrored_rows(s,x0,y0,p4_ui_x(s,x+w)-x0,
        p4_ui_y(s,y+h)-y0,pixels,size);
}
#endif
