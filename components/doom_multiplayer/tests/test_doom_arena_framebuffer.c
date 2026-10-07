// SPDX-License-Identifier: GPL-2.0-or-later
/* Device-independent framebuffer regression; see the companion Python runner. */
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include "i_swap.h"
#include "i_video.h"
#include "r_local.h"
#include "f_wipe.h"
#include "z_zone.h"
#include "../../../apps/console_os/main/doom_gc_engine.c"

patch_t *hu_font[HU_FONTSIZE];
player_t players[MAXPLAYERS];
boolean playeringame[MAXPLAYERS];
int numsectors;
sector_t *sectors;
thinker_t thinkercap={.prev=&thinkercap,.next=&thinkercap};
boolean automapactive, menuactive, inhelpscreens, viewactive=true;
boolean nodrawers, setsizeneeded, paused, testcontrols;
int consoleplayer, displayplayer, gamemap, gametic=1, testcontrols_mousespeed;
int viewheight, scaledviewwidth, viewwindowx, viewwindowy;
gamestate_t gamestate=GS_LEVEL, wipegamestate=GS_LEVEL;
gameaction_t gameaction;
lumpinfo_t *lumpinfo;
unsigned int numlumps;
musicinfo_t S_music[NUMMUSIC];
int key_menu_activate=KEY_ESCAPE, key_menu_back=KEY_BACKSPACE;
int key_menu_abort='n', key_menu_up=KEY_UPARROW, key_menu_down=KEY_DOWNARROW;
int key_menu_left=KEY_LEFTARROW, key_menu_right=KEY_RIGHTARROW;
int key_menu_forward=KEY_ENTER, key_menu_confirm='y';
static bool active=true;
static byte frame[SCREENWIDTH*SCREENHEIGHT], background[SCREENWIDTH*SCREENHEIGHT];
byte *I_VideoBuffer=frame;
static byte *dest_screen=frame, *background_buffer=background;
static vpatchclipfunc_t patchclip_callback;
static int dest_origin_y, dest_height=SCREENHEIGHT;
byte *tinttable, *xlatab;
static hu_stext_t w_message;
static hu_itext_t w_chat;
static hu_textline_t w_title;
static unsigned border_marks;

boolean p4_doom_gc_active(void) { return active; }
void I_Error(char *error, ...) { fprintf(stderr,"engine error: %s\n",error); abort(); }
void S_StopSound(mobj_t *origin) { (void)origin; }
void P_RemoveMobj(mobj_t *origin) { (void)origin; }
void P_MobjThinker(mobj_t *origin) { (void)origin; abort(); }
void G_DeathMatchSpawnPlayer(int slot) { (void)slot; }
void AM_Stop(void) { automapactive=false; }
void M_StartControlPanel(void) { menuactive=true; }
void V_MarkRect(int x,int y,int w,int h) { (void)x;(void)y; if(w==SCREENWIDTH && h==P4_DOOM_SCALE_Y(168)) ++border_marks; }
void V_DrawPatchDirect(int x,int y,patch_t *p) { V_DrawPatch(x,y,p); }
void HUlib_drawSText(hu_stext_t *text) { (void)text; }
void HUlib_drawIText(hu_itext_t *text) { (void)text; }
void HUlib_eraseSText(hu_stext_t *text) { (void)text; }
void HUlib_eraseIText(hu_itext_t *text) { (void)text; }
void HUlib_eraseTextLine(hu_textline_t *text) { (void)text; }
void R_ExecuteSetViewSize(void) { setsizeneeded=false; }
void R_FillBackScreen(void) { /* Already populated with a per-pixel pattern. */ }
void R_RenderPlayerView(player_t *player)
{
    (void)player;
    for(int y=viewwindowy;y<viewwindowy+viewheight;++y)
        for(int x=viewwindowx;x<viewwindowx+scaledviewwidth;++x)
            frame[y*SCREENWIDTH+x]=(byte)(50+(x+y)%83);
}
void ST_Drawer(boolean fullscreen, boolean refresh)
{
    if(!fullscreen && refresh)
        for(int y=P4_DOOM_SCALE_Y(168);y<SCREENHEIGHT;++y) for(int x=0;x<SCREENWIDTH;++x)
            frame[y*SCREENWIDTH+x]=(byte)(170+(x+y)%40);
}
void AM_Drawer(void) { memset(frame,42,SCREENWIDTH*P4_DOOM_SCALE_Y(168)); }
void WI_Drawer(void) { memset(frame,43,sizeof(frame)); }
void F_Drawer(void) { memset(frame,44,sizeof(frame)); }
void D_PageDrawer(void) { memset(frame,45,sizeof(frame)); }
void I_SetPalette(byte *palette) { (void)palette; }
void *W_CacheLumpName(char *name,int tag) { (void)name;(void)tag; abort(); }
void I_UpdateNoBlit(void) {}
void V_DrawMouseSpeedBox(int speed) { (void)speed; }
void M_Drawer(void) { if(menuactive) (void)P4_DoomArenaMenuDraw(); }
void NetUpdate(void) {}
static boolean frame_tail;
static unsigned frame_finishes;
/* No transport runs here, but ordinary presentation must bracket its tail. */
void P4_DoomNetSetFrameTail(boolean ordinary)
{ assert(ordinary!=frame_tail);frame_tail=ordinary; }
void I_FinishUpdate(void) { assert(frame_tail);++frame_finishes; }
int I_GetTime(void) { return 1; }
void I_Sleep(int time) { (void)time; }
int wipe_StartScreen(int x,int y,int width,int height) { (void)x;(void)y;(void)width;(void)height; abort(); }
int wipe_EndScreen(int x,int y,int width,int height) { (void)x;(void)y;(void)width;(void)height; abort(); }
int wipe_ScreenWipe(int type,int x,int y,int width,int height,int tics) { (void)type;(void)x;(void)y;(void)width;(void)height;(void)tics; abort(); }
#define DEH_String(value) (value)
#define SBARHEIGHT (SCREENHEIGHT-P4_DOOM_SCALE_Y(168))
#include "render_functions.h"

static void assert_frame(const byte *expected,const char *transition)
{
    unsigned mismatches=0;
    for(unsigned i=0;i<sizeof(frame);++i) if(frame[i]!=expected[i]) ++mismatches;
    if(mismatches) {
        fprintf(stderr,"FAIL %s width=%d: %u retained/wrong framebuffer pixels\n",transition,scaledviewwidth,mismatches);
        exit(1);
    }
}
static void set_view(int width,int height)
{
    scaledviewwidth=width; viewheight=height;
    viewwindowx=(SCREENWIDTH-width)/2; viewwindowy=width==SCREENWIDTH ? 0 : (P4_DOOM_SCALE_Y(168)-height)/2;
    setsizeneeded=true;
    for(unsigned i=0;i<sizeof(frame);++i) background[i]=(byte)(5+i%37);
    memcpy(frame,background,sizeof(frame));
    ST_Drawer(false,true);
}
int main(void)
{
    /* Both compact labels are transparent pixel art: white foreground, black
     * shadow, and untouched gameplay in the spaces between the letters. */
    static byte controls[2][SCREENWIDTH*SCREENHEIGHT];
    for(unsigned label=0;label<2;++label) {
        memset(frame,77,sizeof(frame)); scores_open=label!=0;
        draw_score_control();
        unsigned white=0,shadow=0,untouched=0;
        for(unsigned py=0;py<SCREENHEIGHT;++py) for(unsigned px=0;px<SCREENWIDTH;++px) {
            const byte pixel=frame[py*SCREENWIDTH+px];
            if(px<P4_DOOM_SCALE_X(101) || px>=P4_DOOM_SCALE_X(121) ||
               py<P4_DOOM_SCALE_Y(20) || py>=P4_DOOM_SCALE_Y(26)) {
                assert(pixel==77);
                continue;
            }
            controls[label][py*SCREENWIDTH+px]=pixel;
            if(pixel==4) ++white;
            else if(pixel==0) ++shadow;
            else { assert(pixel==77); ++untouched; }
        }
        assert(white>=40 && shadow>0 && untouched>0);
        assert(frame[P4_DOOM_SCALE_Y(20)*SCREENWIDTH+P4_DOOM_SCALE_X(104)]==77); /* First inter-letter gap. */
        /* The leading S has a middle bar and open lower-left; C does not. */
        assert(frame[P4_DOOM_SCALE_Y(22)*SCREENWIDTH+P4_DOOM_SCALE_X(102)]==(label ? 0 : 4));
        assert(frame[P4_DOOM_SCALE_Y(23)*SCREENWIDTH+P4_DOOM_SCALE_X(101)]==(label ? 4 : 77));
    }
    assert(memcmp(controls[0],controls[1],sizeof(controls[0]))!=0);
    /* A valid 3x5 Doom column patch: real text layout and patch drawing. */
    static struct { int16_t width,height,left,top; int32_t columns[3]; byte pixels[30]; } glyph;
    glyph.width=3; glyph.height=5;
    for(unsigned x=0;x<3;++x) {
        glyph.columns[x]=(int32_t)(20+10*x);
        byte *column=&glyph.pixels[x*10]; column[0]=0; column[1]=5;
        for(unsigned y=0;y<5;++y) column[3+y]=(byte)(220+x+y);
        column[9]=255;
    }
    for(unsigned i=0;i<HU_FONTSIZE;++i) hu_font[i]=(patch_t *)&glyph;
    HUlib_initTextLine(&w_title,0,150,hu_font,HU_FONTSTART);
    static byte plain[sizeof(frame)], expected[sizeof(frame)];
    const int widths[]={320,320,288,192,96};
    const int heights[]={200,168,144,96,48};
    for(unsigned size=0;size<sizeof(widths)/sizeof(widths[0]);++size) {
        p4_doom_gc_engine_begin(4,1); set_view(P4_DOOM_SCALE_X(widths[size]),P4_DOOM_SCALE_Y(heights[size]));
        /* Establish the no-overlay scene and let vanilla's three border
         * refresh frames expire, as on a device left running for a while. */
        active=false; for(unsigned i=0;i<5;++i) D_Display();
        memcpy(plain,frame,sizeof(plain));
        active=true; memcpy(frame,plain,sizeof(frame)); P4_DoomArenaHUD();
        memcpy(expected,frame,sizeof(expected));
        /* Only the 20x6 label and its shadow may cover gameplay. The old
         * 62x28 opaque button must not return; untouched pixels are exact. */
        unsigned control_changed=0;
        for(unsigned py=0;py<SCREENHEIGHT;++py) for(unsigned px=0;px<SCREENWIDTH;++px) {
            if(frame[py*SCREENWIDTH+px]==plain[py*SCREENWIDTH+px]) continue;
            assert(px>=P4_DOOM_SCALE_X(101) && px<P4_DOOM_SCALE_X(121) &&
                   py>=P4_DOOM_SCALE_Y(20) && py<P4_DOOM_SCALE_Y(26));
            ++control_changed;
        }
        assert(control_changed>0 && control_changed<=(P4_DOOM_SCALE_X(121)-P4_DOOM_SCALE_X(101))*
               (P4_DOOM_SCALE_Y(26)-P4_DOOM_SCALE_Y(20)));
        for(unsigned i=0;i<5;++i) D_Display();
        assert_frame(expected,"score target remains whole");
        setsizeneeded=true; D_Display();
        assert_frame(expected,"view resize preserves whole score target");
        assert(P4_DoomArenaMenuKey(KEY_PAUSE)); D_Display();
        assert(P4_DoomArenaMenuKey(KEY_PAUSE)); D_Display();
        assert_frame(expected,"score open -> close next frame");
        p4_doom_arena_score_touch(true); D_Display();
        p4_doom_arena_score_touch(false); p4_doom_arena_score_touch(true); D_Display();
        assert_frame(expected,"score touch open -> close next frame");
        p4_doom_arena_score_touch(false);
        arena.players[0].active=false; D_Display();
        static byte on_break[sizeof(frame)];
        memcpy(on_break,frame,sizeof(frame));
        assert(P4_DoomArenaMenuKey(KEY_PAUSE)); D_Display();
        assert(P4_DoomArenaMenuKey(KEY_PAUSE)); D_Display();
        assert_frame(on_break,"break text moves after scores close");
        arena.players[0].active=true; D_Display();
        assert_frame(expected,"break -> live next frame");
        arena.vote_map=2; D_Display(); arena.vote_map=0; D_Display();
        assert_frame(expected,"vote -> no vote next frame");
        assert(P4_DoomArenaMenuKey(KEY_PAUSE)); D_Display();
        assert(P4_DoomArenaMenuKey(KEY_ESCAPE)); D_Display();
        assert(P4_DoomArenaMenuKey(KEY_ESCAPE)); D_Display();
        assert_frame(expected,"scores -> Back menu -> live next frame");
        /* Opening the automap is a complete scene redraw, then returning
         * restores the old retained border before compositing Arena UI. */
        automapactive=true; D_Display(); automapactive=false; D_Display();
        assert_frame(expected,"automap -> view next frame");
        active=false; for(unsigned i=0;i<4;++i) D_Display();
        const unsigned before=border_marks; D_Display();
        assert(border_marks==before); /* Ordinary Doom keeps its redraw policy. */
    }
    assert(!frame_tail && frame_finishes>0);
    printf("Arena framebuffer %dx%d: ",SCREENWIDTH,SCREENHEIGHT);
    puts(" score touch/Start, break, vote, Back menu and automap transitions passed across 5 view sizes");
    return 0;
}
