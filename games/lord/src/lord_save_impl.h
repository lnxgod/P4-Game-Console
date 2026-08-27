// SPDX-License-Identifier: LicenseRef-LORD-Permission

#include "lord_internal.h"

#include <string.h>

enum { LORD_SAVE_HEADER_BYTES = 16 };

typedef struct {
    uint8_t *bytes;
    size_t capacity;
    size_t offset;
    bool valid;
} lord_save_writer_t;

typedef struct {
    const uint8_t *bytes;
    size_t length;
    size_t offset;
    bool valid;
} lord_save_reader_t;

static void save_write_u8(lord_save_writer_t *writer, uint8_t value)
{
    if (!writer->valid || writer->offset >= writer->capacity) {
        writer->valid = false;
        return;
    }
    writer->bytes[writer->offset++] = value;
}

static void save_write_u16(lord_save_writer_t *writer, uint16_t value)
{
    save_write_u8(writer, (uint8_t)(value & UINT16_C(0xff)));
    save_write_u8(writer, (uint8_t)(value >> 8U));
}

static void save_write_u32(lord_save_writer_t *writer, uint32_t value)
{
    save_write_u8(writer, (uint8_t)(value & UINT32_C(0xff)));
    save_write_u8(writer, (uint8_t)((value >> 8U) & UINT32_C(0xff)));
    save_write_u8(writer, (uint8_t)((value >> 16U) & UINT32_C(0xff)));
    save_write_u8(writer, (uint8_t)(value >> 24U));
}

static void save_write_u64(lord_save_writer_t *writer, uint64_t value)
{
    save_write_u32(writer, (uint32_t)(value & UINT64_C(0xffffffff)));
    save_write_u32(writer, (uint32_t)(value >> 32U));
}

static void save_write_i32(lord_save_writer_t *writer, int32_t value)
{
    save_write_u32(writer, (uint32_t)value);
}

static void save_write_bool(lord_save_writer_t *writer, bool value)
{
    save_write_u8(writer, value ? 1U : 0U);
}

static void save_write_chars(lord_save_writer_t *writer, const char *text,
                             size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        save_write_u8(writer, (uint8_t)text[index]);
    }
}

static uint8_t save_read_u8(lord_save_reader_t *reader)
{
    if (!reader->valid || reader->offset >= reader->length) {
        reader->valid = false;
        return 0U;
    }
    return reader->bytes[reader->offset++];
}

static uint16_t save_read_u16(lord_save_reader_t *reader)
{
    const uint16_t low = save_read_u8(reader);
    const uint16_t high = save_read_u8(reader);
    return (uint16_t)(low | (uint16_t)(high << 8U));
}

static uint32_t save_read_u32(lord_save_reader_t *reader)
{
    const uint32_t byte_0 = save_read_u8(reader);
    const uint32_t byte_1 = save_read_u8(reader);
    const uint32_t byte_2 = save_read_u8(reader);
    const uint32_t byte_3 = save_read_u8(reader);
    return byte_0 | (byte_1 << 8U) | (byte_2 << 16U) | (byte_3 << 24U);
}

static uint64_t save_read_u64(lord_save_reader_t *reader)
{
    const uint64_t low = save_read_u32(reader);
    const uint64_t high = save_read_u32(reader);
    return low | (high << 32U);
}

static int32_t save_read_i32(lord_save_reader_t *reader)
{
    return (int32_t)save_read_u32(reader);
}

static bool save_read_bool(lord_save_reader_t *reader)
{
    const uint8_t value = save_read_u8(reader);
    if (value > 1U) {
        reader->valid = false;
    }
    return value != 0U;
}

static void save_read_chars(lord_save_reader_t *reader, char *text,
                            size_t length)
{
    for (size_t index = 0U; index < length; ++index) {
        text[index] = (char)save_read_u8(reader);
    }
}

static uint32_t save_crc32(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = UINT32_C(0) - (crc & UINT32_C(1));
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

static void save_store_u16(uint8_t *bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value & UINT16_C(0xff));
    bytes[offset + 1U] = (uint8_t)(value >> 8U);
}

static void save_store_u32(uint8_t *bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value & UINT32_C(0xff));
    bytes[offset + 1U] = (uint8_t)((value >> 8U) & UINT32_C(0xff));
    bytes[offset + 2U] = (uint8_t)((value >> 16U) & UINT32_C(0xff));
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

static void save_write_player(lord_save_writer_t *writer,
                              const lord_player_t *player)
{
    save_write_chars(writer, player->name, sizeof(player->name));
    save_write_u8(writer, (uint8_t)player->hero_style);
    save_write_u8(writer, (uint8_t)player->hero_class);
    save_write_u8(writer, player->level);
    save_write_u8(writer, player->weapon);
    save_write_u8(writer, player->armor);
    save_write_i32(writer, player->hit_points);
    save_write_i32(writer, player->max_hit_points);
    save_write_i32(writer, player->strength);
    save_write_i32(writer, player->defense);
    save_write_u32(writer, player->gold);
    save_write_u32(writer, player->bank);
    save_write_u32(writer, player->experience);
    save_write_u16(writer, player->forest_fights);
    for (size_t index = 0U; index < LORD_SKILL_COUNT; ++index) {
        save_write_u8(writer, player->skill[index]);
        save_write_u8(writer, player->skill_uses[index]);
    }
    save_write_u8(writer, player->dragon_kills);
    save_write_u16(writer, player->day);
    save_write_u16(writer, player->pvp_wins);
    save_write_u16(writer, player->pvp_losses);
    save_write_u16(writer, player->charm);
    save_write_u16(writer, player->gems);
    save_write_u16(writer, player->young_heroes_helped);
    save_write_u16(writer, player->friendship_badges);
    save_write_bool(writer, player->horse);
    save_write_bool(writer, player->fairy);
    save_write_bool(writer, player->fairy_lore);
    save_write_bool(writer, player->amulet);
    save_write_bool(writer, player->high_spirits);
    save_write_bool(writer, player->seen_dragon);
}

static void save_read_player(lord_save_reader_t *reader,
                             lord_player_t *player)
{
    save_read_chars(reader, player->name, sizeof(player->name));
    player->hero_style = (lord_hero_style_t)save_read_u8(reader);
    player->hero_class = (lord_class_t)save_read_u8(reader);
    player->level = save_read_u8(reader);
    player->weapon = save_read_u8(reader);
    player->armor = save_read_u8(reader);
    player->hit_points = save_read_i32(reader);
    player->max_hit_points = save_read_i32(reader);
    player->strength = save_read_i32(reader);
    player->defense = save_read_i32(reader);
    player->gold = save_read_u32(reader);
    player->bank = save_read_u32(reader);
    player->experience = save_read_u32(reader);
    player->forest_fights = save_read_u16(reader);
    for (size_t index = 0U; index < LORD_SKILL_COUNT; ++index) {
        player->skill[index] = save_read_u8(reader);
        player->skill_uses[index] = save_read_u8(reader);
    }
    player->dragon_kills = save_read_u8(reader);
    player->day = save_read_u16(reader);
    player->pvp_wins = save_read_u16(reader);
    player->pvp_losses = save_read_u16(reader);
    player->charm = save_read_u16(reader);
    player->gems = save_read_u16(reader);
    player->young_heroes_helped = save_read_u16(reader);
    player->friendship_badges = save_read_u16(reader);
    player->horse = save_read_bool(reader);
    player->fairy = save_read_bool(reader);
    player->fairy_lore = save_read_bool(reader);
    player->amulet = save_read_bool(reader);
    player->high_spirits = save_read_bool(reader);
    player->seen_dragon = save_read_bool(reader);
}

size_t lord_save_encode(const lord_state_t *state, uint8_t *bytes,
                        size_t capacity)
{
    if (state == NULL || bytes == NULL || capacity < LORD_SAVE_HEADER_BYTES) {
        return 0U;
    }
    lord_save_writer_t writer = {
        .bytes = bytes,
        .capacity = capacity < LORD_SAVE_MAX_BYTES ? capacity :
            LORD_SAVE_MAX_BYTES,
        .valid = true,
    };
    save_write_u8(&writer, (uint8_t)'L');
    save_write_u8(&writer, (uint8_t)'D');
    save_write_u8(&writer, (uint8_t)'S');
    save_write_u8(&writer, (uint8_t)'V');
    save_write_u16(&writer, LORD_SAVE_FORMAT_VERSION);
    save_write_u16(&writer, 0U);
    save_write_u32(&writer, 0U);
    save_write_u32(&writer, state->save_sequence);

    save_write_u32(&writer, state->rng_state);
    save_write_u32(&writer, state->realm_revision);
    save_write_u8(&writer, state->partner_index < 0 ? 0U :
                  (uint8_t)((uint8_t)state->partner_index + 1U));
    save_write_u8(&writer, state->npc_friend < 0 ? 0U :
                  (uint8_t)((uint8_t)state->npc_friend + 1U));
    save_write_u8(&writer, state->pvp_fights);
    save_write_u8(&writer, state->friendship_actions);
    save_write_u8(&writer, state->igm_used_mask);
    save_write_u8(&writer, state->rip_scene);
    save_write_u8(&writer, state->mail_count);
    save_write_u8(&writer, state->log_count);
    save_write_chars(&writer, state->conversation,
                     sizeof(state->conversation));
    save_write_chars(&writer, state->announcement,
                     sizeof(state->announcement));
    save_write_player(&writer, &state->player);

    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        const lord_realm_player_t *const player = &state->realm[index];
        save_write_chars(&writer, player->name, sizeof(player->name));
        save_write_chars(&writer, player->saying, sizeof(player->saying));
        save_write_u8(&writer, (uint8_t)player->hero_style);
        save_write_u8(&writer, (uint8_t)player->hero_class);
        save_write_u8(&writer, player->level);
        save_write_bool(&writer, player->alive);
        save_write_bool(&writer, player->at_inn);
        save_write_bool(&writer, player->teamed);
        save_write_u8(&writer, player->trust);
        save_write_i32(&writer, player->hit_points);
        save_write_i32(&writer, player->max_hit_points);
        save_write_i32(&writer, player->strength);
        save_write_i32(&writer, player->defense);
        save_write_u32(&writer, player->gold);
        save_write_u32(&writer, player->experience);
        save_write_u16(&writer, player->pvp_wins);
        save_write_u16(&writer, player->pvp_losses);
    }
    for (size_t index = 0U; index < LORD_MAIL_COUNT_MAX; ++index) {
        const lord_mail_t *const mail = &state->mail[index];
        save_write_u8(&writer, mail->sender);
        save_write_u8(&writer, mail->recipient);
        save_write_u8(&writer, (uint8_t)mail->kind);
        save_write_bool(&writer, mail->unread);
        save_write_bool(&writer, mail->outgoing);
        save_write_chars(&writer, mail->body, sizeof(mail->body));
    }
    for (size_t index = 0U; index < LORD_LOG_COUNT_MAX; ++index) {
        save_write_u16(&writer, state->log[index].day);
        save_write_chars(&writer, state->log[index].text,
                         sizeof(state->log[index].text));
    }
    save_write_chars(&writer, "MPV5", 4U);
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        for (size_t byte = 0U; byte < LORD_SYNC_ACTOR_ID_BYTES; ++byte) {
            save_write_u8(&writer, state->realm_actor_ids[index][byte]);
        }
    }
    for (size_t byte = 0U; byte < LORD_SYNC_ACTOR_ID_BYTES; ++byte) {
        save_write_u8(&writer, state->partner_actor_id[byte]);
    }
    save_write_u64(&writer, state->last_realm_event_id);
    for (size_t byte = 0U; byte < LORD_SYNC_ACTOR_ID_BYTES; ++byte) {
        save_write_u8(&writer, state->sync_actor_id[byte]);
    }
    save_write_u32(&writer, state->sync_server_revision);
    save_write_u32(&writer, state->sync_committed_save_sequence);
    if (!writer.valid || writer.offset > UINT16_MAX) {
        return 0U;
    }
    const uint16_t total_length = (uint16_t)writer.offset;
    save_store_u16(bytes, 6U, total_length);
    save_store_u32(bytes, 8U,
                   save_crc32(bytes + LORD_SAVE_HEADER_BYTES,
                              writer.offset - LORD_SAVE_HEADER_BYTES));
    return writer.offset;
}

static bool save_text_valid(char *text, size_t capacity, bool allow_empty)
{
    text[capacity - 1U] = '\0';
    const size_t length = text_length(text, capacity);
    return length < capacity && (allow_empty || length > 0U);
}

static bool save_player_valid(lord_player_t *player)
{
    if (!save_text_valid(player->name, sizeof(player->name), false) ||
        text_length(player->name, sizeof(player->name)) < 3U ||
        player->hero_style > LORD_HERO_STYLE_HEROINE ||
        player->hero_class > LORD_CLASS_THIEF || player->level < 1U ||
        player->level > LORD_MAX_LEVEL || player->weapon >= LORD_WEAPON_COUNT ||
        player->armor >= LORD_ARMOR_COUNT || player->max_hit_points <= 0 ||
        player->hit_points < 0 ||
        player->hit_points > player->max_hit_points ||
        player->strength <= 0 || player->defense < 0 || player->day == 0U) {
        return false;
    }
    for (size_t index = 0U; index < LORD_SKILL_COUNT; ++index) {
        if (player->skill[index] > LORD_SKILL_MASTERY_MAX) {
            return false;
        }
    }
    return true;
}

static bool save_realm_valid(lord_state_t *state)
{
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        lord_realm_player_t *const player = &state->realm[index];
        if (!save_text_valid(player->name, sizeof(player->name), false) ||
            !save_text_valid(player->saying, sizeof(player->saying), true) ||
            player->hero_style > LORD_HERO_STYLE_HEROINE ||
            player->hero_class > LORD_CLASS_THIEF || player->level < 1U ||
            player->level > LORD_MAX_LEVEL || player->trust > 100U ||
            player->max_hit_points <= 0 || player->hit_points < 0 ||
            player->hit_points > player->max_hit_points ||
            player->strength <= 0 || player->defense < 0) {
            return false;
        }
    }
    return true;
}

bool lord_save_decode(lord_state_t *state, const uint8_t *bytes,
                      size_t length)
{
    if (state == NULL || bytes == NULL || length < LORD_SAVE_HEADER_BYTES ||
        length > LORD_SAVE_MAX_BYTES || bytes[0] != (uint8_t)'L' ||
        bytes[1] != (uint8_t)'D' || bytes[2] != (uint8_t)'S' ||
        bytes[3] != (uint8_t)'V') {
        return false;
    }
    lord_save_reader_t header = {
        .bytes = bytes, .length = length, .offset = 4U, .valid = true,
    };
    const uint16_t version = save_read_u16(&header);
    const uint16_t stored_length = save_read_u16(&header);
    const uint32_t stored_crc = save_read_u32(&header);
    const uint32_t save_sequence = save_read_u32(&header);
    if (!header.valid || version < LORD_SAVE_MINIMUM_VERSION ||
        version > LORD_SAVE_FORMAT_VERSION ||
        stored_length != length ||
        stored_crc != save_crc32(bytes + LORD_SAVE_HEADER_BYTES,
                                 length - LORD_SAVE_HEADER_BYTES)) {
        return false;
    }

    lord_state_t loaded;
    lord_initialize(&loaded, UINT32_C(0x4c4f5244));
    loaded.save_sequence = save_sequence;
    lord_save_reader_t reader = {
        .bytes = bytes, .length = length,
        .offset = LORD_SAVE_HEADER_BYTES, .valid = true,
    };
    loaded.rng_state = save_read_u32(&reader);
    loaded.realm_revision = save_read_u32(&reader);
    const uint8_t partner_code = save_read_u8(&reader);
    const uint8_t npc_friend_code = save_read_u8(&reader);
    loaded.partner_index = partner_code == 0U ? -1 :
        (int8_t)(partner_code - 1U);
    loaded.npc_friend = npc_friend_code == 0U ? -1 :
        (int8_t)(npc_friend_code - 1U);
    loaded.pvp_fights = save_read_u8(&reader);
    loaded.friendship_actions = save_read_u8(&reader);
    loaded.igm_used_mask = save_read_u8(&reader);
    loaded.rip_scene = save_read_u8(&reader);
    loaded.mail_count = save_read_u8(&reader);
    loaded.log_count = save_read_u8(&reader);
    save_read_chars(&reader, loaded.conversation,
                    sizeof(loaded.conversation));
    save_read_chars(&reader, loaded.announcement,
                    sizeof(loaded.announcement));
    save_read_player(&reader, &loaded.player);

    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        lord_realm_player_t *const player = &loaded.realm[index];
        save_read_chars(&reader, player->name, sizeof(player->name));
        save_read_chars(&reader, player->saying, sizeof(player->saying));
        player->hero_style = (lord_hero_style_t)save_read_u8(&reader);
        player->hero_class = (lord_class_t)save_read_u8(&reader);
        player->level = save_read_u8(&reader);
        player->alive = save_read_bool(&reader);
        player->at_inn = save_read_bool(&reader);
        player->teamed = save_read_bool(&reader);
        player->trust = save_read_u8(&reader);
        player->hit_points = save_read_i32(&reader);
        player->max_hit_points = save_read_i32(&reader);
        player->strength = save_read_i32(&reader);
        player->defense = save_read_i32(&reader);
        player->gold = save_read_u32(&reader);
        player->experience = save_read_u32(&reader);
        player->pvp_wins = save_read_u16(&reader);
        player->pvp_losses = save_read_u16(&reader);
    }
    for (size_t index = 0U; index < LORD_MAIL_COUNT_MAX; ++index) {
        lord_mail_t *const mail = &loaded.mail[index];
        mail->sender = save_read_u8(&reader);
        mail->recipient = save_read_u8(&reader);
        mail->kind = (lord_mail_kind_t)save_read_u8(&reader);
        mail->unread = save_read_bool(&reader);
        mail->outgoing = save_read_bool(&reader);
        save_read_chars(&reader, mail->body, sizeof(mail->body));
    }
    for (size_t index = 0U; index < LORD_LOG_COUNT_MAX; ++index) {
        loaded.log[index].day = save_read_u16(&reader);
        save_read_chars(&reader, loaded.log[index].text,
                        sizeof(loaded.log[index].text));
    }
    if (version >= 4U) {
        char marker[4];
        save_read_chars(&reader, marker, sizeof(marker));
        const char *const expected_marker = version == 4U ? "MPV4" : "MPV5";
        if (memcmp(marker, expected_marker, sizeof(marker)) != 0) {
            reader.valid = false;
        }
        for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
            for (size_t byte = 0U; byte < LORD_SYNC_ACTOR_ID_BYTES; ++byte) {
                loaded.realm_actor_ids[index][byte] = save_read_u8(&reader);
            }
        }
        for (size_t byte = 0U; byte < LORD_SYNC_ACTOR_ID_BYTES; ++byte) {
            loaded.partner_actor_id[byte] = save_read_u8(&reader);
        }
        loaded.last_realm_event_id = save_read_u64(&reader);
        if (version >= 5U) {
            for (size_t byte = 0U; byte < LORD_SYNC_ACTOR_ID_BYTES; ++byte) {
                loaded.sync_actor_id[byte] = save_read_u8(&reader);
            }
            loaded.sync_server_revision = save_read_u32(&reader);
            loaded.sync_committed_save_sequence = save_read_u32(&reader);
        }
    }
    uint8_t sync_actor_combined = 0U;
    for (size_t byte = 0U; byte < LORD_SYNC_ACTOR_ID_BYTES; ++byte) {
        sync_actor_combined = (uint8_t)(
            sync_actor_combined | loaded.sync_actor_id[byte]);
    }
    if (!reader.valid || reader.offset != length || loaded.rng_state == 0U ||
        loaded.realm_revision == 0U || partner_code > LORD_REALM_PLAYER_COUNT ||
        npc_friend_code > 2U || loaded.pvp_fights > LORD_PVP_FIGHTS_PER_DAY ||
        loaded.friendship_actions > LORD_FRIENDSHIP_ACTIONS_PER_DAY ||
        (loaded.igm_used_mask & (uint8_t)~((1U << LORD_IGM_COUNT) - 1U)) != 0U ||
        loaded.rip_scene >= LORD_RIP_SCENE_COUNT ||
        loaded.mail_count > LORD_MAIL_COUNT_MAX ||
        loaded.log_count > LORD_LOG_COUNT_MAX ||
        !save_text_valid(loaded.conversation, sizeof(loaded.conversation), true) ||
        !save_text_valid(loaded.announcement, sizeof(loaded.announcement), true) ||
        !save_player_valid(&loaded.player) || !save_realm_valid(&loaded) ||
        ((loaded.sync_server_revision == 0U) !=
         (sync_actor_combined == 0U)) ||
        ((loaded.sync_server_revision == 0U) !=
         (loaded.sync_committed_save_sequence == 0U)) ||
        loaded.sync_committed_save_sequence > loaded.save_sequence) {
        return false;
    }
    if (loaded.partner_index >= 0 &&
        !loaded.realm[(size_t)loaded.partner_index].teamed) {
        return false;
    }
    for (size_t index = 0U; index < LORD_MAIL_COUNT_MAX; ++index) {
        lord_mail_t *const mail = &loaded.mail[index];
        if (mail->sender > LORD_MAIL_SENDER_HERO ||
            mail->recipient > LORD_MAIL_SENDER_HERO ||
            mail->kind > LORD_MAIL_ANNOUNCEMENT ||
            !save_text_valid(mail->body, sizeof(mail->body), true)) {
            return false;
        }
    }
    for (size_t index = 0U; index < LORD_LOG_COUNT_MAX; ++index) {
        if (!save_text_valid(loaded.log[index].text,
                             sizeof(loaded.log[index].text), true)) {
            return false;
        }
    }
    loaded.screen = LORD_SCREEN_TOWN;
    loaded.return_screen = LORD_SCREEN_TOWN;
    loaded.editor_return_screen = LORD_SCREEN_TOWN;
    loaded.battle_kind = LORD_BATTLE_NONE;
    loaded.selection = 0U;
    loaded.menu_scroll = 0U;
    loaded.held_buttons = 0U;
    loaded.save_dirty = false;
    loaded.message_line_1[0] = '\0';
    loaded.message_line_2[0] = '\0';
    loaded.battle_line[0] = '\0';
    loaded.editor_text[0] = '\0';
    memcpy(state, &loaded, sizeof(loaded));
    return true;
}
