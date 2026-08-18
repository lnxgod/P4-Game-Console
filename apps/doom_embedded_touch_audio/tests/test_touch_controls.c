// SPDX-License-Identifier: GPL-2.0-or-later

#include "touch_controls.h"

#include <assert.h>
#include <stdlib.h>

#include "doomkeys.h"

static void test_action_mapping(void)
{
    static const struct {
        doom_touch_action_t action;
        unsigned char key;
    } cases[] = {
        {DOOM_TOUCH_ACTION_UP, (unsigned char)KEY_UPARROW},
        {DOOM_TOUCH_ACTION_DOWN, (unsigned char)KEY_DOWNARROW},
        {DOOM_TOUCH_ACTION_LEFT, (unsigned char)KEY_LEFTARROW},
        {DOOM_TOUCH_ACTION_RIGHT, (unsigned char)KEY_RIGHTARROW},
        {DOOM_TOUCH_ACTION_FIRE, (unsigned char)KEY_FIRE},
        {DOOM_TOUCH_ACTION_USE, (unsigned char)KEY_USE},
        {DOOM_TOUCH_ACTION_RUN, (unsigned char)KEY_RSHIFT},
        {DOOM_TOUCH_ACTION_STRAFE, (unsigned char)KEY_RALT},
        {DOOM_TOUCH_ACTION_MENU_ACCEPT, (unsigned char)KEY_ENTER},
        {DOOM_TOUCH_ACTION_MENU_BACK, (unsigned char)KEY_ESCAPE},
        {DOOM_TOUCH_ACTION_MAP, (unsigned char)KEY_TAB},
        {DOOM_TOUCH_ACTION_WEAPON_NEXT, (unsigned char)']'},
        {DOOM_TOUCH_ACTION_WEAPON_PREVIOUS, (unsigned char)'['},
        {DOOM_TOUCH_ACTION_PAUSE, (unsigned char)KEY_PAUSE},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        unsigned char key = 0U;
        assert(doom_touch_audio_action_key((uint8_t)cases[index].action, &key));
        assert(key == cases[index].key);
    }
    unsigned char key = 0U;
    assert(!doom_touch_audio_action_key((uint8_t)DOOM_TOUCH_ACTION_COUNT, &key));
    assert(!doom_touch_audio_action_key(0U, NULL));
}

static void test_platform_frame_conversion(void)
{
    platform_touch_frame_t source = {
        .version = PLATFORM_TOUCH_VERSION,
        .size = (uint16_t)sizeof(platform_touch_frame_t),
        .sequence = 7U,
        .timestamp_us = 1234,
        .contact_count = 2U,
        .valid = 1U,
        .reserved = {0U},
        .contacts = {
            {.x = 180U, .y = 445U, .strength = 10U, .reserved = 0U},
            {.x = 900U, .y = 455U, .strength = 20U, .reserved = 0U},
        },
    };
    doom_touch_frame_t destination;
    assert(doom_touch_audio_frame_from_platform(&source, &destination));
    assert(destination.valid == 1U);
    assert(destination.contact_count == 2U);
    assert(destination.contacts[0].x == 180U);
    assert(destination.contacts[1].y == 455U);

    source.valid = 0U;
    assert(!doom_touch_audio_frame_from_platform(&source, &destination));
    assert(destination.valid == 0U);
    assert(destination.contact_count == 0U);
    source.valid = 1U;
    source.contacts[1].x = PLATFORM_TOUCH_WIDTH;
    assert(!doom_touch_audio_frame_from_platform(&source, &destination));
    assert(destination.valid == 0U);
    assert(!doom_touch_audio_frame_from_platform(NULL, &destination));
    assert(!doom_touch_audio_frame_from_platform(&source, NULL));
}

static void test_force_neutral_discards_pending_presses(void)
{
    doom_touch_input_t input;
    doom_touch_input_init(&input);
    doom_touch_frame_t frame;
    doom_touch_frame_init(&frame);
    frame.contact_count = 2U;
    frame.contacts[0] = (doom_touch_contact_t){180U, 390U};
    frame.contacts[1] = (doom_touch_contact_t){900U, 455U};
    assert(doom_touch_input_update(&input, &frame));
    assert(input.active_actions != 0U);
    assert(input.event_count > 0U);

    const uint32_t held = input.active_actions;
    assert(doom_touch_audio_force_neutral(&input));
    assert(input.active_actions == 0U);
    unsigned release_count = 0U;
    doom_touch_event_t event;
    while (doom_touch_input_next(&input, &event)) {
        assert(event.pressed == 0U);
        assert((held & (UINT32_C(1) << event.action)) != 0U);
        ++release_count;
    }
    assert(release_count > 0U);
    input.version = 0U;
    assert(!doom_touch_audio_force_neutral(&input));
    assert(!doom_touch_audio_force_neutral(NULL));
}

static void test_app_compositor(void)
{
    const size_t pixels =
        (size_t)DOOM_TOUCH_FRAME_WIDTH * (size_t)DOOM_TOUCH_FRAME_HEIGHT;
    uint32_t *source = calloc(pixels, sizeof(*source));
    uint32_t *destination = calloc(pixels, sizeof(*destination));
    assert(source != NULL && destination != NULL);
    for (size_t index = 0U; index < pixels; ++index) {
        source[index] = UINT32_C(0x00204060);
    }
    doom_touch_input_t input;
    doom_touch_input_init(&input);
    assert(doom_touch_audio_compose_frame(
        source, DOOM_TOUCH_FRAME_WIDTH, destination,
        DOOM_TOUCH_FRAME_WIDTH, &input));
    assert(destination[100U * DOOM_TOUCH_FRAME_WIDTH + 160U] ==
           source[100U * DOOM_TOUCH_FRAME_WIDTH + 160U]);

    doom_touch_frame_t frame;
    doom_touch_frame_init(&frame);
    frame.contact_count = 1U;
    frame.contacts[0] = (doom_touch_contact_t){900U, 455U};
    assert(doom_touch_input_update(&input, &frame));
    assert(doom_touch_audio_compose_frame(
        source, DOOM_TOUCH_FRAME_WIDTH, destination,
        DOOM_TOUCH_FRAME_WIDTH, &input));
    assert(destination[151U * DOOM_TOUCH_FRAME_WIDTH + 289U] !=
           source[151U * DOOM_TOUCH_FRAME_WIDTH + 289U]);
    assert(destination[16U * DOOM_TOUCH_FRAME_WIDTH + 264U] ==
           UINT32_C(0x00ffffff));
    assert(destination[16U * DOOM_TOUCH_FRAME_WIDTH + 45U] ==
           UINT32_C(0x00ffffff));

    input.version = 0U;
    assert(!doom_touch_audio_compose_frame(
        source, DOOM_TOUCH_FRAME_WIDTH, destination,
        DOOM_TOUCH_FRAME_WIDTH, &input));
    free(destination);
    free(source);
}

int main(void)
{
    test_action_mapping();
    test_platform_frame_conversion();
    test_force_neutral_discards_pending_presses();
    test_app_compositor();
    return 0;
}
