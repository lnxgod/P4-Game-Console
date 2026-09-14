// SPDX-License-Identifier: MIT

#include "p4_rummy_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    NET_KIND_SNAPSHOT = 16,
    NET_REQUEST_BYTES = 8,
    NET_STATE_OFFSET = 6,
    NET_ROSTER_OFFSET = 7,
    NET_DISCARD_OFFSET = 8,
    NET_STOCK_OFFSET = 9,
    NET_HAND_COUNTS_OFFSET = 10,
    NET_HANDS_OFFSET = 12,
    NET_PACKED_HAND_BYTES = 24,
    NET_TURN_OFFSET = 36,
    NET_ROUND_OFFSET = 37,
    NET_DRAW_MELD_OFFSET = 39,
    NET_SCORES_OFFSET = 40,
    NET_MELDS_OFFSET = 48,
    NET_PACKED_NO_CARD = 63,
    NET_RECEIVE_LIMIT = 8,
    NET_SYNC_INTERVAL_MS = 1000,
};

_Static_assert(P4_RUMMY_NETWORK_MESSAGE_BYTES <=
                   P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
               "P4 Rummy snapshot exceeds the Game API ceiling");
_Static_assert(NET_HANDS_OFFSET + NET_PACKED_HAND_BYTES == NET_TURN_OFFSET,
               "P4 Rummy packed hands overlap snapshot metadata");
_Static_assert(NET_MELDS_OFFSET + P4_RUMMY_MAX_MELDS * 2U ==
                   P4_RUMMY_NETWORK_MESSAGE_BYTES,
               "P4 Rummy snapshot layout must fill exactly 64 bytes");

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] |
        (uint16_t)((uint16_t)bytes[1] << 8U);
}

static void write_i16(uint8_t *bytes, int16_t value)
{
    write_u16(bytes, (uint16_t)value);
}

static int16_t read_i16(const uint8_t *bytes)
{
    return (int16_t)read_u16(bytes);
}

static void write_hand_count(uint8_t *bytes, uint8_t player, uint8_t count)
{
    const uint8_t shift = (uint8_t)((player & 1U) * 4U);
    bytes[player / 2U] |= (uint8_t)(count << shift);
}

static uint8_t read_hand_count(const uint8_t *bytes, uint8_t player)
{
    const uint8_t shift = (uint8_t)((player & 1U) * 4U);
    return (uint8_t)((bytes[player / 2U] >> shift) & 0x0fU);
}

static uint8_t snapshot_phase(const uint8_t *bytes)
{
    return (uint8_t)(bytes[NET_STATE_OFFSET] & 0x03U);
}

static uint8_t snapshot_player_count(const uint8_t *bytes)
{
    return (uint8_t)(((bytes[NET_STATE_OFFSET] >> 2U) & 0x03U) + 2U);
}

static uint8_t snapshot_network_player_count(const uint8_t *bytes)
{
    return (uint8_t)(((bytes[NET_STATE_OFFSET] >> 4U) & 0x03U) + 2U);
}

static uint8_t snapshot_current_player(const uint8_t *bytes)
{
    return (uint8_t)(bytes[NET_STATE_OFFSET] >> 6U);
}

static uint8_t snapshot_cpu_mask(const uint8_t *bytes)
{
    return (uint8_t)(bytes[NET_ROSTER_OFFSET] & 0x0fU);
}

static uint8_t snapshot_winner(const uint8_t *bytes)
{
    const uint8_t winner = (uint8_t)(
        (bytes[NET_ROSTER_OFFSET] >> 4U) & 0x07U);
    return winner == 7U ? P4_RUMMY_NO_PLAYER : winner;
}

static uint8_t snapshot_drawn_index(const uint8_t *bytes)
{
    const uint8_t drawn = (uint8_t)(bytes[NET_DRAW_MELD_OFFSET] & 0x0fU);
    return drawn == 0x0fU ? P4_RUMMY_NO_CARD : drawn;
}

static uint8_t snapshot_meld_count(const uint8_t *bytes)
{
    return (uint8_t)(bytes[NET_DRAW_MELD_OFFSET] >> 4U);
}

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        (uint32_t)bytes[1] << 8U |
        (uint32_t)bytes[2] << 16U |
        (uint32_t)bytes[3] << 24U;
}

static void write_packed_card(uint8_t *bytes, uint8_t slot, uint8_t card)
{
    const uint16_t bit = (uint16_t)slot * 6U;
    for (uint8_t part = 0U; part < 6U; ++part) {
        if ((card & (UINT8_C(1) << part)) != 0U) {
            const uint16_t output_bit = (uint16_t)(bit + part);
            bytes[output_bit / 8U] |=
                (uint8_t)(UINT8_C(1) << (output_bit % 8U));
        }
    }
}

static uint8_t read_packed_card(const uint8_t *bytes, uint8_t slot)
{
    const uint16_t bit = (uint16_t)slot * 6U;
    uint8_t card = 0U;
    for (uint8_t part = 0U; part < 6U; ++part) {
        const uint16_t input_bit = (uint16_t)(bit + part);
        if ((bytes[input_bit / 8U] &
             (UINT8_C(1) << (input_bit % 8U))) != 0U) {
            card |= (uint8_t)(UINT8_C(1) << part);
        }
    }
    return card;
}

static uint8_t card_rank(uint8_t card)
{
    return (uint8_t)(card % 13U);
}

static uint8_t card_suit(uint8_t card)
{
    return (uint8_t)(card / 13U);
}

static uint8_t bit_count(uint16_t value)
{
    uint8_t count = 0U;
    while (value != 0U) {
        count = (uint8_t)(count + (value & 1U));
        value >>= 1U;
    }
    return count;
}

static bool encode_meld(const p4_rummy_state_t *state, uint8_t meld,
                        uint16_t *descriptor_out)
{
    if (state == NULL || descriptor_out == NULL ||
        meld >= state->meld_count ||
        state->meld_counts[meld] < 3U ||
        state->meld_counts[meld] > P4_RUMMY_MAX_MELD_CARDS ||
        state->meld_owners[meld] >= state->player_count ||
        !p4_rummy_meld_is_valid(
            state->melds[meld], state->meld_counts[meld])) {
        return false;
    }
    const uint8_t count = state->meld_counts[meld];
    const uint8_t first = state->melds[meld][0];
    bool same_rank = true;
    bool same_suit = true;
    uint16_t ranks = 0U;
    uint8_t suits = 0U;
    uint8_t minimum = 13U;
    for (uint8_t index = 0U; index < count; ++index) {
        const uint8_t card = state->melds[meld][index];
        same_rank = same_rank && card_rank(card) == card_rank(first);
        same_suit = same_suit && card_suit(card) == card_suit(first);
        ranks |= (uint16_t)(UINT16_C(1) << card_rank(card));
        suits |= (uint8_t)(UINT8_C(1) << card_suit(card));
        if (card_rank(card) < minimum) {
            minimum = card_rank(card);
        }
    }
    const uint16_t owner = (uint16_t)(
        (uint16_t)state->meld_owners[meld] << 13U);
    if (same_rank) {
        *descriptor_out = (uint16_t)(owner |
            ((uint16_t)card_rank(first) << 9U) |
            ((uint16_t)suits << 5U));
        return true;
    }
    if (!same_suit) {
        return false;
    }
    uint16_t ace_low_ranks = UINT16_C(1) << 12U;
    for (uint8_t rank = 0U; rank + 1U < count; ++rank) {
        ace_low_ranks |= (uint16_t)(UINT16_C(1) << rank);
    }
    const bool ace_low = ranks == ace_low_ranks;
    const uint8_t start = ace_low ? 12U : minimum;
    uint16_t descriptor = (uint16_t)(UINT16_C(0x8000) | owner);
    descriptor = (uint16_t)(descriptor |
        (uint16_t)((uint16_t)card_suit(first) << 11U));
    descriptor = (uint16_t)(descriptor |
        (uint16_t)((uint16_t)start << 7U));
    descriptor = (uint16_t)(descriptor |
        (uint16_t)((uint16_t)count << 3U));
    if (ace_low) {
        descriptor = (uint16_t)(descriptor | UINT16_C(0x0004));
    }
    *descriptor_out = descriptor;
    return true;
}

static bool decode_meld(uint16_t descriptor, uint8_t player_count,
                        uint8_t cards[P4_RUMMY_MAX_MELD_CARDS],
                        uint8_t *count_out, uint8_t *owner_out)
{
    if (cards == NULL || count_out == NULL || owner_out == NULL) {
        return false;
    }
    const uint8_t owner = (uint8_t)((descriptor >> 13U) & 0x03U);
    if (owner >= player_count) {
        return false;
    }
    uint8_t count = 0U;
    if ((descriptor & UINT16_C(0x8000)) == 0U) {
        const uint8_t rank = (uint8_t)((descriptor >> 9U) & 0x0fU);
        const uint8_t suits = (uint8_t)((descriptor >> 5U) & 0x0fU);
        if ((descriptor & UINT16_C(0x001f)) != 0U || rank >= 13U ||
            bit_count(suits) < 3U) {
            return false;
        }
        for (uint8_t suit = 0U; suit < 4U; ++suit) {
            if ((suits & (UINT8_C(1) << suit)) != 0U) {
                cards[count++] = (uint8_t)(suit * 13U + rank);
            }
        }
    } else {
        const uint8_t suit = (uint8_t)((descriptor >> 11U) & 0x03U);
        const uint8_t start = (uint8_t)((descriptor >> 7U) & 0x0fU);
        count = (uint8_t)((descriptor >> 3U) & 0x0fU);
        const bool ace_low = (descriptor & UINT16_C(0x0004)) != 0U;
        if ((descriptor & UINT16_C(0x0003)) != 0U || start >= 13U ||
            count < 3U || count > 13U ||
            (ace_low && start != 12U) ||
            (!ace_low && (uint8_t)(start + count) > 13U)) {
            return false;
        }
        for (uint8_t index = 0U; index < count; ++index) {
            const uint8_t rank = ace_low && index != 0U
                ? (uint8_t)(index - 1U) : (uint8_t)(start + index);
            cards[index] = (uint8_t)(suit * 13U + rank);
        }
    }
    if (!p4_rummy_meld_is_valid(cards, count)) {
        return false;
    }
    *count_out = count;
    *owner_out = owner;
    return true;
}

static uint8_t stock_remaining(const p4_rummy_state_t *state)
{
    return state->deck_index <= state->deck_count
        ? (uint8_t)(state->deck_count - state->deck_index) : 0U;
}

static bool encode_snapshot(
    const p4_rummy_state_t *state,
    uint8_t bytes[P4_RUMMY_NETWORK_MESSAGE_BYTES])
{
    if (state == NULL || bytes == NULL || state->revision == 0U ||
        state->phase > P4_RUMMY_PHASE_ROUND_OVER ||
        state->player_count < P4_RUMMY_MIN_PLAYERS ||
        state->player_count > P4_RUMMY_MAX_PLAYERS ||
        state->network_player_count < P4_RUMMY_MIN_PLAYERS ||
        state->network_player_count > state->player_count ||
        state->current_player >= state->player_count) {
        return false;
    }
    memset(bytes, 0, P4_RUMMY_NETWORK_MESSAGE_BYTES);
    bytes[0] = P4_RUMMY_NETWORK_PROTOCOL;
    bytes[1] = NET_KIND_SNAPSHOT;
    write_u32(bytes + 2U, state->revision);
    bytes[NET_STATE_OFFSET] = (uint8_t)(
        (uint8_t)state->phase |
        (uint8_t)((state->player_count - 2U) << 2U) |
        (uint8_t)((state->network_player_count - 2U) << 4U) |
        (uint8_t)(state->current_player << 6U));
    const uint8_t winner = state->winner == P4_RUMMY_NO_PLAYER
        ? 7U : state->winner;
    bytes[NET_ROSTER_OFFSET] = (uint8_t)(
        state->cpu_mask | (uint8_t)(winner << 4U));
    bytes[NET_DISCARD_OFFSET] = state->discard_count == 0U ? P4_RUMMY_NO_CARD
        : state->discard[state->discard_count - 1U];
    bytes[NET_STOCK_OFFSET] = stock_remaining(state);
    memset(bytes + NET_HAND_COUNTS_OFFSET, 0, 2U);
    memset(bytes + NET_HANDS_OFFSET, 0, NET_PACKED_HAND_BYTES);
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        write_hand_count(bytes + NET_HAND_COUNTS_OFFSET, player,
                         state->hand_counts[player]);
        for (uint8_t index = 0U; index < P4_RUMMY_DRAWN_CARDS; ++index) {
            const uint8_t card = index < state->hand_counts[player]
                ? state->hands[player][index] : NET_PACKED_NO_CARD;
            write_packed_card(bytes + NET_HANDS_OFFSET,
                (uint8_t)(player * P4_RUMMY_DRAWN_CARDS + index), card);
        }
    }
    bytes[NET_TURN_OFFSET] = (uint8_t)state->turn_count;
    write_u16(bytes + NET_ROUND_OFFSET, state->round_number);
    const uint8_t drawn = state->drawn_card_index == P4_RUMMY_NO_CARD
        ? 0x0fU : state->drawn_card_index;
    bytes[NET_DRAW_MELD_OFFSET] = (uint8_t)(
        drawn | (uint8_t)(state->meld_count << 4U));
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        write_i16(bytes + NET_SCORES_OFFSET + (size_t)player * 2U,
                  state->scores[player]);
    }
    for (uint8_t meld = 0U; meld < state->meld_count; ++meld) {
        uint16_t descriptor = 0U;
        if (!encode_meld(state, meld, &descriptor)) {
            return false;
        }
        write_u16(bytes + NET_MELDS_OFFSET + (size_t)meld * 2U,
                  descriptor);
    }
    return true;
}

static bool snapshot_cards_valid(const uint8_t *bytes,
                                 p4_rummy_phase_t phase,
                                 uint8_t player_count,
                                 uint8_t current_player)
{
    uint64_t seen = 0U;
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        const uint8_t count = read_hand_count(
            bytes + NET_HAND_COUNTS_OFFSET, player);
        if (count > P4_RUMMY_DRAWN_CARDS ||
            (player >= player_count && count != 0U) ||
            (phase == P4_RUMMY_PHASE_SETUP && count != 0U) ||
            (phase == P4_RUMMY_PHASE_DRAW && player < player_count &&
             (count == 0U || count >= P4_RUMMY_DRAWN_CARDS)) ||
            (phase == P4_RUMMY_PHASE_DISCARD && player < player_count &&
             (count == 0U ||
              (player != current_player &&
               count >= P4_RUMMY_DRAWN_CARDS)))) {
            return false;
        }
        for (uint8_t index = 0U; index < P4_RUMMY_DRAWN_CARDS; ++index) {
            const uint8_t card = read_packed_card(
                bytes + NET_HANDS_OFFSET,
                (uint8_t)(player * P4_RUMMY_DRAWN_CARDS + index));
            if (index >= count) {
                if (card != NET_PACKED_NO_CARD) {
                    return false;
                }
                continue;
            }
            if (card >= P4_RUMMY_DECK_CARDS ||
                (seen & (UINT64_C(1) << card)) != 0U) {
                return false;
            }
            seen |= UINT64_C(1) << card;
        }
    }
    const uint8_t meld_count = snapshot_meld_count(bytes);
    if (meld_count > P4_RUMMY_MAX_MELDS ||
        (phase == P4_RUMMY_PHASE_SETUP && meld_count != 0U)) {
        return false;
    }
    for (uint8_t meld = 0U; meld < P4_RUMMY_MAX_MELDS; ++meld) {
        const uint16_t descriptor = read_u16(
            bytes + NET_MELDS_OFFSET + (size_t)meld * 2U);
        if (meld >= meld_count) {
            if (descriptor != 0U) {
                return false;
            }
            continue;
        }
        uint8_t cards[P4_RUMMY_MAX_MELD_CARDS];
        uint8_t count = 0U;
        uint8_t owner = 0U;
        if (!decode_meld(descriptor, player_count,
                         cards, &count, &owner)) {
            return false;
        }
        (void)owner;
        for (uint8_t index = 0U; index < count; ++index) {
            const uint8_t card = cards[index];
            if ((seen & (UINT64_C(1) << card)) != 0U) {
                return false;
            }
            seen |= UINT64_C(1) << card;
        }
    }
    const uint8_t top = bytes[NET_DISCARD_OFFSET];
    if (phase == P4_RUMMY_PHASE_SETUP) {
        return top == P4_RUMMY_NO_CARD;
    }
    if (top == P4_RUMMY_NO_CARD) {
        return phase == P4_RUMMY_PHASE_DISCARD;
    }
    return top < P4_RUMMY_DECK_CARDS &&
        (seen & (UINT64_C(1) << top)) == 0U;
}

static bool snapshot_valid(const p4_rummy_state_t *state,
                           const uint8_t *bytes, size_t byte_count)
{
    if (state == NULL || bytes == NULL ||
        byte_count != P4_RUMMY_NETWORK_MESSAGE_BYTES ||
        bytes[0] != P4_RUMMY_NETWORK_PROTOCOL ||
        bytes[1] != NET_KIND_SNAPSHOT) {
        return false;
    }
    const uint32_t revision = read_u32(bytes + 2U);
    const p4_rummy_phase_t phase =
        (p4_rummy_phase_t)snapshot_phase(bytes);
    const uint8_t players = snapshot_player_count(bytes);
    const uint8_t network_players = snapshot_network_player_count(bytes);
    const uint8_t current = snapshot_current_player(bytes);
    const uint8_t cpu_mask = snapshot_cpu_mask(bytes);
    const uint8_t winner = snapshot_winner(bytes);
    const uint8_t drawn_index = snapshot_drawn_index(bytes);
    if (revision == 0U || revision < state->revision ||
        phase > P4_RUMMY_PHASE_ROUND_OVER ||
        players < P4_RUMMY_MIN_PLAYERS ||
        players > P4_RUMMY_MAX_PLAYERS ||
        network_players != state->network_player_count ||
        players < network_players || current >= players ||
        bytes[NET_STOCK_OFFSET] > P4_RUMMY_DECK_CARDS ||
        bytes[NET_TURN_OFFSET] > P4_RUMMY_TURN_LIMIT ||
        (bytes[NET_ROSTER_OFFSET] & 0x80U) != 0U ||
        (phase == P4_RUMMY_PHASE_DISCARD
             ? (drawn_index != P4_RUMMY_NO_CARD &&
                drawn_index >=
                    read_hand_count(bytes + NET_HAND_COUNTS_OFFSET,
                                    current))
             : drawn_index != P4_RUMMY_NO_CARD)) {
        return false;
    }
    const uint8_t valid_bits = (uint8_t)((UINT8_C(1) << players) - 1U);
    const uint8_t human_bits = (uint8_t)(
        (UINT8_C(1) << network_players) - 1U);
    const uint8_t expected_cpus = (uint8_t)(
        valid_bits & (uint8_t)~human_bits);
    if (cpu_mask != expected_cpus ||
        (phase == P4_RUMMY_PHASE_ROUND_OVER
             ? winner >= players : winner != P4_RUMMY_NO_PLAYER)) {
        return false;
    }
    return snapshot_cards_valid(bytes, phase, players, current);
}

static bool apply_snapshot(p4_rummy_state_t *state,
                           const uint8_t *bytes, size_t byte_count)
{
    if (!snapshot_valid(state, bytes, byte_count)) {
        return false;
    }
    state->revision = read_u32(bytes + 2U);
    state->phase = (p4_rummy_phase_t)snapshot_phase(bytes);
    state->player_count = snapshot_player_count(bytes);
    state->current_player = snapshot_current_player(bytes);
    state->cpu_mask = snapshot_cpu_mask(bytes);
    state->winner = snapshot_winner(bytes);
    state->discard_count = bytes[NET_DISCARD_OFFSET] == P4_RUMMY_NO_CARD
        ? 0U : 1U;
    memset(state->discard, P4_RUMMY_NO_CARD, sizeof(state->discard));
    if (state->discard_count != 0U) {
        state->discard[0] = bytes[NET_DISCARD_OFFSET];
    }
    state->deck_count = P4_RUMMY_DECK_CARDS;
    state->deck_index = (uint8_t)(
        P4_RUMMY_DECK_CARDS - bytes[NET_STOCK_OFFSET]);
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        state->hand_counts[player] = read_hand_count(
            bytes + NET_HAND_COUNTS_OFFSET, player);
        state->scores[player] = read_i16(
            bytes + NET_SCORES_OFFSET + (size_t)player * 2U);
        for (uint8_t index = 0U; index < P4_RUMMY_DRAWN_CARDS; ++index) {
            const uint8_t card = read_packed_card(
                bytes + NET_HANDS_OFFSET,
                (uint8_t)(player * P4_RUMMY_DRAWN_CARDS + index));
            state->hands[player][index] = index < state->hand_counts[player]
                ? card : P4_RUMMY_NO_CARD;
        }
    }
    memset(state->melds, P4_RUMMY_NO_CARD, sizeof(state->melds));
    memset(state->meld_counts, 0, sizeof(state->meld_counts));
    memset(state->meld_owners, P4_RUMMY_NO_PLAYER,
           sizeof(state->meld_owners));
    state->meld_count = snapshot_meld_count(bytes);
    for (uint8_t meld = 0U; meld < state->meld_count; ++meld) {
        if (!decode_meld(read_u16(bytes + NET_MELDS_OFFSET +
                                  (size_t)meld * 2U),
                         state->player_count, state->melds[meld],
                         &state->meld_counts[meld],
                         &state->meld_owners[meld])) {
            return false;
        }
    }
    state->turn_count = bytes[NET_TURN_OFFSET];
    state->round_number = read_u16(bytes + NET_ROUND_OFFSET);
    state->drawn_card_index = snapshot_drawn_index(bytes);
    state->selected_card = 0U;
    state->selected_mask = 0U;
    if (state->phase == P4_RUMMY_PHASE_DISCARD &&
        state->current_player == state->local_player_slot) {
        state->selected_card = (uint8_t)(
            state->hand_counts[state->current_player] - 1U);
    }
    state->network_started = true;
    state->network_request_pending = false;
    state->network_retry_ms = 0U;
    state->cpu_think_ms = 0U;
    return true;
}

static bool send_snapshot(p4_game_context_t *context,
                          const p4_rummy_state_t *state)
{
    uint8_t bytes[P4_RUMMY_NETWORK_MESSAGE_BYTES];
    return encode_snapshot(state, bytes) &&
        p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static bool send_request(p4_game_context_t *context,
                         const p4_rummy_state_t *state,
                         uint8_t kind, uint8_t argument)
{
    uint8_t bytes[NET_REQUEST_BYTES] = {
        P4_RUMMY_NETWORK_PROTOCOL, kind, 0U, 0U, 0U, 0U,
        argument, 0U,
    };
    write_u32(bytes + 2U, state->revision);
    return p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static void play_draw_tone(p4_game_context_t *context)
{
    (void)p4_game_play_tone(context, 440U, 35U, 2U, P4_WAVE_TRIANGLE);
}

static void play_discard_tone(p4_game_context_t *context, bool won)
{
    (void)p4_game_play_tone(context, won ? 880U : 587U,
                            won ? 140U : 45U, won ? 5U : 3U,
                            P4_WAVE_SQUARE);
}

static void play_meld_tone(p4_game_context_t *context, bool won)
{
    (void)p4_game_play_tone(context, won ? 988U : 784U,
                            won ? 180U : 85U, won ? 5U : 4U,
                            P4_WAVE_TRIANGLE);
}

void p4_rummy_mark_snapshot_dirty(p4_rummy_state_t *state)
{
    if (state != NULL && state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        state->network_snapshot_dirty = true;
    }
}

bool p4_rummy_network_begin(p4_game_context_t *context,
                            p4_rummy_state_t *state)
{
    if (context == NULL || state == NULL || context->services == NULL ||
        (context->services->available_capabilities &
         P4_GAME_CAP_MULTIPLAYER_SESSION) == 0U) {
        return false;
    }
    p4_game_multiplayer_profile_t profile;
    p4_game_multiplayer_status_t status;
    if (!p4_game_multiplayer_read_profile(context, &profile) ||
        profile.style != P4_GAME_MULTIPLAYER_STYLE_TURN_BASED ||
        profile.min_players != P4_RUMMY_MIN_PLAYERS ||
        profile.max_players != P4_RUMMY_MAX_PLAYERS ||
        profile.message_bytes != P4_RUMMY_NETWORK_MESSAGE_BYTES ||
        profile.protocol != P4_RUMMY_NETWORK_PROTOCOL ||
        !p4_game_multiplayer_read_status(context, &status) ||
        status.state != P4_GAME_MULTIPLAYER_CONNECTED ||
        status.player_count < P4_RUMMY_MIN_PLAYERS ||
        status.player_count > P4_RUMMY_MAX_PLAYERS ||
        status.local_player_slot >= status.player_count ||
        (status.role != P4_GAME_MULTIPLAYER_ROLE_HOST &&
         status.role != P4_GAME_MULTIPLAYER_ROLE_CLIENT)) {
        return false;
    }
    const uint32_t seed = (uint32_t)status.session_seed ^
        (uint32_t)(status.session_seed >> 32U);
    p4_rummy_reset_lobby(state, status.player_count, seed);
    state->network_mode = true;
    state->network_role = status.role;
    state->network_player_count = status.player_count;
    state->local_player_slot = status.local_player_slot;
    state->session_seed = status.session_seed;
    state->network_started = status.role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    state->network_snapshot_dirty = state->network_started;
    state->human_player_count = status.player_count;
    state->cpu_mask = 0U;
    return true;
}

static void fall_back_to_local(p4_rummy_state_t *state)
{
    const uint32_t seed = state->rng ^ UINT32_C(0x4c4f5354);
    p4_rummy_reset_lobby(state, 1U, seed);
    state->network_error = true;
    state->peer_lost_fallback = true;
}

bool p4_rummy_perform_draw(p4_game_context_t *context,
                           p4_rummy_state_t *state,
                           p4_rummy_draw_source_t source)
{
    if (state == NULL || !p4_rummy_local_turn(state) ||
        state->phase != P4_RUMMY_PHASE_DRAW) {
        return false;
    }
    if (state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        const uint8_t kind = source == P4_RUMMY_DRAW_DISCARD
            ? P4_RUMMY_NET_DRAW_DISCARD : P4_RUMMY_NET_DRAW_STOCK;
        if (!send_request(context, state, kind, 0U)) {
            return false;
        }
        state->network_request_pending = true;
        return true;
    }
    if (!p4_rummy_draw(state, state->current_player, source)) {
        return false;
    }
    play_draw_tone(context);
    p4_rummy_mark_snapshot_dirty(state);
    return true;
}

bool p4_rummy_perform_discard(p4_game_context_t *context,
                              p4_rummy_state_t *state,
                              uint8_t hand_index)
{
    if (state == NULL || !p4_rummy_local_turn(state) ||
        state->phase != P4_RUMMY_PHASE_DISCARD ||
        hand_index >= state->hand_counts[state->current_player] ||
        hand_index == state->drawn_card_index) {
        return false;
    }
    if (state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        if (!send_request(context, state, P4_RUMMY_NET_DISCARD,
                          hand_index)) {
            return false;
        }
        state->network_request_pending = true;
        return true;
    }
    if (!p4_rummy_discard_card(
            state, state->current_player, hand_index)) {
        return false;
    }
    play_discard_tone(context,
                      state->phase == P4_RUMMY_PHASE_ROUND_OVER);
    p4_rummy_mark_snapshot_dirty(state);
    return true;
}

bool p4_rummy_perform_meld(p4_game_context_t *context,
                           p4_rummy_state_t *state,
                           uint8_t selection_mask)
{
    if (state == NULL || !p4_rummy_local_turn(state) ||
        state->phase != P4_RUMMY_PHASE_DISCARD ||
        selection_mask == 0U) {
        return false;
    }
    if (state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        if (!send_request(context, state, P4_RUMMY_NET_PLAY_MELD,
                          selection_mask)) {
            return false;
        }
        state->network_request_pending = true;
        return true;
    }
    if (!p4_rummy_play_meld(
            state, state->current_player, selection_mask)) {
        return false;
    }
    play_meld_tone(context,
                   state->phase == P4_RUMMY_PHASE_ROUND_OVER);
    p4_rummy_mark_snapshot_dirty(state);
    return true;
}

bool p4_rummy_request_new_round(p4_game_context_t *context,
                                p4_rummy_state_t *state)
{
    (void)context;
    if (state == NULL || state->phase != P4_RUMMY_PHASE_ROUND_OVER ||
        (state->network_mode &&
         state->network_role != P4_GAME_MULTIPLAYER_ROLE_HOST)) {
        return false;
    }
    if (p4_rummy_match_winner(state) != P4_RUMMY_NO_PLAYER) {
        memset(state->scores, 0, sizeof(state->scores));
        memset(state->round_scores, 0, sizeof(state->round_scores));
        state->round_number = 0U;
    }
    if (!p4_rummy_begin_round(state)) {
        return false;
    }
    p4_rummy_mark_snapshot_dirty(state);
    return true;
}

static void host_handle_request(
    p4_game_context_t *context, p4_rummy_state_t *state,
    const p4_game_multiplayer_message_t *message)
{
    if (message->bytes != NET_REQUEST_BYTES ||
        message->data[0] != P4_RUMMY_NETWORK_PROTOCOL) {
        return;
    }
    const uint8_t kind = message->data[1];
    if (kind == 0U) {
        state->network_snapshot_dirty = true;
        return;
    }
    if (read_u32(message->data + 2U) != state->revision ||
        message->player_slot != state->current_player) {
        state->network_snapshot_dirty = true;
        return;
    }
    bool changed = false;
    if (kind == P4_RUMMY_NET_DRAW_STOCK) {
        changed = p4_rummy_draw(state, message->player_slot,
                                P4_RUMMY_DRAW_STOCK);
        if (changed) {
            play_draw_tone(context);
        }
    } else if (kind == P4_RUMMY_NET_DRAW_DISCARD) {
        changed = p4_rummy_draw(state, message->player_slot,
                                P4_RUMMY_DRAW_DISCARD);
        if (changed) {
            play_draw_tone(context);
        }
    } else if (kind == P4_RUMMY_NET_DISCARD) {
        changed = p4_rummy_discard_card(
            state, message->player_slot, message->data[6]);
        if (changed) {
            play_discard_tone(
                context, state->phase == P4_RUMMY_PHASE_ROUND_OVER);
        }
    } else if (kind == P4_RUMMY_NET_PLAY_MELD) {
        changed = p4_rummy_play_meld(
            state, message->player_slot, message->data[6]);
        if (changed) {
            play_meld_tone(
                context, state->phase == P4_RUMMY_PHASE_ROUND_OVER);
        }
    }
    if (changed) {
        state->network_snapshot_dirty = true;
    }
}

void p4_rummy_network_poll(p4_game_context_t *context,
                           p4_rummy_state_t *state,
                           uint32_t elapsed_ms)
{
    if (context == NULL || state == NULL || !state->network_mode) {
        return;
    }
    p4_game_multiplayer_status_t status;
    if (!p4_game_multiplayer_read_status(context, &status) ||
        status.state == P4_GAME_MULTIPLAYER_PEER_LEFT ||
        status.state == P4_GAME_MULTIPLAYER_ERROR ||
        status.state == P4_GAME_MULTIPLAYER_OFFLINE) {
        fall_back_to_local(state);
        return;
    }
    if (status.state != P4_GAME_MULTIPLAYER_CONNECTED) {
        return;
    }
    if (status.role != state->network_role ||
        status.player_count != state->network_player_count ||
        status.local_player_slot != state->local_player_slot) {
        fall_back_to_local(state);
        return;
    }
    if (UINT32_MAX - state->network_retry_ms < elapsed_ms) {
        state->network_retry_ms = NET_SYNC_INTERVAL_MS;
    } else {
        state->network_retry_ms += elapsed_ms;
    }

    p4_game_multiplayer_message_t message;
    for (size_t received = 0U;
         received < NET_RECEIVE_LIMIT &&
         p4_game_multiplayer_receive(context, &message);
         ++received) {
        if (message.player_slot >= state->network_player_count ||
            message.player_slot == state->local_player_slot ||
            message.sequence == 0U ||
            message.sequence <=
                state->last_network_sequence[message.player_slot]) {
            continue;
        }
        state->last_network_sequence[message.player_slot] = message.sequence;
        if (state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
            host_handle_request(context, state, &message);
        } else if (message.bytes == P4_RUMMY_NETWORK_MESSAGE_BYTES) {
            (void)apply_snapshot(state, message.data, message.bytes);
        }
    }

    if (state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        if ((state->network_snapshot_dirty ||
             state->network_retry_ms >= NET_SYNC_INTERVAL_MS) &&
            send_snapshot(context, state)) {
            state->network_snapshot_dirty = false;
            state->network_retry_ms = 0U;
        }
    } else if ((!state->network_started || state->network_request_pending) &&
               state->network_retry_ms >= NET_SYNC_INTERVAL_MS &&
               send_request(context, state, 0U, 0U)) {
        state->network_retry_ms = 0U;
    }
}
