// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef P4_QUAKE_EMBEDDED_INTERNAL_H
#define P4_QUAKE_EMBEDDED_INTERNAL_H

#include <stdbool.h>
#include <stdio.h>

bool p4_quake_sys_set_root(const char *root);
FILE *p4_quake_fopen(const char *path, const char *mode);
void p4_quake_report_fatal(void);

#endif
