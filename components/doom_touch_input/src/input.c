#include "doom_touch/input.h"

#include <string.h>

_Static_assert(DOOM_TOUCH_ACTION_COUNT <= 16,
               "touch actions must fit the transition queue");
_Static_assert(DOOM_TOUCH_EVENT_CAPACITY >= (2U * DOOM_TOUCH_ACTION_COUNT),
               "queue must hold every release and press transition");

typedef struct {
    uint16_t center_x;
    uint16_t center_y;
    uint16_t radius;
    doom_touch_action_t action;
} round_control_t;

static const round_control_t s_round_controls[] = {
    {289U, 151U, 35U, DOOM_TOUCH_ACTION_FIRE},
    {237U, 162U, 19U, DOOM_TOUCH_ACTION_USE},
    {246U, 117U, 18U, DOOM_TOUCH_ACTION_RUN},
    {203U, 143U, 17U, DOOM_TOUCH_ACTION_STRAFE},
    {16U, 23U, 14U, DOOM_TOUCH_ACTION_MAP},
    {49U, 23U, 14U, DOOM_TOUCH_ACTION_MENU_BACK},
    {197U, 23U, 14U, DOOM_TOUCH_ACTION_WEAPON_PREVIOUS},
    {230U, 23U, 14U, DOOM_TOUCH_ACTION_WEAPON_NEXT},
    {269U, 23U, 14U, DOOM_TOUCH_ACTION_MENU_ACCEPT},
    {304U, 23U, 14U, DOOM_TOUCH_ACTION_PAUSE},
};

static uint32_t action_bit(doom_touch_action_t action)
{
    return UINT32_C(1) << (unsigned)action;
}

static bool input_valid(const doom_touch_input_t *input)
{
    return input != NULL && input->version == DOOM_TOUCH_INPUT_VERSION &&
           input->size == sizeof(*input) &&
           input->read_index < DOOM_TOUCH_EVENT_CAPACITY &&
           input->event_count <= DOOM_TOUCH_EVENT_CAPACITY;
}

static bool frame_valid(const doom_touch_frame_t *frame)
{
    if (frame == NULL || frame->version != DOOM_TOUCH_INPUT_VERSION ||
        frame->size != sizeof(*frame) || frame->valid != 1U ||
        frame->contact_count > DOOM_TOUCH_MAX_CONTACTS) {
        return false;
    }
    for (unsigned index = 0U; index < frame->contact_count; ++index) {
        if (frame->contacts[index].x >= DOOM_TOUCH_SCREEN_WIDTH ||
            frame->contacts[index].y >= DOOM_TOUCH_SCREEN_HEIGHT) {
            return false;
        }
    }
    return true;
}

static bool inside_round(
    uint16_t x,
    uint16_t y,
    const round_control_t *control
)
{
    const int32_t dx = (int32_t)x - (int32_t)control->center_x;
    const int32_t dy = (int32_t)y - (int32_t)control->center_y;
    const uint32_t distance_squared =
        (uint32_t)(dx * dx) + (uint32_t)(dy * dy);
    return distance_squared <= (uint32_t)control->radius * control->radius;
}

static bool map_to_frame(const doom_touch_contact_t *contact,
                         uint16_t *frame_x,
                         uint16_t *frame_y)
{
    const uint16_t relative_x =
        (uint16_t)(contact->x - DOOM_TOUCH_VIEWPORT_LEFT);
    const uint16_t relative_y =
        (uint16_t)(contact->y - DOOM_TOUCH_VIEWPORT_TOP);
    if (relative_x >= DOOM_TOUCH_VIEWPORT_WIDTH ||
        relative_y >= DOOM_TOUCH_VIEWPORT_HEIGHT) {
        return false;
    }
    *frame_x = (uint16_t)(((uint32_t)relative_x * DOOM_TOUCH_FRAME_WIDTH) /
                          DOOM_TOUCH_VIEWPORT_WIDTH);
    *frame_y = (uint16_t)(((uint32_t)relative_y * DOOM_TOUCH_FRAME_HEIGHT) /
                          DOOM_TOUCH_VIEWPORT_HEIGHT);
    return *frame_x < DOOM_TOUCH_FRAME_WIDTH &&
        *frame_y < DOOM_TOUCH_FRAME_HEIGHT;
}

static uint32_t desired_actions(const doom_touch_frame_t *frame)
{
    uint32_t actions = 0U;
    for (unsigned index = 0U; index < frame->contact_count; ++index) {
        const doom_touch_contact_t *const contact = &frame->contacts[index];
        uint16_t frame_x = 0U;
        uint16_t frame_y = 0U;
        if (!map_to_frame(contact, &frame_x, &frame_y)) {
            continue;
        }

        /* A bounded virtual stick occupies the lower-left portion. */
        if (frame_x <= 119U && frame_y >= 83U) {
            const int32_t dx = (int32_t)frame_x - INT32_C(49);
            const int32_t dy = (int32_t)frame_y - INT32_C(148);
            const uint32_t radius_squared =
                (uint32_t)(dx * dx) + (uint32_t)(dy * dy);
            if (radius_squared <= UINT32_C(4489)) {
                if (dx <= -15) {
                    actions |= action_bit(DOOM_TOUCH_ACTION_LEFT);
                } else if (dx >= 15) {
                    actions |= action_bit(DOOM_TOUCH_ACTION_RIGHT);
                }
                if (dy <= -15) {
                    actions |= action_bit(DOOM_TOUCH_ACTION_UP);
                } else if (dy >= 15) {
                    actions |= action_bit(DOOM_TOUCH_ACTION_DOWN);
                }
            }
        }

        for (size_t control_index = 0U;
             control_index < sizeof(s_round_controls) /
                                 sizeof(s_round_controls[0]);
             ++control_index) {
            const round_control_t *const control =
                &s_round_controls[control_index];
            if (inside_round(frame_x, frame_y, control)) {
                actions |= action_bit(control->action);
                if (control->action == DOOM_TOUCH_ACTION_FIRE) {
                    actions |= action_bit(DOOM_TOUCH_ACTION_MENU_ACCEPT);
                }
            }
        }
    }
    return actions;
}

static void enqueue(
    doom_touch_input_t *input,
    doom_touch_action_t action,
    bool pressed
)
{
    const unsigned write_index =
        ((unsigned)input->read_index + (unsigned)input->event_count) %
        DOOM_TOUCH_EVENT_CAPACITY;
    input->events[write_index] = (doom_touch_event_t){
        .action = (uint8_t)action,
        .pressed = pressed ? 1U : 0U,
        .reserved = {0U, 0U},
    };
    ++input->event_count;
}

void doom_touch_frame_init(doom_touch_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }
    memset(frame, 0, sizeof(*frame));
    frame->version = DOOM_TOUCH_INPUT_VERSION;
    frame->size = (uint16_t)sizeof(*frame);
    frame->valid = 1U;
}

void doom_touch_input_init(doom_touch_input_t *input)
{
    if (input == NULL) {
        return;
    }
    memset(input, 0, sizeof(*input));
    input->version = DOOM_TOUCH_INPUT_VERSION;
    input->size = (uint16_t)sizeof(*input);
}

bool doom_touch_input_update(
    doom_touch_input_t *input,
    const doom_touch_frame_t *frame
)
{
    if (!input_valid(input) || input->event_count != 0U) {
        return false;
    }

    const bool valid = frame_valid(frame);
    const uint32_t desired = valid ? desired_actions(frame) : 0U;
    const uint32_t released = input->active_actions & ~desired;
    const uint32_t pressed = desired & ~input->active_actions;
    for (unsigned action = 0U; action < DOOM_TOUCH_ACTION_COUNT; ++action) {
        if ((released & action_bit((doom_touch_action_t)action)) != 0U) {
            enqueue(input, (doom_touch_action_t)action, false);
        }
    }
    for (unsigned action = 0U; action < DOOM_TOUCH_ACTION_COUNT; ++action) {
        if ((pressed & action_bit((doom_touch_action_t)action)) != 0U) {
            enqueue(input, (doom_touch_action_t)action, true);
        }
    }
    input->active_actions = desired;
    return valid;
}

bool doom_touch_input_next(
    doom_touch_input_t *input,
    doom_touch_event_t *event
)
{
    if (!input_valid(input) || event == NULL || input->event_count == 0U) {
        return false;
    }
    *event = input->events[input->read_index];
    input->read_index = (uint8_t)(
        ((unsigned)input->read_index + 1U) % DOOM_TOUCH_EVENT_CAPACITY
    );
    --input->event_count;
    return true;
}

bool doom_touch_input_idle(const doom_touch_input_t *input)
{
    return input_valid(input) && input->event_count == 0U;
}
