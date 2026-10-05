// SPDX-License-Identifier: MIT
#include "platform/dice_ble.h"
#include "platform/ble_host.h"
#include "p4/dice_accessory.h"
#include "p4/multiplayer.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_att.h"
#include "host/ble_hs_adv.h"
#include "os/os_mbuf.h"
#include <string.h>

/* Dedicated central connection alongside the game connection. Never changes
 * P4MP lobby advertising, roles, player slots, or the controller connection. */
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(
    0x10,0,0x50,0x4d,0x34,0x50,0x9e,0x9c,0x0d,0x4a,0x44,0x6f,0x20,0x9f,0x0d,0x7b);
static const ble_uuid128_t value_uuid=BLE_UUID128_INIT(
    0x11,0,0x50,0x4d,0x34,0x50,0x9e,0x9c,0x0d,0x4a,0x44,0x6f,0x20,0x9f,0x0d,0x7b);
enum { FOUND=1, CONNECTED, LOST, SECURED, MTU, SERVICE, SERVICE_DONE, CHARACTERISTIC, CHARACTERISTIC_DONE, READ, WROTE };
typedef struct {
    int kind, error;
    uint16_t conn, start, end, handle, length;
    ble_addr_t address;
    uint8_t bytes[80];
} event_t;
static QueueHandle_t events, requests, statuses;
static TaskHandle_t worker;
static void emit(event_t *e) { (void)xQueueSend(events,e,0); }
static int gap(struct ble_gap_event *e, void *arg)
{
    (void)arg;
    event_t v={0};
    switch(e->type) {
    case BLE_GAP_EVENT_DISC: {
        struct ble_hs_adv_fields f={0};
        if (ble_hs_adv_parse_fields(&f,e->disc.data,e->disc.length_data)) break;
        for (unsigned i=0;i<f.num_uuids128;++i) if (!ble_uuid_cmp(&f.uuids128[i].u,&service_uuid.u)) {
            v.kind=FOUND; v.address=e->disc.addr; emit(&v); break;
        }
        break;
    }
    case BLE_GAP_EVENT_CONNECT:
        v.kind=CONNECTED; v.conn=e->connect.conn_handle; v.error=e->connect.status; emit(&v); break;
    case BLE_GAP_EVENT_DISCONNECT:
        v.kind=LOST; v.conn=e->disconnect.conn.conn_handle;v.error=e->disconnect.reason; emit(&v); break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        v.kind=SECURED; v.conn=e->enc_change.conn_handle; v.error=e->enc_change.status; emit(&v); break;
    default: break;
    }
    return 0;
}
static int mtu_done(uint16_t c,const struct ble_gatt_error *err,uint16_t mtu,void *arg)
{
    (void)arg; event_t e={.kind=MTU,.conn=c,.error=err->status,.length=mtu};emit(&e);return 0;
}
static int service_found(uint16_t c,const struct ble_gatt_error *err,const struct ble_gatt_svc *s,void *arg)
{
    (void)arg;
    event_t e={.kind=SERVICE_DONE,.conn=c,.error=err->status==BLE_HS_EDONE?0:err->status};
    if (!err->status && s) {e.kind=SERVICE;e.start=s->start_handle;e.end=s->end_handle;}
    emit(&e);return 0;
}
static int value_found(uint16_t c,const struct ble_gatt_error *err,const struct ble_gatt_chr *s,void *arg)
{
    (void)arg;
    event_t e={.kind=CHARACTERISTIC_DONE,.conn=c,.error=err->status==BLE_HS_EDONE?0:err->status};
    if (!err->status && s) {e.kind=CHARACTERISTIC;e.handle=s->val_handle;}
    emit(&e);return 0;
}
static int read_done(uint16_t c,const struct ble_gatt_error *err,struct ble_gatt_attr *a,void *arg)
{
    (void)arg; event_t e={.kind=READ,.conn=c,.error=err->status};
    if (!e.error && a && a->om && OS_MBUF_PKTLEN(a->om)==sizeof(e.bytes)) {
        e.length=sizeof(e.bytes);
        if (os_mbuf_copydata(a->om,0,e.length,e.bytes)) e.error=1;
    } else e.error=1;
    emit(&e); return 0;
}
static int write_done(uint16_t c,const struct ble_gatt_error *err,struct ble_gatt_attr *a,void *arg)
{
    (void)a;(void)arg;event_t e={.kind=WROTE,.conn=c,.error=err->status};emit(&e);return 0;
}
static void start_mtu(uint16_t conn)
{
    int rc=ble_gattc_exchange_mtu(conn,mtu_done,NULL);
    if(rc==BLE_HS_EALREADY && ble_att_mtu(conn)>=83) {
        event_t e={.kind=MTU,.conn=conn,.length=ble_att_mtu(conn)};emit(&e);
    } else if(rc) {
        ESP_LOGW("p4_dice","DICE_SETUP stage=mtu rc=%d",rc);
        ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);
    }
}
static void sync_host(void *arg) {(void)arg;}
static void reset_host(int why,void *arg) {(void)why;(void)arg;event_t e={.kind=LOST,.conn=BLE_HS_CONN_HANDLE_NONE};emit(&e);}
static const platform_ble_host_client_t client={.on_sync=sync_host,.on_reset=reset_host};
static uint64_t now_ms(void) {return (uint64_t)esp_timer_get_time()/1000;}
static void run(void *arg)
{
    (void)arg;
    ESP_LOGI("p4_dice", "DICE_WORKER core=%d game_core=0", xPortGetCoreID());
    p4_dice_request_t desired={0};
    p4_dice_status_t status={0};
    uint16_t conn=BLE_HS_CONN_HANDLE_NONE, handle=0, service_start=0, service_end=0, found_handle=0;
    uint32_t session=0, sequence=0, received=0;
    uint64_t next_scan=0, last_request=0, last_rx=0, next_io=0, connecting_at=0;
    bool scanning=false, connecting=false, busy=false, dirty=false, secured=false;
    for (;;) {
        uint64_t now=now_ms();
        p4_dice_request_t request;
        if (xQueueReceive(requests,&request,0)==pdTRUE) {
            if (!request.token) {
                memset(&desired,0,sizeof(desired)); last_request=0;
            } else {
                if (memcmp(&desired,&request,sizeof(request))) {
                    ESP_LOGI("p4_dice","DICE_GAME player=%u token=%lu roll=%u hold=%u held=%u",
                        request.player_slot+1,(unsigned long)request.token,request.enabled?1:0,request.can_hold?1:0,request.held_mask);
                    dirty=true; status=(p4_dice_status_t){.phase=handle ? P4_DICE_WAITING : P4_DICE_SEARCHING,
                        .token=request.token,.player_slot=request.player_slot};
                }
                desired=request; last_request=now;
            }
        }
        bool active=desired.token && now-last_request < 2000;
        if (!active) {
            if (conn!=BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);
            if (scanning) ble_gap_disc_cancel();
            if (connecting) ble_gap_conn_cancel();
            conn=BLE_HS_CONN_HANDLE_NONE;handle=0;scanning=false;connecting=false;busy=false;secured=false;
            status=(p4_dice_status_t){0};
        }
        event_t e;
        for (int i=0;i<16 && xQueueReceive(events,&e,0)==pdTRUE;++i) {
            if (e.kind==FOUND && active && scanning && !connecting && conn==BLE_HS_CONN_HANDLE_NONE) {
                uint8_t addr_type;
                ble_gap_disc_cancel(); scanning=false;
                if (platform_ble_host_own_addr_type(&addr_type)==ESP_OK &&
                    !ble_gap_connect(addr_type,&e.address,5000,NULL,gap,NULL)) {
                    connecting=true; connecting_at=now;
                }
            } else if (e.kind==CONNECTED) {
                connecting=false;
                if (!e.error && active && conn==BLE_HS_CONN_HANDLE_NONE) {
                    conn=e.conn;handle=service_start=service_end=found_handle=0; session=esp_random(); if (!session) session=1;
                    ESP_LOGI("p4_dice", "DICE_LINK connected=1");
                    sequence=received=0; last_rx=now; dirty=true;
                    struct ble_gap_conn_desc d;
                    if (!ble_gap_conn_find(conn,&d) && d.sec_state.encrypted) {
                        secured=true;start_mtu(conn);
                    } else {int rc=ble_gap_security_initiate(conn);
                        if(rc && rc!=BLE_HS_EALREADY) {ESP_LOGW("p4_dice","DICE_SETUP stage=security rc=%d",rc);ble_gap_terminate(conn,BLE_ERR_AUTH_FAIL);}}
                } else if (!e.error) ble_gap_terminate(e.conn,BLE_ERR_REM_USER_CONN_TERM);
            } else if (e.kind==LOST && (e.conn==conn || e.conn==BLE_HS_CONN_HANDLE_NONE)) {
                ESP_LOGI("p4_dice", "DICE_LINK connected=0 reason=%d",e.error);
                conn=BLE_HS_CONN_HANDLE_NONE;handle=0;busy=false;secured=false;
                status.phase=active ? P4_DICE_SEARCHING : P4_DICE_OFFLINE;
            } else if (e.conn==conn && conn!=BLE_HS_CONN_HANDLE_NONE) {
                if (e.error) {ESP_LOGW("p4_dice","DICE_SETUP event=%d rc=%d",e.kind,e.error);ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);continue;}
                if (e.kind==SECURED) {
                    last_rx=now;
                    struct ble_gap_conn_desc d;
                    if (ble_gap_conn_find(conn,&d) || !d.sec_state.encrypted || !d.sec_state.bonded)
                        ble_gap_terminate(conn,BLE_ERR_AUTH_FAIL);
                    else {secured=true;ESP_LOGI("p4_dice", "DICE_LINK encrypted=1 bonded=1");start_mtu(conn);}
                } else if (e.kind==MTU && secured) {
                    last_rx=now;
                    ESP_LOGI("p4_dice","DICE_SETUP mtu=%u",e.length);
                    int rc=e.length<83?BLE_HS_EMSGSIZE:
                        ble_gattc_disc_svc_by_uuid(conn,&service_uuid.u,service_found,NULL);
                    if(rc) {ESP_LOGW("p4_dice","DICE_SETUP stage=service rc=%d",rc);ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);}
                } else if (e.kind==SERVICE && secured) {
                    service_start=e.start;service_end=e.end;
                } else if (e.kind==SERVICE_DONE && secured) {
                    last_rx=now;
                    int rc=!service_start?BLE_HS_ENOENT:
                        ble_gattc_disc_chrs_by_uuid(conn,service_start,service_end,&value_uuid.u,value_found,NULL);
                    if(rc) {ESP_LOGW("p4_dice","DICE_SETUP stage=characteristic rc=%d",rc);ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);}
                } else if (e.kind==CHARACTERISTIC && secured) {found_handle=e.handle;}
                else if (e.kind==CHARACTERISTIC_DONE && secured) {
                    last_rx=now;
                    if(!found_handle) ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);
                    else {handle=found_handle;dirty=true;ESP_LOGI("p4_dice","DICE_SETUP ready=1");}
                }
                else if (e.kind==WROTE) {busy=false;last_rx=now;next_io=now+50;}
                else if (e.kind==READ) {
                    busy=false; p4_mp_packet_view_t packet; p4_dice_request_t r; p4_dice_phase_t phase;
                    if (p4_mp_packet_decode(e.bytes,e.length,&packet)==P4_MP_OK &&
                        packet.type==P4_MP_PACKET_ACCESSORY && packet.session_id==session &&
                        packet.peer_id==2 && packet.sequence>received &&
                        p4_dice_decode(packet.payload,packet.payload_length,P4_DICE_STATUS,&r,&phase)) {
                        received=packet.sequence;last_rx=now;
                        p4_dice_status_t accepted;
                        if (p4_dice_accept_status(&desired,&r,phase,&accepted)) {
                            if(status.phase!=phase || status.held_mask!=accepted.held_mask)
                                ESP_LOGI("p4_dice", "DICE_STATUS player=%u token=%lu phase=%u held=%u changed=%u",
                                    (unsigned)r.player_slot+1U,(unsigned long)r.token,(unsigned)phase,
                                    accepted.held_mask,accepted.hold_changed?1:0);
                            status=accepted;
                        }
                    }
                }
            }
        }
        if (active) {
            if (!platform_ble_host_ready()) (void)platform_ble_host_start();
            if (connecting && now-connecting_at>6000) {ble_gap_conn_cancel();connecting=false;}
            if (conn==BLE_HS_CONN_HANDLE_NONE && !connecting && now>=next_scan && platform_ble_host_ready()) {
                uint8_t addr_type;
                struct ble_gap_disc_params params={.passive=1,.itvl=160,.window=80,.filter_duplicates=1};
                /* A busy lobby/controller scan is left alone. Retry later. */
                if (!ble_gap_disc_active() && platform_ble_host_own_addr_type(&addr_type)==ESP_OK)
                    scanning=ble_gap_disc(addr_type,1500,&params,gap,NULL)==0;
                next_scan=now+2000;
            }
            if (scanning && !ble_gap_disc_active()) scanning=false;
            if (conn!=BLE_HS_CONN_HANDLE_NONE && now-last_rx>(handle?4000U:15000U)) ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);
            if (handle && secured && !busy && now>=next_io) {
                if (dirty) {
                    uint8_t payload[48],packet[80];size_t length=0;
                    if (p4_dice_encode(&desired,P4_DICE_WAITING,P4_DICE_REQUEST,payload) &&
                        p4_mp_packet_encode(P4_MP_PACKET_ACCESSORY,session,1,++sequence,0,payload,48,packet,sizeof(packet),&length)==P4_MP_OK &&
                        !ble_gattc_write_flat(conn,handle,packet,(uint16_t)length,write_done,NULL)) {
                        busy=true;dirty=false;
                        ESP_LOGI("p4_dice", "DICE_REQUEST player=%u token=%lu enabled=%u held=%u faces=%u,%u,%u,%u,%u",
                            (unsigned)desired.player_slot+1U,(unsigned long)desired.token,
                            desired.enabled?1U:0U,(unsigned)desired.held_mask,
                            desired.faces[0],desired.faces[1],desired.faces[2],desired.faces[3],desired.faces[4]);
                    }
                } else if (!ble_gattc_read(conn,handle,read_done,NULL)) busy=true;
                next_io=now+100;
            }
        }
        xQueueOverwrite(statuses,&status);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t platform_dice_ble_prepare(void)
{
    if (worker) return ESP_OK;
    if (!events) events=xQueueCreate(16,sizeof(event_t));
    if (!requests) requests=xQueueCreate(1,sizeof(p4_dice_request_t));
    if (!statuses) statuses=xQueueCreate(1,sizeof(p4_dice_status_t));
    if (!events || !requests || !statuses) return ESP_ERR_NO_MEM;
    esp_err_t result=platform_ble_host_register_client(&client);
    if (result!=ESP_OK) return result;
    return xTaskCreatePinnedToCore(run,"p4_dice",4096,NULL,4,&worker,1)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
bool platform_dice_ble_exchange(void *context,const p4_dice_request_t *r,p4_dice_status_t *s)
{
    (void)context;
    if (!worker || !s || !p4_dice_request_valid(r)) return false;
    xQueueOverwrite(requests,r); *s=(p4_dice_status_t){.phase=P4_DICE_SEARCHING};
    (void)xQueuePeek(statuses,s,0);
    if (s->token != r->token || s->player_slot != r->player_slot)
        *s=(p4_dice_status_t){.token=r->token,.player_slot=r->player_slot,.phase=P4_DICE_SEARCHING};
    return true;
}
void platform_dice_ble_close(void)
{
    if (requests) {const p4_dice_request_t empty={0};xQueueOverwrite(requests,&empty);}
}
