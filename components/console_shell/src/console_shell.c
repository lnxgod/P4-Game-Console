// SPDX-License-Identifier: MIT

#include "console/shell.h"
#include "console/brand.h"
#include "console/game_art.h"
#include "game_covers.inc"
#include "console/ui_font.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifndef P4_CONSOLE_OS_VERSION
#define P4_CONSOLE_OS_VERSION "DEV"
#endif

#define P4_CONSOLE_OS_VERSION_LABEL "OS " CONSOLE_PRODUCT_VERSION

/* Rendering is single-tasked by the shell owner; this state lets the same
 * layout primitives target either native 768x480 or the compact presentation
 * surface used by the board's exact hardware scaler. */
static size_t s_render_width = CONSOLE_SHELL_WIDTH;
static size_t s_render_height = CONSOLE_SHELL_HEIGHT;

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
    SCROLL_THUMB_CONTROL,
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
    /* Contacts arrive in the display's physical viewport coordinate space.
     * Keep the tap/drag dead zone small, explicit, and independent of the
     * launcher's logical rendering scale. */
    SCROLL_PHYSICAL_DRAG_THRESHOLD = 4,
    SCROLL_TOUCH_ROW_PIXELS = 40,
    SCROLL_POSITION_ONE = 1 << 16,
    /* Keep arrow/menu moves responsive while still exposing several 60 Hz
     * samples for interpolation. */
    SCROLL_ANIMATION_BASE_MS = 90,
    SCROLL_ANIMATION_PER_ROW_MS = 24,
    SCROLL_ANIMATION_MAX_MS = 220,
    SCROLL_ADVANCE_MAX_MS = 50,
    SCROLL_VELOCITY_MAX_SAMPLE_MS = 100,
    SCROLL_FLING_PROJECTION_MS = 80,
    SCROLL_FLING_MAX_Q16 = SCROLL_POSITION_ONE,
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
    CONTROLLER_BUTTON_TOP = 174,
    CONTROLLER_BUTTON_HEIGHT = 20,
    CONTROLLER_BLE_LEFT = 8,
    CONTROLLER_BLE_WIDTH = 47,
    CONTROLLER_PAIR_LEFT = 58,
    CONTROLLER_PAIR_WIDTH = 47,
    CONTROLLER_DISCONNECT_LEFT = 108,
    CONTROLLER_DISCONNECT_WIDTH = 47,
    CONTROLLER_MAP_LEFT = 158,
    CONTROLLER_MAP_WIDTH = 47,
    CONTROLLER_RESET_LEFT = 208,
    CONTROLLER_RESET_WIDTH = 47,
    CONTROLLER_FORGET_LEFT = 258,
    CONTROLLER_FORGET_WIDTH = 54,
    AUDIO_MINUS_LEFT = 174,
    AUDIO_PLUS_LEFT = 274,
    AUDIO_BUTTON_WIDTH = 38,
    AUDIO_BUTTON_HEIGHT = 24,
    AUDIO_BOOT_TOP = 61,
    AUDIO_GAME_TOP = 112,
    MULTIPLAYER_ROLE_TOP = 68,
    MULTIPLAYER_ROLE_WIDTH = 146,
    MULTIPLAYER_ROLE_HEIGHT = 82,
    MULTIPLAYER_ROLE_HOST_LEFT = 8,
    MULTIPLAYER_ROLE_JOIN_LEFT = 166,
    MULTIPLAYER_SIMPLE_GAME_LEFT = 8,
    MULTIPLAYER_SIMPLE_GAME_TOP = 59,
    MULTIPLAYER_SIMPLE_GAME_WIDTH = 304,
    MULTIPLAYER_SIMPLE_GAME_HEIGHT = 36,
    MULTIPLAYER_SIMPLE_MATCH_LEFT = 8,
    MULTIPLAYER_SIMPLE_MATCH_TOP = 101,
    MULTIPLAYER_SIMPLE_MATCH_WIDTH = 304,
    MULTIPLAYER_SIMPLE_MATCH_HEIGHT = 45,
    MULTIPLAYER_SIMPLE_SETTINGS_LEFT = 80,
    MULTIPLAYER_SIMPLE_SETTINGS_TOP = 151,
    MULTIPLAYER_SIMPLE_SETTINGS_WIDTH = 160,
    MULTIPLAYER_SIMPLE_SETTINGS_HEIGHT = 19,
    MULTIPLAYER_OPTION_LEFT = 8,
    MULTIPLAYER_OPTION_WIDTH = 304,
    MULTIPLAYER_OPTION_HEIGHT = 13,
    MULTIPLAYER_TRANSPORT_TOP = 59,
    MULTIPLAYER_GAME_TOP = 73,
    MULTIPLAYER_MODE_TOP = 95,
    MULTIPLAYER_MAP_TOP = 109,
    MULTIPLAYER_SKILL_TOP = 123,
    MULTIPLAYER_MONSTERS_TOP = 137,
    MULTIPLAYER_ADVANCED_LEFT_WIDTH = 99,
    MULTIPLAYER_ADVANCED_MIDDLE_LEFT = 110,
    MULTIPLAYER_ADVANCED_RIGHT_LEFT = 212,
    MULTIPLAYER_ADVANCED_RIGHT_WIDTH = 100,
    MULTIPLAYER_ADVANCED_TOP = 158,
    MULTIPLAYER_ADVANCED_HEIGHT = 14,
    MULTIPLAYER_LAUNCH_LEFT = 8,
    MULTIPLAYER_LAUNCH_TOP = 175,
    MULTIPLAYER_LAUNCH_WIDTH = 304,
    MULTIPLAYER_LAUNCH_HEIGHT = 21,
    MULTIPLAYER_JOIN_ROOMS_TOP = 84,
    MULTIPLAYER_JOIN_ROOM_HEIGHT = 20,
};

#if CONSOLE_SHELL_NATIVE_BBS
enum {
    BBS_DOOR_FIRST_ANSI_ROW = 9,
    BBS_DOOR_ANSI_ROW_COUNT = 14,
    BBS_DOOR_VIEW_TOP =
        BBS_DOOR_FIRST_ANSI_ROW * P4_ANSI_CELL_HEIGHT,
    BBS_DOOR_VIEW_HEIGHT =
        BBS_DOOR_ANSI_ROW_COUNT * P4_ANSI_CELL_HEIGHT,
    BBS_DOOR_PITCH_PIXELS = 3 * P4_ANSI_CELL_HEIGHT,
};
#endif

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
    &s_color_palettes[CONSOLE_COLOR_MODE_ARCADE];
static int s_clip_left;
static int s_clip_top;
static int s_clip_right = CONSOLE_SHELL_LAYOUT_WIDTH;
static int s_clip_bottom = CONSOLE_SHELL_LAYOUT_HEIGHT;
static int s_output_clip_left;
static int s_output_clip_top;
static int s_output_clip_right = CONSOLE_SHELL_WIDTH;
static int s_output_clip_bottom = CONSOLE_SHELL_HEIGHT;

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
    char path_segment[CONSOLE_SHELL_FOLDER_SEGMENT_MAX_BYTES];
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
    CONTROLLER_BLE_CONTROL,
    CONTROLLER_PAIR_CONTROL,
    CONTROLLER_DISCONNECT_CONTROL,
    CONTROLLER_MAP_CONTROL,
    CONTROLLER_RESET_CONTROL,
    CONTROLLER_FORGET_CONTROL,
    AUDIO_BOOT_MINUS_CONTROL,
    AUDIO_BOOT_PLUS_CONTROL,
    AUDIO_GAME_MINUS_CONTROL,
    AUDIO_GAME_PLUS_CONTROL,
    MULTIPLAYER_ROLE_HOST_CONTROL,
    MULTIPLAYER_ROLE_JOIN_CONTROL,
    MULTIPLAYER_JOIN_ROOM_CONTROL_BASE,
    MULTIPLAYER_JOIN_ROOM_CONTROL_LIMIT =
        MULTIPLAYER_JOIN_ROOM_CONTROL_BASE +
            CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX,
    MULTIPLAYER_OPTION_MINUS_CONTROL_BASE,
    MULTIPLAYER_OPTION_PLUS_CONTROL_BASE =
        MULTIPLAYER_OPTION_MINUS_CONTROL_BASE +
            CONSOLE_MULTIPLAYER_OPTION_COUNT,
    MULTIPLAYER_LAUNCH_CONTROL =
        MULTIPLAYER_OPTION_PLUS_CONTROL_BASE +
            CONSOLE_MULTIPLAYER_OPTION_COUNT,
    MULTIPLAYER_SETTINGS_CONTROL,
    TERMINAL_KEY_CONTROL_BASE,
    TERMINAL_LETTER_KEY_COUNT = 26,
    TERMINAL_SPACE_CONTROL =
        TERMINAL_KEY_CONTROL_BASE + TERMINAL_LETTER_KEY_COUNT,
    TERMINAL_BACKSPACE_CONTROL,
    TERMINAL_ENTER_CONTROL,
    TERMINAL_KEY_CONTROL_LIMIT,
};

static const char s_terminal_keys[] = "QWERTYUIOPASDFGHJKLZXCVBNM";

#if CONFIG_P4_BOARD_M5STACK_TAB5
static console_shell_action_t nextgen_buttons(console_shell_t *, uint32_t);
static console_shell_action_t nextgen_touch(console_shell_t *, bool, const console_shell_contact_t *, size_t);
static bool nextgen_render(console_shell_t *, uint16_t *, size_t, bool);
static bool nextgen_advance(console_shell_t *, uint32_t);
#endif
static console_shell_action_t no_action(void);
static console_shell_action_t page_changed(uint32_t app_id);

static bool use_bbs_launcher(const console_shell_t *shell)
{
#if CONSOLE_SHELL_NATIVE_BBS && !CONFIG_P4_BOARD_M5STACK_TAB5 && \
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
    return use_bbs_launcher(shell) ? 1U : CONSOLE_SHELL_APP_COLUMNS;
}

static size_t home_visible_row_count(const console_shell_t *shell)
{
#if CONSOLE_SHELL_NATIVE_BBS
    if (use_bbs_launcher(shell)) {
        return P4_BBS_VISIBLE_DOORS;
    }
#else
    (void)shell;
#endif
    return CONSOLE_SHELL_VISIBLE_APP_ROWS;
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
        page <= CONSOLE_PAGE_APPEARANCE;
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
    shell->color_mode = CONSOLE_COLOR_MODE_ARCADE;
    shell->multiplayer_selected_row = CONSOLE_MULTIPLAYER_OPTION_COUNT;
    shell->multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_ROLE;
    shell->multiplayer_role_selection = 0U;
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

typedef struct {
    unsigned left;
    unsigned top;
    unsigned width;
    unsigned height;
    bool compact;
} multiplayer_option_layout_t;

static bool multiplayer_uses_inline_lobby_panel(const console_shell_t *shell)
{
    return shell != NULL &&
        shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_JOIN;
}

static multiplayer_option_layout_t multiplayer_option_layout(
    bool inline_lobby_panel, console_multiplayer_option_t option)
{
    multiplayer_option_layout_t layout = {
        .left = MULTIPLAYER_OPTION_LEFT,
        .width = MULTIPLAYER_OPTION_WIDTH,
        .height = MULTIPLAYER_OPTION_HEIGHT,
    };
    switch (option) {
    case CONSOLE_MULTIPLAYER_OPTION_GAME:
        layout.top = MULTIPLAYER_GAME_TOP;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_TRANSPORT:
        layout.top = MULTIPLAYER_TRANSPORT_TOP;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_LOBBY:
        (void)inline_lobby_panel;
        layout.width = 0U;
        layout.height = 0U;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_DICE:
    case CONSOLE_MULTIPLAYER_OPTION_MODE:
        layout.top = MULTIPLAYER_MODE_TOP;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_MAP:
        layout.top = MULTIPLAYER_MAP_TOP;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_SKILL:
        layout.top = MULTIPLAYER_SKILL_TOP;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_MONSTERS:
        layout.top = MULTIPLAYER_MONSTERS_TOP;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_FAST:
        layout.top = MULTIPLAYER_ADVANCED_TOP;
        layout.width = MULTIPLAYER_ADVANCED_LEFT_WIDTH;
        layout.height = MULTIPLAYER_ADVANCED_HEIGHT;
        layout.compact = true;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_RESPAWN:
        layout.left = MULTIPLAYER_ADVANCED_MIDDLE_LEFT;
        layout.top = MULTIPLAYER_ADVANCED_TOP;
        layout.width = MULTIPLAYER_ADVANCED_LEFT_WIDTH;
        layout.height = MULTIPLAYER_ADVANCED_HEIGHT;
        layout.compact = true;
        break;
    case CONSOLE_MULTIPLAYER_OPTION_TIME_LIMIT:
        layout.left = MULTIPLAYER_ADVANCED_RIGHT_LEFT;
        layout.top = MULTIPLAYER_ADVANCED_TOP;
        layout.width = MULTIPLAYER_ADVANCED_RIGHT_WIDTH;
        layout.height = MULTIPLAYER_ADVANCED_HEIGHT;
        layout.compact = true;
        break;
    default:
        layout.width = 0U;
        layout.height = 0U;
        break;
    }
    return layout;
}

static multiplayer_option_layout_t multiplayer_launch_layout(
    bool inline_lobby_panel)
{
    (void)inline_lobby_panel;
    return (multiplayer_option_layout_t) {
        .left = MULTIPLAYER_LAUNCH_LEFT,
        .top = MULTIPLAYER_LAUNCH_TOP,
        .width = MULTIPLAYER_LAUNCH_WIDTH,
        .height = MULTIPLAYER_LAUNCH_HEIGHT,
    };
}

static const size_t s_multiplayer_arena_navigation_rows[] = {
    CONSOLE_MULTIPLAYER_OPTION_TRANSPORT,
    CONSOLE_MULTIPLAYER_OPTION_GAME,
    CONSOLE_MULTIPLAYER_OPTION_MAP,
    CONSOLE_MULTIPLAYER_OPTION_COUNT,
};

static const size_t s_multiplayer_doom_navigation_rows[] = {
    CONSOLE_MULTIPLAYER_OPTION_TRANSPORT,
    CONSOLE_MULTIPLAYER_OPTION_GAME,
    CONSOLE_MULTIPLAYER_OPTION_MODE,
    CONSOLE_MULTIPLAYER_OPTION_MAP,
    CONSOLE_MULTIPLAYER_OPTION_SKILL,
    CONSOLE_MULTIPLAYER_OPTION_MONSTERS,
    CONSOLE_MULTIPLAYER_OPTION_FAST,
    CONSOLE_MULTIPLAYER_OPTION_RESPAWN,
    CONSOLE_MULTIPLAYER_OPTION_TIME_LIMIT,
    CONSOLE_MULTIPLAYER_OPTION_COUNT,
};

enum {
    MULTIPLAYER_HOST_SETTINGS_ROW = CONSOLE_MULTIPLAYER_OPTION_COUNT + 1U,
};

static const size_t s_multiplayer_host_navigation_rows[] = {
    CONSOLE_MULTIPLAYER_OPTION_GAME,
    MULTIPLAYER_HOST_SETTINGS_ROW,
    CONSOLE_MULTIPLAYER_OPTION_COUNT,
};

static const size_t s_multiplayer_native_navigation_rows[] = {
    CONSOLE_MULTIPLAYER_OPTION_TRANSPORT,
    CONSOLE_MULTIPLAYER_OPTION_GAME,
    CONSOLE_MULTIPLAYER_OPTION_COUNT,
};

static const size_t s_multiplayer_dice_navigation_rows[] = {
    CONSOLE_MULTIPLAYER_OPTION_TRANSPORT,
    CONSOLE_MULTIPLAYER_OPTION_GAME,
    CONSOLE_MULTIPLAYER_OPTION_DICE,
    CONSOLE_MULTIPLAYER_OPTION_COUNT,
};

static const size_t s_multiplayer_join_navigation_rows[] = {
    CONSOLE_MULTIPLAYER_OPTION_TRANSPORT,
    CONSOLE_MULTIPLAYER_OPTION_LOBBY,
    CONSOLE_MULTIPLAYER_OPTION_COUNT,
};

static const size_t *multiplayer_navigation_rows(
    const console_shell_t *shell, size_t *count)
{
    if (shell != NULL &&
        shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST) {
        *count = sizeof(s_multiplayer_host_navigation_rows) /
            sizeof(s_multiplayer_host_navigation_rows[0]);
        return s_multiplayer_host_navigation_rows;
    }
    if (shell != NULL &&
        shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_JOIN) {
        *count = sizeof(s_multiplayer_join_navigation_rows) /
            sizeof(s_multiplayer_join_navigation_rows[0]);
        return s_multiplayer_join_navigation_rows;
    }
    if (shell != NULL && shell->runtime.multiplayer_game_is_arena) {
        *count=sizeof(s_multiplayer_arena_navigation_rows)/sizeof(s_multiplayer_arena_navigation_rows[0]);
        return s_multiplayer_arena_navigation_rows;
    }
    if (shell != NULL && !shell->runtime.multiplayer_game_is_doom &&
        shell->runtime.multiplayer_dice_available) {
        *count = sizeof(s_multiplayer_dice_navigation_rows) /
            sizeof(s_multiplayer_dice_navigation_rows[0]);
        return s_multiplayer_dice_navigation_rows;
    }
    if (shell != NULL && !shell->runtime.multiplayer_game_is_doom) {
        *count = sizeof(s_multiplayer_native_navigation_rows) /
            sizeof(s_multiplayer_native_navigation_rows[0]);
        return s_multiplayer_native_navigation_rows;
    }
    *count = sizeof(s_multiplayer_doom_navigation_rows) /
        sizeof(s_multiplayer_doom_navigation_rows[0]);
    return s_multiplayer_doom_navigation_rows;
}

static size_t multiplayer_navigation_index(
    const size_t *rows, size_t row_count, size_t selected_row)
{
    for (size_t index = 0U; index < row_count; ++index) {
        if (rows[index] == selected_row) {
            return index;
        }
    }
    return row_count - 1U;
}

static void move_multiplayer_selection(console_shell_t *shell, int delta)
{
    size_t row_count = 0U;
    const size_t *const rows = multiplayer_navigation_rows(shell, &row_count);
    size_t index = multiplayer_navigation_index(
        rows, row_count, shell->multiplayer_selected_row);
    if (delta < 0) {
        index = (index + row_count - 1U) % row_count;
    } else {
        index = (index + 1U) % row_count;
    }
    shell->multiplayer_selected_row = rows[index];
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

static bool item_segment_equal(const home_item_t *item, const char *segment)
{
    return item->kind == HOME_ITEM_FOLDER &&
           strncmp(item->path_segment, segment,
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
    const size_t segment_length = bounded_length(
        segment, sizeof(item->path_segment) - 1U);
    memcpy(item->path_segment, segment, segment_length);
    item->path_segment[segment_length] = '\0';
    const char *const display_title =
        parent[0] == '\0' && strcmp(segment, "SYSTEM") == 0
            ? "CONTROL PANEL" : segment;
    const size_t title_length = bounded_length(
        display_title, sizeof(item->title) - 1U);
    memcpy(item->title, display_title, title_length);
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


/* One settings destination; app IDs and service actions remain unchanged. */
enum { PANEL_SECTION_COUNT = 6, PANEL_VISIBLE_ROWS = 4,
       PANEL_NAV_BASE = 1000, PANEL_ROW_BASE = 1010,
       PANEL_PREVIOUS = 1020, PANEL_NEXT, PANEL_VOLUME_BASE = 1030 };
static const char *const s_panel_names[PANEL_SECTION_COUNT] = {
    "Overview", "Preferences", "Controls", "Storage", "Connections", "Advanced"
};
static bool panel_app(const console_app_descriptor_t *app)
{
    return path_is_at_or_below(app->folder_path, "SYSTEM");
}
static unsigned panel_section(const console_app_descriptor_t *app)
{
    switch (app->page) {
    case CONSOLE_PAGE_COLORS: case CONSOLE_PAGE_AUDIO: return 1U;
    case CONSOLE_PAGE_TOUCH: case CONSOLE_PAGE_CONTROLLERS: return 2U;
    case CONSOLE_PAGE_FILES: case CONSOLE_PAGE_GAMES: case CONSOLE_PAGE_SAVES:
    case CONSOLE_PAGE_STORAGE: case CONSOLE_PAGE_USB_DRIVE: return 3U;
    case CONSOLE_PAGE_MULTIPLAYER: case CONSOLE_PAGE_FILE_TRANSFER: return 4U;
    default: return 5U;
    }
}
static size_t panel_items(const console_shell_t *shell,
                          size_t indexes[CONSOLE_SHELL_MAX_APPS])
{
    size_t count = 0U;
    for (size_t i = 0U; i < shell->app_count; ++i) {
        const console_app_descriptor_t *app = &shell->apps[i];
        if (!panel_app(app) || !app->enabled ||
            panel_section(app) != shell->control_panel_section ||
            (app->page == CONSOLE_PAGE_USB_DRIVE && !shell->runtime.usb_storage_supported) ||
            (shell->control_panel_section == 1U && app->page == CONSOLE_PAGE_AUDIO)) continue;
        indexes[count++] = i;
    }
    return count;
}
static void panel_select_section(console_shell_t *shell, unsigned section)
{
    shell->control_panel_section = (uint8_t)(section < PANEL_SECTION_COUNT ? section : 0U);
    shell->control_panel_first = 0U;
    shell->control_panel_selection = 0U;
    shell->control_panel_row_focus = false;
    shell->dirty = true;
}
static void panel_open(console_shell_t *shell)
{
    shell->page = CONSOLE_PAGE_CONTROL_PANEL;
    shell->active_app_id = 0U;
    shell->control_panel_active = true;
    shell->press_active = false;
    shell->pressed_index = SIZE_MAX;
    shell->dirty = true;
}

static int home_item_order(const console_shell_t *shell, const home_item_t *a, const home_item_t *b)
{
    const unsigned ak = a->kind == HOME_ITEM_ALL_PROGRAMS ? 0U : a->kind == HOME_ITEM_FOLDER ? 1U : 2U;
    const unsigned bk = b->kind == HOME_ITEM_ALL_PROGRAMS ? 0U : b->kind == HOME_ITEM_FOLDER ? 1U : 2U;
    if (ak != bk) return ak < bk ? -1 : 1;
    const char *at = a->kind == HOME_ITEM_APP ? shell->apps[a->app_index].title : a->path_segment;
    const char *bt = b->kind == HOME_ITEM_APP ? shell->apps[b->app_index].title : b->path_segment;
    for (size_t i = 0U; i < sizeof(a->title); ++i) {
        unsigned char ac = (unsigned char)at[i], bc = (unsigned char)bt[i];
        if (ac >= 'a' && ac <= 'z') ac = (unsigned char)(ac - 'a' + 'A');
        if (bc >= 'a' && bc <= 'z') bc = (unsigned char)(bc - 'a' + 'A');
        if (ac != bc) return ac < bc ? -1 : 1;
        if (!ac) break;
    }
    if (a->kind == HOME_ITEM_APP) {
        const uint32_t ai = shell->apps[a->app_index].id, bi = shell->apps[b->app_index].id;
        return ai < bi ? -1 : ai > bi ? 1 : 0;
    }
    return 0;
}

static void sort_home_items(const console_shell_t *shell, home_item_t *items, size_t count)
{
    for (size_t i = 1U; i < count; ++i) {
        const home_item_t item = items[i];
        size_t j = i;
        while (j > 0U && home_item_order(shell, &item, &items[j - 1U]) < 0) {
            items[j] = items[j - 1U]; --j;
        }
        items[j] = item;
    }
}

static size_t build_home_items(
    const console_shell_t *shell,
    home_item_t items[HOME_ITEM_CAPACITY])
{
    size_t count = 0U;
    if (shell->home_all_programs) {
        if (shell->runtime.control_panel_enabled) {
            for (size_t i = 0U; i < shell->app_count; ++i) {
                if (panel_app(&shell->apps[i])) {
                    populate_folder_item(shell, "", "SYSTEM", &items[count++]);
                    break;
                }
            }
        }
        for (size_t i = 0U;
             i < shell->app_count && count < HOME_ITEM_CAPACITY; ++i) {
            if (shell->runtime.control_panel_enabled && panel_app(&shell->apps[i])) continue;
            items[count++] = (home_item_t){
                .kind = HOME_ITEM_APP,
                .app_index = i,
                .accent_rgb565 = shell->apps[i].accent_rgb565,
                .capabilities = shell->apps[i].capabilities,
                .program_count = 1U,
                .enabled = shell->apps[i].enabled,
            };
        }
        sort_home_items(shell, items, count);
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
            if (item_segment_equal(&items[j], segment)) {
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
    sort_home_items(shell, items, count);
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

static size_t home_first_visible_index_at_row(
    const console_shell_t *shell, size_t scroll_row)
{
    return scroll_row * home_column_count(shell);
}

static size_t home_first_visible_index(const console_shell_t *shell)
{
    return home_first_visible_index_at_row(shell, shell->home_scroll_row);
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

static void build_bbs_launcher_model_at_row(
    const console_shell_t *shell,
    size_t scroll_row,
    p4_bbs_launcher_model_t *model)
{
    static const uint8_t accents[P4_BBS_VISIBLE_DOORS] = {
        P4_ANSI_COLOR_BRIGHT_CYAN,
        P4_ANSI_COLOR_YELLOW,
        P4_ANSI_COLOR_BRIGHT_GREEN,
        P4_ANSI_COLOR_BRIGHT_MAGENTA,
        P4_ANSI_COLOR_BRIGHT_RED,
    };
    memset(model, 0, sizeof(*model));
    bbs_copy_text(model->board_name, sizeof(model->board_name),
                  "GAME CHANGERS AI BBS");
    bbs_copy_text(model->node_name, sizeof(model->node_name),
                  shell->runtime.node_name);
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
    model->battery_supported = shell->runtime.battery_supported;
    model->battery_sample_valid = shell->runtime.battery_sample_valid;
    model->battery_percent = shell->runtime.battery_percent;
    model->storage_is_sd = shell->runtime.sd_card_storage;
    model->storage_space_valid = shell->runtime.game_storage_space_valid;
    model->storage_total_kib = shell->runtime.game_storage_kib;
    model->storage_free_kib = shell->runtime.game_storage_free_kib;
    const size_t maximum = home_max_scroll_row(shell);
    if (scroll_row > maximum) {
        scroll_row = maximum;
    }
    model->page = (uint16_t)(scroll_row + 1U);
    model->page_count = (uint16_t)(home_max_scroll_row(shell) + 1U);
    model->uploads = shell->runtime.valid_cart_count;

    home_item_t items[HOME_ITEM_CAPACITY];
    const size_t item_count = build_home_items(shell, items);
    const size_t first = home_first_visible_index_at_row(
        shell, scroll_row);
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

static void build_bbs_launcher_model(
    const console_shell_t *shell,
    p4_bbs_launcher_model_t *model)
{
    build_bbs_launcher_model_at_row(
        shell, shell->home_scroll_row, model);
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

static int32_t home_scroll_row_q16(size_t row)
{
    return (int32_t)(row * (size_t)SCROLL_POSITION_ONE);
}

static int32_t clamp_home_scroll_q16(
    const console_shell_t *shell, int32_t position_q16)
{
    const int32_t maximum_q16 =
        home_scroll_row_q16(home_max_scroll_row(shell));
    if (position_q16 < 0) {
        return 0;
    }
    return position_q16 > maximum_q16 ? maximum_q16 : position_q16;
}

static bool home_scroll_is_animating(const console_shell_t *shell)
{
    return shell->home_scroll_duration_ms > 0U &&
        shell->home_scroll_elapsed_ms < shell->home_scroll_duration_ms;
}

static void sync_home_scroll_visual(console_shell_t *shell)
{
    const int32_t target = home_scroll_row_q16(shell->home_scroll_row);
    shell->home_scroll_visual_q16 = target;
    shell->home_scroll_from_q16 = target;
    shell->home_scroll_elapsed_ms = 0U;
    shell->home_scroll_duration_ms = 0U;
    shell->home_drag_velocity_q16_per_ms = 0;
}

static uint16_t home_scroll_duration(
    int32_t from_q16, int32_t target_q16)
{
    int64_t distance = (int64_t)target_q16 - from_q16;
    if (distance < 0) {
        distance = -distance;
    }
    const uint32_t rows = (uint32_t)(
        (distance + SCROLL_POSITION_ONE - 1) / SCROLL_POSITION_ONE);
    uint32_t duration = SCROLL_ANIMATION_BASE_MS +
        rows * SCROLL_ANIMATION_PER_ROW_MS;
    if (duration > SCROLL_ANIMATION_MAX_MS) {
        duration = SCROLL_ANIMATION_MAX_MS;
    }
    return (uint16_t)duration;
}

static bool set_home_scroll_row_internal(
    console_shell_t *shell, size_t row, bool select_first)
{
    const size_t maximum = home_max_scroll_row(shell);
    const size_t bounded = row > maximum ? maximum : row;
    const bool logical_changed = bounded != shell->home_scroll_row;
    const int32_t target_q16 = home_scroll_row_q16(bounded);
    if (!logical_changed &&
        shell->home_scroll_visual_q16 == target_q16 &&
        !home_scroll_is_animating(shell)) {
        return false;
    }
    shell->home_scroll_row = bounded;
    shell->home_scroll_visual_q16 = clamp_home_scroll_q16(
        shell, shell->home_scroll_visual_q16);
    if (shell->home_scroll_visual_q16 == target_q16) {
        sync_home_scroll_visual(shell);
    } else {
        shell->home_scroll_from_q16 = shell->home_scroll_visual_q16;
        shell->home_scroll_elapsed_ms = 0U;
        shell->home_scroll_duration_ms = home_scroll_duration(
            shell->home_scroll_from_q16, target_q16);
    }
    if (select_first) {
        select_first_visible_item(shell);
    }
    shell->dirty = true;
    return logical_changed;
}

static bool set_home_scroll_row(console_shell_t *shell, size_t row)
{
    return set_home_scroll_row_internal(shell, row, true);
}

bool console_shell_advance(console_shell_t *shell, uint32_t elapsed_ms)
{
    if (shell == NULL) {
        return false;
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    return nextgen_advance(shell, elapsed_ms);
#endif
    shell->animation_clock_ms += elapsed_ms;
    if (shell->scroll_gesture &&
        shell->home_drag_velocity_q16_per_ms != 0 &&
        shell->animation_clock_ms - shell->home_drag_sample_ms >
            SCROLL_VELOCITY_MAX_SAMPLE_MS) {
        /* Do not fling from a touch sample that predates a service stall. */
        shell->home_drag_velocity_q16_per_ms = 0;
    }
    if (!home_scroll_is_animating(shell)) {
        return false;
    }
    if (shell->page != CONSOLE_PAGE_HOME) {
        sync_home_scroll_visual(shell);
        return false;
    }

    uint32_t bounded_ms = elapsed_ms;
    if (bounded_ms > SCROLL_ADVANCE_MAX_MS) {
        bounded_ms = SCROLL_ADVANCE_MAX_MS;
    }
    uint32_t next_elapsed =
        (uint32_t)shell->home_scroll_elapsed_ms + bounded_ms;
    if (next_elapsed >= shell->home_scroll_duration_ms) {
        next_elapsed = shell->home_scroll_duration_ms;
    }
    shell->home_scroll_elapsed_ms = (uint16_t)next_elapsed;

    const uint32_t linear_q16 = (uint32_t)(
        (uint64_t)next_elapsed * SCROLL_POSITION_ONE /
        shell->home_scroll_duration_ms);
    /* Quadratic ease-in/out avoids the perceptual dead-time of a long
     * smoothstep while still limiting the first-frame displacement. */
    uint32_t eased_q16;
    if (linear_q16 < (uint32_t)SCROLL_POSITION_ONE / 2U) {
        const uint32_t squared_q16 = (uint32_t)(
            (uint64_t)linear_q16 * linear_q16 >> 16U);
        eased_q16 = 2U * squared_q16;
    } else {
        const uint32_t remaining_q16 =
            (uint32_t)SCROLL_POSITION_ONE - linear_q16;
        const uint32_t squared_remaining_q16 = (uint32_t)(
            (uint64_t)remaining_q16 * remaining_q16 >> 16U);
        eased_q16 = (uint32_t)SCROLL_POSITION_ONE -
            2U * squared_remaining_q16;
    }
    const int32_t target_q16 =
        home_scroll_row_q16(shell->home_scroll_row);
    const int64_t interpolated =
        shell->home_scroll_from_q16 +
        ((int64_t)target_q16 - shell->home_scroll_from_q16) *
            (int64_t)eased_q16 / SCROLL_POSITION_ONE;
    const int32_t next_visual_q16 = clamp_home_scroll_q16(
        shell, (int32_t)interpolated);
    const bool changed =
        next_visual_q16 != shell->home_scroll_visual_q16;
    shell->home_scroll_visual_q16 = next_visual_q16;
    if (next_elapsed >= shell->home_scroll_duration_ms) {
        sync_home_scroll_visual(shell);
    }
    if (changed) {
        shell->dirty = true;
    }
    return changed;
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

static bool controller_action_enabled(const console_shell_t *shell,
                                      size_t action)
{
    if (shell == NULL) {
        return false;
    }
    switch (action) {
    case 0U:
        return shell->runtime.ble_controller_supported &&
            !shell->runtime.ble_controller_busy;
    case 1U:
        return shell->runtime.ble_controller_supported &&
            shell->runtime.ble_controller_enabled &&
            !shell->runtime.ble_controller_connected &&
            !shell->runtime.ble_controller_busy;
    case 2U:
        return shell->runtime.ble_controller_supported &&
            shell->runtime.ble_controller_connected;
    case 3U:
        return shell->runtime.controller_mapping_active ||
            shell->runtime.controller_ready;
    case 4U:
        return !shell->runtime.controller_mapping_active;
    case 5U:
        return shell->runtime.ble_controller_supported &&
            shell->runtime.ble_controller_bonded &&
            !shell->runtime.ble_controller_busy;
    default:
        return false;
    }
}

static console_shell_action_t controller_action(console_shell_t *shell,
                                                 size_t requested)
{
    if (!controller_action_enabled(shell, requested)) {
        return no_action();
    }
    shell->controller_selected_action = requested;
    shell->dirty = true;
    const console_shell_action_t action = {
        .type = requested == 0U
            ? shell->runtime.ble_controller_enabled
                ? CONSOLE_ACTION_CONTROLLER_BLE_DISABLE
                : CONSOLE_ACTION_CONTROLLER_BLE_ENABLE
            : requested == 1U
                ? CONSOLE_ACTION_CONTROLLER_PAIR
                : requested == 2U
                    ? CONSOLE_ACTION_CONTROLLER_DISCONNECT
                    : requested == 3U
                        ? shell->runtime.controller_mapping_active
                            ? CONSOLE_ACTION_CONTROLLER_MAPPING_CANCEL
                            : CONSOLE_ACTION_CONTROLLER_MAPPING_START
                        : requested == 4U
                            ? CONSOLE_ACTION_CONTROLLER_MAPPING_RESET
                            : CONSOLE_ACTION_CONTROLLER_FORGET,
        .app_id = shell->active_app_id,
        .file_source_index = UINT32_MAX,
    };
    return action;
}

static void reset_controller_controls(console_shell_t *shell)
{
    shell->controller_selected_action = 0U;
    while (shell->controller_selected_action < 6U &&
           !controller_action_enabled(
               shell, shell->controller_selected_action)) {
        ++shell->controller_selected_action;
    }
    if (shell->controller_selected_action >= 6U) {
        shell->controller_selected_action = 0U;
    }
}

static uint8_t bounded_volume(uint8_t volume)
{
    return volume > 10U ? 10U : volume;
}

static console_shell_action_t audio_volume_action(
    const console_shell_t *shell, bool boot, int delta)
{
    const uint8_t current = bounded_volume(boot
        ? shell->runtime.boot_volume_step
        : shell->runtime.game_volume_step);
    uint8_t requested = current;
    if (delta < 0 && current > 0U) {
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
        shell->runtime.multiplayer_launch_syncing || delta == 0 ||
        (option == CONSOLE_MULTIPLAYER_OPTION_DICE &&
         (!shell->runtime.multiplayer_dice_available ||
          shell->runtime.multiplayer_game_is_doom ||
          shell->multiplayer_view != CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS))) {
        return no_action();
    }
    if (shell->runtime.multiplayer_game_is_arena &&
        option != CONSOLE_MULTIPLAYER_OPTION_GAME &&
        option != CONSOLE_MULTIPLAYER_OPTION_MAP &&
        option != CONSOLE_MULTIPLAYER_OPTION_TRANSPORT) return no_action();
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

static console_shell_action_t multiplayer_lobby_select_action(
    console_shell_t *shell, uint8_t selection)
{
    if (shell == NULL || selection == 0U ||
        selection > shell->runtime.multiplayer_lobby_count ||
        selection > CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX ||
        !shell->runtime.multiplayer_settings_editable ||
        shell->runtime.multiplayer_launch_syncing) {
        return no_action();
    }
    shell->multiplayer_selected_row = CONSOLE_MULTIPLAYER_OPTION_LOBBY;
    shell->dirty = true;
    return (console_shell_action_t){
        .type = CONSOLE_ACTION_MULTIPLAYER_LOBBY_SELECT,
        .app_id = shell->active_app_id,
        .file_source_index = UINT32_MAX,
        .multiplayer_lobby_selection = selection,
    };
}

static console_shell_action_t enter_multiplayer_view(
    console_shell_t *shell, console_multiplayer_view_t view)
{
    if (shell == NULL || view == CONSOLE_MULTIPLAYER_VIEW_ROLE ||
        view > CONSOLE_MULTIPLAYER_VIEW_JOIN) {
        return no_action();
    }
    shell->multiplayer_view = view;
    shell->multiplayer_role_selection =
        view == CONSOLE_MULTIPLAYER_VIEW_JOIN ? 1U : 0U;
    shell->multiplayer_selected_row =
        view == CONSOLE_MULTIPLAYER_VIEW_JOIN
            ? (size_t)CONSOLE_MULTIPLAYER_OPTION_LOBBY
            : (size_t)CONSOLE_MULTIPLAYER_OPTION_COUNT;
    shell->dirty = true;
    return page_changed(shell->active_app_id);
}

static console_shell_action_t enter_multiplayer_host_settings(
    console_shell_t *shell)
{
    if (shell == NULL ||
        shell->multiplayer_view != CONSOLE_MULTIPLAYER_VIEW_HOST) {
        return no_action();
    }
    shell->multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS;
    shell->multiplayer_selected_row = shell->runtime.multiplayer_game_is_arena
        ? (size_t)CONSOLE_MULTIPLAYER_OPTION_MAP : shell->runtime.multiplayer_game_is_doom
        ? (size_t)CONSOLE_MULTIPLAYER_OPTION_MODE
        : (size_t)CONSOLE_MULTIPLAYER_OPTION_GAME;
    shell->dirty = true;
    return page_changed(shell->active_app_id);
}

static console_shell_action_t return_to_multiplayer_host(
    console_shell_t *shell)
{
    if (shell == NULL ||
        shell->multiplayer_view !=
            CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS) {
        return no_action();
    }
    shell->multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_HOST;
    shell->multiplayer_selected_row = CONSOLE_MULTIPLAYER_OPTION_COUNT;
    shell->dirty = true;
    return page_changed(shell->active_app_id);
}

static console_shell_action_t leave_multiplayer_view(console_shell_t *shell);

static console_shell_action_t multiplayer_back_action(console_shell_t *shell)
{
    if (shell != NULL &&
        shell->multiplayer_view ==
            CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS) {
        return return_to_multiplayer_host(shell);
    }
    return leave_multiplayer_view(shell);
}

static console_shell_action_t leave_multiplayer_view(console_shell_t *shell)
{
    if (shell == NULL ||
        shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_ROLE) {
        return no_action();
    }
    shell->multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_ROLE;
    shell->multiplayer_selected_row = CONSOLE_MULTIPLAYER_OPTION_COUNT;
    shell->dirty = true;
    return (console_shell_action_t){
        .type = CONSOLE_ACTION_MULTIPLAYER_LOBBY_RESET,
        .app_id = shell->active_app_id,
        .file_source_index = UINT32_MAX,
    };
}

static console_shell_action_t multiplayer_primary_action(
    const console_shell_t *shell)
{
    if (shell == NULL || shell->runtime.multiplayer_launch_syncing ||
        !shell->runtime.multiplayer_game_ready) {
        return no_action();
    }
    if (shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST &&
        shell->runtime.multiplayer_can_start) {
        return (console_shell_action_t){
            .type = CONSOLE_ACTION_MULTIPLAYER_LAUNCH_GAME,
            .app_id = shell->active_app_id,
            .file_source_index = UINT32_MAX,
        };
    }
    if (!shell->runtime.multiplayer_lobby_action_enabled ||
        shell->runtime.multiplayer_lobby_phase !=
            CONSOLE_MULTIPLAYER_LOBBY_BROWSING) {
        return no_action();
    }
    if (shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST) {
        return (console_shell_action_t){
            .type = CONSOLE_ACTION_MULTIPLAYER_CREATE_LOBBY,
            .app_id = shell->active_app_id,
            .file_source_index = UINT32_MAX,
        };
    }
    if (shell->multiplayer_view != CONSOLE_MULTIPLAYER_VIEW_JOIN ||
        shell->runtime.multiplayer_lobby_selection == 0U) {
        return no_action();
    }
    return (console_shell_action_t){
        .type = CONSOLE_ACTION_MULTIPLAYER_JOIN_LOBBY,
        .app_id = shell->active_app_id,
        .file_source_index = UINT32_MAX,
    };
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
        if (shell->page == CONSOLE_PAGE_CONTROL_PANEL) {
            for (size_t i = 0; i < PANEL_SECTION_COUNT; ++i)
                if (point_in_rect(gui_x, gui_y, 5U, 42U + (unsigned)i * 23U, 89U, 22U))
                    return PANEL_NAV_BASE + i;
            size_t indexes[CONSOLE_SHELL_MAX_APPS];
            const size_t count = panel_items(shell, indexes);
            if (shell->control_panel_section == 1U) {
                for (unsigned i = 0; i < 4U; ++i)
                    if (point_in_rect(gui_x, gui_y, 228U + (i % 2U) * 43U,
                                      71U + (i / 2U) * 34U, 36U, 25U))
                        return PANEL_VOLUME_BASE + i;
                if (count && point_in_rect(gui_x, gui_y, 106U, 145U, 202U, 25U))
                    return PANEL_ROW_BASE;
            } else if (shell->control_panel_section != 0U) {
                for (size_t i = 0; i < PANEL_VISIBLE_ROWS; ++i)
                    if (shell->control_panel_first + i < count &&
                        point_in_rect(gui_x, gui_y, 105U, 65U + (unsigned)i * 25U, 203U, 24U))
                        return PANEL_ROW_BASE + i;
                if (shell->control_panel_first > 0U && point_in_rect(gui_x,gui_y,105U,168U,96U,20U)) return PANEL_PREVIOUS;
                if (shell->control_panel_first + PANEL_VISIBLE_ROWS < count && point_in_rect(gui_x,gui_y,211U,168U,97U,20U)) return PANEL_NEXT;
            }
            return SIZE_MAX;
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
        if (shell->page == CONSOLE_PAGE_CONTROLLERS) {
            if (controller_action_enabled(shell, 0U) &&
                point_in_rect(gui_x, gui_y, CONTROLLER_BLE_LEFT,
                              CONTROLLER_BUTTON_TOP, CONTROLLER_BLE_WIDTH,
                              CONTROLLER_BUTTON_HEIGHT)) {
                return CONTROLLER_BLE_CONTROL;
            }
            if (controller_action_enabled(shell, 1U) &&
                point_in_rect(gui_x, gui_y, CONTROLLER_PAIR_LEFT,
                              CONTROLLER_BUTTON_TOP, CONTROLLER_PAIR_WIDTH,
                              CONTROLLER_BUTTON_HEIGHT)) {
                return CONTROLLER_PAIR_CONTROL;
            }
            if (controller_action_enabled(shell, 2U) &&
                point_in_rect(gui_x, gui_y, CONTROLLER_DISCONNECT_LEFT,
                              CONTROLLER_BUTTON_TOP,
                              CONTROLLER_DISCONNECT_WIDTH,
                              CONTROLLER_BUTTON_HEIGHT)) {
                return CONTROLLER_DISCONNECT_CONTROL;
            }
            if (controller_action_enabled(shell, 3U) &&
                point_in_rect(gui_x, gui_y, CONTROLLER_MAP_LEFT,
                              CONTROLLER_BUTTON_TOP, CONTROLLER_MAP_WIDTH,
                              CONTROLLER_BUTTON_HEIGHT)) {
                return CONTROLLER_MAP_CONTROL;
            }
            if (controller_action_enabled(shell, 4U) &&
                point_in_rect(gui_x, gui_y, CONTROLLER_RESET_LEFT,
                              CONTROLLER_BUTTON_TOP, CONTROLLER_RESET_WIDTH,
                              CONTROLLER_BUTTON_HEIGHT)) {
                return CONTROLLER_RESET_CONTROL;
            }
            if (controller_action_enabled(shell, 5U) &&
                point_in_rect(gui_x, gui_y, CONTROLLER_FORGET_LEFT,
                              CONTROLLER_BUTTON_TOP,
                              CONTROLLER_FORGET_WIDTH,
                              CONTROLLER_BUTTON_HEIGHT)) {
                return CONTROLLER_FORGET_CONTROL;
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
            if (shell->multiplayer_view ==
                    CONSOLE_MULTIPLAYER_VIEW_ROLE) {
                if (point_in_rect(
                        gui_x, gui_y, MULTIPLAYER_ROLE_HOST_LEFT,
                        MULTIPLAYER_ROLE_TOP, MULTIPLAYER_ROLE_WIDTH,
                        MULTIPLAYER_ROLE_HEIGHT)) {
                    return MULTIPLAYER_ROLE_HOST_CONTROL;
                }
                if (point_in_rect(
                        gui_x, gui_y, MULTIPLAYER_ROLE_JOIN_LEFT,
                        MULTIPLAYER_ROLE_TOP, MULTIPLAYER_ROLE_WIDTH,
                        MULTIPLAYER_ROLE_HEIGHT)) {
                    return MULTIPLAYER_ROLE_JOIN_CONTROL;
                }
                return SIZE_MAX;
            }
            if (shell->multiplayer_view ==
                    CONSOLE_MULTIPLAYER_VIEW_HOST) {
                if (shell->runtime.multiplayer_settings_editable &&
                    point_in_rect(
                        gui_x, gui_y, MULTIPLAYER_SIMPLE_GAME_LEFT,
                        MULTIPLAYER_SIMPLE_GAME_TOP,
                        MULTIPLAYER_SIMPLE_GAME_WIDTH,
                        MULTIPLAYER_SIMPLE_GAME_HEIGHT)) {
                    return MULTIPLAYER_OPTION_PLUS_CONTROL_BASE +
                        CONSOLE_MULTIPLAYER_OPTION_GAME;
                }
                if (point_in_rect(
                        gui_x, gui_y, MULTIPLAYER_SIMPLE_SETTINGS_LEFT,
                        MULTIPLAYER_SIMPLE_SETTINGS_TOP,
                        MULTIPLAYER_SIMPLE_SETTINGS_WIDTH,
                        MULTIPLAYER_SIMPLE_SETTINGS_HEIGHT)) {
                    return MULTIPLAYER_SETTINGS_CONTROL;
                }
            }
            const bool inline_lobby_panel =
                multiplayer_uses_inline_lobby_panel(shell);
            if (shell->multiplayer_view !=
                    CONSOLE_MULTIPLAYER_VIEW_HOST &&
                shell->runtime.multiplayer_settings_editable) {
                for (size_t option = 0U;
                     option < CONSOLE_MULTIPLAYER_OPTION_COUNT; ++option) {
                    if (shell->multiplayer_view ==
                            CONSOLE_MULTIPLAYER_VIEW_JOIN &&
                        option != CONSOLE_MULTIPLAYER_OPTION_TRANSPORT) {
                        continue;
                    }
                    if (shell->runtime.multiplayer_game_is_arena &&
                        option != CONSOLE_MULTIPLAYER_OPTION_GAME &&
                        option != CONSOLE_MULTIPLAYER_OPTION_MAP &&
                        option != CONSOLE_MULTIPLAYER_OPTION_TRANSPORT) continue;
                    if (option == CONSOLE_MULTIPLAYER_OPTION_DICE &&
                        (!shell->runtime.multiplayer_dice_available ||
                         shell->runtime.multiplayer_game_is_doom)) continue;
                    if (!shell->runtime.multiplayer_game_is_doom &&
                        !(option == CONSOLE_MULTIPLAYER_OPTION_DICE &&
                          shell->runtime.multiplayer_dice_available) &&
                        option != CONSOLE_MULTIPLAYER_OPTION_GAME &&
                        option != CONSOLE_MULTIPLAYER_OPTION_TRANSPORT &&
                        option != CONSOLE_MULTIPLAYER_OPTION_LOBBY) {
                        continue;
                    }
                    const multiplayer_option_layout_t layout =
                        multiplayer_option_layout(
                            inline_lobby_panel,
                            (console_multiplayer_option_t)option);
                    if (!point_in_rect(
                            gui_x, gui_y, layout.left, layout.top,
                            layout.width, layout.height)) {
                        continue;
                    }
                    return gui_x < layout.left + layout.width / 2U
                        ? MULTIPLAYER_OPTION_MINUS_CONTROL_BASE + option
                        : MULTIPLAYER_OPTION_PLUS_CONTROL_BASE + option;
                }
            }
            if (shell->multiplayer_view ==
                    CONSOLE_MULTIPLAYER_VIEW_JOIN) {
                const size_t count =
                    shell->runtime.multiplayer_lobby_count <
                            CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX
                        ? shell->runtime.multiplayer_lobby_count
                        : CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX;
                for (size_t index = 0U; index < count; ++index) {
                    if (point_in_rect(
                            gui_x, gui_y, MULTIPLAYER_OPTION_LEFT,
                            MULTIPLAYER_JOIN_ROOMS_TOP +
                                (unsigned)index *
                                    MULTIPLAYER_JOIN_ROOM_HEIGHT,
                            MULTIPLAYER_OPTION_WIDTH,
                            MULTIPLAYER_JOIN_ROOM_HEIGHT - 1U)) {
                        return MULTIPLAYER_JOIN_ROOM_CONTROL_BASE + index;
                    }
                }
            }
            const multiplayer_option_layout_t launch_layout =
                multiplayer_launch_layout(inline_lobby_panel);
            if (point_in_rect(
                    gui_x, gui_y, launch_layout.left, launch_layout.top,
                    launch_layout.width, launch_layout.height)) {
                if (shell->multiplayer_view ==
                        CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS) {
                    return MULTIPLAYER_LAUNCH_CONTROL;
                }
                const bool primary_enabled =
                    shell->multiplayer_view ==
                            CONSOLE_MULTIPLAYER_VIEW_HOST
                        ? (shell->runtime.multiplayer_can_start ||
                           (shell->runtime.multiplayer_lobby_phase ==
                                CONSOLE_MULTIPLAYER_LOBBY_BROWSING &&
                            shell->runtime
                                .multiplayer_lobby_action_enabled))
                        : (shell->runtime.multiplayer_lobby_phase ==
                                CONSOLE_MULTIPLAYER_LOBBY_BROWSING &&
                           shell->runtime.multiplayer_lobby_action_enabled &&
                           shell->runtime.multiplayer_lobby_selection > 0U);
                if (primary_enabled &&
                    shell->runtime.multiplayer_game_ready) {
                    return MULTIPLAYER_LAUNCH_CONTROL;
                }
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

        /* The thumb is a first-class control.  Keep its hit box at least as
         * wide as the track so a finger need not land on its narrow face. */
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
        const int32_t maximum_q16 = home_scroll_row_q16(
            home_max_scroll_row(shell));
        const int32_t visual_q16 = clamp_home_scroll_q16(
            shell, shell->home_scroll_visual_q16);
        const int thumb_top = maximum_q16 == 0 ? inner_top : inner_top +
            (int)((int64_t)travel * visual_q16 / maximum_q16);
        if (point_in_rect(gui_x, gui_y, SCROLL_LEFT, (unsigned)thumb_top,
                          SCROLL_WIDTH, (unsigned)thumb_height)) {
            return SCROLL_THUMB_CONTROL;
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
    sync_home_scroll_visual(shell);
    shell->selected_home_item = 0U;
    select_first_visible_item(shell);
    shell->dirty = true;
}

static void open_home_folder(console_shell_t *shell, const char *segment)
{
    if (shell->runtime.control_panel_enabled && strcmp(segment, "SYSTEM") == 0) {
        panel_select_section(shell, 0U);
        panel_open(shell);
        return;
    }
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

static console_shell_action_t activate_app_index(console_shell_t *shell, size_t app_index)
{
    if (app_index >= shell->app_count || !shell->apps[app_index].enabled) return no_action();
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
        shell->multiplayer_view = CONSOLE_MULTIPLAYER_VIEW_ROLE;
        shell->multiplayer_role_selection = 0U;
        shell->multiplayer_selected_row =
            CONSOLE_MULTIPLAYER_OPTION_COUNT;
    }
    if (app->page == CONSOLE_PAGE_STORAGE) {
        reset_storage_controls(shell);
    }
    if (app->page == CONSOLE_PAGE_CONTROLLERS) {
        reset_controller_controls(shell);
    }
    if (app->page == CONSOLE_PAGE_FILES ||
        app->page == CONSOLE_PAGE_GAMES) {
        shell->file_delete_confirm = false;
        shell->file_notice = CONSOLE_FILE_NOTICE_NONE;
        normalize_file_selection(shell);
    }
    return page_changed(app->id);
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
        open_home_folder(shell, items[item_index].path_segment);
        return page_changed(0U);
    }
    if (items[item_index].app_index >= shell->app_count) {
        return no_action();
    }
    return activate_app_index(shell, items[item_index].app_index);
}


static console_shell_action_t panel_action(console_shell_t *shell, size_t control)
{
    if (control >= PANEL_NAV_BASE && control < PANEL_NAV_BASE + PANEL_SECTION_COUNT) {
        panel_select_section(shell, (unsigned)(control - PANEL_NAV_BASE));
        return page_changed(0U);
    }
    if (shell->control_panel_section == 1U && control >= PANEL_VOLUME_BASE && control < PANEL_VOLUME_BASE + 4U)
        return audio_volume_action(shell, control - PANEL_VOLUME_BASE < 2U,
                                   ((control - PANEL_VOLUME_BASE) % 2U) == 0U ? -1 : 1);
    size_t indexes[CONSOLE_SHELL_MAX_APPS];
    const size_t count = panel_items(shell, indexes);
    if (control == PANEL_PREVIOUS && shell->control_panel_first >= PANEL_VISIBLE_ROWS)
        shell->control_panel_first -= PANEL_VISIBLE_ROWS;
    else if (control == PANEL_NEXT && shell->control_panel_first + PANEL_VISIBLE_ROWS < count)
        shell->control_panel_first += PANEL_VISIBLE_ROWS;
    else if (control >= PANEL_ROW_BASE && control < PANEL_ROW_BASE + PANEL_VISIBLE_ROWS) {
        const size_t index = shell->control_panel_first + control - PANEL_ROW_BASE;
        if (index < count) return activate_app_index(shell, indexes[index]);
        return no_action();
    } else return no_action();
    shell->control_panel_selection = 0U;
    shell->dirty = true;
    return page_changed(0U);
}
static console_shell_action_t panel_buttons(console_shell_t *shell, uint32_t pressed)
{
    size_t indexes[CONSOLE_SHELL_MAX_APPS];
    const size_t count = panel_items(shell, indexes);
    const size_t actions = shell->control_panel_section == 1U ? 4U + (count ? 1U : 0U) : count;
    if (pressed & CONSOLE_BUTTON_LEFT) shell->control_panel_row_focus = false;
    else if (pressed & CONSOLE_BUTTON_RIGHT) shell->control_panel_row_focus = actions != 0U;
    else if (pressed & (CONSOLE_BUTTON_UP | CONSOLE_BUTTON_DOWN)) {
        if (!shell->control_panel_row_focus) {
            const unsigned next = (shell->control_panel_section + ((pressed & CONSOLE_BUTTON_UP) ? PANEL_SECTION_COUNT - 1U : 1U)) % PANEL_SECTION_COUNT;
            panel_select_section(shell, next);
        } else if (actions) {
            shell->control_panel_selection = (shell->control_panel_selection + ((pressed & CONSOLE_BUTTON_UP) ? actions - 1U : 1U)) % actions;
            if (shell->control_panel_section != 1U)
                shell->control_panel_first = shell->control_panel_selection / PANEL_VISIBLE_ROWS * PANEL_VISIBLE_ROWS;
        }
    } else if (pressed & CONSOLE_BUTTON_ACCEPT) {
        if (!shell->control_panel_row_focus) shell->control_panel_row_focus = actions != 0U;
        else if (actions) {
            const size_t selected = shell->control_panel_selection < actions ? shell->control_panel_selection : 0U;
            return panel_action(shell, shell->control_panel_section == 1U
                ? (selected < 4U ? PANEL_VOLUME_BASE + selected : PANEL_ROW_BASE)
                : PANEL_ROW_BASE + selected % PANEL_VISIBLE_ROWS);
        }
    }
    shell->dirty = true;
    return no_action();
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
        (void)set_home_scroll_row_internal(shell, target_row, false);
    } else if (target_row >= shell->home_scroll_row + visible_rows) {
        (void)set_home_scroll_row_internal(
            shell, target_row - (visible_rows - 1U), false);
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

#if CONFIG_P4_BOARD_M5STACK_TAB5
    return nextgen_buttons(shell, pressed);
#endif
    if ((pressed & CONSOLE_BUTTON_BACK) != 0U) {
        if (shell->page == CONSOLE_PAGE_HOME) {
            if (shell->home_all_programs ||
                shell->home_folder_path[0] != '\0') {
                navigate_home_up(shell);
                return page_changed(0U);
            }
            return no_action();
        }
        if (shell->page == CONSOLE_PAGE_MULTIPLAYER &&
            shell->multiplayer_view != CONSOLE_MULTIPLAYER_VIEW_ROLE) {
            return multiplayer_back_action(shell);
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
        if (shell->page == CONSOLE_PAGE_CONTROL_PANEL) shell->control_panel_active = false;
        console_shell_show_home(shell);
        return page_changed(0U);
    }

    if (shell->page == CONSOLE_PAGE_CONTROL_PANEL) return panel_buttons(shell, pressed);

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
            (void)set_home_scroll_row_internal(
                shell, maximum_scroll, false);
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

    if (shell->page == CONSOLE_PAGE_CONTROLLERS) {
        if ((pressed & CONSOLE_BUTTON_REFRESH) != 0U) {
            return controller_action(
                shell, shell->runtime.ble_controller_enabled ? 1U : 0U);
        }
        if ((pressed & CONSOLE_BUTTON_LEFT) != 0U) {
            for (size_t step = 0U; step < 6U; ++step) {
                shell->controller_selected_action =
                    (shell->controller_selected_action + 5U) % 6U;
                if (controller_action_enabled(
                        shell, shell->controller_selected_action)) {
                    break;
                }
            }
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if ((pressed & CONSOLE_BUTTON_RIGHT) != 0U) {
            for (size_t step = 0U; step < 6U; ++step) {
                shell->controller_selected_action =
                    (shell->controller_selected_action + 1U) % 6U;
                if (controller_action_enabled(
                        shell, shell->controller_selected_action)) {
                    break;
                }
            }
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if ((pressed & CONSOLE_BUTTON_ACCEPT) != 0U) {
            return controller_action(
                shell, shell->controller_selected_action);
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
        if (shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_ROLE) {
            if ((pressed & (CONSOLE_BUTTON_LEFT | CONSOLE_BUTTON_UP)) !=
                0U) {
                shell->multiplayer_role_selection = 0U;
                shell->dirty = true;
                return page_changed(shell->active_app_id);
            }
            if ((pressed & (CONSOLE_BUTTON_RIGHT | CONSOLE_BUTTON_DOWN)) !=
                0U) {
                shell->multiplayer_role_selection = 1U;
                shell->dirty = true;
                return page_changed(shell->active_app_id);
            }
            if ((pressed & CONSOLE_BUTTON_ACCEPT) != 0U) {
                return enter_multiplayer_view(
                    shell, shell->multiplayer_role_selection == 0U
                        ? CONSOLE_MULTIPLAYER_VIEW_HOST
                        : CONSOLE_MULTIPLAYER_VIEW_JOIN);
            }
            return no_action();
        }
        if ((pressed & CONSOLE_BUTTON_UP) != 0U) {
            move_multiplayer_selection(shell, -1);
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if ((pressed & CONSOLE_BUTTON_DOWN) != 0U) {
            move_multiplayer_selection(shell, 1);
            shell->dirty = true;
            return page_changed(shell->active_app_id);
        }
        if (shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST &&
            shell->multiplayer_selected_row ==
                MULTIPLAYER_HOST_SETTINGS_ROW &&
            (pressed & CONSOLE_BUTTON_ACCEPT) != 0U) {
            return enter_multiplayer_host_settings(shell);
        }
        if (shell->multiplayer_view ==
                CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS &&
            shell->multiplayer_selected_row ==
                CONSOLE_MULTIPLAYER_OPTION_COUNT &&
            (pressed & CONSOLE_BUTTON_ACCEPT) != 0U) {
            return return_to_multiplayer_host(shell);
        }
        if (shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_JOIN &&
            shell->multiplayer_selected_row ==
                CONSOLE_MULTIPLAYER_OPTION_LOBBY) {
            if ((pressed & (CONSOLE_BUTTON_LEFT |
                            CONSOLE_BUTTON_RIGHT)) != 0U) {
                return multiplayer_config_action(
                    shell, CONSOLE_MULTIPLAYER_OPTION_LOBBY,
                    (pressed & CONSOLE_BUTTON_LEFT) != 0U ? -1 : 1);
            }
            if ((pressed & CONSOLE_BUTTON_ACCEPT) != 0U) {
                return multiplayer_primary_action(shell);
            }
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
            (pressed & CONSOLE_BUTTON_ACCEPT) != 0U) {
            return multiplayer_primary_action(shell);
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

static int home_drag_row_pixels(const console_shell_t *shell)
{
#if CONSOLE_SHELL_NATIVE_BBS
    if (use_bbs_launcher(shell)) {
        return BBS_DOOR_PITCH_PIXELS * CONSOLE_SHELL_LAYOUT_HEIGHT /
            CONSOLE_SHELL_HEIGHT;
    }
#else
    (void)shell;
#endif
    return SCROLL_TOUCH_ROW_PIXELS;
}

static int home_drag_row_physical_pixels(const console_shell_t *shell)
{
    const int logical_row_pixels = home_drag_row_pixels(shell);
    const int viewport_height = (int)CONSOLE_SHELL_VIEWPORT_HEIGHT;
    const int layout_height = (int)CONSOLE_SHELL_LAYOUT_HEIGHT;
    if (logical_row_pixels <= 0 || viewport_height <= 0 ||
        layout_height <= 0) {
        return 1;
    }
    /* Waveshare's 40 logical-pixel row distance is 96 physical pixels
     * (40 * 480 / 200); retain the geometry calculation for other targets. */
    const int64_t scaled = (int64_t)logical_row_pixels * viewport_height;
    const int physical = (int)((scaled + layout_height / 2) / layout_height);
    return physical > 0 ? physical : 1;
}

static void settle_interrupted_home_drag(console_shell_t *shell)
{
    if (shell->home_scroll_visual_q16 !=
            home_scroll_row_q16(shell->home_scroll_row)) {
        (void)set_home_scroll_row_internal(
            shell, shell->home_scroll_row, false);
    }
    shell->home_drag_velocity_q16_per_ms = 0;
}

static bool update_home_drag(console_shell_t *shell,
                             uint16_t gui_x,
                             uint16_t gui_y,
                             uint16_t physical_x,
                             uint16_t physical_y)
{
    if (!shell->scroll_candidate || shell->page != CONSOLE_PAGE_HOME ||
        home_max_scroll_row(shell) == 0U) {
        return false;
    }
    (void)gui_x;
    (void)gui_y;
    if (shell->pressed_index == SCROLL_THUMB_CONTROL) {
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
        if (travel <= 0) {
            return false;
        }
        const int offset = (int)gui_y - inner_top -
            shell->scroll_thumb_grab_offset;
        const int32_t maximum_q16 = home_scroll_row_q16(
            home_max_scroll_row(shell));
        int32_t bounded_q16 = offset <= 0 ? 0 : (int32_t)(
            (int64_t)offset * maximum_q16 / travel);
        bounded_q16 = clamp_home_scroll_q16(shell, bounded_q16);
        const int32_t previous_q16 = shell->home_scroll_visual_q16;
        const uint32_t sample_ms = shell->animation_clock_ms;
        const uint32_t delta_ms = sample_ms - shell->home_drag_sample_ms;
        if (delta_ms > 0U && delta_ms <= SCROLL_VELOCITY_MAX_SAMPLE_MS &&
            bounded_q16 != previous_q16) {
            const int32_t sample_velocity = (int32_t)(
                ((int64_t)bounded_q16 - previous_q16) / delta_ms);
            shell->home_drag_velocity_q16_per_ms = (int32_t)(
                ((int64_t)shell->home_drag_velocity_q16_per_ms * 2 +
                 sample_velocity) / 3);
        } else if (delta_ms > SCROLL_VELOCITY_MAX_SAMPLE_MS ||
                   bounded_q16 == previous_q16) {
            shell->home_drag_velocity_q16_per_ms = 0;
        }
        shell->home_drag_sample_ms = sample_ms;
        shell->home_scroll_visual_q16 = bounded_q16;
        shell->home_scroll_from_q16 = bounded_q16;
        shell->home_scroll_elapsed_ms = 0U;
        shell->home_scroll_duration_ms = 0U;
        const bool was_pressed = shell->press_active;
        shell->scroll_gesture = true;
        shell->press_active = false;
        /* Keep ownership latched for the remainder of the contact.  The
         * thumb moves under the finger, so a later sample will usually no
         * longer hit its original geometry; clearing this would incorrectly
         * hand the contact to the tile-drag path. */
        if (was_pressed || bounded_q16 != previous_q16) {
            shell->dirty = true;
        }
        return true;
    }

    const int vertical = (int)shell->press_start_physical_y -
        (int)physical_y;
    const int horizontal = (int)shell->press_start_physical_x -
        (int)physical_x;
    const int vertical_magnitude = vertical < 0 ? -vertical : vertical;
    const int horizontal_magnitude = horizontal < 0 ? -horizontal : horizontal;
    if (!shell->scroll_gesture &&
        (vertical_magnitude < SCROLL_PHYSICAL_DRAG_THRESHOLD ||
         vertical_magnitude < horizontal_magnitude)) {
        return false;
    }

    /* The threshold only distinguishes a tap from a drag.  Once crossed,
     * preserve the complete displacement so the first visible frame moves
     * with the finger instead of retaining a second dead-zone. */
    const int effective_vertical = vertical;
    const int row_pixels = home_drag_row_physical_pixels(shell);
    const int64_t drag_q16 =
        (int64_t)effective_vertical * SCROLL_POSITION_ONE / row_pixels;
    const int32_t previous_q16 = shell->home_scroll_visual_q16;
    const int64_t requested_q16 =
        (int64_t)shell->press_start_scroll_q16 + drag_q16;
    int32_t bounded_q16 = 0;
    if (requested_q16 > INT32_MAX) {
        bounded_q16 = INT32_MAX;
    } else if (requested_q16 > 0) {
        bounded_q16 = (int32_t)requested_q16;
    }
    bounded_q16 = clamp_home_scroll_q16(shell, bounded_q16);

    const uint32_t sample_ms = shell->animation_clock_ms;
    const uint32_t delta_ms = sample_ms - shell->home_drag_sample_ms;
    if (delta_ms > 0U && delta_ms <= SCROLL_VELOCITY_MAX_SAMPLE_MS &&
        bounded_q16 != previous_q16) {
        const int32_t sample_velocity = (int32_t)(
            ((int64_t)bounded_q16 - previous_q16) / delta_ms);
        shell->home_drag_velocity_q16_per_ms = (int32_t)(
            ((int64_t)shell->home_drag_velocity_q16_per_ms * 2 +
             sample_velocity) / 3);
    } else if (delta_ms > SCROLL_VELOCITY_MAX_SAMPLE_MS ||
               bounded_q16 == previous_q16) {
        /* A hold or service stall must not reuse an old fast sample and
         * produce a surprise fling when the finger is finally released. */
        shell->home_drag_velocity_q16_per_ms = 0;
    }
    shell->home_drag_sample_ms = sample_ms;
    shell->home_scroll_visual_q16 = bounded_q16;
    shell->home_scroll_from_q16 = bounded_q16;
    shell->home_scroll_elapsed_ms = 0U;
    shell->home_scroll_duration_ms = 0U;

    const bool was_pressed = shell->press_active;
    shell->scroll_gesture = true;
    shell->press_active = false;
    shell->pressed_index = SIZE_MAX;
    if (was_pressed || bounded_q16 != previous_q16) {
        shell->dirty = true;
    }
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
#if CONFIG_P4_BOARD_M5STACK_TAB5
    return nextgen_touch(shell, valid, contacts, contact_count);
#endif
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
        settle_interrupted_home_drag(shell);
        return no_action();
    }

    if (contact_count == 0U) {
        if (!shell->contact_down) {
            return no_action();
        }
        shell->contact_down = false;
        shell->scroll_candidate = false;
        if (shell->scroll_gesture) {
            int64_t projection =
                (int64_t)shell->home_drag_velocity_q16_per_ms *
                SCROLL_FLING_PROJECTION_MS;
            if (projection > SCROLL_FLING_MAX_Q16) {
                projection = SCROLL_FLING_MAX_Q16;
            } else if (projection < -SCROLL_FLING_MAX_Q16) {
                projection = -SCROLL_FLING_MAX_Q16;
            }
            int64_t projected_q16 =
                (int64_t)shell->home_scroll_visual_q16 + projection;
            if (projected_q16 < 0) {
                projected_q16 = 0;
            }
            const int32_t bounded_q16 = clamp_home_scroll_q16(
                shell,
                projected_q16 > INT32_MAX
                    ? INT32_MAX : (int32_t)projected_q16);
            const size_t target_row = (size_t)(
                (bounded_q16 + SCROLL_POSITION_ONE / 2) /
                SCROLL_POSITION_ONE);
            const bool changed =
                target_row != shell->press_start_scroll_row;
            shell->scroll_gesture = false;
            shell->press_active = false;
            shell->pressed_index = SIZE_MAX;
            shell->home_drag_velocity_q16_per_ms = 0;
            (void)set_home_scroll_row(shell, target_row);
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
            if (shell->page == CONSOLE_PAGE_MULTIPLAYER &&
                shell->multiplayer_view !=
                    CONSOLE_MULTIPLAYER_VIEW_ROLE) {
                return multiplayer_back_action(shell);
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
            if (shell->page == CONSOLE_PAGE_CONTROL_PANEL) shell->control_panel_active = false;
            console_shell_show_home(shell);
            return page_changed(0U);
        }
        if (shell->page == CONSOLE_PAGE_CONTROL_PANEL) return panel_action(shell, released_control);
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
                (void)set_home_scroll_row_internal(
                    shell, maximum_scroll, false);
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
        if (shell->page == CONSOLE_PAGE_CONTROLLERS) {
            switch (released_control) {
            case CONTROLLER_BLE_CONTROL:
                return controller_action(shell, 0U);
            case CONTROLLER_PAIR_CONTROL:
                return controller_action(shell, 1U);
            case CONTROLLER_DISCONNECT_CONTROL:
                return controller_action(shell, 2U);
            case CONTROLLER_MAP_CONTROL:
                return controller_action(shell, 3U);
            case CONTROLLER_RESET_CONTROL:
                return controller_action(shell, 4U);
            case CONTROLLER_FORGET_CONTROL:
                return controller_action(shell, 5U);
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
            if (released_control == MULTIPLAYER_ROLE_HOST_CONTROL) {
                return enter_multiplayer_view(
                    shell, CONSOLE_MULTIPLAYER_VIEW_HOST);
            }
            if (released_control == MULTIPLAYER_ROLE_JOIN_CONTROL) {
                return enter_multiplayer_view(
                    shell, CONSOLE_MULTIPLAYER_VIEW_JOIN);
            }
            if (released_control == MULTIPLAYER_SETTINGS_CONTROL) {
                return enter_multiplayer_host_settings(shell);
            }
            if (released_control >=
                    MULTIPLAYER_JOIN_ROOM_CONTROL_BASE &&
                released_control <
                    MULTIPLAYER_JOIN_ROOM_CONTROL_LIMIT) {
                return multiplayer_lobby_select_action(
                    shell,
                    (uint8_t)(released_control -
                        MULTIPLAYER_JOIN_ROOM_CONTROL_BASE + 1U));
            }
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
                shell->multiplayer_view ==
                    CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS) {
                return return_to_multiplayer_host(shell);
            }
            if (released_control == MULTIPLAYER_LAUNCH_CONTROL &&
                shell->runtime.multiplayer_game_ready) {
                shell->multiplayer_selected_row =
                    CONSOLE_MULTIPLAYER_OPTION_COUNT;
                return multiplayer_primary_action(shell);
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
            open_home_folder(shell, items[item_index].path_segment);
            return page_changed(0U);
        }
        if (items[item_index].app_index >= shell->app_count) {
            return no_action();
        }

        return activate_app_index(shell, items[item_index].app_index);
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
        settle_interrupted_home_drag(shell);
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
        settle_interrupted_home_drag(shell);
        return no_action();
    }

    const size_t control = control_at(shell, gui_x, gui_y);
    if (!shell->contact_down) {
        shell->contact_down = true;
        shell->press_start_gui_x = gui_x;
        shell->press_start_gui_y = gui_y;
        shell->press_start_physical_x = contacts[0].x;
        shell->press_start_physical_y = contacts[0].y;
        shell->press_start_scroll_row = shell->home_scroll_row;
        shell->press_start_scroll_q16 =
            shell->home_scroll_visual_q16;
        shell->home_drag_sample_ms = shell->animation_clock_ms;
        shell->home_drag_velocity_q16_per_ms = 0;
        shell->scroll_candidate = shell->page == CONSOLE_PAGE_HOME &&
            home_max_scroll_row(shell) > 0U &&
            (point_in_rect(gui_x, gui_y, TILE_LEFT, TILE_TOP,
                           GRID_WIDTH, GRID_HEIGHT) ||
             control == SCROLL_THUMB_CONTROL);
        shell->scroll_thumb_grab_offset = 0;
        if (control == SCROLL_THUMB_CONTROL) {
            const int inner_top = SCROLL_TRACK_TOP + 2;
            const size_t rows = home_row_count(shell);
            int thumb_height = SCROLL_TRACK_HEIGHT - 4;
            if (rows > CONSOLE_SHELL_VISIBLE_APP_ROWS) {
                thumb_height = (int)((size_t)(SCROLL_TRACK_HEIGHT - 4) *
                    CONSOLE_SHELL_VISIBLE_APP_ROWS / rows);
                if (thumb_height < 12) {
                    thumb_height = 12;
                }
            }
            const int travel = (SCROLL_TRACK_HEIGHT - 4) - thumb_height;
            const int32_t maximum_q16 = home_scroll_row_q16(
                home_max_scroll_row(shell));
            const int32_t visual_q16 = clamp_home_scroll_q16(
                shell, shell->home_scroll_visual_q16);
            const int thumb_top = maximum_q16 == 0 ? inner_top : inner_top +
                (int)((int64_t)travel * visual_q16 / maximum_q16);
            shell->scroll_thumb_grab_offset = (int16_t)(
                (int)gui_y - thumb_top);
        }
        shell->scroll_gesture = false;
        shell->pressed_index = control;
        shell->press_active = shell->pressed_index != SIZE_MAX;
        /* A Windows-home contact may become a scroll.  Keep the cached
         * unpressed frame until that intent is known; this avoids spending a
         * full native redraw on a press highlight that is immediately
         * discarded by the first drag sample.  Non-scrollable controls keep
         * their immediate press feedback. */
        if (shell->press_active && !shell->scroll_candidate) {
            shell->dirty = true;
        }
        return no_action();
    }

    if (update_home_drag(shell, gui_x, gui_y,
                         contacts[0].x, contacts[0].y)) {
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
        memcmp(shell->runtime.node_name, runtime->node_name,
               sizeof(runtime->node_name)) != 0 ||
        shell->runtime.internal_free_kib != runtime->internal_free_kib ||
        shell->runtime.psram_free_kib != runtime->psram_free_kib ||
        shell->runtime.game_storage_kib != runtime->game_storage_kib ||
        shell->runtime.game_storage_state != runtime->game_storage_state ||
        shell->runtime.board_kind != runtime->board_kind ||
        shell->runtime.touch_ready != runtime->touch_ready ||
        shell->runtime.controller_ready != runtime->controller_ready ||
        shell->runtime.controller_transport !=
            runtime->controller_transport ||
        shell->runtime.ble_controller_supported !=
            runtime->ble_controller_supported ||
        shell->runtime.ble_controller_enabled !=
            runtime->ble_controller_enabled ||
        shell->runtime.ble_controller_host_ready !=
            runtime->ble_controller_host_ready ||
        shell->runtime.ble_controller_bonded !=
            runtime->ble_controller_bonded ||
        shell->runtime.ble_controller_connected !=
            runtime->ble_controller_connected ||
        shell->runtime.ble_controller_encrypted !=
            runtime->ble_controller_encrypted ||
        shell->runtime.ble_controller_busy !=
            runtime->ble_controller_busy ||
        shell->runtime.ble_controller_rssi !=
            runtime->ble_controller_rssi ||
        shell->runtime.ble_controller_reports_received !=
            runtime->ble_controller_reports_received ||
        shell->runtime.ble_controller_reports_dropped !=
            runtime->ble_controller_reports_dropped ||
        shell->runtime.ble_controller_last_error !=
            runtime->ble_controller_last_error ||
        memcmp(shell->runtime.ble_controller_name,
               runtime->ble_controller_name,
               sizeof(runtime->ble_controller_name)) != 0 ||
        shell->runtime.ble_controller_multiplayer_ready !=
            runtime->ble_controller_multiplayer_ready ||
        shell->runtime.controller_mapping_active !=
            runtime->controller_mapping_active ||
        shell->runtime.controller_mapping_persistent !=
            runtime->controller_mapping_persistent ||
        shell->runtime.controller_mapping_target !=
            runtime->controller_mapping_target ||
        memcmp(shell->runtime.controller_mapping,
               runtime->controller_mapping,
               sizeof(runtime->controller_mapping)) != 0 ||
        shell->runtime.controller_mapping_last_error !=
            runtime->controller_mapping_last_error ||
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
        shell->runtime.file_transfer_ready !=
            runtime->file_transfer_ready ||
        shell->runtime.file_transfer_busy !=
            runtime->file_transfer_busy ||
        shell->runtime.file_transfer_state !=
            runtime->file_transfer_state ||
        shell->runtime.file_transfer_direction !=
            runtime->file_transfer_direction ||
        shell->runtime.file_transfer_class !=
            runtime->file_transfer_class ||
        shell->runtime.file_transfer_progress_percent !=
            runtime->file_transfer_progress_percent ||
        shell->runtime.file_transfer_last_status !=
            runtime->file_transfer_last_status ||
        shell->runtime.file_transfer_bytes !=
            runtime->file_transfer_bytes ||
        shell->runtime.file_transfer_total_bytes !=
            runtime->file_transfer_total_bytes ||
        shell->runtime.file_transfer_generation !=
            runtime->file_transfer_generation ||
        memcmp(shell->runtime.file_transfer_name,
               runtime->file_transfer_name,
               sizeof(runtime->file_transfer_name)) != 0 ||
        shell->runtime.content_validation_running !=
            runtime->content_validation_running ||
        shell->runtime.content_validation_complete !=
            runtime->content_validation_complete ||
        shell->runtime.content_validation_progress_percent !=
            runtime->content_validation_progress_percent ||
        shell->runtime.multiplayer_core_ready !=
            runtime->multiplayer_core_ready ||
        shell->runtime.multiplayer_transport_ready !=
            runtime->multiplayer_transport_ready ||
        shell->runtime.multiplayer_transport_starting !=
            runtime->multiplayer_transport_starting ||
        shell->runtime.multiplayer_transport_encrypted !=
            runtime->multiplayer_transport_encrypted ||
        shell->runtime.multiplayer_transport_kind !=
            runtime->multiplayer_transport_kind ||
        shell->runtime.multiplayer_peer_seen !=
            runtime->multiplayer_peer_seen ||
        shell->runtime.multiplayer_lobby_ready !=
            runtime->multiplayer_lobby_ready ||
        shell->runtime.multiplayer_lobby_is_host !=
            runtime->multiplayer_lobby_is_host ||
        shell->runtime.multiplayer_lobby_scanning !=
            runtime->multiplayer_lobby_scanning ||
        shell->runtime.multiplayer_lobby_action_enabled !=
            runtime->multiplayer_lobby_action_enabled ||
        shell->runtime.multiplayer_can_start !=
            runtime->multiplayer_can_start ||
        shell->runtime.multiplayer_launch_syncing !=
            runtime->multiplayer_launch_syncing ||
        shell->runtime.multiplayer_settings_editable !=
            runtime->multiplayer_settings_editable ||
        shell->runtime.multiplayer_game_ready !=
            runtime->multiplayer_game_ready ||
        shell->runtime.multiplayer_game_is_doom !=
            runtime->multiplayer_game_is_doom ||
        shell->runtime.multiplayer_game_is_arena != runtime->multiplayer_game_is_arena ||
        shell->runtime.multiplayer_dice_available != runtime->multiplayer_dice_available ||
        shell->runtime.multiplayer_dice_enabled != runtime->multiplayer_dice_enabled ||
        shell->runtime.multiplayer_game_selection !=
            runtime->multiplayer_game_selection ||
        shell->runtime.multiplayer_game_count !=
            runtime->multiplayer_game_count ||
        memcmp(shell->runtime.multiplayer_game_title,
               runtime->multiplayer_game_title,
               sizeof(runtime->multiplayer_game_title)) != 0 ||
        shell->runtime.multiplayer_lobby_phase !=
            runtime->multiplayer_lobby_phase ||
        shell->runtime.multiplayer_lobby_selection !=
            runtime->multiplayer_lobby_selection ||
        shell->runtime.multiplayer_lobby_count !=
            runtime->multiplayer_lobby_count ||
        shell->runtime.multiplayer_lobby_rssi !=
            runtime->multiplayer_lobby_rssi ||
        shell->runtime.multiplayer_lobby_session_id !=
            runtime->multiplayer_lobby_session_id ||
        memcmp(shell->runtime.multiplayer_lobbies,
               runtime->multiplayer_lobbies,
               sizeof(runtime->multiplayer_lobbies)) != 0 ||
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
        shell->runtime.battery_supported != runtime->battery_supported ||
        shell->runtime.battery_sample_valid !=
            runtime->battery_sample_valid ||
        shell->runtime.battery_calibrated != runtime->battery_calibrated ||
        shell->runtime.battery_millivolts !=
            runtime->battery_millivolts ||
        shell->runtime.battery_percent != runtime->battery_percent ||
        shell->runtime.battery_last_error != runtime->battery_last_error ||
        shell->runtime.boot_volume_step != runtime->boot_volume_step ||
        shell->runtime.game_volume_step != runtime->game_volume_step ||
        shell->runtime.audio_settings_persistent !=
            runtime->audio_settings_persistent ||
        shell->runtime.game_storage_space_valid != runtime->game_storage_space_valid ||
        shell->runtime.sd_card_storage != runtime->sd_card_storage ||
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
        shell->runtime.game_storage_space_valid != runtime->game_storage_space_valid ||
        shell->runtime.sd_card_storage != runtime->sd_card_storage ||
        shell->runtime.game_storage_free_kib != runtime->game_storage_free_kib ||
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
    const bool battery_changed =
#if CONFIG_P4_BOARD_M5STACK_TAB5
        shell->runtime.game_volume_step != runtime->game_volume_step ||
#endif
        shell->runtime.battery_supported != runtime->battery_supported ||
        shell->runtime.battery_sample_valid !=
            runtime->battery_sample_valid ||
        shell->runtime.battery_percent != runtime->battery_percent;
    shell->runtime = *runtime;
    if (shell->multiplayer_view != CONSOLE_MULTIPLAYER_VIEW_JOIN &&
        shell->multiplayer_selected_row ==
            CONSOLE_MULTIPLAYER_OPTION_LOBBY) {
        shell->multiplayer_selected_row =
            CONSOLE_MULTIPLAYER_OPTION_COUNT;
    } else if (!shell->runtime.multiplayer_game_is_doom &&
               shell->multiplayer_selected_row !=
                   CONSOLE_MULTIPLAYER_OPTION_TRANSPORT &&
               shell->multiplayer_selected_row !=
                   CONSOLE_MULTIPLAYER_OPTION_GAME &&
               !(shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS &&
                 shell->runtime.multiplayer_dice_available &&
                 shell->multiplayer_selected_row == CONSOLE_MULTIPLAYER_OPTION_DICE) &&
               !(shell->multiplayer_view ==
                     CONSOLE_MULTIPLAYER_VIEW_HOST &&
                 shell->multiplayer_selected_row ==
                     MULTIPLAYER_HOST_SETTINGS_ROW) &&
               !(shell->multiplayer_view ==
                     CONSOLE_MULTIPLAYER_VIEW_JOIN &&
                 shell->multiplayer_selected_row ==
                     CONSOLE_MULTIPLAYER_OPTION_LOBBY) &&
               shell->multiplayer_selected_row !=
                   CONSOLE_MULTIPLAYER_OPTION_COUNT) {
        shell->multiplayer_selected_row =
            CONSOLE_MULTIPLAYER_OPTION_GAME;
    }
    if (shell->page == CONSOLE_PAGE_STORAGE &&
        !storage_action_enabled(shell, shell->storage_selected_action)) {
        reset_storage_controls(shell);
    }
    if (shell->page == CONSOLE_PAGE_CONTROLLERS &&
        !controller_action_enabled(
            shell, shell->controller_selected_action)) {
        reset_controller_controls(shell);
    }
    if (shell->page == CONSOLE_PAGE_CONTROL_PANEL ||
        shell->page == CONSOLE_PAGE_SYSTEM ||
        shell->page == CONSOLE_PAGE_STORAGE ||
        shell->page == CONSOLE_PAGE_POWER ||
        shell->page == CONSOLE_PAGE_SENSORS ||
        shell->page == CONSOLE_PAGE_CONTROLLERS ||
        shell->page == CONSOLE_PAGE_USB_DRIVE ||
        shell->page == CONSOLE_PAGE_FILE_TRANSFER ||
        shell->page == CONSOLE_PAGE_FILES ||
        shell->page == CONSOLE_PAGE_GAMES ||
        shell->page == CONSOLE_PAGE_AUDIO ||
        shell->page == CONSOLE_PAGE_MULTIPLAYER ||
        shell->page == CONSOLE_PAGE_TERMINAL ||
        storage_changed || battery_changed) {
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
#if CONFIG_P4_BOARD_M5STACK_TAB5
    if (shell->files.revision != listing->revision || shell->files.storage_generation != listing->storage_generation ||
        memcmp(&shell->files, listing, sizeof(*listing)) != 0) {
        shell->file_delete_confirm = false;
        shell->press_active = false;
        shell->ng_list_first = 0U;
        shell->ng_file_scroll = 0;
        shell->ng_file_menu = false;
        shell->ng_scroll_kind = 0;
        shell->ng_glide_kind = 0;
        shell->ng_velocity_q16 = 0;
    }
#endif
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
    shell->page = shell->control_panel_active
        ? CONSOLE_PAGE_CONTROL_PANEL : CONSOLE_PAGE_HOME;
    shell->active_app_id = 0U;
    shell->contact_down = false;
    shell->press_active = false;
    shell->scroll_candidate = false;
    shell->scroll_gesture = false;
    sync_home_scroll_visual(shell);
    shell->file_delete_confirm = false;
    shell->storage_repair_confirm = false;
    shell->pressed_index = SIZE_MAX;
    console_shell_invalidate_native_cache(shell);
    shell->dirty = true;
}

console_color_mode_t console_shell_color_mode(const console_shell_t *shell)
{
    return shell != NULL && color_mode_is_valid(shell->color_mode)
        ? shell->color_mode : CONSOLE_COLOR_MODE_ARCADE;
}

bool console_shell_uses_native_bbs_launcher(const console_shell_t *shell)
{
    return shell != NULL && shell->page == CONSOLE_PAGE_HOME &&
        use_bbs_launcher(shell);
}

bool console_shell_is_dirty(const console_shell_t *shell)
{
    return shell != NULL && shell->dirty;
}

static int layout_to_output_x(int x)
{
    return x * (int)s_render_width / CONSOLE_SHELL_LAYOUT_WIDTH;
}

static int layout_to_output_y(int y)
{
    return y * (int)s_render_height / CONSOLE_SHELL_LAYOUT_HEIGHT;
}

static bool uses_compact_raster(void)
{
    return CONSOLE_SHELL_PRESENT_WIDTH != CONSOLE_SHELL_LAYOUT_WIDTH &&
        s_render_width == (size_t)CONSOLE_SHELL_PRESENT_WIDTH &&
        s_render_height == (size_t)CONSOLE_SHELL_PRESENT_HEIGHT;
}

static void set_layout_clip(int left, int top, int width, int height)
{
    s_clip_left = left < 0 ? 0 : left;
    s_clip_top = top < 0 ? 0 : top;
    s_clip_right = left + width;
    s_clip_bottom = top + height;
    if (s_clip_right > CONSOLE_SHELL_LAYOUT_WIDTH) {
        s_clip_right = CONSOLE_SHELL_LAYOUT_WIDTH;
    }
    if (s_clip_bottom > CONSOLE_SHELL_LAYOUT_HEIGHT) {
        s_clip_bottom = CONSOLE_SHELL_LAYOUT_HEIGHT;
    }
    if (s_clip_right < s_clip_left) {
        s_clip_right = s_clip_left;
    }
    if (s_clip_bottom < s_clip_top) {
        s_clip_bottom = s_clip_top;
    }
}

static void reset_layout_clip(void)
{
    set_layout_clip(
        0, 0, CONSOLE_SHELL_LAYOUT_WIDTH, CONSOLE_SHELL_LAYOUT_HEIGHT);
}

static void set_output_clip(int left, int top, int right, int bottom)
{
    s_output_clip_left = left < 0 ? 0 : left;
    s_output_clip_top = top < 0 ? 0 : top;
    s_output_clip_right = right > (int)s_render_width
        ? (int)s_render_width : right;
    s_output_clip_bottom = bottom > (int)s_render_height
        ? (int)s_render_height : bottom;
    if (s_output_clip_right < s_output_clip_left) {
        s_output_clip_right = s_output_clip_left;
    }
    if (s_output_clip_bottom < s_output_clip_top) {
        s_output_clip_bottom = s_output_clip_top;
    }
}

static void reset_output_clip(void)
{
    set_output_clip(0, 0, (int)s_render_width, (int)s_render_height);
}

static void fill_output_rect(uint16_t *pixels, size_t stride,
                             int left, int top, int right, int bottom,
                             uint16_t color)
{
    if (left < s_output_clip_left) {
        left = s_output_clip_left;
    }
    if (top < s_output_clip_top) {
        top = s_output_clip_top;
    }
    if (right > s_output_clip_right) {
        right = s_output_clip_right;
    }
    if (bottom > s_output_clip_bottom) {
        bottom = s_output_clip_bottom;
    }
    if (left >= right || top >= bottom) {
        return;
    }
    uint16_t *const first_row =
        pixels + (size_t)top * stride + (size_t)left;
    const size_t width = (size_t)(right - left);
    for (size_t column = 0U; column < width; ++column) {
        first_row[column] = color;
    }
    const size_t row_bytes = width * sizeof(*first_row);
    for (int row = top + 1; row < bottom; ++row) {
        memcpy(pixels + (size_t)row * stride + (size_t)left,
               first_row, row_bytes);
    }
}

static void put_pixel(uint16_t *pixels, size_t stride,
                      int x, int y, uint16_t color)
{
    if (x >= s_clip_left && x < s_clip_right &&
        y >= s_clip_top && y < s_clip_bottom) {
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
    if (left < s_clip_left) {
        left = s_clip_left;
    }
    if (top < s_clip_top) {
        top = s_clip_top;
    }
    if (right > s_clip_right) {
        right = s_clip_right;
    }
    if (bottom > s_clip_bottom) {
        bottom = s_clip_bottom;
    }
    if (right > CONSOLE_SHELL_LAYOUT_WIDTH) {
        right = CONSOLE_SHELL_LAYOUT_WIDTH;
    }
    if (bottom > CONSOLE_SHELL_LAYOUT_HEIGHT) {
        bottom = CONSOLE_SHELL_LAYOUT_HEIGHT;
    }
    if (left >= right || top >= bottom) {
        return;
    }
    const int output_left = layout_to_output_x(left);
    const int output_top = layout_to_output_y(top);
    int output_right = layout_to_output_x(right);
    int output_bottom = layout_to_output_y(bottom);
    /* The compact shell is an exact 2x source for the panel.  Keep authored
     * one-pixel rules at one source pixel instead of allowing their weight to
     * alternate between one and two pixels with the 6:5 layout conversion. */
    if (uses_compact_raster()) {
        if (width == 1) {
            output_right = output_left + 1;
        }
        if (height == 1) {
            output_bottom = output_top + 1;
        }
    }
    fill_output_rect(pixels, stride, output_left, output_top,
                     output_right, output_bottom, color);
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
    case '%': GLYPH(24,25,2,4,8,19,3); break;
    case '<': GLYPH(2,4,8,16,8,4,2); break;
    case '>': GLYPH(8,4,2,1,2,4,8); break;
    case '?': GLYPH(14,17,1,2,4,0,4); break;
    case '_': GLYPH(0,0,0,0,0,0,31); break;
    default: break;
    }
#undef GLYPH
}

static bool glyph_pixel_is_set(const uint8_t rows[7],
                               unsigned row,
                               unsigned column)
{
    return row < 7U && column < 5U &&
        (rows[row] &
         (uint8_t)(UINT8_C(1) << (4U - column))) != 0U;
}

static int compact_glyph_span(unsigned logical_span,
                              size_t output_extent,
                              unsigned layout_extent)
{
    const size_t span =
        ((size_t)logical_span * output_extent + layout_extent / 2U) /
        layout_extent;
    return span == 0U ? 1 : (int)span;
}

static void fill_output_rect_in_layout_clip(
    uint16_t *pixels,
    size_t stride,
    int left,
    int top,
    int right,
    int bottom,
    uint16_t color)
{
    const int clip_left = layout_to_output_x(s_clip_left);
    const int clip_top = layout_to_output_y(s_clip_top);
    const int clip_right = layout_to_output_x(s_clip_right);
    const int clip_bottom = layout_to_output_y(s_clip_bottom);
    if (left < clip_left) {
        left = clip_left;
    }
    if (top < clip_top) {
        top = clip_top;
    }
    if (right > clip_right) {
        right = clip_right;
    }
    if (bottom > clip_bottom) {
        bottom = clip_bottom;
    }
    fill_output_rect(pixels, stride, left, top, right, bottom, color);
}

static void draw_compact_output_line(uint16_t *pixels,
                                     size_t stride,
                                     int x0,
                                     int y0,
                                     int x1,
                                     int y1,
                                     int stroke_width,
                                     int stroke_height,
                                     uint16_t color)
{
    const int delta_x = x1 >= x0 ? x1 - x0 : x0 - x1;
    const int step_x = x0 < x1 ? 1 : -1;
    const int delta_y = y1 >= y0 ? y1 - y0 : y0 - y1;
    const int step_y = y0 < y1 ? 1 : -1;
    int error = delta_x - delta_y;
    for (;;) {
        fill_output_rect_in_layout_clip(
            pixels, stride, x0, y0,
            x0 + stroke_width, y0 + stroke_height, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice_error = error * 2;
        if (twice_error > -delta_y) {
            error -= delta_y;
            x0 += step_x;
        }
        if (twice_error < delta_x) {
            error += delta_x;
            y0 += step_y;
        }
    }
}

static void draw_compact_glyph(uint16_t *pixels,
                               size_t stride,
                               int x,
                               int y,
                               const uint8_t rows[7],
                               unsigned scale,
                               uint16_t color)
{
    const int stroke_width = compact_glyph_span(
        scale, s_render_width, CONSOLE_SHELL_LAYOUT_WIDTH);
    const int stroke_height = compact_glyph_span(
        scale, s_render_height, CONSOLE_SHELL_LAYOUT_HEIGHT);
    /* Emit bounded row and column runs rather than independently scaling each
     * bitmap cell.  Run length follows the 6:5 geometry, while its transverse
     * stroke stays fixed.  This retains the authored glyph extents without
     * phase-dependent stem weight and needs only a small number of fills. */
    for (unsigned row = 0U; row < 7U; ++row) {
        unsigned column = 0U;
        while (column < 5U) {
            while (column < 5U &&
                   !glyph_pixel_is_set(rows, row, column)) {
                ++column;
            }
            if (column == 5U) {
                break;
            }
            const unsigned first = column;
            while (column < 5U &&
                   glyph_pixel_is_set(rows, row, column)) {
                ++column;
            }
            const int left = layout_to_output_x(
                x + (int)(first * scale));
            int right = layout_to_output_x(
                x + (int)(column * scale));
            if (column == first + 1U) {
                right = left + stroke_width;
            }
            const int top = layout_to_output_y(
                y + (int)(row * scale));
            fill_output_rect_in_layout_clip(
                pixels, stride, left, top, right,
                top + stroke_height, color);
        }
    }
    /* The row pass already paints every set cell.  Only fill the occasional
     * one-pixel gap that the 6:5 vertical mapping inserts between directly
     * adjacent cells; repainting complete column runs costs measurable frame
     * time on the device without improving connectivity. */
    for (unsigned row = 0U; row + 1U < 7U; ++row) {
        const int current_top = layout_to_output_y(
            y + (int)(row * scale));
        const int next_top = layout_to_output_y(
            y + (int)((row + 1U) * scale));
        const int gap_top = current_top + stroke_height;
        if (gap_top >= next_top) {
            continue;
        }
        for (unsigned column = 0U; column < 5U; ++column) {
            if (!glyph_pixel_is_set(rows, row, column) ||
                !glyph_pixel_is_set(rows, row + 1U, column)) {
                continue;
            }
            const int left = layout_to_output_x(
                x + (int)(column * scale));
            fill_output_rect_in_layout_clip(
                pixels, stride, left, gap_top,
                left + stroke_width, next_top, color);
        }
    }

    /* Preserve the corner adjacency of genuinely diagonal bitmap segments.
     * Horizontal and vertical neighbors are already joined by the run passes;
     * only bridge a diagonal when neither orthogonal route exists. */
    for (unsigned row = 0U; row + 1U < 7U; ++row) {
        for (unsigned column = 0U; column < 5U; ++column) {
            if (!glyph_pixel_is_set(rows, row, column)) {
                continue;
            }
            for (int direction = -1; direction <= 1; direction += 2) {
                const int next_column = (int)column + direction;
                if (next_column < 0 || next_column >= 5 ||
                    !glyph_pixel_is_set(
                        rows, row + 1U, (unsigned)next_column) ||
                    glyph_pixel_is_set(
                        rows, row, (unsigned)next_column) ||
                    glyph_pixel_is_set(rows, row + 1U, column)) {
                    continue;
                }
                draw_compact_output_line(
                    pixels, stride,
                    layout_to_output_x(
                        x + (int)(column * scale)),
                    layout_to_output_y(y + (int)(row * scale)),
                    layout_to_output_x(
                        x + next_column * (int)scale),
                    layout_to_output_y(
                        y + (int)((row + 1U) * scale)),
                    stroke_width, stroke_height, color);
            }
        }
    }
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
        if (s_render_height >= 720U) {
            const unsigned char character = (unsigned char)text[index];
            const int left = layout_to_output_x(cursor);
            const int top = layout_to_output_y(y);
            const int glyph_width = layout_to_output_x(cursor + (int)(5U * scale)) - left;
            const int glyph_height = layout_to_output_y(y + (int)(7U * scale)) - top;
            for (int py = 0; py < glyph_height; ++py) {
                for (int px = 0; px < glyph_width; ++px) {
                    const unsigned alpha = console_ui_glyph_alpha(character,
                        (unsigned)(px * 24 / glyph_width), (unsigned)(py * 28 / glyph_height));
                    const int ox = left + px, oy = top + py;
                    if (alpha == 0U || ox < s_output_clip_left || ox >= s_output_clip_right ||
                        oy < s_output_clip_top || oy >= s_output_clip_bottom ||
                        ox < layout_to_output_x(s_clip_left) || ox >= layout_to_output_x(s_clip_right) ||
                        oy < layout_to_output_y(s_clip_top) || oy >= layout_to_output_y(s_clip_bottom)) continue;
                    uint16_t *dst = &pixels[(size_t)oy * stride + (size_t)ox];
                    const unsigned inverse = 15U - alpha;
                    const unsigned r = (((unsigned)color >> 11U) * alpha + ((unsigned)*dst >> 11U) * inverse + 7U) / 15U;
                    const unsigned g = ((((unsigned)color >> 5U) & 63U) * alpha + (((unsigned)*dst >> 5U) & 63U) * inverse + 7U) / 15U;
                    const unsigned b = (((unsigned)color & 31U) * alpha + ((unsigned)*dst & 31U) * inverse + 7U) / 15U;
                    *dst = (uint16_t)((r << 11U) | (g << 5U) | b);
                }
            }
            cursor += (int)(6U * scale);
            continue;
        }
        uint8_t rows[7];
        glyph_rows(text[index], rows);
        if (uses_compact_raster()) {
            draw_compact_glyph(
                pixels, stride, cursor, y, rows, scale, color);
            cursor += (int)(6U * scale);
            continue;
        }
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
    digits[count] = '\0';
    draw_text(pixels, stride, x, y, digits, color, 1U, count);
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
    const int32_t visual_q16 = clamp_home_scroll_q16(
        shell, shell->home_scroll_visual_q16);
    const int32_t maximum_q16 = home_scroll_row_q16(maximum);
    const int thumb_top = maximum_q16 == 0 ? inner_top :
        inner_top + (int)(
            (int64_t)travel * visual_q16 / maximum_q16);
    bevel_rect(pixels, stride, SCROLL_LEFT + 2, thumb_top,
               SCROLL_WIDTH - 4, thumb_height, COLOR_FACE, false);
}

static size_t draw_home_tiles(console_shell_t *shell,
                              uint16_t *pixels,
                              size_t stride)
{
    home_item_t items[HOME_ITEM_CAPACITY];
    const size_t item_count = build_home_items(shell, items);
    const int row_pitch = TILE_HEIGHT + TILE_ROW_GAP;
    const int32_t visual_q16 = clamp_home_scroll_q16(
        shell, shell->home_scroll_visual_q16);
    const size_t first_row = (size_t)(
        visual_q16 / SCROLL_POSITION_ONE);
    const size_t row_count = home_row_count(shell);
    size_t last_row = first_row + CONSOLE_SHELL_VISIBLE_APP_ROWS + 1U;
    if (last_row > row_count) {
        last_row = row_count;
    }
    set_layout_clip(TILE_LEFT, TILE_TOP, GRID_WIDTH, GRID_HEIGHT);
    for (size_t item_row = first_row;
         item_row < last_row; ++item_row) {
        const int top = TILE_TOP + (int)(
            ((int64_t)home_scroll_row_q16(item_row) - visual_q16) *
                row_pitch / SCROLL_POSITION_ONE);
        for (size_t item_column = 0U;
             item_column < CONSOLE_SHELL_APP_COLUMNS; ++item_column) {
            const size_t index =
                item_row * CONSOLE_SHELL_APP_COLUMNS + item_column;
            if (index >= item_count) {
                break;
            }
            const home_item_t *const item = &items[index];
            const console_app_descriptor_t *const app =
                item->kind == HOME_ITEM_APP &&
                        item->app_index < shell->app_count
                    ? &shell->apps[item->app_index] : NULL;
            const int left = TILE_LEFT + (int)item_column *
                (TILE_WIDTH + TILE_COLUMN_GAP);
            const bool pressed = shell->press_active &&
                shell->pressed_index == HOME_ITEM_CONTROL_BASE + index;
            fill_rect(pixels, stride, left, top, TILE_WIDTH, TILE_HEIGHT,
                      pressed ? COLOR_TITLE : COLOR_GROUP);
            if (shell->selected_home_item == index || pressed) {
                outline_rect(
                    pixels, stride, left, top, TILE_WIDTH, TILE_HEIGHT,
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
            draw_centered_text(
                pixels, stride, left, top + 30, TILE_WIDTH,
                app != NULL ? app->title : item->title,
                label_color, 15U);
            if (app != NULL) {
                draw_centered_text(
                    pixels, stride, left, top + 40, TILE_WIDTH,
                    app->enabled ? app->subtitle : "OFFLINE",
                    pressed ? COLOR_WHITE : COLOR_DARK, 13U);
            } else {
                draw_program_count(
                    pixels, stride, left, top + 41, TILE_WIDTH,
                    item->program_count,
                    pressed ? COLOR_WHITE : COLOR_DARK);
            }
        }
    }
    reset_layout_clip();
    return item_count;
}

static void draw_home_status(console_shell_t *shell,
                             uint16_t *pixels,
                             size_t stride,
                             size_t item_count)
{
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
              "GameChangersAI", COLOR_WHITE, 1U, 14U);
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

    const size_t item_count = draw_home_tiles(shell, pixels, stride);
    draw_scrollbar(shell, pixels, stride);
    draw_home_status(shell, pixels, stride, item_count);
}

#if CONSOLE_SHELL_NATIVE_BBS
static bool draw_bbs_home(console_shell_t *shell,
                          uint16_t *pixels,
                          size_t stride)
{
    p4_bbs_launcher_model_t target_model;
    build_bbs_launcher_model(shell, &target_model);
    if (!p4_bbs_build_launcher(&shell->bbs_terminal, &target_model)) {
        return false;
    }

    const int32_t visual_q16 = clamp_home_scroll_q16(
        shell, shell->home_scroll_visual_q16);
    const size_t base_row = (size_t)(
        visual_q16 / SCROLL_POSITION_ONE);
    const int32_t fraction_q16 = visual_q16 -
        home_scroll_row_q16(base_row);
    if (fraction_q16 == 0 || base_row >= home_max_scroll_row(shell)) {
        return p4_ansi_render_scaled_rgb565(
            &shell->bbs_terminal, pixels, stride,
            CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT);
    }

    /* The caller may provide an arbitrary frame buffer. Clear it first, then
     * render only the static ANSI bands and the two moving door bands; this
     * preserves the full-render result without depending on prior contents. */
    for (size_t row = 0U; row < CONSOLE_SHELL_HEIGHT; ++row) {
        memset(pixels + row * stride, 0,
               CONSOLE_SHELL_WIDTH * sizeof(*pixels));
    }
    if (!p4_ansi_render_scaled_rows_rgb565(
            &shell->bbs_terminal, pixels, stride,
            CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT,
            0U, BBS_DOOR_FIRST_ANSI_ROW, 0, 0U,
            P4_ANSI_SURFACE_HEIGHT) ||
        !p4_ansi_render_scaled_rows_rgb565(
            &shell->bbs_terminal, pixels, stride,
            CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT,
            BBS_DOOR_FIRST_ANSI_ROW + BBS_DOOR_ANSI_ROW_COUNT,
            P4_ANSI_ROWS - (BBS_DOOR_FIRST_ANSI_ROW +
                            BBS_DOOR_ANSI_ROW_COUNT),
            0, 0U, P4_ANSI_SURFACE_HEIGHT)) {
        return false;
    }

    const int32_t offset_pixels = (int32_t)(
        ((int64_t)fraction_q16 * BBS_DOOR_PITCH_PIXELS +
         SCROLL_POSITION_ONE / 2) / SCROLL_POSITION_ONE);
    p4_bbs_launcher_model_t moving_model;
    build_bbs_launcher_model_at_row(shell, base_row, &moving_model);
    if (!p4_bbs_build_launcher(&shell->bbs_terminal, &moving_model) ||
        !p4_ansi_render_scaled_rows_rgb565(
            &shell->bbs_terminal, pixels, stride,
            CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT,
            BBS_DOOR_FIRST_ANSI_ROW, BBS_DOOR_ANSI_ROW_COUNT,
            -offset_pixels, BBS_DOOR_VIEW_TOP, BBS_DOOR_VIEW_HEIGHT)) {
        return false;
    }
    build_bbs_launcher_model_at_row(shell, base_row + 1U, &moving_model);
    const size_t entering_rows =
        ((size_t)BBS_DOOR_VIEW_HEIGHT -
         (size_t)(BBS_DOOR_PITCH_PIXELS - offset_pixels) +
         P4_ANSI_CELL_HEIGHT - 1U) / P4_ANSI_CELL_HEIGHT;
    if (!p4_bbs_build_launcher(&shell->bbs_terminal, &moving_model) ||
        !p4_ansi_render_scaled_rows_rgb565(
            &shell->bbs_terminal, pixels, stride,
            CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT,
            BBS_DOOR_FIRST_ANSI_ROW, entering_rows,
            BBS_DOOR_PITCH_PIXELS - offset_pixels,
            BBS_DOOR_VIEW_TOP, BBS_DOOR_VIEW_HEIGHT)) {
        return false;
    }

    /* Leave diagnostics and hit-test inspection on the logical destination. */
    return p4_bbs_build_launcher(&shell->bbs_terminal, &target_model);
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
                  ? "< UP"
                  : shell->page == CONSOLE_PAGE_MULTIPLAYER &&
                            shell->multiplayer_view !=
                                CONSOLE_MULTIPLAYER_VIEW_ROLE
                      ? shell->multiplayer_view ==
                                CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS
                          ? "< HOST"
                          : "< ROLE"
                      : shell->control_panel_active ? "< PANEL" : "< HOME",
              COLOR_WHITE, 1U, 6U);
    const console_app_descriptor_t *const app = active_app(shell);
    draw_text(pixels, stride, 68, 11,
              app != NULL ? app->title : "CONSOLE",
              app != NULL ? app->accent_rgb565 : COLOR_WHITE,
              1U, 14U);
    fill_rect(pixels, stride, 0, 31,
              CONSOLE_SHELL_LAYOUT_WIDTH, 1, COLOR_CYAN);
}


static void draw_control_panel(console_shell_t *shell, uint16_t *pixels, size_t stride)
{
    const uint16_t ink = UINT16_C(0x18E3), muted = UINT16_C(0x6B6D);
    const uint16_t paper = UINT16_C(0xFFFF), rail = UINT16_C(0xE71C);
    const uint16_t line = UINT16_C(0xCE79), blue = UINT16_C(0x1950);
    const uint16_t good = UINT16_C(0x23C9), warning = UINT16_C(0x9AA0);
    fill_rect(pixels,stride,0,0,320,200,paper);
    fill_rect(pixels,stride,0,0,320,32,blue);
    draw_text(pixels,stride,10,12,"< Home",paper,1U,6U);
    draw_text(pixels,stride,69,11,"Control Panel",paper,1U,13U);
    fill_rect(pixels,stride,0,32,98,168,rail);
    fill_rect(pixels,stride,97,32,1,168,line);
    for (size_t i=0; i<PANEL_SECTION_COUNT; ++i) {
        const int y=42+(int)i*23;
        const bool selected=i==shell->control_panel_section;
        if (selected) fill_rect(pixels,stride,5,y,89,22,blue);
        draw_text(pixels,stride,11,y+8,s_panel_names[i],selected?paper:ink,1U,13U);
        if (selected && !shell->control_panel_row_focus)
            fill_rect(pixels,stride,6,y+3,2,16,UINT16_C(0x6E5F));
    }
    const unsigned section = shell->control_panel_section < PANEL_SECTION_COUNT ? shell->control_panel_section : 0U;
    draw_text(pixels,stride,106,44,s_panel_names[section],ink,1U,20U);
    fill_rect(pixels,stride,106,57,202,1,line);
    if (section==0U) {
        char value[40];
        draw_text(pixels,stride,106,70,"Battery",muted,1U,8U);
        if (shell->runtime.battery_sample_valid) {
            (void)snprintf(value,sizeof(value),"%u%% estimated",(unsigned)shell->runtime.battery_percent);
            draw_text(pixels,stride,181,70,value,good,1U,20U);
        } else draw_text(pixels,stride,181,70,"No pack reading",warning,1U,20U);
        draw_text(pixels,stride,106,93,"Storage",muted,1U,8U);
        if (shell->runtime.game_storage_filesystem_ready) {
            (void)snprintf(value,sizeof(value),"%lu MB free",(unsigned long)(shell->runtime.game_storage_free_kib/1024U));
            draw_text(pixels,stride,181,93,value,ink,1U,20U);
        } else draw_text(pixels,stride,181,93,"SD unavailable",warning,1U,20U);
        draw_text(pixels,stride,106,116,"Motion",muted,1U,8U);
        draw_text(pixels,stride,181,116,shell->runtime.motion_valid?"Sensor online":"Unavailable",
                  shell->runtime.motion_valid?good:warning,1U,20U);
        draw_text(pixels,stride,106,139,"Clock",muted,1U,8U);
        draw_text(pixels,stride,181,139,shell->runtime.rtc_valid?shell->runtime.rtc_datetime:"Needs setting",ink,1U,10U);
        if (shell->runtime.rtc_valid && strlen(shell->runtime.rtc_datetime)>11U)
            draw_text(pixels,stride,181,151,shell->runtime.rtc_datetime+11,muted,1U,8U);
        fill_rect(pixels,stride,106,168,202,1,line);
        (void)snprintf(value,sizeof(value),"Up %lu min  |  Details in Advanced",(unsigned long)(shell->runtime.uptime_seconds/60U));
        draw_text(pixels,stride,106,178,value,muted,1U,33U);
        return;
    }
    size_t indexes[CONSOLE_SHELL_MAX_APPS];
    const size_t count=panel_items(shell,indexes);
    if (section==1U) {
        for (unsigned row=0; row<2U; ++row) {
            const int y=71+(int)row*34;
            const unsigned volume=row==0U?shell->runtime.boot_volume_step:shell->runtime.game_volume_step;
            char level[12];(void)snprintf(level,sizeof(level),"%u / 10",volume>10U?10U:volume);
            draw_text(pixels,stride,106,y+1,row==0U?"Startup sound":"Game sound",ink,1U,18U);
            draw_text(pixels,stride,106,y+14,volume==0U?"Muted":level,muted,1U,12U);
            for (unsigned button=0; button<2U; ++button) {
                const size_t control=PANEL_VOLUME_BASE+row*2U+button;
                const bool pressed=shell->press_active&&shell->pressed_index==control;
                const bool focused=shell->control_panel_row_focus&&shell->control_panel_selection==row*2U+button;
                fill_rect(pixels,stride,228+(int)button*43,y,36,25,pressed||focused?blue:rail);
                draw_text(pixels,stride,244+(int)button*43,y+8,button==0U?"-":"+",pressed||focused?paper:ink,1U,1U);
            }
        }
        if (count) {
            const bool focused=shell->control_panel_row_focus&&shell->control_panel_selection==4U;
            fill_rect(pixels,stride,106,145,202,25,focused?blue:rail);
            draw_text(pixels,stride,114,154,"Appearance",focused?paper:ink,1U,20U);
            draw_text(pixels,stride,295,154,">",focused?paper:ink,1U,1U);
        }
        draw_text(pixels,stride,106,178,shell->runtime.audio_settings_persistent
            ? "Volume changes are saved" : "Volume applies this session",muted,1U,33U);
        return;
    }
    if (!count) draw_text(pixels,stride,106,77,"No available tools",muted,1U,30U);
    for (size_t row=0; row<PANEL_VISIBLE_ROWS && shell->control_panel_first+row<count; ++row) {
        const size_t index=shell->control_panel_first+row;
        const console_app_descriptor_t *app=&shell->apps[indexes[index]];
        const bool focused=shell->control_panel_row_focus&&shell->control_panel_selection==index;
        const bool pressed=shell->press_active&&shell->pressed_index==PANEL_ROW_BASE+row;
        const int y=65+(int)row*25;
        if (focused||pressed) fill_rect(pixels,stride,105,y,203,24,blue);
        draw_text(pixels,stride,112,y+8,app->title,focused||pressed?paper:ink,1U,26U);
        draw_text(pixels,stride,294,y+8,">",focused||pressed?paper:muted,1U,1U);
        if (!focused&&!pressed) fill_rect(pixels,stride,112,y+24,190,1,line);
    }
    if (shell->control_panel_first>0U) {
        fill_rect(pixels,stride,105,168,96,20,rail);
        draw_text(pixels,stride,111,175,"< Previous",ink,1U,12U);
    }
    if (shell->control_panel_first+PANEL_VISIBLE_ROWS<count) {
        fill_rect(pixels,stride,211,168,97,20,rail);
        draw_text(pixels,stride,241,175,"More >",ink,1U,9U);
    }
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
        "WINDOWS DEFAULT / BBS OPTIONAL",
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
    draw_text(pixels, stride, 8, 190, "UP TO 5 TOUCH CONTACTS",
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
    } else if (shell->runtime.board_kind == CONSOLE_BOARD_M5STACK_TAB5) {
        board_name = "M5STACK TAB5";
        soc_name = "ESP32-P4";
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

static const char *battery_level_name(uint8_t percent)
{
    if (percent >= 95U) {
        return "FULL";
    }
    if (percent >= 60U) {
        return "HIGH";
    }
    if (percent >= 30U) {
        return "MEDIUM";
    }
    if (percent >= 10U) {
        return "LOW";
    }
    return "CRITICAL";
}

static void draw_power(const console_shell_t *shell,
                       uint16_t *pixels, size_t stride)
{
    draw_text(pixels, stride, 12, 38, "BATTERY STATUS",
              COLOR_WHITE, 2U, 14U);
    if (!shell->runtime.battery_supported) {
        draw_centered_text(pixels, stride, 20, 78, 280,
                           "BATTERY READING UNAVAILABLE",
                           COLOR_YELLOW, 28U);
        draw_centered_text(pixels, stride, 20, 102, 280,
                           "CHECK BATTERY CONNECTION",
                           COLOR_MUTED, 23U);
        draw_text(pixels, stride, 12, 166, "NO ESTIMATE REPORTED",
                  COLOR_RED, 1U, 20U);
        return;
    }
    if (!shell->runtime.battery_sample_valid) {
        draw_centered_text(pixels, stride, 20, 84, 280,
                           shell->runtime.battery_last_error == 0
                               ? "READING BATTERY..." : "NO VALID BATTERY READING",
                           COLOR_YELLOW, 26U);
        if (shell->runtime.battery_last_error != 0) {
            draw_centered_text(pixels, stride, 20, 108, 280,
                               "CHECK BATTERY PACK", COLOR_MUTED, 20U);
        }
        if (shell->runtime.board_kind == CONSOLE_BOARD_M5STACK_TAB5) {
            char voltage[32];
            (void)snprintf(voltage, sizeof(voltage), "MEASURED %u mV",
                           (unsigned)shell->runtime.battery_millivolts);
            draw_centered_text(pixels, stride, 20, 132, 280, voltage, COLOR_MUTED, 28U);
        }
        draw_text(pixels, stride, 12, 166, "LAST ERROR",
                  COLOR_MUTED, 1U, 10U);
        const int last_error = shell->runtime.battery_last_error;
        const uint32_t error_magnitude = last_error == INT_MIN
            ? (uint32_t)INT_MAX + 1U
            : (uint32_t)(last_error < 0 ? -last_error : last_error);
        draw_u32(pixels, stride, 96, 166, error_magnitude, COLOR_RED);
        return;
    }

    const uint8_t percent = shell->runtime.battery_percent > 100U
        ? 100U : shell->runtime.battery_percent;
    outline_rect(pixels, stride, 30, 70, 248, 38, COLOR_WHITE);
    fill_rect(pixels, stride, 278, 81, 8, 16, COLOR_WHITE);
    const int fill_width = (int)((uint32_t)244U * percent / 100U);
    if (fill_width > 0) {
        fill_rect(pixels, stride, 32, 72, fill_width, 34,
                  percent < 10U ? COLOR_RED
                  : percent < 30U ? COLOR_YELLOW : COLOR_GREEN);
    }
    char percentage[8];
    (void)snprintf(percentage, sizeof(percentage), "%u%%",
                   (unsigned)percent);
    draw_centered_text(pixels, stride, 30, 85, 248, percentage,
                       percent >= 30U ? COLOR_BLACK : COLOR_WHITE, 7U);

    char voltage[16];
    (void)snprintf(voltage, sizeof(voltage), "%u.%03u V",
                   (unsigned)(shell->runtime.battery_millivolts / 1000U),
                   (unsigned)(shell->runtime.battery_millivolts % 1000U));
    draw_text(pixels, stride, 12, 124, "LEVEL", COLOR_MUTED, 1U, 5U);
    draw_text(pixels, stride, 96, 124, battery_level_name(percent),
              percent < 10U ? COLOR_RED
              : percent < 30U ? COLOR_YELLOW : COLOR_GREEN,
              1U, 8U);
    draw_text(pixels, stride, 12, 142, "VOLTAGE", COLOR_MUTED, 1U, 7U);
    draw_text(pixels, stride, 96, 142, voltage,
              COLOR_WHITE, 1U, 15U);
    draw_text(pixels, stride, 12, 160, "ESTIMATE", COLOR_MUTED, 1U, 8U);
    draw_text(pixels, stride, 96, 160,
              "VOLTAGE BASED", COLOR_CYAN, 1U, 14U);
    if (shell->runtime.battery_current_valid) {
        char flow[48];
        const int32_t ma = shell->runtime.battery_milliamps;
        const uint32_t magnitude = (uint32_t)(ma < 0 ? -(int64_t)ma : ma);
        (void)snprintf(flow, sizeof(flow), "%s  %lu mA",
            ma < -10 ? "CHARGING" : ma > 10 ? "DISCHARGING" : "IDLE", (unsigned long)magnitude);
        draw_text(pixels, stride, 12, 182, flow, COLOR_MUTED, 1U, 36U);
    } else {
        draw_text(pixels, stride, 12, 182, "APPROXIMATE CHARGE LEVEL", COLOR_MUTED, 1U, 28U);
    }
}

static void draw_sensors(const console_shell_t *shell, uint16_t *pixels, size_t stride)
{
    draw_text(pixels, stride, 12, 36, "SENSORS", COLOR_WHITE, 2U, 7U);
    char value[48];
    draw_text(pixels, stride, 12, 66, "ACCEL  X / Y / Z (mg)", COLOR_MUTED, 1U, 24U);
    if (shell->runtime.motion_valid) {
        (void)snprintf(value, sizeof(value), "%ld / %ld / %ld",
            (long)shell->runtime.accel_mg[0], (long)shell->runtime.accel_mg[1], (long)shell->runtime.accel_mg[2]);
    } else {
        (void)snprintf(value, sizeof(value), "%s", shell->runtime.motion_supported ? "WAITING FOR SAMPLE" : "MOTION SENSOR UNAVAILABLE");
    }
    draw_text(pixels, stride, 12, 80, value, COLOR_CYAN, 1U, 38U);
    draw_text(pixels, stride, 12, 103, "GYRO   X / Y / Z (deg/s)", COLOR_MUTED, 1U, 26U);
    if (shell->runtime.motion_valid) {
        (void)snprintf(value, sizeof(value), "%ld / %ld / %ld",
            (long)(shell->runtime.gyro_mdps[0] / 1000), (long)(shell->runtime.gyro_mdps[1] / 1000), (long)(shell->runtime.gyro_mdps[2] / 1000));
        draw_text(pixels, stride, 12, 117, value, COLOR_CYAN, 1U, 38U);
    }
    if (shell->runtime.temperature_valid) {
        const int32_t temp = shell->runtime.temperature_millicelsius;
        const uint32_t magnitude = (uint32_t)(temp < 0 ? -(int64_t)temp : temp);
        (void)snprintf(value, sizeof(value), "SENSOR TEMP  %s%lu.%lu C", temp < 0 ? "-" : "",
            (unsigned long)(magnitude / 1000U), (unsigned long)(magnitude % 1000U / 100U));
        draw_text(pixels, stride, 12, 144, value, COLOR_WHITE, 1U, 38U);
    }
    draw_text(pixels, stride, 12, 165, "CLOCK", COLOR_MUTED, 1U, 5U);
    draw_text(pixels, stride, 12, 180,
        shell->runtime.rtc_valid ? shell->runtime.rtc_datetime :
        shell->runtime.rtc_supported ? "CLOCK NEEDS SETTING" : "CLOCK UNAVAILABLE",
        shell->runtime.rtc_valid ? COLOR_WHITE : COLOR_YELLOW, 1U, 32U);
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

static void draw_controller_button(const console_shell_t *shell,
                                   uint16_t *pixels, size_t stride,
                                   int left, int width, size_t control,
                                   size_t action, const char *label)
{
    const bool enabled = controller_action_enabled(shell, action);
    const bool pressed = enabled && shell->press_active &&
        shell->pressed_index == control;
    bevel_rect(pixels, stride, left, CONTROLLER_BUTTON_TOP, width,
               CONTROLLER_BUTTON_HEIGHT, COLOR_FACE, pressed);
    if (enabled && shell->controller_selected_action == action) {
        outline_rect(pixels, stride, left + 2, CONTROLLER_BUTTON_TOP + 2,
                     width - 4, CONTROLLER_BUTTON_HEIGHT - 4, COLOR_YELLOW);
    }
    draw_centered_text(pixels, stride, left, CONTROLLER_BUTTON_TOP + 7,
                       width, label,
                       enabled ? COLOR_BLACK : COLOR_SHADOW, 16U);
}

static const char *controller_source_name(uint8_t source)
{
    static const char *const names[] = {
        "SOUTH", "EAST", "WEST", "NORTH", "LB", "RB", "BACK",
        "START", "GUIDE", "L3", "R3", "MISC", "P1", "P2",
        "P3", "P4", "TOUCH",
    };
    return source < sizeof(names) / sizeof(names[0])
        ? names[source] : "BUTTON";
}

static const char *controller_mapping_target_name(uint8_t target)
{
    static const char *const names[CONSOLE_CONTROLLER_MAPPING_COUNT] = {
        "A", "B", "X", "Y", "START", "BACK",
    };
    return target < CONSOLE_CONTROLLER_MAPPING_COUNT
        ? names[target] : "DONE";
}

static void draw_controllers(const console_shell_t *shell,
                             uint16_t *pixels, size_t stride)
{
    const char *active = "NONE";
    uint16_t active_color = COLOR_YELLOW;
    if (shell->runtime.controller_ready &&
        shell->runtime.controller_transport ==
            CONSOLE_CONTROLLER_TRANSPORT_USB_HID) {
        active = "USB HID (PRIORITY)";
        active_color = COLOR_GREEN;
    } else if (shell->runtime.controller_ready &&
               shell->runtime.controller_transport ==
                   CONSOLE_CONTROLLER_TRANSPORT_USB_XUSB) {
        active = "USB XINPUT";
        active_color = COLOR_GREEN;
    } else if (shell->runtime.controller_ready &&
               shell->runtime.controller_transport ==
                   CONSOLE_CONTROLLER_TRANSPORT_BLE_HID) {
        active = "BLUETOOTH HID";
        active_color = COLOR_GREEN;
    }
    draw_text(pixels, stride, 12, 37, "ACTIVE INPUT",
              COLOR_MUTED, 1U, 12U);
    draw_text(pixels, stride, 132, 37, active,
              active_color, 1U, 20U);

    draw_text(pixels, stride, 12, 52, "BLE PAD MODE",
              COLOR_MUTED, 1U, 12U);
    draw_text(pixels, stride, 132, 52,
              shell->runtime.ble_controller_enabled
                  ? "ENABLED" : "DISABLED",
              shell->runtime.ble_controller_enabled
                  ? COLOR_GREEN : COLOR_YELLOW,
              1U, 10U);

    draw_text(pixels, stride, 12, 67, "BLE PAD",
              COLOR_MUTED, 1U, 7U);
    const char *link = "NOT PAIRED";
    uint16_t link_color = COLOR_YELLOW;
    if (!shell->runtime.ble_controller_supported) {
        link = "UNAVAILABLE";
        link_color = COLOR_MUTED;
    } else if (!shell->runtime.ble_controller_enabled) {
        link = "MODE OFF - MULTIPLAYER OK";
        link_color = COLOR_MUTED;
    } else if (shell->runtime.ble_controller_busy) {
        link = "PAIRING / CONNECTING";
        link_color = COLOR_CYAN;
    } else if (shell->runtime.ble_controller_connected) {
        link = "CONNECTED";
        link_color = COLOR_GREEN;
    } else if (shell->runtime.ble_controller_bonded) {
        link = "SAVED - DISCONNECTED";
        link_color = COLOR_YELLOW;
    }
    draw_text(pixels, stride, 132, 67, link, link_color, 1U, 29U);

    draw_text(pixels, stride, 12, 82, "NAME", COLOR_MUTED, 1U, 4U);
    draw_text(pixels, stride, 132, 82,
              shell->runtime.ble_controller_name[0] != '\0'
                  ? shell->runtime.ble_controller_name : "NO SAVED PAD",
              COLOR_WHITE, 1U, 29U);

    draw_text(pixels, stride, 12, 97, "SECURITY", COLOR_MUTED, 1U, 8U);
    draw_text(pixels, stride, 132, 97,
              shell->runtime.ble_controller_encrypted &&
                      shell->runtime.ble_controller_connected
                  ? "BONDED + ENCRYPTED"
                  : shell->runtime.ble_controller_bonded
                      ? "BONDED" : "PAIRING REQUIRED",
              shell->runtime.ble_controller_encrypted &&
                      shell->runtime.ble_controller_connected
                  ? COLOR_GREEN : COLOR_YELLOW,
              1U, 20U);

    draw_text(pixels, stride, 12, 112, "MULTIPLAYER",
              COLOR_MUTED, 1U, 11U);
    const char *multiplayer = "PAIR PAD BEFORE LOBBY";
    uint16_t multiplayer_color = COLOR_YELLOW;
    if (!shell->runtime.ble_controller_enabled) {
        multiplayer = "PAD OFF - BLE LOBBIES OK";
        multiplayer_color = COLOR_CYAN;
    } else if (shell->runtime.ble_controller_multiplayer_ready) {
        multiplayer = "PAD + 1 PEER READY";
        multiplayer_color = COLOR_GREEN;
    }
    draw_text(pixels, stride, 132, 112, multiplayer,
              multiplayer_color, 1U, 29U);

    if (shell->runtime.controller_mapping_active) {
        draw_text(pixels, stride, 12, 127, "MAPPING",
                  COLOR_MUTED, 1U, 7U);
        char prompt[30];
        const int written = snprintf(
            prompt, sizeof(prompt), "PRESS ONE BUTTON FOR %s",
            controller_mapping_target_name(
                shell->runtime.controller_mapping_target));
        draw_text(pixels, stride, 132, 127,
                  written > 0 ? prompt : "PRESS ONE BUTTON",
                  COLOR_CYAN, 1U, 29U);
        draw_text(pixels, stride, 12, 142, "RULE",
                  COLOR_MUTED, 1U, 4U);
        draw_text(pixels, stride, 132, 142,
                  "RELEASE BETWEEN STEPS", COLOR_WHITE, 1U, 29U);
        draw_text(pixels, stride, 12, 157, "STATUS",
                  COLOR_MUTED, 1U, 6U);
        draw_text(pixels, stride, 132, 157,
                  shell->runtime.controller_mapping_last_error == 0
                      ? "WAITING FOR INPUT" : "TRY A DIFFERENT BUTTON",
                  shell->runtime.controller_mapping_last_error == 0
                      ? COLOR_GREEN : COLOR_YELLOW,
                  1U, 29U);
    } else {
        draw_text(pixels, stride, 12, 127, "A / B",
                  COLOR_MUTED, 1U, 5U);
        char ab[30];
        const int ab_written = snprintf(
            ab, sizeof(ab), "%s / %s",
            controller_source_name(shell->runtime.controller_mapping[
                CONSOLE_CONTROLLER_MAPPING_A]),
            controller_source_name(shell->runtime.controller_mapping[
                CONSOLE_CONTROLLER_MAPPING_B]));
        draw_text(pixels, stride, 132, 127,
                  ab_written > 0 ? ab : "--", COLOR_WHITE, 1U, 29U);
        draw_text(pixels, stride, 12, 142, "X / Y",
                  COLOR_MUTED, 1U, 5U);
        char xy[30];
        const int xy_written = snprintf(
            xy, sizeof(xy), "%s / %s",
            controller_source_name(shell->runtime.controller_mapping[
                CONSOLE_CONTROLLER_MAPPING_X]),
            controller_source_name(shell->runtime.controller_mapping[
                CONSOLE_CONTROLLER_MAPPING_Y]));
        draw_text(pixels, stride, 132, 142,
                  xy_written > 0 ? xy : "--", COLOR_WHITE, 1U, 29U);
        draw_text(pixels, stride, 12, 157, "START / BACK",
                  COLOR_MUTED, 1U, 12U);
        char system[30];
        const int system_written = snprintf(
            system, sizeof(system), "%s / %s%s",
            controller_source_name(shell->runtime.controller_mapping[
                CONSOLE_CONTROLLER_MAPPING_START]),
            controller_source_name(shell->runtime.controller_mapping[
                CONSOLE_CONTROLLER_MAPPING_BACK]),
            shell->runtime.controller_mapping_persistent ? "" : " *");
        draw_text(pixels, stride, 132, 157,
                  system_written > 0 ? system : "--",
                  shell->runtime.controller_mapping_last_error == 0
                      ? COLOR_WHITE : COLOR_YELLOW,
                  1U, 29U);
    }

    draw_controller_button(shell, pixels, stride,
                           CONTROLLER_BLE_LEFT, CONTROLLER_BLE_WIDTH,
                           CONTROLLER_BLE_CONTROL, 0U,
                           shell->runtime.ble_controller_enabled
                               ? "BLE OFF" : "BLE ON");
    draw_controller_button(shell, pixels, stride,
                           CONTROLLER_PAIR_LEFT, CONTROLLER_PAIR_WIDTH,
                           CONTROLLER_PAIR_CONTROL, 1U, "PAIR");
    draw_controller_button(shell, pixels, stride,
                           CONTROLLER_DISCONNECT_LEFT,
                           CONTROLLER_DISCONNECT_WIDTH,
                           CONTROLLER_DISCONNECT_CONTROL, 2U, "DISC");
    draw_controller_button(shell, pixels, stride,
                           CONTROLLER_MAP_LEFT, CONTROLLER_MAP_WIDTH,
                           CONTROLLER_MAP_CONTROL, 3U,
                           shell->runtime.controller_mapping_active
                               ? "CANCEL" : "MAP");
    draw_controller_button(shell, pixels, stride,
                           CONTROLLER_RESET_LEFT, CONTROLLER_RESET_WIDTH,
                           CONTROLLER_RESET_CONTROL, 4U, "RESET");
    draw_controller_button(shell, pixels, stride,
                           CONTROLLER_FORGET_LEFT, CONTROLLER_FORGET_WIDTH,
                           CONTROLLER_FORGET_CONTROL, 5U, "FORGET");
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

static const char *file_transfer_state_name(
    console_file_transfer_state_t state)
{
    switch (state) {
    case CONSOLE_FILE_TRANSFER_RECEIVING: return "RECEIVING";
    case CONSOLE_FILE_TRANSFER_SENDING: return "SENDING";
    case CONSOLE_FILE_TRANSFER_COMPLETE: return "COMPLETE";
    case CONSOLE_FILE_TRANSFER_FAILED: return "FAILED";
    case CONSOLE_FILE_TRANSFER_IDLE:
    default: return "WAITING";
    }
}

static void draw_file_transfer(const console_shell_t *shell,
                               uint16_t *pixels, size_t stride)
{
    const uint8_t progress =
        shell->runtime.file_transfer_progress_percent > 100U
            ? 100U : shell->runtime.file_transfer_progress_percent;
    const char *const state = file_transfer_state_name(
        shell->runtime.file_transfer_state);
    uint16_t state_color = COLOR_CYAN;
    if (!shell->runtime.file_transfer_ready) {
        state_color = COLOR_RED;
    } else if (shell->runtime.file_transfer_state ==
               CONSOLE_FILE_TRANSFER_COMPLETE) {
        state_color = COLOR_GREEN;
    } else if (shell->runtime.file_transfer_state ==
               CONSOLE_FILE_TRANSFER_FAILED) {
        state_color = COLOR_RED;
    } else if (shell->runtime.file_transfer_busy) {
        state_color = COLOR_YELLOW;
    }

    draw_text(pixels, stride, 12, 38, "NODE",
              COLOR_MUTED, 1U, 4U);
    draw_text(pixels, stride, 80, 38,
              shell->runtime.node_name[0] != '\0'
                  ? shell->runtime.node_name : "GC-P4-LOCAL",
              COLOR_WHITE, 1U, 16U);
    draw_text(pixels, stride, 12, 55, "H1 LINK",
              COLOR_MUTED, 1U, 7U);
    draw_text(pixels, stride, 80, 55,
              shell->runtime.file_transfer_ready
                  ? "UART 921600 / READY" : "OFFLINE",
              shell->runtime.file_transfer_ready
                  ? COLOR_GREEN : COLOR_RED,
              1U, 19U);
    draw_text(pixels, stride, 12, 72, "STATUS",
              COLOR_MUTED, 1U, 6U);
    draw_text(pixels, stride, 80, 72, state,
              state_color, 1U, 10U);

    fill_rect(pixels, stride, 12, 91, 296, 16, COLOR_SHADOW);
    outline_rect(pixels, stride, 12, 91, 296, 16, COLOR_CYAN);
    if (progress > 0U) {
        fill_rect(pixels, stride, 14, 93,
                  (int)((uint32_t)292U * progress / 100U), 12,
                  shell->runtime.file_transfer_state ==
                          CONSOLE_FILE_TRANSFER_FAILED
                      ? COLOR_RED : COLOR_GREEN);
    }
    char progress_label[20];
    (void)snprintf(progress_label, sizeof(progress_label),
                   "%u%%", (unsigned)progress);
    draw_centered_text(pixels, stride, 12, 96, 296,
                       progress_label, COLOR_WHITE, 5U);

    const char *const class_name =
        shell->runtime.file_transfer_class ==
                CONSOLE_FILE_TRANSFER_CLASS_P4G
            ? "P4G"
            : shell->runtime.file_transfer_class ==
                    CONSOLE_FILE_TRANSFER_CLASS_EXCHANGE
                ? "EXCHANGE"
                : shell->runtime.file_transfer_class == CONSOLE_FILE_TRANSFER_CLASS_P4R
                    ? "P4R"
                    : shell->runtime.file_transfer_class == CONSOLE_FILE_TRANSFER_CLASS_P4CART
                        ? "P4CART" : "NONE";
    char detail[48];
    (void)snprintf(
        detail, sizeof(detail), "%s %s  %lu/%lu BYTES",
        shell->runtime.file_transfer_direction ==
                CONSOLE_FILE_TRANSFER_DOWNLOAD
            ? "PULL" : shell->runtime.file_transfer_direction ==
                    CONSOLE_FILE_TRANSFER_UPLOAD
                ? "PUSH" : "IDLE",
        class_name,
        (unsigned long)shell->runtime.file_transfer_bytes,
        (unsigned long)shell->runtime.file_transfer_total_bytes);
    draw_text(pixels, stride, 12, 115, detail,
              COLOR_WHITE, 1U, 47U);
    draw_text(pixels, stride, 12, 130,
              shell->runtime.file_transfer_name[0] != '\0'
                  ? shell->runtime.file_transfer_name : "NO ACTIVE FILE",
              COLOR_YELLOW, 1U, 39U);

    fill_rect(pixels, stride, 8, 147, 304, 45, COLOR_PANEL);
    outline_rect(pixels, stride, 8, 147, 304, 45, COLOR_GROUP);
    draw_text(pixels, stride, 14, 153,
              "MAC TOOL: scripts/p4-transfer.py",
              COLOR_CYAN, 1U, 37U);
    draw_text(pixels, stride, 14, 166,
              "P4G -> /GAMES   FILES -> /TRANSFER",
              COLOR_WHITE, 1U, 42U);
    draw_text(pixels, stride, 14, 179,
              "H1 LINK; GAME MULTIPLAYER IS ISOLATED",
              COLOR_MUTED, 1U, 43U);
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
        "INVALID", "1 BABY", "2 EASY", "3 NORMAL", "4 ULTRA", "5 NIGHTMARE!",
    };
    return skill < sizeof(names) / sizeof(names[0])
        ? names[skill] : names[0];
}

static const char *multiplayer_map_title(
    bool chex_quest, uint8_t episode, uint8_t map)
{
    static const char *const chex_episode_one[] = {
        "", "LANDING ZONE", "STORAGE FACILITY", "EXPERIMENTAL LAB",
        "ARBORETUM", "CAVERNS OF BAZOIK",
    };
    static const char *const episode_one[] = {
        "", "HANGAR", "NUCLEAR PLANT", "TOXIN REFINERY",
        "COMMAND CONTROL", "PHOBOS LAB", "CENTRAL PROCESSING",
        "COMPUTER STATION", "PHOBOS ANOMALY", "MILITARY BASE",
    };
    if (episode != 1U) {
        return "";
    }
    if (chex_quest) {
        return map < sizeof(chex_episode_one) /
                sizeof(chex_episode_one[0])
            ? chex_episode_one[map] : "";
    }
    return map < sizeof(episode_one) / sizeof(episode_one[0])
        ? episode_one[map] : "";
}

static void draw_multiplayer_option(
    const console_shell_t *shell,
    uint16_t *pixels,
    size_t stride,
    console_multiplayer_option_t option,
    const char *label,
    const char *value)
{
    const multiplayer_option_layout_t layout =
        multiplayer_option_layout(
            multiplayer_uses_inline_lobby_panel(shell), option);
    if (layout.width == 0U || layout.height == 0U) {
        return;
    }
    const bool selected = shell->multiplayer_selected_row == (size_t)option;
    const bool pressed = shell->press_active &&
        (shell->pressed_index ==
             MULTIPLAYER_OPTION_MINUS_CONTROL_BASE + (size_t)option ||
         shell->pressed_index ==
             MULTIPLAYER_OPTION_PLUS_CONTROL_BASE + (size_t)option);
    fill_rect(pixels, stride, (int)layout.left, (int)layout.top,
              (int)layout.width, (int)layout.height,
              pressed ? COLOR_PANEL_PRESSED : COLOR_PANEL);
    outline_rect(pixels, stride, (int)layout.left, (int)layout.top,
                 (int)layout.width, (int)layout.height,
                 selected ? COLOR_YELLOW : COLOR_GROUP);
    if (layout.compact) {
        char compact[20];
        (void)snprintf(compact, sizeof(compact), "%s %s", label, value);
        if (shell->runtime.multiplayer_settings_editable) {
            draw_text(pixels, stride, (int)layout.left + 4,
                      (int)layout.top + 3, "<", COLOR_CYAN, 1U, 1U);
            draw_text(pixels, stride,
                      (int)(layout.left + layout.width) - 9,
                      (int)layout.top + 3, ">", COLOR_CYAN, 1U, 1U);
        }
        draw_centered_text(
            pixels, stride, (int)layout.left + 10, (int)layout.top + 3,
            (int)layout.width - 20, compact,
            shell->runtime.multiplayer_settings_editable
                ? COLOR_GREEN : COLOR_MUTED,
            14U);
        return;
    }
    draw_text(pixels, stride, (int)layout.left + 4, (int)layout.top + 3,
              label, selected ? COLOR_WHITE : COLOR_MUTED, 1U, 12U);
    if (shell->runtime.multiplayer_settings_editable) {
        draw_text(pixels, stride, (int)layout.left + 95,
                  (int)layout.top + 3, "<", COLOR_CYAN, 1U, 1U);
        draw_text(pixels, stride,
                  (int)(layout.left + layout.width) - 12,
                  (int)layout.top + 3, ">", COLOR_CYAN, 1U, 1U);
    }
    draw_text(pixels, stride, (int)layout.left + 108,
              (int)layout.top + 3, value,
              shell->runtime.multiplayer_settings_editable
                  ? COLOR_GREEN : COLOR_MUTED,
              1U, 30U);
}

static void draw_multiplayer_role(const console_shell_t *shell,
                                  uint16_t *pixels, size_t stride)
{
    const bool ble = shell->runtime.multiplayer_transport_kind == 1U;
    draw_text(pixels, stride, 8, 37, "MULTIPLAYER",
              COLOR_WHITE, 2U, 11U);
    draw_centered_text(pixels, stride, 8, 55, 304,
                       "CHOOSE A ROLE", COLOR_CYAN, 13U);

    const bool host_selected = shell->multiplayer_role_selection == 0U;
    const bool join_selected = !host_selected;
    const bool host_pressed = shell->press_active &&
        shell->pressed_index == MULTIPLAYER_ROLE_HOST_CONTROL;
    const bool join_pressed = shell->press_active &&
        shell->pressed_index == MULTIPLAYER_ROLE_JOIN_CONTROL;
    bevel_rect(pixels, stride, MULTIPLAYER_ROLE_HOST_LEFT,
               MULTIPLAYER_ROLE_TOP, MULTIPLAYER_ROLE_WIDTH,
               MULTIPLAYER_ROLE_HEIGHT, COLOR_PANEL, host_pressed);
    bevel_rect(pixels, stride, MULTIPLAYER_ROLE_JOIN_LEFT,
               MULTIPLAYER_ROLE_TOP, MULTIPLAYER_ROLE_WIDTH,
               MULTIPLAYER_ROLE_HEIGHT, COLOR_PANEL, join_pressed);
    if (host_selected) {
        outline_rect(pixels, stride, MULTIPLAYER_ROLE_HOST_LEFT,
                     MULTIPLAYER_ROLE_TOP, MULTIPLAYER_ROLE_WIDTH,
                     MULTIPLAYER_ROLE_HEIGHT, COLOR_YELLOW);
    }
    if (join_selected) {
        outline_rect(pixels, stride, MULTIPLAYER_ROLE_JOIN_LEFT,
                     MULTIPLAYER_ROLE_TOP, MULTIPLAYER_ROLE_WIDTH,
                     MULTIPLAYER_ROLE_HEIGHT, COLOR_YELLOW);
    }
    draw_text(pixels, stride, MULTIPLAYER_ROLE_HOST_LEFT + 49,
              MULTIPLAYER_ROLE_TOP + 15, "HOST",
              host_selected ? COLOR_YELLOW : COLOR_WHITE, 2U, 4U);
    draw_centered_text(
        pixels, stride, MULTIPLAYER_ROLE_HOST_LEFT + 4,
        MULTIPLAYER_ROLE_TOP + 49, MULTIPLAYER_ROLE_WIDTH - 8,
        "SET UP + OPEN ROOM",
        host_selected ? COLOR_WHITE : COLOR_MUTED, 18U);
    draw_text(pixels, stride, MULTIPLAYER_ROLE_JOIN_LEFT + 49,
              MULTIPLAYER_ROLE_TOP + 15, "JOIN",
              join_selected ? COLOR_YELLOW : COLOR_WHITE, 2U, 4U);
    draw_centered_text(
        pixels, stride, MULTIPLAYER_ROLE_JOIN_LEFT + 4,
        MULTIPLAYER_ROLE_TOP + 49, MULTIPLAYER_ROLE_WIDTH - 8,
        "FIND OPEN GAMES",
        join_selected ? COLOR_WHITE : COLOR_MUTED, 15U);

    char link[20];
    (void)snprintf(link, sizeof(link), "CURRENT LINK: %s",
                   ble ? "BLE" : (shell->runtime.multiplayer_transport_kind==2U ? "WI-FI" : "WIRED"));
    draw_centered_text(pixels, stride, 8, 169, 304, link,
                       ble ? COLOR_GREEN : COLOR_WHITE, 19U);
    draw_centered_text(pixels, stride, 8, 184, 304,
                       "A / TAP TO CONTINUE", COLOR_MUTED, 21U);
}

static void draw_multiplayer_join(const console_shell_t *shell,
                                  uint16_t *pixels, size_t stride)
{
    const bool ble = shell->runtime.multiplayer_transport_kind == 1U;
    draw_text(pixels, stride, 8, 37, "JOIN GAME",
              COLOR_WHITE, 2U, 9U);
    draw_text(pixels, stride, 241, 39,
              shell->runtime.multiplayer_transport_ready
                  ? "READY" : "START",
              shell->runtime.multiplayer_transport_ready
                  ? COLOR_GREEN : COLOR_YELLOW,
              1U, 7U);

    draw_multiplayer_option(
        shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_TRANSPORT,
        "LINK", ble ? "BLE" : (shell->runtime.multiplayer_transport_kind==2U ? "LOCAL WI-FI" : "WIRED AUTO"));
    draw_text(pixels, stride, 12, 75, "GAME", COLOR_CYAN, 1U, 4U);
    draw_text(pixels, stride, 116, 75, "ROOM ID", COLOR_CYAN, 1U, 7U);
    draw_text(pixels, stride, 226, 75, "PLAY", COLOR_CYAN, 1U, 4U);
    draw_text(pixels, stride, 275, 75, "SIG", COLOR_CYAN, 1U, 3U);
    fill_rect(pixels, stride, 8, 82, 304, 1, COLOR_GROUP);

    const size_t count = shell->runtime.multiplayer_lobby_count <
            CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX
        ? shell->runtime.multiplayer_lobby_count
        : CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX;
    if (count == 0U) {
        draw_centered_text(
            pixels, stride, 8, 119, 304,
            shell->runtime.multiplayer_lobby_scanning
                ? "SEARCHING FOR OPEN GAMES..." : "NO OPEN GAMES",
            shell->runtime.multiplayer_lobby_scanning
                ? COLOR_YELLOW : COLOR_MUTED,
            27U);
    }
    for (size_t index = 0U; index < count; ++index) {
        const int top = MULTIPLAYER_JOIN_ROOMS_TOP +
            (int)index * MULTIPLAYER_JOIN_ROOM_HEIGHT;
        const bool active =
            shell->runtime.multiplayer_lobby_selection == index + 1U;
        const bool focused = shell->multiplayer_selected_row ==
            CONSOLE_MULTIPLAYER_OPTION_LOBBY && active;
        const bool pressed = shell->press_active &&
            shell->pressed_index ==
                MULTIPLAYER_JOIN_ROOM_CONTROL_BASE + index;
        fill_rect(pixels, stride, MULTIPLAYER_OPTION_LEFT, top,
                  MULTIPLAYER_OPTION_WIDTH,
                  MULTIPLAYER_JOIN_ROOM_HEIGHT - 1,
                  pressed ? COLOR_PANEL_PRESSED : COLOR_PANEL);
        outline_rect(pixels, stride, MULTIPLAYER_OPTION_LEFT, top,
                     MULTIPLAYER_OPTION_WIDTH,
                     MULTIPLAYER_JOIN_ROOM_HEIGHT - 1,
                     focused ? COLOR_WHITE
                             : active ? COLOR_YELLOW : COLOR_GROUP);
        const console_multiplayer_lobby_display_t *const lobby =
            &shell->runtime.multiplayer_lobbies[index];
        draw_text(pixels, stride, 14, top + 6,
                  lobby->game_title[0] == '\0'
                      ? "UNKNOWN" : lobby->game_title,
                  !lobby->game_available
                      ? COLOR_RED : active ? COLOR_YELLOW : COLOR_WHITE,
                  1U, 16U);
        char room[12];
        (void)snprintf(
            room, sizeof(room), "#%08lX",
            (unsigned long)lobby->session_id);
        draw_text(pixels, stride, 116, top + 6, room,
                  active ? COLOR_YELLOW : COLOR_WHITE, 1U, 9U);
        char players[8];
        (void)snprintf(
            players, sizeof(players), "%u/%u",
            (unsigned)lobby->players_present,
            (unsigned)lobby->player_capacity);
        draw_text(pixels, stride, 232, top + 6, players,
                  active ? COLOR_YELLOW : COLOR_WHITE, 1U, 3U);
        unsigned signal = 0U;
        if (lobby->rssi == 0 || lobby->rssi >= -55) {
            signal = 4U;
        } else if (lobby->rssi >= -67) {
            signal = 3U;
        } else if (lobby->rssi >= -78) {
            signal = 2U;
        } else {
            signal = 1U;
        }
        char signal_text[4];
        (void)snprintf(signal_text, sizeof(signal_text), "%u/4", signal);
        draw_text(pixels, stride, 274, top + 6, signal_text,
                  signal >= 2U ? COLOR_GREEN : COLOR_RED, 1U, 3U);
    }

    const bool launch_selected = shell->multiplayer_selected_row ==
        CONSOLE_MULTIPLAYER_OPTION_COUNT;
    const bool launch_enabled = shell->runtime.multiplayer_game_ready &&
        !shell->runtime.multiplayer_launch_syncing &&
        shell->runtime.multiplayer_lobby_phase ==
            CONSOLE_MULTIPLAYER_LOBBY_BROWSING &&
        shell->runtime.multiplayer_lobby_action_enabled &&
        shell->runtime.multiplayer_lobby_selection > 0U;
    const bool launch_pressed = shell->press_active &&
        shell->pressed_index == MULTIPLAYER_LAUNCH_CONTROL;
    bevel_rect(pixels, stride, MULTIPLAYER_LAUNCH_LEFT,
               MULTIPLAYER_LAUNCH_TOP, MULTIPLAYER_LAUNCH_WIDTH,
               MULTIPLAYER_LAUNCH_HEIGHT,
               launch_enabled ? COLOR_YELLOW : COLOR_FACE,
               launch_pressed);
    if (launch_selected) {
        outline_rect(pixels, stride, MULTIPLAYER_LAUNCH_LEFT,
                     MULTIPLAYER_LAUNCH_TOP, MULTIPLAYER_LAUNCH_WIDTH,
                     MULTIPLAYER_LAUNCH_HEIGHT,
                     launch_enabled ? COLOR_WHITE : COLOR_YELLOW);
    }
    const char *label = "SELECT A ROOM";
    if (shell->runtime.multiplayer_lobby_phase ==
        CONSOLE_MULTIPLAYER_LOBBY_JOINING) {
        label = "CONNECTING TO ROOM...";
    } else if (shell->runtime.multiplayer_lobby_phase ==
               CONSOLE_MULTIPLAYER_LOBBY_CONNECTED) {
        label = "JOINED - WAITING FOR HOST";
    } else if (shell->runtime.multiplayer_lobby_scanning) {
        label = "SEARCHING FOR OPEN ROOMS...";
    } else if (shell->runtime.multiplayer_lobby_selection > 0U) {
        const size_t selected =
            shell->runtime.multiplayer_lobby_selection - 1U;
        label = selected < count &&
                !shell->runtime.multiplayer_lobbies[selected].game_available
            ? "GAME NOT INSTALLED" : "JOIN SELECTED";
    }
    draw_centered_text(pixels, stride, MULTIPLAYER_LAUNCH_LEFT,
                       MULTIPLAYER_LAUNCH_TOP + 7,
                       MULTIPLAYER_LAUNCH_WIDTH, label,
                       launch_enabled ? COLOR_BLACK : COLOR_DARK, 31U);
}

static void draw_multiplayer_host(const console_shell_t *shell,
                                  uint16_t *pixels, size_t stride)
{
    const bool ble = shell->runtime.multiplayer_transport_kind == 1U;
    draw_text(pixels, stride, 8, 37, "HOST A GAME",
              COLOR_WHITE, 2U, 11U);
    const char *const link_state =
        shell->runtime.multiplayer_transport_ready
            ? "READY"
            : shell->runtime.multiplayer_transport_starting
                ? "START" : "OFF";
    char link[16];
    (void)snprintf(link, sizeof(link), "%s %s",
                   ble ? "BLE" : (shell->runtime.multiplayer_transport_kind==2U ? "WI-FI" : "WIRED"), link_state);
    draw_text(pixels, stride, ble ? 250 : 238, 38, link,
              shell->runtime.multiplayer_transport_ready
                  ? COLOR_GREEN : COLOR_YELLOW,
              1U, ble ? 9U : 12U);
    draw_text(pixels, stride, 244, 48,
              shell->runtime.multiplayer_peer_seen
                  ? "2/2 LINKED" : "1/2 WAITING",
              shell->runtime.multiplayer_peer_seen
                  ? COLOR_GREEN : COLOR_YELLOW,
              1U, 12U);

    const bool game_selected = shell->multiplayer_selected_row ==
        CONSOLE_MULTIPLAYER_OPTION_GAME;
    const bool game_pressed = shell->press_active &&
        shell->pressed_index ==
            MULTIPLAYER_OPTION_PLUS_CONTROL_BASE +
                CONSOLE_MULTIPLAYER_OPTION_GAME;
    fill_rect(pixels, stride, MULTIPLAYER_SIMPLE_GAME_LEFT,
              MULTIPLAYER_SIMPLE_GAME_TOP,
              MULTIPLAYER_SIMPLE_GAME_WIDTH,
              MULTIPLAYER_SIMPLE_GAME_HEIGHT,
              game_pressed ? COLOR_PANEL_PRESSED : COLOR_PANEL);
    outline_rect(pixels, stride, MULTIPLAYER_SIMPLE_GAME_LEFT,
                 MULTIPLAYER_SIMPLE_GAME_TOP,
                 MULTIPLAYER_SIMPLE_GAME_WIDTH,
                 MULTIPLAYER_SIMPLE_GAME_HEIGHT,
                 game_selected ? COLOR_YELLOW : COLOR_CYAN);
    draw_text(pixels, stride, 15, MULTIPLAYER_SIMPLE_GAME_TOP + 5,
              "GAME", COLOR_CYAN, 1U, 4U);
    const char *const game_title =
        shell->runtime.multiplayer_game_title[0] == '\0'
            ? "NO GAME" : shell->runtime.multiplayer_game_title;
    const size_t game_title_length = bounded_length(game_title, 10U);
    const int game_title_width = (int)(game_title_length * 12U);
    const int game_title_left = 82 +
        (154 - (game_title_width < 154 ? game_title_width : 154)) / 2;
    draw_text(pixels, stride, game_title_left,
              MULTIPLAYER_SIMPLE_GAME_TOP + 11,
              game_title,
              shell->runtime.multiplayer_game_ready
                  ? COLOR_WHITE : COLOR_RED,
              2U, 10U);
    draw_text(pixels, stride, 254, MULTIPLAYER_SIMPLE_GAME_TOP + 14,
              "CHANGE >",
              shell->runtime.multiplayer_settings_editable
                  ? COLOR_CYAN : COLOR_MUTED,
              1U, 8U);

    fill_rect(pixels, stride, MULTIPLAYER_SIMPLE_MATCH_LEFT,
              MULTIPLAYER_SIMPLE_MATCH_TOP,
              MULTIPLAYER_SIMPLE_MATCH_WIDTH,
              MULTIPLAYER_SIMPLE_MATCH_HEIGHT, COLOR_PANEL);
    outline_rect(pixels, stride, MULTIPLAYER_SIMPLE_MATCH_LEFT,
                 MULTIPLAYER_SIMPLE_MATCH_TOP,
                 MULTIPLAYER_SIMPLE_MATCH_WIDTH,
                 MULTIPLAYER_SIMPLE_MATCH_HEIGHT, COLOR_GROUP);
    draw_text(pixels, stride, 15, MULTIPLAYER_SIMPLE_MATCH_TOP + 5,
              "MATCH", COLOR_CYAN, 1U, 5U);
    char summary_one[48];
    char summary_two[48];
    if (shell->runtime.multiplayer_game_is_arena) {
        strcpy(summary_one,"4 PLAYER ARENA / MAP VOTING");
        if (shell->runtime.multiplayer_map<=2U)
            strcpy(summary_two,shell->runtime.multiplayer_map==2U ? "PURE HELL / ROCKETS FIRST" : "PURE HELL / SHOTGUNS FIRST");
        else (void)snprintf(summary_two,sizeof(summary_two),"DWANGO 5 / MAP%02u",(unsigned)shell->runtime.multiplayer_map-2U);
    } else if (shell->runtime.multiplayer_game_is_doom) {
        (void)snprintf(
            summary_one, sizeof(summary_one), "%s / E%uM%u / %s",
            multiplayer_mode_name(shell->runtime.multiplayer_game_mode),
            (unsigned)shell->runtime.multiplayer_episode,
            (unsigned)shell->runtime.multiplayer_map,
            multiplayer_skill_name(shell->runtime.multiplayer_skill));
        if (shell->runtime.multiplayer_time_limit_minutes == 0U) {
            (void)snprintf(
                summary_two, sizeof(summary_two),
                "MONSTERS %s / NO TIME LIMIT",
                shell->runtime.multiplayer_no_monsters ? "OFF" : "ON");
        } else {
            (void)snprintf(
                summary_two, sizeof(summary_two),
                "MONSTERS %s / %u MIN LIMIT",
                shell->runtime.multiplayer_no_monsters ? "OFF" : "ON",
                (unsigned)shell->runtime.multiplayer_time_limit_minutes);
        }
    } else if (shell->runtime.multiplayer_dice_available) {
        strcpy(summary_one, "2 PLAYERS / TAKE TURNS");
        strcpy(summary_two, shell->runtime.multiplayer_dice_enabled
            ? "DICE ACCESSORY: ON - SHARED" : "DICE ACCESSORY: OFF");
    } else {
        strcpy(summary_one, "2 PLAYERS / EXACT GAME HASH");
        strcpy(summary_two, "HOST-AUTHORITATIVE SESSION");
    }
    draw_centered_text(
        pixels, stride, 16, MULTIPLAYER_SIMPLE_MATCH_TOP + 18, 288,
        summary_one, COLOR_WHITE, 47U);
    draw_centered_text(
        pixels, stride, 16, MULTIPLAYER_SIMPLE_MATCH_TOP + 31, 288,
        summary_two, COLOR_MUTED, 47U);

    const bool settings_selected = shell->multiplayer_selected_row ==
        MULTIPLAYER_HOST_SETTINGS_ROW;
    const bool settings_pressed = shell->press_active &&
        shell->pressed_index == MULTIPLAYER_SETTINGS_CONTROL;
    bevel_rect(pixels, stride, MULTIPLAYER_SIMPLE_SETTINGS_LEFT,
               MULTIPLAYER_SIMPLE_SETTINGS_TOP,
               MULTIPLAYER_SIMPLE_SETTINGS_WIDTH,
               MULTIPLAYER_SIMPLE_SETTINGS_HEIGHT,
               COLOR_PANEL, settings_pressed);
    if (settings_selected) {
        outline_rect(pixels, stride, MULTIPLAYER_SIMPLE_SETTINGS_LEFT,
                     MULTIPLAYER_SIMPLE_SETTINGS_TOP,
                     MULTIPLAYER_SIMPLE_SETTINGS_WIDTH,
                     MULTIPLAYER_SIMPLE_SETTINGS_HEIGHT, COLOR_YELLOW);
    }
    draw_centered_text(
        pixels, stride, MULTIPLAYER_SIMPLE_SETTINGS_LEFT,
        MULTIPLAYER_SIMPLE_SETTINGS_TOP + 6,
        MULTIPLAYER_SIMPLE_SETTINGS_WIDTH,
        "MATCH SETTINGS", settings_selected ? COLOR_YELLOW : COLOR_CYAN,
        14U);

    const bool launch_selected = shell->multiplayer_selected_row ==
        CONSOLE_MULTIPLAYER_OPTION_COUNT;
    const bool launch_pressed = shell->press_active &&
        shell->pressed_index == MULTIPLAYER_LAUNCH_CONTROL;
    const bool launch_enabled = shell->runtime.multiplayer_game_ready &&
        !shell->runtime.multiplayer_launch_syncing &&
        (shell->runtime.multiplayer_can_start ||
         (shell->runtime.multiplayer_lobby_phase ==
              CONSOLE_MULTIPLAYER_LOBBY_BROWSING &&
          shell->runtime.multiplayer_lobby_action_enabled));
    bevel_rect(pixels, stride, MULTIPLAYER_LAUNCH_LEFT,
               MULTIPLAYER_LAUNCH_TOP, MULTIPLAYER_LAUNCH_WIDTH,
               MULTIPLAYER_LAUNCH_HEIGHT,
               launch_enabled ? COLOR_YELLOW : COLOR_FACE,
               launch_pressed);
    if (launch_selected) {
        outline_rect(pixels, stride, MULTIPLAYER_LAUNCH_LEFT,
                     MULTIPLAYER_LAUNCH_TOP, MULTIPLAYER_LAUNCH_WIDTH,
                     MULTIPLAYER_LAUNCH_HEIGHT,
                     launch_enabled ? COLOR_WHITE : COLOR_YELLOW);
    }
    char launch[40];
    if (shell->runtime.content_validation_running &&
        shell->runtime.multiplayer_game_is_doom) {
        (void)snprintf(
            launch, sizeof(launch), "VERIFYING GAME DATA %u%%",
            (unsigned)shell->runtime.content_validation_progress_percent);
    } else if (!shell->runtime.multiplayer_game_ready) {
        strcpy(launch, shell->runtime.multiplayer_game_is_arena ?
            "NEEDS WI-FI + ARENA PACK ON SD" : "SELECTED GAME NOT READY");
    } else if (shell->runtime.multiplayer_launch_syncing) {
        strcpy(launch, "STARTING TOGETHER...");
    } else if (shell->runtime.multiplayer_can_start) {
        (void)snprintf(launch, sizeof(launch), "START %s - A / TAP",
                       shell->runtime.multiplayer_game_title);
    } else if (shell->runtime.multiplayer_lobby_phase ==
               CONSOLE_MULTIPLAYER_LOBBY_HOSTING) {
        strcpy(launch, "ROOM OPEN - WAITING FOR PLAYER");
    } else if (shell->runtime.multiplayer_lobby_phase ==
               CONSOLE_MULTIPLAYER_LOBBY_CONNECTED) {
        strcpy(launch, "PLAYER JOINED - PREPARING...");
    } else if (shell->runtime.multiplayer_lobby_scanning) {
        strcpy(launch, "PREPARING BLE HOST...");
    } else {
        (void)snprintf(launch, sizeof(launch), "CREATE %s ROOM - A / TAP",
                       ble ? "BLE" : (shell->runtime.multiplayer_transport_kind==2U ? "WI-FI" : "WIRED"));
    }
    draw_centered_text(
        pixels, stride, MULTIPLAYER_LAUNCH_LEFT,
        MULTIPLAYER_LAUNCH_TOP + 7, MULTIPLAYER_LAUNCH_WIDTH,
        launch, launch_enabled ? COLOR_BLACK : COLOR_DARK, 34U);
}

static void draw_multiplayer(const console_shell_t *shell,
                             uint16_t *pixels, size_t stride)
{
    if (shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_ROLE) {
        draw_multiplayer_role(shell, pixels, stride);
        return;
    }
    if (shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_JOIN) {
        draw_multiplayer_join(shell, pixels, stride);
        return;
    }
    if (shell->multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_HOST) {
        draw_multiplayer_host(shell, pixels, stride);
        return;
    }
    if (shell->multiplayer_view !=
            CONSOLE_MULTIPLAYER_VIEW_HOST_SETTINGS) {
        return;
    }
    const bool ble = shell->runtime.multiplayer_transport_kind == 1U;
    const char *const link = ble
        ? "BLE" : shell->runtime.multiplayer_transport_kind==2U ? "WI-FI"
        : shell->runtime.multiplayer_route_id == 2U
            ? "WIRE"
            : shell->runtime.multiplayer_route_id == 1U ? "RELAY" : "AUTO";
    const char *const link_state = shell->runtime.multiplayer_transport_ready
        ? "READY"
        : shell->runtime.multiplayer_transport_starting ? "START" : "OFF";
    char link_status[16];
    (void)snprintf(link_status, sizeof(link_status), "%s %s",
                   link, link_state);
    draw_text(pixels, stride, 8, 35, "MATCH SETTINGS",
              COLOR_WHITE, 2U, 14U);
    draw_text(pixels, stride, 218, 36, link_status,
              shell->runtime.multiplayer_transport_ready
                  ? COLOR_GREEN : COLOR_RED,
              1U, 15U);
    draw_text(pixels, stride, 218, 45,
              shell->runtime.multiplayer_peer_seen
                  ? "2/2 LINKED" : "1/2 WAITING",
              shell->runtime.multiplayer_peer_seen
                  ? COLOR_GREEN : COLOR_YELLOW,
              1U, 15U);

    draw_text(pixels, stride, 8, 50, "CONNECTION",
              COLOR_CYAN, 1U, 10U);
    fill_rect(pixels, stride, 75, 53, 237, 1, COLOR_GROUP);
    char game[32];
    (void)snprintf(
        game, sizeof(game), "%s %u/%u",
        shell->runtime.multiplayer_game_title[0] == '\0'
            ? "NO GAME" : shell->runtime.multiplayer_game_title,
        (unsigned)shell->runtime.multiplayer_game_selection + 1U,
        (unsigned)shell->runtime.multiplayer_game_count);
    draw_multiplayer_option(
        shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_TRANSPORT,
        "LINK", ble ? "BLE" : (shell->runtime.multiplayer_transport_kind==2U ? "LOCAL WI-FI" : "WIRED AUTO"));
    draw_multiplayer_option(
        shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_GAME,
        "GAME", game);

    if (shell->runtime.multiplayer_game_is_arena) {
        draw_text(pixels,stride,8,87,"ARENA / CHANGE IN GAME BY VOTE",COLOR_CYAN,1U,32U);
        char arena_name[32];
        if (shell->runtime.multiplayer_map<=2U)
            strcpy(arena_name,shell->runtime.multiplayer_map==2U ? "PURE HELL ROCKETS" : "PURE HELL SHOTGUNS");
        else (void)snprintf(arena_name,sizeof(arena_name),"DWANGO 5 MAP%02u",(unsigned)shell->runtime.multiplayer_map-2U);
        draw_multiplayer_option(shell,pixels,stride,CONSOLE_MULTIPLAYER_OPTION_MAP,"MAP",arena_name);
        draw_text(pixels,stride,8,137,"15 SECONDS IDLE: TAKE A BREAK",COLOR_WHITE,1U,32U);
        draw_text(pixels,stride,8,151,"MOVE OR FIRE TO STAY ACTIVE",COLOR_WHITE,1U,32U);
        draw_text(pixels,stride,8,165,"USE TO RETURN WITH ZERO KILLS",COLOR_WHITE,1U,32U);
    } else if (shell->runtime.multiplayer_game_is_doom) {
        draw_text(pixels, stride, 8, 87, "MATCH SETUP",
                  COLOR_CYAN, 1U, 11U);
        fill_rect(pixels, stride, 81, 90, 231, 1, COLOR_GROUP);
        draw_text(pixels, stride, 8, 151, "ADVANCED",
                  COLOR_CYAN, 1U, 8U);
        fill_rect(pixels, stride, 63, 154, 249, 1, COLOR_GROUP);
        char map[32];
        char limit[12];
        const char *const map_title = multiplayer_map_title(
            shell->runtime.multiplayer_game_selection == 1U,
            shell->runtime.multiplayer_episode,
            shell->runtime.multiplayer_map);
        (void)snprintf(
            map, sizeof(map),
            map_title[0] == '\0' ? "E%uM%u" : "E%uM%u %s",
            (unsigned)shell->runtime.multiplayer_episode,
            (unsigned)shell->runtime.multiplayer_map, map_title);
        if (shell->runtime.multiplayer_time_limit_minutes == 0U) {
            strcpy(limit, "OFF");
        } else {
            (void)snprintf(
                limit, sizeof(limit), "%u MIN",
                (unsigned)shell->runtime.multiplayer_time_limit_minutes);
        }
        draw_multiplayer_option(
            shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_MODE,
            "MODE",
            multiplayer_mode_name(shell->runtime.multiplayer_game_mode));
        draw_multiplayer_option(
            shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_MAP,
            "MAP", map);
        draw_multiplayer_option(
            shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_SKILL,
            "SKILL", multiplayer_skill_name(
                shell->runtime.multiplayer_skill));
        draw_multiplayer_option(
            shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_MONSTERS,
            "MONSTERS",
            shell->runtime.multiplayer_no_monsters ? "OFF" : "ON");
        draw_multiplayer_option(
            shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_FAST,
            "FAST",
            shell->runtime.multiplayer_fast_monsters ? "ON" : "OFF");
        draw_multiplayer_option(
            shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_RESPAWN,
            "RESPAWN",
            shell->runtime.multiplayer_respawn_monsters ? "ON" : "OFF");
        draw_multiplayer_option(
            shell, pixels, stride, CONSOLE_MULTIPLAYER_OPTION_TIME_LIMIT,
            "LIMIT", limit);
    } else if (shell->runtime.multiplayer_dice_available) {
        draw_text(pixels, stride, 8, 87, "DICE ACCESSORY", COLOR_CYAN, 1U, 14U);
        draw_multiplayer_option(shell, pixels, stride,
            CONSOLE_MULTIPLAYER_OPTION_DICE, "DICE",
            shell->runtime.multiplayer_dice_enabled ? "ON - SHARED CORE2" : "OFF");
        draw_centered_text(pixels, stride, 16, 118, 288,
            shell->runtime.multiplayer_dice_enabled
                ? "ONE CORE2 FOR EVERY PLAYER" : "ROLL WITH TOUCH OR CONTROLLER",
            COLOR_WHITE, 40U);
        draw_centered_text(pixels, stride, 16, 134, 288,
            "AFTER START: TAP CONNECT ON CORE2", COLOR_MUTED, 40U);
        draw_centered_text(pixels, stride, 16, 150, 288,
            "THEN READY + SHAKE ON YOUR TURN", COLOR_MUTED, 40U);
    } else {
        fill_rect(pixels, stride, 8, 94, 304, 66, COLOR_PANEL);
        outline_rect(pixels, stride, 8, 94, 304, 66, COLOR_GROUP);
        draw_centered_text(pixels, stride, 16, 105, 288,
                           "HOST-AUTHORITATIVE SESSION",
                           COLOR_CYAN, 26U);
        draw_centered_text(pixels, stride, 16, 124, 288,
                           "2 PLAYERS / EXACT GAME HASH",
                           COLOR_WHITE, 28U);
        draw_centered_text(pixels, stride, 16, 143, 288,
                           "ROOM LIST FILTERED BY GAME",
                           COLOR_MUTED, 27U);
    }

    const bool launch_selected = shell->multiplayer_selected_row ==
        CONSOLE_MULTIPLAYER_OPTION_COUNT;
    const bool launch_pressed = shell->press_active &&
        shell->pressed_index == MULTIPLAYER_LAUNCH_CONTROL;
    const bool launch_enabled = true;
    const multiplayer_option_layout_t launch_layout =
        multiplayer_launch_layout(true);
    bevel_rect(pixels, stride, (int)launch_layout.left,
               (int)launch_layout.top, (int)launch_layout.width,
               (int)launch_layout.height,
               launch_enabled ? COLOR_YELLOW : COLOR_FACE,
               launch_pressed);
    if (launch_selected) {
        outline_rect(pixels, stride, (int)launch_layout.left,
                     (int)launch_layout.top, (int)launch_layout.width,
                     (int)launch_layout.height,
                     launch_enabled ? COLOR_WHITE : COLOR_YELLOW);
    }
    const char *const launch = "DONE - A / TAP";
    draw_centered_text(pixels, stride, (int)launch_layout.left,
                       (int)launch_layout.top +
                           ((int)launch_layout.height - 7) / 2,
                       (int)launch_layout.width, launch,
                       launch_enabled ? COLOR_BLACK : COLOR_DARK, 34U);
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

static void draw_os_version(const console_shell_t *shell,
                            uint16_t *pixels, size_t stride)
{
    const char *const label = P4_CONSOLE_OS_VERSION_LABEL;
    const size_t length = bounded_length(label, 16U);
    const int width = (int)(length * 6U);
    int left = CONSOLE_SHELL_LAYOUT_WIDTH - width - 4;
    int top = 11;
    uint16_t color = COLOR_WHITE;
#if CONSOLE_SHELL_NATIVE_BBS
    if (shell->page == CONSOLE_PAGE_HOME && use_bbs_launcher(shell)) {
        top = 2;
        fill_rect(pixels, stride, left - 2, top - 1,
                  width + 4, 9, COLOR_BLACK);
        color = UINT16_C(0x07ff);
    } else
#endif
    if (shell->page == CONSOLE_PAGE_HOME) {
        /* Leave the Windows 3.1 title-bar buttons unobstructed. */
        left = 218;
        top = 12;
    }
    draw_text(pixels, stride, left, top, label, color, 1U, length);
}

static void draw_storage_indicator(const console_shell_t *shell,
                                    uint16_t *pixels, size_t stride)
{
    const int left = shell->page == CONSOLE_PAGE_HOME ? 112 : 162;
    const console_shell_runtime_info_t *r = &shell->runtime;
    draw_text(pixels, stride, left, 8, r->sd_card_storage ? "SD FREE" : "INT FREE",
              COLOR_WHITE, 1U, 8U);
    char label[5] = "--";
    const bool valid = r->game_storage_space_valid && r->game_storage_kib > 0U &&
        r->game_storage_free_kib <= r->game_storage_kib;
    if (valid) {
        const unsigned percent = (unsigned)((uint64_t)r->game_storage_free_kib *
            100U / r->game_storage_kib);
        (void)snprintf(label, sizeof(label), "%u%%", percent);
    }
    draw_text(pixels, stride, left, 18, label,
              valid ? COLOR_WHITE : COLOR_YELLOW, 1U, 4U);
}

static void draw_battery_indicator(const console_shell_t *shell,
                                   uint16_t *pixels, size_t stride)
{
    /* Reserve the title-bar gap, leaving the Home window buttons and OS label
     * untouched. The same compact indicator is present on detail pages. */
    const int left = shell->page == CONSOLE_PAGE_HOME ? 160 : 210;
    const int top = 11;
    const bool readable = shell->runtime.battery_supported &&
        shell->runtime.battery_sample_valid;
    const uint8_t percent = shell->runtime.battery_percent > 100U
        ? 100U : shell->runtime.battery_percent;
    const uint16_t color = readable
        ? (percent < 10U ? COLOR_RED
            : percent < 30U ? COLOR_YELLOW : COLOR_GREEN)
        : COLOR_YELLOW;
    outline_rect(pixels, stride, left, top, 18, 10, COLOR_WHITE);
    fill_rect(pixels, stride, left + 18, top + 3, 2, 4, COLOR_WHITE);
    if (readable && percent > 0U) {
        const int fill_width = (int)(((uint32_t)14U * percent + 99U) /
                                     100U);
        fill_rect(pixels, stride, left + 2, top + 2,
                  fill_width, 6, color);
    }
    char label[5];
    if (readable) {
        (void)snprintf(label, sizeof(label), "%u%%", (unsigned)percent);
    } else {
        (void)snprintf(label, sizeof(label), "--");
    }
    draw_text(pixels, stride, left + 23, top + 2, label,
              readable ? COLOR_WHITE : color, 1U, sizeof(label) - 1U);
}

static uint64_t home_cache_hash_bytes(uint64_t hash,
                                      const void *bytes,
                                      size_t byte_count)
{
    const uint8_t *const input = bytes;
    for (size_t index = 0U; index < byte_count; ++index) {
        hash ^= input[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t home_cache_hash_text(uint64_t hash,
                                     const char *text,
                                     size_t maximum_bytes)
{
    const size_t length = bounded_length(text, maximum_bytes);
    hash = home_cache_hash_bytes(hash, &length, sizeof(length));
    if (length < maximum_bytes) {
        hash = home_cache_hash_bytes(hash, text, length);
    }
    return hash;
}

static uint64_t home_cache_signature(const console_shell_t *shell)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = home_cache_hash_bytes(hash, &shell->runtime.game_storage_space_valid,
        sizeof(shell->runtime.game_storage_space_valid));
    hash = home_cache_hash_bytes(hash, &shell->runtime.game_storage_kib,
        sizeof(shell->runtime.game_storage_kib));
    hash = home_cache_hash_bytes(hash, &shell->runtime.game_storage_free_kib,
        sizeof(shell->runtime.game_storage_free_kib));
    hash = home_cache_hash_bytes(hash, &shell->runtime.sd_card_storage,
        sizeof(shell->runtime.sd_card_storage));
    hash = home_cache_hash_bytes(
        hash, &shell->app_count, sizeof(shell->app_count));
    hash = home_cache_hash_bytes(
        hash, &shell->home_all_programs, sizeof(shell->home_all_programs));
    hash = home_cache_hash_text(
        hash, shell->home_folder_path, sizeof(shell->home_folder_path));
    for (size_t index = 0U; index < shell->app_count; ++index) {
        const console_app_descriptor_t *const app = &shell->apps[index];
        hash = home_cache_hash_bytes(hash, &app->id, sizeof(app->id));
        hash = home_cache_hash_text(
            hash, app->title, CONSOLE_SHELL_TITLE_MAX_BYTES);
        hash = home_cache_hash_text(
            hash, app->subtitle, CONSOLE_SHELL_SUBTITLE_MAX_BYTES);
        hash = home_cache_hash_text(
            hash, app->folder_path, CONSOLE_SHELL_FOLDER_PATH_MAX_BYTES);
        hash = home_cache_hash_bytes(
            hash, &app->accent_rgb565, sizeof(app->accent_rgb565));
        hash = home_cache_hash_bytes(
            hash, &app->capabilities, sizeof(app->capabilities));
        hash = home_cache_hash_bytes(hash, &app->page, sizeof(app->page));
        hash = home_cache_hash_bytes(
            hash, &app->enabled, sizeof(app->enabled));
    }
    return hash;
}

void console_shell_invalidate_native_cache(console_shell_t *shell)
{
    if (shell != NULL) {
        shell->native_update = (console_shell_native_update_t){
            .kind = CONSOLE_SHELL_NATIVE_UPDATE_FULL,
        };
        shell->native_home_cache_valid = false;
        shell->native_home_cache_pixels = 0U;
        shell->native_home_cache_stride = 0U;
    }
}

static bool native_home_cache_candidate(const console_shell_t *shell,
                                        const uint16_t *pixels,
                                        size_t stride_pixels,
                                        uint64_t signature)
{
    return shell->native_home_cache_valid &&
        shell->page == CONSOLE_PAGE_HOME &&
#if CONSOLE_SHELL_NATIVE_BBS
        !use_bbs_launcher(shell) &&
#endif
        !shell->pointer_visible &&
        shell->native_home_cache_pixels == (uintptr_t)pixels &&
        shell->native_home_cache_stride == stride_pixels &&
        shell->native_home_cache_signature == signature &&
        shell->native_home_cache_color_mode == shell->color_mode &&
        shell->native_home_cache_battery_supported ==
            shell->runtime.battery_supported &&
        shell->native_home_cache_battery_valid ==
            shell->runtime.battery_sample_valid &&
        shell->native_home_cache_battery_percent ==
            shell->runtime.battery_percent;
}

static void remember_native_home_frame(console_shell_t *shell,
                                       uint16_t *pixels,
                                       size_t stride_pixels,
                                       uint64_t signature)
{
    shell->native_home_cache_pixels = (uintptr_t)pixels;
    shell->native_home_cache_stride = stride_pixels;
    shell->native_home_cache_scroll_q16 =
        clamp_home_scroll_q16(shell, shell->home_scroll_visual_q16);
    shell->native_home_cache_scroll_row = shell->home_scroll_row;
    shell->native_home_cache_signature = signature;
    shell->native_home_cache_selected_item = shell->selected_home_item;
    shell->native_home_cache_pressed_index = shell->pressed_index;
    shell->native_home_cache_color_mode = shell->color_mode;
    shell->native_home_cache_battery_percent =
        shell->runtime.battery_percent;
    shell->native_home_cache_press_active = shell->press_active;
    shell->native_home_cache_battery_supported =
        shell->runtime.battery_supported;
    shell->native_home_cache_battery_valid =
        shell->runtime.battery_sample_valid;
    shell->native_home_cache_valid = true;
}

static void native_update_region(console_shell_t *shell,
                                 bool include_status)
{
    shell->native_update = (console_shell_native_update_t){
        .kind = CONSOLE_SHELL_NATIVE_UPDATE_REGION,
        .x = (uint16_t)layout_to_output_x(include_status ? 8 : 11),
        .y = (uint16_t)layout_to_output_y(43),
        .width = (uint16_t)(layout_to_output_x(include_status ? 313 : 312) -
            layout_to_output_x(include_status ? 8 : 11)),
        .height = (uint16_t)(layout_to_output_y(include_status ? 193 : 173) -
            layout_to_output_y(43)),
    };
}

static int native_home_scroll_pixels(int32_t visual_q16)
{
    if (visual_q16 <= 0) {
        return 0;
    }
    const uint64_t numerator =
        (uint64_t)(uint32_t)visual_q16 *
        (uint64_t)(TILE_HEIGHT + TILE_ROW_GAP) *
        (uint64_t)CONSOLE_SHELL_HEIGHT;
    const uint64_t denominator =
        (uint64_t)SCROLL_POSITION_ONE * CONSOLE_SHELL_LAYOUT_HEIGHT;
    const uint64_t rounded = (numerator + denominator / 2U) / denominator;
    return rounded > (uint64_t)INT_MAX ? INT_MAX : (int)rounded;
}

static void shift_output_rows(uint16_t *pixels,
                              size_t stride_pixels,
                              int left,
                              int top,
                              int right,
                              int bottom,
                              int shift_y,
                              uint16_t clear_color)
{
    const int height = bottom - top;
    const int width = right - left;
    const int magnitude = shift_y < 0 ? -shift_y : shift_y;
    if (height <= 0 || width <= 0 || magnitude <= 0 || magnitude >= height) {
        return;
    }
    const size_t row_bytes = (size_t)width * sizeof(*pixels);
    if (shift_y < 0) {
        for (int row = top; row < bottom - magnitude; ++row) {
            memmove(pixels + (size_t)row * stride_pixels + (size_t)left,
                    pixels + (size_t)(row + magnitude) * stride_pixels +
                        (size_t)left,
                    row_bytes);
        }
        fill_output_rect(pixels, stride_pixels, left, bottom - magnitude,
                         right, bottom, clear_color);
    } else {
        for (int row = bottom - 1; row >= top + magnitude; --row) {
            memmove(pixels + (size_t)row * stride_pixels + (size_t)left,
                    pixels + (size_t)(row - magnitude) * stride_pixels +
                        (size_t)left,
                    row_bytes);
        }
        fill_output_rect(pixels, stride_pixels, left, top,
                         right, top + magnitude, clear_color);
    }
}

#if CONFIG_P4_BOARD_M5STACK_TAB5
#include "nextgen.inc"
#endif

static bool render_rgb565_target(console_shell_t *shell,
                                 uint16_t *pixels,
                                 size_t stride_pixels,
                                 size_t width,
                                 size_t height,
                                 bool logical_target)
{
    if (shell == NULL || pixels == NULL ||
        stride_pixels < width || width == 0U || height == 0U) {
        return false;
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    return width == CONSOLE_SHELL_WIDTH && height == CONSOLE_SHELL_HEIGHT &&
        nextgen_render(shell, pixels, stride_pixels, false);
#endif
    shell->native_update = (console_shell_native_update_t){
        .kind = CONSOLE_SHELL_NATIVE_UPDATE_FULL,
    };
#if CONSOLE_SHELL_NATIVE_BBS
    if (logical_target && shell->page == CONSOLE_PAGE_HOME &&
        use_bbs_launcher(shell)) {
        return false;
    }
#else
    (void)logical_target;
#endif
    s_render_width = width;
    s_render_height = height;
    s_palette = &s_color_palettes[console_shell_color_mode(shell)];
    reset_layout_clip();
    reset_output_clip();
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
        fill_rect(pixels, stride_pixels, 0, 0,
                  CONSOLE_SHELL_LAYOUT_WIDTH,
                  CONSOLE_SHELL_LAYOUT_HEIGHT, COLOR_BLACK);
        if (shell->page != CONSOLE_PAGE_CONTROL_PANEL) draw_detail_header(shell, pixels, stride_pixels);
        switch (shell->page) {
        case CONSOLE_PAGE_CONTROL_PANEL:
            draw_control_panel(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_APPEARANCE:
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
        case CONSOLE_PAGE_FILE_TRANSFER:
            draw_file_transfer(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_TERMINAL:
            draw_terminal(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_STORAGE:
            draw_storage(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_POWER:
            draw_power(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_SENSORS:
            draw_sensors(shell, pixels, stride_pixels);
            break;
        case CONSOLE_PAGE_CONTROLLERS:
            draw_controllers(shell, pixels, stride_pixels);
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
#if CONSOLE_SHELL_NATIVE_BBS
    if (!(shell->page == CONSOLE_PAGE_HOME && use_bbs_launcher(shell))) {
        draw_storage_indicator(shell, pixels, stride_pixels);
        draw_battery_indicator(shell, pixels, stride_pixels);
    }
#else
    draw_storage_indicator(shell, pixels, stride_pixels);
    draw_battery_indicator(shell, pixels, stride_pixels);
#endif
    draw_os_version(shell, pixels, stride_pixels);
    draw_pointer(shell, pixels, stride_pixels);
    shell->dirty = false;
    ++shell->render_generation;
    const bool native_windows_home =
        width == CONSOLE_SHELL_WIDTH && height == CONSOLE_SHELL_HEIGHT &&
        shell->page == CONSOLE_PAGE_HOME &&
#if CONSOLE_SHELL_NATIVE_BBS
        !use_bbs_launcher(shell) &&
#endif
        true;
    if (native_windows_home) {
        if (shell->native_home_full_frames != UINT32_MAX) {
            ++shell->native_home_full_frames;
        }
        if (!shell->pointer_visible) {
            remember_native_home_frame(
                shell, pixels, stride_pixels, home_cache_signature(shell));
        } else {
            console_shell_invalidate_native_cache(shell);
        }
    } else {
        console_shell_invalidate_native_cache(shell);
    }
    reset_output_clip();
    s_render_width = CONSOLE_SHELL_WIDTH;
    s_render_height = CONSOLE_SHELL_HEIGHT;
    return true;
}

bool console_shell_render_rgb565(console_shell_t *shell,
                                 uint16_t *pixels,
                                 size_t stride_pixels)
{
    return render_rgb565_target(
        shell, pixels, stride_pixels,
        CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT, false);
}

bool console_shell_render_native_cached_rgb565(console_shell_t *shell,
                                                uint16_t *pixels,
                                                size_t stride_pixels)
{
    if (shell == NULL || pixels == NULL ||
        stride_pixels < CONSOLE_SHELL_WIDTH) {
        return false;
    }
#if CONFIG_P4_BOARD_M5STACK_TAB5
    return nextgen_render(shell, pixels, stride_pixels, true);
#endif
    const uint64_t signature = home_cache_signature(shell);
    if (!native_home_cache_candidate(
            shell, pixels, stride_pixels, signature)) {
        return console_shell_render_rgb565(shell, pixels, stride_pixels);
    }
    const bool press_changed =
        shell->native_home_cache_pressed_index != shell->pressed_index ||
        shell->native_home_cache_press_active != shell->press_active;
    if (press_changed &&
        (shell->native_home_cache_pressed_index == FOLDER_UP_CONTROL ||
         shell->pressed_index == FOLDER_UP_CONTROL)) {
        /* The folder-up bevel lives in the otherwise static menu strip. */
        return console_shell_render_rgb565(shell, pixels, stride_pixels);
    }

    s_render_width = CONSOLE_SHELL_WIDTH;
    s_render_height = CONSOLE_SHELL_HEIGHT;
    s_palette = &s_color_palettes[console_shell_color_mode(shell)];
    reset_layout_clip();
    reset_output_clip();

    const int grid_left = layout_to_output_x(TILE_LEFT);
    const int grid_top = layout_to_output_y(TILE_TOP);
    const int grid_right = layout_to_output_x(TILE_LEFT + GRID_WIDTH);
    const int grid_bottom = layout_to_output_y(TILE_TOP + GRID_HEIGHT);
    const int32_t previous_q16 = shell->native_home_cache_scroll_q16;
    const int32_t current_q16 = clamp_home_scroll_q16(
        shell, shell->home_scroll_visual_q16);
    const bool same_selection =
        shell->native_home_cache_selected_item ==
            shell->selected_home_item &&
        (shell->scroll_gesture ||
         (shell->native_home_cache_pressed_index == shell->pressed_index &&
          shell->native_home_cache_press_active == shell->press_active));
    const bool same_base_row =
        previous_q16 / SCROLL_POSITION_ONE ==
            current_q16 / SCROLL_POSITION_ONE;
    const int previous_scroll_pixels =
        native_home_scroll_pixels(previous_q16);
    const int current_scroll_pixels =
        native_home_scroll_pixels(current_q16);
    const int shift_y = previous_scroll_pixels - current_scroll_pixels;
    const int shift_magnitude = shift_y < 0 ? -shift_y : shift_y;
    const bool current_settled =
        current_q16 % SCROLL_POSITION_ONE == 0;
    const bool home_row_changed = shell->native_home_cache_scroll_row !=
        shell->home_scroll_row;
    /* The authored 320x200 coordinates land on a non-integral 2.4x native
     * raster. During motion, translate the cached raster by whole physical
     * pixels for stable cadence instead of independently re-quantizing every
     * glyph and bevel. At each settled row, redraw the full viewport so both
     * endpoints remain identical to the authoritative renderer and rounding
     * error can never accumulate across interactions. */
    const bool can_shift = same_selection &&
        (same_base_row || shell->scroll_gesture) &&
        !current_settled && shift_magnitude > 0 &&
        shift_magnitude < grid_bottom - grid_top;

    if (can_shift) {
        shift_output_rows(pixels, stride_pixels,
                          grid_left, grid_top, grid_right, grid_bottom,
                          shift_y, COLOR_GROUP);
        if (shift_y < 0) {
            set_output_clip(grid_left, grid_bottom - shift_magnitude,
                            grid_right, grid_bottom);
        } else {
            set_output_clip(grid_left, grid_top,
                            grid_right, grid_top + shift_magnitude);
        }
        (void)draw_home_tiles(shell, pixels, stride_pixels);
        reset_output_clip();
        if (shell->native_home_scroll_blit_frames != UINT32_MAX) {
            ++shell->native_home_scroll_blit_frames;
        }
        const uint64_t shifted =
            (uint64_t)(unsigned)(grid_right - grid_left) *
            (uint64_t)(unsigned)(grid_bottom - grid_top - shift_magnitude);
        if (UINT64_MAX - shell->native_home_shifted_pixels < shifted) {
            shell->native_home_shifted_pixels = UINT64_MAX;
        } else {
            shell->native_home_shifted_pixels += shifted;
        }
    } else if (!same_selection || previous_q16 != current_q16) {
        fill_output_rect(pixels, stride_pixels,
                         grid_left, grid_top, grid_right, grid_bottom,
                         COLOR_GROUP);
        set_output_clip(grid_left, grid_top, grid_right, grid_bottom);
        (void)draw_home_tiles(shell, pixels, stride_pixels);
        reset_output_clip();
        if (shell->native_home_dynamic_frames != UINT32_MAX) {
            ++shell->native_home_dynamic_frames;
        }
    }

    draw_scrollbar(shell, pixels, stride_pixels);
    const size_t item_count = home_item_count(shell);
    draw_home_status(shell, pixels, stride_pixels, item_count);
    shell->dirty = false;
    ++shell->render_generation;
    remember_native_home_frame(shell, pixels, stride_pixels, signature);
    native_update_region(shell, home_row_changed);
    reset_layout_clip();
    reset_output_clip();
    return true;
}

bool console_shell_get_native_update(
    const console_shell_t *shell,
    console_shell_native_update_t *update_out)
{
    if (shell == NULL || update_out == NULL) {
        return false;
    }
    *update_out = shell->native_update;
    return true;
}

bool console_shell_render_present_rgb565(console_shell_t *shell,
                                         uint16_t *pixels,
                                         size_t stride_pixels)
{
    return render_rgb565_target(
        shell, pixels, stride_pixels,
        CONSOLE_SHELL_PRESENT_WIDTH, CONSOLE_SHELL_PRESENT_HEIGHT, true);
}
