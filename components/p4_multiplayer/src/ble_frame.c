// SPDX-License-Identifier: MIT

#include "p4/multiplayer_ble.h"

#include <string.h>

static const uint8_t P4_MP_BLE_MAGIC[3] = {'P', '4', 'B'};

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

uint16_t p4_mp_ble_game_token(
    const uint8_t compatibility_sha256[P4_MP_SHA256_BYTES])
{
    if (compatibility_sha256 == NULL) {
        return 0U;
    }
    uint16_t token = 0U;
    for (size_t index = 0U; index < P4_MP_SHA256_BYTES; index += 2U) {
        token ^= (uint16_t)compatibility_sha256[index] |
            (uint16_t)((uint16_t)compatibility_sha256[index + 1U] << 8U);
    }
    return token == 0U ? UINT16_C(1) : token;
}

static bool lobby_beacon_valid(const p4_mp_ble_lobby_beacon_t *beacon)
{
    return beacon != NULL && beacon->session_id != 0U &&
        beacon->game_token != 0U && beacon->players_present > 0U &&
        beacon->player_capacity >= 2U &&
        beacon->player_capacity <= P4_MP_MAX_PLAYERS &&
        beacon->players_present < beacon->player_capacity;
}

p4_mp_status_t p4_mp_ble_lobby_beacon_encode(
    const p4_mp_ble_lobby_beacon_t *beacon,
    uint8_t output[P4_MP_BLE_LOBBY_BEACON_BYTES])
{
    if (output == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (!lobby_beacon_valid(beacon)) {
        return beacon == NULL ? P4_MP_INVALID_ARGUMENT : P4_MP_BAD_IDENTITY;
    }
    output[0] = P4_MP_BLE_LOBBY_BEACON_VERSION;
    output[1] = P4_MP_BLE_LOBBY_FLAG_OPEN;
    write_u32(output + 2U, beacon->session_id);
    write_u16(output + 6U, beacon->game_token);
    output[8] = beacon->players_present;
    output[9] = beacon->player_capacity;
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_ble_lobby_beacon_decode(
    const uint8_t *bytes,
    size_t bytes_length,
    p4_mp_ble_lobby_beacon_t *beacon_out)
{
    if (bytes == NULL || beacon_out == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    *beacon_out = (p4_mp_ble_lobby_beacon_t){0};
    if (bytes_length != P4_MP_BLE_LOBBY_BEACON_BYTES) {
        return P4_MP_BAD_LENGTH;
    }
    if (bytes[0] != P4_MP_BLE_LOBBY_BEACON_VERSION) {
        return P4_MP_BAD_VERSION;
    }
    if (bytes[1] != P4_MP_BLE_LOBBY_FLAG_OPEN) {
        return P4_MP_BAD_FLAGS;
    }
    const p4_mp_ble_lobby_beacon_t decoded = {
        .session_id = read_u32(bytes + 2U),
        .game_token = read_u16(bytes + 6U),
        .players_present = bytes[8],
        .player_capacity = bytes[9],
    };
    if (!lobby_beacon_valid(&decoded)) {
        return P4_MP_BAD_IDENTITY;
    }
    *beacon_out = decoded;
    return P4_MP_OK;
}

static void reset_active(p4_mp_ble_reassembler_t *reassembler)
{
    reassembler->received_bytes = 0U;
    reassembler->expected_bytes = 0U;
    reassembler->frame_id = 0U;
    reassembler->active = false;
}

static p4_mp_ble_fragment_result_t drop_frame(
    p4_mp_ble_reassembler_t *reassembler,
    p4_mp_status_t status)
{
    reassembler->last_packet_status = status;
    if (reassembler->dropped_frames != UINT32_MAX) {
        ++reassembler->dropped_frames;
    }
    reset_active(reassembler);
    return P4_MP_BLE_FRAGMENT_DROPPED;
}

void p4_mp_ble_reassembler_init(p4_mp_ble_reassembler_t *reassembler)
{
    if (reassembler != NULL) {
        *reassembler = (p4_mp_ble_reassembler_t){0};
    }
}

p4_mp_status_t p4_mp_ble_fragment_encode(
    const uint8_t *datagram,
    size_t datagram_length,
    uint16_t frame_id,
    size_t datagram_offset,
    size_t fragment_capacity,
    uint8_t *fragment_out,
    size_t fragment_out_capacity,
    size_t *fragment_length,
    size_t *next_datagram_offset)
{
    if (datagram == NULL || fragment_out == NULL || fragment_length == NULL ||
        next_datagram_offset == NULL || frame_id == 0U ||
        datagram_length > P4_MP_MAX_DATAGRAM_BYTES ||
        datagram_offset >= datagram_length ||
        fragment_capacity <= P4_MP_BLE_FRAGMENT_HEADER_BYTES) {
        return P4_MP_INVALID_ARGUMENT;
    }
    *fragment_length = 0U;
    *next_datagram_offset = datagram_offset;
    p4_mp_packet_view_t packet;
    const p4_mp_status_t packet_status = p4_mp_packet_decode(
        datagram, datagram_length, &packet);
    if (packet_status != P4_MP_OK) {
        return packet_status;
    }
    if (fragment_capacity > P4_MP_BLE_MAX_FRAGMENT_BYTES) {
        fragment_capacity = P4_MP_BLE_MAX_FRAGMENT_BYTES;
    }
    if (fragment_out_capacity < fragment_capacity) {
        fragment_capacity = fragment_out_capacity;
    }
    if (fragment_capacity <= P4_MP_BLE_FRAGMENT_HEADER_BYTES) {
        return P4_MP_BAD_LENGTH;
    }

    const size_t payload_capacity =
        fragment_capacity - P4_MP_BLE_FRAGMENT_HEADER_BYTES;
    const size_t remaining = datagram_length - datagram_offset;
    const size_t payload_length = remaining < payload_capacity
        ? remaining : payload_capacity;
    const size_t next_offset = datagram_offset + payload_length;
    uint8_t flags = 0U;
    if (datagram_offset == 0U) {
        flags |= P4_MP_BLE_FRAGMENT_START;
    }
    if (next_offset == datagram_length) {
        flags |= P4_MP_BLE_FRAGMENT_END;
    }

    memcpy(fragment_out, P4_MP_BLE_MAGIC, sizeof(P4_MP_BLE_MAGIC));
    fragment_out[3] = P4_MP_BLE_FRAGMENT_VERSION;
    fragment_out[4] = flags;
    fragment_out[5] = 0U;
    write_u16(fragment_out + 6U, frame_id);
    write_u16(fragment_out + 8U, (uint16_t)datagram_offset);
    write_u16(fragment_out + 10U, (uint16_t)datagram_length);
    memcpy(fragment_out + P4_MP_BLE_FRAGMENT_HEADER_BYTES,
           datagram + datagram_offset, payload_length);
    *fragment_length = P4_MP_BLE_FRAGMENT_HEADER_BYTES + payload_length;
    *next_datagram_offset = next_offset;
    return P4_MP_OK;
}

p4_mp_ble_fragment_result_t p4_mp_ble_reassembler_consume(
    p4_mp_ble_reassembler_t *reassembler,
    const uint8_t *fragment,
    size_t fragment_length,
    const uint8_t **datagram_out,
    size_t *datagram_length)
{
    if (reassembler == NULL || fragment == NULL || datagram_out == NULL ||
        datagram_length == NULL) {
        return P4_MP_BLE_FRAGMENT_INVALID_ARGUMENT;
    }
    *datagram_out = NULL;
    *datagram_length = 0U;
    if (fragment_length <= P4_MP_BLE_FRAGMENT_HEADER_BYTES ||
        fragment_length > P4_MP_BLE_MAX_FRAGMENT_BYTES) {
        return drop_frame(reassembler, P4_MP_BAD_LENGTH);
    }
    if (memcmp(fragment, P4_MP_BLE_MAGIC, sizeof(P4_MP_BLE_MAGIC)) != 0) {
        return drop_frame(reassembler, P4_MP_BAD_MAGIC);
    }
    if (fragment[3] != P4_MP_BLE_FRAGMENT_VERSION) {
        return drop_frame(reassembler, P4_MP_BAD_VERSION);
    }
    const uint8_t flags = fragment[4];
    if (fragment[5] != 0U ||
        (flags & (uint8_t)~(P4_MP_BLE_FRAGMENT_START |
                           P4_MP_BLE_FRAGMENT_END)) != 0U) {
        return drop_frame(reassembler, P4_MP_BAD_FLAGS);
    }
    const uint16_t frame_id = read_u16(fragment + 6U);
    const size_t offset = read_u16(fragment + 8U);
    const size_t total = read_u16(fragment + 10U);
    const size_t payload_length =
        fragment_length - P4_MP_BLE_FRAGMENT_HEADER_BYTES;
    const bool starts = (flags & P4_MP_BLE_FRAGMENT_START) != 0U;
    const bool ends = (flags & P4_MP_BLE_FRAGMENT_END) != 0U;
    if (frame_id == 0U || total < P4_MP_HEADER_BYTES + P4_MP_TRAILER_BYTES ||
        total > P4_MP_MAX_DATAGRAM_BYTES || offset > total ||
        payload_length > total - offset) {
        return drop_frame(reassembler, P4_MP_BAD_LENGTH);
    }

    if (starts) {
        if (offset != 0U) {
            return drop_frame(reassembler, P4_MP_BAD_LENGTH);
        }
        if (reassembler->active &&
            reassembler->dropped_frames != UINT32_MAX) {
            ++reassembler->dropped_frames;
        }
        reassembler->received_bytes = 0U;
        reassembler->expected_bytes = total;
        reassembler->frame_id = frame_id;
        reassembler->active = true;
    } else if (!reassembler->active ||
               reassembler->frame_id != frame_id ||
               reassembler->expected_bytes != total ||
               reassembler->received_bytes != offset) {
        return drop_frame(reassembler, P4_MP_BAD_LENGTH);
    }

    if (!reassembler->active || reassembler->received_bytes != offset) {
        return drop_frame(reassembler, P4_MP_BAD_LENGTH);
    }
    memcpy(reassembler->datagram + offset,
           fragment + P4_MP_BLE_FRAGMENT_HEADER_BYTES, payload_length);
    reassembler->received_bytes += payload_length;
    if (reassembler->received_bytes < reassembler->expected_bytes) {
        return ends ? drop_frame(reassembler, P4_MP_BAD_LENGTH)
                    : P4_MP_BLE_FRAGMENT_NEED_MORE;
    }
    if (reassembler->received_bytes != reassembler->expected_bytes || !ends) {
        return drop_frame(reassembler, P4_MP_BAD_LENGTH);
    }

    p4_mp_packet_view_t packet;
    const p4_mp_status_t packet_status = p4_mp_packet_decode(
        reassembler->datagram, reassembler->expected_bytes, &packet);
    reassembler->last_packet_status = packet_status;
    if (packet_status != P4_MP_OK) {
        return drop_frame(reassembler, packet_status);
    }
    *datagram_out = reassembler->datagram;
    *datagram_length = reassembler->expected_bytes;
    if (reassembler->completed_frames != UINT32_MAX) {
        ++reassembler->completed_frames;
    }
    reset_active(reassembler);
    return P4_MP_BLE_FRAGMENT_DATAGRAM_READY;
}

uint64_t p4_mp_ble_route_id(const uint8_t address[6])
{
    if (address == NULL) {
        return 0U;
    }
    uint64_t route = P4_MP_BLE_ROUTE_PREFIX;
    for (unsigned index = 0U; index < 6U; ++index) {
        route |= (uint64_t)address[index] << (index * 8U);
    }
    return route;
}
