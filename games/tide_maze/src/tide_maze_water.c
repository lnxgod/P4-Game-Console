// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include <string.h>
static float absolute(float x){return x<0.0f?-x:x;}
/* Bounded sqrt without a runtime import; callers provide [0,64]. */
static float root(float x){if(x<=0.0f)return 0.0f;float r=x>1.0f?x:1.0f;for(int i=0;i<7;++i)r=0.5f*(r+x/r);return r;}
void tm_water_reset(tm_state *s){
 memset(s->flow_x,0,sizeof(s->flow_x));memset(s->flow_y,0,sizeof(s->flow_y));memset(s->body,0,sizeof(s->body));
 for(int y=0;y<TM_H;++y)for(int x=0;x<TM_W;++x){int i=y*TM_W+x;
  s->wet[i]=(uint8_t)(tm_tile(s->level,x/4,y/4)!='#');
  s->water[i]=s->wet[i]?6.0f:0.0f;s->previous_water[i]=s->water[i];
 }
}
float tm_water_height(const tm_state *s,float x,float y){
 int cx=tm_clamp((int)(x*0.25f),0,TM_W-1),cy=tm_clamp((int)(y*0.25f),0,TM_H-1);
 return s->water[cy*TM_W+cx]+s->body[cy*TM_W+cx];
}
/* Integrate the sphere's submerged column with four samples per grid cell.
 * Solid volume raises eta=h+body. Motion of that volume drives a pressure wave;
 * no artistic ripple or volume creation is added to the water. */
static void displace(tm_state *s){
 memset(s->body,0,sizeof(s->body));
 for(int p=0;p<(s->linked?2:1);++p){
  const tm_ball *b=&s->ball[p];if(b->rescue)continue;
  float bx=(float)b->x/256.0f,by=(float)b->y/256.0f;
  int cx=(int)(bx*0.25f),cy=(int)(by*0.25f);
  for(int y=tm_clamp(cy-2,0,TM_H-1);y<=tm_clamp(cy+2,0,TM_H-1);++y)
   for(int x=tm_clamp(cx-2,0,TM_W-1);x<=tm_clamp(cx+2,0,TM_W-1);++x){
    int i=y*TM_W+x;if(!s->wet[i])continue;float volume=0.0f;
    for(int oy=1;oy<=3;oy+=2)for(int ox=1;ox<=3;ox+=2){
     float dx=(float)(x*4+ox)-bx,dy=(float)(y*4+oy)-by,d=16.0f-dx*dx-dy*dy;
     if(d<=0.0f)continue;float half=root(d);
     float low=b->z-half,high=b->z+half;if(low<0.0f)low=0.0f;
     /* Previous surface is stable across this complete displacement pass. */
     if(high>s->previous_water[i])high=s->previous_water[i];
     if(high>low)volume+=(high-low)*0.25f;
    }
    s->body[i]+=volume;
    /* Momentum exchange with submerged body, equal sign to the drag acting
     * on the ball. Walls are re-imposed by the face solver immediately. */
    float coupling=volume*0.012f;
    float vx=(float)b->vx/(256.0f*0.02f),vy=(float)b->vy/(256.0f*0.02f);
    s->flow_x[i]+=(vx-s->flow_x[i])*coupling;
    s->flow_y[i]+=(vy-s->flow_y[i])*coupling;
   }
 }
}
void tm_fluid(tm_state *s){
 for(int i=0;i<TM_CELLS;++i)s->previous_water[i]=s->water[i]+s->body[i];
 displace(s);
 int count=s->linked?2:1;
 float ax=(float)(s->intent[0].x+(count==2?s->intent[1].x:0))/(float)count;
 float ay=(float)(s->intent[0].y+(count==2?s->intent[1].y:0))/(float)count;
 float spin=(float)(s->intent[0].spin+(count==2?s->intent[1].spin:0))/(float)count;
 p4_water_t water={TM_W,TM_H,4.0f,800.0f,s->water,s->flow_x,s->flow_y,s->body,s->work_x,s->work_y,s->wet};
 /* Two 10ms substeps: CFL bounded even at the maximum allowed face speed. */
 for(int sub=0;sub<2;++sub)p4_water_step(&water,0.01f,ax*0.06f,ay*0.06f,spin*0.0008f);
 /* The tray visibly tracks the same gravity input; gyro-induced waves remain
  * physical and do not become a perpetually scrolling texture. */
 s->previous_view_x=s->view_x;s->previous_view_y=s->view_y;
 s->view_x+=(ax*0.00017f-s->view_x)*0.22f;s->view_y+=(ay*0.00017f-s->view_y)*0.22f;
}
/* Kept here so the force and displacement model share units. A sphere's
 * submerged fraction follows the spherical-cap volume, not a binary wet flag. */
void tm_ball_forces(tm_state *s,unsigned p){
 tm_ball *b=&s->ball[p];const tm_intent *in=&s->intent[p];
 float x=(float)b->x/256.0f,y=(float)b->y/256.0f;
 int ix=tm_clamp((int)(x*0.25f),1,TM_W-2),iy=tm_clamp((int)(y*0.25f),1,TM_H-2),i=iy*TM_W+ix;
 float height=tm_water_height(s,x,y);
 float cap=p4_water_limit((height-b->z+4.0f)/8.0f,0.0f,1.0f);
 float submerged=cap*cap*(3.0f-2.0f*cap);
 float u=(s->flow_x[i]+s->flow_x[i-1])*0.5f,v=(s->flow_y[i]+s->flow_y[i-TM_W])*0.5f;
 float vx=(float)b->vx/(256.0f*0.02f),vy=(float)b->vy/(256.0f*0.02f);
 const float density=0.85f,drag=1.5f*submerged;
 float ax=(float)in->x*0.50f+(u-vx)*drag,ay=(float)in->y*0.50f+(v-vy)*drag;
 /* Gradient pressure forces also couple waves to a nearly stationary ball. */
 if(s->wet[i-1]&&s->wet[i+1])ax+=(s->water[i-1]+s->body[i-1]-s->water[i+1]-s->body[i+1])*submerged*9.0f;
 if(s->wet[i-TM_W]&&s->wet[i+TM_W])ay+=(s->water[i-TM_W]+s->body[i-TM_W]-s->water[i+TM_W]-s->body[i+TM_W])*submerged*9.0f;
 vx=(vx+ax*0.02f)*(in->brake?0.76f:0.997f);vy=(vy+ay*0.02f)*(in->brake?0.76f:0.997f);
 b->vx=tm_clamp((int)(vx*(256.0f*0.02f)),-280,280);b->vy=tm_clamp((int)(vy*(256.0f*0.02f)),-280,280);
 b->previous_z=b->z;
 b->vz+=(800.0f*(submerged/density-1.0f)-b->vz*(2.5f+submerged*4.0f))*0.02f;
 if(absolute((float)in->jolt)>120.0f)b->vz+=(float)in->jolt*0.008f;
 b->vz=p4_water_limit(b->vz,-60.0f,60.0f);b->z+=b->vz*0.02f;
 if(b->z<4.0f){b->z=4.0f;if(b->vz<0.0f)b->vz=-b->vz*0.12f;}
 if(b->z>22.0f){b->z=22.0f;b->vz=0.0f;}
}
