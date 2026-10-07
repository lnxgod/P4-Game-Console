// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef DOOM_GC_P4MP_H
#define DOOM_GC_P4MP_H
#include "doom_p4mp_adapter.h"
#include "p4_doom_net.h"
/* Arena retains four fixed seats. Protocol 8 captures at a safe engine boundary
 * into two shared 512 KiB PSRAM buffers and retains a rolling 120-second suffix.
 * Only cold preactivation guests restore; failed attempts leave host play intact
 * and ask the platform to return the guest Home. Transfer/activation leases are
 * 90/110 seconds from capture, within the five-minute total admission ceiling.
 * Protocol 7 keeps the older bounded 4 MiB replay-from-start path for compatibility.
 * Tickets are bearer credentials over Local Wi-Fi, not encryption. Every
 * admitted nonce is retained until the match ends; 64 attempts per guest and 64
 * fresh-attempt tombstones per match are finite admission bounds. Only canonical
 * activation makes a fresh seat permanently owned. Native game messages remain
 * limited to 64 bytes; checkpoint traffic has its own validated packet type. */
esp_err_t p4_doom_gc_prepare(p4_mp_session_t *, const p4_doom_mp_launch_config_t *,
                             const p4_doom_p4mp_transport_t *);
boolean p4_doom_gc_active(void);
boolean p4_doom_gc_configure(net_gamesettings_t *);
void p4_doom_gc_submit(const ticcmd_t *, int);
void p4_doom_gc_poll(void);
void p4_doom_gc_poll_opportunistic(void);
void p4_doom_gc_get_stats(p4_doom_net_stats_t *stats);
void p4_doom_gc_get_loading_progress(p4_doom_loading_progress_t *progress);
boolean p4_doom_gc_failed(void);
void p4_doom_gc_quit(void);
void p4_doom_gc_engine_begin(uint8_t count, uint8_t map);
void p4_doom_gc_engine_begin_mask(uint8_t capacity, uint8_t initial_mask, uint8_t map);
#endif
