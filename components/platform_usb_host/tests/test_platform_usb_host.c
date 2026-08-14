#include "platform_usb_host/model.h"

#include <stdio.h>
#include <string.h>

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

static platform_usb_fixture_evidence_t valid_evidence(void)
{
    platform_usb_fixture_evidence_t evidence = {
        .version = PLATFORM_USB_FIXTURE_EVIDENCE_VERSION,
        .size = sizeof(evidence),
        .current_limit_ma = 500,
        .externally_powered_vbus = 1,
        .current_limited = 1,
        .backfeed_blocked = 1,
        .common_ground = 1,
        .data_pair_direct = 1,
        .source_role_compliant = 1,
        .overcurrent_fault_visible = 1,
        .board_path_reviewed = 1,
    };
    memcpy(evidence.evidence_id, "fixture/rev-a/review-1", 23);
    memcpy(evidence.evidence_sha256,
           "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
           65);
    return evidence;
}

static void test_fixture_gate_requires_every_property(void)
{
    platform_usb_fixture_evidence_t evidence = valid_evidence();
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_fixture_evidence_validate(&evidence));

    uint8_t *const required_flags[] = {
        &evidence.externally_powered_vbus,
        &evidence.current_limited,
        &evidence.backfeed_blocked,
        &evidence.common_ground,
        &evidence.data_pair_direct,
        &evidence.source_role_compliant,
        &evidence.overcurrent_fault_visible,
        &evidence.board_path_reviewed,
    };
    for (size_t index = 0;
         index < sizeof(required_flags) / sizeof(required_flags[0]); ++index) {
        evidence = valid_evidence();
        *required_flags[index] = 0;
        EXPECT_EQ(PLATFORM_USB_STATUS_FIXTURE_REQUIRED,
                  platform_usb_fixture_evidence_validate(&evidence));
    }

    evidence = valid_evidence();
    evidence.current_limit_ma = 99;
    EXPECT_EQ(PLATFORM_USB_STATUS_FIXTURE_REQUIRED,
              platform_usb_fixture_evidence_validate(&evidence));
    evidence = valid_evidence();
    evidence.current_limit_ma = 501;
    EXPECT_EQ(PLATFORM_USB_STATUS_FIXTURE_REQUIRED,
              platform_usb_fixture_evidence_validate(&evidence));
    evidence = valid_evidence();
    memset(evidence.evidence_id, 'x', sizeof(evidence.evidence_id));
    EXPECT_EQ(PLATFORM_USB_STATUS_FIXTURE_REQUIRED,
              platform_usb_fixture_evidence_validate(&evidence));
    evidence = valid_evidence();
    evidence.common_ground = 2;
    EXPECT_EQ(PLATFORM_USB_STATUS_INVALID_ARGUMENT,
              platform_usb_fixture_evidence_validate(&evidence));
    evidence = valid_evidence();
    evidence.evidence_sha256[63] = 'G';
    EXPECT_EQ(PLATFORM_USB_STATUS_FIXTURE_REQUIRED,
              platform_usb_fixture_evidence_validate(&evidence));
    evidence = valid_evidence();
    evidence.evidence_id[0] = ' ';
    EXPECT_EQ(PLATFORM_USB_STATUS_FIXTURE_REQUIRED,
              platform_usb_fixture_evidence_validate(&evidence));
    evidence = valid_evidence();
    evidence.version++;
    EXPECT_EQ(PLATFORM_USB_STATUS_INVALID_ARGUMENT,
              platform_usb_fixture_evidence_validate(&evidence));
    evidence = valid_evidence();
    evidence.size--;
    EXPECT_EQ(PLATFORM_USB_STATUS_INVALID_ARGUMENT,
              platform_usb_fixture_evidence_validate(&evidence));
}

static void test_class_leases_block_teardown_and_reject_stale_release(void)
{
    platform_usb_host_model_t model;
    platform_usb_host_model_init(&model);
    platform_usb_fixture_evidence_t evidence = valid_evidence();
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_start(&model, &evidence));
    EXPECT_EQ(PLATFORM_USB_HOST_STARTING, model.state);
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_start(&model, true));
    EXPECT_EQ(PLATFORM_USB_HOST_READY, model.state);
    EXPECT_EQ(0, model.root_port_enabled);

    platform_usb_class_lease_t hid_lease;
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_acquire(&model, PLATFORM_USB_CLASS_HID,
                                              &hid_lease));
    platform_usb_class_lease_t duplicate;
    EXPECT_EQ(PLATFORM_USB_STATUS_CLASS_BUSY,
              platform_usb_host_model_acquire(&model, PLATFORM_USB_CLASS_HID,
                                              &duplicate));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_root_port_enable(&model));
    EXPECT_EQ(PLATFORM_USB_HOST_ENABLING_ROOT_PORT, model.state);
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_root_port_enable(&model, true));
    EXPECT_EQ(PLATFORM_USB_HOST_RUNNING, model.state);
    EXPECT_EQ(1, model.root_port_enabled);
    EXPECT_EQ(PLATFORM_USB_STATUS_INVALID_STATE,
              platform_usb_host_model_acquire(&model, PLATFORM_USB_CLASS_AUDIO,
                                              &duplicate));

    platform_usb_class_lease_t stale_copy = hid_lease;
    EXPECT_EQ(PLATFORM_USB_STATUS_STALE_LEASE,
              platform_usb_host_model_release(&model, &stale_copy));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_quiesce(&model));
    EXPECT_EQ(PLATFORM_USB_HOST_QUIESCING, model.state);
    EXPECT_EQ(1, model.root_port_enabled);
    EXPECT_EQ(PLATFORM_USB_STATUS_INVALID_STATE,
              platform_usb_host_model_acquire(&model, PLATFORM_USB_CLASS_AUDIO,
                                              &duplicate));
    EXPECT_EQ(PLATFORM_USB_STATUS_INVALID_STATE,
              platform_usb_host_model_begin_stop(&model));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_quiesce(&model, true));
    EXPECT_EQ(0, model.root_port_enabled);
    EXPECT_EQ(PLATFORM_USB_STATUS_CLASS_BUSY,
              platform_usb_host_model_begin_stop(&model));

    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_release(&model, &hid_lease));
    EXPECT_EQ(0, hid_lease.version);
    EXPECT_EQ(PLATFORM_USB_STATUS_STALE_LEASE,
              platform_usb_host_model_release(&model, &stale_copy));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_stop(&model));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_stop(&model, true));
    EXPECT_EQ(PLATFORM_USB_HOST_STOPPED, model.state);
}

static void test_root_port_enable_requires_registered_class_and_retries(void)
{
    platform_usb_host_model_t model;
    platform_usb_host_model_init(&model);
    platform_usb_fixture_evidence_t evidence = valid_evidence();
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_start(&model, &evidence));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_start(&model, true));
    EXPECT_EQ(PLATFORM_USB_STATUS_INVALID_STATE,
              platform_usb_host_model_begin_root_port_enable(&model));

    platform_usb_class_lease_t hid_lease;
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_acquire(&model, PLATFORM_USB_CLASS_HID,
                                              &hid_lease));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_root_port_enable(&model));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_root_port_enable(&model, false));
    EXPECT_EQ(PLATFORM_USB_HOST_READY, model.state);
    EXPECT_EQ(0, model.root_port_enabled);
    EXPECT_TRUE(model.lease_mask != 0U);

    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_root_port_enable(&model));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_root_port_enable(&model, true));
    EXPECT_EQ(PLATFORM_USB_HOST_RUNNING, model.state);
    EXPECT_EQ(1, model.root_port_enabled);

    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_quiesce(&model));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_quiesce(&model, true));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_release(&model, &hid_lease));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_stop(&model));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_stop(&model, true));
}

static void test_failed_start_is_retryable_with_new_generation(void)
{
    platform_usb_host_model_t model;
    platform_usb_host_model_init(&model);
    platform_usb_fixture_evidence_t evidence = valid_evidence();
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_start(&model, &evidence));
    const uint32_t first_generation = model.generation;
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_start(&model, false));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_start(&model, &evidence));
    EXPECT_TRUE(model.generation != first_generation);
}

static void test_uncertain_cleanup_enters_terminal_fault(void)
{
    platform_usb_host_model_t model;
    platform_usb_host_model_init(&model);
    platform_usb_fixture_evidence_t evidence = valid_evidence();
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_start(&model, &evidence));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_mark_fault(&model));
    EXPECT_EQ(PLATFORM_USB_HOST_FAULT, model.state);
    EXPECT_EQ(PLATFORM_USB_STATUS_INVALID_STATE,
              platform_usb_host_model_begin_start(&model, &evidence));
    EXPECT_EQ(PLATFORM_USB_STATUS_INVALID_STATE,
              platform_usb_host_model_begin_stop(&model));
}

static void test_stop_requires_quiesce_even_without_leases(void)
{
    platform_usb_host_model_t model;
    platform_usb_host_model_init(&model);
    platform_usb_fixture_evidence_t evidence = valid_evidence();
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_start(&model, &evidence));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_start(&model, true));
    EXPECT_EQ(PLATFORM_USB_STATUS_INVALID_STATE,
              platform_usb_host_model_begin_stop(&model));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_quiesce(&model));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_quiesce(&model, true));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_stop(&model));
}

static void test_failed_quiesce_enters_terminal_fault(void)
{
    platform_usb_host_model_t model;
    platform_usb_host_model_init(&model);
    platform_usb_fixture_evidence_t evidence = valid_evidence();
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_start(&model, &evidence));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_start(&model, true));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_begin_quiesce(&model));
    EXPECT_EQ(PLATFORM_USB_STATUS_OK,
              platform_usb_host_model_complete_quiesce(&model, false));
    EXPECT_EQ(PLATFORM_USB_HOST_FAULT, model.state);
}

int main(void)
{
    test_fixture_gate_requires_every_property();
    test_class_leases_block_teardown_and_reject_stale_release();
    test_root_port_enable_requires_registered_class_and_retries();
    test_failed_start_is_retryable_with_new_generation();
    test_uncertain_cleanup_enters_terminal_fault();
    test_stop_requires_quiesce_even_without_leases();
    test_failed_quiesce_enters_terminal_fault();
    if (failures != 0) {
        fprintf(stderr, "%d platform_usb_host test(s) failed\n", failures);
        return 1;
    }
    puts("platform_usb_host tests passed");
    return 0;
}
