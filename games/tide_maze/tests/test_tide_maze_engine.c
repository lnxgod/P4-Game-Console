// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern void tm_ball_forces(tm_state *,unsigned);
static double volume(const tm_state *s){double v=0;for(int i=0;i<TM_CELLS;++i)v+=s->water[i];return v;}
static void physical_water(void){
 tm_state *s=calloc(1,sizeof(*s));assert(s);tm_reset(s,0);s->ball[0].rescue=1;
 double initial=volume(s);
 for(int n=0;n<200;++n)tm_fluid(s);
 for(int i=0;i<TM_CELLS;++i){assert(s->water[i]==(s->wet[i]?6.0f:0.0f));assert(s->flow_x[i]==0.0f&&s->flow_y[i]==0.0f);}
 /* Former solver discarded these tilts entirely. */
 s->intent[0].x=35;for(int n=0;n<50;++n)tm_fluid(s);
 double movement=0.0;for(int i=0;i<TM_CELLS;++i)movement+=fabs((double)s->water[i]-(s->wet[i]?6.0:0.0));
 assert(movement>1.0);assert(fabs(volume(s)-initial)<initial*0.00001);
 s->intent[0].x=0;double peak=0.0;
 for(int n=0;n<25;++n){tm_fluid(s);double v=0;for(int i=0;i<TM_CELLS;++i)v+=fabs((double)s->flow_x[i]);if(n==0)peak=v;if(n==4)assert(v>peak*0.1);}
 printf("small tilt displaced %.3f grid-volume units; momentum persists after release\n",movement);
 /* A full-height obstacle reflects flow and never transports mass through it. */
 tm_reset(s,0);s->ball[0].rescue=1;
 for(int y=0;y<TM_H;++y)for(int x=0;x<TM_W;++x){int i=y*TM_W+x;s->wet[i]=(uint8_t)(x>0&&x<TM_W-1&&y>0&&y<TM_H-1&&x!=TM_W/2);s->water[i]=s->wet[i]?6.0f:0.0f;}
 double right=0;for(int y=0;y<TM_H;++y)for(int x=TM_W/2+1;x<TM_W;++x)right+=s->water[y*TM_W+x];
 s->water[10*TM_W+12]+=2.0f;s->water[10*TM_W+11]-=2.0f;
 for(int n=0;n<300;++n)tm_fluid(s);
 double after=0;for(int y=0;y<TM_H;++y)for(int x=TM_W/2+1;x<TM_W;++x)after+=s->water[y*TM_W+x];
 assert(fabs(right-after)<0.0001);
 for(int y=0;y<TM_H;++y)assert(s->flow_x[y*TM_W+TM_W/2-1]==0.0f);
 /* Solid displacement, buoyancy and current-to-ball drag are observable. */
 tm_reset(s,0);initial=volume(s);s->ball[0].x=64*TM_Q;s->ball[0].y=40*TM_Q;s->ball[0].z=4.0f;
 tm_fluid(s);float displaced=0;for(int i=0;i<TM_CELLS;++i)displaced+=s->body[i];assert(displaced>5.0f);
 assert(fabs(volume(s)-initial)<0.01);
 for(int i=0;i<TM_CELLS;++i){s->water[i]=s->wet[i]?10.0f:0.0f;s->body[i]=0;s->flow_x[i]=s->wet[i]?20.0f:0.0f;}
 s->ball[0].z=4.0f;s->ball[0].vz=0;s->ball[0].vx=0;tm_ball_forces(s,0);
 assert(s->ball[0].vz>0.0f&&s->ball[0].z>4.0f&&s->ball[0].vx>0);
 printf("sphere displaced %.3f grid-volume units; buoyancy and current drag PASS; state=%zu\n",(double)displaced,sizeof(*s));free(s);
}
static void depth_and_clipping(void){
 enum{W=64,H=32,STRIDE=71};uint16_t pixels[STRIDE*H+32],depth[W*8];
 p4_game_surface_t f={pixels+16,STRIDE,W,H};
 p4_3d_vertex_t near[3]={{-16,-16,12000,0xf800,0,0},{500,4,12000,0xf800,0,0},{252,256,12000,0xf800,0,0}};
 p4_3d_vertex_t far[3]={{-16,-16,6000,0x07e0,0,0},{500,4,6000,0x07e0,0,0},{252,256,6000,0x07e0,0,0}};
 uint16_t first[W*H];
 for(int order=0;order<2;++order){for(unsigned i=0;i<sizeof(pixels)/sizeof(pixels[0]);++i)pixels[i]=0x1234;
  for(int y=0;y<H;++y){for(int x=0;x<W;++x)f.pixels[y*STRIDE+x]=0;}
  for(int y=0;y<H;y+=8){p4_3d_band_t b;p4_3d_begin(&b,&f,depth,y,8);
   p4_3d_triangle(&b,order?near:far,order?near+1:far+1,order?near+2:far+2,false);
   p4_3d_triangle(&b,order?far:near,order?far+1:near+1,order?far+2:near+2,false);
  }
  assert(f.pixels[16*STRIDE+32]==0xf800);
  for(int y=0;y<H;++y){for(int x=0;x<W;++x){if(!order)first[y*W+x]=f.pixels[y*STRIDE+x];else assert(first[y*W+x]==f.pixels[y*STRIDE+x]);}for(int x=W;x<STRIDE;++x)assert(f.pixels[y*STRIDE+x]==0x1234);}
  for(int i=0;i<16;++i){assert(pixels[i]==0x1234);assert(pixels[STRIDE*H+16+i]==0x1234);}
 }
 puts("native triangle depth is draw-order independent; offscreen clipping, band seams and stride guards PASS");
}
int main(void){physical_water();depth_and_clipping();return 0;}
