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
        /* Main uses the opt-in scroll entry point even on a launch cover. */
        if(frame%2U)assert(console_shell_render_native_scroll_rgb565(&shell,pixels,stride));
        else render();
        console_shell_native_update_t update;assert(console_shell_get_native_update(&shell,&update));
        assert(!update.scroll_context_valid&&!shell.native_logical_incomplete);
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
    puts("TAB5 LOADING REGION PASS pixel_exact=24 bounded_damage=1 invalidation=1 no_stale_bar=1 native_entry=1");
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
static void begin_hint_drag(bool files)
{
    init();
    if(files){
        shell.page=CONSOLE_PAGE_FILES;shell.files.available=true;shell.files.entry_count=12;shell.files.revision=7U;
        for(unsigned i=0;i<12U;++i)snprintf(shell.files.entries[i].label,sizeof(shell.files.entries[i].label),"Hint file %u",i);
        shell.ng_file_scroll=200;
    }else {shell.home_all_programs=true;shell.ng_library_scroll=200;}
    console_shell_contact_t contact={.x=900,.y=500};
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
    (void)console_shell_advance(&shell,120U);contact.y=450;
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
    assert(shell.contact_down&&!shell.press_active&&shell.ng_content_drag_started&&shell.ng_velocity_q16==0);
}
static void advance_hint_cadence(console_shell_t *target,unsigned cadence,unsigned total)
{
    for(unsigned elapsed=0;elapsed<total;){
        const unsigned step=total-elapsed<cadence?total-elapsed:cadence;
        const int previous=target->ng_glide_kind==2U?target->ng_file_scroll:target->ng_library_scroll;
        const int32_t speed=abs(target->ng_velocity_q16);
        (void)console_shell_advance(target,step);
        assert(abs(target->ng_velocity_q16)<=speed);
        const int offset=target->ng_glide_kind==2U?target->ng_file_scroll:target->ng_library_scroll;
        if(target->page==CONSOLE_PAGE_HOME)assert(abs(offset-previous)<=(int)(step*3U));
        elapsed+=step;
    }
}
static void check_release_velocity_hints(void)
{
    assert(!console_shell_set_native_scroll_release_velocity(NULL,65536));
    const unsigned cadences[]={1U,7U,16U,33U,64U};
    for(unsigned files=0;files<2U;++files)for(unsigned negative=0;negative<2U;++negative){
        begin_hint_drag(files!=0U);
        assert(console_shell_set_native_scroll_release_velocity(&shell,negative?-196608:196608));
        assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
        assert(shell.ng_glide_kind==(files?2U:1U)&&!shell.ng_content_drag_started);
        console_shell_t initial=shell,expected=shell;
        advance_hint_cadence(&expected,1U,1000U);
        assert(!expected.ng_glide_kind&&!expected.ng_velocity_q16);
        if(files)assert(expected.ng_file_scroll==(negative?0:714));
        for(size_t i=0;i<sizeof(cadences)/sizeof(cadences[0]);++i){
            console_shell_t actual=initial;advance_hint_cadence(&actual,cadences[i],1000U);
            assert(actual.ng_library_scroll==expected.ng_library_scroll&&actual.ng_file_scroll==expected.ng_file_scroll);
            assert(actual.ng_glide_q16==expected.ng_glide_q16&&!actual.ng_glide_kind&&!actual.ng_velocity_q16);
        }
        shell=initial;(void)console_shell_advance(&shell,1U);
        if(!negative||!files)assert(shell.ng_glide_q16%65536!=0);
        assert(abs(shell.ng_velocity_q16)>190000); /* A fast flick eases rather than stopping suddenly. */
        console_shell_contact_t stop={.x=350,.y=240};
        assert(console_shell_handle_touch(&shell,true,&stop,1).type==CONSOLE_ACTION_NONE);
        assert(!shell.ng_glide_kind&&!shell.ng_content_drag_started);
        (void)console_shell_advance(&shell,1U);stop.y=238U;
        assert(console_shell_handle_touch(&shell,true,&stop,1).type==CONSOLE_ACTION_NONE);
        assert(!shell.press_active&&!shell.ng_content_drag_started);
        const int32_t before=shell.ng_velocity_q16;
        assert(!console_shell_set_native_scroll_release_velocity(&shell,131072));
        assert(shell.ng_velocity_q16==before);
        assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&!shell.ng_glide_kind);
    }
    begin_hint_drag(false);const int slow_start=shell.ng_library_scroll;
    assert(console_shell_set_native_scroll_release_velocity(&shell,9830));
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&shell.ng_glide_kind==1U);
    advance_hint_cadence(&shell,16U,1000U);
    assert(shell.ng_library_scroll>slow_start&&shell.ng_library_scroll-slow_start<=8&&!shell.ng_glide_kind);
    begin_hint_drag(false);assert(console_shell_set_native_scroll_release_velocity(&shell,6553));
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&!shell.ng_glide_kind);
    begin_hint_drag(false);assert(console_shell_set_native_scroll_release_velocity(&shell,65536));
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&shell.ng_glide_kind==1U);
    console_shell_contact_t jitter={.x=350,.y=240};
    assert(console_shell_handle_touch(&shell,true,&jitter,1).type==CONSOLE_ACTION_NONE);
    (void)console_shell_advance(&shell,5U);jitter.y=238U;
    assert(console_shell_handle_touch(&shell,true,&jitter,1).type==CONSOLE_ACTION_NONE);
    assert(shell.ng_velocity_q16>6554&&!shell.ng_content_drag_started&&!shell.press_active);
    /* No hint call: the legacy estimator alone cannot restart this stop tap. */
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&!shell.ng_glide_kind);
    begin_hint_drag(false);assert(console_shell_set_native_scroll_release_velocity(&shell,INT32_MAX));
    assert(shell.ng_velocity_q16==196608);assert(console_shell_set_native_scroll_release_velocity(&shell,INT32_MIN));
    assert(shell.ng_velocity_q16==-196608);assert(console_shell_set_native_scroll_release_velocity(&shell,65536));
    (void)console_shell_advance(&shell,81U);
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&!shell.ng_glide_kind);
    begin_hint_drag(false);shell.ng_reduce_motion=true;
    assert(!console_shell_set_native_scroll_release_velocity(&shell,65536));
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&!shell.ng_glide_kind);
    begin_hint_drag(false);shell.ng_scroll_kind=3U;
    assert(!console_shell_set_native_scroll_release_velocity(&shell,65536));
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&!shell.ng_glide_kind);
    begin_hint_drag(true);++shell.files.revision;
    assert(!console_shell_set_native_scroll_release_velocity(&shell,65536));
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&!shell.ng_glide_kind);
    begin_hint_drag(true);assert(console_shell_set_native_scroll_release_velocity(&shell,65536));
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&shell.ng_glide_kind==2U);
    ++shell.files.revision;(void)console_shell_advance(&shell,16U);assert(!shell.ng_glide_kind&&!shell.ng_velocity_q16);
    begin_hint_drag(false);assert(console_shell_handle_touch(&shell,false,NULL,0).type==CONSOLE_ACTION_NONE);
    assert(!console_shell_set_native_scroll_release_velocity(&shell,65536));
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&!shell.ng_glide_kind);
    init();shell.home_all_programs=true;console_shell_contact_t contact={.x=350,.y=240};
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE&&shell.press_active);
    assert(!console_shell_set_native_scroll_release_velocity(&shell,65536));
    (void)console_shell_handle_touch(&shell,false,NULL,0);(void)console_shell_handle_touch(&shell,true,NULL,0);
    puts("TAB5 RELEASE HINT PASS fresh_real_drag_only=1 fast_easing=1 slow_short_coast=1 fractional=1 five_cadences=1 endpoints=1 stale_cancel=1 stop_jitter_safe=1 no_launch=1");
}
static void compare_cached_frame(uint16_t *reference,uint16_t *previous)
{
    console_shell_t expected=shell;
    memcpy(previous,pixels,721U*stride*sizeof(*pixels));
    memcpy(reference,pixels,721U*stride*sizeof(*pixels));
    render();assert(console_shell_render_rgb565(&expected,reference,stride));
    console_shell_native_update_t update;assert(console_shell_get_native_update(&shell,&update));
    for(size_t y=0;y<721U;++y)for(size_t x=0;x<stride;++x){
        const size_t pos=y*stride+x;
        if(pixels[pos]!=reference[pos]){
            fprintf(stderr,"DYNAMIC mismatch page=%u offset=%d/%d x=%zu y=%zu actual=%04x expected=%04x\n",
                (unsigned)shell.page,shell.ng_library_scroll,shell.ng_file_scroll,x,y,pixels[pos],reference[pos]);abort();}
        const bool damaged=update.kind==CONSOLE_SHELL_NATIVE_UPDATE_FULL||
            (update.kind==CONSOLE_SHELL_NATIVE_UPDATE_REGION&&x>=update.x&&y>=update.y&&
             x<(size_t)update.x+update.width&&y<(size_t)update.y+update.height);
        if(!damaged)assert(pixels[pos]==previous[pos]);
    }
}
static void check_dynamic_damage(void)
{
    init();uint16_t *reference=malloc(721U*stride*sizeof(*reference)),*previous=malloc(721U*stride*sizeof(*previous));
    assert(reference&&previous);
    compare_cached_frame(reference,previous);assert(shell.native_update.kind==CONSOLE_SHELL_NATIVE_UPDATE_NONE);
    console_shell_contact_t contact={.x=100,.y=454};
    (void)console_shell_handle_touch(&shell,true,&contact,1);compare_cached_frame(reference,previous);
    (void)console_shell_handle_touch(&shell,true,NULL,0);compare_cached_frame(reference,previous);
    assert(shell.page==CONSOLE_PAGE_FILES);
    shell.page=CONSOLE_PAGE_CONTROL_PANEL;shell.ng_focus=0;compare_cached_frame(reference,previous);
    console_shell_runtime_info_t runtime=shell.runtime;++runtime.uptime_seconds;
    console_shell_set_runtime_info(&shell,&runtime);compare_cached_frame(reference,previous);
    assert(shell.native_update.kind==CONSOLE_SHELL_NATIVE_UPDATE_NONE);
    runtime.battery_sample_valid=true;runtime.battery_percent=73;
    console_shell_set_runtime_info(&shell,&runtime);compare_cached_frame(reference,previous);
    assert(shell.native_update.kind==CONSOLE_SHELL_NATIVE_UPDATE_REGION&&shell.native_update.height<=80U);
    contact=(console_shell_contact_t){.x=450,.y=220};
    (void)console_shell_handle_touch(&shell,true,&contact,1);compare_cached_frame(reference,previous);
    (void)console_shell_handle_touch(&shell,true,NULL,0);compare_cached_frame(reference,previous);
    assert(shell.page==CONSOLE_PAGE_AUDIO);
    contact=(console_shell_contact_t){.x=865,.y=275};
    (void)console_shell_handle_touch(&shell,true,&contact,1);compare_cached_frame(reference,previous);
    for(unsigned frame=0;frame<8U;++frame){(void)console_shell_advance(&shell,16);compare_cached_frame(reference,previous);}
    (void)console_shell_handle_touch(&shell,true,NULL,0);compare_cached_frame(reference,previous);
    init();contact=(console_shell_contact_t){.x=420,.y=360};
    (void)console_shell_handle_touch(&shell,true,&contact,1);compare_cached_frame(reference,previous);
    for(unsigned frame=0;frame<8U;++frame){(void)console_shell_advance(&shell,16);compare_cached_frame(reference,previous);}
    free(reference);free(previous);puts("TAB5 DYNAMIC DAMAGE PASS full_equivalent=1 unchanged_skipped=1 damage_covers_changes=1");
}
typedef struct {
    unsigned copies,clean_copies,dirty_copies,publications;
    uint16_t last_height,last_width;
    uint64_t published_epochs[2];
    uint16_t published_rows[2];
    bool fail,fail_publish,fail_last;
} copy_test_t;
static bool copy_raster(void *context,const console_shell_rgb565_copy_t *copy)
{
    copy_test_t *state=context;++state->copies;
    state->last_height=copy->height;state->last_width=copy->width;
    if(copy->source_dma_clean){
        const unsigned page=copy->source==shell.native_rasters[0].pixels?0U:1U;
        assert(state->published_epochs[page]==shell.native_rasters[page].preparation_epoch);
        assert(state->published_rows[page]==shell.native_rasters[page].height);
        ++state->clean_copies;
    }else ++state->dirty_copies;
    assert(copy->source_x+copy->width<=copy->source_stride_pixels);
    assert(copy->source_y+copy->height<=copy->source_height);
    assert(copy->destination_x+copy->width<=copy->destination_stride_pixels);
    assert(copy->destination_y+copy->height<=copy->destination_height);
    const uintptr_t source=(uintptr_t)copy->source,destination=(uintptr_t)copy->destination;
    const size_t source_bytes=copy->source_stride_pixels*copy->source_height*sizeof(uint16_t);
    const size_t destination_bytes=copy->destination_stride_pixels*copy->destination_height*sizeof(uint16_t);
    assert(source+source_bytes<=destination||destination+destination_bytes<=source);
    if(state->fail)return false;
    for(size_t row=0;row<copy->height;++row)
        memcpy(copy->destination+(row+copy->destination_y)*copy->destination_stride_pixels+copy->destination_x,
            copy->source+(row+copy->source_y)*copy->source_stride_pixels+copy->source_x,(size_t)copy->width*sizeof(uint16_t));
    return true;
}
static bool publish_raster(void *context,const console_shell_rgb565_publication_t *publication)
{
    copy_test_t *state=context;++state->publications;
    assert(!shell.contact_down&&!shell.ng_scroll_kind&&!shell.ng_glide_kind);
    assert(publication->stride_pixels==1024U&&publication->row_count>0&&publication->row_count<=64U);
    const unsigned page=publication->pixels==shell.native_rasters[0].pixels?0U:1U;
    const console_shell_native_raster_cache_t *cache=&shell.native_rasters[page];
    assert(publication->pixels==cache->pixels&&publication->height==cache->height);
    assert(publication->first_row==cache->published_rows);
    assert((unsigned)publication->first_row+publication->row_count<=cache->prepared_rows);
    if(state->published_epochs[page]!=cache->preparation_epoch){
        assert(cache->published_epoch==0&&cache->published_rows==0);
        state->published_epochs[page]=cache->preparation_epoch;state->published_rows[page]=0;
    }
    assert(state->published_rows[page]==publication->first_row);
    if(state->fail_publish||(state->fail_last&&(unsigned)publication->first_row+publication->row_count==publication->height))return false;
    state->published_rows[page]=(uint16_t)(state->published_rows[page]+publication->row_count);
    return true;
}
static void prepare_rasters(void)
{
    unsigned slices=0;
    while(!console_shell_native_cache_ready(&shell)){
        assert(console_shell_prepare_native_cache(&shell,64));assert(++slices<=64U);
    }
    assert(!console_shell_prepare_native_cache(&shell,64));
}
static void check_immutable_rasters(void)
{
    init();const size_t bytes=console_shell_native_cache_storage_bytes();assert(bytes==4U*1024U*1024U);
    uint16_t *allocation=aligned_alloc(64,bytes+128U);assert(allocation);
    for(size_t i=0;i<(bytes+128U)/sizeof(uint16_t);++i)allocation[i]=0xdead;
    uint16_t *arena=allocation+32U,*snapshot=malloc(bytes);assert(snapshot);
    uint16_t *reference=malloc(721U*stride*sizeof(*reference)),*previous=malloc(721U*stride*sizeof(*previous));assert(reference&&previous);
    copy_test_t copy={0};
    assert(!console_shell_attach_native_cache(&shell,arena+1,bytes,copy_raster,NULL,&copy));
    assert(!console_shell_attach_native_cache(&shell,arena,bytes-64U,copy_raster,NULL,&copy));
    assert(console_shell_attach_native_cache(&shell,arena,bytes,copy_raster,NULL,&copy));
    prepare_rasters();assert(shell.native_rasters[0].valid&&!shell.native_rasters[1].valid);
    (void)tap(100,265);compare_cached_frame(reference,previous);
    console_shell_contact_t contact={.x=350,.y=230};
    (void)console_shell_handle_touch(&shell,true,&contact,1);compare_cached_frame(reference,previous);
    (void)console_shell_advance(&shell,16);compare_cached_frame(reference,previous);
    contact.x=370;(void)console_shell_handle_touch(&shell,true,&contact,1);compare_cached_frame(reference,previous);
    console_shell_runtime_info_t runtime=shell.runtime;runtime.battery_sample_valid=true;runtime.battery_percent=73;
    console_shell_set_runtime_info(&shell,&runtime);compare_cached_frame(reference,previous);
    assert(shell.native_update.kind==CONSOLE_SHELL_NATIVE_UPDATE_REGION&&shell.native_update.height<=80U);
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);compare_cached_frame(reference,previous);
    memcpy(snapshot,arena,bytes);shell.ng_scroll_kind=1;shell.ng_focus=0;shell.ng_focus_ms=0;
    const int offsets[]={6,60,180,410,780,832,900,1006,720,450,120,0};
    for(size_t i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i){
        shell.ng_library_scroll=offsets[i];compare_cached_frame(reference,previous);
        assert(!console_shell_prepare_native_cache(&shell,64));assert(memcmp(snapshot,arena,bytes)==0);
    }
    assert(copy.copies>6U);
    shell.ng_scroll_kind=0;shell.ng_library_scroll=900;prepare_rasters();
    shell.ng_scroll_kind=1;shell.ng_focus=0;shell.ng_library_scroll=1006;compare_cached_frame(reference,previous);
    copy.fail=true;shell.ng_library_scroll=990;compare_cached_frame(reference,previous);copy.fail=false;
    const uint64_t library_signature=shell.native_rasters[0].signature;
    shell.ng_scroll_kind=0;shell.page=CONSOLE_PAGE_FILES;shell.files.available=true;shell.files.entry_count=12;
    for(unsigned i=0;i<12U;++i)snprintf(shell.files.entries[i].label,sizeof(shell.files.entries[i].label),"File %u",i);
    shell.ng_file_scroll=0;compare_cached_frame(reference,previous);prepare_rasters();
    assert(shell.native_rasters[0].valid&&shell.native_rasters[0].signature==library_signature);
    shell.ng_scroll_kind=2;shell.ng_focus=0;
    for(int offset=0;offset<=410;offset+=41){shell.ng_file_scroll=offset;compare_cached_frame(reference,previous);}
    shell.file_selected_index=3;compare_cached_frame(reference,previous);
    shell.ng_file_scroll=714;compare_cached_frame(reference,previous);
    shell.ng_scroll_kind=0;prepare_rasters();shell.ng_scroll_kind=2;
    shell.ng_file_scroll=690;compare_cached_frame(reference,previous);
    ++shell.files.revision;shell.ng_file_scroll=640;compare_cached_frame(reference,previous);
    assert(!console_shell_native_cache_ready(&shell));shell.ng_scroll_kind=0;prepare_rasters();
    const uint64_t file_signature=shell.native_rasters[1].signature;
    apps[0].title="Changed raster title";assert(!console_shell_native_cache_ready(&shell));prepare_rasters();
    assert(shell.native_rasters[1].valid&&shell.native_rasters[1].signature==file_signature);
    for(size_t i=0;i<32U;++i){assert(allocation[i]==0xdead);assert(allocation[32U+bytes/sizeof(uint16_t)+i]==0xdead);}
    for(size_t row=0;row<2048U;++row)for(size_t x=964U;x<1024U;++x)assert(arena[row*1024U+x]==0xdead);
    assert(shell.native_raster_copy_frames>15U);
    console_shell_detach_native_cache(&shell);
    assert(!shell.native_raster_arena&&!shell.native_rasters[0].pixels&&!shell.native_rasters[1].pixels&&!shell.native_copy);
    compare_cached_frame(reference,previous);
    free(reference);free(previous);free(snapshot);free(allocation);
    puts("TAB5 IMMUTABLE RASTER PASS bounded_4mib=1 nonoverlap=1 idle_slices=1 full_equivalent=1 fallback=1 release=1");
}
static void check_small_drag(void)
{
    init();(void)tap(100,265);
    console_shell_contact_t contact={.x=350,.y=250};
    (void)console_shell_handle_touch(&shell,true,&contact,1);
    contact.y=245;(void)console_shell_handle_touch(&shell,true,&contact,1);
    assert(shell.ng_library_scroll==0&&shell.press_active);
    contact.y=244;(void)console_shell_handle_touch(&shell,true,&contact,1);
    assert(shell.ng_library_scroll==6&&!shell.press_active);
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    shell.page=CONSOLE_PAGE_FILES;shell.files.available=true;shell.files.entry_count=1;shell.files.revision=4;
    strcpy(shell.files.entries[0].label,"File");contact=(console_shell_contact_t){.x=350,.y=210};
    (void)console_shell_handle_touch(&shell,true,&contact,1);++shell.files.revision;
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    puts("TAB5 DRAG ONSET PASS native_pixels=6 tap_slop=1 release_suppressed=1 revision_guard=1");
}

enum { TEST_PHYSICAL_STRIDE=725,TEST_PHYSICAL_ROWS=1281 };
static void rotate_test_region(uint16_t *physical,const uint16_t *logical,
    unsigned x,unsigned y,unsigned width,unsigned height)
{
    for(unsigned row=y;row<y+height;++row)for(unsigned col=x;col<x+width;++col)
        physical[(size_t)(1279U-col)*TEST_PHYSICAL_STRIDE+row]=logical[(size_t)row*stride+col];
}
static void compose_scroll_test(uint16_t *physical,const uint16_t *previous,
    const console_shell_native_update_t *update)
{
    const int shift=update->previous_scroll_offset-update->current_scroll_offset;
    const unsigned amount=(unsigned)abs(shift),left=update->viewport_y;
    const unsigned first_row=1280U-update->viewport_x-update->viewport_width;
    for(unsigned row=first_row;row<1280U-update->viewport_x;++row)
        for(unsigned x=left;x<left+update->viewport_height;++x){
            const int source=(int)x-shift;
            if(source>=(int)left&&source<(int)(left+update->viewport_height))
                physical[(size_t)row*TEST_PHYSICAL_STRIDE+x]=previous[(size_t)row*TEST_PHYSICAL_STRIDE+(unsigned)source];
        }
    const unsigned first=shift<0?(unsigned)update->viewport_y+update->viewport_height-amount:update->viewport_y;
    rotate_test_region(physical,pixels,update->viewport_x,first,update->viewport_width,amount);
    rotate_test_region(physical,pixels,1228U,update->viewport_y,32U,update->viewport_height);
    if(update->scroll_stationary_width&&update->scroll_stationary_height)
        rotate_test_region(physical,pixels,update->scroll_stationary_x,update->scroll_stationary_y,
            update->scroll_stationary_width,update->scroll_stationary_height);
}
static void check_physical_scroll_contract(void)
{
    const size_t logical_bytes=721U*stride*sizeof(uint16_t);
    const size_t physical_bytes=(size_t)TEST_PHYSICAL_ROWS*TEST_PHYSICAL_STRIDE*sizeof(uint16_t);
    uint16_t *reference=malloc(logical_bytes),*previous=malloc(logical_bytes);
    uint16_t *physical=malloc(physical_bytes),*old_physical=malloc(physical_bytes),*expected_physical=malloc(physical_bytes);
    assert(reference&&previous&&physical&&old_physical&&expected_physical);
    unsigned scroll_frames=0,full_fallbacks=0;
    for(unsigned cached=0;cached<2U;++cached){
        init();uint16_t *arena=NULL;copy_test_t copy={0};
        if(cached){
            arena=aligned_alloc(64,console_shell_native_cache_storage_bytes());assert(arena);
            assert(console_shell_attach_native_cache(&shell,arena,console_shell_native_cache_storage_bytes(),copy_raster,NULL,&copy));
        }
        for(unsigned files=0;files<2U;++files){
            if(files){
                shell.page=CONSOLE_PAGE_FILES;shell.files.available=true;shell.files.entry_count=12;
                shell.file_selected_index=3U;
                for(unsigned i=0;i<12U;++i)snprintf(shell.files.entries[i].label,sizeof(shell.files.entries[i].label),"Scroll file %u",i);
                shell.ng_file_scroll=100;
            }else {shell.page=CONSOLE_PAGE_HOME;shell.home_all_programs=true;shell.ng_library_scroll=100;}
            shell.ng_focus=0;shell.ng_focus_ms=0;shell.ng_scroll_kind=0;
            if(cached)prepare_rasters();
            shell.ng_scroll_kind=files?2U:1U;
            render();assert(shell.native_update.scroll_context_valid&&!shell.native_logical_incomplete);
            for(size_t i=0;i<physical_bytes/sizeof(uint16_t);++i)physical[i]=expected_physical[i]=0xdead;
            rotate_test_region(physical,pixels,0,0,1280,720);
            const int offsets[]={107,113,96,115,109,124,121,131,126,140,900,891,0,7,15,714,701,693};
            for(size_t frame=0;frame<sizeof(offsets)/sizeof(offsets[0]);++frame){
                if(files)shell.ng_file_scroll=offsets[frame];else shell.ng_library_scroll=offsets[frame];
                if(frame==4U){++shell.runtime.game_volume_step;}
                if(frame==6U){if(files)++shell.files.revision;else apps[1].title="Scroll changed";}
                if(frame==8U){shell.file_selected_index=5U;}
                copy.fail=frame==9U;
                console_shell_t expected=shell;
                memcpy(previous,pixels,logical_bytes);memcpy(reference,pixels,logical_bytes);
                memcpy(old_physical,physical,physical_bytes);
                const unsigned copies_before=copy.copies;
                const console_shell_native_update_t prior_update=shell.native_update;
                assert(console_shell_render_native_scroll_rgb565(&shell,pixels,stride));
                assert(console_shell_render_rgb565(&expected,reference,stride));
                const console_shell_native_update_t update=shell.native_update;
                assert(update.scroll_context_valid&&update.viewport_x==264U&&update.viewport_y==178U&&update.viewport_width==964U);
                assert(update.viewport_height==(files?358U:448U));
                if(update.kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL){
                    assert(prior_update.scroll_context_valid&&prior_update.scroll_context==update.previous_scroll_context);
                    assert(prior_update.current_scroll_offset==update.previous_scroll_offset);
                    const int shift=update.previous_scroll_offset-update.current_scroll_offset;
                    const unsigned amount=(unsigned)abs(shift);
                    const unsigned first=shift<0?178U+update.viewport_height-amount:178U;
                    assert(amount>0U&&amount<update.viewport_height&&shell.native_logical_incomplete);
                    for(size_t y=0;y<721U;++y)for(size_t x=0;x<stride;++x){
                        const bool strip=x>=264U&&x<1228U&&y>=first&&y<first+amount;
                        const bool bar=x>=1228U&&x<1260U&&y>=178U&&y<178U+update.viewport_height;
                        const bool patch=x>=update.scroll_stationary_x&&y>=update.scroll_stationary_y&&
                            x<(size_t)update.scroll_stationary_x+update.scroll_stationary_width&&
                            y<(size_t)update.scroll_stationary_y+update.scroll_stationary_height;
                        if(strip||bar||patch)assert(pixels[y*stride+x]==reference[y*stride+x]);
                        else assert(pixels[y*stride+x]==previous[y*stride+x]);
                    }
                    if(copy.copies>copies_before){assert(copy.copies==copies_before+1U&&copy.last_height==amount);}
                    compose_scroll_test(physical,old_physical,&update);++scroll_frames;
                }else{
                    assert(!shell.native_logical_incomplete);
                    assert(memcmp(pixels,reference,logical_bytes)==0);
                    rotate_test_region(physical,pixels,0,0,1280,720);++full_fallbacks;
                }
                if(files&&prior_update.scroll_context_valid&&
                    ((prior_update.current_scroll_offset==0)!=(update.current_scroll_offset==0)||
                     (prior_update.current_scroll_offset==714)!=(update.current_scroll_offset==714)))
                    assert(prior_update.scroll_context!=update.scroll_context);
                rotate_test_region(expected_physical,reference,0,0,1280,720);
                assert(memcmp(physical,expected_physical,physical_bytes)==0);
                if(frame==2U&&update.kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL){
                    memcpy(previous,pixels,logical_bytes);const unsigned before_none=copy.copies;
                    shell.pressed_index=12345U;assert(!shell.press_active);
                    assert(console_shell_render_native_scroll_rgb565(&shell,pixels,stride));
                    assert(shell.native_update.kind==CONSOLE_SHELL_NATIVE_UPDATE_NONE&&shell.native_logical_incomplete);
                    assert(shell.native_update.scroll_context_valid&&shell.native_update.scroll_context==update.scroll_context);
                    assert(memcmp(pixels,previous,logical_bytes)==0&&copy.copies==before_none);
                }
                if(frame==3U||frame==10U){
                    /* Pre-mutation display rejection must reconstruct a full source. */
                    const unsigned before_fallback=copy.copies;
                    render();assert(!shell.native_logical_incomplete);
                    assert(memcmp(pixels,reference,logical_bytes)==0);
                    if(update.kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL)assert(shell.native_update.kind==CONSOLE_SHELL_NATIVE_UPDATE_FULL);
                    if(cached&&frame==3U){assert(copy.copies==before_fallback+1U&&copy.last_height==update.viewport_height);}
                }
            }
            shell.ng_scroll_kind=0;render();assert(!shell.native_logical_incomplete);
            console_shell_t expected=shell;memcpy(reference,pixels,logical_bytes);
            assert(console_shell_render_rgb565(&expected,reference,stride));assert(memcmp(pixels,reference,logical_bytes)==0);
            shell.pointer_visible=true;assert(console_shell_render_native_scroll_rgb565(&shell,pixels,stride));
            assert(!shell.native_update.scroll_context_valid&&!shell.native_logical_incomplete);shell.pointer_visible=false;
        }
        /* Switching the logical source cannot translate its unrelated prior contents. */
        shell.page=CONSOLE_PAGE_HOME;shell.home_all_programs=true;shell.ng_library_scroll=100;
        shell.ng_scroll_kind=1U;shell.ng_focus=0;shell.ng_focus_ms=0;render();
        shell.ng_library_scroll=107;assert(console_shell_render_native_scroll_rgb565(&shell,pixels,stride));
        assert(shell.native_logical_incomplete);
        console_shell_t expected=shell;assert(console_shell_render_rgb565(&expected,reference,stride));
        assert(console_shell_render_native_scroll_rgb565(&shell,previous,stride));
        assert(shell.native_update.kind==CONSOLE_SHELL_NATIVE_UPDATE_FULL&&!shell.native_logical_incomplete);
        assert(memcmp(previous,reference,logical_bytes)==0);
        render();shell.ng_library_scroll=113;assert(console_shell_render_native_scroll_rgb565(&shell,pixels,stride));
        assert(shell.native_logical_incomplete);
        shell.page=CONSOLE_PAGE_EXTERNAL;shell.active_app_id=100U;
        expected=shell;assert(console_shell_render_rgb565(&expected,reference,stride));
        assert(console_shell_render_native_scroll_rgb565(&shell,pixels,stride));
        assert(!shell.native_update.scroll_context_valid&&!shell.native_logical_incomplete);
        assert(memcmp(pixels,reference,logical_bytes)==0);
        shell.page=CONSOLE_PAGE_HOME;shell.home_all_programs=true;shell.ng_focus=0;render();
        shell.ng_library_scroll=119;assert(console_shell_render_native_scroll_rgb565(&shell,pixels,stride));
        assert(shell.native_logical_incomplete);
        expected=shell;assert(console_shell_render_rgb565(&expected,reference,stride));
        if(cached){console_shell_detach_native_cache(&shell);free(arena);}
        else console_shell_invalidate_native_cache(&shell);
        render();assert(shell.native_update.kind==CONSOLE_SHELL_NATIVE_UPDATE_FULL&&!shell.native_logical_incomplete);
        assert(memcmp(pixels,reference,logical_bytes)==0);
    }
    assert(scroll_frames>=30U&&full_fallbacks>=10U);
    free(reference);free(previous);free(physical);free(old_physical);free(expected_physical);
    printf("TAB5 PHYSICAL SCROLL PASS frames=%u fallbacks=%u exact_rotated_pixels=1 strip_only=1 complete_fallback=1 padded_guards=1\n",scroll_frames,full_fallbacks);
}
/* Model the selected physical frame exactly as the production CCW mapping.
 * A REGION is allowed only after the logical source becomes authoritative. */
static console_shell_native_update_t check_optin_physical(uint16_t *physical,uint16_t *old_physical,
    uint16_t *expected_physical,uint16_t *reference)
{
    const size_t logical_bytes=721U*stride*sizeof(uint16_t);
    const size_t physical_bytes=(size_t)TEST_PHYSICAL_ROWS*TEST_PHYSICAL_STRIDE*sizeof(uint16_t);
    console_shell_t expected=shell;memcpy(reference,pixels,logical_bytes);
    memcpy(old_physical,physical,physical_bytes);
    assert(console_shell_render_native_scroll_rgb565(&shell,pixels,stride));
    assert(console_shell_render_rgb565(&expected,reference,stride));
    const console_shell_native_update_t update=shell.native_update;
    if(update.kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL)compose_scroll_test(physical,old_physical,&update);
    else if(update.kind==CONSOLE_SHELL_NATIVE_UPDATE_FULL)rotate_test_region(physical,pixels,0,0,1280,720);
    else if(update.kind==CONSOLE_SHELL_NATIVE_UPDATE_REGION){
        assert(!shell.native_logical_incomplete);
        rotate_test_region(physical,pixels,update.x,update.y,update.width,update.height);
    }
    if(!shell.native_logical_incomplete)for(size_t y=0;y<721U;++y)for(size_t x=0;x<stride;++x)if(pixels[y*stride+x]!=reference[y*stride+x]){
        fprintf(stderr,"ONSET mismatch page=%u offset=%d/%d glide=%u touch=%u focus=%u update=%u box=%u,%u,%u,%u x=%zu y=%zu actual=%04x expected=%04x\n",
            (unsigned)shell.page,shell.ng_library_scroll,shell.ng_file_scroll,(unsigned)shell.ng_glide_kind,(unsigned)shell.contact_down,
            (unsigned)shell.ng_focus,(unsigned)update.kind,update.x,update.y,update.width,update.height,x,y,pixels[y*stride+x],reference[y*stride+x]);abort();
    }
    rotate_test_region(expected_physical,reference,0,0,1280,720);
    assert(memcmp(physical,expected_physical,physical_bytes)==0);
    for(size_t y=0;y<720U;++y)for(size_t x=1280U;x<stride;++x)assert(pixels[y*stride+x]==0xdead);
    for(size_t x=0;x<stride;++x)assert(pixels[720U*stride+x]==0xdead);
    return update;
}
static void check_deferred_content_focus(void)
{
    const size_t logical_bytes=721U*stride*sizeof(uint16_t);
    const size_t physical_bytes=(size_t)TEST_PHYSICAL_ROWS*TEST_PHYSICAL_STRIDE*sizeof(uint16_t);
    uint16_t *reference=malloc(logical_bytes),*previous=malloc(logical_bytes);
    uint16_t *physical=malloc(physical_bytes),*old_physical=malloc(physical_bytes),*expected_physical=malloc(physical_bytes);
    assert(reference&&previous&&physical&&old_physical&&expected_physical);
    for(unsigned cached=0;cached<2U;++cached)for(unsigned files=0;files<2U;++files){
        init();uint16_t *arena=NULL;copy_test_t copy={0};
        shell.page=files?CONSOLE_PAGE_FILES:CONSOLE_PAGE_HOME;shell.home_all_programs=true;
        if(files){
            shell.files.available=true;shell.files.entry_count=12;shell.files.revision=7;
            shell.file_selected_index=3;
            for(unsigned i=0;i<12U;++i)snprintf(shell.files.entries[i].label,sizeof(shell.files.entries[i].label),"Focus file %u",i);
            shell.ng_file_scroll=100;
        }else shell.ng_library_scroll=100;
        shell.ng_focus=0;shell.ng_focus_ms=0;
        if(cached){
            arena=aligned_alloc(64,console_shell_native_cache_storage_bytes());assert(arena);
            assert(console_shell_attach_native_cache(&shell,arena,console_shell_native_cache_storage_bytes(),copy_raster,NULL,&copy));
            prepare_rasters();
        }
        render();const uint16_t initial_focus=shell.ng_focus;assert(initial_focus);
        for(size_t i=0;i<physical_bytes/sizeof(uint16_t);++i)physical[i]=expected_physical[i]=0xdead;
        rotate_test_region(physical,pixels,0,0,1280,720);
        /* Start from an already translated frame: a DOWN then its first 6px
         * drag must retain that context rather than rebuild stale interiors. */
        shell.ng_scroll_kind=files?2U:1U;
        if(files)shell.ng_file_scroll=107;else shell.ng_library_scroll=107;
        assert(check_optin_physical(physical,old_physical,expected_physical,reference).kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL);
        shell.ng_scroll_kind=0;const uint64_t context=shell.native_update.scroll_context;
        console_shell_contact_t contact={.x=350,.y=files?300U:350U};
        assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
        assert(shell.ng_deferred_content_focus&&shell.press_active&&shell.ng_focus==initial_focus);
        const unsigned before_none=copy.copies;
        assert(check_optin_physical(physical,old_physical,expected_physical,reference).kind==CONSOLE_SHELL_NATIVE_UPDATE_NONE);
        assert(shell.native_logical_incomplete&&shell.native_update.scroll_context==context&&copy.copies==before_none);
        contact.y=(uint16_t)(contact.y-6U);
        assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
        assert(shell.ng_content_drag_started&&!shell.ng_deferred_content_focus&&!shell.press_active&&shell.ng_focus==initial_focus);
        assert(!console_shell_commit_native_content_press_highlight(&shell));
        assert(check_optin_physical(physical,old_physical,expected_physical,reference).kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL);
        assert(shell.native_update.scroll_context==context);
        contact.y=200;
        assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
        assert(check_optin_physical(physical,old_physical,expected_physical,reference).kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL);
        assert(shell.ng_focus==initial_focus);
        assert(console_shell_set_native_scroll_release_velocity(&shell,65536));
        assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&shell.ng_glide_kind);
        unsigned ticks=0;
        do{
            assert(++ticks<100U);(void)console_shell_advance(&shell,16U);
            (void)check_optin_physical(physical,old_physical,expected_physical,reference);
            if(shell.ng_glide_kind)assert(shell.ng_focus==initial_focus);
        }while(shell.ng_glide_kind);
        assert(!shell.contact_down&&!shell.press_active);
        /* A stop-fling touch keeps the visible focus, never commits a card
         * highlight or activates; even small fast jitter cannot restart it. */
        shell.ng_glide_kind=files?2U:1U;shell.ng_velocity_q16=65536;
        shell.ng_glide_q16=(files?shell.ng_file_scroll:shell.ng_library_scroll)*65536;
        const uint16_t stop_focus=shell.ng_focus;
        contact=(console_shell_contact_t){.x=350,.y=230};
        assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
        assert(!shell.ng_glide_kind&&!shell.press_active&&!shell.ng_deferred_content_focus&&shell.ng_focus==stop_focus);
        assert(!console_shell_commit_native_content_press_highlight(&shell));
        (void)check_optin_physical(physical,old_physical,expected_physical,reference);
        (void)console_shell_advance(&shell,5U);contact.y=228;
        assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
        assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE&&!shell.ng_glide_kind);
        (void)check_optin_physical(physical,old_physical,expected_physical,reference);
        /* A held highlight uses only actual old/new row or card damage while
         * the complete source exists, including a narrow cache rectangle. */
        shell.ng_focus=0;if(files)shell.ng_file_scroll=100;else shell.ng_library_scroll=100;
        render();if(cached)prepare_rasters();rotate_test_region(physical,pixels,0,0,1280,720);
        contact=(console_shell_contact_t){.x=350,.y=files?300U:350U};
        const uint16_t old_focus=shell.ng_focus;
        assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE&&shell.ng_deferred_content_focus);
        assert(console_shell_commit_native_content_press_highlight(&shell)&&shell.ng_focus!=old_focus);
        assert(shell.contact_down&&shell.press_active&&!shell.ng_deferred_content_focus);
        assert(!console_shell_commit_native_content_press_highlight(&shell));
        const unsigned copies_before=copy.copies;
        const console_shell_native_update_t held=check_optin_physical(physical,old_physical,expected_physical,reference);
        assert(held.kind==CONSOLE_SHELL_NATIVE_UPDATE_REGION&&held.width<=964U&&held.height<(files?358U:448U));
        if(cached){assert(copy.copies==copies_before+1U&&copy.last_height==held.height&&copy.last_width==held.width);}
        (void)console_shell_handle_touch(&shell,false,NULL,0);(void)console_shell_handle_touch(&shell,true,NULL,0);
        /* Highlight after a translated frame restores authoritative logical
         * pixels, but its proven physical damage remains a bounded REGION. */
        shell.ng_focus=0;if(files)shell.ng_file_scroll=100;else shell.ng_library_scroll=100;
        render();rotate_test_region(physical,pixels,0,0,1280,720);shell.ng_scroll_kind=files?2U:1U;
        if(files)shell.ng_file_scroll=107;else shell.ng_library_scroll=107;
        assert(check_optin_physical(physical,old_physical,expected_physical,reference).kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL);
        shell.ng_scroll_kind=0;
        contact=(console_shell_contact_t){.x=350,.y=files?300U:350U};
        assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
        assert(console_shell_commit_native_content_press_highlight(&shell));
        assert(check_optin_physical(physical,old_physical,expected_physical,reference).kind==CONSOLE_SHELL_NATIVE_UPDATE_REGION);
        assert(!shell.native_logical_incomplete);
        (void)console_shell_handle_touch(&shell,false,NULL,0);(void)console_shell_handle_touch(&shell,true,NULL,0);
        if(cached){console_shell_detach_native_cache(&shell);free(arena);}
    }
    init();shell.home_all_programs=true;render();
    console_shell_contact_t contact={.x=350,.y=420};
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE&&shell.ng_deferred_content_focus);
    const size_t target=shell.pressed_index;
    assert(console_shell_handle_touch(&shell,true,NULL,0).app_id==103U&&shell.ng_focus==target);
    init();shell.home_all_programs=true;render();
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
    ++apps[3].id;assert(!console_shell_commit_native_content_press_highlight(&shell));
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    shell.page=CONSOLE_PAGE_FILES;shell.files.available=true;shell.files.entry_count=4;shell.files.revision=9;shell.ng_file_scroll=0;shell.ng_focus=0;
    for(unsigned i=0;i<4U;++i)snprintf(shell.files.entries[i].label,sizeof(shell.files.entries[i].label),"Tap file %u",i);
    render();contact=(console_shell_contact_t){.x=350,.y=300};
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
    ++shell.files.revision;assert(!console_shell_commit_native_content_press_highlight(&shell));
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
    const size_t file_target=shell.pressed_index;
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    assert(shell.ng_focus==file_target&&shell.file_selected_index==1U);
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
    const uint16_t untouched=shell.ng_focus;contact.x=375;
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
    assert(!shell.ng_deferred_content_focus&&!console_shell_commit_native_content_press_highlight(&shell)&&shell.ng_focus==untouched);
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    init();shell.home_all_programs=true;render();
    contact=(console_shell_contact_t){.x=350,.y=420};
    assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
    contact.y=414;assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
    assert(shell.ng_content_drag_started&&key(CONSOLE_BUTTON_ACCEPT).type==CONSOLE_ACTION_NONE);
    assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
    free(reference);free(previous);free(physical);free(old_physical);free(expected_physical);
    puts("TAB5 CONTENT FOCUS PASS first_drag_delta=1 deferred_hold=1 tap_commit=1 bounded_damage=1 incomplete_region_authoritative=1 glide_pixels=1 stop_safe=1 stale_cancel=1");
}

static void check_file_endpoint_patches(void)
{
    const size_t logical_bytes=721U*stride*sizeof(uint16_t);
    const size_t physical_bytes=(size_t)TEST_PHYSICAL_ROWS*TEST_PHYSICAL_STRIDE*sizeof(uint16_t);
    uint16_t *reference=malloc(logical_bytes),*previous=malloc(logical_bytes);
    uint16_t *physical=malloc(physical_bytes),*old=malloc(physical_bytes),*expected=malloc(physical_bytes);
    assert(reference&&previous&&physical&&old&&expected);
    unsigned patched=0;
    for(unsigned cached=0;cached<2U;++cached)for(unsigned long_list=0;long_list<2U;++long_list){
        init();assert(tap(100,454).type==CONSOLE_ACTION_PAGE_CHANGED);
        console_shell_file_listing_t listing={.available=true,.entry_count=long_list?12U:5U,
            .total_visible_entries=long_list?12U:5U,.revision=11,.storage_generation=1};
        strcpy(listing.path_label,"SD card /");
        const char *names[]={"GAMES","SAVES","chex.deh","chex.wad","doom1.wad"};
        for(size_t i=0;i<listing.entry_count;++i){
            if(i<5U)strcpy(listing.entries[i].label,names[i]);
            else snprintf(listing.entries[i].label,sizeof(listing.entries[i].label),"Additional file %zu.wad",i);
            listing.entries[i].source_index=(uint32_t)i;listing.entries[i].is_directory=i<2U;
            listing.entries[i].removable=i>=2U;listing.entries[i].size_kib=i==2U?9U:i==3U?12098U:i==4U?4098U:0U;
        }
        assert(console_shell_set_file_listing(&shell,&listing));render();(void)console_shell_advance(&shell,200U);render();
        uint16_t *arena=NULL;copy_test_t copy={0};
        if(cached){
            arena=aligned_alloc(64,console_shell_native_cache_storage_bytes());assert(arena);
            assert(console_shell_attach_native_cache(&shell,arena,console_shell_native_cache_storage_bytes(),copy_raster,publish_raster,&copy));
            prepare_rasters();render();
        }
        for(size_t i=0;i<physical_bytes/sizeof(uint16_t);++i)physical[i]=expected[i]=0xdead;
        rotate_test_region(physical,pixels,0,0,1280,720);shell.ng_scroll_kind=2U;
        const int short_offsets[]={6,12,84,78,1,0,84,0};
        const int long_offsets[]={6,20,350,700,714,707,701,353,14,0,6};
        const int *offsets=long_list?long_offsets:short_offsets;
        const size_t frames=long_list?sizeof(long_offsets)/sizeof(*long_offsets):sizeof(short_offsets)/sizeof(*short_offsets);
        const int limit=long_list?714:84;
        for(size_t frame=0;frame<frames;++frame){
            const int prior=shell.ng_file_scroll;const uint64_t prior_context=shell.native_update.scroll_context;
            const unsigned before=copy.copies;memcpy(previous,pixels,logical_bytes);shell.ng_file_scroll=offsets[frame];
            const console_shell_native_update_t update=check_optin_physical(physical,old,expected,reference);
            assert(update.kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL&&shell.native_logical_incomplete);
            assert(update.previous_scroll_context==prior_context&&update.previous_scroll_offset==prior&&update.current_scroll_offset==offsets[frame]);
            const bool edge=(prior==0)!=(offsets[frame]==0)||(prior==limit)!=(offsets[frame]==limit);
            assert((update.scroll_context!=prior_context)==edge);
            if(edge){
                assert(update.scroll_stationary_x==368U&&update.scroll_stationary_y==558U&&
                    update.scroll_stationary_width==288U&&update.scroll_stationary_height==88U);++patched;
            }else assert(!update.scroll_stationary_width&&!update.scroll_stationary_height);
            const unsigned amount=(unsigned)abs(prior-offsets[frame]);
            const unsigned first=prior<offsets[frame]?536U-amount:178U;
            for(size_t y=0;y<721U;++y)for(size_t x=0;x<stride;++x){
                const bool strip=x>=264U&&x<1228U&&y>=first&&y<first+amount;
                const bool bar=x>=1228U&&x<1260U&&y>=178U&&y<536U;
                const bool footer=edge&&x>=368U&&x<656U&&y>=558U&&y<646U;
                if(strip||bar||footer)assert(pixels[y*stride+x]==reference[y*stride+x]);
                else assert(pixels[y*stride+x]==previous[y*stride+x]);
            }
            if(!cached)assert(copy.copies==before);
            else if(!long_list)assert(copy.copies==before+1U&&copy.last_width==964U&&copy.last_height==amount);
        }
        /* Real root-list first drag, fast endpoint flick and return preserve
         * controls while acceleration also works with no prepared raster. */
        shell.ng_scroll_kind=0;shell.ng_focus=0;shell.ng_file_scroll=0;render();
        if(!long_list){
            rotate_test_region(physical,pixels,0,0,1280,720);
            console_shell_contact_t contact={.x=350,.y=220};
            assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
            contact.y=214;assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
            assert(check_optin_physical(physical,old,expected,reference).kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL&&shell.ng_file_scroll==6);
            contact.y=130;assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
            assert(check_optin_physical(physical,old,expected,reference).kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL&&shell.ng_file_scroll==84);
            assert(console_shell_set_native_scroll_release_velocity(&shell,196608));
            assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
            (void)console_shell_advance(&shell,16U);(void)check_optin_physical(physical,old,expected,reference);
            assert(shell.ng_file_scroll==84&&!shell.ng_glide_kind);
            contact=(console_shell_contact_t){.x=350,.y=300};
            assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
            contact.y=306;assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
            assert(check_optin_physical(physical,old,expected,reference).kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL&&shell.ng_file_scroll==78);
            contact.y=390;assert(console_shell_handle_touch(&shell,true,&contact,1).type==CONSOLE_ACTION_NONE);
            assert(check_optin_physical(physical,old,expected,reference).kind==CONSOLE_SHELL_NATIVE_UPDATE_SCROLL&&shell.ng_file_scroll==0);
            assert(console_shell_set_native_scroll_release_velocity(&shell,-196608));
            assert(console_shell_handle_touch(&shell,true,NULL,0).type==CONSOLE_ACTION_NONE);
            (void)console_shell_advance(&shell,16U);(void)check_optin_physical(physical,old,expected,reference);
            assert(shell.ng_file_scroll==0&&!shell.ng_glide_kind);
            render();assert(!shell.native_logical_incomplete);
            assert(tap(430,600).type==CONSOLE_ACTION_NONE&&shell.ng_file_scroll==0); /* Disabled Prev. */
            assert(tap(580,600).type==CONSOLE_ACTION_NONE&&shell.ng_file_scroll==84);render();
            assert(tap(580,600).type==CONSOLE_ACTION_NONE&&shell.ng_file_scroll==84); /* Disabled Next. */
            assert(tap(430,600).type==CONSOLE_ACTION_NONE&&shell.ng_file_scroll==0);render();
            assert(tap(760,600).type==CONSOLE_ACTION_FILE_REFRESH);
            assert(tap(1000,600).type==CONSOLE_ACTION_FILE_OPEN);
            assert(tap(300,600).type==CONSOLE_ACTION_NONE); /* Root Up disabled. */
        }
        /* Endpoint provenance cannot authorize unrelated selected/focus or
         * revision changes; ordinary reconstruction remains exact. */
        shell.ng_scroll_kind=2U;shell.ng_file_scroll=6;render();rotate_test_region(physical,pixels,0,0,1280,720);
        shell.ng_file_scroll=0;shell.file_selected_index=2;
        assert(check_optin_physical(physical,old,expected,reference).kind!=CONSOLE_SHELL_NATIVE_UPDATE_SCROLL);
        shell.ng_file_scroll=6;++shell.files.revision;
        assert(check_optin_physical(physical,old,expected,reference).kind!=CONSOLE_SHELL_NATIVE_UPDATE_SCROLL);
        shell.ng_scroll_kind=0;shell.ng_file_scroll=0;render();
        listing.can_go_up=true;listing.revision=12;assert(console_shell_set_file_listing(&shell,&listing));render();
        assert(tap(300,600).type==CONSOLE_ACTION_FILE_UP);
        if(cached){console_shell_detach_native_cache(&shell);free(arena);}
    }
    assert(patched>=20U);free(reference);free(previous);free(physical);free(old);free(expected);
    printf("TAB5 FILE ENDPOINT PASS patches=%u five_root_entries=1 cold_strip_only=1 warm_strip_only=1 prior_context_exact=1 footer_pixels=1 controls=1 stale_fallback=1\n",patched);
}

static void check_raster_publication(void)
{
    init();const size_t bytes=console_shell_native_cache_storage_bytes();
    uint16_t *arena=aligned_alloc(64,bytes);assert(arena);
    uint16_t *reference=malloc(721U*stride*sizeof(*reference)),*previous=malloc(721U*stride*sizeof(*previous));assert(reference&&previous);
    copy_test_t copy={0};assert(console_shell_attach_native_cache(&shell,arena,bytes,copy_raster,publish_raster,&copy));
    assert(console_shell_prepare_native_cache(&shell,16));
    assert(shell.native_rasters[0].preparation_epoch==1&&shell.native_rasters[0].prepared_rows==16&&shell.native_rasters[0].published_rows==16);
    assert(!console_shell_native_cache_ready(&shell));prepare_rasters();
    assert(shell.native_rasters[0].published_epoch==shell.native_rasters[0].preparation_epoch);
    assert(shell.native_rasters[0].published_rows==shell.native_rasters[0].height);
    (void)tap(100,265);compare_cached_frame(reference,previous);
    shell.ng_scroll_kind=1;shell.ng_focus=0;shell.ng_library_scroll=6;compare_cached_frame(reference,previous);
    assert(copy.clean_copies>0&&copy.dirty_copies==0);

    /* Recenter the same allocation and catalogue: the previous clean epoch
     * must be revoked before the first newly painted slice is published. */
    const uint64_t signature=shell.native_rasters[0].signature,epoch=shell.native_rasters[0].preparation_epoch;
    const uint16_t *const source=shell.native_rasters[0].pixels;
    shell.ng_scroll_kind=0;shell.ng_library_scroll=900;copy.fail_last=true;
    assert(console_shell_prepare_native_cache(&shell,64));
    assert(shell.native_rasters[0].pixels==source&&shell.native_rasters[0].signature==signature);
    assert(shell.native_rasters[0].preparation_epoch==epoch+1U&&shell.native_rasters[0].published_rows==64);
    for(unsigned slice=1;slice<20U;++slice)assert(console_shell_prepare_native_cache(&shell,64));
    assert(shell.native_rasters[0].valid&&shell.native_rasters[0].prepared_rows==1280&&shell.native_rasters[0].published_rows==1216);
    assert(!console_shell_native_cache_ready(&shell));
    shell.ng_scroll_kind=1;shell.ng_focus=0;shell.ng_library_scroll=906;compare_cached_frame(reference,previous);
    assert(copy.dirty_copies>0);
    const unsigned attempts=copy.publications;
    assert(!console_shell_prepare_native_cache(&shell,16));
    shell.ng_scroll_kind=0;shell.contact_down=true;assert(!console_shell_prepare_native_cache(&shell,16));
    shell.contact_down=false;shell.ng_glide_kind=1;assert(!console_shell_prepare_native_cache(&shell,16));
    shell.ng_glide_kind=0;assert(copy.publications==attempts);
    copy.fail_last=false;
    for(unsigned slice=0;slice<4U;++slice){
        assert(console_shell_prepare_native_cache(&shell,16));
        assert(shell.native_rasters[0].preparation_epoch==epoch+1U&&shell.native_rasters[0].prepared_rows==1280);
    }
    assert(console_shell_native_cache_ready(&shell));
    const unsigned clean=copy.clean_copies;
    shell.ng_scroll_kind=1;shell.ng_focus=0;shell.ng_library_scroll=912;compare_cached_frame(reference,previous);
    assert(copy.clean_copies==clean+1U);

    /* Persistent publication failure still permits CPU-valid raster copies;
     * the other page's publication epoch and clean prefix remain independent. */
    shell.ng_scroll_kind=0;shell.page=CONSOLE_PAGE_FILES;shell.files.available=true;shell.files.entry_count=12;
    for(unsigned i=0;i<12U;++i)snprintf(shell.files.entries[i].label,sizeof(shell.files.entries[i].label),"File %u",i);
    shell.ng_file_scroll=0;compare_cached_frame(reference,previous);copy.fail_publish=true;
    for(unsigned slice=0;slice<12U;++slice)assert(console_shell_prepare_native_cache(&shell,64));
    assert(shell.native_rasters[1].valid&&shell.native_rasters[1].published_rows==0);
    assert(shell.native_rasters[0].preparation_epoch==epoch+1U&&shell.native_rasters[0].published_rows==1280);
    shell.ng_scroll_kind=2;shell.ng_focus=0;shell.ng_file_scroll=41;compare_cached_frame(reference,previous);
    assert(!console_shell_native_cache_ready(&shell));
    shell.ng_scroll_kind=0;copy.fail_publish=false;prepare_rasters();
    assert(shell.native_rasters[1].preparation_epoch==1&&shell.native_rasters[1].published_rows==768);
    const uint64_t file_epoch=shell.native_rasters[1].preparation_epoch;
    ++shell.files.revision;assert(console_shell_prepare_native_cache(&shell,16));
    assert(shell.native_rasters[1].preparation_epoch==file_epoch+1U&&shell.native_rasters[1].published_rows==16);
    assert(shell.native_rasters[0].preparation_epoch==epoch+1U);prepare_rasters();
    const uint64_t ready_file_epoch=shell.native_rasters[1].published_epoch;
    apps[0].title="New publication catalogue";assert(console_shell_prepare_native_cache(&shell,16));
    assert(shell.native_rasters[0].preparation_epoch==epoch+2U&&shell.native_rasters[0].published_rows==16);
    assert(shell.native_rasters[1].published_epoch==ready_file_epoch&&shell.native_rasters[1].published_rows==768);
    prepare_rasters();console_shell_detach_native_cache(&shell);assert(!shell.native_publish);
    free(reference);free(previous);free(arena);
    puts("TAB5 PUBLICATION PASS current_epoch_only=1 bounded_retry=1 dirty_cpu_fallback=1 independent_pages=1 idle_only=1");
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
static void check_multiplayer_native_cache(void)
{
    multiplayer_fixture();
    uint16_t *reference=malloc(1280U*720U*sizeof(*reference));assert(reference);
    for(unsigned frame=0;frame<7U;++frame){
        if(frame==1U){
            shell.runtime.multiplayer_games[1].available=true;
            (void)snprintf(shell.runtime.multiplayer_status,sizeof(shell.runtime.multiplayer_status),"Catalog changed");
        }
        if(frame==2U){shell.multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_TRANSPORT;shell.runtime.multiplayer_game_selection=2;}
        if(frame==3U){shell.runtime.multiplayer_transport_starting=true;}
        if(frame==4U){
            shell.multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_JOIN;
            shell.runtime.multiplayer_lobby_count=1;
            shell.runtime.multiplayer_lobbies[0]=(console_multiplayer_lobby_display_t){
                .session_id=123,.players_present=1,.player_capacity=2,.game_available=true,.game_title="DOOM"};
        }
        if(frame==5U){shell.runtime.multiplayer_lobbies[0].session_id=456;}
        assert(console_shell_render_native_scroll_rgb565(&shell,pixels,stride));
        console_shell_native_update_t update;assert(console_shell_get_native_update(&shell,&update));
        assert(update.kind==(frame==6U?CONSOLE_SHELL_NATIVE_UPDATE_NONE:CONSOLE_SHELL_NATIVE_UPDATE_FULL));
        assert(!update.scroll_context_valid&&!shell.native_logical_incomplete);
        console_shell_t exact=shell;assert(console_shell_render_rgb565(&exact,reference,1280U));
        for(size_t y=0;y<720U;++y){
            assert(memcmp(pixels+y*stride,reference+y*1280U,1280U*sizeof(*pixels))==0);
            for(size_t x=1280U;x<stride;++x)assert(pixels[y*stride+x]==0xdead);
        }
    }
    free(reference);
    puts("TAB5 MULTIPLAYER NATIVE CACHE PASS runtime_and_view_invalidation=1 pixel_exact=7 no_scroll_tag=1");
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
    check_scroll_cache();check_scroll_motion();check_release_velocity_hints();check_catalog_grouping();
    check_multiplayer_hierarchy();
    check_multiplayer_native_cache();
    check_dynamic_damage();check_immutable_rasters();check_small_drag();check_raster_publication();check_physical_scroll_contract();check_deferred_content_focus();check_file_endpoint_patches();
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
