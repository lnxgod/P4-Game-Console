// SPDX-License-Identifier: MIT
#pragma once
#include "p4/dice.h"
#include <cstring>
// Main-core owned. A selection remains pending until an authoritative request
// echoes it. Later taps survive an older acknowledgement in flight.
struct DiceControls {
    p4_dice_request_t base{};
    uint8_t selected=0;
    uint16_t sequence=0;
    bool pending=false;
    void update(const p4_dice_request_t &r) {
        bool same_dice=base.token && r.token && base.can_hold && r.can_hold &&
            base.player_slot==r.player_slot && base.count==r.count &&
            base.sides==r.sides && !std::memcmp(base.faces,r.faces,sizeof(r.faces));
        if(!pending || !same_dice || r.hold_ack==sequence) {
            selected=r.held_mask;sequence=r.hold_ack;pending=false;
        }
        base=r;
    }
    bool toggle(unsigned index,p4_dice_phase_t phase) {
        if(!base.token || !base.can_hold || index>=base.count ||
            phase!=P4_DICE_WAITING) return false;
        selected^=(uint8_t)(1U<<index);++sequence;pending=true;
        return true;
    }
    p4_dice_request_t request() const {auto r=base;r.held_mask=selected;return r;}
};
inline int dice_touch_index(unsigned count,int x,int y) {
    if(count<1 || count>P4_DICE_MAX || x<0 || x>=320) return -1;
    if(count>5) {
        if(y<55 || y>=167) return -1;
        unsigned index=(unsigned)((y-55)/56)*4U+(unsigned)x/80U;
        return index<count?(int)index:-1;
    }
    if(y<64 || y>=176) return -1;
    int left=160-(int)count*31;
    int index=(x-left)/62;
    return x>=left && index<(int)count?index:-1;
}
