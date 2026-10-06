// SPDX-License-Identifier: MIT
#ifndef P4_SHALLOW_WATER_H
#define P4_SHALLOW_WATER_H
/* Bounded staggered-grid shallow water. Caller owns every array (w*h floats,
 * solid mask w*h bytes). h is WATER volume per area; body is displaced solid
 * volume per area, so h+body is the free surface. u/v live on right/down faces.
 * No allocation, global state, hardware, or libm. Single precision uses the
 * P4's F extension. This is a height field, not an overturning 3D liquid. */
#include <stdint.h>
typedef struct {
    int w,h;
    float dx,gravity;
    float *depth,*u,*v,*body,*work_u,*work_v;
    const uint8_t *wet;
} p4_water_t;
static inline float p4_water_limit(float x,float a,float b){return x<a?a:(x>b?b:x);}
static inline float p4_water_sample(const float *a,int w,int h,float x,float y){
    x=p4_water_limit(x,0.0f,(float)(w-1));y=p4_water_limit(y,0.0f,(float)(h-1));
    int ix=(int)x,iy=(int)y,jx=ix+1<w?ix+1:ix,jy=iy+1<h?iy+1:iy;
    float fx=x-(float)ix,fy=y-(float)iy;
    float top=a[iy*w+ix]*(1.0f-fx)+a[iy*w+jx]*fx;
    return top*(1.0f-fy)+(a[jy*w+ix]*(1.0f-fx)+a[jy*w+jx]*fx)*fy;
}
/* dt/dx*max_speed <= 0.24: four outward faces cannot empty a donor cell.
 * Face fluxes are computed from the OLD depth then applied simultaneously.
 * Their equal/opposite updates conserve volume without scan-order bias. */
static inline void p4_water_step(p4_water_t *s,float dt,float ax,float ay,float spin){
    const int w=s->w,h=s->h;const float t=dt/s->dx;
    const float speed=0.24f/t,drag=1.0f-0.18f*dt;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        int i=y*w+x;s->work_u[i]=s->work_v[i]=0.0f;
        if(!s->wet[i])continue;
        float u=s->u[i],v=s->v[i];
        if(x+1<w&&s->wet[i+1]){
            float vy=(v+s->v[i+1]+(y?s->v[i-w]+s->v[i-w+1]:0.0f))*0.25f;
            float adv=p4_water_sample(s->u,w,h,(float)x-u*t,(float)y-vy*t);
            float slope=s->depth[i]+s->body[i]-s->depth[i+1]-s->body[i+1];
            s->work_u[i]=p4_water_limit(adv*drag+s->gravity*t*slope+dt*(ax-spin*((float)y-0.5f*(float)h)),-speed,speed);
        }
        if(y+1<h&&s->wet[i+w]){
            float ux=(u+s->u[i+w]+(x?s->u[i-1]+s->u[i+w-1]:0.0f))*0.25f;
            float adv=p4_water_sample(s->v,w,h,(float)x-ux*t,(float)y-v*t);
            float slope=s->depth[i]+s->body[i]-s->depth[i+w]-s->body[i+w];
            s->work_v[i]=p4_water_limit(adv*drag+s->gravity*t*slope+dt*(ay+spin*((float)x-0.5f*(float)w)),-speed,speed);
        }
    }
    for(int i=0;i<w*h;++i){s->u[i]=s->work_u[i];s->v[i]=s->work_v[i];}
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        int i=y*w+x;float u=s->u[i],v=s->v[i];
        s->work_u[i]=x+1<w?u*t*(u>0.0f?s->depth[i]:s->depth[i+1]):0.0f;
        s->work_v[i]=y+1<h?v*t*(v>0.0f?s->depth[i]:s->depth[i+w]):0.0f;
    }
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        int i=y*w+x;
        s->depth[i]+=(x?s->work_u[i-1]:0.0f)+(y?s->work_v[i-w]:0.0f)-s->work_u[i]-s->work_v[i];
    }
}
#endif
