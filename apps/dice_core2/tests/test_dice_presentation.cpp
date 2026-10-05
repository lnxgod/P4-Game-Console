// SPDX-License-Identifier: MIT
#include "dice_presentation.h"
#include <cassert>
#include <cstdio>
int main(){
    DicePresentation p;p4_dice_request_t r{};r.token=1;
    p.update(r,P4_DICE_WAITING,true);p.update(r,P4_DICE_READY,true);
    p.update(r,P4_DICE_SHAKING,true);p.update(r,P4_DICE_ROLLED,true);
    assert(p.waiting&&!p.landed);
    // The renderer can skip ROLLED entirely and still receive the result.
    ++r.token;p.update(r,P4_DICE_WAITING,true);assert(p.landed&&!p.waiting);
    p.update(r,P4_DICE_READY,true);assert(!p.landed&&!p.waiting);
    p.update(r,P4_DICE_ROLLED,true);r.player_slot=1;++r.token;
    p.update(r,P4_DICE_WAITING,true);assert(!p.landed&&!p.waiting);
    r.token=0;p.update(r,P4_DICE_OFFLINE,false);assert(!p.waiting&&!p.landed);
    puts("PASS: landing survives skipped render frames; Ready, handoff and disconnect reset it");
}
