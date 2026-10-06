// SPDX-License-Identifier: MIT

#ifndef P4_ANSI_H
#define P4_ANSI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_ANSI_COLUMNS = 80,
    P4_ANSI_ROWS = 30,
    P4_ANSI_CELL_WIDTH = 9,
    P4_ANSI_CELL_HEIGHT = 16,
    P4_ANSI_TEXT_WIDTH = P4_ANSI_COLUMNS * P4_ANSI_CELL_WIDTH,
    P4_ANSI_SURFACE_WIDTH = 768,
    P4_ANSI_SURFACE_HEIGHT = P4_ANSI_ROWS * P4_ANSI_CELL_HEIGHT,
    P4_ANSI_MAX_PARAMETERS = 8,
    P4_ANSI_MAX_SEQUENCE_BYTES = 32,
};

typedef enum {
    P4_ANSI_COLOR_BLACK = 0,
    P4_ANSI_COLOR_BLUE,
    P4_ANSI_COLOR_GREEN,
    P4_ANSI_COLOR_CYAN,
    P4_ANSI_COLOR_RED,
    P4_ANSI_COLOR_MAGENTA,
    P4_ANSI_COLOR_BROWN,
    P4_ANSI_COLOR_LIGHT_GRAY,
    P4_ANSI_COLOR_DARK_GRAY,
    P4_ANSI_COLOR_BRIGHT_BLUE,
    P4_ANSI_COLOR_BRIGHT_GREEN,
    P4_ANSI_COLOR_BRIGHT_CYAN,
    P4_ANSI_COLOR_BRIGHT_RED,
    P4_ANSI_COLOR_BRIGHT_MAGENTA,
    P4_ANSI_COLOR_YELLOW,
    P4_ANSI_COLOR_WHITE,
} p4_ansi_color_t;

enum {
    P4_ANSI_ATTR_BOLD = UINT8_C(1) << 0U,
    P4_ANSI_ATTR_BLINK = UINT8_C(1) << 1U,
    P4_ANSI_ATTR_INVERSE = UINT8_C(1) << 2U,
};

typedef struct {
    uint8_t character;
    uint8_t foreground;
    uint8_t background;
    uint8_t attributes;
} p4_ansi_cell_t;

typedef enum {
    P4_ANSI_PARSE_TEXT = 0,
    P4_ANSI_PARSE_ESCAPE,
    P4_ANSI_PARSE_CSI,
    P4_ANSI_PARSE_STRING,
    P4_ANSI_PARSE_STRING_ESCAPE,
} p4_ansi_parse_state_t;

typedef struct {
    p4_ansi_cell_t cells[P4_ANSI_COLUMNS * P4_ANSI_ROWS];
    uint16_t parameters[P4_ANSI_MAX_PARAMETERS];
    uint8_t cursor_column;
    uint8_t cursor_row;
    uint8_t saved_column;
    uint8_t saved_row;
    uint8_t foreground;
    uint8_t background;
    uint8_t attributes;
    uint8_t parameter_count;
    uint8_t sequence_bytes;
    p4_ansi_parse_state_t parse_state;
    bool parameter_active;
    bool private_mode;
    bool cursor_visible;
    uint32_t bytes_consumed;
    uint32_t unsupported_sequences;
    uint32_t discarded_strings;
    uint32_t scroll_count;
} p4_ansi_terminal_t;

/** Initialize an empty 80x30 DOS textmode terminal. */
void p4_ansi_init(p4_ansi_terminal_t *terminal);

/** Reset parser, attributes, cursor, and every cell. */
void p4_ansi_reset(p4_ansi_terminal_t *terminal);

/**
 * Consume bounded CP437/ECMA-48 bytes. Unsupported controls are ignored and
 * counted. OSC/DCS-style strings are discarded through BEL or ST.
 */
bool p4_ansi_write(p4_ansi_terminal_t *terminal,
                   const uint8_t *bytes,
                   size_t byte_count);

/** Render the 80x30 grid as centered 9x16 CP437 cells into 768x480 RGB565. */
bool p4_ansi_render_rgb565(const p4_ansi_terminal_t *terminal,
                           uint16_t *pixels,
                           size_t stride_pixels,
                           size_t width,
                           size_t height);

/**
 * Composite selected terminal rows into an existing RGB565 frame.
 *
 * Rows retain their normal horizontal position and are translated vertically
 * by `y_offset_pixels`. Writes are clipped to the bounded destination band;
 * pixels outside that band are left untouched. This is intended for small
 * text-mode UI transitions without allocating a second full framebuffer.
 */
bool p4_ansi_render_rows_rgb565(const p4_ansi_terminal_t *terminal,
                                uint16_t *pixels,
                                size_t stride_pixels,
                                size_t width,
                                size_t height,
                                size_t first_row,
                                size_t row_count,
                                int32_t y_offset_pixels,
                                size_t clip_top,
                                size_t clip_height);

/** Read-only bounded cell access for UI hit mapping and tests. */
const p4_ansi_cell_t *p4_ansi_cell(const p4_ansi_terminal_t *terminal,
                                   size_t column,
                                   size_t row);

/** Scale the bounded terminal to a larger surface. Row offsets and clips use
 * the stable 768x480 terminal coordinates, independently of output resolution. */
bool p4_ansi_render_scaled_rows_rgb565(const p4_ansi_terminal_t *terminal,
    uint16_t *pixels, size_t stride_pixels, size_t width, size_t height,
    size_t first_row, size_t row_count, int32_t y_offset_pixels,
    size_t clip_top, size_t clip_height);
bool p4_ansi_render_scaled_rgb565(const p4_ansi_terminal_t *terminal,
    uint16_t *pixels, size_t stride_pixels, size_t width, size_t height);

#ifdef __cplusplus
}
#endif

#endif
