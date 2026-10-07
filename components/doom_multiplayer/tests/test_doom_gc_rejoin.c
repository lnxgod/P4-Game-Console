// SPDX-License-Identifier: GPL-2.0-or-later
/* Production adapter + real codecs/session/lockstep, bounded virtual transport.
 * Engine ring is a capacity-aware sink; actual-engine equivalence is separate. */
#include "doom_gc_p4mp.c"
#include <assert.h>
#include <stdio.h>

static uint64_t clock_ms=100;
static p4_mp_session_t session;
static p4_doom_mp_launch_config_t cfg;
static uint32_t sequences[4], received, played, input_ack;
static unsigned capacity=128, tickets_sent, ticket_acks, accepts, unavailable, offers, rejects;
static unsigned snapshots, finish_calls, controls, replay_packets;
static uint32_t finish_tick;
static bool automatic, ack_tickets=true, eager_canonical, send_fails;
static uint8_t ticket[16], delivered_masks[256];
static p4_mp_lobby_offer_t observed_offer;
extern bool fixture_store_ok;
extern unsigned fixture_store_calls;

int64_t esp_timer_get_time(void) { return (int64_t)clock_ms*1000; }
void vTaskDelay(unsigned ms) { clock_ms+=ms; }
void p4_doom_gc_engine_begin_mask(uint8_t count,uint8_t mask,uint8_t map)
{ assert(count==4 && mask==cfg.initial_player_mask && map==1); }
unsigned int D_P4TicCapacity(void)
{ return received-played<capacity?capacity-(received-played):0; }
int D_P4ReplayTic(void) { return (int)played; }
boolean D_P4ReplayFinish(int next)
{ assert(next>=(int)received && next>=(int)played);++finish_calls;finish_tick=(uint32_t)next;return true; }
void D_ReceiveTic(ticcmd_t *cmd,boolean *mask)
{
    (void)cmd;assert(mask[0] && D_P4TicCapacity());
    if (received<sizeof(delivered_masks)) {
        uint8_t bits=0;
        for (unsigned i=0;i<4;++i) if (mask[i]) bits |= (uint8_t)(1U<<i);
        delivered_masks[received]=bits;
    }
    ++received;
    if (host()) played=received;
}
static esp_err_t set_handler(void *ctx,p4_doom_p4mp_frame_handler_t fn,void *fnctx)
{ (void)ctx;(void)fnctx;assert(fn==receive_frame);return ESP_OK; }
static bool connected(void *ctx,uint64_t route) { (void)ctx;return route!=0; }
static void inject(uint64_t route,uint32_t peer,p4_mp_packet_type_t type,
    const uint8_t *bytes,uint16_t n,uint32_t ack)
{
    uint8_t wire[P4_MP_MAX_DATAGRAM_BYTES];size_t length=0;
    const unsigned index=peer==100?0U:1U;
    uint64_t nonce=host()?0:cfg.resume_nonce;
    if (host()) for (uint8_t i=1;i<4;++i) if (gc.routes[i]==route) nonce=gc.resume_nonce[i];
    uint8_t wrapped[64]={ 'G','C','E','1' };
    const bool admission=type==P4_MP_PACKET_DISCOVER || type==P4_MP_PACKET_JOIN ||
        (type==P4_MP_PACKET_GAME_MESSAGE && n>=4 && !memcmp(bytes,"GCR2",4));
    if (nonce && !admission) {
        assert(n<=sizeof(wrapped)-16U);
        for (unsigned i=0;i<8;++i) wrapped[4U+i]=(uint8_t)(nonce>>(8U*i));
        wrapped[12]=(uint8_t)type;memcpy(wrapped+16,bytes,n);
        bytes=wrapped;n=(uint16_t)(n+16U);type=P4_MP_PACKET_GAME_MESSAGE;
    }
    assert(p4_mp_packet_encode(type,type==P4_MP_PACKET_DISCOVER?0:7,
        type==P4_MP_PACKET_DISCOVER?0:peer,++sequences[index],ack,bytes,n,
        wire,sizeof(wire),&length)==P4_MP_OK);
    receive_frame(NULL,route,wire,length);
}
static void inject_control(uint64_t route,uint32_t peer,const p4_doom_resume_control_t *c)
{
    uint8_t bytes[64];size_t n=0;
    assert(p4_doom_resume_control_encode(c,bytes,&n));
    inject(route,peer,P4_MP_PACKET_GAME_MESSAGE,bytes,(uint16_t)n,0);
}
static void progress(uint32_t cursor,uint32_t sim,uint32_t pivot,uint8_t credit)
{
    uint8_t b[24]={'G','C','P','1',1,1,credit,0};
    put32(b+8,cursor);put32(b+12,sim);put32(b+16,pivot);
    inject(22,101,P4_MP_PACKET_GAME_MESSAGE,b,sizeof(b),0);
}
static void input(uint64_t route,uint32_t tick,uint32_t ack)
{
    const p4_doom_mp_tic_t tic={.tick=tick,.forward_move=12,.consistency=99};
    p4_mp_input_t value;uint8_t b[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_doom_mp_tic_to_input(&tic,&value);p4_mp_input_encode(&value,b);
    inject(route,101,P4_MP_PACKET_INPUT,b,sizeof(b),ack);
}
static void canonical(uint32_t tick,uint8_t mask)
{
    p4_doom_lockstep_frame_t f={.tick=tick,.mask=mask};
    for (unsigned i=0;i<4;++i) f.commands[i].tick=tick;
    uint8_t b[P4_DOOM_LOCKSTEP_BYTES];
    assert(p4_doom_lockstep_frame_encode(&f,4,b));
    inject(1,100,P4_MP_PACKET_GAME_MESSAGE,b,sizeof(b),0);
}
static esp_err_t send_to(void *ctx,uint64_t route,const uint8_t *wire,size_t n)
{
    (void)ctx;assert(route);p4_mp_packet_view_t p;
    assert(p4_mp_packet_decode(wire,n,&p)==P4_MP_OK);
    if (p.type==P4_MP_PACKET_GAME_MESSAGE && p.payload_length>16 && !memcmp(p.payload,"GCE1",4)) {
        uint64_t nonce=0,expected=host()?0:cfg.resume_nonce;
        if (host()) for (uint8_t i=1;i<4;++i) if (gc.routes[i]==route) expected=gc.resume_nonce[i];
        for (unsigned i=0;i<8;++i) nonce |= (uint64_t)p.payload[4U+i]<<(8U*i);
        assert(expected && nonce==expected && !p.payload[13] && !p.payload[14] && !p.payload[15]);
        p.type=(p4_mp_packet_type_t)p.payload[12];p.payload+=16;p.payload_length=(uint16_t)(p.payload_length-16U);
    }
    ++snapshots;
    if (p.type==P4_MP_PACKET_OFFER) {
        ++offers;assert(p4_mp_lobby_offer_decode(p.payload,p.payload_length,&observed_offer)==P4_MP_OK);
    } else if (p.type==P4_MP_PACKET_REJECT) ++rejects;
    else if (p.type==P4_MP_PACKET_INPUT) input_ack=p.ack;
    else if (p.type==P4_MP_PACKET_GAME_MESSAGE) {
        p4_doom_resume_control_t c;
        if (p4_doom_resume_control_decode(p.payload,p.payload_length,&c)) {
            if (c.type==P4_DOOM_RESUME_TICKET) { ++tickets_sent;memcpy(ticket,c.ticket,16); }
            if (c.type==P4_DOOM_RESUME_TICKET_ACK) ++ticket_acks;
            if (c.type==P4_DOOM_RESUME_ACCEPTED) {
                ++accepts;assert(c.accept.player_count==4 && c.accept.session_seed==cfg.session_seed);
                assert(!memcmp(c.accept.game_settings,cfg.lobby_offer.game_settings,8));
            }
            if (c.type==P4_DOOM_RESUME_UNAVAILABLE) ++unavailable;
        } else if (p.payload_length==24 && !memcmp(p.payload,"GCP1",4)) ++controls;
        else { assert(p.payload_length==40);++replay_packets; }
    }
    return send_fails ? ESP_FAIL : ESP_OK;
}
static void poll(void *ctx)
{
    (void)ctx;if (!automatic) return;
    if (host()) {
        inject(2,101,P4_MP_PACKET_PING,(const uint8_t *)"GCAREADY",8,gc.sync.next_output);
        if (tickets_sent && ack_tickets) {
            p4_doom_resume_control_t c=resume_control(1,P4_DOOM_RESUME_TICKET_ACK);
            inject_control(2,101,&c);
        }
    } else {
        p4_doom_resume_control_t c={.type=P4_DOOM_RESUME_TICKET,.slot=1,.player_count=4,.initial_player_mask=3};
        memset(c.ticket,42,16);inject_control(1,100,&c);
        if (ticket_acks || eager_canonical) { canonical(0,3);canonical(1,3); }
        inject(1,100,P4_MP_PACKET_PING,(const uint8_t *)"GCAHOST!",8,0);
    }
}
static void setup_mask(bool client,bool resume,uint8_t initial_mask)
{
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    memset(delivered_masks,0,sizeof(delivered_masks));
    clock_ms=100;received=played=0;capacity=128;automatic=false;ack_tickets=true;eager_canonical=false;send_fails=false;
    tickets_sent=ticket_acks=accepts=unavailable=offers=rejects=0;
    snapshots=finish_calls=controls=replay_packets=0;finish_tick=input_ack=0;
    sequences[0]=sequences[1]=1;fixture_store_ok=true;fixture_store_calls=0;
    cfg=(p4_doom_mp_launch_config_t){.enabled=true,.role=client?P4_MP_ROLE_CLIENT:P4_MP_ROLE_HOST,
        .session_id=7,.self_peer_id=client?101:100,.remote_peer_id=client?100:101,
        .route_id=client?1:2,.local_player_slot=client?1:0,.player_count=4,.initial_player_mask=initial_mask,
        .input_delay_tics=2,.session_seed=21,.rejoining=resume,.resume_nonce=resume?35:0,
        .setup={.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI,.mode=P4_DOOM_MP_MODE_ALTDEATH,
            .episode=1,.map=1,.skill=3,.no_monsters=true},
        .lobby_offer={.mode=P4_MP_GAME_MODE_LOCKSTEP,.game_api_major=1,.players_present=2,
            .player_capacity=4,.input_delay_tics=2,.tick_rate_hz=35,.game_protocol=7,
            .session_seed=21,.game_id="doom.arena",.content_sha256={1},.compatibility_sha256={2}}};
    assert(p4_doom_mp_setup_encode(&cfg.setup,cfg.lobby_offer.game_settings));
    memset(cfg.resume_ticket,42,16);
    if (!client) {
        assert(p4_mp_session_host_start(&session,7,100,3000)==P4_MP_OK);
        assert(p4_mp_session_accept_peer(&session,101,2,1,1,clock_ms)==P4_MP_OK);
    } else {
        assert(p4_mp_session_client_start(&session,7,101,100,1,clock_ms,3000)==P4_MP_OK);
        assert(p4_mp_session_accept_host(&session,100,1,1,clock_ms)==P4_MP_OK);
    }
    const p4_doom_p4mp_transport_t transport={.set_handler=set_handler,.send_to=send_to,.poll=poll,.connected=connected};
    assert(p4_doom_gc_prepare(&session,&cfg,&transport)==ESP_OK);
    assert(gc.resume_enabled);
}
static void setup(bool client,bool resume) { setup_mask(client,resume,3); }
static void configure(void)
{
    net_gamesettings_t settings;automatic=true;
    assert(p4_doom_gc_configure(&settings));automatic=false;
    assert(settings.num_players==4 && settings.consoleplayer==cfg.local_player_slot);
}
static void tick(void) { clock_ms+=20;p4_doom_gc_poll(); }
static void host_advance(uint32_t end)
{
    while (received<end) {
        const uint32_t t=received;const ticcmd_t cmd={0};
        p4_doom_gc_submit(&cmd,(int)t);
        if ((gc.sync.mask & 2U) && t>=2) input(gc.routes[1],t,t);
        tick();assert(!gc.failed && received==t+1U);
    }
}
static void leave(void)
{
    const uint8_t why[2]={0};inject(2,101,P4_MP_PACKET_LEAVE,why,sizeof(why),0);
    assert(gc.routes[1]==0 && gc.sync.mask==1 && !gc.failed);
}
static p4_doom_resume_control_t request(void)
{
    p4_doom_resume_control_t c={.type=P4_DOOM_RESUME_REQUEST,.slot=1,.player_count=4,.initial_player_mask=3,.nonce=36};
    memcpy(c.ticket,ticket,16);memcpy(c.compatibility_sha256,cfg.lobby_offer.compatibility_sha256,32);
    return c;
}
static void host_admission(void)
{
    setup(false,false);configure();assert(tickets_sent && gc.tickets_acked==3 && received==2);
    host_advance(10);leave();host_advance(80);
    assert(gc.journal.next_tick==80 && gc.original_peer[1]==101);
    const p4_doom_mp_launch_config_t original=gc.config;
    inject(22,101,P4_MP_PACKET_DISCOVER,NULL,0,0);
    assert(offers==1 && !memcmp(&observed_offer,&cfg.lobby_offer,sizeof(observed_offer)));
    p4_mp_lobby_join_t join={.requested_player_slot=1,.join_nonce=5};
    memcpy(join.compatibility_sha256,cfg.lobby_offer.compatibility_sha256,32);
    uint8_t jb[P4_MP_JOIN_PAYLOAD_BYTES];assert(p4_mp_lobby_join_encode(&join,jb)==P4_MP_OK);
    inject(22,101,P4_MP_PACKET_JOIN,jb,sizeof(jb),0);assert(rejects==1 && !gc.routes[1]);
    p4_doom_resume_control_t c=request();c.ticket[0]^=1;inject_control(22,101,&c);
    assert(!accepts && !gc.routes[1]);c=request();c.compatibility_sha256[0]^=1;inject_control(22,101,&c);
    assert(!accepts && !gc.routes[1]);c=request();inject_control(22,102,&c);
    assert(!accepts && !gc.routes[1]);inject_control(22,101,&c);
    assert(accepts==1 && gc.routes[1]==22 && gc.resuming==2 && gc.sync.mask==1);
    inject_control(23,101,&c);assert(accepts==1 && gc.routes[1]==22);
    inject_control(22,101,&c);assert(accepts==2 && gc.resume_started_ms[1]==clock_ms);
    assert(!memcmp(&original,&gc.config,sizeof(original)) && gc.journal.next_tick==80);
    progress(80,80,0,16);const uint32_t pivot=gc.activation[1];assert(pivot==144);
    progress(81,80,pivot,33);assert(gc.replay_cursor[1]==80); /* impossible credit/cursor */
    input(22,pivot,pivot);assert(gc.resume_armed==2 && gc.sync.pending_mask==2 && gc.resuming==2);
    host_advance(pivot);assert(gc.sync.next_output==pivot && gc.sync.mask==1);
    progress(pivot,pivot,pivot,16);assert(gc.resuming==0);
    const ticcmd_t neutral={0};p4_doom_gc_submit(&neutral,(int)pivot);tick();
    assert(received==pivot+1U && gc.sync.mask==3 && !gc.failed);
    p4_doom_gc_quit();
}
static void activation_without_exact_progress(bool use_progress)
{
    setup(false,false);configure();leave();host_advance(40);
    p4_doom_resume_control_t c=request();inject_control(22,101,&c);progress(40,40,0,16);
    const uint32_t pivot=gc.activation[1];input(22,pivot,pivot);
    assert(gc.resuming==2);host_advance(pivot);
    const ticcmd_t cmd={0};p4_doom_gc_submit(&cmd,(int)pivot);tick();
    assert(received==pivot+1U && gc.sync.mask==3 && gc.resuming==2);
    /* Replay finished and activation played between two GCP send intervals. */
    if (use_progress) progress(pivot+1U,pivot+1U,pivot,16);
    else inject(22,101,P4_MP_PACKET_PING,(const uint8_t *)"GCAREADY",8,pivot+1U);
    assert(!gc.resuming && !gc.failed);
    host_advance(pivot+2U);p4_doom_gc_quit();
}
static void bounded_failure(void)
{
    setup(false,false);configure();leave();host_advance(40);
    p4_doom_resume_control_t c=request();inject_control(22,101,&c);progress(40,40,0,16);
    const uint32_t pivot=gc.activation[1];host_advance(pivot);tick();
    assert(!gc.resuming && gc.routes[1]==0 && gc.sync.mask==1 && !gc.failed);
    host_advance(pivot+2U); /* missed activation cannot stop the host */
    inject_control(22,101,&c);assert(unavailable==1); /* stale attempt cannot bind again */
    ++c.nonce;inject_control(22,101,&c);assert(gc.resuming==2);
    clock_ms+=P4_DOOM_GC_STARTUP_LIMIT_MS;tick();assert(!gc.resuming && !gc.failed);
    const uint64_t newer=c.nonce;--c.nonce;inject_control(22,101,&c);
    assert(unavailable==2 && gc.routes[1]==0); /* A -> B -> old A stays stale */
    c.nonce=newer+1U;gc.used_nonce_count[1]=P4_DOOM_GC_RESUME_ATTEMPTS;
    inject_control(22,101,&c);assert(unavailable==3 && !gc.routes[1]);
    gc.used_nonce_count[1]=2;
    gc.journal.available=false;++c.nonce;inject_control(22,101,&c);
    assert(unavailable==4 && gc.routes[1]==0);host_advance(pivot+3U);
    p4_doom_gc_quit();
}
static void guest_replay(bool virgin)
{
    setup_mask(true,true,virgin?1U:3U);configure();
    assert(received==0 && P4_DoomNetReplaying());
    assert(P4_DoomNetInitialPlayerMask()==(virgin?1U:3U));
    capacity=8;
    for (uint32_t t=0;t<40;++t) canonical(t,!virgin && t<10?3:1);
    tick();assert(received==8 && gc.sync.next_output==32 && D_P4TicCapacity()==0);
    canonical(32,1);assert(gc.sync.next_output==33); /* ring regained seats after delivery */
    while (received<80) {
        played=received;
        while (gc.sync.next_output<80 && gc.sync.next_output-gc.sync.next_read<16)
            canonical(gc.sync.next_output,!virgin && gc.sync.next_output<10?3:1);
        tick();assert(!gc.failed && received-played<=capacity);
    }
    played=received;
    uint8_t b[24]={'G','C','P','1',2,1,0,0};put32(b+16,144);
    inject(1,100,P4_MP_PACKET_GAME_MESSAGE,b,sizeof(b),0);tick();
    assert(finish_calls==1 && finish_tick==144 && gc.sync.replaying);
    const ticcmd_t cmd={.forwardmove=12};p4_doom_gc_submit(&cmd,144);tick();assert(input_ack==144);
    while (received<144) {
        played=received;
        while (gc.sync.next_output<144 && gc.sync.next_output-gc.sync.next_read<16)
            canonical(gc.sync.next_output,1);
        tick();assert(!gc.failed);
    }
    assert(!gc.sync.replaying && gc.sync.local.pending_count==1 && P4_DoomNetReplaying());
    played=received;canonical(144,3);tick();played=received;tick();
    assert(played==145 && !P4_DoomNetReplaying() && gc.sync.local.pending_count==0 && !gc.failed);
    for (unsigned i=0;i<144;++i) assert(delivered_masks[i]==(!virgin && i<10?3U:1U));
    assert(delivered_masks[144]==3);
    p4_doom_gc_quit();
}
static void rejected_guest_wire(p4_mp_packet_type_t type,const uint8_t *payload,uint16_t n,uint32_t peer)
{
    const p4_mp_session_t before_session=session;
    const p4_doom_lockstep_t before_sync=gc.sync;
    const uint64_t progress_before=gc.progress_ms[0];
    const uint32_t activation_before=gc.activation[1];
    const unsigned delivered_before=received;
    uint8_t wire[P4_MP_MAX_DATAGRAM_BYTES];size_t length=0;
    assert(p4_mp_packet_encode(type,7,peer,1000000U,999999U,payload,n,
        wire,sizeof(wire),&length)==P4_MP_OK);
    receive_frame(NULL,1,wire,length);
    assert(!memcmp(&before_session,&session,sizeof(session)));
    assert(!memcmp(&before_sync,&gc.sync,sizeof(gc.sync)));
    assert(gc.progress_ms[0]==progress_before && gc.activation[1]==activation_before &&
        received==delivered_before && !gc.failed);
}
static void guest_generation_isolation(void)
{
    setup_mask(true,true,1);configure();
    uint8_t canonical_bytes[40];p4_doom_lockstep_frame_t frame={.mask=1};
    assert(p4_doom_lockstep_frame_encode(&frame,4,canonical_bytes));
    uint8_t gcp[24]={'G','C','P','1',2,1,0,0};put32(gcp+16,144);
    const uint8_t leave[2]={0};
    const uint8_t ping[8]={'G','C','A','H','O','S','T','!'};
    const uint8_t *payloads[]={canonical_bytes,gcp,leave,ping};
    const uint16_t lengths[]={40,24,2,8};
    const p4_mp_packet_type_t types[]={P4_MP_PACKET_GAME_MESSAGE,P4_MP_PACKET_GAME_MESSAGE,
        P4_MP_PACKET_LEAVE,P4_MP_PACKET_PING};
    for (unsigned i=0;i<4;++i) {
        uint8_t b[64]={'G','C','E','1'};
        for (unsigned j=0;j<8;++j) b[4U+j]=(uint8_t)((cfg.resume_nonce-1U)>>(8U*j));
        b[12]=(uint8_t)types[i];memcpy(b+16,payloads[i],lengths[i]);
        rejected_guest_wire(P4_MP_PACKET_GAME_MESSAGE,b,(uint16_t)(lengths[i]+16U),100);
        rejected_guest_wire(types[i],payloads[i],lengths[i],100); /* No raw fallback. */
        ++b[4]; /* Correct nonce on a foreign peer still cannot bind. */
        rejected_guest_wire(P4_MP_PACKET_GAME_MESSAGE,b,(uint16_t)(lengths[i]+16U),102);
    }
    uint8_t b[64]={'G','C','E','1'};
    for (unsigned j=0;j<8;++j) b[4U+j]=(uint8_t)(cfg.resume_nonce>>(8U*j));
    b[12]=(uint8_t)P4_MP_PACKET_PING;memcpy(b+16,ping,8);
    for (uint16_t n=1;n<24;++n) rejected_guest_wire(P4_MP_PACKET_GAME_MESSAGE,b,n,100);
    for (unsigned i=13;i<16;++i) { b[i]=1;rejected_guest_wire(P4_MP_PACKET_GAME_MESSAGE,b,24,100);b[i]=0; }
    b[3]='0';rejected_guest_wire(P4_MP_PACKET_GAME_MESSAGE,b,24,100);b[3]='1';
    b[12]=(uint8_t)P4_MP_PACKET_JOIN;rejected_guest_wire(P4_MP_PACKET_GAME_MESSAGE,b,24,100);
    b[12]=(uint8_t)P4_MP_PACKET_INPUT;rejected_guest_wire(P4_MP_PACKET_GAME_MESSAGE,b,24,100);
    /* Earlier rejected million-scale sequences must not poison the valid stream. */
    canonical(0,1);tick();assert(received==1 && gc.sync.next_output==1 && !gc.failed);
    p4_doom_gc_quit();
}
static void ticket_barrier(void)
{
    setup(false,false);ack_tickets=false;automatic=true;
    net_gamesettings_t settings;assert(!p4_doom_gc_configure(&settings));
    assert(gc.failed && received==0 && gc.sync.next_output==0 && tickets_sent>10);
    p4_doom_gc_quit();
    setup(true,false);fixture_store_ok=false;automatic=true;
    assert(!p4_doom_gc_configure(&settings));
    assert(gc.failed && !ticket_acks && fixture_store_calls>1 && received==0);
    p4_doom_gc_quit();
    setup(true,false);fixture_store_ok=false;eager_canonical=true;automatic=true;
    assert(!p4_doom_gc_configure(&settings));
    assert(gc.failed && !ticket_acks && received==0 && clock_ms>=P4_DOOM_GC_STARTUP_LIMIT_MS);
    p4_doom_gc_quit();
    setup(true,false);configure();assert(ticket_acks && fixture_store_calls && received==2);
    p4_doom_gc_quit();
}
static void network_diagnostics(void)
{
    setup(false,false);configure();
    p4_doom_net_stats_t before,after,repeat;
    p4_doom_gc_get_stats(&before);
    assert(before.tx_attempts && before.rx_packets && !before.tx_failures);
    const uint8_t bad=0;
    receive_frame(NULL,2,&bad,1);
    receive_frame(NULL,99,&bad,1);
    p4_doom_gc_get_stats(&after);
    assert(after.rx_packets==before.rx_packets+2U);
    assert(after.rx_session_rejected==before.rx_session_rejected+2U);
    assert(!gc.failed && gc.routes[1]==2);

    before=after;
    send_route(2,P4_MP_PACKET_PING,(const uint8_t *)"GCAHOST!",8);
    send_fails=true;send_route(2,P4_MP_PACKET_PING,(const uint8_t *)"GCAHOST!",8);send_fails=false;
    /* Oversized payload fails encoding before the transport callback. */
    const unsigned previous_snapshots=snapshots;
    send_route(2,P4_MP_PACKET_PING,&bad,UINT16_MAX);
    p4_doom_gc_get_stats(&after);
    assert(after.tx_attempts==before.tx_attempts+3U);
    assert(after.tx_failures==before.tx_failures+2U && snapshots==previous_snapshots);

    inject(2,101,P4_MP_PACKET_PING,(const uint8_t *)"GCAREADY",8,gc.sync.next_output);
    p4_doom_gc_get_stats(&before);tick();p4_doom_gc_get_stats(&after);
    assert(after.host_blocked_peer_polls==before.host_blocked_peer_polls);
    const uint32_t next=gc.sync.next_output;
    const ticcmd_t cmd={0};p4_doom_gc_submit(&cmd,(int)next);
    tick();p4_doom_gc_get_stats(&after);
    assert(after.host_blocked_peer_polls==before.host_blocked_peer_polls+1U);
    input(2,next,next);tick();p4_doom_gc_get_stats(&after);
    assert(after.host_blocked_peer_polls==before.host_blocked_peer_polls+1U);
    assert(gc.sync.next_output==next+1U && !gc.failed);

    before=after;leave();p4_doom_gc_get_stats(&after);
    assert(after.peer_departures==before.peer_departures+1U);
    departed(1);p4_doom_gc_get_stats(&repeat);
    assert(repeat.peer_departures==after.peer_departures);
    p4_doom_gc_get_stats(NULL);p4_doom_gc_get_stats(&repeat);
    assert(!memcmp(&repeat,&after,sizeof(after)));
    gc.stats.tx_attempts=gc.stats.tx_failures=UINT32_MAX;
    send_fails=true;send_route(22,P4_MP_PACKET_PING,(const uint8_t *)"GCAHOST!",8);send_fails=false;
    gc.stats.rx_packets=gc.stats.rx_session_rejected=UINT32_MAX;
    receive_frame(NULL,99,&bad,1);p4_doom_gc_get_stats(&after);
    assert(after.tx_attempts==UINT32_MAX && after.tx_failures==UINT32_MAX);
    assert(after.rx_packets==UINT32_MAX && after.rx_session_rejected==UINT32_MAX);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    p4_doom_gc_get_stats(&after);
    const p4_doom_net_stats_t zero={0};assert(!memcmp(&after,&zero,sizeof(zero)));
}
int main(void)
{
    ticket_barrier();host_admission();bounded_failure();guest_replay(false);guest_replay(true);guest_generation_isolation();
    activation_without_exact_progress(false);activation_without_exact_progress(true);
    network_diagnostics();
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS: ticket store barrier, reserved new-route admission, live-host preservation, bounded replay/activation/failure");
    return 0;
}
