// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef P4_QUAKE_EMBEDDED_SOCKET_H
#define P4_QUAKE_EMBEDDED_SOCKET_H

#include <stdint.h>

/*
 * Quake's public net structures retain sockaddr storage even when compiled
 * with net_none.c. No embedded code reads this structure until the OS-owned
 * Wi-Fi transport is implemented.
 */
typedef uint16_t sa_family_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

#endif
