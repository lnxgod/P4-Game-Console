// SPDX-License-Identifier: MIT

#include "p4/achievements.h"

#include <limits.h>
#include <string.h>

static size_t bounded_length(const char *text, size_t limit)
{
    if (text == NULL) {
        return limit;
    }
    size_t length = 0U;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool copy_text(char *destination, size_t destination_bytes,
                      const char *source)
{
    const size_t length = bounded_length(source, destination_bytes);
    if (length == 0U || length >= destination_bytes) {
        return false;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
    return true;
}

static void increment(uint32_t *value)
{
    if (value != NULL && *value != UINT32_MAX) {
        ++*value;
    }
}

void p4_achievement_catalog_init(p4_achievement_catalog_t *catalog)
{
    if (catalog != NULL) {
        *catalog = (p4_achievement_catalog_t){0};
    }
}

bool p4_achievement_catalog_unlock(
    p4_achievement_catalog_t *catalog,
    const p4_game_achievement_t *achievement)
{
    if (catalog == NULL || achievement == NULL ||
        bounded_length(achievement->game_id, P4_ACHIEVEMENT_GAME_ID_BYTES) == 0U ||
        bounded_length(achievement->game_id, P4_ACHIEVEMENT_GAME_ID_BYTES) >=
            P4_ACHIEVEMENT_GAME_ID_BYTES ||
        bounded_length(achievement->id, P4_ACHIEVEMENT_ID_BYTES) == 0U ||
        bounded_length(achievement->id, P4_ACHIEVEMENT_ID_BYTES) >=
            P4_ACHIEVEMENT_ID_BYTES ||
        bounded_length(achievement->title, P4_ACHIEVEMENT_TITLE_BYTES) == 0U ||
        bounded_length(achievement->title, P4_ACHIEVEMENT_TITLE_BYTES) >=
            P4_ACHIEVEMENT_TITLE_BYTES ||
        bounded_length(achievement->description,
                       P4_ACHIEVEMENT_DESCRIPTION_BYTES) == 0U ||
        bounded_length(achievement->description,
                       P4_ACHIEVEMENT_DESCRIPTION_BYTES) >=
            P4_ACHIEVEMENT_DESCRIPTION_BYTES) {
        if (catalog != NULL) {
            increment(&catalog->rejected_events);
        }
        return false;
    }
    for (size_t index = 0U; index < catalog->count; ++index) {
        const p4_achievement_entry_t *const entry = &catalog->entries[index];
        if (strcmp(entry->game_id, achievement->game_id) == 0 &&
            strcmp(entry->id, achievement->id) == 0) {
            increment(&catalog->duplicate_events);
            return true;
        }
    }
    if (catalog->count >= P4_ACHIEVEMENT_MAX_ENTRIES) {
        increment(&catalog->rejected_events);
        return false;
    }
    p4_achievement_entry_t entry = {
        .unlocked_at_elapsed_ms = achievement->unlocked_at_elapsed_ms,
    };
    if (!copy_text(entry.game_id, sizeof(entry.game_id), achievement->game_id) ||
        !copy_text(entry.id, sizeof(entry.id), achievement->id) ||
        !copy_text(entry.title, sizeof(entry.title), achievement->title) ||
        !copy_text(entry.description, sizeof(entry.description),
                   achievement->description)) {
        increment(&catalog->rejected_events);
        return false;
    }
    catalog->entries[catalog->count++] = entry;
    increment(&catalog->unlock_events);
    return true;
}

bool p4_achievement_catalog_service_unlock(
    void *context,
    const p4_game_achievement_t *achievement)
{
    return p4_achievement_catalog_unlock(
        (p4_achievement_catalog_t *)context, achievement);
}

const p4_achievement_entry_t *p4_achievement_catalog_get(
    const p4_achievement_catalog_t *catalog,
    size_t index)
{
    return catalog != NULL && index < catalog->count
        ? &catalog->entries[index] : NULL;
}
