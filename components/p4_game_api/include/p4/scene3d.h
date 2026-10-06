// SPDX-License-Identifier: MIT
#ifndef P4_SCENE3D_H
#define P4_SCENE3D_H
#include "p4/draw.h"
#include <string.h>
/* Native RGB565, perspective, reciprocal-depth, Gouraud triangle rasterizer.
 * Re-submit bounded geometry for each horizontal band. Only the band's depth
 * needs storage (768*16*2 = 24 KiB), never a second color framebuffer.
 * Vertices are caller-projected, clipped to the camera near plane before use.
 * x/y have three fractional bits; q=1/depth is linearly interpolated in screen
 * space. Opaque geometry first, then a single-valued translucent surface. */
typedef struct { int16_t x,y;uint16_t q,color,u,v; } p4_3d_vertex_t;
typedef struct { p4_game_surface_t *surface;uint16_t *depth;int top,bottom;uint8_t *tags,material;const uint16_t *refraction_texture; } p4_3d_band_t;
static inline int p4_3d_min(int a,int b){return a<b?a:b;}
static inline int p4_3d_max(int a,int b){return a>b?a:b;}
static inline void p4_3d_begin(p4_3d_band_t *b,p4_game_surface_t *f,uint16_t *z,int y,int rows){
    b->surface=f;b->depth=z;b->tags=NULL;b->material=0;b->refraction_texture=NULL;b->top=y;b->bottom=p4_3d_min(y+rows,f->height);
    memset(z,0,(size_t)(b->bottom-y)*f->width*sizeof(*z));
}
static inline uint16_t p4_3d_half(uint16_t a,uint16_t b){return (uint16_t)(((a&0xf7deU)>>1)+((b&0xf7deU)>>1));}
static inline void p4_3d_triangle(p4_3d_band_t *band,const p4_3d_vertex_t *va,const p4_3d_vertex_t *vb,const p4_3d_vertex_t *vc,bool translucent){
    const p4_3d_vertex_t *a=va,*b=vb,*c=vc,*swap;
    if(a->y>b->y){swap=a;a=b;b=swap;}if(b->y>c->y){swap=b;b=c;c=swap;}if(a->y>b->y){swap=a;a=b;b=swap;}
    int first=p4_3d_max(band->top,(a->y+3)/8),last=p4_3d_min(band->bottom,(c->y+3)/8);
    if(first>=last||a->y==c->y||!a->q||!b->q||!c->q)return;
    float ax=(float)a->x*0.125f,ay=(float)a->y*0.125f;
    float bx=(float)b->x*0.125f,by=(float)b->y*0.125f;
    float cx=(float)c->x*0.125f,cy=(float)c->y*0.125f;
    float det=(bx-ax)*(cy-ay)-(cx-ax)*(by-ay);
    if(det>-0.0625f&&det<0.0625f)return;
    float inv=1.0f/det;
    const float av[6]={(float)a->q,(float)(a->color>>11),(float)((a->color>>5)&63),(float)(a->color&31),(float)a->u,(float)a->v};
    const float bv[6]={(float)b->q,(float)(b->color>>11),(float)((b->color>>5)&63),(float)(b->color&31),(float)b->u,(float)b->v};
    const float cv[6]={(float)c->q,(float)(c->color>>11),(float)((c->color>>5)&63),(float)(c->color&31),(float)c->u,(float)c->v};
    float dx[6],dy[6];int32_t step[6];
    for(int k=0;k<6;++k){
        dx[k]=((bv[k]-av[k])*(cy-ay)-(cv[k]-av[k])*(by-ay))*inv;
        dy[k]=((bx-ax)*(cv[k]-av[k])-(cx-ax)*(bv[k]-av[k]))*inv;
        /* Extreme degenerate/offscreen inputs must not overflow fixed spans. */
        if(dx[k]>2000000.0f||dx[k]<-2000000.0f)return;
        step[k]=(int32_t)(dx[k]*256.0f);
    }
    const bool flat=!translucent&&a->color==b->color&&a->color==c->color;
    const uint16_t flat_color=a->color;const uint8_t material=band->material;
    const float long_slope=(cx-ax)/(cy-ay);
    for(int y=first;y<last;++y){
        float sample=(float)y+0.5f,left=ax+(sample-ay)*long_slope,right;
        if(sample<by&&by>ay)right=ax+(sample-ay)*(bx-ax)/(by-ay);
        else if(cy>by)right=bx+(sample-by)*(cx-bx)/(cy-by);
        else continue;
        if(left>right){float t=left;left=right;right=t;}
        int x0=p4_3d_max(0,(int)(left+0.5f)),x1=p4_3d_min(band->surface->width,(int)(right+0.5f));
        if(x0>=x1)continue;
        int32_t value[6];
        for(int k=0;k<6;++k)value[k]=(int32_t)((av[k]+dx[k]*((float)x0+0.5f-ax)+dy[k]*(sample-ay))*256.0f);
        uint16_t *dst=band->surface->pixels+(size_t)y*band->surface->stride_pixels+(size_t)x0;
        uint16_t *z=band->depth+(size_t)(y-band->top)*band->surface->width+(size_t)x0;
        uint8_t *tag=band->tags?band->tags+(size_t)(y-band->top)*band->surface->width+(size_t)x0:NULL;
        /* Scalars keep span state in registers on RV32 -Os. An array loop
         * otherwise loads/stores all six interpolants for every pixel. */
        int32_t depth_value=value[0],red=value[1],green=value[2],blue=value[3],tex_u=value[4],tex_v=value[5];
        const int32_t dq=step[0],dr=step[1],dg=step[2],db=step[3],du=step[4],dv=step[5];
        if(flat){
            /* Walls and bed cover most opaque pixels. Their constant material
             * needs only depth interpolation, never six attribute updates. */
            for(int x=x0;x<x1;++x){int q=depth_value>>8;
                if(q>0&&q<=65535&&q>*z){*dst=flat_color;*z=(uint16_t)q;if(tag)*tag=material;}
                ++dst;++z;if(tag)++tag;depth_value+=dq;
            }
            continue;
        }
        for(int x=x0;x<x1;++x){
            int q=depth_value>>8;
            if(q>0&&q<=65535&&q>*z){
                unsigned r=(unsigned)p4_3d_max(0,p4_3d_min(31,red>>8));
                unsigned g=(unsigned)p4_3d_max(0,p4_3d_min(63,green>>8));
                unsigned bl=(unsigned)p4_3d_max(0,p4_3d_min(31,blue>>8));
                uint16_t color=(uint16_t)((r<<11)|(g<<5)|bl);
                uint16_t underneath=*dst;
                if(translucent&&tag&&*tag==1&&band->refraction_texture){
                    unsigned u=(unsigned)(tex_u>>16)&31U,v=(unsigned)(tex_v>>16)&31U;
                    underneath=band->refraction_texture[v*32U+u];
                }
                *dst=translucent?p4_3d_half(underneath,color):color;*z=(uint16_t)q;
                if(tag&&!translucent)*tag=band->material;
            }
            ++dst;++z;if(tag)++tag;depth_value+=dq;red+=dr;green+=dg;blue+=db;tex_u+=du;tex_v+=dv;
        }
    }
}
static inline void p4_3d_quad(p4_3d_band_t *b,const p4_3d_vertex_t v[4],bool translucent){
    p4_3d_triangle(b,&v[0],&v[1],&v[2],translucent);p4_3d_triangle(b,&v[0],&v[2],&v[3],translucent);
}
#endif
