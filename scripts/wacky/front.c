/* SPDX-License-Identifier: MIT
 * Shareware front end. Uses original art with normalized controls. */
#include "front.h"
#include "p4/draw.h"
#include "ww_race.h"
#include "ww_sprite.h"
#include <string.h>
#include <stdio.h>
static const char *const names[]={"UNO","SULTAN","MORRIS","PEGGLES","RAZER","RINGO","BLOMBO","TIGI"};
static const unsigned lap_values[]={3,6,8,10};
bool ww_front_open(WwFront *f,const WwArchive *a) {
 if(f->background.pixels)return true;
 f->loaded_group=0;
 WwArchiveView v;
 return ww_archive_view(a,"CHECK.PCX",&v)&&ww_pcx_decode(v.data,v.size,&f->background);
}
void ww_front_close(WwFront *f){ww_pcx_free(&f->background);}
static unsigned rows(const WwFront *f) {
 return f->screen==WW_HOME?7:f->screen==WW_SETUP?7:f->screen==WW_OPTIONS?7:f->screen==WW_INFO?4:f->screen==WW_PAUSE?3:1;
}
unsigned ww_front_input(WwFront *f,const p4_game_input_t *in) {
 uint32_t p=in->pressed;bool tap=in->touch_valid&&in->touch_count&&!f->touch_down;
 f->touch_down=in->touch_valid&&in->touch_count;
 if(f->touch_down)p=0;
 if(tap&&in->touches[0].y>=182&&f->screen!=WW_HOME) {
  p=(f->screen==WW_HELP||f->screen==WW_RESULTS)?P4_BUTTON_A:P4_BUTTON_B;
  tap=false;
 }
 if(f->screen==WW_SLIDES) {
  if(tap||p&(P4_BUTTON_A|P4_BUTTON_RIGHT|P4_BUTTON_DOWN))f->slide=(uint8_t)((f->slide+1)%(f->slide_group==1?8:11));
  if(p&(P4_BUTTON_LEFT|P4_BUTTON_UP))f->slide=(uint8_t)((f->slide+(f->slide_group==1?7:10))%(f->slide_group==1?8:11));
  if(p&P4_BUTTON_B){f->screen=WW_INFO;f->row=0;}
  return 0;
 }
 if(tap) {
  unsigned y=in->touches[0].y;
  if(y>=52&&y<52+rows(f)*18){f->row=(uint8_t)((y-52)/18);p=P4_BUTTON_A;}
 }
 if(p&P4_BUTTON_UP)f->row=(uint8_t)((f->row+rows(f)-1)%rows(f));
 if(p&P4_BUTTON_DOWN)f->row=(uint8_t)((f->row+1)%rows(f));
 bool accept=(p&(P4_BUTTON_A|P4_BUTTON_START))!=0;
 int d=(p&P4_BUTTON_LEFT)?-1:((p&P4_BUTTON_RIGHT)||accept)?1:0;
 if(p&P4_BUTTON_B){if(f->screen==WW_PAUSE)return WW_FRONT_RESUME;f->screen=WW_HOME;f->row=0;return 0;}
 if(f->screen==WW_HOME&&accept) {
  if(f->row<3){f->mode=f->row==0?WW_CUP:f->row==1?WW_SINGLE:WW_DUCK;f->screen=WW_SETUP;f->row=0;}
  else if(f->row==3){f->screen=WW_OPTIONS;f->row=0;}
  else if(f->row==4){f->screen=WW_HELP;f->row=0;}
  else if(f->row==5){f->screen=WW_INFO;f->row=0;}
  else return WW_FRONT_EXIT;
 } else if(f->screen==WW_SETUP&&d) {
  switch(f->row) {
  case 0:f->vehicle=(uint8_t)((f->vehicle+8+d)%8);break;
  case 1:if(f->mode==WW_SINGLE)f->track=(uint8_t)((f->track+5+d)%5);break;
  case 2:f->laps=(uint8_t)((f->laps+4+d)%4);break;
  case 3:f->engine=(uint8_t)(3-f->engine);break;
  case 4:f->race_class=(uint8_t)((f->race_class-1+5+d)%5+1);break;
  case 5:if(accept)return WW_FRONT_LAUNCH;break;
  case 6:if(accept){f->screen=WW_HOME;f->row=0;}break;
  }
 } else if(f->screen==WW_OPTIONS&&d) {
  switch(f->row){
  case 0:f->music=(uint8_t)((f->music+11+d)%11);break;
  case 1:f->effects=(uint8_t)((f->effects+11+d)%11);break;
  case 2:f->motor=(uint8_t)((f->motor+11+d)%11);break;
  case 3:f->map=!f->map;break;
  case 4:f->clock=!f->clock;break;
  case 5:f->speedometer=!f->speedometer;break;
  case 6:if(accept){f->screen=WW_HOME;f->row=0;}break;
  }
 } else if(f->screen==WW_INFO&&accept) {
  if(f->row<2){f->screen=WW_SLIDES;f->slide_group=(uint8_t)(f->row+1);f->slide=0;}
  else if(f->row==2)return WW_FRONT_INTRO;
  else {f->screen=WW_HOME;f->row=0;}
 } else if(f->screen==WW_PAUSE&&accept)return f->row==0?WW_FRONT_RESUME:f->row==1?WW_FRONT_RESTART:WW_FRONT_HOME;
 else if((f->screen==WW_HELP||f->screen==WW_RESULTS)&&accept){f->screen=WW_HOME;f->row=0;}
 return 0;
}
bool ww_front_draw(WwFront *f,WwDisplay *d,const WwArchive *a) {
 unsigned group=f->screen==WW_SLIDES?f->slide_group:0;
 if(group!=f->loaded_group||(group&&f->slide!=f->loaded_slide)) {
  char name[16];if(group)snprintf(name,sizeof(name),"%s%u.PCX",group==1?"INT":"OR",(unsigned)f->slide+1);else strcpy(name,"CHECK.PCX");
  WwArchiveView v;ww_front_close(f);
  if(!ww_archive_view(a,name,&v)||!ww_pcx_decode(v.data,v.size,&f->background))return false;
  f->loaded_group=(uint8_t)group;f->loaded_slide=f->slide;
 }
 if(!f->background.pixels)return false;
 ww_display_set_palette(d,f->background.palette);
 if(!ww_display_blit_pcx(d,&f->background))return false;
 if(f->screen==WW_SETUP) {
  WwArchiveView v;
  if(ww_archive_view(a,"CARS.SP",&v)&&v.size==WW_CAR_BYTES)
   ww_display_blit_column_major(d,263,17,38,28,v.data+(size_t)f->vehicle*12*38*28+4*38*28,28,0);
 }
 return true;
}
static void label(p4_game_surface_t *s,int x,int y,const char *t,uint16_t c){p4_draw_text(s,x,y,t,c,1,50);}
void ww_front_overlay(const WwFront *f,p4_game_surface_t *s) {
 if(f->screen==WW_SLIDES) {
  p4_draw_fill_rect(s,0,182,320,18,0x1044);
  label(s,55,188,"TAP HERE: BACK TO EXTRAS",0xffff);return;
 }
 p4_draw_fill_rect(s,14,7,292,37,0x1044);label(s,24,13,"WACKY WHEELS",0xffe0);
 const char *subtitle=f->screen==WW_SETUP?(f->mode==WW_CUP?"BRONZE CHAMPIONSHIP":f->mode==WW_DUCK?"WACKY DUCK SHOOT":"SINGLE RACE"):
  f->screen==WW_INFO?"ORIGINAL GAME EXTRAS":f->screen==WW_OPTIONS?"OPTIONS":f->screen==WW_HELP?"HOW TO PLAY":f->screen==WW_RESULTS?"RESULTS":f->screen==WW_PAUSE?"PAUSED":"SHAREWARE EDITION";
 label(s,24,29,subtitle,0xffff);
 if(f->screen==WW_HELP) {
  const char *lines[]={"UP / A: ACCELERATE   DOWN: BRAKE","LEFT / RIGHT: STEER   B: FIRE","START: PAUSE / RETRY / MAIN MENU","BACK: EXIT TO CONSOLE","5 RACES, 8 DRIVERS, DUCK SHOOT","NETWORK PLAY: OS MULTIPLAYER","ORIGINAL GAME: BEAVIS SOFT / APOGEE"};
  for(unsigned i=0;i<7;++i){p4_draw_fill_rect(s,18,50+(int)i*17,284,16,0x1044);label(s,23,55+(int)i*17,lines[i],0xffff);}
 } else if(f->screen==WW_RESULTS) {
  char b[48];p4_draw_fill_rect(s,18,52,284,103,0x1044);
  if(f->mode==WW_DUCK)snprintf(b,sizeof(b),"SCORE %u    BEST %u",(unsigned)f->result_score,(unsigned)f->duck_best);
  else snprintf(b,sizeof(b),"TIME %u.%u  BEST %u.%u",(unsigned)(f->result_time/10),(unsigned)(f->result_time%10),(unsigned)(f->best_time[f->track]/10),(unsigned)(f->best_time[f->track]%10));label(s,42,76,b,0xffe0);
  label(s,42,116,"A / START TO RETURN",0xffff);
 } else for(unsigned i=0;i<rows(f);++i) {
  char b[48]={0};const char *t=b;
  if(f->screen==WW_HOME){static const char *const menu[]={"CHAMPIONSHIP","SINGLE RACE","WACKY DUCK SHOOT","OPTIONS","HOW TO PLAY","ORIGINAL GAME EXTRAS","EXIT TO CONSOLE"};t=menu[i];}
  else if(f->screen==WW_SETUP) {
   switch(i){case 0:snprintf(b,sizeof(b),"DRIVER: %s",names[f->vehicle]);break;
    case 1:snprintf(b,sizeof(b),"COURSE: %u",f->mode==WW_CUP?1u:f->mode==WW_DUCK?7u:(unsigned)f->track+1);break;
    case 2:snprintf(b,sizeof(b),"LAPS: %u",f->race_class==4?6u:f->race_class==5?3u:lap_values[f->laps]);break;
    case 3:snprintf(b,sizeof(b),"ENGINE: %u HP",f->engine==1?12u:6u);break;
    case 4:t=f->race_class==1?"CLASS: AMATEUR":f->race_class==2?"CLASS: PRO":f->race_class==3?"CLASS: CHAMPION":f->race_class==4?"CLASS: TIME TRIAL":"CLASS: KID MODE";break;
    case 5:t="START RACE";break;case 6:t="MAIN MENU";break;}
  } else if(f->screen==WW_OPTIONS){switch(i){
   case 0:snprintf(b,sizeof(b),"MUSIC: %u / 10",(unsigned)f->music);break;
   case 1:snprintf(b,sizeof(b),"SOUND EFFECTS: %u / 10",(unsigned)f->effects);break;
   case 2:snprintf(b,sizeof(b),"ENGINE SOUND: %u / 10",(unsigned)f->motor);break;
   case 3:t=f->map?"MAP: ON":"MAP: OFF";break;case 4:t=f->clock?"CLOCK: ON":"CLOCK: OFF";break;case 5:t=f->speedometer?"SPEEDOMETER: ON":"SPEEDOMETER: OFF";break;case 6:t="MAIN MENU";break;
  }} else if(f->screen==WW_INFO){static const char *const menu[]={"ORIGINAL INSTRUCTIONS","SHAREWARE INFORMATION","REPLAY INTRO","MAIN MENU"};t=menu[i];}
  else if(f->screen==WW_PAUSE){static const char *const menu[]={"RESUME","RESTART RACE","MAIN MENU"};t=menu[i];}
  p4_draw_fill_rect(s,18,50+(int)i*18,284,17,i==f->row?0x3188:0x1044);
  label(s,27,55+(int)i*18,i==f->row?">":" ",0xffe0);label(s,41,55+(int)i*18,t,i==f->row?0xffe0:0xffff);
 }
 p4_draw_fill_rect(s,0,182,320,18,0x1044);
 label(s,12,188,f->screen==WW_HOME?"TAP A ROW   /   ARROWS + A: SELECT":
       f->screen==WW_PAUSE?"TAP HERE TO RESUME   /   B: RESUME":"TAP HERE TO RETURN   /   B: BACK",0xffff);
}
