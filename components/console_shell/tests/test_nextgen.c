// SPDX-License-Identifier: MIT
#include "console/shell.h"
#include "console/game_art.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static console_app_descriptor_t apps[32];
static char titles[20][16];
static console_shell_t shell;
static uint16_t *pixels;
static const size_t stride=1287;
static void render(void)
{
    assert(console_shell_render_native_cached_rgb565(&shell,pixels,stride));
    for(size_t y=0;y<720;++y)for(size_t x=1280;x<stride;++x)assert(pixels[y*stride+x]==0xdead);
    for(size_t x=0;x<stride;++x)assert(pixels[720*stride+x]==0xdead);
}
static console_shell_action_t tap(uint16_t x,uint16_t y)
{
    console_shell_contact_t c={.x=x,.y=y};
    assert(console_shell_handle_touch(&shell,true,&c,1).type==CONSOLE_ACTION_NONE);
    return console_shell_handle_touch(&shell,true,NULL,0);
}
static console_shell_action_t key(uint32_t b)
{
    (void)console_shell_handle_buttons(&shell,0);
    return console_shell_handle_buttons(&shell,b);
}
static void drag(uint16_t x,uint16_t y,uint16_t end_y)
{
    console_shell_contact_t c={.x=x,.y=y};
    assert(console_shell_handle_touch(&shell,true,&c,1).type==CONSOLE_ACTION_NONE);
    c.y=end_y;assert(console_shell_handle_touch(&shell,true,&c,1).type==CONSOLE_ACTION_NONE);
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
}
static const char *capture_prefix;
static void capture(const char *name)
{
    render();if(!capture_prefix)return;
    /* Save an authoritative full frame and verify the production cached path
     * preserved every pixel, including navigation and status chrome. */
    uint16_t *frame=malloc(1280U*720U*sizeof(*frame));assert(frame);
    console_shell_t reference=shell;
    assert(console_shell_render_rgb565(&reference,frame,1280U));
    for(size_t y=0;y<720U;++y)
        assert(memcmp(pixels+y*stride,frame+y*1280U,1280U*sizeof(*frame))==0);
    char path[1024];(void)snprintf(path,sizeof(path),"%s-%s.ppm",capture_prefix,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1280 720\n255\n");
    for(size_t y=0;y<720U;++y)for(size_t x=0;x<1280U;++x){uint16_t v=frame[y*1280U+x];
        const unsigned char rgb[3]={(unsigned char)(((v>>11U)&31U)*255U/31U),
            (unsigned char)(((v>>5U)&63U)*255U/63U),(unsigned char)((v&31U)*255U/31U)};
        assert(fwrite(rgb,1,3,f)==3);}
    assert(fclose(f)==0);free(frame);
}
static void init(void)
{
    memset(apps,0,sizeof(apps));
    for(unsigned i=0;i<20U;++i){
        apps[i].id=100U+i;(void)snprintf(titles[i],sizeof(titles[i]),"Game %02u",i);
        apps[i].title=titles[i];
        apps[i].page=CONSOLE_PAGE_EXTERNAL;apps[i].enabled=true;
    }
    apps[0].title="Byte Buddy";
    apps[20]=(console_app_descriptor_t){.id=90,.title="Files",.page=CONSOLE_PAGE_FILES,.enabled=true};
    apps[21]=(console_app_descriptor_t){.id=91,.title="Game manager",.page=CONSOLE_PAGE_GAMES,.enabled=true};
    apps[22]=(console_app_descriptor_t){.id=92,.title="Multiplayer",.page=CONSOLE_PAGE_MULTIPLAYER,.enabled=true};
    for(unsigned i=0;i<23U;++i){apps[i].subtitle="Test";apps[i].folder_path="";}
    assert(console_shell_init(&shell,apps,23));
    const console_shell_runtime_info_t rt={.game_storage_state=CONSOLE_STORAGE_READY,.control_panel_enabled=true,
        .battery_supported=true,.battery_sample_valid=false,.battery_millivolts=1234,
        .boot_volume_step=3,.game_volume_step=7,.sd_card_storage=true};
    console_shell_set_runtime_info(&shell,&rt);render();
}

static void check_featured_game(void)
{
    init();
    apps[1].title="Blast Circuit";apps[2].title="Wacky Wheels";
    capture("blast-featured");
    assert(tap(420,360).app_id==101U);
    shell.page=CONSOLE_PAGE_HOME;apps[1].enabled=false;render();
    assert(tap(420,360).app_id==102U);
    puts("TAB5 FEATURED PASS blast_first=1 wacky_fallback=1 byte_not_promoted=1");
}

static void check_disabled_reason(void)
{
    init();
    char reason[CONSOLE_SHELL_SUBTITLE_MAX_BYTES];
    memset(reason, 'R', sizeof(reason));
    apps[0].disabled_reason=reason;
    console_shell_t validation;
    assert(!console_shell_init(&validation,apps,23U));
    reason[sizeof(reason)-1U]='\0';
    assert(console_shell_init(&validation,apps,23U));
    apps[0].disabled_reason=NULL;
    assert(console_shell_init(&validation,apps,23U));
    apps[0].enabled=false;shell.home_all_programs=true;render();
    const size_t words=721U*stride;
    uint16_t *fallback=malloc(words*sizeof(*fallback));assert(fallback);
    uint16_t *reference=malloc(1280U*720U*sizeof(*reference));assert(reference);
    memcpy(fallback,pixels,words*sizeof(*fallback));
    (void)strcpy(reason,"Update game for Tab5");apps[0].disabled_reason=reason;
    uint32_t full_frames=shell.native_home_full_frames;
    shell.ng_library_scroll=1;render();
    assert(shell.native_home_full_frames==full_frames+1U);
    console_shell_t fresh=shell;
    assert(console_shell_render_rgb565(&fresh,reference,1280U));
    for(size_t y=0;y<720U;++y)
        assert(memcmp(pixels+y*stride,reference+y*1280U,1280U*sizeof(*reference))==0);
    assert(tap(400,230).type==CONSOLE_ACTION_NONE);
    assert(shell.page==CONSOLE_PAGE_HOME&&shell.active_app_id==0U);
    /* The cache must track contents even when the catalog reuses its buffer. */
    (void)strcpy(reason,"Install an update");full_frames=shell.native_home_full_frames;
    shell.ng_library_scroll=2;render();
    assert(shell.native_home_full_frames==full_frames+1U);
    fresh=shell;assert(console_shell_render_rgb565(&fresh,reference,1280U));
    for(size_t y=0;y<720U;++y)
        assert(memcmp(pixels+y*stride,reference+y*1280U,1280U*sizeof(*reference))==0);
    apps[0].disabled_reason=NULL;shell.ng_library_scroll=0;render();
    assert(memcmp(fallback,pixels,words*sizeof(*fallback))==0);
    /* The disabled featured card also explains itself, and cannot launch. */
    assert(console_shell_init(&shell,apps,1U));render();
    memcpy(fallback,pixels,words*sizeof(*fallback));
    apps[0].disabled_reason=reason;render();
    assert(memcmp(fallback,pixels,words*sizeof(*fallback))!=0);
    assert(tap(420,360).type==CONSOLE_ACTION_NONE);
    assert(shell.page==CONSOLE_PAGE_HOME&&shell.active_app_id==0U);
    assert(key(CONSOLE_BUTTON_ACCEPT).type!=CONSOLE_ACTION_LAUNCH);
    assert(shell.page!=CONSOLE_PAGE_EXTERNAL&&shell.active_app_id!=apps[0].id);
    /* A catalog change between press and release must not launch a stale tile. */
    apps[0].enabled=true;assert(console_shell_init(&shell,apps,1U));render();
    const console_shell_contact_t down={.x=420,.y=360};
    assert(console_shell_handle_touch(&shell,true,&down,1U).type==CONSOLE_ACTION_NONE);
    apps[0].enabled=false;
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    assert(shell.page==CONSOLE_PAGE_HOME&&shell.active_app_id==0U);
    free(fallback);free(reference);init();
    puts("TAB5 DISABLED REASON PASS bounded=1 cache_contents=1 fallback_unchanged=1 no_launch=1");
}

static void check_game_covers(void)
{
    init();
    static uint8_t custom_pixels[128U*72U];
    static uint16_t custom_palette[256];
    memset(custom_pixels,0,sizeof(custom_pixels));
    custom_palette[0]=0xF81FU;
    apps[0].title="Byte Buddy";
    apps[0].icon_pixels=custom_pixels;
    apps[0].icon_palette=custom_palette;
    render();
    /* A new cartridge cover wins over the legacy high-resolution illustration. */
    assert(pixels[420U*stride+1000U]==0xF81FU);
    apps[0].id=1U;apps[0].title="Doom";apps[0].subtitle="Shareware 1.9";
    apps[0].folder_path="GAMES/SHOOTERS";
    apps[0].icon_pixels=console_doom_icon_pixels;
    apps[0].icon_palette=console_doom_icon_palette;
    apps[0].cover=&console_doom_cover;
    assert(console_doom_cover.width==640U&&console_doom_cover.height==360U);
    assert(tap(420,360).type==CONSOLE_ACTION_LAUNCH);
    const uint16_t tile_focus=shell.ng_focus;
    shell.ng_focus_ms=137U;
    shell.page=CONSOLE_PAGE_EXTERNAL;shell.active_app_id=1U;
    capture("doom-launch");
    assert(shell.ng_focus==tile_focus&&shell.ng_focus_ms==137U);
    /* A rejected launch returns to the same tile: A retries this game. */
    shell.page=CONSOLE_PAGE_HOME;
    assert(key(CONSOLE_BUTTON_ACCEPT).app_id==1U);
    shell.page=CONSOLE_PAGE_EXTERNAL;
    /* Center of the cover samples actual art, never the generic gamepad. */
    const uint16_t doom_center=pixels[356U*stride+584U];
    assert(doom_center!=0xF81FU);
    apps[0].id=14U;apps[0].title="Chex Quest";
    apps[0].icon_pixels=console_chex_icon_pixels;
    apps[0].icon_palette=console_chex_icon_palette;
    apps[0].cover=&console_chex_cover;
    shell.active_app_id=14U;capture("chex-launch");
    assert(pixels[356U*stride+584U]!=doom_center);
    /* Cover rendering must not navigate, launch twice, or change selection. */
    assert(shell.page==CONSOLE_PAGE_EXTERNAL&&shell.active_app_id==14U);
    shell.active_app_id=0xffffffffU;render();
    init();
}

static void check_loading_progress(void)
{
    init();
    shell.page=CONSOLE_PAGE_EXTERNAL;shell.active_app_id=apps[0].id;
    render();
    uint16_t original[306];
    memcpy(original,pixels+435U*stride+910U,sizeof(original));
    const uint16_t focus=shell.ng_focus;
    shell.loading=(console_shell_loading_info_t){
        .active=true,.progress_visible=true,.elapsed_seconds=12,
        .title="Doom Arena by Game Changers",.stage="1/3 Check files",
        .detail="16.7 / 33.4 MB  50%",
    };
    render();
    const uint16_t track=pixels[435U*stride+910U];
    shell.loading.progress_percent=50;capture("arena-loading-files");
    const uint16_t fill=pixels[435U*stride+910U];
    assert(fill!=track);
    for(size_t x=0;x<306U;++x)
        assert(pixels[435U*stride+910U+x]==(x<153U?fill:track));
    shell.loading.progress_percent=255;render();
    for(size_t x=0;x<306U;++x)assert(pixels[435U*stride+910U+x]==fill);
    shell.loading.progress_visible=false;
    (void)snprintf(shell.loading.stage,sizeof(shell.loading.stage),"1/3 Check WAD structure");
    (void)snprintf(shell.loading.detail,sizeof(shell.loading.detail),"File 1 of 16");
    capture("arena-loading-structure");
    assert(memcmp(original,pixels+435U*stride+910U,sizeof(original))==0);
    assert(shell.page==CONSOLE_PAGE_EXTERNAL&&shell.active_app_id==apps[0].id&&shell.ng_focus==focus);
    shell.loading=(console_shell_loading_info_t){0};
    render();
    assert(memcmp(original,pixels+435U*stride+910U,sizeof(original))==0);
    puts("TAB5 LOADING PASS measured_bar=1 structure_indeterminate=1 bounded=1 no_navigation=1");
}

static void check_loading_region_cache(void)
{
    init();
    shell.page=CONSOLE_PAGE_EXTERNAL;shell.active_app_id=apps[0].id;
    shell.loading=(console_shell_loading_info_t){
        .active=true,.progress_visible=true,
        .title="Doom Arena by Game Changers",.stage="1/3 Check files",
        .detail="0.0 / 33.4 MB  0%",
    };
    uint16_t *reference=malloc(1280U*720U*sizeof(*reference));assert(reference);
    uint16_t *before=malloc(1280U*720U*sizeof(*before));assert(before);
    for(unsigned frame=0;frame<24U;++frame){
        for(size_t y=0;y<720U;++y)memcpy(before+y*1280U,pixels+y*stride,1280U*sizeof(*before));
        bool full=frame==0U;
        shell.loading.progress_percent=(uint8_t)(frame*13U);
        shell.loading.elapsed_seconds=frame*7U;
        shell.loading.progress_visible=frame%3U!=1U;
        (void)snprintf(shell.loading.stage,sizeof(shell.loading.stage),"%s",
            frame%3U==1U?"1/3 Check WAD structure":frame%3U==2U?"1/3 Files verified":"1/3 Check files");
        (void)snprintf(shell.loading.detail,sizeof(shell.loading.detail),"%s",
            frame%2U?"File 1 of 16":"33.4 / 33.4 MB  100%");
        if(frame==6U){shell.runtime.game_volume_step=0U;full=true;}
        if(frame==8U){shell.active_app_id=apps[1].id;full=true;}
        if(frame==10U){(void)snprintf(shell.loading.title,sizeof(shell.loading.title),"Other title");full=true;}
        if(frame==12U){console_shell_invalidate_native_cache(&shell);full=true;}
        if(frame==14U){shell.active_app_id=UINT32_MAX;full=true;}
        if(frame==15U){shell.active_app_id=apps[0].id;full=true;}
        if(frame==17U){shell.loading.active=false;full=true;}
        if(frame==18U){shell.loading.active=true;full=true;}
        if(frame==20U){shell.page=CONSOLE_PAGE_HOME;full=true;}
        if(frame==21U){shell.page=CONSOLE_PAGE_EXTERNAL;full=true;}
        render();
        console_shell_native_update_t update;assert(console_shell_get_native_update(&shell,&update));
        assert(update.kind==(full?CONSOLE_SHELL_NATIVE_UPDATE_FULL:CONSOLE_SHELL_NATIVE_UPDATE_REGION));
        if(!full)assert(update.x==910U&&update.y==340U&&update.width==306U&&update.height==164U);
        console_shell_t exact=shell;
        assert(console_shell_render_rgb565(&exact,reference,1280U));
        for(size_t y=0;y<720U;++y)for(size_t x=0;x<1280U;++x){
            assert(pixels[y*stride+x]==reference[y*1280U+x]);
            if(!full&&(x<910U||x>=1216U||y<340U||y>=504U))
                assert(pixels[y*stride+x]==before[y*1280U+x]);
        }
    }
    free(before);free(reference);
    puts("TAB5 LOADING REGION PASS pixel_exact=24 bounded_damage=1 invalidation=1 no_stale_bar=1");
}

static void check_scroll_cache(void)
{
    init();(void)tap(100,265);
    uint16_t *reference=malloc(721U*stride*sizeof(*reference));assert(reference);
    double cached_seconds=0,full_seconds=0;
    for(unsigned files=0;files<2U;++files){
        if(files){shell.page=CONSOLE_PAGE_FILES;shell.files.available=true;shell.files.entry_count=12;
            for(unsigned i=0;i<12;++i){snprintf(shell.files.entries[i].label,sizeof(shell.files.entries[i].label),"File %u",i);shell.files.entries[i].source_index=i;}}
        shell.ng_scroll_kind=files?2U:1U;shell.ng_focus=0;shell.ng_focus_ms=0;
        for(unsigned frame=0;frame<120;++frame){
            int offset=frame<60?(int)frame*7:(119-(int)frame)*7;
            if(files)shell.ng_file_scroll=offset;else shell.ng_library_scroll=offset;
            if(frame==20)apps[0].title="Changed title";
            if(frame==30)shell.runtime.battery_percent=51;
            if(frame==35)shell.runtime.game_volume_step=3;
            if(frame==40)shell.file_selected_index=3;
            if(frame==50)shell.ng_large_text=true;
            if(frame==70)console_shell_invalidate_native_cache(&shell);
            console_shell_t expected=shell;
            memcpy(reference,pixels,721U*stride*sizeof(*pixels));
            clock_t begin=clock();render();cached_seconds+=(double)(clock()-begin)/CLOCKS_PER_SEC;
            begin=clock();assert(console_shell_render_rgb565(&expected,reference,stride));full_seconds+=(double)(clock()-begin)/CLOCKS_PER_SEC;
            for(size_t y=0;y<720;++y)for(size_t x=0;x<stride;++x)if(pixels[y*stride+x]!=reference[y*stride+x]){
                fprintf(stderr,"CACHE mismatch files=%u frame=%u x=%zu y=%zu actual=%04x expected=%04x\n",files,frame,x,y,pixels[y*stride+x],reference[y*stride+x]);abort();}
        }
    }
    assert(shell.native_home_scroll_blit_frames>200);
    printf("TAB5 SCROLL CACHE PASS frames=240 pixel_exact=1 cached_ms=%.2f full_ms=%.2f speedup=%.2f\n",cached_seconds*1000,full_seconds*1000,full_seconds/cached_seconds);
    free(reference);
}
static void check_scroll_motion(void)
{
    init();(void)tap(100,265);
    console_shell_contact_t c={.x=900,.y=550};
    (void)console_shell_handle_touch(&shell,true,&c,1);
    for(unsigned i=0;i<5;++i){(void)console_shell_advance(&shell,16);c.y-=24;
        (void)console_shell_handle_touch(&shell,true,&c,1);}
    (void)console_shell_handle_touch(&shell,true,NULL,0);
    assert(shell.ng_library_scroll==120&&shell.ng_glide_kind==1);
    console_shell_t fine=shell;
    int last=shell.ng_library_scroll;
    for(unsigned i=0;i<40;++i){(void)console_shell_advance(&shell,16);
        assert(shell.ng_library_scroll>=last&&shell.ng_library_scroll-last<=24);last=shell.ng_library_scroll;
        for(unsigned j=0;j<4;++j)(void)console_shell_advance(&fine,4);}
    assert(shell.ng_glide_kind==0&&shell.ng_library_scroll>200&&shell.ng_library_scroll==fine.ng_library_scroll);
    /* Stopping a fling cannot activate the tile under the stopping finger. */
    shell.ng_glide_kind=1;shell.ng_velocity_q16=65536;
    assert(tap(350,240).type==CONSOLE_ACTION_NONE);assert(!shell.ng_glide_kind);
    /* Slow/stale release, reduced motion, invalid touch and long stalls stop motion. */
    shell.ng_glide_kind=1;shell.ng_reduce_motion=true;(void)console_shell_advance(&shell,16);assert(!shell.ng_glide_kind);
    shell.ng_reduce_motion=false;shell.ng_glide_kind=1;(void)console_shell_advance(&shell,500);assert(!shell.ng_glide_kind);
    shell.ng_glide_kind=1;(void)console_shell_handle_touch(&shell,false,NULL,0);assert(!shell.ng_glide_kind);
    (void)console_shell_handle_touch(&shell,true,NULL,0);
    c.y=550;(void)console_shell_handle_touch(&shell,true,&c,1);(void)console_shell_advance(&shell,16);
    c.y=500;(void)console_shell_handle_touch(&shell,true,&c,1);(void)console_shell_advance(&shell,120);
    (void)console_shell_handle_touch(&shell,true,NULL,0);assert(!shell.ng_glide_kind);
    puts("TAB5 SCROLL MOTION PASS cadence_independent=1 bounded=1 stop_touch_safe=1 stale_cancel=1");
}
static void check_catalog_grouping(void)
{
    init();
    for(unsigned i=0;i<20U;++i)apps[i].folder_path="GAMES/ARCADE";
    apps[0].folder_path="GAMES/WIP";
    apps[1].folder_path="GAMES/OPTIONAL";
    apps[20]=(console_app_descriptor_t){.id=90,.title="Calculator",.subtitle="Tool",.folder_path="SYSTEM/TOOLS",.page=CONSOLE_PAGE_EXTERNAL,.enabled=true};
    apps[21]=(console_app_descriptor_t){.id=91,.title="Input Monitor",.subtitle="Test",.folder_path="SYSTEM/TESTS",.page=CONSOLE_PAGE_EXTERNAL,.enabled=true};
    apps[22]=(console_app_descriptor_t){.id=92,.title="Sound & Motion",.subtitle="Test",.folder_path="SYSTEM/TESTS",.page=CONSOLE_PAGE_EXTERNAL,.enabled=true};
    (void)tap(20,265);render();
    /* Utilities cannot increase the Games list or become game categories. */
    drag(1242,205,625);assert(shell.ng_library_scroll==1006);
    (void)tap(850,115);assert(shell.ng_categories);capture("catalog-categories");
    assert(tap(720,335).type==CONSOLE_ACTION_NONE&&!shell.ng_categories);
    assert(shell.ng_category[0]=='\0'); /* No fifth (System) category. */
    (void)tap(850,115);(void)tap(1010,265);
    assert(strcmp(shell.ng_category,"GAMES/OPTIONAL")==0);
    assert(tap(340,240).app_id==101U);
    (void)tap(850,115);(void)tap(440,335);
    assert(strcmp(shell.ng_category,"GAMES/WIP")==0);
    capture("catalog-wip");assert(tap(340,240).app_id==100U);
    shell.page=CONSOLE_PAGE_SYSTEM;shell.ng_categories=false;shell.ng_focus=0;render();
    capture("catalog-system-tools");
    for(unsigned i=0;i<3U;++i){
        shell.page=CONSOLE_PAGE_SYSTEM;
        const console_shell_action_t action=tap((uint16_t)(420U+i*334U),585);
        assert(action.type==CONSOLE_ACTION_LAUNCH&&action.app_id==90U+i);
    }
    puts("TAB5 CATALOG PASS: utility exclusion, utility launch access, WIP and Optional categories");
}
static void multiplayer_fixture(void)
{
    init();
    console_shell_runtime_info_t rt=shell.runtime;
    rt.multiplayer_core_ready=true;
    rt.multiplayer_settings_editable=true;
    rt.multiplayer_game_ready=true;
    rt.multiplayer_transport_ready=true;
    rt.multiplayer_game_is_doom=true;
    rt.multiplayer_game_count=12;
    rt.multiplayer_game_selection=0;
    rt.multiplayer_transport_kind=2;
    rt.multiplayer_lobby_action_enabled=true;
    rt.multiplayer_games[0]=(console_multiplayer_game_display_t){"DOOM",true,7};
    rt.multiplayer_games[1]=(console_multiplayer_game_display_t){"CHEX QUEST",false,7};
    rt.multiplayer_games[2]=(console_multiplayer_game_display_t){"Doom Arena by Game Changers",true,4};
    /* These are the nine native entries reported by both Tab5 registries.
     * Production registry-to-runtime coverage lives in the inventory test. */
    static const char *const installed[]={"Blast Circuit","Checkers","Color Clash",
        "Air Hockey","Rummy 500","Yahtzee","Texas Hold'em","Tide Maze","Wacky Wheels"};
    for(unsigned i=0;i<9U;++i){
        (void)snprintf(rt.multiplayer_games[i+3U].title,sizeof(rt.multiplayer_games[i+3U].title),"%s",installed[i]);
        rt.multiplayer_games[i+3U].available=true;
        rt.multiplayer_games[i+3U].transport_mask=7;
    }
    memcpy(rt.multiplayer_game_title,"DOOM",sizeof("DOOM"));
    console_shell_set_runtime_info(&shell,&rt);
    assert(tap(100,350).type==CONSOLE_ACTION_PAGE_CHANGED);
    assert(shell.page==CONSOLE_PAGE_MULTIPLAYER);
}
static void check_multiplayer_hierarchy(void)
{
    multiplayer_fixture();
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_GAME);
    capture("multiplayer-games");
    assert(tap(1010,254).type==CONSOLE_ACTION_NONE); /* Missing optional game. */
    assert(tap(1000,635).type==CONSOLE_ACTION_NONE&&shell.ng_list_first==0);
    /* Every installed game is visible and selects its original registry index. */
    for(unsigned i=3;i<12U;++i){
        const console_shell_action_t selected=tap((uint16_t)(506U+(i%2U)*506U),
            (uint16_t)(254U+(i/2U)*56U));
        assert(selected.type==CONSOLE_ACTION_MULTIPLAYER_GAME_SELECT&&selected.multiplayer_game_selection==i);
        assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_TRANSPORT);
        assert(key(CONSOLE_BUTTON_BACK).type==CONSOLE_ACTION_PAGE_CHANGED);
        assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_GAME);
    }
    console_shell_action_t action=tap(506,310);
    assert(action.type==CONSOLE_ACTION_MULTIPLAYER_GAME_SELECT);
    assert(action.multiplayer_game_selection==2);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_TRANSPORT);
    shell.runtime.multiplayer_game_selection=2;
    shell.runtime.multiplayer_game_is_arena=true;
    (void)snprintf(shell.runtime.multiplayer_game_title,sizeof(shell.runtime.multiplayer_game_title),"Doom Arena by Game Changers");
    capture("multiplayer-connection");
    assert(tap(750,360).type==CONSOLE_ACTION_NONE); /* Arena needs Wi-Fi. */
    assert(tap(750,442).type==CONSOLE_ACTION_NONE);
    action=tap(750,524);
    assert(action.type==CONSOLE_ACTION_MULTIPLAYER_TRANSPORT_SELECT);
    assert(action.multiplayer_transport_kind==2);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_ROLE);
    capture("multiplayer-role");
    assert(tap(1000,402).type==CONSOLE_ACTION_PAGE_CHANGED);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_JOIN);
    shell.runtime.multiplayer_lobby_count=1;
    shell.runtime.multiplayer_lobby_selection=1;
    shell.runtime.multiplayer_lobbies[0]=(console_multiplayer_lobby_display_t){
        .session_id=123,.players_present=1,.player_capacity=4,
        .game_available=true,.game_title="ARENA"};
    capture("multiplayer-rooms");
    action=tap(750,342);
    assert(action.type==CONSOLE_ACTION_MULTIPLAYER_LOBBY_SELECT);
    assert(action.multiplayer_lobby_selection==1);
    assert(action.multiplayer_lobby_session_id==123);
    assert(tap(750,634).type==CONSOLE_ACTION_MULTIPLAYER_JOIN_LOBBY);
    shell.runtime.multiplayer_lobby_phase=CONSOLE_MULTIPLAYER_LOBBY_JOINING;
    shell.runtime.multiplayer_settings_editable=false;
    capture("multiplayer-joining");
    assert(tap(750,634).type==CONSOLE_ACTION_NONE);
    console_shell_runtime_info_t rt=shell.runtime;
    rt.multiplayer_lobby_phase=CONSOLE_MULTIPLAYER_LOBBY_CONNECTED;
    rt.multiplayer_lobby_ready=true;rt.multiplayer_can_start=true;
    rt.multiplayer_lobby_is_host=false;
    console_shell_set_runtime_info(&shell,&rt);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_JOIN);
    capture("multiplayer-connected");
    assert(tap(750,634).type==CONSOLE_ACTION_NONE); /* Only host starts. */
    assert(tap(100,350).type==CONSOLE_ACTION_NONE); /* Active rail preserves room. */
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_JOIN);
    assert(key(CONSOLE_BUTTON_BACK).type==CONSOLE_ACTION_MULTIPLAYER_LOBBY_RESET);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_ROLE);
    assert(key(CONSOLE_BUTTON_BACK).type==CONSOLE_ACTION_PAGE_CHANGED);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_TRANSPORT);
    assert(key(CONSOLE_BUTTON_BACK).type==CONSOLE_ACTION_PAGE_CHANGED);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_GAME);
    assert(key(CONSOLE_BUTTON_BACK).type==CONSOLE_ACTION_PAGE_CHANGED);
    assert(shell.page==CONSOLE_PAGE_HOME);

    /* Controller-only setup follows the same enabled choices as touch. */
    multiplayer_fixture();
    action=key(CONSOLE_BUTTON_ACCEPT);
    assert(action.type==CONSOLE_ACTION_MULTIPLAYER_GAME_SELECT&&action.multiplayer_game_selection==0);
    action=key(CONSOLE_BUTTON_ACCEPT);
    assert(action.type==CONSOLE_ACTION_MULTIPLAYER_TRANSPORT_SELECT&&action.multiplayer_transport_kind==0);
    action=key(CONSOLE_BUTTON_ACCEPT);
    assert(action.type==CONSOLE_ACTION_PAGE_CHANGED&&shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_HOST);
    assert(tap(750,524).type==CONSOLE_ACTION_PAGE_CHANGED);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS);
    action=tap(1200,350);
    assert(action.type==CONSOLE_ACTION_MULTIPLAYER_CONFIGURE&&action.multiplayer_option==CONSOLE_MULTIPLAYER_OPTION_MODE);
    shell.runtime.multiplayer_game_is_arena=true;shell.ng_list_first=0;
    shell.runtime.multiplayer_game_selection=2;
    shell.runtime.multiplayer_episode=1;shell.runtime.multiplayer_map=1;
    capture("multiplayer-arena-settings");
    action=tap(1200,350);
    assert(action.type==CONSOLE_ACTION_MULTIPLAYER_CONFIGURE&&action.multiplayer_option==CONSOLE_MULTIPLAYER_OPTION_MAP);
    assert(tap(1200,430).type==CONSOLE_ACTION_NONE);
    shell.runtime.multiplayer_game_is_arena=false;shell.runtime.multiplayer_game_is_doom=false;
    shell.runtime.multiplayer_dice_available=true;
    action=tap(1200,350);
    assert(action.type==CONSOLE_ACTION_MULTIPLAYER_CONFIGURE&&action.multiplayer_option==CONSOLE_MULTIPLAYER_OPTION_DICE);
    shell.runtime.multiplayer_dice_available=false;
    assert(tap(1200,350).type==CONSOLE_ACTION_NONE);

    /* Neither a starting nor a failed connection may enable Host or Join. */
    multiplayer_fixture();shell.multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_ROLE;
    shell.runtime.multiplayer_game_selection=2;
    shell.runtime.multiplayer_game_is_arena=true;
    shell.runtime.multiplayer_transport_starting=true;
    (void)snprintf(shell.runtime.multiplayer_status,sizeof(shell.runtime.multiplayer_status),
        "Starting Local Wi-Fi. Back remains available.");
    capture("multiplayer-role-starting");
    assert(tap(506,402).type==CONSOLE_ACTION_NONE);
    assert(tap(1010,402).type==CONSOLE_ACTION_NONE);
    assert(key(CONSOLE_BUTTON_BACK).type==CONSOLE_ACTION_PAGE_CHANGED);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_TRANSPORT);
    shell.multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_ROLE;
    shell.runtime.multiplayer_transport_starting=false;
    shell.runtime.multiplayer_transport_ready=false;
    (void)snprintf(shell.runtime.multiplayer_status,sizeof(shell.runtime.multiplayer_status),
        "Local Wi-Fi could not start. Go back and choose it again.");
    capture("multiplayer-role-error");
    assert(tap(506,402).type==CONSOLE_ACTION_NONE);
    assert(tap(1010,402).type==CONSOLE_ACTION_NONE);
    assert(key(CONSOLE_BUTTON_BACK).type==CONSOLE_ACTION_PAGE_CHANGED);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_TRANSPORT);

    shell.multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_ROLE;
    shell.runtime.multiplayer_transport_ready=true;
    shell.runtime.multiplayer_status[0]='\0';
    capture("multiplayer-role-ready");
    assert(tap(506,402).type==CONSOLE_ACTION_PAGE_CHANGED);
    assert(shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_HOST);

    /* Discovery cannot replace the room underneath a held finger. */
    multiplayer_fixture();shell.multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_JOIN;
    shell.runtime.multiplayer_lobby_count=1;
    shell.runtime.multiplayer_lobbies[0]=(console_multiplayer_lobby_display_t){
        .session_id=123,.players_present=1,.player_capacity=2,.game_available=true,.game_title="DOOM"};
    console_shell_contact_t contact={.x=750,.y=342};
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
    shell.runtime.multiplayer_lobbies[0].session_id=456;
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    shell.runtime.multiplayer_lobbies[0].game_available=false;
    assert(tap(750,342).type==CONSOLE_ACTION_NONE);
    shell.runtime.multiplayer_lobby_selection=1;
    assert(tap(750,634).type==CONSOLE_ACTION_NONE);
    shell.runtime.multiplayer_lobby_selection=CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX+1;
    assert(tap(750,634).type==CONSOLE_ACTION_NONE);
    shell.multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_HOST;
    shell.runtime.multiplayer_lobby_phase=CONSOLE_MULTIPLAYER_LOBBY_CONNECTED;
    shell.runtime.multiplayer_can_start=true;shell.runtime.multiplayer_lobby_is_host=false;
    assert(tap(750,634).type==CONSOLE_ACTION_NONE);
    shell.runtime.multiplayer_lobby_is_host=true;
    assert(tap(750,634).type==CONSOLE_ACTION_MULTIPLAYER_LAUNCH_GAME);

    /* Updating only availability or a failure message must reach the screen. */
    multiplayer_fixture();
    rt=shell.runtime;rt.multiplayer_games[1].available=true;shell.dirty=false;
    console_shell_set_runtime_info(&shell,&rt);
    assert(shell.dirty&&shell.runtime.multiplayer_games[1].available);
    rt=shell.runtime;
    (void)snprintf(rt.multiplayer_status,sizeof(rt.multiplayer_status),"Room no longer available. Choose another room.");
    shell.dirty=false;console_shell_set_runtime_info(&shell,&rt);
    assert(shell.dirty&&strcmp(shell.runtime.multiplayer_status,rt.multiplayer_status)==0);

    /* All installed games remain reachable across the bounded list pages. */
    rt=shell.runtime;rt.multiplayer_game_count=CONSOLE_MULTIPLAYER_MAX_GAMES;
    for(unsigned i=12;i<CONSOLE_MULTIPLAYER_MAX_GAMES;++i){
        (void)snprintf(rt.multiplayer_games[i].title,sizeof(rt.multiplayer_games[i].title),"GAME %u",i);
        rt.multiplayer_games[i].available=true;rt.multiplayer_games[i].transport_mask=7;
    }
    console_shell_set_runtime_info(&shell,&rt);
    (void)tap(1000,635);
    assert(shell.ng_list_first==12);
    capture("multiplayer-more-games");
    assert(tap(1000,635).type==CONSOLE_ACTION_NONE&&shell.ng_list_first==12);
    (void)tap(506,635);
    assert(shell.ng_list_first==0);
    (void)tap(1000,635);
    action=tap(506,422);
    assert(action.type==CONSOLE_ACTION_MULTIPLAYER_GAME_SELECT&&action.multiplayer_game_selection==18);
    assert(key(CONSOLE_BUTTON_BACK).type==CONSOLE_ACTION_PAGE_CHANGED);
    shell.runtime.multiplayer_game_count=3;
    assert(tap(1010,310).type==CONSOLE_ACTION_NONE); /* Removed catalog entry. */
    shell.ng_large_text=true;
    capture("multiplayer-large-text");
    puts("TAB5 MULTIPLAYER PASS: game/connection/role hierarchy, per-game settings, join state and room identity");
}
int main(int argc,char **argv)
{
    capture_prefix=argc>1?argv[1]:NULL;
    assert(CONSOLE_SHELL_WIDTH==1280&&CONSOLE_SHELL_HEIGHT==720);
    pixels=malloc((721U)*stride*sizeof(*pixels));assert(pixels);
    for(size_t i=0;i<721U*stride;++i)pixels[i]=0xdead;
    check_game_covers();
    check_loading_progress();
    check_loading_region_cache();
    check_featured_game();
    check_disabled_reason();
    check_scroll_cache();check_scroll_motion();check_catalog_grouping();
    check_multiplayer_hierarchy();
    init();
    /* Every shell page reports the effective game master volume, and the
     * status target has its own identity rather than duplicating Sound tiles. */
    console_shell_runtime_info_t sound=shell.runtime;sound.game_volume_step=3;
    shell.dirty=false;console_shell_set_runtime_info(&shell,&sound);assert(shell.dirty);capture("volume-three");
    assert(tap(700,35).type==CONSOLE_ACTION_PAGE_CHANGED&&shell.page==CONSOLE_PAGE_AUDIO);
    assert(tap(1160,483).type==CONSOLE_ACTION_GAME_VOLUME_SET);
    sound.game_volume_step=0;console_shell_set_runtime_info(&shell,&sound);capture("volume-muted");
    assert(tap(700,35).type==CONSOLE_ACTION_PAGE_CHANGED&&shell.page==CONSOLE_PAGE_AUDIO);
    init();
    /* Cartridge art overrides even a built-in fallback title. */
    static uint8_t icon_pixels[128U*72U];
    static uint16_t icon_palette[256];
    icon_palette[0]=0xf800;
    apps[0].icon_pixels=icon_pixels;apps[0].icon_palette=icon_palette;
    shell.dirty=true;render();
    assert(pixels[300U*stride+430U]==0xf800);
    init();
    assert(!console_shell_render_rgb565(&shell,pixels,1279));
    assert(!console_shell_render_rgb565(&shell,pixels,SIZE_MAX));
    assert(!console_shell_uses_native_bbs_launcher(&shell));
    shell.color_mode=CONSOLE_COLOR_MODE_GAMECHANGERS;assert(!console_shell_uses_native_bbs_launcher(&shell));
    assert(key(CONSOLE_BUTTON_ACCEPT).app_id==100U);
    assert(console_shell_handle_buttons(&shell,CONSOLE_BUTTON_ACCEPT).type==CONSOLE_ACTION_NONE);
    assert(tap(430,360).app_id==100U);
    /* The full-width rail is touchable, including the old 64px margin. */
    (void)tap(20,265);assert(shell.home_all_programs);render();
    /* The content follows the finger before release; dragging never launches. */
    console_shell_contact_t swipe={.x=900,.y=560};
    (void)console_shell_handle_touch(&shell,true,&swipe,1);swipe.y=350;
    (void)console_shell_handle_touch(&shell,true,&swipe,1);assert(shell.ng_library_scroll==210);render();
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    drag(900,550,340);assert(shell.ng_library_scroll==420);assert(tap(340,240).app_id==106U);
    drag(900,550,130);assert(tap(340,240).app_id==112U);
    drag(900,550,130);assert(shell.ng_library_scroll==1006);assert(tap(340,500).app_id==118U);
    drag(900,550,100);assert(shell.ng_library_scroll==1006);capture("scrolled-library");
    /* Header/sidebar drags do not scroll content. */
    drag(450,120,20);assert(shell.ng_library_scroll==1006);
    /* Controller navigation reaches off-screen rows and reveals them. */
    for(unsigned i=0;i<7;++i)(void)key(CONSOLE_BUTTON_UP);
    assert(shell.ng_library_scroll<1006);
    /* Categories come from cartridge folders; selecting one resets scroll. */
    for(unsigned i=0;i<20U;++i)apps[i].folder_path=i<7?"GAMES/ARCADE":i<14?"GAMES/CARDS":"GAMES/SPORTS";
    (void)tap(850,115);assert(shell.ng_categories);capture("categories");
    (void)tap(720,265);assert(!shell.ng_categories&&strcmp(shell.ng_category,"GAMES/ARCADE")==0);
    assert(shell.ng_library_scroll==0);capture("filtered-library");
    assert(tap(340,240).app_id==100U);drag(900,550,180);assert(shell.ng_library_scroll==166);
    assert(tap(340,550).app_id==106U);
    (void)tap(850,115);(void)tap(440,265);assert(shell.ng_category[0]=='\0'&&shell.ng_library_scroll==0);
    /* More than twelve categories stay reachable; scrollbar dragging clamps. */
    static char folders[20][32];
    for(unsigned i=0;i<20U;++i){(void)snprintf(folders[i],sizeof(folders[i]),"GAMES/C%02u",i);apps[i].folder_path=folders[i];}
    (void)tap(850,115);(void)tap(1030,590);assert(shell.ng_category_first==12U);render();
    (void)tap(440,265);assert(strcmp(shell.ng_category,"GAMES/C11")==0&&tap(340,240).app_id==111U);
    (void)tap(850,115);(void)tap(440,265);assert(shell.ng_category[0]=='\0');
    drag(1242,205,625);assert(shell.ng_library_scroll==1006);render();
    drag(1242,600,178);assert(shell.ng_library_scroll==0);
    for(unsigned i=0;i<20U;++i)apps[i].folder_path=i<7?"GAMES/ARCADE":i<14?"GAMES/CARDS":"GAMES/SPORTS";
    /* A category removed by a catalog replacement falls back to All. */
    (void)snprintf(shell.ng_category,sizeof(shell.ng_category),"GAMES/REMOVED");render();assert(shell.ng_category[0]=='\0');
    (void)tap(20,170);assert(!shell.home_all_programs);
    /* Movement, invalid input, multitouch, and out-of-panel coordinates cancel launch. */
    console_shell_contact_t c={.x=420,.y=360};
    (void)console_shell_handle_touch(&shell,true,&c,1);c.x+=24;
    (void)console_shell_handle_touch(&shell,true,&c,1);assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    (void)console_shell_handle_touch(&shell,true,&c,1);(void)console_shell_handle_touch(&shell,false,NULL,0);
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    console_shell_contact_t multi[2]={c,c};(void)console_shell_handle_touch(&shell,true,multi,2);
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    assert(tap(1280,300).type==CONSOLE_ACTION_NONE);
    /* Catalog replacement during a press cannot launch another app at the same index. */
    c=(console_shell_contact_t){.x=420,.y=360};(void)console_shell_handle_touch(&shell,true,&c,1);
    apps[0].id=333;assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);apps[0].id=100;
    (void)tap(100,540);assert(shell.page==CONSOLE_PAGE_CONTROL_PANEL);render();
    (void)tap(450,240);assert(shell.page==CONSOLE_PAGE_AUDIO);render();
    console_shell_action_t a=tap(1170,270);assert(a.type==CONSOLE_ACTION_BOOT_VOLUME_SET&&a.volume_step==4);
    a=tap(868,468);assert(a.type==CONSOLE_ACTION_GAME_VOLUME_SET&&a.volume_step==6);
    (void)key(CONSOLE_BUTTON_BACK);assert(shell.page==CONSOLE_PAGE_CONTROL_PANEL);
    (void)tap(1000,230);assert(shell.page==CONSOLE_PAGE_APPEARANCE);
    (void)tap(1120,250);assert(shell.ng_large_text);
    (void)tap(1120,430);assert(shell.ng_reduce_motion);
    /* Real file source ids reach the existing service only after confirmation. */
    assert(tap(100,450).app_id==90U);
    console_shell_file_listing_t files={.entry_count=1,.total_visible_entries=1,.available=true,.revision=1};
    files.entries[0]=(console_shell_file_entry_t){.source_index=77,.label="SAVE.DAT",.removable=true};
    assert(console_shell_set_file_listing(&shell,&files));render();
    /* The old large Delete/Open position is inert for ordinary files. */
    assert(tap(1150,600).type==CONSOLE_ACTION_NONE);assert(!shell.file_delete_confirm&&!shell.ng_file_menu);
    capture("files");
    (void)tap(1220,600);assert(shell.ng_file_menu&&!shell.file_delete_confirm);capture("file-actions");
    assert(key(CONSOLE_BUTTON_ACCEPT).type==CONSOLE_ACTION_NONE);assert(!shell.ng_file_menu); /* default close */
    (void)tap(1220,600);(void)tap(1000,500);assert(shell.file_delete_confirm&&!shell.ng_file_menu);capture("delete-confirm");
    assert(key(CONSOLE_BUTTON_ACCEPT).type==CONSOLE_ACTION_NONE);assert(!shell.file_delete_confirm); /* default Cancel */
    (void)tap(1220,600);(void)tap(1000,500);a=tap(950,510);assert(a.type==CONSOLE_ACTION_FILE_DELETE&&a.file_source_index==77U);
    (void)tap(1220,600);(void)tap(1000,500);assert(shell.file_delete_confirm);
    files.revision=2;files.entries[0].source_index=88;assert(console_shell_set_file_listing(&shell,&files));
    assert(!shell.file_delete_confirm);assert(tap(950,510).type!=CONSOLE_ACTION_FILE_DELETE);
    /* Refresh invalidates both stages; a held press cannot target a replacement. */
    (void)tap(1220,600);assert(shell.ng_file_menu);
    c=(console_shell_contact_t){.x=1000,.y=500};(void)console_shell_handle_touch(&shell,true,&c,1);
    files.revision=3;files.entries[0].source_index=99;assert(console_shell_set_file_listing(&shell,&files));
    assert(!shell.ng_file_menu);assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    /* File touch and controller scrolling stay bounded and never delete. */
    files.entry_count=12;files.total_visible_entries=12;
    for(unsigned i=0;i<12;++i)files.entries[i]=(console_shell_file_entry_t){.source_index=100+i,.label="SAVE.DAT",.removable=true};
    ++files.revision;assert(console_shell_set_file_listing(&shell,&files));
    drag(900,480,210);assert(shell.ng_file_scroll==270);assert(!shell.file_delete_confirm);capture("scrolled-files");
    (void)tap(500,215);assert(shell.file_selected_index==3U);
    for(unsigned i=0;i<8;++i)assert(key(CONSOLE_BUTTON_DOWN).type==CONSOLE_ACTION_NONE);
    assert(shell.file_selected_index==11U&&shell.ng_file_scroll==714);
    assert(key(CONSOLE_BUTTON_ACCEPT).type==CONSOLE_ACTION_NONE&&!shell.file_delete_confirm);
    /* Only directories and updates get a primary Open/Install action. */
    files.entry_count=1;files.total_visible_entries=1;files.entries[0]=(console_shell_file_entry_t){.source_index=25,.label="GAMES",.is_directory=true};
    ++files.revision;assert(console_shell_set_file_listing(&shell,&files));
    assert(tap(1010,600).type==CONSOLE_ACTION_FILE_OPEN);
    assert(tap(1220,600).type==CONSOLE_ACTION_NONE&&!shell.ng_file_menu);
    shell.page=CONSOLE_PAGE_GAMES;
    files.entries[0]=(console_shell_file_entry_t){.source_index=26,.label="P4UPDATE.P4U",.installable=true};
    ++files.revision;assert(console_shell_set_file_listing(&shell,&files));
    (void)tap(1010,600);assert(shell.file_delete_confirm);assert(key(CONSOLE_BUTTON_ACCEPT).type==CONSOLE_ACTION_NONE);
    (void)tap(1010,600);assert(tap(950,510).type==CONSOLE_ACTION_OS_UPDATE_INSTALL);
    /* Unsupported services remain inert. */
    shell.page=CONSOLE_PAGE_USB_DRIVE;assert(tap(440,600).type==CONSOLE_ACTION_NONE);
    shell.page=CONSOLE_PAGE_CONTROLLERS;assert(tap(400,510).type==CONSOLE_ACTION_NONE);
    /* Every native page, full keyboard, and modal preserves the output bounds. */
    for(unsigned page=0;page<=CONSOLE_PAGE_APPEARANCE;++page){
        shell.page=(console_page_t)page;shell.ng_focus=0;shell.file_delete_confirm=false;shell.ng_file_menu=false;shell.ng_categories=false;render();}
    shell.page=CONSOLE_PAGE_TERMINAL;shell.ng_keyboard=true;render();
    (void)tap(308,319);assert(shell.terminal.input_length==1U);
    (void)key(CONSOLE_BUTTON_BACK);assert(!shell.ng_keyboard&&shell.page==CONSOLE_PAGE_TERMINAL);
    free(pixels);
    puts("Tab5 native UI PASS: real launch/actions, category filters, continuous scrolling, touch cancellation, live-data bounds, safe confirmation, service gates, every screen.");
    return 0;
}
