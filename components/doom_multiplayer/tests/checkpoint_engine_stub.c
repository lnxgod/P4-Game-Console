// SPDX-License-Identifier: GPL-2.0-or-later
/* Protocol-7 adapter regressions never enter the checkpoint engine. Teardown
 * is unconditional; every other call fails visibly instead of faking success. */
#include "p4_doom_checkpoint.h"
#include "doomtype.h"
#include <assert.h>

bool P4_DoomCheckpointInit(void)
{
    assert(0 && "Protocol-7 fixture unexpectedly initialized checkpoints");
    return false;
}

void P4_DoomCheckpointShutdown(void) {}

bool P4_DoomCheckpointSetContentIdentity(const char *identity)
{
    (void)identity;
    assert(0 && "Protocol-7 fixture unexpectedly set checkpoint identity");
    return false;
}

bool P4_DoomCheckpointCapture(uint8_t *buffer, size_t capacity, size_t *length,
    uint32_t *next_tic, uint8_t *mask)
{
    (void)buffer; (void)capacity; (void)length; (void)next_tic; (void)mask;
    assert(0 && "Protocol-7 fixture unexpectedly captured a checkpoint");
    return false;
}

bool P4_DoomCheckpointRestore(const uint8_t *buffer, size_t length,
    uint32_t *next_tic, uint8_t *mask)
{
    (void)buffer; (void)length; (void)next_tic; (void)mask;
    assert(0 && "Protocol-7 fixture unexpectedly restored a checkpoint");
    return false;
}

const char *P4_DoomCheckpointReason(void)
{
    return "checkpoint engine unavailable in protocol-7 regression fixture";
}

void p4_doom_gc_checkpoint_get_arena(p4_doom_arena_t *out)
{
    (void)out;
    assert(0 && "Protocol-7 fixture unexpectedly read checkpoint arena");
}

bool p4_doom_gc_checkpoint_set_arena(const p4_doom_arena_t *in)
{
    (void)in;
    assert(0 && "Protocol-7 fixture unexpectedly restored checkpoint arena");
    return false;
}

boolean D_P4CheckpointRebase(int next_tic, unsigned int mask)
{
    (void)next_tic; (void)mask;
    assert(0 && "Protocol-7 fixture unexpectedly rebased engine tics");
    return false;
}

void doomgeneric_RequestQuit(void)
{
    assert(0 && "Protocol-7 fixture unexpectedly requested engine shutdown");
}
