// SPDX-License-Identifier: LicenseRef-LORD-Permission
/* Original CP437 scenery. Five rows fit the unused space above the command
 * footer; the gallery uses the same art at the font's native 16-pixel height.
 * These are glyph compositions, not raster assets or saved gameplay state. */

typedef enum {
    LORD_SCENE_TOWN, LORD_SCENE_FOREST, LORD_SCENE_INN,
    LORD_SCENE_WEAPONS, LORD_SCENE_ARMOUR, LORD_SCENE_HEALER,
    LORD_SCENE_TRAINING, LORD_SCENE_BANK, LORD_SCENE_POST,
    LORD_SCENE_SETH, LORD_SCENE_VIOLET, LORD_SCENE_FRIENDS,
    LORD_SCENE_DICE, LORD_SCENE_NEWS, LORD_SCENE_HALL,
    LORD_SCENE_SKILLS, LORD_SCENE_CLUB, LORD_SCENE_REST,
    LORD_SCENE_DRAGON, LORD_SCENE_DUEL, LORD_SCENE_MATH,
    LORD_SCENE_HOUSE, LORD_SCENE_BAG, LORD_SCENE_GRAVEYARD,
    LORD_SCENE_YOUTH, LORD_SCENE_CABIN, LORD_SCENE_PICKLES,
    LORD_SCENE_DARKCLOAK, LORD_SCENE_COUNT,
} lord_scene_t;

typedef struct {
    /* Ten glyphs plus NUL, compile-time bounded rather than truncating art. */
    char rows[5][11];
} lord_scene_motif_t;

static const lord_scene_motif_t s_scene_castle = {{
    "[] [] []", "||_||_||", "|#@#@#|", "|#/^\\#|", "|_| |_|",
}};
static const lord_scene_motif_t s_scene_house = {{
    "    /\\", " __/##\\__", " /######\\", " |@|^^|@|", " |_|__|_|",
}};
static const lord_scene_motif_t s_scene_tree = {{
    "    ^", "   /#\\", "  /###\\", " /#####\\", "____||____",
}};
static const lord_scene_motif_t s_scene_trail = {{
    "  )    *", "     .", "    / \\", "   /   \\", "__/     \\_",
}};
static const lord_scene_motif_t s_scene_fountain = {{
    "    *", "  \\ | /", " __\\|/___", " \\~~~~~/", "___|#|____",
}};
static const lord_scene_motif_t s_scene_hearth = {{
    "_########_", "|#|    |#|", "|#| /\\ |#|", "|#|^@@^|#|", "|#|====|#|",
}};
static const lord_scene_motif_t s_scene_table = {{
    "    *", "   _|_", " o [_] o", "==========", " ||    ||",
}};
static const lord_scene_motif_t s_scene_bard = {{
    "  _/\\  &", "  (oo) &", " /|#/)_", "  |#(o )", " _/\\_)/",
}};
static const lord_scene_motif_t s_scene_violet = {{
    "   ___", "  /oo/)", " _|#|_  *", "[_]#| [_]", "  / \\",
}};
static const lord_scene_motif_t s_scene_hero = {{
    "   /\\", "  [oo]  /", " /|##|=/", "  |##|", " _/  \\_",
}};
static const lord_scene_motif_t s_scene_anvil = {{
    "  _   *", " [#] /", "__|_____", "\\######>", " _|##|_",
}};
static const lord_scene_motif_t s_scene_swords = {{
    " \\    /", "  \\  /", "   \\/", "  =/\\=", "  /  \\",
}};
static const lord_scene_motif_t s_scene_shield = {{
    " .______.", " |##@@##|", " |#@@@@#|", "  \\#@@#/", "   \\__/",
}};
static const lord_scene_motif_t s_scene_potion = {{
    "   [=]", "   | |  *", "  /~~~\\", " (~~~~~)", "  \\___/",
}};
static const lord_scene_motif_t s_scene_herbs = {{
    " *   *", " \\| /|/", "  \\|/", " [####]", "  |##|",
}};
static const lord_scene_motif_t s_scene_target = {{
    "  .----.", " / (()) \\", " | ((@))|", "  \\____/", " __/  \\__",
}};
static const lord_scene_motif_t s_scene_vault = {{
    ".========.", "|#______#|", "|#| (X)|#|", "|#|____|#|", "|########|",
}};
static const lord_scene_motif_t s_scene_coins = {{
    "     ___", " ___( C )", "( C )===", " ===( C )", "==========",
}};
static const lord_scene_motif_t s_scene_letter = {{
    "__________", "|\\      /|", "| \\____/ |", "| / ** \\ |", "|/______\\|",
}};
static const lord_scene_motif_t s_scene_quill = {{
    "     /##>", "    /##>", "   /#>", " _/_", "[___]____",
}};
static const lord_scene_motif_t s_scene_book = {{
    "____  ____", "|===\\/===|", "|===||===|", "|===||===|", "|___/\\___|",
}};
static const lord_scene_motif_t s_scene_dice = {{
    " .----.", " |o  o|__", " | o o|o |", " '----' o|", "    |o___|",
}};
static const lord_scene_motif_t s_scene_trophy = {{
    " __####__", "( \\##/ )", " \\_||_/", "    ||", "  __##__",
}};
static const lord_scene_motif_t s_scene_banner = {{
    "|\\______", "|#  **  |", "|#  /\\  |", "|#______|", "|   \\/",
}};
static const lord_scene_motif_t s_scene_bed = {{
    "|    *", "|_oo_____", "|[__]####|", "|========|", "|        |",
}};
static const lord_scene_motif_t s_scene_window = {{
    " .______.", " |  )| *|", " |---+--|", " | * |  |", " '------'",
}};
static const lord_scene_motif_t s_scene_dragon = {{
    " /\\  /\\_/>", "<##\\/o o)", " \\###\\^/", " _\\##/\\__", "<___/  \\_>",
}};
static const lord_scene_motif_t s_scene_bag = {{
    "   \\|/", "   /=\\", "  /###\\ *", " (##?##)", "  \\___/",
}};
static const lord_scene_motif_t s_scene_graves = {{
    "   ___", "  /   \\", "  | + |", "__|___|__", "##~~##~~##",
}};
static const lord_scene_motif_t s_scene_cabin = {{
    "    /\\", " __/##\\__", " |  __  |", " | | )| |", " |_|__|_|",
}};
static const lord_scene_motif_t s_scene_pickles = {{
    "  [====]", " /      \\", " | (~)(~)|", " | (~)(~)|", "  \\____/",
}};

typedef struct {
    const lord_scene_motif_t *motif[3];
    uint16_t color[3];
    const char *caption;
} lord_scene_composition_t;

static const lord_scene_composition_t s_ansi_scenes[LORD_SCENE_COUNT] = {
    {{&s_scene_castle, &s_scene_fountain, &s_scene_house},
     {ANSI_BRIGHT_BLUE, ANSI_BRIGHT_CYAN, ANSI_BRIGHT_RED}, "LANTERNS LIGHT THE TOWN SQUARE"},
    {{&s_scene_tree, &s_scene_trail, &s_scene_tree},
     {ANSI_BRIGHT_GREEN, ANSI_LIGHT_GRAY, ANSI_CYAN}, "FOLLOW THE MOONLIT FOREST TRAIL"},
    {{&s_scene_hearth, &s_scene_bard, &s_scene_table},
     {ANSI_BRIGHT_RED, ANSI_BRIGHT_GREEN, ANSI_YELLOW}, "A WARM HEARTH AND A FRIENDLY TABLE"},
    {{&s_scene_anvil, &s_scene_swords, &s_scene_hearth},
     {ANSI_BRIGHT_CYAN, ANSI_WHITE, ANSI_BRIGHT_RED}, "HAMMERS RING IN ARTHUR'S FORGE"},
    {{&s_scene_shield, &s_scene_hero, &s_scene_anvil},
     {ANSI_BRIGHT_CYAN, ANSI_BRIGHT_BLUE, ANSI_LIGHT_GRAY}, "FIND YOUR NEXT SUIT OF ARMOUR"},
    {{&s_scene_house, &s_scene_potion, &s_scene_herbs},
     {ANSI_BRIGHT_GREEN, ANSI_BRIGHT_MAGENTA, ANSI_GREEN}, "HERBS AND POTIONS FOR THE ROAD"},
    {{&s_scene_swords, &s_scene_hero, &s_scene_target},
     {ANSI_LIGHT_GRAY, ANSI_BRIGHT_BLUE, ANSI_BRIGHT_RED}, "PRACTICE MAKES A BRAVER HERO"},
    {{&s_scene_coins, &s_scene_vault, &s_scene_coins},
     {ANSI_YELLOW, ANSI_BRIGHT_CYAN, ANSI_BROWN}, "YOUR CHOMPCOIN HAS A SAFE HOME"},
    {{&s_scene_quill, &s_scene_letter, &s_scene_house},
     {ANSI_WHITE, ANSI_BRIGHT_CYAN, ANSI_BRIGHT_BLUE}, "A LETTER CAN START AN ADVENTURE"},
    {{&s_scene_hearth, &s_scene_bard, &s_scene_book},
     {ANSI_BRIGHT_RED, ANSI_BRIGHT_GREEN, ANSI_BRIGHT_CYAN}, "SETH ALWAYS HAS ANOTHER SONG"},
    {{&s_scene_herbs, &s_scene_violet, &s_scene_table},
     {ANSI_BRIGHT_GREEN, ANSI_BRIGHT_MAGENTA, ANSI_YELLOW}, "VIOLET SAVED A PLACE FOR A FRIEND"},
    {{&s_scene_hero, &s_scene_table, &s_scene_violet},
     {ANSI_BRIGHT_CYAN, ANSI_YELLOW, ANSI_BRIGHT_MAGENTA}, "GOOD FRIENDS MAKE GREAT TEAMS"},
    {{&s_scene_coins, &s_scene_dice, &s_scene_table},
     {ANSI_YELLOW, ANSI_WHITE, ANSI_BRIGHT_GREEN}, "ROLL, HOLD, AND CHEER YOUR FRIENDS"},
    {{&s_scene_castle, &s_scene_book, &s_scene_quill},
     {ANSI_BRIGHT_BLUE, ANSI_YELLOW, ANSI_WHITE}, "TALES TRAVEL FROM DOOR TO DOOR"},
    {{&s_scene_banner, &s_scene_trophy, &s_scene_castle},
     {ANSI_BRIGHT_RED, ANSI_YELLOW, ANSI_BRIGHT_BLUE}, "EVERY DRAGON DEED TELLS A STORY"},
    {{&s_scene_swords, &s_scene_book, &s_scene_potion},
     {ANSI_BRIGHT_RED, ANSI_BRIGHT_CYAN, ANSI_BRIGHT_MAGENTA}, "THREE PATHS, ONE GREAT ADVENTURE"},
    {{&s_scene_banner, &s_scene_shield, &s_scene_banner},
     {ANSI_BRIGHT_MAGENTA, ANSI_YELLOW, ANSI_BRIGHT_CYAN}, "RAISE YOUR BANNER WITH FRIENDS"},
    {{&s_scene_window, &s_scene_bed, &s_scene_potion},
     {ANSI_BRIGHT_BLUE, ANSI_BRIGHT_CYAN, ANSI_BRIGHT_MAGENTA}, "REST HERE; TOMORROW IS A NEW DAY"},
    {{&s_scene_banner, &s_scene_dragon, &s_scene_trophy},
     {ANSI_BRIGHT_GREEN, ANSI_BRIGHT_RED, ANSI_YELLOW}, "THE RED DRAGON'S LEGEND LIVES ON"},
    {{&s_scene_hero, &s_scene_swords, &s_scene_shield},
     {ANSI_BRIGHT_BLUE, ANSI_YELLOW, ANSI_BRIGHT_RED}, "A FRIENDLY CHALLENGE IN THE ARENA"},
    {{&s_scene_book, &s_scene_quill, &s_scene_coins},
     {ANSI_BRIGHT_CYAN, ANSI_WHITE, ANSI_YELLOW}, "ARAGORN HAS A PUZZLE FOR YOU"},
    {{&s_scene_tree, &s_scene_house, &s_scene_table},
     {ANSI_GREEN, ANSI_BRIGHT_RED, ANSI_YELLOW}, "KNOCK ON BARAK'S DOOR"},
    {{&s_scene_book, &s_scene_bag, &s_scene_cabin},
     {ANSI_BRIGHT_CYAN, ANSI_BRIGHT_MAGENTA, ANSI_BROWN}, "WHAT WILL YOU FIND IN THE GRAB BAG?"},
    {{&s_scene_tree, &s_scene_graves, &s_scene_window},
     {ANSI_CYAN, ANSI_LIGHT_GRAY, ANSI_BRIGHT_BLUE}, "OLD STONES REMEMBER OLD HEROES"},
    {{&s_scene_hero, &s_scene_banner, &s_scene_house},
     {ANSI_BRIGHT_CYAN, ANSI_YELLOW, ANSI_BRIGHT_GREEN}, "HELP THE NEXT HERO FIND THEIR WAY"},
    {{&s_scene_tree, &s_scene_cabin, &s_scene_herbs},
     {ANSI_GREEN, ANSI_BROWN, ANSI_BRIGHT_GREEN}, "A SMALL CABIN OFF THE FOREST PATH"},
    {{&s_scene_herbs, &s_scene_pickles, &s_scene_trophy},
     {ANSI_BRIGHT_GREEN, ANSI_GREEN, ANSI_YELLOW}, "A VERY CURIOUS PICKLE SHRINE"},
    {{&s_scene_window, &s_scene_dice, &s_scene_hearth},
     {ANSI_BRIGHT_BLUE, ANSI_WHITE, ANSI_BRIGHT_RED}, "DARKCLOAK'S LANTERNS WELCOME YOU"},
};

static void draw_scene_motif(p4_game_surface_t *surface,
                              const lord_scene_motif_t *motif,
                              int x, int y, uint16_t primary,
                              uint8_t cell_height)
{
    for (size_t row = 0U; row < 5U; ++row) {
        for (size_t column = 0U; column < 10U; ++column) {
            const uint8_t marker = (uint8_t)motif->rows[row][column];
            if (marker == 0U) {
                break;
            }
            if (marker == (uint8_t)' ') {
                continue;
            }
            uint8_t glyph = marker;
            uint16_t color = primary;
            switch (marker) {
            case '#': glyph = CP437_SHADE_MEDIUM; break;
            case '@': glyph = CP437_BLOCK; color = ANSI_YELLOW; break;
            case '~': glyph = CP437_SHADE_LIGHT; break;
            case '*': glyph = CP437_STAR; color = ANSI_YELLOW; break;
            case '&': glyph = CP437_MUSIC; color = ANSI_YELLOW; break;
            case '=': glyph = CP437_DOUBLE_HORIZONTAL; break;
            case 'o': color = ANSI_WHITE; break;
            case 'C': color = ANSI_YELLOW; break;
            default: break;
            }
            p4_draw_cp437_glyph(surface, x + (int)column * 8,
                                y + (int)row * cell_height,
                                glyph, color, ANSI_BLACK, cell_height);
        }
    }
}

static void draw_location_scene(p4_game_surface_t *surface,
                                 lord_scene_t scene, int y,
                                 uint8_t cell_height)
{
    if ((unsigned)scene >= LORD_SCENE_COUNT) {
        scene = LORD_SCENE_TOWN;
    }
    const lord_scene_composition_t *const composition = &s_ansi_scenes[scene];
    p4_draw_fill_rect(surface, 8, y, 304, 5 * cell_height, ANSI_BLACK);
    /* A continuous flagstone floor ties the three landmarks into one scene.
     * Its muted gutters stay behind the foreground silhouettes. */
    for (int x = 8; x < 312; x += 8) {
        p4_draw_cp437_glyph(surface, x, y + 4 * cell_height,
                            CP437_SHADE_LIGHT, ANSI_BROWN, ANSI_BLACK,
                            cell_height);
    }
    /* Each composition reserves ten columns per motif and three gutters.
     * Keeping captions outside this area prevents art obscuring choices. */
    for (size_t index = 0U; index < 3U; ++index) {
        draw_scene_motif(surface, composition->motif[index],
                          16 + (int)index * 104, y,
                          composition->color[index], cell_height);
    }
}

static lord_scene_t igm_location_scene(uint8_t igm)
{
    static const lord_scene_t scenes[LORD_IGM_COUNT] = {
        LORD_SCENE_MATH, LORD_SCENE_HOUSE, LORD_SCENE_BAG,
        LORD_SCENE_GRAVEYARD, LORD_SCENE_YOUTH, LORD_SCENE_CABIN,
        LORD_SCENE_PICKLES,
    };
    return igm < LORD_IGM_COUNT ? scenes[igm] : LORD_SCENE_TOWN;
}

static lord_scene_t screen_location_scene(const lord_state_t *state,
                                           lord_screen_t screen)
{
    switch (screen) {
    case LORD_SCREEN_FOREST: return LORD_SCENE_FOREST;
    case LORD_SCREEN_WEAPON_SHOP: return LORD_SCENE_WEAPONS;
    case LORD_SCREEN_ARMOR_SHOP: return LORD_SCENE_ARMOUR;
    case LORD_SCREEN_HEALER: return LORD_SCENE_HEALER;
    case LORD_SCREEN_TRAINING: return LORD_SCENE_TRAINING;
    case LORD_SCREEN_BANK: case LORD_SCREEN_BANK_TRANSFER: return LORD_SCENE_BANK;
    case LORD_SCREEN_INN: case LORD_SCREEN_BARTENDER:
    case LORD_SCREEN_CONVERSE: return LORD_SCENE_INN;
    case LORD_SCREEN_SETH: return LORD_SCENE_SETH;
    case LORD_SCREEN_VIOLET: return LORD_SCENE_VIOLET;
    case LORD_SCREEN_FRIENDSHIP: case LORD_SCREEN_FRIENDSHIP_ACTION:
        return LORD_SCENE_FRIENDS;
    case LORD_SCREEN_DRAGON_DICE: return LORD_SCENE_DICE;
    case LORD_SCREEN_MAILBOX: case LORD_SCREEN_MAIL_VIEW:
    case LORD_SCREEN_MAIL_COMPOSE: case LORD_SCREEN_TEXT_EDITOR:
        return LORD_SCENE_POST;
    case LORD_SCREEN_NEWS: return LORD_SCENE_NEWS;
    case LORD_SCREEN_RANKINGS: case LORD_SCREEN_STATS:
    case LORD_SCREEN_PLAYER_DETAIL: return LORD_SCENE_HALL;
    case LORD_SCREEN_CLASS: case LORD_SCREEN_SKILLS: return LORD_SCENE_SKILLS;
    case LORD_SCREEN_GUILD: case LORD_SCREEN_GUILD_CREATE:
    case LORD_SCREEN_GUILD_TARGET: case LORD_SCREEN_GUILD_STANDINGS:
        return LORD_SCENE_CLUB;
    case LORD_SCREEN_DEAD: return LORD_SCENE_REST;
    case LORD_SCREEN_DRAGON_VICTORY: return LORD_SCENE_DRAGON;
    case LORD_SCREEN_PLAYERS: return LORD_SCENE_DUEL;
    case LORD_SCREEN_ARAGORN_QUIZ: return LORD_SCENE_MATH;
    case LORD_SCREEN_IGM_DETAIL: return igm_location_scene(state->selected_igm);
    case LORD_SCREEN_IGM: return LORD_SCENE_BAG;
    case LORD_SCREEN_NAME: case LORD_SCREEN_HERO_STYLE: return LORD_SCENE_YOUTH;
    default: return LORD_SCENE_TOWN;
    }
}

static void draw_location_band(p4_game_surface_t *surface,
                                const lord_state_t *state)
{
    switch (state->screen) {
    case LORD_SCREEN_TITLE: case LORD_SCREEN_BATTLE: case LORD_SCREEN_MESSAGE:
    case LORD_SCREEN_RIP_SCENE:
        return; /* These screens already own their illustration area. */
    default: break;
    }
    p4_draw_fill_rect(surface, 16, 133, 288, 1, ANSI_BLUE);
    draw_location_scene(surface, screen_location_scene(state, state->screen),
                         136, P4_DRAW_CP437_COMPACT_HEIGHT);
}
