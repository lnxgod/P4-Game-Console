// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Versioned, exact-length SSID; game token is a hint only. P4MP verifies the
 * full package identity after association, before joining or launching. */
bool p4_wifi_room_name(char out[33], uint32_t session, uint16_t game);
bool p4_wifi_room_parse(const uint8_t ssid[33], uint32_t *session, uint16_t *game);
