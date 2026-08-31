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
    {"Long Sword", 8000U, 30}, {"Huge Axe", 20000U, 40},
    {"Bone Cruncher", 50000U, 60}, {"Twin Swords", 100000U, 80},
    {"Power Axe", 200000U, 120}, {"Able's Sword", 350000U, 180},
    {"Wans' Weapon", 600000U, 250},
    {"Spear of Gold", 1000000U, 350},
    {"Crystal Shard", 1750000U, 500},
    {"Nira's Teeth", 3000000U, 800},
    {"Blood Sword", 5000000U, 1200},
    {"Death Sword", 8000000U, 1800},
};

static const lord_item_t s_armor[LORD_ARMOR_COUNT] = {
    {"Nothing", 0U, 0}, {"Coat", 200U, 1},
    {"Heavy Coat", 1000U, 3}, {"Leather Vest", 3000U, 10},
    {"Bronze Armour", 8000U, 15}, {"Iron Armour", 20000U, 25},
    {"Graphite Armour", 50000U, 35},
    {"Erdricks Armour", 100000U, 50},
    {"Armour Of Death", 200000U, 75},
    {"Able's Armour", 350000U, 100},
    {"Full Body Armour", 600000U, 150},
    {"Blood Armour", 1000000U, 225},
    {"Magic Protection", 1750000U, 300},
    {"Belar's Mail", 3000000U, 400},
    {"Golden Armour", 5000000U, 600},
    {"Armour Of Lore", 8000000U, 1000},
};

static const lord_trainer_t s_trainers[LORD_MAX_LEVEL - 1] = {
    {"Halder", 15, 30, 10, 5, 2, 50U},
    {"Barak", 17, 40, 15, 7, 3, 200U},
    {"Aragorn", 35, 70, 20, 10, 5, 650U},
    {"Olodrin", 70, 120, 30, 12, 10, 1800U},
    {"Sandtiger", 100, 200, 50, 20, 15, 6000U},
    {"Sparhawk", 150, 400, 75, 35, 22, 16000U},
    {"Atsuko Sensei", 250, 600, 125, 50, 35, 34000U},
    {"Aladdin", 350, 800, 185, 75, 60, 110000U},
    {"Prince Caspian", 500, 1200, 250, 110, 80, 300000U},
    {"Gandalf", 800, 1800, 350, 150, 120, 625000U},
    {"Turgon", 1200, 2500, 550, 200, 150, 1250000U},
};

#include "generated/lord_monsters.h"

typedef struct {
    const char *name;
    const char *saying;
    lord_hero_style_t hero_style;
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
        {"Sir Galahad", "Honor before ChompCoin.", LORD_HERO_STYLE_HERO,
         LORD_CLASS_DEATH_KNIGHT, 1U, 24, 11, 2, 320U, 25U},
        {"Moonwitch", "The stars remember.", LORD_HERO_STYLE_HEROINE,
         LORD_CLASS_MYSTICAL, 2U, 38, 19, 5, 850U, 130U},
        {"Shadow Jack", "Your ChompCoin pouch looks heavy.", LORD_HERO_STYLE_HERO,
         LORD_CLASS_THIEF, 3U, 58, 30, 8, 1800U, 475U},
        {"Lady Celes", "No dragon frightens me.", LORD_HERO_STYLE_HEROINE,
         LORD_CLASS_MYSTICAL, 4U, 92, 46, 13, 4200U, 1200U},
        {"Iron Rose", "Steel blooms in battle.", LORD_HERO_STYLE_HEROINE,
         LORD_CLASS_DEATH_KNIGHT, 5U, 145, 72, 22, 9000U, 3900U},
        {"Nightblade", "You never saw me.", LORD_HERO_STYLE_HERO,
         LORD_CLASS_THIEF, 6U, 230, 108, 34, 18000U, 9800U},
        {"Aria Dawn", "Magic favors the bold.", LORD_HERO_STYLE_HEROINE,
         LORD_CLASS_MYSTICAL, 7U, 360, 165, 52, 43000U, 27000U},
        {"Dread Rowan", "Meet me in the arena.", LORD_HERO_STYLE_HERO,
         LORD_CLASS_DEATH_KNIGHT, 8U, 590, 255, 79, 92000U, 71000U},
    };

static const char *const s_rip_scene_names[LORD_RIP_SCENE_COUNT] = {
    "Town Square", "Dark Forest", "Red Dragon Inn", "Battle Arena",
    "Red Dragon", "King Arthur's", "Abdul's Armour", "Healer's Hut",
    "Turgon's Gym", "First Bank", "Graveyard", "DarkCloak Tavern",
};

static const char *const s_igm_names[LORD_IGM_COUNT] = {
    "Aragorn's Math", "Barak's House", "The Grab Bag",
    "The Graveyard", "Olodrin's Youth Guild", "The Outhouse",
    "The Pickle Goddess",
};

/* Fixed server name codes keep club identity compact, kid-safe, and
 * deterministic on old cartridges. Code zero always means no club. */
static const char *const s_guild_names[LORD_GUILD_NAME_COUNT] = {
    "Brave Badgers", "Curious Comets", "Kind Krakens", "Lantern Lions",
    "Merry Meteors", "Noble Narwhals", "Sunny Sprites", "Clever Chompers",
    "Friendly Falcons", "Daring Dragons", "Helpful Heroes",
    "Starlight Scouts", "Rainbow Rangers", "Cozy Capybaras",
    "Gallant Geckos", "Questing Quokkas",
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

static bool realm_character_bound(const lord_state_t *state)
{
    uint8_t actor_combined = 0U;
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        actor_combined = (uint8_t)(
            actor_combined | state->sync_actor_id[index]);
    }
    return actor_combined != 0U;
}

static bool realm_actor_bound(const lord_state_t *state, size_t slot)
{
    uint8_t actor_combined = 0U;
    if (slot >= LORD_REALM_PLAYER_COUNT) {
        return false;
    }
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        actor_combined = (uint8_t)(
            actor_combined | state->realm_actor_ids[slot][index]);
    }
    return actor_combined != 0U;
}

static void remove_friendship_hp_bonus(lord_state_t *state)
{
    state->player.max_hit_points = state->player.max_hit_points > 5 ?
        state->player.max_hit_points - 5 : 1;
    if (state->player.hit_points > state->player.max_hit_points) {
        state->player.hit_points = state->player.max_hit_points;
    }
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

static void mark_local_save_dirty(lord_state_t *state)
{
    state->save_dirty = true;
    ++state->save_local_generation;
}

static void mark_dirty(lord_state_t *state)
{
    mark_local_save_dirty(state);
    if (state->save_sequence != UINT32_MAX) {
        ++state->save_sequence;
    }
}

/* Resolve local knockouts in the same durable mutation as the lethal hit.
 * The DEAD screen is then informational, so rebooting cannot skip the loss. */
static void apply_local_knockout(lord_state_t *state)
{
    state->player.gold = 0U;
    state->player.experience -= state->player.experience / 10U;
    state->player.high_spirits = false;
    state->player.hit_points = state->player.max_hit_points;
    state->player.forest_fights = 0U;
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
    state->partner_index = -1;
    state->npc_friend = -1;
    state->pvp_fights = LORD_PVP_FIGHTS_PER_DAY;
    state->friendship_actions = LORD_FRIENDSHIP_ACTIONS_PER_DAY;
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        const lord_realm_template_t *const source =
            &s_realm_templates[index];
        lord_realm_player_t *const target = &state->realm[index];
        text_copy(target->name, sizeof(target->name), source->name);
        text_copy(target->saying, sizeof(target->saying), source->saying);
        target->hero_style = source->hero_style;
        target->hero_class = source->hero_class;
        target->level = source->level;
        target->alive = true;
        target->at_inn = index % 3U == 0U;
        target->teamed = false;
        target->trust = 0U;
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

static void maybe_help_young_hero(lord_state_t *state)
{
    if ((state->partner_index < 0 && state->npc_friend < 0) ||
        random_below(state, 5U) != 0U) {
        return;
    }
    state->player.young_heroes_helped = add_u16_saturating(
        state->player.young_heroes_helped, 1U);
    add_mail(state, LORD_MAIL_SENDER_SETH, LORD_MAIL_SENDER_HERO,
             LORD_MAIL_MENTOR, false,
             "Your adventure team helped a young hero train!");
    add_named_log(state, "", " helped a young hero train.");
}

static void reset_new_day_common(lord_state_t *state)
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
    state->friendship_actions = LORD_FRIENDSHIP_ACTIONS_PER_DAY;
    state->igm_used_mask = 0U;
    state->player.seen_dragon = false;
    state->player.high_spirits = false;
    if (state->player.day != UINT16_MAX) {
        ++state->player.day;
    }
    for (size_t index = 0U; index < LORD_REALM_PLAYER_COUNT; ++index) {
        state->realm[index].alive = true;
        state->realm[index].hit_points = state->realm[index].max_hit_points;
        state->realm[index].at_inn =
            ((index + (size_t)state->player.day) % 3U) == 0U;
    }
    maybe_help_young_hero(state);
    add_log(state, "A new day dawned over the realm.");
    mark_dirty(state);
}

static void reset_new_day(lord_state_t *state)
{
    reset_new_day_common(state);
}

/** Local and server-authoritative days deliberately share one economy. */
static void reset_hourly_realm_day(lord_state_t *state)
{
    reset_new_day_common(state);
}

static void initialize_player(lord_state_t *state,
                              lord_class_t hero_class)
{
    char name[LORD_NAME_BYTES];
    text_copy(name, sizeof(name), state->player.name);
    const lord_hero_style_t hero_style = state->player.hero_style;
    const uint8_t former_kills = state->player.dragon_kills;
    const uint16_t former_day = state->player.day;
    const uint16_t former_pvp_wins = state->player.pvp_wins;
    const uint16_t former_pvp_losses = state->player.pvp_losses;
    const bool has_friendship_bonus =
        state->partner_index >= 0 || state->npc_friend >= 0;
    const int32_t rebirth_hit_points = 20 + (int32_t)former_kills * 5 +
        (has_friendship_bonus ? 5 : 0);
    state->player = (lord_player_t){
        .hero_style = hero_style,
        .hero_class = hero_class,
        .level = 1U,
        .hit_points = rebirth_hit_points,
        .max_hit_points = rebirth_hit_points,
        .strength = 10 + (int32_t)former_kills * 2,
        .defense = 1 + (int32_t)former_kills,
        .gold = 500U,
        .forest_fights = LORD_FOREST_FIGHTS_PER_DAY,
        .dragon_kills = former_kills,
        .day = former_day == 0U ? 1U : former_day,
        .pvp_wins = former_pvp_wins,
        .pvp_losses = former_pvp_losses,
        .charm = 10U,
        .amulet = former_kills > 0U,
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

static void choose_enemy_intent(lord_state_t *state)
{
    state->enemy_intent = (lord_enemy_intent_t)random_below(state, 4U);
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
    state->thief_bonus_claimed = false;
    choose_enemy_intent(state);
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
    state->thief_bonus_claimed = false;
    choose_enemy_intent(state);
    text_copy(state->battle_line, sizeof(state->battle_line),
              "Your master raises a weapon.");
    set_screen(state, LORD_SCREEN_BATTLE);
}

static void begin_dragon_battle(lord_state_t *state)
{
    text_copy(state->enemy.name, sizeof(state->enemy.name), "The Red Dragon");
    text_copy(state->enemy.weapon, sizeof(state->enemy.weapon), "Dragon Fire");
    state->enemy.death_text = "The Red Dragon yields and leaves the realm!";
    state->enemy.hit_points = 10000;
    state->enemy.max_hit_points = 10000;
    state->enemy.strength = 1500;
    state->enemy.defense = 200;
    state->enemy.gold = 1000U;
    state->enemy.experience = 1000U;
    state->player.seen_dragon = true;
    state->battle_kind = LORD_BATTLE_DRAGON;
    state->thief_bonus_claimed = false;
    choose_enemy_intent(state);
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
                    in_inn ? "No resting warrior accepts the challenge." :
                             "That warrior is already defeated.", "");
        return;
    }
    --state->pvp_fights;
    text_copy(state->enemy.name, sizeof(state->enemy.name), opponent->name);
    text_copy(state->enemy.weapon, sizeof(state->enemy.weapon), "Arena weapon");
    state->enemy.death_text = "The defeated warrior offers a respectful bow.";
    state->enemy.hit_points = opponent->hit_points;
    state->enemy.max_hit_points = opponent->max_hit_points;
    state->enemy.strength = opponent->strength;
    state->enemy.defense = opponent->defense;
    state->enemy.gold = opponent->gold;
    state->enemy.experience = opponent->experience;
    state->battle_kind = in_inn ? LORD_BATTLE_INN : LORD_BATTLE_PVP;
    state->thief_bonus_claimed = false;
    choose_enemy_intent(state);
    text_copy(state->battle_line, sizeof(state->battle_line),
              in_inn ? "The innkeeper opens the friendly sparring ring." :
                       "The town gathers around the duel.");
    mark_dirty(state);
    set_screen(state, LORD_SCREEN_BATTLE);
}

/* Cartridge ELFs cannot import libgcc's non-PIC 64-bit division helpers.
 * Scale with checked 32-bit quotient/remainder arithmetic instead. */
static int32_t scale_damage(int32_t damage, int32_t numerator,
                            int32_t denominator)
{
    if (damage <= 0 || numerator <= 0 || denominator <= 0) {
        return 0;
    }
    const int32_t quotient = damage / denominator;
    const int32_t remainder = damage % denominator;
    if (quotient > INT32_MAX / numerator ||
        (remainder != 0 && remainder > INT32_MAX / numerator)) {
        return INT32_MAX;
    }
    const int32_t whole = quotient * numerator;
    const int32_t fraction = (remainder * numerator) / denominator;
    return whole > INT32_MAX - fraction ? INT32_MAX : whole + fraction;
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
    if (state->player.high_spirits) {
        damage = scale_damage(damage, 23, 20);
    }
    damage -= state->enemy.defense;
    return damage < 1 ? 1 : damage;
}

typedef enum {
    LORD_COMBAT_ACTION_STRIKE = 0,
    LORD_COMBAT_ACTION_GUARD,
    LORD_COMBAT_ACTION_TECHNIQUE,
    LORD_COMBAT_ACTION_FEINT,
    LORD_COMBAT_ACTION_RUN,
} lord_combat_action_t;

static int32_t friendship_guard_bonus(const lord_state_t *state)
{
    const uint16_t earned = (uint16_t)(
        (state->player.friendship_badges / 5U) * 2U);
    return earned > 8U ? 8 : (int32_t)earned;
}

static int32_t enemy_damage(lord_state_t *state)
{
    const int32_t half = state->enemy.strength / 2;
    int32_t damage = half +
        (int32_t)random_below(state, (uint32_t)(half + 1));
    damage -= state->player.defense;
    damage -= friendship_guard_bonus(state);
    if (state->player.skill[LORD_CLASS_THIEF] > 0U &&
        random_below(state, 8U) <
            (uint32_t)(state->player.skill[LORD_CLASS_THIEF] / 10U)) {
        damage = 0;
    }
    return damage < 0 ? 0 : damage;
}

static int32_t enemy_reply_damage(lord_state_t *state,
                                  lord_combat_action_t action,
                                  bool technique_evade)
{
    if (state->enemy_intent == LORD_ENEMY_INTENT_GUARD) {
        return 0;
    }

    int32_t damage = enemy_damage(state);
    if (state->enemy_intent == LORD_ENEMY_INTENT_POWER) {
        damage = scale_damage(damage, 2, 1);
    } else if (state->enemy_intent == LORD_ENEMY_INTENT_QUICK) {
        damage = scale_damage(damage, 3, 4);
    }

    /* Shadowstep avoids committed blows, while a quick enemy can still catch
     * the thief. This is separate from the mastery-based passive evade. */
    if (technique_evade && state->enemy_intent != LORD_ENEMY_INTENT_QUICK) {
        return 0;
    }

    if (action == LORD_COMBAT_ACTION_GUARD) {
        if (state->enemy_intent == LORD_ENEMY_INTENT_POWER) {
            damage = scale_damage(damage, 1, 5);
        } else if (state->enemy_intent == LORD_ENEMY_INTENT_QUICK) {
            damage = scale_damage(damage, 2, 3);
        } else {
            damage = scale_damage(damage, 1, 4);
        }
    } else if (action == LORD_COMBAT_ACTION_FEINT &&
               state->enemy_intent == LORD_ENEMY_INTENT_QUICK) {
        damage = scale_damage(damage, 3, 2);
    } else if (action == LORD_COMBAT_ACTION_TECHNIQUE &&
               state->player.hero_class == LORD_CLASS_DEATH_KNIGHT) {
        /* Reckless Blow leaves the Death Knight open if it does not finish
         * the fight. */
        damage = scale_damage(damage, 3, 2);
    }
    return damage;
}

static int32_t player_action_damage(lord_state_t *state,
                                    lord_combat_action_t action,
                                    int32_t multiplier)
{
    if (action == LORD_COMBAT_ACTION_GUARD) {
        return 0;
    }
    int32_t damage = player_damage(state, multiplier);
    if (state->enemy_intent == LORD_ENEMY_INTENT_GUARD) {
        if (action != LORD_COMBAT_ACTION_FEINT) {
            damage = scale_damage(damage, 1, 4);
        }
    } else if (action == LORD_COMBAT_ACTION_FEINT) {
        damage = scale_damage(
            damage, 1,
            state->enemy_intent == LORD_ENEMY_INTENT_QUICK ? 3 : 2);
    }
    return damage < 1 ? 1 : damage;
}

static void set_exchange_line(lord_state_t *state, const char *prefix,
                              int32_t dealt, int32_t received,
                              lord_combat_action_t action)
{
    text_copy(state->battle_line, sizeof(state->battle_line), prefix);
    text_append_u32(state->battle_line, sizeof(state->battle_line),
                    (uint32_t)dealt);
    if (state->enemy_intent == LORD_ENEMY_INTENT_GUARD) {
        text_append(state->battle_line, sizeof(state->battle_line),
                    "; foe holds guard.");
        return;
    }
    if (received == 0) {
        text_append(state->battle_line, sizeof(state->battle_line),
                    action == LORD_COMBAT_ACTION_GUARD ?
                        "; your guard stops the reply." :
                        "; you evade the reply.");
        return;
    }
    text_append(state->battle_line, sizeof(state->battle_line), "; foe hits ");
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
        text_append(line, sizeof(line), " ChompCoin, ");
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
            const size_t skill = (size_t)state->player.hero_class;
            const uint16_t mastery = (uint16_t)state->player.skill[skill] + 2U;
            state->player.skill[skill] = mastery > LORD_SKILL_MASTERY_MAX ?
                LORD_SKILL_MASTERY_MAX : (uint8_t)mastery;
            if (state->player.skill_uses[skill] < 9U) {
                ++state->player.skill_uses[skill];
            }
            add_named_log(state, "", " rose to a new level.");
            set_message(state, LORD_SCREEN_TOWN,
                        "Your master yields. Level gained!",
                        "Your class technique also grows stronger.");
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
        const uint32_t pvp_experience = 500U *
            (uint32_t)opponent->level * (uint32_t)opponent->level;
        state->player.experience = add_u32_saturating(
            state->player.experience, pvp_experience);
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
                    "Half their carried ChompCoin is yours.");
        mark_dirty(state);
        return LORD_EVENT_WIN;
    }
    state->battle_kind = LORD_BATTLE_NONE;
    if (state->player.dragon_kills != UINT8_MAX) {
        ++state->player.dragon_kills;
    }
    add_named_log(state, "", " slew the Red Dragon!");
    const lord_class_t hero_class = state->player.hero_class;
    initialize_player(state, hero_class);
    set_screen(state, LORD_SCREEN_DRAGON_VICTORY);
    return LORD_EVENT_WIN;
}

static lord_event_t battle_action(lord_state_t *state,
                                  lord_combat_action_t action,
                                  int32_t multiplier,
                                  const char *prefix,
                                  bool technique_evade)
{
    const int32_t dealt = player_action_damage(state, action, multiplier);
    state->enemy.hit_points -= dealt;
    if (state->enemy.hit_points <= 0) {
        state->enemy.hit_points = 0;
        return finish_battle(state);
    }
    const int32_t received = enemy_reply_damage(
        state, action, technique_evade);
    state->player.hit_points -= received;
    mark_dirty(state);
    set_exchange_line(state, prefix, dealt, received, action);
    if (state->player.hit_points <= 0) {
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
        apply_local_knockout(state);
        state->battle_kind = LORD_BATTLE_NONE;
        add_named_log(state, "", " was knocked out in battle.");
        set_screen(state, LORD_SCREEN_DEAD);
        return LORD_EVENT_LOSE;
    }
    choose_enemy_intent(state);
    return LORD_EVENT_HIT;
}

static lord_event_t battle_round(lord_state_t *state, int32_t multiplier,
                                 const char *prefix)
{
    return battle_action(state, LORD_COMBAT_ACTION_STRIKE, multiplier,
                         prefix, false);
}

static lord_event_t use_class_technique(lord_state_t *state)
{
    const size_t skill = (size_t)state->player.hero_class;
    if (skill >= LORD_SKILL_COUNT || state->player.skill[skill] == 0U ||
        state->player.skill_uses[skill] == 0U) {
        text_copy(state->battle_line, sizeof(state->battle_line),
                  "Your technique has no uses remaining today.");
        return LORD_EVENT_CONFIRM;
    }
    --state->player.skill_uses[skill];
    mark_dirty(state);
    if (skill == LORD_CLASS_DEATH_KNIGHT) {
        const int32_t multiplier = state->player.skill[skill] >= 20U ? 3 : 2;
        return battle_action(state, LORD_COMBAT_ACTION_TECHNIQUE,
                             multiplier, "Reckless blow: ", false);
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
        return battle_action(state, LORD_COMBAT_ACTION_TECHNIQUE,
                             1, "Mystic renewal: ", false);
    }
    /* Bank the one-time forest bonus in the transient enemy purse so running,
     * losing, or rebooting cannot turn Shadowstep into free ChompCoin. */
    if (state->battle_kind == LORD_BATTLE_FOREST &&
        !state->thief_bonus_claimed) {
        const uint32_t bonus = state->enemy.gold / 8U + 1U;
        state->enemy.gold = add_u32_saturating(state->enemy.gold, bonus);
        state->thief_bonus_claimed = true;
    }
    return battle_action(state, LORD_COMBAT_ACTION_TECHNIQUE,
                         1, "Shadowstep: ", true);
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
        set_screen(state, LORD_SCREEN_HERO_STYLE);
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
        text_append(line, sizeof(line), " ChompCoin.");
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
                    "Royal ChompCoin and two charm are awarded.");
        break;
    case 12U: {
        const uint32_t found = (uint32_t)state->player.level * 750U;
        state->player.gold = add_u32_saturating(state->player.gold, found);
        text_append(line, sizeof(line), "You recover ");
        text_append_u32(line, sizeof(line), found);
        text_append(line, sizeof(line), " lost forest ChompCoin.");
        set_message(state, LORD_SCREEN_FOREST, line, "");
        break;
    }
    case 13U: {
        const int32_t damage = state->player.hero_class == LORD_CLASS_THIEF ?
            0 : (int32_t)random_below(state, 10U) + 1;
        state->player.hit_points -= damage;
        if (state->player.hit_points <= 0) {
            state->player.gems = 0U;
            apply_local_knockout(state);
            add_named_log(state, "", " was knocked out by a forest troll.");
            set_screen(state, LORD_SCREEN_DEAD);
        } else {
            set_message(state, LORD_SCREEN_FOREST,
                        "A troll lunges for your ChompCoin pouch!",
                        damage == 0 ? "Your thieving reflexes foil him." :
                                      "You drive him off, wounded.");
        }
        break;
    }
    default:
        if (random_below(state, 2U) == 0U) {
            state->player.gold = add_u32_saturating(
                state->player.gold, 10U);
            set_message(state, LORD_SCREEN_FOREST,
                        "DarkCloak Tavern appears in the gloom.",
                        "A lucky riddle wins 10 ChompCoin.");
        } else {
            set_message(state, LORD_SCREEN_FOREST,
                        "You warm yourself at DarkCloak Tavern.",
                        "The old man's riddle leaves you puzzled.");
        }
        break;
    }
    mark_dirty(state);
}

static lord_event_t perform_friendship_action(lord_state_t *state)
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
    if (state->friendship_actions == 0U) {
        set_message(state, LORD_SCREEN_FRIENDSHIP_ACTION,
                    "No friendship actions remain today.",
                    "Sleep at the inn and try tomorrow.");
        return LORD_EVENT_CONFIRM;
    }
    if (state->selection == 3U) {
        if (state->partner_index != (int8_t)state->selected_player) {
            set_message(state, LORD_SCREEN_FRIENDSHIP_ACTION,
                        "Choose this warrior as your teammate first.", "");
            return LORD_EVENT_CONFIRM;
        }
        --state->friendship_actions;
        state->player.young_heroes_helped = add_u16_saturating(
            state->player.young_heroes_helped, 1U);
        add_mail(state, state->selected_player, LORD_MAIL_SENDER_HERO,
                 LORD_MAIL_MENTOR, false,
                 "Our team helped a young hero begin training!");
        set_message(state, LORD_SCREEN_FRIENDSHIP_ACTION,
                    "You mentor a young hero together!",
                    "Teamwork brightens the realm.");
        add_named_log(state, "", " mentored a young hero.");
        mark_dirty(state);
        return LORD_EVENT_CONFIRM;
    }
    if (state->selection == 2U) {
        --state->friendship_actions;
        if (state->partner_index == (int8_t)state->selected_player) {
            remove_friendship_hp_bonus(state);
            person->teamed = false;
            state->partner_index = -1;
            memset(state->partner_actor_id, 0,
                   sizeof(state->partner_actor_id));
            set_message(state, LORD_SCREEN_FRIENDSHIP_ACTION,
                        "Your adventure team parts as friends.",
                        "The daily teamwork bonus is gone.");
            add_named_log(state, "", " ended an adventure team.");
            mark_dirty(state);
            return LORD_EVENT_CONFIRM;
        }
        if (state->partner_index >= 0 || state->npc_friend >= 0) {
            set_message(state, LORD_SCREEN_FRIENDSHIP_ACTION,
                        "You already have an adventure partner.", "");
            return LORD_EVENT_CONFIRM;
        }
        if (person->trust < 60U || person->teamed) {
            add_mail(state, state->selected_player, LORD_MAIL_SENDER_HERO,
                     LORD_MAIL_TEAM_INVITE, false,
                     "Let us build more trust before forming a team.");
            set_message(state, LORD_SCREEN_FRIENDSHIP_ACTION,
                        "The team invitation is kindly declined.",
                        "Build more trust first.");
            return LORD_EVENT_CONFIRM;
        }
        state->partner_index = (int8_t)state->selected_player;
        person->teamed = true;
        state->player.max_hit_points += 5;
        state->player.hit_points += 5;
        add_mail(state, state->selected_player, LORD_MAIL_SENDER_HERO,
                 LORD_MAIL_TEAM_PLEDGE, false,
                 "Our adventure team is official. Let us help the realm!");
        add_named_log(state, "", " formed an adventure team.");
        set_message(state, LORD_SCREEN_FRIENDSHIP_ACTION,
                    "Your adventure team is official!",
                    "Teamwork grants 5 HP and an inn bonus.");
        mark_dirty(state);
        return LORD_EVENT_CONFIRM;
    }
    uint8_t gain = 15U;
    if (state->selection == 1U) {
        if (state->player.gold < 100U) {
            set_message(state, LORD_SCREEN_FRIENDSHIP_ACTION,
                        "Shared supplies cost 100 ChompCoin.", "");
            return LORD_EVENT_CONFIRM;
        }
        state->player.gold -= 100U;
        gain = 30U;
    }
    --state->friendship_actions;
    const uint16_t trust = (uint16_t)person->trust + gain;
    person->trust = trust > 100U ? 100U : (uint8_t)trust;
    state->player.charm = add_u16_saturating(state->player.charm, 1U);
    mark_dirty(state);
    set_message(state, LORD_SCREEN_FRIENDSHIP_ACTION,
                state->selection == 0U ?
                    "Your encouragement earns a bright smile." :
                    "The shared supplies are gratefully accepted.",
                "Trust and charm have grown.");
    return LORD_EVENT_CONFIRM;
}

static lord_event_t npc_friendship(lord_state_t *state, int8_t npc)
{
    const lord_screen_t screen = npc == 0 ? LORD_SCREEN_SETH :
                                                LORD_SCREEN_VIOLET;
    const char *const name = npc == 0 ? "Seth Able" : "Violet";
    if (state->selection == 5U) {
        set_screen(state, LORD_SCREEN_INN);
        return LORD_EVENT_CONFIRM;
    }
    if (state->friendship_actions == 0U) {
        set_message(state, screen, "No friendship activities remain today.",
                    "Try again after sleeping.");
        return LORD_EVENT_CONFIRM;
    }
    --state->friendship_actions;
    if (state->selection == 0U) {
        set_message(state, screen,
                    npc == 0 ? "Seth shares a new verse with you." :
                               "Violet shares news from the inn.",
                    "A good chat builds friendship.");
    } else if (state->selection == 1U) {
        state->player.charm = add_u16_saturating(state->player.charm, 1U);
        set_message(state, screen, "Your joke earns a delighted laugh!",
                    "Charm rises by one.");
    } else if (state->selection == 2U) {
        const uint8_t hero_roll = (uint8_t)(random_below(state, 6U) + 1U);
        const uint8_t friend_roll = (uint8_t)(random_below(state, 6U) + 1U);
        state->player.friendship_badges = add_u16_saturating(
            state->player.friendship_badges, 1U);
        state->player.high_spirits = true;
        set_message(state, screen,
                    hero_roll >= friend_roll ?
                        "You win a cheerful round of Dragon Dice!" :
                        "Your friend wins the Dragon Dice round!",
                    "Either way, friendship wins.");
    } else if (state->selection == 3U) {
        if (state->player.gems == 0U) {
            set_message(state, screen, "A sparkling gift needs one gem.", "");
        } else {
            --state->player.gems;
            state->player.charm = add_u16_saturating(state->player.charm, 3U);
            set_message(state, screen, "The gem is accepted with delight.",
                        "Charm rises by three.");
        }
    } else if (state->npc_friend == npc) {
        remove_friendship_hp_bonus(state);
        state->npc_friend = -1;
        set_message(state, screen, "You part as best friends.",
                    "The 5 HP bonus rests until a new pact.");
        add_named_log(state, "", " ended a best-friend pact.");
    } else if (state->partner_index >= 0 || state->npc_friend >= 0) {
        set_message(state, screen, "You already have a best-friend pact.", "");
    } else if (state->player.charm < 30U) {
        set_message(state, screen, "The best-friend pact can wait.",
                    "Reach 30 charm and keep being kind.");
    } else {
        state->npc_friend = npc;
        state->player.max_hit_points += 5;
        state->player.hit_points += 5;
        char log_line[LORD_LOG_TEXT_BYTES];
        log_line[0] = '\0';
        text_append(log_line, sizeof(log_line), state->player.name);
        text_append(log_line, sizeof(log_line), " became best friends with ");
        text_append(log_line, sizeof(log_line), name);
        text_append(log_line, sizeof(log_line), "!");
        add_log(state, log_line);
        set_message(state, screen, "A best-friend cheer fills the inn!",
                    "Friendship grants 5 maximum HP.");
    }
    mark_dirty(state);
    return LORD_EVENT_CONFIRM;
}

static uint8_t roll_dragon_die(lord_state_t *state)
{
    return (uint8_t)(random_below(state, 6U) + 1U);
}

static void begin_dragon_dice(lord_state_t *state)
{
    state->dice_player = 0U;
    state->dice_host = 0U;
    state->dice_rolls = 0U;
    state->dice_active = false;
    text_copy(state->battle_line, sizeof(state->battle_line),
              "Closest to 18 wins. Roll, then hold in time.");
    set_screen(state, LORD_SCREEN_DRAGON_DICE);
}

static void start_dragon_dice_round(lord_state_t *state)
{
    state->dice_player = (uint8_t)(roll_dragon_die(state) +
                                   roll_dragon_die(state));
    state->dice_host = 0U;
    state->dice_rolls = 2U;
    state->dice_active = true;
    text_copy(state->battle_line, sizeof(state->battle_line),
              "Safe so far. Roll again or hold against the host.");
    mark_dirty(state);
}

static lord_event_t finish_dragon_dice(lord_state_t *state)
{
    state->dice_host = (uint8_t)(roll_dragon_die(state) +
                                 roll_dragon_die(state));
    for (uint8_t rolls = 2U;
         state->dice_host < 14U && rolls < 8U; ++rolls) {
        state->dice_host = (uint8_t)(state->dice_host +
                                     roll_dragon_die(state));
    }
    if (state->dice_host > 18U ||
        state->dice_player > state->dice_host) {
        /* Eight back on a five-coin wager makes optimal play slightly
         * negative-EV, so the table stays a diversion instead of an infinite
         * money engine. Friendship is rewarded only once per realm day. */
        state->player.gold = add_u32_saturating(state->player.gold, 8U);
        if ((state->igm_used_mask & LORD_DAILY_DICE_FRIENDSHIP_MASK) == 0U) {
            state->player.charm = add_u16_saturating(state->player.charm, 1U);
            state->player.high_spirits = true;
            state->igm_used_mask = (uint8_t)(
                state->igm_used_mask | LORD_DAILY_DICE_FRIENDSHIP_MASK);
            text_copy(state->battle_line, sizeof(state->battle_line),
                      "You win 8 ChompCoin and today's friendship cheer!");
        } else {
            text_copy(state->battle_line, sizeof(state->battle_line),
                      "You win 8 ChompCoin; today's cheer is already earned.");
        }
    } else if (state->dice_player == state->dice_host) {
        state->player.gold = add_u32_saturating(state->player.gold, 5U);
        text_copy(state->battle_line, sizeof(state->battle_line),
                  "A tie! Your 5 ChompCoin returns.");
    } else {
        text_copy(state->battle_line, sizeof(state->battle_line),
                  "The host is closer to 18 and wins the round.");
    }
    state->dice_active = false;
    state->dice_rolls = 0U;
    mark_dirty(state);
    return LORD_EVENT_CONFIRM;
}

static lord_event_t dragon_dice_action(lord_state_t *state)
{
    if (state->selection == 2U) {
        state->dice_active = false;
        state->dice_rolls = 0U;
        set_screen(state, LORD_SCREEN_INN);
        return LORD_EVENT_CONFIRM;
    }
    if (state->selection == 1U) {
        if (!state->dice_active) {
            text_copy(state->battle_line, sizeof(state->battle_line),
                      "Roll first. Highest total at or below 18 wins.");
            return LORD_EVENT_CONFIRM;
        }
        return finish_dragon_dice(state);
    }
    if (!state->dice_active && state->player.gold < 5U) {
        set_message(state, LORD_SCREEN_DRAGON_DICE,
                    "A round costs 5 ChompCoin.",
                    "Try Aragorn's real math challenge first.");
        return LORD_EVENT_CONFIRM;
    }
    if (!state->dice_active) {
        state->player.gold -= 5U;
        if (state->save_available) {
            state->minigame_pending = LORD_MINIGAME_PENDING_DICE;
            state->minigame_save_barrier = true;
            state->dice_player = 0U;
            state->dice_host = 0U;
            state->dice_rolls = 0U;
            text_copy(state->battle_line, sizeof(state->battle_line),
                      "Locking 5 ChompCoin into your saved realm...");
            mark_dirty(state);
            return LORD_EVENT_CONFIRM;
        }
        start_dragon_dice_round(state);
        return LORD_EVENT_CONFIRM;
    }
    state->dice_player = (uint8_t)(state->dice_player +
                                   roll_dragon_die(state));
    if (state->dice_rolls != UINT8_MAX) {
        ++state->dice_rolls;
    }
    if (state->dice_player > 18U) {
        text_copy(state->battle_line, sizeof(state->battle_line),
                  "Bust! Your total passed 18; the wager is lost.");
        state->dice_active = false;
        state->dice_rolls = 0U;
        mark_dirty(state);
        return LORD_EVENT_CONFIRM;
    }
    if (state->dice_player == 18U || state->dice_rolls >= 6U) {
        return finish_dragon_dice(state);
    }
    text_copy(state->battle_line, sizeof(state->battle_line),
              "Safe so far. Roll again or hold against the host.");
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

static uint16_t aragorn_answer(const lord_state_t *state)
{
    if (state->quiz_operator == 0U) {
        return (uint16_t)(state->quiz_left + state->quiz_right);
    }
    if (state->quiz_operator == 1U) {
        return (uint16_t)(state->quiz_left - state->quiz_right);
    }
    return (uint16_t)(state->quiz_left * state->quiz_right);
}

static void generate_aragorn_quiz(lord_state_t *state)
{
    const bool hard = state->quiz_wager == 20U;
    state->quiz_operator = (uint8_t)random_below(state, hard ? 3U : 2U);
    if (state->quiz_operator == 2U) {
        state->quiz_left = (uint16_t)(random_below(state, 8U) + 2U);
        state->quiz_right = (uint16_t)(random_below(state, 8U) + 2U);
    } else {
        const uint32_t span = hard ? 31U : 12U;
        const uint32_t base = hard ? 10U : 1U;
        state->quiz_left = (uint16_t)(random_below(state, span) + base);
        state->quiz_right = (uint16_t)(random_below(state, span) + base);
        if (state->quiz_operator == 1U &&
            state->quiz_right > state->quiz_left) {
            const uint16_t swap = state->quiz_left;
            state->quiz_left = state->quiz_right;
            state->quiz_right = swap;
        }
    }
    const uint16_t answer = aragorn_answer(state);
    state->quiz_correct = (uint8_t)random_below(state, 4U);
    for (size_t index = 0U; index < 4U; ++index) {
        if (index == state->quiz_correct) {
            state->quiz_answers[index] = answer;
            continue;
        }
        int32_t delta = (int32_t)index - (int32_t)state->quiz_correct;
        if (delta >= 0) {
            ++delta;
        }
        int32_t distractor = (int32_t)answer + delta;
        if (distractor < 0) {
            distractor = (int32_t)answer + 4 + (int32_t)index;
        }
        state->quiz_answers[index] = (uint16_t)distractor;
    }
    mark_dirty(state);
}

static lord_event_t begin_aragorn_quiz(lord_state_t *state, uint8_t wager)
{
    state->player.gold -= wager;
    state->quiz_wager = wager;
    mark_igm_used(state);
    set_screen(state, LORD_SCREEN_ARAGORN_QUIZ);
    if (state->save_available) {
        state->minigame_pending = LORD_MINIGAME_PENDING_QUIZ;
        state->minigame_save_barrier = true;
        return LORD_EVENT_CONFIRM;
    }
    generate_aragorn_quiz(state);
    return LORD_EVENT_CONFIRM;
}

static lord_event_t resolve_aragorn_quiz(lord_state_t *state)
{
    const bool correct = state->selection == state->quiz_correct;
    if (correct) {
        state->player.gold = add_u32_saturating(
            state->player.gold, (uint32_t)state->quiz_wager * 2U);
        state->player.high_spirits = true;
        set_message(state, LORD_SCREEN_IGM,
                    "Correct! Aragorn doubles your ChompCoin.",
                    "A solved challenge also raises your spirits.");
    } else {
        set_message(state, LORD_SCREEN_IGM,
                    "Not quite. Aragorn shows the solution.",
                    "Your wager funds tomorrow's challenge.");
    }
    state->quiz_wager = 0U;
    mark_dirty(state);
    return LORD_EVENT_CONFIRM;
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
                        "Solve one real four-choice arithmetic puzzle.",
                        "A correct answer pays twice your wager.");
            return LORD_EVENT_CONFIRM;
        }
        const uint8_t wager = state->selection == 0U ? 5U : 20U;
        if (state->player.gold < wager) {
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "You need more ChompCoin for that challenge.", "");
            return LORD_EVENT_CONFIRM;
        }
        return begin_aragorn_quiz(state, wager);
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
                            "A sack of ChompCoin makes it outside.");
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
                            "A pouch of ChompCoin is awarded.");
            }
        } else if (state->selection == 1U) {
            state->player.forest_fights = add_u16_saturating(
                state->player.forest_fights, 2U);
            state->player.hit_points = state->player.max_hit_points;
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "A cabin offers a perfect night's rest.",
                        "Full health and two forest fights.");
        } else {
            state->player.friendship_badges = add_u16_saturating(
                state->player.friendship_badges, 1U);
            state->player.experience = add_u32_saturating(
                state->player.experience, 1000U);
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "You join a cheerful team challenge.",
                        "Friendship and 1000 XP are your prize.");
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
                            "ChompCoin and two gems fill your hands.");
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
                            "The youth guild needs a larger donation.", "");
                return LORD_EVENT_CONFIRM;
            }
            state->player.gold -= price;
            state->player.young_heroes_helped = add_u16_saturating(
                state->player.young_heroes_helped, 1U);
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "You sponsor a young hero's first supplies.",
                        "The youth guild gives you a helper badge.");
        } else if (state->selection == 1U) {
            if (random_below(state, 2U) == 0U) {
                state->player.young_heroes_helped = add_u16_saturating(
                    state->player.young_heroes_helped, 1U);
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "You guide a lost youngster safely home.",
                            "The youth guild adds a helper badge.");
            } else {
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "The youngster found home by another path.",
                            "You wave and promise to help next time.");
            }
        } else {
            const uint16_t needed = (uint16_t)(
                (uint16_t)state->player.level * state->player.level);
            if (state->player.horse || state->player.young_heroes_helped < needed) {
                set_message(state, LORD_SCREEN_IGM_DETAIL,
                            "The stable reward is not ready yet.",
                            "Help level-squared young heroes first.");
                return LORD_EVENT_CONFIRM;
            }
            state->player.horse = true;
            set_message(state, LORD_SCREEN_IGM_DETAIL,
                        "Olodrin awards you a helper's horse!",
                        "Your kindness now extends every day.");
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
                        "Its ChompCoin is now yours.");
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
    state->player.hero_style = LORD_HERO_STYLE_HERO;
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
    case LORD_SCREEN_HERO_STYLE:
        return 2U;
    case LORD_SCREEN_CLASS:
        return 3U;
    case LORD_SCREEN_TOWN:
        return 16U;
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
    case LORD_SCREEN_FRIENDSHIP:
        return LORD_REALM_PLAYER_COUNT +
            (lord_realm_net_directory_paging_available() ? 3U : 1U);
    case LORD_SCREEN_INN:
        return 11U;
    case LORD_SCREEN_BARTENDER:
        return 6U;
    case LORD_SCREEN_CONVERSE:
        return 3U;
    case LORD_SCREEN_SETH:
    case LORD_SCREEN_VIOLET:
        return 6U;
    case LORD_SCREEN_DRAGON_DICE:
        return 3U;
    case LORD_SCREEN_PLAYER_DETAIL:
        return 5U;
    case LORD_SCREEN_MAILBOX:
        return (size_t)state->mail_count + 2U;
    case LORD_SCREEN_FRIENDSHIP_ACTION:
        return 5U;
    case LORD_SCREEN_GUILD:
        return state->guild_status.supported &&
                state->guild_status.guild_id != 0U ? 8U : 5U;
    case LORD_SCREEN_GUILD_CREATE:
        return LORD_GUILD_NAME_COUNT + 1U;
    case LORD_SCREEN_GUILD_TARGET:
        return LORD_REALM_PLAYER_COUNT +
            (lord_realm_net_directory_paging_available() ? 3U : 1U);
    case LORD_SCREEN_GUILD_STANDINGS:
        return 3U;
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
    case LORD_SCREEN_ARAGORN_QUIZ:
        return 4U;
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
    case LORD_SCREEN_HERO_STYLE:
        state->player.hero_style = state->selection == 0U ?
            LORD_HERO_STYLE_HERO : LORD_HERO_STYLE_HEROINE;
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
        case 14U:
            set_screen(state, LORD_SCREEN_SKILLS);
            break;
        default:
            set_screen(state, LORD_SCREEN_GUILD);
            break;
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_FOREST:
        if (state->selection == 2U) {
            set_screen(state, LORD_SCREEN_HEALER);
        } else if (state->selection == 3U) {
            set_screen(state, LORD_SCREEN_TOWN);
        } else if (state->selection == 4U && state->player.horse) {
            if (state->player.gold < 5U) {
                set_message(state, LORD_SCREEN_FOREST,
                            "DarkCloak games cost 5 ChompCoin.",
                            "Aragorn's math challenge awards more.");
                return LORD_EVENT_CONFIRM;
            }
            state->player.gold -= 5U;
            if (random_below(state, 2U) == 0U) {
                state->player.gold = add_u32_saturating(
                    state->player.gold, 10U);
                set_message(state, LORD_SCREEN_FOREST,
                            "DarkCloak Tavern welcomes your horse.",
                            "A lucky game wins 10 ChompCoin.");
            } else {
                set_message(state, LORD_SCREEN_FOREST,
                            "The DarkCloak fire warms you.",
                            "The house wins 5 ChompCoin.");
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
                        "Healing costs 5 ChompCoin per point.",
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
                        "All carried ChompCoin is deposited.", "");
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
                        "Transfers require 100 vaulted ChompCoin.", "");
        } else {
            state->player.bank -= 100U;
            state->realm[state->selection].gold = add_u32_saturating(
                state->realm[state->selection].gold, 100U);
            state->selected_player = state->selection;
            add_mail(state, state->selected_player, LORD_MAIL_SENDER_HERO,
                     LORD_MAIL_CUSTOM, false,
                     "Thank you for the 100 ChompCoin transfer.");
            set_message(state, LORD_SCREEN_BANK,
                        "The bank transfers 100 ChompCoin.",
                        "The recipient sends thanks.");
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_INN:
        switch (state->selection) {
        case 0U: {
            if (realm_character_bound(state)) {
                set_message(state, LORD_SCREEN_INN,
                            "Your Mac realm grants each new day.",
                            "The next realm hour will refresh adventures.");
            } else if (state->player.forest_fights != 0U) {
                char line[LORD_TEXT_BYTES];
                line[0] = '\0';
                text_append(line, sizeof(line), "Forest adventures left: ");
                text_append_u32(line, sizeof(line),
                                state->player.forest_fights);
                set_message(state, LORD_SCREEN_INN,
                            "Finish today's forest adventures first.", line);
            } else {
                reset_new_day(state);
                set_message(state, LORD_SCREEN_TOWN,
                            "A new day dawns over the realm.",
                            "Your strength has returned.");
            }
            break;
        }
        case 1U: set_screen(state, LORD_SCREEN_BARTENDER); break;
        case 2U: set_screen(state, LORD_SCREEN_CONVERSE); break;
        case 3U: set_screen(state, LORD_SCREEN_SETH); break;
        case 4U: set_screen(state, LORD_SCREEN_VIOLET); break;
        case 5U:
            set_message(state, LORD_SCREEN_INN,
                        "Seth sings of warriors and crimson fire.",
                        "The whole tavern joins the final chorus.");
            break;
        case 6U: begin_dragon_dice(state); break;
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
                            "No resting warrior wants to spar." :
                            "The friendly sparring ring costs 100 ChompCoin.", "");
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
                            "A berry fizz costs 10 ChompCoin.", "");
            } else {
                state->player.gold -= 10U;
                state->player.hit_points += 2;
                if (state->player.hit_points > state->player.max_hit_points) {
                    state->player.hit_points = state->player.max_hit_points;
                }
                mark_dirty(state);
                set_message(state, LORD_SCREEN_BARTENDER,
                            "The bartender pours a berry fizz.",
                            "Two hit points return.");
            }
        } else if (state->selection == 1U) {
            set_message(state, LORD_SCREEN_BARTENDER,
                        "Violet welcomes warriors who help others.",
                        "Kindness and teamwork impress her.");
        } else if (state->selection == 2U) {
            set_message(state, LORD_SCREEN_BARTENDER,
                        "Seth Able knows every song in the realm.",
                        "He enjoys jokes, dice, songs, and gems.");
        } else if (state->selection == 3U) {
            set_message(state, LORD_SCREEN_BARTENDER,
                        "The Red Dragon waits beyond level twelve.",
                        "Face it only once each day.");
        } else if (state->selection == 4U) {
            if (state->friendship_actions == 0U) {
                set_message(state, LORD_SCREEN_BARTENDER,
                            "No friendship games remain today.",
                            "The next new day brings more riddles.");
            } else if (random_below(state, 2U) == 0U) {
                --state->friendship_actions;
                state->player.charm = add_u16_saturating(
                    state->player.charm, 1U);
                set_message(state, LORD_SCREEN_BARTENDER,
                            "You solve the bartender's riddle!",
                            "The cheering crowd adds one charm.");
                mark_dirty(state);
            } else {
                --state->friendship_actions;
                set_message(state, LORD_SCREEN_BARTENDER,
                            "The bartender's riddle stumps you.",
                            "Everyone laughs and shares the answer.");
                mark_dirty(state);
            }
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
        return npc_friendship(state, 0);
    case LORD_SCREEN_VIOLET:
        return npc_friendship(state, 1);
    case LORD_SCREEN_DRAGON_DICE:
        return dragon_dice_action(state);
    case LORD_SCREEN_ARAGORN_QUIZ:
        return resolve_aragorn_quiz(state);
    case LORD_SCREEN_PLAYERS:
    case LORD_SCREEN_FRIENDSHIP:
        if (state->selection >= LORD_REALM_PLAYER_COUNT) {
            set_screen(state, LORD_SCREEN_TOWN);
        } else {
            state->selected_player = state->selection;
            set_screen(state, state->screen == LORD_SCREEN_FRIENDSHIP ?
                       LORD_SCREEN_FRIENDSHIP_ACTION : LORD_SCREEN_PLAYER_DETAIL);
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_PLAYER_DETAIL:
        if (state->selection == 0U) {
            begin_pvp_battle(state, false);
        } else if (state->selection == 1U) {
            begin_editor(state, LORD_EDITOR_MAIL,
                         LORD_SCREEN_PLAYER_DETAIL, "");
        } else if (state->selection == 2U) {
            set_screen(state, LORD_SCREEN_FRIENDSHIP_ACTION);
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
    case LORD_SCREEN_FRIENDSHIP_ACTION:
        return perform_friendship_action(state);
    case LORD_SCREEN_GUILD: {
        const bool member = state->guild_status.supported &&
            state->guild_status.guild_id != 0U;
        if (!member) {
            if (state->selection == 0U) {
                set_screen(state, LORD_SCREEN_GUILD_STANDINGS);
            } else if (state->selection == 1U) {
                state->guild_target = LORD_GUILD_TARGET_JOIN;
                set_screen(state, LORD_SCREEN_GUILD_TARGET);
            } else if (state->selection == 2U) {
                set_screen(state, LORD_SCREEN_GUILD_CREATE);
            } else if (state->selection == 3U) {
                set_message(state, LORD_SCREEN_GUILD,
                            "Charge beats Sneak; Sneak beats Ward.",
                            "Ward beats Charge. A quest takes 12 rallies.");
            } else {
                set_screen(state, LORD_SCREEN_TOWN);
            }
        } else if (state->selection < 3U) {
            set_message(state, LORD_SCREEN_GUILD,
                        "Connect to the Mac hub to rally.",
                        "Club progress is shared, never stored locally.");
        } else if (state->selection == 3U || state->selection == 4U) {
            state->guild_target = state->selection == 3U ?
                LORD_GUILD_TARGET_CLASH : LORD_GUILD_TARGET_CHEER;
            set_screen(state, LORD_SCREEN_GUILD_TARGET);
        } else if (state->selection == 5U) {
            set_screen(state, LORD_SCREEN_GUILD_STANDINGS);
        } else if (state->selection == 6U) {
            set_message(state, LORD_SCREEN_GUILD,
                        "Connect to the Mac hub to leave this club.", "");
        } else {
            set_screen(state, LORD_SCREEN_TOWN);
        }
        return LORD_EVENT_CONFIRM;
    }
    case LORD_SCREEN_GUILD_CREATE:
        if (state->selection >= LORD_GUILD_NAME_COUNT) {
            set_screen(state, LORD_SCREEN_GUILD);
        } else {
            set_message(state, LORD_SCREEN_GUILD_CREATE,
                        "Connect to the Mac hub to found this club.",
                        "Membership is hub-owned and never a local save.");
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_GUILD_TARGET:
        if (state->selection >= LORD_REALM_PLAYER_COUNT) {
            set_screen(state, LORD_SCREEN_GUILD);
        } else {
            state->selected_player = state->selection;
            set_message(state, LORD_SCREEN_GUILD_TARGET,
                        "Connect to the Mac hub for this club action.", "");
        }
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_GUILD_STANDINGS:
        if (state->selection == 2U) {
            set_screen(state, LORD_SCREEN_GUILD);
        }
        return LORD_EVENT_CONFIRM;
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
        if (state->selection == 1U) {
            return battle_action(state, LORD_COMBAT_ACTION_GUARD,
                                 0, "Guard: ", false);
        }
        if (state->selection == 2U) {
            return use_class_technique(state);
        }
        if (state->selection == 3U) {
            const int32_t multiplier =
                state->enemy_intent == LORD_ENEMY_INTENT_GUARD ? 2 : 1;
            return battle_action(state, LORD_COMBAT_ACTION_FEINT,
                                 multiplier, "Feint: ", false);
        }
        if (state->selection == 4U) {
            if (state->battle_kind != LORD_BATTLE_FOREST) {
                text_copy(state->battle_line, sizeof(state->battle_line),
                          "There is no escape from this fight.");
            } else if (state->player.fairy_lore ||
                       state->enemy_intent == LORD_ENEMY_INTENT_GUARD ||
                       random_below(state, 2U) == 0U) {
                state->battle_kind = LORD_BATTLE_NONE;
                set_message(state, LORD_SCREEN_FOREST,
                            "You escape into the undergrowth.", "");
            } else {
                const int32_t received = enemy_reply_damage(
                    state, LORD_COMBAT_ACTION_RUN, false);
                state->player.hit_points -= received;
                if (received == 0) {
                    text_copy(state->battle_line,
                              sizeof(state->battle_line),
                              "Escape fails, but you evade the reply.");
                } else {
                    text_copy(state->battle_line,
                              sizeof(state->battle_line),
                              "Escape fails; foe hits ");
                    text_append_u32(state->battle_line,
                                    sizeof(state->battle_line),
                                    (uint32_t)received);
                    text_append(state->battle_line,
                                sizeof(state->battle_line), ".");
                }
                if (state->player.hit_points <= 0) {
                    apply_local_knockout(state);
                    state->battle_kind = LORD_BATTLE_NONE;
                    set_screen(state, LORD_SCREEN_DEAD);
                    mark_dirty(state);
                    return LORD_EVENT_LOSE;
                }
                choose_enemy_intent(state);
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
        set_message(state, LORD_SCREEN_INN,
                    "You awaken at the inn, fully rested.",
                    "Carried ChompCoin lost; experience falls 10%.");
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_DRAGON_VICTORY:
        set_message(state, LORD_SCREEN_TOWN,
                    "A new legend begins.",
                    "Your dragon deed grants lasting power.");
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_TEXT_EDITOR:
        return activate_editor(state);
    }
    return LORD_EVENT_NONE;
}

lord_event_t lord_cancel(lord_state_t *state)
{
    switch (state->screen) {
    case LORD_SCREEN_NAME:
    case LORD_SCREEN_HERO_STYLE:
        set_screen(state, LORD_SCREEN_TITLE);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_CLASS:
        set_screen(state, LORD_SCREEN_HERO_STYLE);
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
    case LORD_SCREEN_FRIENDSHIP:
    case LORD_SCREEN_IGM:
    case LORD_SCREEN_NEWS:
    case LORD_SCREEN_RANKINGS:
    case LORD_SCREEN_SKILLS:
    case LORD_SCREEN_RIP_GALLERY:
    case LORD_SCREEN_GUILD:
        set_screen(state, LORD_SCREEN_TOWN);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_BANK_TRANSFER:
        set_screen(state, LORD_SCREEN_BANK);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_BARTENDER:
    case LORD_SCREEN_CONVERSE:
    case LORD_SCREEN_SETH:
    case LORD_SCREEN_VIOLET:
        set_screen(state, LORD_SCREEN_INN);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_DRAGON_DICE:
        state->dice_active = false;
        state->dice_rolls = 0U;
        set_screen(state, LORD_SCREEN_INN);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_PLAYER_DETAIL:
        set_screen(state, LORD_SCREEN_PLAYERS);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_MAIL_VIEW:
    case LORD_SCREEN_MAIL_COMPOSE:
        set_screen(state, LORD_SCREEN_MAILBOX);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_FRIENDSHIP_ACTION:
        set_screen(state, LORD_SCREEN_PLAYER_DETAIL);
        return LORD_EVENT_CONFIRM;
    case LORD_SCREEN_GUILD_CREATE:
    case LORD_SCREEN_GUILD_TARGET:
    case LORD_SCREEN_GUILD_STANDINGS:
        set_screen(state, LORD_SCREEN_GUILD);
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
    case LORD_SCREEN_ARAGORN_QUIZ:
        state->quiz_wager = 0U;
        set_screen(state, LORD_SCREEN_IGM);
        return LORD_EVENT_CONFIRM;
    default:
        return LORD_EVENT_NONE;
    }
}

static uint8_t ranking_level(const lord_state_t *state, int8_t slot)
{
    return slot < 0 ? state->player.level : state->realm[(size_t)slot].level;
}

static uint8_t ranking_dragon_kills(const lord_state_t *state, int8_t slot)
{
    return slot < 0 ? state->player.dragon_kills :
                      state->realm[(size_t)slot].dragon_kills;
}

static uint32_t ranking_experience(const lord_state_t *state, int8_t slot)
{
    return slot < 0 ? state->player.experience :
                      state->realm[(size_t)slot].experience;
}

static uint16_t ranking_pvp_wins(const lord_state_t *state, int8_t slot)
{
    return slot < 0 ? state->player.pvp_wins :
                      state->realm[(size_t)slot].pvp_wins;
}

static uint16_t ranking_pvp_losses(const lord_state_t *state, int8_t slot)
{
    return slot < 0 ? state->player.pvp_losses :
                      state->realm[(size_t)slot].pvp_losses;
}

static bool ranking_precedes(const lord_state_t *state,
                             int8_t left, int8_t right)
{
    /* Finishing the main quest is the realm's highest achievement. A reborn
     * dragon slayer must never rank below someone camping at level 12. */
    const uint8_t left_deeds = ranking_dragon_kills(state, left);
    const uint8_t right_deeds = ranking_dragon_kills(state, right);
    if (left_deeds != right_deeds) {
        return left_deeds > right_deeds;
    }
    const uint8_t left_level = ranking_level(state, left);
    const uint8_t right_level = ranking_level(state, right);
    if (left_level != right_level) {
        return left_level > right_level;
    }
    const uint32_t left_experience = ranking_experience(state, left);
    const uint32_t right_experience = ranking_experience(state, right);
    if (left_experience != right_experience) {
        return left_experience > right_experience;
    }
    const uint16_t left_wins = ranking_pvp_wins(state, left);
    const uint16_t right_wins = ranking_pvp_wins(state, right);
    if (left_wins != right_wins) {
        return left_wins > right_wins;
    }
    const uint16_t left_losses = ranking_pvp_losses(state, left);
    const uint16_t right_losses = ranking_pvp_losses(state, right);
    if (left_losses != right_losses) {
        return left_losses < right_losses;
    }
    return left < right;
}

int8_t lord_ranked_slot(const lord_state_t *state, size_t rank)
{
    if (rank >= LORD_RANKING_COUNT) {
        return -2;
    }
    int8_t order[LORD_RANKING_COUNT];
    size_t count = 1U;
    order[0] = -1;
    const bool bound = realm_character_bound(state);
    for (size_t slot = 0U; slot < LORD_REALM_PLAYER_COUNT; ++slot) {
        /* Offline cartridges keep eight seeded rivals. Once a character is
         * realm-bound, zero actor IDs are empty directory-page slots and must
         * never appear as fabricated players in the ranking. */
        if (!bound || realm_actor_bound(state, slot)) {
            order[count++] = (int8_t)slot;
        }
    }
    if (rank >= count) {
        return -2;
    }
    for (size_t index = 1U; index < count; ++index) {
        const int8_t candidate = order[index];
        size_t position = index;
        while (position > 0U &&
               ranking_precedes(state, candidate, order[position - 1U])) {
            order[position] = order[position - 1U];
            --position;
        }
        order[position] = candidate;
    }
    return order[rank];
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

const char *lord_hero_style_name(lord_hero_style_t hero_style)
{
    return hero_style == LORD_HERO_STYLE_HEROINE ? "Heroine" : "Hero";
}

const char *lord_guild_name(uint16_t name_code)
{
    return name_code >= 1U && name_code <= LORD_GUILD_NAME_COUNT ?
        s_guild_names[name_code - 1U] : "No Club";
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
    case LORD_MAIL_TEAM_INVITE: return "About Your Team Invite";
    case LORD_MAIL_TEAM_PLEDGE: return "Adventure Team Formed";
    case LORD_MAIL_CUSTOM: return mail->outgoing ? "Sent Letter" : "Letter";
    case LORD_MAIL_ATTACK: return "Inn Sparring Match";
    case LORD_MAIL_MENTOR: return "Young Hero News";
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
