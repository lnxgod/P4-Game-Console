// SPDX-License-Identifier: MIT
#include "p4/mesh.h"
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Original scalar implementations retained as independent pixel oracles. */
static void reference_p4_draw_fill_rect(p4_game_surface_t *surface,
                       int x, int y, int width, int height,
                       uint16_t color)
{
    if (!p4_surface_valid(surface) || width <= 0 || height <= 0) {
        return;
    }
    int64_t left = x;
    int64_t top = y;
    int64_t right = left + width;
    int64_t bottom = top + height;
    if (right <= 0 || bottom <= 0 || left >= surface->width ||
        top >= surface->height) {
        return;
    }
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > surface->width) {
        right = surface->width;
    }
    if (bottom > surface->height) {
        bottom = surface->height;
    }
    for (int64_t row = top; row < bottom; ++row) {
        for (int64_t column = left; column < right; ++column) {
            surface->pixels[(size_t)row * surface->stride_pixels +
                            (size_t)column] = color;
        }
    }
}
static void reference_p4_draw_fill_circle(p4_game_surface_t *surface,
                         int center_x, int center_y, int radius,
                         uint16_t color)
{
    if (!p4_surface_valid(surface) || radius < 0 || radius > 1024) {
        return;
    }
    const int64_t radius_squared = (int64_t)radius * radius;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if ((int64_t)x * x + (int64_t)y * y <= radius_squared) {
                p4_draw_pixel(surface, center_x + x, center_y + y, color);
            }
        }
    }
}
static void reference_p4_draw_sprite_rgb565(p4_game_surface_t *surface,
                           int x, int y,
                           const uint16_t *pixels,
                           size_t width, size_t height,
                           size_t stride_pixels,
                           bool use_transparency,
                           uint16_t transparent_color)
{
    if (!p4_surface_valid(surface) || pixels == NULL || width == 0U ||
        height == 0U || width > 4096U || height > 4096U ||
        stride_pixels < width) {
        return;
    }
    for (size_t row = 0U; row < height; ++row) {
        for (size_t column = 0U; column < width; ++column) {
            const uint16_t color = pixels[row * stride_pixels + column];
            if (!use_transparency || color != transparent_color) {
                p4_draw_pixel(surface,
                              x + (int)column,
                              y + (int)row,
                              color);
            }
        }
    }
}
static void verify(unsigned w,unsigned h){
 size_t stride=w+7U,count=stride*h+38U;
 uint16_t *a=malloc(count*2U);assert(a);
 p4_game_surface_t s={.pixels=a+19,.width=(uint16_t)w,.height=(uint16_t)h,.stride_pixels=stride};
 uint16_t texture[128*128];for(unsigned i=0;i<128U*128U;++i)texture[i]=(uint16_t)(i+1U);
 for(unsigned n=0;n<32;++n){
  for(size_t i=0;i<count;++i)a[i]=0xa55a;
  int x=(int)(n*37U%(w+40U))-20,y=(int)(n*23U%(h+40U))-20;
  p4_mesh_point_t box[4]={{x,y},{x+32,y},{x+32,y+32},{x,y+32}};
  p4_draw_face(&s,box,4,0xcafe);
  for(unsigned py=0;py<h;++py)for(unsigned px=0;px<w;++px){bool hit=(int)px>=x&&(int)px<x+32&&(int)py>=y&&(int)py<y+32;assert(s.pixels[py*stride+px]==(hit?0xcafe:0xa55a));}
  p4_mesh_tex_vertex_t quad[4]={{{x,y},-8*256,-9*256},{{x+32,y},24*256,-9*256},{{x+32,y+32},24*256,23*256},{{x,y+32},-8*256,23*256}};
  p4_draw_textured_face(&s,quad,4,texture,128);
  for(unsigned py=0;py<h;++py)for(unsigned px=0;px<w;++px)if((int)px>=x&&(int)px<x+32&&(int)py>=y&&(int)py<y+32){unsigned tx=(unsigned)((int)px-x-8)&127U,ty=(unsigned)((int)py-y-9)&127U;assert(s.pixels[py*stride+px]==texture[ty*128U+tx]);}
  for(unsigned py=0;py<h;++py)for(size_t px=w;px<stride;++px)assert(s.pixels[py*stride+px]==0xa55a);
  for(unsigned k=0;k<19;++k)assert(a[k]==0xa55a&&a[count-1U-k]==0xa55a);
 }
 /* Geometric coverage oracle for a slanted face, ignoring boundary ties. */
 memset(a,0,count*2U);
 const p4_mesh_point_t triangle[3]={{-7,13},{183,27},{67,173}};p4_draw_face(&s,triangle,3,1);
 for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){double cross[3];bool inside=true,edge=false;
  for(unsigned j=0;j<3;++j){p4_mesh_point_t p=triangle[j],q=triangle[(j+1U)%3U];cross[j]=(q.x-p.x)*((double)y+.5-p.y)-(q.y-p.y)*((double)x+.5-p.x);if(cross[j]<0)inside=false;if(cross[j]>-2&&cross[j]<2)edge=true;}
  if(!edge)assert((s.pixels[y*stride+x]!=0)==inside);
 }
 /* Entirely clipped, one-pixel spans with extreme UV deltas must not
  * overflow while advancing to the viewport or touch the surface. */
 uint16_t *offscreen=malloc(count*2U);assert(offscreen);memcpy(offscreen,a,count*2U);
 const p4_mesh_tex_vertex_t narrow[4]={{{-4096,0},-1048576,0},{{-4095,0},1048576,0},{{-4095,32},1048576,8192},{{-4096,32},-1048576,8192}};
 p4_draw_textured_face(&s,narrow,4,texture,128);assert(memcmp(a,offscreen,count*2U)==0);free(offscreen);
 p4_mesh_point_t invalid[4]={{INT_MIN,0},{1,0},{1,1},{0,1}};
 uint16_t *saved=malloc(count*2U);assert(saved);memcpy(saved,a,count*2U);p4_draw_face(&s,invalid,4,5);p4_draw_mesh_line(&s,invalid[0],invalid[1],5);assert(memcmp(saved,a,count*2U)==0);
 free(saved);free(a);
}
static void raster_equivalence(unsigned w,unsigned h){
 size_t stride=w+7U,count=stride*h+38U;uint16_t *a=malloc(count*2U),*b=malloc(count*2U);assert(a&&b);
 p4_game_surface_t aa={.pixels=a+19,.width=(uint16_t)w,.height=(uint16_t)h,.stride_pixels=stride},bb=aa;bb.pixels=b+19;
 uint16_t sprite[17*13];for(unsigned i=0;i<17U*13U;++i)sprite[i]=(uint16_t)i;
 for(unsigned n=0;n<60;++n){
  for(size_t k=0;k<count;++k)a[k]=b[k]=(uint16_t)(k*13U);
  int x=(int)(n*53U%(w+80U))-40,y=(int)(n*29U%(h+80U))-40,r=(int)(n*7U%80U);
  p4_draw_fill_rect(&aa,x,y,r*2,r+3,0xbeef);reference_p4_draw_fill_rect(&bb,x,y,r*2,r+3,0xbeef);
  assert(memcmp(a,b,count*2U)==0);
  p4_draw_fill_circle(&aa,x,y,r,0xcafe);reference_p4_draw_fill_circle(&bb,x,y,r,0xcafe);assert(memcmp(a,b,count*2U)==0);
  p4_draw_sprite_rgb565(&aa,x,y,sprite,17,13,17,n%2U!=0U,0);reference_p4_draw_sprite_rgb565(&bb,x,y,sprite,17,13,17,n%2U!=0U,0);assert(memcmp(a,b,count*2U)==0);
 }
 p4_draw_fill_rect(&aa,INT_MIN,INT_MIN,INT_MAX,INT_MAX,0);reference_p4_draw_fill_rect(&bb,INT_MIN,INT_MIN,INT_MAX,INT_MAX,0);assert(memcmp(a,b,count*2U)==0);
 free(a);free(b);
}
int main(void){verify(320,200);verify(768,480);raster_equivalence(320,200);raster_equivalence(768,480);puts("Mesh geometry, UV wrapping, clipping, padded stride, guards and 360 full raster equivalence comparisons PASS");}
