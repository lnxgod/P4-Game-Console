#include "doom_gamepad/input.h"

#include <stddef.h>
#include <string.h>

_Static_assert(DOOM_GAMEPAD_ACTION_COUNT <= 16,
               "Doom gamepad actions must fit the bounded event queue");
_Static_assert(DOOM_GAMEPAD_EVENT_CAPACITY >= (2U * DOOM_GAMEPAD_ACTION_COUNT),
               "event queue must hold every release and press transition");

static bool input_valid(const doom_gamepad_input_t *input)
{
    return input != NULL && input->version == DOOM_GAMEPAD_INPUT_VERSION &&
           input->size == sizeof(*input) &&
           input->read_index < DOOM_GAMEPAD_EVENT_CAPACITY &&
           input->event_count <= DOOM_GAMEPAD_EVENT_CAPACITY;
}

static uint32_t action_bit(doom_gamepad_action_t action)
{
    return UINT32_C(1) << (unsigned)action;
}

static bool button_pressed(const gamepad_state_t *state, gamepad_button_t button)
{
    return (state->buttons & GAMEPAD_BUTTON_MASK(button)) != 0U;
}

static uint32_t desired_actions(const gamepad_state_t *state)
{
    if (state == NULL || state->connected == 0U) {
        return 0U;
    }

    uint32_t actions = 0U;
    const bool up = (state->dpad & GAMEPAD_DPAD_UP) != 0U ||
                    state->left_y <= -DOOM_GAMEPAD_STICK_THRESHOLD;
    const bool down = (state->dpad & GAMEPAD_DPAD_DOWN) != 0U ||
                      state->left_y >= DOOM_GAMEPAD_STICK_THRESHOLD;
    const bool left = (state->dpad & GAMEPAD_DPAD_LEFT) != 0U ||
                      state->left_x <= -DOOM_GAMEPAD_STICK_THRESHOLD;
    const bool right = (state->dpad & GAMEPAD_DPAD_RIGHT) != 0U ||
                       state->left_x >= DOOM_GAMEPAD_STICK_THRESHOLD;

    if (up != down) {
        actions |= action_bit(up ? DOOM_GAMEPAD_ACTION_UP
                                 : DOOM_GAMEPAD_ACTION_DOWN);
    }
    if (left != right) {
        actions |= action_bit(left ? DOOM_GAMEPAD_ACTION_LEFT
                                   : DOOM_GAMEPAD_ACTION_RIGHT);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_SOUTH) ||
        state->right_trigger >= DOOM_GAMEPAD_TRIGGER_THRESHOLD) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_FIRE);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_SOUTH)) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_MENU_ACCEPT);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_EAST)) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_USE);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_WEST) ||
        button_pressed(state, GAMEPAD_BUTTON_LEFT_STICK)) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_RUN);
    }
    if (state->left_trigger >= DOOM_GAMEPAD_TRIGGER_THRESHOLD) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_STRAFE);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_STRAFE_LEFT);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_STRAFE_RIGHT);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_BACK)) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_MENU_BACK);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_GUIDE)) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_MAP);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_NORTH)) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_WEAPON_NEXT);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_RIGHT_STICK)) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_WEAPON_PREVIOUS);
    }
    if (button_pressed(state, GAMEPAD_BUTTON_START)) {
        actions |= action_bit(DOOM_GAMEPAD_ACTION_PAUSE);
    }
    return actions;
}

static void enqueue(
    doom_gamepad_input_t *input,
    doom_gamepad_action_t action,
    bool pressed
)
{
    const unsigned write_index =
        ((unsigned)input->read_index + (unsigned)input->event_count) %
        DOOM_GAMEPAD_EVENT_CAPACITY;
    input->events[write_index] = (doom_gamepad_event_t){
        .action = (uint8_t)action,
        .pressed = pressed ? 1U : 0U,
        .reserved = {0U, 0U},
    };
    ++input->event_count;
}

void doom_gamepad_input_init(doom_gamepad_input_t *input)
{
    if (input == NULL) {
        return;
    }
    memset(input, 0, sizeof(*input));
    input->version = DOOM_GAMEPAD_INPUT_VERSION;
    input->size = (uint16_t)sizeof(*input);
}

gamepad_status_t doom_gamepad_input_update(
    doom_gamepad_input_t *input,
    const gamepad_state_t *state
)
{
    if (!input_valid(input)) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }
    if (input->event_count != 0U) {
        return GAMEPAD_ERR_INVALID_STATE;
    }

    gamepad_status_t status = GAMEPAD_OK;
    uint32_t desired = 0U;
    if (state == NULL || state->version != GAMEPAD_STATE_VERSION ||
        state->size != sizeof(*state) || state->connected > 1U) {
        status = GAMEPAD_ERR_INVALID_ARGUMENT;
    } else {
        desired = desired_actions(state);
    }

    const uint32_t released = input->active_actions & ~desired;
    const uint32_t pressed = desired & ~input->active_actions;
    for (unsigned action = 0U; action < DOOM_GAMEPAD_ACTION_COUNT; ++action) {
        if ((released & action_bit((doom_gamepad_action_t)action)) != 0U) {
            enqueue(input, (doom_gamepad_action_t)action, false);
        }
    }
    for (unsigned action = 0U; action < DOOM_GAMEPAD_ACTION_COUNT; ++action) {
        if ((pressed & action_bit((doom_gamepad_action_t)action)) != 0U) {
            enqueue(input, (doom_gamepad_action_t)action, true);
        }
    }
    input->active_actions = desired;
    return status;
}

bool doom_gamepad_input_next(
    doom_gamepad_input_t *input,
    doom_gamepad_event_t *event
)
{
    if (!input_valid(input) || event == NULL || input->event_count == 0U) {
        return false;
    }
    *event = input->events[input->read_index];
    input->read_index = (uint8_t)(
        ((unsigned)input->read_index + 1U) % DOOM_GAMEPAD_EVENT_CAPACITY
    );
    --input->event_count;
    return true;
}

bool doom_gamepad_input_idle(const doom_gamepad_input_t *input)
{
    return input_valid(input) && input->event_count == 0U;
}
