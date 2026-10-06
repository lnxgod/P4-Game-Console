/* SPDX-License-Identifier: MIT */
#ifndef WACKY_NETWORK_H
#define WACKY_NETWORK_H
#include "p4/game.h"
#include "ww_race.h"
enum { WW_NET_BYTES = 64, WW_NET_COUNTDOWN = 34 };
typedef struct {
    WwPlayerMotion motion;
    WwLapState lap;
    uint8_t buttons, rank, jump_ticks, vehicle;
} WwNetPlayer;
typedef struct {
    WwNetPlayer players[4], pending[4];
    uint64_t seed;
    uint32_t generation, revision, tick, input_sequence, received_sequence[4];
    uint32_t pending_revision, pending_tick;
    uint32_t silent_ms[4], send_ms;
    uint16_t finish_count;
    uint8_t slot, count, heard_mask, pending_mask, track;
    bool active, host, ended, heard_peer;
} WwNet;
bool ww_net_start(WwNet *, p4_game_context_t *, WwRace *);
/* Returns true only when a complete client snapshot changed the view. */
bool ww_net_poll(WwNet *, p4_game_context_t *, uint32_t buttons, uint32_t ms);
bool ww_net_step(WwNet *, WwRace *, const WwRenderer *);
bool ww_net_view(const WwNet *, WwRace *, const WwRenderer *);
void ww_net_publish(WwNet *, p4_game_context_t *);
#endif
