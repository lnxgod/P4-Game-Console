// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include "p4/presentation.h"

#include "generated/cover.inc"
/* Caller reserves eleven bytes; all UI values are bounded game state. */
static char *number(char *out,unsigned value){
 char digits[10];unsigned n=0;
 do{digits[n++]=(char)('0'+value%10U);value/=10U;}while(value&&n<10U);
 while(n)*out++=digits[--n];*out=0;return out;
}
static uint16_t rgb(int r,int g,int b){return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
static void rect(p4_game_surface_t *f,int x,int y,int w,int h,uint16_t c){
 int px=p4_ui_x(f,x),py=p4_ui_y(f,y);p4_draw_fill_rect(f,px,py,p4_ui_x(f,x+w)-px,p4_ui_y(f,y+h)-py,c);
}
static void text(p4_game_surface_t *f,int x,int y,const char *t,uint16_t c,int size){
 p4_ui_text(f,p4_ui_x(f,x),p4_ui_y(f,y),t,c,(unsigned)p4_ui_y(f,size),60);
}
static void circle(p4_game_surface_t *f,int x,int y,int r,uint16_t c){p4_draw_fill_circle(f,p4_ui_x(f,x),p4_ui_y(f,y),p4_ui_x(f,r),c);}
static void button(p4_game_surface_t *f,int y,const char *title,bool on){
 p4_ui_round_rect(f,p4_ui_x(f,253),p4_ui_y(f,y),p4_ui_x(f,59),p4_ui_y(f,24),p4_ui_x(f,3),on?rgb(46,133,146):rgb(23,56,69));
 text(f,259,y+6,title,rgb(228,245,237),9);
}
static void marble(p4_game_surface_t *f,int32_t x,int32_t y,int radius,bool coral){
 /* Bounded Q8 positions times 768 fit int32; avoid RV32 non-PIC divdi3. */
 const int px=p4_ui_x(f,8)+(int)(x*(int32_t)f->width/(TM_Q*320));
 const int py=p4_ui_y(f,32)+(int)(y*(int32_t)f->height/(TM_Q*200));
 const int r=p4_ui_x(f,radius);
 p4_draw_fill_circle(f,px+2,py+3,r+1,rgb(5,57,69));
 for(int i=r;i>0;--i){int light=200-i*120/(r?r:1);
  p4_draw_fill_circle(f,px,py,i,coral?rgb(245,light,light*3/4):rgb(light/2,light+35,245));}
 p4_draw_fill_circle(f,px-r/3,py-r/3,r/3? r/3:1,rgb(237,255,249));
}
static unsigned collected(unsigned bits){unsigned n=0;for(;bits;bits>>=1)n+=bits&1U;return n;}
static void board(p4_game_surface_t *f,tm_state *s){
 rect(f,6,30,244,148,rgb(7,25,36));
 for(int y=0;y<TM_H;++y)for(int x=0;x<TM_W;++x){
  int i=y*TM_W+x;if(!s->wet[i])continue;
  int h=tm_clamp(s->water[i],0,700);int shade=h/18;
  uint16_t color=p4_ui_blend(rgb(162,176,146),rgb(12,116+shade,142+shade),(unsigned)tm_clamp(h/12,0,15));
  rect(f,8+x*8,32+y*8,8,8,color);
  /* Native-pixel caustics move with local flow and pressure, not a scaled frame. */
  int px=p4_ui_x(f,8+x*8),py=p4_ui_y(f,32+y*8),w=p4_ui_x(f,8),hh=p4_ui_y(f,8);
  unsigned phase=(s->animation_ms/70U+(unsigned)(x*7+y*11)+(unsigned)h/12U)%24U;
  int yy=(int)phase*hh/24;
  const int curve[8]={0,1,2,3,3,2,1,0};
  for(int k=2;k<w-2;++k){
   int bend=curve[(unsigned)(k+(int)phase)%8U]*hh/20;
   if(yy+bend<hh)p4_draw_pixel(f,px+k,py+yy+bend,rgb(69,184+shade,194+shade));
  }
  if(h>340){
   uint16_t foam=rgb(137+shade,218,214);
   if(x>0&&!s->wet[i-1])p4_draw_fill_rect(f,px,py,2,hh,foam);
   if(y>0&&!s->wet[i-TM_W])p4_draw_fill_rect(f,px,py,w,2,foam);
  }
  if(h>410)p4_draw_fill_rect(f,px+3,py+yy+2,w/2,1,rgb(170,220,217));
 }
 for(int y=0;y<TM_ROWS;++y)for(int x=0;x<TM_COLS;++x){
  if(tm_tile(s->level,x,y)!='#')continue;
  int bx=8+x*16,by=32+y*16;
  rect(f,bx,by,16,16,rgb(134,148,137));
  rect(f,bx,by,16,13,rgb(219,220,187));
  rect(f,bx,by,16,1,rgb(251,244,214));
  rect(f,bx,by,1,14,rgb(239,235,202));
  rect(f,bx+15,by+1,1,15,rgb(114,135,126));
  /* Fine joints and bevels stay native geometry. */
  rect(f,bx+3,by+11,10,1,rgb(200,207,177));
 }
 unsigned pearl=0;
 for(int y=1;y<TM_ROWS-1;++y)for(int x=1;x<TM_COLS-1;++x){
  const char t=tm_tile(s->level,x,y);int bx=8+x*16+8,by=32+y*16+8;
  if(t=='o'){
   if(!(s->pearls&(1U<<pearl))){circle(f,bx,by,5,rgb(38,163,170));circle(f,bx,by,3,rgb(244,222,154));circle(f,bx-1,by-1,1,rgb(255,255,235));}
   ++pearl;
  }else if(t=='~'){
   circle(f,bx,by,6,rgb(13,102,127));circle(f,bx,by,4,rgb(12,66,91));circle(f,bx,by,2,rgb(4,35,56));
   int o=(int)(s->animation_ms/130U%4U);rect(f,bx-5+o,by-4,4,1,rgb(151,230,225));
  }else if(t=='E'){
   bool ready=s->pearls==((1U<<s->all_pearls)-1U);
   circle(f,bx,by,7,ready?rgb(230,199,104):rgb(66,99,104));circle(f,bx,by,5,rgb(16,81,95));
   text(f,bx-3,by-4,"E",rgb(245,239,199),8);
  }
 }
 for(unsigned p=0;p<(s->linked?2U:1U);++p){
  if(s->ball[p].rescue&&(s->animation_ms/100U%2U))continue;
  int32_t x=s->ball[p].x,y=s->ball[p].y;
  if(s->linked&&!s->host){x=s->previous_x[p]+(x-s->previous_x[p])*(int32_t)s->blend_ms/50;y=s->previous_y[p]+(y-s->previous_y[p])*(int32_t)s->blend_ms/50;}
  marble(f,x,y,3,p==1);
  if(s->linked)text(f,8+x/TM_Q-2,32+y/TM_Q-10,p==0?"1":"2",rgb(255,255,233),7);
 }
}
bool tm_render(p4_game_context_t *ctx,p4_game_surface_t *f){
 if(!p4_surface_valid(f))return false;tm_state *s=ctx->state;
 const uint16_t white=rgb(236,243,222),muted=rgb(143,191,195),gold=rgb(246,212,142);
 p4_draw_clear(f,rgb(11,31,45));
 text(f,9,5,"TIDE MAZE",white,16);
 const char *names[]={"01  STILLWATER","02  CROSSCURRENT","03  THE UNDERTOW"};
 text(f,9,22,names[s->level],muted,7);
 text(f,288,1,"Exit",muted,8);
 text(f,252,12,s->linked?"TOGETHER":"SOLO DIVE",gold,9);
 board(f,s);
 char line[64];
 if(s->linked){text(f,252,25,"You are",white,8);number(line,(unsigned)s->slot+1U);text(f,280,25,line,white,8);}
 char *end=number(line,collected(s->pearls));*end++=' ';*end++='/';*end++=' ';number(end,s->all_pearls);
 text(f,258,36,"PEARLS",muted,8);text(f,257,48,line,gold,18);
 end=number(line,s->time_ms/60000U);*end++=':';
 *end++=(char)('0'+s->time_ms/10000U%6U);*end++=(char)('0'+s->time_ms/1000U%10U);*end=0;
 text(f,258,75,"TIME LEFT",muted,8);text(f,257,86,line,white,16);
 button(f,113,"A  Brake",s->intent[s->slot].brake);button(f,141,"B  Center",false);button(f,169,"Pause",s->phase==TM_PAUSE);
 text(f,10,184,s->motion_live?"TILT TO ROLL   /   GENTLE MOVES, SMALL WAVES":"DRAG TO STEER   /   ARROWS TO ROLL   /   A TO BRAKE",muted,8);
 if(s->phase==TM_TITLE){
  p4_ui_sprite(f,p4_ui_x(f,8),p4_ui_y(f,31),p4_ui_x(f,240),p4_ui_y(f,146),cover,288,162,false,0);
 }
 if(s->phase!=TM_PLAY){
  p4_ui_round_rect(f,p4_ui_x(f,30),p4_ui_y(f,53),p4_ui_x(f,202),p4_ui_y(f,105),p4_ui_x(f,6),rgb(7,34,48));
  const char *title="Tide Maze",*a="Collect every pearl. Find the gold exit.",*b="Tilt gently; hold A to steady your marble.",*c="A / tap to dive in";
  if(s->phase==TM_PAUSE){title="A moment of calm";a="Take a breath. The tide can wait.";b="B rotates tilt axes; A resumes.";c="A / tap to resume";}
  if(s->phase==TM_CLEAR){title="Safe in the harbor";a="Every pearl recovered. Beautiful sailing.";b="A new labyrinth waits beyond the tide.";c="A / tap for next maze";}
  if(s->phase==TM_WON){title="Masters of the tide";a=s->linked?"Two marbles. Three mazes. One great team.":"Three labyrinths safely navigated.";b="Try a quicker run, or bring a friend.";c="A / tap to dive again";}
  if(s->phase==TM_LOST){title="The tide rolled in";a="Time ran out. Your next run starts fresh.";b="Whirlpools cost three seconds. Brake early.";c="A / tap to try again";}
  if(s->phase==TM_LINK_LOST){title="Your friend drifted away";a="The linked run has ended.";b="Reconnect in the console's Multiplayer menu.";c="A / tap for a fresh solo run";}
  if(s->phase==TM_WAIT){title="Catching the same wave";a="Waiting for the first shared game state.";b="Both players collect pearls and reach E.";c="Back returns to the console";}
  if(s->linked&&!s->host&&s->phase!=TM_WAIT)c="Your host continues the voyage";
  text(f,40,63,title,white,14);text(f,40,87,a,muted,8);text(f,40,101,b,muted,8);
  if(s->phase==TM_TITLE){
   text(f,40,116,"Arrows: maze",gold,8);number(line,s->level+1U);text(f,88,116,line,gold,8);
   text(f,116,116,"B: tilt axes",gold,8);number(line,(unsigned)s->orientation+1U);text(f,157,116,line,gold,8);
  }
  text(f,40,139,c,gold,9);
 }
 return true;
}
