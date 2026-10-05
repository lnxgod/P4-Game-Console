// SPDX-License-Identifier: MIT
#include "dice_cup.h"
#include <algorithm>
#include <cmath>
namespace {float clamp(float x,float lo,float hi){return std::max(lo,std::min(x,hi));}}
void DiceCup::reset(unsigned n){
    count=std::min(n,8U);
    for(unsigned i=0;i<count;++i)dice[i]={160-((float)count-1)*26+i*52,110+(i%2?8.0f:-8.0f),
        (i%2?1.0f:-1.0f)*40,(i%3?1.0f:-1.0f)*36,(float)(i*9),18+(float)i*3,0,0};
}
void DiceCup::hit(float energy,uint8_t held){
    energy=clamp(energy,.2f,2.5f);
    for(unsigned i=0;i<count;++i)if(!(held&(1U<<i))){
        auto &d=dice[i];d.vx+=(i%2?1:-1)*(38+energy*24);d.vy+=(i%3?1:-1)*(35+energy*20);
        d.vz=35+energy*15;d.spin+=(i%2?1:-1)*(9+energy*3);
    }
}
void DiceCup::step(float dt,float force_x,float force_y,uint8_t held){
    dt=clamp(dt,0,.05f);force_x=clamp(force_x,-3,3);force_y=clamp(force_y,-3,3);
    for(unsigned i=0;i<count;++i){
        auto &d=dice[i];
        if(held&(1U<<i)){
            d.x=count>5?40+(i%4)*80:160-((float)count-1)*31+i*62;
            d.y=count>5?83+(i/4)*56:99;d.lift=d.vx=d.vy=0;continue;
        }
        d.vx=clamp((d.vx+force_x*850*dt)*(1-dt*1.7f),-220,220);
        d.vy=clamp((d.vy+force_y*750*dt)*(1-dt*1.7f),-175,175);
        d.x+=d.vx*dt;d.y+=d.vy*dt;
        // Elliptical cup wall. Reflect outward velocity at the local normal.
        float nx=(d.x-160)/130,ny=(d.y-114)/28;
        float radius=std::sqrt(nx*nx+ny*ny);
        if(radius>1){
            d.x=160+nx*130/radius;d.y=114+ny*28/radius;
            float wx=nx/130,wy=ny/28,len=std::sqrt(wx*wx+wy*wy);wx/=len;wy/=len;
            float speed=d.vx*wx+d.vy*wy;
            if(speed>0){d.vx-=1.65f*speed*wx;d.vy-=1.65f*speed*wy;d.spin+=speed*.07f;}
        }
        d.vz-=500*dt;d.lift=clamp(d.lift+d.vz*dt,0,5);
        if(d.lift==0&&d.vz<0)d.vz=-d.vz*.15f;
        d.spin=clamp(d.spin*(1-dt*.4f),-36,36);
        d.frame=std::fmod(d.frame+(d.spin+(std::fabs(d.vx)+std::fabs(d.vy))*.04f)*dt*2+96,96);
    }
    float diameter=count>5?33:43;
    for(unsigned i=0;i<count;++i)for(unsigned j=i+1;j<count;++j){
        auto &a=dice[i];auto &b=dice[j];float x=b.x-a.x,y=b.y-a.y,d2=x*x+y*y;
        if(d2>=diameter*diameter)continue;
        if(d2<.01f){x=.1f;y=0;d2=.01f;}
        bool fixed_a=held&(1U<<i),fixed_b=held&(1U<<j);if(fixed_a&&fixed_b)continue;
        float distance=std::sqrt(d2);x/=distance;y/=distance;
        float shift=(diameter-distance)/(fixed_a||fixed_b?1:2);
        if(!fixed_a){a.x-=x*shift;a.y-=y*shift;}
        if(!fixed_b){b.x+=x*shift;b.y+=y*shift;}
        float speed=(b.vx-a.vx)*x+(b.vy-a.vy)*y;
        if(speed<0){
            float kick=-(1.5f*speed)/(fixed_a||fixed_b?1:2);
            if(!fixed_a){a.vx-=kick*x;a.vy-=kick*y;a.spin-=kick*.06f;}
            if(!fixed_b){b.vx+=kick*x;b.vy+=kick*y;b.spin+=kick*.06f;}
        }
    }
    // Bounds remain valid after pair separation, including dense 8-die games.
    for(unsigned i=0;i<count;++i)if(!(held&(1U<<i))){dice[i].x=clamp(dice[i].x,28,292);dice[i].y=clamp(dice[i].y,86,142);}
}
