// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static bool motion(void *p,p4_game_motion_t *out){*out=*(p4_game_motion_t*)p;return true;}
static int response(unsigned frame_ms){
 tm_state s={0};tm_reset(&s,0);
 p4_game_motion_t sample={.valid=true,.sequence=1,.accel_mg={0,0,1000}};
 p4_game_services_t services={.available_capabilities=P4_GAME_CAP_MOTION,.motion_context=&sample,.read_motion=motion};
 p4_game_context_t ctx={.services=&services};p4_game_input_t in={0};
 tm_controls(&ctx,&s,&in,16,false);assert(s.calibrated&&s.intent[0].x==0);
 sample.accel_mg[0]=400;sample.accel_mg[2]=916;++sample.sequence;
 tm_controls(&ctx,&s,&in,0,false);assert(s.filtered_x==0&&s.intent[0].x==0);
 unsigned elapsed=0,first=0;
 while(elapsed<90){unsigned ms=frame_ms;if(ms>90-elapsed)ms=90-elapsed;
  tm_controls(&ctx,&s,&in,ms,false);elapsed+=ms;if(!first&&s.intent[0].x>0)first=elapsed;
 }
 assert(first<=frame_ms+2U);assert(s.intent[0].x>=740&&s.intent[0].x<=810);
 const int at90=s.intent[0].x;
 sample.accel_mg[0]=-400;++sample.sequence;elapsed=0;
 while(elapsed<30){unsigned ms=frame_ms;if(ms>30-elapsed)ms=30-elapsed;tm_controls(&ctx,&s,&in,ms,false);elapsed+=ms;}
 assert(s.intent[0].x<0); /* Reversal must stop pushing the old direction in30ms. */
 tm_controls(&ctx,&s,&in,frame_ms,true);assert(abs(s.intent[0].x)<35);
 sample.age_ms=151;tm_controls(&ctx,&s,&in,frame_ms,false);assert(!s.motion_live&&s.intent[0].x==0);
 return at90;
}
int main(void){
 int minimum=1000,maximum=0;
 const unsigned steps[]={1,8,16,17,20,33,50,90};
 for(unsigned i=0;i<sizeof(steps)/sizeof(steps[0]);++i){int value=response(steps[i]);if(value<minimum)minimum=value;if(value>maximum)maximum=value;}
 assert(maximum-minimum<=36); /* Integer milligravity rounding, same90ms pose. */
 /* Stationary hand/sensor noise below the existing deadzone cannot drift. */
 tm_state s={0};tm_reset(&s,0);p4_game_motion_t sample={.valid=true,.sequence=1,.accel_mg={0,0,1000}};
 p4_game_services_t services={.available_capabilities=P4_GAME_CAP_MOTION,.motion_context=&sample,.read_motion=motion};
 p4_game_context_t ctx={.services=&services};p4_game_input_t in={0};tm_controls(&ctx,&s,&in,16,false);
 for(int n=0;n<1000;++n){sample.accel_mg[0]=(n%3-1)*12;sample.accel_mg[1]=(n%5-2)*6;++sample.sequence;tm_controls(&ctx,&s,&in,16,false);assert(s.intent[0].x==0&&s.intent[0].y==0);}
 puts("Tilt latency: immediate response, >=92% at90ms, reversal<30ms, cadence/zero-time/noise/calibration/stale checks PASS");
 return 0;
}
