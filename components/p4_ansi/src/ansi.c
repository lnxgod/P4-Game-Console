// SPDX-License-Identifier: MIT

#include "p4/ansi.h"

#include <limits.h>
#include <string.h>

#include "p4/cp437.h"

static const uint16_t s_dos_palette[16] = {
    UINT16_C(0x0000), UINT16_C(0x0015), UINT16_C(0x0540),
    UINT16_C(0x0555), UINT16_C(0xa800), UINT16_C(0xa815),
    UINT16_C(0xaaa0), UINT16_C(0xad55), UINT16_C(0x52aa),
    UINT16_C(0x52bf), UINT16_C(0x57ea), UINT16_C(0x57ff),
    UINT16_C(0xfaaa), UINT16_C(0xfabf), UINT16_C(0xffea),
    UINT16_C(0xffff),
};

/* ECMA-48 SGR color order mapped onto the IBM PC/DOS palette order. */
static const uint8_t s_sgr_colors[8] = {
    P4_ANSI_COLOR_BLACK,
    P4_ANSI_COLOR_RED,
    P4_ANSI_COLOR_GREEN,
    P4_ANSI_COLOR_BROWN,
    P4_ANSI_COLOR_BLUE,
    P4_ANSI_COLOR_MAGENTA,
    P4_ANSI_COLOR_CYAN,
    P4_ANSI_COLOR_LIGHT_GRAY,
};

static const uint8_t s_sgr_bright_colors[8] = {
    P4_ANSI_COLOR_DARK_GRAY,
    P4_ANSI_COLOR_BRIGHT_RED,
    P4_ANSI_COLOR_BRIGHT_GREEN,
    P4_ANSI_COLOR_YELLOW,
    P4_ANSI_COLOR_BRIGHT_BLUE,
    P4_ANSI_COLOR_BRIGHT_MAGENTA,
    P4_ANSI_COLOR_BRIGHT_CYAN,
    P4_ANSI_COLOR_WHITE,
};

static size_t cell_index(size_t column, size_t row)
{
    return row * P4_ANSI_COLUMNS + column;
}

static p4_ansi_cell_t blank_cell(const p4_ansi_terminal_t *terminal)
{
    const p4_ansi_cell_t cell = {
        .character = (uint8_t)' ',
        .foreground = terminal->foreground,
        .background = terminal->background,
        .attributes = terminal->attributes,
    };
    return cell;
}

static void clear_cells(p4_ansi_terminal_t *terminal,
                        size_t first, size_t last)
{
    if (first > last || last > P4_ANSI_COLUMNS * P4_ANSI_ROWS) {
        return;
    }
    const p4_ansi_cell_t blank = blank_cell(terminal);
    for (size_t index = first; index < last; ++index) {
        terminal->cells[index] = blank;
    }
}

static void reset_parser(p4_ansi_terminal_t *terminal)
{
    terminal->parse_state = P4_ANSI_PARSE_TEXT;
    terminal->parameter_count = 0U;
    terminal->sequence_bytes = 0U;
    terminal->parameter_active = false;
    terminal->private_mode = false;
    memset(terminal->parameters, 0, sizeof(terminal->parameters));
}

void p4_ansi_reset(p4_ansi_terminal_t *terminal)
{
    if (terminal == NULL) {
        return;
    }
    memset(terminal, 0, sizeof(*terminal));
    terminal->foreground = P4_ANSI_COLOR_LIGHT_GRAY;
    terminal->background = P4_ANSI_COLOR_BLACK;
    terminal->cursor_visible = true;
    reset_parser(terminal);
    clear_cells(terminal, 0U, P4_ANSI_COLUMNS * P4_ANSI_ROWS);
}

void p4_ansi_init(p4_ansi_terminal_t *terminal)
{
    p4_ansi_reset(terminal);
}

static void scroll_up(p4_ansi_terminal_t *terminal)
{
    const size_t row_cells = P4_ANSI_COLUMNS;
    memmove(terminal->cells, terminal->cells + row_cells,
            (P4_ANSI_ROWS - 1U) * row_cells * sizeof(terminal->cells[0]));
    clear_cells(terminal, (P4_ANSI_ROWS - 1U) * row_cells,
                P4_ANSI_ROWS * row_cells);
    if (terminal->scroll_count < UINT32_MAX) {
        ++terminal->scroll_count;
    }
}

static void line_feed(p4_ansi_terminal_t *terminal)
{
    if (terminal->cursor_row + 1U >= P4_ANSI_ROWS) {
        scroll_up(terminal);
        terminal->cursor_row = P4_ANSI_ROWS - 1U;
    } else {
        ++terminal->cursor_row;
    }
}

static void put_character(p4_ansi_terminal_t *terminal, uint8_t character)
{
    p4_ansi_cell_t *const cell = &terminal->cells[cell_index(
        terminal->cursor_column, terminal->cursor_row)];
    *cell = (p4_ansi_cell_t) {
        .character = character,
        .foreground = terminal->foreground,
        .background = terminal->background,
        .attributes = terminal->attributes,
    };
    if (terminal->cursor_column + 1U >= P4_ANSI_COLUMNS) {
        terminal->cursor_column = 0U;
        line_feed(terminal);
    } else {
        ++terminal->cursor_column;
    }
}

static unsigned csi_parameter(const p4_ansi_terminal_t *terminal,
                              size_t index, unsigned default_value)
{
    if (index > terminal->parameter_count ||
        terminal->parameters[index] == 0U) {
        return default_value;
    }
    return terminal->parameters[index];
}

static uint8_t bounded_position(unsigned one_based, unsigned limit)
{
    if (one_based == 0U) {
        one_based = 1U;
    }
    if (one_based > limit) {
        one_based = limit;
    }
    return (uint8_t)(one_based - 1U);
}

static void move_relative(uint8_t *position, unsigned amount,
                          unsigned limit, bool negative)
{
    unsigned value = *position;
    if (negative) {
        value = amount > value ? 0U : value - amount;
    } else {
        const unsigned maximum = limit - 1U;
        value = amount > maximum - value ? maximum : value + amount;
    }
    *position = (uint8_t)value;
}

static void erase_display(p4_ansi_terminal_t *terminal, unsigned mode)
{
    const size_t cursor = cell_index(
        terminal->cursor_column, terminal->cursor_row);
    if (mode == 0U) {
        clear_cells(terminal, cursor, P4_ANSI_COLUMNS * P4_ANSI_ROWS);
    } else if (mode == 1U) {
        clear_cells(terminal, 0U, cursor + 1U);
    } else if (mode == 2U || mode == 3U) {
        clear_cells(terminal, 0U, P4_ANSI_COLUMNS * P4_ANSI_ROWS);
    }
}

static void erase_line(p4_ansi_terminal_t *terminal, unsigned mode)
{
    const size_t first = cell_index(0U, terminal->cursor_row);
    const size_t cursor = first + terminal->cursor_column;
    const size_t last = first + P4_ANSI_COLUMNS;
    if (mode == 0U) {
        clear_cells(terminal, cursor, last);
    } else if (mode == 1U) {
        clear_cells(terminal, first, cursor + 1U);
    } else if (mode == 2U) {
        clear_cells(terminal, first, last);
    }
}

static void reset_attributes(p4_ansi_terminal_t *terminal)
{
    terminal->foreground = P4_ANSI_COLOR_LIGHT_GRAY;
    terminal->background = P4_ANSI_COLOR_BLACK;
    terminal->attributes = 0U;
}

static void apply_sgr(p4_ansi_terminal_t *terminal)
{
    for (size_t index = 0U; index <= terminal->parameter_count; ++index) {
        const unsigned value = terminal->parameters[index];
        if (value == 0U) {
            reset_attributes(terminal);
        } else if (value == 1U) {
            terminal->attributes |= P4_ANSI_ATTR_BOLD;
        } else if (value == 5U || value == 6U) {
            terminal->attributes |= P4_ANSI_ATTR_BLINK;
        } else if (value == 7U) {
            terminal->attributes |= P4_ANSI_ATTR_INVERSE;
        } else if (value == 22U) {
            terminal->attributes &= (uint8_t)~P4_ANSI_ATTR_BOLD;
        } else if (value == 25U) {
            terminal->attributes &= (uint8_t)~P4_ANSI_ATTR_BLINK;
        } else if (value == 27U) {
            terminal->attributes &= (uint8_t)~P4_ANSI_ATTR_INVERSE;
        } else if (value >= 30U && value <= 37U) {
            terminal->foreground = s_sgr_colors[value - 30U];
        } else if (value == 39U) {
            terminal->foreground = P4_ANSI_COLOR_LIGHT_GRAY;
        } else if (value >= 40U && value <= 47U) {
            terminal->background = s_sgr_colors[value - 40U];
        } else if (value == 49U) {
            terminal->background = P4_ANSI_COLOR_BLACK;
        } else if (value >= 90U && value <= 97U) {
            terminal->foreground = s_sgr_bright_colors[value - 90U];
        } else if (value >= 100U && value <= 107U) {
            terminal->background = s_sgr_bright_colors[value - 100U];
        }
    }
}

static bool execute_csi(p4_ansi_terminal_t *terminal, uint8_t final)
{
    const unsigned first = csi_parameter(terminal, 0U, 1U);
    switch (final) {
    case 'A':
        move_relative(&terminal->cursor_row, first,
                      P4_ANSI_ROWS, true);
        return true;
    case 'B':
        move_relative(&terminal->cursor_row, first,
                      P4_ANSI_ROWS, false);
        return true;
    case 'C':
        move_relative(&terminal->cursor_column, first,
                      P4_ANSI_COLUMNS, false);
        return true;
    case 'D':
        move_relative(&terminal->cursor_column, first,
                      P4_ANSI_COLUMNS, true);
        return true;
    case 'G':
        terminal->cursor_column = bounded_position(
            first, P4_ANSI_COLUMNS);
        return true;
    case 'H':
    case 'f':
        terminal->cursor_row = bounded_position(
            csi_parameter(terminal, 0U, 1U), P4_ANSI_ROWS);
        terminal->cursor_column = bounded_position(
            csi_parameter(terminal, 1U, 1U), P4_ANSI_COLUMNS);
        return true;
    case 'J':
        erase_display(terminal, csi_parameter(terminal, 0U, 0U));
        return true;
    case 'K':
        erase_line(terminal, csi_parameter(terminal, 0U, 0U));
        return true;
    case 'm':
        apply_sgr(terminal);
        return true;
    case 's':
        terminal->saved_column = terminal->cursor_column;
        terminal->saved_row = terminal->cursor_row;
        return true;
    case 'u':
        terminal->cursor_column = terminal->saved_column;
        terminal->cursor_row = terminal->saved_row;
        return true;
    case 'h':
    case 'l':
        if (terminal->private_mode &&
            csi_parameter(terminal, 0U, 0U) == 25U) {
            terminal->cursor_visible = final == 'h';
            return true;
        }
        return false;
    default:
        return false;
    }
}

static void note_unsupported(p4_ansi_terminal_t *terminal)
{
    if (terminal->unsupported_sequences < UINT32_MAX) {
        ++terminal->unsupported_sequences;
    }
}

static void consume_text(p4_ansi_terminal_t *terminal, uint8_t byte)
{
    switch (byte) {
    case 0x07U:
        break;
    case 0x08U:
        if (terminal->cursor_column > 0U) {
            --terminal->cursor_column;
        }
        break;
    case 0x09U: {
        const unsigned next =
            ((unsigned)terminal->cursor_column + 8U) & ~7U;
        terminal->cursor_column = (uint8_t)(
            next >= P4_ANSI_COLUMNS ? P4_ANSI_COLUMNS - 1U : next);
        break;
    }
    case 0x0aU:
    case 0x0bU:
    case 0x0cU:
        line_feed(terminal);
        break;
    case 0x0dU:
        terminal->cursor_column = 0U;
        break;
    case 0x1bU:
        terminal->parse_state = P4_ANSI_PARSE_ESCAPE;
        terminal->sequence_bytes = 1U;
        break;
    case 0x7fU:
        break;
    default:
        if (byte >= 0x20U) {
            put_character(terminal, byte);
        }
        break;
    }
}

static void consume_escape(p4_ansi_terminal_t *terminal, uint8_t byte)
{
    terminal->sequence_bytes = (uint8_t)(terminal->sequence_bytes + 1U);
    if (byte == '[') {
        terminal->parse_state = P4_ANSI_PARSE_CSI;
        terminal->parameter_count = 0U;
        terminal->parameter_active = false;
        terminal->private_mode = false;
        memset(terminal->parameters, 0, sizeof(terminal->parameters));
    } else if (byte == ']' || byte == 'P' || byte == '_' || byte == '^') {
        terminal->parse_state = P4_ANSI_PARSE_STRING;
        if (terminal->discarded_strings < UINT32_MAX) {
            ++terminal->discarded_strings;
        }
    } else if (byte == '7') {
        terminal->saved_column = terminal->cursor_column;
        terminal->saved_row = terminal->cursor_row;
        reset_parser(terminal);
    } else if (byte == '8') {
        terminal->cursor_column = terminal->saved_column;
        terminal->cursor_row = terminal->saved_row;
        reset_parser(terminal);
    } else if (byte == 'c') {
        p4_ansi_reset(terminal);
    } else {
        note_unsupported(terminal);
        reset_parser(terminal);
    }
}

static void consume_csi(p4_ansi_terminal_t *terminal, uint8_t byte)
{
    if (terminal->sequence_bytes >= P4_ANSI_MAX_SEQUENCE_BYTES) {
        note_unsupported(terminal);
        reset_parser(terminal);
        return;
    }
    ++terminal->sequence_bytes;
    if (byte == '?' && terminal->parameter_count == 0U &&
        !terminal->parameter_active) {
        terminal->private_mode = true;
        return;
    }
    if (byte >= '0' && byte <= '9') {
        const unsigned current = terminal->parameters[
            terminal->parameter_count];
        const unsigned digit = (unsigned)(byte - '0');
        terminal->parameters[terminal->parameter_count] = (uint16_t)(
            current > 999U ? 9999U : current * 10U + digit);
        terminal->parameter_active = true;
        return;
    }
    if (byte == ';') {
        if (terminal->parameter_count + 1U >= P4_ANSI_MAX_PARAMETERS) {
            note_unsupported(terminal);
            reset_parser(terminal);
            return;
        }
        ++terminal->parameter_count;
        terminal->parameter_active = false;
        return;
    }
    if (byte >= 0x40U && byte <= 0x7eU) {
        if (!execute_csi(terminal, byte)) {
            note_unsupported(terminal);
        }
        reset_parser(terminal);
        return;
    }
    note_unsupported(terminal);
    reset_parser(terminal);
}

bool p4_ansi_write(p4_ansi_terminal_t *terminal,
                   const uint8_t *bytes,
                   size_t byte_count)
{
    if (terminal == NULL || (byte_count > 0U && bytes == NULL)) {
        return false;
    }
    for (size_t index = 0U; index < byte_count; ++index) {
        const uint8_t byte = bytes[index];
        if (terminal->bytes_consumed < UINT32_MAX) {
            ++terminal->bytes_consumed;
        }
        switch (terminal->parse_state) {
        case P4_ANSI_PARSE_TEXT:
            consume_text(terminal, byte);
            break;
        case P4_ANSI_PARSE_ESCAPE:
            consume_escape(terminal, byte);
            break;
        case P4_ANSI_PARSE_CSI:
            consume_csi(terminal, byte);
            break;
        case P4_ANSI_PARSE_STRING:
            if (byte == 0x07U) {
                reset_parser(terminal);
            } else if (byte == 0x1bU) {
                terminal->parse_state = P4_ANSI_PARSE_STRING_ESCAPE;
            }
            break;
        case P4_ANSI_PARSE_STRING_ESCAPE:
            if (byte == '\\') {
                reset_parser(terminal);
            } else if (byte != 0x1bU) {
                terminal->parse_state = P4_ANSI_PARSE_STRING;
            }
            break;
        default:
            reset_parser(terminal);
            break;
        }
    }
    return true;
}

const p4_ansi_cell_t *p4_ansi_cell(const p4_ansi_terminal_t *terminal,
                                   size_t column,
                                   size_t row)
{
    if (terminal == NULL || column >= P4_ANSI_COLUMNS ||
        row >= P4_ANSI_ROWS) {
        return NULL;
    }
    return &terminal->cells[cell_index(column, row)];
}

static bool render_rows(const p4_ansi_terminal_t *terminal,
                                uint16_t *pixels,
                                size_t stride_pixels,
                                size_t width,
                                size_t height,
                                size_t first_row,
                                size_t row_count,
                                int32_t y_offset_pixels,
                                size_t clip_top,
                                size_t clip_height,
                                bool scaled)
{
    if (terminal == NULL || pixels == NULL ||
        width < P4_ANSI_SURFACE_WIDTH ||
        height < P4_ANSI_SURFACE_HEIGHT || stride_pixels < width ||
        first_row > P4_ANSI_ROWS ||
        row_count > P4_ANSI_ROWS - first_row ||
        width > 4096U || height > 4096U ||
        stride_pixels > SIZE_MAX / height / sizeof(*pixels) ||
        clip_top > (scaled ? P4_ANSI_SURFACE_HEIGHT : height) ||
        clip_height > (scaled ? P4_ANSI_SURFACE_HEIGHT : height) - clip_top) {
        return false;
    }
    if (row_count == 0U || clip_height == 0U) {
        return true;
    }
    const size_t left = scaled ? (P4_ANSI_SURFACE_WIDTH - P4_ANSI_TEXT_WIDTH) / 2U :
        (width - P4_ANSI_TEXT_WIDTH) / 2U;
    const int64_t clip_bottom = (int64_t)(clip_top + clip_height);
    for (size_t row = first_row; row < first_row + row_count; ++row) {
        const int64_t cell_top =
            (int64_t)row * P4_ANSI_CELL_HEIGHT + y_offset_pixels;
        for (size_t column = 0U; column < P4_ANSI_COLUMNS; ++column) {
            const p4_ansi_cell_t *const cell =
                &terminal->cells[cell_index(column, row)];
            unsigned foreground = cell->foreground & 0x0fU;
            unsigned background = cell->background & 0x0fU;
            if ((cell->attributes & P4_ANSI_ATTR_BOLD) != 0U &&
                foreground < 8U) {
                foreground += 8U;
            }
            const bool cursor = terminal->cursor_visible &&
                terminal->cursor_column == column &&
                terminal->cursor_row == row;
            if ((cell->attributes & P4_ANSI_ATTR_INVERSE) != 0U || cursor) {
                const unsigned swap = foreground;
                foreground = background;
                background = swap;
            }
            const size_t cell_left = left + column * P4_ANSI_CELL_WIDTH;
            const bool embolden =
                (cell->attributes & P4_ANSI_ATTR_BOLD) != 0U &&
                cell->character >= UINT8_C(0x20) &&
                cell->character <= UINT8_C(0x7e);
            for (size_t glyph_row = 0U;
                 glyph_row < P4_ANSI_CELL_HEIGHT; ++glyph_row) {
                const int64_t destination_y =
                    cell_top + (int64_t)glyph_row;
                if (destination_y < (int64_t)clip_top ||
                    destination_y >= clip_bottom) {
                    continue;
                }
                const uint8_t bits = p4_cp437_font_8x16[
                    (size_t)cell->character * P4_ANSI_CELL_HEIGHT +
                    glyph_row];
                for (size_t glyph_column = 0U;
                     glyph_column < P4_ANSI_CELL_WIDTH; ++glyph_column) {
                    bool set = false;
                    if (glyph_column < 8U) {
                        const uint8_t mask = (uint8_t)(
                            UINT8_C(0x80) >> glyph_column);
                        set = (bits & mask) != 0U;
                        if (embolden && glyph_column > 0U) {
                            set = set ||
                                (bits & (uint8_t)(mask << 1U)) != 0U;
                        }
                    } else if (cell->character >= UINT8_C(0xc0) &&
                               cell->character <= UINT8_C(0xdf)) {
                        set = (bits & UINT8_C(0x01)) != 0U;
                    } else if (embolden) {
                        set = (bits & UINT8_C(0x01)) != 0U;
                    }
                    const size_t sx = cell_left + glyph_column;
                    const size_t sy = (size_t)destination_y;
                    const size_t x0 = scaled ? sx * width / P4_ANSI_SURFACE_WIDTH : sx;
                    const size_t x1 = scaled ? (sx + 1U) * width / P4_ANSI_SURFACE_WIDTH : sx + 1U;
                    const size_t y0 = scaled ? sy * height / P4_ANSI_SURFACE_HEIGHT : sy;
                    const size_t y1 = scaled ? (sy + 1U) * height / P4_ANSI_SURFACE_HEIGHT : sy + 1U;
                    for (size_t py = y0; py < y1; ++py) {
                        for (size_t px = x0; px < x1; ++px) {
                            pixels[py * stride_pixels + px] =
                                s_dos_palette[set ? foreground : background];
                        }
                    }
                }
            }
        }
    }
    return true;
}

bool p4_ansi_render_rows_rgb565(const p4_ansi_terminal_t *terminal,
    uint16_t *pixels, size_t stride_pixels, size_t width, size_t height,
    size_t first_row, size_t row_count, int32_t y_offset_pixels,
    size_t clip_top, size_t clip_height)
{
    return render_rows(terminal, pixels, stride_pixels, width, height,
        first_row, row_count, y_offset_pixels, clip_top, clip_height, false);
}

bool p4_ansi_render_scaled_rows_rgb565(const p4_ansi_terminal_t *terminal,
    uint16_t *pixels, size_t stride_pixels, size_t width, size_t height,
    size_t first_row, size_t row_count, int32_t y_offset_pixels,
    size_t clip_top, size_t clip_height)
{
    return render_rows(terminal, pixels, stride_pixels, width, height,
        first_row, row_count, y_offset_pixels, clip_top, clip_height, true);
}

bool p4_ansi_render_rgb565(const p4_ansi_terminal_t *terminal,
                           uint16_t *pixels,
                           size_t stride_pixels,
                           size_t width,
                           size_t height)
{
    if (terminal == NULL || pixels == NULL ||
        width < P4_ANSI_SURFACE_WIDTH ||
        height < P4_ANSI_SURFACE_HEIGHT || stride_pixels < width) {
        return false;
    }
    for (size_t row = 0U; row < height; ++row) {
        for (size_t column = 0U; column < width; ++column) {
            pixels[row * stride_pixels + column] = s_dos_palette[0];
        }
    }
    return p4_ansi_render_rows_rgb565(
        terminal, pixels, stride_pixels, width, height,
        0U, P4_ANSI_ROWS, 0, 0U, height);
}

bool p4_ansi_render_scaled_rgb565(const p4_ansi_terminal_t *terminal,
    uint16_t *pixels, size_t stride_pixels, size_t width, size_t height)
{
    if (terminal == NULL || pixels == NULL || width < P4_ANSI_SURFACE_WIDTH ||
        height < P4_ANSI_SURFACE_HEIGHT || width > 4096U || height > 4096U ||
        stride_pixels < width || stride_pixels > SIZE_MAX / height / sizeof(*pixels)) {
        return false;
    }
    for (size_t y = 0U; y < height; ++y) {
        memset(pixels + y * stride_pixels, 0, width * sizeof(*pixels));
    }
    return p4_ansi_render_scaled_rows_rgb565(terminal, pixels, stride_pixels,
        width, height, 0U, P4_ANSI_ROWS, 0, 0U, P4_ANSI_SURFACE_HEIGHT);
}
