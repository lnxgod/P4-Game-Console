/* SPDX-License-Identifier: MIT */
#ifndef WACKY_FRONT_H
#define WACKY_FRONT_H
#include "p4/game.h"
#include "ww_archive.h"
#include "ww_pcx.h"
#include "ww_display.h"
enum { WW_HOME,WW_SETUP,WW_OPTIONS,WW_HELP,WW_RESULTS,WW_PAUSE,WW_INFO,WW_SLIDES };
enum { WW_SINGLE,WW_CUP,WW_DUCK };
enum { WW_FRONT_NONE,WW_FRONT_LAUNCH,WW_FRONT_EXIT,WW_FRONT_RESUME,WW_FRONT_RESTART,WW_FRONT_HOME,WW_FRONT_INTRO };
typedef struct {
 WwPcxImage background;
 uint8_t screen,row,mode,vehicle,track,laps,engine,race_class;
 uint8_t music,effects,motor,duck_best;
 bool map,clock,speedometer,touch_down;
 uint8_t result_place,result_score;
 uint16_t cup_points;
 uint32_t result_time,best_time[5];
 uint8_t slide,slide_group,loaded_slide,loaded_group;
} WwFront;
bool ww_front_open(WwFront *,const WwArchive *);
void ww_front_close(WwFront *);
unsigned ww_front_input(WwFront *,const p4_game_input_t *);
bool ww_front_draw(WwFront *,WwDisplay *,const WwArchive *);
void ww_front_overlay(const WwFront *,p4_game_surface_t *);
#endif
