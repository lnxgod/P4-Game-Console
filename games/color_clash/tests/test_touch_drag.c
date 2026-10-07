// SPDX-License-Identifier: MIT
#include "test_game_start.h"
#include "color_clash_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const p4_game_descriptor_t p4_color_clash_game;
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
        char path[1024];snprintf(path,sizeof(path),"%s/color_clash-drag-%u-%u.ppm",folder,width,stage);
        FILE *file=fopen(path,"wb");CHECK(file!=NULL);fprintf(file,"P6\n%u %u\n255\n",width,height);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            uint16_t p=s.pixels[(size_t)y*stride+x];
            unsigned char rgb[3]={(unsigned char)(((p>>11)&31U)*255U/31U),(unsigned char)(((p>>5)&63U)*255U/63U),(unsigned char)((p&31U)*255U/31U)};
            CHECK(fwrite(rgb,1,3,file)==3U);
        }CHECK(fclose(file)==0);
    }free(memory);
}

static void fixture(color_clash_state_t *s)
{
    *s=(color_clash_state_t){.player_count=2U,.human_player_count=2U,.current_player=0U,
        .local_player_slot=0U,.phase=COLOR_CLASH_TURN,.mode=COLOR_CLASH_PRACTICE,
        .winner=0xff,.uno_pending_player=0xff,.notice_player=0xff,.discard_count=1U,.active_color=COLOR_CLASH_RED};
    s->discard[0]=color_clash_make_card(COLOR_CLASH_RED,COLOR_CLASH_THREE);
    s->hand_counts[0]=3U;s->hand_counts[1]=3U;
    s->hands[0][0]=color_clash_make_card(COLOR_CLASH_RED,COLOR_CLASH_FIVE);
    s->hands[0][1]=color_clash_make_card(COLOR_CLASH_GOLD,COLOR_CLASH_NINE);
    s->hands[0][2]=color_clash_make_card(COLOR_CLASH_VIOLET,COLOR_CLASH_SEVEN);
}
static void pick(p4_game_instance_t *g,const color_clash_state_t *s,uint8_t index)
{
    const uint8_t visual=color_clash_hand_visual_index(s,0U,index);
    touch(g,18+(int)visual*34,160);
}
int main(void)
{
    for(unsigned high=0;high<2U;++high){
        const unsigned width=high?768U:320U;
        const p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|(high?P4_GAME_CAP_VIDEO_HIGH_RES:0U)};
        p4_game_instance_t game={0};color_clash_state_t state;
        CHECK(test_start_game(&game,&p4_color_clash_game,&services,&state,sizeof(state)));
        fixture(&state);pick(&game,&state,0U);touch(&game,55,60);CHECK(state.hand_dragging);
        capture(&game,width,0U);touch(&game,-1,-1);
        CHECK(state.hand_counts[0]==3U && state.discard_count==1U && state.selected_card==0U);
        pick(&game,&state,1U);touch(&game,153,79);CHECK(state.hand_dragging);
        capture(&game,width,1U);touch(&game,-1,-1);
        CHECK(state.hand_counts[0]==3U && state.discard_count==1U);
        pick(&game,&state,0U);touch(&game,153,79);capture(&game,width,2U);touch(&game,-1,-1);
        CHECK(state.hand_counts[0]==2U && state.current_player==1U && state.discard_count==2U);
        fixture(&state);state.hands[0][0]=color_clash_make_card(COLOR_CLASH_RED,COLOR_CLASH_WILD);
        pick(&game,&state,0U);touch(&game,153,79);touch(&game,-1,-1);
        CHECK(state.phase==COLOR_CLASH_CHOOSE_COLOR && state.hand_counts[0]==2U);
        touch(&game,128,100);touch(&game,-1,-1);
        CHECK(state.active_color==COLOR_CLASH_GOLD && state.current_player==1U);
        fixture(&state);state.hands[0][1]=color_clash_make_card(COLOR_CLASH_RED,COLOR_CLASH_WILD_DRAW_FOUR);
        pick(&game,&state,1U);touch(&game,153,79);touch(&game,-1,-1);
        CHECK(state.hand_counts[0]==3U && state.discard_count==1U && state.phase==COLOR_CLASH_TURN);
        fixture(&state);state.phase=COLOR_CLASH_DRAWN_CARD;state.selected_card=2U;state.hands[0][2]=state.hands[0][0];
        pick(&game,&state,2U);touch(&game,153,79);touch(&game,-1,-1);
        CHECK(state.hand_counts[0]==2U && state.current_player==1U);
        fixture(&state);pick(&game,&state,0U);++state.network_revision;
        touch(&game,153,79);touch(&game,-1,-1);CHECK(state.hand_counts[0]==3U);
        /* Direct tap PLAY and physical A use the same existing rule action. */
        touch(&game,62,95);touch(&game,-1,-1);CHECK(state.hand_counts[0]==2U);
        fixture(&state);p4_game_input_t key={.pressed=P4_BUTTON_A};
        CHECK(p4_game_instance_update(&game,&key,16U)==P4_GAME_CONTINUE);CHECK(state.hand_counts[0]==2U);
        p4_game_instance_stop(&game);
    }
    puts("Color Clash drag legality, wild choice, cancellation and mapped-touch isolation passed");return 0;
}
