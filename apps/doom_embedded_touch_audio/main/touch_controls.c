// SPDX-License-Identifier: GPL-2.0-or-later

#include "touch_controls.h"

#include "doomkeys.h"

bool doom_touch_audio_frame_from_platform(
    const platform_touch_frame_t *source,
    doom_touch_frame_t *destination
)
{
    if (destination == NULL) {
        return false;
    }
    doom_touch_frame_init(destination);
    if (source == NULL || source->version != PLATFORM_TOUCH_VERSION ||
        source->size != sizeof(*source) || source->valid != 1U ||
        source->contact_count > PLATFORM_TOUCH_MAX_CONTACTS ||
        PLATFORM_TOUCH_MAX_CONTACTS > DOOM_TOUCH_MAX_CONTACTS) {
        destination->valid = 0U;
        return false;
    }
    for (uint8_t index = 0U; index < source->contact_count; ++index) {
        if (source->contacts[index].x >= PLATFORM_TOUCH_WIDTH ||
            source->contacts[index].y >= PLATFORM_TOUCH_HEIGHT) {
            destination->valid = 0U;
            destination->contact_count = 0U;
            return false;
        }
        destination->contacts[index] = (doom_touch_contact_t){
            .x = source->contacts[index].x,
            .y = source->contacts[index].y,
        };
    }
    destination->contact_count = source->contact_count;
    return true;
}

bool doom_touch_audio_action_key(uint8_t action, unsigned char *out_key)
{
    if (out_key == NULL) {
        return false;
    }
    switch ((doom_touch_action_t)action) {
    case DOOM_TOUCH_ACTION_UP:
        *out_key = (unsigned char)KEY_UPARROW;
        return true;
    case DOOM_TOUCH_ACTION_DOWN:
        *out_key = (unsigned char)KEY_DOWNARROW;
        return true;
    case DOOM_TOUCH_ACTION_LEFT:
        *out_key = (unsigned char)KEY_LEFTARROW;
        return true;
    case DOOM_TOUCH_ACTION_RIGHT:
        *out_key = (unsigned char)KEY_RIGHTARROW;
        return true;
    case DOOM_TOUCH_ACTION_FIRE:
        *out_key = (unsigned char)KEY_FIRE;
        return true;
    case DOOM_TOUCH_ACTION_USE:
        *out_key = (unsigned char)KEY_USE;
        return true;
    case DOOM_TOUCH_ACTION_RUN:
        *out_key = (unsigned char)KEY_RSHIFT;
        return true;
    case DOOM_TOUCH_ACTION_STRAFE:
        *out_key = (unsigned char)KEY_RALT;
        return true;
    case DOOM_TOUCH_ACTION_MENU_ACCEPT:
        *out_key = (unsigned char)KEY_ENTER;
        return true;
    case DOOM_TOUCH_ACTION_MENU_BACK:
        *out_key = (unsigned char)KEY_ESCAPE;
        return true;
    case DOOM_TOUCH_ACTION_MAP:
        *out_key = (unsigned char)KEY_TAB;
        return true;
    case DOOM_TOUCH_ACTION_WEAPON_NEXT:
        *out_key = (unsigned char)']';
        return true;
    case DOOM_TOUCH_ACTION_WEAPON_PREVIOUS:
        *out_key = (unsigned char)'[';
        return true;
    case DOOM_TOUCH_ACTION_PAUSE:
        *out_key = (unsigned char)KEY_PAUSE;
        return true;
    case DOOM_TOUCH_ACTION_COUNT:
    default:
        return false;
    }
}

bool doom_touch_audio_force_neutral(doom_touch_input_t *input)
{
    if (input == NULL || input->version != DOOM_TOUCH_INPUT_VERSION ||
        input->size != sizeof(*input) ||
        input->read_index >= DOOM_TOUCH_EVENT_CAPACITY ||
        input->event_count > DOOM_TOUCH_EVENT_CAPACITY) {
        return false;
    }
    const uint32_t held = input->active_actions;
    doom_touch_input_init(input);
    for (unsigned action = 0U; action < DOOM_TOUCH_ACTION_COUNT; ++action) {
        if ((held & (UINT32_C(1) << action)) == 0U) {
            continue;
        }
        input->events[input->event_count] = (doom_touch_event_t){
            .action = (uint8_t)action,
            .pressed = 0U,
            .reserved = {0U, 0U},
        };
        ++input->event_count;
    }
    return true;
}

bool doom_touch_audio_compose_frame(
    const uint32_t *source,
    size_t source_stride_pixels,
    uint32_t *destination,
    size_t destination_stride_pixels,
    const doom_touch_input_t *input
)
{
    if (input == NULL || input->version != DOOM_TOUCH_INPUT_VERSION ||
        input->size != sizeof(*input) ||
        input->read_index >= DOOM_TOUCH_EVENT_CAPACITY ||
        input->event_count > DOOM_TOUCH_EVENT_CAPACITY) {
        return false;
    }
    return doom_touch_overlay_render_xrgb8888(
        source, source_stride_pixels, destination, destination_stride_pixels,
        input->active_actions);
}

bool doom_touch_audio_compose_frame_sized(
    const uint32_t *source, size_t source_stride_pixels,
    uint32_t *destination, size_t destination_stride_pixels,
    size_t width, size_t height, const doom_touch_input_t *input)
{
    if (input == NULL || input->version != DOOM_TOUCH_INPUT_VERSION ||
        input->size != sizeof(*input) ||
        input->read_index >= DOOM_TOUCH_EVENT_CAPACITY ||
        input->event_count > DOOM_TOUCH_EVENT_CAPACITY) return false;
    return doom_touch_overlay_render_xrgb8888_sized(source,source_stride_pixels,
        destination,destination_stride_pixels,width,height,input->active_actions);
}
