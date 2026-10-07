// SPDX-License-Identifier: GPL-2.0-or-later
/* Actual adapter with real wire/session/service/transfer/journal/lockstep.
 * Only the engine and transport are instrumented. Sends copy before responses. */
#include <stdlib.h>
static void *fixture_malloc(size_t);
static void fixture_free(void *);
#define malloc fixture_malloc
#define free fixture_free
#include "doom_gc_p4mp.c"
#undef malloc
#undef free
#include <assert.h>
#include <stdio.h>

#include "checkpoint_host_sha.h"
extern bool fixture_store_ok;
extern unsigned fixture_store_calls;

enum { FIXTURE_BYTES=3*P4_CT_CHUNK_BYTES+17, QUEUE_MAX=128 };
typedef struct { uint8_t bytes[P4_MP_MAX_DATAGRAM_BYTES];size_t length;uint64_t route; } sent_packet;
static sent_packet output[QUEUE_MAX];
static size_t output_count;
static p4_mp_session_t fixture_session;
static p4_doom_mp_launch_config_t cfg;
static uint64_t clock_ms;
static unsigned delivery_elapsed_ms;
static uint32_t abort_reason;
static uint32_t sequences[4],received,played;
static unsigned captures,restores,engine_rebases,checkpoint_packets,readiness,restored_reports;
static unsigned engine_queries;
static unsigned canonical_packets,accepts,quit_requests;
static uint32_t first_canonical,last_canonical;
static bool inside_boundary,capture_fail,restore_fail,send_fail;
static bool handler_fail,init_fail,identity_fail,codec_active;
static uint8_t capture_member_override;
static unsigned allocations,deallocations,live_allocations,init_calls,shutdown_calls;
static void *owned_allocations[8];
static uint8_t fixture_bytes[FIXTURE_BYTES];
static p4_doom_arena_t arena;
static p4_doom_resume_control_t accepted;

static void *fixture_malloc(size_t length)
{
    void *pointer=malloc(length);assert(pointer);
    for (unsigned i=0;i<8;++i) if (!owned_allocations[i]) {
        owned_allocations[i]=pointer;++allocations;++live_allocations;return pointer;
    }
    assert(false);return NULL;
}
static void fixture_free(void *pointer)
{
    if (!pointer) return;
    for (unsigned i=0;i<8;++i) if (owned_allocations[i]==pointer) {
        owned_allocations[i]=NULL;++deallocations;assert(live_allocations);--live_allocations;
        free(pointer);return;
    }
    assert(false);
}
int64_t esp_timer_get_time(void) { return (int64_t)clock_ms*1000; }
void vTaskDelay(unsigned ms) { clock_ms+=ms; }
void doomgeneric_RequestQuit(void) { ++quit_requests; }
void p4_doom_gc_engine_begin_mask(uint8_t count,uint8_t mask,uint8_t map)
{ assert(count==4 && mask==cfg.initial_player_mask && map==1); }
unsigned int D_P4TicCapacity(void) { ++engine_queries;assert(received>=played);return 128U-(received-played); }
int D_P4ReplayTic(void) { ++engine_queries;return (int)played; }
boolean D_P4ReplayFinish(int next) { assert(!host() && next>=(int)played);return true; }
void D_ReceiveTic(ticcmd_t *commands,boolean *mask)
{
    assert(!inside_boundary && commands && mask[0] && D_P4TicCapacity());
    ++received;if (host()) played=received;
    clock_ms+=delivery_elapsed_ms;
}
bool P4_DoomCheckpointInit(void) { ++init_calls;codec_active=!init_fail;return !init_fail; }
void P4_DoomCheckpointShutdown(void) { ++shutdown_calls;codec_active=false; }
bool P4_DoomCheckpointSetContentIdentity(const char *identity)
{
    assert(identity && strlen(identity)==70 && !memcmp(identity,"P4CK2|",6));
    assert(identity[6]=='0' && identity[7]=='2');return !identity_fail;
}
static void make_bytes(uint32_t tic,uint8_t members)
{
    for (unsigned i=0;i<FIXTURE_BYTES;++i) fixture_bytes[i]=(uint8_t)(i*29U+17U);
    memcpy(fixture_bytes,"FIX2",4);put32(fixture_bytes+4,tic);fixture_bytes[8]=members;
}
bool P4_DoomCheckpointCapture(uint8_t *buffer,size_t capacity,size_t *length,
    uint32_t *next_tic,uint8_t *members)
{
    assert(inside_boundary && host());++captures;
    if (capture_fail) return false;
    const uint8_t mask=capture_member_override?capture_member_override:gc.sync.mask;
    assert(capacity>=sizeof(fixture_bytes));make_bytes(played,mask);
    memcpy(buffer,fixture_bytes,sizeof(fixture_bytes));*length=sizeof(fixture_bytes);
    *next_tic=played;*members=mask;return true;
}
bool P4_DoomCheckpointRestore(const uint8_t *buffer,size_t length,
    uint32_t *next_tic,uint8_t *members)
{
    assert(inside_boundary && !host() && gc.replaying && !gc.checkpoint_guest_restored);
    ++restores;
    assert(length==sizeof(fixture_bytes) && !memcmp(buffer,fixture_bytes,length));
    if (restore_fail) return false;
    *next_tic=get32(buffer+4);*members=buffer[8];
    /* The real codec rebases the engine itself; this models that contract. */
    received=played=*next_tic;++engine_rebases;arena.connected_mask=*members;return true;
}
const char *P4_DoomCheckpointReason(void) { return "instrumented engine fixture"; }
void p4_doom_gc_checkpoint_get_arena(p4_doom_arena_t *value) { *value=arena; }
bool p4_doom_gc_checkpoint_set_arena(const p4_doom_arena_t *value) { arena=*value;return true; }

static esp_err_t set_handler(void *context,p4_doom_p4mp_frame_handler_t handler,void *handler_context)
{ (void)context;(void)handler_context;assert(handler==receive_frame);return handler_fail?ESP_FAIL:ESP_OK; }
static bool connected(void *context,uint64_t route) { (void)context;return route!=0; }
static void transport_poll(void *context) { (void)context; }
static void unwrap(p4_mp_packet_view_t *packet)
{
    if (packet->type==P4_MP_PACKET_GAME_MESSAGE && packet->payload_length>16 &&
        !memcmp(packet->payload,"GCE1",4)) {
        packet->type=(p4_mp_packet_type_t)packet->payload[12];
        packet->payload+=16;packet->payload_length=(uint16_t)(packet->payload_length-16U);
    }
}
static esp_err_t send_to(void *context,uint64_t route,const uint8_t *bytes,size_t length)
{
    (void)context;assert(route && length<=sizeof(output[0].bytes) && output_count<QUEUE_MAX);
    output[output_count].route=route;output[output_count].length=length;
    memcpy(output[output_count++].bytes,bytes,length);
    p4_mp_packet_view_t packet;assert(p4_mp_packet_decode(bytes,length,&packet)==P4_MP_OK);
    if (packet.type==P4_MP_PACKET_CHECKPOINT) ++checkpoint_packets;
    unwrap(&packet);
    if (packet.type==P4_MP_PACKET_GAME_MESSAGE) {
        p4_doom_resume_control_t control;
        if (p4_doom_resume_control_decode(packet.payload,packet.payload_length,&control)) {
            if (control.type==P4_DOOM_RESUME_ACCEPTED) { ++accepts;accepted=control; }
        } else if (packet.payload_length==24 && !memcmp(packet.payload,"GCP1",4)) {
            if (packet.payload[4]==3) abort_reason=get32(packet.payload+16);
            if (packet.payload[4]==4) ++readiness;
            if (packet.payload[4]==5) ++restored_reports;
        } else {
            p4_doom_lockstep_frame_t frame;
            if (p4_doom_lockstep_frame_decode(packet.payload,packet.payload_length,4,&frame)) {
                if (!canonical_packets) first_canonical=frame.tick;
                last_canonical=frame.tick;++canonical_packets;
            }
        }
    }
    return send_fail?ESP_FAIL:ESP_OK;
}
static void clear_output(void) { output_count=0; }
static void inject_raw(uint64_t route,uint32_t peer,p4_mp_packet_type_t type,
    const uint8_t *bytes,size_t length,uint32_t sequence,uint32_t session)
{
    uint8_t wire[P4_MP_MAX_DATAGRAM_BYTES];size_t n;
    assert(length<=UINT16_MAX);
    assert(p4_mp_packet_encode(type,session,peer,sequence,0,bytes,(uint16_t)length,
        wire,sizeof(wire),&n)==P4_MP_OK);
    receive_frame(NULL,route,wire,n);
}
static void inject(uint64_t route,uint32_t peer,p4_mp_packet_type_t type,
    const uint8_t *bytes,size_t length)
{
    const unsigned index=peer==100?0U:(unsigned)(peer-100U);assert(index<4);
    uint64_t nonce=host()?gc.resume_nonce[index]:cfg.resume_nonce;
    const bool admission=type==P4_MP_PACKET_JOIN ||
        (type==P4_MP_PACKET_GAME_MESSAGE && length>=4 && !memcmp(bytes,"GCR2",4));
    uint8_t wrapped[64]={'G','C','E','1'};
    if (nonce && type!=P4_MP_PACKET_CHECKPOINT && !admission) {
        assert(length<=sizeof(wrapped)-16);
        for (unsigned i=0;i<8;++i) wrapped[4+i]=(uint8_t)(nonce>>(8U*i));
        wrapped[12]=(uint8_t)type;memcpy(wrapped+16,bytes,length);
        bytes=wrapped;length+=16;type=P4_MP_PACKET_GAME_MESSAGE;
    }
    inject_raw(route,peer,type,bytes,length,++sequences[index],7);
}
static void control(uint8_t slot,uint8_t type,uint32_t cursor,uint32_t simulated,uint8_t credit)
{
    uint8_t bytes[24]={'G','C','P','1',type,slot,credit,0};
    put32(bytes+8,cursor);put32(bytes+12,simulated);
    inject(gc.routes[slot],100U+slot,P4_MP_PACKET_GAME_MESSAGE,bytes,sizeof(bytes));
}
static void host_input(uint8_t slot,uint32_t tic,uint32_t ack)
{
    const p4_doom_mp_tic_t value={.tick=tic};p4_mp_input_t input;
    p4_doom_mp_tic_to_input(&value,&input);
    uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES];p4_mp_input_encode(&input,payload);
    uint8_t wrapped[16+P4_MP_INPUT_PAYLOAD_BYTES]={'G','C','E','1'};
    for (unsigned i=0;i<8;++i) wrapped[4+i]=(uint8_t)(gc.resume_nonce[slot]>>(8U*i));
    wrapped[12]=(uint8_t)P4_MP_PACKET_INPUT;memcpy(wrapped+16,payload,sizeof(payload));
    uint8_t wire[P4_MP_MAX_DATAGRAM_BYTES];size_t length;
    assert(p4_mp_packet_encode(P4_MP_PACKET_GAME_MESSAGE,7,100U+slot,++sequences[slot],ack,
        wrapped,sizeof(wrapped),wire,sizeof(wire),&length)==P4_MP_OK);
    receive_frame(NULL,gc.routes[slot],wire,length);
}
static void join(uint8_t slot)
{
    p4_mp_lobby_join_t request={.join_nonce=34U+slot,.requested_player_slot=slot};
    memcpy(request.compatibility_sha256,cfg.lobby_offer.compatibility_sha256,32);
    uint8_t bytes[P4_MP_JOIN_PAYLOAD_BYTES];assert(p4_mp_lobby_join_encode(&request,bytes)==P4_MP_OK);
    inject(21U+slot,100U+slot,P4_MP_PACKET_JOIN,bytes,sizeof(bytes));
    assert(gc.resuming & (1U<<slot));
}
static boolean boundary(void)
{
    assert(!inside_boundary);inside_boundary=true;
    const boolean result=P4_DoomNetCheckpointBoundary();inside_boundary=false;return result;
}
static void tick(void)
{
    const unsigned before=checkpoint_packets;clock_ms+=20;p4_doom_gc_poll();
    if (host()) assert(checkpoint_packets-before<=P4_CT_SEND_BUDGET);
}
static void assert_loading(p4_doom_loading_phase_t phase,uint32_t completed,uint32_t total)
{
    /* The display can call this while another subsystem owns its lock. A
     * snapshot must neither re-enter the engine nor mutate protocol state. */
    uint8_t before_gc[sizeof(gc)];memcpy(before_gc,&gc,sizeof(gc));
    const p4_mp_session_t before_session=fixture_session;
    const unsigned queries=engine_queries,before_restores=restores,before_captures=captures;
    const unsigned before_allocations=allocations,before_deallocations=deallocations;
    const size_t before_output=output_count;const uint64_t before_clock=clock_ms;
    p4_doom_loading_progress_t progress={.phase=P4_DOOM_LOADING_FAILED,.completed=UINT32_MAX,.total=UINT32_MAX};
    for (unsigned i=0;i<2;++i) {
        p4_doom_gc_get_loading_progress(&progress);
        assert(progress.phase==phase && progress.completed==completed && progress.total==total);
    }
    p4_doom_gc_get_loading_progress(NULL);
    assert(!memcmp(before_gc,&gc,sizeof(gc)));
    assert(!memcmp(&before_session,&fixture_session,sizeof(fixture_session)));
    assert(engine_queries==queries && restores==before_restores && captures==before_captures);
    assert(allocations==before_allocations && deallocations==before_deallocations);
    assert(output_count==before_output && clock_ms==before_clock);
}
static void setup(bool guest)
{
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    assert_loading(P4_DOOM_LOADING_IDLE,0,0);
    clock_ms=100;received=played=0;delivery_elapsed_ms=0;abort_reason=0;
    for (unsigned i=0;i<4;++i) sequences[i]=1;
    captures=restores=engine_rebases=checkpoint_packets=readiness=restored_reports=0;
    canonical_packets=accepts=quit_requests=0;first_canonical=last_canonical=0;
    inside_boundary=capture_fail=restore_fail=send_fail=false;
    handler_fail=init_fail=identity_fail=false;
    capture_member_override=0;
    assert(!live_allocations && !codec_active);allocations=deallocations=init_calls=shutdown_calls=0;clear_output();
    fixture_store_ok=true;fixture_store_calls=0;
    arena=(p4_doom_arena_t){.capacity=4,.connected_mask=1,.maps={1},.map_count=1};
    cfg=(p4_doom_mp_launch_config_t){.enabled=true,.role=guest?P4_MP_ROLE_CLIENT:P4_MP_ROLE_HOST,
        .session_id=7,.self_peer_id=guest?101:100,.remote_peer_id=guest?100:0,
        .route_id=guest?1:0,.local_player_slot=guest?1:0,.player_count=4,.initial_player_mask=1,
        .input_delay_tics=2,.session_seed=21,.rejoining=guest,.resume_nonce=guest?35:0,
        .setup={.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI,.mode=P4_DOOM_MP_MODE_ALTDEATH,
            .episode=1,.map=1,.skill=3,.no_monsters=true},
        .lobby_offer={.mode=P4_MP_GAME_MODE_LOCKSTEP,.game_api_major=1,.players_present=1,
            .player_capacity=4,.input_delay_tics=2,.tick_rate_hz=35,.game_protocol=8,
            .session_seed=21,.game_id="doom.arena",.content_sha256={1},.compatibility_sha256={2}}};
    memset(cfg.resume_ticket,42,sizeof(cfg.resume_ticket));
    assert(p4_doom_mp_setup_encode(&cfg.setup,cfg.lobby_offer.game_settings));
    assert(p4_doom_mp_launch_config_valid(&cfg));
    if (guest) {
        assert(p4_mp_session_client_start(&fixture_session,7,101,100,1,clock_ms,3000)==P4_MP_OK);
        assert(p4_mp_session_accept_host(&fixture_session,100,1,1,clock_ms)==P4_MP_OK);
    } else assert(p4_mp_session_host_start(&fixture_session,7,100,3000)==P4_MP_OK);
    const p4_doom_p4mp_transport_t transport={.set_handler=set_handler,.send_to=send_to,
        .poll=transport_poll,.connected=connected};
    assert(p4_doom_gc_prepare(&fixture_session,&cfg,&transport)==ESP_OK);
    assert(gc.checkpoint_mode && gc.resume_enabled);
    assert_loading(P4_DOOM_LOADING_IDLE,0,0);
    net_gamesettings_t settings;assert(p4_doom_gc_configure(&settings));
    assert(settings.num_players==4 && settings.consoleplayer==cfg.local_player_slot);
    assert(!captures && !restores && !engine_rebases);clear_output();
    assert_loading(guest?P4_DOOM_LOADING_IDLE:P4_DOOM_LOADING_READY,0,0);
}
static void host_advance(uint32_t end)
{
    assert(host());
    while (received<end) {
        clear_output();const uint32_t tic=received;const ticcmd_t cmd={0};
        p4_doom_gc_submit(&cmd,(int)tic);tick();assert(!gc.failed && received==tic+1);
    }
}
static bool verify(void *context,const uint8_t *bytes,size_t length,const uint8_t expected[32])
{
    (void)context;uint8_t digest[32];
    return p4_doom_checkpoint_sha256(bytes,length,digest) && !memcmp(digest,expected,32);
}
static void host_receive_ack(uint8_t slot,p4_ct_rx *receiver)
{
    uint8_t bytes[P4_CT_ACK_WIRE];size_t length;
    assert(p4_ct_rx_ack(receiver,bytes,sizeof(bytes),&length));
    inject(gc.routes[slot],100U+slot,P4_MP_PACKET_CHECKPOINT,bytes,length);
}
static void host_wire_step(p4_ct_rx *receiver,uint8_t *storage,bool *drop_first)
{
    for (size_t i=0;i<output_count;++i) {
        p4_mp_packet_view_t packet;
        assert(p4_mp_packet_decode(output[i].bytes,output[i].length,&packet)==P4_MP_OK);
        if (packet.type!=P4_MP_PACKET_CHECKPOINT) continue;
        p4_ct_meta meta;
        if (p4_ct_meta_decode(&meta,packet.payload,packet.payload_length)) {
            assert(meta.identity.schema==2 && meta.identity.session==7 && meta.identity.attempt==35);
            assert(meta.next_tic>0 && meta.members==1 && meta.map==1);
            if (!receiver->active) assert(p4_ct_rx_start(receiver,&meta.identity,&meta,
                storage,FIXTURE_BYTES,verify,NULL,clock_ms));
            host_receive_ack(1,receiver);
        } else {
            assert(receiver->active);
            if (*drop_first) { *drop_first=false;continue; }
            assert(p4_ct_rx_chunk(receiver,packet.payload,packet.payload_length,clock_ms)!=P4_CT_REJECT);
            host_receive_ack(1,receiver);
        }
    }
    clear_output();
}
static void host_capture_transfer_suffix(void)
{
    setup(false);assert(gc.journal.rolling && gc.journal.capacity==4200);
    host_advance(96);join(1);assert(accepts==1 && accepted.slot==1);
    assert(!boundary() && !captures);clear_output();tick();
    assert(!captures && !restores);control(1,4,0,0,0);
    clear_output();tick();assert(platform_readonly_blob_loading_progress());
    assert(!captures && !restores && !engine_rebases);
    const uint32_t at=played;assert(!boundary() && captures==1 && played==at);
    const p4_ct_meta *meta=p4_cs_meta(&gc.checkpoints,1);
    assert(meta && meta->next_tic==at && gc.replay_cursor[1]==0 && !gc.activation[1]);
    control(1,1,at,at,16);assert(gc.replay_cursor[1]==0 && !gc.activation[1]);
    control(1,5,at,at,0);assert(!gc.checkpoint_restored && !gc.activation[1]);
    p4_ct_rx receiver={0};uint8_t storage[FIXTURE_BYTES];bool dropped=true;
    for (unsigned i=0;i<100 && !p4_cs_complete(&gc.checkpoints,1);++i) {
        clear_output();tick();host_wire_step(&receiver,storage,&dropped);
    }
    assert(receiver.verified && p4_cs_complete(&gc.checkpoints,1) && !dropped);
    assert(!memcmp(storage,fixture_bytes,sizeof(storage)) && !restores && !engine_rebases);
    uint8_t forged[P4_CT_ACK_WIRE];size_t forged_length;
    assert(p4_ct_rx_ack(&receiver,forged,sizeof(forged),&forged_length));forged[20]^=1U;
    const p4_mp_session_t unchanged=fixture_session;
    inject_raw(22,101,P4_MP_PACKET_CHECKPOINT,forged,forged_length,1000000U,7);
    assert(!memcmp(&unchanged,&fixture_session,sizeof(unchanged)) && !gc.failed);
    control(1,5,at+1,at+1,0);assert(!gc.checkpoint_restored && !gc.activation[1]);
    control(1,5,at,at,0);assert(gc.checkpoint_restored==2 && gc.replay_cursor[1]==at);
    host_advance(at+8);canonical_packets=0;control(1,1,at,at,16);clear_output();tick();
    assert(canonical_packets && first_canonical==at && last_canonical<gc.journal.next_tick);
    assert(!restores && !engine_rebases && played==at+8 && gc.sync.next_output==at+8);
    control(1,4,0,0,0);assert(!boundary() && captures==1);
    control(1,1,played,played,16);const uint32_t pivot=gc.activation[1];
    assert(pivot==played+P4_DOOM_GC_REPLAY_PIVOT_AHEAD);
    host_input(1,pivot,pivot);assert(gc.resume_armed==2 && gc.sync.pending_mask==2);
    host_advance(pivot);control(1,1,pivot,pivot,16);
    assert(!gc.resuming && !p4_cs_meta(&gc.checkpoints,1));
    clear_output();const ticcmd_t neutral={0};p4_doom_gc_submit(&neutral,(int)pivot);tick();
    assert(!gc.failed && received==pivot+1 && gc.sync.mask==3 && gc.original_peer[1]==101);
    assert(!gc.fresh_pending && !restores && !engine_rebases);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS host safe capture, loss repair, forged ACK isolation, no rewind, suffix T and fresh activation");
}
static void host_coalesces_and_expires(void)
{
    setup(false);host_advance(8);join(1);join(2);control(1,4,0,0,0);control(2,4,0,0,0);
    assert(!boundary() && captures==1);
    const p4_ct_meta *a=p4_cs_meta(&gc.checkpoints,1),*b=p4_cs_meta(&gc.checkpoints,2);
    assert(a && b && a->identity.checkpoint==b->identity.checkpoint && a->next_tic==b->next_tic);
    assert(a->identity.attempt!=b->identity.attempt);clear_output();tick();
    assert(checkpoint_packets<=2 && !restores && !engine_rebases);
    clock_ms+=P4_CS_LEASE_MS;clear_output();tick();
    assert(!gc.resuming && !gc.failed && gc.sync.mask==1);
    host_advance(received+1);assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    setup(false);host_advance(8);join(1);control(1,4,0,0,0);capture_fail=true;
    assert(!boundary() && captures==1 && !p4_cs_meta(&gc.checkpoints,1));
    clock_ms+=P4_CS_CAPTURE_WAIT_MS;clear_output();tick();
    assert(!gc.resuming && !gc.failed && !restores && !engine_rebases);
    host_advance(received+1);assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS host coalesced capture, bounded failed capture and lease preserve live host");
}
static void host_waits_for_engine_departure(void)
{
    setup(false);host_advance(20);join(1);join(2);
    control(1,4,0,0,0);control(2,4,0,0,0);
    /* Session/sync has removed seat 2, but its departure tic has not yet
     * removed it from the actual engine world. Both coalesced requests wait. */
    capture_member_override=5;const uint32_t before=played;
    assert(!boundary() && captures==1 && !restores && !engine_rebases);
    assert(!p4_cs_meta(&gc.checkpoints,1) && !p4_cs_meta(&gc.checkpoints,2));
    clear_output();tick();assert(!checkpoint_packets && played==before && !gc.failed);
    capture_member_override=1;
    assert(!boundary() && captures==2 && played==before && !restores && !engine_rebases);
    const p4_ct_meta *a=p4_cs_meta(&gc.checkpoints,1),*b=p4_cs_meta(&gc.checkpoints,2);
    assert(a && b && a->members==1 && b->members==1 && a->next_tic==before && b->next_tic==before);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS checkpoint waits for all requested seats to leave actual engine world without host rewind");
}
static p4_ct_meta guest_meta(uint32_t tic)
{
    make_bytes(tic,1);
    p4_ct_meta meta={.identity={.schema=2,.session=7,.attempt=35,.checkpoint=1},
        .length=FIXTURE_BYTES,.next_tic=tic,.map=1,.members=1};
    memcpy(meta.identity.content_id,cfg.lobby_offer.compatibility_sha256,32);
    assert(p4_doom_checkpoint_sha256(fixture_bytes,sizeof(fixture_bytes),meta.sha256));return meta;
}
static void inject_meta(const p4_ct_meta *meta)
{
    uint8_t bytes[P4_CT_META_WIRE];size_t length;
    assert(p4_ct_meta_encode(meta,bytes,sizeof(bytes),&length));
    inject(1,100,P4_MP_PACKET_CHECKPOINT,bytes,length);
}
static void canonical_mask(uint32_t tic,uint8_t mask)
{
    p4_doom_lockstep_frame_t frame={.tick=tic,.mask=mask};
    for (unsigned i=0;i<4;++i) frame.commands[i].tick=tic;
    uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES];assert(p4_doom_lockstep_frame_encode(&frame,4,bytes));
    inject(1,100,P4_MP_PACKET_GAME_MESSAGE,bytes,sizeof(bytes));
}
static void canonical(uint32_t tic) { canonical_mask(tic,1); }
static void tx_receive_acks(p4_ct_tx *sender)
{
    for (size_t i=0;i<output_count;++i) {
        p4_mp_packet_view_t packet;
        assert(p4_mp_packet_decode(output[i].bytes,output[i].length,&packet)==P4_MP_OK);
        if (packet.type==P4_MP_PACKET_CHECKPOINT)
            assert(p4_ct_tx_ack(sender,packet.payload,packet.payload_length,clock_ms)!=P4_CT_REJECT);
    }
    clear_output();
}
static void deliver_guest(const p4_ct_meta *meta,bool corrupt)
{
    p4_ct_tx sender;assert(p4_ct_tx_start(&sender,meta,fixture_bytes,sizeof(fixture_bytes),verify,NULL,clock_ms));
    clear_output();inject_meta(meta);assert(gc.checkpoint_rx.active);
    assert_loading(P4_DOOM_LOADING_CHECKPOINT,0,FIXTURE_BYTES);
    tx_receive_acks(&sender);
    uint8_t delayed[P4_CT_PACKET_MAX];size_t delayed_length=0;
    bool delivered_chunks[4]={false};
    for (unsigned i=0;i<32 && !sender.verified && !gc.failed;++i) {
        uint8_t bytes[P4_CT_PACKET_MAX];size_t length;uint32_t index;
        assert(p4_ct_tx_prepare(&sender,clock_ms,bytes,sizeof(bytes),&length,&index));
        assert(p4_ct_tx_sent(&sender,index,clock_ms));
        assert(index<4);
        if (index==0 && !delayed_length) {
            memcpy(delayed,bytes,length);delayed_length=length;continue;
        }
        if (corrupt && index==sender.count-1U) bytes[length-1U]^=1U;
        inject(1,100,P4_MP_PACKET_CHECKPOINT,bytes,length);
        assert(!restores && !engine_rebases);tx_receive_acks(&sender);
        delivered_chunks[index]=true;
        if (gc.failed) assert_loading(P4_DOOM_LOADING_FAILED,0,0);
        else {
            unsigned contiguous=0;
            while (contiguous<4 && delivered_chunks[contiguous]) ++contiguous;
            const uint32_t bytes_done=contiguous*P4_CT_CHUNK_BYTES;
            assert_loading(P4_DOOM_LOADING_CHECKPOINT,
                bytes_done<FIXTURE_BYTES?bytes_done:FIXTURE_BYTES,FIXTURE_BYTES);
            if (index==1 && !delivered_chunks[0]) {
                /* Out-of-order and repeated packets do not count bytes past
                 * a hole. Closing the hole advances the contiguous prefix. */
                inject(1,100,P4_MP_PACKET_CHECKPOINT,bytes,length);
                tx_receive_acks(&sender);assert_loading(P4_DOOM_LOADING_CHECKPOINT,0,FIXTURE_BYTES);
                inject(1,100,P4_MP_PACKET_CHECKPOINT,delayed,delayed_length);
                delivered_chunks[0]=true;tx_receive_acks(&sender);
                assert_loading(P4_DOOM_LOADING_CHECKPOINT,2*P4_CT_CHUNK_BYTES,FIXTURE_BYTES);
            }
        }
    }
    if (!corrupt) {
        assert(sender.verified && gc.checkpoint_rx.verified);
        /* The short final chunk is 17 bytes, never rounded up to 896. */
        assert_loading(P4_DOOM_LOADING_CHECKPOINT,FIXTURE_BYTES,FIXTURE_BYTES);
    }
}
static void guest_restore_boundary_suffix(void)
{
    setup(true);p4_ct_meta meta=guest_meta(90);
    const p4_mp_session_t before=fixture_session;inject_meta(&meta);
    assert(!memcmp(&fixture_session,&before,sizeof(before)) && !gc.checkpoint_rx.active);
    clear_output();tick();assert(!readiness && !restores);
    assert_loading(P4_DOOM_LOADING_IDLE,0,0);
    assert(!boundary() && gc.checkpoint_content_ready);clear_output();tick();assert(readiness==1);
    assert_loading(P4_DOOM_LOADING_CHECKPOINT,0,0);
    canonical(0);assert(gc.sync.next_output==0 && !received);
    deliver_guest(&meta,false);
    assert(!restores && !engine_rebases && !gc.checkpoint_guest_restored);
    clear_output();tick();assert(platform_readonly_blob_loading_progress());
    assert(!restores && !engine_rebases && !restored_reports && gc.sync.next_output==0);
    assert(boundary() && restores==1 && engine_rebases==1 && gc.checkpoint_guest_restored);
    assert(received==90 && played==90 && gc.sync.next_output==90 && gc.sync.next_read==90);
    assert_loading(P4_DOOM_LOADING_CATCHUP,0,0);
    assert(!boundary() && restores==1);clear_output();tick();assert(restored_reports==1);
    canonical(0);assert(gc.sync.next_output==90);
    canonical(90);assert(gc.sync.next_output==91 && received==90);
    clear_output();tick();assert(received==91 && played==90 && !gc.failed);
    assert_loading(P4_DOOM_LOADING_CATCHUP,0,0);
    assert(restores==1 && engine_rebases==1 && captures==0);
    uint8_t pivot_control[24]={'G','C','P','1',2,1,0,0};put32(pivot_control+16,98);
    inject(1,100,P4_MP_PACKET_GAME_MESSAGE,pivot_control,sizeof(pivot_control));
    clear_output();tick();assert(gc.replay_input_ready);
    assert_loading(P4_DOOM_LOADING_CATCHUP,0,8);
    const ticcmd_t command={0};p4_doom_gc_submit(&command,98);
    for (uint32_t tic=91;tic<98;++tic) canonical(tic);
    played=received;clear_output();tick();assert(received==98 && !gc.sync.replaying && gc.replaying);
    /* Seven more queued canonical tics are not consumed engine work. */
    assert(played==91);assert_loading(P4_DOOM_LOADING_CATCHUP,1,8);
    played=95;assert_loading(P4_DOOM_LOADING_CATCHUP,1,8);
    clear_output();tick();assert_loading(P4_DOOM_LOADING_CATCHUP,5,8);
    played=received;canonical_mask(98,3);clear_output();tick();
    assert_loading(P4_DOOM_LOADING_CATCHUP,8,8);
    played=received;clear_output();tick();
    assert(played==99 && !P4_DoomNetReplaying() && !gc.failed && gc.sync.mask==3);
    assert_loading(P4_DOOM_LOADING_READY,0,0);
    assert(!gc.checkpoint_storage[0] && !gc.checkpoint_rx.active && restores==1 && engine_rebases==1);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS guest readiness, boundary-only restore once, suffix T through live activation and buffer release");
}
static void guest_metadata_gates(void)
{
    setup(true);assert(!boundary());p4_ct_meta valid=guest_meta(120);
    for (unsigned which=0;which<8;++which) {
        p4_ct_meta bad=valid;
        if (which==0) ++bad.identity.attempt;
        if (which==1) ++bad.identity.session;
        if (which==2) ++bad.identity.schema;
        if (which==3) bad.identity.content_id[0]^=1U;
        if (which==4) bad.members=3;
        if (which==5) bad.next_tic=(uint32_t)INT_MAX+1U;
        uint8_t bytes[P4_CT_META_WIRE];size_t length;assert(p4_ct_meta_encode(&bad,bytes,sizeof(bytes),&length));
        const p4_mp_session_t before=fixture_session;
        inject_raw(which==6?99:1,100,P4_MP_PACKET_CHECKPOINT,bytes,length,
            sequences[0]+100U,which==7?99:7);
        assert(!memcmp(&fixture_session,&before,sizeof(before)) && !gc.checkpoint_rx.active && !gc.failed);
    }
    inject_meta(&valid);assert(gc.checkpoint_rx.active);
    p4_ct_meta replacement=valid;++replacement.identity.checkpoint;
    const p4_mp_session_t before=fixture_session;inject_meta(&replacement);
    assert(!memcmp(&fixture_session,&before,sizeof(before)) &&
        gc.checkpoint_rx.meta.identity.checkpoint==valid.identity.checkpoint && !restores);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS checkpoint route/session/attempt/schema/content/roster/tic gates leave session unchanged");
}
static void stale_packets_after_quit(const p4_ct_meta *meta)
{
    assert(!gc.prepared && !live_allocations && !codec_active);
    p4_ct_tx sender;assert(p4_ct_tx_start(&sender,meta,fixture_bytes,sizeof(fixture_bytes),verify,NULL,clock_ms));
    uint8_t chunk[P4_CT_PACKET_MAX];size_t length;uint32_t index;
    assert(p4_ct_tx_prepare(&sender,clock_ms,chunk,sizeof(chunk),&length,&index));
    const p4_mp_session_t before_session=fixture_session;
    uint8_t before_gc[sizeof(gc)];memcpy(before_gc,&gc,sizeof(gc));
    p4_doom_net_stats_t expected_stats=gc.stats;
    expected_stats.rx_packets+=2;expected_stats.rx_session_rejected+=2;
    const size_t stats_offset=(size_t)((const uint8_t *)&gc.stats-(const uint8_t *)&gc);
    memcpy(before_gc+stats_offset,&expected_stats,sizeof(expected_stats));
    const unsigned before_quits=quit_requests;const size_t before_output=output_count;
    inject_meta(meta);inject(1,100,P4_MP_PACKET_CHECKPOINT,chunk,length);
    assert(!memcmp(before_gc,&gc,sizeof(gc)) && !memcmp(&before_session,&fixture_session,sizeof(fixture_session)));
    assert(quit_requests==before_quits && output_count==before_output && !live_allocations && !codec_active);
}
static void guest_integrity_and_restore_failure(void)
{
    setup(true);assert(!boundary());p4_ct_meta meta=guest_meta(130);deliver_guest(&meta,true);
    assert(gc.failed && !restores && !engine_rebases && !gc.checkpoint_guest_restored);
    assert(gc.prepared && !quit_requests); /* Receive only latches failure. */
    assert_loading(P4_DOOM_LOADING_FAILED,0,0);
    assert(!platform_readonly_blob_loading_progress() && gc.prepared && !quit_requests);
    clear_output();tick();assert(quit_requests==1 && !gc.prepared && !live_allocations && !codec_active);
    assert_loading(P4_DOOM_LOADING_FAILED,0,0);
    assert(boundary() && !restores);stale_packets_after_quit(&meta);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    assert_loading(P4_DOOM_LOADING_IDLE,0,0);
    setup(true);assert(!boundary());meta=guest_meta(130);deliver_guest(&meta,false);restore_fail=true;
    assert(boundary() && gc.failed && restores==1 && !engine_rebases && !gc.checkpoint_guest_restored);
    assert(quit_requests==1 && !gc.prepared);
    assert_loading(P4_DOOM_LOADING_FAILED,0,0);
    assert(boundary() && restores==1);clear_output();tick();assert(!restored_reports && !received);
    assert(!live_allocations && !codec_active && quit_requests==1);
    stale_packets_after_quit(&meta);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    /* The !prepared guard must also protect an ordinary cancellation without
     * gc.failed, while the retired receive object still describes a transfer. */
    setup(true);assert(!boundary());meta=guest_meta(130);deliver_guest(&meta,false);
    p4_doom_gc_quit();assert(!gc.failed && !quit_requests);stale_packets_after_quit(&meta);
    assert_loading(P4_DOOM_LOADING_IDLE,0,0);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS real SHA/restore failure aborts guest; delayed callbacks after abort/cancel only count rejections");
}
static void guest_transfer_deadlines(void)
{
    for (unsigned total=0;total<2;++total) {
        setup(true);assert(!boundary());p4_ct_meta meta=guest_meta(150);
        inject_meta(&meta);assert(gc.checkpoint_rx.active);clear_output();
        if (total) {
            p4_ct_tx sender;assert(p4_ct_tx_start(&sender,&meta,fixture_bytes,sizeof(fixture_bytes),verify,NULL,clock_ms));
            uint8_t chunk[P4_CT_PACKET_MAX];size_t length;uint32_t index;
            assert(p4_ct_tx_prepare(&sender,clock_ms,chunk,sizeof(chunk),&length,&index) && index==0);
            for (unsigned step=0;step<4;++step) {
                clock_ms+=20000;inject(1,100,P4_MP_PACKET_CHECKPOINT,chunk,length);
                assert(!gc.failed);clear_output();
            }
            clock_ms+=10000; /* Fresh peer traffic at 80 s; total expires at 90 s. */
        } else clock_ms+=P4_CT_SILENCE_MS;
        inject(1,100,P4_MP_PACKET_PING,(const uint8_t *)"GCAHOST!",8);
        assert(!gc.failed && gc.prepared && !quit_requests);clear_output();p4_doom_gc_poll();
        assert(gc.failed && !gc.prepared && quit_requests==1 && !restores && !engine_rebases);
        assert_loading(P4_DOOM_LOADING_FAILED,0,0);
        assert(!live_allocations && !codec_active && boundary());
        assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    }
    /* A verified snapshot can wait only the bounded service lease for a safe
     * restore boundary, even while its host supplies current heartbeats. */
    setup(true);assert(!boundary());p4_ct_meta meta=guest_meta(150);deliver_guest(&meta,false);
    clock_ms+=P4_CS_LEASE_MS;inject(1,100,P4_MP_PACKET_PING,(const uint8_t *)"GCAHOST!",8);
    clear_output();p4_doom_gc_poll();
    assert(gc.failed && !gc.prepared && quit_requests==1 && !restores && !engine_rebases);
    assert_loading(P4_DOOM_LOADING_FAILED,0,0);
    assert(!live_allocations && !codec_active && boundary());
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS public poll cleanly aborts cold guest on transfer silence, total deadline and restore lease");
}
static void prepare_cleanup(void)
{
    const p4_doom_p4mp_transport_t transport={.set_handler=set_handler,.send_to=send_to,
        .poll=transport_poll,.connected=connected};
    for (unsigned invalid=0;invalid<2;++invalid) {
        setup(false);assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
        assert(!live_allocations && !codec_active);const unsigned before=allocations,before_init=init_calls;
        assert(p4_mp_session_accept_peer(&fixture_session,101,22,1,1,clock_ms)==P4_MP_OK);
        if (invalid) fixture_session.peers[0].player_slot=4;
        assert(p4_doom_gc_prepare(&fixture_session,&cfg,&transport)==ESP_ERR_INVALID_STATE);
        assert(!gc.prepared && allocations==before && init_calls==before_init && !live_allocations && !codec_active);
    }
    for (unsigned failure=0;failure<3;++failure) {
        setup(false);const unsigned before=allocations,before_shutdown=shutdown_calls;
        handler_fail=failure==0;init_fail=failure==1;identity_fail=failure==2;
        const esp_err_t result=p4_doom_gc_prepare(&fixture_session,&cfg,&transport);
        assert(result==(handler_fail?ESP_FAIL:ESP_ERR_NO_MEM));
        assert(allocations>before && deallocations==allocations && !live_allocations);
        assert(!gc.prepared && !gc.journal_storage && !gc.checkpoint_storage[0] && !gc.checkpoint_storage[1]);
        assert(!codec_active && shutdown_calls==before_shutdown+2 && !quit_requests);
    }
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS invalid peer/roster avoids allocations; handler/init/identity failures release owned resources");
}

/* Exercise the public poll with real canonical delivery. D_ReceiveTic advances
 * the fake timer, so the second frame expires the service at a newer time than
 * poll_runtime's earlier cached sample. All protocol/service code is actual. */
static void host_delivery_clock_order(void)
{
    setup(false);host_advance(8);join(1);control(1,4,0,0,0);
    assert(!boundary() && captures==1 && p4_cs_meta(&gc.checkpoints,1));
    const uint32_t at=played;
    const uint64_t captured=gc.checkpoints.last_now_ms;
    const ticcmd_t command={0};
    p4_doom_gc_submit(&command,(int)received);
    p4_doom_gc_submit(&command,(int)received+1);
    assert(!gc.failed);delivery_elapsed_ms=1;clear_output();tick();
    assert(received==at+2 && clock_ms==captured+22);
    assert(gc.checkpoints.last_now_ms>=captured+21);
    fprintf(stderr,"CLOCK_ORDER elapsed=%llu failed_slots=%u retained=%u packets=%u\n",
        (unsigned long long)(clock_ms-captured),(unsigned)gc.checkpoints.failed_slots,
        p4_cs_meta(&gc.checkpoints,1)?1U:0U,checkpoint_packets);
    if (getenv("P4_EXPECT_STALE_CLOCK_FAILURE")) {
        assert(gc.checkpoints.failed_slots==2 && !p4_cs_meta(&gc.checkpoints,1));
        assert(gc.resuming==2 && !abort_reason && !gc.failed);
        clear_output();tick();
        assert(abort_reason==5 && !gc.resuming && !gc.failed);
        assert(!gc.checkpoints.failed_slots && !p4_cs_meta(&gc.checkpoints,1));
        assert(gc.sync.mask==1 && !gc.routes[1]);
        assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
        puts("PASS baseline reproduces latched slot 1 failure, released snapshot, reason 5 and surviving host");
        return;
    }
    assert(!gc.checkpoints.failed_slots && p4_cs_meta(&gc.checkpoints,1));
    assert(gc.resuming==2 && !abort_reason && !gc.failed && checkpoint_packets);
    p4_ct_rx receiver={0};uint8_t storage[FIXTURE_BYTES];bool dropped=true;
    host_wire_step(&receiver,storage,&dropped);
    for (unsigned i=0;i<100 && !p4_cs_complete(&gc.checkpoints,1);++i) {
        const uint32_t next=received;
        p4_doom_gc_submit(&command,(int)next);
        p4_doom_gc_submit(&command,(int)next+1);
        clear_output();tick();host_wire_step(&receiver,storage,&dropped);
        assert(received==next+2 && !gc.failed && !gc.checkpoints.failed_slots);
        assert(gc.resuming==2 && !abort_reason && p4_cs_meta(&gc.checkpoints,1));
    }
    assert(receiver.verified && p4_cs_complete(&gc.checkpoints,1) && !dropped);
    assert(!memcmp(storage,fixture_bytes,sizeof(storage)));
    assert(!restores && !engine_rebases && played>at);
    /* Completion remains pinned until restore/activation retires it. */
    assert(p4_cs_meta(&gc.checkpoints,1) && !gc.checkpoints.failed_slots);
    /* Truly reversed external time must still fail closed through the actual
     * adapter and unchanged coordinator; the fix must never clamp time. */
    clock_ms=gc.checkpoints.last_now_ms-1;clear_output();p4_doom_gc_poll();
    assert(abort_reason==5 && !gc.resuming && !gc.failed);
    assert(!p4_cs_meta(&gc.checkpoints,1) && gc.sync.mask==1);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    puts("PASS delivery advances time: transfer completes with loss, snapshot stays pinned, genuine backward time still aborts");
}

int main(void)
{
    host_delivery_clock_order();
    if (getenv("P4_EXPECT_STALE_CLOCK_FAILURE")) return 0;
    host_capture_transfer_suffix();host_coalesces_and_expires();host_waits_for_engine_departure();guest_restore_boundary_suffix();
    guest_metadata_gates();guest_integrity_and_restore_failure();guest_transfer_deadlines();prepare_cleanup();
    puts("PASS checkpoint adapter integration (instrumented engine, virtual queued transport)");
    return 0;
}
