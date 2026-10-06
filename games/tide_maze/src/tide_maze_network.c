// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include <string.h>
static void w16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static unsigned r16(const uint8_t *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
static int signed16(const uint8_t *p){unsigned v=r16(p);return v>=32768U?(int)v-65536:(int)v;}
static void w32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8U*i));}
static uint32_t r32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void intent_write(uint8_t *p,const tm_intent *in){
 w16(p,(uint16_t)in->x);w16(p+2,(uint16_t)in->y);w16(p+4,(uint16_t)in->spin);w16(p+6,(uint16_t)in->jolt);p[8]=(uint8_t)in->brake;p[9]=0;
}
static bool intent_read(const uint8_t *p,tm_intent *out){
 int x=signed16(p),y=signed16(p+2),spin=signed16(p+4),jolt=signed16(p+6);
 if(x < -1000||x>1000||y < -1000||y>1000||spin < -1000||spin>1000||jolt < -1000||jolt>1000||p[8]>1||p[9])return false;
 *out=(tm_intent){(int16_t)x,(int16_t)y,(int16_t)spin,(int16_t)jolt,p[8]!=0};return true;
}
bool tm_network_begin(p4_game_context_t *ctx,tm_state *s){
 p4_game_multiplayer_profile_t p;p4_game_multiplayer_status_t st;
 if(!p4_game_multiplayer_read_profile(ctx,&p)||p.style!=P4_GAME_MULTIPLAYER_STYLE_REALTIME||p.protocol!=1||p.message_bytes<64||p.tick_rate_hz!=20||p.min_players!=2||p.max_players!=2||
  !p4_game_multiplayer_read_status(ctx,&st)||st.state!=P4_GAME_MULTIPLAYER_CONNECTED||st.player_count!=2||
  !((st.role==P4_GAME_MULTIPLAYER_ROLE_HOST&&st.local_player_slot==0)||(st.role==P4_GAME_MULTIPLAYER_ROLE_CLIENT&&st.local_player_slot==1)))return false;
 s->linked=true;s->host=st.role==P4_GAME_MULTIPLAYER_ROLE_HOST;s->slot=st.local_player_slot;
 s->seed=st.session_seed;s->generation=st.generation;s->net_ms=50;
 tm_reset(s,0);if(!s->host)s->phase=TM_WAIT;return true;
}
static bool snapshot(tm_state *s,const uint8_t *b){
 uint32_t rev=r32(b+4),time=r32(b+8);unsigned level=b[3],pearls=r16(b+12);
 if(rev==0||rev<=s->received_revision||level>=TM_LEVELS||time>150000||b[2]<TM_PLAY||b[2]>TM_LOST||b[15]>3)return false;
 unsigned n=0;for(int y=0;y<TM_ROWS;++y)for(int x=0;x<TM_COLS;++x)if(tm_tile(level,x,y)=='o')++n;
 if(pearls>=(1U<<n))return false;
 tm_ball ball[2];tm_intent intent[2];
 for(unsigned i=0;i<2;++i){
  unsigned k=16+i*8;ball[i]=(tm_ball){(int32_t)r16(b+k),(int32_t)r16(b+k+2),signed16(b+k+4),signed16(b+k+6),(uint16_t)r16(b+32+i*2)};
  if(ball[i].x<19*TM_Q||ball[i].x>221*TM_Q||ball[i].y<19*TM_Q||ball[i].y>125*TM_Q||
   ball[i].vx < -280||ball[i].vx>280||ball[i].vy < -280||ball[i].vy>280||ball[i].rescue>700||!intent_read(b+36+i*10,&intent[i]))return false;
 }
 for(unsigned i=56;i<64;++i)if(b[i])return false;
 /* Time only increases when a maze restarts. This also detects a retry whose
  * result screen was lost in transit, and resets the client's cosmetic water. */
 const bool reset=level!=s->level||(s->snapshot_seen&&time>s->time_ms);
 if(reset){tm_intent local=s->intent[s->slot];tm_reset(s,level);s->intent[s->slot]=local;}
 for(int i=0;i<2;++i){
  /* Continue from the currently drawn point if a snapshot arrives early.
   * Starting from the previous target instead causes a visible position jump. */
  const bool blend=s->snapshot_seen&&!reset;
  s->previous_x[i]=blend?s->previous_x[i]+(s->ball[i].x-s->previous_x[i])*(int32_t)s->blend_ms/50:ball[i].x;
  s->previous_y[i]=blend?s->previous_y[i]+(s->ball[i].y-s->previous_y[i])*(int32_t)s->blend_ms/50:ball[i].y;
  s->ball[i]=ball[i];
 }
 s->intent[0]=intent[0]; /* Local input remains local until the next intent send. */
 s->time_ms=time;s->pearls=pearls;s->rescues=b[14];s->docked=b[15];s->phase=(tm_phase)b[2];
 s->received_revision=rev;s->snapshot_seen=true;s->blend_ms=0;return true;
}
bool tm_network_poll(p4_game_context_t *ctx,tm_state *s,uint32_t ms){
 p4_game_multiplayer_status_t st;
 if(!p4_game_multiplayer_read_status(ctx,&st)||st.state!=P4_GAME_MULTIPLAYER_CONNECTED||st.generation!=s->generation||st.session_seed!=s->seed||
  st.player_count!=2||st.local_player_slot!=s->slot||st.role!=(s->host?P4_GAME_MULTIPLAYER_ROLE_HOST:P4_GAME_MULTIPLAYER_ROLE_CLIENT))return false;
 s->net_ms=(uint32_t)tm_clamp((int)s->net_ms+(int)ms,0,1000);s->peer_ms+=ms;
 p4_game_multiplayer_message_t message;
 for(unsigned count=0;count<8&&p4_game_multiplayer_receive(ctx,&message);++count){
  const uint8_t *b=message.data;
  if(message.player_slot!=(s->host?1:0)||message.sequence<=s->received_seq||message.bytes<2||b[0]!=1)continue;
  bool accepted=false;
  if(s->host&&message.bytes==TM_INPUT&&b[1]==1&&b[2]==0&&b[3]==0){
   uint32_t revision=r32(b+4);
   if(revision<=s->revision&&s->revision-revision<=60U&&b[18]==0&&b[19]==0)
    accepted=intent_read(b+8,&s->intent[1]);
  }else if(!s->host&&message.bytes==TM_SNAPSHOT&&b[1]==2)accepted=snapshot(s,b);
  if(accepted){s->received_seq=message.sequence;s->peer_ms=0;}
 }
 if(s->peer_ms>250&&s->host)s->intent[1]=(tm_intent){0};
 if(s->peer_ms>3000)return false;
 return true;
}
void tm_network_publish(p4_game_context_t *ctx,tm_state *s){
 if(s->net_ms<50)return;
 uint8_t b[TM_SNAPSHOT]={1,2};size_t bytes=TM_SNAPSHOT;
 if(s->host){
  b[2]=(uint8_t)s->phase;b[3]=(uint8_t)s->level;w32(b+4,s->revision+1U);w32(b+8,s->time_ms);
  w16(b+12,s->pearls);b[14]=(uint8_t)(s->rescues>255?255:s->rescues);b[15]=(uint8_t)s->docked;
  for(unsigned i=0;i<2;++i){unsigned k=16+i*8;const tm_ball *a=&s->ball[i];
   w16(b+k,(unsigned)a->x);w16(b+k+2,(unsigned)a->y);w16(b+k+4,(uint16_t)a->vx);w16(b+k+6,(uint16_t)a->vy);
   w16(b+32+i*2,a->rescue);intent_write(b+36+i*10,&s->intent[i]);}
 }else{bytes=TM_INPUT;b[1]=1;w32(b+4,s->received_revision);intent_write(b+8,&s->intent[1]);}
 /* Keep the fractional period, without bursting a backlog after rejection. */
 if(p4_game_multiplayer_send(ctx,b,bytes)){s->net_ms%=50;if(s->host)++s->revision;}
}
