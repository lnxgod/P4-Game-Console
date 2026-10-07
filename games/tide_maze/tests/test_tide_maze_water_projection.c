// SPDX-License-Identifier: MIT
#include "tide_maze_water_projection.h"
#include <assert.h>
#include <stdio.h>

/* Original general world projection is the oracle for every possible bounded
 * water height, including negative numerators and C truncation near zero. */
static p4_mesh_point_t original(int x,int y,int z,int width,int height){
 const int xc=x-120*TM_Q,depth=600-y/TM_Q-xc/(10*TM_Q);
 const int sx=156*TM_Q+(xc*530+(y-72*TM_Q)*60)/depth;
 const int sy=42*TM_Q+(y*330+xc*45-z*600)/depth;
 return (p4_mesh_point_t){sx*width/(320*TM_Q),sy*height/(200*TM_Q)};
}
int main(void){
 unsigned comparisons=0;
 for(unsigned native=0;native<2;++native){
  const int width=native?768:320,height=native?480:200;
  for(int y=0;y<=TM_H;++y)for(int x=0;x<=TM_W;++x)
   for(int z=87;z<=1193;++z){
    p4_mesh_point_t a=original(x*8*TM_Q,y*8*TM_Q,z,width,height);
    p4_mesh_point_t b=tm_water_project(&tm_water_projection[y][x],z,native!=0);
    assert(a.x==b.x&&a.y==b.y);++comparisons;
   }
 }
 printf("%u exact native/legacy water projections match original integer oracle\n",comparisons);
 return 0;
}
