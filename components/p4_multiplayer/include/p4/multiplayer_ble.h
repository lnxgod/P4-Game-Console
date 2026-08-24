// SPDX-License-Identifier: MIT

#ifndef P4_MULTIPLAYER_BLE_H
#define P4_MULTIPLAYER_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/multiplayer.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_MP_BLE_LOBBY_BEACON_VERSION = 1,
    P4_MP_BLE_LOBBY_BEACON_BYTES = 10,
    P4_MP_BLE_LOBBY_FLAG_OPEN = 1U << 0U,
    P4_MP_BLE_FRAGMENT_VERSION = 1,
    P4_MP_BLE_FRAGMENT_HEADER_BYTES = 12,
    /* ATT_MTU 247 minus the three-byte ATT notification/write header. */
    P4_MP_BLE_MAX_FRAGMENT_BYTES = 244,
    P4_MP_BLE_FRAGMENT_START = 1U << 0U,
    P4_MP_BLE_FRAGMENT_END = 1U << 1U,
};

#define P4_MP_BLE_ROUTE_PREFIX UINT64_C(0x424c000000000000)

typedef enum {
    P4_MP_BLE_FRAGMENT_NEED_MORE = 0,
    P4_MP_BLE_FRAGMENT_DATAGRAM_READY,
    P4_MP_BLE_FRAGMENT_DROPPED,
    P4_MP_BLE_FRAGMENT_INVALID_ARGUMENT,
} p4_mp_ble_fragment_result_t;

/**
 * Compact service-data beacon carried in a host's legacy BLE advertisement.
 * It identifies an open room before a GATT connection is made, allowing a
 * joiner to choose one room while other nearby pairs remain isolated.
 */
typedef struct {
    uint32_t session_id;
    uint16_t game_token;
    uint8_t players_present;
    uint8_t player_capacity;
} p4_mp_ble_lobby_beacon_t;

/** Derive the nonzero compact game/version filter advertised by a host. */
uint16_t p4_mp_ble_game_token(
    const uint8_t compatibility_sha256[P4_MP_SHA256_BYTES]);

p4_mp_status_t p4_mp_ble_lobby_beacon_encode(
    const p4_mp_ble_lobby_beacon_t *beacon,
    uint8_t output[P4_MP_BLE_LOBBY_BEACON_BYTES]);

p4_mp_status_t p4_mp_ble_lobby_beacon_decode(
    const uint8_t *bytes,
    size_t bytes_length,
    p4_mp_ble_lobby_beacon_t *beacon_out);

/**
 * Bounded, allocation-free reassembly for one BLE peer. A fragment received
 * out of order, with inconsistent identity or length, or with an invalid P4MP
 * CRC drops the entire in-progress datagram. A new START safely replaces a
 * partial prior datagram.
 */
typedef struct {
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    size_t received_bytes;
    size_t expected_bytes;
    uint16_t frame_id;
    uint32_t dropped_frames;
    uint32_t completed_frames;
    p4_mp_status_t last_packet_status;
    bool active;
} p4_mp_ble_reassembler_t;

void p4_mp_ble_reassembler_init(p4_mp_ble_reassembler_t *reassembler);

/**
 * Encode one fragment beginning at datagram_offset. fragment_capacity is the
 * negotiated ATT value capacity and is clamped to 244 bytes. The complete
 * P4MP datagram is validated before any bytes are emitted.
 */
p4_mp_status_t p4_mp_ble_fragment_encode(
    const uint8_t *datagram,
    size_t datagram_length,
    uint16_t frame_id,
    size_t datagram_offset,
    size_t fragment_capacity,
    uint8_t *fragment_out,
    size_t fragment_out_capacity,
    size_t *fragment_length,
    size_t *next_datagram_offset);

/**
 * Consume one complete GATT write or notification value. On READY,
 * datagram_out points into the reassembler and remains valid until the next
 * consume or init call.
 */
p4_mp_ble_fragment_result_t p4_mp_ble_reassembler_consume(
    p4_mp_ble_reassembler_t *reassembler,
    const uint8_t *fragment,
    size_t fragment_length,
    const uint8_t **datagram_out,
    size_t *datagram_length);

/** Build an opaque route ID from a six-byte BLE identity address. */
uint64_t p4_mp_ble_route_id(const uint8_t address[6]);

#ifdef __cplusplus
}
#endif

#endif
