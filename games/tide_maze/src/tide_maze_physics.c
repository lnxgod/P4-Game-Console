// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include <string.h>
/* Original hand-authored labyrinths: # stone, o pearl, ~ whirlpool, E dock. */
const char *const tm_maps[TM_LEVELS][TM_ROWS]={
 {"###############","#S............#","#.....#...o...#","#..o..#.......#","#.....#...#...#","#.........#o..#","#..#......#...#","#o~.........E.#","###############"},
 {"###############","#S......o.....#","#....###......#","#........#....#","#..##....#.~..#","#..o.....#....#","#.....##......#","#o....~.....E.#","###############"},
 {"###############","#S....~......o#","#...#....##...#","#...#o........#","#...#...#.....#","#o......#.~...#","#...##..#.....#","#.....o.....E.#","###############"}
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
 tm_water_reset(s);
 for(int y=0;y<TM_ROWS;++y)for(int x=0;x<TM_COLS;++x)
  if(tm_tile(s->level,x,y)=='o')++s->all_pearls;
 for(int p=0;p<2;++p){s->ball[p]=(tm_ball){.x=(24+p*10)*TM_Q,.y=24*TM_Q,.z=6.0f,.previous_z=6.0f};
  s->previous_x[p]=s->ball[p].x;s->previous_y[p]=s->ball[p].y;}
}
/* Circle/AABB contacts, not a square hit box. Resolve the closest wall
 * normal and remove inward velocity, leaving tangential rolling intact. */
static void collide(tm_state *s,tm_ball *b){
 const int r=4*TM_Q;
 for(int cy=(b->y-r)/(16*TM_Q);cy<=(b->y+r)/(16*TM_Q);++cy)
  for(int cx=(b->x-r)/(16*TM_Q);cx<=(b->x+r)/(16*TM_Q);++cx){
   if(tm_tile(s->level,cx,cy)!='#')continue;
   int near_x=tm_clamp(b->x,cx*16*TM_Q,(cx+1)*16*TM_Q);
   int near_y=tm_clamp(b->y,cy*16*TM_Q,(cy+1)*16*TM_Q);
   int dx=b->x-near_x,dy=b->y-near_y,d2=dx*dx+dy*dy;
   if(d2>=r*r)continue;
   if(d2==0){/* Invalid spawn/network pose: choose the nearest outer face. */
    int left=b->x-cx*16*TM_Q,right=(cx+1)*16*TM_Q-b->x;
    int top=b->y-cy*16*TM_Q,bottom=(cy+1)*16*TM_Q-b->y;
    int m=left;if(right<m)m=right;if(top<m)m=top;if(bottom<m)m=bottom;
    if(m==left)b->x=cx*16*TM_Q-r;else if(m==right)b->x=(cx+1)*16*TM_Q+r;
    else if(m==top)b->y=cy*16*TM_Q-r;else b->y=(cy+1)*16*TM_Q+r;
    b->vx=b->vy=0;continue;
   }
   float distance=(float)r;for(int n=0;n<6;++n)distance=0.5f*(distance+(float)d2/distance);
   float nx=(float)dx/distance,ny=(float)dy/distance;
   b->x+=(int32_t)(nx*((float)r-distance));b->y+=(int32_t)(ny*((float)r-distance));
   float inward=(float)b->vx*nx+(float)b->vy*ny;
   if(inward<0.0f){b->vx-=(int32_t)(1.22f*inward*nx);b->vy-=(int32_t)(1.22f*inward*ny);}
  }
 b->x=tm_clamp(b->x,20*TM_Q,220*TM_Q);b->y=tm_clamp(b->y,20*TM_Q,124*TM_Q);
}
extern void tm_ball_forces(tm_state *s,unsigned p);
static void marble_contact(tm_state *s){
 if(!s->linked||s->ball[0].rescue||s->ball[1].rescue)return;
 tm_ball *a=&s->ball[0],*b=&s->ball[1];
 float dx=(float)(b->x-a->x)/256.0f,dy=(float)(b->y-a->y)/256.0f,dz=b->z-a->z;
 float d2=dx*dx+dy*dy+dz*dz;if(d2>=64.0f)return;
 if(d2<0.001f){dx=0.1f;d2=0.01f;}
 float d=8.0f;for(int i=0;i<7;++i)d=0.5f*(d+d2/d);
 float nx=dx/d,ny=dy/d,nz=dz/d,penetration=(8.0f-d)*0.5f;
 a->x-=(int32_t)(nx*penetration*256.0f);a->y-=(int32_t)(ny*penetration*256.0f);a->z-=nz*penetration;
 b->x+=(int32_t)(nx*penetration*256.0f);b->y+=(int32_t)(ny*penetration*256.0f);b->z+=nz*penetration;
 float relative=(float)(b->vx-a->vx)*nx/5.12f+(float)(b->vy-a->vy)*ny/5.12f+(b->vz-a->vz)*nz;
 if(relative<0.0f){float impulse=-relative*0.625f;
  a->vx-=(int32_t)(impulse*nx*5.12f);a->vy-=(int32_t)(impulse*ny*5.12f);a->vz-=impulse*nz;
  b->vx+=(int32_t)(impulse*nx*5.12f);b->vy+=(int32_t)(impulse*ny*5.12f);b->vz+=impulse*nz;
 }
 for(int i=0;i<2;++i){tm_ball *ball=&s->ball[i];
  ball->vx=tm_clamp(ball->vx,-280,280);ball->vy=tm_clamp(ball->vy,-280,280);
  ball->z=p4_water_limit(ball->z,4.0f,22.0f);ball->vz=p4_water_limit(ball->vz,-60.0f,60.0f);collide(s,ball);
 }
}
void tm_simulate(tm_state *s){
 if(s->phase!=TM_PLAY)return;
 if(s->time_ms<=TM_STEP){s->time_ms=0;s->phase=TM_LOST;return;}
 s->time_ms-=TM_STEP;tm_fluid(s);
 const unsigned count=s->linked?2U:1U;
 s->docked=0;
 for(unsigned p=0;p<count;++p){
  tm_ball *b=&s->ball[p];
  s->previous_x[p]=b->x;s->previous_y[p]=b->y;
  if(b->rescue){b->rescue=(uint16_t)(b->rescue>TM_STEP?b->rescue-TM_STEP:0);continue;}
  tm_ball_forces(s,p);
  b->x+=b->vx;b->y+=b->vy;collide(s,b);
  unsigned pearl=0;
  for(int y=1;y<TM_ROWS-1;++y)for(int x=1;x<TM_COLS-1;++x){
   char t=tm_tile(s->level,x,y);int dx=b->x/TM_Q-(x*16+8),dy=b->y/TM_Q-(y*16+8);
   int d=dx*dx+dy*dy;
   if(t=='o'){if(d<64)s->pearls|=1U<<pearl;++pearl;}
   if(t=='E' && d<64)s->docked|=1U<<p;
   if(t=='~' && d<100){
    b->vx=tm_clamp(b->vx-dx*3,-280,280);b->vy=tm_clamp(b->vy-dy*3,-280,280);
    if(d<9){b->x=(24+(int)p*10)*TM_Q;b->y=24*TM_Q;b->vx=b->vy=0;b->rescue=700;b->z=b->previous_z=6.0f;b->vz=0.0f;
     s->previous_x[p]=b->x;s->previous_y[p]=b->y;
     ++s->rescues;s->time_ms=s->time_ms>3000?s->time_ms-3000:0;}
   }
  }
 }
 marble_contact(s);
 s->docked=0;
 for(unsigned p=0;p<count;++p)for(int y=1;y<TM_ROWS-1;++y)for(int x=1;x<TM_COLS-1;++x)if(tm_tile(s->level,x,y)=='E'){
  int dx=s->ball[p].x/TM_Q-(x*16+8),dy=s->ball[p].y/TM_Q-(y*16+8);
  if(dx*dx+dy*dy<64&&!s->ball[p].rescue)s->docked|=1U<<p;
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
  if(tm_screen_to_board(s,x,y,&bx,&by)){
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
