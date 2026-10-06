// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct endpoint endpoint;
struct endpoint { endpoint *peer; p4_game_multiplayer_status_t status;
 p4_game_multiplayer_message_t queue[32]; unsigned head,tail;uint32_t sequence;bool reject;};
static bool status(void *p,p4_game_multiplayer_status_t *out){*out=((endpoint*)p)->status;return true;}
static bool send(void *p,const uint8_t *b,size_t n){endpoint *e=p;if(e->reject||e->peer->tail-e->peer->head>=32)return false;
 p4_game_multiplayer_message_t *m=&e->peer->queue[e->peer->tail++%32];memset(m,0,sizeof(*m));m->sequence=++e->sequence;m->player_slot=e->status.local_player_slot;m->bytes=(uint8_t)n;memcpy(m->data,b,n);return true;}
static bool receive(void *p,p4_game_multiplayer_message_t *m){endpoint *e=p;if(e->head==e->tail)return false;*m=e->queue[e->head++%32];return true;}
static bool motion(void *p,p4_game_motion_t *out){*out=*(p4_game_motion_t*)p;return true;}
static const p4_game_multiplayer_profile_t profile={.schema=1,.style=P4_GAME_MULTIPLAYER_STYLE_REALTIME,.min_players=2,.max_players=2,.tick_rate_hz=20,.message_bytes=64,.protocol=1};
static void begin(p4_game_instance_t *game,tm_state *s,endpoint *e){
 p4_game_services_t svc={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_VIDEO_HIGH_RES,.game_id=p4_tide_maze_game.id};
 if(e){svc.available_capabilities|=P4_GAME_CAP_MULTIPLAYER_SESSION;svc.multiplayer_context=e;svc.multiplayer_read_status=status;svc.multiplayer_send=send;svc.multiplayer_receive=receive;svc.multiplayer_profile=&profile;}
 assert(p4_game_instance_start(game,&p4_tide_maze_game,&svc,s,sizeof(*s)));
}
static void tick(p4_game_instance_t *g,uint32_t held,uint32_t pressed){p4_game_input_t in={.held=held,.pressed=pressed};assert(p4_game_instance_update(g,&in,20)==P4_GAME_CONTINUE);}
static int volume(tm_state *s){int sum=0;for(int i=0;i<TM_CELLS;++i)sum+=s->water[i];return sum;}
static void maps(void){
 for(unsigned l=0;l<TM_LEVELS;++l){
  int queue[135],head=0,tail=0;bool seen[135]={false};queue[tail++]=16;seen[16]=true;
  for(int y=0;y<TM_ROWS;++y)assert(strlen(tm_maps[l][y])==TM_COLS);
  while(head<tail){int i=queue[head++],x=i%15,y=i/15;const int nx[4]={x-1,x+1,x,x},ny[4]={y,y,y-1,y+1};
   for(int n=0;n<4;++n){char t=tm_tile(l,nx[n],ny[n]);if(t=='#'||t=='~')continue;int j=ny[n]*15+nx[n];if(!seen[j]){seen[j]=true;queue[tail++]=j;}}}
  for(int y=0;y<TM_ROWS;++y)for(int x=0;x<TM_COLS;++x){char t=tm_tile(l,x,y);if(t=='o'||t=='E')assert(seen[y*15+x]);}
 }
}
static void rules(void){
 tm_state s={0};p4_game_instance_t g={0};begin(&g,&s,NULL);assert(s.phase==TM_TITLE);
 tick(&g,P4_BUTTON_A,P4_BUTTON_A);assert(s.phase==TM_PLAY);int32_t x=s.ball[0].x;
 for(int i=0;i<60;++i)tick(&g,P4_BUTTON_RIGHT,0);assert(s.ball[0].x>x);assert(s.ball[0].x<=93*TM_Q);
 int v=s.ball[0].vx;tick(&g,P4_BUTTON_A,0);assert(s.ball[0].vx<v);
 tick(&g,0,P4_BUTTON_START);assert(s.phase==TM_PAUSE);uint32_t time=s.time_ms;tick(&g,0,0);assert(s.time_ms==time);
 tick(&g,0,P4_BUTTON_START);assert(s.phase==TM_PLAY);
 for(unsigned l=0;l<TM_LEVELS;++l){tm_reset(&s,l);
  for(int y=0;y<TM_ROWS;++y)for(int col=0;col<TM_COLS;++col)if(tm_tile(l,col,y)=='o'){
   s.ball[0].x=(col*16+8)*TM_Q;s.ball[0].y=(y*16+8)*TM_Q;s.ball[0].vx=s.ball[0].vy=0;tm_simulate(&s);}
  assert(s.pearls==((1U<<s.all_pearls)-1U));s.ball[0].x=200*TM_Q;s.ball[0].y=120*TM_Q;tm_simulate(&s);assert(s.phase==(l==2?TM_WON:TM_CLEAR));
 }
 tm_reset(&s,0);s.time_ms=10;tm_simulate(&s);assert(s.phase==TM_LOST);tick(&g,0,P4_BUTTON_A);assert(s.phase==TM_PLAY);
 s.ball[0].x=40*TM_Q;s.ball[0].y=120*TM_Q;tm_simulate(&s);assert(s.rescues==1&&s.ball[0].rescue>0);
 p4_game_input_t touch={.touch_valid=true,.touch_count=1,.touches={{100,56}}};assert(p4_game_instance_update(&g,&touch,20)==P4_GAME_CONTINUE);assert(s.intent[0].x>0);
 touch.pressed=P4_BUTTON_BACK|P4_BUTTON_START|P4_BUTTON_B;assert(p4_game_instance_update(&g,&touch,20)==P4_GAME_CONTINUE);assert(s.phase==TM_PLAY);
 p4_game_input_t back={.pressed=P4_BUTTON_BACK};assert(p4_game_instance_update(&g,&back,20)==P4_GAME_EXIT_TO_LAUNCHER);
}
/* Complete every maze through ordinary normalized controls, with no state
 * teleportation. BFS chooses waypoints; the real marble dynamics reach them. */
static void navigate(p4_game_instance_t *g,tm_state *s,int goal){
 int prev[135],queue[135],head=0,tail=0,path[135],n=0;
 for(int i=0;i<135;++i)prev[i]=-1;
 int start=s->ball[0].y/(16*TM_Q)*15+s->ball[0].x/(16*TM_Q);
 prev[start]=start;queue[tail++]=start;
 while(head<tail&&prev[goal]<0){int i=queue[head++],x=i%15,y=i/15;int nx[4]={x-1,x+1,x,x},ny[4]={y,y,y-1,y+1};
  for(int k=0;k<4;++k){char t=tm_tile(s->level,nx[k],ny[k]);if(t=='#'||t=='~')continue;int j=ny[k]*15+nx[k];if(prev[j]<0){prev[j]=i;queue[tail++]=j;}}}
 assert(prev[goal]>=0);for(int i=goal;i!=start;i=prev[i])path[n++]=i;
 for(int k=n-1;k>=0;--k){int x=(path[k]%15*16+8)*TM_Q,y=(path[k]/15*16+8)*TM_Q;unsigned frame;
  for(frame=0;frame<250;++frame){int dx=x-s->ball[0].x,dy=y-s->ball[0].y;
   if(dx > -TM_Q && dx < TM_Q && dy > -TM_Q && dy < TM_Q)break;
   uint32_t held=P4_BUTTON_A;
   if(dx>TM_Q)held|=P4_BUTTON_RIGHT;else if(dx < -TM_Q)held|=P4_BUTTON_LEFT;
   if(dy>TM_Q)held|=P4_BUTTON_DOWN;else if(dy < -TM_Q)held|=P4_BUTTON_UP;
   tick(g,held,0);if(s->phase!=TM_PLAY)break;
  }assert(frame<250);if(s->phase!=TM_PLAY)break;
 }
}
static void complete_voyage(void){
 tm_state s={0};p4_game_instance_t g={0};begin(&g,&s,NULL);tick(&g,0,P4_BUTTON_A);
 for(unsigned level=0;level<TM_LEVELS;++level){
  assert(s.level==level);
  for(int y=0;y<TM_ROWS;++y)for(int x=0;x<TM_COLS;++x)if(tm_tile(level,x,y)=='o')navigate(&g,&s,y*15+x);
  assert(s.pearls==((1U<<s.all_pearls)-1U));
  for(int y=0;y<TM_ROWS;++y)for(int x=0;x<TM_COLS;++x)if(tm_tile(level,x,y)=='E')navigate(&g,&s,y*15+x);
  assert(s.phase==(level==2?TM_WON:TM_CLEAR));assert(s.rescues==0);
  if(level<2)tick(&g,0,P4_BUTTON_A);
 }
}
static void water(void){tm_state s={0};tm_reset(&s,0);int vol=volume(&s);s.intent[0]=(tm_intent){800,-650,400,900,false};
 for(int n=0;n<10000;++n){tm_fluid(&s);if(n==300)s.intent[0]=(tm_intent){-700,550,-600,-1000,false};}
 assert(volume(&s)==vol);bool moved=false;for(int i=0;i<TM_CELLS;++i){assert(s.water[i]>=0&&s.water[i]<=1024);if(!s.wet[i])assert(s.water[i]==0);else if(s.water[i]!=320)moved=true;}assert(moved);
}
static void sensors(void){
 tm_state s={0};tm_reset(&s,0);p4_game_motion_t m={.sequence=1,.valid=true,.accel_mg={0,0,1000}},out;
 p4_game_services_t svc={.available_capabilities=P4_GAME_CAP_MOTION,.motion_context=&m,.read_motion=motion};p4_game_context_t c={.services=&svc};p4_game_input_t in={0};
 assert(p4_game_read_motion(&c,&out));tm_controls(&c,&s,&in,20,false);assert(s.calibrated&&s.intent[0].x==0);
 m.accel_mg[0]=400;m.accel_mg[2]=916;for(int i=0;i<30;++i){++m.sequence;tm_controls(&c,&s,&in,20,false);}assert(s.intent[0].x>600);
 tm_controls(&c,&s,&in,20,true);assert(s.intent[0].x<40);
 m.gyro_mdps[0]=90000;m.gyro_mdps[1]=90000;m.gyro_mdps[2]=90000;++m.sequence;tm_controls(&c,&s,&in,20,false);assert(s.intent[0].spin!=0&&s.filtered_x!=0&&s.filtered_y!=0);
 m.accel_mg[2]=1600;++m.sequence;tm_controls(&c,&s,&in,20,false);assert(s.intent[0].jolt>500);
 m.age_ms=151;assert(!p4_game_read_motion(&c,&out)&&!out.valid&&out.accel_mg[0]==0);tm_controls(&c,&s,&in,20,false);assert(!s.motion_live&&s.intent[0].x==0);
 m.age_ms=0;m.accel_mg[0]=4001;assert(!p4_game_read_motion(&c,&out));m.accel_mg[0]=0;m.gyro_mdps[0]=2000001;assert(!p4_game_read_motion(&c,&out));
 svc.available_capabilities=0;assert(!p4_game_read_motion(&c,&out));assert(!p4_game_read_motion(NULL,&out));assert(!p4_game_read_motion(&c,NULL));
}
static void network(void){
 endpoint a={.status={.generation=7,.session_seed=42,.state=P4_GAME_MULTIPLAYER_CONNECTED,.role=P4_GAME_MULTIPLAYER_ROLE_HOST,.local_player_slot=0,.player_count=2}};
 endpoint b={.status={.generation=7,.session_seed=42,.state=P4_GAME_MULTIPLAYER_CONNECTED,.role=P4_GAME_MULTIPLAYER_ROLE_CLIENT,.local_player_slot=1,.player_count=2}};
 a.peer=&b;b.peer=&a;tm_state host={0},client={0};p4_game_instance_t gh={0},gc={0};begin(&gh,&host,&a);begin(&gc,&client,&b);
 assert(host.linked&&client.phase==TM_WAIT);
 for(int n=0;n<60;++n){tick(&gc,P4_BUTTON_RIGHT,0);tick(&gh,0,0);}assert(host.ball[1].x>27*TM_Q);assert(client.snapshot_seen&&client.phase==host.phase);
 a.reject=true;uint32_t rev=host.revision;for(int n=0;n<5;++n){tick(&gc,0,0);tick(&gh,0,0);}assert(host.revision==rev);a.reject=false;
 /* Receive a final complete snapshot without another host simulation tick. */
 host.net_ms=50;tm_network_publish(&gh.context,&host);assert(tm_network_poll(&gc.context,&client,0));assert(client.ball[1].x==host.ball[1].x&&client.time_ms==host.time_ms);
 b.reject=true;for(int n=0;n<20;++n)tick(&gh,0,0);assert(host.intent[1].x==0);b.reject=false;
 /* Malformed, stale, wrong-player packets must not move the remote marble. */
 p4_game_multiplayer_message_t bad={.sequence=900,.player_slot=1,.bytes=TM_INPUT,.data={1,1}};
 bad.data[4]=(uint8_t)host.revision;bad.data[8]=0xff;bad.data[9]=0x7f;a.queue[a.tail++%32]=bad;uint32_t seq=host.received_seq;
 assert(tm_network_poll(&gh.context,&host,0));assert(host.received_seq==seq&&host.intent[1].x==0);
 assert(tm_network_poll(&gc.context,&client,0));
 bad.bytes=64;bad.player_slot=0;bad.data[1]=2;bad.data[2]=TM_PLAY;bad.data[3]=255;b.queue[b.tail++%32]=bad;
 uint32_t got=client.received_revision;assert(tm_network_poll(&gc.context,&client,0));assert(client.received_revision==got);
 /* Semantic fuzzing is bounded and cannot manufacture a valid future state. */
 for(unsigned n=0;n<=64;++n){
  p4_game_multiplayer_message_t malformed={.sequence=1000U+n,.player_slot=1,.bytes=(uint8_t)n};
  memset(malformed.data,0xff,sizeof(malformed.data));malformed.data[0]=1;malformed.data[1]=1;
  a.queue[a.tail++%32]=malformed;uint32_t before=host.received_seq;
  assert(tm_network_poll(&gh.context,&host,0));assert(host.received_seq==before);
 }
 /* Replayed input and a packet attributed to our own slot are ignored. */
 bad=(p4_game_multiplayer_message_t){.sequence=host.received_seq,.player_slot=1,.bytes=TM_INPUT,.data={1,1}};
 a.queue[a.tail++%32]=bad;assert(tm_network_poll(&gh.context,&host,0));assert(host.intent[1].x==0);
 bad.sequence=1200;bad.player_slot=0;a.queue[a.tail++%32]=bad;assert(tm_network_poll(&gh.context,&host,0));assert(host.intent[1].x==0);
 host.pearls=(1U<<host.all_pearls)-1U;host.ball[0]=(tm_ball){.x=200*TM_Q,.y=120*TM_Q};host.ball[1]=(tm_ball){.x=200*TM_Q,.y=120*TM_Q};
 tm_simulate(&host);assert(host.phase==TM_CLEAR);host.net_ms=50;tm_network_publish(&gh.context,&host);assert(tm_network_poll(&gc.context,&client,0));assert(client.phase==TM_CLEAR);
 ++a.status.generation;tick(&gh,0,0);assert(!host.linked&&host.phase==TM_LINK_LOST&&host.intent[1].x==0);tick(&gh,0,P4_BUTTON_A);assert(host.phase==TM_PLAY&&!host.linked&&host.slot==0);
 b.status.state=P4_GAME_MULTIPLAYER_PEER_LEFT;tick(&gc,0,0);assert(client.phase==TM_LINK_LOST);tick(&gc,0,P4_BUTTON_A);assert(!client.linked&&client.slot==0);
}
static void frames(const char *directory){
 for(unsigned w=320;w<=768;w+=448){unsigned h=w==320?200:480,stride=w+7;size_t words=(size_t)stride*h+32;
  uint16_t *data=malloc(words*sizeof(*data));assert(data);tm_state s={0};p4_game_instance_t g={0};begin(&g,&s,NULL);
  if(w==320)g.services.available_capabilities&=~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
  p4_game_surface_t f={.pixels=data+16,.stride_pixels=stride,.width=(uint16_t)w,.height=(uint16_t)h};
  const tm_phase phases[]={TM_TITLE,TM_PLAY,TM_PAUSE,TM_CLEAR,TM_WON,TM_LOST,TM_LINK_LOST,TM_WAIT};
  for(unsigned p=0;p<8;++p){for(size_t i=0;i<words;++i)data[i]=0xa55a;
   tm_reset(&s,p%TM_LEVELS);s.intent[0]=(tm_intent){1000,-850,900,1000,false};
   for(unsigned tick=0;tick<100;++tick)tm_fluid(&s);
   s.animation_ms=1777U+p*137U;s.phase=phases[p];assert(p4_game_instance_render(&g,&f));
   for(unsigned i=0;i<16;++i){assert(data[i]==0xa55a);assert(data[words-1-i]==0xa55a);}
   for(unsigned y=0;y<h;++y)for(unsigned x=w;x<stride;++x)assert(f.pixels[(size_t)y*stride+x]==0xa55a);
   if(directory){char path[512];snprintf(path,sizeof(path),"%s/tide-%u-%u.ppm",directory,w,p);FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P6\n%u %u\n255\n",w,h);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){uint16_t c=f.pixels[(size_t)y*stride+x];unsigned char b[3]={(unsigned char)((c>>11)*255/31),(unsigned char)(((c>>5)&63)*255/63),(unsigned char)((c&31)*255/31)};assert(fwrite(b,1,3,file)==3);}fclose(file);}
  }free(data);
 }
}
int main(int argc,char **argv){maps();rules();complete_voyage();water();sensors();network();frames(argc>1?argv[1]:NULL);printf("Tide Maze: rules, connectivity, conservative water, six-axis input, two-instance protocol and guarded frames PASS; state=%zu\n",sizeof(tm_state));return 0;}
