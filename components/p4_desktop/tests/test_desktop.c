// SPDX-License-Identifier: MIT

#include "p4/desktop.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(value) do { if (!(value)) { ++failures; } } while (0)

int main(void)
{
    p4_file_list_t files;
    p4_file_list_init(&files);
    CHECK(p4_file_list_add(&files, "B.P4CART", 2048U,
                           P4_FILE_KIND_CARTRIDGE, false));
    CHECK(p4_file_list_add(&files, "A.P4CART", 4096U,
                           P4_FILE_KIND_CARTRIDGE, false));
    CHECK(strcmp(files.entries[0].name, "A.P4CART") == 0);
    p4_file_list_set_sort(&files, P4_FILE_SORT_SIZE, true);
    CHECK(files.entries[0].size_bytes == 4096U);
    char size[16];
    p4_format_file_size(UINT64_C(18689235), size);
    CHECK(strcmp(size, "17.8 MB") == 0);

    p4_save_catalog_t saves;
    p4_save_catalog_init(&saves, false);
    CHECK(p4_save_catalog_add(
        &saves, "org.p4console.solitaire", "AUTO", 512U, 1U));
    CHECK(saves.count == 1U && saves.total_bytes == 512U);

    p4_terminal_t terminal;
    p4_terminal_init(&terminal, false);
    CHECK(p4_terminal_input_char(&terminal, 's'));
    CHECK(p4_terminal_input_char(&terminal, 's'));
    CHECK(p4_terminal_input_char(&terminal, 'h'));
    CHECK(p4_terminal_submit(&terminal) == P4_TERMINAL_COMMAND_SSH);
    CHECK(strstr(terminal.lines[terminal.line_count - 1U], "OFFLINE") != NULL);
    CHECK(!terminal.ssh_connected);

    if (failures != 0) {
        fprintf(stderr, "%d desktop service failure(s)\n", failures);
        return 1;
    }
    puts("desktop service tests passed");
    return 0;
}
