#ifndef P4_CARTRIDGE_TRANSFER_H
#define P4_CARTRIDGE_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define P4CT_PROTOCOL_VERSION UINT8_C(1)
#define P4CT_MAX_CHUNK_BYTES UINT16_C(1024)
#define P4CT_MAX_FRAME_PAYLOAD_BYTES UINT16_C(1028)
#define P4CT_MAX_DECODED_BYTES UINT16_C(1048)
#define P4CT_MAX_ENCODED_BYTES UINT16_C(1054)
#define P4CT_ABSOLUTE_MAX_OBJECT_BYTES UINT32_C(16777216)
#define P4CT_DEFAULT_INACTIVITY_MS UINT32_C(30000)
#define P4CT_SHA256_BYTES 32U

typedef int32_t p4ct_result_t;

#define P4CT_RESULT_OK ((p4ct_result_t)0)
#define P4CT_RESULT_INVALID_ARGUMENT ((p4ct_result_t)-1)
#define P4CT_RESULT_LIMIT_REACHED ((p4ct_result_t)-2)
#define P4CT_RESULT_MALFORMED ((p4ct_result_t)-3)
#define P4CT_RESULT_EMIT_FAILED ((p4ct_result_t)-4)
#define P4CT_RESULT_BACKEND_FAILED ((p4ct_result_t)-5)

typedef enum {
    P4CT_STATE_UNBOUND = 0,
    P4CT_STATE_READY = 1,
    P4CT_STATE_RECEIVING = 2,
    P4CT_STATE_VERIFIED = 3
} p4ct_state_t;

typedef enum {
    P4CT_TYPE_HELLO = 0x01,
    P4CT_TYPE_BEGIN = 0x02,
    P4CT_TYPE_DATA = 0x03,
    P4CT_TYPE_END = 0x04,
    P4CT_TYPE_COMMIT = 0x05,
    P4CT_TYPE_ABORT = 0x06,
    P4CT_TYPE_CAPS = 0x81,
    P4CT_TYPE_ACK = 0x82,
    P4CT_TYPE_NACK = 0x83
} p4ct_frame_type_t;

typedef enum {
    P4CT_STATUS_NONE = 0,
    P4CT_STATUS_PROTOCOL = 1,
    P4CT_STATUS_SESSION = 2,
    P4CT_STATUS_SEQUENCE = 3,
    P4CT_STATUS_SEQUENCE_REUSE = 4,
    P4CT_STATUS_STATE = 5,
    P4CT_STATUS_LENGTH = 6,
    P4CT_STATUS_LIMIT = 7,
    P4CT_STATUS_OFFSET = 8,
    P4CT_STATUS_STORAGE = 9,
    P4CT_STATUS_VERIFY = 10,
    P4CT_STATUS_UNSUPPORTED = 11
} p4ct_status_code_t;

typedef struct {
    uint32_t max_object_bytes;
    uint32_t inactivity_ms;
    uint32_t feature_bits;
    uint16_t max_chunk_bytes;
} p4ct_config_t;

typedef struct {
    /*
     * All callbacks are synchronous. stage_verify must hash bytes read back
     * from staging and validate the container. stage_commit must report OK
     * only after atomic activation metadata is durable. Callback byte/hash
     * pointers are valid only for the duration of the callback.
     */
    p4ct_result_t (*stage_begin)(
        void *user,
        uint32_t size,
        const uint8_t sha256[P4CT_SHA256_BYTES]);
    p4ct_result_t (*stage_write)(
        void *user,
        uint32_t offset,
        const uint8_t *data,
        uint16_t size);
    p4ct_result_t (*stage_verify)(
        void *user,
        uint32_t size,
        const uint8_t sha256[P4CT_SHA256_BYTES]);
    p4ct_result_t (*stage_commit)(void *user);
    void (*stage_abort)(void *user);
} p4ct_backend_t;

typedef bool (*p4ct_emit_fn)(void *user, const uint8_t *bytes, size_t size);

typedef struct {
    p4ct_config_t config;
    p4ct_backend_t backend;
    void *backend_user;

    p4ct_state_t state;
    uint32_t session_id;
    uint32_t expected_sequence;
    uint32_t next_offset;
    uint32_t total_size;
    uint32_t last_activity_ms;
    uint8_t expected_sha256[P4CT_SHA256_BYTES];

    uint8_t rx_encoded[P4CT_MAX_ENCODED_BYTES - 1U];
    uint8_t decoded[P4CT_MAX_DECODED_BYTES];
    size_t rx_length;
    bool discarding_oversize;

    bool last_request_valid;
    uint8_t last_request_type;
    uint32_t last_request_session;
    uint32_t last_request_sequence;
    uint32_t last_request_crc;
    uint16_t last_request_payload_size;
    uint8_t last_request_payload[P4CT_MAX_FRAME_PAYLOAD_BYTES];
    uint8_t cached_response[P4CT_MAX_ENCODED_BYTES];
    size_t cached_response_length;
} p4ct_t;

p4ct_result_t p4ct_init(
    p4ct_t *context,
    const p4ct_config_t *config,
    const p4ct_backend_t *backend,
    void *backend_user);

/* The owner must serialize feed, tick, and disconnect calls. */
p4ct_result_t p4ct_feed(
    p4ct_t *context,
    const uint8_t *bytes,
    size_t count,
    uint32_t now_ms,
    p4ct_emit_fn emit,
    void *emit_user);

void p4ct_tick(p4ct_t *context, uint32_t now_ms);
void p4ct_disconnect(p4ct_t *context);
p4ct_state_t p4ct_get_state(const p4ct_t *context);
uint32_t p4ct_get_next_offset(const p4ct_t *context);

uint32_t p4ct_crc32(const uint8_t *bytes, size_t count);

p4ct_result_t p4ct_encode_frame(
    uint8_t type,
    uint32_t session_id,
    uint32_t sequence,
    const uint8_t *payload,
    uint16_t payload_size,
    uint8_t *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_size_out);

#ifdef __cplusplus
}
#endif

#endif
