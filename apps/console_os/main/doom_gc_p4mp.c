// SPDX-License-Identifier: GPL-2.0-or-later
#include "doom_gc_p4mp.h"
#include "p4/doom_lockstep.h"
#include "p4/doom_arena.h"
#include "d_loop.h"
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
#include "sdkconfig.h"
#endif
#if defined(CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY) && CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY
#define P4_ARENA_LARGE_BSS EXT_RAM_BSS_ATTR
#else
#define P4_ARENA_LARGE_BSS
#endif

/* This bounded command history is task-owned, never accessed by an ISR or DMA.
 * Keep it out of the internal heap needed by startup and peripheral drivers. */
static P4_ARENA_LARGE_BSS struct {
    p4_mp_session_t *session;
    p4_doom_mp_launch_config_t config;
    p4_doom_p4mp_transport_t transport;
    p4_doom_lockstep_t sync;
    uint64_t routes[4], progress_ms[4], next_send_ms, next_keepalive_ms;
    uint32_t saved_timeout;
    uint8_t ready;
    bool prepared, configuring, configured, failed;
} gc;
static uint64_t millis(void) { return (uint64_t)esp_timer_get_time()/1000U; }
static bool host(void) { return gc.config.local_player_slot==0; }

static void send_packet(uint8_t recipient, p4_mp_packet_type_t type,
                        const uint8_t *payload, uint16_t length)
{
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    size_t n=0;
    if (p4_mp_session_encode(gc.session,type,gc.sync.next_output,payload,length,
            datagram,sizeof(datagram),&n)==P4_MP_OK)
        (void)gc.transport.send_to(gc.transport.context,gc.routes[recipient],datagram,n);
}

static void departed(uint8_t slot)
{
    if (slot>=gc.config.player_count) return;
    if (!host() || !gc.configured || slot==0) { gc.failed=true; return; }
    (void)p4_doom_lockstep_depart(&gc.sync,slot);
    gc.routes[slot]=0;
    ESP_LOGI("doom_gc", "GAME_CHANGERS_AI PLAYER_LEFT slot=%u", (unsigned)slot);
}

static void receive_frame(void *context, uint64_t route,
                           const uint8_t *datagram, size_t length)
{
    (void)context;
    p4_mp_event_t e;
    bool known_route=false;
    for (uint8_t i=0;i<gc.config.player_count;++i)
        if (gc.routes[i]!=0 && gc.routes[i]==route) known_route=true;
    /* Runtime admission is closed; do not let JOIN allocate a pending slot. */
    if (!known_route) return;
    if (!gc.prepared || p4_mp_session_receive(gc.session,route,millis(),datagram,length,&e)!=P4_MP_OK)
        return;
    const uint8_t slot=e.player_slot;
    if (slot>=gc.config.player_count || route!=gc.routes[slot]) return;
    if (e.type==P4_MP_EVENT_PEER_LEFT || e.type==P4_MP_EVENT_REJECTED) {
        departed(slot); return;
    }
    if (host()) {
        const uint32_t before=gc.sync.peer_ack[slot];
        if (e.type==P4_MP_EVENT_INPUT) {
            p4_mp_input_t input;
            p4_doom_mp_tic_t tic;
            if (p4_mp_input_decode(e.packet.payload,e.packet.payload_length,&input)==P4_MP_OK &&
                p4_doom_mp_tic_from_input(&input,&tic))
                (void)p4_doom_lockstep_input(&gc.sync,slot,&tic,e.packet.ack);
        } else if (e.type==P4_MP_EVENT_PING && e.packet.payload_length==8 &&
                   memcmp(e.packet.payload,"GCAREADY",8)==0) {
            gc.ready |= (uint8_t)(1U<<slot);
            (void)p4_doom_lockstep_ack(&gc.sync,slot,e.packet.ack);
        }
        if (gc.sync.peer_ack[slot]!=before) gc.progress_ms[slot]=millis();
    } else if (slot==0 && e.type==P4_MP_EVENT_PING &&
               e.packet.payload_length==8 &&
               memcmp(e.packet.payload,"GCAHOST!",8)==0) {
        /* A live host can be waiting for another guest's missing command.
         * Its session-validated heartbeat keeps healthy clients connected. */
        gc.progress_ms[0]=millis();
    } else if (slot==0 && e.type==P4_MP_EVENT_GAME_MESSAGE && gc.configuring &&
               p4_doom_lockstep_receive(&gc.sync,e.packet.payload,e.packet.payload_length)) {
        gc.progress_ms[0]=millis();
        /* Immediate ACK lets the host drain loss recovery without a busy loop. */
        const uint8_t ready[8]={'G','C','A','R','E','A','D','Y'};
        send_packet(0,P4_MP_PACKET_PING,ready,sizeof(ready));
    }
}

esp_err_t p4_doom_gc_prepare(p4_mp_session_t *session,
    const p4_doom_mp_launch_config_t *config, const p4_doom_p4mp_transport_t *transport)
{
    if (gc.session && gc.saved_timeout) gc.session->timeout_ms=gc.saved_timeout;
    memset(&gc,0,sizeof(gc));
    if (!session && !config && !transport) return ESP_OK;
    if (!session || !config || !transport || !transport->set_handler ||
        !transport->send_to || !transport->poll || !transport->connected ||
        !p4_doom_mp_launch_config_valid(config) || !config->enabled ||
        config->setup.game!=P4_DOOM_MP_GAME_GAME_CHANGERS_AI ||
        config->start_tic!=0 || config->input_delay_tics!=2 ||
        session->state!=P4_MP_SESSION_CONNECTED || session->role!=config->role ||
        session->session_id!=config->session_id || session->self_peer_id!=config->self_peer_id ||
        ((config->role==P4_MP_ROLE_HOST)!=(config->local_player_slot==0))) return ESP_ERR_INVALID_ARG;
    gc.session=session; gc.config=*config; gc.transport=*transport;
    uint8_t mask=(uint8_t)(1U<<config->local_player_slot);
    for (size_t i=0;i<P4_MP_MAX_REMOTE_PEERS;++i) {
        const p4_mp_peer_t *p=&session->peers[i];
        if (!p->connected) continue;
        if (p->player_slot>=config->player_count || (mask & (1U<<p->player_slot))) return ESP_ERR_INVALID_STATE;
        gc.routes[p->player_slot]=p->route_id;
        gc.progress_ms[p->player_slot]=millis();
        mask |= (uint8_t)(1U<<p->player_slot);
    }
    if ((host() && mask!=(1U<<config->player_count)-1U) || (!host() && !gc.routes[0]) ||
        !p4_doom_lockstep_init(&gc.sync,config->local_player_slot,config->player_count))
        return ESP_ERR_INVALID_STATE;
    gc.saved_timeout=session->timeout_ms;
    session->timeout_ms=60000;
    gc.prepared=true;
    const esp_err_t result=transport->set_handler(transport->context,receive_frame,NULL);
    if (result!=ESP_OK) {
        session->timeout_ms=gc.saved_timeout;
        gc.prepared=false;
    }
    return result;
}

boolean p4_doom_gc_active(void) { return gc.prepared; }
boolean p4_doom_gc_failed(void) { return gc.failed; }

static void deliver(void)
{
    p4_doom_lockstep_frame_t frame;
    while (p4_doom_lockstep_pop(&gc.sync,&frame)) {
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

void p4_doom_gc_poll(void)
{
    if (!gc.prepared || gc.failed) return;
    gc.transport.poll(gc.transport.context);
    const uint64_t now=millis();
    p4_mp_event_t e;
    if (p4_mp_session_tick(gc.session,now,&e)) departed(e.player_slot);
    if (gc.failed) return;
    for (uint8_t i=0;i<gc.config.player_count;++i) {
        if (!gc.routes[i]) continue;
        if (!gc.configured) continue;
        if (host() && !peer_blocks_progress(i)) gc.progress_ms[i]=now;
        if (!gc.transport.connected(gc.transport.context,gc.routes[i]) ||
            now-gc.progress_ms[i]>10000) departed(i);
    }
    if (gc.failed || !gc.configuring) return;
    if (host() && gc.ready==(1U<<gc.config.player_count)-1U)
        p4_doom_lockstep_pump(&gc.sync);
    if (gc.configured) deliver();
    if (now<gc.next_send_ms) return;
    gc.next_send_ms=now+20;
    if (host()) {
        const bool keepalive=now>=gc.next_keepalive_ms;
        if (keepalive) gc.next_keepalive_ms=now+500;
        for (uint8_t i=1;i<gc.config.player_count;++i) {
            uint8_t packet[P4_DOOM_LOCKSTEP_BYTES];
            if (gc.routes[i] && p4_doom_lockstep_packet(&gc.sync,i,packet))
                send_packet(i,P4_MP_PACKET_GAME_MESSAGE,packet,sizeof(packet));
            /* A missing guest can stall command production until its timeout.
             * Remaining guests must still know their server is alive. */
            if (gc.routes[i] && keepalive) {
                const uint8_t alive[8]={'G','C','A','H','O','S','T','!'};
                send_packet(i,P4_MP_PACKET_PING,alive,sizeof(alive));
            }
        }
    } else {
        p4_doom_mp_tic_t tic;
        if (p4_doom_mp_tx_window_oldest(&gc.sync.local,&tic)) {
            p4_mp_input_t input; uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES];
            p4_doom_mp_tic_to_input(&tic,&input); p4_mp_input_encode(&input,payload);
            send_packet(0,P4_MP_PACKET_INPUT,payload,sizeof(payload));
        }
        const uint8_t ready[8]={'G','C','A','R','E','A','D','Y'};
        send_packet(0,P4_MP_PACKET_PING,ready,sizeof(ready));
    }
}

boolean p4_doom_gc_configure(net_gamesettings_t *settings)
{
    if (!gc.prepared || !settings) return false;
    *settings=(net_gamesettings_t){.consoleplayer=gc.config.local_player_slot,
        .num_players=gc.config.player_count,.deathmatch=2,.episode=1,
        .map=p4_doom_arena_map_number(gc.config.setup.map),.skill=(int)gc.config.setup.skill-1,.loadgame=-1,
        .nomonsters=1,.new_sync=1,.extratics=1,.ticdup=1};
    gc.configuring=true; gc.ready=(uint8_t)(1U<<gc.config.local_player_slot);
    for (uint32_t t=0;t<2;++t) {
        const p4_doom_mp_tic_t neutral={.tick=t};
        if (host()) {
            for (uint8_t i=0;i<gc.config.player_count;++i)
                (void)p4_doom_mp_tic_queue_submit(&gc.sync.input,i,&neutral);
        } else (void)p4_doom_lockstep_submit(&gc.sync,&neutral);
    }
    const uint64_t start=millis();
    while (gc.sync.next_output<2 && !gc.failed && millis()-start<30000) {
        p4_doom_gc_poll(); vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (gc.sync.next_output<2 || gc.failed) { gc.failed=true; return false; }
    gc.configured=true;
    for (unsigned i=0;i<4;++i) gc.progress_ms[i]=millis();
    gc.session->timeout_ms=gc.saved_timeout;
    p4_doom_gc_engine_begin(gc.config.player_count,gc.config.setup.map);
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
}
