// SPDX-License-Identifier: LicenseRef-LORD-Permission

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lord_internal.h"
#include "p4/game.h"
#include "p4/input.h"

extern const p4_game_descriptor_t p4_lord_game;

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static void enter_town(lord_state_t *state, lord_class_t hero_class)
{
    lord_initialize(state, UINT32_C(0x12345678));
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_NAME);
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_TEXT_EDITOR);
    (void)strcpy(state->editor_text, "Test Hero");
    state->selection = 42U;
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_HERO_STYLE);
    state->selection = 1U;
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_CLASS);
    state->selection = (uint8_t)hero_class;
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_MESSAGE);
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_TOWN);
}

static void test_character_creation_and_core_menu(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_MYSTICAL);
    CHECK(strcmp(state.player.name, "Test Hero") == 0);
    CHECK(state.player.hero_style == LORD_HERO_STYLE_HEROINE);
    CHECK(state.player.hero_class == LORD_CLASS_MYSTICAL);
    CHECK(state.player.level == 1U);
    CHECK(state.player.hit_points == 20);
    CHECK(state.player.gold == 500U);
    CHECK(state.player.charm == 10U);
    CHECK(state.player.skill[LORD_CLASS_MYSTICAL] == 5U);
    CHECK(state.player.skill_uses[LORD_CLASS_MYSTICAL] == 3U);
    CHECK(lord_menu_count(&state) == 15U);
    lord_move_selection(&state, -1);
    CHECK(state.selection == 14U);
    lord_move_selection(&state, 1);
    CHECK(state.selection == 0U);

    state.screen = LORD_SCREEN_TEXT_EDITOR;
    state.selection = 0U;
    lord_move_selection(&state, -6);
    CHECK(state.selection == 38U);
    CHECK(strcmp(lord_keyboard_label(42U), "DONE") == 0);
    CHECK(strcmp(lord_keyboard_label(43U), "DEL") == 0);
}

static void test_shops_bank_and_transfer(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_DEATH_KNIGHT);
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.weapon == 1U);
    CHECK(state.player.gold == 300U);
    CHECK(state.player.strength == 15);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_BANK;
    state.selection = 0U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.gold == 0U);
    CHECK(state.player.bank == 300U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_BANK_TRANSFER);
    const uint32_t old_gold = state.realm[0].gold;
    state.selection = 0U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.bank == 200U);
    CHECK(state.realm[0].gold == old_gold + 100U);
}

static void test_forest_training_skills_and_dragon(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_THIEF);
    state.screen = LORD_SCREEN_FOREST;
    state.selection = 0U;
    state.rng_state = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_BATTLE);
    CHECK(state.battle_kind == LORD_BATTLE_FOREST);
    CHECK(state.player.forest_fights == LORD_FOREST_FIGHTS_PER_DAY - 1U);
    CHECK(state.enemy.name[0] != '\0');
    CHECK(state.enemy.weapon[0] != '\0');
    state.player.strength = 100000;
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(state.player.experience > 0U);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_TRAINING;
    state.selection = 0U;
    state.player.experience = 100U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.player.strength = 100000;
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(state.player.level == 2U);

    state.screen = LORD_SCREEN_BATTLE;
    state.battle_kind = LORD_BATTLE_FOREST;
    state.player.strength = 10;
    state.player.skill[LORD_CLASS_DEATH_KNIGHT] = 5U;
    state.player.skill_uses[LORD_CLASS_DEATH_KNIGHT] = 1U;
    state.enemy = (lord_enemy_t){
        .hit_points = 1000, .max_hit_points = 1000,
        .strength = 1, .death_text = "done",
    };
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_HIT);
    CHECK(state.player.skill_uses[LORD_CLASS_DEATH_KNIGHT] == 0U);

    state.player.level = LORD_MAX_LEVEL;
    state.player.strength = 100000;
    state.player.hit_points = 5000;
    state.player.max_hit_points = 5000;
    state.player.forest_fights = 1U;
    state.player.seen_dragon = false;
    state.screen = LORD_SCREEN_FOREST;
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.battle_kind == LORD_BATTLE_DRAGON);
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(state.player.dragon_kills == 1U);
}

static void test_mail_pvp_friendship_and_mentoring(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_DEATH_KNIGHT);
    CHECK(state.mail_count == 2U);
    CHECK(lord_mail_unread_count(&state) == 2U);

    state.screen = LORD_SCREEN_MAIL_COMPOSE;
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_TEXT_EDITOR);
    (void)strcpy(state.editor_text, "MEET ME AT THE INN");
    state.selection = 42U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.mail_count == 4U);
    CHECK(state.mail[2].outgoing);
    CHECK(strcmp(state.mail[2].body, "MEET ME AT THE INN") == 0);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_PLAYER_DETAIL;
    state.selected_player = 0U;
    state.selection = 0U;
    state.player.strength = 100000;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(!state.realm[0].alive);
    CHECK(state.player.pvp_wins == 1U);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_FRIENDSHIP_ACTION;
    state.selected_player = 1U;
    state.realm[1].trust = 70U;
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.partner_index == 1);
    CHECK(state.realm[1].teamed);
    CHECK(state.player.max_hit_points >= 25);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_FRIENDSHIP_ACTION;
    state.selected_player = 1U;
    state.friendship_actions = 1U;
    state.selection = 3U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.young_heroes_helped == 1U);
}

static void test_full_inn_and_igms(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_MYSTICAL);
    state.player.gold = 1000000U;
    state.player.gems = 10U;
    state.player.charm = 40U;

    state.screen = LORD_SCREEN_BARTENDER;
    state.selection = 0U;
    state.player.hit_points = 10;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.gold == 999990U);
    CHECK(state.player.hit_points == 12);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_SETH;
    state.selection = 4U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.npc_friend == 0);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_DRAGON_DICE;
    state.selection = 0U;
    const uint32_t chomp_before = state.player.gold;
    const uint16_t badges_before = state.player.friendship_badges;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.gold == chomp_before - 5U ||
          state.player.gold == chomp_before ||
          state.player.gold == chomp_before + 5U);
    CHECK(state.player.friendship_badges == badges_before + 1U);
    CHECK(state.player.high_spirits);
    CHECK(lord_menu_count(&state) == 2U);

    state.screen = LORD_SCREEN_INN;
    state.selection = 8U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    (void)strcpy(state.editor_text, "DRAGON AT MIDNIGHT");
    state.selection = 42U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(strcmp(state.announcement, "DRAGON AT MIDNIGHT") == 0);

    for (uint8_t module = 0U; module < LORD_IGM_COUNT; ++module) {
        if (state.screen == LORD_SCREEN_MESSAGE) {
            (void)lord_activate(&state);
        }
        state.screen = LORD_SCREEN_IGM_DETAIL;
        state.selected_igm = module;
        state.selection = 0U;
        state.player.gold = 1000000U;
        state.player.forest_fights = 10U;
        CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
        CHECK((state.igm_used_mask & (uint8_t)(1U << module)) != 0U);
    }
    CHECK(state.igm_used_mask == UINT8_C(0x7f));

    state.screen = LORD_SCREEN_INN;
    state.selection = 0U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.igm_used_mask == 0U);
    CHECK(state.player.forest_fights >= LORD_FOREST_FIGHTS_PER_DAY);
}

static void test_save_round_trip(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_THIEF);
    state.player.gold = 12345U;
    state.player.bank = 67890U;
    state.player.gems = 9U;
    state.player.young_heroes_helped = 3U;
    state.player.horse = true;
    state.player.fairy = true;
    state.player.amulet = true;
    state.player.skill[LORD_CLASS_MYSTICAL] = 17U;
    state.player.skill_uses[LORD_CLASS_MYSTICAL] = 4U;
    state.realm[1].trust = 55U;
    state.partner_index = 1;
    state.realm[1].teamed = true;
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        state.realm_actor_ids[1][index] = (uint8_t)(index + 1U);
        state.partner_actor_id[index] = (uint8_t)(index + 1U);
    }
    state.last_realm_event_id = UINT64_C(123456789);
    (void)strcpy(state.conversation, "THE DRAGON IS AWAKE");
    (void)strcpy(state.announcement, "MEET IN THE FOREST");
    state.save_sequence = 42U;

    uint8_t encoded[LORD_SAVE_MAX_BYTES];
    uint8_t duplicate[LORD_SAVE_MAX_BYTES];
    const size_t encoded_length = lord_save_encode(
        &state, encoded, sizeof(encoded));
    const size_t duplicate_length = lord_save_encode(
        &state, duplicate, sizeof(duplicate));
    CHECK(encoded_length > 2000U);
    CHECK(encoded_length < sizeof(encoded));
    CHECK(encoded_length == duplicate_length);
    CHECK(memcmp(encoded, duplicate, encoded_length) == 0);

    lord_state_t restored;
    CHECK(lord_save_decode(&restored, encoded, encoded_length));
    CHECK(restored.screen == LORD_SCREEN_TOWN);
    CHECK(strcmp(restored.player.name, "Test Hero") == 0);
    CHECK(restored.player.gold == 12345U);
    CHECK(restored.player.bank == 67890U);
    CHECK(restored.player.gems == 9U);
    CHECK(restored.player.young_heroes_helped == 3U);
    CHECK(restored.player.horse && restored.player.fairy);
    CHECK(restored.player.amulet);
    CHECK(restored.player.skill[LORD_CLASS_MYSTICAL] == 17U);
    CHECK(restored.partner_index == 1);
    CHECK(memcmp(restored.realm_actor_ids[1], state.realm_actor_ids[1],
                 LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(memcmp(restored.partner_actor_id, state.partner_actor_id,
                 LORD_SYNC_ACTOR_ID_BYTES) == 0);
    CHECK(restored.last_realm_event_id == UINT64_C(123456789));
    CHECK(strcmp(restored.conversation, "THE DRAGON IS AWAKE") == 0);
    CHECK(restored.save_sequence == 42U);
    CHECK(!restored.save_dirty);

    enum { LORD_V4_SAVE_EXTENSION_BYTES = 4 + 8 * 16 + 16 + 8 };
    const size_t v3_length = encoded_length - LORD_V4_SAVE_EXTENSION_BYTES;
    encoded[4] = (uint8_t)LORD_SAVE_MINIMUM_VERSION;
    encoded[5] = 0U;
    encoded[6] = (uint8_t)v3_length;
    encoded[7] = (uint8_t)(v3_length >> 8U);
    uint32_t v3_crc = UINT32_MAX;
    for (size_t index = 16U; index < v3_length; ++index) {
        v3_crc ^= encoded[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = UINT32_C(0) - (v3_crc & UINT32_C(1));
            v3_crc = (v3_crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    v3_crc = ~v3_crc;
    encoded[8] = (uint8_t)v3_crc;
    encoded[9] = (uint8_t)(v3_crc >> 8U);
    encoded[10] = (uint8_t)(v3_crc >> 16U);
    encoded[11] = (uint8_t)(v3_crc >> 24U);
    CHECK(lord_save_decode(&restored, encoded, v3_length));
    CHECK(restored.last_realm_event_id == 0U);
    CHECK(restored.partner_actor_id[0] == 0U);
    memcpy(encoded, duplicate, encoded_length);

    encoded[encoded_length - 1U] ^= UINT8_C(0x80);
    CHECK(!lord_save_decode(&restored, encoded, encoded_length));
    encoded[encoded_length - 1U] ^= UINT8_C(0x80);
    CHECK(!lord_save_decode(&restored, encoded, encoded_length - 1U));
    CHECK(lord_save_encode(&state, encoded, 16U) == 0U);
}

static void test_backend_sync_envelope(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_MYSTICAL);
    state.realm_revision = 27U;
    state.save_sequence = 44U;
    state.player.gold = 1234U;
    state.realm[2].trust = 73U;
    const uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES] = {
        0x10U, 0x32U, 0x54U, 0x76U, 0x98U, 0xbaU, 0xdcU, 0xfeU,
        0x01U, 0x23U, 0x45U, 0x67U, 0x89U, 0xabU, 0xcdU, 0xefU,
    };
    uint8_t record[LORD_SYNC_MAX_BYTES];
    const size_t record_bytes = lord_sync_encode(
        &state, actor_id, UINT64_C(0x1122334455667788),
        record, sizeof(record));
    CHECK(record_bytes > LORD_SAVE_MAX_BYTES / 2U);
    CHECK(record_bytes <= sizeof(record));

    lord_state_t restored;
    lord_sync_metadata_t metadata;
    CHECK(lord_sync_decode(&restored, &metadata, actor_id, 27U,
                           record, record_bytes));
    CHECK(metadata.realm_revision == 27U);
    CHECK(metadata.save_sequence == 44U);
    CHECK(metadata.operation_nonce == UINT64_C(0x1122334455667788));
    CHECK(restored.player.gold == 1234U);
    CHECK(restored.realm[2].trust == 73U);

    CHECK(!lord_sync_decode(&restored, &metadata, actor_id, 28U,
                            record, record_bytes));
    uint8_t wrong_actor[LORD_SYNC_ACTOR_ID_BYTES];
    (void)memcpy(wrong_actor, actor_id, sizeof(wrong_actor));
    wrong_actor[0] ^= UINT8_C(0xff);
    CHECK(!lord_sync_decode(&restored, &metadata, wrong_actor, 0U,
                            record, record_bytes));
    record[record_bytes - 1U] ^= UINT8_C(0x40);
    CHECK(!lord_sync_decode(&restored, &metadata, actor_id, 0U,
                            record, record_bytes));
    record[record_bytes - 1U] ^= UINT8_C(0x40);
    CHECK(lord_sync_encode(&state, actor_id, 0U,
                           record, sizeof(record)) == 0U);
}

typedef struct {
    uint8_t payload[LORD_SAVE_MAX_BYTES];
    size_t bytes;
    uint32_t expected_sequence;
    uint32_t sequence;
    p4_game_save_ticket_t ticket;
} save_mock_t;

enum {
    TEST_P4RM_HEADER_BYTES = 16,
    TEST_P4RM_WELCOME = 2,
    TEST_P4RM_ACK = 5,
    TEST_P4RM_UPLOAD_BEGIN = 6,
    TEST_P4RM_UPLOAD_CHUNK = 7,
    TEST_P4RM_COMMIT_RESULT = 8,
    TEST_P4RM_CLOCK = 9,
    TEST_P4RM_PROFILE = 11,
    TEST_P4RM_DIRECTORY_SUMMARY = 12,
    TEST_P4RM_DIRECTORY_STATS = 13,
    TEST_P4RM_PROFILE_STATS = 14,
    TEST_P4RM_ACTION_BEGIN = 15,
    TEST_P4RM_ACTION_BODY = 16,
    TEST_P4RM_ACTION_RESULT = 17,
    TEST_P4RM_EVENT_BEGIN = 18,
    TEST_P4RM_EVENT_BODY = 19,
    TEST_P4RM_EVENT_ACK = 20,
    TEST_P4RM_BEGIN_INDEX = UINT16_MAX,
};

typedef struct {
    p4_game_multiplayer_message_t incoming[8];
    size_t incoming_head;
    size_t incoming_count;
    uint8_t outgoing[P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES];
    size_t outgoing_bytes;
    uint32_t next_sequence;
} realm_mock_t;

static void test_store_u16(uint8_t *bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
}

static void test_store_u32(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
    bytes[offset + 2U] = (uint8_t)(value >> 16U);
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

static void test_store_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    test_store_u32(bytes, offset, (uint32_t)value);
    test_store_u32(bytes, offset + 4U, (uint32_t)(value >> 32U));
}

static uint16_t test_load_u16(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)((uint16_t)bytes[offset] |
        (uint16_t)((uint16_t)bytes[offset + 1U] << 8U));
}

static uint32_t test_load_u32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
        (uint32_t)bytes[offset + 1U] << 8U |
        (uint32_t)bytes[offset + 2U] << 16U |
        (uint32_t)bytes[offset + 3U] << 24U;
}

static bool realm_mock_status(
    void *context, p4_game_multiplayer_status_t *status_out)
{
    (void)context;
    *status_out = (p4_game_multiplayer_status_t){
        .generation = 1U,
        .session_seed = UINT64_C(0x123456789abcdef0),
        .state = P4_GAME_MULTIPLAYER_CONNECTED,
        .role = P4_GAME_MULTIPLAYER_ROLE_HOST,
        .local_player_slot = 0U,
        .player_count = 2U,
    };
    return true;
}

static bool realm_mock_send(
    void *context, const uint8_t *data, size_t data_bytes)
{
    realm_mock_t *const mock = context;
    if (mock->outgoing_bytes != 0U || data == NULL || data_bytes == 0U ||
        data_bytes > sizeof(mock->outgoing)) {
        return false;
    }
    memcpy(mock->outgoing, data, data_bytes);
    mock->outgoing_bytes = data_bytes;
    return true;
}

static bool realm_mock_receive(
    void *context, p4_game_multiplayer_message_t *message_out)
{
    realm_mock_t *const mock = context;
    if (mock->incoming_count == 0U) {
        return false;
    }
    *message_out = mock->incoming[mock->incoming_head];
    mock->incoming_head = (mock->incoming_head + 1U) % 8U;
    --mock->incoming_count;
    return true;
}

static void realm_mock_queue(
    realm_mock_t *mock,
    uint8_t kind,
    uint32_t transaction,
    uint16_t chunk_index,
    uint16_t chunk_count,
    const uint8_t *payload,
    size_t payload_bytes)
{
    CHECK(mock->incoming_count < 8U);
    CHECK(payload_bytes <=
          P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES - TEST_P4RM_HEADER_BYTES);
    if (mock->incoming_count >= 8U || payload_bytes >
            P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES -
                TEST_P4RM_HEADER_BYTES) {
        return;
    }
    const size_t tail = (mock->incoming_head + mock->incoming_count) % 8U;
    p4_game_multiplayer_message_t *const message = &mock->incoming[tail];
    *message = (p4_game_multiplayer_message_t){
        .sequence = ++mock->next_sequence,
        .player_slot = 1U,
        .bytes = (uint8_t)(TEST_P4RM_HEADER_BYTES + payload_bytes),
    };
    memcpy(message->data, "P4RM", 4U);
    message->data[4] = 2U;
    message->data[5] = kind;
    message->data[6] = 0U;
    message->data[7] = TEST_P4RM_HEADER_BYTES;
    test_store_u32(message->data, 8U, transaction);
    test_store_u16(message->data, 12U, chunk_index);
    test_store_u16(message->data, 14U, chunk_count);
    if (payload_bytes != 0U) {
        memcpy(message->data + TEST_P4RM_HEADER_BYTES,
               payload, payload_bytes);
    }
    ++mock->incoming_count;
}

static uint8_t realm_mock_take_kind(realm_mock_t *mock)
{
    CHECK(mock->outgoing_bytes >= TEST_P4RM_HEADER_BYTES);
    const uint8_t kind = mock->outgoing_bytes >= TEST_P4RM_HEADER_BYTES
        ? mock->outgoing[5] : 0U;
    mock->outgoing_bytes = 0U;
    return kind;
}

static bool mock_queue_save(void *context, const char *slot_id,
                            uint32_t schema_version,
                            uint32_t expected_sequence,
                            const uint8_t *data, size_t data_bytes,
                            p4_game_save_ticket_t *ticket_out)
{
    save_mock_t *const mock = context;
    if (strcmp(slot_id, "AUTO") != 0 ||
        schema_version != LORD_SAVE_FORMAT_VERSION ||
        data_bytes > sizeof(mock->payload)) {
        return false;
    }
    memcpy(mock->payload, data, data_bytes);
    mock->bytes = data_bytes;
    mock->expected_sequence = expected_sequence;
    mock->ticket = 7U;
    *ticket_out = mock->ticket;
    return true;
}

static bool mock_read_save(void *context, p4_game_save_ticket_t ticket,
                           p4_game_save_status_t *status_out,
                           uint32_t *sequence_out)
{
    save_mock_t *const mock = context;
    if (ticket != mock->ticket) {
        return false;
    }
    if (mock->sequence == 0U) {
        mock->sequence = mock->expected_sequence + 1U;
    }
    *status_out = P4_GAME_SAVE_COMMITTED;
    *sequence_out = mock->sequence;
    return true;
}

static bool start_game(p4_game_instance_t *instance, lord_state_t *state,
                       const p4_game_services_t *services)
{
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(instance, &p4_lord_game, services,
                                  state, sizeof(*state));
}

static void test_p4mp_mac_realm_hourly_sync(void)
{
    realm_mock_t realm = {0};
    static const p4_game_multiplayer_profile_t profile = {
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = 30U,
        .message_bytes = P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
        .protocol = UINT16_C(0x4c53),
    };
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_MULTIPLAYER_SESSION,
        .multiplayer_context = &realm,
        .multiplayer_read_status = realm_mock_status,
        .multiplayer_send = realm_mock_send,
        .multiplayer_receive = realm_mock_receive,
        .multiplayer_profile = &profile,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    CHECK(start_game(&instance, &state, &services));
    const p4_game_input_t idle = {0};
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm_mock_take_kind(&realm) == 1U);

    const uint32_t hello_transaction =
        test_load_u32(realm.outgoing, 8U);
    /* take_kind cleared only the length; the copied bytes remain available. */
    CHECK(hello_transaction != 0U);
    const uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES] = {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U,
        9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U,
    };
    uint8_t welcome[36] = {0};
    memcpy(welcome, actor_id, sizeof(actor_id));
    test_store_u32(welcome, 16U, 0U);
    test_store_u64(welcome, 20U, UINT64_C(100));
    test_store_u32(welcome, 28U, 1800U);
    realm_mock_queue(&realm, TEST_P4RM_WELCOME, hello_transaction,
                     0U, 0U, welcome, sizeof(welcome));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);

    enter_town(&state, LORD_CLASS_MYSTICAL);
    state.player.bank = 1000U;
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    const uint32_t upload_transaction = test_load_u32(realm.outgoing, 8U);
    const uint16_t upload_chunks = test_load_u16(realm.outgoing, 14U);
    CHECK(upload_chunks > 1U);
    (void)realm_mock_take_kind(&realm);

    const uint8_t begin_ack = TEST_P4RM_UPLOAD_BEGIN;
    realm_mock_queue(&realm, TEST_P4RM_ACK, upload_transaction,
                     TEST_P4RM_BEGIN_INDEX, upload_chunks,
                     &begin_ack, 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    for (uint16_t chunk = 0U; chunk < upload_chunks; ++chunk) {
        CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_CHUNK);
        CHECK(test_load_u16(realm.outgoing, 12U) == chunk);
        (void)realm_mock_take_kind(&realm);
        const uint8_t chunk_ack = TEST_P4RM_UPLOAD_CHUNK;
        realm_mock_queue(&realm, TEST_P4RM_ACK, upload_transaction,
                         chunk, upload_chunks, &chunk_ack, 1U);
        CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
              P4_GAME_CONTINUE);
    }
    CHECK(realm.outgoing_bytes == 0U);
    uint8_t committed[17] = {0};
    committed[0] = 0U;
    test_store_u32(committed, 1U, 1U);
    test_store_u64(committed, 5U, UINT64_C(100));
    test_store_u32(committed, 13U, 1700U);
    realm_mock_queue(&realm, TEST_P4RM_COMMIT_RESULT, upload_transaction,
                     0U, 0U, committed, sizeof(committed));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm_mock_take_kind(&realm) == TEST_P4RM_PROFILE);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_PROFILE_STATS);
    CHECK(test_load_u32(realm.outgoing, TEST_P4RM_HEADER_BYTES) ==
          state.player.gold);
    (void)realm_mock_take_kind(&realm);

    uint8_t directory_summary[44] = {0};
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        directory_summary[index] = (uint8_t)(0x80U + index);
    }
    memcpy(directory_summary + 16U, "Other Hero", 10U);
    directory_summary[36] = (uint8_t)LORD_HERO_STYLE_HEROINE;
    directory_summary[37] = (uint8_t)LORD_CLASS_THIEF;
    directory_summary[38] = 4U;
    directory_summary[39] = 0x05U;
    test_store_u16(directory_summary, 40U, 7U);
    test_store_u16(directory_summary, 42U, 2U);
    uint8_t directory_stats[40] = {0};
    memcpy(directory_stats, directory_summary, LORD_SYNC_ACTOR_ID_BYTES);
    test_store_u32(directory_stats, 16U, 35U);
    test_store_u32(directory_stats, 20U, 40U);
    test_store_u32(directory_stats, 24U, 18U);
    test_store_u32(directory_stats, 28U, 6U);
    test_store_u32(directory_stats, 32U, 1500U);
    test_store_u32(directory_stats, 36U, 250U);
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_SUMMARY,
                     UINT32_C(0x5001), 0U, 1U,
                     directory_summary, sizeof(directory_summary));
    realm_mock_queue(&realm, TEST_P4RM_DIRECTORY_STATS,
                     UINT32_C(0x5002), 0U, 1U,
                     directory_stats, sizeof(directory_stats));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(strcmp(state.realm[0].name, "Other Hero") == 0);
    CHECK(state.realm[0].level == 4U);
    CHECK(state.realm[0].hit_points == 35);
    CHECK(state.realm[0].pvp_wins == 7U);
    CHECK(strstr(state.realm[0].saying, "ONLINE") != NULL);

    const uint16_t old_day = state.player.day;
    const uint32_t old_bank = state.player.bank;
    uint8_t clock[16] = {0};
    test_store_u64(clock, 0U, UINT64_C(101));
    test_store_u32(clock, 8U, 3600U);
    clock[12] = 1U;
    realm_mock_queue(&realm, TEST_P4RM_CLOCK, UINT32_C(0x7777),
                     0U, 0U, clock, sizeof(clock));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.day == (uint16_t)(old_day + 1U));
    CHECK(state.player.bank == old_bank);
    CHECK(state.player.forest_fights == LORD_FOREST_FIGHTS_PER_DAY);
    CHECK(realm.outgoing[5] == TEST_P4RM_UPLOAD_BEGIN);
    (void)realm_mock_take_kind(&realm);

    const p4_game_input_t activate = {
        .held = P4_BUTTON_A, .pressed = P4_BUTTON_A,
    };
    state.screen = LORD_SCREEN_BANK_TRANSFER;
    state.selection = 0U;
    const uint32_t bank_before = state.player.bank;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    const uint32_t action_transaction = test_load_u32(realm.outgoing, 8U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 2U);
    CHECK(test_load_u16(realm.outgoing, TEST_P4RM_HEADER_BYTES + 2U) == 100U);
    CHECK(memcmp(realm.outgoing + TEST_P4RM_HEADER_BYTES + 4U,
                 directory_summary, LORD_SYNC_ACTOR_ID_BYTES) == 0);
    (void)realm_mock_take_kind(&realm);
    const uint8_t action_ack = TEST_P4RM_ACTION_BEGIN;
    realm_mock_queue(&realm, TEST_P4RM_ACK, action_transaction,
                     TEST_P4RM_BEGIN_INDEX, 0U, &action_ack, 1U);
    uint8_t action_result[16] = {0};
    action_result[1] = 2U;
    test_store_u32(action_result, 4U, 100U);
    realm_mock_queue(&realm, TEST_P4RM_ACTION_RESULT, action_transaction,
                     0U, 0U, action_result, sizeof(action_result));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.bank == bank_before);
    CHECK(realm.outgoing_bytes == 0U);

    uint8_t debit_event[48] = {0};
    test_store_u64(debit_event, 0U, UINT64_C(40));
    debit_event[8] = 2U;
    debit_event[9] = 1U;
    test_store_u32(debit_event, 12U, 100U);
    memcpy(debit_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(debit_event + 32U, "Other Hero", 10U);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6500),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     debit_event, sizeof(debit_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.bank == bank_before - 100U);
    CHECK(state.last_realm_event_id == UINT64_C(40));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    const uint32_t gold_before_event = state.player.gold;
    uint8_t transfer_event[48] = {0};
    test_store_u64(transfer_event, 0U, UINT64_C(41));
    transfer_event[8] = 2U;
    test_store_u32(transfer_event, 12U, 100U);
    memcpy(transfer_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(transfer_event + 32U, "Other Hero", 10U);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6600),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     transfer_event, sizeof(transfer_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == gold_before_event + 100U);
    CHECK(state.last_realm_event_id == UINT64_C(41));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    state.screen = LORD_SCREEN_TEXT_EDITOR;
    state.editor_target = LORD_EDITOR_MAIL;
    state.editor_return_screen = LORD_SCREEN_MAILBOX;
    state.selected_player = 0U;
    state.selection = 42U;
    (void)strcpy(state.editor_text, "MEET AT THE INN");
    const uint8_t mail_count_before = state.mail_count;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    const uint32_t mail_transaction = test_load_u32(realm.outgoing, 8U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 1U);
    CHECK(test_load_u16(realm.outgoing, 14U) == 1U);
    (void)realm_mock_take_kind(&realm);
    const uint8_t mail_begin_ack = TEST_P4RM_ACTION_BEGIN;
    realm_mock_queue(&realm, TEST_P4RM_ACK, mail_transaction,
                     TEST_P4RM_BEGIN_INDEX, 1U, &mail_begin_ack, 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BODY);
    CHECK(memcmp(realm.outgoing + TEST_P4RM_HEADER_BYTES,
                 "MEET AT THE INN", 15U) == 0);
    (void)realm_mock_take_kind(&realm);
    const uint8_t mail_body_ack = TEST_P4RM_ACTION_BODY;
    realm_mock_queue(&realm, TEST_P4RM_ACK, mail_transaction,
                     0U, 1U, &mail_body_ack, 1U);
    uint8_t mail_result[16] = {0};
    mail_result[1] = 1U;
    realm_mock_queue(&realm, TEST_P4RM_ACTION_RESULT, mail_transaction,
                     0U, 0U, mail_result, sizeof(mail_result));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.mail_count == (uint8_t)(mail_count_before + 1U));
    CHECK(state.mail[state.mail_count - 1U].outgoing);

    static const uint8_t incoming_mail[] = "WELCOME FRIEND";
    uint8_t mail_event[48] = {0};
    test_store_u64(mail_event, 0U, UINT64_C(42));
    mail_event[8] = 1U;
    mail_event[10] = (uint8_t)(sizeof(incoming_mail) - 1U);
    memcpy(mail_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(mail_event + 32U, "Other Hero", 10U);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6700),
                     TEST_P4RM_BEGIN_INDEX, 1U,
                     mail_event, sizeof(mail_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACK);
    (void)realm_mock_take_kind(&realm);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BODY, UINT32_C(0x6700),
                     0U, 1U, incoming_mail, sizeof(incoming_mail) - 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.last_realm_event_id == UINT64_C(42));
    CHECK(strcmp(state.mail[state.mail_count - 1U].body,
                 "WELCOME FRIEND") == 0);
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    const uint8_t mail_count_after_event = state.mail_count;
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BODY, UINT32_C(0x6700),
                     0U, 1U, incoming_mail, sizeof(incoming_mail) - 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.last_realm_event_id == UINT64_C(42));
    CHECK(state.mail_count == mail_count_after_event);
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    const uint8_t friendship_before = state.friendship_actions;
    const uint32_t supplies_before = state.player.gold;
    const uint16_t charm_before = state.player.charm;
    const uint8_t trust_before = state.realm[0].trust;
    uint8_t friend_event[48] = {0};
    test_store_u64(friend_event, 0U, UINT64_C(43));
    friend_event[8] = 3U;
    friend_event[9] = UINT8_C(0x81);
    test_store_u32(friend_event, 12U, 30U);
    memcpy(friend_event + 16U, directory_summary,
           LORD_SYNC_ACTOR_ID_BYTES);
    memcpy(friend_event + 32U, "Other Hero", 10U);
    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6800),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     friend_event, sizeof(friend_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.friendship_actions == (uint8_t)(friendship_before - 1U));
    CHECK(state.player.gold == supplies_before - 100U);
    CHECK(state.player.charm == (uint16_t)(charm_before + 1U));
    CHECK(state.realm[0].trust == (uint8_t)(trust_before + 30U));
    CHECK(state.last_realm_event_id == UINT64_C(43));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    realm_mock_queue(&realm, TEST_P4RM_EVENT_BEGIN, UINT32_C(0x6801),
                     TEST_P4RM_BEGIN_INDEX, 0U,
                     friend_event, sizeof(friend_event));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.friendship_actions == (uint8_t)(friendship_before - 1U));
    CHECK(state.player.gold == supplies_before - 100U);
    CHECK(state.player.charm == (uint16_t)(charm_before + 1U));
    CHECK(state.realm[0].trust == (uint8_t)(trust_before + 30U));
    CHECK(realm.outgoing[5] == TEST_P4RM_EVENT_ACK);
    (void)realm_mock_take_kind(&realm);

    state.screen = LORD_SCREEN_PLAYER_DETAIL;
    state.selection = 0U;
    state.selected_player = 0U;
    state.player.strength = 500;
    state.player.gold = 700U;
    state.realm[0].gold = 251U;
    state.realm[0].hit_points = 1;
    state.realm[0].max_hit_points = 1;
    state.realm[0].defense = 0;
    state.realm[0].alive = true;
    const uint32_t pvp_gold_before = state.player.gold;
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    const uint32_t pvp_begin_transaction =
        test_load_u32(realm.outgoing, 8U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 6U);
    (void)realm_mock_take_kind(&realm);
    realm_mock_queue(&realm, TEST_P4RM_ACK, pvp_begin_transaction,
                     TEST_P4RM_BEGIN_INDEX, 0U, &action_ack, 1U);
    uint8_t pvp_begin_result[16] = {0};
    pvp_begin_result[1] = 6U;
    test_store_u64(pvp_begin_result, 8U, UINT64_C(7001));
    realm_mock_queue(&realm, TEST_P4RM_ACTION_RESULT,
                     pvp_begin_transaction, 0U, 0U,
                     pvp_begin_result, sizeof(pvp_begin_result));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_BATTLE);
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == pvp_gold_before);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BEGIN);
    const uint32_t pvp_resolve_transaction =
        test_load_u32(realm.outgoing, 8U);
    CHECK(realm.outgoing[TEST_P4RM_HEADER_BYTES] == 7U);
    (void)realm_mock_take_kind(&realm);
    realm_mock_queue(&realm, TEST_P4RM_ACK, pvp_resolve_transaction,
                     TEST_P4RM_BEGIN_INDEX, 1U, &action_ack, 1U);
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(realm.outgoing[5] == TEST_P4RM_ACTION_BODY);
    (void)realm_mock_take_kind(&realm);
    realm_mock_queue(&realm, TEST_P4RM_ACK, pvp_resolve_transaction,
                     0U, 1U, &mail_body_ack, 1U);
    uint8_t pvp_resolve_result[16] = {0};
    pvp_resolve_result[1] = 7U;
    pvp_resolve_result[2] = 1U;
    test_store_u32(pvp_resolve_result, 4U, 37U);
    test_store_u64(pvp_resolve_result, 8U, UINT64_C(7001));
    realm_mock_queue(&realm, TEST_P4RM_ACTION_RESULT,
                     pvp_resolve_transaction, 0U, 0U,
                     pvp_resolve_result, sizeof(pvp_resolve_result));
    CHECK(p4_game_instance_update(&instance, &idle, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player.gold == pvp_gold_before + 37U);

    p4_game_instance_stop(&instance);
}

static void capture_frame_if_requested(const p4_game_surface_t *surface,
                                       lord_screen_t screen)
{
    const char *const directory = getenv("LORD_CAPTURE_DIR");
    if (directory == NULL || directory[0] == '\0') {
        return;
    }
    char path[512];
    const int path_length = snprintf(path, sizeof(path),
                                     "%s/screen-%02d.ppm", directory,
                                     (int)screen);
    CHECK(path_length > 0 && (size_t)path_length < sizeof(path));
    if (path_length <= 0 || (size_t)path_length >= sizeof(path)) {
        return;
    }
    FILE *const file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL) {
        return;
    }
    (void)fprintf(file, "P6\n%u %u\n255\n", surface->width,
                  surface->height);
    for (uint16_t y = 0U; y < surface->height; ++y) {
        for (uint16_t x = 0U; x < surface->width; ++x) {
            const uint16_t pixel = surface->pixels[
                (size_t)y * surface->stride_pixels + x];
            const uint8_t rgb[3] = {
                (uint8_t)((((uint32_t)(pixel >> 11U) & 31U) * 255U + 15U) /
                          31U),
                (uint8_t)((((uint32_t)(pixel >> 5U) & 63U) * 255U + 31U) /
                          63U),
                (uint8_t)(((uint32_t)(pixel & 31U) * 255U + 15U) / 31U),
            };
            CHECK(fwrite(rgb, sizeof(rgb), 1U, file) == 1U);
        }
    }
    CHECK(fclose(file) == 0);
}

static void test_runtime_save_render_and_exit(void)
{
    save_mock_t save = {0};
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            P4_GAME_CAP_SAVE,
        .save_context = &save,
        .queue_save = mock_queue_save,
        .read_save_status = mock_read_save,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    CHECK(start_game(&instance, &state, &services));
    const p4_game_input_t activate = {
        .held = P4_BUTTON_A, .pressed = P4_BUTTON_A,
    };
    CHECK(p4_game_instance_update(&instance, &activate, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_NAME);

    enum {
        GUARD = 17,
        STRIDE = P4_GAME_SURFACE_WIDTH + 7,
        WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
        TOTAL = GUARD + WORDS + GUARD,
    };
    uint16_t *const allocation = calloc(TOTAL, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation != NULL) {
        for (size_t index = 0U; index < TOTAL; ++index) {
            allocation[index] = UINT16_C(0x5aa5);
        }
        p4_game_surface_t surface = {
            .pixels = allocation + GUARD,
            .stride_pixels = STRIDE,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        (void)strcpy(state.player.name, "Test Hero");
        state.player.hero_style = LORD_HERO_STYLE_HEROINE;
        state.player.hero_class = LORD_CLASS_MYSTICAL;
        state.player.level = 3U;
        state.player.hit_points = 54;
        state.player.max_hit_points = 70;
        state.player.strength = 38;
        state.player.defense = 9;
        state.player.gold = 4321U;
        state.player.bank = 9876U;
        state.player.experience = 1120U;
        state.player.forest_fights = 11U;
        state.player.day = 4U;
        state.player.charm = 22U;
        state.player.gems = 3U;
        state.player.young_heroes_helped = 1U;
        state.player.skill[LORD_CLASS_MYSTICAL] = 12U;
        state.player.skill_uses[LORD_CLASS_MYSTICAL] = 3U;
        state.selected_player = 1U;
        state.realm[1].trust = 70U;
        state.dice_player = 9U;
        state.dice_host = 6U;
        (void)strcpy(state.battle_line,
                     "You win 10 ChompCoin! The table cheers.");
        (void)strcpy(state.conversation, "THE DRAGON IS RESTLESS TONIGHT");
        (void)strcpy(state.editor_text, "MEET ME AT THE INN");
        for (int screen = LORD_SCREEN_TITLE;
             screen <= LORD_SCREEN_TEXT_EDITOR; ++screen) {
            state.screen = (lord_screen_t)screen;
            state.selection = 0U;
            state.menu_scroll = 0U;
            state.selected_igm = 0U;
            CHECK(p4_game_instance_render(&instance, &surface));
            capture_frame_if_requested(&surface, state.screen);
        }
        for (size_t index = 0U; index < GUARD; ++index) {
            CHECK(allocation[index] == UINT16_C(0x5aa5));
            CHECK(allocation[GUARD + WORDS + index] == UINT16_C(0x5aa5));
        }
        for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
            for (size_t column = P4_GAME_SURFACE_WIDTH;
                 column < STRIDE; ++column) {
                CHECK(surface.pixels[row * STRIDE + column] ==
                      UINT16_C(0x5aa5));
            }
        }
        free(allocation);
    }
    state.player.level = 1U;
    (void)strcpy(state.player.name, "Saved Hero");
    state.player.max_hit_points = 20;
    state.player.hit_points = 20;
    state.player.strength = 10;
    state.player.day = 1U;
    state.save_dirty = true;
    ++state.save_sequence;
    uint8_t save_probe[LORD_SAVE_MAX_BYTES];
    CHECK(state.save_available);
    CHECK(state.save_ticket == P4_GAME_SAVE_INVALID_TICKET);
    CHECK(lord_save_encode(&state, save_probe, sizeof(save_probe)) > 0U);
    const p4_game_input_t idle = {0};
    (void)p4_game_instance_update(&instance, &idle, 16U);
    CHECK(save.bytes > 0U);
    (void)p4_game_instance_update(&instance, &idle, 16U);
    CHECK(!state.save_dirty);

    const p4_game_input_t back = {
        .held = P4_BUTTON_BACK, .pressed = P4_BUTTON_BACK,
    };
    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

static p4_physical_touch_t physical_touch_for(uint16_t logical_x,
                                              uint16_t logical_y)
{
    return (p4_physical_touch_t){
        .x = (uint16_t)(P4_INPUT_VIEWPORT_LEFT +
            ((uint32_t)logical_x * P4_INPUT_VIEWPORT_WIDTH) /
                P4_GAME_SURFACE_WIDTH + 1U),
        .y = (uint16_t)(P4_INPUT_VIEWPORT_TOP +
            ((uint32_t)logical_y * P4_INPUT_VIEWPORT_HEIGHT) /
                P4_GAME_SURFACE_HEIGHT + 1U),
    };
}

static void test_standard_touch_lifecycle(void)
{
    static const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    p4_game_input_mapper_t mapper;
    p4_game_input_t input;
    CHECK(start_game(&instance, &state, &services));
    p4_game_input_mapper_init(&mapper);

    p4_physical_touch_t touch = physical_touch_for(286U, 158U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK((input.pressed & P4_BUTTON_A) != 0U);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_NAME);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);

    touch = physical_touch_for(240U, 176U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK((input.pressed & P4_BUTTON_B) != 0U);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_TITLE);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);

    touch = physical_touch_for(26U, 12U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK((input.pressed & P4_BUTTON_BACK) != 0U);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

int main(void)
{
    CHECK(p4_game_descriptor_valid(&p4_lord_game));
    CHECK(p4_lord_game.launcher_id == 112U);
    CHECK(p4_lord_game.state_bytes == sizeof(lord_state_t));
    CHECK(sizeof(lord_state_t) <= P4_GAME_MAX_STATE_BYTES);
    test_character_creation_and_core_menu();
    test_shops_bank_and_transfer();
    test_forest_training_skills_and_dragon();
    test_mail_pvp_friendship_and_mentoring();
    test_full_inn_and_igms();
    test_save_round_trip();
    test_backend_sync_envelope();
    test_p4mp_mac_realm_hourly_sync();
    test_runtime_save_render_and_exit();
    test_standard_touch_lifecycle();
    if (s_failures != 0) {
        fprintf(stderr, "%d LORD test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("LORD full-port tests passed");
    return EXIT_SUCCESS;
}
