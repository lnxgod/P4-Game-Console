#include "gamepad/gamepad.h"
#include "gamepad/hid_gamepad.h"
#include "gamepad/mapping.h"
#include "gamepad/snapshot.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned g_failures;

#define EXPECT_TRUE(expression_)                                                        \
    do {                                                                                \
        if (!(expression_)) {                                                           \
            fprintf(stderr, "%s:%d: expected true: %s\n", __FILE__, __LINE__,        \
                    #expression_);                                                      \
            g_failures++;                                                               \
        }                                                                               \
    } while (0)

#define EXPECT_EQ(expected_, actual_)                                                   \
    do {                                                                                \
        const long long expected_value_ = (long long)(expected_);                       \
        const long long actual_value_ = (long long)(actual_);                           \
        if (expected_value_ != actual_value_) {                                         \
            fprintf(stderr, "%s:%d: expected %lld, got %lld: %s\n", __FILE__,         \
                    __LINE__, expected_value_, actual_value_, #actual_);                 \
            g_failures++;                                                               \
        }                                                                               \
    } while (0)

static void expect_parse_error(const uint8_t *descriptor,
                               size_t descriptor_size,
                               gamepad_status_t expected);

static const uint8_t simple_gamepad_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Collection (Application) */
    0x15, 0x81,       /* Logical Minimum (-127) */
    0x25, 0x7F,       /* Logical Maximum (127) */
    0x75, 0x08,       /* Report Size (8) */
    0x95, 0x02,       /* Report Count (2) */
    0x09, 0x30,       /* Usage (X) */
    0x09, 0x31,       /* Usage (Y) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x05, 0x09,       /* Usage Page (Button) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x04,       /* Report Count (4) */
    0x19, 0x01,       /* Usage Minimum (Button 1) */
    0x29, 0x04,       /* Usage Maximum (Button 4) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x75, 0x04,       /* Report Size (4) */
    0x95, 0x01,       /* Report Count (1) */
    0x81, 0x03,       /* Input (Constant, Variable, Absolute) */
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x07,       /* Logical Maximum (7) */
    0x75, 0x04,       /* Report Size (4) */
    0x95, 0x01,       /* Report Count (1) */
    0x09, 0x39,       /* Usage (Hat Switch) */
    0x81, 0x42,       /* Input (Data, Variable, Absolute, Null State) */
    0x75, 0x04,       /* Report Size (4) */
    0x95, 0x01,       /* Report Count (1) */
    0x81, 0x03,       /* Input (Constant, Variable, Absolute) */
    0xC0,             /* End Collection */
};

static const uint8_t report_id_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /* Report ID (1) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x26, 0xFF, 0x00, /* Logical Maximum (255) */
    0x75, 0x08,       /* Report Size (8) */
    0x95, 0x02,       /* Report Count (2) */
    0x09, 0x30,       /* Usage (X) */
    0x09, 0x31,       /* Usage (Y) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x85, 0x02,       /* Report ID (2) */
    0x05, 0x09,       /* Usage Page (Button) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x02,       /* Report Count (2) */
    0x19, 0x01,       /* Usage Minimum (Button 1) */
    0x29, 0x02,       /* Usage Maximum (Button 2) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x75, 0x06,       /* Report Size (6) */
    0x95, 0x01,       /* Report Count (1) */
    0x81, 0x03,       /* Input (Constant, Variable, Absolute) */
    0xC0,             /* End Collection */
};

static const uint8_t signed_twelve_bit_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Collection (Application) */
    0x16, 0x00, 0xF8, /* Logical Minimum (-2048) */
    0x26, 0xFF, 0x07, /* Logical Maximum (2047) */
    0x75, 0x0C,       /* Report Size (12) */
    0x95, 0x02,       /* Report Count (2) */
    0x09, 0x30,       /* Usage (X) */
    0x09, 0x31,       /* Usage (Y) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0xC0,             /* End Collection */
};

static const uint8_t push_pop_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Collection (Application) */
    0x15, 0x81,       /* Logical Minimum (-127) */
    0x25, 0x7F,       /* Logical Maximum (127) */
    0x75, 0x08,       /* Report Size (8) */
    0x95, 0x02,       /* Report Count (2) */
    0xA4,             /* Push globals */
    0x05, 0x09,       /* Usage Page (Button) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x02,       /* Report Count (2) */
    0x19, 0x01,       /* Usage Minimum (Button 1) */
    0x29, 0x02,       /* Usage Maximum (Button 2) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x75, 0x06,       /* Report Size (6) */
    0x95, 0x01,       /* Report Count (1) */
    0x81, 0x03,       /* Input (Constant, Variable, Absolute) */
    0xB4,             /* Pop globals */
    0x09, 0x30,       /* Usage (X), restored Generic Desktop page */
    0x09, 0x31,       /* Usage (Y) */
    0x81, 0x02,       /* Input using restored 8-bit signed globals */
    0xC0,             /* End Collection */
};

static const uint8_t button_array_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Collection (Application) */
    0x05, 0x09,       /* Usage Page (Button) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x07,       /* Logical Maximum (7; 5..7 are null selections) */
    0x75, 0x03,       /* Report Size (3) */
    0x95, 0x02,       /* Report Count (2 array slots) */
    0x19, 0x00,       /* Usage Minimum (No button) */
    0x29, 0x04,       /* Usage Maximum (Button 4) */
    0x81, 0x40,       /* Input (Data, Array, Absolute, Null State) */
    0x75, 0x02,       /* Report Size (2) */
    0x95, 0x01,       /* Report Count (1) */
    0x81, 0x01,       /* Input (Constant, Array, Absolute) */
    0xC0,             /* End Collection */
};

static const uint8_t unsigned_trigger_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Collection (Application) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x26, 0xFF, 0x03, /* Logical Maximum (1023) */
    0x75, 0x0A,       /* Report Size (10) */
    0x95, 0x02,       /* Report Count (2) */
    0x09, 0x32,       /* Usage (Z / left trigger) */
    0x09, 0x35,       /* Usage (Rz / right trigger) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x75, 0x04,       /* Report Size (4) */
    0x95, 0x01,       /* Report Count (1) */
    0x81, 0x03,       /* Input (Constant, Variable, Absolute) */
    0xC0,             /* End Collection */
};

static const uint8_t direct_button_array_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0x05, 0x09,       /* Change the global page after the local Usage. */
    0xA1, 0x01,       /* Collection (Application) */
    0x15, 0x00,       /* Logical Minimum (0; null selector) */
    0x25, 0x04,       /* Logical Maximum (4) */
    0x75, 0x03,       /* Report Size (3) */
    0x95, 0x02,       /* Report Count (2 array slots) */
    0x19, 0x01,       /* Usage Minimum (Button 1) */
    0x29, 0x04,       /* Usage Maximum (Button 4) */
    0x81, 0x40,       /* Input (Data, Array, Absolute, Null State) */
    0x75, 0x02,       /* Report Size (2) */
    0x95, 0x01,       /* Report Count (1) */
    0x81, 0x01,       /* Input (Constant, Array, Absolute) */
    0xC0,             /* End Collection */
};

static const uint8_t ordinal_button_array_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Collection (Application) */
    0x05, 0x09,       /* Usage Page (Button) */
    0x15, 0x01,       /* Logical Minimum (1) */
    0x25, 0x04,       /* Logical Maximum (4) */
    0x75, 0x03,       /* Report Size (3) */
    0x95, 0x02,       /* Report Count (2 array slots) */
    0x19, 0x05,       /* Usage Minimum (Button 5) */
    0x29, 0x08,       /* Usage Maximum (Button 8) */
    0x81, 0x40,       /* Input (Data, Array, Absolute, Null State) */
    0x75, 0x02,       /* Report Size (2) */
    0x95, 0x01,       /* Report Count (1) */
    0x81, 0x01,       /* Input (Constant, Array, Absolute) */
    0xC0,             /* End Collection */
};

static const uint8_t null_axis_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Collection (Application) */
    0x15, 0x81,       /* Logical Minimum (-127) */
    0x25, 0x7F,       /* Logical Maximum (127) */
    0x75, 0x08,       /* Report Size (8) */
    0x95, 0x01,       /* Report Count (1) */
    0x09, 0x30,       /* Usage (X) */
    0x81, 0x42,       /* Input (Data, Variable, Absolute, Null State) */
    0xC0,             /* End Collection */
};

/* Captured from the live low-speed USB Gamepad (VID:PID 0079:0011). */
static const uint8_t usb_gamepad_0079_0011_descriptor[] = {
    0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0xA1, 0x02, 0x75, 0x08,
    0x95, 0x05, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x35, 0x00, 0x46,
    0xFF, 0x00, 0x09, 0x30, 0x09, 0x30, 0x09, 0x30, 0x09, 0x30,
    0x09, 0x31, 0x81, 0x02, 0x75, 0x04, 0x95, 0x01, 0x25, 0x07,
    0x46, 0x3B, 0x01, 0x65, 0x14, 0x09, 0x00, 0x81, 0x42, 0x65,
    0x00, 0x75, 0x01, 0x95, 0x0A, 0x25, 0x01, 0x45, 0x01, 0x05,
    0x09, 0x19, 0x01, 0x29, 0x0A, 0x81, 0x02, 0x06, 0x00, 0xFF,
    0x75, 0x01, 0x95, 0x0A, 0x25, 0x01, 0x45, 0x01, 0x09, 0x01,
    0x81, 0x02, 0xC0, 0xA1, 0x02, 0x75, 0x08, 0x95, 0x04, 0x46,
    0xFF, 0x00, 0x26, 0xFF, 0x00, 0x09, 0x02, 0x91, 0x02, 0xC0,
    0xC0,
};

static const uint8_t usb_gamepad_0079_0011_descriptor_sha256[] = {
    0x05, 0xA1, 0x51, 0xC9, 0x32, 0x36, 0x2F, 0xEE,
    0x13, 0x50, 0x39, 0x05, 0x96, 0x28, 0x80, 0xCE,
    0x75, 0x21, 0x2B, 0xBF, 0x8C, 0x43, 0xC9, 0x57,
    0x01, 0x52, 0x7B, 0x82, 0x90, 0x83, 0x31, 0x62,
};

static const uint8_t two_gamepad_applications_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* First Application Collection (selected) */
    0x85, 0x01,       /* Report ID (1) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x26, 0xFF, 0x00, /* Logical Maximum (255) */
    0x75, 0x08,       /* Report Size (8) */
    0x95, 0x01,       /* Report Count (1) */
    0x09, 0x30,       /* Usage (X) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0xC0,             /* End Collection */
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x05,       /* Usage (Game Pad) */
    0xA1, 0x01,       /* Second Application Collection (not selected) */
    0x85, 0x02,       /* Report ID (2) */
    0x05, 0x09,       /* Usage Page (Button) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x01,       /* Report Count (1) */
    0x09, 0x01,       /* Usage (Button 1) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0xC0,             /* End Collection */
};

static void connect_state(gamepad_state_t *state)
{
    gamepad_state_init(state);
    EXPECT_EQ(GAMEPAD_OK, gamepad_state_connect(state, 1));
    EXPECT_TRUE(state->connected);
    EXPECT_TRUE(gamepad_state_is_neutral(state));
}

static void test_simple_mapping_hat_and_disconnect(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(simple_gamepad_descriptor,
                                           sizeof(simple_gamepad_descriptor), &layout));
    EXPECT_EQ(7, layout.field_count);
    EXPECT_EQ(1, layout.report_count);
    EXPECT_EQ(4, layout.reports[0].payload_bytes);

    const uint32_t capabilities = gamepad_hid_capabilities(&layout);
    EXPECT_TRUE((capabilities & GAMEPAD_CAP_LEFT_STICK) != 0U);
    EXPECT_TRUE((capabilities & GAMEPAD_CAP_BUTTONS) != 0U);
    EXPECT_TRUE((capabilities & GAMEPAD_CAP_DPAD) != 0U);

    gamepad_state_t state;
    connect_state(&state);
    const uint8_t pressed[] = {0x81, 0x7F, 0x05, 0x02};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, pressed, sizeof(pressed), 20, &state));
    EXPECT_EQ(INT16_MIN, state.left_x);
    EXPECT_EQ(INT16_MAX, state.left_y);
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_WEST),
              state.buttons);
    EXPECT_EQ(GAMEPAD_DPAD_RIGHT, state.dpad);
    EXPECT_EQ(20, state.timestamp_us);

    const uint8_t released_and_null_hat[] = {0x00, 0x00, 0x00, 0x08};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, released_and_null_hat,
                                        sizeof(released_and_null_hat), 21, &state));
    EXPECT_EQ(0, state.buttons);
    EXPECT_EQ(GAMEPAD_DPAD_CENTERED, state.dpad);

    const uint32_t sequence_before_disconnect = state.sequence;
    EXPECT_EQ(GAMEPAD_OK, gamepad_state_disconnect(&state, 30));
    EXPECT_TRUE(!state.connected);
    EXPECT_TRUE(gamepad_state_is_neutral(&state));
    EXPECT_EQ(sequence_before_disconnect + 1U, state.sequence);
    EXPECT_EQ(GAMEPAD_ERR_DISCONNECTED,
              gamepad_hid_decode_report(&layout, pressed, sizeof(pressed), 31, &state));
    EXPECT_TRUE(gamepad_state_is_neutral(&state));

    const uint32_t sequence_before_reconnect = state.sequence;
    EXPECT_EQ(GAMEPAD_OK, gamepad_state_connect(&state, 32));
    EXPECT_TRUE(state.connected);
    EXPECT_TRUE(gamepad_state_is_neutral(&state));
    EXPECT_EQ(sequence_before_reconnect + 1U, state.sequence);
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, pressed, sizeof(pressed), 33, &state));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_WEST),
              state.buttons);
}

static void test_report_ids_are_incremental_and_transactional(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(report_id_descriptor,
                                           sizeof(report_id_descriptor), &layout));
    EXPECT_EQ(1, layout.uses_report_ids);
    EXPECT_EQ(2, layout.report_count);

    gamepad_state_t state;
    connect_state(&state);
    const uint8_t axes[] = {0x01, 0x00, 0xFF};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, axes, sizeof(axes), 40, &state));
    EXPECT_EQ(INT16_MIN, state.left_x);
    EXPECT_EQ(INT16_MAX, state.left_y);

    const uint8_t buttons[] = {0x02, 0x03};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, buttons, sizeof(buttons), 41, &state));
    EXPECT_EQ(3, state.buttons);
    EXPECT_EQ(INT16_MIN, state.left_x);
    EXPECT_EQ(INT16_MAX, state.left_y);

    const gamepad_state_t before_error = state;
    const uint8_t unknown[] = {0x7F, 0x00};
    EXPECT_EQ(GAMEPAD_ERR_REPORT_ID,
              gamepad_hid_decode_report(&layout, unknown, sizeof(unknown), 42, &state));
    EXPECT_TRUE(memcmp(&before_error, &state, sizeof(state)) == 0);

    const uint8_t short_axes[] = {0x01, 0x00};
    EXPECT_EQ(GAMEPAD_ERR_REPORT_SIZE,
              gamepad_hid_decode_report(&layout, short_axes, sizeof(short_axes), 43,
                                        &state));
    EXPECT_TRUE(memcmp(&before_error, &state, sizeof(state)) == 0);
}

static void test_only_first_gamepad_application_is_selected(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(two_gamepad_applications_descriptor,
                                           sizeof(two_gamepad_applications_descriptor),
                                           &layout));
    EXPECT_EQ(2, layout.application_collection_count);
    EXPECT_EQ(2, layout.report_count);
    EXPECT_EQ(1, layout.reports[0].report_id);

    gamepad_state_t state;
    connect_state(&state);
    const uint8_t selected_report[] = {0x01, 0xFF};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, selected_report,
                                        sizeof(selected_report), 45, &state));
    EXPECT_EQ(INT16_MAX, state.left_x);

    gamepad_state_t expected = state;
    expected.sequence++;
    expected.timestamp_us = 46;
    const uint8_t unselected_report[] = {0x02, 0x01};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, unselected_report,
                                        sizeof(unselected_report), 46, &state));
    EXPECT_TRUE(memcmp(&expected, &state, sizeof(state)) == 0);

    const uint8_t short_report[] = {0x02};
    const uint8_t long_report[] = {0x02, 0x01, 0x00};
    const uint8_t unknown_report[] = {0x03, 0x01};
    EXPECT_EQ(GAMEPAD_ERR_REPORT_SIZE, gamepad_hid_decode_report(
        &layout, short_report, sizeof(short_report), 47, &state));
    EXPECT_EQ(GAMEPAD_ERR_REPORT_SIZE, gamepad_hid_decode_report(
        &layout, long_report, sizeof(long_report), 47, &state));
    EXPECT_EQ(GAMEPAD_ERR_REPORT_ID, gamepad_hid_decode_report(
        &layout, unknown_report, sizeof(unknown_report), 47, &state));
    EXPECT_TRUE(memcmp(&expected, &state, sizeof(state)) == 0);
}


/* Synthetic HID fixtures exercise layout rules, not a named controller claim. */
static void test_shared_report_offsets_cross_application_collections(void)
{
    static const uint8_t descriptor[] = {
        0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, /* vendor application */
        0x75, 0x03, 0x95, 0x01, 0x81, 0x02,       /* 3 input prefix bits */
        0x75, 0x08, 0x91, 0x02, 0xB1, 0x02,       /* output/feature != input */
        0xC0,
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,       /* selected gamepad */
        0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01,
        0x09, 0x30, 0x81, 0x02,                   /* X at input bit 3 */
        0xC0,
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,       /* unselected gamepad */
        0x15, 0x00, 0x25, 0x1F, 0x75, 0x05, 0x95, 0x01,
        0x09, 0x31, 0x81, 0x02,                   /* 5 suffix bits, no Y */
        0xC0,
    };
    for (unsigned with_id = 0U; with_id <= 1U; ++with_id) {
        uint8_t bytes[sizeof(descriptor) + 2U];
        bytes[0] = 0x85;
        bytes[1] = 7;
        memcpy(bytes + 2U, descriptor, sizeof(descriptor));
        gamepad_hid_layout_t layout;
        EXPECT_EQ(GAMEPAD_OK, gamepad_hid_parse_descriptor(
            with_id != 0U ? bytes : descriptor,
            with_id != 0U ? sizeof(bytes) : sizeof(descriptor), &layout));
        EXPECT_EQ(1, layout.report_count);
        EXPECT_EQ(1, layout.field_count);
        EXPECT_EQ(3, layout.fields[0].bit_offset);
        EXPECT_EQ(16, layout.reports[0].payload_bits);
        EXPECT_EQ(2, layout.reports[0].payload_bytes);
        gamepad_state_t state;
        connect_state(&state);
        /* Prefix=7, X=127, suffix=31. Only X may affect canonical state. */
        const uint8_t report[] = {7, 0xFF, 0xFB};
        EXPECT_EQ(GAMEPAD_OK, gamepad_hid_decode_report(
            &layout, report + (with_id != 0U ? 0U : 1U),
            with_id != 0U ? 3U : 2U, 20U, &state));
        EXPECT_EQ(INT16_MAX, state.left_x);
        EXPECT_EQ(0, state.left_y);
        EXPECT_EQ(0, state.buttons);
        const gamepad_state_t before_error = state;
        EXPECT_EQ(GAMEPAD_ERR_REPORT_SIZE, gamepad_hid_decode_report(
            &layout, report + (with_id != 0U ? 0U : 1U),
            with_id != 0U ? 2U : 1U, 21U, &state));
        EXPECT_TRUE(memcmp(&before_error, &state, sizeof(state)) == 0);
    }
}

static void test_secondary_reports_remain_bounded(void)
{
    uint8_t descriptor[GAMEPAD_HID_MAX_DESCRIPTOR_BYTES];
    const size_t base = sizeof(two_gamepad_applications_descriptor);
    memcpy(descriptor, two_gamepad_applications_descriptor, base);
    size_t length = base;
    for (uint8_t id = 3U; id <= GAMEPAD_HID_MAX_REPORTS + 1U; ++id) {
        const uint8_t application[] = {
            0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
            0x85, id, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0xC0,
        };
        memcpy(descriptor + length, application, sizeof(application));
        length += sizeof(application);
        if (id == GAMEPAD_HID_MAX_REPORTS) {
            gamepad_hid_layout_t layout;
            EXPECT_EQ(GAMEPAD_OK, gamepad_hid_parse_descriptor(
                descriptor, length, &layout));
            EXPECT_EQ(GAMEPAD_HID_MAX_REPORTS, layout.report_count);
            gamepad_state_t state;
            connect_state(&state);
            const uint8_t vendor_report[] = {id, 0xFF};
            EXPECT_EQ(GAMEPAD_OK, gamepad_hid_decode_report(
                &layout, vendor_report, sizeof(vendor_report), 2U, &state));
            EXPECT_TRUE(gamepad_state_is_neutral(&state));
        }
    }
    expect_parse_error(descriptor, length, GAMEPAD_ERR_LIMIT_EXCEEDED);

    /* Multiple ignored Input items cannot evade the total report bit limit. */
    memcpy(descriptor, two_gamepad_applications_descriptor, base);
    static const uint8_t overflow[] = {
        0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 3,
        0x75, 32, 0x96, 0, 1, 0x81, 0x02, /* 32 * 256 = 8192 bits */
        0x75, 1, 0x95, 1, 0x81, 0x02, 0xC0,
    };
    memcpy(descriptor + base, overflow, sizeof(overflow));
    expect_parse_error(descriptor, base + sizeof(overflow),
                       GAMEPAD_ERR_LIMIT_EXCEEDED);

    /* Report-ID policy applies to the entire interface, not just the pad. */
    static const uint8_t implicit_id_prefix[] = {
        0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
        0x75, 8, 0x95, 1, 0x81, 0x02, 0xC0,
    };
    memcpy(descriptor, implicit_id_prefix, sizeof(implicit_id_prefix));
    memcpy(descriptor + sizeof(implicit_id_prefix),
           two_gamepad_applications_descriptor, base);
    expect_parse_error(descriptor, sizeof(implicit_id_prefix) + base,
                       GAMEPAD_ERR_MALFORMED);
}

static void test_secondary_report_commits_without_releasing_held_controls(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK, gamepad_hid_parse_descriptor(
        two_gamepad_applications_descriptor,
        sizeof(two_gamepad_applications_descriptor), &layout));
    platform_gamepad_model_t model;
    platform_gamepad_model_init(&model);
    const platform_gamepad_identity_t identity = {
        .transport = PLATFORM_GAMEPAD_TRANSPORT_USB_HID,
        .descriptor_sha256 = {1},
    };
    uint32_t session = 0U;
    EXPECT_EQ(GAMEPAD_OK, platform_gamepad_model_connect(
        &model, &identity, gamepad_hid_capabilities(&layout), 1U, &session));
    const uint8_t reports[][2] = {{1, 0xFF}, {2, 1}, {1, 0}, {2, 0}};
    for (size_t index = 0U; index < sizeof(reports) / sizeof(reports[0]); ++index) {
        platform_gamepad_snapshot_t snapshot;
        EXPECT_EQ(GAMEPAD_OK, platform_gamepad_model_copy(&model, &snapshot));
        EXPECT_EQ(GAMEPAD_OK, gamepad_hid_decode_report(
            &layout, reports[index], sizeof(reports[index]), index + 2U,
            &snapshot.state));
        EXPECT_EQ(GAMEPAD_OK, platform_gamepad_model_commit_report(
            &model, session, &snapshot.state));
        EXPECT_EQ(1, snapshot.state.connected);
        EXPECT_EQ(index < 2U ? INT16_MAX : INT16_MIN, snapshot.state.left_x);
        EXPECT_EQ(0, snapshot.state.buttons);
    }
    EXPECT_EQ(GAMEPAD_OK, platform_gamepad_model_disconnect(&model, session, 6U));
    platform_gamepad_snapshot_t disconnected;
    EXPECT_EQ(GAMEPAD_OK, platform_gamepad_model_copy(&model, &disconnected));
    EXPECT_TRUE(gamepad_state_is_neutral(&disconnected.state));
    EXPECT_EQ(0, disconnected.state.connected);
}

static void test_signed_non_byte_aligned_axes(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(signed_twelve_bit_descriptor,
                                           sizeof(signed_twelve_bit_descriptor), &layout));
    EXPECT_EQ(3, layout.reports[0].payload_bytes);
    EXPECT_EQ(0, layout.fields[0].bit_offset);
    EXPECT_EQ(12, layout.fields[1].bit_offset);

    gamepad_state_t state;
    connect_state(&state);
    const uint8_t packed[] = {0x00, 0xF8, 0x7F}; /* X=-2048, Y=2047 */
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, packed, sizeof(packed), 50, &state));
    EXPECT_EQ(INT16_MIN, state.left_x);
    EXPECT_EQ(INT16_MAX, state.left_y);
}

static void test_push_pop_restores_globals(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(push_pop_descriptor,
                                           sizeof(push_pop_descriptor), &layout));
    EXPECT_EQ(4, layout.field_count);
    EXPECT_EQ(3, layout.reports[0].payload_bytes);

    gamepad_state_t state;
    connect_state(&state);
    const uint8_t report[] = {0x03, 0x81, 0x7F};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, report, sizeof(report), 60, &state));
    EXPECT_EQ(3, state.buttons);
    EXPECT_EQ(INT16_MIN, state.left_x);
    EXPECT_EQ(INT16_MAX, state.left_y);
}

static void test_button_array_replaces_prior_selection(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(button_array_descriptor,
                                           sizeof(button_array_descriptor), &layout));
    EXPECT_EQ(2, layout.field_count);
    EXPECT_EQ(1, layout.reports[0].payload_bytes);
    EXPECT_TRUE((layout.fields[0].flags & GAMEPAD_HID_FIELD_ARRAY) != 0U);
    EXPECT_TRUE((layout.fields[0].flags & GAMEPAD_HID_FIELD_NULL_STATE) != 0U);

    gamepad_state_t state;
    connect_state(&state);
    const uint8_t buttons_one_and_three[] = {0x19};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, buttons_one_and_three,
                                        sizeof(buttons_one_and_three), 65, &state));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_WEST),
              state.buttons);

    const uint8_t button_two_only[] = {0x02};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, button_two_only,
                                        sizeof(button_two_only), 66, &state));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST), state.buttons);

    const uint8_t null_selection[] = {0x05};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, null_selection,
                                        sizeof(null_selection), 67, &state));
    EXPECT_EQ(0, state.buttons);
}

static void test_array_selector_semantics(void)
{
    gamepad_hid_layout_t direct_layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(direct_button_array_descriptor,
                                           sizeof(direct_button_array_descriptor),
                                           &direct_layout));
    EXPECT_EQ(2, direct_layout.field_count);

    gamepad_state_t state;
    connect_state(&state);
    const uint8_t direct_button_one[] = {0x01}; /* Selectors 1 and null 0. */
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&direct_layout, direct_button_one,
                                        sizeof(direct_button_one), 67, &state));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH), state.buttons);

    const uint8_t direct_button_four[] = {0x04}; /* Selectors 4 and null 0. */
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&direct_layout, direct_button_four,
                                        sizeof(direct_button_four), 68, &state));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_NORTH), state.buttons);

    gamepad_hid_layout_t ordinal_layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(ordinal_button_array_descriptor,
                                           sizeof(ordinal_button_array_descriptor),
                                           &ordinal_layout));
    connect_state(&state);
    const uint8_t ordinal_first[] = {0x01}; /* Logical 1 maps to Usage Button 5. */
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&ordinal_layout, ordinal_first,
                                        sizeof(ordinal_first), 69, &state));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_LEFT_SHOULDER), state.buttons);
}

static void test_out_of_range_values_are_not_clamped(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(simple_gamepad_descriptor,
                                           sizeof(simple_gamepad_descriptor), &layout));
    gamepad_state_t state;
    connect_state(&state);
    const uint8_t valid[] = {0x7F, 0x00, 0x00, 0x08};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, valid, sizeof(valid), 70, &state));
    const gamepad_state_t before_error = state;
    const uint8_t invalid_axis[] = {0x80, 0x00, 0x00, 0x08}; /* -128 < -127 */
    EXPECT_EQ(GAMEPAD_ERR_MALFORMED,
              gamepad_hid_decode_report(&layout, invalid_axis,
                                        sizeof(invalid_axis), 71, &state));
    EXPECT_TRUE(memcmp(&before_error, &state, sizeof(state)) == 0);

    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(null_axis_descriptor,
                                           sizeof(null_axis_descriptor), &layout));
    connect_state(&state);
    const uint8_t maximum[] = {0x7F};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, maximum, sizeof(maximum), 72, &state));
    EXPECT_EQ(INT16_MAX, state.left_x);
    const uint8_t null_value[] = {0x80};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, null_value, sizeof(null_value), 73,
                                        &state));
    EXPECT_EQ(0, state.left_x);
}

static void test_unsigned_non_byte_aligned_triggers(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(unsigned_trigger_descriptor,
                                           sizeof(unsigned_trigger_descriptor), &layout));
    EXPECT_EQ(2, layout.field_count);
    EXPECT_EQ(3, layout.reports[0].payload_bytes);
    EXPECT_EQ(10, layout.fields[1].bit_offset);
    EXPECT_TRUE((gamepad_hid_capabilities(&layout) & GAMEPAD_CAP_LEFT_TRIGGER) != 0U);
    EXPECT_TRUE((gamepad_hid_capabilities(&layout) & GAMEPAD_CAP_RIGHT_TRIGGER) != 0U);

    gamepad_state_t state;
    connect_state(&state);
    const uint8_t minimum_and_maximum[] = {0x00, 0xFC, 0x0F};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, minimum_and_maximum,
                                        sizeof(minimum_and_maximum), 68, &state));
    EXPECT_EQ(0, state.left_trigger);
    EXPECT_EQ(UINT16_MAX, state.right_trigger);
    EXPECT_EQ(GAMEPAD_OK, gamepad_state_disconnect(&state, 69));
    EXPECT_TRUE(gamepad_state_is_neutral(&state));
    EXPECT_EQ(0, state.right_trigger);
}

static void test_profile_can_rebind_field(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(signed_twelve_bit_descriptor,
                                           sizeof(signed_twelve_bit_descriptor), &layout));
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_set_field_mapping(&layout, 0, GAMEPAD_HID_MAP_RIGHT_X, 0));
    EXPECT_TRUE((gamepad_hid_capabilities(&layout) & GAMEPAD_CAP_RIGHT_STICK) != 0U);

    gamepad_state_t state;
    connect_state(&state);
    const uint8_t packed[] = {0x00, 0xF8, 0x7F};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, packed, sizeof(packed), 70, &state));
    EXPECT_EQ(0, state.left_x);
    EXPECT_EQ(INT16_MIN, state.right_x);
}

static void test_exact_usb_gamepad_profile(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(
                  usb_gamepad_0079_0011_descriptor,
                  sizeof(usb_gamepad_0079_0011_descriptor), &layout));
    EXPECT_EQ(12, layout.field_count);
    EXPECT_EQ(8, layout.reports[0].payload_bytes);
    EXPECT_TRUE((gamepad_hid_capabilities(&layout) & GAMEPAD_CAP_LEFT_STICK) !=
                0U);
    EXPECT_TRUE((gamepad_hid_capabilities(&layout) & GAMEPAD_CAP_DPAD) == 0U);

    gamepad_hid_profile_t profile = GAMEPAD_HID_PROFILE_NONE;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_apply_known_profile(
                  0x0079U, 0x0011U,
                  usb_gamepad_0079_0011_descriptor_sha256, &layout, &profile));
    EXPECT_EQ(GAMEPAD_HID_PROFILE_USB_GAMEPAD_0079_0011, profile);
    EXPECT_EQ(13, layout.field_count);
    EXPECT_TRUE(strcmp("usb-gamepad-0079-0011",
                       gamepad_hid_profile_name(profile)) == 0);
    EXPECT_EQ(GAMEPAD_CAP_BUTTONS | GAMEPAD_CAP_DPAD,
              gamepad_hid_capabilities(&layout));

    gamepad_state_t state;
    connect_state(&state);

    /* Captured live neutral: the five ambiguous axes are intentionally ignored. */
    const uint8_t neutral[] = {0x01, 0x7F, 0x7F, 0x7F,
                               0x7F, 0x0F, 0x00, 0x00};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, neutral, sizeof(neutral), 80,
                                        &state));
    EXPECT_TRUE(gamepad_state_is_neutral(&state));

    /* Retrolink mapping: axis 3 right, with physical Y and L pressed. */
    const uint8_t right_with_buttons[] = {0x01, 0x7F, 0x7F, 0xFF,
                                          0x7F, 0x1F, 0x01, 0x00};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, right_with_buttons,
                                        sizeof(right_with_buttons), 81, &state));
    EXPECT_EQ(GAMEPAD_DPAD_RIGHT, state.dpad);
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_WEST) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_LEFT_SHOULDER),
              state.buttons);
    EXPECT_EQ(0, state.left_x);
    EXPECT_EQ(0, state.left_y);
    EXPECT_EQ(0, state.right_x);
    EXPECT_EQ(0, state.right_y);

    const uint8_t up_left[] = {0x01, 0x7F, 0x7F, 0x00,
                               0x00, 0x0F, 0x00, 0x00};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, up_left, sizeof(up_left), 82,
                                        &state));
    EXPECT_EQ(GAMEPAD_DPAD_UP | GAMEPAD_DPAD_LEFT, state.dpad);
    EXPECT_EQ(0, state.buttons);

    const uint8_t down_right[] = {0x01, 0x7F, 0x7F, 0xFF,
                                  0xFF, 0x0F, 0x00, 0x00};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, down_right,
                                        sizeof(down_right), 83, &state));
    EXPECT_EQ(GAMEPAD_DPAD_DOWN | GAMEPAD_DPAD_RIGHT, state.dpad);

    /* HID buttons are Y, B, A, X; canonical face buttons follow labels. */
    const uint8_t face_buttons[] = {0x01, 0x7F, 0x7F, 0x7F,
                                    0x7F, 0xFF, 0x00, 0x00};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, face_buttons,
                                        sizeof(face_buttons), 84, &state));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_WEST) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_NORTH),
              state.buttons);

    const uint8_t physical_a[] = {0x01, 0x7F, 0x7F, 0x7F,
                                  0x7F, 0x4F, 0x00, 0x00};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, physical_a,
                                        sizeof(physical_a), 85, &state));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH), state.buttons);

    const uint8_t physical_b[] = {0x01, 0x7F, 0x7F, 0x7F,
                                  0x7F, 0x2F, 0x00, 0x00};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, physical_b,
                                        sizeof(physical_b), 86, &state));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST), state.buttons);

    const uint8_t select_start[] = {0x01, 0x7F, 0x7F, 0x7F,
                                    0x7F, 0x0F, 0x30, 0x00};
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_decode_report(&layout, select_start,
                                        sizeof(select_start), 87, &state));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_BACK) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_START),
              state.buttons);
}

static void test_exact_usb_gamepad_profile_fails_closed(void)
{
    gamepad_hid_layout_t generic;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(
                  usb_gamepad_0079_0011_descriptor,
                  sizeof(usb_gamepad_0079_0011_descriptor), &generic));

    gamepad_hid_layout_t wrong_hash_layout = generic;
    uint8_t wrong_hash[GAMEPAD_HID_DESCRIPTOR_SHA256_BYTES];
    memcpy(wrong_hash, usb_gamepad_0079_0011_descriptor_sha256,
           sizeof(wrong_hash));
    wrong_hash[0] ^= 1U;
    gamepad_hid_profile_t profile = GAMEPAD_HID_PROFILE_USB_GAMEPAD_0079_0011;
    EXPECT_EQ(GAMEPAD_ERR_UNSUPPORTED,
              gamepad_hid_apply_known_profile(0x0079U, 0x0011U, wrong_hash,
                                              &wrong_hash_layout, &profile));
    EXPECT_EQ(GAMEPAD_HID_PROFILE_NONE, profile);
    EXPECT_TRUE(memcmp(&generic, &wrong_hash_layout, sizeof(generic)) == 0);

    gamepad_hid_layout_t wrong_layout = generic;
    wrong_layout.fields[0].bit_offset = 8U;
    const gamepad_hid_layout_t before = wrong_layout;
    profile = GAMEPAD_HID_PROFILE_USB_GAMEPAD_0079_0011;
    EXPECT_EQ(GAMEPAD_ERR_UNSUPPORTED,
              gamepad_hid_apply_known_profile(
                  0x0079U, 0x0011U,
                  usb_gamepad_0079_0011_descriptor_sha256, &wrong_layout,
                  &profile));
    EXPECT_EQ(GAMEPAD_HID_PROFILE_NONE, profile);
    EXPECT_TRUE(memcmp(&before, &wrong_layout, sizeof(before)) == 0);
}

static void test_corrupted_public_objects_are_rejected(void)
{
    gamepad_state_t corrupt_state;
    gamepad_state_init(&corrupt_state);
    corrupt_state.connected = 2U;
    EXPECT_TRUE(!gamepad_state_is_neutral(&corrupt_state));
    EXPECT_EQ(GAMEPAD_ERR_INVALID_STATE, gamepad_state_connect(&corrupt_state, 75));
    EXPECT_EQ(GAMEPAD_ERR_INVALID_STATE, gamepad_state_disconnect(&corrupt_state, 76));

    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(simple_gamepad_descriptor,
                                           sizeof(simple_gamepad_descriptor), &layout));
    gamepad_state_t state;
    connect_state(&state);
    const gamepad_state_t before_error = state;
    const uint8_t report[] = {0, 0, 0, 8};

    gamepad_hid_layout_t mutated = layout;
    mutated.field_count = GAMEPAD_HID_MAX_FIELDS + 1U;
    EXPECT_EQ(GAMEPAD_ERR_INVALID_STATE,
              gamepad_hid_decode_report(&mutated, report, sizeof(report), 77, &state));
    EXPECT_TRUE(memcmp(&before_error, &state, sizeof(state)) == 0);

    mutated = layout;
    mutated.reports[0].payload_bytes++;
    EXPECT_EQ(GAMEPAD_ERR_INVALID_STATE,
              gamepad_hid_decode_report(&mutated, report, sizeof(report), 78, &state));
    EXPECT_TRUE(memcmp(&before_error, &state, sizeof(state)) == 0);

    mutated = layout;
    mutated.fields[0].bit_offset = mutated.reports[0].payload_bits;
    EXPECT_EQ(GAMEPAD_ERR_INVALID_STATE,
              gamepad_hid_decode_report(&mutated, report, sizeof(report), 79, &state));
    EXPECT_TRUE(memcmp(&before_error, &state, sizeof(state)) == 0);

    mutated = layout;
    mutated.fields[0].logical_min = 0;
    mutated.fields[0].logical_max = 300;
    EXPECT_EQ(GAMEPAD_ERR_INVALID_STATE,
              gamepad_hid_decode_report(&mutated, report, sizeof(report), 80, &state));
    EXPECT_TRUE(memcmp(&before_error, &state, sizeof(state)) == 0);

    mutated = layout;
    mutated.application_collection_count = 0U;
    EXPECT_EQ(GAMEPAD_ERR_INVALID_STATE,
              gamepad_hid_decode_report(&mutated, report, sizeof(report), 81, &state));
    EXPECT_TRUE(memcmp(&before_error, &state, sizeof(state)) == 0);

    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(button_array_descriptor,
                                           sizeof(button_array_descriptor), &mutated));
    mutated.fields[0].usage = GAMEPAD_BUTTON_COUNT + 1U;
    mutated.fields[0].usage_max = GAMEPAD_BUTTON_COUNT + 2U;
    EXPECT_EQ(0, gamepad_hid_capabilities(&mutated));
    EXPECT_EQ(GAMEPAD_ERR_INVALID_STATE,
              gamepad_hid_set_field_mapping(&mutated, 0,
                                            GAMEPAD_HID_MAP_BUTTON, 0));
}

static void test_local_usage_page_is_captured(void)
{
    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(direct_button_array_descriptor,
                                           sizeof(direct_button_array_descriptor), &layout));
    EXPECT_EQ(GAMEPAD_HID_USAGE_GAME_PAD, layout.selected_application_usage);

    static const uint8_t explicit_page_zero_usage[] = {
        0x05, 0x01,                   /* Usage Page (Generic Desktop) */
        0x0B, 0x05, 0x00, 0x00, 0x00, /* 32-bit Usage (Page 0, ID 5) */
        0xA1, 0x01,                   /* Collection (Application) */
        0xC0,                         /* End Collection */
    };
    expect_parse_error(explicit_page_zero_usage, sizeof(explicit_page_zero_usage),
                       GAMEPAD_ERR_NO_GAMEPAD);
}

static void expect_parse_error(const uint8_t *descriptor,
                               size_t descriptor_size,
                               gamepad_status_t expected)
{
    gamepad_hid_layout_t layout;
    memset(&layout, 0xA5, sizeof(layout));
    EXPECT_EQ(expected,
              gamepad_hid_parse_descriptor(descriptor, descriptor_size, &layout));
    EXPECT_EQ(GAMEPAD_HID_LAYOUT_VERSION, layout.version);
    EXPECT_EQ(sizeof(layout), layout.size);
    EXPECT_EQ(0, layout.field_count);
    EXPECT_EQ(0, layout.report_count);
}

static void test_malformed_and_bounded_inputs(void)
{
    static const uint8_t truncated_short_item[] = {0x05};
    static const uint8_t truncated_long_item[] = {0xFE, 0x04, 0x01, 0xAA};
    static const uint8_t end_without_collection[] = {0xC0};
    static const uint8_t collection_without_type[] = {0x05, 0x01, 0x09, 0x05, 0xA0};
    static const uint8_t pop_without_push[] = {0xB4};
    static const uint8_t unclosed_collection[] = {0x05, 0x01, 0x09, 0x05, 0xA1, 0x01};
    static const uint8_t excessive_report_count[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
        0x96, 0x01, 0x01, /* Report Count (257) */
        0x19, 0x01, 0x29, 0x01, 0x81, 0x02, 0xC0,
    };
    static const uint8_t keyboard_descriptor[] = {
        0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0xC0,
    };
    static const uint8_t excessive_fields[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
        0x96, 0x81, 0x00, /* Report Count (129) */
        0x05, 0x09,       /* Usage Page (Button) */
        0x09, 0x01,       /* Repeated Usage (Button 1) */
        0x81, 0x02, 0xC0,
    };
    static const uint8_t unsupported_array_mapping[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x05, 0x09, 0x15, 0x00, 0x25, 0x02,
        0x75, 0x03, 0x95, 0x01, 0x19, 0x05,
        0x29, 0x08, 0x81, 0x40, 0xC0,
    };
    static const uint8_t impossible_logical_range[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x15, 0x00, 0x26, 0xFF, 0x00, /* Logical 0..255 */
        0x75, 0x01, 0x95, 0x01,       /* One-bit field cannot encode it. */
        0x09, 0x30, 0x81, 0x02, 0xC0,
    };
    static const uint8_t zero_report_size[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x75, 0x00, 0xC0,
    };
    static const uint8_t zero_report_count[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x95, 0x00, 0xC0,
    };
    static const uint8_t buffered_button_bytes[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
        0x05, 0x09, 0x15, 0x00, 0x25, 0x01,
        0x75, 0x01, 0x95, 0x01, 0x19, 0x01,
        0x29, 0x01,
        0x82, 0x00, 0x01, /* Input (Data, Array, Absolute, Buffered Bytes) */
        0xC0,
    };

    expect_parse_error(truncated_short_item, sizeof(truncated_short_item),
                       GAMEPAD_ERR_TRUNCATED);
    expect_parse_error(truncated_long_item, sizeof(truncated_long_item),
                       GAMEPAD_ERR_TRUNCATED);
    expect_parse_error(end_without_collection, sizeof(end_without_collection),
                       GAMEPAD_ERR_MALFORMED);
    expect_parse_error(collection_without_type, sizeof(collection_without_type),
                       GAMEPAD_ERR_MALFORMED);
    expect_parse_error(pop_without_push, sizeof(pop_without_push),
                       GAMEPAD_ERR_MALFORMED);
    expect_parse_error(unclosed_collection, sizeof(unclosed_collection),
                       GAMEPAD_ERR_MALFORMED);
    expect_parse_error(excessive_report_count, sizeof(excessive_report_count),
                       GAMEPAD_ERR_LIMIT_EXCEEDED);
    expect_parse_error(keyboard_descriptor, sizeof(keyboard_descriptor),
                       GAMEPAD_ERR_NO_GAMEPAD);
    expect_parse_error(excessive_fields, sizeof(excessive_fields),
                       GAMEPAD_ERR_LIMIT_EXCEEDED);
    expect_parse_error(unsupported_array_mapping,
                       sizeof(unsupported_array_mapping),
                       GAMEPAD_ERR_NO_MAPPABLE_INPUT);
    expect_parse_error(impossible_logical_range,
                       sizeof(impossible_logical_range),
                       GAMEPAD_ERR_MALFORMED);
    expect_parse_error(zero_report_size, sizeof(zero_report_size),
                       GAMEPAD_ERR_MALFORMED);
    expect_parse_error(zero_report_count, sizeof(zero_report_count),
                       GAMEPAD_ERR_MALFORMED);
    expect_parse_error(buffered_button_bytes, sizeof(buffered_button_bytes),
                       GAMEPAD_ERR_NO_MAPPABLE_INPUT);

    static uint8_t oversized_descriptor[GAMEPAD_HID_MAX_DESCRIPTOR_BYTES + 1U];
    expect_parse_error(oversized_descriptor, sizeof(oversized_descriptor),
                       GAMEPAD_ERR_DESCRIPTOR_TOO_LARGE);

    gamepad_hid_layout_t layout;
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_hid_parse_descriptor(simple_gamepad_descriptor,
                                           sizeof(simple_gamepad_descriptor), &layout));
    gamepad_state_t state;
    connect_state(&state);
    static uint8_t oversized_report[GAMEPAD_HID_MAX_REPORT_BYTES + 1U];
    EXPECT_EQ(GAMEPAD_ERR_REPORT_TOO_LARGE,
              gamepad_hid_decode_report(&layout, oversized_report,
                                        sizeof(oversized_report), 80, &state));

    const gamepad_state_t before_extra_data = state;
    const uint8_t report_with_trailing_data[] = {0, 0, 0, 0, 0xA5};
    EXPECT_EQ(GAMEPAD_ERR_REPORT_SIZE,
              gamepad_hid_decode_report(&layout, report_with_trailing_data,
                                        sizeof(report_with_trailing_data), 81, &state));
    EXPECT_TRUE(memcmp(&before_extra_data, &state, sizeof(state)) == 0);

    EXPECT_EQ(GAMEPAD_ERR_INVALID_ARGUMENT,
              gamepad_hid_parse_descriptor(NULL, 1, &layout));
    EXPECT_EQ(GAMEPAD_HID_LAYOUT_VERSION, layout.version);
    EXPECT_EQ(sizeof(layout), layout.size);
    EXPECT_EQ(0, layout.field_count);
    EXPECT_EQ(0, layout.report_count);
}

static uint32_t fuzz_next(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

static void test_deterministic_hostile_mutations(void)
{
    gamepad_hid_layout_t layout;
    for (size_t size = 0; size < sizeof(simple_gamepad_descriptor); ++size) {
        const gamepad_status_t status =
            gamepad_hid_parse_descriptor(simple_gamepad_descriptor, size, &layout);
        EXPECT_TRUE(status != GAMEPAD_OK);
        EXPECT_EQ(0, layout.field_count);
        EXPECT_EQ(0, layout.report_count);
    }

    uint32_t random_state = UINT32_C(0xC0DEC0DE);
    uint8_t descriptor[sizeof(simple_gamepad_descriptor)];
    uint8_t report[8];
    for (unsigned iteration = 0; iteration < 5000U; ++iteration) {
        memcpy(descriptor, simple_gamepad_descriptor, sizeof(descriptor));
        const unsigned mutation_count = 1U + fuzz_next(&random_state) % 4U;
        for (unsigned mutation = 0; mutation < mutation_count; ++mutation) {
            const size_t index = fuzz_next(&random_state) % sizeof(descriptor);
            descriptor[index] ^= (uint8_t)(1U << (fuzz_next(&random_state) % 8U));
        }

        const gamepad_status_t parse_status =
            gamepad_hid_parse_descriptor(descriptor, sizeof(descriptor), &layout);
        EXPECT_TRUE(parse_status <= GAMEPAD_OK &&
                    parse_status >= GAMEPAD_ERR_DISCONNECTED);
        if (parse_status != GAMEPAD_OK) {
            EXPECT_EQ(0, layout.field_count);
            EXPECT_EQ(0, layout.report_count);
            continue;
        }

        gamepad_state_t gamepad_state;
        connect_state(&gamepad_state);
        for (size_t index = 0; index < sizeof(report); ++index) {
            report[index] = (uint8_t)fuzz_next(&random_state);
        }
        const size_t report_size = fuzz_next(&random_state) % (sizeof(report) + 1U);
        const gamepad_state_t before_decode = gamepad_state;
        const gamepad_status_t decode_status =
            gamepad_hid_decode_report(&layout, report, report_size, iteration,
                                      &gamepad_state);
        EXPECT_TRUE(decode_status <= GAMEPAD_OK &&
                    decode_status >= GAMEPAD_ERR_DISCONNECTED);
        if (decode_status != GAMEPAD_OK) {
            EXPECT_TRUE(memcmp(&before_decode, &gamepad_state,
                               sizeof(gamepad_state)) == 0);
        }
    }
}

static void test_console_button_mapping(void)
{
    gamepad_button_mapping_t mapping;
    gamepad_button_mapping_default(&mapping);
    EXPECT_TRUE(gamepad_button_mapping_valid(&mapping));

    gamepad_state_t state;
    gamepad_state_init(&state);
    EXPECT_EQ(GAMEPAD_OK, gamepad_state_connect(&state, 1U));
    state.buttons = GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_NORTH) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_GUIDE);
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_state_apply_button_mapping(&state, &mapping));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_NORTH) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_GUIDE),
              state.buttons);

    mapping.source[GAMEPAD_MAPPING_A] = GAMEPAD_BUTTON_EAST;
    mapping.source[GAMEPAD_MAPPING_B] = GAMEPAD_BUTTON_SOUTH;
    EXPECT_TRUE(gamepad_button_mapping_valid(&mapping));
    state.buttons = GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_GUIDE);
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_state_apply_button_mapping(&state, &mapping));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_GUIDE),
              state.buttons);

    gamepad_button_mapping_default(&mapping);
    mapping.source[GAMEPAD_MAPPING_A] = GAMEPAD_BUTTON_LEFT_SHOULDER;
    EXPECT_TRUE(gamepad_button_mapping_valid(&mapping));
    state.buttons = GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_LEFT_SHOULDER) |
                    GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_GUIDE);
    EXPECT_EQ(GAMEPAD_OK,
              gamepad_state_apply_button_mapping(&state, &mapping));
    EXPECT_EQ(GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH) |
                  GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_GUIDE),
              state.buttons);

    mapping.source[GAMEPAD_MAPPING_A] = GAMEPAD_BUTTON_SOUTH;
    mapping.source[GAMEPAD_MAPPING_B] = GAMEPAD_BUTTON_SOUTH;
    EXPECT_TRUE(!gamepad_button_mapping_valid(&mapping));
    const gamepad_state_t before = state;
    EXPECT_EQ(GAMEPAD_ERR_INVALID_ARGUMENT,
              gamepad_state_apply_button_mapping(&state, &mapping));
    EXPECT_TRUE(memcmp(&state, &before, sizeof(state)) == 0);
}

int main(void)
{
    test_simple_mapping_hat_and_disconnect();
    test_report_ids_are_incremental_and_transactional();
    test_only_first_gamepad_application_is_selected();
    test_shared_report_offsets_cross_application_collections();
    test_secondary_reports_remain_bounded();
    test_secondary_report_commits_without_releasing_held_controls();
    test_signed_non_byte_aligned_axes();
    test_push_pop_restores_globals();
    test_button_array_replaces_prior_selection();
    test_array_selector_semantics();
    test_out_of_range_values_are_not_clamped();
    test_unsigned_non_byte_aligned_triggers();
    test_profile_can_rebind_field();
    test_exact_usb_gamepad_profile();
    test_exact_usb_gamepad_profile_fails_closed();
    test_corrupted_public_objects_are_rejected();
    test_local_usage_page_is_captured();
    test_malformed_and_bounded_inputs();
    test_deterministic_hostile_mutations();
    test_console_button_mapping();

    if (g_failures != 0U) {
        fprintf(stderr, "%u gamepad_core test assertion(s) failed\n", g_failures);
        return 1;
    }
    puts("gamepad_core: all tests passed");
    return 0;
}
