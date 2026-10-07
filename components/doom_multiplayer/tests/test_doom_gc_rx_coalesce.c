// SPDX-License-Identifier: GPL-2.0-or-later
/* Actual adapter, session and lockstep. This proves policy/canonical parity,
 * not hardware performance or an RTOS scheduling bound. */
#define main retained_rejoin_main
#include "test_doom_gc_rejoin.c"
#undef main
static unsigned rx_calls, queued, next_frame, send_calls;
static bool drained, fail_tx;
static uint32_t incoming_mask=3;
static void bounded_poll(void *ctx)
{
    (void)ctx;++rx_calls;unsigned used=0;
    while(queued && used<8) {
        --queued;++used;
        p4_doom_lockstep_frame_t f={.tick=next_frame,.mask=(uint8_t)incoming_mask};
        for(unsigned i=0;i<2;++i) f.commands[i]=(p4_doom_mp_tic_t){
            .tick=next_frame,.forward_move=(int8_t)((next_frame+i)%31),
            .side_move=(int8_t)(i-2),.angle_turn=(int16_t)(next_frame*19U),
            .buttons=(uint8_t)(next_frame&3U),.consistency=(uint8_t)(next_frame*7U)};
        for (unsigned i=2;i<4;++i) f.commands[i].tick=next_frame;
        const ticcmd_t local={0};
        if (next_frame>=gc.sync.local.next_local_tick)
            p4_doom_gc_submit(&local,(int)next_frame);
        uint8_t b[P4_DOOM_LOCKSTEP_BYTES];
        assert(p4_doom_lockstep_frame_encode(&f,4,b));
        inject(1,100,P4_MP_PACKET_GAME_MESSAGE,b,sizeof(b),0);++next_frame;
    }
    drained=used<8;
}
static bool was_drained(void *ctx) { (void)ctx;return drained; }
static esp_err_t observed_send(void *ctx,uint64_t route,const uint8_t *b,size_t n)
{ ++send_calls;return fail_tx?ESP_FAIL:send_to(ctx,route,b,n); }
static void ready_role(bool client,bool framed)
{
    setup(client,false);configure();
    if(framed) { gc.config.resume_nonce=cfg.resume_nonce=123;
        if (!client) gc.resume_nonce[1]=123; }
    gc.transport.poll=bounded_poll;gc.transport.poll_drained=was_drained;
    gc.transport.send_to=observed_send;
    gc.rx_seen=gc.rx_drained=gc.rx_was_live=false;
    rx_calls=queued=send_calls=0;next_frame=gc.sync.next_output;
    drained=fail_tx=false;incoming_mask=3;
    memset(&gc.stats,0,sizeof(gc.stats));
}
static void ready(bool framed) { ready_role(true,framed); }
static void host_progress(bool framed)
{
    ready_role(false,framed);p4_doom_gc_poll();
    const ticcmd_t cmd={.forwardmove=7};p4_doom_gc_submit(&cmd,2);
    input(2,2,2);p4_doom_gc_poll_opportunistic();
    assert(rx_calls==1 && gc.stats.rx_poll_skips==1);
    assert(gc.sync.next_output==3 && received==3 && !gc.failed);
    assert(gc.sync.history[2].commands[0].forward_move==7);
    assert(gc.sync.history[2].commands[1].forward_move==12);
}
static void eligibility(bool framed)
{
    ready(framed);p4_doom_gc_poll_opportunistic();assert(rx_calls==1);
    p4_doom_gc_poll_opportunistic();assert(rx_calls==1 && gc.stats.rx_poll_skips==1);
    ++clock_ms;p4_doom_gc_poll_opportunistic();assert(rx_calls==1);
    ++clock_ms;p4_doom_gc_poll_opportunistic();assert(rx_calls==2);
    p4_doom_gc_poll();assert(rx_calls==3); /* Forced even at the identical clock. */
    --clock_ms;p4_doom_gc_poll_opportunistic();assert(rx_calls==4); /* rollback */
    gc.transport.poll_drained=NULL;p4_doom_gc_poll();
    p4_doom_gc_poll_opportunistic();assert(rx_calls==6 && gc.stats.rx_busy_polls==2);
    ready(framed);p4_doom_gc_poll();drained=false;
    p4_doom_gc_poll_opportunistic();assert(rx_calls==2);
    ready(framed);p4_doom_gc_poll();++clock_ms;gc.configured=false;
    p4_doom_gc_poll_opportunistic();assert(rx_calls==2);
    gc.configured=true;clock_ms+=100;p4_doom_gc_poll_opportunistic();
    assert(rx_calls==3 && gc.stats.rx_gap_max_us==0);
    ready(framed);p4_doom_gc_poll();++clock_ms;
    poll_runtime_mode(true,true);assert(rx_calls==2 && gc.local_loading);
    p4_doom_gc_poll_opportunistic();assert(rx_calls==3 && !gc.local_loading);
    ready(framed);p4_doom_gc_poll();++clock_ms;gc.loading_peers=1;
    gc.loading_since_ms[0]=gc.loading_seen_ms[0]=clock_ms;
    p4_doom_gc_poll_opportunistic();assert(rx_calls==2);
    ready(framed);p4_doom_gc_poll();++clock_ms;gc.replaying=true;
    gc.resume_started_ms[1]=clock_ms;p4_doom_gc_poll_opportunistic();assert(rx_calls==2);
    ready(framed);p4_doom_gc_poll();++clock_ms;gc.resuming=2;
    p4_doom_gc_poll_opportunistic();assert(rx_calls==2);
    ready(framed);p4_doom_gc_poll();++clock_ms;gc.fresh_pending=2;
    p4_doom_gc_poll_opportunistic();assert(rx_calls==2);
}
static void burst(bool framed,unsigned size)
{
    ready(framed);queued=size;p4_doom_gc_poll();
    while(queued) {unsigned before=rx_calls;p4_doom_gc_poll_opportunistic();assert(rx_calls==before+1);}
    if(!drained) {unsigned before=rx_calls;p4_doom_gc_poll_opportunistic();assert(rx_calls==before+1);}
    const unsigned before=rx_calls;p4_doom_gc_poll_opportunistic();assert(rx_calls==before);
    assert(gc.sync.next_output==2+size && received==2+size && !gc.failed);
    assert(gc.stats.rx_busy_polls==size/8);
}
static void progress_without_receive(bool framed)
{
    ready(framed);capacity=received-played;queued=1;p4_doom_gc_poll();
    assert(gc.sync.next_output==3 && gc.sync.next_read==2);
    capacity=128;p4_doom_gc_poll_opportunistic();
    assert(rx_calls==1 && gc.sync.next_read==3 && received==3);
    const ticcmd_t cmd={.forwardmove=17,.buttons=1};
    p4_doom_gc_submit(&cmd,3);gc.next_send_ms=0;send_calls=0;
    fail_tx=true;p4_doom_gc_poll_opportunistic();
    assert(rx_calls==1 && send_calls>=1 && gc.stats.tx_failures>=1);
    fail_tx=false;clock_ms+=20;send_calls=0;p4_doom_gc_poll_opportunistic();
    assert(rx_calls==2 && send_calls>=1 && !gc.failed);
    /* A fresh packet at a skipped tail is received at the next forced critical
     * call, before even a simulated 15 ms BSP or 6 ms palette span. */
    queued=1;const unsigned prior=rx_calls;
    p4_doom_gc_poll_opportunistic();assert(rx_calls==prior && queued==1);
    p4_doom_gc_poll();assert(rx_calls==prior+1 && queued==0);
    clock_ms+=15;p4_doom_gc_poll();clock_ms+=6;p4_doom_gc_poll();
    assert(gc.stats.rx_gap_max_us>=15000 && !gc.failed);
    /* Prepare closes the lifetime; no old drained receipt suppresses first RX. */
    ready(framed);p4_doom_gc_poll_opportunistic();assert(rx_calls==1);
}
static void parity(bool framed,bool hints,p4_doom_lockstep_t *result,unsigned *calls)
{
    ready(framed);const unsigned offsets[]={0,2,15,18,19,20,26,27,36};
    const uint64_t base=clock_ms;unsigned arrivals=2;
    for(unsigned draw=0;draw<4;++draw) {
        for(unsigned p=0;p<sizeof(offsets)/sizeof(offsets[0]);++p) {
            clock_ms=base+draw*36U+offsets[p];
            while(arrivals<2+(clock_ms-base)/29U) {++queued;++arrivals;}
            if(hints && (p==4 || p==7)) p4_doom_gc_poll_opportunistic();
            else p4_doom_gc_poll();
        }
        played=received;
    }
    *result=gc.sync;*calls=rx_calls;
    assert(!gc.failed && queued==0 && received==gc.sync.next_read);
    if(hints) assert(gc.stats.rx_poll_skips==8);
}
int main(void)
{
    for(unsigned framed=0;framed<2;++framed) {
        host_progress(framed!=0);eligibility(framed!=0);burst(framed!=0,9);burst(framed!=0,24);
        progress_without_receive(framed!=0);
        p4_doom_lockstep_t original,coalesced;unsigned before,after;
        parity(framed!=0,false,&original,&before);parity(framed!=0,true,&coalesced,&after);
        assert(!memcmp(&original,&coalesced,sizeof(original)) && before==36 && after==28);
        printf("PASS RX policy/parity %s: 36->28 transport calls; 9/24 bursts; forced phases; no hardware claim\n",framed?"GCE1":"raw");
    }
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
}
