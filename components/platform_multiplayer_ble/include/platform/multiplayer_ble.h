// SPDX-License-Identifier: MIT

#ifndef PLATFORM_MULTIPLAYER_BLE_H
#define PLATFORM_MULTIPLAYER_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLATFORM_MULTIPLAYER_BLE_OFF = 0,
    PLATFORM_MULTIPLAYER_BLE_STARTING_RADIO,
    PLATFORM_MULTIPLAYER_BLE_STARTING_HOST,
    PLATFORM_MULTIPLAYER_BLE_STANDBY,
    PLATFORM_MULTIPLAYER_BLE_DISCOVERING,
    PLATFORM_MULTIPLAYER_BLE_CONNECTING,
    PLATFORM_MULTIPLAYER_BLE_SECURING,
    PLATFORM_MULTIPLAYER_BLE_DISCOVERING_GATT,
    PLATFORM_MULTIPLAYER_BLE_READY,
    PLATFORM_MULTIPLAYER_BLE_ERROR,
} platform_multiplayer_ble_state_t;

typedef enum {
    PLATFORM_MULTIPLAYER_BLE_LOBBY_IDLE = 0,
    PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER,
    PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST,
    PLATFORM_MULTIPLAYER_BLE_LOBBY_CLIENT,
} platform_multiplayer_ble_lobby_mode_t;

enum {
    PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES = 4,
};

typedef struct {
    /** Opaque identity used only to select this scan result. */
    uint64_t lobby_id;
    uint32_t session_id;
    uint16_t game_token;
    int8_t rssi;
    uint8_t players_present;
    uint8_t player_capacity;
} platform_multiplayer_ble_lobby_t;

typedef void (*platform_multiplayer_ble_frame_handler_t)(
    void *context,
    uint64_t route_id,
    const uint8_t *datagram,
    size_t datagram_length);

typedef struct {
    platform_multiplayer_ble_state_t state;
    platform_multiplayer_ble_lobby_mode_t lobby_mode;
    bool enabled;
    bool host_ready;
    bool connected;
    bool encrypted;
    bool central;
    bool subscribed;
    bool ready;
    uint32_t lobby_session_id;
    uint16_t lobby_game_token;
    uint8_t discovered_lobbies;
    uint64_t route_id;
    uint16_t att_mtu;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t dropped_frames;
    int last_ble_error;
} platform_multiplayer_ble_status_t;

/** Register the local P4 game service without starting radio or NimBLE. */
esp_err_t platform_multiplayer_ble_prepare(void);

/**
 * Lazily start the P4-to-C6 Hosted link and encrypted NimBLE game service.
 * No radio work occurs during Console OS boot; the lobby calls this only when
 * the player explicitly selects BLE.
 */
esp_err_t platform_multiplayer_ble_enable(
    platform_multiplayer_ble_frame_handler_t handler,
    void *handler_context);

/**
 * Choose the pre-connection role. Browsers scan without advertising. Hosts
 * advertise one open room and periodically perform a bounded collision scan
 * so two independently created rooms can deterministically converge. Idle
 * mode does neither.
 * session_id is required only for host mode. game_token is required for host
 * mode. A browser may pass zero to discover every advertised P4 game and use
 * each returned lobby's game_token to resolve a locally installed title, or
 * pass a nonzero token to filter discovery to one compatible game.
 */
esp_err_t platform_multiplayer_ble_set_lobby_mode(
    platform_multiplayer_ble_lobby_mode_t mode,
    uint32_t session_id,
    uint16_t game_token);

/**
 * Snapshot the bounded, strongest-first compatible host list. Browser scans
 * populate it normally; a host collision scan may also populate it briefly.
 */
size_t platform_multiplayer_ble_list_lobbies(
    platform_multiplayer_ble_lobby_t *lobbies,
    size_t lobby_capacity);

/** Connect only to the explicitly selected scan result. */
esp_err_t platform_multiplayer_ble_join_lobby(uint64_t lobby_id);

/** Rebind frame delivery during an OS-owned foreground-game handoff. */
esp_err_t platform_multiplayer_ble_set_handler(
    platform_multiplayer_ble_frame_handler_t handler,
    void *handler_context);

/** Stop advertising/scanning and disconnect, while retaining initialized HCI. */
void platform_multiplayer_ble_disable(void);

/** Deliver queued, CRC-checked P4MP datagrams on the caller's task. */
void platform_multiplayer_ble_poll(void);

/** Fragment and send one complete P4MP datagram over the active GATT link. */
esp_err_t platform_multiplayer_ble_send(
    const uint8_t *datagram,
    size_t datagram_length);

/** Drop queued protocol input without tearing down the encrypted BLE link. */
void platform_multiplayer_ble_reset_route(void);

platform_multiplayer_ble_status_t platform_multiplayer_ble_status(void);
bool platform_multiplayer_ble_route_connected(uint64_t route_id);
const char *platform_multiplayer_ble_route_name(uint64_t route_id);

#ifdef __cplusplus
}
#endif

#endif
