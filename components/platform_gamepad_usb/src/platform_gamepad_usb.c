#include "platform_gamepad_usb/platform_gamepad_usb.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gamepad/hid_gamepad.h"
#include "mbedtls/sha256.h"
#include "platform_usb_host/platform_usb_host.h"
#include "usb/hid_host.h"

#define GAMEPAD_EVENT_QUEUE_LENGTH 16U
#define GAMEPAD_REPORT_SLOT_COUNT 8U
#define GAMEPAD_CALLBACK_REPORT_BYTES (GAMEPAD_HID_MAX_REPORT_BYTES + 2U)
#define GAMEPAD_MANAGER_STACK_BYTES 6144U
#define GAMEPAD_MANAGER_PRIORITY 6U
#define GAMEPAD_HID_STACK_BYTES 4096U
#define GAMEPAD_HID_PRIORITY 5U
#define GAMEPAD_UNINSTALL_STACK_BYTES 3072U
#define GAMEPAD_UNINSTALL_PRIORITY 5U
#define GAMEPAD_EVENT_MANAGER_STOPPED (1U << 0)
#define GAMEPAD_EVENT_HID_UNINSTALL_DONE (1U << 1)

typedef enum {
    GAMEPAD_SERVICE_STOPPED = 0,
    GAMEPAD_SERVICE_STARTING,
    GAMEPAD_SERVICE_RUNNING,
    GAMEPAD_SERVICE_STOPPING,
    GAMEPAD_SERVICE_FAULT,
} gamepad_service_state_t;

typedef enum {
    GAMEPAD_EVENT_CONNECTED = 0,
    GAMEPAD_EVENT_REPORT,
} gamepad_event_type_t;

typedef enum {
    GAMEPAD_CLOSE_DEVICE_REMOVED = 0,
    GAMEPAD_CLOSE_TRANSFER_ERROR,
    GAMEPAD_CLOSE_QUEUE_OVERFLOW,
    GAMEPAD_CLOSE_DECODE_ERROR,
    GAMEPAD_CLOSE_SHUTDOWN,
} gamepad_close_reason_t;

typedef struct {
    gamepad_event_type_t type;
    union {
        hid_host_device_handle_t handle;
        uint8_t report_slot;
    };
} gamepad_event_t;

typedef struct {
    hid_host_device_handle_t handle;
    uint32_t session;
    gamepad_close_reason_t reason;
} gamepad_close_request_t;

typedef struct {
    hid_host_device_handle_t handle;
    uint32_t session;
    size_t length;
    uint8_t data[GAMEPAD_CALLBACK_REPORT_BYTES];
    bool in_use;
} gamepad_report_slot_t;

typedef struct {
    gamepad_service_state_t state;
    bool stopping;
    bool manager_stop_requested;
    platform_usb_class_lease_t host_lease;
    QueueHandle_t event_queue;
    QueueHandle_t close_queue;
    EventGroupHandle_t task_events;
    TaskHandle_t manager_task;
    TaskHandle_t uninstall_task;
    hid_host_device_handle_t active_handle;
    hid_host_device_handle_t retained_fault_handle;
    hid_host_device_handle_t callback_closed_handle;
    uint32_t active_session;
    uint32_t callback_closed_session;
    uint32_t pending_finalize_session;
    gamepad_close_reason_t pending_finalize_reason;
    bool active_closing;
    bool pending_finalize;
    bool uninstall_abort_requested;
    esp_err_t uninstall_result;
    gamepad_hid_layout_t active_layout;
    uint8_t active_descriptor[GAMEPAD_HID_MAX_DESCRIPTOR_BYTES];
    size_t active_descriptor_size;
    gamepad_report_slot_t report_slots[GAMEPAD_REPORT_SLOT_COUNT];
    platform_gamepad_usb_stats_t stats;
    volatile uint32_t callback_users;
} gamepad_service_t;

static const char *const TAG = "platform_gamepad";
static portMUX_TYPE s_service_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static gamepad_service_t s_service;
static platform_gamepad_model_t s_model;

static bool wait_for_callbacks_to_drain(TickType_t timeout_ticks);
static void complete_active_close(hid_host_device_handle_t handle,
                                  uint32_t session);
static void enter_terminal_fault(const char *stage, esp_err_t error);
static void retain_unclosed_handle(hid_host_device_handle_t handle);

static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static bool callback_enter(void)
{
    bool accepted = false;
    taskENTER_CRITICAL(&s_service_lock);
    if (!s_service.stopping &&
        (s_service.state == GAMEPAD_SERVICE_STARTING ||
         s_service.state == GAMEPAD_SERVICE_RUNNING ||
         s_service.state == GAMEPAD_SERVICE_STOPPING) &&
        s_service.callback_users != UINT32_MAX) {
        s_service.callback_users++;
        accepted = true;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    return accepted;
}

static bool disconnect_callback_enter(void)
{
    bool accepted = false;
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.state != GAMEPAD_SERVICE_STOPPED &&
        s_service.callback_users != UINT32_MAX) {
        s_service.callback_users++;
        accepted = true;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    return accepted;
}

static void callback_leave(void)
{
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.callback_users > 0U) {
        s_service.callback_users--;
    }
    taskEXIT_CRITICAL(&s_service_lock);
}

static void increment_stat(uint32_t *counter)
{
    taskENTER_CRITICAL(&s_service_lock);
    (*counter)++;
    taskEXIT_CRITICAL(&s_service_lock);
}

static bool get_active_session(hid_host_device_handle_t handle,
                               uint32_t *session)
{
    bool matches;
    taskENTER_CRITICAL(&s_service_lock);
    matches = s_service.active_handle == handle &&
              s_service.active_session != 0U && !s_service.active_closing;
    if (matches && session != NULL) {
        *session = s_service.active_session;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    return matches;
}

static void neutralize_session(uint32_t session)
{
    if (session == 0U) {
        return;
    }
    taskENTER_CRITICAL(&s_snapshot_lock);
    (void)platform_gamepad_model_disconnect(&s_model, session,
                                            now_us());
    taskEXIT_CRITICAL(&s_snapshot_lock);
}

static void request_close(hid_host_device_handle_t handle,
                          uint32_t session,
                          gamepad_close_reason_t reason)
{
    neutralize_session(session);
    const gamepad_close_request_t request = {
        .handle = handle,
        .session = session,
        .reason = reason,
    };
    if (s_service.close_queue != NULL) {
        (void)xQueueOverwrite(s_service.close_queue, &request);
    }
}

static void handle_driver_disconnect(hid_host_device_handle_t handle)
{
    if (!disconnect_callback_enter()) {
        return;
    }

    uint32_t session = 0;
    bool finalize_from_callback = false;
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.active_handle == handle &&
        s_service.active_session != 0U) {
        session = s_service.active_session;
        if (!s_service.active_closing) {
            s_service.active_closing = true;
            finalize_from_callback = true;
        }
    }
    taskEXIT_CRITICAL(&s_service_lock);

    neutralize_session(session);

    /*
     * usb_host_hid 1.2.0 invokes this callback after its first close phase and
     * requires the user callback to perform the second phase synchronously.
     * The driver releases its open/close mutex before calling us.
     */
    const esp_err_t close_result = hid_host_device_close(handle);
    if (close_result != ESP_OK) {
        retain_unclosed_handle(handle);
        enter_terminal_fault("disconnect-callback-close", close_result);
        callback_leave();
        return;
    }

    taskENTER_CRITICAL(&s_service_lock);
    s_service.callback_closed_handle = handle;
    s_service.callback_closed_session = session;
    if (s_service.retained_fault_handle == handle) {
        s_service.retained_fault_handle = NULL;
    }
    taskEXIT_CRITICAL(&s_service_lock);

    if (finalize_from_callback) {
        taskENTER_CRITICAL(&s_service_lock);
        if (s_service.active_handle == handle &&
            s_service.active_session == session &&
            s_service.active_closing) {
            s_service.active_handle = NULL;
            s_service.active_session = 0;
            s_service.active_closing = false;
            s_service.pending_finalize = true;
            s_service.pending_finalize_session = session;
            s_service.pending_finalize_reason =
                GAMEPAD_CLOSE_DEVICE_REMOVED;
        }
        taskEXIT_CRITICAL(&s_service_lock);
    }
    callback_leave();
}

static int claim_report_slot(hid_host_device_handle_t handle, uint32_t session)
{
    int selected = -1;
    taskENTER_CRITICAL(&s_service_lock);
    for (size_t index = 0; index < GAMEPAD_REPORT_SLOT_COUNT; ++index) {
        if (!s_service.report_slots[index].in_use) {
            s_service.report_slots[index].in_use = true;
            s_service.report_slots[index].handle = handle;
            s_service.report_slots[index].session = session;
            selected = (int)index;
            break;
        }
    }
    taskEXIT_CRITICAL(&s_service_lock);
    return selected;
}

static void release_report_slot(uint8_t slot_index)
{
    if (slot_index >= GAMEPAD_REPORT_SLOT_COUNT) {
        return;
    }
    taskENTER_CRITICAL(&s_service_lock);
    memset(&s_service.report_slots[slot_index], 0,
           sizeof(s_service.report_slots[slot_index]));
    taskEXIT_CRITICAL(&s_service_lock);
}

static void hid_interface_callback(hid_host_device_handle_t handle,
                                   const hid_host_interface_event_t event,
                                   void *argument)
{
    (void)argument;
    if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        handle_driver_disconnect(handle);
        return;
    }
    if (!callback_enter()) {
        return;
    }

    uint32_t session = 0;
    if (!get_active_session(handle, &session)) {
        callback_leave();
        return;
    }

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        const int slot_number = claim_report_slot(handle, session);
        if (slot_number < 0) {
            increment_stat(&s_service.stats.reports_dropped);
            increment_stat(&s_service.stats.callback_faults);
            request_close(handle, session, GAMEPAD_CLOSE_QUEUE_OVERFLOW);
            callback_leave();
            return;
        }

        gamepad_report_slot_t *slot = &s_service.report_slots[slot_number];
        size_t copied = 0;
        const esp_err_t result = hid_host_device_get_raw_input_report_data(
            handle, slot->data, sizeof(slot->data), &copied);
        if (result != ESP_OK || copied == 0U ||
            copied > GAMEPAD_HID_MAX_REPORT_BYTES + 1U) {
            release_report_slot((uint8_t)slot_number);
            increment_stat(&s_service.stats.callback_faults);
            request_close(handle, session, GAMEPAD_CLOSE_TRANSFER_ERROR);
            callback_leave();
            return;
        }
        slot->length = copied;

        const gamepad_event_t queued = {
            .type = GAMEPAD_EVENT_REPORT,
            .report_slot = (uint8_t)slot_number,
        };
        if (xQueueSend(s_service.event_queue, &queued, 0) != pdTRUE) {
            release_report_slot((uint8_t)slot_number);
            increment_stat(&s_service.stats.reports_dropped);
            increment_stat(&s_service.stats.callback_faults);
            request_close(handle, session, GAMEPAD_CLOSE_QUEUE_OVERFLOW);
        }
        break;
    }
    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        increment_stat(&s_service.stats.callback_faults);
        request_close(handle, session, GAMEPAD_CLOSE_TRANSFER_ERROR);
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
        request_close(handle, session, GAMEPAD_CLOSE_TRANSFER_ERROR);
        break;
    }
    callback_leave();
}

static void hid_driver_callback(hid_host_device_handle_t handle,
                                const hid_host_driver_event_t event,
                                void *argument)
{
    (void)argument;
    if (!callback_enter()) {
        return;
    }
    if (event != HID_HOST_DRIVER_EVENT_CONNECTED) {
        callback_leave();
        return;
    }
    increment_stat(&s_service.stats.interfaces_seen);
    const gamepad_event_t queued = {
        .type = GAMEPAD_EVENT_CONNECTED,
        .handle = handle,
    };
    if (xQueueSend(s_service.event_queue, &queued, 0) != pdTRUE) {
        increment_stat(&s_service.stats.interfaces_rejected);
        increment_stat(&s_service.stats.callback_faults);
        retain_unclosed_handle(handle);
        enter_terminal_fault("driver-event-queue", ESP_ERR_NO_MEM);
    }
    callback_leave();
}

static void descriptor_hash_hex(const uint8_t hash[32], char output[65])
{
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0; index < 32U; ++index) {
        output[index * 2U] = digits[hash[index] >> 4U];
        output[index * 2U + 1U] = digits[hash[index] & 0x0fU];
    }
    output[64] = '\0';
}

static const char *close_reason_name(gamepad_close_reason_t reason)
{
    switch (reason) {
    case GAMEPAD_CLOSE_DEVICE_REMOVED:
        return "removed";
    case GAMEPAD_CLOSE_TRANSFER_ERROR:
        return "transfer-error";
    case GAMEPAD_CLOSE_QUEUE_OVERFLOW:
        return "queue-overflow";
    case GAMEPAD_CLOSE_DECODE_ERROR:
        return "decode-error";
    case GAMEPAD_CLOSE_SHUTDOWN:
        return "shutdown";
    default:
        return "unknown";
    }
}

static bool begin_active_close(hid_host_device_handle_t handle,
                               uint32_t session)
{
    bool claimed = false;
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.active_handle == handle &&
        s_service.active_session == session && !s_service.active_closing) {
        s_service.active_closing = true;
        claimed = true;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    return claimed;
}

static void complete_active_close(hid_host_device_handle_t handle,
                                  uint32_t session)
{
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.active_handle == handle &&
        s_service.active_session == session && s_service.active_closing) {
        s_service.active_handle = NULL;
        s_service.active_session = 0;
        s_service.active_closing = false;
    }
    taskEXIT_CRITICAL(&s_service_lock);
}

static void enter_terminal_fault(const char *stage, esp_err_t error)
{
    uint32_t session;
    taskENTER_CRITICAL(&s_service_lock);
    s_service.state = GAMEPAD_SERVICE_FAULT;
    s_service.stopping = true;
    session = s_service.active_session;
    taskEXIT_CRITICAL(&s_service_lock);

    /*
     * A terminal transport fault retains uncertain driver resources, but game
     * consumers must never retain the last held buttons or axis values.
     */
    neutralize_session(session);
    ESP_LOGE(TAG,
             "GAMEPAD_USB_FAULT stage=%s code=%s resources=retained neutral=%u",
             stage, esp_err_to_name(error), session != 0U ? 1U : 0U);
}

static void retain_unclosed_handle(hid_host_device_handle_t handle)
{
    taskENTER_CRITICAL(&s_service_lock);
    s_service.retained_fault_handle = handle;
    taskEXIT_CRITICAL(&s_service_lock);
}

/*
 * usb_host_hid 1.2.0 deliberately uses a two-call close handshake when the
 * application initiates close on an opened interface: the first call invokes
 * the DISCONNECTED callback and moves to WAIT_USER_DELETION, and the second
 * removes the interface. handle_driver_disconnect() performs phase two before
 * the first call returns. ESP_ERR_INVALID_ARG is accepted here only if that
 * callback already recorded completion for the same active session (or for a
 * rejected interface with no session) during a concurrent DEV_GONE.
 */
static bool manager_finish_interface_close(hid_host_device_handle_t handle,
                                           uint32_t session,
                                           const char *stage)
{
    const esp_err_t result = hid_host_device_close(handle);
    bool callback_closed;
    bool faulted;
    taskENTER_CRITICAL(&s_service_lock);
    callback_closed = s_service.callback_closed_handle == handle &&
                      s_service.callback_closed_session == session;
    faulted = s_service.state == GAMEPAD_SERVICE_FAULT;
    taskEXIT_CRITICAL(&s_service_lock);
    if (faulted) {
        return false;
    }
    if (result != ESP_OK &&
        !(result == ESP_ERR_INVALID_ARG && callback_closed)) {
        retain_unclosed_handle(handle);
        enter_terminal_fault(stage, result);
        return false;
    }
    return true;
}

static void manager_finalize_disconnect(uint32_t session,
                                        gamepad_close_reason_t reason)
{
    memset(&s_service.active_layout, 0, sizeof(s_service.active_layout));
    memset(s_service.active_descriptor, 0,
           sizeof(s_service.active_descriptor));
    s_service.active_descriptor_size = 0;
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.callback_closed_session == session) {
        s_service.callback_closed_handle = NULL;
        s_service.callback_closed_session = 0;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    increment_stat(&s_service.stats.disconnections);
    ESP_LOGI(TAG, "GAMEPAD_DISCONNECTED session=%" PRIu32 " reason=%s neutral=1",
             session, close_reason_name(reason));
}

static void manager_process_pending_finalize(void)
{
    uint32_t session = 0;
    gamepad_close_reason_t reason = GAMEPAD_CLOSE_DEVICE_REMOVED;
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.pending_finalize) {
        session = s_service.pending_finalize_session;
        reason = s_service.pending_finalize_reason;
        s_service.pending_finalize = false;
        s_service.pending_finalize_session = 0;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    if (session != 0U) {
        manager_finalize_disconnect(session, reason);
    }
}

static bool manager_close_idle_interface(hid_host_device_handle_t handle,
                                         const char *stage)
{
    const esp_err_t result = hid_host_device_close(handle);
    if (result == ESP_OK || result == ESP_ERR_INVALID_ARG) {
        return true;
    }
    retain_unclosed_handle(handle);
    enter_terminal_fault(stage, result);
    return false;
}

static void manager_close_active(hid_host_device_handle_t handle,
                                 uint32_t session,
                                 gamepad_close_reason_t reason)
{
    if (!begin_active_close(handle, session)) {
        return;
    }
    neutralize_session(session);

    const esp_err_t stop_result = hid_host_device_stop(handle);
    if (stop_result != ESP_OK && stop_result != ESP_ERR_NOT_FOUND &&
        stop_result != ESP_ERR_INVALID_STATE &&
        stop_result != ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "GAMEPAD_STOP_WARN code=%s",
                 esp_err_to_name(stop_result));
    }
    if (!manager_finish_interface_close(handle, session, "interface-close")) {
        return;
    }
    complete_active_close(handle, session);
    manager_finalize_disconnect(session, reason);
}

static bool manager_reject_open_interface(hid_host_device_handle_t handle,
                                          const char *reason)
{
    increment_stat(&s_service.stats.interfaces_rejected);
    if (!manager_finish_interface_close(handle, 0,
                                        "rejected-interface-close")) {
        ESP_LOGE(TAG, "GAMEPAD_INTERFACE_REJECT reason=%s close=failed",
                 reason);
        return false;
    }
    ESP_LOGW(TAG, "GAMEPAD_INTERFACE_REJECT reason=%s close=ok", reason);
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.callback_closed_handle == handle &&
        s_service.callback_closed_session == 0U) {
        s_service.callback_closed_handle = NULL;
    }
    taskEXIT_CRITICAL(&s_service_lock);
    memset(&s_service.active_layout, 0, sizeof(s_service.active_layout));
    memset(s_service.active_descriptor, 0,
           sizeof(s_service.active_descriptor));
    s_service.active_descriptor_size = 0;
    return true;
}

static void manager_handle_connected(hid_host_device_handle_t handle)
{
    taskENTER_CRITICAL(&s_service_lock);
    const bool already_active = s_service.active_handle != NULL;
    const bool accepting = s_service.state == GAMEPAD_SERVICE_RUNNING &&
                           !s_service.stopping &&
                           !s_service.pending_finalize;
    taskEXIT_CRITICAL(&s_service_lock);
    if (already_active || !accepting) {
        increment_stat(&s_service.stats.interfaces_rejected);
        (void)manager_close_idle_interface(handle, "idle-interface-close");
        return;
    }

    hid_host_dev_params_t parameters;
    const esp_err_t params_result =
        hid_host_device_get_params(handle, &parameters);
    if (params_result != ESP_OK) {
        increment_stat(&s_service.stats.interfaces_rejected);
        (void)manager_close_idle_interface(handle, "params-interface-close");
        return;
    }

    const hid_host_device_config_t interface_config = {
        .callback = hid_interface_callback,
        .callback_arg = &s_service,
    };
    const esp_err_t open_result =
        hid_host_device_open(handle, &interface_config);
    if (open_result != ESP_OK) {
        increment_stat(&s_service.stats.interfaces_rejected);
        retain_unclosed_handle(handle);
        enter_terminal_fault("interface-open", open_result);
        return;
    }

    size_t descriptor_size = 0;
    const uint8_t *descriptor =
        hid_host_get_report_descriptor(handle, &descriptor_size);
    if (descriptor == NULL || descriptor_size == 0U ||
        descriptor_size > sizeof(s_service.active_descriptor)) {
        (void)manager_reject_open_interface(handle, "descriptor-size");
        return;
    }
    memcpy(s_service.active_descriptor, descriptor, descriptor_size);
    s_service.active_descriptor_size = descriptor_size;

    const gamepad_status_t parse_status = gamepad_hid_parse_descriptor(
        s_service.active_descriptor, s_service.active_descriptor_size,
        &s_service.active_layout);
    if (parse_status != GAMEPAD_OK) {
        (void)manager_reject_open_interface(
            handle, gamepad_status_name(parse_status));
        return;
    }

    hid_host_dev_info_t device_info;
    if (hid_host_get_device_info(handle, &device_info) != ESP_OK) {
        (void)manager_reject_open_interface(handle, "device-info");
        return;
    }

    platform_gamepad_identity_t identity = {
        .vendor_id = device_info.VID,
        .product_id = device_info.PID,
        .interface_number = parameters.iface_num,
        .transport = PLATFORM_GAMEPAD_TRANSPORT_USB_HID,
    };
    if (mbedtls_sha256(s_service.active_descriptor,
                       s_service.active_descriptor_size,
                       identity.descriptor_sha256, 0) != 0) {
        (void)manager_reject_open_interface(handle, "descriptor-hash");
        return;
    }

    gamepad_hid_profile_t profile = GAMEPAD_HID_PROFILE_NONE;
    const gamepad_status_t profile_status = gamepad_hid_apply_known_profile(
        identity.vendor_id, identity.product_id, identity.descriptor_sha256,
        &s_service.active_layout, &profile);
    if (profile_status != GAMEPAD_OK) {
        (void)manager_reject_open_interface(
            handle, gamepad_status_name(profile_status));
        return;
    }

    const uint32_t capabilities =
        gamepad_hid_capabilities(&s_service.active_layout);
    uint32_t session = 0;
    taskENTER_CRITICAL(&s_snapshot_lock);
    const gamepad_status_t connect_status = platform_gamepad_model_connect(
        &s_model, &identity, capabilities, now_us(), &session);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    if (connect_status != GAMEPAD_OK) {
        (void)manager_reject_open_interface(
            handle, gamepad_status_name(connect_status));
        return;
    }

    taskENTER_CRITICAL(&s_service_lock);
    s_service.active_handle = handle;
    s_service.active_session = session;
    s_service.active_closing = false;
    taskEXIT_CRITICAL(&s_service_lock);

    const esp_err_t start_result = hid_host_device_start(handle);
    if (start_result != ESP_OK) {
        manager_close_active(handle, session, GAMEPAD_CLOSE_TRANSFER_ERROR);
        return;
    }

    increment_stat(&s_service.stats.connections);
    char hash_hex[65];
    descriptor_hash_hex(identity.descriptor_sha256, hash_hex);
    ESP_LOGI(TAG,
             "GAMEPAD_CONNECTED session=%" PRIu32
             " vid=%04x pid=%04x interface=%u protocol=%u descriptor_bytes=%u"
             " descriptor_sha256=%s profile=%s capabilities=0x%08" PRIx32,
             session, identity.vendor_id, identity.product_id,
             (unsigned)parameters.iface_num, (unsigned)parameters.proto,
             (unsigned)descriptor_size, hash_hex,
             gamepad_hid_profile_name(profile), capabilities);
}

static void manager_handle_report(uint8_t slot_index)
{
    if (slot_index >= GAMEPAD_REPORT_SLOT_COUNT) {
        increment_stat(&s_service.stats.callback_faults);
        return;
    }
    gamepad_report_slot_t *slot = &s_service.report_slots[slot_index];
    if (!slot->in_use ||
        !get_active_session(slot->handle, NULL)) {
        release_report_slot(slot_index);
        return;
    }

    platform_gamepad_snapshot_t current;
    taskENTER_CRITICAL(&s_snapshot_lock);
    gamepad_status_t decode_status =
        platform_gamepad_model_copy(&s_model, &current);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    if (decode_status == GAMEPAD_OK && current.session == slot->session &&
        current.state.connected != 0U) {
        gamepad_state_t decoded = current.state;
        decode_status = gamepad_hid_decode_report(
            &s_service.active_layout, slot->data, slot->length, now_us(),
            &decoded);
        if (decode_status == GAMEPAD_OK) {
            taskENTER_CRITICAL(&s_snapshot_lock);
            decode_status = platform_gamepad_model_commit_report(
                &s_model, slot->session, &decoded);
            taskEXIT_CRITICAL(&s_snapshot_lock);
        }
    } else {
        decode_status = GAMEPAD_ERR_DISCONNECTED;
    }

    const hid_host_device_handle_t handle = slot->handle;
    const uint32_t session = slot->session;
    release_report_slot(slot_index);
    if (decode_status == GAMEPAD_OK) {
        increment_stat(&s_service.stats.reports_committed);
    } else if (decode_status != GAMEPAD_ERR_DISCONNECTED) {
        increment_stat(&s_service.stats.malformed_reports);
        manager_close_active(handle, session, GAMEPAD_CLOSE_DECODE_ERROR);
    }
}

static bool manager_should_stop(void)
{
    bool stopping;
    taskENTER_CRITICAL(&s_service_lock);
    stopping = s_service.manager_stop_requested ||
               s_service.state == GAMEPAD_SERVICE_FAULT;
    taskEXIT_CRITICAL(&s_service_lock);
    return stopping;
}

static void manager_task(void *argument)
{
    (void)argument;
    while (!manager_should_stop()) {
        manager_process_pending_finalize();
        gamepad_close_request_t close_request;
        if (xQueueReceive(s_service.close_queue, &close_request, 0) == pdTRUE) {
            manager_close_active(close_request.handle, close_request.session,
                                 close_request.reason);
            continue;
        }

        gamepad_event_t event;
        if (xQueueReceive(s_service.event_queue, &event,
                          pdMS_TO_TICKS(20)) != pdTRUE) {
            continue;
        }
        if (event.type == GAMEPAD_EVENT_CONNECTED) {
            manager_handle_connected(event.handle);
        } else if (event.type == GAMEPAD_EVENT_REPORT) {
            manager_handle_report(event.report_slot);
        }
    }

    taskENTER_CRITICAL(&s_service_lock);
    const bool faulted = s_service.state == GAMEPAD_SERVICE_FAULT;
    taskEXIT_CRITICAL(&s_service_lock);
    if (!faulted) {
        manager_process_pending_finalize();
        hid_host_device_handle_t handle;
        uint32_t session;
        taskENTER_CRITICAL(&s_service_lock);
        handle = s_service.active_handle;
        session = s_service.active_session;
        taskEXIT_CRITICAL(&s_service_lock);
        if (handle != NULL && session != 0U) {
            manager_close_active(handle, session, GAMEPAD_CLOSE_SHUTDOWN);
        }
        taskENTER_CRITICAL(&s_service_lock);
        const bool close_faulted =
            s_service.state == GAMEPAD_SERVICE_FAULT;
        taskEXIT_CRITICAL(&s_service_lock);
        if (!close_faulted) {
            manager_process_pending_finalize();
            for (uint8_t index = 0; index < GAMEPAD_REPORT_SLOT_COUNT; ++index) {
                release_report_slot(index);
            }
        }
    }
    xEventGroupSetBits(s_service.task_events,
                       GAMEPAD_EVENT_MANAGER_STOPPED);
    vTaskDelete(NULL);
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
        s_service.task_events, GAMEPAD_EVENT_MANAGER_STOPPED, pdFALSE, pdTRUE,
        timeout_ticks);
    return (bits & GAMEPAD_EVENT_MANAGER_STOPPED) != 0U ? ESP_OK
                                                        : ESP_ERR_TIMEOUT;
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
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            return false;
        }
        vTaskDelay(1);
    }
}

static bool uninstall_abort_requested(void)
{
    bool abort_requested;
    taskENTER_CRITICAL(&s_service_lock);
    abort_requested = s_service.uninstall_abort_requested;
    taskEXIT_CRITICAL(&s_service_lock);
    return abort_requested;
}

static void hid_uninstall_task(void *argument)
{
    (void)argument;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    for (;;) {
        if (uninstall_abort_requested()) {
            result = ESP_ERR_TIMEOUT;
            break;
        }
        result = hid_host_uninstall();
        if (result != ESP_ERR_INVALID_STATE) {
            break;
        }
        vTaskDelay(1);
    }

    taskENTER_CRITICAL(&s_service_lock);
    s_service.uninstall_result = result;
    taskEXIT_CRITICAL(&s_service_lock);
    xEventGroupSetBits(s_service.task_events,
                       GAMEPAD_EVENT_HID_UNINSTALL_DONE);

    /* The owner deletes this suspended task after observing the completion bit. */
    vTaskSuspend(NULL);
}

static esp_err_t uninstall_hid_bounded(TickType_t timeout_ticks)
{
    taskENTER_CRITICAL(&s_service_lock);
    s_service.uninstall_abort_requested = false;
    s_service.uninstall_result = ESP_ERR_INVALID_STATE;
    taskEXIT_CRITICAL(&s_service_lock);
    xEventGroupClearBits(s_service.task_events,
                         GAMEPAD_EVENT_HID_UNINSTALL_DONE);

    if (xTaskCreate(hid_uninstall_task, "p4_hid_uninstall",
                    GAMEPAD_UNINSTALL_STACK_BYTES, NULL,
                    GAMEPAD_UNINSTALL_PRIORITY,
                    &s_service.uninstall_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    const EventBits_t bits = xEventGroupWaitBits(
        s_service.task_events, GAMEPAD_EVENT_HID_UNINSTALL_DONE, pdFALSE,
        pdTRUE, timeout_ticks);
    if ((bits & GAMEPAD_EVENT_HID_UNINSTALL_DONE) == 0U) {
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

esp_err_t platform_gamepad_usb_start(void)
{
    taskENTER_CRITICAL(&s_service_lock);
    if (s_service.state != GAMEPAD_SERVICE_STOPPED) {
        taskEXIT_CRITICAL(&s_service_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_service, 0, sizeof(s_service));
    s_service.state = GAMEPAD_SERVICE_STARTING;
    taskEXIT_CRITICAL(&s_service_lock);

    taskENTER_CRITICAL(&s_snapshot_lock);
    platform_gamepad_model_init(&s_model);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    s_service.event_queue =
        xQueueCreate(GAMEPAD_EVENT_QUEUE_LENGTH, sizeof(gamepad_event_t));
    s_service.close_queue =
        xQueueCreate(1, sizeof(gamepad_close_request_t));
    s_service.task_events = xEventGroupCreate();
    if (s_service.event_queue == NULL || s_service.close_queue == NULL ||
        s_service.task_events == NULL) {
        delete_service_resources();
        taskENTER_CRITICAL(&s_service_lock);
        s_service.state = GAMEPAD_SERVICE_STOPPED;
        taskEXIT_CRITICAL(&s_service_lock);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = platform_usb_host_class_acquire(
        PLATFORM_USB_CLASS_HID, &s_service.host_lease);
    if (result != ESP_OK) {
        delete_service_resources();
        taskENTER_CRITICAL(&s_service_lock);
        s_service.state = GAMEPAD_SERVICE_STOPPED;
        taskEXIT_CRITICAL(&s_service_lock);
        return result;
    }

    if (xTaskCreate(manager_task, "p4_gamepad_mgr",
                    GAMEPAD_MANAGER_STACK_BYTES, NULL,
                    GAMEPAD_MANAGER_PRIORITY,
                    &s_service.manager_task) != pdPASS) {
        const esp_err_t release_result =
            platform_usb_host_class_release(&s_service.host_lease);
        if (release_result != ESP_OK) {
            enter_terminal_fault("manager-create-release", release_result);
            return release_result;
        }
        delete_service_resources();
        taskENTER_CRITICAL(&s_service_lock);
        s_service.state = GAMEPAD_SERVICE_STOPPED;
        taskEXIT_CRITICAL(&s_service_lock);
        return ESP_ERR_NO_MEM;
    }

    const hid_host_driver_config_t hid_config = {
        .create_background_task = true,
        .task_priority = GAMEPAD_HID_PRIORITY,
        .stack_size = GAMEPAD_HID_STACK_BYTES,
        .core_id = tskNO_AFFINITY,
        .callback = hid_driver_callback,
        .callback_arg = &s_service,
    };
    result = hid_host_install(&hid_config);
    if (result != ESP_OK) {
        const esp_err_t manager_result =
            stop_manager(pdMS_TO_TICKS(1000));
        if (manager_result != ESP_OK) {
            enter_terminal_fault("hid-install-manager-stop", manager_result);
            return manager_result;
        }
        const esp_err_t release_result =
            platform_usb_host_class_release(&s_service.host_lease);
        if (release_result != ESP_OK) {
            enter_terminal_fault("hid-install-lease-release", release_result);
            return release_result;
        }
        delete_service_resources();
        taskENTER_CRITICAL(&s_service_lock);
        s_service.state = GAMEPAD_SERVICE_STOPPED;
        taskEXIT_CRITICAL(&s_service_lock);
        return result;
    }

    taskENTER_CRITICAL(&s_service_lock);
    s_service.state = GAMEPAD_SERVICE_RUNNING;
    taskEXIT_CRITICAL(&s_service_lock);
    ESP_LOGI(TAG, "GAMEPAD_USB_READY tier=generic-hid report_max=%u",
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
    if (s_service.state != GAMEPAD_SERVICE_RUNNING) {
        taskEXIT_CRITICAL(&s_service_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_service.state = GAMEPAD_SERVICE_STOPPING;
    taskEXIT_CRITICAL(&s_service_lock);

    esp_err_t result = stop_manager(timeout_ticks);
    if (result != ESP_OK) {
        enter_terminal_fault("manager-stop", result);
        return result;
    }
    result = uninstall_hid_bounded(timeout_ticks);
    if (result != ESP_OK) {
        enter_terminal_fault("hid-uninstall", result);
        return result;
    }
    result = platform_usb_host_class_release(&s_service.host_lease);
    if (result != ESP_OK) {
        enter_terminal_fault("lease-release", result);
        return result;
    }
    manager_process_pending_finalize();
    delete_service_resources();

    taskENTER_CRITICAL(&s_service_lock);
    s_service.state = GAMEPAD_SERVICE_STOPPED;
    taskEXIT_CRITICAL(&s_service_lock);
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
        platform_gamepad_model_copy(&s_model, snapshot);
    taskEXIT_CRITICAL(&s_snapshot_lock);
    return status == GAMEPAD_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
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
