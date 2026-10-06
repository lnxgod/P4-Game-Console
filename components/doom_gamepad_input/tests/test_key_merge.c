// SPDX-License-Identifier: MIT
#include "doom_gamepad/key_merge.h"
#include "doom_gamepad/input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_overlapping_keys(void)
{
    /* Both arrival/release orders and the complete engine key-code range. */
    for (unsigned key = 0; key < 256; ++key) {
        for (unsigned first = 0; first < DOOM_KEY_SOURCE_COUNT; ++first) {
            doom_key_merge_t input;
            doom_key_merge_init(&input);
            const doom_key_source_t a = (doom_key_source_t)first;
            const doom_key_source_t b = (doom_key_source_t)(1U - first);
            assert(doom_key_merge_update(&input, a, (uint8_t)key, true));
            assert(!doom_key_merge_update(&input, a, (uint8_t)key, true));
            assert(!doom_key_merge_update(&input, b, (uint8_t)key, true));
            assert(!doom_key_merge_update(&input, a, (uint8_t)key, false));
            assert(input.owners[key] != 0U);
            assert(!doom_key_merge_update(&input, a, (uint8_t)key, false));
            assert(doom_key_merge_update(&input, b, (uint8_t)key, false));
            assert(input.owners[key] == 0U);
        }
    }
}

static void test_disconnect_and_reconnect(void)
{
    doom_key_merge_t merged;
    doom_key_merge_init(&merged);
    doom_gamepad_input_t adapter;
    doom_gamepad_input_init(&adapter);
    gamepad_state_t state;
    gamepad_state_init(&state);
    assert(gamepad_state_connect(&state, 1U) == GAMEPAD_OK);
    state.dpad = GAMEPAD_DPAD_UP;
    assert(doom_gamepad_input_update(&adapter, &state) == GAMEPAD_OK);
    doom_gamepad_event_t event;
    assert(doom_gamepad_input_next(&adapter, &event));
    assert(event.action == DOOM_GAMEPAD_ACTION_UP && event.pressed == 1U);
    assert(doom_key_merge_update(&merged, DOOM_KEY_SOURCE_GAMEPAD, 128U, true));
    assert(!doom_key_merge_update(&merged, DOOM_KEY_SOURCE_TOUCH, 128U, true));

    gamepad_state_init(&state); /* Transport disconnect supplies neutral state. */
    assert(doom_gamepad_input_update(&adapter, &state) == GAMEPAD_OK);
    assert(doom_gamepad_input_next(&adapter, &event));
    assert(event.action == DOOM_GAMEPAD_ACTION_UP && event.pressed == 0U);
    assert(!doom_key_merge_update(&merged, DOOM_KEY_SOURCE_GAMEPAD, 128U, false));
    assert(doom_key_merge_update(&merged, DOOM_KEY_SOURCE_TOUCH, 128U, false));

    assert(gamepad_state_connect(&state, 2U) == GAMEPAD_OK);
    state.dpad = GAMEPAD_DPAD_UP;
    assert(doom_gamepad_input_update(&adapter, &state) == GAMEPAD_OK);
    assert(doom_gamepad_input_next(&adapter, &event));
    assert(event.pressed == 1U);
    assert(doom_key_merge_update(&merged, DOOM_KEY_SOURCE_GAMEPAD, 128U, true));
    gamepad_state_init(&state);
    assert(doom_gamepad_input_update(&adapter, &state) == GAMEPAD_OK);
    assert(doom_gamepad_input_next(&adapter, &event));
    assert(event.pressed == 0U);
    assert(doom_key_merge_update(&merged, DOOM_KEY_SOURCE_GAMEPAD, 128U, false));
}

int main(void)
{
    test_overlapping_keys();
    test_disconnect_and_reconnect();
    doom_key_merge_t input;
    doom_key_merge_init(&input);
    assert(doom_key_merge_update(&input, DOOM_KEY_SOURCE_TOUCH, 1U, true));
    assert(doom_key_merge_update(&input, DOOM_KEY_SOURCE_GAMEPAD, 2U, true));
    assert(doom_key_merge_update(&input, DOOM_KEY_SOURCE_TOUCH, 1U, false));
    assert(input.owners[2] != 0U);
    doom_key_merge_t before = input;
    assert(!doom_key_merge_update(&input, (doom_key_source_t)-1, 2U, false));
    assert(!doom_key_merge_update(&input, DOOM_KEY_SOURCE_COUNT, 2U, false));
    assert(memcmp(&before, &input, sizeof(input)) == 0);
    assert(!doom_key_merge_update(NULL, DOOM_KEY_SOURCE_TOUCH, 0U, true));
    doom_key_merge_init(&input);
    for (unsigned key = 0; key < 256; ++key) assert(input.owners[key] == 0U);
    puts("Doom touch/gamepad ownership, disconnect and reconnect: PASS");
    return 0;
}
