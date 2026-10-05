// SPDX-License-Identifier: MIT
#include "dice_dirty.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <algorithm>
int main(){
 DiceDirtyRect output[DICE_DIRTY_MAX];assert(dice_dirty_rects(nullptr,0,output)==0);uint32_t random=9;
 for(unsigned frame=0;frame<500;++frame){
  DiceDirtyRect boxes[16];std::array<bool,320*240> changed{},covered{};
  for(auto &b:boxes){random^=random<<13;random^=random>>17;random^=random<<5;
   b={(int16_t)((int)(random%400)-40),(int16_t)((int)(random/400%180)+20),(int16_t)(1+random%70),(int16_t)(1+random/70%70)};
   for(int y=std::max(50,(int)b.y);y<std::min(176,b.y+b.h);++y)
    for(int x=std::max(0,(int)b.x);x<std::min(320,b.x+b.w);++x)changed[y*320+x]=true;
  }
  unsigned count=dice_dirty_rects(boxes,16,output);assert(count<=DICE_DIRTY_MAX);
  for(unsigned i=0;i<count;++i){auto r=output[i];assert(r.x>=0&&r.x+r.w<=320&&r.y>=50&&r.y+r.h<=176&&r.w>0&&r.h>0);
   for(int y=r.y;y<r.y+r.h;++y)for(int x=r.x;x<r.x+r.w;++x)covered[y*320+x]=true;}
  for(unsigned i=0;i<changed.size();++i)assert(!changed[i]||covered[i]);
 }
 DiceDirtyRect stripes[10];for(int i=0;i<10;++i)stripes[i]={(int16_t)(i*32),50,16,126};
 assert(dice_dirty_rects(stripes,10,output)==DICE_DIRTY_MAX);
 puts("PASS: moving-rectangle coverage, clipped edge bands, unchanged frames and maximum run count");
}
