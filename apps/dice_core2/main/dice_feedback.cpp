// SPDX-License-Identifier: MIT
#include "dice_feedback.h"
#include <M5Unified.h>
#include <algorithm>
#include "esp_random.h"
#include "dice_audio_clips.h"
extern const uint8_t audio_data[] asm("_binary_dice_audio_pcm_start");
namespace {
uint32_t last_motion=0,next_pulse=0,pulse_end=0,settle_start=0,settle_second=0;
unsigned voice=0,previous_clip=99;
uint8_t motor=0,pulse_level=255;
bool settling=false;
void vibration(uint8_t value){if(motor!=value){M5.Power.setVibration(value);motor=value;}}
void play(unsigned index,uint8_t volume=240){
    const auto &clip=dice_audio_clips[index];unsigned channel=voice++%4;
    M5.Speaker.setChannelVolume(channel,volume);
    M5.Speaker.playRaw(reinterpret_cast<const int16_t*>(audio_data)+clip.offset,
        clip.samples,22050,false,1,(int)channel,true);
}
}
void dice_feedback_begin(){M5.Speaker.setVolume(255);M5.Speaker.setAllChannelVolume(240);dice_feedback_stop();}
void dice_feedback_stop(){vibration(0);M5.Speaker.stop();last_motion=next_pulse=pulse_end=0;settling=false;}
void dice_feedback_settle(uint32_t now){
    unsigned index=6+esp_random()%3;
    settle_start=now;settle_second=dice_audio_clips[index].second_pulse_ms;settling=true;
    vibration(255);M5.Speaker.stop();play(index);
}
bool dice_feedback_update(uint32_t now,float motion,p4_dice_phase_t phase,bool enabled){
    if(settling){
        uint32_t elapsed=now-settle_start;
        bool second=settle_second && elapsed>=settle_second && elapsed<settle_second+55;
        if(elapsed<70)vibration(255);
        else vibration(second?240:0);
        if(elapsed>=(settle_second?settle_second+55:100))settling=false;
        return false;
    }
    if(!enabled || (phase!=P4_DICE_READY && phase!=P4_DICE_SHAKING)){
        vibration(0);next_pulse=pulse_end=last_motion=0;return false;
    }
    if(motion>.22f)last_motion=now;
    bool moving=last_motion && now-last_motion<160;
    if(!moving){vibration(0);pulse_end=0;return false;}
    bool hit=false;
    if((int32_t)(now-next_pulse)>=0){
        unsigned clip=esp_random()%6;if(clip==previous_clip)clip=(clip+1)%6;previous_clip=clip;
        unsigned on=motion>.8f?85:70;
        pulse_end=now+on;next_pulse=now+on+145+esp_random()%16;
        pulse_level=(uint8_t)(235+std::min(20.0f,std::max(0.0f,motion)*20));
        // A single impact event drives motor, sound and visible dice impulse.
        // PCM's main contact starts ~35 ms later, during ERM spin-up.
        vibration(pulse_level);play(clip,(uint8_t)(225+std::min(25.0f,std::max(0.0f,motion)*20)));
        hit=true;
    }
    vibration((int32_t)(pulse_end-now)>0?pulse_level:0);
    return hit;
}
