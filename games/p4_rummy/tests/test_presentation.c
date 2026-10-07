// SPDX-License-Identifier: MIT
/* Maintained native admission and explicit legacy fixture render coverage. */
#include "test_game_start.h"
#include "p4/game.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const p4_game_descriptor_t p4_p4_rummy_game;
#define CHECK(c) do { if(!(c)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#c);return 1;} }while(0)
static int save_frame(const p4_game_surface_t *surface,unsigned stage)
{
    const char *folder=getenv("P4_CARD_CAPTURE_DIR");if(folder==NULL)return 0;
    char path[1024];
    const int length=snprintf(path,sizeof(path),"%s/p4_rummy-%u-%u.ppm",folder,(unsigned)surface->width,stage);
    CHECK(length>0 && (size_t)length<sizeof(path));
    FILE *file=fopen(path,"wb");CHECK(file!=NULL);
    fprintf(file,"P6\n%u %u\n255\n",(unsigned)surface->width,(unsigned)surface->height);
    for(size_t y=0;y<surface->height;++y)for(size_t x=0;x<surface->width;++x){
        const uint16_t p=surface->pixels[y*surface->stride_pixels+x];
        const unsigned char rgb[3]={(unsigned char)(((p>>11)&31U)*255U/31U),(unsigned char)(((p>>5)&63U)*255U/63U),(unsigned char)((p&31U)*255U/31U)};
        CHECK(fwrite(rgb,1U,3U,file)==3U);
    }
    CHECK(fclose(file)==0);return 0;
}
int main(void)
{
    CHECK((p4_p4_rummy_game.required_capabilities&P4_GAME_CAP_VIDEO_HIGH_RES)!=0U);
    CHECK((p4_p4_rummy_game.optional_capabilities&P4_GAME_CAP_VIDEO_HIGH_RES)==0U);
    const p4_game_services_t low_res_services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS};
    p4_game_instance_t rejected={0};
    void *rejected_state=calloc(1U,p4_p4_rummy_game.state_bytes);CHECK(rejected_state!=NULL);
    CHECK(!p4_game_instance_start(&rejected,&p4_p4_rummy_game,&low_res_services,rejected_state,p4_p4_rummy_game.state_bytes));
    CHECK(!rejected.active);free(rejected_state);
    for(unsigned high=0;high<2U;++high){
        const unsigned width=high?768U:320U,height=high?480U:200U,stride=width+7U;
        const size_t count=(size_t)stride*height+32U;
        uint16_t *allocation=malloc(count*sizeof(*allocation));CHECK(allocation!=NULL);
        void *state=calloc(1U,p4_p4_rummy_game.state_bytes);CHECK(state!=NULL);
        const p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|(high?P4_GAME_CAP_VIDEO_HIGH_RES:0U)};
        p4_game_instance_t instance={0};
        if(high){
            CHECK(p4_game_instance_start(&instance,&p4_p4_rummy_game,&services,state,p4_p4_rummy_game.state_bytes));
            CHECK(instance.descriptor==&p4_p4_rummy_game);
        }else{
            CHECK(test_start_game(&instance,&p4_p4_rummy_game,&services,state,p4_p4_rummy_game.state_bytes));
        }
        p4_game_surface_t surface={.pixels=allocation+16U,.width=(uint16_t)width,.height=(uint16_t)height,.stride_pixels=stride};
        for(unsigned stage=0;stage<3U;++stage){
            for(size_t i=0;i<count;++i)allocation[i]=0x5aa5;
            CHECK(p4_game_instance_render(&instance,&surface));
            for(size_t i=0;i<16U;++i){CHECK(allocation[i]==0x5aa5);CHECK(allocation[count-1U-i]==0x5aa5);}
            for(size_t y=0;y<height;++y)for(size_t x=width;x<stride;++x)CHECK(surface.pixels[y*stride+x]==0x5aa5);
            CHECK(surface.pixels[0]!=0x5aa5);CHECK(surface.pixels[(height-1U)*stride+width-1U]!=0x5aa5);
            CHECK(save_frame(&surface,stage)==0);
            p4_game_input_t input={.pressed=P4_BUTTON_A};
            CHECK(p4_game_instance_update(&instance,&input,16U)==P4_GAME_CONTINUE);
            input=(p4_game_input_t){0};CHECK(p4_game_instance_update(&instance,&input,16U)==P4_GAME_CONTINUE);
        }
        const p4_game_input_t back={.pressed=P4_BUTTON_BACK};
        CHECK(p4_game_instance_update(&instance,&back,16U)==P4_GAME_EXIT_TO_LAUNCHER);
        p4_game_instance_stop(&instance);free(state);free(allocation);
    }
    puts("p4_rummy native admission, legacy fixture render and bounds passed");return 0;
}
