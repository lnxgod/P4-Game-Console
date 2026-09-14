// SPDX-License-Identifier: MIT

#include "p4_rummy_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    NET_KIND_SNAPSHOT_HEADER = 16,
    NET_KIND_SNAPSHOT_CARDS = 17,
    NET_REQUEST_BYTES = 16,
    NET_STATE_OFFSET = 6,
    NET_ROSTER_OFFSET = 7,
    NET_STOCK_OFFSET = 8,
    NET_DISCARD_COUNT_OFFSET = 9,
    NET_HAND_COUNTS_OFFSET = 10,
    NET_TURN_OFFSET = 14,
    NET_ROUND_OFFSET = 16,
    NET_DRAWN_OFFSET = 18,
    NET_REQUIRED_OFFSET = 19,
    NET_MELD_COUNT_OFFSET = 20,
    NET_SCORES_OFFSET = 22,
    NET_MELDS_OFFSET = 30,
    NET_HEADER_USED_BYTES = 46,
    NET_CARDS_COUNT_OFFSET = 6,
    NET_CARDS_OFFSET = 8,
    NET_CARDS_USED_BYTES = NET_CARDS_OFFSET + P4_RUMMY_DECK_CARDS,
    NET_REQUEST_ARGUMENT_OFFSET = 6,
    NET_REQUEST_MELD_OFFSET = 7,
    NET_REQUEST_MASK_OFFSET = 8,
    NET_RECEIVE_LIMIT = 8,
    NET_SYNC_INTERVAL_MS = 1000,
};

_Static_assert(P4_RUMMY_NETWORK_MESSAGE_BYTES <=
                   P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
               "P4 Rummy snapshot exceeds the Game API ceiling");
_Static_assert(NET_HEADER_USED_BYTES <= P4_RUMMY_NETWORK_MESSAGE_BYTES,
               "P4 Rummy snapshot header exceeds 64 bytes");
_Static_assert(NET_CARDS_USED_BYTES <= P4_RUMMY_NETWORK_MESSAGE_BYTES,
               "P4 Rummy card snapshot exceeds 64 bytes");

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
    return bytes[NET_DRAWN_OFFSET];
}

static uint8_t snapshot_meld_count(const uint8_t *bytes)
{
    return bytes[NET_MELD_COUNT_OFFSET];
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

static void write_u64(uint8_t *bytes, uint64_t value)
{
    for (uint8_t index = 0U; index < 8U; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8U));
    }
}

static uint64_t read_u64(const uint8_t *bytes)
{
    uint64_t value = 0U;
    for (uint8_t index = 0U; index < 8U; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8U);
    }
    return value;
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

static bool mark_cards(uint64_t *seen, const uint8_t *cards,
                       uint8_t count)
{
    if (seen == NULL || cards == NULL) {
        return false;
    }
    for (uint8_t index = 0U; index < count; ++index) {
        const uint8_t card = cards[index];
        if (card >= P4_RUMMY_DECK_CARDS ||
            (*seen & (UINT64_C(1) << card)) != 0U) {
            return false;
        }
        *seen |= UINT64_C(1) << card;
    }
    return true;
}

static uint8_t count_marked_cards(uint64_t seen)
{
    uint8_t count = 0U;
    while (seen != 0U) {
        count = (uint8_t)(count + (seen & 1U));
        seen >>= 1U;
    }
    return count;
}

static bool encode_snapshot(
    const p4_rummy_state_t *state,
    uint8_t header[P4_RUMMY_NETWORK_MESSAGE_BYTES],
    uint8_t cards[P4_RUMMY_NETWORK_MESSAGE_BYTES])
{
    if (state == NULL || header == NULL || cards == NULL ||
        state->revision == 0U ||
        state->phase > P4_RUMMY_PHASE_ROUND_OVER ||
        state->player_count < P4_RUMMY_MIN_PLAYERS ||
        state->player_count > P4_RUMMY_MAX_PLAYERS ||
        state->network_player_count < P4_RUMMY_MIN_PLAYERS ||
        state->network_player_count > state->player_count ||
        state->current_player >= state->player_count ||
        state->discard_count > P4_RUMMY_DECK_CARDS ||
        state->meld_count > P4_RUMMY_MAX_MELDS ||
        state->turn_count > P4_RUMMY_TURN_LIMIT) {
        return false;
    }
    memset(header, 0, P4_RUMMY_NETWORK_MESSAGE_BYTES);
    memset(cards, 0, P4_RUMMY_NETWORK_MESSAGE_BYTES);
    header[0] = P4_RUMMY_NETWORK_PROTOCOL;
    header[1] = NET_KIND_SNAPSHOT_HEADER;
    cards[0] = P4_RUMMY_NETWORK_PROTOCOL;
    cards[1] = NET_KIND_SNAPSHOT_CARDS;
    write_u32(header + 2U, state->revision);
    write_u32(cards + 2U, state->revision);
    header[NET_STATE_OFFSET] = (uint8_t)(
        (uint8_t)state->phase |
        (uint8_t)((state->player_count - 2U) << 2U) |
        (uint8_t)((state->network_player_count - 2U) << 4U) |
        (uint8_t)(state->current_player << 6U));
    const uint8_t winner = state->winner == P4_RUMMY_NO_PLAYER
        ? 7U : state->winner;
    header[NET_ROSTER_OFFSET] = (uint8_t)(
        state->cpu_mask | (uint8_t)(winner << 4U));
    header[NET_STOCK_OFFSET] = stock_remaining(state);
    header[NET_DISCARD_COUNT_OFFSET] = state->discard_count;
    write_u16(header + NET_TURN_OFFSET, state->turn_count);
    write_u16(header + NET_ROUND_OFFSET, state->round_number);
    header[NET_DRAWN_OFFSET] = state->drawn_card_index;
    header[NET_REQUIRED_OFFSET] = state->required_meld_card;
    header[NET_MELD_COUNT_OFFSET] = state->meld_count;

    uint64_t seen = 0U;
    uint8_t output = 0U;
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        const uint8_t count = state->hand_counts[player];
        if (count > P4_RUMMY_MAX_HAND_CARDS ||
            (player >= state->player_count && count != 0U) ||
            output + count > P4_RUMMY_DECK_CARDS ||
            !mark_cards(&seen, state->hands[player], count)) {
            return false;
        }
        header[NET_HAND_COUNTS_OFFSET + player] = count;
        memcpy(cards + NET_CARDS_OFFSET + output,
               state->hands[player], count);
        output = (uint8_t)(output + count);
        write_i16(header + NET_SCORES_OFFSET + (size_t)player * 2U,
                  state->scores[player]);
    }
    if (output + state->discard_count > P4_RUMMY_DECK_CARDS ||
        !mark_cards(&seen, state->discard, state->discard_count)) {
        return false;
    }
    memcpy(cards + NET_CARDS_OFFSET + output,
           state->discard, state->discard_count);
    output = (uint8_t)(output + state->discard_count);
    cards[NET_CARDS_COUNT_OFFSET] = output;

    for (uint8_t meld = 0U; meld < state->meld_count; ++meld) {
        uint16_t descriptor = 0U;
        if (!encode_meld(state, meld, &descriptor) ||
            !mark_cards(&seen, state->melds[meld],
                        state->meld_counts[meld])) {
            return false;
        }
        write_u16(header + NET_MELDS_OFFSET + (size_t)meld * 2U,
                  descriptor);
    }
    const uint8_t placed = count_marked_cards(seen);
    if (state->phase == P4_RUMMY_PHASE_SETUP) {
        return placed == 0U && stock_remaining(state) == 0U;
    }
    return (uint8_t)(placed + stock_remaining(state)) ==
        P4_RUMMY_DECK_CARDS;
}

static bool bytes_are_zero(const uint8_t *bytes,
                           size_t begin, size_t end)
{
    for (size_t index = begin; index < end; ++index) {
        if (bytes[index] != 0U) {
            return false;
        }
    }
    return true;
}

static bool snapshot_header_valid(const p4_rummy_state_t *state,
                                  const uint8_t *header)
{
    if (state == NULL || header == NULL ||
        header[0] != P4_RUMMY_NETWORK_PROTOCOL ||
        header[1] != NET_KIND_SNAPSHOT_HEADER ||
        !bytes_are_zero(header, 21U, 22U) ||
        !bytes_are_zero(header, NET_HEADER_USED_BYTES,
                        P4_RUMMY_NETWORK_MESSAGE_BYTES)) {
        return false;
    }
    const uint32_t revision = read_u32(header + 2U);
    const p4_rummy_phase_t phase =
        (p4_rummy_phase_t)snapshot_phase(header);
    const uint8_t players = snapshot_player_count(header);
    const uint8_t network_players = snapshot_network_player_count(header);
    const uint8_t current = snapshot_current_player(header);
    const uint8_t cpu_mask = snapshot_cpu_mask(header);
    const uint8_t winner = snapshot_winner(header);
    const uint8_t drawn_index = snapshot_drawn_index(header);
    const uint8_t required = header[NET_REQUIRED_OFFSET];
    const uint8_t meld_count = snapshot_meld_count(header);
    if (revision == 0U || revision < state->revision ||
        phase > P4_RUMMY_PHASE_ROUND_OVER ||
        players < P4_RUMMY_MIN_PLAYERS ||
        players > P4_RUMMY_MAX_PLAYERS ||
        network_players != state->network_player_count ||
        players < network_players || current >= players ||
        header[NET_STOCK_OFFSET] > P4_RUMMY_DECK_CARDS ||
        header[NET_DISCARD_COUNT_OFFSET] > P4_RUMMY_DECK_CARDS ||
        read_u16(header + NET_TURN_OFFSET) > P4_RUMMY_TURN_LIMIT ||
        meld_count > P4_RUMMY_MAX_MELDS ||
        (header[NET_ROSTER_OFFSET] & 0x80U) != 0U ||
        (required != P4_RUMMY_NO_CARD &&
         required >= P4_RUMMY_DECK_CARDS)) {
        return false;
    }
    uint16_t loose_cards = header[NET_DISCARD_COUNT_OFFSET];
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        const uint8_t count = header[NET_HAND_COUNTS_OFFSET + player];
        if (count > P4_RUMMY_MAX_HAND_CARDS ||
            (player >= players && count != 0U) ||
            ((phase == P4_RUMMY_PHASE_DRAW ||
              phase == P4_RUMMY_PHASE_DISCARD) &&
             player < players && count == 0U)) {
            return false;
        }
        loose_cards = (uint16_t)(loose_cards + count);
    }
    if (loose_cards > P4_RUMMY_DECK_CARDS ||
        (phase == P4_RUMMY_PHASE_SETUP &&
         (loose_cards != 0U || meld_count != 0U ||
          header[NET_STOCK_OFFSET] != 0U)) ||
        (phase == P4_RUMMY_PHASE_DRAW &&
         (drawn_index != P4_RUMMY_NO_CARD ||
          required != P4_RUMMY_NO_CARD)) ||
        (phase != P4_RUMMY_PHASE_DISCARD &&
         drawn_index != P4_RUMMY_NO_CARD) ||
        (phase != P4_RUMMY_PHASE_DISCARD &&
         required != P4_RUMMY_NO_CARD) ||
        (phase == P4_RUMMY_PHASE_DISCARD &&
         drawn_index != P4_RUMMY_NO_CARD &&
         drawn_index >= header[NET_HAND_COUNTS_OFFSET + current])) {
        return false;
    }
    const uint8_t valid_bits = (uint8_t)((UINT8_C(1) << players) - 1U);
    const uint8_t human_bits = (uint8_t)(
        (UINT8_C(1) << network_players) - 1U);
    const uint8_t expected_cpus = (uint8_t)(
        valid_bits & (uint8_t)~human_bits);
    return cpu_mask == expected_cpus &&
        (phase == P4_RUMMY_PHASE_ROUND_OVER
             ? winner < players : winner == P4_RUMMY_NO_PLAYER);
}

static bool snapshot_cards_valid(const uint8_t *header,
                                 const uint8_t *cards)
{
    if (header == NULL || cards == NULL ||
        cards[0] != P4_RUMMY_NETWORK_PROTOCOL ||
        cards[1] != NET_KIND_SNAPSHOT_CARDS ||
        read_u32(cards + 2U) != read_u32(header + 2U) ||
        cards[7] != 0U ||
        !bytes_are_zero(cards, NET_CARDS_USED_BYTES,
                        P4_RUMMY_NETWORK_MESSAGE_BYTES)) {
        return false;
    }
    uint8_t expected = header[NET_DISCARD_COUNT_OFFSET];
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        expected = (uint8_t)(expected +
            header[NET_HAND_COUNTS_OFFSET + player]);
    }
    if (cards[NET_CARDS_COUNT_OFFSET] != expected) {
        return false;
    }
    if (!bytes_are_zero(cards, NET_CARDS_OFFSET + expected,
                        P4_RUMMY_NETWORK_MESSAGE_BYTES)) {
        return false;
    }
    uint64_t seen = 0U;
    if (!mark_cards(&seen, cards + NET_CARDS_OFFSET, expected)) {
        return false;
    }
    const uint8_t players = snapshot_player_count(header);
    const uint8_t meld_count = snapshot_meld_count(header);
    for (uint8_t meld = 0U; meld < P4_RUMMY_MAX_MELDS; ++meld) {
        const uint16_t descriptor = read_u16(
            header + NET_MELDS_OFFSET + (size_t)meld * 2U);
        if (meld >= meld_count) {
            if (descriptor != 0U) {
                return false;
            }
            continue;
        }
        uint8_t meld_cards[P4_RUMMY_MAX_MELD_CARDS];
        uint8_t count = 0U;
        uint8_t owner = 0U;
        if (!decode_meld(descriptor, players, meld_cards,
                         &count, &owner) ||
            !mark_cards(&seen, meld_cards, count)) {
            return false;
        }
        (void)owner;
    }
    const p4_rummy_phase_t phase =
        (p4_rummy_phase_t)snapshot_phase(header);
    const uint8_t placed = count_marked_cards(seen);
    if (phase == P4_RUMMY_PHASE_SETUP) {
        return placed == 0U;
    }
    if ((uint8_t)(placed + header[NET_STOCK_OFFSET]) !=
        P4_RUMMY_DECK_CARDS) {
        return false;
    }
    const uint8_t required = header[NET_REQUIRED_OFFSET];
    if (required != P4_RUMMY_NO_CARD) {
        const uint8_t current = snapshot_current_player(header);
        uint8_t offset = 0U;
        for (uint8_t player = 0U; player < current; ++player) {
            offset = (uint8_t)(offset +
                header[NET_HAND_COUNTS_OFFSET + player]);
        }
        const uint8_t drawn = snapshot_drawn_index(header);
        if (drawn == P4_RUMMY_NO_CARD ||
            cards[NET_CARDS_OFFSET + offset + drawn] != required) {
            return false;
        }
    }
    return true;
}

static bool apply_snapshot(p4_rummy_state_t *state,
                           const uint8_t *header,
                           const uint8_t *cards)
{
    if (!snapshot_header_valid(state, header) ||
        !snapshot_cards_valid(header, cards)) {
        return false;
    }
    const uint32_t incoming_revision = read_u32(header + 2U);
    const p4_rummy_phase_t incoming_phase =
        (p4_rummy_phase_t)snapshot_phase(header);
    const uint8_t incoming_current_player =
        snapshot_current_player(header);
    const bool preserve_selection =
        state->network_started &&
        incoming_revision == state->revision &&
        state->phase == P4_RUMMY_PHASE_DISCARD &&
        incoming_phase == P4_RUMMY_PHASE_DISCARD &&
        state->current_player == state->local_player_slot &&
        incoming_current_player == state->local_player_slot;
    const bool preserve_draw_choice =
        state->network_started &&
        incoming_revision == state->revision &&
        state->phase == P4_RUMMY_PHASE_DRAW &&
        incoming_phase == P4_RUMMY_PHASE_DRAW &&
        state->current_player == state->local_player_slot &&
        incoming_current_player == state->local_player_slot;
    const uint8_t prior_selected_card = state->selected_card;
    const uint8_t prior_selected_discard = state->selected_discard;
    const uint8_t prior_selected_meld = state->selected_meld;
    const uint64_t prior_selected_mask = state->selected_mask;
    const p4_rummy_draw_source_t prior_draw_source = state->draw_source;

    state->revision = incoming_revision;
    state->phase = incoming_phase;
    state->player_count = snapshot_player_count(header);
    state->current_player = incoming_current_player;
    state->cpu_mask = snapshot_cpu_mask(header);
    state->winner = snapshot_winner(header);
    state->deck_count = P4_RUMMY_DECK_CARDS;
    state->deck_index = (uint8_t)(
        P4_RUMMY_DECK_CARDS - header[NET_STOCK_OFFSET]);
    memset(state->deck, P4_RUMMY_NO_CARD, sizeof(state->deck));
    memset(state->hands, P4_RUMMY_NO_CARD, sizeof(state->hands));
    uint8_t input = 0U;
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        state->hand_counts[player] =
            header[NET_HAND_COUNTS_OFFSET + player];
        state->scores[player] = read_i16(
            header + NET_SCORES_OFFSET + (size_t)player * 2U);
        memcpy(state->hands[player], cards + NET_CARDS_OFFSET + input,
               state->hand_counts[player]);
        input = (uint8_t)(input + state->hand_counts[player]);
    }
    state->discard_count = header[NET_DISCARD_COUNT_OFFSET];
    memset(state->discard, P4_RUMMY_NO_CARD, sizeof(state->discard));
    memcpy(state->discard, cards + NET_CARDS_OFFSET + input,
           state->discard_count);
    memset(state->melds, P4_RUMMY_NO_CARD, sizeof(state->melds));
    memset(state->meld_counts, 0, sizeof(state->meld_counts));
    memset(state->meld_owners, P4_RUMMY_NO_PLAYER,
           sizeof(state->meld_owners));
    state->meld_count = snapshot_meld_count(header);
    for (uint8_t meld = 0U; meld < state->meld_count; ++meld) {
        if (!decode_meld(read_u16(header + NET_MELDS_OFFSET +
                                   (size_t)meld * 2U),
                         state->player_count, state->melds[meld],
                         &state->meld_counts[meld],
                         &state->meld_owners[meld])) {
            return false;
        }
    }
    state->turn_count = read_u16(header + NET_TURN_OFFSET);
    state->round_number = read_u16(header + NET_ROUND_OFFSET);
    state->drawn_card_index = snapshot_drawn_index(header);
    state->required_meld_card = header[NET_REQUIRED_OFFSET];
    state->selected_card = 0U;
    state->selected_mask = 0U;
    state->selected_meld = P4_RUMMY_NO_CARD;
    state->draw_source = P4_RUMMY_DRAW_STOCK;
    state->selected_discard = state->discard_count == 0U
        ? P4_RUMMY_NO_CARD : (uint8_t)(state->discard_count - 1U);
    if (state->phase == P4_RUMMY_PHASE_DISCARD &&
        state->current_player == state->local_player_slot) {
        state->selected_card = (uint8_t)(
            state->hand_counts[state->current_player] - 1U);
        if (state->required_meld_card != P4_RUMMY_NO_CARD &&
            state->drawn_card_index <
                state->hand_counts[state->current_player]) {
            state->selected_card = state->drawn_card_index;
            state->selected_mask =
                UINT64_C(1) << state->drawn_card_index;
        }
        if (preserve_selection) {
            const uint8_t hand_count =
                state->hand_counts[state->current_player];
            const uint64_t valid_mask = hand_count == 64U
                ? UINT64_MAX
                : (UINT64_C(1) << hand_count) - UINT64_C(1);
            state->selected_mask =
                prior_selected_mask & valid_mask;
            if (prior_selected_card < hand_count) {
                state->selected_card = prior_selected_card;
            }
            if (prior_selected_meld == P4_RUMMY_NO_CARD ||
                prior_selected_meld < state->meld_count) {
                state->selected_meld = prior_selected_meld;
            }
        }
    } else if (state->phase == P4_RUMMY_PHASE_DRAW &&
               state->current_player == state->local_player_slot &&
               preserve_draw_choice) {
        state->draw_source = prior_draw_source;
        if (prior_selected_discard < state->discard_count) {
            state->selected_discard = prior_selected_discard;
        }
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
    uint8_t header[P4_RUMMY_NETWORK_MESSAGE_BYTES];
    uint8_t cards[P4_RUMMY_NETWORK_MESSAGE_BYTES];
    return encode_snapshot(state, header, cards) &&
        p4_game_multiplayer_send(context, header, sizeof(header)) &&
        p4_game_multiplayer_send(context, cards, sizeof(cards));
}

static bool send_request(p4_game_context_t *context,
                         const p4_rummy_state_t *state,
                         uint8_t kind, uint8_t argument,
                         uint8_t meld_index, uint64_t selection_mask)
{
    uint8_t bytes[NET_REQUEST_BYTES] = {0};
    bytes[0] = P4_RUMMY_NETWORK_PROTOCOL;
    bytes[1] = kind;
    write_u32(bytes + 2U, state->revision);
    bytes[NET_REQUEST_ARGUMENT_OFFSET] = argument;
    bytes[NET_REQUEST_MELD_OFFSET] = meld_index;
    write_u64(bytes + NET_REQUEST_MASK_OFFSET, selection_mask);
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
                           p4_rummy_draw_source_t source,
                           uint8_t discard_index)
{
    if (state == NULL || !p4_rummy_local_turn(state) ||
        state->phase != P4_RUMMY_PHASE_DRAW ||
        (source == P4_RUMMY_DRAW_DISCARD &&
         discard_index >= state->discard_count)) {
        return false;
    }
    if (state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        const uint8_t kind = source == P4_RUMMY_DRAW_DISCARD
            ? P4_RUMMY_NET_DRAW_DISCARD : P4_RUMMY_NET_DRAW_STOCK;
        if (!send_request(context, state, kind, discard_index,
                          P4_RUMMY_NO_CARD, 0U)) {
            return false;
        }
        state->network_request_pending = true;
        return true;
    }
    if (!p4_rummy_draw(state, state->current_player,
                       source, discard_index)) {
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
                          hand_index, P4_RUMMY_NO_CARD, 0U)) {
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
                           uint64_t selection_mask)
{
    return p4_rummy_perform_meld_to(
        context, state, selection_mask, P4_RUMMY_NO_CARD);
}

bool p4_rummy_perform_meld_to(p4_game_context_t *context,
                              p4_rummy_state_t *state,
                              uint64_t selection_mask,
                              uint8_t meld_index)
{
    if (state == NULL || !p4_rummy_local_turn(state) ||
        state->phase != P4_RUMMY_PHASE_DISCARD ||
        selection_mask == 0U) {
        return false;
    }
    if (state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        if (!send_request(context, state, P4_RUMMY_NET_PLAY_MELD,
                          0U, meld_index, selection_mask)) {
            return false;
        }
        state->network_request_pending = true;
        return true;
    }
    if (!p4_rummy_play_meld_to(
            state, state->current_player, selection_mask, meld_index)) {
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
                                P4_RUMMY_DRAW_STOCK,
                                P4_RUMMY_NO_CARD);
        if (changed) {
            play_draw_tone(context);
        }
    } else if (kind == P4_RUMMY_NET_DRAW_DISCARD) {
        changed = p4_rummy_draw(state, message->player_slot,
                                P4_RUMMY_DRAW_DISCARD,
                                message->data[NET_REQUEST_ARGUMENT_OFFSET]);
        if (changed) {
            play_draw_tone(context);
        }
    } else if (kind == P4_RUMMY_NET_DISCARD) {
        changed = p4_rummy_discard_card(
            state, message->player_slot,
            message->data[NET_REQUEST_ARGUMENT_OFFSET]);
        if (changed) {
            play_discard_tone(
                context, state->phase == P4_RUMMY_PHASE_ROUND_OVER);
        }
    } else if (kind == P4_RUMMY_NET_PLAY_MELD) {
        changed = p4_rummy_play_meld_to(
            state, message->player_slot,
            read_u64(message->data + NET_REQUEST_MASK_OFFSET),
            message->data[NET_REQUEST_MELD_OFFSET]);
        if (changed) {
            play_meld_tone(
                context, state->phase == P4_RUMMY_PHASE_ROUND_OVER);
        }
    }
    if (changed) {
        state->network_snapshot_dirty = true;
    }
}

static void client_handle_snapshot_part(
    p4_rummy_state_t *state,
    const p4_game_multiplayer_message_t *message)
{
    if (state == NULL || message == NULL ||
        message->bytes != P4_RUMMY_NETWORK_MESSAGE_BYTES ||
        message->data[0] != P4_RUMMY_NETWORK_PROTOCOL) {
        return;
    }
    const uint32_t revision = read_u32(message->data + 2U);
    if (revision == 0U || revision < state->revision) {
        return;
    }
    if (message->data[1] == NET_KIND_SNAPSHOT_HEADER) {
        memcpy(state->network_header, message->data,
               P4_RUMMY_NETWORK_MESSAGE_BYTES);
        state->network_header_revision = revision;
        state->network_header_ready = true;
    } else if (message->data[1] == NET_KIND_SNAPSHOT_CARDS) {
        memcpy(state->network_cards, message->data,
               P4_RUMMY_NETWORK_MESSAGE_BYTES);
        state->network_cards_revision = revision;
        state->network_cards_ready = true;
    } else {
        return;
    }
    if (!state->network_header_ready || !state->network_cards_ready) {
        return;
    }
    if (state->network_header_revision != state->network_cards_revision) {
        if (state->network_header_revision < state->network_cards_revision) {
            state->network_header_ready = false;
        } else {
            state->network_cards_ready = false;
        }
        return;
    }
    (void)apply_snapshot(
        state, state->network_header, state->network_cards);
    state->network_header_ready = false;
    state->network_cards_ready = false;
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
        } else {
            client_handle_snapshot_part(state, &message);
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
               send_request(context, state, 0U, 0U,
                            P4_RUMMY_NO_CARD, 0U)) {
        state->network_retry_ms = 0U;
    }
}
