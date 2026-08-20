// SPDX-License-Identifier: LicenseRef-LORD-Permission

#include "lord_internal.h"

#include <limits.h>
#include <string.h>

enum {
    LORD_WEAPON_COUNT = 16,
    LORD_ARMOR_COUNT = 16,
    LORD_MAIL_SENDER_SETH = LORD_REALM_PLAYER_COUNT,
    LORD_MAIL_SENDER_TURGON = LORD_REALM_PLAYER_COUNT + 1,
    LORD_MAIL_SENDER_HERO = LORD_REALM_PLAYER_COUNT + 2,
};

static const lord_item_t s_weapons[LORD_WEAPON_COUNT] = {
    {"Fists", 0U, 0}, {"Stick", 200U, 5},
    {"Dagger", 1000U, 10}, {"Short Sword", 3000U, 20},
    {"Long Sword", 10000U, 30}, {"Huge Axe", 30000U, 40},
    {"Bone Cruncher", 100000U, 60}, {"Twin Swords", 150000U, 80},
    {"Power Axe", 200000U, 120}, {"Able's Sword", 400000U, 180},
    {"Wans' Weapon", 1000000U, 250},
    {"Spear of Gold", 4000000U, 350},
    {"Crystal Shard", 10000000U, 500},
    {"Nira's Teeth", 40000000U, 800},
    {"Blood Sword", 100000000U, 1200},
    {"Death Sword", 400000000U, 1800},
};

static const lord_item_t s_armor[LORD_ARMOR_COUNT] = {
    {"Nothing", 0U, 0}, {"Coat", 200U, 1},
    {"Heavy Coat", 1000U, 3}, {"Leather Vest", 3000U, 10},
    {"Bronze Armour", 10000U, 15}, {"Iron Armour", 30000U, 25},
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

#include "generated/lord_monsters.h"

typedef struct {
    const char *name;
    const char *saying;
    lord_sex_t sex;
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
        {"Sir Galahad", "Honor before gold.", LORD_SEX_MALE,
         LORD_CLASS_DEATH_KNIGHT, 1U, 24, 11, 2, 320U, 25U},
        {"Moonwitch", "The stars remember.", LORD_SEX_FEMALE,
         LORD_CLASS_MYSTICAL, 2U, 38, 19, 5, 850U, 130U},
        {"Shadow Jack", "Your purse looks heavy.", LORD_SEX_MALE,
         LORD_CLASS_THIEF, 3U, 58, 30, 8, 1800U, 475U},
        {"Lady Celes", "No dragon frightens me.", LORD_SEX_FEMALE,
         LORD_CLASS_MYSTICAL, 4U, 92, 46, 13, 4200U, 1200U},
        {"Iron Rose", "Steel blooms in battle.", LORD_SEX_FEMALE,
         LORD_CLASS_DEATH_KNIGHT, 5U, 145, 72, 22, 9000U, 3900U},
        {"Nightblade", "You never saw me.", LORD_SEX_MALE,
         LORD_CLASS_THIEF, 6U, 230, 108, 34, 18000U, 9800U},
        {"Aria Dawn", "Magic favors the bold.", LORD_SEX_FEMALE,
         LORD_CLASS_MYSTICAL, 7U, 360, 165, 52, 43000U, 27000U},
        {"Dread Rowan", "Meet me in the arena.", LORD_SEX_MALE,
         LORD_CLASS_DEATH_KNIGHT, 8U, 590, 255, 79, 92000U, 71000U},
    };

static const char *const s_rip_scene_names[LORD_RIP_SCENE_COUNT] = {
    "Town Square", "Dark Forest", "Red Dragon Inn", "Battle Arena",
    "Red Dragon", "King Arthur's", "Abdul's Armour", "Healer's Hut",
    "Turgon's Gym", "First Bank", "Graveyard", "DarkCloak Tavern",
};

static const char *const s_igm_names[LORD_IGM_COUNT] = {
    "Aragorn's Math", "Barak's House", "The Grab Bag",
    "The Graveyard", "Olodrin's Orphans", "The Outhouse",
    "The Pickle Goddess",
};

static const char s_keyboard_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .!?-'";

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
    return upper_bound == 0U ? 0U : random_next(state) % upper_bound;
}

static uint32_t add_u32_saturating(uint32_t left, uint32_t right)
{
    return UINT32_MAX - left < right ? UINT32_MAX : left + right;
}

static uint16_t add_u16_saturating(uint16_t left, uint16_t right)
{
    return UINT16_MAX - left < right ? UINT16_MAX :
        (uint16_t)(left + right);
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

static void add_log(lord_state_t *state, const char *text)
{
    if (state->log_count >= LORD_LOG_COUNT_MAX) {
        for (size_t index = 1U; index < LORD_LOG_COUNT_MAX; ++index) {
            state->log[index - 1U].day = state->log[index].day;
            text_copy(state->log[index - 1U].text,
                      sizeof(state->log[index - 1U].text),
                      state->log[index].text);
        }
        state->log_count = LORD_LOG_COUNT_MAX - 1U;
    }
    lord_log_entry_t *const entry = &state->log[state->log_count];
    entry->day = state->player.day == 0U ? 1U : state->player.day;
    text_copy(entry->text, sizeof(entry->text), text);
    ++state->log_count;
    mark_dirty(state);
}

static void add_named_log(lord_state_t *state, const char *prefix,
                          const char *suffix)
{
    char line[LORD_LOG_TEXT_BYTES];
    line[0] = '\0';
    text_append(line, sizeof(line), prefix);
    text_append(line, sizeof(line), state->player.name);
    text_append(line, sizeof(line), suffix);
    add_log(state, line);
}

static void add_mail(lord_state_t *state, uint8_t sender,
                     uint8_t recipient, lord_mail_kind_t kind,
                     bool outgoing, const char *body)
{
    if (state->mail_count >= LORD_MAIL_COUNT_MAX) {
        for (size_t index = 1U; index < LORD_MAIL_COUNT_MAX; ++index) {
            lord_mail_t *const destination = &state->mail[index - 1U];
            const lord_mail_t *const source = &state->mail[index];
            destination->sender = source->sender;
            destination->recipient = source->recipient;
            destination->kind = source->kind;
            destination->unread = source->unread;
            destination->outgoing = source->outgoing;
            text_copy(destination->body, sizeof(destination->body),
                      source->body);
        }
        state->mail_count = LORD_MAIL_COUNT_MAX - 1U;
    }
    lord_mail_t *const mail = &state->mail[state->mail_count];
    *mail = (lord_mail_t){
        .sender = sender,
        .recipient = recipient,
        .kind = kind,
        .unread = !outgoing,
        .outgoing = outgoing,
    };
    text_copy(mail->body, sizeof(mail->body), body);
    ++state->mail_count;
    if (state->realm_revision != UINT32_MAX) {
        ++state->realm_revision;
    }
    mark_dirty(state);
}

static void initialize_realm(lord_state_t *state)
{
    state->spouse_index = -1;
    state->npc_spouse = -1;
    state->pvp_fights = LORD_PVP_FIGHTS_PER_DAY;
    state->romance_actions = LORD_ROMANCE_ACTIONS_PER_DAY;
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        const lord_realm_template_t *const source =
            &s_realm_templates[index];
        lord_realm_player_t *const target = &state->realm[index];
        text_copy(target->name, sizeof(target->name), source->name);
        text_copy(target->saying, sizeof(target->saying), source->saying);
        target->sex = source->sex;
        target->hero_class = source->hero_class;
        target->level = source->level;
        target->alive = true;
        target->at_inn = index % 3U == 0U;
        target->married = false;
        target->affection = 0U;
        target->hit_points = source->hit_points;
        target->max_hit_points = source->hit_points;
        target->strength = source->strength;
        target->defense = source->defense;
        target->gold = source->gold;
        target->experience = source->experience;
    }
    state->mail_count = 0U;
    add_mail(state, LORD_MAIL_SENDER_SETH, LORD_MAIL_SENDER_HERO,
             LORD_MAIL_WELCOME, false,
             "Welcome. Fame, fortune and danger wait in LORD.");
    add_mail(state, LORD_MAIL_SENDER_TURGON, LORD_MAIL_SENDER_HERO,
             LORD_MAIL_TRAINER, false,
             "Earn experience, then challenge each master in order.");
    state->realm_revision = 1U;
}

static uint8_t daily_skill_uses(uint8_t mastery)
{
    if (mastery == 0U) {
        return 0U;
    }
    const uint8_t uses = (uint8_t)(mastery / 5U + 1U);
    return uses > 9U ? 9U : uses;
}

static void maybe_have_baby(lord_state_t *state)
{
    if ((state->spouse_index < 0 && state->npc_spouse < 0) ||
        random_below(state, 5U) != 0U) {
        return;
    }
    state->player.children = add_u16_saturating(
        state->player.children, 1U);
    add_mail(state, LORD_MAIL_SENDER_SETH, LORD_MAIL_SENDER_HERO,
             LORD_MAIL_BABY, false,
             "A child has joined your household. Congratulations!");
    add_named_log(state, "", " welcomed a child into the realm.");
}

static void reset_new_day(lord_state_t *state)
{
    state->player.hit_points = state->player.max_hit_points;
    state->player.forest_fights = LORD_FOREST_FIGHTS_PER_DAY;
    if (state->player.horse) {
        state->player.forest_fights = add_u16_saturating(
            state->player.forest_fights, 3U);
    }
    for (size_t index = 0U; index < LORD_SKILL_COUNT; ++index) {
        state->player.skill_uses[index] =
            daily_skill_uses(state->player.skill[index]);
    }
    state->pvp_fights = LORD_PVP_FIGHTS_PER_DAY;
    state->romance_actions = LORD_ROMANCE_ACTIONS_PER_DAY;
    state->igm_used_mask = 0U;
    state->player.seen_dragon = false;
    state->blackjack_active = false;
    state->player.bank = add_u32_saturating(
        state->player.bank, state->player.bank / 10U);
    if (state->spouse_index >= 0 || state->npc_spouse >= 0) {
        state->player.bank = add_u32_saturating(
            state->player.bank, state->player.bank / 20U);
    }
    if (state->player.day != UINT16_MAX) {
        ++state->player.day;
    }
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        state->realm[index].alive = true;
        state->realm[index].hit_points = state->realm[index].max_hit_points;
        state->realm[index].at_inn =
            ((index + (size_t)state->player.day) % 3U) == 0U;
    }
    maybe_have_baby(state);
    add_log(state, "A new day dawned over the realm.");
    mark_dirty(state);
}

static void initialize_player(lord_state_t *state,
                              lord_class_t hero_class)
{
    char name[LORD_NAME_BYTES];
    text_copy(name, sizeof(name), state->player.name);
    const lord_sex_t sex = state->player.sex;
    const uint8_t former_kills = state->player.dragon_kills;
    const uint16_t former_day = state->player.day;
    const uint16_t former_pvp_wins = state->player.pvp_wins;
    const uint16_t former_pvp_losses = state->player.pvp_losses;
    state->player = (lord_player_t){
        .sex = sex,
        .hero_class = hero_class,
        .level = 1U,
        .hit_points = 20 + (int32_t)former_kills * 5,
        .max_hit_points = 20 + (int32_t)former_kills * 5,
        .strength = 10 + (int32_t)former_kills * 2,
        .defense = 1 + (int32_t)former_kills,
        .gold = 500U,
        .forest_fights = LORD_FOREST_FIGHTS_PER_DAY,
        .dragon_kills = former_kills,
        .day = former_day == 0U ? 1U : former_day,
        .pvp_wins = former_pvp_wins,
        .pvp_losses = former_pvp_losses,
        .charm = 10U,
    };
    text_copy(state->player.name, sizeof(state->player.name), name);
    state->player.skill[(size_t)hero_class] = 5U;
    state->player.skill_uses[(size_t)hero_class] = 3U;
    add_named_log(state, "", " entered the realm.");
    mark_dirty(state);
}

static void enemy_from_monster(lord_state_t *state,
                               const lord_monster_t *monster)
{
    text_copy(state->enemy.name, sizeof(state->enemy.name), monster->name);
    text_copy(state->enemy.weapon, sizeof(state->enemy.weapon),
              monster->weapon);
    state->enemy.death_text = monster->death;
    state->enemy.hit_points = monster->hit_points;
    state->enemy.max_hit_points = monster->hit_points;
    state->enemy.strength = monster->strength;
    state->enemy.defense = 0;
    state->enemy.gold = monster->gold;
    state->enemy.experience = monster->experience;
}

static void begin_forest_battle(lord_state_t *state)
{
    const size_t tier = (size_t)(state->player.level - 1U);
    const size_t first = tier * 11U;
    size_t count = 11U;
    if (first + count > LORD_MONSTER_COUNT) {
        count = LORD_MONSTER_COUNT - first;
    }
    const size_t monster_index = first +
        (size_t)random_below(state, (uint32_t)count);
    enemy_from_monster(state, &s_monsters[monster_index]);
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
    text_copy(state->enemy.weapon, sizeof(state->enemy.weapon),
              "Master's weapon");
    state->enemy.death_text = "Your master yields the lesson.";
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
    text_copy(state->enemy.name, sizeof(state->enemy.name), "The Red Dragon");
    text_copy(state->enemy.weapon, sizeof(state->enemy.weapon), "Dragon Fire");
    state->enemy.death_text = "The Red Dragon crashes to the earth!";
    state->enemy.hit_points = 15000;
    state->enemy.max_hit_points = 15000;
    state->enemy.strength = 2000;
    state->enemy.defense = 200;
    state->enemy.gold = 1000U;
    state->enemy.experience = 1000U;
    state->player.seen_dragon = true;
    state->battle_kind = LORD_BATTLE_DRAGON;
    text_copy(state->battle_line, sizeof(state->battle_line),
              "Flame fills the horizon!");
    mark_dirty(state);
    set_screen(state, LORD_SCREEN_BATTLE);
}

static void begin_pvp_battle(lord_state_t *state, bool in_inn)
{
    if (state->selected_player >= LORD_REALM_PLAYER_COUNT) {
        set_message(state, LORD_SCREEN_PLAYERS,
                    "That warrior has left the realm.", "");
        return;
    }
    lord_realm_player_t *const opponent = &state->realm[state->selected_player];
    if (state->pvp_fights == 0U) {
        set_message(state, LORD_SCREEN_PLAYER_DETAIL,
                    "No player fights remain today.",
                    "Sleep at the inn before dueling again.");
        return;
    }
    if (!opponent->alive || (in_inn && !opponent->at_inn)) {
        set_message(state, in_inn ? LORD_SCREEN_INN : LORD_SCREEN_PLAYER_DETAIL,
                    in_inn ? "No sleeping warrior is in that room." :
                             "That warrior is already defeated.", "");
        return;
    }
    --state->pvp_fights;
    text_copy(state->enemy.name, sizeof(state->enemy.name), opponent->name);
    text_copy(state->enemy.weapon, sizeof(state->enemy.weapon), "Arena weapon");
    state->enemy.death_text = "The defeated warrior falls.";
    state->enemy.hit_points = opponent->hit_points;
    state->enemy.max_hit_points = opponent->max_hit_points;
    state->enemy.strength = opponent->strength;
    state->enemy.defense = opponent->defense;
    state->enemy.gold = opponent->gold;
    state->enemy.experience = opponent->experience;
    state->battle_kind = in_inn ? LORD_BATTLE_INN : LORD_BATTLE_PVP;
    text_copy(state->battle_line, sizeof(state->battle_line),
              in_inn ? "The innkeeper takes 100 gold and looks away." :
                       "The town gathers around the duel.");
    mark_dirty(state);
    set_screen(state, LORD_SCREEN_BATTLE);
}

static int32_t player_damage(lord_state_t *state, int32_t multiplier)
{
    const int32_t half = state->player.strength / 2;
    int32_t damage = half +
        (int32_t)random_below(state, (uint32_t)(half + 1));
    const uint32_t critical_range = state->player.amulet ? 7U : 10U;
    if (random_below(state, critical_range) == 0U) {
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
    if (state->player.skill[LORD_CLASS_THIEF] > 0U &&
        random_below(state, 8U) <
            (uint32_t)(state->player.skill[LORD_CLASS_THIEF] / 10U)) {
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
    text_append(state->battle_line, sizeof(state->battle_line),
                received == 0 ? "; you evade the reply." : "; foe hits ");
    if (received != 0) {
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
        char line[LORD_TEXT_BYTES];
        line[0] = '\0';
        text_append(line, sizeof(line), "Victory: ");
        text_append_u32(line, sizeof(line), state->enemy.gold);
        text_append(line, sizeof(line), " gold, ");
        text_append_u32(line, sizeof(line), state->enemy.experience);
        text_append(line, sizeof(line), " experience.");
        set_message(state, LORD_SCREEN_FOREST, line,
                    state->enemy.death_text == NULL ? "Enemy defeated." :
                                                     state->enemy.death_text);
        state->battle_kind = LORD_BATTLE_NONE;
        mark_dirty(state);
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
            add_named_log(state, "", " rose to a new level.");
            set_message(state, LORD_SCREEN_TOWN,
                        "Your master yields. Level gained!",
                        "The realm grows more dangerous.");
            mark_dirty(state);
        }
        state->battle_kind = LORD_BATTLE_NONE;
        return LORD_EVENT_WIN;
    }
    if (state->battle_kind == LORD_BATTLE_PVP ||
        state->battle_kind == LORD_BATTLE_INN) {
        lord_realm_player_t *const opponent = &state->realm[state->selected_player];
        const uint32_t prize = opponent->gold / 2U;
        opponent->gold -= prize;
        opponent->hit_points = 0;
        opponent->alive = false;
        opponent->at_inn = false;
        ++opponent->pvp_losses;
        state->player.gold = add_u32_saturating(state->player.gold, prize);
        state->player.experience = add_u32_saturating(
            state->player.experience, opponent->experience / 4U + 1U);
        if (state->player.pvp_wins != UINT16_MAX) {
            ++state->player.pvp_wins;
        }
        add_mail(state, state->selected_player, LORD_MAIL_SENDER_HERO,
                 LORD_MAIL_PVP_VICTORY, false,
                 "You fought with honor. I will be ready tomorrow.");
        add_named_log(state, "", " won a player fight.");
        state->battle_kind = LORD_BATTLE_NONE;
        set_message(state, LORD_SCREEN_PLAYERS,
                    "You win the player fight!",
                    "Half the opponent's carried gold is yours.");
        return LORD_EVENT_WIN;
    }
    state->battle_kind = LORD_BATTLE_NONE;
    ++state->player.dragon_kills;
    add_named_log(state, "", " slew the Red Dragon!");
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
        if (state->battle_kind == LORD_BATTLE_PVP ||
            state->battle_kind == LORD_BATTLE_INN) {
            lord_realm_player_t *const opponent =
                &state->realm[state->selected_player];
            opponent->hit_points = state->enemy.hit_points;
            ++opponent->pvp_wins;
            if (state->player.pvp_losses != UINT16_MAX) {
                ++state->player.pvp_losses;
            }
        }
        state->battle_kind = LORD_BATTLE_NONE;
        add_named_log(state, "", " was slain in battle.");
        set_screen(state, LORD_SCREEN_DEAD);
        return LORD_EVENT_LOSE;
    }
    return LORD_EVENT_HIT;
}

static lord_event_t use_battle_skill(lord_state_t *state, size_t skill)
{
    if (skill >= LORD_SKILL_COUNT || state->player.skill[skill] == 0U ||
        state->player.skill_uses[skill] == 0U) {
        text_copy(state->battle_line, sizeof(state->battle_line),
                  "That skill has no uses remaining today.");
        return LORD_EVENT_CONFIRM;
    }
    --state->player.skill_uses[skill];
    mark_dirty(state);
    if (skill == LORD_CLASS_DEATH_KNIGHT) {
        const int32_t multiplier = state->player.skill[skill] >= 20U ? 3 : 2;
        return battle_round(state, multiplier, "Death Knight strike: ");
    }
    if (skill == LORD_CLASS_MYSTICAL) {
        const int32_t missing = state->player.max_hit_points -
            state->player.hit_points;
        int32_t healing = state->player.max_hit_points / 3 +
            (int32_t)state->player.skill[skill];
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
    state->player.gold = add_u32_saturating(state->player.gold, actual);
    return battle_round(state, 1, "Thief attack: ");
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
        set_message(state, weapon_shop ? LORD_SCREEN_WEAPON_SHOP :
                                         LORD_SCREEN_ARMOR_SHOP,
                    "You already own that item.", "Choose another.");
        return LORD_EVENT_CONFIRM;
    }
    const uint32_t trade = items[current_index].price / 2U;
    const uint32_t funds = add_u32_saturating(state->player.gold, trade);
    if (funds < items[selected].price) {
        set_message(state, weapon_shop ? LORD_SCREEN_WEAPON_SHOP :
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
    mark_dirty(state);
    set_message(state, weapon_shop ? LORD_SCREEN_WEAPON_SHOP :
                                     LORD_SCREEN_ARMOR_SHOP,
                "The trade is made.", items[selected].name);
    return LORD_EVENT_CONFIRM;
}

static void begin_editor(lord_state_t *state, lord_editor_target_t target,
                         lord_screen_t return_screen, const char *initial)
{
    state->editor_target = target;
    state->editor_return_screen = return_screen;
    text_copy(state->editor_text, sizeof(state->editor_text), initial);
    set_screen(state, LORD_SCREEN_TEXT_EDITOR);
}

static lord_event_t finish_editor(lord_state_t *state)
{
    const size_t length = text_length(state->editor_text,
                                      sizeof(state->editor_text));
    if (state->editor_target == LORD_EDITOR_NAME) {
        if (length < 3U) {
            set_message(state, LORD_SCREEN_NAME,
                        "A warrior's name needs 3 letters.", "Try again.");
            return LORD_EVENT_CONFIRM;
        }
        text_copy(state->player.name, sizeof(state->player.name),
                  state->editor_text);
        set_screen(state, LORD_SCREEN_SEX);
        return LORD_EVENT_CONFIRM;
    }
    if (length == 0U) {
        set_message(state, state->editor_return_screen,
                    "Nothing was written.", "");
        return LORD_EVENT_CONFIRM;
    }
    if (state->editor_target == LORD_EDITOR_MAIL) {
        add_mail(state, LORD_MAIL_SENDER_HERO, state->selected_player,
                 LORD_MAIL_CUSTOM, true, state->editor_text);
        add_mail(state, state->selected_player, LORD_MAIL_SENDER_HERO,
                 LORD_MAIL_REPLY, false,
                 "Your letter reached me. We will speak again soon.");
        set_message(state, LORD_SCREEN_MAILBOX,
                    "Your sealed letter was delivered.",
                    "A reply is waiting in the post.");
    } else if (state->editor_target == LORD_EDITOR_ANNOUNCEMENT) {
        text_copy(state->announcement, sizeof(state->announcement),
                  state->editor_text);
        add_mail(state, LORD_MAIL_SENDER_HERO, LORD_MAIL_SENDER_HERO,
                 LORD_MAIL_ANNOUNCEMENT, true, state->editor_text);
        add_named_log(state, "", " made a public announcement.");
        set_message(state, LORD_SCREEN_INN,
                    "The bartender posts your announcement.",
                    state->announcement);
    } else if (state->editor_target == LORD_EDITOR_SAYING) {
        if (state->selected_player < LORD_REALM_PLAYER_COUNT) {
            text_copy(state->realm[state->selected_player].saying,
                      sizeof(state->realm[state->selected_player].saying),
                      state->editor_text);
            mark_dirty(state);
        }
        set_message(state, LORD_SCREEN_PLAYER_DETAIL,
                    "A new saying is entered in the record.", "");
    } else {
        text_copy(state->conversation, sizeof(state->conversation),
                  state->editor_text);
        add_named_log(state, "", " spoke in the tavern.");
        set_message(state, LORD_SCREEN_CONVERSE,
                    "Your words join the tavern conversation.",
                    state->conversation);
    }
    return LORD_EVENT_CONFIRM;
}

static lord_event_t activate_editor(lord_state_t *state)
{
    const size_t character_count = sizeof(s_keyboard_chars) - 1U;
    if (state->selection < character_count) {
        const size_t length = text_length(state->editor_text,
                                          sizeof(state->editor_text));
        if (length + 1U < sizeof(state->editor_text)) {
            state->editor_text[length] = s_keyboard_chars[state->selection];
            state->editor_text[length + 1U] = '\0';
        }
        return LORD_EVENT_CONFIRM;
    }
    if (state->selection == character_count) {
        return finish_editor(state);
    }
    const size_t length = text_length(state->editor_text,
                                      sizeof(state->editor_text));
    if (length > 0U) {
        state->editor_text[length - 1U] = '\0';
    }
    return LORD_EVENT_CONFIRM;
}

static void run_forest_event(lord_state_t *state)
{
    const uint32_t event = random_below(state, 15U);
    char line[LORD_TEXT_BYTES];
    line[0] = '\0';
    switch (event) {
    case 0U: {
        const uint32_t reward = (uint32_t)state->player.level * 500U;
        state->player.gold = add_u32_saturating(state->player.gold, reward);
        state->player.charm = add_u16_saturating(state->player.charm, 1U);
        if (state->player.forest_fights > 0U) {
            --state->player.forest_fights;
        }
        text_append(line, sizeof(line), "Old man rewards your kindness: ");
        text_append_u32(line, sizeof(line), reward);
        set_message(state, LORD_SCREEN_FOREST, line, "Charm rises by one.");
        break;
    }
    case 1U:
        if (state->player.gems > 0U) {
            --state->player.gems;
            if (state->player.hit_points == state->player.max_hit_points) {
                ++state->player.max_hit_points;
            }
            state->player.hit_points = state->player.max_hit_points;
            set_message(state, LORD_SCREEN_FOREST,
                        "The old hag accepts one gem.",
                        "Her spell completely restores you.");
        } else {
            state->player.hit_points = 1;
            set_message(state, LORD_SCREEN_FOREST,
                        "The hag sees your empty gem pouch.",
                        "Her cane leaves you at one hit point.");
        }
        break;
    case 2U: {
        const uint32_t reward = (random_below(state, 500U) + 250U) *
            (uint32_t)state->player.level * (uint32_t)state->player.level;
        state->player.gold = add_u32_saturating(state->player.gold, reward);
        text_append(line, sizeof(line), "A lost sack holds ");
        text_append_u32(line, sizeof(line), reward);
        text_append(line, sizeof(line), " gold.");
        set_message(state, LORD_SCREEN_FOREST, line, "Fortune smiles.");
        break;
    }
    case 3U:
        state->player.hit_points = state->player.max_hit_points;
        set_message(state, LORD_SCREEN_FOREST,
                    "Merry Men invite you to their feast.",
                    "You leave completely refreshed.");
        break;
    case 4U:
        state->player.gems = add_u16_saturating(state->player.gems, 1U);
        set_message(state, LORD_SCREEN_FOREST,
                    "A gem glitters beneath the leaves.",
                    "You add it to your pouch.");
        break;
    case 5U:
        if (random_below(state, 2U) == 0U) {
            state->player.hit_points = state->player.max_hit_points;
            state->player.forest_fights = add_u16_saturating(
                state->player.forest_fights, 1U);
            set_message(state, LORD_SCREEN_FOREST,
                        "You rest in a flower garden.",
                        "Full health and one extra forest fight!");
        } else {
            ++state->player.strength;
            set_message(state, LORD_SCREEN_FOREST,
                        "Your weapon breaks a Hammer Stone.",
                        "Attack strength rises by one.");
        }
        break;
    case 6U: {
        const size_t skill = (size_t)state->player.hero_class;
        if (state->player.skill[skill] < LORD_SKILL_MASTERY_MAX) {
            ++state->player.skill[skill];
        }
        if (state->player.skill_uses[skill] != UINT8_MAX) {
            ++state->player.skill_uses[skill];
        }
        set_message(state, LORD_SCREEN_FOREST,
                    "A hidden master tests your profession.",
                    "Skill mastery and one daily use increase.");
        break;
    }
    case 7U: {
        const uint16_t change = (uint16_t)(random_below(state, 2U) + 1U);
        if (random_below(state, 3U) == 1U) {
            state->player.charm = add_u16_saturating(
                state->player.charm, change);
            set_message(state, LORD_SCREEN_FOREST,
                        "An old man swings a pretty stick!",
                        "Your charm rises.");
        } else {
            state->player.charm = state->player.charm > change ?
                (uint16_t)(state->player.charm - change) : 0U;
            set_message(state, LORD_SCREEN_FOREST,
                        "An old man swings an ugly stick!",
                        "Your charm falls.");
        }
        break;
    }
    case 8U: {
        const uint32_t price = (uint32_t)state->player.level * 10000U;
        if (!state->player.horse && state->player.gold >= price) {
            state->player.gold -= price;
            state->player.horse = true;
            set_message(state, LORD_SCREEN_FOREST,
                        "The hairyfoot horse trader finds you.",
                        "You buy a horse for longer days.");
        } else if (state->player.horse) {
            set_message(state, LORD_SCREEN_FOREST,
                        "Your horse greets the forest herd.",
                        "The trader offers to buy it another day.");
        } else {
            set_message(state, LORD_SCREEN_FOREST,
                        "A horse trader names his price.",
                        "Your purse is too light.");
        }
        break;
    }
    case 9U:
        if (!state->player.fairy && random_below(state, 2U) == 0U) {
            state->player.fairy = true;
            set_message(state, LORD_SCREEN_FOREST,
                        "You catch a furious forest fairy!",
                        "A tiny companion now rides in your pouch.");
        } else {
            const uint32_t blessing = random_below(state, 3U);
            if (blessing == 0U) {
                state->player.hit_points = state->player.max_hit_points;
            } else if (blessing == 1U) {
                state->player.gems = add_u16_saturating(
                    state->player.gems, 2U);
            } else {
                state->player.fairy_lore = true;
                state->player.experience = add_u32_saturating(
                    state->player.experience,
                    10U * (uint32_t)state->player.level *
                        (uint32_t)state->player.level);
            }
            set_message(state, LORD_SCREEN_FOREST,
                        "The fairies grant a strange blessing.",
                        "Health, gems, or lore are yours.");
        }
        break;
    case 10U:
        state->player.high_spirits = true;
        state->player.charm = add_u16_saturating(state->player.charm, 1U);
        set_message(state, LORD_SCREEN_FOREST,
                    "You comfort the mysterious Olivia.",
                    "Kindness lifts your spirits and charm.");
        break;
    case 11U:
        state->player.charm = add_u16_saturating(state->player.charm, 2U);
        state->player.gold = add_u32_saturating(
            state->player.gold, (uint32_t)state->player.level * 2000U);
        set_message(state, LORD_SCREEN_FOREST,
                    "You rescue a lost princess from danger.",
                    "Royal gold and two charm are awarded.");
        break;
    case 12U: {
        const uint32_t found = (uint32_t)state->player.level * 750U;
        state->player.gold = add_u32_saturating(state->player.gold, found);
        text_append(line, sizeof(line), "You recover ");
        text_append_u32(line, sizeof(line), found);
        text_append(line, sizeof(line), " lost forest gold.");
        set_message(state, LORD_SCREEN_FOREST, line, "");
        break;
    }
    case 13U: {
        const int32_t damage = state->player.hero_class == LORD_CLASS_THIEF ?
            0 : (int32_t)random_below(state, 10U) + 1;
        state->player.hit_points -= damage;
        if (state->player.hit_points <= 0) {
            state->player.hit_points = 0;
            state->player.gold = 0U;
            state->player.gems = 0U;
            add_named_log(state, "", " was killed by a forest troll.");
            set_screen(state, LORD_SCREEN_DEAD);
        } else {
            set_message(state, LORD_SCREEN_FOREST,
                        "A troll lunges for your coin purse!",
                        damage == 0 ? "Your thieving reflexes foil him." :
                                      "You drive him off, wounded.");
        }
        break;
    }
    default:
        if (random_below(state, 2U) == 0U) {
            state->player.gold = add_u32_saturating(
                state->player.gold, (uint32_t)state->player.level * 500U);
            set_message(state, LORD_SCREEN_FOREST,
                        "DarkCloak Tavern appears in the gloom.",
                        "A lucky wager wins forest gold.");
        } else {
            set_message(state, LORD_SCREEN_FOREST,
                        "You warm yourself at DarkCloak Tavern.",
                        "The old man's gamble favors the house.");
        }
        break;
    }
    mark_dirty(state);
}

static lord_event_t perform_romance_action(lord_state_t *state)
{
    if (state->selected_player >= LORD_REALM_PLAYER_COUNT) {
        set_screen(state, LORD_SCREEN_PLAYERS);
        return LORD_EVENT_CONFIRM;
    }
    lord_realm_player_t *const person = &state->realm[state->selected_player];
    if (state->selection == 4U) {
        set_screen(state, LORD_SCREEN_PLAYER_DETAIL);
        return LORD_EVENT_CONFIRM;
    }
    if (state->romance_actions == 0U) {
        set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                    "No courtship actions remain today.",
                    "Sleep at the inn and try tomorrow.");
        return LORD_EVENT_CONFIRM;
    }
    if (state->selection == 3U) {
        if (state->spouse_index != (int8_t)state->selected_player) {
            set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                        "Children require a committed household.", "");
            return LORD_EVENT_CONFIRM;
        }
        --state->romance_actions;
        if (random_below(state, 3U) == 0U) {
            state->player.children = add_u16_saturating(
                state->player.children, 1U);
            add_mail(state, state->selected_player, LORD_MAIL_SENDER_HERO,
                     LORD_MAIL_BABY, false,
                     "Our family has grown by one child!");
            set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                        "A child joins your household!",
                        "The realm celebrates with you.");
        } else {
            set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                        "You spend a quiet evening together.",
                        "Perhaps the family will grow another day.");
        }
        mark_dirty(state);
        return LORD_EVENT_CONFIRM;
    }
    if (state->selection == 2U) {
        --state->romance_actions;
        if (state->spouse_index == (int8_t)state->selected_player) {
            person->married = false;
            state->spouse_index = -1;
            set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                        "The marriage is ended.",
                        "The daily spouse bonus is gone.");
            add_named_log(state, "", " was divorced.");
            mark_dirty(state);
            return LORD_EVENT_CONFIRM;
        }
        if (state->spouse_index >= 0 || state->npc_spouse >= 0) {
            set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                        "You have already made a marriage vow.", "");
            return LORD_EVENT_CONFIRM;
        }
        if (person->affection < 60U || person->married) {
            add_mail(state, state->selected_player, LORD_MAIL_SENDER_HERO,
                     LORD_MAIL_ROMANCE, false,
                     "My heart is not ready. Kindness may change that.");
            set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                        "Your proposal is gently refused.",
                        "Build more affection first.");
            return LORD_EVENT_CONFIRM;
        }
        state->spouse_index = (int8_t)state->selected_player;
        person->married = true;
        state->player.max_hit_points += 5;
        state->player.hit_points += 5;
        add_mail(state, state->selected_player, LORD_MAIL_SENDER_HERO,
                 LORD_MAIL_PROPOSAL, false,
                 "Our names are joined in the realm.");
        add_named_log(state, "", " married another warrior.");
        set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                    "Wedding bells ring across the realm!",
                    "Marriage grants 5 HP and an inn bonus.");
        return LORD_EVENT_CONFIRM;
    }
    uint8_t gain = 15U;
    if (state->selection == 1U) {
        if (state->player.gold < 100U) {
            set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                        "A proper gift costs 100 gold.", "");
            return LORD_EVENT_CONFIRM;
        }
        state->player.gold -= 100U;
        gain = 30U;
    }
    --state->romance_actions;
    const uint16_t affection = (uint16_t)person->affection + gain;
    person->affection = affection > 100U ? 100U : (uint8_t)affection;
    state->player.charm = add_u16_saturating(state->player.charm, 1U);
    mark_dirty(state);
    set_message(state, LORD_SCREEN_ROMANCE_ACTION,
                state->selection == 0U ?
                    "Your compliment earns a warm smile." :
                    "The gift is accepted with delight.",
                "Affection and charm have grown.");
    return LORD_EVENT_CONFIRM;
}

static lord_event_t npc_romance(lord_state_t *state, int8_t npc)
{
    const lord_screen_t screen = npc == 0 ? LORD_SCREEN_SETH :
                                                LORD_SCREEN_VIOLET;
    const char *const name = npc == 0 ? "Seth Able" : "Violet";
    if (state->selection == 5U) {
        set_screen(state, LORD_SCREEN_INN);
        return LORD_EVENT_CONFIRM;
    }
    if (state->romance_actions == 0U) {
        set_message(state, screen, "No flirting remains today.",
                    "Try again after sleeping.");
        return LORD_EVENT_CONFIRM;
    }
    --state->romance_actions;
    if (state->selection == 0U) {
        set_message(state, screen,
                    npc == 0 ? "Seth shares a new verse with you." :
                               "Violet tells you about the bar.",
                    "The conversation is warm.");
    } else if (state->selection == 1U) {
        state->player.charm = add_u16_saturating(state->player.charm, 1U);
        set_message(state, screen, "Your flirt earns a delighted smile.",
                    "Charm rises by one.");
    } else if (state->selection == 2U) {
        if (state->player.charm >= 20U) {
            state->player.laid = add_u16_saturating(state->player.laid, 1U);
            state->player.high_spirits = true;
            add_named_log(state, "", npc == 0 ?
                          " spent the evening with Seth." :
                          " spent the evening with Violet.");
            set_message(state, screen, "A kiss becomes a private evening.",
                        "You leave in high spirits.");
        } else {
            set_message(state, screen, "Your kiss is politely refused.",
                        "More charm may help.");
        }
    } else if (state->selection == 3U) {
        if (state->player.gems == 0U) {
            set_message(state, screen, "A sparkling gift needs one gem.", "");
        } else {
            --state->player.gems;
            state->player.charm = add_u16_saturating(state->player.charm, 3U);
            set_message(state, screen, "The gem is accepted with delight.",
                        "Charm rises by three.");
        }
    } else if (state->npc_spouse == npc) {
        state->npc_spouse = -1;
        set_message(state, screen, "Your marriage is dissolved.", "");
        add_named_log(state, "", " was divorced.");
    } else if (state->spouse_index >= 0 || state->npc_spouse >= 0) {
        set_message(state, screen, "You have already made another vow.", "");
    } else if (state->player.charm < 30U) {
        set_message(state, screen, "The proposal is refused for now.",
                    "Reach 30 charm and ask again.");
    } else {
        state->npc_spouse = npc;
        state->player.max_hit_points += 5;
        state->player.hit_points += 5;
        char log_line[LORD_LOG_TEXT_BYTES];
        log_line[0] = '\0';
        text_append(log_line, sizeof(log_line), state->player.name);
        text_append(log_line, sizeof(log_line), " married ");
        text_append(log_line, sizeof(log_line), name);
        text_append(log_line, sizeof(log_line), "!");
        add_log(state, log_line);
        set_message(state, screen, "Wedding bells shake the inn!",
                    "Marriage grants 5 maximum HP.");
    }
    mark_dirty(state);
    return LORD_EVENT_CONFIRM;
}

static uint8_t deal_card(lord_state_t *state)
{
    return (uint8_t)(random_below(state, 10U) + 2U);
}

static void settle_blackjack(lord_state_t *state, bool player_wins,
                             bool push)
{
    if (push) {
        state->player.gold = add_u32_saturating(
            state->player.gold, state->blackjack_wager);
        text_copy(state->battle_line, sizeof(state->battle_line),
                  "Push. Your wager is returned.");
    } else if (player_wins) {
        const uint32_t payout = (uint32_t)state->blackjack_wager * 2U;
        state->player.gold = add_u32_saturating(state->player.gold, payout);
        text_copy(state->battle_line, sizeof(state->battle_line),
                  "You beat the dealer and collect double.");
    } else {
        text_copy(state->battle_line, sizeof(state->battle_line),
                  "The dealer takes your wager.");
    }
    state->blackjack_active = false;
    mark_dirty(state);
}

static lord_event_t blackjack_action(lord_state_t *state)
{
    if (state->selection == 3U) {
        state->blackjack_active = false;
        set_screen(state, LORD_SCREEN_INN);
        return LORD_EVENT_CONFIRM;
    }
    if (state->selection == 0U) {
        if (state->blackjack_active) {
            set_message(state, LORD_SCREEN_BLACKJACK,
                        "Finish the current hand first.", "");
            return LORD_EVENT_CONFIRM;
        }
        if (state->player.gold < 100U) {
            set_message(state, LORD_SCREEN_BLACKJACK,
                        "A blackjack hand costs 100 gold.", "");
            return LORD_EVENT_CONFIRM;
        }
        state->player.gold -= 100U;
        state->blackjack_wager = 100U;
        state->blackjack_player = (uint8_t)(deal_card(state) + deal_card(state));
        state->blackjack_dealer = (uint8_t)(deal_card(state) + deal_card(state));
        state->blackjack_active = true;
        text_copy(state->battle_line, sizeof(state->battle_line),
                  "Cards dealt. Hit or stand.");
        if (state->blackjack_player >= 21U) {
            settle_blackjack(state, state->blackjack_player == 21U,
                             state->blackjack_dealer == 21U);
        }
        mark_dirty(state);
        return LORD_EVENT_CONFIRM;
    }
    if (!state->blackjack_active) {
        set_message(state, LORD_SCREEN_BLACKJACK,
                    "Deal a new hand first.", "");
        return LORD_EVENT_CONFIRM;
    }
    if (state->selection == 1U) {
        state->blackjack_player = (uint8_t)(
            state->blackjack_player + deal_card(state));
        if (state->blackjack_player > 21U) {
            settle_blackjack(state, false, false);
        } else {
            text_copy(state->battle_line, sizeof(state->battle_line),
                      "Another card lands before you.");
        }
    } else {
        while (state->blackjack_dealer < 17U) {
            state->blackjack_dealer = (uint8_t)(
                state->blackjack_dealer + deal_card(state));
        }
        const bool dealer_bust = state->blackjack_dealer > 21U;
        settle_blackjack(state,
                         dealer_bust || state->blackjack_player >
                             state->blackjack_dealer,
                         state->blackjack_player == state->blackjack_dealer);
    }
    mark_dirty(state);
    return LORD_EVENT_CONFIRM;
}

static bool igm_already_used(lord_state_t *state)
{
    const uint8_t bit = (uint8_t)(1U << state->selected_igm);
    if ((state->igm_used_mask & bit) != 0U) {
        set_message(state, LORD_SCREEN_IGM_DETAIL,
                    "That place is closed to you today.",
                    "A new day resets every module.");
        return true;
    }
    return false;
}

static void mark_igm_used(lord_state_t *state)
{
    state->igm_used_mask = (uint8_t)(state->igm_used_mask |
        (uint8_t)(1U << state->selected_igm));
    mark_dirty(state);
}

static lord_event_t run_igm_action(lord_state_t *state)
{
    if (state->selection == 3U) {
        set_screen(state, LORD_SCREEN_IGM);
        return LORD_EVENT_CONFIRM;
    }
    if (igm_already_used(state)) {
        return LORD_EVENT_CONFIRM;
    }
    const uint8_t module = state->selected_igm;
    if (module == 0U) {
        if (state->selection == 2U) {
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "Aragorn gives twenty arithmetic questions.",
                        "This port resolves the bounded wager at once.");
            return LORD_EVENT_CONFIRM;
        }
        const uint32_t wager = state->selection == 0U ? 100U : 1000U;
        if (state->player.gold < wager) {
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "Aragorn refuses an unfunded wager.", "");
            return LORD_EVENT_CONFIRM;
        }
        state->player.gold -= wager;
        if (random_below(state, 100U) >= 45U) {
            state->player.gold = add_u32_saturating(
                state->player.gold, wager * 2U);
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "Your arithmetic defeats Aragorn!",
                        "The wager is doubled.");
        } else {
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "Aragorn catches an arithmetic error.",
                        "Your wager joins his gold piles.");
        }
    } else if (module == 1U) {
        if (state->selection == 0U) {
            const uint32_t haul = (uint32_t)state->player.level *
                (uint32_t)state->player.level * 100U;
            if (random_below(state, 3U) == 0U) {
                state->player.hit_points = 1;
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "Barak catches you in his house!",
                            "You escape with one hit point.");
            } else {
                state->player.gold = add_u32_saturating(
                    state->player.gold, haul);
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "You sneak through Barak's house.",
                            "A sack of gold makes it outside.");
            }
        } else if (state->selection == 1U) {
            state->player.gems = add_u16_saturating(state->player.gems, 1U);
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "Barak misunderstands 'sugar'.",
                        "His strange gift is one gem.");
        } else {
            const size_t skill = (size_t)state->player.hero_class;
            if (state->player.skill[skill] < LORD_SKILL_MASTERY_MAX) {
                ++state->player.skill[skill];
            }
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "Barak drills your chosen profession.",
                        "Skill mastery rises by one.");
        }
    } else if (module == 2U) {
        if (state->selection == 0U) {
            const uint32_t reward = (uint32_t)state->player.level * 1000U;
            if (random_below(state, 2U) == 0U) {
                state->player.experience = add_u32_saturating(
                    state->player.experience, reward);
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "You answer the Grab Bag trivia.",
                            "Experience is your prize.");
            } else {
                state->player.gold = add_u32_saturating(
                    state->player.gold, reward);
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "The Grab Bag host likes your answer.",
                            "A purse of gold is awarded.");
            }
        } else if (state->selection == 1U) {
            state->player.forest_fights = add_u16_saturating(
                state->player.forest_fights, 2U);
            state->player.hit_points = state->player.max_hit_points;
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "A cabin offers a perfect night's rest.",
                        "Full health and two forest fights.");
        } else {
            ++state->player.laid;
            state->player.experience = add_u32_saturating(
                state->player.experience, 1000U);
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "A private invitation arrives.",
                        "You gain 1000 experience.");
        }
    } else if (module == 3U) {
        if (state->selection == 0U) {
            if (random_below(state, 4U) == 0U) {
                state->player.charm = state->player.charm > 2U ?
                    (uint16_t)(state->player.charm - 2U) : 0U;
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "A grave guardian catches you.",
                            "Shame costs two charm.");
            } else {
                state->player.gold = add_u32_saturating(
                    state->player.gold,
                    (uint32_t)state->player.level * 1500U);
                state->player.gems = add_u16_saturating(
                    state->player.gems, 2U);
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "An old grave conceals treasure.",
                            "Gold and two gems fill your hands.");
            }
        } else if (state->selection == 1U) {
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "The epitaph warns: greed wakes the dead.",
                        "The stone is cold beneath your hand.");
            return LORD_EVENT_CONFIRM;
        } else {
            state->player.charm = add_u16_saturating(state->player.charm, 1U);
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "You honor the forgotten warriors.",
                        "Respect adds one charm.");
        }
    } else if (module == 4U) {
        const uint32_t price = (uint32_t)state->player.level *
            (uint32_t)state->player.level * 1000U;
        if (state->selection == 0U) {
            if (state->player.gold < price) {
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "Adoption costs level squared times 1000.", "");
                return LORD_EVENT_CONFIRM;
            }
            state->player.gold -= price;
            state->player.children = add_u16_saturating(
                state->player.children, 1U);
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "You adopt an orphan into your household.",
                        "Your family grows by one.");
        } else if (state->selection == 1U) {
            if (random_below(state, 2U) == 0U) {
                state->player.children = add_u16_saturating(
                    state->player.children, 1U);
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "You catch a runaway orphan safely.",
                            "Your household grows.");
            } else {
                const uint32_t loss = state->player.gold / 10U;
                state->player.gold -= loss;
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "The nimble orphan escapes.",
                            "Ten percent of your gold vanishes too.");
            }
        } else {
            const uint16_t needed = (uint16_t)(
                (uint16_t)state->player.level * state->player.level);
            if (state->player.horse || state->player.children < needed) {
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "The horse trade cannot be completed.",
                            "You need level squared children and no horse.");
                return LORD_EVENT_CONFIRM;
            }
            state->player.children = (uint16_t)(
                state->player.children - needed);
            state->player.horse = true;
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "Olodrin trades the children for a horse.",
                        "Your new mount extends every day.");
        }
    } else if (module == 5U) {
        if (state->selection == 2U) {
            begin_editor(state, LORD_EDITOR_CONVERSATION,
                         LORD_SCREEN_IGM_DETAIL, "");
            return LORD_EVENT_CONFIRM;
        }
        if (state->player.forest_fights == 0U) {
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "You are too tired to search the outhouse.", "");
            return LORD_EVENT_CONFIRM;
        }
        --state->player.forest_fights;
        if (random_below(state, 3U) == 0U) {
            state->player.gems = add_u16_saturating(
                state->player.gems, (uint16_t)state->player.level);
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "Something shiny glitters in the muck.",
                        "You recover a pouch of gems.");
        } else {
            state->player.gold = add_u32_saturating(
                state->player.gold,
                (uint32_t)state->player.level * 3500U);
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "A lost pouch waits behind the trees.",
                        "Its gold is now yours.");
        }
    } else {
        if (state->selection != 0U) {
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "The Pickle Goddess changes one stat by fate.",
                        "Good and bad pickles are equally possible.");
            return LORD_EVENT_CONFIRM;
        }
        const bool good = random_below(state, 2U) != 0U;
        const uint32_t stat = random_below(state, 8U);
        if (stat == 0U) {
            const int32_t delta = state->player.max_hit_points / 10 + 1;
            state->player.max_hit_points += good ? delta : -delta;
            if (state->player.max_hit_points < 1) {
                state->player.max_hit_points = 1;
            }
            if (state->player.hit_points > state->player.max_hit_points) {
                state->player.hit_points = state->player.max_hit_points;
            }
        } else if (stat == 1U) {
            const uint32_t delta = state->player.gold / 10U + 1U;
            state->player.gold = good ? add_u32_saturating(
                state->player.gold, delta) :
                (state->player.gold > delta ? state->player.gold - delta : 0U);
        } else if (stat == 2U) {
            const uint32_t delta = state->player.bank / 10U + 1U;
            state->player.bank = good ? add_u32_saturating(
                state->player.bank, delta) :
                (state->player.bank > delta ? state->player.bank - delta : 0U);
        } else if (stat == 3U) {
            state->player.strength += good ? 2 : -2;
            if (state->player.strength < 1) {
                state->player.strength = 1;
            }
        } else if (stat == 4U) {
            state->player.defense += good ? 1 : -1;
            if (state->player.defense < 0) {
                state->player.defense = 0;
            }
        } else if (stat == 5U) {
            state->player.charm = good ? add_u16_saturating(
                state->player.charm, 2U) :
                (state->player.charm > 2U ?
                    (uint16_t)(state->player.charm - 2U) : 0U);
        } else if (stat == 6U) {
            state->player.gems = good ? add_u16_saturating(
                state->player.gems, 2U) :
                (state->player.gems > 2U ?
                    (uint16_t)(state->player.gems - 2U) : 0U);
        } else {
            state->player.forest_fights = good ? add_u16_saturating(
                state->player.forest_fights, 2U) :
                (state->player.forest_fights > 2U ?
                    (uint16_t)(state->player.forest_fights - 2U) : 0U);
        }
        set_message(state, LORD_SCREEN_IGM_DETAIL,
                    good ? "The pickle is divine!" :
                           "The pickle tastes like doom!",
                    good ? "A random stat increases." :
                           "A random stat decreases.");
    }
    mark_igm_used(state);
    return LORD_EVENT_CONFIRM;
}

void lord_initialize(lord_state_t *state, uint32_t seed)
{
    memset(state, 0, sizeof(*state));
    state->rng_state = seed == 0U ? UINT32_C(0x4c4f5244) : seed;
    text_copy(state->player.name, sizeof(state->player.name), "Warrior");
    state->player.sex = LORD_SEX_MALE;
    initialize_realm(state);
    state->save_dirty = false;
    state->save_sequence = 0U;
    state->screen = LORD_SCREEN_TITLE;
}

size_t lord_menu_count(const lord_state_t *state)
{
    switch (state->screen) {
    case LORD_SCREEN_NAME:
        return 1U;
    case LORD_SCREEN_SEX:
        return 2U;
    case LORD_SCREEN_CLASS:
        return 3U;
    case LORD_SCREEN_TOWN:
        return 15U;
    case LORD_SCREEN_FOREST:
        return state->player.horse ? 5U : 4U;
    case LORD_SCREEN_WEAPON_SHOP:
    case LORD_SCREEN_ARMOR_SHOP:
        return 17U;
    case LORD_SCREEN_HEALER:
        return 3U;
    case LORD_SCREEN_TRAINING:
        return 2U;
    case LORD_SCREEN_BANK:
        return 4U;
    case LORD_SCREEN_BANK_TRANSFER:
    case LORD_SCREEN_PLAYERS:
    case LORD_SCREEN_MAIL_COMPOSE:
    case LORD_SCREEN_ROMANCE:
        return LORD_REALM_PLAYER_COUNT + 1U;
    case LORD_SCREEN_INN:
        return 11U;
    case LORD_SCREEN_BARTENDER:
        return 6U;
    case LORD_SCREEN_CONVERSE:
        return 3U;
    case LORD_SCREEN_SETH:
    case LORD_SCREEN_VIOLET:
        return 6U;
    case LORD_SCREEN_BLACKJACK:
        return 4U;
    case LORD_SCREEN_PLAYER_DETAIL:
        return 5U;
    case LORD_SCREEN_MAILBOX:
        return (size_t)state->mail_count + 2U;
    case LORD_SCREEN_ROMANCE_ACTION:
        return 5U;
    case LORD_SCREEN_IGM:
        return LORD_IGM_COUNT + 1U;
    case LORD_SCREEN_IGM_DETAIL:
        return 4U;
    case LORD_SCREEN_BATTLE:
        return 6U;
    case LORD_SCREEN_RIP_GALLERY:
        return LORD_RIP_SCENE_COUNT + 1U;
    case LORD_SCREEN_TEXT_EDITOR:
        return LORD_KEYBOARD_COUNT;
    case LORD_SCREEN_TITLE:
    case LORD_SCREEN_MAIL_VIEW:
    case LORD_SCREEN_NEWS:
    case LORD_SCREEN_RANKINGS:
    case LORD_SCREEN_SKILLS:
    case LORD_SCREEN_STATS:
    case LORD_SCREEN_MESSAGE:
    case LORD_SCREEN_DEAD:
    case LORD_SCREEN_DRAGON_VICTORY:
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
    int selected = (int)state->selection + direction;
    const int count_int = (int)count;
    while (selected < 0) {
        selected += count_int;
    }
    while (selected >= count_int) {
        selected -= count_int;
    }
    state->selection = (uint8_t)selected;
    if (state->screen != LORD_SCREEN_TEXT_EDITOR) {
        const size_t selected_size = (size_t)state->selection;
        if (selected_size < state->menu_scroll) {
            state->menu_scroll = state->selection;
        } else if (selected_size >= (size_t)state->menu_scroll + 7U) {
            state->menu_scroll = (uint8_t)(selected_size - 6U);
        }
    }
}

lord_event_t lord_activate(lord_state_t *state)
{
    switch (state->screen) {
    case LORD_SCREEN_TITLE:
        set_screen(state, LORD_SCREEN_NAME);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_NAME:
        begin_editor(state, LORD_EDITOR_NAME, LORD_SCREEN_NAME, "");
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_SEX:
        state->player.sex = state->selection == 0U ?
            LORD_SEX_MALE : LORD_SEX_FEMALE;
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
        case 9U: set_screen(state, LORD_SCREEN_IGM); break;
        case 10U: set_screen(state, LORD_SCREEN_NEWS); break;
        case 11U: set_screen(state, LORD_SCREEN_RANKINGS); break;
        case 12U: set_screen(state, LORD_SCREEN_RIP_GALLERY); break;
        case 13U:
            state->return_screen = LORD_SCREEN_TOWN;
            set_screen(state, LORD_SCREEN_STATS);
            break;
        default:
            set_screen(state, LORD_SCREEN_SKILLS);
            break;
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_FOREST:
        if (state->selection == 2U) {
            set_screen(state, LORD_SCREEN_HEALER);
        } else if (state->selection == 3U) {
            set_screen(state, LORD_SCREEN_TOWN);
        } else if (state->selection == 4U && state->player.horse) {
            if (random_below(state, 2U) == 0U) {
                state->player.gold = add_u32_saturating(
                    state->player.gold,
                    (uint32_t)state->player.level * 500U);
                set_message(state, LORD_SCREEN_FOREST,
                            "DarkCloak Tavern welcomes your horse.",
                            "A lucky game pays forest gold.");
            } else {
                set_message(state, LORD_SCREEN_FOREST,
                            "The DarkCloak fire warms you.",
                            "The house wins tonight's wager.");
            }
            mark_dirty(state);
        } else if (state->player.forest_fights == 0U) {
            set_message(state, LORD_SCREEN_FOREST,
                        "You are too weary to fight.",
                        "Sleep at the inn for a new day.");
        } else if (state->selection == 0U) {
            --state->player.forest_fights;
            mark_dirty(state);
            if (random_below(state, 5U) == 1U) {
                run_forest_event(state);
            } else {
                begin_forest_battle(state);
            }
        } else if (state->player.level < LORD_MAX_LEVEL) {
            set_message(state, LORD_SCREEN_FOREST,
                        "The dragon ignores your challenge.",
                        "Reach level 12 first.");
        } else if (state->player.seen_dragon) {
            set_message(state, LORD_SCREEN_FOREST,
                        "You already faced the dragon today.",
                        "Gather strength for tomorrow.");
        } else {
            --state->player.forest_fights;
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
        } else if (state->selection == 2U) {
            set_screen(state, LORD_SCREEN_BANK_TRANSFER);
        } else {
            set_screen(state, LORD_SCREEN_TOWN);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_BANK_TRANSFER:
        if (state->selection >= LORD_REALM_PLAYER_COUNT) {
            set_screen(state, LORD_SCREEN_BANK);
        } else if (state->player.bank < 100U) {
            set_message(state, LORD_SCREEN_BANK_TRANSFER,
                        "Transfers require 100 banked gold.", "");
        } else {
            state->player.bank -= 100U;
            state->realm[state->selection].gold = add_u32_saturating(
                state->realm[state->selection].gold, 100U);
            state->selected_player = state->selection;
            add_mail(state, state->selected_player, LORD_MAIL_SENDER_HERO,
                     LORD_MAIL_CUSTOM, false,
                     "Thank you for the 100 gold bank transfer.");
            set_message(state, LORD_SCREEN_BANK,
                        "The bank transfers 100 gold.",
                        "The recipient sends thanks.");
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_INN:
        switch (state->selection) {
        case 0U:
            reset_new_day(state);
            set_message(state, LORD_SCREEN_TOWN,
                        "A new day dawns over the realm.",
                        "Your strength has returned.");
            break;
        case 1U: set_screen(state, LORD_SCREEN_BARTENDER); break;
        case 2U: set_screen(state, LORD_SCREEN_CONVERSE); break;
        case 3U: set_screen(state, LORD_SCREEN_SETH); break;
        case 4U: set_screen(state, LORD_SCREEN_VIOLET); break;
        case 5U:
            set_message(state, LORD_SCREEN_INN,
                        "Seth sings of warriors and crimson fire.",
                        "The whole tavern joins the final chorus.");
            break;
        case 6U: set_screen(state, LORD_SCREEN_BLACKJACK); break;
        case 7U: {
            size_t found = LORD_REALM_PLAYER_COUNT;
            for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
                if (state->realm[index].alive && state->realm[index].at_inn) {
                    found = index;
                    break;
                }
            }
            if (found == LORD_REALM_PLAYER_COUNT || state->player.gold < 100U) {
                set_message(state, LORD_SCREEN_INN,
                            found == LORD_REALM_PLAYER_COUNT ?
                                "No vulnerable sleeper is at the inn." :
                                "The bartender demands a 100 gold bribe.", "");
            } else {
                state->player.gold -= 100U;
                state->selected_player = (uint8_t)found;
                begin_pvp_battle(state, true);
            }
            break;
        }
        case 8U:
            begin_editor(state, LORD_EDITOR_ANNOUNCEMENT,
                         LORD_SCREEN_INN, "");
            break;
        case 9U:
            set_message(state, LORD_SCREEN_INN,
                        "Your room is safe until morning.",
                        state->announcement[0] == '\0' ?
                            "No announcement hangs in the bar." :
                            state->announcement);
            break;
        default: set_screen(state, LORD_SCREEN_TOWN); break;
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_BARTENDER:
        if (state->selection == 0U) {
            if (state->player.gold < 10U) {
                set_message(state, LORD_SCREEN_BARTENDER,
                            "A tankard costs 10 gold.", "");
            } else {
                state->player.gold -= 10U;
                state->player.hit_points += 2;
                if (state->player.hit_points > state->player.max_hit_points) {
                    state->player.hit_points = state->player.max_hit_points;
                }
                mark_dirty(state);
                set_message(state, LORD_SCREEN_BARTENDER,
                            "The bartender pours a dark ale.",
                            "Two hit points return.");
            }
        } else if (state->selection == 1U) {
            set_message(state, LORD_SCREEN_BARTENDER,
                        "Violet heals warriors and guards her heart.",
                        "Charm and patience impress her.");
        } else if (state->selection == 2U) {
            set_message(state, LORD_SCREEN_BARTENDER,
                        "Seth Able knows every song in the realm.",
                        "He notices charm, wit, and gems.");
        } else if (state->selection == 3U) {
            set_message(state, LORD_SCREEN_BARTENDER,
                        "The Red Dragon waits beyond level twelve.",
                        "Face it only once each day.");
        } else if (state->selection == 4U) {
            if (random_below(state, 2U) == 0U) {
                state->player.charm = add_u16_saturating(
                    state->player.charm, 1U);
                set_message(state, LORD_SCREEN_BARTENDER,
                            "You outdrink the bartender!",
                            "The cheering crowd adds one charm.");
            } else {
                state->player.hit_points = state->player.hit_points > 2 ?
                    state->player.hit_points - 2 : 1;
                set_message(state, LORD_SCREEN_BARTENDER,
                            "The bartender wins the drinking contest.",
                            "Your head costs two hit points.");
            }
            mark_dirty(state);
        } else {
            set_screen(state, LORD_SCREEN_INN);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_CONVERSE:
        if (state->selection == 0U) {
            set_message(state, LORD_SCREEN_CONVERSE,
                        state->conversation[0] == '\0' ?
                            "The tavern conversation is quiet." :
                            state->conversation,
                        "The fire crackles under the voices.");
        } else if (state->selection == 1U) {
            begin_editor(state, LORD_EDITOR_CONVERSATION,
                         LORD_SCREEN_CONVERSE, "");
        } else {
            set_screen(state, LORD_SCREEN_INN);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_SETH:
        return npc_romance(state, 0);
    case LORD_SCREEN_VIOLET:
        return npc_romance(state, 1);
    case LORD_SCREEN_BLACKJACK:
        return blackjack_action(state);
    case LORD_SCREEN_PLAYERS:
    case LORD_SCREEN_ROMANCE:
        if (state->selection >= LORD_REALM_PLAYER_COUNT) {
            set_screen(state, LORD_SCREEN_TOWN);
        } else {
            state->selected_player = state->selection;
            set_screen(state, state->screen == LORD_SCREEN_ROMANCE ?
                       LORD_SCREEN_ROMANCE_ACTION : LORD_SCREEN_PLAYER_DETAIL);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_PLAYER_DETAIL:
        if (state->selection == 0U) {
            begin_pvp_battle(state, false);
        } else if (state->selection == 1U) {
            begin_editor(state, LORD_EDITOR_MAIL,
                         LORD_SCREEN_PLAYER_DETAIL, "");
        } else if (state->selection == 2U) {
            set_screen(state, LORD_SCREEN_ROMANCE_ACTION);
        } else if (state->selection == 3U) {
            begin_editor(state, LORD_EDITOR_SAYING,
                         LORD_SCREEN_PLAYER_DETAIL,
                         state->realm[state->selected_player].saying);
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
            begin_editor(state, LORD_EDITOR_MAIL, LORD_SCREEN_MAILBOX, "");
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_ROMANCE_ACTION:
        return perform_romance_action(state);
    case LORD_SCREEN_IGM:
        if (state->selection >= LORD_IGM_COUNT) {
            set_screen(state, LORD_SCREEN_TOWN);
        } else {
            state->selected_igm = state->selection;
            set_screen(state, LORD_SCREEN_IGM_DETAIL);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_IGM_DETAIL:
        return run_igm_action(state);
    case LORD_SCREEN_NEWS:
    case LORD_SCREEN_RANKINGS:
    case LORD_SCREEN_SKILLS:
        set_screen(state, LORD_SCREEN_TOWN);
        return LORD_EVENT_CONFIRM;
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
        if (state->selection >= 1U && state->selection <= 3U) {
            return use_battle_skill(state, (size_t)(state->selection - 1U));
        }
        if (state->selection == 4U) {
            if (state->battle_kind != LORD_BATTLE_FOREST) {
                text_copy(state->battle_line, sizeof(state->battle_line),
                          "There is no escape from this fight.");
            } else if (random_below(state, 2U) == 0U) {
                state->battle_kind = LORD_BATTLE_NONE;
                set_message(state, LORD_SCREEN_FOREST,
                            "You escape into the undergrowth.", "");
            } else {
                const int32_t received = enemy_damage(state);
                state->player.hit_points -= received;
                text_copy(state->battle_line, sizeof(state->battle_line),
                          "Escape fails; the enemy strikes.");
                if (state->player.hit_points <= 0) {
                    state->player.hit_points = 0;
                    state->battle_kind = LORD_BATTLE_NONE;
                    set_screen(state, LORD_SCREEN_DEAD);
                    return LORD_EVENT_LOSE;
                }
            }
            mark_dirty(state);
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
                    "Sleep before returning to the forest.");
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_DRAGON_VICTORY: {
        const lord_class_t hero_class = state->player.hero_class;
        initialize_player(state, hero_class);
        set_message(state, LORD_SCREEN_TOWN,
                    "A new legend begins.",
                    "Your dragon deed grants lasting power.");
        return LORD_EVENT_CONFIRM;
    }
    case LORD_SCREEN_TEXT_EDITOR:
        return activate_editor(state);
    }
    return LORD_EVENT_NONE;
}

lord_event_t lord_cancel(lord_state_t *state)
{
    switch (state->screen) {
    case LORD_SCREEN_NAME:
    case LORD_SCREEN_SEX:
        set_screen(state, LORD_SCREEN_TITLE);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_CLASS:
        set_screen(state, LORD_SCREEN_SEX);
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
    case LORD_SCREEN_NEWS:
    case LORD_SCREEN_RANKINGS:
    case LORD_SCREEN_SKILLS:
    case LORD_SCREEN_RIP_GALLERY:
        set_screen(state, LORD_SCREEN_TOWN);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_BANK_TRANSFER:
        set_screen(state, LORD_SCREEN_BANK);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_BARTENDER:
    case LORD_SCREEN_CONVERSE:
    case LORD_SCREEN_SETH:
    case LORD_SCREEN_VIOLET:
    case LORD_SCREEN_BLACKJACK:
        set_screen(state, LORD_SCREEN_INN);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_PLAYER_DETAIL:
        set_screen(state, LORD_SCREEN_PLAYERS);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_MAIL_VIEW:
    case LORD_SCREEN_MAIL_COMPOSE:
        set_screen(state, LORD_SCREEN_MAILBOX);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_ROMANCE_ACTION:
        set_screen(state, LORD_SCREEN_PLAYER_DETAIL);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_IGM_DETAIL:
        set_screen(state, LORD_SCREEN_IGM);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_RIP_SCENE:
        set_screen(state, LORD_SCREEN_RIP_GALLERY);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_STATS:
    case LORD_SCREEN_MESSAGE:
        set_screen(state, state->return_screen);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_TEXT_EDITOR:
        set_screen(state, state->editor_return_screen);
        return LORD_EVENT_CONFIRM;
    default:
        return LORD_EVENT_NONE;
    }
}

const char *lord_class_name(lord_class_t hero_class)
{
    switch (hero_class) {
    case LORD_CLASS_DEATH_KNIGHT: return "Death Knight";
    case LORD_CLASS_MYSTICAL: return "Mystical Skills";
    case LORD_CLASS_THIEF: return "Thieving Skills";
    }
    return "Unknown";
}

const char *lord_sex_name(lord_sex_t sex)
{
    return sex == LORD_SEX_FEMALE ? "Female" : "Male";
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
    if (state->player.level == 0U || state->player.level >= LORD_MAX_LEVEL) {
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
    const uint8_t party = mail->outgoing ? mail->recipient : mail->sender;
    if (party < LORD_REALM_PLAYER_COUNT) {
        return state->realm[party].name;
    }
    if (party == LORD_MAIL_SENDER_SETH) {
        return "Seth Able";
    }
    if (party == LORD_MAIL_SENDER_TURGON) {
        return "Turgon";
    }
    return state->player.name;
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
    case LORD_MAIL_REPLY: return "Re: Your Letter";
    case LORD_MAIL_PVP_VICTORY: return "A Worthy Fight";
    case LORD_MAIL_ROMANCE: return "About Your Proposal";
    case LORD_MAIL_PROPOSAL: return "Our Wedding";
    case LORD_MAIL_CUSTOM: return mail->outgoing ? "Sent Letter" : "Letter";
    case LORD_MAIL_ATTACK: return "Inn Attack";
    case LORD_MAIL_BABY: return "Family News";
    case LORD_MAIL_ANNOUNCEMENT: return "Town Announcement";
    }
    return "Message";
}

const char *lord_mail_body_1(const lord_state_t *state, size_t index)
{
    const lord_mail_t *const mail = mail_at(state, index);
    return mail == NULL ? "This message has vanished." : mail->body;
}

const char *lord_mail_body_2(const lord_state_t *state, size_t index)
{
    const lord_mail_t *const mail = mail_at(state, index);
    if (mail == NULL) {
        return "";
    }
    return mail->outgoing ? "Recorded in your sent mail." :
                            "A local realm message.";
}

size_t lord_mail_unread_count(const lord_state_t *state)
{
    size_t unread = 0U;
    for (size_t index = 0U;
         index < state->mail_count && index < LORD_MAIL_COUNT_MAX; ++index) {
        if (state->mail[index].unread) {
            ++unread;
        }
    }
    return unread;
}

const char *lord_rip_scene_name(size_t index)
{
    return index < LORD_RIP_SCENE_COUNT ? s_rip_scene_names[index] :
                                          "Unknown";
}

const char *lord_igm_name(size_t index)
{
    return index < LORD_IGM_COUNT ? s_igm_names[index] : "Unknown";
}

const char *lord_keyboard_label(size_t index)
{
    static char label[2];
    const size_t character_count = sizeof(s_keyboard_chars) - 1U;
    if (index < character_count) {
        label[0] = s_keyboard_chars[index];
        label[1] = '\0';
        return label;
    }
    return index == character_count ? "DONE" : "DEL";
}
