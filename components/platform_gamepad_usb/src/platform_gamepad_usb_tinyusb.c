// SPDX-License-Identifier: MIT

#include "platform_gamepad_usb/platform_gamepad_usb.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "gamepad/hid_gamepad.h"
#include "mbedtls/sha256.h"
#include "platform_gamepad_usb/input_model.h"
#include "platform_usb_host/platform_usb_host.h"
#include "tusb.h"

enum {
    TINYUSB_ADDRESS_MAX = CFG_TUH_DEVICE_MAX + CFG_TUH_HUB,
};

typedef struct {
    platform_usb_input_kind_t kind;
    uint32_t session;
    bool active;
} tinyusb_hid_slot_t;

typedef struct {
    bool running;
    platform_usb_class_lease_t host_lease;
    uint8_t gamepad_address;
    uint8_t gamepad_instance;
    uint32_t gamepad_session;
    gamepad_hid_layout_t gamepad_layout;
    tinyusb_hid_slot_t slots[TINYUSB_ADDRESS_MAX + 1U][CFG_TUH_HID];
    platform_gamepad_usb_stats_t stats;
} tinyusb_gamepad_service_t;

static const char *const TAG = "platform_gamepad";
static portMUX_TYPE s_service_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static tinyusb_gamepad_service_t s_service;
static platform_gamepad_model_t s_gamepad_model;
static platform_usb_input_model_t s_aux_input_model;

static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static void increment_stat(uint32_t *counter)
{
    taskENTER_CRITICAL(&s_service_lock);
    if (*counter != UINT32_MAX) {
        ++*counter;
    }
    taskEXIT_CRITICAL(&s_service_lock);
}

static void descriptor_hash_hex(const uint8_t hash[32], char output[65])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0U; index < 32U; ++index) {
        output[index * 2U] = digits[hash[index] >> 4U];
        output[index * 2U + 1U] = digits[hash[index] & 0x0fU];
    }
    output[64] = '\0';
}

static const char *speed_name(uint8_t speed)
{
    switch ((tusb_speed_t)speed) {
    case TUSB_SPEED_LOW:
        return "low";
    case TUSB_SPEED_FULL:
        return "full";
    case TUSB_SPEED_HIGH:
        return "high";
    default:
        return "unknown";
    }
}

static bool service_running(void)
{
    bool running;
    taskENTER_CRITICAL(&s_service_lock);
    running = s_service.running;
    taskEXIT_CRITICAL(&s_service_lock);
    return running;
}

static void disconnect_slot(uint8_t address, uint8_t instance)
{
    if (address > TINYUSB_ADDRESS_MAX || instance >= CFG_TUH_HID) {
        return;
    }

    tinyusb_hid_slot_t prior = {0};
    taskENTER_CRITICAL(&s_service_lock);
    tinyusb_hid_slot_t *const slot = &s_service.slots[address][instance];
    if (slot->active) {
        prior = *slot;
        memset(slot, 0, sizeof(*slot));
        if (prior.kind == PLATFORM_USB_INPUT_KIND_GAMEPAD &&
            s_service.gamepad_address == address &&
            s_service.gamepad_instance == instance) {
            s_service.gamepad_address = 0U;
            s_service.gamepad_instance = 0U;
            s_service.gamepad_session = 0U;
            gamepad_hid_layout_init(&s_service.gamepad_layout);
        }
    }
    taskEXIT_CRITICAL(&s_service_lock);
    if (!prior.active) {
        return;
    }

    taskENTER_CRITICAL(&s_snapshot_lock);
    if (prior.kind == PLATFORM_USB_INPUT_KIND_GAMEPAD) {
        (void)platform_gamepad_model_disconnect(
            &s_gamepad_model, prior.session, now_us());
    } else {
        (void)platform_usb_input_model_disconnect(
            &s_aux_input_model, prior.kind, prior.session, now_us());
    }
    taskEXIT_CRITICAL(&s_snapshot_lock);
    increment_stat(&s_service.stats.disconnections);
    ESP_LOGI(TAG,
             "USB_INPUT_DISCONNECTED addr=%u instance=%u kind=%u input=neutral",
             (unsigned)address, (unsigned)instance, (unsigned)prior.kind);
}

static bool connect_auxiliary(uint8_t address, uint8_t instance,
                              platform_usb_input_kind_t kind)
{
    uint32_t session = 0U;
    taskENTER_CRITICAL(&s_snapshot_lock);
    const platform_usb_input_status_t status =
        platform_usb_input_model_connect(
            &s_aux_input_model, kind, now_us(), &session);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    if (status != PLATFORM_USB_INPUT_OK) {
        return false;
    }

    taskENTER_CRITICAL(&s_service_lock);
    s_service.slots[address][instance] = (tinyusb_hid_slot_t){
        .kind = kind,
        .session = session,
        .active = true,
    };
    taskEXIT_CRITICAL(&s_service_lock);
    increment_stat(&s_service.stats.connections);
    if (kind == PLATFORM_USB_INPUT_KIND_KEYBOARD) {
        increment_stat(&s_service.stats.keyboard_connections);
    } else {
        increment_stat(&s_service.stats.mouse_connections);
    }
    ESP_LOGI(TAG,
             "USB_INPUT_CONNECTED addr=%u instance=%u kind=%s session=%" PRIu32,
             (unsigned)address, (unsigned)instance,
             kind == PLATFORM_USB_INPUT_KIND_KEYBOARD ? "keyboard" : "mouse",
             session);
    return true;
}

static bool connect_gamepad(uint8_t address, uint8_t instance,
                            const uint8_t *descriptor,
                            uint16_t descriptor_bytes)
{
    if (descriptor == NULL || descriptor_bytes == 0U ||
        descriptor_bytes > GAMEPAD_HID_MAX_DESCRIPTOR_BYTES) {
        return false;
    }

    taskENTER_CRITICAL(&s_service_lock);
    const bool busy = s_service.gamepad_session != 0U;
    taskEXIT_CRITICAL(&s_service_lock);
    if (busy) {
        return false;
    }

    gamepad_hid_layout_t layout;
    if (gamepad_hid_parse_descriptor(descriptor, descriptor_bytes,
                                     &layout) != GAMEPAD_OK) {
        return false;
    }

    uint16_t vendor_id = 0U;
    uint16_t product_id = 0U;
    if (!tuh_vid_pid_get(address, &vendor_id, &product_id)) {
        return false;
    }
    uint8_t descriptor_sha256[32];
    if (mbedtls_sha256(descriptor, descriptor_bytes,
                       descriptor_sha256, 0) != 0) {
        return false;
    }
    gamepad_hid_profile_t profile = GAMEPAD_HID_PROFILE_NONE;
    if (gamepad_hid_apply_known_profile(
            vendor_id, product_id, descriptor_sha256,
            &layout, &profile) != GAMEPAD_OK) {
        return false;
    }

    const platform_gamepad_identity_t identity = {
        .vendor_id = vendor_id,
        .product_id = product_id,
        .interface_number = instance,
        .transport = PLATFORM_GAMEPAD_TRANSPORT_USB_HID,
        .descriptor_sha256 = {0},
    };
    platform_gamepad_identity_t complete_identity = identity;
    memcpy(complete_identity.descriptor_sha256, descriptor_sha256,
           sizeof(descriptor_sha256));

    uint32_t session = 0U;
    taskENTER_CRITICAL(&s_snapshot_lock);
    const gamepad_status_t connect_status =
        platform_gamepad_model_connect(
            &s_gamepad_model, &complete_identity,
            gamepad_hid_capabilities(&layout), now_us(), &session);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    if (connect_status != GAMEPAD_OK) {
        return false;
    }

    taskENTER_CRITICAL(&s_service_lock);
    s_service.gamepad_address = address;
    s_service.gamepad_instance = instance;
    s_service.gamepad_session = session;
    s_service.gamepad_layout = layout;
    s_service.slots[address][instance] = (tinyusb_hid_slot_t){
        .kind = PLATFORM_USB_INPUT_KIND_GAMEPAD,
        .session = session,
        .active = true,
    };
    taskEXIT_CRITICAL(&s_service_lock);
    increment_stat(&s_service.stats.connections);
    increment_stat(&s_service.stats.gamepad_connections);

    char hash_hex[65];
    descriptor_hash_hex(descriptor_sha256, hash_hex);
    ESP_LOGI(TAG,
             "GAMEPAD_CONNECTED session=%" PRIu32
             " addr=%u instance=%u vid=%04x pid=%04x descriptor_bytes=%u "
             "descriptor_sha256=%s profile=%s capabilities=0x%08" PRIx32,
             session, (unsigned)address, (unsigned)instance,
             (unsigned)vendor_id, (unsigned)product_id,
             (unsigned)descriptor_bytes, hash_hex,
             gamepad_hid_profile_name(profile),
             gamepad_hid_capabilities(&layout));
    return true;
}

void tuh_mount_cb(uint8_t address)
{
    uint16_t vendor_id = 0U;
    uint16_t product_id = 0U;
    tuh_bus_info_t route = {0};
    const bool identity_ok =
        tuh_vid_pid_get(address, &vendor_id, &product_id);
    const bool route_ok = tuh_bus_info_get(address, &route);
    ESP_LOGI(TAG,
             "USB_DEVICE_MOUNT addr=%u vid=%04x pid=%04x speed=%s "
             "parent_hub=%u parent_port=%u identity_ok=%u route_ok=%u",
             (unsigned)address, (unsigned)vendor_id, (unsigned)product_id,
             route_ok ? speed_name(route.speed) : "unknown",
             route_ok ? (unsigned)route.hub_addr : 0U,
             route_ok ? (unsigned)route.hub_port : 0U,
             identity_ok ? 1U : 0U, route_ok ? 1U : 0U);
}

void tuh_umount_cb(uint8_t address)
{
    if (address <= TINYUSB_ADDRESS_MAX) {
        for (uint8_t instance = 0U; instance < CFG_TUH_HID; ++instance) {
            disconnect_slot(address, instance);
        }
    }
    ESP_LOGI(TAG, "USB_DEVICE_UNMOUNT addr=%u input=neutral",
             (unsigned)address);
}

void tuh_hid_mount_cb(uint8_t address, uint8_t instance,
                      const uint8_t *descriptor, uint16_t descriptor_bytes)
{
    if (!service_running() || address > TINYUSB_ADDRESS_MAX ||
        instance >= CFG_TUH_HID) {
        return;
    }
    increment_stat(&s_service.stats.interfaces_seen);

    const uint8_t protocol =
        tuh_hid_interface_protocol(address, instance);
    bool accepted = false;
    if (protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        accepted = connect_auxiliary(
            address, instance, PLATFORM_USB_INPUT_KIND_KEYBOARD);
    } else if (protocol == HID_ITF_PROTOCOL_MOUSE) {
        accepted = connect_auxiliary(
            address, instance, PLATFORM_USB_INPUT_KIND_MOUSE);
    } else {
        accepted = connect_gamepad(
            address, instance, descriptor, descriptor_bytes);
    }

    if (!accepted) {
        increment_stat(&s_service.stats.interfaces_rejected);
        ESP_LOGW(TAG,
                 "USB_HID_REJECTED addr=%u instance=%u protocol=%u descriptor_bytes=%u",
                 (unsigned)address, (unsigned)instance,
                 (unsigned)protocol, (unsigned)descriptor_bytes);
        return;
    }
    if (!tuh_hid_receive_report(address, instance)) {
        increment_stat(&s_service.stats.callback_faults);
        disconnect_slot(address, instance);
        ESP_LOGE(TAG,
                 "USB_HID_RECEIVE_FAIL addr=%u instance=%u stage=mount",
                 (unsigned)address, (unsigned)instance);
    }
}

void tuh_hid_umount_cb(uint8_t address, uint8_t instance)
{
    disconnect_slot(address, instance);
}

void tuh_hid_report_received_cb(uint8_t address, uint8_t instance,
                                const uint8_t *report, uint16_t report_bytes)
{
    if (report == NULL || report_bytes == 0U ||
        report_bytes > GAMEPAD_HID_MAX_REPORT_BYTES ||
        address > TINYUSB_ADDRESS_MAX || instance >= CFG_TUH_HID) {
        increment_stat(&s_service.stats.malformed_reports);
        return;
    }

    tinyusb_hid_slot_t slot = {0};
    gamepad_hid_layout_t layout;
    taskENTER_CRITICAL(&s_service_lock);
    slot = s_service.slots[address][instance];
    if (slot.active && slot.kind == PLATFORM_USB_INPUT_KIND_GAMEPAD) {
        layout = s_service.gamepad_layout;
    }
    taskEXIT_CRITICAL(&s_service_lock);

    bool committed = false;
    if (slot.active && slot.kind == PLATFORM_USB_INPUT_KIND_GAMEPAD) {
        platform_gamepad_snapshot_t current;
        taskENTER_CRITICAL(&s_snapshot_lock);
        gamepad_status_t status = platform_gamepad_model_copy(
            &s_gamepad_model, &current);
        if (status == GAMEPAD_OK && current.session == slot.session &&
            current.state.connected != 0U) {
            gamepad_state_t decoded = current.state;
            status = gamepad_hid_decode_report(
                &layout, report, report_bytes, now_us(), &decoded);
            if (status == GAMEPAD_OK) {
                status = platform_gamepad_model_commit_report(
                    &s_gamepad_model, slot.session, &decoded);
            }
        }
        taskEXIT_CRITICAL(&s_snapshot_lock);
        committed = status == GAMEPAD_OK;
        if (committed) {
            increment_stat(&s_service.stats.gamepad_reports);
        }
    } else if (slot.active &&
               (slot.kind == PLATFORM_USB_INPUT_KIND_KEYBOARD ||
                slot.kind == PLATFORM_USB_INPUT_KIND_MOUSE)) {
        taskENTER_CRITICAL(&s_snapshot_lock);
        const platform_usb_input_status_t status =
            platform_usb_input_model_commit_boot_report(
                &s_aux_input_model, slot.kind, slot.session,
                report, report_bytes, now_us());
        taskEXIT_CRITICAL(&s_snapshot_lock);
        committed = status == PLATFORM_USB_INPUT_OK;
        if (committed && slot.kind == PLATFORM_USB_INPUT_KIND_KEYBOARD) {
            increment_stat(&s_service.stats.keyboard_reports);
        } else if (committed) {
            increment_stat(&s_service.stats.mouse_reports);
        }
    }

    if (committed) {
        increment_stat(&s_service.stats.reports_committed);
    } else if (slot.active) {
        increment_stat(&s_service.stats.malformed_reports);
    }

    if (slot.active && service_running() &&
        !tuh_hid_receive_report(address, instance)) {
        increment_stat(&s_service.stats.callback_faults);
        disconnect_slot(address, instance);
        ESP_LOGE(TAG,
                 "USB_HID_RECEIVE_FAIL addr=%u instance=%u stage=report",
                 (unsigned)address, (unsigned)instance);
    }
}

esp_err_t platform_gamepad_usb_start(void)
{
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.running) {
        taskEXIT_CRITICAL(&s_service_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_service, 0, sizeof(s_service));
    taskEXIT_CRITICAL(&s_service_lock);

    taskENTER_CRITICAL(&s_snapshot_lock);
    platform_gamepad_model_init(&s_gamepad_model);
    platform_usb_input_model_init(&s_aux_input_model);
    taskEXIT_CRITICAL(&s_snapshot_lock);

    const esp_err_t result = platform_usb_host_class_acquire(
        PLATFORM_USB_CLASS_HID, &s_service.host_lease);
    if (result != ESP_OK) {
        return result;
    }
    taskENTER_CRITICAL(&s_service_lock);
    s_service.running = true;
    taskEXIT_CRITICAL(&s_service_lock);
    ESP_LOGI(TAG,
             "GAMEPAD_USB_READY stack=tinyusb tier=generic-hid "
             "hub_tt=enabled report_max=%u",
             (unsigned)GAMEPAD_HID_MAX_REPORT_BYTES);
    return ESP_OK;
}

esp_err_t platform_gamepad_usb_stop(TickType_t timeout_ticks)
{
    if (timeout_ticks == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    platform_usb_host_info_t host_info;
    if (platform_usb_host_get_info(&host_info) != ESP_OK ||
        host_info.state != PLATFORM_USB_HOST_QUIESCING) {
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&s_service_lock);
    const bool was_running = s_service.running;
    s_service.running = false;
    taskEXIT_CRITICAL(&s_service_lock);
    if (!was_running) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t address = 1U; address <= TINYUSB_ADDRESS_MAX; ++address) {
        for (uint8_t instance = 0U; instance < CFG_TUH_HID; ++instance) {
            disconnect_slot(address, instance);
        }
    }
    const esp_err_t result =
        platform_usb_host_class_release(&s_service.host_lease);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "GAMEPAD_USB_STOPPED stack=tinyusb input=neutral");
    }
    return result;
}

esp_err_t platform_gamepad_usb_get_snapshot(
    platform_gamepad_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    const gamepad_status_t status =
        platform_gamepad_model_copy(&s_gamepad_model, snapshot);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    return status == GAMEPAD_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t platform_gamepad_usb_get_input_snapshot(
    platform_usb_input_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    const platform_usb_input_status_t status =
        platform_usb_input_model_take(&s_aux_input_model, snapshot);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    return status == PLATFORM_USB_INPUT_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t platform_gamepad_usb_get_stats(platform_gamepad_usb_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_service_lock);
    *stats = s_service.stats;
    taskEXIT_CRITICAL(&s_service_lock);
    return ESP_OK;
}
