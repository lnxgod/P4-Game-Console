// SPDX-License-Identifier: LicenseRef-LORD-Permission

#ifndef P4_LORD_REALM_NET_IMPL_H
#define P4_LORD_REALM_NET_IMPL_H

enum {
    LORD_P4RM_VERSION = 1,
    LORD_P4RM_HEADER_BYTES = 16,
    LORD_P4RM_PAYLOAD_BYTES =
        P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES - LORD_P4RM_HEADER_BYTES,
    LORD_P4RM_MAX_CHUNKS =
        (LORD_SYNC_MAX_BYTES + LORD_P4RM_PAYLOAD_BYTES - 1) /
            LORD_P4RM_PAYLOAD_BYTES,
    LORD_P4RM_BEGIN_INDEX = UINT16_MAX,
    LORD_P4RM_PROTOCOL = 0x4c52,
    LORD_P4RM_RETRY_MS = 1000,
};

typedef enum {
    LORD_P4RM_HELLO = 1,
    LORD_P4RM_WELCOME = 2,
    LORD_P4RM_DOWNLOAD_BEGIN = 3,
    LORD_P4RM_DOWNLOAD_CHUNK = 4,
    LORD_P4RM_ACK = 5,
    LORD_P4RM_UPLOAD_BEGIN = 6,
    LORD_P4RM_UPLOAD_CHUNK = 7,
    LORD_P4RM_COMMIT_RESULT = 8,
    LORD_P4RM_CLOCK = 9,
    LORD_P4RM_ERROR = 10,
    LORD_P4RM_PROFILE = 11,
    LORD_P4RM_DIRECTORY_SUMMARY = 12,
    LORD_P4RM_DIRECTORY_STATS = 13,
    LORD_P4RM_PROFILE_STATS = 14,
} lord_p4rm_kind_t;

typedef enum {
    LORD_REALM_NET_OFFLINE = 0,
    LORD_REALM_NET_SEND_HELLO,
    LORD_REALM_NET_WAIT_WELCOME,
    LORD_REALM_NET_WAIT_DOWNLOAD,
    LORD_REALM_NET_DOWNLOADING,
    LORD_REALM_NET_READY,
    LORD_REALM_NET_UPLOAD_BEGIN,
    LORD_REALM_NET_UPLOAD_CHUNK,
    LORD_REALM_NET_UPLOAD_COMMIT,
    LORD_REALM_NET_CONFLICT,
    LORD_REALM_NET_ERROR,
} lord_realm_net_state_t;

enum {
    LORD_P4RM_WELCOME_HAS_SNAPSHOT = 1U << 0U,
    LORD_P4RM_WELCOME_ROLLOVER_PENDING = 1U << 1U,
    LORD_P4RM_COMMIT_OK = 0,
    LORD_P4RM_COMMIT_CONFLICT = 1,
};

typedef struct {
    lord_p4rm_kind_t kind;
    uint32_t transaction_id;
    uint16_t chunk_index;
    uint16_t chunk_count;
    const uint8_t *payload;
    size_t payload_bytes;
} lord_p4rm_message_t;

typedef struct {
    lord_realm_net_state_t state;
    uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES];
    uint8_t record[LORD_SYNC_MAX_BYTES];
    size_t record_bytes;
    size_t expected_record_bytes;
    uint32_t expected_record_crc;
    uint32_t transaction_id;
    uint32_t next_transaction_id;
    uint32_t server_revision;
    uint32_t download_revision;
    uint32_t committed_save_sequence;
    uint32_t queued_save_sequence;
    uint32_t profile_save_sequence;
    uint32_t profile_pending_save_sequence;
    uint32_t profile_transaction;
    uint32_t profile_chompcoin;
    uint32_t retry_elapsed_ms;
    uint32_t clock_elapsed_ms;
    uint16_t chunk_index;
    uint16_t chunk_count;
    uint64_t session_seed;
    uint64_t realm_day_id;
    uint64_t operation_nonce;
    uint32_t seconds_remaining;
    uint8_t directory_actor_ids[LORD_REALM_PLAYER_COUNT]
        [LORD_SYNC_ACTOR_ID_BYTES];
    bool connected;
    bool rollover_pending;
    bool profile_stats_pending;
} lord_realm_net_runtime_t;

static lord_realm_net_runtime_t s_lord_realm_net;

static uint32_t p4rm_next_transaction(void)
{
    uint32_t value = s_lord_realm_net.next_transaction_id;
    if (value == 0U) {
        value = UINT32_C(1);
    }
    s_lord_realm_net.next_transaction_id = value + 1U;
    if (s_lord_realm_net.next_transaction_id == 0U) {
        s_lord_realm_net.next_transaction_id = 1U;
    }
    return value;
}

static bool p4rm_kind_valid(uint8_t kind)
{
    return kind >= (uint8_t)LORD_P4RM_HELLO &&
        kind <= (uint8_t)LORD_P4RM_PROFILE_STATS;
}

static size_t p4rm_encode(
    lord_p4rm_kind_t kind,
    uint32_t transaction_id,
    uint16_t chunk_index,
    uint16_t chunk_count,
    const uint8_t *payload,
    size_t payload_bytes,
    uint8_t output[P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES])
{
    if (!p4rm_kind_valid((uint8_t)kind) || transaction_id == 0U ||
        chunk_count > LORD_P4RM_MAX_CHUNKS ||
        payload_bytes > LORD_P4RM_PAYLOAD_BYTES ||
        (payload == NULL && payload_bytes != 0U) || output == NULL) {
        return 0U;
    }
    output[0] = (uint8_t)'P';
    output[1] = (uint8_t)'4';
    output[2] = (uint8_t)'R';
    output[3] = (uint8_t)'M';
    output[4] = LORD_P4RM_VERSION;
    output[5] = (uint8_t)kind;
    output[6] = 0U;
    output[7] = LORD_P4RM_HEADER_BYTES;
    save_store_u32(output, 8U, transaction_id);
    save_store_u16(output, 12U, chunk_index);
    save_store_u16(output, 14U, chunk_count);
    if (payload_bytes != 0U) {
        memcpy(output + LORD_P4RM_HEADER_BYTES, payload, payload_bytes);
    }
    return LORD_P4RM_HEADER_BYTES + payload_bytes;
}

static bool p4rm_decode(
    const uint8_t *bytes,
    size_t bytes_length,
    lord_p4rm_message_t *message)
{
    if (bytes == NULL || message == NULL ||
        bytes_length < LORD_P4RM_HEADER_BYTES ||
        bytes_length > P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES ||
        bytes[0] != (uint8_t)'P' || bytes[1] != (uint8_t)'4' ||
        bytes[2] != (uint8_t)'R' || bytes[3] != (uint8_t)'M' ||
        bytes[4] != LORD_P4RM_VERSION || !p4rm_kind_valid(bytes[5]) ||
        bytes[6] != 0U || bytes[7] != LORD_P4RM_HEADER_BYTES) {
        return false;
    }
    const uint32_t transaction_id = sync_load_u32(bytes, 8U);
    const uint16_t chunk_count = sync_load_u16(bytes, 14U);
    if (transaction_id == 0U || chunk_count > LORD_P4RM_MAX_CHUNKS) {
        return false;
    }
    *message = (lord_p4rm_message_t){
        .kind = (lord_p4rm_kind_t)bytes[5],
        .transaction_id = transaction_id,
        .chunk_index = sync_load_u16(bytes, 12U),
        .chunk_count = chunk_count,
        .payload = bytes + LORD_P4RM_HEADER_BYTES,
        .payload_bytes = bytes_length - LORD_P4RM_HEADER_BYTES,
    };
    return true;
}

static bool p4rm_send(
    p4_game_context_t *context,
    lord_p4rm_kind_t kind,
    uint32_t transaction_id,
    uint16_t chunk_index,
    uint16_t chunk_count,
    const uint8_t *payload,
    size_t payload_bytes)
{
    uint8_t message[P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES];
    const size_t message_bytes = p4rm_encode(
        kind, transaction_id, chunk_index, chunk_count,
        payload, payload_bytes, message);
    return message_bytes != 0U &&
        p4_game_multiplayer_send(context, message, message_bytes);
}

static bool p4rm_send_ack(
    p4_game_context_t *context,
    const lord_p4rm_message_t *message)
{
    const uint8_t kind = (uint8_t)message->kind;
    return p4rm_send(
        context, LORD_P4RM_ACK, message->transaction_id,
        message->chunk_index, message->chunk_count, &kind, 1U);
}

static void p4rm_send_hello(p4_game_context_t *context)
{
    const uint32_t transaction = p4rm_next_transaction();
    if (p4rm_send(
            context, LORD_P4RM_HELLO, transaction, 0U, 0U,
            NULL, 0U)) {
        s_lord_realm_net.transaction_id = transaction;
        s_lord_realm_net.state = LORD_REALM_NET_WAIT_WELCOME;
        s_lord_realm_net.retry_elapsed_ms = 0U;
    }
}

static bool p4rm_actor_valid(const uint8_t actor[LORD_SYNC_ACTOR_ID_BYTES])
{
    uint8_t combined = 0U;
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        combined = (uint8_t)(combined | actor[index]);
    }
    return combined != 0U;
}

static void p4rm_handle_welcome(const lord_p4rm_message_t *message)
{
    if (s_lord_realm_net.state != LORD_REALM_NET_WAIT_WELCOME ||
        message->transaction_id != s_lord_realm_net.transaction_id ||
        message->payload_bytes != 36U ||
        !p4rm_actor_valid(message->payload) ||
        message->payload[33] != 0U || message->payload[34] != 0U ||
        message->payload[35] != 0U) {
        return;
    }
    const uint8_t flags = message->payload[32];
    if ((flags & (uint8_t)~(LORD_P4RM_WELCOME_HAS_SNAPSHOT |
                            LORD_P4RM_WELCOME_ROLLOVER_PENDING)) != 0U) {
        s_lord_realm_net.state = LORD_REALM_NET_ERROR;
        return;
    }
    memcpy(s_lord_realm_net.actor_id, message->payload,
           LORD_SYNC_ACTOR_ID_BYTES);
    s_lord_realm_net.server_revision = sync_load_u32(
        message->payload, 16U);
    s_lord_realm_net.realm_day_id = sync_load_u64(
        message->payload, 20U);
    s_lord_realm_net.seconds_remaining = sync_load_u32(
        message->payload, 28U);
    s_lord_realm_net.clock_elapsed_ms = 0U;
    if (s_lord_realm_net.realm_day_id == 0U ||
        s_lord_realm_net.seconds_remaining == 0U ||
        s_lord_realm_net.seconds_remaining > 3600U) {
        s_lord_realm_net.state = LORD_REALM_NET_ERROR;
        return;
    }
    s_lord_realm_net.rollover_pending =
        (flags & LORD_P4RM_WELCOME_ROLLOVER_PENDING) != 0U;
    s_lord_realm_net.state =
        (flags & LORD_P4RM_WELCOME_HAS_SNAPSHOT) != 0U
            ? LORD_REALM_NET_WAIT_DOWNLOAD : LORD_REALM_NET_READY;
}

static void p4rm_handle_download_begin(
    p4_game_context_t *context,
    const lord_p4rm_message_t *message)
{
    if (s_lord_realm_net.state != LORD_REALM_NET_WAIT_DOWNLOAD ||
        message->chunk_index != LORD_P4RM_BEGIN_INDEX ||
        message->chunk_count == 0U ||
        message->chunk_count > LORD_P4RM_MAX_CHUNKS ||
        message->payload_bytes != 12U) {
        return;
    }
    const size_t total_bytes = sync_load_u32(message->payload, 0U);
    const uint32_t crc = sync_load_u32(message->payload, 4U);
    const uint32_t revision = sync_load_u32(message->payload, 8U);
    const size_t expected_chunks =
        (total_bytes + LORD_P4RM_PAYLOAD_BYTES - 1U) /
            LORD_P4RM_PAYLOAD_BYTES;
    if (total_bytes < 52U || total_bytes > LORD_SYNC_MAX_BYTES ||
        crc == 0U || revision == 0U ||
        expected_chunks != message->chunk_count) {
        s_lord_realm_net.state = LORD_REALM_NET_ERROR;
        return;
    }
    s_lord_realm_net.transaction_id = message->transaction_id;
    s_lord_realm_net.expected_record_bytes = total_bytes;
    s_lord_realm_net.expected_record_crc = crc;
    s_lord_realm_net.download_revision = revision;
    s_lord_realm_net.record_bytes = 0U;
    s_lord_realm_net.chunk_index = 0U;
    s_lord_realm_net.chunk_count = message->chunk_count;
    if (p4rm_send_ack(context, message)) {
        s_lord_realm_net.state = LORD_REALM_NET_DOWNLOADING;
    }
}

static void p4rm_finish_download(lord_state_t *state)
{
    lord_sync_metadata_t metadata;
    if (s_lord_realm_net.record_bytes !=
            s_lord_realm_net.expected_record_bytes ||
        save_crc32(s_lord_realm_net.record,
                   s_lord_realm_net.record_bytes) !=
            s_lord_realm_net.expected_record_crc ||
        !lord_sync_decode(
            state, &metadata, s_lord_realm_net.actor_id, 1U,
            s_lord_realm_net.record, s_lord_realm_net.record_bytes)) {
        s_lord_realm_net.state = LORD_REALM_NET_ERROR;
        return;
    }
    s_lord_realm_net.server_revision =
        s_lord_realm_net.download_revision;
    s_lord_realm_net.committed_save_sequence = state->save_sequence;
    if (s_lord_realm_net.rollover_pending && state->player.day != 0U) {
        reset_hourly_realm_day(state);
        s_lord_realm_net.rollover_pending = false;
    }
    s_lord_realm_net.state = LORD_REALM_NET_READY;
}

static void p4rm_handle_download_chunk(
    p4_game_context_t *context,
    lord_state_t *state,
    const lord_p4rm_message_t *message)
{
    if (s_lord_realm_net.state != LORD_REALM_NET_DOWNLOADING ||
        message->transaction_id != s_lord_realm_net.transaction_id ||
        message->chunk_count != s_lord_realm_net.chunk_count ||
        message->chunk_index != s_lord_realm_net.chunk_index ||
        message->payload_bytes == 0U ||
        message->payload_bytes > LORD_P4RM_PAYLOAD_BYTES ||
        message->payload_bytes >
            s_lord_realm_net.expected_record_bytes -
                s_lord_realm_net.record_bytes) {
        return;
    }
    memcpy(s_lord_realm_net.record + s_lord_realm_net.record_bytes,
           message->payload, message->payload_bytes);
    s_lord_realm_net.record_bytes += message->payload_bytes;
    if (!p4rm_send_ack(context, message)) {
        s_lord_realm_net.record_bytes -= message->payload_bytes;
        return;
    }
    ++s_lord_realm_net.chunk_index;
    if (s_lord_realm_net.chunk_index == s_lord_realm_net.chunk_count) {
        p4rm_finish_download(state);
    }
}

static bool p4rm_send_upload_begin(p4_game_context_t *context)
{
    uint8_t payload[28];
    save_store_u32(payload, 0U, s_lord_realm_net.server_revision);
    save_store_u32(payload, 4U, (uint32_t)s_lord_realm_net.record_bytes);
    save_store_u32(payload, 8U,
                   save_crc32(s_lord_realm_net.record,
                              s_lord_realm_net.record_bytes));
    sync_store_u64(payload, 12U, s_lord_realm_net.operation_nonce);
    sync_store_u64(payload, 20U, s_lord_realm_net.realm_day_id);
    return p4rm_send(
        context, LORD_P4RM_UPLOAD_BEGIN,
        s_lord_realm_net.transaction_id, LORD_P4RM_BEGIN_INDEX,
        s_lord_realm_net.chunk_count, payload, sizeof(payload));
}

static bool p4rm_send_upload_chunk(p4_game_context_t *context)
{
    const size_t offset =
        (size_t)s_lord_realm_net.chunk_index * LORD_P4RM_PAYLOAD_BYTES;
    if (offset >= s_lord_realm_net.record_bytes) {
        return false;
    }
    size_t payload_bytes = s_lord_realm_net.record_bytes - offset;
    if (payload_bytes > LORD_P4RM_PAYLOAD_BYTES) {
        payload_bytes = LORD_P4RM_PAYLOAD_BYTES;
    }
    return p4rm_send(
        context, LORD_P4RM_UPLOAD_CHUNK,
        s_lord_realm_net.transaction_id,
        s_lord_realm_net.chunk_index,
        s_lord_realm_net.chunk_count,
        s_lord_realm_net.record + offset, payload_bytes);
}

static void p4rm_begin_upload(
    p4_game_context_t *context,
    lord_state_t *state)
{
    const uint32_t transaction = p4rm_next_transaction();
    uint64_t nonce = s_lord_realm_net.session_seed ^
        ((uint64_t)transaction << 24U) ^ state->save_sequence;
    nonce &= UINT64_C(0x7fffffffffffffff);
    if (nonce == 0U) {
        nonce = 1U;
    }
    const size_t record_bytes = lord_sync_encode(
        state, s_lord_realm_net.actor_id, nonce,
        s_lord_realm_net.record, sizeof(s_lord_realm_net.record));
    if (record_bytes == 0U) {
        return;
    }
    s_lord_realm_net.transaction_id = transaction;
    s_lord_realm_net.operation_nonce = nonce;
    s_lord_realm_net.record_bytes = record_bytes;
    s_lord_realm_net.chunk_count = (uint16_t)(
        (record_bytes + LORD_P4RM_PAYLOAD_BYTES - 1U) /
            LORD_P4RM_PAYLOAD_BYTES);
    s_lord_realm_net.chunk_index = 0U;
    s_lord_realm_net.queued_save_sequence = state->save_sequence;
    if (p4rm_send_upload_begin(context)) {
        s_lord_realm_net.state = LORD_REALM_NET_UPLOAD_BEGIN;
        s_lord_realm_net.retry_elapsed_ms = 0U;
    }
}

static void p4rm_handle_ack(
    p4_game_context_t *context,
    const lord_p4rm_message_t *message)
{
    if (message->transaction_id != s_lord_realm_net.transaction_id ||
        message->payload_bytes != 1U ||
        message->chunk_count != s_lord_realm_net.chunk_count) {
        return;
    }
    if (s_lord_realm_net.state == LORD_REALM_NET_UPLOAD_BEGIN &&
        message->chunk_index == LORD_P4RM_BEGIN_INDEX &&
        message->payload[0] == (uint8_t)LORD_P4RM_UPLOAD_BEGIN) {
        s_lord_realm_net.chunk_index = 0U;
        if (p4rm_send_upload_chunk(context)) {
            s_lord_realm_net.state = LORD_REALM_NET_UPLOAD_CHUNK;
            s_lord_realm_net.retry_elapsed_ms = 0U;
        }
        return;
    }
    if (s_lord_realm_net.state == LORD_REALM_NET_UPLOAD_CHUNK &&
        message->payload[0] == (uint8_t)LORD_P4RM_UPLOAD_CHUNK &&
        message->chunk_index == s_lord_realm_net.chunk_index) {
        ++s_lord_realm_net.chunk_index;
        s_lord_realm_net.retry_elapsed_ms = 0U;
        if (s_lord_realm_net.chunk_index == s_lord_realm_net.chunk_count) {
            s_lord_realm_net.state = LORD_REALM_NET_UPLOAD_COMMIT;
        } else {
            (void)p4rm_send_upload_chunk(context);
        }
    }
}

static void p4rm_handle_commit_result(const lord_p4rm_message_t *message)
{
    if (s_lord_realm_net.state != LORD_REALM_NET_UPLOAD_COMMIT ||
        message->transaction_id != s_lord_realm_net.transaction_id ||
        message->payload_bytes != 17U) {
        return;
    }
    const uint8_t status = message->payload[0];
    const uint32_t revision = sync_load_u32(message->payload, 1U);
    const uint64_t day_id = sync_load_u64(message->payload, 5U);
    const uint32_t remaining = sync_load_u32(message->payload, 13U);
    if (status == LORD_P4RM_COMMIT_OK && revision != 0U && day_id != 0U &&
        remaining != 0U && remaining <= 3600U) {
        s_lord_realm_net.server_revision = revision;
        s_lord_realm_net.realm_day_id = day_id;
        s_lord_realm_net.seconds_remaining = remaining;
        s_lord_realm_net.clock_elapsed_ms = 0U;
        s_lord_realm_net.committed_save_sequence =
            s_lord_realm_net.queued_save_sequence;
        s_lord_realm_net.state = LORD_REALM_NET_READY;
    } else if (status == LORD_P4RM_COMMIT_CONFLICT) {
        s_lord_realm_net.state = LORD_REALM_NET_CONFLICT;
    } else {
        s_lord_realm_net.state = LORD_REALM_NET_ERROR;
    }
}

static void p4rm_handle_clock(
    lord_state_t *state,
    const lord_p4rm_message_t *message)
{
    if (message->payload_bytes != 16U || message->payload[13] != 0U ||
        message->payload[14] != 0U || message->payload[15] != 0U) {
        return;
    }
    const uint64_t day_id = sync_load_u64(message->payload, 0U);
    const uint32_t remaining = sync_load_u32(message->payload, 8U);
    const uint8_t pending = message->payload[12];
    if (day_id == 0U || remaining == 0U || remaining > 3600U ||
        pending > 1U) {
        return;
    }
    const bool newer_day = day_id > s_lord_realm_net.realm_day_id;
    s_lord_realm_net.realm_day_id = day_id;
    s_lord_realm_net.seconds_remaining = remaining;
    s_lord_realm_net.clock_elapsed_ms = 0U;
    if (pending != 0U && newer_day && state->player.day != 0U &&
        s_lord_realm_net.state == LORD_REALM_NET_READY) {
        reset_hourly_realm_day(state);
    }
}

static void p4rm_handle_directory_summary(
    lord_state_t *state,
    const lord_p4rm_message_t *message)
{
    if (message->chunk_count > LORD_REALM_PLAYER_COUNT ||
        message->chunk_index >= LORD_REALM_PLAYER_COUNT) {
        return;
    }
    if (message->chunk_count == 0U && message->payload_bytes == 0U) {
        memset(s_lord_realm_net.directory_actor_ids, 0,
               sizeof(s_lord_realm_net.directory_actor_ids));
        return;
    }
    if (message->payload_bytes != 44U ||
        message->chunk_index >= message->chunk_count ||
        !p4rm_actor_valid(message->payload)) {
        return;
    }
    const size_t slot = message->chunk_index;
    const uint8_t *const name = message->payload + 16U;
    bool terminated = false;
    size_t name_bytes = 0U;
    for (size_t index = 0U; index < LORD_NAME_BYTES; ++index) {
        const uint8_t value = name[index];
        if (terminated) {
            if (value != 0U) {
                return;
            }
        } else if (value == 0U) {
            terminated = true;
        } else if (value < 0x20U || value > 0x7eU) {
            return;
        } else {
            ++name_bytes;
        }
    }
    const uint8_t hero_style = message->payload[36];
    const uint8_t hero_class = message->payload[37];
    const uint8_t level = message->payload[38];
    const uint8_t flags = message->payload[39];
    if (!terminated || name_bytes < 3U ||
        hero_style > (uint8_t)LORD_HERO_STYLE_HEROINE ||
        hero_class > (uint8_t)LORD_CLASS_THIEF || level == 0U ||
        level > LORD_MAX_LEVEL || (flags & (uint8_t)~0x07U) != 0U) {
        return;
    }
    lord_realm_player_t *const player = &state->realm[slot];
    memset(player->name, 0, sizeof(player->name));
    memcpy(player->name, name, name_bytes);
    player->hero_style = (lord_hero_style_t)hero_style;
    player->hero_class = (lord_class_t)hero_class;
    player->level = level;
    player->alive = (flags & 0x01U) != 0U;
    player->at_inn = (flags & 0x02U) != 0U;
    player->pvp_wins = sync_load_u16(message->payload, 40U);
    player->pvp_losses = sync_load_u16(message->payload, 42U);
    text_copy(player->saying, sizeof(player->saying),
              (flags & 0x04U) != 0U ?
                  "ONLINE THROUGH THE MAC HUB" : "OFFLINE FROM THE MAC HUB");
    memcpy(s_lord_realm_net.directory_actor_ids[slot], message->payload,
           LORD_SYNC_ACTOR_ID_BYTES);
    for (size_t index = message->chunk_count;
         index < LORD_REALM_PLAYER_COUNT; ++index) {
        memset(s_lord_realm_net.directory_actor_ids[index], 0,
               LORD_SYNC_ACTOR_ID_BYTES);
    }
}

static void p4rm_handle_directory_stats(
    lord_state_t *state,
    const lord_p4rm_message_t *message)
{
    if (message->payload_bytes != 40U ||
        message->chunk_count == 0U ||
        message->chunk_count > LORD_REALM_PLAYER_COUNT ||
        message->chunk_index >= message->chunk_count ||
        memcmp(s_lord_realm_net.directory_actor_ids[message->chunk_index],
               message->payload, LORD_SYNC_ACTOR_ID_BYTES) != 0) {
        return;
    }
    const int32_t hit_points = (int32_t)sync_load_u32(message->payload, 16U);
    const int32_t max_hit_points =
        (int32_t)sync_load_u32(message->payload, 20U);
    const int32_t strength = (int32_t)sync_load_u32(message->payload, 24U);
    const int32_t defense = (int32_t)sync_load_u32(message->payload, 28U);
    if (max_hit_points <= 0 || hit_points < 0 ||
        hit_points > max_hit_points || strength <= 0 || defense < 0) {
        return;
    }
    lord_realm_player_t *const player =
        &state->realm[message->chunk_index];
    player->hit_points = hit_points;
    player->max_hit_points = max_hit_points;
    player->strength = strength;
    player->defense = defense;
    player->experience = sync_load_u32(message->payload, 32U);
    player->gold = sync_load_u32(message->payload, 36U);
}

static bool p4rm_send_profile(
    p4_game_context_t *context,
    const lord_state_t *state)
{
    uint8_t payload[48] = {0};
    const size_t name_bytes = text_length(
        state->player.name, sizeof(state->player.name));
    if (state->player.day == 0U || name_bytes < 3U ||
        name_bytes >= sizeof(state->player.name)) {
        return false;
    }
    memcpy(payload, state->player.name, name_bytes);
    payload[20] = (uint8_t)state->player.hero_style;
    payload[21] = (uint8_t)state->player.hero_class;
    payload[22] = state->player.level;
    payload[23] = (uint8_t)(state->player.hit_points > 0 ? 0x01U : 0U);
    if (state->screen == LORD_SCREEN_INN ||
        state->screen == LORD_SCREEN_BARTENDER ||
        state->screen == LORD_SCREEN_CONVERSE ||
        state->screen == LORD_SCREEN_SETH ||
        state->screen == LORD_SCREEN_VIOLET ||
        state->screen == LORD_SCREEN_DRAGON_DICE) {
        payload[23] |= 0x02U;
    }
    save_store_u32(payload, 24U, (uint32_t)state->player.hit_points);
    save_store_u32(payload, 28U, (uint32_t)state->player.max_hit_points);
    save_store_u32(payload, 32U, (uint32_t)state->player.strength);
    save_store_u32(payload, 36U, (uint32_t)state->player.defense);
    save_store_u16(payload, 40U, state->player.pvp_wins);
    save_store_u16(payload, 42U, state->player.pvp_losses);
    save_store_u32(payload, 44U, state->player.experience);
    const uint32_t transaction = p4rm_next_transaction();
    if (!p4rm_send(
            context, LORD_P4RM_PROFILE, transaction,
            0U, 0U, payload, sizeof(payload))) {
        return false;
    }
    s_lord_realm_net.profile_transaction = transaction;
    s_lord_realm_net.profile_pending_save_sequence = state->save_sequence;
    s_lord_realm_net.profile_chompcoin = state->player.gold;
    s_lord_realm_net.profile_stats_pending = true;
    return true;
}

static bool p4rm_send_profile_stats(p4_game_context_t *context)
{
    uint8_t payload[4];
    save_store_u32(payload, 0U, s_lord_realm_net.profile_chompcoin);
    if (!p4rm_send(
            context, LORD_P4RM_PROFILE_STATS,
            s_lord_realm_net.profile_transaction,
            0U, 0U, payload, sizeof(payload))) {
        return false;
    }
    s_lord_realm_net.profile_save_sequence =
        s_lord_realm_net.profile_pending_save_sequence;
    s_lord_realm_net.profile_stats_pending = false;
    return true;
}

static void p4rm_receive_messages(
    p4_game_context_t *context,
    lord_state_t *state)
{
    for (unsigned received = 0U; received < 8U; ++received) {
        p4_game_multiplayer_message_t game_message;
        if (!p4_game_multiplayer_receive(context, &game_message)) {
            break;
        }
        lord_p4rm_message_t message;
        if (!p4rm_decode(
                game_message.data, game_message.bytes, &message)) {
            continue;
        }
        switch (message.kind) {
        case LORD_P4RM_WELCOME:
            p4rm_handle_welcome(&message);
            break;
        case LORD_P4RM_DOWNLOAD_BEGIN:
            p4rm_handle_download_begin(context, &message);
            break;
        case LORD_P4RM_DOWNLOAD_CHUNK:
            p4rm_handle_download_chunk(context, state, &message);
            break;
        case LORD_P4RM_ACK:
            p4rm_handle_ack(context, &message);
            break;
        case LORD_P4RM_COMMIT_RESULT:
            p4rm_handle_commit_result(&message);
            break;
        case LORD_P4RM_CLOCK:
            p4rm_handle_clock(state, &message);
            break;
        case LORD_P4RM_DIRECTORY_SUMMARY:
            p4rm_handle_directory_summary(state, &message);
            break;
        case LORD_P4RM_DIRECTORY_STATS:
            p4rm_handle_directory_stats(state, &message);
            break;
        case LORD_P4RM_ERROR:
            s_lord_realm_net.state = LORD_REALM_NET_ERROR;
            break;
        case LORD_P4RM_HELLO:
        case LORD_P4RM_UPLOAD_BEGIN:
        case LORD_P4RM_UPLOAD_CHUNK:
        case LORD_P4RM_PROFILE:
        case LORD_P4RM_PROFILE_STATS:
            break;
        }
    }
}

static void lord_realm_net_start(p4_game_context_t *context)
{
    memset(&s_lord_realm_net, 0, sizeof(s_lord_realm_net));
    p4_game_multiplayer_status_t status;
    if (!p4_game_multiplayer_read_status(context, &status) ||
        status.state != P4_GAME_MULTIPLAYER_CONNECTED ||
        status.player_count != 2U || status.session_seed == 0U) {
        return;
    }
    s_lord_realm_net.connected = true;
    s_lord_realm_net.session_seed = status.session_seed;
    s_lord_realm_net.next_transaction_id =
        (uint32_t)status.session_seed ^ (uint32_t)(status.session_seed >> 32U);
    if (s_lord_realm_net.next_transaction_id == 0U) {
        s_lord_realm_net.next_transaction_id = 1U;
    }
    s_lord_realm_net.state = LORD_REALM_NET_SEND_HELLO;
}

static void lord_realm_net_poll(
    p4_game_context_t *context,
    lord_state_t *state,
    uint32_t elapsed_ms)
{
    if (!s_lord_realm_net.connected) {
        return;
    }
    p4_game_multiplayer_status_t status;
    if (!p4_game_multiplayer_read_status(context, &status) ||
        status.state != P4_GAME_MULTIPLAYER_CONNECTED) {
        s_lord_realm_net.state = LORD_REALM_NET_OFFLINE;
        s_lord_realm_net.connected = false;
        return;
    }
    p4rm_receive_messages(context, state);
    if (s_lord_realm_net.seconds_remaining != 0U) {
        if (UINT32_MAX - s_lord_realm_net.clock_elapsed_ms < elapsed_ms) {
            s_lord_realm_net.clock_elapsed_ms = UINT32_MAX;
        } else {
            s_lord_realm_net.clock_elapsed_ms += elapsed_ms;
        }
        while (s_lord_realm_net.clock_elapsed_ms >= 1000U &&
               s_lord_realm_net.seconds_remaining > 1U) {
            s_lord_realm_net.clock_elapsed_ms -= 1000U;
            --s_lord_realm_net.seconds_remaining;
        }
    }
    if (s_lord_realm_net.state == LORD_REALM_NET_SEND_HELLO) {
        p4rm_send_hello(context);
        return;
    }
    if (UINT32_MAX - s_lord_realm_net.retry_elapsed_ms < elapsed_ms) {
        s_lord_realm_net.retry_elapsed_ms = UINT32_MAX;
    } else {
        s_lord_realm_net.retry_elapsed_ms += elapsed_ms;
    }
    if (s_lord_realm_net.retry_elapsed_ms >= LORD_P4RM_RETRY_MS) {
        if (s_lord_realm_net.state == LORD_REALM_NET_WAIT_WELCOME) {
            s_lord_realm_net.state = LORD_REALM_NET_SEND_HELLO;
        } else if (s_lord_realm_net.state == LORD_REALM_NET_UPLOAD_BEGIN &&
                   p4rm_send_upload_begin(context)) {
            s_lord_realm_net.retry_elapsed_ms = 0U;
        } else if (s_lord_realm_net.state == LORD_REALM_NET_UPLOAD_CHUNK &&
                   p4rm_send_upload_chunk(context)) {
            s_lord_realm_net.retry_elapsed_ms = 0U;
        }
    }
    if (s_lord_realm_net.state == LORD_REALM_NET_READY &&
        state->player.day != 0U && state->save_sequence !=
            s_lord_realm_net.committed_save_sequence) {
        p4rm_begin_upload(context, state);
    } else if (s_lord_realm_net.state == LORD_REALM_NET_READY &&
               s_lord_realm_net.profile_stats_pending) {
        (void)p4rm_send_profile_stats(context);
    } else if (s_lord_realm_net.state == LORD_REALM_NET_READY &&
               state->player.day != 0U && state->save_sequence !=
                   s_lord_realm_net.profile_save_sequence &&
               p4rm_send_profile(context, state)) {
        /* The matching ChompCoin packet is sent on the next update. */
    }
}

static bool lord_realm_net_blocks_gameplay(void)
{
    return s_lord_realm_net.connected &&
        s_lord_realm_net.state != LORD_REALM_NET_READY &&
        s_lord_realm_net.state != LORD_REALM_NET_UPLOAD_BEGIN &&
        s_lord_realm_net.state != LORD_REALM_NET_UPLOAD_CHUNK &&
        s_lord_realm_net.state != LORD_REALM_NET_UPLOAD_COMMIT;
}

static const char *lord_realm_net_label(void)
{
    switch (s_lord_realm_net.state) {
    case LORD_REALM_NET_READY:
    case LORD_REALM_NET_UPLOAD_BEGIN:
    case LORD_REALM_NET_UPLOAD_CHUNK:
    case LORD_REALM_NET_UPLOAD_COMMIT:
        return "MAC REALM";
    case LORD_REALM_NET_CONFLICT:
        return "SYNC CONFLICT";
    case LORD_REALM_NET_ERROR:
        return "SYNC ERROR";
    case LORD_REALM_NET_SEND_HELLO:
    case LORD_REALM_NET_WAIT_WELCOME:
    case LORD_REALM_NET_WAIT_DOWNLOAD:
    case LORD_REALM_NET_DOWNLOADING:
        return "SYNCING";
    case LORD_REALM_NET_OFFLINE:
    default:
        return NULL;
    }
}

#endif
