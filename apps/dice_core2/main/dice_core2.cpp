// SPDX-License-Identifier: MIT
#include <M5Unified.h>
#include "dice_feedback.h"
#include "dice_view.h"
#include "dice_controls.h"
#include <cmath>
#include <algorithm>
#include "esp_heap_caps.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
extern "C" {
#include "p4/dice_accessory.h"
#include "p4/multiplayer.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"
void ble_store_config_init(void);
}
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static p4_dice_request_t desired{};
static p4_dice_phase_t wire_phase=P4_DICE_WAITING;
static uint8_t wire_held=0;
static uint16_t wire_hold_sequence=0;
static uint32_t wire_token=0;
static uint32_t session=0, last_sequence=0, tx_sequence=0;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE;
static bool radio_ready=false, linked=false, link_requested=false;
static uint8_t own_address_type;
static uint32_t last_request_ms=0, advertise_until=0;
static const ble_uuid128_t service_uuid=BLE_UUID128_INIT(
    0x10,0,0x50,0x4d,0x34,0x50,0x9e,0x9c,0x0d,0x4a,0x44,0x6f,0x20,0x9f,0x0d,0x7b);
static const ble_uuid128_t value_uuid=BLE_UUID128_INIT(
    0x11,0,0x50,0x4d,0x34,0x50,0x9e,0x9c,0x0d,0x4a,0x44,0x6f,0x20,0x9f,0x0d,0x7b);
static uint32_t now_ms() {return (uint32_t)((uint64_t)esp_timer_get_time()/1000);}
static int access(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctxt,void *arg)
{
    (void)attr;(void)arg;
    struct ble_gap_conn_desc d;
    if (ble_gap_conn_find(conn,&d) || !d.sec_state.encrypted || !d.sec_state.bonded)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    uint8_t bytes[80], payload[48];size_t length=0;
    if (ctxt->op==BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (OS_MBUF_PKTLEN(ctxt->om)!=sizeof(bytes) || os_mbuf_copydata(ctxt->om,0,sizeof(bytes),bytes))
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        p4_mp_packet_view_t packet; p4_dice_request_t r; p4_dice_phase_t phase;
        if (p4_mp_packet_decode(bytes,sizeof(bytes),&packet)!=P4_MP_OK ||
            packet.type!=P4_MP_PACKET_ACCESSORY || packet.peer_id!=1 || !packet.session_id ||
            !p4_dice_decode(packet.payload,packet.payload_length,P4_DICE_REQUEST,&r,&phase))
            return BLE_ATT_ERR_UNLIKELY;
        portENTER_CRITICAL(&guard);
        bool valid=(!session || session==packet.session_id) && packet.sequence>last_sequence;
        if (valid) {
            if (!session || desired.token!=r.token) {wire_phase=P4_DICE_WAITING;wire_token=0;}
            else {
                uint8_t previous[48];
                p4_dice_encode(&desired,P4_DICE_WAITING,P4_DICE_REQUEST,previous);
                valid=!memcmp(previous,packet.payload,sizeof(previous));
            }
        }
        if (valid) {session=packet.session_id;last_sequence=packet.sequence;desired=r;last_request_ms=now_ms();}
        portEXIT_CRITICAL(&guard);
        if(valid) printf("P4_DICE REQUEST player=%u token=%lu roll=%u hold=%u held=%u\n",
            r.player_slot+1,(unsigned long)r.token,r.enabled?1:0,r.can_hold?1:0,r.held_mask);
        return valid ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    if (ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR) {
        portENTER_CRITICAL(&guard);
        p4_dice_request_t r=desired; p4_dice_phase_t phase=wire_token==desired.token ? wire_phase : P4_DICE_WAITING;
        if(wire_token==r.token && r.can_hold) {r.held_mask=wire_held;r.hold_ack=wire_hold_sequence;}
        uint32_t id=session, seq=++tx_sequence;
        /* Reads are the bidirectional heartbeat after the initial request. */
        last_request_ms=now_ms();
        portEXIT_CRITICAL(&guard);
        if (!p4_dice_encode(&r,phase,P4_DICE_STATUS,payload) ||
            p4_mp_packet_encode(P4_MP_PACKET_ACCESSORY,id,2,seq,0,payload,48,bytes,sizeof(bytes),&length)!=P4_MP_OK)
            return BLE_ATT_ERR_UNLIKELY;
        return os_mbuf_append(ctxt->om,bytes,(uint16_t)length) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
static const ble_gatt_chr_def characteristics[]={
    {.uuid=&value_uuid.u,.access_cb=access,.arg=nullptr,.descriptors=nullptr,
     .flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC,
     .min_key_size=16,.val_handle=nullptr,.cpfd=nullptr},{}
};
static const ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service_uuid.u,.includes=nullptr,.characteristics=characteristics},{}
};
static int gap(struct ble_gap_event *e,void *arg)
{
    (void)arg;
    if(e->type==BLE_GAP_EVENT_REPEAT_PAIRING) return BLE_GAP_REPEAT_PAIRING_IGNORE;
    if(e->type==BLE_GAP_EVENT_CONNECT) printf("P4_DICE LINK connected=%u rc=%d\n",e->connect.status==0,e->connect.status);
    if(e->type==BLE_GAP_EVENT_DISCONNECT) printf("P4_DICE LINK connected=0 reason=%d\n",e->disconnect.reason);
    if(e->type==BLE_GAP_EVENT_ENC_CHANGE) printf("P4_DICE SECURITY rc=%d\n",e->enc_change.status);
    portENTER_CRITICAL(&guard);
    if (e->type==BLE_GAP_EVENT_CONNECT && !e->connect.status) {
        connection=e->connect.conn_handle;linked=true;session=last_sequence=tx_sequence=wire_token=0;desired={};
    } else if (e->type==BLE_GAP_EVENT_DISCONNECT) {
        connection=BLE_HS_CONN_HANDLE_NONE;linked=false;session=0;desired={};wire_phase=P4_DICE_WAITING;
    }
    portEXIT_CRITICAL(&guard);
    return 0;
}
static void sync_radio()
{
    if (!ble_hs_util_ensure_addr(0) && !ble_hs_id_infer_auto(0,&own_address_type)) radio_ready=true;
}
static void reset_radio(int reason)
{
    (void)reason;portENTER_CRITICAL(&guard);linked=false;desired={};connection=BLE_HS_CONN_HANDLE_NONE;portEXIT_CRITICAL(&guard);
}
static void host_task(void *arg) {(void)arg;nimble_port_run();nimble_port_freertos_deinit();}
static bool advertise()
{
    if (!radio_ready || ble_gap_adv_active()) return false;
    ble_hs_adv_fields fields{};
    fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128=const_cast<ble_uuid128_t*>(&service_uuid);fields.num_uuids128=1;fields.uuids128_is_complete=1;
    if (ble_gap_adv_set_fields(&fields)) return false;
    ble_gap_adv_params params{};params.conn_mode=BLE_GAP_CONN_MODE_UND;params.disc_mode=BLE_GAP_DISC_MODE_GEN;
    return !ble_gap_adv_start(own_address_type,nullptr,60000,&params,gap,nullptr);
}
static void quiet() { dice_feedback_stop(); }
extern "C" void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init()); // Never erase existing NVS on failure.
    auto config=M5.config();config.internal_spk=true;config.internal_mic=false;config.internal_imu=true;
    config.clear_display=true;config.output_power=false;
    M5.begin(config);M5.Power.setVibration(0);
    if(M5.getBoard()!=m5::board_t::board_M5StackCore2) {
        M5.Display.println("Core2 required. Hardware stopped.");return;
    }
    M5.Display.setRotation(1);M5.Display.setBrightness(120);
    // Preserve vendor pins/codec configuration, but shorten the queued audio
    // from 8x256 to 4x128 frames (~43 ms to ~11 ms at 48 kHz).
    M5.Speaker.end();auto speaker_config=M5.Speaker.config();
    speaker_config.task_pinned_core=0;
    speaker_config.dma_buf_len=128;speaker_config.dma_buf_count=4;
    M5.Speaker.config(speaker_config);M5.Speaker.begin();dice_feedback_begin();
    if (!dice_view_begin()) { M5.Display.println("Dice framebuffer unavailable"); return; }
    printf("P4_DICE DISPLAY buffered=320x240x16 psram_bytes=%u\n", (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    bool imu_ok=M5.Imu.isEnabled();
    printf("P4_DICE BOOT board=core2 imu=%s firmware=0.6.0\n",imu_ok ? "ready" : "missing");
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb=sync_radio;ble_hs_cfg.reset_cb=reset_radio;
    ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set("P4 Dice Core2");
    ESP_ERROR_CHECK(ble_gatts_count_cfg(services));ESP_ERROR_CHECK(ble_gatts_add_svcs(services));
    nimble_port_freertos_init(host_task);
    p4_dice_request_t current{}, practice{};
    p4_dice_gesture_t gesture{};
    DiceControls controls;bool ready_requested=false;
    bool demo=false, was_linked=false;uint32_t drawn=0;
    printf("P4_DICE MOTION core=%d renderer_core=1 audio_core=0\n",xPortGetCoreID());
    float prior_x=0,prior_y=0,prior_z=1;
    p4_dice_phase_t last_phase=P4_DICE_OFFLINE;
    // Bounded local diagnostic: render only, no rolls, sound or motor output.
    int console_flags=fcntl(STDIN_FILENO,F_GETFL,0);
    bool console_ready=console_flags>=0 && fcntl(STDIN_FILENO,F_SETFL,console_flags|O_NONBLOCK)==0;
    char command[16]{};unsigned command_length=0;bool command_overflow=false;
    uint32_t benchmark_until=0,last_bench_hit=0;
    for(;;) {
        uint32_t now=now_ms();M5.update();
        portENTER_CRITICAL(&guard);p4_dice_request_t incoming=desired;bool connected=linked;
        uint32_t last=last_request_ms;uint16_t conn=connection;portEXIT_CRITICAL(&guard);
        if (connected && incoming.token && now-last>2000) {
            quiet();ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);incoming={};
        }
        if (connected && !was_linked) {demo=false;quiet();current={};}
        if (!connected && was_linked) {quiet();current={};p4_dice_gesture_reset(&gesture,0);}
        was_linked=connected;
        for(unsigned budget=0;console_ready && budget<16;++budget){
            char c;if(read(STDIN_FILENO,&c,1)!=1)break;
            if(c=='\n'){
                if(!command_overflow && !std::strcmp(command,"dicebench") && !connected){
                    quiet();p4_dice_gesture_reset(&gesture,current.token);benchmark_until=now+2200;last_bench_hit=0;drawn=0;
                    printf("P4_DICE BENCH begin render_only=1\n");
                }
                if(!command_overflow && !std::strcmp(command,"dicestatus"))
                    printf("P4_DICE STATE linked=%u token=%lu phase=%u roll=%u hold=%u held=%u pending=%u imu=%u\n",
                        connected?1:0,(unsigned long)current.token,(unsigned)gesture.phase,current.enabled?1:0,
                        current.can_hold?1:0,current.held_mask,controls.pending?1:0,imu_ok?1:0);
                command_length=0;command[0]=0;command_overflow=false;
            }else if(c!='\r'){
                if(command_length<sizeof(command)-1){command[command_length++]=c;command[command_length]=0;}
                else command_overflow=true;
            }
        }
        bool benchmarking=benchmark_until && (int32_t)(benchmark_until-now)>0;
        if(benchmark_until&&!benchmarking){benchmark_until=0;drawn=0;printf("P4_DICE BENCH end\n");}
        incoming=demo ? practice : incoming;
        controls.update(incoming);
        auto touch=M5.Touch.getDetail();
        if(!benchmarking && touch.wasPressed() && touch.y<200) {
            int die=dice_touch_index(incoming.count,touch.x,touch.y);
            if(die>=0 && controls.toggle((unsigned)die,gesture.phase)) {
                if(demo) {practice.held_mask=controls.selected;practice.hold_ack=controls.sequence;practice.enabled=practice.held_mask!=31;++practice.token;}
                printf("P4_DICE HOLD player=%u token=%lu mask=%u\n",
                    incoming.player_slot+1,(unsigned long)incoming.token,controls.selected);
                drawn=0;
            }
        }
        if(!benchmarking && touch.wasPressed() && touch.y>=200) {
            if(touch.x>=217 && !connected) {
                demo=!demo;quiet();
                if(demo) {
                    if(ble_gap_adv_active()) { ble_gap_adv_stop(); }
                    link_requested=false;
                    practice={};practice.token=1;practice.count=5;practice.sides=6;practice.enabled=true;practice.can_hold=true;
                    std::memcpy(practice.player_name,"PRACTICE",9);
                    for(unsigned i=0;i<5;++i)practice.faces[i]=1;
                } else current={};
                drawn=0;
            } else if (touch.x<207 && !connected && !demo) {
                link_requested=true;advertise_until=now+60000;drawn=0;
            } else if(touch.x<207 && current.enabled && imu_ok) {
                ready_requested=true;
            } else if(touch.x<207 && connected) {
                printf("P4_DICE READY_BLOCKED token=%lu phase=%u roll=%u hold=%u held=%u imu=%u\n",
                    (unsigned long)current.token,(unsigned)gesture.phase,current.enabled?1:0,current.can_hold?1:0,current.held_mask,imu_ok?1:0);
            }
        }
        if(link_requested && !connected && !demo && radio_ready) {
            if((int32_t)(advertise_until-now)>0) advertise();
            else {link_requested=false;if(ble_gap_adv_active())ble_gap_adv_stop();}
        }
        if(demo) incoming=practice;
        controls.update(incoming);
        incoming=controls.request();
        if (memcmp(&incoming,&current,sizeof(current))) {
            if (!demo && gesture.phase!=P4_DICE_ROLLED) { quiet(); }
            current=incoming;p4_dice_gesture_reset(&gesture,current.token);drawn=0;
        }
        if(ready_requested && !controls.pending) {
            if(current.enabled && p4_dice_gesture_ready(&gesture,now))
                printf("P4_DICE READY player=%u token=%lu\n",current.player_slot+1,(unsigned long)current.token);
            ready_requested=false;
        }
        if(!current.enabled && !current.can_hold) ready_requested=false;
        float motion=0, shake_x=0, shake_y=0;
        bool landed_this_tick=false;
        if(!benchmarking && current.enabled && imu_ok && M5.Imu.update()) {
            auto data=M5.Imu.getImuData();
            if (!std::isfinite(data.accel.x) || !std::isfinite(data.accel.y) ||
                !std::isfinite(data.accel.z)) {quiet();vTaskDelay(pdMS_TO_TICKS(10));continue;}
            float delta_x=data.accel.x-prior_x,delta_y=data.accel.y-prior_y,delta_z=data.accel.z-prior_z;
            shake_x=-delta_y;shake_y=delta_x+delta_z*.4f;
            prior_x=data.accel.x;prior_y=data.accel.y;prior_z=data.accel.z;
            float magnitude=std::sqrt(prior_x*prior_x+prior_y*prior_y+prior_z*prior_z);
            motion=std::max(std::fabs(magnitude-1),.65f*std::sqrt(delta_x*delta_x+delta_y*delta_y+delta_z*delta_z));
            bool rolled=p4_dice_gesture_sample(&gesture,now,(int32_t)(data.accel.x*1000),
                (int32_t)(data.accel.y*1000),(int32_t)(data.accel.z*1000));
            if(rolled) {
                landed_this_tick=true;
                dice_feedback_settle(now);
                printf("P4_DICE ROLL player=%u token=%lu mode=%s\n",current.player_slot+1,(unsigned long)current.token,demo?"practice":"p4mp");
                if(demo) {
                    for(unsigned i=0;i<practice.count;++i) {if(practice.held_mask&(1U<<i))continue;uint32_t value;do {value=esp_random();}while(value>=UINT32_MAX-UINT32_MAX%6);practice.faces[i]=(uint8_t)(1+value%6);}
                    ++practice.token;
                }
            }
        }
        bool impact=dice_feedback_update(now,motion,gesture.phase,current.enabled);
        dice_view_motion(shake_x,shake_y,motion,impact||landed_this_tick);
        portENTER_CRITICAL(&guard);
        if(!demo && desired.token==current.token) {wire_phase=gesture.phase;wire_held=current.held_mask;wire_hold_sequence=controls.sequence;wire_token=current.token;}
        portEXIT_CRITICAL(&guard);
        if(!drawn || gesture.phase!=last_phase || now-drawn>=20) {
            if(benchmarking){
                p4_dice_request_t check{};check.token=UINT32_MAX;check.count=5;check.sides=6;
                std::memcpy(check.player_name,"RENDER CHECK",13);for(unsigned i=0;i<5;++i)check.faces[i]=i+1;
                bool hit=now-last_bench_hit>=230;if(hit)last_bench_hit=now;
                dice_view_motion(std::sin(now*.015f)*.7f,std::cos(now*.012f)*.7f,1.4f,hit);
                dice_view_draw(check,P4_DICE_SHAKING,false,true,false,imu_ok,now);
            }else dice_view_draw(current,gesture.phase,connected,demo,link_requested,imu_ok,now,controls.pending);
            drawn=now;last_phase=gesture.phase;
        }
        vTaskDelay(pdMS_TO_TICKS(benchmarking?2:10));
    }
}
