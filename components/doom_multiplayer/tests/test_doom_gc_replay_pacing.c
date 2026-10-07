// SPDX-License-Identifier: GPL-2.0-or-later
/* Actual adapter scheduler/session/codecs and actual guest lockstep, with bounded
 * UDP queues. This is a deterministic transport model, not device throughput. */
#define main rejoin_regressions_main
#include "test_doom_gc_rejoin.c"
#undef main

enum { MAILBOX_MAX=32, SIM_MS=6000, BACKLOG=10000 };
typedef struct { uint8_t bytes[128];size_t n;uint8_t slot; } wire_t;
typedef struct { wire_t wire[MAILBOX_MAX];unsigned head,count; } mailbox_t;
static mailbox_t inbox[4];
static struct { p4_mp_session_t session;p4_doom_lockstep_t sync;uint32_t played,reported_cursor,reported_played;unsigned engine,reported_credit,report_due; } guest[4];
static unsigned mailbox_size,sim_peers,drops,loss_every,reorder_every,wire_serial;
static unsigned host_sends,max_host_sends,per_slot[4],sends[4],canonical_sends[4],control_sends[4];
static bool immediate_ready,coalesced_reports,sim_verify;
static void enqueue(unsigned destination,const wire_t *w)
{
    ++wire_serial;
    if ((loss_every && wire_serial%loss_every==0) || inbox[destination].count>=mailbox_size) { ++drops;return; }
    mailbox_t *q=&inbox[destination];
    const unsigned at=(q->head+q->count)%MAILBOX_MAX;
    q->wire[at]=*w;++q->count;
    if (reorder_every && wire_serial%reorder_every==0 && q->count>=2) {
        const unsigned previous=(at+MAILBOX_MAX-1U)%MAILBOX_MAX;
        wire_t old=q->wire[previous];q->wire[previous]=q->wire[at];q->wire[at]=old;
    }
}
static bool dequeue(unsigned source,wire_t *w)
{
    mailbox_t *q=&inbox[source];if (!q->count) return false;
    *w=q->wire[q->head];q->head=(q->head+1U)%MAILBOX_MAX;--q->count;return true;
}
static esp_err_t simulated_send(void *context,uint64_t route,const uint8_t *bytes,size_t n)
{
    (void)context;assert((route==2 || (route>=21 && route<=23)) && n<=sizeof(inbox[0].wire[0].bytes));
    wire_t w={.n=n,.slot=route==2?1:(uint8_t)(route-20U)};memcpy(w.bytes,bytes,n);
    p4_mp_packet_view_t outer,inner;
    assert(p4_mp_packet_decode(bytes,n,&outer)==P4_MP_OK);inner=outer;
    if (gc.resume_nonce[w.slot]) assert(runtime_unwrap(&outer,gc.resume_nonce[w.slot],&inner));
    if (inner.type==P4_MP_PACKET_GAME_MESSAGE && inner.payload_length==40) ++canonical_sends[w.slot];
    else ++control_sends[w.slot];
    ++host_sends;++per_slot[w.slot];++sends[w.slot];enqueue(w.slot,&w);return ESP_OK;
}
static void guest_send(unsigned slot,p4_mp_packet_type_t type,const uint8_t *payload,uint16_t n)
{
    uint8_t wrapped[80]={'G','C','E','1'};assert(n<=sizeof(wrapped)-16);
    const uint64_t nonce=gc.resume_nonce[slot];
    for (unsigned i=0;i<8;++i) wrapped[4U+i]=(uint8_t)(nonce>>(i*8U));
    wrapped[12]=(uint8_t)type;memcpy(wrapped+16,payload,n);
    wire_t w={.slot=(uint8_t)slot};
    assert(p4_mp_session_encode(&guest[slot].session,P4_MP_PACKET_GAME_MESSAGE,
        guest[slot].sync.next_output,wrapped,(uint16_t)(n+16U),w.bytes,sizeof(w.bytes),&w.n)==P4_MP_OK);
    enqueue(0,&w);
}
static void guest_ready(unsigned slot)
{ guest_send(slot,P4_MP_PACKET_PING,(const uint8_t *)"GCAREADY",8); }
static void simulated_poll(void *context)
{
    (void)context;wire_t w;
    for (unsigned i=0;i<8 && dequeue(0,&w);++i)
        receive_frame(NULL,20U+w.slot,w.bytes,w.n);
}
static void guest_poll(unsigned slot,unsigned elapsed)
{
    wire_t w;
    for (unsigned i=0;i<8 && dequeue(slot,&w);++i) {
        p4_mp_event_t event;
        if (p4_mp_session_receive(&guest[slot].session,1,clock_ms,w.bytes,w.n,&event)!=P4_MP_OK) continue;
        p4_mp_packet_view_t inner;
        assert(runtime_unwrap(&event.packet,gc.resume_nonce[slot],&inner));
        if (inner.type==P4_MP_PACKET_GAME_MESSAGE && inner.payload_length==40 &&
            p4_doom_lockstep_receive_replay(&guest[slot].sync,inner.payload,inner.payload_length) && immediate_ready)
            guest_ready(slot);
    }
    p4_doom_lockstep_frame_t frame;
    while (guest[slot].engine<128 && p4_doom_lockstep_pop(&guest[slot].sync,&frame)) {
        assert(frame.tick==guest[slot].played+guest[slot].engine);++guest[slot].engine;
    }
    /* A 400 ms engine pause proves credit reaches zero and later recovers. */
    if (elapsed<1000 || elapsed>=1400) for (unsigned i=0;i<2 && guest[slot].engine;++i) {
        --guest[slot].engine;++guest[slot].played;
    }
    assert(guest[slot].engine<=128 && guest[slot].sync.next_output-guest[slot].sync.next_read<=32);
    if (elapsed%20U==0) {
        unsigned credit=32U-(guest[slot].sync.next_output-guest[slot].sync.next_read);
        const unsigned engine_credit=128U-guest[slot].engine;
        if (credit>engine_credit) credit=engine_credit;
        uint8_t b[24]={'G','C','P','1',1,(uint8_t)slot,(uint8_t)credit,0};
        put32(b+8,guest[slot].sync.next_output);put32(b+12,guest[slot].played);
        if (!coalesced_reports || guest[slot].reported_cursor!=guest[slot].sync.next_output ||
            guest[slot].reported_played!=guest[slot].played || guest[slot].reported_credit!=credit ||
            elapsed>=guest[slot].report_due) {
            guest_send(slot,P4_MP_PACKET_GAME_MESSAGE,b,sizeof(b));
            guest[slot].reported_cursor=guest[slot].sync.next_output;guest[slot].reported_played=guest[slot].played;
            guest[slot].reported_credit=credit;guest[slot].report_due=elapsed+100;
        }
        if (immediate_ready) guest_ready(slot);
    }
}
static bool observed_immediate_ack(void)
{
    setup(true,true);configure();snapshots=0;
    for (uint32_t t=0;t<16;++t) canonical(t,1);
    const bool immediate=snapshots!=0;
    if (sim_verify) assert(!immediate);
    /* A scheduled progress report remains available even with zero engine credit. */
    capacity=0;const unsigned before=controls;tick();assert(controls==before+1);
    tick();coalesced_reports=controls==before+1;
    if (sim_verify) {
        assert(coalesced_reports);
        for (unsigned i=0;i<3;++i) { tick();assert(controls==before+1); }
        tick();assert(controls==before+2); /* bounded retry of an unchanged/lost report */
    }
    return immediate;
}
static unsigned run_case(unsigned peers,unsigned poll_ms,unsigned queue,unsigned loss,unsigned reorder)
{
    immediate_ready=observed_immediate_ack();setup(false,false);configure();leave();host_advance(BACKLOG);
    memset(inbox,0,sizeof(inbox));memset(guest,0,sizeof(guest));memset(sends,0,sizeof(sends));
    sim_peers=peers;mailbox_size=queue;drops=wire_serial=max_host_sends=0;loss_every=loss;reorder_every=reorder;
    gc.transport.send_to=simulated_send;gc.transport.poll=simulated_poll;
    for (unsigned i=1;i<=peers;++i) {
        const uint8_t slot=(uint8_t)i;
        assert(p4_mp_session_accept_peer(&session,100U+i,20U+i,slot,1,clock_ms)==P4_MP_OK);
        begin_replay(slot,20U+i,200U+i);gc.replay_credit[i]=32;
        assert(p4_mp_session_client_start(&guest[i].session,7,100U+i,100,1,clock_ms,60000)==P4_MP_OK);
        assert(p4_mp_session_accept_host(&guest[i].session,100,1,1,clock_ms)==P4_MP_OK);
        assert(p4_doom_lockstep_init(&guest[i].sync,slot,4));assert(p4_doom_lockstep_replay_begin(&guest[i].sync));
    }
    const uint64_t started=clock_ms;uint32_t live=0;
    for (unsigned elapsed=0;elapsed<SIM_MS;++elapsed) {
        clock_ms=started+elapsed;
        if (elapsed%poll_ms==0) {
            /* Host world keeps accumulating canonical commands at exactly 35 Hz. */
            const uint32_t due=(uint32_t)(elapsed*35U/1000U);
            while (live<due) {
                const p4_doom_mp_tic_t tic={.tick=BACKLOG+live};
                assert(p4_doom_lockstep_submit(&gc.sync,&tic));++live;
            }
            host_sends=0;memset(per_slot,0,sizeof(per_slot));p4_doom_gc_poll();
            if (host_sends>max_host_sends) max_host_sends=host_sends;
            if (sim_verify) assert(host_sends<=16);
        }
        for (unsigned i=1;i<=peers;++i) guest_poll(i,elapsed);
        assert(!gc.failed && gc.sync.mask==1 && gc.resuming==(uint8_t)((1U<<(peers+1U))-2U));
    }
    unsigned minimum=UINT_MAX,maximum=0;
    for (unsigned i=1;i<=peers;++i) {
        if (guest[i].played<minimum) minimum=guest[i].played;
        if (guest[i].played>maximum) maximum=guest[i].played;
        
    }
    assert(gc.journal.next_tick==BACKLOG+live && live>=207);
    printf("peers=%u host_ms=%u mailbox=%u loss=%u reorder=%u unique_min=%u unique_max=%u drops=%u max_host_batch=%u immediate_ready=%u\n",
        peers,poll_ms,queue,loss,reorder,minimum,maximum,drops,max_host_sends,immediate_ready);
    if (sim_verify && queue==32) {
        assert(minimum>SIM_MS*35U/1000U);
        if (!loss && !reorder) assert(maximum-minimum<=16);
    }
    return minimum;
}
static void priority_and_credit(void)
{
    setup(false,false);configure();host_advance(80);
    memset(inbox,0,sizeof(inbox));mailbox_size=32;loss_every=reorder_every=0;
    gc.transport.send_to=simulated_send;gc.transport.poll=simulated_poll;
    for (uint8_t slot=2;slot<4;++slot) {
        assert(p4_mp_session_accept_peer(&session,100U+slot,20U+slot,slot,1,clock_ms)==P4_MP_OK);
        begin_replay(slot,20U+slot,200U+slot);gc.replay_credit[slot]=32;
    }
    for (unsigned phase=0;phase<3;++phase) {
        memset(inbox,0,sizeof(inbox));memset(canonical_sends,0,sizeof(canonical_sends));
        memset(control_sends,0,sizeof(control_sends));host_sends=0;
        if (phase) gc.activation[2]=gc.activation[3]=144;
        if (phase==2) gc.replay_credit[2]=0;
        gc.next_keepalive_ms=gc.next_send_ms=0;p4_doom_gc_poll();
        assert(host_sends<=16 && canonical_sends[1]==1 && control_sends[1]==1);
        assert(control_sends[2]==(phase?2U:1U) && control_sends[3]==control_sends[2]);
        if (phase==2) assert(canonical_sends[2]==0 && canonical_sends[3]==10);
        else assert(canonical_sends[2]==canonical_sends[3] && canonical_sends[2]==(phase?5U:6U));
    }
    puts("PASS: live frame/keepalive and activation controls precede fair replay; zero credit receives no replay");
}
int main(int argc,char **argv)
{
    sim_verify=argc<2 || strcmp(argv[1],"--baseline")!=0;
    if (sim_verify) priority_and_credit();
    for (unsigned peers=1;peers<=3;peers+=2) for (unsigned poll_ms=20;poll_ms<=60;poll_ms+=40) {
        const unsigned small=run_case(peers,poll_ms,6,0,0);
        const unsigned large=run_case(peers,poll_ms,32,0,0);
        if (sim_verify) { assert(large>=small);if (peers==1) assert(large>small); }
        (void)run_case(peers,poll_ms,32,31,47);
    }
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts(sim_verify?"PASS: bounded replay send budget, guest credit/queues, 35Hz continuing host, loss/reorder and 1/3-guest progress":"BASELINE measurements only; candidate budget/ACK assertions disabled");
    return 0;
}
