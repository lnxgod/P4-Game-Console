// SPDX-License-Identifier: MIT

#ifndef P4_MULTIPLAYER_REGISTRY_H
#define P4_MULTIPLAYER_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game_package.h"
#include "p4/multiplayer.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_MP_REGISTRY_MAX_GAMES = 16,
};

/**
 * One Console OS-owned registration copied from validated P4G metadata.
 * Cartridge code cannot create or mutate registrations at runtime.
 */
typedef struct {
    uint32_t launcher_id;
    char game_id[P4_MP_GAME_ID_BYTES];
    char title[P4_GAME_PACKAGE_TITLE_BYTES];
    uint8_t payload_sha256[P4_GAME_PACKAGE_SHA256_BYTES];
    p4_game_multiplayer_profile_t profile;
} p4_mp_registered_game_t;

typedef struct {
    p4_mp_registered_game_t games[P4_MP_REGISTRY_MAX_GAMES];
    size_t game_count;
} p4_mp_game_registry_t;

typedef enum {
    P4_MP_REGISTRATION_ACCEPTED = 0,
    P4_MP_REGISTRATION_BAD_ARGUMENT,
    P4_MP_REGISTRATION_NOT_MULTIPLAYER,
    P4_MP_REGISTRATION_INVALID_PROFILE,
    P4_MP_REGISTRATION_UNSUPPORTED_PLAYER_COUNT,
    P4_MP_REGISTRATION_BAD_IDENTITY,
    P4_MP_REGISTRATION_DUPLICATE,
    P4_MP_REGISTRATION_FULL,
} p4_mp_registration_result_t;

void p4_mp_game_registry_init(p4_mp_game_registry_t *registry);

/**
 * Register one already-validated package during an OS catalog scan.
 * runtime_player_capacity is the number of players supported by the current
 * Console OS transport adapter, never a value supplied by cartridge code.
 */
p4_mp_registration_result_t p4_mp_game_registry_register_package(
    p4_mp_game_registry_t *registry,
    const p4_game_package_info_t *package,
    uint8_t runtime_player_capacity);

bool p4_mp_game_package_is_registerable(
    const p4_game_package_info_t *package,
    uint8_t runtime_player_capacity);

const p4_mp_registered_game_t *p4_mp_game_registry_at(
    const p4_mp_game_registry_t *registry, size_t index);

const p4_mp_registered_game_t *p4_mp_game_registry_find_launcher(
    const p4_mp_game_registry_t *registry, uint32_t launcher_id);

/** Native lobby setting: one shared dice accessory belongs to the host.
 * All-zero settings preserve the default (off). Unknown encodings fail closed.
 * The host service is granted only after the accepted settings are validated. */
bool p4_mp_game_supports_dice(const p4_game_package_info_t *package);
bool p4_mp_game_dice_settings_encode(const p4_game_package_info_t *package,
    bool enabled, uint8_t settings[P4_MP_GAME_SETTINGS_BYTES]);
bool p4_mp_game_dice_settings_decode(const p4_game_package_info_t *package,
    const uint8_t settings[P4_MP_GAME_SETTINGS_BYTES], bool *enabled);
bool p4_mp_game_dice_service_allowed(const p4_game_package_info_t *package,
    bool hardware_ready, bool multiplayer, bool host, bool shared_dice);

const char *p4_mp_registration_result_name(
    p4_mp_registration_result_t result);

#ifdef __cplusplus
}
#endif

#endif
