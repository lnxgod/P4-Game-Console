// SPDX-License-Identifier: MIT
#ifndef P4_MESH_H
#define P4_MESH_H
#include "p4/draw.h"
/* Bounded screen-space convex faces. No z-buffer, heap, or per-pixel division.
 * The caller supplies painter order. Coordinates outside +/-4096 are rejected. */
typedef struct { int x, y; } p4_mesh_point_t;
static inline int p4_mesh_pixel_edge(int32_t x){
 x+=32767;return x>=0?x/65536:-((-x+65535)/65536);
}
static inline void p4_draw_face(p4_game_surface_t *s,
    const p4_mesh_point_t *v, unsigned count, uint16_t color)
{
    if (!p4_surface_valid(s) || !v || count < 3U || count > 4U) return;
    int top = s->height, bottom = 0;
    for (unsigned i=0; i<count; ++i) {
        if(v[i].x < -4096 || v[i].x > 4096 || v[i].y < -4096 || v[i].y > 4096) return;
        if(v[i].y < top) top=v[i].y;
        if(v[i].y > bottom) bottom=v[i].y;
    }
    if(top < 0) top=0;
    if(bottom > s->height) bottom=s->height;
    /* Intersections are initialized once per edge, then advanced in Q16.
     * Wide multiply is outside the row/pixel loops, for signed clipping. */
    int first[4], last[4]; int32_t edge_x[4], step[4]; unsigned edges=0;
    for(unsigned i=0; i<count; ++i) {
        p4_mesh_point_t a=v[i], b=v[(i+1U)%count];
        if(a.y==b.y) continue;
        if(a.y>b.y){p4_mesh_point_t t=a;a=b;b=t;}
        int lo=a.y>top?a.y:top, hi=b.y<bottom?b.y:bottom;
        if(lo>=hi) continue;
        const int32_t dx=(b.x-a.x)*65536/(b.y-a.y);
        first[edges]=lo;last[edges]=hi;step[edges]=dx;
        edge_x[edges]=(int32_t)((int64_t)a.x*65536+(int64_t)dx*(lo-a.y)+dx/2);
        ++edges;
    }
    for(int y=top; y<bottom; ++y) {
        int32_t left=INT32_MAX,right=INT32_MIN;
        for(unsigned i=0;i<edges;++i) if(y>=first[i]&&y<last[i]){
            const int32_t x=edge_x[i];
            if(x<left)left=x;if(x>right)right=x;edge_x[i]+=step[i];
        }
        if(left>right)continue;
        int x0=p4_mesh_pixel_edge(left),x1=p4_mesh_pixel_edge(right);
        if(x0<0)x0=0;if(x1>s->width)x1=s->width;
        if(x0>=x1)continue;
        uint16_t *p=s->pixels+(size_t)y*s->stride_pixels+(size_t)x0;
        uint16_t *end=p+(x1-x0);
        do{*p++=color;}while(p!=end);
    }
}
static inline void p4_draw_mesh_line(p4_game_surface_t *s,p4_mesh_point_t a,p4_mesh_point_t b,uint16_t color){
 if(!p4_surface_valid(s)||a.x < -4096||a.x>4096||a.y < -4096||a.y>4096||b.x < -4096||b.x>4096||b.y < -4096||b.y>4096)return;
 int dx=b.x>a.x?b.x-a.x:a.x-b.x,dy=b.y>a.y?a.y-b.y:b.y-a.y;
 int sx=a.x<b.x?1:-1,sy=a.y<b.y?1:-1,error=dx+dy;
 for(;;){
  if(a.x>=0&&a.x<s->width&&a.y>=0&&a.y<s->height)s->pixels[(size_t)a.y*s->stride_pixels+(size_t)a.x]=color;
  if(a.x==b.x&&a.y==b.y)break;
  int twice=2*error;if(twice>=dy){error+=dy;a.x+=sx;}if(twice<=dx){error+=dx;a.y+=sy;}
 }
}
#endif

#ifndef P4_MESH_TEXTURE_H
#define P4_MESH_TEXTURE_H
/* UV coordinates are Q8 and wrap a power-of-two, tightly packed texture.
 * One division per edge/scanline; the pixel loop only steps and samples. */
typedef struct { p4_mesh_point_t point; int u,v; } p4_mesh_tex_vertex_t;
static inline void p4_draw_textured_face(p4_game_surface_t *s,
 const p4_mesh_tex_vertex_t *v,unsigned count,const uint16_t *texture,unsigned size){
 if(!p4_surface_valid(s)||!v||!texture||count<3U||count>4U||!size||size>128U||(size&(size-1U)))return;
 int top=s->height,bottom=0;
 for(unsigned i=0;i<count;++i){
  if(v[i].point.x < -4096||v[i].point.x>4096||v[i].point.y < -4096||v[i].point.y>4096||v[i].u < -1048576||v[i].u>1048576||v[i].v < -1048576||v[i].v>1048576)return;
  if(v[i].point.y<top)top=v[i].point.y;if(v[i].point.y>bottom)bottom=v[i].point.y;
 }
 if(top<0)top=0;if(bottom>s->height)bottom=s->height;
 int first[4],last[4],us[4],vs[4],du[4],dv[4];int32_t xs[4],dx[4];unsigned edges=0;
 for(unsigned i=0;i<count;++i){
  p4_mesh_tex_vertex_t a=v[i],b=v[(i+1U)%count];
  if(a.point.y==b.point.y)continue;
  if(a.point.y>b.point.y){p4_mesh_tex_vertex_t t=a;a=b;b=t;}
  int lo=a.point.y>top?a.point.y:top,hi=b.point.y<bottom?b.point.y:bottom;
  if(lo>=hi)continue;
  int h=b.point.y-a.point.y,offset=lo-a.point.y;
  dx[edges]=(b.point.x-a.point.x)*65536/h;
  du[edges]=(b.u-a.u)/h;dv[edges]=(b.v-a.v)/h;
  xs[edges]=(int32_t)((int64_t)a.point.x*65536+(int64_t)dx[edges]*offset+dx[edges]/2);
  us[edges]=a.u+du[edges]*offset+du[edges]/2;
  vs[edges]=a.v+dv[edges]*offset+dv[edges]/2;
  first[edges]=lo;last[edges]=hi;++edges;
 }
 const unsigned mask=size-1U;
 for(int y=top;y<bottom;++y){
  int l=-1,r=-1;
  for(unsigned i=0;i<edges;++i)if(y>=first[i]&&y<last[i]){
   if(l<0||xs[i]<xs[l])l=(int)i;if(r<0||xs[i]>xs[r])r=(int)i;
  }
  if(l>=0&&r>=0&&xs[l]<xs[r]){
   int x0=p4_mesh_pixel_edge(xs[l]),x1=p4_mesh_pixel_edge(xs[r]);
   if(x1>x0&&x1>0&&x0<s->width){
    int ud=(us[r]-us[l])/(x1-x0),vd=(vs[r]-vs[l])/(x1-x0),u=us[l],vv=vs[l];
    if(x0<0){u-=x0*ud;vv-=x0*vd;x0=0;}if(x1>s->width)x1=s->width;
    if(x0<x1){uint16_t *dst=s->pixels+(size_t)y*s->stride_pixels+(size_t)x0,*end=dst+(x1-x0);
     do{*dst++=texture[(((unsigned)vv>>8U)&mask)*size+(((unsigned)u>>8U)&mask)];u+=ud;vv+=vd;}while(dst!=end);
    }
   }
  }
  for(unsigned i=0;i<edges;++i)if(y>=first[i]&&y<last[i]){xs[i]+=dx[i];us[i]+=du[i];vs[i]+=dv[i];}
 }
}
#endif
