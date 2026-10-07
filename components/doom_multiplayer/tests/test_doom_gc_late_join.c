// SPDX-License-Identifier: GPL-2.0-or-later
/* Staged production adapter, real codecs/session/lockstep, virtual transport.
 * The engine sink verifies canonical masks and commands; device play is separate. */
#include "doom_gc_p4mp.c"
#include <assert.h>
#include <stdio.h>

static uint64_t clock_ms=100;
static p4_mp_session_t session;
static p4_doom_mp_launch_config_t cfg;
static uint32_t sequence,received,played;
static unsigned tickets_sent,accepts,unavailable,rejects,offers,transmits,wrapped_transmits;
static bool automatic;
static uint8_t frame_masks[1024];
static p4_doom_resume_control_t accepted,denied;
static p4_mp_lobby_offer_t observed_offer;
extern bool fixture_store_ok;
extern unsigned fixture_store_calls;

int64_t esp_timer_get_time(void) { return (int64_t)clock_ms*1000; }
void vTaskDelay(unsigned ms) { clock_ms+=ms; }
void p4_doom_gc_engine_begin(uint8_t count,uint8_t map)
{ assert(count==4 && map==1); }
void p4_doom_gc_engine_begin_mask(uint8_t count,uint8_t mask,uint8_t map)
{ assert(count==4 && mask==cfg.initial_player_mask && map==1); }
unsigned int D_P4TicCapacity(void) { return 128U-(received-played); }
int D_P4ReplayTic(void) { return (int)played; }
boolean D_P4ReplayFinish(int next) { (void)next;assert(false);return false; }
void D_ReceiveTic(ticcmd_t *cmd,boolean *mask)
{
    assert(host() && mask[0] && received<sizeof(frame_masks));
    uint8_t bits=0;
    for (unsigned i=0;i<4;++i) {
        if (mask[i]) bits |= (uint8_t)(1U<<i);
        else assert(!cmd[i].forwardmove && !cmd[i].sidemove && !cmd[i].angleturn &&
            !cmd[i].buttons && !cmd[i].consistancy && !cmd[i].chatchar);
    }
    frame_masks[received++]=bits;played=received;
}
static esp_err_t set_handler(void *ctx,p4_doom_p4mp_frame_handler_t fn,void *fnctx)
{ (void)ctx;(void)fnctx;assert(fn==receive_frame);return ESP_OK; }
static bool connected(void *ctx,uint64_t route) { (void)ctx;return route!=0; }
static void inject_sequence(uint64_t route,uint32_t peer,p4_mp_packet_type_t type,
    const uint8_t *bytes,uint16_t n,uint32_t ack,uint32_t seq,uint32_t session_id)
{
    uint8_t wire[P4_MP_MAX_DATAGRAM_BYTES];size_t length=0;
    assert(p4_mp_packet_encode(type,session_id,peer,seq,ack,bytes,n,
        wire,sizeof(wire),&length)==P4_MP_OK);
    receive_frame(NULL,route,wire,length);
}
static uint64_t fixture_read_nonce(const uint8_t *b)
{
    uint64_t nonce=0;
    for (unsigned i=0;i<8;++i) nonce |= (uint64_t)b[i]<<(8U*i);
    return nonce;
}
static void fixture_write_nonce(uint8_t *b,uint64_t nonce)
{ for (unsigned i=0;i<8;++i) b[i]=(uint8_t)(nonce>>(8U*i)); }
static uint64_t fixture_route_nonce(uint64_t route)
{
    for (unsigned i=0;i<4;++i) if (gc.routes[i]==route) return gc.resume_nonce[i];
    return 0;
}
static bool fixture_admission(p4_mp_packet_type_t type,const uint8_t *bytes,uint16_t n)
{
    return type==P4_MP_PACKET_DISCOVER || type==P4_MP_PACKET_JOIN ||
        (type==P4_MP_PACKET_GAME_MESSAGE && n>=4 && !memcmp(bytes,"GCR2",4));
}
static void inject_epoch_sequence(uint64_t route,uint32_t peer,p4_mp_packet_type_t type,
    const uint8_t *bytes,uint16_t n,uint32_t ack,uint32_t seq,uint64_t nonce)
{
    uint8_t wrapped[64]={'G','C','E','1'};
    assert(n<=sizeof(wrapped)-16U);
    fixture_write_nonce(wrapped+4,nonce);wrapped[12]=(uint8_t)type;
    memcpy(wrapped+16,bytes,n);
    inject_sequence(route,peer,P4_MP_PACKET_GAME_MESSAGE,wrapped,(uint16_t)(n+16U),ack,seq,7);
}
static void inject(uint64_t route,uint32_t peer,p4_mp_packet_type_t type,
    const uint8_t *bytes,uint16_t n,uint32_t ack)
{
    const uint64_t nonce=fixture_route_nonce(route);
    if (nonce && !fixture_admission(type,bytes,n))
        inject_epoch_sequence(route,peer,type,bytes,n,ack,++sequence,nonce);
    else inject_sequence(route,peer,type,bytes,n,ack,++sequence,type==P4_MP_PACKET_DISCOVER?0:7);
}
static void inject_control(uint64_t route,uint32_t peer,const p4_doom_resume_control_t *c)
{
    uint8_t bytes[P4_DOOM_RESUME_CONTROL_BYTES];size_t n=0;
    assert(p4_doom_resume_control_encode(c,bytes,&n));
    inject(route,peer,P4_MP_PACKET_GAME_MESSAGE,bytes,(uint16_t)n,0);
}
static void progress(uint8_t slot,uint32_t cursor,uint32_t sim,uint32_t pivot)
{
    uint8_t b[24]={'G','C','P','1',1,slot,16,0};
    put32(b+8,cursor);put32(b+12,sim);put32(b+16,pivot);
    const uint32_t peer=gc.pending_peer[slot]?gc.pending_peer[slot]:gc.original_peer[slot];
    inject(gc.routes[slot],peer,P4_MP_PACKET_GAME_MESSAGE,b,sizeof(b),0);
}
static void input(uint8_t slot,uint32_t tick,uint32_t ack)
{
    const p4_doom_mp_tic_t tic={.tick=tick,.forward_move=12,.consistency=99};
    p4_mp_input_t value;uint8_t b[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_doom_mp_tic_to_input(&tic,&value);p4_mp_input_encode(&value,b);
    const uint32_t peer=gc.pending_peer[slot]?gc.pending_peer[slot]:gc.original_peer[slot];
    inject(gc.routes[slot],peer,P4_MP_PACKET_INPUT,b,sizeof(b),ack);
}
static esp_err_t send_to(void *ctx,uint64_t route,const uint8_t *wire,size_t n)
{
    (void)ctx;assert(route);p4_mp_packet_view_t p;
    assert(p4_mp_packet_decode(wire,n,&p)==P4_MP_OK);++transmits;
    const uint64_t nonce=fixture_route_nonce(route);
    if (p.type==P4_MP_PACKET_GAME_MESSAGE && p.payload_length>=4 && !memcmp(p.payload,"GCE1",4)) {
        assert(nonce && p.payload_length>=16 && fixture_read_nonce(p.payload+4)==nonce &&
            !p.payload[13] && !p.payload[14] && !p.payload[15]);
        p.type=(p4_mp_packet_type_t)p.payload[12];p.payload+=16;p.payload_length-=16;
        assert(p.type==P4_MP_PACKET_GAME_MESSAGE || p.type==P4_MP_PACKET_INPUT ||
            p.type==P4_MP_PACKET_PING || p.type==P4_MP_PACKET_LEAVE);
        ++wrapped_transmits;
    } else assert(!nonce || fixture_admission(p.type,p.payload,p.payload_length) ||
        p.type==P4_MP_PACKET_REJECT || p.type==P4_MP_PACKET_OFFER);
    if (p.type==P4_MP_PACKET_OFFER) {
        ++offers;assert(p4_mp_lobby_offer_decode(p.payload,p.payload_length,&observed_offer)==P4_MP_OK);
    } else if (p.type==P4_MP_PACKET_REJECT) ++rejects;
    else if (p.type==P4_MP_PACKET_GAME_MESSAGE) {
        p4_doom_resume_control_t c;
        if (p4_doom_resume_control_decode(p.payload,p.payload_length,&c)) {
            assert(c.initial_player_mask==cfg.initial_player_mask);
            if (c.type==P4_DOOM_RESUME_TICKET) {
                ++tickets_sent;assert(cfg.initial_player_mask & (1U<<c.slot));
            } else if (c.type==P4_DOOM_RESUME_ACCEPTED) {
                ++accepts;accepted=c;
                assert(c.player_count==4 && c.accept.player_count==4 &&
                    c.accept.session_seed==cfg.session_seed && c.accept.assigned_player_slot==c.slot &&
                    c.accept.input_delay_tics==cfg.input_delay_tics);
                assert(!memcmp(c.accept.game_settings,cfg.lobby_offer.game_settings,8));
            } else if (c.type==P4_DOOM_RESUME_UNAVAILABLE) { ++unavailable;denied=c; }
        } else assert(p.payload_length==24 || p.payload_length==P4_DOOM_LOCKSTEP_BYTES);
    }
    return ESP_OK;
}
static void poll(void *ctx)
{
    (void)ctx;if (!automatic) return;
    for (uint8_t i=1;i<4;++i) if (cfg.initial_player_mask & (1U<<i)) {
        inject(gc.routes[i],gc.original_peer[i],P4_MP_PACKET_PING,
            (const uint8_t *)"GCAREADY",8,gc.sync.next_output);
        if (tickets_sent) {
            p4_doom_resume_control_t c=resume_control(i,P4_DOOM_RESUME_TICKET_ACK);
            inject_control(gc.routes[i],gc.original_peer[i],&c);
        }
    }
}
static void setup(uint8_t mask)
{
    assert(mask==1 || mask==3);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    clock_ms=100;received=played=0;sequence=1;automatic=false;
    tickets_sent=accepts=unavailable=rejects=offers=transmits=wrapped_transmits=0;
    memset(frame_masks,0,sizeof(frame_masks));memset(&accepted,0,sizeof(accepted));
    memset(&denied,0,sizeof(denied));fixture_store_ok=true;fixture_store_calls=0;
    cfg=(p4_doom_mp_launch_config_t){.enabled=true,.role=P4_MP_ROLE_HOST,
        .session_id=7,.self_peer_id=100,.remote_peer_id=mask==3?101U:0U,
        .route_id=mask==3?2U:0U,.local_player_slot=0,.player_count=4,.initial_player_mask=mask,
        .input_delay_tics=2,.session_seed=21,
        .setup={.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI,.mode=P4_DOOM_MP_MODE_ALTDEATH,
            .episode=1,.map=1,.skill=3,.no_monsters=true},
        .lobby_offer={.mode=P4_MP_GAME_MODE_LOCKSTEP,.game_api_major=1,
            .players_present=mask==3?2U:1U,.player_capacity=4,.input_delay_tics=2,
            .tick_rate_hz=35,.game_protocol=7,.session_seed=21,.game_id="doom.arena",
            .content_sha256={1},.compatibility_sha256={2}}};
    assert(p4_doom_mp_setup_encode(&cfg.setup,cfg.lobby_offer.game_settings));
    assert(p4_doom_mp_launch_config_valid(&cfg));
    assert(p4_mp_session_host_start(&session,7,100,3000)==P4_MP_OK);
    if (mask==3) assert(p4_mp_session_accept_peer(&session,101,2,1,1,clock_ms)==P4_MP_OK);
    else assert(session.state==P4_MP_SESSION_HOSTING);
    const p4_doom_p4mp_transport_t transport={.set_handler=set_handler,.send_to=send_to,.poll=poll,.connected=connected};
    assert(p4_doom_gc_prepare(&session,&cfg,&transport)==ESP_OK);
    assert(gc.resume_enabled && gc.sync.mask==mask);
}
static void configure(void)
{
    net_gamesettings_t settings;automatic=true;
    assert(p4_doom_gc_configure(&settings));automatic=false;
    assert(settings.num_players==4 && settings.consoleplayer==0 && received==2);
    assert(P4_DoomNetInitialPlayerMask()==cfg.initial_player_mask);
    assert(gc.ready==cfg.initial_player_mask && gc.tickets_acked==cfg.initial_player_mask);
    assert(frame_masks[0]==cfg.initial_player_mask && frame_masks[1]==cfg.initial_player_mask);
}
static void tick(void) { clock_ms+=20;p4_doom_gc_poll(); }
static void host_advance(uint32_t end)
{
    while (received<end) {
        const uint32_t t=received;const ticcmd_t cmd={0};
        p4_doom_gc_submit(&cmd,(int)t);
        for (uint8_t i=1;i<4;++i) if (gc.sync.mask & (1U<<i)) input(i,t,t);
        tick();assert(!gc.failed && received==t+1U);
    }
}
static p4_mp_lobby_join_t join_value(uint32_t nonce,uint8_t slot)
{
    p4_mp_lobby_join_t j={.join_nonce=nonce,.requested_player_slot=slot};
    memcpy(j.compatibility_sha256,cfg.lobby_offer.compatibility_sha256,32);return j;
}
static void join_send(uint64_t route,uint32_t peer,uint32_t nonce,uint8_t slot)
{
    const p4_mp_lobby_join_t j=join_value(nonce,slot);uint8_t b[P4_MP_JOIN_PAYLOAD_BYTES];
    assert(p4_mp_lobby_join_encode(&j,b)==P4_MP_OK);
    inject(route,peer,P4_MP_PACKET_JOIN,b,sizeof(b),0);
}
static p4_doom_resume_control_t request_from(const p4_doom_resume_control_t *a,uint64_t nonce)
{
    p4_doom_resume_control_t c=*a;c.type=P4_DOOM_RESUME_REQUEST;c.nonce=nonce;
    memcpy(c.compatibility_sha256,cfg.lobby_offer.compatibility_sha256,32);return c;
}
static unsigned connected_peers(void)
{
    unsigned count=0;
    for (unsigned i=0;i<P4_MP_MAX_REMOTE_PEERS;++i) if (session.peers[i].connected) ++count;
    return count;
}
static void assert_virgin(uint8_t slot)
{
    const uint8_t bit=(uint8_t)(1U<<slot);const uint8_t zero[16]={0};
    assert(!gc.original_peer[slot] && !gc.pending_peer[slot] && !gc.routes[slot]);
    assert(!(gc.fresh_pending & bit) && !(gc.resuming & bit) && !(gc.resume_armed & bit) &&
        !(gc.sync.mask & bit) && !(gc.sync.pending_mask & bit));
    assert(!gc.resume_nonce[slot] && !memcmp(gc.tickets[slot],zero,16));
}
static void initial_rosters(void)
{
    setup(1);configure();assert(!tickets_sent && !transmits && !connected_peers());
    for (uint8_t i=1;i<4;++i) assert_virgin(i);
    host_advance(100);
    for (unsigned i=0;i<100;++i) assert(frame_masks[i]==1);
    inject(22,0,P4_MP_PACKET_DISCOVER,NULL,0,0);
    assert(offers==1 && observed_offer.players_present==1 && observed_offer.player_capacity==4);
    setup(3);configure();assert(tickets_sent && connected_peers()==1 && gc.original_peer[1]==101);
    assert_virgin(2);assert_virgin(3);host_advance(20);
    for (unsigned i=0;i<20;++i) assert(frame_masks[i]==3);
}
static void retry_and_validation(void)
{
    setup(1);configure();host_advance(30);const p4_doom_mp_launch_config_t before=gc.config;
    const uint32_t nonce=UINT32_C(0xfedcba98);
    join_send(22,201,nonce,P4_MP_PLAYER_SLOT_ANY);
    assert(accepts==1 && accepted.nonce==nonce && accepted.initial_player_mask==1 && accepted.slot==1);
    assert(gc.fresh_attempt_count==1 && gc.pending_peer[1]==201 && !gc.original_peer[1] && gc.sync.mask==1);
    assert(gc.fresh_pending==2 && gc.resuming==2 && connected_peers()==1);
    assert(!memcmp(accepted.ticket,gc.fresh_attempts[0].ticket,16) &&
        gc.fresh_attempts[0].peer==201 && gc.fresh_attempts[0].nonce==nonce && gc.fresh_attempts[0].slot==1);
    const p4_doom_resume_control_t first=accepted;const uint64_t started=gc.resume_started_ms[1];
    clock_ms+=1000;join_send(22,201,nonce,P4_MP_PLAYER_SLOT_ANY);
    assert(accepts==2 && gc.resume_started_ms[1]==started && gc.fresh_attempt_count==1);
    assert(!memcmp(&first,&accepted,sizeof(first)));
    const p4_mp_lobby_join_t j=join_value(nonce,P4_MP_PLAYER_SLOT_ANY);uint8_t b[P4_MP_JOIN_PAYLOAD_BYTES];
    assert(p4_mp_lobby_join_encode(&j,b)==P4_MP_OK);
    inject_sequence(22,201,P4_MP_PACKET_JOIN,b,sizeof(b),0,sequence,7);assert(accepts==2);
    join_send(23,201,nonce,P4_MP_PLAYER_SLOT_ANY);
    join_send(22,201,nonce+1U,P4_MP_PLAYER_SLOT_ANY);
    join_send(22,201,nonce,1);
    assert(accepts==2 && gc.routes[1]==22 && gc.fresh_attempt_count==1 && connected_peers()==1);
    p4_doom_resume_control_t req=request_from(&first,nonce+1U);
    inject_control(23,201,&req);assert(unavailable==1 && denied.reason==1);
    assert(gc.resume_started_ms[1]==started && gc.resume_nonce[1]==nonce);
    /* Foreign match, short JOIN, bad schema, wrong compatibility and route alias. */
    inject_sequence(24,202,P4_MP_PACKET_JOIN,b,sizeof(b),0,++sequence,8);
    uint8_t short_wire[P4_MP_MAX_DATAGRAM_BYTES];size_t short_length=0;
    assert(p4_mp_packet_encode(P4_MP_PACKET_JOIN,7,202,++sequence,0,b,sizeof(b),
        short_wire,sizeof(short_wire),&short_length)==P4_MP_OK);
    receive_frame(NULL,24,short_wire,short_length-1U);
    b[0]^=1;inject(24,202,P4_MP_PACKET_JOIN,b,sizeof(b),0);
    p4_mp_lobby_join_t wrong_compat=join_value(7,P4_MP_PLAYER_SLOT_ANY);
    wrong_compat.compatibility_sha256[0]^=1;
    assert(p4_mp_lobby_join_encode(&wrong_compat,b)==P4_MP_OK);
    inject(24,202,P4_MP_PACKET_JOIN,b,sizeof(b),0);
    join_send(22,202,7,P4_MP_PLAYER_SLOT_ANY);
    join_send(24,202,5,1);join_send(24,100,6,P4_MP_PLAYER_SLOT_ANY);
    assert(accepts==2 && gc.fresh_attempt_count==1 && connected_peers()==1);
    req=request_from(&first,nonce+1U);req.ticket[0]^=2;inject_control(24,201,&req);
    req=request_from(&first,nonce+1U);req.initial_player_mask=3;inject_control(24,201,&req);
    req=request_from(&first,nonce+1U);req.compatibility_sha256[0]^=1;inject_control(24,201,&req);
    req=request_from(&first,nonce+1U);inject_control(24,202,&req);
    assert(unavailable==1 && connected_peers()==1 && gc.routes[1]==22 &&
        gc.resume_started_ms[1]==started && gc.pending_peer[1]==201);
    assert(!memcmp(&before,&gc.config,sizeof(before)) && gc.journal.next_tick==30);
    host_advance(35);for (unsigned i=0;i<35;++i) assert(frame_masks[i]==1);
}
static void owned_and_full(void)
{
    setup(3);configure();departed(1);assert(gc.original_peer[1]==101 && gc.sync.mask==1);
    join_send(22,101,8,P4_MP_PLAYER_SLOT_ANY);assert(!accepts && !gc.routes[1] && !gc.routes[2]);
    join_send(22,201,9,1);assert(!accepts && !connected_peers());
    join_send(22,201,10,P4_MP_PLAYER_SLOT_ANY);assert(accepts==1 && accepted.slot==2);
    join_send(23,202,11,P4_MP_PLAYER_SLOT_ANY);assert(accepts==2 && accepted.slot==3);
    join_send(24,203,12,P4_MP_PLAYER_SLOT_ANY);
    assert(accepts==2 && connected_peers()==2 && gc.fresh_attempt_count==2);
    clock_ms+=100;inject(25,0,P4_MP_PACKET_DISCOVER,NULL,0,0);
    assert(offers==1 && observed_offer.players_present==4);host_advance(10);
}
static void absolute_lease_and_reuse(void)
{
    setup(1);configure();join_send(22,201,41,P4_MP_PLAYER_SLOT_ANY);
    const p4_doom_resume_control_t first=accepted;const uint64_t started=gc.resume_started_ms[1];
    for (unsigned i=1;i<30;++i) {
        clock_ms=started+(uint64_t)i*10000U;
        join_send(22,201,41,P4_MP_PLAYER_SLOT_ANY);
        inject(22,201,P4_MP_PACKET_PING,(const uint8_t *)"GCAWAIT!",8,0);p4_doom_gc_poll();
        assert(gc.routes[1]==22 && !gc.failed && gc.resume_started_ms[1]==started);
    }
    clock_ms=started+P4_DOOM_GC_STARTUP_LIMIT_MS;
    join_send(22,201,41,P4_MP_PLAYER_SLOT_ANY);
    inject(22,201,P4_MP_PACKET_PING,(const uint8_t *)"GCAWAIT!",8,0);p4_doom_gc_poll();
    assert_virgin(1);assert(gc.fresh_attempts[0].revoked && !connected_peers() && !gc.failed);
    p4_doom_resume_control_t req=request_from(&first,99);inject_control(23,201,&req);
    assert(unavailable==1 && denied.reason==P4_DOOM_RESUME_REASON_PROVISIONAL_EXPIRED && denied.nonce==99);
    const unsigned count=accepts;join_send(23,201,41,P4_MP_PLAYER_SLOT_ANY);
    assert(accepts==count && !connected_peers());
    join_send(23,202,42,P4_MP_PLAYER_SLOT_ANY);
    assert(accepts==count+1U && accepted.slot==1 && memcmp(first.ticket,accepted.ticket,16));
    const p4_doom_resume_control_t second=accepted;inject_control(24,201,&req);
    assert(unavailable==2 && denied.reason==3 && gc.pending_peer[1]==202 && gc.routes[1]==23);
    assert(!memcmp(second.ticket,gc.tickets[1],16));host_advance(6);
}
static uint32_t arm_fresh(void)
{
    host_advance(40);join_send(22,201,51,P4_MP_PLAYER_SLOT_ANY);
    progress(1,40,40,0);const uint32_t pivot=gc.activation[1];assert(pivot==104);
    input(1,pivot,pivot);
    assert(gc.resume_armed==2 && gc.sync.pending_mask==2 && !gc.original_peer[1]);
    return pivot;
}
static void activation_and_departure(bool report_caught_up)
{
    setup(1);configure();const uint32_t pivot=arm_fresh();const p4_doom_resume_control_t fresh=accepted;
    host_advance(pivot);assert(!gc.original_peer[1] && gc.pending_peer[1]==201);
    if (report_caught_up) { progress(1,pivot,pivot,pivot);assert(!gc.resuming && !gc.original_peer[1]); }
    const ticcmd_t cmd={0};p4_doom_gc_submit(&cmd,(int)pivot);tick();
    assert(received==pivot+1U && gc.sync.mask==3 && gc.original_peer[1]==201 &&
        !gc.pending_peer[1] && !gc.fresh_pending && !gc.fresh_attempts[0].revoked);
    assert(frame_masks[pivot-1U]==1 && frame_masks[pivot]==3 && gc.config.initial_player_mask==1);
    if (report_caught_up) {
        inject(22,201,P4_MP_PACKET_PING,(const uint8_t *)"GCAREADY",8,received);
        assert(gc.ready==3);host_advance(pivot+3U); /* Extra READY bit must not close the initial barrier. */
    }
    /* Canonical ownership survives a lost activation ACK and later departure. */
    departed(1);assert(gc.original_peer[1]==201 && gc.sync.mask==1 && !gc.fresh_attempts[0].revoked);
    assert(!memcmp(gc.tickets[1],fresh.ticket,16));
    const unsigned count=accepts;join_send(23,201,52,P4_MP_PLAYER_SLOT_ANY);assert(accepts==count && !gc.routes[2]);
    p4_doom_resume_control_t req=request_from(&fresh,53);inject_control(23,201,&req);
    assert(accepts==count+1U && gc.routes[1]==23 && !gc.fresh_pending && gc.original_peer[1]==201);
    host_advance(received+2U);
}
static void release_before_activation(void)
{
    setup(1);configure();(void)arm_fresh();const p4_doom_resume_control_t first=accepted;
    departed(1);assert_virgin(1);assert(gc.fresh_attempts[0].revoked);
    join_send(23,202,54,P4_MP_PLAYER_SLOT_ANY);assert(accepts==2 && accepted.slot==1);
    p4_doom_resume_control_t req=request_from(&first,55);inject_control(24,201,&req);
    assert(unavailable==1 && denied.reason==3 && gc.pending_peer[1]==202);host_advance(50);
}
static void missed_activation(void)
{
    setup(1);configure();host_advance(40);join_send(22,201,56,P4_MP_PLAYER_SLOT_ANY);
    progress(1,40,40,0);const uint32_t pivot=gc.activation[1];assert(pivot==104);
    host_advance(pivot);tick();assert_virgin(1);
    assert(gc.fresh_attempts[0].revoked && !gc.failed);host_advance(pivot+2U);
    join_send(23,202,57,P4_MP_PLAYER_SLOT_ANY);assert(accepts==2 && accepted.slot==1);
}
static void journal_overflow(void)
{
    setup(1);configure();host_advance(8);join_send(22,201,61,P4_MP_PLAYER_SLOT_ANY);
    const p4_doom_resume_control_t first=accepted;
    /* Capacity is measured in complete records. Exhaust it through the real append path. */
    gc.journal.capacity=gc.journal.next_tick;host_advance(9);
    assert(!p4_doom_replay_journal_available(&gc.journal) && !gc.failed);
    tick();assert_virgin(1);assert(gc.fresh_attempts[0].revoked);host_advance(20);
    const unsigned count=accepts;join_send(23,202,62,P4_MP_PLAYER_SLOT_ANY);assert(accepts==count);
    p4_doom_resume_control_t req=request_from(&first,63);inject_control(23,201,&req);
    assert(unavailable==1 && denied.reason==3);clock_ms+=100;
    inject(24,0,P4_MP_PACKET_DISCOVER,NULL,0,0);assert(observed_offer.players_present==4);
    for (unsigned i=0;i<20;++i) assert(frame_masks[i]==1);
}
static void actual_attempt_bound(void)
{
    setup(1);configure();
    p4_doom_resume_control_t oldest={0};
    for (unsigned i=0;i<P4_DOOM_GC_RESUME_ATTEMPTS;++i) {
        join_send(22,201,1000U+i,P4_MP_PLAYER_SLOT_ANY);
        assert(accepts==i+1U && gc.fresh_attempt_count==i+1U && gc.used_nonce_count[1]==i+1U);
        assert(accepted.slot==1 && gc.pending_peer[1]==201);
        if (!i) oldest=accepted;
        departed(1);assert_virgin(1);assert(gc.fresh_attempts[i].revoked && !connected_peers());
    }
    join_send(22,201,2000,P4_MP_PLAYER_SLOT_ANY);
    assert(accepts==P4_DOOM_GC_RESUME_ATTEMPTS && !connected_peers() && gc.fresh_attempt_count==64);
    join_send(23,202,2001,P4_MP_PLAYER_SLOT_ANY);assert(!connected_peers() && !gc.routes[2]);
    p4_doom_resume_control_t req=request_from(&oldest,2002);inject_control(24,201,&req);
    assert(unavailable==1 && denied.reason==3);host_advance(100);
    clock_ms+=100;inject(25,0,P4_MP_PACKET_DISCOVER,NULL,0,0);
    assert(observed_offer.players_present==4 && !gc.failed);
}

/* Rejection must precede generic session processing. In particular, a stale
 * high sequence must neither renew the lease nor poison the new sequence cursor.
 * Compare every adapter byte except diagnostic counters, plus its session. */
static uint8_t epoch_state[sizeof(gc)];
static p4_mp_session_t epoch_session;
static unsigned epoch_transmits;
static void save_epoch_state(void)
{
    memcpy(epoch_state,&gc,sizeof(gc));epoch_session=session;epoch_transmits=transmits;
}
static void assert_epoch_unchanged(void)
{
    const size_t prefix=(size_t)((const uint8_t *)&gc.stats-(const uint8_t *)&gc);
    const size_t suffix=prefix+sizeof(gc.stats);
    assert(!memcmp(epoch_state,&gc,prefix));
    assert(!memcmp(epoch_state+suffix,(const uint8_t *)&gc+suffix,sizeof(gc)-suffix));
    assert(!memcmp(&epoch_session,&session,sizeof(session)) && transmits==epoch_transmits);
}
static void stale_epoch_packet(p4_mp_packet_type_t type,const uint8_t *payload,
    uint16_t length,uint32_t ack,uint64_t old_nonce,uint32_t *high_sequence)
{
    inject_epoch_sequence(22,201,type,payload,length,ack,++*high_sequence,old_nonce);
    assert_epoch_unchanged();
    /* Before GCE1 these same bare packets could belong to either admission. */
    inject_sequence(22,201,type,payload,length,ack,++*high_sequence,7);
    assert_epoch_unchanged();
}
static void epoch_isolation(void)
{
    setup(1);configure();host_advance(40);
    join_send(22,201,501,P4_MP_PLAYER_SLOT_ANY);const uint64_t old_nonce=gc.resume_nonce[1];
    departed(1);join_send(22,201,502,P4_MP_PLAYER_SLOT_ANY);
    const uint64_t nonce=gc.resume_nonce[1];assert(nonce!=old_nonce && !gc.replay_cursor[1]);
    join_send(23,202,503,P4_MP_PLAYER_SLOT_ANY);assert(gc.pending_peer[2]==202);
    uint32_t high_sequence=10000;
    uint8_t gcp[24]={'G','C','P','1',1,1,16,0};put32(gcp+8,40);put32(gcp+12,40);
    const uint8_t leave_bytes[2]={0};
    save_epoch_state();clock_ms+=1000;
    stale_epoch_packet(P4_MP_PACKET_GAME_MESSAGE,gcp,sizeof(gcp),0,old_nonce,&high_sequence);
    stale_epoch_packet(P4_MP_PACKET_PING,(const uint8_t *)"GCAWAIT!",8,0,old_nonce,&high_sequence);
    stale_epoch_packet(P4_MP_PACKET_PING,(const uint8_t *)"GCAREADY",8,40,old_nonce,&high_sequence);
    stale_epoch_packet(P4_MP_PACKET_LEAVE,leave_bytes,sizeof(leave_bytes),0,old_nonce,&high_sequence);
    /* Correct nonce alone does not authorize another identity, route or seat. */
    inject_epoch_sequence(22,202,P4_MP_PACKET_GAME_MESSAGE,gcp,sizeof(gcp),0,++high_sequence,nonce);
    assert_epoch_unchanged();
    inject_epoch_sequence(24,201,P4_MP_PACKET_GAME_MESSAGE,gcp,sizeof(gcp),0,++high_sequence,nonce);
    assert_epoch_unchanged();
    inject_epoch_sequence(23,202,P4_MP_PACKET_GAME_MESSAGE,gcp,sizeof(gcp),0,++high_sequence,nonce);
    assert_epoch_unchanged();
    inject_epoch_sequence(22,201,P4_MP_PACKET_GAME_MESSAGE,gcp,sizeof(gcp),0,++high_sequence,gc.resume_nonce[2]);
    assert_epoch_unchanged();

    uint8_t malformed[64]={'G','C','E','1'};
    fixture_write_nonce(malformed+4,nonce);malformed[12]=P4_MP_PACKET_PING;
    memcpy(malformed+16,"GCAWAIT!",8);
    for (uint16_t n=1;n<16;++n) {
        inject_sequence(22,201,P4_MP_PACKET_GAME_MESSAGE,malformed,n,0,++high_sequence,7);
        assert_epoch_unchanged();
    }
    for (unsigned i=13;i<16;++i) {
        malformed[i]=1;
        inject_sequence(22,201,P4_MP_PACKET_GAME_MESSAGE,malformed,24,0,++high_sequence,7);
        assert_epoch_unchanged();malformed[i]=0;
    }
    malformed[3]='0';
    inject_sequence(22,201,P4_MP_PACKET_GAME_MESSAGE,malformed,24,0,++high_sequence,7);
    assert_epoch_unchanged();malformed[3]='1';
    fixture_write_nonce(malformed+4,0);
    inject_sequence(22,201,P4_MP_PACKET_GAME_MESSAGE,malformed,24,0,++high_sequence,7);
    assert_epoch_unchanged();fixture_write_nonce(malformed+4,nonce);
    const uint8_t invalid_types[]={0,255,P4_MP_PACKET_JOIN,P4_MP_PACKET_DISCOVER,
        P4_MP_PACKET_REJECT,P4_MP_PACKET_PONG,P4_MP_PACKET_STATE_HASH};
    for (size_t i=0;i<sizeof(invalid_types);++i) {
        malformed[12]=invalid_types[i];
        inject_sequence(22,201,P4_MP_PACKET_GAME_MESSAGE,malformed,24,0,++high_sequence,7);
        assert_epoch_unchanged();
    }
    const struct { uint8_t type;uint16_t length; } bad_lengths[]={
        {P4_MP_PACKET_PING,7},{P4_MP_PACKET_PING,9},
        {P4_MP_PACKET_INPUT,P4_MP_INPUT_PAYLOAD_BYTES-1},
        {P4_MP_PACKET_INPUT,P4_MP_INPUT_PAYLOAD_BYTES+1},
        {P4_MP_PACKET_LEAVE,1},{P4_MP_PACKET_LEAVE,3},{P4_MP_PACKET_GAME_MESSAGE,0}};
    for (size_t i=0;i<sizeof(bad_lengths)/sizeof(bad_lengths[0]);++i) {
        malformed[12]=bad_lengths[i].type;
        inject_sequence(22,201,P4_MP_PACKET_GAME_MESSAGE,malformed,
            (uint16_t)(16U+bad_lengths[i].length),0,++high_sequence,7);
        assert_epoch_unchanged();
    }

    /* The lower, current sequence still works after every rejected high sequence. */
    progress(1,40,40,0);const uint32_t pivot=gc.activation[1];
    assert(gc.replay_cursor[1]==40 && gc.resume_seen_ms[1]==clock_ms && pivot==104);
    const p4_doom_mp_tic_t tic={.tick=pivot,.forward_move=12,.consistency=99};
    p4_mp_input_t input_value;uint8_t input_bytes[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_doom_mp_tic_to_input(&tic,&input_value);p4_mp_input_encode(&input_value,input_bytes);
    save_epoch_state();clock_ms+=100;
    stale_epoch_packet(P4_MP_PACKET_INPUT,input_bytes,sizeof(input_bytes),pivot,old_nonce,&high_sequence);
    input(1,pivot,pivot);assert(gc.resume_armed==2 && gc.sync.pending_mask==2);
    tick();assert(wrapped_transmits>0);
    host_advance(pivot);
    const ticcmd_t cmd={0};p4_doom_gc_submit(&cmd,(int)pivot);tick();
    assert(gc.original_peer[1]==201 && gc.sync.mask==3);
    inject(22,201,P4_MP_PACKET_PING,(const uint8_t *)"GCAREADY",8,received);
    assert(!(gc.resuming & 2U));
    /* The nonce binding survives the transition from provisional to live. */
    save_epoch_state();clock_ms+=100;
    stale_epoch_packet(P4_MP_PACKET_LEAVE,leave_bytes,sizeof(leave_bytes),0,old_nonce,&high_sequence);
    stale_epoch_packet(P4_MP_PACKET_PING,(const uint8_t *)"GCAWAIT!",8,0,old_nonce,&high_sequence);
    stale_epoch_packet(P4_MP_PACKET_GAME_MESSAGE,gcp,sizeof(gcp),0,old_nonce,&high_sequence);
    input_value.tick=received;p4_mp_input_encode(&input_value,input_bytes);
    stale_epoch_packet(P4_MP_PACKET_INPUT,input_bytes,sizeof(input_bytes),received,old_nonce,&high_sequence);
    host_advance(pivot+3U);
    inject(22,201,P4_MP_PACKET_LEAVE,leave_bytes,sizeof(leave_bytes),0);
    assert(gc.original_peer[1]==201 && !gc.routes[1] && gc.sync.mask==1 && !gc.failed);
}
int main(void)
{
    initial_rosters();retry_and_validation();owned_and_full();absolute_lease_and_reuse();
    activation_and_departure(false);activation_and_departure(true);release_before_activation();
    missed_activation();journal_overflow();actual_attempt_bound();epoch_isolation();
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS: solo/two-player masks, fresh identity/retry/expiry, canonical ownership, READY progress, journal overflow, 64 attempts, GCE1 stale/raw/malformed/cross-peer isolation before session mutation");
    return 0;
}
