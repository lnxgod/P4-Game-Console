#include "p4/cartridge_transfer.h"

#include <limits.h>
#include <string.h>

#define P4CT_HEADER_BYTES 16U
#define P4CT_CRC_BYTES 4U
#define P4CT_MIN_DECODED_BYTES (P4CT_HEADER_BYTES + P4CT_CRC_BYTES)
#define P4CT_BEGIN_PAYLOAD_BYTES 40U
#define P4CT_STATUS_PAYLOAD_BYTES 8U
#define P4CT_CAPS_PAYLOAD_BYTES 16U
#define P4CT_KIND_CART_V1 UINT8_C(1)

typedef struct {
    uint8_t type;
    uint32_t session_id;
    uint32_t sequence;
    const uint8_t *payload;
    uint16_t payload_size;
    uint32_t crc;
} p4ct_frame_view_t;

static uint16_t read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) |
           ((uint32_t)bytes[3] << 24U);
}

static void write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & UINT16_C(0x00ff));
    bytes[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & UINT32_C(0x000000ff));
    bytes[1] = (uint8_t)((value >> 8U) & UINT32_C(0x000000ff));
    bytes[2] = (uint8_t)((value >> 16U) & UINT32_C(0x000000ff));
    bytes[3] = (uint8_t)(value >> 24U);
}

uint32_t p4ct_crc32(const uint8_t *bytes, size_t count)
{
    uint32_t crc = UINT32_C(0xffffffff);
    size_t index;

    if (bytes == NULL && count != 0U) {
        return 0U;
    }

    for (index = 0U; index < count; ++index) {
        uint32_t bit;
        crc ^= (uint32_t)bytes[index];
        for (bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)(0U - (crc & 1U));
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

static p4ct_result_t cobs_encode(
    const uint8_t *input,
    size_t input_size,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size)
{
    size_t read_index = 0U;
    size_t write_index = 1U;
    size_t code_index = 0U;
    uint8_t code = 1U;

    if (input == NULL || output == NULL || output_size == NULL || output_capacity == 0U) {
        return P4CT_RESULT_INVALID_ARGUMENT;
    }

    while (read_index < input_size) {
        if (input[read_index] == 0U) {
            if (code_index >= output_capacity) {
                return P4CT_RESULT_LIMIT_REACHED;
            }
            output[code_index] = code;
            code = 1U;
            code_index = write_index;
            write_index++;
            if (write_index > output_capacity) {
                return P4CT_RESULT_LIMIT_REACHED;
            }
            read_index++;
        } else {
            if (write_index >= output_capacity) {
                return P4CT_RESULT_LIMIT_REACHED;
            }
            output[write_index] = input[read_index];
            write_index++;
            read_index++;
            code++;
            if (code == UINT8_MAX) {
                if (code_index >= output_capacity) {
                    return P4CT_RESULT_LIMIT_REACHED;
                }
                output[code_index] = code;
                code = 1U;
                code_index = write_index;
                write_index++;
                if (write_index > output_capacity) {
                    return P4CT_RESULT_LIMIT_REACHED;
                }
            }
        }
    }

    if (code_index >= output_capacity || write_index >= output_capacity) {
        return P4CT_RESULT_LIMIT_REACHED;
    }
    output[code_index] = code;
    output[write_index] = 0U;
    *output_size = write_index + 1U;
    return P4CT_RESULT_OK;
}

static p4ct_result_t cobs_decode(
    const uint8_t *input,
    size_t input_size,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_size)
{
    size_t read_index = 0U;
    size_t write_index = 0U;

    if (input == NULL || output == NULL || output_size == NULL || input_size == 0U) {
        return P4CT_RESULT_INVALID_ARGUMENT;
    }

    while (read_index < input_size) {
        uint8_t code = input[read_index];
        size_t copy_count;
        size_t copy_index;

        if (code == 0U) {
            return P4CT_RESULT_MALFORMED;
        }
        read_index++;
        copy_count = (size_t)code - 1U;
        if (copy_count > input_size - read_index || copy_count > output_capacity - write_index) {
            return P4CT_RESULT_MALFORMED;
        }

        for (copy_index = 0U; copy_index < copy_count; ++copy_index) {
            output[write_index] = input[read_index];
            write_index++;
            read_index++;
        }

        if (code != UINT8_MAX && read_index < input_size) {
            if (write_index >= output_capacity) {
                return P4CT_RESULT_MALFORMED;
            }
            output[write_index] = 0U;
            write_index++;
        }
    }

    *output_size = write_index;
    return P4CT_RESULT_OK;
}

p4ct_result_t p4ct_encode_frame(
    uint8_t type,
    uint32_t session_id,
    uint32_t sequence,
    const uint8_t *payload,
    uint16_t payload_size,
    uint8_t *encoded_out,
    size_t encoded_capacity,
    size_t *encoded_size_out)
{
    uint8_t decoded[P4CT_MAX_DECODED_BYTES];
    size_t decoded_size;
    uint32_t crc;

    if (encoded_out == NULL || encoded_size_out == NULL ||
        (payload == NULL && payload_size != 0U)) {
        return P4CT_RESULT_INVALID_ARGUMENT;
    }
    if (payload_size > P4CT_MAX_FRAME_PAYLOAD_BYTES) {
        return P4CT_RESULT_LIMIT_REACHED;
    }

    decoded_size = P4CT_HEADER_BYTES + (size_t)payload_size + P4CT_CRC_BYTES;
    decoded[0] = (uint8_t)'P';
    decoded[1] = (uint8_t)'4';
    decoded[2] = P4CT_PROTOCOL_VERSION;
    decoded[3] = type;
    write_u16_le(&decoded[4], 0U);
    write_u16_le(&decoded[6], payload_size);
    write_u32_le(&decoded[8], session_id);
    write_u32_le(&decoded[12], sequence);
    if (payload_size != 0U) {
        memcpy(&decoded[P4CT_HEADER_BYTES], payload, payload_size);
    }
    crc = p4ct_crc32(decoded, P4CT_HEADER_BYTES + (size_t)payload_size);
    write_u32_le(&decoded[P4CT_HEADER_BYTES + (size_t)payload_size], crc);

    return cobs_encode(decoded, decoded_size, encoded_out, encoded_capacity, encoded_size_out);
}

static p4ct_result_t parse_frame(
    p4ct_t *context,
    const uint8_t *encoded,
    size_t encoded_size,
    p4ct_frame_view_t *frame_out)
{
    size_t decoded_size = 0U;
    uint32_t received_crc;
    uint32_t computed_crc;
    uint16_t payload_size;
    p4ct_result_t result;

    result = cobs_decode(
        encoded,
        encoded_size,
        context->decoded,
        sizeof(context->decoded),
        &decoded_size);
    if (result != P4CT_RESULT_OK || decoded_size < P4CT_MIN_DECODED_BYTES) {
        return P4CT_RESULT_MALFORMED;
    }

    received_crc = read_u32_le(&context->decoded[decoded_size - P4CT_CRC_BYTES]);
    computed_crc = p4ct_crc32(context->decoded, decoded_size - P4CT_CRC_BYTES);
    if (received_crc != computed_crc) {
        return P4CT_RESULT_MALFORMED;
    }

    payload_size = read_u16_le(&context->decoded[6]);
    if (context->decoded[0] != (uint8_t)'P' || context->decoded[1] != (uint8_t)'4' ||
        context->decoded[2] != P4CT_PROTOCOL_VERSION || read_u16_le(&context->decoded[4]) != 0U ||
        payload_size > P4CT_MAX_FRAME_PAYLOAD_BYTES ||
        decoded_size != P4CT_MIN_DECODED_BYTES + (size_t)payload_size) {
        return P4CT_RESULT_MALFORMED;
    }

    frame_out->type = context->decoded[3];
    frame_out->session_id = read_u32_le(&context->decoded[8]);
    frame_out->sequence = read_u32_le(&context->decoded[12]);
    frame_out->payload = &context->decoded[P4CT_HEADER_BYTES];
    frame_out->payload_size = payload_size;
    frame_out->crc = received_crc;
    return P4CT_RESULT_OK;
}

static bool transfer_is_staged(const p4ct_t *context)
{
    return context->state == P4CT_STATE_RECEIVING || context->state == P4CT_STATE_VERIFIED;
}

static void reset_transfer(p4ct_t *context)
{
    context->next_offset = 0U;
    context->total_size = 0U;
    memset(context->expected_sha256, 0, sizeof(context->expected_sha256));
}

static void abort_staging(p4ct_t *context)
{
    if (transfer_is_staged(context)) {
        context->backend.stage_abort(context->backend_user);
    }
    reset_transfer(context);
}

static void reset_unbound(p4ct_t *context)
{
    abort_staging(context);
    context->state = P4CT_STATE_UNBOUND;
    context->session_id = 0U;
    context->expected_sequence = 0U;
    context->last_request_valid = false;
    context->cached_response_length = 0U;
    context->rx_length = 0U;
    context->discarding_oversize = false;
}

static p4ct_result_t emit_bytes(
    p4ct_emit_fn emit,
    void *emit_user,
    const uint8_t *bytes,
    size_t size)
{
    return emit(emit_user, bytes, size) ? P4CT_RESULT_OK : P4CT_RESULT_EMIT_FAILED;
}

static void build_status_payload(
    uint8_t payload[P4CT_STATUS_PAYLOAD_BYTES],
    uint8_t request_type,
    p4ct_state_t state,
    p4ct_status_code_t status,
    uint32_t next_offset)
{
    payload[0] = request_type;
    payload[1] = (uint8_t)state;
    write_u16_le(&payload[2], (uint16_t)status);
    write_u32_le(&payload[4], next_offset);
}

static p4ct_result_t cache_response(
    p4ct_t *context,
    const p4ct_frame_view_t *request,
    uint8_t response_type,
    const uint8_t *payload,
    uint16_t payload_size,
    p4ct_emit_fn emit,
    void *emit_user)
{
    p4ct_result_t result = p4ct_encode_frame(
        response_type,
        request->session_id,
        request->sequence,
        payload,
        payload_size,
        context->cached_response,
        sizeof(context->cached_response),
        &context->cached_response_length);
    if (result != P4CT_RESULT_OK) {
        return result;
    }

    context->last_request_valid = true;
    context->last_request_type = request->type;
    context->last_request_session = request->session_id;
    context->last_request_sequence = request->sequence;
    context->last_request_crc = request->crc;
    context->last_request_payload_size = request->payload_size;
    if (request->payload_size != 0U) {
        memcpy(
            context->last_request_payload,
            request->payload,
            request->payload_size);
    }
    return emit_bytes(
        emit,
        emit_user,
        context->cached_response,
        context->cached_response_length);
}

static bool request_matches_last(
    const p4ct_t *context,
    const p4ct_frame_view_t *request)
{
    return context->last_request_valid &&
           context->last_request_session == request->session_id &&
           context->last_request_sequence == request->sequence &&
           context->last_request_type == request->type &&
           context->last_request_crc == request->crc &&
           context->last_request_payload_size == request->payload_size &&
           (request->payload_size == 0U ||
            memcmp(
                context->last_request_payload,
                request->payload,
                request->payload_size) == 0);
}

static p4ct_result_t respond_status_cached(
    p4ct_t *context,
    const p4ct_frame_view_t *request,
    bool ack,
    p4ct_status_code_t status,
    p4ct_emit_fn emit,
    void *emit_user)
{
    uint8_t payload[P4CT_STATUS_PAYLOAD_BYTES];
    build_status_payload(
        payload,
        request->type,
        context->state,
        status,
        context->next_offset);
    return cache_response(
        context,
        request,
        ack ? (uint8_t)P4CT_TYPE_ACK : (uint8_t)P4CT_TYPE_NACK,
        payload,
        (uint16_t)sizeof(payload),
        emit,
        emit_user);
}

static p4ct_result_t respond_status_transient(
    const p4ct_t *context,
    const p4ct_frame_view_t *request,
    p4ct_status_code_t status,
    p4ct_emit_fn emit,
    void *emit_user)
{
    uint8_t payload[P4CT_STATUS_PAYLOAD_BYTES];
    uint8_t encoded[32];
    size_t encoded_size = 0U;
    p4ct_result_t result;

    build_status_payload(
        payload,
        request->type,
        context->state,
        status,
        context->next_offset);
    result = p4ct_encode_frame(
        (uint8_t)P4CT_TYPE_NACK,
        request->session_id,
        request->sequence,
        payload,
        (uint16_t)sizeof(payload),
        encoded,
        sizeof(encoded),
        &encoded_size);
    if (result != P4CT_RESULT_OK) {
        return result;
    }
    return emit_bytes(emit, emit_user, encoded, encoded_size);
}

static void advance_expected_sequence(p4ct_t *context)
{
    if (context->expected_sequence != UINT32_MAX) {
        context->expected_sequence++;
    }
}

static p4ct_result_t handle_hello(
    p4ct_t *context,
    const p4ct_frame_view_t *frame,
    uint32_t now_ms,
    p4ct_emit_fn emit,
    void *emit_user)
{
    uint8_t payload[P4CT_CAPS_PAYLOAD_BYTES];

    if (frame->session_id == 0U || frame->sequence != 0U || frame->payload_size != 0U) {
        return respond_status_transient(
            context,
            frame,
            P4CT_STATUS_PROTOCOL,
            emit,
            emit_user);
    }

    if (request_matches_last(context, frame)) {
        context->last_activity_ms = now_ms;
        return emit_bytes(
            emit,
            emit_user,
            context->cached_response,
            context->cached_response_length);
    }

    abort_staging(context);
    context->state = P4CT_STATE_READY;
    context->session_id = frame->session_id;
    context->expected_sequence = 1U;
    context->last_activity_ms = now_ms;
    context->last_request_valid = false;

    write_u16_le(&payload[0], context->config.max_chunk_bytes);
    write_u16_le(&payload[2], 0U);
    write_u32_le(&payload[4], context->config.max_object_bytes);
    write_u32_le(&payload[8], context->config.inactivity_ms);
    write_u32_le(&payload[12], context->config.feature_bits);
    return cache_response(
        context,
        frame,
        (uint8_t)P4CT_TYPE_CAPS,
        payload,
        (uint16_t)sizeof(payload),
        emit,
        emit_user);
}

static p4ct_result_t finish_request(
    p4ct_t *context,
    const p4ct_frame_view_t *frame,
    bool ack,
    p4ct_status_code_t status,
    p4ct_emit_fn emit,
    void *emit_user)
{
    advance_expected_sequence(context);
    return respond_status_cached(context, frame, ack, status, emit, emit_user);
}

static p4ct_result_t handle_begin(
    p4ct_t *context,
    const p4ct_frame_view_t *frame,
    p4ct_emit_fn emit,
    void *emit_user)
{
    uint32_t total_size;

    if (context->state != P4CT_STATE_READY) {
        return finish_request(context, frame, false, P4CT_STATUS_STATE, emit, emit_user);
    }
    if (frame->payload_size != P4CT_BEGIN_PAYLOAD_BYTES) {
        return finish_request(context, frame, false, P4CT_STATUS_LENGTH, emit, emit_user);
    }
    if (frame->payload[0] != P4CT_KIND_CART_V1 || frame->payload[1] != 0U ||
        read_u16_le(&frame->payload[2]) != 0U) {
        return finish_request(context, frame, false, P4CT_STATUS_UNSUPPORTED, emit, emit_user);
    }

    total_size = read_u32_le(&frame->payload[4]);
    if (total_size == 0U || total_size > context->config.max_object_bytes) {
        return finish_request(context, frame, false, P4CT_STATUS_LIMIT, emit, emit_user);
    }
    if (context->backend.stage_begin(
            context->backend_user,
            total_size,
            &frame->payload[8]) != P4CT_RESULT_OK) {
        context->backend.stage_abort(context->backend_user);
        reset_transfer(context);
        return finish_request(context, frame, false, P4CT_STATUS_STORAGE, emit, emit_user);
    }

    context->total_size = total_size;
    context->next_offset = 0U;
    memcpy(context->expected_sha256, &frame->payload[8], P4CT_SHA256_BYTES);
    context->state = P4CT_STATE_RECEIVING;
    return finish_request(context, frame, true, P4CT_STATUS_NONE, emit, emit_user);
}

static p4ct_result_t handle_data(
    p4ct_t *context,
    const p4ct_frame_view_t *frame,
    p4ct_emit_fn emit,
    void *emit_user)
{
    uint32_t offset;
    uint16_t data_size;

    if (context->state != P4CT_STATE_RECEIVING) {
        return finish_request(context, frame, false, P4CT_STATUS_STATE, emit, emit_user);
    }
    if (frame->payload_size <= 4U) {
        return finish_request(context, frame, false, P4CT_STATUS_LENGTH, emit, emit_user);
    }
    data_size = (uint16_t)(frame->payload_size - 4U);
    if (data_size > context->config.max_chunk_bytes) {
        return finish_request(context, frame, false, P4CT_STATUS_LIMIT, emit, emit_user);
    }

    offset = read_u32_le(frame->payload);
    if (offset != context->next_offset) {
        return finish_request(context, frame, false, P4CT_STATUS_OFFSET, emit, emit_user);
    }
    if (offset > context->total_size || (uint32_t)data_size > context->total_size - offset) {
        return finish_request(context, frame, false, P4CT_STATUS_LIMIT, emit, emit_user);
    }
    if (context->backend.stage_write(
            context->backend_user,
            offset,
            &frame->payload[4],
            data_size) != P4CT_RESULT_OK) {
        abort_staging(context);
        context->state = P4CT_STATE_READY;
        return finish_request(context, frame, false, P4CT_STATUS_STORAGE, emit, emit_user);
    }

    context->next_offset += (uint32_t)data_size;
    return finish_request(context, frame, true, P4CT_STATUS_NONE, emit, emit_user);
}

static p4ct_result_t handle_end(
    p4ct_t *context,
    const p4ct_frame_view_t *frame,
    p4ct_emit_fn emit,
    void *emit_user)
{
    if (frame->payload_size != 0U) {
        return finish_request(context, frame, false, P4CT_STATUS_LENGTH, emit, emit_user);
    }
    if (context->state != P4CT_STATE_RECEIVING) {
        return finish_request(context, frame, false, P4CT_STATUS_STATE, emit, emit_user);
    }
    if (context->next_offset != context->total_size) {
        return finish_request(context, frame, false, P4CT_STATUS_LENGTH, emit, emit_user);
    }
    if (context->backend.stage_verify(
            context->backend_user,
            context->total_size,
            context->expected_sha256) != P4CT_RESULT_OK) {
        abort_staging(context);
        context->state = P4CT_STATE_READY;
        return finish_request(context, frame, false, P4CT_STATUS_VERIFY, emit, emit_user);
    }

    context->state = P4CT_STATE_VERIFIED;
    return finish_request(context, frame, true, P4CT_STATUS_NONE, emit, emit_user);
}

static p4ct_result_t handle_commit(
    p4ct_t *context,
    const p4ct_frame_view_t *frame,
    p4ct_emit_fn emit,
    void *emit_user)
{
    if (frame->payload_size != 0U) {
        return finish_request(context, frame, false, P4CT_STATUS_LENGTH, emit, emit_user);
    }
    if (context->state != P4CT_STATE_VERIFIED) {
        return finish_request(context, frame, false, P4CT_STATUS_STATE, emit, emit_user);
    }
    if (context->backend.stage_commit(context->backend_user) != P4CT_RESULT_OK) {
        abort_staging(context);
        context->state = P4CT_STATE_READY;
        return finish_request(context, frame, false, P4CT_STATUS_STORAGE, emit, emit_user);
    }

    reset_transfer(context);
    context->state = P4CT_STATE_READY;
    return finish_request(context, frame, true, P4CT_STATUS_NONE, emit, emit_user);
}

static p4ct_result_t handle_abort(
    p4ct_t *context,
    const p4ct_frame_view_t *frame,
    p4ct_emit_fn emit,
    void *emit_user)
{
    if (frame->payload_size != 0U) {
        return finish_request(context, frame, false, P4CT_STATUS_LENGTH, emit, emit_user);
    }
    abort_staging(context);
    context->state = P4CT_STATE_READY;
    return finish_request(context, frame, true, P4CT_STATUS_NONE, emit, emit_user);
}

static p4ct_result_t process_frame(
    p4ct_t *context,
    const uint8_t *encoded,
    size_t encoded_size,
    uint32_t now_ms,
    p4ct_emit_fn emit,
    void *emit_user)
{
    p4ct_frame_view_t frame;
    p4ct_result_t result = parse_frame(context, encoded, encoded_size, &frame);
    if (result != P4CT_RESULT_OK) {
        return P4CT_RESULT_OK;
    }

    if (frame.type == (uint8_t)P4CT_TYPE_HELLO) {
        return handle_hello(context, &frame, now_ms, emit, emit_user);
    }
    if (context->state == P4CT_STATE_UNBOUND) {
        return P4CT_RESULT_OK;
    }
    if (frame.session_id != context->session_id) {
        return respond_status_transient(
            context,
            &frame,
            P4CT_STATUS_SESSION,
            emit,
            emit_user);
    }

    if (context->last_request_valid &&
        context->last_request_session == frame.session_id &&
        context->last_request_sequence == frame.sequence) {
        context->last_activity_ms = now_ms;
        if (request_matches_last(context, &frame)) {
            return emit_bytes(
                emit,
                emit_user,
                context->cached_response,
                context->cached_response_length);
        }
        return respond_status_transient(
            context,
            &frame,
            P4CT_STATUS_SEQUENCE_REUSE,
            emit,
            emit_user);
    }

    if (frame.sequence != context->expected_sequence) {
        return respond_status_transient(
            context,
            &frame,
            P4CT_STATUS_SEQUENCE,
            emit,
            emit_user);
    }

    context->last_activity_ms = now_ms;
    switch ((p4ct_frame_type_t)frame.type) {
    case P4CT_TYPE_BEGIN:
        return handle_begin(context, &frame, emit, emit_user);
    case P4CT_TYPE_DATA:
        return handle_data(context, &frame, emit, emit_user);
    case P4CT_TYPE_END:
        return handle_end(context, &frame, emit, emit_user);
    case P4CT_TYPE_COMMIT:
        return handle_commit(context, &frame, emit, emit_user);
    case P4CT_TYPE_ABORT:
        return handle_abort(context, &frame, emit, emit_user);
    case P4CT_TYPE_HELLO:
    case P4CT_TYPE_CAPS:
    case P4CT_TYPE_ACK:
    case P4CT_TYPE_NACK:
    default:
        return finish_request(
            context,
            &frame,
            false,
            P4CT_STATUS_UNSUPPORTED,
            emit,
            emit_user);
    }
}

p4ct_result_t p4ct_init(
    p4ct_t *context,
    const p4ct_config_t *config,
    const p4ct_backend_t *backend,
    void *backend_user)
{
    if (context == NULL || config == NULL || backend == NULL ||
        backend->stage_begin == NULL || backend->stage_write == NULL ||
        backend->stage_verify == NULL || backend->stage_commit == NULL ||
        backend->stage_abort == NULL || config->max_object_bytes == 0U ||
        config->max_object_bytes > P4CT_ABSOLUTE_MAX_OBJECT_BYTES ||
        config->max_chunk_bytes == 0U || config->max_chunk_bytes > P4CT_MAX_CHUNK_BYTES ||
        config->inactivity_ms == 0U) {
        return P4CT_RESULT_INVALID_ARGUMENT;
    }

    memset(context, 0, sizeof(*context));
    context->config = *config;
    context->backend = *backend;
    context->backend_user = backend_user;
    context->state = P4CT_STATE_UNBOUND;
    return P4CT_RESULT_OK;
}

p4ct_result_t p4ct_feed(
    p4ct_t *context,
    const uint8_t *bytes,
    size_t count,
    uint32_t now_ms,
    p4ct_emit_fn emit,
    void *emit_user)
{
    size_t index;

    if (context == NULL || emit == NULL || (bytes == NULL && count != 0U)) {
        return P4CT_RESULT_INVALID_ARGUMENT;
    }

    for (index = 0U; index < count; ++index) {
        const uint8_t byte = bytes[index];
        if (context->discarding_oversize) {
            if (byte == 0U) {
                context->discarding_oversize = false;
                context->rx_length = 0U;
            }
            continue;
        }

        if (byte == 0U) {
            p4ct_result_t result;
            if (context->rx_length == 0U) {
                continue;
            }
            result = process_frame(
                context,
                context->rx_encoded,
                context->rx_length,
                now_ms,
                emit,
                emit_user);
            context->rx_length = 0U;
            if (result != P4CT_RESULT_OK) {
                return result;
            }
            continue;
        }

        if (context->rx_length >= sizeof(context->rx_encoded)) {
            context->rx_length = 0U;
            context->discarding_oversize = true;
            continue;
        }
        context->rx_encoded[context->rx_length] = byte;
        context->rx_length++;
    }

    return P4CT_RESULT_OK;
}

void p4ct_tick(p4ct_t *context, uint32_t now_ms)
{
    if (context == NULL || context->state == P4CT_STATE_UNBOUND) {
        return;
    }
    if ((uint32_t)(now_ms - context->last_activity_ms) >= context->config.inactivity_ms) {
        reset_unbound(context);
    }
}

void p4ct_disconnect(p4ct_t *context)
{
    if (context != NULL) {
        reset_unbound(context);
        context->rx_length = 0U;
        context->discarding_oversize = false;
    }
}

p4ct_state_t p4ct_get_state(const p4ct_t *context)
{
    return context != NULL ? context->state : P4CT_STATE_UNBOUND;
}

uint32_t p4ct_get_next_offset(const p4ct_t *context)
{
    return context != NULL ? context->next_offset : 0U;
}
