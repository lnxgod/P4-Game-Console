/* SPDX-License-Identifier: MIT */
/* Focused paired-instance audio-service proof using the actual game callbacks.
 * Fixture positioning makes a real physics update collect/finish deterministically;
 * it is not a gameplay, device sound, or maze-navigation acceptance test. */
#include "tide_maze_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct endpoint endpoint;
struct endpoint {
 endpoint *peer;
 p4_game_multiplayer_status_t status;
 p4_game_multiplayer_message_t queue[32], last_sent;
 unsigned head,tail,pearl_tones,clear_tones,stops;
 uint32_t sequence;
};
static bool status(void *p,p4_game_multiplayer_status_t *s){*s=((endpoint*)p)->status;return true;}
static bool send(void *p,const uint8_t *b,size_t n){
 endpoint *e=p;if(e->peer->tail-e->peer->head>=32)return false;
 p4_game_multiplayer_message_t m={.sequence=++e->sequence,.player_slot=e->status.local_player_slot,.bytes=(uint8_t)n};
 memcpy(m.data,b,n);e->last_sent=m;e->peer->queue[e->peer->tail++%32]=m;return true;
}
static bool receive(void *p,p4_game_multiplayer_message_t *m){endpoint *e=p;if(e->head==e->tail)return false;*m=e->queue[e->head++%32];return true;}
static bool tone(void *p,const p4_tone_t *t){
 endpoint *e=p;assert(t->volume_step==3&&t->waveform==P4_WAVE_TRIANGLE);
 if(t->frequency_hz==880){assert(t->duration_ms==75);++e->pearl_tones;}
 else{assert(t->frequency_hz==1320&&t->duration_ms==180);++e->clear_tones;}return true;
}
static void stop(void *p){++((endpoint*)p)->stops;}
static const p4_game_multiplayer_profile_t profile={.schema=1,.style=P4_GAME_MULTIPLAYER_STYLE_REALTIME,.min_players=2,.max_players=2,.tick_rate_hz=20,.message_bytes=64,.protocol=1};
static void begin(p4_game_instance_t *g,tm_state *s,endpoint *e){
 p4_game_services_t svc={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_AUDIO_TONE|P4_GAME_CAP_MULTIPLAYER_SESSION,
 .game_id=p4_tide_maze_game.id,.audio_context=e,.play_tone=tone,.stop_audio=stop,
 .multiplayer_context=e,.multiplayer_read_status=status,.multiplayer_send=send,.multiplayer_receive=receive,.multiplayer_profile=&profile};
 assert(p4_game_instance_start(g,&p4_tide_maze_game,&svc,s,sizeof(*s)));
}
static void tick(p4_game_instance_t *g,uint32_t ms,uint32_t press){p4_game_input_t in={.pressed=press};assert(p4_game_instance_update(g,&in,ms)==P4_GAME_CONTINUE);}
static void deliver(tm_state *s,p4_game_instance_t *host,p4_game_instance_t *client){s->net_ms=50;tick(host,0,0);tick(client,0,0);}
int main(void){
 endpoint a={.status={.generation=7,.session_seed=42,.state=P4_GAME_MULTIPLAYER_CONNECTED,.role=P4_GAME_MULTIPLAYER_ROLE_HOST,.local_player_slot=0,.player_count=2}};
 endpoint b={.status={.generation=7,.session_seed=42,.state=P4_GAME_MULTIPLAYER_CONNECTED,.role=P4_GAME_MULTIPLAYER_ROLE_CLIENT,.local_player_slot=1,.player_count=2}};
 a.peer=&b;b.peer=&a;tm_state h={0},c={0};p4_game_instance_t gh={0},gc={0};begin(&gh,&h,&a);begin(&gc,&c,&b);
 deliver(&h,&gh,&gc);assert(c.snapshot_seen&&c.phase==TM_PLAY);assert(!a.pearl_tones&&!b.pearl_tones&&!b.clear_tones);
 /* Fresh malformed snapshot must reach semantic validation, not just stale-revision rejection. */
 const uint32_t first_revision=c.received_revision;
 p4_game_multiplayer_message_t bad=a.last_sent;bad.sequence=++a.sequence;
 ++bad.data[4];bad.data[12]=1;bad.data[63]=1;
 b.queue[b.tail++%32]=bad;tick(&gc,0,0);
 assert(c.received_revision==first_revision&&c.pearls==0&&!b.pearl_tones&&!b.clear_tones);
 /* Separately, a new transport sequence carrying a stale revision is silent. */
 bad=a.last_sent;bad.sequence=++a.sequence;bad.data[12]=1;
 b.queue[b.tail++%32]=bad;tick(&gc,0,0);
 assert(c.received_revision==first_revision&&c.pearls==0&&!b.pearl_tones&&!b.clear_tones);
 unsigned pearls=0;
 for(int y=0;y<TM_ROWS;++y)for(int x=0;x<TM_COLS;++x)if(tm_tile(0,x,y)=='o'){
  h.ball[0]=(tm_ball){.x=(x*16+8)*TM_Q,.y=(y*16+8)*TM_Q};tick(&gh,20,0);deliver(&h,&gh,&gc);++pearls;
  assert(a.pearl_tones==pearls);assert(c.pearls==h.pearls);assert(b.pearl_tones==pearls);
 }
 printf("Pearl snapshots: host cues=%u guest cues=%u state_equal=%d\n",a.pearl_tones,b.pearl_tones,c.pearls==h.pearls);
 h.ball[0]=(tm_ball){.x=200*TM_Q,.y=120*TM_Q};h.ball[1]=h.ball[0];tick(&gh,20,0);deliver(&h,&gh,&gc);
 assert(h.phase==TM_CLEAR&&c.phase==TM_CLEAR);assert(a.clear_tones==1&&b.clear_tones==1U);
 printf("Clear snapshot: host cues=%u guest cues=%u state_equal=%d\n",a.clear_tones,b.clear_tones,c.phase==h.phase);
 unsigned before=b.pearl_tones+b.clear_tones;
 /* Duplicate transport sequence cannot sound. */
 b.queue[b.tail++%32]=a.last_sent;tick(&gc,0,0);
 assert(b.pearl_tones+b.clear_tones==before);
 /* A valid repeated state (new revision) is also silent. */
 deliver(&h,&gh,&gc);assert(b.pearl_tones+b.clear_tones==before);
 /* Host advances the level; a reset is not a newly collected pearl/clear. */
 tick(&gh,0,P4_BUTTON_A);deliver(&h,&gh,&gc);assert(h.level==1&&c.level==1&&c.phase==TM_PLAY);assert(b.pearl_tones+b.clear_tones==before);
 /* Same-level retry with a higher remaining timer also stays silent. */
 h.time_ms=100;deliver(&h,&gh,&gc);tm_reset(&h,1);deliver(&h,&gh,&gc);assert(c.time_ms==h.time_ms&&b.pearl_tones+b.clear_tones==before);
 /* A new client's first snapshot can be progressed, but should not play history. */
 h.pearls=1;deliver(&h,&gh,&gc);before=b.pearl_tones+b.clear_tones;
 p4_game_instance_stop(&gc);begin(&gc,&c,&b);deliver(&h,&gh,&gc);assert(c.pearls==1&&b.pearl_tones+b.clear_tones==before);
 /* A guest that last saw PAUSE must hear a same-update resume-and-clear,
  * including the final maze WON transition. */
 const unsigned levels[2]={0,TM_LEVELS-1U};
 for(unsigned i=0;i<2;++i){
  tm_reset(&h,levels[i]);deliver(&h,&gh,&gc);
  h.time_ms-=20;deliver(&h,&gh,&gc);
  h.pearls=(1U<<h.all_pearls)-1U;h.ball[0]=(tm_ball){.x=200*TM_Q,.y=120*TM_Q};h.ball[1]=h.ball[0];h.phase=TM_PAUSE;
  deliver(&h,&gh,&gc);assert(c.phase==TM_PAUSE);
  const unsigned old_host_clear=a.clear_tones,old_guest_clear=b.clear_tones;
  tick(&gh,20,P4_BUTTON_START);deliver(&h,&gh,&gc);
  assert(h.phase==(i?TM_WON:TM_CLEAR)&&c.phase==h.phase);
  assert(a.clear_tones==old_host_clear+1&&b.clear_tones==old_guest_clear+1U);
 }
 printf("Pause/resume completion: host cues=%u guest cues=%u (includes final WON)\n",a.clear_tones,b.clear_tones);
 p4_game_instance_stop(&gh);p4_game_instance_stop(&gc);assert(a.stops==2&&b.stops==4); /* Descriptor and API both stop audio. */
 puts("Tide Maze guest audio: cue parity and no replay/reset/first-snapshot cues PASS");
 return 0;
}
