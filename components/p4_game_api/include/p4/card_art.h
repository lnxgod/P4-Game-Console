// SPDX-License-Identifier: MIT
#ifndef P4_CARD_ART_H
#define P4_CARD_ART_H

/* Header-only presentation. All card geometry is rasterized at the negotiated
 * resolution; input remains canonical 320x200. No new runtime ABI imports. */
#include "p4/presentation.h"
#include <string.h>

static inline void p4_card_fill(p4_game_surface_t *s,int x,int y,int w,int h,uint16_t c)
{ p4_draw_fill_rect(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,x+w)-p4_ui_x(s,x),p4_ui_y(s,y+h)-p4_ui_y(s,y),c); }
static inline void p4_card_outline(p4_game_surface_t *s,int x,int y,int w,int h,uint16_t c)
{ p4_draw_rect(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,x+w)-p4_ui_x(s,x),p4_ui_y(s,y+h)-p4_ui_y(s,y),c); }
static inline void p4_card_circle(p4_game_surface_t *s,int x,int y,int r,uint16_t c)
{ p4_draw_fill_circle(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,r),c); }
static inline void p4_card_text(p4_game_surface_t *s,int x,int y,const char *t,uint16_t c,unsigned scale,size_t max)
{
    if(s->width==P4_GAME_SURFACE_WIDTH){p4_draw_text(s,x,y,t,c,scale,max);return;}
    p4_ui_text(s,p4_ui_x(s,x),p4_ui_y(s,y),t,c,(unsigned)p4_ui_y(s,(int)(9U*scale)),max);
}

/* Fine texture samples are written to native pixels, never to a low-res frame. */
static inline void p4_card_felt(p4_game_surface_t *s,int x,int y,int w,int h,const uint16_t *texture)
{
    if(!p4_surface_valid(s)||texture==NULL||w<=0||h<=0||w>4096||h>4096||x < -8192||x > 8192||y < -8192||y > 8192)return;
    int left=p4_ui_x(s,x),top=p4_ui_y(s,y);
    int right=p4_ui_x(s,x+w),bottom=p4_ui_y(s,y+h);
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>(int)s->width)right=(int)s->width;
    if(bottom>(int)s->height)bottom=(int)s->height;
    /* Copy clipped texture-row spans. This preserves native pixel sampling
     * while avoiding per-pixel address/mask work over the whole table. */
    for(int py=top;py<bottom;++py){
        uint16_t *dst=s->pixels+(size_t)py*s->stride_pixels;
        const uint16_t *src=texture+(size_t)(py&127)*128U;
        for(int px=left;px<right;){
            const int offset=px&127;
            int count=128-offset;
            if(count>right-px)count=right-px;
            memcpy(dst+px,src+offset,(size_t)count*sizeof(*dst));
            px+=count;
        }
    }
}

/* Suit order: club, diamond, heart, spade. Mathematical silhouettes are
 * evaluated at native pixel size so their edges gain detail at 768x480. */
static inline void p4_card_suit(p4_game_surface_t *s,int x,int y,int size,unsigned suit,uint16_t color)
{
    if(!p4_surface_valid(s)||size<3||size>512||x < -8192||x > 8192||y < -8192||y > 8192)return;
    const int start_x=x<0?-x:0, start_y=y<0?-y:0;
    const int end_x=x+size>(int)s->width?(int)s->width-x:size;
    const int end_y=y+size>(int)s->height?(int)s->height-y:size;
    if(start_x>=end_x||start_y>=end_y)return;
    if(suit==1U){
        /* Pointed rhombus, symmetric for odd and even sizes. Its sides use
         * 4/5 horizontal pixels per vertical pixel; Q8 coverage softens just
         * two edge pixels per row. The interior is a clipped opaque span. */
        const int center=(size-1)*128;
        for(int py=start_y;py<end_y;++py){
            const int edge=py<size-1-py?py:size-1-py;
            const int extent=edge*1024/5;
            const int full_left=(center-extent+255)>>8;
            const int full_right=(center+extent)>>8;
            int left=full_left<start_x?start_x:full_left;
            int right=full_right>=end_x?end_x-1:full_right;
            uint16_t *row=s->pixels+(size_t)(y+py)*s->stride_pixels;
            if(left<=right){
                uint16_t *dst=row+(size_t)(x+left);
                uint16_t *const end=dst+(size_t)(right-left+1);
                do{*dst++=color;}while(dst!=end);
            }
            const int tips[2]={full_left-1,full_right+1};
            for(unsigned side=0;side<2U;++side){
                const int px=tips[side];
                if(px<start_x||px>=end_x)continue;
                const int distance=px*256<center?center-px*256:px*256-center;
                const int coverage=extent+256-distance;
                if(coverage<=0)continue;
                const unsigned alpha=(unsigned)(coverage*15+128)>>8;
                uint16_t *dst=row+(size_t)(x+px);
                *dst=p4_ui_blend(*dst,color,alpha);
            }
        }
        return;
    }
    int8_t source_x[512];
    for(int px=start_x;px<end_x;++px)source_x[px]=(int8_t)(px*100/size-50);
    for(int py=start_y;py<end_y;++py){
      const int v=py*100/size;
      const int taper=suit==2U?(91-v)*49/63:(v-5)*45/48;
      uint16_t *dst=s->pixels+(size_t)(y+py)*s->stride_pixels+(size_t)(x+start_x);
      for(int px=start_x;px<end_x;++px,++dst){
        const int u=source_x[px];
        bool inside=false;
        if(suit==2U){
            const int a=u+22,b=u-22,d=v-28;
            inside=(a*a+d*d<27*27)||(b*b+d*d<27*27)||(v>=28 && v<91 && (u<0?-u:u)<taper);
        }else if(suit==0U){
            const int d=v-27,e=v-54,a=u+24,b=u-24;
            inside=u*u+d*d<23*23 || a*a+e*e<24*24 || b*b+e*e<24*24 || (v>46 && v<87 && (u<0?-u:u)<8) || (v>=82 && v<91 && (u<0?-u:u)<20);
        }else{
            const int a=u+21,b=u-21,d=v-53;
            inside=(v<53 && v>5 && (u<0?-u:u)<taper) || a*a+d*d<24*24 || b*b+d*d<24*24 || (v>51 && v<88 && (u<0?-u:u)<8) || (v>=83 && v<92 && (u<0?-u:u)<20);
        }
        if(inside)*dst=color;
      }
    }
}

static inline void p4_card_shell(p4_game_surface_t *s,int x,int y,int w,int h,bool selected)
{
    int radius=w/12; if(radius<2)radius=2;
    p4_ui_round_rect(s,x+1,y+3,w,h,radius,0x0124);
    if(selected)p4_ui_round_rect(s,x-2,y-2,w+4,h+4,radius+1,0xfe60);
    p4_ui_round_rect(s,x,y,w,h,radius,0xa514);
    p4_ui_round_rect(s,x+1,y+1,w-2,h-2,radius,0xffde);
    p4_draw_fill_rect(s,x+radius,y+1,w-radius*2,1,0xffff);
}
static inline void p4_card_back(p4_game_surface_t *s,int cx,int cy,int cw,int ch,const uint16_t *texture,bool selected)
{
    const int x=p4_ui_x(s,cx),y=p4_ui_y(s,cy),w=p4_ui_x(s,cw),h=p4_ui_y(s,ch);
    p4_card_shell(s,x,y,w,h,selected);
    int inset=w/12; if(inset<2)inset=2;
    p4_ui_sprite(s,x+inset,y+inset,w-inset*2,h-inset*2,texture,96U,96U,false,0U);
    p4_draw_rect(s,x+inset,y+inset,w-inset*2,h-inset*2,0xd5d2);
}
static inline void p4_card_face(p4_game_surface_t *s,int cx,int cy,int cw,int ch,unsigned rank,unsigned suit,bool selected)
{
    const int x=p4_ui_x(s,cx),y=p4_ui_y(s,cy),w=p4_ui_x(s,cw),h=p4_ui_y(s,ch);
    p4_card_shell(s,x,y,w,h,selected);
    if(rank<1U||rank>13U||suit>3U)return;
    const uint16_t ink=(suit==1U||suit==2U)?0xb904:0x18e5;
    static const char ranks[13][3]={"A","2","3","4","5","6","7","8","9","10","J","Q","K"};
    const char *label=ranks[rank-1U];
    int font=h>w?h/5:w/4; if(font<11)font=11; if(font>22)font=22;
    int inset=w/10; if(inset<2)inset=2;
    p4_ui_text(s,x+inset,y+inset,label,ink,(unsigned)font,2U);
    const int labelw=p4_ui_text_width(label,(unsigned)font,2U);
    const int small=font*3/4;
    p4_card_suit(s,x+w-inset-small,y+inset,small,suit,ink);
    p4_ui_text(s,x+w-inset-labelw,y+h-inset-font,label,ink,(unsigned)font,2U);
    p4_card_suit(s,x+inset,y+h-inset-small,small,suit,ink);
    const int center_size=h< w ? h*2/5:w*2/5;
    if(rank>=11U){
        const int bx=x+w/2-center_size/2,by=y+h/2-center_size/2;
        p4_draw_rect(s,bx-3,by-3,center_size+6,center_size+6,0xcdd3);
        p4_card_suit(s,bx,by,center_size,suit,ink);
    }else{
        p4_card_suit(s,x+w/2-center_size/2,y+h/2-center_size/2,center_size,suit,ink);

    }
}
#endif
