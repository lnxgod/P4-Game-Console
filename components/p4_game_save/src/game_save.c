// SPDX-License-Identifier: MIT

#include "p4/game_save.h"

#include <string.h>

#include "sha256.h"

enum {
    SAVE_MAGIC = 0,
    SAVE_HEADER_BYTES = 8,
    SAVE_TOTAL_BYTES = 12,
    SAVE_PAYLOAD_OFFSET = 16,
    SAVE_PAYLOAD_BYTES = 20,
    SAVE_FORMAT_VERSION = 24,
    SAVE_SCHEMA_VERSION = 28,
    SAVE_SEQUENCE = 32,
    SAVE_FLAGS = 36,
    SAVE_GAME_ID = 40,
    SAVE_SLOT_ID = 88,
    SAVE_PAYLOAD_SHA256 = 104,
    SAVE_OBJECT_SHA256 = 136,
    SAVE_RESERVED = 168,
    SAVE_RESERVED_BYTES = 88,
    SAVE_AUTH_SCHEME = 168,
    SAVE_AUTH_KEY_ID_BYTES = 172,
    SAVE_AUTH_KEY_ID = 176,
    SAVE_AUTH_TAG = 192,
    SAVE_AUTH_RESERVED = 224,
    SAVE_AUTH_RESERVED_BYTES = 32,
    SAVE_AUTH_SCHEME_HMAC_SHA256 = 1,
    SAVE_FLAG_AUTHENTICATED = 1,
    SHA256_BLOCK_BYTES = 64,
};

static const uint8_t s_magic[8] = {
    'P', '4', 'S', 'A', 'V', 'E', '1', 0,
};

static const uint8_t s_auth_magic[8] = {
    'P', '4', 'S', 'A', 'V', 'E', '2', 0,
};

static const uint8_t s_key_id_domain[] = {
    'P', '4', 'S', 'A', 'V', 'E', '2', '-', 'K', 'E', 'Y', '-', 'I', 'D', 0,
};

static size_t bounded_length(const char *text, size_t limit)
{
    if (text == NULL) {
        return limit;
    }
    size_t length = 0U;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

bool p4_game_save_game_id_valid(const char *game_id)
{
    const size_t length = bounded_length(game_id, P4_GAME_ID_MAX_BYTES);
    if (length < 3U || length >= P4_GAME_ID_MAX_BYTES ||
        game_id[0] < 'a' || game_id[0] > 'z') {
        return false;
    }
    for (size_t index = 1U; index < length; ++index) {
        const char character = game_id[index];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '-')) {
            return false;
        }
    }
    return true;
}

bool p4_game_save_slot_id_valid(const char *slot_id)
{
    const size_t length = bounded_length(slot_id,
                                         P4_GAME_SAVE_SLOT_ID_BYTES);
    if (length == 0U || length >= P4_GAME_SAVE_SLOT_ID_BYTES) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const char character = slot_id[index];
        const bool alpha_numeric =
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9');
        if (!alpha_numeric &&
            (index == 0U || (character != '_' && character != '-'))) {
            return false;
        }
    }
    return true;
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
        ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static bool all_zero(const uint8_t *data, size_t bytes)
{
    for (size_t index = 0U; index < bytes; ++index) {
        if (data[index] != 0U) {
            return false;
        }
    }
    return true;
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right,
                                size_t bytes)
{
    volatile uint8_t difference = 0U;
    for (size_t index = 0U; index < bytes; ++index) {
        difference = (uint8_t)(difference |
            (uint8_t)(left[index] ^ right[index]));
    }
    return difference == 0U;
}

bool p4_game_save_protection_valid(
    const p4_game_save_protection_t *protection)
{
    return protection != NULL &&
        !all_zero(protection->key, sizeof(protection->key));
}

void p4_game_save_protection_clear(
    p4_game_save_protection_t *protection)
{
    if (protection == NULL) {
        return;
    }
    volatile uint8_t *const bytes = protection->key;
    for (size_t index = 0U; index < sizeof(protection->key); ++index) {
        bytes[index] = 0U;
    }
}

static bool copy_field(char *output, size_t output_bytes,
                       const uint8_t *field, size_t field_bytes)
{
    const uint8_t *const terminator = memchr(field, 0, field_bytes);
    if (output == NULL || output_bytes != field_bytes || terminator == NULL) {
        return false;
    }
    const size_t length = (size_t)(terminator - field);
    if (!all_zero(field + length + 1U, field_bytes - length - 1U)) {
        return false;
    }
    memcpy(output, field, field_bytes);
    return true;
}

static void hash_bytes(const uint8_t *data, size_t bytes,
                       uint8_t digest[P4_GAME_SAVE_SHA256_BYTES])
{
    p4_game_save_sha256_t hash;
    p4_game_save_sha256_init(&hash);
    p4_game_save_sha256_update(&hash, data, bytes);
    p4_game_save_sha256_finish(&hash, digest);
}

static void hash_object(const uint8_t *data, size_t bytes,
                        uint8_t digest[P4_GAME_SAVE_SHA256_BYTES])
{
    static const uint8_t zero_digest[P4_GAME_SAVE_SHA256_BYTES] = {0};
    p4_game_save_sha256_t hash;
    p4_game_save_sha256_init(&hash);
    p4_game_save_sha256_update(&hash, data, SAVE_OBJECT_SHA256);
    p4_game_save_sha256_update(&hash, zero_digest, sizeof(zero_digest));
    p4_game_save_sha256_update(
        &hash, data + SAVE_OBJECT_SHA256 + P4_GAME_SAVE_SHA256_BYTES,
        bytes - SAVE_OBJECT_SHA256 - P4_GAME_SAVE_SHA256_BYTES);
    p4_game_save_sha256_finish(&hash, digest);
}

static void derive_key_id(
    const p4_game_save_protection_t *protection,
    uint8_t key_id[P4_GAME_SAVE_KEY_ID_BYTES])
{
    uint8_t digest[P4_GAME_SAVE_SHA256_BYTES];
    p4_game_save_sha256_t hash;
    p4_game_save_sha256_init(&hash);
    p4_game_save_sha256_update(
        &hash, s_key_id_domain, sizeof(s_key_id_domain));
    p4_game_save_sha256_update(
        &hash, protection->key, sizeof(protection->key));
    p4_game_save_sha256_finish(&hash, digest);
    memcpy(key_id, digest, P4_GAME_SAVE_KEY_ID_BYTES);
    volatile uint8_t *const clear = digest;
    for (size_t index = 0U; index < sizeof(digest); ++index) {
        clear[index] = 0U;
    }
}

static void hmac_object(
    const p4_game_save_protection_t *protection,
    const uint8_t *data,
    size_t bytes,
    uint8_t digest[P4_GAME_SAVE_AUTH_TAG_BYTES])
{
    uint8_t inner_pad[SHA256_BLOCK_BYTES];
    uint8_t outer_pad[SHA256_BLOCK_BYTES];
    uint8_t inner_digest[P4_GAME_SAVE_SHA256_BYTES];
    static const uint8_t zero_digest[P4_GAME_SAVE_SHA256_BYTES] = {0};
    for (size_t index = 0U; index < SHA256_BLOCK_BYTES; ++index) {
        const uint8_t key_byte = index < P4_GAME_SAVE_KEY_BYTES
            ? protection->key[index] : 0U;
        inner_pad[index] = (uint8_t)(key_byte ^ UINT8_C(0x36));
        outer_pad[index] = (uint8_t)(key_byte ^ UINT8_C(0x5c));
    }

    p4_game_save_sha256_t hash;
    p4_game_save_sha256_init(&hash);
    p4_game_save_sha256_update(&hash, inner_pad, sizeof(inner_pad));
    p4_game_save_sha256_update(&hash, data, SAVE_OBJECT_SHA256);
    p4_game_save_sha256_update(&hash, zero_digest, sizeof(zero_digest));
    p4_game_save_sha256_update(
        &hash, data + SAVE_OBJECT_SHA256 + P4_GAME_SAVE_SHA256_BYTES,
        SAVE_AUTH_TAG - SAVE_OBJECT_SHA256 - P4_GAME_SAVE_SHA256_BYTES);
    p4_game_save_sha256_update(&hash, zero_digest, sizeof(zero_digest));
    p4_game_save_sha256_update(
        &hash, data + SAVE_AUTH_TAG + P4_GAME_SAVE_AUTH_TAG_BYTES,
        bytes - SAVE_AUTH_TAG - P4_GAME_SAVE_AUTH_TAG_BYTES);
    p4_game_save_sha256_finish(&hash, inner_digest);

    p4_game_save_sha256_init(&hash);
    p4_game_save_sha256_update(&hash, outer_pad, sizeof(outer_pad));
    p4_game_save_sha256_update(&hash, inner_digest, sizeof(inner_digest));
    p4_game_save_sha256_finish(&hash, digest);

    volatile uint8_t *clear = inner_pad;
    for (size_t index = 0U; index < sizeof(inner_pad); ++index) {
        clear[index] = 0U;
    }
    clear = outer_pad;
    for (size_t index = 0U; index < sizeof(outer_pad); ++index) {
        clear[index] = 0U;
    }
    clear = inner_digest;
    for (size_t index = 0U; index < sizeof(inner_digest); ++index) {
        clear[index] = 0U;
    }
}

p4_game_save_result_t p4_game_save_encode(
    const char *game_id,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t sequence,
    const uint8_t *payload,
    size_t payload_bytes,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_bytes)
{
    if (output_bytes != NULL) {
        *output_bytes = 0U;
    }
    if (output == NULL || output_bytes == NULL || payload == NULL) {
        return P4_GAME_SAVE_BAD_ARGUMENT;
    }
    if (!p4_game_save_game_id_valid(game_id) ||
        !p4_game_save_slot_id_valid(slot_id)) {
        return P4_GAME_SAVE_BAD_ID;
    }
    if (schema_version == 0U || sequence == 0U || payload_bytes == 0U ||
        payload_bytes > P4_GAME_SAVE_MAX_BYTES ||
        payload_bytes > SIZE_MAX - P4_GAME_SAVE_HEADER_BYTES) {
        return P4_GAME_SAVE_BAD_SIZE;
    }
    const size_t total_bytes = P4_GAME_SAVE_HEADER_BYTES + payload_bytes;
    if (output_capacity < total_bytes || total_bytes > UINT32_MAX) {
        return P4_GAME_SAVE_BAD_SIZE;
    }
    memset(output, 0, P4_GAME_SAVE_HEADER_BYTES);
    memcpy(output + SAVE_MAGIC, s_magic, sizeof(s_magic));
    write_u32(output + SAVE_HEADER_BYTES, P4_GAME_SAVE_HEADER_BYTES);
    write_u32(output + SAVE_TOTAL_BYTES, (uint32_t)total_bytes);
    write_u32(output + SAVE_PAYLOAD_OFFSET, P4_GAME_SAVE_HEADER_BYTES);
    write_u32(output + SAVE_PAYLOAD_BYTES, (uint32_t)payload_bytes);
    write_u32(output + SAVE_FORMAT_VERSION, P4_GAME_SAVE_FORMAT_VERSION);
    write_u32(output + SAVE_SCHEMA_VERSION, schema_version);
    write_u32(output + SAVE_SEQUENCE, sequence);
    memcpy(output + SAVE_GAME_ID, game_id, strlen(game_id));
    memcpy(output + SAVE_SLOT_ID, slot_id, strlen(slot_id));
    memmove(output + P4_GAME_SAVE_HEADER_BYTES, payload, payload_bytes);
    hash_bytes(output + P4_GAME_SAVE_HEADER_BYTES, payload_bytes,
               output + SAVE_PAYLOAD_SHA256);
    uint8_t object_digest[P4_GAME_SAVE_SHA256_BYTES];
    hash_object(output, total_bytes, object_digest);
    memcpy(output + SAVE_OBJECT_SHA256, object_digest,
           sizeof(object_digest));
    *output_bytes = total_bytes;
    return P4_GAME_SAVE_VALID;
}

p4_game_save_result_t p4_game_save_encode_authenticated(
    const p4_game_save_protection_t *protection,
    const char *game_id,
    const char *slot_id,
    uint32_t schema_version,
    uint32_t sequence,
    const uint8_t *payload,
    size_t payload_bytes,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_bytes)
{
    if (output_bytes != NULL) {
        *output_bytes = 0U;
    }
    if (!p4_game_save_protection_valid(protection) || output == NULL ||
        output_bytes == NULL || payload == NULL) {
        return P4_GAME_SAVE_BAD_ARGUMENT;
    }
    if (!p4_game_save_game_id_valid(game_id) ||
        !p4_game_save_slot_id_valid(slot_id)) {
        return P4_GAME_SAVE_BAD_ID;
    }
    if (schema_version == 0U || sequence == 0U || payload_bytes == 0U ||
        payload_bytes > P4_GAME_SAVE_MAX_BYTES ||
        payload_bytes > SIZE_MAX - P4_GAME_SAVE_HEADER_BYTES) {
        return P4_GAME_SAVE_BAD_SIZE;
    }
    const size_t total_bytes = P4_GAME_SAVE_HEADER_BYTES + payload_bytes;
    if (output_capacity < total_bytes || total_bytes > UINT32_MAX) {
        return P4_GAME_SAVE_BAD_SIZE;
    }

    memset(output, 0, P4_GAME_SAVE_HEADER_BYTES);
    memcpy(output + SAVE_MAGIC, s_auth_magic, sizeof(s_auth_magic));
    write_u32(output + SAVE_HEADER_BYTES, P4_GAME_SAVE_HEADER_BYTES);
    write_u32(output + SAVE_TOTAL_BYTES, (uint32_t)total_bytes);
    write_u32(output + SAVE_PAYLOAD_OFFSET, P4_GAME_SAVE_HEADER_BYTES);
    write_u32(output + SAVE_PAYLOAD_BYTES, (uint32_t)payload_bytes);
    write_u32(output + SAVE_FORMAT_VERSION,
              P4_GAME_SAVE_AUTH_FORMAT_VERSION);
    write_u32(output + SAVE_SCHEMA_VERSION, schema_version);
    write_u32(output + SAVE_SEQUENCE, sequence);
    write_u32(output + SAVE_FLAGS, SAVE_FLAG_AUTHENTICATED);
    memcpy(output + SAVE_GAME_ID, game_id, strlen(game_id));
    memcpy(output + SAVE_SLOT_ID, slot_id, strlen(slot_id));
    write_u32(output + SAVE_AUTH_SCHEME, SAVE_AUTH_SCHEME_HMAC_SHA256);
    write_u32(output + SAVE_AUTH_KEY_ID_BYTES,
              P4_GAME_SAVE_KEY_ID_BYTES);
    derive_key_id(protection, output + SAVE_AUTH_KEY_ID);
    memmove(output + P4_GAME_SAVE_HEADER_BYTES, payload, payload_bytes);
    hash_bytes(output + P4_GAME_SAVE_HEADER_BYTES, payload_bytes,
               output + SAVE_PAYLOAD_SHA256);
    hmac_object(protection, output, total_bytes, output + SAVE_AUTH_TAG);
    uint8_t object_digest[P4_GAME_SAVE_SHA256_BYTES];
    hash_object(output, total_bytes, object_digest);
    memcpy(output + SAVE_OBJECT_SHA256, object_digest,
           sizeof(object_digest));
    *output_bytes = total_bytes;
    return P4_GAME_SAVE_VALID;
}

p4_game_save_result_t p4_game_save_parse(
    const uint8_t *data,
    size_t data_bytes,
    const char *expected_game_id,
    const char *expected_slot_id,
    p4_game_save_info_t *out_info)
{
    if (data == NULL || out_info == NULL) {
        return P4_GAME_SAVE_BAD_ARGUMENT;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (data_bytes <= P4_GAME_SAVE_HEADER_BYTES ||
        data_bytes > P4_GAME_SAVE_MAX_FILE_BYTES) {
        return P4_GAME_SAVE_BAD_SIZE;
    }
    if (memcmp(data + SAVE_MAGIC, s_magic, sizeof(s_magic)) != 0) {
        return P4_GAME_SAVE_BAD_MAGIC;
    }
    if (read_u32(data + SAVE_FORMAT_VERSION) !=
            P4_GAME_SAVE_FORMAT_VERSION) {
        return P4_GAME_SAVE_BAD_VERSION;
    }
    const uint32_t total_bytes = read_u32(data + SAVE_TOTAL_BYTES);
    const uint32_t payload_offset = read_u32(data + SAVE_PAYLOAD_OFFSET);
    const uint32_t payload_bytes = read_u32(data + SAVE_PAYLOAD_BYTES);
    if (read_u32(data + SAVE_HEADER_BYTES) != P4_GAME_SAVE_HEADER_BYTES ||
        total_bytes != data_bytes ||
        payload_offset != P4_GAME_SAVE_HEADER_BYTES || payload_bytes == 0U ||
        payload_bytes > P4_GAME_SAVE_MAX_BYTES ||
        payload_bytes != total_bytes - payload_offset ||
        read_u32(data + SAVE_FLAGS) != 0U ||
        !all_zero(data + SAVE_RESERVED, SAVE_RESERVED_BYTES)) {
        return P4_GAME_SAVE_BAD_LAYOUT;
    }
    out_info->schema_version = read_u32(data + SAVE_SCHEMA_VERSION);
    out_info->sequence = read_u32(data + SAVE_SEQUENCE);
    if (out_info->schema_version == 0U || out_info->sequence == 0U) {
        return P4_GAME_SAVE_BAD_LAYOUT;
    }
    if (!copy_field(out_info->game_id, sizeof(out_info->game_id),
                    data + SAVE_GAME_ID, P4_GAME_ID_MAX_BYTES) ||
        !copy_field(out_info->slot_id, sizeof(out_info->slot_id),
                    data + SAVE_SLOT_ID, P4_GAME_SAVE_SLOT_ID_BYTES) ||
        !p4_game_save_game_id_valid(out_info->game_id) ||
        !p4_game_save_slot_id_valid(out_info->slot_id)) {
        return P4_GAME_SAVE_BAD_ID;
    }
    if ((expected_game_id != NULL &&
         (!p4_game_save_game_id_valid(expected_game_id) ||
          strcmp(out_info->game_id, expected_game_id) != 0)) ||
        (expected_slot_id != NULL &&
         (!p4_game_save_slot_id_valid(expected_slot_id) ||
          strcmp(out_info->slot_id, expected_slot_id) != 0))) {
        return P4_GAME_SAVE_BAD_ID;
    }
    uint8_t digest[P4_GAME_SAVE_SHA256_BYTES];
    hash_bytes(data + payload_offset, payload_bytes, digest);
    if (memcmp(digest, data + SAVE_PAYLOAD_SHA256, sizeof(digest)) != 0) {
        return P4_GAME_SAVE_BAD_DIGEST;
    }
    hash_object(data, data_bytes, digest);
    if (memcmp(digest, data + SAVE_OBJECT_SHA256, sizeof(digest)) != 0) {
        return P4_GAME_SAVE_BAD_DIGEST;
    }
    out_info->payload_offset = payload_offset;
    out_info->payload_bytes = payload_bytes;
    out_info->format_version = P4_GAME_SAVE_FORMAT_VERSION;
    out_info->authenticated = false;
    memcpy(out_info->payload_sha256, data + SAVE_PAYLOAD_SHA256,
           sizeof(out_info->payload_sha256));
    memcpy(out_info->object_sha256, data + SAVE_OBJECT_SHA256,
           sizeof(out_info->object_sha256));
    return P4_GAME_SAVE_VALID;
}

p4_game_save_result_t p4_game_save_parse_authenticated(
    const p4_game_save_protection_t *protection,
    const uint8_t *data,
    size_t data_bytes,
    const char *expected_game_id,
    const char *expected_slot_id,
    p4_game_save_info_t *out_info)
{
    if (!p4_game_save_protection_valid(protection) || data == NULL ||
        out_info == NULL) {
        return P4_GAME_SAVE_BAD_ARGUMENT;
    }
    if (data_bytes > P4_GAME_SAVE_HEADER_BYTES &&
        data_bytes <= P4_GAME_SAVE_MAX_FILE_BYTES &&
        memcmp(data + SAVE_MAGIC, s_magic, sizeof(s_magic)) == 0) {
        return p4_game_save_parse(
            data, data_bytes, expected_game_id, expected_slot_id, out_info);
    }

    memset(out_info, 0, sizeof(*out_info));
    if (data_bytes <= P4_GAME_SAVE_HEADER_BYTES ||
        data_bytes > P4_GAME_SAVE_MAX_FILE_BYTES) {
        return P4_GAME_SAVE_BAD_SIZE;
    }
    if (memcmp(data + SAVE_MAGIC, s_auth_magic, sizeof(s_auth_magic)) != 0) {
        return P4_GAME_SAVE_BAD_MAGIC;
    }
    if (read_u32(data + SAVE_FORMAT_VERSION) !=
            P4_GAME_SAVE_AUTH_FORMAT_VERSION) {
        return P4_GAME_SAVE_BAD_VERSION;
    }
    const uint32_t total_bytes = read_u32(data + SAVE_TOTAL_BYTES);
    const uint32_t payload_offset = read_u32(data + SAVE_PAYLOAD_OFFSET);
    const uint32_t payload_bytes = read_u32(data + SAVE_PAYLOAD_BYTES);
    if (read_u32(data + SAVE_HEADER_BYTES) != P4_GAME_SAVE_HEADER_BYTES ||
        total_bytes != data_bytes ||
        payload_offset != P4_GAME_SAVE_HEADER_BYTES || payload_bytes == 0U ||
        payload_bytes > P4_GAME_SAVE_MAX_BYTES ||
        payload_bytes != total_bytes - payload_offset ||
        read_u32(data + SAVE_FLAGS) != SAVE_FLAG_AUTHENTICATED ||
        read_u32(data + SAVE_AUTH_SCHEME) !=
            SAVE_AUTH_SCHEME_HMAC_SHA256 ||
        read_u32(data + SAVE_AUTH_KEY_ID_BYTES) !=
            P4_GAME_SAVE_KEY_ID_BYTES ||
        !all_zero(data + SAVE_AUTH_RESERVED, SAVE_AUTH_RESERVED_BYTES)) {
        return P4_GAME_SAVE_BAD_LAYOUT;
    }
    out_info->schema_version = read_u32(data + SAVE_SCHEMA_VERSION);
    out_info->sequence = read_u32(data + SAVE_SEQUENCE);
    if (out_info->schema_version == 0U || out_info->sequence == 0U) {
        return P4_GAME_SAVE_BAD_LAYOUT;
    }
    if (!copy_field(out_info->game_id, sizeof(out_info->game_id),
                    data + SAVE_GAME_ID, P4_GAME_ID_MAX_BYTES) ||
        !copy_field(out_info->slot_id, sizeof(out_info->slot_id),
                    data + SAVE_SLOT_ID, P4_GAME_SAVE_SLOT_ID_BYTES) ||
        !p4_game_save_game_id_valid(out_info->game_id) ||
        !p4_game_save_slot_id_valid(out_info->slot_id)) {
        return P4_GAME_SAVE_BAD_ID;
    }
    if ((expected_game_id != NULL &&
         (!p4_game_save_game_id_valid(expected_game_id) ||
          strcmp(out_info->game_id, expected_game_id) != 0)) ||
        (expected_slot_id != NULL &&
         (!p4_game_save_slot_id_valid(expected_slot_id) ||
          strcmp(out_info->slot_id, expected_slot_id) != 0))) {
        return P4_GAME_SAVE_BAD_ID;
    }

    uint8_t digest[P4_GAME_SAVE_SHA256_BYTES];
    hash_bytes(data + payload_offset, payload_bytes, digest);
    if (!constant_time_equal(
            digest, data + SAVE_PAYLOAD_SHA256, sizeof(digest))) {
        return P4_GAME_SAVE_BAD_DIGEST;
    }
    hash_object(data, data_bytes, digest);
    if (!constant_time_equal(
            digest, data + SAVE_OBJECT_SHA256, sizeof(digest))) {
        return P4_GAME_SAVE_BAD_DIGEST;
    }
    uint8_t key_id[P4_GAME_SAVE_KEY_ID_BYTES];
    derive_key_id(protection, key_id);
    if (!constant_time_equal(
            key_id, data + SAVE_AUTH_KEY_ID, sizeof(key_id))) {
        return P4_GAME_SAVE_BAD_AUTH;
    }
    hmac_object(protection, data, data_bytes, digest);
    if (!constant_time_equal(digest, data + SAVE_AUTH_TAG,
                             P4_GAME_SAVE_AUTH_TAG_BYTES)) {
        return P4_GAME_SAVE_BAD_AUTH;
    }

    out_info->payload_offset = payload_offset;
    out_info->payload_bytes = payload_bytes;
    out_info->format_version = P4_GAME_SAVE_AUTH_FORMAT_VERSION;
    out_info->authenticated = true;
    memcpy(out_info->payload_sha256, data + SAVE_PAYLOAD_SHA256,
           sizeof(out_info->payload_sha256));
    memcpy(out_info->object_sha256, data + SAVE_OBJECT_SHA256,
           sizeof(out_info->object_sha256));
    memcpy(out_info->key_id, data + SAVE_AUTH_KEY_ID,
           sizeof(out_info->key_id));
    memcpy(out_info->auth_tag, data + SAVE_AUTH_TAG,
           sizeof(out_info->auth_tag));
    return P4_GAME_SAVE_VALID;
}

const char *p4_game_save_result_name(p4_game_save_result_t result)
{
    switch (result) {
    case P4_GAME_SAVE_VALID: return "valid";
    case P4_GAME_SAVE_BAD_ARGUMENT: return "bad-argument";
    case P4_GAME_SAVE_BAD_SIZE: return "bad-size";
    case P4_GAME_SAVE_BAD_MAGIC: return "bad-magic";
    case P4_GAME_SAVE_BAD_VERSION: return "bad-version";
    case P4_GAME_SAVE_BAD_LAYOUT: return "bad-layout";
    case P4_GAME_SAVE_BAD_ID: return "bad-id";
    case P4_GAME_SAVE_BAD_DIGEST: return "bad-digest";
    case P4_GAME_SAVE_BAD_AUTH: return "bad-auth";
    default: return "unknown";
    }
}
