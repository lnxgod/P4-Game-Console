// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "p4/game.h"
#include "p4_yahtzee_internal.h"
extern const p4_game_descriptor_t p4_p4_yahtzee_game;
#include "legacy_surface.h"
static int failures;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;}} while(0)
static void frame(p4_game_instance_t *i,p4_game_surface_t *s,uint16_t *m,const char *name)
{
 const size_t words=s->stride_pixels*s->height;
 for(size_t n=0;n<words+32U;++n)m[n]=0x55aaU;
 CHECK(p4_game_instance_render(i,s));
 for(size_t n=0;n<16U;++n){CHECK(m[n]==0x55aaU);CHECK(m[16U+words+n]==0x55aaU);}
 for(unsigned y=0;y<s->height;++y)for(size_t x=s->width;x<s->stride_pixels;++x)CHECK(s->pixels[y*s->stride_pixels+x]==0x55aaU);
 CHECK(s->pixels[0]!=0x55aaU);CHECK(s->pixels[(s->height-1U)*s->stride_pixels+s->width-1U]!=0x55aaU);
 const char *dir=getenv("P4_CAPTURE_DIR");if(dir==NULL)return;
 char path[1024];int count=snprintf(path,sizeof(path),"%s/%s-%u.ppm",dir,name,s->width);CHECK(count>0&&(size_t)count<sizeof(path));
 FILE *f=fopen(path,"wb");CHECK(f!=NULL);if(f==NULL)return;
 fprintf(f,"P6\n%u %u\n255\n",s->width,s->height);
 for(unsigned y=0;y<s->height;++y)for(unsigned x=0;x<s->width;++x){unsigned v=s->pixels[y*s->stride_pixels+x];fputc((int)(((v>>11U)&31U)*255U/31U),f);fputc((int)(((v>>5U)&63U)*255U/63U),f);fputc((int)((v&31U)*255U/31U),f);}
 CHECK(fclose(f)==0);
}
static void step(p4_game_instance_t *i,uint32_t buttons,bool touch,uint16_t x,uint16_t y)
{
 const p4_game_input_t input={.pressed=buttons,.held=buttons,.touch_valid=touch,.touch_count=touch?1U:0U,.touches={{x,y}}};
 CHECK(p4_game_instance_update(i,&input,16U)==P4_GAME_CONTINUE);
}
int main(void)
{
 CHECK((p4_p4_yahtzee_game.required_capabilities&P4_GAME_CAP_VIDEO_HIGH_RES)!=0U);
 CHECK((p4_p4_yahtzee_game.optional_capabilities&P4_GAME_CAP_VIDEO_HIGH_RES)==0U);
 /* Real cartridge admission must reject low-resolution-only services. */
 p4_game_instance_t rejected={0};
 p4_yahtzee_state_t rejected_state;
 const p4_game_services_t legacy_services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS};
 CHECK(!p4_game_instance_start(&rejected,&p4_p4_yahtzee_game,&legacy_services,&rejected_state,sizeof(rejected_state)));
 CHECK(!rejected.active);
 for(unsigned mode=0;mode<2U;++mode){
 const uint16_t width=mode==0U?320U:768U,height=mode==0U?200U:480U;
 const size_t stride=(size_t)width+7U,words=stride*height;
 uint16_t *memory=calloc(words+32U,sizeof(*memory));CHECK(memory!=NULL);if(memory==NULL)continue;
 p4_yahtzee_state_t state;p4_game_instance_t instance={0};
 const p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|(mode==0U?0U:P4_GAME_CAP_VIDEO_HIGH_RES)};
 CHECK(test_start_game(&instance,&p4_p4_yahtzee_game,&services,&state,sizeof(state)));
 CHECK((instance.descriptor==&p4_p4_yahtzee_game)==(mode!=0U));
 p4_game_surface_t surface={.pixels=memory+16U,.stride_pixels=stride,.width=width,.height=height};
 frame(&instance,&surface,memory,"initial");
 step(&instance,P4_BUTTON_RIGHT,false,0U,0U);step(&instance,P4_BUTTON_RIGHT,false,0U,0U);step(&instance,P4_BUTTON_A,false,0U,0U);CHECK(state.player_count==4U);
 step(&instance,P4_BUTTON_START,false,0U,0U);for(unsigned tick=0;tick<64U;++tick)step(&instance,0U,false,0U,0U);
 step(&instance,0U,true,38U,53U);CHECK((state.held_mask&1U)!=0U);step(&instance,0U,false,0U,0U);frame(&instance,&surface,memory,"held");
 state.dice[0]=2U;state.dice[1]=2U;state.dice[2]=3U;state.dice[3]=3U;state.dice[4]=3U;state.scores[0][P4_YAHTZEE_ONES]=3;
 state.focus=P4_YAHTZEE_FOCUS_SCORE;state.selected_category=P4_YAHTZEE_FULL_HOUSE;frame(&instance,&surface,memory,"scorecard");
 step(&instance,0U,true,210U,117U);CHECK(state.scores[0][P4_YAHTZEE_FULL_HOUSE]==25);frame(&instance,&surface,memory,"pass");
 state.phase=P4_YAHTZEE_GAME_OVER;frame(&instance,&surface,memory,"result");
 p4_game_instance_stop(&instance);free(memory);
 }
 return failures==0?EXIT_SUCCESS:EXIT_FAILURE;
}
