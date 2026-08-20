// SPDX-License-Identifier: GPL-2.0-or-later

#include "doom_p4mp_adapter.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_log.h"
#include "esp_timer.h"
#pragma GCC diagnostic pop
#include "d_loop.h"
#include "p4/multiplayer_uart.h"
#include "p4_doom_net.h"

enum {
    P4_DOOM_MP_KEEPALIVE_MS = 1000,
    P4_DOOM_MP_REDUNDANT_INPUT_COPIES = 2,
};

typedef struct {
    p4_mp_session_t *session;
    p4_doom_mp_launch_config_t config;
    p4_doom_mp_tic_queue_t queue;
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    int64_t next_keepalive_us;
    uint32_t inputs_sent;
    uint32_t inputs_received;
    uint32_t complete_tics;
    uint32_t rejected_inputs;
    uint32_t send_failures;
    boolean prepared;
    boolean configured;
} p4_doom_p4mp_state_t;

static const char *const TAG = "p4_doom_net";
static p4_doom_p4mp_state_t s_net;

static uint64_t now_ms(void)
{
    return (uint64_t)esp_timer_get_time() / UINT64_C(1000);
}

static uint8_t player_bit(uint8_t slot)
{
    return (uint8_t)(UINT8_C(1) << slot);
}

static p4_mp_peer_t *remote_peer(void)
{
    if (s_net.session == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < P4_MP_MAX_REMOTE_PEERS; ++index) {
        p4_mp_peer_t *const peer = &s_net.session->peers[index];
        if (peer->connected &&
            peer->peer_id == s_net.config.remote_peer_id &&
            peer->route_id == s_net.config.route_id) {
            return peer;
        }
    }
    return NULL;
}

static esp_err_t send_packet(
    p4_mp_packet_type_t type,
    uint32_t ack,
    const uint8_t *payload,
    uint16_t payload_length)
{
    if (s_net.session == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t datagram_length = 0U;
    if (p4_mp_session_encode(
            s_net.session, type, ack, payload, payload_length,
            s_net.datagram, sizeof(s_net.datagram),
            &datagram_length) != P4_MP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    return p4_mp_uart_endpoint_send(s_net.datagram, datagram_length);
}

static void flush_complete_tics(void)
{
    if (!s_net.configured) {
        return;
    }
    while (p4_doom_mp_tic_queue_ready(&s_net.queue)) {
        p4_doom_mp_tic_t source[P4_MP_MAX_PLAYERS];
        uint8_t connected_mask = 0U;
        if (!p4_doom_mp_tic_queue_pop(
                &s_net.queue, source, &connected_mask)) {
            return;
        }
        ticcmd_t commands[NET_MAXPLAYERS];
        boolean players[NET_MAXPLAYERS];
        memset(commands, 0, sizeof(commands));
        memset(players, 0, sizeof(players));
        for (uint8_t slot = 0U; slot < s_net.config.player_count; ++slot) {
            commands[slot].forwardmove = source[slot].forward_move;
            commands[slot].sidemove = source[slot].side_move;
            commands[slot].angleturn = source[slot].angle_turn;
            commands[slot].buttons = source[slot].buttons;
            commands[slot].consistancy = source[slot].consistency;
            commands[slot].chatchar = source[slot].chat_char;
            players[slot] =
                (connected_mask & player_bit(slot)) != 0U ? true : false;
        }
        D_ReceiveTic(commands, players);
        if (s_net.complete_tics != UINT32_MAX) {
            ++s_net.complete_tics;
        }
    }
}

static void disconnect_remote(uint8_t slot, const char *reason)
{
    if (slot >= s_net.config.player_count ||
        slot == s_net.config.local_player_slot) {
        return;
    }
    if (p4_doom_mp_tic_queue_disconnect(&s_net.queue, slot)) {
        ESP_LOGW(TAG,
                 "P4_DOOM_MP PEER_DISCONNECTED slot=%u reason=%s "
                 "fallback=continue-local",
                 (unsigned)slot, reason);
        flush_complete_tics();
    }
}

static void frame_received(
    void *context,
    uint64_t route_id,
    const uint8_t *datagram,
    size_t datagram_length)
{
    (void)context;
    if (!s_net.prepared || s_net.session == NULL ||
        route_id != s_net.config.route_id) {
        return;
    }
    p4_mp_event_t event;
    const p4_mp_status_t status = p4_mp_session_receive(
        s_net.session, route_id, now_ms(), datagram, datagram_length, &event);
    if (status != P4_MP_OK) {
        return;
    }
    if (event.type == P4_MP_EVENT_INPUT) {
        p4_mp_input_t input;
        p4_doom_mp_tic_t tic;
        if (event.player_slot == s_net.config.local_player_slot ||
            event.player_slot >= s_net.config.player_count ||
            p4_mp_input_decode(
                event.packet.payload, event.packet.payload_length,
                &input) != P4_MP_OK ||
            !p4_doom_mp_tic_from_input(&input, &tic) ||
            !p4_doom_mp_tic_queue_submit(
                &s_net.queue, event.player_slot, &tic)) {
            if (s_net.rejected_inputs != UINT32_MAX) {
                ++s_net.rejected_inputs;
            }
            return;
        }
        if (s_net.inputs_received != UINT32_MAX) {
            ++s_net.inputs_received;
        }
        flush_complete_tics();
    } else if (event.type == P4_MP_EVENT_PING) {
        (void)send_packet(
            P4_MP_PACKET_PONG, event.packet.sequence,
            event.packet.payload, event.packet.payload_length);
    } else if (event.type == P4_MP_EVENT_PEER_LEFT ||
               event.type == P4_MP_EVENT_REJECTED) {
        disconnect_remote(event.player_slot, "peer-left");
    }
}

esp_err_t p4_doom_p4mp_prepare(
    p4_mp_session_t *session,
    const p4_doom_mp_launch_config_t *config)
{
    memset(&s_net, 0, sizeof(s_net));
    if (session == NULL && config == NULL) {
        return ESP_OK;
    }
    if (session == NULL || config == NULL ||
        !p4_doom_mp_launch_config_valid(config) || !config->enabled ||
        config->start_tic != 0U ||
        session->state != P4_MP_SESSION_CONNECTED ||
        session->role != config->role ||
        session->session_id != config->session_id ||
        session->self_peer_id != config->self_peer_id) {
        return ESP_ERR_INVALID_ARG;
    }
    s_net.session = session;
    s_net.config = *config;
    if (remote_peer() == NULL ||
        !p4_doom_mp_tic_queue_init(
            &s_net.queue, config->player_count, config->start_tic)) {
        memset(&s_net, 0, sizeof(s_net));
        return ESP_ERR_INVALID_STATE;
    }
    s_net.prepared = true;
    const esp_err_t handler_result = p4_mp_uart_endpoint_set_handler(
        frame_received, &s_net);
    if (handler_result != ESP_OK) {
        memset(&s_net, 0, sizeof(s_net));
        return handler_result;
    }
    ESP_LOGI(TAG,
             "P4_DOOM_MP PREPARED session=%" PRIu32 " role=%s "
             "local_slot=%u players=%u transport=h1-uart-relay",
             config->session_id,
             config->role == P4_MP_ROLE_HOST ? "host" : "client",
             (unsigned)config->local_player_slot,
             (unsigned)config->player_count);
    return ESP_OK;
}

boolean P4_DoomNetActive(void)
{
    return s_net.prepared;
}

boolean P4_DoomNetConfigure(net_gamesettings_t *settings)
{
    if (!s_net.prepared || settings == NULL) {
        return false;
    }
    settings->consoleplayer = s_net.config.local_player_slot;
    settings->num_players = s_net.config.player_count;
    settings->deathmatch = 1;
    settings->loadgame = -1;
    settings->new_sync = 1;
    settings->extratics = 1;
    settings->ticdup = 1;
    memset(settings->player_classes, 0, sizeof(settings->player_classes));
    s_net.configured = true;
    ESP_LOGI(TAG,
             "P4_DOOM_MP ENGINE_CONFIG mode=deathmatch tic_hz=%u "
             "local_slot=%u players=%u input_delay=%u",
             (unsigned)P4_DOOM_MP_TICK_RATE_HZ,
             (unsigned)s_net.config.local_player_slot,
             (unsigned)s_net.config.player_count,
             (unsigned)s_net.config.input_delay_tics);
    flush_complete_tics();
    return true;
}

void P4_DoomNetSubmitTic(const ticcmd_t *command, int tic_number)
{
    if (!s_net.prepared || command == NULL || tic_number < 0) {
        return;
    }
    const p4_doom_mp_tic_t tic = {
        .tick = (uint32_t)tic_number,
        .forward_move = command->forwardmove,
        .side_move = command->sidemove,
        .angle_turn = command->angleturn,
        .buttons = command->buttons,
        .consistency = command->consistancy,
        .chat_char = command->chatchar,
    };
    if (!p4_doom_mp_tic_queue_submit(
            &s_net.queue, s_net.config.local_player_slot, &tic)) {
        return;
    }
    p4_mp_input_t input;
    uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_doom_mp_tic_to_input(&tic, &input);
    p4_mp_input_encode(&input, payload);
    bool sent = false;
    for (unsigned copy = 0U;
         copy < P4_DOOM_MP_REDUNDANT_INPUT_COPIES; ++copy) {
        if (send_packet(
                P4_MP_PACKET_INPUT, s_net.queue.next_tick,
                payload, sizeof(payload)) == ESP_OK) {
            sent = true;
            if (s_net.inputs_sent != UINT32_MAX) {
                ++s_net.inputs_sent;
            }
        }
    }
    if (!sent && s_net.send_failures != UINT32_MAX) {
        ++s_net.send_failures;
    }
    flush_complete_tics();
}

void P4_DoomNetPoll(void)
{
    if (!s_net.prepared) {
        return;
    }
    p4_mp_uart_endpoint_poll();
    const int64_t now_us = esp_timer_get_time();
    p4_mp_event_t timeout_event;
    if (s_net.session != NULL &&
        p4_mp_session_tick(
            s_net.session, (uint64_t)now_us / UINT64_C(1000),
            &timeout_event)) {
        disconnect_remote(timeout_event.player_slot, "timeout");
    }
    if (s_net.session != NULL && remote_peer() != NULL &&
        (s_net.next_keepalive_us == 0 ||
         now_us >= s_net.next_keepalive_us)) {
        uint8_t keepalive[8];
        const uint64_t stamp = (uint64_t)now_us / UINT64_C(1000);
        for (unsigned index = 0U; index < sizeof(keepalive); ++index) {
            keepalive[index] = (uint8_t)(stamp >> (index * 8U));
        }
        (void)send_packet(
            P4_MP_PACKET_PING, s_net.queue.next_tick,
            keepalive, sizeof(keepalive));
        s_net.next_keepalive_us = now_us +
            (int64_t)P4_DOOM_MP_KEEPALIVE_MS * 1000;
    }
    flush_complete_tics();
}

void P4_DoomNetQuit(void)
{
    if (s_net.prepared && s_net.session != NULL &&
        remote_peer() != NULL) {
        const uint8_t reason[2] = {0U, 0U};
        (void)send_packet(P4_MP_PACKET_LEAVE, 0U, reason, sizeof(reason));
    }
    if (s_net.prepared) {
        ESP_LOGI(TAG,
                 "P4_DOOM_MP STOP complete_tics=%" PRIu32
                 " tx_inputs=%" PRIu32 " rx_inputs=%" PRIu32
                 " rejected=%" PRIu32 " send_failures=%" PRIu32,
                 s_net.complete_tics, s_net.inputs_sent,
                 s_net.inputs_received, s_net.rejected_inputs,
                 s_net.send_failures);
    }
    s_net.prepared = false;
    s_net.configured = false;
}
