// SPDX-License-Identifier: MIT
#include "p4/dice_accessory.h"
#include "p4/multiplayer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    p4_dice_request_t r={.token=123,.player_slot=2,.count=5,.sides=6,.held_mask=3,
        .faces={1,2,3,4,5},.player_name="ALICE",.enabled=true};
    uint8_t b[48],packet[80];size_t n;
    assert(p4_dice_encode(&r,P4_DICE_READY,P4_DICE_REQUEST,b));
    p4_dice_request_t decoded={0};p4_dice_phase_t phase;
    assert(p4_dice_decode(b,sizeof(b),P4_DICE_REQUEST,&decoded,&phase));
    assert(decoded.token==123 && decoded.player_slot==2 && !strcmp(decoded.player_name,"ALICE"));
    assert(!p4_dice_decode(b,47,P4_DICE_REQUEST,&decoded,&phase));
    assert(!p4_dice_decode(b,48,P4_DICE_STATUS,&decoded,&phase));
    b[47]=1;assert(!p4_dice_decode(b,48,1,&decoded,&phase));b[47]=0;
    b[11]=128;assert(!p4_dice_decode(b,48,1,&decoded,&phase));b[11]=3;
    b[12]=7;assert(!p4_dice_decode(b,48,1,&decoded,&phase));b[12]=1;
    b[39]=65;assert(!p4_dice_decode(b,48,1,&decoded,&phase));b[39]=0;
    b[2]=255;assert(!p4_dice_decode(b,48,1,&decoded,&phase));b[2]=3;
    assert(p4_mp_packet_encode(P4_MP_PACKET_ACCESSORY,99,1,1,0,b,48,packet,80,&n)==P4_MP_OK && n==80);
    p4_mp_packet_view_t view;
    assert(p4_mp_packet_decode(packet,n,&view)==P4_MP_OK);
    packet[40]^=1;assert(p4_mp_packet_decode(packet,n,&view)==P4_MP_BAD_CRC);
    r.can_hold=true;
    assert(p4_dice_encode(&r,P4_DICE_WAITING,P4_DICE_STATUS,b));
    assert(p4_dice_decode(b,48,P4_DICE_STATUS,&decoded,&phase) && decoded.can_hold);
    p4_dice_status_t status;
    decoded.held_mask=0;decoded.hold_ack=1;
    assert(p4_dice_accept_status(&r,&decoded,P4_DICE_WAITING,&status) && status.hold_changed && !status.held_mask);
    assert(!p4_dice_accept_status(&r,&decoded,P4_DICE_READY,&status));
    decoded.held_mask=r.held_mask;
    assert(p4_dice_accept_status(&r,&decoded,P4_DICE_WAITING,&status) && status.hold_changed); // keep then undo
    decoded.held_mask=0;

    r.can_hold=false;decoded.can_hold=false;
    assert(!p4_dice_accept_status(&r,&decoded,P4_DICE_WAITING,&status));
    r.can_hold=decoded.can_hold=true;decoded.token++;
    assert(!p4_dice_accept_status(&r,&decoded,P4_DICE_WAITING,&status));
    decoded=r;decoded.faces[0]=6;
    assert(!p4_dice_accept_status(&r,&decoded,P4_DICE_WAITING,&status));
    decoded=r;decoded.held_mask=128;
    assert(!p4_dice_accept_status(&r,&decoded,P4_DICE_WAITING,&status));
    b[0]=1;assert(!p4_dice_decode(b,48,P4_DICE_STATUS,&decoded,&phase));
    p4_dice_gesture_t g; p4_dice_gesture_reset(&g,1);
    assert(!p4_dice_gesture_sample(&g,0,3000,0,0));
    assert(p4_dice_gesture_ready(&g,0));
    for (uint32_t t=0;t<1000;t+=10) assert(!p4_dice_gesture_sample(&g,t,0,0,1000));
    for (uint32_t t=1000;t<=1400;t+=200) {
        assert(!p4_dice_gesture_sample(&g,t,2500,0,0));
        assert(!p4_dice_gesture_sample(&g,t+100,0,0,1000));
    }
    assert(g.phase==P4_DICE_SHAKING && g.peaks==3);
    assert(p4_dice_gesture_sample(&g,1800,0,0,1000));
    assert(!p4_dice_gesture_sample(&g,1900,3000,0,0));
    assert(!p4_dice_gesture_ready(&g,2000));
    p4_dice_gesture_reset(&g,2);assert(g.phase==P4_DICE_WAITING);
    assert(p4_dice_gesture_ready(&g,0));
    assert(!p4_dice_gesture_sample(&g,16001,0,0,1000));assert(g.phase==P4_DICE_WAITING);
    assert(p4_dice_gesture_ready(&g,17000));
    assert(!p4_dice_gesture_sample(&g,17100,INT32_MAX,0,0));
    assert(!p4_dice_gesture_sample(&g,17200,2500,0,0));
    assert(!p4_dice_gesture_sample(&g,17900,0,0,1000));assert(g.phase!=P4_DICE_ROLLED);
    puts("dice codec / CRC / Ready / shake / settle / duplicate / timeout tests passed");
    return 0;
}
