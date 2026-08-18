// SPDX-License-Identifier: LicenseRef-LORD-Permission

#include "lord_internal.h"

#include <string.h>

enum {
    LORD_SAVE_HEADER_BYTES = 16,
};

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
    writer->bytes[writer->offset] = value;
    ++writer->offset;
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

static void save_write_i32(lord_save_writer_t *writer, int32_t value)
{
    save_write_u32(writer, (uint32_t)value);
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
    const uint8_t value = reader->bytes[reader->offset];
    ++reader->offset;
    return value;
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

static int32_t save_read_i32(lord_save_reader_t *reader)
{
    return (int32_t)save_read_u32(reader);
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
    bytes[offset + 1U] =
        (uint8_t)((value >> 8U) & UINT32_C(0xff));
    bytes[offset + 2U] =
        (uint8_t)((value >> 16U) & UINT32_C(0xff));
    bytes[offset + 3U] = (uint8_t)(value >> 24U);
}

size_t lord_save_encode(const lord_state_t *state, uint8_t *bytes,
                        size_t capacity)
{
    if (state == NULL || bytes == NULL ||
        capacity < LORD_SAVE_HEADER_BYTES) {
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
    save_write_u8(&writer, state->spouse_index < 0 ? 0U :
                  (uint8_t)((uint8_t)state->spouse_index + 1U));
    save_write_u8(&writer, state->pvp_fights);
    save_write_u8(&writer, state->romance_actions);
    save_write_u8(&writer, state->igm_used_mask);
    save_write_u8(&writer, state->rip_scene);
    save_write_u8(&writer, state->mail_count);

    save_write_u8(&writer, (uint8_t)state->player.hero_class);
    save_write_u8(&writer, state->player.level);
    save_write_u8(&writer, state->player.weapon);
    save_write_u8(&writer, state->player.armor);
    save_write_i32(&writer, state->player.hit_points);
    save_write_i32(&writer, state->player.max_hit_points);
    save_write_i32(&writer, state->player.strength);
    save_write_i32(&writer, state->player.defense);
    save_write_u32(&writer, state->player.gold);
    save_write_u32(&writer, state->player.bank);
    save_write_u32(&writer, state->player.experience);
    save_write_u16(&writer, state->player.forest_fights);
    save_write_u8(&writer, state->player.skill_uses);
    save_write_u8(&writer, state->player.dragon_kills);
    save_write_u16(&writer, state->player.day);
    save_write_u16(&writer, state->player.pvp_wins);
    save_write_u16(&writer, state->player.pvp_losses);

    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        const lord_realm_player_t *const player = &state->realm[index];
        save_write_chars(&writer, player->name, sizeof(player->name));
        save_write_u8(&writer, (uint8_t)player->hero_class);
        save_write_u8(&writer, player->level);
        save_write_u8(&writer, player->alive ? 1U : 0U);
        save_write_u8(&writer, player->married ? 1U : 0U);
        save_write_u8(&writer, player->affection);
        save_write_i32(&writer, player->hit_points);
        save_write_i32(&writer, player->max_hit_points);
        save_write_i32(&writer, player->strength);
        save_write_i32(&writer, player->defense);
        save_write_u32(&writer, player->gold);
        save_write_u32(&writer, player->experience);
    }
    for (size_t index = 0U; index < LORD_MAIL_COUNT_MAX; ++index) {
        save_write_u8(&writer, state->mail[index].sender);
        save_write_u8(&writer, (uint8_t)state->mail[index].kind);
        save_write_u8(&writer, state->mail[index].unread ? 1U : 0U);
    }
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

static bool save_player_valid(const lord_state_t *state)
{
    return state->player.hero_class <= LORD_CLASS_THIEF &&
        state->player.level >= 1U && state->player.level <= LORD_MAX_LEVEL &&
        state->player.weapon < LORD_WEAPON_COUNT &&
        state->player.armor < LORD_ARMOR_COUNT &&
        state->player.max_hit_points > 0 &&
        state->player.hit_points >= 0 &&
        state->player.hit_points <= state->player.max_hit_points &&
        state->player.strength > 0 && state->player.defense >= 0 &&
        state->player.forest_fights <= LORD_FOREST_FIGHTS_PER_DAY &&
        state->player.skill_uses <= LORD_CLASS_SKILLS_PER_DAY &&
        state->player.day > 0U;
}

static bool save_realm_valid(lord_state_t *state)
{
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        lord_realm_player_t *const player = &state->realm[index];
        player->name[sizeof(player->name) - 1U] = '\0';
        if (player->name[0] == '\0' ||
            player->hero_class > LORD_CLASS_THIEF ||
            player->level < 1U || player->level > LORD_MAX_LEVEL ||
            player->affection > 100U || player->max_hit_points <= 0 ||
            player->hit_points < 0 ||
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
    if (state == NULL || bytes == NULL ||
        length < LORD_SAVE_HEADER_BYTES || length > LORD_SAVE_MAX_BYTES ||
        bytes[0] != (uint8_t)'L' || bytes[1] != (uint8_t)'D' ||
        bytes[2] != (uint8_t)'S' || bytes[3] != (uint8_t)'V') {
        return false;
    }
    lord_save_reader_t header = {
        .bytes = bytes,
        .length = length,
        .offset = 4U,
        .valid = true,
    };
    const uint16_t version = save_read_u16(&header);
    const uint16_t stored_length = save_read_u16(&header);
    const uint32_t stored_crc = save_read_u32(&header);
    const uint32_t save_sequence = save_read_u32(&header);
    if (!header.valid || version != LORD_SAVE_FORMAT_VERSION ||
        stored_length != length ||
        stored_crc != save_crc32(bytes + LORD_SAVE_HEADER_BYTES,
                                 length - LORD_SAVE_HEADER_BYTES)) {
        return false;
    }

    lord_state_t loaded;
    lord_initialize(&loaded, UINT32_C(0x4c4f5244));
    loaded.save_sequence = save_sequence;
    lord_save_reader_t reader = {
        .bytes = bytes,
        .length = length,
        .offset = LORD_SAVE_HEADER_BYTES,
        .valid = true,
    };
    loaded.rng_state = save_read_u32(&reader);
    loaded.realm_revision = save_read_u32(&reader);
    const uint8_t spouse_code = save_read_u8(&reader);
    loaded.spouse_index = spouse_code == 0U ? -1 :
        (int8_t)(spouse_code - 1U);
    loaded.pvp_fights = save_read_u8(&reader);
    loaded.romance_actions = save_read_u8(&reader);
    loaded.igm_used_mask = save_read_u8(&reader);
    loaded.rip_scene = save_read_u8(&reader);
    loaded.mail_count = save_read_u8(&reader);

    loaded.player.hero_class = (lord_class_t)save_read_u8(&reader);
    loaded.player.level = save_read_u8(&reader);
    loaded.player.weapon = save_read_u8(&reader);
    loaded.player.armor = save_read_u8(&reader);
    loaded.player.hit_points = save_read_i32(&reader);
    loaded.player.max_hit_points = save_read_i32(&reader);
    loaded.player.strength = save_read_i32(&reader);
    loaded.player.defense = save_read_i32(&reader);
    loaded.player.gold = save_read_u32(&reader);
    loaded.player.bank = save_read_u32(&reader);
    loaded.player.experience = save_read_u32(&reader);
    loaded.player.forest_fights = save_read_u16(&reader);
    loaded.player.skill_uses = save_read_u8(&reader);
    loaded.player.dragon_kills = save_read_u8(&reader);
    loaded.player.day = save_read_u16(&reader);
    loaded.player.pvp_wins = save_read_u16(&reader);
    loaded.player.pvp_losses = save_read_u16(&reader);

    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        lord_realm_player_t *const player = &loaded.realm[index];
        save_read_chars(&reader, player->name, sizeof(player->name));
        player->hero_class = (lord_class_t)save_read_u8(&reader);
        player->level = save_read_u8(&reader);
        player->alive = save_read_u8(&reader) != 0U;
        player->married = save_read_u8(&reader) != 0U;
        player->affection = save_read_u8(&reader);
        player->hit_points = save_read_i32(&reader);
        player->max_hit_points = save_read_i32(&reader);
        player->strength = save_read_i32(&reader);
        player->defense = save_read_i32(&reader);
        player->gold = save_read_u32(&reader);
        player->experience = save_read_u32(&reader);
    }
    for (size_t index = 0U; index < LORD_MAIL_COUNT_MAX; ++index) {
        loaded.mail[index].sender = save_read_u8(&reader);
        loaded.mail[index].kind = (lord_mail_kind_t)save_read_u8(&reader);
        loaded.mail[index].unread = save_read_u8(&reader) != 0U;
    }
    if (!reader.valid || reader.offset != length ||
        loaded.rng_state == 0U || loaded.realm_revision == 0U ||
        spouse_code > LORD_REALM_PLAYER_COUNT ||
        loaded.pvp_fights > LORD_PVP_FIGHTS_PER_DAY ||
        loaded.romance_actions > LORD_ROMANCE_ACTIONS_PER_DAY ||
        loaded.igm_used_mask >= (uint8_t)(1U << LORD_IGM_COUNT) ||
        loaded.rip_scene >= LORD_RIP_SCENE_COUNT ||
        loaded.mail_count > LORD_MAIL_COUNT_MAX ||
        !save_player_valid(&loaded) || !save_realm_valid(&loaded)) {
        return false;
    }
    if (loaded.spouse_index >= 0 &&
        !loaded.realm[(size_t)loaded.spouse_index].married) {
        return false;
    }
    for (size_t index = 0U; index < LORD_MAIL_COUNT_MAX; ++index) {
        if (loaded.mail[index].sender > LORD_REALM_PLAYER_COUNT + 1U ||
            loaded.mail[index].kind > LORD_MAIL_PROPOSAL) {
            return false;
        }
    }
    loaded.screen = LORD_SCREEN_TOWN;
    loaded.return_screen = LORD_SCREEN_TOWN;
    loaded.battle_kind = LORD_BATTLE_NONE;
    loaded.selection = 0U;
    loaded.menu_scroll = 0U;
    loaded.held_buttons = 0U;
    loaded.save_dirty = false;
    loaded.message_line_1[0] = '\0';
    loaded.message_line_2[0] = '\0';
    loaded.battle_line[0] = '\0';
    memcpy(state, &loaded, sizeof(loaded));
    return true;
}
