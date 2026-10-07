// SPDX-License-Identifier: MIT
#include "p4/doom_replay.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static p4_doom_lockstep_t host, guest;
static p4_doom_lockstep_frame_t frame;

static void commit(uint32_t tick, bool active)
{
    p4_doom_mp_tic_t input = {.tick=tick, .forward_move=9};
    assert(p4_doom_lockstep_submit(&host, &input));
    if (active) {
        input.forward_move = -7;
        assert(p4_doom_lockstep_input(&host, 1, &input, tick));
    }
    p4_doom_lockstep_pump(&host);
    assert(p4_doom_lockstep_pop(&host, &frame));
    assert(frame.tick == tick && frame.mask == (active ? 3 : 1));
    if (active) assert(p4_doom_lockstep_ack(&host, 1, tick+1));
}

static void replay_retains_commands_and_roster(void)
{
    uint8_t storage[40 * 96], packet[40];
    p4_doom_replay_journal_t journal;
    assert(p4_doom_replay_journal_init(&journal, storage, sizeof(storage), 2));
    assert(p4_doom_lockstep_init(&host, 0, 2));
    assert(p4_doom_lockstep_init(&guest, 1, 2));
    assert(!p4_doom_lockstep_replay_begin(&host));
    assert(p4_doom_lockstep_replay_begin(&guest));
    for (uint32_t tick=0; tick<80; ++tick) {
        if (tick == 8) assert(p4_doom_lockstep_depart(&host, 1));
        commit(tick, tick<8);
        assert(p4_doom_replay_journal_append(&journal, &frame));
    }
    /* Replay never holds host history, including while the guest is absent. */
    assert(host.next_output == 80 && host.next_read == 80);
    assert(!p4_doom_replay_journal_packet(&journal, 80, packet));
    assert(p4_doom_replay_journal_packet(&journal, 1, packet));
    assert(!p4_doom_lockstep_receive_replay(&guest, packet, sizeof(packet)));
    assert(!p4_doom_lockstep_replay_finish(&guest, 80));
    for (uint32_t tick=0; tick<80; ++tick) {
        assert(p4_doom_replay_journal_packet(&journal, tick, packet));
        assert(!p4_doom_lockstep_receive(&guest, packet, sizeof(packet)));
        assert(p4_doom_lockstep_receive_replay(&guest, packet, sizeof(packet)));
        assert(!p4_doom_lockstep_receive_replay(&guest, packet, sizeof(packet)));
        assert(p4_doom_lockstep_pop(&guest, &frame));
        assert(frame.tick == tick && frame.mask == (tick<8 ? 3 : 1));
        assert(frame.commands[0].forward_move == 9);
        assert(frame.commands[1].forward_move == (tick<8 ? -7 : 0));
    }
    assert(p4_doom_lockstep_reactivate(&host, 1, 82));
    assert(!p4_doom_lockstep_prepare_live_input(&guest, 79));
    assert(!p4_doom_lockstep_prepare_live_input(&guest, 1000));
    assert(p4_doom_lockstep_prepare_live_input(&guest, 82));
    p4_doom_mp_tic_t input = {.tick=82, .forward_move=-7, .consistency=123};
    assert(p4_doom_lockstep_submit(&guest, &input));
    assert(p4_doom_lockstep_input(&host, 1, &input, 82));
    assert(!p4_doom_lockstep_prepare_live_input(&guest, 83));
    assert(p4_doom_lockstep_prepare_live_input(&guest, 82));
    assert(guest.local.pending_count == 1 && guest.local.next_local_tick == 83);
    for (uint32_t tick=80; tick<82; ++tick) {
        commit(tick, false);
        assert(p4_doom_replay_journal_append(&journal, &frame));
        assert(p4_doom_replay_journal_packet(&journal, tick, packet));
        assert(p4_doom_lockstep_receive_replay(&guest, packet, sizeof(packet)));
        assert(p4_doom_lockstep_pop(&guest, &frame));
    }
    packet[3] = 3; packet[4] = 82;
    assert(!p4_doom_lockstep_receive_replay(&guest, packet, sizeof(packet)));
    assert(guest.next_output == 82 && guest.local.pending_count == 1);
    assert(!p4_doom_lockstep_replay_finish(&guest, 83));
    assert(p4_doom_lockstep_replay_finish(&guest, 82));
    assert(guest.local.next_local_tick == 83 && guest.local.peer_ack == 82);
    assert(guest.local.pending_count == 1);
    assert(!p4_doom_lockstep_receive_replay(&guest, packet, sizeof(packet)));
    assert(!p4_doom_lockstep_replay_begin(&guest));
    /* First live input carries the reconstructed consistency value unchanged. */
    assert(p4_doom_lockstep_submit(&guest, &input));
    assert(p4_doom_lockstep_input(&host, 1, &input, 82));
    input.forward_move=9; input.consistency=27;
    assert(p4_doom_lockstep_submit(&host, &input));
    p4_doom_lockstep_pump(&host);
    assert(p4_doom_lockstep_pop(&host, &frame));
    assert(frame.tick == 82 && frame.mask == 3);
    assert(p4_doom_lockstep_packet(&host, 1, packet));
    assert(p4_doom_lockstep_receive(&guest, packet, sizeof(packet)));
    assert(p4_doom_lockstep_pop(&guest, &frame));
    assert(frame.tick == 82 && frame.mask == 3);
    assert(frame.commands[1].consistency == 123);
    assert(p4_doom_lockstep_ack(&host, 1, 83));
    assert(guest.local.pending_count == 0);
    assert(!p4_doom_lockstep_prepare_live_input(&guest, 83));
}

static void future_activation_erases_old_input(void)
{
    assert(p4_doom_lockstep_init(&host, 0, 2));
    p4_doom_mp_tic_t old = {.tick=2, .forward_move=100};
    assert(p4_doom_lockstep_input(&host, 1, &old, 0));
    assert(p4_doom_lockstep_depart(&host, 1));
    assert(!p4_doom_lockstep_reactivate(&host, 0, 2));
    assert(!p4_doom_lockstep_reactivate(&host, 2, 2));
    assert(!p4_doom_lockstep_reactivate(&host, 1, 0));
    assert(!p4_doom_lockstep_reactivate(&host, 1, UINT32_MAX));
    assert(p4_doom_lockstep_reactivate(&host, 1, 2));
    assert(!p4_doom_lockstep_reactivate(&host, 1, 3));
    p4_doom_mp_tic_t fresh = {.tick=2, .forward_move=-7};
    assert(!p4_doom_lockstep_ack(&host, 1, 2));
    assert(!p4_doom_lockstep_input(&host, 1, &fresh, 3));
    fresh.tick = 1;
    assert(!p4_doom_lockstep_input(&host, 1, &fresh, 2));
    fresh.tick = 2;
    assert(p4_doom_lockstep_input(&host, 1, &fresh, 2));
    commit(0, false);
    commit(1, false);
    commit(2, true);
    assert(frame.commands[1].forward_move == -7);
    assert(host.mask == 3 && host.pending_mask == 0);
    /* Canceled admissions cannot later reactivate or retain queued input. */
    assert(p4_doom_lockstep_depart(&host, 1));
    assert(p4_doom_lockstep_reactivate(&host, 1, 5));
    assert(p4_doom_lockstep_depart(&host, 1));
    commit(3, false); commit(4, false); commit(5, false);
}

static void rejects_corrupt_replay_without_advancing(void)
{
    assert(p4_doom_lockstep_init(&guest, 1, 2));
    assert(p4_doom_lockstep_replay_begin(&guest));
    uint8_t canonical[40] = {'G', 'C', 1, 1};
    uint8_t bad[40];
    const unsigned positions[] = {0, 1, 2, 3, 4, 15, 16, 39};
    const uint8_t values[] = {'B', 'B', 2, 5, 1, 1, 1, 1};
    for (unsigned i=0; i<sizeof(positions)/sizeof(positions[0]); ++i) {
        memcpy(bad, canonical, sizeof(bad)); bad[positions[i]] = values[i];
        assert(!p4_doom_lockstep_receive_replay(&guest, bad, sizeof(bad)));
        assert(guest.next_output == 0 && guest.next_read == 0);
    }
    assert(!p4_doom_lockstep_receive_replay(&guest, canonical, 39));
    assert(p4_doom_lockstep_receive_replay(&guest, canonical, 40));
    assert(!p4_doom_lockstep_replay_finish(&guest, 1));
    assert(p4_doom_lockstep_pop(&guest, &frame));
    assert(p4_doom_lockstep_replay_finish(&guest, 1));
}

static void overflow_disables_rejoin_and_preserves_match(void)
{
    uint8_t storage[81], original[80], packet[40];
    memset(storage, 0xa5, sizeof(storage));
    p4_doom_replay_journal_t journal;
    assert(!p4_doom_replay_journal_init(&journal, NULL, 81, 2));
    assert(!p4_doom_replay_journal_available(&journal));
    assert(!p4_doom_replay_journal_init(&journal, storage, 39, 2));
    assert(p4_doom_replay_journal_init(&journal, storage, sizeof(storage), 2));
    assert(journal.capacity == 2);
    assert(p4_doom_lockstep_init(&host, 0, 2));
    assert(p4_doom_lockstep_depart(&host, 1));
    for (uint32_t tick=0; tick<2; ++tick) {
        commit(tick, false);
        assert(p4_doom_replay_journal_append(&journal, &frame));
    }
    memcpy(original, storage, sizeof(original));
    for (uint32_t tick=2; tick<90; ++tick) {
        commit(tick, false);
        assert(!p4_doom_replay_journal_append(&journal, &frame));
        assert(!p4_doom_replay_journal_available(&journal));
        assert(!p4_doom_replay_journal_packet(&journal, 0, packet));
    }
    assert(memcmp(original, storage, sizeof(original)) == 0);
    assert(storage[80] == 0xa5 && host.next_output == 90);
    assert(p4_doom_replay_journal_init(&journal, storage, sizeof(storage), 2));
    assert(!p4_doom_replay_journal_append(&journal, &frame));
    assert(!p4_doom_replay_journal_available(&journal));
}

static void replay_never_overwrites_unconsumed_frames(void)
{
    assert(p4_doom_lockstep_init(&guest, 1, 2));
    assert(p4_doom_lockstep_replay_begin(&guest));
    p4_doom_mp_tic_t local = {.tick=0};
    assert(!p4_doom_lockstep_submit(&guest, &local));
    uint8_t packet[40] = {'G', 'C', 1, 1};
    for (uint8_t tick=0; tick<32; ++tick) {
        packet[4] = tick;
        assert(p4_doom_lockstep_receive_replay(&guest, packet, sizeof(packet)));
    }
    packet[4] = 32;
    assert(!p4_doom_lockstep_receive_replay(&guest, packet, sizeof(packet)));
    assert(p4_doom_lockstep_pop(&guest, &frame) && frame.tick == 0);
    assert(p4_doom_lockstep_receive_replay(&guest, packet, sizeof(packet)));
    for (uint32_t tick=1; tick<=32; ++tick)
        assert(p4_doom_lockstep_pop(&guest, &frame) && frame.tick == tick);
    assert(p4_doom_lockstep_replay_finish(&guest, 33));
}

static void missed_activation_cannot_stall_host(void)
{
    assert(p4_doom_lockstep_init(&host, 0, 2));
    assert(p4_doom_lockstep_depart(&host, 1));
    assert(p4_doom_lockstep_reactivate(&host, 1, 2));
    p4_doom_mp_tic_t later = {.tick=4, .forward_move=99};
    assert(p4_doom_lockstep_input(&host, 1, &later, 2));
    commit(0, false); commit(1, false);
    /* Calling pump while other players are not ready cannot cancel early. */
    p4_doom_lockstep_pump(&host);
    assert(host.pending_mask == 2 && host.activate_at[1] == 2);
    commit(2, false);
    assert(host.pending_mask == 0 && host.mask == 1 && host.activate_at[1] == 0);
    assert(!p4_doom_lockstep_input(&host, 1, &later, 2));
    assert(p4_doom_lockstep_reactivate(&host, 1, 4));
    later.forward_move = -7;
    assert(p4_doom_lockstep_input(&host, 1, &later, 4));
    commit(3, false); commit(4, true);
    assert(frame.commands[1].forward_move == -7);
}

int main(void)
{
    replay_retains_commands_and_roster();
    future_activation_erases_old_input();
    rejects_corrupt_replay_without_advancing();
    overflow_disables_rejoin_and_preserves_match();
    replay_never_overwrites_unconsumed_frames();
    missed_activation_cannot_stall_host();
    puts("bounded replay: exact history, absent masks, future activation, malformed input and overflow passed");
    return 0;
}
