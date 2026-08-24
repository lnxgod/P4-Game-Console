// SPDX-License-Identifier: MIT

#include "p4/multiplayer_uart.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#pragma GCC diagnostic pop
#include "p4/multiplayer.h"
#include "sdkconfig.h"

#ifndef CONFIG_ESP_CONSOLE_UART
#define CONFIG_ESP_CONSOLE_UART 0
#endif

#if defined(CONFIG_P4_MP_DIRECT_UART) && CONFIG_P4_MP_DIRECT_UART
#define P4_MP_DIRECT_UART_ENABLED 1
#else
#define P4_MP_DIRECT_UART_ENABLED 0
#endif

enum {
    P4_MP_UART_RX_RING_BYTES = 2048,
    P4_MP_UART_READ_BYTES = 256,
    P4_MP_UART_MAX_READS_PER_POLL = 8,
};

static const char *TAG = "p4_mp_uart";

typedef struct {
    p4_mp_stream_decoder_t decoder;
    uart_port_t port;
    uint64_t route_id;
    uint32_t baudrate;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint8_t receive[P4_MP_UART_READ_BYTES];
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    bool ready;
    bool raw_capable;
} p4_mp_uart_channel_t;

typedef struct {
    p4_mp_uart_frame_handler_t handler;
    void *handler_context;
    p4_mp_uart_raw_handler_t raw_handler;
    void *raw_handler_context;
    p4_mp_uart_channel_t direct;
    p4_mp_uart_channel_t relay;
    uint64_t active_route_id;
    uint32_t direct_discoveries;
    esp_err_t last_error;
    bool initialized;
} p4_mp_uart_endpoint_t;

static p4_mp_uart_endpoint_t s_endpoint;

static void add_counter(uint32_t *counter, size_t amount)
{
    if (counter == NULL) {
        return;
    }
    if (amount >= UINT32_MAX || *counter > UINT32_MAX - (uint32_t)amount) {
        *counter = UINT32_MAX;
    } else {
        *counter += (uint32_t)amount;
    }
}

static uint32_t add_saturated(uint32_t left, uint32_t right)
{
    return left > UINT32_MAX - right ? UINT32_MAX : left + right;
}

static void initialize_channel(
    p4_mp_uart_channel_t *channel,
    uart_port_t port,
    uint64_t route_id,
    uint32_t baudrate,
    bool raw_capable)
{
    *channel = (p4_mp_uart_channel_t){
        .port = port,
        .route_id = route_id,
        .baudrate = baudrate,
        .raw_capable = raw_capable,
    };
    p4_mp_stream_decoder_init(&channel->decoder);
}

static esp_err_t initialize_relay(void)
{
#if !CONFIG_ESP_CONSOLE_UART
    return ESP_ERR_NOT_SUPPORTED;
#else
    p4_mp_uart_channel_t *const channel = &s_endpoint.relay;
    initialize_channel(
        channel,
        (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM,
        P4_MP_UART_RELAY_ROUTE_ID,
        CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        true);
    if (!uart_is_driver_installed(channel->port)) {
        const esp_err_t install = uart_driver_install(
            channel->port, P4_MP_UART_RX_RING_BYTES, 0, 0, NULL, 0);
        if (install != ESP_OK) {
            return install;
        }
    }
    const esp_err_t baud = uart_set_baudrate(
        channel->port, channel->baudrate);
    if (baud != ESP_OK) {
        return baud;
    }
    (void)uart_flush_input(channel->port);
    channel->ready = true;
    ESP_LOGI(TAG,
             "P4_MP_UART_ROUTE_READY transport=h1-ch343-relay "
             "uart=%u baud=%u route=%u host_relay=required",
             (unsigned)channel->port,
             (unsigned)channel->baudrate,
             (unsigned)channel->route_id);
    return ESP_OK;
#endif
}

static esp_err_t initialize_direct(void)
{
#if !P4_MP_DIRECT_UART_ENABLED
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (CONFIG_P4_MP_DIRECT_UART_NUM < 0 ||
        CONFIG_P4_MP_DIRECT_UART_TX_GPIO < 0 ||
        CONFIG_P4_MP_DIRECT_UART_RX_GPIO < 0 ||
        CONFIG_P4_MP_DIRECT_UART_TX_GPIO ==
            CONFIG_P4_MP_DIRECT_UART_RX_GPIO) {
        return ESP_ERR_INVALID_ARG;
    }
    p4_mp_uart_channel_t *const channel = &s_endpoint.direct;
    initialize_channel(
        channel,
        (uart_port_t)CONFIG_P4_MP_DIRECT_UART_NUM,
        P4_MP_UART_DIRECT_ROUTE_ID,
        CONFIG_P4_MP_DIRECT_UART_BAUDRATE,
        false);
#if CONFIG_ESP_CONSOLE_UART
    if (channel->port == (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM) {
        return ESP_ERR_INVALID_ARG;
    }
#endif
    if (uart_is_driver_installed(channel->port)) {
        return ESP_ERR_INVALID_STATE;
    }
    const uart_config_t config = {
        .baud_rate = (int)channel->baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0U,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t result = uart_param_config(channel->port, &config);
    if (result != ESP_OK) {
        return result;
    }
    result = uart_driver_install(
        channel->port, P4_MP_UART_RX_RING_BYTES, 0, 0, NULL, 0);
    if (result != ESP_OK) {
        return result;
    }
    result = uart_set_pin(
        channel->port,
        CONFIG_P4_MP_DIRECT_UART_TX_GPIO,
        CONFIG_P4_MP_DIRECT_UART_RX_GPIO,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE);
    if (result != ESP_OK) {
        (void)uart_driver_delete(channel->port);
        return result;
    }
    (void)uart_flush_input(channel->port);
    channel->ready = true;
    ESP_LOGI(TAG,
             "P4_MP_UART_ROUTE_READY transport=direct-uart uart=%u "
             "tx_gpio=%d rx_gpio=%d baud=%u route=%u host_relay=0",
             (unsigned)channel->port,
             CONFIG_P4_MP_DIRECT_UART_TX_GPIO,
             CONFIG_P4_MP_DIRECT_UART_RX_GPIO,
             (unsigned)channel->baudrate,
             (unsigned)channel->route_id);
    return ESP_OK;
#endif
}

const char *p4_mp_uart_route_name(uint64_t route_id)
{
    switch (route_id) {
    case P4_MP_UART_DIRECT_ROUTE_ID:
        return "direct-uart";
    case P4_MP_UART_RELAY_ROUTE_ID:
        return "h1-uart-relay";
    case P4_MP_UART_ROUTE_NONE:
    default:
        return "auto";
    }
}

esp_err_t p4_mp_uart_endpoint_init(
    p4_mp_uart_frame_handler_t handler,
    void *handler_context)
{
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_endpoint.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_endpoint, 0, sizeof(s_endpoint));
    s_endpoint.handler = handler;
    s_endpoint.handler_context = handler_context;
    const esp_err_t relay_result = initialize_relay();
    const esp_err_t direct_result = initialize_direct();
    if (!s_endpoint.relay.ready && !s_endpoint.direct.ready) {
        s_endpoint.last_error = direct_result != ESP_ERR_NOT_SUPPORTED
            ? direct_result : relay_result;
        ESP_LOGE(TAG,
                 "P4_MP_UART_UNAVAILABLE direct=%s relay=%s",
                 esp_err_to_name(direct_result),
                 esp_err_to_name(relay_result));
        return s_endpoint.last_error;
    }
    s_endpoint.initialized = true;
    s_endpoint.last_error = ESP_OK;
    ESP_LOGI(TAG,
             "P4_MP_UART_READY policy=direct-first fallback=h1-relay "
             "direct=%u relay=%u frame_max=%u",
             s_endpoint.direct.ready ? 1U : 0U,
             s_endpoint.relay.ready ? 1U : 0U,
             (unsigned)P4_MP_MAX_DATAGRAM_BYTES);
    if (direct_result != ESP_OK && direct_result != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG,
                 "P4_MP_UART_DIRECT_DEGRADED error=%s fallback=h1-relay",
                 esp_err_to_name(direct_result));
    }
    return ESP_OK;
}

esp_err_t p4_mp_uart_endpoint_set_handler(
    p4_mp_uart_frame_handler_t handler,
    void *handler_context)
{
    if (!s_endpoint.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_endpoint.handler = handler;
    s_endpoint.handler_context = handler_context;
    return ESP_OK;
}

esp_err_t p4_mp_uart_endpoint_set_raw_handler(
    p4_mp_uart_raw_handler_t handler,
    void *handler_context)
{
    if (!s_endpoint.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_endpoint.relay.ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_endpoint.raw_handler = handler;
    s_endpoint.raw_handler_context =
        handler == NULL ? NULL : handler_context;
    return ESP_OK;
}

static bool relay_fallback_armed(void)
{
    if (!s_endpoint.direct.ready) {
        return true;
    }
#if P4_MP_DIRECT_UART_ENABLED
    return s_endpoint.direct_discoveries >=
        (uint32_t)CONFIG_P4_MP_DIRECT_FALLBACK_DISCOVERIES;
#else
    return true;
#endif
}

static bool packet_can_bind_route(const p4_mp_packet_view_t *packet)
{
    return packet != NULL &&
        (packet->type == P4_MP_PACKET_DISCOVER ||
         packet->type == P4_MP_PACKET_OFFER);
}

static void deliver_frame(
    p4_mp_uart_channel_t *channel,
    size_t datagram_length)
{
    if (s_endpoint.active_route_id == P4_MP_UART_ROUTE_NONE) {
        p4_mp_packet_view_t packet;
        if (p4_mp_packet_decode(
                channel->datagram, datagram_length, &packet) != P4_MP_OK ||
            !packet_can_bind_route(&packet) ||
            (channel->route_id == P4_MP_UART_RELAY_ROUTE_ID &&
             !relay_fallback_armed())) {
            return;
        }
        s_endpoint.active_route_id = channel->route_id;
        ESP_LOGI(TAG,
                 "P4_MP_UART_ROUTE_LOCK transport=%s route=%" PRIu64,
                 p4_mp_uart_route_name(channel->route_id),
                 channel->route_id);
    }
    if (s_endpoint.active_route_id != channel->route_id) {
        return;
    }
    s_endpoint.handler(
        s_endpoint.handler_context,
        channel->route_id,
        channel->datagram,
        datagram_length);
}

static void poll_channel(p4_mp_uart_channel_t *channel)
{
    if (!channel->ready) {
        return;
    }
    for (unsigned pass = 0U;
         pass < P4_MP_UART_MAX_READS_PER_POLL;
         ++pass) {
        size_t available = 0U;
        esp_err_t result = uart_get_buffered_data_len(
            channel->port, &available);
        if (result != ESP_OK) {
            s_endpoint.last_error = result;
            return;
        }
        if (available == 0U) {
            break;
        }
        size_t requested = available;
        if (requested > sizeof(channel->receive)) {
            requested = sizeof(channel->receive);
        }
        const int count = uart_read_bytes(
            channel->port, channel->receive, (uint32_t)requested, 0U);
        if (count <= 0) {
            break;
        }
        add_counter(&channel->rx_bytes, (size_t)count);
        if (channel->raw_capable && s_endpoint.raw_handler != NULL &&
            s_endpoint.raw_handler(
                s_endpoint.raw_handler_context,
                channel->receive,
                (size_t)count)) {
            p4_mp_stream_decoder_init(&channel->decoder);
            continue;
        }
        size_t offset = 0U;
        while (offset < (size_t)count) {
            size_t consumed = 0U;
            size_t datagram_length = 0U;
            const p4_mp_stream_result_t stream_result =
                p4_mp_stream_consume(
                    &channel->decoder,
                    channel->receive + offset,
                    (size_t)count - offset,
                    &consumed,
                    channel->datagram,
                    sizeof(channel->datagram),
                    &datagram_length);
            if (consumed == 0U) {
                s_endpoint.last_error = ESP_FAIL;
                return;
            }
            offset += consumed;
            if (stream_result == P4_MP_STREAM_FRAME_READY) {
                add_counter(&channel->rx_frames, 1U);
                deliver_frame(channel, datagram_length);
            } else if (stream_result == P4_MP_STREAM_INVALID_ARGUMENT) {
                s_endpoint.last_error = ESP_ERR_INVALID_ARG;
                return;
            }
        }
    }
}

void p4_mp_uart_endpoint_poll(void)
{
    if (!s_endpoint.initialized) {
        return;
    }
    /* Direct is always drained first so simultaneous valid probes select it. */
    poll_channel(&s_endpoint.direct);
    poll_channel(&s_endpoint.relay);
}

static p4_mp_uart_channel_t *channel_for_route(uint64_t route_id)
{
    if (route_id == P4_MP_UART_DIRECT_ROUTE_ID &&
        s_endpoint.direct.ready) {
        return &s_endpoint.direct;
    }
    if (route_id == P4_MP_UART_RELAY_ROUTE_ID &&
        s_endpoint.relay.ready) {
        return &s_endpoint.relay;
    }
    return NULL;
}

static esp_err_t write_channel(
    p4_mp_uart_channel_t *channel,
    const uint8_t *bytes,
    size_t bytes_length,
    bool framed)
{
    if (channel == NULL || !channel->ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const int written = uart_write_bytes(
        channel->port, bytes, bytes_length);
    if (written < 0 || (size_t)written != bytes_length) {
        return ESP_FAIL;
    }
    add_counter(&channel->tx_bytes, bytes_length);
    if (framed) {
        add_counter(&channel->tx_frames, 1U);
    }
    return ESP_OK;
}

esp_err_t p4_mp_uart_endpoint_send(
    const uint8_t *datagram,
    size_t datagram_length)
{
    if (!s_endpoint.initialized || datagram == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    p4_mp_packet_view_t packet;
    if (p4_mp_packet_decode(
            datagram, datagram_length, &packet) != P4_MP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_endpoint.active_route_id != P4_MP_UART_ROUTE_NONE) {
        const esp_err_t result = write_channel(
            channel_for_route(s_endpoint.active_route_id),
            datagram, datagram_length, true);
        s_endpoint.last_error = result;
        return result;
    }

    bool sent = false;
    esp_err_t direct_result = ESP_ERR_NOT_SUPPORTED;
    esp_err_t relay_result = ESP_ERR_NOT_SUPPORTED;
    const bool relay_now = relay_fallback_armed();
    if (s_endpoint.direct.ready) {
        direct_result = write_channel(
            &s_endpoint.direct, datagram, datagram_length, true);
        sent = direct_result == ESP_OK;
        if (packet.type == P4_MP_PACKET_DISCOVER &&
            s_endpoint.direct_discoveries != UINT32_MAX) {
            ++s_endpoint.direct_discoveries;
        }
    }
    if (s_endpoint.relay.ready &&
        (!s_endpoint.direct.ready || relay_now || direct_result != ESP_OK)) {
        relay_result = write_channel(
            &s_endpoint.relay, datagram, datagram_length, true);
        sent = sent || relay_result == ESP_OK;
    }
    if (sent) {
        s_endpoint.last_error = ESP_OK;
        return ESP_OK;
    }
    s_endpoint.last_error = direct_result != ESP_ERR_NOT_SUPPORTED
        ? direct_result : relay_result;
    return s_endpoint.last_error;
}

esp_err_t p4_mp_uart_endpoint_send_raw(
    const uint8_t *bytes,
    size_t bytes_length)
{
    if (!s_endpoint.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (bytes == NULL || bytes_length == 0U ||
        bytes_length > P4_MP_UART_RAW_TX_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result = write_channel(
        s_endpoint.relay.ready ? &s_endpoint.relay : NULL,
        bytes, bytes_length, false);
    s_endpoint.last_error = result;
    return result;
}

esp_err_t p4_mp_uart_endpoint_wait_tx_done(uint32_t timeout_ms)
{
    if (!s_endpoint.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (timeout_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_endpoint.relay.ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const esp_err_t result = uart_wait_tx_done(
        s_endpoint.relay.port, pdMS_TO_TICKS(timeout_ms));
    s_endpoint.last_error = result;
    return result;
}

esp_err_t p4_mp_uart_endpoint_set_baudrate(uint32_t baudrate)
{
    if (!s_endpoint.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (baudrate < 9600U || baudrate > 2000000U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_endpoint.relay.ready) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const esp_err_t result = uart_set_baudrate(
        s_endpoint.relay.port, baudrate);
    if (result == ESP_OK) {
        s_endpoint.relay.baudrate = baudrate;
    }
    s_endpoint.last_error = result;
    return result;
}

void p4_mp_uart_endpoint_reset_route(void)
{
    if (!s_endpoint.initialized) {
        return;
    }
    if (s_endpoint.active_route_id != P4_MP_UART_ROUTE_NONE) {
        ESP_LOGI(TAG,
                 "P4_MP_UART_ROUTE_RELEASE transport=%s route=%" PRIu64,
                 p4_mp_uart_route_name(s_endpoint.active_route_id),
                 s_endpoint.active_route_id);
    }
    s_endpoint.active_route_id = P4_MP_UART_ROUTE_NONE;
    s_endpoint.direct_discoveries = 0U;
    p4_mp_stream_decoder_init(&s_endpoint.direct.decoder);
    p4_mp_stream_decoder_init(&s_endpoint.relay.decoder);
    if (s_endpoint.direct.ready) {
        (void)uart_flush_input(s_endpoint.direct.port);
    }
    if (s_endpoint.relay.ready) {
        (void)uart_flush_input(s_endpoint.relay.port);
    }
}

uint64_t p4_mp_uart_endpoint_active_route(void)
{
    return s_endpoint.active_route_id;
}

p4_mp_uart_status_t p4_mp_uart_endpoint_status(void)
{
    return (p4_mp_uart_status_t){
        .ready = s_endpoint.initialized,
        .relay_ready = s_endpoint.relay.ready,
        .direct_ready = s_endpoint.direct.ready,
        .active_route_id = s_endpoint.active_route_id,
        .baudrate = s_endpoint.relay.baudrate,
        .direct_baudrate = s_endpoint.direct.baudrate,
        .rx_bytes = add_saturated(
            s_endpoint.direct.rx_bytes, s_endpoint.relay.rx_bytes),
        .tx_bytes = add_saturated(
            s_endpoint.direct.tx_bytes, s_endpoint.relay.tx_bytes),
        .rx_frames = add_saturated(
            s_endpoint.direct.rx_frames, s_endpoint.relay.rx_frames),
        .tx_frames = add_saturated(
            s_endpoint.direct.tx_frames, s_endpoint.relay.tx_frames),
        .discarded_bytes = add_saturated(
            s_endpoint.direct.decoder.discarded_bytes,
            s_endpoint.relay.decoder.discarded_bytes),
        .dropped_frames = add_saturated(
            s_endpoint.direct.decoder.dropped_frames,
            s_endpoint.relay.decoder.dropped_frames),
        .last_error = s_endpoint.last_error,
    };
}
