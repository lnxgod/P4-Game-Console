// SPDX-License-Identifier: GPL-2.0-or-later
/* Actual adapter/checkpoint/session scheduler. Engine/transport are the existing
 * bounded checkpoint fixture; snapshots are synthetic, not Doom world proof. */
#define main checkpoint_regression_original_main
#include "test_doom_gc_checkpoint.c"
#undef main

static p4_ct_rx readers[4];
static uint8_t stores[4][FIXTURE_BYTES];
static void transfer_selected(uint8_t slots)
{
    for (size_t i=0;i<output_count;++i) {
        p4_mp_packet_view_t p;assert(p4_mp_packet_decode(output[i].bytes,output[i].length,&p)==P4_MP_OK);
        if (p.type!=P4_MP_PACKET_CHECKPOINT) continue;
        const unsigned slot=(unsigned)(output[i].route-21U);assert(slot>0 && slot<4);
        if (!(slots & (1U<<slot))) continue;
        p4_ct_meta m;
        if (p4_ct_meta_decode(&m,p.payload,p.payload_length)) {
            assert(m.identity.attempt==34U+slot && m.members==1);
            if (!readers[slot].active) assert(p4_ct_rx_start(&readers[slot],&m.identity,&m,
                stores[slot],FIXTURE_BYTES,verify,NULL,clock_ms));
        } else {
            assert(readers[slot].active);
            assert(p4_ct_rx_chunk(&readers[slot],p.payload,p.payload_length,clock_ms)!=P4_CT_REJECT);
        }
        host_receive_ack((uint8_t)slot,&readers[slot]);
    }
    clear_output();
}
static void finish_transfer(uint8_t slots)
{
    for (unsigned step=0;step<300;++step) {
        bool done=true;
        for (uint8_t i=1;i<4;++i) if ((slots & (1U<<i)) && !p4_cs_complete(&gc.checkpoints,i)) done=false;
        if (done) return;
        clear_output();tick();assert(output_count<=P4_DOOM_GC_HOST_SEND_BUDGET);transfer_selected(slots);
        assert(!gc.failed);
    }
    assert(false);
}
static void advance_live(uint32_t end,uint32_t ack)
{
    while (received<end) {
        const uint32_t t=received;
        for (uint8_t i=1;i<4;++i) if (gc.sync.mask & (1U<<i)) host_input(i,t,ack==UINT32_MAX?t:ack);
        clear_output();const ticcmd_t cmd={0};p4_doom_gc_submit(&cmd,(int)t);tick();
        if (gc.failed || received!=t+1 || output_count>P4_DOOM_GC_HOST_SEND_BUDGET) fprintf(stderr,"ADVANCE t=%u end=%u received=%u failed=%d mask=%u pending=%u output=%zu ack=%u pivot1=%u pivot2=%u pivot3=%u\n",t,end,received,gc.failed,gc.sync.mask,gc.sync.pending_mask,output_count,ack,gc.activation[1],gc.activation[2],gc.activation[3]);
        assert(!gc.failed && received==t+1 && output_count<=P4_DOOM_GC_HOST_SEND_BUDGET);
    }
}
static void activate(uint8_t slot)
{
    const uint32_t at=played;control(slot,1,at,at,16);
    const uint32_t pivot=gc.activation[slot];assert(pivot==at+P4_DOOM_GC_REPLAY_PIVOT_AHEAD);
    host_input(slot,pivot,pivot);assert(gc.resume_armed & (1U<<slot));
    advance_live(pivot,UINT32_MAX);control(slot,1,pivot,pivot,16);
    assert(!(gc.resuming & (1U<<slot)) && !p4_cs_meta(&gc.checkpoints,slot));
    advance_live(pivot+1,pivot);assert(gc.sync.mask & (1U<<slot));
}
struct pass_counts { unsigned canonical[4],keepalive[4],checkpoint[4],control[4];uint32_t first[4],last[4]; };
static struct pass_counts count_pass(void)
{
    struct pass_counts c={0};assert(output_count<=P4_DOOM_GC_HOST_SEND_BUDGET);
    for (size_t i=0;i<output_count;++i) {
        const unsigned slot=(unsigned)(output[i].route-21U);assert(slot>0 && slot<4);
        p4_mp_packet_view_t p;assert(p4_mp_packet_decode(output[i].bytes,output[i].length,&p)==P4_MP_OK);
        if (p.type==P4_MP_PACKET_CHECKPOINT) {++c.checkpoint[slot];continue;}
        unwrap(&p);
        if (p.type==P4_MP_PACKET_PING) {++c.keepalive[slot];continue;}
        assert(p.type==P4_MP_PACKET_GAME_MESSAGE);
        p4_doom_lockstep_frame_t f;
        if (p4_doom_lockstep_frame_decode(p.payload,p.payload_length,4,&f)) {
            if (!c.canonical[slot]) c.first[slot]=f.tick;
            else assert(f.tick==c.last[slot]+1);
            c.last[slot]=f.tick;++c.canonical[slot];
        } else { assert(p.payload_length==24 && !memcmp(p.payload,"GCP1",4));++c.control[slot]; }
    }
    return c;
}
static struct pass_counts forced_pass(void)
{
    clear_output();gc.next_keepalive_ms=0;tick();return count_pass();
}
int main(void)
{
    setup(false);host_advance(96);
    memset(readers,0,sizeof(readers));
    for (uint8_t i=1;i<4;++i) {join(i);control(i,4,0,0,0);}
    assert(!boundary() && captures==1);
    finish_transfer(10); /* Real transfers finish seats 1 and 3, seat 2 stays pending. */
    for (uint8_t i=1;i<4;i+=2) {assert(readers[i].verified);control(i,5,96,96,0);}
    assert(gc.checkpoint_restored==10 && !p4_cs_complete(&gc.checkpoints,2));
    activate(1);
    const uint32_t held_ack=played;advance_live(played+12,held_ack);
    control(3,1,96,96,32);assert(gc.replay_credit[3]==32 && !gc.activation[3]);
    unsigned checkpoint_sends=0,replay_sends=0;
    for (unsigned pass=0;pass<16;++pass) {
        const struct pass_counts c=forced_pass();
        assert(c.canonical[1]==3 && c.first[1]==held_ack);
        for (unsigned i=1;i<4;++i) assert(c.keepalive[i]==1);
        assert(!c.canonical[2] && !c.checkpoint[1] && !c.checkpoint[3]);
        assert(c.checkpoint[2]<=P4_CT_SEND_BUDGET && c.canonical[3]>0 && c.first[3]==96);
        checkpoint_sends+=c.checkpoint[2];replay_sends+=c.canonical[3];
        assert(!gc.failed && gc.resuming==12);
    }
    assert(checkpoint_sends && replay_sends);
    control(3,1,96,96,0);const struct pass_counts zero=forced_pass();assert(!zero.canonical[3]);
    control(3,1,96,96,32);
    finish_transfer(4);assert(readers[2].verified);control(2,5,96,96,0);
    /* Two replay peers share the remaining send budget without starving either. */
    control(2,1,96,96,32);
    for (unsigned pass=0;pass<8;++pass) {
        const struct pass_counts c=forced_pass();
        assert(output_count==P4_DOOM_GC_HOST_SEND_BUDGET);
        assert(c.canonical[1]==3 && c.canonical[2]==5 && c.canonical[3]==5);
        assert(c.first[2]==96 && c.first[3]==96);
    }
    /* Restored peers announce their consumed cursor; activation itself still
     * uses the real control/input/pivot path and never rewinds the host. */
    activate(2);activate(3);assert(gc.sync.mask==15 && !gc.resuming);
    const uint32_t all_ack=played;advance_live(played+3,all_ack);
    const struct pass_counts live=forced_pass();
    assert(output_count==12);
    for (unsigned i=1;i<4;++i) {
        assert(live.canonical[i]==3 && live.first[i]==all_ack && live.last[i]==all_ack+2);
        assert(live.keepalive[i]==1 && !live.checkpoint[i] && !live.control[i]);
    }
    assert(!gc.failed && !restores && !engine_rebases);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK && !live_allocations);
    puts("PASS actual fresh admission/checkpoint/activation; mixed 4-slot live + transfer + replay <=16, zero credit, two-peer replay fairness, all-live oldest-first 12-send pass");
    return 0;
}
