// SPDX-License-Identifier: MIT
#include "dice_controls.h"
#include <cassert>
#include <cstdio>
int main() {
    DiceControls c;p4_dice_request_t r{};r.token=1;r.count=5;r.sides=6;r.enabled=true;r.can_hold=true;
    for(unsigned i=0;i<5;++i)r.faces[i]=(uint8_t)(i+1);
    c.update(r);
    for(unsigned i=0;i<5;++i)assert(dice_touch_index(5,36+(int)i*62,99)==(int)i);
    assert(dice_touch_index(5,160,201)==-1 && dice_touch_index(5,-1,99)==-1);
    assert(dice_touch_index(8,280,145)==7 && dice_touch_index(6,280,145)==-1);
    assert(c.toggle(0,P4_DICE_WAITING));assert(c.pending && c.request().held_mask==1);
    c.update(r);assert(c.pending && c.selected==1); // unchanged heartbeat cannot undo a tap
    assert(c.toggle(2,P4_DICE_WAITING));assert(c.selected==5);
    ++r.token;r.held_mask=1;r.hold_ack=1;c.update(r);assert(c.pending && c.selected==5); // older ack
    ++r.token;r.held_mask=5;r.hold_ack=2;c.update(r);assert(!c.pending && c.selected==5);
    assert(!c.toggle(1,P4_DICE_SHAKING) && !c.toggle(5,P4_DICE_WAITING));
    ++r.token;r.held_mask=31;r.enabled=false;c.update(r);
    assert(c.toggle(0,P4_DICE_WAITING) && c.selected==30); // unhold all-held state
    ++r.token;r.held_mask=30;r.hold_ack=c.sequence;r.enabled=true;c.update(r);assert(!c.pending);
    assert(c.toggle(1,P4_DICE_WAITING));
    assert(c.toggle(1,P4_DICE_WAITING));assert(c.pending && c.selected==30);
    ++r.token;r.held_mask=28;r.hold_ack=(uint16_t)(c.sequence-1);c.update(r);
    assert(c.pending && c.selected==30); // undo must survive intermediate confirmation
    ++r.token;r.held_mask=30;r.hold_ack=c.sequence;c.update(r);assert(!c.pending);
    assert(c.toggle(1,P4_DICE_WAITING));
    ++r.token;r.player_slot=1;r.held_mask=0;r.can_hold=false;c.update(r);
    assert(!c.pending && c.selected==0 && !c.toggle(0,P4_DICE_WAITING));
    r.can_hold=true;c.update(r);assert(c.toggle(0,P4_DICE_WAITING));
    ++r.token;r.faces[0]=6;c.update(r);assert(!c.pending && c.selected==0);
    c.update({});assert(!c.pending && !c.request().token);
    puts("PASS: touch targets, rapid taps/old ack, unhold, locked shake, turn/roll/disconnect cancellation");
}
