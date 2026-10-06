// SPDX-License-Identifier: MIT
#include "discovery.h"
#include <stdio.h>
#include <string.h>
bool p4_wifi_room_name(char out[33], uint32_t session, uint16_t game)
{
    if (!out || !session || !game) return false;
    return snprintf(out, 33, "P4PLAY1-%08lx-%04x", (unsigned long)session, (unsigned)game) == 21;
}
bool p4_wifi_room_parse(const uint8_t ssid[33], uint32_t *session, uint16_t *game)
{
    if (!ssid || !session || !game || memcmp(ssid,"P4PLAY1-",8) || ssid[16]!='-' || ssid[21]!=0) return false;
    uint32_t sid=0, token=0;
    for (unsigned i=8;i<21;++i) {
        if (i==16) continue;
        unsigned c=ssid[i], v;
        if (c>='0'&&c<='9') v=c-'0';
        else if (c>='a'&&c<='f') v=c-'a'+10;
        else return false;
        if (i<16) sid=(sid<<4)|v;
        else token=(token<<4)|v;
    }
    if (!sid || !token) return false;
    *session=sid; *game=(uint16_t)token; return true;
}
