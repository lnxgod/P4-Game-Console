// SPDX-License-Identifier: LicenseRef-LORD-Permission

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lord_internal.h"
#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"

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

static const char *const s_dragon_dice_menu[] = {
    "Wager 5 ChompCoin",
    "Return to the inn",
};

static const char *const s_battle_menu[] = {
    "Attack",
    "Death Knight attack",
    "Mystical attack",
    "Thieving attack",
    "Run",
    "View stats",
};

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

static void draw_header(p4_game_surface_t *surface, const char *title)
{
    const size_t title_length = line_length(title, 50U);
    int title_x = (320 - (int)title_length * 8) / 2;
    if (title_x < 56) {
        title_x = 56;
    }
    p4_draw_fill_rect(surface, 0, 0, 320, 25, ANSI_PANEL);
    for (int x = 0; x < 320; x += P4_DRAW_CP437_CELL_WIDTH) {
        draw_cp437(surface, x, 17, CP437_DOUBLE_HORIZONTAL,
                   ANSI_BRIGHT_RED, ANSI_PANEL);
    }
    draw_cp437(surface, 48, 1, CP437_DOUBLE_VERTICAL,
               ANSI_RED, ANSI_PANEL);
    draw_cp437(surface, 264, 1, CP437_DOUBLE_VERTICAL,
               ANSI_RED, ANSI_PANEL);
    draw_text(surface, title_x, 8, title, ANSI_YELLOW);
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

static void draw_controls(p4_game_surface_t *surface,
                          const lord_state_t *state)
{
    p4_game_draw_standard_controls(surface, ANSI_CONTROL,
                                   ANSI_BRIGHT_RED,
                                   state->held_buttons);
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
}

static void draw_realm_menu(p4_game_surface_t *surface,
                            const lord_state_t *state,
                            bool show_trust,
                            const char *return_label)
{
    const size_t count = LORD_REALM_PLAYER_COUNT + 1U;
    const size_t scroll = state->menu_scroll;
    const size_t end = scroll + 7U < count ? scroll + 7U : count;
    draw_ansi_box(surface, 8, 32, 304, 96,
                  ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
    int row = 0;
    for (size_t index = scroll; index < end; ++index) {
        const bool selected = index == state->selection;
        char line[52];
        line_clear(line, sizeof(line));
        if (index == LORD_REALM_PLAYER_COUNT) {
            line_append(line, sizeof(line), return_label);
        } else {
            const lord_realm_player_t *const player = &state->realm[index];
            line_append(line, sizeof(line), player->name);
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
    draw_text(surface, 10, 67, player->saying, ANSI_LIGHT_GRAY);
    draw_compact_menu(surface, state, s_player_detail_menu, 5U, 78);
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

    draw_ansi_box(surface, 208, 32, 104, 96,
                  ANSI_BRIGHT_RED, ANSI_PANEL_ALT, true);
    const char *const realm_label = lord_realm_net_label();
    draw_text(surface, 218, 41,
              realm_label != NULL ? realm_label :
                  state->save_available ? "SAVED REALM" : "LOCAL REALM",
              ANSI_YELLOW);
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

static void draw_title(p4_game_surface_t *surface)
{
    p4_draw_sprite_rgb565(surface, 0, 25, s_lord_title_art,
                          LORD_TITLE_ART_WIDTH, LORD_TITLE_ART_HEIGHT,
                          LORD_TITLE_ART_WIDTH, false, 0U);
    draw_ansi_box(surface, 16, 92, 288, 40,
                  ANSI_BRIGHT_RED, ANSI_BLACK, true);
    draw_text(surface, 76, 104, "P4 ANSI DOOR EDITION", ANSI_WHITE);
    draw_text(surface, 28, 118, "A/START ENTER  -  PERSISTENT REALM",
              ANSI_YELLOW);
}

static void draw_shop(p4_game_surface_t *surface,
                      const lord_state_t *state, bool weapon_shop)
{
    const size_t count = lord_menu_count(state);
    const size_t scroll = state->menu_scroll;
    const size_t end = scroll + 8U < count ? scroll + 8U : count;
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
                line_append(line, sizeof(line), "g");
            }
        }
        if (selected) {
            p4_draw_fill_rect(surface, 8, 29 + row * 12 - 2,
                              304, 11, ANSI_BLUE);
        }
        draw_ansi_selector(surface, 8, 27 + row * 12, selected,
                           selected ? ANSI_BLUE : ANSI_BLACK);
        draw_text(surface, 24, 29 + row * 12, line,
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
    char funds[52];
    line_clear(funds, sizeof(funds));
    line_append(funds, sizeof(funds), "Carried ChompCoin: ");
    line_append_u32(funds, sizeof(funds), state->player.gold);
    draw_text(surface, 8, 122, funds, ANSI_BRIGHT_CYAN);
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
    draw_text(surface, 12, 109, line, ANSI_BRIGHT_CYAN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), state->player.horse ? "HORSE " : "");
    line_append(line, sizeof(line), state->player.fairy ? "FAIRY " : "");
    line_append(line, sizeof(line), state->player.amulet ? "AMULET " : "");
    line_append(line, sizeof(line), " PVP ");
    line_append_u32(line, sizeof(line), state->player.pvp_wins);
    line_append(line, sizeof(line), "-");
    line_append_u32(line, sizeof(line), state->player.pvp_losses);
    draw_text(surface, 12, 122, line, ANSI_BRIGHT_GREEN);
}

static void draw_battle(p4_game_surface_t *surface,
                        const lord_state_t *state)
{
    char line[52];
    p4_draw_fill_rect(surface, 8, 27, 304, 103, ANSI_PANEL);
    p4_draw_rect(surface, 8, 27, 304, 103, ANSI_RED);
    draw_text(surface, 10, 29, state->enemy.name, ANSI_BRIGHT_RED);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "Enemy HP ");
    line_append_i32(line, sizeof(line), state->enemy.hit_points);
    line_append(line, sizeof(line), "/");
    line_append_i32(line, sizeof(line), state->enemy.max_hit_points);
    draw_text(surface, 174, 29, line, ANSI_RED);
    draw_player_status(surface, state, 42);
    draw_text(surface, 10, 56, state->battle_line, ANSI_YELLOW);
    draw_compact_menu(surface, state, s_battle_menu, 6U, 66);
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
    char line[52];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "1  ");
    line_append(line, sizeof(line), state->player.name);
    line_append(line, sizeof(line), "  LV ");
    line_append_u32(line, sizeof(line), state->player.level);
    line_append(line, sizeof(line), "  DR ");
    line_append_u32(line, sizeof(line), state->player.dragon_kills);
    draw_text(surface, 16, 42, line, ANSI_YELLOW);
    for (size_t index = 0U; index < 7U; ++index) {
        line_clear(line, sizeof(line));
        line_append_u32(line, sizeof(line), (uint32_t)index + 2U);
        line_append(line, sizeof(line), "  ");
        line_append(line, sizeof(line), state->realm[index].name);
        line_append(line, sizeof(line), "  LV ");
        line_append_u32(line, sizeof(line), state->realm[index].level);
        line_append(line, sizeof(line), "  PVP ");
        line_append_u32(line, sizeof(line), state->realm[index].pvp_wins);
        draw_text(surface, 16, 53 + (int)index * 10, line,
                  index % 2U == 0U ? ANSI_WHITE : ANSI_LIGHT_GRAY);
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
        draw_text(surface, 17, 42 + (int)index * 24, line,
                  index == (size_t)state->player.hero_class ?
                    ANSI_BRIGHT_MAGENTA : ANSI_BRIGHT_CYAN);
    }
    draw_text(surface, 17, 111,
              "Forest teachers and IGMs increase mastery.",
              ANSI_LIGHT_GRAY);
}

static void draw_dragon_dice(p4_game_surface_t *surface,
                           const lord_state_t *state)
{
    draw_dice_total(surface, 80, 28, "YOU", state->dice_player,
                    ANSI_BRIGHT_GREEN);
    draw_dice_total(surface, 176, 28, "HOST", state->dice_host,
                    ANSI_BRIGHT_MAGENTA);
    draw_text(surface, 24, 80, state->battle_line, ANSI_BRIGHT_CYAN);
    draw_menu(surface, state, s_dragon_dice_menu, 2U, 98);
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
        draw_title(surface);
        break;
    case LORD_SCREEN_NAME:
        draw_header(surface, "NAME YOUR WARRIOR");
        draw_text(surface, 32, 38,
                  "Use the built-in ANSI keyboard.", ANSI_BRIGHT_CYAN);
        draw_text(surface, 33, 57,
                  "Names are stored in your save realm.", ANSI_LIGHT_GRAY);
        draw_text(surface, 76, 88,
                  "PRESS A TO ENTER A NAME", ANSI_YELLOW);
        break;
    case LORD_SCREEN_HERO_STYLE:
        draw_header(surface, "CHOOSE YOUR HERO STYLE");
        draw_text(surface, 10, 29, state->player.name,
                  ANSI_BRIGHT_MAGENTA);
        draw_menu(surface, state, s_hero_style_menu, 2U, 50);
        break;
    case LORD_SCREEN_CLASS:
        draw_header(surface, "CHOOSE YOUR SKILL");
        draw_text(surface, 10, 29, "Your path shapes your battle skill.",
                  ANSI_BRIGHT_CYAN);
        draw_menu(surface, state, s_class_menu, 3U, 50);
        break;
    case LORD_SCREEN_TOWN:
        draw_header(surface, "THE TOWN SQUARE");
        draw_town(surface, state);
        break;
    case LORD_SCREEN_FOREST: {
        draw_header(surface, "THE DARK FOREST");
        draw_text(surface, 10, 29, "Twisted branches hide old dangers.",
                  ANSI_GREEN);
        draw_menu(surface, state, s_forest_menu,
                  state->player.horse ? 5U : 4U, 47);
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), "Fights remaining: ");
        line_append_u32(line, sizeof(line), state->player.forest_fights);
        draw_text(surface, 10, 116, line, ANSI_BRIGHT_CYAN);
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
        draw_player_status(surface, state, 29);
        draw_menu(surface, state, s_healer_menu, 3U, 52);
        break;
    case LORD_SCREEN_TRAINING: {
        draw_header(surface, "TURGON'S TRAINING");
        const lord_trainer_t *const trainer = lord_current_trainer(state);
        if (trainer == NULL) {
            draw_text(surface, 10, 29, "No master remains above you.",
                      ANSI_YELLOW);
        } else {
            char line[52];
            line_clear(line, sizeof(line));
            line_append(line, sizeof(line), "Master: ");
            line_append(line, sizeof(line), trainer->name);
            line_append(line, sizeof(line), "  Need XP: ");
            line_append_u32(line, sizeof(line), trainer->experience_needed);
            draw_text(surface, 10, 29, line, ANSI_BRIGHT_MAGENTA);
        }
        draw_menu(surface, state, s_training_menu, 2U, 52);
        break;
    }
    case LORD_SCREEN_BANK: {
        draw_header(surface, "THE FIRST BANK OF LORD");
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), "ChompCoin: ");
        line_append_u32(line, sizeof(line), state->player.gold);
        line_append(line, sizeof(line), "   Vault: ");
        line_append_u32(line, sizeof(line), state->player.bank);
        draw_text(surface, 10, 29, line, ANSI_YELLOW);
        draw_menu(surface, state, s_bank_menu, 4U, 52);
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
        draw_text(surface, 10, 29,
                  state->conversation[0] == '\0' ?
                    "No one has spoken yet." : state->conversation,
                  ANSI_BRIGHT_CYAN);
        draw_menu(surface, state, s_converse_menu, 3U, 54);
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
        draw_header(surface, "LOCAL REALM: WARRIORS");
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
        draw_header(surface, "LOCAL POST OFFICE");
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
        draw_header(surface, "LOCAL REALM: FRIENDSHIP");
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
        draw_text(surface, 10, 29,
                  (state->igm_used_mask &
                   (uint8_t)(1U << state->selected_igm)) != 0U ?
                    "This place is closed to you today." :
                    "Choose one encounter for today's visit.",
                  ANSI_BRIGHT_CYAN);
        draw_menu(surface, state, s_igm_actions[state->selected_igm],
                  4U, 50);
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
        draw_ansi_box(surface, 8, 40, 304, 56,
                      ANSI_BRIGHT_BLUE, ANSI_PANEL, false);
        draw_text(surface, 18, 52, state->message_line_1, ANSI_WHITE);
        draw_text(surface, 18, 69, state->message_line_2,
                  ANSI_BRIGHT_CYAN);
        draw_text(surface, 90, 111, "PRESS A TO CONTINUE", ANSI_YELLOW);
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
    if (state->save_available && context->services->save_bytes != 0U) {
        if ((context->services->save_schema_version !=
                 LORD_SAVE_MINIMUM_VERSION &&
             context->services->save_schema_version !=
                 LORD_SAVE_FORMAT_VERSION) ||
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
            return;
        }
        if (status == P4_GAME_SAVE_QUEUED ||
            status == P4_GAME_SAVE_READY || status == P4_GAME_SAVE_NONE) {
            return;
        }
        state->save_ticket = P4_GAME_SAVE_INVALID_TICKET;
        if (status == P4_GAME_SAVE_COMMITTED && committed_sequence != 0U) {
            state->host_save_sequence = committed_sequence;
            if (state->save_sequence == state->save_queued_generation) {
                state->save_dirty = false;
            }
        } else {
            state->save_error = true;
            if (status == P4_GAME_SAVE_UNAVAILABLE ||
                status == P4_GAME_SAVE_CONFLICT) {
                state->save_available = false;
            }
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
        state->save_queued_generation = state->save_sequence;
    }
}

static p4_game_result_t game_update(p4_game_context_t *context,
                                    const p4_game_input_t *input,
                                    uint32_t elapsed_ms)
{
    lord_state_t *const state = context->state;
    lord_realm_net_poll(context, state, elapsed_ms);
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        service_save(context, state);
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    if (lord_realm_net_blocks_gameplay()) {
        return P4_GAME_CONTINUE;
    }
    if (state->screen == LORD_SCREEN_TEXT_EDITOR) {
        if ((input->pressed & P4_BUTTON_LEFT) != 0U) {
            lord_move_selection(state, -1);
            play_event_tone(context, LORD_EVENT_MOVE);
        } else if ((input->pressed & P4_BUTTON_RIGHT) != 0U) {
            lord_move_selection(state, 1);
            play_event_tone(context, LORD_EVENT_MOVE);
        } else if ((input->pressed & P4_BUTTON_UP) != 0U) {
            lord_move_selection(state, -6);
            play_event_tone(context, LORD_EVENT_MOVE);
        } else if ((input->pressed & P4_BUTTON_DOWN) != 0U) {
            lord_move_selection(state, 6);
            play_event_tone(context, LORD_EVENT_MOVE);
        }
    } else if ((input->pressed & (P4_BUTTON_UP | P4_BUTTON_LEFT)) != 0U) {
        lord_move_selection(state, -1);
        play_event_tone(context, LORD_EVENT_MOVE);
    } else if ((input->pressed & (P4_BUTTON_DOWN | P4_BUTTON_RIGHT)) != 0U) {
        lord_move_selection(state, 1);
        play_event_tone(context, LORD_EVENT_MOVE);
    }
    lord_event_t event = LORD_EVENT_NONE;
    if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
        const lord_battle_kind_t prior_battle_kind = state->battle_kind;
        if (!lord_realm_net_activate(context, state, &event)) {
            event = lord_activate(state);
        }
        lord_realm_net_after_activate(
            context, state, prior_battle_kind, event);
    } else if ((input->pressed & P4_BUTTON_B) != 0U) {
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
    draw_controls(surface, state);
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
