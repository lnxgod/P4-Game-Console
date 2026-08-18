// SPDX-License-Identifier: LicenseRef-LORD-Permission

#include "lord_internal.h"

#include <string.h>

enum {
    LORD_WEAPON_COUNT = 16,
    LORD_ARMOR_COUNT = 16,
    LORD_MONSTERS_PER_LEVEL = 4,
};

static const lord_item_t s_weapons[LORD_WEAPON_COUNT] = {
    {"Fists", 0U, 0},
    {"Stick", 200U, 5},
    {"Dagger", 1000U, 10},
    {"Short Sword", 3000U, 20},
    {"Long Sword", 10000U, 30},
    {"Huge Axe", 30000U, 40},
    {"Bone Cruncher", 100000U, 60},
    {"Twin Swords", 150000U, 80},
    {"Power Axe", 200000U, 120},
    {"Able's Sword", 400000U, 180},
    {"Wans' Weapon", 1000000U, 250},
    {"Spear of Gold", 4000000U, 350},
    {"Crystal Shard", 10000000U, 500},
    {"Nira's Teeth", 40000000U, 800},
    {"Blood Sword", 100000000U, 1200},
    {"Death Sword", 400000000U, 1800},
};

static const lord_item_t s_armor[LORD_ARMOR_COUNT] = {
    {"Nothing", 0U, 0},
    {"Coat", 200U, 1},
    {"Heavy Coat", 1000U, 3},
    {"Leather Vest", 3000U, 10},
    {"Bronze Armour", 10000U, 15},
    {"Iron Armour", 30000U, 25},
    {"Graphite Armour", 100000U, 35},
    {"Erdricks Armour", 150000U, 50},
    {"Armour Of Death", 200000U, 75},
    {"Able's Armour", 400000U, 100},
    {"Full Body Armour", 1000000U, 150},
    {"Blood Armour", 4000000U, 225},
    {"Magic Protection", 10000000U, 300},
    {"Belar's Mail", 40000000U, 400},
    {"Golden Armour", 100000000U, 600},
    {"Armour Of Lore", 400000000U, 1000},
};

static const lord_trainer_t s_trainers[LORD_MAX_LEVEL - 1] = {
    {"Halder", 15, 30, 10, 5, 2, 100U},
    {"Barak", 17, 40, 15, 7, 3, 400U},
    {"Aragorn", 35, 70, 20, 10, 5, 1000U},
    {"Olodrin", 70, 120, 30, 12, 10, 4000U},
    {"Sandtiger", 100, 200, 50, 20, 15, 10000U},
    {"Sparhawk", 150, 400, 75, 35, 22, 40000U},
    {"Atsuko Sensei", 250, 600, 125, 50, 35, 100000U},
    {"Aladdin", 350, 800, 185, 75, 60, 400000U},
    {"Prince Caspian", 500, 1200, 250, 110, 80, 1000000U},
    {"Gandalf", 800, 1800, 350, 150, 120, 4000000U},
    {"Turgon", 1200, 2500, 550, 200, 150, 10000000U},
};

/* Four representative encounters from each upstream eleven-monster tier. */
static const lord_monster_t s_monsters[
    LORD_MAX_LEVEL * LORD_MONSTERS_PER_LEVEL] = {
    {"Small Thief", 6, 9, 56U, 2U},
    {"Rude Boy", 3, 7, 7U, 3U},
    {"Old Man", 5, 13, 73U, 4U},
    {"Large Green Rat", 3, 4, 32U, 1U},
    {"Green Python", 13, 17, 80U, 6U},
    {"Gath The Barbarian", 12, 13, 134U, 9U},
    {"Evil Wood Nymph", 15, 10, 160U, 11U},
    {"Fedrick The Baboon", 8, 23, 97U, 6U},
    {"Lazy Bum", 19, 29, 380U, 18U},
    {"Two Headed Hound", 18, 32, 384U, 17U},
    {"Purple Monchichi", 14, 29, 763U, 23U},
    {"Bone", 27, 11, 432U, 16U},
    {"Death Dog", 36, 52, 1150U, 36U},
    {"Weak Orc", 27, 32, 900U, 25U},
    {"Dark Elf", 43, 57, 1070U, 33U},
    {"Evil Hobbit", 35, 95, 1240U, 46U},
    {"Pandion Knight", 64, 59, 3100U, 98U},
    {"Jabba", 61, 198, 2384U, 137U},
    {"Manoken Sloth", 54, 69, 2452U, 97U},
    {"Trojan Warrior", 73, 87, 3432U, 154U},
    {"Iron Warrior", 100, 253, 6542U, 364U},
    {"Black Soul", 112, 432, 5865U, 432U},
    {"Gold Man", 86, 354, 8964U, 493U},
    {"Screaming Zombie", 98, 286, 5322U, 354U},
    {"Emperor Len", 210, 432, 12043U, 764U},
    {"Night Hawk", 220, 675, 10433U, 686U},
    {"Charging Rhino", 187, 454, 9853U, 654U},
    {"Goblin Pygmy", 165, 576, 13252U, 754U},
    {"Screeching Witch", 300, 674, 19753U, 2283U},
    {"Rundorig", 330, 675, 17853U, 2748U},
    {"Wheeler", 250, 786, 23433U, 1980U},
    {"Death Knight", 287, 674, 21923U, 4282U},
    {"Pink Elephant", 434, 1232, 33844U, 7843U},
    {"Gwendolen's Nightmare", 490, 764, 35846U, 8232U},
    {"Flying Cobra", 400, 1123, 37694U, 8433U},
    {"Rentaki's Pet", 556, 987, 37584U, 9854U},
    {"Torak's Son, Korak", 921, 1384, 46575U, 13877U},
    {"Brand The Wanderer", 643, 2788, 38755U, 13744U},
    {"The Grimest Reaper", 878, 1674, 39844U, 14237U},
    {"Death Dealer", 765, 1764, 47333U, 13877U},
    {"Gorma The Leper", 1132, 2766, 168774U, 26333U},
    {"Shogun Warrior", 1143, 3878, 165433U, 26555U},
    {"Weak Old Woman", 1543, 1878, 173522U, 37762U},
    {"Able's Creature", 985, 2455, 176775U, 28222U},
    {"Corinthian Giant", 2400, 2544, 336643U, 60333U},
    {"Screaming Eunuch", 1488, 2877, 197888U, 78884U},
    {"Black Warlock", 1366, 2767, 168483U, 58989U},
    {"Kal Torak", 876, 6666, 447774U, 94663U},
};

typedef struct {
    const char *name;
    lord_class_t hero_class;
    uint8_t level;
    int32_t hit_points;
    int32_t strength;
    int32_t defense;
    uint32_t gold;
    uint32_t experience;
} lord_realm_template_t;

static const lord_realm_template_t
    s_realm_templates[LORD_REALM_PLAYER_COUNT] = {
        {"Sir Galahad", LORD_CLASS_DEATH_KNIGHT, 1U,
         24, 11, 2, 320U, 25U},
        {"Moonwitch", LORD_CLASS_MYSTICAL, 2U,
         38, 19, 5, 850U, 130U},
        {"Shadow Jack", LORD_CLASS_THIEF, 3U,
         58, 30, 8, 1800U, 475U},
        {"Lady Celes", LORD_CLASS_MYSTICAL, 4U,
         92, 46, 13, 4200U, 1200U},
};

static const char *const s_rip_scene_names[] = {
    "Town Square",
    "Dark Forest",
    "The Inn",
    "Battle Arena",
    "Red Dragon",
};

static void text_copy(char *destination, size_t capacity,
                      const char *source)
{
    if (capacity == 0U) {
        return;
    }
    size_t index = 0U;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static size_t text_length(const char *text, size_t capacity)
{
    size_t length = 0U;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

static void text_append(char *destination, size_t capacity,
                        const char *source)
{
    size_t index = text_length(destination, capacity);
    size_t source_index = 0U;
    while (index + 1U < capacity && source[source_index] != '\0') {
        destination[index] = source[source_index];
        ++index;
        ++source_index;
    }
    if (index < capacity) {
        destination[index] = '\0';
    }
}

static void text_append_u32(char *destination, size_t capacity,
                            uint32_t value)
{
    char reversed[11];
    size_t digits = 0U;
    do {
        reversed[digits] = (char)('0' + (char)(value % 10U));
        ++digits;
        value /= 10U;
    } while (value != 0U && digits < sizeof(reversed));
    while (digits > 0U) {
        char character[2];
        --digits;
        character[0] = reversed[digits];
        character[1] = '\0';
        text_append(destination, capacity, character);
    }
}

static uint32_t random_next(lord_state_t *state)
{
    uint32_t value = state->rng_state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    state->rng_state = value;
    return value;
}

static uint32_t random_below(lord_state_t *state, uint32_t upper_bound)
{
    if (upper_bound == 0U) {
        return 0U;
    }
    return random_next(state) % upper_bound;
}

static uint32_t add_u32_saturating(uint32_t left, uint32_t right)
{
    if (UINT32_MAX - left < right) {
        return UINT32_MAX;
    }
    return left + right;
}

static void set_screen(lord_state_t *state, lord_screen_t screen)
{
    state->screen = screen;
    state->selection = 0U;
    state->menu_scroll = 0U;
}

static void set_message(lord_state_t *state, lord_screen_t return_screen,
                        const char *line_1, const char *line_2)
{
    text_copy(state->message_line_1, sizeof(state->message_line_1), line_1);
    text_copy(state->message_line_2, sizeof(state->message_line_2), line_2);
    state->return_screen = return_screen;
    set_screen(state, LORD_SCREEN_MESSAGE);
}

static void mark_dirty(lord_state_t *state)
{
    state->save_dirty = true;
    if (state->save_sequence != UINT32_MAX) {
        ++state->save_sequence;
    }
}

static void initialize_realm(lord_state_t *state)
{
    state->spouse_index = -1;
    state->pvp_fights = LORD_PVP_FIGHTS_PER_DAY;
    state->romance_actions = LORD_ROMANCE_ACTIONS_PER_DAY;
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        const lord_realm_template_t *const source =
            &s_realm_templates[index];
        lord_realm_player_t *const target = &state->realm[index];
        text_copy(target->name, sizeof(target->name), source->name);
        target->hero_class = source->hero_class;
        target->level = source->level;
        target->alive = true;
        target->married = false;
        target->affection = 0U;
        target->hit_points = source->hit_points;
        target->max_hit_points = source->hit_points;
        target->strength = source->strength;
        target->defense = source->defense;
        target->gold = source->gold;
        target->experience = source->experience;
    }
    state->mail_count = 2U;
    state->mail[0] = (lord_mail_t){
        .sender = (uint8_t)LORD_REALM_PLAYER_COUNT,
        .kind = LORD_MAIL_WELCOME,
        .unread = true,
    };
    state->mail[1] = (lord_mail_t){
        .sender = (uint8_t)(LORD_REALM_PLAYER_COUNT + 1U),
        .kind = LORD_MAIL_TRAINER,
        .unread = true,
    };
    state->realm_revision = 1U;
}

static void add_mail(lord_state_t *state, uint8_t sender,
                     lord_mail_kind_t kind)
{
    if (state->mail_count >= LORD_MAIL_COUNT_MAX) {
        for (size_t index = 1U; index < LORD_MAIL_COUNT_MAX; ++index) {
            state->mail[index - 1U].sender = state->mail[index].sender;
            state->mail[index - 1U].kind = state->mail[index].kind;
            state->mail[index - 1U].unread = state->mail[index].unread;
        }
        state->mail_count = LORD_MAIL_COUNT_MAX - 1U;
    }
    state->mail[state->mail_count] = (lord_mail_t){
        .sender = sender,
        .kind = kind,
        .unread = true,
    };
    ++state->mail_count;
    if (state->realm_revision != UINT32_MAX) {
        ++state->realm_revision;
    }
    mark_dirty(state);
}

static void reset_new_day(lord_state_t *state)
{
    state->player.hit_points = state->player.max_hit_points;
    state->player.forest_fights = LORD_FOREST_FIGHTS_PER_DAY;
    state->player.skill_uses = LORD_CLASS_SKILLS_PER_DAY;
    state->pvp_fights = LORD_PVP_FIGHTS_PER_DAY;
    state->romance_actions = LORD_ROMANCE_ACTIONS_PER_DAY;
    state->igm_used_mask = 0U;
    state->player.bank = add_u32_saturating(
        state->player.bank, state->player.bank / 10U);
    if (state->spouse_index >= 0 &&
        state->spouse_index < LORD_REALM_PLAYER_COUNT) {
        state->player.bank = add_u32_saturating(
            state->player.bank, state->player.bank / 20U);
    }
    if (state->player.day != UINT16_MAX) {
        ++state->player.day;
    }
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        state->realm[index].alive = true;
        state->realm[index].hit_points =
            state->realm[index].max_hit_points;
    }
    if (state->realm_revision != UINT32_MAX) {
        ++state->realm_revision;
    }
    mark_dirty(state);
}

static void initialize_player(lord_state_t *state,
                              lord_class_t hero_class)
{
    const uint8_t former_kills = state->player.dragon_kills;
    const uint16_t former_day = state->player.day;
    const uint16_t former_pvp_wins = state->player.pvp_wins;
    const uint16_t former_pvp_losses = state->player.pvp_losses;
    state->player = (lord_player_t){
        .hero_class = hero_class,
        .level = 1U,
        .hit_points = 20 + (int32_t)former_kills * 5,
        .max_hit_points = 20 + (int32_t)former_kills * 5,
        .strength = 10 + (int32_t)former_kills * 2,
        .defense = 1 + (int32_t)former_kills,
        .gold = 500U,
        .forest_fights = LORD_FOREST_FIGHTS_PER_DAY,
        .skill_uses = LORD_CLASS_SKILLS_PER_DAY,
        .dragon_kills = former_kills,
        .day = former_day == 0U ? 1U : former_day,
        .pvp_wins = former_pvp_wins,
        .pvp_losses = former_pvp_losses,
    };
    mark_dirty(state);
}

static void enemy_from_monster(lord_state_t *state,
                               const lord_monster_t *monster)
{
    text_copy(state->enemy.name, sizeof(state->enemy.name), monster->name);
    state->enemy.hit_points = monster->hit_points;
    state->enemy.max_hit_points = monster->hit_points;
    state->enemy.strength = monster->strength;
    state->enemy.defense = 0;
    state->enemy.gold = monster->gold;
    state->enemy.experience = monster->experience;
}

static void begin_forest_battle(lord_state_t *state)
{
    const uint32_t level_index = (uint32_t)(state->player.level - 1U);
    const uint32_t monster_index = level_index * LORD_MONSTERS_PER_LEVEL +
        random_below(state, LORD_MONSTERS_PER_LEVEL);
    enemy_from_monster(state, &s_monsters[monster_index]);
    --state->player.forest_fights;
    mark_dirty(state);
    state->battle_kind = LORD_BATTLE_FOREST;
    text_copy(state->battle_line, sizeof(state->battle_line),
              "The forest grows suddenly quiet...");
    set_screen(state, LORD_SCREEN_BATTLE);
}

static void begin_trainer_battle(lord_state_t *state)
{
    const lord_trainer_t *const trainer = lord_current_trainer(state);
    if (trainer == NULL) {
        set_message(state, LORD_SCREEN_TRAINING,
                    "No trainer can teach you more.",
                    "Only the Red Dragon remains.");
        return;
    }
    text_copy(state->enemy.name, sizeof(state->enemy.name), trainer->name);
    state->enemy.hit_points = trainer->hit_points;
    state->enemy.max_hit_points = trainer->hit_points;
    state->enemy.strength = trainer->strength;
    state->enemy.defense = 0;
    state->enemy.gold = 0U;
    state->enemy.experience = 0U;
    state->battle_kind = LORD_BATTLE_TRAINER;
    text_copy(state->battle_line, sizeof(state->battle_line),
              "Your master raises a weapon.");
    set_screen(state, LORD_SCREEN_BATTLE);
}

static void begin_dragon_battle(lord_state_t *state)
{
    text_copy(state->enemy.name, sizeof(state->enemy.name),
              "The Red Dragon");
    state->enemy.hit_points = 15000;
    state->enemy.max_hit_points = 15000;
    state->enemy.strength = 2000;
    state->enemy.defense = 200;
    state->enemy.gold = 1000U;
    state->enemy.experience = 1000U;
    --state->player.forest_fights;
    mark_dirty(state);
    state->battle_kind = LORD_BATTLE_DRAGON;
    text_copy(state->battle_line, sizeof(state->battle_line),
              "Flame fills the horizon!");
    set_screen(state, LORD_SCREEN_BATTLE);
}

static void begin_pvp_battle(lord_state_t *state)
{
    if (state->selected_player >= LORD_REALM_PLAYER_COUNT) {
        set_message(state, LORD_SCREEN_PLAYERS,
                    "That warrior has left the realm.", "");
        return;
    }
    lord_realm_player_t *const opponent =
        &state->realm[state->selected_player];
    if (state->pvp_fights == 0U) {
        set_message(state, LORD_SCREEN_PLAYER_DETAIL,
                    "No player fights remain today.",
                    "Rest at the inn before dueling again.");
        return;
    }
    if (!opponent->alive) {
        set_message(state, LORD_SCREEN_PLAYER_DETAIL,
                    "That warrior is already defeated.",
                    "A new day will revive the realm.");
        return;
    }
    --state->pvp_fights;
    text_copy(state->enemy.name, sizeof(state->enemy.name), opponent->name);
    state->enemy.hit_points = opponent->hit_points;
    state->enemy.max_hit_points = opponent->max_hit_points;
    state->enemy.strength = opponent->strength;
    state->enemy.defense = opponent->defense;
    state->enemy.gold = opponent->gold;
    state->enemy.experience = opponent->experience;
    state->battle_kind = LORD_BATTLE_PVP;
    text_copy(state->battle_line, sizeof(state->battle_line),
              "The town gathers around the duel.");
    mark_dirty(state);
    set_screen(state, LORD_SCREEN_BATTLE);
}

static int32_t player_damage(lord_state_t *state, int32_t multiplier)
{
    const int32_t half = state->player.strength / 2;
    int32_t damage = half +
        (int32_t)random_below(state, (uint32_t)(half + 1));
    if (random_below(state, 10U) == 0U) {
        damage *= 3;
    }
    damage *= multiplier;
    damage -= state->enemy.defense;
    return damage < 1 ? 1 : damage;
}

static int32_t enemy_damage(lord_state_t *state)
{
    const int32_t half = state->enemy.strength / 2;
    int32_t damage = half +
        (int32_t)random_below(state, (uint32_t)(half + 1));
    damage -= state->player.defense;
    if (state->player.hero_class == LORD_CLASS_THIEF &&
        random_below(state, 5U) == 0U) {
        damage = 0;
    }
    return damage < 0 ? 0 : damage;
}

static void set_exchange_line(lord_state_t *state, const char *prefix,
                              int32_t dealt, int32_t received)
{
    text_copy(state->battle_line, sizeof(state->battle_line), prefix);
    text_append_u32(state->battle_line, sizeof(state->battle_line),
                    (uint32_t)dealt);
    if (received == 0) {
        text_append(state->battle_line, sizeof(state->battle_line),
                    "; you evade the reply.");
    } else {
        text_append(state->battle_line, sizeof(state->battle_line),
                    "; foe hits ");
        text_append_u32(state->battle_line, sizeof(state->battle_line),
                        (uint32_t)received);
        text_append(state->battle_line, sizeof(state->battle_line), ".");
    }
}

static lord_event_t finish_battle(lord_state_t *state)
{
    if (state->battle_kind == LORD_BATTLE_FOREST) {
        state->player.gold = add_u32_saturating(
            state->player.gold, state->enemy.gold);
        state->player.experience = add_u32_saturating(
            state->player.experience, state->enemy.experience);
        state->message_line_1[0] = '\0';
        text_append(state->message_line_1, sizeof(state->message_line_1),
                    "Victory! You gain ");
        text_append_u32(state->message_line_1, sizeof(state->message_line_1),
                        state->enemy.gold);
        text_append(state->message_line_1, sizeof(state->message_line_1),
                    " gold.");
        state->message_line_2[0] = '\0';
        text_append(state->message_line_2, sizeof(state->message_line_2),
                    "You gain ");
        text_append_u32(state->message_line_2, sizeof(state->message_line_2),
                        state->enemy.experience);
        text_append(state->message_line_2, sizeof(state->message_line_2),
                    " experience.");
        state->return_screen = LORD_SCREEN_FOREST;
        state->battle_kind = LORD_BATTLE_NONE;
        mark_dirty(state);
        set_screen(state, LORD_SCREEN_MESSAGE);
        return LORD_EVENT_WIN;
    }
    if (state->battle_kind == LORD_BATTLE_TRAINER) {
        const lord_trainer_t *const trainer = lord_current_trainer(state);
        if (trainer != NULL) {
            ++state->player.level;
            state->player.max_hit_points += trainer->hp_gained;
            state->player.hit_points = state->player.max_hit_points;
            state->player.strength += trainer->strength_gained;
            state->player.defense += trainer->defense_gained;
            set_message(state, LORD_SCREEN_TOWN,
                        "Your master yields. Level gained!",
                        "The realm grows more dangerous.");
            mark_dirty(state);
        }
        state->battle_kind = LORD_BATTLE_NONE;
        return LORD_EVENT_WIN;
    }
    if (state->battle_kind == LORD_BATTLE_PVP) {
        lord_realm_player_t *const opponent =
            &state->realm[state->selected_player];
        const uint32_t prize = opponent->gold / 2U;
        opponent->gold -= prize;
        opponent->hit_points = 0;
        opponent->alive = false;
        state->player.gold = add_u32_saturating(state->player.gold, prize);
        state->player.experience = add_u32_saturating(
            state->player.experience, opponent->experience / 4U + 1U);
        if (state->player.pvp_wins != UINT16_MAX) {
            ++state->player.pvp_wins;
        }
        if (state->realm_revision != UINT32_MAX) {
            ++state->realm_revision;
        }
        state->battle_kind = LORD_BATTLE_NONE;
        add_mail(state, state->selected_player, LORD_MAIL_PVP_VICTORY);
        set_message(state, LORD_SCREEN_PLAYERS,
                    "You win the player duel!",
                    "Half the opponent's carried gold is yours.");
        return LORD_EVENT_WIN;
    }
    state->battle_kind = LORD_BATTLE_NONE;
    ++state->player.dragon_kills;
    mark_dirty(state);
    set_screen(state, LORD_SCREEN_DRAGON_VICTORY);
    return LORD_EVENT_WIN;
}

static lord_event_t battle_round(lord_state_t *state, int32_t multiplier,
                                 const char *prefix)
{
    const int32_t dealt = player_damage(state, multiplier);
    state->enemy.hit_points -= dealt;
    if (state->enemy.hit_points <= 0) {
        state->enemy.hit_points = 0;
        return finish_battle(state);
    }
    const int32_t received = enemy_damage(state);
    state->player.hit_points -= received;
    mark_dirty(state);
    set_exchange_line(state, prefix, dealt, received);
    if (state->player.hit_points <= 0) {
        state->player.hit_points = 0;
        if (state->battle_kind == LORD_BATTLE_PVP) {
            lord_realm_player_t *const opponent =
                &state->realm[state->selected_player];
            opponent->hit_points = state->enemy.hit_points;
            if (state->player.pvp_losses != UINT16_MAX) {
                ++state->player.pvp_losses;
            }
            if (state->realm_revision != UINT32_MAX) {
                ++state->realm_revision;
            }
            mark_dirty(state);
        }
        state->battle_kind = LORD_BATTLE_NONE;
        set_screen(state, LORD_SCREEN_DEAD);
        return LORD_EVENT_LOSE;
    }
    return LORD_EVENT_HIT;
}

static lord_event_t buy_item(lord_state_t *state, bool weapon_shop)
{
    const uint8_t selected = state->selection;
    if (selected >= LORD_WEAPON_COUNT) {
        set_screen(state, LORD_SCREEN_TOWN);
        return LORD_EVENT_CONFIRM;
    }
    const lord_item_t *const items = weapon_shop ? s_weapons : s_armor;
    const uint8_t current_index = weapon_shop ? state->player.weapon :
        state->player.armor;
    if (selected == current_index) {
        set_message(state,
                    weapon_shop ? LORD_SCREEN_WEAPON_SHOP :
                                  LORD_SCREEN_ARMOR_SHOP,
                    "You already own that item.", "Choose another.");
        return LORD_EVENT_CONFIRM;
    }
    const uint32_t trade = items[current_index].price / 2U;
    const uint32_t funds = add_u32_saturating(state->player.gold, trade);
    if (funds < items[selected].price) {
        set_message(state,
                    weapon_shop ? LORD_SCREEN_WEAPON_SHOP :
                                  LORD_SCREEN_ARMOR_SHOP,
                    "The shopkeeper shakes their head.",
                    "You cannot afford that item.");
        return LORD_EVENT_CONFIRM;
    }
    state->player.gold = funds - items[selected].price;
    if (weapon_shop) {
        state->player.strength -= items[current_index].bonus;
        state->player.strength += items[selected].bonus;
        state->player.weapon = selected;
    } else {
        state->player.defense -= items[current_index].bonus;
        state->player.defense += items[selected].bonus;
        state->player.armor = selected;
    }
    set_message(state,
                weapon_shop ? LORD_SCREEN_WEAPON_SHOP :
                              LORD_SCREEN_ARMOR_SHOP,
                "The trade is made.", items[selected].name);
    mark_dirty(state);
    return LORD_EVENT_CONFIRM;
}

static lord_event_t send_preset_mail(lord_state_t *state,
                                     lord_screen_t return_screen)
{
    if (state->selected_player >= LORD_REALM_PLAYER_COUNT) {
        set_message(state, return_screen,
                    "No recipient was selected.", "");
        return LORD_EVENT_CONFIRM;
    }
    add_mail(state, state->selected_player, LORD_MAIL_REPLY);
    set_message(state, return_screen,
                "Your sealed letter is delivered.",
                "A reply is already waiting in the post.");
    return LORD_EVENT_CONFIRM;
}

static lord_event_t perform_romance_action(lord_state_t *state)
{
    if (state->selected_player >= LORD_REALM_PLAYER_COUNT) {
        set_screen(state, LORD_SCREEN_ROMANCE);
        return LORD_EVENT_CONFIRM;
    }
    lord_realm_player_t *const person =
        &state->realm[state->selected_player];
    if (state->selection == 3U) {
        set_screen(state, LORD_SCREEN_ROMANCE);
        return LORD_EVENT_CONFIRM;
    }
    if (state->romance_actions == 0U) {
        set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                    "No courtship actions remain today.",
                    "Rest at the inn and try tomorrow.");
        return LORD_EVENT_CONFIRM;
    }
    uint8_t affection_gain = 0U;
    if (state->selection == 0U) {
        affection_gain = 15U;
    } else if (state->selection == 1U) {
        if (state->player.gold < 100U) {
            set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                        "A proper gift costs 100 gold.",
                        "Return when your purse is heavier.");
            return LORD_EVENT_CONFIRM;
        }
        state->player.gold -= 100U;
        affection_gain = 30U;
    } else {
        --state->romance_actions;
        if (state->spouse_index >= 0) {
            set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                        "You have already made your vow.",
                        "The realm remembers every promise.");
            mark_dirty(state);
            return LORD_EVENT_CONFIRM;
        }
        if (person->affection < 60U || person->married) {
            add_mail(state, state->selected_player, LORD_MAIL_ROMANCE);
            set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                        "Your proposal is gently refused.",
                        "Build more affection before asking again.");
            return LORD_EVENT_CONFIRM;
        }
        state->spouse_index = (int8_t)state->selected_player;
        person->married = true;
        state->player.max_hit_points += 5;
        state->player.hit_points += 5;
        add_mail(state, state->selected_player, LORD_MAIL_PROPOSAL);
        set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                    "Wedding bells ring across the realm!",
                    "Marriage grants 5 HP and an inn bonus.");
        return LORD_EVENT_CONFIRM;
    }
    --state->romance_actions;
    const uint16_t affection =
        (uint16_t)person->affection + (uint16_t)affection_gain;
    person->affection = affection > 100U ? 100U : (uint8_t)affection;
    if (state->realm_revision != UINT32_MAX) {
        ++state->realm_revision;
    }
    mark_dirty(state);
    set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                state->selection == 0U ?
                    "Your compliment earns a warm smile." :
                    "The gift is accepted with delight.",
                "Affection in the realm has grown.");
    return LORD_EVENT_CONFIRM;
}

static lord_event_t run_igm(lord_state_t *state)
{
    if (state->selection >= LORD_IGM_COUNT) {
        set_screen(state, LORD_SCREEN_TOWN);
        return LORD_EVENT_CONFIRM;
    }
    const uint8_t used_bit = (uint8_t)(1U << state->selection);
    if ((state->igm_used_mask & used_bit) != 0U) {
        set_message(state, LORD_SCREEN_IGM,
                    "That IGM is closed for today.",
                    "A new day resets every module.");
        return LORD_EVENT_CONFIRM;
    }
    if (state->selection == 0U) {
        if (state->player.gold < 100U) {
            set_message(state, LORD_SCREEN_IGM,
                        "Goblin Dice requires a 100 gold stake.", "");
            return LORD_EVENT_CONFIRM;
        }
        state->player.gold -= 100U;
        if (random_below(state, 2U) == 0U) {
            state->player.gold = add_u32_saturating(
                state->player.gold, 250U);
            set_message(state, LORD_SCREEN_IGM,
                        "The goblin dice flash double sixes!",
                        "You collect 250 gold.");
        } else {
            set_message(state, LORD_SCREEN_IGM,
                        "The goblins sweep away your stake.",
                        "Luck may return tomorrow.");
        }
    } else if (state->selection == 1U) {
        const uint32_t fate = random_below(state, 3U);
        if (fate == 0U) {
            state->player.strength += 2;
            set_message(state, LORD_SCREEN_IGM,
                        "The old wizard enchants your blade.",
                        "Strength rises by 2.");
        } else if (fate == 1U) {
            state->player.defense += 1;
            set_message(state, LORD_SCREEN_IGM,
                        "A silver ward settles on your armour.",
                        "Defense rises by 1.");
        } else {
            state->player.max_hit_points += 5;
            state->player.hit_points += 5;
            set_message(state, LORD_SCREEN_IGM,
                        "Ancient vitality fills your body.",
                        "Maximum HP rises by 5.");
        }
    } else {
        if (state->player.gold < 50U) {
            set_message(state, LORD_SCREEN_IGM,
                        "The herbalist asks 50 gold.", "");
            return LORD_EVENT_CONFIRM;
        }
        state->player.gold -= 50U;
        state->player.hit_points = state->player.max_hit_points;
        set_message(state, LORD_SCREEN_IGM,
                    "The herbalist restores every wound.",
                    "You return to full health.");
    }
    state->igm_used_mask = (uint8_t)(state->igm_used_mask | used_bit);
    mark_dirty(state);
    return LORD_EVENT_CONFIRM;
}

void lord_initialize(lord_state_t *state, uint32_t seed)
{
    memset(state, 0, sizeof(*state));
    state->rng_state = seed == 0U ? UINT32_C(0x4c4f5244) : seed;
    initialize_realm(state);
    state->save_dirty = false;
    state->save_sequence = 0U;
    state->screen = LORD_SCREEN_TITLE;
}

size_t lord_menu_count(const lord_state_t *state)
{
    switch (state->screen) {
    case LORD_SCREEN_CLASS:
        return 3U;
    case LORD_SCREEN_TOWN:
        return 13U;
    case LORD_SCREEN_FOREST:
        return 3U;
    case LORD_SCREEN_WEAPON_SHOP:
    case LORD_SCREEN_ARMOR_SHOP:
        return 17U;
    case LORD_SCREEN_HEALER:
        return 3U;
    case LORD_SCREEN_TRAINING:
        return 2U;
    case LORD_SCREEN_BANK:
    case LORD_SCREEN_INN:
        return 3U;
    case LORD_SCREEN_BATTLE:
        return 4U;
    case LORD_SCREEN_PLAYERS:
    case LORD_SCREEN_MAIL_COMPOSE:
    case LORD_SCREEN_ROMANCE:
        return 5U;
    case LORD_SCREEN_RIP_GALLERY:
        return LORD_RIP_SCENE_COUNT + 1U;
    case LORD_SCREEN_PLAYER_DETAIL:
    case LORD_SCREEN_ROMANCE_ACTION:
    case LORD_SCREEN_IGM:
        return 4U;
    case LORD_SCREEN_MAILBOX:
        return (size_t)state->mail_count + 2U;
    case LORD_SCREEN_TITLE:
    case LORD_SCREEN_STATS:
    case LORD_SCREEN_MESSAGE:
    case LORD_SCREEN_DEAD:
    case LORD_SCREEN_DRAGON_VICTORY:
    case LORD_SCREEN_MAIL_VIEW:
    case LORD_SCREEN_RIP_SCENE:
        return 1U;
    }
    return 0U;
}

void lord_move_selection(lord_state_t *state, int direction)
{
    const size_t count = lord_menu_count(state);
    if (count <= 1U || direction == 0) {
        return;
    }
    size_t selected = state->selection;
    if (direction < 0) {
        selected = selected == 0U ? count - 1U : selected - 1U;
    } else {
        selected = (selected + 1U) % count;
    }
    state->selection = (uint8_t)selected;
    if (selected < state->menu_scroll) {
        state->menu_scroll = (uint8_t)selected;
    } else if (selected >= (size_t)state->menu_scroll + 8U) {
        state->menu_scroll = (uint8_t)(selected - 7U);
    }
}

lord_event_t lord_activate(lord_state_t *state)
{
    switch (state->screen) {
    case LORD_SCREEN_TITLE:
        set_screen(state, LORD_SCREEN_CLASS);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_CLASS:
        initialize_player(state, (lord_class_t)state->selection);
        set_message(state, LORD_SCREEN_TOWN,
                    "Welcome to the realm, warrior.",
                    "Seek the Red Dragon and survive.");
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_TOWN:
        switch (state->selection) {
        case 0U: set_screen(state, LORD_SCREEN_FOREST); break;
        case 1U: set_screen(state, LORD_SCREEN_WEAPON_SHOP); break;
        case 2U: set_screen(state, LORD_SCREEN_ARMOR_SHOP); break;
        case 3U: set_screen(state, LORD_SCREEN_HEALER); break;
        case 4U: set_screen(state, LORD_SCREEN_TRAINING); break;
        case 5U: set_screen(state, LORD_SCREEN_BANK); break;
        case 6U: set_screen(state, LORD_SCREEN_INN); break;
        case 7U: set_screen(state, LORD_SCREEN_PLAYERS); break;
        case 8U: set_screen(state, LORD_SCREEN_MAILBOX); break;
        case 9U: set_screen(state, LORD_SCREEN_ROMANCE); break;
        case 10U: set_screen(state, LORD_SCREEN_IGM); break;
        case 11U: set_screen(state, LORD_SCREEN_RIP_GALLERY); break;
        default:
            state->return_screen = LORD_SCREEN_TOWN;
            set_screen(state, LORD_SCREEN_STATS);
            break;
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_FOREST:
        if (state->selection == 2U) {
            set_screen(state, LORD_SCREEN_TOWN);
        } else if (state->player.forest_fights == 0U) {
            set_message(state, LORD_SCREEN_FOREST,
                        "You are too weary to fight.",
                        "Rest at the inn for a new day.");
        } else if (state->selection == 0U) {
            begin_forest_battle(state);
        } else if (state->player.level < LORD_MAX_LEVEL) {
            set_message(state, LORD_SCREEN_FOREST,
                        "The dragon ignores your challenge.",
                        "Reach level 12 first.");
        } else {
            begin_dragon_battle(state);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_WEAPON_SHOP:
        return buy_item(state, true);
    case LORD_SCREEN_ARMOR_SHOP:
        return buy_item(state, false);
    case LORD_SCREEN_HEALER: {
        if (state->selection == 2U) {
            set_screen(state, LORD_SCREEN_TOWN);
            return LORD_EVENT_CONFIRM;
        }
        const int32_t missing = state->player.max_hit_points -
            state->player.hit_points;
        const int32_t wanted = state->selection == 0U ? 1 : missing;
        const uint32_t cost = (uint32_t)wanted * 5U;
        if (missing <= 0) {
            set_message(state, LORD_SCREEN_HEALER,
                        "You are already at full health.", "");
        } else if (state->player.gold < cost) {
            set_message(state, LORD_SCREEN_HEALER,
                        "Healing costs 5 gold per point.",
                        "You do not have enough.");
        } else {
            state->player.gold -= cost;
            state->player.hit_points += wanted;
            mark_dirty(state);
            set_message(state, LORD_SCREEN_HEALER,
                        "The healer tends your wounds.",
                        "You feel stronger.");
        }
        return LORD_EVENT_CONFIRM;
    }
    case LORD_SCREEN_TRAINING: {
        if (state->selection == 1U) {
            set_screen(state, LORD_SCREEN_TOWN);
            return LORD_EVENT_CONFIRM;
        }
        const lord_trainer_t *const trainer = lord_current_trainer(state);
        if (trainer == NULL) {
            set_message(state, LORD_SCREEN_TRAINING,
                        "Your training is complete.",
                        "The Red Dragon awaits.");
        } else if (state->player.experience < trainer->experience_needed) {
            set_message(state, LORD_SCREEN_TRAINING,
                        "Your master refuses the challenge.",
                        "Gain more experience first.");
        } else {
            begin_trainer_battle(state);
        }
        return LORD_EVENT_CONFIRM;
    }
    case LORD_SCREEN_BANK:
        if (state->selection == 0U) {
            state->player.bank = add_u32_saturating(
                state->player.bank, state->player.gold);
            state->player.gold = 0U;
            mark_dirty(state);
            set_message(state, LORD_SCREEN_BANK,
                        "All carried gold is deposited.", "");
        } else if (state->selection == 1U) {
            state->player.gold = add_u32_saturating(
                state->player.gold, state->player.bank);
            state->player.bank = 0U;
            mark_dirty(state);
            set_message(state, LORD_SCREEN_BANK,
                        "Your account is withdrawn.", "");
        } else {
            set_screen(state, LORD_SCREEN_TOWN);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_INN:
        if (state->selection == 0U) {
            reset_new_day(state);
            set_message(state, LORD_SCREEN_TOWN,
                        "A new day dawns over the realm.",
                        "Your strength has returned.");
        } else if (state->selection == 1U) {
            set_message(state, LORD_SCREEN_INN,
                        "A bard whispers of a crimson beast",
                        "beyond the deepest forest path.");
        } else {
            set_screen(state, LORD_SCREEN_TOWN);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_PLAYERS:
        if (state->selection >= LORD_REALM_PLAYER_COUNT) {
            set_screen(state, LORD_SCREEN_TOWN);
        } else {
            state->selected_player = state->selection;
            set_screen(state, LORD_SCREEN_PLAYER_DETAIL);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_PLAYER_DETAIL:
        if (state->selection == 0U) {
            begin_pvp_battle(state);
        } else if (state->selection == 1U) {
            return send_preset_mail(state, LORD_SCREEN_PLAYER_DETAIL);
        } else if (state->selection == 2U) {
            set_screen(state, LORD_SCREEN_ROMANCE_ACTION);
        } else {
            set_screen(state, LORD_SCREEN_PLAYERS);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_MAILBOX:
        if (state->selection < state->mail_count) {
            state->selected_mail = state->selection;
            state->mail[state->selected_mail].unread = false;
            mark_dirty(state);
            set_screen(state, LORD_SCREEN_MAIL_VIEW);
        } else if (state->selection == state->mail_count) {
            set_screen(state, LORD_SCREEN_MAIL_COMPOSE);
        } else {
            set_screen(state, LORD_SCREEN_TOWN);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_MAIL_VIEW:
        set_screen(state, LORD_SCREEN_MAILBOX);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_MAIL_COMPOSE:
        if (state->selection >= LORD_REALM_PLAYER_COUNT) {
            set_screen(state, LORD_SCREEN_MAILBOX);
        } else {
            state->selected_player = state->selection;
            return send_preset_mail(state, LORD_SCREEN_MAILBOX);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_ROMANCE:
        if (state->selection >= LORD_REALM_PLAYER_COUNT) {
            set_screen(state, LORD_SCREEN_TOWN);
        } else {
            state->selected_player = state->selection;
            set_screen(state, LORD_SCREEN_ROMANCE_ACTION);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_ROMANCE_ACTION:
        return perform_romance_action(state);
    case LORD_SCREEN_IGM:
        return run_igm(state);
    case LORD_SCREEN_RIP_GALLERY:
        if (state->selection >= LORD_RIP_SCENE_COUNT) {
            set_screen(state, LORD_SCREEN_TOWN);
        } else {
            state->rip_scene = state->selection;
            set_screen(state, LORD_SCREEN_RIP_SCENE);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_RIP_SCENE:
        set_screen(state, LORD_SCREEN_RIP_GALLERY);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_STATS:
        set_screen(state, state->return_screen);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_BATTLE:
        if (state->selection == 0U) {
            return battle_round(state, 1, "You hit ");
        }
        if (state->selection == 1U) {
            if (state->player.skill_uses == 0U) {
                text_copy(state->battle_line, sizeof(state->battle_line),
                          "No class skills remain today.");
                return LORD_EVENT_CONFIRM;
            }
            --state->player.skill_uses;
            mark_dirty(state);
            if (state->player.hero_class == LORD_CLASS_DEATH_KNIGHT) {
                return battle_round(state, 2, "Death Knight strike: ");
            }
            if (state->player.hero_class == LORD_CLASS_MYSTICAL) {
                const int32_t missing = state->player.max_hit_points -
                    state->player.hit_points;
                int32_t healing = state->player.max_hit_points / 3;
                if (healing > missing) {
                    healing = missing;
                }
                state->player.hit_points += healing;
                return battle_round(state, 1, "Mystic attack: ");
            }
            const uint32_t stolen = state->enemy.gold / 4U + 1U;
            const uint32_t actual = stolen > state->enemy.gold ?
                state->enemy.gold : stolen;
            state->enemy.gold -= actual;
            state->player.gold = add_u32_saturating(
                state->player.gold, actual);
            return battle_round(state, 1, "Thief attack: ");
        }
        if (state->selection == 2U) {
            if (state->battle_kind != LORD_BATTLE_FOREST) {
                text_copy(state->battle_line, sizeof(state->battle_line),
                          "There is no escape from this duel.");
            } else if (random_below(state, 2U) == 0U) {
                state->battle_kind = LORD_BATTLE_NONE;
                mark_dirty(state);
                set_message(state, LORD_SCREEN_FOREST,
                            "You escape into the undergrowth.", "");
            } else {
                const int32_t received = enemy_damage(state);
                state->player.hit_points -= received;
                mark_dirty(state);
                text_copy(state->battle_line, sizeof(state->battle_line),
                          "Escape fails; the enemy strikes.");
                if (state->player.hit_points <= 0) {
                    state->player.hit_points = 0;
                    state->battle_kind = LORD_BATTLE_NONE;
                    mark_dirty(state);
                    set_screen(state, LORD_SCREEN_DEAD);
                    return LORD_EVENT_LOSE;
                }
            }
            return LORD_EVENT_CONFIRM;
        }
        state->return_screen = LORD_SCREEN_BATTLE;
        set_screen(state, LORD_SCREEN_STATS);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_MESSAGE:
        set_screen(state, state->return_screen);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_DEAD:
        state->player.gold = 0U;
        state->player.experience -= state->player.experience / 10U;
        state->player.hit_points = state->player.max_hit_points;
        state->player.forest_fights = 0U;
        mark_dirty(state);
        set_message(state, LORD_SCREEN_INN,
                    "You awaken at the inn, penniless.",
                    "Rest before returning to the forest.");
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_DRAGON_VICTORY: {
        const lord_class_t hero_class = state->player.hero_class;
        initialize_player(state, hero_class);
        set_message(state, LORD_SCREEN_TOWN,
                    "A new legend begins.",
                    "Your dragon deed grants lasting power.");
        return LORD_EVENT_CONFIRM;
    }
    }
    return LORD_EVENT_NONE;
}

lord_event_t lord_cancel(lord_state_t *state)
{
    switch (state->screen) {
    case LORD_SCREEN_CLASS:
        set_screen(state, LORD_SCREEN_TITLE);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_FOREST:
    case LORD_SCREEN_WEAPON_SHOP:
    case LORD_SCREEN_ARMOR_SHOP:
    case LORD_SCREEN_HEALER:
    case LORD_SCREEN_TRAINING:
    case LORD_SCREEN_BANK:
    case LORD_SCREEN_INN:
    case LORD_SCREEN_PLAYERS:
    case LORD_SCREEN_MAILBOX:
    case LORD_SCREEN_ROMANCE:
    case LORD_SCREEN_IGM:
    case LORD_SCREEN_RIP_GALLERY:
        set_screen(state, LORD_SCREEN_TOWN);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_PLAYER_DETAIL:
        set_screen(state, LORD_SCREEN_PLAYERS);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_MAIL_VIEW:
    case LORD_SCREEN_MAIL_COMPOSE:
        set_screen(state, LORD_SCREEN_MAILBOX);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_ROMANCE_ACTION:
        set_screen(state, LORD_SCREEN_ROMANCE);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_RIP_SCENE:
        set_screen(state, LORD_SCREEN_RIP_GALLERY);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_STATS:
    case LORD_SCREEN_MESSAGE:
        set_screen(state, state->return_screen);
        return LORD_EVENT_CONFIRM;
    default:
        return LORD_EVENT_NONE;
    }
}

const char *lord_class_name(lord_class_t hero_class)
{
    switch (hero_class) {
    case LORD_CLASS_DEATH_KNIGHT:
        return "Death Knight";
    case LORD_CLASS_MYSTICAL:
        return "Mystical Skills";
    case LORD_CLASS_THIEF:
        return "Thieving Skills";
    }
    return "Unknown";
}

const lord_item_t *lord_weapon(size_t index)
{
    return index < LORD_WEAPON_COUNT ? &s_weapons[index] : NULL;
}

const lord_item_t *lord_armor(size_t index)
{
    return index < LORD_ARMOR_COUNT ? &s_armor[index] : NULL;
}

const lord_trainer_t *lord_current_trainer(const lord_state_t *state)
{
    if (state->player.level == 0U ||
        state->player.level >= LORD_MAX_LEVEL) {
        return NULL;
    }
    return &s_trainers[state->player.level - 1U];
}

static const lord_mail_t *mail_at(const lord_state_t *state, size_t index)
{
    return index < state->mail_count && index < LORD_MAIL_COUNT_MAX ?
        &state->mail[index] : NULL;
}

const char *lord_mail_sender(const lord_state_t *state, size_t index)
{
    const lord_mail_t *const mail = mail_at(state, index);
    if (mail == NULL) {
        return "Unknown";
    }
    if (mail->sender < LORD_REALM_PLAYER_COUNT) {
        return state->realm[mail->sender].name;
    }
    if (mail->sender == LORD_REALM_PLAYER_COUNT) {
        return "Seth Able";
    }
    return "Turgon";
}

const char *lord_mail_subject(const lord_state_t *state, size_t index)
{
    const lord_mail_t *const mail = mail_at(state, index);
    if (mail == NULL) {
        return "No message";
    }
    switch (mail->kind) {
    case LORD_MAIL_WELCOME: return "Welcome, Warrior";
    case LORD_MAIL_TRAINER: return "Training Advice";
    case LORD_MAIL_REPLY: return "Re: Town Square";
    case LORD_MAIL_PVP_VICTORY: return "A Worthy Duel";
    case LORD_MAIL_ROMANCE: return "About Your Proposal";
    case LORD_MAIL_PROPOSAL: return "Our Wedding";
    }
    return "Message";
}

const char *lord_mail_body_1(const lord_state_t *state, size_t index)
{
    const lord_mail_t *const mail = mail_at(state, index);
    if (mail == NULL) {
        return "This message has vanished.";
    }
    switch (mail->kind) {
    case LORD_MAIL_WELCOME:
        return "Welcome to LORD. Seek fame, fortune,";
    case LORD_MAIL_TRAINER:
        return "Earn experience, then challenge each";
    case LORD_MAIL_REPLY:
        return "I received your letter. Meet me in";
    case LORD_MAIL_PVP_VICTORY:
        return "You fought with honor. I will be";
    case LORD_MAIL_ROMANCE:
        return "My heart is not ready for a vow.";
    case LORD_MAIL_PROPOSAL:
        return "Our names are joined in the realm.";
    }
    return "The parchment is difficult to read.";
}

const char *lord_mail_body_2(const lord_state_t *state, size_t index)
{
    const lord_mail_t *const mail = mail_at(state, index);
    if (mail == NULL) {
        return "";
    }
    switch (mail->kind) {
    case LORD_MAIL_WELCOME:
        return "and the Red Dragon beyond the forest.";
    case LORD_MAIL_TRAINER:
        return "master before seeking the next level.";
    case LORD_MAIL_REPLY:
        return "the town square when the moon rises.";
    case LORD_MAIL_PVP_VICTORY:
        return "ready for another duel tomorrow.";
    case LORD_MAIL_ROMANCE:
        return "Kind words and gifts may change that.";
    case LORD_MAIL_PROPOSAL:
        return "Come home safely from every battle.";
    }
    return "";
}

size_t lord_mail_unread_count(const lord_state_t *state)
{
    size_t unread = 0U;
    for (size_t index = 0U;
         index < state->mail_count && index < LORD_MAIL_COUNT_MAX;
         ++index) {
        if (state->mail[index].unread) {
            ++unread;
        }
    }
    return unread;
}

const char *lord_rip_scene_name(size_t index)
{
    return index < sizeof(s_rip_scene_names) /
        sizeof(s_rip_scene_names[0]) ? s_rip_scene_names[index] : "Unknown";
}
