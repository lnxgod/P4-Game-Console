/* SPDX-License-Identifier: MIT */
#include "p4/game.h"
#include "port_support.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
extern bool ww_p4_probe_launch(void *,unsigned,unsigned,unsigned);
extern uint32_t ww_p4_probe_logic(const void *);
extern const p4_game_descriptor_t p4_wacky_probe_game;
static uint32_t hash(const uint16_t *p) {
    uint32_t h=2166136261u;
    for(size_t i=0;i<64000;++i) h=(h^p[i])*16777619u;
    return h;
}
int main(void)
{
    FILE *f=fopen(WW_DATA_PATH,"rb"); assert(f);
    const size_t n=4398948;
    uint8_t *data=malloc(n); assert(data && fread(data,1,n,f)==n); fclose(f);
    void *state=calloc(1,p4_wacky_probe_game.state_bytes); assert(state);
    uint16_t *pixels=calloc(64000,2); assert(pixels);
    p4_game_surface_t surf={.pixels=pixels,.stride_pixels=320,.width=320,.height=200};
    p4_game_services_t svc={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_STORAGE,
        .resource_data=data,.resource_bytes=n,.resource_format_version=1};
    p4_game_instance_t game={0};
    for(unsigned run=0;run<3;++run) {
        assert(p4_game_instance_start(&game,&p4_wacky_probe_game,&svc,state,p4_wacky_probe_game.state_bytes));
        assert(ww_p4_probe_launch(state,0,0,0));
        assert(p4_game_instance_render(&game,&surf));
        uint32_t initial=hash(pixels);
        for(unsigned tick=0;tick<600;++tick) {
            p4_game_input_t input={.held=P4_BUTTON_UP | (tick%180<40 ? P4_BUTTON_LEFT:0) | (tick%7==0 ? P4_BUTTON_B:0)};
            assert(p4_game_instance_update(&game,&input,88)==P4_GAME_CONTINUE);
            assert(p4_game_instance_render(&game,&surf));
        }
        assert(hash(pixels)!=initial);
        p4_game_input_t pause={.pressed=P4_BUTTON_START};
        assert(p4_game_instance_update(&game,&pause,16)==P4_GAME_CONTINUE);
        assert(p4_game_instance_render(&game,&surf));
        uint32_t frozen=hash(pixels);
        for(unsigned i=0;i<20;++i) {
            p4_game_input_t input={.held=P4_BUTTON_UP};
            assert(p4_game_instance_update(&game,&input,100)==P4_GAME_CONTINUE);
            assert(p4_game_instance_render(&game,&surf));
            assert(hash(pixels)==frozen);
        }
        assert(p4_game_instance_update(&game,&pause,16)==P4_GAME_CONTINUE);
        p4_game_input_t back={.pressed=P4_BUTTON_BACK};
        assert(p4_game_instance_update(&game,&back,16)==P4_GAME_EXIT_TO_LAUNCHER);
        p4_game_instance_stop(&game); assert(ww_p4_heap_live()==0);
    }
    /* Equal inputs/time, one render vs five: simulation must stay identical. */
    void *other_state=calloc(1,p4_wacky_probe_game.state_bytes); assert(other_state);
    uint16_t *other_pixels=calloc(64000,2); assert(other_pixels);
    p4_game_surface_t other_surface=surf;other_surface.pixels=other_pixels;
    p4_game_instance_t other={0};
    assert(p4_game_instance_start(&game,&p4_wacky_probe_game,&svc,state,p4_wacky_probe_game.state_bytes));
        assert(ww_p4_probe_launch(state,0,0,0));
    assert(p4_game_instance_start(&other,&p4_wacky_probe_game,&svc,other_state,p4_wacky_probe_game.state_bytes));
    assert(ww_p4_probe_launch(other_state,0,0,0));
    assert(p4_game_instance_render(&game,&surf));
    assert(p4_game_instance_render(&other,&other_surface));
    unsigned smooth_changes=0, stable_changes=0;
    double samples[1500];
    for(unsigned tick=0;tick<300;++tick) {
        p4_game_input_t input={.held=P4_BUTTON_UP | (tick%90<20 ? P4_BUTTON_RIGHT:0)};
        for(unsigned sub=0;sub<5;++sub) {
            assert(p4_game_instance_update(&game,&input,16)==P4_GAME_CONTINUE);
            assert(p4_game_instance_update(&other,&input,16)==P4_GAME_CONTINUE);
            uint32_t before=hash(other_pixels);
            struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
            assert(p4_game_instance_render(&other,&other_surface));
            clock_gettime(CLOCK_MONOTONIC,&b);
            samples[tick*5+sub]=(double)(b.tv_sec-a.tv_sec)*1000.0+(double)(b.tv_nsec-a.tv_nsec)/1000000.0;
            if(hash(other_pixels)!=before) ++smooth_changes;
        }
        uint32_t before=hash(pixels);
        assert(p4_game_instance_render(&game,&surf));
        if(hash(pixels)!=before) ++stable_changes;
        /* This comparison requires the same committed animation ticks. */
        if(ww_p4_probe_logic(state)!=ww_p4_probe_logic(other_state)) {
            fprintf(stderr,"render cadence changed simulation at tick %u\n",tick);
            return 2;
        }
    }
    for(size_t i=1;i<1500;++i) { double v=samples[i];size_t j=i;while(j && samples[j-1]>v){samples[j]=samples[j-1];--j;}samples[j]=v; }
    printf("Host sanitized render: median=%.3f ms p95=%.3f ms max=%.3f ms; visual changes=%u/1500 vs %u/300\n",samples[750],samples[1425],samples[1499],smooth_changes,stable_changes);
    p4_game_instance_stop(&game);p4_game_instance_stop(&other);
    free(other_state);free(other_pixels);assert(ww_p4_heap_live()==0);
    svc.resource_bytes=n-1;
    assert(!p4_game_instance_start(&game,&p4_wacky_probe_game,&svc,state,p4_wacky_probe_game.state_bytes));
    svc.resource_bytes=n;data[0]=0;
    assert(!p4_game_instance_start(&game,&p4_wacky_probe_game,&svc,state,p4_wacky_probe_game.state_bytes));
    assert(ww_p4_heap_live()==0);
    printf("PASS: 1800 race steps, steering/fire, pause/resume, Back/relaunch, invalid resources; state=%zu, peak engine heap=%zu\n",p4_wacky_probe_game.state_bytes,ww_p4_heap_peak());
    free(data);free(state);free(pixels);
}
