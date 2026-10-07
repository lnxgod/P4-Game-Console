// SPDX-License-Identifier: GPL-2.0-or-later

#include "p4_doom_net.h"

__attribute__((weak)) boolean P4_DoomNetActive(void)
{
    return false;
}

__attribute__((weak)) boolean P4_DoomNetConfigure(
    net_gamesettings_t *settings)
{
    (void)settings;
    return false;
}

__attribute__((weak)) void P4_DoomNetSubmitTic(
    const ticcmd_t *command,
    int tic)
{
    (void)command;
    (void)tic;
}

__attribute__((weak)) void P4_DoomNetPoll(void)
{
}

__attribute__((weak)) void P4_DoomNetPollOpportunistic(void)
{ P4_DoomNetPoll(); }
__attribute__((weak)) void P4_DoomNetSetFrameTail(boolean ordinary)
{ (void)ordinary; }
__attribute__((weak)) void P4_DoomNetPollFrameTail(boolean allow)
{ (void)allow;P4_DoomNetPoll(); }

__attribute__((weak)) void P4_DoomNetGetStats(p4_doom_net_stats_t *stats)
{
    if (stats) *stats = (p4_doom_net_stats_t){0};
}

__attribute__((weak)) void P4_DoomNetGetLoadingProgress(
    p4_doom_loading_progress_t *progress)
{
    if (progress) *progress = (p4_doom_loading_progress_t){0};
}

__attribute__((weak)) void P4_DoomLoadingProgress(
    p4_doom_engine_loading_phase_t phase, uint32_t completed, uint32_t total)
{
    (void)phase; (void)completed; (void)total;
}

__attribute__((weak)) boolean P4_DoomNetFailed(void)
{
    return false;
}

__attribute__((weak)) void P4_DoomNetQuit(void)
{
}

__attribute__((weak)) boolean P4_DoomNetReplaying(void) { return false; }
__attribute__((weak)) unsigned int P4_DoomNetInitialPlayerMask(void) { return 0; }

__attribute__((weak)) boolean P4_DoomArenaActive(void) { return false; }
__attribute__((weak)) boolean P4_DoomArenaPlayerActive(int slot) { (void)slot; return true; }
__attribute__((weak)) void P4_DoomArenaRejoin(int slot) { (void)slot; }
__attribute__((weak)) void P4_DoomArenaTicker(void) {}
__attribute__((weak)) void P4_DoomArenaKill(int killer, int victim) { (void)killer; (void)victim; }
__attribute__((weak)) boolean P4_DoomArenaCompleted(void) { return false; }
__attribute__((weak)) void P4_DoomArenaHUD(void) {}

__attribute__((weak)) int P4_DoomArenaFragCount(int vanilla) { return vanilla; }

__attribute__((weak)) void P4_DoomArenaWadLoaded(const char *file,int first,int count)
{ (void)file; (void)first; (void)count; }
__attribute__((weak)) int P4_DoomArenaMapLump(int vanilla) { return vanilla; }
__attribute__((weak)) int P4_DoomArenaMusicLump(int vanilla) { return vanilla; }
__attribute__((weak)) void P4_DoomArenaBuildCommand(ticcmd_t *command) { (void)command; }
__attribute__((weak)) boolean P4_DoomArenaMenuKey(int key) { (void)key; return false; }
__attribute__((weak)) boolean P4_DoomArenaMenuDraw(void) { return false; }

__attribute__((weak)) uint64_t P4_DoomEnginePerfNow(void) { return 0; }
__attribute__((weak)) void P4_DoomEnginePerfRecord(
    p4_doom_engine_perf_phase_t phase, uint64_t elapsed_us)
{ (void)phase; (void)elapsed_us; }

/* The owner task overrides the boundary hook for checkpoint coordination. */
__attribute__((weak)) boolean P4_DoomNetCheckpointBoundary(void) { return false; }
#include "p4_doom_checkpoint.h"
__attribute__((weak)) void p4_doom_gc_checkpoint_get_arena(p4_doom_arena_t *out)
{ if (out) *out = (p4_doom_arena_t){0}; }
__attribute__((weak)) bool p4_doom_gc_checkpoint_set_arena(const p4_doom_arena_t *in)
{ (void)in; return false; }
