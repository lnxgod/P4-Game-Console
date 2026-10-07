// SPDX-License-Identifier: MIT
#ifndef COLOR_CLASH_TEST_GAME_START_H
#define COLOR_CLASH_TEST_GAME_START_H

#include "p4/game.h"

/* The maintained descriptor must use native resolution. Legacy host fixtures
 * retain their explicit 320x200 coverage through a test-only descriptor copy.
 * Its static lifetime covers every concurrently active instance in this file.
 */
static inline bool test_start_game(p4_game_instance_t *instance,
                                   const p4_game_descriptor_t *descriptor,
                                   const p4_game_services_t *services,
                                   void *state_memory,
                                   size_t state_memory_bytes)
{
    if (descriptor == NULL || services == NULL ||
        (services->available_capabilities & P4_GAME_CAP_VIDEO_HIGH_RES) != 0U) {
        return p4_game_instance_start(instance, descriptor, services,
                                      state_memory, state_memory_bytes);
    }
    static p4_game_descriptor_t legacy_descriptor;
    static bool initialized;
    if (!initialized) {
        legacy_descriptor = *descriptor;
        legacy_descriptor.required_capabilities &=
            ~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
        legacy_descriptor.optional_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
        initialized = true;
    }
    return p4_game_instance_start(instance, &legacy_descriptor, services,
                                  state_memory, state_memory_bytes);
}

#endif
