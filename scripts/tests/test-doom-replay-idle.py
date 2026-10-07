#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production replay loop with deterministic transport/clock stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
preamble=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BACKUPTICS 128
/* Match the disabled profiling macros; cadence assertions stay unchanged. */
#define P4_ENGINE_PERF_BEGIN(name) ((void)0)
#define P4_ENGINE_PERF_END(phase, name) ((void)0)
typedef struct { int cmds[4]; int ingame[4]; } ticcmd_set_t;
static ticcmd_set_t ticdata[BACKUPTICS];
static int gametic,recvtic,ticdup=1,local_playeringame[4];
static unsigned polls,sleeps,ran,fill,stop_at;
static int replaying=1;
static int P4_DoomNetReplaying(void) { return replaying; }
/* This legacy idle fixture has no pending checkpoint. */
static int P4_DoomNetCheckpointBoundary(void) { return 0; }
static void NetUpdate(void) {
    ++polls;
    if (fill && recvtic-gametic<BACKUPTICS) { ++recvtic; --fill; }
    if (stop_at && polls==stop_at) replaying=0;
}
void I_Sleep(int ms) { assert(ms==1);assert(gametic>=recvtic);++sleeps; }
static void I_Error(const char *message) { (void)message;abort(); }
static void RunTic(int *cmds,int *mask) { (void)cmds;(void)mask;assert(gametic<recvtic);++ran; }
static struct { void (*RunTic)(int *,int *); } iface={RunTic},*loop_interface=&iface;
static void step(void) {
'''
post=r'''
}
static void reset(void) { gametic=recvtic=0;polls=sleeps=ran=fill=stop_at=0;replaying=1; }
int main(void) {
    reset();step();assert(polls==1 && sleeps==1 && ran==0);
    reset();fill=256;step();assert(polls==256 && sleeps==0 && ran==256);
    reset();recvtic=16;step();assert(polls==17 && sleeps==1 && ran==16);
    reset();stop_at=1;step();assert(polls==1 && sleeps==0 && ran==0);
    reset();replaying=0;step();assert(polls==0 && sleeps==0 && ran==0);
    reset();for(unsigned ms=0;ms<100;++ms) { if(ms%20==0) recvtic+=16;step(); }
    assert(ran==80 && sleeps==100 && polls==180);
    puts("replay idle: empty queue yields 1 ms, 256 ready tics run without sleeps, phase exit preserved, 100 ms scheduled stream drained exactly");
}
'''

source=(root/'third_party/doomgeneric/doomgeneric/d_loop.c').read_text()
start=source.index('    if (P4_DoomNetReplaying())',source.index('void TryRunTics (void)'))
opening=source.index('{',start); depth=1; end=opening+1
while depth:
    if source[end]=='{': depth+=1
    if source[end]=='}': depth-=1
    end+=1
with tempfile.TemporaryDirectory(prefix='p4-replay-idle-') as temporary:
    folder=Path(temporary); path=folder/'test.c'; binary=folder/'test'
    path.write_text(preamble+source[start:end]+post)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-g',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
