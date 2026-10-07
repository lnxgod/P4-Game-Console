// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef P4_DOOM_NET_H
#define P4_DOOM_NET_H

#include <stdint.h>
#include <stddef.h>

/* Optional application-owned admission immediately before a zone malloc.
 * The default is a no-op; the hook must not change the requested zone size. */
void P4_DoomBeforeZoneAllocation(size_t requested_bytes);
void P4_DoomAfterZoneAllocation(size_t requested_bytes, int allocated);
#include "d_ticcmd.h"
#include "doomtype.h"
#include "net_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional Tab5 engine timings. Record only owner-task scalar counters;
 * callbacks never call the engine, networking, storage or presentation.
 * TICS/DISPLAY/NET are inclusive. SIM and render subphases are nested;
 * PALETTE excludes DG_DrawFrame. These totals must not be added together. */
typedef enum {
    P4_DOOM_ENGINE_TICS, P4_DOOM_ENGINE_SIM, P4_DOOM_ENGINE_SOUND,
    P4_DOOM_ENGINE_DISPLAY, P4_DOOM_ENGINE_SETUP, P4_DOOM_ENGINE_BSP,
    P4_DOOM_ENGINE_PLANES, P4_DOOM_ENGINE_MASKED, P4_DOOM_ENGINE_PALETTE,
    P4_DOOM_ENGINE_NET, P4_DOOM_ENGINE_PHASE_COUNT
} p4_doom_engine_perf_phase_t;
uint64_t P4_DoomEnginePerfNow(void);
void P4_DoomEnginePerfRecord(p4_doom_engine_perf_phase_t phase,
                           uint64_t elapsed_us);
#ifdef P4_DOOM_ENGINE_PERF
#define P4_ENGINE_PERF_BEGIN(name) const uint64_t name = P4_DoomEnginePerfNow()
#define P4_ENGINE_PERF_END(phase, name) \
    do { if ((name) != 0) \
        P4_DoomEnginePerfRecord((phase), P4_DoomEnginePerfNow() - (name)); \
    } while (0)
#else
#define P4_ENGINE_PERF_BEGIN(name) ((void)0)
#define P4_ENGINE_PERF_END(phase, name) ((void)0)
#endif

/*
 * Narrow platform networking seam for the otherwise transport-free embedded
 * Doom engine. The engine component supplies weak single-player stubs; Console
 * OS supplies the strong P4MP implementation at link time.
 */
boolean P4_DoomNetActive(void);
/* Initial starts queue agreed startup tics; cold rejoins return into replay. */
boolean P4_DoomNetConfigure(net_gamesettings_t *settings);
/* Immutable initial roster, available after Configure and before map loading.
 * Zero preserves the legacy all-configured-players behavior. Reserved Arena
 * seats stay absent until an authoritative canonical activation. */
unsigned int P4_DoomNetInitialPlayerMask(void);
void P4_DoomNetSubmitTic(const ticcmd_t *command, int tic);
void P4_DoomNetPoll(void);
/* Explicit short render-tail hint. All simulation checks and long-work
 * boundaries retain forced Poll; single-player/legacy implementations do too. */
void P4_DoomNetPollOpportunistic(void);
/* Only D_Display's ordinary (non-wipe) I_FinishUpdate scope enables this tail. */
void P4_DoomNetSetFrameTail(boolean ordinary);
void P4_DoomNetPollFrameTail(boolean allow);
/* Cumulative, saturating owner-task diagnostics, reset by adapter prepare.
 * Poll timing includes every public call, including inactive/early returns.
 * TX counts adapter-send attempts and encode/transport failures. RX counts
 * frame callbacks; rejected counts route/session-envelope rejection only,
 * not every game-payload refusal. Blocked-peer polls is Arena-host-only: one
 * per peer per ordinary runtime poll when ACK or required input is missing;
 * it is a sampling count, not a duration or packet-loss measurement.
 * Departures count the first bound-route departure per peer lifecycle.
 * Poll maximum is cumulative since prepare, not a reporting-window maximum. */
typedef struct {
    uint32_t poll_calls;
    uint64_t poll_us;
    uint64_t poll_max_us;
    uint32_t tx_attempts, tx_failures;
    uint32_t rx_packets, rx_session_rejected;
    uint32_t peer_departures, host_blocked_peer_polls;
    /* RX-only timings include packet callbacks. Busy means no proven drained
     * receipt, including unknown transports. Gap is cumulative live start-to-
     * start spacing; maxima are never reset at a reporting boundary. */
    uint32_t rx_poll_calls, rx_poll_skips, rx_busy_polls;
    uint64_t rx_poll_us, rx_poll_max_us, rx_gap_max_us;
} p4_doom_net_stats_t;
/* Read on the network owner task; no synchronization, clearing or logging. */
void P4_DoomNetGetStats(p4_doom_net_stats_t *stats);
/* Startup-only owner-task snapshots. These hooks never perform I/O, allocate,
 * log, redraw, or call back into the engine. A zero total is indeterminate;
 * completed/total describes this phase, never an estimated whole-launch ETA. */
typedef enum {
    P4_DOOM_LOADING_IDLE, P4_DOOM_LOADING_WAITING_HOST,
    P4_DOOM_LOADING_CHECKPOINT, P4_DOOM_LOADING_CATCHUP,
    P4_DOOM_LOADING_READY, P4_DOOM_LOADING_FAILED
} p4_doom_loading_phase_t;
typedef struct {
    p4_doom_loading_phase_t phase;
    uint32_t completed;
    uint32_t total;
} p4_doom_loading_progress_t;
/* CHECKPOINT counts received bytes; CATCHUP counts actually consumed tics
 * relative to one fixed replay baseline, with target minus that baseline. */
void P4_DoomNetGetLoadingProgress(p4_doom_loading_progress_t *progress);
typedef enum {
    P4_DOOM_ENGINE_LOADING_PREPARING, P4_DOOM_ENGINE_LOADING_TEXTURES,
    P4_DOOM_ENGINE_LOADING_SPRITES, P4_DOOM_ENGINE_LOADING_MAP,
    P4_DOOM_ENGINE_LOADING_READY
} p4_doom_engine_loading_phase_t;
void P4_DoomLoadingProgress(p4_doom_engine_loading_phase_t phase,
                          uint32_t completed, uint32_t total);
boolean P4_DoomNetFailed(void);
void P4_DoomNetQuit(void);
/* Cold guests replay authoritative history without presenting stale frames. */
/* Called only by the Doom owner at a safe between-tic boundary, including
 * empty replay queues. True means yield immediately: restore may rebase clocks
 * and invalidate any selected ticdata. Never call from render/VFS callbacks. */
boolean P4_DoomNetCheckpointBoundary(void);

boolean P4_DoomNetReplaying(void);

/* Optional arena rules; neutral stubs preserve ordinary Doom and Chex. */
boolean P4_DoomArenaActive(void);
boolean P4_DoomArenaPlayerActive(int slot);
/* Called only for an authoritative absent-to-present canonical mask edge. */
void P4_DoomArenaRejoin(int slot);
void P4_DoomArenaTicker(void);
void P4_DoomArenaKill(int killer, int victim);
boolean P4_DoomArenaCompleted(void);
void P4_DoomArenaHUD(void);
int P4_DoomArenaFragCount(int vanilla);
void P4_DoomArenaWadLoaded(const char *filename, int first, int count);
int P4_DoomArenaMapLump(int vanilla);
int P4_DoomArenaMusicLump(int vanilla);
void P4_DoomArenaBuildCommand(ticcmd_t *command);
boolean P4_DoomArenaMenuKey(int key);
boolean P4_DoomArenaMenuDraw(void);

#ifdef __cplusplus
}
#endif

#endif
