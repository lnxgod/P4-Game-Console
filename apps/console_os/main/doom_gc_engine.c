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
#include <strings.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

extern patch_t *hu_font[HU_FONTSIZE];
static p4_doom_arena_t arena;
static int map_lumps[P4_DOOM_ARENA_SELECTIONS], music_lumps[P4_DOOM_ARENA_SELECTIONS];
static uint8_t queued[2], queue_count, menu_row, picker;
static boolean panel_open, picker_open;
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

void p4_doom_gc_engine_begin(uint8_t count, uint8_t map)
{
    if (!p4_doom_arena_begin_selected(&arena,count,map)) I_Error("Invalid arena setup");
    queue_count=0; panel_open=picker_open=false;
}

boolean P4_DoomArenaActive(void) { return p4_doom_gc_active() && arena.map_count!=0; }
boolean P4_DoomArenaPlayerActive(int slot)
{
    return !P4_DoomArenaActive() || (slot>=0 && slot<MAXPLAYERS && arena.players[slot].active);
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
            if (p->mo) {
                S_StopSound(p->mo);
                p->mo->player=NULL;
                P_RemoveMobj(p->mo);
                p->mo=NULL;
            }
            continue;
        }
        if (transition.returned & (1U<<i)) {
            /* Do not insert a frozen live actor into Doom's corpse queue.
             * A NULL previous body follows the normal first-spawn path. */
            if (p->mo) {
                S_StopSound(p->mo);
                p->mo->player=NULL;
                P_RemoveMobj(p->mo);
            }
            p->mo=NULL;
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

void P4_DoomArenaHUD(void)
{
    if (!P4_DoomArenaActive()) return;
    V_DrawFilledBox(0,11,320,48,0);
    text_line(4,12,"GAME CHANGERS AI");
    text_line(4,23,p4_doom_arena_label(arena.maps[arena.map_index]));
    for (unsigned i=0;i<MAXPLAYERS;++i) {
        if (!arena.players[i].visit) continue;
        char text[32];
        if (!(arena.connected_mask & (1U<<i)))
            (void)snprintf(text,sizeof(text),"P%u LEFT",i+1U);
        else if (!arena.players[i].active)
            (void)snprintf(text,sizeof(text),"P%u BREAK",i+1U);
        else
            (void)snprintf(text,sizeof(text),"P%u %" PRIu32,i+1U,arena.players[i].kills);
        text_line(i%2U ? 168 : 4,36+(int)(i/2U)*12,text);
    }
    if (arena.vote_map) {
        text_line(4,141,"MAP VOTE: OPEN MENU TO VOTE");
        text_line(4,153,p4_doom_arena_label(arena.vote_map));
    }
    if (!arena.players[consoleplayer].active) {
        text_line(100,78,"TAKE A BREAK");
        text_line(48,94,"PRESS USE TO RETURN AT ZERO");
    }
}

#ifdef P4_DOOM_ARENA_HOST_TEST
const p4_doom_arena_t *p4_doom_gc_test_state(void) { return &arena; }
#endif

int P4_DoomArenaFragCount(int vanilla)
{
    if (!P4_DoomArenaActive()) return vanilla;
    const uint32_t kills=arena.players[consoleplayer].kills;
    /* Vanilla's two-digit status widget caps at 99; the shared HUD is full. */
    return kills>99U?99:(int)kills;
}

boolean P4_DoomArenaMenuKey(int key)
{
    if (!P4_DoomArenaActive() || gamestate!=GS_LEVEL) return false;
    if (!panel_open) {
        if (menuactive || key!=key_menu_activate) return false;
        panel_open=true; menuactive=true; picker_open=false; menu_row=0;
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
    text_line(24,67,arena.vote_map ? p4_doom_arena_label(arena.vote_map) : "GAME CHANGERS AI");
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
