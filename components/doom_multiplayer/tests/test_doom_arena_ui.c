// SPDX-License-Identifier: GPL-2.0-or-later
/* Production Arena UI with rendering/engine boundaries observed on the host. */
#include <assert.h>
#include <stdlib.h>
#include "../../../apps/console_os/main/doom_gc_engine.c"

patch_t *hu_font[HU_FONTSIZE];
player_t players[MAXPLAYERS];
boolean playeringame[MAXPLAYERS];
/* This presentation fixture has no map or actors; keep a valid empty world. */
int numsectors;
sector_t *sectors;
thinker_t thinkercap={.prev=&thinkercap,.next=&thinkercap};
boolean automapactive, menuactive;
int consoleplayer, gamemap;
gamestate_t gamestate=GS_LEVEL;
gameaction_t gameaction;
lumpinfo_t *lumpinfo;
unsigned int numlumps;
musicinfo_t S_music[NUMMUSIC];
int key_menu_activate=KEY_ESCAPE, key_menu_back=KEY_BACKSPACE;
int key_menu_abort='n', key_menu_up=KEY_UPARROW, key_menu_down=KEY_DOWNARROW;
int key_menu_left=KEY_LEFTARROW, key_menu_right=KEY_RIGHTARROW;
int key_menu_forward=KEY_ENTER, key_menu_confirm='y';
static bool active=true;
static unsigned boxes, large_boxes, text_count, control_pixels;
static char drawn_lines[20][HU_MAXLINELENGTH+1];
static int drawn_y[20];
boolean p4_doom_gc_active(void) { return active; }
void I_Error(char *error, ...) { (void)error; abort(); }
void S_StopSound(mobj_t *origin) { (void)origin; }
void P_RemoveMobj(mobj_t *origin) { (void)origin; }
void P_MobjThinker(mobj_t *origin) { (void)origin; abort(); }
void G_DeathMatchSpawnPlayer(int slot) { (void)slot; }
void AM_Stop(void) { automapactive=false; }
void M_StartControlPanel(void) { menuactive=true; }
void V_DrawFilledBox(int x,int y,int w,int h,int color)
{
    assert(x>=0 && y>=0 && w>0 && h>0 && x+w<=320 && y+h<=200);
    if(w==1 && h==1) {
        assert(color==0 || color==4);
        assert(x>=80 && x<142 && y>=9 && y<37);
        ++control_pixels;
        return;
    }
    assert(color==0); ++boxes; if(w>100) ++large_boxes;
}
void HUlib_initTextLine(hu_textline_t *line,int x,int y,patch_t **font,int start)
{
    (void)font; (void)start; memset(line,0,sizeof(*line)); line->x=x; line->y=y;
}
boolean HUlib_addCharToTextLine(hu_textline_t *line,char ch)
{
    assert(line->len<HU_MAXLINELENGTH); line->l[line->len++]=ch; return true;
}
void HUlib_drawTextLine(hu_textline_t *line,boolean cursor)
{
    (void)cursor; assert(text_count<20); drawn_y[text_count]=line->y;
    strcpy(drawn_lines[text_count++],line->l);
}
static bool contains(const char *text)
{
    for(unsigned i=0;i<text_count;++i) if(strcmp(drawn_lines[i],text)==0) return true;
    return false;
}
static void draw(void)
{
    boxes=large_boxes=text_count=control_pixels=0; P4_DoomArenaHUD();
}
int main(void)
{
    p4_doom_gc_engine_begin(4,1);
    draw();
    assert(boxes==0 && text_count==0 && control_pixels>0 && control_pixels<=120);
    const p4_doom_arena_t original=arena;
    ticcmd_t command={.forwardmove=12,.buttons=BT_ATTACK};
    const ticcmd_t untouched=command;
    p4_doom_arena_score_touch(true); draw();
    assert(large_boxes==1 && control_pixels>0 && contains("P1 0") && contains("P4 0"));
    p4_doom_arena_score_touch(true); draw(); /* Held touch never repeats. */
    assert(large_boxes==1);
    p4_doom_arena_score_touch(false); p4_doom_arena_score_touch(true); draw();
    assert(boxes==0 && control_pixels>0);
    assert(P4_DoomArenaMenuKey(KEY_PAUSE)); draw();
    assert(large_boxes==1 && !menuactive); /* Start inspects, never pauses. */
    P4_DoomArenaBuildCommand(&command);
    assert(memcmp(&command,&untouched,sizeof(command))==0);
    assert(memcmp(&arena,&original,sizeof(arena))==0);
    assert(P4_DoomArenaMenuKey(KEY_ESCAPE)); draw();
    assert(boxes==0 && !scores_open && panel_open && menuactive);
    assert(P4_DoomArenaMenuKey(KEY_PAUSE) && !scores_open);
    assert(P4_DoomArenaMenuKey(KEY_ESCAPE)); draw();
    assert(large_boxes==0 && !menuactive);
    /* Vote/break notifications survive hiding the score panel. */
    arena.vote_map=2; arena.players[0].active=false;
    draw(); assert(contains("MAP VOTE: OPEN MENU TO VOTE") && contains("TAKE A BREAK"));
    assert(large_boxes==0);
    arena.players[0].kills=1234; arena.players[2].active=false;
    arena.connected_mask &= (uint8_t)~8U;
    assert(P4_DoomArenaMenuKey(KEY_PAUSE)); draw();
    assert(contains("P1 BREAK") && contains("P3 BREAK") && contains("P4 LEFT"));
    for(unsigned i=0;i<text_count;++i)
        if(strcmp(drawn_lines[i],"TAKE A BREAK")==0) assert(drawn_y[i]>=108);
    arena.players[0].active=true; draw();
    assert(contains("P1 1234") && P4_DoomArenaFragCount(0)==99);
    assert(P4_DoomArenaCompleted()); draw();
    assert(large_boxes==0 && !scores_open && !score_touch_held);
    assert(P4_DoomArenaMenuKey(KEY_PAUSE));
    p4_doom_gc_engine_begin(2,6); draw(); assert(large_boxes==0);
    assert(P4_DoomArenaMenuKey(KEY_PAUSE));
    active=false; draw(); assert(boxes==0 && text_count==0 && !scores_open);
    assert(!P4_DoomArenaMenuKey(KEY_PAUSE)); /* Ordinary Doom retains pause. */
    p4_doom_arena_score_touch(true); assert(!scores_open);
    active=true; gamestate=GS_INTERMISSION; draw(); assert(boxes==0);
    puts("Arena score UI: hidden/edge toggle/controller/menu/reset/rules/prompts passed");
    return 0;
}
