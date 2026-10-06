// SPDX-License-Identifier: GPL-2.0-or-later
/* Drive the production adapter and sessions with virtual time. One guest
 * keeps its network link alive but stops contributing simulation commands. */
#include "doom_gc_p4mp.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t now_ms=100;
static p4_mp_session_t peers[4];
static p4_doom_p4mp_frame_handler_t handler;
static void *handler_context;
static uint32_t ack[4];
static unsigned delivered;
static uint8_t last_mask;
static bool client_case, silent_host;
int64_t esp_timer_get_time(void) { return (int64_t)now_ms*1000; }
void vTaskDelay(unsigned ms) { now_ms+=ms; }
void p4_doom_gc_engine_begin(uint8_t count,uint8_t map) { assert(count==4 && map==1); }
void D_ReceiveTic(ticcmd_t *commands,boolean *mask)
{
    (void)commands;last_mask=0;
    for (unsigned i=0;i<4;++i) if(mask[i])last_mask|=(uint8_t)(1U<<i);
    ++delivered;
}
static esp_err_t set_handler(void *ctx,p4_doom_p4mp_frame_handler_t fn,void *hctx)
{ (void)ctx;handler=fn;handler_context=hctx;return ESP_OK; }
static bool connected(void *ctx,uint64_t route) { (void)ctx;(void)route;return true; }
static void receive(unsigned slot,p4_mp_packet_type_t type,const uint8_t *payload,uint16_t n,uint32_t next)
{
    uint8_t data[P4_MP_MAX_DATAGRAM_BYTES];size_t bytes=0;
    assert(p4_mp_session_encode(&peers[slot],type,next,payload,n,data,sizeof(data),&bytes)==P4_MP_OK);
    handler(handler_context,slot+1U,data,bytes);
}
static esp_err_t send_to(void *ctx,uint64_t route,const uint8_t *data,size_t n)
{
    (void)ctx;assert(route>=1 && route<=4);
    p4_mp_packet_view_t p;assert(p4_mp_packet_decode(data,n,&p)==P4_MP_OK);
    if(p.type==P4_MP_PACKET_GAME_MESSAGE) {
        const uint8_t *b=p.payload+4;
        const uint32_t tick=(uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
        if(tick==ack[route-1U])++ack[route-1U];
    }
    return ESP_OK;
}
static void poll(void *ctx)
{
    (void)ctx;
    if(client_case) {
        if(silent_host)return;
        if(delivered<2) {
            /* Real canonical startup frames, with zero input for all seats. */
            uint8_t batch[40]={'G','C',1,15};batch[4]=(uint8_t)delivered;
            receive(0,P4_MP_PACKET_GAME_MESSAGE,batch,sizeof(batch),0);
            /* configure() defers engine delivery; ack tracks decoded frames. */
            batch[4]=1;receive(0,P4_MP_PACKET_GAME_MESSAGE,batch,sizeof(batch),0);
        }
        receive(0,P4_MP_PACKET_PING,(const uint8_t *)"GCAHOST!",8,0);
    } else {
        for(unsigned i=1;i<4;++i)receive(i,P4_MP_PACKET_PING,(const uint8_t *)"GCAREADY",8,ack[i]);
    }
}
static void submit_remote(unsigned slot,uint32_t tick)
{
    p4_doom_mp_tic_t tic={.tick=tick};p4_mp_input_t input;
    uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_doom_mp_tic_to_input(&tic,&input);p4_mp_input_encode(&input,payload);
    receive(slot,P4_MP_PACKET_INPUT,payload,sizeof(payload),ack[slot]);
}
int main(int argc,char **argv)
{
    assert(argc==2);client_case=strcmp(argv[1],"client")==0;
    const bool paused_host=strcmp(argv[1],"host-paused")==0;
    assert(client_case||paused_host||strcmp(argv[1],"host")==0);
    assert(p4_mp_session_host_start(&peers[0],7,100,3000)==P4_MP_OK);
    for(uint8_t i=1;i<4;++i) {
        assert(p4_mp_session_accept_peer(&peers[0],100U+i,i+1U,i,1,now_ms)==P4_MP_OK);
        assert(p4_mp_session_client_start(&peers[i],7,100U+i,100,1,now_ms,3000)==P4_MP_OK);
        peers[i].state=P4_MP_SESSION_CONNECTED;peers[i].peers[0].connected=true;peers[i].next_sequence=2;
    }
    const uint8_t local=client_case?1:0;
    p4_doom_mp_launch_config_t config={.enabled=true,.role=local?P4_MP_ROLE_CLIENT:P4_MP_ROLE_HOST,
        .session_id=7,.self_peer_id=100U+local,.remote_peer_id=local?100:101,.route_id=local?1:2,
        .local_player_slot=local,.player_count=4,.input_delay_tics=2,.session_seed=1,
        .setup={.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI,.mode=P4_DOOM_MP_MODE_ALTDEATH,
            .episode=1,.map=1,.skill=3,.no_monsters=true}};
    const p4_doom_p4mp_transport_t transport={.set_handler=set_handler,.send_to=send_to,.poll=poll,.connected=connected};
    assert(p4_doom_gc_prepare(&peers[local],&config,&transport)==ESP_OK);
    net_gamesettings_t settings;assert(p4_doom_gc_configure(&settings));assert(delivered==2);
    ticcmd_t neutral={0};
    if(!paused_host)p4_doom_gc_submit(&neutral,2);
    if(!client_case && !paused_host) { submit_remote(1,2);submit_remote(2,2); }
    for(unsigned i=0;i<230;++i) { now_ms+=50;p4_doom_gc_poll();assert(!p4_doom_gc_failed()); }
    if(client_case) {
        assert(delivered==2); /* Host is alive while waiting for a stalled guest. */
        silent_host=true;now_ms+=3001;p4_doom_gc_poll();assert(p4_doom_gc_failed());
    } else if(paused_host) {
        assert(delivered==2 && last_mask==15);
        p4_doom_gc_submit(&neutral,2);
        for(unsigned i=1;i<4;++i)submit_remote(i,2);
        now_ms+=50;p4_doom_gc_poll();assert(delivered==3 && last_mask==15);
    } else {
        assert(delivered==3);
        assert(last_mask==7); /* Only guest 3 was withholding the required input. */
        /* The remaining seats can complete the next simulation tic. */
        p4_doom_gc_submit(&neutral,3);submit_remote(1,3);submit_remote(2,3);
        now_ms+=50;p4_doom_gc_poll();assert(delivered==4 && last_mask==7);
    }
    p4_doom_gc_quit();puts("PASS: a stalled guest does not expel healthy peers");return 0;
}
