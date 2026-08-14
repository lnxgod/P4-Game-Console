#include "gamepad/gamepad.h"

#include <string.h>

_Static_assert(sizeof(gamepad_state_t) <= UINT16_MAX,
               "gamepad_state_t size must fit its public size field");

static bool state_header_valid(const gamepad_state_t *state)
{
    return state != NULL && state->version == GAMEPAD_STATE_VERSION &&
           state->size == sizeof(*state) && state->connected <= 1U;
}

static void neutralize_controls(gamepad_state_t *state)
{
    state->buttons = 0;
    state->left_x = 0;
    state->left_y = 0;
    state->right_x = 0;
    state->right_y = 0;
    state->left_trigger = 0;
    state->right_trigger = 0;
    state->dpad = GAMEPAD_DPAD_CENTERED;
}

void gamepad_state_init(gamepad_state_t *state)
{
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->version = GAMEPAD_STATE_VERSION;
    state->size = (uint16_t)sizeof(*state);
}

gamepad_status_t gamepad_state_connect(gamepad_state_t *state, uint64_t timestamp_us)
{
    if (!state_header_valid(state)) {
        return GAMEPAD_ERR_INVALID_STATE;
    }

    neutralize_controls(state);
    state->connected = 1U;
    state->timestamp_us = timestamp_us;
    state->sequence++;
    return GAMEPAD_OK;
}

gamepad_status_t gamepad_state_disconnect(gamepad_state_t *state, uint64_t timestamp_us)
{
    if (!state_header_valid(state)) {
        return GAMEPAD_ERR_INVALID_STATE;
    }

    neutralize_controls(state);
    state->connected = 0U;
    state->timestamp_us = timestamp_us;
    state->sequence++;
    return GAMEPAD_OK;
}

bool gamepad_state_is_neutral(const gamepad_state_t *state)
{
    return state_header_valid(state) && state->buttons == 0 && state->left_x == 0 &&
           state->left_y == 0 && state->right_x == 0 && state->right_y == 0 &&
           state->left_trigger == 0 && state->right_trigger == 0 &&
           state->dpad == GAMEPAD_DPAD_CENTERED;
}

const char *gamepad_status_name(gamepad_status_t status)
{
    switch (status) {
    case GAMEPAD_OK:
        return "ok";
    case GAMEPAD_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case GAMEPAD_ERR_INVALID_STATE:
        return "invalid state";
    case GAMEPAD_ERR_DESCRIPTOR_TOO_LARGE:
        return "descriptor too large";
    case GAMEPAD_ERR_REPORT_TOO_LARGE:
        return "report too large";
    case GAMEPAD_ERR_TRUNCATED:
        return "truncated data";
    case GAMEPAD_ERR_MALFORMED:
        return "malformed data";
    case GAMEPAD_ERR_LIMIT_EXCEEDED:
        return "limit exceeded";
    case GAMEPAD_ERR_UNSUPPORTED:
        return "unsupported data";
    case GAMEPAD_ERR_NO_GAMEPAD:
        return "no gamepad collection";
    case GAMEPAD_ERR_NO_MAPPABLE_INPUT:
        return "no mappable input";
    case GAMEPAD_ERR_REPORT_ID:
        return "unknown report id";
    case GAMEPAD_ERR_REPORT_SIZE:
        return "invalid report size";
    case GAMEPAD_ERR_DISCONNECTED:
        return "gamepad disconnected";
    default:
        return "unknown status";
    }
}
