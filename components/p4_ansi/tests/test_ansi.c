// SPDX-License-Identifier: MIT

#include "p4/ansi.h"

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

static void write_text(p4_ansi_terminal_t *terminal, const char *text)
{
    CHECK(p4_ansi_write(terminal, (const uint8_t *)text, strlen(text)));
}

static void test_text_and_sgr(void)
{
    p4_ansi_terminal_t terminal;
    p4_ansi_init(&terminal);
    write_text(&terminal, "\x1b[31;44mA\x1b[1mB");
    const p4_ansi_cell_t *cell = p4_ansi_cell(&terminal, 0U, 0U);
    CHECK(cell != NULL);
    CHECK(cell->character == 'A');
    CHECK(cell->foreground == P4_ANSI_COLOR_RED);
    CHECK(cell->background == P4_ANSI_COLOR_BLUE);
    CHECK(cell->attributes == 0U);
    cell = p4_ansi_cell(&terminal, 1U, 0U);
    CHECK(cell != NULL && cell->character == 'B');
    CHECK((cell->attributes & P4_ANSI_ATTR_BOLD) != 0U);
}

static void test_cursor_erase_and_private_mode(void)
{
    p4_ansi_terminal_t terminal;
    p4_ansi_init(&terminal);
    write_text(&terminal, "FIRST\x1b[2;4HSECOND\x1b[2KX\x1b[?25l");
    CHECK(p4_ansi_cell(&terminal, 0U, 0U)->character == 'F');
    CHECK(p4_ansi_cell(&terminal, 9U, 1U)->character == 'X');
    CHECK(p4_ansi_cell(&terminal, 8U, 1U)->character == ' ');
    CHECK(!terminal.cursor_visible);
    write_text(&terminal, "\x1b[?25h");
    CHECK(terminal.cursor_visible);
}

static void test_scroll_and_untrusted_sequences(void)
{
    p4_ansi_terminal_t terminal;
    p4_ansi_init(&terminal);
    for (unsigned row = 0U; row < P4_ANSI_ROWS + 2U; ++row) {
        write_text(&terminal, "X\r\n");
    }
    CHECK(terminal.scroll_count >= 2U);
    write_text(&terminal, "\x1b]0;discard me\x07Y");
    CHECK(terminal.discarded_strings == 1U);
    CHECK(p4_ansi_cell(&terminal, terminal.cursor_column - 1U,
                       terminal.cursor_row)->character == 'Y');
    write_text(&terminal,
               "\x1b[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15mZ");
    CHECK(terminal.unsupported_sequences > 0U);
    CHECK(terminal.parse_state == P4_ANSI_PARSE_TEXT);
}

static void test_cp437_render_bounds(void)
{
    p4_ansi_terminal_t terminal;
    p4_ansi_init(&terminal);
    terminal.cursor_visible = false;
    const uint8_t art[] = {
        0x1bU, '[', '9', '6', ';', '4', '4', 'm',
        0xdaU, 0xc4U, 0xbfU, '\r', '\n',
        0xc0U, 0xc4U, 0xd9U,
    };
    CHECK(p4_ansi_write(&terminal, art, sizeof(art)));
    enum {
        GUARD = 17,
        STRIDE = P4_ANSI_SURFACE_WIDTH + 5,
        WORDS = STRIDE * P4_ANSI_SURFACE_HEIGHT,
    };
    uint16_t *const allocation = calloc(
        GUARD + WORDS + GUARD, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation == NULL) {
        return;
    }
    for (size_t index = 0U; index < GUARD + WORDS + GUARD; ++index) {
        allocation[index] = UINT16_C(0xa55a);
    }
    uint16_t *const frame = allocation + GUARD;
    CHECK(!p4_ansi_render_rgb565(
        &terminal, frame, P4_ANSI_SURFACE_WIDTH - 1U,
        P4_ANSI_SURFACE_WIDTH, P4_ANSI_SURFACE_HEIGHT));
    CHECK(p4_ansi_render_rgb565(
        &terminal, frame, STRIDE,
        P4_ANSI_SURFACE_WIDTH, P4_ANSI_SURFACE_HEIGHT));
    CHECK(frame[24U] != UINT16_C(0xa55a));
    for (size_t index = 0U; index < GUARD; ++index) {
        CHECK(allocation[index] == UINT16_C(0xa55a));
        CHECK(allocation[GUARD + WORDS + index] == UINT16_C(0xa55a));
    }
    for (size_t row = 0U; row < P4_ANSI_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_ANSI_SURFACE_WIDTH;
             column < STRIDE; ++column) {
            CHECK(frame[row * STRIDE + column] == UINT16_C(0xa55a));
        }
    }
    free(allocation);
}

static void test_bold_ascii_uses_larger_glyph(void)
{
    p4_ansi_terminal_t terminal;
    p4_ansi_init(&terminal);
    terminal.cursor_visible = false;
    write_text(&terminal, "\x1b[97mA\x1b[1mA");
    uint16_t *const frame = calloc(
        (size_t)P4_ANSI_SURFACE_WIDTH * P4_ANSI_SURFACE_HEIGHT,
        sizeof(*frame));
    CHECK(frame != NULL);
    if (frame == NULL) {
        return;
    }
    CHECK(p4_ansi_render_rgb565(
        &terminal, frame, P4_ANSI_SURFACE_WIDTH,
        P4_ANSI_SURFACE_WIDTH, P4_ANSI_SURFACE_HEIGHT));
    const size_t left =
        (P4_ANSI_SURFACE_WIDTH - P4_ANSI_TEXT_WIDTH) / 2U;
    size_t normal_pixels = 0U;
    size_t bold_pixels = 0U;
    for (size_t row = 0U; row < P4_ANSI_CELL_HEIGHT; ++row) {
        for (size_t column = 0U; column < P4_ANSI_CELL_WIDTH; ++column) {
            if (frame[row * P4_ANSI_SURFACE_WIDTH + left + column] != 0U) {
                ++normal_pixels;
            }
            if (frame[row * P4_ANSI_SURFACE_WIDTH + left +
                      P4_ANSI_CELL_WIDTH + column] != 0U) {
                ++bold_pixels;
            }
        }
    }
    CHECK(normal_pixels > 0U);
    CHECK(bold_pixels > normal_pixels);
    free(frame);
}

int main(void)
{
    test_text_and_sgr();
    test_cursor_erase_and_private_mode();
    test_scroll_and_untrusted_sequences();
    test_cp437_render_bounds();
    test_bold_ascii_uses_larger_glyph();
    if (s_failures != 0) {
        fprintf(stderr, "%d p4 ANSI test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 ANSI tests passed");
    return EXIT_SUCCESS;
}
