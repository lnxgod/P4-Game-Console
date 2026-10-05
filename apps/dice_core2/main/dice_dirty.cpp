// SPDX-License-Identifier: MIT
#include "dice_dirty.h"
#include <algorithm>
unsigned dice_dirty_rects(const DiceDirtyRect *boxes,unsigned boxes_count,DiceDirtyRect out[DICE_DIRTY_MAX]){
    bool dirty[8][20]{};unsigned count=0;
    for(unsigned i=0;i<boxes_count;++i){auto b=boxes[i];
        int x0=std::max(0,(int)b.x),y0=std::max(50,(int)b.y);
        int x1=std::min(320,(int)b.x+b.w),y1=std::min(176,(int)b.y+b.h);
        if(x0>=x1||y0>=y1)continue;
        for(int row=(y0-50)/16;row<=(y1-51)/16;++row)
            for(int col=x0/16;col<=(x1-1)/16;++col)dirty[row][col]=true;
    }
    for(int row=0;row<8;++row)for(int col=0;col<20;){
        if(!dirty[row][col]){++col;continue;}
        int first=col;while(col<20&&dirty[row][col])++col;
        int y=50+row*16;
        out[count++]={(int16_t)(first*16),(int16_t)y,(int16_t)((col-first)*16),(int16_t)std::min(16,176-y)};
    }
    return count;
}
