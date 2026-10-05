// SPDX-License-Identifier: MIT
#pragma once
#include "p4/dice.h"
// Track semantic transitions on the motion core, so skipping an obsolete
// render frame never loses the landing/result handshake.
struct DicePresentation {
    bool landed=false,waiting=false,practice=false;
    uint32_t awaited=0;
    uint8_t player=0;
    p4_dice_phase_t phase=P4_DICE_OFFLINE;
    void update(const p4_dice_request_t &r,p4_dice_phase_t next,bool demo) {
        if(next==P4_DICE_ROLLED && phase!=P4_DICE_ROLLED){waiting=true;awaited=r.token;}
        if(waiting && r.token!=awaited){waiting=false;landed=true;}
        if(next==P4_DICE_READY || next==P4_DICE_SHAKING || !r.token ||
            demo!=practice || r.player_slot!=player){landed=false;waiting=false;}
        phase=next;practice=demo;player=r.player_slot;
    }
};
