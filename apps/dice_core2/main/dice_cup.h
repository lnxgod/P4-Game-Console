// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
struct DiceCupBody {float x,y,vx,vy,frame,spin,lift,vz;};
struct DiceCup {
    DiceCupBody dice[8]{};
    unsigned count=0;
    void reset(unsigned n);
    void hit(float energy,uint8_t held);
    void step(float dt,float force_x,float force_y,uint8_t held);
};
