// SPDX-License-Identifier: MIT

#include "p4/desktop.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static size_t bounded_length(const char *text, size_t capacity)
{
    if (text == NULL) {
        return capacity;
    }
    size_t length = 0U;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool copy_bounded(char *output, size_t capacity, const char *input)
{
    if (output == NULL || capacity == 0U || input == NULL) {
        return false;
    }
    const size_t length = bounded_length(input, capacity);
    if (length == 0U || length >= capacity) {
        return false;
    }
    memcpy(output, input, length + 1U);
    return true;
}

static int ascii_case_compare(const char *left, const char *right)
{
    for (size_t index = 0U;; ++index) {
        unsigned char a = (unsigned char)left[index];
        unsigned char b = (unsigned char)right[index];
        if (a >= (unsigned char)'a' && a <= (unsigned char)'z') {
            a = (unsigned char)(a - (unsigned char)'a' + (unsigned char)'A');
        }
        if (b >= (unsigned char)'a' && b <= (unsigned char)'z') {
            b = (unsigned char)(b - (unsigned char)'a' + (unsigned char)'A');
        }
        if (a != b) {
            return a < b ? -1 : 1;
        }
        if (a == 0U) {
            return 0;
        }
    }
}

static int compare_entries(const p4_file_entry_t *left,
                           const p4_file_entry_t *right,
                           p4_file_sort_t sort)
{
    int result = 0;
    if (sort == P4_FILE_SORT_SIZE && left->size_bytes != right->size_bytes) {
        result = left->size_bytes < right->size_bytes ? -1 : 1;
    } else if (sort == P4_FILE_SORT_TYPE && left->kind != right->kind) {
        result = left->kind < right->kind ? -1 : 1;
    }
    return result != 0 ? result : ascii_case_compare(left->name, right->name);
}

static void sort_files(p4_file_list_t *list)
{
    if (list == NULL) {
        return;
    }
    for (size_t index = 1U; index < list->count; ++index) {
        const p4_file_entry_t moving = list->entries[index];
        size_t location = index;
        while (location > 0U) {
            int order = compare_entries(
                &list->entries[location - 1U], &moving, list->sort);
            if (list->descending) {
                order = -order;
            }
            if (order <= 0) {
                break;
            }
            list->entries[location] = list->entries[location - 1U];
            --location;
        }
        list->entries[location] = moving;
    }
}

void p4_file_list_init(p4_file_list_t *list)
{
    if (list != NULL) {
        *list = (p4_file_list_t){.sort = P4_FILE_SORT_NAME};
    }
}

bool p4_file_list_add(p4_file_list_t *list,
                      const char *name,
                      uint64_t size_bytes,
                      p4_file_kind_t kind,
                      bool read_only)
{
    if (list == NULL || kind < P4_FILE_KIND_FOLDER ||
        kind > P4_FILE_KIND_OTHER) {
        return false;
    }
    if (list->count >= P4_DESKTOP_MAX_FILES) {
        list->truncated = true;
        return false;
    }
    p4_file_entry_t entry = {
        .size_bytes = size_bytes,
        .kind = kind,
        .read_only = read_only,
    };
    if (!copy_bounded(entry.name, sizeof(entry.name), name)) {
        return false;
    }
    list->entries[list->count++] = entry;
    sort_files(list);
    return true;
}

void p4_file_list_set_sort(p4_file_list_t *list,
                           p4_file_sort_t sort,
                           bool descending)
{
    if (list == NULL || sort < P4_FILE_SORT_NAME ||
        sort > P4_FILE_SORT_TYPE) {
        return;
    }
    list->sort = sort;
    list->descending = descending;
    sort_files(list);
}

const char *p4_file_kind_name(p4_file_kind_t kind)
{
    switch (kind) {
    case P4_FILE_KIND_FOLDER: return "FOLDER";
    case P4_FILE_KIND_BUILTIN_GAME: return "BUILTIN";
    case P4_FILE_KIND_CARTRIDGE: return "CART";
    case P4_FILE_KIND_SAVE: return "SAVE";
    case P4_FILE_KIND_GAME_DATA: return "DATA";
    case P4_FILE_KIND_OTHER: return "FILE";
    default: return "?";
    }
}

void p4_format_file_size(uint64_t size_bytes, char output[16])
{
    if (output == NULL) {
        return;
    }
    if (size_bytes < UINT64_C(1024)) {
        (void)snprintf(output, 16U, "%llu B",
                       (unsigned long long)size_bytes);
        return;
    }
    uint64_t unit = UINT64_C(1024);
    const char *suffix = "KB";
    if (size_bytes >= UINT64_C(1099511627776)) {
        unit = UINT64_C(1099511627776);
        suffix = "TB";
    } else if (size_bytes >= UINT64_C(1073741824)) {
        unit = UINT64_C(1073741824);
        suffix = "GB";
    } else if (size_bytes >= UINT64_C(1048576)) {
        unit = UINT64_C(1048576);
        suffix = "MB";
    }
    uint64_t whole64 = size_bytes / unit;
    uint32_t tenth = (uint32_t)(((size_bytes % unit) * UINT64_C(10) +
                                 unit / 2U) / unit);
    if (tenth == 10U) {
        ++whole64;
        tenth = 0U;
    }
    const uint32_t whole = (uint32_t)whole64;
    (void)snprintf(output, 16U, "%" PRIu32 ".%" PRIu32 " %s",
                   whole, tenth, suffix);
}

void p4_save_catalog_init(p4_save_catalog_t *catalog, bool writable)
{
    if (catalog != NULL) {
        *catalog = (p4_save_catalog_t){.writable = writable};
    }
}

bool p4_save_catalog_add(p4_save_catalog_t *catalog,
                         const char *game_id,
                         const char *slot_name,
                         uint64_t size_bytes,
                         uint32_t sequence)
{
    if (catalog == NULL) {
        return false;
    }
    if (catalog->count >= P4_DESKTOP_MAX_SAVE_SLOTS) {
        catalog->truncated = true;
        return false;
    }
    p4_save_slot_t slot = {
        .size_bytes = size_bytes,
        .sequence = sequence,
        .valid = true,
    };
    if (!copy_bounded(slot.game_id, sizeof(slot.game_id), game_id) ||
        !copy_bounded(slot.slot_name, sizeof(slot.slot_name), slot_name)) {
        return false;
    }
    if (UINT64_MAX - catalog->total_bytes < size_bytes) {
        return false;
    }
    catalog->slots[catalog->count++] = slot;
    catalog->total_bytes += size_bytes;
    return true;
}

void p4_terminal_write_line(p4_terminal_t *terminal, const char *line)
{
    if (terminal == NULL || line == NULL) {
        return;
    }
    if (terminal->line_count == P4_TERMINAL_SCROLLBACK_LINES) {
        memmove(terminal->lines, terminal->lines[1],
                (P4_TERMINAL_SCROLLBACK_LINES - 1U) *
                    P4_TERMINAL_LINE_BYTES);
        --terminal->line_count;
    }
    char *const destination = terminal->lines[terminal->line_count];
    const size_t length = bounded_length(line, P4_TERMINAL_LINE_BYTES);
    if (length >= P4_TERMINAL_LINE_BYTES) {
        memcpy(destination, line, P4_TERMINAL_LINE_BYTES - 1U);
        destination[P4_TERMINAL_LINE_BYTES - 1U] = '\0';
    } else {
        memcpy(destination, line, length + 1U);
    }
    ++terminal->line_count;
}

void p4_terminal_init(p4_terminal_t *terminal, bool network_ready)
{
    if (terminal == NULL) {
        return;
    }
    *terminal = (p4_terminal_t){.network_ready = network_ready};
    p4_terminal_write_line(terminal, "P4 CONSOLE COMMAND PROMPT");
    p4_terminal_write_line(terminal, "TYPE HELP OR SSH");
}

bool p4_terminal_input_char(p4_terminal_t *terminal, char character)
{
    if (terminal == NULL || character < 32 || character > 126 ||
        terminal->input_length + 1U >= sizeof(terminal->input)) {
        return false;
    }
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    terminal->input[terminal->input_length++] = character;
    terminal->input[terminal->input_length] = '\0';
    return true;
}

bool p4_terminal_backspace(p4_terminal_t *terminal)
{
    if (terminal == NULL || terminal->input_length == 0U) {
        return false;
    }
    terminal->input[--terminal->input_length] = '\0';
    return true;
}

static bool command_equal(const char *input, const char *expected)
{
    return ascii_case_compare(input, expected) == 0;
}

p4_terminal_command_t p4_terminal_submit(p4_terminal_t *terminal)
{
    if (terminal == NULL || terminal->input_length == 0U) {
        return P4_TERMINAL_COMMAND_NONE;
    }
    char prompt[P4_TERMINAL_LINE_BYTES] = "> ";
    const size_t copy = terminal->input_length < sizeof(prompt) - 3U
        ? terminal->input_length : sizeof(prompt) - 3U;
    memcpy(&prompt[2], terminal->input, copy);
    prompt[copy + 2U] = '\0';
    p4_terminal_write_line(terminal, prompt);

    p4_terminal_command_t command = P4_TERMINAL_COMMAND_UNKNOWN;
    if (command_equal(terminal->input, "HELP")) {
        command = P4_TERMINAL_COMMAND_HELP;
        p4_terminal_write_line(terminal, "HELP STATUS GAMES FILES CLEAR SSH");
    } else if (command_equal(terminal->input, "STATUS")) {
        command = P4_TERMINAL_COMMAND_STATUS;
    } else if (command_equal(terminal->input, "GAMES")) {
        command = P4_TERMINAL_COMMAND_GAMES;
    } else if (command_equal(terminal->input, "FILES")) {
        command = P4_TERMINAL_COMMAND_FILES;
    } else if (command_equal(terminal->input, "CLEAR")) {
        command = P4_TERMINAL_COMMAND_CLEAR;
        terminal->line_count = 0U;
    } else if (command_equal(terminal->input, "SSH")) {
        command = P4_TERMINAL_COMMAND_SSH;
        p4_terminal_write_line(terminal, terminal->network_ready
            ? "SSH TRANSPORT READY - PROFILE REQUIRED"
            : "SSH OFFLINE - WIFI TRANSPORT PENDING");
    } else {
        p4_terminal_write_line(terminal, "BAD COMMAND OR FILE NAME");
    }
    terminal->input_length = 0U;
    terminal->input[0] = '\0';
    return command;
}
