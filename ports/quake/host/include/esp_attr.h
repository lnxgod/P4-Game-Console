// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef P4_QUAKE_HOST_ESP_ATTR_H
#define P4_QUAKE_HOST_ESP_ATTR_H

/* Espressif's quakegeneric subtree marks large globals for PSRAM. The host
 * build keeps those declarations ordinary while preserving vendor bytes. */
#define EXT_RAM_BSS_ATTR

#endif
