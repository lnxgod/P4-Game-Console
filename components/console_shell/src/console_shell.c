// SPDX-License-Identifier: MIT

#include "console/shell.h"

#include <limits.h>
#include <string.h>

enum {
    TILE_LEFT = 8,
    TILE_TOP = 43,
    TILE_WIDTH = 148,
    TILE_HEIGHT = 44,
    TILE_COLUMN_GAP = 8,
    TILE_ROW_GAP = 6,
    BACK_LEFT = 6,
    BACK_TOP = 6,
    BACK_WIDTH = 54,
    BACK_HEIGHT = 20,
    BACK_CONTROL = CONSOLE_SHELL_MAX_APPS,
    PAGE_PREVIOUS_CONTROL,
    PAGE_NEXT_CONTROL,
    PAGE_BUTTON_TOP = 7,
    PAGE_BUTTON_WIDTH = 24,
    PAGE_BUTTON_HEIGHT = 24,
    PAGE_PREVIOUS_LEFT = 262,
    PAGE_NEXT_LEFT = 291,
};

static const uint16_t COLOR_BLACK = UINT16_C(0x0000);
static const uint16_t COLOR_PANEL = UINT16_C(0x10A2);
static const uint16_t COLOR_PANEL_PRESSED = UINT16_C(0x2945);
static const uint16_t COLOR_WHITE = UINT16_C(0xFFFF);
static const uint16_t COLOR_MUTED = UINT16_C(0x9CF3);
static const uint16_t COLOR_CYAN = UINT16_C(0x5FFF);
static const uint16_t COLOR_GREEN = UINT16_C(0x5FEA);
static const uint16_t COLOR_YELLOW = UINT16_C(0xFFE0);
static const uint16_t COLOR_RED = UINT16_C(0xF904);

static size_t bounded_length(const char *text, size_t limit)
{
    if (text == NULL) {
        return limit;
    }
    size_t length = 0U;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool valid_page(console_page_t page)
{
    return page >= CONSOLE_PAGE_EXTERNAL && page <= CONSOLE_PAGE_AUDIO;
}

static bool registry_is_valid(const console_app_descriptor_t *apps,
                              size_t app_count)
{
    if (apps == NULL || app_count == 0U ||
        app_count > CONSOLE_SHELL_MAX_APPS) {
        return false;
    }

    const uint32_t known_capabilities =
        CONSOLE_CAPABILITY_DISPLAY |
        CONSOLE_CAPABILITY_TOUCH |
        CONSOLE_CAPABILITY_AUDIO |
        CONSOLE_CAPABILITY_STORAGE;
    for (size_t i = 0U; i < app_count; ++i) {
        const console_app_descriptor_t *const app = &apps[i];
        if (app->id == 0U ||
            bounded_length(app->title, CONSOLE_SHELL_TITLE_MAX_BYTES) == 0U ||
            bounded_length(app->title, CONSOLE_SHELL_TITLE_MAX_BYTES) >=
                CONSOLE_SHELL_TITLE_MAX_BYTES ||
            bounded_length(app->subtitle, CONSOLE_SHELL_SUBTITLE_MAX_BYTES) >=
                CONSOLE_SHELL_SUBTITLE_MAX_BYTES ||
            !valid_page(app->page) ||
            (app->capabilities & ~known_capabilities) != 0U) {
            return false;
        }
        for (size_t j = 0U; j < i; ++j) {
            if (apps[j].id == app->id) {
                return false;
            }
        }
    }
    return true;
}

bool console_shell_init(console_shell_t *shell,
                        const console_app_descriptor_t *apps,
                        size_t app_count)
{
    if (shell == NULL || !registry_is_valid(apps, app_count)) {
        return false;
    }
    memset(shell, 0, sizeof(*shell));
    shell->apps = apps;
    shell->app_count = app_count;
    shell->pressed_index = SIZE_MAX;
    shell->page = CONSOLE_PAGE_HOME;
    shell->dirty = true;
    for (size_t i = 0U; i < app_count; ++i) {
        if (apps[i].enabled) {
            shell->selected_index = i;
            break;
        }
    }
    return true;
}

static bool map_physical_to_gui(uint16_t physical_x,
                                uint16_t physical_y,
                                uint16_t *gui_x,
                                uint16_t *gui_y)
{
    const uint16_t viewport_right =
        (uint16_t)(CONSOLE_SHELL_VIEWPORT_LEFT +
                   CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_VIEWPORT_SCALE);
    if (physical_x < CONSOLE_SHELL_VIEWPORT_LEFT ||
        physical_x >= viewport_right ||
        physical_y >= CONSOLE_SHELL_PHYSICAL_HEIGHT ||
        gui_x == NULL || gui_y == NULL) {
        return false;
    }
    *gui_x = (uint16_t)((physical_x - CONSOLE_SHELL_VIEWPORT_LEFT) /
                       CONSOLE_SHELL_VIEWPORT_SCALE);
    *gui_y = (uint16_t)(physical_y / CONSOLE_SHELL_VIEWPORT_SCALE);
    return *gui_x < CONSOLE_SHELL_WIDTH && *gui_y < CONSOLE_SHELL_HEIGHT;
}

static bool point_in_rect(uint16_t x, uint16_t y,
                          unsigned left, unsigned top,
                          unsigned width, unsigned height)
{
    const unsigned point_x = x;
    const unsigned point_y = y;
    return point_x >= left && point_x < left + width &&
           point_y >= top && point_y < top + height;
}

static size_t home_page_count(const console_shell_t *shell)
{
    return (shell->app_count + CONSOLE_SHELL_APPS_PER_PAGE - 1U) /
        CONSOLE_SHELL_APPS_PER_PAGE;
}

static size_t control_at(const console_shell_t *shell,
                         uint16_t gui_x,
                         uint16_t gui_y)
{
    if (shell->page != CONSOLE_PAGE_HOME) {
        return point_in_rect(gui_x, gui_y,
                             BACK_LEFT, BACK_TOP,
                             BACK_WIDTH, BACK_HEIGHT)
                   ? BACK_CONTROL : SIZE_MAX;
    }

    if (home_page_count(shell) > 1U) {
        if (point_in_rect(gui_x, gui_y,
                          PAGE_PREVIOUS_LEFT, PAGE_BUTTON_TOP,
                          PAGE_BUTTON_WIDTH, PAGE_BUTTON_HEIGHT)) {
            return PAGE_PREVIOUS_CONTROL;
        }
        if (point_in_rect(gui_x, gui_y,
                          PAGE_NEXT_LEFT, PAGE_BUTTON_TOP,
                          PAGE_BUTTON_WIDTH, PAGE_BUTTON_HEIGHT)) {
            return PAGE_NEXT_CONTROL;
        }
    }

    const size_t first = shell->home_page * CONSOLE_SHELL_APPS_PER_PAGE;
    const size_t last = first + CONSOLE_SHELL_APPS_PER_PAGE < shell->app_count
        ? first + CONSOLE_SHELL_APPS_PER_PAGE : shell->app_count;
    for (size_t index = first; index < last; ++index) {
        const size_t page_index = index - first;
        const unsigned column = (unsigned)(page_index % 2U);
        const unsigned row = (unsigned)(page_index / 2U);
        const unsigned left = TILE_LEFT +
            column * (TILE_WIDTH + TILE_COLUMN_GAP);
        const unsigned top = TILE_TOP +
            row * (TILE_HEIGHT + TILE_ROW_GAP);
        if (point_in_rect(gui_x, gui_y, left, top,
                          TILE_WIDTH, TILE_HEIGHT)) {
            return shell->apps[index].enabled ? index : SIZE_MAX;
        }
    }
    return SIZE_MAX;
}

static bool contacts_equal(const console_shell_t *shell,
                           const console_shell_contact_t *contacts,
                           size_t contact_count)
{
    if (shell->contact_count != contact_count) {
        return false;
    }
    for (size_t i = 0U; i < contact_count; ++i) {
        if (shell->contacts[i].x != contacts[i].x ||
            shell->contacts[i].y != contacts[i].y) {
            return false;
        }
    }
    return true;
}

static void remember_contacts(console_shell_t *shell,
                              bool valid,
                              const console_shell_contact_t *contacts,
                              size_t contact_count)
{
    const size_t bounded_count =
        valid && contacts != NULL && contact_count <= CONSOLE_SHELL_MAX_CONTACTS
            ? contact_count : 0U;
    if (contacts_equal(shell, contacts, bounded_count)) {
        return;
    }
    shell->contact_count = bounded_count;
    for (size_t i = 0U; i < bounded_count; ++i) {
        shell->contacts[i] = contacts[i];
    }
    if (shell->page == CONSOLE_PAGE_TOUCH) {
        shell->dirty = true;
    }
}

static console_shell_action_t no_action(void)
{
    const console_shell_action_t action = {
        .type = CONSOLE_ACTION_NONE,
        .app_id = 0U,
    };
    return action;
}

console_shell_action_t console_shell_handle_touch(
    console_shell_t *shell,
    bool valid,
    const console_shell_contact_t *contacts,
    size_t contact_count)
{
    if (shell == NULL) {
        return no_action();
    }
    remember_contacts(shell, valid, contacts, contact_count);

    if (!valid || contact_count > CONSOLE_SHELL_MAX_CONTACTS ||
        (contact_count > 0U && contacts == NULL)) {
        if (shell->press_active) {
            shell->dirty = true;
        }
        /* Require one later valid neutral frame before input can re-arm. */
        shell->contact_down = true;
        shell->press_active = false;
        shell->pressed_index = SIZE_MAX;
        return no_action();
    }

    if (contact_count == 0U) {
        if (!shell->contact_down) {
            return no_action();
        }
        shell->contact_down = false;
        if (!shell->press_active) {
            shell->pressed_index = SIZE_MAX;
            return no_action();
        }

        const size_t released_control = shell->pressed_index;
        shell->press_active = false;
        shell->pressed_index = SIZE_MAX;
        shell->dirty = true;
        if (released_control == BACK_CONTROL) {
            console_shell_show_home(shell);
            const console_shell_action_t action = {
                .type = CONSOLE_ACTION_PAGE_CHANGED,
                .app_id = 0U,
            };
            return action;
        }
        if (released_control == PAGE_PREVIOUS_CONTROL ||
            released_control == PAGE_NEXT_CONTROL) {
            const size_t page_count = home_page_count(shell);
            if (released_control == PAGE_PREVIOUS_CONTROL) {
                shell->home_page = shell->home_page == 0U
                    ? page_count - 1U : shell->home_page - 1U;
            } else {
                shell->home_page = (shell->home_page + 1U) % page_count;
            }
            const size_t first =
                shell->home_page * CONSOLE_SHELL_APPS_PER_PAGE;
            shell->selected_index = first;
            const console_shell_action_t action = {
                .type = CONSOLE_ACTION_PAGE_CHANGED,
                .app_id = 0U,
            };
            return action;
        }
        if (released_control >= shell->app_count) {
            return no_action();
        }

        const console_app_descriptor_t *const app =
            &shell->apps[released_control];
        shell->selected_index = released_control;
        if (app->page == CONSOLE_PAGE_EXTERNAL) {
            const console_shell_action_t action = {
                .type = CONSOLE_ACTION_LAUNCH,
                .app_id = app->id,
            };
            return action;
        }
        shell->page = app->page;
        shell->active_app_id = app->id;
        const console_shell_action_t action = {
            .type = CONSOLE_ACTION_PAGE_CHANGED,
            .app_id = app->id,
        };
        return action;
    }

    if (contact_count != 1U) {
        if (shell->press_active) {
            shell->dirty = true;
        }
        shell->contact_down = true;
        shell->press_active = false;
        shell->pressed_index = SIZE_MAX;
        return no_action();
    }

    uint16_t gui_x = 0U;
    uint16_t gui_y = 0U;
    if (!map_physical_to_gui(contacts[0].x, contacts[0].y,
                             &gui_x, &gui_y)) {
        if (shell->press_active) {
            shell->dirty = true;
        }
        shell->contact_down = true;
        shell->press_active = false;
        shell->pressed_index = SIZE_MAX;
        return no_action();
    }

    const size_t control = control_at(shell, gui_x, gui_y);
    if (!shell->contact_down) {
        shell->contact_down = true;
        shell->pressed_index = control;
        shell->press_active = control != SIZE_MAX;
        if (shell->press_active) {
            shell->dirty = true;
        }
        return no_action();
    }

    if (!shell->press_active || control != shell->pressed_index) {
        if (shell->press_active) {
            shell->dirty = true;
        }
        shell->press_active = false;
        shell->pressed_index = SIZE_MAX;
    }
    return no_action();
}

void console_shell_set_runtime_info(
    console_shell_t *shell,
    const console_shell_runtime_info_t *runtime)
{
    if (shell == NULL || runtime == NULL) {
        return;
    }
    const bool changed =
        shell->runtime.uptime_seconds != runtime->uptime_seconds ||
        shell->runtime.internal_free_kib != runtime->internal_free_kib ||
        shell->runtime.psram_free_kib != runtime->psram_free_kib ||
        shell->runtime.touch_ready != runtime->touch_ready ||
        shell->runtime.audio_handoff_ready != runtime->audio_handoff_ready;
    if (!changed) {
        return;
    }
    shell->runtime = *runtime;
    if (shell->page == CONSOLE_PAGE_SYSTEM ||
        shell->page == CONSOLE_PAGE_AUDIO) {
        shell->dirty = true;
    }
}

void console_shell_show_home(console_shell_t *shell)
{
    if (shell == NULL) {
        return;
    }
    shell->page = CONSOLE_PAGE_HOME;
    shell->active_app_id = 0U;
    shell->contact_down = false;
    shell->press_active = false;
    shell->pressed_index = SIZE_MAX;
    shell->dirty = true;
}

bool console_shell_is_dirty(const console_shell_t *shell)
{
    return shell != NULL && shell->dirty;
}

static void put_pixel(uint16_t *pixels, size_t stride,
                      int x, int y, uint16_t color)
{
    if (x >= 0 && x < CONSOLE_SHELL_WIDTH &&
        y >= 0 && y < CONSOLE_SHELL_HEIGHT) {
        pixels[(size_t)y * stride + (size_t)x] = color;
    }
}

static void fill_rect(uint16_t *pixels, size_t stride,
                      int x, int y, int width, int height,
                      uint16_t color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    int left = x < 0 ? 0 : x;
    int top = y < 0 ? 0 : y;
    int right = x + width;
    int bottom = y + height;
    if (right > CONSOLE_SHELL_WIDTH) {
        right = CONSOLE_SHELL_WIDTH;
    }
    if (bottom > CONSOLE_SHELL_HEIGHT) {
        bottom = CONSOLE_SHELL_HEIGHT;
    }
    for (int row = top; row < bottom; ++row) {
        for (int column = left; column < right; ++column) {
            put_pixel(pixels, stride, column, row, color);
        }
    }
}

static void outline_rect(uint16_t *pixels, size_t stride,
                         int x, int y, int width, int height,
                         uint16_t color)
{
    fill_rect(pixels, stride, x, y, width, 1, color);
    fill_rect(pixels, stride, x, y + height - 1, width, 1, color);
    fill_rect(pixels, stride, x, y, 1, height, color);
    fill_rect(pixels, stride, x + width - 1, y, 1, height, color);
}

static void glyph_rows(char character, uint8_t rows[7])
{
    memset(rows, 0, 7U);
    char c = character;
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
#define GLYPH(a,b,c_,d,e,f,g) do { \
        rows[0] = (a); rows[1] = (b); rows[2] = (c_); rows[3] = (d); \
        rows[4] = (e); rows[5] = (f); rows[6] = (g); \
    } while (0)
    switch (c) {
    case 'A': GLYPH(14,17,17,31,17,17,17); break;
    case 'B': GLYPH(30,17,17,30,17,17,30); break;
    case 'C': GLYPH(14,17,16,16,16,17,14); break;
    case 'D': GLYPH(30,17,17,17,17,17,30); break;
    case 'E': GLYPH(31,16,16,30,16,16,31); break;
    case 'F': GLYPH(31,16,16,30,16,16,16); break;
    case 'G': GLYPH(14,17,16,23,17,17,15); break;
    case 'H': GLYPH(17,17,17,31,17,17,17); break;
    case 'I': GLYPH(31,4,4,4,4,4,31); break;
    case 'J': GLYPH(7,2,2,2,18,18,12); break;
    case 'K': GLYPH(17,18,20,24,20,18,17); break;
    case 'L': GLYPH(16,16,16,16,16,16,31); break;
    case 'M': GLYPH(17,27,21,21,17,17,17); break;
    case 'N': GLYPH(17,25,21,19,17,17,17); break;
    case 'O': GLYPH(14,17,17,17,17,17,14); break;
    case 'P': GLYPH(30,17,17,30,16,16,16); break;
    case 'Q': GLYPH(14,17,17,17,21,18,13); break;
    case 'R': GLYPH(30,17,17,30,20,18,17); break;
    case 'S': GLYPH(15,16,16,14,1,1,30); break;
    case 'T': GLYPH(31,4,4,4,4,4,4); break;
    case 'U': GLYPH(17,17,17,17,17,17,14); break;
    case 'V': GLYPH(17,17,17,17,17,10,4); break;
    case 'W': GLYPH(17,17,17,21,21,21,10); break;
    case 'X': GLYPH(17,17,10,4,10,17,17); break;
    case 'Y': GLYPH(17,17,10,4,4,4,4); break;
    case 'Z': GLYPH(31,1,2,4,8,16,31); break;
    case '0': GLYPH(14,17,19,21,25,17,14); break;
    case '1': GLYPH(4,12,4,4,4,4,14); break;
    case '2': GLYPH(14,17,1,2,4,8,31); break;
    case '3': GLYPH(30,1,1,14,1,1,30); break;
    case '4': GLYPH(2,6,10,18,31,2,2); break;
    case '5': GLYPH(31,16,16,30,1,1,30); break;
    case '6': GLYPH(14,16,16,30,17,17,14); break;
    case '7': GLYPH(31,1,2,4,8,8,8); break;
    case '8': GLYPH(14,17,17,14,17,17,14); break;
    case '9': GLYPH(14,17,17,15,1,1,14); break;
    case '-': GLYPH(0,0,0,31,0,0,0); break;
    case '.': GLYPH(0,0,0,0,0,12,12); break;
    case ':': GLYPH(0,12,12,0,12,12,0); break;
    case '/': GLYPH(1,2,2,4,8,8,16); break;
    case '+': GLYPH(0,4,4,31,4,4,0); break;
    case '<': GLYPH(2,4,8,16,8,4,2); break;
    case '>': GLYPH(8,4,2,1,2,4,8); break;
    case '?': GLYPH(14,17,1,2,4,0,4); break;
    default: break;
    }
#undef GLYPH
}

static void draw_text(uint16_t *pixels, size_t stride,
                      int x, int y, const char *text,
                      uint16_t color, unsigned scale,
                      size_t max_characters)
{
    if (text == NULL || scale == 0U) {
        return;
    }
    int cursor = x;
    for (size_t index = 0U;
         index < max_characters && text[index] != '\0'; ++index) {
        uint8_t rows[7];
        glyph_rows(text[index], rows);
        for (unsigned row = 0U; row < 7U; ++row) {
            for (unsigned column = 0U; column < 5U; ++column) {
                const uint8_t mask = (uint8_t)(UINT8_C(1) << (4U - column));
                if ((rows[row] & mask) != 0U) {
                    fill_rect(pixels, stride,
                              cursor + (int)(column * scale),
                              y + (int)(row * scale),
                              (int)scale, (int)scale, color);
                }
            }
        }
        cursor += (int)(6U * scale);
    }
}

static void draw_u32(uint16_t *pixels, size_t stride,
                     int x, int y, uint32_t value, uint16_t color)
{
    char digits[11];
    size_t count = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (size_t i = 0U; i < count / 2U; ++i) {
        const char temporary = digits[i];
        digits[i] = digits[count - i - 1U];
        digits[count - i - 1U] = temporary;
    }
    for (size_t i = 0U; i < count; ++i) {
        uint8_t rows[7];
        glyph_rows(digits[i], rows);
        for (unsigned row = 0U; row < 7U; ++row) {
            for (unsigned column = 0U; column < 5U; ++column) {
                if ((rows[row] & (uint8_t)(UINT8_C(1) << (4U - column))) != 0U) {
                    put_pixel(pixels, stride,
                              x + (int)(i * 6U + column),
                              y + (int)row, color);
                }
            }
        }
    }
}

static void draw_capabilities(uint16_t *pixels, size_t stride,
                              int x, int y, uint32_t capabilities)
{
    int cursor = x;
    const struct {
        uint32_t bit;
        char letter;
    } entries[] = {
        { CONSOLE_CAPABILITY_DISPLAY, 'V' },
        { CONSOLE_CAPABILITY_TOUCH, 'T' },
        { CONSOLE_CAPABILITY_AUDIO, 'A' },
        { CONSOLE_CAPABILITY_STORAGE, 'S' },
    };
    for (size_t i = 0U; i < sizeof(entries) / sizeof(entries[0]); ++i) {
        if ((capabilities & entries[i].bit) == 0U) {
            continue;
        }
        char label[2] = {entries[i].letter, '\0'};
        fill_rect(pixels, stride, cursor, y, 9, 9, UINT16_C(0x2104));
        draw_text(pixels, stride, cursor + 2, y + 1, label, COLOR_CYAN, 1U, 1U);
        cursor += 11;
    }
}

static void draw_home(console_shell_t *shell, uint16_t *pixels, size_t stride)
{
    draw_text(pixels, stride, 8, 6, "P4 CONSOLE", COLOR_WHITE, 2U, 10U);
    draw_text(pixels, stride, 9, 25, "FREERTOS NATIVE SHELL",
              COLOR_CYAN, 1U, 22U);
    fill_rect(pixels, stride, 0, 38, CONSOLE_SHELL_WIDTH, 1, COLOR_CYAN);

    const size_t first = shell->home_page * CONSOLE_SHELL_APPS_PER_PAGE;
    const size_t last = first + CONSOLE_SHELL_APPS_PER_PAGE < shell->app_count
        ? first + CONSOLE_SHELL_APPS_PER_PAGE : shell->app_count;
    for (size_t index = first; index < last; ++index) {
        const console_app_descriptor_t *const app = &shell->apps[index];
        const size_t page_index = index - first;
        const int column = (int)(page_index % 2U);
        const int row = (int)(page_index / 2U);
        const int left = TILE_LEFT + column * (TILE_WIDTH + TILE_COLUMN_GAP);
        const int top = TILE_TOP + row * (TILE_HEIGHT + TILE_ROW_GAP);
        const bool pressed = shell->press_active &&
                             shell->pressed_index == index;
        const uint16_t panel = pressed ? COLOR_PANEL_PRESSED : COLOR_PANEL;
        fill_rect(pixels, stride, left, top, TILE_WIDTH, TILE_HEIGHT, panel);
        fill_rect(pixels, stride, left, top, 4, TILE_HEIGHT,
                  app->enabled ? app->accent_rgb565 : COLOR_MUTED);
        outline_rect(pixels, stride, left, top, TILE_WIDTH, TILE_HEIGHT,
                     app->enabled ? UINT16_C(0x4A69) : UINT16_C(0x2945));
        draw_text(pixels, stride, left + 9, top + 5, app->title,
                  app->enabled ? COLOR_WHITE : COLOR_MUTED, 1U, 15U);
        draw_text(pixels, stride, left + 9, top + 17, app->subtitle,
                  COLOR_MUTED, 1U, 22U);
        draw_capabilities(pixels, stride, left + 9, top + 31,
                          app->capabilities);
        if (!app->enabled) {
            draw_text(pixels, stride, left + 87, top + 31, "OFFLINE",
                      COLOR_RED, 1U, 7U);
        }
    }
    const size_t page_count = home_page_count(shell);
    if (page_count > 1U) {
        const bool previous_pressed = shell->press_active &&
            shell->pressed_index == PAGE_PREVIOUS_CONTROL;
        const bool next_pressed = shell->press_active &&
            shell->pressed_index == PAGE_NEXT_CONTROL;
        fill_rect(pixels, stride, PAGE_PREVIOUS_LEFT, PAGE_BUTTON_TOP,
                  PAGE_BUTTON_WIDTH, PAGE_BUTTON_HEIGHT,
                  previous_pressed ? COLOR_PANEL_PRESSED : COLOR_PANEL);
        fill_rect(pixels, stride, PAGE_NEXT_LEFT, PAGE_BUTTON_TOP,
                  PAGE_BUTTON_WIDTH, PAGE_BUTTON_HEIGHT,
                  next_pressed ? COLOR_PANEL_PRESSED : COLOR_PANEL);
        outline_rect(pixels, stride, PAGE_PREVIOUS_LEFT, PAGE_BUTTON_TOP,
                     PAGE_BUTTON_WIDTH, PAGE_BUTTON_HEIGHT, COLOR_CYAN);
        outline_rect(pixels, stride, PAGE_NEXT_LEFT, PAGE_BUTTON_TOP,
                     PAGE_BUTTON_WIDTH, PAGE_BUTTON_HEIGHT, COLOR_CYAN);
        draw_text(pixels, stride, PAGE_PREVIOUS_LEFT + 9,
                  PAGE_BUTTON_TOP + 8, "<", COLOR_WHITE, 1U, 1U);
        draw_text(pixels, stride, PAGE_NEXT_LEFT + 9,
                  PAGE_BUTTON_TOP + 8, ">", COLOR_WHITE, 1U, 1U);
        draw_text(pixels, stride, 8, 191, "PAGE",
                  COLOR_MUTED, 1U, 4U);
        draw_u32(pixels, stride, 38, 191,
                 (uint32_t)(shell->home_page + 1U), COLOR_WHITE);
        draw_text(pixels, stride, 46, 191, "/",
                  COLOR_MUTED, 1U, 1U);
        draw_u32(pixels, stride, 54, 191,
                 (uint32_t)page_count, COLOR_WHITE);
    } else {
        draw_text(pixels, stride, 8, 191, "TAP AN APP",
                  COLOR_MUTED, 1U, 10U);
    }
}

static const console_app_descriptor_t *active_app(
    const console_shell_t *shell)
{
    for (size_t i = 0U; i < shell->app_count; ++i) {
        if (shell->apps[i].id == shell->active_app_id) {
            return &shell->apps[i];
        }
    }
    return NULL;
}

static void draw_detail_header(console_shell_t *shell,
                               uint16_t *pixels, size_t stride)
{
    const bool pressed = shell->press_active &&
                         shell->pressed_index == BACK_CONTROL;
    fill_rect(pixels, stride, BACK_LEFT, BACK_TOP,
              BACK_WIDTH, BACK_HEIGHT,
              pressed ? COLOR_PANEL_PRESSED : COLOR_PANEL);
    outline_rect(pixels, stride, BACK_LEFT, BACK_TOP,
                 BACK_WIDTH, BACK_HEIGHT, COLOR_CYAN);
    draw_text(pixels, stride, BACK_LEFT + 6, BACK_TOP + 6,
              "< HOME", COLOR_WHITE, 1U, 6U);
    const console_app_descriptor_t *const app = active_app(shell);
    draw_text(pixels, stride, 68, 11,
              app != NULL ? app->title : "CONSOLE",
              app != NULL ? app->accent_rgb565 : COLOR_WHITE,
              1U, 15U);
    fill_rect(pixels, stride, 0, 31, CONSOLE_SHELL_WIDTH, 1, COLOR_CYAN);
}

static void draw_colors(uint16_t *pixels, size_t stride)
{
    const uint16_t colors[] = {
        UINT16_C(0xF800), UINT16_C(0x07E0), UINT16_C(0x001F),
        UINT16_C(0xFFE0), UINT16_C(0x07FF), UINT16_C(0xF81F),
    };
    const char *const labels[] = {"RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA"};
    for (size_t i = 0U; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        const int top = 36 + (int)i * 26;
        fill_rect(pixels, stride, 10, top, 300, 21, colors[i]);
        outline_rect(pixels, stride, 10, top, 300, 21, COLOR_WHITE);
        draw_text(pixels, stride, 18, top + 7, labels[i],
                  i == 3U || i == 4U ? COLOR_BLACK : COLOR_WHITE,
                  1U, 8U);
    }
}

static void draw_touch(const console_shell_t *shell,
                       uint16_t *pixels, size_t stride)
{
    for (int x = 0; x < CONSOLE_SHELL_WIDTH; x += 32) {
        fill_rect(pixels, stride, x, 32, 1, 168, UINT16_C(0x18E3));
    }
    for (int y = 40; y < CONSOLE_SHELL_HEIGHT; y += 20) {
        fill_rect(pixels, stride, 0, y, CONSOLE_SHELL_WIDTH, 1,
                  UINT16_C(0x18E3));
    }
    draw_text(pixels, stride, 8, 37, "CONTACTS", COLOR_MUTED, 1U, 8U);
    draw_u32(pixels, stride, 62, 37, (uint32_t)shell->contact_count, COLOR_WHITE);
    for (size_t i = 0U; i < shell->contact_count; ++i) {
        uint16_t x = 0U;
        uint16_t y = 0U;
        if (!map_physical_to_gui(shell->contacts[i].x,
                                 shell->contacts[i].y, &x, &y)) {
            continue;
        }
        fill_rect(pixels, stride, (int)x - 7, (int)y, 15, 1, COLOR_YELLOW);
        fill_rect(pixels, stride, (int)x, (int)y - 7, 1, 15, COLOR_YELLOW);
        outline_rect(pixels, stride, (int)x - 4, (int)y - 4,
                     9, 9, COLOR_WHITE);
    }
    draw_text(pixels, stride, 8, 190, "UP TO 5 GT911 CONTACTS",
              COLOR_MUTED, 1U, 22U);
}

static void draw_system(const console_shell_t *shell,
                        uint16_t *pixels, size_t stride)
{
    draw_text(pixels, stride, 12, 40, "SOC", COLOR_MUTED, 1U, 3U);
    draw_text(pixels, stride, 112, 40, "ESP32-P4 V1.3", COLOR_WHITE, 1U, 13U);
    draw_text(pixels, stride, 12, 58, "RTOS", COLOR_MUTED, 1U, 4U);
    draw_text(pixels, stride, 112, 58, "FREERTOS / IDF 5.5.3", COLOR_WHITE, 1U, 20U);
    draw_text(pixels, stride, 12, 76, "UPTIME SEC", COLOR_MUTED, 1U, 10U);
    draw_u32(pixels, stride, 112, 76, shell->runtime.uptime_seconds, COLOR_GREEN);
    draw_text(pixels, stride, 12, 94, "INTERNAL FREE KIB", COLOR_MUTED, 1U, 17U);
    draw_u32(pixels, stride, 142, 94, shell->runtime.internal_free_kib, COLOR_GREEN);
    draw_text(pixels, stride, 12, 112, "PSRAM FREE KIB", COLOR_MUTED, 1U, 14U);
    draw_u32(pixels, stride, 142, 112, shell->runtime.psram_free_kib, COLOR_GREEN);
    draw_text(pixels, stride, 12, 130, "TOUCH", COLOR_MUTED, 1U, 5U);
    draw_text(pixels, stride, 112, 130,
              shell->runtime.touch_ready ? "READY" : "OFFLINE",
              shell->runtime.touch_ready ? COLOR_GREEN : COLOR_RED, 1U, 7U);
    draw_text(pixels, stride, 12, 166, "STATIC APPS / SHARED SERVICES",
              COLOR_CYAN, 1U, 28U);
}

static void draw_audio(const console_shell_t *shell,
                       uint16_t *pixels, size_t stride)
{
    draw_text(pixels, stride, 12, 40, "DOOM AUDIO HANDOFF",
              COLOR_WHITE, 1U, 18U);
    draw_text(pixels, stride, 12, 56,
              shell->runtime.audio_handoff_ready ? "READY" : "BLOCKED",
              shell->runtime.audio_handoff_ready ? COLOR_GREEN : COLOR_RED,
              2U, 7U);
    draw_text(pixels, stride, 12, 78, "PCM16 STEREO / 16000 HZ",
              COLOR_MUTED, 1U, 23U);
    draw_text(pixels, stride, 12, 94, "SFX + MUS / 16 VOICES",
              COLOR_MUTED, 1U, 22U);
    draw_text(pixels, stride, 12, 110, "VOLUME 6 / 10",
              COLOR_YELLOW, 1U, 13U);
    draw_text(pixels, stride, 12, 126, "FACTORY I2S1 GPIO30 PATH",
              COLOR_MUTED, 1U, 25U);
    draw_text(pixels, stride, 12, 150, "SHELL KEEPS AMP SAFE",
              COLOR_CYAN, 1U, 20U);
    draw_text(pixels, stride, 12, 166, "AUDIO STARTS INSIDE DOOM",
              COLOR_CYAN, 1U, 24U);
    draw_text(pixels, stride, 12, 188, "NO EXTERNAL MIDI DEVICE",
              COLOR_MUTED, 1U, 23U);
}

bool console_shell_render_rgb565(console_shell_t *shell,
                                 uint16_t *pixels,
                                 size_t stride_pixels)
{
    if (shell == NULL || pixels == NULL ||
        stride_pixels < CONSOLE_SHELL_WIDTH) {
        return false;
    }
    fill_rect(pixels, stride_pixels, 0, 0,
              CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT, COLOR_BLACK);
    if (shell->page == CONSOLE_PAGE_HOME) {
        draw_home(shell, pixels, stride_pixels);
    } else {
        draw_detail_header(shell, pixels, stride_pixels);
        switch (shell->page) {
        case CONSOLE_PAGE_COLORS:
            draw_colors(pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_TOUCH:
            draw_touch(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_SYSTEM:
            draw_system(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_AUDIO:
            draw_audio(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_EXTERNAL:
            draw_text(pixels, stride_pixels, 12, 60,
                      "EXTERNAL APP HANDOFF", COLOR_YELLOW, 1U, 20U);
            break;
        case CONSOLE_PAGE_HOME:
        default:
            break;
        }
    }
    shell->dirty = false;
    ++shell->render_generation;
    return true;
}
