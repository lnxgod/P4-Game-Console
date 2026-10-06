/* SPDX-License-Identifier: MIT */
#include "p4/game.h"
#include "p4/input.h"
#include "controls.h"
#include "front.h"
#include "ww_race.h"
#include "port_support.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
extern const p4_game_descriptor_t p4_wacky_probe_game;
extern bool ww_p4_probe_launch(void *,unsigned,unsigned,unsigned);
extern uint32_t ww_p4_probe_logic(const void *);
extern WwRace *ww_p4_probe_race(void *);
static p4_physical_touch_t physical(unsigned x,unsigned y)
{
 return (p4_physical_touch_t){
  (uint16_t)(P4_INPUT_VIEWPORT_LEFT+(x*P4_INPUT_VIEWPORT_WIDTH+160)/320),
  (uint16_t)(P4_INPUT_VIEWPORT_TOP+(y*P4_INPUT_VIEWPORT_HEIGHT+100)/200)};
}
int main(void)
{
 FILE *file=fopen(WW_DATA_PATH,"rb");assert(file);uint8_t *data=malloc(4398948);
 assert(data&&fread(data,1,4398948,file)==4398948);fclose(file);
 p4_game_services_t service={.available_capabilities=19,.resource_data=data,.resource_bytes=4398948,.resource_format_version=1};
 p4_game_context_t c[2];
 for(unsigned i=0;i<2;++i){c[i]=(p4_game_context_t){.state=calloc(1,p4_wacky_probe_game.state_bytes),.services=&service};assert(c[i].state&&p4_wacky_probe_game.start(&c[i]));assert(ww_p4_probe_launch(c[i].state,WW_SINGLE,0,0));}
 p4_game_input_mapper_t mapper={0};
 uint16_t initial=ww_p4_probe_race(c[0].state)->player.camera_y;
 for(unsigned frame=0;frame<240;++frame) {
  p4_physical_touch_t touches[3];size_t count=0;uint32_t buttons=0;
  if(frame<180){touches[count++]=physical(286,158);buttons|=P4_BUTTON_A;}
  if(frame>=60&&frame<90){touches[count++]=physical(20,170);buttons|=P4_BUTTON_LEFT;}
  if(frame>=90&&frame<120){touches[count++]=physical(70,170);buttons|=P4_BUTTON_RIGHT;}
  if(frame>=120&&frame<150){touches[count++]=physical(240,176);buttons|=P4_BUTTON_B;}
  if(frame>=180&&frame<210){touches[count++]=physical(46,186);buttons|=P4_BUTTON_DOWN;}
  p4_game_input_t touch,digital={.held=buttons};
  p4_game_input_mapper_update(&mapper,true,touches,count,0,&touch);assert(touch.held==buttons);
  assert(p4_wacky_probe_game.update(&c[0],&touch,88)==P4_GAME_CONTINUE);
  assert(p4_wacky_probe_game.update(&c[1],&digital,88)==P4_GAME_CONTINUE);
  assert(ww_p4_probe_logic(c[0].state)==ww_p4_probe_logic(c[1].state));
 }
 assert(ww_p4_probe_race(c[0].state)->player.camera_y!=initial);
 p4_physical_touch_t pause=physical(290,10);p4_game_input_t input;
 p4_game_input_mapper_update(&mapper,true,&pause,1,0,&input);
 assert(input.pressed==P4_BUTTON_START);assert(p4_wacky_probe_game.update(&c[0],&input,16)==P4_GAME_CONTINUE);
 /* Menu footer remains touch-usable on the full help/results/slide screens. */
 for(unsigned screen=WW_HELP;screen<=WW_SLIDES;++screen) {
  if(screen!=WW_HELP&&screen!=WW_RESULTS&&screen!=WW_SLIDES)continue;
  WwFront front={.screen=(uint8_t)screen};input=(p4_game_input_t){.touch_valid=true,.touch_count=1,.touches={{160,190}}};
  (void)ww_front_input(&front,&input);assert(front.screen==(screen==WW_SLIDES?WW_INFO:WW_HOME));
 }
 uint16_t *pixels=calloc(64000,2);assert(pixels);p4_game_surface_t surface={pixels,320,320,200};
 ww_controls_draw(&surface,0,false);assert(pixels[158*320+8]==0xffff);
 ww_controls_draw(&surface,P4_BUTTON_LEFT,false);assert(pixels[158*320+8]==0xffe0);
 p4_physical_touch_t exit=physical(20,10);p4_game_input_mapper_update(&mapper,true,&exit,1,0,&input);
 assert(p4_wacky_probe_game.update(&c[0],&input,16)==P4_GAME_EXIT_TO_LAUNCHER);
 for(unsigned i=0;i<2;++i){p4_wacky_probe_game.stop(&c[i]);free(c[i].state);}
 assert(ww_p4_heap_live()==0);free(pixels);free(data);
 puts("PASS: actual touch mapping drives gas/left/right/fire/brake/release identically to buttons, multi-touch, visible held feedback, pause, menu return and exit");
}
