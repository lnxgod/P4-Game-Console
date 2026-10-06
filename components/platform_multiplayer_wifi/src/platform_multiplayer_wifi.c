// SPDX-License-Identifier: MIT
#include "platform/multiplayer_wifi.h"
#include "platform/ble_host.h"
#include "p4/multiplayer.h"
#include "discovery.h"
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#pragma GCC diagnostic pop

enum { MODE_OFF, MODE_BROWSER, MODE_HOST, MODE_CLIENT, PORT=42424, RAW_APS=32 };
static const char *TAG="p4_wifi_mp";
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static unsigned s_mode, s_generation;
static uint32_t s_session;
static uint16_t s_game;
static platform_multiplayer_wifi_status_t s_status;
static platform_multiplayer_wifi_lobby_t s_rooms[4];
static wifi_ap_record_t s_records[4], s_target;
static size_t s_count;
static platform_multiplayer_wifi_handler_t s_handler;
static void *s_context;
static int s_socket=-1;
static uint32_t s_peer_ip, s_local_ip, s_gateway_ip;
static int64_t s_last_rx;
static uint32_t s_peer_ips[3];
static int64_t s_peer_seen[3];
static bool s_ip_ready, s_associated;
static esp_netif_t *s_sta, *s_ap;
#define LOCK() ((void)xSemaphoreTake(s_lock,portMAX_DELAY))
#define UNLOCK() ((void)xSemaphoreGive(s_lock))

static void close_link_locked(void)
{
    if (s_socket>=0) { close(s_socket); s_socket=-1; }
    s_status.ready=false; s_status.connected=false; s_status.route_id=0;
    memset(s_peer_ips,0,sizeof(s_peer_ips));memset(s_peer_seen,0,sizeof(s_peer_seen));
    s_peer_ip=0; s_local_ip=0; s_gateway_ip=0; s_last_rx=0;
    s_ip_ready=false; s_associated=false;
}
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    LOCK();
    if (base==IP_EVENT && id==IP_EVENT_STA_GOT_IP && s_mode==MODE_CLIENT) {
        const ip_event_got_ip_t *event=data;
        s_local_ip=event->ip_info.ip.addr; s_gateway_ip=event->ip_info.gw.addr;
        s_ip_ready=true;
    } else if (base==WIFI_EVENT && id==WIFI_EVENT_AP_STACONNECTED && s_mode==MODE_HOST) {
        s_associated=true;
    } else if (base==WIFI_EVENT && (id==WIFI_EVENT_STA_DISCONNECTED || id==WIFI_EVENT_AP_STADISCONNECTED)) {
        s_associated=false; s_status.connected=false; s_status.route_id=0; s_peer_ip=0; s_last_rx=0;
        memset(s_peer_ips,0,sizeof(s_peer_ips));memset(s_peer_seen,0,sizeof(s_peer_seen));
        if (s_mode==MODE_CLIENT) { s_ip_ready=false; s_status.ready=false; }
    }
    UNLOCK();
}
static esp_err_t initialize(void)
{
    /* Serialize C6 activation through the already shared NimBLE owner. Its
     * VHCI initializer must run before Hosted RPC on pinned Hosted 1.4.7. */
    esp_err_t e=platform_ble_host_start();
    if(e!=ESP_OK) return e;
    int64_t deadline=esp_timer_get_time()+INT64_C(15000000);
    while(!platform_ble_host_ready()) {
        if(platform_ble_host_status().state==PLATFORM_BLE_HOST_ERROR) return ESP_FAIL;
        if(esp_timer_get_time()>=deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    e=esp_netif_init(); if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) return e;
    e=esp_event_loop_create_default(); if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) return e;
    s_sta=esp_netif_create_default_wifi_sta(); s_ap=esp_netif_create_default_wifi_ap();
    if(!s_sta || !s_ap) return ESP_ERR_NO_MEM;
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
    e=esp_wifi_init(&config); if(e!=ESP_OK) return e;
    e=esp_wifi_set_storage(WIFI_STORAGE_RAM); if(e!=ESP_OK) return e;
    e=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL); if(e!=ESP_OK) return e;
    return esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL);
}
static esp_err_t start_mode(unsigned mode, uint32_t session, uint16_t game, const wifi_ap_record_t *target)
{
    (void)esp_wifi_scan_stop(); (void)esp_wifi_stop();
    LOCK(); close_link_locked(); UNLOCK();
    if(mode==MODE_OFF) return ESP_OK;
    esp_err_t e=esp_wifi_set_mode(mode==MODE_HOST?WIFI_MODE_AP:WIFI_MODE_STA);
    if(e!=ESP_OK) return e;
    wifi_config_t config={0};
    if(mode==MODE_HOST) {
        char name[33]; if(!p4_wifi_room_name(name,session,game)) return ESP_ERR_INVALID_ARG;
        memcpy(config.ap.ssid,name,strlen(name)); config.ap.ssid_len=(uint8_t)strlen(name);
        config.ap.channel=6; config.ap.max_connection=3;
        config.ap.authmode=WIFI_AUTH_OPEN; config.ap.beacon_interval=100;
        e=esp_wifi_set_config(WIFI_IF_AP,&config);
    } else if(mode==MODE_CLIENT) {
        memcpy(config.sta.ssid,target->ssid,sizeof(config.sta.ssid));
        memcpy(config.sta.bssid,target->bssid,6); config.sta.bssid_set=true;
        config.sta.channel=target->primary; config.sta.threshold.authmode=WIFI_AUTH_OPEN;
        e=esp_wifi_set_config(WIFI_IF_STA,&config);
    }
    if(e!=ESP_OK) return e;
    e=esp_wifi_start(); if(e!=ESP_OK) return e;
    /* Disable station power save to avoid bursty race input. */
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    if(mode==MODE_CLIENT) return esp_wifi_connect();
    if(mode==MODE_HOST) {
        esp_netif_ip_info_t ip;
        e=esp_netif_get_ip_info(s_ap,&ip);
        if(e==ESP_OK) { LOCK(); s_local_ip=ip.ip.addr; s_ip_ready=true; UNLOCK(); }
    }
    return e;
}
static void scan_rooms(unsigned generation)
{
    wifi_scan_config_t config={.show_hidden=false,.scan_type=WIFI_SCAN_TYPE_PASSIVE,.scan_time.passive=120};
    wifi_ap_record_t records[RAW_APS]; uint16_t count=RAW_APS;
    esp_err_t e=esp_wifi_scan_start(&config,true);
    if(e==ESP_OK) e=esp_wifi_scan_get_ap_records(&count,records);
    LOCK();
    if(s_generation==generation && s_mode==MODE_BROWSER) {
        s_count=0;
        if(e==ESP_OK) for(unsigned i=0;i<count && i<RAW_APS && s_count<4;++i) {
            uint32_t session; uint16_t game;
            if(records[i].authmode!=WIFI_AUTH_OPEN || !p4_wifi_room_parse(records[i].ssid,&session,&game)) continue;
            uint64_t id=0; for(unsigned j=0;j<6;++j) id=(id<<8)|records[i].bssid[j];
            if(!id) continue;
            s_rooms[s_count]=(platform_multiplayer_wifi_lobby_t){.lobby_id=id,.session_id=session,.game_token=game,.rssi=records[i].rssi,.players_present=1,.player_capacity=4};
            s_records[s_count++]=records[i];
        }
        s_status.last_error=e;
    }
    UNLOCK();
}
static esp_err_t open_socket_locked(void)
{
    s_socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(s_socket<0) return ESP_FAIL;
    int yes=1;
    (void)setsockopt(s_socket,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes));
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(PORT),.sin_addr.s_addr=s_local_ip};
    if(bind(s_socket,(struct sockaddr *)&addr,sizeof(addr))<0 || fcntl(s_socket,F_SETFL,O_NONBLOCK)<0) {
        close(s_socket); s_socket=-1; return ESP_FAIL;
    }
    s_status.ready=true; return ESP_OK;
}
static void worker(void *arg)
{
    (void)arg;
    bool initialized=false, attempted=false;
    unsigned applied=UINT32_MAX;
    int64_t next_scan=0, join_deadline=0;
    for(;;) {
        LOCK(); unsigned generation=s_generation, mode=s_mode; uint32_t session=s_session;
        uint16_t game=s_game; wifi_ap_record_t target=s_target; UNLOCK();
        if(generation!=applied) {
            esp_err_t e=ESP_OK;
            if(mode!=MODE_OFF && !attempted) { attempted=true; e=initialize(); initialized=(e==ESP_OK); }
            if(mode!=MODE_OFF && !initialized && e==ESP_OK) e=ESP_ERR_INVALID_STATE;
            if(initialized && e==ESP_OK) e=start_mode(mode,session,game,&target);
            LOCK();
            if(generation==s_generation) {
                s_status.available=initialized && mode!=MODE_OFF && e==ESP_OK;
                s_status.starting=e==ESP_OK && mode==MODE_CLIENT;
                s_status.last_error=e;
            }
            UNLOCK();
            ESP_LOGI(TAG,"WIFI_LOCAL mode=%u result=%s",mode,esp_err_to_name(e));
            applied=generation; next_scan=0; join_deadline=esp_timer_get_time()+INT64_C(12000000);
        }
        LOCK();
        if(s_ip_ready && s_socket<0 && s_status.available && s_generation==applied) {
            s_status.last_error=open_socket_locked(); s_status.starting=false;
        }
        if(mode==MODE_CLIENT && s_status.starting && esp_timer_get_time()>join_deadline) {
            s_status.starting=false; s_status.last_error=ESP_ERR_TIMEOUT;
        }
        bool scan=initialized && s_status.available && mode==MODE_BROWSER && s_generation==applied;
        UNLOCK();
        if(scan && esp_timer_get_time()>=next_scan) { scan_rooms(applied); next_scan=esp_timer_get_time()+INT64_C(2000000); }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
static esp_err_t request_mode(unsigned mode, uint32_t session, uint16_t game)
{
    if(!s_lock) return ESP_ERR_INVALID_STATE;
    LOCK(); s_mode=mode; s_session=session; s_game=game; ++s_generation;
    s_count=0; close_link_locked();
    s_status.enabled=mode!=MODE_OFF; s_status.starting=mode!=MODE_OFF;
    s_status.available=false; s_status.last_error=ESP_OK; UNLOCK(); return ESP_OK;
}
esp_err_t platform_multiplayer_wifi_enable(platform_multiplayer_wifi_handler_t handler, void *ctx)
{
    if(!s_lock) {
        s_lock=xSemaphoreCreateMutex(); if(!s_lock) return ESP_ERR_NO_MEM;
        if(xTaskCreateWithCaps(worker,"p4_wifi_mp",12288,NULL,4,&s_task,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
            vSemaphoreDelete(s_lock); s_lock=NULL; return ESP_ERR_NO_MEM;
        }
    }
    (void)platform_multiplayer_wifi_set_handler(handler,ctx);
    return request_mode(MODE_BROWSER,0,0);
}
void platform_multiplayer_wifi_disable(void) { if(s_lock) (void)request_mode(MODE_OFF,0,0); }
esp_err_t platform_multiplayer_wifi_browse(void) { return request_mode(MODE_BROWSER,0,0); }
esp_err_t platform_multiplayer_wifi_host(uint32_t session,uint16_t game)
{ return session && game ? request_mode(MODE_HOST,session,game) : ESP_ERR_INVALID_ARG; }
size_t platform_multiplayer_wifi_list_lobbies(platform_multiplayer_wifi_lobby_t *out,size_t capacity)
{
    if(!s_lock || !out) return 0;
    LOCK(); size_t n=s_count<capacity?s_count:capacity; memcpy(out,s_rooms,n*sizeof(*out)); UNLOCK(); return n;
}
esp_err_t platform_multiplayer_wifi_join(uint64_t lobby)
{
    if(!s_lock) return ESP_ERR_INVALID_STATE;
    LOCK();
    for(size_t i=0;i<s_count;++i) if(s_rooms[i].lobby_id==lobby && s_mode==MODE_BROWSER) {
        s_target=s_records[i]; s_mode=MODE_CLIENT; s_session=s_rooms[i].session_id; s_game=s_rooms[i].game_token;
        ++s_generation; close_link_locked(); s_status.starting=true; s_status.available=false; s_count=0;
        UNLOCK(); return ESP_OK;
    }
    UNLOCK(); return ESP_ERR_NOT_FOUND;
}
esp_err_t platform_multiplayer_wifi_set_handler(platform_multiplayer_wifi_handler_t handler,void *ctx)
{
    if(!s_lock) return ESP_ERR_INVALID_STATE;
    LOCK(); s_handler=handler; s_context=ctx; UNLOCK(); return ESP_OK;
}
void platform_multiplayer_wifi_poll(void)
{
    if(!s_lock) return;
    for(unsigned i=0;i<8;++i) {
        uint8_t bytes[P4_MP_MAX_DATAGRAM_BYTES+1]; struct sockaddr_in from; socklen_t len=sizeof(from);
        LOCK();
        ssize_t n=s_socket>=0?recvfrom(s_socket,bytes,sizeof(bytes),MSG_DONTWAIT,(struct sockaddr *)&from,&len):-1;
        if(n<0) { UNLOCK(); break; }
        p4_mp_packet_view_t packet;
        bool valid=len==sizeof(from) && from.sin_family==AF_INET && from.sin_port==htons(PORT) &&
            from.sin_addr.s_addr!=s_local_ip && (size_t)n<=P4_MP_MAX_DATAGRAM_BYTES &&
            p4_mp_packet_decode(bytes,(size_t)n,&packet)==P4_MP_OK;
        if(valid) valid=s_mode==MODE_HOST ? s_associated : (s_mode==MODE_CLIENT && from.sin_addr.s_addr==s_gateway_ip);
        /* Session-zero DISCOVER is allowed; every other packet binds to the
         * selected room. Peer/session/game rules remain enforced by P4MP. */
        if(valid && packet.type!=P4_MP_PACKET_DISCOVER && packet.session_id!=s_session) valid=false;
        unsigned slot=3;
        if(valid)for(unsigned j=0;j<3;++j)if(s_peer_ips[j]==from.sin_addr.s_addr){slot=j;break;}
        if(valid&&slot==3)for(unsigned j=0;j<3;++j)if(!s_peer_ips[j]){slot=j;break;}
        if(slot==3)valid=false;
        if(!valid) { UNLOCK(); continue; }
        s_peer_ips[slot]=from.sin_addr.s_addr;s_peer_seen[slot]=esp_timer_get_time();
        if(!s_peer_ip)s_peer_ip=from.sin_addr.s_addr;
        s_last_rx=esp_timer_get_time();s_status.route_id=P4_MP_WIFI_ROUTE_PREFIX|s_peer_ip;
        s_status.connected=true; ++s_status.rx_frames;
        uint64_t route=P4_MP_WIFI_ROUTE_PREFIX|from.sin_addr.s_addr; platform_multiplayer_wifi_handler_t handler=s_handler; void *ctx=s_context;
        UNLOCK(); if(handler) handler(ctx,route,bytes,(size_t)n);
    }
}
esp_err_t platform_multiplayer_wifi_send_to(uint64_t route,const uint8_t *bytes,size_t length)
{
    p4_mp_packet_view_t packet;
    if(!s_lock||p4_mp_packet_decode(bytes,length,&packet)!=P4_MP_OK||
       (route&&(route&UINT64_C(0xffffffff00000000))!=P4_MP_WIFI_ROUTE_PREFIX))return ESP_ERR_INVALID_ARG;
    LOCK();
    if(s_socket<0||!s_status.ready){UNLOCK();return ESP_ERR_INVALID_STATE;}
    unsigned sent=0;bool ok=true;
    for(unsigned i=0;i<3;++i) {
        uint32_t dest=s_peer_ips[i];if(!dest||(route&&(uint32_t)route!=dest))continue;
        struct sockaddr_in to={.sin_family=AF_INET,.sin_port=htons(PORT),.sin_addr.s_addr=dest};
        ssize_t n=sendto(s_socket,bytes,length,MSG_DONTWAIT,(struct sockaddr *)&to,sizeof(to));
        ++sent;if(n==(int)length)++s_status.tx_frames;else ok=false;
    }
    if(!sent&&!route) {
        uint32_t dest=s_mode==MODE_CLIENT?s_gateway_ip:INADDR_BROADCAST;
        struct sockaddr_in to={.sin_family=AF_INET,.sin_port=htons(PORT),.sin_addr.s_addr=dest};
        ssize_t n=sendto(s_socket,bytes,length,MSG_DONTWAIT,(struct sockaddr *)&to,sizeof(to));
        ++sent;if(n==(int)length)++s_status.tx_frames;else ok=false;
    }
    UNLOCK();return sent&&ok?ESP_OK:ESP_FAIL;
}
esp_err_t platform_multiplayer_wifi_send(const uint8_t *bytes,size_t length)
{ return platform_multiplayer_wifi_send_to(0,bytes,length); }
void platform_multiplayer_wifi_reset_route(void)
{
    if(!s_lock) return;
    LOCK(); memset(s_peer_ips,0,sizeof(s_peer_ips));memset(s_peer_seen,0,sizeof(s_peer_seen));
    s_peer_ip=0; s_last_rx=0; s_status.route_id=0; s_status.connected=false;
    if(s_socket>=0) { uint8_t discard[P4_MP_MAX_DATAGRAM_BYTES+1]; for(unsigned i=0;i<16;++i) if(recv(s_socket,discard,sizeof(discard),MSG_DONTWAIT)<0) break; }
    UNLOCK();
}
platform_multiplayer_wifi_status_t platform_multiplayer_wifi_status(void)
{
    if(!s_lock) return (platform_multiplayer_wifi_status_t){0};
    LOCK(); platform_multiplayer_wifi_status_t result=s_status;
    result.connected=result.connected && s_last_rx && esp_timer_get_time()-s_last_rx<INT64_C(3000000);
    UNLOCK(); return result;
}
bool platform_multiplayer_wifi_route_connected(uint64_t route)
{
    if(!s_lock||(route&UINT64_C(0xffffffff00000000))!=P4_MP_WIFI_ROUTE_PREFIX)return false;
    LOCK();bool connected=false;
    for(unsigned i=0;i<3;++i)if(s_peer_ips[i]&&(uint32_t)route==s_peer_ips[i]&&s_peer_seen[i]&&
        esp_timer_get_time()-s_peer_seen[i]<INT64_C(3000000))connected=s_status.ready;
    UNLOCK();return connected;
}
