/* SPDX-License-Identifier: MIT */
#ifndef P4_TEST_LEGACY_SURFACE_H
#define P4_TEST_LEGACY_SURFACE_H

/* The maintained cartridge requires native video. Preserve legacy callback and
 * renderer coverage through this test-only copy, whose storage lasts until exit.
 * Native service cases always retain the actual maintained descriptor. */
static bool test_start_game(p4_game_instance_t *instance,
                            const p4_game_descriptor_t *descriptor,
                            const p4_game_services_t *services,
                            void *state, size_t state_bytes)
{
    if ((services->available_capabilities & P4_GAME_CAP_VIDEO_HIGH_RES) != 0U) {
        return p4_game_instance_start(instance, descriptor, services,
                                      state, state_bytes);
    }
    static p4_game_descriptor_t legacy_descriptor;
    static bool initialized;
    if (!initialized) {
        legacy_descriptor = *descriptor;
        legacy_descriptor.required_capabilities &=
            (uint32_t)~P4_GAME_CAP_VIDEO_HIGH_RES;
        legacy_descriptor.optional_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
        initialized = true;
    }
    return p4_game_instance_start(instance, &legacy_descriptor, services,
                                  state, state_bytes);
}

#endif
