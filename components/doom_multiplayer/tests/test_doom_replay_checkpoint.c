// SPDX-License-Identifier: MIT
#include "p4/doom_replay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static p4_doom_lockstep_t guest, before, expected;

static p4_doom_lockstep_frame_t canonical(uint32_t tick, uint8_t members)
{
    p4_doom_lockstep_frame_t frame = {.tick=tick, .mask=members};
    for(uint8_t slot=0;slot<P4_MP_MAX_PLAYERS;++slot) {
        frame.commands[slot].tick=tick;
        if(members & (1U << slot)) {
            frame.commands[slot].forward_move=(int8_t)(tick%100U);
            frame.commands[slot].side_move=(int8_t)-(int8_t)slot;
            frame.commands[slot].consistency=(uint8_t)((tick+slot)%256U);
        }
    }
    return frame;
}

static void packet(uint32_t tick, uint8_t members, uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES])
{
    p4_doom_lockstep_frame_t frame=canonical(tick,members);
    assert(p4_doom_lockstep_frame_encode(&frame,4,bytes));
}

static void begin(void)
{
    assert(p4_doom_lockstep_init(&guest,3,4));
    assert(p4_doom_lockstep_replay_begin(&guest));
}

static void reject_unchanged(uint32_t tick, uint8_t members)
{
    memcpy(&before,&guest,sizeof(before));
    assert(!p4_doom_lockstep_replay_checkpoint(&guest,tick,members));
    assert(memcmp(&before,&guest,sizeof(before))==0);
}

static void malformed_and_live_state_rejections(void)
{
    assert(!p4_doom_lockstep_replay_checkpoint(NULL,1000,1));
    for(unsigned variant=0;variant<12;++variant) {
        begin();
        uint32_t tick=1000; uint8_t members=1;
        if(variant==0)guest.count=0;
        if(variant==1)guest.count=1;
        if(variant==2)guest.count=5;
        if(variant==3)guest.slot=0;
        if(variant==4)guest.slot=4;
        if(variant==5)guest.replaying=false;
        if(variant==6)members=0;
        if(variant==7)members=2;
        if(variant==8)members=0x11;
        if(variant==9)tick=UINT32_MAX;
        if(variant==10)guest.local.pending_count=1;
        if(variant==11)guest.live_input_prepared=true;
        reject_unchanged(tick,members);
    }
    assert(p4_doom_lockstep_init(&guest,0,4));
    reject_unchanged(1000,1); /* A host can never be rewound. */
    assert(p4_doom_lockstep_init(&guest,3,4));
    reject_unchanged(1000,1); /* Explicit replay_begin is mandatory. */
    p4_doom_mp_tic_t local={.tick=0};
    assert(p4_doom_lockstep_submit(&guest,&local));
    reject_unchanged(1000,1); /* Live pending input is retained intact. */
    begin();
    assert(p4_doom_lockstep_prepare_live_input(&guest,2));
    reject_unchanged(1000,1); /* Prepared empty TX frontier cannot be reset. */
    local.tick=2;
    assert(p4_doom_lockstep_submit(&guest,&local));
    reject_unchanged(1000,1); /* Neither can prepared pending TX. */
}

static void exact_clean_rebase_and_membership(void)
{
    for(uint8_t count=2;count<=P4_MP_MAX_PLAYERS;++count) {
        for(uint8_t slot=1;slot<count;++slot) {
            for(uint8_t members=1;members<(1U<<count);members=(uint8_t)(members+2U)) {
                assert(p4_doom_lockstep_init(&guest,slot,count));
                assert(p4_doom_lockstep_replay_begin(&guest));
                assert(p4_doom_lockstep_replay_checkpoint(&guest,12345,members));
                assert(p4_doom_lockstep_init(&expected,slot,count));
                expected.next_output=expected.next_read=12345;
                expected.input.next_tick=12345;
                expected.input.connected_mask=expected.mask=members;
                p4_doom_mp_tx_window_init(&expected.local,12345);
                expected.replaying=true;
                assert(memcmp(&guest,&expected,sizeof(guest))==0);
                p4_doom_lockstep_frame_t out;
                assert(!p4_doom_lockstep_pop(&guest,&out));
                p4_doom_mp_tic_t local={.tick=12345};
                assert(!p4_doom_lockstep_submit(&guest,&local));
            }
        }
    }
}

static void explicit_replay_replaces_old_history(void)
{
    begin();
    uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES];
    for(uint32_t tick=0;tick<4;++tick) {
        packet(tick,3,bytes);
        assert(p4_doom_lockstep_receive_replay(&guest,bytes,sizeof(bytes)));
    }
    assert(guest.next_output==4 && guest.next_read==0);
    assert(p4_doom_lockstep_replay_checkpoint(&guest,2000,1));
    assert(guest.next_output==2000 && guest.next_read==2000 && guest.mask==1);
    p4_doom_lockstep_frame_t out;
    assert(!p4_doom_lockstep_pop(&guest,&out));
    assert(!p4_doom_lockstep_receive_replay(&guest,bytes,sizeof(bytes)));
    packet(2000,1,bytes);
    assert(p4_doom_lockstep_receive_replay(&guest,bytes,sizeof(bytes)));
    assert(p4_doom_lockstep_pop(&guest,&out) && out.tick==2000 && out.mask==1);
    /* An authenticated replacement can reset an explicit replay, including
     * backwards. A live path never has permission to use this API. */
    assert(p4_doom_lockstep_replay_checkpoint(&guest,1000,3));
    assert(guest.next_output==1000 && guest.next_read==1000);
    packet(1000,3,bytes);
    assert(p4_doom_lockstep_receive_replay(&guest,bytes,sizeof(bytes)));
    assert(p4_doom_lockstep_pop(&guest,&out) && out.tick==1000 && out.mask==3);
}

static void suffix_replay_preserves_prepared_local_frontier(void)
{
    enum { SNAPSHOT=1200, ACTIVATION=1252 };
    uint8_t storage[80*P4_DOOM_LOCKSTEP_BYTES], bytes[P4_DOOM_LOCKSTEP_BYTES];
    p4_doom_replay_journal_t journal;
    assert(p4_doom_replay_journal_init_rolling(&journal,storage,sizeof(storage),4));
    for(uint32_t tick=0;tick<ACTIVATION;++tick) {
        const uint8_t members=tick<1220?3:1;
        p4_doom_lockstep_frame_t frame=canonical(tick,members);
        assert(p4_doom_replay_journal_append(&journal,&frame));
    }
    assert(journal.first_tick==1172 && journal.next_tick==ACTIVATION);
    begin();
    /* Fresh slot 3 did not exist in this snapshot. */
    assert(p4_doom_lockstep_replay_checkpoint(&guest,SNAPSHOT,3));
    assert(!(guest.mask & (1U<<guest.slot)) && guest.input.connected_mask==3);
    assert(guest.local.next_local_tick==SNAPSHOT && guest.local.peer_ack==SNAPSHOT);
    assert(p4_doom_replay_journal_packet(&journal,SNAPSHOT+1,bytes));
    memcpy(&before,&guest,sizeof(before));
    assert(!p4_doom_lockstep_receive_replay(&guest,bytes,sizeof(bytes)));
    assert(memcmp(&guest,&before,sizeof(guest))==0); /* Out of order is atomic. */
    for(uint32_t tick=SNAPSHOT;tick<1250;++tick) {
        assert(p4_doom_replay_journal_packet(&journal,tick,bytes));
        assert(!p4_doom_lockstep_receive(&guest,bytes,sizeof(bytes)));
        assert(p4_doom_lockstep_receive_replay(&guest,bytes,sizeof(bytes)));
        assert(!p4_doom_lockstep_receive_replay(&guest,bytes,sizeof(bytes))); /* Lost ACK retry. */
        p4_doom_lockstep_frame_t out;
        assert(p4_doom_lockstep_pop(&guest,&out) && out.tick==tick);
        assert(out.mask==(tick<1220?3:1));
        assert(guest.local.next_local_tick==SNAPSHOT && guest.local.pending_count==0);
    }
    assert(!p4_doom_lockstep_prepare_live_input(&guest,1249));
    assert(p4_doom_lockstep_prepare_live_input(&guest,ACTIVATION));
    for(uint32_t tick=ACTIVATION;tick<ACTIVATION+2;++tick) {
        p4_doom_mp_tic_t local={.tick=tick,.forward_move=17,.consistency=42};
        assert(p4_doom_lockstep_submit(&guest,&local));
    }
    assert(guest.local.next_local_tick==ACTIVATION+2 && guest.local.pending_count==2);
    reject_unchanged(SNAPSHOT,3);
    assert(p4_doom_lockstep_prepare_live_input(&guest,ACTIVATION));
    assert(!p4_doom_lockstep_prepare_live_input(&guest,ACTIVATION+1));
    for(uint32_t tick=1250;tick<ACTIVATION;++tick) {
        assert(p4_doom_replay_journal_packet(&journal,tick,bytes));
        assert(p4_doom_lockstep_receive_replay(&guest,bytes,sizeof(bytes)));
        p4_doom_lockstep_frame_t out;
        assert(p4_doom_lockstep_pop(&guest,&out) && out.tick==tick);
        assert(guest.local.next_local_tick==ACTIVATION+2 && guest.local.pending_count==2);
    }
    assert(p4_doom_lockstep_replay_finish(&guest,ACTIVATION));
    assert(!guest.replaying && !guest.live_input_prepared);
    assert(guest.local.next_local_tick==ACTIVATION+2 && guest.local.pending_count==2);
    reject_unchanged(SNAPSHOT,3);
    for(uint32_t tick=ACTIVATION;tick<ACTIVATION+2;++tick) {
        packet(tick,9,bytes);
        assert(!p4_doom_lockstep_receive_replay(&guest,bytes,sizeof(bytes)));
        assert(p4_doom_lockstep_receive(&guest,bytes,sizeof(bytes)));
        p4_doom_lockstep_frame_t out;
        assert(p4_doom_lockstep_pop(&guest,&out) && out.tick==tick && out.mask==9);
        assert(guest.local.next_local_tick==ACTIVATION+2);
    }
    assert(!guest.local.pending_count && guest.local.peer_ack==ACTIVATION+2);
}

static void terminal_counter(void)
{
    begin();
    assert(p4_doom_lockstep_replay_checkpoint(&guest,UINT32_MAX-1,1));
    uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES];
    packet(UINT32_MAX-1,1,bytes);
    assert(p4_doom_lockstep_receive_replay(&guest,bytes,sizeof(bytes)));
    p4_doom_lockstep_frame_t out;
    assert(p4_doom_lockstep_pop(&guest,&out) && out.tick==UINT32_MAX-1);
    assert(guest.next_output==UINT32_MAX && guest.next_read==UINT32_MAX);
    assert(!p4_doom_lockstep_prepare_live_input(&guest,UINT32_MAX));
    assert(!p4_doom_lockstep_replay_finish(&guest,UINT32_MAX));
    reject_unchanged(UINT32_MAX,1);
}

int main(void)
{
    malformed_and_live_state_rejections();
    exact_clean_rebase_and_membership();
    explicit_replay_replaces_old_history();
    suffix_replay_preserves_prepared_local_frontier();
    terminal_counter();
    puts("checkpoint rebase: guest-only explicit replay, atomic rejections, clean nonzero roster restore, virgin absent slot, suffix/live frontier and counter limit passed");
    return 0;
}
