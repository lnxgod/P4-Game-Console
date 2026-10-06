// SPDX-License-Identifier: MIT
#include "platform_gamepad_xusb/xusb.h"
#include "platform_usb_host/platform_usb_host.h"
#include "gamepad/xusb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "usb/usb_host.h"
#include <inttypes.h>
#include <string.h>

#define STARTED_BIT (1U << 0)
#define STOPPED_BIT (1U << 1)
typedef enum { SERVICE_OFF, SERVICE_STARTING, SERVICE_RUNNING, SERVICE_FAULT } service_state_t;

/* USB handles, flags and packets below have one owner: worker_task. Public
 * lifecycle requests and snapshot access use s_lock; no game sees USB state. */
static struct {
    service_state_t state;
    bool stop_requested;
    esp_err_t startup_result;
    esp_err_t worker_result;
    EventGroupHandle_t events;
    TaskHandle_t task;
    platform_usb_class_lease_t lease;
    platform_gamepad_model_t model;
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    usb_transfer_t *transfer;
    gamepad_xusb_interface_t interface;
    uint32_t session;
    uint8_t pending_address;
    bool claimed;
    bool pending;
    bool closing;
    bool flush_requested;
    bool cleanup_fault;
    bool report_ready;
    size_t report_bytes;
    uint8_t report[GAMEPAD_XUSB_PACKET_MAX_BYTES];
} s;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static const char *const TAG = "gamepad_xusb";

static uint64_t now_us(void) { return (uint64_t)esp_timer_get_time(); }

static bool stop_requested(void)
{
    taskENTER_CRITICAL(&s_lock);
    const bool result = s.stop_requested;
    taskEXIT_CRITICAL(&s_lock);
    return result;
}

static void request_close(void)
{
    taskENTER_CRITICAL(&s_lock);
    (void)platform_gamepad_model_disconnect(&s.model, s.session, now_us());
    taskEXIT_CRITICAL(&s_lock);
    s.closing = true;
    s.report_ready = false;
}

static void fault(const char *stage, esp_err_t error)
{
    request_close();
    s.cleanup_fault = true;
    taskENTER_CRITICAL(&s_lock);
    s.state = SERVICE_FAULT;
    s.worker_result = error;
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGE(TAG, "XUSB_FAULT stage=%s error=%s resources=retained",
             stage, esp_err_to_name(error));
}

static void client_event(const usb_host_client_event_msg_t *event, void *context)
{
    (void)context;
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if ((s.device == NULL || s.closing) && s.pending_address == 0U &&
            !s.cleanup_fault && !stop_requested())
            s.pending_address = event->new_dev.address;
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
               s.device == event->dev_gone.dev_hdl) {
        request_close(); /* Neutral before any asynchronous USB cleanup. */
    }
}

static void transfer_done(usb_transfer_t *transfer)
{
    if (transfer != s.transfer || !s.pending) {
        fault("unexpected-completion", ESP_ERR_INVALID_STATE);
        return;
    }
    s.pending = false;
    if (s.closing || stop_requested()) {
        request_close();
        return;
    }
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED ||
        transfer->actual_num_bytes < 0 ||
        (size_t)transfer->actual_num_bytes > sizeof(s.report)) {
        request_close();
        return;
    }
    s.report_bytes = (size_t)transfer->actual_num_bytes;
    memcpy(s.report, transfer->data_buffer, s.report_bytes);
    s.report_ready = true; /* Decode in worker_step, outside the callback. */
}

static void close_device(void)
{
    if (s.device == NULL || s.cleanup_fault) return;
    if (s.pending) {
        if (!s.flush_requested) {
            s.flush_requested = true;
            const esp_err_t halted = usb_host_endpoint_halt(s.device, s.interface.endpoint_in);
            const esp_err_t flushed = usb_host_endpoint_flush(s.device, s.interface.endpoint_in);
            if (flushed != ESP_OK) fault("cancel", halted != ESP_OK ? halted : flushed);
        }
        return; /* Completion callback owns the transfer until cancellation arrives. */
    }
    if (s.claimed) {
        const esp_err_t result = usb_host_interface_release(
            s.client, s.device, s.interface.interface_number);
        if (result != ESP_OK) { fault("release", result); return; }
        s.claimed = false;
    }
    const esp_err_t closed = usb_host_device_close(s.client, s.device);
    if (closed != ESP_OK) { fault("close", closed); return; }
    s.device = NULL;
    if (s.transfer != NULL) {
        const esp_err_t freed = usb_host_transfer_free(s.transfer);
        if (freed != ESP_OK) { fault("transfer-free", freed); return; }
        s.transfer = NULL;
    }
    if (s.session != 0U)
        ESP_LOGI(TAG, "XUSB_DISCONNECTED session=%" PRIu32 " neutral=1", s.session);
    s.session = 0U;
    s.closing = false;
    s.flush_requested = false;
    s.report_ready = false;
    memset(&s.interface, 0, sizeof(s.interface));
}

static void open_device(void)
{
    const uint8_t address = s.pending_address;
    s.pending_address = 0U;
    /* Opening may race unplug; failure leaves the pre-cleared handle alone. */
    usb_device_handle_t device = NULL;
    const esp_err_t opened = usb_host_device_open(s.client, address, &device);
    if (opened != ESP_OK) return;
    s.device = device;
    const usb_config_desc_t *configuration = NULL;
    const usb_device_desc_t *description = NULL;
    if (usb_host_get_active_config_descriptor(device, &configuration) != ESP_OK ||
        configuration == NULL ||
        usb_host_get_device_descriptor(device, &description) != ESP_OK ||
        description == NULL) {
        request_close();
        return;
    }
    const size_t bytes = configuration->wTotalLength;
    const gamepad_status_t selection = gamepad_xusb_find_interface(
        (const uint8_t *)configuration, bytes, &s.interface);
    if (selection != GAMEPAD_OK) {
        ESP_LOGD(TAG, "XUSB_SKIP vid=%04x pid=%04x reason=%s",
                 description->idVendor, description->idProduct,
                 gamepad_status_name(selection));
        request_close();
        return;
    }
    platform_gamepad_identity_t identity = {
        .vendor_id = description->idVendor,
        .product_id = description->idProduct,
        .interface_number = s.interface.interface_number,
        .transport = PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB,
    };
    if (mbedtls_sha256((const unsigned char *)configuration, bytes,
                       identity.descriptor_sha256, 0) != 0 ||
        usb_host_interface_claim(s.client, device, s.interface.interface_number, 0) != ESP_OK) {
        request_close();
        return;
    }
    s.claimed = true;
    if (usb_host_transfer_alloc(GAMEPAD_XUSB_PACKET_MAX_BYTES, 0, &s.transfer) != ESP_OK) {
        request_close();
        return;
    }
    s.transfer->device_handle = device;
    s.transfer->bEndpointAddress = s.interface.endpoint_in;
    s.transfer->callback = transfer_done;
    s.transfer->context = NULL;
    s.transfer->num_bytes = s.interface.packet_bytes;
    taskENTER_CRITICAL(&s_lock);
    const gamepad_status_t connected = platform_gamepad_model_connect(
        &s.model, &identity, GAMEPAD_XUSB_CAPABILITIES, now_us(), &s.session);
    taskEXIT_CRITICAL(&s_lock);
    if (connected != GAMEPAD_OK) { request_close(); return; }
    char hash[65];
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0U; i < sizeof(identity.descriptor_sha256); ++i) {
        hash[2U*i] = digits[identity.descriptor_sha256[i] >> 4U];
        hash[2U*i+1U] = digits[identity.descriptor_sha256[i] & 15U];
    }
    hash[64] = '\0';
    ESP_LOGI(TAG, "XUSB_CONNECTED session=%" PRIu32
             " vid=%04x pid=%04x interface=%u protocol=wired-360"
             " config_sha256=%s",
             s.session, identity.vendor_id, identity.product_id,
             (unsigned)identity.interface_number, hash);
}

/* One bounded work step, also used by the host lifecycle simulation. */
static void worker_step(void)
{
    if (stop_requested()) {
        s.pending_address = 0U;
        if (s.device != NULL) request_close();
    }
    if (s.cleanup_fault) return;
    if (s.closing) { close_device(); return; }
    if (s.device == NULL && s.pending_address != 0U) open_device();
    if (s.device == NULL || s.closing) return;
    if (s.report_ready) {
        s.report_ready = false;
        platform_gamepad_snapshot_t current;
        taskENTER_CRITICAL(&s_lock);
        gamepad_status_t result = platform_gamepad_model_copy(&s.model, &current);
        taskEXIT_CRITICAL(&s_lock);
        bool input = false;
        if (result == GAMEPAD_OK)
            result = gamepad_xusb_decode(s.report, s.report_bytes, now_us(),
                                        &current.state, &input);
        if (result == GAMEPAD_OK && input) {
            taskENTER_CRITICAL(&s_lock);
            result = platform_gamepad_model_commit_report(&s.model, s.session, &current.state);
            taskEXIT_CRITICAL(&s_lock);
        }
        if (result != GAMEPAD_OK) {
            ESP_LOGW(TAG, "XUSB_REPORT_REJECT reason=%s neutral=1", gamepad_status_name(result));
            request_close();
            return;
        }
    }
    if (!s.pending) {
        const esp_err_t result = usb_host_transfer_submit(s.transfer);
        if (result == ESP_OK) s.pending = true;
        else request_close();
    }
}

static void worker_task(void *argument)
{
    (void)argument;
    const usb_host_client_config_t configuration = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {.client_event_callback = client_event, .callback_arg = NULL},
    };
    const esp_err_t registered = usb_host_client_register(&configuration, &s.client);
    taskENTER_CRITICAL(&s_lock);
    s.startup_result = registered;
    s.worker_result = registered;
    taskEXIT_CRITICAL(&s_lock);
    xEventGroupSetBits(s.events, STARTED_BIT);
    if (registered == ESP_OK) {
        for (;;) {
            worker_step();
            if (stop_requested() && s.device == NULL && !s.cleanup_fault) break;
            const esp_err_t result = usb_host_client_handle_events(s.client, pdMS_TO_TICKS(20U));
            if (result != ESP_OK && result != ESP_ERR_TIMEOUT && !s.cleanup_fault)
                fault("client-events", result);
        }
        const esp_err_t result = usb_host_client_deregister(s.client);
        taskENTER_CRITICAL(&s_lock);
        s.worker_result = result;
        taskEXIT_CRITICAL(&s_lock);
        if (result == ESP_OK) s.client = NULL;
    }
    xEventGroupSetBits(s.events, STOPPED_BIT);
    vTaskSuspend(NULL); /* Owner deletes task after observing STOPPED_BIT. */
}

static esp_err_t finish_stop(TickType_t timeout_ticks)
{
    taskENTER_CRITICAL(&s_lock);
    s.stop_requested = true;
    taskEXIT_CRITICAL(&s_lock);
    const EventBits_t bits = xEventGroupWaitBits(
        s.events, STOPPED_BIT, pdFALSE, pdTRUE, timeout_ticks);
    if ((bits & STOPPED_BIT) == 0U) return ESP_ERR_TIMEOUT;
    if (s.client != NULL || s.device != NULL || s.transfer != NULL)
        return ESP_ERR_INVALID_STATE;
    if (s.task != NULL) { vTaskDelete(s.task); s.task = NULL; }
    const esp_err_t result = platform_usb_host_class_release(&s.lease);
    if (result != ESP_OK) return result;
    platform_gamepad_unregister_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB,
                                         platform_gamepad_xusb_get_snapshot);
    vEventGroupDelete(s.events);
    s.events = NULL;
    taskENTER_CRITICAL(&s_lock);
    s.state = SERVICE_OFF;
    taskEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t platform_gamepad_xusb_start(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s.state != SERVICE_OFF) { taskEXIT_CRITICAL(&s_lock); return ESP_ERR_INVALID_STATE; }
    memset(&s, 0, sizeof(s));
    s.state = SERVICE_STARTING;
    platform_gamepad_model_init(&s.model);
    taskEXIT_CRITICAL(&s_lock);
    s.events = xEventGroupCreate();
    if (s.events == NULL) { s.state = SERVICE_OFF; return ESP_ERR_NO_MEM; }
    esp_err_t result = platform_usb_host_class_acquire(PLATFORM_USB_CLASS_GAMEPAD_VENDOR, &s.lease);
    if (result != ESP_OK) {
        vEventGroupDelete(s.events); s.events = NULL; s.state = SERVICE_OFF;
        return result;
    }
    if (xTaskCreate(worker_task, "p4_xusb", 4096U, NULL, 5U, &s.task) != pdPASS) {
        xEventGroupSetBits(s.events, STOPPED_BIT);
        result = finish_stop(pdMS_TO_TICKS(1000U));
        return result == ESP_OK ? ESP_ERR_NO_MEM : result;
    }
    const EventBits_t bits = xEventGroupWaitBits(s.events, STARTED_BIT, pdFALSE, pdTRUE,
                                               pdMS_TO_TICKS(2000U));
    taskENTER_CRITICAL(&s_lock);
    result = (bits & STARTED_BIT) != 0U ? s.startup_result : ESP_ERR_TIMEOUT;
    taskEXIT_CRITICAL(&s_lock);
    if (result != ESP_OK) {
        const esp_err_t cleanup = finish_stop(pdMS_TO_TICKS(1000U));
        return cleanup == ESP_OK ? result : cleanup;
    }
    result = platform_gamepad_register_provider(PLATFORM_GAMEPAD_TRANSPORT_USB_XUSB,
                                                platform_gamepad_xusb_get_snapshot);
    if (result != ESP_OK) {
        const esp_err_t cleanup = finish_stop(pdMS_TO_TICKS(1000U));
        return cleanup == ESP_OK ? result : cleanup;
    }
    taskENTER_CRITICAL(&s_lock);
    s.state = SERVICE_RUNNING;
    taskEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "XUSB_READY protocol=wired-360 input_only=1");
    return ESP_OK;
}

esp_err_t platform_gamepad_xusb_stop(TickType_t timeout_ticks)
{
    platform_usb_host_info_t host;
    if (timeout_ticks == 0U) return ESP_ERR_INVALID_ARG;
    if (platform_usb_host_get_info(&host) != ESP_OK || host.state != PLATFORM_USB_HOST_QUIESCING)
        return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_lock);
    const bool active = s.state != SERVICE_OFF;
    taskEXIT_CRITICAL(&s_lock);
    return active ? finish_stop(timeout_ticks) : ESP_ERR_INVALID_STATE;
}

esp_err_t platform_gamepad_xusb_get_snapshot(platform_gamepad_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(&s_lock);
    const gamepad_status_t result = platform_gamepad_model_copy(&s.model, snapshot);
    taskEXIT_CRITICAL(&s_lock);
    return result == GAMEPAD_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}
