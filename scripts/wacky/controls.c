/* SPDX-License-Identifier: MIT */
#include "controls.h"
#include "p4/draw.h"

/* These bounds match the OS's canonical p4/input.h touch regions exactly.
 * Darken the original picture instead of hiding it behind solid controls. */
static void shade(p4_game_surface_t *s,int x,int y,int w,int h)
{
    for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col) {
        uint16_t *p=&s->pixels[(size_t)row*s->stride_pixels+(size_t)col];
        *p=(uint16_t)((*p&0xf7deU)>>1);
    }
}
static void box(p4_game_surface_t *s,int x,int y,int w,int h,
                const char *label,uint32_t held,uint32_t button)
{
    shade(s,x,y,w,h);
    uint16_t color=(held&button)?0xffe0U:0xffffU;
    p4_draw_rect(s,x,y,w,h,color);
    int n=0;while(label[n])++n;
    p4_draw_text(s,x+(w-n*6)/2,y+(h-7)/2,label,color,1,8);
}
static void pedal(p4_game_surface_t *s,int x,int y,int radius,
                  const char *label,uint32_t held,uint32_t button)
{
    uint16_t color=(held&button)?0xffe0U:0xffffU;
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
        int d=dx*dx+dy*dy;if(d>radius*radius)continue;
        uint16_t *p=&s->pixels[(size_t)(y+dy)*s->stride_pixels+(size_t)(x+dx)];
        *p=d>=(radius-1)*(radius-1)?color:(uint16_t)((*p&0xf7deU)>>1);
    }
    int n=0;while(label[n])++n;
    p4_draw_text(s,x-n*3,y-3,label,color,1,8);
}
void ww_controls_draw(p4_game_surface_t *s,uint32_t held,bool linked)
{
    if(!p4_surface_valid(s)||s->width!=320||s->height!=200)return;
    box(s,0,0,52,24,"EXIT",held,P4_BUTTON_BACK);
    if(!linked)box(s,268,0,52,24,"PAUSE",held,P4_BUTTON_START);
    box(s,34,132,24,26,"GAS",held,P4_BUTTON_UP);
    box(s,8,158,26,25,"<",held,P4_BUTTON_LEFT);
    box(s,58,158,26,25,">",held,P4_BUTTON_RIGHT);
    box(s,34,174,24,26,"BRK",held,P4_BUTTON_DOWN);
    pedal(s,286,158,22,"GAS",held,P4_BUTTON_A);
    if(!linked)pedal(s,240,176,18,"FIRE",held,P4_BUTTON_B);
}
