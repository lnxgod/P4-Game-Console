// SPDX-License-Identifier: MIT

#include "p4/multiplayer.h"

#include <string.h>

enum {
    OFFER_MODE_OFFSET = 1,
    OFFER_API_MAJOR_OFFSET = 2,
    OFFER_API_MINOR_OFFSET = 3,
    OFFER_PLAYERS_PRESENT_OFFSET = 4,
    OFFER_PLAYER_CAPACITY_OFFSET = 5,
    OFFER_INPUT_DELAY_OFFSET = 6,
    OFFER_FLAGS_OFFSET = 7,
    OFFER_TICK_RATE_OFFSET = 8,
    OFFER_GAME_PROTOCOL_OFFSET = 10,
    OFFER_RESERVED_OFFSET = 12,
    OFFER_SEED_OFFSET = 16,
    OFFER_GAME_ID_OFFSET = 24,
    OFFER_CONTENT_HASH_OFFSET = 56,
    OFFER_COMPATIBILITY_HASH_OFFSET = 88,
    JOIN_REQUESTED_SLOT_OFFSET = 1,
    JOIN_FLAGS_OFFSET = 2,
    JOIN_COMPATIBILITY_HASH_OFFSET = 4,
    JOIN_NONCE_OFFSET = 36,
    ACCEPT_ASSIGNED_SLOT_OFFSET = 1,
    ACCEPT_PLAYER_COUNT_OFFSET = 2,
    ACCEPT_INPUT_DELAY_OFFSET = 3,
    ACCEPT_START_TIC_OFFSET = 4,
    ACCEPT_SEED_OFFSET = 8,
    P4_MP_MAX_INPUT_DELAY_TICS = 15,
    P4_MP_MAX_TICK_RATE_HZ = 240,
};

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

static uint64_t read_u64(const uint8_t *bytes)
{
    return (uint64_t)read_u32(bytes) |
        ((uint64_t)read_u32(bytes + 4) << 32U);
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

static void write_u64(uint8_t *bytes, uint64_t value)
{
    write_u32(bytes, (uint32_t)value);
    write_u32(bytes + 4, (uint32_t)(value >> 32U));
}

static bool hash_valid(const uint8_t hash[P4_MP_SHA256_BYTES])
{
    if (hash == NULL) {
        return false;
    }
    uint8_t combined = 0U;
    for (size_t index = 0U; index < P4_MP_SHA256_BYTES; ++index) {
        combined |= hash[index];
    }
    return combined != 0U;
}

static bool game_id_valid(const char game_id[P4_MP_GAME_ID_BYTES])
{
    if (game_id == NULL || game_id[0] == '\0') {
        return false;
    }
    bool terminated = false;
    for (size_t index = 0U; index < P4_MP_GAME_ID_BYTES; ++index) {
        const unsigned char value = (unsigned char)game_id[index];
        if (terminated) {
            if (value != 0U) {
                return false;
            }
            continue;
        }
        if (value == 0U) {
            terminated = true;
            continue;
        }
        const bool allowed =
            (value >= (unsigned char)'a' && value <= (unsigned char)'z') ||
            (value >= (unsigned char)'0' && value <= (unsigned char)'9') ||
            value == (unsigned char)'.' || value == (unsigned char)'-' ||
            value == (unsigned char)'_';
        if (!allowed) {
            return false;
        }
    }
    return terminated;
}

static bool mode_valid(p4_mp_game_mode_t mode)
{
    return mode == P4_MP_GAME_MODE_HOST_AUTHORITATIVE ||
        mode == P4_MP_GAME_MODE_LOCKSTEP;
}

static p4_mp_status_t offer_identity_valid(
    const p4_mp_lobby_offer_t *offer)
{
    if (offer == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (!mode_valid(offer->mode) || offer->game_api_major == 0U ||
        offer->player_capacity < 2U ||
        offer->player_capacity > P4_MP_MAX_PLAYERS ||
        offer->input_delay_tics > P4_MP_MAX_INPUT_DELAY_TICS ||
        offer->tick_rate_hz == 0U ||
        offer->tick_rate_hz > P4_MP_MAX_TICK_RATE_HZ ||
        offer->game_protocol == 0U ||
        !game_id_valid(offer->game_id) ||
        !hash_valid(offer->content_sha256)) {
        return P4_MP_BAD_IDENTITY;
    }
    return P4_MP_OK;
}

static p4_mp_status_t offer_valid(const p4_mp_lobby_offer_t *offer)
{
    const p4_mp_status_t identity = offer_identity_valid(offer);
    if (identity != P4_MP_OK) {
        return identity;
    }
    if (offer->players_present == 0U ||
        offer->players_present > offer->player_capacity ||
        offer->session_seed == 0U ||
        !hash_valid(offer->compatibility_sha256)) {
        return P4_MP_BAD_IDENTITY;
    }
    return P4_MP_OK;
}

static p4_mp_status_t join_valid(const p4_mp_lobby_join_t *join)
{
    if (join == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if ((join->requested_player_slot != P4_MP_PLAYER_SLOT_ANY &&
         (join->requested_player_slot == 0U ||
          join->requested_player_slot >= P4_MP_MAX_PLAYERS)) ||
        join->join_nonce == 0U ||
        !hash_valid(join->compatibility_sha256)) {
        return P4_MP_BAD_IDENTITY;
    }
    return P4_MP_OK;
}

static p4_mp_status_t accept_valid(const p4_mp_lobby_accept_t *accept)
{
    if (accept == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (accept->assigned_player_slot == 0U ||
        accept->assigned_player_slot >= P4_MP_MAX_PLAYERS ||
        accept->player_count < 2U ||
        accept->player_count > P4_MP_MAX_PLAYERS ||
        accept->assigned_player_slot >= accept->player_count ||
        accept->input_delay_tics > P4_MP_MAX_INPUT_DELAY_TICS ||
        accept->session_seed == 0U) {
        return P4_MP_BAD_IDENTITY;
    }
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_lobby_compatibility_material(
    const p4_mp_lobby_offer_t *offer,
    uint8_t material[P4_MP_COMPATIBILITY_MATERIAL_BYTES])
{
    if (material == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    const p4_mp_status_t status = offer_identity_valid(offer);
    if (status != P4_MP_OK) {
        return status;
    }
    memset(material, 0, P4_MP_COMPATIBILITY_MATERIAL_BYTES);
    material[0] = P4_MP_LOBBY_SCHEMA;
    material[1] = (uint8_t)offer->mode;
    material[2] = offer->game_api_major;
    material[3] = offer->game_api_minor;
    material[4] = offer->player_capacity;
    material[5] = offer->input_delay_tics;
    write_u16(material + 6, offer->tick_rate_hz);
    write_u16(material + 8, offer->game_protocol);
    memcpy(material + 16, offer->game_id, P4_MP_GAME_ID_BYTES);
    memcpy(material + 48,
           offer->content_sha256,
           P4_MP_SHA256_BYTES);
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_lobby_offer_encode(
    const p4_mp_lobby_offer_t *offer,
    uint8_t payload[P4_MP_OFFER_PAYLOAD_BYTES])
{
    if (payload == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    const p4_mp_status_t status = offer_valid(offer);
    if (status != P4_MP_OK) {
        return status;
    }
    memset(payload, 0, P4_MP_OFFER_PAYLOAD_BYTES);
    payload[0] = P4_MP_LOBBY_SCHEMA;
    payload[OFFER_MODE_OFFSET] = (uint8_t)offer->mode;
    payload[OFFER_API_MAJOR_OFFSET] = offer->game_api_major;
    payload[OFFER_API_MINOR_OFFSET] = offer->game_api_minor;
    payload[OFFER_PLAYERS_PRESENT_OFFSET] = offer->players_present;
    payload[OFFER_PLAYER_CAPACITY_OFFSET] = offer->player_capacity;
    payload[OFFER_INPUT_DELAY_OFFSET] = offer->input_delay_tics;
    write_u16(payload + OFFER_TICK_RATE_OFFSET, offer->tick_rate_hz);
    write_u16(payload + OFFER_GAME_PROTOCOL_OFFSET, offer->game_protocol);
    write_u64(payload + OFFER_SEED_OFFSET, offer->session_seed);
    memcpy(payload + OFFER_GAME_ID_OFFSET,
           offer->game_id,
           P4_MP_GAME_ID_BYTES);
    memcpy(payload + OFFER_CONTENT_HASH_OFFSET,
           offer->content_sha256,
           P4_MP_SHA256_BYTES);
    memcpy(payload + OFFER_COMPATIBILITY_HASH_OFFSET,
           offer->compatibility_sha256,
           P4_MP_SHA256_BYTES);
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_lobby_offer_decode(
    const uint8_t *payload,
    size_t payload_length,
    p4_mp_lobby_offer_t *offer_out)
{
    if (payload == NULL || offer_out == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    *offer_out = (p4_mp_lobby_offer_t){0};
    if (payload_length != P4_MP_OFFER_PAYLOAD_BYTES) {
        return P4_MP_BAD_LENGTH;
    }
    if (payload[0] != P4_MP_LOBBY_SCHEMA) {
        return P4_MP_BAD_VERSION;
    }
    if (payload[OFFER_FLAGS_OFFSET] != 0U ||
        read_u32(payload + OFFER_RESERVED_OFFSET) != 0U) {
        return P4_MP_BAD_FLAGS;
    }
    *offer_out = (p4_mp_lobby_offer_t){
        .mode = (p4_mp_game_mode_t)payload[OFFER_MODE_OFFSET],
        .game_api_major = payload[OFFER_API_MAJOR_OFFSET],
        .game_api_minor = payload[OFFER_API_MINOR_OFFSET],
        .players_present = payload[OFFER_PLAYERS_PRESENT_OFFSET],
        .player_capacity = payload[OFFER_PLAYER_CAPACITY_OFFSET],
        .input_delay_tics = payload[OFFER_INPUT_DELAY_OFFSET],
        .tick_rate_hz = read_u16(payload + OFFER_TICK_RATE_OFFSET),
        .game_protocol = read_u16(payload + OFFER_GAME_PROTOCOL_OFFSET),
        .session_seed = read_u64(payload + OFFER_SEED_OFFSET),
    };
    memcpy(offer_out->game_id,
           payload + OFFER_GAME_ID_OFFSET,
           P4_MP_GAME_ID_BYTES);
    memcpy(offer_out->content_sha256,
           payload + OFFER_CONTENT_HASH_OFFSET,
           P4_MP_SHA256_BYTES);
    memcpy(offer_out->compatibility_sha256,
           payload + OFFER_COMPATIBILITY_HASH_OFFSET,
           P4_MP_SHA256_BYTES);
    return offer_valid(offer_out);
}

p4_mp_status_t p4_mp_lobby_join_encode(
    const p4_mp_lobby_join_t *join,
    uint8_t payload[P4_MP_JOIN_PAYLOAD_BYTES])
{
    if (payload == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    const p4_mp_status_t status = join_valid(join);
    if (status != P4_MP_OK) {
        return status;
    }
    memset(payload, 0, P4_MP_JOIN_PAYLOAD_BYTES);
    payload[0] = P4_MP_LOBBY_SCHEMA;
    payload[JOIN_REQUESTED_SLOT_OFFSET] = join->requested_player_slot;
    memcpy(payload + JOIN_COMPATIBILITY_HASH_OFFSET,
           join->compatibility_sha256,
           P4_MP_SHA256_BYTES);
    write_u32(payload + JOIN_NONCE_OFFSET, join->join_nonce);
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_lobby_join_decode(
    const uint8_t *payload,
    size_t payload_length,
    p4_mp_lobby_join_t *join_out)
{
    if (payload == NULL || join_out == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    *join_out = (p4_mp_lobby_join_t){0};
    if (payload_length != P4_MP_JOIN_PAYLOAD_BYTES) {
        return P4_MP_BAD_LENGTH;
    }
    if (payload[0] != P4_MP_LOBBY_SCHEMA) {
        return P4_MP_BAD_VERSION;
    }
    if (read_u16(payload + JOIN_FLAGS_OFFSET) != 0U) {
        return P4_MP_BAD_FLAGS;
    }
    join_out->requested_player_slot = payload[JOIN_REQUESTED_SLOT_OFFSET];
    memcpy(join_out->compatibility_sha256,
           payload + JOIN_COMPATIBILITY_HASH_OFFSET,
           P4_MP_SHA256_BYTES);
    join_out->join_nonce = read_u32(payload + JOIN_NONCE_OFFSET);
    return join_valid(join_out);
}

p4_mp_status_t p4_mp_lobby_accept_encode(
    const p4_mp_lobby_accept_t *accept,
    uint8_t payload[P4_MP_ACCEPT_PAYLOAD_BYTES])
{
    if (payload == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    const p4_mp_status_t status = accept_valid(accept);
    if (status != P4_MP_OK) {
        return status;
    }
    memset(payload, 0, P4_MP_ACCEPT_PAYLOAD_BYTES);
    payload[0] = P4_MP_LOBBY_SCHEMA;
    payload[ACCEPT_ASSIGNED_SLOT_OFFSET] = accept->assigned_player_slot;
    payload[ACCEPT_PLAYER_COUNT_OFFSET] = accept->player_count;
    payload[ACCEPT_INPUT_DELAY_OFFSET] = accept->input_delay_tics;
    write_u32(payload + ACCEPT_START_TIC_OFFSET, accept->start_tic);
    write_u64(payload + ACCEPT_SEED_OFFSET, accept->session_seed);
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_lobby_accept_decode(
    const uint8_t *payload,
    size_t payload_length,
    p4_mp_lobby_accept_t *accept_out)
{
    if (payload == NULL || accept_out == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    *accept_out = (p4_mp_lobby_accept_t){0};
    if (payload_length != P4_MP_ACCEPT_PAYLOAD_BYTES) {
        return P4_MP_BAD_LENGTH;
    }
    if (payload[0] != P4_MP_LOBBY_SCHEMA) {
        return P4_MP_BAD_VERSION;
    }
    *accept_out = (p4_mp_lobby_accept_t){
        .assigned_player_slot = payload[ACCEPT_ASSIGNED_SLOT_OFFSET],
        .player_count = payload[ACCEPT_PLAYER_COUNT_OFFSET],
        .input_delay_tics = payload[ACCEPT_INPUT_DELAY_OFFSET],
        .start_tic = read_u32(payload + ACCEPT_START_TIC_OFFSET),
        .session_seed = read_u64(payload + ACCEPT_SEED_OFFSET),
    };
    return accept_valid(accept_out);
}

bool p4_mp_lobby_offers_compatible(
    const p4_mp_lobby_offer_t *local,
    const p4_mp_lobby_offer_t *remote)
{
    return offer_valid(local) == P4_MP_OK &&
        offer_valid(remote) == P4_MP_OK &&
        local->mode == remote->mode &&
        local->game_api_major == remote->game_api_major &&
        local->game_api_minor == remote->game_api_minor &&
        local->player_capacity == remote->player_capacity &&
        local->input_delay_tics == remote->input_delay_tics &&
        local->tick_rate_hz == remote->tick_rate_hz &&
        local->game_protocol == remote->game_protocol &&
        memcmp(local->game_id,
               remote->game_id,
               P4_MP_GAME_ID_BYTES) == 0 &&
        memcmp(local->content_sha256,
               remote->content_sha256,
               P4_MP_SHA256_BYTES) == 0 &&
        memcmp(local->compatibility_sha256,
               remote->compatibility_sha256,
               P4_MP_SHA256_BYTES) == 0;
}

bool p4_mp_lobby_join_matches_offer(
    const p4_mp_lobby_offer_t *offer,
    const p4_mp_lobby_join_t *join)
{
    if (offer_valid(offer) != P4_MP_OK || join_valid(join) != P4_MP_OK) {
        return false;
    }
    if (join->requested_player_slot != P4_MP_PLAYER_SLOT_ANY &&
        join->requested_player_slot >= offer->player_capacity) {
        return false;
    }
    return memcmp(offer->compatibility_sha256,
                  join->compatibility_sha256,
                  P4_MP_SHA256_BYTES) == 0;
}
