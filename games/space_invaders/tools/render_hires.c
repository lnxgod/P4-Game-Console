// SPDX-License-Identifier: MIT
// Render maintained native game sources; --legacy explicitly adds diagnostic captures.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p4/game.h"
#include "../src/space_invaders_internal.h"
extern const p4_game_descriptor_t p4_space_invaders_game;
static int capture(const char *prefix,const char *phase,p4_game_instance_t *instance,p4_game_surface_t *s)
{
    if(!p4_game_instance_render(instance,s))return 0;
    char path[512];
    if(snprintf(path,sizeof(path),"%s-%s%s-%u.ppm",prefix,s->width==320U?"legacy-":"",phase,s->width)<0)return 0;
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
    if(argc<2||argc>3||(argc==3&&strcmp(argv[2],"--legacy")!=0)){
        fprintf(stderr,"usage: %s OUTPUT_PREFIX [--legacy]\n",argv[0]);return 2;}
    const bool include_legacy=argc==3;
    int ok=1;
    for(unsigned mode=include_legacy?0U:1U;mode<2U;++mode){
        const uint16_t w=mode?768U:320U,h=mode?480U:200U;
        uint16_t *pixels=calloc((size_t)w*h,sizeof(*pixels));void *state=calloc(1,p4_space_invaders_game.state_bytes);
        if(pixels==NULL||state==NULL){free(pixels);free(state);return 1;}
        p4_game_surface_t s={.pixels=pixels,.width=w,.height=h,.stride_pixels=w};
        p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|(mode?P4_GAME_CAP_VIDEO_HIGH_RES:0U)};
        p4_game_instance_t instance={0};
        /* This scoped copy remains alive through stop and serves explicit legacy diagnostics only. */
        p4_game_descriptor_t legacy=p4_space_invaders_game;
        legacy.required_capabilities &= ~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
        legacy.optional_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
        const p4_game_descriptor_t *descriptor=mode?&p4_space_invaders_game:&legacy;
        ok=ok&&p4_game_instance_start(&instance,descriptor,&services,state,p4_space_invaders_game.state_bytes);
        ok=ok&&capture(argv[1],"opening",&instance,&s);
        p4_game_input_t input={.held=P4_BUTTON_A,.pressed=P4_BUTTON_A};
        ok=ok&&p4_game_instance_update(&instance,&input,16U)==P4_GAME_CONTINUE;
        input=(p4_game_input_t){0};
        ok=ok&&p4_game_instance_update(&instance,&input,0U)==P4_GAME_CONTINUE;
        input=(p4_game_input_t){.held=P4_BUTTON_A,.pressed=P4_BUTTON_A};
        ok=ok&&p4_game_instance_update(&instance,&input,16U)==P4_GAME_CONTINUE;
        input=(p4_game_input_t){0};
        for(unsigned frame=0;frame<30U;++frame)ok=ok&&p4_game_instance_update(&instance,&input,16U)==P4_GAME_CONTINUE;
        ok=ok&&capture(argv[1],"gameplay",&instance,&s);
        input=(p4_game_input_t){.held=P4_BUTTON_START,.pressed=P4_BUTTON_START};
        ok=ok&&p4_game_instance_update(&instance,&input,16U)==P4_GAME_CONTINUE;
        ok=ok&&capture(argv[1],"paused",&instance,&s);
        space_invaders_state_t *game = state;
        game->paused = false;
        game->shields[0] = 0x0a76U;
        game->shields[1] = 0x0bc9U;
        game->shields[2] = 0x064fU;
        game->formation_y = 76;
        game->formation_from_y = 76;
        game->formation_tween_ms = 140U;
        game->player_x = 223;
        game->previous_player_x = 223;
        game->player_projectiles[0] = (space_projectile_t){.x=207,.y=100,.previous_y=100,.active=true};
        game->enemy_projectiles[0] = (space_projectile_t){.x=120,.y=109,.previous_y=109,.active=true};
        game->impact_x = 164;
        game->impact_y = 122;
        game->impact_ms = 128U;
        game->impact_frame = 15U;
        ok=ok&&capture(argv[1],"damaged-edge",&instance,&s);
        game->game_over = true;
        ok=ok&&capture(argv[1],"game-over",&instance,&s);
        input=(p4_game_input_t){.held=P4_BUTTON_BACK,.pressed=P4_BUTTON_BACK};
        ok=ok&&p4_game_instance_update(&instance,&input,16U)==P4_GAME_EXIT_TO_LAUNCHER;
        p4_game_instance_stop(&instance);free(pixels);free(state);
    }
    return ok?0:1;
}
