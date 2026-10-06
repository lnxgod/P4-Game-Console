// SPDX-License-Identifier: MIT
#ifndef TIDE_MAZE_INTERNAL_H
#define TIDE_MAZE_INTERNAL_H
#include "p4/game.h"
enum { TM_COLS=15, TM_ROWS=9, TM_W=30, TM_H=18, TM_CELLS=540,
    TM_Q=256, TM_STEP=20, TM_LEVELS=3, TM_SNAPSHOT=64, TM_INPUT=20 };
typedef enum { TM_TITLE, TM_PLAY, TM_PAUSE, TM_CLEAR, TM_WON, TM_LOST, TM_LINK_LOST, TM_WAIT } tm_phase;
typedef struct { int32_t x,y,vx,vy; uint16_t rescue; } tm_ball;
typedef struct { int16_t x,y,spin,jolt; bool brake; } tm_intent;
typedef struct {
    tm_phase phase;
    unsigned level, pearls, all_pearls, docked, rescues;
    uint32_t time_ms, accumulator, animation_ms, tone_ms;
    tm_ball ball[2];
    tm_intent intent[2];
    int16_t water[TM_CELLS], flow_x[TM_CELLS], flow_y[TM_CELLS];
    uint8_t wet[TM_CELLS];
    bool linked, host, motion_live, calibrated, touching;
    uint8_t slot, orientation;
    uint32_t motion_seq;
    int32_t neutral[3], basis_x[3], basis_y[3], filtered_x, filtered_y;
    uint32_t generation, revision, received_revision, received_seq, send_seq;
    uint64_t seed;
    uint32_t net_ms, peer_ms, blend_ms;
    int32_t previous_x[2], previous_y[2];
    bool snapshot_seen;
} tm_state;
extern const p4_game_descriptor_t p4_tide_maze_game;
extern const char *const tm_maps[TM_LEVELS][TM_ROWS];
int tm_clamp(int value,int lo,int hi);
char tm_tile(unsigned level,int x,int y);
void tm_reset(tm_state *s,unsigned level);
void tm_fluid(tm_state *s);
void tm_simulate(tm_state *s);
void tm_controls(p4_game_context_t *ctx, tm_state *s,const p4_game_input_t *in, uint32_t ms, bool calibrate);
bool tm_network_begin(p4_game_context_t *ctx,tm_state *s);
bool tm_network_poll(p4_game_context_t *ctx,tm_state *s,uint32_t ms);
void tm_network_publish(p4_game_context_t *ctx,tm_state *s);
bool tm_render(p4_game_context_t *ctx,p4_game_surface_t *surface);
#endif
