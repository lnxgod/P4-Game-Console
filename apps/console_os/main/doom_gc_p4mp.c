// SPDX-License-Identifier: GPL-2.0-or-later
#include "doom_gc_p4mp.h"
#include "p4/doom_lockstep.h"
#include "p4/doom_arena.h"
#include "p4/doom_replay.h"
#include "p4/doom_resume.h"
#include "p4/doom_checkpoint_service.h"
#include "p4_doom_checkpoint.h"
#include <limits.h>
#include <stdlib.h>
#include "p4/multiplayer_group.h"
#include "platform/readonly_blob_loading.h"
#include "d_loop.h"
#include "doomgeneric.h"
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "mbedtls/sha256.h"
#endif
#if defined(ESP_PLATFORM) && defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
#include "console_debug.h"
#include "esp_heap_caps.h"
#endif
#if defined(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#define P4_ARENA_LARGE_BSS EXT_RAM_BSS_ATTR
#else
#define P4_ARENA_LARGE_BSS
#endif

enum {
    P4_DOOM_GC_STARTUP_SILENCE_MS = 30000,
    P4_DOOM_GC_STARTUP_LIMIT_MS = 300000,
    P4_DOOM_GC_REPLAY_BYTES = 4 * 1024 * 1024,
    P4_DOOM_GC_SUFFIX_BYTES = 120 * 35 * P4_DOOM_LOCKSTEP_BYTES,
    P4_DOOM_GC_HOST_SEND_BUDGET = 16,
    P4_DOOM_GC_REPLAY_CONTROL_BYTES = 24,
    P4_DOOM_GC_REPLAY_PIVOT_AHEAD = 64,
    P4_DOOM_GC_RESUME_ATTEMPTS = 64,
    P4_DOOM_GC_EPOCH_HEADER_BYTES = 16,
};

/* This bounded command history is task-owned, never accessed by an ISR or DMA.
 * Keep it out of the internal heap needed by startup and peripheral drivers. */
static P4_ARENA_LARGE_BSS struct {
    p4_mp_session_t *session;
    p4_doom_mp_launch_config_t config;
    p4_doom_p4mp_transport_t transport;
    p4_doom_lockstep_t sync;
    uint64_t routes[4], progress_ms[4], next_send_ms, next_keepalive_ms;
    uint64_t startup_seen_ms[4], prepared_ms;
    uint64_t rx_started_us;
    bool rx_seen, rx_drained, rx_was_live;
    uint64_t loading_since_ms[4], loading_seen_ms[4], local_loading_since_ms;
    uint32_t input_seen[4];
    uint64_t next_start_reply_ms[4];
    uint64_t next_startup_memory_ms;
    p4_doom_replay_journal_t journal;
    uint8_t *journal_storage;
    /* Protocol 8: two shared host snapshots or one private cold-guest buffer.
     * Never DMA, never allocated per poll/capture, never a host restore target. */
    p4_cs_host checkpoints;
    p4_ct_rx checkpoint_rx;
    uint8_t *checkpoint_storage[2];
    uint8_t checkpoint_restored;
    bool checkpoint_mode, checkpoint_content_ready, checkpoint_guest_restored;
    uint64_t checkpoint_started_ms, checkpoint_control_ms;
    uint32_t checkpoint_capture_max_us;

    uint32_t original_peer[4], replay_cursor[4], replay_played[4], activation[4];
    uint8_t tickets[4][P4_DOOM_RESUME_TICKET_BYTES], replay_credit[4];
    uint64_t resume_nonce[4], resume_started_ms[4], resume_seen_ms[4];
    uint64_t used_nonces[4][P4_DOOM_GC_RESUME_ATTEMPTS];
    uint8_t used_nonce_count[4];
    /* Fresh attempts retain revoked bindings for the entire match. Otherwise
     * captured JOIN A could claim a virgin seat again after A -> B -> A. */
    struct {
        uint32_t peer, nonce;
        uint8_t ticket[P4_DOOM_RESUME_TICKET_BYTES], slot, requested_slot;
        bool revoked;
    } fresh_attempts[P4_DOOM_GC_RESUME_ATTEMPTS];
    uint32_t pending_peer[4];
    uint8_t fresh_attempt_count, fresh_pending;
    uint64_t next_ticket_ms, next_offer_ms, next_reject_ms;
    uint64_t next_replay_report_ms;
    uint32_t reported_cursor, reported_played, reported_pivot;
    /* Presentation reads copied owner-task values, never calls the engine
     * while a VFS/display callback may own another subsystem. */
    uint32_t loading_replay_base, loading_replay_played;
    uint8_t reported_credit;
    uint8_t tickets_acked, resuming, resume_armed, replay_next_slot;
    bool resume_enabled, replaying, replay_input_ready;
    uint32_t saved_timeout;
    p4_doom_net_stats_t stats;
    uint8_t ready, loading_peers;
    bool prepared, configuring, configured, failed, local_loading;
    bool live_ready_sent_in_poll;
    uint32_t live_ready_ack_in_poll;
} gc;
static void count_stat(uint32_t *value)
{ if (*value!=UINT32_MAX) ++*value; }
void p4_doom_gc_get_stats(p4_doom_net_stats_t *stats)
{ if (stats) *stats=gc.stats; }
void p4_doom_gc_get_loading_progress(p4_doom_loading_progress_t *progress)
{
    if (!progress) return;
    *progress=(p4_doom_loading_progress_t){.phase=P4_DOOM_LOADING_IDLE};
    /* Keep the failure visible after teardown until the next prepare resets
     * this attempt. This does not extend its session or retain its buffers. */
    if (gc.failed) { progress->phase=P4_DOOM_LOADING_FAILED;return; }
    if (!gc.prepared) return;
    if (gc.replaying) {
        if (!gc.configuring || (gc.checkpoint_mode && !gc.checkpoint_content_ready)) return;
        if (gc.checkpoint_mode && !gc.checkpoint_guest_restored) {
            progress->phase=P4_DOOM_LOADING_CHECKPOINT;
            if (gc.checkpoint_rx.active) {
                progress->total=gc.checkpoint_rx.meta.length;
                const uint64_t contiguous=(uint64_t)gc.checkpoint_rx.base*P4_CT_CHUNK_BYTES;
                progress->completed=contiguous<progress->total
                    ?(uint32_t)contiguous:progress->total;
            }
            return;
        }
        progress->phase=P4_DOOM_LOADING_CATCHUP;
        const uint32_t target=gc.activation[gc.config.local_player_slot];
        if (target>gc.loading_replay_base) {
            progress->total=target-gc.loading_replay_base;
            progress->completed=gc.loading_replay_played>gc.loading_replay_base
                ?gc.loading_replay_played-gc.loading_replay_base:0;
            if (progress->completed>progress->total) progress->completed=progress->total;
        }
        return;
    }
    if (gc.configured) progress->phase=P4_DOOM_LOADING_READY;
    else if (gc.configuring) progress->phase=P4_DOOM_LOADING_WAITING_HOST;
}
static uint64_t millis(void) { return (uint64_t)esp_timer_get_time()/1000U; }
static bool host(void) { return gc.config.local_player_slot==0; }
static void put32(uint8_t *p, uint32_t v)
{ for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(v>>(8U*i)); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }

/* Console OS replaces this with its checked RTC record writer. Host fixtures
 * may exercise the adapter without an OS restart/persistence implementation. */
__attribute__((weak)) bool p4_doom_arena_resume_store(
    const p4_doom_mp_launch_config_t *config, const uint8_t ticket[16])
{ (void)config; (void)ticket; return true; }

static void make_ticket(uint8_t ticket[16])
{
#if defined(ESP_PLATFORM)
    esp_fill_random(ticket,16);
#else
    /* Deterministic host-test substitute; never used by device firmware. */
    static uint32_t serial=1;
    for (unsigned i=0;i<16;++i) ticket[i]=(uint8_t)(serial+i+1U);
    ++serial;
#endif
    /* The codec reserves the all-zero token; preserve 127 random bits. */
    ticket[0]|=1U;
}

static void *checkpoint_allocate(size_t length)
{
#if defined(ESP_PLATFORM)
    /* This optional CPU-only history must never consume scarce DMA/internal RAM. */
    return heap_caps_malloc(length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return malloc(length);
#endif
}

/* Host-only fixtures provide a real digest implementation; an absent provider
 * disables checkpoint service rather than substituting a weak checksum. */
#if !defined(ESP_PLATFORM)
extern bool p4_doom_checkpoint_sha256(const uint8_t *,size_t,uint8_t[32])
    __attribute__((weak));
#endif
static bool checkpoint_hash(void *context,const uint8_t *bytes,size_t length,uint8_t out[32])
{
    (void)context;
#if defined(ESP_PLATFORM)
    return mbedtls_sha256(bytes,length,out,0)==0;
#else
    return p4_doom_checkpoint_sha256 && p4_doom_checkpoint_sha256(bytes,length,out);
#endif
}
static bool checkpoint_verify(void *context,const uint8_t *bytes,size_t length,
    const uint8_t expected[32])
{
    uint8_t digest[32];
    return checkpoint_hash(context,bytes,length,digest) && !memcmp(digest,expected,32);
}
static void checkpoint_release(void)
{
    free(gc.checkpoint_storage[0]);free(gc.checkpoint_storage[1]);
    gc.checkpoint_storage[0]=gc.checkpoint_storage[1]=NULL;
    P4_DoomCheckpointShutdown();
}

boolean P4_DoomNetReplaying(void)
{ return gc.prepared && gc.replaying && !gc.failed; }

unsigned int P4_DoomNetInitialPlayerMask(void)
{ return gc.configured ? gc.config.initial_player_mask : 0U; }

static uint8_t initial_count(void)
{
    uint8_t count=0;
    for (unsigned i=0;i<gc.config.player_count;++i)
        if (gc.config.initial_player_mask & (1U<<i)) ++count;
    return count;
}


/* The application may draw a compiled-in loading view. It must not read WADs
 * or call the engine: this hook also runs under the readonly VFS lock. */
__attribute__((weak)) void p4_doom_startup_status(bool waiting) { (void)waiting; }

/* Owner-only scalar observation. Applications may defer heap sampling until
 * the engine call returns; the default does no work. */
__attribute__((weak)) void p4_doom_memory_checkpoint(bool restored) { (void)restored; }

static void session_timeout(void)
{
    if (!gc.configured) return;
    const bool loading=gc.local_loading || gc.loading_peers!=0 || gc.resuming!=0 || gc.replaying;
    gc.session->timeout_ms=loading && gc.saved_timeout<P4_DOOM_GC_STARTUP_SILENCE_MS
        ? P4_DOOM_GC_STARTUP_SILENCE_MS : gc.saved_timeout;
}

static void peer_progress(uint8_t slot)
{
    gc.progress_ms[slot]=millis();
    gc.loading_peers &= (uint8_t)~(1U<<slot);
}

/* GCE1 binds all runtime traffic on a rebound route to its admission nonce.
 * Validate this envelope before session sequence/liveness mutation: Wi-Fi routes
 * identify IPs and can be reused after a reboot or an expired provisional lease. */
static uint64_t runtime_nonce(uint8_t slot)
{ return host()?gc.resume_nonce[slot]:gc.config.resume_nonce; }

static bool runtime_payload_valid(p4_mp_packet_type_t type,const uint8_t *bytes,uint16_t n)
{
    if (!bytes) return false;
    if (type==P4_MP_PACKET_INPUT) {
        p4_mp_input_t input;
        return p4_mp_input_decode(bytes,n,&input)==P4_MP_OK;
    }
    if (type==P4_MP_PACKET_PING) return n==8;
    if (type==P4_MP_PACKET_LEAVE) return n==2;
    return type==P4_MP_PACKET_GAME_MESSAGE && n>0 &&
        n<=P4_MP_GAME_MESSAGE_MAX_BYTES-P4_DOOM_GC_EPOCH_HEADER_BYTES;
}

static bool runtime_unwrap(const p4_mp_packet_view_t *outer,uint64_t nonce,
    p4_mp_packet_view_t *inner)
{
    const uint8_t *b=outer->payload;
    if (!nonce || outer->type!=P4_MP_PACKET_GAME_MESSAGE ||
        outer->payload_length<=P4_DOOM_GC_EPOCH_HEADER_BYTES || memcmp(b,"GCE1",4) ||
        b[13] || b[14] || b[15] ||
        ((uint64_t)get32(b+4)|((uint64_t)get32(b+8)<<32))!=nonce) return false;
    *inner=*outer;
    inner->type=(p4_mp_packet_type_t)b[12];
    inner->payload=b+P4_DOOM_GC_EPOCH_HEADER_BYTES;
    inner->payload_length=(uint16_t)(outer->payload_length-P4_DOOM_GC_EPOCH_HEADER_BYTES);
    return runtime_payload_valid(inner->type,inner->payload,inner->payload_length);
}

static uint32_t outgoing_ack(p4_mp_packet_type_t type)
{
    return !host() && gc.replaying && type==P4_MP_PACKET_INPUT
        ? gc.activation[gc.config.local_player_slot] : gc.sync.next_output;
}

static bool send_route_ack(uint64_t route,p4_mp_packet_type_t type,
    const uint8_t *payload,uint16_t length,uint32_t ack)
{
    count_stat(&gc.stats.tx_attempts);
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];size_t n=0;
    if (p4_mp_session_encode(gc.session,type,ack,payload,length,
            datagram,sizeof(datagram),&n)!=P4_MP_OK) {
        count_stat(&gc.stats.tx_failures);return false;
    } else if (gc.transport.send_to(gc.transport.context,route,datagram,n)!=ESP_OK) {
        count_stat(&gc.stats.tx_failures);return false;
    }
    return true;
}

/* Admission responses remain GCR2 so the OS can persist the ticket before
 * handing over to Doom. Only send_packet emits admitted runtime traffic. */
static bool send_route(uint64_t route,p4_mp_packet_type_t type,
    const uint8_t *payload,uint16_t length)
{ return send_route_ack(route,type,payload,length,outgoing_ack(type)); }

static bool send_packet(uint8_t recipient,p4_mp_packet_type_t type,
    const uint8_t *payload,uint16_t length)
{
    const uint64_t nonce=runtime_nonce(recipient);
    if (!nonce) return send_route(gc.routes[recipient],type,payload,length);
    if (!runtime_payload_valid(type,payload,length)) {
        count_stat(&gc.stats.tx_attempts);count_stat(&gc.stats.tx_failures);return false;
    }
    uint8_t b[P4_MP_GAME_MESSAGE_MAX_BYTES]={ 'G','C','E','1' };
    put32(b+4,(uint32_t)nonce);put32(b+8,(uint32_t)(nonce>>32));b[12]=(uint8_t)type;
    memcpy(b+P4_DOOM_GC_EPOCH_HEADER_BYTES,payload,length);
    return send_route_ack(gc.routes[recipient],P4_MP_PACKET_GAME_MESSAGE,b,
        (uint16_t)(length+P4_DOOM_GC_EPOCH_HEADER_BYTES),outgoing_ack(type));
}

static void send_resume(uint64_t route,const p4_doom_resume_control_t *control)
{
    uint8_t bytes[P4_DOOM_RESUME_CONTROL_BYTES]; size_t n=0;
    if (p4_doom_resume_control_encode(control,bytes,&n))
        send_route(route,P4_MP_PACKET_GAME_MESSAGE,bytes,(uint16_t)n);
}

static p4_doom_resume_control_t resume_control(uint8_t slot,p4_doom_resume_type_t type)
{
    p4_doom_resume_control_t c={.type=type,.slot=slot,
        .player_count=gc.config.player_count,.nonce=gc.resume_nonce[slot],
        .initial_player_mask=gc.config.initial_player_mask};
    memcpy(c.ticket,gc.tickets[slot],sizeof(c.ticket));
    return c;
}

/* Session framing checks an already admitted identity and route. GCP1 controls never
 * allocate peers: progress includes both received and actually simulated tics;
 * credit is bounded by the receiver's canonical ring and engine ring. */
static void send_replay_control(uint8_t recipient,uint8_t type,uint32_t cursor,
    uint32_t played,uint32_t pivot,uint8_t credit)
{
    uint8_t b[P4_DOOM_GC_REPLAY_CONTROL_BYTES]={ 'G','C','P','1',type,
        host()?recipient:gc.config.local_player_slot,credit,0 };
    put32(b+8,cursor);put32(b+12,played);put32(b+16,pivot);
    send_packet(recipient,P4_MP_PACKET_GAME_MESSAGE,b,sizeof(b));
}

/* Queue reservation is provisional. Only producing the canonical activation
 * frame makes a seat permanently owned, even when its ACK or delivery is lost. */
static void commit_fresh_ownership(void)
{
    for (uint8_t i=1;i<gc.config.player_count;++i) {
        const uint8_t bit=(uint8_t)(1U<<i);
        if ((gc.fresh_pending & gc.sync.mask & bit) && gc.activation[i] &&
            gc.sync.next_output>gc.activation[i]) {
            gc.original_peer[i]=gc.pending_peer[i];
            gc.pending_peer[i]=0;
            gc.fresh_pending &= (uint8_t)~bit;
        }
    }
}

static void revoke_fresh(uint8_t slot)
{
    const uint8_t bit=(uint8_t)(1U<<slot);
    if (!(gc.fresh_pending & bit)) return;
    for (unsigned i=0;i<gc.fresh_attempt_count;++i)
        if (gc.fresh_attempts[i].slot==slot &&
            gc.fresh_attempts[i].peer==gc.pending_peer[slot] &&
            !memcmp(gc.fresh_attempts[i].ticket,gc.tickets[slot],16))
            gc.fresh_attempts[i].revoked=true;
    gc.fresh_pending &= (uint8_t)~bit;
    gc.pending_peer[slot]=0;
    gc.resume_nonce[slot]=0;
    memset(gc.tickets[slot],0,sizeof(gc.tickets[slot]));
    gc.ready &= (uint8_t)~bit;
    gc.tickets_acked &= (uint8_t)~bit;
}

static void departed(uint8_t slot)
{
    if (slot>=gc.config.player_count) return;
    if (gc.routes[slot] && !gc.failed) count_stat(&gc.stats.peer_departures);
    if (!host() || !gc.configured || slot==0) {
        gc.failed=true;
        gc.session->timeout_ms=gc.saved_timeout;
        return;
    }
    if (gc.checkpoint_mode) p4_cs_retire(&gc.checkpoints,slot);
    gc.checkpoint_restored &= (uint8_t)~(1U<<slot);
    commit_fresh_ownership();
    revoke_fresh(slot);
    (void)p4_doom_lockstep_depart(&gc.sync,slot);
    p4_mp_event_t ignored;
    if (gc.routes[slot])
        (void)p4_mp_session_route_disconnected(gc.session,gc.routes[slot],&ignored);
    gc.routes[slot]=0;
    gc.resuming &= (uint8_t)~(1U<<slot);
    gc.resume_armed &= (uint8_t)~(1U<<slot);
    gc.activation[slot]=0;
    gc.replay_credit[slot]=0;
    gc.loading_peers &= (uint8_t)~(1U<<slot);
    ESP_LOGI("doom_gc", "GAME_CHANGERS_AI PLAYER_LEFT slot=%u", (unsigned)slot);
}

static void resume_abort(uint8_t slot,uint8_t reason)
{
    send_replay_control(slot,3,0,0,reason,0);
    ESP_LOGI("doom_gc","GAME_CHANGERS_AI REJOIN_FAILED slot=%u reason=%u",
        (unsigned)slot,(unsigned)reason);
    departed(slot);
}

/* Runtime admission owns every JOIN. It allocates only a virgin reserved seat;
 * the generic session allocator must never create an unvalidated pending peer.
 * Tickets are bearer credentials on the current unencrypted Wi-Fi transport. */
static bool nonce_used(uint8_t slot,uint64_t nonce)
{
    for (unsigned i=0;i<gc.used_nonce_count[slot];++i)
        if (gc.used_nonces[slot][i]==nonce) return true;
    return false;
}

static void send_accepted(uint8_t slot,uint64_t route)
{
    p4_doom_resume_control_t c=resume_control(slot,P4_DOOM_RESUME_ACCEPTED);
    c.accept=(p4_mp_lobby_accept_t){.assigned_player_slot=slot,
        .player_count=gc.config.player_count,.input_delay_tics=gc.config.input_delay_tics,
        .session_seed=gc.config.session_seed};
    memcpy(c.accept.game_settings,gc.config.lobby_offer.game_settings,
        sizeof(c.accept.game_settings));
    send_resume(route,&c);
}

static void begin_replay(uint8_t slot,uint64_t route,uint64_t nonce)
{
    if (gc.checkpoint_mode) p4_cs_retire(&gc.checkpoints,slot);
    gc.checkpoint_restored &= (uint8_t)~(1U<<slot);
    gc.routes[slot]=route;gc.resume_nonce[slot]=nonce;
    gc.resuming |= (uint8_t)(1U<<slot);
    gc.resume_armed &= (uint8_t)~(1U<<slot);
    gc.replay_cursor[slot]=gc.replay_played[slot]=gc.activation[slot]=0;
    gc.replay_credit[slot]=0;
    gc.resume_started_ms[slot]=gc.resume_seen_ms[slot]=millis();
}

static void reject_join(uint64_t route)
{
    if (millis()<gc.next_reject_ms) return;
    const uint8_t unavailable[2]={1,0};
    gc.next_reject_ms=millis()+100;
    send_route(route,P4_MP_PACKET_REJECT,unavailable,sizeof(unavailable));
}

static void fresh_join(uint64_t route,const uint8_t *data,size_t n,
    const p4_mp_packet_view_t *p)
{
    p4_mp_lobby_join_t join;
    if (p4_mp_lobby_join_decode(p->payload,p->payload_length,&join)!=P4_MP_OK)
        return;
    if (!gc.resume_enabled || !gc.configured ||
        !p4_mp_lobby_join_matches_offer(&gc.config.lobby_offer,&join) ||
        p->peer_id==gc.config.self_peer_id) { reject_join(route);return; }
    for (uint8_t i=1;i<gc.config.player_count;++i) {
        /* A departed owner's identity cannot consume another virgin seat. */
        if (gc.original_peer[i]==p->peer_id) { reject_join(route);return; }
        if (gc.pending_peer[i]!=p->peer_id) continue;
        if (gc.routes[i]!=route || gc.resume_nonce[i]!=join.join_nonce) {
            reject_join(route);return;
        }
        bool exact=false;
        for (unsigned j=0;j<gc.fresh_attempt_count;++j)
            if (!gc.fresh_attempts[j].revoked && gc.fresh_attempts[j].peer==p->peer_id &&
                gc.fresh_attempts[j].nonce==join.join_nonce &&
                gc.fresh_attempts[j].requested_slot==join.requested_player_slot) exact=true;
        p4_mp_event_t event;
        if (exact && p4_mp_session_receive(gc.session,route,millis(),data,n,&event)==P4_MP_OK)
            send_accepted(i,route);
        /* A retry may recover a lost reply, but never extend its absolute lease. */
        return;
    }
    if (gc.fresh_attempt_count>=P4_DOOM_GC_RESUME_ATTEMPTS ||
        !p4_doom_replay_journal_available(&gc.journal)) { reject_join(route);return; }
    for (unsigned i=0;i<gc.fresh_attempt_count;++i)
        if (gc.fresh_attempts[i].peer==p->peer_id &&
            gc.fresh_attempts[i].nonce==join.join_nonce) { reject_join(route);return; }
    uint8_t slot=0;
    for (uint8_t i=1;i<gc.config.player_count;++i)
        if (!gc.original_peer[i] && !gc.pending_peer[i] && !gc.routes[i] &&
            !(gc.sync.mask & (1U<<i)) &&
            (join.requested_player_slot==P4_MP_PLAYER_SLOT_ANY || join.requested_player_slot==i)) {
            slot=i;break;
        }
    if (!slot || p4_mp_session_accept_peer(gc.session,p->peer_id,route,slot,
            p->sequence,millis())!=P4_MP_OK) { reject_join(route);return; }
    make_ticket(gc.tickets[slot]);
    gc.pending_peer[slot]=p->peer_id;
    gc.fresh_pending |= (uint8_t)(1U<<slot);
    const unsigned attempt=gc.fresh_attempt_count++;
    gc.fresh_attempts[attempt].peer=p->peer_id;
    gc.fresh_attempts[attempt].nonce=join.join_nonce;
    gc.fresh_attempts[attempt].slot=slot;
    gc.fresh_attempts[attempt].requested_slot=join.requested_player_slot;
    memcpy(gc.fresh_attempts[attempt].ticket,gc.tickets[slot],16);
    /* The fresh nonce is also retired for future owned-seat returns. */
    gc.used_nonces[slot][gc.used_nonce_count[slot]++]=join.join_nonce;
    begin_replay(slot,route,join.join_nonce);
    ESP_LOGI("doom_gc","GAME_CHANGERS_AI ADMISSION_ACCEPTED side=host kind=fresh slot=%u frontier=%u",
        (unsigned)slot,(unsigned)gc.journal.next_tick);
    send_accepted(slot,route);
}

static bool runtime_admission(uint64_t route,const uint8_t *data,size_t n)
{
    if (!host() || !gc.prepared || gc.failed || !route) return false;
    p4_mp_packet_view_t p;
    if (p4_mp_packet_decode(data,n,&p)!=P4_MP_OK) return false;
    if (p.type==P4_MP_PACKET_DISCOVER) {
        if (gc.resume_enabled && gc.configured && millis()>=gc.next_offer_ms) {
            gc.next_offer_ms=millis()+100;
            p4_mp_lobby_offer_t running=gc.config.lobby_offer;
            running.players_present=1;
            for (uint8_t i=1;i<gc.config.player_count;++i)
                if (gc.original_peer[i] || gc.pending_peer[i]) ++running.players_present;
            if (!p4_doom_replay_journal_available(&gc.journal) ||
                gc.fresh_attempt_count>=P4_DOOM_GC_RESUME_ATTEMPTS)
                running.players_present=running.player_capacity;
            uint8_t offer[P4_MP_OFFER_PAYLOAD_BYTES];
            if (p4_mp_lobby_offer_encode(&running,offer)==P4_MP_OK)
                send_route(route,P4_MP_PACKET_OFFER,offer,sizeof(offer));
        }
        return true;
    }
    if (p.type==P4_MP_PACKET_JOIN) {
        if (p.session_id==gc.config.session_id) fresh_join(route,data,n,&p);
        return true;
    }
    p4_doom_resume_control_t c;
    if (p.type!=P4_MP_PACKET_GAME_MESSAGE || p.session_id!=gc.config.session_id ||
        !p4_doom_resume_control_decode(p.payload,p.payload_length,&c) ||
        c.type!=P4_DOOM_RESUME_REQUEST) return false;
    const uint8_t slot=c.slot;
    if (!gc.resume_enabled || !gc.configured || slot>=gc.config.player_count ||
        c.player_count!=gc.config.player_count ||
        c.initial_player_mask!=gc.config.initial_player_mask ||
        memcmp(c.compatibility_sha256,gc.config.lobby_offer.compatibility_sha256,
            sizeof(c.compatibility_sha256))) return true;
    for (unsigned i=0;i<gc.fresh_attempt_count;++i)
        if (gc.fresh_attempts[i].revoked && gc.fresh_attempts[i].peer==p.peer_id &&
            gc.fresh_attempts[i].slot==slot &&
            !memcmp(gc.fresh_attempts[i].ticket,c.ticket,16)) {
            c.type=P4_DOOM_RESUME_UNAVAILABLE;
            c.reason=P4_DOOM_RESUME_REASON_PROVISIONAL_EXPIRED;
            send_resume(route,&c);return true;
        }
    if ((gc.fresh_pending & (1U<<slot)) && p.peer_id==gc.pending_peer[slot] &&
        !memcmp(c.ticket,gc.tickets[slot],sizeof(c.ticket))) {
        /* A restarted provisional guest cannot replace its existing route.
         * Its bounded lease will revoke this credential if never activated. */
        c.type=P4_DOOM_RESUME_UNAVAILABLE;c.reason=1;
        send_resume(route,&c);return true;
    }
    if (p.peer_id!=gc.original_peer[slot] ||
        memcmp(c.ticket,gc.tickets[slot],sizeof(c.ticket))) return true;
    const uint8_t bit=(uint8_t)(1U<<slot);
    if (gc.resuming & bit) {
        if (gc.routes[slot]!=route || gc.resume_nonce[slot]!=c.nonce) return true;
        p4_mp_event_t event;
        if (p4_mp_session_receive(gc.session,route,millis(),data,n,&event)!=P4_MP_OK)
            return true;
    } else {
        c.type=P4_DOOM_RESUME_UNAVAILABLE;c.reason=1;
        if ((gc.sync.mask & bit) || gc.routes[slot] || nonce_used(slot,c.nonce) ||
            gc.used_nonce_count[slot]>=P4_DOOM_GC_RESUME_ATTEMPTS ||
            !p4_doom_replay_journal_available(&gc.journal)) {
            send_resume(route,&c);return true;
        }
        if (p4_mp_session_accept_peer(gc.session,p.peer_id,route,slot,p.sequence,millis())!=P4_MP_OK)
            return true;
        gc.used_nonces[slot][gc.used_nonce_count[slot]++]=c.nonce;
        begin_replay(slot,route,c.nonce);
        ESP_LOGI("doom_gc","GAME_CHANGERS_AI REJOIN_ACCEPTED slot=%u frontier=%u",
            (unsigned)slot,(unsigned)gc.journal.next_tick);
        ESP_LOGI("doom_gc","GAME_CHANGERS_AI ADMISSION_ACCEPTED side=host kind=return slot=%u frontier=%u",
            (unsigned)slot,(unsigned)gc.journal.next_tick);
    }
    send_accepted(slot,route);
    return true;
}

static bool resume_message(uint8_t slot,const p4_mp_packet_view_t *p)
{
    if (p->type!=P4_MP_PACKET_GAME_MESSAGE) return false;
    p4_doom_resume_control_t c;
    if (p4_doom_resume_control_decode(p->payload,p->payload_length,&c)) {
        if (c.player_count!=gc.config.player_count ||
            c.initial_player_mask!=gc.config.initial_player_mask) return true;
        if (host() && c.type==P4_DOOM_RESUME_TICKET_ACK && c.slot==slot &&
            !memcmp(c.ticket,gc.tickets[slot],sizeof(c.ticket))) {
            gc.tickets_acked |= (uint8_t)(1U<<slot);
            gc.startup_seen_ms[slot]=millis();
        } else if (!host() && slot==0 && c.type==P4_DOOM_RESUME_TICKET &&
            c.slot==gc.config.local_player_slot && !gc.configured &&
            p4_doom_arena_resume_store(&gc.config,c.ticket)) {
            memcpy(gc.tickets[c.slot],c.ticket,sizeof(c.ticket));
            gc.tickets_acked |= (uint8_t)(1U<<c.slot);
            c.type=P4_DOOM_RESUME_TICKET_ACK;send_resume(gc.routes[0],&c);
        }
        return true;
    }
    const uint8_t *b=p->payload;
    if (p->payload_length!=P4_DOOM_GC_REPLAY_CONTROL_BYTES || memcmp(b,"GCP1",4))
        return false;
    if (b[7] || get32(b+20) || b[5]!=(host()?slot:gc.config.local_player_slot)) return true;
    const uint32_t cursor=get32(b+8),played=get32(b+12),pivot=get32(b+16);
    if (host() && gc.checkpoint_mode && (gc.resuming & (1U<<slot))) {
        if (b[4]==4 && !b[6] && !cursor && !played && !pivot) {
            if (p4_cs_request(&gc.checkpoints,slot,gc.resume_nonce[slot],millis()))
                gc.resume_seen_ms[slot]=millis();
            return true;
        }
        if (b[4]==5 && !b[6] && cursor==played && !pivot) {
            const p4_ct_meta *m=p4_cs_meta(&gc.checkpoints,slot);
            if (m && p4_cs_complete(&gc.checkpoints,slot) && cursor==m->next_tic) {
                if (!(gc.checkpoint_restored & (1U<<slot))) {
                    gc.replay_cursor[slot]=gc.replay_played[slot]=cursor;
                    gc.checkpoint_restored |= (uint8_t)(1U<<slot);
                }
                gc.resume_seen_ms[slot]=millis();
            }
            return true;
        }
        if (!(gc.checkpoint_restored & (1U<<slot))) return true;
    }
    if (host() && (gc.resuming & (1U<<slot)) && b[4]==1 &&
        b[6]<=P4_DOOM_LOCKSTEP_HISTORY && cursor>=gc.replay_cursor[slot] &&
        cursor<=gc.journal.next_tick && played>=gc.replay_played[slot] && played<=cursor &&
        (!gc.activation[slot] ||
            ((cursor<=gc.activation[slot] || (gc.resume_armed & (1U<<slot))) &&
                (!pivot || pivot==gc.activation[slot])))) {
        gc.replay_cursor[slot]=cursor;gc.replay_played[slot]=played;
        gc.replay_credit[slot]=b[6];gc.resume_seen_ms[slot]=millis();
        if ((gc.resume_armed & (1U<<slot)) && cursor>=gc.activation[slot]) {
            gc.resuming &= (uint8_t)~(1U<<slot);
            if (gc.checkpoint_mode) p4_cs_retire(&gc.checkpoints,slot);
            gc.progress_ms[slot]=millis();
        }
        if (!gc.activation[slot] && cursor==gc.journal.next_tick &&
            cursor-played<=8U && gc.sync.next_output<=INT_MAX-P4_DOOM_GC_REPLAY_PIVOT_AHEAD)
            gc.activation[slot]=gc.sync.next_output+P4_DOOM_GC_REPLAY_PIVOT_AHEAD;
    } else if (!host() && slot==0 && gc.replaying) {
        if (b[4]==3) { gc.failed=true;return true; }
        if (b[4]==2 && !b[6] && !cursor && !played && pivot>0 && pivot<=INT_MAX &&
            pivot>=gc.sync.next_output &&
            (!gc.activation[gc.config.local_player_slot] ||
                pivot==gc.activation[gc.config.local_player_slot])) {
            gc.activation[gc.config.local_player_slot]=pivot;
            gc.progress_ms[0]=millis();
        }
    }
    return true;
}

static bool checkpoint_send(void *context,uint8_t slot,const uint8_t *bytes,size_t length)
{
    (void)context;
    return slot<gc.config.player_count && gc.routes[slot] && length<=UINT16_MAX &&
        send_route(gc.routes[slot],P4_MP_PACKET_CHECKPOINT,bytes,(uint16_t)length);
}
static void checkpoint_ack(void)
{
    uint8_t ack[P4_CT_ACK_WIRE];size_t n;
    if (p4_ct_rx_ack(&gc.checkpoint_rx,ack,sizeof(ack),&n))
        (void)checkpoint_send(NULL,0,ack,n);
}
static bool checkpoint_receive(uint8_t slot,uint64_t route,const uint8_t *bytes,size_t length)
{
    p4_mp_packet_view_t outer;
    if (p4_mp_packet_decode(bytes,length,&outer)!=P4_MP_OK ||
        outer.type!=P4_MP_PACKET_CHECKPOINT) return false;
    /* Validate a copy first. Invalid transfer traffic must not advance the
     * shared session sequence or refresh peer liveness. Single owner task. */
    p4_mp_session_t candidate=*gc.session;p4_mp_event_t event;
    if (!gc.checkpoint_mode || !runtime_nonce(slot) ||
        p4_mp_session_receive_checkpoint(&candidate,route,millis(),bytes,length,&event)!=P4_MP_OK ||
        event.player_slot!=slot) goto reject;
    const uint8_t *b=event.packet.payload;const size_t n=event.packet.payload_length;
    p4_ct_result result=P4_CT_REJECT;
    if (host()) {
        if (!(gc.resuming & (1U<<slot))) goto reject;
        result=p4_cs_ack(&gc.checkpoints,slot,b,n,millis());
        if (result!=P4_CT_REJECT) gc.resume_seen_ms[slot]=millis();
    } else {
        if (slot!=0 || !gc.replaying || !gc.checkpoint_content_ready) goto reject;
        p4_ct_meta m;
        if (p4_ct_meta_decode(&m,b,n)) {
            if (m.identity.schema!=2 || m.identity.session!=gc.config.session_id ||
                m.identity.attempt!=gc.config.resume_nonce || m.next_tic>INT_MAX ||
                !(m.members & 1U) || (m.members & (1U<<gc.config.local_player_slot)) ||
                memcmp(m.identity.content_id,gc.config.lobby_offer.compatibility_sha256,32)) goto reject;
            if (gc.checkpoint_rx.active) {
                uint8_t prior[P4_CT_META_WIRE];size_t prior_n;
                if (!p4_ct_meta_encode(&gc.checkpoint_rx.meta,prior,sizeof(prior),&prior_n) ||
                    prior_n!=n || memcmp(prior,b,n)) goto reject;
                result=P4_CT_DUPLICATE;
            } else if (p4_ct_rx_start(&gc.checkpoint_rx,&m.identity,&m,
                    gc.checkpoint_storage[0],P4_CT_MAX_BYTES,checkpoint_verify,NULL,millis())) {
                gc.checkpoint_started_ms=millis();result=P4_CT_PROGRESS;
            }
        } else if (gc.checkpoint_rx.active) {
            result=p4_ct_rx_chunk(&gc.checkpoint_rx,b,n,millis());
        }
        if (result!=P4_CT_REJECT) gc.progress_ms[0]=millis();
        else if (gc.checkpoint_rx.failed) gc.failed=true;
    }
    if (result==P4_CT_REJECT) goto reject;
    *gc.session=candidate;
    if (!host()) checkpoint_ack();
    return true;
reject:
    count_stat(&gc.stats.rx_session_rejected);return true;
}

static void receive_frame(void *context, uint64_t route,
                           const uint8_t *datagram, size_t length)
{
    (void)context;
    count_stat(&gc.stats.rx_packets);
    /* The installed handler can outlive a failed prepare or cold-guest quit.
     * Never let a queued datagram reach retired checkpoint storage. */
    if (!gc.prepared || gc.failed) {
        count_stat(&gc.stats.rx_session_rejected);return;
    }
    if (runtime_admission(route,datagram,length)) return;
    p4_mp_event_t e;
    uint8_t route_slot=UINT8_MAX;
    for (uint8_t i=0;i<gc.config.player_count;++i)
        if (gc.routes[i]!=0 && gc.routes[i]==route) route_slot=i;
    /* Only explicit runtime admission can allocate a pending replay seat. */
    if (route_slot==UINT8_MAX) { count_stat(&gc.stats.rx_session_rejected); return; }
    if (gc.checkpoint_mode && checkpoint_receive(route_slot,route,datagram,length)) return;
    const uint64_t nonce=runtime_nonce(route_slot);
    p4_mp_packet_view_t outer,inner;
    if (nonce && (p4_mp_packet_decode(datagram,length,&outer)!=P4_MP_OK ||
        !runtime_unwrap(&outer,nonce,&inner))) {
        count_stat(&gc.stats.rx_session_rejected);return;
    }
    if (!gc.prepared || p4_mp_session_receive(gc.session,route,millis(),datagram,length,&e)!=P4_MP_OK) {
        count_stat(&gc.stats.rx_session_rejected);
        return;
    }
    const uint8_t slot=e.player_slot;
    if (slot>=gc.config.player_count || route!=gc.routes[slot]) {
        count_stat(&gc.stats.rx_session_rejected); return;
    }
    if (nonce) {
        e.packet=inner;
        if (inner.type==P4_MP_PACKET_INPUT) e.type=P4_MP_EVENT_INPUT;
        else if (inner.type==P4_MP_PACKET_PING) e.type=P4_MP_EVENT_PING;
        else if (inner.type==P4_MP_PACKET_LEAVE) {
            p4_mp_event_t ignored;
            (void)p4_mp_session_route_disconnected(gc.session,route,&ignored);
            e.type=P4_MP_EVENT_PEER_LEFT;
        } else e.type=P4_MP_EVENT_GAME_MESSAGE;
    }
    if (e.type==P4_MP_EVENT_PEER_LEFT || e.type==P4_MP_EVENT_REJECTED) {
        departed(slot); return;
    }
    if (resume_message(slot,&e.packet)) return;
    if (host() && (gc.resuming & gc.resume_armed & (1U<<slot)) &&
        e.type==P4_MP_EVENT_PING && e.packet.payload_length==8 &&
        !memcmp(e.packet.payload,"GCAREADY",8) &&
        e.packet.ack>=gc.activation[slot] && e.packet.ack<=gc.sync.next_output) {
        /* The guest may consume activation and stop GCP reports between send
         * intervals. This ACK carries its real canonical receive cursor;
         * INPUT ACK==pivot is preloaded early and cannot prove catch-up. */
        gc.resuming &= (uint8_t)~(1U<<slot);
        if (gc.checkpoint_mode) p4_cs_retire(&gc.checkpoints,slot);
        gc.replay_cursor[slot]=e.packet.ack;
        gc.progress_ms[slot]=millis();
    }
    if (host() && (gc.resuming & (1U<<slot))) {
        if (e.type==P4_MP_EVENT_PING && e.packet.payload_length==8 &&
            !memcmp(e.packet.payload,"GCAWAIT!",8)) gc.resume_seen_ms[slot]=millis();
        if (e.type==P4_MP_EVENT_INPUT && gc.activation[slot]) {
            p4_mp_input_t input;p4_doom_mp_tic_t tic;
            const uint32_t pivot=gc.activation[slot];
            if (p4_mp_input_decode(e.packet.payload,e.packet.payload_length,&input)==P4_MP_OK &&
                p4_doom_mp_tic_from_input(&input,&tic) && tic.tick>=pivot &&
                tic.tick!=UINT32_MAX && e.packet.ack==pivot) {
                const uint8_t bit=(uint8_t)(1U<<slot);
                if (!(gc.resume_armed & bit)) {
                    if (tic.tick!=pivot || gc.sync.next_output>=pivot ||
                        !p4_doom_lockstep_reactivate(&gc.sync,slot,pivot)) return;
                    gc.resume_armed |= bit;
                    ESP_LOGI("doom_gc","GAME_CHANGERS_AI REJOIN_ARMED slot=%u tic=%u",
                        (unsigned)slot,(unsigned)pivot);
                }
                if (!p4_doom_lockstep_input(&gc.sync,slot,&tic,pivot)) {
                    resume_abort(slot,4);return;
                }
                gc.progress_ms[slot]=gc.resume_seen_ms[slot]=millis();
                if (gc.input_seen[slot]<=tic.tick) gc.input_seen[slot]=tic.tick+1U;
                if (gc.replay_cursor[slot]==pivot) {
                    gc.resuming &= (uint8_t)~bit;
                    if (gc.checkpoint_mode) p4_cs_retire(&gc.checkpoints,slot);
                }
            }
        }
        return;
    }
    /* Loading is liveness, never engine readiness or simulation progress.
     * Only exact Arena messages from the admitted route/session can extend
     * the wait; replay and framing checks have already succeeded. The first
     * loading message starts an absolute deadline which heartbeats cannot
     * extend. Real command/ack progress ends this loading phase. */
    if (e.type==P4_MP_EVENT_PING &&
        e.packet.payload_length==8 && !memcmp(e.packet.payload,"GCAWAIT!",8)) {
        gc.startup_seen_ms[slot]=millis();
        if (gc.configured) {
            if (!(gc.loading_peers & (1U<<slot))) gc.loading_since_ms[slot]=millis();
            gc.loading_seen_ms[slot]=millis();
            gc.loading_peers |= (uint8_t)(1U<<slot);
        }
        return;
    }
    if (host()) {
        /* A guest may have missed every lobby COMMIT before the host handed
         * the session to Doom. Recover its repeated READY here until all
         * engines are ready; lobby READY must never set gc.ready. */
        const uint64_t now=millis();
        uint8_t commit[P4_MP_GROUP_BYTES];
        if (!gc.configured && e.type==P4_MP_EVENT_GAME_MESSAGE &&
            now>=gc.next_start_reply_ms[slot] &&
            p4_mp_group_commit_reply(
                p4_mp_group_token(gc.config.session_id,gc.config.session_seed),
                initial_count(),slot,e.packet.payload,e.packet.payload_length,commit)) {
            gc.next_start_reply_ms[slot]=now+100;
            send_packet(slot,P4_MP_GROUP_PACKET_TYPE,commit,sizeof(commit));
            return;
        }
        const uint32_t before=gc.sync.peer_ack[slot];
        if (e.type==P4_MP_EVENT_INPUT) {
            p4_mp_input_t input;
            p4_doom_mp_tic_t tic;
            if (p4_mp_input_decode(e.packet.payload,e.packet.payload_length,&input)==P4_MP_OK &&
                p4_doom_mp_tic_from_input(&input,&tic) &&
                p4_doom_lockstep_input(&gc.sync,slot,&tic,e.packet.ack) &&
                tic.tick>=gc.sync.next_output &&
                tic.tick>=gc.input_seen[slot] && tic.tick!=UINT32_MAX) {
                gc.input_seen[slot]=tic.tick+1U;
                peer_progress(slot);
            }
        } else if (e.type==P4_MP_EVENT_PING && e.packet.payload_length==8 &&
                   memcmp(e.packet.payload,"GCAREADY",8)==0) {
            gc.startup_seen_ms[slot]=millis();
            gc.ready |= (uint8_t)(1U<<slot);
            (void)p4_doom_lockstep_ack(&gc.sync,slot,e.packet.ack);
        }
        if (gc.sync.peer_ack[slot]!=before) peer_progress(slot);
    } else if (slot==0 && e.type==P4_MP_EVENT_PING &&
               e.packet.payload_length==8 &&
               memcmp(e.packet.payload,"GCAHOST!",8)==0) {
        /* A live host can be waiting for another guest's missing command.
         * Its session-validated heartbeat keeps healthy clients connected. */
        gc.progress_ms[0]=millis();
        gc.startup_seen_ms[0]=millis();
    } else if (slot==0 && e.type==P4_MP_EVENT_GAME_MESSAGE && gc.configuring &&
               (!gc.checkpoint_mode || !gc.replaying || gc.checkpoint_guest_restored) &&
               ((gc.sync.replaying && (!gc.activation[gc.config.local_player_slot] ||
                    gc.sync.next_output<gc.activation[gc.config.local_player_slot]))
                ? p4_doom_lockstep_receive_replay(&gc.sync,e.packet.payload,e.packet.payload_length)
                : p4_doom_lockstep_receive(&gc.sync,e.packet.payload,e.packet.payload_length))) {
        peer_progress(0);
        gc.startup_seen_ms[0]=millis();
        /* Live loss recovery needs an immediate ACK. Replay already reports its
         * receive cursor and credit every 20 ms; one READY per historical tic
         * would crowd those useful reports out of the host UDP mailbox. */
        if (!gc.local_loading && !gc.replaying) {
            const uint8_t ready[8]={'G','C','A','R','E','A','D','Y'};
            if (send_packet(0,P4_MP_PACKET_PING,ready,sizeof(ready))) {
                gc.live_ready_sent_in_poll=true;
                gc.live_ready_ack_in_poll=gc.sync.next_output;
            }
        }
    }
}

esp_err_t p4_doom_gc_prepare(p4_mp_session_t *session,
    const p4_doom_mp_launch_config_t *config, const p4_doom_p4mp_transport_t *transport)
{
    if (gc.session && gc.saved_timeout) gc.session->timeout_ms=gc.saved_timeout;
    free(gc.journal_storage);
    checkpoint_release();
    memset(&gc,0,sizeof(gc));
    if (!session && !config && !transport) return ESP_OK;
    if (!session || !config || !transport || !transport->set_handler ||
        !transport->send_to || !transport->poll || !transport->connected ||
        !p4_doom_mp_launch_config_valid(config) || !config->enabled ||
        config->setup.game!=P4_DOOM_MP_GAME_GAME_CHANGERS_AI ||
        config->start_tic!=0 || config->input_delay_tics!=2 ||
        (session->state!=P4_MP_SESSION_CONNECTED &&
            !(session->state==P4_MP_SESSION_HOSTING && config->role==P4_MP_ROLE_HOST &&
                config->initial_player_mask==1U)) || session->role!=config->role ||
        session->session_id!=config->session_id || session->self_peer_id!=config->self_peer_id ||
        ((config->role==P4_MP_ROLE_HOST)!=(config->local_player_slot==0))) return ESP_ERR_INVALID_ARG;
    gc.session=session; gc.config=*config; gc.transport=*transport;
    uint8_t offer[P4_MP_OFFER_PAYLOAD_BYTES];
    uint8_t settings[P4_MP_GAME_SETTINGS_BYTES];
    gc.resume_enabled=p4_mp_lobby_offer_encode(&config->lobby_offer,offer)==P4_MP_OK &&
        (config->lobby_offer.game_protocol==7U ||
            config->lobby_offer.game_protocol==P4_DOOM_ARENA_CHECKPOINT_PROTOCOL) &&
        config->lobby_offer.player_capacity==config->player_count &&
        config->lobby_offer.session_seed==config->session_seed &&
        config->lobby_offer.input_delay_tics==config->input_delay_tics &&
        p4_doom_mp_setup_encode(&config->setup,settings) &&
        !memcmp(config->lobby_offer.game_settings,settings,sizeof(settings));
    if (config->rejoining && (!gc.resume_enabled || config->role!=P4_MP_ROLE_CLIENT ||
        !config->resume_nonce)) return ESP_ERR_INVALID_ARG;
    gc.checkpoint_mode=gc.resume_enabled &&
        config->lobby_offer.game_protocol==P4_DOOM_ARENA_CHECKPOINT_PROTOCOL;
    gc.original_peer[config->local_player_slot]=config->self_peer_id;
    uint8_t mask=(uint8_t)(1U<<config->local_player_slot);
    for (size_t i=0;i<P4_MP_MAX_REMOTE_PEERS;++i) {
        const p4_mp_peer_t *p=&session->peers[i];
        if (!p->connected) continue;
        if (p->player_slot>=config->player_count || (mask & (1U<<p->player_slot))) return ESP_ERR_INVALID_STATE;
        gc.routes[p->player_slot]=p->route_id;
        gc.original_peer[p->player_slot]=p->peer_id;
        gc.progress_ms[p->player_slot]=millis();
        mask |= (uint8_t)(1U<<p->player_slot);
    }
    if ((host() && mask!=config->initial_player_mask) || (!host() && !gc.routes[0]) ||
        !p4_doom_lockstep_init(&gc.sync,config->local_player_slot,config->player_count))
        return ESP_ERR_INVALID_STATE;
    for (uint8_t i=1;i<config->player_count;++i)
        if (!(config->initial_player_mask & (1U<<i)))
            (void)p4_doom_lockstep_depart(&gc.sync,i);
    if (gc.checkpoint_mode) {
        char identity[71]="P4CK2|";static const char hex[]="0123456789abcdef";
        for (unsigned i=0;i<32;++i) {
            identity[6+i*2]=hex[config->lobby_offer.compatibility_sha256[i]>>4];
            identity[7+i*2]=hex[config->lobby_offer.compatibility_sha256[i]&15U];
        }
        identity[70]=0;
        gc.checkpoint_storage[0]=checkpoint_allocate(P4_CT_MAX_BYTES);
        if (host()) gc.checkpoint_storage[1]=checkpoint_allocate(P4_CT_MAX_BYTES);
        if (!gc.checkpoint_storage[0] || (host() && !gc.checkpoint_storage[1]) ||
            !P4_DoomCheckpointInit() || !P4_DoomCheckpointSetContentIdentity(identity) ||
            (host() && !p4_cs_init(&gc.checkpoints,gc.checkpoint_storage[0],
                gc.checkpoint_storage[1],P4_CT_MAX_BYTES,checkpoint_hash,NULL))) {
            checkpoint_release();return ESP_ERR_NO_MEM;
        }
    }
    if (host() && gc.resume_enabled) {
        const size_t bytes=gc.checkpoint_mode?P4_DOOM_GC_SUFFIX_BYTES:P4_DOOM_GC_REPLAY_BYTES;
        gc.journal_storage=checkpoint_allocate(bytes);
        if (gc.checkpoint_mode)
            (void)p4_doom_replay_journal_init_rolling(&gc.journal,gc.journal_storage,
                gc.journal_storage?bytes:0,config->player_count);
        else (void)p4_doom_replay_journal_init(&gc.journal,gc.journal_storage,
                gc.journal_storage?bytes:0,config->player_count);
        for (uint8_t i=1;i<config->player_count;++i)
            if (config->initial_player_mask & (1U<<i)) make_ticket(gc.tickets[i]);
    }
    gc.tickets_acked=gc.resume_enabled?1U:config->initial_player_mask;
    if (config->rejoining) {
        if (!p4_doom_lockstep_replay_begin(&gc.sync)) {
            free(gc.journal_storage);gc.journal_storage=NULL;
            checkpoint_release();return ESP_ERR_INVALID_STATE;
        }
        gc.replaying=true;
        gc.resume_started_ms[config->local_player_slot]=millis();
        gc.tickets_acked |= (uint8_t)(1U<<config->local_player_slot);
    }
    gc.saved_timeout=session->timeout_ms;
    session->timeout_ms=60000;
    gc.prepared_ms=millis();
    gc.prepared=true;
    const esp_err_t result=transport->set_handler(transport->context,receive_frame,NULL);
    if (result!=ESP_OK) {
        session->timeout_ms=gc.saved_timeout;
        gc.prepared=false;
        free(gc.journal_storage);gc.journal_storage=NULL;
        checkpoint_release();
    }
    return result;
}

boolean p4_doom_gc_active(void) { return gc.prepared; }
boolean p4_doom_gc_failed(void) { return gc.failed; }

static void checkpoint_expire(uint32_t first,uint64_t now)
{
    if (!gc.checkpoint_mode || !host()) return;
    const uint8_t failed=p4_cs_expire(&gc.checkpoints,first,now);
    for (uint8_t i=1;i<gc.config.player_count;++i)
        if (failed & (1U<<i)) resume_abort(i,5);
}

static void deliver(void)
{
    p4_doom_lockstep_frame_t frame;
    while (D_P4TicCapacity() && p4_doom_lockstep_pop(&gc.sync,&frame)) {
        if (host() && gc.resume_enabled) {
            if (gc.checkpoint_mode && gc.journal.available) {
                const uint32_t first=gc.journal.next_tick>=gc.journal.capacity
                    ? gc.journal.next_tick-(uint32_t)gc.journal.capacity+1U : 0;
                checkpoint_expire(first,millis());
            }
            (void)p4_doom_replay_journal_append(&gc.journal,&frame);
        }
        ticcmd_t commands[NET_MAXPLAYERS]={0};
        boolean ingame[NET_MAXPLAYERS]={0};
        for (uint8_t i=0;i<gc.config.player_count;++i) {
            const p4_doom_mp_tic_t *t=&frame.commands[i];
            commands[i].forwardmove=t->forward_move;
            commands[i].sidemove=t->side_move;
            commands[i].angleturn=t->angle_turn;
            commands[i].buttons=t->buttons;
            commands[i].consistancy=t->consistency;
            commands[i].chatchar=t->chat_char;
            ingame[i]=(frame.mask & (1U<<i))!=0;
        }
        D_ReceiveTic(commands,ingame);
    }
}

static bool peer_blocks_progress(uint8_t slot)
{
    if (gc.sync.peer_ack[slot]<gc.sync.next_output) return true;
    const p4_doom_mp_tic_queue_t *queue=&gc.sync.input;
    const uint32_t tick=queue->next_tick;
    const size_t index=tick%P4_DOOM_MP_TIC_RING_SIZE;
    /* Do not blame guests for a host that has not built its own next tic,
     * or for another guest when their required command is already queued. */
    const bool local_ready=queue->valid[0][index] && queue->tags[0][index]==tick;
    const bool peer_ready=queue->valid[slot][index] && queue->tags[slot][index]==tick;
    return local_ready && !peer_ready;
}

/* Only explicitly marked short render tails may omit RX. Session deadlines,
 * lockstep, engine delivery and outbound opportunities still run every call.
 * 2 ms is an eligibility window, not an RTOS input-latency guarantee. */
static void receive_runtime(bool loading, bool opportunistic)
{
    const uint64_t start=(uint64_t)esp_timer_get_time();
    const bool live=gc.configured && !loading && !gc.local_loading &&
        !gc.loading_peers && !gc.replaying && !gc.resuming && !gc.fresh_pending;
    if (opportunistic && live && gc.rx_seen && gc.rx_was_live && gc.rx_drained &&
        start>=gc.rx_started_us && start-gc.rx_started_us<2000U &&
        gc.transport.poll_drained &&
        gc.transport.poll_drained(gc.transport.context)) {
        count_stat(&gc.stats.rx_poll_skips);
        return;
    }
    if (live && gc.rx_seen && gc.rx_was_live && start>=gc.rx_started_us) {
        const uint64_t gap=start-gc.rx_started_us;
        if (gap>gc.stats.rx_gap_max_us) gc.stats.rx_gap_max_us=gap;
    }
    gc.rx_seen=true;gc.rx_was_live=live;gc.rx_started_us=start;
    gc.transport.poll(gc.transport.context);
    gc.rx_drained=gc.transport.poll_drained &&
        gc.transport.poll_drained(gc.transport.context);
    if (!gc.rx_drained) count_stat(&gc.stats.rx_busy_polls);
    const uint64_t end=(uint64_t)esp_timer_get_time();
    const uint64_t elapsed=end>=start?end-start:0;
    count_stat(&gc.stats.rx_poll_calls);
    gc.stats.rx_poll_us=UINT64_MAX-gc.stats.rx_poll_us<elapsed
        ?UINT64_MAX:gc.stats.rx_poll_us+elapsed;
    if (elapsed>gc.stats.rx_poll_max_us) gc.stats.rx_poll_max_us=elapsed;
}

static void poll_runtime_mode(bool loading, bool opportunistic)
{
    if (!gc.prepared || gc.failed) return;
    /* The first ordinary call after storage loading must also drain RX. */
    if (gc.local_loading) opportunistic=false;
    const uint64_t entered=millis();
    if (!gc.configured && entered-gc.prepared_ms>=P4_DOOM_GC_STARTUP_LIMIT_MS) {
        ESP_LOGI("doom_gc","GAME_CHANGERS_AI STARTUP_FAILED reason=loading-deadline");
        gc.failed=true;
        gc.session->timeout_ms=gc.saved_timeout;
        return;
    }
    if (gc.configured) {
        if (loading && !gc.local_loading) gc.local_loading_since_ms=entered;
        if (gc.local_loading && entered-gc.local_loading_since_ms>=P4_DOOM_GC_STARTUP_LIMIT_MS) {
            ESP_LOGI("doom_gc","GAME_CHANGERS_AI LOADING_FAILED reason=local-deadline");
            gc.failed=true;
            gc.session->timeout_ms=gc.saved_timeout;
            return;
        }
        if (!loading && gc.local_loading) {
            /* The engine has resumed. Peers must not lose their simulation
             * progress window because this device was reading the level.
             * Exclude only the time actually spent loading: incidental WAD
             * reads during gameplay must not renew a stalled peer's window. */
            const uint64_t elapsed=entered-gc.local_loading_since_ms;
            for (unsigned i=0;i<4;++i) {
                gc.progress_ms[i]+=elapsed;
                if (gc.progress_ms[i]>entered) gc.progress_ms[i]=entered;
            }
        }
        gc.local_loading=loading;
    }
#if defined(ESP_PLATFORM) && defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
    /* Loading and configure also run on the foreground owner task. Debug
     * status reads cached state; game mode rejects storage-transfer starts. */
    console_os_debug_poll();
#endif
    p4_doom_startup_status(gc.configuring && !gc.configured);
    gc.live_ready_sent_in_poll=false;
    receive_runtime(loading,opportunistic);
    const uint64_t now=millis();
#if defined(ESP_PLATFORM) && defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
    if (!gc.configured && now>=gc.next_startup_memory_ms) {
        gc.next_startup_memory_ms=now+10000;
        ESP_LOGI("doom_gc", "GAME_CHANGERS_AI STARTUP_MEMORY phase=%s dma_free=%u dma_largest=%u internal_free=%u internal_largest=%u",
            gc.configuring ? "configure" : "loading",
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
#endif
    checkpoint_expire(gc.journal.first_tick,now);
    if (gc.checkpoint_mode && !host() && gc.replaying && gc.checkpoint_rx.active &&
        (!p4_ct_rx_tick(&gc.checkpoint_rx,now) ||
            now-gc.checkpoint_started_ms>=P4_CS_LEASE_MS)) gc.failed=true;
    session_timeout();
    p4_mp_event_t e;
    if (p4_mp_session_tick(gc.session,now,&e)) departed(e.player_slot);
    if (gc.failed) return;
    for (uint8_t i=0;i<gc.config.player_count;++i) {
        if (!gc.routes[i]) continue;
        if (!gc.configured) continue;
        if (host() && ((gc.resuming | gc.fresh_pending) & (1U<<i))) {
            if (!gc.transport.connected(gc.transport.context,gc.routes[i]) ||
                now-gc.resume_seen_ms[i]>=P4_DOOM_GC_STARTUP_SILENCE_MS ||
                now-gc.resume_started_ms[i]>=P4_DOOM_GC_STARTUP_LIMIT_MS ||
                !p4_doom_replay_journal_available(&gc.journal)) resume_abort(i,2);
            continue;
        }
        if (gc.loading_peers & (1U<<i)) {
            if (!gc.transport.connected(gc.transport.context,gc.routes[i]) ||
                now-gc.loading_seen_ms[i]>=P4_DOOM_GC_STARTUP_SILENCE_MS ||
                now-gc.loading_since_ms[i]>=P4_DOOM_GC_STARTUP_LIMIT_MS) {
                ESP_LOGI("doom_gc","GAME_CHANGERS_AI LOADING_FAILED reason=peer-deadline slot=%u",
                    (unsigned)i);
                departed(i);
            }
            continue;
        }
        if (gc.local_loading) {
            if (!gc.transport.connected(gc.transport.context,gc.routes[i])) departed(i);
            continue;
        }
        if (host()) {
            if (!peer_blocks_progress(i)) gc.progress_ms[i]=now;
            else count_stat(&gc.stats.host_blocked_peer_polls);
        }
        if (!gc.transport.connected(gc.transport.context,gc.routes[i]) ||
            now-gc.progress_ms[i]>10000) departed(i);
    }
    if (gc.failed) return;
    session_timeout();
    if (host() && gc.resume_enabled && !gc.configured && now>=gc.next_ticket_ms) {
        gc.next_ticket_ms=now+200;
        for (uint8_t i=1;i<gc.config.player_count;++i) {
            if ((gc.config.initial_player_mask & (1U<<i)) && !(gc.tickets_acked & (1U<<i))) {
                const p4_doom_resume_control_t c=resume_control(i,P4_DOOM_RESUME_TICKET);
                send_resume(gc.routes[i],&c);
            }
        }
    }
    if (gc.replaying && now-gc.resume_started_ms[gc.config.local_player_slot]>=
        P4_DOOM_GC_STARTUP_LIMIT_MS) { gc.failed=true;return; }
    if (loading || !gc.configuring) {
        /* SD validation can outlast the transport's route lease. Keep the
         * admitted session alive while loading without claiming engine READY
         * or producing/delivering simulation tics under the VFS lock. This
         * includes both the initial level and subsequent arena map loads. */
        if (now>=gc.next_keepalive_ms) {
            gc.next_keepalive_ms=now+500;
            const uint8_t waiting[8]={'G','C','A','W','A','I','T','!'};
            for (uint8_t i=0;i<gc.config.player_count;++i)
                if (gc.routes[i]) send_packet(i,P4_MP_PACKET_PING,waiting,sizeof(waiting));
        }
        return;
    }
    if (host() && (gc.ready & gc.config.initial_player_mask)==gc.config.initial_player_mask &&
        (gc.tickets_acked & gc.config.initial_player_mask)==gc.config.initial_player_mask) {
        p4_doom_lockstep_pump(&gc.sync);
        commit_fresh_ownership();
    }
    if (gc.configured) deliver();
    if (gc.replaying) {
        const uint32_t pivot=gc.activation[gc.config.local_player_slot];
        if (pivot && !gc.replay_input_ready) {
            if (!D_P4ReplayFinish((int)pivot) ||
                !p4_doom_lockstep_prepare_live_input(&gc.sync,pivot)) {
                gc.failed=true;return;
            }
            gc.replay_input_ready=true;
        }
        if (pivot && gc.sync.replaying && gc.sync.next_read==pivot &&
            gc.sync.next_output==pivot) {
            if (!p4_doom_lockstep_replay_finish(&gc.sync,pivot)) {
                gc.failed=true;return;
            }
        }
        if (pivot && !gc.sync.replaying && D_P4ReplayTic()>(int)pivot) {
            gc.replaying=false;
            if (gc.checkpoint_mode) {
                memset(&gc.checkpoint_rx,0,sizeof(gc.checkpoint_rx));
                free(gc.checkpoint_storage[0]);gc.checkpoint_storage[0]=NULL;
            }
            ESP_LOGI("doom_gc","GAME_CHANGERS_AI REJOIN_READY slot=%u tic=%u",
                (unsigned)gc.config.local_player_slot,(unsigned)pivot);
        }
    }
    if (now<gc.next_send_ms) return;
    gc.next_send_ms=now+20;
    if (host()) {
        const bool keepalive=now>=gc.next_keepalive_ms;
        if (keepalive) gc.next_keepalive_ms=now+500;
        /* Bound this periodic send pass across all guests, leaving the Hosted
         * TX queue room to drain. Live recovery and admission controls take
         * priority; remaining replay credit is shared one frame per guest. */
        unsigned budget=P4_DOOM_GC_HOST_SEND_BUDGET;
        for (uint8_t i=1;i<gc.config.player_count && budget;++i) {
            if (gc.resuming & (1U<<i)) {
                const uint32_t pivot=gc.activation[i];
                if (pivot && gc.sync.next_output>=pivot && !(gc.resume_armed & (1U<<i))) {
                    resume_abort(i,3);--budget;continue;
                }
                if (pivot) { send_replay_control(i,2,0,0,pivot,0);--budget; }
            }
            uint8_t packet[P4_DOOM_LOCKSTEP_BYTES];
            /* Bounded live pipeline: retain the oldest unacknowledged frame
             * and send at most two following frames. Startup/rejoin remain
             * on their existing one-frame boundary and global budget. */
            const unsigned batch=gc.configured && !(gc.resuming & (1U<<i))?3U:1U;
            const uint32_t first=gc.sync.peer_ack[i];
            for (unsigned n=0;gc.routes[i] && (gc.sync.mask & (1U<<i)) && n<batch && budget;++n) {
                const uint32_t tick=first+n;
                if (tick<first || !p4_doom_lockstep_packet_at(&gc.sync,i,tick,packet)) break;
                send_packet(i,P4_MP_PACKET_GAME_MESSAGE,packet,sizeof(packet));--budget;
            }
            /* A missing guest can stall command production until its timeout.
             * Remaining guests must still know their server is alive. */
            if (gc.routes[i] && keepalive && budget) {
                const uint8_t alive[8]={'G','C','A','H','O','S','T','!'};
                send_packet(i,P4_MP_PACKET_PING,alive,sizeof(alive));--budget;
            }
        }
        /* deliver() may advance the checkpoint service clock while consuming
         * canonical tics. Sample again after those callbacks so normal progress
         * cannot look like a backward clock to the strict lease checks. */
        if (gc.checkpoint_mode && budget>=P4_CT_SEND_BUDGET)
            budget-=p4_cs_poll(&gc.checkpoints,false,millis(),checkpoint_send,NULL);
        uint8_t sent[4]={0};
        unsigned idle=0;
        while (budget && idle<gc.config.player_count-1U) {
            const uint8_t i=(uint8_t)(1U+gc.replay_next_slot);
            gc.replay_next_slot=(uint8_t)((gc.replay_next_slot+1U)%(gc.config.player_count-1U));
            const uint32_t pivot=gc.activation[i];
            const uint32_t end=pivot && pivot<gc.journal.next_tick?pivot:gc.journal.next_tick;
            const uint32_t t=gc.replay_cursor[i]+sent[i];
            if (!(gc.resuming & (1U<<i)) ||
                (gc.checkpoint_mode && !(gc.checkpoint_restored & (1U<<i))) ||
                sent[i]>=gc.replay_credit[i] || t>=end) {
                ++idle;continue;
            }
            uint8_t bytes[P4_DOOM_LOCKSTEP_BYTES];
            if (!p4_doom_replay_journal_packet(&gc.journal,t,bytes)) {
                resume_abort(i,2);--budget;++idle;continue;
            }
            send_packet(i,P4_MP_PACKET_GAME_MESSAGE,bytes,sizeof(bytes));
            ++sent[i];--budget;idle=0;
        }
    } else {
        if (gc.replaying && gc.checkpoint_mode && gc.checkpoint_content_ready &&
            now>=gc.checkpoint_control_ms) {
            gc.checkpoint_control_ms=now+100;
            if (!gc.checkpoint_rx.active) send_replay_control(0,4,0,0,0,0);
            else {
                checkpoint_ack();
                if (gc.checkpoint_guest_restored)
                    send_replay_control(0,5,gc.checkpoint_rx.meta.next_tic,
                        gc.checkpoint_rx.meta.next_tic,0,0);
            }
        }
        if (gc.replaying && (!gc.checkpoint_mode || gc.checkpoint_guest_restored)) {
            unsigned credit=P4_DOOM_LOCKSTEP_HISTORY-(gc.sync.next_output-gc.sync.next_read);
            const unsigned engine_credit=D_P4TicCapacity();
            if (credit>engine_credit) credit=engine_credit;
            const int engine_played=D_P4ReplayTic();
            const uint32_t played=engine_played<0?0:(uint32_t)engine_played;
            gc.loading_replay_played=played;
            const uint32_t cursor=gc.sync.next_output;
            const uint32_t pivot=gc.activation[gc.config.local_player_slot];
            /* Three guests must not fill a slower host's mailbox with identical
             * reports. Changed credit/progress is sent on the normal 20 ms
             * opportunity; a 100 ms retry repairs lost reports and stays live. */
            if (cursor!=gc.reported_cursor || played!=gc.reported_played ||
                pivot!=gc.reported_pivot || credit!=gc.reported_credit ||
                now>=gc.next_replay_report_ms) {
                send_replay_control(0,1,cursor,played,pivot,(uint8_t)credit);
                gc.reported_cursor=cursor;gc.reported_played=played;
                gc.reported_pivot=pivot;gc.reported_credit=(uint8_t)credit;
                gc.next_replay_report_ms=now+100;
            }
        }
        p4_doom_mp_tic_t tic;
        if (p4_doom_mp_tx_window_oldest(&gc.sync.local,&tic)) {
            /* Never retire or skip canonical input: the oldest command is
             * retried with up to two queued followers in ordinary live play. */
            const unsigned batch=gc.configured && !gc.replaying?3U:1U;
            const uint32_t first=tic.tick;
            for (unsigned n=0;n<batch;++n) {
                const uint32_t tick=first+n;
                const size_t index=tick%P4_DOOM_MP_TX_WINDOW_SIZE;
                if (tick<first || tick<gc.sync.local.peer_ack ||
                    tick>=gc.sync.local.next_local_tick || !gc.sync.local.valid[index] ||
                    gc.sync.local.tags[index]!=tick || gc.sync.local.tics[index].tick!=tick) break;
                p4_mp_input_t input; uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES];
                p4_doom_mp_tic_to_input(&gc.sync.local.tics[index],&input);
                p4_mp_input_encode(&input,payload);
                send_packet(0,P4_MP_PACKET_INPUT,payload,sizeof(payload));
            }
        }
        /* GCP supplies liveness and cursor while catching up. READY becomes
         * useful again at the activation boundary and during ordinary play. */
        if ((!gc.replaying || gc.activation[gc.config.local_player_slot]) &&
            (!gc.configured || gc.replaying || !gc.live_ready_sent_in_poll ||
                gc.live_ready_ack_in_poll!=gc.sync.next_output)) {
            /* Receiving a live canonical frame already ACKs it immediately.
             * Omit only a successfully sent, same-cursor ACK from this poll;
             * keep failed sends, startup/activation and later retries intact. */
            const uint8_t ready[8]={'G','C','A','R','E','A','D','Y'};
            send_packet(0,P4_MP_PACKET_PING,ready,sizeof(ready));
        }
    }
}

static void poll_runtime(bool loading)
{
    poll_runtime_mode(loading,false);
}

static bool checkpoint_capture(void *context,uint8_t *storage,size_t capacity,
    size_t *length,uint32_t *next_tic,uint32_t *map,uint8_t *members)
{
    (void)context;
    const bool ok=P4_DoomCheckpointCapture(storage,capacity,length,next_tic,members);
    if (!ok || *next_tic<gc.journal.first_tick || *next_tic>gc.journal.next_tick)
        return false;
    /* Session departure can precede engine consumption of that canonical tic.
     * A cold returning seat must already be absent in the captured world. Wait
     * for the owner to consume the departure instead of pinning an unusable
     * snapshot or changing the host's world to accommodate the guest. */
    for (uint8_t slot=1;slot<gc.config.player_count;++slot)
        if (gc.checkpoints.guests[slot-1U].pending && (*members & (1U<<slot)))
            return false;
    p4_doom_arena_t arena;p4_doom_gc_checkpoint_get_arena(&arena);
    if (!arena.map_count || arena.map_index>=arena.map_count) return false;
    *map=arena.maps[arena.map_index];
    return true;
}

/* An admitted cold guest has no live gameplay to preserve. Stop its adapter
 * and ask the existing platform owner to cleanly restart Home; ordinary engine
 * exit callbacks must not inspect a partially restored or absent player. */
static void checkpoint_abort_guest(const char *reason)
{
    (void)reason; /* Logging may be compiled out in minimal platform builds. */
    if (!gc.prepared || !gc.configured || !gc.checkpoint_mode || host() || !gc.replaying)
        return;
    gc.failed=true;
    ESP_LOGI("doom_gc","GAME_CHANGERS_AI CHECKPOINT_FAILED reason=%s action=return-home",reason);
    p4_doom_startup_status(true);
    p4_doom_gc_quit();
    doomgeneric_RequestQuit();
}

/* Sole engine-owner boundary. Capture is finite synchronous work (measured),
 * never a promise of zero frame cost. Destructive restore is cold guest only;
 * failure yields immediately so no stale tic pointer or renderer can execute. */
boolean P4_DoomNetCheckpointBoundary(void)
{
    /* Keep yielding after an abort even if a caller still has queued tics. */
    if (gc.checkpoint_mode && gc.failed && !host()) return true;
    if (!gc.prepared || !gc.configured || !gc.checkpoint_mode || gc.failed) return false;
    if (host()) {
        const int64_t before=esp_timer_get_time();
        const uint8_t started=p4_cs_boundary(&gc.checkpoints,2,gc.config.session_id,
            gc.config.lobby_offer.compatibility_sha256,millis(),checkpoint_capture,NULL);
        const int64_t elapsed=esp_timer_get_time()-before;
        /* Include validation, encode and the one shared SHA-256, including a
         * deferred/failed capture, when reporting foreground boundary cost. */
        if (elapsed>0 && (uint64_t)elapsed>gc.checkpoint_capture_max_us)
            gc.checkpoint_capture_max_us=(uint64_t)elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
        if (started) {
            p4_doom_memory_checkpoint(false);
            ESP_LOGI("doom_gc","GAME_CHANGERS_AI CHECKPOINT_CAPTURE slots=%u boundary_max_us=%u",
                (unsigned)started,(unsigned)gc.checkpoint_capture_max_us);
        }
        return false;
    }
    if (!gc.replaying || gc.checkpoint_guest_restored) return false;
    gc.checkpoint_content_ready=true;
    size_t length=0;const uint8_t *bytes=p4_ct_rx_data(&gc.checkpoint_rx,&length);
    if (!bytes) return false;
    uint32_t next_tic=0;uint8_t members=0;
    bool ok=P4_DoomCheckpointRestore(bytes,length,&next_tic,&members);
    p4_doom_arena_t arena={0};
    if (ok) p4_doom_gc_checkpoint_get_arena(&arena);
    ok=ok && next_tic==gc.checkpoint_rx.meta.next_tic && members==gc.checkpoint_rx.meta.members &&
        arena.map_count && arena.map_index<arena.map_count &&
        arena.maps[arena.map_index]==gc.checkpoint_rx.meta.map &&
        p4_doom_lockstep_replay_checkpoint(&gc.sync,next_tic,members);
    if (!ok) {
        checkpoint_abort_guest("restore");
        return true;
    }
    gc.checkpoint_guest_restored=true;gc.progress_ms[0]=millis();
    gc.loading_replay_base=next_tic;gc.loading_replay_played=next_tic;
    gc.checkpoint_control_ms=0;gc.next_send_ms=0;
    p4_doom_memory_checkpoint(true);
    ESP_LOGI("doom_gc","GAME_CHANGERS_AI CHECKPOINT_RESTORED tic=%u bytes=%u",
        (unsigned)next_tic,(unsigned)length);
    return true;
}

void p4_doom_gc_poll_opportunistic(void)
{
    poll_runtime_mode(false,true);
    if (gc.failed) p4_doom_startup_status(true);
    if (gc.failed) checkpoint_abort_guest("admission");
}

void p4_doom_gc_poll(void)
{
    poll_runtime(false);
    if (gc.failed) p4_doom_startup_status(true);
    if (gc.failed) checkpoint_abort_guest("admission");
}

bool platform_readonly_blob_loading_progress(void)
{
    /* The readonly VFS owns its lock here. Transport receive may buffer
     * canonical commands, but engine delivery waits for an ordinary poll. */
    if (!gc.prepared || gc.failed) {
        if (gc.failed) p4_doom_startup_status(true);
        return false;
    }
    poll_runtime(true);
    if (gc.failed) p4_doom_startup_status(true);
    return !gc.failed;
}

boolean p4_doom_gc_configure(net_gamesettings_t *settings)
{
    if (!gc.prepared || !settings) return false;
    *settings=(net_gamesettings_t){.consoleplayer=gc.config.local_player_slot,
        .num_players=gc.config.player_count,.deathmatch=2,.episode=1,
        .map=p4_doom_arena_map_number(gc.config.setup.map),.skill=(int)gc.config.setup.skill-1,.loadgame=-1,
        .nomonsters=1,.new_sync=1,.extratics=1,.ticdup=1};
    gc.configuring=true; gc.ready=(uint8_t)(1U<<gc.config.local_player_slot);
    if (gc.replaying) {
        gc.configured=true;
        for (unsigned i=0;i<4;++i) gc.progress_ms[i]=millis();
        session_timeout();
        p4_doom_gc_engine_begin_mask(gc.config.player_count,gc.config.initial_player_mask,gc.config.setup.map);
        ESP_LOGI("doom_gc","GAME_CHANGERS_AI REJOIN_REPLAY slot=%u",
            (unsigned)gc.config.local_player_slot);
        return true;
    }
    for (uint32_t t=0;t<2;++t) {
        const p4_doom_mp_tic_t neutral={.tick=t};
        if (host()) {
            for (uint8_t i=0;i<gc.config.player_count;++i)
                if (gc.config.initial_player_mask & (1U<<i))
                    (void)p4_doom_mp_tic_queue_submit(&gc.sync.input,i,&neutral);
        } else (void)p4_doom_lockstep_submit(&gc.sync,&neutral);
    }
    const uint64_t start=millis();
    /* The other engines can still be loading verified SD content. Give
     * every required peer its own silence window and bound the total wait,
     * so one healthy guest cannot conceal another missing guest forever. */
    for (unsigned i=0;i<4;++i) gc.startup_seen_ms[i]=start;
    ESP_LOGI("doom_gc","GAME_CHANGERS_AI STARTUP_WAIT slot=%u silence_ms=%u limit_ms=%u",
        (unsigned)gc.config.local_player_slot,
        (unsigned)P4_DOOM_GC_STARTUP_SILENCE_MS,
        (unsigned)P4_DOOM_GC_STARTUP_LIMIT_MS);
    while ((gc.sync.next_output<2 ||
        !(gc.tickets_acked & (1U<<gc.config.local_player_slot))) && !gc.failed) {
        p4_doom_gc_poll();
        const uint64_t now=millis();
        if (now-start>=P4_DOOM_GC_STARTUP_LIMIT_MS) {
            ESP_LOGI("doom_gc","GAME_CHANGERS_AI STARTUP_FAILED reason=deadline");
            gc.failed=true;
        }
        for (uint8_t i=0;i<gc.config.player_count && !gc.failed;++i) {
            if (!gc.routes[i]) continue;
            if (!gc.transport.connected(gc.transport.context,gc.routes[i]) ||
                now-gc.startup_seen_ms[i]>=P4_DOOM_GC_STARTUP_SILENCE_MS) {
                ESP_LOGI("doom_gc","GAME_CHANGERS_AI STARTUP_FAILED reason=peer-unavailable slot=%u",
                    (unsigned)i);
                gc.failed=true;
            }
        }
        if (!gc.failed && (gc.sync.next_output<2 ||
            !(gc.tickets_acked & (1U<<gc.config.local_player_slot))))
            vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (gc.sync.next_output<2 || gc.failed ||
        !(gc.tickets_acked & (1U<<gc.config.local_player_slot))) {
        gc.failed=true;
        gc.session->timeout_ms=gc.saved_timeout;
        p4_doom_startup_status(true);
        return false;
    }
    gc.configured=true;
    for (unsigned i=0;i<4;++i) gc.progress_ms[i]=millis();
    gc.session->timeout_ms=gc.saved_timeout;
    p4_doom_gc_engine_begin_mask(gc.config.player_count,gc.config.initial_player_mask,gc.config.setup.map);
    deliver();
    ESP_LOGI("doom_gc","GAME_CHANGERS_AI READY players=%u slot=%u",
        (unsigned)gc.config.player_count,(unsigned)gc.config.local_player_slot);
    return true;
}

void p4_doom_gc_submit(const ticcmd_t *c, int tick)
{
    if (!gc.prepared || !c || tick<0) return;
    const p4_doom_mp_tic_t t={.tick=(uint32_t)tick,.forward_move=c->forwardmove,
        .side_move=c->sidemove,.angle_turn=c->angleturn,.buttons=c->buttons,
        .consistency=c->consistancy,.chat_char=c->chatchar};
    if (!p4_doom_lockstep_submit(&gc.sync,&t)) gc.failed=true;
}

void p4_doom_gc_quit(void)
{
    if (!gc.prepared) return;
    const uint8_t reason[2]={0};
    for (uint8_t i=0;i<gc.config.player_count;++i)
        if (gc.routes[i]) send_packet(i,P4_MP_PACKET_LEAVE,reason,sizeof(reason));
    gc.session->timeout_ms=gc.saved_timeout;
    gc.prepared=false;
    gc.replaying=false;
    free(gc.journal_storage);gc.journal_storage=NULL;
    checkpoint_release();
}
