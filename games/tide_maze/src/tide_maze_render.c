// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include "p4/presentation.h"
#include "p4/mesh.h"
#include "tide_maze_water_projection.h"
#include "generated/water.inc"
#include "generated/marble_lighting.inc"

typedef p4_mesh_point_t point;
static const int8_t wave[64]={0,12,25,37,49,60,71,81,90,98,106,112,117,122,125,126,127,126,125,122,117,112,106,98,90,81,71,60,49,37,25,12,0,-12,-25,-37,-49,-60,-71,-81,-90,-98,-106,-112,-117,-122,-125,-126,-127,-126,-125,-122,-117,-112,-106,-98,-90,-81,-71,-60,-49,-37,-25,-12};
static uint16_t rgb(int r,int g,int b){return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
static char *number(char *out,unsigned value){char a[10];unsigned n=0;do{a[n++]=(char)('0'+value%10U);value/=10U;}while(value&&n<10U);while(n)*out++=a[--n];*out=0;return out;}
static void rect(p4_game_surface_t *f,int x,int y,int w,int h,uint16_t c){int px=p4_ui_x(f,x),py=p4_ui_y(f,y);p4_draw_fill_rect(f,px,py,p4_ui_x(f,x+w)-px,p4_ui_y(f,y+h)-py,c);}
static void text(p4_game_surface_t *f,int x,int y,const char *t,uint16_t c,int size){p4_ui_text(f,p4_ui_x(f,x),p4_ui_y(f,y),t,c,(unsigned)p4_ui_y(f,size),60);}
/* Q8 world -> perspective -> native pixels. No intermediate integer logical
 * pixel truncation: slow marble motion survives both negotiated resolutions. */
static point project(const p4_game_surface_t *f,int x,int y,int z){
 const int xc=x-120*TM_Q;
 const int depth=600-y/TM_Q-xc/(10*TM_Q);
 const int sx=156*TM_Q+(xc*530+(y-72*TM_Q)*60)/depth;
 const int sy=42*TM_Q+(y*330+xc*45-z*600)/depth;
 return (point){sx*(int)f->width/(320*TM_Q),sy*(int)f->height/(200*TM_Q)};
}
static void face(p4_game_surface_t *f,point a,point b,point c,point d,uint16_t color){const point v[4]={a,b,c,d};p4_draw_face(f,v,4,color);}
static void line(p4_game_surface_t *f,point a,point b,uint16_t color){p4_draw_mesh_line(f,a,b,color);}
static void plane(p4_game_surface_t *f,int x,int y,int w,int h,int z,uint16_t color){
 face(f,project(f,x,y,z),project(f,x+w,y,z),project(f,x+w,y+h,z),project(f,x,y+h,z),color);
}
static void box(p4_game_surface_t *f,int x,int y,int w,int h,int bottom,int top,uint16_t front,uint16_t side,uint16_t cap){
 point a=project(f,x,y,top),b=project(f,x+w,y,top),c=project(f,x+w,y+h,top),d=project(f,x,y+h,top);
 point aa=project(f,x,y,bottom),bb=project(f,x+w,y,bottom),cc=project(f,x+w,y+h,bottom),dd=project(f,x,y+h,bottom);
 if(x<120*TM_Q)face(f,b,bb,cc,c,side);
 if(x+w>120*TM_Q)face(f,aa,a,d,dd,side);
 face(f,d,c,cc,dd,front);face(f,a,b,c,d,cap);
}
typedef struct { point position; int height; } water_vertex;
static int surface_height(const tm_state *s,int x,int y,unsigned phase_a,unsigned phase_b,bool *used){
 int sum=0,n=0;
 for(int dy=-1;dy<=0;++dy)for(int dx=-1;dx<=0;++dx){int xx=x+dx,yy=y+dy;
  if(xx>=0&&xx<TM_W&&yy>=0&&yy<TM_H&&s->wet[yy*TM_W+xx]){sum+=s->water[yy*TM_W+xx];++n;}}
 *used=n!=0;int level=n?sum/n:320;
 /* The solver drives the large slosh. Two small travelling waves prevent a
  * stationary, tile-coloured surface; phase is evaluated every render. */
 int ripple=wave[(phase_a+(unsigned)x*5U+(unsigned)y*3U)%64U]+wave[(phase_b+(unsigned)x*2U+64U-(unsigned)y%64U)%64U];
 return 2*TM_Q+tm_clamp((level-320)*2,-TM_Q,2*TM_Q)+ripple*2/3;
}
/* Three rolling rows retain every used height. A drawn wet cell contributes to
 * all four corner stencils, so a vertex with no wet neighbor is never consumed.
 * Static grid projection constants retain both native and legacy scene pixels. */
static void water_row(p4_game_surface_t *f,const tm_state *s,int y,water_vertex *row){
 const unsigned phase_a=s->animation_ms/19U,phase_b=s->animation_ms/27U;
 const bool native=f->width==P4_GAME_SURFACE_HIGH_RES_WIDTH;
 for(int x=0;x<=TM_W;++x){
  bool used;
  row[x].height=surface_height(s,x,y,phase_a,phase_b,&used);
  row[x].position=used?tm_water_project(&tm_water_projection[y][x],row[x].height,native):(point){0,0};
 }
}
/* Compile the bounded shared raster with an already-resolved palette pointer.
 * RV32 -Os otherwise reloads the palette offset inside every texture sample. */
static __attribute__((noinline)) void water_face(p4_game_surface_t *f,const p4_mesh_tex_vertex_t *v,const uint16_t *texture){
 p4_draw_textured_face(f,v,4,texture,128);
}
/* Bound stack/register lifetime to one cell, independent of the scene/UI. */
static __attribute__((noinline)) void water_cell(p4_game_surface_t *f,const tm_state *s,int x,int y,const water_vertex *top,const water_vertex *bottom){
 int i=y*TM_W+x;if(!s->wet[i])return;
 int h0=top[x].height,h1=top[x+1].height,h2=bottom[x+1].height,h3=bottom[x].height;
 point a=top[x].position,b=top[x+1].position,c=bottom[x+1].position,d=bottom[x].position;
 int light=tm_clamp((h0-h2)/6+(h1-h3)/9,-20,32);
 if((y>0&&!s->wet[i-TM_W])||(x>0&&!s->wet[i-1]))light-=12;
 int u=x*16*TM_Q+(int)(s->animation_ms%32768U)*2;
 int v=y*16*TM_Q+(int)(s->animation_ms%32768U);
 int du=(h1-h0)/2,dv=(h3-h0)/2;
 const p4_mesh_tex_vertex_t uv[4]={{a,u,v},{b,u+16*TM_Q+du,v},{c,u+16*TM_Q+du,v+16*TM_Q+dv},{d,u,v+16*TM_Q+dv}};
 unsigned palette=light < -12?0U:(light>18?2U:1U);
 water_face(f,uv,water_material+palette*16384U);
 if(x>0&&!s->wet[i-1])line(f,a,d,rgb(162,224,213));
 if(y>0&&!s->wet[i-TM_W])line(f,a,b,rgb(128,209,207));
}
static void ellipse(p4_game_surface_t *f,int x,int y,int rx,int ry,uint16_t color){
 if(rx<1||ry<1)return;
 for(int row=-ry;row<=ry;++row){int half=rx*(ry*ry-row*row)/(ry*ry);if(half<1)half=1;p4_draw_fill_rect(f,x-half,y+row,half*2+1,1,color);}
}
static void marble(p4_game_surface_t *f,const tm_state *s,unsigned p){
 if(s->ball[p].rescue&&(s->animation_ms/100U%2U))return;
 int32_t x,y;tm_visual_ball(s,p,&x,&y);
 point shadow=project(f,x+TM_Q,y+2*TM_Q,2*TM_Q),center=project(f,x,y,5*TM_Q);
 int radius=(int)f->width*2450/((600-y/TM_Q)*320);if(radius<3)radius=3;
 ellipse(f,shadow.x,shadow.y,radius+2,radius/2,rgb(15,83,98));
 /* The baked LUT exactly retains the former integer lighting. The dynamic
  * stripe only selects a palette; the hot loop performs no divide, remainder,
  * validation call or color arithmetic per pixel. Physical game bounds make
  * the radius 4..12 at the two supported surfaces (3..14 is provisioned). */
 if(radius>14)return;
 const int diameter=radius*2+1,area=diameter*diameter;
 const uint16_t *base=tm_marble_lighting+tm_marble_offsets[radius]+(int)p*area*2;
 const int left=center.x-radius,top=center.y-radius;
 const int first_x=left<0?-left:0,last_x=left+diameter>(int)f->width?(int)f->width-left:diameter;
 const int first_y=top<0?-top:0,last_y=top+diameter>(int)f->height?(int)f->height-top:diameter;
 const int period=radius+2;
 for(int row=first_y;row<last_y;++row){
  const uint16_t *normal=base+row*diameter+first_x,*striped=normal+area;
  uint16_t *dst=f->pixels+(size_t)(top+row)*f->stride_pixels+(size_t)(left+first_x);
  int raw=(x+y)/(TM_Q/2)+row+first_x-radius*2;
  int phase=raw>0?raw%period:0;
  for(int column=first_x;column<last_x;++column){
   const uint16_t color=(raw<0||phase<2)?*striped:*normal;
   if(color)*dst=color;
   ++dst;++normal;++striped;++raw;
   if(raw>0&&++phase==period)phase=0;
  }
 }
 p4_draw_fill_circle(f,center.x-radius/3,center.y-radius/3,radius/4,rgb(255,255,245));
 if(s->linked){text(f,center.x*320/(int)f->width-2,center.y*200/(int)f->height-13,p==0?"1":"2",rgb(247,244,216),8);}
}
static void objects(p4_game_surface_t *f,const tm_state *s,int row,unsigned *pearl){
 for(int x=1;x<TM_COLS-1;++x){char t=tm_tile(s->level,x,row);int xx=(x*16+8)*TM_Q,yy=(row*16+8)*TM_Q;
  point a=project(f,xx,yy,2*TM_Q);int r=p4_ui_x(f,3);
  if(t=='o'){
   if(!(s->pearls&(1U<<*pearl))){
    ellipse(f,a.x+2,a.y+3,r+2,r/2,rgb(25,106,118));
    int z=4*TM_Q+wave[(s->animation_ms/18U+(unsigned)x*7U)%64U]/2;
    point c=project(f,xx,yy,z);
    p4_draw_fill_circle(f,c.x,c.y,r+1,rgb(120,92,45));p4_draw_fill_circle(f,c.x,c.y-1,r,rgb(234,183,89));p4_draw_fill_circle(f,c.x-1,c.y-2,r/2,rgb(255,242,191));
   }++*pearl;
  }else if(t=='~'){
   ellipse(f,a.x,a.y,r*2,r,rgb(11,75,96));ellipse(f,a.x,a.y,r,r/2,rgb(4,29,46));
   unsigned phase=s->animation_ms/14U%64U;int dx=wave[phase]*r*2/128,dy=wave[(phase+16U)%64U]*r/128;
   p4_draw_fill_circle(f,a.x+dx,a.y+dy,r/3,rgb(168,229,218));
  }else if(t=='E'){
   bool ready=s->pearls==((1U<<s->all_pearls)-1U);
   ellipse(f,a.x,a.y,r*3,r*2,ready?rgb(245,197,92):rgb(91,116,110));
   ellipse(f,a.x,a.y,r*2,r,rgb(25,90,102));
   text(f,a.x*320/(int)f->width-3,a.y*200/(int)f->height-4,"E",rgb(255,238,187),8);
  }
 }
 for(unsigned p=0;p<(s->linked?2U:1U);++p){int32_t x,y;tm_visual_ball(s,p,&x,&y);if(y/(16*TM_Q)==row)marble(f,s,p);}
}
static __attribute__((noinline)) void scene(p4_game_surface_t *f,const tm_state *s){
 /* A single background pass, followed by bounded, back-to-front mesh faces. */
 for(int t=0;t<18;++t){
  int first=(t*(int)f->height+17)/18,last=((t+1)*(int)f->height+17)/18;
  p4_draw_fill_rect(f,0,first,f->width,last-first,rgb(16-t/3,29-t/2,43-t/2));
 }
 plane(f,-5*TM_Q,4*TM_Q,250*TM_Q,146*TM_Q,-12*TM_Q,rgb(4,12,22));
 box(f,-3*TM_Q,-3*TM_Q,246*TM_Q,150*TM_Q,-9*TM_Q,0,rgb(45,58,62),rgb(64,77,76),rgb(97,119,115));
 plane(f,0,0,240*TM_Q,144*TM_Q,0,rgb(18,94,113));
 unsigned pearl=0;
 water_vertex rows[3][TM_W+1];
 water_vertex *top=rows[0],*middle=rows[1],*bottom=rows[2];
 water_row(f,s,0,top);
 for(int y=0;y<TM_ROWS;++y){
  water_row(f,s,y*2+1,middle);water_row(f,s,y*2+2,bottom);
  for(int x=0;x<TM_COLS;++x){
   if(tm_tile(s->level,x,y)=='#'){
    int end=x+1;while(end<TM_COLS&&tm_tile(s->level,end,y)=='#')++end;
    int xx=x*16*TM_Q,yy=y*16*TM_Q,ww=(end-x)*16*TM_Q;
    box(f,xx,yy,ww,16*TM_Q,0,6*TM_Q,rgb(72,108,117),rgb(98,135,140),rgb(166,193,187));
    plane(f,xx+TM_Q/2,yy+TM_Q/2,ww-TM_Q,15*TM_Q,6*TM_Q+16,rgb(214,223,201));
    line(f,project(f,xx,yy,6*TM_Q),project(f,xx+ww,yy,6*TM_Q),rgb(249,244,218));
    x=end-1;
   }else{
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)water_cell(f,s,x*2+dx,y*2+dy,dy?middle:top,dy?bottom:middle);
   }
  }
  if(y>0&&y<TM_ROWS-1)objects(f,s,y,&pearl);
  water_vertex *swap=top;top=bottom;bottom=swap;
 }
 point a=project(f,-3*TM_Q,147*TM_Q,-2*TM_Q),b=project(f,243*TM_Q,147*TM_Q,-2*TM_Q);line(f,a,b,rgb(181,160,104));
}
static void button(p4_game_surface_t *f,int x,const char *label,bool active){
 p4_ui_round_rect(f,p4_ui_x(f,x),p4_ui_y(f,180),p4_ui_x(f,49),p4_ui_y(f,17),p4_ui_x(f,4),active?rgb(51,130,142):rgb(31,53,68));text(f,x+6,184,label,rgb(218,233,224),8);
}
static unsigned collected(unsigned bits){unsigned n=0;for(;bits;bits>>=1)n+=bits&1U;return n;}
bool tm_render(p4_game_context_t *ctx,p4_game_surface_t *f){
 if(!p4_surface_valid(f))return false;tm_state *s=ctx->state;
 const uint16_t white=rgb(239,242,222),muted=rgb(148,186,191),gold=rgb(241,202,123);
 scene(f,s);
 text(f,9,4,"TIDE MAZE",white,13);
 const char *names[]={"STILLWATER","CROSSCURRENT","THE UNDERTOW"};text(f,10,20,names[s->level],muted,7);
 char value[32],*end=number(value,collected(s->pearls));*end++='/';number(end,s->all_pearls);
 text(f,151,5,"PEARLS",muted,7);text(f,152,15,value,gold,11);
 end=number(value,s->time_ms/60000U);*end++=':';*end++=(char)('0'+s->time_ms/10000U%6U);*end++=(char)('0'+s->time_ms/1000U%10U);*end=0;
 text(f,216,5,"TIME",muted,7);text(f,214,15,value,white,11);text(f,291,5,"Exit",muted,8);
 text(f,10,183,s->linked?"TWO MARBLES. ONE TIDE.":"ROLL WITH THE TIDE.",gold,8);
 text(f,10,193,s->motion_live?"Tilt gently. B sets your center.":"Drag the marble or use arrows.",muted,6);
 button(f,164,"A Brake",s->intent[s->slot].brake);button(f,216,"B Center",false);button(f,268,"Pause",s->phase==TM_PAUSE);
 if(s->phase!=TM_PLAY){
  /* Keep the actual 3D labyrinth visible around a compact, readable modal. */
  p4_ui_round_rect(f,p4_ui_x(f,57),p4_ui_y(f,62),p4_ui_x(f,206),p4_ui_y(f,94),p4_ui_x(f,6),rgb(7,26,40));
  rect(f,70,72,20,1,gold);
  const char *title="TIDE MAZE",*a="A marble. A labyrinth. A restless tide.",*b="Collect the pearls. Reach the gold dock.",*c="A / tap to dive in";
  if(s->phase==TM_PAUSE){title="A moment of calm";a="The water can wait.";b="B rotates tilt axes. A resumes.";c="A / tap to resume";}
  if(s->phase==TM_CLEAR){title="Safe in the harbor";a="Every pearl recovered.";b="A new labyrinth waits beyond the tide.";c="A / tap for next maze";}
  if(s->phase==TM_WON){title="Masters of the tide";a=s->linked?"Two marbles. Three mazes. One team.":"Three labyrinths safely navigated.";b="Try a quicker run, or bring a friend.";c="A / tap to dive again";}
  if(s->phase==TM_LOST){title="The tide rolled in";a="The next voyage starts fresh.";b="Brake early around the whirlpools.";c="A / tap to try again";}
  if(s->phase==TM_LINK_LOST){title="Your friend drifted away";a="Your linked run has ended.";b="Reconnect through Console Multiplayer.";c="A / tap for solo play";}
  if(s->phase==TM_WAIT){title="Catching the same wave";a="Waiting for your friend's game state.";b="Both marbles must reach the dock.";c="Back returns to the console";}
  if(s->linked&&!s->host&&s->phase!=TM_WAIT)c="Your host continues the voyage";
  text(f,70,80,title,white,13);text(f,70,102,a,muted,8);text(f,70,115,b,muted,8);
  if(s->phase==TM_TITLE){text(f,70,128,"Arrows: maze    B: rotate tilt axes",muted,7);}
  text(f,70,140,c,gold,9);
 }
 return true;
}
