// SPDX-License-Identifier: MIT
/* Original frog-crossing game using the P4 Game API v1 and original art. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "frog_hop_internal.h"

#include "p4/audio_pack.h"
#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/game.h"
#include "p4/input.h"
#include "hires.h"
#include "generated/environment.inc"

enum {
    HUD_HEIGHT = 18,
    GRID_TOP = HUD_HEIGHT,
    GRID_ROWS = 10,
    ROW_HEIGHT = 15,
    START_ROW = GRID_ROWS - 1,
    WATER_FIRST_ROW = 1,
    WATER_ROW_COUNT = 4,
    SAFE_ROW = 5,
    ROAD_FIRST_ROW = 6,
    ROAD_ROW_COUNT = 3,
    GRID_STEP_X = 20,
    FROG_MIN_X = 20,
    FROG_MAX_X = 300,
    /* Keep the moving lanes readable at level 1 (100 px/s maximum). */
    WORLD_STEP_MS = 20,
    ATLAS_WIDTH = 160,
    FRAME_WIDTH = 40,
    FRAME_HEIGHT = 25,
    FROG_ROW = 0,
    CAR_ROW = 1,
    LOG_ROW = 2,
    PAD_ROW = 3,
    GOAL_COUNT = 5,
    HOME_MASK = (1U << GOAL_COUNT) - 1U,
};

static const uint16_t COLOR_NAVY = UINT16_C(0x0925);
static const uint16_t COLOR_WATER_LIGHT = UINT16_C(0x8f5b);
static const uint16_t COLOR_LANE = UINT16_C(0xe6b4);
static const uint16_t COLOR_WHITE = UINT16_C(0xffff);
static const uint16_t COLOR_TEXT = UINT16_C(0xc6d8);
static const int s_goal_x[GOAL_COUNT] = {32, 96, 160, 224, 288};

typedef struct {
    int16_t spacing;
    int8_t speed;
} moving_lane_t;



static const moving_lane_t s_road_lanes[ROAD_ROW_COUNT] = {
    {.spacing = 112, .speed = 2},
    {.spacing = 132, .speed = -1},
    {.spacing = 96, .speed = 1},
};

static const moving_lane_t s_log_lanes[WATER_ROW_COUNT] = {
    {.spacing = 94, .speed = 1},
    {.spacing = 122, .speed = -1},
    {.spacing = 106, .speed = 1},
    {.spacing = 138, .speed = -1},
};

static int row_top(unsigned row)
{
    return GRID_TOP + (int)row * ROW_HEIGHT;
}

static int wrap_offset(int value, int spacing, uint8_t *variant)
{
    while (value >= 0) {
        value -= spacing;
        *variant = (uint8_t)((*variant + 3U) & 3U);
    }
    while (value < -spacing) {
        value += spacing;
        *variant = (uint8_t)((*variant + 1U) & 3U);
    }
    return value;
}

static uint16_t tick_down(uint16_t value, uint16_t amount)
{
    return value > amount ? (uint16_t)(value - amount) : 0U;
}

static void play_tone(p4_game_context_t *context, uint16_t frequency_hz,
                      uint16_t duration_ms, uint8_t volume,
                      p4_waveform_t waveform)
{
    (void)p4_game_play_tone(context, frequency_hz, duration_ms, volume,
                            waveform);
}

static void reset_frog(frog_hop_state_t *state)
{
    state->frog_x = 160;
    state->frog_row = START_ROW;
    state->visual_from_x_q8=(int32_t)state->frog_x*256;
    state->visual_from_y_q8=(int32_t)(row_top(START_ROW)+7)*256;
    state->hop_ms=0U;
}

static void reset_round(frog_hop_state_t *state)
{
    *state = (frog_hop_state_t){
        .frog_x = 160,
        .splash_x = 160,
        .frog_row = START_ROW,
        .splash_row = START_ROW,
        .lives = 3U,
        .level = 1U,
        .intro = true,
        .road_offsets = {-22, -79, -51},
        .log_offsets = {-37, -88, -24, -110},
    };
    reset_frog(state);
}

static void draw_frame(p4_game_surface_t *surface, int x, int y,
                       unsigned atlas_row, unsigned frame)
{
    hi_frame(surface,x,y,FRAME_WIDTH,FRAME_HEIGHT,atlas_row*4U+(frame&3U));
}

static void u32_text(uint32_t value,char output[11])
{
    char reversed[10];size_t count=0U;
    do {reversed[count++]=(char)('0'+value%10U);value/=10U;} while(value!=0U);
    for(size_t i=0U;i<count;++i)output[i]=reversed[count-i-1U];
    output[count]='\0';
}

/* Sample the material field at native resolution. Only visible pixels are
 * written, preserving padded strides; no framebuffer or decoder allocation. */
static void draw_material(p4_game_surface_t *surface, int top, int height,
                          unsigned material, unsigned phase)
{
    int first = p4_ui_y(surface, top);
    int last = p4_ui_y(surface, top + height);
    if (first < 0) first = 0;
    if (last > (int)surface->height) last = surface->height;
    for (int y = first; y < last; ++y) {
        const unsigned ty = ((unsigned)y * 480U / surface->height) % TERRAIN_H;
        const uint16_t *source = terrain_art[material & 3U] + ty * TERRAIN_W;
        uint16_t *dest = surface->pixels + (size_t)y * surface->stride_pixels;
        if (surface->width == 768U) {
            /* Six repeating native tile spans; no address/mask per pixel. */
            unsigned x = 0U, offset = phase & (TERRAIN_W - 1U);
            while (x < 768U) {
                unsigned count = TERRAIN_W - offset;
                if (count > 768U - x) count = 768U - x;
                memcpy(dest + x, source + offset, count * sizeof(*dest));
                x += count; offset = 0U;
            }
        } else {
            for (unsigned x=0U;x<surface->width;++x)
                dest[x]=source[((x*12U)/5U+phase)&(TERRAIN_W-1U)];
        }
    }
}

static void scene_shade(p4_game_surface_t *surface, int top, int height,
                        uint16_t color, unsigned opacity)
{
    int first = p4_ui_y(surface,top), last = p4_ui_y(surface,top+height);
    if (first < 0) first = 0;
    if (last > (int)surface->height) last = surface->height;
    for (int y=first;y<last;++y) {
        uint16_t *row=surface->pixels+(size_t)y*surface->stride_pixels;
        for(unsigned x=0;x<surface->width;++x)
            row[x]=p4_ui_blend(row[x],color,opacity);
    }
}

static void centered_text(p4_game_surface_t *surface, int y, const char *text,
                          uint16_t color, unsigned height)
{
    const unsigned pixels=surface->width==768U?height:(height*5U+6U)/12U;
    const int width=p4_ui_text_width(text,pixels,40U);
    p4_ui_text(surface,((int)surface->width-width)/2,p4_ui_y(surface,y),
               text,color,pixels,40U);
}

static void shore_edge(p4_game_surface_t *surface,int top,bool below)
{
    const int sign=below?1:-1;
    for(int x=0;x<320;x+=4) {
        const int depth=2+((x*7)%3);
        const int y=below?top:top-depth;
        hi_fill_rect(surface,x,y,4,depth,UINT16_C(0x6269));
        hi_fill_rect(surface,x,y+(below?depth-1:0),4,1,UINT16_C(0xb4d1));
        hi_fill_rect(surface,x,top+sign*(depth+1),3,1,UINT16_C(0x19a8));
    }
}

static void draw_course(p4_game_surface_t *surface,
                        const frog_hop_state_t *state)
{
    draw_material(surface,0,row_top(WATER_FIRST_ROW),1U,0U);
    draw_material(surface,row_top(SAFE_ROW),ROW_HEIGHT,1U,0U);
    draw_material(surface,row_top(START_ROW),ROW_HEIGHT,1U,0U);
    draw_material(surface,row_top(WATER_FIRST_ROW),ROW_HEIGHT*WATER_ROW_COUNT,0U,
                  (state->scenery_ms / 80U) & 127U);
    shore_edge(surface,row_top(WATER_FIRST_ROW),true);
    shore_edge(surface,row_top(SAFE_ROW),false);
    draw_material(surface,row_top(ROAD_FIRST_ROW),ROW_HEIGHT*ROAD_ROW_COUNT,2U,0U);
    /* Static paint locates each traffic lane; cars provide the motion. */
    for (unsigned lane=1U;lane<ROAD_ROW_COUNT;++lane) {
        const int y=row_top(ROAD_FIRST_ROW+lane);
        for(int x=8;x<320;x+=31) {
            hi_fill_rect(surface,x,y,14,1,COLOR_LANE);
            if(surface->width==768U)
                p4_draw_fill_rect(surface,p4_ui_x(surface,x),p4_ui_y(surface,y)+1,
                                 p4_ui_x(surface,14),1,UINT16_C(0xf7dc));
        }
    }
    const int roads[2]={row_top(ROAD_FIRST_ROW),row_top(START_ROW)};
    for(unsigned edge=0;edge<2U;++edge) {
        const int y=roads[edge];
        hi_fill_rect(surface,0,y-1,320,1,UINT16_C(0xad50));
        hi_fill_rect(surface,0,y,320,1,UINT16_C(0x2945));
        for(int x=0;x<320;x+=12)
            hi_fill_rect(surface,x,y-1,1,1,UINT16_C(0x5269));
    }
    /* A pebbled lower garden replaces the former empty cyan strip. */
    draw_material(surface,row_top(START_ROW)+ROW_HEIGHT,32,3U,0U);
    shore_edge(surface,row_top(START_ROW)+ROW_HEIGHT,true);
    scene_shade(surface,180,20,COLOR_NAVY,5U);
}

static void draw_homes(p4_game_surface_t *surface,
                       const frog_hop_state_t *state)
{
    for (unsigned home = 0U; home < GOAL_COUNT; ++home) {
        const int x = s_goal_x[home] - FRAME_WIDTH / 2;
        draw_frame(surface, x, row_top(0) - 5, PAD_ROW,
                   state->animation_frame);
        if ((state->homes & (uint8_t)(UINT8_C(1) << home)) != 0U) {
            draw_frame(surface, x, row_top(0) - 6, FROG_ROW, 0U);
        }
    }
}

/* Reconstruct the preceding world sample and interpolate its remainder.
 * A 20 ms presentation delay keeps each vehicle/log on a continuous path;
 * physics still uses exactly the original fixed-step positions. */
int32_t frog_hop_lane_visual_q8(const frog_hop_state_t *state, bool water, unsigned lane)
{
    const int offset = water ? state->log_offsets[lane] : state->road_offsets[lane];
    const int speed = (water ? s_log_lanes[lane].speed : s_road_lanes[lane].speed) * (int)state->level;
    const int remaining = state->world_advanced ? WORLD_STEP_MS - (int)state->simulation_ms : 0;
    return offset * 256 - speed * remaining * 256 / WORLD_STEP_MS;
}
static void draw_lane_frame(p4_game_surface_t *surface, int x_q8, int y, unsigned frame)
{
    const int x = x_q8 * (int)surface->width / (320 * 256);
    p4_ui_sprite(surface,x,p4_ui_y(surface,y),p4_ui_x(surface,FRAME_WIDTH),
        p4_ui_y(surface,FRAME_HEIGHT),hi_art[frame & 15U],HI_ART_W,HI_ART_H,true,0U);
}

static void draw_traffic(p4_game_surface_t *surface,
                         const frog_hop_state_t *state)
{
    for (unsigned lane = 0U; lane < ROAD_ROW_COUNT; ++lane) {
        const int top = row_top(ROAD_FIRST_ROW + lane) - 5;
        const int spacing = s_road_lanes[lane].spacing;
        for (int item = 0; item < 5; ++item) {
            const int x = frog_hop_lane_visual_q8(state,false,lane) + item * spacing * 256;
            draw_lane_frame(surface, x, top, CAR_ROW * 4U +
                       ((lane + (unsigned)item + state->road_variants[lane]) & 3U));
        }
    }
}

static void draw_logs(p4_game_surface_t *surface,
                      const frog_hop_state_t *state)
{
    for (unsigned lane = 0U; lane < WATER_ROW_COUNT; ++lane) {
        const int top = row_top(WATER_FIRST_ROW + lane) - 5;
        const int spacing = s_log_lanes[lane].spacing;
        for (int item = 0; item < 5; ++item) {
            const int x = frog_hop_lane_visual_q8(state,true,lane) + item * spacing * 256;
            draw_lane_frame(surface, x, top, LOG_ROW * 4U +
                       ((lane + (unsigned)item + state->log_variants[lane]) & 3U));
        }
    }
}

static void hud_text(p4_game_surface_t *surface,int x,int y,
                     const char *text,uint16_t color)
{
    if(surface->width>320U)
        p4_ui_text(surface,p4_ui_x(surface,x),p4_ui_y(surface,y),text,color,18U,16U);
    else p4_draw_text(surface,x,y,text,color,1U,16U);
}

static void draw_hud(p4_game_surface_t *surface,
                     const frog_hop_state_t *state)
{
    hi_fill_rect(surface,0,0,P4_GAME_SURFACE_WIDTH,HUD_HEIGHT,COLOR_NAVY);
    p4_ui_text(surface,p4_ui_x(surface,58),p4_ui_y(surface,0),"FROG HOP",
        UINT16_C(0xd775),surface->width>320U?22U:9U,8U);
    char text[11];
    u32_text(state->level,text);
    hud_text(surface,193,1,"ROUND",COLOR_TEXT);
    hud_text(surface,231,1,text,COLOR_WHITE);
    hud_text(surface,58,10,"SCORE",COLOR_TEXT);
    u32_text(state->score,text);hud_text(surface,89,10,text,COLOR_WHITE);
    hud_text(surface,155,10,"LIVES",COLOR_TEXT);
    u32_text(state->lives,text);hud_text(surface,191,10,text,COLOR_WHITE);
    hud_text(surface,208,10,"HOME",COLOR_TEXT);
    unsigned homes=0U;
    for(unsigned i=0U;i<5U;++i)homes+=(state->homes>>i)&1U;
    char filled[4]={(char)('0'+homes),'/', '5','\0'};
    hud_text(surface,242,10,filled,COLOR_WHITE);
}

static void draw_scene_panel(p4_game_surface_t *surface,int y,int height)
{
    p4_ui_round_rect(surface,p4_ui_x(surface,65),p4_ui_y(surface,y+3),
        p4_ui_x(surface,194),p4_ui_y(surface,height),p4_ui_x(surface,7),UINT16_C(0x0022));
    p4_ui_round_rect(surface,p4_ui_x(surface,63),p4_ui_y(surface,y),
        p4_ui_x(surface,194),p4_ui_y(surface,height),p4_ui_x(surface,7),UINT16_C(0x7c90));
    p4_ui_round_rect(surface,p4_ui_x(surface,64),p4_ui_y(surface,y+1),
        p4_ui_x(surface,192),p4_ui_y(surface,height-2),p4_ui_x(surface,6),COLOR_NAVY);
    hi_fill_rect(surface,82,y+3,156,1,UINT16_C(0x42ad));
}

static void draw_title(p4_game_surface_t *surface,
                       const frog_hop_state_t *state)
{
    draw_course(surface,state);
    draw_logs(surface,state);
    draw_traffic(surface,state);
    scene_shade(surface,0,200,COLOR_NAVY,8U);
    draw_scene_panel(surface,26,129);
    centered_text(surface,34,"R I V E R S I D E   R U N",COLOR_TEXT,19U);
    centered_text(surface,46,"FROG HOP",UINT16_C(0x0001),54U);
    centered_text(surface,45,"FROG HOP",UINT16_C(0xf798),54U);
    /* Use the original measured source crop for the larger title character. */
    hi_frame(surface,103,85,114,43,8U);
    p4_ui_sprite(surface,p4_ui_x(surface,132),p4_ui_y(surface,76),
        p4_ui_x(surface,56),p4_ui_y(surface,46),title_frog,128,104,true,0U);
    centered_text(surface,124,"FIVE HOMES. ONE BRAVE FROG.",COLOR_TEXT,19U);
    centered_text(surface,139,"A OR START TO PLAY",UINT16_C(0xd775),23U);
    p4_game_draw_standard_controls(surface,COLOR_TEXT,UINT16_C(0x6e4f),state->held_buttons);
}

static void advance_lanes(frog_hop_state_t *state)
{
    const int multiplier = (int)state->level;
    for (unsigned lane = 0U; lane < ROAD_ROW_COUNT; ++lane) {
        const int speed = (int)s_road_lanes[lane].speed * multiplier;
        state->road_offsets[lane] = (int16_t)wrap_offset(
            (int)state->road_offsets[lane] + speed,
            (int)s_road_lanes[lane].spacing, &state->road_variants[lane]);
    }
    for (unsigned lane = 0U; lane < WATER_ROW_COUNT; ++lane) {
        const int speed = (int)s_log_lanes[lane].speed * multiplier;
        state->log_offsets[lane] = (int16_t)wrap_offset(
            (int)state->log_offsets[lane] + speed,
            (int)s_log_lanes[lane].spacing, &state->log_variants[lane]);
    }
}

static bool frog_on_log(const frog_hop_state_t *state, int *out_speed)
{
    if (state->frog_row < WATER_FIRST_ROW ||
        state->frog_row >= WATER_FIRST_ROW + WATER_ROW_COUNT ||
        out_speed == NULL) {
        return false;
    }
    const unsigned lane = state->frog_row - WATER_FIRST_ROW;
    const int spacing = s_log_lanes[lane].spacing;
    for (int item = 0; item < 5; ++item) {
        const int x = state->log_offsets[lane] + item * spacing;
        if (state->frog_x >= x + 5 &&
            state->frog_x <= x + FRAME_WIDTH - 5) {
            *out_speed = (int)s_log_lanes[lane].speed * (int)state->level;
            return true;
        }
    }
    return false;
}

static bool frog_hit_car(const frog_hop_state_t *state)
{
    if (state->frog_row < ROAD_FIRST_ROW ||
        state->frog_row >= ROAD_FIRST_ROW + ROAD_ROW_COUNT) {
        return false;
    }
    const unsigned lane = state->frog_row - ROAD_FIRST_ROW;
    const int spacing = s_road_lanes[lane].spacing;
    for (int item = 0; item < 5; ++item) {
        const int x = state->road_offsets[lane] + item * spacing;
        if (state->frog_x >= x + 5 &&
            state->frog_x <= x + FRAME_WIDTH - 5) {
            return true;
        }
    }
    return false;
}

static void lose_life(p4_game_context_t *context, frog_hop_state_t *state)
{
    if (state->respawn_ms != 0U || state->game_over) {
        return;
    }
    state->splash_x = state->frog_x;
    state->splash_row = state->frog_row;
    state->splash_ms = 450U;
    if (state->lives != 0U) {
        --state->lives;
    }
    reset_frog(state);
    state->respawn_ms = 550U;
    play_tone(context, 110U, 220U, 5U, P4_WAVE_SQUARE);
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
    if (state->lives == 0U) {
        state->game_over = true;
    }
}

static void reach_home(p4_game_context_t *context, frog_hop_state_t *state)
{
    for (unsigned home = 0U; home < GOAL_COUNT; ++home) {
        int distance = state->frog_x - s_goal_x[home];
        if (distance < 0) {
            distance = -distance;
        }
        if (distance > 15) {
            continue;
        }
        const uint8_t home_bit = (uint8_t)(UINT8_C(1) << home);
        if ((state->homes & home_bit) != 0U) {
            lose_life(context, state);
            return;
        }
        state->homes = (uint8_t)(state->homes | home_bit);
        state->score += 100U + (uint32_t)state->level * 20U;
        play_tone(context, 880U, 90U, 4U, P4_WAVE_TRIANGLE);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
        reset_frog(state);
        state->respawn_ms = 350U;
        if (state->homes == HOME_MASK) {
            state->homes = 0U;
            ++state->level;
            state->score += 500U;
            play_tone(context, 1175U, 160U, 5U, P4_WAVE_TRIANGLE);
        }
        return;
    }
    lose_life(context, state);
}

static void update_frog_position(p4_game_context_t *context,
                                 frog_hop_state_t *state)
{
    if (state->frog_row == 0U) {
        reach_home(context, state);
        return;
    }
    int log_speed = 0;
    if (state->frog_row >= WATER_FIRST_ROW &&
        state->frog_row < WATER_FIRST_ROW + WATER_ROW_COUNT) {
        if (!frog_on_log(state, &log_speed)) {
            lose_life(context, state);
            return;
        }
        state->frog_x = (int16_t)((int)state->frog_x + log_speed);
        if (state->frog_x < -8 || state->frog_x > 328) {
            lose_life(context, state);
        }
        return;
    }
    if (frog_hit_car(state)) {
        lose_life(context, state);
    }
}

/* A short visual hop follows the already resolved destination. A collision
 * resets both endpoints immediately, so dead frogs never drift across traffic. */
int32_t frog_hop_visual_q8(const frog_hop_state_t *state,bool vertical)
{
    int32_t target=vertical?(int32_t)(row_top(state->frog_row)+7)*256:
        (int32_t)state->frog_x*256;
    if (!vertical && state->world_advanced && state->respawn_ms == 0U &&
        state->frog_row >= WATER_FIRST_ROW && state->frog_row < WATER_FIRST_ROW + WATER_ROW_COUNT) {
        const unsigned lane = state->frog_row - WATER_FIRST_ROW;
        target += frog_hop_lane_visual_q8(state,true,lane) - state->log_offsets[lane] * 256;
    }
    if(state->hop_ms==0U)return target;
    const int32_t from=vertical?state->visual_from_y_q8:state->visual_from_x_q8;
    return from+(target-from)*(80-(int32_t)state->hop_ms)/80;
}
static void draw_player_frog(p4_game_surface_t *surface,const frog_hop_state_t *state)
{
    const int32_t progress=80-(int32_t)state->hop_ms;
    const int32_t arc=state->hop_ms==0U?0:12*progress*(80-progress)*256/(80*80);
    const int x=(int)(frog_hop_visual_q8(state,false)*(int32_t)surface->width/(320*256));
    const int y=(int)((frog_hop_visual_q8(state,true)-arc)*(int32_t)surface->height/(200*256));
    const unsigned frame=state->hop_ms==0U?0U:(unsigned)progress/20U;
    p4_ui_sprite(surface,x-p4_ui_x(surface,20),y-p4_ui_y(surface,12),
        p4_ui_x(surface,FRAME_WIDTH),p4_ui_y(surface,FRAME_HEIGHT),
        hi_art[frame&3U],HI_ART_W,HI_ART_H,true,0U);
}

static void move_frog(p4_game_context_t *context, frog_hop_state_t *state,
                      uint32_t pressed)
{
    bool moved = false;
    const int32_t from_x=frog_hop_visual_q8(state,false);
    const int32_t from_y=frog_hop_visual_q8(state,true);
    if ((pressed & P4_BUTTON_UP) != 0U && state->frog_row != 0U) {
        --state->frog_row;
        state->score += 10U;
        moved = true;
    } else if ((pressed & P4_BUTTON_DOWN) != 0U &&
               state->frog_row < START_ROW) {
        ++state->frog_row;
        moved = true;
    } else if ((pressed & P4_BUTTON_LEFT) != 0U &&
               state->frog_x > FROG_MIN_X) {
        state->frog_x = (int16_t)(state->frog_x - GRID_STEP_X);
        moved = true;
    } else if ((pressed & P4_BUTTON_RIGHT) != 0U &&
               state->frog_x < FROG_MAX_X) {
        state->frog_x = (int16_t)(state->frog_x + GRID_STEP_X);
        moved = true;
    }
    if (moved) {
        state->visual_from_x_q8=from_x;
        state->visual_from_y_q8=from_y;
        state->hop_ms=80U;
        play_tone(context, 440U, 45U, 3U, P4_WAVE_TRIANGLE);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_ACTION);
    }
}

static void tick_game(p4_game_context_t *context, frog_hop_state_t *state)
{
    advance_lanes(state);
    state->world_advanced = true;
    state->splash_ms = tick_down(state->splash_ms, WORLD_STEP_MS);
    if (state->respawn_ms != 0U) {
        state->respawn_ms = tick_down(state->respawn_ms, WORLD_STEP_MS);
        return;
    }
    update_frog_position(context, state);
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(frog_hop_state_t)) {
        return false;
    }
    frog_hop_state_t *const state = context->state;
    reset_round(state);
    play_tone(context, 523U, 90U, 3U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    if (context == NULL || context->state == NULL || input == NULL) {
        return P4_GAME_ERROR;
    }
    frog_hop_state_t *const state = context->state;
    (void)p4_game_audio_effect_service(context, &state->audio);
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    if (!state->paused && !state->game_over) {
        state->scenery_ms = (uint16_t)((state->scenery_ms + elapsed_ms) % 30720U);
        state->animation_ms = (uint16_t)(state->scenery_ms % 96U);
        state->animation_frame = (uint8_t)((state->scenery_ms / 96U) & 3U);
    }
    if (state->intro) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            state->intro = false;
            play_tone(context, 660U, 90U, 4U, P4_WAVE_TRIANGLE);
        }
        return P4_GAME_CONTINUE;
    }
    if (state->game_over) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            reset_round(state);
            state->intro = false;
            play_tone(context, 523U, 90U, 4U, P4_WAVE_TRIANGLE);
        }
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_START) != 0U) {
        state->paused = !state->paused;
        play_tone(context, state->paused ? 330U : 660U, 70U, 3U,
                  P4_WAVE_SQUARE);
        return P4_GAME_CONTINUE;
    }
    if (state->paused) {
        return P4_GAME_CONTINUE;
    }
    state->hop_ms=tick_down(state->hop_ms,(uint16_t)elapsed_ms);
    /* Recovery locks movement, but the world clock must still expire it. */
    if (state->respawn_ms == 0U) {
        move_frog(context, state, input->pressed);
    }
    state->simulation_ms += elapsed_ms;
    while (state->simulation_ms >= WORLD_STEP_MS) {
        state->simulation_ms -= WORLD_STEP_MS;
        tick_game(context, state);
    }
    return P4_GAME_CONTINUE;
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (context == NULL || context->state == NULL || !p4_surface_valid(surface)) {
        return false;
    }
    const frog_hop_state_t *const state = context->state;
    if (state->intro) {
        draw_title(surface, state);
        return true;
    }
    draw_course(surface, state);
    draw_homes(surface, state);
    draw_logs(surface, state);
    draw_traffic(surface, state);
    if (state->splash_ms != 0U) {
        /* A loss is an expanding burst, never a reusable home-pad sprite. */
        const int radius=3+(450-(int)state->splash_ms)/55;
        const int center_y=row_top(state->splash_row)+7;
        const bool water=state->splash_row>=WATER_FIRST_ROW &&
            state->splash_row<WATER_FIRST_ROW+WATER_ROW_COUNT;
        const uint16_t color=water?COLOR_WATER_LIGHT:UINT16_C(0xff80);
        static const int8_t offsets[8][2]={
            {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
        for(unsigned i=0;i<8U;++i) {
            hi_fill_rect(surface,state->splash_x+(int)offsets[i][0]*radius,
                center_y+(int)offsets[i][1]*radius,2,2,color);
        }
        if(state->splash_ms>300U)
            hi_circle(surface,state->splash_x,center_y,2,COLOR_WHITE);
    }
    if (state->respawn_ms == 0U ||
        ((state->respawn_ms / 100U) & 1U) == 0U) {
        hi_feedback(
            surface, &state->audio,
            state->game_over ? 160 : state->frog_x,
            state->game_over ? 92 : row_top(state->frog_row) + 7);
        draw_player_frog(surface,state);
    }
    draw_hud(surface, state);
    if (state->paused || state->game_over) {
        scene_shade(surface,HUD_HEIGHT,200-HUD_HEIGHT,COLOR_NAVY,7U);
        draw_scene_panel(surface,67,66);
        centered_text(surface,79,state->paused?"TAKE A BREATHER":"THE RIVER CALLS AGAIN",COLOR_TEXT,18U);
        centered_text(surface,91,state->paused?"PAUSED":"FROG DOWN",COLOR_WHITE,36U);
        centered_text(surface,115,state->paused?"START TO RESUME":"A TO TRY AGAIN",UINT16_C(0xd775),21U);
    }
    p4_game_draw_standard_controls(surface, COLOR_TEXT, UINT16_C(0x6e4f),
                                   state->held_buttons);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_frog_hop_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(105),
    .id = "org.p4console.frog-hop",
    .title = "Frog Hop",
    .subtitle = "Cross roads and rivers",
    .accent_rgb565 = UINT16_C(0x6e4f),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
        P4_GAME_CAP_VIDEO_HIGH_RES,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM,
    .state_bytes = sizeof(frog_hop_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
