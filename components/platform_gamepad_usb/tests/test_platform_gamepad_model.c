#include "platform_gamepad_usb/model.h"

#include <stdio.h>
#include <string.h>

#include "usb_host_open_guard.h"

static int failures;

#define EXPECT_TRUE(condition_)                                                   \
    do {                                                                          \
        if (!(condition_)) {                                                       \
            fprintf(stderr, "%s:%d expectation failed: %s\n", __FILE__, __LINE__, \
                    #condition_);                                                  \
            failures++;                                                           \
        }                                                                         \
    } while (0)

#define EXPECT_EQ(expected_, actual_) EXPECT_TRUE((expected_) == (actual_))

static platform_gamepad_identity_t identity(uint8_t seed)
{
    platform_gamepad_identity_t value = {
        .vendor_id = 0x1234,
        .product_id = (uint16_t)(0x5600U | seed),
        .interface_number = 2,
        .transport = PLATFORM_GAMEPAD_TRANSPORT_USB_HID,
    };
    for (size_t index = 0; index < sizeof(value.descriptor_sha256); ++index) {
        value.descriptor_sha256[index] = (uint8_t)(seed + index);
    }
    return value;
}

typedef struct {
    void **output;
    void *success_handle;
    int32_t result;
    bool write_success_handle;
    bool called;
    bool observed_precleared_output;
} guarded_open_fixture_t;

static int32_t guarded_open_delegate(void *context)
{
    guarded_open_fixture_t *const fixture = context;
    fixture->called = true;
    fixture->observed_precleared_output = *fixture->output == NULL;
    if (fixture->write_success_handle) {
        *fixture->output = fixture->success_handle;
    }
    return fixture->result;
}

static void test_usb_open_guard_clears_failure_and_preserves_success(void)
{
    static int initial_object;
    static int success_object;
    void *handle = &initial_object;
    guarded_open_fixture_t fixture = {
        .output = &handle,
        .success_handle = &success_object,
        .result = -37,
    };
    EXPECT_EQ(-37, platform_gamepad_usb_open_guard_call(
                       &handle, sizeof(handle), guarded_open_delegate, &fixture,
                       -1));
    EXPECT_TRUE(fixture.called);
    EXPECT_TRUE(fixture.observed_precleared_output);
    EXPECT_TRUE(handle == NULL);

    handle = &initial_object;
    fixture = (guarded_open_fixture_t){
        .output = &handle,
        .success_handle = &success_object,
        .result = 0,
        .write_success_handle = true,
    };
    EXPECT_EQ(0, platform_gamepad_usb_open_guard_call(
                     &handle, sizeof(handle), guarded_open_delegate, &fixture,
                     -1));
    EXPECT_TRUE(fixture.called);
    EXPECT_TRUE(fixture.observed_precleared_output);
    EXPECT_TRUE(handle == &success_object);

    fixture.called = false;
    EXPECT_EQ(-1, platform_gamepad_usb_open_guard_call(
                      NULL, sizeof(handle), guarded_open_delegate, &fixture,
                      -1));
    EXPECT_TRUE(!fixture.called);
}

static void test_disconnect_neutralizes_and_rejects_queued_report(void)
{
    platform_gamepad_model_t model;
    platform_gamepad_model_init(&model);
    const platform_gamepad_identity_t first_identity = identity(1);
    uint32_t session = 0;
    EXPECT_EQ(GAMEPAD_OK,
              platform_gamepad_model_connect(
                  &model, &first_identity,
                  GAMEPAD_CAP_BUTTONS | GAMEPAD_CAP_LEFT_STICK, 10, &session));

    platform_gamepad_snapshot_t before_report;
    EXPECT_EQ(GAMEPAD_OK, platform_gamepad_model_copy(&model, &before_report));
    gamepad_state_t decoded = before_report.state;
    decoded.buttons = GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH);
    decoded.left_x = INT16_MAX;
    decoded.sequence++;
    decoded.timestamp_us = 20;
    EXPECT_EQ(GAMEPAD_OK,
              platform_gamepad_model_commit_report(&model, session, &decoded));

    EXPECT_EQ(GAMEPAD_OK,
              platform_gamepad_model_disconnect(&model, session, 30));
    platform_gamepad_snapshot_t disconnected;
    EXPECT_EQ(GAMEPAD_OK,
              platform_gamepad_model_copy(&model, &disconnected));
    EXPECT_TRUE(!disconnected.state.connected);
    EXPECT_TRUE(gamepad_state_is_neutral(&disconnected.state));
    EXPECT_TRUE(memcmp(&disconnected.identity, &first_identity,
                       sizeof(first_identity)) == 0);

    EXPECT_EQ(GAMEPAD_ERR_DISCONNECTED,
              platform_gamepad_model_commit_report(&model, session, &decoded));
    platform_gamepad_snapshot_t unchanged;
    EXPECT_EQ(GAMEPAD_OK, platform_gamepad_model_copy(&model, &unchanged));
    EXPECT_TRUE(memcmp(&disconnected, &unchanged, sizeof(unchanged)) == 0);
}

static void test_old_session_cannot_clear_or_update_reconnected_device(void)
{
    platform_gamepad_model_t model;
    platform_gamepad_model_init(&model);
    const platform_gamepad_identity_t first = identity(2);
    uint32_t old_session;
    EXPECT_EQ(GAMEPAD_OK,
              platform_gamepad_model_connect(&model, &first,
                                             GAMEPAD_CAP_BUTTONS, 1,
                                             &old_session));
    EXPECT_EQ(GAMEPAD_OK,
              platform_gamepad_model_disconnect(&model, old_session, 2));

    const platform_gamepad_identity_t second = identity(3);
    uint32_t new_session;
    EXPECT_EQ(GAMEPAD_OK,
              platform_gamepad_model_connect(&model, &second,
                                             GAMEPAD_CAP_DPAD, 3,
                                             &new_session));
    EXPECT_TRUE(new_session != old_session);
    EXPECT_EQ(GAMEPAD_ERR_DISCONNECTED,
              platform_gamepad_model_disconnect(&model, old_session, 4));

    platform_gamepad_snapshot_t current;
    EXPECT_EQ(GAMEPAD_OK, platform_gamepad_model_copy(&model, &current));
    EXPECT_TRUE(current.state.connected);
    EXPECT_EQ(new_session, current.session);
    EXPECT_TRUE(memcmp(&current.identity, &second, sizeof(second)) == 0);
}

static void test_report_commit_is_transactional(void)
{
    platform_gamepad_model_t model;
    platform_gamepad_model_init(&model);
    const platform_gamepad_identity_t controller = identity(4);
    uint32_t session;
    EXPECT_EQ(GAMEPAD_OK,
              platform_gamepad_model_connect(&model, &controller,
                                             GAMEPAD_CAP_BUTTONS, 10,
                                             &session));
    platform_gamepad_snapshot_t before;
    EXPECT_EQ(GAMEPAD_OK, platform_gamepad_model_copy(&model, &before));
    gamepad_state_t invalid = before.state;
    invalid.buttons = UINT64_MAX;
    invalid.timestamp_us = 11;
    EXPECT_EQ(GAMEPAD_ERR_INVALID_STATE,
              platform_gamepad_model_commit_report(&model, session, &invalid));
    platform_gamepad_snapshot_t after;
    EXPECT_EQ(GAMEPAD_OK, platform_gamepad_model_copy(&model, &after));
    EXPECT_TRUE(memcmp(&before, &after, sizeof(after)) == 0);
}

int main(void)
{
    test_usb_open_guard_clears_failure_and_preserves_success();
    test_disconnect_neutralizes_and_rejects_queued_report();
    test_old_session_cannot_clear_or_update_reconnected_device();
    test_report_commit_is_transactional();
    if (failures != 0) {
        fprintf(stderr, "%d platform_gamepad model test(s) failed\n", failures);
        return 1;
    }
    puts("platform_gamepad model tests passed");
    return 0;
}
