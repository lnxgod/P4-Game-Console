// SPDX-License-Identifier: GPL-2.0-or-later
/* Existing adapter fixtures consume D_ReceiveTic synchronously. Their engine
 * sink is always available and must never enter a cold-guest replay. */
#include "doomtype.h"
#include <assert.h>
unsigned int D_P4TicCapacity(void) { return 128U; }
int D_P4ReplayTic(void) { return 0; }
boolean D_P4ReplayFinish(int next_tic)
{
    (void)next_tic;
    assert(0 && "Unexpected replay activation in an ordinary-start fixture");
    return false;
}
