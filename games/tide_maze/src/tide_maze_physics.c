// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include <string.h>
/* Original hand-authored labyrinths: # stone, o pearl, ~ whirlpool, E dock. */
const char *const tm_maps[TM_LEVELS][TM_ROWS]={
 {"###############","#S....#...o...#","#.##..#.###.#.#","#..o..#.....#.#","###.#####.#.#.#","#...#...o.#...#","#.#.#.###.###.#","#o~.........E.#","###############"},
 {"###############","#S..#...o.....#","#.#.#.#####.#.#","#.#...#...#.#.#","#.#####.~.#.#.#","#...o...#...#.#","###.###.#####.#","#o....~.....E.#","###############"},
 {"###############","#S....~...#..o#","#.#######.#.#.#","#...#o....#.#.#","###.#.#####.#.#","#o..#...~...#.#","#.#####.###.#.#","#.....o.....E.#","###############"}
};
int tm_clamp(int value,int lo,int hi){return value<lo?lo:(value>hi?hi:value);}
char tm_tile(unsigned level,int x,int y){
 return level<TM_LEVELS && x>=0 && x<TM_COLS && y>=0 && y<TM_ROWS ? tm_maps[level][y][x]:'#';
}
static int abs_i(int x){return x<0?-x:x;}
void tm_reset(tm_state *s,unsigned level){
 s->level=level%TM_LEVELS;s->pearls=0;s->all_pearls=0;s->docked=0;s->rescues=0;
 s->time_ms=150000;s->accumulator=0;s->phase=TM_PLAY;
 memset(s->intent,0,sizeof(s->intent));
 memset(s->flow_x,0,sizeof(s->flow_x));memset(s->flow_y,0,sizeof(s->flow_y));
 for(int y=0;y<TM_H;++y)for(int x=0;x<TM_W;++x){
  int i=y*TM_W+x;s->wet[i]=(uint8_t)(tm_tile(s->level,x/2,y/2)!='#');
  s->water[i]=(int16_t)(s->wet[i]?320:0);
 }
 for(int y=0;y<TM_ROWS;++y)for(int x=0;x<TM_COLS;++x)
  if(tm_tile(s->level,x,y)=='o')++s->all_pearls;
 for(int p=0;p<2;++p){s->ball[p]=(tm_ball){.x=(24+p*3)*TM_Q,.y=24*TM_Q};
  s->previous_x[p]=s->ball[p].x;s->previous_y[p]=s->ball[p].y;}
}
/* Conservative face fluxes. Each pair exchanges equal volume, walls reflect,
 * bounded pressure and damping form a shallow-water approximation at 50 Hz. */
static void flux(tm_state *s,int a,int b,int16_t *velocity,int force){
 if(!s->wet[a]||!s->wet[b]){*velocity=0;return;}
 int v=(*velocity*29)/32+(s->water[a]-s->water[b])/10+force;
 v=tm_clamp(v,-40,40);
 v=tm_clamp(v,-s->water[b]/4,s->water[a]/4);
 v=tm_clamp(v,-(1024-s->water[a])/4,(1024-s->water[b])/4);
 *velocity=(int16_t)v;
 s->water[a]=(int16_t)(s->water[a]-v);s->water[b]=(int16_t)(s->water[b]+v);
}
void tm_fluid(tm_state *s){
 const int count=s->linked?2:1;
 const int ax=(s->intent[0].x+(count==2?s->intent[1].x:0))/count;
 const int ay=(s->intent[0].y+(count==2?s->intent[1].y:0))/count;
 const int spin=(s->intent[0].spin+(count==2?s->intent[1].spin:0))/count;
 for(int y=0;y<TM_H;++y)for(int x=0;x<TM_W;++x){
  int i=y*TM_W+x;
  if(x+1<TM_W)flux(s,i,i+1,&s->flow_x[i],ax/100-spin*(y-TM_H/2)/1000);
  if(y+1<TM_H)flux(s,i,i+TM_W,&s->flow_y[i],ay/100+spin*(x-TM_W/2)/1000);
 }
 for(int p=0;p<count;++p){
  const tm_ball *b=&s->ball[p];int x=tm_clamp(b->x/(8*TM_Q),1,TM_W-2);
  int y=tm_clamp(b->y/(8*TM_Q),1,TM_H-2),i=y*TM_W+x;
  if(s->wet[i]){
   /* A wake and vertical jolt move volume into adjacent cells, never create it. */
   int kick=tm_clamp((abs_i(b->vx)+abs_i(b->vy))/64+abs_i(s->intent[p].jolt)/50,0,24);
   const int neighbor[4]={i-1,i+1,i-TM_W,i+TM_W};
   for(int n=0;n<4;++n){int j=neighbor[n];if(!s->wet[j])continue;
    int amount=tm_clamp(kick,0,s->water[i]/8);
    amount=tm_clamp(amount,0,(1024-s->water[j])/8);
    s->water[i]=(int16_t)(s->water[i]-amount);s->water[j]=(int16_t)(s->water[j]+amount);
   }
  }
 }
}
static bool blocked(tm_state *s,int32_t x,int32_t y){
 const int r=3*TM_Q;
 for(int cy=(y-r)/(16*TM_Q);cy<=(y+r)/(16*TM_Q);++cy)
  for(int cx=(x-r)/(16*TM_Q);cx<=(x+r)/(16*TM_Q);++cx)
   if(tm_tile(s->level,cx,cy)=='#')return true;
 return x<r||y<r||x>(240*TM_Q-r)||y>(144*TM_Q-r);
}
void tm_simulate(tm_state *s){
 if(s->phase!=TM_PLAY)return;
 if(s->time_ms<=TM_STEP){s->time_ms=0;s->phase=TM_LOST;return;}
 s->time_ms-=TM_STEP;tm_fluid(s);
 const unsigned count=s->linked?2U:1U;
 s->docked=0;
 for(unsigned p=0;p<count;++p){
  tm_ball *b=&s->ball[p];const tm_intent *in=&s->intent[p];
  s->previous_x[p]=b->x;s->previous_y[p]=b->y;
  if(b->rescue){b->rescue=(uint16_t)(b->rescue>TM_STEP?b->rescue-TM_STEP:0);continue;}
  int ix=tm_clamp(b->x/(8*TM_Q),1,TM_W-2),iy=tm_clamp(b->y/(8*TM_Q),1,TM_H-2),i=iy*TM_W+ix;
  int drag=in->brake?190:246;
  b->vx=tm_clamp((b->vx*drag)/256+in->x/18+s->flow_x[i]/3,-280,280);
  b->vy=tm_clamp((b->vy*drag)/256+in->y/18+s->flow_y[i]/3,-280,280);
  /* Max speed <1.1 logical pixels/tick, far below a wall thickness. */
  if(!blocked(s,b->x+b->vx,b->y))b->x+=b->vx;else b->vx=-b->vx/3;
  if(!blocked(s,b->x,b->y+b->vy))b->y+=b->vy;else b->vy=-b->vy/3;
  unsigned pearl=0;
  for(int y=1;y<TM_ROWS-1;++y)for(int x=1;x<TM_COLS-1;++x){
   char t=tm_tile(s->level,x,y);int dx=b->x/TM_Q-(x*16+8),dy=b->y/TM_Q-(y*16+8);
   int d=dx*dx+dy*dy;
   if(t=='o'){if(d<64)s->pearls|=1U<<pearl;++pearl;}
   if(t=='E' && d<64)s->docked|=1U<<p;
   if(t=='~' && d<100){
    b->vx=tm_clamp(b->vx-dx*3,-280,280);b->vy=tm_clamp(b->vy-dy*3,-280,280);
    if(d<9){b->x=(24+(int)p*3)*TM_Q;b->y=24*TM_Q;b->vx=b->vy=0;b->rescue=700;
     s->previous_x[p]=b->x;s->previous_y[p]=b->y;
     ++s->rescues;s->time_ms=s->time_ms>3000?s->time_ms-3000:0;}
   }
  }
 }
 if(s->pearls==((1U<<s->all_pearls)-1U)&&s->docked==((1U<<count)-1U))
  s->phase=s->level+1U==TM_LEVELS?TM_WON:TM_CLEAR;
}
static int root(int v){int r=0;for(int bit=4096;bit;bit/=2){int n=r+bit;if(n*n<=v)r=n;}return r;}
static int dot(const int32_t *a,const int32_t *b){return (a[0]*b[0]+a[1]*b[1]+a[2]*b[2])/1000;}
static void normalize(int32_t *v){int m=root(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(m<1)m=1;for(int n=0;n<3;++n)v[n]=v[n]*1000/m;}
static void calibrate_pose(tm_state *s,const p4_game_motion_t *m){
 memcpy(s->neutral,m->accel_mg,sizeof(s->neutral));normalize(s->neutral);
 /* Project a sensor axis onto the plane perpendicular to gravity; works for
  * arbitrary comfortable holding angles, including a nearly upright tablet. */
 int reference=abs_i(s->neutral[0])<800?0:1;
 for(int i=0;i<3;++i)s->basis_x[i]=(i==reference?1000:0)-s->neutral[i]*s->neutral[reference]/1000;
 normalize(s->basis_x);
 s->basis_y[0]=(s->neutral[1]*s->basis_x[2]-s->neutral[2]*s->basis_x[1])/1000;
 s->basis_y[1]=(s->neutral[2]*s->basis_x[0]-s->neutral[0]*s->basis_x[2])/1000;
 s->basis_y[2]=(s->neutral[0]*s->basis_x[1]-s->neutral[1]*s->basis_x[0])/1000;
 s->calibrated=true;s->filtered_x=s->filtered_y=0;
}
/* 30ms exponential correction, Q16, sampled once per elapsed millisecond.
 * Table makes the response nearly cadence-independent and avoids runtime FP.
 * Zero elapsed time must not alter the filtered pose. */
static const uint16_t motion_blend[101]={
 0,2148,4227,6236,8181,10061,11879,13639,15340,16985,18577,20117,
 21606,23046,24439,25786,27089,28350,29569,30748,31888,32991,34058,35090,
 36088,37054,37987,38890,39764,40609,41426,42216,42981,43720,44435,45127,
 45796,46443,47069,47675,48260,48826,49374,49904,50417,50912,51392,51855,
 52304,52737,53157,53563,53955,54335,54702,55057,55401,55733,56054,56365,
 56666,56957,57238,57510,57773,58027,58274,58512,58742,58965,59180,59388,
 59590,59785,59973,60156,60332,60502,60667,60827,60981,61131,61275,61415,
 61550,61680,61807,61929,62047,62162,62272,62379,62483,62583,62679,62773,
 62864,62951,63036,63118,63197,
};
void tm_controls(p4_game_context_t *ctx,tm_state *s,const p4_game_input_t *in,uint32_t ms,bool calibrate){
 tm_intent *out=&s->intent[s->slot];*out=(tm_intent){0};
 p4_game_motion_t m;
 s->motion_live=p4_game_read_motion(ctx,&m);
 if(s->motion_live){
  int mag=root(m.accel_mg[0]*m.accel_mg[0]+m.accel_mg[1]*m.accel_mg[1]+m.accel_mg[2]*m.accel_mg[2]);
  if(mag<300){s->motion_live=false;s->filtered_x=s->filtered_y=0;}
  else {
   if(!s->calibrated||calibrate)calibrate_pose(s,&m);
   int32_t gravity[3];memcpy(gravity,m.accel_mg,sizeof(gravity));normalize(gravity);
   /* Complementary filter: gyros predict, accelerometer gravity removes drift.
    * Division before dot product bounds even a full-range 2000 dps sample. */
   int32_t gyro[3];for(int n=0;n<3;++n)gyro[n]=m.gyro_mdps[n]/100;
   int gx=dot(gyro,s->basis_y),gy=-dot(gyro,s->basis_x);
   s->filtered_x=tm_clamp(s->filtered_x+gx*(int)ms*16/573,-16000,16000);
   s->filtered_y=tm_clamp(s->filtered_y+gy*(int)ms*16/573,-16000,16000);
   /* Q4 retains fractional milligravity across short frames; correction
    * operands remain bounded to32000*65535, inside signed32-bit arithmetic. */
   const int blend=motion_blend[ms>100U?100U:ms];
   s->filtered_x+=(tm_clamp(dot(gravity,s->basis_x),-1000,1000)*16-s->filtered_x)*blend/65536;
   s->filtered_y+=(tm_clamp(dot(gravity,s->basis_y),-1000,1000)*16-s->filtered_y)*blend/65536;
   int tx=s->filtered_x/16,ty=s->filtered_y/16;
   for(unsigned n=0;n<s->orientation;++n){int swap=tx;tx=-ty;ty=swap;}
   out->x=(int16_t)tm_clamp(tx*2,-1000,1000);out->y=(int16_t)tm_clamp(ty*2,-1000,1000);
   out->spin=(int16_t)tm_clamp(dot(gyro,s->neutral),-1000,1000);
   if(m.sequence!=s->motion_seq)out->jolt=(int16_t)tm_clamp(mag-1000,-1000,1000);
   s->motion_seq=m.sequence;
  }
 }else{s->filtered_x=s->filtered_y=0;}
 if(abs_i(out->x)<35)out->x=0;if(abs_i(out->y)<35)out->y=0;
 /* Explicit normalized input takes precedence over tilt. */
 if(in->held&(P4_BUTTON_LEFT|P4_BUTTON_RIGHT|P4_BUTTON_UP|P4_BUTTON_DOWN)){
  out->x=(int16_t)(((in->held&P4_BUTTON_RIGHT)?600:0)-((in->held&P4_BUTTON_LEFT)?600:0));
  out->y=(int16_t)(((in->held&P4_BUTTON_DOWN)?600:0)-((in->held&P4_BUTTON_UP)?600:0));
 }
 out->brake=(in->held&P4_BUTTON_A)!=0;
 if(in->touch_valid&&in->touch_count){
  int x=in->touches[0].x,y=in->touches[0].y;
  int bx,by;
  if(tm_screen_to_board(x,y,&bx,&by)){
   out->x=(int16_t)tm_clamp((bx-s->ball[s->slot].x/TM_Q)*50,-1000,1000);
   out->y=(int16_t)tm_clamp((by-s->ball[s->slot].y/TM_Q)*50,-1000,1000);
  }
  if(x>=164&&x<215&&y>=178)out->brake=true;
 }
}

/* Rendering interpolates the last completed local physics step. The rules and
 * collision positions remain untouched. Teleports reset both endpoints. */
void tm_visual_ball(const tm_state *s,unsigned p,int32_t *x,int32_t *y){
 *x=s->ball[p].x;*y=s->ball[p].y;
 if(s->phase!=TM_PLAY)return;
 const unsigned period=s->linked&&!s->host?50U:TM_STEP;
 const unsigned fraction=s->linked&&!s->host?s->blend_ms:s->accumulator;
 *x=s->previous_x[p]+(*x-s->previous_x[p])*(int32_t)fraction/(int32_t)period;
 *y=s->previous_y[p]+(*y-s->previous_y[p])*(int32_t)fraction/(int32_t)period;
}
/* Inverse of the perspective board projection at the water plane; input stays
 * in canonical 320x200 coordinates regardless of the physical display. */
bool tm_screen_to_board(int sx,int sy,int *x,int *y){
 if(sy<20||sy>173||sx<0||sx>=320)return false;
 const int u=sx-156,v=sy-42;
 const int a=5300+u,b=600+10*u,c=450+v,d=3300+10*v;
 /* For bounded 320x200 input all products fit int32. The determinant
  * is exactly divisible by ten; reduce it before division on RV32. */
 const int r=600*u+4320,t=600*v+1200;
 const int det=(a*d-b*c)/10;
 *x=120+(r*d-b*t)/det;
 *y=(a*t-r*c)/det;
 return *x>=0&&*x<240&&*y>=0&&*y<144;
}
