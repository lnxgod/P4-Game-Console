// SPDX-License-Identifier: LicenseRef-LORD-Permission

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lord_internal.h"
#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"

static bool lord_realm_net_directory_paging_available(void);
static uint16_t lord_realm_net_directory_offset(void);
static uint16_t lord_realm_net_directory_total(void);

/* The P4 cartridge packager compiles this canonical translation unit. */
#include "lord_logic_impl.h"
#include "lord_save_impl.h"
#include "lord_sync_impl.h"
#include "lord_realm_net_impl.h"
#include "generated/lord_title_art.h"

enum {
    ANSI_BLACK = 0x0000,
    ANSI_BLUE = 0x0015,
    ANSI_GREEN = 0x0540,
    ANSI_CYAN = 0x0555,
    ANSI_RED = 0xa800,
    ANSI_MAGENTA = 0xa815,
    ANSI_BROWN = 0xa540,
    ANSI_LIGHT_GRAY = 0xc618,
    ANSI_DARK_GRAY = 0x7bef,
    ANSI_BRIGHT_BLUE = 0x52bf,
    ANSI_BRIGHT_GREEN = 0x57ea,
    ANSI_BRIGHT_CYAN = 0x57ff,
    ANSI_BRIGHT_RED = 0xfaaa,
    ANSI_BRIGHT_MAGENTA = 0xfabf,
    ANSI_YELLOW = 0xffe0,
    ANSI_WHITE = 0xffff,
    ANSI_PANEL = 0x0842,
    ANSI_PANEL_ALT = 0x108a,
    ANSI_CONTROL = 0x4a75,
    CP437_SMILE = 0x02,
    CP437_HEART = 0x03,
    CP437_DIAMOND = 0x04,
    CP437_BULLET = 0x07,
    CP437_MUSIC = 0x0e,
    CP437_STAR = 0x0f,
    CP437_ARROW_RIGHT = 0x10,
    CP437_ARROW_UP = 0x18,
    CP437_ARROW_DOWN = 0x19,
    CP437_SHADE_LIGHT = 0xb0,
    CP437_SHADE_MEDIUM = 0xb1,
    CP437_SHADE_DARK = 0xb2,
    CP437_VERTICAL = 0xb3,
    CP437_TOP_RIGHT = 0xbf,
    CP437_BOTTOM_LEFT = 0xc0,
    CP437_HORIZONTAL = 0xc4,
    CP437_BOTTOM_RIGHT = 0xd9,
    CP437_TOP_LEFT = 0xda,
    CP437_BLOCK = 0xdb,
    CP437_DOUBLE_VERTICAL = 0xba,
    CP437_DOUBLE_TOP_RIGHT = 0xbb,
    CP437_DOUBLE_BOTTOM_RIGHT = 0xbc,
    CP437_DOUBLE_BOTTOM_LEFT = 0xc8,
    CP437_DOUBLE_TOP_LEFT = 0xc9,
    CP437_DOUBLE_HORIZONTAL = 0xcd,
};

static const char *const s_class_menu[] = {
    "Death Knight Skills",
    "Mystical Skills",
    "Thieving Skills",
};

static const char *const s_hero_style_menu[] = {
    "Hero",
    "Heroine",
};

static const char *const s_town_menu[] = {
    "The Dark Forest",
    "King Arthur's Weapons",
    "Abdul's Armour",
    "The Healer's Hut",
    "Turgon's Warrior Training",
    "The First Bank",
    "The Red Dragon Inn",
    "Challenge Other Warriors",
    "The Post Office",
    "Other Places (IGMs)",
    "The Daily News",
    "Hall of Rankings",
    "RIP Art Gallery",
    "Character Stats",
    "Skill Mastery",
    "Adventure Club Hall",
};

static const char *const s_forest_menu[] = {
    "Hunt for a creature",
    "Search for the Red Dragon",
    "Visit the healer's hut",
    "Return to town",
    "Ride to DarkCloak Tavern",
};

static const char *const s_healer_menu[] = {
    "Heal one hit point (5 ChompCoin)",
    "Heal all wounds",
    "Return to town",
};

static const char *const s_training_menu[] = {
    "Challenge your master",
    "Return to town",
};

static const char *const s_bank_menu[] = {
    "Deposit all carried ChompCoin",
    "Withdraw entire account",
    "Transfer 100 ChompCoin",
    "Return to town",
};

static const char *const s_inn_menu[] = {
    "Sleep until a new day",
    "Talk with the bartender",
    "Join the conversation",
    "Spend time with Seth Able",
    "Spend time with Violet",
    "Listen to the bard's song",
    "Play Dragon Dice",
    "Invite a warrior to spar",
    "Make an announcement",
    "Check your room",
    "Return to town",
};

static const char *const s_bartender_menu[] = {
    "Buy berry fizz (10 ChompCoin)",
    "Ask about Violet",
    "Ask about Seth Able",
    "Ask about the Red Dragon",
    "Try the friendship riddle",
    "Return to the inn",
};

static const char *const s_converse_menu[] = {
    "Read the latest conversation",
    "Add to the conversation",
    "Return to the inn",
};

static const char *const s_npc_friendship_menu[] = {
    "Talk",
    "Tell a joke",
    "Play Dragon Dice",
    "Share a gem",
    "Best-friend pact / part ways",
    "Return to the inn",
};

static const char *battle_technique_label(lord_class_t hero_class)
{
    switch (hero_class) {
    case LORD_CLASS_DEATH_KNIGHT: return "Reckless Blow";
    case LORD_CLASS_MYSTICAL: return "Mystic Renewal";
    case LORD_CLASS_THIEF: return "Shadowstep";
    }
    return "Class technique";
}

static const char *class_preview_label(size_t selection)
{
    switch (selection) {
    case LORD_CLASS_DEATH_KNIGHT:
        return "RECKLESS BLOW: HIGH RISK, HIGH POWER";
    case LORD_CLASS_MYSTICAL:
        return "MYSTIC RENEWAL: HEAL WHILE FIGHTING";
    case LORD_CLASS_THIEF:
        return "SHADOWSTEP: EVADE AND COUNTER";
    default:
        return "CHOOSE THE STYLE THAT SOUNDS FUN";
    }
}

static const char *enemy_intent_label(lord_enemy_intent_t intent)
{
    switch (intent) {
    case LORD_ENEMY_INTENT_POWER: return "POWER: GUARD COUNTERS";
    case LORD_ENEMY_INTENT_GUARD: return "GUARD: FEINT BREAKS";
    case LORD_ENEMY_INTENT_QUICK: return "QUICK: STRIKE SAFE";
    case LORD_ENEMY_INTENT_STRIKE: return "STRIKE: STEADY";
    }
    return "STRIKE: STEADY";
}

static const char *const s_player_detail_menu[] = {
    "Challenge to a duel",
    "Write a sealed letter",
    "Build friendship",
    "Edit local saying",
    "Return to player list",
};

static const char *const s_friendship_action_menu[] = {
    "Offer encouragement",
    "Share supplies (100 ChompCoin)",
    "Form team / part as friends",
    "Mentor a young hero together",
    "Return to player record",
};

static const char *const s_guild_none_menu[] = {
    "View club standings",
    "Join through a club member",
    "Found a new club",
    "Read the Adventure Club rules",
    "Return to town",
};

static const char *const s_guild_member_menu[] = {
    "Rally: Charge (beats Sneak)",
    "Rally: Ward (beats Charge)",
    "Rally: Sneak (beats Ward)",
    "Friendly clash with another club",
    "Cheer for another club",
    "View club standings",
    "Leave this club",
    "Return to town",
};

static const char *const s_igm_actions[LORD_IGM_COUNT][4] = {
    {"Wager 5 ChompCoin", "Wager 20 ChompCoin", "Read the rules", "Return"},
    {"Visit Barak's house", "Ask Barak for sugar", "Skill lesson", "Return"},
    {"Answer trivia", "Sleep at a cabin", "Join team challenge", "Return"},
    {"Search old graveyard", "Read an epitaph", "Pay your respects", "Return"},
    {"Sponsor youth supplies", "Find lost youngster", "Claim helper's horse", "Return"},
    {"Search the outhouse", "Look behind the trees", "Write on the wall", "Return"},
    {"Eat a pickle", "Read the warning", "Pray to the goddess", "Return"},
};

static const char *const s_rip_gallery_menu[] = {
    "Town Square",
    "Dark Forest",
    "Red Dragon Inn",
    "Battle Arena",
    "Red Dragon",
    "King Arthur's",
    "Abdul's Armour",
    "Healer's Hut",
    "Turgon's Gym",
    "First Bank",
    "Graveyard",
    "DarkCloak Tavern",
    "Return to town",
};

static size_t line_length(const char *text, size_t capacity)
{
    size_t length = 0U;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void line_clear(char *line, size_t capacity)
{
    if (capacity > 0U) {
        line[0] = '\0';
    }
}

static void line_append(char *line, size_t capacity, const char *text)
{
    size_t index = line_length(line, capacity);
    size_t text_index = 0U;
    while (index + 1U < capacity && text[text_index] != '\0') {
        line[index] = text[text_index];
        ++index;
        ++text_index;
    }
    if (index < capacity) {
        line[index] = '\0';
    }
}

static void line_append_u32(char *line, size_t capacity, uint32_t value)
{
    char digits[11];
    size_t count = 0U;
    do {
        digits[count] = (char)('0' + (char)(value % 10U));
        ++count;
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count > 0U) {
        char one[2];
        --count;
        one[0] = digits[count];
        one[1] = '\0';
        line_append(line, capacity, one);
    }
}

static void line_append_compact_u32(char *line, size_t capacity,
                                    uint32_t value)
{
    uint32_t divisor;
    const char *suffix;
    if (value >= UINT32_C(1000000000)) {
        divisor = UINT32_C(1000000000);
        suffix = "B";
    } else if (value >= UINT32_C(1000000)) {
        divisor = UINT32_C(1000000);
        suffix = "M";
    } else if (value >= UINT32_C(1000)) {
        divisor = UINT32_C(1000);
        suffix = "K";
    } else {
        line_append_u32(line, capacity, value);
        return;
    }

    line_append_u32(line, capacity, value / divisor);
    const uint32_t tenth = (value % divisor) / (divisor / 10U);
    if (tenth != 0U) {
        line_append(line, capacity, ".");
        line_append_u32(line, capacity, tenth);
    }
    line_append(line, capacity, suffix);
}

static void line_append_i32(char *line, size_t capacity, int32_t value)
{
    if (value < 0) {
        line_append(line, capacity, "-");
        line_append_u32(line, capacity, (uint32_t)(-(value + 1)) + 1U);
    } else {
        line_append_u32(line, capacity, (uint32_t)value);
    }
}

static void draw_text(p4_game_surface_t *surface, int x, int y,
                      const char *text, uint16_t color)
{
    p4_draw_text(surface, x, y, text, color, 1U, 50U);
}

static void draw_cp437(p4_game_surface_t *surface, int x, int y,
                       uint8_t character, uint16_t foreground,
                       uint16_t background)
{
    p4_draw_cp437_glyph(surface, x, y, character, foreground, background,
                        P4_DRAW_CP437_COMPACT_HEIGHT);
}

static void draw_ansi_box(p4_game_surface_t *surface,
                          int x, int y, int width, int height,
                          uint16_t foreground, uint16_t background,
                          bool double_line)
{
    if (width < 16 || height < 16 || width > 320 || height > 200 ||
        width % P4_DRAW_CP437_CELL_WIDTH != 0 ||
        height % P4_DRAW_CP437_COMPACT_HEIGHT != 0) {
        return;
    }
    const unsigned columns = (unsigned)width /
        P4_DRAW_CP437_CELL_WIDTH;
    const unsigned rows = (unsigned)height /
        P4_DRAW_CP437_COMPACT_HEIGHT;
    const uint8_t top_left = double_line ?
        CP437_DOUBLE_TOP_LEFT : CP437_TOP_LEFT;
    const uint8_t top_right = double_line ?
        CP437_DOUBLE_TOP_RIGHT : CP437_TOP_RIGHT;
    const uint8_t bottom_left = double_line ?
        CP437_DOUBLE_BOTTOM_LEFT : CP437_BOTTOM_LEFT;
    const uint8_t bottom_right = double_line ?
        CP437_DOUBLE_BOTTOM_RIGHT : CP437_BOTTOM_RIGHT;
    const uint8_t horizontal = double_line ?
        CP437_DOUBLE_HORIZONTAL : CP437_HORIZONTAL;
    const uint8_t vertical = double_line ?
        CP437_DOUBLE_VERTICAL : CP437_VERTICAL;
    p4_draw_fill_rect(surface, x, y, width, height, background);
    for (unsigned column = 0U; column < columns; ++column) {
        const uint8_t top = column == 0U ? top_left :
            (column + 1U == columns ? top_right : horizontal);
        const uint8_t bottom = column == 0U ? bottom_left :
            (column + 1U == columns ? bottom_right : horizontal);
        const int cell_x = x + (int)(column * P4_DRAW_CP437_CELL_WIDTH);
        draw_cp437(surface, cell_x, y, top, foreground, background);
        draw_cp437(surface, cell_x,
                   y + height - P4_DRAW_CP437_COMPACT_HEIGHT,
                   bottom, foreground, background);
    }
    for (unsigned row = 1U; row + 1U < rows; ++row) {
        const int cell_y = y +
            (int)(row * P4_DRAW_CP437_COMPACT_HEIGHT);
        draw_cp437(surface, x, cell_y, vertical, foreground, background);
        draw_cp437(surface,
                   x + width - P4_DRAW_CP437_CELL_WIDTH, cell_y,
                   vertical, foreground, background);
    }
}

static void draw_ansi_selector(p4_game_surface_t *surface,
                               int x, int y, bool selected,
                               uint16_t background)
{
    draw_cp437(surface, x, y,
               selected ? CP437_ARROW_RIGHT : (uint8_t)' ',
               ANSI_YELLOW, background);
}

static void draw_centered_text(p4_game_surface_t *surface, int y,
                               const char *text, uint16_t color,
                               int minimum_x, int maximum_x)
{
    const int width = (int)line_length(text, 50U) * 6;
    int x = (320 - width) / 2;
    if (x < minimum_x) {
        x = minimum_x;
    }
    if (x + width > maximum_x) {
        x = maximum_x - width;
    }
    draw_text(surface, x, y, text, color);
}

static void draw_caption_strip(p4_game_surface_t *surface,
                               const char *caption, uint16_t accent,
                               uint8_t left_symbol, uint8_t right_symbol)
{
    draw_ansi_box(surface, 8, 24, 304, 24, accent, ANSI_PANEL_ALT, true);
    draw_cp437(surface, 16, 32, left_symbol, accent, ANSI_PANEL_ALT);
    draw_cp437(surface, 296, 32, right_symbol, accent, ANSI_PANEL_ALT);
    draw_centered_text(surface, 33, caption, ANSI_WHITE, 32, 288);
}

static void draw_progress_bar(p4_game_surface_t *surface,
                              int x, int y, int width,
                              uint32_t current, uint32_t maximum,
                              uint16_t color)
{
    if (width < 4) {
        return;
    }
    p4_draw_fill_rect(surface, x, y, width, 7, ANSI_BLACK);
    p4_draw_rect(surface, x, y, width, 7, ANSI_DARK_GRAY);
    if (maximum == 0U || current == 0U) {
        return;
    }
    if (current > maximum) {
        current = maximum;
    }
    const uint32_t inner_width = (uint32_t)(width - 2);
    while (maximum > UINT32_MAX / inner_width) {
        current /= 2U;
        maximum = maximum / 2U + maximum % 2U;
    }
    const int filled = (int)((current * inner_width) / maximum);
    if (filled > 0) {
        p4_draw_fill_rect(surface, x + 1, y + 1, filled, 5, color);
    }
}

static void draw_scroll_status(p4_game_surface_t *surface,
                               int x, int y, int width,
                               size_t first, size_t end, size_t count,
                               uint16_t background)
{
    if (count == 0U || (first == 0U && end >= count)) {
        return;
    }
    char page[24];
    line_clear(page, sizeof(page));
    line_append_u32(page, sizeof(page), (uint32_t)first + 1U);
    line_append(page, sizeof(page), "-");
    line_append_u32(page, sizeof(page), (uint32_t)end);
    line_append(page, sizeof(page), "/");
    line_append_u32(page, sizeof(page), (uint32_t)count);
    const int page_width = (int)line_length(page, sizeof(page)) * 6 + 4;
    const int page_x = x + (width - page_width) / 2;
    p4_draw_fill_rect(surface, page_x, y + 1, page_width, 7, background);
    draw_text(surface, page_x + 2, y + 1, page, ANSI_BRIGHT_CYAN);
    if (first > 0U) {
        draw_cp437(surface, x + 8, y, CP437_ARROW_UP,
                   ANSI_YELLOW, background);
    }
    if (end < count) {
        draw_cp437(surface, x + width - 16, y, CP437_ARROW_DOWN,
                   ANSI_YELLOW, background);
    }
}

static bool has_bound_realm_actor(const lord_state_t *state)
{
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        if (state->sync_actor_id[index] != 0U) {
            return true;
        }
    }
    return false;
}

static const char *realm_context_title(const lord_state_t *state,
                                       const char *local_title,
                                       const char *offline_title,
                                       const char *mac_title)
{
    if (lord_realm_net_label() != NULL) {
        return mac_title;
    }
    return has_bound_realm_actor(state) ? offline_title : local_title;
}

static void draw_header(p4_game_surface_t *surface, const char *title)
{
    const size_t title_length = line_length(title, 50U);
    p4_draw_fill_rect(surface, 0, 0, 320, 25, ANSI_PANEL);
    for (int x = 0; x < 320; x += P4_DRAW_CP437_CELL_WIDTH) {
        draw_cp437(surface, x, 17, CP437_DOUBLE_HORIZONTAL,
                   ANSI_BRIGHT_RED, ANSI_PANEL);
    }
    draw_cp437(surface, 48, 1, CP437_DOUBLE_VERTICAL,
               ANSI_RED, ANSI_PANEL);
    draw_cp437(surface, 264, 1, CP437_DOUBLE_VERTICAL,
               ANSI_RED, ANSI_PANEL);
    draw_text(surface, 8, 8, "EXIT", ANSI_BRIGHT_CYAN);
    draw_text(surface, 276, 8, "CHOOSE", ANSI_BRIGHT_CYAN);
    if (title_length <= 26U) {
        int title_x = (320 - (int)title_length * 8) / 2;
        if (title_x < 56) {
            title_x = 56;
        }
        p4_draw_cp437_text(surface, title_x, 5,
                           (const uint8_t *)title, title_length,
                           ANSI_YELLOW, ANSI_PANEL,
                           P4_DRAW_CP437_COMPACT_HEIGHT);
    } else {
        draw_centered_text(surface, 8, title, ANSI_YELLOW, 56, 264);
    }
}

static void draw_inn_ansi_strip(p4_game_surface_t *surface)
{
    draw_ansi_box(surface, 8, 24, 304, 24,
                  ANSI_YELLOW, ANSI_PANEL_ALT, true);
    draw_cp437(surface, 18, 32, CP437_MUSIC,
               ANSI_BRIGHT_MAGENTA, ANSI_PANEL_ALT);
    draw_cp437(surface, 290, 32, CP437_MUSIC,
               ANSI_BRIGHT_MAGENTA, ANSI_PANEL_ALT);
    draw_text(surface, 39, 33, "SONGS  STORIES  DICE  FRIENDS",
              ANSI_BRIGHT_CYAN);
}

static void draw_friend_ansi_strip(p4_game_surface_t *surface,
                                   bool violet)
{
    draw_ansi_box(surface, 8, 24, 304, 32,
                  violet ? ANSI_BRIGHT_MAGENTA : ANSI_BRIGHT_GREEN,
                  ANSI_PANEL_ALT, true);
    draw_cp437(surface, 24, 32, CP437_SMILE,
               violet ? ANSI_BRIGHT_MAGENTA : ANSI_BRIGHT_GREEN,
               ANSI_PANEL_ALT);
    draw_cp437(surface, 48, 32,
               violet ? CP437_DIAMOND : CP437_MUSIC,
               ANSI_YELLOW, ANSI_PANEL_ALT);
    draw_text(surface, 72, 33,
              violet ? "VIOLET: KINDNESS MAKES HEROES" :
                       "SETH: EVERY FRIEND NEEDS A SONG",
              ANSI_WHITE);
    draw_cp437(surface, 288, 32, CP437_SMILE,
               ANSI_BRIGHT_CYAN, ANSI_PANEL_ALT);
}

static void draw_friendship_ansi_strip(p4_game_surface_t *surface,
                                       const char *name)
{
    draw_ansi_box(surface, 8, 24, 304, 24,
                  ANSI_BRIGHT_MAGENTA, ANSI_PANEL_ALT, true);
    draw_cp437(surface, 24, 32, CP437_SMILE,
               ANSI_BRIGHT_CYAN, ANSI_PANEL_ALT);
    draw_cp437(surface, 56, 32, CP437_DIAMOND,
               ANSI_YELLOW, ANSI_PANEL_ALT);
    draw_cp437(surface, 88, 32, CP437_SMILE,
               ANSI_BRIGHT_GREEN, ANSI_PANEL_ALT);
    draw_text(surface, 120, 33, name, ANSI_WHITE);
}

static void draw_dice_total(p4_game_surface_t *surface,
                            int x, int y, const char *label,
                            uint8_t value, uint16_t color)
{
    draw_ansi_box(surface, x, y, 64, 48, color, ANSI_PANEL_ALT, true);
    draw_text(surface, x + 16, y + 8, label, color);
    char number[12];
    line_clear(number, sizeof(number));
    line_append_u32(number, sizeof(number), value);
    draw_cp437(surface, x + 8, y + 28, CP437_BULLET,
               ANSI_YELLOW, ANSI_PANEL_ALT);
    draw_text(surface, x + 28, y + 27, number, ANSI_WHITE);
    draw_cp437(surface, x + 48, y + 28, CP437_BULLET,
               ANSI_YELLOW, ANSI_PANEL_ALT);
}

static void draw_recovery_ansi(p4_game_surface_t *surface)
{
    draw_ansi_box(surface, 64, 32, 192, 72,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL_ALT, true);
    draw_cp437(surface, 88, 48, CP437_STAR,
               ANSI_YELLOW, ANSI_PANEL_ALT);
    draw_cp437(surface, 224, 48, CP437_STAR,
               ANSI_YELLOW, ANSI_PANEL_ALT);
    draw_text(surface, 112, 47, "RESTING AT THE INN", ANSI_WHITE);
    for (int x = 88; x <= 224; x += 16) {
        draw_cp437(surface, x, 72,
                   x % 32 == 0 ? CP437_SHADE_MEDIUM : CP437_SHADE_LIGHT,
                   ANSI_BRIGHT_CYAN, ANSI_PANEL_ALT);
    }
}

static void draw_victory_ansi(p4_game_surface_t *surface)
{
    draw_ansi_box(surface, 32, 28, 256, 88,
                  ANSI_YELLOW, ANSI_PANEL_ALT, true);
    for (int x = 48; x < 272; x += 24) {
        draw_cp437(surface, x, 36, CP437_STAR,
                   x % 48 == 0 ? ANSI_BRIGHT_MAGENTA : ANSI_BRIGHT_CYAN,
                   ANSI_PANEL_ALT);
    }
    draw_cp437(surface, 64, 60, CP437_SMILE,
               ANSI_BRIGHT_GREEN, ANSI_PANEL_ALT);
    draw_cp437(surface, 248, 60, CP437_SMILE,
               ANSI_BRIGHT_GREEN, ANSI_PANEL_ALT);
    draw_text(surface, 88, 59, "THE REALM CHEERS!", ANSI_YELLOW);
    for (int x = 56; x < 264; x += 8) {
        draw_cp437(surface, x, 88,
                   x % 24 == 0 ? CP437_BLOCK : CP437_SHADE_MEDIUM,
                   ANSI_BRIGHT_RED, ANSI_PANEL_ALT);
    }
}

static void draw_global_status_overlay(p4_game_surface_t *surface,
                                       const lord_state_t *state)
{
    const bool sync_blocked = lord_realm_net_blocks_gameplay(state);
    if (!sync_blocked && !state->save_error) {
        return;
    }

    const bool error = state->save_error ||
        s_lord_realm_net.state == LORD_REALM_NET_CONFLICT ||
        s_lord_realm_net.state == LORD_REALM_NET_ERROR;
    const uint16_t accent = error ? ANSI_BRIGHT_RED : ANSI_BRIGHT_CYAN;
    const char *heading = "SYNCING WITH THE MAC REALM";
    const char *detail_1 = "CHECKING YOUR REALM PROGRESS";
    const char *detail_2 = "PLEASE WAIT - DO NOT POWER OFF";
    if (state->save_error) {
        heading = "SAVE ERROR";
        detail_1 = "LOCAL SAVE IS UNAVAILABLE";
        detail_2 = "EXIT AND ASK A GUIDE FOR HELP";
    } else if (s_lord_realm_net.state == LORD_REALM_NET_CONFLICT) {
        heading = "SYNC CONFLICT";
        detail_1 = "BOTH COPIES ARE PRESERVED";
        detail_2 = "EXIT AND ASK A GUIDE TO CHOOSE";
    } else if (s_lord_realm_net.state == LORD_REALM_NET_ERROR) {
        heading = "SYNC ERROR";
        detail_1 = "THE MAC HUB NEEDS ATTENTION";
        detail_2 = "EXIT AND ASK A GUIDE FOR HELP";
    }

    draw_ansi_box(surface, 40, 40, 240, 72, accent, ANSI_PANEL_ALT, true);
    draw_cp437(surface, 56, 56, error ? CP437_DIAMOND : CP437_STAR,
               accent, ANSI_PANEL_ALT);
    draw_cp437(surface, 256, 56, error ? CP437_DIAMOND : CP437_STAR,
               accent, ANSI_PANEL_ALT);
    draw_centered_text(surface, 51, heading, ANSI_YELLOW, 64, 256);
    draw_centered_text(surface, 72, detail_1, ANSI_WHITE, 52, 268);
    draw_centered_text(surface, 88, detail_2,
                       error ? ANSI_BRIGHT_CYAN : ANSI_LIGHT_GRAY,
                       52, 268);
}

static void draw_command_footer(p4_game_surface_t *surface)
{
    static const char *const labels[] = {
        "PREV", "NEXT", "BACK", "CHOOSE",
    };
    static const uint16_t accents[] = {
        ANSI_BRIGHT_CYAN, ANSI_BRIGHT_CYAN,
        ANSI_BRIGHT_MAGENTA, ANSI_YELLOW,
    };
    for (size_t index = 0U; index < 4U; ++index) {
        const int x = (int)index * 80;
        draw_ansi_box(surface, x, 176, 80, 24,
                      accents[index], ANSI_PANEL, false);
        const int label_width = (int)line_length(labels[index], 8U) * 6;
        draw_text(surface, x + (80 - label_width) / 2, 185,
                  labels[index], accents[index]);
    }
}

static void draw_menu(p4_game_surface_t *surface, const lord_state_t *state,
                      const char *const *labels, size_t count,
                      int first_y)
{
    const size_t scroll = state->menu_scroll;
    const size_t end = scroll + 7U < count ? scroll + 7U : count;
    const int visible = (int)(end - scroll);
    const int top = ((first_y - 6) / 8) * 8;
    const int height = ((visible * 12 + 15) / 8) * 8;
    draw_ansi_box(surface, 8, top, 304, height,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    int row = 0;
    for (size_t index = scroll; index < end; ++index) {
        const bool selected = index == state->selection;
        if (selected) {
            p4_draw_fill_rect(surface, 16, first_y + row * 12 - 2,
                              288, 11, ANSI_RED);
        }
        draw_ansi_selector(surface, 16, first_y + row * 12 - 2,
                           selected, selected ? ANSI_RED : ANSI_PANEL);
        draw_text(surface, 24, first_y + row * 12, labels[index],
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
    draw_scroll_status(surface, 8, top, 304, scroll, end, count,
                       ANSI_PANEL);
}

static void draw_compact_menu(p4_game_surface_t *surface,
                              const lord_state_t *state,
                              const char *const *labels, size_t count,
                              int first_y)
{
    const size_t scroll = state->menu_scroll;
    const size_t end = scroll + 7U < count ? scroll + 7U : count;
    const int visible = (int)(end - scroll);
    const int top = ((first_y - 4) / 8) * 8;
    const int height = ((visible * 10 + 13) / 8) * 8;
    draw_ansi_box(surface, 8, top, 304, height,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    int row = 0;
    for (size_t index = scroll; index < end; ++index) {
        const bool selected = index == state->selection;
        if (selected) {
            p4_draw_fill_rect(surface, 16, first_y + row * 10 - 1,
                              288, 9, ANSI_RED);
        }
        draw_ansi_selector(surface, 16, first_y + row * 10 - 1,
                           selected, selected ? ANSI_RED : ANSI_PANEL);
        draw_text(surface, 24, first_y + row * 10, labels[index],
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
    draw_scroll_status(surface, 8, top, 304, scroll, end, count,
                       ANSI_PANEL);
}

static void draw_realm_menu(p4_game_surface_t *surface,
                            const lord_state_t *state,
                            bool show_trust,
                            const char *return_label)
{
    const bool paged = lord_realm_net_directory_paging_available();
    const size_t count = LORD_REALM_PLAYER_COUNT + (paged ? 3U : 1U);
    const size_t scroll = state->menu_scroll;
    const size_t end = scroll + 7U < count ? scroll + 7U : count;
    draw_ansi_box(surface, 8, 32, 304, 96,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    int row = 0;
    for (size_t index = scroll; index < end; ++index) {
        const bool selected = index == state->selection;
        char line[52];
        line_clear(line, sizeof(line));
        if (paged && index == LORD_REALM_PLAYER_COUNT) {
            line_append(line, sizeof(line), "Previous realm page");
        } else if (paged && index == LORD_REALM_PLAYER_COUNT + 1U) {
            line_append(line, sizeof(line), "Next realm page");
        } else if (index >= LORD_REALM_PLAYER_COUNT) {
            line_append(line, sizeof(line), return_label);
        } else {
            const lord_realm_player_t *const player = &state->realm[index];
            line_append(line, sizeof(line), player->name);
            if (state->screen == LORD_SCREEN_GUILD_TARGET) {
                line_append(line, sizeof(line), " C:");
                line_append(line, sizeof(line),
                            lord_guild_name(player->guild_name_code));
            } else {
                line_append(line, sizeof(line), "  LV ");
                line_append_u32(line, sizeof(line), player->level);
                if (show_trust) {
                    line_append(line, sizeof(line), "  TRUST ");
                    line_append_u32(line, sizeof(line), player->trust);
                } else {
                    line_append(line, sizeof(line),
                                player->alive ? "  READY" : "  DEFEATED");
                }
            }
        }
        const int y = 39 + row * 11;
        if (selected) {
            p4_draw_fill_rect(surface, 16, y - 2, 288, 12, ANSI_RED);
        }
        draw_ansi_selector(surface, 16, y - 2, selected,
                           selected ? ANSI_RED : ANSI_PANEL);
        draw_text(surface, 24, y, line,
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
    draw_scroll_status(surface, 8, 32, 304, scroll, end, count,
                       ANSI_PANEL);
    if (paged) {
        char page[52];
        line_clear(page, sizeof(page));
        line_append(page, sizeof(page), "Realm roster ");
        line_append_u32(page, sizeof(page),
                        lord_realm_net_directory_total() == 0U ? 0U :
                        (uint32_t)lord_realm_net_directory_offset() + 1U);
        line_append(page, sizeof(page), "-");
        uint32_t last = (uint32_t)lord_realm_net_directory_offset() +
            LORD_REALM_PLAYER_COUNT;
        const uint32_t total = lord_realm_net_directory_total();
        if (last > total) {
            last = total;
        }
        line_append_u32(page, sizeof(page), last);
        line_append(page, sizeof(page), " of ");
        line_append_u32(page, sizeof(page), total);
        draw_text(surface, 174, 119, page, ANSI_BRIGHT_CYAN);
    }
}

static void draw_player_detail(p4_game_surface_t *surface,
                               const lord_state_t *state)
{
    if (state->selected_player >= LORD_REALM_PLAYER_COUNT) {
        draw_text(surface, 10, 31, "PLAYER RECORD UNAVAILABLE", ANSI_RED);
        return;
    }
    const lord_realm_player_t *const player =
        &state->realm[state->selected_player];
    char line[52];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), player->name);
    line_append(line, sizeof(line), "  LV ");
    line_append_u32(line, sizeof(line), player->level);
    line_append(line, sizeof(line), "  ");
    line_append(line, sizeof(line), lord_class_name(player->hero_class));
    draw_text(surface, 10, 29, line, ANSI_BRIGHT_MAGENTA);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "HP ");
    line_append_i32(line, sizeof(line), player->hit_points);
    line_append(line, sizeof(line), "/");
    line_append_i32(line, sizeof(line), player->max_hit_points);
    line_append(line, sizeof(line), "  STR ");
    line_append_i32(line, sizeof(line), player->strength);
    line_append(line, sizeof(line), "  DEF ");
    line_append_i32(line, sizeof(line), player->defense);
    draw_text(surface, 10, 42, line, ANSI_BRIGHT_CYAN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "Trust ");
    line_append_u32(line, sizeof(line), player->trust);
    line_append(line, sizeof(line), player->teamed ? "  TEAMED" : "");
    draw_cp437(surface, 10, 53, CP437_HEART,
               ANSI_BRIGHT_RED, ANSI_BLACK);
    draw_text(surface, 24, 55, line, ANSI_YELLOW);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "Adventure Club: ");
    line_append(line, sizeof(line),
                lord_guild_name(player->guild_name_code));
    draw_text(surface, 10, 67, line,
              player->guild_name_code != 0U ? ANSI_BRIGHT_GREEN :
                                               ANSI_DARK_GRAY);
    draw_text(surface, 10, 78, player->saying, ANSI_LIGHT_GRAY);
    draw_compact_menu(surface, state, s_player_detail_menu, 5U, 88);
}

static void draw_guild_crest(p4_game_surface_t *surface,
                             uint16_t name_code)
{
    const uint16_t accent = name_code == 0U ? ANSI_DARK_GRAY :
        (name_code % 3U == 0U ? ANSI_BRIGHT_GREEN :
         name_code % 3U == 1U ? ANSI_YELLOW : ANSI_BRIGHT_MAGENTA);
    draw_ansi_box(surface, 240, 28, 72, 32, accent, ANSI_PANEL_ALT, true);
    draw_cp437(surface, 248, 36, CP437_STAR, accent, ANSI_PANEL_ALT);
    draw_cp437(surface, 296, 36, CP437_STAR, accent, ANSI_PANEL_ALT);
    draw_cp437(surface, 256, 44, CP437_BOTTOM_LEFT,
               accent, ANSI_PANEL_ALT);
    draw_cp437(surface, 288, 44, CP437_BOTTOM_RIGHT,
               accent, ANSI_PANEL_ALT);
    draw_text(surface, 263, 37, "CLUB", ANSI_WHITE);
}

static void draw_guild_member_menu(p4_game_surface_t *surface,
                                   const lord_state_t *state)
{
    enum {
        GUILD_MENU_COUNT = 8,
        GUILD_MENU_VISIBLE = 3,
    };
    size_t selected = state->selection;
    if (selected >= GUILD_MENU_COUNT) {
        selected = GUILD_MENU_COUNT - 1U;
    }
    size_t first = selected > 1U ? selected - 1U : 0U;
    if (first + GUILD_MENU_VISIBLE > GUILD_MENU_COUNT) {
        first = GUILD_MENU_COUNT - GUILD_MENU_VISIBLE;
    }

    draw_ansi_box(surface, 8, 88, 304, 40,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    for (size_t row = 0U; row < GUILD_MENU_VISIBLE; ++row) {
        const size_t index = first + row;
        const bool is_selected = index == selected;
        const int y = 97 + (int)row * 8;
        if (is_selected) {
            p4_draw_fill_rect(surface, 16, y - 1, 288, 9, ANSI_RED);
        }
        draw_ansi_selector(surface, 16, y - 1, is_selected,
                           is_selected ? ANSI_RED : ANSI_PANEL);
        draw_text(surface, 24, y, s_guild_member_menu[index],
                  is_selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
    }
    draw_scroll_status(surface, 8, 88, 304, first,
                       first + GUILD_MENU_VISIBLE, GUILD_MENU_COUNT,
                       ANSI_PANEL);
}

static void draw_guild_hall(p4_game_surface_t *surface,
                            const lord_state_t *state)
{
    const lord_guild_status_t *const status = &state->guild_status;
    draw_guild_crest(surface, status->name_code);
    if (!status->supported) {
        draw_text(surface, 10, 29, "WAITING FOR CLUB STATUS",
                  ANSI_YELLOW);
        draw_text(surface, 10, 41, "Mac hub needs club upgrade.",
                  ANSI_BRIGHT_CYAN);
        draw_compact_menu(surface, state, s_guild_none_menu, 5U, 66);
        return;
    }
    if (status->guild_id == 0U) {
        draw_text(surface, 10, 29, "NO ADVENTURE CLUB YET",
                  ANSI_BRIGHT_CYAN);
        draw_text(surface, 10, 41, "Join a member or found one.",
                  ANSI_LIGHT_GRAY);
        draw_compact_menu(surface, state, s_guild_none_menu, 5U, 66);
        return;
    }

    char line[52];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), lord_guild_name(status->name_code));
    line_append(line, sizeof(line), status->role == 1U ? "  LEADER" :
                                                        "  MEMBER");
    draw_text(surface, 10, 29, line, ANSI_BRIGHT_MAGENTA);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "M");
    line_append_u32(line, sizeof(line), status->member_count);
    line_append(line, sizeof(line), "  P");
    line_append_compact_u32(line, sizeof(line), status->prestige);
    line_append(line, sizeof(line), "  S");
    line_append_compact_u32(line, sizeof(line), status->season_points);
    draw_text(surface, 10, 41, line, ANSI_YELLOW);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "Stars ");
    line_append_compact_u32(line, sizeof(line), status->banner_stars);
    line_append(line, sizeof(line), "  Quest ");
    line_append_u32(line, sizeof(line), status->quest_progress);
    line_append(line, sizeof(line), "/");
    line_append_u32(line, sizeof(line), status->quest_goal);
    draw_text(surface, 10, 53, line, ANSI_BRIGHT_GREEN);
    draw_progress_bar(surface, 160, 53, 72,
                      status->quest_progress, status->quest_goal,
                      ANSI_BRIGHT_GREEN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "W/L/D ");
    line_append_compact_u32(line, sizeof(line), status->wins);
    line_append(line, sizeof(line), "/");
    line_append_compact_u32(line, sizeof(line), status->losses);
    line_append(line, sizeof(line), "/");
    line_append_compact_u32(line, sizeof(line), status->draws);
    draw_text(surface, 10, 65, line, ANSI_BRIGHT_CYAN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "Last ");
    line_append(line, sizeof(line),
                status->last_outcome == 1U ? "W" :
                status->last_outcome == 2U ? "L" :
                status->last_outcome == 3U ? "D" : "-");
    if (status->last_outcome != 0U) {
        line_append(line, sizeof(line), ":");
        line_append(line, sizeof(line),
                    lord_guild_name(status->last_opponent_name_code));
    }
    if ((status->daily_flags & 0x08U) == 0U) {
        line_append(line, sizeof(line), "  LOCKED");
    } else {
        line_append(line, sizeof(line), "  R:");
        line_append(line, sizeof(line),
                    (status->daily_flags & 0x01U) != 0U ? "N" : "Y");
        line_append(line, sizeof(line), " C:");
        line_append(line, sizeof(line),
                    (status->daily_flags & 0x02U) != 0U ? "N" : "Y");
        line_append(line, sizeof(line), " H:");
        line_append(line, sizeof(line),
                    (status->daily_flags & 0x04U) != 0U ? "N" : "Y");
    }
    draw_text(surface, 10, 77, line, ANSI_BRIGHT_MAGENTA);
    draw_guild_member_menu(surface, state);
}

static void draw_guild_create(p4_game_surface_t *surface,
                              const lord_state_t *state)
{
    const char *labels[LORD_GUILD_NAME_COUNT + 1U];
    for (uint16_t code = 1U; code <= LORD_GUILD_NAME_COUNT; ++code) {
        labels[code - 1U] = lord_guild_name(code);
    }
    labels[LORD_GUILD_NAME_COUNT] = "Return to Adventure Club Hall";
    draw_text(surface, 10, 29, "Choose one permanent curated club name.",
              ANSI_BRIGHT_CYAN);
    draw_compact_menu(surface, state, labels,
                      LORD_GUILD_NAME_COUNT + 1U, 47);
}

static void draw_guild_standings(p4_game_surface_t *surface,
                                 const lord_state_t *state)
{
    draw_ansi_box(surface, 8, 28, 304, 88,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    for (size_t index = 0U; index < LORD_GUILD_SUMMARY_COUNT; ++index) {
        const lord_guild_summary_t *const summary =
            &state->guild_summaries[index];
        char line[52];
        line_clear(line, sizeof(line));
        if (summary->valid) {
            line_append_u32(line, sizeof(line),
                            (uint32_t)state->guild_page_offset +
                                (uint32_t)index + 1U);
            line_append(line, sizeof(line), " ");
            line_append(line, sizeof(line),
                        lord_guild_name(summary->name_code));
            line_append(line, sizeof(line), " M");
            line_append_u32(line, sizeof(line), summary->member_count);
            line_append(line, sizeof(line), " P");
            line_append_compact_u32(line, sizeof(line), summary->prestige);
            line_append(line, sizeof(line), " S");
            line_append_compact_u32(line, sizeof(line),
                                    summary->season_points);
            line_append(line, sizeof(line), " *");
            line_append_compact_u32(line, sizeof(line),
                                    summary->banner_stars);
        } else if (index == 0U) {
            line_append(line, sizeof(line),
                        state->guild_page_received &&
                                state->guild_page_total == 0U ?
                            "No Adventure Clubs yet." :
                            "Waiting for club standings...");
        }
        const int y = 39 + (int)index * 9;
        if (summary->valid) {
            p4_draw_fill_rect(surface, 16, y - 1, 288, 8,
                              index == 0U ? ANSI_PANEL_ALT : ANSI_PANEL);
            draw_cp437(surface, 16, y - 1,
                       index < 3U ? CP437_STAR : CP437_BULLET,
                       index == 0U ? ANSI_YELLOW : ANSI_BRIGHT_CYAN,
                       index == 0U ? ANSI_PANEL_ALT : ANSI_PANEL);
        }
        draw_text(surface, summary->valid ? 28 : 16, y, line,
                  summary->valid ? ANSI_LIGHT_GRAY : ANSI_DARK_GRAY);
    }
    char page[40];
    line_clear(page, sizeof(page));
    line_append(page, sizeof(page), "Clubs ");
    line_append_u32(page, sizeof(page),
                    state->guild_page_total == 0U ? 0U :
                    (uint32_t)state->guild_page_offset + 1U);
    line_append(page, sizeof(page), "-");
    uint32_t last = (uint32_t)state->guild_page_offset +
        LORD_GUILD_SUMMARY_COUNT;
    if (last > state->guild_page_total) {
        last = state->guild_page_total;
    }
    line_append_u32(page, sizeof(page), last);
    line_append(page, sizeof(page), " of ");
    line_append_u32(page, sizeof(page), state->guild_page_total);
    draw_text(surface, 10, 119, page, ANSI_BRIGHT_CYAN);
    draw_text(surface, 132, 119, "PREV", state->selection == 0U ?
              ANSI_YELLOW : ANSI_DARK_GRAY);
    draw_text(surface, 184, 119, "NEXT", state->selection == 1U ?
              ANSI_YELLOW : ANSI_DARK_GRAY);
    draw_text(surface, 236, 119, "RETURN", state->selection == 2U ?
              ANSI_YELLOW : ANSI_DARK_GRAY);
}

static void draw_mailbox(p4_game_surface_t *surface,
                         const lord_state_t *state)
{
    const size_t count = lord_menu_count(state);
    const size_t scroll = state->menu_scroll;
    const size_t end = scroll + 7U < count ? scroll + 7U : count;
    const int panel_height = (int)(end - scroll) * 11 + 8;
    const int ansi_height = ((panel_height + 7) / 8) * 8;
    draw_ansi_box(surface, 8, 40, 304, ansi_height,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    int row = 0;
    for (size_t index = scroll; index < end; ++index) {
        const bool selected = index == state->selection;
        char line[52];
        line_clear(line, sizeof(line));
        if (index < state->mail_count) {
            line_append(line, sizeof(line),
                        state->mail[index].unread ? "* " : "  ");
            line_append(line, sizeof(line), lord_mail_sender(state, index));
            line_append(line, sizeof(line), ": ");
            line_append(line, sizeof(line), lord_mail_subject(state, index));
        } else if (index == state->mail_count) {
            line_append(line, sizeof(line), "Write a new letter");
        } else {
            line_append(line, sizeof(line), "Return to town");
        }
        const int y = 44 + row * 11;
        if (selected) {
            p4_draw_fill_rect(surface, 16, y - 2, 288, 12, ANSI_RED);
        }
        draw_ansi_selector(surface, 16, y - 2, selected,
                           selected ? ANSI_RED : ANSI_PANEL);
        draw_text(surface, 24, y, line,
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
    draw_scroll_status(surface, 8, 40, 304, scroll, end, count,
                       ANSI_PANEL);
}

static void draw_mail_view(p4_game_surface_t *surface,
                           const lord_state_t *state)
{
    const size_t index = state->selected_mail;
    char line[52];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "From: ");
    line_append(line, sizeof(line), lord_mail_sender(state, index));
    draw_text(surface, 12, 32, line, ANSI_BRIGHT_CYAN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "Subject: ");
    line_append(line, sizeof(line), lord_mail_subject(state, index));
    draw_text(surface, 12, 46, line, ANSI_YELLOW);
    draw_ansi_box(surface, 8, 56, 304, 56,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    draw_text(surface, 16, 68, lord_mail_body_1(state, index), ANSI_WHITE);
    draw_text(surface, 16, 84, lord_mail_body_2(state, index),
              ANSI_LIGHT_GRAY);
    draw_text(surface, 84, 118, "A/B: RETURN TO MAILBOX", ANSI_DARK_GRAY);
}

static void draw_player_status(p4_game_surface_t *surface,
                               const lord_state_t *state, int y)
{
    char line[52];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "LV ");
    line_append_u32(line, sizeof(line), state->player.level);
    line_append(line, sizeof(line), "  HP ");
    line_append_i32(line, sizeof(line), state->player.hit_points);
    line_append(line, sizeof(line), "/");
    line_append_i32(line, sizeof(line), state->player.max_hit_points);
    line_append(line, sizeof(line), "  CHOMP ");
    line_append_u32(line, sizeof(line), state->player.gold);
    draw_text(surface, 8, y, line, ANSI_BRIGHT_CYAN);
}

static void draw_town(p4_game_surface_t *surface,
                      const lord_state_t *state)
{
    draw_ansi_box(surface, 8, 32, 200, 96,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    const size_t scroll = state->menu_scroll;
    const size_t count = sizeof(s_town_menu) / sizeof(s_town_menu[0]);
    const size_t end = scroll + 7U < count ? scroll + 7U : count;
    int row = 0;
    for (size_t index = scroll; index < end; ++index) {
        const bool selected = index == state->selection;
        const int y = 43 + row * 11;
        if (selected) {
            p4_draw_fill_rect(surface, 16, y - 2, 184, 11, ANSI_RED);
        }
        draw_ansi_selector(surface, 16, y - 2, selected,
                           selected ? ANSI_RED : ANSI_PANEL);
        draw_text(surface, 25, y, s_town_menu[index],
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
    draw_scroll_status(surface, 8, 32, 200, scroll, end, count,
                       ANSI_PANEL);

    draw_ansi_box(surface, 208, 32, 104, 96,
                  ANSI_BRIGHT_RED, ANSI_PANEL_ALT, true);
    const char *const realm_label = lord_realm_net_label();
    const char *const status_label = realm_label != NULL ? realm_label :
        (has_bound_realm_actor(state) ? "REALM OFFLINE" :
         state->save_available ? "SAVED REALM" : "LOCAL REALM");
    draw_text(surface, 218, 41,
              status_label, ANSI_YELLOW);
    for (int x = 216; x < 304; x += P4_DRAW_CP437_CELL_WIDTH) {
        draw_cp437(surface, x, 50, CP437_HORIZONTAL,
                   ANSI_RED, ANSI_PANEL_ALT);
    }
    char line[24];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "DAY ");
    line_append_u32(line, sizeof(line), state->player.day);
    draw_text(surface, 218, 57, line, ANSI_LIGHT_GRAY);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "LEVEL ");
    line_append_u32(line, sizeof(line), state->player.level);
    draw_text(surface, 218, 68, line, ANSI_BRIGHT_CYAN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "HP ");
    line_append_i32(line, sizeof(line), state->player.hit_points);
    line_append(line, sizeof(line), "/");
    line_append_i32(line, sizeof(line), state->player.max_hit_points);
    draw_text(surface, 218, 79, line, ANSI_BRIGHT_GREEN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "CHOMP ");
    line_append_u32(line, sizeof(line), state->player.gold);
    draw_text(surface, 218, 90, line, ANSI_YELLOW);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "MAIL ");
    line_append_u32(line, sizeof(line),
                    (uint32_t)lord_mail_unread_count(state));
    line_append(line, sizeof(line), " NEW");
    draw_text(surface, 218, 101, line, ANSI_BRIGHT_MAGENTA);
    line_clear(line, sizeof(line));
    if (realm_label != NULL && s_lord_realm_net.seconds_remaining != 0U) {
        line_append(line, sizeof(line), "BELL ");
        line_append_u32(line, sizeof(line),
                        s_lord_realm_net.seconds_remaining / 60U);
        line_append(line, sizeof(line), ":");
        const uint32_t seconds = s_lord_realm_net.seconds_remaining % 60U;
        if (seconds < 10U) {
            line_append(line, sizeof(line), "0");
        }
        line_append_u32(line, sizeof(line), seconds);
        draw_text(surface, 218, 112, line, ANSI_BRIGHT_GREEN);
    } else {
        line_append(line, sizeof(line), "PVP LEFT ");
        line_append_u32(line, sizeof(line), state->pvp_fights);
        draw_text(surface, 218, 112, line, ANSI_BRIGHT_RED);
    }
}

static void draw_title(p4_game_surface_t *surface, const lord_state_t *state)
{
    p4_draw_sprite_rgb565(surface, 0, 25, s_lord_title_art,
                          LORD_TITLE_ART_WIDTH, LORD_TITLE_ART_HEIGHT,
                          LORD_TITLE_ART_WIDTH, false, 0U);
    draw_ansi_box(surface, 16, 92, 288, 40,
                  ANSI_BRIGHT_RED, ANSI_BLACK, true);
    draw_text(surface, 76, 104, "P4 ANSI DOOR EDITION", ANSI_WHITE);
    const char *realm_mode = state->save_error ? "SAVE NEEDS HELP" :
        (state->save_available ?
            (lord_realm_net_label() != NULL ? "SAVED + MAC REALM" :
                                              "SAVED OFFLINE") :
            "SESSION PLAY");
    char line[52];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "A/START ENTER - ");
    line_append(line, sizeof(line), realm_mode);
    draw_centered_text(surface, 118, line, ANSI_YELLOW, 24, 296);
}

static void draw_shop(p4_game_surface_t *surface,
                      const lord_state_t *state, bool weapon_shop)
{
    const size_t count = lord_menu_count(state);
    const size_t scroll = state->menu_scroll;
    const size_t end = scroll + 7U < count ? scroll + 7U : count;
    char funds[52];
    line_clear(funds, sizeof(funds));
    line_append(funds, sizeof(funds), weapon_shop ?
                "KING ARTHUR - PURSE " : "ABDUL - PURSE ");
    line_append_u32(funds, sizeof(funds), state->player.gold);
    line_append(funds, sizeof(funds), " CHOMP");
    draw_caption_strip(surface, funds,
                       weapon_shop ? ANSI_BRIGHT_RED : ANSI_BRIGHT_CYAN,
                       weapon_shop ? CP437_STAR : CP437_DIAMOND,
                       weapon_shop ? CP437_STAR : CP437_DIAMOND);
    draw_ansi_box(surface, 8, 48, 304, 80,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    int row = 0;
    for (size_t index = scroll; index < end; ++index) {
        const bool selected = index == state->selection;
        char line[52];
        line_clear(line, sizeof(line));
        if (index == 16U) {
            line_append(line, sizeof(line), "Return to town");
        } else {
            const lord_item_t *const item = weapon_shop ?
                lord_weapon(index) : lord_armor(index);
            if (item != NULL) {
                line_append(line, sizeof(line), item->name);
                line_append(line, sizeof(line), "  ");
                line_append_u32(line, sizeof(line), item->price);
                line_append(line, sizeof(line), " CHOMP");
            }
        }
        const int y = 56 + row * 9;
        if (selected) {
            p4_draw_fill_rect(surface, 16, y - 1, 288, 9, ANSI_BLUE);
        }
        draw_ansi_selector(surface, 16, y - 1, selected,
                           selected ? ANSI_BLUE : ANSI_PANEL);
        draw_text(surface, 24, y, line,
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
    draw_scroll_status(surface, 8, 48, 304, scroll, end, count,
                       ANSI_PANEL);
}

static void draw_stats(p4_game_surface_t *surface,
                       const lord_state_t *state)
{
    char line[52];
    p4_draw_fill_rect(surface, 8, 28, 304, 102, ANSI_PANEL);
    p4_draw_rect(surface, 8, 28, 304, 102, ANSI_BLUE);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), state->player.name);
    line_append(line, sizeof(line), "  ");
    line_append(line, sizeof(line), lord_hero_style_name(state->player.hero_style));
    line_append(line, sizeof(line), "  ");
    line_append(line, sizeof(line), lord_class_name(state->player.hero_class));
    draw_text(surface, 12, 31, line, ANSI_BRIGHT_MAGENTA);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "LV ");
    line_append_u32(line, sizeof(line), state->player.level);
    line_append(line, sizeof(line), "  DAY ");
    line_append_u32(line, sizeof(line), state->player.day);
    line_append(line, sizeof(line), "  DRAGONS ");
    line_append_u32(line, sizeof(line), state->player.dragon_kills);
    draw_text(surface, 12, 44, line, ANSI_WHITE);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "HP ");
    line_append_i32(line, sizeof(line), state->player.hit_points);
    line_append(line, sizeof(line), "/");
    line_append_i32(line, sizeof(line), state->player.max_hit_points);
    line_append(line, sizeof(line), "  STR ");
    line_append_i32(line, sizeof(line), state->player.strength);
    line_append(line, sizeof(line), "  DEF ");
    line_append_i32(line, sizeof(line), state->player.defense);
    draw_text(surface, 12, 57, line, ANSI_BRIGHT_CYAN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "CHOMP ");
    line_append_u32(line, sizeof(line), state->player.gold);
    line_append(line, sizeof(line), "  VAULT ");
    line_append_u32(line, sizeof(line), state->player.bank);
    draw_text(surface, 12, 70, line, ANSI_YELLOW);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "XP ");
    line_append_u32(line, sizeof(line), state->player.experience);
    line_append(line, sizeof(line), "  CHARM ");
    line_append_u32(line, sizeof(line), state->player.charm);
    line_append(line, sizeof(line), "  GEMS ");
    line_append_u32(line, sizeof(line), state->player.gems);
    draw_text(surface, 12, 83, line, ANSI_BRIGHT_GREEN);
    const lord_item_t *const weapon = lord_weapon(state->player.weapon);
    const lord_item_t *const armor = lord_armor(state->player.armor);
    draw_text(surface, 12, 96, weapon != NULL ? weapon->name : "Fists",
              ANSI_LIGHT_GRAY);
    draw_text(surface, 156, 96, armor != NULL ? armor->name : "Nothing",
              ANSI_LIGHT_GRAY);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "FIGHTS ");
    line_append_u32(line, sizeof(line), state->player.forest_fights);
    line_append(line, sizeof(line), "  YOUTH ");
    line_append_u32(line, sizeof(line), state->player.young_heroes_helped);
    line_append(line, sizeof(line), "  TEAM ");
    line_append_u32(line, sizeof(line), state->player.friendship_badges);
    line_append(line, sizeof(line), "  GUARD +");
    line_append_i32(line, sizeof(line), friendship_guard_bonus(state));
    draw_text(surface, 12, 109, line, ANSI_BRIGHT_CYAN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), state->player.horse ? "HORSE " : "");
    line_append(line, sizeof(line), state->player.fairy ? "FAIRY " : "");
    line_append(line, sizeof(line), state->player.fairy_lore ? "LORE " : "");
    line_append(line, sizeof(line), state->player.amulet ? "AMULET " : "");
    line_append(line, sizeof(line), state->player.high_spirits ? "SPIRIT " : "");
    line_append(line, sizeof(line), " PVP ");
    line_append_u32(line, sizeof(line), state->player.pvp_wins);
    line_append(line, sizeof(line), "-");
    line_append_u32(line, sizeof(line), state->player.pvp_losses);
    draw_text(surface, 12, 122, line, ANSI_BRIGHT_GREEN);
}

static uint32_t positive_hit_points(int32_t hit_points)
{
    return hit_points > 0 ? (uint32_t)hit_points : 0U;
}

static uint16_t intent_accent(lord_enemy_intent_t intent)
{
    switch (intent) {
    case LORD_ENEMY_INTENT_POWER: return ANSI_BRIGHT_RED;
    case LORD_ENEMY_INTENT_GUARD: return ANSI_BRIGHT_BLUE;
    case LORD_ENEMY_INTENT_QUICK: return ANSI_YELLOW;
    case LORD_ENEMY_INTENT_STRIKE: return ANSI_BRIGHT_CYAN;
    }
    return ANSI_BRIGHT_CYAN;
}

static uint8_t intent_symbol(lord_enemy_intent_t intent)
{
    switch (intent) {
    case LORD_ENEMY_INTENT_POWER: return CP437_DIAMOND;
    case LORD_ENEMY_INTENT_GUARD: return CP437_BLOCK;
    case LORD_ENEMY_INTENT_QUICK: return CP437_ARROW_RIGHT;
    case LORD_ENEMY_INTENT_STRIKE: return CP437_STAR;
    }
    return CP437_STAR;
}

typedef enum {
    LORD_BATTLE_ART_ROGUE = 0,
    LORD_BATTLE_ART_HUMAN,
    LORD_BATTLE_ART_ELF_ARCHER,
    LORD_BATTLE_ART_HOBBIT,
    LORD_BATTLE_ART_BUTCHER,
    LORD_BATTLE_ART_HERALD,
    LORD_BATTLE_ART_KNIGHT,
    LORD_BATTLE_ART_SHOGUN,
    LORD_BATTLE_ART_BARBARIAN,
    LORD_BATTLE_ART_CASTER,
    LORD_BATTLE_ART_GNOME,
    LORD_BATTLE_ART_SHAPECHANGER,
    LORD_BATTLE_ART_OGRE,
    LORD_BATTLE_ART_TROLL,
    LORD_BATTLE_ART_GOBLIN,
    LORD_BATTLE_ART_ORC,
    LORD_BATTLE_ART_GIANT,
    LORD_BATTLE_ART_CYCLOPS,
    LORD_BATTLE_ART_RODENT,
    LORD_BATTLE_ART_WOMBAT,
    LORD_BATTLE_ART_BOAR,
    LORD_BATTLE_ART_BEAR,
    LORD_BATTLE_ART_CANINE,
    LORD_BATTLE_ART_TWO_HEADED_CANINE,
    LORD_BATTLE_ART_WEREWOLF,
    LORD_BATTLE_ART_FELINE,
    LORD_BATTLE_ART_LION,
    LORD_BATTLE_ART_TIGER,
    LORD_BATTLE_ART_LIONTAUR,
    LORD_BATTLE_ART_HOOFED,
    LORD_BATTLE_ART_NIGHTMARE,
    LORD_BATTLE_ART_UNICORN,
    LORD_BATTLE_ART_ANTLERED,
    LORD_BATTLE_ART_FRANKENMOOSE,
    LORD_BATTLE_ART_ELEPHANT,
    LORD_BATTLE_ART_RHINO,
    LORD_BATTLE_ART_APE,
    LORD_BATTLE_ART_BIRD,
    LORD_BATTLE_ART_SERPENT,
    LORD_BATTLE_ART_WINGED_SERPENT,
    LORD_BATTLE_ART_DRAGON,
    LORD_BATTLE_ART_BABY_DRAGON,
    LORD_BATTLE_ART_REPTILE,
    LORD_BATTLE_ART_DINOSAUR,
    LORD_BATTLE_ART_INSECT,
    LORD_BATTLE_ART_SPIDER,
    LORD_BATTLE_ART_SKELETON,
    LORD_BATTLE_ART_ZOMBIE,
    LORD_BATTLE_ART_GHOST,
    LORD_BATTLE_ART_REAPER,
    LORD_BATTLE_ART_SLIME,
    LORD_BATTLE_ART_SLUG,
    LORD_BATTLE_ART_EYE,
    LORD_BATTLE_ART_CONSTRUCT,
    LORD_BATTLE_ART_EMPTY_ARMOUR,
    LORD_BATTLE_ART_GOLEM,
    LORD_BATTLE_ART_MOUNTAIN,
    LORD_BATTLE_ART_PLANT,
    LORD_BATTLE_ART_TREE_SPIRIT,
    LORD_BATTLE_ART_WINGED,
    LORD_BATTLE_ART_DEMON,
    LORD_BATTLE_ART_MEDUSA,
    LORD_BATTLE_ART_JESTER,
    LORD_BATTLE_ART_BRUTE,
    LORD_BATTLE_ART_BEAST,
    LORD_BATTLE_ART_COUNT,
} lord_battle_art_t;

typedef enum {
    LORD_CREATURE_MOTION_BLINK = 0,
    LORD_CREATURE_MOTION_BREATHE,
    LORD_CREATURE_MOTION_FLUTTER,
    LORD_CREATURE_MOTION_ATTACK_JOLT,
    LORD_CREATURE_MOTION_SPARK,
    LORD_CREATURE_MOTION_MORPH,
    LORD_CREATURE_MOTION_COUNT,
} lord_creature_motion_t;

enum {
    LORD_BATTLE_ART_ROWS = 5,
    LORD_BATTLE_ART_COLUMNS = 10,
    LORD_CREATURE_ANIMATION_WRAP_MS = 60000,
};

/* These fixed CP437 silhouettes keep every encounter readable without a
 * raster asset. Each semantic family has a distinct outline, while
 * hash-driven mirroring, eyes, texture, palette, and sigils give duplicate
 * family members deterministic individual variants. Motion assignment is
 * family-aware: silhouettes without filled body cells or living eyes never
 * receive animation modes that depend on those markers. */
static const char
    s_battle_art[LORD_BATTLE_ART_COUNT]
                [LORD_BATTLE_ART_ROWS]
                [LORD_BATTLE_ART_COLUMNS + 1] = {
    [LORD_BATTLE_ART_ROGUE] = {
        "   /^\\", "  /oo]", " /|##|/\\", "  |##|", " _/  \\_",
    },
    [LORD_BATTLE_ART_HUMAN] = {
        "   ___", "  /oo\\", "  |##|", " /|##|\\", "  /  \\",
    },
    [LORD_BATTLE_ART_ELF_ARCHER] = {
        " /\\.__./\\", "<  o  o  >", " \\_|##| )", "  /##\\/|", "  /  \\ )",
    },
    [LORD_BATTLE_ART_HOBBIT] = {
        "  .---.", " /o  o\\", " \\_==_--@", " /|##|\\", " _/  \\_",
    },
    [LORD_BATTLE_ART_BUTCHER] = {
        "  .--.", " /o  o\\ []", " |####| /", " /|####|/", "  /  \\",
    },
    [LORD_BATTLE_ART_HERALD] = {
        "  .--.", " /o  o\\__", " |  O |==>", " /|####|", "  /  \\",
    },
    [LORD_BATTLE_ART_KNIGHT] = {
        "  .==.", "  [oo] /|", " /|##|/ |", "  |##|  |", "  /  \\  |",
    },
    [LORD_BATTLE_ART_SHOGUN] = {
        " _/====\\_", " | o  o |", " /|####|\\", "  |####| /", "  /  \\ /",
    },
    [LORD_BATTLE_ART_BARBARIAN] = {
        "  _/\\_", " /o  o\\", " \\_##_/", " /|##|\\", " _/  \\_",
    },
    [LORD_BATTLE_ART_CASTER] = {
        "   /\\", "  /oo\\  *", " /####\\ /", "  |##| /", "  /  \\",
    },
    [LORD_BATTLE_ART_GNOME] = {
        "    /\\", "   /##\\", "  /o  o\\", " /####\\", "  /  \\",
    },
    [LORD_BATTLE_ART_SHAPECHANGER] = {
        "  .-\\/-.", " /o | o\\", "<##|  ##>", " /#|##|#\\", " _/    \\_",
    },
    [LORD_BATTLE_ART_OGRE] = {
        " /^^^^\\ @", "/o    o\\|", "| /\\/\\ ||", "\\>####</|", " /_/\\_\\ |",
    },
    [LORD_BATTLE_ART_TROLL] = {
        "  _/^^\\_", " /o  o  \\", "|   >   |", "\\ /####\\/", " /_/  \\_\\",
    },
    [LORD_BATTLE_ART_GOBLIN] = {
        "/\\ .--. /\\", "<  o  o  >", " \\  <>  /", " /|####|\\", "  / /\\ \\",
    },
    [LORD_BATTLE_ART_ORC] = {
        " /\\.__./\\", "<  o  o  >", " \\_>==<_/", " /|####|\\", "  /_||_\\",
    },
    [LORD_BATTLE_ART_GIANT] = {
        "  _^^^^_", " / o  o \\", "|   ##   |", "|########|", " /_/  \\_\\",
    },
    [LORD_BATTLE_ART_CYCLOPS] = {
        "  _^^^^_", " /   o   \\", "|   ##   |", "|########|", " /_/  \\_\\",
    },
    [LORD_BATTLE_ART_RODENT] = {
        " ()____()", "( o  o  )", " \\_==__/", " /####\\~~", "/_/  \\_\\",
    },
    [LORD_BATTLE_ART_WOMBAT] = {
        " ()____()", "( o  o  )", " \\_==__/", " /######\\", " /_/\\_\\",
    },
    [LORD_BATTLE_ART_BOAR] = {
        "  /\\_/\\", " / o  o \\", "|  (==)  |", "\\_>####</", " /_/\\_\\",
    },
    [LORD_BATTLE_ART_BEAR] = {
        " ()____()", " / o  o \\", "|  (==)  |", "\\_######/", " /_/  \\_\\",
    },
    [LORD_BATTLE_ART_CANINE] = {
        " /\\____/\\", "( o  o  )", " \\_vv__/", " /####\\", "/_/  \\_\\",
    },
    [LORD_BATTLE_ART_TWO_HEADED_CANINE] = {
        "/\\_/\\/\\_/\\", "(o o)(o o)", " \\v/##\\v/", "  /####\\", " _/  \\_",
    },
    [LORD_BATTLE_ART_WEREWOLF] = {
        " /\\_  _/\\", "<  o  o  >", " \\_vvvv_/", " /|####|\\", " _/ /\\ \\_",
    },
    [LORD_BATTLE_ART_FELINE] = {
        " /\\_  _/\\", "( o \\/ o )", " \\_~~~~_/", " /######\\", " ~/_/\\_\\",
    },
    [LORD_BATTLE_ART_LION] = {
        " /\\_  _/\\", "( o \\/ o )", "\\~(####)~/", " /######\\", " ~/_/\\_\\",
    },
    [LORD_BATTLE_ART_TIGER] = {
        " /\\_||_/\\", "( o \\/ o )", " \\_||||_/", " /##||##\\", " ~/_/\\_\\",
    },
    [LORD_BATTLE_ART_LIONTAUR] = {
        "   /\\", "  /oo\\", " /|##|\\", "<######>~", " / /  \\ \\",
    },
    [LORD_BATTLE_ART_HOOFED] = {
        "  /\\__/\\", " /o  o  \\", " \\_==__/", " /######\\", " / /  \\ \\",
    },
    [LORD_BATTLE_ART_NIGHTMARE] = {
        " ~/\\__/\\~", " /o  o  \\", " \\_==__/~", "~/######\\", " / /  \\ \\",
    },
    [LORD_BATTLE_ART_UNICORN] = {
        " /\\  !  /\\", "  \\_^^_/", "  /o  o\\", " /######\\", " / /  \\ \\",
    },
    [LORD_BATTLE_ART_ANTLERED] = {
        "\\_/\\  /\\_/", "  /o  o\\", "  \\_==_/", " /######\\", " / /  \\ \\",
    },
    [LORD_BATTLE_ART_FRANKENMOOSE] = {
        "\\_/\\  /\\_/", "  /o  o\\", "  \\_++_/", " /##+###\\", " / /  \\ \\",
    },
    [LORD_BATTLE_ART_ELEPHANT] = {
        " /\\____/\\", "/ o    o \\", "|  ####  |", "\\_###(\\_/", " /_/ )\\_\\",
    },
    [LORD_BATTLE_ART_RHINO] = {
        "   /\\^^", " / o  o \\", "<__####__|", "  |####|/", "  / / \\ \\",
    },
    [LORD_BATTLE_ART_APE] = {
        "  _/^^\\_", " / o  o \\", "| /####\\ |", "\\|######|/", " /_/  \\_\\",
    },
    [LORD_BATTLE_ART_BIRD] = {
        "{  /\\  }", " \\/oo\\/", " /####\\", "<######>", "  /  \\",
    },
    [LORD_BATTLE_ART_SERPENT] = {
        "  /^\\/\\", " < o  /", "  \\##\\", "   \\##\\_", " ~~\\____>",
    },
    [LORD_BATTLE_ART_WINGED_SERPENT] = {
        "{ /^\\/\\ }", " \\< o  >/", "  \\##\\", "   \\##\\_", " ~~\\____>",
    },
    [LORD_BATTLE_ART_DRAGON] = {
        "\\/ /^\\/\\_", "< o  o _>", " \\####/~~", "<######>", " / /\\ \\",
    },
    [LORD_BATTLE_ART_BABY_DRAGON] = {
        "   /^\\", "  /o o\\", " </##\\>", "  /##\\~", "  /  \\",
    },
    [LORD_BATTLE_ART_REPTILE] = {
        "  __/\\__", " / o  o \\", "<__####__>", "  \\####\\", " ~~/  \\~~",
    },
    [LORD_BATTLE_ART_DINOSAUR] = {
        " __/^^\\", "/ o  __>", "\\_####\\", " /######>", " / /  \\",
    },
    [LORD_BATTLE_ART_INSECT] = {
        " { | }", "--\\o/--", " /###\\", "--/|\\--", " / | \\",
    },
    [LORD_BATTLE_ART_SPIDER] = {
        "\\ \\ | / /", " \\(o o)/", "--/###\\--", " /|###|\\", "/ / | \\ \\",
    },
    [LORD_BATTLE_ART_SKELETON] = {
        "  .--.", "  |oo|", "  /=|=\\", "  \\=|=/", " _/   \\_",
    },
    [LORD_BATTLE_ART_ZOMBIE] = {
        "  .__.", " /o  o\\", "|  ##  |", " /####\\", " _/  \\_",
    },
    [LORD_BATTLE_ART_GHOST] = {
        "  .----.", " / o  o \\", "|  ####  |", "| ###### |", "\\/\\/\\/\\/",
    },
    [LORD_BATTLE_ART_REAPER] = {
        "  .--. __)", " / oo\\  |", " |####| |", " /####\\ |", " _/  \\_/",
    },
    [LORD_BATTLE_ART_SLIME] = {
        "  .----.", " / o  o \\", "|######|", " \\_####/", "   \\__/",
    },
    [LORD_BATTLE_ART_SLUG] = {
        "  .----.", " / o  o \\", "<__####  \\", "   \\#### )", "~~~~\\____>",
    },
    [LORD_BATTLE_ART_EYE] = {
        "   ____", " /\\    /\\", "<##(o)##>", " \\/____\\/", "   /  \\",
    },
    [LORD_BATTLE_ART_CONSTRUCT] = {
        "  [==]", " /|oo|\\", " |####|", " |####|", " /_||_\\",
    },
    [LORD_BATTLE_ART_EMPTY_ARMOUR] = {
        "  .==.", " /[  ]\\ /|", " /|####|/|", "  |####| |", "  /_||_\\ |",
    },
    [LORD_BATTLE_ART_GOLEM] = {
        "  _/\\_", " / o  o\\", "|[####]|", "|######|", " /_/\\_\\",
    },
    [LORD_BATTLE_ART_MOUNTAIN] = {
        "   /\\", "  /o \\", " /####\\", "/######\\", "_/__/\\__\\_",
    },
    [LORD_BATTLE_ART_PLANT] = {
        " \\|/  \\|/", "  \\ oo /", " --####--", "  /####\\", " _/ /\\ \\_",
    },
    [LORD_BATTLE_ART_TREE_SPIRIT] = {
        " \\|/\\|/", " / o  o \\", "| /####|", "\\|######/", " _/ /\\ \\_",
    },
    [LORD_BATTLE_ART_WINGED] = {
        "  .====.", "{ /oo\\ }", "<|####|>", " /|####|\\", "  /  \\",
    },
    [LORD_BATTLE_ART_DEMON] = {
        "{ /^\\ }", " \\ o o /", "<|####|>", " /####\\", " / /\\ \\",
    },
    [LORD_BATTLE_ART_MEDUSA] = {
        "~\\/\\/\\/~", " / o  o \\", "|  ####  |", " \\######/", "  / /\\ \\",
    },
    [LORD_BATTLE_ART_JESTER] = {
        " \\* /\\ */", "  \\o o/", "  /##\\", " /####\\", "  /  \\",
    },
    [LORD_BATTLE_ART_BRUTE] = {
        "  _^^_", " /o  o\\", "/|####|\\", " |####|", " /_/\\_\\",
    },
    [LORD_BATTLE_ART_BEAST] = {
        " /\\____/\\", "( o  o  )", " \\_^^__/", " /####\\", "/_/  \\_\\",
    },
};

/* One entry per generated lord_monsters.h row. Keeping this table in the
 * same order makes the visual review auditable and prevents broad substring
 * rules from silently turning an ogre, spider, or horse into a generic body. */
typedef uint32_t lord_forest_art_profile_t;

#define LORD_FOREST_ART_COLOR(kind, color) \
    ((lord_forest_art_profile_t)(kind) | ((uint32_t)(color) << 8U))

static const lord_forest_art_profile_t
    s_forest_battle_art[LORD_MONSTER_COUNT] = {
    LORD_BATTLE_ART_ROGUE,       /*   0 Small Thief */
    LORD_BATTLE_ART_ROGUE,       /*   1 Rude Rascal */
    LORD_BATTLE_ART_HUMAN,       /*   2 Wandering Elder */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_RODENT, ANSI_BRIGHT_GREEN),
                                    /*   3 Large Green Rat */
    LORD_BATTLE_ART_BOAR,        /*   4 Wild Boar */
    LORD_BATTLE_ART_CASTER,      /*   5 Forest Hag */
    LORD_BATTLE_ART_INSECT,      /*   6 Large Mosquito */
    LORD_BATTLE_ART_KNIGHT,      /*   7 Bran The Warrior */
    LORD_BATTLE_ART_HUMAN,       /*   8 Cranky Wanderer */
    LORD_BATTLE_ART_BEAR,        /*   9 Small Bear */
    LORD_BATTLE_ART_TROLL,       /*  10 Small Troll */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_SERPENT, ANSI_BRIGHT_GREEN),
                                    /*  11 Green Python */
    LORD_BATTLE_ART_BARBARIAN,   /*  12 Gath The Barbarian */
    LORD_BATTLE_ART_TREE_SPIRIT, /*  13 Evil Wood Nymph */
    LORD_BATTLE_ART_APE,         /*  14 Fedrick The Limping Baboon */
    LORD_BATTLE_ART_BARBARIAN,   /*  15 Wild Man */
    LORD_BATTLE_ART_BARBARIAN,   /*  16 Brorandia The Viking */
    LORD_BATTLE_ART_CYCLOPS,     /*  17 Towering Traveler */
    LORD_BATTLE_ART_HUMAN,       /*  18 Senile Senior Citizen */
    LORD_BATTLE_ART_SLIME,       /*  19 Membrain Man */
    LORD_BATTLE_ART_TREE_SPIRIT, /*  20 Bent River Dryad */
    LORD_BATTLE_ART_GOLEM,       /*  21 Rock Man */
    LORD_BATTLE_ART_HUMAN,       /*  22 Sleepy Traveler */
    LORD_BATTLE_ART_TWO_HEADED_CANINE, /* 23 Two Headed Rotwieler */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_APE, ANSI_BRIGHT_MAGENTA),
                                    /*  24 Purple Monchichi */
    LORD_BATTLE_ART_SKELETON,    /*  25 Bone */
    LORD_BATTLE_ART_ROGUE,       /*  26 Country Trickster */
    LORD_BATTLE_ART_DEMON,       /*  27 Winged Demon Of Death */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_BIRD, ANSI_DARK_GRAY),
                                    /*  28 Black Owl */
    LORD_BATTLE_ART_BARBARIAN,   /*  29 Muscled Sprinter */
    LORD_BATTLE_ART_HUMAN,       /*  30 Headbanger Of The West */
    LORD_BATTLE_ART_ZOMBIE,      /*  31 Morbid Walker */
    LORD_BATTLE_ART_GNOME,       /*  32 Magical Evil Gnome */
    LORD_BATTLE_ART_CANINE,      /*  33 Death Dog */
    LORD_BATTLE_ART_ORC,         /*  34 Weak Orc */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_ELF_ARCHER, ANSI_DARK_GRAY),
                                    /*  35 Dark Elf */
    LORD_BATTLE_ART_HOBBIT,      /*  36 Evil Hobbit */
    LORD_BATTLE_ART_GOBLIN,      /*  37 Short Goblin */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_BEAR, ANSI_DARK_GRAY),
                                    /*  38 Huge Black Bear */
    LORD_BATTLE_ART_CANINE,      /*  39 Rabid Wolf */
    LORD_BATTLE_ART_CASTER,      /*  40 Young Wizard */
    LORD_BATTLE_ART_GOLEM,       /*  41 Mud Man */
    LORD_BATTLE_ART_JESTER,      /*  42 Death Jester */
    LORD_BATTLE_ART_GOLEM,       /*  43 Rock Man */
    LORD_BATTLE_ART_KNIGHT,      /*  44 Pandion Knight */
    LORD_BATTLE_ART_SLUG,        /*  45 Jabba */
    LORD_BATTLE_ART_APE,         /*  46 Manoken Sloth */
    LORD_BATTLE_ART_KNIGHT,      /*  47 Trojan Warrior */
    LORD_BATTLE_ART_ROGUE,       /*  48 Mischief Maker */
    LORD_BATTLE_ART_BARBARIAN,   /*  49 George Of The Jungle */
    LORD_BATTLE_ART_GHOST,       /*  50 Silent Death */
    LORD_BATTLE_ART_MEDUSA,      /*  51 Stone-Eyed Medusa */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_REPTILE, ANSI_DARK_GRAY),
                                    /*  52 Black Alligator */
    LORD_BATTLE_ART_KNIGHT,      /*  53 Clancy, Son Of Emporor Len */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_CASTER, ANSI_DARK_GRAY),
                                    /*  54 Black Sorcerer */
    LORD_BATTLE_ART_CONSTRUCT,   /*  55 Iron Warrior */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_GHOST, ANSI_DARK_GRAY),
                                    /*  56 Black Soul */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_GOLEM, ANSI_YELLOW),
                                    /*  57 Gold Man */
    LORD_BATTLE_ART_ZOMBIE,      /*  58 Screaming Zombie */
    LORD_BATTLE_ART_DEMON,       /*  59 Satans Helper */
    LORD_BATTLE_ART_HOOFED,      /*  60 Wild Stallion */
    LORD_BATTLE_ART_BRUTE,       /*  61 Belar */
    LORD_BATTLE_ART_EMPTY_ARMOUR, /* 62 Empty Armour */
    LORD_BATTLE_ART_LION,        /*  63 Raging Lion */
    LORD_BATTLE_ART_GOLEM,       /*  64 Huge Stone Warrior */
    LORD_BATTLE_ART_GNOME,       /*  65 Magical Evil Gnome */
    LORD_BATTLE_ART_KNIGHT,      /*  66 Emporer Len */
    LORD_BATTLE_ART_BIRD,        /*  67 Night Hawk */
    LORD_BATTLE_ART_RHINO,       /*  68 Charging Rhinoceros */
    LORD_BATTLE_ART_GOBLIN,      /*  69 Tiny Goblin */
    LORD_BATTLE_ART_GIANT,       /*  70 Goliath */
    LORD_BATTLE_ART_LIONTAUR,    /*  71 Angry Liontaur */
    LORD_BATTLE_ART_WINGED,      /*  72 Fallen Angel */
    LORD_BATTLE_ART_WOMBAT,      /*  73 Wicked Wombat */
    LORD_BATTLE_ART_DINOSAUR,    /*  74 Massive Dinosaur */
    LORD_BATTLE_ART_BUTCHER,     /*  75 Swiss Butcher */
    LORD_BATTLE_ART_GNOME,       /*  76 Death Gnome */
    LORD_BATTLE_ART_CASTER,      /*  77 Screeching Witch */
    LORD_BATTLE_ART_BRUTE,       /*  78 Rundorig */
    LORD_BATTLE_ART_JESTER,      /*  79 Wheeler */
    LORD_BATTLE_ART_KNIGHT,      /*  80 Death Knight */
    LORD_BATTLE_ART_WEREWOLF,    /*  81 Werewolf */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_ORC, ANSI_BRIGHT_RED),
                                    /*  82 Fire Ork */
    LORD_BATTLE_ART_BEAST,       /*  83 Wans Beast */
    LORD_BATTLE_ART_KNIGHT,      /*  84 Lord Mathese */
    LORD_BATTLE_ART_KNIGHT,      /*  85 King Vidion */
    LORD_BATTLE_ART_BABY_DRAGON, /*  86 Baby Dragon */
    LORD_BATTLE_ART_GNOME,       /*  87 Death Gnome */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_ELEPHANT,
                          ANSI_BRIGHT_MAGENTA), /* 88 Pink Elephant */
    LORD_BATTLE_ART_NIGHTMARE,   /*  89 Gwendolens Nightmare */
    LORD_BATTLE_ART_WINGED_SERPENT, /* 90 Flying Cobra */
    LORD_BATTLE_ART_BEAST,       /*  91 Rentakis Pet */
    LORD_BATTLE_ART_HUMAN,       /*  92 Ernest Brown */
    LORD_BATTLE_ART_ROGUE,       /*  93 Scallian Rap */
    LORD_BATTLE_ART_APE,         /*  94 Forest Ape */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_SLIME, ANSI_BRIGHT_RED),
                                    /*  95 Hemo-Glob */
    LORD_BATTLE_ART_FRANKENMOOSE, /* 96 FrankenMoose */
    LORD_BATTLE_ART_GOLEM,       /*  97 Earth Shaker */
    LORD_BATTLE_ART_GOBLIN,      /*  98 Gollums Wrath */
    LORD_BATTLE_ART_KNIGHT,      /*  99 Toraks Son, Korak */
    LORD_BATTLE_ART_HUMAN,       /* 100 Brand The Wanderer */
    LORD_BATTLE_ART_REAPER,      /* 101 The Grimest Reaper */
    LORD_BATTLE_ART_REAPER,      /* 102 Death Dealer */
    LORD_BATTLE_ART_TIGER,       /* 103 Tiger Of The Deep Jungle */
    LORD_BATTLE_ART_ROGUE,       /* 104 Disguised Trickster */
    LORD_BATTLE_ART_EYE,         /* 105 Floating Evil Eye */
    LORD_BATTLE_ART_SLIME,       /* 106 Slock */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_DRAGON, ANSI_YELLOW),
                                    /* 107 Adult Gold Dragon */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_CASTER, ANSI_DARK_GRAY),
                                    /* 108 Black Sorcerer */
    LORD_BATTLE_ART_BRUTE,       /* 109 Kill Joy */
    LORD_BATTLE_ART_HUMAN,       /* 110 Gorma The Wanderer */
    LORD_BATTLE_ART_SHOGUN,      /* 111 Shogun Warrior */
    LORD_BATTLE_ART_CASTER,      /* 112 Hooded Illusionist */
    LORD_BATTLE_ART_BEAR,        /* 113 Ables Creature */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_BEAR, ANSI_WHITE),
                                    /* 114 White Bear Of Lore */
    LORD_BATTLE_ART_MOUNTAIN,    /* 115 Mountain */
    LORD_BATTLE_ART_SHAPECHANGER, /* 116 Sheena The Shapechanger */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_KNIGHT, ANSI_DARK_GRAY),
                                    /* 117 ShadowStormWarrior */
    LORD_BATTLE_ART_CASTER,      /* 118 Madman */
    LORD_BATTLE_ART_PLANT,       /* 119 Vegetable Creature */
    LORD_BATTLE_ART_CYCLOPS,     /* 120 Cyclops Warrior */
    LORD_BATTLE_ART_GIANT,       /* 121 Corinthian Giant */
    LORD_BATTLE_ART_HERALD,      /* 122 The Screaming Herald */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_CASTER, ANSI_DARK_GRAY),
                                    /* 123 Black Warlock */
    LORD_BATTLE_ART_CASTER,      /* 124 Kal Torak */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_GHOST, ANSI_DARK_GRAY),
                                    /* 125 The Mighty Shadow */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_UNICORN, ANSI_DARK_GRAY),
                                    /* 126 Black Unicorn */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_SPIDER, ANSI_DARK_GRAY),
                                    /* 127 Mutated Black Widow */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_DRAGON, ANSI_DARK_GRAY),
                                    /* 128 Humongous Black Wyre */
    LORD_FOREST_ART_COLOR(LORD_BATTLE_ART_CASTER, ANSI_DARK_GRAY),
                                    /* 129 The Wizard Of Darkness */
    LORD_BATTLE_ART_OGRE,        /* 130 Great Ogre Of The North */
};

#undef LORD_FOREST_ART_COLOR

static bool battle_text_has(const char *text, size_t capacity,
                            const char *needle)
{
    const size_t text_size = line_length(text, capacity);
    const size_t needle_size = line_length(needle, 24U);
    if (needle_size == 0U || needle_size > text_size) {
        return false;
    }
    for (size_t start = 0U; start + needle_size <= text_size; ++start) {
        size_t matched = 0U;
        while (matched < needle_size &&
               text[start + matched] == needle[matched]) {
            ++matched;
        }
        if (matched == needle_size) {
            return true;
        }
    }
    return false;
}

static bool battle_name_has(const lord_state_t *state, const char *needle)
{
    return battle_text_has(state->enemy.name, sizeof(state->enemy.name),
                           needle);
}

static bool battle_weapon_has(const lord_state_t *state, const char *needle)
{
    return battle_text_has(state->enemy.weapon, sizeof(state->enemy.weapon),
                           needle);
}

static bool battle_text_equal(const char *first, size_t first_capacity,
                              const char *second, size_t second_capacity)
{
    const size_t first_size = line_length(first, first_capacity);
    const size_t second_size = line_length(second, second_capacity);
    if (first_size != second_size) {
        return false;
    }
    for (size_t index = 0U; index < first_size; ++index) {
        if (first[index] != second[index]) {
            return false;
        }
    }
    return true;
}

static bool forest_battle_art_profile(const lord_state_t *state,
                                      lord_forest_art_profile_t *profile)
{
    if (state->battle_kind != LORD_BATTLE_FOREST || profile == NULL) {
        return false;
    }
    for (size_t index = 0U; index < LORD_MONSTER_COUNT; ++index) {
        if (battle_text_equal(state->enemy.name, sizeof(state->enemy.name),
                              s_monsters[index].name,
                              sizeof(state->enemy.name))) {
            *profile = s_forest_battle_art[index];
            return true;
        }
    }
    return false;
}

static lord_battle_art_t battle_art_kind(const lord_state_t *state,
                                         uint16_t *primary_override)
{
    if (primary_override != NULL) {
        *primary_override = 0U;
    }

    if (state->battle_kind == LORD_BATTLE_DRAGON) {
        return LORD_BATTLE_ART_DRAGON;
    }

    if (state->battle_kind == LORD_BATTLE_PVP ||
        state->battle_kind == LORD_BATTLE_INN) {
        if (state->selected_player < LORD_REALM_PLAYER_COUNT) {
            switch (state->realm[state->selected_player].hero_class) {
            case LORD_CLASS_DEATH_KNIGHT: return LORD_BATTLE_ART_KNIGHT;
            case LORD_CLASS_MYSTICAL: return LORD_BATTLE_ART_CASTER;
            case LORD_CLASS_THIEF: return LORD_BATTLE_ART_ROGUE;
            }
        }
        return LORD_BATTLE_ART_ROGUE;
    }

    if (state->battle_kind == LORD_BATTLE_TRAINER) {
        if (battle_name_has(state, "Gandalf") ||
            battle_name_has(state, "Aladdin")) {
            return LORD_BATTLE_ART_CASTER;
        }
        return LORD_BATTLE_ART_KNIGHT;
    }

    lord_forest_art_profile_t forest_profile = 0U;
    if (forest_battle_art_profile(state, &forest_profile)) {
        if (primary_override != NULL) {
            *primary_override = (uint16_t)(forest_profile >> 8U);
        }
        return (lord_battle_art_t)(forest_profile & UINT32_C(0xff));
    }

    /* Non-table battles retain semantic fallbacks for future trainers and
     * server-authored encounters. Forest rows above never depend on these
     * deliberately broad rules. */
    if (battle_name_has(state, "Baby Dragon")) {
        return LORD_BATTLE_ART_BABY_DRAGON;
    }
    if (battle_name_has(state, "Dragon") || battle_name_has(state, "Wyre")) {
        return LORD_BATTLE_ART_DRAGON;
    }
    if (battle_name_has(state, "Cyclops")) {
        return LORD_BATTLE_ART_CYCLOPS;
    }
    if (battle_name_has(state, "Floating Evil Eye")) {
        return LORD_BATTLE_ART_EYE;
    }
    if (battle_name_has(state, "Widow") || battle_name_has(state, "Spider")) {
        return LORD_BATTLE_ART_SPIDER;
    }
    if (battle_name_has(state, "Mosquito") || battle_name_has(state, "Bug")) {
        return LORD_BATTLE_ART_INSECT;
    }
    if (battle_name_has(state, "Nymph") || battle_name_has(state, "Dryad")) {
        return LORD_BATTLE_ART_TREE_SPIRIT;
    }
    if (battle_name_has(state, "Vegetable")) {
        return LORD_BATTLE_ART_PLANT;
    }
    if (battle_name_has(state, "Jabba")) return LORD_BATTLE_ART_SLUG;
    if (battle_name_has(state, "Membrain") ||
        battle_name_has(state, "Hemo-Glob") ||
        battle_name_has(state, "Slock")) {
        return LORD_BATTLE_ART_SLIME;
    }
    if (battle_name_has(state, "Mountain")) {
        return LORD_BATTLE_ART_MOUNTAIN;
    }
    if (battle_name_has(state, "Rock") || battle_name_has(state, "Mud") ||
        battle_name_has(state, "Stone") || battle_name_has(state, "Gold Man") ||
        battle_name_has(state, "Earth Shaker")) {
        return LORD_BATTLE_ART_GOLEM;
    }
    if (battle_name_has(state, "Armour") ||
        battle_name_has(state, "Iron Warrior") ||
        battle_name_has(state, "Robot")) {
        if (battle_name_has(state, "Empty Armour")) {
            return LORD_BATTLE_ART_EMPTY_ARMOUR;
        }
        return LORD_BATTLE_ART_CONSTRUCT;
    }
    if (battle_name_has(state, "Fallen Angel")) {
        return LORD_BATTLE_ART_WINGED;
    }
    if (battle_name_has(state, "Winged Demon")) {
        return LORD_BATTLE_ART_DEMON;
    }
    if (battle_name_has(state, "Owl") || battle_name_has(state, "Hawk")) {
        return LORD_BATTLE_ART_BIRD;
    }
    if (battle_name_has(state, "Flying Cobra")) {
        return LORD_BATTLE_ART_WINGED_SERPENT;
    }
    if (battle_name_has(state, "Python") || battle_name_has(state, "Cobra")) {
        return LORD_BATTLE_ART_SERPENT;
    }
    if (battle_name_has(state, "Troll")) return LORD_BATTLE_ART_TROLL;
    if (battle_name_has(state, "Goblin")) return LORD_BATTLE_ART_GOBLIN;
    if (battle_name_has(state, "Orc") || battle_name_has(state, "Ork")) {
        return LORD_BATTLE_ART_ORC;
    }
    if (battle_name_has(state, "Ogre")) return LORD_BATTLE_ART_OGRE;
    if (battle_name_has(state, "Giant") || battle_name_has(state, "Goliath")) {
        return LORD_BATTLE_ART_GIANT;
    }
    if (battle_name_has(state, "Rotwieler") &&
        battle_name_has(state, "Two Headed")) {
        return LORD_BATTLE_ART_TWO_HEADED_CANINE;
    }
    if (battle_name_has(state, "Werewolf")) return LORD_BATTLE_ART_WEREWOLF;
    if (battle_name_has(state, "Wolf") || battle_name_has(state, "Dog")) {
        return LORD_BATTLE_ART_CANINE;
    }
    if (battle_name_has(state, "Liontaur")) return LORD_BATTLE_ART_LIONTAUR;
    if (battle_name_has(state, "Tiger")) return LORD_BATTLE_ART_TIGER;
    if (battle_name_has(state, "Lion")) return LORD_BATTLE_ART_LION;
    if (battle_name_has(state, "Nightmare")) return LORD_BATTLE_ART_NIGHTMARE;
    if (battle_name_has(state, "Unicorn")) return LORD_BATTLE_ART_UNICORN;
    if (battle_name_has(state, "FrankenMoose")) {
        return LORD_BATTLE_ART_FRANKENMOOSE;
    }
    if (battle_name_has(state, "Moose")) return LORD_BATTLE_ART_ANTLERED;
    if (battle_name_has(state, "Stallion") || battle_name_has(state, "Horse")) {
        return LORD_BATTLE_ART_HOOFED;
    }
    if (battle_name_has(state, "Elephant")) return LORD_BATTLE_ART_ELEPHANT;
    if (battle_name_has(state, "Rhinoceros")) return LORD_BATTLE_ART_RHINO;
    if (battle_name_has(state, "Bear")) return LORD_BATTLE_ART_BEAR;
    if (battle_name_has(state, "Boar")) return LORD_BATTLE_ART_BOAR;
    if (battle_name_has(state, "Wombat")) return LORD_BATTLE_ART_WOMBAT;
    if (battle_name_has(state, "Rat")) return LORD_BATTLE_ART_RODENT;
    if (battle_name_has(state, "Baboon") || battle_name_has(state, "Ape") ||
        battle_name_has(state, "Monchichi") || battle_name_has(state, "Sloth")) {
        return LORD_BATTLE_ART_APE;
    }
    if (battle_name_has(state, "Dinosaur")) return LORD_BATTLE_ART_DINOSAUR;
    if (battle_name_has(state, "Alligator")) return LORD_BATTLE_ART_REPTILE;
    if (battle_name_has(state, "Medusa")) return LORD_BATTLE_ART_MEDUSA;
    if (battle_name_has(state, "Jester")) return LORD_BATTLE_ART_JESTER;
    if (battle_name_has(state, "Reaper") ||
        battle_name_has(state, "Death Dealer")) {
        return LORD_BATTLE_ART_REAPER;
    }
    if (battle_name_has(state, "Bone") || battle_name_has(state, "Skeleton")) {
        return LORD_BATTLE_ART_SKELETON;
    }
    if (battle_name_has(state, "Zombie") || battle_name_has(state, "Morbid")) {
        return LORD_BATTLE_ART_ZOMBIE;
    }
    if (battle_name_has(state, "ShadowStormWarrior")) {
        return LORD_BATTLE_ART_KNIGHT;
    }
    if (battle_name_has(state, "Soul") || battle_name_has(state, "Shadow") ||
        battle_name_has(state, "Silent Death")) {
        return LORD_BATTLE_ART_GHOST;
    }
    if (battle_name_has(state, "Gnome")) return LORD_BATTLE_ART_GNOME;
    if (battle_name_has(state, "Shapechanger")) {
        return LORD_BATTLE_ART_SHAPECHANGER;
    }
    if (battle_name_has(state, "Dark Elf")) return LORD_BATTLE_ART_ELF_ARCHER;
    if (battle_name_has(state, "Hobbit")) return LORD_BATTLE_ART_HOBBIT;
    if (battle_name_has(state, "Butcher")) return LORD_BATTLE_ART_BUTCHER;
    if (battle_name_has(state, "Herald")) return LORD_BATTLE_ART_HERALD;
    if (battle_name_has(state, "Hag") ||
        battle_name_has(state, "Wizard") || battle_name_has(state, "Sorcerer") ||
        battle_name_has(state, "Witch") ||
        battle_name_has(state, "Illusionist") ||
        battle_name_has(state, "Warlock") || battle_name_has(state, "Madman") ||
        battle_name_has(state, "Kal Torak") || battle_name_has(state, "Satans") ||
        battle_weapon_has(state, "Spell") || battle_weapon_has(state, "Magic") ||
        battle_weapon_has(state, "Chant") || battle_weapon_has(state, "Dream")) {
        return LORD_BATTLE_ART_CASTER;
    }
    if (battle_name_has(state, "Shogun")) return LORD_BATTLE_ART_SHOGUN;
    if (battle_name_has(state, "Knight") || battle_name_has(state, "Warrior") ||
        battle_name_has(state, "Viking") || battle_name_has(state, "Emporer") ||
        battle_name_has(state, "Emperor") || battle_name_has(state, "King ") ||
        battle_name_has(state, "Lord ") || battle_name_has(state, "Clancy") ||
        battle_name_has(state, "Toraks Son") || battle_name_has(state, "Brand")) {
        return LORD_BATTLE_ART_KNIGHT;
    }
    if (battle_name_has(state, "Gorma")) return LORD_BATTLE_ART_HUMAN;
    if (battle_name_has(state, "Wild Man") || battle_name_has(state, "Belar") ||
        battle_name_has(state, "Rundorig") ||
        battle_name_has(state, "Kill Joy") || battle_name_has(state, "Creature")) {
        return LORD_BATTLE_ART_BRUTE;
    }
    if (battle_weapon_has(state, "Fangs") || battle_weapon_has(state, "Teeth") ||
        battle_weapon_has(state, "Claws") || battle_weapon_has(state, "Paws") ||
        battle_weapon_has(state, "Tail") || battle_weapon_has(state, "Horn")) {
        return LORD_BATTLE_ART_BEAST;
    }
    if (battle_weapon_has(state, "Sword") || battle_weapon_has(state, "Mace") ||
        battle_weapon_has(state, "Whip") || battle_weapon_has(state, "Cleaver")) {
        return LORD_BATTLE_ART_KNIGHT;
    }
    return LORD_BATTLE_ART_ROGUE;
}

static uint32_t battle_hash_text(uint32_t hash, const char *text,
                                 size_t capacity)
{
    const size_t length = line_length(text, capacity);
    for (size_t index = 0U; index < length; ++index) {
        hash ^= (uint8_t)text[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static uint32_t battle_art_seed(const lord_state_t *state)
{
    uint32_t hash = battle_hash_text(UINT32_C(2166136261),
                                     state->enemy.name,
                                     sizeof(state->enemy.name));
    hash = battle_hash_text(hash, state->enemy.weapon,
                            sizeof(state->enemy.weapon));
    hash ^= (uint32_t)state->enemy.max_hit_points;
    hash *= UINT32_C(16777619);
    hash ^= (uint32_t)state->enemy.strength;
    hash *= UINT32_C(16777619);
    hash ^= state->enemy.experience;
    hash *= UINT32_C(16777619);
    hash ^= (uint32_t)state->battle_kind;
    return hash;
}

static uint16_t battle_art_primary(lord_battle_art_t kind)
{
    switch (kind) {
    case LORD_BATTLE_ART_ROGUE:
    case LORD_BATTLE_ART_HUMAN:
    case LORD_BATTLE_ART_ELF_ARCHER:
    case LORD_BATTLE_ART_HOBBIT:
    case LORD_BATTLE_ART_BUTCHER:
    case LORD_BATTLE_ART_HERALD: return ANSI_BRIGHT_CYAN;
    case LORD_BATTLE_ART_KNIGHT:
    case LORD_BATTLE_ART_SHOGUN: return ANSI_LIGHT_GRAY;
    case LORD_BATTLE_ART_BARBARIAN: return ANSI_BROWN;
    case LORD_BATTLE_ART_CASTER:
    case LORD_BATTLE_ART_GNOME:
    case LORD_BATTLE_ART_SHAPECHANGER: return ANSI_BRIGHT_MAGENTA;
    case LORD_BATTLE_ART_OGRE:
    case LORD_BATTLE_ART_TROLL:
    case LORD_BATTLE_ART_GOBLIN:
    case LORD_BATTLE_ART_ORC:
    case LORD_BATTLE_ART_BRUTE: return ANSI_BRIGHT_GREEN;
    case LORD_BATTLE_ART_GIANT:
    case LORD_BATTLE_ART_CYCLOPS: return ANSI_BROWN;
    case LORD_BATTLE_ART_RODENT:
    case LORD_BATTLE_ART_WOMBAT:
    case LORD_BATTLE_ART_BOAR:
    case LORD_BATTLE_ART_BEAR:
    case LORD_BATTLE_ART_CANINE:
    case LORD_BATTLE_ART_TWO_HEADED_CANINE:
    case LORD_BATTLE_ART_WEREWOLF:
    case LORD_BATTLE_ART_FELINE:
    case LORD_BATTLE_ART_LION:
    case LORD_BATTLE_ART_TIGER:
    case LORD_BATTLE_ART_LIONTAUR:
    case LORD_BATTLE_ART_HOOFED:
    case LORD_BATTLE_ART_UNICORN:
    case LORD_BATTLE_ART_ANTLERED:
    case LORD_BATTLE_ART_FRANKENMOOSE:
    case LORD_BATTLE_ART_ELEPHANT:
    case LORD_BATTLE_ART_RHINO:
    case LORD_BATTLE_ART_APE:
    case LORD_BATTLE_ART_BEAST: return ANSI_BROWN;
    case LORD_BATTLE_ART_NIGHTMARE: return ANSI_BRIGHT_MAGENTA;
    case LORD_BATTLE_ART_BIRD: return ANSI_BRIGHT_CYAN;
    case LORD_BATTLE_ART_SERPENT:
    case LORD_BATTLE_ART_WINGED_SERPENT:
    case LORD_BATTLE_ART_REPTILE: return ANSI_BRIGHT_GREEN;
    case LORD_BATTLE_ART_DRAGON:
    case LORD_BATTLE_ART_BABY_DRAGON: return ANSI_BRIGHT_RED;
    case LORD_BATTLE_ART_DINOSAUR: return ANSI_BROWN;
    case LORD_BATTLE_ART_INSECT: return ANSI_BRIGHT_MAGENTA;
    case LORD_BATTLE_ART_SPIDER: return ANSI_DARK_GRAY;
    case LORD_BATTLE_ART_SKELETON: return ANSI_WHITE;
    case LORD_BATTLE_ART_ZOMBIE: return ANSI_BRIGHT_GREEN;
    case LORD_BATTLE_ART_GHOST:
    case LORD_BATTLE_ART_REAPER: return ANSI_LIGHT_GRAY;
    case LORD_BATTLE_ART_SLIME:
    case LORD_BATTLE_ART_SLUG: return ANSI_GREEN;
    case LORD_BATTLE_ART_EYE: return ANSI_YELLOW;
    case LORD_BATTLE_ART_CONSTRUCT:
    case LORD_BATTLE_ART_EMPTY_ARMOUR: return ANSI_DARK_GRAY;
    case LORD_BATTLE_ART_GOLEM:
    case LORD_BATTLE_ART_MOUNTAIN: return ANSI_BROWN;
    case LORD_BATTLE_ART_PLANT:
    case LORD_BATTLE_ART_TREE_SPIRIT: return ANSI_GREEN;
    case LORD_BATTLE_ART_WINGED: return ANSI_BRIGHT_BLUE;
    case LORD_BATTLE_ART_DEMON: return ANSI_BRIGHT_RED;
    case LORD_BATTLE_ART_MEDUSA: return ANSI_BRIGHT_GREEN;
    case LORD_BATTLE_ART_JESTER: return ANSI_BRIGHT_MAGENTA;
    case LORD_BATTLE_ART_COUNT: break;
    }
    return ANSI_BRIGHT_CYAN;
}

static uint16_t battle_art_secondary(uint32_t seed)
{
    switch ((seed >> 5U) % 6U) {
    case 0U: return ANSI_BRIGHT_BLUE;
    case 1U: return ANSI_BRIGHT_GREEN;
    case 2U: return ANSI_BRIGHT_CYAN;
    case 3U: return ANSI_BRIGHT_RED;
    case 4U: return ANSI_BRIGHT_MAGENTA;
    default: return ANSI_YELLOW;
    }
}

static uint8_t battle_art_eye(lord_battle_art_t kind, uint32_t seed,
                              uint8_t animation_frame)
{
    if (animation_frame != 0U) {
        return (uint8_t)'-';
    }
    switch (kind) {
    case LORD_BATTLE_ART_CONSTRUCT:
    case LORD_BATTLE_ART_GOLEM:
    case LORD_BATTLE_ART_EYE: return CP437_DIAMOND;
    case LORD_BATTLE_ART_CASTER:
    case LORD_BATTLE_ART_GHOST:
    case LORD_BATTLE_ART_REAPER:
    case LORD_BATTLE_ART_NIGHTMARE:
    case LORD_BATTLE_ART_DEMON:
        return (seed & UINT32_C(1)) != 0U ?
            CP437_DIAMOND : CP437_BULLET;
    default: return CP437_BULLET;
    }
}

static uint8_t battle_art_sigil(uint32_t seed)
{
    switch ((seed >> 17U) & 3U) {
    case 0U: return CP437_HEART;
    case 1U: return CP437_DIAMOND;
    case 2U: return CP437_STAR;
    default: return CP437_ARROW_RIGHT;
    }
}

static lord_creature_motion_t creature_motion_style(lord_battle_art_t kind,
                                                     uint32_t seed)
{
    const uint32_t choice = (seed >> 20U) % UINT32_C(3);
    switch (kind) {
    case LORD_BATTLE_ART_SHAPECHANGER:
        return LORD_CREATURE_MOTION_MORPH;
    case LORD_BATTLE_ART_BIRD:
    case LORD_BATTLE_ART_WINGED_SERPENT:
    case LORD_BATTLE_ART_INSECT:
    case LORD_BATTLE_ART_WINGED:
    case LORD_BATTLE_ART_DEMON:
        return choice == 0U ? LORD_CREATURE_MOTION_FLUTTER :
            (choice == 1U ? LORD_CREATURE_MOTION_BREATHE :
                            LORD_CREATURE_MOTION_ATTACK_JOLT);
    case LORD_BATTLE_ART_CASTER:
    case LORD_BATTLE_ART_GNOME:
    case LORD_BATTLE_ART_NIGHTMARE:
    case LORD_BATTLE_ART_EYE:
    case LORD_BATTLE_ART_MEDUSA:
        return choice == 0U ? LORD_CREATURE_MOTION_SPARK :
            (choice == 1U ? LORD_CREATURE_MOTION_BLINK :
                            LORD_CREATURE_MOTION_BREATHE);
    case LORD_BATTLE_ART_CONSTRUCT:
    case LORD_BATTLE_ART_EMPTY_ARMOUR:
    case LORD_BATTLE_ART_GOLEM:
    case LORD_BATTLE_ART_MOUNTAIN:
        return choice == 0U ? LORD_CREATURE_MOTION_SPARK :
                              LORD_CREATURE_MOTION_ATTACK_JOLT;
    case LORD_BATTLE_ART_SKELETON:
        return choice == 0U ? LORD_CREATURE_MOTION_BLINK :
                              LORD_CREATURE_MOTION_ATTACK_JOLT;
    case LORD_BATTLE_ART_GHOST:
    case LORD_BATTLE_ART_REAPER:
        return choice == 0U ? LORD_CREATURE_MOTION_BLINK :
                              LORD_CREATURE_MOTION_SPARK;
    case LORD_BATTLE_ART_SLIME:
    case LORD_BATTLE_ART_SLUG:
    case LORD_BATTLE_ART_PLANT:
    case LORD_BATTLE_ART_TREE_SPIRIT:
        return choice == 0U ? LORD_CREATURE_MOTION_BLINK :
                              LORD_CREATURE_MOTION_BREATHE;
    case LORD_BATTLE_ART_SERPENT:
    case LORD_BATTLE_ART_DRAGON:
    case LORD_BATTLE_ART_BABY_DRAGON:
    case LORD_BATTLE_ART_REPTILE:
    case LORD_BATTLE_ART_DINOSAUR:
        return choice == 0U ? LORD_CREATURE_MOTION_ATTACK_JOLT :
                              LORD_CREATURE_MOTION_BREATHE;
    default:
        return choice == 0U ? LORD_CREATURE_MOTION_BLINK :
            (choice == 1U ? LORD_CREATURE_MOTION_BREATHE :
                            LORD_CREATURE_MOTION_ATTACK_JOLT);
    }
}

static uint32_t creature_motion_cadence_ms(uint32_t seed)
{
    /* Seven cadences avoid making an entire archetype move in lockstep. */
    return UINT32_C(160) + ((seed >> 25U) % UINT32_C(7)) * UINT32_C(32);
}

static uint8_t creature_animation_frame(uint32_t animation_ms,
                                        uint32_t seed)
{
    return (uint8_t)((animation_ms / creature_motion_cadence_ms(seed)) & 1U);
}

static void advance_creature_animation(lord_state_t *state,
                                       uint32_t elapsed_ms)
{
    /* Reduce both operands before adding so even UINT32_MAX elapsed time
     * cannot overflow. The presentation clock has no gameplay meaning. */
    uint32_t current = state->creature_animation_ms %
        LORD_CREATURE_ANIMATION_WRAP_MS;
    const uint32_t step = elapsed_ms % LORD_CREATURE_ANIMATION_WRAP_MS;
    const uint32_t until_wrap = LORD_CREATURE_ANIMATION_WRAP_MS - current;
    current = step >= until_wrap ? step - until_wrap : current + step;
    state->creature_animation_ms = current;
}

static uint8_t mirrored_art_glyph(uint8_t glyph)
{
    switch (glyph) {
    case (uint8_t)'/': return (uint8_t)'\\';
    case (uint8_t)'\\': return (uint8_t)'/';
    case (uint8_t)'<': return (uint8_t)'>';
    case (uint8_t)'>': return (uint8_t)'<';
    case (uint8_t)'[': return (uint8_t)']';
    case (uint8_t)']': return (uint8_t)'[';
    case (uint8_t)'(': return (uint8_t)')';
    case (uint8_t)')': return (uint8_t)'(';
    case (uint8_t)'{': return (uint8_t)'}';
    case (uint8_t)'}': return (uint8_t)'{';
    default: return glyph;
    }
}

static void draw_ansi_creature_art(p4_game_surface_t *surface,
                                   lord_battle_art_t kind,
                                   uint32_t seed,
                                   lord_enemy_intent_t intent,
                                   uint32_t animation_ms,
                                   uint16_t primary_override,
                                   int x, int y, uint16_t background,
                                   unsigned cell_height)
{
    const uint16_t primary = primary_override != 0U ?
        primary_override : battle_art_primary(kind);
    /* A literal color in the imported name owns the whole silhouette. The
     * seed still varies eyes, texture, motion, mirroring, and the panel sigil,
     * but cannot visually outvote "Pink", "Gold", "Fire", or "Black". */
    const uint16_t secondary = primary_override != 0U ?
        primary_override : battle_art_secondary(seed);
    const bool mirrored = (seed & 1U) != 0U;
    const lord_creature_motion_t motion = creature_motion_style(kind, seed);
    const uint8_t animation_frame_value =
        creature_animation_frame(animation_ms, seed);
    const int attack_shift_x =
        motion == LORD_CREATURE_MOTION_ATTACK_JOLT &&
        animation_frame_value != 0U ? P4_DRAW_CP437_CELL_WIDTH : 0;

    /* Every pattern contains eyes, body cells, and diagonal limbs. That makes
     * blink, breathe, and flutter visibly different in both phases. The two
     * overlay styles move a glyph between separate in-bounds cells. */
    for (size_t row = 0U; row < LORD_BATTLE_ART_ROWS; ++row) {
        const char *const pattern = s_battle_art[kind][row];
        const size_t length = line_length(pattern,
                                          LORD_BATTLE_ART_COLUMNS);
        const int offset = (LORD_BATTLE_ART_COLUMNS - (int)length) / 2;
        for (size_t source = 0U; source < length; ++source) {
            const char marker = pattern[source];
            if (marker == ' ') {
                continue;
            }
            const size_t visible = mirrored ? length - source - 1U : source;
            const int glyph_x = x + attack_shift_x +
                (offset + (int)visible) * P4_DRAW_CP437_CELL_WIDTH;
            const int glyph_y = y + (int)row * (int)cell_height;
            uint8_t glyph = (uint8_t)marker;
            uint16_t color = secondary;
            if (marker == '#') {
                if (motion == LORD_CREATURE_MOTION_BREATHE ||
                    motion == LORD_CREATURE_MOTION_MORPH) {
                    glyph = animation_frame_value == 0U ?
                        CP437_SHADE_MEDIUM : CP437_SHADE_DARK;
                } else {
                    glyph = ((seed + (uint32_t)row +
                              (uint32_t)source) & 3U) == 0U ?
                        CP437_BLOCK : CP437_SHADE_MEDIUM;
                }
                color = primary;
            } else if (marker == 'o') {
                if (motion == LORD_CREATURE_MOTION_MORPH &&
                    animation_frame_value != 0U) {
                    glyph = CP437_DIAMOND;
                    color = ANSI_BRIGHT_CYAN;
                } else {
                    const uint8_t eye_frame =
                        motion == LORD_CREATURE_MOTION_BLINK ?
                            animation_frame_value : 0U;
                    glyph = battle_art_eye(kind, seed + (uint32_t)source,
                                           eye_frame);
                    color = ANSI_YELLOW;
                }
            } else if (marker == '*') {
                glyph = intent_symbol(intent);
                color = intent_accent(intent);
            } else if (marker == '!') {
                glyph = (uint8_t)'^';
                color = ANSI_YELLOW;
            } else if (marker == '~') {
                glyph = CP437_SHADE_LIGHT;
                color = primary;
            } else if (marker == '.') {
                glyph = CP437_BULLET;
                color = primary;
            } else if (marker == '=') {
                glyph = CP437_HORIZONTAL;
                color = primary;
            } else if (motion == LORD_CREATURE_MOTION_MORPH &&
                       animation_frame_value != 0U && marker == '|') {
                glyph = ((row + source) & 1U) == 0U ?
                    (uint8_t)'/' : (uint8_t)'\\';
                color = primary;
            } else if (marker == '{' || marker == '}') {
                if (motion == LORD_CREATURE_MOTION_FLUTTER &&
                    animation_frame_value != 0U) {
                    glyph = marker == '{' ? (uint8_t)'<' : (uint8_t)'>';
                } else {
                    glyph = marker == '{' ? (uint8_t)'\\' : (uint8_t)'/';
                }
                color = primary;
            }
            if (mirrored) {
                glyph = mirrored_art_glyph(glyph);
            }
            p4_draw_cp437_glyph(surface, glyph_x, glyph_y, glyph,
                                color, background, cell_height);
        }
    }

    if (motion == LORD_CREATURE_MOTION_ATTACK_JOLT) {
        const int effect_x = x +
            (animation_frame_value == 0U ? 0 :
             (LORD_BATTLE_ART_COLUMNS - 1) *
                P4_DRAW_CP437_CELL_WIDTH);
        const int effect_y = y + 2 * (int)cell_height;
        p4_draw_cp437_glyph(surface, effect_x, effect_y,
                            animation_frame_value == 0U ?
                                CP437_STAR : intent_symbol(intent),
                            intent_accent(intent), background,
                            P4_DRAW_CP437_COMPACT_HEIGHT);
    } else if (motion == LORD_CREATURE_MOTION_SPARK) {
        const int effect_x = x +
            (animation_frame_value == 0U ? 0 :
             (LORD_BATTLE_ART_COLUMNS - 1) *
                P4_DRAW_CP437_CELL_WIDTH);
        const int effect_y = y +
            (animation_frame_value == 0U ? 0 :
             (LORD_BATTLE_ART_ROWS - 1) * (int)cell_height);
        p4_draw_cp437_glyph(surface, effect_x, effect_y,
                            animation_frame_value == 0U ?
                                CP437_STAR : CP437_DIAMOND,
                            animation_frame_value == 0U ?
                                ANSI_BRIGHT_CYAN : ANSI_BRIGHT_MAGENTA,
                            background, P4_DRAW_CP437_COMPACT_HEIGHT);
    }
}

static bool draw_message_creature_art(p4_game_surface_t *surface,
                                      const lord_state_t *state)
{
    lord_battle_art_t kind = LORD_BATTLE_ART_COUNT;
    const char *const first = state->message_line_1;
    const char *const second = state->message_line_2;
    if (battle_text_has(first, sizeof(state->message_line_1), "troll") ||
        battle_text_has(second, sizeof(state->message_line_2), "troll")) {
        kind = LORD_BATTLE_ART_TROLL;
    } else if (battle_text_has(first, sizeof(state->message_line_1),
                               "grave guardian") ||
               battle_text_has(second, sizeof(state->message_line_2),
                               "grave guardian")) {
        kind = LORD_BATTLE_ART_SKELETON;
    } else if (battle_text_has(first, sizeof(state->message_line_1),
                               "forest fairy") ||
               battle_text_has(second, sizeof(state->message_line_2),
                               "forest fairy")) {
        kind = LORD_BATTLE_ART_WINGED;
    } else if (battle_text_has(first, sizeof(state->message_line_1), "horse") ||
               battle_text_has(second, sizeof(state->message_line_2), "horse")) {
        kind = LORD_BATTLE_ART_HOOFED;
    } else if (battle_text_has(first, sizeof(state->message_line_1), "hag") ||
               battle_text_has(second, sizeof(state->message_line_2), "hag")) {
        kind = LORD_BATTLE_ART_CASTER;
    } else if (battle_text_has(first, sizeof(state->message_line_1), "dragon") ||
               battle_text_has(first, sizeof(state->message_line_1), "Dragon") ||
               battle_text_has(second, sizeof(state->message_line_2), "dragon") ||
               battle_text_has(second, sizeof(state->message_line_2), "Dragon")) {
        kind = LORD_BATTLE_ART_DRAGON;
    } else if (battle_text_has(first, sizeof(state->message_line_1), "creature") ||
               battle_text_has(second, sizeof(state->message_line_2), "creature")) {
        kind = LORD_BATTLE_ART_BRUTE;
    }
    if (kind == LORD_BATTLE_ART_COUNT) {
        return false;
    }

    uint32_t seed = battle_hash_text(UINT32_C(2166136261), first,
                                     sizeof(state->message_line_1));
    seed = battle_hash_text(seed, second, sizeof(state->message_line_2));
    const uint16_t primary = battle_art_primary(kind);
    draw_ansi_box(surface, 108, 24, 104, 96,
                  primary, ANSI_PANEL_ALT, true);
    draw_ansi_creature_art(surface, kind, seed, LORD_ENEMY_INTENT_STRIKE,
                           state->creature_animation_ms,
                           0U,
                           116, 32, ANSI_PANEL_ALT,
                           P4_DRAW_CP437_FULL_HEIGHT);
    draw_cp437(surface, 196, 32, battle_art_sigil(seed),
               battle_art_secondary(seed), ANSI_PANEL_ALT);
    draw_text(surface, 8, 128, first, ANSI_WHITE);
    draw_text(surface, 8, 141, second, ANSI_BRIGHT_CYAN);
    draw_centered_text(surface, 158, "CHOOSE TO CONTINUE",
                       ANSI_YELLOW, 8, 312);
    return true;
}

static void draw_battle_silhouette(p4_game_surface_t *surface,
                                   const lord_state_t *state)
{
    uint16_t primary_override = 0U;
    const lord_battle_art_t kind = battle_art_kind(state, &primary_override);
    const uint32_t seed = battle_art_seed(state);
    const uint16_t primary = primary_override != 0U ?
        primary_override : battle_art_primary(kind);

    draw_ansi_box(surface, 8, 24, 104, 96,
                  primary, ANSI_PANEL_ALT, true);
    draw_ansi_creature_art(surface, kind, seed, state->enemy_intent,
                           state->creature_animation_ms,
                           primary_override,
                           16, 32, ANSI_PANEL_ALT,
                           P4_DRAW_CP437_FULL_HEIGHT);
    draw_cp437(surface, 96, 32, battle_art_sigil(seed),
               battle_art_secondary(seed), ANSI_PANEL_ALT);
    draw_cp437(surface, 96, 96, intent_symbol(state->enemy_intent),
               intent_accent(state->enemy_intent), ANSI_PANEL_ALT);
}

static void draw_battle_meter(p4_game_surface_t *surface,
                              int y, const char *label,
                              int32_t current, int32_t maximum,
                              uint16_t color)
{
    char line[28];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), label);
    line_append(line, sizeof(line), " ");
    line_append_i32(line, sizeof(line), current);
    line_append(line, sizeof(line), "/");
    line_append_i32(line, sizeof(line), maximum);
    draw_text(surface, 120, y, line, color);
    draw_progress_bar(surface, 216, y, 80,
                      positive_hit_points(current),
                      positive_hit_points(maximum), color);
}

static void draw_battle_menu(p4_game_surface_t *surface,
                             const lord_state_t *state,
                             const char *const *labels)
{
    draw_ansi_box(surface, 8, 128, 304, 48,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    for (size_t index = 0U; index < 6U; ++index) {
        const int column = (int)(index % 2U);
        const int row = (int)(index / 2U);
        const int x = 16 + column * 144;
        const int y = 137 + row * 12;
        const bool selected = index == state->selection;
        if (selected) {
            p4_draw_fill_rect(surface, x, y - 2, 136, 11, ANSI_RED);
        }
        draw_ansi_selector(surface, x, y - 2, selected,
                           selected ? ANSI_RED : ANSI_PANEL);
        draw_text(surface, x + 8, y, labels[index],
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
    }
}

static void draw_battle(p4_game_surface_t *surface,
                        const lord_state_t *state)
{
    const char *const menu[] = {
        "Strike",
        "Guard",
        battle_technique_label(state->player.hero_class),
        "Feint",
        "Run",
        "Stats",
    };
    draw_battle_silhouette(surface, state);
    draw_ansi_box(surface, 112, 24, 200, 96,
                  ANSI_BRIGHT_RED, ANSI_PANEL, false);
    draw_text(surface, 120, 29, state->enemy.name, ANSI_BRIGHT_RED);
    draw_text(surface, 120, 41, state->enemy.weapon, ANSI_LIGHT_GRAY);
    draw_battle_meter(surface, 53, "FOE", state->enemy.hit_points,
                      state->enemy.max_hit_points, ANSI_BRIGHT_RED);
    draw_battle_meter(surface, 65, "YOU", state->player.hit_points,
                      state->player.max_hit_points, ANSI_BRIGHT_GREEN);
    draw_cp437(surface, 120, 80, intent_symbol(state->enemy_intent),
               intent_accent(state->enemy_intent), ANSI_PANEL);
    draw_text(surface, 136, 81, enemy_intent_label(state->enemy_intent),
              ANSI_BRIGHT_CYAN);
    p4_draw_fill_rect(surface, 8, 120, 304, 8, ANSI_PANEL_ALT);
    draw_cp437(surface, 8, 120, CP437_ARROW_RIGHT,
               ANSI_YELLOW, ANSI_PANEL_ALT);
    draw_text(surface, 18, 121, state->battle_line, ANSI_YELLOW);
    draw_battle_menu(surface, state, menu);
}

static int positive_distance(int left, int right)
{
    return left < right ? right - left : left - right;
}

static void draw_line(p4_game_surface_t *surface, int x0, int y0,
                      int x1, int y1, uint16_t color)
{
    const int dx = positive_distance(x0, x1);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -positive_distance(y0, y1);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (unsigned step = 0U; step < 640U; ++step) {
        p4_draw_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void draw_rip_town(p4_game_surface_t *surface)
{
    p4_draw_fill_circle(surface, 272, 43, 12, ANSI_YELLOW);
    p4_draw_fill_rect(surface, 0, 111, 320, 25, ANSI_GREEN);
    const int homes[] = {22, 96, 184, 250};
    for (size_t index = 0U; index < sizeof(homes) / sizeof(homes[0]);
         ++index) {
        const int x = homes[index];
        p4_draw_fill_rect(surface, x, 72, 50, 40,
                          index % 2U == 0U ? ANSI_BROWN : ANSI_RED);
        draw_line(surface, x - 4, 72, x + 25, 48, ANSI_BRIGHT_RED);
        draw_line(surface, x + 25, 48, x + 54, 72, ANSI_BRIGHT_RED);
        p4_draw_fill_rect(surface, x + 20, 89, 11, 23, ANSI_BLACK);
        p4_draw_fill_rect(surface, x + 6, 80, 9, 9, ANSI_BRIGHT_CYAN);
    }
    p4_draw_fill_rect(surface, 150, 80, 5, 32, ANSI_DARK_GRAY);
    p4_draw_fill_rect(surface, 136, 78, 34, 4, ANSI_DARK_GRAY);
    for (int x = 0; x < 320; x += P4_DRAW_CP437_CELL_WIDTH) {
        draw_cp437(surface, x, 112,
                   x % 24 == 0 ? CP437_SHADE_DARK : CP437_SHADE_MEDIUM,
                   ANSI_BRIGHT_GREEN, ANSI_GREEN);
    }
}

static void draw_rip_forest(p4_game_surface_t *surface)
{
    p4_draw_fill_circle(surface, 270, 48, 15, ANSI_LIGHT_GRAY);
    for (int x = 15; x < 320; x += 42) {
        p4_draw_fill_rect(surface, x + 15, 79, 9, 45, ANSI_BROWN);
        p4_draw_fill_circle(surface, x + 19, 68, 21, ANSI_GREEN);
        p4_draw_fill_circle(surface, x + 6, 79, 14, ANSI_GREEN);
        p4_draw_fill_circle(surface, x + 32, 79, 14, ANSI_GREEN);
        draw_cp437(surface, x + 15, 58, UINT8_C(0x06),
                   ANSI_BRIGHT_GREEN, ANSI_BLACK);
    }
    p4_draw_fill_rect(surface, 0, 122, 320, 14, ANSI_DARK_GRAY);
    draw_line(surface, 145, 136, 162, 105, ANSI_YELLOW);
    draw_line(surface, 179, 136, 162, 105, ANSI_YELLOW);
}

static void draw_rip_inn(p4_game_surface_t *surface)
{
    p4_draw_fill_rect(surface, 16, 39, 288, 91, ANSI_BROWN);
    p4_draw_fill_rect(surface, 35, 54, 65, 61, ANSI_DARK_GRAY);
    p4_draw_fill_rect(surface, 45, 67, 45, 42, ANSI_RED);
    p4_draw_fill_circle(surface, 67, 93, 19, ANSI_YELLOW);
    p4_draw_fill_rect(surface, 128, 93, 150, 12, ANSI_RED);
    p4_draw_fill_rect(surface, 140, 104, 8, 23, ANSI_RED);
    p4_draw_fill_rect(surface, 258, 104, 8, 23, ANSI_RED);
    for (int x = 151; x <= 247; x += 32) {
        p4_draw_fill_circle(surface, x, 89, 6, ANSI_YELLOW);
    }
    draw_ansi_box(surface, 128, 48, 152, 24,
                  ANSI_YELLOW, ANSI_RED, true);
    draw_text(surface, 142, 57, "TALES  DICE  REST", ANSI_WHITE);
    for (int x = 128; x < 280; x += P4_DRAW_CP437_CELL_WIDTH) {
        draw_cp437(surface, x, 104, CP437_SHADE_LIGHT,
                   ANSI_BRIGHT_RED, ANSI_RED);
    }
}

static void draw_rip_battle(p4_game_surface_t *surface)
{
    p4_draw_fill_circle(surface, 68, 67, 12, ANSI_LIGHT_GRAY);
    p4_draw_fill_rect(surface, 61, 79, 15, 35, ANSI_BRIGHT_BLUE);
    draw_line(surface, 64, 87, 39, 110, ANSI_WHITE);
    draw_line(surface, 74, 88, 104, 61, ANSI_WHITE);
    p4_draw_fill_circle(surface, 247, 67, 13, ANSI_GREEN);
    p4_draw_fill_rect(surface, 238, 79, 18, 36, ANSI_GREEN);
    draw_line(surface, 238, 89, 208, 61, ANSI_YELLOW);
    draw_line(surface, 256, 89, 282, 111, ANSI_YELLOW);
    p4_draw_fill_rect(surface, 0, 119, 320, 17, ANSI_BROWN);
    draw_line(surface, 110, 43, 207, 111, ANSI_BRIGHT_RED);
    draw_text(surface, 126, 55, "CLASH!", ANSI_YELLOW);
    draw_cp437(surface, 108, 39, UINT8_C(0x2f),
               ANSI_WHITE, ANSI_BLACK);
    draw_cp437(surface, 204, 39, UINT8_C(0x5c),
               ANSI_WHITE, ANSI_BLACK);
}

static void draw_rip_dragon(p4_game_surface_t *surface)
{
    p4_draw_sprite_rgb565(surface, 0, 25, s_lord_title_art,
                          LORD_TITLE_ART_WIDTH, LORD_TITLE_ART_HEIGHT,
                          LORD_TITLE_ART_WIDTH, false, 0U);
}

static void draw_rip_building(p4_game_surface_t *surface, uint8_t scene)
{
    const uint16_t walls[] = {
        ANSI_BROWN, ANSI_RED, ANSI_GREEN, ANSI_BRIGHT_BLUE,
        ANSI_YELLOW, ANSI_DARK_GRAY, ANSI_MAGENTA,
    };
    const uint16_t wall = walls[(size_t)(scene - 5U) %
        (sizeof(walls) / sizeof(walls[0]))];
    p4_draw_fill_circle(surface, 270, 44, 12, ANSI_YELLOW);
    p4_draw_fill_rect(surface, 34, 66, 252, 61, wall);
    draw_line(surface, 25, 66, 160, 35, ANSI_BRIGHT_RED);
    draw_line(surface, 160, 35, 295, 66, ANSI_BRIGHT_RED);
    p4_draw_fill_rect(surface, 139, 88, 42, 39, ANSI_BLACK);
    p4_draw_fill_rect(surface, 55, 80, 38, 24, ANSI_BRIGHT_CYAN);
    p4_draw_fill_rect(surface, 227, 80, 38, 24, ANSI_BRIGHT_CYAN);
    draw_cp437(surface, 64, 88, CP437_SHADE_LIGHT,
               ANSI_WHITE, ANSI_BRIGHT_CYAN);
    draw_cp437(surface, 240, 88, CP437_SHADE_LIGHT,
               ANSI_WHITE, ANSI_BRIGHT_CYAN);
    if (scene == 9U) {
        p4_draw_fill_circle(surface, 160, 58, 10, ANSI_YELLOW);
        draw_text(surface, 153, 55, "$", ANSI_BLACK);
    } else if (scene == 10U) {
        for (int x = 53; x < 278; x += 45) {
            p4_draw_fill_rect(surface, x, 111, 22, 7, ANSI_LIGHT_GRAY);
        }
    } else if (scene == 11U) {
        p4_draw_fill_rect(surface, 108, 75, 104, 12, ANSI_RED);
        draw_text(surface, 118, 77, "DARKCLOAK", ANSI_WHITE);
    }
}

static void draw_rip_scene(p4_game_surface_t *surface,
                           const lord_state_t *state)
{
    switch (state->rip_scene) {
    case 0U: draw_rip_town(surface); break;
    case 1U: draw_rip_forest(surface); break;
    case 2U: draw_rip_inn(surface); break;
    case 3U: draw_rip_battle(surface); break;
    case 4U: draw_rip_dragon(surface); break;
    default: draw_rip_building(surface, state->rip_scene); break;
    }
    for (int x = 0; x < 320; x += P4_DRAW_CP437_CELL_WIDTH) {
        draw_cp437(surface, x, 116, CP437_SHADE_DARK,
                   ANSI_RED, ANSI_BLACK);
    }
    p4_draw_fill_rect(surface, 0, 124, 320, 8, ANSI_BLACK);
    draw_text(surface, 8, 121, lord_rip_scene_name(state->rip_scene),
              ANSI_BRIGHT_CYAN);
    draw_text(surface, 214, 121, "A/B: GALLERY", ANSI_YELLOW);
}

static void draw_keyboard(p4_game_surface_t *surface,
                          const lord_state_t *state)
{
    draw_ansi_box(surface, 8, 24, 304, 32,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    draw_text(surface, 16, 36,
              state->editor_text[0] == '\0' ? "_" : state->editor_text,
              ANSI_WHITE);
    for (size_t index = 0U; index < LORD_KEYBOARD_COUNT; ++index) {
        const int column = (int)(index % 6U);
        const int row = (int)(index / 6U);
        const int x = 10 + column * 51;
        const int y = 56 + row * 9;
        const bool selected = index == state->selection;
        if (selected) {
            p4_draw_fill_rect(surface, x, y - 1, 47, 9, ANSI_RED);
        }
        const char *const label = lord_keyboard_label(index);
        draw_text(surface, x + (index >= 42U ? 4 : 19), y, label,
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
    }
}

static void draw_news(p4_game_surface_t *surface, const lord_state_t *state)
{
    draw_ansi_box(surface, 8, 32, 304, 96,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    if (state->log_count == 0U) {
        draw_text(surface, 16, 40, "No news has reached the town crier.",
                  ANSI_LIGHT_GRAY);
    }
    const size_t first = state->log_count > 7U ? state->log_count - 7U : 0U;
    int row = 0;
    for (size_t index = first; index < state->log_count; ++index) {
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), "D");
        line_append_u32(line, sizeof(line), state->log[index].day);
        line_append(line, sizeof(line), " ");
        line_append(line, sizeof(line), state->log[index].text);
        draw_text(surface, 16, 42 + row * 11, line,
                  row % 2 == 0 ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
}

static void draw_rankings(p4_game_surface_t *surface,
                          const lord_state_t *state)
{
    draw_ansi_box(surface, 8, 32, 304, 96,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    p4_draw_fill_rect(surface, 96, 33, 152, 7, ANSI_PANEL);
    draw_text(surface, 99, 33, "DRAGONS  LEVEL  XP  WINS",
              ANSI_BRIGHT_CYAN);
    for (size_t rank = 0U; rank < LORD_RANKING_COUNT; ++rank) {
        const int8_t slot = lord_ranked_slot(state, rank);
        if (slot == -2) {
            break;
        }
        const bool local = slot < 0;
        const char *const name = local ? state->player.name :
            state->realm[(size_t)slot].name;
        const uint8_t level = local ? state->player.level :
            state->realm[(size_t)slot].level;
        const uint32_t experience = local ? state->player.experience :
            state->realm[(size_t)slot].experience;
        const uint16_t wins = local ? state->player.pvp_wins :
            state->realm[(size_t)slot].pvp_wins;
        const uint8_t deeds = local ? state->player.dragon_kills :
            state->realm[(size_t)slot].dragon_kills;
        char line[52];
        line_clear(line, sizeof(line));
        line_append_u32(line, sizeof(line), (uint32_t)rank + 1U);
        line_append(line, sizeof(line), "  ");
        line_append(line, sizeof(line), name);
        line_append(line, sizeof(line), "  D ");
        line_append_u32(line, sizeof(line), deeds);
        line_append(line, sizeof(line), " L ");
        line_append_u32(line, sizeof(line), level);
        line_append(line, sizeof(line), " XP ");
        line_append_compact_u32(line, sizeof(line), experience);
        line_append(line, sizeof(line), " W ");
        line_append_u32(line, sizeof(line), wins);
        const int y = 43 + (int)rank * 9;
        if (local || rank < 3U) {
            p4_draw_fill_rect(surface, 16, y - 1, 288, 8,
                              local ? ANSI_RED : ANSI_PANEL_ALT);
        }
        draw_cp437(surface, 16, y - 1,
                   rank < 3U ? CP437_STAR : CP437_BULLET,
                   local ? ANSI_YELLOW :
                       (rank < 3U ? ANSI_BRIGHT_MAGENTA : ANSI_DARK_GRAY),
                   local ? ANSI_RED :
                       (rank < 3U ? ANSI_PANEL_ALT : ANSI_PANEL));
        draw_text(surface, 28, y, line,
                  local ? ANSI_YELLOW :
                    (rank % 2U == 0U ? ANSI_WHITE : ANSI_LIGHT_GRAY));
    }
}

static void draw_skills(p4_game_surface_t *surface,
                        const lord_state_t *state)
{
    draw_ansi_box(surface, 8, 32, 304, 96,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    const char *const names[LORD_SKILL_COUNT] = {
        "Death Knight", "Mystical", "Thieving",
    };
    for (size_t index = 0U; index < LORD_SKILL_COUNT; ++index) {
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), names[index]);
        line_append(line, sizeof(line), "  mastery ");
        line_append_u32(line, sizeof(line), state->player.skill[index]);
        line_append(line, sizeof(line), "/40  uses ");
        line_append_u32(line, sizeof(line), state->player.skill_uses[index]);
        const int y = 42 + (int)index * 24;
        const bool active = index == (size_t)state->player.hero_class;
        draw_cp437(surface, 17, y - 1,
                   active ? CP437_STAR : CP437_BULLET,
                   active ? ANSI_YELLOW : ANSI_DARK_GRAY, ANSI_PANEL);
        draw_text(surface, 29, y, line,
                  active ? ANSI_BRIGHT_MAGENTA : ANSI_BRIGHT_CYAN);
        draw_progress_bar(surface, 29, y + 10, 274,
                          state->player.skill[index],
                          LORD_SKILL_MASTERY_MAX,
                          active ? ANSI_BRIGHT_MAGENTA : ANSI_BRIGHT_CYAN);
    }
    draw_text(surface, 17, 111,
              "Forest teachers and IGMs increase mastery.",
              ANSI_LIGHT_GRAY);
}

static void draw_dragon_dice(p4_game_surface_t *surface,
                           const lord_state_t *state)
{
    const char *const menu[] = {
        state->minigame_save_barrier ? "Saving wager - please wait" :
          (state->dice_active ? "Roll another die" :
                             "Start round (5 ChompCoin)"),
        state->minigame_save_barrier ? "Round unlocks after save" :
          (state->dice_active ? "Hold and challenge the host" :
                             "Read the rules"),
        state->minigame_save_barrier ? "Wager is being protected" :
                                      "Leave the dice table",
    };
    draw_dice_total(surface, 80, 28, "YOU", state->dice_player,
                    ANSI_BRIGHT_GREEN);
    draw_dice_total(surface, 176, 28, "HOST", state->dice_host,
                    ANSI_BRIGHT_MAGENTA);
    draw_text(surface, 16, 80, "TARGET 18 - OVER 18 BUSTS", ANSI_YELLOW);
    draw_text(surface, 16, 91, state->battle_line, ANSI_BRIGHT_CYAN);
    draw_compact_menu(surface, state, menu, 3U, 104);
}

static void draw_aragorn_quiz(p4_game_surface_t *surface,
                              const lord_state_t *state)
{
    char line[52];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "Wager ");
    line_append_u32(line, sizeof(line), state->quiz_wager);
    line_append(line, sizeof(line), " ChompCoin - correct pays double");
    draw_text(surface, 24, 31, line, ANSI_BRIGHT_CYAN);

    if (state->minigame_save_barrier) {
        draw_ansi_box(surface, 32, 50, 256, 46,
                      ANSI_BRIGHT_MAGENTA, ANSI_PANEL_ALT, true);
        draw_text(surface, 62, 62, "LOCKING WAGER TO SAVED REALM",
                  ANSI_YELLOW);
        draw_text(surface, 72, 77, "PUZZLE OPENS AFTER COMMIT",
                  ANSI_BRIGHT_CYAN);
        return;
    }

    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "SOLVE:  ");
    line_append_u32(line, sizeof(line), state->quiz_left);
    line_append(line, sizeof(line), state->quiz_operator == 0U ? " + " :
              (state->quiz_operator == 1U ? " - " : " x "));
    line_append_u32(line, sizeof(line), state->quiz_right);
    line_append(line, sizeof(line), " = ?");
    draw_ansi_box(surface, 48, 44, 224, 32,
                  ANSI_BRIGHT_MAGENTA, ANSI_PANEL_ALT, true);
    draw_text(surface, 88, 56, line, ANSI_YELLOW);

    char answer_text[4][12];
    const char *answers[4];
    for (size_t index = 0U; index < 4U; ++index) {
        line_clear(answer_text[index], sizeof(answer_text[index]));
        line_append_u32(answer_text[index], sizeof(answer_text[index]),
                        state->quiz_answers[index]);
        answers[index] = answer_text[index];
    }
    draw_compact_menu(surface, state, answers, 4U, 82);
}

static void draw_igm_menu(p4_game_surface_t *surface,
                          const lord_state_t *state)
{
    const size_t count = LORD_IGM_COUNT + 1U;
    const char *labels[LORD_IGM_COUNT + 1U];
    for (size_t index = 0U; index < LORD_IGM_COUNT; ++index) {
        labels[index] = lord_igm_name(index);
    }
    labels[LORD_IGM_COUNT] = "Return to town";
    draw_menu(surface, state, labels, count, 43);
}

static void render_screen(p4_game_surface_t *surface,
                          const lord_state_t *state)
{
    switch (state->screen) {
    case LORD_SCREEN_TITLE:
        draw_header(surface, "LEGEND OF THE RED DRAGON");
        draw_title(surface, state);
        break;
    case LORD_SCREEN_NAME:
        draw_header(surface, "NAME YOUR WARRIOR");
        draw_caption_strip(surface, "CREATE YOUR ANSI HERO",
                           ANSI_BRIGHT_MAGENTA, CP437_SMILE, CP437_STAR);
        draw_text(surface, 48, 55,
                  "Use the built-in ANSI keyboard.", ANSI_BRIGHT_CYAN);
        draw_text(surface, 45, 71,
                  "Names are stored in your save realm.", ANSI_LIGHT_GRAY);
        draw_text(surface, 76, 98,
                  "PRESS A TO ENTER A NAME", ANSI_YELLOW);
        break;
    case LORD_SCREEN_HERO_STYLE:
        draw_header(surface, "CHOOSE YOUR HERO STYLE");
        draw_caption_strip(surface, state->player.name,
                           ANSI_BRIGHT_MAGENTA, CP437_SMILE, CP437_DIAMOND);
        draw_menu(surface, state, s_hero_style_menu, 2U, 58);
        draw_centered_text(surface, 101, "STYLE DOES NOT CHANGE YOUR STATS",
                           ANSI_LIGHT_GRAY, 24, 296);
        break;
    case LORD_SCREEN_CLASS:
        draw_header(surface, "CHOOSE YOUR SKILL");
        draw_caption_strip(surface, "TACTICS SHAPE EVERY BATTLE",
                           ANSI_BRIGHT_CYAN, CP437_STAR, CP437_DIAMOND);
        draw_menu(surface, state, s_class_menu, 3U, 58);
        draw_centered_text(surface, 105,
                           class_preview_label(state->selection),
                           ANSI_YELLOW, 16, 304);
        break;
    case LORD_SCREEN_TOWN:
        draw_header(surface, "THE TOWN SQUARE");
        draw_town(surface, state);
        break;
    case LORD_SCREEN_FOREST: {
        draw_header(surface, "THE DARK FOREST");
        draw_caption_strip(surface, "TWISTED PATHS HIDE OLD DANGERS",
                           ANSI_BRIGHT_GREEN, CP437_SHADE_DARK,
                           CP437_SHADE_DARK);
        draw_menu(surface, state, s_forest_menu,
                  state->player.horse ? 5U : 4U, 54);
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), "Fights remaining: ");
        line_append_u32(line, sizeof(line), state->player.forest_fights);
        draw_text(surface, 10, 121, line, ANSI_BRIGHT_CYAN);
        break;
    }
    case LORD_SCREEN_WEAPON_SHOP:
        draw_header(surface, "WEAPON SHOP");
        draw_shop(surface, state, true);
        break;
    case LORD_SCREEN_ARMOR_SHOP:
        draw_header(surface, "ARMOUR SHOP");
        draw_shop(surface, state, false);
        break;
    case LORD_SCREEN_HEALER:
        draw_header(surface, "VIOLET'S HEALING HUT");
        draw_caption_strip(surface, "RESTORE YOUR STRENGTH",
                           ANSI_BRIGHT_GREEN, CP437_HEART, CP437_HEART);
        draw_player_status(surface, state, 51);
        draw_menu(surface, state, s_healer_menu, 3U, 70);
        break;
    case LORD_SCREEN_TRAINING: {
        draw_header(surface, "TURGON'S TRAINING");
        draw_caption_strip(surface, "TRAIN FOR THE NEXT LEVEL",
                           ANSI_BRIGHT_MAGENTA, CP437_STAR, CP437_STAR);
        const lord_trainer_t *const trainer = lord_current_trainer(state);
        if (trainer == NULL) {
            draw_text(surface, 10, 52, "No master remains above you.",
                      ANSI_YELLOW);
        } else {
            char line[52];
            line_clear(line, sizeof(line));
            line_append(line, sizeof(line), "Master: ");
            line_append(line, sizeof(line), trainer->name);
            line_append(line, sizeof(line), "  XP ");
            line_append_u32(line, sizeof(line), state->player.experience);
            line_append(line, sizeof(line), "/");
            line_append_u32(line, sizeof(line), trainer->experience_needed);
            draw_text(surface, 10, 52, line, ANSI_BRIGHT_MAGENTA);
            draw_progress_bar(surface, 10, 62, 300,
                              state->player.experience,
                              trainer->experience_needed,
                              ANSI_BRIGHT_MAGENTA);
        }
        draw_menu(surface, state, s_training_menu, 2U, 78);
        break;
    }
    case LORD_SCREEN_BANK: {
        draw_header(surface, "THE FIRST BANK OF LORD");
        draw_caption_strip(surface, "THE CHOMPCOIN VAULT",
                           ANSI_YELLOW, CP437_DIAMOND, CP437_DIAMOND);
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), "ChompCoin: ");
        line_append_u32(line, sizeof(line), state->player.gold);
        line_append(line, sizeof(line), "   Vault: ");
        line_append_u32(line, sizeof(line), state->player.bank);
        draw_text(surface, 10, 52, line, ANSI_YELLOW);
        draw_menu(surface, state, s_bank_menu, 4U, 70);
        break;
    }
    case LORD_SCREEN_BANK_TRANSFER:
        draw_header(surface, "TRANSFER 100 CHOMPCOIN");
        draw_realm_menu(surface, state, false, "Return to bank");
        break;
    case LORD_SCREEN_INN:
        draw_header(surface, "THE RED DRAGON INN");
        draw_inn_ansi_strip(surface);
        draw_compact_menu(surface, state, s_inn_menu, 11U, 52);
        break;
    case LORD_SCREEN_BARTENDER:
        draw_header(surface, "TALK WITH THE BARTENDER");
        draw_inn_ansi_strip(surface);
        draw_compact_menu(surface, state, s_bartender_menu, 6U, 58);
        break;
    case LORD_SCREEN_CONVERSE:
        draw_header(surface, "TAVERN CONVERSATION");
        draw_caption_strip(surface, "HEAR THE TAVERN TALES",
                           ANSI_BRIGHT_CYAN, CP437_MUSIC, CP437_SMILE);
        draw_text(surface, 10, 52,
                  state->conversation[0] == '\0' ?
                    "No one has spoken yet." : state->conversation,
                  ANSI_BRIGHT_CYAN);
        draw_menu(surface, state, s_converse_menu, 3U, 70);
        break;
    case LORD_SCREEN_SETH:
        draw_header(surface, "SETH ABLE THE BARD");
        draw_friend_ansi_strip(surface, false);
        draw_compact_menu(surface, state, s_npc_friendship_menu, 6U, 60);
        break;
    case LORD_SCREEN_VIOLET:
        draw_header(surface, "VIOLET OF THE INN");
        draw_friend_ansi_strip(surface, true);
        draw_compact_menu(surface, state, s_npc_friendship_menu, 6U, 60);
        break;
    case LORD_SCREEN_DRAGON_DICE:
        draw_header(surface, "FRIENDLY DRAGON DICE");
        draw_dragon_dice(surface, state);
        break;
    case LORD_SCREEN_PLAYERS: {
        draw_header(surface, realm_context_title(
            state, "LOCAL REALM: WARRIORS", "OFFLINE REALM: WARRIORS",
            "MAC REALM: WARRIORS"));
        draw_realm_menu(surface, state, false, "Return to town");
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), "Player duels remaining: ");
        line_append_u32(line, sizeof(line), state->pvp_fights);
        draw_text(surface, 10, 120, line, ANSI_BRIGHT_CYAN);
        break;
    }
    case LORD_SCREEN_PLAYER_DETAIL:
        draw_header(surface, "PLAYER RECORD");
        draw_player_detail(surface, state);
        break;
    case LORD_SCREEN_MAILBOX: {
        draw_header(surface, realm_context_title(
            state, "LOCAL POST OFFICE", "OFFLINE POST OFFICE",
            "MAC REALM POST OFFICE"));
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), "Unread letters: ");
        line_append_u32(line, sizeof(line),
                        (uint32_t)lord_mail_unread_count(state));
        draw_text(surface, 10, 29, line, ANSI_BRIGHT_CYAN);
        draw_mailbox(surface, state);
        break;
    }
    case LORD_SCREEN_MAIL_VIEW:
        draw_header(surface, "SEALED LETTER");
        draw_mail_view(surface, state);
        break;
    case LORD_SCREEN_MAIL_COMPOSE:
        draw_header(surface, "CHOOSE A RECIPIENT");
        draw_text(surface, 10, 28,
                  "Write with the cartridge's ANSI keyboard.",
                  ANSI_BRIGHT_CYAN);
        draw_realm_menu(surface, state, false, "Return to mailbox");
        break;
    case LORD_SCREEN_FRIENDSHIP: {
        draw_header(surface, realm_context_title(
            state, "LOCAL REALM: FRIENDSHIP",
            "OFFLINE REALM: FRIENDS", "MAC REALM: FRIENDSHIP"));
        draw_realm_menu(surface, state, true, "Return to town");
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), "Friendship actions remaining: ");
        line_append_u32(line, sizeof(line), state->friendship_actions);
        draw_text(surface, 10, 120, line, ANSI_BRIGHT_MAGENTA);
        break;
    }
    case LORD_SCREEN_FRIENDSHIP_ACTION: {
        draw_header(surface, "BUILD FRIENDSHIP");
        const char *name = state->selected_player < LORD_REALM_PLAYER_COUNT ?
            state->realm[state->selected_player].name : "Unknown";
        draw_friendship_ansi_strip(surface, name);
        draw_compact_menu(surface, state, s_friendship_action_menu, 5U, 58);
        break;
    }
    case LORD_SCREEN_IGM: {
        draw_header(surface, "OTHER PLACES: IGMS");
        draw_text(surface, 10, 29,
                  "Seven Synchronet modules, once each per day.",
                  ANSI_BRIGHT_CYAN);
        draw_igm_menu(surface, state);
        break;
    }
    case LORD_SCREEN_IGM_DETAIL:
        draw_header(surface, lord_igm_name(state->selected_igm));
        draw_caption_strip(surface, "ONE CURIOUS VISIT EACH REALM DAY",
                           ANSI_BRIGHT_MAGENTA, CP437_DIAMOND, CP437_STAR);
        draw_text(surface, 10, 52,
                  (state->igm_used_mask &
                   (uint8_t)(1U << state->selected_igm)) != 0U ?
                    "This place is closed to you today." :
                    "Choose one encounter for today's visit.",
                  ANSI_BRIGHT_CYAN);
        draw_menu(surface, state, s_igm_actions[state->selected_igm],
                  4U, 70);
        break;
    case LORD_SCREEN_NEWS:
        draw_header(surface, "THE DAILY NEWS");
        draw_news(surface, state);
        break;
    case LORD_SCREEN_RANKINGS:
        draw_header(surface, "HALL OF RANKINGS");
        draw_rankings(surface, state);
        break;
    case LORD_SCREEN_SKILLS:
        draw_header(surface, "SKILL MASTERY");
        draw_skills(surface, state);
        break;
    case LORD_SCREEN_RIP_GALLERY:
        draw_header(surface, "RIP ART GALLERY");
        draw_text(surface, 10, 29,
                  "ANSI/RIP-style scenes; choose an exhibit.",
                  ANSI_BRIGHT_CYAN);
        draw_menu(surface, state, s_rip_gallery_menu,
                  LORD_RIP_SCENE_COUNT + 1U, 48);
        break;
    case LORD_SCREEN_RIP_SCENE:
        draw_header(surface, "RIP-STYLE SCENE");
        draw_rip_scene(surface, state);
        break;
    case LORD_SCREEN_STATS:
        draw_header(surface, "CHARACTER STATS");
        draw_stats(surface, state);
        break;
    case LORD_SCREEN_BATTLE:
        draw_header(surface, "BATTLE");
        draw_battle(surface, state);
        break;
    case LORD_SCREEN_MESSAGE:
        draw_header(surface, "WORD FROM THE REALM");
        if (!draw_message_creature_art(surface, state)) {
            draw_ansi_box(surface, 8, 40, 304, 56,
                          ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
            draw_text(surface, 18, 52, state->message_line_1, ANSI_WHITE);
            draw_text(surface, 18, 69, state->message_line_2,
                      ANSI_BRIGHT_CYAN);
            draw_text(surface, 90, 111, "CHOOSE TO CONTINUE", ANSI_YELLOW);
        }
        break;
    case LORD_SCREEN_DEAD:
        draw_header(surface, "REST AND RECOVER");
        draw_recovery_ansi(surface);
        draw_text(surface, 80, 108, "YOU WERE KNOCKED OUT", ANSI_BRIGHT_RED);
        draw_text(surface, 61, 120,
                  "PRESS A TO FACE THE MORNING", ANSI_YELLOW);
        break;
    case LORD_SCREEN_DRAGON_VICTORY:
        draw_header(surface, "THE RED DRAGON YIELDS");
        draw_victory_ansi(surface);
        draw_text(surface, 72, 111,
                  "PRESS A TO PLAY AGAIN", ANSI_BRIGHT_CYAN);
        break;
    case LORD_SCREEN_TEXT_EDITOR:
        draw_header(surface, "ANSI TEXT EDITOR");
        draw_keyboard(surface, state);
        break;
    case LORD_SCREEN_ARAGORN_QUIZ:
        draw_header(surface, "ARAGORN'S MATH CHALLENGE");
        draw_aragorn_quiz(surface, state);
        break;
    case LORD_SCREEN_GUILD:
        draw_header(surface, "ADVENTURE CLUB HALL");
        draw_guild_hall(surface, state);
        break;
    case LORD_SCREEN_GUILD_CREATE:
        draw_header(surface, "FOUND AN ADVENTURE CLUB");
        draw_guild_create(surface, state);
        break;
    case LORD_SCREEN_GUILD_TARGET:
        draw_header(surface,
                    state->guild_target == LORD_GUILD_TARGET_JOIN ?
                        "JOIN THROUGH A CLUB MEMBER" :
                    state->guild_target == LORD_GUILD_TARGET_CLASH ?
                        "CHOOSE A FRIENDLY RIVAL" :
                        "CHOOSE A CLUB TO CHEER");
        draw_realm_menu(surface, state, false,
                        "Return to Adventure Club Hall");
        break;
    case LORD_SCREEN_GUILD_STANDINGS:
        draw_header(surface, "ADVENTURE CLUB STANDINGS");
        draw_guild_standings(surface, state);
        break;
    }
}

static void play_event_tone(p4_game_context_t *context,
                            lord_event_t event)
{
    switch (event) {
    case LORD_EVENT_MOVE:
        (void)p4_game_play_tone(context, 330U, 20U, 2U,
                                P4_WAVE_SQUARE);
        break;
    case LORD_EVENT_CONFIRM:
        (void)p4_game_play_tone(context, 523U, 55U, 3U,
                                P4_WAVE_TRIANGLE);
        break;
    case LORD_EVENT_HIT:
        (void)p4_game_play_tone(context, 196U, 70U, 4U,
                                P4_WAVE_SQUARE);
        break;
    case LORD_EVENT_WIN:
        (void)p4_game_play_tone(context, 880U, 150U, 4U,
                                P4_WAVE_TRIANGLE);
        break;
    case LORD_EVENT_LOSE:
        (void)p4_game_play_tone(context, 110U, 250U, 4U,
                                P4_WAVE_SQUARE);
        break;
    case LORD_EVENT_NONE:
        break;
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(lord_state_t)) {
        return false;
    }
    lord_state_t *const state = context->state;
    lord_initialize(state, UINT32_C(0x4c4f5244));
    state->save_available = context->services != NULL &&
        (context->services->available_capabilities & P4_GAME_CAP_SAVE) != 0U;
    if (state->save_available) {
        /* A missing/replaced SD can have no payload while Console OS retains
         * a nonzero anti-rollback sequence floor in NVS. Continue from that
         * host sequence so the first reconstructed save commits at N+1. */
        state->host_save_sequence = context->services->save_sequence;
    }
    if (state->save_available && context->services->save_bytes != 0U) {
        if (context->services->save_schema_version <
                LORD_SAVE_MINIMUM_VERSION ||
            context->services->save_schema_version >
                LORD_SAVE_FORMAT_VERSION ||
            context->services->save_sequence == 0U ||
            !lord_save_decode(state, context->services->save_data,
                              context->services->save_bytes)) {
            lord_initialize(state, UINT32_C(0x4c4f5244));
            state->save_available = true;
            state->save_error = true;
        } else {
            state->host_save_sequence = context->services->save_sequence;
            state->save_available = true;
        }
    }
    lord_realm_net_start(context);
    (void)p4_game_play_tone(context, 392U, 90U, 3U,
                            P4_WAVE_TRIANGLE);
    return true;
}

static void service_save(p4_game_context_t *context, lord_state_t *state)
{
    if (!state->save_available) {
        return;
    }
    if (state->save_ticket != P4_GAME_SAVE_INVALID_TICKET) {
        p4_game_save_status_t status = P4_GAME_SAVE_NONE;
        uint32_t committed_sequence = 0U;
        if (!p4_game_read_save_status(context, state->save_ticket, &status,
                                      &committed_sequence)) {
            /* A valid queued ticket must remain queryable. Treat a broken
             * adapter/ticket as terminal for this launch so a paid barrier can
             * cancel safely instead of trapping input forever. */
            state->save_ticket = P4_GAME_SAVE_INVALID_TICKET;
            state->save_error = true;
            state->save_available = false;
            return;
        }
        if (status == P4_GAME_SAVE_QUEUED ||
            status == P4_GAME_SAVE_READY || status == P4_GAME_SAVE_NONE) {
            return;
        }
        state->save_ticket = P4_GAME_SAVE_INVALID_TICKET;
        if (status == P4_GAME_SAVE_COMMITTED && committed_sequence != 0U) {
            state->host_save_sequence = committed_sequence;
            if (state->save_local_generation ==
                    state->save_queued_generation) {
                state->save_dirty = false;
            }
        } else {
            state->save_error = true;
            /* CONFLICT, UNAVAILABLE, and ERROR are all terminal for this
             * immutable launch snapshot. Relaunching is the recovery path. */
            state->save_available = false;
            return;
        }
    }
    if (!state->save_dirty ||
        state->save_ticket != P4_GAME_SAVE_INVALID_TICKET) {
        return;
    }
    uint8_t payload[LORD_SAVE_MAX_BYTES];
    const size_t payload_bytes = lord_save_encode(
        state, payload, sizeof(payload));
    p4_game_save_ticket_t ticket = P4_GAME_SAVE_INVALID_TICKET;
    if (payload_bytes == 0U) {
        state->save_error = true;
        return;
    }
    if (p4_game_queue_save(context, "AUTO", LORD_SAVE_FORMAT_VERSION,
                           state->host_save_sequence, payload,
                           payload_bytes, &ticket)) {
        state->save_ticket = ticket;
        state->save_queued_generation = state->save_local_generation;
    } else {
        /* A paid minigame must never wait forever or reveal an outcome after
         * its debit failed to enter durable storage. The barrier below will
         * cancel and refund the hidden round. */
        state->save_error = true;
        state->save_available = false;
    }
}

static void cancel_minigame_after_save_failure(
    lord_state_t *state, lord_minigame_pending_t pending)
{
    if (pending == LORD_MINIGAME_PENDING_DICE) {
        state->player.gold = add_u32_saturating(state->player.gold, 5U);
        state->dice_player = 0U;
        state->dice_host = 0U;
        state->dice_rolls = 0U;
        state->dice_active = false;
        set_message(state, LORD_SCREEN_INN,
                    "The dice wager was canceled and refunded.",
                    "Saved storage must be ready before a protected round.");
    } else if (pending == LORD_MINIGAME_PENDING_QUIZ) {
        state->player.gold = add_u32_saturating(
            state->player.gold, state->quiz_wager);
        state->igm_used_mask = (uint8_t)(state->igm_used_mask &
                                         (uint8_t)~UINT8_C(1));
        state->quiz_wager = 0U;
        set_message(state, LORD_SCREEN_IGM,
                    "The math wager was canceled and refunded.",
                    "Saved storage must be ready before a protected puzzle.");
    }
    state->minigame_pending = LORD_MINIGAME_PENDING_NONE;
    state->minigame_save_barrier = false;
    mark_dirty(state);
}

static bool service_minigame_save_barrier(
    p4_game_context_t *context, lord_state_t *state)
{
    if (!state->minigame_save_barrier) {
        return false;
    }
    service_save(context, state);
    if (!state->save_available) {
        cancel_minigame_after_save_failure(state, state->minigame_pending);
        return true;
    }
    if (state->save_available &&
        (state->save_dirty ||
         state->save_ticket != P4_GAME_SAVE_INVALID_TICKET)) {
        return true;
    }

    const lord_minigame_pending_t pending = state->minigame_pending;
    state->minigame_pending = LORD_MINIGAME_PENDING_NONE;
    state->minigame_save_barrier = false;
    if (pending == LORD_MINIGAME_PENDING_DICE) {
        start_dragon_dice_round(state);
    } else if (pending == LORD_MINIGAME_PENDING_QUIZ) {
        generate_aragorn_quiz(state);
    }
    return true;
}

typedef enum {
    LORD_TOUCH_NONE = 0,
    LORD_TOUCH_PREVIOUS,
    LORD_TOUCH_NEXT,
    LORD_TOUCH_CANCEL,
    LORD_TOUCH_ACTIVATE,
    LORD_TOUCH_EXIT,
} lord_touch_action_t;

static lord_touch_action_t touch_action_for_point(
    lord_state_t *state, const p4_game_point_t *point)
{
    const unsigned x = point->x;
    const unsigned y = point->y;
    if (y < 24U) {
        if (x < 52U) {
            return LORD_TOUCH_EXIT;
        }
        if (x >= 268U) {
            return LORD_TOUCH_ACTIVATE;
        }
        return LORD_TOUCH_NONE;
    }

    /* The ANSI keyboard remains the only character-entry path on native
     * cartridges, so every drawn key is directly tappable. */
    if (state->screen == LORD_SCREEN_TEXT_EDITOR &&
        x >= 10U && x < 312U && y >= 55U && y < 128U) {
        const size_t column = (size_t)(x - 10U) / 51U;
        const size_t row = (size_t)(y - 55U) / 9U;
        const unsigned cell_x = 10U + (unsigned)column * 51U;
        const size_t key = row * 6U + column;
        if (column < 6U && x < cell_x + 47U &&
            key < LORD_KEYBOARD_COUNT) {
            state->selection = (uint8_t)key;
            return LORD_TOUCH_ACTIVATE;
        }
    }

    /* The six battle commands are also direct touch targets. */
    if (state->screen == LORD_SCREEN_BATTLE &&
        y >= 134U && y < 170U) {
        size_t column;
        if (x >= 16U && x < 152U) {
            column = 0U;
        } else if (x >= 160U && x < 296U) {
            column = 1U;
        } else {
            return LORD_TOUCH_NONE;
        }
        const size_t row = (size_t)(y - 134U) / 12U;
        const size_t command = row * 2U + column;
        if (row < 3U && command < 6U) {
            state->selection = (uint8_t)command;
            return LORD_TOUCH_ACTIVATE;
        }
    }

    if (y >= 176U) {
        if (x < 80U) {
            return LORD_TOUCH_PREVIOUS;
        }
        if (x < 160U) {
            return LORD_TOUCH_NEXT;
        }
        if (x < 240U) {
            return LORD_TOUCH_CANCEL;
        }
        return LORD_TOUCH_ACTIVATE;
    }
    return LORD_TOUCH_NONE;
}

static p4_game_result_t game_update(p4_game_context_t *context,
                                    const p4_game_input_t *input,
                                    uint32_t elapsed_ms)
{
    lord_state_t *const state = context->state;
    advance_creature_animation(state, elapsed_ms);
    lord_realm_net_poll(context, state, elapsed_ms);
    const bool touch_down = input->touch_valid && input->touch_count > 0U;
    const bool touch_pressed = touch_down && !state->touch_was_down;
    state->touch_was_down = touch_down;
    state->held_buttons = touch_down ? 0U : input->held;
    uint32_t pressed = touch_down ? 0U : input->pressed;
    lord_touch_action_t touch_action = LORD_TOUCH_NONE;
    if (touch_pressed && input->touch_count == 1U) {
        touch_action = touch_action_for_point(state, &input->touches[0]);
        if (touch_action == LORD_TOUCH_EXIT) {
            pressed |= P4_BUTTON_BACK;
        } else if (touch_action == LORD_TOUCH_ACTIVATE) {
            pressed |= P4_BUTTON_A;
        } else if (touch_action == LORD_TOUCH_CANCEL) {
            pressed |= P4_BUTTON_B;
        }
    }
    if (service_minigame_save_barrier(context, state)) {
        return P4_GAME_CONTINUE;
    }
    if ((pressed & P4_BUTTON_BACK) != 0U) {
        service_save(context, state);
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    if (state->save_error || lord_realm_net_blocks_gameplay(state)) {
        service_save(context, state);
        return P4_GAME_CONTINUE;
    }
    if (touch_action == LORD_TOUCH_PREVIOUS ||
        touch_action == LORD_TOUCH_NEXT) {
        lord_move_selection(state,
                            touch_action == LORD_TOUCH_PREVIOUS ? -1 : 1);
        play_event_tone(context, LORD_EVENT_MOVE);
    } else if (state->screen == LORD_SCREEN_TEXT_EDITOR) {
        if ((pressed & P4_BUTTON_LEFT) != 0U) {
            lord_move_selection(state, -1);
            play_event_tone(context, LORD_EVENT_MOVE);
        } else if ((pressed & P4_BUTTON_RIGHT) != 0U) {
            lord_move_selection(state, 1);
            play_event_tone(context, LORD_EVENT_MOVE);
        } else if ((pressed & P4_BUTTON_UP) != 0U) {
            lord_move_selection(state, -6);
            play_event_tone(context, LORD_EVENT_MOVE);
        } else if ((pressed & P4_BUTTON_DOWN) != 0U) {
            lord_move_selection(state, 6);
            play_event_tone(context, LORD_EVENT_MOVE);
        }
    } else if (state->screen == LORD_SCREEN_BATTLE) {
        if ((pressed & (P4_BUTTON_LEFT | P4_BUTTON_RIGHT)) != 0U) {
            state->selection = (uint8_t)(state->selection ^ UINT8_C(1));
            play_event_tone(context, LORD_EVENT_MOVE);
        } else if ((pressed & P4_BUTTON_UP) != 0U) {
            lord_move_selection(state, -2);
            play_event_tone(context, LORD_EVENT_MOVE);
        } else if ((pressed & P4_BUTTON_DOWN) != 0U) {
            lord_move_selection(state, 2);
            play_event_tone(context, LORD_EVENT_MOVE);
        }
    } else if ((pressed & (P4_BUTTON_UP | P4_BUTTON_LEFT)) != 0U) {
        lord_move_selection(state, -1);
        play_event_tone(context, LORD_EVENT_MOVE);
    } else if ((pressed & (P4_BUTTON_DOWN | P4_BUTTON_RIGHT)) != 0U) {
        lord_move_selection(state, 1);
        play_event_tone(context, LORD_EVENT_MOVE);
    }
    lord_event_t event = LORD_EVENT_NONE;
    if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
        const lord_battle_kind_t prior_battle_kind = state->battle_kind;
        lord_realm_pvp_before_t pvp_before;
        p4rm_capture_pvp_before(state, &pvp_before);
        if (!lord_realm_net_activate(context, state, &event)) {
            event = lord_activate(state);
        }
        lord_realm_net_after_activate(
            context, state, prior_battle_kind, event, &pvp_before);
    } else if ((pressed & P4_BUTTON_B) != 0U) {
        event = lord_cancel(state);
    }
    play_event_tone(context, event);
    service_save(context, state);
    return P4_GAME_CONTINUE;
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const lord_state_t *const state = context->state;
    p4_draw_clear(surface, ANSI_BLACK);
    render_screen(surface, state);
    draw_global_status_overlay(surface, state);
    draw_command_footer(surface);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_lord_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(112),
    .id = "org.p4console.lord",
    .title = "LORD",
    .subtitle = "LEGEND OF THE RED DRAGON",
    .accent_rgb565 = ANSI_BRIGHT_RED,
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_SAVE |
        P4_GAME_CAP_MULTIPLAYER_SESSION,
    .state_bytes = sizeof(lord_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
