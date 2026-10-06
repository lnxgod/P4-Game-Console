// SPDX-License-Identifier: GPL-2.0-or-later
/* Deterministic four-seat engine exercise; transport loss is tested separately. */
#include "p4_doom_net.h"
#include "p4/doom_arena.h"
#include "doomstat.h"
#include "d_main.h"
#include "d_loop.h"
#include "g_game.h"
#include "p_local.h"
#include "w_wad.h"
#include "z_zone.h"
#include "doomkeys.h"
#include "m_menu.h"
#include "d_event.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
extern void p4_doom_gc_engine_begin(uint8_t,uint8_t);
extern const p4_doom_arena_t *p4_doom_gc_test_state(void);
static bool killed,broken,returned,exited,observed_map;
static unsigned rotations;
static int pending_map;
static unsigned phase, dwango_loaded;
static uint8_t quick_selection;
static uint8_t pending_selection, remote_generation=255, remote_token;
static bool remote_pending;
static void old_world_probe(thinker_t *thinker)
{
    (void)thinker;
    const p4_doom_arena_t *a=p4_doom_gc_test_state();
    /* A passed vote must not run old-map thinkers/exits in the same tic. */
    assert(!(phase==2 && a->maps[a->map_index]==3));
}
static void key(int value)
{
    event_t event={.type=ev_keydown,.data1=value,.data2=value};
    assert(M_Responder(&event));
}
static void propose(uint8_t target)
{
    key(KEY_ESCAPE); key(KEY_DOWNARROW); key(KEY_ENTER); /* choose arena */
    const p4_doom_arena_t *a=p4_doom_gc_test_state();
    uint8_t selected=a->maps[a->map_index];
    while(selected!=target) { key(KEY_RIGHTARROW); selected=selected==26?1:(uint8_t)(selected+1U); }
    key(KEY_ENTER);
}

static void verify_loaded_arena(void)
{
    assert(gamemode==commercial);
    const p4_doom_arena_t *a=p4_doom_gc_test_state();
    const uint8_t selection=a->maps[a->map_index];
    assert(gamemap==p4_doom_arena_map_number(selection));
    char marker[9]; (void)snprintf(marker,sizeof(marker),"MAP%02d",gamemap);
    const int map_lump=P4_DoomArenaMapLump(W_GetNumForName(marker));
    if (selection>2) {
        assert(strncmp(lumpinfo[map_lump].name,"D5L",3)==0);
        assert(W_LumpLength((unsigned)(map_lump+1))>0);
        assert(W_LumpLength((unsigned)P4_DoomArenaMusicLump(0))>0);
        assert(players[0].mo && players[1].mo && players[2].mo);
        printf("P4_DOOM_ARENA LOADED DWANGO5 MAP%02d lump=%d vertices=%d lines=%d sectors=%d\n",gamemap,map_lump,numvertexes,numlines,numsectors);
        return;
    }
    assert(W_LumpLength((unsigned)(map_lump+1))==660);
    const uint8_t *things=W_CacheLumpNum(map_lump+1,PU_STATIC);
    unsigned shotgun=0,super=0,rocket=0,starts=0;
    for (size_t i=0;i<660;i+=10) {
        const unsigned type=(unsigned)things[i+6]|((unsigned)things[i+7]<<8);
        shotgun+=type==2001; super+=type==82; rocket+=type==2003; starts+=type==11;
    }
    assert(starts==8);
    assert(gamemap==1 ? shotgun==8 && super==8 && rocket==0 : shotgun==0 && super==0 && rocket==16);
    W_ReleaseLumpNum(map_lump+1);
    const int music=P4_DoomArenaMusicLump(W_GetNumForName(gamemap==1?"D_RUNNIN":"D_STALKS"));
    assert(W_LumpLength((unsigned)music)==43798);
    const uint8_t *midi=W_CacheLumpNum(music,PU_STATIC);
    assert(memcmp(midi,"MThd",4)==0);
    W_ReleaseLumpNum(music);
    printf("P4_DOOM_ARENA LOADED map=%d starts=%u shotguns=%u super=%u rockets=%u midi_bytes=43798\n",gamemap,starts,shotgun,super,rocket);
}

boolean p4_doom_gc_active(void) { return true; }
boolean P4_DoomNetActive(void) { return true; }
boolean P4_DoomNetFailed(void) { return false; }
boolean P4_DoomNetConfigure(net_gamesettings_t *s)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    *s=(net_gamesettings_t){.consoleplayer=0,.num_players=4,.deathmatch=2,
        .episode=1,.map=1,.skill=2,.loadgame=-1,.nomonsters=1,.new_sync=1,
        .extratics=1,.ticdup=1};
    const char *start=getenv("P4_DOOM_ARENA_START");
    if (start) { quick_selection=(uint8_t)atoi(start); s->map=p4_doom_arena_map_number(quick_selection); assert(s->map); }
    p4_doom_gc_engine_begin(4,quick_selection?quick_selection:1);
    ticcmd_t commands[NET_MAXPLAYERS]={0};
    boolean mask[NET_MAXPLAYERS]={true,true,true,true};
    D_ReceiveTic(commands,mask); D_ReceiveTic(commands,mask);
    return true;
}
void P4_DoomNetSubmitTic(const ticcmd_t *local,int tick)
{

    ticcmd_t commands[NET_MAXPLAYERS]={0};
    boolean mask[NET_MAXPLAYERS]={true,true,true,true};
    if (tick>=575) mask[3]=false;
    /* Only player one stays idle. Two fires, three and four move. */
    commands[1].buttons=BT_ATTACK;
    commands[2].forwardmove=1;
    commands[3].forwardmove=1;
    if (tick==550) commands[0].buttons=BT_USE;
    if (tick>550) commands[0].forwardmove=1;
    commands[0].chatchar=local->chatchar;
    const p4_doom_arena_t *a=p4_doom_gc_test_state();
    if (remote_pending) { commands[1].chatchar=remote_token; remote_pending=false; }
    else if (a->vote_map && remote_generation!=a->vote_generation) {
        commands[1].chatchar=0xf0; remote_generation=a->vote_generation;
        remote_token=(uint8_t)(0x80U|remote_generation); remote_pending=true;
    }
    /* Engine-generated consistency is view-independent for every seat. */
    extern byte consistancy[MAXPLAYERS][BACKUPTICS];
    for (int i=0;i<4;++i) {
        commands[i].consistancy=consistancy[i][tick%BACKUPTICS];
    }
    D_ReceiveTic(commands,mask);
}
void P4_DoomNetPoll(void)
{
    if (gamestate!=GS_LEVEL || !players[0].mo) return;
    const p4_doom_arena_t *a=p4_doom_gc_test_state();
    if (quick_selection) {
        if (!observed_map) { verify_loaded_arena(); observed_map=true; }
        if (!exited && leveltime>=5) { G_SecretExitLevel(); exited=true; }
        const uint8_t next=quick_selection<=2?1:3;
        if (exited && a->maps[a->map_index]==next && gameaction==ga_nothing) {
            verify_loaded_arena(); puts("P4_DOOM_ARENA HOST SELECTION PASS starting selection and secret-exit wrap"); exit(0);
        }
        return;
    }
    if (!observed_map) { assert(gamemap==1); verify_loaded_arena(); observed_map=true; }
    if (leveltime>=20 && !killed) {
        P_DamageMobj(players[3].mo,players[0].mo,players[0].mo,10000);
        assert(a->players[0].kills==1);
        killed=true;
    }
    if (leveltime>=530 && !broken) {
        assert(!a->players[0].active && a->players[0].kills==0 && P4_DoomArenaFragCount(1)==0);
        assert(a->players[1].active);
        assert((players[0].mo->flags & (MF_SOLID|MF_SHOOTABLE|MF_PICKUP))==0);
        assert(players[0].mo->momx==0 && players[0].mo->momy==0);
        broken=true;
    }
    if (leveltime>=565 && !returned) {
        assert(broken && a->players[0].active && a->players[0].visit==2);
        assert(a->players[0].kills==0 && players[0].health==100);
        assert(players[0].mo->flags & MF_SHOOTABLE);
        P_DamageMobj(players[2].mo,players[0].mo,players[0].mo,10000);
        assert(a->players[0].kills==1);
        returned=true;
    }
    if (pending_map && gamemap==pending_map && leveltime<20) {
        assert(returned && a->players[0].kills==1 && a->players[0].visit==2);
        assert(!a->players[3].active && players[3].mo==NULL && a->connected_mask==7);
        assert(P4_DoomArenaFragCount(0)==1 && a->map_count==2);
        assert(gamestate==GS_LEVEL && gameaction==ga_nothing);
        verify_loaded_arena(); pending_map=0; ++rotations;
        if (rotations==4) {
            phase=1;
            key(KEY_ESCAPE); key(KEY_DOWNARROW); key(KEY_ENTER);
        }
    }
    if (!phase && !pending_map && ((!exited && leveltime>=580) || (exited && leveltime>=25))) {
        pending_map=gamemap==1?2:1;
        /* Both exit types on both maps must bypass vanilla progression/finale. */
        if (rotations==1 || rotations==2) G_SecretExitLevel(); else G_ExitLevel();
        exited=true;
    }
    if (phase==1 && leveltime>=15) {
        key(KEY_RIGHTARROW); key(KEY_RIGHTARROW); key(KEY_ENTER); /* Pure Hell 1 -> DWANGO 1 */
        phase=2;
        thinker_t *probe=Z_Malloc(sizeof(*probe),PU_LEVEL,NULL);
        probe->function.acp1=(actionf_p1)old_world_probe;
        P_AddThinker(probe);
    }
    if (phase==2 && a->maps[a->map_index]==3 && gameaction==ga_nothing && leveltime<20) {
        assert(a->players[0].kills==1 && a->players[0].visit==2 && a->connected_mask==7);
        verify_loaded_arena(); dwango_loaded=1; phase=3;
    }
    if (phase==3 && pending_selection && a->maps[a->map_index]==pending_selection && gameaction==ga_nothing && leveltime<20) {
        verify_loaded_arena(); pending_selection=0; ++dwango_loaded;
        if (dwango_loaded==25) phase=4;
    }
    if (phase==3 && !pending_selection && leveltime>=20) {
        pending_selection=a->maps[a->map_index]==26 ? 3 : (uint8_t)(a->maps[a->map_index]+1U);
        if (gamemap%2) G_SecretExitLevel(); else G_ExitLevel();
    }
    if (phase==4 && leveltime>=10 && !a->vote_cooldown) { propose(1); phase=5; }
    if (phase==5 && a->maps[a->map_index]==1 && gameaction==ga_nothing && leveltime<20) {
        verify_loaded_arena();
        assert(a->players[0].kills==1 && a->players[0].visit==2 && a->connected_mask==7);
        assert(a->map_count==2 && !players[3].mo);
        puts("P4_DOOM_ARENA PASS four seats; idle/fire/break/return; Pure Hell ordinary+secret 1->2 loop; controller menu proposal and synchronized majority to DWANGO; all 24 DWANGO maps loaded and cycled; vote back to Pure Hell; scores/visits/membership and music preserved");
        exit(0);
    }

}

void p4_doom_arena_capture(const uint32_t *pixels)
{
    static unsigned saved;
    const char *directory=getenv("P4_DOOM_ARENA_SNAPSHOT_DIR");
    if (!directory || (!phase && (!broken || exited))) return;
    const unsigned bit=phase==1 ? 4U : phase>=3 ? 8U : returned?2U:1U;
    if (saved & bit) return;
    if ((bit==4U || bit==8U) && leveltime<12) return;
    char path[1024];
    const int n=snprintf(path,sizeof(path),"%s/arena-%s.ppm",directory,bit==4U?"picker":bit==8U?"dwango":returned?"returned":"break");
    assert(n>0 && (size_t)n<sizeof(path));
    FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n320 200\n255\n");
    for (unsigned i=0;i<320U*200U;++i) {
        const uint8_t rgb[3]={(uint8_t)(pixels[i]>>16),(uint8_t)(pixels[i]>>8),(uint8_t)pixels[i]};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(fclose(f)==0);
    saved |= bit;
}
