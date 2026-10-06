// SPDX-License-Identifier: MIT
#include "p4_games/maze_chase.h"
#include "maze_chase_internal.h"
#include "p4/audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(c) do { if(!(c)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c);++failures;} } while(0)
static bool start_width(p4_game_instance_t *game,maze_chase_state_t *state,p4_audio_mixer_t *mixer,uint16_t width)
{
    static p4_game_services_t services;
    p4_audio_mixer_init(mixer);
    services=(p4_game_services_t){.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_AUDIO_TONE|(width==768U?P4_GAME_CAP_VIDEO_HIGH_RES:0U),
        .audio_context=mixer,.play_tone=p4_audio_mixer_service_play_tone,.stop_audio=p4_audio_mixer_service_stop};
    *game=(p4_game_instance_t){0};
    return p4_game_instance_start(game,&p4_maze_chase_game,&services,state,sizeof(*state));
}
static bool start(p4_game_instance_t *game,maze_chase_state_t *state,p4_audio_mixer_t *mixer)
{return start_width(game,state,mixer,768U);}
static void advance(p4_game_instance_t *game,uint32_t held,uint32_t pressed,uint32_t ms)
{
    p4_game_input_t input={.held=held,.pressed=pressed};
    while(ms){const uint32_t amount=ms>100U?100U:ms;CHECK(p4_game_instance_update(game,&input,amount)==P4_GAME_CONTINUE);ms-=amount;input.pressed=0U;}
}
static void place_player(maze_chase_state_t *s,uint8_t x,uint8_t y,maze_direction_t direction)
{s->player_x=x;s->player_y=y;s->player_target_x=x;s->player_target_y=y;s->player_moving=false;s->player_move_accumulator_ms=0U;s->player_direction=direction;s->desired_direction=direction;}
static void quiet(maze_chase_state_t *s)
{for(unsigned i=0;i<4U;++i)s->enemies[i].release_ms=100000U;}
static void test_map_and_movement(void)
{
    p4_game_instance_t game;maze_chase_state_t s;p4_audio_mixer_t mixer;
    CHECK(start(&game,&s,&mixer));CHECK(p4_game_descriptor_valid(&p4_maze_chase_game));CHECK(s.level==1U&&s.lives==3U&&s.intro);
    bool seen[13][25]={{false}};uint8_t qx[325],qy[325];size_t read=0U,write=1U;qx[0]=12U;qy[0]=10U;seen[10][12]=true;
    while(read<write){int x=qx[read],y=qy[read++];const int dx[4]={0,0,-1,1},dy[4]={-1,1,0,0};
        for(unsigned d=0;d<4U;++d){int a=x+dx[d],b=y+dy[d];if(b==6){if(a<0)a=24;else if(a>24)a=0;}
            if(maze_chase_cell_open(a,b)&&!seen[b][a]){seen[b][a]=true;qx[write]=(uint8_t)a;qy[write++]=(uint8_t)b;}}}
    unsigned open=0U,power=0U;
    for(unsigned y=0;y<13U;++y)for(unsigned x=0;x<25U;++x){if(maze_chase_cell_open((int)x,(int)y)){++open;CHECK(seen[y][x]);}if(s.pellets[y][x]==2U)++power;}
    CHECK(open==181U&&write==181U&&power==4U);CHECK(s.pellets_remaining==176U);
    const unsigned pellets=s.pellets_remaining;advance(&game,P4_BUTTON_LEFT,P4_BUTTON_LEFT,112U);
    CHECK(s.player_x==11U&&s.player_y==10U&&s.score==10U&&s.pellets_remaining+1U==pellets);
    CHECK(s.player_target_x==10U&&s.player_target_y==10U&&s.player_moving);
    p4_audio_mixer_stats_t stats;p4_audio_mixer_get_stats(&mixer,&stats);CHECK(stats.tones_started>=2U);
    /* Buffered turns wait for an open intersection while forward motion continues. */
    quiet(&s);place_player(&s,2U,1U,MAZE_DIRECTION_RIGHT);
    advance(&game,P4_BUTTON_DOWN,P4_BUTTON_DOWN,224U);CHECK(s.player_x==4U&&s.player_y==1U);
    advance(&game,0U,0U,112U);CHECK(s.player_x==4U&&s.player_y==2U&&s.player_direction==MAZE_DIRECTION_DOWN);
    /* Both portal directions wrap, while the visual tunnel wraps continuously at the two lips. */
    place_player(&s,0U,6U,MAZE_DIRECTION_LEFT);
    advance(&game,P4_BUTTON_LEFT,P4_BUTTON_LEFT,112U);CHECK(s.player_x==24U&&s.player_y==6U);
    place_player(&s,24U,6U,MAZE_DIRECTION_RIGHT);advance(&game,P4_BUTTON_RIGHT,P4_BUTTON_RIGHT,112U);CHECK(s.player_x==0U&&s.player_y==6U);
    p4_game_input_t back={.pressed=P4_BUTTON_BACK};CHECK(p4_game_instance_update(&game,&back,16U)==P4_GAME_EXIT_TO_LAUNCHER);p4_game_instance_stop(&game);
}
static void test_mid_edge_reverse(void)
{
    p4_game_instance_t game;maze_chase_state_t s;p4_audio_mixer_t mixer;
    CHECK(start(&game,&s,&mixer));s.intro=false;s.awaiting_move=false;quiet(&s);
    place_player(&s,12U,10U,MAZE_DIRECTION_LEFT);
    advance(&game,P4_BUTTON_LEFT,P4_BUTTON_LEFT,32U);
    CHECK(s.player_x==12U&&s.player_target_x==11U&&s.player_move_accumulator_ms==32U);
    const uint32_t score=s.score;
    advance(&game,P4_BUTTON_RIGHT,P4_BUTTON_RIGHT,1U);
    CHECK(s.player_direction==MAZE_DIRECTION_RIGHT&&s.player_target_x==12U&&s.player_move_accumulator_ms==81U);
    /* Return takes the distance already travelled, not the rest of the edge. */
    advance(&game,P4_BUTTON_RIGHT,0U,31U);
    CHECK(s.player_x==12U&&s.player_move_accumulator_ms==0U&&s.score==score);
    CHECK(s.pellets[10][11]==1U); /* Reversal must not consume an unreached pearl. */
    p4_game_instance_stop(&game);
}
static void force_collision(maze_chase_state_t *s,unsigned index)
{
    s->intro=false;place_player(s,12U,10U,MAZE_DIRECTION_RIGHT);
    s->player_target_x=13U;s->player_target_y=10U;s->player_moving=true;s->player_move_accumulator_ms=100U;
    quiet(s);s->enemies[index]=(maze_enemy_t){.x=13U,.y=10U,.target_x=13U,.target_y=10U,
        .direction=MAZE_DIRECTION_LEFT,.color_index=(uint8_t)index,.step_duration_ms=176U};
}
static void test_recovery_guarantees(void)
{
    p4_game_instance_t game;maze_chase_state_t s;p4_audio_mixer_t mixer;CHECK(start(&game,&s,&mixer));
    advance(&game,0U,0U,5000U);CHECK(s.lives==3U&&s.score==0U&&s.awaiting_move&&s.intro);
    s.awaiting_move=false;force_collision(&s,0U);advance(&game,P4_BUTTON_RIGHT,0U,12U);
    CHECK(s.lives==2U&&s.recovery_ms>=688U&&s.recovery_ms<=700U&&s.awaiting_move);
    advance(&game,P4_BUTTON_RIGHT,0U,10000U);CHECK(s.lives==2U&&s.recovery_ms==0U&&s.awaiting_move&&s.player_x==12U&&s.player_y==10U);
    advance(&game,P4_BUTTON_RIGHT,P4_BUTTON_RIGHT,16U);CHECK(!s.awaiting_move&&s.protection_ms==1484U);
    for(unsigned i=0;i<10U;++i){force_collision(&s,0U);advance(&game,P4_BUTTON_RIGHT,0U,100U);CHECK(s.lives==2U&&!s.awaiting_move);}
    const uint32_t protection=s.protection_ms;advance(&game,0U,P4_BUTTON_START,16U);advance(&game,0U,0U,500U);CHECK(s.paused&&s.protection_ms==protection);
    advance(&game,0U,P4_BUTTON_START,16U);s.protection_ms=0U;force_collision(&s,0U);advance(&game,P4_BUTTON_RIGHT,0U,12U);CHECK(s.lives==1U&&s.awaiting_move&&!s.game_over);
    p4_game_instance_stop(&game);
}
static void test_power_combo_return_and_modes(void)
{
    p4_game_instance_t game;maze_chase_state_t s;p4_audio_mixer_t mixer;CHECK(start(&game,&s,&mixer));
    s.intro=false;s.awaiting_move=false;quiet(&s);place_player(&s,2U,1U,MAZE_DIRECTION_LEFT);
    s.enemies[0].x=8U;s.enemies[0].y=3U;s.enemies[0].direction=MAZE_DIRECTION_RIGHT;s.enemies[0].release_ms=0U;
    advance(&game,P4_BUTTON_LEFT,P4_BUTTON_LEFT,112U);CHECK(s.score==50U&&s.frightened_ms>6300U&&s.enemies[0].reverse_pending);
    advance(&game,P4_BUTTON_LEFT,0U,130U);CHECK(s.enemies[0].direction==MAZE_DIRECTION_LEFT&&s.enemies[0].target_x<s.enemies[0].x);
    const uint32_t phase_time=s.mode_ms;advance(&game,P4_BUTTON_LEFT,0U,500U);CHECK(s.mode_ms==phase_time);
    const uint32_t before=s.score;
    for(unsigned i=0;i<4U;++i){force_collision(&s,i);s.frightened_ms=5000U;advance(&game,P4_BUTTON_RIGHT,0U,12U);CHECK(s.enemies[i].returning);}
    CHECK(s.ghost_chain==4U&&s.score>=before+3000U&&s.lives==3U);
    /* Eyes reach their home by a shortest path, then have a safe release delay. */
    quiet(&s);place_player(&s,1U,11U,MAZE_DIRECTION_LEFT);
    s.enemies[0].x=1U;s.enemies[0].y=1U;s.enemies[0].returning=true;s.enemies[0].release_ms=0U;s.enemies[0].move_ms=0U;s.enemies[0].moving=false;
    bool home=false;for(unsigned n=0;n<100U;++n){advance(&game,P4_BUTTON_LEFT,0U,100U);if(!s.enemies[0].returning&&s.enemies[0].release_ms){home=true;break;}}
    CHECK(home&&s.enemies[0].x==11U&&s.enemies[0].y==6U&&s.enemies[0].power_immune);
    s.frightened_ms=0U;s.mode_phase=0U;s.mode_ms=5990U;quiet(&s);advance(&game,P4_BUTTON_LEFT,0U,16U);CHECK(s.mode_phase==1U&&s.enemies[0].reverse_pending);
    const uint32_t revive_score=s.score;const uint8_t revive_lives=s.lives;
    force_collision(&s,0U);s.enemies[0].power_immune=true;s.frightened_ms=5000U;s.protection_ms=0U;
    advance(&game,P4_BUTTON_RIGHT,0U,12U);CHECK(s.lives+1U==revive_lives&&s.score==revive_score);
    p4_game_instance_stop(&game);CHECK(start(&game,&s,&mixer));s.intro=false;s.awaiting_move=false;quiet(&s);
    for(unsigned i=0;i<4U;++i)s.enemies[i].power_immune=true;
    place_player(&s,2U,1U,MAZE_DIRECTION_LEFT);advance(&game,P4_BUTTON_LEFT,0U,112U);
    for(unsigned i=0;i<4U;++i)CHECK(!s.enemies[i].power_immune);
    p4_game_instance_stop(&game);
}
static void test_bonus_levels_extra_life(void)
{
    p4_game_instance_t game;maze_chase_state_t s;p4_audio_mixer_t mixer;CHECK(start(&game,&s,&mixer));s.intro=false;s.awaiting_move=false;quiet(&s);
    place_player(&s,11U,9U,MAZE_DIRECTION_RIGHT);s.fruit_ms=5000U;
    const uint32_t before=s.score;advance(&game,P4_BUTTON_RIGHT,P4_BUTTON_RIGHT,112U);CHECK(s.fruit_ms==0U&&s.score>=before+100U&&s.popup_score==100U);
    place_player(&s,12U,10U,MAZE_DIRECTION_LEFT);s.pellets[10][11]=1U;s.pellets_remaining=1U;s.score=9990U;
    advance(&game,P4_BUTTON_LEFT,P4_BUTTON_LEFT,112U);CHECK(s.won&&s.level_clear_ms==1300U&&s.lives==4U&&s.score==10000U);
    advance(&game,0U,0U,1300U);CHECK(!s.won&&s.level==2U&&s.score==10000U&&s.lives==4U&&s.awaiting_move&&s.pellets_remaining==176U);
    advance(&game,P4_BUTTON_LEFT,0U,2000U);CHECK(s.awaiting_move&&s.score==10000U);
    advance(&game,P4_BUTTON_LEFT,P4_BUTTON_LEFT,16U);CHECK(!s.awaiting_move&&s.protection_ms==1484U);
    s.game_over=true;s.score=12000U;s.best_score=12000U;advance(&game,P4_BUTTON_A,P4_BUTTON_A,16U);
    CHECK(s.level==1U&&s.score==0U&&s.lives==3U&&s.best_score==12000U&&!s.intro);
    p4_game_instance_stop(&game);
}
static void test_direct_touch_and_motion(void)
{
    p4_game_instance_t game;maze_chase_state_t s;p4_audio_mixer_t mixer;CHECK(start(&game,&s,&mixer));
    p4_game_input_t touch={.touch_valid=true,.touch_count=1U,.touches={{60U,180U}},.held=P4_BUTTON_B,.pressed=P4_BUTTON_B};
    CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_CONTINUE);CHECK(!s.intro&&s.desired_direction==MAZE_DIRECTION_UP&&s.held_buttons==P4_BUTTON_UP);
    touch.touches[0]=(p4_game_point_t){240U,175U};touch.held=P4_BUTTON_B;touch.pressed=P4_BUTTON_START;
    CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_CONTINUE);CHECK(!s.paused&&s.held_buttons==0U);
    touch.touch_count=0U;touch.held=0U;touch.pressed=0U;CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_CONTINUE);
    touch.touch_count=1U;touch.touches[0]=(p4_game_point_t){150U,100U};CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_CONTINUE);
    touch.touches[0].x=170U;CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_CONTINUE);CHECK(s.desired_direction==MAZE_DIRECTION_RIGHT);
    touch.touches[0]=(p4_game_point_t){285U,10U};CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_CONTINUE);CHECK(!s.paused);
    touch.touches[0]=(p4_game_point_t){20U,10U};CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_CONTINUE);CHECK(!s.paused);
    touch.touch_count=0U;CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_CONTINUE);
    touch.touch_count=1U;touch.touches[0]=(p4_game_point_t){285U,10U};CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_CONTINUE);CHECK(s.paused);
    touch.touch_count=0U;CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_CONTINUE);
    touch.touch_count=1U;touch.touches[0]=(p4_game_point_t){20U,10U};CHECK(p4_game_instance_update(&game,&touch,16U)==P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&game);
    CHECK(start(&game,&s,&mixer));quiet(&s);advance(&game,P4_BUTTON_LEFT,P4_BUTTON_LEFT,112U);
    uint16_t *a=calloc(768U*480U,sizeof(*a)),*b=calloc(768U*480U,sizeof(*b));CHECK(a&&b);
    if(a&&b){p4_game_surface_t surface={.pixels=a,.width=768U,.height=480U,.stride_pixels=768U};CHECK(p4_game_instance_render(&game,&surface));
        const uint8_t x=s.player_x;advance(&game,P4_BUTTON_LEFT,0U,32U);surface.pixels=b;CHECK(p4_game_instance_render(&game,&surface));
        CHECK(s.player_x==x&&memcmp(a,b,768U*480U*sizeof(*a))!=0);}
    free(a);free(b);p4_game_instance_stop(&game);
}
static void contact_scene(maze_chase_state_t *s,uint8_t px,uint8_t py,uint8_t tx,uint8_t ty,uint32_t progress,
                          uint8_t ex,uint8_t ey,uint8_t etx,uint8_t ety,uint32_t enemy_progress)
{
    s->intro=false;s->awaiting_move=false;s->protection_ms=0U;s->frightened_ms=0U;quiet(s);
    const maze_direction_t dir=tx<px?MAZE_DIRECTION_LEFT:MAZE_DIRECTION_RIGHT;
    place_player(s,px,py,dir);s->player_target_x=tx;s->player_target_y=ty;s->player_moving=true;s->player_move_accumulator_ms=progress;
    s->enemies[0]=(maze_enemy_t){.x=ex,.y=ey,.target_x=etx,.target_y=ety,.moving=true,
        .move_ms=enemy_progress,.step_duration_ms=176U,.direction=etx<ex?MAZE_DIRECTION_LEFT:MAZE_DIRECTION_RIGHT};
}
static void test_visible_contact_and_portals(void)
{
    p4_game_instance_t game;maze_chase_state_t s;p4_audio_mixer_t mixer;CHECK(start(&game,&s,&mixer));
    /* The targets coincide, but the visible centers begin two tiles apart. */
    contact_scene(&s,12U,10U,13U,10U,0U,14U,10U,13U,10U,0U);
    advance(&game,P4_BUTTON_RIGHT,0U,16U);CHECK(s.lives==3U&&!s.awaiting_move);
    advance(&game,P4_BUTTON_RIGHT,0U,100U);CHECK(s.lives==2U&&s.awaiting_move);
    p4_game_instance_stop(&game);CHECK(start(&game,&s,&mixer));
    contact_scene(&s,10U,6U,11U,6U,0U,11U,6U,10U,6U,0U);
    advance(&game,P4_BUTTON_RIGHT,0U,100U);CHECK(s.lives==2U&&s.awaiting_move); /* Opposite-direction crossing. */
    p4_game_instance_stop(&game);CHECK(start(&game,&s,&mixer));
    contact_scene(&s,12U,10U,13U,10U,100U,14U,10U,13U,10U,164U);
    advance(&game,P4_BUTTON_RIGHT,0U,12U);CHECK(s.lives==2U&&s.awaiting_move); /* Arrivals in one update. */
    p4_game_instance_stop(&game);CHECK(start(&game,&s,&mixer));
    contact_scene(&s,2U,1U,1U,1U,111U,1U,1U,1U,2U,0U);
    advance(&game,P4_BUTTON_LEFT,0U,1U);CHECK(s.lives==3U&&s.score==250U&&s.enemies[0].returning); /* Power arrival wins contact. */
    p4_game_instance_stop(&game);CHECK(start(&game,&s,&mixer));
    contact_scene(&s,0U,6U,24U,6U,111U,24U,6U,23U,6U,0U);
    advance(&game,P4_BUTTON_LEFT,0U,1U);CHECK(s.lives==2U&&s.awaiting_move); /* Contact at the receiving portal. */
    p4_game_instance_stop(&game);CHECK(start(&game,&s,&mixer));
    contact_scene(&s,0U,6U,0U,6U,0U,12U,6U,13U,6U,87U);
    s.player_moving=false;s.player_direction=MAZE_DIRECTION_UP;s.desired_direction=MAZE_DIRECTION_UP;
    advance(&game,P4_BUTTON_UP,0U,2U);CHECK(s.lives==3U&&!s.awaiting_move); /* Antipodal periodic-image seam is not contact. */
    p4_game_instance_stop(&game);
}
static void timer_partition_result(unsigned partition,bool protection,bool late,maze_chase_state_t *result)
{
    p4_game_instance_t game;p4_audio_mixer_t mixer;CHECK(start(&game,result,&mixer));
    if(late)contact_scene(result,12U,10U,13U,10U,0U,14U,10U,13U,10U,0U);
    else contact_scene(result,12U,10U,13U,10U,100U,13U,10U,12U,10U,0U);
    if(protection)result->protection_ms=80U;else result->frightened_ms=80U;
    for(unsigned t=0;t<100U;t+=partition)advance(&game,P4_BUTTON_RIGHT,0U,partition);
    p4_game_instance_stop(&game);
}
static void test_timer_partition_invariance(void)
{
    for(unsigned protection=0;protection<2U;++protection)for(unsigned late=0;late<2U;++late){
        maze_chase_state_t whole,split;timer_partition_result(100U,protection!=0U,late!=0U,&whole);
        timer_partition_result(10U,protection!=0U,late!=0U,&split);
        CHECK(whole.lives==split.lives&&whole.score==split.score&&whole.recovery_ms==split.recovery_ms);
        CHECK(whole.protection_ms==split.protection_ms&&whole.frightened_ms==split.frightened_ms);
        CHECK(whole.player_x==split.player_x&&whole.player_y==split.player_y);
        CHECK(whole.lives==(late?2U:3U));
        if(!protection&&!late)CHECK(whole.score>=200U);
    }
}
static uint32_t rng(uint32_t *s){*s=*s*1664525U+1013904223U;return *s;}
static void test_render_guard_and_fuzz(uint16_t width,uint16_t height)
{
    const size_t guard=31U,stride=(size_t)width+7U,words=stride*height,total=words+guard*2U;
    uint16_t *buffer=malloc(total*sizeof(*buffer));CHECK(buffer!=NULL);if(!buffer)return;
    for(size_t i=0;i<total;++i)buffer[i]=0x5aa5U;
    p4_game_surface_t surface={.pixels=buffer+guard,.width=width,.height=height,.stride_pixels=stride};
    p4_game_instance_t game;maze_chase_state_t s;p4_audio_mixer_t mixer;CHECK(start_width(&game,&s,&mixer,width));CHECK(p4_game_instance_render(&game,&surface));
    uint32_t random=0xc0ffee01U;
    for(unsigned i=0;i<1800U;++i){p4_game_input_t input={.held=rng(&random)&127U,.pressed=rng(&random)&127U};
        CHECK(p4_game_instance_update(&game,&input,rng(&random)%101U)==P4_GAME_CONTINUE);
        CHECK(maze_chase_cell_open(s.player_x,s.player_y));for(unsigned j=0;j<4U;++j)CHECK(maze_chase_cell_open(s.enemies[j].x,s.enemies[j].y));
        if(i%7U==0U)CHECK(p4_game_instance_render(&game,&surface));}
    s.intro=false;s.game_over=true;CHECK(p4_game_instance_render(&game,&surface));s.game_over=false;s.won=true;CHECK(p4_game_instance_render(&game,&surface));
    for(size_t i=0;i<guard;++i){CHECK(buffer[i]==0x5aa5U);CHECK(buffer[guard+words+i]==0x5aa5U);}
    for(size_t y=0;y<height;++y)for(size_t x=width;x<stride;++x)CHECK(surface.pixels[y*stride+x]==0x5aa5U);
    p4_game_instance_stop(&game);free(buffer);
}
int main(void)
{
    test_map_and_movement();test_mid_edge_reverse();test_recovery_guarantees();test_power_combo_return_and_modes();test_bonus_levels_extra_life();test_direct_touch_and_motion();test_visible_contact_and_portals();test_timer_partition_invariance();
    test_render_guard_and_fuzz(320U,200U);test_render_guard_and_fuzz(768U,480U);
    if(failures){fprintf(stderr,"%d Maze Chase failures\n",failures);return EXIT_FAILURE;}puts("Maze Chase overhaul regressions passed");return EXIT_SUCCESS;
}
