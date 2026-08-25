// SPDX-License-Identifier: MIT

/*
 * Olimex ESP32-P4-PC input transport. One HID class owner accepts one
 * gamepad, one boot keyboard, and one boot mouse concurrently. Every report
 * crosses a bounded queue and disconnect neutralizes state before returning
 * from the HID callback.
 */

#include "platform_gamepad_usb/platform_gamepad_usb.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gamepad/hid_gamepad.h"
#include "mbedtls/sha256.h"
#include "platform/gamepad.h"
#include "platform_gamepad_usb/input_model.h"
#include "platform_usb_host/platform_usb_host.h"
#include "usb/hid_host.h"

enum {
    INPUT_MAX_INTERFACES = 6,
    INPUT_EVENT_QUEUE_LENGTH = 24,
    INPUT_CLOSE_QUEUE_LENGTH = INPUT_MAX_INTERFACES,
    INPUT_REPORT_SLOT_COUNT = 16,
    INPUT_CALLBACK_REPORT_BYTES = GAMEPAD_HID_MAX_REPORT_BYTES + 2,
    INPUT_MANAGER_STACK_BYTES = 7168,
    INPUT_MANAGER_PRIORITY = 6,
    INPUT_HID_STACK_BYTES = 4096,
    INPUT_HID_PRIORITY = 5,
    INPUT_UNINSTALL_STACK_BYTES = 3072,
    INPUT_UNINSTALL_PRIORITY = 5,
    INPUT_EVENT_MANAGER_STOPPED = 1U << 0,
    INPUT_EVENT_HID_UNINSTALL_DONE = 1U << 1,
};

typedef enum {
    INPUT_SERVICE_STOPPED = 0,
    INPUT_SERVICE_STARTING,
    INPUT_SERVICE_RUNNING,
    INPUT_SERVICE_STOPPING,
    INPUT_SERVICE_FAULT,
} input_service_state_t;

typedef enum {
    INPUT_EVENT_CONNECTED = 0,
    INPUT_EVENT_REPORT,
} input_event_type_t;

typedef enum {
    INPUT_CLOSE_DEVICE_REMOVED = 0,
    INPUT_CLOSE_TRANSFER_ERROR,
    INPUT_CLOSE_QUEUE_OVERFLOW,
    INPUT_CLOSE_DECODE_ERROR,
    INPUT_CLOSE_SHUTDOWN,
} input_close_reason_t;

typedef struct {
    hid_host_device_handle_t handle;
    hid_host_device_handle_t closed_handle;
    platform_usb_input_kind_t kind;
    uint32_t session;
    input_close_reason_t pending_reason;
    bool running;
    bool closing;
    bool close_queued;
    bool pending_finalize;
    bool callback_closed;
    gamepad_hid_layout_t gamepad_layout;
    uint8_t descriptor[GAMEPAD_HID_MAX_DESCRIPTOR_BYTES];
    size_t descriptor_size;
} input_interface_t;

typedef struct {
    input_event_type_t type;
    union {
        hid_host_device_handle_t handle;
        uint8_t report_slot;
    };
} input_event_t;

typedef struct {
    hid_host_device_handle_t handle;
    uint32_t session;
    input_close_reason_t reason;
} input_close_request_t;

typedef struct {
    hid_host_device_handle_t handle;
    uint32_t session;
    size_t length;
    uint8_t data[INPUT_CALLBACK_REPORT_BYTES];
    bool in_use;
} input_report_slot_t;

typedef struct {
    input_service_state_t state;
    bool stopping;
    bool manager_stop_requested;
    bool uninstall_abort_requested;
    esp_err_t uninstall_result;
    platform_usb_class_lease_t host_lease;
    QueueHandle_t event_queue;
    QueueHandle_t close_queue;
    EventGroupHandle_t task_events;
    TaskHandle_t manager_task;
    TaskHandle_t uninstall_task;
    hid_host_device_handle_t retained_fault_handle;
    input_interface_t interfaces[INPUT_MAX_INTERFACES];
    input_report_slot_t report_slots[INPUT_REPORT_SLOT_COUNT];
    platform_gamepad_usb_stats_t stats;
    volatile uint32_t callback_users;
} input_service_t;

static const char *const TAG = "platform_usb_input";
static portMUX_TYPE s_service_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static input_service_t s_service;
static platform_gamepad_model_t s_gamepad_model;
static platform_usb_input_model_t s_input_model;

static uint64_t now_us(void)
{
    const int64_t value = esp_timer_get_time();
    return value < 0 ? 0U : (uint64_t)value;
}

static const char *kind_name(platform_usb_input_kind_t kind)
{
    switch (kind) {
    case PLATFORM_USB_INPUT_KIND_GAMEPAD:
        return "gamepad";
    case PLATFORM_USB_INPUT_KIND_KEYBOARD:
        return "keyboard";
    case PLATFORM_USB_INPUT_KIND_MOUSE:
        return "mouse";
    case PLATFORM_USB_INPUT_KIND_NONE:
    default:
        return "unknown";
    }
}

static const char *close_reason_name(input_close_reason_t reason)
{
    switch (reason) {
    case INPUT_CLOSE_DEVICE_REMOVED:
        return "removed";
    case INPUT_CLOSE_TRANSFER_ERROR:
        return "transfer-error";
    case INPUT_CLOSE_QUEUE_OVERFLOW:
        return "queue-overflow";
    case INPUT_CLOSE_DECODE_ERROR:
        return "decode-error";
    case INPUT_CLOSE_SHUTDOWN:
        return "shutdown";
    default:
        return "unknown";
    }
}

static input_interface_t *find_interface_locked(
    hid_host_device_handle_t handle)
{
    for (size_t index = 0U; index < INPUT_MAX_INTERFACES; ++index) {
        if (s_service.interfaces[index].handle == handle) {
            return &s_service.interfaces[index];
        }
    }
    return NULL;
}

static input_interface_t *find_empty_interface_locked(void)
{
    for (size_t index = 0U; index < INPUT_MAX_INTERFACES; ++index) {
        input_interface_t *const interface = &s_service.interfaces[index];
        if (interface->handle == NULL && !interface->pending_finalize &&
            interface->kind == PLATFORM_USB_INPUT_KIND_NONE) {
            return interface;
        }
    }
    return NULL;
}

static bool kind_claimed_locked(platform_usb_input_kind_t kind)
{
    for (size_t index = 0U; index < INPUT_MAX_INTERFACES; ++index) {
        if (s_service.interfaces[index].kind == kind) {
            return true;
        }
    }
    return false;
}

static bool callback_enter(bool allow_stopping)
{
    bool accepted = false;
    taskENTER_CRITICAL(&s_service_lock);
    if ((allow_stopping || !s_service.stopping) &&
        s_service.state != INPUT_SERVICE_STOPPED &&
        s_service.callback_users != UINT32_MAX) {
        ++s_service.callback_users;
        accepted = true;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    return accepted;
}

static void callback_leave(void)
{
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.callback_users > 0U) {
        --s_service.callback_users;
    }
    taskEXIT_CRITICAL(&s_service_lock);
}

static void increment_stat(uint32_t *counter)
{
    taskENTER_CRITICAL(&s_service_lock);
    if (*counter != UINT32_MAX) {
        ++*counter;
    }
    taskEXIT_CRITICAL(&s_service_lock);
}

static void neutralize(platform_usb_input_kind_t kind, uint32_t session)
{
    if (session == 0U) {
        return;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    if (kind == PLATFORM_USB_INPUT_KIND_GAMEPAD) {
        (void)platform_gamepad_model_disconnect(
            &s_gamepad_model, session, now_us());
    } else if (kind == PLATFORM_USB_INPUT_KIND_KEYBOARD ||
               kind == PLATFORM_USB_INPUT_KIND_MOUSE) {
        (void)platform_usb_input_model_disconnect(
            &s_input_model, kind, session, now_us());
    }
    taskEXIT_CRITICAL(&s_snapshot_lock);
}

static void neutralize_all(void)
{
    platform_usb_input_kind_t kinds[INPUT_MAX_INTERFACES];
    uint32_t sessions[INPUT_MAX_INTERFACES];
    taskENTER_CRITICAL(&s_service_lock);
    for (size_t index = 0U; index < INPUT_MAX_INTERFACES; ++index) {
        kinds[index] = s_service.interfaces[index].kind;
        sessions[index] = s_service.interfaces[index].session;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    for (size_t index = 0U; index < INPUT_MAX_INTERFACES; ++index) {
        neutralize(kinds[index], sessions[index]);
    }
}

static void enter_terminal_fault(const char *stage, esp_err_t error,
                                 hid_host_device_handle_t retained_handle)
{
    taskENTER_CRITICAL(&s_service_lock);
    s_service.state = INPUT_SERVICE_FAULT;
    s_service.stopping = true;
    if (retained_handle != NULL) {
        s_service.retained_fault_handle = retained_handle;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    neutralize_all();
    ESP_LOGE(TAG,
             "USB_INPUT_FAULT stage=%s code=%s resources=retained neutral=1",
             stage, esp_err_to_name(error));
}

static bool get_running_interface(hid_host_device_handle_t handle,
                                  platform_usb_input_kind_t *kind,
                                  uint32_t *session)
{
    bool found = false;
    taskENTER_CRITICAL(&s_service_lock);
    const input_interface_t *const interface =
        find_interface_locked(handle);
    if (interface != NULL && interface->running && !interface->closing &&
        interface->session != 0U) {
        if (kind != NULL) {
            *kind = interface->kind;
        }
        if (session != NULL) {
            *session = interface->session;
        }
        found = true;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    return found;
}

static int claim_report_slot(hid_host_device_handle_t handle,
                             uint32_t session)
{
    int selected = -1;
    taskENTER_CRITICAL(&s_service_lock);
    for (size_t index = 0U; index < INPUT_REPORT_SLOT_COUNT; ++index) {
        if (!s_service.report_slots[index].in_use) {
            input_report_slot_t *const slot = &s_service.report_slots[index];
            slot->in_use = true;
            slot->handle = handle;
            slot->session = session;
            selected = (int)index;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_service_lock);
    return selected;
}

static void release_report_slot(uint8_t index)
{
    if (index >= INPUT_REPORT_SLOT_COUNT) {
        return;
    }
    taskENTER_CRITICAL(&s_service_lock);
    memset(&s_service.report_slots[index], 0,
           sizeof(s_service.report_slots[index]));
    taskEXIT_CRITICAL(&s_service_lock);
}

static void request_close(hid_host_device_handle_t handle,
                          uint32_t session,
                          input_close_reason_t reason)
{
    platform_usb_input_kind_t kind = PLATFORM_USB_INPUT_KIND_NONE;
    bool enqueue = false;
    taskENTER_CRITICAL(&s_service_lock);
    input_interface_t *const interface = find_interface_locked(handle);
    if (interface != NULL && interface->session == session &&
        interface->running && !interface->closing &&
        !interface->close_queued) {
        kind = interface->kind;
        interface->running = false;
        interface->close_queued = true;
        interface->pending_reason = reason;
        enqueue = true;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    if (!enqueue) {
        return;
    }
    neutralize(kind, session);
    const input_close_request_t request = {
        .handle = handle,
        .session = session,
        .reason = reason,
    };
    if (s_service.close_queue == NULL ||
        xQueueSend(s_service.close_queue, &request, 0) != pdTRUE) {
        increment_stat(&s_service.stats.callback_faults);
        enter_terminal_fault("close-queue", ESP_ERR_NO_MEM, handle);
    }
}

static void handle_driver_disconnect(hid_host_device_handle_t handle)
{
    if (!callback_enter(true)) {
        return;
    }

    platform_usb_input_kind_t kind = PLATFORM_USB_INPUT_KIND_NONE;
    uint32_t session = 0U;
    taskENTER_CRITICAL(&s_service_lock);
    input_interface_t *const interface = find_interface_locked(handle);
    if (interface != NULL) {
        kind = interface->kind;
        session = interface->session;
        interface->closing = true;
        interface->running = false;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    neutralize(kind, session);

    /* usb_host_hid 1.2.0 requires its second close phase in this callback. */
    const esp_err_t close_result = hid_host_device_close(handle);
    if (close_result != ESP_OK) {
        enter_terminal_fault(
            "disconnect-callback-close", close_result, handle);
        callback_leave();
        return;
    }

    taskENTER_CRITICAL(&s_service_lock);
    input_interface_t *const current = find_interface_locked(handle);
    if (current != NULL) {
        current->closed_handle = handle;
        current->handle = NULL;
        current->callback_closed = true;
        current->pending_finalize = true;
        if (!current->close_queued) {
            current->pending_reason = INPUT_CLOSE_DEVICE_REMOVED;
        }
        current->close_queued = false;
        current->closing = false;
    }
    if (s_service.retained_fault_handle == handle) {
        s_service.retained_fault_handle = NULL;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    callback_leave();
}

static void interface_callback(hid_host_device_handle_t handle,
                               const hid_host_interface_event_t event,
                               void *argument)
{
    (void)argument;
    if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        handle_driver_disconnect(handle);
        return;
    }
    if (!callback_enter(false)) {
        return;
    }

    uint32_t session = 0U;
    if (!get_running_interface(handle, NULL, &session)) {
        callback_leave();
        return;
    }
    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        const int selected = claim_report_slot(handle, session);
        if (selected < 0) {
            increment_stat(&s_service.stats.reports_dropped);
            request_close(handle, session, INPUT_CLOSE_QUEUE_OVERFLOW);
            callback_leave();
            return;
        }
        input_report_slot_t *const report =
            &s_service.report_slots[selected];
        size_t copied = 0U;
        const esp_err_t result = hid_host_device_get_raw_input_report_data(
            handle, report->data, sizeof(report->data), &copied);
        if (result != ESP_OK || copied == 0U ||
            copied > sizeof(report->data)) {
            release_report_slot((uint8_t)selected);
            increment_stat(&s_service.stats.callback_faults);
            request_close(handle, session, INPUT_CLOSE_TRANSFER_ERROR);
            callback_leave();
            return;
        }
        report->length = copied;
        const input_event_t queued = {
            .type = INPUT_EVENT_REPORT,
            .report_slot = (uint8_t)selected,
        };
        if (xQueueSend(s_service.event_queue, &queued, 0) != pdTRUE) {
            release_report_slot((uint8_t)selected);
            increment_stat(&s_service.stats.reports_dropped);
            request_close(handle, session, INPUT_CLOSE_QUEUE_OVERFLOW);
        }
        break;
    }
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        increment_stat(&s_service.stats.callback_faults);
        request_close(handle, session, INPUT_CLOSE_TRANSFER_ERROR);
        break;
    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        break;
#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED
    case HID_HOST_INTERFACE_EVENT_SUSPENDED:
    case HID_HOST_INTERFACE_EVENT_RESUMED:
        break;
#endif
    default:
        increment_stat(&s_service.stats.callback_faults);
        request_close(handle, session, INPUT_CLOSE_TRANSFER_ERROR);
        break;
    }
    callback_leave();
}

static void driver_callback(hid_host_device_handle_t handle,
                            const hid_host_driver_event_t event,
                            void *argument)
{
    (void)argument;
    if (!callback_enter(false)) {
        return;
    }
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        increment_stat(&s_service.stats.interfaces_seen);
        const input_event_t queued = {
            .type = INPUT_EVENT_CONNECTED,
            .handle = handle,
        };
        if (xQueueSend(s_service.event_queue, &queued, 0) != pdTRUE) {
            increment_stat(&s_service.stats.interfaces_rejected);
            enter_terminal_fault("driver-event-queue", ESP_ERR_NO_MEM,
                                 handle);
        }
    }
    callback_leave();
}

static platform_usb_input_kind_t classify_interface(
    const hid_host_dev_params_t *parameters)
{
    if (parameters == NULL) {
        return PLATFORM_USB_INPUT_KIND_NONE;
    }
    if (parameters->sub_class == HID_SUBCLASS_BOOT_INTERFACE &&
        parameters->proto == HID_PROTOCOL_KEYBOARD) {
        return PLATFORM_USB_INPUT_KIND_KEYBOARD;
    }
    if (parameters->sub_class == HID_SUBCLASS_BOOT_INTERFACE &&
        parameters->proto == HID_PROTOCOL_MOUSE) {
        return PLATFORM_USB_INPUT_KIND_MOUSE;
    }
    if (parameters->proto == HID_PROTOCOL_NONE) {
        return PLATFORM_USB_INPUT_KIND_GAMEPAD;
    }
    return PLATFORM_USB_INPUT_KIND_NONE;
}

static bool interface_still_open(input_interface_t *interface,
                                 hid_host_device_handle_t handle)
{
    bool current;
    taskENTER_CRITICAL(&s_service_lock);
    current = interface != NULL && interface->handle == handle &&
        !interface->pending_finalize;
    taskEXIT_CRITICAL(&s_service_lock);
    return current;
}

static bool manager_close_idle_interface(hid_host_device_handle_t handle,
                                         const char *stage)
{
    const esp_err_t result = hid_host_device_close(handle);
    if (result == ESP_OK || result == ESP_ERR_INVALID_ARG) {
        return true;
    }
    enter_terminal_fault(stage, result, handle);
    return false;
}

static bool manager_finish_close(input_interface_t *interface,
                                 hid_host_device_handle_t handle,
                                 uint32_t session,
                                 input_close_reason_t reason,
                                 const char *stage)
{
    taskENTER_CRITICAL(&s_service_lock);
    if (interface == NULL || interface->handle != handle ||
        interface->session != session || interface->closing) {
        taskEXIT_CRITICAL(&s_service_lock);
        return false;
    }
    interface->closing = true;
    interface->close_queued = false;
    interface->running = false;
    interface->pending_reason = reason;
    taskEXIT_CRITICAL(&s_service_lock);
    neutralize(interface->kind, session);

    const esp_err_t stop_result = hid_host_device_stop(handle);
    if (stop_result != ESP_OK && stop_result != ESP_ERR_NOT_FOUND &&
        stop_result != ESP_ERR_INVALID_STATE &&
        stop_result != ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "USB_INPUT_STOP_WARN kind=%s code=%s",
                 kind_name(interface->kind), esp_err_to_name(stop_result));
    }
    const esp_err_t result = hid_host_device_close(handle);
    bool callback_closed;
    taskENTER_CRITICAL(&s_service_lock);
    callback_closed = interface->pending_finalize &&
        interface->callback_closed && interface->closed_handle == handle;
    if (callback_closed) {
        interface->pending_reason = reason;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    if (result != ESP_OK &&
        !(result == ESP_ERR_INVALID_ARG && callback_closed)) {
        enter_terminal_fault(stage, result, handle);
        return false;
    }
    return callback_closed;
}

static void manager_process_pending_finalize(void)
{
    for (;;) {
        platform_usb_input_kind_t kind = PLATFORM_USB_INPUT_KIND_NONE;
        uint32_t session = 0U;
        input_close_reason_t reason = INPUT_CLOSE_DEVICE_REMOVED;
        bool found = false;
        taskENTER_CRITICAL(&s_service_lock);
        for (size_t index = 0U; index < INPUT_MAX_INTERFACES; ++index) {
            input_interface_t *const interface =
                &s_service.interfaces[index];
            if (interface->pending_finalize) {
                kind = interface->kind;
                session = interface->session;
                reason = interface->pending_reason;
                memset(interface, 0, sizeof(*interface));
                found = true;
                break;
            }
        }
        taskEXIT_CRITICAL(&s_service_lock);
        if (!found) {
            return;
        }
        increment_stat(&s_service.stats.disconnections);
        ESP_LOGI(TAG,
                 "USB_INPUT_DISCONNECTED kind=%s session=%" PRIu32
                 " reason=%s neutral=1",
                 kind_name(kind), session, close_reason_name(reason));
    }
}

static bool descriptor_hash(const uint8_t *descriptor, size_t bytes,
                            uint8_t hash[32])
{
    return descriptor != NULL && bytes > 0U &&
        mbedtls_sha256(descriptor, bytes, hash, 0) == 0;
}

static void hash_hex(const uint8_t hash[32], char output[65])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0U; index < 32U; ++index) {
        output[index * 2U] = digits[hash[index] >> 4U];
        output[index * 2U + 1U] = digits[hash[index] & 0x0fU];
    }
    output[64] = '\0';
}

static void reject_open_interface(input_interface_t *interface,
                                  hid_host_device_handle_t handle,
                                  const char *reason)
{
    increment_stat(&s_service.stats.interfaces_rejected);
    if (interface_still_open(interface, handle)) {
        (void)manager_finish_close(interface, handle, interface->session,
                                   INPUT_CLOSE_DECODE_ERROR,
                                   "rejected-interface-close");
    }
    ESP_LOGW(TAG, "USB_INPUT_INTERFACE_REJECT reason=%s", reason);
}

static void manager_handle_connected(hid_host_device_handle_t handle)
{
    hid_host_dev_params_t parameters;
    if (hid_host_device_get_params(handle, &parameters) != ESP_OK) {
        increment_stat(&s_service.stats.interfaces_rejected);
        (void)manager_close_idle_interface(handle, "params-interface-close");
        return;
    }
    const platform_usb_input_kind_t kind = classify_interface(&parameters);
    if (kind == PLATFORM_USB_INPUT_KIND_NONE) {
        increment_stat(&s_service.stats.interfaces_rejected);
        (void)manager_close_idle_interface(handle, "protocol-interface-close");
        return;
    }

    taskENTER_CRITICAL(&s_service_lock);
    const bool accepting =
        (s_service.state == INPUT_SERVICE_STARTING ||
         s_service.state == INPUT_SERVICE_RUNNING) &&
        !s_service.stopping && !kind_claimed_locked(kind);
    input_interface_t *const interface = accepting
        ? find_empty_interface_locked() : NULL;
    if (interface != NULL) {
        memset(interface, 0, sizeof(*interface));
        interface->handle = handle;
        interface->kind = kind;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    if (interface == NULL) {
        increment_stat(&s_service.stats.interfaces_rejected);
        (void)manager_close_idle_interface(handle,
                                           "busy-interface-close");
        return;
    }

    const hid_host_device_config_t config = {
        .callback = interface_callback,
        .callback_arg = &s_service,
    };
    const esp_err_t open_result = hid_host_device_open(handle, &config);
    if (open_result != ESP_OK) {
        enter_terminal_fault("interface-open", open_result, handle);
        return;
    }
    if (!interface_still_open(interface, handle)) {
        manager_process_pending_finalize();
        return;
    }

    size_t descriptor_size = 0U;
    const uint8_t *const descriptor =
        hid_host_get_report_descriptor(handle, &descriptor_size);
    if (descriptor == NULL || descriptor_size == 0U ||
        descriptor_size > sizeof(interface->descriptor)) {
        reject_open_interface(interface, handle, "descriptor-size");
        return;
    }
    memcpy(interface->descriptor, descriptor, descriptor_size);
    interface->descriptor_size = descriptor_size;

    hid_host_dev_info_t device_info;
    if (hid_host_get_device_info(handle, &device_info) != ESP_OK) {
        reject_open_interface(interface, handle, "device-info");
        return;
    }
    uint8_t hash[32];
    if (!descriptor_hash(descriptor, descriptor_size, hash)) {
        reject_open_interface(interface, handle, "descriptor-hash");
        return;
    }

    uint32_t session = 0U;
    uint32_t capabilities = 0U;
    gamepad_hid_profile_t profile = GAMEPAD_HID_PROFILE_NONE;
    if (kind == PLATFORM_USB_INPUT_KIND_GAMEPAD) {
        const gamepad_status_t parse_status = gamepad_hid_parse_descriptor(
            interface->descriptor, interface->descriptor_size,
            &interface->gamepad_layout);
        if (parse_status != GAMEPAD_OK) {
            reject_open_interface(interface, handle,
                                  gamepad_status_name(parse_status));
            return;
        }
        platform_gamepad_identity_t identity = {
            .vendor_id = device_info.VID,
            .product_id = device_info.PID,
            .interface_number = parameters.iface_num,
            .transport = PLATFORM_GAMEPAD_TRANSPORT_USB_HID,
        };
        memcpy(identity.descriptor_sha256, hash,
               sizeof(identity.descriptor_sha256));
        const gamepad_status_t profile_status =
            gamepad_hid_apply_known_profile(
                identity.vendor_id, identity.product_id,
                identity.descriptor_sha256,
                &interface->gamepad_layout, &profile);
        if (profile_status != GAMEPAD_OK) {
            reject_open_interface(interface, handle,
                                  gamepad_status_name(profile_status));
            return;
        }
        capabilities = gamepad_hid_capabilities(
            &interface->gamepad_layout);
        taskENTER_CRITICAL(&s_snapshot_lock);
        const gamepad_status_t connect_status =
            platform_gamepad_model_connect(
                &s_gamepad_model, &identity, capabilities, now_us(),
                &session);
        taskEXIT_CRITICAL(&s_snapshot_lock);
        if (connect_status != GAMEPAD_OK) {
            reject_open_interface(interface, handle,
                                  gamepad_status_name(connect_status));
            return;
        }
    } else {
        const esp_err_t protocol_result = hid_class_request_set_protocol(
            handle, HID_REPORT_PROTOCOL_BOOT);
        if (protocol_result != ESP_OK) {
            reject_open_interface(interface, handle, "boot-protocol");
            return;
        }
        taskENTER_CRITICAL(&s_snapshot_lock);
        const platform_usb_input_status_t connect_status =
            platform_usb_input_model_connect(
                &s_input_model, kind, now_us(), &session);
        taskEXIT_CRITICAL(&s_snapshot_lock);
        if (connect_status != PLATFORM_USB_INPUT_OK) {
            reject_open_interface(interface, handle, "snapshot-connect");
            return;
        }
    }

    taskENTER_CRITICAL(&s_service_lock);
    if (interface->handle == handle && !interface->pending_finalize) {
        interface->session = session;
        interface->running = true;
    }
    const bool start_allowed = interface->handle == handle &&
        interface->running;
    taskEXIT_CRITICAL(&s_service_lock);
    if (!start_allowed) {
        neutralize(kind, session);
        manager_process_pending_finalize();
        return;
    }

    const esp_err_t start_result = hid_host_device_start(handle);
    if (start_result != ESP_OK) {
        (void)manager_finish_close(interface, handle, session,
                                   INPUT_CLOSE_TRANSFER_ERROR,
                                   "interface-start-close");
        return;
    }

    increment_stat(&s_service.stats.connections);
    if (kind == PLATFORM_USB_INPUT_KIND_KEYBOARD) {
        increment_stat(&s_service.stats.keyboard_connections);
    } else if (kind == PLATFORM_USB_INPUT_KIND_MOUSE) {
        increment_stat(&s_service.stats.mouse_connections);
    } else {
        increment_stat(&s_service.stats.gamepad_connections);
    }
    char descriptor_sha256[65];
    hash_hex(hash, descriptor_sha256);
    ESP_LOGI(TAG,
             "USB_INPUT_CONNECTED kind=%s session=%" PRIu32
             " vid=%04x pid=%04x interface=%u protocol=%u "
             "descriptor_bytes=%u descriptor_sha256=%s "
             "profile=%s capabilities=0x%08" PRIx32,
             kind_name(kind), session, device_info.VID, device_info.PID,
             (unsigned)parameters.iface_num, (unsigned)parameters.proto,
             (unsigned)descriptor_size, descriptor_sha256,
             kind == PLATFORM_USB_INPUT_KIND_GAMEPAD
                ? gamepad_hid_profile_name(profile) : "boot",
             capabilities);
}

static void manager_handle_report(uint8_t report_index)
{
    if (report_index >= INPUT_REPORT_SLOT_COUNT) {
        increment_stat(&s_service.stats.callback_faults);
        return;
    }
    input_report_slot_t *const report =
        &s_service.report_slots[report_index];
    platform_usb_input_kind_t kind = PLATFORM_USB_INPUT_KIND_NONE;
    uint32_t session = 0U;
    if (!report->in_use ||
        !get_running_interface(report->handle, &kind, &session) ||
        session != report->session) {
        release_report_slot(report_index);
        return;
    }

    bool decoded = false;
    if (kind == PLATFORM_USB_INPUT_KIND_GAMEPAD) {
        input_interface_t *interface;
        taskENTER_CRITICAL(&s_service_lock);
        interface = find_interface_locked(report->handle);
        taskEXIT_CRITICAL(&s_service_lock);
        platform_gamepad_snapshot_t current;
        taskENTER_CRITICAL(&s_snapshot_lock);
        gamepad_status_t status = platform_gamepad_model_copy(
            &s_gamepad_model, &current);
        taskEXIT_CRITICAL(&s_snapshot_lock);
        if (status == GAMEPAD_OK && interface != NULL &&
            current.session == session && current.state.connected != 0U) {
            gamepad_state_t state = current.state;
            status = gamepad_hid_decode_report(
                &interface->gamepad_layout, report->data, report->length,
                now_us(), &state);
            if (status == GAMEPAD_OK) {
                taskENTER_CRITICAL(&s_snapshot_lock);
                status = platform_gamepad_model_commit_report(
                    &s_gamepad_model, session, &state);
                taskEXIT_CRITICAL(&s_snapshot_lock);
            }
        }
        decoded = status == GAMEPAD_OK;
    } else {
        taskENTER_CRITICAL(&s_snapshot_lock);
        const platform_usb_input_status_t status =
            platform_usb_input_model_commit_boot_report(
                &s_input_model, kind, session, report->data,
                report->length, now_us());
        taskEXIT_CRITICAL(&s_snapshot_lock);
        decoded = status == PLATFORM_USB_INPUT_OK;
    }

    const hid_host_device_handle_t handle = report->handle;
    release_report_slot(report_index);
    if (decoded) {
        increment_stat(&s_service.stats.reports_committed);
        if (kind == PLATFORM_USB_INPUT_KIND_KEYBOARD) {
            increment_stat(&s_service.stats.keyboard_reports);
        } else if (kind == PLATFORM_USB_INPUT_KIND_MOUSE) {
            increment_stat(&s_service.stats.mouse_reports);
        } else {
            increment_stat(&s_service.stats.gamepad_reports);
        }
    } else {
        increment_stat(&s_service.stats.malformed_reports);
        request_close(handle, session, INPUT_CLOSE_DECODE_ERROR);
    }
}

static bool manager_should_stop(void)
{
    bool stopping;
    taskENTER_CRITICAL(&s_service_lock);
    stopping = s_service.manager_stop_requested ||
        s_service.state == INPUT_SERVICE_FAULT;
    taskEXIT_CRITICAL(&s_service_lock);
    return stopping;
}

static void manager_close_request(const input_close_request_t *request)
{
    if (request == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_service_lock);
    input_interface_t *const interface =
        find_interface_locked(request->handle);
    taskEXIT_CRITICAL(&s_service_lock);
    if (interface != NULL) {
        (void)manager_finish_close(
            interface, request->handle, request->session,
            request->reason, "interface-close");
    }
}

static bool manager_close_one_for_shutdown(void)
{
    input_close_request_t request = {0};
    bool found = false;
    taskENTER_CRITICAL(&s_service_lock);
    for (size_t index = 0U; index < INPUT_MAX_INTERFACES; ++index) {
        const input_interface_t *const interface =
            &s_service.interfaces[index];
        if (interface->handle != NULL && interface->session != 0U &&
            !interface->closing) {
            request.handle = interface->handle;
            request.session = interface->session;
            request.reason = INPUT_CLOSE_SHUTDOWN;
            found = true;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_service_lock);
    if (found) {
        manager_close_request(&request);
    }
    return found;
}

static void manager_task(void *argument)
{
    (void)argument;
    while (!manager_should_stop()) {
        manager_process_pending_finalize();
        input_close_request_t close_request;
        if (xQueueReceive(s_service.close_queue, &close_request, 0) == pdTRUE) {
            manager_close_request(&close_request);
            continue;
        }
        input_event_t event;
        if (xQueueReceive(s_service.event_queue, &event,
                          pdMS_TO_TICKS(20U)) != pdTRUE) {
            continue;
        }
        if (event.type == INPUT_EVENT_CONNECTED) {
            manager_handle_connected(event.handle);
        } else if (event.type == INPUT_EVENT_REPORT) {
            manager_handle_report(event.report_slot);
        }
    }

    taskENTER_CRITICAL(&s_service_lock);
    const bool faulted = s_service.state == INPUT_SERVICE_FAULT;
    taskEXIT_CRITICAL(&s_service_lock);
    if (!faulted) {
        manager_process_pending_finalize();
        while (manager_close_one_for_shutdown()) {
            manager_process_pending_finalize();
        }
        for (uint8_t index = 0U;
             index < INPUT_REPORT_SLOT_COUNT; ++index) {
            release_report_slot(index);
        }
    }
    xEventGroupSetBits(s_service.task_events,
                       INPUT_EVENT_MANAGER_STOPPED);
    vTaskDelete(NULL);
}

static bool wait_for_callbacks_to_drain(TickType_t timeout_ticks)
{
    const TickType_t start = xTaskGetTickCount();
    for (;;) {
        uint32_t active;
        taskENTER_CRITICAL(&s_service_lock);
        active = s_service.callback_users;
        taskEXIT_CRITICAL(&s_service_lock);
        if (active == 0U) {
            return true;
        }
        if (xTaskGetTickCount() - start >= timeout_ticks) {
            return false;
        }
        vTaskDelay(1U);
    }
}

static esp_err_t stop_manager(TickType_t timeout_ticks)
{
    taskENTER_CRITICAL(&s_service_lock);
    s_service.stopping = true;
    taskEXIT_CRITICAL(&s_service_lock);
    if (!wait_for_callbacks_to_drain(timeout_ticks)) {
        return ESP_ERR_TIMEOUT;
    }
    taskENTER_CRITICAL(&s_service_lock);
    s_service.manager_stop_requested = true;
    taskEXIT_CRITICAL(&s_service_lock);
    const EventBits_t bits = xEventGroupWaitBits(
        s_service.task_events, INPUT_EVENT_MANAGER_STOPPED,
        pdFALSE, pdTRUE, timeout_ticks);
    return (bits & INPUT_EVENT_MANAGER_STOPPED) != 0U
        ? ESP_OK : ESP_ERR_TIMEOUT;
}

static bool uninstall_abort_requested(void)
{
    bool requested;
    taskENTER_CRITICAL(&s_service_lock);
    requested = s_service.uninstall_abort_requested;
    taskEXIT_CRITICAL(&s_service_lock);
    return requested;
}

static void hid_uninstall_task(void *argument)
{
    (void)argument;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    while (!uninstall_abort_requested()) {
        result = hid_host_uninstall();
        if (result != ESP_ERR_INVALID_STATE) {
            break;
        }
        vTaskDelay(1U);
    }
    taskENTER_CRITICAL(&s_service_lock);
    s_service.uninstall_result = result;
    taskEXIT_CRITICAL(&s_service_lock);
    xEventGroupSetBits(s_service.task_events,
                       INPUT_EVENT_HID_UNINSTALL_DONE);
    vTaskSuspend(NULL);
}

static esp_err_t uninstall_hid_bounded(TickType_t timeout_ticks)
{
    taskENTER_CRITICAL(&s_service_lock);
    s_service.uninstall_abort_requested = false;
    s_service.uninstall_result = ESP_ERR_INVALID_STATE;
    taskEXIT_CRITICAL(&s_service_lock);
    xEventGroupClearBits(s_service.task_events,
                         INPUT_EVENT_HID_UNINSTALL_DONE);
    if (xTaskCreate(hid_uninstall_task, "p4_hid_uninstall",
                    INPUT_UNINSTALL_STACK_BYTES, NULL,
                    INPUT_UNINSTALL_PRIORITY,
                    &s_service.uninstall_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    const EventBits_t bits = xEventGroupWaitBits(
        s_service.task_events, INPUT_EVENT_HID_UNINSTALL_DONE,
        pdFALSE, pdTRUE, timeout_ticks);
    if ((bits & INPUT_EVENT_HID_UNINSTALL_DONE) == 0U) {
        taskENTER_CRITICAL(&s_service_lock);
        s_service.uninstall_abort_requested = true;
        taskEXIT_CRITICAL(&s_service_lock);
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result;
    TaskHandle_t task;
    taskENTER_CRITICAL(&s_service_lock);
    result = s_service.uninstall_result;
    task = s_service.uninstall_task;
    s_service.uninstall_task = NULL;
    taskEXIT_CRITICAL(&s_service_lock);
    if (task != NULL) {
        vTaskDelete(task);
    }
    return result;
}

static void delete_service_resources(void)
{
    if (s_service.event_queue != NULL) {
        vQueueDelete(s_service.event_queue);
    }
    if (s_service.close_queue != NULL) {
        vQueueDelete(s_service.close_queue);
    }
    if (s_service.task_events != NULL) {
        vEventGroupDelete(s_service.task_events);
    }
    s_service.event_queue = NULL;
    s_service.close_queue = NULL;
    s_service.task_events = NULL;
    s_service.manager_task = NULL;
}

esp_err_t platform_gamepad_usb_start(void)
{
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.state != INPUT_SERVICE_STOPPED) {
        taskEXIT_CRITICAL(&s_service_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_service, 0, sizeof(s_service));
    s_service.state = INPUT_SERVICE_STARTING;
    taskEXIT_CRITICAL(&s_service_lock);
    taskENTER_CRITICAL(&s_snapshot_lock);
    platform_gamepad_model_init(&s_gamepad_model);
    platform_usb_input_model_init(&s_input_model);
    taskEXIT_CRITICAL(&s_snapshot_lock);

    s_service.event_queue = xQueueCreate(
        INPUT_EVENT_QUEUE_LENGTH, sizeof(input_event_t));
    s_service.close_queue = xQueueCreate(
        INPUT_CLOSE_QUEUE_LENGTH, sizeof(input_close_request_t));
    s_service.task_events = xEventGroupCreate();
    if (s_service.event_queue == NULL || s_service.close_queue == NULL ||
        s_service.task_events == NULL) {
        delete_service_resources();
        taskENTER_CRITICAL(&s_service_lock);
        s_service.state = INPUT_SERVICE_STOPPED;
        taskEXIT_CRITICAL(&s_service_lock);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = platform_usb_host_class_acquire(
        PLATFORM_USB_CLASS_HID, &s_service.host_lease);
    if (result != ESP_OK) {
        delete_service_resources();
        taskENTER_CRITICAL(&s_service_lock);
        s_service.state = INPUT_SERVICE_STOPPED;
        taskEXIT_CRITICAL(&s_service_lock);
        return result;
    }
    if (xTaskCreate(manager_task, "p4_usb_input",
                    INPUT_MANAGER_STACK_BYTES, NULL,
                    INPUT_MANAGER_PRIORITY,
                    &s_service.manager_task) != pdPASS) {
        (void)platform_usb_host_class_release(&s_service.host_lease);
        delete_service_resources();
        taskENTER_CRITICAL(&s_service_lock);
        s_service.state = INPUT_SERVICE_STOPPED;
        taskEXIT_CRITICAL(&s_service_lock);
        return ESP_ERR_NO_MEM;
    }

    const hid_host_driver_config_t hid_config = {
        .create_background_task = true,
        .task_priority = INPUT_HID_PRIORITY,
        .stack_size = INPUT_HID_STACK_BYTES,
        .core_id = tskNO_AFFINITY,
        .callback = driver_callback,
        .callback_arg = &s_service,
    };
    result = hid_host_install(&hid_config);
    if (result != ESP_OK) {
        const esp_err_t manager_result = stop_manager(
            pdMS_TO_TICKS(1000U));
        if (manager_result != ESP_OK) {
            enter_terminal_fault("hid-install-manager-stop",
                                 manager_result, NULL);
            return manager_result;
        }
        (void)platform_usb_host_class_release(&s_service.host_lease);
        delete_service_resources();
        taskENTER_CRITICAL(&s_service_lock);
        s_service.state = INPUT_SERVICE_STOPPED;
        taskEXIT_CRITICAL(&s_service_lock);
        return result;
    }

    taskENTER_CRITICAL(&s_service_lock);
    s_service.state = INPUT_SERVICE_RUNNING;
    taskEXIT_CRITICAL(&s_service_lock);
    ESP_LOGI(TAG,
             "USB_INPUT_READY classes=gamepad,boot-keyboard,boot-mouse "
             "interfaces_max=%u report_max=%u",
             (unsigned)INPUT_MAX_INTERFACES,
             (unsigned)INPUT_CALLBACK_REPORT_BYTES);
    return platform_gamepad_register_provider(
        PLATFORM_GAMEPAD_TRANSPORT_USB_HID,
        platform_gamepad_usb_get_snapshot);
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
    if (s_service.state != INPUT_SERVICE_RUNNING) {
        taskEXIT_CRITICAL(&s_service_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_service.state = INPUT_SERVICE_STOPPING;
    taskEXIT_CRITICAL(&s_service_lock);

    esp_err_t result = stop_manager(timeout_ticks);
    if (result != ESP_OK) {
        enter_terminal_fault("manager-stop", result, NULL);
        return result;
    }
    result = uninstall_hid_bounded(timeout_ticks);
    if (result != ESP_OK) {
        enter_terminal_fault("hid-uninstall", result, NULL);
        return result;
    }
    result = platform_usb_host_class_release(&s_service.host_lease);
    if (result != ESP_OK) {
        enter_terminal_fault("lease-release", result, NULL);
        return result;
    }
    manager_process_pending_finalize();
    delete_service_resources();
    taskENTER_CRITICAL(&s_service_lock);
    s_service.state = INPUT_SERVICE_STOPPED;
    taskEXIT_CRITICAL(&s_service_lock);
    platform_gamepad_unregister_provider(
        PLATFORM_GAMEPAD_TRANSPORT_USB_HID,
        platform_gamepad_usb_get_snapshot);
    return ESP_OK;
}

esp_err_t platform_gamepad_usb_get_snapshot(
    platform_gamepad_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    const gamepad_status_t result = platform_gamepad_model_copy(
        &s_gamepad_model, snapshot);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    return result == GAMEPAD_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t platform_gamepad_usb_get_input_snapshot(
    platform_usb_input_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    const platform_usb_input_status_t result =
        platform_usb_input_model_take(&s_input_model, snapshot);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    return result == PLATFORM_USB_INPUT_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
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
