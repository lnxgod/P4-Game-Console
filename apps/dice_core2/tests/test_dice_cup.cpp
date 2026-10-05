// SPDX-License-Identifier: MIT
#include "dice_cup.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
 for(unsigned count=1;count<=8;++count){
  DiceCup cup;cup.reset(count);
  for(unsigned tick=0;tick<5000;++tick){
   unsigned held=tick>100?1:0;
   if(tick%7==0)cup.hit(4,held);
   cup.step(tick%4==0?.20f:.03f,std::sin(tick*.8f)*8,std::cos(tick*.6f)*8,held);
   for(unsigned i=0;i<count;++i){const auto &d=cup.dice[i];
    assert(std::isfinite(d.x)&&std::isfinite(d.y)&&std::isfinite(d.frame));
    assert(d.x>=0&&d.x<=320&&d.y>=50&&d.y<=176);
    assert(d.frame>=0&&d.frame<96&&d.lift>=0&&d.lift<=5);
    if(i==0&&held){float x=count>5?40:160-((float)count-1)*31;assert(d.x==x&&d.y==(count>5?83:99)&&d.lift==0);}
   }
  }
 }
 DiceCup pair;pair.reset(2);pair.dice[0].x=pair.dice[1].x=160;pair.dice[0].y=pair.dice[1].y=110;
 pair.dice[0].vx=pair.dice[0].vy=pair.dice[1].vx=pair.dice[1].vy=0;pair.step(0,0,0,0);
 assert(pair.dice[0].x!=pair.dice[1].x);
 puts("PASS: bounded cup dynamics for 1-8 dice, held dice, large timesteps, coincident collisions");
}
