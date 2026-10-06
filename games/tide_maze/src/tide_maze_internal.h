// SPDX-License-Identifier: MIT
#ifndef TIDE_MAZE_INTERNAL_H
#define TIDE_MAZE_INTERNAL_H
#include "p4/game.h"
#include "p4/scene3d.h"
#include "p4/shallow_water.h"
enum { TM_COLS=15, TM_ROWS=9, TM_W=60, TM_H=36, TM_CELLS=2160, TM_VERTICES=2257, TM_BAND=12,
    TM_Q=256, TM_STEP=20, TM_LEVELS=3, TM_SNAPSHOT=64, TM_INPUT=20 };
typedef enum { TM_TITLE, TM_PLAY, TM_PAUSE, TM_CLEAR, TM_WON, TM_LOST, TM_LINK_LOST, TM_WAIT } tm_phase;
typedef struct { int32_t x,y,vx,vy; uint16_t rescue; float z,vz,previous_z; } tm_ball;
typedef struct { int16_t x,y,spin,jolt; bool brake; } tm_intent;
typedef struct {
    tm_phase phase;
    unsigned level, pearls, all_pearls, docked, rescues;
    uint32_t time_ms, accumulator, animation_ms, tone_ms;
    tm_ball ball[2];
    tm_intent intent[2];
    float water[TM_CELLS], flow_x[TM_CELLS], flow_y[TM_CELLS];
    float previous_water[TM_CELLS], body[TM_CELLS], work_x[TM_CELLS], work_y[TM_CELLS];
    uint8_t wet[TM_CELLS];
    bool linked, host, motion_live, calibrated, touching;
    uint8_t slot, orientation;
    uint32_t motion_seq;
    int32_t neutral[3], basis_x[3], basis_y[3], filtered_x, filtered_y; /* filter is Q4 */
    uint32_t generation, revision, received_revision, received_seq, send_seq;
    uint64_t seed;
    uint32_t net_ms, peer_ms, blend_ms;
    int32_t previous_x[2], previous_y[2];
    bool snapshot_seen;
    float view_x,view_y,previous_view_x,previous_view_y;
    /* Render scratch is per-instance and never authoritative/network state. */
    p4_3d_vertex_t water_vertices[TM_VERTICES];
    p4_3d_vertex_t sphere_vertices[2][153];
    uint16_t depth_band[768*TM_BAND];
    uint8_t material_band[768*TM_BAND];
} tm_state;
extern const p4_game_descriptor_t p4_tide_maze_game;
extern const char *const tm_maps[TM_LEVELS][TM_ROWS];
int tm_clamp(int value,int lo,int hi);
char tm_tile(unsigned level,int x,int y);
void tm_reset(tm_state *s,unsigned level);
void tm_fluid(tm_state *s);
void tm_water_reset(tm_state *s);
float tm_water_height(const tm_state *s,float x,float y);
void tm_camera(const tm_state *s,float matrix[9]);
p4_3d_vertex_t tm_project(const tm_state *s,int width,int height,float x,float y,float z,uint16_t color);
void tm_simulate(tm_state *s);
void tm_controls(p4_game_context_t *ctx, tm_state *s,const p4_game_input_t *in, uint32_t ms, bool calibrate);
bool tm_network_begin(p4_game_context_t *ctx,tm_state *s);
bool tm_network_poll(p4_game_context_t *ctx,tm_state *s,uint32_t ms);
void tm_network_publish(p4_game_context_t *ctx,tm_state *s);
void tm_visual_ball(const tm_state *s,unsigned player,int32_t *x,int32_t *y);
bool tm_screen_to_board(const tm_state *s,int sx,int sy,int *x,int *y);
bool tm_render(p4_game_context_t *ctx,p4_game_surface_t *surface);
_Static_assert(sizeof(tm_state)<=P4_GAME_MAX_STATE_BYTES,"Tide state budget");
#endif
