#ifndef P4_SCRIPT_GAME_API_COMPAT_H
#define P4_SCRIPT_GAME_API_COMPAT_H

/*
 * Compatibility include for early Game Platform proofs. New code should use
 * p4/script_game_api.h so it cannot collide with Console OS's native P4G API.
 */
#include "p4/script_game_api.h"

#define P4_GAME_API_VERSION P4_SCRIPT_GAME_API_VERSION

#endif
