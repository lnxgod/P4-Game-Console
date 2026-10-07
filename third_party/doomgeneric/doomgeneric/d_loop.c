//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//     Main loop code.
//

#include <stdlib.h>
#include <string.h>

#include "doomfeatures.h"

#include "d_event.h"
#include "d_loop.h"
#include "doomgeneric.h"
#include "d_ticcmd.h"

#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"

#include "m_argv.h"
#include "m_fixed.h"

#include "net_client.h"
#include "net_gui.h"
#include "net_io.h"
#include "net_query.h"
#include "net_server.h"
#include "net_sdl.h"
#include "net_loop.h"
#include "p4_doom_net.h"

// The complete set of data for a particular tic.

typedef struct
{
    ticcmd_t cmds[NET_MAXPLAYERS];
    boolean ingame[NET_MAXPLAYERS];
} ticcmd_set_t;

//
// gametic is the tic about to (or currently being) run
// maketic is the tic that hasn't had control made for it yet
// recvtic is the latest tic received from the server.
//
// a gametic cannot be run until ticcmds are received for it
// from all players.
//

static ticcmd_set_t ticdata[BACKUPTICS];

// The index of the next tic to be made (with a call to BuildTiccmd).

static int maketic;

// The number of complete tics received from the server so far.

static int recvtic;
static boolean p4_replay_input_ready;

// The number of tics that have been run (using RunTic) so far.

int gametic;

// When set to true, a single tic is run each time TryRunTics() is called.
// This is used for -timedemo mode.

boolean singletics = false;

// Index of the local player.

static int localplayer;

// Used for original sync code.

static int      skiptics = 0;

// Reduce the bandwidth needed by sampling game input less and transmitting
// less.  If ticdup is 2, sample half normal, 3 = one third normal, etc.

int		ticdup;

// Amount to offset the timer for game sync.

fixed_t         offsetms;

// Use new client syncronisation code

static boolean  new_sync = true;

// Callback functions for loop code.

static loop_interface_t *loop_interface = NULL;

// Current players in the multiplayer game.
// This is distinct from playeringame[] used by the game code, which may
// modify playeringame[] when playing back multiplayer demos.

static boolean local_playeringame[NET_MAXPLAYERS];

// Requested player class "sent" to the server on connect.
// If we are only doing a single player game then this needs to be remembered
// and saved in the game settings.

static int player_class;


// 35 fps clock adjusted by offsetms milliseconds

static int GetAdjustedTime(void)
{
    int time_ms;

    time_ms = I_GetTimeMS();

    if (new_sync)
    {
	// Use the adjustments from net_client.c only if we are
	// using the new sync mode.

        time_ms += (offsetms / FRACUNIT);
    }

    return (time_ms * TICRATE) / 1000;
}

static boolean BuildNewTic(void)
{
    int	gameticdiv;
    ticcmd_t cmd;

    gameticdiv = gametic/ticdup;

    I_StartTic ();
    loop_interface->ProcessEvents();

    // Always run the menu

    loop_interface->RunMenu();

    if (drone)
    {
        // In drone mode, do not generate any ticcmds.

        return false;
    }

    if (new_sync)
    {
       // If playing single player, do not allow tics to buffer
       // up very far

       if (!net_client_connected && maketic - gameticdiv > 2)
           return false;

       // Never go more than ~200ms ahead

       if (maketic - gameticdiv > 8)
           return false;
    }
    else
    {
       if (maketic - gameticdiv >= 5)
           return false;
    }

    //printf ("mk:%i ",maketic);
    memset(&cmd, 0, sizeof(ticcmd_t));
    loop_interface->BuildTiccmd(&cmd, maketic);

    // P4's host supplies every canonical command, including the local slot.
    // Provisional input must not overwrite queued history during catchup.
    if (!P4_DoomNetActive())
    {
        ticdata[maketic % BACKUPTICS].cmds[localplayer] = cmd;
        ticdata[maketic % BACKUPTICS].ingame[localplayer] = true;
    }

    if (P4_DoomNetActive())
    {
        P4_DoomNetSubmitTic(&cmd, maketic);
    }
#ifdef FEATURE_MULTIPLAYER
    else if (net_client_connected)
    {
        NET_CL_SendTiccmd(&cmd, maketic);
    }
#endif

    ++maketic;

    return true;
}

//
// NetUpdate
// Builds ticcmds for console player,
// sends out a packet
//
int      lasttime;

static void NetUpdateInternal (boolean opportunistic)
{
    int nowtime;
    int newtics;
    int	i;

    // If we are running with singletics (timing a demo), this
    // is all done separately.

    if (singletics)
        return;

    if (opportunistic) P4_DoomNetPollOpportunistic();
    else P4_DoomNetPoll();
    // Cold checkpoint failure requests the platform's clean Home shutdown.
    // Do not build input or take the generic fatal-disconnect path afterwards.
    if (doomgeneric_QuitRequested()) return;
    if (P4_DoomNetFailed())
    {
        I_Error("P4 multiplayer peer disconnected");
    }

#ifdef FEATURE_MULTIPLAYER

    // Run network subsystems

    NET_CL_Run();
    NET_SV_Run();

#endif

    // check time
    nowtime = GetAdjustedTime() / ticdup;
    newtics = nowtime - lasttime;

    lasttime = nowtime;

    // Rejoining guests consume only the host's canonical command journal.
    // An agreed activation tic can enable input before its actor is spawned,
    // while presentation remains suspended through that canonical edge.
    if (P4_DoomNetReplaying() && !p4_replay_input_ready)
        return;

    if (skiptics <= newtics)
    {
        newtics -= skiptics;
        skiptics = 0;
    }
    else
    {
        skiptics -= newtics;
        newtics = 0;
    }

    // build new ticcmds for console player

    for (i=0 ; i<newtics ; i++)
    {
        if (!BuildNewTic())
        {
            break;
        }
    }
}

void NetUpdate (void)
{
    P4_ENGINE_PERF_BEGIN(net_started);
    NetUpdateInternal(false);
    P4_ENGINE_PERF_END(P4_DOOM_ENGINE_NET, net_started);
}

void NetUpdateRenderTail (void)
{
    P4_ENGINE_PERF_BEGIN(net_started);
    NetUpdateInternal(true);
    P4_ENGINE_PERF_END(P4_DOOM_ENGINE_NET, net_started);
}

static void D_Disconnected(void)
{
    // In drone mode, the game cannot continue once disconnected.

    if (drone)
    {
        I_Error("Disconnected from server in drone mode.");
    }

    // disconnected from server

    printf("Disconnected from server.\n");
}

//
// Invoked by the network engine when a complete set of ticcmds is
// available.
//

void D_ReceiveTic(ticcmd_t *ticcmds, boolean *players_mask)
{
    int i;

    // Disconnected from server?

    if (ticcmds == NULL && players_mask == NULL)
    {
        D_Disconnected();
        return;
    }

    if (P4_DoomNetActive() && D_P4TicCapacity() == 0)
        I_Error("P4 canonical tic buffer exhausted");

    for (i = 0; i < NET_MAXPLAYERS; ++i)
    {
        if (!P4_DoomNetActive() && !drone && i == localplayer)
        {
            // This is us.  Don't overwrite it.
        }
        else
        {
            ticdata[recvtic % BACKUPTICS].cmds[i] = ticcmds[i];
            ticdata[recvtic % BACKUPTICS].ingame[i] = players_mask[i];
        }
    }

    ++recvtic;
}

unsigned int D_P4TicCapacity(void)
{
    int pending = recvtic - (ticdup > 0 ? gametic / ticdup : gametic);
    if (pending < 0 || pending >= BACKUPTICS)
        return 0;
    return (unsigned int)(BACKUPTICS - pending);
}

int D_P4ReplayTic(void)
{
    return gametic;
}

boolean D_P4ReplayFinish(int next_input_tic)
{
    if (!P4_DoomNetReplaying() || p4_replay_input_ready || ticdup != 1
        || next_input_tic < gametic || next_input_tic < recvtic)
        return false;
    maketic = next_input_tic;
    skiptics = 0;
    lasttime = GetAdjustedTime();
    p4_replay_input_ready = true;
    return true;
}

//
// Start game loop
//
// Called after the screen is set but before the game starts running.
//

void D_StartGameLoop(void)
{
    lasttime = GetAdjustedTime() / ticdup;
}

#if ORIGCODE
//
// Block until the game start message is received from the server.
//

static void BlockUntilStart(net_gamesettings_t *settings,
                            netgame_startup_callback_t callback)
{
    while (!NET_CL_GetSettings(settings))
    {
        NET_CL_Run();
        NET_SV_Run();

        if (!net_client_connected)
        {
            I_Error("Lost connection to server");
        }

        if (callback != NULL && !callback(net_client_wait_data.ready_players,
                                          net_client_wait_data.num_players))
        {
            I_Error("Netgame startup aborted.");
        }

        I_Sleep(100);
    }
}

#endif

void D_StartNetGame(net_gamesettings_t *settings,
                    netgame_startup_callback_t callback)
{
    int i;

    if (P4_DoomNetActive())
    {
        offsetms = 0;
        recvtic = 0;
        maketic = 0;
        p4_replay_input_ready = false;
        if (!P4_DoomNetConfigure(settings))
        {
            I_Error("P4 multiplayer configuration failed");
        }
        // Initial P4 starts have queued the same neutral startup tics. Cold
        // rejoins return with no queued tics and defer input until activation.
        // Continue initial input at the next canonical tic.
        maketic = recvtic;
        localplayer = settings->consoleplayer;
        if (settings->num_players < 1 || settings->num_players > NET_MAXPLAYERS
            || localplayer < 0 || localplayer >= settings->num_players)
        {
            I_Error("Invalid P4 player capacity");
        }
        unsigned int initial_mask = P4_DoomNetInitialPlayerMask();
        unsigned int capacity_mask = (1U << settings->num_players) - 1U;
        if (!initial_mask) initial_mask = capacity_mask;
        if (!(initial_mask & 1U) || (initial_mask & ~capacity_mask)
            || (!P4_DoomNetReplaying() && !(initial_mask & (1U << localplayer))))
        {
            I_Error("Invalid P4 initial player mask");
        }
        for (i = 0; i < NET_MAXPLAYERS; ++i)
        {
            local_playeringame[i] = (initial_mask & (1U << i)) != 0;
        }
        ticdup = settings->ticdup;
        new_sync = settings->new_sync;
        net_client_connected = true;
        return;
    }
#if ORIGCODE
    offsetms = 0;
    recvtic = 0;

    settings->consoleplayer = 0;
    settings->num_players = 1;
    settings->player_classes[0] = player_class;

    //!
    // @category net
    //
    // Use new network client sync code rather than the classic
    // sync code. This is currently disabled by default because it
    // has some bugs.
    //
    if (M_CheckParm("-newsync") > 0)
        settings->new_sync = 1;
    else
        settings->new_sync = 0;

    // TODO: New sync code is not enabled by default because it's
    // currently broken. 
    //if (M_CheckParm("-oldsync") > 0)
    //    settings->new_sync = 0;
    //else
    //    settings->new_sync = 1;

    //!
    // @category net
    // @arg <n>
    //
    // Send n extra tics in every packet as insurance against dropped
    // packets.
    //

    i = M_CheckParmWithArgs("-extratics", 1);

    if (i > 0)
        settings->extratics = atoi(myargv[i+1]);
    else
        settings->extratics = 1;

    //!
    // @category net
    // @arg <n>
    //
    // Reduce the resolution of the game by a factor of n, reducing
    // the amount of network bandwidth needed.
    //

    i = M_CheckParmWithArgs("-dup", 1);

    if (i > 0)
        settings->ticdup = atoi(myargv[i+1]);
    else
        settings->ticdup = 1;

    if (net_client_connected)
    {
        // Send our game settings and block until game start is received
        // from the server.

        NET_CL_StartGame(settings);
        BlockUntilStart(settings, callback);

        // Read the game settings that were received.

        NET_CL_GetSettings(settings);
    }

    if (drone)
    {
        settings->consoleplayer = 0;
    }

    // Set the local player and playeringame[] values.

    localplayer = settings->consoleplayer;

    for (i = 0; i < NET_MAXPLAYERS; ++i)
    {
        local_playeringame[i] = i < settings->num_players;
    }

    // Copy settings to global variables.

    ticdup = settings->ticdup;
    new_sync = settings->new_sync;

    // TODO: Message disabled until we fix new_sync.
    //if (!new_sync)
    //{
    //    printf("Syncing netgames like Vanilla Doom.\n");
    //}
#else
    settings->consoleplayer = 0;
	settings->num_players = 1;
	settings->player_classes[0] = player_class;
	settings->new_sync = 0;
	settings->extratics = 1;
	settings->ticdup = 1;

	ticdup = settings->ticdup;
	new_sync = settings->new_sync;
#endif
}

boolean D_InitNetGame(net_connect_data_t *connect_data)
{
    boolean result = P4_DoomNetActive();
#ifdef FEATURE_MULTIPLAYER
    net_addr_t *addr = NULL;
    int i;
#endif

    // Call D_QuitNetGame on exit:

    I_AtExit(D_QuitNetGame, true);

    player_class = connect_data->player_class;

#ifdef FEATURE_MULTIPLAYER

    //!
    // @category net
    //
    // Start a multiplayer server, listening for connections.
    //

    if (M_CheckParm("-server") > 0
     || M_CheckParm("-privateserver") > 0)
    {
        NET_SV_Init();
        NET_SV_AddModule(&net_loop_server_module);
        NET_SV_AddModule(&net_sdl_module);
        NET_SV_RegisterWithMaster();

        net_loop_client_module.InitClient();
        addr = net_loop_client_module.ResolveAddress(NULL);
    }
    else
    {
        //!
        // @category net
        //
        // Automatically search the local LAN for a multiplayer
        // server and join it.
        //

        i = M_CheckParm("-autojoin");

        if (i > 0)
        {
            addr = NET_FindLANServer();

            if (addr == NULL)
            {
                I_Error("No server found on local LAN");
            }
        }

        //!
        // @arg <address>
        // @category net
        //
        // Connect to a multiplayer server running on the given
        // address.
        //

        i = M_CheckParmWithArgs("-connect", 1);

        if (i > 0)
        {
            net_sdl_module.InitClient();
            addr = net_sdl_module.ResolveAddress(myargv[i+1]);

            if (addr == NULL)
            {
                I_Error("Unable to resolve '%s'\n", myargv[i+1]);
            }
        }
    }

    if (addr != NULL)
    {
        if (M_CheckParm("-drone") > 0)
        {
            connect_data->drone = true;
        }

        if (!NET_CL_Connect(addr, connect_data))
        {
            I_Error("D_InitNetGame: Failed to connect to %s\n",
                    NET_AddrToString(addr));
        }

        printf("D_InitNetGame: Connected to %s\n", NET_AddrToString(addr));

        // Wait for launch message received from server.

        NET_WaitForLaunch();

        result = true;
    }
#endif

    return result;
}


//
// D_QuitNetGame
// Called before quitting to leave a net game
// without hanging the other players
//
void D_QuitNetGame (void)
{
    P4_DoomNetQuit();
#ifdef FEATURE_MULTIPLAYER
    NET_SV_Shutdown();
    NET_CL_Disconnect();
#endif
}

static int GetLowTic(void)
{
    int lowtic;

    lowtic = maketic;

    if (P4_DoomNetActive() && recvtic < lowtic)
    {
        lowtic = recvtic;
    }

#ifdef FEATURE_MULTIPLAYER
    if (net_client_connected)
    {
        if (drone || recvtic < lowtic)
        {
            lowtic = recvtic;
        }
    }
#endif

    return lowtic;
}

static int frameon;
static int frameskip[4];
static int oldnettics;

static void OldNetSync(void)
{
    unsigned int i;
    int keyplayer = -1;

    frameon++;

    // ideally maketic should be 1 - 3 tics above lowtic
    // if we are consistantly slower, speed up time

    for (i=0 ; i<NET_MAXPLAYERS ; i++)
    {
        if (local_playeringame[i])
        {
            keyplayer = i;
            break;
        }
    }

    if (keyplayer < 0)
    {
        // If there are no players, we can never advance anyway

        return;
    }

    if (localplayer == keyplayer)
    {
        // the key player does not adapt
    }
    else
    {
        if (maketic <= recvtic)
        {
            lasttime--;
            // printf ("-");
        }

        frameskip[frameon & 3] = oldnettics > recvtic;
        oldnettics = maketic;

        if (frameskip[0] && frameskip[1] && frameskip[2] && frameskip[3])
        {
            skiptics = 1;
            // printf ("+");
        }
    }
}

// Returns true if there are players in the game:

static boolean PlayersInGame(void)
{
    boolean result = false;
    unsigned int i;

    // If we are connected to a server, check if there are any players
    // in the game.

    if (net_client_connected)
    {
        for (i = 0; i < NET_MAXPLAYERS; ++i)
        {
            result = result || local_playeringame[i];
        }
    }

    // Whether single or multi-player, unless we are running as a drone,
    // we are in the game.

    if (!drone)
    {
        result = true;
    }

    return result;
}

// When using ticdup, certain values must be cleared out when running
// the duplicate ticcmds.

static void TicdupSquash(ticcmd_set_t *set)
{
    ticcmd_t *cmd;
    unsigned int i;

    for (i = 0; i < NET_MAXPLAYERS ; ++i)
    {
        cmd = &set->cmds[i];
        cmd->chatchar = 0;
        if (cmd->buttons & BT_SPECIAL)
            cmd->buttons = 0;
    }
}

// When running in single player mode, clear all the ingame[] array
// except the local player.

static void SinglePlayerClear(ticcmd_set_t *set)
{
    unsigned int i;

    for (i = 0; i < NET_MAXPLAYERS; ++i)
    {
        if (i != localplayer)
        {
            set->ingame[i] = false;
        }
    }
}

//
// TryRunTics
//

static int p4_checkpoint_oldentertics;

void TryRunTics (void)
{
    int	i;
    int	lowtic;
    int	entertic;

    int realtics;
    int	availabletics;
    int	counts;

    if (P4_DoomNetCheckpointBoundary()) return;

    // Replay is limited by received commands, never the guest's wall clock.
    // Bound one invocation so transport polling and the scheduler keep running.
    if (P4_DoomNetReplaying())
    {
        unsigned int replayed = 0;
        while (replayed < 256 && P4_DoomNetReplaying())
        {
            ticcmd_set_t *set;
            NetUpdate();
            if (gametic >= recvtic)
            {
                /* No canonical command is available after polling. Yield one
                 * millisecond instead of spinning through the outer game loop;
                 * ready replay commands continue without a wall-clock throttle. */
                if (P4_DoomNetReplaying())
                    I_Sleep(1);
                break;
            }
            if (ticdup != 1)
                I_Error("P4 replay requires unduplicated tics");
            if (P4_DoomNetCheckpointBoundary()) return;
            set = &ticdata[gametic % BACKUPTICS];
            memcpy(local_playeringame, set->ingame,
                   sizeof(local_playeringame));
            P4_ENGINE_PERF_BEGIN(sim_started);
            loop_interface->RunTic(set->cmds, set->ingame);
            P4_ENGINE_PERF_END(P4_DOOM_ENGINE_SIM, sim_started);
            ++gametic;
            ++replayed;
        }
        return;
    }

    // get real tics
    entertic = I_GetTime() / ticdup;
    realtics = entertic - p4_checkpoint_oldentertics;
    p4_checkpoint_oldentertics = entertic;

    // in singletics mode, run a single tic every time this function
    // is called.

    if (singletics)
    {
        BuildNewTic();
    }
    else
    {
        NetUpdate ();
    }

    lowtic = GetLowTic();

    availabletics = lowtic - gametic/ticdup;

    // decide how many tics to run

    if (new_sync)
    {
	counts = availabletics;
    }
    else
    {
        // decide how many tics to run
        if (realtics < availabletics-1)
            counts = realtics+1;
        else if (realtics < availabletics)
            counts = realtics;
        else
            counts = availabletics;

        if (counts < 1)
            counts = 1;

        if (net_client_connected)
        {
            OldNetSync();
        }
    }

    if (counts < 1)
	counts = 1;

    // wait for new tics if needed

    while (!PlayersInGame() || lowtic < gametic/ticdup + counts)
    {
	NetUpdate ();

        lowtic = GetLowTic();

	if (lowtic < gametic/ticdup)
	    I_Error ("TryRunTics: lowtic < gametic");

        // Don't stay in this loop forever.  The menu is still running,
        // so return to update the screen

	if (I_GetTime() / ticdup - entertic > 0)
	{
	    return;
	}

        I_Sleep(1);
    }

    // run the count * ticdup dics
    while (counts--)
    {
        ticcmd_set_t *set;

        if (!PlayersInGame())
        {
            return;
        }

        set = &ticdata[(gametic / ticdup) % BACKUPTICS];

        if (!net_client_connected)
        {
            SinglePlayerClear(set);
        }

	for (i=0 ; i<ticdup ; i++)
	{
            if (gametic/ticdup > lowtic)
                I_Error ("gametic>lowtic");

            if (P4_DoomNetCheckpointBoundary()) return;
            memcpy(local_playeringame, set->ingame, sizeof(local_playeringame));

            P4_ENGINE_PERF_BEGIN(sim_started);
            loop_interface->RunTic(set->cmds, set->ingame);
            P4_ENGINE_PERF_END(P4_DOOM_ENGINE_SIM, sim_started);
	    gametic++;

	    // modify command for duplicated tics

            TicdupSquash(set);
	}

	NetUpdate ();	// check for new console commands
    }
}

void D_RegisterLoopCallbacks(loop_interface_t *i)
{
    loop_interface = i;
}

/* Canonical replay restarts at a verified
 * snapshot boundary, with transport queues freshly empty, not copied from host. */
boolean D_P4CheckpointRebase(int next_tic, unsigned int mask)
{
    unsigned int slot;
    if (!P4_DoomNetActive() || !P4_DoomNetReplaying() || ticdup != 1
        || !loop_interface || next_tic < 0 || next_tic > 1000000000
        || (mask & 1U) == 0 || (mask & ~15U) != 0)
        return false;
    memset(ticdata, 0, sizeof(ticdata));
    for (slot = 0; slot < NET_MAXPLAYERS; ++slot)
        local_playeringame[slot] = (mask & (1U << slot)) != 0;
    gametic = recvtic = maketic = next_tic;
    p4_replay_input_ready = false;
    skiptics = frameon = oldnettics = 0;
    memset(frameskip, 0, sizeof(frameskip));
    offsetms = 0;
    lasttime = GetAdjustedTime();
    p4_checkpoint_oldentertics = I_GetTime();
    return true;
}
