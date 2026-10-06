// SPDX-License-Identifier: MIT
// Bounded arena authoring, validation and OS-owned optimistic saves.
#include "blast_circuit_internal.h"
#include <string.h>

bool bc_border(int x,int y)
{ return x<=0 || y<=0 || x>=BC_W-1 || y>=BC_H-1; }
bool bc_destructible(uint8_t t)
{ return t==BC_CRATE || t==BC_ARMOR || t==BC_DAMAGED || t==BC_GLASS || t==BC_FUEL; }
bool bc_pickup(uint8_t t) { return t>=BC_RANGE && t<=BC_SPEED; }
bool bc_solid(uint8_t t) { return t==BC_WALL || bc_destructible(t); }
bool bc_blocked(const bc_world_t *w,int x,int y)
{ return bc_border(x,y) || w->tile[y*BC_W+x]==BC_WALL; }
bool bc_spawn_safe(int x,int y)
{
    if(bc_border(x,y)) return false;
    const int mx=x<BC_W/2?x:BC_W-1-x, my=y<BC_H/2?y:BC_H-1-y;
    return mx<=3 && my<=3 && !(mx==2 && my==2);
}
void bc_level_preset(bc_level_t *l,unsigned theme,uint32_t seed)
{
    theme%=BC_THEMES;l->theme=(uint8_t)theme;
    for(int y=0;y<BC_H;++y) for(int x=0;x<BC_W;++x) {
        const int mx=x<BC_W/2?x:BC_W-1-x, my=y<BC_H/2?y:BC_H-1-y;
        bool wall=bc_wall(x,y);
        if(theme==1U && x>=6 && x<=10 && y>=4 && y<=8) wall=x==8 && y==6;
        if(theme==2U && !bc_border(x,y))
            wall=(x%4==0 && y%2==0) || ((y==4 || y==8) && (x==6 || x==10));
        // Two complete breakable rings surround each corner. Their overlap on
        // the middle row gives shortest routes of 3 (vertical) or 4 blocks.
        // The connected pillar lanes give room to retreat between rings.
        const int radius=(mx>my?mx:my)-1;
        const uint32_t n=seed^(uint32_t)(mx*83492791)^(uint32_t)(my*19349663);
        const unsigned value=(n^(n>>13U))%100U;
        uint8_t tile=BC_FLOOR;
        if(radius==3 || radius==5) {
            tile=value<22U?BC_ARMOR:value<38U?BC_FUEL:
                theme==2U && value<76U?BC_GLASS:BC_CRATE;
            if(mx==4 && my==1) tile=BC_CRATE;
            if(mx==1 && my==4) tile=BC_FUEL;
            if(mx==4 && my==3) tile=BC_ARMOR;
        }
        l->tile[y*BC_W+x]=wall?BC_WALL:bc_spawn_safe(x,y)?BC_FLOOR:tile;
    }
}
bc_level_error_t bc_level_validate(const bc_level_t *l)
{
    if(l->theme>=BC_THEMES) return BC_LEVEL_BAD_TILE;
    for(int y=0;y<BC_H;++y) for(int x=0;x<BC_W;++x) {
        const uint8_t t=l->tile[y*BC_W+x];
        if(t>BC_FUEL || (bc_border(x,y) && t!=BC_WALL)) return BC_LEVEL_BAD_TILE;
        if(bc_spawn_safe(x,y) && t!=BC_FLOOR) return BC_LEVEL_SPAWN;
    }
    // Crates can be cleared. Every usable cell must eventually connect to P1.
    uint16_t queue[BC_CELLS];bool seen[BC_CELLS]={false};
    unsigned head=0U,tail=1U;queue[0]=BC_W+1;seen[BC_W+1]=true;
    static const int offsets[4]={-BC_W,BC_W,-1,1};
    while(head<tail) {
        const int c=queue[head++];
        for(unsigned d=0;d<4U;++d) {
            const int n=c+offsets[d];
            if(n<0 || n>=BC_CELLS || seen[n] || l->tile[n]==BC_WALL) continue;
            seen[n]=true;queue[tail++]=(uint16_t)n;
        }
    }
    for(unsigned i=0;i<BC_CELLS;++i)
        if(l->tile[i]!=BC_WALL && !seen[i]) return BC_LEVEL_DISCONNECTED;
    return BC_LEVEL_OK;
}
unsigned bc_level_route_blocks(const bc_level_t *l,unsigned from,unsigned to)
{
    static const uint16_t starts[4]={BC_W+1,(BC_H-2)*BC_W+BC_W-2,2*BC_W-2,(BC_H-2)*BC_W+1};
    if(from>=BC_PLAYERS || to>=BC_PLAYERS) return BC_CELLS;
    uint16_t distance[BC_CELLS];bool visited[BC_CELLS]={false};
    for(unsigned i=0;i<BC_CELLS;++i) distance[i]=BC_CELLS;
    distance[starts[from]]=0U;
    // Fixed-size Dijkstra: count breakable blocks, not hits; walls are impassable.
    for(unsigned step=0;step<BC_CELLS;++step) {
        unsigned at=BC_CELLS,best=BC_CELLS;
        for(unsigned i=0;i<BC_CELLS;++i)
            if(!visited[i] && distance[i]<best) {at=i;best=distance[i];}
        if(at==BC_CELLS || at==starts[to]) return best;
        visited[at]=true;
        const int offsets[4]={-BC_W,BC_W,-1,1};
        for(unsigned d=0;d<4U;++d) {
            const int next=(int)at+offsets[d];
            if(next<0 || next>=BC_CELLS || bc_border(next%BC_W,next/BC_W) || l->tile[next]==BC_WALL) continue;
            const unsigned cost=best+(bc_destructible(l->tile[next])?1U:0U);
            if(cost<distance[next]) distance[next]=(uint16_t)cost;
        }
    }
    return BC_CELLS;
}
bc_level_error_t bc_level_match_validate(const bc_level_t *l)
{
    const bc_level_error_t structural=bc_level_validate(l);
    if(structural!=BC_LEVEL_OK) return structural;
    for(unsigned y=0;y<BC_H;++y) for(unsigned x=0;x<BC_W;++x)
        if(l->tile[y*BC_W+x]!=l->tile[y*BC_W+BC_W-1U-x] ||
           l->tile[y*BC_W+x]!=l->tile[(BC_H-1U-y)*BC_W+x]) return BC_LEVEL_ASYMMETRIC;
    for(unsigned from=0;from<BC_PLAYERS;++from) for(unsigned to=from+1U;to<BC_PLAYERS;++to) {
        const unsigned count=bc_level_route_blocks(l,from,to);
        if(count<3U || count>5U) return BC_LEVEL_SEPARATION;
    }
    return BC_LEVEL_OK;
}
void bc_level_apply(bc_world_t *w,const bc_level_t *l)
{ memcpy(w->tile,l->tile,BC_CELLS);w->theme=l->theme; }
uint32_t bc_hash(const uint8_t *bytes,unsigned count)
{
    uint32_t crc=UINT32_MAX;
    for(unsigned i=0;i<count;++i) {
        crc^=bytes[i];
        for(unsigned bit=0;bit<8U;++bit) crc=(crc>>1U)^((crc&1U)?UINT32_C(0xedb88320):0U);
    }
    return ~crc;
}
void bc_level_encode(const bc_level_t *l,uint8_t out[BC_LEVEL_BYTES])
{ out[0]=l->theme;memcpy(out+1,l->tile,BC_CELLS); }
bool bc_level_decode(bc_level_t *l,const uint8_t in[BC_LEVEL_BYTES])
{
    bc_level_t next;next.theme=in[0];memcpy(next.tile,in+1,BC_CELLS);
    if(bc_level_validate(&next)!=BC_LEVEL_OK) return false;
    *l=next;return true;
}
static void notice(bc_state_t *s,unsigned code)
{ s->notice=(uint8_t)code;s->notice_ms=3500U; }
static void save_encode(const bc_state_t *s,uint8_t out[BC_SAVE_BYTES])
{
    out[0]='B';out[1]='C';out[2]='L';out[3]=1U;out[4]=s->custom_slot;
    for(unsigned i=0;i<BC_CUSTOM;++i) bc_level_encode(&s->custom[i],out+5U+i*BC_LEVEL_BYTES);
    const uint32_t crc=bc_hash(out,BC_SAVE_BYTES-4U);
    for(unsigned i=0;i<4U;++i) out[BC_SAVE_BYTES-4U+i]=(uint8_t)(crc>>(i*8U));
}
void bc_levels_load(p4_game_context_t *c,bc_state_t *s)
{
    for(unsigned i=0;i<BC_CUSTOM;++i) bc_level_preset(&s->custom[i],i,0x421U+i*127U);
    s->dirty=true;s->mirror=true;s->cursor_x=7U;s->cursor_y=5U;s->brush=BC_CRATE;s->last_painted=-1;
    if(!c->services) return;
    s->save_sequence=c->services->save_sequence;
    if(!c->services->save_bytes) return;
    const uint8_t *p=c->services->save_data;
    if(!p || c->services->save_schema_version!=1U || c->services->save_bytes!=BC_SAVE_BYTES ||
       p[0]!='B' || p[1]!='C' || p[2]!='L' || p[3]!=1U || p[4]>=BC_CUSTOM) {notice(s,9U);return;}
    uint32_t crc=0U;
    for(unsigned i=0;i<4U;++i) crc|=(uint32_t)p[BC_SAVE_BYTES-4U+i]<<(i*8U);
    bc_level_t levels[BC_CUSTOM];
    if(crc!=bc_hash(p,BC_SAVE_BYTES-4U)) {notice(s,9U);return;}
    for(unsigned i=0;i<BC_CUSTOM;++i)
        if(!bc_level_decode(&levels[i],p+5U+i*BC_LEVEL_BYTES)) {notice(s,9U);return;}
    memcpy(s->custom,levels,sizeof(levels));s->custom_slot=p[4];s->dirty=false;
}
void bc_levels_save(p4_game_context_t *c,bc_state_t *s)
{
    if(s->save_ticket) {notice(s,5U);return;}
    for(unsigned i=0;i<BC_CUSTOM;++i) if(bc_level_validate(&s->custom[i])!=BC_LEVEL_OK) {
        notice(s,2U);bc_cue(s,BC_S_ERROR);return;
    }
    if(!c->services || !(c->services->available_capabilities&P4_GAME_CAP_SAVE)) {
        notice(s,7U);bc_cue(s,BC_S_ERROR);return;
    }
    save_encode(s,s->save_blob);
    if(!p4_game_queue_save(c,"AUTO",1U,s->save_sequence,s->save_blob,BC_SAVE_BYTES,&s->save_ticket)) {
        s->save_ticket=0U;notice(s,6U);bc_cue(s,BC_S_ERROR);return;
    }
    s->save_status=P4_GAME_SAVE_QUEUED;notice(s,5U);
}
void bc_save_poll(p4_game_context_t *c,bc_state_t *s)
{
    if(!s->save_ticket) return;
    p4_game_save_status_t status;uint32_t sequence;
    if(!p4_game_read_save_status(c,s->save_ticket,&status,&sequence)) return;
    if(status==P4_GAME_SAVE_QUEUED) return;
    s->save_status=status;s->save_ticket=0U;
    if(status==P4_GAME_SAVE_COMMITTED) {
        s->save_sequence=sequence;
        uint8_t current[BC_SAVE_BYTES];save_encode(s,current);
        s->dirty=memcmp(current,s->save_blob,BC_SAVE_BYTES)!=0;
        notice(s,s->dirty?4U:3U);bc_cue(s,BC_S_SAVE);
    } else {notice(s,6U);bc_cue(s,BC_S_ERROR);}
}
void bc_select_level(bc_state_t *s,unsigned selected)
{
    s->testing=false;
    s->selected=(uint8_t)(selected%(BC_THEMES+BC_CUSTOM));
    if(s->selected<BC_THEMES) bc_level_preset(&s->level,s->selected,0xb1a57c17U+s->selected*127U);
    else {s->custom_slot=(uint8_t)(s->selected-BC_THEMES);s->level=s->custom[s->custom_slot];}
    bc_round(&s->world,s->seed_counter,true);bc_level_apply(&s->world,&s->level);
    s->world.phase=BC_SELECT;s->level_error=(uint8_t)bc_level_match_validate(&s->level);
    memset(s->particles,0,sizeof(s->particles));memset(s->death_ms,0,sizeof(s->death_ms));
    memset(s->debris_ms,0,sizeof(s->debris_ms));s->shake_ms=0U;s->banner_ms=0U;memset(s->face,0,sizeof(s->face));
}
static void edited(bc_state_t *s)
{
    s->custom[s->custom_slot]=s->level;s->dirty=true;
    s->level_error=(uint8_t)bc_level_match_validate(&s->level);
    bc_level_apply(&s->world,&s->level);
}
void bc_editor_enter(bc_state_t *s)
{
    if(s->selected<BC_THEMES) {s->custom[s->custom_slot]=s->level;s->dirty=true;}
    s->selected=(uint8_t)(BC_THEMES+s->custom_slot);
    s->world.phase=BC_EDITOR;s->undo_valid=false;s->last_painted=-1;s->testing=false;
    s->stroke_changed=false;s->editor_menu=0U;
    bc_cue(s,BC_S_CONFIRM);
}
static bool paint_cell(bc_state_t *s,int x,int y,bool keep_undo)
{
    if(bc_border(x,y) || bc_spawn_safe(x,y)) {notice(s,1U);bc_cue(s,BC_S_ERROR);return false;}
    const int xs[2]={x,BC_W-1-x},ys[2]={y,BC_H-1-y};
    bool changed=false;
    for(unsigned j=0;j<(s->mirror?2U:1U);++j) for(unsigned i=0;i<(s->mirror?2U:1U);++i)
        if(!bc_spawn_safe(xs[i],ys[j]) && s->level.tile[ys[j]*BC_W+xs[i]]!=s->brush) changed=true;
    if(!changed) return false;
    if(!keep_undo) {s->undo=s->level;s->undo_valid=true;}
    for(unsigned j=0;j<(s->mirror?2U:1U);++j) for(unsigned i=0;i<(s->mirror?2U:1U);++i)
        if(!bc_spawn_safe(xs[i],ys[j])) s->level.tile[ys[j]*BC_W+xs[i]]=s->brush;
    edited(s);bc_cue(s,s->brush==BC_FLOOR?BC_S_ERASE:BC_S_PAINT);return true;
}
void bc_editor_paint(bc_state_t *s,int x,int y)
{ (void)paint_cell(s,x,y,false); }
static void paint_stroke(bc_state_t *s,int x,int y,bool fresh)
{
    if(fresh || s->last_painted<0) {s->last_painted=(int16_t)(y*BC_W+x);s->stroke_changed=false;}
    int px=s->last_painted%BC_W,py=s->last_painted/BC_W;
    const int dx=x>px?x-px:px-x,dy=y>py?py-y:y-py;
    const int sx=px<x?1:-1,sy=py<y?1:-1;
    int error=dx+dy;
    // Connect bounded grid samples; the entire drag retains one undo snapshot.
    for(unsigned step=0;step<BC_W+BC_H;++step) {
        if(paint_cell(s,px,py,s->stroke_changed)) s->stroke_changed=true;
        if(px==x && py==y) break;
        const int twice=2*error;
        if(twice>=dy) {error+=dy;px+=sx;}
        if(twice<=dx) {error+=dx;py+=sy;}
    }
    s->last_painted=(int16_t)(y*BC_W+x);
}
void bc_editor_undo(bc_state_t *s)
{
    if(!s->undo_valid) {bc_cue(s,BC_S_ERROR);return;}
    const bc_level_t swap=s->level;s->level=s->undo;s->undo=swap;edited(s);notice(s,8U);bc_cue(s,BC_S_ERASE);
}
void bc_start_battle(bc_state_t *s)
{
    const bc_level_error_t error=bc_level_match_validate(&s->level);
    s->level_error=(uint8_t)error;
    if(error!=BC_LEVEL_OK) {notice(s,error==BC_LEVEL_ASYMMETRIC?10U:error==BC_LEVEL_SEPARATION?11U:2U);bc_cue(s,BC_S_ERROR);return;}
    bc_cue(s,BC_S_CONFIRM);
    if(s->network) bc_network_transfer(s);else bc_restart(s);
}
static void brush_next(bc_state_t *s)
{
    static const uint8_t brushes[9]={BC_FLOOR,BC_WALL,BC_CRATE,BC_ARMOR,BC_FUEL,BC_GLASS,BC_RANGE,BC_EXTRA,BC_SPEED};
    for(unsigned i=0;i<9U;++i) if(brushes[i]==s->brush) {s->brush=brushes[(i+1U)%9U];break;}
    bc_cue(s,BC_S_NAV);
}
static void editor_action(p4_game_context_t *c,bc_state_t *s,unsigned action)
{
    if(action==0U) {s->testing=!s->network;bc_start_battle(s);}
    else if(action==1U) bc_levels_save(c,s);
    else if(action==2U) {s->undo=s->level;s->undo_valid=true;s->level.theme=(uint8_t)((s->level.theme+1U)%BC_THEMES);edited(s);bc_cue(s,BC_S_NAV);}
    else if(action==3U) {s->mirror=!s->mirror;bc_cue(s,BC_S_NAV);}
    else if(action==4U) bc_editor_undo(s);
    else if(action==5U) {
        s->undo=s->level;s->undo_valid=true;
        for(int y=1;y<BC_H-1;++y) for(int x=1;x<BC_W-1;++x) s->level.tile[y*BC_W+x]=BC_FLOOR;
        edited(s);bc_cue(s,BC_S_ERASE);
    } else if(action==6U) {bc_select_level(s,s->selected);bc_cue(s,BC_S_CONFIRM);}
    if(action>0U && action<6U) s->world.phase=BC_EDITOR;
}
bool bc_menu_update(p4_game_context_t *c,bc_state_t *s,const p4_game_input_t *in,bool fresh,uint32_t elapsed)
{
    const uint8_t phase=s->world.phase;
    if(phase!=BC_SELECT && phase!=BC_EDITOR && phase!=BC_EDITOR_MENU && phase!=BC_COPY) return false;
    // Raw touch owns this UI; do not also act on the OS's overlapping virtual pad.
    const bool touching=in->touch_valid && in->touch_count;
    const uint32_t pressed=touching?0U:in->pressed,held=touching?0U:in->held;
    const int tx=touching?(int)in->touches[0].x*768/320:-1;
    const int ty=touching?(int)in->touches[0].y*480/200:-1;
    if(phase==BC_COPY) {
        if(pressed&P4_BUTTON_UP) {s->editor_menu=(uint8_t)((s->editor_menu+3U)%4U);bc_cue(s,BC_S_NAV);}
        if(pressed&P4_BUTTON_DOWN) {s->editor_menu=(uint8_t)((s->editor_menu+1U)%4U);bc_cue(s,BC_S_NAV);}
        bool choose=(pressed&P4_BUTTON_A)!=0U;
        if(fresh && tx>=186 && tx<582 && ty>=148 && ty<308) {s->editor_menu=(uint8_t)((ty-148)/40);choose=true;}
        if(pressed&(P4_BUTTON_B|P4_BUTTON_START)) s->world.phase=BC_SELECT;
        else if(choose) {
            if(s->editor_menu<3U) {s->custom_slot=s->editor_menu;bc_editor_enter(s);}
            else s->world.phase=BC_SELECT;
        }
    } else if(phase==BC_SELECT) {
        if((pressed&P4_BUTTON_LEFT) || (fresh && tx>=18 && tx<130 && ty>=150 && ty<310))
            {bc_select_level(s,(s->selected+5U)%6U);bc_cue(s,BC_S_NAV);}
        if((pressed&P4_BUTTON_RIGHT) || (fresh && tx>=638 && tx<750 && ty>=150 && ty<310))
            {bc_select_level(s,(s->selected+1U)%6U);bc_cue(s,BC_S_NAV);}
        if((pressed&P4_BUTTON_B) || (fresh && tx>=24 && tx<130 && ty>=328 && ty<416)) {
            if(s->selected<BC_THEMES) {s->world.phase=BC_COPY;s->editor_menu=s->custom_slot;bc_cue(s,BC_S_CONFIRM);}
            else bc_editor_enter(s);
        }
        else if((pressed&P4_BUTTON_A) || (fresh && tx>=638 && tx<750 && ty>=328 && ty<416)) bc_start_battle(s);
        else if(pressed&P4_BUTTON_START) s->world.phase=BC_TITLE;
    } else if(phase==BC_EDITOR_MENU) {
        if(pressed&P4_BUTTON_UP) {s->editor_menu=(uint8_t)((s->editor_menu+6U)%7U);bc_cue(s,BC_S_NAV);}
        if(pressed&P4_BUTTON_DOWN) {s->editor_menu=(uint8_t)((s->editor_menu+1U)%7U);bc_cue(s,BC_S_NAV);}
        if(pressed&(P4_BUTTON_B|P4_BUTTON_START)) s->world.phase=BC_EDITOR;
        else if(pressed&P4_BUTTON_A) editor_action(c,s,s->editor_menu);
        else if(fresh && tx>=186 && tx<582 && ty>=112 && ty<392) editor_action(c,s,(unsigned)(ty-112)/40U);
    } else {
        s->edit_repeat=s->edit_repeat>elapsed?(uint16_t)(s->edit_repeat-elapsed):0U;
        if((pressed&15U) || (!s->edit_repeat && (held&15U))) {
            const uint32_t d=(pressed&15U)?pressed:held;
            if((d&P4_BUTTON_LEFT) && s->cursor_x>1U) --s->cursor_x;
            if((d&P4_BUTTON_RIGHT) && s->cursor_x<BC_W-2U) ++s->cursor_x;
            if((d&P4_BUTTON_UP) && s->cursor_y>1U) --s->cursor_y;
            if((d&P4_BUTTON_DOWN) && s->cursor_y<BC_H-2U) ++s->cursor_y;
            s->edit_repeat=(pressed&15U)?250U:95U;
        }
        if(pressed&P4_BUTTON_A) bc_editor_paint(s,s->cursor_x,s->cursor_y);
        if((pressed&P4_BUTTON_B) || (fresh && tx<132 && ty>=100 && ty<280)) brush_next(s);
        if((pressed&P4_BUTTON_START) || (fresh && tx>=636 && ty>=310 && ty<425)) {s->world.phase=BC_EDITOR_MENU;bc_cue(s,BC_S_PAUSE);}
        if(fresh && tx>=636 && ty>=92 && ty<168) editor_action(c,s,3U);
        if(fresh && tx>=636 && ty>=180 && ty<260) bc_editor_undo(s);
        if(touching && tx>=146 && tx<622 && ty>=64 && ty<428) {
            const int x=(tx-146)/28,y=(ty-64)/28,at=y*BC_W+x;
            s->cursor_x=(uint8_t)x;s->cursor_y=(uint8_t)y;
            if(fresh || at!=s->last_painted) paint_stroke(s,x,y,fresh);
        } else {s->last_painted=-1;s->stroke_changed=false;}
    }
    return true;
}
