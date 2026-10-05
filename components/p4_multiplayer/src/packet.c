// SPDX-License-Identifier: MIT

#include "p4/multiplayer.h"

#include <string.h>

static const uint8_t P4_MP_MAGIC[4] = {'P', '4', 'M', 'P'};

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        ((uint32_t)bytes[1] << 8U) |
        ((uint32_t)bytes[2] << 16U) |
        ((uint32_t)bytes[3] << 24U);
}

static int16_t read_i16(const uint8_t *bytes)
{
    return (int16_t)read_u16(bytes);
}

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static bool payload_length_valid(p4_mp_packet_type_t type, uint16_t length)
{
    switch (type) {
        case P4_MP_PACKET_DISCOVER:
            return length == 0U;
        case P4_MP_PACKET_OFFER:
            return length == P4_MP_OFFER_PAYLOAD_BYTES;
        case P4_MP_PACKET_JOIN:
            return length == P4_MP_JOIN_PAYLOAD_BYTES;
        case P4_MP_PACKET_ACCEPT:
            return length == P4_MP_ACCEPT_PAYLOAD_BYTES;
        case P4_MP_PACKET_INPUT:
            return length == P4_MP_INPUT_PAYLOAD_BYTES;
        case P4_MP_PACKET_STATE_HASH:
            return length == 12U;
        case P4_MP_PACKET_PING:
        case P4_MP_PACKET_PONG:
            return length == 8U;
        case P4_MP_PACKET_LEAVE:
        case P4_MP_PACKET_REJECT:
            return length == 2U;
        case P4_MP_PACKET_ACCESSORY:
            return length == 48U;
        case P4_MP_PACKET_GAME_MESSAGE:
            return length != 0U &&
                length <= P4_MP_GAME_MESSAGE_MAX_BYTES;
        default:
            return false;
    }
}

static bool identity_valid(
    p4_mp_packet_type_t type,
    uint32_t session_id,
    uint32_t peer_id,
    uint32_t sequence)
{
    if (sequence == 0U) {
        return false;
    }
    if (type == P4_MP_PACKET_DISCOVER) {
        return session_id == 0U && peer_id == 0U;
    }
    return session_id != 0U && peer_id != 0U;
}

uint32_t p4_mp_crc32(const uint8_t *bytes, size_t length)
{
    if (bytes == NULL && length != 0U) {
        return 0U;
    }
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

p4_mp_status_t p4_mp_packet_encode(
    p4_mp_packet_type_t type,
    uint32_t session_id,
    uint32_t peer_id,
    uint32_t sequence,
    uint32_t ack,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    if (output == NULL || output_length == NULL ||
        (payload == NULL && payload_length != 0U)) {
        return P4_MP_INVALID_ARGUMENT;
    }
    *output_length = 0U;
    if (!payload_length_valid(type, payload_length) ||
        payload_length > P4_MP_MAX_PAYLOAD_BYTES) {
        return P4_MP_BAD_LENGTH;
    }
    if (!identity_valid(type, session_id, peer_id, sequence)) {
        return P4_MP_BAD_IDENTITY;
    }
    const size_t total =
        P4_MP_HEADER_BYTES + (size_t)payload_length + P4_MP_TRAILER_BYTES;
    if (output_capacity < total) {
        return P4_MP_BAD_LENGTH;
    }
    memcpy(output, P4_MP_MAGIC, sizeof(P4_MP_MAGIC));
    output[4] = P4_MP_VERSION;
    output[5] = (uint8_t)type;
    write_u16(output + 6, 0U);
    write_u32(output + 8, session_id);
    write_u32(output + 12, peer_id);
    write_u32(output + 16, sequence);
    write_u32(output + 20, ack);
    write_u16(output + 24, payload_length);
    write_u16(output + 26, 0U);
    if (payload_length != 0U) {
        memcpy(output + P4_MP_HEADER_BYTES, payload, payload_length);
    }
    const uint32_t crc = p4_mp_crc32(
        output, P4_MP_HEADER_BYTES + (size_t)payload_length);
    write_u32(output + P4_MP_HEADER_BYTES + payload_length, crc);
    *output_length = total;
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_packet_decode(
    const uint8_t *datagram,
    size_t datagram_length,
    p4_mp_packet_view_t *packet_out)
{
    if (datagram == NULL || packet_out == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    *packet_out = (p4_mp_packet_view_t){0};
    if (datagram_length < P4_MP_HEADER_BYTES + P4_MP_TRAILER_BYTES) {
        return P4_MP_TOO_SHORT;
    }
    if (datagram_length > P4_MP_MAX_DATAGRAM_BYTES) {
        return P4_MP_BAD_LENGTH;
    }
    if (memcmp(datagram, P4_MP_MAGIC, sizeof(P4_MP_MAGIC)) != 0) {
        return P4_MP_BAD_MAGIC;
    }
    if (datagram[4] != P4_MP_VERSION) {
        return P4_MP_BAD_VERSION;
    }
    const p4_mp_packet_type_t type = (p4_mp_packet_type_t)datagram[5];
    if (type < P4_MP_PACKET_DISCOVER ||
        type > P4_MP_PACKET_ACCESSORY) {
        return P4_MP_BAD_TYPE;
    }
    const uint16_t flags = read_u16(datagram + 6);
    const uint16_t reserved = read_u16(datagram + 26);
    if (flags != 0U || reserved != 0U) {
        return P4_MP_BAD_FLAGS;
    }
    const uint16_t payload_length = read_u16(datagram + 24);
    if (!payload_length_valid(type, payload_length)) {
        return P4_MP_BAD_LENGTH;
    }
    const size_t expected =
        P4_MP_HEADER_BYTES + (size_t)payload_length + P4_MP_TRAILER_BYTES;
    if (datagram_length != expected) {
        return P4_MP_BAD_LENGTH;
    }
    const uint32_t session_id = read_u32(datagram + 8);
    const uint32_t peer_id = read_u32(datagram + 12);
    const uint32_t sequence = read_u32(datagram + 16);
    if (!identity_valid(type, session_id, peer_id, sequence)) {
        return P4_MP_BAD_IDENTITY;
    }
    const uint32_t expected_crc = read_u32(
        datagram + P4_MP_HEADER_BYTES + payload_length);
    const uint32_t actual_crc = p4_mp_crc32(
        datagram, P4_MP_HEADER_BYTES + (size_t)payload_length);
    if (expected_crc != actual_crc) {
        return P4_MP_BAD_CRC;
    }
    *packet_out = (p4_mp_packet_view_t){
        .type = type,
        .flags = flags,
        .session_id = session_id,
        .peer_id = peer_id,
        .sequence = sequence,
        .ack = read_u32(datagram + 20),
        .payload = datagram + P4_MP_HEADER_BYTES,
        .payload_length = payload_length,
    };
    return P4_MP_OK;
}

void p4_mp_input_encode(
    const p4_mp_input_t *input,
    uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES])
{
    if (input == NULL || payload == NULL) {
        return;
    }
    memset(payload, 0, P4_MP_INPUT_PAYLOAD_BYTES);
    write_u32(payload, input->tick);
    write_u32(payload + 4, input->buttons);
    write_u16(payload + 8, (uint16_t)input->left_x);
    write_u16(payload + 10, (uint16_t)input->left_y);
    write_u16(payload + 12, (uint16_t)input->right_x);
    write_u16(payload + 14, (uint16_t)input->right_y);
    write_u16(payload + 16, input->left_trigger);
    write_u16(payload + 18, input->right_trigger);
    payload[20] = input->dpad & UINT8_C(0x0f);
    payload[21] = input->flags;
}

p4_mp_status_t p4_mp_input_decode(
    const uint8_t *payload,
    size_t payload_length,
    p4_mp_input_t *input_out)
{
    if (payload == NULL || input_out == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    *input_out = (p4_mp_input_t){0};
    if (payload_length != P4_MP_INPUT_PAYLOAD_BYTES) {
        return P4_MP_BAD_LENGTH;
    }
    if ((payload[20] & UINT8_C(0xf0)) != 0U || payload[21] != 0U ||
        payload[22] != 0U || payload[23] != 0U) {
        return P4_MP_BAD_FLAGS;
    }
    *input_out = (p4_mp_input_t){
        .tick = read_u32(payload),
        .buttons = read_u32(payload + 4),
        .left_x = read_i16(payload + 8),
        .left_y = read_i16(payload + 10),
        .right_x = read_i16(payload + 12),
        .right_y = read_i16(payload + 14),
        .left_trigger = read_u16(payload + 16),
        .right_trigger = read_u16(payload + 18),
        .dpad = payload[20],
        .flags = payload[21],
    };
    return P4_MP_OK;
}
