// SPDX-License-Identifier: GPL-2.0-or-later
#include "p4_doom_net.h"
extern boolean p4_doom_gc_active(void);
#include "p4/doom_arena.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "p_local.h"
#include "s_sound.h"
#include "hu_stuff.h"
#include "hu_lib.h"
#include "am_map.h"
#include "w_wad.h"
#include "sounds.h"
#include "i_system.h"
#include "v_video.h"
#include "m_menu.h"
#include "m_controls.h"
#include "doomkeys.h"
#include "doom_arena_ui.h"
#include <strings.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

extern patch_t *hu_font[HU_FONTSIZE];
static p4_doom_arena_t arena;
static int map_lumps[P4_DOOM_ARENA_SELECTIONS], music_lumps[P4_DOOM_ARENA_SELECTIONS];
static uint8_t queued[2], queue_count, menu_row, picker;
static boolean panel_open, picker_open;
static bool scores_open, score_touch_held;
static void reload_arena(void);

void P4_DoomArenaWadLoaded(const char *filename,int first,int count)
{
    if (!p4_doom_gc_active()) return;
    const char *name=strrchr(filename,'/'); name=name ? name+1 : filename;
    const bool pure=strcasecmp(name,"purehades.wad")==0;
    const bool dwango=strcasecmp(name,"dwango5.wad")==0;
    if (!pure && !dwango) return;
    if (first<0 || count!=(pure?71:317) || (unsigned)(first+count)>numlumps)
        I_Error("Arena WAD directory mismatch");
    const unsigned maps=pure?5U:24U, base=pure?0U:5U;
    for (unsigned m=0;m<maps;++m) {
        char marker[9], music[9];
        (void)snprintf(marker,sizeof(marker),"MAP%02u",m+1U);
        (void)snprintf(music,sizeof(music),"D_%s",S_music[mus_runnin+m].name);
        map_lumps[base+m]=-1; music_lumps[base+m]=-1;
        for (int j=first;j<first+count;++j) {
            if (strncasecmp(lumpinfo[j].name,marker,8)==0) map_lumps[base+m]=j;
            if (strncasecmp(lumpinfo[j].name,music,8)==0) music_lumps[base+m]=j;
        }
        if (map_lumps[base+m]<0 || music_lumps[base+m]<0) I_Error("Missing arena map/music");
    }
    /* Isolate the old compilation's globals, names and music. The actual
     * PWAD stays unchanged; map loaders address its ten following lumps by
     * index. Freedoom supplies textures/sounds/UI for both packs. */
    if (dwango) for (int j=0;j<count;++j) {
        char private_name[9];
        (void)snprintf(private_name,sizeof(private_name),"D5L%04u",(unsigned)j%10000U);
        memcpy(lumpinfo[first+j].name,private_name,8);
    }
}

int P4_DoomArenaMapLump(int vanilla)
{
    if (!P4_DoomArenaActive()) return vanilla;
    const uint8_t selection=arena.maps[arena.map_index];
    if (selection<1 || selection>P4_DOOM_ARENA_SELECTIONS || map_lumps[selection-1U]<=0) I_Error("Arena map unavailable");
    return map_lumps[selection-1U];
}

int P4_DoomArenaMusicLump(int vanilla)
{
    if (!P4_DoomArenaActive()) return vanilla;
    const uint8_t selection=arena.maps[arena.map_index];
    if (selection<1 || selection>P4_DOOM_ARENA_SELECTIONS || music_lumps[selection-1U]<=0) I_Error("Arena music unavailable");
    return music_lumps[selection-1U];
}

void P4_DoomArenaBuildCommand(ticcmd_t *command)
{
    if (!P4_DoomArenaActive() || !queue_count) return;
    command->chatchar=queued[2U-queue_count];
    --queue_count;
}

static bool queue_vote(uint8_t opcode)
{
    if (queue_count || !arena.players[consoleplayer].active) return false;
    if (opcode>=0x81 && opcode<=0x9d && (arena.vote_map || arena.vote_cooldown)) return false;
    if ((opcode==0xf0 || opcode==0xf1) && !arena.vote_map) return false;
    queued[0]=opcode; queued[1]=(uint8_t)(0xa0U+arena.vote_generation); queue_count=2;
    return true;
}

void p4_doom_gc_engine_begin_mask(uint8_t count, uint8_t initial_mask, uint8_t map)
{
    if (!p4_doom_arena_begin_selected_mask(&arena,count,initial_mask,map)) I_Error("Invalid arena setup");
    queue_count=0; panel_open=picker_open=false;
    scores_open=score_touch_held=false;
}

void p4_doom_gc_engine_begin(uint8_t count, uint8_t map)
{
    if (count < 2 || count > MAXPLAYERS) I_Error("Invalid arena capacity");
    p4_doom_gc_engine_begin_mask(count, (uint8_t)((1U << count) - 1U), map);
}

boolean P4_DoomArenaActive(void) { return p4_doom_gc_active() && arena.map_count!=0; }
boolean P4_DoomArenaPlayerActive(int slot)
{
    return !P4_DoomArenaActive() || (slot>=0 && slot<MAXPLAYERS && arena.players[slot].active);
}

static void remove_arena_body(player_t *p)
{
    mobj_t *body = p->mo;
    p->attacker = NULL;
    if (!body) return;
    /* Vanilla quits keep their actor. Arena seats remove it, so outstanding
     * damage and projectile references must stop pointing at the freed body. */
    for (unsigned i=0; i<MAXPLAYERS; ++i)
        if (players[i].attacker == body) players[i].attacker = NULL;
    for (int i=0; i<numsectors; ++i)
        if (sectors[i].soundtarget == body) sectors[i].soundtarget = NULL;
    for (thinker_t *th=thinkercap.next; th!=&thinkercap; th=th->next) {
        if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
        mobj_t *mo=(mobj_t *)th;
        if (mo->target == body) mo->target = NULL;
        if (mo->tracer == body) mo->tracer = NULL;
    }
    S_StopSound(body);
    body->player = NULL;
    P_RemoveMobj(body);
    p->mo = NULL;
}

void P4_DoomArenaRejoin(int slot)
{
    if (slot < 0 || slot >= MAXPLAYERS
        || !p4_doom_arena_activate(&arena, (uint8_t)slot))
        I_Error("Invalid arena reactivation");
    player_t *p = &players[slot];
    remove_arena_body(p);
    p->playerstate = PST_REBORN;
    /* A pending voted-map load will spawn this seat with every other seat.
     * Otherwise use the existing first-spawn path, never G_DoReborn's corpse
     * path: a departed seat has no previous body. */
    if (gameaction != ga_loadlevel)
        G_DeathMatchSpawnPlayer(slot);
}

void P4_DoomArenaTicker(void)
{
    if (!P4_DoomArenaActive()) return;
    p4_doom_arena_activity_t activity[P4_MP_MAX_PLAYERS]={0};
    uint8_t mask=0, chat[P4_MP_MAX_PLAYERS]={0};
    for (unsigned i=0;i<MAXPLAYERS;++i) {
        ticcmd_t *c=&players[i].cmd;
        chat[i]=(uint8_t)c->chatchar;
        if (chat[i]&0x80U) c->chatchar=0; /* Reserved bounded vote commands never reach chat. */
        if (playeringame[i]) mask |= (uint8_t)(1U<<i);
        activity[i]=(p4_doom_arena_activity_t){
            .moving=c->forwardmove!=0 || c->sidemove!=0,
            .firing=(c->buttons & BT_ATTACK)!=0,
            .use=(c->buttons & BT_USE)!=0};
    }
    const p4_doom_arena_transition_t transition=p4_doom_arena_tick(&arena,mask,activity);
    const uint8_t approved=p4_doom_arena_vote_tick(&arena,chat);
    if (approved && p4_doom_arena_select(&arena,approved)) reload_arena();
    for (unsigned i=0;i<MAXPLAYERS;++i) {
        player_t *p=&players[i];
        if (!playeringame[i]) {
            remove_arena_body(p);
            continue;
        }
        if (transition.returned & (1U<<i)) {
            /* Do not insert a frozen live actor into Doom's corpse queue.
             * A NULL previous body follows the normal first-spawn path. */
            remove_arena_body(p);
            p->playerstate=PST_REBORN;
            G_DeathMatchSpawnPlayer((int)i);
            memset(&p->cmd,0,sizeof(p->cmd));
        }
        if (!arena.players[i].active && p->mo) {
            /* Reapply after a map reset as inactive seats still have cameras. */
            if (transition.entered_break & (1U<<i)) S_StopSound(p->mo);
            p->mo->flags &= ~(MF_SOLID|MF_SHOOTABLE|MF_PICKUP);
            p->mo->flags |= MF_NOCLIP|MF_NOGRAVITY;
            p->mo->momx=p->mo->momy=p->mo->momz=0;
            p->damagecount=p->bonuscount=0;
            memset(&p->cmd,0,sizeof(p->cmd));
        }
    }
}

void P4_DoomArenaKill(int killer, int victim)
{
    if (P4_DoomArenaActive() && killer>=0 && killer<MAXPLAYERS && victim>=0 && victim<MAXPLAYERS)
        (void)p4_doom_arena_kill(&arena,(uint8_t)killer,(uint8_t)victim);
}

static void reload_arena(void)
{
    if (automapactive) AM_Stop();
    panel_open=picker_open=false; menuactive=false; queue_count=0;
    scores_open=score_touch_held=false;
    gamemap=p4_doom_arena_map_number(arena.maps[arena.map_index]);
    for (unsigned i=0;i<MAXPLAYERS;++i)
        if (playeringame[i]) players[i].playerstate=PST_REBORN;
    gameaction=ga_loadlevel;
}

boolean P4_DoomArenaCompleted(void)
{
    if (!P4_DoomArenaActive()) return false;
    (void)p4_doom_arena_next_map(&arena);
    reload_arena();
    return true;
}

static void text_line(int x,int y,const char *text)
{
    hu_textline_t line;
    HUlib_initTextLine(&line,x,y,hu_font,HU_FONTSTART);
    while (*text) (void)HUlib_addCharToTextLine(&line,*text++);
    HUlib_drawTextLine(&line,false);
}

void p4_doom_arena_score_touch(bool pressed)
{
    if (P4_DoomArenaActive() && gamestate==GS_LEVEL && !menuactive &&
        pressed && !score_touch_held) scores_open=!scores_open;
    score_touch_held=pressed;
}

static void draw_scores(void)
{
    V_DrawFilledBox(12,44,296,64,0);
    text_line(20,48,"Doom Arena by Game Changers");
    text_line(20,59,p4_doom_arena_label(arena.maps[arena.map_index]));
    for (unsigned i=0;i<MAXPLAYERS;++i) {
        if (!arena.players[i].visit) continue;
        char text[32];
        if (!(arena.connected_mask & (1U<<i)))
            (void)snprintf(text,sizeof(text),"P%u LEFT",i+1U);
        else if (!arena.players[i].active)
            (void)snprintf(text,sizeof(text),"P%u BREAK",i+1U);
        else
            (void)snprintf(text,sizeof(text),"P%u %" PRIu32,i+1U,arena.players[i].kills);
        text_line(i%2U ? 168 : 20,76+(int)(i/2U)*12,text);
    }
}

static void draw_score_control(void)
{
    enum { C, E, L, O, R, S };
    static const uint8_t glyphs[][5]={
        {7,4,4,4,7}, {7,4,6,4,7}, {4,4,4,4,7},
        {7,5,5,5,7}, {6,5,6,5,5}, {7,4,7,1,7}};
    static const uint8_t labels[2][5]={{S,C,O,R,E},{C,L,O,S,E}};
    const unsigned left=P4_DOOM_SCORE_LEFT+(P4_DOOM_SCORE_WIDTH-20U)/2U;
    const unsigned glyph_top=P4_DOOM_SCORE_TOP+(P4_DOOM_SCORE_HEIGHT-6U)/2U;
    /* Leave gameplay visible throughout the large touch target. These tiny
     * glyphs use Doom's black/white palette entries and a one-pixel shadow. */
    for (unsigned pass=0;pass<2;++pass) {
        const unsigned shadow=1U-pass;
        for (unsigned letter=0;letter<5;++letter)
            for (unsigned row=0;row<5;++row)
                for (unsigned column=0;column<3;++column)
                    if (glyphs[labels[scores_open ? 1 : 0][letter]][row]
                        & (1U << (2U-column)))
                        V_DrawFilledBox((int)(left+letter*4U+column+shadow),
                                        (int)(glyph_top+row+shadow),1,1,pass ? 4 : 0);
    }
}

void P4_DoomArenaHUD(void)
{
    if (!P4_DoomArenaActive() || gamestate!=GS_LEVEL) {
        scores_open=score_touch_held=false;
        return;
    }
    if (menuactive) scores_open=false;
    else {
        draw_score_control();
        if (scores_open) draw_scores();
    }
    if (arena.vote_map) {
        text_line(4,141,"MAP VOTE: OPEN MENU TO VOTE");
        text_line(4,153,p4_doom_arena_label(arena.vote_map));
    }
    if (!arena.players[consoleplayer].active) {
        const int y=scores_open && !menuactive ? 114 : 78;
        text_line(100,y,"TAKE A BREAK");
        text_line(48,y+16,"PRESS USE TO RETURN AT ZERO");
    }
}

#ifdef P4_DOOM_ARENA_HOST_TEST
const p4_doom_arena_t *p4_doom_gc_test_state(void) { return &arena; }
#endif

int P4_DoomArenaFragCount(int vanilla)
{
    if (!P4_DoomArenaActive()) return vanilla;
    const uint32_t kills=arena.players[consoleplayer].kills;
    /* Vanilla's two-digit status widget caps at 99; the score panel is full. */
    return kills>99U?99:(int)kills;
}

boolean P4_DoomArenaMenuKey(int key)
{
    if (!P4_DoomArenaActive() || gamestate!=GS_LEVEL) return false;
    /* Start/Pause is local score inspection in Arena; Back opens its menu. */
    if (key==KEY_PAUSE) {
        if (!menuactive) scores_open=!scores_open;
        return true;
    }
    if (!panel_open) {
        if (menuactive || key!=key_menu_activate) return false;
        panel_open=true; menuactive=true; picker_open=false; menu_row=0;
        scores_open=false;
        picker=arena.maps[arena.map_index];
        return true;
    }
    if (key==key_menu_activate || key==key_menu_back || key==key_menu_abort) {
        if (picker_open) picker_open=false;
        else { panel_open=false; menuactive=false; }
    } else if (key==key_menu_up || key==key_menu_down || key==key_menu_left || key==key_menu_right) {
        const bool previous=key==key_menu_up || key==key_menu_left;
        if (picker_open) picker=(uint8_t)(previous ? (picker==1?P4_DOOM_ARENA_SELECTIONS:picker-1) : (picker==P4_DOOM_ARENA_SELECTIONS?1:picker+1));
        else menu_row=(uint8_t)((menu_row+(previous?4U:1U))%5U);
    } else if (key==key_menu_forward || key==key_menu_confirm) {
        if (picker_open) {
            if (queue_vote((uint8_t)(0x80U+picker))) { panel_open=picker_open=false; menuactive=false; }
        } else if (menu_row==0) { panel_open=false; menuactive=false; }
        else if (menu_row==1) picker_open=true;
        else if (menu_row==2 || menu_row==3) {
            if (queue_vote(menu_row==2?0xf0:0xf1)) { panel_open=false; menuactive=false; }
        } else { panel_open=false; menuactive=false; M_StartControlPanel(); }
    }
    return true;
}

boolean P4_DoomArenaMenuDraw(void)
{
    if (!P4_DoomArenaActive() || !panel_open) return false;
    V_DrawFilledBox(12,61,296,106,0);
    text_line(24,67,arena.vote_map ? p4_doom_arena_label(arena.vote_map) : "Doom Arena by Game Changers");
    if (picker_open) {
        text_line(24,88,"CHOOSE AN ARENA");
        text_line(24,106,p4_doom_arena_label(picker));
        char position[32]; (void)snprintf(position,sizeof(position),"%u / %u  -  UP / DOWN",(unsigned)picker,(unsigned)P4_DOOM_ARENA_SELECTIONS);
        text_line(24,121,position);
        text_line(24,141,"CONFIRM TO PROPOSE / BACK");
        if (arena.vote_map) text_line(24,155,"WAIT FOR THE CURRENT VOTE");
        else if (arena.vote_cooldown) text_line(24,155,"NEXT VOTE AVAILABLE SHORTLY");
        else if (!arena.players[consoleplayer].active) text_line(24,155,"RETURN FROM BREAK TO VOTE");
    } else {
        static const char *const labels[]={"RESUME","CHOOSE ARENA","VOTE YES","VOTE NO","DOOM OPTIONS / QUIT"};
        for (unsigned i=0;i<5;++i) {
            if (i==menu_row) text_line(20,86+(int)i*13,">");
            text_line(34,86+(int)i*13,labels[i]);
        }
        if (arena.vote_map) {
            unsigned yes=0,eligible=0;
            const uint8_t active=p4_doom_arena_active_mask(&arena);
            for (unsigned i=0;i<4;++i) { yes+=(arena.vote_yes>>i)&1U; eligible+=(active>>i)&1U; }
            char tally[48]; (void)snprintf(tally,sizeof(tally),"YES %u / NEED %u / %u SEC",yes,eligible/2U+1U,(unsigned)(arena.vote_tics+34U)/35U);
            text_line(24,155,tally);
        } else text_line(24,155,"MAP CHANGES KEEP YOUR KILLS");
    }
    return true;
}

/* Exact deterministic state only; local panels/input remain guest-owned. */
void p4_doom_gc_checkpoint_get_arena(p4_doom_arena_t *out)
{
    if (out) *out = arena;
}
bool p4_doom_gc_checkpoint_set_arena(const p4_doom_arena_t *in)
{
    if (!in || in->capacity != 4 || !in->map_count
        || in->map_count > P4_DOOM_ARENA_MAX_MAPS || in->map_index >= in->map_count
        || !(in->connected_mask & 1U) || (in->connected_mask & ~15U)
        || in->vote_generation > 63 || in->vote_map > P4_DOOM_ARENA_SELECTIONS
        || (in->vote_yes & ~15U) || (in->vote_no & ~15U)
        || in->vote_tics > P4_DOOM_ARENA_VOTE_TICS
        || in->vote_cooldown > P4_DOOM_ARENA_VOTE_COOLDOWN)
        return false;
    for (unsigned i = 0; i < in->map_count; ++i)
        if (!in->maps[i] || in->maps[i] > P4_DOOM_ARENA_SELECTIONS) return false;
    for (unsigned i = 0; i < 4; ++i) {
        if (in->players[i].active && !(in->connected_mask & (1U << i))) return false;
        if (in->players[i].idle_tics > P4_DOOM_ARENA_IDLE_TICS) return false;
    }
    arena = *in;
    queue_count = 0; panel_open = picker_open = false;
    scores_open = score_touch_held = false;
    return true;
}
