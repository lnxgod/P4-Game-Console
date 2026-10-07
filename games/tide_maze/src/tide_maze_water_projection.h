// SPDX-License-Identifier: MIT
#ifndef TIDE_MAZE_WATER_PROJECTION_H
#define TIDE_MAZE_WATER_PROJECTION_H
#include "tide_maze_internal.h"
#include "p4/mesh.h"

/* Only world height changes for the fixed water grid. Preserve the original
 * two integer truncations instead of rounding a completed screen coordinate. */
typedef struct {
 int32_t y_numerator;
 uint16_t depth, native_x, legacy_x;
} tm_water_projection_vertex;
#include "generated/water_projection.inc"

static inline p4_mesh_point_t tm_water_project(const tm_water_projection_vertex *v,int height,bool native){
 const int sy=42*TM_Q+(v->y_numerator-height*600)/(int)v->depth;
 /* Cancel common factors exactly before multiplication; no float/rounding. */
 return (p4_mesh_point_t){native?(int)v->native_x:(int)v->legacy_x,native?sy*3/320:sy/TM_Q};
}
#endif
