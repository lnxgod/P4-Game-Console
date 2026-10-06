// SPDX-License-Identifier: MIT
#include "platform/gamepad.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static platform_gamepad_snapshot_t hid,xusb,ble;
static esp_err_t hid_read(platform_gamepad_snapshot_t *out){*out=hid;return ESP_OK;}
static esp_err_t xusb_read(platform_gamepad_snapshot_t *out){*out=xusb;return ESP_OK;}
static esp_err_t ble_read(platform_gamepad_snapshot_t *out){*out=ble;return ESP_OK;}
static platform_gamepad_snapshot_t provider(platform_gamepad_transport_t transport)
{
    platform_gamepad_model_t model;platform_gamepad_model_init(&model);
    const platform_gamepad_identity_t identity={.transport=(uint8_t)transport,.descriptor_sha256={1}};
    uint32_t session;
    assert(platform_gamepad_model_connect(&model,&identity,GAMEPAD_CAP_BUTTONS,1,&session)==GAMEPAD_OK);
    platform_gamepad_snapshot_t snapshot;
    assert(platform_gamepad_model_copy(&model,&snapshot)==GAMEPAD_OK);
    snapshot.state.buttons=GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH);
    return snapshot;
}
int main(void)
{
    hid=provider(PLATFORM_GAMEPAD_TRANSPORT_USB_HID);
    xusb=provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB);
    ble=provider(PLATFORM_GAMEPAD_TRANSPORT_BLE_HID);
    assert(platform_gamepad_register_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_HID,hid_read)==ESP_OK);
    assert(platform_gamepad_register_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB,xusb_read)==ESP_OK);
    assert(platform_gamepad_register_provider(PLATFORM_GAMEPAD_TRANSPORT_BLE_HID,ble_read)==ESP_OK);
    platform_gamepad_snapshot_t out;
    assert(platform_gamepad_get_snapshot(&out)==ESP_OK && out.identity.transport==PLATFORM_GAMEPAD_TRANSPORT_USB_HID);
    assert(gamepad_state_disconnect(&hid.state,2)==GAMEPAD_OK);
    assert(platform_gamepad_get_snapshot(&out)==ESP_OK && out.identity.transport==PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB);
    gamepad_button_mapping_t mapping;gamepad_button_mapping_default(&mapping);
    mapping.source[GAMEPAD_MAPPING_A]=GAMEPAD_BUTTON_EAST;
    mapping.source[GAMEPAD_MAPPING_B]=GAMEPAD_BUTTON_SOUTH;
    assert(platform_gamepad_set_mapping(&mapping)==ESP_OK);
    assert(platform_gamepad_get_snapshot(&out)==ESP_OK);
    assert(out.state.buttons==GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_EAST));
    assert(platform_gamepad_get_raw_snapshot(&out)==ESP_OK);
    assert(out.state.buttons==GAMEPAD_BUTTON_MASK(GAMEPAD_BUTTON_SOUTH));
    platform_gamepad_unregister_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB,xusb_read);
    assert(platform_gamepad_get_snapshot(&out)==ESP_OK && out.identity.transport==PLATFORM_GAMEPAD_TRANSPORT_BLE_HID);
    assert(gamepad_state_disconnect(&ble.state,3)==GAMEPAD_OK);
    assert(platform_gamepad_get_snapshot(&out)==ESP_OK && !out.state.connected && gamepad_state_is_neutral(&out.state));
    puts("controller broker: HID/XUSB/BLE priority, mapping and neutral fallback passed");
    return 0;
}
