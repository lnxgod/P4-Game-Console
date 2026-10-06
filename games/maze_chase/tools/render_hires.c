// SPDX-License-Identifier: MIT
// Render exact game sources at both supported sizes for visual acceptance.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p4/game.h"
#include "../src/maze_chase_internal.h"
extern const p4_game_descriptor_t p4_maze_chase_game;
static int capture(const char *prefix,const char *phase,p4_game_instance_t *instance,p4_game_surface_t *s)
{
    if(!p4_game_instance_render(instance,s))return 0;
    char path[512];
    if(snprintf(path,sizeof(path),"%s-%s-%u.ppm",prefix,phase,s->width)<0)return 0;
    FILE *file=fopen(path,"wb");if(file==NULL)return 0;
    fprintf(file,"P6\n%u %u\n255\n",s->width,s->height);
    for(unsigned y=0;y<s->height;++y)for(unsigned x=0;x<s->width;++x){
        const uint16_t p=s->pixels[(size_t)y*s->stride_pixels+x];
        const unsigned char rgb[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
        if(fwrite(rgb,1,3,file)!=3U){fclose(file);return 0;}
    }
    return fclose(file)==0;
}
int main(int argc,char **argv)
{
    if(argc!=2){fprintf(stderr,"usage: %s OUTPUT_PREFIX\n",argv[0]);return 2;}
    int ok=1;
    for(unsigned mode=0;mode<2U;++mode){
        const uint16_t w=mode?768U:320U,h=mode?480U:200U;
        uint16_t *pixels=calloc((size_t)w*h,sizeof(*pixels));void *state=calloc(1,p4_maze_chase_game.state_bytes);
        if(pixels==NULL||state==NULL){free(pixels);free(state);return 1;}
        p4_game_surface_t s={.pixels=pixels,.width=w,.height=h,.stride_pixels=w};
        p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|(mode?P4_GAME_CAP_VIDEO_HIGH_RES:0U)};
        p4_game_instance_t instance={0};
        ok=ok&&p4_game_instance_start(&instance,&p4_maze_chase_game,&services,state,p4_maze_chase_game.state_bytes);
        ok=ok&&capture(argv[1],"opening",&instance,&s);
        p4_game_input_t input={.held=P4_BUTTON_A,.pressed=P4_BUTTON_A};
        ok=ok&&p4_game_instance_update(&instance,&input,16U)==P4_GAME_CONTINUE;
        input=(p4_game_input_t){0};
        for(unsigned frame=0;frame<30U;++frame)ok=ok&&p4_game_instance_update(&instance,&input,16U)==P4_GAME_CONTINUE;
        ok=ok&&capture(argv[1],"gameplay",&instance,&s);
        input=(p4_game_input_t){.held=P4_BUTTON_START,.pressed=P4_BUTTON_START};
        ok=ok&&p4_game_instance_update(&instance,&input,16U)==P4_GAME_CONTINUE;
        ok=ok&&capture(argv[1],"paused",&instance,&s);
        /* Seeded visual-state coverage; these are not interactive play claims. */
        maze_chase_state_t *scene=state;
        scene->held_buttons=0U;scene->paused=false;scene->presentation_ms=150U;scene->frightened_ms=4000U;scene->fruit_ms=5000U;
        for(unsigned i=0;i<4U;++i)scene->enemies[i].release_ms=0U;
        ok=ok&&capture(argv[1],"powered",&instance,&s);
        scene->frightened_ms=900U;scene->presentation_ms=1260U;
        ok=ok&&capture(argv[1],"fright-warning",&instance,&s);
        const maze_chase_state_t before_clear=*scene;
        scene->frightened_ms=0U;scene->won=true;scene->level_clear_ms=1300U;scene->pellets_remaining=0U;memset(scene->pellets,0,sizeof(scene->pellets));
        ok=ok&&capture(argv[1],"round-clear",&instance,&s);
        *scene=before_clear;scene->frightened_ms=0U;scene->won=false;scene->game_over=true;scene->lives=0U;
        ok=ok&&capture(argv[1],"game-over",&instance,&s);
        scene->game_over=false;scene->lives=2U;scene->awaiting_move=true;scene->recovery_ms=500U;
        ok=ok&&capture(argv[1],"recovery",&instance,&s);
        input=(p4_game_input_t){.held=P4_BUTTON_BACK,.pressed=P4_BUTTON_BACK};
        ok=ok&&p4_game_instance_update(&instance,&input,16U)==P4_GAME_EXIT_TO_LAUNCHER;
        p4_game_instance_stop(&instance);free(pixels);free(state);
    }
    return ok?0:1;
}
