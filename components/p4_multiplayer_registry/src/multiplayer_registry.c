// SPDX-License-Identifier: MIT

#include "p4/multiplayer_registry.h"

#include <string.h>

_Static_assert((size_t)P4_GAME_PACKAGE_SHA256_BYTES ==
                   (size_t)P4_MP_SHA256_BYTES,
               "package and lobby digests must have the same width");

static bool bounded_text_length(
    const char *text, size_t capacity, size_t *length_out)
{
    if (text == NULL || capacity == 0U || length_out == NULL) {
        return false;
    }
    for (size_t index = 0U; index < capacity; ++index) {
        if (text[index] == '\0') {
            *length_out = index;
            return index > 0U;
        }
    }
    return false;
}

static p4_mp_registration_result_t make_registration(
    const p4_game_package_info_t *package,
    uint8_t runtime_player_capacity,
    p4_mp_registered_game_t *registration)
{
    if (package == NULL || registration == NULL ||
        runtime_player_capacity < 2U ||
        runtime_player_capacity > P4_GAME_MULTIPLAYER_MAX_PLAYERS) {
        return P4_MP_REGISTRATION_BAD_ARGUMENT;
    }
    const uint32_t capabilities = package->required_capabilities |
        package->optional_capabilities;
    if ((capabilities & P4_GAME_CAP_MULTIPLAYER_SESSION) == 0U) {
        return P4_MP_REGISTRATION_NOT_MULTIPLAYER;
    }
    if (!p4_game_multiplayer_profile_valid(
            &package->multiplayer_profile)) {
        return P4_MP_REGISTRATION_INVALID_PROFILE;
    }
    if (package->multiplayer_profile.min_players >
        runtime_player_capacity) {
        return P4_MP_REGISTRATION_UNSUPPORTED_PLAYER_COUNT;
    }

    size_t game_id_length = 0U;
    size_t title_length = 0U;
    if (package->launcher_id == 0U ||
        !bounded_text_length(package->id, sizeof(package->id),
                             &game_id_length) ||
        game_id_length >= P4_MP_GAME_ID_BYTES ||
        !bounded_text_length(package->title, sizeof(package->title),
                             &title_length) ||
        title_length >= sizeof(registration->title)) {
        return P4_MP_REGISTRATION_BAD_IDENTITY;
    }

    memset(registration, 0, sizeof(*registration));
    registration->launcher_id = package->launcher_id;
    memcpy(registration->game_id, package->id, game_id_length + 1U);
    memcpy(registration->title, package->title, title_length + 1U);
    memcpy(registration->payload_sha256, package->payload_sha256,
           sizeof(registration->payload_sha256));
    registration->profile = package->multiplayer_profile;
    return P4_MP_REGISTRATION_ACCEPTED;
}

void p4_mp_game_registry_init(p4_mp_game_registry_t *registry)
{
    if (registry != NULL) {
        memset(registry, 0, sizeof(*registry));
    }
}

p4_mp_registration_result_t p4_mp_game_registry_register_package(
    p4_mp_game_registry_t *registry,
    const p4_game_package_info_t *package,
    uint8_t runtime_player_capacity)
{
    if (registry == NULL) {
        return P4_MP_REGISTRATION_BAD_ARGUMENT;
    }
    if (registry->game_count > P4_MP_REGISTRY_MAX_GAMES) {
        return P4_MP_REGISTRATION_FULL;
    }
    p4_mp_registered_game_t registration;
    const p4_mp_registration_result_t result = make_registration(
        package, runtime_player_capacity, &registration);
    if (result != P4_MP_REGISTRATION_ACCEPTED) {
        return result;
    }
    for (size_t index = 0U; index < registry->game_count; ++index) {
        if (registry->games[index].launcher_id ==
                registration.launcher_id ||
            strcmp(registry->games[index].game_id,
                   registration.game_id) == 0) {
            return P4_MP_REGISTRATION_DUPLICATE;
        }
    }
    if (registry->game_count >= P4_MP_REGISTRY_MAX_GAMES) {
        return P4_MP_REGISTRATION_FULL;
    }
    registry->games[registry->game_count] = registration;
    ++registry->game_count;
    return P4_MP_REGISTRATION_ACCEPTED;
}

bool p4_mp_game_package_is_registerable(
    const p4_game_package_info_t *package,
    uint8_t runtime_player_capacity)
{
    p4_mp_registered_game_t ignored;
    return make_registration(package, runtime_player_capacity, &ignored) ==
        P4_MP_REGISTRATION_ACCEPTED;
}

const p4_mp_registered_game_t *p4_mp_game_registry_at(
    const p4_mp_game_registry_t *registry, size_t index)
{
    return registry != NULL && index < registry->game_count
        ? &registry->games[index] : NULL;
}

const p4_mp_registered_game_t *p4_mp_game_registry_find_launcher(
    const p4_mp_game_registry_t *registry, uint32_t launcher_id)
{
    if (registry == NULL || launcher_id == 0U) {
        return NULL;
    }
    for (size_t index = 0U; index < registry->game_count; ++index) {
        if (registry->games[index].launcher_id == launcher_id) {
            return &registry->games[index];
        }
    }
    return NULL;
}

const char *p4_mp_registration_result_name(
    p4_mp_registration_result_t result)
{
    switch (result) {
    case P4_MP_REGISTRATION_ACCEPTED:
        return "accepted";
    case P4_MP_REGISTRATION_BAD_ARGUMENT:
        return "bad-argument";
    case P4_MP_REGISTRATION_NOT_MULTIPLAYER:
        return "not-multiplayer";
    case P4_MP_REGISTRATION_INVALID_PROFILE:
        return "invalid-profile";
    case P4_MP_REGISTRATION_UNSUPPORTED_PLAYER_COUNT:
        return "unsupported-player-count";
    case P4_MP_REGISTRATION_BAD_IDENTITY:
        return "bad-identity";
    case P4_MP_REGISTRATION_DUPLICATE:
        return "duplicate";
    case P4_MP_REGISTRATION_FULL:
        return "full";
    default:
        return "unknown";
    }
}

bool p4_mp_game_supports_dice(const p4_game_package_info_t *package)
{
    return package != NULL && ((package->required_capabilities |
        package->optional_capabilities) & P4_GAME_CAP_DICE_ACCESSORY) != 0U;
}

bool p4_mp_game_dice_settings_encode(const p4_game_package_info_t *package,
    bool enabled, uint8_t settings[P4_MP_GAME_SETTINGS_BYTES])
{
    if (package == NULL || settings == NULL ||
        (enabled && !p4_mp_game_supports_dice(package))) return false;
    memset(settings, 0, P4_MP_GAME_SETTINGS_BYTES);
    if (enabled) { settings[0] = 1U; settings[1] = 1U; }
    return true;
}

bool p4_mp_game_dice_settings_decode(const p4_game_package_info_t *package,
    const uint8_t settings[P4_MP_GAME_SETTINGS_BYTES], bool *enabled)
{
    if (package == NULL || settings == NULL || enabled == NULL) return false;
    *enabled = false;
    uint8_t combined = 0U;
    for (size_t i = 0U; i < P4_MP_GAME_SETTINGS_BYTES; ++i) combined |= settings[i];
    if (combined == 0U) return true;
    if (settings[0] != 1U || settings[1] != 1U ||
        !p4_mp_game_supports_dice(package)) return false;
    for (size_t i = 2U; i < P4_MP_GAME_SETTINGS_BYTES; ++i)
        if (settings[i] != 0U) return false;
    *enabled = true;
    return true;
}

bool p4_mp_game_dice_service_allowed(const p4_game_package_info_t *package,
    bool hardware_ready, bool multiplayer, bool host, bool shared_dice)
{
    return hardware_ready && p4_mp_game_supports_dice(package) &&
        (!multiplayer || (host && shared_dice));
}
