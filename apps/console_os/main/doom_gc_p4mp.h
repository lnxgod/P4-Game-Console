// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef DOOM_GC_P4MP_H
#define DOOM_GC_P4MP_H
#include "doom_p4mp_adapter.h"
#include "p4_doom_net.h"
esp_err_t p4_doom_gc_prepare(p4_mp_session_t *, const p4_doom_mp_launch_config_t *,
                             const p4_doom_p4mp_transport_t *);
boolean p4_doom_gc_active(void);
boolean p4_doom_gc_configure(net_gamesettings_t *);
void p4_doom_gc_submit(const ticcmd_t *, int);
void p4_doom_gc_poll(void);
boolean p4_doom_gc_failed(void);
void p4_doom_gc_quit(void);
void p4_doom_gc_engine_begin(uint8_t count, uint8_t map);
#endif
