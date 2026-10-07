// SPDX-License-Identifier: GPL-2.0-or-later
#include "doom_gc_p4mp.h"
#include "p4/multiplayer_group.h"
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

static int sockets[3][2];
static uint8_t self;
static unsigned received,sent;
static p4_doom_p4mp_frame_handler_t handler;
static void *handler_context;
static uint8_t last_mask;
static bool handoff_case;
static int64_t started_us;
static p4_mp_session_t *lobby_session;
static p4_mp_group_start_t lobby_start;
int64_t esp_timer_get_time(void)
{ struct timespec t; assert(clock_gettime(CLOCK_MONOTONIC,&t)==0); return (int64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
void vTaskDelay(unsigned ms) { usleep(ms*1000U); }
void p4_doom_gc_engine_begin(uint8_t count,uint8_t map) { assert(count==4 && map==26); }
static uint8_t chat(unsigned tick,unsigned slot) { return (uint8_t)(0x80U+((tick+slot)%127U)); }
static int8_t move(unsigned tick,unsigned slot) { return (int8_t)((tick+slot)%40U); }
void D_ReceiveTic(ticcmd_t *commands,boolean *mask)
{
    last_mask=0;
    for (unsigned i=0;i<4;++i) {
        if (mask[i]) last_mask |= (uint8_t)(1U<<i);
        assert(commands[i].forwardmove==(received<2 || !mask[i] ? 0 : move(received,i)));
        assert(commands[i].chatchar==(received<2 || !mask[i] ? 0 : chat(received,i)));
        if (!mask[i]) assert(commands[i].buttons==0);
    }
    ++received;
}
static esp_err_t set_handler(void *ctx,p4_doom_p4mp_frame_handler_t fn,void *hctx)
{ (void)ctx; handler=fn; handler_context=hctx; return ESP_OK; }
static bool connected(void *ctx,uint64_t route) { (void)ctx; (void)route; return true; }
static esp_err_t send_to(void *ctx,uint64_t route,const uint8_t *data,size_t length)
{
    (void)ctx;
    const int fd=self ? sockets[self-1][1] : sockets[route-2][0];
    ++sent;
    /* Drop traffic including startup, INPUT, batches and acknowledgments. */
    p4_mp_packet_view_t packet;
    assert(p4_mp_packet_decode(data,length,&packet)==P4_MP_OK);
    /* Lose the entire original one-second commit burst for guest 1. Other
     * peers replace their lobby handlers with the real Doom adapter first. */
    if (handoff_case && !self && route==2 &&
        packet.type==P4_MP_GROUP_PACKET_TYPE && packet.payload_length==P4_MP_GROUP_BYTES &&
        !memcmp(packet.payload,"P4GS",4) && packet.payload[5]==3 &&
        esp_timer_get_time()-started_us<1400000) return ESP_OK;
    /* Force loss of the departing guest's LEAVE: other clients must survive
     * the host waiting for that guest's full session timeout. */
    if ((self==3 && packet.type==P4_MP_PACKET_LEAVE) || sent%11U==0) return ESP_OK;
    if (send(fd,data,length,0)!=(ssize_t)length) {
        assert(errno==EAGAIN || errno==EWOULDBLOCK || errno==ENOBUFS);
        return ESP_FAIL;
    }
    if (sent%17U==0) (void)send(fd,data,length,0);
    return ESP_OK;
}
static void poll(void *ctx)
{
    (void)ctx;
    for (unsigned seat=1;seat<4;++seat) {
        if (self && seat!=self) continue;
        const int fd=sockets[seat-1][self?1:0];
        for (unsigned budget=0;budget<8;++budget) {
            uint8_t bytes[P4_MP_MAX_DATAGRAM_BYTES];
            const ssize_t n=recv(fd,bytes,sizeof(bytes),MSG_DONTWAIT);
            if (n<=0) break;
            handler(handler_context,self?1:seat+1,bytes,(size_t)n);
        }
    }
}
static uint16_t start_token(void)
{
    return p4_mp_group_token(7,1);
}
static void lobby_frame(void *ctx,uint64_t route,const uint8_t *data,size_t n)
{
    (void)ctx;p4_mp_event_t event;
    const uint64_t now=(uint64_t)esp_timer_get_time()/1000U;
    if (p4_mp_session_receive(lobby_session,route,now,data,n,&event)!=P4_MP_OK) return;
    if (event.type==P4_MP_EVENT_GAME_MESSAGE)
        (void)p4_mp_group_receive(&lobby_start,self,event.player_slot,start_token(),
            event.packet.payload,event.packet.payload_length,now);
}
static void enter_from_lobby(p4_mp_session_t *session)
{
    lobby_session=session;
    assert(set_handler(NULL,lobby_frame,NULL)==ESP_OK);
    if (!self) assert(p4_mp_group_begin(&lobby_start,start_token(),4,
        (uint64_t)esp_timer_get_time()/1000U));
    while (lobby_start.phase!=P4_MP_GROUP_DUE) {
        poll(NULL);
        uint8_t payload[P4_MP_GROUP_BYTES];
        if (p4_mp_group_poll(&lobby_start,(uint64_t)esp_timer_get_time()/1000U,payload)) {
            for (uint8_t slot=0;slot<4;++slot) {
                if (slot==self || (self && slot!=0)) continue;
                uint8_t data[P4_MP_MAX_DATAGRAM_BYTES];size_t n=0;
                assert(p4_mp_session_encode(session,P4_MP_GROUP_PACKET_TYPE,0,payload,
                    sizeof(payload),data,sizeof(data),&n)==P4_MP_OK);
                assert(send_to(NULL,slot+1U,data,n)==ESP_OK);
            }
        }
        assert(lobby_start.phase!=P4_MP_GROUP_FAILED);
        usleep(1000);
    }
}
static void run(uint8_t slot)
{
    self=slot;
    p4_mp_session_t session;
    p4_mp_session_init(&session);
    if (!slot) {
        assert(p4_mp_session_host_start(&session,7,100,3000)==P4_MP_OK);
        for (uint8_t i=1;i<4;++i)
            assert(p4_mp_session_accept_peer(&session,100U+i,i+1U,i,1,(uint64_t)esp_timer_get_time()/1000U)==P4_MP_OK);
    } else {
        assert(p4_mp_session_client_start(&session,7,100U+slot,100,1,
            (uint64_t)esp_timer_get_time()/1000U,3000)==P4_MP_OK);
        session.state=P4_MP_SESSION_CONNECTED; /* already accepted by OS lobby */
        session.peers[0].connected=true;
        session.next_sequence=2;
    }
    p4_doom_mp_launch_config_t config={.enabled=true,.role=slot?P4_MP_ROLE_CLIENT:P4_MP_ROLE_HOST,
        .session_id=7,.self_peer_id=100U+slot,.remote_peer_id=slot?100:101,.route_id=slot?1:2,
        .local_player_slot=slot,.player_count=4,.input_delay_tics=2,.session_seed=1,
        .setup={.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI,.mode=P4_DOOM_MP_MODE_ALTDEATH,
            .episode=1,.map=26,.skill=3,.no_monsters=true}};
    const p4_doom_p4mp_transport_t transport={.set_handler=set_handler,.send_to=send_to,.poll=poll,.connected=connected};
    if (handoff_case) enter_from_lobby(&session);
    assert(p4_doom_gc_prepare(&session,&config,&transport)==ESP_OK);
    net_gamesettings_t settings;
    assert(p4_doom_gc_configure(&settings));
    if (handoff_case) assert(esp_timer_get_time()-started_us>=1400000);
    assert(received==2 && settings.consoleplayer==slot && settings.num_players==4 && settings.map==24);
    for (unsigned tick=2;tick<160;++tick) {
        if (slot==3 && tick==120) {
            /* If the leave packet is dropped, ordinary peer timeout recovers. */
            p4_doom_gc_quit();
            _exit(0);
        }
        ticcmd_t cmd={.forwardmove=move(tick,slot),.chatchar=chat(tick,slot)};
        p4_doom_gc_submit(&cmd,(int)tick);
        while (received<=tick) {
            p4_doom_gc_poll(); assert(!p4_doom_gc_failed()); usleep(1000);
        }
    }
    assert(last_mask==7);
    if (!slot) {
        const int64_t until=esp_timer_get_time()+500000;
        while (esp_timer_get_time()<until) { p4_doom_gc_poll(); usleep(1000); }
        p4_doom_gc_quit();
    } else {
        while (!p4_doom_gc_failed()) { p4_doom_gc_poll(); usleep(1000); }
    }
    _exit(0);
}
int main(int argc,char **argv)
{
    assert(argc==1 || (argc==2 && !strcmp(argv[1],"handoff")));
    handoff_case=argc==2;started_us=esp_timer_get_time();
    alarm(30);
    for (unsigned i=0;i<3;++i) {
        assert(socketpair(AF_UNIX,SOCK_DGRAM,0,sockets[i])==0);
        assert(fcntl(sockets[i][0],F_SETFL,O_NONBLOCK)==0);
        assert(fcntl(sockets[i][1],F_SETFL,O_NONBLOCK)==0);
    }
    pid_t children[4];
    for (uint8_t slot=0;slot<4;++slot) {
        children[slot]=fork(); assert(children[slot]>=0);
        if (!children[slot]) run(slot);
    }
    for (unsigned i=0;i<4;++i) {
        int status;const pid_t done=waitpid(-1,&status,0);assert(done>0);
        for (unsigned j=0;j<4;++j) if(children[j]==done)children[j]=0;
        if (!WIFEXITED(status) || WEXITSTATUS(status)!=0) {
            for (unsigned j=0;j<4;++j) if(children[j]>0)(void)kill(children[j],SIGKILL);
            while(waitpid(-1,NULL,0)>0) {}
            return 1;
        }
    }
    puts("Doom adapter: four real session instances, lossy duplicated socket transport, guest departure and host shutdown passed");
    return 0;
}
