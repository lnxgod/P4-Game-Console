// SPDX-License-Identifier: MIT

#include "p4/bbs_ui.h"

#include <stdio.h>
#include <string.h>

enum {
    CP437_VERTICAL = 0xb3,
    CP437_TOP_RIGHT = 0xbf,
    CP437_BOTTOM_LEFT = 0xc0,
    CP437_HORIZONTAL = 0xc4,
    CP437_BOTTOM_RIGHT = 0xd9,
    CP437_TOP_LEFT = 0xda,
    CP437_SHADE_LIGHT = 0xb0,
    CP437_SHADE_MEDIUM = 0xb1,
    CP437_SHADE_DARK = 0xb2,
    CP437_BLOCK = 0xdb,
    CP437_LOWER_HALF = 0xdc,
    CP437_UPPER_HALF = 0xdf,
    CP437_DOUBLE_VERTICAL = 0xba,
    CP437_DOUBLE_TOP_RIGHT = 0xbb,
    CP437_DOUBLE_BOTTOM_RIGHT = 0xbc,
    CP437_DOUBLE_BOTTOM_LEFT = 0xc8,
    CP437_DOUBLE_TOP_LEFT = 0xc9,
    CP437_DOUBLE_HORIZONTAL = 0xcd,
    BBS_TEXT_LEFT = (P4_ANSI_SURFACE_WIDTH - P4_ANSI_TEXT_WIDTH) / 2,
    BBS_DOOR_COLUMN = 6,
    BBS_DOOR_TOP_ROW = 10,
    BBS_DOOR_COLUMN_CELLS = 68,
    BBS_DOOR_ROW_CELLS = 3,
    BBS_PAGE_CONTROL_ROW = 25,
    BBS_PAGE_PREVIOUS_COLUMN = 5,
    BBS_PAGE_NEXT_COLUMN = 65,
    BBS_PAGE_BUTTON_CELLS = 11,
};

static bool write_bytes(p4_ansi_terminal_t *terminal,
                        const void *bytes, size_t count)
{
    return p4_ansi_write(terminal, bytes, count);
}

static bool write_literal(p4_ansi_terminal_t *terminal, const char *text)
{
    return text != NULL && write_bytes(terminal, text, strlen(text));
}

static bool move_to(p4_ansi_terminal_t *terminal,
                    unsigned row, unsigned column)
{
    char sequence[20];
    const int written = snprintf(
        sequence, sizeof(sequence), "\x1b[%u;%uH", row, column);
    return written > 0 && (size_t)written < sizeof(sequence) &&
        write_bytes(terminal, sequence, (size_t)written);
}

static unsigned foreground_sgr(uint8_t color)
{
    static const uint8_t codes[16] = {
        30U, 34U, 32U, 36U, 31U, 35U, 33U, 37U,
        90U, 94U, 92U, 96U, 91U, 95U, 93U, 97U,
    };
    return codes[color & 0x0fU];
}

static unsigned background_sgr(uint8_t color)
{
    static const uint8_t codes[16] = {
        40U, 44U, 42U, 46U, 41U, 45U, 43U, 47U,
        100U, 104U, 102U, 106U, 101U, 105U, 103U, 107U,
    };
    return codes[color & 0x0fU];
}

static bool style(p4_ansi_terminal_t *terminal,
                  uint8_t foreground, uint8_t background, bool bold)
{
    char sequence[24];
    const int written = snprintf(
        sequence, sizeof(sequence), "\x1b[0;%u;%u%sm",
        foreground_sgr(foreground), background_sgr(background),
        bold ? ";1" : "");
    return written > 0 && (size_t)written < sizeof(sequence) &&
        write_bytes(terminal, sequence, (size_t)written);
}

static bool repeated_byte(p4_ansi_terminal_t *terminal,
                          uint8_t byte, size_t count)
{
    uint8_t bytes[80];
    if (count > sizeof(bytes)) {
        return false;
    }
    memset(bytes, byte, count);
    return write_bytes(terminal, bytes, count);
}

static bool box(p4_ansi_terminal_t *terminal,
                unsigned top, unsigned left,
                unsigned bottom, unsigned right,
                uint8_t color, bool double_line)
{
    if (top >= bottom || left >= right || right > P4_ANSI_COLUMNS ||
        bottom > P4_ANSI_ROWS || !style(
            terminal, color, P4_ANSI_COLOR_BLACK, true)) {
        return false;
    }
    const uint8_t top_left = double_line
        ? CP437_DOUBLE_TOP_LEFT : CP437_TOP_LEFT;
    const uint8_t top_right = double_line
        ? CP437_DOUBLE_TOP_RIGHT : CP437_TOP_RIGHT;
    const uint8_t bottom_left = double_line
        ? CP437_DOUBLE_BOTTOM_LEFT : CP437_BOTTOM_LEFT;
    const uint8_t bottom_right = double_line
        ? CP437_DOUBLE_BOTTOM_RIGHT : CP437_BOTTOM_RIGHT;
    const uint8_t horizontal = double_line
        ? CP437_DOUBLE_HORIZONTAL : CP437_HORIZONTAL;
    const uint8_t vertical = double_line
        ? CP437_DOUBLE_VERTICAL : CP437_VERTICAL;
    if (!move_to(terminal, top, left) ||
        !write_bytes(terminal, &top_left, 1U) ||
        !repeated_byte(terminal, horizontal, right - left - 1U) ||
        !write_bytes(terminal, &top_right, 1U)) {
        return false;
    }
    for (unsigned row = top + 1U; row < bottom; ++row) {
        if (!move_to(terminal, row, left) ||
            !write_bytes(terminal, &vertical, 1U) ||
            !move_to(terminal, row, right) ||
            !write_bytes(terminal, &vertical, 1U)) {
            return false;
        }
    }
    return move_to(terminal, bottom, left) &&
        write_bytes(terminal, &bottom_left, 1U) &&
        repeated_byte(terminal, horizontal, right - left - 1U) &&
        write_bytes(terminal, &bottom_right, 1U);
}

static size_t bounded_text_length(const char *text, size_t capacity)
{
    if (text == NULL || capacity == 0U) {
        return 0U;
    }
    size_t length = 0U;
    while (length + 1U < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool model_strings_are_terminated(
    const p4_bbs_launcher_model_t *model)
{
    if (memchr(model->board_name, '\0', sizeof(model->board_name)) == NULL ||
        memchr(model->node_name, '\0', sizeof(model->node_name)) == NULL ||
        memchr(model->section, '\0', sizeof(model->section)) == NULL ||
        memchr(model->connection, '\0', sizeof(model->connection)) == NULL) {
        return false;
    }
    for (size_t index = 0U; index < model->door_count; ++index) {
        const p4_bbs_door_t *const door = &model->doors[index];
        if (memchr(door->title, '\0', sizeof(door->title)) == NULL ||
            memchr(door->subtitle, '\0', sizeof(door->subtitle)) == NULL) {
            return false;
        }
    }
    return true;
}

static bool field(p4_ansi_terminal_t *terminal,
                  unsigned row, unsigned column, unsigned width,
                  const char *text, uint8_t foreground,
                  uint8_t background, bool bold, bool centered)
{
    if (width == 0U || column + width - 1U > P4_ANSI_COLUMNS ||
        !move_to(terminal, row, column) ||
        !style(terminal, foreground, background, bold)) {
        return false;
    }
    char output[81];
    if (width >= sizeof(output)) {
        return false;
    }
    memset(output, ' ', width);
    output[width] = '\0';
    const size_t available = bounded_text_length(text, width + 1U);
    const size_t start = centered && available < width
        ? (width - available) / 2U : 0U;
    for (size_t index = 0U; index < available; ++index) {
        const unsigned char byte = (unsigned char)text[index];
        output[start + index] = byte >= 0x20U && byte <= 0x7eU
            ? (char)byte : '?';
    }
    return write_bytes(terminal, output, width);
}

static bool number_field(p4_ansi_terminal_t *terminal,
                         unsigned row, unsigned column,
                         uint16_t value, uint8_t color)
{
    char text[12];
    const int written = snprintf(text, sizeof(text), "%u", (unsigned)value);
    return written > 0 && (size_t)written < sizeof(text) &&
        field(terminal, row, column, (unsigned)written, text,
              color, P4_ANSI_COLOR_BLACK, false, false);
}

static bool draw_header(p4_ansi_terminal_t *terminal,
                        const p4_bbs_launcher_model_t *model)
{
    static const char *const wordmark[] = {
        "  ___   _   __  __ ___    ___ _  _   _   _  _  ___ ___ ___  ___",
        " / __| /_\\ |  \\/  | __|  / __| || | /_\\ | \\| |/ __| __| _ \\/ __|",
        "| (_ |/ _ \\| |\\/| | _|  | (__| __ |/ _ \\| .` | (_ | _||   /\\__ \\",
        " \\___/_/ \\_\\_|  |_|___|  \\___|_||_/_/ \\_\\_|\\_|\\___|___|_|_\\|___/",
    };
    if (!box(terminal, 1U, 2U, 30U, 79U,
             P4_ANSI_COLOR_BRIGHT_CYAN, true)) {
        return false;
    }
    for (size_t index = 0U;
         index < sizeof(wordmark) / sizeof(wordmark[0]); ++index) {
        if (!field(terminal, 2U + (unsigned)index, 4U, 74U,
                   wordmark[index],
                   index == 1U || index == 3U
                       ? P4_ANSI_COLOR_BRIGHT_MAGENTA
                       : P4_ANSI_COLOR_BRIGHT_CYAN,
                   P4_ANSI_COLOR_BLACK, true, true)) {
            return false;
        }
    }
    char identity[76];
    const int identity_written = snprintf(
        identity, sizeof(identity), "%s // OPEN SOURCE DOORS + REMIX EXCHANGE",
        model->board_name[0] == '\0'
            ? "GAME CHANGERS AI BBS" : model->board_name);
    char link[76];
    const int link_written = snprintf(
        link, sizeof(link), "NODE %-16s | 2400 BAUD | %-10s | %s",
        model->node_name[0] == '\0' ? "GC-P4-LOCAL" : model->node_name,
        model->connection[0] == '\0' ? "ONLINE" : model->connection,
        model->local_board ? "LOCAL" : "REMOTE");
    if (identity_written <= 0 ||
        (size_t)identity_written >= sizeof(identity) ||
        link_written <= 0 || (size_t)link_written >= sizeof(link) ||
        !field(terminal, 6U, 5U, 72U, identity,
               P4_ANSI_COLOR_YELLOW,
               P4_ANSI_COLOR_BLACK, true, true) ||
        !field(terminal, 7U, 5U, 72U, link,
               P4_ANSI_COLOR_BRIGHT_GREEN,
               P4_ANSI_COLOR_BLUE, true, true)) {
        return false;
    }
    if (model->can_go_up &&
        !field(terminal, 8U, 5U, 11U, "[<] BACK",
               P4_ANSI_COLOR_WHITE, P4_ANSI_COLOR_BLUE, true, true)) {
        return false;
    }
    return field(terminal, 8U, model->can_go_up ? 18U : 25U,
                 model->can_go_up ? 51U : 31U,
                 model->section[0] == '\0'
                    ? "[ DOOR GAMES ]" : model->section,
                 P4_ANSI_COLOR_YELLOW,
                 P4_ANSI_COLOR_BLUE, true, true);
}

static bool draw_door(p4_ansi_terminal_t *terminal,
                      const p4_bbs_door_t *door,
                      size_t index, bool selected)
{
    const unsigned row = BBS_DOOR_TOP_ROW +
        (unsigned)index * BBS_DOOR_ROW_CELLS;
    const uint8_t foreground = selected
        ? P4_ANSI_COLOR_WHITE
        : (door->enabled ? P4_ANSI_COLOR_BRIGHT_CYAN
                         : P4_ANSI_COLOR_DARK_GRAY);
    const uint8_t background = selected
        ? P4_ANSI_COLOR_BRIGHT_MAGENTA : P4_ANSI_COLOR_BLUE;
    char label[80];
    const int written = snprintf(
        label, sizeof(label), "%s [%02u] %-24s %s",
        selected ? ">>" : "::",
        (unsigned)(door->number == 0U ? index + 1U : door->number),
        door->title[0] == '\0' ? (door->menu ? "MORE DOORS" : "UNTITLED")
                               : door->title,
        door->enabled ? "[ OPEN ]" : "[OFFLINE]");
    if (written <= 0 || (size_t)written >= sizeof(label) ||
        !field(terminal, row, BBS_DOOR_COLUMN,
               BBS_DOOR_COLUMN_CELLS, label,
               foreground, background, true, false)) {
        return false;
    }
    char detail[80];
    const int detail_written = snprintf(
        detail, sizeof(detail), "     %-32s  %s",
        door->enabled ? door->subtitle : "DOOR CURRENTLY OFFLINE",
        door->menu ? "DIRECTORY" : "A/TOUCH TO ENTER");
    return detail_written > 0 &&
        (size_t)detail_written < sizeof(detail) &&
        field(terminal, row + 1U, BBS_DOOR_COLUMN,
              BBS_DOOR_COLUMN_CELLS, detail,
              selected ? P4_ANSI_COLOR_WHITE
                       : (door->enabled
                            ? (door->accent & 0x0fU)
                            : P4_ANSI_COLOR_DARK_GRAY),
              background, false, false);
}

static bool draw_touch_controls(p4_ansi_terminal_t *terminal,
                                const p4_bbs_launcher_model_t *model)
{
    if (model->page > 1U &&
        !field(terminal, BBS_PAGE_CONTROL_ROW,
               BBS_PAGE_PREVIOUS_COLUMN, BBS_PAGE_BUTTON_CELLS,
               "[<] PREV", P4_ANSI_COLOR_WHITE,
               P4_ANSI_COLOR_BLUE, true, true)) {
        return false;
    }
    if (!field(terminal, BBS_PAGE_CONTROL_ROW, 18U, 44U,
               "A / TOUCH: OPEN DOOR     B: BACK",
               P4_ANSI_COLOR_YELLOW,
               P4_ANSI_COLOR_BLACK, true, true)) {
        return false;
    }
    return model->page >= model->page_count ||
        field(terminal, BBS_PAGE_CONTROL_ROW,
              BBS_PAGE_NEXT_COLUMN, BBS_PAGE_BUTTON_CELLS,
              "NEXT [>]", P4_ANSI_COLOR_WHITE,
              P4_ANSI_COLOR_BLUE, true, true);
}

bool p4_bbs_build_launcher(p4_ansi_terminal_t *terminal,
                           const p4_bbs_launcher_model_t *model)
{
    if (terminal == NULL || model == NULL ||
        model->door_count > P4_BBS_VISIBLE_DOORS ||
        (model->door_count > 0U && model->selected_door >= model->door_count) ||
        model->page == 0U || model->page_count == 0U ||
        model->page > model->page_count ||
        !model_strings_are_terminated(model)) {
        return false;
    }
    p4_ansi_reset(terminal);
    if (!write_literal(terminal, "\x1b[2J\x1b[?25l") ||
        !draw_header(terminal, model) ||
        !box(terminal, 9U, 4U, 24U, 77U,
             P4_ANSI_COLOR_BRIGHT_CYAN, false)) {
        return false;
    }
    for (size_t index = 0U; index < model->door_count; ++index) {
        if (!draw_door(terminal, &model->doors[index], index,
                       index == model->selected_door)) {
            return false;
        }
    }
    if (!draw_touch_controls(terminal, model) ||
        !field(terminal, 27U, 5U, 15U, "SELECTED:",
               P4_ANSI_COLOR_WHITE, P4_ANSI_COLOR_BLACK, true, false) ||
        !number_field(terminal, 27U, 21U,
                      model->door_count == 0U ? 0U :
                      (model->doors[model->selected_door].number == 0U
                           ? (uint16_t)(model->selected_door + 1U)
                           : model->doors[model->selected_door].number),
                      P4_ANSI_COLOR_YELLOW)) {
        return false;
    }
    char transfer[40];
    const int transfer_written = snprintf(
        transfer, sizeof(transfer),
        "UPLOADS %u  TRADES %u  PAGE %u/%u",
        (unsigned)model->uploads, (unsigned)model->trades,
        (unsigned)(model->page == 0U ? 1U : model->page),
        (unsigned)(model->page_count == 0U ? 1U : model->page_count));
    return transfer_written > 0 &&
        (size_t)transfer_written < sizeof(transfer) &&
        field(terminal, 27U, 42U, 34U, transfer,
              P4_ANSI_COLOR_BRIGHT_GREEN,
              P4_ANSI_COLOR_BLACK, false, true) &&
        field(terminal, 29U, 4U, 74U,
              "GAMECHANGERSAI.ORG  /  614-276-3639  /  CARRIER DETECT",
              P4_ANSI_COLOR_BRIGHT_MAGENTA,
              P4_ANSI_COLOR_BLACK, true, true);
}

static const char *boot_phase_status(p4_bbs_boot_phase_t phase)
{
    switch (phase) {
    case P4_BBS_BOOT_POST: return "POST / VIDEO ONLINE";
    case P4_BBS_BOOT_DISK: return "SEEKING LOCAL STORAGE";
    case P4_BBS_BOOT_DIALING: return "DIALING 614-276-3639";
    case P4_BBS_BOOT_TRAINING: return "V.22BIS TRAINING / 2400 BAUD";
    case P4_BBS_BOOT_SYNCING: return "SYNCING DOOR DIRECTORY";
    case P4_BBS_BOOT_CONNECTED: return "CONNECT 2400 / CARRIER DETECT";
    case P4_BBS_BOOT_DEGRADED: return "LOCAL NODE / STORAGE DEGRADED";
    default: return "CONNECTING";
    }
}

static const char *boot_phase_detail(p4_bbs_boot_phase_t phase)
{
    switch (phase) {
    case P4_BBS_BOOT_POST: return "INITIALIZING CONSOLE SERVICES";
    case P4_BBS_BOOT_DISK: return "READING MICROSD PARTITION TABLE";
    case P4_BBS_BOOT_DIALING: return "ATDT 6142763639";
    case P4_BBS_BOOT_TRAINING: return "NEGOTIATING LOCAL BBS SESSION";
    case P4_BBS_BOOT_SYNCING: return "SCANNING /P4/GAMES FOR OPEN DOORS";
    case P4_BBS_BOOT_CONNECTED: return "OPENING GAME CHANGERS AI BBS";
    case P4_BBS_BOOT_DEGRADED: return "BUILT-IN DOORS REMAIN AVAILABLE";
    default: return "PLEASE STAND BY";
    }
}

static bool draw_boot_progress(p4_ansi_terminal_t *terminal,
                               const p4_bbs_boot_model_t *model)
{
    enum { BAR_CELLS = 40 };
    const unsigned total = model->progress_total == 0U
        ? 1U : model->progress_total;
    const unsigned step = model->progress_step > total
        ? total : model->progress_step;
    const unsigned filled = step * BAR_CELLS / total;
    const uint8_t color = model->phase == P4_BBS_BOOT_DEGRADED
        ? P4_ANSI_COLOR_BRIGHT_RED
        : (model->phase == P4_BBS_BOOT_CONNECTED
            ? P4_ANSI_COLOR_BRIGHT_GREEN : P4_ANSI_COLOR_YELLOW);
    const uint8_t open = '[';
    const uint8_t close = ']';
    return field(terminal, 21U, 8U, 10U, "LINK:",
                 P4_ANSI_COLOR_WHITE, P4_ANSI_COLOR_BLACK, true, false) &&
        move_to(terminal, 21U, 19U) &&
        style(terminal, color, P4_ANSI_COLOR_BLACK, true) &&
        write_bytes(terminal, &open, 1U) &&
        repeated_byte(terminal, CP437_BLOCK, filled) &&
        repeated_byte(terminal, CP437_SHADE_LIGHT, BAR_CELLS - filled) &&
        write_bytes(terminal, &close, 1U);
}

bool p4_bbs_build_boot_screen(p4_ansi_terminal_t *terminal,
                              const p4_bbs_boot_model_t *model)
{
    static const char *const mark[] = {
        "                              .-(@)-.",
        "                                /|\\",
        "                          .----/ | \\----.",
        "                         / o---[:::]---o \\",
        "                        /__o____/ \\____o__\\",
    };
    static const char *const wordmark[] = {
        "  ___   _   __  __ ___    ___ _  _   _   _  _  ___ ___ ___  ___",
        " / __| /_\\ |  \\/  | __|  / __| || | /_\\ | \\| |/ __| __| _ \\/ __|",
        "| (_ |/ _ \\| |\\/| | _|  | (__| __ |/ _ \\| .` | (_ | _||   /\\__ \\",
        " \\___/_/ \\_\\_|  |_|___|  \\___|_||_/_/ \\_\\_|\\_|\\___|___|_|_\\|___/",
    };
    if (terminal == NULL || model == NULL ||
        model->phase < P4_BBS_BOOT_POST ||
        model->phase > P4_BBS_BOOT_DEGRADED ||
        memchr(model->node_name, '\0', sizeof(model->node_name)) == NULL ||
        memchr(model->status, '\0', sizeof(model->status)) == NULL ||
        memchr(model->detail, '\0', sizeof(model->detail)) == NULL) {
        return false;
    }
    char boot_title[76];
    const int boot_title_written = snprintf(
        boot_title, sizeof(boot_title),
        "GAMECHANGERSAI.ORG // P4 CONSOLE // NODE %s",
        model->node_name[0] == '\0' ? "GC-P4-LOCAL" : model->node_name);
    if (boot_title_written <= 0 ||
        (size_t)boot_title_written >= sizeof(boot_title)) {
        return false;
    }
    p4_ansi_reset(terminal);
    if (!write_literal(terminal, "\x1b[2J\x1b[?25l") ||
        !box(terminal, 1U, 2U, 30U, 79U,
             P4_ANSI_COLOR_BRIGHT_CYAN, true) ||
        !field(terminal, 2U, 5U, 72U, boot_title,
               P4_ANSI_COLOR_BRIGHT_MAGENTA,
               P4_ANSI_COLOR_BLACK, true, true)) {
        return false;
    }
    for (size_t index = 0U; index < sizeof(mark) / sizeof(mark[0]); ++index) {
        if (!field(terminal, 3U + (unsigned)index, 4U, 74U,
                   mark[index],
                   index == 0U ? P4_ANSI_COLOR_YELLOW
                               : P4_ANSI_COLOR_BRIGHT_GREEN,
                   P4_ANSI_COLOR_BLACK, true, true)) {
            return false;
        }
    }
    for (size_t index = 0U;
         index < sizeof(wordmark) / sizeof(wordmark[0]); ++index) {
        if (!field(terminal, 8U + (unsigned)index, 4U, 74U,
                   wordmark[index], P4_ANSI_COLOR_BRIGHT_CYAN,
                   P4_ANSI_COLOR_BLACK, true, true)) {
            return false;
        }
    }
    const char *const status = model->status[0] == '\0'
        ? boot_phase_status(model->phase) : model->status;
    const char *const detail = model->detail[0] == '\0'
        ? boot_phase_detail(model->phase) : model->detail;
    const uint8_t status_color = model->phase == P4_BBS_BOOT_DEGRADED
        ? P4_ANSI_COLOR_BRIGHT_RED
        : (model->phase == P4_BBS_BOOT_CONNECTED
            ? P4_ANSI_COLOR_BRIGHT_GREEN : P4_ANSI_COLOR_YELLOW);
    if (!field(terminal, 13U, 19U, 44U,
               "[ GAME CHANGERS AI // CONSOLE OS ]",
               P4_ANSI_COLOR_BLACK, P4_ANSI_COLOR_BRIGHT_CYAN, true, true) ||
        !box(terminal, 15U, 5U, 27U, 76U,
             P4_ANSI_COLOR_CYAN, false) ||
        !field(terminal, 16U, 7U, 67U,
               "AT&F  E0  V1  X4  S0=0  +MS=V22B",
               P4_ANSI_COLOR_LIGHT_GRAY,
               P4_ANSI_COLOR_BLACK, false, false) ||
        !field(terminal, 18U, 8U, 64U, status,
               status_color, P4_ANSI_COLOR_BLACK, true, false) ||
        !draw_boot_progress(terminal, model) ||
        !field(terminal, 23U, 8U, 64U, detail,
               P4_ANSI_COLOR_BRIGHT_CYAN,
               P4_ANSI_COLOR_BLACK, false, false) ||
        !field(terminal, 25U, 8U, 31U, "[OK] VIDEO: ANSI 768x480",
               P4_ANSI_COLOR_BRIGHT_GREEN,
               P4_ANSI_COLOR_BLACK, false, false) ||
        !field(terminal, 25U, 42U, 30U,
               model->phase >= P4_BBS_BOOT_SYNCING &&
                       model->phase != P4_BBS_BOOT_DEGRADED
                   ? "[OK] DOORS: DIRECTORY ONLINE"
                   : "[..] DOORS: WAITING FOR SD",
               model->phase >= P4_BBS_BOOT_SYNCING &&
                       model->phase != P4_BBS_BOOT_DEGRADED
                   ? P4_ANSI_COLOR_BRIGHT_GREEN : P4_ANSI_COLOR_YELLOW,
               P4_ANSI_COLOR_BLACK, false, false) ||
        !field(terminal, 29U, 4U, 74U,
               "CALL 614-276-3639  /  CONNECT 2400  /  OPEN DOORS ONLY",
               P4_ANSI_COLOR_BRIGHT_MAGENTA,
               P4_ANSI_COLOR_BLACK, true, true)) {
        return false;
    }
    return true;
}

p4_bbs_hit_t p4_bbs_hit_test(const p4_bbs_launcher_model_t *model,
                             uint16_t surface_x,
                             uint16_t surface_y)
{
    const p4_bbs_hit_t none = {
        .kind = P4_BBS_HIT_NONE,
        .door_index = SIZE_MAX,
    };
    if (model == NULL || surface_x >= P4_ANSI_SURFACE_WIDTH ||
        surface_y >= P4_ANSI_SURFACE_HEIGHT) {
        return none;
    }
    const unsigned back_left = BBS_TEXT_LEFT +
        (5U - 1U) * P4_ANSI_CELL_WIDTH;
    const unsigned back_top = (8U - 1U) * P4_ANSI_CELL_HEIGHT;
    if (model->can_go_up && surface_x >= back_left &&
        surface_x < back_left + 11U * P4_ANSI_CELL_WIDTH &&
        surface_y >= back_top &&
        surface_y < back_top + P4_ANSI_CELL_HEIGHT) {
        return (p4_bbs_hit_t){
            .kind = P4_BBS_HIT_BACK,
            .door_index = SIZE_MAX,
        };
    }
    const unsigned page_top =
        (BBS_PAGE_CONTROL_ROW - 1U) * P4_ANSI_CELL_HEIGHT;
    if (surface_y >= page_top &&
        surface_y < page_top + 2U * P4_ANSI_CELL_HEIGHT) {
        const unsigned previous_left = BBS_TEXT_LEFT +
            (BBS_PAGE_PREVIOUS_COLUMN - 1U) * P4_ANSI_CELL_WIDTH;
        if (model->page > 1U && surface_x >= previous_left &&
            surface_x < previous_left +
                BBS_PAGE_BUTTON_CELLS * P4_ANSI_CELL_WIDTH) {
            return (p4_bbs_hit_t){
                .kind = P4_BBS_HIT_PAGE_PREVIOUS,
                .door_index = SIZE_MAX,
            };
        }
        const unsigned next_left = BBS_TEXT_LEFT +
            (BBS_PAGE_NEXT_COLUMN - 1U) * P4_ANSI_CELL_WIDTH;
        if (model->page < model->page_count &&
            surface_x >= next_left &&
            surface_x < next_left +
                BBS_PAGE_BUTTON_CELLS * P4_ANSI_CELL_WIDTH) {
            return (p4_bbs_hit_t){
                .kind = P4_BBS_HIT_PAGE_NEXT,
                .door_index = SIZE_MAX,
            };
        }
    }
    const unsigned door_top =
        (BBS_DOOR_TOP_ROW - 1U) * P4_ANSI_CELL_HEIGHT;
    if (surface_y < door_top ||
        surface_y >= door_top + P4_BBS_VISIBLE_DOORS *
            BBS_DOOR_ROW_CELLS *
            P4_ANSI_CELL_HEIGHT) {
        return none;
    }
    const unsigned door_left = BBS_TEXT_LEFT +
        (BBS_DOOR_COLUMN - 1U) * P4_ANSI_CELL_WIDTH;
    if (surface_x < door_left || surface_x >=
        door_left + BBS_DOOR_COLUMN_CELLS * P4_ANSI_CELL_WIDTH) {
        return none;
    }
    const size_t index = (surface_y - door_top) /
        (BBS_DOOR_ROW_CELLS * P4_ANSI_CELL_HEIGHT);
    if (index >= model->door_count || !model->doors[index].enabled) {
        return none;
    }
    return (p4_bbs_hit_t){
        .kind = P4_BBS_HIT_DOOR,
        .door_index = index,
    };
}
