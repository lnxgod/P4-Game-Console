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

static const char *const s_town_menu[] = {
    "The Dark Forest",
    "Weapon Shop",
    "Armour Shop",
    "Violet's Healing Hut",
    "Turgon's Training",
    "The First Bank",
    "The Inn",
    "Other Warriors",
    "The Post Office",
    "Romance",
    "In-Game Modules",
    "RIP Art Gallery",
    "Character Stats",
};

static const char *const s_forest_menu[] = {
    "Hunt for a creature",
    "Search for the Red Dragon",
    "Return to town",
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
    "Return to town",
};

static const char *const s_inn_menu[] = {
    "Rest until a new day",
    "Listen to the bard",
    "Return to town",
};

static const char *const s_battle_menu[] = {
    "Attack",
    "Use class skill",
    "Run",
    "View stats",
};

static const char *const s_player_detail_menu[] = {
    "Challenge to a duel",
    "Send a sealed letter",
    "Court this warrior",
    "Return to player list",
};

static const char *const s_romance_action_menu[] = {
    "Offer a compliment",
    "Give a gift (100 gold)",
    "Propose marriage",
    "Return to romance list",
};

static const char *const s_igm_menu[] = {
    "Goblin Dice (100 gold)",
    "The Old Wizard",
    "The Herbalist (50 gold)",
    "Return to town",
};

static const char *const s_rip_gallery_menu[] = {
    "Town Square",
    "Dark Forest",
    "The Inn",
    "Battle Arena",
    "Red Dragon",
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
    const size_t end = scroll + 8U < count ? scroll + 8U : count;
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

static void draw_realm_menu(p4_game_surface_t *surface,
                            const lord_state_t *state,
                            bool show_affection,
                            const char *return_label)
{
    p4_draw_fill_rect(surface, 8, 33, 304, 82, ANSI_PANEL);
    p4_draw_rect(surface, 8, 33, 304, 82, ANSI_BLUE);
    for (size_t index = 0U; index <= LORD_REALM_PLAYER_COUNT; ++index) {
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
        const int y = 39 + (int)index * 16;
        if (selected) {
            p4_draw_fill_rect(surface, 10, y - 2, 300, 12, ANSI_RED);
        }
        draw_text(surface, 12, y, selected ? ">" : " ", ANSI_YELLOW);
        draw_text(surface, 24, y, line,
                  selected ? ANSI_WHITE : ANSI_LIGHT_GRAY);
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
    draw_menu(surface, state, s_player_detail_menu, 4U, 73);
}

static void draw_mailbox(p4_game_surface_t *surface,
                         const lord_state_t *state)
{
    const size_t count = lord_menu_count(state);
    const size_t scroll = state->menu_scroll;
    const size_t end = scroll + 8U < count ? scroll + 8U : count;
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
            line_append(line, sizeof(line), "Compose preset letter");
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
    const size_t end = scroll + 8U < count ? scroll + 8U : count;
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
    draw_text(surface, 218, 35, "LOCAL REALM", ANSI_YELLOW);
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
    line_append(line, sizeof(line), "/13");
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
    draw_text(surface, 40, 118, "A/START ENTER  -  LOCAL SESSION",
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
    draw_text(surface, 12, 31, lord_class_name(state->player.hero_class),
              ANSI_BRIGHT_MAGENTA);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "Level ");
    line_append_u32(line, sizeof(line), state->player.level);
    line_append(line, sizeof(line), "   Day ");
    line_append_u32(line, sizeof(line), state->player.day);
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
    line_append(line, sizeof(line), "Gold ");
    line_append_u32(line, sizeof(line), state->player.gold);
    line_append(line, sizeof(line), "   Bank ");
    line_append_u32(line, sizeof(line), state->player.bank);
    draw_text(surface, 12, 70, line, ANSI_YELLOW);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "Experience ");
    line_append_u32(line, sizeof(line), state->player.experience);
    draw_text(surface, 12, 83, line, ANSI_BRIGHT_GREEN);
    const lord_item_t *const weapon = lord_weapon(state->player.weapon);
    const lord_item_t *const armor = lord_armor(state->player.armor);
    draw_text(surface, 12, 96, weapon != NULL ? weapon->name : "Fists",
              ANSI_LIGHT_GRAY);
    draw_text(surface, 156, 96, armor != NULL ? armor->name : "Nothing",
              ANSI_LIGHT_GRAY);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "Forest fights ");
    line_append_u32(line, sizeof(line), state->player.forest_fights);
    line_append(line, sizeof(line), "  Skills ");
    line_append_u32(line, sizeof(line), state->player.skill_uses);
    draw_text(surface, 12, 109, line, ANSI_BRIGHT_CYAN);
    line_clear(line, sizeof(line));
    line_append(line, sizeof(line), "PvP ");
    line_append_u32(line, sizeof(line), state->player.pvp_wins);
    line_append(line, sizeof(line), "-");
    line_append_u32(line, sizeof(line), state->player.pvp_losses);
    line_append(line, sizeof(line), "  Duels left ");
    line_append_u32(line, sizeof(line), state->pvp_fights);
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
    draw_menu(surface, state, s_battle_menu, 4U, 72);
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

static void draw_rip_scene(p4_game_surface_t *surface,
                           const lord_state_t *state)
{
    switch (state->rip_scene) {
    case 0U: draw_rip_town(surface); break;
    case 1U: draw_rip_forest(surface); break;
    case 2U: draw_rip_inn(surface); break;
    case 3U: draw_rip_battle(surface); break;
    default: draw_rip_dragon(surface); break;
    }
    p4_draw_fill_rect(surface, 0, 118, 320, 14, ANSI_BLACK);
    draw_text(surface, 8, 121, lord_rip_scene_name(state->rip_scene),
              ANSI_BRIGHT_CYAN);
    draw_text(surface, 214, 121, "A/B: GALLERY", ANSI_YELLOW);
}

static void render_screen(p4_game_surface_t *surface,
                          const lord_state_t *state)
{
    switch (state->screen) {
    case LORD_SCREEN_TITLE:
        draw_header(surface, "LEGEND OF THE RED DRAGON");
        draw_title(surface);
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
        draw_menu(surface, state, s_forest_menu, 3U, 50);
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
        draw_menu(surface, state, s_bank_menu, 3U, 52);
        break;
    }
    case LORD_SCREEN_INN:
        draw_header(surface, "THE INN");
        draw_text(surface, 10, 29,
                  "Firelight, rumors, and a safe bed.", ANSI_BROWN);
        draw_menu(surface, state, s_inn_menu, 3U, 52);
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
                  "Preset text is used until OS text input arrives.",
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
        draw_menu(surface, state, s_romance_action_menu, 4U, 48);
        break;
    }
    case LORD_SCREEN_IGM: {
        draw_header(surface, "IN-GAME MODULES");
        draw_text(surface, 10, 29,
                  "Each module may be visited once per day.",
                  ANSI_BRIGHT_CYAN);
        draw_menu(surface, state, s_igm_menu, 4U, 48);
        char line[52];
        line_clear(line, sizeof(line));
        line_append(line, sizeof(line), "Used module mask: ");
        line_append_u32(line, sizeof(line), state->igm_used_mask);
        draw_text(surface, 10, 112, line, ANSI_DARK_GRAY);
        break;
    }
    case LORD_SCREEN_RIP_GALLERY:
        draw_header(surface, "RIP ART GALLERY");
        draw_text(surface, 10, 29,
                  "Code-drawn BBS scenes; choose an exhibit.",
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
    lord_initialize(context->state, UINT32_C(0x4c4f5244));
    (void)p4_game_play_tone(context, 392U, 90U, 3U,
                            P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(p4_game_context_t *context,
                                    const p4_game_input_t *input,
                                    uint32_t elapsed_ms)
{
    (void)elapsed_ms;
    lord_state_t *const state = context->state;
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    if ((input->pressed & (P4_BUTTON_UP | P4_BUTTON_LEFT)) != 0U) {
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
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE,
    .state_bytes = sizeof(lord_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
