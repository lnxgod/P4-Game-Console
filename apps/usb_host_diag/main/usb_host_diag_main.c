// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

enum {
    USB_DIAG_RHPORT = 1,
    USB_DIAG_TASK_CORE = 1,
    USB_DIAG_TASK_STACK_BYTES = 12 * 1024,
    USB_DIAG_TASK_PRIORITY = 8,
    USB_DIAG_REPORT_LOG_BYTES = 16,
    USB_DIAG_ADDRESS_MAX = CFG_TUH_DEVICE_MAX + CFG_TUH_HUB,
    USB_DIAG_HPRT_OFFSET = 0x440,
    USB_DIAG_ROOT_LOG_INTERVAL_US = 1000000,
};

#define USB_DIAG_DWC2_HS_BASE UINT32_C(0x50000000)

typedef struct {
    uint8_t bytes[CFG_TUH_HID_EP_BUFSIZE];
    uint16_t length;
    uint32_t count;
    bool valid;
} usb_diag_report_state_t;

static const char *const TAG = "usb_host_diag";
static usb_phy_handle_t s_phy;
static usb_diag_report_state_t
    s_reports[USB_DIAG_ADDRESS_MAX + 1U][CFG_TUH_HID];

static uint32_t root_port_status(void)
{
    const uintptr_t address =
        (uintptr_t)USB_DIAG_DWC2_HS_BASE + USB_DIAG_HPRT_OFFSET;
    return *(volatile const uint32_t *)address;
}

static void log_root_port(uint32_t hprt, const char *reason)
{
    ESP_LOGI(TAG,
             "USB_DIAG_ROOT_PORT hprt=0x%08lx connected=%lu enabled=%lu "
             "line_state=%lu power_bit=%lu speed_code=%lu reason=%s",
             (unsigned long)hprt,
             (unsigned long)(hprt & UINT32_C(1)),
             (unsigned long)((hprt >> 2U) & UINT32_C(1)),
             (unsigned long)((hprt >> 10U) & UINT32_C(3)),
             (unsigned long)((hprt >> 12U) & UINT32_C(1)),
             (unsigned long)((hprt >> 17U) & UINT32_C(3)), reason);
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
        return "invalid";
    }
}

static void log_bytes(const char *label, const uint8_t *bytes,
                      uint16_t length)
{
    const uint16_t shown = length < USB_DIAG_REPORT_LOG_BYTES
        ? length : USB_DIAG_REPORT_LOG_BYTES;
    printf("USB_DIAG_%s len=%u data=", label, (unsigned)length);
    for (uint16_t index = 0U; index < shown; ++index) {
        printf("%02x", (unsigned)bytes[index]);
    }
    if (shown < length) {
        printf("...");
    }
    printf("\r\n");
}

static void halt_diag(const char *stage, esp_err_t error)
{
    ESP_LOGE(TAG, "USB_DIAG_HALT stage=%s error=%s automatic_retry=0",
             stage, esp_err_to_name(error));
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}

static void usb_host_task(void *argument)
{
    (void)argument;
    const usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_UTMI,
        .otg_mode = USB_OTG_MODE_HOST,
        .otg_speed = USB_PHY_SPEED_UNDEFINED,
    };
    const esp_err_t phy_result = usb_new_phy(&phy_config, &s_phy);
    if (phy_result != ESP_OK) {
        halt_diag("phy", phy_result);
    }

    const tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO,
    };
    if (!tusb_init(USB_DIAG_RHPORT, &host_init)) {
        halt_diag("tinyusb", ESP_FAIL);
    }

    ESP_LOGI(TAG,
             "USB_DIAG_READY controller=p4-hs rhport=%u root_speed=high "
             "hub_max=%u devices_max=%u hid_max=%u core=%d "
             "firmware_vbus_source=0",
             (unsigned)USB_DIAG_RHPORT, (unsigned)CFG_TUH_HUB,
             (unsigned)CFG_TUH_DEVICE_MAX, (unsigned)CFG_TUH_HID,
             xPortGetCoreID());
    uint32_t previous_hprt = root_port_status();
    int64_t next_root_log_us = esp_timer_get_time();
    log_root_port(previous_hprt, "initial");
    for (;;) {
        tuh_task();
        const uint32_t hprt = root_port_status();
        const int64_t now_us = esp_timer_get_time();
        if (hprt != previous_hprt) {
            log_root_port(hprt, "changed");
            previous_hprt = hprt;
            next_root_log_us = now_us + USB_DIAG_ROOT_LOG_INTERVAL_US;
        } else if (now_us >= next_root_log_us) {
            log_root_port(hprt, "heartbeat");
            next_root_log_us = now_us + USB_DIAG_ROOT_LOG_INTERVAL_US;
        }
        /* A continuously reporting HID endpoint can keep the host queue
         * permanently non-empty. Yield one tick so core 1's idle task can
         * service the watchdog even while reports arrive every 10 ms. */
        vTaskDelay(1U);
    }
}

void tuh_mount_cb(uint8_t daddr)
{
    uint16_t vid = 0U;
    uint16_t pid = 0U;
    tuh_bus_info_t route = {0};
    const bool identity_ok = tuh_vid_pid_get(daddr, &vid, &pid);
    const bool route_ok = tuh_bus_info_get(daddr, &route);
    ESP_LOGI(TAG,
             "USB_DIAG_DEVICE_MOUNT addr=%u vid=%04x pid=%04x "
             "speed=%s rhport=%u parent_hub=%u parent_port=%u "
             "identity_ok=%u route_ok=%u",
             (unsigned)daddr, (unsigned)vid, (unsigned)pid,
             route_ok ? speed_name(route.speed) : "unknown",
             route_ok ? (unsigned)route.rhport : 0U,
             route_ok ? (unsigned)route.hub_addr : 0U,
             route_ok ? (unsigned)route.hub_port : 0U,
             identity_ok ? 1U : 0U, route_ok ? 1U : 0U);
}

void tuh_umount_cb(uint8_t daddr)
{
    if (daddr <= USB_DIAG_ADDRESS_MAX) {
        memset(s_reports[daddr], 0, sizeof(s_reports[daddr]));
    }
    ESP_LOGI(TAG, "USB_DIAG_DEVICE_UNMOUNT addr=%u input=neutral",
             (unsigned)daddr);
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t instance,
                      const uint8_t *report_desc, uint16_t desc_len)
{
    uint16_t vid = 0U;
    uint16_t pid = 0U;
    (void)tuh_vid_pid_get(daddr, &vid, &pid);
    ESP_LOGI(TAG,
             "USB_DIAG_HID_MOUNT addr=%u instance=%u vid=%04x pid=%04x "
             "protocol=%u descriptor_bytes=%u",
             (unsigned)daddr, (unsigned)instance,
             (unsigned)vid, (unsigned)pid,
             (unsigned)tuh_hid_interface_protocol(daddr, instance),
             (unsigned)desc_len);
    if (report_desc != NULL && desc_len != 0U) {
        log_bytes("HID_DESCRIPTOR", report_desc, desc_len);
    }
    if (!tuh_hid_receive_report(daddr, instance)) {
        ESP_LOGE(TAG,
                 "USB_DIAG_HID_RECEIVE_FAIL addr=%u instance=%u stage=mount",
                 (unsigned)daddr, (unsigned)instance);
    }
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t instance)
{
    if (daddr <= USB_DIAG_ADDRESS_MAX && instance < CFG_TUH_HID) {
        memset(&s_reports[daddr][instance], 0,
               sizeof(s_reports[daddr][instance]));
    }
    ESP_LOGI(TAG,
             "USB_DIAG_HID_UNMOUNT addr=%u instance=%u input=neutral",
             (unsigned)daddr, (unsigned)instance);
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t instance,
                                const uint8_t *report, uint16_t len)
{
    if (report == NULL || len > CFG_TUH_HID_EP_BUFSIZE ||
        daddr > USB_DIAG_ADDRESS_MAX || instance >= CFG_TUH_HID) {
        ESP_LOGW(TAG,
                 "USB_DIAG_HID_REPORT_REJECT addr=%u instance=%u len=%u",
                 (unsigned)daddr, (unsigned)instance, (unsigned)len);
        return;
    }

    usb_diag_report_state_t *const previous =
        &s_reports[daddr][instance];
    const bool changed = !previous->valid || previous->length != len ||
        memcmp(previous->bytes, report, len) != 0;
    if (previous->count != UINT32_MAX) {
        ++previous->count;
    }
    if (changed) {
        memcpy(previous->bytes, report, len);
        previous->length = len;
        previous->valid = true;
        ESP_LOGI(TAG,
                 "USB_DIAG_HID_REPORT addr=%u instance=%u count=%lu "
                 "changed=1",
                 (unsigned)daddr, (unsigned)instance,
                 (unsigned long)previous->count);
        log_bytes("HID_DATA", report, len);
    }

    if (!tuh_hid_receive_report(daddr, instance)) {
        ESP_LOGE(TAG,
                 "USB_DIAG_HID_RECEIVE_FAIL addr=%u instance=%u stage=report",
                 (unsigned)daddr, (unsigned)instance);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG,
             "USB_DIAG_BOOT board=waveshare-esp32-p4-wifi6-touch-lcd-4.3 "
             "connector=H2 stack=tinyusb-0.21.0-1 display=off audio=off "
             "touch=off sd=off firmware_vbus_source=0");
    const BaseType_t created = xTaskCreatePinnedToCore(
        usb_host_task, "usb_host_diag", USB_DIAG_TASK_STACK_BYTES,
        NULL, USB_DIAG_TASK_PRIORITY, NULL, USB_DIAG_TASK_CORE);
    if (created != pdPASS) {
        halt_diag("task-create", ESP_ERR_NO_MEM);
    }
}
