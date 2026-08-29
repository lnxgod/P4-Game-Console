// SPDX-License-Identifier: MIT

#ifndef P4_GAME_SAVE_STORE_H
#define P4_GAME_SAVE_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game_save.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_GAME_SAVE_STORE_ROOT_BYTES = 256,
    P4_GAME_SAVE_STORE_PATH_BYTES = 384,
};

typedef enum {
    P4_GAME_SAVE_STORAGE_UNAVAILABLE = 0,
    P4_GAME_SAVE_STORAGE_READ_ONLY,
    P4_GAME_SAVE_STORAGE_WRITABLE,
    P4_GAME_SAVE_STORAGE_HOST_OWNED,
} p4_game_save_storage_mode_t;

typedef enum {
    P4_GAME_SAVE_STORE_OK = 0,
    P4_GAME_SAVE_STORE_NOT_FOUND,
    P4_GAME_SAVE_STORE_CONFLICT,
    P4_GAME_SAVE_STORE_READ_ONLY,
    P4_GAME_SAVE_STORE_UNAVAILABLE,
    P4_GAME_SAVE_STORE_CORRUPT,
    P4_GAME_SAVE_STORE_IO_ERROR,
    P4_GAME_SAVE_STORE_INTERRUPTED,
    P4_GAME_SAVE_STORE_BAD_ARGUMENT,
} p4_game_save_store_result_t;

typedef enum {
    P4_GAME_SAVE_TRANSITION_STAGE_SYNCED = 1,
    P4_GAME_SAVE_TRANSITION_JOURNAL_STAGED,
    P4_GAME_SAVE_TRANSITION_BACKUP_INSTALLED,
    P4_GAME_SAVE_TRANSITION_JOURNAL_BACKED_UP,
    P4_GAME_SAVE_TRANSITION_CURRENT_INSTALLED,
    P4_GAME_SAVE_TRANSITION_JOURNAL_INSTALLED,
    P4_GAME_SAVE_TRANSITION_CURRENT_VERIFIED,
    P4_GAME_SAVE_TRANSITION_JOURNAL_REMOVED,
} p4_game_save_transition_t;

/** Return false to simulate interruption immediately after a durable step. */
typedef bool (*p4_game_save_transition_hook_fn)(
    void *context, p4_game_save_transition_t transition);

/**
 * OS-owned persistent downgrade policy. The query returns whether one legacy
 * P4SAVE1 object may still be grandfathered for this exact game/slot. The
 * close callback permanently ends that window after a sealed/empty state is
 * established. Callback failure is a fail-closed storage error.
 */
typedef bool (*p4_game_save_legacy_query_fn)(
    void *context, const char *game_id, const char *slot_id,
    bool *allowed_out);
typedef bool (*p4_game_save_legacy_close_fn)(
    void *context, const char *game_id, const char *slot_id);
typedef bool (*p4_game_save_object_query_fn)(
    void *context, const char *game_id, const char *slot_id,
    uint32_t sequence,
    const uint8_t object_sha256[P4_GAME_SAVE_SHA256_BYTES],
    bool *allowed_out);
typedef bool (*p4_game_save_object_advance_fn)(
    void *context, const char *game_id, const char *slot_id,
    uint32_t sequence,
    const uint8_t object_sha256[P4_GAME_SAVE_SHA256_BYTES]);
typedef bool (*p4_game_save_object_sequence_fn)(
    void *context, const char *game_id, const char *slot_id,
    uint32_t *sequence_out);

typedef struct {
    p4_game_save_legacy_query_fn query;
    p4_game_save_legacy_close_fn close;
    p4_game_save_object_query_fn object_query;
    p4_game_save_object_advance_fn object_advance;
    p4_game_save_object_sequence_fn object_sequence;
    void *context;
} p4_game_save_legacy_policy_t;

typedef struct {
    char root_path[P4_GAME_SAVE_STORE_ROOT_BYTES];
    p4_game_save_protection_t protection;
    p4_game_save_storage_mode_t mode;
    p4_game_save_transition_hook_fn transition_hook;
    void *transition_context;
    p4_game_save_legacy_policy_t legacy_policy;
    bool initialized;
} p4_game_save_store_t;

bool p4_game_save_store_init(p4_game_save_store_t *store,
                             const char *root_path,
                             p4_game_save_storage_mode_t mode,
                             const p4_game_save_protection_t *protection,
                             const p4_game_save_legacy_policy_t *legacy_policy);

/** Clear the in-memory device key when a foreground save service closes. */
void p4_game_save_store_clear(p4_game_save_store_t *store);

void p4_game_save_store_set_mode(p4_game_save_store_t *store,
                                 p4_game_save_storage_mode_t mode);

void p4_game_save_store_set_transition_hook(
    p4_game_save_store_t *store,
    p4_game_save_transition_hook_fn hook,
    void *context);

/** Recover one slot deterministically. Recovery never formats storage. */
p4_game_save_store_result_t p4_game_save_store_recover(
    p4_game_save_store_t *store,
    const char *game_id,
    const char *slot_id,
    uint8_t *object_workspace,
    size_t object_workspace_bytes);

/** Recover if writable, validate, and copy one immutable payload snapshot. */
p4_game_save_store_result_t p4_game_save_store_load(
    p4_game_save_store_t *store,
    const char *game_id,
    const char *slot_id,
    uint8_t *object_workspace,
    size_t object_workspace_bytes,
    uint8_t *payload_output,
    size_t payload_capacity,
    size_t *payload_bytes_out,
    uint32_t *schema_version_out,
    uint32_t *sequence_out,
    bool *authenticated_out);

/**
 * Synchronously commit one already-copied request.
 *
 * Call this only from an OS storage worker, never from queue_save. The method
 * stages, syncs, validates, journals, preserves one backup, renames, and
 * verifies before returning OK.
 */
p4_game_save_store_result_t p4_game_save_store_commit(
    p4_game_save_store_t *store,
    const char *game_id,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t expected_sequence,
    const uint8_t *payload,
    size_t payload_bytes,
    uint8_t *object_workspace,
    size_t object_workspace_bytes,
    uint32_t *committed_sequence_out);

const char *p4_game_save_store_result_name(
    p4_game_save_store_result_t result);

#ifdef __cplusplus
}
#endif

#endif
