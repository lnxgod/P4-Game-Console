// SPDX-License-Identifier: MIT

#include "p4/multiplayer_uart.h"

#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>
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

#if CONFIG_P4_BOARD_M5STACK_TAB5 && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#define P4_MP_NATIVE_USB_RELAY 1
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#else
#define P4_MP_NATIVE_USB_RELAY 0
#endif

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

#if P4_MP_NATIVE_USB_RELAY
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "USB readiness must be lock-free");
/* Separate from foreground-owned s_endpoint. Published once; the native USB
 * driver is retained until reboot, including game/route handoffs. */
static atomic_uint s_console_record_ready;
#if CONFIG_LIBC_NEWLIB
static bool console_stream_retains_lock(const FILE *stream)
{
    /* Pinned newlib vfprintf can release the original FILE lock before
     * __sbprintf for unbuffered streams (including malloc fallback). String
     * or caller-locked streams also bypass the original stdio lock. */
    return stream != NULL &&
        (stream->_flags & (__SWR | __SNBF | __SSTR)) == __SWR &&
        (stream->_flags2 & __SNLK) == 0;
}
#endif
#endif

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
#if P4_MP_NATIVE_USB_RELAY
    p4_mp_uart_channel_t *const channel = &s_endpoint.relay;
    initialize_channel(channel, UART_NUM_0, P4_MP_UART_RELAY_ROUTE_ID, 115200U, true);
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {
            .rx_buffer_size = 8192U, .tx_buffer_size = 8192U,
        };
        const esp_err_t result = usb_serial_jtag_driver_install(&config);
        if (result != ESP_OK) return result;
    }
    /* Logs and binary frames share one buffered TX owner, preserving each
     * write's bytes. No TinyUSB PHY takeover or USB-A power is involved. */
    usb_serial_jtag_vfs_use_driver();
    channel->ready = true;
    ESP_LOGI(TAG, "P4_USB_SERIAL_READY transport=native-usb-serial-jtag content=1");
    return ESP_OK;
#elif !CONFIG_ESP_CONSOLE_UART
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
#if P4_MP_NATIVE_USB_RELAY
    if (s_endpoint.relay.ready) {
        atomic_store_explicit(&s_console_record_ready, 1U, memory_order_release);
    }
#endif
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
        int count;
#if P4_MP_NATIVE_USB_RELAY
        if (channel == &s_endpoint.relay) {
            count = usb_serial_jtag_read_bytes(channel->receive,
                (uint32_t)sizeof(channel->receive), 0U);
        } else
#endif
        {
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
        count = uart_read_bytes(
            channel->port, channel->receive, (uint32_t)requested, 0U);
        }
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
    const int written =
#if P4_MP_NATIVE_USB_RELAY
        channel == &s_endpoint.relay
            ? usb_serial_jtag_write_bytes(bytes, bytes_length, pdMS_TO_TICKS(1000U))
            :
#endif
        uart_write_bytes(channel->port, bytes, bytes_length);
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

esp_err_t p4_mp_uart_endpoint_try_console_record(
    const uint8_t *bytes,
    size_t bytes_length)
{
    if (bytes == NULL || bytes_length < 3U ||
        bytes_length > P4_MP_UART_RAW_TX_MAX_BYTES ||
        bytes[0] != '\n' || bytes[bytes_length - 1U] != '\n') {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 1U; i + 1U < bytes_length; ++i) {
        if (bytes[i] < 0x20U || bytes[i] > 0x7eU) {
            return ESP_ERR_INVALID_ARG;
        }
    }
#if P4_MP_NATIVE_USB_RELAY
    if (atomic_load_explicit(&s_console_record_ready,
                             memory_order_acquire) == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_LIBC_NEWLIB
    /* Pinned newlib shares standard FILEs across tasks. ESP_LOG v1 uses
     * vprintf(stdout); Doom errors use stderr. Respect each whole stdio
     * write before bypassing VFS, whose private byte-writer lock cannot be
     * acquired here. Never wait for either stream or flush it. */
    if (ftrylockfile(stdout) != 0) return ESP_ERR_TIMEOUT;
    if (ftrylockfile(stderr) != 0) {
        funlockfile(stdout);
        return ESP_ERR_TIMEOUT;
    }
    if (!console_stream_retains_lock(stdout) ||
        !console_stream_retains_lock(stderr)) {
        funlockfile(stderr);
        funlockfile(stdout);
        return ESP_ERR_NOT_SUPPORTED;
    }
    /* IDF 5.5.3 takes its TX mutex and admits the complete ring-buffer item,
     * or returns zero. Zero ticks also rejects contention without waiting.
     * Do not use VFS: its per-byte writes can silently discard a suffix. */
    const int written = usb_serial_jtag_write_bytes(bytes, bytes_length, 0U);
    funlockfile(stderr);
    funlockfile(stdout);
    if (written == 0) return ESP_ERR_TIMEOUT;
    return written > 0 && (size_t)written == bytes_length ? ESP_OK : ESP_FAIL;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
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
    const esp_err_t result =
#if P4_MP_NATIVE_USB_RELAY
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(timeout_ms));
#else
        uart_wait_tx_done(s_endpoint.relay.port, pdMS_TO_TICKS(timeout_ms));
#endif
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
    /* CDC baud is host metadata for native USB; physical USB rate is fixed. */
    const esp_err_t result =
#if P4_MP_NATIVE_USB_RELAY
        ESP_OK;
#else
        uart_set_baudrate(s_endpoint.relay.port, baudrate);
#endif
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
#if P4_MP_NATIVE_USB_RELAY
        /* Bounded drain; do not touch an unrelated UART peripheral. */
        for (unsigned pass=0; pass<P4_MP_UART_MAX_READS_PER_POLL; ++pass) {
            if (usb_serial_jtag_read_bytes(s_endpoint.relay.receive,
                    (uint32_t)sizeof(s_endpoint.relay.receive), 0U) <= 0) break;
        }
#else
        (void)uart_flush_input(s_endpoint.relay.port);
#endif
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
