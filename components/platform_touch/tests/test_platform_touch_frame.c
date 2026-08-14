#include "platform/touch.h"
#include "platform_touch_frame.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned failures;

#define EXPECT_TRUE(expression_) do { if (!(expression_)) { \
    fprintf(stderr, "%s:%d expected true: %s\n", __FILE__, __LINE__, #expression_); \
    ++failures; } } while (0)
#define EXPECT_EQ(expected_, actual_) do { \
    const long long e_ = (long long)(expected_); \
    const long long a_ = (long long)(actual_); \
    if (e_ != a_) { fprintf(stderr, "%s:%d expected %lld got %lld\n", \
        __FILE__, __LINE__, e_, a_); ++failures; } } while (0)

static void test_neutral_and_fail_closed(void)
{
    platform_touch_frame_t frame;
    memset(&frame, 0xa5, sizeof(frame));
    platform_touch_frame_neutral(&frame);
    EXPECT_EQ(PLATFORM_TOUCH_VERSION, frame.version);
    EXPECT_EQ(sizeof(frame), frame.size);
    EXPECT_EQ(1, frame.valid);
    EXPECT_EQ(0, frame.contact_count);

    memset(&frame, 0xa5, sizeof(frame));
    platform_touch_frame_fail_closed(&frame, 17U, 123456);
    EXPECT_EQ(0, frame.valid);
    EXPECT_EQ(0, frame.contact_count);
    EXPECT_EQ(17, frame.sequence);
    EXPECT_EQ(123456, frame.timestamp_us);
    for (size_t index = 0U; index < sizeof(frame.contacts); ++index) {
        EXPECT_EQ(0, ((const uint8_t *)frame.contacts)[index]);
    }
}

static void test_five_contacts_and_strength(void)
{
    const uint16_t x[5] = {0U, 1023U, 180U, 900U, 742U};
    const uint16_t y[5] = {0U, 599U, 445U, 455U, 486U};
    const uint16_t strength[5] = {1U, 2U, 3U, 4U, 65535U};
    platform_touch_frame_t frame;
    EXPECT_TRUE(platform_touch_frame_from_raw(
        &frame, 42U, 9000, x, y, strength, 5U));
    EXPECT_EQ(1, frame.valid);
    EXPECT_EQ(5, frame.contact_count);
    EXPECT_EQ(1023, frame.contacts[1].x);
    EXPECT_EQ(599, frame.contacts[1].y);
    EXPECT_EQ(65535, frame.contacts[4].strength);
}

static void test_malformed_input_neutralizes(void)
{
    uint16_t x[6] = {1U, 2U, 3U, 4U, 5U, 6U};
    uint16_t y[6] = {1U, 2U, 3U, 4U, 5U, 6U};
    platform_touch_frame_t frame;
    memset(&frame, 0xa5, sizeof(frame));
    EXPECT_TRUE(!platform_touch_frame_from_raw(
        &frame, 3U, 4, x, y, NULL, 6U));
    EXPECT_EQ(0, frame.valid);
    EXPECT_EQ(0, frame.contact_count);

    x[0] = PLATFORM_TOUCH_WIDTH;
    EXPECT_TRUE(!platform_touch_frame_from_raw(
        &frame, 5U, 6, x, y, NULL, 1U));
    EXPECT_EQ(0, frame.valid);
    EXPECT_EQ(0, frame.contact_count);

    x[0] = 1U;
    y[0] = PLATFORM_TOUCH_HEIGHT;
    EXPECT_TRUE(!platform_touch_frame_from_raw(
        &frame, 7U, 8, x, y, NULL, 1U));
    EXPECT_EQ(0, frame.valid);

    EXPECT_TRUE(!platform_touch_frame_from_raw(
        &frame, 9U, 10, NULL, y, NULL, 1U));
    EXPECT_TRUE(!platform_touch_frame_from_raw(
        NULL, 9U, 10, x, y, NULL, 1U));
}

int main(void)
{
    test_neutral_and_fail_closed();
    test_five_contacts_and_strength();
    test_malformed_input_neutralizes();
    if (failures != 0U) {
        fprintf(stderr, "platform touch frame failures: %u\n", failures);
        return 1;
    }
    puts("P4_TOUCH_FRAME HOST PASS contacts=5 bounds=1024x600 fail_closed=true");
    return 0;
}
