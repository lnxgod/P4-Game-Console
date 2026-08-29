// SPDX-License-Identifier: MIT

#ifndef P4_GAME_SAVE_SERVICE_H
#define P4_GAME_SAVE_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game_save.h"
#include "p4/game_save_store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    p4_game_save_store_t store;
    p4_game_save_memory_t queue;
    char launch_slot_id[P4_GAME_SAVE_SLOT_ID_BYTES];
    uint8_t *object_workspace;
    size_t object_workspace_bytes;
    uint8_t *launch_snapshot;
    size_t launch_snapshot_capacity;
    size_t launch_snapshot_bytes;
    uint32_t launch_schema_version;
    uint32_t launch_sequence;
    p4_game_save_store_result_t startup_result;
    bool launch_migrated_legacy;
    bool launch_missing_at_floor;
    bool capability_available;
    bool initialized;
} p4_game_save_service_t;

/**
 * Initialize one foreground game's namespaced durable service.
 *
 * No-save is a successful writable startup with a zero-length snapshot.
 * Corrupt, unavailable, host-owned, and read-only storage leave the service
 * initialized but remove the save capability.
 */
bool p4_game_save_service_init(
    p4_game_save_service_t *service,
    const char *root_path,
    p4_game_save_storage_mode_t mode,
    const p4_game_save_protection_t *protection,
    const p4_game_save_legacy_policy_t *legacy_policy,
    const char *game_id,
    const char *launch_slot_id,
    uint8_t *queue_workspace,
    size_t queue_workspace_bytes,
    uint8_t *object_workspace,
    size_t object_workspace_bytes,
    uint8_t *launch_snapshot,
    size_t launch_snapshot_capacity);

/** Clear the copied in-memory seal key after the worker has stopped. */
void p4_game_save_service_clear(p4_game_save_service_t *service);

void p4_game_save_service_set_storage_mode(
    p4_game_save_service_t *service,
    p4_game_save_storage_mode_t mode);

bool p4_game_save_service_available(
    const p4_game_save_service_t *service);

/** Process copied requests from an OS storage task, never a game callback. */
size_t p4_game_save_service_process(p4_game_save_service_t *service,
                                    size_t max_commits);

bool p4_game_save_service_queue(
    void *context,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t expected_sequence,
    const uint8_t *data,
    size_t data_bytes,
    p4_game_save_ticket_t *ticket_out);

bool p4_game_save_service_read_status(
    void *context,
    p4_game_save_ticket_t ticket,
    p4_game_save_status_t *status_out,
    uint32_t *committed_sequence_out);

#ifdef __cplusplus
}
#endif

#endif
