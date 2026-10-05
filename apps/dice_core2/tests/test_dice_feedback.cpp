// SPDX-License-Identifier: MIT
#include "dice_feedback.h"
#include "dice_audio_clips.h"
#include <M5Unified.h>
#include <cassert>
#include <cstdio>
extern const uint8_t audio_data[(dice_audio_clips[8].offset+dice_audio_clips[8].samples)*2] asm("_binary_dice_audio_pcm_start")={};
int main(){
 dice_feedback_begin();
 for(unsigned t=1;t<100;t++)dice_feedback_update(t,2,P4_DICE_WAITING,true);
 assert(!M5.Power.level && !M5.Speaker.sounds);
 unsigned on=0,off=0,bursts=0;bool seen=false;
 for(unsigned t=100;t<3100;t++){
  unsigned before=M5.Speaker.sounds;
  bool hit=dice_feedback_update(t,2,P4_DICE_SHAKING,true);
  assert(M5.Speaker.sounds-before==(hit?1U:0U));
  if(M5.Power.level){if(!on){if(seen)assert(off>=145);bursts++;}on++;off=0;seen=true;assert(on<=85);}
  else{on=0;off++;}
 }
 assert(bursts>=10 && M5.Speaker.sounds==bursts);
 for(unsigned t=3100;t<3300;t++)dice_feedback_update(t,0,P4_DICE_SHAKING,true);
 assert(!M5.Power.level);
 dice_feedback_settle(3400);
 unsigned second=0;
 for(unsigned i=6;i<9;++i)if(M5.Speaker.last_data==reinterpret_cast<const int16_t*>(audio_data)+dice_audio_clips[i].offset)second=dice_audio_clips[i].second_pulse_ms;
 for(unsigned t=3400;t<4100;t++){
  dice_feedback_update(t,0,P4_DICE_ROLLED,true);
  auto dt=t-3400;
  assert((M5.Power.level!=0)==(dt<70 || (second&&dt>=second&&dt<second+55)));
 }
 dice_feedback_update(4200,2,P4_DICE_READY,true);dice_feedback_update(4201,2,P4_DICE_READY,false);assert(!M5.Power.level);
 dice_feedback_stop();assert(!M5.Power.level);
 printf("PASS: %u separate bursts, 85 ms maximum on, at least 145 ms off, stop/disarm/landing timing\n",bursts);
}
