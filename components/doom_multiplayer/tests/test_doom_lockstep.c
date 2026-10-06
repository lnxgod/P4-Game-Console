// SPDX-License-Identifier: MIT
#include "p4/doom_lockstep.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static p4_doom_lockstep_t nodes[4];
static p4_doom_lockstep_frame_t frames[4];
int main(void)
{
    for (uint8_t i=0; i<4; ++i) assert(p4_doom_lockstep_init(&nodes[i], i, 4));
    for (uint32_t tick=0; tick<1200; ++tick) {
        const uint8_t count = tick<800 ? 4 : 3;
        if (tick==800) assert(p4_doom_lockstep_depart(&nodes[0],3));
        for (uint8_t i=0; i<count; ++i) {
            p4_doom_mp_tic_t t = {.tick=tick,.forward_move=(int8_t)i,
                .side_move=-5,.angle_turn=-317,.buttons=(uint8_t)(tick%2),
                .consistency=(uint8_t)tick};
            assert(p4_doom_lockstep_submit(&nodes[i], &t));
            if (i) {
                assert(!p4_doom_lockstep_input(&nodes[i],1,&t,tick));
                /* Simulated loss: retransmit oldest command from actual queue. */
                p4_doom_mp_tic_t retry;
                assert(p4_doom_mp_tx_window_oldest(&nodes[i].local,&retry));
                assert(p4_doom_lockstep_input(&nodes[0],i,&retry,tick));
                assert(p4_doom_lockstep_input(&nodes[0],i,&retry,tick));
            }
        }
        p4_doom_lockstep_pump(&nodes[0]);
        assert(p4_doom_lockstep_pop(&nodes[0],&frames[0]));
        for (uint8_t i=1; i<count; ++i) {
            uint8_t packet[P4_DOOM_LOCKSTEP_BYTES], retry[P4_DOOM_LOCKSTEP_BYTES];
            assert(p4_doom_lockstep_packet(&nodes[0],i,packet));
            assert(p4_doom_lockstep_packet(&nodes[0],i,retry));
            assert(memcmp(packet,retry,sizeof(packet))==0);
            assert(!p4_doom_lockstep_receive(&nodes[i],packet,sizeof(packet)-1));
            retry[15]=1;
            assert(!p4_doom_lockstep_receive(&nodes[i],retry,sizeof(retry)));
            assert(p4_doom_lockstep_receive(&nodes[i],packet,sizeof(packet)));
            assert(!p4_doom_lockstep_receive(&nodes[i],packet,sizeof(packet)));
            assert(p4_doom_lockstep_pop(&nodes[i],&frames[i]));
            assert(frames[i].mask==frames[0].mask && frames[i].tick==tick);
            for (uint8_t j=0; j<4; ++j) {
                const p4_doom_mp_tic_t *a=&frames[i].commands[j], *b=&frames[0].commands[j];
                assert(a->forward_move==b->forward_move && a->side_move==b->side_move &&
                    a->angle_turn==b->angle_turn && a->buttons==b->buttons &&
                    a->consistency==b->consistency);
            }
            assert(!p4_doom_lockstep_ack(&nodes[0],i,tick+2));
            assert(p4_doom_lockstep_ack(&nodes[0],i,tick+1));
            assert(!p4_doom_lockstep_ack(&nodes[0],i,tick));
        }
    }
    /* Slow recipient holds history; no overwrite of unacknowledged frames. */
    assert(p4_doom_lockstep_init(&nodes[0],0,2));
    for (uint32_t tick=0; tick<40; ++tick) {
        p4_doom_mp_tic_t t={.tick=tick};
        assert(p4_doom_lockstep_submit(&nodes[0],&t));
        assert(p4_doom_lockstep_input(&nodes[0],1,&t,0));
        p4_doom_lockstep_pump(&nodes[0]);
        (void)p4_doom_lockstep_pop(&nodes[0],&frames[0]);
    }
    assert(nodes[0].next_output==32);
    assert(p4_doom_lockstep_ack(&nodes[0],1,32));
    p4_doom_lockstep_pump(&nodes[0]);
    assert(nodes[0].next_output==40);
    puts("four-player lockstep: command retry, malformed/replayed frames, departure and backpressure passed");
    return 0;
}
