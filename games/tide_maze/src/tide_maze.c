// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include <string.h>
static bool start(p4_game_context_t *ctx){
 tm_state *s=ctx->state;memset(s,0,sizeof(*s));tm_reset(s,0);s->phase=TM_TITLE;
 (void)tm_network_begin(ctx,s);return true;
}
static p4_game_result_t update(p4_game_context_t *ctx,const p4_game_input_t *in,uint32_t ms){
 tm_state *s=ctx->state;
 if(ms>100)ms=100;s->animation_ms+=ms;s->blend_ms=(uint32_t)tm_clamp((int)s->blend_ms+(int)ms,0,50);
 const bool touch=in->touch_valid&&in->touch_count>0;
 if(!touch&&(in->pressed&P4_BUTTON_BACK))return P4_GAME_EXIT_TO_LAUNCHER;
 p4_game_input_t clean=*in;
 if(touch||s->touching){clean.held=0;clean.pressed=0;clean.released=0;}
 const bool tap=touch&&!s->touching;s->touching=touch;in=&clean;
 const int tx=touch?in->touches[0].x:0,ty=touch?in->touches[0].y:0;
 if((in->pressed&P4_BUTTON_BACK)||(tap&&tx>=280&&ty<12))return P4_GAME_EXIT_TO_LAUNCHER;
 const bool action=(in->pressed&P4_BUTTON_A)||(tap&&s->phase!=TM_PLAY&&ty>=32&&ty<178);
 const bool pause=(in->pressed&P4_BUTTON_START)||(tap&&tx>=267&&ty>=178);
 const bool cal=(in->pressed&P4_BUTTON_B)||(tap&&tx>=216&&tx<267&&ty>=178);
 if(s->phase==TM_TITLE && (in->pressed&P4_BUTTON_LEFT)){tm_reset(s,(s->level+TM_LEVELS-1U)%TM_LEVELS);s->phase=TM_TITLE;}
 if(s->phase==TM_TITLE && (in->pressed&P4_BUTTON_RIGHT)){tm_reset(s,(s->level+1U)%TM_LEVELS);s->phase=TM_TITLE;}
 if((s->phase==TM_TITLE||s->phase==TM_PAUSE)&&cal){s->orientation=(uint8_t)((s->orientation+1U)%4U);s->calibrated=false;}
 tm_controls(ctx,s,in,ms,cal);
 if(s->linked&&!tm_network_poll(ctx,s,ms)){
  s->linked=false;s->phase=TM_LINK_LOST;memset(s->intent,0,sizeof(s->intent));
  for(int p=0;p<2;++p)s->ball[p].vx=s->ball[p].vy=0;
 }
 if(s->phase==TM_LINK_LOST){if(action){s->slot=0;tm_reset(s,0);}return P4_GAME_CONTINUE;}
 if(!s->linked||s->host){
  if(pause&&(s->phase==TM_PLAY||s->phase==TM_PAUSE))s->phase=s->phase==TM_PLAY?TM_PAUSE:TM_PLAY;
  else if(action){
   if(s->phase==TM_TITLE||s->phase==TM_LOST)tm_reset(s,s->level);
   else if(s->phase==TM_CLEAR)tm_reset(s,s->level+1U);
   else if(s->phase==TM_WON)tm_reset(s,0);
   else if(s->phase==TM_PAUSE)s->phase=TM_PLAY;
  }
  const unsigned old=s->pearls;const tm_phase phase=s->phase;
  if(s->phase==TM_PLAY){s->accumulator+=ms;for(unsigned n=0;n<5&&s->accumulator>=TM_STEP;++n){tm_simulate(s);s->accumulator-=TM_STEP;}}
  else s->accumulator=0;
  if(old!=s->pearls)(void)p4_game_play_tone(ctx,880,75,3,P4_WAVE_TRIANGLE);
  if(phase==TM_PLAY&&(s->phase==TM_CLEAR||s->phase==TM_WON))(void)p4_game_play_tone(ctx,1320,180,3,P4_WAVE_TRIANGLE);
 }else if(s->phase==TM_PLAY){
  s->accumulator+=ms;for(unsigned n=0;n<5&&s->accumulator>=TM_STEP;++n){tm_fluid(s);s->accumulator-=TM_STEP;}
 }
 if(s->linked)tm_network_publish(ctx,s);
 return P4_GAME_CONTINUE;
}
static void stop(p4_game_context_t *ctx){p4_game_stop_audio(ctx);}
const p4_game_descriptor_t p4_tide_maze_game={
 .api_version=P4_GAME_API_VERSION,.launcher_id=120,.id="org.p4console.tide-maze",
 .title="Tide Maze",.subtitle="Tilt, slosh and escape together",
 .accent_rgb565=0x2e5b,.required_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS,
 .optional_capabilities=P4_GAME_CAP_AUDIO_TONE|P4_GAME_CAP_VIDEO_HIGH_RES|P4_GAME_CAP_MOTION|P4_GAME_CAP_MULTIPLAYER_SESSION,
 .state_bytes=sizeof(tm_state),.start=start,.update=update,.render=tm_render,.stop=stop
};
