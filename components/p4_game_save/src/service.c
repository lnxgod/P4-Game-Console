// SPDX-License-Identifier: MIT

#include "p4/game_save_service.h"

#include <string.h>

bool p4_game_save_service_init(
    p4_game_save_service_t *service,
    const char *root_path,
    p4_game_save_storage_mode_t mode,
    const char *game_id,
    const char *launch_slot_id,
    uint8_t *queue_workspace,
    size_t queue_workspace_bytes,
    uint8_t *object_workspace,
    size_t object_workspace_bytes,
    uint8_t *launch_snapshot,
    size_t launch_snapshot_capacity)
{
    if (service == NULL || !p4_game_save_game_id_valid(game_id) ||
        !p4_game_save_slot_id_valid(launch_slot_id) ||
        object_workspace == NULL ||
        object_workspace_bytes < P4_GAME_SAVE_MAX_FILE_BYTES ||
        launch_snapshot == NULL ||
        launch_snapshot_capacity < P4_GAME_SAVE_MAX_BYTES) {
        return false;
    }
    memset(service, 0, sizeof(*service));
    if (!p4_game_save_store_init(&service->store, root_path, mode) ||
        !p4_game_save_memory_init(
            &service->queue, game_id, queue_workspace,
            queue_workspace_bytes)) {
        return false;
    }
    memcpy(service->launch_slot_id, launch_slot_id,
           strlen(launch_slot_id) + 1U);
    service->object_workspace = object_workspace;
    service->object_workspace_bytes = object_workspace_bytes;
    service->launch_snapshot = launch_snapshot;
    service->launch_snapshot_capacity = launch_snapshot_capacity;
    service->initialized = true;
    if (mode != P4_GAME_SAVE_STORAGE_WRITABLE) {
        service->startup_result = mode == P4_GAME_SAVE_STORAGE_READ_ONLY
            ? P4_GAME_SAVE_STORE_READ_ONLY
            : P4_GAME_SAVE_STORE_UNAVAILABLE;
        return true;
    }
    service->startup_result = p4_game_save_store_load(
        &service->store, game_id, launch_slot_id, object_workspace,
        object_workspace_bytes, launch_snapshot, launch_snapshot_capacity,
        &service->launch_snapshot_bytes, &service->launch_schema_version,
        &service->launch_sequence);
    if (service->startup_result == P4_GAME_SAVE_STORE_NOT_FOUND) {
        service->startup_result = P4_GAME_SAVE_STORE_OK;
    } else if (service->startup_result == P4_GAME_SAVE_STORE_OK &&
               !p4_game_save_memory_seed(
                   &service->queue, launch_slot_id,
                   service->launch_schema_version,
                   service->launch_sequence,
                   launch_snapshot, service->launch_snapshot_bytes)) {
        service->startup_result = P4_GAME_SAVE_STORE_CORRUPT;
    }
    service->capability_available =
        service->startup_result == P4_GAME_SAVE_STORE_OK;
    return true;
}

void p4_game_save_service_set_storage_mode(
    p4_game_save_service_t *service,
    p4_game_save_storage_mode_t mode)
{
    if (service == NULL || !service->initialized) {
        return;
    }
    p4_game_save_store_set_mode(&service->store, mode);
    service->capability_available =
        mode == P4_GAME_SAVE_STORAGE_WRITABLE &&
        service->startup_result == P4_GAME_SAVE_STORE_OK;
}

bool p4_game_save_service_available(
    const p4_game_save_service_t *service)
{
    return service != NULL && service->initialized &&
        service->capability_available;
}

bool p4_game_save_service_queue(
    void *context,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t expected_sequence,
    const uint8_t *data,
    size_t data_bytes,
    p4_game_save_ticket_t *ticket_out)
{
    p4_game_save_service_t *const service = context;
    if (ticket_out != NULL) {
        *ticket_out = P4_GAME_SAVE_INVALID_TICKET;
    }
    return p4_game_save_service_available(service) &&
        p4_game_save_memory_queue(
            &service->queue, slot_id, schema_version, expected_sequence,
            data, data_bytes, ticket_out);
}

bool p4_game_save_service_read_status(
    void *context,
    p4_game_save_ticket_t ticket,
    p4_game_save_status_t *status_out,
    uint32_t *committed_sequence_out)
{
    p4_game_save_service_t *const service = context;
    return service != NULL && service->initialized &&
        p4_game_save_memory_read_status(
            &service->queue, ticket, status_out, committed_sequence_out);
}

static p4_game_save_status_t status_for_store_result(
    p4_game_save_store_result_t result)
{
    switch (result) {
    case P4_GAME_SAVE_STORE_CONFLICT:
        return P4_GAME_SAVE_CONFLICT;
    case P4_GAME_SAVE_STORE_READ_ONLY:
    case P4_GAME_SAVE_STORE_UNAVAILABLE:
        return P4_GAME_SAVE_UNAVAILABLE;
    case P4_GAME_SAVE_STORE_OK:
        return P4_GAME_SAVE_COMMITTED;
    default:
        return P4_GAME_SAVE_ERROR;
    }
}

size_t p4_game_save_service_process(p4_game_save_service_t *service,
                                    size_t max_commits)
{
    if (service == NULL || !service->initialized || max_commits == 0U) {
        return 0U;
    }
    size_t processed = 0U;
    while (processed < max_commits) {
        p4_game_save_pending_t pending;
        if (!p4_game_save_memory_peek_pending(&service->queue, &pending)) {
            break;
        }
        ++processed;
        if (!service->capability_available ||
            service->store.mode != P4_GAME_SAVE_STORAGE_WRITABLE) {
            (void)p4_game_save_memory_complete(
                &service->queue, pending.ticket,
                P4_GAME_SAVE_UNAVAILABLE, pending.current_sequence);
            continue;
        }
        if (pending.expected_sequence != pending.current_sequence) {
            (void)p4_game_save_memory_complete(
                &service->queue, pending.ticket, P4_GAME_SAVE_CONFLICT,
                pending.current_sequence);
            continue;
        }
        uint32_t committed_sequence = 0U;
        const p4_game_save_store_result_t result =
            p4_game_save_store_commit(
                &service->store, service->queue.game_id, pending.slot_id,
                pending.schema_version, pending.expected_sequence,
                pending.data, pending.data_bytes, service->object_workspace,
                service->object_workspace_bytes, &committed_sequence);
        const p4_game_save_status_t status = status_for_store_result(result);
        const uint32_t reported_sequence =
            status == P4_GAME_SAVE_COMMITTED
            ? committed_sequence : pending.current_sequence;
        (void)p4_game_save_memory_complete(
            &service->queue, pending.ticket, status, reported_sequence);
    }
    return processed;
}
