// SPDX-License-Identifier: GPL-2.0-or-later
#define main rejoin_regressions_main
#include "test_doom_gc_rejoin.c"
#undef main

static unsigned input_count,canonical_count;
static uint32_t input_ticks[32],canonical_ticks[32];
static esp_err_t observe(void *ctx,uint64_t route,const uint8_t *wire,size_t n)
{
    (void)ctx;assert(route);
    p4_mp_packet_view_t outer,p;assert(p4_mp_packet_decode(wire,n,&outer)==P4_MP_OK);p=outer;
    const uint64_t nonce=host()?gc.resume_nonce[1]:gc.config.resume_nonce;
    if(nonce)assert(runtime_unwrap(&outer,nonce,&p));
    if(p.type==P4_MP_PACKET_INPUT) {
        p4_mp_input_t input;p4_doom_mp_tic_t tic;
        assert(input_count<32 && p4_mp_input_decode(p.payload,p.payload_length,&input)==P4_MP_OK);
        assert(p4_doom_mp_tic_from_input(&input,&tic));input_ticks[input_count++]=tic.tick;
    } else if(p.type==P4_MP_PACKET_GAME_MESSAGE && p.payload_length==P4_DOOM_LOCKSTEP_BYTES) {
        p4_doom_lockstep_frame_t f;assert(canonical_count<32);
        assert(p4_doom_lockstep_frame_decode(p.payload,p.payload_length,4,&f));canonical_ticks[canonical_count++]=f.tick;
    }
    return ESP_OK;
}
static void watch(void)
{ input_count=canonical_count=0;gc.transport.send_to=observe;gc.next_send_ms=0; }
static void guest_setup(bool replay)
{
    setup(true,replay);configure();
    const uint32_t first=replay?64:2;
    if(replay){assert(p4_doom_lockstep_prepare_live_input(&gc.sync,first));gc.activation[1]=first;}
    for(uint32_t t=first;t<first+6U;++t){const p4_doom_mp_tic_t tic={.tick=t};assert(p4_doom_lockstep_submit(&gc.sync,&tic));}
    watch();
}
static void guest_bounds(void)
{
    guest_setup(false);p4_doom_gc_poll();assert(input_count==3);
    for(unsigned i=0;i<3;++i)assert(input_ticks[i]==2U+i);
    assert(gc.sync.local.pending_count==6 && gc.sync.local.peer_ack==2);
    input_count=0;clock_ms+=19;p4_doom_gc_poll();assert(!input_count);
    ++clock_ms;p4_doom_gc_poll();assert(input_count==3 && input_ticks[0]==2);
    guest_setup(false);gc.configured=false;p4_doom_gc_poll();assert(input_count==1 && input_ticks[0]==2);
    guest_setup(true);p4_doom_gc_poll();assert(input_count==1 && input_ticks[0]==64 && gc.replaying);
    guest_setup(false);gc.sync.local.tags[3]=99;p4_doom_gc_poll();assert(input_count==1 && input_ticks[0]==2);
    guest_setup(false);gc.sync.local.tics[3].tick=99;p4_doom_gc_poll();assert(input_count==1 && input_ticks[0]==2);
    guest_setup(false);gc.sync.local.next_local_tick=3;p4_doom_gc_poll();assert(input_count==1 && input_ticks[0]==2);
    puts("PASS guest: oldest+two, unchanged retry interval, no retirement, startup/replay single-frame, tag/tic/upper bounds");
}
static void host_bounds(void)
{
    setup(false,false);configure();host_advance(8);gc.sync.peer_ack[1]=5;watch();
    p4_doom_gc_poll();assert(canonical_count==3);
    for(unsigned i=0;i<3;++i)assert(canonical_ticks[i]==5U+i);
    assert(gc.sync.peer_ack[1]==5 && gc.sync.next_output==8);
    canonical_count=0;clock_ms+=20;p4_doom_gc_poll();assert(canonical_count==3 && canonical_ticks[0]==5);
    gc.configured=false;canonical_count=0;clock_ms+=20;p4_doom_gc_poll();assert(canonical_count==1);
    gc.configured=true;gc.resuming=gc.resume_armed=2;gc.activation[1]=8;gc.replay_cursor[1]=8;
    canonical_count=0;clock_ms+=20;p4_doom_gc_poll();assert(canonical_count==1);
    puts("PASS host: oldest+two, unchanged ACK/history, startup and activation single-frame");
}
static void packet_api(void)
{
    p4_doom_lockstep_t s;assert(p4_doom_lockstep_init(&s,0,4));
    uint8_t a[P4_DOOM_LOCKSTEP_BYTES],b[P4_DOOM_LOCKSTEP_BYTES];
    for(uint32_t t=0;t<4;++t){
        const p4_doom_mp_tic_t tic={.tick=t};
        for(uint8_t slot=0;slot<4;++slot)assert(slot?p4_doom_lockstep_input(&s,slot,&tic,0):p4_doom_lockstep_submit(&s,&tic));
    }
    p4_doom_lockstep_pump(&s);assert(s.next_output==4);
    const p4_doom_lockstep_t unchanged=s;
    assert(p4_doom_lockstep_packet(&s,1,a) && p4_doom_lockstep_packet_at(&s,1,0,b) && !memcmp(a,b,sizeof(a)));
    for(uint32_t t=0;t<4;++t)assert(p4_doom_lockstep_packet_at(&s,1,t,b));
    assert(!memcmp(&s,&unchanged,sizeof(s)));
    assert(!p4_doom_lockstep_packet_at(NULL,1,0,b) && !p4_doom_lockstep_packet_at(&s,1,0,NULL));
    assert(!p4_doom_lockstep_packet_at(&s,0,0,b) && !p4_doom_lockstep_packet_at(&s,4,0,b));
    assert(!p4_doom_lockstep_packet(&s,255,b));
    assert(!p4_doom_lockstep_packet_at(&s,1,4,b) && !p4_doom_lockstep_packet_at(&s,1,UINT32_MAX,b));
    s.peer_ack[1]=2;assert(!p4_doom_lockstep_packet_at(&s,1,1,b) && p4_doom_lockstep_packet_at(&s,1,2,b));
    s.history[2].tick=34;assert(!p4_doom_lockstep_packet_at(&s,1,2,b));s=unchanged;
    s.next_output=33;assert(!p4_doom_lockstep_packet_at(&s,1,0,b));s=unchanged;
    s.mask=1;assert(!p4_doom_lockstep_packet_at(&s,1,0,b));s=unchanged;
    s.slot=1;assert(!p4_doom_lockstep_packet_at(&s,1,0,b));s=unchanged;
    s.count=255;assert(!p4_doom_lockstep_packet_at(&s,1,0,b));
    puts("PASS packet_at: wrapper equality, read-only, recipients, ACK/commit/retention bounds and exact tags");
}
int main(void)
{
    packet_api();guest_bounds();host_bounds();
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    return 0;
}
