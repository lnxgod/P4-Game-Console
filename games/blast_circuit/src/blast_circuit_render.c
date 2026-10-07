// SPDX-License-Identifier: MIT
#include "blast_circuit_internal.h"
#include "p4/draw.h"
#include "p4/input.h"
#include "generated/runner.inc"
#include "generated/greenhouse.inc"
#include "generated/effects.inc"
#include "generated/ember.inc"
#include "generated/volt.inc"
#include "generated/gilt.inc"
#include "generated/biomes.inc"
#include "generated/reinforced.inc"
#include "generated/ember_crate.inc"

enum { INK = 0x0864, PAPER = 0xffb7, MINT = 0x5f79, MUTED = 0x6c73,
       GOLD = 0xfe88, KEY = 0xf81f, BOARD_X = 146, BOARD_Y = 64, TILE = 28 };
static const uint16_t colors[4] = {MINT, 0xfbb3, 0x5d9f, GOLD};
static int sx(const p4_game_surface_t *f, int x)
{ return f->width == 768U ? x : x * (int)f->width / 768; }
static int sy(const p4_game_surface_t *f, int y)
{ return f->height == 480U ? y : y * (int)f->height / 480; }
static void box(p4_game_surface_t *f, int x, int y, int w, int h, uint16_t c)
{ p4_draw_fill_rect(f, sx(f,x), sy(f,y), sx(f,x+w)-sx(f,x), sy(f,y+h)-sy(f,y), c); }
static void circle(p4_game_surface_t *f, int x, int y, int r, uint16_t c)
{ p4_draw_fill_circle(f, sx(f,x), sy(f,y), sx(f,r), c); }
static void label(p4_game_surface_t *f, int x, int y, const char *t, uint16_t c, unsigned scale)
{ p4_draw_text(f, sx(f,x), sy(f,y), t, c, f->width > 320U ? scale : 1U, 40U); }
static unsigned length(const char *s) { unsigned n=0; while(s[n] && n<40U) ++n; return n; }
static void centered(p4_game_surface_t *f, int x, int y, const char *t, uint16_t c, unsigned scale)
{
    const unsigned z = f->width > 320U ? scale : 1U;
    const int width = (int)(length(t) * 6U * z);
    p4_draw_text(f, sx(f,x)-width/2, sy(f,y), t,c,z,40U);
}
static void sprite_blit(p4_game_surface_t *f, const uint16_t *data, int source_size,
                       int x, int y, int size, int dx, int dy, bool silhouette)
{
    const int w=sx(f,x+size)-sx(f,x),h=sy(f,y+size)-sy(f,y);
    const int left=sx(f,x)+dx,top=sy(f,y)+dy;
    if (w<=0 || h<=0) return;
    const int x0=left<0?-left:0,y0=top<0?-top:0;
    const int x1=left+w>(int)f->width?(int)f->width-left:w;
    const int y1=top+h>(int)f->height?(int)f->height-top:h;
    if(x0>=x1 || y0>=y1) return;
    // Exact nearest-neighbor stepping without a division for every pixel.
    const int step=source_size/w,remainder=source_size%w;
    const int first=x0*source_size/w,initial_error=x0*source_size%w;
    const int y_step=source_size/h,y_remainder=source_size%h;
    int source_y=y0*source_size/h,y_error=y0*source_size%h;
    for(int yy=y0;yy<y1;++yy) {
        const uint16_t *row=data+source_y*source_size;
        uint16_t *out=f->pixels+(size_t)(top+yy)*f->stride_pixels+(size_t)(left+x0);
        int source_x=first,error=initial_error;
        for(int xx=x0;xx<x1;++xx) {
            const uint16_t c=row[source_x];
            if(c!=KEY) *out=silhouette?INK:c;
            ++out;source_x+=step;error+=remainder;
            if(error>=w) {++source_x;error-=w;}
        }
        source_y+=y_step;y_error+=y_remainder;
        if(y_error>=h) {++source_y;y_error-=h;}
    }
}
static void sprite(p4_game_surface_t *f,const uint16_t *data,int source_size,
                   int x,int y,int size)
{ sprite_blit(f,data,source_size,x,y,size,0,0,false); }
static void outlined_sprite(p4_game_surface_t *f,const uint16_t *data,int source_size,
                            int x,int y,int size)
{
    // Separate moving foreground sprites from nearby materials at native size.
    const int border=f->width>320U?2:1;
    sprite_blit(f,data,source_size,x,y,size,-border,0,true);
    sprite_blit(f,data,source_size,x,y,size,border,0,true);
    sprite_blit(f,data,source_size,x,y,size,0,-border,true);
    sprite_blit(f,data,source_size,x,y,size,0,border,true);
    sprite(f,data,source_size,x,y,size);
}
static const uint16_t *character_art(unsigned player,unsigned frame)
{ return player==1U?bc_ember[frame]:player==2U?bc_volt[frame]:player==3U?bc_gilt[frame]:bc_runner[frame]; }
static void character(p4_game_surface_t *f,unsigned player,unsigned frame,int x,int y,int size)
{
    sprite(f,character_art(player,frame),player?32:48,x,y,size);
}
static const char *theme_name(unsigned theme)
{ return theme==1U?"COPPER FOUNDRY":theme==2U?"PRISM VAULT":"ORBITAL GREENHOUSE"; }
static const uint16_t *tile_art(unsigned theme,uint8_t tile,unsigned variation)
{
    if(tile==BC_WALL) return bc_greenhouse[7];
    if(tile==BC_CRATE) return bc_greenhouse[5];
    if(tile==BC_ARMOR || tile==BC_DAMAGED) return bc_reinforced[tile==BC_ARMOR?0U:1U];
    if(tile==BC_GLASS) return bc_biomes[13];
    if(tile==BC_FUEL) return bc_ember_crate[0];
    if(bc_pickup(tile)) return bc_greenhouse[12U+tile-BC_RANGE];
    return theme?bc_biomes[(theme==1U?0U:8U)+variation%4U]:bc_greenhouse[variation%4U];
}
static uint16_t fade(uint16_t c)
{ return (uint16_t)((c & 0xe79cU) >> 2U); }
static void dim(p4_game_surface_t *f)
{
    uint16_t *row=f->pixels;
    for(unsigned y=0;y<f->height;++y) {
        uint16_t *p=row;
        uint16_t *const end=row+f->width;
        while(p!=end) {*p=fade(*p);++p;}
        row+=f->stride_pixels;
    }
}
static void particle(bc_state_t *s, int x, int y, unsigned count, uint16_t color, uint32_t seed)
{
    for(unsigned k=0;k<count;++k) {
        unsigned selected=96U;
        for(unsigned i=0;i<96U;++i) if(!s->particles[i].life) {selected=i;break;}
        if(selected==96U) break;
        seed=seed*1664525U+1013904223U;
        const int vx=(int)((seed>>16U)%161U)-80;
        seed=seed*1664525U+1013904223U;
        const int vy=(int)((seed>>16U)%141U)-100;
        s->particles[selected]=(bc_particle_t){x*256,y*256,vx*256,vy*256,
            (uint16_t)(300U+(seed%350U)),color};
    }
}
void bc_effects(bc_state_t *s, const bc_world_t *before)
{
    if(before->round != s->world.round) {
        for(unsigned i=0;i<4U;++i) s->death_ms[i]=0U;
        s->banner_ms=0U;bc_cue(s,BC_S_COUNT);
        for(unsigned i=0;i<BC_CELLS;++i) s->debris_ms[i]=0U;
        return;
    }
    for(unsigned i=0;i<BC_CELLS;++i) {
        const int x=BOARD_X+(int)(i%BC_W)*TILE+TILE/2;
        const int y=BOARD_Y+(int)(i/BC_W)*TILE+TILE/2;
        if(bc_destructible(before->tile[i]) && s->world.tile[i]!=before->tile[i]) {
            bc_cue(s,before->tile[i]==BC_FUEL?BC_S_SIZZLE:before->tile[i]==BC_CRATE?BC_S_WOOD:before->tile[i]==BC_GLASS?BC_S_GLASS:BC_S_METAL);
            s->debris_ms[i]=before->tile[i]==BC_CRATE?400U:0U;
            const uint16_t material=before->tile[i]==BC_FUEL?0xfa44:before->tile[i]==BC_CRATE?0xe46a:before->tile[i]==BC_GLASS?0x77ff:0xad75;
            particle(s,x,y,before->tile[i]==BC_GLASS?12U:6U,material,s->world.tick+i);
        }
        if(s->world.fire[i]>before->fire[i]) {
            s->shake_ms=130U; s->events|=BC_EVENT_BLAST;
            particle(s,x,y,2U,GOLD,s->world.tick+i*17U);
        }
    }
    for(unsigned i=0;i<4U;++i) {
        const bc_player_t *p=&s->world.players[i];
        if(p->x!=p->from_x) s->face[i]=p->x>p->from_x?2U:1U;
        else if(p->y!=p->from_y) s->face[i]=p->y>p->from_y?0U:3U;
        if(before->players[i].alive && !p->alive) {
            s->death_ms[i]=1100U;bc_cue(s,i==0U?BC_S_DEATH:i==1U?BC_S_DEATH_EMBER:i==2U?BC_S_DEATH_VOLT:BC_S_DEATH_GILT);
            particle(s,BOARD_X+p->x*TILE+TILE/2,BOARD_Y+p->y*TILE+TILE/2,16U,colors[i],s->world.tick+i);
        }
        const uint8_t taken=before->tile[p->y*BC_W+p->x];
        if(p->alive && !s->world.fire[p->y*BC_W+p->x] && bc_pickup(taken) && s->world.tile[p->y*BC_W+p->x]==BC_FLOOR)
            bc_cue(s,taken==BC_RANGE?BC_S_RANGE:taken==BC_EXTRA?BC_S_EXTRA:BC_S_SPEED);
        if(before->players[i].range!=p->range) bc_cue(s,BC_S_RANGE);
        if(before->players[i].capacity!=p->capacity) bc_cue(s,BC_S_EXTRA);
        if(before->players[i].speed!=p->speed) bc_cue(s,BC_S_SPEED);
        if(i==s->local_slot && p->alive && (before->players[i].x!=p->x || before->players[i].y!=p->y)) bc_cue(s,s->world.theme==1U?BC_S_STEP_METAL:s->world.theme==2U?BC_S_STEP_GLASS:BC_S_STEP);
    }
    for(unsigned i=0;i<BC_BOMBS;++i)
        if(!before->bombs[i].fuse && s->world.bombs[i].fuse) s->events|=BC_EVENT_BOMB;
    if(before->phase!=BC_PLAY && s->world.phase==BC_PLAY) {s->banner_ms=600U;bc_cue(s,BC_S_GO);}
    if(s->world.phase==BC_READY && (before->timer+19U)/20U!=(s->world.timer+19U)/20U) bc_cue(s,BC_S_COUNT);
    if(before->time_left>600U && s->world.time_left<=600U) bc_cue(s,BC_S_WARNING);
    if((s->world.phase==BC_ROUND || s->world.phase==BC_MATCH) &&
        before->phase!=s->world.phase) bc_cue(s,s->world.winner==255U?BC_S_DRAW:s->world.phase==BC_MATCH?BC_S_CHAMPION:BC_S_ROUND);
}
void bc_visual_step(bc_state_t *s, uint32_t elapsed)
{
    s->visual_ms+=elapsed;
    s->shake_ms=s->shake_ms>elapsed?(uint16_t)(s->shake_ms-elapsed):0U;
    s->banner_ms=s->banner_ms>elapsed?(uint16_t)(s->banner_ms-elapsed):0U;
    for(unsigned i=0;i<4U;++i)
        s->death_ms[i]=s->death_ms[i]>elapsed?(uint16_t)(s->death_ms[i]-elapsed):0U;
    for(unsigned i=0;i<BC_CELLS;++i)
        s->debris_ms[i]=s->debris_ms[i]>elapsed?(uint16_t)(s->debris_ms[i]-elapsed):0U;
    for(unsigned i=0;i<96U;++i) {
        bc_particle_t *p=&s->particles[i];
        if(!p->life) continue;
        if(elapsed>=p->life) {p->life=0U;continue;}
        p->life=(uint16_t)(p->life-elapsed);
        p->x+=p->vx*(int32_t)elapsed/1000;
        p->y+=p->vy*(int32_t)elapsed/1000;
        p->vy+=80*(int32_t)elapsed;
    }
}
static void floor_swatch(p4_game_surface_t *f,int x,int y,int size,unsigned theme)
{
    // Walkable space stays quiet; only obstacles and hazards carry busy artwork.
    static const uint16_t floor[3]={0x1168,0x18e6,0x18e9};
    // Partition the same top/left grid edge and colored interior so every
    // pixel is written once. Logical edges preserve legacy scaling exactly.
    box(f,x,y,size,1,INK);
    box(f,x,y+1,1,size-1,INK);
    box(f,x+1,y+1,size-1,size-1,floor[theme<BC_THEMES?theme:0U]);
}
static void floor_tile(p4_game_surface_t *f,int x,int y,unsigned variation,unsigned theme)
{ (void)variation;floor_swatch(f,x,y,TILE,theme); }
static void flame(p4_game_surface_t *f,const bc_state_t *s,int x,int y,unsigned life,unsigned hash)
{
    const bc_world_t *w=&s->world;
    const bool horizontal=(hash%BC_W>0U && w->fire[hash-1U]) ||
                          (hash%BC_W<BC_W-1U && w->fire[hash+1U]);
    const bool vertical=(hash>=BC_W && w->fire[hash-BC_W]) ||
                        (hash<BC_CELLS-BC_W && w->fire[hash+BC_W]);
    const unsigned row=horizontal&&!vertical?1U:vertical&&!horizontal?2U:0U;
    const unsigned frame=w->time_left<=500U || life>BC_FIRE?(s->visual_ms/90U)%3U:(BC_FIRE-life)/2U;
    if(life>BC_FIRE) {
        box(f,x+3,y+TILE-5,TILE-6,3,0xfa44);
        box(f,x+5,y+TILE-5,(TILE-10)*(int)(life-BC_FIRE)/(BC_EMBER_FIRE-BC_FIRE),2,GOLD);
    }
    if(horizontal) box(f,x,y+12,TILE,4,life>2U?GOLD:0xcac4);
    if(vertical) box(f,x+12,y,4,TILE,life>2U?GOLD:0xcac4);
    sprite(f,bc_effect_atlas[row*4U+frame],32,x,y,TILE);
}
static void frame_band(p4_game_surface_t *f,int x,int y,int w,int h,
                       int inset,uint16_t color)
{
    // Interior pixels are completely covered by the next band or floor.
    // Disjoint rectangles preserve scaled edges and the original draw order.
    box(f,x,y,w,inset,color);
    box(f,x,y+h-inset,w,inset,color);
    box(f,x,y+inset,inset,h-2*inset,color);
    box(f,x+w-inset,y+inset,inset,h-2*inset,color);
}
static void background_sides(p4_game_surface_t *f,int left,int right,
                             int y,int height,bool panels)
{
    if(panels) {
        p4_draw_fill_rect(f,0,y,16,height,INK);
        p4_draw_fill_rect(f,120,y,left-120,height,INK);
        p4_draw_fill_rect(f,right,y,648-right,height,INK);
        p4_draw_fill_rect(f,752,y,16,height,INK);
    } else {
        p4_draw_fill_rect(f,0,y,left,height,INK);
        p4_draw_fill_rect(f,right,y,768-right,height,INK);
    }
}
static void background(p4_game_surface_t *f,const bc_state_t *s)
{
    if(f->width!=768U || f->height!=480U) {
        p4_draw_clear(f,INK);
        for(int y=0;y<480;y+=8) box(f,0,y,768,1,0x08a5);
        return;
    }
    int left=BOARD_X-8,top=BOARD_Y-8;
    if(s->shake_ms && s->world.phase!=BC_PAUSED) {
        left+=(int)((s->visual_ms/16U)%3U)-1;
        top+=(int)((s->visual_ms/23U)%3U)-1;
    }
    const int right=left+BC_W*TILE+16,bottom=top+BC_H*TILE+16;
    // The disjoint frame bands and every floor cell overwrite this entire
    // native board rectangle before any overlay reads the surface.
    p4_draw_fill_rect(f,0,0,768,top,INK);
    p4_draw_fill_rect(f,0,bottom,768,480-bottom,INK);
    const uint8_t phase=s->world.phase;
    const bool panels=phase!=BC_TITLE && phase!=BC_SELECT && phase!=BC_EDITOR &&
                      phase!=BC_EDITOR_MENU && phase!=BC_COPY;
    if(panels) {
        // These four opaque HUD panels also overwrite their entire rectangle.
        background_sides(f,left,right,top,86-top,false);
        background_sides(f,left,right,86,112,true);
        background_sides(f,left,right,198,22,false);
        background_sides(f,left,right,220,112,true);
        background_sides(f,left,right,332,bottom-332,false);
    } else background_sides(f,left,right,top,bottom-top,false);
    for(int y=0;y<480;y+=8) {
        if(y<top || y>=bottom) p4_draw_fill_rect(f,0,y,768,1,0x08a5);
        else if(panels && ((y>=86 && y<198) || (y>=220 && y<332))) {
            p4_draw_fill_rect(f,0,y,16,1,0x08a5);
            p4_draw_fill_rect(f,120,y,left-120,1,0x08a5);
            p4_draw_fill_rect(f,right,y,648-right,1,0x08a5);
            p4_draw_fill_rect(f,752,y,16,1,0x08a5);
        } else {
            p4_draw_fill_rect(f,0,y,left,1,0x08a5);
            p4_draw_fill_rect(f,right,y,768-right,1,0x08a5);
        }
    }
}
static void arena(p4_game_surface_t *f,const bc_state_t *s)
{
    const bc_world_t *w=&s->world;
    int ox=BOARD_X,oy=BOARD_Y;
    if(s->shake_ms && s->world.phase!=BC_PAUSED) {
        ox+=(int)((s->visual_ms/16U)%3U)-1;
        oy+=(int)((s->visual_ms/23U)%3U)-1;
    }
    frame_band(f,ox-8,oy-8,BC_W*TILE+16,BC_H*TILE+16,4,0x0148);
    frame_band(f,ox-4,oy-4,BC_W*TILE+8,BC_H*TILE+8,2,0x3b92);
    frame_band(f,ox-2,oy-2,BC_W*TILE+4,BC_H*TILE+4,2,0x0927);
    for(int y=0;y<BC_H;++y) for(int x=0;x<BC_W;++x) {
        const int c=y*BC_W+x,px=ox+x*TILE,py=oy+y*TILE;
        floor_tile(f,px,py,(unsigned)(x*3+y*7),w->theme);
        if(w->time_left<=600U && !bc_blocked(w,x,y)) {
            const int ring=1+(600-(int)w->time_left)/100;
            int edge=x<BC_W-1-x?x:BC_W-1-x;
            if(y<edge) edge=y;
            if(BC_H-1-y<edge) edge=BC_H-1-y;
            if(edge==ring) box(f,px+1,py+TILE-3,TILE-2,2,
                              (s->visual_ms/180U)%2U?0xfb89:0xa229);
        }
        if(w->tile[c]==BC_WALL) {
            sprite(f,tile_art(w->theme,BC_WALL,0U),32,px-1,py-3,TILE+2);
        } else if(bc_destructible(w->tile[c])) {
            box(f,px+2,py+TILE-6,TILE-2,6,0x0926);
            sprite(f,tile_art(w->theme,w->tile[c],0U),32,px,py-2,TILE);
        } else if(bc_pickup(w->tile[c]) && !w->fire[c]) {
            const int bob=(int)((s->visual_ms/180U+(unsigned)c)%4U);
            circle(f,px+TILE/2,py+TILE-6,9,0x126b);
            outlined_sprite(f,bc_greenhouse[12U+w->tile[c]-BC_RANGE],32,px+2,py-2+(bob<2?bob:3-bob),TILE-4);
        }
    }
    for(unsigned i=0;i<BC_BOMBS;++i) {
        const bc_bomb_t *b=&w->bombs[i]; if(!b->fuse) continue;
        const int x=ox+b->x*TILE,y=oy+b->y*TILE;
        const unsigned frame=(BC_FUSE-b->fuse)/(b->fuse<14U?2U:5U)%4U;
        circle(f,x+TILE/2,y+TILE-6,10,0x0926);
        outlined_sprite(f,bc_greenhouse[8U+frame],32,x-3,y-7,TILE+6);
        box(f,x+8,y+TILE-3,12,2,colors[b->owner]);
    }
    for(unsigned i=0;i<BC_CELLS;++i)
        if(w->fire[i]) flame(f,s,ox+(int)(i%BC_W)*TILE,oy+(int)(i/BC_W)*TILE,w->fire[i],i);
    for(unsigned i=0;i<BC_CELLS;++i) {
        if(s->debris_ms[i]) {
            const unsigned frame=(400U-s->debris_ms[i])/100U;
            sprite(f,bc_effect_atlas[12U+frame],32,ox+(int)(i%BC_W)*TILE,
                   oy+(int)(i/BC_W)*TILE,TILE);
        }
    }
    for(unsigned i=0;i<4U;++i) {
        const bc_player_t *p=&w->players[i];
        if(!p->alive && !s->death_ms[i]) continue;
        if(!p->alive && (s->visual_ms/70U)%2U) continue;
        int x=ox+p->x*TILE,y=oy+p->y*TILE;
        const unsigned duration=p->speed?100U:150U;
        if(p->cooldown && p->alive) {
            unsigned remaining=p->cooldown*50U;
            remaining=remaining>s->accumulator?remaining-s->accumulator:0U;
            if(remaining>duration) remaining=duration;
            x+=((int)p->from_x-p->x)*TILE*(int)remaining/(int)duration;
            y+=((int)p->from_y-p->y)*TILE*(int)remaining/(int)duration;
        }
        if(!p->alive) y-=(1100-(int)s->death_ms[i])/25;
        circle(f,x+TILE/2,y+TILE-6,10,0x0926);
        box(f,x+7,y+TILE-5,14,3,colors[i]);
        const unsigned frame=p->cooldown?(s->visual_ms/70U)%4U:1U;
        outlined_sprite(f,character_art(i,s->face[i]*4U+frame),i?32:48,x-8,y-18,TILE+16);
        if(i==s->local_slot && p->alive) {
            box(f,x+11,y-17,6,2,PAPER); box(f,x+13,y-15,2,2,PAPER);
        }
    }
    for(unsigned i=0;i<96U;++i) {
        const bc_particle_t *p=&s->particles[i];
        if(p->life) box(f,p->x/256,p->y/256,p->life>150U?3:2,3,p->color);
    }
}
static void number2(char out[3],unsigned v)
{ out[0]=(char)('0'+(v/10U)%10U);out[1]=(char)('0'+v%10U);out[2]='\0'; }
static void hud(p4_game_surface_t *f,const bc_state_t *s)
{
    const bc_world_t *w=&s->world;
    label(f,24,20,"BACK",MUTED,1U);
    centered(f,354,20,w->time_left<=600U?"CORE OVERLOAD":theme_name(w->theme),w->time_left<=600U?GOLD:MINT,2U);
    const unsigned seconds=(w->time_left+19U)/20U;
    char time[6]={(char)('0'+seconds/60U),':',(char)('0'+seconds%60U/10U),(char)('0'+seconds%10U),'\0','\0'};
    label(f,544,20,time,seconds<=20U?0xfb69:PAPER,2U);
    box(f,24,44,720,1,0x228e);
    label(f,679,20,s->testing?"EDITOR":"PAUSE",MUTED,1U);
    for(unsigned i=0;i<4U;++i) {
        const int x=(i==0U || i==3U)?16:648;
        const int y=(i==0U || i==2U)?86:220;
        const bc_player_t *p=&w->players[i];
        box(f,x,y,104,112,0x10e7);
        box(f,x,y,3,112,p->alive?colors[i]:MUTED);
        const char title[3]={'P',(char)('1'+i),'\0'};
        label(f,x+12,y+9,title,colors[i],2U);
        label(f,x+48,y+11,i>=s->humans?"CPU":i==s->local_slot?"YOU":"LINK",MUTED,1U);
        const unsigned frame=p->alive?(s->visual_ms/220U+i)%4U:1U;
        character(f,i,frame,x+20,y+24,64);
        if(!p->alive) { box(f,x+10,y+53,84,20,INK);centered(f,x+52,y+59,"OUT",MUTED,1U); }
        for(unsigned k=0;k<BC_WIN_SCORE;++k)
            circle(f,x+32+(int)k*20,y+100,5,k<w->score[i]?colors[i]:0x29ab);
        if(i==s->local_slot) {
            char stats[15]={'B','O','M','B','S',' ',(char)('0'+p->capacity),' ',
                            'F','I','R','E',' ',(char)('0'+p->range),'\0'};
            char compact[6]={'B',(char)('0'+p->capacity),' ',
                             'F',(char)('0'+p->range),'\0'};
            label(f,x+6,y+114,f->width>320U?stats:compact,PAPER,1U);
        }
    }
    char round[3]; number2(round,w->round);
    char round_label[9]={'R','O','U','N','D',' ',round[0],round[1],'\0'};
    label(f,180,437,round_label,MUTED,1U);
    centered(f,384,447,!s->network && !w->players[0].alive ?
        (f->width>320U?"HOLD A TO FAST-FORWARD":"A SPEED UP") :
        (f->width>320U?"FIRST TO 3 WINS":"FIRST TO 3"),MUTED,1U);
    if(s->touch_seen && w->phase!=BC_TITLE) {
        static const int points[6][2]={{110,348},{110,449},{50,409},{170,409},{686,379},{576,439}};
        static const char *names[6]={"U","D","L","R","A","B"};
        for(unsigned i=0;i<6U;++i) {
            const int radius=i==4U?26:14;
            const uint16_t c=(s->held&(1U<<i))?MINT:0x3b50;
            circle(f,points[i][0],points[i][1],radius,c);
            circle(f,points[i][0],points[i][1],radius-2,INK);
            centered(f,points[i][0],points[i][1]-5,names[i],c,1U);
        }
    }
    else if(w->phase!=BC_TITLE) {
        label(f,24,414,"MOVE",MUTED,1U); label(f,24,433,"D-PAD",PAPER,1U);
        label(f,650,410,"A BOMB",PAPER,1U); label(f,650,434,"START PAUSE",MUTED,1U);
    }
}
static void title(p4_game_surface_t *f,const bc_state_t *s)
{
    dim(f);
    for(int i=0;i<8;++i) {
        const int y=65+i*46;
        box(f,40,y,688,1,0x0947);
    }
    label(f,66,60,"P4 ORIGINALS / 01",MINT,2U);
    character(f,0U,(s->visual_ms/170U)%4U,36,110,256);
    if(f->width>320U) {
        label(f,322,130,"BLAST",0x0149,8U);
        label(f,318,125,"BLAST",PAPER,8U);
        label(f,320,195,"CIRCUIT",MINT,6U);
    } else {
        centered(f,474,128,"BLAST",PAPER,1U);
        centered(f,474,160,"CIRCUIT",MINT,1U);
    }
    label(f,322,255,"FOUR ENTER. ONE SURVIVES.",PAPER,2U);
    label(f,322,290,"BREAK CRATES. CHAIN BLASTS.",MUTED,1U);
    label(f,322,310,"CLAIM POWER. WIN THE CIRCUIT.",MUTED,1U);
    box(f,314,352,358,48,MINT);
    centered(f,493,367,"A / START  -  ARENAS",INK,2U);
    centered(f,384,428,"3 ARENAS / 4 RUNNERS / LEVEL EDITOR",MUTED,1U);
    centered(f,384,454,s->audio.muted?"B  SOUND OFF   |   BACK  EXIT":"B  SOUND ON   |   BACK  EXIT",MUTED,1U);
}
static void overlay(p4_game_surface_t *f,const bc_state_t *s)
{
    const bc_world_t *w=&s->world;
    if(w->phase==BC_READY || s->banner_ms) {
        box(f,290,209,188,74,INK);
        if(s->banner_ms) centered(f,384,230,"GO!",MINT,5U);
        else {
            const char count[2]={(char)('1'+(w->timer?w->timer-1U:0U)/20U),'\0'};
            centered(f,384,219,count,PAPER,5U);
            centered(f,384,262,"GET READY",MINT,1U);
        }
    } else if(w->phase==BC_ROUND || w->phase==BC_MATCH || w->phase==BC_PAUSED ||
              w->phase==BC_WAIT || w->phase==BC_LOST || w->phase==BC_TRANSFER) {
        dim(f);
        box(f,164,146,440,185,INK); box(f,164,146,440,3,MINT);
        if(w->phase==BC_PAUSED) {
            centered(f,384,180,"TIME OUT",PAPER,4U);
            centered(f,384,237,"START / A  RESUME",MINT,2U);
            centered(f,384,281,s->audio.muted?"B SOUND OFF / BACK EXIT":"B SOUND ON / BACK EXIT",MUTED,1U);
        } else if(w->phase==BC_WAIT || w->phase==BC_LOST || w->phase==BC_TRANSFER) {
            const char *heading=w->phase==BC_TRANSFER?"SHARING ARENA":w->phase==BC_WAIT?"HOST PREPARING":"LINK ENDED";
            centered(f,384,184,heading,PAPER,3U);
            centered(f,384,244,w->phase==BC_LOST?"A SOLO BATTLE / BACK EXIT":
                s->level_ready?"LEVEL RECEIVED / WAITING FOR ALL":"HOST IS CHOOSING OR EDITING",MINT,1U);
            if(w->phase==BC_TRANSFER) for(unsigned i=0;i<s->humans;++i) {
                const int x=324+(int)i*40;
                circle(f,x,289,9,(s->level_ack&(1U<<i))?colors[i]:MUTED);
                const char n[2]={(char)('1'+i),'\0'};centered(f,x,285,n,INK,1U);
            }
        } else {
            if(w->winner<4U) {
                sprite(f,bc_greenhouse[15],32,204,170,76);
                char win[9]={'P',(char)('1'+w->winner),' ','W','I','N','S','!','\0'};
                centered(f,417,190,win,colors[w->winner],4U);
            } else centered(f,384,188,"DOUBLE KO",GOLD,4U);
            centered(f,384,253,w->phase==BC_MATCH?"CIRCUIT CHAMPION":"NEXT ROUND IN A MOMENT",PAPER,2U);
            if(w->phase==BC_MATCH) centered(f,384,292,
                s->network&&!s->host?"WAITING FOR HOST REMATCH":"A REMATCH / START ARENAS",MINT,1U);
        }
    }
}
static void button(p4_game_surface_t *f,int x,int y,int w,int h,const char *text,bool selected)
{
    box(f,x,y,w,h,selected?MINT:0x194b);
    box(f,x,y,w,2,selected?PAPER:0x3b92);
    centered(f,x+w/2,y+h/2-7,text,selected?INK:PAPER,2U);
}
static void outline(p4_game_surface_t *f,int x,int y,int size,uint16_t color)
{
    box(f,x,y,size,2,color);box(f,x,y+size-2,size,2,color);
    box(f,x,y,2,size,color);box(f,x+size-2,y,2,size,color);
}
static void authoring(p4_game_surface_t *f,const bc_state_t *s)
{
    const bool edit=s->world.phase==BC_EDITOR || s->world.phase==BC_EDITOR_MENU;
    label(f,24,20,"BACK",MUTED,1U);
    char custom[10]={'C','U','S','T','O','M',' ',(char)('1'+s->custom_slot),'\0','\0'};
    centered(f,384,16,edit?"ARENA WORKSHOP":s->selected<BC_THEMES?theme_name(s->level.theme):custom,PAPER,2U);
    centered(f,384,39,edit?theme_name(s->level.theme):s->network?"HOST CHOOSES / EVERYONE GETS THE LEVEL":"SOLO WITH THREE CPU RIVALS",MINT,1U);
    if(!edit) {
        button(f,24,164,96,112,"<",false);button(f,648,164,96,112,">",false);
        button(f,24,332,96,70,"EDIT",false);button(f,648,332,96,70,"PLAY",true);
        centered(f,72,414,"B",MINT,1U);centered(f,696,414,"A",MINT,1U);
        char position[4]={(char)('1'+s->selected),'/', '6','\0'};
        centered(f,72,295,position,MUTED,1U);
        centered(f,384,443,"WOOD/GLASS 1 HIT | ARMOR 2 | EMBER +1S",PAPER,1U);
        centered(f,384,465,"LEFT / RIGHT ARENA   |   START TITLE",MUTED,1U);
        if(s->world.phase==BC_COPY) {
            dim(f);box(f,176,88,416,256,INK);box(f,176,88,416,3,MINT);
            centered(f,384,106,"COPY TEMPLATE TO",PAPER,2U);
            const char *items[4]={"REPLACE CUSTOM 1","REPLACE CUSTOM 2","REPLACE CUSTOM 3","CANCEL"};
            for(unsigned i=0;i<4U;++i) button(f,186,148+(int)i*40,396,36,items[i],s->editor_menu==i);
            centered(f,384,320,"A CHOOSE / B CANCEL",MUTED,1U);
        }
    } else {
        static const char *names[10]={"FLOOR","WALL","WOOD","RANGE","BOMB","SPEED","ARMOR","CRACKED","GLASS","EMBER"};
        centered(f,72,83,"BRUSH",MUTED,1U);
        if(s->brush==BC_FLOOR) floor_swatch(f,36,113,72,s->level.theme);
        else sprite(f,tile_art(s->level.theme,s->brush,0U),32,36,113,72);
        centered(f,72,212,names[s->brush],PAPER,1U);
        if(bc_destructible(s->brush) || s->brush==BC_WALL)
            centered(f,72,229,s->brush==BC_WALL?"PERMANENT":
                s->brush==BC_ARMOR?"2 HITS":s->brush==BC_FUEL?"BURNS +1 SEC":"1 HIT",GOLD,1U);
        centered(f,72,248,"B NEXT",MINT,1U);
        centered(f,72,304,"A PAINT",MUTED,1U);
        centered(f,72,345,custom,MUTED,1U);
        centered(f,72,373,s->dirty?"UNSAVED":"SAVED",s->dirty?GOLD:MINT,1U);
        button(f,642,96,108,65,"MIRROR",s->mirror);
        centered(f,696,171,s->mirror?"ON":"OFF",MUTED,1U);
        button(f,642,199,108,57,"UNDO",false);
        button(f,642,330,108,69,"TOOLS",true);
        centered(f,696,415,"START",MUTED,1U);
        const int xs[2]={s->cursor_x,BC_W-1-s->cursor_x},ys[2]={s->cursor_y,BC_H-1-s->cursor_y};
        for(unsigned j=0;j<(s->mirror?2U:1U);++j) for(unsigned i=0;i<(s->mirror?2U:1U);++i)
            outline(f,BOARD_X+xs[i]*TILE,BOARD_Y+ys[j]*TILE,TILE,(i||j)?MINT:GOLD);
        centered(f,384,441,"D-PAD MOVE / A PAINT / B BRUSH",MUTED,1U);
        const char *hint=s->level_error==BC_LEVEL_OK?"FAIR MAP / 3-5 BLOCKS BETWEEN STARTS":
            s->level_error==BC_LEVEL_ASYMMETRIC?"MIRROR BOTH AXES BEFORE PLAY":
            s->level_error==BC_LEVEL_SEPARATION?"KEEP 3-5 BLOCKS BETWEEN EVERY START":
            "CONNECT ALL OPEN AREAS BEFORE PLAY";
        centered(f,384,465,hint,s->level_error==BC_LEVEL_OK?MINT:GOLD,1U);
        if(s->world.phase==BC_EDITOR_MENU) {
            dim(f);box(f,176,74,416,326,INK);box(f,176,74,416,3,MINT);
            centered(f,384,89,"WORKSHOP TOOLS",PAPER,2U);
            const char *items[7]={s->network?"PLAY WITH EVERYONE":"TEST ARENA","SAVE LEVELS","CHANGE THEME",
                s->mirror?"MIRROR ON":"MIRROR OFF","UNDO / REDO","CLEAR TO FLOOR","RETURN TO ARENAS"};
            for(unsigned i=0;i<7U;++i) button(f,186,112+(int)i*40,396,36,items[i],s->editor_menu==i);
            centered(f,384,405,"A CHOOSE / B RETURN",MINT,1U);
        }
    }
}
static void notification(p4_game_surface_t *f,const bc_state_t *s)
{
    if(!s->notice_ms) return;
    static const char *messages[12]={"","SPAWN LANES AND EDGES ARE PROTECTED","CONNECT ALL OPEN AREAS BEFORE PLAY",
        "LEVELS SAVED","SAVED / NEW EDITS STILL UNSAVED","SAVING LEVELS...","SAVE FAILED / EDITS STILL IN MEMORY",
        "SESSION ONLY / SAVE UNAVAILABLE","UNDO / REDO","SAVE INVALID / TEMPLATES RESTORED",
        "MIRROR BOTH AXES BEFORE PLAY","KEEP 3-5 BLOCKS BETWEEN EVERY START"};
    box(f,90,438,588,39,INK);box(f,90,438,588,2,s->notice==3U?MINT:GOLD);
    centered(f,384,452,messages[s->notice<12U?s->notice:0U],s->notice==3U?MINT:GOLD,1U);
}
bool bc_render(p4_game_context_t *context,p4_game_surface_t *f)
{
    if(!p4_surface_valid(f)) return false;
    const bc_state_t *s=context->state;
    background(f,s);
    arena(f,s);
    if(s->world.phase==BC_TITLE) title(f,s);
    else if(s->world.phase==BC_SELECT || s->world.phase==BC_EDITOR || s->world.phase==BC_EDITOR_MENU || s->world.phase==BC_COPY) authoring(f,s);
    else {hud(f,s);overlay(f,s);}
    notification(f,s);
    return true;
}
