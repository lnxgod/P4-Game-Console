// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include "mocks/mock.h"
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
#include "../src/platform_multiplayer_wifi.c"
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
int main(void)
{
    char name[33];uint32_t session;uint16_t game;
    assert(p4_wifi_room_name(name,0xdeadbeef,0x4567));assert(strlen(name)==21);
    assert(p4_wifi_room_parse((uint8_t *)name,&session,&game));assert(session==0xdeadbeef&&game==0x4567);
    for(unsigned i=0;i<21;++i){char saved=name[i];name[i]='!';assert(!p4_wifi_room_parse((uint8_t *)name,&session,&game));name[i]=saved;}
    name[21]='x';assert(!p4_wifi_room_parse((uint8_t *)name,&session,&game));name[21]=0;
    memset(name,'x',sizeof(name));assert(!p4_wifi_room_parse((uint8_t *)name,&session,&game));
    assert(platform_multiplayer_wifi_enable(frame,NULL)==ESP_OK);assert(initialize()==ESP_OK);
    assert(p4_wifi_room_name((char *)mock_records[0].ssid,7,13));mock_records[0].bssid[5]=4;mock_records[0].authmode=WIFI_AUTH_OPEN;mock_count=1;
    scan_rooms(s_generation);platform_multiplayer_wifi_lobby_t rooms[4];assert(platform_multiplayer_wifi_list_lobbies(rooms,4)==1);
    assert(rooms[0].session_id==7 && rooms[0].game_token==13);
    assert(platform_multiplayer_wifi_join(999)==ESP_ERR_NOT_FOUND);
    assert(platform_multiplayer_wifi_join(rooms[0].lobby_id)==ESP_OK);
    scan_rooms(s_generation-1);assert(platform_multiplayer_wifi_list_lobbies(rooms,4)==0);
    assert(start_mode(MODE_CLIENT,7,13,&s_target)==ESP_OK);assert(mock_config.sta.bssid_set && mock_config.sta.bssid[5]==4);
    assert(platform_multiplayer_wifi_host(7,13)==ESP_OK);
    assert(start_mode(MODE_HOST,7,13,NULL)==ESP_OK);assert(mock_mode==WIFI_MODE_AP && mock_config.ap.max_connection==3);
    s_status.available=true;assert(open_socket_locked()==ESP_OK);
    int peer=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(peer>=0);
    struct sockaddr_in from={.sin_family=AF_INET,.sin_port=htons(PORT+1),.sin_addr.s_addr=inet_addr("127.0.0.1")};
    assert(bind(peer,(void *)&from,sizeof(from))==0);
    assert(fcntl(peer,F_SETFL,O_NONBLOCK)==0);
    uint8_t bytes[P4_MP_MAX_DATAGRAM_BYTES+1];size_t n=packet(bytes,7);
    transmit(peer,bytes,n);platform_multiplayer_wifi_poll();assert(received==0); // not associated
    wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_AP_STACONNECTED,NULL);
    transmit(peer,bytes,n);platform_multiplayer_wifi_poll();assert(received==1);
    uint64_t route=platform_multiplayer_wifi_status().route_id;assert(platform_multiplayer_wifi_route_connected(route));
    n=packet(bytes,8);transmit(peer,bytes,n);platform_multiplayer_wifi_poll();assert(received==1);
    n=packet(bytes,7);bytes[n-1]^=1;transmit(peer,bytes,n);platform_multiplayer_wifi_poll();assert(received==1);
    memset(bytes,0,sizeof(bytes));transmit(peer,bytes,sizeof(bytes));platform_multiplayer_wifi_poll();assert(received==1);
    n=packet(bytes,7);for(unsigned i=0;i<9;++i)transmit(peer,bytes,n);
    platform_multiplayer_wifi_poll();assert(received==9);platform_multiplayer_wifi_poll();assert(received==10);
    assert(platform_multiplayer_wifi_send(bytes,n)==ESP_OK);usleep(1000);assert(recv(peer,bytes,sizeof(bytes),0)==(int)n);
    int others[3];
    for(unsigned i=0;i<3;++i) {
        others[i]=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(others[i]>=0);
        from.sin_port=htons((uint16_t)(PORT+2+i));
        assert(bind(others[i],(void *)&from,sizeof(from))==0);assert(fcntl(others[i],F_SETFL,O_NONBLOCK)==0);
        transmit(others[i],bytes,n);platform_multiplayer_wifi_poll();
    }
    assert(received==12); // The fourth remote is over the three-peer limit.
    for(unsigned i=0;i<3;++i)assert(platform_multiplayer_wifi_route_connected(P4_MP_WIFI_ROUTE_PREFIX|htonl(0x7f000002u+i)));
    assert(platform_multiplayer_wifi_send(bytes,n)==ESP_OK);usleep(1000);
    assert(recv(peer,bytes,sizeof(bytes),0)==(int)n);
    for(unsigned i=0;i<2;++i)assert(recv(others[i],bytes,sizeof(bytes),0)==(int)n);
    assert(recv(others[2],bytes,sizeof(bytes),0)<0);
    assert(platform_multiplayer_wifi_send_to(P4_MP_WIFI_ROUTE_PREFIX|inet_addr("127.0.0.3"),bytes,n)==ESP_OK);usleep(1000);
    assert(recv(others[0],bytes,sizeof(bytes),0)==(int)n);
    assert(recv(peer,bytes,sizeof(bytes),0)<0&&recv(others[1],bytes,sizeof(bytes),0)<0);
    assert(platform_multiplayer_wifi_send_to(P4_MP_WIFI_ROUTE_PREFIX|inet_addr("127.0.0.5"),bytes,n)!=ESP_OK);
    wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_AP_STADISCONNECTED,NULL);
    for(unsigned i=0;i<3;++i)assert(platform_multiplayer_wifi_route_connected(P4_MP_WIFI_ROUTE_PREFIX|htonl(0x7f000002u+i)));
    /* A departed lobby guest must release capacity without restarting the
     * host; two still-active guests keep their routes and a fourth IP joins. */
    mock_time+=2900000;
    for(unsigned i=0;i<2;++i) { transmit(others[i],bytes,n);platform_multiplayer_wifi_poll(); }
    mock_time+=200001;
    transmit(others[2],bytes,n);platform_multiplayer_wifi_poll();
    assert(received==15);
    assert(!platform_multiplayer_wifi_route_connected(route));
    for(unsigned i=1;i<4;++i)assert(platform_multiplayer_wifi_route_connected(P4_MP_WIFI_ROUTE_PREFIX|htonl(0x7f000002u+i)));
    assert(platform_multiplayer_wifi_send_to(P4_MP_WIFI_ROUTE_PREFIX|inet_addr("127.0.0.5"),bytes,n)==ESP_OK);
    usleep(1000);assert(recv(others[2],bytes,sizeof(bytes),0)==(int)n);
    assert(platform_multiplayer_wifi_send_to(route,bytes,n)!=ESP_OK);
    for(unsigned i=0;i<3;++i)close(others[i]);
    mock_time+=3000001;assert(!platform_multiplayer_wifi_route_connected(route));
    wifi_event(NULL,WIFI_EVENT,WIFI_EVENT_AP_STADISCONNECTED,NULL);assert(!platform_multiplayer_wifi_status().connected);
    platform_multiplayer_wifi_disable();assert(platform_multiplayer_wifi_send(bytes,n)==ESP_ERR_INVALID_STATE);
    close(peer);puts("Wi-Fi: three simultaneous UDP peers, addressed and broadcast delivery, peer/session/CRC/size rejection, bounded poll, expiry, disconnect and discovery passed");return 0;
}
