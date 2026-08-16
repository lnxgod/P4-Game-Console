// SPDX-License-Identifier: MIT

#ifndef P4_GAME_API_ACHIEVEMENTS_H
#define P4_GAME_API_ACHIEVEMENTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_ACHIEVEMENT_MAX_ENTRIES = 32,
    P4_ACHIEVEMENT_GAME_ID_BYTES = P4_GAME_ID_MAX_BYTES,
    P4_ACHIEVEMENT_ID_BYTES = P4_GAME_ACHIEVEMENT_ID_MAX_BYTES,
    P4_ACHIEVEMENT_TITLE_BYTES = P4_GAME_ACHIEVEMENT_TITLE_MAX_BYTES,
    P4_ACHIEVEMENT_DESCRIPTION_BYTES =
        P4_GAME_ACHIEVEMENT_DESCRIPTION_MAX_BYTES,
};

typedef struct {
    char game_id[P4_ACHIEVEMENT_GAME_ID_BYTES];
    char id[P4_ACHIEVEMENT_ID_BYTES];
    char title[P4_ACHIEVEMENT_TITLE_BYTES];
    char description[P4_ACHIEVEMENT_DESCRIPTION_BYTES];
    uint64_t unlocked_at_elapsed_ms;
} p4_achievement_entry_t;

typedef struct {
    p4_achievement_entry_t entries[P4_ACHIEVEMENT_MAX_ENTRIES];
    size_t count;
    uint32_t unlock_events;
    uint32_t duplicate_events;
    uint32_t rejected_events;
} p4_achievement_catalog_t;

/** Initialize a fixed, no-allocation achievement catalog. */
void p4_achievement_catalog_init(p4_achievement_catalog_t *catalog);

/**
 * Record an achievement event. Replaying the same game-ID and achievement-ID
 * pair is a successful no-op so games may safely report on every state edge.
 */
bool p4_achievement_catalog_unlock(
    p4_achievement_catalog_t *catalog,
    const p4_game_achievement_t *achievement);

/** Adapter for p4_game_services_t.unlock_achievement. */
bool p4_achievement_catalog_service_unlock(
    void *context,
    const p4_game_achievement_t *achievement);

const p4_achievement_entry_t *p4_achievement_catalog_get(
    const p4_achievement_catalog_t *catalog,
    size_t index);

#ifdef __cplusplus
}
#endif

#endif
