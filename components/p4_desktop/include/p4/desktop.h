// SPDX-License-Identifier: MIT

#ifndef P4_DESKTOP_H
#define P4_DESKTOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_DESKTOP_MAX_FILES = 16,
    P4_DESKTOP_FILE_NAME_BYTES = 40,
    P4_DESKTOP_MAX_SAVE_SLOTS = 8,
    P4_DESKTOP_SAVE_GAME_ID_BYTES = 40,
    P4_DESKTOP_SAVE_SLOT_NAME_BYTES = 24,
    P4_TERMINAL_INPUT_BYTES = 48,
    P4_TERMINAL_LINE_BYTES = 48,
    P4_TERMINAL_SCROLLBACK_LINES = 5,
};

typedef enum {
    P4_FILE_KIND_FOLDER = 0,
    P4_FILE_KIND_BUILTIN_GAME,
    P4_FILE_KIND_CARTRIDGE,
    P4_FILE_KIND_SAVE,
    P4_FILE_KIND_GAME_DATA,
    P4_FILE_KIND_OTHER,
} p4_file_kind_t;

typedef enum {
    P4_FILE_SORT_NAME = 0,
    P4_FILE_SORT_SIZE,
    P4_FILE_SORT_TYPE,
} p4_file_sort_t;

typedef struct {
    char name[P4_DESKTOP_FILE_NAME_BYTES];
    uint64_t size_bytes;
    p4_file_kind_t kind;
    bool read_only;
} p4_file_entry_t;

typedef struct {
    p4_file_entry_t entries[P4_DESKTOP_MAX_FILES];
    size_t count;
    p4_file_sort_t sort;
    bool descending;
    bool truncated;
} p4_file_list_t;

void p4_file_list_init(p4_file_list_t *list);
bool p4_file_list_add(p4_file_list_t *list,
                      const char *name,
                      uint64_t size_bytes,
                      p4_file_kind_t kind,
                      bool read_only);
void p4_file_list_set_sort(p4_file_list_t *list,
                           p4_file_sort_t sort,
                           bool descending);
const char *p4_file_kind_name(p4_file_kind_t kind);
void p4_format_file_size(uint64_t size_bytes, char output[16]);

typedef struct {
    char game_id[P4_DESKTOP_SAVE_GAME_ID_BYTES];
    char slot_name[P4_DESKTOP_SAVE_SLOT_NAME_BYTES];
    uint64_t size_bytes;
    uint32_t sequence;
    bool valid;
} p4_save_slot_t;

typedef struct {
    p4_save_slot_t slots[P4_DESKTOP_MAX_SAVE_SLOTS];
    size_t count;
    uint64_t total_bytes;
    bool writable;
    bool truncated;
} p4_save_catalog_t;

void p4_save_catalog_init(p4_save_catalog_t *catalog, bool writable);
bool p4_save_catalog_add(p4_save_catalog_t *catalog,
                         const char *game_id,
                         const char *slot_name,
                         uint64_t size_bytes,
                         uint32_t sequence);

typedef enum {
    P4_TERMINAL_COMMAND_NONE = 0,
    P4_TERMINAL_COMMAND_HELP,
    P4_TERMINAL_COMMAND_STATUS,
    P4_TERMINAL_COMMAND_GAMES,
    P4_TERMINAL_COMMAND_FILES,
    P4_TERMINAL_COMMAND_CLEAR,
    P4_TERMINAL_COMMAND_SSH,
    P4_TERMINAL_COMMAND_UNKNOWN,
} p4_terminal_command_t;

typedef struct {
    char input[P4_TERMINAL_INPUT_BYTES];
    char lines[P4_TERMINAL_SCROLLBACK_LINES][P4_TERMINAL_LINE_BYTES];
    size_t input_length;
    size_t line_count;
    bool network_ready;
    bool ssh_connected;
} p4_terminal_t;

void p4_terminal_init(p4_terminal_t *terminal, bool network_ready);
bool p4_terminal_input_char(p4_terminal_t *terminal, char character);
bool p4_terminal_backspace(p4_terminal_t *terminal);
void p4_terminal_write_line(p4_terminal_t *terminal, const char *line);
p4_terminal_command_t p4_terminal_submit(p4_terminal_t *terminal);

#ifdef __cplusplus
}
#endif

#endif
