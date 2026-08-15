// SPDX-License-Identifier: MIT

#include "p4/game_package.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "p4/game.h"

enum {
    HEADER_MAGIC = 0,
    HEADER_BYTES = 8,
    HEADER_PACKAGE_BYTES = 12,
    HEADER_PAYLOAD_OFFSET = 16,
    HEADER_PAYLOAD_BYTES = 20,
    HEADER_FORMAT_VERSION = 24,
    HEADER_API_VERSION = 28,
    HEADER_LAUNCHER_ID = 32,
    HEADER_REQUIRED_CAPS = 36,
    HEADER_OPTIONAL_CAPS = 40,
    HEADER_ACCENT = 44,
    HEADER_FLAGS = 46,
    HEADER_SHA256 = 48,
    HEADER_ID = 80,
    HEADER_TITLE = 128,
    HEADER_SUBTITLE = 144,
    HEADER_FOLDER = 176,
    HEADER_VERSION_TEXT = 208,
    HEADER_LICENSE = 224,
    HEADER_RESERVED = 240,
    HEADER_RESERVED_BYTES = 16,

    ELF_HEADER_BYTES = 52,
    ELF_PROGRAM_HEADER_BYTES = 32,
    ELF_SECTION_HEADER_BYTES = 40,
    ELF_SYMBOL_BYTES = 16,
    ELF_RELOCATION_BYTES = 12,
    ELF_MACHINE_RISCV = 243,
    ELF_TYPE_DYNAMIC = 3,
    ELF_PT_LOAD = 1,
    ELF_PF_EXECUTE = 1,
    ELF_SHT_NULL = 0,
    ELF_SHT_PROGBITS = 1,
    ELF_SHT_SYMTAB = 2,
    ELF_SHT_STRTAB = 3,
    ELF_SHT_RELA = 4,
    ELF_SHT_NOBITS = 8,
    ELF_SHT_DYNSYM = 11,
    ELF_MAX_PROGRAM_HEADERS = 12,
    ELF_MAX_SECTION_HEADERS = 96,
    ELF_MAX_RELOCATIONS = 8192,
    ELF_MAX_MEMORY_BYTES = 512 * 1024,
};

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | (uint16_t)data[1] << 8U);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8U |
        (uint32_t)data[2] << 16U | (uint32_t)data[3] << 24U;
}

static bool range_valid(size_t total, uint32_t offset, uint32_t bytes)
{
    return (size_t)offset <= total && (size_t)bytes <= total - offset;
}

static bool table_valid(size_t total, uint32_t offset,
                        uint16_t count, uint16_t entry_bytes)
{
    return count == 0U ||
        ((size_t)count <= SIZE_MAX / entry_bytes &&
         (size_t)offset <= total &&
         (size_t)count * entry_bytes <= total - offset);
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

static bool text_copy(char *destination, size_t destination_bytes,
                      const uint8_t *source, size_t source_bytes,
                      bool allow_empty)
{
    const void *const terminator = memchr(source, 0, source_bytes);
    if (destination == NULL || destination_bytes != source_bytes ||
        terminator == NULL) {
        return false;
    }
    const size_t length = (size_t)((const uint8_t *)terminator - source);
    if ((!allow_empty && length == 0U) ||
        !all_zero(source + length + 1U, source_bytes - length - 1U)) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const uint8_t byte = source[index];
        if (byte < UINT8_C(0x20) || byte > UINT8_C(0x7e)) {
            return false;
        }
    }
    memcpy(destination, source, source_bytes);
    return true;
}

static bool id_valid(const char *text)
{
    const size_t length = strlen(text);
    if (length < 3U || length >= P4_GAME_PACKAGE_ID_BYTES ||
        text[0] < 'a' || text[0] > 'z') {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const char byte = text[index];
        if (!((byte >= 'a' && byte <= 'z') ||
              (byte >= '0' && byte <= '9') || byte == '.' || byte == '-')) {
            return false;
        }
    }
    return true;
}

static bool folder_valid(const char *text)
{
    const size_t length = strlen(text);
    if (length == 0U || length >= P4_GAME_PACKAGE_FOLDER_BYTES) {
        return false;
    }
    unsigned slashes = 0U;
    size_t segment = 0U;
    for (size_t index = 0U; index < length; ++index) {
        const char byte = text[index];
        if (byte == '/') {
            if (segment == 0U || segment > 15U || slashes != 0U) {
                return false;
            }
            ++slashes;
            segment = 0U;
            continue;
        }
        if (!((byte >= 'A' && byte <= 'Z') ||
              (byte >= '0' && byte <= '9') || byte == ' ' || byte == '-') ||
            (segment == 0U && (byte == ' ' || byte == '-')) ||
            ++segment > 15U) {
            return false;
        }
    }
    return segment > 0U;
}

static bool string_reference_valid(const uint8_t *table,
                                   uint32_t table_bytes,
                                   uint32_t offset)
{
    return table != NULL && offset < table_bytes &&
        memchr(table + offset, 0, table_bytes - offset) != NULL;
}

static bool symbol_name_allowed(const uint8_t *table, uint32_t table_bytes,
                                uint32_t offset)
{
    static const char *const allowed[] = {
        "calloc", "free", "memcmp", "memcpy", "memset", "strcmp",
    };
    if (!string_reference_valid(table, table_bytes, offset)) {
        return false;
    }
    const char *const name = (const char *)(table + offset);
    if (name[0] == '\0') {
        return true;
    }
    for (size_t index = 0U;
         index < sizeof(allowed) / sizeof(allowed[0]); ++index) {
        if (strcmp(name, allowed[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool section_header(const uint8_t *elf, size_t size_bytes,
                           uint32_t section_table, uint16_t section_count,
                           uint16_t index, const uint8_t **out_section)
{
    if (index >= section_count || out_section == NULL ||
        !range_valid(size_bytes, section_table,
                     (uint32_t)section_count * ELF_SECTION_HEADER_BYTES)) {
        return false;
    }
    *out_section = elf + section_table +
        (size_t)index * ELF_SECTION_HEADER_BYTES;
    return true;
}

p4_game_package_result_t p4_game_package_validate_elf(
    const uint8_t *elf, size_t size_bytes)
{
    if (elf == NULL) {
        return P4_GAME_PACKAGE_BAD_ARGUMENT;
    }
    if (size_bytes < ELF_HEADER_BYTES || size_bytes > P4_GAME_PACKAGE_MAX_BYTES ||
        memcmp(elf, "\x7f" "ELF", 4U) != 0 || elf[4] != 1U ||
        elf[5] != 1U || elf[6] != 1U ||
        read_u16(elf + 16U) != ELF_TYPE_DYNAMIC ||
        read_u16(elf + 18U) != ELF_MACHINE_RISCV ||
        read_u32(elf + 20U) != 1U ||
        read_u16(elf + 40U) != ELF_HEADER_BYTES) {
        return P4_GAME_PACKAGE_BAD_ELF;
    }

    const uint32_t entry = read_u32(elf + 24U);
    const uint32_t program_table = read_u32(elf + 28U);
    const uint32_t section_table = read_u32(elf + 32U);
    const uint16_t program_entry_bytes = read_u16(elf + 42U);
    const uint16_t program_count = read_u16(elf + 44U);
    const uint16_t section_entry_bytes = read_u16(elf + 46U);
    const uint16_t section_count = read_u16(elf + 48U);
    const uint16_t string_section = read_u16(elf + 50U);
    if (program_count == 0U || program_count > ELF_MAX_PROGRAM_HEADERS ||
        program_entry_bytes != ELF_PROGRAM_HEADER_BYTES ||
        section_count == 0U || section_count > ELF_MAX_SECTION_HEADERS ||
        section_entry_bytes != ELF_SECTION_HEADER_BYTES ||
        string_section >= section_count ||
        !table_valid(size_bytes, program_table, program_count,
                     program_entry_bytes) ||
        !table_valid(size_bytes, section_table, section_count,
                     section_entry_bytes)) {
        return P4_GAME_PACKAGE_BAD_ELF;
    }

    uint32_t memory_end = 0U;
    bool load_seen = false;
    bool entry_executable = false;
    for (uint16_t index = 0U; index < program_count; ++index) {
        const uint8_t *const program = elf + program_table +
            (size_t)index * ELF_PROGRAM_HEADER_BYTES;
        if (read_u32(program) != ELF_PT_LOAD) {
            continue;
        }
        const uint32_t offset = read_u32(program + 4U);
        const uint32_t virtual_address = read_u32(program + 8U);
        const uint32_t file_bytes = read_u32(program + 16U);
        const uint32_t memory_bytes = read_u32(program + 20U);
        const uint32_t flags = read_u32(program + 24U);
        const uint32_t alignment = read_u32(program + 28U);
        if (virtual_address > UINT32_MAX - memory_bytes ||
            file_bytes > memory_bytes ||
            !range_valid(size_bytes, offset, file_bytes) ||
            (alignment != 0U &&
             ((alignment & (alignment - 1U)) != 0U || alignment > 4096U))) {
            return P4_GAME_PACKAGE_BAD_ELF;
        }
        const uint32_t end = virtual_address + memory_bytes;
        if (!load_seen && virtual_address != 0U) {
            return P4_GAME_PACKAGE_BAD_ELF;
        }
        load_seen = true;
        if (end > memory_end) {
            memory_end = end;
        }
        if ((flags & ELF_PF_EXECUTE) != 0U && entry >= virtual_address &&
            entry < end) {
            entry_executable = true;
        }
    }
    if (!load_seen || !entry_executable || memory_end < sizeof(uint32_t) ||
        memory_end > ELF_MAX_MEMORY_BYTES) {
        return P4_GAME_PACKAGE_BAD_ELF;
    }

    const uint8_t *section_names_header = NULL;
    if (!section_header(elf, size_bytes, section_table, section_count,
                        string_section, &section_names_header) ||
        read_u32(section_names_header + 4U) != ELF_SHT_STRTAB ||
        !range_valid(size_bytes, read_u32(section_names_header + 16U),
                     read_u32(section_names_header + 20U))) {
        return P4_GAME_PACKAGE_BAD_ELF;
    }
    const uint8_t *const section_names =
        elf + read_u32(section_names_header + 16U);
    const uint32_t section_names_bytes =
        read_u32(section_names_header + 20U);

    size_t relocation_total = 0U;
    for (uint16_t index = 0U; index < section_count; ++index) {
        const uint8_t *section = NULL;
        if (!section_header(elf, size_bytes, section_table, section_count,
                            index, &section)) {
            return P4_GAME_PACKAGE_BAD_ELF;
        }
        const uint32_t type = read_u32(section + 4U);
        const uint32_t offset = read_u32(section + 16U);
        const uint32_t bytes = read_u32(section + 20U);
        if (!string_reference_valid(
                section_names, section_names_bytes, read_u32(section)) ||
            (type != ELF_SHT_NOBITS &&
             !range_valid(size_bytes, offset, bytes))) {
            return P4_GAME_PACKAGE_BAD_ELF;
        }
        if (type != ELF_SHT_RELA) {
            continue;
        }
        const uint32_t symbol_index = read_u32(section + 24U);
        const uint32_t target_index = read_u32(section + 28U);
        const uint32_t entry_bytes = read_u32(section + 36U);
        const uint8_t *symbols = NULL;
        const uint8_t *strings = NULL;
        if (entry_bytes != ELF_RELOCATION_BYTES ||
            bytes % ELF_RELOCATION_BYTES != 0U ||
            target_index >= section_count ||
            !section_header(elf, size_bytes, section_table, section_count,
                            (uint16_t)symbol_index, &symbols) ||
            (read_u32(symbols + 4U) != ELF_SHT_SYMTAB &&
             read_u32(symbols + 4U) != ELF_SHT_DYNSYM) ||
            read_u32(symbols + 36U) != ELF_SYMBOL_BYTES ||
            read_u32(symbols + 20U) % ELF_SYMBOL_BYTES != 0U ||
            !range_valid(size_bytes, read_u32(symbols + 16U),
                         read_u32(symbols + 20U)) ||
            !section_header(elf, size_bytes, section_table, section_count,
                            (uint16_t)read_u32(symbols + 24U), &strings) ||
            read_u32(strings + 4U) != ELF_SHT_STRTAB ||
            !range_valid(size_bytes, read_u32(strings + 16U),
                         read_u32(strings + 20U))) {
            return P4_GAME_PACKAGE_BAD_ELF;
        }
        const uint32_t symbol_count =
            read_u32(symbols + 20U) / ELF_SYMBOL_BYTES;
        const uint8_t *const symbol_data =
            elf + read_u32(symbols + 16U);
        const uint8_t *const string_data =
            elf + read_u32(strings + 16U);
        const uint32_t string_bytes = read_u32(strings + 20U);
        const uint32_t relocation_count = bytes / ELF_RELOCATION_BYTES;
        relocation_total += relocation_count;
        if (relocation_total > ELF_MAX_RELOCATIONS) {
            return P4_GAME_PACKAGE_BAD_ELF;
        }
        for (uint32_t item = 0U; item < relocation_count; ++item) {
            const uint8_t *const relocation = elf + offset +
                (size_t)item * ELF_RELOCATION_BYTES;
            const uint32_t target = read_u32(relocation);
            const uint32_t info = read_u32(relocation + 4U);
            const uint32_t relocation_type = info & UINT32_C(0xff);
            const uint32_t symbol = info >> 8U;
            if (target > memory_end - sizeof(uint32_t) ||
                symbol >= symbol_count ||
                (relocation_type != 0U && relocation_type != 1U &&
                 relocation_type != 3U && relocation_type != 5U)) {
                return P4_GAME_PACKAGE_BAD_ELF;
            }
            const uint8_t *const symbol_entry = symbol_data +
                (size_t)symbol * ELF_SYMBOL_BYTES;
            const uint16_t symbol_section = read_u16(symbol_entry + 14U);
            const uint32_t symbol_name = read_u32(symbol_entry);
            if (!string_reference_valid(
                    string_data, string_bytes, symbol_name) ||
                (symbol_section == 0U &&
                 !symbol_name_allowed(
                     string_data, string_bytes, symbol_name))) {
                return P4_GAME_PACKAGE_BAD_ELF;
            }
        }
    }
    return P4_GAME_PACKAGE_VALID;
}

p4_game_package_result_t p4_game_package_parse(
    const uint8_t *data, size_t size_bytes, p4_game_package_info_t *out_info)
{
    if (data == NULL || out_info == NULL) {
        return P4_GAME_PACKAGE_BAD_ARGUMENT;
    }
    memset(out_info, 0, sizeof(*out_info));
    if (size_bytes < P4_GAME_PACKAGE_HEADER_BYTES ||
        size_bytes > P4_GAME_PACKAGE_MAX_BYTES) {
        return P4_GAME_PACKAGE_BAD_SIZE;
    }
    if (memcmp(data + HEADER_MAGIC, P4_GAME_PACKAGE_MAGIC, 8U) != 0) {
        return P4_GAME_PACKAGE_BAD_MAGIC;
    }
    if (read_u32(data + HEADER_BYTES) != P4_GAME_PACKAGE_HEADER_BYTES ||
        read_u32(data + HEADER_FORMAT_VERSION) !=
            P4_GAME_PACKAGE_FORMAT_VERSION ||
        read_u32(data + HEADER_API_VERSION) !=
            P4_GAME_PACKAGE_API_VERSION) {
        return P4_GAME_PACKAGE_BAD_VERSION;
    }
    out_info->package_bytes = read_u32(data + HEADER_PACKAGE_BYTES);
    out_info->payload_offset = read_u32(data + HEADER_PAYLOAD_OFFSET);
    out_info->payload_bytes = read_u32(data + HEADER_PAYLOAD_BYTES);
    if (out_info->package_bytes != size_bytes ||
        out_info->payload_offset != P4_GAME_PACKAGE_HEADER_BYTES ||
        out_info->payload_bytes !=
            out_info->package_bytes - out_info->payload_offset ||
        !range_valid(size_bytes, out_info->payload_offset,
                     out_info->payload_bytes) ||
        !all_zero(data + HEADER_RESERVED, HEADER_RESERVED_BYTES)) {
        return P4_GAME_PACKAGE_BAD_LAYOUT;
    }

    out_info->launcher_id = read_u32(data + HEADER_LAUNCHER_ID);
    out_info->required_capabilities = read_u32(data + HEADER_REQUIRED_CAPS);
    out_info->optional_capabilities = read_u32(data + HEADER_OPTIONAL_CAPS);
    out_info->accent_rgb565 = read_u16(data + HEADER_ACCENT);
    out_info->flags = read_u16(data + HEADER_FLAGS);
    memcpy(out_info->payload_sha256, data + HEADER_SHA256,
           sizeof(out_info->payload_sha256));
    if (!text_copy(out_info->id, sizeof(out_info->id),
                   data + HEADER_ID, P4_GAME_PACKAGE_ID_BYTES, false) ||
        !text_copy(out_info->title, sizeof(out_info->title),
                   data + HEADER_TITLE, P4_GAME_PACKAGE_TITLE_BYTES, false) ||
        !text_copy(out_info->subtitle, sizeof(out_info->subtitle),
                   data + HEADER_SUBTITLE,
                   P4_GAME_PACKAGE_SUBTITLE_BYTES, true) ||
        !text_copy(out_info->folder, sizeof(out_info->folder),
                   data + HEADER_FOLDER, P4_GAME_PACKAGE_FOLDER_BYTES, false) ||
        !text_copy(out_info->version, sizeof(out_info->version),
                   data + HEADER_VERSION_TEXT,
                   P4_GAME_PACKAGE_VERSION_BYTES, false) ||
        !text_copy(out_info->license, sizeof(out_info->license),
                   data + HEADER_LICENSE,
                   P4_GAME_PACKAGE_LICENSE_BYTES, false)) {
        return P4_GAME_PACKAGE_BAD_METADATA;
    }
    const uint32_t known_capabilities =
        P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
        P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_AUDIO_STREAM |
        P4_GAME_CAP_STORAGE;
    if (out_info->launcher_id < 100U || !id_valid(out_info->id) ||
        !folder_valid(out_info->folder) ||
        (out_info->required_capabilities & P4_GAME_CAP_VIDEO) == 0U ||
        ((out_info->required_capabilities | out_info->optional_capabilities) &
         ~known_capabilities) != 0U ||
        (out_info->required_capabilities &
         out_info->optional_capabilities) != 0U ||
        (out_info->flags & ~P4_GAME_PACKAGE_FLAG_DEVELOPMENT) != 0U) {
        return P4_GAME_PACKAGE_BAD_METADATA;
    }
    return p4_game_package_validate_elf(
        data + out_info->payload_offset, out_info->payload_bytes);
}

const char *p4_game_package_result_name(p4_game_package_result_t result)
{
    switch (result) {
    case P4_GAME_PACKAGE_VALID: return "valid";
    case P4_GAME_PACKAGE_BAD_ARGUMENT: return "bad-argument";
    case P4_GAME_PACKAGE_BAD_SIZE: return "bad-size";
    case P4_GAME_PACKAGE_BAD_MAGIC: return "bad-magic";
    case P4_GAME_PACKAGE_BAD_VERSION: return "bad-version";
    case P4_GAME_PACKAGE_BAD_LAYOUT: return "bad-layout";
    case P4_GAME_PACKAGE_BAD_METADATA: return "bad-metadata";
    case P4_GAME_PACKAGE_BAD_ELF: return "bad-elf";
    case P4_GAME_PACKAGE_BAD_DIGEST: return "bad-digest";
    default: return "unknown";
    }
}
