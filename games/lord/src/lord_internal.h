// SPDX-License-Identifier: LicenseRef-LORD-Permission

#ifndef P4_LORD_INTERNAL_H
#define P4_LORD_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    LORD_TEXT_BYTES = 56,
    LORD_NAME_BYTES = 20,
    LORD_REALM_NAME_BYTES = 24,
    LORD_SAYING_BYTES = 40,
    LORD_MAIL_BODY_BYTES = 48,
    LORD_LOG_TEXT_BYTES = 52,
    LORD_REALM_PLAYER_COUNT = 8,
    LORD_MAIL_COUNT_MAX = 12,
    LORD_LOG_COUNT_MAX = 12,
    LORD_FOREST_FIGHTS_PER_DAY = 15,
    LORD_PVP_FIGHTS_PER_DAY = 3,
    LORD_ROMANCE_ACTIONS_PER_DAY = 3,
    LORD_IGM_COUNT = 7,
    LORD_RIP_SCENE_COUNT = 12,
    LORD_MAX_LEVEL = 12,
    LORD_SKILL_COUNT = 3,
    LORD_SKILL_MASTERY_MAX = 40,
    LORD_SAVE_FORMAT_VERSION = 3,
    LORD_SAVE_MAX_BYTES = 4096,
    LORD_KEYBOARD_COUNT = 44,
};

typedef enum {
    LORD_SCREEN_TITLE = 0,
    LORD_SCREEN_NAME,
    LORD_SCREEN_SEX,
    LORD_SCREEN_CLASS,
    LORD_SCREEN_TOWN,
    LORD_SCREEN_FOREST,
    LORD_SCREEN_WEAPON_SHOP,
    LORD_SCREEN_ARMOR_SHOP,
    LORD_SCREEN_HEALER,
    LORD_SCREEN_TRAINING,
    LORD_SCREEN_BANK,
    LORD_SCREEN_BANK_TRANSFER,
    LORD_SCREEN_INN,
    LORD_SCREEN_BARTENDER,
    LORD_SCREEN_CONVERSE,
    LORD_SCREEN_SETH,
    LORD_SCREEN_VIOLET,
    LORD_SCREEN_BLACKJACK,
    LORD_SCREEN_PLAYERS,
    LORD_SCREEN_PLAYER_DETAIL,
    LORD_SCREEN_MAILBOX,
    LORD_SCREEN_MAIL_VIEW,
    LORD_SCREEN_MAIL_COMPOSE,
    LORD_SCREEN_ROMANCE,
    LORD_SCREEN_ROMANCE_ACTION,
    LORD_SCREEN_IGM,
    LORD_SCREEN_IGM_DETAIL,
    LORD_SCREEN_NEWS,
    LORD_SCREEN_RANKINGS,
    LORD_SCREEN_SKILLS,
    LORD_SCREEN_STATS,
    LORD_SCREEN_BATTLE,
    LORD_SCREEN_MESSAGE,
    LORD_SCREEN_DEAD,
    LORD_SCREEN_DRAGON_VICTORY,
    LORD_SCREEN_RIP_GALLERY,
    LORD_SCREEN_RIP_SCENE,
    LORD_SCREEN_TEXT_EDITOR,
} lord_screen_t;

typedef enum {
    LORD_CLASS_DEATH_KNIGHT = 0,
    LORD_CLASS_MYSTICAL,
    LORD_CLASS_THIEF,
} lord_class_t;

typedef enum {
    LORD_SEX_MALE = 0,
    LORD_SEX_FEMALE,
} lord_sex_t;

typedef enum {
    LORD_BATTLE_NONE = 0,
    LORD_BATTLE_FOREST,
    LORD_BATTLE_TRAINER,
    LORD_BATTLE_DRAGON,
    LORD_BATTLE_PVP,
    LORD_BATTLE_INN,
} lord_battle_kind_t;

typedef enum {
    LORD_MAIL_WELCOME = 0,
    LORD_MAIL_TRAINER,
    LORD_MAIL_REPLY,
    LORD_MAIL_PVP_VICTORY,
    LORD_MAIL_ROMANCE,
    LORD_MAIL_PROPOSAL,
    LORD_MAIL_CUSTOM,
    LORD_MAIL_ATTACK,
    LORD_MAIL_BABY,
    LORD_MAIL_ANNOUNCEMENT,
} lord_mail_kind_t;

typedef enum {
    LORD_EDITOR_NAME = 0,
    LORD_EDITOR_MAIL,
    LORD_EDITOR_ANNOUNCEMENT,
    LORD_EDITOR_SAYING,
    LORD_EDITOR_CONVERSATION,
} lord_editor_target_t;

typedef enum {
    LORD_EVENT_NONE = 0,
    LORD_EVENT_MOVE,
    LORD_EVENT_CONFIRM,
    LORD_EVENT_HIT,
    LORD_EVENT_WIN,
    LORD_EVENT_LOSE,
} lord_event_t;

typedef struct {
    const char *name;
    uint32_t price;
    int32_t bonus;
} lord_item_t;

typedef struct {
    const char *name;
    int32_t strength;
    int32_t hit_points;
    int32_t hp_gained;
    int32_t strength_gained;
    int32_t defense_gained;
    uint32_t experience_needed;
} lord_trainer_t;

typedef struct {
    const char *name;
    const char *weapon;
    const char *death;
    int32_t strength;
    int32_t hit_points;
    uint32_t gold;
    uint32_t experience;
} lord_monster_t;

typedef struct {
    char name[LORD_NAME_BYTES];
    lord_sex_t sex;
    lord_class_t hero_class;
    uint8_t level;
    uint8_t weapon;
    uint8_t armor;
    int32_t hit_points;
    int32_t max_hit_points;
    int32_t strength;
    int32_t defense;
    uint32_t gold;
    uint32_t bank;
    uint32_t experience;
    uint16_t forest_fights;
    uint8_t skill[LORD_SKILL_COUNT];
    uint8_t skill_uses[LORD_SKILL_COUNT];
    uint8_t dragon_kills;
    uint16_t day;
    uint16_t pvp_wins;
    uint16_t pvp_losses;
    uint16_t charm;
    uint16_t gems;
    uint16_t children;
    uint16_t laid;
    bool horse;
    bool fairy;
    bool fairy_lore;
    bool amulet;
    bool high_spirits;
    bool seen_dragon;
} lord_player_t;

typedef struct {
    char name[28];
    char weapon[28];
    const char *death_text;
    int32_t hit_points;
    int32_t max_hit_points;
    int32_t strength;
    int32_t defense;
    uint32_t gold;
    uint32_t experience;
} lord_enemy_t;

typedef struct {
    char name[LORD_REALM_NAME_BYTES];
    char saying[LORD_SAYING_BYTES];
    lord_sex_t sex;
    lord_class_t hero_class;
    uint8_t level;
    bool alive;
    bool at_inn;
    bool married;
    uint8_t affection;
    int32_t hit_points;
    int32_t max_hit_points;
    int32_t strength;
    int32_t defense;
    uint32_t gold;
    uint32_t experience;
    uint16_t pvp_wins;
    uint16_t pvp_losses;
} lord_realm_player_t;

typedef struct {
    uint8_t sender;
    uint8_t recipient;
    lord_mail_kind_t kind;
    bool unread;
    bool outgoing;
    char body[LORD_MAIL_BODY_BYTES];
} lord_mail_t;

typedef struct {
    uint16_t day;
    char text[LORD_LOG_TEXT_BYTES];
} lord_log_entry_t;

typedef struct {
    lord_screen_t screen;
    lord_screen_t return_screen;
    lord_screen_t editor_return_screen;
    lord_battle_kind_t battle_kind;
    lord_editor_target_t editor_target;
    uint8_t selection;
    uint8_t menu_scroll;
    uint32_t rng_state;
    uint32_t held_buttons;
    uint32_t realm_revision;
    uint32_t save_sequence;
    uint32_t host_save_sequence;
    uint32_t save_ticket;
    uint32_t save_queued_generation;
    int8_t spouse_index;
    int8_t npc_spouse;
    uint8_t selected_player;
    uint8_t selected_mail;
    uint8_t selected_igm;
    uint8_t pvp_fights;
    uint8_t romance_actions;
    uint8_t igm_used_mask;
    uint8_t rip_scene;
    uint8_t mail_count;
    uint8_t log_count;
    uint8_t blackjack_player;
    uint8_t blackjack_dealer;
    uint16_t blackjack_wager;
    bool blackjack_active;
    bool save_dirty;
    bool save_available;
    bool save_error;
    lord_player_t player;
    lord_enemy_t enemy;
    lord_realm_player_t realm[LORD_REALM_PLAYER_COUNT];
    lord_mail_t mail[LORD_MAIL_COUNT_MAX];
    lord_log_entry_t log[LORD_LOG_COUNT_MAX];
    char editor_text[LORD_MAIL_BODY_BYTES];
    char conversation[LORD_MAIL_BODY_BYTES];
    char announcement[LORD_MAIL_BODY_BYTES];
    char message_line_1[LORD_TEXT_BYTES];
    char message_line_2[LORD_TEXT_BYTES];
    char battle_line[LORD_TEXT_BYTES];
} lord_state_t;

void lord_initialize(lord_state_t *state, uint32_t seed);
size_t lord_menu_count(const lord_state_t *state);
void lord_move_selection(lord_state_t *state, int direction);
lord_event_t lord_activate(lord_state_t *state);
lord_event_t lord_cancel(lord_state_t *state);

const char *lord_class_name(lord_class_t hero_class);
const char *lord_sex_name(lord_sex_t sex);
const lord_item_t *lord_weapon(size_t index);
const lord_item_t *lord_armor(size_t index);
const lord_trainer_t *lord_current_trainer(const lord_state_t *state);
const char *lord_mail_sender(const lord_state_t *state, size_t index);
const char *lord_mail_subject(const lord_state_t *state, size_t index);
const char *lord_mail_body_1(const lord_state_t *state, size_t index);
const char *lord_mail_body_2(const lord_state_t *state, size_t index);
size_t lord_mail_unread_count(const lord_state_t *state);
const char *lord_rip_scene_name(size_t index);
const char *lord_igm_name(size_t index);
const char *lord_keyboard_label(size_t index);

size_t lord_save_encode(const lord_state_t *state, uint8_t *bytes,
                        size_t capacity);
bool lord_save_decode(lord_state_t *state, const uint8_t *bytes,
                      size_t length);

#endif
