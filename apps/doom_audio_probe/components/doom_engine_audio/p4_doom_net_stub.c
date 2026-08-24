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
