// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef P4_QUAKE_EMBEDDED_COMPAT_H
#define P4_QUAKE_EMBEDDED_COMPAT_H

#include <stdio.h>

/*
 * Doom and Quake predate namespaced C APIs and export a handful of identical
 * globals. Console OS keeps both engines in one ELF, so namespace the
 * overlapping Quake symbols at compile time. This header is force-included
 * only for the pinned vendor files; the p4_quake_* adapter remains stable.
 */
#define M_Init p4_quake_engine_M_Init
#define R_Init p4_quake_engine_R_Init
#define R_InitTextures p4_quake_engine_R_InitTextures
#define R_SetupFrame p4_quake_engine_R_SetupFrame
#define S_Init p4_quake_engine_S_Init
#define S_Shutdown p4_quake_engine_S_Shutdown
#define S_StartSound p4_quake_engine_S_StartSound
#define S_StopSound p4_quake_engine_S_StopSound
#define V_Init p4_quake_engine_V_Init
#define WritePCXfile p4_quake_engine_WritePCXfile
#define Z_CheckHeap p4_quake_engine_Z_CheckHeap
#define Z_ClearZone p4_quake_engine_Z_ClearZone
#define Z_Free p4_quake_engine_Z_Free
#define Z_Malloc p4_quake_engine_Z_Malloc
#define deathmatch p4_quake_engine_deathmatch
#define gammatable p4_quake_engine_gammatable
#define mainzone p4_quake_engine_mainzone
#define nomonsters p4_quake_engine_nomonsters
#define onground p4_quake_engine_onground
#define precache p4_quake_engine_precache
#define startepisode p4_quake_engine_startepisode
#define timelimit p4_quake_engine_timelimit

/* Force every vendor fopen through the read-only, root-bounded adapter. */
FILE *p4_quake_fopen(const char *path, const char *mode);
#define fopen p4_quake_fopen

#endif
