// SPDX-License-Identifier: MIT
/* Original arcade maze game; no ROM, franchise map, character or audio asset. */
#include "p4_games/maze_chase.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "maze_chase_internal.h"
#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/input.h"
#include "hires.h"
#include "generated/chase_art.inc"
enum {
    MAZE_TILE_SIZE=11, MAZE_ORIGIN_X=22, MAZE_ORIGIN_Y=24,
    PLAYER_MOVE_INTERVAL_MS=112, RECOVERY_DURATION_MS=700,
    SPAWN_PROTECTION_MS=1500, TUNNEL_ROW=6, FRUIT_X=12, FRUIT_Y=9,
};
/* A connected, original symmetric labyrinth. The two side portals join row 6. */
static const char s_maze[MAZE_CHASE_HEIGHT][MAZE_CHASE_WIDTH+1]={
    "#########################",
    "#o....#.....#.....#....o#",
    "#.##..#.###...###.#..##.#",
    "#.....#...........#.....#",
    "###.#.###.#.#.#.###.#.###",
    "#...#.....#...#.....#...#",
    "...###.#.........#.###...",
    "#......#.#..#..#.#......#",
    "#.####...#.....#...####.#",
    "#......#.#.....#.#......#",
    "#.##.#.#.........#.#.##.#",
    "#o...#......#......#...o#",
    "#########################",
};
static const uint8_t s_starts[4][2]={{11,6},{12,6},{13,6},{12,5}};
static const maze_direction_t s_directions[4]={MAZE_DIRECTION_UP,MAZE_DIRECTION_LEFT,MAZE_DIRECTION_DOWN,MAZE_DIRECTION_RIGHT};
static const uint16_t s_spirit_colors[4]={0xfa8a,0x4f7f,0xb3bf,0xfcc4};
static uint32_t countdown(uint32_t value,uint32_t elapsed){return value>elapsed?value-elapsed:0U;}
static int distance(int a,int b){return a>b?a-b:b-a;}
bool maze_chase_cell_open(int x,int y)
{return x>=0&&x<MAZE_CHASE_WIDTH&&y>=0&&y<MAZE_CHASE_HEIGHT&&s_maze[y][x]!='#';}
static void delta(maze_direction_t d,int *x,int *y)
{
    *x=d==MAZE_DIRECTION_LEFT?-1:(d==MAZE_DIRECTION_RIGHT?1:0);
    *y=d==MAZE_DIRECTION_UP?-1:(d==MAZE_DIRECTION_DOWN?1:0);
}
static maze_direction_t reverse(maze_direction_t d)
{
    switch(d){case MAZE_DIRECTION_UP:return MAZE_DIRECTION_DOWN;case MAZE_DIRECTION_DOWN:return MAZE_DIRECTION_UP;
        case MAZE_DIRECTION_LEFT:return MAZE_DIRECTION_RIGHT;case MAZE_DIRECTION_RIGHT:return MAZE_DIRECTION_LEFT;default:return MAZE_DIRECTION_NONE;}
}
static bool destination(uint8_t x,uint8_t y,maze_direction_t d,uint8_t *nx,uint8_t *ny)
{
    if(d==MAZE_DIRECTION_NONE)return false;
    int dx,dy;delta(d,&dx,&dy);int a=(int)x+dx,b=(int)y+dy;
    if(b==TUNNEL_ROW){if(a<0)a=MAZE_CHASE_WIDTH-1;else if(a>=MAZE_CHASE_WIDTH)a=0;}
    if(!maze_chase_cell_open(a,b))return false;
    *nx=(uint8_t)a;*ny=(uint8_t)b;return true;
}
static maze_direction_t requested(uint32_t buttons)
{
    if(buttons&P4_BUTTON_UP)return MAZE_DIRECTION_UP;
    if(buttons&P4_BUTTON_DOWN)return MAZE_DIRECTION_DOWN;
    if(buttons&P4_BUTTON_LEFT)return MAZE_DIRECTION_LEFT;
    if(buttons&P4_BUTTON_RIGHT)return MAZE_DIRECTION_RIGHT;
    return MAZE_DIRECTION_NONE;
}
static uint32_t direction_button(maze_direction_t d)
{
    switch(d){case MAZE_DIRECTION_UP:return P4_BUTTON_UP;case MAZE_DIRECTION_DOWN:return P4_BUTTON_DOWN;
        case MAZE_DIRECTION_LEFT:return P4_BUTTON_LEFT;case MAZE_DIRECTION_RIGHT:return P4_BUTTON_RIGHT;default:return 0U;}
}
static void tone(p4_game_context_t *c,uint16_t hz,uint16_t duration,uint8_t volume)
{(void)p4_game_play_tone(c,hz,duration,volume,P4_WAVE_TRIANGLE);}
static void clear_pellet(maze_chase_state_t *s,unsigned x,unsigned y)
{if(s->pellets[y][x]){s->pellets[y][x]=0U;--s->pellets_remaining;}}
static void reset_positions(maze_chase_state_t *s)
{
    s->player_x=12U;s->player_y=10U;s->player_direction=MAZE_DIRECTION_LEFT;s->desired_direction=MAZE_DIRECTION_LEFT;
    s->player_target_x=s->player_x;s->player_target_y=s->player_y;s->player_moving=false;s->player_move_accumulator_ms=0U;
    for(size_t i=0;i<4U;++i){
        s->enemies[i]=(maze_enemy_t){.x=s_starts[i][0],.y=s_starts[i][1],.color_index=(uint8_t)i,
            .direction=i==0U?MAZE_DIRECTION_LEFT:MAZE_DIRECTION_RIGHT,.release_ms=(uint32_t)i*900U};
        s->enemies[i].target_x=s->enemies[i].x;s->enemies[i].target_y=s->enemies[i].y;
    }
    s->frightened_ms=0U;s->ghost_chain=0U;s->mode_ms=0U;s->mode_phase=0U;
}
static void load_level(maze_chase_state_t *s)
{
    memset(s->pellets,0,sizeof(s->pellets));s->pellets_remaining=0U;
    for(unsigned y=0;y<MAZE_CHASE_HEIGHT;++y)for(unsigned x=0;x<MAZE_CHASE_WIDTH;++x)
        if(maze_chase_cell_open((int)x,(int)y)){s->pellets[y][x]=(uint8_t)(s_maze[y][x]=='o'?2U:1U);++s->pellets_remaining;}
    reset_positions(s);clear_pellet(s,s->player_x,s->player_y);
    for(unsigned i=0;i<4U;++i)clear_pellet(s,s->enemies[i].x,s->enemies[i].y);
    s->round_pellets=s->pellets_remaining;s->fruit_ms=0U;s->fruit_stage=0U;s->popup_ms=0U;
    s->won=false;s->level_clear_ms=0U;s->awaiting_move=true;s->protection_ms=0U;s->recovery_ms=0U;
}
void maze_chase_reset(maze_chase_state_t *s)
{
    if(s==NULL)return;memset(s,0,sizeof(*s));s->lives=3U;s->level=1U;s->next_extra_life=10000U;
    s->intro=true;load_level(s);
}
static void score_points(maze_chase_state_t *s,uint32_t amount)
{
    s->score=UINT32_MAX-s->score<amount?UINT32_MAX:s->score+amount;
    if(s->score>s->best_score)s->best_score=s->score;
    if(s->score>=s->next_extra_life&&s->next_extra_life!=UINT32_MAX){
        if(s->lives<5U)++s->lives;
        s->next_extra_life=UINT32_MAX-s->next_extra_life<10000U?UINT32_MAX:s->next_extra_life+10000U;
    }
}
static void popup(maze_chase_state_t *s,uint32_t points)
{s->popup_score=points;s->popup_ms=900U;s->popup_x=s->player_x;s->popup_y=s->player_y;}
static void reverse_spirits(maze_chase_state_t *s)
{for(unsigned i=0;i<4U;++i)if(!s->enemies[i].returning)s->enemies[i].reverse_pending=true;}
static uint32_t fright_duration(const maze_chase_state_t *s)
{return s->level>=9U?2500U:6500U-(uint32_t)(s->level-1U)*500U;}
static uint32_t enemy_interval(const maze_chase_state_t *s,const maze_enemy_t *e)
{
    if(e->returning)return 65U;
    if(s->frightened_ms&&!e->power_immune)return 242U;
    const uint32_t step=s->level>9U?8U:(uint32_t)s->level-1U;
    return 176U-step*5U;
}
static void collect(p4_game_context_t *c,maze_chase_state_t *s)
{
    const uint8_t item=s->pellets[s->player_y][s->player_x];
    if(item){clear_pellet(s,s->player_x,s->player_y);score_points(s,item==2U?50U:10U);
        if(item==2U){s->frightened_ms=fright_duration(s);s->ghost_chain=0U;
            for(unsigned i=0;i<4U;++i)s->enemies[i].power_immune=false;reverse_spirits(s);
            tone(c,392U,110U,5U);(void)p4_game_audio_effect_play(c,&s->audio,P4_GAME_AUDIO_EFFECT_REWARD);}
        else tone(c,(s->score&16U)?784U:988U,22U,3U);
        if(s->pellets_remaining==0U){s->won=true;s->level_clear_ms=1300U;tone(c,1047U,400U,5U);
            (void)p4_game_audio_effect_play(c,&s->audio,P4_GAME_AUDIO_EFFECT_REWARD);return;}
        const unsigned eaten=s->round_pellets-s->pellets_remaining;
        if((s->fruit_stage==0U&&eaten>=50U)||(s->fruit_stage==1U&&eaten>=120U)){
            ++s->fruit_stage;s->fruit_ms=12000U;}
    }
    if(s->fruit_ms&&s->player_x==FRUIT_X&&s->player_y==FRUIT_Y){
        const uint32_t points=100U*(s->level>5U?5U:(uint32_t)s->level);
        score_points(s,points);popup(s,points);s->fruit_ms=0U;tone(c,1175U,160U,4U);
    }
}
/* Returning eyes use a bounded shortest-path field so every original corridor
 * and tunnel leads back home; no random wandering or heap allocation. */
static maze_direction_t return_direction(const maze_enemy_t *e,size_t index)
{
    uint16_t distances[MAZE_CHASE_HEIGHT][MAZE_CHASE_WIDTH];
    uint8_t qx[MAZE_CHASE_WIDTH*MAZE_CHASE_HEIGHT],qy[MAZE_CHASE_WIDTH*MAZE_CHASE_HEIGHT];
    memset(distances,0xff,sizeof(distances));size_t read=0U,write=1U;
    qx[0]=s_starts[index][0];qy[0]=s_starts[index][1];distances[qy[0]][qx[0]]=0U;
    while(read<write){const uint8_t x=qx[read],y=qy[read++];
        for(unsigned d=0;d<4U;++d){uint8_t nx,ny;
            if(destination(x,y,s_directions[d],&nx,&ny)&&distances[ny][nx]==UINT16_MAX){
                distances[ny][nx]=(uint16_t)(distances[y][x]+1U);qx[write]=nx;qy[write++]=ny;}
        }
    }
    maze_direction_t best=MAZE_DIRECTION_NONE;uint16_t value=UINT16_MAX;
    for(unsigned d=0;d<4U;++d){uint8_t nx,ny;if(destination(e->x,e->y,s_directions[d],&nx,&ny)&&distances[ny][nx]<value){value=distances[ny][nx];best=s_directions[d];}}
    return best;
}
static maze_direction_t choose_direction(const maze_chase_state_t *s,const maze_enemy_t *e,size_t index)
{
    if(e->returning)return return_direction(e,index);
    uint8_t nx,ny;
    if(e->reverse_pending&&destination(e->x,e->y,reverse(e->direction),&nx,&ny))return reverse(e->direction);
    static const int corners[4][2]={{1,1},{23,1},{23,11},{1,11}};
    int tx=s->player_x,ty=s->player_y,dx,dy;delta(s->player_direction,&dx,&dy);
    if((s->mode_phase&1U)==0U){tx=corners[index][0];ty=corners[index][1];}
    else if(index==1U){tx+=dx*3;ty+=dy*3;}
    else if(index==2U){tx=(tx+dx*2)*2-s->enemies[0].x;ty=(ty+dy*2)*2-s->enemies[0].y;}
    else if(index==3U&&distance(e->x,tx)+distance(e->y,ty)<7){tx=corners[index][0];ty=corners[index][1];}
    maze_direction_t choices[4];unsigned count=0U;
    for(unsigned k=0;k<4U;++k){const maze_direction_t d=s_directions[(k+(unsigned)index+s->enemy_step)%4U];
        if(destination(e->x,e->y,d,&nx,&ny))choices[count++]=d;}
    maze_direction_t best=MAZE_DIRECTION_NONE;const bool frightened=s->frightened_ms&&!e->power_immune;int best_value=frightened?INT_MIN:INT_MAX;
    for(unsigned k=0;k<count;++k){const maze_direction_t d=choices[k];if(count>1U&&d==reverse(e->direction))continue;
        (void)destination(e->x,e->y,d,&nx,&ny);
        const int value=frightened?distance(nx,s->player_x)+distance(ny,s->player_y):distance(nx,tx)+distance(ny,ty);
        if((frightened&&value>best_value)||(!frightened&&value<best_value)){best_value=value;best=d;}
    }
    return best;
}
/* One motion timeline drives rendering, arrival scoring, and contact. Cells
 * represent centers already reached; targets are committed until arrival. */
static int motion_q8(uint8_t from,uint8_t target,uint32_t elapsed,uint32_t duration,bool moving)
{
    if(!moving||duration==0U)return (int)from*256;
    if(elapsed>duration)elapsed=duration;
    const int to=distance(from,target)>1?(from==0U?-1:MAZE_CHASE_WIDTH):(int)target;
    return (int)from*256+(to-(int)from)*256*(int)elapsed/(int)duration;
}
static void player_position(const maze_chase_state_t *s,int *x,int *y)
{
    *x=motion_q8(s->player_x,s->player_target_x,s->player_move_accumulator_ms,112U,s->player_moving);
    *y=motion_q8(s->player_y,s->player_target_y,s->player_move_accumulator_ms,112U,s->player_moving);
}
static void enemy_position(const maze_enemy_t *e,int *x,int *y)
{
    *x=motion_q8(e->x,e->target_x,e->move_ms,e->step_duration_ms,e->moving);
    *y=motion_q8(e->y,e->target_y,e->move_ms,e->step_duration_ms,e->moving);
}
static bool segment_contact(int rx0,int ry0,int rx1,int ry1)
{
    const int64_t x=rx0,y=ry0,vx=rx1-rx0,vy=ry1-ry0;
    const int64_t length=vx*vx+vy*vy,projection=-(x*vx+y*vy),radius2=160*160;
    if(length==0||projection<=0)return x*x+y*y<=radius2;
    if(projection>=length)return (x+vx)*(x+vx)+(y+vy)*(y+vy)<=radius2;
    return (x*x+y*y)*length-projection*projection<=radius2*length;
}
static bool swept_contact(int px0,int py0,int ex0,int ey0,int px1,int py1,int ex1,int ey1)
{
    /* Portal arrivals are discontinuous across the field, never a center sweep. */
    if(distance(px0,px1)>3*256||distance(ex0,ex1)>3*256){px0=px1;py0=py1;ex0=ex1;ey0=ey1;}
    const int rx0=px0-ex0,rx1=px1-ex1,ry0=py0-ey0,ry1=py1-ey1;
    if(segment_contact(rx0,ry0,rx1,ry1))return true;
    if(distance(py0,TUNNEL_ROW*256)<128&&distance(ey0,TUNNEL_ROW*256)<128&&
       distance(py1,TUNNEL_ROW*256)<128&&distance(ey1,TUNNEL_ROW*256)<128){
        /* Test consistent periodic images; independently normalizing endpoints
         * would create a false sweep when a spirit crosses the opposite side. */
        const int span=MAZE_CHASE_WIDTH*256;
        return segment_contact(rx0-span,ry0,rx1-span,ry1)||segment_contact(rx0+span,ry0,rx1+span,ry1);
    }
    return false;
}
static void collisions(p4_game_context_t *c,maze_chase_state_t *s,int px0,int py0,const int ex0[4],const int ey0[4])
{
    int px1,py1;player_position(s,&px1,&py1);
    for(size_t i=0;i<4U;++i){maze_enemy_t *e=&s->enemies[i];int ex1,ey1;enemy_position(e,&ex1,&ey1);
        if(e->returning||e->release_ms||!swept_contact(px0,py0,ex0[i],ey0[i],px1,py1,ex1,ey1))continue;
        if(s->frightened_ms&&!e->power_immune){const uint32_t points=200U<<(s->ghost_chain>3U?3U:s->ghost_chain);
            if(s->ghost_chain<4U)++s->ghost_chain;score_points(s,points);popup(s,points);
            e->returning=true;e->reverse_pending=false;tone(c,1319U,110U,4U);continue;}
        if(s->protection_ms)continue;
        if(s->lives)--s->lives;tone(c,110U,420U,5U);
        (void)p4_game_audio_effect_play(c,&s->audio,P4_GAME_AUDIO_EFFECT_FAIL);
        if(s->lives==0U)s->game_over=true;
        else{reset_positions(s);s->recovery_ms=RECOVERY_DURATION_MS;s->awaiting_move=true;s->protection_ms=0U;}
        return;
    }
}
/* Compact controls keep the full labyrinth visible. Touch is canonical 320x200;
 * ignore the platform's old virtual-pad synthesis while a finger is down. */
static uint32_t arrow_touch(int x,int y)
{
    if(y<172)return 0U;
    if(x>=4&&x<26)return P4_BUTTON_LEFT;
    if(x>=28&&x<50)return P4_BUTTON_DOWN;
    if(x>=52&&x<74)return P4_BUTTON_UP;
    if(x>=76&&x<98)return P4_BUTTON_RIGHT;
    return 0U;
}
static p4_game_input_t local_input(maze_chase_state_t *s,const p4_game_input_t *input)
{
    p4_game_input_t out=*input;uint32_t buttons=0U;
    const bool down=input->touch_valid&&input->touch_count>0U;
    if(down){const int x=input->touches[0].x,y=input->touches[0].y;
        if(!s->touch_was_down){
            s->touch_role=0U;s->touch_lock=0U;s->swipe_direction=MAZE_DIRECTION_NONE;
            s->touch_anchor_x=(uint16_t)x;s->touch_anchor_y=(uint16_t)y;
            if(y<24&&(x<52||x>=268)){s->touch_role=3U;s->touch_lock=x<52?P4_BUTTON_BACK:P4_BUTTON_START;}
            else if(arrow_touch(x,y)){s->touch_role=2U;}
            else if(y>=172&&x>=264){s->touch_role=3U;s->touch_lock=P4_BUTTON_A;}
            else if(s->intro&&x>=58&&x<=262&&y>=40&&y<=151){s->touch_role=3U;s->touch_lock=P4_BUTTON_A;}
            else if(y>=24&&y<172){s->touch_role=1U;}
        }
        if(s->touch_role==3U)buttons=s->touch_lock;
        else if(s->touch_role==2U)buttons=arrow_touch(x,y);
        else if(s->touch_role==1U){
            const int dx=x-(int)s->touch_anchor_x,dy=y-(int)s->touch_anchor_y;
            if(distance(dx,0)>=6||distance(dy,0)>=6){s->swipe_direction=distance(dx,0)>distance(dy,0)?(dx<0?MAZE_DIRECTION_LEFT:MAZE_DIRECTION_RIGHT):(dy<0?MAZE_DIRECTION_UP:MAZE_DIRECTION_DOWN);
                s->touch_anchor_x=(uint16_t)x;s->touch_anchor_y=(uint16_t)y;}
            buttons=direction_button(s->swipe_direction);
        }
        out.held=buttons;out.pressed=buttons&~s->touch_buttons;out.released=s->touch_buttons&~buttons;
    }
    s->touch_buttons=buttons;s->touch_was_down=down;return out;
}
static void move_player(p4_game_context_t *c,maze_chase_state_t *s,uint32_t elapsed)
{
    if(!s->player_moving){uint8_t nx,ny;
        if(destination(s->player_x,s->player_y,s->desired_direction,&nx,&ny))s->player_direction=s->desired_direction;
        if(!destination(s->player_x,s->player_y,s->player_direction,&nx,&ny)){s->player_move_accumulator_ms=0U;return;}
        s->player_target_x=nx;s->player_target_y=ny;s->player_moving=true;
    }
    if(s->desired_direction==reverse(s->player_direction)&&s->player_move_accumulator_ms>0U){
        const uint8_t x=s->player_x,y=s->player_y;
        s->player_x=s->player_target_x;s->player_y=s->player_target_y;
        s->player_target_x=x;s->player_target_y=y;
        s->player_move_accumulator_ms=PLAYER_MOVE_INTERVAL_MS-s->player_move_accumulator_ms;
        s->player_direction=s->desired_direction;
    }
    s->player_move_accumulator_ms+=elapsed;
    if(s->player_move_accumulator_ms>=112U){s->player_move_accumulator_ms-=112U;
        s->player_x=s->player_target_x;s->player_y=s->player_target_y;s->player_moving=false;collect(c,s);
        /* Commit the next edge immediately, preserving residual sub-frame time. */
        if(!s->won)move_player(c,s,0U);
    }
}
static void move_enemy(maze_chase_state_t *s,size_t index,uint32_t elapsed)
{
    maze_enemy_t *e=&s->enemies[index];
    if(e->release_ms){e->release_ms=countdown(e->release_ms,elapsed);return;}
    if(!e->moving){e->direction=choose_direction(s,e,index);e->reverse_pending=false;uint8_t nx,ny;
        if(!destination(e->x,e->y,e->direction,&nx,&ny)){e->move_ms=0U;return;}
        e->target_x=nx;e->target_y=ny;e->step_duration_ms=enemy_interval(s,e);e->moving=true;
    }
    e->move_ms+=elapsed;
    if(e->move_ms>=e->step_duration_ms){e->move_ms-=e->step_duration_ms;e->x=e->target_x;e->y=e->target_y;e->moving=false;
        if(e->returning&&e->x==s_starts[index][0]&&e->y==s_starts[index][1]){
            e->returning=false;e->power_immune=true;e->release_ms=1500U;e->move_ms=0U;return;}
        ++s->enemy_step;move_enemy(s,index,0U);
    }
}
static bool maze_start(p4_game_context_t *c)
{
    if(c==NULL||c->state==NULL||c->state_bytes!=sizeof(maze_chase_state_t))return false;
    maze_chase_reset(c->state);tone(c,523U,90U,3U);return true;
}
static p4_game_result_t maze_update(p4_game_context_t *c,const p4_game_input_t *raw,uint32_t elapsed)
{
    maze_chase_state_t *s=c->state;(void)p4_game_audio_effect_service(c,&s->audio);
    const p4_game_input_t input=local_input(s,raw);s->held_buttons=input.held;
    if(input.pressed&P4_BUTTON_BACK)return P4_GAME_EXIT_TO_LAUNCHER;
    if(!s->paused)s->presentation_ms=(s->presentation_ms+elapsed)%120000U;
    if(s->game_over){if(input.pressed&(P4_BUTTON_A|P4_BUTTON_START)){
        const uint32_t best=s->best_score;maze_chase_reset(s);s->best_score=best;s->intro=false;
        s->awaiting_move=false;s->protection_ms=SPAWN_PROTECTION_MS;}return P4_GAME_CONTINUE;}
    if(s->won){s->level_clear_ms=countdown(s->level_clear_ms,elapsed);
        if(s->level_clear_ms==0U){if(s->level<99U)++s->level;load_level(s);}return P4_GAME_CONTINUE;}
    const uint32_t begin=P4_BUTTON_A|P4_BUTTON_UP|P4_BUTTON_DOWN|P4_BUTTON_LEFT|P4_BUTTON_RIGHT;
    if(s->intro){if(!(input.pressed&(begin|P4_BUTTON_START)))return P4_GAME_CONTINUE;
        s->intro=false;s->awaiting_move=false;s->protection_ms=SPAWN_PROTECTION_MS;
    }else if(input.pressed&P4_BUTTON_START){s->paused=!s->paused;tone(c,s->paused?330U:660U,65U,3U);}
    if(s->paused)return P4_GAME_CONTINUE;
    if(s->recovery_ms){s->recovery_ms=countdown(s->recovery_ms,elapsed);return P4_GAME_CONTINUE;}
    if(s->awaiting_move){if(!(input.pressed&begin))return P4_GAME_CONTINUE;
        s->awaiting_move=false;s->protection_ms=SPAWN_PROTECTION_MS;}
    const maze_direction_t want=requested(input.held);if(want!=MAZE_DIRECTION_NONE)s->desired_direction=want;
    /* Millisecond slices are bounded by the API's 100 ms maximum. Motion,
     * contact, power expiry and spawn protection share this exact timeline,
     * giving identical outcomes for one 100 ms or ten 10 ms updates. */
    uint32_t remaining=elapsed;
    while(remaining){--remaining;
        int px0,py0,ex0[4],ey0[4];player_position(s,&px0,&py0);
        for(unsigned i=0;i<4U;++i)enemy_position(&s->enemies[i],&ex0[i],&ey0[i]);
        if(!s->frightened_ms&&s->mode_phase<5U){static const uint32_t phases[5]={6000U,18000U,5000U,18000U,4000U};
            ++s->mode_ms;if(s->mode_ms>=phases[s->mode_phase]){s->mode_ms=0U;++s->mode_phase;reverse_spirits(s);}}
        move_player(c,s,1U);
        if(s->won){s->level_clear_ms=countdown(s->level_clear_ms,remaining);return P4_GAME_CONTINUE;}
        for(unsigned i=0;i<4U;++i)move_enemy(s,i,1U);
        collisions(c,s,px0,py0,ex0,ey0);
        if(s->game_over||s->awaiting_move){s->recovery_ms=countdown(s->recovery_ms,remaining);return P4_GAME_CONTINUE;}
        s->protection_ms=countdown(s->protection_ms,1U);s->frightened_ms=countdown(s->frightened_ms,1U);
        s->popup_ms=countdown(s->popup_ms,1U);s->fruit_ms=countdown(s->fruit_ms,1U);
    }
    return P4_GAME_CONTINUE;
}
static void number_text(uint32_t value,char out[11])
{
    char reversed[10];size_t n=0U;do{reversed[n++]=(char)('0'+value%10U);value/=10U;}while(value&&n<10U);
    for(size_t i=0;i<n;++i)out[i]=reversed[n-i-1U];out[n]='\0';
}
static void label(p4_game_surface_t *surface,int center,int y,const char *text,uint16_t color,unsigned height)
{
    const unsigned h=surface->width==768U?height:(height*5U+6U)/12U;
    const int w=p4_ui_text_width(text,h,48U);
    p4_ui_text(surface,p4_ui_x(surface,center)-w/2,p4_ui_y(surface,y),text,color,h,48U);
}
static void art(p4_game_surface_t *s,int x,int y,int w,int h,unsigned frame)
{p4_ui_sprite(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,x+w)-p4_ui_x(s,x),p4_ui_y(s,y+h)-p4_ui_y(s,y),chase_art[frame%24U],48,48,true,0U);}
static int visual_axis(const p4_game_surface_t *s,int origin,uint8_t from,uint8_t to,uint32_t time,uint32_t interval,bool vertical)
{
    const int target=distance(from,to)>1?(from==0U?-1:MAZE_CHASE_WIDTH):(int)to;
    if(time>interval)time=interval;
    const int a=vertical?p4_ui_y(s,origin+(int)from*MAZE_TILE_SIZE):p4_ui_x(s,origin+(int)from*MAZE_TILE_SIZE);
    const int b=vertical?p4_ui_y(s,origin+target*MAZE_TILE_SIZE):p4_ui_x(s,origin+target*MAZE_TILE_SIZE);
    return a+(b-a)*(int)time/(int)interval;
}
static void shade(p4_game_surface_t *s,unsigned amount)
{
    for(unsigned y=24U*(unsigned)s->height/200U;y<s->height;++y){uint16_t *row=s->pixels+(size_t)y*s->stride_pixels;
        for(unsigned x=0;x<s->width;++x)row[x]=p4_ui_blend(row[x],UINT16_C(0x0023),amount);}
}
static void panel(p4_game_surface_t *s,int y,int height)
{
    p4_ui_round_rect(s,p4_ui_x(s,58),p4_ui_y(s,y+3),p4_ui_x(s,208),p4_ui_y(s,height),p4_ui_x(s,6),UINT16_C(0x0001));
    p4_ui_round_rect(s,p4_ui_x(s,56),p4_ui_y(s,y),p4_ui_x(s,208),p4_ui_y(s,height),p4_ui_x(s,6),UINT16_C(0x3d7b));
    p4_ui_round_rect(s,p4_ui_x(s,57),p4_ui_y(s,y+1),p4_ui_x(s,206),p4_ui_y(s,height-2),p4_ui_x(s,5),UINT16_C(0x0866));
    hi_fill_rect(s,75,y+3,170,1,UINT16_C(0x9f7f));
}
#if defined(MAZE_BACKDROP_GENERATOR) || defined(MAZE_BACKDROP_TEST)
static bool wall_at(int x,int y)
{return x>=0&&x<MAZE_CHASE_WIDTH&&y>=0&&y<MAZE_CHASE_HEIGHT&&s_maze[y][x]=='#';}
/* Connected rounded runs remove per-cell bevel seams. Neighbour quadrants fill
 * the interiors of thick wall islands, while open corridors keep a bright rim. */
static void wall_layer(p4_game_surface_t *s,int x,int y,int left,int top,int inset,uint16_t color)
{
    const int width=11-inset*2;
    p4_ui_round_rect(s,p4_ui_x(s,left+inset),p4_ui_y(s,top+inset),
        p4_ui_x(s,width),p4_ui_y(s,width),p4_ui_x(s,2),color);
    if(wall_at(x-1,y))hi_fill_rect(s,left,top+inset,6,width,color);
    if(wall_at(x+1,y))hi_fill_rect(s,left+5,top+inset,6,width,color);
    if(wall_at(x,y-1))hi_fill_rect(s,left+inset,top,width,6,color);
    if(wall_at(x,y+1))hi_fill_rect(s,left+inset,top+5,width,6,color);
    for(int dy=-1;dy<=1;dy+=2)for(int dx=-1;dx<=1;dx+=2)
        if(wall_at(x+dx,y)&&wall_at(x,y+dy)&&wall_at(x+dx,y+dy))
            hi_fill_rect(s,left+(dx>0?5:0),top+(dy>0?5:0),6,6,color);
}
static void draw_wall(p4_game_surface_t *s,unsigned x,unsigned y,int left,int top,bool clear)
{
    const uint16_t base=UINT16_C(0x016e);
    hi_fill_rect(s,left,top,11,11,UINT16_C(0x0846));
    wall_layer(s,(int)x,(int)y,left,top,1,clear?UINT16_C(0xffb2):UINT16_C(0x4e5e));
    wall_layer(s,(int)x,(int)y,left,top,2,base);
    /* Native-only low-contrast etching reuses the original ImageGen stone face.
     * It is deliberately inset; no old tile borders survive at wall joins. */
    if(s->width==768U){
        const int px=p4_ui_x(s,left+5)-4,py=p4_ui_y(s,top+5)-4;
        const uint16_t *texture=hi_art[12U+((x+y)&3U)];
        for(int row=0;row<8;++row)for(int col=0;col<8;++col)
            p4_draw_pixel(s,px+col,py+row,p4_ui_blend(base,texture[(12+row)*HI_ART_W+12+col],3U));
    }
}
static void draw_backdrop_reference(p4_game_surface_t *surface,bool won)
{
    for(unsigned y=0;y<surface->height;++y){const unsigned amount=y*6U/surface->height;
        p4_draw_fill_rect(surface,0,(int)y,surface->width,1,p4_ui_blend(UINT16_C(0x0845),UINT16_C(0x20e8),amount));}
    hi_fill_rect(surface,20,23,279,145,UINT16_C(0x022c));
    hi_rect(surface,20,23,279,145,UINT16_C(0x3c75));
    hi_fill_rect(surface,22,24,275,143,UINT16_C(0x0002));
    for(unsigned y=0;y<MAZE_CHASE_HEIGHT;++y)for(unsigned x=0;x<MAZE_CHASE_WIDTH;++x){
        const int left=MAZE_ORIGIN_X+(int)x*MAZE_TILE_SIZE,top=MAZE_ORIGIN_Y+(int)y*MAZE_TILE_SIZE;
        if(s_maze[y][x]=='#'){
            draw_wall(surface,x,y,left,top,won);
        }else{
            hi_fill_rect(surface,left,top,11,11,((x+y)&1U)?UINT16_C(0x0846):UINT16_C(0x0867));
            if(surface->width==768U){p4_draw_pixel(surface,p4_ui_x(surface,left)+2,p4_ui_y(surface,top)+2,UINT16_C(0x18c9));
                p4_draw_fill_rect(surface,p4_ui_x(surface,left)+1,p4_ui_y(surface,top+11)-1,p4_ui_x(surface,11)-2,1,UINT16_C(0x0024));}

        }
    }
    /* Luminous portal brackets explain the only wraparound passage. */
    const int portal_y=MAZE_ORIGIN_Y+TUNNEL_ROW*MAZE_TILE_SIZE;
    hi_fill_rect(surface,18,portal_y,2,11,UINT16_C(0x757f));
    hi_fill_rect(surface,299,portal_y,2,11,UINT16_C(0x757f));
 }
#endif
#ifndef MAZE_BACKDROP_GENERATOR
#include "generated/backdrop.inc"
#endif
/* Complete native frames without re-rasterizing immutable wall geometry.
 * Runs are cartridge-owned, generated and pixel-checked at both resolutions.
 * No retained framebuffer, heap allocation or dependence on buffer contents. */
static void draw_backdrop(p4_game_surface_t *surface,bool won)
{
#ifdef MAZE_BACKDROP_GENERATOR
    draw_backdrop_reference(surface,won);
#else
    const uint32_t *offsets=surface->width==768U?maze_backdrop_rows_768:maze_backdrop_rows_320;
    for(unsigned y=0;y<surface->height;++y){
        uint16_t *dst=surface->pixels+(size_t)y*surface->stride_pixels;
        const uint32_t *run=maze_backdrop_runs+offsets[y];
        unsigned remaining=surface->width;
        while(remaining){const uint32_t packed=*run++;
            unsigned count=packed>>16U;
            uint16_t color=(uint16_t)packed;
            if(won&&color==UINT16_C(0x4e5e))color=UINT16_C(0xffb2);
            /* The generated data is verified, but bound every destination row. */
            if(count==0U||count>remaining)count=remaining;
            remaining-=count;
            uint16_t *const end=dst+count;
            do{*dst++=color;}while(dst!=end);
        }
    }
#endif
}
static void draw_board(p4_game_surface_t *surface,const maze_chase_state_t *state)
{
    draw_backdrop(surface,state->won);
    for(unsigned y=0;y<MAZE_CHASE_HEIGHT;++y)for(unsigned x=0;x<MAZE_CHASE_WIDTH;++x){
        const int left=MAZE_ORIGIN_X+(int)x*MAZE_TILE_SIZE,top=MAZE_ORIGIN_Y+(int)y*MAZE_TILE_SIZE;
            const uint8_t item=state->pellets[y][x];
            if(item==2U){hi_circle(surface,left+5,top+5,4,UINT16_C(0x5227));
                art(surface,left+1,top+1,9,9,21U);
                if((state->presentation_ms/180U)%2U==0U)hi_pixel(surface,left+5,top,UINT16_C(0xffdb));
            }else if(item)art(surface,left+4,top+4,3,3,20U);
    }
    if(state->fruit_ms){const int bob=(state->presentation_ms/200U)%2U?0:1;
        art(surface,MAZE_ORIGIN_X+FRUIT_X*MAZE_TILE_SIZE-1,MAZE_ORIGIN_Y+FRUIT_Y*MAZE_TILE_SIZE-1-bob,13,13,19U);}
}
/* Only the two tunnel lips clip/wrap actors. Sample the original 48px art
 * directly at native size so crossing the seam never waits or teleports. */
static void actor_sprite(p4_game_surface_t *s,int x,int y,unsigned frame,bool tunnel)
{
    const int w=p4_ui_x(s,13),h=p4_ui_y(s,13);
    x-=p4_ui_x(s,1);y-=p4_ui_y(s,1);
    if(!tunnel){p4_ui_sprite(s,x,y,w,h,chase_art[frame],48,48,true,0U);return;}
    const int left=p4_ui_x(s,MAZE_ORIGIN_X),right=p4_ui_x(s,MAZE_ORIGIN_X+MAZE_CHASE_WIDTH*MAZE_TILE_SIZE);
    const int span=right-left;
    for(int copy=-1;copy<=1;++copy){const int at=x+copy*span;
        const int lo=at<left?left-at:0,hi=at+w>right?right-at:w;
        if(lo>=hi)continue;
        for(int row=0;row<h;++row){const int yy=y+row;if(yy<0||yy>=s->height)continue;
            uint16_t *dst=s->pixels+(size_t)yy*s->stride_pixels;
            const uint16_t *src=chase_art[frame]+(row*48/h)*48;
            for(int col=lo;col<hi;++col){const uint16_t color=src[col*48/w];if(color)dst[at+col]=color;}
        }
    }
}
static void draw_actors(p4_game_surface_t *surface,const maze_chase_state_t *state)
{
    int x=visual_axis(surface,MAZE_ORIGIN_X,state->player_x,state->player_moving?state->player_target_x:state->player_x,state->player_move_accumulator_ms,112U,false);
    int y=visual_axis(surface,MAZE_ORIGIN_Y,state->player_y,state->player_moving?state->player_target_y:state->player_y,state->player_move_accumulator_ms,112U,true);
    unsigned direction=0U;if(state->player_direction==MAZE_DIRECTION_DOWN)direction=1U;
    else if(state->player_direction==MAZE_DIRECTION_LEFT)direction=2U;else if(state->player_direction==MAZE_DIRECTION_UP)direction=3U;
    static const unsigned mouth[4]={0U,1U,2U,1U};const unsigned pose=mouth[(state->presentation_ms/70U)%4U];
    if(state->protection_ms){p4_draw_fill_circle(surface,x+p4_ui_x(surface,5),y+p4_ui_y(surface,5),p4_ui_x(surface,7),UINT16_C(0x23b6));
        p4_draw_fill_circle(surface,x+p4_ui_x(surface,5),y+p4_ui_y(surface,5),p4_ui_x(surface,6),UINT16_C(0x08a8));}
    actor_sprite(surface,x,y,pose*4U+direction,state->player_y==TUNNEL_ROW);
    for(size_t i=0;i<4U;++i){const maze_enemy_t *e=&state->enemies[i];const uint32_t interval=e->moving?e->step_duration_ms:enemy_interval(state,e);
        x=visual_axis(surface,MAZE_ORIGIN_X,e->x,e->moving?e->target_x:e->x,e->move_ms,interval,false);
        y=visual_axis(surface,MAZE_ORIGIN_Y,e->y,e->moving?e->target_y:e->y,e->move_ms,interval,true);
        unsigned frame=12U+(unsigned)i;
        if(e->returning)frame=18U;
        else if(state->frightened_ms&&!e->power_immune&&e->release_ms==0U)frame=state->frightened_ms<1800U&&((state->presentation_ms/140U)&1U)?17U:16U;
        const int bob=state->paused?0:(int)((state->presentation_ms/120U+i)%2U);
        p4_draw_fill_circle(surface,x+p4_ui_x(surface,5),y+p4_ui_y(surface,9),p4_ui_x(surface,4),UINT16_C(0x0002));
        actor_sprite(surface,x,y-bob,frame,e->y==TUNNEL_ROW);
    }
    if(state->popup_ms){char value[11];number_text(state->popup_score,value);
        label(surface,MAZE_ORIGIN_X+(int)state->popup_x*11+5,MAZE_ORIGIN_Y+(int)state->popup_y*11-5,value,UINT16_C(0xffdb),20U);}
}
static void button(p4_game_surface_t *s,int x,int y,int w,int h,const char *text,uint32_t mask,uint32_t held)
{
    const bool active=(held&mask)!=0U;const uint16_t border=active?UINT16_C(0xb7ff):UINT16_C(0x43d3);
    p4_ui_round_rect(s,p4_ui_x(s,x),p4_ui_y(s,y),p4_ui_x(s,w),p4_ui_y(s,h),p4_ui_x(s,3),border);
    p4_ui_round_rect(s,p4_ui_x(s,x+1),p4_ui_y(s,y+1),p4_ui_x(s,w-2),p4_ui_y(s,h-2),p4_ui_x(s,2),active?UINT16_C(0x1b31):UINT16_C(0x08a8));
    label(s,x+w/2,y+(h-10)/2,text,UINT16_C(0xe7df),22U);
}
static void draw_hud(p4_game_surface_t *surface,const maze_chase_state_t *state)
{
    char value[11];hi_fill_rect(surface,0,0,320,23,UINT16_C(0x0845));
    button(surface,0,0,52,22,"EXIT",P4_BUTTON_BACK,state->held_buttons);
    button(surface,268,0,52,22,state->paused?"RESUME":"PAUSE",P4_BUTTON_START,state->held_buttons);
    label(surface,160,4,"MAZE CHASE",UINT16_C(0xf7ba),29U);
    label(surface,83,1,"SCORE",UINT16_C(0x7cf5),15U);number_text(state->score,value);label(surface,83,9,value,UINT16_C(0xffff),22U);
    label(surface,239,1,"ROUND",UINT16_C(0x7cf5),15U);number_text(state->level,value);label(surface,239,9,value,UINT16_C(0xffff),22U);
    hi_fill_rect(surface,0,171,320,29,UINT16_C(0x0845));
    button(surface,4,173,22,25,"<",P4_BUTTON_LEFT,state->held_buttons);
    button(surface,28,173,22,25,"v",P4_BUTTON_DOWN,state->held_buttons);
    button(surface,52,173,22,25,"^",P4_BUTTON_UP,state->held_buttons);
    button(surface,76,173,22,25,">",P4_BUTTON_RIGHT,state->held_buttons);
    label(surface,124,172,"LIVES",UINT16_C(0x7cf5),14U);
    for(unsigned i=0;i<state->lives;++i)art(surface,105+(int)i*9,182,10,10,0U);
    number_text(state->pellets_remaining,value);label(surface,178,181,value,UINT16_C(0xe7df),22U);
    label(surface,178,172,"PEARLS",UINT16_C(0x7cf5),14U);
    const char *mode=state->frightened_ms?"HUNT":((state->mode_phase&1U)?"CHASE":"SCATTER");
    label(surface,231,173,mode,state->frightened_ms?UINT16_C(0xffb2):UINT16_C(0x6e9c),18U);
    hi_fill_rect(surface,209,187,44,3,UINT16_C(0x19cd));
    if(state->frightened_ms)hi_fill_rect(surface,209,187,(int)(44U*state->frightened_ms/fright_duration(state)),3,UINT16_C(0xffb2));
    else for(unsigned i=0;i<4U;++i)hi_fill_rect(surface,209+(int)i*11,187,8,3,s_spirit_colors[i]);
    button(surface,264,173,52,25,state->game_over?"RETRY":"GO",P4_BUTTON_A,state->held_buttons);
}
static bool maze_render(p4_game_context_t *context,p4_game_surface_t *surface)
{
    if(!p4_surface_valid(surface))return false;const maze_chase_state_t *s=context->state;
    draw_board(surface,s);draw_actors(surface,s);
    if(s->intro){shade(surface,8U);panel(surface,37,114);
        label(surface,160,48,"MAZE CHASE",UINT16_C(0xf7ba),47U);
        label(surface,160,72,"COLLECT. POWER UP. TURN THE TABLES.",UINT16_C(0x9e5c),18U);
        p4_ui_sprite(surface,p4_ui_x(surface,83),p4_ui_y(surface,87),p4_ui_x(surface,44),p4_ui_y(surface,44),chase_hero,128,128,true,0U);
        static const char *const names[4]={"EMBER","GLINT","HEX","PUFF"};
        for(unsigned i=0;i<4U;++i){art(surface,139+(int)i*23,98,22,25,12U+i);
            label(surface,150+(int)i*23,124,names[i],s_spirit_colors[i],13U);}
        label(surface,160,136,"A / DIRECTION TO BEGIN",UINT16_C(0x9fff),22U);
    }else if(s->paused||s->game_over||s->won){shade(surface,8U);panel(surface,57,88);
        label(surface,160,69,s->won?"EVERY PEARL COLLECTED":(s->game_over?"THE SPIRITS CAUGHT UP":"TAKE A BREATHER"),UINT16_C(0x8dfb),18U);
        label(surface,160,83,s->won?"ROUND CLEAR":(s->game_over?"GAME OVER":"PAUSED"),UINT16_C(0xf7ba),39U);
        if(s->game_over){char value[11];number_text(s->score,value);label(surface,160,104,value,UINT16_C(0xffff),26U);}
        label(surface,160,126,s->won?"NEXT ROUND INCOMING":(s->game_over?"A TO TRY AGAIN":"START TO RESUME"),UINT16_C(0x9fff),21U);
    }else if(s->awaiting_move){panel(surface,78,41);label(surface,160,87,s->recovery_ms?"GET READY":"READY FOR ANOTHER RUN?",UINT16_C(0xf7ba),24U);
        label(surface,160,102,"DIRECTION OR A TO BEGIN",UINT16_C(0x9fff),18U);}
    draw_hud(surface,s);return true;
}
static void maze_stop(p4_game_context_t *context){p4_game_stop_audio(context);}
const p4_game_descriptor_t p4_maze_chase_game={
    .api_version=P4_GAME_API_VERSION,.launcher_id=100U,.id="org.p4console.maze-chase",.title="Maze Chase",
    .subtitle="Power up. Chase the spirits.",.accent_rgb565=UINT16_C(0xffe0),
    .required_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS,
    .optional_capabilities=P4_GAME_CAP_AUDIO_TONE|P4_GAME_CAP_AUDIO_STREAM|P4_GAME_CAP_VIDEO_HIGH_RES,
    .state_bytes=sizeof(maze_chase_state_t),.start=maze_start,.update=maze_update,.render=maze_render,.stop=maze_stop,
};
