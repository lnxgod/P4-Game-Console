// SPDX-License-Identifier: MIT
#include "gamepad/xusb.h"
#include "gamepad/snapshot.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Synthetic protocol fixture, not a capture from the owner's controller. */
static const uint8_t config[] = {
    9,2,32,0,1,1,0,0x80,50,
    9,4,0,0,2,0xFF,0x5D,1,0,
    7,5,0x81,3,32,0,4,
    7,5,0x01,3,32,0,8,
};
static void selection(void)
{
    gamepad_xusb_interface_t selected;
    assert(gamepad_xusb_find_interface(config,sizeof(config),&selected)==GAMEPAD_OK);
    assert(selected.endpoint_in==0x81 && selected.packet_bytes==32);
    for(size_t n=0;n<sizeof(config);++n) {
        memset(&selected,0xA5,sizeof(selected));
        assert(gamepad_xusb_find_interface(config,n,&selected)!=GAMEPAD_OK);
        assert(selected.endpoint_in==0 && selected.packet_bytes==0);
    }
    const size_t bad_offsets[]={0,1,2,4,9,13,18,20,21,22,24,25,27};
    const uint8_t bad_values[]={0,1,31,0,1,3,1,0x80,2,19,0,1,0x81};
    for(size_t i=0;i<sizeof(bad_offsets)/sizeof(bad_offsets[0]);++i) {
        uint8_t broken[sizeof(config)]; memcpy(broken,config,sizeof(config));
        broken[bad_offsets[i]]=bad_values[i];
        assert(gamepad_xusb_find_interface(broken,sizeof(broken),&selected)!=GAMEPAD_OK);
        assert(selected.endpoint_in==0);
    }
    uint8_t other[sizeof(config)]; memcpy(other,config,sizeof(config));
    other[16]=0x81; /* wireless receiver */
    assert(gamepad_xusb_find_interface(other,sizeof(other),&selected)==GAMEPAD_ERR_NO_GAMEPAD);
    other[15]=0x47; other[16]=0xD0; /* GIP */
    assert(gamepad_xusb_find_interface(other,sizeof(other),&selected)==GAMEPAD_ERR_NO_GAMEPAD);
    other[14]=3; other[15]=0; other[16]=0; /* HID */
    assert(gamepad_xusb_find_interface(other,sizeof(other),&selected)==GAMEPAD_ERR_NO_GAMEPAD);
    uint8_t duplicate[sizeof(config)+23U];
    memcpy(duplicate,config,sizeof(config));
    memcpy(duplicate+sizeof(config),config+9U,23U);
    duplicate[2]=(uint8_t)sizeof(duplicate); duplicate[4]=2;
    duplicate[sizeof(config)+2U]=1;
    assert(gamepad_xusb_find_interface(duplicate,sizeof(duplicate),&selected)==GAMEPAD_ERR_UNSUPPORTED);
}
static gamepad_state_t connected(void)
{
    gamepad_state_t state; gamepad_state_init(&state);
    assert(gamepad_state_connect(&state,1U)==GAMEPAD_OK);
    return state;
}
static void controls(void)
{
    gamepad_state_t state=connected();
    uint8_t packet[20]={0,20};
    bool input=false;
    assert(gamepad_xusb_decode(packet,sizeof(packet),2,&state,&input)==GAMEPAD_OK);
    assert(input && gamepad_state_is_neutral(&state));
    static const uint8_t canonical[]={
        GAMEPAD_BUTTON_START,GAMEPAD_BUTTON_BACK,GAMEPAD_BUTTON_LEFT_STICK,
        GAMEPAD_BUTTON_RIGHT_STICK,GAMEPAD_BUTTON_LEFT_SHOULDER,
        GAMEPAD_BUTTON_RIGHT_SHOULDER,GAMEPAD_BUTTON_GUIDE,255,
        GAMEPAD_BUTTON_SOUTH,GAMEPAD_BUTTON_EAST,GAMEPAD_BUTTON_WEST,GAMEPAD_BUTTON_NORTH
    };
    for(unsigned i=0;i<12U;++i) {
        memset(packet+2,0,18);
        const unsigned wire=i+4U;
        packet[2U+wire/8U]=(uint8_t)(1U<<(wire%8U));
        assert(gamepad_xusb_decode(packet,20,3,&state,&input)==GAMEPAD_OK);
        const uint64_t expected=canonical[i]==255U?0U:GAMEPAD_BUTTON_MASK(canonical[i]);
        assert(state.buttons==expected);
    }
    memset(packet+2,0,18); packet[2]=0x09; packet[3]=0xF7;
    packet[4]=255; packet[5]=127;
    assert(gamepad_xusb_decode(packet,20,4,&state,&input)==GAMEPAD_OK);
    assert(state.dpad==(GAMEPAD_DPAD_UP|GAMEPAD_DPAD_RIGHT));
    assert(state.left_trigger==UINT16_MAX && state.right_trigger==32639);
    packet[2]=0x0F;
    assert(gamepad_xusb_decode(packet,20,5,&state,&input)==GAMEPAD_OK);
    assert(state.dpad==0); /* contradictory axes neutralize */
    for(unsigned raw=0;raw<=UINT16_MAX;++raw) {
        for(unsigned axis=0;axis<4U;++axis) {
            packet[6U+axis*2U]=(uint8_t)raw;
            packet[7U+axis*2U]=(uint8_t)(raw>>8U);
        }
        assert(gamepad_xusb_decode(packet,20,6,&state,&input)==GAMEPAD_OK);
        const int32_t value=raw>=32768U?(int32_t)raw-65536:(int32_t)raw;
        const int32_t y=value==INT16_MIN?INT16_MAX:-value;
        assert(state.left_x==value && state.right_x==value);
        assert(state.left_y==y && state.right_y==y);
    }
}
static void framing_and_lifecycle(void)
{
    platform_gamepad_model_t model; platform_gamepad_model_init(&model);
    const platform_gamepad_identity_t identity={
        .transport=PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB,
        .descriptor_sha256={1},
    };
    uint32_t session=0;
    assert(platform_gamepad_model_connect(&model,&identity,GAMEPAD_XUSB_CAPABILITIES,1,&session)==GAMEPAD_OK);
    platform_gamepad_snapshot_t snapshot;
    assert(platform_gamepad_model_copy(&model,&snapshot)==GAMEPAD_OK);
    uint8_t packet[65]={0,20,0x10,0x10};
    bool input;
    assert(gamepad_xusb_decode(packet,20,2,&snapshot.state,&input)==GAMEPAD_OK && input);
    assert(platform_gamepad_model_commit_report(&model,session,&snapshot.state)==GAMEPAD_OK);
    const gamepad_state_t prior=snapshot.state;
    for(size_t n=0;n<=sizeof(packet);++n) {
        if(n==20U) continue;
        assert(gamepad_xusb_decode(packet,n,3,&snapshot.state,&input)!=GAMEPAD_OK);
        assert(!input && memcmp(&prior,&snapshot.state,sizeof(prior))==0);
    }
    packet[0]=1; packet[1]=3;
    assert(gamepad_xusb_decode(packet,3,3,&snapshot.state,&input)==GAMEPAD_OK && !input);
    assert(memcmp(&prior,&snapshot.state,sizeof(prior))==0);
    assert(platform_gamepad_model_disconnect(&model,session,4)==GAMEPAD_OK);
    assert(platform_gamepad_model_copy(&model,&snapshot)==GAMEPAD_OK);
    assert(gamepad_state_is_neutral(&snapshot.state) && !snapshot.state.connected);
    const uint32_t old=session;
    assert(platform_gamepad_model_connect(&model,&identity,GAMEPAD_XUSB_CAPABILITIES,5,&session)==GAMEPAD_OK);
    assert(session!=old);
    assert(platform_gamepad_model_commit_report(&model,old,&prior)==GAMEPAD_ERR_DISCONNECTED);
}
int main(void)
{
    selection(); controls(); framing_and_lifecycle();
    puts("xusb core: descriptor bounds, canonical controls and lifecycle passed");
    return 0;
}
