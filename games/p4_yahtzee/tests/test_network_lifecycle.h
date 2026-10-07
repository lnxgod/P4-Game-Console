/* SPDX-License-Identifier: MIT
 * Included by test_p4_yahtzee.c to reuse its actual-source paired-link fixture.
 * Exercises 52 lifecycle cases through Game API update/render callbacks.
 */
#ifndef P4_YAHTZEE_TEST_NETWORK_LIFECYCLE_H
#define P4_YAHTZEE_TEST_NETWORK_LIFECYCLE_H

static int loss_kind[2];
static size_t send_attempts[2];
static int identity_change[2];
static bool refuse_send[2];
static bool zero_generation;
static const char *const loss_names[] = {
    "connected", "peer_left", "error", "offline", "read_failure"
};

static bool departure_status(void *context, p4_game_multiplayer_status_t *out)
{
    test_endpoint_t *endpoint = context;
    (void)link_status(context, out);
    switch (loss_kind[endpoint->slot]) {
    case 1: out->state = P4_GAME_MULTIPLAYER_PEER_LEFT; break;
    case 2: out->state = P4_GAME_MULTIPLAYER_ERROR; break;
    case 3: *out = (p4_game_multiplayer_status_t){0}; break;
    case 4: return false;
    case 5: out->state = P4_GAME_MULTIPLAYER_WAITING; break;
    default: break;
    }
    if (zero_generation) out->generation = 0U;
    switch (identity_change[endpoint->slot]) {
    case 1: ++out->generation; break;
    case 2: ++out->session_seed; break;
    case 3: out->role = out->role == P4_GAME_MULTIPLAYER_ROLE_HOST ?
        P4_GAME_MULTIPLAYER_ROLE_CLIENT : P4_GAME_MULTIPLAYER_ROLE_HOST; break;
    case 4: out->local_player_slot ^= 1U; break;
    case 5: out->player_count = 3U; break;
    default: break;
    }
    return true;
}

static bool departure_send(void *context, const uint8_t *data, size_t bytes)
{
    test_endpoint_t *endpoint = context;
    ++send_attempts[endpoint->slot];
    /* Like the real disconnected service, refuse traffic after departure. */
    return !refuse_send[endpoint->slot] && loss_kind[endpoint->slot] == 0 && link_send(context, data, bytes);
}

static void capture(p4_game_instance_t *instance, uint16_t width,
                    const char *name, uint16_t **out_pixels, size_t *out_words)
{
    const uint16_t height = width == 320U ? 200U : 480U;
    const size_t words = (size_t)width * height;
    uint16_t *pixels = calloc(words + 32U, sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels == NULL) exit(EXIT_FAILURE);
    for (size_t n = words; n < words + 32U; ++n) pixels[n] = 0x55aaU;
    p4_game_surface_t surface = {.pixels = pixels, .width = width,
        .height = height, .stride_pixels = width};
    CHECK(p4_game_instance_render(instance, &surface));
    for (size_t n = words; n < words + 32U; ++n) CHECK(pixels[n] == 0x55aaU);
    const char *dir = getenv("P4_CAPTURE_DIR");
    if (dir != NULL && name != NULL) {
        char path[1024];
        int written = snprintf(path, sizeof(path), "%s/%s-%u.ppm", dir, name, width);
        CHECK(written > 0 && (size_t)written < sizeof(path));
        FILE *file = fopen(path, "wb");
        CHECK(file != NULL);
        if (file != NULL) {
            fprintf(file, "P6\n%u %u\n255\n", width, height);
            for (size_t n = 0; n < words; ++n) {
                unsigned v = pixels[n];
                fputc((int)(((v >> 11U) & 31U) * 255U / 31U), file);
                fputc((int)(((v >> 5U) & 63U) * 255U / 63U), file);
                fputc((int)((v & 31U) * 255U / 31U), file);
            }
            CHECK(fclose(file) == 0);
        }
    }
    *out_pixels = pixels;
    *out_words = words;
}

static void departure_case(int kind, uint8_t survivor, uint16_t width)
{
    memset(loss_kind, 0, sizeof(loss_kind));
    memset(send_attempts, 0, sizeof(send_attempts));
    test_link_t link;
    init_link(&link, 2U);
    p4_game_instance_t instances[2] = {{0}};
    p4_yahtzee_state_t states[2];
    p4_game_services_t services[2];
    for (uint8_t slot = 0; slot < 2U; ++slot) {
        services[slot] = network_services(&link.endpoints[slot]);
        services[slot].multiplayer_read_status = departure_status;
        services[slot].multiplayer_send = departure_send;
        if (width == 768U) services[slot].available_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
        CHECK(test_start_game(&instances[slot], &p4_p4_yahtzee_game,
            &services[slot], &states[slot], sizeof(states[slot])));
    }
    CHECK(update_empty(&instances[0], 16U));
    CHECK(update_empty(&instances[1], 16U));
    CHECK(states[0].phase == P4_YAHTZEE_TURN && states[1].phase == P4_YAHTZEE_TURN);
    CHECK(update_button(&instances[0], P4_BUTTON_START));
    CHECK(update_empty(&instances[1], 16U));
    settle_roll(&instances[0]); settle_roll(&instances[1]);
    if (survivor == 1U) {
        /* Advance legally to guest turn, then roll through the actual protocol. */
        CHECK(update_button(&instances[0], P4_BUTTON_B));
        CHECK(update_button(&instances[0], P4_BUTTON_A));
        CHECK(update_empty(&instances[1], 16U));
        CHECK(update_button(&instances[1], P4_BUTTON_START));
        CHECK(update_empty(&instances[0], 16U));
        CHECK(update_empty(&instances[1], 16U));
        settle_roll(&instances[0]); settle_roll(&instances[1]);
    }
    CHECK(states[survivor].current_player == survivor);
    CHECK(states[0].roll_count == 1U && states[1].roll_count == 1U);
    CHECK(memcmp(states[0].dice, states[1].dice, sizeof(states[0].dice)) == 0);
    p4_game_instance_t *instance = &instances[survivor];
    p4_yahtzee_state_t *state = &states[survivor];
    char name[100];
    uint16_t *before, *after, *expected;
    size_t words, other_words;
    snprintf(name, sizeof(name), "%s-%u-before", loss_names[kind], survivor);
    capture(instance, width, kind == 1 ? name : NULL, &before, &words);
    const uint32_t revision = state->network_revision;
    const uint8_t rolls = state->roll_count;
    loss_kind[survivor] = kind;
    CHECK(update_empty(instance, 16U));
    CHECK(state->network_error);
    snprintf(name, sizeof(name), "%s-%u-after", loss_names[kind], survivor);
    capture(instance, width, kind == 1 ? name : NULL, &after, &other_words);
    CHECK(words == other_words);
    const p4_yahtzee_phase_t actual_phase = state->phase;
    state->phase = P4_YAHTZEE_NETWORK_WAIT;
    snprintf(name, sizeof(name), "%s-%u-expected", loss_names[kind], survivor);
    capture(instance, width, kind == 1 ? name : NULL, &expected, &other_words);
    state->phase = actual_phase;
    size_t changed = 0, missing_loss = 0;
    for (size_t n = 0; n < words; ++n) {
        changed += (size_t)(before[n] != after[n]);
        missing_loss += (size_t)(expected[n] != after[n]);
    }
    printf("DEPARTURE status=%s role=%s width=%u phase=%d error=%d changed_pixels=%zu loss_screen_diff=%zu\n",
        loss_names[kind], survivor ? "guest" : "host", width, (int)state->phase,
        (int)state->network_error, changed, missing_loss);
    CHECK(state->phase == P4_YAHTZEE_NETWORK_WAIT);
    CHECK(missing_loss == 0U);
    const size_t sends = send_attempts[survivor];
    CHECK(update_button(instance, P4_BUTTON_START));
    printf("POST_LOSS_INPUT role=%s rolls_before=%u rolls_after=%u revision_delta=%u send_attempt_delta=%zu\n",
        survivor ? "guest" : "host", rolls, state->roll_count,
        state->network_revision - revision, send_attempts[survivor] - sends);
    CHECK(state->roll_count == rolls);
    CHECK(state->network_revision == revision);
    CHECK(send_attempts[survivor] == sends);
    /* A late status recovery must not revive the abandoned board. */
    loss_kind[survivor] = 0;
    CHECK(update_empty(instance, 16U));
    CHECK(state->network_error && state->phase == P4_YAHTZEE_NETWORK_WAIT);
    const p4_game_input_t back = {.pressed = P4_BUTTON_BACK, .held = P4_BUTTON_BACK};
    CHECK(p4_game_instance_update(instance, &back, 16U) == P4_GAME_EXIT_TO_LAUNCHER);
    printf("BACK_EXIT role=%s passed\n", survivor ? "guest" : "host");
    p4_game_instance_stop(&instances[0]);
    p4_game_instance_stop(&instances[1]);
    free(before); free(after); free(expected);
}


/* Expanded lifecycle fixture: includes real code and the existing paired queue. */
typedef struct {
    test_link_t link;
    p4_game_instance_t instances[2];
    p4_yahtzee_state_t states[2];
    p4_game_services_t services[2];
} lifecycle_pair_t;

static void pair_start(lifecycle_pair_t *pair, bool pump)
{
    memset(pair, 0, sizeof(*pair));
    memset(loss_kind, 0, sizeof(loss_kind));
    memset(identity_change, 0, sizeof(identity_change));
    memset(send_attempts, 0, sizeof(send_attempts));
    memset(refuse_send, 0, sizeof(refuse_send));
    zero_generation = false;
    init_link(&pair->link, 2U);
    for (uint8_t slot = 0; slot < 2U; ++slot) {
        pair->services[slot] = network_services(&pair->link.endpoints[slot]);
        pair->services[slot].multiplayer_read_status = departure_status;
        pair->services[slot].multiplayer_send = departure_send;
        CHECK(test_start_game(&pair->instances[slot], &p4_p4_yahtzee_game,
            &pair->services[slot], &pair->states[slot], sizeof(pair->states[slot])));
    }
    if (pump) {
        CHECK(update_empty(&pair->instances[0], 16U));
        CHECK(update_empty(&pair->instances[1], 16U));
        CHECK(pair->states[0].network_started && pair->states[1].network_started);
    }
}

static void pair_stop(lifecycle_pair_t *pair)
{
    for (size_t slot = 0; slot < 2U; ++slot) p4_game_instance_stop(&pair->instances[slot]);
}

static void check_world_unchanged(const p4_yahtzee_state_t *before,
                                 const p4_yahtzee_state_t *after)
{
    CHECK(memcmp(before->scores, after->scores, sizeof(before->scores)) == 0);
    CHECK(memcmp(before->dice, after->dice, sizeof(before->dice)) == 0);
    CHECK(memcmp(before->animation_dice, after->animation_dice, sizeof(before->animation_dice)) == 0);
    CHECK(memcmp(before->turns_scored, after->turns_scored, sizeof(before->turns_scored)) == 0);
    CHECK(memcmp(before->last_network_sequence, after->last_network_sequence,
                 sizeof(before->last_network_sequence)) == 0);
    CHECK(before->rng == after->rng && before->roll_rng == after->roll_rng);
    CHECK(before->network_revision == after->network_revision);
    CHECK(before->current_player == after->current_player && before->roll_count == after->roll_count);
    CHECK(before->held_mask == after->held_mask && before->focus == after->focus);
    CHECK(before->selected_die == after->selected_die && before->selected_category == after->selected_category);
    CHECK(before->network_seed == after->network_seed && before->network_role == after->network_role);
    CHECK(before->local_player_slot == after->local_player_slot && before->player_count == after->player_count);
    CHECK(before->network_started == after->network_started);
    CHECK(before->network_bound == after->network_bound);
    CHECK(before->network_generation == after->network_generation);
}

static void queue_request(lifecycle_pair_t *pair, uint8_t recipient, uint8_t kind)
{
    const uint32_t revision = pair->states[recipient].network_revision;
    uint8_t bytes[7] = {P4_YAHTZEE_NETWORK_PROTOCOL, kind,
        (uint8_t)revision, (uint8_t)(revision >> 8U),
        (uint8_t)(revision >> 16U), (uint8_t)(revision >> 24U), 0U};
    CHECK(link_send(&pair->link.endpoints[recipient ^ 1U], bytes, sizeof(bytes)));
}

static void check_terminal(p4_yahtzee_state_t *state)
{
    CHECK(state->network_error && state->phase == P4_YAHTZEE_NETWORK_WAIT);
    CHECK(state->roll_animation_ms == 0U && state->animation_step_ms == 0U);
    CHECK(!state->accessory_pending);
    CHECK(!state->accessory_request.enabled && !state->accessory_request.can_hold);
}

static void identity_case(uint8_t survivor, int field, bool before_snapshot)
{
    lifecycle_pair_t pair;
    pair_start(&pair, !before_snapshot);
    p4_game_instance_t *instance = &pair.instances[survivor];
    p4_yahtzee_state_t *state = &pair.states[survivor];
    if (before_snapshot && survivor == 1U) CHECK(update_empty(&pair.instances[0], 16U));
    else queue_request(&pair, survivor, P4_YAHTZEE_NET_ROLL);
    const size_t queued = pair.link.endpoints[survivor].queue_count;
    CHECK(queued > 0U);
    const p4_yahtzee_state_t before = *state;
    const size_t sends = send_attempts[survivor];
    identity_change[survivor] = field;
    CHECK(update_button(instance, P4_BUTTON_START));
    check_terminal(state);
    check_world_unchanged(&before, state);
    CHECK(pair.link.endpoints[survivor].queue_count == queued);
    CHECK(send_attempts[survivor] == sends);
    identity_change[survivor] = 0;
    CHECK(update_empty(instance, 100U));
    check_terminal(state);
    check_world_unchanged(&before, state);
    CHECK(pair.link.endpoints[survivor].queue_count == queued);
    printf("IDENTITY role=%s field=%d before_first_snapshot=%d terminal_checked\n",
        survivor ? "guest" : "host", field, (int)before_snapshot);
    pair_stop(&pair);
}

typedef struct {
    unsigned calls;
    bool hold;
    p4_dice_request_t request;
} late_dice_t;

static bool late_dice_exchange(void *opaque, const p4_dice_request_t *request,
                               p4_dice_status_t *status)
{
    late_dice_t *fixture = opaque;
    ++fixture->calls;
    fixture->request = *request;
    *status = (p4_dice_status_t){.token=request->token,
        .player_slot=request->player_slot,
        .phase=fixture->hold ? P4_DICE_WAITING : P4_DICE_ROLLED,
        .held_mask=3U, .hold_changed=fixture->hold, .hold_sequence=1U};
    return true;
}

static void terminal_input_case(uint8_t survivor, bool game_over)
{
    lifecycle_pair_t pair;
    pair_start(&pair, true);
    CHECK(update_button(&pair.instances[0], P4_BUTTON_START));
    CHECK(update_empty(&pair.instances[1], 16U));
    p4_game_instance_t *instance = &pair.instances[survivor];
    p4_yahtzee_state_t *state = &pair.states[survivor];
    late_dice_t late_dice = {0};
    instance->services.available_capabilities |= P4_GAME_CAP_DICE_ACCESSORY;
    instance->services.dice_context = &late_dice;
    instance->services.dice_exchange = late_dice_exchange;
    CHECK(state->roll_animation_ms != 0U);
    if (game_over) {
        state->phase = P4_YAHTZEE_GAME_OVER;
        state->roll_animation_ms = state->animation_step_ms = 0U;
    }
    state->accessory_pending = true;
    state->accessory_pending_revision = state->network_revision;
    if (survivor == 0U) {
        queue_request(&pair, survivor, P4_YAHTZEE_NET_RESTART);
    } else {
        /* Replay a real encoded snapshot with fresh sequence/revision. Without
         * the terminal latch this is eligible to replace the guest board. */
        test_endpoint_t *endpoint = &pair.link.endpoints[1];
        p4_game_multiplayer_message_t late = endpoint->queue[
            (endpoint->queue_head + LINK_QUEUE - 1U) % LINK_QUEUE];
        CHECK(late.bytes == 61U && late.data[1] == 16U);
        const uint32_t next = state->network_revision + 1U;
        for (unsigned byte = 0U; byte < 4U; ++byte)
            late.data[2U + byte] = (uint8_t)(next >> (byte * 8U));
        CHECK(link_send(&pair.link.endpoints[0], late.data, late.bytes));
    }
    const size_t queued = pair.link.endpoints[survivor].queue_count;
    const p4_yahtzee_state_t before = *state;
    const size_t sends = send_attempts[survivor];
    loss_kind[survivor] = 1;
    /* The first loss is detected with held touch plus virtual controller input. */
    const p4_game_input_t touch = {.pressed=P4_BUTTON_START | P4_BUTTON_A,
        .touch_valid=true, .touch_count=1U, .touches={{.x=270U,.y=190U}}};
    CHECK(p4_game_instance_update(instance, &touch, 100U) == P4_GAME_CONTINUE);
    check_terminal(state);
    check_world_unchanged(&before, state);
    CHECK(state->touch_was_down);
    CHECK(late_dice.calls > 0U);
    CHECK(!late_dice.request.enabled && !late_dice.request.can_hold);
    late_dice.hold = true;
    const unsigned calls = late_dice.calls;
    CHECK(pair.link.endpoints[survivor].queue_count == queued);
    CHECK(update_empty(instance, 16U));
    CHECK(!state->touch_was_down);
    CHECK(late_dice.calls > calls);
    CHECK(!late_dice.request.enabled && !late_dice.request.can_hold);
    const uint32_t buttons[] = {P4_BUTTON_START, P4_BUTTON_A, P4_BUTTON_B,
        P4_BUTTON_RIGHT, P4_BUTTON_DOWN};
    for (size_t n = 0U; n < sizeof(buttons)/sizeof(buttons[0]); ++n)
        CHECK(update_button(instance, buttons[n]));
    tap(instance, 25U, 45U); tap(instance, 80U, 95U); tap(instance, 270U, 190U);
    for (uint8_t kind = P4_YAHTZEE_NET_ROLL; kind <= P4_YAHTZEE_NET_RESTART; ++kind)
        CHECK(!p4_yahtzee_perform_action(&instance->context, state, kind, 0U));
    loss_kind[survivor] = 0;
    CHECK(update_empty(instance, 16U));
    check_terminal(state);
    check_world_unchanged(&before, state);
    CHECK(send_attempts[survivor] == sends);
    CHECK(pair.link.endpoints[survivor].queue_count == queued);
    const p4_game_input_t exit_touch = {.touch_valid=true,.touch_count=1U,
        .touches={{.x=10U,.y=10U}}};
    CHECK(p4_game_instance_update(instance, &exit_touch, 16U) == P4_GAME_EXIT_TO_LAUNCHER);
    printf("TERMINAL_INPUT role=%s phase=%s world_immutable queue_undrained accessory_disabled touch_exit_checked\n",
        survivor ? "guest" : "host", game_over ? "results" : "animation");
    pair_stop(&pair);
}

static void back_case(uint8_t survivor, int phase)
{
    lifecycle_pair_t pair;
    pair_start(&pair, phase != 0);
    if (phase == 2) {
        loss_kind[survivor] = 1;
        CHECK(update_empty(&pair.instances[survivor], 16U));
    }
    const p4_game_input_t back = {.pressed=P4_BUTTON_BACK};
    const p4_yahtzee_state_t before = pair.states[survivor];
    CHECK(p4_game_instance_update(&pair.instances[survivor], &back, 16U) == P4_GAME_EXIT_TO_LAUNCHER);
    CHECK(memcmp(&before, &pair.states[survivor], sizeof(before)) == 0);
    printf("LINKED_BACK role=%s phase=%d first_press_exit_checked\n", survivor ? "guest" : "host", phase);
    pair_stop(&pair);
}

static void initial_wait_and_send_failure(void)
{
    for (uint8_t slot = 0U; slot < 2U; ++slot) {
        lifecycle_pair_t pair;
        pair_start(&pair, false);
        /* Restart this instance while its service reports an initial WAITING. */
        p4_game_instance_stop(&pair.instances[slot]);
        loss_kind[slot] = 5;
        zero_generation = true;
        CHECK(test_start_game(&pair.instances[slot], &p4_p4_yahtzee_game,
            &pair.services[slot], &pair.states[slot], sizeof(pair.states[slot])));
        CHECK(pair.states[slot].phase == P4_YAHTZEE_MENU);
        CHECK(update_button(&pair.instances[slot], P4_BUTTON_DOWN));
        CHECK(update_button(&pair.instances[slot], P4_BUTTON_A));
        CHECK(update_empty(&pair.instances[slot], 16U));
        CHECK(!pair.states[slot].network_error && !pair.states[slot].network_started);
        CHECK(pair.states[slot].phase == P4_YAHTZEE_NETWORK_WAIT);
        const size_t sends = send_attempts[slot];
        for (uint8_t kind = P4_YAHTZEE_NET_ROLL; kind <= P4_YAHTZEE_NET_RESTART; ++kind)
            CHECK(!p4_yahtzee_perform_action(&pair.instances[slot].context, &pair.states[slot], kind, 0U));
        CHECK(send_attempts[slot] == sends);
        loss_kind[slot] = 0;
        CHECK(update_empty(&pair.instances[slot], 16U));
        CHECK(!pair.states[slot].network_error);
        loss_kind[slot] = 5;
        CHECK(update_empty(&pair.instances[slot], 16U));
        check_terminal(&pair.states[slot]);
        printf("INITIAL_WAIT role=%s unbound_wait generation_zero_connected bound_wait_terminal_checked\n", slot ? "guest" : "host");
        pair_stop(&pair);
    }
    lifecycle_pair_t pair;
    pair_start(&pair, false);
    refuse_send[0] = true;
    CHECK(update_button(&pair.instances[0], P4_BUTTON_START));
    CHECK(!pair.states[0].network_started && !pair.states[0].network_error);
    CHECK(pair.states[0].phase == P4_YAHTZEE_NETWORK_WAIT && pair.states[0].roll_count == 0U);
    const size_t sends = send_attempts[0];
    CHECK(!p4_yahtzee_perform_action(&pair.instances[0].context, &pair.states[0], P4_YAHTZEE_NET_ROLL, 0U));
    CHECK(send_attempts[0] == sends);
    refuse_send[0] = false;
    CHECK(update_empty(&pair.instances[0], 16U));
    CHECK(update_empty(&pair.instances[1], 16U));
    CHECK(pair.states[0].network_started && pair.states[1].network_started);
    check_fresh_match(&pair.states[0], P4_YAHTZEE_NETWORK, 2U);
    printf("INITIAL_SEND_FAILURE host_waits retry_same_session_checked\n");
    pair_stop(&pair);
}

static void same_session_rematch(uint8_t initiator)
{
    lifecycle_pair_t pair;
    pair_start(&pair, true);
    CHECK(update_button(&pair.instances[0], P4_BUTTON_START));
    CHECK(update_empty(&pair.instances[1], 16U));
    settle_roll(&pair.instances[0]); settle_roll(&pair.instances[1]);
    uint32_t sequence_before[2][P4_YAHTZEE_PLAYERS];
    uint32_t revision = pair.states[0].network_revision;
    for (size_t slot = 0; slot < 2U; ++slot) {
        pair.states[slot].phase = P4_YAHTZEE_GAME_OVER;
        pair.states[slot].scores[0][P4_YAHTZEE_ONES] = 3;
        memcpy(sequence_before[slot], pair.states[slot].last_network_sequence, sizeof(sequence_before[slot]));
    }
    CHECK(update_button(&pair.instances[initiator], P4_BUTTON_A));
    CHECK(update_empty(&pair.instances[0], 16U));
    CHECK(update_empty(&pair.instances[1], 16U));
    for (size_t slot = 0; slot < 2U; ++slot) {
        check_fresh_match(&pair.states[slot], P4_YAHTZEE_NETWORK, 2U);
        CHECK(pair.states[slot].network_revision == revision + 1U);
        CHECK(!pair.states[slot].network_error && pair.states[slot].network_started);
        CHECK(pair.states[slot].network_seed == UINT64_C(0x123456789abcdef0));
        for (size_t other = 0; other < P4_YAHTZEE_PLAYERS; ++other)
            CHECK(pair.states[slot].last_network_sequence[other] >= sequence_before[slot][other]);
    }
    CHECK(update_button(&pair.instances[0], P4_BUTTON_START));
    CHECK(update_empty(&pair.instances[1], 16U));
    CHECK(pair.states[0].roll_count == 1U && pair.states[1].roll_count == 1U);
    CHECK(memcmp(pair.states[0].dice, pair.states[1].dice, sizeof(pair.states[0].dice)) == 0);
    identity_change[initiator] = 1;
    CHECK(update_empty(&pair.instances[initiator], 16U));
    check_terminal(&pair.states[initiator]);
    printf("REMATCH initiator=%s same_session_play_and_replay_sequences_preserved generation_change_terminal_checked\n", initiator ? "guest" : "host");
    pair_stop(&pair);
}

static void first_snapshot_roster(void)
{
    lifecycle_pair_t pair;
    pair_start(&pair, false);
    /* A valid but different three-player host roster must not overwrite the guest binding. */
    pair.link.player_count = 3U;
    p4_game_instance_stop(&pair.instances[0]);
    CHECK(test_start_game(&pair.instances[0], &p4_p4_yahtzee_game,
        &pair.services[0], &pair.states[0], sizeof(pair.states[0])));
    CHECK(update_empty(&pair.instances[0], 16U));
    pair.link.player_count = 2U;
    CHECK(update_empty(&pair.instances[1], 16U));
    CHECK(pair.states[1].player_count == 2U && !pair.states[1].network_started);
    CHECK(!pair.states[1].network_error && pair.states[1].phase == P4_YAHTZEE_NETWORK_WAIT);
    p4_game_instance_stop(&pair.instances[0]);
    CHECK(test_start_game(&pair.instances[0], &p4_p4_yahtzee_game,
        &pair.services[0], &pair.states[0], sizeof(pair.states[0])));
    CHECK(update_empty(&pair.instances[0], 16U));
    CHECK(update_empty(&pair.instances[1], 16U));
    CHECK(pair.states[1].network_started && !pair.states[1].network_error);
    check_fresh_match(&pair.states[1], P4_YAHTZEE_NETWORK, 2U);
    printf("FIRST_SNAPSHOT mismatched_roster_rejected_matching_roster_start_checked\n");
    pair_stop(&pair);
}

static void extended_lifecycle(void)
{
    for (uint8_t survivor = 0; survivor < 2U; ++survivor) {
        for (int field = 1; field <= 5; ++field)
            for (int before = 0; before < 2; ++before)
                identity_case(survivor, field, before != 0);
        terminal_input_case(survivor, false);
        terminal_input_case(survivor, true);
        for (int phase = 0; phase < 3; ++phase) back_case(survivor, phase);
        same_session_rematch(survivor);
    }
    initial_wait_and_send_failure();
    first_snapshot_roster();
}

static void test_network_lifecycle(void)
{
    const int failures_before = s_failures;
    for (int kind = 1; kind <= 4; ++kind)
        for (uint8_t survivor = 0; survivor < 2U; ++survivor)
            for (unsigned high = 0; high < 2U; ++high)
                departure_case(kind, survivor, high ? 768U : 320U);
    extended_lifecycle();
    printf("Network lifecycle: 52 cases, %d assertion failures\n", s_failures - failures_before);
}
#endif
