// SPDX-License-Identifier: MIT
#pragma once
#include "p4/dice.h"
#include <stdint.h>
bool dice_view_begin();
void dice_view_draw(const p4_dice_request_t &request,p4_dice_phase_t phase,
    bool connected,bool practice,bool searching,bool imu_ok,uint32_t now,bool hold_pending=false);

void dice_view_motion(float x,float y,float energy,bool hit);
