// SPDX-License-Identifier: GPL-2.0-or-later
/* Staged separate-process solo/late-join engine proof. Canonical-tic fixture
 * events are test inputs, not journal or transport semantics. */
#include "p4_doom_net.h"
#include "p4/doom_arena.h"
#include "d_loop.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "p_local.h"
#include "i_video.h"
#include "m_random.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#endif
#include "doomgeneric.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

extern void p4_doom_gc_engine_begin_mask(uint8_t, uint8_t, uint8_t);
extern const p4_doom_arena_t *p4_doom_gc_test_state(void);
extern byte consistancy[MAXPLAYERS][BACKUPTICS];
extern boolean G_CheckSpot(int, mapthing_t *);
#include "doom_late_join_digest.h"

enum { FIRST_CATCHUP_TIC = 960, FIRST_ACTIVATE_TIC = 1024,
       DEPARTURE_TIC = 1700, SECOND_CATCHUP_TIC = 2000,
       SECOND_ACTIVATE_TIC = 2048, FINAL_TIC = 2704,
       MAX_HISTORY = FINAL_TIC + BACKUPTICS, GUEST_SLOT = 1 };
typedef struct { ticcmd_t cmds[NET_MAXPLAYERS]; boolean mask[NET_MAXPLAYERS]; } record_t;
static record_t history[MAX_HISTORY];
static unsigned history_count, next_received;
static FILE *journal, *state_stream;
static boolean guest, solo_reference, replaying, input_ready;
static const char *role_name;
static int catchup_tic, activate_tic, final_tic;
static uint32_t clock_ms = 1000;
static unsigned main_calls, replay_calls, max_replay_batch, live_submits;
static int observed_tic = -1, first_live_submit = -1;
static int removed_item_index = -1, removed_item_time;
static boolean item_at_tail, saw_first, saw_absent, saw_return, saw_respawn, saw_vote;
static p4_doom_arena_player_t host_before_activation;
static mobj_t *host_body_before_activation;
static uint8_t map_before_activation, map_count_before_activation;

static void fail(const char *reason)
{
    fprintf(stderr, "P4_DOOM_LATE_JOIN FAIL role=%s tic=%d reason=%s\n", role_name, gametic, reason);
    exit(EXIT_FAILURE);
}

static void write_record(FILE *stream, unsigned tick, const record_t *record)
{
    fprintf(stream, "%u", tick);
    for (unsigned i = 0; i < NET_MAXPLAYERS; ++i) {
        const ticcmd_t *cmd = &record->cmds[i];
        fprintf(stream, " %d %d %d %d %d %u %u", record->mask[i],
                (int)cmd->forwardmove, (int)cmd->sidemove, (int)cmd->angleturn,
                (int)cmd->consistancy, (unsigned char)cmd->chatchar,
                (unsigned char)cmd->buttons);
    }
    fputc('\n', stream);
}

static void read_history(void)
{
    unsigned tick;
    while (fscanf(journal, "%u", &tick) == 1) {
        if (tick != history_count || tick >= MAX_HISTORY) fail("journal ordering/bounds");
        for (unsigned i = 0; i < NET_MAXPLAYERS; ++i) {
            int mask, forward, side, angle, consistency;
            unsigned chat, buttons;
            if (fscanf(journal, "%d%d%d%d%d%u%u", &mask, &forward, &side,
                       &angle, &consistency, &chat, &buttons) != 7)
                fail("truncated journal");
            if (mask < 0 || mask > 1 || forward < -128 || forward > 127 ||
                side < -128 || side > 127 || angle < -32768 || angle > 32767 ||
                consistency < 0 || consistency > 255 || chat > 255 || buttons > 255)
                fail("invalid journal field");
            history[tick].mask[i] = mask != 0;
            history[tick].cmds[i] = (ticcmd_t){
                .forwardmove = (signed char)forward, .sidemove = (signed char)side,
                .angleturn = (short)angle, .consistancy = (byte)consistency,
                .chatchar = (byte)chat, .buttons = (byte)buttons };
        }
        ++history_count;
    }
    if (history_count < FINAL_TIC) fail("insufficient history");
}

static record_t canonical_command(unsigned tick)
{
    record_t result = {0};
    result.mask[0] = true;
    result.mask[GUEST_SLOT] = tick >= FIRST_ACTIVATE_TIC &&
        (tick < DEPARTURE_TIC || tick >= SECOND_ACTIVATE_TIC);
    for (unsigned i = 0; i < MAXPLAYERS; ++i) {
        if (!result.mask[i]) continue;
        result.cmds[i].consistancy = consistancy[i][tick % BACKUPTICS];
        if (tick >= 2) {
            result.cmds[i].forwardmove = 3;
            result.cmds[i].sidemove = (signed char)((tick / 70U) % 2U ? 2 : -2);
            result.cmds[i].angleturn = (short)((i + 1U) * 32U);
            if (tick % 35U == 20) result.cmds[i].buttons = BT_USE;
        }
    }
    const unsigned vote_start = tick < 2000 ? 1250U : 2450U;
    const uint8_t generation = p4_doom_gc_test_state()->vote_generation;
    if (tick == vote_start) result.cmds[1].chatchar = (byte)(vote_start == 1250 ? 0x82 : 0x86);
    if (tick == vote_start + 1U) result.cmds[1].chatchar = (byte)(0xa0U + generation);
    if (tick == vote_start + 2U) result.cmds[0].chatchar = 0xf0;
    if (tick == vote_start + 3U) result.cmds[0].chatchar = (byte)(0xa0U + generation);
    return result;
}

boolean p4_doom_gc_active(void) { return true; }
boolean P4_DoomNetActive(void) { return true; }
boolean P4_DoomNetFailed(void) { return false; }
boolean P4_DoomNetReplaying(void) { return replaying; }
void P4_DoomNetQuit(void) { }
unsigned int P4_DoomNetInitialPlayerMask(void)
{
    if (getenv("P4_LATE_JOIN_ALL_INITIAL")) return 15U;
    return solo_reference ? 0U : 1U;
}

boolean P4_DoomNetConfigure(net_gamesettings_t *settings)
{
    *settings = (net_gamesettings_t){ .consoleplayer = guest ? GUEST_SLOT : 0,
        .num_players = solo_reference ? 1 : 4, .deathmatch = 2,
        .episode = 1, .map = 1, .skill = 2, .loadgame = -1, .nomonsters = 1,
        .new_sync = 1, .extratics = 1, .ticdup = 1 };
    p4_doom_gc_engine_begin_mask(4, 1, 1);
    if (getenv("P4_LATE_JOIN_ALL_INITIAL"))
        fprintf(stdout, "NEGATIVE_FAULT tic=0 kind=all-seat-initial-mask mask=15\n");
    for (unsigned tick = 0; tick < 2; ++tick) {
        record_t record = guest || solo_reference ? history[tick] : canonical_command(tick);
        if (!guest && !solo_reference) write_record(journal, tick, &record);
        D_ReceiveTic(record.cmds, record.mask);
    }
    next_received = 2;
    return true;
}

void P4_DoomNetSubmitTic(const ticcmd_t *local, int input_tic)
{
    if (input_tic < 0) fail("negative input tic");
    const unsigned tick = (unsigned)input_tic;
    if (tick >= MAX_HISTORY || D_P4TicCapacity() == 0) fail("live journal bounds");
    record_t record;
    if (guest || solo_reference) {
        if ((guest && (!input_ready || tick < (unsigned)activate_tic)) ||
            tick != next_received || tick >= history_count)
            fail("local input resumed at wrong tic");
        const unsigned local_slot = guest ? GUEST_SLOT : 0U;
        if (history[tick].mask[local_slot] &&
            local->consistancy != history[tick].cmds[local_slot].consistancy)
            fail("guest local consistency did not replay");
        record = history[tick];
        if (guest) {
            if (first_live_submit < 0) first_live_submit = (int)tick;
            ++live_submits;
        }
    } else {
        if (tick != next_received) fail("host journal discontinuity");
        record = canonical_command(tick);
        write_record(journal, tick, &record);
    }
    D_ReceiveTic(record.cmds, record.mask);
    ++next_received;
}

static void fixture_events(void)
{
    if (gametic == 0) {
        /* Exercise the real first-spawn occupancy branch with a higher seat.
         * Restore these observation-only aliases before any simulated tick;
         * no extra actor is created and no random value may be consumed. */
        mobj_t *host_body = players[0].mo;
        if (!host_body || players[1].mo || players[3].mo ||
            (host_body->x & (FRACUNIT - 1)) || (host_body->y & (FRACUNIT - 1)))
            fail("spawn occupancy fixture needs aligned virgin start");
        mapthing_t spot = { .x = (short)(host_body->x >> FRACBITS),
                           .y = (short)(host_body->y >> FRACBITS) };
        const int before_game_rng = prndindex, before_ui_rng = rndindex;
        players[0].mo = NULL;
        players[3].mo = host_body;
        const boolean free_spot = G_CheckSpot(1, &spot);
        players[3].mo = NULL;
        players[0].mo = host_body;
        if (free_spot) fail("first spawn ignored occupied higher seat");
        if (prndindex != before_game_rng || rndindex != before_ui_rng)
            fail("spawn occupancy probe consumed RNG");
        fprintf(stdout, "SPAWN_OCCUPANCY PASS tic=0 virgin_slot=1 occupied_slot=3 rng_unchanged=1\n");
    }
    if (gametic == DEPARTURE_TIC - 1) {
        if (!players[1].mo || !players[0].mo || players[1].mo->health <= 10)
            fail("nonfatal departure fixture needs live guest");
        P_DamageMobj(players[1].mo, players[0].mo, players[0].mo, 10);
        P_NoiseAlert(players[1].mo, players[1].mo);
        unsigned sound_refs = 0;
        for (int i = 0; i < numsectors; ++i)
            if (sectors[i].soundtarget == players[1].mo) ++sound_refs;
        if (!sound_refs) fail("departure sound-target fixture missing");
        fprintf(stdout, "FIXTURE tic=%d kind=nonfatal-damage victim=1 sound_refs=%u\n", gametic, sound_refs);
    }
    if (gametic == 1100 || gametic == 1305 || gametic == 2100) {
        const unsigned killer = gametic == 1305 ? 0U : 1U;
        const unsigned victim = 1U - killer;
        if (!players[killer].mo || !players[victim].mo) fail("fixture actor absent");
        if (players[victim].mo->health <= 0) fail("fixture victim already dead");
        P_DamageMobj(players[victim].mo, players[killer].mo, players[killer].mo, 10000);
        fprintf(stdout, "FIXTURE tic=%d kind=damage killer=%u victim=%u\n", gametic, killer, victim);
    }
    if (gametic == 1300) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
            mobj_t *mo = (mobj_t *)th;
            if (!(mo->flags & MF_SPECIAL) || (mo->flags & MF_DROPPED) ||
                mo->type == MT_INV || mo->type == MT_INS || mo->spawnpoint.type == 0) continue;
            removed_item_index = iquehead;
            removed_item_time = leveltime;
            P_RemoveMobj(mo);
            item_at_tail = iquetail == removed_item_index;
            fprintf(stdout, "FIXTURE tic=%d kind=item-remove queue=%d leveltime=%d\n",
                    gametic, removed_item_index, removed_item_time);
            break;
        }
        if (removed_item_index < 0) fail("fixture item missing");
    }
}

static void check_virgin_seat(unsigned slot)
{
    const p4_doom_arena_t *arena = p4_doom_gc_test_state();
    const p4_doom_arena_player_t *seat = &arena->players[slot];
    const ticcmd_t *cmd = &players[slot].cmd;
    if (playeringame[slot] || players[slot].mo || seat->visit || seat->active ||
        seat->kills || seat->idle_tics || seat->use_held ||
        (arena->connected_mask & (1U << slot))) fail("virgin reserved seat spawned or visited");
    if (cmd->forwardmove || cmd->sidemove || cmd->angleturn || cmd->chatchar || cmd->buttons)
        fail("virgin reserved seat retained commands");
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function.acp1 == (actionf_p1)P_MobjThinker &&
            ((const mobj_t *)th)->player == &players[slot])
            fail("virgin reserved seat has actor reference");
}

static void capture_host_before_activation(void)
{
    const p4_doom_arena_t *arena = p4_doom_gc_test_state();
    host_before_activation = arena->players[0];
    host_body_before_activation = players[0].mo;
    map_before_activation = arena->maps[arena->map_index];
    map_count_before_activation = arena->map_count;
}

static void check_host_preserved(void)
{
    const p4_doom_arena_t *arena = p4_doom_gc_test_state();
    const p4_doom_arena_player_t *host = &arena->players[0];
    if (host->visit != host_before_activation.visit || host->kills != host_before_activation.kills ||
        host->active != host_before_activation.active || players[0].mo != host_body_before_activation ||
        arena->maps[arena->map_index] != map_before_activation || arena->map_count != map_count_before_activation)
        fail("guest activation reset host or map");
}

static void check_scenario(void)
{
    const p4_doom_arena_t *arena = p4_doom_gc_test_state();
    check_virgin_seat(2); check_virgin_seat(3);
    if (gametic <= FIRST_ACTIVATE_TIC) check_virgin_seat(1);
    if (arena->players[0].visit != 1 || !arena->players[0].active)
        fail("host continuous visit was interrupted");
    if (gametic == FIRST_ACTIVATE_TIC || gametic == SECOND_ACTIVATE_TIC)
        capture_host_before_activation();
    if (gametic == FIRST_ACTIVATE_TIC + 1) {
        if (!playeringame[1] || !players[1].mo || !arena->players[1].active ||
            arena->players[1].visit != 1 || arena->players[1].kills != 0)
            fail("first activation did not create first visit");
        check_host_preserved(); saw_first = true;
        fprintf(stdout, "FIRST_VISIT tic=%d slot=1 visit=1 host_visit=%" PRIu32 "\n", gametic, arena->players[0].visit);
    }
    if (gametic == 1280) {
        if (arena->maps[arena->map_index] != 2 || arena->players[1].kills == 0)
            fail("vote or preserved guest score missing");
        saw_vote = true;
    }
    if (gametic == 1350 && arena->players[0].kills != 1) fail("host score fixture missing");
    if (gametic == DEPARTURE_TIC + 1) {
        if (playeringame[1] || players[1].mo || (arena->connected_mask & 2U) ||
            arena->players[1].visit != 1 || arena->players[1].active)
            fail("departed guest remained present");
        saw_absent = true;
    }
    if (gametic == SECOND_CATCHUP_TIC && (!saw_absent || playeringame[1] || players[1].mo ||
        arena->players[0].kills != 1 || iquehead == iquetail)) fail("second catchup fixture incomplete");
    if (gametic == SECOND_ACTIVATE_TIC + 1) {
        if (!playeringame[1] || !players[1].mo || !arena->players[1].active ||
            arena->players[1].visit != 2 || arena->players[1].kills != 0)
            fail("reactivation did not create second visit");
        check_host_preserved(); saw_return = true;
        fprintf(stdout, "SECOND_VISIT tic=%d slot=1 visit=2 host_visit=%" PRIu32 " host_kills=%" PRIu32 "\n",
                gametic, arena->players[0].visit, arena->players[0].kills);
    }
    if (gametic == 2150 && arena->players[1].kills != 1) fail("second live score missing");
    if (removed_item_index >= 0) {
        if (iquetail == removed_item_index) item_at_tail = true;
        if (!saw_respawn && item_at_tail && leveltime - removed_item_time >= 30 * TICRATE &&
            iquetail != removed_item_index) {
            saw_respawn = true;
            fprintf(stdout, "ITEM_RESPAWN tic=%d queue=%d leveltime=%d\n", gametic, removed_item_index, leveltime);
        }
    }
    if (gametic == 2500 && arena->maps[arena->map_index] != 6) fail("second live vote map missing");
}

void P4_DoomNetPoll(void)
{
    screenvisible = false;
    if (gamestate == GS_LEVEL && players[0].mo && gametic != observed_tic) {
        if (observed_tic >= 0 && gametic != observed_tic + 1) fail("state observation skipped tic");
        observed_tic = gametic;
        p4_rejoin_write_state(state_stream, gametic);
        check_scenario();
        if (gametic >= final_tic) {
            if (!solo_reference && (!saw_first || !saw_absent || !saw_vote)) fail("first scenario coverage missing");
            if (final_tic == FINAL_TIC && (!saw_return || !saw_respawn)) fail("second scenario coverage missing");
            if (guest && (first_live_submit != activate_tic || live_submits < (unsigned)(final_tic - activate_tic)))
                fail("live command coverage missing");
            fprintf(stdout, "P4_DOOM_LATE_JOIN PROCESS PASS role=%s tics=%d main_calls=%u replay_calls=%u max_replay_batch=%u first_live_submit=%d live_submits=%u\n",
                    role_name, gametic, main_calls, replay_calls, max_replay_batch, first_live_submit, live_submits);
            fclose(state_stream); fclose(journal); exit(EXIT_SUCCESS);
        }
        fixture_events();
    }
    if (!guest || !replaying) return;
    if (!input_ready && gametic >= catchup_tic) {
        if (!D_P4ReplayFinish(activate_tic)) fail("replay finish rejected valid activation");
        if (getenv("P4_LATE_JOIN_ZERO_CONSISTENCY")) {
            memset(consistancy[GUEST_SLOT], 0, sizeof(consistancy[GUEST_SLOT]));
            fprintf(stdout, "NEGATIVE_FAULT tic=%d kind=consistency-reset slot=1\n", gametic);
        }
        input_ready = true;
        fprintf(stdout, "REPLAY_READY role=%s tic=%d activation=%d pending=%u clock_ms=%" PRIu32 "\n",
                role_name, gametic, activate_tic, next_received - (unsigned)gametic, clock_ms);
    }
    if (gametic > activate_tic) {
        replaying = false;
        fprintf(stdout, "REPLAY_LIVE role=%s tic=%d clock_ms=%" PRIu32 "\n", role_name, gametic, clock_ms);
        return;
    }
    while (next_received < (unsigned)activate_tic && next_received < history_count && D_P4TicCapacity() > 0) {
        D_ReceiveTic(history[next_received].cmds, history[next_received].mask);
        ++next_received;
    }
}

void DG_Init(void)
{
    role_name = getenv("P4_LATE_JOIN_ROLE");
    const char *journal_path = getenv("P4_LATE_JOIN_JOURNAL");
    const char *state_path = getenv("P4_LATE_JOIN_STATES");
    if (!role_name) fail("missing role");
    guest = strcmp(role_name, "guest-first") == 0 || strcmp(role_name, "guest-rejoin") == 0;
    solo_reference = strcmp(role_name, "solo-reference") == 0;
    if (!guest && !solo_reference && strcmp(role_name, "host") != 0) fail("invalid role");
    const boolean first_guest = strcmp(role_name, "guest-first") == 0;
    catchup_tic = first_guest ? FIRST_CATCHUP_TIC : SECOND_CATCHUP_TIC;
    activate_tic = first_guest ? FIRST_ACTIVATE_TIC : SECOND_ACTIVATE_TIC;
    final_tic = solo_reference ? FIRST_ACTIVATE_TIC - 1 : (first_guest ? DEPARTURE_TIC + 1 : FINAL_TIC);
    replaying = guest;
    if (!journal_path || !state_path) fail("missing output paths");
    journal = fopen(journal_path, guest || solo_reference ? "r" : "w");
    state_stream = fopen(state_path, "w");
    if (!journal || !state_stream) fail("file open");
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (guest || solo_reference) read_history();
}
void DG_DrawFrame(void) { }
void DG_SleepMs(uint32_t milliseconds) { clock_ms += milliseconds; }
uint32_t DG_GetTicksMs(void) { return clock_ms; }
int DG_GetKey(int *pressed, unsigned char *key) { (void)pressed; (void)key; return 0; }
void DG_SetWindowTitle(const char *title) { (void)title; }

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    screenvisible = false;
    for (;;) {
        const int before = gametic;
        const uint32_t before_clock = clock_ms;
        ++main_calls;
        if (!replaying || input_ready) clock_ms += 29;
        if (replaying) ++replay_calls;
        TryRunTics();
        const unsigned advanced = (unsigned)(gametic - before);
        if (replaying && !input_ready && clock_ms != before_clock) fail("replay clock advanced");
        if (advanced > max_replay_batch) max_replay_batch = advanced;
        if (main_calls > 100000) fail("progress deadline");
    }
}
