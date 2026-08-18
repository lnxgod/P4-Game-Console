// SPDX-License-Identifier: MIT

#include "p4/bbs_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

int main(void)
{
    p4_ansi_terminal_t terminal;
    p4_bbs_launcher_model_t model = {
        .board_name = "GAME CHANGERS AI BBS",
        .section = "[ DOOR GAMES ]",
        .connection = "ONLINE",
        .doors = {
            {.number = 1U, .title = "ASTEROIDS",
             .subtitle = "VECTOR REMIX", .accent = P4_ANSI_COLOR_BRIGHT_CYAN,
             .enabled = true},
            {.number = 2U, .title = "MAZE CHASE",
             .subtitle = "ARCADE", .accent = P4_ANSI_COLOR_YELLOW,
             .enabled = true},
        },
        .door_count = 2U,
        .selected_door = 0U,
        .page = 1U,
        .page_count = 1U,
        .local_board = true,
    };
    CHECK(p4_bbs_build_launcher(&terminal, &model));
    CHECK(terminal.unsupported_sequences == 0U);
    CHECK(terminal.discarded_strings == 0U);
    CHECK(!terminal.cursor_visible);
    CHECK(p4_ansi_cell(&terminal, 1U, 0U)->character == 0xc9U);
    CHECK(p4_ansi_cell(&terminal, 6U, 8U)->character == '[');
    CHECK(p4_ansi_cell(&terminal, 7U, 8U)->character == '0');
    const p4_bbs_hit_t first = p4_bbs_hit_test(&model, 80U, 130U);
    CHECK(first.kind == P4_BBS_HIT_DOOR);
    CHECK(first.door_index == 0U);
    const p4_bbs_hit_t second_column =
        p4_bbs_hit_test(&model, 405U, 130U);
    CHECK(second_column.kind == P4_BBS_HIT_DOOR);
    CHECK(second_column.door_index == 1U);
    model.can_go_up = true;
    const p4_bbs_hit_t back = p4_bbs_hit_test(&model, 62U, 98U);
    CHECK(back.kind == P4_BBS_HIT_BACK);
    p4_bbs_launcher_model_t unterminated = model;
    memset(unterminated.board_name, 'X', sizeof(unterminated.board_name));
    CHECK(!p4_bbs_build_launcher(&terminal, &unterminated));
    model.selected_door = 2U;
    CHECK(!p4_bbs_build_launcher(&terminal, &model));
    model.selected_door = 0U;
    model.door_count = P4_BBS_VISIBLE_DOORS + 1U;
    CHECK(!p4_bbs_build_launcher(&terminal, &model));
    const p4_bbs_boot_model_t boot = {
        .phase = P4_BBS_BOOT_CONNECTED,
        .status = "CONNECT 2400 / CARRIER DETECT",
        .detail = "OPENING GAME CHANGERS AI BBS",
        .progress_step = 5U,
        .progress_total = 5U,
    };
    CHECK(p4_bbs_build_boot_screen(&terminal, &boot));
    CHECK(terminal.scroll_count == 0U);
    CHECK(terminal.unsupported_sequences == 0U);
    CHECK(p4_ansi_cell(&terminal, 1U, 0U)->character == 0xc9U);
    p4_bbs_boot_model_t bad_boot = boot;
    memset(bad_boot.detail, 'X', sizeof(bad_boot.detail));
    CHECK(!p4_bbs_build_boot_screen(&terminal, &bad_boot));
    if (s_failures != 0) {
        fprintf(stderr, "%d p4 BBS test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 BBS UI tests passed");
    return EXIT_SUCCESS;
}
