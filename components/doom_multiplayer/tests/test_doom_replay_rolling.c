// SPDX-License-Identifier: MIT
#include "p4/doom_replay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SUFFIX_TICS = 35 * 120, SUFFIX_BYTES = SUFFIX_TICS * P4_DOOM_LOCKSTEP_BYTES };
_Static_assert(SUFFIX_BYTES == 168000, "120-second canonical suffix budget");

static p4_doom_lockstep_frame_t canonical(uint32_t tick)
{
    p4_doom_lockstep_frame_t f = {.tick = tick, .mask = (tick % 3U) == 0 ? 15 : 1};
    for (uint8_t slot = 0; slot < P4_MP_MAX_PLAYERS; ++slot) {
        f.commands[slot].tick = tick;
        if (!(f.mask & (1U << slot))) continue;
        f.commands[slot].forward_move = (int8_t)(tick % 101U);
        f.commands[slot].side_move = (int8_t)-(int8_t)(tick % 97U);
        f.commands[slot].angle_turn = (int16_t)(tick % 31001U);
        f.commands[slot].buttons = (uint8_t)(tick % 256U);
        f.commands[slot].consistency = (uint8_t)((tick + slot) % 256U);
        f.commands[slot].chat_char = (uint8_t)(tick % 128U);
    }
    return f;
}

static void exact(const p4_doom_replay_journal_t *j, uint32_t tick)
{
    uint8_t actual[P4_DOOM_LOCKSTEP_BYTES], expected[P4_DOOM_LOCKSTEP_BYTES];
    p4_doom_lockstep_frame_t f = canonical(tick), decoded;
    assert(p4_doom_lockstep_frame_encode(&f, 4, expected));
    assert(p4_doom_replay_journal_packet(j, tick, actual));
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
    assert(p4_doom_lockstep_frame_decode(actual, sizeof(actual), 4, &decoded));
    assert(decoded.tick == tick && decoded.mask == f.mask);
}

static void rejects_without_output(const p4_doom_replay_journal_t *j, uint32_t tick)
{
    uint8_t actual[P4_DOOM_LOCKSTEP_BYTES], original[P4_DOOM_LOCKSTEP_BYTES];
    memset(actual, 0xd9, sizeof(actual)); memcpy(original, actual, sizeof(actual));
    assert(!p4_doom_replay_journal_packet(j, tick, actual));
    assert(memcmp(actual, original, sizeof(actual)) == 0);
}

static void initialization_and_reset(void)
{
    uint8_t bytes[3 * P4_DOOM_LOCKSTEP_BYTES];
    p4_doom_replay_journal_t j;
    assert(!p4_doom_replay_journal_init_rolling(NULL, bytes, sizeof(bytes), 4));
    assert(!p4_doom_replay_journal_init_rolling(&j, NULL, sizeof(bytes), 4));
    assert(!p4_doom_replay_journal_available(&j));
    assert(!p4_doom_replay_journal_init_rolling(&j, bytes, 39, 4));
    assert(!p4_doom_replay_journal_init_rolling(&j, bytes, sizeof(bytes), 1));
    assert(!p4_doom_replay_journal_init_rolling(&j, bytes, sizeof(bytes), 5));
    assert(p4_doom_replay_journal_init_rolling(&j, bytes, sizeof(bytes), 4));
    assert(j.rolling && j.capacity == 3 && j.first_tick == 0 && j.next_tick == 0);
    rejects_without_output(&j, 0);
    rejects_without_output(&j, UINT32_MAX);
    for (uint32_t tick = 0; tick < 4; ++tick) {
        p4_doom_lockstep_frame_t f = canonical(tick);
        assert(p4_doom_replay_journal_append(&j, &f));
    }
    assert(j.first_tick == 1 && j.next_tick == 4);
    assert(p4_doom_replay_journal_init(&j, bytes, sizeof(bytes), 4));
    assert(!j.rolling && j.first_tick == 0 && j.next_tick == 0);
    rejects_without_output(&j, 0);
    assert(!p4_doom_replay_journal_packet(&j, 0, NULL));
    assert(!p4_doom_replay_journal_packet(NULL, 0, bytes));
    assert(!p4_doom_replay_journal_append(NULL, NULL));
}

static void all_capacity_boundaries(void)
{
    /* Include one-slot and non-power-of-two rings; preserve prefix, suffix,
     * and non-frame-aligned storage tail bytes on every eviction. */
    for (size_t capacity = 1; capacity <= 9; ++capacity) {
        uint8_t guarded[2 + 9 * P4_DOOM_LOCKSTEP_BYTES + 7];
        memset(guarded, 0xa5, sizeof(guarded));
        const size_t bytes = capacity * P4_DOOM_LOCKSTEP_BYTES;
        p4_doom_replay_journal_t j;
        assert(p4_doom_replay_journal_init_rolling(&j, guarded + 1, bytes + 7, 4));
        assert(j.capacity == capacity);
        for (uint32_t tick = 0; tick < 127; ++tick) {
            p4_doom_lockstep_frame_t f = canonical(tick);
            assert(p4_doom_replay_journal_append(&j, &f));
            assert(j.next_tick == tick + 1);
            const uint32_t first = tick + 1 > capacity ? tick + 1 - (uint32_t)capacity : 0;
            assert(j.first_tick == first && p4_doom_replay_journal_available(&j));
            for (uint32_t retained = first; retained <= tick; ++retained) exact(&j, retained);
            if (first) rejects_without_output(&j, first - 1);
            rejects_without_output(&j, tick + 1);
            rejects_without_output(&j, UINT32_MAX);
            assert(guarded[0] == 0xa5);
            for (size_t i = bytes + 1; i < sizeof(guarded); ++i) assert(guarded[i] == 0xa5);
        }
    }
}

static void loss_retries_and_stale_reader(void)
{
    uint8_t bytes[3 * P4_DOOM_LOCKSTEP_BYTES], first[P4_DOOM_LOCKSTEP_BYTES];
    uint8_t retry[P4_DOOM_LOCKSTEP_BYTES];
    p4_doom_replay_journal_t j;
    assert(p4_doom_replay_journal_init_rolling(&j, bytes, sizeof(bytes), 4));
    for (uint32_t tick = 0; tick < 3; ++tick) {
        p4_doom_lockstep_frame_t f = canonical(tick);
        assert(p4_doom_replay_journal_append(&j, &f));
    }
    assert(p4_doom_replay_journal_packet(&j, 0, first));
    /* Lost delivery/repeated reads leave storage and the range untouched. */
    for (unsigned loss = 0; loss < 100; ++loss) {
        assert(p4_doom_replay_journal_packet(&j, 0, retry));
        assert(memcmp(first, retry, sizeof(first)) == 0);
        assert(j.first_tick == 0 && j.next_tick == 3);
    }
    p4_doom_lockstep_frame_t f = canonical(3);
    assert(p4_doom_replay_journal_append(&j, &f));
    rejects_without_output(&j, 0);
    /* Even a completely stalled replay reader never retains storage. */
    for (uint32_t tick = 4; tick < 10000; ++tick) {
        f = canonical(tick);
        assert(p4_doom_replay_journal_append(&j, &f));
        rejects_without_output(&j, 0);
    }
    assert(j.first_tick == 9997 && j.next_tick == 10000);
    exact(&j, 9997); exact(&j, 9998); exact(&j, 9999);
}

static void invalid_append_is_atomic(void)
{
    for (unsigned fault = 0; fault < 6; ++fault) {
        uint8_t bytes[2 * P4_DOOM_LOCKSTEP_BYTES], original[sizeof(bytes)];
        p4_doom_replay_journal_t j;
        assert(p4_doom_replay_journal_init_rolling(&j, bytes, sizeof(bytes), 4));
        for (uint32_t tick = 0; tick < 3; ++tick) {
            p4_doom_lockstep_frame_t f = canonical(tick);
            assert(p4_doom_replay_journal_append(&j, &f));
        }
        memcpy(original, bytes, sizeof(bytes));
        p4_doom_lockstep_frame_t f = canonical(3);
        if (fault == 1) f = canonical(2);       /* duplicate */
        if (fault == 2) f = canonical(4);       /* missing canonical tic */
        if (fault == 3) f.mask = 0;            /* host must exist */
        if (fault == 4) f.commands[1].tick = 2; /* per-command tic mismatch */
        if (fault == 5) { f.mask = 1; f.commands[1].forward_move = 1; }
        assert(!p4_doom_replay_journal_append(&j, fault == 0 ? NULL : &f));
        assert(!j.available && j.first_tick == 1 && j.next_tick == 3);
        assert(memcmp(bytes, original, sizeof(bytes)) == 0);
        rejects_without_output(&j, 1);
        f = canonical(3);
        assert(!p4_doom_replay_journal_append(&j, &f));
        assert(memcmp(bytes, original, sizeof(bytes)) == 0);
    }
}

static void monotonic_counter_limit(void)
{
    uint8_t guarded[2 + 3 * P4_DOOM_LOCKSTEP_BYTES], original[sizeof(guarded)];
    memset(guarded, 0xa5, sizeof(guarded));
    p4_doom_replay_journal_t j;
    assert(p4_doom_replay_journal_init_rolling(&j, guarded + 1, sizeof(guarded) - 2, 4));
    /* Valid empty-range fixture near the terminal counter; no billions of
     * iterations or invalid retained slots are needed to reach the boundary. */
    j.first_tick = UINT32_MAX - 4; j.next_tick = j.first_tick;
    for (uint32_t tick = UINT32_MAX - 4; tick < UINT32_MAX; ++tick) {
        p4_doom_lockstep_frame_t f = canonical(tick);
        assert(p4_doom_replay_journal_append(&j, &f));
        exact(&j, tick);
    }
    assert(j.first_tick == UINT32_MAX - 3 && j.next_tick == UINT32_MAX);
    assert(j.available && guarded[0] == 0xa5 && guarded[sizeof(guarded)-1] == 0xa5);
    for (uint32_t tick = j.first_tick; tick < UINT32_MAX; ++tick) exact(&j, tick);
    rejects_without_output(&j, UINT32_MAX - 4);
    rejects_without_output(&j, UINT32_MAX);
    memcpy(original, guarded, sizeof(guarded));
    p4_doom_lockstep_frame_t f = canonical(UINT32_MAX);
    assert(!p4_doom_replay_journal_append(&j, &f));
    assert(!j.available && j.first_tick == UINT32_MAX - 3 && j.next_tick == UINT32_MAX);
    assert(memcmp(original, guarded, sizeof(guarded)) == 0);
    f = canonical(0);
    assert(!p4_doom_replay_journal_append(&j, &f));
    assert(j.next_tick == UINT32_MAX);
}

static void long_match_keeps_host_running(void)
{
    static uint8_t guarded[2 + SUFFIX_BYTES];
    p4_doom_replay_journal_t j;
    p4_doom_lockstep_t host;
    p4_doom_lockstep_frame_t f, decoded;
    uint8_t packet[P4_DOOM_LOCKSTEP_BYTES];
    memset(guarded, 0xa5, sizeof(guarded));
    assert(p4_doom_replay_journal_init_rolling(&j, guarded + 1, SUFFIX_BYTES, 2));
    assert(j.capacity == SUFFIX_TICS);
    assert(p4_doom_lockstep_init(&host, 0, 2));
    /* A departed reader never supplies ACKs. Host goes beyond the existing
     * 4-MiB linear horizon, with fifty complete ring rotations. */
    assert(p4_doom_lockstep_depart(&host, 1));
    for (uint32_t tick = 0; tick < 210000; ++tick) {
        p4_doom_mp_tic_t input = {.tick = tick, .forward_move = (int8_t)(tick % 101U)};
        assert(p4_doom_lockstep_submit(&host, &input));
        p4_doom_lockstep_pump(&host);
        assert(p4_doom_lockstep_pop(&host, &f) && f.tick == tick && f.mask == 1);
        assert(p4_doom_replay_journal_append(&j, &f));
        assert(j.next_tick == host.next_output && j.next_tick == host.next_read);
        assert(j.next_tick - j.first_tick <= SUFFIX_TICS);
        assert(j.available);
    }
    assert(j.first_tick == 205800 && j.next_tick == 210000);
    for (uint32_t tick = j.first_tick; tick < j.next_tick; ++tick) {
        assert(p4_doom_replay_journal_packet(&j, tick, packet));
        assert(p4_doom_lockstep_frame_decode(packet, sizeof(packet), 2, &decoded));
        assert(decoded.tick == tick && decoded.mask == 1);
        assert(decoded.commands[0].forward_move == (int8_t)(tick % 101U));
        assert(decoded.commands[1].forward_move == 0);
    }
    rejects_without_output(&j, j.first_tick - 1);
    rejects_without_output(&j, j.next_tick);
    assert(guarded[0] == 0xa5 && guarded[sizeof(guarded)-1] == 0xa5);
}

int main(void)
{
    initialization_and_reset();
    all_capacity_boundaries();
    loss_retries_and_stale_reader();
    invalid_append_is_atomic();
    monotonic_counter_limit();
    long_match_keeps_host_running();
    puts("rolling replay: initialization/reset, capacities 1-9, eviction/ranges, loss/retries, atomic invalid input, UINT32_MAX and 210000-tic live host passed");
    return 0;
}
