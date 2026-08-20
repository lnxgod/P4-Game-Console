// SPDX-License-Identifier: MIT

#ifndef P4_GAME_SAVE_H
#define P4_GAME_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define P4_GAME_SAVE_MAGIC "P4SAVE1\0"

enum {
    P4_GAME_SAVE_FORMAT_VERSION = 1,
    P4_GAME_SAVE_HEADER_BYTES = 256,
    P4_GAME_SAVE_SHA256_BYTES = 32,
    P4_GAME_SAVE_MAX_FILE_BYTES =
        P4_GAME_SAVE_HEADER_BYTES + P4_GAME_SAVE_MAX_BYTES,
    P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES =
        P4_GAME_SAVE_MAX_SLOTS * P4_GAME_SAVE_MAX_BYTES * 2,
};

typedef enum {
    P4_GAME_SAVE_VALID = 0,
    P4_GAME_SAVE_BAD_ARGUMENT,
    P4_GAME_SAVE_BAD_SIZE,
    P4_GAME_SAVE_BAD_MAGIC,
    P4_GAME_SAVE_BAD_VERSION,
    P4_GAME_SAVE_BAD_LAYOUT,
    P4_GAME_SAVE_BAD_ID,
    P4_GAME_SAVE_BAD_DIGEST,
} p4_game_save_result_t;

typedef struct {
    char game_id[P4_GAME_ID_MAX_BYTES];
    char slot_id[P4_GAME_SAVE_SLOT_ID_BYTES];
    uint32_t schema_version;
    uint32_t sequence;
    uint32_t payload_offset;
    uint32_t payload_bytes;
    uint8_t payload_sha256[P4_GAME_SAVE_SHA256_BYTES];
    uint8_t object_sha256[P4_GAME_SAVE_SHA256_BYTES];
} p4_game_save_info_t;

bool p4_game_save_game_id_valid(const char *game_id);
bool p4_game_save_slot_id_valid(const char *slot_id);

p4_game_save_result_t p4_game_save_encode(
    const char *game_id,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t sequence,
    const uint8_t *payload,
    size_t payload_bytes,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_bytes);

p4_game_save_result_t p4_game_save_parse(
    const uint8_t *data,
    size_t data_bytes,
    const char *expected_game_id,
    const char *expected_slot_id,
    p4_game_save_info_t *out_info);

const char *p4_game_save_result_name(p4_game_save_result_t result);

/** One bounded slot used by the deterministic host-only save backend. */
typedef struct {
    char slot_id[P4_GAME_SAVE_SLOT_ID_BYTES];
    uint8_t *data;
    uint8_t *pending_data;
    size_t data_bytes;
    size_t pending_bytes;
    uint32_t schema_version;
    uint32_t sequence;
    uint32_t pending_schema_version;
    uint32_t pending_expected_sequence;
    p4_game_save_ticket_t ticket;
    p4_game_save_status_t status;
    uint32_t reported_sequence;
    bool used;
    bool request_valid;
} p4_game_save_memory_slot_t;

/**
 * Deterministic in-memory backend for SDL/local tests.
 *
 * The caller owns a P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES buffer for the entire
 * backend lifetime. queue_save copies before returning; process performs the
 * optimistic commit separately so tests exercise the real queued contract.
 */
typedef struct {
    char game_id[P4_GAME_ID_MAX_BYTES];
    uint8_t *workspace;
    size_t workspace_bytes;
    p4_game_save_memory_slot_t slots[P4_GAME_SAVE_MAX_SLOTS];
    p4_game_save_ticket_t next_ticket;
    bool initialized;
} p4_game_save_memory_t;

typedef struct {
    const char *slot_id;
    const uint8_t *data;
    size_t data_bytes;
    uint32_t schema_version;
    uint32_t expected_sequence;
    uint32_t current_sequence;
    p4_game_save_ticket_t ticket;
} p4_game_save_pending_t;

bool p4_game_save_memory_init(p4_game_save_memory_t *memory,
                              const char *game_id,
                              uint8_t *workspace,
                              size_t workspace_bytes);

bool p4_game_save_memory_seed(p4_game_save_memory_t *memory,
                              const char *slot_id,
                              uint32_t schema_version,
                              uint32_t sequence,
                              const uint8_t *data,
                              size_t data_bytes);

/** Missing slots return true with zero bytes/schema/sequence. */
bool p4_game_save_memory_copy_snapshot(
    const p4_game_save_memory_t *memory,
    const char *slot_id,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_bytes,
    uint32_t *schema_version_out,
    uint32_t *sequence_out);

/** Commit up to max_commits queued requests outside the game callback. */
size_t p4_game_save_memory_process(p4_game_save_memory_t *memory,
                                   size_t max_commits);

/** Borrow the next copied queued request until it is completed. */
bool p4_game_save_memory_peek_pending(
    const p4_game_save_memory_t *memory,
    p4_game_save_pending_t *pending_out);

/** Complete one queued request after an OS worker finishes persistence. */
bool p4_game_save_memory_complete(
    p4_game_save_memory_t *memory,
    p4_game_save_ticket_t ticket,
    p4_game_save_status_t status,
    uint32_t reported_sequence);

bool p4_game_save_memory_queue(
    void *context,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t expected_sequence,
    const uint8_t *data,
    size_t data_bytes,
    p4_game_save_ticket_t *ticket_out);

bool p4_game_save_memory_read_status(
    void *context,
    p4_game_save_ticket_t ticket,
    p4_game_save_status_t *status_out,
    uint32_t *committed_sequence_out);

#ifdef __cplusplus
}
#endif

#endif
