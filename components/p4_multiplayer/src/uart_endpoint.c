// SPDX-License-Identifier: MIT

#include "p4/multiplayer_uart.h"

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

enum {
    P4_MP_UART_RX_RING_BYTES = 2048,
    P4_MP_UART_READ_BYTES = 256,
    P4_MP_UART_MAX_READS_PER_POLL = 8,
};

static const char *TAG = "p4_mp_uart";

typedef struct {
    p4_mp_stream_decoder_t decoder;
    p4_mp_uart_frame_handler_t handler;
    void *handler_context;
    p4_mp_uart_raw_handler_t raw_handler;
    void *raw_handler_context;
    p4_mp_uart_status_t status;
    uint8_t receive[P4_MP_UART_READ_BYTES];
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
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
#if !CONFIG_ESP_CONSOLE_UART
    (void)handler_context;
    return ESP_ERR_NOT_SUPPORTED;
#else
    const uart_port_t port = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
    if (!uart_is_driver_installed(port)) {
        const esp_err_t install = uart_driver_install(
            port, P4_MP_UART_RX_RING_BYTES, 0, 0, NULL, 0);
        if (install != ESP_OK) {
            return install;
        }
    }
    const esp_err_t baud = uart_set_baudrate(
        port, CONFIG_ESP_CONSOLE_UART_BAUDRATE);
    if (baud != ESP_OK) {
        return baud;
    }
    (void)uart_flush_input(port);
    memset(&s_endpoint, 0, sizeof(s_endpoint));
    p4_mp_stream_decoder_init(&s_endpoint.decoder);
    s_endpoint.handler = handler;
    s_endpoint.handler_context = handler_context;
    s_endpoint.status.ready = true;
    s_endpoint.status.baudrate = CONFIG_ESP_CONSOLE_UART_BAUDRATE;
    s_endpoint.status.last_error = ESP_OK;
    s_endpoint.initialized = true;
    ESP_LOGI(TAG,
             "P4_MP_UART_READY transport=h1-ch343-uart baud=%u "
             "route=%u rx_ring=%u frame_max=%u host_relay=required",
             (unsigned)CONFIG_ESP_CONSOLE_UART_BAUDRATE,
             (unsigned)P4_MP_UART_RELAY_ROUTE_ID,
             (unsigned)P4_MP_UART_RX_RING_BYTES,
             (unsigned)P4_MP_MAX_DATAGRAM_BYTES);
    return ESP_OK;
#endif
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
    s_endpoint.raw_handler = handler;
    s_endpoint.raw_handler_context =
        handler == NULL ? NULL : handler_context;
    return ESP_OK;
}

void p4_mp_uart_endpoint_poll(void)
{
#if CONFIG_ESP_CONSOLE_UART
    if (!s_endpoint.initialized) {
        return;
    }
    const uart_port_t port = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
    for (unsigned pass = 0U;
         pass < P4_MP_UART_MAX_READS_PER_POLL;
         ++pass) {
        size_t available = 0U;
        esp_err_t result = uart_get_buffered_data_len(port, &available);
        if (result != ESP_OK) {
            s_endpoint.status.last_error = result;
            return;
        }
        if (available == 0U) {
            break;
        }
        size_t requested = available;
        if (requested > sizeof(s_endpoint.receive)) {
            requested = sizeof(s_endpoint.receive);
        }
        const int count = uart_read_bytes(
            port, s_endpoint.receive, (uint32_t)requested, 0U);
        if (count <= 0) {
            break;
        }
        add_counter(&s_endpoint.status.rx_bytes, (size_t)count);
        if (s_endpoint.raw_handler != NULL &&
            s_endpoint.raw_handler(
                s_endpoint.raw_handler_context,
                s_endpoint.receive,
                (size_t)count)) {
            /* A raw session owns complete receive blocks until it terminates.
             * Drop any partial multiplayer frame from before that handoff. */
            p4_mp_stream_decoder_init(&s_endpoint.decoder);
            continue;
        }
        size_t offset = 0U;
        while (offset < (size_t)count) {
            size_t consumed = 0U;
            size_t datagram_length = 0U;
            const p4_mp_stream_result_t stream_result =
                p4_mp_stream_consume(
                    &s_endpoint.decoder,
                    s_endpoint.receive + offset,
                    (size_t)count - offset,
                    &consumed,
                    s_endpoint.datagram,
                    sizeof(s_endpoint.datagram),
                    &datagram_length);
            if (consumed == 0U) {
                s_endpoint.status.last_error = ESP_FAIL;
                return;
            }
            offset += consumed;
            if (stream_result == P4_MP_STREAM_FRAME_READY) {
                add_counter(&s_endpoint.status.rx_frames, 1U);
                s_endpoint.handler(
                    s_endpoint.handler_context,
                    P4_MP_UART_RELAY_ROUTE_ID,
                    s_endpoint.datagram,
                    datagram_length);
            } else if (stream_result == P4_MP_STREAM_INVALID_ARGUMENT) {
                s_endpoint.status.last_error = ESP_ERR_INVALID_ARG;
                return;
            }
        }
    }
    s_endpoint.status.discarded_bytes =
        s_endpoint.decoder.discarded_bytes;
    s_endpoint.status.dropped_frames =
        s_endpoint.decoder.dropped_frames;
#endif
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
#if !CONFIG_ESP_CONSOLE_UART
    return ESP_ERR_NOT_SUPPORTED;
#else
    const int written = uart_write_bytes(
        (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM,
        datagram,
        datagram_length);
    if (written < 0 || (size_t)written != datagram_length) {
        s_endpoint.status.last_error = ESP_FAIL;
        return ESP_FAIL;
    }
    add_counter(&s_endpoint.status.tx_bytes, datagram_length);
    add_counter(&s_endpoint.status.tx_frames, 1U);
    s_endpoint.status.last_error = ESP_OK;
    return ESP_OK;
#endif
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
#if !CONFIG_ESP_CONSOLE_UART
    return ESP_ERR_NOT_SUPPORTED;
#else
    const int written = uart_write_bytes(
        (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM,
        bytes,
        bytes_length);
    if (written < 0 || (size_t)written != bytes_length) {
        s_endpoint.status.last_error = ESP_FAIL;
        return ESP_FAIL;
    }
    add_counter(&s_endpoint.status.tx_bytes, bytes_length);
    s_endpoint.status.last_error = ESP_OK;
    return ESP_OK;
#endif
}

esp_err_t p4_mp_uart_endpoint_wait_tx_done(uint32_t timeout_ms)
{
    if (!s_endpoint.initialized || timeout_ms == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
#if !CONFIG_ESP_CONSOLE_UART
    return ESP_ERR_NOT_SUPPORTED;
#else
    const esp_err_t result = uart_wait_tx_done(
        (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM,
        pdMS_TO_TICKS(timeout_ms));
    s_endpoint.status.last_error = result;
    return result;
#endif
}

esp_err_t p4_mp_uart_endpoint_set_baudrate(uint32_t baudrate)
{
    if (!s_endpoint.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (baudrate < 9600U || baudrate > 2000000U) {
        return ESP_ERR_INVALID_ARG;
    }
#if !CONFIG_ESP_CONSOLE_UART
    return ESP_ERR_NOT_SUPPORTED;
#else
    const esp_err_t result = uart_set_baudrate(
        (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM,
        baudrate);
    if (result == ESP_OK) {
        s_endpoint.status.baudrate = baudrate;
    }
    s_endpoint.status.last_error = result;
    return result;
#endif
}

p4_mp_uart_status_t p4_mp_uart_endpoint_status(void)
{
    return s_endpoint.status;
}
