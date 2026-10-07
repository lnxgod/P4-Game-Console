// SPDX-License-Identifier: GPL-2.0-or-later
/* Real adapter, codecs and session; virtual transport records every attempted
 * datagram. This proves duplicate removal and retry semantics, not device FPS. */
#define main rejoin_regressions_main
#include "test_doom_gc_rejoin.c"
#undef main

static unsigned queued_frames, ready_attempts, ready_successes, input_attempts;
static unsigned fail_ready_at;
static uint32_t ready_acks[16];
static bool duplicate_frame;

static esp_err_t observe_send(void *ctx,uint64_t route,const uint8_t *wire,size_t n)
{
    p4_mp_packet_view_t outer,p;
    assert(p4_mp_packet_decode(wire,n,&outer)==P4_MP_OK);p=outer;
    if (gc.config.resume_nonce) assert(runtime_unwrap(&outer,gc.config.resume_nonce,&p));
    if (p.type==P4_MP_PACKET_PING && p.payload_length==8 && !memcmp(p.payload,"GCAREADY",8)) {
        assert(ready_attempts<16);ready_acks[ready_attempts++]=p.ack;
        if (ready_attempts==fail_ready_at) return ESP_FAIL;
        ++ready_successes;
    } else if (p.type==P4_MP_PACKET_INPUT) ++input_attempts;
    return send_to(ctx,route,wire,n);
}
static void receive_queued(void *ctx)
{
    (void)ctx;
    while (queued_frames) {
        --queued_frames;
        canonical(duplicate_frame?gc.sync.next_output-1U:gc.sync.next_output,3);
    }
}
static void reset_observations(void)
{
    queued_frames=ready_attempts=ready_successes=input_attempts=fail_ready_at=0;
    duplicate_frame=false;memset(ready_acks,0,sizeof(ready_acks));
}
static void prepare_live(bool framed)
{
    setup(true,false);configure();assert(gc.configured && gc.sync.next_output==2);
    if (framed) gc.config.resume_nonce=cfg.resume_nonce=123;
    for (uint32_t t=2;t<6;++t) {
        const p4_doom_mp_tic_t tic={.tick=t};assert(p4_doom_lockstep_submit(&gc.sync,&tic));
    }
    gc.transport.send_to=observe_send;gc.transport.poll=receive_queued;
    reset_observations();gc.next_send_ms=0;
}
static void run_contract(bool framed)
{
    prepare_live(framed);queued_frames=1;p4_doom_gc_poll();
    assert(gc.sync.next_output==3 && ready_attempts==1 && ready_successes==1 && ready_acks[0]==3);
    /* An empty later poll still retries at the original 20 ms interval. */
    reset_observations();clock_ms+=19;p4_doom_gc_poll();assert(ready_attempts==0);
    ++clock_ms;p4_doom_gc_poll();assert(ready_attempts==1 && ready_acks[0]==3);
    assert(!gc.failed && gc.sync.mask==3);

    prepare_live(framed);queued_frames=2;p4_doom_gc_poll();
    assert(ready_attempts==2 && ready_acks[0]==3 && ready_acks[1]==4);
    assert(gc.sync.next_output==4);

    /* Failed most-recent cursor never borrows success from an older cursor. */
    prepare_live(framed);queued_frames=2;fail_ready_at=2;p4_doom_gc_poll();
    assert(ready_attempts==3 && ready_successes==2);
    assert(ready_acks[0]==3 && ready_acks[1]==4 && ready_acks[2]==4);
    assert(gc.stats.tx_failures==1);

    prepare_live(framed);queued_frames=1;fail_ready_at=1;p4_doom_gc_poll();
    assert(ready_attempts==2 && ready_successes==1 && ready_acks[0]==3 && ready_acks[1]==3);
    assert(gc.stats.tx_failures==1);

    /* A callback outside this poll cannot suppress its periodic retry. */
    prepare_live(framed);canonical(2,3);assert(ready_attempts==1);
    reset_observations();p4_doom_gc_poll();assert(ready_attempts==1 && ready_acks[0]==3);

    /* Old canonical frames do not emit an immediate ACK; periodic recovery remains. */
    prepare_live(framed);duplicate_frame=true;queued_frames=1;p4_doom_gc_poll();
    assert(gc.sync.next_output==2 && ready_attempts==1 && ready_acks[0]==2);

    /* An INPUT datagram remains independent of same-poll READY deduplication. */
    prepare_live(framed);
    queued_frames=1;p4_doom_gc_poll();
    assert(ready_attempts==1 && input_attempts==3);

    /* Startup READY cadence is deliberately unchanged even after immediate ACK. */
    prepare_live(framed);gc.configured=false;queued_frames=1;p4_doom_gc_poll();
    assert(ready_attempts==2 && ready_acks[0]==3 && ready_acks[1]==3);

    /* Phase/loading controls and their deadlines remain independent. */
    prepare_live(framed);gc.next_keepalive_ms=0;queued_frames=1;
    poll_runtime(true);assert(ready_attempts==0 && gc.local_loading);
    reset_observations();clock_ms+=20;p4_doom_gc_poll();
    assert(ready_attempts==1 && ready_acks[0]==3 && !gc.local_loading);
    printf("PASS: live READY same-poll dedup/retry contract (%s)\n",framed?"GCE1":"initial raw roster");
}
int main(void)
{
    run_contract(false);run_contract(true);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    return 0;
}
