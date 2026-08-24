// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef CONSOLE_DOOM_P4MP_ADAPTER_H
#define CONSOLE_DOOM_P4MP_ADAPTER_H

#include "esp_err.h"
#include "p4/doom_multiplayer.h"
#include "p4/multiplayer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*p4_doom_p4mp_frame_handler_t)(
    void *handler_context,
    uint64_t route_id,
    const uint8_t *datagram,
    size_t datagram_length);

/** Transport callbacks keep Doom lockstep independent of UART or BLE. */
typedef struct {
    void *context;
    esp_err_t (*set_handler)(
        void *context,
        p4_doom_p4mp_frame_handler_t handler,
        void *handler_context);
    void (*poll)(void *context);
    esp_err_t (*send)(
        void *context,
        const uint8_t *datagram,
        size_t datagram_length);
    bool (*connected)(void *context, uint64_t route_id);
    const char *(*route_name)(void *context, uint64_t route_id);
} p4_doom_p4mp_transport_t;

/** Transfer the already-connected Console OS P4MP session into Doom. */
esp_err_t p4_doom_p4mp_prepare(
    p4_mp_session_t *session,
    const p4_doom_mp_launch_config_t *config,
    const p4_doom_p4mp_transport_t *transport);

#ifdef __cplusplus
}
#endif

#endif
