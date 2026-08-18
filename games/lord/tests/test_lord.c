// SPDX-License-Identifier: LicenseRef-LORD-Permission

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lord_internal.h"
#include "p4/game.h"
#include "p4/input.h"

extern const p4_game_descriptor_t p4_lord_game;

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static void enter_town(lord_state_t *state, lord_class_t hero_class)
{
    lord_initialize(state, UINT32_C(0x12345678));
    CHECK(state->screen == LORD_SCREEN_TITLE);
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_CLASS);
    state->selection = (uint8_t)hero_class;
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_MESSAGE);
    CHECK(lord_activate(state) == LORD_EVENT_CONFIRM);
    CHECK(state->screen == LORD_SCREEN_TOWN);
}

static void test_new_player_and_menu_wrap(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_MYSTICAL);
    CHECK(state.player.hero_class == LORD_CLASS_MYSTICAL);
    CHECK(state.player.level == 1U);
    CHECK(state.player.hit_points == 20);
    CHECK(state.player.gold == 500U);
    CHECK(state.player.forest_fights == LORD_FOREST_FIGHTS_PER_DAY);
    CHECK(state.player.skill_uses == LORD_CLASS_SKILLS_PER_DAY);
    CHECK(lord_menu_count(&state) == 13U);
    lord_move_selection(&state, -1);
    CHECK(state.selection == 12U);
    lord_move_selection(&state, 1);
    CHECK(state.selection == 0U);
}

static void test_realm_mail_and_pvp(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_DEATH_KNIGHT);
    CHECK(state.mail_count == 2U);
    CHECK(lord_mail_unread_count(&state) == 2U);

    state.selection = 8U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_MAILBOX);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_MAIL_VIEW);
    CHECK(lord_mail_unread_count(&state) == 1U);
    CHECK(strcmp(lord_mail_sender(&state, 0U), "Seth Able") == 0);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = state.mail_count;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_MAIL_COMPOSE);
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(state.mail_count == 3U);
    CHECK(state.mail[2].kind == LORD_MAIL_REPLY);
    CHECK(state.mail[2].sender == 2U);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_MAILBOX);
    CHECK(lord_cancel(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_TOWN);
    state.selection = 7U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_PLAYERS);
    CHECK(lord_menu_count(&state) == 5U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_PLAYER_DETAIL);
    state.player.strength = 100000;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_BATTLE);
    CHECK(state.battle_kind == LORD_BATTLE_PVP);
    CHECK(state.pvp_fights == LORD_PVP_FIGHTS_PER_DAY - 1U);
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(!state.realm[0].alive);
    CHECK(state.player.pvp_wins == 1U);
    CHECK(state.mail_count == 4U);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_PLAYER_DETAIL;
    state.selected_player = 1U;
    state.pvp_fights = 0U;
    state.selection = 0U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(state.battle_kind == LORD_BATTLE_NONE);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.screen = LORD_SCREEN_INN;
    state.selection = 0U;
    state.igm_used_mask = 7U;
    state.romance_actions = 0U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.realm[0].alive);
    CHECK(state.pvp_fights == LORD_PVP_FIGHTS_PER_DAY);
    CHECK(state.romance_actions == LORD_ROMANCE_ACTIONS_PER_DAY);
    CHECK(state.igm_used_mask == 0U);

    enter_town(&state, LORD_CLASS_DEATH_KNIGHT);
    state.screen = LORD_SCREEN_PLAYER_DETAIL;
    state.selected_player = 0U;
    state.realm[0].strength = 100000;
    state.player.hit_points = 1;
    state.player.strength = 1;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(lord_activate(&state) == LORD_EVENT_LOSE);
    CHECK(state.screen == LORD_SCREEN_DEAD);
    CHECK(state.player.pvp_losses == 1U);
}

static void test_romance_and_igms(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_MYSTICAL);
    state.selection = 9U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_ROMANCE);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_ROMANCE_ACTION);

    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.realm[0].affection == 30U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.realm[0].affection == 60U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.spouse_index == 0);
    CHECK(state.realm[0].married);
    CHECK(state.player.max_hit_points == 25);
    CHECK(state.romance_actions == 0U);
    CHECK(state.mail[state.mail_count - 1U].kind == LORD_MAIL_PROPOSAL);

    enter_town(&state, LORD_CLASS_THIEF);
    state.player.hit_points = 1;
    state.selection = 10U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_IGM);
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.hit_points == state.player.max_hit_points);
    CHECK(state.player.gold == 450U);
    CHECK((state.igm_used_mask & 4U) != 0U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = 2U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.gold == 450U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = 0U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK((state.igm_used_mask & 1U) != 0U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.igm_used_mask == 7U);
}

static void test_rip_gallery_and_save_codec(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_THIEF);
    state.selection = 11U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_RIP_GALLERY);
    state.selection = 4U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_RIP_SCENE);
    CHECK(state.rip_scene == 4U);
    CHECK(strcmp(lord_rip_scene_name(state.rip_scene), "Red Dragon") == 0);
    CHECK(lord_cancel(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_RIP_GALLERY);

    state.screen = LORD_SCREEN_TOWN;
    state.player.gold = 12345U;
    state.player.bank = 67890U;
    state.realm[1].affection = 55U;
    state.mail[0].unread = false;
    state.spouse_index = 1;
    state.realm[1].married = true;
    state.save_sequence = 42U;
    uint8_t encoded[LORD_SAVE_MAX_BYTES];
    uint8_t duplicate[LORD_SAVE_MAX_BYTES];
    const size_t encoded_length = lord_save_encode(
        &state, encoded, sizeof(encoded));
    const size_t duplicate_length = lord_save_encode(
        &state, duplicate, sizeof(duplicate));
    CHECK(encoded_length > 0U);
    CHECK(encoded_length == 302U);
    CHECK(encoded_length < sizeof(encoded));
    CHECK(encoded_length == duplicate_length);
    CHECK(memcmp(encoded, duplicate, encoded_length) == 0);

    lord_state_t restored;
    lord_initialize(&restored, 1U);
    CHECK(lord_save_decode(&restored, encoded, encoded_length));
    CHECK(restored.screen == LORD_SCREEN_TOWN);
    CHECK(restored.player.hero_class == LORD_CLASS_THIEF);
    CHECK(restored.player.gold == 12345U);
    CHECK(restored.player.bank == 67890U);
    CHECK(restored.realm[1].affection == 55U);
    CHECK(restored.spouse_index == 1);
    CHECK(restored.realm[1].married);
    CHECK(!restored.mail[0].unread);
    CHECK(restored.save_sequence == 42U);
    CHECK(!restored.save_dirty);

    encoded[encoded_length - 1U] ^= UINT8_C(0x80);
    CHECK(!lord_save_decode(&restored, encoded, encoded_length));
    encoded[encoded_length - 1U] ^= UINT8_C(0x80);
    CHECK(!lord_save_decode(&restored, encoded, encoded_length - 1U));
    CHECK(lord_save_encode(&state, encoded, 16U) == 0U);
    state.player.level = 0U;
    const size_t invalid_length = lord_save_encode(
        &state, encoded, sizeof(encoded));
    CHECK(invalid_length == 302U);
    CHECK(!lord_save_decode(&restored, encoded, invalid_length));
}

static void test_shop_and_bank(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_DEATH_KNIGHT);
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_WEAPON_SHOP);
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.weapon == 1U);
    CHECK(state.player.gold == 300U);
    CHECK(state.player.strength == 15);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_WEAPON_SHOP);
    CHECK(lord_cancel(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_TOWN);

    state.selection = 5U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_BANK);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.gold == 0U);
    CHECK(state.player.bank == 300U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.gold == 300U);
    CHECK(state.player.bank == 0U);
}

static void test_forest_training_and_death(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_THIEF);
    state.selection = 0U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_FOREST);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_BATTLE);
    CHECK(state.battle_kind == LORD_BATTLE_FOREST);
    CHECK(state.player.forest_fights ==
          LORD_FOREST_FIGHTS_PER_DAY - 1U);
    state.player.strength = 100000;
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
    CHECK(state.player.experience > 0U);

    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(lord_cancel(&state) == LORD_EVENT_CONFIRM);
    state.selection = 4U;
    state.player.experience = 100U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_TRAINING);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.screen == LORD_SCREEN_BATTLE);
    state.player.strength = 100000;
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(state.player.level == 2U);
    CHECK(state.player.max_hit_points == 30);

    lord_initialize(&state, UINT32_C(0x87654321));
    state.player = (lord_player_t){
        .hero_class = LORD_CLASS_DEATH_KNIGHT,
        .level = 1U,
        .hit_points = 1,
        .max_hit_points = 20,
        .strength = 1,
        .forest_fights = 1U,
    };
    state.screen = LORD_SCREEN_BATTLE;
    state.battle_kind = LORD_BATTLE_FOREST;
    state.enemy = (lord_enemy_t){
        .hit_points = 100,
        .max_hit_points = 100,
        .strength = 100000,
    };
    CHECK(lord_activate(&state) == LORD_EVENT_LOSE);
    CHECK(state.screen == LORD_SCREEN_DEAD);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.gold == 0U);
    CHECK(state.player.forest_fights == 0U);
    CHECK(state.screen == LORD_SCREEN_MESSAGE);
}

static void test_dragon_victory_rebirth(void)
{
    lord_state_t state;
    enter_town(&state, LORD_CLASS_DEATH_KNIGHT);
    state.player.level = LORD_MAX_LEVEL;
    state.player.strength = 100000;
    state.player.hit_points = 5000;
    state.player.max_hit_points = 5000;
    state.player.forest_fights = 1U;
    state.screen = LORD_SCREEN_FOREST;
    state.selection = 1U;
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.battle_kind == LORD_BATTLE_DRAGON);
    CHECK(lord_activate(&state) == LORD_EVENT_WIN);
    CHECK(state.screen == LORD_SCREEN_DRAGON_VICTORY);
    CHECK(state.player.dragon_kills == 1U);
    CHECK(lord_activate(&state) == LORD_EVENT_CONFIRM);
    CHECK(state.player.level == 1U);
    CHECK(state.player.dragon_kills == 1U);
    CHECK(state.player.max_hit_points == 25);
    CHECK(state.player.strength == 12);
}

static bool start_game(p4_game_instance_t *instance, lord_state_t *state)
{
    static const p4_game_services_t services = {
        .available_capabilities =
            P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(instance, &p4_lord_game, &services,
                                  state, sizeof(*state));
}

static void test_runtime_render_bounds_and_exit(void)
{
    enum {
        GUARD = 17,
        STRIDE = P4_GAME_SURFACE_WIDTH + 7,
        WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
        TOTAL = GUARD + WORDS + GUARD,
    };
    uint16_t *const allocation = calloc(TOTAL, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation == NULL) {
        return;
    }
    for (size_t index = 0U; index < TOTAL; ++index) {
        allocation[index] = UINT16_C(0x5aa5);
    }
    p4_game_surface_t surface = {
        .pixels = allocation + GUARD,
        .stride_pixels = STRIDE,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    p4_game_instance_t instance;
    lord_state_t state;
    CHECK(start_game(&instance, &state));
    CHECK(state.screen == LORD_SCREEN_TITLE);
    for (int screen = LORD_SCREEN_TITLE; screen <= LORD_SCREEN_RIP_SCENE;
         ++screen) {
        state.screen = (lord_screen_t)screen;
        state.selection = 0U;
        state.menu_scroll = 0U;
        CHECK(p4_game_instance_render(&instance, &surface));
    }
    for (size_t index = 0U; index < GUARD; ++index) {
        CHECK(allocation[index] == UINT16_C(0x5aa5));
        CHECK(allocation[GUARD + WORDS + index] == UINT16_C(0x5aa5));
    }
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_GAME_SURFACE_WIDTH;
             column < STRIDE; ++column) {
            CHECK(surface.pixels[row * STRIDE + column] ==
                  UINT16_C(0x5aa5));
        }
    }
    const p4_game_input_t back = {
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    free(allocation);
}

static p4_physical_touch_t physical_touch_for(uint16_t logical_x,
                                              uint16_t logical_y)
{
    return (p4_physical_touch_t){
        .x = (uint16_t)(P4_INPUT_VIEWPORT_LEFT +
            ((uint32_t)logical_x * P4_INPUT_VIEWPORT_WIDTH) /
                P4_GAME_SURFACE_WIDTH + 1U),
        .y = (uint16_t)(P4_INPUT_VIEWPORT_TOP +
            ((uint32_t)logical_y * P4_INPUT_VIEWPORT_HEIGHT) /
                P4_GAME_SURFACE_HEIGHT + 1U),
    };
}

static void test_standard_touch_lifecycle(void)
{
    p4_game_instance_t instance;
    lord_state_t state;
    p4_game_input_mapper_t mapper;
    p4_game_input_t input;
    CHECK(start_game(&instance, &state));
    p4_game_input_mapper_init(&mapper);

    p4_physical_touch_t touch = physical_touch_for(286U, 158U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK((input.pressed & P4_BUTTON_A) != 0U);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_CLASS);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);
    CHECK((input.released & P4_BUTTON_A) != 0U);

    touch = physical_touch_for(240U, 176U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK((input.pressed & P4_BUTTON_B) != 0U);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.screen == LORD_SCREEN_TITLE);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);

    touch = physical_touch_for(26U, 12U);
    p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
    CHECK((input.pressed & P4_BUTTON_BACK) != 0U);
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

int main(void)
{
    CHECK(p4_game_descriptor_valid(&p4_lord_game));
    CHECK(p4_lord_game.launcher_id == 112U);
    CHECK(p4_lord_game.state_bytes == sizeof(lord_state_t));
    CHECK(sizeof(lord_state_t) <= P4_GAME_MAX_STATE_BYTES);
    test_new_player_and_menu_wrap();
    test_shop_and_bank();
    test_forest_training_and_death();
    test_dragon_victory_rebirth();
    test_realm_mail_and_pvp();
    test_romance_and_igms();
    test_rip_gallery_and_save_codec();
    test_runtime_render_bounds_and_exit();
    test_standard_touch_lifecycle();
    if (s_failures != 0) {
        fprintf(stderr, "%d LORD test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("LORD tests passed");
    return EXIT_SUCCESS;
}
