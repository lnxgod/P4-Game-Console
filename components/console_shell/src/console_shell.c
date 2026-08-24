// SPDX-License-Identifier: MIT

#include "console/shell.h"

#include <limits.h>
#include <stdio.h>
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
    COLOR_MODE_CONTROL_BASE,
    COLOR_MODE_CONTROL_LIMIT =
        COLOR_MODE_CONTROL_BASE + CONSOLE_COLOR_MODE_COUNT,
    HOME_ITEM_CONTROL_BASE = COLOR_MODE_CONTROL_LIMIT,
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
    COLOR_MODE_LEFT = 12,
    COLOR_MODE_TOP = 51,
    COLOR_MODE_WIDTH = 142,
    COLOR_MODE_HEIGHT = 44,
    COLOR_MODE_COLUMN_GAP = 12,
    COLOR_MODE_ROW_GAP = 11,
    SYSTEM_USB_LEFT = 8,
    SYSTEM_USB_TOP = 174,
    SYSTEM_USB_WIDTH = 304,
    SYSTEM_USB_HEIGHT = 20,
    STORAGE_BUTTON_TOP = 174,
    STORAGE_BUTTON_HEIGHT = 20,
    STORAGE_CHECK_LEFT = 8,
    STORAGE_CHECK_WIDTH = 92,
    STORAGE_RETRY_LEFT = 104,
    STORAGE_RETRY_WIDTH = 92,
    STORAGE_REPAIR_LEFT = 200,
    STORAGE_REPAIR_WIDTH = 112,
    AUDIO_MINUS_LEFT = 174,
    AUDIO_PLUS_LEFT = 274,
    AUDIO_BUTTON_WIDTH = 38,
    AUDIO_BUTTON_HEIGHT = 24,
    AUDIO_BOOT_TOP = 61,
    AUDIO_GAME_TOP = 112,
    MULTIPLAYER_OPTION_LEFT = 8,
    MULTIPLAYER_OPTION_TOP = 57,
    MULTIPLAYER_OPTION_WIDTH = 304,
    MULTIPLAYER_OPTION_HEIGHT = 13,
    MULTIPLAYER_OPTION_PITCH = 15,
    MULTIPLAYER_LAUNCH_LEFT = 8,
    MULTIPLAYER_LAUNCH_TOP = 166,
    MULTIPLAYER_LAUNCH_WIDTH = 304,
    MULTIPLAYER_LAUNCH_HEIGHT = 21,
};

typedef struct {
    uint16_t black;
    uint16_t panel;
    uint16_t panel_pressed;
    uint16_t white;
    uint16_t muted;
    uint16_t cyan;
    uint16_t green;
    uint16_t yellow;
    uint16_t red;
    uint16_t desktop;
    uint16_t face;
    uint16_t light;
    uint16_t shadow;
    uint16_t dark;
    uint16_t title;
    uint16_t group;
} console_palette_t;

static const console_palette_t s_color_palettes[CONSOLE_COLOR_MODE_COUNT] = {
    [CONSOLE_COLOR_MODE_GAMECHANGERS] = {
        .black = UINT16_C(0x0000), .panel = UINT16_C(0x1082),
        .panel_pressed = UINT16_C(0x2945), .white = UINT16_C(0xffff),
        .muted = UINT16_C(0x9cf3), .cyan = UINT16_C(0xfd20),
        .green = UINT16_C(0x5fea), .yellow = UINT16_C(0xfd20),
        .red = UINT16_C(0xf904), .desktop = UINT16_C(0x0841),
        .face = UINT16_C(0xbdf7), .light = UINT16_C(0xffff),
        .shadow = UINT16_C(0x632c), .dark = UINT16_C(0x0000),
        .title = UINT16_C(0x0000), .group = UINT16_C(0x9cf3),
    },
    [CONSOLE_COLOR_MODE_ARCADE] = {
        .black = UINT16_C(0x0000), .panel = UINT16_C(0x10a2),
        .panel_pressed = UINT16_C(0x2945), .white = UINT16_C(0xffff),
        .muted = UINT16_C(0x9cf3), .cyan = UINT16_C(0x5fff),
        .green = UINT16_C(0x5fea), .yellow = UINT16_C(0xffe0),
        .red = UINT16_C(0xf904), .desktop = UINT16_C(0x0410),
        .face = UINT16_C(0xc618), .light = UINT16_C(0xffff),
        .shadow = UINT16_C(0x8410), .dark = UINT16_C(0x4208),
        .title = UINT16_C(0x0010), .group = UINT16_C(0xe71c),
    },
    [CONSOLE_COLOR_MODE_OCEAN] = {
        .black = UINT16_C(0x0006), .panel = UINT16_C(0x0821),
        .panel_pressed = UINT16_C(0x10a3), .white = UINT16_C(0xefff),
        .muted = UINT16_C(0x7df7), .cyan = UINT16_C(0x07ff),
        .green = UINT16_C(0x07e0), .yellow = UINT16_C(0xffe0),
        .red = UINT16_C(0xf81f), .desktop = UINT16_C(0x0010),
        .face = UINT16_C(0x18c7), .light = UINT16_C(0xafff),
        .shadow = UINT16_C(0x0842), .dark = UINT16_C(0x0008),
        .title = UINT16_C(0x001f), .group = UINT16_C(0x0844),
    },
    [CONSOLE_COLOR_MODE_SUNSET] = {
        .black = UINT16_C(0x1004), .panel = UINT16_C(0x39a7),
        .panel_pressed = UINT16_C(0x59e9), .white = UINT16_C(0xffff),
        .muted = UINT16_C(0xd57b), .cyan = UINT16_C(0xf81f),
        .green = UINT16_C(0xafe0), .yellow = UINT16_C(0xfd20),
        .red = UINT16_C(0xf8b4), .desktop = UINT16_C(0x180c),
        .face = UINT16_C(0x7a4f), .light = UINT16_C(0xffff),
        .shadow = UINT16_C(0x390e), .dark = UINT16_C(0x1004),
        .title = UINT16_C(0x780f), .group = UINT16_C(0x5a2f),
    },
};

static const char *const s_color_mode_names[CONSOLE_COLOR_MODE_COUNT] = {
#if CONSOLE_SHELL_NATIVE_BBS
    [CONSOLE_COLOR_MODE_GAMECHANGERS] = "BBS",
    [CONSOLE_COLOR_MODE_ARCADE] = "WINDOWS 3.1",
#else
    [CONSOLE_COLOR_MODE_GAMECHANGERS] = "GOLD",
    [CONSOLE_COLOR_MODE_ARCADE] = "ARCADE",
#endif
    [CONSOLE_COLOR_MODE_OCEAN] = "OCEAN",
    [CONSOLE_COLOR_MODE_SUNSET] = "SUNSET",
};

static const console_palette_t *s_palette =
    &s_color_palettes[CONSOLE_COLOR_MODE_GAMECHANGERS];

#define COLOR_BLACK (s_palette->black)
#define COLOR_PANEL (s_palette->panel)
#define COLOR_PANEL_PRESSED (s_palette->panel_pressed)
#define COLOR_WHITE (s_palette->white)
#define COLOR_MUTED (s_palette->muted)
#define COLOR_CYAN (s_palette->cyan)
#define COLOR_GREEN (s_palette->green)
#define COLOR_YELLOW (s_palette->yellow)
#define COLOR_RED (s_palette->red)
#define COLOR_DESKTOP (s_palette->desktop)
#define COLOR_FACE (s_palette->face)
#define COLOR_LIGHT (s_palette->light)
#define COLOR_SHADOW (s_palette->shadow)
#define COLOR_DARK (s_palette->dark)
#define COLOR_TITLE (s_palette->title)
#define COLOR_GROUP (s_palette->group)

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
    BBS_PAGE_PREVIOUS_CONTROL =
        HOME_ITEM_CONTROL_BASE + HOME_ITEM_CAPACITY,
    BBS_PAGE_NEXT_CONTROL,
    FILE_ROW_CONTROL_BASE,
    FILE_PREV_CONTROL =
        FILE_ROW_CONTROL_BASE + CONSOLE_SHELL_FILE_VISIBLE_ROWS,
    FILE_NEXT_CONTROL,
    FILE_REFRESH_CONTROL,
    FILE_DELETE_CONTROL,
    FILE_CANCEL_CONTROL,
    FILE_CONFIRM_CONTROL,
    SYSTEM_USB_CONTROL,
    STORAGE_CHECK_CONTROL,
    STORAGE_RETRY_CONTROL,
    STORAGE_REPAIR_CONTROL,
    AUDIO_BOOT_MINUS_CONTROL,
    AUDIO_BOOT_PLUS_CONTROL,
    AUDIO_GAME_MINUS_CONTROL,
    AUDIO_GAME_PLUS_CONTROL,
    MULTIPLAYER_OPTION_MINUS_CONTROL_BASE,
    MULTIPLAYER_OPTION_PLUS_CONTROL_BASE =
        MULTIPLAYER_OPTION_MINUS_CONTROL_BASE +
            CONSOLE_MULTIPLAYER_OPTION_COUNT,
    MULTIPLAYER_LAUNCH_CONTROL =
        MULTIPLAYER_OPTION_PLUS_CONTROL_BASE +
            CONSOLE_MULTIPLAYER_OPTION_COUNT,
    TERMINAL_KEY_CONTROL_BASE,
    TERMINAL_LETTER_KEY_COUNT = 26,
    TERMINAL_SPACE_CONTROL =
        TERMINAL_KEY_CONTROL_BASE + TERMINAL_LETTER_KEY_COUNT,
    TERMINAL_BACKSPACE_CONTROL,
    TERMINAL_ENTER_CONTROL,
    TERMINAL_KEY_CONTROL_LIMIT,
};

static const char s_terminal_keys[] = "QWERTYUIOPASDFGHJKLZXCVBNM";

static console_shell_action_t no_action(void);
static console_shell_action_t page_changed(uint32_t app_id);

static bool use_bbs_launcher(const console_shell_t *shell)
{
#if CONSOLE_SHELL_NATIVE_BBS && \
    !defined(CONSOLE_SHELL_FORCE_WINDOWS)
    return shell != NULL &&
        shell->color_mode == CONSOLE_COLOR_MODE_GAMECHANGERS;
#else
    (void)shell;
    return false;
#endif
}

static size_t home_column_count(const console_shell_t *shell)
{
    return use_bbs_launcher(shell) ? 2U : CONSOLE_SHELL_APP_COLUMNS;
}

static size_t home_visible_row_count(const console_shell_t *shell)
{
    return use_bbs_launcher(shell) ? 3U : CONSOLE_SHELL_VISIBLE_APP_ROWS;
}

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
    if (length >= CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES) {
        return false;
    }
    /* The empty path is the launcher root, so apps can be first-class doors
     * without inventing a synthetic folder name. */
    if (length == 0U) {
        return true;
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
    return page >= CONSOLE_PAGE_EXTERNAL &&
        page <= CONSOLE_PAGE_STORAGE;
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
    shell->color_mode = CONSOLE_COLOR_MODE_GAMECHANGERS;
    shell->multiplayer_selected_row = CONSOLE_MULTIPLAYER_OPTION_COUNT;
    p4_achievement_catalog_init(&shell->achievements);
    p4_file_list_init(&shell->desktop_files);
    p4_save_catalog_init(&shell->saves, false);
    p4_terminal_init(&shell->terminal, false);
#if CONSOLE_SHELL_NATIVE_BBS
    p4_ansi_init(&shell->bbs_terminal);
#endif
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
    const uint32_t relative_x =
        (uint32_t)physical_x - CONSOLE_SHELL_VIEWPORT_LEFT;
    const uint32_t relative_y =
        (uint32_t)physical_y - CONSOLE_SHELL_VIEWPORT_TOP;
    if (relative_x >= CONSOLE_SHELL_VIEWPORT_WIDTH ||
        relative_y >= CONSOLE_SHELL_VIEWPORT_HEIGHT ||
        gui_x == NULL || gui_y == NULL) {
        return false;
    }
    *gui_x = (uint16_t)(
        (relative_x *
         CONSOLE_SHELL_LAYOUT_WIDTH) / CONSOLE_SHELL_VIEWPORT_WIDTH);
    *gui_y = (uint16_t)(
        (relative_y *
         CONSOLE_SHELL_LAYOUT_HEIGHT) / CONSOLE_SHELL_VIEWPORT_HEIGHT);
    return *gui_x < CONSOLE_SHELL_LAYOUT_WIDTH &&
           *gui_y < CONSOLE_SHELL_LAYOUT_HEIGHT;
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
    const size_t columns = home_column_count(shell);
    return (count + columns - 1U) / columns;
}

static size_t home_max_scroll_row(const console_shell_t *shell)
{
    const size_t rows = home_row_count(shell);
    const size_t visible_rows = home_visible_row_count(shell);
    return rows > visible_rows ? rows - visible_rows : 0U;
}

static size_t home_first_visible_index(const console_shell_t *shell)
{
    return shell->home_scroll_row * home_column_count(shell);
}

#if CONSOLE_SHELL_NATIVE_BBS
static void bbs_copy_text(char *destination, size_t capacity,
                          const char *source)
{
    if (destination == NULL || capacity == 0U) {
        return;
    }
    const size_t length = bounded_length(source, capacity - 1U);
    if (length > 0U) {
        memcpy(destination, source, length);
    }
    destination[length] = '\0';
}

static void build_bbs_launcher_model(
    const console_shell_t *shell,
    p4_bbs_launcher_model_t *model)
{
    static const uint8_t accents[P4_BBS_VISIBLE_DOORS] = {
        P4_ANSI_COLOR_BRIGHT_CYAN,
        P4_ANSI_COLOR_YELLOW,
        P4_ANSI_COLOR_BRIGHT_GREEN,
        P4_ANSI_COLOR_BRIGHT_MAGENTA,
        P4_ANSI_COLOR_BRIGHT_RED,
        P4_ANSI_COLOR_BRIGHT_BLUE,
    };
    memset(model, 0, sizeof(*model));
    bbs_copy_text(model->board_name, sizeof(model->board_name),
                  "GAME CHANGERS AI BBS");
    if (shell->home_all_programs) {
        bbs_copy_text(model->section, sizeof(model->section),
                      "[ ALL PROGRAMS ]");
    } else if (shell->home_folder_path[0] != '\0') {
        bbs_copy_text(model->section, sizeof(model->section),
                      shell->home_folder_path);
    } else {
        bbs_copy_text(model->section, sizeof(model->section),
                      "[ MAIN DOORS ]");
    }
    bbs_copy_text(model->connection, sizeof(model->connection),
                  shell->runtime.content_scan_complete
                      ? "ONLINE" : "SCANNING");
    model->local_board = true;
    model->can_go_up = shell->home_all_programs ||
        shell->home_folder_path[0] != '\0';
    model->page = (uint16_t)(shell->home_scroll_row + 1U);
    model->page_count = (uint16_t)(home_max_scroll_row(shell) + 1U);
    model->uploads = shell->runtime.valid_cart_count;

    home_item_t items[HOME_ITEM_CAPACITY];
    const size_t item_count = build_home_items(shell, items);
    const size_t first = home_first_visible_index(shell);
    const size_t last = first + P4_BBS_VISIBLE_DOORS < item_count
        ? first + P4_BBS_VISIBLE_DOORS : item_count;
    model->door_count = last > first ? last - first : 0U;
    for (size_t index = first; index < last; ++index) {
        const size_t visible = index - first;
        const home_item_t *const item = &items[index];
        const console_app_descriptor_t *const app =
            item->kind == HOME_ITEM_APP && item->app_index < shell->app_count
                ? &shell->apps[item->app_index] : NULL;
        p4_bbs_door_t *const door = &model->doors[visible];
        door->number = (uint16_t)(index + 1U);
        door->accent = accents[visible];
        door->enabled = item->enabled;
        door->menu = app == NULL;
        bbs_copy_text(door->title, sizeof(door->title),
                      app != NULL ? app->title : item->title);
        if (app != NULL) {
            bbs_copy_text(door->subtitle, sizeof(door->subtitle),
                          app->subtitle);
        } else {
            const int written = snprintf(
                door->subtitle, sizeof(door->subtitle), "%u DOORS",
                (unsigned)item->program_count);
            if (written <= 0 || (size_t)written >= sizeof(door->subtitle)) {
                bbs_copy_text(door->subtitle, sizeof(door->subtitle),
                              "DOOR DIRECTORY");
            }
        }
    }
    size_t selected = shell->selected_home_item;
    if (shell->press_active &&
        shell->pressed_index >= HOME_ITEM_CONTROL_BASE &&
        shell->pressed_index < HOME_ITEM_CONTROL_BASE + item_count) {
        selected = shell->pressed_index - HOME_ITEM_CONTROL_BASE;
    }
    model->selected_door = selected >= first && selected < last
        ? selected - first : 0U;
}
#endif

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

static bool file_selected_is_openable(const console_shell_t *shell)
{
    return shell != NULL && shell->page == CONSOLE_PAGE_FILES &&
        shell->files.available &&
        shell->file_selected_index < shell->files.entry_count &&
        shell->files.entries[shell->file_selected_index].is_directory;
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

static bool file_selected_has_primary_action(const console_shell_t *shell)
{
    return file_selected_is_openable(shell) ||
        file_selected_is_actionable(shell);
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

static bool color_mode_is_valid(console_color_mode_t mode)
{
    return mode >= CONSOLE_COLOR_MODE_GAMECHANGERS &&
        mode < CONSOLE_COLOR_MODE_COUNT;
}

static int color_mode_tile_left(size_t mode_index)
{
    return COLOR_MODE_LEFT + (int)(mode_index % 2U) *
        (COLOR_MODE_WIDTH + COLOR_MODE_COLUMN_GAP);
}

static int color_mode_tile_top(size_t mode_index)
{
    return COLOR_MODE_TOP + (int)(mode_index / 2U) *
        (COLOR_MODE_HEIGHT + COLOR_MODE_ROW_GAP);
}

static size_t terminal_control_at(uint16_t gui_x, uint16_t gui_y)
{
    for (size_t index = 0U; index < TERMINAL_LETTER_KEY_COUNT; ++index) {
        unsigned row = 0U;
        unsigned column = 0U;
        unsigned left = 8U;
        unsigned top = 111U;
        if (index >= 19U) {
            row = 2U;
            column = (unsigned)(index - 19U);
            left = 53U;
        } else if (index >= 10U) {
            row = 1U;
            column = (unsigned)(index - 10U);
            left = 23U;
        } else {
            column = (unsigned)index;
        }
        top += row * 21U;
        if (point_in_rect(gui_x, gui_y, left + column * 30U,
                          top, 28U, 18U)) {
            return TERMINAL_KEY_CONTROL_BASE + index;
        }
    }
    if (point_in_rect(gui_x, gui_y, 8U, 174U, 170U, 20U)) {
        return TERMINAL_SPACE_CONTROL;
    }
    if (point_in_rect(gui_x, gui_y, 182U, 174U, 60U, 20U)) {
        return TERMINAL_BACKSPACE_CONTROL;
    }
    if (point_in_rect(gui_x, gui_y, 246U, 174U, 66U, 20U)) {
        return TERMINAL_ENTER_CONTROL;
    }
    return SIZE_MAX;
}

static bool usb_mode_active(const console_shell_t *shell)
{
    return shell->runtime.usb_drive_active;
}

static bool system_usb_button_enabled(const console_shell_t *shell)
{
    if (!shell->runtime.usb_storage_supported) {
        return false;
    }
    if (usb_mode_active(shell)) {
        return !shell->runtime.game_storage_usb_attached ||
            shell->runtime.usb_storage_eject_safe;
    }
    return shell->runtime.game_storage_state == CONSOLE_STORAGE_READY ||
        shell->runtime.game_storage_state == CONSOLE_STORAGE_MISSING ||
        shell->runtime.game_storage_state == CONSOLE_STORAGE_INVALID ||
        shell->runtime.game_storage_state == CONSOLE_STORAGE_FORMAT_REQUIRED ||
        shell->runtime.game_storage_state == CONSOLE_STORAGE_FAULT;
}

static console_shell_action_t system_usb_action(
    const console_shell_t *shell)
{
    const console_shell_action_t action = {
        .type = usb_mode_active(shell)
            ? CONSOLE_ACTION_USB_MODE_DISABLE
            : CONSOLE_ACTION_USB_MODE_ENABLE,
        .app_id = shell->active_app_id,
        .file_source_index = UINT32_MAX,
    };
    return action;
}

static bool storage_check_enabled(const console_shell_t *shell)
{
    return shell->runtime.game_storage_operation ==
            CONSOLE_STORAGE_OPERATION_NONE &&
        shell->runtime.sd_card_storage &&
        (shell->runtime.game_storage_state == CONSOLE_STORAGE_READY ||
         shell->runtime.game_storage_state == CONSOLE_STORAGE_MISSING ||
         shell->runtime.game_storage_state == CONSOLE_STORAGE_INVALID);
}

static bool storage_retry_enabled(const console_shell_t *shell)
{
    return shell->runtime.game_storage_operation ==
            CONSOLE_STORAGE_OPERATION_NONE &&
        shell->runtime.sd_card_storage &&
        shell->runtime.game_storage_state == CONSOLE_STORAGE_FAULT;
}

static bool storage_repair_enabled(const console_shell_t *shell)
{
    return shell->runtime.game_storage_operation ==
            CONSOLE_STORAGE_OPERATION_NONE &&
        shell->runtime.game_storage_repair_supported &&
        (shell->runtime.game_storage_state == CONSOLE_STORAGE_READY ||
         shell->runtime.game_storage_state == CONSOLE_STORAGE_MISSING ||
         shell->runtime.game_storage_state == CONSOLE_STORAGE_INVALID ||
         shell->runtime.game_storage_state == CONSOLE_STORAGE_FAULT);
}

static bool storage_action_enabled(const console_shell_t *shell,
                                   size_t action)
{
    switch (action) {
    case 0U: return storage_check_enabled(shell);
    case 1U: return storage_retry_enabled(shell);
    case 2U: return storage_repair_enabled(shell);
    default: return false;
    }
}

static console_shell_action_t storage_action(console_shell_t *shell,
                                             size_t requested)
{
    if (!storage_action_enabled(shell, requested)) {
        return no_action();
    }
    shell->storage_selected_action = requested;
    shell->dirty = true;
    if (requested == 2U && !shell->storage_repair_confirm) {
        shell->storage_repair_confirm = true;
        return page_changed(shell->active_app_id);
    }
    shell->storage_repair_confirm = false;
    const console_shell_action_t action = {
        .type = requested == 0U
            ? CONSOLE_ACTION_STORAGE_CHECK
            : requested == 1U
                ? CONSOLE_ACTION_STORAGE_RETRY
                : CONSOLE_ACTION_STORAGE_REPAIR,
        .app_id = shell->active_app_id,
        .file_source_index = UINT32_MAX,
    };
    return action;
}

static void reset_storage_controls(console_shell_t *shell)
{
    shell->storage_repair_confirm = false;
    shell->storage_selected_action = 0U;
    while (shell->storage_selected_action < 3U &&
           !storage_action_enabled(
               shell, shell->storage_selected_action)) {
        ++shell->storage_selected_action;
    }
    if (shell->storage_selected_action >= 3U) {
        shell->storage_selected_action = 0U;
    }
}

static uint8_t bounded_volume(uint8_t volume)
{
    if (volume < 1U) {
        return 1U;
    }
    return volume > 10U ? 10U : volume;
}

static console_shell_action_t audio_volume_action(
    const console_shell_t *shell, bool boot, int delta)
{
    const uint8_t current = bounded_volume(boot
        ? shell->runtime.boot_volume_step
        : shell->runtime.game_volume_step);
    uint8_t requested = current;
    if (delta < 0 && current > 1U) {
        requested = (uint8_t)(current - 1U);
    } else if (delta > 0 && current < 10U) {
        requested = (uint8_t)(current + 1U);
    }
    if (requested == current) {
        return no_action();
    }
    const console_shell_action_t action = {
        .type = boot ? CONSOLE_ACTION_BOOT_VOLUME_SET
                     : CONSOLE_ACTION_GAME_VOLUME_SET,
        .app_id = shell->active_app_id,
        .file_source_index = UINT32_MAX,
        .volume_step = requested,
    };
    return action;
}

static console_shell_action_t multiplayer_config_action(
    console_shell_t *shell,
    console_multiplayer_option_t option,
    int delta)
{
    if (shell == NULL || option >= CONSOLE_MULTIPLAYER_OPTION_COUNT ||
        !shell->runtime.multiplayer_settings_editable ||
        shell->runtime.multiplayer_launch_syncing || delta == 0) {
        return no_action();
    }
    shell->multiplayer_selected_row = (size_t)option;
    shell->dirty = true;
    const console_shell_action_t action = {
        .type = CONSOLE_ACTION_MULTIPLAYER_CONFIGURE,
        .app_id = shell->active_app_id,
        .file_source_index = UINT32_MAX,
        .multiplayer_option = option,
        .multiplayer_delta = delta < 0 ? INT8_C(-1) : INT8_C(1),
    };
    return action;
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
        if (shell->page == CONSOLE_PAGE_COLORS) {
            for (size_t mode = 0U; mode < CONSOLE_COLOR_MODE_COUNT; ++mode) {
                if (point_in_rect(
                        gui_x, gui_y,
                        (unsigned)color_mode_tile_left(mode),
                        (unsigned)color_mode_tile_top(mode),
                        COLOR_MODE_WIDTH, COLOR_MODE_HEIGHT)) {
                    return COLOR_MODE_CONTROL_BASE + mode;
                }
            }
        }
        if ((shell->page == CONSOLE_PAGE_SYSTEM ||
             shell->page == CONSOLE_PAGE_USB_DRIVE) &&
            system_usb_button_enabled(shell) &&
            point_in_rect(gui_x, gui_y,
                          SYSTEM_USB_LEFT, SYSTEM_USB_TOP,
                          SYSTEM_USB_WIDTH, SYSTEM_USB_HEIGHT)) {
            return SYSTEM_USB_CONTROL;
        }
        if (shell->page == CONSOLE_PAGE_STORAGE) {
            if (storage_check_enabled(shell) &&
                point_in_rect(gui_x, gui_y, STORAGE_CHECK_LEFT,
                              STORAGE_BUTTON_TOP, STORAGE_CHECK_WIDTH,
                              STORAGE_BUTTON_HEIGHT)) {
                return STORAGE_CHECK_CONTROL;
            }
            if (storage_retry_enabled(shell) &&
                point_in_rect(gui_x, gui_y, STORAGE_RETRY_LEFT,
                              STORAGE_BUTTON_TOP, STORAGE_RETRY_WIDTH,
                              STORAGE_BUTTON_HEIGHT)) {
                return STORAGE_RETRY_CONTROL;
            }
            if (storage_repair_enabled(shell) &&
                point_in_rect(gui_x, gui_y, STORAGE_REPAIR_LEFT,
                              STORAGE_BUTTON_TOP, STORAGE_REPAIR_WIDTH,
                              STORAGE_BUTTON_HEIGHT)) {
                return STORAGE_REPAIR_CONTROL;
            }
            return SIZE_MAX;
        }
        if (shell->page == CONSOLE_PAGE_AUDIO) {
            if (point_in_rect(gui_x, gui_y, AUDIO_MINUS_LEFT,
                              AUDIO_BOOT_TOP, AUDIO_BUTTON_WIDTH,
                              AUDIO_BUTTON_HEIGHT)) {
                return AUDIO_BOOT_MINUS_CONTROL;
            }
            if (point_in_rect(gui_x, gui_y, AUDIO_PLUS_LEFT,
                              AUDIO_BOOT_TOP, AUDIO_BUTTON_WIDTH,
                              AUDIO_BUTTON_HEIGHT)) {
                return AUDIO_BOOT_PLUS_CONTROL;
            }
            if (point_in_rect(gui_x, gui_y, AUDIO_MINUS_LEFT,
                              AUDIO_GAME_TOP, AUDIO_BUTTON_WIDTH,
                              AUDIO_BUTTON_HEIGHT)) {
                return AUDIO_GAME_MINUS_CONTROL;
            }
            if (point_in_rect(gui_x, gui_y, AUDIO_PLUS_LEFT,
                              AUDIO_GAME_TOP, AUDIO_BUTTON_WIDTH,
                              AUDIO_BUTTON_HEIGHT)) {
                return AUDIO_GAME_PLUS_CONTROL;
            }
            return SIZE_MAX;
        }
        if (shell->page == CONSOLE_PAGE_MULTIPLAYER &&
            !shell->runtime.multiplayer_launch_syncing) {
            if (shell->runtime.multiplayer_settings_editable) {
                for (size_t option = 0U;
                     option < CONSOLE_MULTIPLAYER_OPTION_COUNT; ++option) {
                    if (!point_in_rect(
                            gui_x, gui_y, MULTIPLAYER_OPTION_LEFT,
                            MULTIPLAYER_OPTION_TOP +
                                (unsigned)option * MULTIPLAYER_OPTION_PITCH,
                            MULTIPLAYER_OPTION_WIDTH,
                            MULTIPLAYER_OPTION_HEIGHT)) {
                        continue;
                    }
                    return gui_x < CONSOLE_SHELL_LAYOUT_WIDTH / 2U
                        ? MULTIPLAYER_OPTION_MINUS_CONTROL_BASE + option
                        : MULTIPLAYER_OPTION_PLUS_CONTROL_BASE + option;
                }
            }
            if (shell->runtime.multiplayer_lobby_ready &&
                shell->runtime.doom_wad_ready &&
                point_in_rect(
                    gui_x, gui_y, MULTIPLAYER_LAUNCH_LEFT,
                    MULTIPLAYER_LAUNCH_TOP, MULTIPLAYER_LAUNCH_WIDTH,
                    MULTIPLAYER_LAUNCH_HEIGHT)) {
                return MULTIPLAYER_LAUNCH_CONTROL;
            }
            return SIZE_MAX;
        }
        if (shell->page == CONSOLE_PAGE_TERMINAL) {
            return terminal_control_at(gui_x, gui_y);
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
        if (file_selected_has_primary_action(shell) &&
            point_in_rect(gui_x, gui_y,
                          FILE_DELETE_LEFT, FILE_BUTTON_TOP,
                          FILE_DELETE_WIDTH, FILE_BUTTON_HEIGHT)) {
            return FILE_DELETE_CONTROL;
        }
        return SIZE_MAX;
    }

#if CONSOLE_SHELL_NATIVE_BBS
    if (use_bbs_launcher(shell)) {
        p4_bbs_launcher_model_t model;
        build_bbs_launcher_model(shell, &model);
        const uint16_t surface_x = (uint16_t)(
            (uint32_t)gui_x * P4_ANSI_SURFACE_WIDTH /
            CONSOLE_SHELL_LAYOUT_WIDTH);
        const uint16_t surface_y = (uint16_t)(
            (uint32_t)gui_y * P4_ANSI_SURFACE_HEIGHT /
            CONSOLE_SHELL_LAYOUT_HEIGHT);
        const p4_bbs_hit_t hit =
            p4_bbs_hit_test(&model, surface_x, surface_y);
        if (hit.kind == P4_BBS_HIT_BACK) {
            return FOLDER_UP_CONTROL;
        }
        if (hit.kind == P4_BBS_HIT_PAGE_PREVIOUS) {
            return BBS_PAGE_PREVIOUS_CONTROL;
        }
        if (hit.kind == P4_BBS_HIT_PAGE_NEXT) {
            return BBS_PAGE_NEXT_CONTROL;
        }
        if (hit.kind == P4_BBS_HIT_DOOR &&
            hit.door_index < model.door_count) {
            return HOME_ITEM_CONTROL_BASE +
                home_first_visible_index(shell) + hit.door_index;
        }
        return SIZE_MAX;
    }
#endif

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

static console_shell_action_t activate_home_selection(console_shell_t *shell)
{
    home_item_t items[HOME_ITEM_CAPACITY];
    const size_t item_count = build_home_items(shell, items);
    const size_t item_index = shell->selected_home_item;
    if (item_index >= item_count || !items[item_index].enabled) {
        return no_action();
    }
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
    shell->dirty = true;
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
    if (app->page == CONSOLE_PAGE_MULTIPLAYER) {
        shell->multiplayer_selected_row =
            CONSOLE_MULTIPLAYER_OPTION_COUNT;
    }
    if (app->page == CONSOLE_PAGE_STORAGE) {
        reset_storage_controls(shell);
    }
    if (app->page == CONSOLE_PAGE_FILES ||
        app->page == CONSOLE_PAGE_GAMES) {
        shell->file_delete_confirm = false;
        shell->file_notice = CONSOLE_FILE_NOTICE_NONE;
        normalize_file_selection(shell);
    }
    return page_changed(app->id);
}

static void select_home_direction(console_shell_t *shell, int row_delta,
                                  int column_delta)
{
    home_item_t items[HOME_ITEM_CAPACITY];
    const size_t count = build_home_items(shell, items);
    if (count == 0U) {
        return;
    }
    size_t current = shell->selected_home_item < count
        ? shell->selected_home_item : 0U;
    const size_t columns = home_column_count(shell);
    const size_t visible_rows = home_visible_row_count(shell);
    const size_t row = current / columns;
    const size_t column = current % columns;
    size_t target = current;
    if (row_delta < 0 && row > 0U) {
        target = current - columns;
    } else if (row_delta > 0 &&
               current + columns < count) {
        target = current + columns;
    } else if (column_delta < 0 && column > 0U) {
        target = current - 1U;
    } else if (column_delta > 0 &&
               column + 1U < columns &&
               current + 1U < count) {
        target = current + 1U;
    }
    if (target == current || !items[target].enabled) {
        return;
    }
    shell->selected_home_item = target;
    if (items[target].kind == HOME_ITEM_APP) {
        shell->selected_index = items[target].app_index;
    }
    const size_t target_row = target / columns;
    if (target_row < shell->home_scroll_row) {
        shell->home_scroll_row = target_row;
    } else if (target_row >= shell->home_scroll_row + visible_rows) {
        shell->home_scroll_row = target_row -
            (visible_rows - 1U);
    }
    shell->dirty = true;
}

static console_shell_action_t file_selected_action(console_shell_t *shell)
{
    if (file_selected_is_openable(shell)) {
        shell->file_delete_confirm = false;
        shell->dirty = true;
        const console_shell_action_t action = {
            .type = CONSOLE_ACTION_FILE_OPEN,
            .app_id = shell->active_app_id,
            .file_source_index = shell->files.entries[
                shell->file_selected_index].source_index,
        };
        return action;
    }
    if (!file_selected_is_actionable(shell)) {
        return no_action();
    }
    if (!shell->file_delete_confirm) {
        shell->file_delete_confirm = true;
        shell->file_notice = CONSOLE_FILE_NOTICE_NONE;
        shell->dirty = true;
        return page_changed(shell->active_app_id);
    }
    shell->file_delete_confirm = false;
    shell->dirty = true;
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

console_shell_action_t console_shell_handle_buttons(
    console_shell_t *shell, uint32_t held_buttons)
{
    if (shell == NULL) {
        return no_action();
    }
    const uint32_t held = held_buttons & CONSOLE_BUTTON_MASK;
    const uint32_t pressed = held & ~shell->previous_buttons;
    shell->previous_buttons = held;
    if (pressed == 0U) {
        return no_action();
    }

    if ((pressed & CONSOLE_BUTTON_BACK) != 0U) {
        if (shell->page == CONSOLE_PAGE_HOME) {
            if (shell->home_all_programs ||
                shell->home_folder_path[0] != '\0') {
                navigate_home_up(shell);
                return page_changed(0U);
            }
            return no_action();
        }
        if ((shell->page == CONSOLE_PAGE_FILES ||
             shell->page == CONSOLE_PAGE_GAMES) &&
            shell->file_delete_confirm) {
            shell->file_delete_confirm = false;
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if (shell->page == CONSOLE_PAGE_FILES &&
            shell->files.can_go_up) {
            const console_shell_action_t action = {
                .type = CONSOLE_ACTION_FILE_UP,
                .app_id = shell->active_app_id,
                .file_source_index = UINT32_MAX,
            };
            return action;
        }
        if (shell->page == CONSOLE_PAGE_STORAGE &&
            shell->storage_repair_confirm) {
            shell->storage_repair_confirm = false;
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        console_shell_show_home(shell);
        return page_changed(0U);
    }

    if (shell->page == CONSOLE_PAGE_HOME) {
        if ((pressed & CONSOLE_BUTTON_UP) != 0U) {
            select_home_direction(shell, -1, 0);
        } else if ((pressed & CONSOLE_BUTTON_DOWN) != 0U) {
            select_home_direction(shell, 1, 0);
        } else if ((pressed & CONSOLE_BUTTON_LEFT) != 0U) {
            select_home_direction(shell, 0, -1);
        } else if ((pressed & CONSOLE_BUTTON_RIGHT) != 0U) {
            select_home_direction(shell, 0, 1);
        } else if ((pressed & CONSOLE_BUTTON_ACCEPT) != 0U) {
            return activate_home_selection(shell);
        }
        return no_action();
    }

    if (shell->page == CONSOLE_PAGE_COLORS &&
        (pressed & (CONSOLE_BUTTON_LEFT | CONSOLE_BUTTON_RIGHT |
                    CONSOLE_BUTTON_UP | CONSOLE_BUTTON_DOWN |
                    CONSOLE_BUTTON_ACCEPT)) != 0U) {
        console_color_mode_t requested = shell->color_mode;
        if ((pressed & (CONSOLE_BUTTON_LEFT | CONSOLE_BUTTON_UP)) != 0U) {
            requested = requested == CONSOLE_COLOR_MODE_GAMECHANGERS
                ? (console_color_mode_t)(CONSOLE_COLOR_MODE_COUNT - 1)
                : (console_color_mode_t)(requested - 1);
        } else {
            requested = (console_color_mode_t)(
                ((unsigned)requested + 1U) % CONSOLE_COLOR_MODE_COUNT);
        }
        shell->color_mode = requested;
        const size_t maximum_scroll = home_max_scroll_row(shell);
        if (shell->home_scroll_row > maximum_scroll) {
            shell->home_scroll_row = maximum_scroll;
        }
        shell->dirty = true;
        const console_shell_action_t action = {
            .type = CONSOLE_ACTION_COLOR_MODE_CHANGED,
            .app_id = shell->active_app_id,
            .file_source_index = UINT32_MAX,
            .color_mode = requested,
        };
        return action;
    }

    if ((shell->page == CONSOLE_PAGE_SYSTEM ||
         shell->page == CONSOLE_PAGE_USB_DRIVE) &&
        (pressed & CONSOLE_BUTTON_ACCEPT) != 0U &&
        system_usb_button_enabled(shell)) {
        return system_usb_action(shell);
    }

    if (shell->page == CONSOLE_PAGE_STORAGE) {
        if ((pressed & CONSOLE_BUTTON_REFRESH) != 0U) {
            shell->storage_repair_confirm = false;
            return storage_action(shell, 0U);
        }
        if ((pressed & CONSOLE_BUTTON_LEFT) != 0U) {
            for (size_t step = 0U; step < 3U; ++step) {
                shell->storage_selected_action =
                    (shell->storage_selected_action + 2U) % 3U;
                if (storage_action_enabled(
                        shell, shell->storage_selected_action)) {
                    break;
                }
            }
            shell->storage_repair_confirm = false;
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if ((pressed & CONSOLE_BUTTON_RIGHT) != 0U) {
            for (size_t step = 0U; step < 3U; ++step) {
                shell->storage_selected_action =
                    (shell->storage_selected_action + 1U) % 3U;
                if (storage_action_enabled(
                        shell, shell->storage_selected_action)) {
                    break;
                }
            }
            shell->storage_repair_confirm = false;
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if ((pressed & CONSOLE_BUTTON_ACCEPT) != 0U) {
            return storage_action(shell, shell->storage_selected_action);
        }
        return no_action();
    }

    if (shell->page == CONSOLE_PAGE_AUDIO) {
        if ((pressed & CONSOLE_BUTTON_UP) != 0U) {
            shell->audio_selected_row = 0U;
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if ((pressed & CONSOLE_BUTTON_DOWN) != 0U) {
            shell->audio_selected_row = 1U;
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if ((pressed & CONSOLE_BUTTON_LEFT) != 0U) {
            return audio_volume_action(
                shell, shell->audio_selected_row == 0U, -1);
        }
        if ((pressed & CONSOLE_BUTTON_RIGHT) != 0U) {
            return audio_volume_action(
                shell, shell->audio_selected_row == 0U, 1);
        }
        return no_action();
    }

    if (shell->page == CONSOLE_PAGE_MULTIPLAYER) {
        const size_t row_count =
            (size_t)CONSOLE_MULTIPLAYER_OPTION_COUNT + 1U;
        if ((pressed & CONSOLE_BUTTON_UP) != 0U) {
            shell->multiplayer_selected_row =
                (shell->multiplayer_selected_row + row_count - 1U) %
                    row_count;
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if ((pressed & CONSOLE_BUTTON_DOWN) != 0U) {
            shell->multiplayer_selected_row =
                (shell->multiplayer_selected_row + 1U) % row_count;
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if (shell->multiplayer_selected_row <
                CONSOLE_MULTIPLAYER_OPTION_COUNT &&
            (pressed & (CONSOLE_BUTTON_LEFT | CONSOLE_BUTTON_RIGHT |
                        CONSOLE_BUTTON_ACCEPT)) != 0U) {
            const int delta =
                (pressed & CONSOLE_BUTTON_LEFT) != 0U ? -1 : 1;
            return multiplayer_config_action(
                shell,
                (console_multiplayer_option_t)
                    shell->multiplayer_selected_row,
                delta);
        }
        if (shell->multiplayer_selected_row ==
                CONSOLE_MULTIPLAYER_OPTION_COUNT &&
            (pressed & CONSOLE_BUTTON_ACCEPT) != 0U &&
            shell->runtime.multiplayer_lobby_ready &&
            !shell->runtime.multiplayer_launch_syncing &&
            shell->runtime.doom_wad_ready) {
            const console_shell_action_t action = {
                .type = CONSOLE_ACTION_MULTIPLAYER_LAUNCH_DOOM,
                .app_id = shell->active_app_id,
                .file_source_index = UINT32_MAX,
            };
            return action;
        }
        return no_action();
    }

    if (shell->page != CONSOLE_PAGE_FILES &&
        shell->page != CONSOLE_PAGE_GAMES) {
        return no_action();
    }
    shell->file_notice = CONSOLE_FILE_NOTICE_NONE;
    if ((pressed & CONSOLE_BUTTON_REFRESH) != 0U) {
        shell->file_delete_confirm = false;
        shell->dirty = true;
        const console_shell_action_t action = {
            .type = shell->page == CONSOLE_PAGE_GAMES
                ? CONSOLE_ACTION_GAME_REFRESH
                : CONSOLE_ACTION_FILE_REFRESH,
            .app_id = shell->active_app_id,
            .file_source_index = UINT32_MAX,
        };
        return action;
    }
    if ((pressed & CONSOLE_BUTTON_UP) != 0U &&
        shell->file_selected_index > 0U) {
        --shell->file_selected_index;
        normalize_file_selection(shell);
        shell->file_delete_confirm = false;
        shell->dirty = true;
        return page_changed(shell->active_app_id);
    }
    if ((pressed & CONSOLE_BUTTON_DOWN) != 0U &&
        shell->file_selected_index + 1U < shell->files.entry_count) {
        ++shell->file_selected_index;
        normalize_file_selection(shell);
        shell->file_delete_confirm = false;
        shell->dirty = true;
        return page_changed(shell->active_app_id);
    }
    if ((pressed & CONSOLE_BUTTON_LEFT) != 0U &&
        file_can_page_previous(shell)) {
        const size_t step = CONSOLE_SHELL_FILE_VISIBLE_ROWS;
        shell->file_first_visible = step > shell->file_first_visible
            ? 0U : shell->file_first_visible - step;
        shell->file_selected_index = shell->file_first_visible;
        shell->file_delete_confirm = false;
        shell->dirty = true;
        return page_changed(shell->active_app_id);
    }
    if ((pressed & CONSOLE_BUTTON_RIGHT) != 0U &&
        file_can_page_next(shell)) {
        shell->file_first_visible += CONSOLE_SHELL_FILE_VISIBLE_ROWS;
        shell->file_selected_index = shell->file_first_visible;
        shell->file_delete_confirm = false;
        shell->dirty = true;
        return page_changed(shell->active_app_id);
    }
    if ((pressed & CONSOLE_BUTTON_ACCEPT) != 0U) {
        return file_selected_action(shell);
    }
    return no_action();
}

bool console_shell_handle_text_key(console_shell_t *shell, char key)
{
    if (shell == NULL || shell->page != CONSOLE_PAGE_TERMINAL) {
        return false;
    }
    bool changed = false;
    if (key == '\b' || key == 127) {
        changed = p4_terminal_backspace(&shell->terminal);
    } else if (key == '\n' || key == '\r') {
        const p4_terminal_command_t command =
            p4_terminal_submit(&shell->terminal);
        changed = command != P4_TERMINAL_COMMAND_NONE;
        char response[P4_TERMINAL_LINE_BYTES];
        switch (command) {
        case P4_TERMINAL_COMMAND_STATUS:
            (void)snprintf(response, sizeof(response),
                           "STORAGE %s  AUDIO %s",
                           shell->runtime.game_storage_state ==
                                   CONSOLE_STORAGE_READY
                               ? "READY" : "OFFLINE",
                           shell->runtime.audio_handoff_ready
                               ? "READY" : "OFFLINE");
            p4_terminal_write_line(&shell->terminal, response);
            break;
        case P4_TERMINAL_COMMAND_GAMES:
            (void)snprintf(response, sizeof(response),
                           "%u CARTS + %u BUILTINS",
                           (unsigned)shell->runtime.valid_cart_count,
                           (unsigned)shell->runtime.builtin_game_count);
            p4_terminal_write_line(&shell->terminal, response);
            break;
        case P4_TERMINAL_COMMAND_FILES:
            (void)snprintf(response, sizeof(response),
                           "%u FILES  SORT %s",
                           (unsigned)shell->desktop_files.count,
                           shell->desktop_files.sort == P4_FILE_SORT_SIZE
                               ? "SIZE"
                               : (shell->desktop_files.sort ==
                                      P4_FILE_SORT_TYPE
                                      ? "TYPE" : "NAME"));
            p4_terminal_write_line(&shell->terminal, response);
            break;
        case P4_TERMINAL_COMMAND_NONE:
        case P4_TERMINAL_COMMAND_HELP:
        case P4_TERMINAL_COMMAND_CLEAR:
        case P4_TERMINAL_COMMAND_SSH:
        case P4_TERMINAL_COMMAND_UNKNOWN:
        default:
            break;
        }
    } else {
        changed = p4_terminal_input_char(&shell->terminal, key);
    }
    if (changed) {
        shell->dirty = true;
    }
    return changed;
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
            if (shell->page == CONSOLE_PAGE_FILES &&
                shell->files.can_go_up) {
                const console_shell_action_t action = {
                    .type = CONSOLE_ACTION_FILE_UP,
                    .app_id = shell->active_app_id,
                    .file_source_index = UINT32_MAX,
                };
                return action;
            }
            if (shell->page == CONSOLE_PAGE_STORAGE &&
                shell->storage_repair_confirm) {
                shell->storage_repair_confirm = false;
                shell->dirty = true;
                return page_changed(shell->active_app_id);
            }
            console_shell_show_home(shell);
            return page_changed(0U);
        }
        if (released_control >= COLOR_MODE_CONTROL_BASE &&
            released_control < COLOR_MODE_CONTROL_LIMIT) {
            const console_color_mode_t requested =
                (console_color_mode_t)(released_control -
                                       COLOR_MODE_CONTROL_BASE);
            if (!color_mode_is_valid(requested) ||
                shell->color_mode == requested) {
                return no_action();
            }
            shell->color_mode = requested;
            const size_t maximum_scroll = home_max_scroll_row(shell);
            if (shell->home_scroll_row > maximum_scroll) {
                shell->home_scroll_row = maximum_scroll;
            }
            shell->dirty = true;
            const console_shell_action_t action = {
                .type = CONSOLE_ACTION_COLOR_MODE_CHANGED,
                .app_id = shell->active_app_id,
                .file_source_index = UINT32_MAX,
                .color_mode = requested,
            };
            return action;
        }
        if ((shell->page == CONSOLE_PAGE_SYSTEM ||
             shell->page == CONSOLE_PAGE_USB_DRIVE) &&
            released_control == SYSTEM_USB_CONTROL &&
            system_usb_button_enabled(shell)) {
            return system_usb_action(shell);
        }
        if (shell->page == CONSOLE_PAGE_STORAGE) {
            switch (released_control) {
            case STORAGE_CHECK_CONTROL:
                shell->storage_repair_confirm = false;
                return storage_action(shell, 0U);
            case STORAGE_RETRY_CONTROL:
                shell->storage_repair_confirm = false;
                return storage_action(shell, 1U);
            case STORAGE_REPAIR_CONTROL:
                return storage_action(shell, 2U);
            default:
                return no_action();
            }
        }
        if (shell->page == CONSOLE_PAGE_AUDIO) {
            switch (released_control) {
            case AUDIO_BOOT_MINUS_CONTROL:
                shell->audio_selected_row = 0U;
                return audio_volume_action(shell, true, -1);
            case AUDIO_BOOT_PLUS_CONTROL:
                shell->audio_selected_row = 0U;
                return audio_volume_action(shell, true, 1);
            case AUDIO_GAME_MINUS_CONTROL:
                shell->audio_selected_row = 1U;
                return audio_volume_action(shell, false, -1);
            case AUDIO_GAME_PLUS_CONTROL:
                shell->audio_selected_row = 1U;
                return audio_volume_action(shell, false, 1);
            default:
                return no_action();
            }
        }
        if (shell->page == CONSOLE_PAGE_MULTIPLAYER) {
            if (released_control >=
                    MULTIPLAYER_OPTION_MINUS_CONTROL_BASE &&
                released_control <
                    MULTIPLAYER_OPTION_PLUS_CONTROL_BASE) {
                return multiplayer_config_action(
                    shell,
                    (console_multiplayer_option_t)(released_control -
                        MULTIPLAYER_OPTION_MINUS_CONTROL_BASE),
                    -1);
            }
            if (released_control >=
                    MULTIPLAYER_OPTION_PLUS_CONTROL_BASE &&
                released_control < MULTIPLAYER_LAUNCH_CONTROL) {
                return multiplayer_config_action(
                    shell,
                    (console_multiplayer_option_t)(released_control -
                        MULTIPLAYER_OPTION_PLUS_CONTROL_BASE),
                    1);
            }
            if (released_control == MULTIPLAYER_LAUNCH_CONTROL &&
                shell->runtime.multiplayer_lobby_ready &&
                !shell->runtime.multiplayer_launch_syncing &&
                shell->runtime.doom_wad_ready) {
                shell->multiplayer_selected_row =
                    CONSOLE_MULTIPLAYER_OPTION_COUNT;
                const console_shell_action_t action = {
                    .type = CONSOLE_ACTION_MULTIPLAYER_LAUNCH_DOOM,
                    .app_id = shell->active_app_id,
                    .file_source_index = UINT32_MAX,
                };
                return action;
            }
            return no_action();
        }
        if (shell->page == CONSOLE_PAGE_TERMINAL &&
            released_control >= TERMINAL_KEY_CONTROL_BASE &&
            released_control < TERMINAL_KEY_CONTROL_LIMIT) {
            char key = '\0';
            if (released_control < TERMINAL_SPACE_CONTROL) {
                key = s_terminal_keys[
                    released_control - TERMINAL_KEY_CONTROL_BASE];
            } else if (released_control == TERMINAL_SPACE_CONTROL) {
                key = ' ';
            } else if (released_control == TERMINAL_BACKSPACE_CONTROL) {
                key = '\b';
            } else if (released_control == TERMINAL_ENTER_CONTROL) {
                key = '\n';
            }
            if (key != '\0' && console_shell_handle_text_key(shell, key)) {
                return page_changed(shell->active_app_id);
            }
            return no_action();
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
                file_selected_is_openable(shell)) {
                return file_selected_action(shell);
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
        if (use_bbs_launcher(shell) &&
            released_control == BBS_PAGE_PREVIOUS_CONTROL &&
            shell->home_scroll_row > 0U) {
            (void)set_home_scroll_row(
                shell, shell->home_scroll_row - 1U);
            return page_changed(0U);
        }
        if (use_bbs_launcher(shell) &&
            released_control == BBS_PAGE_NEXT_CONTROL &&
            shell->home_scroll_row < home_max_scroll_row(shell)) {
            (void)set_home_scroll_row(
                shell, shell->home_scroll_row + 1U);
            return page_changed(0U);
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
        if (app->page == CONSOLE_PAGE_MULTIPLAYER) {
            shell->multiplayer_selected_row =
                CONSOLE_MULTIPLAYER_OPTION_COUNT;
        }
        if (app->page == CONSOLE_PAGE_STORAGE) {
            reset_storage_controls(shell);
        }
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
        shell->runtime.board_kind != runtime->board_kind ||
        shell->runtime.touch_ready != runtime->touch_ready ||
        shell->runtime.controller_ready != runtime->controller_ready ||
        shell->runtime.keyboard_ready != runtime->keyboard_ready ||
        shell->runtime.mouse_ready != runtime->mouse_ready ||
        shell->runtime.sd_card_storage != runtime->sd_card_storage ||
        shell->runtime.audio_handoff_ready != runtime->audio_handoff_ready ||
        shell->runtime.game_storage_usb_attached !=
            runtime->game_storage_usb_attached ||
        shell->runtime.usb_storage_supported !=
            runtime->usb_storage_supported ||
        shell->runtime.usb_storage_eject_safe !=
            runtime->usb_storage_eject_safe ||
        shell->runtime.usb_drive_active != runtime->usb_drive_active ||
        shell->runtime.usb_input_host_active !=
            runtime->usb_input_host_active ||
        shell->runtime.doom_wad_ready != runtime->doom_wad_ready ||
        shell->runtime.content_scan_complete !=
            runtime->content_scan_complete ||
        shell->runtime.usb_content_ready != runtime->usb_content_ready ||
        shell->runtime.multiplayer_core_ready !=
            runtime->multiplayer_core_ready ||
        shell->runtime.multiplayer_transport_ready !=
            runtime->multiplayer_transport_ready ||
        shell->runtime.multiplayer_peer_seen !=
            runtime->multiplayer_peer_seen ||
        shell->runtime.multiplayer_lobby_ready !=
            runtime->multiplayer_lobby_ready ||
        shell->runtime.multiplayer_launch_syncing !=
            runtime->multiplayer_launch_syncing ||
        shell->runtime.multiplayer_settings_editable !=
            runtime->multiplayer_settings_editable ||
        shell->runtime.multiplayer_route_id !=
            runtime->multiplayer_route_id ||
        shell->runtime.multiplayer_player_slot !=
            runtime->multiplayer_player_slot ||
        shell->runtime.multiplayer_game_mode !=
            runtime->multiplayer_game_mode ||
        shell->runtime.multiplayer_episode !=
            runtime->multiplayer_episode ||
        shell->runtime.multiplayer_map != runtime->multiplayer_map ||
        shell->runtime.multiplayer_skill != runtime->multiplayer_skill ||
        shell->runtime.multiplayer_time_limit_minutes !=
            runtime->multiplayer_time_limit_minutes ||
        shell->runtime.multiplayer_no_monsters !=
            runtime->multiplayer_no_monsters ||
        shell->runtime.multiplayer_fast_monsters !=
            runtime->multiplayer_fast_monsters ||
        shell->runtime.multiplayer_respawn_monsters !=
            runtime->multiplayer_respawn_monsters ||
        shell->runtime.multiplayer_rx_frames !=
            runtime->multiplayer_rx_frames ||
        shell->runtime.multiplayer_tx_frames !=
            runtime->multiplayer_tx_frames ||
        shell->runtime.physical_keyboard_ready !=
            runtime->physical_keyboard_ready ||
        shell->runtime.valid_cart_count != runtime->valid_cart_count ||
        shell->runtime.builtin_game_count != runtime->builtin_game_count ||
        shell->runtime.boot_volume_step != runtime->boot_volume_step ||
        shell->runtime.game_volume_step != runtime->game_volume_step ||
        shell->runtime.audio_settings_persistent !=
            runtime->audio_settings_persistent ||
        shell->runtime.game_storage_free_kib !=
            runtime->game_storage_free_kib ||
        shell->runtime.game_storage_sector_bytes !=
            runtime->game_storage_sector_bytes ||
        shell->runtime.game_storage_frequency_khz !=
            runtime->game_storage_frequency_khz ||
        shell->runtime.game_storage_root_entries !=
            runtime->game_storage_root_entries ||
        shell->runtime.game_storage_mount_failures !=
            runtime->game_storage_mount_failures ||
        shell->runtime.game_storage_scans !=
            runtime->game_storage_scans ||
        shell->runtime.game_storage_checks !=
            runtime->game_storage_checks ||
        shell->runtime.game_storage_recovery_attempts !=
            runtime->game_storage_recovery_attempts ||
        shell->runtime.game_storage_repair_attempts !=
            runtime->game_storage_repair_attempts ||
        shell->runtime.game_storage_repair_sectors !=
            runtime->game_storage_repair_sectors ||
        shell->runtime.game_storage_card_ready !=
            runtime->game_storage_card_ready ||
        shell->runtime.game_storage_filesystem_ready !=
            runtime->game_storage_filesystem_ready ||
        shell->runtime.game_storage_repair_supported !=
            runtime->game_storage_repair_supported ||
        shell->runtime.game_storage_last_check_ok !=
            runtime->game_storage_last_check_ok ||
        shell->runtime.game_storage_repair_outcome !=
            runtime->game_storage_repair_outcome ||
        shell->runtime.game_storage_operation !=
            runtime->game_storage_operation;

    if (!changed) {
        return;
    }
    const bool storage_changed =
        shell->runtime.game_storage_kib != runtime->game_storage_kib ||
        shell->runtime.game_storage_state != runtime->game_storage_state ||
        shell->runtime.game_storage_usb_attached !=
            runtime->game_storage_usb_attached ||
        shell->runtime.usb_storage_supported !=
            runtime->usb_storage_supported ||
        shell->runtime.usb_storage_eject_safe !=
            runtime->usb_storage_eject_safe ||
        shell->runtime.usb_drive_active != runtime->usb_drive_active ||
        shell->runtime.usb_input_host_active !=
            runtime->usb_input_host_active ||
        shell->runtime.doom_wad_ready != runtime->doom_wad_ready;
    shell->runtime = *runtime;
    if (shell->page == CONSOLE_PAGE_STORAGE &&
        !storage_action_enabled(shell, shell->storage_selected_action)) {
        reset_storage_controls(shell);
    }
    if (shell->page == CONSOLE_PAGE_SYSTEM ||
        shell->page == CONSOLE_PAGE_STORAGE ||
        shell->page == CONSOLE_PAGE_USB_DRIVE ||
        shell->page == CONSOLE_PAGE_FILES ||
        shell->page == CONSOLE_PAGE_GAMES ||
        shell->page == CONSOLE_PAGE_AUDIO ||
        shell->page == CONSOLE_PAGE_MULTIPLAYER ||
        shell->page == CONSOLE_PAGE_TERMINAL ||
        (shell->page == CONSOLE_PAGE_HOME && storage_changed)) {
        shell->dirty = true;
    }
}

void console_shell_set_pointer(console_shell_t *shell,
                               bool visible,
                               uint16_t x,
                               uint16_t y,
                               bool pressed)
{
    if (shell == NULL) {
        return;
    }
    if (x >= CONSOLE_SHELL_LAYOUT_WIDTH) {
        x = CONSOLE_SHELL_LAYOUT_WIDTH - 1U;
    }
    if (y >= CONSOLE_SHELL_LAYOUT_HEIGHT) {
        y = CONSOLE_SHELL_LAYOUT_HEIGHT - 1U;
    }
    if (shell->pointer_visible == visible &&
        (!visible || (shell->pointer_x == x && shell->pointer_y == y &&
                      shell->pointer_pressed == pressed))) {
        return;
    }
    shell->pointer_visible = visible;
    shell->pointer_x = x;
    shell->pointer_y = y;
    shell->pointer_pressed = visible && pressed;
    shell->dirty = true;
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
    const size_t path_length = bounded_length(
        listing->path_label, CONSOLE_SHELL_FILE_PATH_LABEL_MAX_BYTES);
    if (path_length >= CONSOLE_SHELL_FILE_PATH_LABEL_MAX_BYTES) {
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

bool console_shell_set_file_list(
    console_shell_t *shell,
    const p4_file_list_t *files)
{
    if (shell == NULL || files == NULL ||
        files->count > P4_DESKTOP_MAX_FILES ||
        files->count > CONSOLE_SHELL_FILE_MAX_ENTRIES ||
        files->sort < P4_FILE_SORT_NAME || files->sort > P4_FILE_SORT_TYPE) {
        return false;
    }
    console_shell_file_listing_t listing = {
        .entry_count = files->count,
        .total_visible_entries = (uint32_t)files->count,
        .omitted_entries = files->truncated ? 1U : 0U,
        .revision = shell->files.revision + 1U,
        .available = true,
    };
    for (size_t index = 0U; index < files->count; ++index) {
        const p4_file_entry_t *const source = &files->entries[index];
        const size_t name_length = bounded_length(
            source->name, P4_DESKTOP_FILE_NAME_BYTES);
        if (name_length == 0U || name_length >= P4_DESKTOP_FILE_NAME_BYTES ||
            source->kind < P4_FILE_KIND_FOLDER ||
            source->kind > P4_FILE_KIND_OTHER) {
            return false;
        }
        console_shell_file_entry_t *const target = &listing.entries[index];
        target->source_index = (uint32_t)index;
        const size_t copied = name_length < sizeof(target->label) - 1U
            ? name_length : sizeof(target->label) - 1U;
        memcpy(target->label, source->name, copied);
        target->label[copied] = '\0';
        const uint64_t size_kib = source->size_bytes / UINT64_C(1024) +
            (source->size_bytes % UINT64_C(1024) != 0U ? 1U : 0U);
        target->size_kib = size_kib > UINT32_MAX
            ? UINT32_MAX : (uint32_t)size_kib;
        target->is_directory = source->kind == P4_FILE_KIND_FOLDER;
        target->removable = !target->is_directory && !source->read_only;
    }
    shell->desktop_files = *files;
    return console_shell_set_file_listing(shell, &listing);
}

bool console_shell_set_save_catalog(
    console_shell_t *shell,
    const p4_save_catalog_t *saves)
{
    if (shell == NULL || saves == NULL ||
        saves->count > P4_DESKTOP_MAX_SAVE_SLOTS) {
        return false;
    }
    for (size_t index = 0U; index < saves->count; ++index) {
        const p4_save_slot_t *const slot = &saves->slots[index];
        const size_t game_id_length = bounded_length(
            slot->game_id, P4_DESKTOP_SAVE_GAME_ID_BYTES);
        const size_t slot_name_length = bounded_length(
            slot->slot_name, P4_DESKTOP_SAVE_SLOT_NAME_BYTES);
        if (!slot->valid ||
            game_id_length == 0U ||
            game_id_length >= P4_DESKTOP_SAVE_GAME_ID_BYTES ||
            slot_name_length == 0U ||
            slot_name_length >= P4_DESKTOP_SAVE_SLOT_NAME_BYTES) {
            return false;
        }
    }
    shell->saves = *saves;
    if (shell->page == CONSOLE_PAGE_SAVES) {
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

void console_shell_set_achievement_catalog(
    console_shell_t *shell,
    const p4_achievement_catalog_t *achievements)
{
    if (shell == NULL || achievements == NULL ||
        achievements->count > P4_ACHIEVEMENT_MAX_ENTRIES) {
        return;
    }
    if (shell->achievements.count == achievements->count &&
        shell->achievements.unlock_events == achievements->unlock_events &&
        shell->achievements.rejected_events ==
            achievements->rejected_events) {
        return;
    }
    shell->achievements = *achievements;
    if (shell->page == CONSOLE_PAGE_ACHIEVEMENTS) {
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
    shell->storage_repair_confirm = false;
    shell->pressed_index = SIZE_MAX;
    shell->dirty = true;
}

console_color_mode_t console_shell_color_mode(const console_shell_t *shell)
{
    return shell != NULL && color_mode_is_valid(shell->color_mode)
        ? shell->color_mode : CONSOLE_COLOR_MODE_GAMECHANGERS;
}

bool console_shell_is_dirty(const console_shell_t *shell)
{
    return shell != NULL && shell->dirty;
}

static int layout_to_output_x(int x)
{
    return x * CONSOLE_SHELL_WIDTH / CONSOLE_SHELL_LAYOUT_WIDTH;
}

static int layout_to_output_y(int y)
{
    return y * CONSOLE_SHELL_HEIGHT / CONSOLE_SHELL_LAYOUT_HEIGHT;
}

static void fill_output_rect(uint16_t *pixels, size_t stride,
                             int left, int top, int right, int bottom,
                             uint16_t color)
{
    if (left < 0) {
        left = 0;
    }
    if (top < 0) {
        top = 0;
    }
    if (right > CONSOLE_SHELL_WIDTH) {
        right = CONSOLE_SHELL_WIDTH;
    }
    if (bottom > CONSOLE_SHELL_HEIGHT) {
        bottom = CONSOLE_SHELL_HEIGHT;
    }
    for (int row = top; row < bottom; ++row) {
        for (int column = left; column < right; ++column) {
            pixels[(size_t)row * stride + (size_t)column] = color;
        }
    }
}

static void put_pixel(uint16_t *pixels, size_t stride,
                      int x, int y, uint16_t color)
{
    if (x >= 0 && x < CONSOLE_SHELL_LAYOUT_WIDTH &&
        y >= 0 && y < CONSOLE_SHELL_LAYOUT_HEIGHT) {
        fill_output_rect(
            pixels, stride,
            layout_to_output_x(x), layout_to_output_y(y),
            layout_to_output_x(x + 1), layout_to_output_y(y + 1), color);
    }
}

static void draw_pointer(const console_shell_t *shell,
                         uint16_t *pixels, size_t stride)
{
    if (!shell->pointer_visible) {
        return;
    }
    const int left = shell->pointer_x;
    const int top = shell->pointer_y;
    const uint16_t fill = shell->pointer_pressed ? COLOR_CYAN : COLOR_WHITE;
    /* Compact classic desktop arrow with a dark one-pixel outline. */
    for (int row = 0; row < 11; ++row) {
        const int width = row < 8 ? row / 2 + 1 : 3;
        for (int column = 0; column < width; ++column) {
            const bool edge = column == 0 || column == width - 1 ||
                row == 0 || row == 10;
            put_pixel(pixels, stride, left + column, top + row,
                      edge ? COLOR_BLACK : fill);
        }
    }
    put_pixel(pixels, stride, left + 1, top + 1, fill);
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
    if (right > CONSOLE_SHELL_LAYOUT_WIDTH) {
        right = CONSOLE_SHELL_LAYOUT_WIDTH;
    }
    if (bottom > CONSOLE_SHELL_LAYOUT_HEIGHT) {
        bottom = CONSOLE_SHELL_LAYOUT_HEIGHT;
    }
    if (left >= right || top >= bottom) {
        return;
    }
    fill_output_rect(
        pixels, stride,
        layout_to_output_x(left), layout_to_output_y(top),
        layout_to_output_x(right), layout_to_output_y(bottom), color);
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
    fill_rect(pixels, stride, 0, 0, CONSOLE_SHELL_LAYOUT_WIDTH,
              CONSOLE_SHELL_LAYOUT_HEIGHT, COLOR_DESKTOP);
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

#if CONSOLE_SHELL_NATIVE_BBS
static bool draw_bbs_home(console_shell_t *shell,
                          uint16_t *pixels,
                          size_t stride)
{
    p4_bbs_launcher_model_t model;
    build_bbs_launcher_model(shell, &model);
    return p4_bbs_build_launcher(&shell->bbs_terminal, &model) &&
        p4_ansi_render_rgb565(
            &shell->bbs_terminal, pixels, stride,
            CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT);
}
#endif

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
              shell->page == CONSOLE_PAGE_FILES && shell->files.can_go_up
                  ? "< UP" : "< HOME",
              COLOR_WHITE, 1U, 6U);
    const console_app_descriptor_t *const app = active_app(shell);
    draw_text(pixels, stride, 68, 11,
              app != NULL ? app->title : "CONSOLE",
              app != NULL ? app->accent_rgb565 : COLOR_WHITE,
              1U, 15U);
    fill_rect(pixels, stride, 0, 31,
              CONSOLE_SHELL_LAYOUT_WIDTH, 1, COLOR_CYAN);
}

static void draw_colors(const console_shell_t *shell,
                        uint16_t *pixels, size_t stride)
{
#if CONSOLE_SHELL_NATIVE_BBS
    draw_text(pixels, stride, 12, 39, "PICK AN INTERFACE",
              COLOR_WHITE, 1U, 17U);
#else
    draw_text(pixels, stride, 12, 39, "PICK A COLOR MODE",
              COLOR_WHITE, 1U, 17U);
#endif
    for (size_t mode = 0U; mode < CONSOLE_COLOR_MODE_COUNT; ++mode) {
        const console_palette_t *const preview = &s_color_palettes[mode];
        const int left = color_mode_tile_left(mode);
        const int top = color_mode_tile_top(mode);
        const bool selected = shell->color_mode ==
            (console_color_mode_t)mode;
        const bool pressed = shell->press_active &&
            shell->pressed_index == COLOR_MODE_CONTROL_BASE + mode;
        fill_rect(pixels, stride, left, top,
                  COLOR_MODE_WIDTH, COLOR_MODE_HEIGHT, preview->desktop);
        outline_rect(pixels, stride, left, top,
                     COLOR_MODE_WIDTH, COLOR_MODE_HEIGHT,
                     selected ? COLOR_YELLOW : preview->cyan);
        fill_rect(pixels, stride, left + 3, top + 3,
                  COLOR_MODE_WIDTH - 6, 8, preview->title);
        if (pressed) {
            outline_rect(pixels, stride, left + 2, top + 2,
                         COLOR_MODE_WIDTH - 4, COLOR_MODE_HEIGHT - 4,
                         preview->white);
        }
        draw_centered_text(pixels, stride, left, top + 17,
                           COLOR_MODE_WIDTH, s_color_mode_names[mode],
                           preview->white, 10U);
        draw_centered_text(pixels, stride, left, top + 30,
                           COLOR_MODE_WIDTH,
                           selected ? "ACTIVE" : "TAP TO USE",
                           selected ? preview->yellow : preview->muted,
                           10U);
    }
    draw_centered_text(
        pixels, stride, 0, 177, CONSOLE_SHELL_LAYOUT_WIDTH,
#if CONSOLE_SHELL_NATIVE_BBS
        "BBS DEFAULT / WINDOWS FALLBACK",
#else
        "SESSION ONLY - NO CRT FILTER",
#endif
        COLOR_MUTED, 29U);
}

static void draw_touch(const console_shell_t *shell,
                       uint16_t *pixels, size_t stride)
{
    for (int x = 0; x < CONSOLE_SHELL_LAYOUT_WIDTH; x += 32) {
        fill_rect(pixels, stride, x, 32, 1, 168, UINT16_C(0x18E3));
    }
    for (int y = 40; y < CONSOLE_SHELL_LAYOUT_HEIGHT; y += 20) {
        fill_rect(pixels, stride, 0, y,
                  CONSOLE_SHELL_LAYOUT_WIDTH, 1,
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
    const bool gamepad_board =
        shell->runtime.board_kind == CONSOLE_BOARD_OLIMEX_P4_PC;
    const char *board_name = "ELECROW 10 IN VARIANT";
    const char *soc_name = "ESP32-P4 V1.3";
    if (shell->runtime.board_kind == CONSOLE_BOARD_OLIMEX_P4_PC) {
        board_name = "OLIMEX ESP32-P4-PC REV.B";
        soc_name = "ESP32-P4NRW32";
    } else if (shell->runtime.board_kind == CONSOLE_BOARD_WAVESHARE_4_3) {
        board_name = "WAVESHARE P4 LCD 4.3";
        soc_name = "ESP32-P4NRW32";
    } else if (shell->runtime.board_kind == CONSOLE_BOARD_HOST_PREVIEW) {
        board_name = "PC 768X480 PREVIEW";
        soc_name = "HOST SDL3";
    }
    draw_text(pixels, stride, 12, 37, "BOARD", COLOR_MUTED, 1U, 5U);
    draw_text(pixels, stride, 76, 37, board_name,
              COLOR_WHITE, 1U, 23U);
    draw_text(pixels, stride, 12, 53, "SOC", COLOR_MUTED, 1U, 3U);
    draw_text(pixels, stride, 112, 53, soc_name,
              COLOR_WHITE, 1U, 13U);
    draw_text(pixels, stride, 12, 69, "RTOS", COLOR_MUTED, 1U, 4U);
    draw_text(pixels, stride, 112, 69, "FREERTOS / IDF 5.5.3",
              COLOR_WHITE, 1U, 20U);
    draw_text(pixels, stride, 12, 85, "UPTIME SEC", COLOR_MUTED, 1U, 10U);
    draw_u32(pixels, stride, 112, 85, shell->runtime.uptime_seconds,
             COLOR_GREEN);
    draw_text(pixels, stride, 12, 101, "INTERNAL FREE KIB",
              COLOR_MUTED, 1U, 17U);
    draw_u32(pixels, stride, 142, 101, shell->runtime.internal_free_kib,
             COLOR_GREEN);
    draw_text(pixels, stride, 12, 117, "PSRAM FREE KIB",
              COLOR_MUTED, 1U, 14U);
    draw_u32(pixels, stride, 142, 117, shell->runtime.psram_free_kib,
             COLOR_GREEN);
    draw_text(pixels, stride, 12, 133,
              gamepad_board ? "USB INPUT" : "TOUCH",
              COLOR_MUTED, 1U, gamepad_board ? 9U : 5U);
    const bool input_ready = gamepad_board
        ? shell->runtime.controller_ready : shell->runtime.touch_ready;
    if (gamepad_board) {
        char status[] = "PAD- KBD- MOUSE-";
        status[3] = shell->runtime.controller_ready ? '+' : '-';
        status[8] = shell->runtime.keyboard_ready ? '+' : '-';
        status[15] = shell->runtime.mouse_ready ? '+' : '-';
        draw_text(pixels, stride, 112, 133, status,
                  (shell->runtime.controller_ready ||
                   shell->runtime.keyboard_ready ||
                   shell->runtime.mouse_ready) ? COLOR_GREEN : COLOR_YELLOW,
                  1U, 16U);
    } else {
        draw_text(pixels, stride, 112, 133,
                  input_ready ? "READY" : "OFFLINE",
                  input_ready ? COLOR_GREEN : COLOR_YELLOW, 1U,
                  input_ready ? 5U : 7U);
    }
    draw_text(pixels, stride, 12, 149, "GAME STORAGE",
              COLOR_MUTED, 1U, 12U);
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
        storage = shell->runtime.sd_card_storage
            ? "SD READY" : "WAD MISSING";
        storage_color = shell->runtime.sd_card_storage
            ? COLOR_GREEN : COLOR_YELLOW;
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
    draw_text(pixels, stride, 112, 149, storage,
              storage_color, 1U, 14U);
    draw_text(pixels, stride, 12, 165, "DOOM1.WAD", COLOR_MUTED, 1U, 9U);
    draw_text(pixels, stride, 112, 165,
              shell->runtime.doom_wad_ready ? "VERIFIED" : "NOT READY",
              shell->runtime.doom_wad_ready ? COLOR_GREEN : COLOR_YELLOW,
              1U, 9U);
    if (shell->runtime.usb_storage_supported) {
        const bool active = usb_mode_active(shell);
        const bool enabled = system_usb_button_enabled(shell);
        const bool pressed = enabled && shell->press_active &&
            shell->pressed_index == SYSTEM_USB_CONTROL;
        const char *label = "USB MODE UNAVAILABLE";
        if (active && shell->runtime.game_storage_usb_attached &&
            !shell->runtime.usb_storage_eject_safe) {
            label = "EJECT ON MAC FIRST";
        } else if (active) {
            label = "TURN OFF USB MODE";
        } else if (enabled) {
            label = "TURN ON USB MODE";
        }
        bevel_rect(pixels, stride, SYSTEM_USB_LEFT, SYSTEM_USB_TOP,
                   SYSTEM_USB_WIDTH, SYSTEM_USB_HEIGHT,
                   COLOR_FACE, pressed);
        draw_centered_text(pixels, stride, SYSTEM_USB_LEFT,
                           SYSTEM_USB_TOP + 7, SYSTEM_USB_WIDTH,
                           label, enabled ? COLOR_BLACK : COLOR_SHADOW,
                           23U);
    } else {
        draw_text(pixels, stride, 12, 184, "EJECT USB BEFORE GAME",
                  COLOR_CYAN, 1U, 21U);
        if (shell->runtime.sd_card_storage) {
            fill_rect(pixels, stride, 8, 181, 304, 15, COLOR_FACE);
            draw_text(pixels, stride, 12, 184, "POWER OFF TO REMOVE SD",
                      COLOR_CYAN, 1U, 22U);
        }
    }
}

static void draw_storage_button(const console_shell_t *shell,
                                uint16_t *pixels, size_t stride,
                                int left, int width, size_t control,
                                size_t action, const char *label,
                                bool enabled)
{
    const bool pressed = enabled && shell->press_active &&
        shell->pressed_index == control;
    bevel_rect(pixels, stride, left, STORAGE_BUTTON_TOP, width,
               STORAGE_BUTTON_HEIGHT, COLOR_FACE, pressed);
    if (enabled && shell->storage_selected_action == action) {
        outline_rect(pixels, stride, left + 2, STORAGE_BUTTON_TOP + 2,
                     width - 4, STORAGE_BUTTON_HEIGHT - 4, COLOR_YELLOW);
    }
    draw_centered_text(pixels, stride, left, STORAGE_BUTTON_TOP + 7,
                       width, label,
                       enabled ? COLOR_BLACK : COLOR_SHADOW, 16U);
}

static void draw_storage(const console_shell_t *shell,
                         uint16_t *pixels, size_t stride)
{
    const char *card = shell->runtime.game_storage_card_ready
        ? "ONLINE" : "OFFLINE";
    const uint16_t card_color = shell->runtime.game_storage_card_ready
        ? COLOR_GREEN : COLOR_RED;
    draw_text(pixels, stride, 12, 37, "CARD LINK", COLOR_MUTED, 1U, 9U);
    draw_text(pixels, stride, 132, 37, card, card_color, 1U, 8U);

    const char *filesystem = "NOT MOUNTED";
    uint16_t filesystem_color = COLOR_RED;
    if (shell->runtime.usb_drive_active) {
        filesystem = "OWNED BY USB";
        filesystem_color = COLOR_CYAN;
    } else if (shell->runtime.game_storage_filesystem_ready) {
        filesystem = "FAT READABLE";
        filesystem_color = COLOR_GREEN;
    }
    draw_text(pixels, stride, 12, 52, "FILESYSTEM", COLOR_MUTED, 1U, 10U);
    draw_text(pixels, stride, 132, 52, filesystem,
              filesystem_color, 1U, 12U);

    draw_text(pixels, stride, 12, 67, "CAPACITY KIB", COLOR_MUTED, 1U, 12U);
    draw_u32(pixels, stride, 132, 67,
             shell->runtime.game_storage_kib, COLOR_WHITE);
    draw_text(pixels, stride, 12, 82, "FREE KIB", COLOR_MUTED, 1U, 8U);
    draw_u32(pixels, stride, 132, 82,
             shell->runtime.game_storage_free_kib, COLOR_WHITE);
    draw_text(pixels, stride, 12, 97, "SD BUS KHZ", COLOR_MUTED, 1U, 10U);
    draw_u32(pixels, stride, 132, 97,
             shell->runtime.game_storage_frequency_khz, COLOR_WHITE);
    draw_text(pixels, stride, 12, 112, "SECTOR BYTES", COLOR_MUTED, 1U, 12U);
    draw_u32(pixels, stride, 132, 112,
             shell->runtime.game_storage_sector_bytes, COLOR_WHITE);
    draw_text(pixels, stride, 12, 127, "ROOT ENTRIES", COLOR_MUTED, 1U, 12U);
    draw_u32(pixels, stride, 132, 127,
             shell->runtime.game_storage_root_entries, COLOR_WHITE);

    if (shell->runtime.game_storage_operation !=
        CONSOLE_STORAGE_OPERATION_NONE) {
        const char *operation = "CHECKING CARD";
        if (shell->runtime.game_storage_operation ==
            CONSOLE_STORAGE_OPERATION_RETRY) {
            operation = "RETRYING CARD";
        } else if (shell->runtime.game_storage_operation ==
                   CONSOLE_STORAGE_OPERATION_REPAIR) {
            operation = "REPAIRING FAT";
        }
        draw_text(pixels, stride, 12, 142, operation,
                  COLOR_YELLOW, 1U, 24U);
        draw_text(pixels, stride, 12, 157,
                  shell->runtime.game_storage_operation ==
                          CONSOLE_STORAGE_OPERATION_REPAIR
                      ? "KEEP POWER ON - DO NOT REMOVE SD"
                      : "READ-ONLY DIAGNOSTIC RUNNING",
                  shell->runtime.game_storage_operation ==
                          CONSOLE_STORAGE_OPERATION_REPAIR
                      ? COLOR_RED : COLOR_CYAN,
                  1U, 31U);
    } else if (shell->storage_repair_confirm) {
        draw_text(pixels, stride, 12, 142,
                  "REPAIR WRITES FAT METADATA", COLOR_RED, 1U, 26U);
        draw_text(pixels, stride, 12, 157,
                  "KEEP POWER ON - PRESS AGAIN", COLOR_YELLOW, 1U, 27U);
    } else {
        const char *check = "NOT RUN";
        uint16_t check_color = COLOR_MUTED;
        if (shell->runtime.game_storage_checks != 0U) {
            check = shell->runtime.game_storage_last_check_ok
                ? "PASS" : "FAIL";
            check_color = shell->runtime.game_storage_last_check_ok
                ? COLOR_GREEN : COLOR_RED;
        }
        draw_text(pixels, stride, 12, 142, "LAST CHECK",
                  COLOR_MUTED, 1U, 10U);
        draw_text(pixels, stride, 132, 142, check,
                  check_color, 1U, 7U);
        draw_text(pixels, stride, 194, 142, "RUNS",
                  COLOR_MUTED, 1U, 4U);
        draw_u32(pixels, stride, 232, 142,
                 shell->runtime.game_storage_checks, COLOR_WHITE);

        const char *repair = "NOT RUN";
        uint16_t repair_color = COLOR_MUTED;
        switch (shell->runtime.game_storage_repair_outcome) {
        case CONSOLE_STORAGE_REPAIR_CLEAN:
            repair = "CLEAN";
            repair_color = COLOR_GREEN;
            break;
        case CONSOLE_STORAGE_REPAIR_REPAIRED:
            repair = "REPAIRED";
            repair_color = COLOR_GREEN;
            break;
        case CONSOLE_STORAGE_REPAIR_NEEDS_HOST:
            repair = "NEEDS FULL FSCK";
            repair_color = COLOR_YELLOW;
            break;
        case CONSOLE_STORAGE_REPAIR_UNSUPPORTED:
            repair = "UNSUPPORTED";
            repair_color = COLOR_YELLOW;
            break;
        case CONSOLE_STORAGE_REPAIR_FAILED:
            repair = "FAILED";
            repair_color = COLOR_RED;
            break;
        case CONSOLE_STORAGE_REPAIR_NOT_RUN:
        default:
            break;
        }
        draw_text(pixels, stride, 12, 157, "LAST REPAIR",
                  COLOR_MUTED, 1U, 11U);
        draw_text(pixels, stride, 132, 157, repair,
                  repair_color, 1U, 16U);
        draw_text(pixels, stride, 248, 157, "WROTE",
                  COLOR_MUTED, 1U, 5U);
        draw_u32(pixels, stride, 282, 157,
                 shell->runtime.game_storage_repair_sectors, COLOR_WHITE);
    }

    draw_storage_button(shell, pixels, stride,
                        STORAGE_CHECK_LEFT, STORAGE_CHECK_WIDTH,
                        STORAGE_CHECK_CONTROL, 0U, "CHECK CARD",
                        storage_check_enabled(shell));
    draw_storage_button(shell, pixels, stride,
                        STORAGE_RETRY_LEFT, STORAGE_RETRY_WIDTH,
                        STORAGE_RETRY_CONTROL, 1U, "RETRY CARD",
                        storage_retry_enabled(shell));
    draw_storage_button(shell, pixels, stride,
                        STORAGE_REPAIR_LEFT, STORAGE_REPAIR_WIDTH,
                        STORAGE_REPAIR_CONTROL, 2U,
                        shell->storage_repair_confirm
                            ? "CONFIRM REPAIR" : "REPAIR FAT",
                        storage_repair_enabled(shell));
}

static void draw_usb_drive(const console_shell_t *shell,
                           uint16_t *pixels, size_t stride)
{
    const bool active = usb_mode_active(shell);
    const bool enabled = system_usb_button_enabled(shell);
    const bool pressed = enabled && shell->press_active &&
        shell->pressed_index == SYSTEM_USB_CONTROL;

    draw_text(pixels, stride, 12, 38, "H2 USB-C ROLE",
              COLOR_MUTED, 1U, 13U);
    const char *role = "QUIESCED";
    uint16_t role_color = COLOR_YELLOW;
    if (active) {
        role = "THUMBDRIVE / MSC";
        role_color = COLOR_CYAN;
    } else if (shell->runtime.usb_input_host_active) {
        role = "CONTROLLER HOST";
        role_color = COLOR_GREEN;
    }
    draw_text(pixels, stride, 122, 38, role, role_color, 1U, 17U);

    draw_text(pixels, stride, 12, 57, "DEFAULT",
              COLOR_MUTED, 1U, 7U);
    draw_text(pixels, stride, 122, 57, "WIRED HID CONTROLLERS",
              COLOR_WHITE, 1U, 21U);
    draw_text(pixels, stride, 12, 76, "USB DRIVE",
              COLOR_MUTED, 1U, 9U);
    draw_text(pixels, stride, 122, 76,
              active ? "SD OWNED BY MAC" : "OFF UNTIL STARTED",
              active ? COLOR_CYAN : COLOR_WHITE, 1U, 18U);
    draw_text(pixels, stride, 12, 95, "POWER",
              COLOR_MUTED, 1U, 5U);
    draw_text(pixels, stride, 122, 95, "EXTERNAL POWERED HUB",
              COLOR_WHITE, 1U, 20U);
    draw_text(pixels, stride, 12, 114, "CABLE",
              COLOR_MUTED, 1U, 5U);
    draw_text(pixels, stride, 122, 114, "H2 DATA; NO FW VBUS",
              COLOR_WHITE, 1U, 19U);

    const char *host = "NOT ATTACHED";
    uint16_t host_color = COLOR_MUTED;
    if (active && shell->runtime.game_storage_usb_attached) {
        host = shell->runtime.usb_storage_eject_safe
            ? "EJECTED / SAFE" : "MAC MOUNTED";
        host_color = shell->runtime.usb_storage_eject_safe
            ? COLOR_GREEN : COLOR_YELLOW;
    }
    draw_text(pixels, stride, 12, 133, "HOST STATUS",
              COLOR_MUTED, 1U, 11U);
    draw_text(pixels, stride, 122, 133, host, host_color, 1U, 15U);

    draw_text(pixels, stride, 12, 153,
              active ? "EJECT OR UNPLUG BEFORE RETURNING"
                     : "STARTING THIS APP PAUSES ALL USB INPUT",
              active ? COLOR_YELLOW : COLOR_CYAN, 1U, 37U);

    const char *label = "ROLE SWITCH UNAVAILABLE";
    if (active && shell->runtime.game_storage_usb_attached &&
        !shell->runtime.usb_storage_eject_safe) {
        label = "EJECT ON MAC FIRST";
    } else if (active) {
        label = "RETURN TO CONTROLLERS";
    } else if (enabled) {
        label = "ENABLE USB THUMBDRIVE";
    }
    bevel_rect(pixels, stride, SYSTEM_USB_LEFT, SYSTEM_USB_TOP,
               SYSTEM_USB_WIDTH, SYSTEM_USB_HEIGHT,
               COLOR_FACE, pressed);
    draw_centered_text(pixels, stride, SYSTEM_USB_LEFT,
                       SYSTEM_USB_TOP + 7, SYSTEM_USB_WIDTH,
                       label, enabled ? COLOR_BLACK : COLOR_SHADOW, 24U);
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
                  shell->runtime.sd_card_storage
                    ? "POWER OFF BEFORE SD REMOVAL"
                    : "USB AND APP NEVER SHARE FAT",
                  COLOR_DARK, 1U, 27U);
        return;
    }
    if (shell->files.entry_count == 0U) {
        const bool games = shell->page == CONSOLE_PAGE_GAMES;
        draw_text(pixels, stride, 12, 83,
                  games ? "NO GAME PACKAGES" : "NO VISIBLE FILES",
                  COLOR_TITLE, 1U, 16U);
        draw_text(pixels, stride, 12, 101,
                  shell->runtime.sd_card_storage
                    ? (games ? "COPY .P4G TO MICROSD"
                             : "COPY FILES TO MICROSD")
                    : (games ? "COPY .P4G WITH J16 USB"
                             : "COPY FILES WITH J16 USB"),
                  COLOR_DARK, 1U, 24U);
    }
}

static void draw_files(console_shell_t *shell,
                       uint16_t *pixels, size_t stride)
{
    const bool games = shell->page == CONSOLE_PAGE_GAMES;
    fill_rect(pixels, stride, 0, 32, CONSOLE_SHELL_LAYOUT_WIDTH,
              CONSOLE_SHELL_LAYOUT_HEIGHT - 32, COLOR_FACE);
    draw_text(pixels, stride, 12, 37,
              games ? "GAME / UPDATE" :
                  (shell->files.path_label[0] != '\0'
                      ? shell->files.path_label : "SD:/"),
              COLOR_DARK, 1U, games ? 13U : 38U);
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
        ? "P4G RUNS / P4CART LUA PENDING"
        : (shell->runtime.usb_storage_supported
            ? "OPEN FOLDERS / USB MODE EDITS"
            : shell->runtime.sd_card_storage
            ? "SD ADDS / FILE MANAGER REMOVES"
            : "USB ADDS / FILE MANAGER REMOVES");
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
                         file_selected_is_openable(shell)
                            ? "OPEN" : file_selected_is_installable(shell)
                            ? "INSTALL" : (games ? "REMOVE" : "DELETE"),
                         file_selected_has_primary_action(shell));
    }
}

static void draw_audio(const console_shell_t *shell,
                       uint16_t *pixels, size_t stride)
{
    draw_text(pixels, stride, 12, 36, "AUDIO CONTROL PANEL",
              COLOR_WHITE, 1U, 19U);
    draw_text(pixels, stride, 218, 36,
              shell->runtime.audio_handoff_ready ? "READY" : "BLOCKED",
              shell->runtime.audio_handoff_ready ? COLOR_GREEN : COLOR_RED,
              1U, 7U);

    const uint8_t levels[2] = {
        bounded_volume(shell->runtime.boot_volume_step),
        bounded_volume(shell->runtime.game_volume_step),
    };
    const int tops[2] = {AUDIO_BOOT_TOP, AUDIO_GAME_TOP};
    const size_t minus_controls[2] = {
        AUDIO_BOOT_MINUS_CONTROL, AUDIO_GAME_MINUS_CONTROL,
    };
    const size_t plus_controls[2] = {
        AUDIO_BOOT_PLUS_CONTROL, AUDIO_GAME_PLUS_CONTROL,
    };
    const char *const labels[2] = {"BOOT SOUNDS", "GAME AUDIO"};
    for (size_t row = 0U; row < 2U; ++row) {
        const bool selected = shell->audio_selected_row == row;
        const uint16_t label_color = selected ? COLOR_YELLOW : COLOR_WHITE;
        draw_text(pixels, stride, 12, tops[row] + 2, labels[row],
                  label_color, 1U, 11U);
        for (unsigned segment = 0U; segment < 10U; ++segment) {
            fill_rect(pixels, stride, 94 + (int)(segment * 7U),
                      tops[row] + 5, 5, 11,
                      segment < levels[row] ? COLOR_GREEN : COLOR_SHADOW);
        }
        bevel_rect(pixels, stride, AUDIO_MINUS_LEFT, tops[row],
                   AUDIO_BUTTON_WIDTH, AUDIO_BUTTON_HEIGHT, COLOR_FACE,
                   shell->pressed_index == minus_controls[row]);
        draw_centered_text(pixels, stride, AUDIO_MINUS_LEFT,
                           tops[row] + 8, AUDIO_BUTTON_WIDTH, "-",
                           COLOR_BLACK, 1U);
        draw_u32(pixels, stride, 235, tops[row] + 8,
                 levels[row], COLOR_YELLOW);
        bevel_rect(pixels, stride, AUDIO_PLUS_LEFT, tops[row],
                   AUDIO_BUTTON_WIDTH, AUDIO_BUTTON_HEIGHT, COLOR_FACE,
                   shell->pressed_index == plus_controls[row]);
        draw_centered_text(pixels, stride, AUDIO_PLUS_LEFT,
                           tops[row] + 8, AUDIO_BUTTON_WIDTH, "+",
                           COLOR_BLACK, 1U);
    }

    draw_text(pixels, stride, 12, 92, "NEXT RESTART",
              COLOR_MUTED, 1U, 12U);
    draw_text(pixels, stride, 12, 143, "NEXT GAME LAUNCH",
              COLOR_MUTED, 1U, 16U);
    draw_text(pixels, stride, 12, 166,
              shell->runtime.audio_settings_persistent
                  ? "SAVES AUTOMATICALLY"
                  : "SETTINGS NOT PERSISTENT",
              shell->runtime.audio_settings_persistent
                  ? COLOR_CYAN : COLOR_RED,
              1U, 24U);
    draw_text(pixels, stride, 12, 184,
              "TOUCH +/- OR USE D-PAD",
              COLOR_MUTED, 1U, 24U);
}

static void draw_achievements(const console_shell_t *shell,
                              uint16_t *pixels, size_t stride)
{
    draw_text(pixels, stride, 12, 40, "SESSION UNLOCKS",
              COLOR_WHITE, 1U, 15U);
    draw_u32(pixels, stride, 118, 40,
             (uint32_t)shell->achievements.count, COLOR_YELLOW);
    draw_text(pixels, stride, 136, 40, "/ 32", COLOR_MUTED, 1U, 4U);
    if (shell->achievements.count == 0U) {
        draw_centered_text(pixels, stride, 36, 88, 248,
                           "NO BADGES YET", COLOR_BLACK, 13U);
        draw_centered_text(pixels, stride, 36, 106, 248,
                           "PLAY BYTE BUDDY", COLOR_CYAN, 15U);
    } else {
        const size_t visible = shell->achievements.count < 5U
            ? shell->achievements.count : 5U;
        for (size_t index = 0U; index < visible; ++index) {
            const p4_achievement_entry_t *const entry =
                p4_achievement_catalog_get(&shell->achievements, index);
            if (entry == NULL) {
                continue;
            }
            const int top = 59 + (int)index * 23;
            fill_rect(pixels, stride, 10, top, 300, 19, COLOR_PANEL);
            outline_rect(pixels, stride, 10, top, 300, 19, COLOR_CYAN);
            draw_text(pixels, stride, 17, top + 3, entry->title,
                      COLOR_YELLOW, 1U, 23U);
            draw_text(pixels, stride, 17, top + 11, entry->description,
                      COLOR_MUTED, 1U, 47U);
        }
    }
    draw_text(pixels, stride, 12, 181,
              "PERSISTENCE ARRIVES WITH SAVES", COLOR_MUTED, 1U, 32U);
}

static const char *multiplayer_mode_name(uint8_t mode)
{
    switch (mode) {
    case 0U: return "COOPERATIVE";
    case 1U: return "DEATHMATCH";
    case 2U: return "ALTDEATH";
    default: return "INVALID";
    }
}

static const char *multiplayer_skill_name(uint8_t skill)
{
    static const char *const names[] = {
        "INVALID", "1 BABY", "2 EASY", "3 NORMAL", "4 ULTRA", "5 NIGHTMARE",
    };
    return skill < sizeof(names) / sizeof(names[0])
        ? names[skill] : names[0];
}

static void draw_multiplayer_option(
    const console_shell_t *shell,
    uint16_t *pixels,
    size_t stride,
    console_multiplayer_option_t option,
    const char *label,
    const char *value)
{
    const int top = MULTIPLAYER_OPTION_TOP +
        (int)option * MULTIPLAYER_OPTION_PITCH;
    const bool selected = shell->multiplayer_selected_row == (size_t)option;
    const bool pressed = shell->press_active &&
        (shell->pressed_index ==
             MULTIPLAYER_OPTION_MINUS_CONTROL_BASE + (size_t)option ||
         shell->pressed_index ==
             MULTIPLAYER_OPTION_PLUS_CONTROL_BASE + (size_t)option);
    fill_rect(pixels, stride, MULTIPLAYER_OPTION_LEFT, top,
              MULTIPLAYER_OPTION_WIDTH, MULTIPLAYER_OPTION_HEIGHT,
              pressed ? COLOR_PANEL_PRESSED : COLOR_PANEL);
    outline_rect(pixels, stride, MULTIPLAYER_OPTION_LEFT, top,
                 MULTIPLAYER_OPTION_WIDTH, MULTIPLAYER_OPTION_HEIGHT,
                 selected ? COLOR_YELLOW : COLOR_GROUP);
    draw_text(pixels, stride, 12, top + 3, label,
              selected ? COLOR_WHITE : COLOR_MUTED, 1U, 12U);
    if (shell->runtime.multiplayer_settings_editable) {
        draw_text(pixels, stride, 103, top + 3, "<", COLOR_CYAN, 1U, 1U);
        draw_text(pixels, stride, 300, top + 3, ">", COLOR_CYAN, 1U, 1U);
    }
    draw_text(pixels, stride, 116, top + 3, value,
              shell->runtime.multiplayer_settings_editable
                  ? COLOR_GREEN : COLOR_MUTED,
              1U, 29U);
}

static void draw_multiplayer(const console_shell_t *shell,
                             uint16_t *pixels, size_t stride)
{
    char status[52];
    const char *const link = shell->runtime.multiplayer_route_id == 2U
        ? "WIRE"
        : shell->runtime.multiplayer_route_id == 1U ? "RELAY" : "AUTO";
    (void)snprintf(
        status, sizeof(status), "%s %s  PEER %s  RX %lu TX %lu",
        link,
        shell->runtime.multiplayer_transport_ready ? "READY" : "OFF",
        shell->runtime.multiplayer_peer_seen ? "LINK" : "WAIT",
        (unsigned long)shell->runtime.multiplayer_rx_frames,
        (unsigned long)shell->runtime.multiplayer_tx_frames);
    draw_text(pixels, stride, 8, 36, "DOOM MATCH SETUP",
              COLOR_WHITE, 1U, 20U);
    draw_text(pixels, stride, 8, 47, status,
              shell->runtime.multiplayer_transport_ready
                  ? COLOR_GREEN : COLOR_RED,
              1U, 51U);

    char map[12];
    char limit[12];
    (void)snprintf(map, sizeof(map), "E%uM%u",
                   (unsigned)shell->runtime.multiplayer_episode,
                   (unsigned)shell->runtime.multiplayer_map);
    if (shell->runtime.multiplayer_time_limit_minutes == 0U) {
        strcpy(limit, "OFF");
    } else {
        (void)snprintf(
            limit, sizeof(limit), "%u MIN",
            (unsigned)shell->runtime.multiplayer_time_limit_minutes);
    }
    draw_multiplayer_option(
        shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_MODE,
        "MODE", multiplayer_mode_name(shell->runtime.multiplayer_game_mode));
    draw_multiplayer_option(
        shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_MAP, "MAP", map);
    draw_multiplayer_option(
        shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_SKILL,
        "SKILL", multiplayer_skill_name(shell->runtime.multiplayer_skill));
    draw_multiplayer_option(
        shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_MONSTERS,
        "MONSTERS", shell->runtime.multiplayer_no_monsters ? "OFF" : "ON");
    draw_multiplayer_option(
        shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_FAST,
        "FAST", shell->runtime.multiplayer_fast_monsters ? "ON" : "OFF");
    draw_multiplayer_option(
        shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_RESPAWN,
        "RESPAWN", shell->runtime.multiplayer_respawn_monsters ? "ON" : "OFF");
    draw_multiplayer_option(
        shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_TIME_LIMIT,
        "LIMIT", limit);

    const bool launch_selected = shell->multiplayer_selected_row ==
        CONSOLE_MULTIPLAYER_OPTION_COUNT;
    const bool launch_pressed = shell->press_active &&
        shell->pressed_index == MULTIPLAYER_LAUNCH_CONTROL;
    bevel_rect(pixels, stride, MULTIPLAYER_LAUNCH_LEFT,
               MULTIPLAYER_LAUNCH_TOP, MULTIPLAYER_LAUNCH_WIDTH,
               MULTIPLAYER_LAUNCH_HEIGHT, COLOR_FACE, launch_pressed);
    if (launch_selected) {
        outline_rect(pixels, stride, MULTIPLAYER_LAUNCH_LEFT,
                     MULTIPLAYER_LAUNCH_TOP, MULTIPLAYER_LAUNCH_WIDTH,
                     MULTIPLAYER_LAUNCH_HEIGHT, COLOR_YELLOW);
    }
    const char *launch = !shell->runtime.doom_wad_ready
        ? "DOOM DATA NOT READY"
        : shell->runtime.multiplayer_launch_syncing
            ? "STARTING TOGETHER..."
            : shell->runtime.multiplayer_lobby_ready
                ? "A / TAP: START MATCH"
                : "WAITING FOR PEER + RELAY";
    draw_centered_text(pixels, stride, MULTIPLAYER_LAUNCH_LEFT,
                       MULTIPLAYER_LAUNCH_TOP + 7,
                       MULTIPLAYER_LAUNCH_WIDTH, launch,
                       shell->runtime.multiplayer_lobby_ready
                           ? COLOR_TITLE : COLOR_DARK,
                       28U);
    draw_text(pixels, stride, 8, 190,
              shell->runtime.multiplayer_settings_editable
                  ? "HOST SETTINGS WIN / LEFT-RIGHT OR TAP"
                  : "CLIENT LOCKED TO HOST SETTINGS",
              COLOR_MUTED, 1U, 43U);
}

static void draw_saves(const console_shell_t *shell,
                       uint16_t *pixels, size_t stride)
{
    fill_rect(pixels, stride, 0, 32, CONSOLE_SHELL_LAYOUT_WIDTH,
              CONSOLE_SHELL_LAYOUT_HEIGHT - 32, COLOR_FACE);
    draw_text(pixels, stride, 12, 38, "SAVE SLOT", COLOR_DARK, 1U, 9U);
    draw_text(pixels, stride, 246, 38, "SIZE", COLOR_DARK, 1U, 4U);
    if (shell->saves.count == 0U) {
        draw_centered_text(pixels, stride, 28, 83, 264,
                           "NO SAVE GAMES YET", COLOR_TITLE, 17U);
        draw_centered_text(pixels, stride, 28, 103, 264,
                           "GAMES OWN DATA THROUGH OS API",
                           COLOR_DARK, 29U);
    } else {
        const size_t visible = shell->saves.count < 5U
            ? shell->saves.count : 5U;
        for (size_t index = 0U; index < visible; ++index) {
            const p4_save_slot_t *const slot = &shell->saves.slots[index];
            const int top = 51 + (int)index * 23;
            fill_rect(pixels, stride, 8, top, 304, 20, COLOR_GROUP);
            outline_rect(pixels, stride, 8, top, 304, 20, COLOR_SHADOW);
            draw_text(pixels, stride, 14, top + 4, slot->slot_name,
                      COLOR_BLACK, 1U, 18U);
            draw_text(pixels, stride, 94, top + 4, slot->game_id,
                      COLOR_DARK, 1U, 23U);
            char size[16];
            p4_format_file_size(slot->size_bytes, size);
            draw_text(pixels, stride, 250, top + 4, size,
                      COLOR_BLACK, 1U, 10U);
        }
    }
    char total[16];
    p4_format_file_size(shell->saves.total_bytes, total);
    draw_text(pixels, stride, 10, 169, "TOTAL", COLOR_DARK, 1U, 5U);
    draw_text(pixels, stride, 48, 169, total, COLOR_BLACK, 1U, 15U);
    draw_text(pixels, stride, 180, 169,
              shell->saves.writable ? "OS MANAGED" : "READ ONLY",
              shell->saves.writable ? COLOR_GREEN : COLOR_RED, 1U, 10U);
    draw_text(pixels, stride, 10, 187,
              "NO GAME GETS A FILESYSTEM HANDLE", COLOR_DARK, 1U, 33U);
}

static void draw_terminal_key(const console_shell_t *shell,
                              uint16_t *pixels, size_t stride,
                              int left, int top, int width,
                              size_t control, const char *label)
{
    const bool pressed = shell->press_active &&
        shell->pressed_index == control;
    bevel_rect(pixels, stride, left, top, width, 18, COLOR_FACE, pressed);
    draw_centered_text(pixels, stride, left, top + 6, width,
                       label, COLOR_BLACK, 6U);
}

static void draw_terminal(const console_shell_t *shell,
                          uint16_t *pixels, size_t stride)
{
    fill_rect(pixels, stride, 0, 32, CONSOLE_SHELL_LAYOUT_WIDTH,
              CONSOLE_SHELL_LAYOUT_HEIGHT - 32, COLOR_BLACK);
    for (size_t index = 0U; index < shell->terminal.line_count; ++index) {
        draw_text(pixels, stride, 8, 36 + (int)index * 10,
                  shell->terminal.lines[index], COLOR_MUTED, 1U, 47U);
    }
    fill_rect(pixels, stride, 6, 88, 308, 18, COLOR_PANEL);
    outline_rect(pixels, stride, 6, 88, 308, 18, COLOR_CYAN);
    draw_text(pixels, stride, 10, 94, ">", COLOR_GREEN, 1U, 1U);
    draw_text(pixels, stride, 20, 94, shell->terminal.input,
              COLOR_WHITE, 1U, 47U);

    for (size_t index = 0U; index < TERMINAL_LETTER_KEY_COUNT; ++index) {
        unsigned row = 0U;
        unsigned column = 0U;
        int left = 8;
        if (index >= 19U) {
            row = 2U;
            column = (unsigned)(index - 19U);
            left = 53;
        } else if (index >= 10U) {
            row = 1U;
            column = (unsigned)(index - 10U);
            left = 23;
        } else {
            column = (unsigned)index;
        }
        char label[2] = {s_terminal_keys[index], '\0'};
        draw_terminal_key(shell, pixels, stride,
                          left + (int)column * 30,
                          111 + (int)row * 21, 28,
                          TERMINAL_KEY_CONTROL_BASE + index, label);
    }
    draw_terminal_key(shell, pixels, stride, 8, 174, 170,
                      TERMINAL_SPACE_CONTROL, "SPACE");
    draw_terminal_key(shell, pixels, stride, 182, 174, 60,
                      TERMINAL_BACKSPACE_CONTROL, "BACK");
    draw_terminal_key(shell, pixels, stride, 246, 174, 66,
                      TERMINAL_ENTER_CONTROL, "ENTER");
}

bool console_shell_render_rgb565(console_shell_t *shell,
                                 uint16_t *pixels,
                                 size_t stride_pixels)
{
    if (shell == NULL || pixels == NULL ||
        stride_pixels < CONSOLE_SHELL_WIDTH) {
        return false;
    }
    s_palette = &s_color_palettes[console_shell_color_mode(shell)];
    fill_rect(pixels, stride_pixels, 0, 0,
              CONSOLE_SHELL_LAYOUT_WIDTH,
              CONSOLE_SHELL_LAYOUT_HEIGHT, COLOR_BLACK);
    if (shell->page == CONSOLE_PAGE_HOME) {
#if CONSOLE_SHELL_NATIVE_BBS
        if (use_bbs_launcher(shell) &&
            draw_bbs_home(shell, pixels, stride_pixels)) {
            /* Native ANSI renderer owns the complete 768x480 home surface. */
        } else {
            draw_home(shell, pixels, stride_pixels);
        }
#else
        draw_home(shell, pixels, stride_pixels);
#endif
    } else {
        draw_detail_header(shell, pixels, stride_pixels);
        switch (shell->page) {
        case CONSOLE_PAGE_COLORS:
            draw_colors(shell, pixels, stride_pixels);
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
        case CONSOLE_PAGE_ACHIEVEMENTS:
            draw_achievements(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_MULTIPLAYER:
            draw_multiplayer(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_SAVES:
            draw_saves(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_USB_DRIVE:
            draw_usb_drive(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_TERMINAL:
            draw_terminal(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_STORAGE:
            draw_storage(shell, pixels, stride_pixels);
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
    draw_pointer(shell, pixels, stride_pixels);
    shell->dirty = false;
    ++shell->render_generation;
    return true;
}
