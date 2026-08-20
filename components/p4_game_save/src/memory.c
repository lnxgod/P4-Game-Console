// SPDX-License-Identifier: MIT

#include "p4/game_save.h"

#include <string.h>

static p4_game_save_memory_slot_t *find_slot(
    p4_game_save_memory_t *memory, const char *slot_id, bool create)
{
    p4_game_save_memory_slot_t *available = NULL;
    for (size_t index = 0U; index < P4_GAME_SAVE_MAX_SLOTS; ++index) {
        p4_game_save_memory_slot_t *const slot = &memory->slots[index];
        if (slot->used && strcmp(slot->slot_id, slot_id) == 0) {
            return slot;
        }
        if (!slot->used && available == NULL) {
            available = slot;
        }
    }
    if (!create || available == NULL) {
        return NULL;
    }
    const size_t length = strlen(slot_id);
    memcpy(available->slot_id, slot_id, length + 1U);
    available->used = true;
    available->status = P4_GAME_SAVE_READY;
    return available;
}

static const p4_game_save_memory_slot_t *find_const_slot(
    const p4_game_save_memory_t *memory, const char *slot_id)
{
    for (size_t index = 0U; index < P4_GAME_SAVE_MAX_SLOTS; ++index) {
        const p4_game_save_memory_slot_t *const slot = &memory->slots[index];
        if (slot->used && strcmp(slot->slot_id, slot_id) == 0) {
            return slot;
        }
    }
    return NULL;
}

bool p4_game_save_memory_init(p4_game_save_memory_t *memory,
                              const char *game_id,
                              uint8_t *workspace,
                              size_t workspace_bytes)
{
    if (memory == NULL || !p4_game_save_game_id_valid(game_id) ||
        workspace == NULL ||
        workspace_bytes < P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES) {
        return false;
    }
    memset(memory, 0, sizeof(*memory));
    memset(workspace, 0, P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES);
    memcpy(memory->game_id, game_id, strlen(game_id) + 1U);
    memory->workspace = workspace;
    memory->workspace_bytes = workspace_bytes;
    for (size_t index = 0U; index < P4_GAME_SAVE_MAX_SLOTS; ++index) {
        const size_t offset = index * P4_GAME_SAVE_MAX_BYTES * 2U;
        memory->slots[index].data = workspace + offset;
        memory->slots[index].pending_data =
            workspace + offset + P4_GAME_SAVE_MAX_BYTES;
    }
    memory->initialized = true;
    return true;
}

bool p4_game_save_memory_seed(p4_game_save_memory_t *memory,
                              const char *slot_id,
                              uint32_t schema_version,
                              uint32_t sequence,
                              const uint8_t *data,
                              size_t data_bytes)
{
    if (memory == NULL || !memory->initialized ||
        !p4_game_save_slot_id_valid(slot_id) || schema_version == 0U ||
        sequence == 0U || data == NULL || data_bytes == 0U ||
        data_bytes > P4_GAME_SAVE_MAX_BYTES) {
        return false;
    }
    p4_game_save_memory_slot_t *const slot =
        find_slot(memory, slot_id, true);
    if (slot == NULL || slot->status == P4_GAME_SAVE_QUEUED) {
        return false;
    }
    memmove(slot->data, data, data_bytes);
    slot->data_bytes = data_bytes;
    slot->schema_version = schema_version;
    slot->sequence = sequence;
    slot->request_valid = false;
    slot->ticket = P4_GAME_SAVE_INVALID_TICKET;
    slot->status = P4_GAME_SAVE_READY;
    slot->reported_sequence = sequence;
    return true;
}

bool p4_game_save_memory_copy_snapshot(
    const p4_game_save_memory_t *memory,
    const char *slot_id,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_bytes,
    uint32_t *schema_version_out,
    uint32_t *sequence_out)
{
    if (output_bytes != NULL) {
        *output_bytes = 0U;
    }
    if (schema_version_out != NULL) {
        *schema_version_out = 0U;
    }
    if (sequence_out != NULL) {
        *sequence_out = 0U;
    }
    if (memory == NULL || !memory->initialized ||
        !p4_game_save_slot_id_valid(slot_id) || output_bytes == NULL ||
        schema_version_out == NULL || sequence_out == NULL) {
        return false;
    }
    const p4_game_save_memory_slot_t *const slot =
        find_const_slot(memory, slot_id);
    if (slot == NULL || slot->data_bytes == 0U) {
        return true;
    }
    if (output == NULL || output_capacity < slot->data_bytes) {
        return false;
    }
    memcpy(output, slot->data, slot->data_bytes);
    *output_bytes = slot->data_bytes;
    *schema_version_out = slot->schema_version;
    *sequence_out = slot->sequence;
    return true;
}

static p4_game_save_ticket_t next_ticket(p4_game_save_memory_t *memory)
{
    ++memory->next_ticket;
    if (memory->next_ticket == P4_GAME_SAVE_INVALID_TICKET) {
        ++memory->next_ticket;
    }
    return memory->next_ticket;
}

bool p4_game_save_memory_queue(
    void *context,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t expected_sequence,
    const uint8_t *data,
    size_t data_bytes,
    p4_game_save_ticket_t *ticket_out)
{
    p4_game_save_memory_t *const memory = context;
    if (ticket_out != NULL) {
        *ticket_out = P4_GAME_SAVE_INVALID_TICKET;
    }
    if (memory == NULL || !memory->initialized || ticket_out == NULL ||
        !p4_game_save_slot_id_valid(slot_id) || schema_version == 0U ||
        data == NULL || data_bytes == 0U ||
        data_bytes > P4_GAME_SAVE_MAX_BYTES) {
        return false;
    }
    p4_game_save_memory_slot_t *const slot =
        find_slot(memory, slot_id, true);
    if (slot == NULL) {
        return false;
    }
    const bool duplicate = slot->request_valid &&
        slot->pending_schema_version == schema_version &&
        slot->pending_expected_sequence == expected_sequence &&
        slot->pending_bytes == data_bytes &&
        memcmp(slot->pending_data, data, data_bytes) == 0;
    if (duplicate) {
        *ticket_out = slot->ticket;
        return true;
    }
    if (slot->status == P4_GAME_SAVE_QUEUED) {
        return false;
    }
    memmove(slot->pending_data, data, data_bytes);
    slot->pending_bytes = data_bytes;
    slot->pending_schema_version = schema_version;
    slot->pending_expected_sequence = expected_sequence;
    slot->ticket = next_ticket(memory);
    slot->status = P4_GAME_SAVE_QUEUED;
    slot->reported_sequence = 0U;
    slot->request_valid = true;
    *ticket_out = slot->ticket;
    return true;
}

size_t p4_game_save_memory_process(p4_game_save_memory_t *memory,
                                   size_t max_commits)
{
    if (memory == NULL || !memory->initialized || max_commits == 0U) {
        return 0U;
    }
    size_t processed = 0U;
    for (size_t index = 0U;
         index < P4_GAME_SAVE_MAX_SLOTS && processed < max_commits; ++index) {
        p4_game_save_memory_slot_t *const slot = &memory->slots[index];
        if (!slot->used || slot->status != P4_GAME_SAVE_QUEUED) {
            continue;
        }
        ++processed;
        if (slot->pending_expected_sequence != slot->sequence) {
            (void)p4_game_save_memory_complete(
                memory, slot->ticket, P4_GAME_SAVE_CONFLICT,
                slot->sequence);
            continue;
        }
        if (slot->sequence == UINT32_MAX) {
            (void)p4_game_save_memory_complete(
                memory, slot->ticket, P4_GAME_SAVE_ERROR,
                slot->sequence);
            continue;
        }
        (void)p4_game_save_memory_complete(
            memory, slot->ticket, P4_GAME_SAVE_COMMITTED,
            slot->sequence + 1U);
    }
    return processed;
}

bool p4_game_save_memory_peek_pending(
    const p4_game_save_memory_t *memory,
    p4_game_save_pending_t *pending_out)
{
    if (pending_out != NULL) {
        *pending_out = (p4_game_save_pending_t){0};
    }
    if (memory == NULL || !memory->initialized || pending_out == NULL) {
        return false;
    }
    for (size_t index = 0U; index < P4_GAME_SAVE_MAX_SLOTS; ++index) {
        const p4_game_save_memory_slot_t *const slot = &memory->slots[index];
        if (slot->used && slot->status == P4_GAME_SAVE_QUEUED &&
            slot->request_valid) {
            *pending_out = (p4_game_save_pending_t){
                .slot_id = slot->slot_id,
                .data = slot->pending_data,
                .data_bytes = slot->pending_bytes,
                .schema_version = slot->pending_schema_version,
                .expected_sequence = slot->pending_expected_sequence,
                .current_sequence = slot->sequence,
                .ticket = slot->ticket,
            };
            return true;
        }
    }
    return false;
}

bool p4_game_save_memory_complete(
    p4_game_save_memory_t *memory,
    p4_game_save_ticket_t ticket,
    p4_game_save_status_t status,
    uint32_t reported_sequence)
{
    if (memory == NULL || !memory->initialized ||
        ticket == P4_GAME_SAVE_INVALID_TICKET ||
        (status != P4_GAME_SAVE_COMMITTED &&
         status != P4_GAME_SAVE_CONFLICT &&
         status != P4_GAME_SAVE_UNAVAILABLE &&
         status != P4_GAME_SAVE_ERROR)) {
        return false;
    }
    for (size_t index = 0U; index < P4_GAME_SAVE_MAX_SLOTS; ++index) {
        p4_game_save_memory_slot_t *const slot = &memory->slots[index];
        if (!slot->used || !slot->request_valid || slot->ticket != ticket ||
            slot->status != P4_GAME_SAVE_QUEUED) {
            continue;
        }
        if (status == P4_GAME_SAVE_COMMITTED) {
            if (slot->sequence == UINT32_MAX ||
                reported_sequence != slot->sequence + 1U) {
                return false;
            }
            memmove(slot->data, slot->pending_data, slot->pending_bytes);
            slot->data_bytes = slot->pending_bytes;
            slot->schema_version = slot->pending_schema_version;
            slot->sequence = reported_sequence;
        }
        slot->status = status;
        slot->reported_sequence = reported_sequence;
        return true;
    }
    return false;
}

bool p4_game_save_memory_read_status(
    void *context,
    p4_game_save_ticket_t ticket,
    p4_game_save_status_t *status_out,
    uint32_t *committed_sequence_out)
{
    const p4_game_save_memory_t *const memory = context;
    if (status_out != NULL) {
        *status_out = P4_GAME_SAVE_NONE;
    }
    if (committed_sequence_out != NULL) {
        *committed_sequence_out = 0U;
    }
    if (memory == NULL || !memory->initialized ||
        ticket == P4_GAME_SAVE_INVALID_TICKET || status_out == NULL ||
        committed_sequence_out == NULL) {
        return false;
    }
    for (size_t index = 0U; index < P4_GAME_SAVE_MAX_SLOTS; ++index) {
        const p4_game_save_memory_slot_t *const slot = &memory->slots[index];
        if (slot->used && slot->request_valid && slot->ticket == ticket) {
            *status_out = slot->status;
            *committed_sequence_out = slot->reported_sequence;
            return true;
        }
    }
    return false;
}
