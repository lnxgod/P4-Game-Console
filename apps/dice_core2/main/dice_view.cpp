// SPDX-License-Identifier: MIT
#include "dice_view.h"
#include "dice_cup.h"
#include "dice_dirty.h"
#include "dice_presentation.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dice_sprite_bounds.h"
#include <M5Unified.h>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstring>
extern const uint8_t atlas_data[] asm("_binary_dice_atlas_rgb565_start");
namespace {
constexpr unsigned atlas_frames=96, side=64;
constexpr uint32_t background=0x0c1923, table_color=0x17343c;
M5Canvas frame(&M5.Display);
M5Canvas table(&M5.Display);
lgfx::swap565_t *dma_band=nullptr;
struct Pose {unsigned index;int x,y,size;DiceDirtyRect bounds;};
Pose previous_pose[8]{};
DiceDirtyRect damage[16];unsigned damage_count=0;
DiceDirtyRect bounds(unsigned index,int cx,int cy,int size){
    if(size!=64)return {(int16_t)(cx-size/2),(int16_t)(cy-size/2),(int16_t)size,(int16_t)size};
    const auto &b=dice_sprite_bounds[index];
    int x0=std::min(cx-32+b[0],cx-18),x1=std::max(cx-32+b[2],cx+23);
    int y0=cy-32+b[1],y1=std::max(cy-32+b[3],cy+27);
    return {(int16_t)x0,(int16_t)y0,(int16_t)(x1-x0),(int16_t)(y1-y0)};
}
p4_dice_request_t previous{};
p4_dice_phase_t previous_phase=P4_DICE_OFFLINE;
bool initialized=false,previous_connected=false,previous_practice=false,previous_searching=false;
bool previous_landed=false,previous_awaiting=false,previous_hold_pending=false;
struct ViewFrame {
    p4_dice_request_t request{};
    p4_dice_phase_t phase=P4_DICE_OFFLINE;
    bool connected=false,practice=false,searching=false,imu_ok=false;
    bool landed=false,waiting=false,hit=false,hold_pending=false;
    float x=0,y=0,energy=0;
    uint32_t now=0;
};
ViewFrame latest;
portMUX_TYPE view_guard=portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t renderer=nullptr;
DicePresentation presentation;
float input_x=0,input_y=0,input_energy=0;
bool input_hit=false;
void render_frame(const ViewFrame &view);
void render_task(void *) {
    printf("P4_DICE RENDER core=%d latest_frame_mailbox=1\n",xPortGetCoreID());
    for(;;){
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        portENTER_CRITICAL(&view_guard);
        ViewFrame view=latest;latest.hit=false;
        portEXIT_CRITICAL(&view_guard);
        render_frame(view);
    }
}
uint64_t perf_start=0,render_total=0,transfer_total=0;
unsigned perf_frames=0;
DiceCup cup;
float force_x=0,force_y=0,motion_energy=0;
bool pending_impact=false;
uint32_t cup_time=0;
void upload(bool full) {
    const auto *pixels=static_cast<const lgfx::swap565_t*>(table.getBuffer());
    if(full)std::memcpy(static_cast<uint16_t*>(frame.getBuffer())+50*320,pixels,320*126*2);
    M5.Display.startWrite();
    if(full){
        for(int line=0;line<240;line+=16){
            M5.Display.waitDMA();std::memcpy(dma_band,static_cast<const lgfx::swap565_t*>(frame.getBuffer())+line*320,320*16*2);
            M5.Display.pushImageDMA(0,line,320,16,dma_band);
        }
    }else{
        DiceDirtyRect rectangles[DICE_DIRTY_MAX];
        unsigned count=dice_dirty_rects(damage,damage_count,rectangles);
        for(unsigned i=0;i<count;++i){auto r=rectangles[i];M5.Display.waitDMA();
            for(int row=0;row<r.h;++row)std::memcpy(dma_band+row*r.w,pixels+(r.y-50+row)*320+r.x,r.w*2);
            M5.Display.pushImageDMA(r.x,r.y,r.w,r.h,dma_band);
        }
    }
    M5.Display.waitDMA();M5.Display.endWrite();
}
void sprite(unsigned index,int cx,int cy,int size=64) {
    const auto *source=reinterpret_cast<const uint16_t*>(atlas_data)+index*side*side;
    auto *pixels=static_cast<uint16_t*>(table.getBuffer());
    int x=cx-size/2,y=cy-size/2-50;
    for(int row=std::max(0,-y);row<std::min(size,126-y);++row){
        int input_row=(row*64/size)*64;
        for(int col=std::max(0,-x);col<std::min(size,320-x);++col){
            uint16_t pixel=source[input_row+col*64/size];
            if(pixel)pixels[(y+row)*320+x+col]=pixel;
        }
    }
}
void performance(bool animate,uint64_t start,uint64_t rendered,uint64_t finished) {
    if(animate){
        if(!perf_start)perf_start=start;
        ++perf_frames;render_total+=rendered-start;transfer_total+=finished-rendered;
    }
    if(perf_frames && (!animate || finished-perf_start>=1000000)){
        uint64_t elapsed=finished-perf_start;
        printf("P4_DICE PERF frames=%u fps_x10=%u render_us=%u transfer_us=%u\n",perf_frames,
            (unsigned)((uint64_t)(perf_frames-1)*10000000/(elapsed?elapsed:1)),
            (unsigned)(render_total/perf_frames),(unsigned)(transfer_total/perf_frames));
        perf_start=render_total=transfer_total=0;perf_frames=0;
    }
}
}
void dice_view_motion(float x,float y,float energy,bool hit){
    input_x=std::clamp(input_x*.65f+x,-3.0f,3.0f);
    input_y=std::clamp(input_y*.65f+y,-3.0f,3.0f);
    input_energy=std::max(energy,input_energy*.82f);input_hit|=hit;
}
bool dice_view_begin(){
    frame.setColorDepth(16);frame.setPsram(true);
    dma_band=static_cast<lgfx::swap565_t*>(heap_caps_malloc(320*16*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
    void *table_memory=heap_caps_malloc(320*126*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    table.setColorDepth(16);if(table_memory)table.setBuffer(table_memory,320,126,16);
    bool ready=dma_band && table_memory && frame.createSprite(320,240)!=nullptr;
    printf("P4_DICE MEMORY internal_free=%u table_internal=%u\n",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        ready && esp_ptr_internal(table.getBuffer())?1:0);
    return ready && xTaskCreatePinnedToCore(render_task,"dice_render",6144,nullptr,2,&renderer,1)==pdPASS;
}
void dice_view_draw(const p4_dice_request_t &r,p4_dice_phase_t phase,
    bool connected,bool practice,bool searching,bool imu_ok,uint32_t now,bool hold_pending){
    presentation.update(r,phase,practice);
    ViewFrame view;
    view.request=r;view.phase=phase;view.connected=connected;view.practice=practice;
    view.searching=searching;view.imu_ok=imu_ok;view.now=now;
    view.landed=presentation.landed;view.waiting=presentation.waiting;view.hold_pending=hold_pending;
    view.x=input_x;view.y=input_y;view.energy=input_energy;view.hit=input_hit;input_hit=false;
    portENTER_CRITICAL(&view_guard);
    view.hit|=latest.hit;latest=view;
    portEXIT_CRITICAL(&view_guard);
    xTaskNotifyGive(renderer);
}
namespace {
void render_frame(const ViewFrame &view){
    const auto &r=view.request;
    const auto phase=view.phase;
    const bool connected=view.connected,practice=view.practice,searching=view.searching,imu_ok=view.imu_ok;
    const bool landed=view.landed,awaiting_result=view.waiting;
    const uint32_t now=view.now;
    force_x=view.x;force_y=view.y;motion_energy=view.energy;pending_impact|=view.hit;
    uint64_t start=(uint64_t)esp_timer_get_time();
    bool changed=!initialized || std::memcmp(&previous,&r,sizeof(r)) || phase!=previous_phase ||
        connected!=previous_connected || practice!=previous_practice || searching!=previous_searching ||
        landed!=previous_landed || awaiting_result!=previous_awaiting || view.hold_pending!=previous_hold_pending;
    bool animate=phase==P4_DICE_SHAKING;
    if(!changed && !animate)return;
    if(changed){
        frame.fillScreen(background);frame.setTextDatum(top_left);frame.setTextSize(1);frame.setTextColor(0x8eafb8);
        frame.drawString(practice?"PRACTICE DICE":"P4 DICE",14,9);
        frame.fillCircle(304,13,3,connected?0x72e6c3:0x607a87);
        frame.setTextSize(2);frame.setTextColor(0xf7f3e9);
        frame.drawString(r.token?r.player_name:"VIRTUAL DICE",14,27);
        frame.setTextSize(1);frame.setTextColor(landed?0x72e6c3:0xaec5cb);frame.setTextDatum(middle_center);
        const char *message=view.hold_pending?"SAVING HELD DICE...":!imu_ok?"MOTION SENSOR UNAVAILABLE":!r.token?(connected?"LINKING GAME...":searching?"CONNECTING TO P4...":"CONNECT OR TRY A PRACTICE ROLL"):
            animate?"SHAKING... LET GO TO LAND":awaiting_result?"WAITING FOR THE P4 RESULT...":
            phase==P4_DICE_READY?"SHAKE THE DICE":r.can_hold?"TAP DICE TO KEEP / TAP READY TO ROLL":!r.enabled?"START OR FINISH YOUR TURN ON P4":"TAP READY TO ROLL";
        frame.drawString(message,160,184);
        bool ready=r.enabled && phase==P4_DICE_WAITING && !view.hold_pending;
        frame.fillRoundRect(12,200,194,32,8,ready?0x72e6c3:0x294a51);
        frame.setTextSize(2);frame.setTextColor(ready?0x11252c:0xd5e4e0);
        frame.drawString(ready?"READY":view.hold_pending?"SAVING...":!connected&&!practice?"CONNECT":phase==P4_DICE_READY||animate?"SHAKE IT":r.can_hold?"ALL HELD":"USE P4",109,216);
        frame.fillRoundRect(216,200,92,32,8,0x263a47);frame.setTextSize(1);frame.setTextColor(0xd2dedf);
        frame.drawString(practice?"EXIT PRACTICE":connected?"CONNECTED":"TRY DICE",262,216);
    }
    // Only this complete table region is transferred during animation. Header,
    // status and touch buttons remain stable until their state changes.
    table.fillScreen(table_color);
    unsigned count=r.token?r.count:5;
    if(animate){
        if(previous_phase!=P4_DICE_SHAKING || cup.count!=count){cup.reset(count);cup_time=now;}
        if(pending_impact)cup.hit(motion_energy,r.held_mask);
        float dt=cup_time?std::min(.05f,(float)(now-cup_time)/1000):.03f;cup_time=now;
        cup.step(dt,force_x,force_y,r.held_mask);
        table.drawEllipse(160,62,153,59,0x39616a);
        table.drawEllipse(160,62,148,54,0x244a55);
    }
    pending_impact=false;
    damage_count=0;
    unsigned order[8];
    for(unsigned i=0;i<count;++i)order[i]=i;
    if(animate)std::sort(order,order+count,[](unsigned a,unsigned b){return cup.dice[a].y<cup.dice[b].y;});
    for(unsigned at=0;at<count;++at){
        unsigned i=order[at];
        bool held=r.token && (r.held_mask&(1U<<i));
        unsigned value=r.token?r.faces[i]:i+1;
        unsigned index=atlas_frames+(held?6:0)+(r.sides>6?0:value-1);
        int cx=count>5?40+(int)(i%4)*80:160-((int)count-1)*31+(int)i*62;
        int cy=count>5?83+(int)(i/4)*56:99;
        if(animate){cx=(int)cup.dice[i].x;cy=(int)(cup.dice[i].y-cup.dice[i].lift);if(!held)index=(unsigned)cup.dice[i].frame;}
        int size=count>5?48:64;auto box=bounds(index,cx,cy,size);
        const auto &old=previous_pose[i];
        if(old.index!=index||old.x!=cx||old.y!=cy||old.size!=size){
            damage[damage_count++]=box;
            if(old.size)damage[damage_count++]=old.bounds;
        }
        previous_pose[i]={index,cx,cy,size,box};
        table.fillEllipse(cx+2,cy-50+(count>5?17:23),count>5?15:20,3,0x10252d);
        sprite(index,cx,cy,count>5?48:64);
    }
    // Explicit values make the result unambiguous even on this small display.
    for(unsigned i=0;!animate && i<count;++i){
        bool held=r.token && (r.held_mask&(1U<<i));
        int cx=count>5?40+(int)(i%4)*80:160-((int)count-1)*31+(int)i*62;
        int cy=count>5?55+(int)(i/4)*56:97;
        table.setTextDatum(middle_center);table.setTextSize(count>5?1:3);
        table.setTextColor(held?0xf0c462:landed?0x91f1d0:0xf7f3e9);
        if((animate||awaiting_result)&&!held)table.drawString("-",cx,cy);
        else table.drawNumber(r.token?r.faces[i]:i+1,cx,cy);
        if(held&&count<=5){table.setTextSize(1);table.drawString("HELD",cx,116);}
    }
    uint64_t rendered=(uint64_t)esp_timer_get_time();
    upload(changed);
    uint64_t finished=(uint64_t)esp_timer_get_time();performance(animate,start,rendered,finished);
    if(!initialized)printf("P4_DICE FRAME render_us=%u full_transfer_us=%u atlas_frames=108\n",
        (unsigned)(rendered-start),(unsigned)(finished-rendered));
    previous_landed=landed;previous_awaiting=awaiting_result;previous_hold_pending=view.hold_pending;
    previous=r;previous_phase=phase;previous_connected=connected;previous_practice=practice;previous_searching=searching;initialized=true;
}
}
