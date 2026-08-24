// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef P4_DOOM_NET_H
#define P4_DOOM_NET_H

#include "d_ticcmd.h"
#include "doomtype.h"
#include "net_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Narrow platform networking seam for the otherwise transport-free embedded
 * Doom engine. The engine component supplies weak single-player stubs; Console
 * OS supplies the strong P4MP implementation at link time.
 */
boolean P4_DoomNetActive(void);
/* Returns only after both peers have delivered identical startup tics. */
boolean P4_DoomNetConfigure(net_gamesettings_t *settings);
void P4_DoomNetSubmitTic(const ticcmd_t *command, int tic);
void P4_DoomNetPoll(void);
boolean P4_DoomNetFailed(void);
void P4_DoomNetQuit(void);

#ifdef __cplusplus
}
#endif

#endif
