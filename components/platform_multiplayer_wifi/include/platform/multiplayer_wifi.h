// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define P4_MP_WIFI_ROUTE_PREFIX UINT64_C(0x5746000000000000)
enum { PLATFORM_MULTIPLAYER_WIFI_MAX_LOBBIES = 4 };
typedef struct {
    uint64_t lobby_id;
    uint32_t session_id;
    uint16_t game_token;
    int8_t rssi;
    uint8_t players_present, player_capacity;
} platform_multiplayer_wifi_lobby_t;
typedef void (*platform_multiplayer_wifi_handler_t)(void *, uint64_t, const uint8_t *, size_t);
typedef struct {
    bool enabled, available, ready, starting, connected;
    uint64_t route_id;
    uint32_t rx_frames, tx_frames;
    esp_err_t last_error;
} platform_multiplayer_wifi_status_t;
/* All connection operations are asynchronous. No credentials or router needed.
 * Host creates an isolated, open local AP; only P4MP UDP is served (no internet,
 * file sharing or remote administration). Games consume the existing P4 API. */
esp_err_t platform_multiplayer_wifi_enable(platform_multiplayer_wifi_handler_t, void *);
void platform_multiplayer_wifi_disable(void);
esp_err_t platform_multiplayer_wifi_browse(void);
/* Filter discovery before applying the bounded lobby cache. Zero browses all
 * games; the token remains only a hint until P4MP verifies package identity. */
esp_err_t platform_multiplayer_wifi_browse_game(uint16_t game_token);
esp_err_t platform_multiplayer_wifi_host(uint32_t session, uint16_t game);
size_t platform_multiplayer_wifi_list_lobbies(platform_multiplayer_wifi_lobby_t *, size_t);
esp_err_t platform_multiplayer_wifi_join(uint64_t lobby);
esp_err_t platform_multiplayer_wifi_set_handler(platform_multiplayer_wifi_handler_t, void *);
void platform_multiplayer_wifi_poll(void);
/* The foreground poll owner may query whether its most recent poll observed
 * EAGAIN/EWOULDBLOCK. False means budget exhausted, error, uninitialized or
 * link state changed. True is an observation, not a promise about new arrivals.
 * Polls must not run concurrently or recursively through receive callbacks. */
bool platform_multiplayer_wifi_poll_drained(void);
esp_err_t platform_multiplayer_wifi_send(const uint8_t *, size_t);
/* A nonzero route targets one admitted peer; zero sends to every peer. */
esp_err_t platform_multiplayer_wifi_send_to(uint64_t route,const uint8_t *,size_t);
void platform_multiplayer_wifi_reset_route(void);
platform_multiplayer_wifi_status_t platform_multiplayer_wifi_status(void);
bool platform_multiplayer_wifi_route_connected(uint64_t route);
