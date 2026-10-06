// SPDX-License-Identifier: GPL-2.0-or-later

#include "doom_p4mp_adapter.h"
#include "doom_gc_p4mp.h"

#include <inttypes.h>
#include <limits.h>
#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "d_loop.h"
#include "p4_doom_net.h"

enum {
    P4_DOOM_MP_KEEPALIVE_MS = 500,
    P4_DOOM_MP_RETRY_INTERVAL_MS = 75,
    P4_DOOM_MP_STALLED_RETRY_INTERVAL_MS = 20,
    P4_DOOM_MP_STALL_THRESHOLD_MS = 250,
    P4_DOOM_MP_STALL_LOG_INTERVAL_MS = 1000,
    P4_DOOM_MP_STALL_ABORT_MS = 10000,
    P4_DOOM_MP_READY_INTERVAL_MS = 100,
    P4_DOOM_MP_READY_TIMEOUT_MS = 30000,
    P4_DOOM_MP_READY_SETTLE_MS = 100,
    P4_DOOM_MP_HANDOFF_TIMEOUT_MS = 60000,
    P4_DOOM_MP_BOOTSTRAP_INTERVAL_MS = 100,
    P4_DOOM_MP_BOOTSTRAP_TIMEOUT_MS = 5000,
    P4_DOOM_MP_STATS_INTERVAL_TICS = P4_DOOM_MP_TICK_RATE_HZ * 10,
};

typedef struct {
    p4_mp_session_t *session;
    p4_doom_mp_launch_config_t config;
    p4_doom_p4mp_transport_t transport;
    p4_doom_mp_tic_queue_t queue;
    p4_doom_mp_tx_window_t tx_window;
    p4_doom_mp_engine_barrier_t engine_barrier;
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    int64_t next_keepalive_us;
    int64_t next_retry_us;
    int64_t last_progress_us;
    int64_t next_stall_log_us;
    uint32_t inputs_sent;
    uint32_t inputs_received;
    uint32_t complete_tics;
    uint32_t rejected_inputs;
    uint32_t send_failures;
    uint32_t retransmits;
    uint32_t ack_updates;
    uint32_t invalid_acks;
    uint32_t tx_window_failures;
    uint32_t next_stats_tic;
    uint32_t saved_session_timeout_ms;
    boolean prepared;
    boolean configured;
    boolean runtime_started;
    boolean peer_failed;
} p4_doom_p4mp_state_t;

static const char *const TAG = "p4_doom_net";
static p4_doom_p4mp_state_t s_net;

static void disconnect_remote(uint8_t slot, const char *reason);

static void restore_session_timeout(void)
{
    if (s_net.session != NULL && s_net.saved_session_timeout_ms != 0U) {
        s_net.session->timeout_ms = s_net.saved_session_timeout_ms;
        s_net.saved_session_timeout_ms = 0U;
    }
}

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
    return s_net.transport.send(
        s_net.transport.context, s_net.datagram, datagram_length);
}

static bool send_input_tic(const p4_doom_mp_tic_t *tic)
{
    if (tic == NULL) {
        return false;
    }
    p4_mp_input_t input;
    uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_doom_mp_tic_to_input(tic, &input);
    p4_mp_input_encode(&input, payload);
    if (send_packet(
            P4_MP_PACKET_INPUT, s_net.queue.next_tick,
            payload, sizeof(payload)) != ESP_OK) {
        if (s_net.send_failures != UINT32_MAX) {
            ++s_net.send_failures;
        }
        return false;
    }
    if (s_net.inputs_sent != UINT32_MAX) {
        ++s_net.inputs_sent;
    }
    return true;
}

static bool track_local_tic(const p4_doom_mp_tic_t *tic)
{
    if (!p4_doom_mp_tx_window_track(&s_net.tx_window, tic)) {
        if (s_net.tx_window_failures != UINT32_MAX) {
            ++s_net.tx_window_failures;
        }
        ESP_LOGE(TAG,
                 "P4_DOOM_MP TX_WINDOW_FAILED tick=%" PRIu32
                 " peer_ack=%" PRIu32 " next_local=%" PRIu32
                 " pending=%u",
                 tic == NULL ? 0U : tic->tick,
                 s_net.tx_window.peer_ack,
                 s_net.tx_window.next_local_tick,
                 (unsigned)s_net.tx_window.pending_count);
        s_net.peer_failed = true;
        return false;
    }
    if (s_net.next_retry_us == 0) {
        s_net.next_retry_us = esp_timer_get_time() +
            (int64_t)P4_DOOM_MP_RETRY_INTERVAL_MS * 1000;
    }
    return true;
}

static void observe_peer_ack(uint32_t peer_ack, int64_t now_us)
{
    const uint32_t previous = s_net.tx_window.peer_ack;
    if (!p4_doom_mp_tx_window_acknowledge(
            &s_net.tx_window, peer_ack)) {
        if (s_net.invalid_acks != UINT32_MAX) {
            ++s_net.invalid_acks;
        }
        return;
    }
    if (s_net.tx_window.peer_ack == previous) {
        return;
    }
    if (s_net.ack_updates != UINT32_MAX) {
        ++s_net.ack_updates;
    }
    if (p4_doom_mp_tx_window_pending(&s_net.tx_window) == 0U) {
        s_net.next_retry_us = 0;
    } else {
        s_net.next_retry_us = now_us +
            (int64_t)P4_DOOM_MP_RETRY_INTERVAL_MS * 1000;
    }
}

static void retry_oldest_unacknowledged(int64_t now_us)
{
    const size_t pending =
        p4_doom_mp_tx_window_pending(&s_net.tx_window);
    if (pending == 0U) {
        s_net.next_retry_us = 0;
        return;
    }
    if (s_net.next_retry_us == 0) {
        s_net.next_retry_us = now_us +
            (int64_t)P4_DOOM_MP_RETRY_INTERVAL_MS * 1000;
        return;
    }
    if (now_us < s_net.next_retry_us) {
        return;
    }
    const int64_t stalled_us = s_net.runtime_started
        ? now_us - s_net.last_progress_us : 0;
    const unsigned interval_ms =
        stalled_us >= (int64_t)P4_DOOM_MP_STALL_THRESHOLD_MS * 1000
            ? P4_DOOM_MP_STALLED_RETRY_INTERVAL_MS
            : P4_DOOM_MP_RETRY_INTERVAL_MS;
    p4_doom_mp_tic_t retry;
    if (p4_doom_mp_tx_window_oldest(&s_net.tx_window, &retry) &&
        send_input_tic(&retry) && s_net.retransmits != UINT32_MAX) {
        ++s_net.retransmits;
    }
    s_net.next_retry_us = now_us + (int64_t)interval_ms * 1000;

    if (s_net.runtime_started &&
        stalled_us >= (int64_t)P4_DOOM_MP_STALL_THRESHOLD_MS * 1000 &&
        (s_net.next_stall_log_us == 0 ||
         now_us >= s_net.next_stall_log_us)) {
        ESP_LOGW(TAG,
                 "P4_DOOM_MP LOCKSTEP_RECOVERY stalled_ms=%" PRIi64
                 " next=%" PRIu32 " peer_ack=%" PRIu32
                 " pending=%u retries=%" PRIu32
                 " tx=%" PRIu32 " rx=%" PRIu32
                 " rejected=%" PRIu32 " send_failures=%" PRIu32,
                 stalled_us / 1000, s_net.queue.next_tick,
                 s_net.tx_window.peer_ack, (unsigned)pending,
                 s_net.retransmits, s_net.inputs_sent,
                 s_net.inputs_received, s_net.rejected_inputs,
                 s_net.send_failures);
        s_net.next_stall_log_us = now_us +
            (int64_t)P4_DOOM_MP_STALL_LOG_INTERVAL_MS * 1000;
    }
}

static void fail_lockstep_if_stalled(int64_t now_us)
{
    if (!s_net.runtime_started || s_net.peer_failed ||
        now_us - s_net.last_progress_us <
            (int64_t)P4_DOOM_MP_STALL_ABORT_MS * 1000) {
        return;
    }
    p4_mp_peer_t *const peer = remote_peer();
    const uint8_t remote_slot = peer != NULL
        ? peer->player_slot
        : (uint8_t)(s_net.config.local_player_slot == 0U ? 1U : 0U);
    disconnect_remote(remote_slot, "lockstep-timeout");
}

static void count_send_failure(void)
{
    if (s_net.send_failures != UINT32_MAX) {
        ++s_net.send_failures;
    }
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
        s_net.last_progress_us = esp_timer_get_time();
        s_net.next_stall_log_us = 0;
        if (s_net.complete_tics != UINT32_MAX) {
            ++s_net.complete_tics;
        }
        if (s_net.next_stats_tic != 0U &&
            s_net.complete_tics >= s_net.next_stats_tic) {
            ESP_LOGI(TAG,
                     "P4_DOOM_MP STATS complete_tics=%" PRIu32
                     " next_tic=%" PRIu32 " peer_ack=%" PRIu32
                     " pending=%u tx=%" PRIu32 " rx=%" PRIu32
                     " retries=%" PRIu32 " rejected=%" PRIu32
                     " send_failures=%" PRIu32
                     " tx_window_failures=%" PRIu32,
                     s_net.complete_tics, s_net.queue.next_tick,
                     s_net.tx_window.peer_ack,
                     (unsigned)p4_doom_mp_tx_window_pending(
                         &s_net.tx_window),
                     s_net.inputs_sent, s_net.inputs_received,
                     s_net.retransmits, s_net.rejected_inputs,
                     s_net.send_failures, s_net.tx_window_failures);
            if (UINT32_MAX - s_net.next_stats_tic <
                P4_DOOM_MP_STATS_INTERVAL_TICS) {
                s_net.next_stats_tic = UINT32_MAX;
            } else {
                s_net.next_stats_tic += P4_DOOM_MP_STATS_INTERVAL_TICS;
            }
        }
    }
}

static bool bootstrap_initial_tics(void)
{
    const uint32_t bootstrap_count =
        s_net.config.input_delay_tics == 0U
            ? UINT32_C(1)
            : (uint32_t)s_net.config.input_delay_tics;
    const uint32_t first_tick = s_net.config.start_tic;
    const uint32_t target_tick = first_tick + bootstrap_count;

    for (uint32_t tick = first_tick; tick < target_tick; ++tick) {
        const p4_doom_mp_tic_t neutral = {.tick = tick};
        if (!p4_doom_mp_tic_queue_submit(
                &s_net.queue, s_net.config.local_player_slot, &neutral) ||
            !track_local_tic(&neutral)) {
            ESP_LOGE(TAG,
                     "P4_DOOM_MP TIC_BOOTSTRAP_QUEUE_FAILED tick=%" PRIu32,
                     tick);
            return false;
        }
    }

    ESP_LOGI(TAG,
             "P4_DOOM_MP TIC_BOOTSTRAP_WAIT first=%" PRIu32
             " count=%" PRIu32 " timeout_ms=%u",
             first_tick, bootstrap_count,
             (unsigned)P4_DOOM_MP_BOOTSTRAP_TIMEOUT_MS);
    const int64_t started_us = esp_timer_get_time();
    int64_t next_send_us = 0;
    while (s_net.queue.next_tick < target_tick) {
        const int64_t now_us = esp_timer_get_time();
        if (next_send_us == 0 || now_us >= next_send_us) {
            for (uint32_t tick = first_tick; tick < target_tick; ++tick) {
                const p4_doom_mp_tic_t neutral = {.tick = tick};
                (void)send_input_tic(&neutral);
            }
            next_send_us = now_us +
                (int64_t)P4_DOOM_MP_BOOTSTRAP_INTERVAL_MS * 1000;
        }
        P4_DoomNetPoll();
        if (s_net.peer_failed || remote_peer() == NULL) {
            ESP_LOGE(TAG,
                     "P4_DOOM_MP TIC_BOOTSTRAP_ABORT reason=peer-lost");
            return false;
        }
        if (now_us - started_us >=
            (int64_t)P4_DOOM_MP_BOOTSTRAP_TIMEOUT_MS * 1000) {
            ESP_LOGE(TAG,
                     "P4_DOOM_MP TIC_BOOTSTRAP_TIMEOUT next=%" PRIu32
                     " target=%" PRIu32 " rx_inputs=%" PRIu32,
                     s_net.queue.next_tick, target_tick,
                     s_net.inputs_received);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(2U));
    }
    ESP_LOGI(TAG,
             "P4_DOOM_MP TIC_BOOTSTRAP_READY next=%" PRIu32
             " complete=%" PRIu32,
             s_net.queue.next_tick, s_net.complete_tics);
    return true;
}

static void disconnect_remote(uint8_t slot, const char *reason)
{
    if (slot >= s_net.config.player_count ||
        slot == s_net.config.local_player_slot) {
        return;
    }
    s_net.peer_failed = true;
    ESP_LOGE(TAG,
             "P4_DOOM_MP PEER_DISCONNECTED slot=%u reason=%s "
             "action=abort-match",
             (unsigned)slot, reason);
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
    if (event.type == P4_MP_EVENT_INPUT ||
        event.type == P4_MP_EVENT_PING) {
        observe_peer_ack(event.packet.ack, esp_timer_get_time());
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
        uint8_t response[P4_DOOM_MP_ENGINE_CONTROL_BYTES];
        const p4_doom_mp_engine_control_t control =
            p4_doom_mp_engine_barrier_observe_ping(
                &s_net.engine_barrier,
                event.packet.payload, event.packet.payload_length);
        if (control == P4_DOOM_MP_ENGINE_CONTROL_ACK &&
            p4_doom_mp_engine_control_encode(control, response)) {
            (void)send_packet(
                P4_MP_PACKET_PONG, event.packet.sequence,
                response, sizeof(response));
        } else {
            (void)send_packet(
                P4_MP_PACKET_PONG, event.packet.sequence,
                event.packet.payload, event.packet.payload_length);
        }
    } else if (event.type == P4_MP_EVENT_PONG) {
        p4_doom_mp_engine_barrier_observe_pong(
            &s_net.engine_barrier,
            event.packet.payload, event.packet.payload_length);
    } else if (event.type == P4_MP_EVENT_PEER_LEFT ||
               event.type == P4_MP_EVENT_REJECTED) {
        disconnect_remote(event.player_slot, "peer-left");
    }
}

esp_err_t p4_doom_p4mp_prepare(
    p4_mp_session_t *session,
    const p4_doom_mp_launch_config_t *config,
    const p4_doom_p4mp_transport_t *transport)
{
    restore_session_timeout();
    memset(&s_net, 0, sizeof(s_net));
    (void)p4_doom_gc_prepare(NULL, NULL, NULL);
    if (config != NULL && config->setup.game == P4_DOOM_MP_GAME_GAME_CHANGERS_AI)
        return p4_doom_gc_prepare(session, config, transport);
    if (session == NULL && config == NULL && transport == NULL) {
        return ESP_OK;
    }
    if (session == NULL || config == NULL || transport == NULL ||
        transport->set_handler == NULL || transport->poll == NULL ||
        transport->send == NULL || transport->connected == NULL ||
        transport->route_name == NULL ||
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
    s_net.transport = *transport;
    if (remote_peer() == NULL ||
        !p4_doom_mp_tic_queue_init(
            &s_net.queue, config->player_count, config->start_tic)) {
        memset(&s_net, 0, sizeof(s_net));
        return ESP_ERR_INVALID_STATE;
    }
    p4_doom_mp_tx_window_init(&s_net.tx_window, config->start_tic);
    s_net.last_progress_us = esp_timer_get_time();
    s_net.next_stats_tic = P4_DOOM_MP_STATS_INTERVAL_TICS;
    s_net.prepared = true;
    const esp_err_t handler_result = s_net.transport.set_handler(
        s_net.transport.context, frame_received, &s_net);
    if (handler_result != ESP_OK) {
        memset(&s_net, 0, sizeof(s_net));
        return handler_result;
    }
    s_net.saved_session_timeout_ms = session->timeout_ms;
    session->timeout_ms = P4_DOOM_MP_HANDOFF_TIMEOUT_MS;
    ESP_LOGI(TAG,
             "P4_DOOM_MP PREPARED session=%" PRIu32 " role=%s "
             "local_slot=%u players=%u transport=%s route=%" PRIu64 " "
             "handoff_timeout_ms=%u",
             config->session_id,
             config->role == P4_MP_ROLE_HOST ? "host" : "client",
             (unsigned)config->local_player_slot,
             (unsigned)config->player_count,
             s_net.transport.route_name(
                 s_net.transport.context, config->route_id),
             config->route_id,
             (unsigned)P4_DOOM_MP_HANDOFF_TIMEOUT_MS);
    return ESP_OK;
}

boolean P4_DoomNetActive(void)
{
    return s_net.prepared || p4_doom_gc_active();
}

boolean P4_DoomNetConfigure(net_gamesettings_t *settings)
{
    if (p4_doom_gc_active()) return p4_doom_gc_configure(settings);
    if (!s_net.prepared || settings == NULL) {
        return false;
    }
    settings->consoleplayer = s_net.config.local_player_slot;
    settings->num_players = s_net.config.player_count;
    settings->deathmatch = (int)s_net.config.setup.mode;
    settings->episode = (int)s_net.config.setup.episode;
    settings->map = (int)s_net.config.setup.map;
    settings->skill = (int)s_net.config.setup.skill - 1;
    settings->loadgame = -1;
    settings->nomonsters = s_net.config.setup.no_monsters ? 1 : 0;
    settings->fast_monsters = s_net.config.setup.fast_monsters ? 1 : 0;
    settings->respawn_monsters =
        s_net.config.setup.respawn_monsters ? 1 : 0;
    settings->timelimit = (int)s_net.config.setup.time_limit_minutes;
    settings->new_sync = 1;
    settings->extratics = 1;
    settings->ticdup = 1;
    memset(settings->player_classes, 0, sizeof(settings->player_classes));
    p4_doom_mp_engine_barrier_begin(&s_net.engine_barrier);
    uint8_t ready[P4_DOOM_MP_ENGINE_CONTROL_BYTES];
    if (!p4_doom_mp_engine_control_encode(
            P4_DOOM_MP_ENGINE_CONTROL_READY, ready)) {
        p4_doom_mp_engine_barrier_init(&s_net.engine_barrier);
        restore_session_timeout();
        return false;
    }
    ESP_LOGI(TAG,
             "P4_DOOM_MP ENGINE_BARRIER_WAIT local_slot=%u timeout_ms=%u",
             (unsigned)s_net.config.local_player_slot,
             (unsigned)P4_DOOM_MP_READY_TIMEOUT_MS);
    const int64_t started_us = esp_timer_get_time();
    int64_t next_ready_us = 0;
    int64_t ready_since_us = 0;
    while (true) {
        const int64_t now_us = esp_timer_get_time();
        if (next_ready_us == 0 || now_us >= next_ready_us) {
            if (send_packet(
                    P4_MP_PACKET_PING, s_net.queue.next_tick,
                    ready, sizeof(ready)) != ESP_OK &&
                s_net.send_failures != UINT32_MAX) {
                count_send_failure();
            }
            next_ready_us = now_us +
                (int64_t)P4_DOOM_MP_READY_INTERVAL_MS * 1000;
        }
        P4_DoomNetPoll();
        if (s_net.peer_failed || remote_peer() == NULL) {
            ESP_LOGE(TAG,
                     "P4_DOOM_MP ENGINE_BARRIER_ABORT local_slot=%u "
                     "reason=peer-lost",
                     (unsigned)s_net.config.local_player_slot);
            p4_doom_mp_engine_barrier_init(&s_net.engine_barrier);
            restore_session_timeout();
            return false;
        }
        if (p4_doom_mp_engine_barrier_complete(&s_net.engine_barrier)) {
            if (ready_since_us == 0) {
                ready_since_us = now_us;
            } else if (now_us - ready_since_us >=
                       (int64_t)P4_DOOM_MP_READY_SETTLE_MS * 1000) {
                break;
            }
        }
        if (now_us - started_us >=
            (int64_t)P4_DOOM_MP_READY_TIMEOUT_MS * 1000) {
            ESP_LOGE(TAG,
                     "P4_DOOM_MP ENGINE_BARRIER_TIMEOUT local_slot=%u "
                     "peer_ready=%u peer_ack=%u",
                     (unsigned)s_net.config.local_player_slot,
                     s_net.engine_barrier.peer_ready ? 1U : 0U,
                     s_net.engine_barrier.peer_acknowledged ? 1U : 0U);
            p4_doom_mp_engine_barrier_init(&s_net.engine_barrier);
            restore_session_timeout();
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(5U));
    }
    s_net.configured = true;
    ESP_LOGI(TAG,
             "P4_DOOM_MP ENGINE_BARRIER_READY local_slot=%u wait_ms=%" PRIi64,
             (unsigned)s_net.config.local_player_slot,
             (esp_timer_get_time() - started_us) / 1000);
    if (!bootstrap_initial_tics()) {
        s_net.configured = false;
        p4_doom_mp_engine_barrier_init(&s_net.engine_barrier);
        restore_session_timeout();
        return false;
    }
    restore_session_timeout();
    ESP_LOGI(TAG,
             "P4_DOOM_MP ENGINE_CONFIG mode=%u episode=%u map=%u skill=%u "
             "monsters=%u fast=%u respawn=%u limit=%u tic_hz=%u "
             "local_slot=%u players=%u input_delay=%u",
             (unsigned)s_net.config.setup.mode,
             (unsigned)s_net.config.setup.episode,
             (unsigned)s_net.config.setup.map,
             (unsigned)s_net.config.setup.skill,
             s_net.config.setup.no_monsters ? 0U : 1U,
             s_net.config.setup.fast_monsters ? 1U : 0U,
             s_net.config.setup.respawn_monsters ? 1U : 0U,
             (unsigned)s_net.config.setup.time_limit_minutes,
             (unsigned)P4_DOOM_MP_TICK_RATE_HZ,
             (unsigned)s_net.config.local_player_slot,
             (unsigned)s_net.config.player_count,
             (unsigned)s_net.config.input_delay_tics);
    flush_complete_tics();
    return true;
}

void P4_DoomNetSubmitTic(const ticcmd_t *command, int tic_number)
{
    if (p4_doom_gc_active()) { p4_doom_gc_submit(command, tic_number); return; }
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
        ESP_LOGE(TAG,
                 "P4_DOOM_MP LOCAL_QUEUE_FAILED tick=%" PRIu32
                 " next=%" PRIu32,
                 tic.tick, s_net.queue.next_tick);
        s_net.peer_failed = true;
        return;
    }
    if (!track_local_tic(&tic)) {
        return;
    }
    if (!s_net.runtime_started) {
        s_net.runtime_started = true;
        s_net.last_progress_us = esp_timer_get_time();
    }
    (void)send_input_tic(&tic);
    flush_complete_tics();
}

void P4_DoomNetPoll(void)
{
    if (p4_doom_gc_active()) { p4_doom_gc_poll(); return; }
    if (!s_net.prepared) {
        return;
    }
    s_net.transport.poll(s_net.transport.context);
    if (!s_net.transport.connected(
            s_net.transport.context, s_net.config.route_id)) {
        p4_mp_peer_t *const peer = remote_peer();
        const uint8_t remote_slot = peer != NULL
            ? peer->player_slot : (uint8_t)(
                s_net.config.local_player_slot == 0U ? 1U : 0U);
        p4_mp_event_t disconnected;
        (void)p4_mp_session_route_disconnected(
            s_net.session, s_net.config.route_id, &disconnected);
        disconnect_remote(remote_slot, "transport-disconnected");
        return;
    }
    const int64_t now_us = esp_timer_get_time();
    p4_mp_event_t timeout_event;
    if (s_net.session != NULL &&
        p4_mp_session_tick(
            s_net.session, (uint64_t)now_us / UINT64_C(1000),
            &timeout_event)) {
        disconnect_remote(timeout_event.player_slot, "timeout");
    }
    retry_oldest_unacknowledged(now_us);
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
    fail_lockstep_if_stalled(now_us);
}

boolean P4_DoomNetFailed(void)
{
    if (p4_doom_gc_active()) return p4_doom_gc_failed();
    return s_net.prepared && s_net.peer_failed;
}

void P4_DoomNetQuit(void)
{
    if (p4_doom_gc_active()) { p4_doom_gc_quit(); return; }
    if (s_net.prepared && s_net.session != NULL &&
        remote_peer() != NULL) {
        const uint8_t reason[2] = {0U, 0U};
        (void)send_packet(P4_MP_PACKET_LEAVE, 0U, reason, sizeof(reason));
    }
    if (s_net.prepared) {
        ESP_LOGI(TAG,
                 "P4_DOOM_MP STOP complete_tics=%" PRIu32
                 " tx_inputs=%" PRIu32 " rx_inputs=%" PRIu32
                 " retries=%" PRIu32 " ack_updates=%" PRIu32
                 " invalid_acks=%" PRIu32 " rejected=%" PRIu32
                 " send_failures=%" PRIu32 " tx_window_failures=%" PRIu32,
                 s_net.complete_tics, s_net.inputs_sent,
                 s_net.inputs_received, s_net.retransmits,
                 s_net.ack_updates, s_net.invalid_acks,
                 s_net.rejected_inputs, s_net.send_failures,
                 s_net.tx_window_failures);
    }
    s_net.prepared = false;
    s_net.configured = false;
    restore_session_timeout();
    p4_doom_mp_engine_barrier_init(&s_net.engine_barrier);
}
