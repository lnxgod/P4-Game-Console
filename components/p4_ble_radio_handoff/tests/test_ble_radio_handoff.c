// SPDX-License-Identifier: MIT

#include "p4/ble_radio_handoff.h"

#include <stdbool.h>
#include <stdio.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void test_unbonded_idle_pad_is_ready(void)
{
    p4_ble_radio_handoff_t handoff;
    p4_ble_radio_handoff_init(&handoff);
    CHECK(p4_ble_radio_handoff_request(
              &handoff, false, false, false) ==
          P4_BLE_RADIO_HANDOFF_READY);
    CHECK(!handoff.waiting_for_gamepad);
    CHECK(!handoff.reconnect_suspended);
}

static void test_connected_pad_coexists(void)
{
    p4_ble_radio_handoff_t handoff;
    p4_ble_radio_handoff_init(&handoff);
    CHECK(p4_ble_radio_handoff_request(
              &handoff, true, true, true) ==
          P4_BLE_RADIO_HANDOFF_READY);
    CHECK(!p4_ble_radio_handoff_release(
        &handoff, true, true, true));
}

static void test_saved_reconnect_yields_then_resumes(void)
{
    p4_ble_radio_handoff_t handoff;
    p4_ble_radio_handoff_init(&handoff);
    CHECK(p4_ble_radio_handoff_request(
              &handoff, false, true, false) ==
          P4_BLE_RADIO_HANDOFF_CANCEL_GAMEPAD);
    CHECK(handoff.waiting_for_gamepad);
    CHECK(handoff.reconnect_suspended);
    CHECK(p4_ble_radio_handoff_request(
              &handoff, false, true, false) ==
          P4_BLE_RADIO_HANDOFF_WAIT);
    CHECK(p4_ble_radio_handoff_poll(&handoff, false) ==
          P4_BLE_RADIO_HANDOFF_WAIT);
    CHECK(p4_ble_radio_handoff_poll(&handoff, true) ==
          P4_BLE_RADIO_HANDOFF_READY);
    CHECK(!handoff.waiting_for_gamepad);
    CHECK(handoff.reconnect_suspended);
    CHECK(p4_ble_radio_handoff_release(
        &handoff, true, false, true));
    CHECK(!handoff.waiting_for_gamepad);
    CHECK(!handoff.reconnect_suspended);
}

static void test_unbonded_pairing_is_cancelled_without_resume(void)
{
    p4_ble_radio_handoff_t handoff;
    p4_ble_radio_handoff_init(&handoff);
    CHECK(p4_ble_radio_handoff_request(
              &handoff, false, false, true) ==
          P4_BLE_RADIO_HANDOFF_CANCEL_GAMEPAD);
    CHECK(p4_ble_radio_handoff_poll(&handoff, true) ==
          P4_BLE_RADIO_HANDOFF_READY);
    CHECK(!p4_ble_radio_handoff_release(
        &handoff, true, false, false));
}

static void test_disabled_or_unbonded_release_never_resumes(void)
{
    p4_ble_radio_handoff_t handoff;
    p4_ble_radio_handoff_init(&handoff);
    CHECK(p4_ble_radio_handoff_request(
              &handoff, false, true, true) ==
          P4_BLE_RADIO_HANDOFF_CANCEL_GAMEPAD);
    CHECK(!p4_ble_radio_handoff_release(
        &handoff, false, false, true));

    p4_ble_radio_handoff_init(&handoff);
    CHECK(p4_ble_radio_handoff_request(
              &handoff, false, true, true) ==
          P4_BLE_RADIO_HANDOFF_CANCEL_GAMEPAD);
    CHECK(!p4_ble_radio_handoff_release(
        &handoff, true, false, false));
}

int main(void)
{
    test_unbonded_idle_pad_is_ready();
    test_connected_pad_coexists();
    test_saved_reconnect_yields_then_resumes();
    test_unbonded_pairing_is_cancelled_without_resume();
    test_disabled_or_unbonded_release_never_resumes();
    if (failures != 0) {
        fprintf(stderr, "%d BLE radio handoff test(s) failed\n", failures);
        return 1;
    }
    puts("BLE radio handoff tests passed");
    return 0;
}
