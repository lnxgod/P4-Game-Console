// SPDX-License-Identifier: GPL-2.0-or-later
#include "doom_gc_p4mp.h"
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

static int sockets[3][2];
static uint8_t self;
static unsigned received,sent;
static p4_doom_p4mp_frame_handler_t handler;
static void *handler_context;
static uint8_t last_mask;
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
    assert(p4_doom_gc_prepare(&session,&config,&transport)==ESP_OK);
    net_gamesettings_t settings;
    assert(p4_doom_gc_configure(&settings));
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
int main(void)
{
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
    for (unsigned i=0;i<4;++i) { int status; assert(waitpid(children[i],&status,0)>0); assert(WIFEXITED(status) && WEXITSTATUS(status)==0); }
    puts("Doom adapter: four real session instances, lossy duplicated socket transport, guest departure and host shutdown passed");
    return 0;
}
