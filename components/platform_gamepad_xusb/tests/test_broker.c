// SPDX-License-Identifier: MIT
#include "platform/gamepad.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static platform_gamepad_snapshot_t hid, xusb, ble;
static esp_err_t hid_result = ESP_OK;
static unsigned int hid_reads, xusb_reads, ble_reads;

static esp_err_t hid_read(platform_gamepad_snapshot_t *out)
{
    ++hid_reads;
    *out = hid;
    return hid_result;
}

static esp_err_t xusb_read(platform_gamepad_snapshot_t *out)
{
    ++xusb_reads;
    *out = xusb;
    return ESP_OK;
}

static esp_err_t ble_read(platform_gamepad_snapshot_t *out)
{
    ++ble_reads;
    *out = ble;
    return ESP_OK;
}

static platform_gamepad_snapshot_t fresh_provider(void)
{
    platform_gamepad_model_t model;
    platform_gamepad_model_init(&model);
    platform_gamepad_snapshot_t snapshot;
    assert(platform_gamepad_model_copy(&model, &snapshot) == GAMEPAD_OK);
    return snapshot;
}

static platform_gamepad_snapshot_t provider(platform_gamepad_transport_t transport)
{
    platform_gamepad_model_t model;
    platform_gamepad_model_init(&model);
    const platform_gamepad_identity_t identity = {
        .transport = (uint8_t)transport, .descriptor_sha256 = {1},
    };
    uint32_t session;
    assert(platform_gamepad_model_connect(
        &model, &identity, GAMEPAD_CAP_BUTTONS, 1, &session) == GAMEPAD_OK);
    platform_gamepad_snapshot_t snapshot;
    assert(platform_gamepad_model_copy(&model, &snapshot) == GAMEPAD_OK);
    snapshot.state.buttons = GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH);
    return snapshot;
}

static void register_all(void)
{
    assert(platform_gamepad_register_provider(
        PLATFORM_GAMEPAD_TRANSPORT_USB_HID, hid_read) == ESP_OK);
    assert(platform_gamepad_register_provider(
        PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB, xusb_read) == ESP_OK);
    assert(platform_gamepad_register_provider(
        PLATFORM_GAMEPAD_TRANSPORT_BLE_HID, ble_read) == ESP_OK);
}

static void unregister_all(void)
{
    platform_gamepad_unregister_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_HID, hid_read);
    platform_gamepad_unregister_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB, xusb_read);
    platform_gamepad_unregister_provider(PLATFORM_GAMEPAD_TRANSPORT_BLE_HID, ble_read);
}

static void assert_fresh_neutral(void)
{
    platform_gamepad_snapshot_t out;
    assert(platform_gamepad_get_snapshot(&out) == ESP_OK);
    assert(out.state.connected == 0U && gamepad_state_is_neutral(&out.state));
    assert(out.identity.transport == PLATFORM_GAMEPAD_TRANSPORT_NONE);
    assert(out.session == 0U && out.capabilities == 0U);
    assert(platform_gamepad_get_raw_snapshot(&out) == ESP_OK);
    assert(out.state.connected == 0U && gamepad_state_is_neutral(&out.state));
}

static void test_never_connected(void)
{
    platform_gamepad_snapshot_t out;
    assert(platform_gamepad_get_snapshot(&out) == ESP_ERR_INVALID_STATE);
    hid = xusb = ble = fresh_provider();
    register_all();
    assert_fresh_neutral();
    assert(hid_reads == 2U && xusb_reads == 2U && ble_reads == 2U);

    /* A fresh high-priority provider must not hide a connected lower tier. */
    xusb = provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB);
    assert(platform_gamepad_get_snapshot(&out) == ESP_OK);
    assert(out.identity.transport == PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB);
    xusb = fresh_provider();
    ble = provider(PLATFORM_GAMEPAD_TRANSPORT_BLE_HID);
    assert(platform_gamepad_get_snapshot(&out) == ESP_OK);
    assert(out.identity.transport == PLATFORM_GAMEPAD_TRANSPORT_BLE_HID);
    ble = fresh_provider();
    unregister_all();

    /* Each transport can independently report its genuine initial state. */
    assert(platform_gamepad_register_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_HID, hid_read) == ESP_OK);
    assert_fresh_neutral();
    unregister_all();
    assert(platform_gamepad_register_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB, xusb_read) == ESP_OK);
    assert_fresh_neutral();
    unregister_all();
    assert(platform_gamepad_register_provider(PLATFORM_GAMEPAD_TRANSPORT_BLE_HID, ble_read) == ESP_OK);
    assert_fresh_neutral();
    unregister_all();
}

static void assert_invalid_hid(void)
{
    platform_gamepad_snapshot_t out;
    memset(&out, 0xa5, sizeof(out));
    assert(platform_gamepad_get_snapshot(&out) == ESP_ERR_INVALID_RESPONSE);
    const platform_gamepad_snapshot_t empty = {0};
    assert(memcmp(&out, &empty, sizeof(out)) == 0);
    assert(platform_gamepad_get_raw_snapshot(&out) == ESP_ERR_INVALID_RESPONSE);
}

static void test_invalid_initial_state(void)
{
    assert(platform_gamepad_register_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_HID, hid_read) == ESP_OK);
    const platform_gamepad_snapshot_t fresh = fresh_provider();
    /* The NONE exception is only for the neutral model before connection. */
    hid = fresh; hid.state.connected = 1U; assert_invalid_hid();
    hid = fresh; hid.state.connected = 2U; assert_invalid_hid();
    hid = fresh; hid.state.buttons = 1U; assert_invalid_hid();
    hid = fresh; hid.state.dpad = GAMEPAD_DPAD_UP; assert_invalid_hid();
    hid = fresh; hid.state.left_x = 1; assert_invalid_hid();
    hid = fresh; hid.state.left_y = -1; assert_invalid_hid();
    hid = fresh; hid.state.right_x = 1; assert_invalid_hid();
    hid = fresh; hid.state.right_y = -1; assert_invalid_hid();
    hid = fresh; hid.state.left_trigger = 1U; assert_invalid_hid();
    hid = fresh; hid.state.right_trigger = 1U; assert_invalid_hid();
    hid = fresh; hid.session = 1U; assert_invalid_hid();
    hid = fresh; hid.capabilities = GAMEPAD_CAP_BUTTONS; assert_invalid_hid();
    hid = fresh; hid.state.sequence = 1U; assert_invalid_hid();
    hid = fresh; hid.state.timestamp_us = 1U; assert_invalid_hid();
    hid = fresh; hid.state.reserved[0] = 1U; assert_invalid_hid();
    hid = fresh; hid.state.reserved[1] = 1U; assert_invalid_hid();
    hid = fresh; hid.identity.vendor_id = 1U; assert_invalid_hid();
    hid = fresh; hid.identity.product_id = 1U; assert_invalid_hid();
    hid = fresh; hid.identity.interface_number = 1U; assert_invalid_hid();
    hid = fresh; hid.identity.descriptor_sha256[31] = 1U; assert_invalid_hid();
    hid = fresh; hid.identity.reserved[1] = 1U; assert_invalid_hid();
    hid = fresh; hid.identity.transport = PLATFORM_GAMEPAD_TRANSPORT_BLE_HID; assert_invalid_hid();
    hid = fresh; hid.version = 0U; assert_invalid_hid();
    hid = fresh; hid.size = 0U; assert_invalid_hid();
    hid = fresh; hid.state.version = 0U; assert_invalid_hid();
    hid = fresh; hid.state.size = 0U; assert_invalid_hid();
    hid = provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB); assert_invalid_hid();
    hid = provider(PLATFORM_GAMEPAD_TRANSPORT_USB_HID);
    hid.identity.transport = PLATFORM_GAMEPAD_TRANSPORT_NONE; assert_invalid_hid();

    /* A provider failure is still a failure; a valid fallback still wins. */
    hid = fresh; hid_result = ESP_ERR_TIMEOUT;
    platform_gamepad_snapshot_t out;
    assert(platform_gamepad_get_snapshot(&out) == ESP_ERR_TIMEOUT);
    xusb = fresh;
    assert(platform_gamepad_register_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB, xusb_read) == ESP_OK);
    assert_fresh_neutral();
    hid_result = ESP_OK;
    hid.state.connected = 1U;
    assert_fresh_neutral();
    unregister_all();
}

static void test_connected_priority_mapping_disconnect(void)
{
    hid = provider(PLATFORM_GAMEPAD_TRANSPORT_USB_HID);
    xusb = provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB);
    ble = provider(PLATFORM_GAMEPAD_TRANSPORT_BLE_HID);
    register_all();
    platform_gamepad_snapshot_t out;
    assert(platform_gamepad_get_snapshot(&out) == ESP_OK && out.identity.transport == PLATFORM_GAMEPAD_TRANSPORT_USB_HID);
    assert(gamepad_state_disconnect(&hid.state, 2) == GAMEPAD_OK);
    assert(platform_gamepad_get_snapshot(&out) == ESP_OK && out.identity.transport == PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB);
    gamepad_button_mapping_t mapping;
    gamepad_button_mapping_default(&mapping);
    mapping.source[GAMEPAD_MAPPING_A] = GAMEPAD_BUTTON_EAST;
    mapping.source[GAMEPAD_MAPPING_B] = GAMEPAD_BUTTON_SOUTH;
    assert(platform_gamepad_set_mapping(&mapping) == ESP_OK);
    assert(platform_gamepad_get_snapshot(&out) == ESP_OK);
    assert(out.state.buttons == GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST));
    assert(platform_gamepad_get_raw_snapshot(&out) == ESP_OK);
    assert(out.state.buttons == GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH));
    platform_gamepad_unregister_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB, xusb_read);
    assert(platform_gamepad_get_snapshot(&out) == ESP_OK && out.identity.transport == PLATFORM_GAMEPAD_TRANSPORT_BLE_HID);
    assert(gamepad_state_disconnect(&ble.state, 3) == GAMEPAD_OK);
    assert(platform_gamepad_get_snapshot(&out) == ESP_OK && !out.state.connected && gamepad_state_is_neutral(&out.state));
    unregister_all();
}

int main(void)
{
    test_never_connected();
    test_invalid_initial_state();
    test_connected_priority_mapping_disconnect();
    puts("controller broker: fresh-neutral acceptance, malformed rejection, HID/XUSB/BLE priority, mapping and disconnect passed");
    return 0;
}
