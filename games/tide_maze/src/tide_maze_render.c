// SPDX-License-Identifier: MIT
#include "tide_maze_internal.h"
#include "p4/presentation.h"
static uint16_t rgb(int r,int g,int b){return (uint16_t)(((unsigned)tm_clamp(r,0,255)>>3<<11)|((unsigned)tm_clamp(g,0,255)>>2<<5)|((unsigned)tm_clamp(b,0,255)>>3));}
static char *number(char *out,unsigned value){char a[10];unsigned n=0;do{a[n++]=(char)('0'+value%10U);value/=10U;}while(value&&n<10U);while(n)*out++=a[--n];*out=0;return out;}
static void rect(p4_game_surface_t *f,int x,int y,int w,int h,uint16_t c){int px=p4_ui_x(f,x),py=p4_ui_y(f,y);p4_draw_fill_rect(f,px,py,p4_ui_x(f,x+w)-px,p4_ui_y(f,y+h)-py,c);}
static void text(p4_game_surface_t *f,int x,int y,const char *t,uint16_t c,int size){p4_ui_text(f,p4_ui_x(f,x),p4_ui_y(f,y),t,c,(unsigned)p4_ui_y(f,size),60);}
static float absolute(float a){return a<0.0f?-a:a;}
/* Angles are bounded to +/-0.17 radians. Polynomial rotation has <1e-6
 * error here; no libm/runtime imports. Matrix includes board roll/pitch and
 * the fixed camera yaw/elevation; inverse picking uses its transpose. */
void tm_camera(const tm_state *s,float m[9]){
 float alpha=s->phase==TM_PLAY?(float)s->accumulator/20.0f:1.0f;
 float a=s->previous_view_x+(s->view_x-s->previous_view_x)*alpha,b=s->previous_view_y+(s->view_y-s->previous_view_y)*alpha,sa=a-a*a*a/6.0f,ca=1.0f-a*a*0.5f+a*a*a*a/24.0f;
 float sb=b-b*b*b/6.0f,cb=1.0f-b*b*0.5f+b*b*b*b/24.0f;
 /* Camera basis: right, down, toward camera. Orthonormal to float precision. */
 const float camera[9]={0.9928086f,0.1197122f,0.0f,-0.0861928f,0.7148222f,-0.6939741f,-0.0830772f,0.6889834f,0.72f};
 const float rotation[9]={ca,0.0f,sa,-sb*sa,cb,sb*ca,-cb*sa,-sb,cb*ca};
 for(int row=0;row<3;++row)for(int col=0;col<3;++col){m[row*3+col]=0.0f;for(int k=0;k<3;++k)m[row*3+col]+=camera[row*3+k]*rotation[k*3+col];}
}
static p4_3d_vertex_t project(const float m[9],int w,int h,float x,float y,float z,uint16_t color){
 x-=120.0f;y-=72.0f;
 float u=m[0]*x+m[1]*y+m[2]*z,v=m[3]*x+m[4]*y+m[5]*z;
 float depth=420.0f-m[6]*x-m[7]*y-m[8]*z;
 if(!(depth>100.0f))return (p4_3d_vertex_t){0};
 float scale=385.0f/depth;
 float sx=(160.0f+u*scale)*(float)w/320.0f,sy=(104.0f+v*scale)*(float)h/200.0f;
 if(!(sx>-2048.0f&&sx<2048.0f&&sy>-2048.0f&&sy<2048.0f))return (p4_3d_vertex_t){0};
 return (p4_3d_vertex_t){(int16_t)(sx*8.0f),(int16_t)(sy*8.0f),(uint16_t)(4000000.0f/depth),color,0,0};
}
p4_3d_vertex_t tm_project(const tm_state *s,int w,int h,float x,float y,float z,uint16_t color){float m[9];tm_camera(s,m);return project(m,w,h,x,y,z,color);}
bool tm_screen_to_board(const tm_state *s,int sx,int sy,int *x,int *y){
 if(sy<30||sy>=178||sx<0||sx>=320)return false;
 float m[9];tm_camera(s,m);float u=(float)(sx-160)/385.0f,v=(float)(sy-104)/385.0f;
 /* camera point (u*d,v*d,420-d), intersect with world z=6. */
 float denominator=m[2]*u+m[5]*v-m[8];if(absolute(denominator)<0.01f)return false;
 float d=(6.0f-420.0f*m[8])/denominator;
 *x=(int)(120.0f+m[0]*u*d+m[3]*v*d+m[6]*(420.0f-d)+0.5f);
 *y=(int)(72.0f+m[1]*u*d+m[4]*v*d+m[7]*(420.0f-d)+0.5f);
 return *x>=0&&*x<240&&*y>=0&&*y<144;
}
static void quad(p4_3d_band_t *band,const p4_3d_vertex_t *a,const p4_3d_vertex_t *b,const p4_3d_vertex_t *c,const p4_3d_vertex_t *d,uint16_t color){
 p4_3d_vertex_t v[4]={*a,*b,*c,*d};for(int n=0;n<4;++n)v[n].color=color;p4_3d_quad(band,v,false);
}
static void plane(p4_3d_band_t *band,const float m[9],float x,float y,float w,float h,float z,uint16_t color){
 p4_game_surface_t *f=band->surface;
 p4_3d_vertex_t v[4]={project(m,f->width,f->height,x,y,z,color),project(m,f->width,f->height,x+w,y,z,color),project(m,f->width,f->height,x+w,y+h,z,color),project(m,f->width,f->height,x,y+h,z,color)};
 p4_3d_quad(band,v,false);
}
static void box(p4_3d_band_t *band,const float m[9],float x,float y,float w,float h,float low,float high){
 p4_game_surface_t *f=band->surface;p4_3d_vertex_t v[12];
 for(int i=0;i<4;++i){float xx=x+((i==1||i==2)?w:0.0f),yy=y+(i>=2?h:0.0f);
  v[i]=project(m,f->width,f->height,xx,yy,low,0);
  v[i+4]=project(m,f->width,f->height,xx,yy,high-0.7f,0);
  v[i+8]=project(m,f->width,f->height,xx+((i==1||i==2)?-0.7f:0.7f),yy+(i>=2?-0.7f:0.7f),high,0);
 }
 for(int i=0;i<4;++i){int j=(i+1)%4;
  quad(band,&v[i],&v[j],&v[j+4],&v[i+4],i==2?rgb(74,101,116):rgb(113,143,153));
  quad(band,&v[i+4],&v[j+4],&v[j+8],&v[i+8],i==2?rgb(181,200,200):rgb(220,230,218));
 }
 quad(band,&v[8],&v[9],&v[10],&v[11],rgb(225,234,222));
}
static float height(const tm_state *s,int x,int y,float alpha){
 float sum=0.0f;int count=0;
 for(int dy=-1;dy<=0;++dy)for(int dx=-1;dx<=0;++dx){int xx=x+dx,yy=y+dy;
  if(xx<0||xx>=TM_W||yy<0||yy>=TM_H)continue;int i=yy*TM_W+xx;
  if(s->wet[i]){sum+=s->previous_water[i]*(1.0f-alpha)+(s->water[i]+s->body[i])*alpha;++count;}
 }
 return count?sum/(float)count:6.0f;
}
static void prepare_water(tm_state *s,const float m[9],int w,int h){
 float alpha=s->phase==TM_PLAY?(float)s->accumulator/20.0f:1.0f;
 /* Reuse work arrays only in simulation; rendering scratch has independent
  * lifetime, leaving all physical state untouched. */
 for(int y=0;y<=TM_H;++y)for(int x=0;x<=TM_W;++x){
  float z=height(s,x,y,alpha);
  float nx=(height(s,x-1,y,alpha)-height(s,x+1,y,alpha))*0.125f;
  float ny=(height(s,x,y-1,alpha)-height(s,x,y+1,alpha))*0.125f;
  float slope=nx*nx+ny*ny;
  /* A broad skylight and a small sun lobe reflect from SIMULATED normals.
   * No time-scrolling UVs or unforced cosmetic surface waves. */
  float light=p4_water_limit(0.58f+nx*0.4f-ny*0.6f,0.12f,1.1f);
  float spec=p4_water_limit(1.0f-((nx-0.2f)*(nx-0.2f)+(ny+0.28f)*(ny+0.28f))*5.0f,0.0f,1.0f);
  spec*=spec;spec*=spec;spec*=spec;
  float foam=p4_water_limit((slope-0.12f)*0.55f,0.0f,0.45f);
  int r=(int)(18.0f+light*35.0f+spec*180.0f+foam*200.0f);
  int g=(int)(102.0f+light*58.0f+spec*135.0f+foam*100.0f);
  int b=(int)(134.0f+light*64.0f+spec*85.0f+foam*60.0f);
  p4_3d_vertex_t *vertex=&s->water_vertices[y*(TM_W+1)+x];
  *vertex=project(m,w,h,(float)x*4.0f,(float)y*4.0f,z,rgb(r,g,b));
  /* Refract the submerged tiled bed through the local surface normal. The
   * depth/tag test preserves actual underwater balls, coins and wall faces. */
  vertex->u=(uint16_t)(p4_water_limit((float)x*4.0f-nx*z*2.5f,0.0f,255.0f)*256.0f);
  vertex->v=(uint16_t)(p4_water_limit((float)y*4.0f-ny*z*2.5f,0.0f,255.0f)*256.0f);
 }
}
static const float sine[17]={0.0f,0.3826834f,0.7071068f,0.9238795f,1.0f,0.9238795f,0.7071068f,0.3826834f,0.0f,-0.3826834f,-0.7071068f,-0.9238795f,-1.0f,-0.9238795f,-0.7071068f,-0.3826834f,0.0f};
static void prepare_sphere(tm_state *s,unsigned p,const float m[9],int w,int h){
 int32_t px,py;tm_visual_ball(s,p,&px,&py);float x=(float)px/256.0f,y=(float)py/256.0f;
 float a=s->phase==TM_PLAY?(s->linked&&!s->host?(float)s->blend_ms/50.0f:(float)s->accumulator/20.0f):1.0f;
 float z=s->ball[p].previous_z*(1.0f-a)+s->ball[p].z*a;
 /* Continuous table interpolation keeps the painted band from snapping by
  * one longitude segment when the marble crosses a position threshold. */
 float roll=(x+y)*0.1f;int step=(int)roll,phase=step%16;float fraction=roll-(float)step;
 float roll_sin=sine[phase]*(1.0f-fraction)+sine[phase+1]*fraction;
 float roll_cos=sine[(phase+4)%16]*(1.0f-fraction)+sine[(phase+5)%16]*fraction;
 for(int lat=0;lat<=8;++lat)for(int lon=0;lon<=16;++lon){
  float nz=sine[(lat+4)%16],ring=sine[lat],nx=ring*sine[(lon+4)%16],ny=ring*sine[lon];
  float light=p4_water_limit(0.4f-0.35f*nx-0.25f*ny+0.6f*nz,0.1f,1.0f);
  float spec=p4_water_limit(-0.25f*nx-0.45f*ny+0.86f*nz,0.0f,1.0f);spec*=spec;spec*=spec;spec*=spec;spec*=spec;
  /* Great-circle band makes rolling readable on the spherical geometry. */
  float stripe=nx*roll_sin+nz*roll_cos;
  float band=p4_water_limit((0.30f-absolute(stripe))*8.0f,0.0f,1.0f);
  float base_r=p?90.0f:245.0f,base_g=p?197.0f:155.0f,base_b=p?240.0f:48.0f;
  int r=(int)((base_r+(55.0f-base_r)*band)*light+spec*(150.0f+20.0f*band));
  int g=(int)((base_g+(72.0f-base_g)*band)*light+spec*(150.0f+20.0f*band));
  int b=(int)((base_b+(78.0f-base_b)*band)*light+spec*(150.0f+20.0f*band));
  s->sphere_vertices[p][lat*17+lon]=project(m,w,h,x+nx*4.0f,y+ny*4.0f,z+nz*4.0f,rgb(r,g,b));
 }
}
static void spheres(p4_3d_band_t *band,const tm_state *s){
 for(unsigned p=0;p<(s->linked?2U:1U);++p){if(s->ball[p].rescue&&(s->animation_ms/100U%2U))continue;
  for(int lat=0;lat<8;++lat)for(int lon=0;lon<16;++lon){int i=lat*17+lon;
   const p4_3d_vertex_t *v=s->sphere_vertices[p];
   p4_3d_triangle(band,v+i,v+i+1,v+i+18,false);p4_3d_triangle(band,v+i,v+i+18,v+i+17,false);
  }
 }
}
static void disc(p4_3d_band_t *band,const float m[9],float x,float y,float radius,float z,uint16_t color){
 p4_game_surface_t *f=band->surface;p4_3d_vertex_t center=project(m,f->width,f->height,x,y,z,color);
 for(int i=0;i<16;++i){p4_3d_vertex_t a=project(m,f->width,f->height,x+sine[i]*radius,y+sine[(i+4)%16]*radius,z,color),b=project(m,f->width,f->height,x+sine[i+1]*radius,y+sine[(i+5)%16]*radius,z,color);p4_3d_triangle(band,&center,&a,&b,false);}
}
static void geometry(p4_3d_band_t *band,const float m[9],const tm_state *s){
 band->material=1;
 /* Solid basin, visible grouted bed and beveled stone dividers. */
 box(band,m,-2.0f,-2.0f,244.0f,148.0f,-6.0f,0.0f);band->material=2;
 for(int y=0;y<TM_ROWS;++y)for(int x=0;x<TM_COLS;++x){
  if(tm_tile(s->level,x,y)=='#'){
   int end=x+1;while(end<TM_COLS&&tm_tile(s->level,end,y)=='#')++end;
   box(band,m,(float)(x*16),(float)(y*16),(float)((end-x)*16),16.0f,0.0f,18.0f);x=end-1;
  }else{
   band->material=1;
   plane(band,m,(float)(x*16)+0.35f,(float)(y*16)+0.35f,15.3f,15.3f,0.1f,((x+y)&1)?rgb(115,168,177):rgb(153,190,188));band->material=2;
  }
 }
 unsigned pearl=0;
 for(int y=1;y<TM_ROWS-1;++y)for(int x=1;x<TM_COLS-1;++x){char tile=tm_tile(s->level,x,y);float xx=(float)(x*16+8),yy=(float)(y*16+8);
  if(tile=='o'){
   if(!(s->pearls&(1U<<pearl))){disc(band,m,xx,yy,3.5f,0.2f,rgb(89,105,89));disc(band,m,xx,yy,2.8f,3.5f,rgb(248,206,110));disc(band,m,xx-0.7f,yy-0.7f,1.2f,3.6f,rgb(255,245,203));}++pearl;
  }else if(tile=='~'){
   disc(band,m,xx,yy,5.0f,0.3f,rgb(25,51,67));disc(band,m,xx,yy,3.0f,0.4f,rgb(6,17,29));
  }else if(tile=='E'){
   bool ready=s->pearls==((1U<<s->all_pearls)-1U);
   disc(band,m,xx,yy,6.3f,0.4f,ready?rgb(255,225,91):rgb(201,178,123));disc(band,m,xx,yy,4.5f,0.5f,rgb(51,104,107));
  }
 }
 for(unsigned p=0;p<(s->linked?2U:1U);++p){int32_t x,y;tm_visual_ball(s,p,&x,&y);disc(band,m,(float)x/256.0f+0.7f,(float)y/256.0f+0.8f,4.2f,0.2f,rgb(40,73,83));}
}
static void water_mesh(p4_3d_band_t *band,const tm_state *s){
 for(int y=0;y<TM_H;++y)for(int x=0;x<TM_W;++x){
  int i=y*TM_W+x;if(!s->wet[i]||s->water[i]<0.025f)continue;
  int j=y*(TM_W+1)+x;const p4_3d_vertex_t *v=s->water_vertices;
  int top=p4_3d_min(p4_3d_min(v[j].y,v[j+1].y),p4_3d_min(v[j+TM_W+1].y,v[j+TM_W+2].y));
  int bottom=p4_3d_max(p4_3d_max(v[j].y,v[j+1].y),p4_3d_max(v[j+TM_W+1].y,v[j+TM_W+2].y));
  if(bottom<band->top*8||top>=band->bottom*8)continue;
  p4_3d_triangle(band,v+j,v+j+1,v+j+TM_W+2,true);
  p4_3d_triangle(band,v+j,v+j+TM_W+2,v+j+TM_W+1,true);
 }
}
static const uint16_t floor_texture[1024]={
#include "generated/floor.inc"
};
static void scene(p4_game_surface_t *f,tm_state *s){
 for(int y=0;y<f->height;++y){int shade=y*12/(int)f->height;p4_draw_fill_rect(f,0,y,f->width,1,rgb(10+shade/3,20+shade/2,32+shade));}
 float m[9];tm_camera(s,m);prepare_water(s,m,f->width,f->height);
 for(unsigned p=0;p<(s->linked?2U:1U);++p)prepare_sphere(s,p,m,f->width,f->height);
 for(int row=0;row<f->height;row+=TM_BAND){p4_3d_band_t band;p4_3d_begin(&band,f,s->depth_band,row,TM_BAND);band.tags=s->material_band;memset(band.tags,0,(size_t)(band.bottom-band.top)*f->width);geometry(&band,m,s);spheres(&band,s);band.refraction_texture=floor_texture;water_mesh(&band,s);}
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
