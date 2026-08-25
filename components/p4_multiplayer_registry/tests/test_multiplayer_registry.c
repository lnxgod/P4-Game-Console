// SPDX-License-Identifier: MIT

#include "p4/multiplayer_registry.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static p4_game_package_info_t package(
    uint32_t launcher_id, const char *id, const char *title)
{
    p4_game_package_info_t result = {
        .launcher_id = launcher_id,
        .optional_capabilities = P4_GAME_CAP_MULTIPLAYER_SESSION,
    };
    assert(strlen(id) < sizeof(result.id));
    assert(strlen(title) < sizeof(result.title));
    memcpy(result.id, id, strlen(id) + 1U);
    memcpy(result.title, title, strlen(title) + 1U);
    for (size_t index = 0U; index < sizeof(result.payload_sha256); ++index) {
        result.payload_sha256[index] = (uint8_t)(launcher_id + index);
    }
    assert(p4_game_multiplayer_profile_default(
        P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
        &result.multiplayer_profile));
    return result;
}

static void test_console_os_registers_yahtzee_and_kid_game(void)
{
    p4_mp_game_registry_t registry;
    p4_mp_game_registry_init(&registry);

    p4_game_package_info_t yahtzee = package(
        UINT32_C(0x50445941), "org.p4console.p4-yahtzee", "YAHTZEE");
    yahtzee.multiplayer_profile.message_bytes = 48U;
    assert(p4_mp_game_registry_register_package(
        &registry, &yahtzee, 2U) == P4_MP_REGISTRATION_ACCEPTED);

    const p4_game_package_info_t kid_game = package(
        UINT32_C(0x4b494431), "org.example.kids-racers", "KID RACERS");
    assert(p4_mp_game_registry_register_package(
        &registry, &kid_game, 2U) == P4_MP_REGISTRATION_ACCEPTED);

    assert(registry.game_count == 2U);
    const p4_mp_registered_game_t *registered =
        p4_mp_game_registry_at(&registry, 0U);
    assert(registered != NULL);
    assert(strcmp(registered->game_id,
                  "org.p4console.p4-yahtzee") == 0);
    assert(strcmp(registered->title, "YAHTZEE") == 0);
    assert(registered->profile.style ==
        P4_GAME_MULTIPLAYER_STYLE_TURN_BASED);
    assert(registered->profile.message_bytes == 48U);
    assert(memcmp(registered->payload_sha256, yahtzee.payload_sha256,
                  sizeof(registered->payload_sha256)) == 0);
    assert(p4_mp_game_registry_find_launcher(
        &registry, kid_game.launcher_id) != NULL);
}

static void test_console_os_rejects_unsafe_registrations(void)
{
    p4_mp_game_registry_t registry;
    p4_mp_game_registry_init(&registry);

    p4_game_package_info_t offline = package(
        1U, "org.example.offline", "OFFLINE");
    offline.optional_capabilities = 0U;
    assert(p4_mp_game_registry_register_package(
        &registry, &offline, 2U) == P4_MP_REGISTRATION_NOT_MULTIPLAYER);

    p4_game_package_info_t invalid = package(
        2U, "org.example.invalid", "INVALID");
    invalid.multiplayer_profile.protocol = 0U;
    assert(p4_mp_game_registry_register_package(
        &registry, &invalid, 2U) == P4_MP_REGISTRATION_INVALID_PROFILE);

    p4_game_package_info_t four_player = package(
        3U, "org.example.four-player", "FOUR PLAYER");
    four_player.multiplayer_profile.min_players = 3U;
    four_player.multiplayer_profile.max_players = 4U;
    assert(p4_mp_game_registry_register_package(
        &registry, &four_player, 2U) ==
        P4_MP_REGISTRATION_UNSUPPORTED_PLAYER_COUNT);

    const p4_game_package_info_t accepted = package(
        4U, "org.example.accepted", "ACCEPTED");
    assert(p4_mp_game_registry_register_package(
        &registry, &accepted, 2U) == P4_MP_REGISTRATION_ACCEPTED);

    const p4_game_package_info_t duplicate_id = package(
        5U, "org.example.accepted", "OTHER");
    assert(p4_mp_game_registry_register_package(
        &registry, &duplicate_id, 2U) == P4_MP_REGISTRATION_DUPLICATE);

    const p4_game_package_info_t duplicate_launcher = package(
        4U, "org.example.other", "OTHER");
    assert(p4_mp_game_registry_register_package(
        &registry, &duplicate_launcher, 2U) ==
        P4_MP_REGISTRATION_DUPLICATE);
    assert(registry.game_count == 1U);
}

int main(void)
{
    test_console_os_registers_yahtzee_and_kid_game();
    test_console_os_rejects_unsafe_registrations();
    puts("p4 multiplayer registry tests passed");
    return 0;
}
