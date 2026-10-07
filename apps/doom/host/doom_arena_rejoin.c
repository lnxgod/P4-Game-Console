// SPDX-License-Identifier: GPL-2.0-or-later
/* Separate-process replay fixture. This is a host engine proof, not transport,
 * device cadence, or physical multiplayer acceptance. Fixture damage/removal
 * events are deterministic test inputs tied to canonical gametic, and are not
 * production journal semantics. */
#include "p4_doom_net.h"
#include "p4/doom_arena.h"
#include "d_loop.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "p_local.h"
#include "i_video.h"
#include "m_random.h"
#include <assert.h>
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

extern void p4_doom_gc_engine_begin(uint8_t count, uint8_t map);
extern const p4_doom_arena_t *p4_doom_gc_test_state(void);
extern byte consistancy[MAXPLAYERS][BACKUPTICS];
#include "doom_rejoin_digest.h"

enum { DEPARTURE_TIC = 700, CATCHUP_TIC = 2000, ACTIVATE_TIC = 2048,
       FINAL_TIC = 2704, MAX_HISTORY = FINAL_TIC + BACKUPTICS };
typedef struct { ticcmd_t cmds[NET_MAXPLAYERS]; boolean mask[NET_MAXPLAYERS]; } record_t;
static record_t history[MAX_HISTORY];
static unsigned history_count, next_received;
static FILE *journal, *state_stream;
static boolean guest, replaying, input_ready;
static uint32_t clock_ms = 1000;
static unsigned main_calls, replay_calls, max_replay_batch, live_submits;
static int observed_tic = -1, first_live_submit = -1;
static int removed_item_index = -1, removed_item_time;
static boolean item_at_tail;
static boolean saw_break, saw_absent, saw_return, saw_respawn, saw_vote;

static void fail(const char *reason)
{
    fprintf(stderr, "P4_DOOM_REJOIN FAIL role=%s tic=%d reason=%s\n",
            guest ? "cold-guest" : "host", gametic, reason);
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
    for (unsigned i = 0; i < MAXPLAYERS; ++i) {
        result.mask[i] = i != 3 || tick < DEPARTURE_TIC || tick >= ACTIVATE_TIC;
        result.cmds[i].consistancy = consistancy[i][tick % BACKUPTICS];
        if (tick >= 2 && result.mask[i]) {
            result.cmds[i].forwardmove = (signed char)(i == 0 && tick < 551 ? 0 : 3);
            result.cmds[i].sidemove = (signed char)(i == 0 && tick < 551 ? 0 : ((tick / 70U) % 2U ? 2 : -2));
            result.cmds[i].angleturn = (short)(i == 0 && tick < 551 ? 0 : (i + 1U) * 32U);
            if (i != 0 && tick % 5U == 0) result.cmds[i].buttons = BT_ATTACK;
            if ((i == 0 && tick == 550) || ((i != 0 || tick > 550) && tick % 35U == 20))
                result.cmds[i].buttons |= BT_USE;
        }
    }
    /* Votes are real reserved ticcmd chat bytes, including generation framing. */
    const unsigned vote_start = tick < 2000 ? 1250U : 2450U;
    const uint8_t generation = p4_doom_gc_test_state()->vote_generation;
    if (tick == vote_start) result.cmds[1].chatchar = (byte)(vote_start == 1250 ? 0x82 : 0x86);
    if (tick == vote_start + 1U) result.cmds[1].chatchar = (byte)(0xa0U + generation);
    if (tick == vote_start + 2U) result.cmds[2].chatchar = 0xf0;
    if (tick == vote_start + 3U) result.cmds[2].chatchar = (byte)(0xa0U + generation);
    if (vote_start == 2450 && tick == vote_start + 4U) result.cmds[3].chatchar = 0xf0;
    if (vote_start == 2450 && tick == vote_start + 5U) result.cmds[3].chatchar = (byte)(0xa0U + generation);
    return result;
}

boolean p4_doom_gc_active(void) { return true; }
boolean P4_DoomNetActive(void) { return true; }
boolean P4_DoomNetFailed(void) { return false; }
boolean P4_DoomNetReplaying(void) { return replaying; }
void P4_DoomNetQuit(void) { }

boolean P4_DoomNetConfigure(net_gamesettings_t *settings)
{
    *settings = (net_gamesettings_t){ .consoleplayer = guest ? 3 : 0,
        .num_players = 4, .deathmatch = 2, .episode = 1, .map = 1, .skill = 2,
        .loadgame = -1, .nomonsters = 1, .new_sync = 1, .extratics = 1, .ticdup = 1 };
    p4_doom_gc_engine_begin(4, 1);
    for (unsigned tick = 0; tick < 2; ++tick) {
        record_t record = guest ? history[tick] : canonical_command(tick);
        if (!guest) write_record(journal, tick, &record);
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
    if (guest) {
        if (!input_ready || tick < ACTIVATE_TIC || tick != next_received || tick >= history_count)
            fail("local input resumed at wrong tic");
        if (local->consistancy != history[tick].cmds[3].consistancy)
            fail("guest local consistency did not replay");
        record = history[tick];
        if (first_live_submit < 0) first_live_submit = (int)tick;
        ++live_submits;
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
    if (gametic == DEPARTURE_TIC - 1) {
        if (!players[3].mo || !players[1].mo || players[3].mo->health <= 10)
            fail("nonfatal departure fixture needs live guest");
        P_DamageMobj(players[3].mo, players[1].mo, players[1].mo, 10);
        P_NoiseAlert(players[3].mo, players[3].mo);
        unsigned sound_refs = 0;
        for (int i = 0; i < numsectors; ++i)
            if (sectors[i].soundtarget == players[3].mo) ++sound_refs;
        if (!sound_refs) fail("departure sound-target fixture missing");
        fprintf(stdout, "FIXTURE tic=%d kind=nonfatal-damage victim=3 sound_refs=%u\n", gametic, sound_refs);
    }
    if (gametic == 50 || gametic == 1305 || gametic == 2100) {
        const unsigned killer = gametic == 2100 ? 3U : 1U;
        unsigned victim = gametic == 2100 ? 0U : 2U;
        if (!players[victim].mo || players[victim].mo->health <= 0) {
            for (unsigned i = 0; i < MAXPLAYERS; ++i)
                if (i != killer && playeringame[i] && players[i].mo && players[i].mo->health > 0) {
                    victim = i;
                    break;
                }
        }
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

static void check_scenario(void)
{
    const p4_doom_arena_t *arena = p4_doom_gc_test_state();
    if (gametic == 540) {
        if (arena->players[0].active) fail("idle break not exercised");
        saw_break = true;
    }
    if (gametic == 560 && (!arena->players[0].active || arena->players[0].visit != 2))
        fail("use return not exercised");
    if (gametic == DEPARTURE_TIC + 1) {
        if (playeringame[3] || players[3].mo || (arena->connected_mask & 8U))
            fail("departed seat remained present");
        saw_absent = true;
    }
    if (gametic == 1280) {
        if (arena->maps[arena->map_index] != 2 || arena->players[1].kills == 0)
            fail("vote or preserved score missing");
        saw_vote = true;
    }
    if (gametic == CATCHUP_TIC && (!saw_absent || playeringame[3] || players[3].mo ||
        arena->players[1].kills < 2 || iquehead == iquetail)) fail("catchup fixture incomplete");
    if (gametic == ACTIVATE_TIC + 1) {
        if (!playeringame[3] || !players[3].mo || !arena->players[3].active ||
            arena->players[3].visit != 2 || arena->players[3].kills != 0)
            fail("reactivation did not create fresh visit");
        saw_return = true;
    }
    if (gametic == 2150 && arena->players[3].kills != 1) fail("resumed live score missing");
    if (removed_item_index >= 0) {
        if (iquetail == removed_item_index) item_at_tail = true;
        if (!saw_respawn && item_at_tail && leveltime - removed_item_time >= 30 * TICRATE &&
            iquetail != removed_item_index) {
            saw_respawn = true;
            fprintf(stdout, "ITEM_RESPAWN tic=%d queue=%d leveltime=%d\n",
                    gametic, removed_item_index, leveltime);
        }
    }
    if (gametic == 2500 && arena->maps[arena->map_index] != 6) fail("live vote map missing");
}

void P4_DoomNetPoll(void)
{
    /* I_InitGraphics resets this during startup; keep the headless host's
     * initial frame from setting map-view flags or advancing wipe-only RNG. */
    screenvisible = false;
    if (gamestate == GS_LEVEL && players[0].mo && gametic != observed_tic) {
        if (observed_tic >= 0 && gametic != observed_tic + 1) fail("state observation skipped tic");
        observed_tic = gametic;
        p4_rejoin_write_state(state_stream, gametic);
        check_scenario();
        if (gametic >= FINAL_TIC) {
            if (!saw_break || !saw_absent || !saw_return || !saw_respawn || !saw_vote)
                fail("scenario coverage missing");
            if (guest && (first_live_submit != ACTIVATE_TIC || live_submits < FINAL_TIC - ACTIVATE_TIC))
                fail("resumed live command coverage missing");
            fprintf(stdout, "P4_DOOM_REJOIN PROCESS PASS role=%s tics=%d main_calls=%u replay_calls=%u max_replay_batch=%u first_live_submit=%d live_submits=%u\n",
                    guest ? "cold-guest" : "host", gametic, main_calls, replay_calls,
                    max_replay_batch, first_live_submit, live_submits);
            fclose(state_stream); fclose(journal); exit(EXIT_SUCCESS);
        }
        fixture_events();
    }
    if (!guest || !replaying) return;
    if (!input_ready && gametic >= CATCHUP_TIC) {
        if (!D_P4ReplayFinish(ACTIVATE_TIC)) fail("replay finish rejected valid activation");
        if (getenv("P4_REJOIN_ZERO_CONSISTENCY")) {
            memset(consistancy[3], 0, sizeof(consistancy[3]));
            fprintf(stdout, "NEGATIVE_FAULT tic=%d kind=consistency-reset slot=3\n", gametic);
        }
        input_ready = true;
        fprintf(stdout, "REPLAY_READY tic=%d activation=%d pending=%u clock_ms=%" PRIu32 "\n",
                gametic, ACTIVATE_TIC, next_received - (unsigned)gametic, clock_ms);
    }
    if (gametic > ACTIVATE_TIC) {
        replaying = false;
        fprintf(stdout, "REPLAY_LIVE tic=%d clock_ms=%" PRIu32 "\n", gametic, clock_ms);
        return;
    }
    /* Keep each digest/fixture boundary visible while exercising the real ring.
     * Initial tics are already buffered; subsequent records retain host bytes. */
    while (next_received < ACTIVATE_TIC && next_received < history_count && D_P4TicCapacity() > 0) {
        D_ReceiveTic(history[next_received].cmds, history[next_received].mask);
        ++next_received;
    }
}

void DG_Init(void)
{
    const char *role = getenv("P4_REJOIN_ROLE");
    const char *journal_path = getenv("P4_REJOIN_JOURNAL");
    const char *state_path = getenv("P4_REJOIN_STATES");
    guest = role && strcmp(role, "guest") == 0;
    replaying = guest;
    if (!journal_path || !state_path) fail("missing output paths");
    journal = fopen(journal_path, guest ? "r" : "w");
    state_stream = fopen(state_path, "w");
    if (!journal || !state_stream) fail("file open");
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (guest) read_history();
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
