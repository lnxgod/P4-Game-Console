// SPDX-License-Identifier: MIT

#include "platform/multiplayer_ble.h"

#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#pragma GCC diagnostic pop

#include "p4/multiplayer_ble.h"
#include "platform/ble_host.h"

enum {
    P4_BLE_RX_QUEUE_DEPTH = 32,
    P4_BLE_TX_QUEUE_DEPTH = 32,
    P4_BLE_TX_DRAIN_BUDGET = 8,
    P4_BLE_CONNECT_TIMEOUT_MS = 10000,
    P4_BLE_LOBBY_STALE_MS = 4000,
    P4_BLE_HOST_COLLISION_SCAN_MS = 600,
    P4_BLE_HOST_COLLISION_ADV_BASE_MS = 900,
    P4_BLE_HOST_COLLISION_ADV_JITTER_MS = 900,
    P4_BLE_ADV_INTERVAL_MIN = 160, /* 100 ms. */
    P4_BLE_ADV_INTERVAL_MAX = 240, /* 150 ms. */
    P4_BLE_FAST_INTERVAL_MIN = 6,  /* 7.5 ms. */
    P4_BLE_FAST_INTERVAL_MAX = 12, /* 15 ms. */
    P4_BLE_SUPERVISION_TIMEOUT = 400, /* 4 seconds. */
};

typedef struct {
    uint64_t route_id;
    uint16_t length;
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
} p4_ble_rx_entry_t;

typedef struct {
    platform_multiplayer_ble_lobby_t lobby;
    ble_addr_t address;
    uint64_t last_seen_ms;
    bool occupied;
} p4_ble_lobby_entry_t;

typedef struct {
    platform_multiplayer_ble_status_t status;
    platform_multiplayer_ble_frame_handler_t handler;
    void *handler_context;
    QueueHandle_t rx_queue;
    QueueHandle_t tx_queue;
    uint8_t own_addr_type;
    uint8_t peer_address[6];
    uint8_t selected_host_address[6];
    uint8_t selected_host_address_type;
    bool selected_host_valid;
    uint16_t conn_handle;
    uint16_t local_value_handle;
    uint16_t peer_value_handle;
    uint16_t peer_cccd_handle;
    uint16_t peer_service_start;
    uint16_t peer_service_end;
    uint16_t next_frame_id;
    p4_ble_lobby_entry_t lobbies[PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES];
    p4_mp_ble_reassembler_t reassembler;
    uint64_t host_collision_deadline_ms;
    uint32_t host_collision_round;
    bool host_collision_scanning;
} p4_ble_state_t;

static const char *const TAG = "p4_ble_game";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static p4_ble_state_t s_ble = {
    .status = {
        .state = PLATFORM_MULTIPLAYER_BLE_OFF,
        .att_mtu = BLE_ATT_MTU_DFLT,
    },
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
    .next_frame_id = 1U,
};

/* UUIDs are stored least-significant byte first by NimBLE. */
static const ble_uuid128_t P4_BLE_SERVICE_UUID = BLE_UUID128_INIT(
    0x01, 0x00, 0x50, 0x4d, 0x34, 0x50, 0x9e, 0x9c,
    0x0d, 0x4a, 0x44, 0x6f, 0x20, 0x9f, 0x0d, 0x7b);
static const ble_uuid128_t P4_BLE_CHARACTERISTIC_UUID = BLE_UUID128_INIT(
    0x02, 0x00, 0x50, 0x4d, 0x34, 0x50, 0x9e, 0x9c,
    0x0d, 0x4a, 0x44, 0x6f, 0x20, 0x9f, 0x0d, 0x7b);
_Static_assert(
    3U + 2U + 16U + P4_MP_BLE_LOBBY_BEACON_BYTES == 31U,
    "flags plus P4 room service data must fit one legacy advertisement");

static int gap_event(struct ble_gap_event *event, void *argument);
static int gatt_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *context,
    void *argument);
static void host_reset(int reason, void *context);
static void host_sync(void *context);
static esp_err_t send_datagram_now(
    const uint8_t *datagram,
    size_t datagram_length);

static const struct ble_gatt_chr_def P4_BLE_CHARACTERISTICS[] = {
    {
        .uuid = &P4_BLE_CHARACTERISTIC_UUID.u,
        .access_cb = gatt_access,
        .flags = BLE_GATT_CHR_F_WRITE |
                 BLE_GATT_CHR_F_WRITE_NO_RSP |
                 BLE_GATT_CHR_F_WRITE_ENC |
                 BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_ble.local_value_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def P4_BLE_SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &P4_BLE_SERVICE_UUID.u,
        .characteristics = P4_BLE_CHARACTERISTICS,
    },
    {0},
};

static const platform_ble_host_client_t P4_BLE_HOST_CLIENT = {
    .services = P4_BLE_SERVICES,
    .on_sync = host_sync,
    .on_reset = host_reset,
};

static void set_state(platform_multiplayer_ble_state_t state, int error)
{
    portENTER_CRITICAL(&s_lock);
    s_ble.status.state = state;
    s_ble.status.last_ble_error = error;
    s_ble.status.ready = state == PLATFORM_MULTIPLAYER_BLE_READY;
    portEXIT_CRITICAL(&s_lock);
}

static bool is_enabled(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool enabled = s_ble.status.enabled;
    portEXIT_CRITICAL(&s_lock);
    return enabled;
}

static uint64_t monotonic_ms(void)
{
    const int64_t now_us = esp_timer_get_time();
    return now_us <= 0 ? 0U : (uint64_t)now_us / UINT64_C(1000);
}

static platform_multiplayer_ble_lobby_mode_t lobby_mode(void)
{
    portENTER_CRITICAL(&s_lock);
    const platform_multiplayer_ble_lobby_mode_t mode =
        s_ble.status.lobby_mode;
    portEXIT_CRITICAL(&s_lock);
    return mode;
}

static uint64_t host_collision_advertise_ms(
    uint32_t session_id, uint32_t round)
{
    uint32_t mixed = session_id ^ (round * UINT32_C(2654435761));
    mixed ^= mixed >> 16U;
    return P4_BLE_HOST_COLLISION_ADV_BASE_MS +
        mixed % P4_BLE_HOST_COLLISION_ADV_JITTER_MS;
}

static void refresh_lobby_count_locked(void)
{
    uint8_t count = 0U;
    for (size_t index = 0U;
         index < PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES; ++index) {
        if (s_ble.lobbies[index].occupied) {
            ++count;
        }
    }
    s_ble.status.discovered_lobbies = count;
}

static void clear_lobbies_locked(void)
{
    memset(s_ble.lobbies, 0, sizeof(s_ble.lobbies));
    s_ble.status.discovered_lobbies = 0U;
}

static void prune_lobbies_locked(uint64_t now_ms)
{
    for (size_t index = 0U;
         index < PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES; ++index) {
        p4_ble_lobby_entry_t *const entry = &s_ble.lobbies[index];
        if (entry->occupied && now_ms >= entry->last_seen_ms &&
            now_ms - entry->last_seen_ms > P4_BLE_LOBBY_STALE_MS) {
            *entry = (p4_ble_lobby_entry_t){0};
        }
    }
    refresh_lobby_count_locked();
}

static void count_drop(int error)
{
    portENTER_CRITICAL(&s_lock);
    if (s_ble.status.dropped_frames != UINT32_MAX) {
        ++s_ble.status.dropped_frames;
    }
    s_ble.status.last_ble_error = error;
    portEXIT_CRITICAL(&s_lock);
}

static void clear_rx_queue(void)
{
    if (s_ble.rx_queue != NULL) {
        (void)xQueueReset(s_ble.rx_queue);
    }
}

static void clear_tx_queue(void)
{
    if (s_ble.tx_queue != NULL) {
        (void)xQueueReset(s_ble.tx_queue);
    }
}

static void clear_transport_queues(void)
{
    clear_rx_queue();
    clear_tx_queue();
}

static void clear_connection_state(void)
{
    portENTER_CRITICAL(&s_lock);
    s_ble.status.connected = false;
    s_ble.status.encrypted = false;
    s_ble.status.central = false;
    s_ble.status.subscribed = false;
    s_ble.status.ready = false;
    s_ble.status.route_id = 0U;
    s_ble.status.att_mtu = BLE_ATT_MTU_DFLT;
    s_ble.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_ble.peer_value_handle = 0U;
    s_ble.peer_cccd_handle = 0U;
    s_ble.peer_service_start = 0U;
    s_ble.peer_service_end = 0U;
    s_ble.host_collision_deadline_ms = 0U;
    s_ble.host_collision_round = 0U;
    s_ble.host_collision_scanning = false;
    memset(s_ble.peer_address, 0, sizeof(s_ble.peer_address));
    portEXIT_CRITICAL(&s_lock);
    p4_mp_ble_reassembler_init(&s_ble.reassembler);
    clear_transport_queues();
}

static bool connection_matches(uint16_t conn_handle)
{
    portENTER_CRITICAL(&s_lock);
    const bool matches = s_ble.status.connected &&
        s_ble.conn_handle == conn_handle;
    portEXIT_CRITICAL(&s_lock);
    return matches;
}

static void queue_datagram(const uint8_t *datagram, size_t datagram_length)
{
    if (datagram == NULL || datagram_length > P4_MP_MAX_DATAGRAM_BYTES ||
        datagram_length > UINT16_MAX || s_ble.rx_queue == NULL) {
        count_drop(BLE_HS_EINVAL);
        return;
    }
    p4_ble_rx_entry_t entry = {
        .length = (uint16_t)datagram_length,
    };
    portENTER_CRITICAL(&s_lock);
    entry.route_id = s_ble.status.route_id;
    portEXIT_CRITICAL(&s_lock);
    if (entry.route_id == 0U) {
        count_drop(BLE_HS_ENOTCONN);
        return;
    }
    memcpy(entry.datagram, datagram, datagram_length);
    if (xQueueSend(s_ble.rx_queue, &entry, 0U) != pdTRUE) {
        count_drop(BLE_HS_ENOMEM);
        return;
    }
    portENTER_CRITICAL(&s_lock);
    if (s_ble.status.rx_frames != UINT32_MAX) {
        ++s_ble.status.rx_frames;
    }
    const uint32_t bytes = datagram_length > UINT32_MAX
        ? UINT32_MAX : (uint32_t)datagram_length;
    s_ble.status.rx_bytes = UINT32_MAX - s_ble.status.rx_bytes < bytes
        ? UINT32_MAX : s_ble.status.rx_bytes + bytes;
    portEXIT_CRITICAL(&s_lock);
}

static void consume_fragment(const uint8_t *fragment, size_t fragment_length)
{
    const uint8_t *datagram = NULL;
    size_t datagram_length = 0U;
    const p4_mp_ble_fragment_result_t result =
        p4_mp_ble_reassembler_consume(
            &s_ble.reassembler, fragment, fragment_length,
            &datagram, &datagram_length);
    if (result == P4_MP_BLE_FRAGMENT_DATAGRAM_READY) {
        queue_datagram(datagram, datagram_length);
    } else if (result == P4_MP_BLE_FRAGMENT_DROPPED ||
               result == P4_MP_BLE_FRAGMENT_INVALID_ARGUMENT) {
        count_drop(BLE_HS_EBADDATA);
    }
}

static int gatt_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *context,
    void *argument)
{
    (void)argument;
    if (context == NULL ||
        context->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        attr_handle != s_ble.local_value_handle ||
        !connection_matches(conn_handle)) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    const uint16_t length = OS_MBUF_PKTLEN(context->om);
    if (length <= P4_MP_BLE_FRAGMENT_HEADER_BYTES ||
        length > P4_MP_BLE_MAX_FRAGMENT_BYTES) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint8_t fragment[P4_MP_BLE_MAX_FRAGMENT_BYTES];
    uint16_t copied = 0U;
    const int result = ble_hs_mbuf_to_flat(
        context->om, fragment, sizeof(fragment), &copied);
    if (result != 0 || copied != length) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    consume_fragment(fragment, copied);
    return 0;
}

static int start_advertising(void)
{
    if (!is_enabled() || lobby_mode() != PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST ||
        ble_gap_adv_active()) {
        return 0;
    }
    p4_mp_ble_lobby_beacon_t beacon;
    portENTER_CRITICAL(&s_lock);
    beacon = (p4_mp_ble_lobby_beacon_t){
        .session_id = s_ble.status.lobby_session_id,
        .game_token = s_ble.status.lobby_game_token,
        .players_present = 1U,
        .player_capacity = 2U,
    };
    portEXIT_CRITICAL(&s_lock);
    uint8_t service_data[16U + P4_MP_BLE_LOBBY_BEACON_BYTES];
    memcpy(service_data, P4_BLE_SERVICE_UUID.value, 16U);
    if (p4_mp_ble_lobby_beacon_encode(
            &beacon, service_data + 16U) != P4_MP_OK) {
        return BLE_HS_EINVAL;
    }
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.svc_data_uuid128 = service_data;
    fields.svc_data_uuid128_len = (uint8_t)sizeof(service_data);
    int result = ble_gap_adv_set_fields(&fields);
    if (result != 0) {
        return result;
    }
    struct ble_gap_adv_params parameters = {0};
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    parameters.itvl_min = P4_BLE_ADV_INTERVAL_MIN;
    parameters.itvl_max = P4_BLE_ADV_INTERVAL_MAX;
    result = ble_gap_adv_start(
        s_ble.own_addr_type, NULL, BLE_HS_FOREVER,
        &parameters, gap_event, NULL);
    return result == BLE_HS_EALREADY ? 0 : result;
}

static int start_scanning(void)
{
    if (!is_enabled() ||
        lobby_mode() != PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER ||
        ble_gap_disc_active()) {
        return 0;
    }
    struct ble_gap_disc_params parameters = {0};
    parameters.itvl = 48U;
    parameters.window = 24U;
    parameters.passive = 1U;
    /* Repeated observations refresh bounded expiry and RSSI. */
    parameters.filter_duplicates = 0U;
    const int result = ble_gap_disc(
        s_ble.own_addr_type, BLE_HS_FOREVER,
        &parameters, gap_event, NULL);
    return result == BLE_HS_EALREADY ? 0 : result;
}

static int start_host_collision_scan(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool allowed = s_ble.status.enabled &&
        s_ble.status.lobby_mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST &&
        s_ble.host_collision_scanning && !s_ble.status.connected;
    portEXIT_CRITICAL(&s_lock);
    if (!allowed || ble_gap_disc_active()) {
        return 0;
    }
    struct ble_gap_disc_params parameters = {0};
    parameters.itvl = 48U;
    parameters.window = 24U;
    parameters.passive = 1U;
    parameters.filter_duplicates = 0U;
    const int result = ble_gap_disc(
        s_ble.own_addr_type, BLE_HS_FOREVER,
        &parameters, gap_event, NULL);
    return result == BLE_HS_EALREADY ? 0 : result;
}

static void start_discovery(void)
{
    if (!is_enabled()) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    const bool connected = s_ble.status.connected;
    const bool host_collision_scanning =
        s_ble.host_collision_scanning;
    portEXIT_CRITICAL(&s_lock);
    if (connected) {
        return;
    }
    const platform_multiplayer_ble_lobby_mode_t mode = lobby_mode();
    int result = 0;
    if (mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST) {
        if (host_collision_scanning) {
            if (ble_gap_adv_active()) {
                (void)ble_gap_adv_stop();
                return;
            }
            result = start_host_collision_scan();
        } else {
            if (ble_gap_disc_active()) {
                (void)ble_gap_disc_cancel();
                return;
            }
            result = start_advertising();
        }
    } else if (mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER) {
        (void)ble_gap_adv_stop();
        result = start_scanning();
    } else {
        (void)ble_gap_disc_cancel();
        (void)ble_gap_adv_stop();
        set_state(PLATFORM_MULTIPLAYER_BLE_STANDBY, 0);
        return;
    }
    if (result != 0) {
        const int error = result;
        ESP_LOGE(TAG, "discovery start failed rc=%d", error);
        set_state(PLATFORM_MULTIPLAYER_BLE_ERROR, error);
        return;
    }
    set_state(PLATFORM_MULTIPLAYER_BLE_DISCOVERING, 0);
}

static bool decode_lobby_advertisement(
    const struct ble_gap_disc_desc *discovery,
    p4_mp_ble_lobby_beacon_t *beacon_out)
{
    if (discovery == NULL || beacon_out == NULL ||
        (discovery->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
         discovery->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)) {
        return false;
    }
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(
            &fields, discovery->data, discovery->length_data) != 0) {
        return false;
    }
    const size_t expected = 16U + P4_MP_BLE_LOBBY_BEACON_BYTES;
    if (fields.svc_data_uuid128 != NULL &&
        fields.svc_data_uuid128_len == expected &&
        memcmp(fields.svc_data_uuid128,
               P4_BLE_SERVICE_UUID.value, 16U) == 0) {
        return p4_mp_ble_lobby_beacon_decode(
                   fields.svc_data_uuid128 + 16U,
                   P4_MP_BLE_LOBBY_BEACON_BYTES,
                   beacon_out) == P4_MP_OK;
    }
    bool p4_service = false;
    for (size_t index = 0U; index < fields.num_uuids128; ++index) {
        if (ble_uuid_cmp(&fields.uuids128[index].u,
                        &P4_BLE_SERVICE_UUID.u) == 0) {
            p4_service = true;
            break;
        }
    }
    if (!p4_service) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    const uint16_t game_token = s_ble.status.lobby_game_token;
    portEXIT_CRITICAL(&s_lock);
    if (game_token == 0U) {
        return false;
    }
    *beacon_out = (p4_mp_ble_lobby_beacon_t){
        .session_id = P4_MP_BLE_UUID_ONLY_SESSION_ID,
        .game_token = game_token,
        .players_present = 1U,
        .player_capacity = 2U,
    };
    return true;
}

static void observe_lobby(const struct ble_gap_disc_desc *discovery)
{
    p4_mp_ble_lobby_beacon_t beacon;
    if (!is_enabled() ||
        !decode_lobby_advertisement(discovery, &beacon)) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    const bool browser = s_ble.status.lobby_mode ==
        PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER;
    const bool host_collision_scan = s_ble.status.lobby_mode ==
            PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST &&
        s_ble.host_collision_scanning;
    const bool game_matches =
        s_ble.status.lobby_game_token == 0U ||
        beacon.game_token == s_ble.status.lobby_game_token;
    if ((!browser && !host_collision_scan) || !game_matches ||
        (host_collision_scan &&
         beacon.session_id == s_ble.status.lobby_session_id)) {
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    const uint64_t now_ms = monotonic_ms();
    prune_lobbies_locked(now_ms);
    p4_ble_lobby_entry_t *slot = NULL;
    p4_ble_lobby_entry_t *weakest = NULL;
    for (size_t index = 0U;
         index < PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES; ++index) {
        p4_ble_lobby_entry_t *const candidate = &s_ble.lobbies[index];
        if (candidate->occupied &&
            candidate->address.type == discovery->addr.type &&
            memcmp(candidate->address.val, discovery->addr.val,
                   sizeof(candidate->address.val)) == 0) {
            slot = candidate;
            break;
        }
        if (!candidate->occupied && slot == NULL) {
            slot = candidate;
        }
        if (candidate->occupied &&
            (weakest == NULL || candidate->lobby.rssi < weakest->lobby.rssi)) {
            weakest = candidate;
        }
    }
    if (slot == NULL) {
        if (weakest == NULL || discovery->rssi <= weakest->lobby.rssi) {
            portEXIT_CRITICAL(&s_lock);
            return;
        }
        slot = weakest;
    }
    *slot = (p4_ble_lobby_entry_t){
        .lobby = {
            .lobby_id = p4_mp_ble_route_id(discovery->addr.val),
            .session_id = beacon.session_id,
            .game_token = beacon.game_token,
            .rssi = discovery->rssi,
            .players_present = beacon.players_present,
            .player_capacity = beacon.player_capacity,
        },
        .address = discovery->addr,
        .last_seen_ms = now_ms,
        .occupied = true,
    };
    refresh_lobby_count_locked();
    portEXIT_CRITICAL(&s_lock);
}

static void fail_connection(uint16_t conn_handle, int error, const char *stage)
{
    ESP_LOGW(TAG, "peer setup failed stage=%s rc=%d", stage, error);
    count_drop(error);
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void resume_lobby_after_connection_loss(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_ble.status.lobby_mode ==
            PLATFORM_MULTIPLAYER_BLE_LOBBY_CLIENT) {
        s_ble.status.lobby_mode =
            PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER;
        s_ble.status.lobby_session_id = 0U;
        s_ble.status.lobby_game_token = 0U;
        s_ble.selected_host_valid = false;
        memset(s_ble.selected_host_address, 0,
               sizeof(s_ble.selected_host_address));
        s_ble.selected_host_address_type = 0U;
    }
    portEXIT_CRITICAL(&s_lock);
    start_discovery();
}

static int subscribe_complete(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    struct ble_gatt_attr *attribute,
    void *argument)
{
    (void)attribute;
    (void)argument;
    if (!connection_matches(conn_handle)) {
        return 0;
    }
    if (error == NULL || error->status != 0U) {
        fail_connection(conn_handle,
                        error == NULL ? BLE_HS_EINVAL : error->status,
                        "subscribe");
        return 0;
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.status.subscribed = true;
    portEXIT_CRITICAL(&s_lock);
    set_state(PLATFORM_MULTIPLAYER_BLE_READY, 0);
    ESP_LOGI(TAG, "BLE multiplayer ready role=central mtu=%u route=%llu",
             (unsigned)ble_att_mtu(conn_handle),
             (unsigned long long)s_ble.status.route_id);
    return 0;
}

static int descriptor_discovered(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    uint16_t characteristic_handle,
    const struct ble_gatt_dsc *descriptor,
    void *argument)
{
    (void)characteristic_handle;
    (void)argument;
    if (!connection_matches(conn_handle) || error == NULL) {
        return 0;
    }
    if (error->status == 0U && descriptor != NULL) {
        if (ble_uuid_u16(&descriptor->uuid.u) ==
            BLE_GATT_DSC_CLT_CFG_UUID16) {
            s_ble.peer_cccd_handle = descriptor->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE || s_ble.peer_cccd_handle == 0U) {
        fail_connection(conn_handle, error->status, "descriptor-discovery");
        return 0;
    }
    const uint8_t notifications_on[2] = {1U, 0U};
    const int result = ble_gattc_write_flat(
        conn_handle, s_ble.peer_cccd_handle,
        notifications_on, sizeof(notifications_on),
        subscribe_complete, NULL);
    if (result != 0) {
        fail_connection(conn_handle, result, "subscribe-start");
    }
    return 0;
}

static int characteristic_discovered(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    const struct ble_gatt_chr *characteristic,
    void *argument)
{
    (void)argument;
    if (!connection_matches(conn_handle) || error == NULL) {
        return 0;
    }
    if (error->status == 0U && characteristic != NULL) {
        s_ble.peer_value_handle = characteristic->val_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || s_ble.peer_value_handle == 0U ||
        s_ble.peer_service_end <= s_ble.peer_value_handle) {
        fail_connection(conn_handle, error->status,
                        "characteristic-discovery");
        return 0;
    }
    const int result = ble_gattc_disc_all_dscs(
        conn_handle, s_ble.peer_value_handle,
        s_ble.peer_service_end, descriptor_discovered, NULL);
    if (result != 0) {
        fail_connection(conn_handle, result, "descriptor-start");
    }
    return 0;
}

static int service_discovered(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    const struct ble_gatt_svc *service,
    void *argument)
{
    (void)argument;
    if (!connection_matches(conn_handle) || error == NULL) {
        return 0;
    }
    if (error->status == 0U && service != NULL) {
        s_ble.peer_service_start = service->start_handle;
        s_ble.peer_service_end = service->end_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE ||
        s_ble.peer_service_start == 0U ||
        s_ble.peer_service_end <= s_ble.peer_service_start) {
        fail_connection(conn_handle, error->status, "service-discovery");
        return 0;
    }
    const int result = ble_gattc_disc_chrs_by_uuid(
        conn_handle, s_ble.peer_service_start, s_ble.peer_service_end,
        &P4_BLE_CHARACTERISTIC_UUID.u,
        characteristic_discovered, NULL);
    if (result != 0) {
        fail_connection(conn_handle, result, "characteristic-start");
    }
    return 0;
}

static void start_gatt_discovery(uint16_t conn_handle)
{
    set_state(PLATFORM_MULTIPLAYER_BLE_DISCOVERING_GATT, 0);
    const int result = ble_gattc_disc_svc_by_uuid(
        conn_handle, &P4_BLE_SERVICE_UUID.u,
        service_discovered, NULL);
    if (result != 0) {
        fail_connection(conn_handle, result, "service-start");
    }
}

static int mtu_exchanged(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    uint16_t mtu,
    void *argument)
{
    (void)argument;
    if (!connection_matches(conn_handle)) {
        return 0;
    }
    if (error != NULL && error->status != 0U) {
        ESP_LOGW(TAG, "MTU exchange degraded rc=%u; using default",
                 (unsigned)error->status);
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.status.att_mtu = mtu >= BLE_ATT_MTU_DFLT
        ? mtu : BLE_ATT_MTU_DFLT;
    portEXIT_CRITICAL(&s_lock);
    start_gatt_discovery(conn_handle);
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
    portENTER_CRITICAL(&s_lock);
    s_ble.status.encrypted = true;
    const bool central = s_ble.status.central;
    portEXIT_CRITICAL(&s_lock);
    if (!central) {
        set_state(PLATFORM_MULTIPLAYER_BLE_DISCOVERING_GATT, 0);
        return;
    }
    const int result = ble_gattc_exchange_mtu(
        conn_handle, mtu_exchanged, NULL);
    if (result != 0) {
        /* Discovery still works at the mandatory 23-byte ATT MTU. */
        (void)mtu_exchanged(conn_handle, NULL, BLE_ATT_MTU_DFLT, NULL);
    }
}

static void connected(uint16_t conn_handle)
{
    struct ble_gap_conn_desc description;
    const int result = ble_gap_conn_find(conn_handle, &description);
    if (result != 0) {
        fail_connection(conn_handle, result, "connection-description");
        return;
    }
    const bool central = description.role == BLE_GAP_ROLE_MASTER;
    portENTER_CRITICAL(&s_lock);
    const platform_multiplayer_ble_lobby_mode_t mode =
        s_ble.status.lobby_mode;
    const bool selected_host_valid = s_ble.selected_host_valid;
    const uint8_t selected_host_address_type =
        s_ble.selected_host_address_type;
    uint8_t selected_host_address[6];
    memcpy(selected_host_address, s_ble.selected_host_address,
           sizeof(selected_host_address));
    portEXIT_CRITICAL(&s_lock);
    const bool expected_role = central
        ? mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_CLIENT &&
            selected_host_valid &&
            description.peer_ota_addr.type == selected_host_address_type &&
            memcmp(description.peer_ota_addr.val, selected_host_address,
                   sizeof(selected_host_address)) == 0
        : mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST;
    if (!expected_role) {
        ESP_LOGW(TAG, "rejecting connection outside selected lobby role");
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    (void)ble_gap_disc_cancel();
    (void)ble_gap_adv_stop();
    const uint8_t *peer = description.peer_id_addr.val;
    memcpy(s_ble.peer_address, peer, sizeof(s_ble.peer_address));
    portENTER_CRITICAL(&s_lock);
    s_ble.conn_handle = conn_handle;
    s_ble.status.connected = true;
    s_ble.status.central = central;
    s_ble.status.encrypted = description.sec_state.encrypted;
    s_ble.status.subscribed = false;
    s_ble.status.route_id = p4_mp_ble_route_id(peer);
    s_ble.status.att_mtu = ble_att_mtu(conn_handle);
    s_ble.host_collision_deadline_ms = 0U;
    s_ble.host_collision_scanning = false;
    portEXIT_CRITICAL(&s_lock);
    set_state(PLATFORM_MULTIPLAYER_BLE_SECURING, 0);

    const struct ble_gap_upd_params fast = {
        .itvl_min = P4_BLE_FAST_INTERVAL_MIN,
        .itvl_max = P4_BLE_FAST_INTERVAL_MAX,
        .latency = 0U,
        .supervision_timeout = P4_BLE_SUPERVISION_TIMEOUT,
        .min_ce_len = 0U,
        .max_ce_len = 0U,
    };
    (void)ble_gap_update_params(conn_handle, &fast);
    ESP_LOGI(TAG, "peer connected role=%s route=%llu",
             central ? "central" : "peripheral",
             (unsigned long long)p4_mp_ble_route_id(peer));
    if (description.sec_state.encrypted) {
        connection_encrypted(conn_handle);
    } else if (central) {
        const int security = ble_gap_security_initiate(conn_handle);
        if (security != 0) {
            fail_connection(conn_handle, security, "security-start");
        }
    }
}

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    if (event == NULL) {
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        observe_lobby(&event->disc);
        break;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            connected(event->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "connection failed rc=%d", event->connect.status);
            clear_connection_state();
            resume_lobby_after_connection_loss();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "peer disconnected reason=%d",
                 event->disconnect.reason);
        clear_connection_state();
        resume_lobby_after_connection_loss();
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            connection_encrypted(event->enc_change.conn_handle);
        } else {
            fail_connection(event->enc_change.conn_handle,
                            event->enc_change.status, "encryption-change");
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (connection_matches(event->subscribe.conn_handle) &&
            event->subscribe.attr_handle == s_ble.local_value_handle &&
            event->subscribe.cur_notify != 0U) {
            portENTER_CRITICAL(&s_lock);
            s_ble.status.subscribed = true;
            const bool encrypted = s_ble.status.encrypted;
            const bool peripheral = !s_ble.status.central;
            portEXIT_CRITICAL(&s_lock);
            if (encrypted && peripheral) {
                set_state(PLATFORM_MULTIPLAYER_BLE_READY, 0);
                ESP_LOGI(TAG,
                         "BLE multiplayer ready role=peripheral mtu=%u",
                         (unsigned)ble_att_mtu(event->subscribe.conn_handle));
            }
        }
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (connection_matches(event->notify_rx.conn_handle) &&
            event->notify_rx.attr_handle == s_ble.peer_value_handle) {
            const uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (length > P4_MP_BLE_FRAGMENT_HEADER_BYTES &&
                length <= P4_MP_BLE_MAX_FRAGMENT_BYTES) {
                uint8_t fragment[P4_MP_BLE_MAX_FRAGMENT_BYTES];
                uint16_t copied = 0U;
                if (ble_hs_mbuf_to_flat(
                        event->notify_rx.om, fragment,
                        sizeof(fragment), &copied) == 0 &&
                    copied == length) {
                    consume_fragment(fragment, copied);
                } else {
                    count_drop(BLE_HS_EBADDATA);
                }
            } else {
                count_drop(BLE_HS_EMSGSIZE);
            }
        }
        break;
    case BLE_GAP_EVENT_MTU:
        if (connection_matches(event->mtu.conn_handle)) {
            portENTER_CRITICAL(&s_lock);
            s_ble.status.att_mtu = event->mtu.value;
            portEXIT_CRITICAL(&s_lock);
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (is_enabled()) {
            start_discovery();
        }
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (is_enabled()) {
            start_discovery();
        }
        break;
    default:
        break;
    }
    return 0;
}

static void host_reset(int reason, void *context)
{
    (void)context;
    ESP_LOGE(TAG, "NimBLE host reset reason=%d", reason);
    clear_connection_state();
    set_state(PLATFORM_MULTIPLAYER_BLE_ERROR, reason);
}

static void host_sync(void *context)
{
    (void)context;
    const esp_err_t result =
        platform_ble_host_own_addr_type(&s_ble.own_addr_type);
    if (result != ESP_OK) {
        host_reset((int)result, NULL);
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.status.host_ready = true;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "NimBLE host synchronized; game radio is opt-in");
    start_discovery();
}

esp_err_t platform_multiplayer_ble_prepare(void)
{
    return platform_ble_host_register_client(&P4_BLE_HOST_CLIENT);
}

esp_err_t platform_multiplayer_ble_enable(
    platform_multiplayer_ble_frame_handler_t handler,
    void *handler_context)
{
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t prepare_result = platform_multiplayer_ble_prepare();
    if (prepare_result != ESP_OK) {
        return prepare_result;
    }
    if (s_ble.rx_queue == NULL) {
        /* Complete game datagrams are not DMA data and this queue is never
         * touched from an ISR. Keep its backing store in PSRAM so the
         * late-started C6 SDIO transport retains contiguous internal RAM. */
        s_ble.rx_queue = xQueueCreateWithCaps(
            P4_BLE_RX_QUEUE_DEPTH, sizeof(p4_ble_rx_entry_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_ble.rx_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_ble.tx_queue == NULL) {
        s_ble.tx_queue = xQueueCreateWithCaps(
            P4_BLE_TX_QUEUE_DEPTH, sizeof(p4_ble_rx_entry_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_ble.tx_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.handler = handler;
    s_ble.handler_context = handler_context;
    s_ble.status.enabled = true;
    const bool ready = s_ble.status.host_ready;
    portEXIT_CRITICAL(&s_lock);
    if (ready) {
        start_discovery();
        return ESP_OK;
    }
    set_state(PLATFORM_MULTIPLAYER_BLE_STARTING_RADIO, 0);
    const esp_err_t start_result = platform_ble_host_start();
    if (start_result != ESP_OK) {
        set_state(PLATFORM_MULTIPLAYER_BLE_ERROR, (int)start_result);
    } else {
        set_state(PLATFORM_MULTIPLAYER_BLE_STARTING_HOST, 0);
    }
    return start_result;
}

esp_err_t platform_multiplayer_ble_set_lobby_mode(
    platform_multiplayer_ble_lobby_mode_t mode,
    uint32_t session_id,
    uint16_t game_token)
{
    const uint64_t now_ms = monotonic_ms();
    if ((mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_IDLE &&
         (session_id != 0U || game_token != 0U)) ||
        (mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER &&
         session_id != 0U) ||
        (mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST &&
         (session_id == 0U || game_token == 0U)) ||
        mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_CLIENT ||
        mode > PLATFORM_MULTIPLAYER_BLE_LOBBY_CLIENT) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    if (!s_ble.status.enabled) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const bool clear_lobbies =
        mode != PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER ||
        s_ble.status.lobby_game_token != game_token;
    s_ble.status.lobby_mode = mode;
    s_ble.status.lobby_session_id = session_id;
    s_ble.status.lobby_game_token = game_token;
    s_ble.host_collision_round = 0U;
    s_ble.host_collision_scanning = false;
    s_ble.host_collision_deadline_ms =
        mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST
            ? now_ms + host_collision_advertise_ms(session_id, 0U)
            : 0U;
    s_ble.selected_host_valid = false;
    memset(s_ble.selected_host_address, 0,
           sizeof(s_ble.selected_host_address));
    s_ble.selected_host_address_type = 0U;
    if (clear_lobbies) {
        clear_lobbies_locked();
    }
    const bool host_ready = s_ble.status.host_ready;
    const uint16_t conn_handle = s_ble.conn_handle;
    portEXIT_CRITICAL(&s_lock);

    clear_transport_queues();
    if (!host_ready) {
        return ESP_OK;
    }
    (void)ble_gap_disc_cancel();
    (void)ble_gap_adv_stop();
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return ESP_OK;
    }
    start_discovery();
    return ESP_OK;
}

size_t platform_multiplayer_ble_list_lobbies(
    platform_multiplayer_ble_lobby_t *lobbies,
    size_t lobby_capacity)
{
    if (lobby_capacity != 0U && lobbies == NULL) {
        return 0U;
    }
    platform_multiplayer_ble_lobby_t snapshot[
        PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES];
    size_t count = 0U;
    const uint64_t now_ms = monotonic_ms();
    portENTER_CRITICAL(&s_lock);
    prune_lobbies_locked(now_ms);
    for (size_t index = 0U;
         index < PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES; ++index) {
        if (s_ble.lobbies[index].occupied) {
            snapshot[count++] = s_ble.lobbies[index].lobby;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    for (size_t index = 1U; index < count; ++index) {
        const platform_multiplayer_ble_lobby_t value = snapshot[index];
        size_t position = index;
        while (position > 0U &&
               (snapshot[position - 1U].rssi < value.rssi ||
                (snapshot[position - 1U].rssi == value.rssi &&
                 snapshot[position - 1U].session_id > value.session_id))) {
            snapshot[position] = snapshot[position - 1U];
            --position;
        }
        snapshot[position] = value;
    }
    const size_t copied = count < lobby_capacity ? count : lobby_capacity;
    if (copied != 0U) {
        memcpy(lobbies, snapshot, copied * sizeof(*lobbies));
    }
    return copied;
}

esp_err_t platform_multiplayer_ble_join_lobby(uint64_t lobby_id)
{
    if (lobby_id == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    ble_addr_t address = {0};
    const uint64_t now_ms = monotonic_ms();
    portENTER_CRITICAL(&s_lock);
    prune_lobbies_locked(now_ms);
    if (!s_ble.status.enabled || !s_ble.status.host_ready ||
        s_ble.status.lobby_mode !=
            PLATFORM_MULTIPLAYER_BLE_LOBBY_BROWSER ||
        s_ble.status.connected) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    p4_ble_lobby_entry_t *selected = NULL;
    for (size_t index = 0U;
         index < PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES; ++index) {
        if (s_ble.lobbies[index].occupied &&
            s_ble.lobbies[index].lobby.lobby_id == lobby_id) {
            selected = &s_ble.lobbies[index];
            break;
        }
    }
    if (selected == NULL) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NOT_FOUND;
    }
    address = selected->address;
    memcpy(s_ble.selected_host_address, address.val,
           sizeof(s_ble.selected_host_address));
    s_ble.selected_host_address_type = address.type;
    s_ble.selected_host_valid = true;
    s_ble.status.lobby_mode = PLATFORM_MULTIPLAYER_BLE_LOBBY_CLIENT;
    s_ble.status.lobby_session_id = selected->lobby.session_id;
    s_ble.status.lobby_game_token = selected->lobby.game_token;
    clear_lobbies_locked();
    portEXIT_CRITICAL(&s_lock);

    (void)ble_gap_disc_cancel();
    (void)ble_gap_adv_stop();
    const struct ble_gap_conn_params parameters = {
        .scan_itvl = 48U,
        .scan_window = 24U,
        .itvl_min = P4_BLE_FAST_INTERVAL_MIN,
        .itvl_max = P4_BLE_FAST_INTERVAL_MAX,
        .latency = 0U,
        .supervision_timeout = P4_BLE_SUPERVISION_TIMEOUT,
        .min_ce_len = 0U,
        .max_ce_len = 0U,
    };
    set_state(PLATFORM_MULTIPLAYER_BLE_CONNECTING, 0);
    const int result = ble_gap_connect(
        s_ble.own_addr_type, &address,
        P4_BLE_CONNECT_TIMEOUT_MS, &parameters, gap_event, NULL);
    if (result == 0) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "selected lobby connect failed rc=%d", result);
    clear_connection_state();
    resume_lobby_after_connection_loss();
    return ESP_FAIL;
}

esp_err_t platform_multiplayer_ble_set_handler(
    platform_multiplayer_ble_frame_handler_t handler,
    void *handler_context)
{
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    s_ble.handler = handler;
    s_ble.handler_context = handler_context;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

void platform_multiplayer_ble_disable(void)
{
    portENTER_CRITICAL(&s_lock);
    /* The shared host may be running for Wi-Fi or a controller while this
     * client is off. Do not cancel another client's discovery or issue a
     * synchronous HCI advertising stop from the launcher's input task. */
    if (!s_ble.status.enabled) {
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    s_ble.status.enabled = false;
    s_ble.status.ready = false;
    s_ble.status.state = PLATFORM_MULTIPLAYER_BLE_OFF;
    s_ble.status.lobby_mode = PLATFORM_MULTIPLAYER_BLE_LOBBY_IDLE;
    s_ble.status.lobby_session_id = 0U;
    s_ble.status.lobby_game_token = 0U;
    s_ble.host_collision_deadline_ms = 0U;
    s_ble.host_collision_round = 0U;
    s_ble.host_collision_scanning = false;
    clear_lobbies_locked();
    s_ble.selected_host_valid = false;
    const bool host_ready = s_ble.status.host_ready;
    const uint16_t conn_handle = s_ble.conn_handle;
    portEXIT_CRITICAL(&s_lock);
    clear_transport_queues();
    if (!host_ready) {
        return;
    }
    (void)ble_gap_disc_cancel();
    (void)ble_gap_adv_stop();
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void platform_multiplayer_ble_poll(void)
{
    const uint64_t now_ms = monotonic_ms();
    bool collision_phase_changed = false;
    bool host_collision_active = false;
    bool host_collision_scanning = false;
    uint32_t host_collision_round = 0U;
    portENTER_CRITICAL(&s_lock);
    if (s_ble.status.enabled && s_ble.status.host_ready &&
        s_ble.status.lobby_mode == PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST &&
        !s_ble.status.connected) {
        host_collision_active = true;
        if (s_ble.host_collision_deadline_ms == 0U) {
            s_ble.host_collision_deadline_ms = now_ms +
                host_collision_advertise_ms(
                    s_ble.status.lobby_session_id,
                    s_ble.host_collision_round);
        } else if (now_ms >= s_ble.host_collision_deadline_ms) {
            if (s_ble.host_collision_scanning) {
                s_ble.host_collision_scanning = false;
                ++s_ble.host_collision_round;
                s_ble.host_collision_deadline_ms = now_ms +
                    host_collision_advertise_ms(
                        s_ble.status.lobby_session_id,
                        s_ble.host_collision_round);
            } else {
                s_ble.host_collision_scanning = true;
                s_ble.host_collision_deadline_ms = now_ms +
                    P4_BLE_HOST_COLLISION_SCAN_MS;
            }
            collision_phase_changed = true;
        }
        host_collision_scanning = s_ble.host_collision_scanning;
        host_collision_round = s_ble.host_collision_round;
    }
    prune_lobbies_locked(now_ms);
    portEXIT_CRITICAL(&s_lock);

    if (collision_phase_changed) {
        ESP_LOGI(TAG,
                 "P4_BLE_HOST_COLLISION_PHASE phase=%s round=%lu",
                 host_collision_scanning ? "scan" : "advertise",
                 (unsigned long)host_collision_round);
    }
    /*
     * GAP stop completion callbacks are not guaranteed for an explicit stop.
     * Keep driving the requested procedure until the controller reports the
     * matching active state, so a transition cannot strand a host idle.
     */
    if (host_collision_active &&
        (collision_phase_changed ||
         (host_collision_scanning
              ? (!ble_gap_disc_active() || ble_gap_adv_active())
              : (!ble_gap_adv_active() || ble_gap_disc_active())))) {
        start_discovery();
    }
    if (s_ble.rx_queue == NULL) {
        return;
    }
    p4_ble_rx_entry_t entry;
    for (unsigned drained = 0U; drained < P4_BLE_RX_QUEUE_DEPTH; ++drained) {
        if (xQueueReceive(s_ble.rx_queue, &entry, 0U) != pdTRUE) {
            break;
        }
        portENTER_CRITICAL(&s_lock);
        platform_multiplayer_ble_frame_handler_t handler = s_ble.handler;
        void *const context = s_ble.handler_context;
        portEXIT_CRITICAL(&s_lock);
        if (handler != NULL) {
            handler(context, entry.route_id,
                    entry.datagram, entry.length);
        }
    }
    if (s_ble.tx_queue == NULL) {
        return;
    }
    for (unsigned drained = 0U;
         drained < P4_BLE_TX_DRAIN_BUDGET; ++drained) {
        if (xQueueReceive(s_ble.tx_queue, &entry, 0U) != pdTRUE) {
            break;
        }
        const esp_err_t result =
            send_datagram_now(entry.datagram, entry.length);
        if (result != ESP_OK) {
            if (xQueueSendToFront(s_ble.tx_queue, &entry, 0U) != pdTRUE) {
                count_drop(BLE_HS_ENOMEM);
            }
            break;
        }
    }
}

static esp_err_t send_datagram_now(
    const uint8_t *datagram,
    size_t datagram_length)
{
    if (datagram == NULL || datagram_length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    const bool ready = s_ble.status.ready;
    const bool central = s_ble.status.central;
    const uint16_t conn_handle = s_ble.conn_handle;
    const uint16_t value_handle = central
        ? s_ble.peer_value_handle : s_ble.local_value_handle;
    const uint16_t mtu = s_ble.status.att_mtu;
    uint16_t frame_id = s_ble.next_frame_id++;
    if (s_ble.next_frame_id == 0U) {
        s_ble.next_frame_id = 1U;
    }
    portEXIT_CRITICAL(&s_lock);
    if (!ready || conn_handle == BLE_HS_CONN_HANDLE_NONE ||
        value_handle == 0U || mtu <= P4_MP_BLE_FRAGMENT_HEADER_BYTES + 3U) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame_id == 0U) {
        frame_id = 1U;
    }
    size_t fragment_capacity = (size_t)mtu - 3U;
    if (fragment_capacity > P4_MP_BLE_MAX_FRAGMENT_BYTES) {
        fragment_capacity = P4_MP_BLE_MAX_FRAGMENT_BYTES;
    }
    uint8_t fragment[P4_MP_BLE_MAX_FRAGMENT_BYTES];
    size_t offset = 0U;
    while (offset < datagram_length) {
        size_t fragment_length = 0U;
        size_t next_offset = offset;
        const p4_mp_status_t encoded = p4_mp_ble_fragment_encode(
            datagram, datagram_length, frame_id, offset,
            fragment_capacity, fragment, sizeof(fragment),
            &fragment_length, &next_offset);
        if (encoded != P4_MP_OK || fragment_length > UINT16_MAX) {
            count_drop((int)encoded);
            return ESP_ERR_INVALID_ARG;
        }
        int result;
        if (central) {
            result = ble_gattc_write_no_rsp_flat(
                conn_handle, value_handle, fragment,
                (uint16_t)fragment_length);
        } else {
            struct os_mbuf *const message =
                os_msys_get_pkthdr((uint16_t)fragment_length, 0U);
            if (message == NULL) {
                result = BLE_HS_ENOMEM;
            } else if (os_mbuf_append(
                           message, fragment,
                           (uint16_t)fragment_length) != 0) {
                os_mbuf_free_chain(message);
                result = BLE_HS_ENOMEM;
            } else {
                result = ble_gatts_notify_custom(
                    conn_handle, value_handle, message);
            }
        }
        if (result != 0) {
            count_drop(result);
            return ESP_FAIL;
        }
        offset = next_offset;
    }
    portENTER_CRITICAL(&s_lock);
    if (s_ble.status.tx_frames != UINT32_MAX) {
        ++s_ble.status.tx_frames;
    }
    const uint32_t bytes = datagram_length > UINT32_MAX
        ? UINT32_MAX : (uint32_t)datagram_length;
    s_ble.status.tx_bytes = UINT32_MAX - s_ble.status.tx_bytes < bytes
        ? UINT32_MAX : s_ble.status.tx_bytes + bytes;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t platform_multiplayer_ble_send(
    const uint8_t *datagram,
    size_t datagram_length)
{
    if (datagram == NULL || datagram_length == 0U ||
        datagram_length > P4_MP_MAX_DATAGRAM_BYTES ||
        datagram_length > UINT16_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    const bool ready = s_ble.status.ready;
    portEXIT_CRITICAL(&s_lock);
    if (!ready || s_ble.tx_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const p4_ble_rx_entry_t entry = {
        .length = (uint16_t)datagram_length,
    };
    p4_ble_rx_entry_t queued = entry;
    memcpy(queued.datagram, datagram, datagram_length);
    if (xQueueSend(s_ble.tx_queue, &queued, 0U) != pdTRUE) {
        count_drop(BLE_HS_ENOMEM);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void platform_multiplayer_ble_reset_route(void)
{
    /* The next START fragment atomically supersedes any partial frame. */
    clear_transport_queues();
}

platform_multiplayer_ble_status_t platform_multiplayer_ble_status(void)
{
    portENTER_CRITICAL(&s_lock);
    const platform_multiplayer_ble_status_t status = s_ble.status;
    portEXIT_CRITICAL(&s_lock);
    return status;
}

bool platform_multiplayer_ble_route_connected(uint64_t route_id)
{
    portENTER_CRITICAL(&s_lock);
    const bool connected = s_ble.status.ready && route_id != 0U &&
        route_id == s_ble.status.route_id;
    portEXIT_CRITICAL(&s_lock);
    return connected;
}

const char *platform_multiplayer_ble_route_name(uint64_t route_id)
{
    return (route_id & UINT64_C(0xffff000000000000)) ==
            P4_MP_BLE_ROUTE_PREFIX
        ? "ble-gatt" : "ble-none";
}
