// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef P4_DOOM_ARENA_UI_H
#define P4_DOOM_ARENA_UI_H

#include <stdbool.h>

/* Logical 320x200 Doom coordinates, clear of the existing touch controls. */
#define P4_DOOM_SCORE_LEFT 80U
#define P4_DOOM_SCORE_TOP 9U
#define P4_DOOM_SCORE_WIDTH 62U
#define P4_DOOM_SCORE_HEIGHT 28U

/* A complete touch snapshot; toggles once per press, never changes a ticcmd. */
void p4_doom_arena_score_touch(bool pressed);

#endif
