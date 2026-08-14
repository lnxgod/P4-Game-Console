#include "doom_touch/input.h"

#include <stdio.h>
#include <stdlib.h>
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

static unsigned drain(
    doom_touch_input_t *input,
    doom_touch_event_t *events,
    unsigned capacity
)
{
    unsigned count = 0U;
    while (count < capacity && doom_touch_input_next(input, &events[count])) {
        ++count;
    }
    return count;
}

static void test_multitouch_mapping_and_release(void)
{
    doom_touch_input_t input;
    doom_touch_input_init(&input);
    doom_touch_frame_t frame;
    doom_touch_frame_init(&frame);
    frame.contact_count = 3U;
    frame.contacts[0] = (doom_touch_contact_t){110U, 365U};
    frame.contacts[1] = (doom_touch_contact_t){900U, 455U};
    frame.contacts[2] = (doom_touch_contact_t){770U, 350U};
    EXPECT_TRUE(doom_touch_input_update(&input, &frame));

    doom_touch_event_t events[DOOM_TOUCH_EVENT_CAPACITY];
    const unsigned pressed = drain(&input, events, DOOM_TOUCH_EVENT_CAPACITY);
    EXPECT_EQ(5, pressed);
    uint32_t seen = 0U;
    for (unsigned index = 0U; index < pressed; ++index) {
        EXPECT_EQ(1, events[index].pressed);
        seen |= UINT32_C(1) << events[index].action;
    }
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_TOUCH_ACTION_UP)) != 0U);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_TOUCH_ACTION_LEFT)) != 0U);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_TOUCH_ACTION_FIRE)) != 0U);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_TOUCH_ACTION_MENU_ACCEPT)) != 0U);
    EXPECT_TRUE((seen & (UINT32_C(1) << DOOM_TOUCH_ACTION_RUN)) != 0U);

    frame.contact_count = 0U;
    EXPECT_TRUE(doom_touch_input_update(&input, &frame));
    const unsigned released = drain(&input, events, DOOM_TOUCH_EVENT_CAPACITY);
    EXPECT_EQ(pressed, released);
    for (unsigned index = 0U; index < released; ++index) {
        EXPECT_EQ(0, events[index].pressed);
    }
}

static void test_all_discrete_controls(void)
{
    static const struct {
        uint16_t x;
        uint16_t y;
        doom_touch_action_t action;
    } cases[] = {
        {742U, 486U, DOOM_TOUCH_ACTION_USE},
        {640U, 430U, DOOM_TOUCH_ACTION_STRAFE},
        {80U, 68U, DOOM_TOUCH_ACTION_MAP},
        {178U, 68U, DOOM_TOUCH_ACTION_MENU_BACK},
        {625U, 68U, DOOM_TOUCH_ACTION_WEAPON_PREVIOUS},
        {723U, 68U, DOOM_TOUCH_ACTION_WEAPON_NEXT},
        {840U, 68U, DOOM_TOUCH_ACTION_MENU_ACCEPT},
        {945U, 68U, DOOM_TOUCH_ACTION_PAUSE},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        doom_touch_input_t input;
        doom_touch_input_init(&input);
        doom_touch_frame_t frame;
        doom_touch_frame_init(&frame);
        frame.contact_count = 1U;
        frame.contacts[0] = (doom_touch_contact_t){cases[index].x,
                                                   cases[index].y};
        EXPECT_TRUE(doom_touch_input_update(&input, &frame));
        doom_touch_event_t event;
        EXPECT_TRUE(doom_touch_input_next(&input, &event));
        EXPECT_EQ(cases[index].action, event.action);
        EXPECT_EQ(1, event.pressed);
    }
}

static void test_fail_closed_frames_and_queue(void)
{
    doom_touch_input_t input;
    doom_touch_input_init(&input);
    doom_touch_frame_t frame;
    doom_touch_frame_init(&frame);
    frame.contact_count = 1U;
    frame.contacts[0] = (doom_touch_contact_t){900U, 455U};
    EXPECT_TRUE(doom_touch_input_update(&input, &frame));
    EXPECT_TRUE(!doom_touch_input_update(&input, &frame));

    doom_touch_event_t events[4];
    EXPECT_EQ(2, drain(&input, events, 4U));
    frame.contacts[0].x = DOOM_TOUCH_SCREEN_WIDTH;
    EXPECT_TRUE(!doom_touch_input_update(&input, &frame));
    EXPECT_EQ(2, drain(&input, events, 4U));
    EXPECT_EQ(0, events[0].pressed);
    EXPECT_EQ(0, events[1].pressed);
    EXPECT_TRUE(doom_touch_input_idle(&input));

    frame.contacts[0].x = 900U;
    frame.valid = 2U;
    EXPECT_TRUE(!doom_touch_input_update(&input, &frame));
    EXPECT_TRUE(doom_touch_input_idle(&input));
    EXPECT_TRUE(!doom_touch_input_update(NULL, &frame));
}

static void test_overlay_copy_and_active_feedback(void)
{
    const size_t pixels = DOOM_TOUCH_FRAME_WIDTH * DOOM_TOUCH_FRAME_HEIGHT;
    uint32_t *const source = calloc(pixels, sizeof(uint32_t));
    uint32_t *const inactive = calloc(pixels, sizeof(uint32_t));
    uint32_t *const active = calloc(pixels, sizeof(uint32_t));
    EXPECT_TRUE(source != NULL && inactive != NULL && active != NULL);
    if (source == NULL || inactive == NULL || active == NULL) {
        free(source);
        free(inactive);
        free(active);
        return;
    }
    for (size_t index = 0U; index < pixels; ++index) {
        source[index] = UINT32_C(0x00102030);
    }
    EXPECT_TRUE(doom_touch_overlay_render_xrgb8888(
        source, DOOM_TOUCH_FRAME_WIDTH, inactive, DOOM_TOUCH_FRAME_WIDTH, 0U));
    EXPECT_TRUE(doom_touch_overlay_render_xrgb8888(
        source, DOOM_TOUCH_FRAME_WIDTH, active, DOOM_TOUCH_FRAME_WIDTH,
        UINT32_C(1) << DOOM_TOUCH_ACTION_FIRE));
    EXPECT_EQ(source[100U * DOOM_TOUCH_FRAME_WIDTH + 160U],
              inactive[100U * DOOM_TOUCH_FRAME_WIDTH + 160U]);
    const size_t fire_border = 185U * DOOM_TOUCH_FRAME_WIDTH + 289U;
    const size_t fire_center = 151U * DOOM_TOUCH_FRAME_WIDTH + 289U;
    EXPECT_TRUE(inactive[fire_border] != source[fire_border]);
    EXPECT_TRUE(active[fire_center] != inactive[fire_center]);
    EXPECT_TRUE((active[fire_center] & UINT32_C(0x00ff0000)) >
                (inactive[fire_center] & UINT32_C(0x00ff0000)));
    EXPECT_TRUE(!doom_touch_overlay_render_xrgb8888(
        source, DOOM_TOUCH_FRAME_WIDTH - 1U, active,
        DOOM_TOUCH_FRAME_WIDTH, 0U));
    EXPECT_TRUE(!doom_touch_overlay_render_xrgb8888(
        source, DOOM_TOUCH_FRAME_WIDTH, active, DOOM_TOUCH_FRAME_WIDTH,
        UINT32_C(1) << 31U));
    EXPECT_TRUE(!doom_touch_overlay_render_xrgb8888(
        active, DOOM_TOUCH_FRAME_WIDTH, active,
        DOOM_TOUCH_FRAME_WIDTH + 1U, 0U));
    EXPECT_TRUE(!doom_touch_overlay_render_xrgb8888(
        source, DOOM_TOUCH_FRAME_WIDTH, source + 1,
        DOOM_TOUCH_FRAME_WIDTH, 0U));
    free(source);
    free(inactive);
    free(active);
}

int main(void)
{
    test_multitouch_mapping_and_release();
    test_all_discrete_controls();
    test_fail_closed_frames_and_queue();
    test_overlay_copy_and_active_feedback();
    if (failures != 0) {
        fprintf(stderr, "%d Doom touch input test(s) failed\n", failures);
        return 1;
    }
    puts("P4_DOOM_TOUCH_INPUT HOST PASS multitouch=true releases=true overlay=true");
    return 0;
}
