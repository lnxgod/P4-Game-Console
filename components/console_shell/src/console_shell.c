// SPDX-License-Identifier: MIT

#include "console/shell.h"

#include <limits.h>
#include <string.h>

enum {
    WINDOW_LEFT = 3,
    WINDOW_TOP = 3,
    WINDOW_WIDTH = 314,
    WINDOW_HEIGHT = 194,
    TITLE_LEFT = 7,
    TITLE_TOP = 7,
    TITLE_WIDTH = 306,
    TITLE_HEIGHT = 17,
    TILE_LEFT = 11,
    TILE_TOP = 44,
    TILE_WIDTH = 91,
    TILE_HEIGHT = 61,
    TILE_COLUMN_GAP = 4,
    TILE_ROW_GAP = 5,
    GRID_WIDTH = CONSOLE_SHELL_APP_COLUMNS * TILE_WIDTH +
        (CONSOLE_SHELL_APP_COLUMNS - 1) * TILE_COLUMN_GAP,
    GRID_HEIGHT = CONSOLE_SHELL_VISIBLE_APP_ROWS * TILE_HEIGHT +
        (CONSOLE_SHELL_VISIBLE_APP_ROWS - 1) * TILE_ROW_GAP,
    BACK_LEFT = 6,
    BACK_TOP = 6,
    BACK_WIDTH = 54,
    BACK_HEIGHT = 20,
    BACK_CONTROL = CONSOLE_SHELL_MAX_APPS,
    FOLDER_UP_CONTROL,
    SCROLL_UP_CONTROL,
    SCROLL_DOWN_CONTROL,
    HOME_ITEM_CONTROL_BASE,
    SCROLL_LEFT = 297,
    SCROLL_WIDTH = 15,
    SCROLL_UP_TOP = 43,
    SCROLL_DOWN_TOP = 158,
    SCROLL_BUTTON_HEIGHT = 15,
    SCROLL_TRACK_TOP = 58,
    SCROLL_TRACK_HEIGHT = 100,
    SCROLL_DRAG_THRESHOLD = 18,
    SCROLL_DRAG_ROW_STEP = 32,
    FILE_LIST_LEFT = 8,
    FILE_LIST_TOP = 48,
    FILE_LIST_WIDTH = 304,
    FILE_ROW_HEIGHT = 20,
    FILE_BUTTON_TOP = 174,
    FILE_BUTTON_HEIGHT = 20,
    FILE_PREV_LEFT = 8,
    FILE_PREV_WIDTH = 56,
    FILE_NEXT_LEFT = 68,
    FILE_NEXT_WIDTH = 56,
    FILE_REFRESH_LEFT = 128,
    FILE_REFRESH_WIDTH = 83,
    FILE_DELETE_LEFT = 215,
    FILE_DELETE_WIDTH = 97,
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
static const uint16_t COLOR_DESKTOP = UINT16_C(0x0410);
static const uint16_t COLOR_FACE = UINT16_C(0xC618);
static const uint16_t COLOR_LIGHT = UINT16_C(0xFFFF);
static const uint16_t COLOR_SHADOW = UINT16_C(0x8410);
static const uint16_t COLOR_DARK = UINT16_C(0x4208);
static const uint16_t COLOR_TITLE = UINT16_C(0x0010);
static const uint16_t COLOR_GROUP = UINT16_C(0xE71C);

typedef enum {
    HOME_ITEM_APP = 0,
    HOME_ITEM_FOLDER,
    HOME_ITEM_ALL_PROGRAMS,
} home_item_kind_t;

typedef struct {
    home_item_kind_t kind;
    size_t app_index;
    char title[CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES];
    uint16_t accent_rgb565;
    uint32_t capabilities;
    size_t program_count;
    bool enabled;
} home_item_t;

enum {
    HOME_ITEM_CAPACITY = CONSOLE_SHELL_MAX_APPS + 1,
    FILE_ROW_CONTROL_BASE = HOME_ITEM_CONTROL_BASE + HOME_ITEM_CAPACITY,
    FILE_PREV_CONTROL =
        FILE_ROW_CONTROL_BASE + CONSOLE_SHELL_FILE_VISIBLE_ROWS,
    FILE_NEXT_CONTROL,
    FILE_REFRESH_CONTROL,
    FILE_DELETE_CONTROL,
    FILE_CANCEL_CONTROL,
    FILE_CONFIRM_CONTROL,
};

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

static bool folder_lead_character(char character)
{
    return (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9');
}

static bool folder_character(char character)
{
    return folder_lead_character(character) ||
           character == ' ' || character == '-';
}

static bool folder_path_is_valid(const char *path)
{
    const size_t length = bounded_length(
        path, CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES);
    if (length == 0U || length >= CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES) {
        return false;
    }

    size_t segment_length = 0U;
    unsigned slash_count = 0U;
    for (size_t i = 0U; i < length; ++i) {
        const char character = path[i];
        if (character == '/') {
            if (segment_length == 0U ||
                segment_length >= CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES ||
                slash_count != 0U) {
                return false;
            }
            ++slash_count;
            segment_length = 0U;
            continue;
        }
        if ((segment_length == 0U && !folder_lead_character(character)) ||
            !folder_character(character) ||
            segment_length + 1U >=
                CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES) {
            return false;
        }
        ++segment_length;
    }
    return segment_length > 0U;
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
            !folder_path_is_valid(app->folder_path) ||
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

static bool path_is_at_or_below(const char *path, const char *prefix)
{
    const size_t prefix_length = bounded_length(
        prefix, CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES);
    return prefix_length > 0U &&
           strncmp(path, prefix, prefix_length) == 0 &&
           (path[prefix_length] == '\0' || path[prefix_length] == '/');
}

static bool immediate_child_segment(
    const char *path,
    const char *parent,
    char segment[CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES])
{
    const char *remainder = path;
    if (parent[0] != '\0') {
        const size_t parent_length = bounded_length(
            parent, CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES);
        if (strncmp(path, parent, parent_length) != 0 ||
            path[parent_length] != '/') {
            return false;
        }
        remainder = path + parent_length + 1U;
    }
    size_t length = 0U;
    while (remainder[length] != '\0' && remainder[length] != '/' &&
           length + 1U < CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES) {
        segment[length] = remainder[length];
        ++length;
    }
    if (length == 0U) {
        return false;
    }
    segment[length] = '\0';
    return true;
}

static bool item_title_equal(const home_item_t *item, const char *title)
{
    return item->kind == HOME_ITEM_FOLDER &&
           strncmp(item->title, title,
                   CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES) == 0;
}

static bool compose_child_path(
    const char *parent,
    const char *segment,
    char path[CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES])
{
    const size_t parent_length = bounded_length(
        parent, CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES);
    const size_t segment_length = bounded_length(
        segment, CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES);
    const size_t separator = parent_length == 0U ? 0U : 1U;
    if (segment_length == 0U ||
        parent_length + separator + segment_length >=
            CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES) {
        return false;
    }
    if (parent_length > 0U) {
        memcpy(path, parent, parent_length);
        path[parent_length] = '/';
    }
    memcpy(path + parent_length + separator, segment, segment_length);
    path[parent_length + separator + segment_length] = '\0';
    return true;
}

static void populate_folder_item(
    const console_shell_t *shell,
    const char *parent,
    const char *segment,
    home_item_t *item)
{
    memset(item, 0, sizeof(*item));
    item->kind = HOME_ITEM_FOLDER;
    item->app_index = SIZE_MAX;
    const size_t title_length = bounded_length(
        segment, sizeof(item->title) - 1U);
    memcpy(item->title, segment, title_length);
    item->title[title_length] = '\0';
    char path[CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES];
    if (!compose_child_path(parent, segment, path)) {
        return;
    }
    bool have_accent = false;
    for (size_t i = 0U; i < shell->app_count; ++i) {
        const console_app_descriptor_t *const app = &shell->apps[i];
        if (!path_is_at_or_below(app->folder_path, path)) {
            continue;
        }
        ++item->program_count;
        item->capabilities |= app->capabilities;
        if (!have_accent || (!item->enabled && app->enabled)) {
            item->accent_rgb565 = app->accent_rgb565;
            have_accent = true;
        }
        item->enabled = item->enabled || app->enabled;
    }
}

static size_t build_home_items(
    const console_shell_t *shell,
    home_item_t items[HOME_ITEM_CAPACITY])
{
    size_t count = 0U;
    if (shell->home_all_programs) {
        for (size_t i = 0U;
             i < shell->app_count && count < HOME_ITEM_CAPACITY; ++i) {
            items[count++] = (home_item_t){
                .kind = HOME_ITEM_APP,
                .app_index = i,
                .accent_rgb565 = shell->apps[i].accent_rgb565,
                .capabilities = shell->apps[i].capabilities,
                .program_count = 1U,
                .enabled = shell->apps[i].enabled,
            };
        }
        return count;
    }

    if (shell->home_folder_path[0] == '\0') {
        home_item_t all = {
            .kind = HOME_ITEM_ALL_PROGRAMS,
            .app_index = SIZE_MAX,
            .accent_rgb565 = COLOR_TITLE,
            .program_count = shell->app_count,
        };
        for (size_t i = 0U; i < shell->app_count; ++i) {
            all.capabilities |= shell->apps[i].capabilities;
            all.enabled = all.enabled || shell->apps[i].enabled;
        }
        memcpy(all.title, "ALL PROGRAMS", sizeof("ALL PROGRAMS"));
        items[count++] = all;
    }

    for (size_t i = 0U; i < shell->app_count; ++i) {
        char segment[CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES];
        if (!immediate_child_segment(shell->apps[i].folder_path,
                                     shell->home_folder_path, segment)) {
            continue;
        }
        bool duplicate = false;
        for (size_t j = 0U; j < count; ++j) {
            if (item_title_equal(&items[j], segment)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && count < HOME_ITEM_CAPACITY) {
            populate_folder_item(shell, shell->home_folder_path,
                                 segment, &items[count++]);
        }
    }

    for (size_t i = 0U;
         i < shell->app_count && count < HOME_ITEM_CAPACITY; ++i) {
        if (strcmp(shell->apps[i].folder_path,
                   shell->home_folder_path) == 0) {
            items[count++] = (home_item_t){
                .kind = HOME_ITEM_APP,
                .app_index = i,
                .accent_rgb565 = shell->apps[i].accent_rgb565,
                .capabilities = shell->apps[i].capabilities,
                .program_count = 1U,
                .enabled = shell->apps[i].enabled,
            };
        }
    }
    return count;
}

static size_t home_item_count(const console_shell_t *shell)
{
    home_item_t items[HOME_ITEM_CAPACITY];
    return build_home_items(shell, items);
}

static size_t home_row_count(const console_shell_t *shell)
{
    const size_t count = home_item_count(shell);
    return (count + CONSOLE_SHELL_APP_COLUMNS - 1U) /
        CONSOLE_SHELL_APP_COLUMNS;
}

static size_t home_max_scroll_row(const console_shell_t *shell)
{
    const size_t rows = home_row_count(shell);
    return rows > CONSOLE_SHELL_VISIBLE_APP_ROWS
        ? rows - CONSOLE_SHELL_VISIBLE_APP_ROWS : 0U;
}

static size_t home_first_visible_index(const console_shell_t *shell)
{
    return shell->home_scroll_row * CONSOLE_SHELL_APP_COLUMNS;
}

static size_t file_last_visible_index(const console_shell_t *shell)
{
    const size_t end = shell->file_first_visible +
        CONSOLE_SHELL_FILE_VISIBLE_ROWS;
    return end < shell->files.entry_count ? end : shell->files.entry_count;
}

static bool file_can_page_previous(const console_shell_t *shell)
{
    return shell->file_first_visible > 0U;
}

static bool file_can_page_next(const console_shell_t *shell)
{
    return file_last_visible_index(shell) < shell->files.entry_count;
}

static bool file_selected_is_removable(const console_shell_t *shell)
{
    return shell->files.available &&
        shell->file_selected_index < shell->files.entry_count &&
        shell->files.entries[shell->file_selected_index].removable;
}

static bool file_selected_is_installable(const console_shell_t *shell)
{
    return shell != NULL && shell->files.available &&
        shell->file_selected_index < shell->files.entry_count &&
        shell->files.entries[shell->file_selected_index].installable;
}

static bool file_selected_is_actionable(const console_shell_t *shell)
{
    return file_selected_is_removable(shell) ||
        file_selected_is_installable(shell);
}

static void normalize_file_selection(console_shell_t *shell)
{
    if (shell->files.entry_count == 0U) {
        shell->file_selected_index = 0U;
        shell->file_first_visible = 0U;
        shell->file_delete_confirm = false;
        return;
    }
    if (shell->file_selected_index >= shell->files.entry_count) {
        shell->file_selected_index = shell->files.entry_count - 1U;
    }
    const size_t page = shell->file_selected_index /
        CONSOLE_SHELL_FILE_VISIBLE_ROWS;
    shell->file_first_visible =
        page * CONSOLE_SHELL_FILE_VISIBLE_ROWS;
    if (!file_selected_is_actionable(shell)) {
        shell->file_delete_confirm = false;
    }
}

static void select_first_visible_item(console_shell_t *shell)
{
    home_item_t items[HOME_ITEM_CAPACITY];
    const size_t count = build_home_items(shell, items);
    const size_t first = home_first_visible_index(shell);
    const size_t last = first + CONSOLE_SHELL_APPS_PER_VIEW < count
        ? first + CONSOLE_SHELL_APPS_PER_VIEW : count;
    shell->selected_home_item = first < count ? first : 0U;
    for (size_t index = first; index < last; ++index) {
        if (items[index].enabled) {
            shell->selected_home_item = index;
            if (items[index].kind == HOME_ITEM_APP) {
                shell->selected_index = items[index].app_index;
            }
            return;
        }
    }
}

static bool set_home_scroll_row(console_shell_t *shell, size_t row)
{
    const size_t maximum = home_max_scroll_row(shell);
    const size_t bounded = row > maximum ? maximum : row;
    if (bounded == shell->home_scroll_row) {
        return false;
    }
    shell->home_scroll_row = bounded;
    select_first_visible_item(shell);
    shell->dirty = true;
    return true;
}

static size_t control_at(const console_shell_t *shell,
                         uint16_t gui_x,
                         uint16_t gui_y)
{
    if (shell->page != CONSOLE_PAGE_HOME) {
        if (point_in_rect(gui_x, gui_y,
                          BACK_LEFT, BACK_TOP,
                          BACK_WIDTH, BACK_HEIGHT)) {
            return BACK_CONTROL;
        }
        if (shell->page != CONSOLE_PAGE_FILES &&
            shell->page != CONSOLE_PAGE_GAMES) {
            return SIZE_MAX;
        }
        if (shell->file_delete_confirm) {
            if (point_in_rect(gui_x, gui_y,
                              FILE_REFRESH_LEFT, FILE_BUTTON_TOP,
                              FILE_REFRESH_WIDTH, FILE_BUTTON_HEIGHT)) {
                return FILE_CANCEL_CONTROL;
            }
            return point_in_rect(gui_x, gui_y,
                                 FILE_DELETE_LEFT, FILE_BUTTON_TOP,
                                 FILE_DELETE_WIDTH, FILE_BUTTON_HEIGHT)
                ? FILE_CONFIRM_CONTROL : SIZE_MAX;
        }
        for (size_t row = 0U;
             row < CONSOLE_SHELL_FILE_VISIBLE_ROWS; ++row) {
            const size_t index = shell->file_first_visible + row;
            if (index < shell->files.entry_count &&
                point_in_rect(gui_x, gui_y,
                              FILE_LIST_LEFT,
                              FILE_LIST_TOP +
                                  (unsigned)row * FILE_ROW_HEIGHT,
                              FILE_LIST_WIDTH, FILE_ROW_HEIGHT)) {
                return FILE_ROW_CONTROL_BASE + row;
            }
        }
        if (file_can_page_previous(shell) &&
            point_in_rect(gui_x, gui_y,
                          FILE_PREV_LEFT, FILE_BUTTON_TOP,
                          FILE_PREV_WIDTH, FILE_BUTTON_HEIGHT)) {
            return FILE_PREV_CONTROL;
        }
        if (file_can_page_next(shell) &&
            point_in_rect(gui_x, gui_y,
                          FILE_NEXT_LEFT, FILE_BUTTON_TOP,
                          FILE_NEXT_WIDTH, FILE_BUTTON_HEIGHT)) {
            return FILE_NEXT_CONTROL;
        }
        if (point_in_rect(gui_x, gui_y,
                          FILE_REFRESH_LEFT, FILE_BUTTON_TOP,
                          FILE_REFRESH_WIDTH, FILE_BUTTON_HEIGHT)) {
            return FILE_REFRESH_CONTROL;
        }
        if (file_selected_is_actionable(shell) &&
            point_in_rect(gui_x, gui_y,
                          FILE_DELETE_LEFT, FILE_BUTTON_TOP,
                          FILE_DELETE_WIDTH, FILE_BUTTON_HEIGHT)) {
            return FILE_DELETE_CONTROL;
        }
        return SIZE_MAX;
    }

    if ((shell->home_all_programs || shell->home_folder_path[0] != '\0') &&
        point_in_rect(gui_x, gui_y, 8U, 26U, 31U, 13U)) {
        return FOLDER_UP_CONTROL;
    }

    if (home_max_scroll_row(shell) > 0U) {
        if (point_in_rect(gui_x, gui_y,
                          SCROLL_LEFT, SCROLL_UP_TOP,
                          SCROLL_WIDTH, SCROLL_BUTTON_HEIGHT) &&
            shell->home_scroll_row > 0U) {
            return SCROLL_UP_CONTROL;
        }
        if (point_in_rect(gui_x, gui_y,
                          SCROLL_LEFT, SCROLL_DOWN_TOP,
                          SCROLL_WIDTH, SCROLL_BUTTON_HEIGHT) &&
            shell->home_scroll_row < home_max_scroll_row(shell)) {
            return SCROLL_DOWN_CONTROL;
        }
    }

    home_item_t items[HOME_ITEM_CAPACITY];
    const size_t count = build_home_items(shell, items);
    const size_t first = home_first_visible_index(shell);
    const size_t last = first + CONSOLE_SHELL_APPS_PER_VIEW < count
        ? first + CONSOLE_SHELL_APPS_PER_VIEW : count;
    for (size_t index = first; index < last; ++index) {
        const size_t view_index = index - first;
        const unsigned column =
            (unsigned)(view_index % CONSOLE_SHELL_APP_COLUMNS);
        const unsigned row =
            (unsigned)(view_index / CONSOLE_SHELL_APP_COLUMNS);
        const unsigned left = TILE_LEFT +
            column * (TILE_WIDTH + TILE_COLUMN_GAP);
        const unsigned top = TILE_TOP +
            row * (TILE_HEIGHT + TILE_ROW_GAP);
        if (point_in_rect(gui_x, gui_y, left, top,
                          TILE_WIDTH, TILE_HEIGHT)) {
            return items[index].enabled
                ? HOME_ITEM_CONTROL_BASE + index : SIZE_MAX;
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

static void reset_home_grid(console_shell_t *shell)
{
    shell->home_scroll_row = 0U;
    shell->selected_home_item = 0U;
    select_first_visible_item(shell);
    shell->dirty = true;
}

static void open_home_folder(console_shell_t *shell, const char *segment)
{
    char path[CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES];
    if (!compose_child_path(shell->home_folder_path, segment, path)) {
        return;
    }
    const size_t length = bounded_length(
        path, CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES);
    memcpy(shell->home_folder_path, path, length + 1U);
    shell->home_all_programs = false;
    reset_home_grid(shell);
}

static void open_all_programs(console_shell_t *shell)
{
    shell->home_folder_path[0] = '\0';
    shell->home_all_programs = true;
    reset_home_grid(shell);
}

static void navigate_home_up(console_shell_t *shell)
{
    if (shell->home_all_programs) {
        shell->home_all_programs = false;
        shell->home_folder_path[0] = '\0';
        reset_home_grid(shell);
        return;
    }
    char *const separator = strrchr(shell->home_folder_path, '/');
    if (separator != NULL) {
        *separator = '\0';
    } else {
        shell->home_folder_path[0] = '\0';
    }
    reset_home_grid(shell);
}

static console_shell_action_t no_action(void)
{
    const console_shell_action_t action = {
        .type = CONSOLE_ACTION_NONE,
        .app_id = 0U,
        .file_source_index = UINT32_MAX,
    };
    return action;
}

static console_shell_action_t page_changed(uint32_t app_id)
{
    const console_shell_action_t action = {
        .type = CONSOLE_ACTION_PAGE_CHANGED,
        .app_id = app_id,
        .file_source_index = UINT32_MAX,
    };
    return action;
}

static bool update_home_drag(console_shell_t *shell,
                             uint16_t gui_x,
                             uint16_t gui_y)
{
    if (!shell->scroll_candidate || shell->page != CONSOLE_PAGE_HOME ||
        home_max_scroll_row(shell) == 0U) {
        return false;
    }
    const int vertical = (int)shell->press_start_gui_y - (int)gui_y;
    const int horizontal = (int)shell->press_start_gui_x - (int)gui_x;
    const int vertical_magnitude = vertical < 0 ? -vertical : vertical;
    const int horizontal_magnitude = horizontal < 0 ? -horizontal : horizontal;
    if (vertical_magnitude < SCROLL_DRAG_THRESHOLD ||
        vertical_magnitude < horizontal_magnitude) {
        return false;
    }

    int row_delta = 0;
    if (vertical > 0) {
        row_delta = 1 +
            (vertical - SCROLL_DRAG_THRESHOLD) / SCROLL_DRAG_ROW_STEP;
    } else {
        row_delta = -1 +
            (vertical + SCROLL_DRAG_THRESHOLD) / SCROLL_DRAG_ROW_STEP;
    }
    size_t requested = shell->press_start_scroll_row;
    if (row_delta > 0) {
        const size_t increase = (size_t)row_delta;
        const size_t maximum = home_max_scroll_row(shell);
        requested = increase > maximum - requested
            ? maximum : requested + increase;
    } else {
        const size_t decrease = (size_t)(-row_delta);
        requested = decrease > requested ? 0U : requested - decrease;
    }
    const bool was_pressed = shell->press_active;
    shell->scroll_gesture = true;
    shell->press_active = false;
    shell->pressed_index = SIZE_MAX;
    if (was_pressed) {
        shell->dirty = true;
    }
    (void)set_home_scroll_row(shell, requested);
    return true;
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
        shell->scroll_candidate = false;
        shell->scroll_gesture = false;
        shell->pressed_index = SIZE_MAX;
        return no_action();
    }

    if (contact_count == 0U) {
        if (!shell->contact_down) {
            return no_action();
        }
        shell->contact_down = false;
        shell->scroll_candidate = false;
        if (shell->scroll_gesture) {
            const bool changed =
                shell->home_scroll_row != shell->press_start_scroll_row;
            shell->scroll_gesture = false;
            shell->press_active = false;
            shell->pressed_index = SIZE_MAX;
            shell->dirty = true;
            if (changed) {
                const console_shell_action_t action = {
                    .type = CONSOLE_ACTION_PAGE_CHANGED,
                    .app_id = 0U,
                };
                return action;
            }
            return no_action();
        }
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
            return page_changed(0U);
        }
        if (shell->page == CONSOLE_PAGE_FILES ||
            shell->page == CONSOLE_PAGE_GAMES) {
            shell->file_notice = CONSOLE_FILE_NOTICE_NONE;
            if (released_control >= FILE_ROW_CONTROL_BASE &&
                released_control < FILE_ROW_CONTROL_BASE +
                    CONSOLE_SHELL_FILE_VISIBLE_ROWS) {
                const size_t row = released_control - FILE_ROW_CONTROL_BASE;
                const size_t index = shell->file_first_visible + row;
                if (index < shell->files.entry_count) {
                    shell->file_selected_index = index;
                    shell->file_delete_confirm = false;
                    return page_changed(shell->active_app_id);
                }
                return no_action();
            }
            if (released_control == FILE_PREV_CONTROL &&
                file_can_page_previous(shell)) {
                const size_t step = CONSOLE_SHELL_FILE_VISIBLE_ROWS;
                shell->file_first_visible = step > shell->file_first_visible
                    ? 0U : shell->file_first_visible - step;
                shell->file_selected_index = shell->file_first_visible;
                shell->file_delete_confirm = false;
                return page_changed(shell->active_app_id);
            }
            if (released_control == FILE_NEXT_CONTROL &&
                file_can_page_next(shell)) {
                shell->file_first_visible +=
                    CONSOLE_SHELL_FILE_VISIBLE_ROWS;
                shell->file_selected_index = shell->file_first_visible;
                shell->file_delete_confirm = false;
                return page_changed(shell->active_app_id);
            }
            if (released_control == FILE_REFRESH_CONTROL) {
                shell->file_delete_confirm = false;
                const console_shell_action_t action = {
                    .type = shell->page == CONSOLE_PAGE_GAMES
                        ? CONSOLE_ACTION_GAME_REFRESH
                        : CONSOLE_ACTION_FILE_REFRESH,
                    .app_id = shell->active_app_id,
                    .file_source_index = UINT32_MAX,
                };
                return action;
            }
            if (released_control == FILE_DELETE_CONTROL &&
                file_selected_is_actionable(shell)) {
                shell->file_delete_confirm = true;
                return page_changed(shell->active_app_id);
            }
            if (released_control == FILE_CANCEL_CONTROL) {
                shell->file_delete_confirm = false;
                return page_changed(shell->active_app_id);
            }
            if (released_control == FILE_CONFIRM_CONTROL &&
                file_selected_is_actionable(shell)) {
                shell->file_delete_confirm = false;
                const console_shell_action_t action = {
                    .type = shell->page == CONSOLE_PAGE_GAMES
                        ? (file_selected_is_installable(shell)
                            ? CONSOLE_ACTION_OS_UPDATE_INSTALL
                            : CONSOLE_ACTION_GAME_REMOVE)
                        : CONSOLE_ACTION_FILE_DELETE,
                    .app_id = shell->active_app_id,
                    .file_source_index = shell->files.entries[
                        shell->file_selected_index].source_index,
                };
                return action;
            }
            return no_action();
        }
        if (released_control == FOLDER_UP_CONTROL) {
            navigate_home_up(shell);
            return page_changed(0U);
        }
        if (released_control == SCROLL_UP_CONTROL ||
            released_control == SCROLL_DOWN_CONTROL) {
            const size_t requested = released_control == SCROLL_UP_CONTROL
                ? shell->home_scroll_row - 1U
                : shell->home_scroll_row + 1U;
            (void)set_home_scroll_row(shell, requested);
            return page_changed(0U);
        }
        if (released_control < HOME_ITEM_CONTROL_BASE) {
            return no_action();
        }
        home_item_t items[HOME_ITEM_CAPACITY];
        const size_t item_count = build_home_items(shell, items);
        const size_t item_index = released_control - HOME_ITEM_CONTROL_BASE;
        if (item_index >= item_count || !items[item_index].enabled) {
            return no_action();
        }
        shell->selected_home_item = item_index;
        if (items[item_index].kind == HOME_ITEM_ALL_PROGRAMS) {
            open_all_programs(shell);
            return page_changed(0U);
        }
        if (items[item_index].kind == HOME_ITEM_FOLDER) {
            open_home_folder(shell, items[item_index].title);
            return page_changed(0U);
        }
        if (items[item_index].app_index >= shell->app_count) {
            return no_action();
        }

        const size_t app_index = items[item_index].app_index;
        const console_app_descriptor_t *const app = &shell->apps[app_index];
        shell->selected_index = app_index;
        if (app->page == CONSOLE_PAGE_EXTERNAL) {
            const console_shell_action_t action = {
                .type = CONSOLE_ACTION_LAUNCH,
                .app_id = app->id,
                .file_source_index = UINT32_MAX,
            };
            return action;
        }
        shell->page = app->page;
        shell->active_app_id = app->id;
        if (app->page == CONSOLE_PAGE_FILES ||
            app->page == CONSOLE_PAGE_GAMES) {
            shell->file_delete_confirm = false;
            shell->file_notice = CONSOLE_FILE_NOTICE_NONE;
            normalize_file_selection(shell);
        }
        return page_changed(app->id);
    }

    if (contact_count != 1U) {
        if (shell->press_active) {
            shell->dirty = true;
        }
        shell->contact_down = true;
        shell->press_active = false;
        shell->scroll_candidate = false;
        shell->scroll_gesture = false;
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
        shell->scroll_candidate = false;
        shell->scroll_gesture = false;
        shell->pressed_index = SIZE_MAX;
        return no_action();
    }

    const size_t control = control_at(shell, gui_x, gui_y);
    if (!shell->contact_down) {
        shell->contact_down = true;
        shell->press_start_gui_x = gui_x;
        shell->press_start_gui_y = gui_y;
        shell->press_start_scroll_row = shell->home_scroll_row;
        shell->scroll_candidate = shell->page == CONSOLE_PAGE_HOME &&
            home_max_scroll_row(shell) > 0U &&
            point_in_rect(gui_x, gui_y, TILE_LEFT, TILE_TOP,
                          GRID_WIDTH, GRID_HEIGHT);
        shell->scroll_gesture = false;
        shell->pressed_index = control;
        shell->press_active = control != SIZE_MAX;
        if (shell->press_active) {
            shell->dirty = true;
        }
        return no_action();
    }

    if (update_home_drag(shell, gui_x, gui_y)) {
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
        shell->runtime.game_storage_kib != runtime->game_storage_kib ||
        shell->runtime.game_storage_state != runtime->game_storage_state ||
        shell->runtime.touch_ready != runtime->touch_ready ||
        shell->runtime.audio_handoff_ready != runtime->audio_handoff_ready ||
        shell->runtime.game_storage_usb_attached !=
            runtime->game_storage_usb_attached ||
        shell->runtime.doom_wad_ready != runtime->doom_wad_ready;
    if (!changed) {
        return;
    }
    const bool storage_changed =
        shell->runtime.game_storage_kib != runtime->game_storage_kib ||
        shell->runtime.game_storage_state != runtime->game_storage_state ||
        shell->runtime.game_storage_usb_attached !=
            runtime->game_storage_usb_attached ||
        shell->runtime.doom_wad_ready != runtime->doom_wad_ready;
    shell->runtime = *runtime;
    if (shell->page == CONSOLE_PAGE_SYSTEM ||
        shell->page == CONSOLE_PAGE_FILES ||
        shell->page == CONSOLE_PAGE_GAMES ||
        shell->page == CONSOLE_PAGE_AUDIO ||
        (shell->page == CONSOLE_PAGE_HOME && storage_changed)) {
        shell->dirty = true;
    }
}

bool console_shell_set_file_listing(
    console_shell_t *shell,
    const console_shell_file_listing_t *listing)
{
    if (shell == NULL || listing == NULL ||
        listing->entry_count > CONSOLE_SHELL_FILE_MAX_ENTRIES ||
        listing->total_visible_entries < listing->entry_count) {
        return false;
    }
    for (size_t i = 0U; i < listing->entry_count; ++i) {
        const console_shell_file_entry_t *const entry = &listing->entries[i];
        const size_t label_length = bounded_length(
            entry->label, CONSOLE_SHELL_FILE_LABEL_MAX_BYTES);
        if (label_length == 0U ||
            label_length >= CONSOLE_SHELL_FILE_LABEL_MAX_BYTES ||
            (entry->is_directory &&
             (entry->removable || entry->installable)) ||
            (entry->removable && entry->installable)) {
            return false;
        }
    }
    shell->files = *listing;
    normalize_file_selection(shell);
    if (shell->page == CONSOLE_PAGE_FILES ||
        shell->page == CONSOLE_PAGE_GAMES) {
        shell->dirty = true;
    }
    return true;
}

void console_shell_set_file_notice(
    console_shell_t *shell,
    console_shell_file_notice_t notice)
{
    if (shell == NULL || notice < CONSOLE_FILE_NOTICE_NONE ||
        notice > CONSOLE_FILE_NOTICE_UPDATING) {
        return;
    }
    if (shell->file_notice == notice) {
        return;
    }
    shell->file_notice = notice;
    if (shell->page == CONSOLE_PAGE_FILES ||
        shell->page == CONSOLE_PAGE_GAMES) {
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
    shell->scroll_candidate = false;
    shell->scroll_gesture = false;
    shell->file_delete_confirm = false;
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

static void bevel_rect(uint16_t *pixels, size_t stride,
                       int x, int y, int width, int height,
                       uint16_t face, bool pressed)
{
    fill_rect(pixels, stride, x, y, width, height, face);
    const uint16_t upper = pressed ? COLOR_DARK : COLOR_LIGHT;
    const uint16_t lower = pressed ? COLOR_LIGHT : COLOR_DARK;
    fill_rect(pixels, stride, x, y, width, 1, upper);
    fill_rect(pixels, stride, x, y, 1, height, upper);
    fill_rect(pixels, stride, x, y + height - 1, width, 1, lower);
    fill_rect(pixels, stride, x + width - 1, y, 1, height, lower);
    if (width > 3 && height > 3) {
        fill_rect(pixels, stride, x + 1, y + 1, width - 2, 1,
                  pressed ? COLOR_SHADOW : COLOR_FACE);
        fill_rect(pixels, stride, x + 1, y + 1, 1, height - 2,
                  pressed ? COLOR_SHADOW : COLOR_FACE);
    }
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
    case '_': GLYPH(0,0,0,0,0,0,31); break;
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

static void draw_centered_text(uint16_t *pixels, size_t stride,
                               int left, int top, int width,
                               const char *text, uint16_t color,
                               size_t max_characters)
{
    size_t length = bounded_length(text, max_characters);
    if (length > max_characters) {
        length = max_characters;
    }
    const int text_width = (int)(length * 6U);
    const int x = text_width < width
        ? left + (width - text_width) / 2 : left;
    draw_text(pixels, stride, x, top, text, color, 1U, max_characters);
}

static void draw_program_icon(uint16_t *pixels, size_t stride,
                              int tile_left, int tile_top,
                              const console_app_descriptor_t *app)
{
    const int left = tile_left + (TILE_WIDTH - 26) / 2;
    const int top = tile_top + 4;
    bevel_rect(pixels, stride, left, top, 26, 22, COLOR_FACE, false);
    const uint16_t accent = app->enabled
        ? app->accent_rgb565 : COLOR_SHADOW;
    fill_rect(pixels, stride, left + 3, top + 3, 20, 4, accent);
    fill_rect(pixels, stride, left + 4, top + 9, 8, 8, COLOR_GROUP);
    outline_rect(pixels, stride, left + 4, top + 9, 8, 8, COLOR_DARK);
    fill_rect(pixels, stride, left + 15, top + 9, 7, 2, accent);
    fill_rect(pixels, stride, left + 15, top + 13, 7, 2, COLOR_SHADOW);
    fill_rect(pixels, stride, left + 15, top + 17, 5, 1, COLOR_DARK);
}

static void draw_folder_icon(uint16_t *pixels, size_t stride,
                             int tile_left, int tile_top,
                             uint16_t accent, bool enabled)
{
    const int left = tile_left + (TILE_WIDTH - 30) / 2;
    const int top = tile_top + 5;
    const uint16_t face = enabled ? COLOR_YELLOW : COLOR_SHADOW;
    fill_rect(pixels, stride, left + 2, top + 1, 12, 5, face);
    outline_rect(pixels, stride, left + 2, top + 1, 12, 5, COLOR_DARK);
    bevel_rect(pixels, stride, left, top + 5, 30, 18, face, false);
    fill_rect(pixels, stride, left + 4, top + 10, 22, 2,
              enabled ? accent : COLOR_DARK);
}

static size_t decimal_width(uint32_t value)
{
    size_t count = 1U;
    while (value >= 10U) {
        value /= 10U;
        ++count;
    }
    return count * 6U;
}

static void draw_program_count(uint16_t *pixels, size_t stride,
                               int left, int top, int width,
                               size_t count, uint16_t color)
{
    const uint32_t bounded = count > UINT32_MAX
        ? UINT32_MAX : (uint32_t)count;
    const size_t label_width = decimal_width(bounded) + 6U + 8U * 6U;
    const int x = label_width < (size_t)width
        ? left + (width - (int)label_width) / 2 : left;
    draw_u32(pixels, stride, x, top, bounded, color);
    draw_text(pixels, stride, x + (int)decimal_width(bounded) + 6,
              top, "PROGRAMS", color, 1U, 8U);
}

static void draw_scroll_arrow(uint16_t *pixels, size_t stride,
                              int top, bool up, bool enabled,
                              bool pressed)
{
    bevel_rect(pixels, stride, SCROLL_LEFT, top,
               SCROLL_WIDTH, SCROLL_BUTTON_HEIGHT,
               COLOR_FACE, pressed);
    const uint16_t color = enabled ? COLOR_BLACK : COLOR_SHADOW;
    const int center = SCROLL_LEFT + SCROLL_WIDTH / 2;
    for (int row = 0; row < 4; ++row) {
        const int width = up ? row * 2 + 1 : (4 - row) * 2 - 1;
        const int y = up ? top + 4 + row : top + 4 + row;
        fill_rect(pixels, stride, center - width / 2, y, width, 1, color);
    }
}

static void draw_scrollbar(console_shell_t *shell,
                           uint16_t *pixels, size_t stride)
{
    const size_t maximum = home_max_scroll_row(shell);
    const bool can_up = shell->home_scroll_row > 0U;
    const bool can_down = shell->home_scroll_row < maximum;
    draw_scroll_arrow(
        pixels, stride, SCROLL_UP_TOP, true, can_up,
        shell->press_active && shell->pressed_index == SCROLL_UP_CONTROL);
    draw_scroll_arrow(
        pixels, stride, SCROLL_DOWN_TOP, false, can_down,
        shell->press_active && shell->pressed_index == SCROLL_DOWN_CONTROL);
    fill_rect(pixels, stride, SCROLL_LEFT, SCROLL_TRACK_TOP,
              SCROLL_WIDTH, SCROLL_TRACK_HEIGHT, COLOR_SHADOW);
    outline_rect(pixels, stride, SCROLL_LEFT, SCROLL_TRACK_TOP,
                 SCROLL_WIDTH, SCROLL_TRACK_HEIGHT, COLOR_DARK);

    const int inner_top = SCROLL_TRACK_TOP + 2;
    const int inner_height = SCROLL_TRACK_HEIGHT - 4;
    const size_t rows = home_row_count(shell);
    int thumb_height = inner_height;
    if (rows > CONSOLE_SHELL_VISIBLE_APP_ROWS) {
        thumb_height = (int)((size_t)inner_height *
            CONSOLE_SHELL_VISIBLE_APP_ROWS / rows);
        if (thumb_height < 12) {
            thumb_height = 12;
        }
    }
    const int travel = inner_height - thumb_height;
    const int thumb_top = maximum == 0U ? inner_top :
        inner_top + (int)((size_t)travel * shell->home_scroll_row / maximum);
    bevel_rect(pixels, stride, SCROLL_LEFT + 2, thumb_top,
               SCROLL_WIDTH - 4, thumb_height, COLOR_FACE, false);
}

static void draw_home(console_shell_t *shell, uint16_t *pixels, size_t stride)
{
    fill_rect(pixels, stride, 0, 0, CONSOLE_SHELL_WIDTH,
              CONSOLE_SHELL_HEIGHT, COLOR_DESKTOP);
    bevel_rect(pixels, stride, WINDOW_LEFT, WINDOW_TOP,
               WINDOW_WIDTH, WINDOW_HEIGHT, COLOR_FACE, false);
    fill_rect(pixels, stride, TITLE_LEFT, TITLE_TOP,
              TITLE_WIDTH, TITLE_HEIGHT, COLOR_TITLE);
    fill_rect(pixels, stride, TITLE_LEFT + 3, TITLE_TOP + 4,
              7, 7, COLOR_FACE);
    outline_rect(pixels, stride, TITLE_LEFT + 3, TITLE_TOP + 4,
                 7, 7, COLOR_LIGHT);
    draw_text(pixels, stride, TITLE_LEFT + 15, TITLE_TOP + 5,
              "P4 PROGRAM MANAGER", COLOR_WHITE, 1U, 18U);
    bevel_rect(pixels, stride, 281, TITLE_TOP + 2, 13, 13,
               COLOR_FACE, false);
    bevel_rect(pixels, stride, 297, TITLE_TOP + 2, 13, 13,
               COLOR_FACE, false);
    fill_rect(pixels, stride, 285, TITLE_TOP + 10, 5, 1, COLOR_BLACK);
    outline_rect(pixels, stride, 300, TITLE_TOP + 5, 7, 6, COLOR_BLACK);
    if (shell->home_all_programs || shell->home_folder_path[0] != '\0') {
        const bool up_pressed = shell->press_active &&
            shell->pressed_index == FOLDER_UP_CONTROL;
        bevel_rect(pixels, stride, 8, 26, 31, 13,
                   COLOR_FACE, up_pressed);
        draw_text(pixels, stride, 14, 29, "< UP",
                  COLOR_BLACK, 1U, 4U);
        draw_text(pixels, stride, 47, 29,
                  shell->home_all_programs
                      ? "ALL PROGRAMS" : shell->home_folder_path,
                  COLOR_BLACK, 1U, 31U);
    } else {
        draw_text(pixels, stride, 11, 29, "FILE", COLOR_BLACK, 1U, 4U);
        draw_text(pixels, stride, 47, 29, "OPTIONS", COLOR_BLACK, 1U, 7U);
        draw_text(pixels, stride, 101, 29, "HELP", COLOR_BLACK, 1U, 4U);
    }
    fill_rect(pixels, stride, 8, 40, 305, 135, COLOR_GROUP);
    outline_rect(pixels, stride, 8, 40, 305, 135, COLOR_DARK);

    home_item_t items[HOME_ITEM_CAPACITY];
    const size_t item_count = build_home_items(shell, items);
    const size_t first = home_first_visible_index(shell);
    const size_t last = first + CONSOLE_SHELL_APPS_PER_VIEW < item_count
        ? first + CONSOLE_SHELL_APPS_PER_VIEW : item_count;
    for (size_t index = first; index < last; ++index) {
        const home_item_t *const item = &items[index];
        const console_app_descriptor_t *const app =
            item->kind == HOME_ITEM_APP && item->app_index < shell->app_count
                ? &shell->apps[item->app_index] : NULL;
        const size_t view_index = index - first;
        const int column =
            (int)(view_index % CONSOLE_SHELL_APP_COLUMNS);
        const int row =
            (int)(view_index / CONSOLE_SHELL_APP_COLUMNS);
        const int left = TILE_LEFT + column * (TILE_WIDTH + TILE_COLUMN_GAP);
        const int top = TILE_TOP + row * (TILE_HEIGHT + TILE_ROW_GAP);
        const bool pressed = shell->press_active &&
            shell->pressed_index == HOME_ITEM_CONTROL_BASE + index;
        fill_rect(pixels, stride, left, top, TILE_WIDTH, TILE_HEIGHT,
                  pressed ? COLOR_TITLE : COLOR_GROUP);
        if (shell->selected_home_item == index || pressed) {
            outline_rect(pixels, stride, left, top, TILE_WIDTH, TILE_HEIGHT,
                         pressed ? COLOR_WHITE : COLOR_TITLE);
        }
        if (app != NULL) {
            draw_program_icon(pixels, stride, left, top, app);
        } else {
            draw_folder_icon(pixels, stride, left, top,
                             item->accent_rgb565, item->enabled);
        }
        const uint16_t label_color = pressed ? COLOR_WHITE :
            (item->enabled ? COLOR_BLACK : COLOR_SHADOW);
        draw_centered_text(pixels, stride, left, top + 30, TILE_WIDTH,
                           app != NULL ? app->title : item->title,
                           label_color, 15U);
        if (app != NULL) {
            draw_centered_text(pixels, stride, left, top + 40, TILE_WIDTH,
                               app->enabled ? app->subtitle : "OFFLINE",
                               pressed ? COLOR_WHITE : COLOR_DARK, 13U);
        } else {
            draw_program_count(pixels, stride, left, top + 41, TILE_WIDTH,
                               item->program_count,
                               pressed ? COLOR_WHITE : COLOR_DARK);
        }
    }
    draw_scrollbar(shell, pixels, stride);
    bevel_rect(pixels, stride, 8, 178, 305, 15, COLOR_FACE, true);
    draw_u32(pixels, stride, 13, 182, (uint32_t)item_count,
             COLOR_BLACK);
    draw_text(pixels, stride, 31, 182,
              shell->home_all_programs ? "PROGRAMS" : "ITEMS",
              COLOR_BLACK, 1U, 8U);
    if (home_max_scroll_row(shell) > 0U) {
        draw_text(pixels, stride, 220, 182, "ROW", COLOR_DARK, 1U, 3U);
        draw_u32(pixels, stride, 244, 182,
                 (uint32_t)(shell->home_scroll_row + 1U), COLOR_BLACK);
        draw_text(pixels, stride, 254, 182, "/", COLOR_DARK, 1U, 1U);
        draw_u32(pixels, stride, 264, 182,
                 (uint32_t)home_row_count(shell), COLOR_BLACK);
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
    draw_text(pixels, stride, 12, 148, "GAME STORAGE", COLOR_MUTED, 1U, 12U);
    const char *storage = "STARTING";
    uint16_t storage_color = COLOR_YELLOW;
    switch (shell->runtime.game_storage_state) {
    case CONSOLE_STORAGE_READY:
        storage = "APP READY";
        storage_color = COLOR_GREEN;
        break;
    case CONSOLE_STORAGE_USB_HOST:
        storage = "USB HOST";
        storage_color = COLOR_CYAN;
        break;
    case CONSOLE_STORAGE_FORMAT_REQUIRED:
        storage = "FORMAT NEEDED";
        storage_color = COLOR_RED;
        break;
    case CONSOLE_STORAGE_MISSING:
        storage = "WAD MISSING";
        storage_color = COLOR_YELLOW;
        break;
    case CONSOLE_STORAGE_INVALID:
        storage = "WAD INVALID";
        storage_color = COLOR_RED;
        break;
    case CONSOLE_STORAGE_LOCKED:
        storage = "GAME LOCKED";
        storage_color = COLOR_GREEN;
        break;
    case CONSOLE_STORAGE_FAULT:
        storage = "OFFLINE";
        storage_color = COLOR_RED;
        break;
    case CONSOLE_STORAGE_STARTING:
    default:
        break;
    }
    draw_text(pixels, stride, 112, 148, storage,
              storage_color, 1U, 14U);
    draw_text(pixels, stride, 12, 166, "DOOM1.WAD", COLOR_MUTED, 1U, 9U);
    draw_text(pixels, stride, 112, 166,
              shell->runtime.doom_wad_ready ? "VERIFIED" : "NOT READY",
              shell->runtime.doom_wad_ready ? COLOR_GREEN : COLOR_YELLOW,
              1U, 9U);
    draw_text(pixels, stride, 12, 184, "EJECT USB BEFORE GAME",
              COLOR_CYAN, 1U, 21U);
}

static void draw_file_button(console_shell_t *shell,
                             uint16_t *pixels, size_t stride,
                             int left, int width, size_t control,
                             const char *label, bool enabled)
{
    const bool pressed = enabled && shell->press_active &&
        shell->pressed_index == control;
    bevel_rect(pixels, stride, left, FILE_BUTTON_TOP,
               width, FILE_BUTTON_HEIGHT, COLOR_FACE, pressed);
    draw_centered_text(pixels, stride, left, FILE_BUTTON_TOP + 7,
                       width, label,
                       enabled ? COLOR_BLACK : COLOR_SHADOW, 10U);
}

static void draw_file_listing_status(const console_shell_t *shell,
                                     uint16_t *pixels, size_t stride)
{
    if (!shell->files.available) {
        const char *message = "STORAGE NOT AVAILABLE";
        if (shell->runtime.game_storage_state == CONSOLE_STORAGE_USB_HOST) {
            message = "EJECT P4 GAMES ON LAPTOP";
        } else if (shell->runtime.game_storage_state ==
                   CONSOLE_STORAGE_FORMAT_REQUIRED) {
            message = "FORMAT P4 GAMES ON LAPTOP";
        } else if (shell->runtime.game_storage_state ==
                   CONSOLE_STORAGE_STARTING) {
            message = "STORAGE SWITCHING OWNERS";
        }
        draw_text(pixels, stride, 12, 83, message,
                  COLOR_TITLE, 1U, 29U);
        draw_text(pixels, stride, 12, 101,
                  "USB AND APP NEVER SHARE FAT",
                  COLOR_DARK, 1U, 27U);
        return;
    }
    if (shell->files.entry_count == 0U) {
        const bool games = shell->page == CONSOLE_PAGE_GAMES;
        draw_text(pixels, stride, 12, 83,
                  games ? "NO GAME PACKAGES" : "NO VISIBLE FILES",
                  COLOR_TITLE, 1U, 16U);
        draw_text(pixels, stride, 12, 101,
                  games ? "COPY .P4G WITH J16 USB"
                        : "COPY FILES WITH J16 USB",
                  COLOR_DARK, 1U, 24U);
    }
}

static void draw_files(console_shell_t *shell,
                       uint16_t *pixels, size_t stride)
{
    const bool games = shell->page == CONSOLE_PAGE_GAMES;
    fill_rect(pixels, stride, 0, 32, CONSOLE_SHELL_WIDTH,
              CONSOLE_SHELL_HEIGHT - 32, COLOR_FACE);
    draw_text(pixels, stride, 12, 37,
              games ? "GAME / UPDATE" : "NAME",
              COLOR_DARK, 1U, 13U);
    draw_text(pixels, stride, 248, 37,
              games ? "KIB" : "SIZE KIB",
              COLOR_DARK, 1U, 8U);
    fill_rect(pixels, stride, FILE_LIST_LEFT, FILE_LIST_TOP,
              FILE_LIST_WIDTH,
              FILE_ROW_HEIGHT * CONSOLE_SHELL_FILE_VISIBLE_ROWS,
              COLOR_GROUP);
    outline_rect(pixels, stride, FILE_LIST_LEFT, FILE_LIST_TOP,
                 FILE_LIST_WIDTH,
                 FILE_ROW_HEIGHT * CONSOLE_SHELL_FILE_VISIBLE_ROWS,
                 COLOR_DARK);

    if (shell->files.available && shell->files.entry_count > 0U) {
        const size_t last = file_last_visible_index(shell);
        for (size_t index = shell->file_first_visible;
             index < last; ++index) {
            const size_t row = index - shell->file_first_visible;
            const int top = FILE_LIST_TOP + (int)row * FILE_ROW_HEIGHT;
            const console_shell_file_entry_t *const entry =
                &shell->files.entries[index];
            const bool selected = index == shell->file_selected_index;
            fill_rect(pixels, stride, FILE_LIST_LEFT + 1, top + 1,
                      FILE_LIST_WIDTH - 2, FILE_ROW_HEIGHT - 1,
                      selected ? COLOR_TITLE : COLOR_GROUP);
            if (row > 0U) {
                fill_rect(pixels, stride, FILE_LIST_LEFT + 1, top,
                          FILE_LIST_WIDTH - 2, 1, COLOR_SHADOW);
            }
            const uint16_t text_color = selected ? COLOR_WHITE : COLOR_BLACK;
            if (entry->is_directory) {
                fill_rect(pixels, stride, 13, top + 6, 12, 8,
                          selected ? COLOR_YELLOW : UINT16_C(0xFD20));
                fill_rect(pixels, stride, 15, top + 4, 6, 3,
                          selected ? COLOR_YELLOW : UINT16_C(0xFD20));
            } else {
                bevel_rect(pixels, stride, 13, top + 4, 12, 12,
                           COLOR_FACE, false);
                fill_rect(pixels, stride, 16, top + 7, 6, 1,
                          entry->installable ? COLOR_GREEN :
                          (entry->removable ? COLOR_CYAN : COLOR_SHADOW));
                fill_rect(pixels, stride, 16, top + 10, 6, 1,
                          COLOR_DARK);
            }
            draw_text(pixels, stride, 30, top + 7, entry->label,
                      text_color, 1U, 24U);
            if (entry->is_directory) {
                draw_text(pixels, stride, 264, top + 7, "DIR",
                          selected ? COLOR_WHITE : COLOR_DARK, 1U, 3U);
            } else {
                draw_u32(pixels, stride, 258, top + 7,
                         entry->size_kib, text_color);
            }
        }
    } else {
        draw_file_listing_status(shell, pixels, stride);
    }

    draw_text(pixels, stride, 8, 153, "VISIBLE", COLOR_DARK, 1U, 7U);
    draw_u32(pixels, stride, 56, 153,
             shell->files.total_visible_entries, COLOR_BLACK);
    draw_text(pixels, stride, 104, 153, "HIDDEN", COLOR_DARK, 1U, 6U);
    draw_u32(pixels, stride, 146, 153,
             shell->files.hidden_entries, COLOR_BLACK);
    draw_text(pixels, stride, 190, 153, "GEN", COLOR_DARK, 1U, 3U);
    draw_u32(pixels, stride, 214, 153,
             shell->files.storage_generation, COLOR_BLACK);

    const char *notice = games
        ? "NATIVE CODE - TRUST PACKAGES"
        : "USB ADDS / FILE MANAGER REMOVES";
    uint16_t notice_color = COLOR_DARK;
    if (shell->file_delete_confirm && file_selected_is_actionable(shell)) {
        notice = file_selected_is_installable(shell)
            ? "CONFIRM INSTALL NEW CONSOLE OS"
            : (games ? "CONFIRM REMOVE GAME PACKAGE"
                     : "CONFIRM DELETE SELECTED FILE");
        notice_color = COLOR_RED;
    } else if (shell->file_notice == CONSOLE_FILE_NOTICE_REFRESHED) {
        notice = games ? "GAMES + UPDATES CHECKED" : "FILE LIST REFRESHED";
        notice_color = COLOR_GREEN;
    } else if (shell->file_notice == CONSOLE_FILE_NOTICE_DELETED) {
        notice = games ? "GAME PACKAGE REMOVED" : "FILE DELETED";
        notice_color = COLOR_GREEN;
    } else if (shell->file_notice == CONSOLE_FILE_NOTICE_ERROR) {
        notice = "OPERATION FAILED / CHECK USB";
        notice_color = COLOR_RED;
    } else if (shell->file_notice == CONSOLE_FILE_NOTICE_UPDATING) {
        notice = "WRITING INACTIVE OS SLOT...";
        notice_color = COLOR_YELLOW;
    } else if (shell->files.omitted_entries > 0U) {
        notice = "MORE FILES NOT SHOWN";
        notice_color = COLOR_YELLOW;
    }
    draw_text(pixels, stride, 8, 163, notice, notice_color, 1U, 31U);

    if (shell->file_delete_confirm) {
        draw_file_button(shell, pixels, stride,
                         FILE_PREV_LEFT, FILE_PREV_WIDTH,
                         FILE_PREV_CONTROL, "PREV", false);
        draw_file_button(shell, pixels, stride,
                         FILE_NEXT_LEFT, FILE_NEXT_WIDTH,
                         FILE_NEXT_CONTROL, "NEXT", false);
        draw_file_button(shell, pixels, stride,
                         FILE_REFRESH_LEFT, FILE_REFRESH_WIDTH,
                         FILE_CANCEL_CONTROL, "CANCEL", true);
        draw_file_button(shell, pixels, stride,
                         FILE_DELETE_LEFT, FILE_DELETE_WIDTH,
                         FILE_CONFIRM_CONTROL,
                         file_selected_is_installable(shell)
                            ? "INSTALL" : (games ? "REMOVE" : "DELETE"),
                         true);
    } else {
        draw_file_button(shell, pixels, stride,
                         FILE_PREV_LEFT, FILE_PREV_WIDTH,
                         FILE_PREV_CONTROL, "PREV",
                         file_can_page_previous(shell));
        draw_file_button(shell, pixels, stride,
                         FILE_NEXT_LEFT, FILE_NEXT_WIDTH,
                         FILE_NEXT_CONTROL, "NEXT",
                         file_can_page_next(shell));
        draw_file_button(shell, pixels, stride,
                         FILE_REFRESH_LEFT, FILE_REFRESH_WIDTH,
                         FILE_REFRESH_CONTROL,
                         games ? "CHECK" : "REFRESH", true);
        draw_file_button(shell, pixels, stride,
                         FILE_DELETE_LEFT, FILE_DELETE_WIDTH,
                         FILE_DELETE_CONTROL,
                         file_selected_is_installable(shell)
                            ? "INSTALL" : (games ? "REMOVE" : "DELETE"),
                         file_selected_is_actionable(shell));
    }
}

static void draw_audio(const console_shell_t *shell,
                       uint16_t *pixels, size_t stride)
{
    draw_text(pixels, stride, 12, 40, "GAME AUDIO SERVICE",
              COLOR_WHITE, 1U, 18U);
    draw_text(pixels, stride, 12, 56,
              shell->runtime.audio_handoff_ready ? "READY" : "BLOCKED",
              shell->runtime.audio_handoff_ready ? COLOR_GREEN : COLOR_RED,
              2U, 7U);
    draw_text(pixels, stride, 12, 78, "PCM16 STEREO / 16000 HZ",
              COLOR_MUTED, 1U, 23U);
    draw_text(pixels, stride, 12, 94, "TONE MIXER / 8 VOICES",
              COLOR_MUTED, 1U, 21U);
    draw_text(pixels, stride, 12, 110, "NATIVE GAMES / HOST OWNED",
              COLOR_MUTED, 1U, 25U);
    draw_text(pixels, stride, 12, 126, "VOLUME 6 / 10",
              COLOR_YELLOW, 1U, 13U);
    draw_text(pixels, stride, 12, 142, "FACTORY I2S1 GPIO30 PATH",
              COLOR_MUTED, 1U, 25U);
    draw_text(pixels, stride, 12, 166, "HOME AMP OFF / GAME OWNED",
              COLOR_CYAN, 1U, 25U);
    draw_text(pixels, stride, 12, 188, "DOOM SFX + MUS ON HANDOFF",
              COLOR_MUTED, 1U, 26U);
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
        case CONSOLE_PAGE_FILES:
            draw_files(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_GAMES:
            draw_files(shell, pixels, stride_pixels);
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
