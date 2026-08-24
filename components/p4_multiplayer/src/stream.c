// SPDX-License-Identifier: MIT

#include "p4/multiplayer.h"

#include <string.h>

static const uint8_t P4_MP_STREAM_MAGIC[4] = {'P', '4', 'M', 'P'};

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static void reset_frame(p4_mp_stream_decoder_t *decoder)
{
    decoder->buffered_bytes = 0U;
    decoder->expected_bytes = 0U;
    decoder->magic_bytes = 0U;
}

bool p4_mp_wired_transport_info(
    p4_mp_wired_transport_kind_t kind,
    p4_mp_wired_transport_info_t *info_out)
{
    if (info_out == NULL) {
        return false;
    }
    *info_out = (p4_mp_wired_transport_info_t){0};
    switch (kind) {
        case P4_MP_WIRED_TRANSPORT_UART_DIRECT:
            *info_out = (p4_mp_wired_transport_info_t){
                .kind = kind,
                .name = "uart-direct",
                .requires_host_relay = false,
                .console_is_usb_device = false,
                .console_sources_vbus = false,
            };
            return true;
        case P4_MP_WIRED_TRANSPORT_UART_RELAY:
            *info_out = (p4_mp_wired_transport_info_t){
                .kind = kind,
                .name = "uart-host-relay",
                .requires_host_relay = true,
                .console_is_usb_device = false,
                .console_sources_vbus = false,
            };
            return true;
        case P4_MP_WIRED_TRANSPORT_USB2_DEVICE_RELAY:
            *info_out = (p4_mp_wired_transport_info_t){
                .kind = kind,
                .name = "usb2-device-host-relay",
                .requires_host_relay = true,
                .console_is_usb_device = true,
                .console_sources_vbus = false,
            };
            return true;
        case P4_MP_WIRED_TRANSPORT_NONE:
        default:
            return false;
    }
}

void p4_mp_stream_decoder_init(p4_mp_stream_decoder_t *decoder)
{
    if (decoder != NULL) {
        *decoder = (p4_mp_stream_decoder_t){0};
    }
}

static bool seek_magic(p4_mp_stream_decoder_t *decoder, uint8_t byte)
{
    if (byte == P4_MP_STREAM_MAGIC[decoder->magic_bytes]) {
        ++decoder->magic_bytes;
        if (decoder->magic_bytes == sizeof(P4_MP_STREAM_MAGIC)) {
            memcpy(decoder->datagram, P4_MP_STREAM_MAGIC,
                   sizeof(P4_MP_STREAM_MAGIC));
            decoder->buffered_bytes = sizeof(P4_MP_STREAM_MAGIC);
            decoder->magic_bytes = 0U;
            return true;
        }
        return false;
    }

    decoder->discarded_bytes += decoder->magic_bytes;
    decoder->magic_bytes = byte == P4_MP_STREAM_MAGIC[0] ? 1U : 0U;
    if (decoder->magic_bytes == 0U) {
        ++decoder->discarded_bytes;
    }
    return false;
}

p4_mp_stream_result_t p4_mp_stream_consume(
    p4_mp_stream_decoder_t *decoder,
    const uint8_t *bytes,
    size_t bytes_length,
    size_t *bytes_consumed,
    uint8_t *datagram_out,
    size_t datagram_capacity,
    size_t *datagram_length)
{
    if (decoder == NULL || bytes_consumed == NULL || datagram_out == NULL ||
        datagram_length == NULL ||
        (bytes == NULL && bytes_length != 0U) ||
        datagram_capacity < P4_MP_MAX_DATAGRAM_BYTES) {
        return P4_MP_STREAM_INVALID_ARGUMENT;
    }
    *bytes_consumed = 0U;
    *datagram_length = 0U;

    for (size_t index = 0U; index < bytes_length; ++index) {
        const uint8_t byte = bytes[index];
        *bytes_consumed = index + 1U;
        if (decoder->buffered_bytes == 0U) {
            (void)seek_magic(decoder, byte);
            continue;
        }

        if (decoder->buffered_bytes >= sizeof(decoder->datagram)) {
            decoder->last_packet_status = P4_MP_BAD_LENGTH;
            ++decoder->dropped_frames;
            reset_frame(decoder);
            return P4_MP_STREAM_FRAME_DROPPED;
        }
        decoder->datagram[decoder->buffered_bytes++] = byte;
        if (decoder->buffered_bytes == P4_MP_HEADER_BYTES) {
            const uint16_t payload_length = read_u16(decoder->datagram + 24U);
            decoder->expected_bytes = P4_MP_HEADER_BYTES +
                (size_t)payload_length + P4_MP_TRAILER_BYTES;
            if (payload_length > P4_MP_MAX_PAYLOAD_BYTES ||
                decoder->expected_bytes > P4_MP_MAX_DATAGRAM_BYTES) {
                decoder->last_packet_status = P4_MP_BAD_LENGTH;
                ++decoder->dropped_frames;
                reset_frame(decoder);
                return P4_MP_STREAM_FRAME_DROPPED;
            }
        }

        if (decoder->expected_bytes != 0U &&
            decoder->buffered_bytes == decoder->expected_bytes) {
            p4_mp_packet_view_t packet;
            const p4_mp_status_t status = p4_mp_packet_decode(
                decoder->datagram, decoder->buffered_bytes, &packet);
            decoder->last_packet_status = status;
            if (status != P4_MP_OK) {
                ++decoder->dropped_frames;
                reset_frame(decoder);
                return P4_MP_STREAM_FRAME_DROPPED;
            }
            *datagram_length = decoder->buffered_bytes;
            memcpy(datagram_out, decoder->datagram, *datagram_length);
            reset_frame(decoder);
            return P4_MP_STREAM_FRAME_READY;
        }
    }
    return P4_MP_STREAM_NEED_MORE;
}
