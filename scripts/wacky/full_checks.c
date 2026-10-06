/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "p4/game.h"
#include "front.h"
#include "sound.h"
#include "ww_race.h"
#include "ww_victory.h"
#include "ww_intro.h"
#include "port_support.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const p4_game_descriptor_t p4_wacky_probe_game;
extern WwFront *ww_p4_probe_front(void *);
extern WwSoundPlayer *ww_p4_probe_sound(void *);
extern const WwArchive *ww_p4_probe_archive(void *);
extern WwRace *ww_p4_probe_race(void *);
extern WwIntro *ww_p4_probe_intro(void *);
extern WwVictory *ww_p4_probe_victory(void *);
extern bool ww_p4_probe_launch(void *,unsigned,unsigned,unsigned);
static uint8_t saved[36];static uint32_t save_sequence;static unsigned save_commits;
static bool save(void *opaque,const char *slot,uint32_t schema,uint32_t expected,const uint8_t *b,size_t n,p4_game_save_ticket_t *ticket) {
 (void)opaque;assert(!strcmp(slot,"AUTO")&&schema==2&&n==36&&expected==save_sequence);
 memcpy(saved,b,n);*ticket=++save_sequence;++save_commits;return true;
}
static bool save_status(void *opaque,p4_game_save_ticket_t ticket,p4_game_save_status_t *status,uint32_t *sequence) {
 (void)opaque;assert(ticket==save_sequence);*sequence=save_sequence;*status=P4_GAME_SAVE_COMMITTED;return true;
}
static uint64_t energy;static unsigned frames,stops;static bool reject;
static bool audio(void *c,const int16_t *pcm,size_t n){(void)c;assert(n<=256);if(reject)return false;frames+=(unsigned)n;for(size_t i=0;i<n*2;++i)energy+=(uint64_t)((int32_t)pcm[i]*pcm[i]);return true;}
static void stop_audio(void *c){(void)c;++stops;}
static void key(p4_game_context_t *c,uint32_t k) {
 p4_game_input_t in={.pressed=k,.held=k};assert(p4_wacky_probe_game.update(c,&in,16)==P4_GAME_CONTINUE);
 in=(p4_game_input_t){0};assert(p4_wacky_probe_game.update(c,&in,16)==P4_GAME_CONTINUE);
}
int main(void) {
 FILE *f=fopen(WW_DATA_PATH,"rb");assert(f);uint8_t *data=malloc(4398948);assert(data&&fread(data,1,4398948,f)==4398948);fclose(f);
 p4_game_services_t svc={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_STORAGE|P4_GAME_CAP_AUDIO_STREAM|P4_GAME_CAP_SAVE,
 .resource_data=data,.resource_bytes=4398948,.resource_format_version=1,.submit_pcm16_stereo=audio,.stop_audio=stop_audio,.save_context=saved,.queue_save=save,.read_save_status=save_status};
 p4_game_context_t ctx={.state=calloc(1,p4_wacky_probe_game.state_bytes),.services=&svc};assert(ctx.state);
 uint16_t *pixels=calloc(64000,2);assert(pixels);p4_game_surface_t surface={pixels,320,320,200};
 assert(p4_wacky_probe_game.start(&ctx));WwFront *front=ww_p4_probe_front(ctx.state);
 // Let all three intro stages and the original falling-car animation finish.
 for(unsigned i=0;i<500&&ww_p4_probe_intro(ctx.state)->open;++i) {
  p4_game_input_t in={0};assert(p4_wacky_probe_game.update(&ctx,&in,88)==P4_GAME_CONTINUE);
  assert(p4_wacky_probe_game.render(&ctx,&surface));
 }
 assert(!ww_p4_probe_intro(ctx.state)->open);assert(front->screen==WW_HOME);assert(p4_wacky_probe_game.render(&ctx,&surface));
 FILE *shot=fopen("wacky-menu.rgb565","wb");assert(shot);assert(fwrite(pixels,2,64000,shot)==64000);fclose(shot);
 // Navigate the actual public game callbacks through character/settings/start.
 key(&ctx,P4_BUTTON_DOWN);key(&ctx,P4_BUTTON_A);assert(front->screen==WW_SETUP&&front->mode==WW_SINGLE);
 key(&ctx,P4_BUTTON_RIGHT);assert(front->vehicle==1);
 for(unsigned i=0;i<5;++i)key(&ctx,P4_BUTTON_DOWN);
 key(&ctx,P4_BUTTON_A);assert(ww_p4_probe_race(ctx.state)->open&&ww_p4_probe_race(ctx.state)->selected_vehicle==1);
 for(unsigned track=0;track<5;++track)for(unsigned car=0;car<8;++car) {
  assert(ww_p4_probe_launch(ctx.state,WW_SINGLE,track,car));
  WwRace *race=ww_p4_probe_race(ctx.state);assert(race->selection.track_number==track+1&&race->selected_vehicle==car);
  for(unsigned i=0;i<45;++i) {
   p4_game_input_t in={.held=P4_BUTTON_A|P4_BUTTON_B|(i%20<8?P4_BUTTON_RIGHT:0)};
   assert(p4_wacky_probe_game.update(&ctx,&in,88)==P4_GAME_CONTINUE);
   assert(p4_wacky_probe_game.render(&ctx,&surface));
  }
 }
 assert(ww_p4_probe_launch(ctx.state,WW_DUCK,0,0));assert(ww_p4_probe_race(ctx.state)->duck.active);
 for(unsigned i=0;i<100;++i){p4_game_input_t in={.held=P4_BUTTON_A|P4_BUTTON_B};assert(p4_wacky_probe_game.update(&ctx,&in,88)==P4_GAME_CONTINUE);assert(p4_wacky_probe_game.render(&ctx,&surface));}
 key(&ctx,P4_BUTTON_START);assert(front->screen==WW_PAUSE);key(&ctx,P4_BUTTON_DOWN);key(&ctx,P4_BUTTON_DOWN);key(&ctx,P4_BUTTON_A);assert(front->screen==WW_HOME);
 // Drive each championship result into the next course, including final podium.
 front->race_class=1;assert(ww_p4_probe_launch(ctx.state,WW_CUP,0,0));
 for(unsigned course=0;course<5;++course) {
  WwRace *r=ww_p4_probe_race(ctx.state);assert(r->open&&r->selection.track_number==course+1);
  r->start_released=true;r->finish.phase=WW_FINISH_POINTS_READY;r->finish.points_ready_136_tick=0;r->elapsed_136_ticks=1000;
  for(unsigned i=0;i<8;++i){r->racer_lap[i].finished=true;r->racer_lap[i].finish_place=(uint16_t)(i+1);}
  key(&ctx,0);p4_game_input_t in={0};assert(p4_wacky_probe_game.update(&ctx,&in,100)==P4_GAME_CONTINUE);
  assert(ww_p4_probe_victory(ctx.state)->open);assert(p4_wacky_probe_game.render(&ctx,&surface));
  key(&ctx,0);key(&ctx,P4_BUTTON_A);
  if(course==4){for(unsigned i=0;i<5;++i)assert(p4_wacky_probe_game.update(&ctx,&in,100)==P4_GAME_CONTINUE);assert(p4_wacky_probe_game.render(&ctx,&surface));key(&ctx,P4_BUTTON_A);}
 }
 assert(front->screen==WW_HOME);
 // Time trial is six laps without CPUs, saves the best time; kid mode is three.
 front->race_class=4;assert(ww_p4_probe_launch(ctx.state,WW_SINGLE,2,3));
 WwRace *timed=ww_p4_probe_race(ctx.state);assert(timed->lap_count==6);
 for(unsigned i=1;i<8;++i)assert(!timed->racers[i].active);
 timed->start_released=true;timed->finish.phase=WW_FINISH_POINTS_READY;timed->finish.points_ready_136_tick=0;timed->elapsed_136_ticks=1000;timed->finish.final_time_tenths=1234;
 {p4_game_input_t in={0};assert(p4_wacky_probe_game.update(&ctx,&in,100)==P4_GAME_CONTINUE);}
 assert(front->screen==WW_RESULTS&&front->best_time[2]==1234);assert(p4_wacky_probe_game.render(&ctx,&surface));key(&ctx,P4_BUTTON_A);
 front->race_class=5;assert(ww_p4_probe_launch(ctx.state,WW_SINGLE,1,4));assert(ww_p4_probe_race(ctx.state)->lap_count==3);
 key(&ctx,P4_BUTTON_START);key(&ctx,P4_BUTTON_DOWN);key(&ctx,P4_BUTTON_DOWN);key(&ctx,P4_BUTTON_A);
 // All original info slides, then back to normal menus.
 for(unsigned group=1;group<=2;++group)for(unsigned slide=0;slide<(group==1?8:11);++slide) {
  front->screen=WW_SLIDES;front->slide_group=(uint8_t)group;front->slide=(uint8_t)slide;
  key(&ctx,0);assert(p4_wacky_probe_game.render(&ctx,&surface));
 }
 key(&ctx,P4_BUTTON_B);assert(front->screen==WW_INFO);assert(p4_wacky_probe_game.render(&ctx,&surface));
 front->screen=WW_OPTIONS;front->row=0;key(&ctx,P4_BUTTON_RIGHT);key(&ctx,P4_BUTTON_B);key(&ctx,0);
 assert(save_commits&&saved[9]==front->music);
 assert(energy&&frames&&ww_p4_probe_sound(ctx.state)->music.stats.parse_failures==0);
 const WwArchive *a=ww_p4_probe_archive(ctx.state);unsigned songs=0,effects=0;
 for(unsigned i=0;i<a->entry_count;++i) {
  const char *name=a->entries[i].name;size_t n=strlen(name);
  if(n>=4&&!strcmp(name+n-4,".VOC")){WwSample v;assert(ww_sound_sample(a,name,&v));++effects;}
  if(n>=4&&!strcmp(name+n-4,".MID")) {
   WwArchiveView v;assert(ww_archive_view(a,name,&v));p4_midi_player_t m;assert(p4_midi_start(&m,v.data,v.size,true));
   int16_t pcm[512];uint64_t e=0;
   for(unsigned j=0;j<125;++j){memset(pcm,0,sizeof(pcm));assert(p4_midi_mix(&m,pcm,256));for(unsigned k=0;k<512;++k)e+=(uint64_t)((int32_t)pcm[k]*pcm[k]);}
   assert(e&&m.stats.notes_started);
   // Skip sample gaps to validate every remaining SMF event through a loop.
   for(unsigned j=0;j<100000&&!m.stats.loops_completed;++j){m.samples_until_event=0;int16_t frame[2]={0};assert(p4_midi_mix(&m,frame,1));}
   assert(m.stats.loops_completed&&m.stats.parse_failures==0);++songs;
   printf("MIDI %s: audible; %u events; loop passed\n",name,m.stats.events_processed);
  }
 }
 assert(songs==16&&effects==36);
 reject=true;key(&ctx,0);assert(ww_p4_probe_sound(ctx.state)->rejected);reject=false;
 p4_wacky_probe_game.stop(&ctx);assert(stops&&ww_p4_heap_live()==0);
 svc.save_data=saved;svc.save_bytes=36;svc.save_schema_version=2;svc.save_sequence=save_sequence;
 assert(p4_wacky_probe_game.start(&ctx));assert(ww_p4_probe_front(ctx.state)->best_time[2]==1234);assert(ww_p4_probe_front(ctx.state)->music==saved[9]);p4_wacky_probe_game.stop(&ctx);assert(ww_p4_heap_live()==0);
 printf("PASS: all 5 races x 8 drivers, championship/podium, time trial/kid mode, intro, info slides, saves, Duck Shoot, menus, pause/home/relaunch, 36 VOC effects, 16 MIDI songs, audio rejection; state=%zu heap peak=%zu\n",p4_wacky_probe_game.state_bytes,ww_p4_heap_peak());
 free(pixels);free(ctx.state);free(data);
}
