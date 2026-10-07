// SPDX-License-Identifier: MIT
/* Run the real arena adapter against the real Wi-Fi route/UDP implementation.
 * Only ESP-IDF calls and elapsed time are mocked. */
#define esp_timer_get_time wifi_mock_clock
#define vTaskDelay wifi_mock_delay
#include <assert.h>
#include <stdio.h>
#include "../../platform_multiplayer_wifi/tests/mocks/mock.h"
/* macOS has only 127.0.0.1 configured. Translate the peer socket address at
 * the platform boundary; datagrams still traverse real UDP unchanged. */
static ssize_t test_recvfrom(int fd,void *buf,size_t n,int flags,struct sockaddr *addr,socklen_t *len)
{
    ssize_t result=recvfrom(fd,buf,n,flags,addr,len);
    struct sockaddr_in *ip=(void *)addr;
    if(result>=0 && ntohs(ip->sin_port)>=42425 && ntohs(ip->sin_port)<=42428) {
        unsigned index=(unsigned)ntohs(ip->sin_port)-42425;
        ip->sin_port=htons(42424);ip->sin_addr.s_addr=htonl(0x7f000002u+index);
    }
    return result;
}
static ssize_t test_sendto(int fd,const void *buf,size_t n,int flags,const struct sockaddr *addr,socklen_t len)
{
    struct sockaddr_in ip=*(const struct sockaddr_in *)addr;
    if(ntohl(ip.sin_addr.s_addr)>=0x7f000002u&&ntohl(ip.sin_addr.s_addr)<=0x7f000005u) {
        unsigned index=ntohl(ip.sin_addr.s_addr)-0x7f000002u;
        ip.sin_addr.s_addr=inet_addr("127.0.0.1");ip.sin_port=htons((uint16_t)(42425+index));
    }
    return sendto(fd,buf,n,flags,(void *)&ip,len);
}
#define recvfrom test_recvfrom
#define sendto test_sendto
#include "../../platform_multiplayer_wifi/src/platform_multiplayer_wifi.c"
#undef recvfrom
#undef sendto
esp_err_t platform_ble_host_start(void){return ESP_OK;}
bool platform_ble_host_ready(void){return true;}
platform_ble_host_status_t platform_ble_host_status(void){return (platform_ble_host_status_t){.state=PLATFORM_BLE_HOST_READY};}
static unsigned received;
static void frame(void *ctx,uint64_t route,const uint8_t *bytes,size_t n)
{(void)ctx;assert((route&UINT64_C(0xffffffff00000000))==P4_MP_WIFI_ROUTE_PREFIX);assert(ntohl((uint32_t)route)>=0x7f000002u&&ntohl((uint32_t)route)<=0x7f000005u);p4_mp_packet_view_t packet;assert(p4_mp_packet_decode(bytes,n,&packet)==P4_MP_OK);++received;}
static size_t packet(uint8_t *out,uint32_t session)
{const uint8_t stamp[8]={0};size_t n=0;assert(p4_mp_packet_encode(P4_MP_PACKET_PING,session,42,1,0,stamp,8,out,P4_MP_MAX_DATAGRAM_BYTES,&n)==P4_MP_OK);return n;}
static void transmit(int fd,const void *bytes,size_t n)
{struct sockaddr_in to={.sin_family=AF_INET,.sin_port=htons(PORT),.sin_addr.s_addr=inet_addr("127.0.0.1")};assert(sendto(fd,bytes,n,0,(void *)&to,sizeof(to))==(int)n);usleep(1000);}
#undef esp_timer_get_time
#undef vTaskDelay
#include "doom_gc_p4mp.h"
#include "test_doom_gc_startup_vfs.h"
int64_t esp_timer_get_time(void) { return mock_time; }
static bool waiting_case;
void vTaskDelay(unsigned ms)
{
    /* Exercise the bounded five-minute wait without spending wall-clock time
     * on 20 ms UDP sends. Each poll still occurs within one heartbeat period. */
    mock_time += (int64_t)(waiting_case ? 500U : ms) * 1000;
}
static int remote_socket;
static p4_mp_session_t remote_session;
static unsigned attempts, failures, delivered, waiting_packets;
static unsigned adapter_polls, storage_reads, storage_delivered;
static size_t largest_storage_read;
static bool slow_storage;
static bool loading, remote_ready;
static uint32_t remote_ack;
void startup_storage_read(size_t bytes)
{
    ++storage_reads;
    if (bytes>largest_storage_read) largest_storage_read=bytes;
    if (slow_storage) {
        mock_time+=(int64_t)((bytes+4095U)/4096U)*700000;
        assert(delivered==storage_delivered);
    }
}
void p4_doom_gc_engine_begin_mask(uint8_t count,uint8_t mask,uint8_t map) { assert(count==4 && mask==3 && map==1); }
void D_ReceiveTic(ticcmd_t *commands,boolean *mask) { (void)commands;assert(mask[0] && mask[1]);++delivered; }
static esp_err_t adapter_handler(void *ctx,p4_doom_p4mp_frame_handler_t h,void *hctx)
{ (void)ctx;return platform_multiplayer_wifi_set_handler(h,hctx); }
static esp_err_t adapter_send(void *ctx,uint64_t route,const uint8_t *bytes,size_t n)
{ (void)ctx;++attempts;esp_err_t result=platform_multiplayer_wifi_send_to(route,bytes,n);if(result!=ESP_OK)++failures;else usleep(1000);return result; }
static bool adapter_connected(void *ctx,uint64_t route)
{ (void)ctx;return platform_multiplayer_wifi_route_connected(route); }
static void adapter_poll(void *ctx)
{
    (void)ctx;
    ++adapter_polls;
    uint8_t bytes[P4_MP_MAX_DATAGRAM_BYTES];
    ssize_t n;
    while((n=recv(remote_socket,bytes,sizeof(bytes),MSG_DONTWAIT))>0) {
        p4_mp_packet_view_t p;assert(p4_mp_packet_decode(bytes,(size_t)n,&p)==P4_MP_OK);
        if (loading) {
            assert(p.type==P4_MP_PACKET_PING && p.payload_length==8 &&
                   memcmp(p.payload,"GCAWAIT!",8)==0);
            ++waiting_packets;
        }
        if(p.type==P4_MP_PACKET_GAME_MESSAGE && p.payload_length==40 && p.payload[0]=='G') {
            uint32_t tic=(uint32_t)p.payload[4]|(uint32_t)p.payload[5]<<8|(uint32_t)p.payload[6]<<16|(uint32_t)p.payload[7]<<24;
            if(tic>=remote_ack)remote_ack=tic+1;
        }
        size_t out=0;
        assert(p4_mp_session_encode(&remote_session,P4_MP_PACKET_PING,remote_ack,
            (const uint8_t *)(remote_ready ? "GCAREADY" : "GCAWAIT!"),8,bytes,sizeof(bytes),&out)==P4_MP_OK);
        transmit(remote_socket,bytes,out);
    }
    platform_multiplayer_wifi_poll();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    const bool vfs=strcmp(argv[1],"vfs")==0;
    const bool pumped=strcmp(argv[1],"pumped")==0 || strcmp(argv[1],"waiting")==0 || vfs;
    const bool waiting=strcmp(argv[1],"waiting")==0;
    waiting_case=waiting;
    const unsigned delay_ms=pumped ? 5000U : strcmp(argv[1],"expired")==0 ? 3100U : 0U;
    assert(platform_multiplayer_wifi_enable(frame,NULL)==ESP_OK);
    assert(initialize(s_generation)==ESP_OK);
    assert(platform_multiplayer_wifi_host(7,13)==ESP_OK);
    assert(start_mode(s_generation,MODE_HOST,7,13,NULL)==ESP_OK);
    s_status.available=true;assert(open_socket_locked()==ESP_OK);
    wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_AP_STACONNECTED,NULL);
    remote_socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(remote_socket>=0);
    struct sockaddr_in from={.sin_family=AF_INET,.sin_port=htons(PORT+1),.sin_addr.s_addr=inet_addr("127.0.0.1")};
    assert(bind(remote_socket,(void *)&from,sizeof(from))==0);assert(fcntl(remote_socket,F_SETFL,O_NONBLOCK)==0);
    uint8_t bytes[P4_MP_MAX_DATAGRAM_BYTES];size_t n=packet(bytes,7);
    transmit(remote_socket,bytes,n);platform_multiplayer_wifi_poll();assert(received==1);
    const uint64_t route=platform_multiplayer_wifi_status().route_id;
    assert(platform_multiplayer_wifi_route_connected(route));
    p4_mp_session_t session;assert(p4_mp_session_host_start(&session,7,100,3000)==P4_MP_OK);
    assert(p4_mp_session_accept_peer(&session,42,route,1,1,(uint64_t)mock_time/1000U)==P4_MP_OK);
    assert(p4_mp_session_client_start(&remote_session,7,42,100,1,(uint64_t)mock_time/1000U,3000)==P4_MP_OK);
    remote_session.state=P4_MP_SESSION_CONNECTED;remote_session.peers[0].connected=true;remote_session.next_sequence=2;
    const p4_doom_mp_launch_config_t config={.enabled=true,.role=P4_MP_ROLE_HOST,
        .session_id=7,.self_peer_id=100,.remote_peer_id=42,.route_id=route,
        .local_player_slot=0,.player_count=4,.initial_player_mask=3,.input_delay_tics=2,.session_seed=1,
        .setup={.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI,.mode=P4_DOOM_MP_MODE_ALTDEATH,
          .episode=1,.map=1,.skill=3,.no_monsters=true}};
    const p4_doom_p4mp_transport_t transport={.set_handler=adapter_handler,.send_to=adapter_send,.poll=adapter_poll,.connected=adapter_connected};
    assert(p4_doom_gc_prepare(&session,&config,&transport)==ESP_OK);
    /* Match OS order: prepare, validate SD blocks, then configure the engine.
     * Neither peer is engine-ready during validation; no gameplay may start. */
    loading=true;
    if (vfs) {
        startup_vfs_open();storage_reads=0;largest_storage_read=0;slow_storage=true;
        /* The first read ends off block alignment; the second starts there.
         * Each single engine request is longer than the actual Wi-Fi lease. */
        startup_vfs_read(10U*4096U+17U);
        startup_vfs_read(6U*4096U+11U);
        slow_storage=false;
        assert(platform_multiplayer_wifi_route_connected(route));
        assert(storage_reads>16 && largest_storage_read<=4096);
        assert(waiting_packets>=10 && delivered==0 && !p4_doom_gc_failed());
        startup_vfs_read_failure(3U*4096U,2);
    } else if (pumped) {
        for (unsigned elapsed=0;elapsed<delay_ms;elapsed+=100) {
            p4_doom_gc_poll();
            mock_time+=100000;
            assert(delivered==0 && !p4_doom_gc_failed());
        }
        p4_doom_gc_poll();
        assert(waiting_packets>=10);
        assert(platform_multiplayer_wifi_route_connected(route));
    } else {
        mock_time += (int64_t)delay_ms*1000;
    }
    loading=false;
    remote_ready=!waiting;
    net_gamesettings_t settings;
    bool configured=p4_doom_gc_configure(&settings);
    printf("sd_delay_ms=%u configured=%u failed=%u attempts=%u failed_sends=%u delivered=%u elapsed_ms=%lld\n",
        delay_ms,configured,p4_doom_gc_failed(),attempts,failures,delivered,(long long)(mock_time/1000-100));
    assert(configured==((delay_ms<3000 || pumped) && !waiting));
    if (configured) assert(failures==0 && delivered==2);
    if (vfs) {
        const unsigned polls_before=adapter_polls;
        storage_reads=0;largest_storage_read=0;
        storage_delivered=delivered;slow_storage=true;
        startup_vfs_read(10U*4096U);
        slow_storage=false;
        assert(storage_reads>=10 && largest_storage_read<=4096 && adapter_polls>polls_before);
        assert(delivered==storage_delivered && platform_multiplayer_wifi_route_connected(route));
        p4_doom_gc_poll();
        assert(!p4_doom_gc_failed() && session.timeout_ms==3000);
        startup_vfs_close();
    }
    if (waiting) assert(delivered==0 && failures==0);
    if (!pumped && delay_ms>=3000) assert(failures==attempts && delivered==0);
    p4_doom_gc_quit();
    close(remote_socket);platform_multiplayer_wifi_disable();
    return 0;
}
