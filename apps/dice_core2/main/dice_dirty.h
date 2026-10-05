// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
struct DiceDirtyRect {int16_t x,y,w,h;};
constexpr unsigned DICE_DIRTY_MAX=80;
unsigned dice_dirty_rects(const DiceDirtyRect *boxes,unsigned boxes_count,DiceDirtyRect out[DICE_DIRTY_MAX]);
