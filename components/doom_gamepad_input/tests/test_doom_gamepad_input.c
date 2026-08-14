#include "doom_gamepad/input.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define EXPECT_TRUE(value_) do { \
    if (!(value_)) { \
        fprintf(stderr, "%s:%d expectation failed: %s\n", \
                __FILE__, __LINE__, #value_); \
        ++failures; \
    } \
} while (0)

#define EXPECT_EQ(expected_, actual_) do { \
    const long long expected_value = (long long)(expected_); \
    const long long actual_value = (long long)(actual_); \
    if (expected_value != actual_value) { \
        fprintf(stderr, "%s:%d expected %lld, got %lld\n", \
                __FILE__, __LINE__, expected_value, actual_value); \
        ++failures; \
    } \
} while (0)

static gamepad_state_t connected_state(void)
{
    gamepad_state_t state;
    gamepad_state_init(&state);
    EXPECT_EQ(GAMEPAD_OK, gamepad_state_connect(&state, 1U));
    return state;
}

static unsigned drain(
    doom_gamepad_input_t *input,
    doom_gamepad_event_t *events,
    unsigned capacity
)
{
    unsigned count = 0U;
    while (count < capacity && doom_gamepad_input_next(input, &events[count])) {
        ++count;
    }
    return count;
}

static void test_mapping_and_disconnect_release(void)
{
    doom_gamepad_input_t input;
    doom_gamepad_input_init(&input);
    gamepad_state_t state = connected_state();
    state.dpad = GAMEPAD_DPAD_UP | GAMEPAD_DPAD_LEFT;
    state.buttons = GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_WEST) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_NORTH) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_START) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_BACK);
    EXPECT_EQ(GAMEPAD_OK, doom_gamepad_input_update(&input, &state));

    doom_gamepad_event_t events[DOOM_GAMEPAD_EVENT_CAPACITY];
    const unsigned pressed_count = drain(&input, events, DOOM_GAMEPAD_EVENT_CAPACITY);
    EXPECT_EQ(9, pressed_count);
    for (unsigned index = 0U; index < pressed_count; ++index) {
        EXPECT_EQ(1, events[index].pressed);
    }

    EXPECT_EQ(GAMEPAD_OK, gamepad_state_disconnect(&state, 2U));
    EXPECT_EQ(GAMEPAD_OK, doom_gamepad_input_update(&input, &state));
    const unsigned released_count = drain(&input, events, DOOM_GAMEPAD_EVENT_CAPACITY);
    EXPECT_EQ(pressed_count, released_count);
    for (unsigned index = 0U; index < released_count; ++index) {
        EXPECT_EQ(0, events[index].pressed);
    }
    EXPECT_TRUE(doom_gamepad_input_idle(&input));
}

static void test_thresholds_and_conflicts(void)
{
    doom_gamepad_input_t input;
    doom_gamepad_input_init(&input);
    gamepad_state_t state = connected_state();
    state.left_x = DOOM_GAMEPAD_STICK_THRESHOLD - 1;
    state.left_y = (int16_t)(-DOOM_GAMEPAD_STICK_THRESHOLD + 1);
    state.right_trigger = DOOM_GAMEPAD_TRIGGER_THRESHOLD - 1U;
    EXPECT_EQ(GAMEPAD_OK, doom_gamepad_input_update(&input, &state));
    EXPECT_TRUE(doom_gamepad_input_idle(&input));

    state.left_x = DOOM_GAMEPAD_STICK_THRESHOLD;
    state.left_y = -DOOM_GAMEPAD_STICK_THRESHOLD;
    state.left_trigger = DOOM_GAMEPAD_TRIGGER_THRESHOLD;
    state.right_trigger = DOOM_GAMEPAD_TRIGGER_THRESHOLD;
    EXPECT_EQ(GAMEPAD_OK, doom_gamepad_input_update(&input, &state));
    doom_gamepad_event_t events[8];
    EXPECT_EQ(4, drain(&input, events, 8U));

    state.dpad = GAMEPAD_DPAD_UP | GAMEPAD_DPAD_DOWN |
                 GAMEPAD_DPAD_LEFT | GAMEPAD_DPAD_RIGHT;
    state.left_x = 0;
    state.left_y = 0;
    state.left_trigger = 0U;
    state.right_trigger = 0U;
    EXPECT_EQ(GAMEPAD_OK, doom_gamepad_input_update(&input, &state));
    EXPECT_EQ(4, drain(&input, events, 8U));
}

static void test_extended_control_mapping(void)
{
    doom_gamepad_input_t input;
    doom_gamepad_input_init(&input);
    gamepad_state_t state = connected_state();
    state.buttons = GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_LEFT_SHOULDER) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_RIGHT_SHOULDER) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_GUIDE) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_LEFT_STICK) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_RIGHT_STICK) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_NORTH) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_START);
    EXPECT_EQ(GAMEPAD_OK, doom_gamepad_input_update(&input, &state));

    uint32_t seen = 0U;
    doom_gamepad_event_t event;
    unsigned count = 0U;
    while (doom_gamepad_input_next(&input, &event)) {
        EXPECT_EQ(1, event.pressed);
        seen |= UINT32_C(1) << event.action;
        ++count;
    }
    EXPECT_EQ(7, count);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_GAMEPAD_ACTION_STRAFE_LEFT)) != 0U);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_GAMEPAD_ACTION_STRAFE_RIGHT)) != 0U);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_GAMEPAD_ACTION_MAP)) != 0U);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_GAMEPAD_ACTION_RUN)) != 0U);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_GAMEPAD_ACTION_WEAPON_NEXT)) != 0U);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_GAMEPAD_ACTION_WEAPON_PREVIOUS)) != 0U);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_GAMEPAD_ACTION_PAUSE)) != 0U);
}

static void test_queue_contract_and_invalid_neutralization(void)
{
    doom_gamepad_input_t input;
    doom_gamepad_input_init(&input);
    gamepad_state_t state = connected_state();
    state.buttons = GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH);
    EXPECT_EQ(GAMEPAD_OK, doom_gamepad_input_update(&input, &state));
    EXPECT_EQ(GAMEPAD_ERR_INVALID_STATE,
              doom_gamepad_input_update(&input, &state));

    doom_gamepad_event_t event;
    EXPECT_TRUE(doom_gamepad_input_next(&input, &event));
    EXPECT_EQ(DOOM_GAMEPAD_ACTION_FIRE, event.action);
    EXPECT_EQ(1, event.pressed);
    EXPECT_TRUE(doom_gamepad_input_next(&input, &event));
    EXPECT_EQ(DOOM_GAMEPAD_ACTION_MENU_ACCEPT, event.action);
    EXPECT_EQ(1, event.pressed);

    state.version = 0U;
    EXPECT_EQ(GAMEPAD_ERR_INVALID_ARGUMENT,
              doom_gamepad_input_update(&input, &state));
    EXPECT_TRUE(doom_gamepad_input_next(&input, &event));
    EXPECT_EQ(DOOM_GAMEPAD_ACTION_FIRE, event.action);
    EXPECT_EQ(0, event.pressed);
    EXPECT_TRUE(doom_gamepad_input_next(&input, &event));
    EXPECT_EQ(DOOM_GAMEPAD_ACTION_MENU_ACCEPT, event.action);
    EXPECT_EQ(0, event.pressed);
    EXPECT_TRUE(doom_gamepad_input_idle(&input));

    memset(&input, 0, sizeof(input));
    EXPECT_EQ(GAMEPAD_ERR_INVALID_ARGUMENT,
              doom_gamepad_input_update(&input, &state));
}

int main(void)
{
    test_mapping_and_disconnect_release();
    test_thresholds_and_conflicts();
    test_extended_control_mapping();
    test_queue_contract_and_invalid_neutralization();
    if (failures != 0) {
        fprintf(stderr, "%d Doom gamepad input test(s) failed\n", failures);
        return 1;
    }
    puts("P4_DOOM_GAMEPAD_INPUT HOST PASS disconnect_releases=true latest_snapshot=true");
    return 0;
}
