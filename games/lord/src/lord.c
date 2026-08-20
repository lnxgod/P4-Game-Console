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
};

static const char *const s_class_menu[] = {
    "Death Knight Skills",
    "Mystical Skills",
    "Thieving Skills",
};

static const char *const s_sex_menu[] = {
    "Male warrior",
    "Female warrior",
};

static const char *const s_town_menu[] = {
    "The Dark Forest",
    "King Arthur's Weapons",
    "Abdul's Armour",
    "The Healer's Hut",
    "Turgon's Warrior Training",
    "The First Bank",
    "The Red Dragon Inn",
    "Slaughter Other Warriors",
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
    "Heal one hit point (5 gold)",
    "Heal all wounds",
    "Return to town",
};

static const char *const s_training_menu[] = {
    "Challenge your master",
    "Return to town",
};

static const char *const s_bank_menu[] = {
    "Deposit all carried gold",
    "Withdraw entire account",
    "Transfer 100 gold",
    "Return to town",
};

static const char *const s_inn_menu[] = {
    "Sleep until a new day",
    "Talk with the bartender",
    "Join the conversation",
    "Spend time with Seth Able",
    "Spend time with Violet",
    "Listen to the bard's song",
    "Play blackjack",
    "Attack a sleeping warrior",
    "Make an announcement",
    "Check your room",
    "Return to town",
};

static const char *const s_bartender_menu[] = {
    "Buy dark ale (10 gold)",
    "Ask about Violet",
    "Ask about Seth Able",
    "Ask about the Red Dragon",
    "Drinking contest",
    "Return to the inn",
};

static const char *const s_converse_menu[] = {
    "Read the latest conversation",
    "Add to the conversation",
    "Return to the inn",
};

static const char *const s_npc_romance_menu[] = {
    "Talk",
    "Flirt",
    "Kiss",
    "Give a gem",
    "Propose or divorce",
    "Return to the inn",
};

static const char *const s_blackjack_menu[] = {
    "Deal a hand (100 gold)",
    "Hit",
    "Stand",
    "Leave the table",
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
    "Court this warrior",
    "Edit local saying",
    "Return to player list",
};

static const char *const s_romance_action_menu[] = {
    "Offer a compliment",
    "Give a gift (100 gold)",
    "Propose marriage / divorce",
    "Try to have a child",
    "Return to player record",
};

static const char *const s_igm_actions[LORD_IGM_COUNT][4] = {
    {"Wager 100 gold", "Wager 1000 gold", "Read the rules", "Return"},
    {"Raid Barak's house", "Ask Barak for sugar", "Skill lesson", "Return"},
    {"Answer trivia", "Sleep at a cabin", "Accept invitation", "Return"},
    {"Rob a grave", "Read an epitaph", "Pay your respects", "Return"},
    {"Adopt an orphan", "Catch a runaway", "Trade children for horse", "Return"},
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

static void draw_header(p4_game_surface_t *surface, const char *title)
{
    const size_t title_length = line_length(title, 50U);
    int title_x = (320 - (int)title_length * 8) / 2;
    if (title_x < 56) {
        title_x = 56;
    }
    p4_draw_fill_rect(surface, 0, 0, 320, 25, ANSI_PANEL);
    p4_draw_fill_rect(surface, 0, 23, 320, 2, ANSI_BRIGHT_RED);
    p4_draw_fill_rect(surface, 55, 5, 3, 14, ANSI_RED);
    p4_draw_fill_rect(surface, 262, 5, 3, 14, ANSI_RED);
    draw_text(surface, title_x, 8, title, ANSI_YELLOW);
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
    p4_draw_fill_rect(surface, 8, first_y - 6, 304,
                      visible * 12 + 8, ANSI_PANEL);
    p4_draw_rect(surface, 8, first_y - 6, 304,
                 visible * 12 + 8, ANSI_BLUE);
    int row = 0;
    for (size_t index = scroll; index < end; ++index) {
        const bool selected = index == state->selection;
        if (selected) {
            p4_draw_fill_rect(surface, 10, first_y + row * 12 - 2,
                              300, 11, ANSI_RED);
        }
        draw_text(surface, 12, first_y + row * 12,
                  selected ? ">" : " ", ANSI_YELLOW);
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
    p4_draw_fill_rect(surface, 8, first_y - 4, 304,
                      visible * 10 + 6, ANSI_PANEL);
    p4_draw_rect(surface, 8, first_y - 4, 304,
                 visible * 10 + 6, ANSI_BLUE);
    int row = 0;
    for (size_t index = scroll; index < end; ++index) {
        const bool selected = index == state->selection;
        if (selected) {
            p4_draw_fill_rect(surface, 10, first_y + row * 10 - 1,
                              300, 9, ANSI_RED);
        }
        draw_text(surface, 12, first_y + row * 10,
                  selected ? ">" : " ", ANSI_YELLOW);
        draw_text(surface, 24, first_y + row * 10, labels[index],
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
}

static void draw_realm_menu(p4_game_surface_t *surface,
                            const lord_state_t *state,
                            bool show_affection,
                            const char *return_label)
{
    const size_t count = LORD_REALM_PLAYER_COUNT + 1U;
    const size_t scroll = state->menu_scroll;
    const size_t end = scroll + 7U < count ? scroll + 7U : count;
    p4_draw_fill_rect(surface, 8, 33, 304, 96, ANSI_PANEL);
    p4_draw_rect(surface, 8, 33, 304, 96, ANSI_BLUE);
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
            if (show_affection) {
                line_append(line, sizeof(line), "  HEART ");
                line_append_u32(line, sizeof(line), player->affection);
            } else {
                line_append(line, sizeof(line),
                            player->alive ? "  READY" : "  DEFEATED");
            }
        }
        const int y = 39 + row * 11;
        if (selected) {
            p4_draw_fill_rect(surface, 10, y - 2, 300, 12, ANSI_RED);
        }
        draw_text(surface, 12, y, selected ? ">" : " ", ANSI_YELLOW);
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
    line_append(line, sizeof(line), "Affection ");
    line_append_u32(line, sizeof(line), player->affection);
    line_append(line, sizeof(line), player->married ? "  MARRIED" : "");
    draw_text(surface, 10, 55, line, ANSI_YELLOW);
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
    p4_draw_fill_rect(surface, 8, 39, 304, panel_height, ANSI_PANEL);
    p4_draw_rect(surface, 8, 39, 304, panel_height, ANSI_BLUE);
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
            p4_draw_fill_rect(surface, 10, y - 2, 300, 12, ANSI_RED);
        }
        draw_text(surface, 12, y, selected ? ">" : " ", ANSI_YELLOW);
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
    p4_draw_fill_rect(surface, 8, 60, 304, 52, ANSI_PANEL);
    p4_draw_rect(surface, 8, 60, 304, 52, ANSI_BLUE);
    draw_text(surface, 16, 72, lord_mail_body_1(state, index), ANSI_WHITE);
    draw_text(surface, 16, 88, lord_mail_body_2(state, index),
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
    line_append(line, sizeof(line), "  GOLD ");
    line_append_u32(line, sizeof(line), state->player.gold);
    draw_text(surface, 8, y, line, ANSI_BRIGHT_CYAN);
}

static void draw_town(p4_game_surface_t *surface,
                      const lord_state_t *state)
{
    p4_draw_fill_rect(surface, 6, 29, 198, 101, ANSI_PANEL);
    p4_draw_rect(surface, 6, 29, 198, 101, ANSI_BLUE);
    const size_t scroll = state->menu_scroll;
    const size_t count = sizeof(s_town_menu) / sizeof(s_town_menu[0]);
    const size_t end = scroll + 7U < count ? scroll + 7U : count;
    int row = 0;
    for (size_t index = scroll; index < end; ++index) {
        const bool selected = index == state->selection;
        const int y = 35 + row * 12;
        if (selected) {
            p4_draw_fill_rect(surface, 9, y - 2, 192, 11, ANSI_RED);
        }
        draw_text(surface, 13, y, selected ? ">" : " ", ANSI_YELLOW);
        draw_text(surface, 25, y, s_town_menu[index],
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }

    p4_draw_fill_rect(surface, 210, 29, 104, 101, ANSI_PANEL_ALT);
    p4_draw_rect(surface, 210, 29, 104, 101, ANSI_RED);
    draw_text(surface, 218, 35,
              state->save_available ? "SAVED REALM" : "LOCAL REALM",
              ANSI_YELLOW);
    p4_draw_fill_rect(surface, 216, 46, 92, 1, ANSI_RED);
    char line[24];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "DAY ");
    line_append_u32(line, sizeof(line), state->player.day);
    draw_text(surface, 218, 52, line, ANSI_LIGHT_GRAY);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "LEVEL ");
    line_append_u32(line, sizeof(line), state->player.level);
    draw_text(surface, 218, 64, line, ANSI_BRIGHT_CYAN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "HP ");
    line_append_i32(line, sizeof(line), state->player.hit_points);
    line_append(line, sizeof(line), "/");
    line_append_i32(line, sizeof(line), state->player.max_hit_points);
    draw_text(surface, 218, 76, line, ANSI_BRIGHT_GREEN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "GOLD ");
    line_append_u32(line, sizeof(line), state->player.gold);
    draw_text(surface, 218, 88, line, ANSI_YELLOW);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "MAIL ");
    line_append_u32(line, sizeof(line),
                    (uint32_t)lord_mail_unread_count(state));
    line_append(line, sizeof(line), " NEW");
    draw_text(surface, 218, 100, line, ANSI_BRIGHT_MAGENTA);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "PVP LEFT ");
    line_append_u32(line, sizeof(line), state->pvp_fights);
    draw_text(surface, 218, 112, line, ANSI_BRIGHT_RED);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "MENU ");
    line_append_u32(line, sizeof(line), (uint32_t)state->selection + 1U);
    line_append(line, sizeof(line), "/15");
    draw_text(surface, 218, 120, line, ANSI_DARK_GRAY);
}

static void draw_title(p4_game_surface_t *surface)
{
    p4_draw_sprite_rgb565(surface, 0, 25, s_lord_title_art,
                          LORD_TITLE_ART_WIDTH, LORD_TITLE_ART_HEIGHT,
                          LORD_TITLE_ART_WIDTH, false, 0U);
    p4_draw_fill_rect(surface, 18, 98, 284, 31, ANSI_BLACK);
    p4_draw_rect(surface, 18, 98, 284, 31, ANSI_BRIGHT_RED);
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
        draw_text(surface, 12, 29 + row * 12,
                  selected ? ">" : " ", ANSI_YELLOW);
        draw_text(surface, 24, 29 + row * 12, line,
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
    char funds[52];
    line_clear(funds, sizeof(funds));
    line_append(funds, sizeof(funds), "Carried gold: ");
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
    line_append(line, sizeof(line), lord_sex_name(state->player.sex));
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
    line_append(line, sizeof(line), "GOLD ");
    line_append_u32(line, sizeof(line), state->player.gold);
    line_append(line, sizeof(line), "  BANK ");
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
    line_append(line, sizeof(line), "  KIDS ");
    line_append_u32(line, sizeof(line), state->player.children);
    line_append(line, sizeof(line), "  LAID ");
    line_append_u32(line, sizeof(line), state->player.laid);
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
}

static void draw_rip_forest(p4_game_surface_t *surface)
{
    p4_draw_fill_circle(surface, 270, 48, 15, ANSI_LIGHT_GRAY);
    for (int x = 15; x < 320; x += 42) {
        p4_draw_fill_rect(surface, x + 15, 79, 9, 45, ANSI_BROWN);
        p4_draw_fill_circle(surface, x + 19, 68, 21, ANSI_GREEN);
        p4_draw_fill_circle(surface, x + 6, 79, 14, ANSI_GREEN);
        p4_draw_fill_circle(surface, x + 32, 79, 14, ANSI_GREEN);
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
    draw_text(surface, 146, 53, "TALES  ALE  REST", ANSI_WHITE);
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
    p4_draw_fill_rect(surface, 0, 118, 320, 14, ANSI_BLACK);
    draw_text(surface, 8, 121, lord_rip_scene_name(state->rip_scene),
              ANSI_BRIGHT_CYAN);
    draw_text(surface, 214, 121, "A/B: GALLERY", ANSI_YELLOW);
}

static void draw_keyboard(p4_game_surface_t *surface,
                          const lord_state_t *state)
{
    p4_draw_fill_rect(surface, 8, 28, 304, 24, ANSI_PANEL);
    p4_draw_rect(surface, 8, 28, 304, 24, ANSI_BLUE);
    draw_text(surface, 13, 36,
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
    p4_draw_fill_rect(surface, 8, 29, 304, 99, ANSI_PANEL);
    p4_draw_rect(surface, 8, 29, 304, 99, ANSI_BLUE);
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
        draw_text(surface, 14, 37 + row * 12, line,
                  row % 2 == 0 ? ANSI_WHITE : ANSI_LIGHT_GRAY);
        ++row;
    }
}

static void draw_rankings(p4_game_surface_t *surface,
                          const lord_state_t *state)
{
    p4_draw_fill_rect(surface, 8, 29, 304, 99, ANSI_PANEL);
    p4_draw_rect(surface, 8, 29, 304, 99, ANSI_BLUE);
    char line[52];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "1  ");
    line_append(line, sizeof(line), state->player.name);
    line_append(line, sizeof(line), "  LV ");
    line_append_u32(line, sizeof(line), state->player.level);
    line_append(line, sizeof(line), "  DR ");
    line_append_u32(line, sizeof(line), state->player.dragon_kills);
    draw_text(surface, 14, 36, line, ANSI_YELLOW);
    for (size_t index = 0U; index < 7U; ++index) {
        line_clear(line, sizeof(line));
        line_append_u32(line, sizeof(line), (uint32_t)index + 2U);
        line_append(line, sizeof(line), "  ");
        line_append(line, sizeof(line), state->realm[index].name);
        line_append(line, sizeof(line), "  LV ");
        line_append_u32(line, sizeof(line), state->realm[index].level);
        line_append(line, sizeof(line), "  PVP ");
        line_append_u32(line, sizeof(line), state->realm[index].pvp_wins);
        draw_text(surface, 14, 48 + (int)index * 11, line,
                  index % 2U == 0U ? ANSI_WHITE : ANSI_LIGHT_GRAY);
    }
}

static void draw_skills(p4_game_surface_t *surface,
                        const lord_state_t *state)
{
    p4_draw_fill_rect(surface, 8, 29, 304, 99, ANSI_PANEL);
    p4_draw_rect(surface, 8, 29, 304, 99, ANSI_BLUE);
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
    draw_text(surface, 17, 115,
              "Forest teachers and IGMs increase mastery.",
              ANSI_LIGHT_GRAY);
}

static void draw_blackjack(p4_game_surface_t *surface,
                           const lord_state_t *state)
{
    char line[52];
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "YOU ");
    line_append_u32(line, sizeof(line), state->blackjack_player);
    line_append(line, sizeof(line), "  DEALER ");
    line_append_u32(line, sizeof(line), state->blackjack_dealer);
    line_append(line, sizeof(line), "  GOLD ");
    line_append_u32(line, sizeof(line), state->player.gold);
    draw_text(surface, 10, 29, line, ANSI_YELLOW);
    draw_text(surface, 10, 42, state->battle_line, ANSI_BRIGHT_CYAN);
    draw_menu(surface, state, s_blackjack_menu, 4U, 62);
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
    case LORD_SCREEN_SEX:
        draw_header(surface, "CHOOSE YOUR WARRIOR");
        draw_text(surface, 10, 29, state->player.name,
                  ANSI_BRIGHT_MAGENTA);
        draw_menu(surface, state, s_sex_menu, 2U, 50);
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
        line_append(line, sizeof(line), "Carried: ");
        line_append_u32(line, sizeof(line), state->player.gold);
        line_append(line, sizeof(line), "   Account: ");
        line_append_u32(line, sizeof(line), state->player.bank);
        draw_text(surface, 10, 29, line, ANSI_YELLOW);
        draw_menu(surface, state, s_bank_menu, 4U, 52);
        break;
    }
    case LORD_SCREEN_BANK_TRANSFER:
        draw_header(surface, "TRANSFER 100 GOLD");
        draw_realm_menu(surface, state, false, "Return to bank");
        break;
    case LORD_SCREEN_INN:
        draw_header(surface, "THE RED DRAGON INN");
        draw_text(surface, 10, 29,
                  "Firelight, rumors, and a safe bed.", ANSI_BROWN);
        draw_menu(surface, state, s_inn_menu, 11U, 43);
        break;
    case LORD_SCREEN_BARTENDER:
        draw_header(surface, "TALK WITH THE BARTENDER");
        draw_text(surface, 10, 29,
                  "Ale, gossip, and a dangerous contest.", ANSI_BROWN);
        draw_menu(surface, state, s_bartender_menu, 6U, 48);
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
        draw_text(surface, 10, 29,
                  "A grin, a lute, and a dangerous heart.",
                  ANSI_BRIGHT_MAGENTA);
        draw_menu(surface, state, s_npc_romance_menu, 6U, 48);
        break;
    case LORD_SCREEN_VIOLET:
        draw_header(surface, "VIOLET OF THE INN");
        draw_text(surface, 10, 29,
                  "Violet watches the room with bright eyes.",
                  ANSI_BRIGHT_MAGENTA);
        draw_menu(surface, state, s_npc_romance_menu, 6U, 48);
        break;
    case LORD_SCREEN_BLACKJACK:
        draw_header(surface, "RED DRAGON BLACKJACK");
        draw_blackjack(surface, state);
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
    case LORD_SCREEN_ROMANCE: {
        draw_header(surface, "LOCAL REALM: ROMANCE");
        draw_realm_menu(surface, state, true, "Return to town");
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), "Courtship actions remaining: ");
        line_append_u32(line, sizeof(line), state->romance_actions);
        draw_text(surface, 10, 120, line, ANSI_BRIGHT_MAGENTA);
        break;
    }
    case LORD_SCREEN_ROMANCE_ACTION: {
        draw_header(surface, "COURTSHIP");
        const char *name = state->selected_player < LORD_REALM_PLAYER_COUNT ?
            state->realm[state->selected_player].name : "Unknown";
        draw_text(surface, 10, 29, name, ANSI_BRIGHT_MAGENTA);
        draw_menu(surface, state, s_romance_action_menu, 5U, 48);
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
        p4_draw_rect(surface, 8, 38, 304, 58, ANSI_BLUE);
        draw_text(surface, 18, 52, state->message_line_1, ANSI_WHITE);
        draw_text(surface, 18, 69, state->message_line_2,
                  ANSI_BRIGHT_CYAN);
        draw_text(surface, 90, 111, "PRESS A TO CONTINUE", ANSI_YELLOW);
        break;
    case LORD_SCREEN_DEAD:
        draw_header(surface, "A DARKNESS FALLS");
        draw_text(surface, 84, 48, "YOU HAVE BEEN SLAIN", ANSI_BRIGHT_RED);
        draw_text(surface, 46, 69,
                  "The innkeeper drags you home.", ANSI_LIGHT_GRAY);
        draw_text(surface, 61, 102,
                  "PRESS A TO FACE THE MORNING", ANSI_YELLOW);
        break;
    case LORD_SCREEN_DRAGON_VICTORY:
        draw_header(surface, "THE RED DRAGON FALLS");
        draw_text(surface, 52, 42,
                  "YOUR NAME BECOMES LEGEND!", ANSI_YELLOW);
        draw_text(surface, 38, 64,
                  "The realm is free of crimson fire.", ANSI_WHITE);
        draw_text(surface, 43, 86,
                  "A stronger new adventure awaits.", ANSI_BRIGHT_GREEN);
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
        if (context->services->save_schema_version !=
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
    (void)elapsed_ms;
    lord_state_t *const state = context->state;
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        service_save(context, state);
        return P4_GAME_EXIT_TO_LAUNCHER;
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
        event = lord_activate(state);
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
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_SAVE,
    .state_bytes = sizeof(lord_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
