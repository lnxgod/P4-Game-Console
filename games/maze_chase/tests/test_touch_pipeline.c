// SPDX-License-Identifier: MIT
/* Exercise actual queued desktop contacts, platform mapping, and host slices. */
#include "p4_games/maze_chase.h"
#include "maze_chase_internal.h"
#include "p4/input.h"
#include "host_mouse.h"
#include "host_service.h"
#include <stdio.h>
#include <stdlib.h>
static int failures;
#define CHECK(c) do {if(!(c)){fprintf(stderr,"TOUCH FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
static p4_physical_touch_t physical(unsigned x,unsigned y)
{
    return (p4_physical_touch_t){
        (uint16_t)(P4_INPUT_VIEWPORT_LEFT+((x*2U+1U)*P4_INPUT_VIEWPORT_WIDTH)/(320U*2U)),
        (uint16_t)(P4_INPUT_VIEWPORT_TOP+((y*2U+1U)*P4_INPUT_VIEWPORT_HEIGHT)/(200U*2U))};
}
static p4_game_result_t service(p4_game_instance_t *game,p4_host_mouse_t *mouse,p4_game_input_mapper_t *mapper,uint32_t ms)
{
    const p4_host_mouse_sample_t sample=p4_host_mouse_next(mouse);
    p4_game_input_t input;
    p4_game_input_mapper_update(mapper,sample.valid,&sample.point,sample.down?1U:0U,0U,&input);
    return p4_host_service_update(game,&input,ms,NULL,NULL);
}
static void click(p4_host_mouse_t *mouse,unsigned x,unsigned y)
{
    /* The complete down/up pair arrives before a frame, like a short CUA click. */
    p4_host_mouse_press(mouse,physical(x,y));p4_host_mouse_release(mouse);
}
static void test_pipeline(uint16_t width)
{
    p4_game_services_t services={.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|(width==768U?P4_GAME_CAP_VIDEO_HIGH_RES:0U)};
    p4_game_instance_t game={0};maze_chase_state_t state;
    /* Preserve the explicit legacy input branch without weakening the actual cartridge. */
    p4_game_descriptor_t legacy = p4_maze_chase_game;
    legacy.required_capabilities &= ~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
    legacy.optional_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
    if (width != 768U) {
        CHECK(!p4_game_instance_start(&game,&p4_maze_chase_game,&services,&state,sizeof(state)));
    }
    CHECK(p4_game_instance_start(&game,width==768U?&p4_maze_chase_game:&legacy,&services,&state,sizeof(state)));
    p4_game_input_mapper_t mapper;p4_game_input_mapper_init(&mapper);
    p4_host_mouse_t mouse={.current={.valid=true}};
    p4_game_point_t point;const p4_physical_touch_t start_point=physical(290U,184U);
    CHECK(p4_game_map_physical_touch(start_point.x,start_point.y,&point));CHECK(point.x==290U&&point.y==184U);
    click(&mouse,290U,184U);CHECK(service(&game,&mouse,&mapper,100U)==P4_GAME_CONTINUE);
    CHECK(!state.intro&&!state.paused&&!state.awaiting_move);
    CHECK(service(&game,&mouse,&mapper,33U)==P4_GAME_CONTINUE);CHECK(!state.touch_was_down);
    state.game_over=true;state.lives=0U;state.score=300U;
    click(&mouse,290U,184U);CHECK(service(&game,&mouse,&mapper,100U)==P4_GAME_CONTINUE);
    CHECK(!state.game_over&&state.lives==3U&&!state.intro&&state.score==0U);
    CHECK(service(&game,&mouse,&mapper,33U)==P4_GAME_CONTINUE);CHECK(!state.touch_was_down);
    click(&mouse,290U,11U);CHECK(service(&game,&mouse,&mapper,100U)==P4_GAME_CONTINUE);CHECK(state.paused);
    CHECK(service(&game,&mouse,&mapper,100U)==P4_GAME_CONTINUE);CHECK(state.paused&&!state.touch_was_down);
    click(&mouse,290U,11U);CHECK(service(&game,&mouse,&mapper,100U)==P4_GAME_CONTINUE);CHECK(!state.paused);
    CHECK(service(&game,&mouse,&mapper,33U)==P4_GAME_CONTINUE);
    click(&mouse,60U,184U);CHECK(service(&game,&mouse,&mapper,33U)==P4_GAME_CONTINUE);CHECK(state.desired_direction==MAZE_DIRECTION_UP);
    CHECK(service(&game,&mouse,&mapper,33U)==P4_GAME_CONTINUE);
    p4_host_mouse_press(&mouse,physical(150U,100U));p4_host_mouse_move(&mouse,physical(20U,10U));p4_host_mouse_release(&mouse);
    CHECK(service(&game,&mouse,&mapper,33U)==P4_GAME_CONTINUE);
    CHECK(service(&game,&mouse,&mapper,33U)==P4_GAME_CONTINUE);CHECK(!state.paused);
    CHECK(service(&game,&mouse,&mapper,33U)==P4_GAME_CONTINUE);
    click(&mouse,20U,11U);CHECK(service(&game,&mouse,&mapper,33U)==P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&game);
}
int main(void)
{
    test_pipeline(768U);test_pipeline(320U);
    if(failures)return EXIT_FAILURE;puts("Maze actual mapper / queued touch / multi-slice host pipeline passed");return EXIT_SUCCESS;
}
