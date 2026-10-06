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

__attribute__((weak)) boolean P4_DoomNetFailed(void)
{
    return false;
}

__attribute__((weak)) void P4_DoomNetQuit(void)
{
}

__attribute__((weak)) boolean P4_DoomArenaActive(void) { return false; }
__attribute__((weak)) boolean P4_DoomArenaPlayerActive(int slot) { (void)slot; return true; }
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
