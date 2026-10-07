// SPDX-License-Identifier: MIT
#include "test_game_start.h"
#include "p4_rummy_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const p4_game_descriptor_t p4_p4_rummy_game;
#define CHECK(c) do {if(!(c)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c);exit(1);}}while(0)
static void touch(p4_game_instance_t *game,int x,int y)
{
    p4_game_input_t input={.touch_valid=true,.held=P4_BUTTON_MASK,.pressed=P4_BUTTON_MASK};
    if(x>=0){input.touch_count=1U;input.touches[0]=(p4_game_point_t){(uint16_t)x,(uint16_t)y};}
    CHECK(p4_game_instance_update(game,&input,16U)==P4_GAME_CONTINUE);
}
static void capture(p4_game_instance_t *game,unsigned width,unsigned stage)
{
    const unsigned height=width==768U?480U:200U,stride=width+9U;
    const size_t count=(size_t)stride*height+32U;
    uint16_t *memory=malloc(count*sizeof(*memory));CHECK(memory!=NULL);
    for(size_t i=0;i<count;++i)memory[i]=0x5aa5;
    p4_game_surface_t s={.pixels=memory+16,.width=(uint16_t)width,.height=(uint16_t)height,.stride_pixels=stride};
    CHECK(p4_game_instance_render(game,&s));
    for(size_t i=0;i<16U;++i){CHECK(memory[i]==0x5aa5);CHECK(memory[count-1-i]==0x5aa5);}
    for(unsigned y=0;y<height;++y)for(unsigned x=width;x<stride;++x)CHECK(s.pixels[(size_t)y*stride+x]==0x5aa5);
    const char *folder=getenv("P4_CARD_CAPTURE_DIR");
    if(folder!=NULL){
        char path[1024];snprintf(path,sizeof(path),"%s/p4_rummy-drag-%u-%u.ppm",folder,width,stage);
        FILE *file=fopen(path,"wb");CHECK(file!=NULL);fprintf(file,"P6\n%u %u\n255\n",width,height);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            uint16_t p=s.pixels[(size_t)y*stride+x];
            unsigned char rgb[3]={(unsigned char)(((p>>11)&31U)*255U/31U),(unsigned char)(((p>>5)&63U)*255U/63U),(unsigned char)((p&31U)*255U/31U)};
            CHECK(fwrite(rgb,1,3,file)==3U);
        }CHECK(fclose(file)==0);
    }free(memory);
}

static void fixture(p4_rummy_state_t *s)
{
    p4_rummy_reset_lobby(s,2U,123U);CHECK(p4_rummy_begin_round(s));
    s->phase=P4_RUMMY_PHASE_DISCARD;s->current_player=0U;s->cpu_mask=0U;
    s->hand_counts[0]=4U;s->hands[0][0]=0U;s->hands[0][1]=13U;s->hands[0][2]=26U;s->hands[0][3]=5U;
    s->selected_card=0U;s->selected_mask=0U;s->meld_count=0U;s->selected_meld=P4_RUMMY_NO_CARD;
    s->drawn_card_index=P4_RUMMY_NO_CARD;s->required_meld_card=P4_RUMMY_NO_CARD;
}
int main(void)
{
    for(unsigned high=0;high<2U;++high){
        const unsigned width=high?768U:320U;
        const p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|(high?P4_GAME_CAP_VIDEO_HIGH_RES:0U)};
        p4_game_instance_t game={0};p4_rummy_state_t state;
        CHECK(test_start_game(&game,&p4_p4_rummy_game,&services,&state,sizeof(state)));
        fixture(&state);
        const uint32_t revision=state.revision;
        const uint8_t discard=state.discard_count;
        touch(&game,100,165);touch(&game,300,80);
        CHECK(state.hand_dragging);capture(&game,width,0U);
        touch(&game,-1,-1);
        CHECK(state.hand_counts[0]==4U && state.discard_count==discard && state.revision==revision && state.selected_mask==0U);
        touch(&game,100,165);touch(&game,151,80);
        CHECK(state.drag_drop_legal);capture(&game,width,1U);
        touch(&game,-1,-1);
        CHECK(state.hand_counts[0]==3U && state.current_player==1U && state.discard[state.discard_count-1U]==0U);

        fixture(&state);touch(&game,100,165);touch(&game,-1,-1);
        touch(&game,133,165);touch(&game,-1,-1);CHECK(state.selected_mask==3U);
        touch(&game,166,165);touch(&game,90,130);CHECK(state.drag_drop_legal);
        capture(&game,width,2U);touch(&game,-1,-1);
        CHECK(state.meld_count==1U && state.meld_counts[0]==3U && state.hand_counts[0]==1U);

        fixture(&state);state.meld_count=1U;state.meld_counts[0]=3U;
        state.melds[0][0]=0U;state.melds[0][1]=13U;state.melds[0][2]=26U;state.hands[0][0]=39U;
        touch(&game,100,165);touch(&game,30,50);CHECK(state.drag_drop_legal);
        touch(&game,-1,-1);CHECK(state.meld_counts[0]==4U && state.hand_counts[0]==3U);

        fixture(&state);state.required_meld_card=0U;
        touch(&game,100,165);touch(&game,151,80);CHECK(!state.drag_drop_legal);
        touch(&game,-1,-1);CHECK(state.hand_counts[0]==4U && state.current_player==0U);
        fixture(&state);touch(&game,100,165);touch(&game,90,130);CHECK(!state.drag_drop_legal);
        touch(&game,-1,-1);CHECK(state.hand_counts[0]==4U && state.meld_count==0U && state.selected_mask==0U);
        fixture(&state);touch(&game,100,165);++state.revision;
        touch(&game,151,80);touch(&game,-1,-1);CHECK(state.hand_counts[0]==4U && !state.hand_dragging);
        /* Keyboard/controller actions remain available between touch gestures. */
        p4_game_input_t key={.pressed=P4_BUTTON_A};
        CHECK(p4_game_instance_update(&game,&key,16U)==P4_GAME_CONTINUE);
        CHECK(state.hand_counts[0]==3U);
        p4_game_instance_stop(&game);
    }
    puts("Rummy drag legality, cancellation, selection and mapped-touch isolation passed");return 0;
}
