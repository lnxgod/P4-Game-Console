// SPDX-License-Identifier: MIT
// Canonical game geometry maps directly to the negotiated framebuffer.
// Assets and text are sampled at native resolution, with no low-res frame copy.
#ifndef GAME_HIRES_ART_H
#define GAME_HIRES_ART_H
#include "p4/presentation.h"
#include "p4/feedback.h"
#include "generated/hires_art.inc"
static inline void hi_fill_rect(p4_game_surface_t *s,int x,int y,int w,int h,uint16_t c)
{ p4_draw_fill_rect(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,x+w)-p4_ui_x(s,x),p4_ui_y(s,y+h)-p4_ui_y(s,y),c); }
static inline void hi_rect(p4_game_surface_t *s,int x,int y,int w,int h,uint16_t c)
{ p4_draw_rect(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,x+w)-p4_ui_x(s,x),p4_ui_y(s,y+h)-p4_ui_y(s,y),c); }
static inline void hi_circle(p4_game_surface_t *s,int x,int y,int r,uint16_t c)
{ p4_draw_fill_circle(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,r),c); }
static inline void hi_pixel(p4_game_surface_t *s,int x,int y,uint16_t c)
{ p4_draw_pixel(s,p4_ui_x(s,x),p4_ui_y(s,y),c); }
static inline void hi_text(p4_game_surface_t *s,int x,int y,const char *t,uint16_t c,unsigned scale,size_t n)
{ if(s->width==P4_GAME_SURFACE_HIGH_RES_WIDTH) p4_ui_text(s,p4_ui_x(s,x),p4_ui_y(s,y-2),t,c,22U*scale,n); else p4_draw_text(s,x,y,t,c,scale,n); }
static inline void hi_frame(p4_game_surface_t *s,int x,int y,int w,int h,unsigned frame)
{ p4_ui_sprite(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,x+w)-p4_ui_x(s,x),p4_ui_y(s,y+h)-p4_ui_y(s,y),hi_art[frame&15U],HI_ART_W,HI_ART_H,true,0U); }
static inline void hi_frame_flip(p4_game_surface_t *s,int x,int y,int w,int h,unsigned frame,bool flip)
{
    if(!flip){hi_frame(s,x,y,w,h,frame);return;}
    const int left=p4_ui_x(s,x),top=p4_ui_y(s,y);
    const int width=p4_ui_x(s,x+w)-left,height=p4_ui_y(s,y+h)-top;
    if(width<=0||height<=0)return;
    for(int py=0;py<height;++py) {
        if(top+py<0||top+py>=(int)s->height)continue;
        const int sy=py*HI_ART_H/height;
        for(int px=0;px<width;++px) {
            if(left+px<0||left+px>=(int)s->width)continue;
            const int sx=HI_ART_W-1-px*HI_ART_W/width;
            const uint16_t c=hi_art[frame&15U][sy*HI_ART_W+sx];
            if(c!=0U)s->pixels[(size_t)(top+py)*s->stride_pixels+(size_t)(left+px)]=c;
        }
    }
}
static inline void hi_feedback(p4_game_surface_t *s,const p4_game_audio_effect_player_t *p,int x,int y)
{ p4_game_feedback_draw_audio_effect(s,p,p4_ui_x(s,x),p4_ui_y(s,y)); }
#endif
