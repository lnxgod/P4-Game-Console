// SPDX-License-Identifier: MIT

#include "platform/gamepad_ble.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#pragma GCC diagnostic pop

#include "gamepad/hid_gamepad.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "platform/ble_host.h"
#include "platform/gamepad.h"

enum {
    BLE_HID_SERVICE_UUID = 0x1812,
    BLE_HID_REPORT_MAP_UUID = 0x2A4B,
    BLE_HID_REPORT_UUID = 0x2A4D,
    BLE_HID_PROTOCOL_MODE_UUID = 0x2A4E,
    BLE_HID_REPORT_REFERENCE_UUID = 0x2908,
    BLE_HID_REPORT_TYPE_INPUT = 1,
    BLE_HID_REPORT_PROTOCOL = 1,
    BLE_HID_MAX_REPORT_CHARACTERISTICS = 12,
    BLE_HID_SCAN_CANDIDATES = 4,
    BLE_HID_SCAN_TIMEOUT_MS = 30000,
    BLE_HID_CONNECT_TIMEOUT_MS = 10000,
    BLE_HID_SCAN_INTERVAL = 48,
    BLE_HID_SCAN_WINDOW = 24,
    BLE_HID_CONN_INTERVAL_MIN = 6,
    BLE_HID_CONN_INTERVAL_MAX = 12,
    BLE_HID_SUPERVISION_TIMEOUT = 500,
    BLE_HID_APPEARANCE_JOYSTICK = 0x03C3,
    BLE_HID_APPEARANCE_GAMEPAD = 0x03C4,
    BLE_HID_PAIR_RECORD_MAGIC = 0x50344750,
    BLE_HID_PAIR_RECORD_VERSION = 1,
};

typedef struct {
    uint16_t def_handle;
    uint16_t val_handle;
    uint16_t end_handle;
    uint16_t cccd_handle;
    uint16_t reference_handle;
    uint8_t properties;
    uint8_t report_id;
    uint8_t report_type;
    bool subscribed;
} ble_hid_report_characteristic_t;

typedef struct {
    ble_addr_t address;
    int8_t rssi;
    bool occupied;
    bool connectable;
    bool hid;
    char name[PLATFORM_GAMEPAD_BLE_NAME_BYTES];
} ble_hid_scan_candidate_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t address_type;
    uint8_t address[6];
    char name[PLATFORM_GAMEPAD_BLE_NAME_BYTES];
} ble_hid_pair_record_t;

typedef struct {
    platform_gamepad_ble_status_t status;
    platform_gamepad_model_t model;
    gamepad_hid_layout_t layout;
    uint8_t report_map[GAMEPAD_HID_MAX_DESCRIPTOR_BYTES];
    size_t report_map_length;
    uint8_t decode_buffer[GAMEPAD_HID_MAX_REPORT_BYTES + 1U];
    ble_hid_report_characteristic_t
        reports[BLE_HID_MAX_REPORT_CHARACTERISTICS];
    ble_hid_scan_candidate_t candidates[BLE_HID_SCAN_CANDIDATES];
    ble_hid_pair_record_t saved;
    ble_addr_t selected_address;
    ble_addr_t peer_id_address;
    uint16_t conn_handle;
    uint16_t service_start;
    uint16_t service_end;
    uint16_t report_map_handle;
    uint16_t protocol_mode_handle;
    uint8_t protocol_mode_properties;
    uint32_t session;
    uint8_t report_count;
    uint8_t descriptor_index;
    uint8_t reference_index;
    uint8_t subscribe_index;
    int8_t last_report_characteristic;
    bool initialized;
    bool connect_pending;
    bool forget_pending;
    bool selected_valid;
    bool saved_valid;
} ble_hid_runtime_t;

static const char *const TAG = "p4_ble_gamepad";
static const char *const NVS_NAMESPACE = "p4_ble_pad";
static const char *const NVS_PEER_KEY = "peer";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_model_lock = portMUX_INITIALIZER_UNLOCKED;
/*
 * Descriptor/layout storage is several KiB and is not consumed by DMA.
 * Keep the complete BLE-HID runtime in external RAM so adding controller
 * profiles cannot starve the board's fixed 32 KiB internal DMA reserve before
 * app_main. The runtime is initialized explicitly by prepare() after PSRAM is
 * available and before this provider is registered with the shared broker.
 */
static EXT_RAM_BSS_ATTR ble_hid_runtime_t s_ble;

static int gap_event(struct ble_gap_event *event, void *argument);
static void host_sync(void *context);
static void host_reset(int reason, void *context);
static void start_scan(void);
static void start_report_descriptor_discovery(void);
static void start_report_reference_reads(void);
static void start_subscriptions(void);
static int characteristic_discovered(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    const struct ble_gatt_chr *characteristic, void *argument);
static int report_map_read(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attribute, void *argument);
static int protocol_mode_complete(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attribute, void *argument);

static const platform_ble_host_client_t BLE_HID_HOST_CLIENT = {
    .on_sync = host_sync,
    .on_reset = host_reset,
};

static uint64_t monotonic_us(void)
{
    const int64_t now = esp_timer_get_time();
    return now <= 0 ? 0U : (uint64_t)now;
}

static void set_state(platform_gamepad_ble_state_t state, int error)
{
    portENTER_CRITICAL(&s_lock);
    s_ble.status.state = state;
    s_ble.status.last_error = error;
    s_ble.status.ready = state == PLATFORM_GAMEPAD_BLE_READY;
    portEXIT_CRITICAL(&s_lock);
}

static void count_drop(int error)
{
    portENTER_CRITICAL(&s_lock);
    if (s_ble.status.reports_dropped != UINT32_MAX) {
        ++s_ble.status.reports_dropped;
    }
    s_ble.status.last_error = error;
    portEXIT_CRITICAL(&s_lock);
}

static bool connection_matches(uint16_t conn_handle)
{
    portENTER_CRITICAL(&s_lock);
    const bool matches = s_ble.status.connected &&
        s_ble.conn_handle == conn_handle;
    portEXIT_CRITICAL(&s_lock);
    return matches;
}

static bool pair_record_valid(const ble_hid_pair_record_t *record)
{
    if (record == NULL || record->magic != BLE_HID_PAIR_RECORD_MAGIC ||
        record->version != BLE_HID_PAIR_RECORD_VERSION ||
        record->size != sizeof(*record) || record->address_type > 1U ||
        record->name[PLATFORM_GAMEPAD_BLE_NAME_BYTES - 1U] != '\0') {
        return false;
    }
    uint8_t address_or = 0U;
    for (size_t index = 0U; index < sizeof(record->address); ++index) {
        address_or |= record->address[index];
    }
    return address_or != 0U;
}

static void load_pair_record(void)
{
    ble_hid_pair_record_t record = {0};
    nvs_handle_t handle = 0U;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_OK) {
        size_t size = sizeof(record);
        result = nvs_get_blob(handle, NVS_PEER_KEY, &record, &size);
        nvs_close(handle);
        if (result == ESP_OK && size == sizeof(record) &&
            pair_record_valid(&record)) {
            portENTER_CRITICAL(&s_lock);
            s_ble.saved = record;
            s_ble.saved_valid = true;
            s_ble.status.bonded = true;
            (void)snprintf(s_ble.status.name, sizeof(s_ble.status.name),
                           "%s", record.name);
            s_ble.status.state = PLATFORM_GAMEPAD_BLE_STANDBY;
            portEXIT_CRITICAL(&s_lock);
            return;
        }
    }
    portENTER_CRITICAL(&s_lock);
    memset(&s_ble.saved, 0, sizeof(s_ble.saved));
    s_ble.saved_valid = false;
    s_ble.status.bonded = false;
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t save_pair_record(const ble_addr_t *address, const char *name)
{
    if (address == NULL || address->type > 1U || name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ble_hid_pair_record_t record = {
        .magic = BLE_HID_PAIR_RECORD_MAGIC,
        .version = BLE_HID_PAIR_RECORD_VERSION,
        .size = (uint16_t)sizeof(record),
        .address_type = address->type,
    };
    memcpy(record.address, address->val, sizeof(record.address));
    (void)snprintf(record.name, sizeof(record.name), "%s", name);

    nvs_handle_t handle = 0U;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_blob(handle, NVS_PEER_KEY, &record, sizeof(record));
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    if (handle != 0U) {
        nvs_close(handle);
    }
    if (result == ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_ble.saved = record;
        s_ble.saved_valid = true;
        s_ble.status.bonded = true;
        portEXIT_CRITICAL(&s_lock);
    }
    return result;
}

static esp_err_t clear_pair_record(void)
{
    nvs_handle_t handle = 0U;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_erase_key(handle, NVS_PEER_KEY);
        if (result == ESP_ERR_NVS_NOT_FOUND) {
            result = ESP_OK;
        }
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    if (handle != 0U) {
        nvs_close(handle);
    }
    portENTER_CRITICAL(&s_lock);
    memset(&s_ble.saved, 0, sizeof(s_ble.saved));
    s_ble.saved_valid = false;
    s_ble.status.bonded = false;
    s_ble.status.name[0] = '\0';
    portEXIT_CRITICAL(&s_lock);
    return result;
}

static esp_err_t complete_forget(void)
{
    ble_addr_t peer = {0};
    portENTER_CRITICAL(&s_lock);
    const bool have_peer = s_ble.saved_valid;
    if (have_peer) {
        peer.type = s_ble.saved.address_type;
        memcpy(peer.val, s_ble.saved.address, sizeof(peer.val));
    }
    portEXIT_CRITICAL(&s_lock);

    int unpair_result = 0;
    if (have_peer && platform_ble_host_ready()) {
        unpair_result = ble_gap_unpair(&peer);
        if (unpair_result == BLE_HS_ENOENT) {
            unpair_result = 0;
        }
    }
    const esp_err_t clear_result = clear_pair_record();
    portENTER_CRITICAL(&s_lock);
    s_ble.forget_pending = false;
    portEXIT_CRITICAL(&s_lock);
    if (unpair_result != 0) {
        set_state(PLATFORM_GAMEPAD_BLE_ERROR, unpair_result);
        return ESP_FAIL;
    }
    set_state(PLATFORM_GAMEPAD_BLE_OFF, (int)clear_result);
    return clear_result;
}

static void neutralize_model(void)
{
    portENTER_CRITICAL(&s_model_lock);
    if (s_ble.session != 0U) {
        (void)platform_gamepad_model_disconnect(
            &s_ble.model, s_ble.session, monotonic_us());
    }
    portEXIT_CRITICAL(&s_model_lock);
}

static void clear_connection_state(void)
{
    neutralize_model();
    portENTER_CRITICAL(&s_lock);
    s_ble.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_ble.service_start = 0U;
    s_ble.service_end = 0U;
    s_ble.report_map_handle = 0U;
    s_ble.protocol_mode_handle = 0U;
    s_ble.protocol_mode_properties = 0U;
    s_ble.report_map_length = 0U;
    s_ble.report_count = 0U;
    s_ble.descriptor_index = 0U;
    s_ble.reference_index = 0U;
    s_ble.subscribe_index = 0U;
    s_ble.last_report_characteristic = -1;
    s_ble.session = 0U;
    s_ble.connect_pending = false;
    s_ble.selected_valid = false;
    s_ble.status.connected = false;
    s_ble.status.encrypted = false;
    s_ble.status.ready = false;
    s_ble.status.input_reports = 0U;
    s_ble.status.att_mtu = BLE_ATT_MTU_DFLT;
    memset(s_ble.reports, 0, sizeof(s_ble.reports));
    gamepad_hid_layout_init(&s_ble.layout);
    portEXIT_CRITICAL(&s_lock);
}

static void fail_connection(uint16_t conn_handle, int error, const char *stage)
{
    ESP_LOGW(TAG, "BLE_GAMEPAD_SETUP_FAIL stage=%s rc=%d", stage, error);
    count_drop(error);
    set_state(PLATFORM_GAMEPAD_BLE_ERROR, error);
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void copy_adv_name(const struct ble_hs_adv_fields *fields,
                          char output[PLATFORM_GAMEPAD_BLE_NAME_BYTES])
{
    if (fields == NULL || output == NULL || fields->name == NULL ||
        fields->name_len == 0U) {
        return;
    }
    size_t length = fields->name_len;
    if (length >= PLATFORM_GAMEPAD_BLE_NAME_BYTES) {
        length = PLATFORM_GAMEPAD_BLE_NAME_BYTES - 1U;
    }
    for (size_t index = 0U; index < length; ++index) {
        const uint8_t character = fields->name[index];
        output[index] = character >= 0x20U && character <= 0x7eU
            ? (char)character : '?';
    }
    output[length] = '\0';
}

static bool adv_is_hid(const struct ble_hs_adv_fields *fields)
{
    if (fields == NULL) {
        return false;
    }
    for (uint8_t index = 0U; index < fields->num_uuids16; ++index) {
        if (fields->uuids16[index].value == BLE_HID_SERVICE_UUID) {
            return true;
        }
    }
    return fields->appearance_is_present &&
        (fields->appearance == BLE_HID_APPEARANCE_JOYSTICK ||
         fields->appearance == BLE_HID_APPEARANCE_GAMEPAD);
}

static bool addresses_equal(const ble_addr_t *left, const ble_addr_t *right)
{
    return left != NULL && right != NULL && left->type == right->type &&
        memcmp(left->val, right->val, sizeof(left->val)) == 0;
}

static ble_hid_scan_candidate_t *candidate_for(const ble_addr_t *address)
{
    ble_hid_scan_candidate_t *free_slot = NULL;
    ble_hid_scan_candidate_t *weakest = NULL;
    for (size_t index = 0U; index < BLE_HID_SCAN_CANDIDATES; ++index) {
        ble_hid_scan_candidate_t *const candidate = &s_ble.candidates[index];
        if (candidate->occupied &&
            addresses_equal(&candidate->address, address)) {
            return candidate;
        }
        if (!candidate->occupied && free_slot == NULL) {
            free_slot = candidate;
        }
        if (candidate->occupied &&
            (weakest == NULL || candidate->rssi < weakest->rssi)) {
            weakest = candidate;
        }
    }
    ble_hid_scan_candidate_t *const selected =
        free_slot != NULL ? free_slot : weakest;
    if (selected != NULL) {
        *selected = (ble_hid_scan_candidate_t){
            .address = *address,
            .occupied = true,
            .rssi = INT8_MIN,
        };
    }
    return selected;
}

static bool saved_candidate_matches(const ble_hid_scan_candidate_t *candidate)
{
    if (!s_ble.saved_valid || candidate == NULL) {
        return false;
    }
    if (candidate->address.type == s_ble.saved.address_type &&
        memcmp(candidate->address.val, s_ble.saved.address,
               sizeof(s_ble.saved.address)) == 0) {
        return true;
    }
    return s_ble.saved.name[0] != '\0' && candidate->name[0] != '\0' &&
        strcmp(candidate->name, s_ble.saved.name) == 0;
}

static bool saved_identity_matches(const ble_addr_t *peer_id_address)
{
    if (!s_ble.saved_valid) {
        return true;
    }
    return peer_id_address != NULL &&
        peer_id_address->type == s_ble.saved.address_type &&
        memcmp(peer_id_address->val, s_ble.saved.address,
               sizeof(s_ble.saved.address)) == 0;
}

static void connect_candidate(const ble_hid_scan_candidate_t *candidate)
{
    if (candidate == NULL || !candidate->hid || !candidate->connectable) {
        return;
    }
    s_ble.selected_address = candidate->address;
    s_ble.selected_valid = true;
    portENTER_CRITICAL(&s_lock);
    s_ble.connect_pending = false;
    s_ble.status.rssi = candidate->rssi;
    (void)snprintf(s_ble.status.name, sizeof(s_ble.status.name), "%s",
                   candidate->name[0] != '\0'
                       ? candidate->name : "BLE GAMEPAD");
    portEXIT_CRITICAL(&s_lock);
    (void)ble_gap_disc_cancel();

    uint8_t own_addr_type = 0U;
    const esp_err_t own_result =
        platform_ble_host_own_addr_type(&own_addr_type);
    if (own_result != ESP_OK) {
        fail_connection(BLE_HS_CONN_HANDLE_NONE, (int)own_result,
                        "own-address");
        return;
    }
    const struct ble_gap_conn_params parameters = {
        .scan_itvl = BLE_HID_SCAN_INTERVAL,
        .scan_window = BLE_HID_SCAN_WINDOW,
        .itvl_min = BLE_HID_CONN_INTERVAL_MIN,
        .itvl_max = BLE_HID_CONN_INTERVAL_MAX,
        .latency = 0U,
        .supervision_timeout = BLE_HID_SUPERVISION_TIMEOUT,
        .min_ce_len = 0U,
        .max_ce_len = 0U,
    };
    set_state(PLATFORM_GAMEPAD_BLE_CONNECTING, 0);
    const int result = ble_gap_connect(
        own_addr_type, &candidate->address, BLE_HID_CONNECT_TIMEOUT_MS,
        &parameters, gap_event, NULL);
    if (result != 0) {
        fail_connection(BLE_HS_CONN_HANDLE_NONE, result, "connect-start");
    }
}

static void observe_advertisement(const struct ble_gap_disc_desc *discovery)
{
    if (discovery == NULL ||
        s_ble.status.state != PLATFORM_GAMEPAD_BLE_SCANNING) {
        return;
    }
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(
            &fields, discovery->data, discovery->length_data) != 0) {
        return;
    }
    ble_hid_scan_candidate_t *const candidate =
        candidate_for(&discovery->addr);
    if (candidate == NULL) {
        return;
    }
    candidate->rssi = discovery->rssi;
    candidate->connectable = candidate->connectable ||
        discovery->event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
        discovery->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
    candidate->hid = candidate->hid || adv_is_hid(&fields);
    copy_adv_name(&fields, candidate->name);
    if (candidate->hid && candidate->connectable &&
        (!s_ble.saved_valid || saved_candidate_matches(candidate))) {
        connect_candidate(candidate);
    }
}

static void start_scan(void)
{
    if (!platform_ble_host_ready() || ble_gap_disc_active() ||
        ble_gap_adv_active()) {
        fail_connection(BLE_HS_CONN_HANDLE_NONE, BLE_HS_EBUSY,
                        "scan-radio-busy");
        return;
    }
    uint8_t own_addr_type = 0U;
    const esp_err_t own_result =
        platform_ble_host_own_addr_type(&own_addr_type);
    if (own_result != ESP_OK) {
        fail_connection(BLE_HS_CONN_HANDLE_NONE, (int)own_result,
                        "scan-own-address");
        return;
    }
    memset(s_ble.candidates, 0, sizeof(s_ble.candidates));
    const struct ble_gap_disc_params parameters = {
        .itvl = BLE_HID_SCAN_INTERVAL,
        .window = BLE_HID_SCAN_WINDOW,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0U,
        .passive = 0U,
        .filter_duplicates = 0U,
    };
    set_state(PLATFORM_GAMEPAD_BLE_SCANNING, 0);
    const int result = ble_gap_disc(
        own_addr_type, BLE_HID_SCAN_TIMEOUT_MS,
        &parameters, gap_event, NULL);
    if (result != 0) {
        fail_connection(BLE_HS_CONN_HANDLE_NONE, result, "scan-start");
    } else {
        ESP_LOGI(TAG, "BLE_GAMEPAD_SCAN state=started mode=%s timeout_ms=%u",
                 s_ble.saved_valid ? "saved-only" : "pair-new",
                 (unsigned)BLE_HID_SCAN_TIMEOUT_MS);
    }
}

static int service_discovered(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    const struct ble_gatt_svc *service, void *argument)
{
    (void)argument;
    if (!connection_matches(conn_handle) || error == NULL) {
        return 0;
    }
    if (error->status == 0U && service != NULL) {
        if (s_ble.service_start == 0U) {
            s_ble.service_start = service->start_handle;
            s_ble.service_end = service->end_handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE || s_ble.service_start == 0U ||
        s_ble.service_end <= s_ble.service_start) {
        fail_connection(conn_handle, error->status, "hid-service");
        return 0;
    }
    const int result = ble_gattc_disc_all_chrs(
        conn_handle, s_ble.service_start, s_ble.service_end,
        characteristic_discovered, NULL);
    if (result != 0) {
        fail_connection(conn_handle, result, "hid-characteristics-start");
    }
    return 0;
}

static int characteristic_discovered(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    const struct ble_gatt_chr *characteristic, void *argument)
{
    (void)argument;
    if (!connection_matches(conn_handle) || error == NULL) {
        return 0;
    }
    if (error->status == 0U && characteristic != NULL) {
        if (s_ble.last_report_characteristic >= 0) {
            ble_hid_report_characteristic_t *const previous =
                &s_ble.reports[(uint8_t)s_ble.last_report_characteristic];
            if (previous->end_handle == 0U &&
                characteristic->def_handle > previous->val_handle) {
                previous->end_handle =
                    (uint16_t)(characteristic->def_handle - 1U);
            }
            s_ble.last_report_characteristic = -1;
        }
        const uint16_t uuid = ble_uuid_u16(&characteristic->uuid.u);
        if (uuid == BLE_HID_REPORT_MAP_UUID &&
            s_ble.report_map_handle == 0U) {
            s_ble.report_map_handle = characteristic->val_handle;
        } else if (uuid == BLE_HID_PROTOCOL_MODE_UUID &&
                   s_ble.protocol_mode_handle == 0U) {
            s_ble.protocol_mode_handle = characteristic->val_handle;
            s_ble.protocol_mode_properties = characteristic->properties;
        } else if (uuid == BLE_HID_REPORT_UUID &&
                   s_ble.report_count <
                       BLE_HID_MAX_REPORT_CHARACTERISTICS) {
            ble_hid_report_characteristic_t *const report =
                &s_ble.reports[s_ble.report_count];
            *report = (ble_hid_report_characteristic_t){
                .def_handle = characteristic->def_handle,
                .val_handle = characteristic->val_handle,
                .properties = characteristic->properties,
            };
            s_ble.last_report_characteristic = (int8_t)s_ble.report_count;
            ++s_ble.report_count;
        }
        return 0;
    }
    if (s_ble.last_report_characteristic >= 0) {
        s_ble.reports[(uint8_t)s_ble.last_report_characteristic].end_handle =
            s_ble.service_end;
        s_ble.last_report_characteristic = -1;
    }
    if (error->status != BLE_HS_EDONE || s_ble.report_map_handle == 0U ||
        s_ble.report_count == 0U) {
        fail_connection(conn_handle, error->status,
                        "hid-characteristics");
        return 0;
    }
    s_ble.report_map_length = 0U;
    const int result = ble_gattc_read_long(
        conn_handle, s_ble.report_map_handle, 0U,
        report_map_read, NULL);
    if (result != 0) {
        fail_connection(conn_handle, result, "report-map-read-start");
    }
    return 0;
}

static int report_map_read(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attribute, void *argument)
{
    (void)argument;
    if (!connection_matches(conn_handle) || error == NULL) {
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        const gamepad_status_t parsed = gamepad_hid_parse_descriptor(
            s_ble.report_map, s_ble.report_map_length, &s_ble.layout);
        if (parsed != GAMEPAD_OK) {
            fail_connection(conn_handle, (int)parsed, "report-map-parse");
            return 0;
        }
        start_report_descriptor_discovery();
        return 0;
    }
    if (error->status != 0U || attribute == NULL || attribute->om == NULL ||
        (size_t)attribute->offset != s_ble.report_map_length) {
        fail_connection(conn_handle, error->status, "report-map-read");
        return 0;
    }
    const uint16_t length = OS_MBUF_PKTLEN(attribute->om);
    if (length == 0U ||
        s_ble.report_map_length + length > sizeof(s_ble.report_map)) {
        fail_connection(conn_handle, BLE_HS_EMSGSIZE, "report-map-bound");
        return BLE_HS_EMSGSIZE;
    }
    uint16_t copied = 0U;
    const size_t remaining =
        sizeof(s_ble.report_map) - s_ble.report_map_length;
    const int result = ble_hs_mbuf_to_flat(
        attribute->om, s_ble.report_map + s_ble.report_map_length,
        (uint16_t)remaining, &copied);
    if (result != 0 || copied != length) {
        fail_connection(conn_handle,
                        result != 0 ? result : BLE_HS_EBADDATA,
                        "report-map-copy");
        return result;
    }
    s_ble.report_map_length += copied;
    return 0;
}

static int descriptor_discovered(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    uint16_t characteristic_handle,
    const struct ble_gatt_dsc *descriptor, void *argument)
{
    (void)argument;
    if (!connection_matches(conn_handle) || error == NULL ||
        s_ble.descriptor_index >= s_ble.report_count) {
        return 0;
    }
    ble_hid_report_characteristic_t *const report =
        &s_ble.reports[s_ble.descriptor_index];
    if (characteristic_handle != report->val_handle) {
        fail_connection(conn_handle, BLE_HS_EBADDATA,
                        "descriptor-owner");
        return 0;
    }
    if (error->status == 0U && descriptor != NULL) {
        const uint16_t uuid = ble_uuid_u16(&descriptor->uuid.u);
        if (uuid == BLE_GATT_DSC_CLT_CFG_UUID16) {
            report->cccd_handle = descriptor->handle;
        } else if (uuid == BLE_HID_REPORT_REFERENCE_UUID) {
            report->reference_handle = descriptor->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail_connection(conn_handle, error->status, "report-descriptors");
        return 0;
    }
    ++s_ble.descriptor_index;
    start_report_descriptor_discovery();
    return 0;
}

static void start_report_descriptor_discovery(void)
{
    if (s_ble.descriptor_index >= s_ble.report_count) {
        s_ble.reference_index = 0U;
        start_report_reference_reads();
        return;
    }
    ble_hid_report_characteristic_t *const report =
        &s_ble.reports[s_ble.descriptor_index];
    if (report->end_handle <= report->val_handle) {
        fail_connection(s_ble.conn_handle, BLE_HS_EBADDATA,
                        "report-descriptor-range");
        return;
    }
    const int result = ble_gattc_disc_all_dscs(
        s_ble.conn_handle, report->val_handle, report->end_handle,
        descriptor_discovered, NULL);
    if (result != 0) {
        fail_connection(s_ble.conn_handle, result,
                        "report-descriptors-start");
    }
}

static int report_reference_read(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attribute, void *argument)
{
    ble_hid_report_characteristic_t *const report = argument;
    if (!connection_matches(conn_handle) || error == NULL || report == NULL ||
        error->status != 0U || attribute == NULL || attribute->om == NULL) {
        fail_connection(conn_handle,
                        error == NULL ? BLE_HS_EINVAL : error->status,
                        "report-reference-read");
        return 0;
    }
    uint8_t reference[2] = {0};
    uint16_t copied = 0U;
    const int result = ble_hs_mbuf_to_flat(
        attribute->om, reference, sizeof(reference), &copied);
    if (result != 0 || copied != sizeof(reference)) {
        fail_connection(conn_handle,
                        result != 0 ? result : BLE_HS_EBADDATA,
                        "report-reference-copy");
        return 0;
    }
    report->report_id = reference[0];
    report->report_type = reference[1];
    ++s_ble.reference_index;
    start_report_reference_reads();
    return 0;
}

static bool layout_has_report_id(uint8_t report_id)
{
    for (uint8_t index = 0U; index < s_ble.layout.report_count; ++index) {
        if (s_ble.layout.reports[index].report_id == report_id) {
            return true;
        }
    }
    return false;
}

static void finish_report_reference_reads(void)
{
    uint8_t input_reports = 0U;
    for (uint8_t index = 0U; index < s_ble.report_count; ++index) {
        const ble_hid_report_characteristic_t *const report =
            &s_ble.reports[index];
        if (report->report_type == BLE_HID_REPORT_TYPE_INPUT &&
            report->cccd_handle != 0U &&
            (report->properties & BLE_GATT_CHR_PROP_NOTIFY) != 0U &&
            layout_has_report_id(s_ble.layout.uses_report_ids != 0U
                                     ? report->report_id : 0U)) {
            ++input_reports;
        }
    }
    if (input_reports == 0U) {
        fail_connection(s_ble.conn_handle, BLE_HS_ENOTSUP,
                        "no-input-reports");
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.status.input_reports = input_reports;
    portEXIT_CRITICAL(&s_lock);
    s_ble.subscribe_index = 0U;
    if (s_ble.protocol_mode_handle == 0U) {
        start_subscriptions();
        return;
    }
    const uint8_t mode = BLE_HID_REPORT_PROTOCOL;
    int result = 0;
    if ((s_ble.protocol_mode_properties &
         BLE_GATT_CHR_PROP_WRITE_NO_RSP) != 0U) {
        result = ble_gattc_write_no_rsp_flat(
            s_ble.conn_handle, s_ble.protocol_mode_handle,
            &mode, sizeof(mode));
        if (result == 0) {
            start_subscriptions();
        }
    } else if ((s_ble.protocol_mode_properties &
                BLE_GATT_CHR_PROP_WRITE) != 0U) {
        result = ble_gattc_write_flat(
            s_ble.conn_handle, s_ble.protocol_mode_handle,
            &mode, sizeof(mode), protocol_mode_complete, NULL);
    } else {
        /* HOGP defaults to Report Protocol mode on a new connection. */
        start_subscriptions();
        return;
    }
    if (result != 0) {
        fail_connection(s_ble.conn_handle, result,
                        "protocol-mode-start");
    }
}

static void start_report_reference_reads(void)
{
    while (s_ble.reference_index < s_ble.report_count) {
        ble_hid_report_characteristic_t *const report =
            &s_ble.reports[s_ble.reference_index];
        if (report->reference_handle == 0U) {
            ++s_ble.reference_index;
            continue;
        }
        const int result = ble_gattc_read(
            s_ble.conn_handle, report->reference_handle,
            report_reference_read, report);
        if (result != 0) {
            fail_connection(s_ble.conn_handle, result,
                            "report-reference-start");
        }
        return;
    }
    finish_report_reference_reads();
}

static int protocol_mode_complete(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attribute, void *argument)
{
    (void)attribute;
    (void)argument;
    if (!connection_matches(conn_handle)) {
        return 0;
    }
    if (error == NULL || error->status != 0U) {
        fail_connection(conn_handle,
                        error == NULL ? BLE_HS_EINVAL : error->status,
                        "protocol-mode");
        return 0;
    }
    start_subscriptions();
    return 0;
}

static int subscription_complete(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attribute, void *argument)
{
    (void)attribute;
    ble_hid_report_characteristic_t *const report = argument;
    if (!connection_matches(conn_handle) || report == NULL) {
        return 0;
    }
    if (error == NULL || error->status != 0U) {
        fail_connection(conn_handle,
                        error == NULL ? BLE_HS_EINVAL : error->status,
                        "subscribe");
        return 0;
    }
    report->subscribed = true;
    ++s_ble.subscribe_index;
    start_subscriptions();
    return 0;
}

static void publish_ready(void)
{
    uint8_t digest[PLATFORM_GAMEPAD_DESCRIPTOR_SHA256_BYTES];
    if (mbedtls_sha256(s_ble.report_map, s_ble.report_map_length,
                       digest, 0) != 0) {
        fail_connection(s_ble.conn_handle, BLE_HS_EUNKNOWN,
                        "report-map-hash");
        return;
    }
    const uint32_t capabilities = gamepad_hid_capabilities(&s_ble.layout);
    const platform_gamepad_identity_t identity = {
        .transport = PLATFORM_GAMEPAD_TRANSPORT_BLE_HID,
        .descriptor_sha256 = {0},
    };
    platform_gamepad_identity_t complete_identity = identity;
    memcpy(complete_identity.descriptor_sha256, digest, sizeof(digest));

    portENTER_CRITICAL(&s_model_lock);
    const gamepad_status_t model_result = platform_gamepad_model_connect(
        &s_ble.model, &complete_identity, capabilities, monotonic_us(),
        &s_ble.session);
    portEXIT_CRITICAL(&s_model_lock);
    if (model_result != GAMEPAD_OK) {
        fail_connection(s_ble.conn_handle, (int)model_result,
                        "snapshot-connect");
        return;
    }

    struct ble_gap_conn_desc description;
    const int found = ble_gap_conn_find(s_ble.conn_handle, &description);
    if (found != 0 || !description.sec_state.encrypted) {
        fail_connection(s_ble.conn_handle,
                        found != 0 ? found : BLE_HS_EAUTHEN,
                        "ready-security");
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.peer_id_address = description.peer_id_addr;
    s_ble.status.connected = true;
    s_ble.status.encrypted = true;
    s_ble.status.bonded = description.sec_state.bonded != 0U;
    portEXIT_CRITICAL(&s_lock);
    if (description.sec_state.bonded != 0U) {
        const esp_err_t saved = save_pair_record(
            &description.peer_id_addr, s_ble.status.name);
        if (saved != ESP_OK) {
            ESP_LOGW(TAG, "BLE_GAMEPAD_BOND_SAVE result=%s",
                     esp_err_to_name(saved));
        }
    }
    set_state(PLATFORM_GAMEPAD_BLE_READY, 0);
    ESP_LOGI(TAG,
             "BLE_GAMEPAD_READY name=%s reports=%u mtu=%u "
             "descriptor_bytes=%u capabilities=0x%08" PRIx32,
             s_ble.status.name, (unsigned)s_ble.status.input_reports,
             (unsigned)s_ble.status.att_mtu,
             (unsigned)s_ble.report_map_length, capabilities);
}

static void start_subscriptions(void)
{
    set_state(PLATFORM_GAMEPAD_BLE_SUBSCRIBING, 0);
    while (s_ble.subscribe_index < s_ble.report_count) {
        ble_hid_report_characteristic_t *const report =
            &s_ble.reports[s_ble.subscribe_index];
        const uint8_t layout_id = s_ble.layout.uses_report_ids != 0U
            ? report->report_id : 0U;
        if (report->report_type != BLE_HID_REPORT_TYPE_INPUT ||
            report->cccd_handle == 0U ||
            (report->properties & BLE_GATT_CHR_PROP_NOTIFY) == 0U ||
            !layout_has_report_id(layout_id)) {
            ++s_ble.subscribe_index;
            continue;
        }
        const uint8_t notifications_on[2] = {1U, 0U};
        const int result = ble_gattc_write_flat(
            s_ble.conn_handle, report->cccd_handle,
            notifications_on, sizeof(notifications_on),
            subscription_complete, report);
        if (result != 0) {
            fail_connection(s_ble.conn_handle, result, "subscribe-start");
        }
        return;
    }
    publish_ready();
}

static int mtu_exchanged(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    uint16_t mtu, void *argument)
{
    (void)argument;
    if (!connection_matches(conn_handle)) {
        return 0;
    }
    if (error != NULL && error->status != 0U) {
        mtu = BLE_ATT_MTU_DFLT;
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.status.att_mtu = mtu >= BLE_ATT_MTU_DFLT
        ? mtu : BLE_ATT_MTU_DFLT;
    portEXIT_CRITICAL(&s_lock);
    set_state(PLATFORM_GAMEPAD_BLE_DISCOVERING, 0);
    const int result = ble_gattc_disc_svc_by_uuid(
        conn_handle, BLE_UUID16_DECLARE(BLE_HID_SERVICE_UUID),
        service_discovered, NULL);
    if (result != 0) {
        fail_connection(conn_handle, result, "hid-service-start");
    }
    return 0;
}

static void connection_encrypted(uint16_t conn_handle)
{
    struct ble_gap_conn_desc description;
    const int found = ble_gap_conn_find(conn_handle, &description);
    if (found != 0 || !description.sec_state.encrypted) {
        fail_connection(conn_handle,
                        found != 0 ? found : BLE_HS_EAUTHEN,
                        "encryption");
        return;
    }
    if (!saved_identity_matches(&description.peer_id_addr)) {
        fail_connection(conn_handle, BLE_HS_EAUTHEN,
                        "saved-peer-identity");
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.status.encrypted = true;
    s_ble.status.bonded = description.sec_state.bonded != 0U;
    s_ble.peer_id_address = description.peer_id_addr;
    portEXIT_CRITICAL(&s_lock);
    const int result = ble_gattc_exchange_mtu(
        conn_handle, mtu_exchanged, NULL);
    if (result != 0) {
        (void)mtu_exchanged(conn_handle, NULL, BLE_ATT_MTU_DFLT, NULL);
    }
}

static void connected(uint16_t conn_handle)
{
    struct ble_gap_conn_desc description;
    const int found = ble_gap_conn_find(conn_handle, &description);
    if (found != 0 || description.role != BLE_GAP_ROLE_MASTER ||
        !s_ble.selected_valid ||
        !addresses_equal(&description.peer_ota_addr,
                         &s_ble.selected_address)) {
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.connect_pending = false;
    s_ble.conn_handle = conn_handle;
    s_ble.peer_id_address = description.peer_id_addr;
    s_ble.status.connected = true;
    s_ble.status.encrypted = description.sec_state.encrypted != 0U;
    s_ble.status.bonded = description.sec_state.bonded != 0U;
    portEXIT_CRITICAL(&s_lock);
    set_state(PLATFORM_GAMEPAD_BLE_SECURING, 0);
    if (description.sec_state.encrypted) {
        connection_encrypted(conn_handle);
    } else {
        const int security = ble_gap_security_initiate(conn_handle);
        if (security != 0) {
            fail_connection(conn_handle, security, "security-start");
        }
    }
}

static const ble_hid_report_characteristic_t *report_for_handle(
    uint16_t value_handle)
{
    for (uint8_t index = 0U; index < s_ble.report_count; ++index) {
        if (s_ble.reports[index].val_handle == value_handle &&
            s_ble.reports[index].subscribed) {
            return &s_ble.reports[index];
        }
    }
    return NULL;
}

static void consume_input_report(
    uint16_t value_handle, struct os_mbuf *message)
{
    const ble_hid_report_characteristic_t *const report =
        report_for_handle(value_handle);
    if (report == NULL || message == NULL) {
        count_drop(BLE_HS_EBADDATA);
        return;
    }
    const uint16_t length = OS_MBUF_PKTLEN(message);
    const uint8_t report_id = s_ble.layout.uses_report_ids != 0U
        ? report->report_id : 0U;
    const gamepad_hid_report_t *expected = NULL;
    for (uint8_t index = 0U; index < s_ble.layout.report_count; ++index) {
        if (s_ble.layout.reports[index].report_id == report_id) {
            expected = &s_ble.layout.reports[index];
            break;
        }
    }
    if (expected == NULL) {
        count_drop(GAMEPAD_ERR_REPORT_ID);
        return;
    }
    const bool includes_id = s_ble.layout.uses_report_ids != 0U &&
        length == (uint16_t)(expected->payload_bytes + 1U);
    const bool excludes_id = length == expected->payload_bytes;
    if ((!includes_id && !excludes_id) ||
        (size_t)length + (excludes_id && s_ble.layout.uses_report_ids != 0U
                             ? 1U : 0U) > sizeof(s_ble.decode_buffer)) {
        count_drop(GAMEPAD_ERR_REPORT_SIZE);
        return;
    }
    size_t offset = 0U;
    if (excludes_id && s_ble.layout.uses_report_ids != 0U) {
        s_ble.decode_buffer[0] = report_id;
        offset = 1U;
    }
    uint16_t copied = 0U;
    const size_t capacity = sizeof(s_ble.decode_buffer) - offset;
    const int copied_result = ble_hs_mbuf_to_flat(
        message, s_ble.decode_buffer + offset,
        (uint16_t)capacity, &copied);
    if (copied_result != 0 || copied != length ||
        (includes_id && s_ble.decode_buffer[0] != report_id)) {
        count_drop(copied_result != 0 ? copied_result : BLE_HS_EBADDATA);
        return;
    }

    platform_gamepad_snapshot_t snapshot;
    portENTER_CRITICAL(&s_model_lock);
    const gamepad_status_t copied_model =
        platform_gamepad_model_copy(&s_ble.model, &snapshot);
    portEXIT_CRITICAL(&s_model_lock);
    if (copied_model != GAMEPAD_OK) {
        count_drop((int)copied_model);
        return;
    }
    gamepad_state_t decoded = snapshot.state;
    const gamepad_status_t decoded_result = gamepad_hid_decode_report(
        &s_ble.layout, s_ble.decode_buffer, offset + copied,
        monotonic_us(), &decoded);
    if (decoded_result != GAMEPAD_OK) {
        count_drop((int)decoded_result);
        return;
    }
    portENTER_CRITICAL(&s_model_lock);
    const gamepad_status_t committed = platform_gamepad_model_commit_report(
        &s_ble.model, s_ble.session, &decoded);
    portEXIT_CRITICAL(&s_model_lock);
    if (committed != GAMEPAD_OK) {
        count_drop((int)committed);
        return;
    }
    portENTER_CRITICAL(&s_lock);
    if (s_ble.status.reports_received != UINT32_MAX) {
        ++s_ble.status.reports_received;
    }
    portEXIT_CRITICAL(&s_lock);
}

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    if (event == NULL) {
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        observe_advertisement(&event->disc);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_ble.status.state == PLATFORM_GAMEPAD_BLE_SCANNING) {
            s_ble.connect_pending = false;
            set_state(s_ble.saved_valid
                          ? PLATFORM_GAMEPAD_BLE_STANDBY
                          : PLATFORM_GAMEPAD_BLE_OFF,
                      event->disc_complete.reason);
        }
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            connected(event->connect.conn_handle);
        } else {
            clear_connection_state();
            set_state(s_ble.saved_valid
                          ? PLATFORM_GAMEPAD_BLE_STANDBY
                          : PLATFORM_GAMEPAD_BLE_ERROR,
                      event->connect.status);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        if (connection_matches(event->disconnect.conn.conn_handle)) {
            ESP_LOGI(TAG, "BLE_GAMEPAD_DISCONNECTED reason=%d input=neutral",
                     event->disconnect.reason);
            clear_connection_state();
            set_state(s_ble.saved_valid
                          ? PLATFORM_GAMEPAD_BLE_STANDBY
                          : PLATFORM_GAMEPAD_BLE_OFF,
                      event->disconnect.reason);
        }
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            connection_encrypted(event->enc_change.conn_handle);
        } else {
            fail_connection(event->enc_change.conn_handle,
                            event->enc_change.status, "encryption-change");
        }
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (connection_matches(event->notify_rx.conn_handle)) {
            consume_input_report(event->notify_rx.attr_handle,
                                 event->notify_rx.om);
        }
        break;
    case BLE_GAP_EVENT_MTU:
        if (connection_matches(event->mtu.conn_handle)) {
            portENTER_CRITICAL(&s_lock);
            s_ble.status.att_mtu = event->mtu.value;
            portEXIT_CRITICAL(&s_lock);
        }
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc description;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle,
                              &description) == 0) {
            (void)ble_store_util_delete_peer(&description.peer_id_addr);
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    default:
        break;
    }
    return 0;
}

static void host_sync(void *context)
{
    (void)context;
    portENTER_CRITICAL(&s_lock);
    s_ble.status.host_ready = true;
    const bool pending = s_ble.connect_pending;
    const bool forget_pending = s_ble.forget_pending;
    portEXIT_CRITICAL(&s_lock);
    if (forget_pending) {
        (void)complete_forget();
    } else if (pending) {
        start_scan();
    }
}

static void host_reset(int reason, void *context)
{
    (void)context;
    clear_connection_state();
    portENTER_CRITICAL(&s_lock);
    s_ble.status.host_ready = false;
    portEXIT_CRITICAL(&s_lock);
    set_state(PLATFORM_GAMEPAD_BLE_ERROR, reason);
}

esp_err_t platform_gamepad_ble_prepare(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool initialized = s_ble.initialized;
    if (!initialized) {
        s_ble.status.state = PLATFORM_GAMEPAD_BLE_OFF;
        s_ble.status.supported = true;
        s_ble.status.att_mtu = BLE_ATT_MTU_DFLT;
        s_ble.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_ble.last_report_characteristic = -1;
        s_ble.initialized = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (!initialized) {
        portENTER_CRITICAL(&s_model_lock);
        platform_gamepad_model_init(&s_ble.model);
        portEXIT_CRITICAL(&s_model_lock);
        gamepad_hid_layout_init(&s_ble.layout);
        load_pair_record();
    }
    esp_err_t result =
        platform_ble_host_register_client(&BLE_HID_HOST_CLIENT);
    if (result == ESP_OK) {
        result = platform_gamepad_register_provider(
            PLATFORM_GAMEPAD_TRANSPORT_BLE_HID,
            platform_gamepad_ble_get_snapshot);
    }
    return result;
}

esp_err_t platform_gamepad_ble_connect_or_pair(void)
{
    const esp_err_t prepared = platform_gamepad_ble_prepare();
    if (prepared != ESP_OK) {
        return prepared;
    }
    portENTER_CRITICAL(&s_lock);
    if (s_ble.status.connected || s_ble.connect_pending ||
        s_ble.status.state == PLATFORM_GAMEPAD_BLE_SCANNING ||
        s_ble.status.state == PLATFORM_GAMEPAD_BLE_CONNECTING) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_ble.connect_pending = true;
    const bool host_ready = s_ble.status.host_ready;
    portEXIT_CRITICAL(&s_lock);
    if (host_ready) {
        start_scan();
        return s_ble.status.state == PLATFORM_GAMEPAD_BLE_ERROR
            ? ESP_FAIL : ESP_OK;
    }
    set_state(PLATFORM_GAMEPAD_BLE_STARTING_HOST, 0);
    const esp_err_t result = platform_ble_host_start();
    if (result != ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_ble.connect_pending = false;
        portEXIT_CRITICAL(&s_lock);
        set_state(PLATFORM_GAMEPAD_BLE_ERROR, (int)result);
    }
    return result;
}

void platform_gamepad_ble_cancel(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool scanning =
        s_ble.status.state == PLATFORM_GAMEPAD_BLE_SCANNING;
    s_ble.connect_pending = false;
    portEXIT_CRITICAL(&s_lock);
    if (scanning) {
        (void)ble_gap_disc_cancel();
        set_state(s_ble.saved_valid
                      ? PLATFORM_GAMEPAD_BLE_STANDBY
                      : PLATFORM_GAMEPAD_BLE_OFF,
                  0);
    }
}

void platform_gamepad_ble_disconnect(void)
{
    platform_gamepad_ble_cancel();
    portENTER_CRITICAL(&s_lock);
    const uint16_t conn_handle = s_ble.conn_handle;
    portEXIT_CRITICAL(&s_lock);
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

esp_err_t platform_gamepad_ble_forget(void)
{
    const esp_err_t prepared = platform_gamepad_ble_prepare();
    if (prepared != ESP_OK) {
        return prepared;
    }
    platform_gamepad_ble_cancel();
    portENTER_CRITICAL(&s_lock);
    const uint16_t conn_handle = s_ble.conn_handle;
    const bool have_peer = s_ble.saved_valid;
    s_ble.forget_pending = have_peer;
    portEXIT_CRITICAL(&s_lock);
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    if (!have_peer) {
        set_state(PLATFORM_GAMEPAD_BLE_OFF, 0);
        return clear_pair_record();
    }
    if (platform_ble_host_ready()) {
        return complete_forget();
    }
    set_state(PLATFORM_GAMEPAD_BLE_STARTING_HOST, 0);
    const esp_err_t start_result = platform_ble_host_start();
    if (start_result != ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_ble.forget_pending = false;
        portEXIT_CRITICAL(&s_lock);
        set_state(PLATFORM_GAMEPAD_BLE_ERROR, (int)start_result);
    }
    return start_result;
}

esp_err_t platform_gamepad_ble_get_snapshot(
    platform_gamepad_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_model_lock);
    const gamepad_status_t result =
        platform_gamepad_model_copy(&s_ble.model, snapshot);
    portEXIT_CRITICAL(&s_model_lock);
    return result == GAMEPAD_OK ? ESP_OK : ESP_ERR_INVALID_STATE;
}

platform_gamepad_ble_status_t platform_gamepad_ble_status(void)
{
    portENTER_CRITICAL(&s_lock);
    const platform_gamepad_ble_status_t status = s_ble.status;
    portEXIT_CRITICAL(&s_lock);
    return status;
}
