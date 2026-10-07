// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/game.h"
#include "p4_air_hockey_internal.h"

extern const p4_game_descriptor_t p4_p4_air_hockey_game;

enum {
    LINK_QUEUE = 64,
    GUARD_WORDS = 19,
    STRIDE = P4_GAME_SURFACE_WIDTH + 7,
    FRAME_WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
    TOTAL_WORDS = GUARD_WORDS + FRAME_WORDS + GUARD_WORDS,
};

static int s_failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++s_failures; \
    } \
} while (0)

typedef struct test_link test_link_t;

typedef struct {
    test_link_t *link;
    uint8_t slot;
    uint32_t next_sequence;
    p4_game_multiplayer_message_t queue[LINK_QUEUE];
    size_t head;
    size_t count;
    uint32_t tone_count;
    p4_game_multiplayer_state_t state;
} endpoint_t;

struct test_link {
    endpoint_t endpoint[P4_AIR_HOCKEY_PLAYERS];
};

static bool read_status(void *context,
                        p4_game_multiplayer_status_t *status_out)
{
    endpoint_t *const endpoint = context;
    *status_out = (p4_game_multiplayer_status_t){
        .generation = 1U,
        .session_seed = UINT64_C(0x1020304050607080),
        .state = endpoint->state,
        .role = endpoint->slot == 0U ? P4_GAME_MULTIPLAYER_ROLE_HOST
                                    : P4_GAME_MULTIPLAYER_ROLE_CLIENT,
        .local_player_slot = endpoint->slot,
        .player_count = P4_AIR_HOCKEY_PLAYERS,
    };
    return true;
}

static bool send_message(void *context, const uint8_t *data,
                         size_t data_bytes)
{
    endpoint_t *const source = context;
    endpoint_t *const destination =
        &source->link->endpoint[source->slot ^ 1U];
    if (data == NULL || data_bytes == 0U ||
        data_bytes > P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES ||
        destination->count >= LINK_QUEUE) {
        return false;
    }
    const size_t tail = (destination->head + destination->count) % LINK_QUEUE;
    p4_game_multiplayer_message_t *const message = &destination->queue[tail];
    *message = (p4_game_multiplayer_message_t){
        .sequence = ++source->next_sequence,
        .player_slot = source->slot,
        .bytes = (uint8_t)data_bytes,
    };
    memcpy(message->data, data, data_bytes);
    ++destination->count;
    return true;
}

static bool receive_message(void *context,
                            p4_game_multiplayer_message_t *message_out)
{
    endpoint_t *const endpoint = context;
    if (endpoint->count == 0U) {
        return false;
    }
    *message_out = endpoint->queue[endpoint->head];
    endpoint->head = (endpoint->head + 1U) % LINK_QUEUE;
    --endpoint->count;
    return true;
}

static bool play_tone(void *context, const p4_tone_t *tone)
{
    endpoint_t *const endpoint = context;
    if (endpoint == NULL || tone == NULL || tone->frequency_hz < 40U ||
        tone->frequency_hz > 4000U || tone->duration_ms == 0U ||
        tone->duration_ms > 5000U || tone->volume_step == 0U ||
        tone->volume_step > 10U) {
        return false;
    }
    ++endpoint->tone_count;
    return true;
}

static void init_link(test_link_t *link)
{
    memset(link, 0, sizeof(*link));
    for (uint8_t slot = 0U; slot < P4_AIR_HOCKEY_PLAYERS; ++slot) {
        link->endpoint[slot].link = link;
        link->endpoint[slot].slot = slot;
        link->endpoint[slot].state = P4_GAME_MULTIPLAYER_CONNECTED;
    }
}

static p4_game_services_t network_services(endpoint_t *endpoint)
{
    static const p4_game_multiplayer_profile_t profile = {
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = P4_GAME_MULTIPLAYER_STYLE_REALTIME,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = 30U,
        .message_bytes = P4_AIR_HOCKEY_SNAPSHOT_BYTES,
        .protocol = P4_AIR_HOCKEY_PROTOCOL,
    };
    return (p4_game_services_t){
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_MULTIPLAYER_SESSION,
        .audio_context = endpoint,
        .play_tone = play_tone,
        .multiplayer_context = endpoint,
        .multiplayer_read_status = read_status,
        .multiplayer_send = send_message,
        .multiplayer_receive = receive_message,
        .multiplayer_profile = &profile,
    };
}

static p4_game_result_t update(p4_game_instance_t *instance, uint32_t held,
                               uint32_t pressed, uint32_t elapsed_ms)
{
    const p4_game_input_t input = {
        .held = held,
        .pressed = pressed,
        .touch_valid = true,
    };
    return p4_game_instance_update(instance, &input, elapsed_ms);
}

static p4_game_result_t update_touch(p4_game_instance_t *instance,
                                     uint16_t x, uint16_t y,
                                     uint32_t elapsed_ms)
{
    const p4_game_input_t input = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    return p4_game_instance_update(instance, &input, elapsed_ms);
}

static void test_physics_and_scoring(void)
{
    p4_air_hockey_state_t state = {0};
    p4_air_hockey_reset_match(&state, 1U);
    CHECK(state.phase == P4_AIR_HOCKEY_SERVE);
    CHECK(state.paddle_x[0] < state.paddle_x[1]);
    CHECK((p4_air_hockey_step(&state, 704U, true) &
           P4_AIR_HOCKEY_EVENT_SERVE) != 0U);
    CHECK(state.phase == P4_AIR_HOCKEY_PLAY);
    const int32_t before = state.paddle_x[0];
    p4_air_hockey_set_touch_target(&state, 0U, true, 120U, 100U);
    (void)p4_air_hockey_step(&state, 32U, true);
    CHECK(state.paddle_x[0] > before);

    state.phase = P4_AIR_HOCKEY_PLAY;
    state.puck_x = 301 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_y = 100 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_vx = 2 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_vy = 0;
    const uint32_t event = p4_air_hockey_step(&state, 16U, false);
    CHECK((event & P4_AIR_HOCKEY_EVENT_GOAL) != 0U);
    CHECK(state.score[0] == 1U);
    CHECK(state.phase == P4_AIR_HOCKEY_GOAL);

    state.score[0] = P4_AIR_HOCKEY_WIN_SCORE - 1U;
    state.phase = P4_AIR_HOCKEY_PLAY;
    state.puck_x = 301 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_y = 100 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_vx = 2 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_vy = 0;
    CHECK((p4_air_hockey_step(&state, 16U, false) &
           P4_AIR_HOCKEY_EVENT_WIN) != 0U);
    CHECK(state.phase == P4_AIR_HOCKEY_GAME_OVER);
    CHECK(state.winner == 0U);
}

static void test_offline_lifecycle_and_render_bounds(void)
{
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    p4_air_hockey_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_p4_air_hockey_game,
                                 &services, &state, sizeof(state)));
    CHECK(state.mode == P4_AIR_HOCKEY_OFFLINE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_TITLE);
    CHECK(update(&instance, P4_BUTTON_A, P4_BUTTON_A, 16U) == P4_GAME_CONTINUE);
    const int32_t before = state.paddle_x[0];
    CHECK(update(&instance, P4_BUTTON_RIGHT, P4_BUTTON_RIGHT, 32U) ==
          P4_GAME_CONTINUE);
    CHECK(state.paddle_x[0] == before);
    CHECK(update_touch(&instance, 120U, 100U, 32U) ==
          P4_GAME_CONTINUE);
    CHECK(state.paddle_x[0] > before);

    uint16_t *const allocation = calloc(TOTAL_WORDS, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation != NULL) {
        for (size_t index = 0U; index < TOTAL_WORDS; ++index) {
            allocation[index] = UINT16_C(0x5aa5);
        }
        p4_game_surface_t surface = {
            .pixels = allocation + GUARD_WORDS,
            .stride_pixels = STRIDE,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        CHECK(p4_game_instance_render(&instance, &surface));
        for (size_t index = 0U; index < GUARD_WORDS; ++index) {
            CHECK(allocation[index] == UINT16_C(0x5aa5));
            CHECK(allocation[GUARD_WORDS + FRAME_WORDS + index] ==
                  UINT16_C(0x5aa5));
        }
        for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
            for (size_t column = P4_GAME_SURFACE_WIDTH; column < STRIDE;
                 ++column) {
                CHECK(surface.pixels[row * STRIDE + column] ==
                      UINT16_C(0x5aa5));
            }
        }
        free(allocation);
    }
    CHECK(update(&instance, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(update_touch(&instance, 10U, 10U, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

static bool accept_tone(void *context, const p4_tone_t *tone)
{
    return context != NULL && tone != NULL;
}

static void count_stop(void *context)
{
    ++*(unsigned *)context;
}

static void test_title_pause_and_gesture_lifecycle(void)
{
    unsigned stops = 0U;
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS | P4_GAME_CAP_AUDIO_TONE,
        .audio_context = &stops, .stop_audio = count_stop, .play_tone = accept_tone,
    };
    p4_game_instance_t instance = {0};
    p4_air_hockey_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_p4_air_hockey_game, &services, &state, sizeof(state)));
    const p4_air_hockey_pose_t initial = p4_air_hockey_render_pose(&state);
    for (unsigned n = 0U; n < 60U; ++n)
        CHECK(update(&instance, 0U, 0U, 100U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_TITLE);
    CHECK(state.simulation_tick == 0U && state.phase_timer_ms == 700U);
    CHECK(state.step_accumulator_ms == 0U);
    CHECK(update_touch(&instance, 80U, 100U, 16U) == P4_GAME_CONTINUE);
    CHECK(update_touch(&instance, 132U, 130U, 16U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_TITLE); /* Dragging onto Play is not a tap. */
    CHECK(update(&instance, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(update_touch(&instance, 132U, 130U, 16U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);
    CHECK(state.simulation_tick == 0U && state.phase_timer_ms == 700U);
    CHECK(!state.touch_on_rink);
    const p4_air_hockey_pose_t started = p4_air_hockey_render_pose(&state);
    CHECK(memcmp(&initial, &started, sizeof(initial)) == 0);
    CHECK(update(&instance, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(update_touch(&instance, 120U, 100U, 16U) == P4_GAME_CONTINUE);
    CHECK(state.target_active[0]);
    p4_game_input_t drag = {.touch_valid=true, .touch_count=1U,
        .touches={{285U,12U}}, .pressed=P4_BUTTON_START, .held=P4_BUTTON_START};
    CHECK(p4_game_instance_update(&instance, &drag, 16U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);
    drag.touches[0].x=10U; drag.pressed=P4_BUTTON_BACK; drag.held=P4_BUTTON_BACK;
    CHECK(p4_game_instance_update(&instance, &drag, 16U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);
    CHECK(update(&instance, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(update_touch(&instance, 285U, 12U, 16U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_PAUSED && stops == 1U);
    const p4_air_hockey_state_t paused = state;
    CHECK(update_touch(&instance, 132U, 130U, 16U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_PAUSED); /* Must release Pause first. */
    for (unsigned n = 0U; n < 60U; ++n)
        CHECK(update(&instance, 0U, 0U, 100U) == P4_GAME_CONTINUE);
    CHECK(state.simulation_tick == paused.simulation_tick);
    CHECK(state.phase_timer_ms == paused.phase_timer_ms);
    CHECK(state.step_accumulator_ms == paused.step_accumulator_ms);
    CHECK(state.puck_x == paused.puck_x && state.puck_y == paused.puck_y);
    CHECK(memcmp(state.paddle_x, paused.paddle_x, sizeof(state.paddle_x)) == 0);
    CHECK(memcmp(state.paddle_y, paused.paddle_y, sizeof(state.paddle_y)) == 0);
    CHECK(update_touch(&instance, 132U, 130U, 100U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);
    CHECK(state.simulation_tick == paused.simulation_tick); /* No pause debt on Resume. */
    CHECK(update(&instance, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(state.simulation_tick == paused.simulation_tick + 1U);
    CHECK(update(&instance, P4_BUTTON_START, P4_BUTTON_START, 16U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_PAUSED && stops == 2U);
    CHECK(update(&instance, P4_BUTTON_A, P4_BUTTON_A, 16U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);
    CHECK(update(&instance, P4_BUTTON_START, P4_BUTTON_START, 16U) == P4_GAME_CONTINUE);
    CHECK(update(&instance, P4_BUTTON_START, P4_BUTTON_START, 16U) == P4_GAME_CONTINUE);
    CHECK(state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);
    state.phase = P4_AIR_HOCKEY_GAME_OVER;
    state.winner = 1U; state.score[1] = 7U;
    CHECK(update_touch(&instance, 70U, 100U, 16U) == P4_GAME_CONTINUE);
    CHECK(state.phase == P4_AIR_HOCKEY_GAME_OVER);
    CHECK(update(&instance, P4_BUTTON_A, P4_BUTTON_A, 16U) == P4_GAME_CONTINUE);
    CHECK(state.phase == P4_AIR_HOCKEY_SERVE && state.score[1] == 0U);
    CHECK(update(&instance, P4_BUTTON_START, P4_BUTTON_START, 16U) == P4_GAME_CONTINUE);
    CHECK(update_touch(&instance, 208U, 130U, 16U) == P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    CHECK(stops == 6U);

    /* Both visible title Exit and controller Back retain the OS lifecycle. */
    CHECK(p4_game_instance_start(&instance, &p4_p4_air_hockey_game, &services, &state, sizeof(state)));
    CHECK(update_touch(&instance, 208U, 130U, 16U) == P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    CHECK(p4_game_instance_start(&instance, &p4_p4_air_hockey_game, &services, &state, sizeof(state)));
    CHECK(update(&instance, P4_BUTTON_BACK, P4_BUTTON_BACK, 16U) == P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

/* Replay the benchmark's real tape through the game and prove it launches and
 * spends its sample window in active simulation rather than idling on title. */
static void test_active_performance_tape(void)
{
    FILE *file = fopen(P4_TEST_INPUT_TAPE, "r");
    CHECK(file != NULL); if (file == NULL) return;
    const p4_game_services_t services = {.available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS};
    p4_air_hockey_state_t state;
    p4_game_instance_t instance={0};
    CHECK(p4_game_instance_start(&instance,&p4_p4_air_hockey_game,&services,&state,sizeof(state)));
    unsigned frame=0U, next_frame=0U, next_buttons=0U, buttons=0U, active=0U, moving=0U, title=0U;
    int next_x=-1,next_y=-1,x=-1,y=-1;
    uint32_t previous=0U;
    int fields=fscanf(file,"%u %u %d %d",&next_frame,&next_buttons,&next_x,&next_y);
    for(frame=0U; frame<2000U; ++frame) {
        if(fields==4 && frame==next_frame) {
            buttons=next_buttons; x=next_x; y=next_y;
            fields=fscanf(file,"%u %u %d %d",&next_frame,&next_buttons,&next_x,&next_y);
        }
        p4_game_input_t input={.held=buttons,.pressed=buttons&~previous,.touch_valid=true,
            .touch_count=(uint8_t)(x>=0&&y>=0?1U:0U)};
        if(input.touch_count) input.touches[0]=(p4_game_point_t){(uint16_t)x,(uint16_t)y};
        const p4_air_hockey_pose_t before=p4_air_hockey_render_pose(&state);
        CHECK(p4_game_instance_update(&instance,&input,frame%3U==2U?16U:17U)==P4_GAME_CONTINUE);
        const p4_air_hockey_pose_t after=p4_air_hockey_render_pose(&state);
        if(state.ui_screen==P4_AIR_HOCKEY_UI_TITLE) ++title;
        if(state.ui_screen==P4_AIR_HOCKEY_UI_PLAY && state.phase==P4_AIR_HOCKEY_PLAY) ++active;
        if(memcmp(&before,&after,sizeof(before))!=0) ++moving;
        previous=buttons;
    }
    CHECK(active>1500U && moving>1700U && title==0U);
    CHECK(state.ui_screen==P4_AIR_HOCKEY_UI_PLAY);
    CHECK(fclose(file)==0);
    const char *path=getenv("P4_ACTIVE_TRACE_PATH");
    if(path!=NULL) {
        FILE *trace=fopen(path,"w"); CHECK(trace!=NULL);
        if(trace!=NULL) {
            fprintf(trace,"{\"frames\":2000,\"active_play_frames\":%u,\"moving_pose_frames\":%u,\"final_simulation_tick\":%u,\"title_frames_after_start\":%u}\n",active,moving,state.simulation_tick,title);
            CHECK(fclose(trace)==0);
        }
    }
    p4_game_instance_stop(&instance);
}

static void test_two_console_host_authority_and_peer_loss(void)
{
    test_link_t link;
    init_link(&link);
    p4_game_services_t host_services = network_services(&link.endpoint[0]);
    p4_game_services_t client_services = network_services(&link.endpoint[1]);
    p4_game_instance_t host = {0};
    p4_game_instance_t client = {0};
    p4_air_hockey_state_t host_state;
    p4_air_hockey_state_t client_state;
    CHECK(p4_game_instance_start(&host, &p4_p4_air_hockey_game,
                                 &host_services, &host_state,
                                 sizeof(host_state)));
    CHECK(p4_game_instance_start(&client, &p4_p4_air_hockey_game,
                                 &client_services, &client_state,
                                 sizeof(client_state)));
    CHECK(host_state.network_role == P4_GAME_MULTIPLAYER_ROLE_HOST);
    CHECK(client_state.phase == P4_AIR_HOCKEY_NETWORK_WAIT);
    CHECK(host_state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);
    CHECK(client_state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);

    /* Start and the offline Pause location cannot freeze either linked peer. */
    CHECK(update(&host, P4_BUTTON_START, P4_BUTTON_START, 16U) == P4_GAME_CONTINUE);
    CHECK(host_state.simulation_tick == 1U);
    CHECK(host_state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);
    CHECK(update_touch(&client, 285U, 12U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);
    CHECK(!client_state.target_active[1]);
    CHECK(update(&client, P4_BUTTON_START, P4_BUTTON_START, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);

    CHECK(update(&host, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.snapshot_received);
    CHECK(client_state.snapshot_revision == host_state.snapshot_revision);

    const uint32_t client_tones_before = link.endpoint[1].tone_count;
    host_state.network_audio_events = P4_AIR_HOCKEY_EVENT_HIT;
    p4_air_hockey_network_publish(&host.context, &host_state, true);
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(link.endpoint[1].tone_count == client_tones_before + 2U);
    CHECK(client_state.network_audio_events == P4_AIR_HOCKEY_EVENT_NONE);

    const int32_t client_world_x = host_state.paddle_x[1];
    CHECK(update_touch(&client, 40U, 100U, 50U) ==
          P4_GAME_CONTINUE);
    CHECK(update(&host, 0U, 0U, 32U) == P4_GAME_CONTINUE);
    CHECK(host_state.paddle_x[1] > client_world_x);
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.snapshot_revision == host_state.snapshot_revision);

    host_state.phase = P4_AIR_HOCKEY_GAME_OVER;
    host_state.winner = 0U;
    host_state.score[0] = P4_AIR_HOCKEY_WIN_SCORE;
    p4_air_hockey_network_publish(&host.context, &host_state, true);
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.phase == P4_AIR_HOCKEY_GAME_OVER);
    CHECK(update_touch(&client, 132U, 130U, 50U) == P4_GAME_CONTINUE);
    CHECK(update(&host, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(host_state.phase == P4_AIR_HOCKEY_SERVE);
    CHECK(host_state.score[0] == 0U);
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.phase == P4_AIR_HOCKEY_SERVE);

    link.endpoint[1].state = P4_GAME_MULTIPLAYER_PEER_LEFT;
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.mode == P4_AIR_HOCKEY_OFFLINE);
    CHECK(client_state.ui_screen == P4_AIR_HOCKEY_UI_PLAY);
    CHECK(client_state.disconnect_banner_ms != 0U);
    p4_game_instance_stop(&client);
    p4_game_instance_stop(&host);
}

static void test_fixed_step_presentation(void)
{
    p4_air_hockey_state_t state={0};
    p4_air_hockey_reset_match(&state,1U);
    state.phase=P4_AIR_HOCKEY_PLAY;
    state.puck_x=80*256;state.puck_y=100*256;state.puck_vx=256;state.puck_vy=0;
    p4_air_hockey_visual_snap(&state);
    uint32_t total=0U;
    const char *trace_path=getenv("P4_MOTION_TRACE_PATH");
    FILE *trace=trace_path==NULL?NULL:fopen(trace_path,"w");
    CHECK(trace_path==NULL||trace!=NULL);
    if(trace!=NULL)fputs("elapsed_ms,authoritative_fixed_x,previous_renderer_fixed_x,interpolated_fixed_x,remainder_ms\n",trace);
    int32_t last=state.puck_x;
    for(unsigned frame=0U;frame<36U;++frame){
        const uint32_t delta=frame%2U==0U?16U:17U;total+=delta;
        CHECK(p4_air_hockey_step(&state,delta,false)==P4_AIR_HOCKEY_EVENT_NONE);
        const p4_air_hockey_state_t before=state;
        const p4_air_hockey_pose_t pose=p4_air_hockey_render_pose(&state);
        CHECK(pose.puck_x==80*256+(int32_t)(total-16U)*16);
        CHECK(pose.puck_x>=last&&pose.puck_x-last<=272); /* No doubled step. */
        CHECK(state.puck_x==80*256+(int32_t)(total/16U)*256);
        CHECK(memcmp(&before,&state,sizeof(state))==0); /* Rendering is pure. */
        if(trace!=NULL)fprintf(trace,"%lu,%ld,%ld,%ld,%lu\n",(unsigned long)total,
            (long)state.puck_x,(long)state.puck_x,(long)pose.puck_x,(unsigned long)state.step_accumulator_ms);
        last=pose.puck_x;
    }
    if(trace!=NULL)CHECK(fclose(trace)==0);
    const p4_air_hockey_pose_t host_pose=p4_air_hockey_render_pose(&state);
    state.local_player_slot=1U;
    const p4_air_hockey_pose_t reflected_view_pose=p4_air_hockey_render_pose(&state);
    CHECK(memcmp(&host_pose,&reflected_view_pose,sizeof(host_pose))==0); /* Reflection belongs to drawing. */
    p4_air_hockey_reset_match(&state,2U);
    CHECK(p4_air_hockey_render_pose(&state).puck_x==state.puck_x);
    state.phase_timer_ms=1U;
    (void)p4_air_hockey_step(&state,17U,false);
    CHECK(state.phase==P4_AIR_HOCKEY_PLAY);
    CHECK(p4_air_hockey_render_pose(&state).puck_x==state.puck_x); /* Serve teleports snap. */
    state.puck_x=301*256;state.puck_y=100*256;state.puck_vx=256;state.puck_vy=0;
    (void)p4_air_hockey_step(&state,16U,false);
    CHECK(state.phase==P4_AIR_HOCKEY_GOAL);
    CHECK(p4_air_hockey_render_pose(&state).puck_x==state.puck_x);
}
static void client_tick(p4_game_instance_t *client,p4_air_hockey_state_t *state,uint32_t ms)
{
    CHECK(p4_air_hockey_network_update(&client->context,state,false,0U,0U,false,ms));
}
static void test_client_presentation_jitter(void)
{
    test_link_t link;init_link(&link);
    p4_game_services_t hs=network_services(&link.endpoint[0]),cs=network_services(&link.endpoint[1]);
    p4_game_instance_t host={0},client={0};p4_air_hockey_state_t h,c;
    CHECK(p4_game_instance_start(&host,&p4_p4_air_hockey_game,&hs,&h,sizeof(h)));
    CHECK(p4_game_instance_start(&client,&p4_p4_air_hockey_game,&cs,&c,sizeof(c)));
    h.phase=P4_AIR_HOCKEY_PLAY;h.puck_x=80*256;h.puck_y=100*256;
    p4_air_hockey_network_publish(&host.context,&h,true);client_tick(&client,&c,0U);
    CHECK(p4_air_hockey_render_pose(&c).puck_x==80*256);
    h.puck_x=100*256;p4_air_hockey_network_publish(&host.context,&h,true);client_tick(&client,&c,0U);
    CHECK(c.puck_x==100*256); /* Authoritative packet is applied immediately. */
    CHECK(p4_air_hockey_render_pose(&c).puck_x==80*256); /* Visible position is continuous. */
    client_tick(&client,&c,16U);
    CHECK(p4_air_hockey_render_pose(&c).puck_x==80*256+20*256*16/33);
    client_tick(&client,&c,9U);
    const int32_t in_flight=p4_air_hockey_render_pose(&c).puck_x;
    h.puck_x=120*256;p4_air_hockey_network_publish(&host.context,&h,true);client_tick(&client,&c,0U);
    CHECK(p4_air_hockey_render_pose(&c).puck_x==in_flight); /* Early packet restarts from displayed pose. */
    client_tick(&client,&c,17U);
    CHECK(p4_air_hockey_render_pose(&c).puck_x>in_flight);
    CHECK(p4_air_hockey_render_pose(&c).puck_x<120*256);
    client_tick(&client,&c,100U);
    CHECK(p4_air_hockey_render_pose(&c).puck_x==120*256);
    client_tick(&client,&c,100U);
    CHECK(p4_air_hockey_render_pose(&c).puck_x==120*256); /* Late/lost packet never extrapolates. */
    h.phase=P4_AIR_HOCKEY_GOAL;h.score[0]=1U;h.puck_x=302*256;
    p4_air_hockey_network_publish(&host.context,&h,true);client_tick(&client,&c,16U);
    CHECK(p4_air_hockey_render_pose(&c).puck_x==302*256);
    p4_air_hockey_reset_match(&h,3U);p4_air_hockey_network_publish(&host.context,&h,true);client_tick(&client,&c,17U);
    CHECK(c.phase==P4_AIR_HOCKEY_SERVE&&c.score[0]==0U);
    CHECK(p4_air_hockey_render_pose(&c).puck_x==160*256);
    p4_game_instance_stop(&client);p4_game_instance_stop(&host);
}

/* Source regression: no device I/O; reuses the existing two-console fixture. */
static void test_rematch_request_survives_input_drain(void)
{
    for (unsigned scenario = 0U; scenario < 8U; ++scenario) {
        test_link_t link;
        init_link(&link);
        p4_game_services_t hs = network_services(&link.endpoint[0]);
        p4_game_services_t cs = network_services(&link.endpoint[1]);
        p4_game_instance_t host = {0}, client = {0};
        p4_air_hockey_state_t h, c;
        CHECK(p4_game_instance_start(&host, &p4_p4_air_hockey_game,
                                    &hs, &h, sizeof(h)));
        CHECK(p4_game_instance_start(&client, &p4_p4_air_hockey_game,
                                    &cs, &c, sizeof(c)));
        h.phase = P4_AIR_HOCKEY_GAME_OVER;
        h.winner = 0U;
        h.score[0] = 7U;
        h.score[1] = 2U;
        p4_air_hockey_network_publish(&host.context, &h, true);
        CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
        /* Discard startup traffic; each case then chooses an exact order. */
        link.endpoint[0].count = 0U;
        uint8_t neutral[P4_AIR_HOCKEY_INPUT_BYTES] = {
            P4_AIR_HOCKEY_PROTOCOL, 1U, 0U, 0U, 0U, 0U, 0U, 0U
        };
        const bool expect_restart = scenario != 6U;
        if (scenario == 3U || scenario == 4U || scenario == 7U) {
            if (scenario == 7U)
                CHECK(send_message(&link.endpoint[1], neutral, sizeof(neutral)));
            if (scenario == 4U)
                CHECK(update_touch(&client, 132U, 130U, 16U) == P4_GAME_CONTINUE);
            else
                CHECK(update(&client, P4_BUTTON_A, P4_BUTTON_A, 16U) == P4_GAME_CONTINUE);
        }
        unsigned neutral_count = scenario == 0U || scenario == 7U ? 0U : 1U;
        if (scenario == 2U) neutral_count = 8U;
        for (unsigned n = 0U; n < neutral_count; ++n)
            CHECK(send_message(&link.endpoint[1], neutral, sizeof(neutral)));
        if (scenario == 5U)
            CHECK(update_touch(&host, 132U, 130U, 16U) == P4_GAME_CONTINUE);
        else {
            const uint32_t button = scenario <= 2U ? P4_BUTTON_A : 0U;
            CHECK(update(&host, button, button, 16U) == P4_GAME_CONTINUE);
        }
        printf("rematch scenario=%u phase=%u score=%u:%u expected_restart=%u\n",
               scenario, (unsigned)h.phase, h.score[0], h.score[1], expect_restart);
        CHECK((h.phase == P4_AIR_HOCKEY_SERVE) == expect_restart);
        CHECK(h.score[0] == (expect_restart ? 0U : 7U));
        CHECK(h.score[1] == (expect_restart ? 0U : 2U));
        CHECK(!h.restart_requested);
        CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
        CHECK(c.phase == h.phase && c.score[0] == h.score[0] && c.score[1] == h.score[1]);
        /* A new frame with no edge must not trigger another reset. */
        CHECK(update(&host, P4_BUTTON_A, 0U, 16U) == P4_GAME_CONTINUE);
        if (expect_restart) CHECK(h.simulation_tick == 1U);
        p4_game_instance_stop(&client);
        p4_game_instance_stop(&host);
    }
}

static void test_rematch_does_not_arm_during_play(void)
{
    test_link_t link;
    init_link(&link);
    p4_game_services_t hs = network_services(&link.endpoint[0]);
    p4_game_instance_t host = {0};
    p4_air_hockey_state_t h;
    CHECK(p4_game_instance_start(&host, &p4_p4_air_hockey_game,
                                &hs, &h, sizeof(h)));
    h.phase = P4_AIR_HOCKEY_PLAY;
    const uint8_t restart[P4_AIR_HOCKEY_INPUT_BYTES] = {
        P4_AIR_HOCKEY_PROTOCOL, 1U, 0U, 0U, 0U, 0U, 2U, 0U
    };
    CHECK(send_message(&link.endpoint[1], restart, sizeof(restart)));
    CHECK(update(&host, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(!h.restart_requested);
    h.phase = P4_AIR_HOCKEY_GAME_OVER;
    h.score[0] = 7U;
    CHECK(update(&host, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(h.phase == P4_AIR_HOCKEY_GAME_OVER && h.score[0] == 7U);
    p4_game_instance_stop(&host);
}

int main(void)
{
    test_rematch_request_survives_input_drain();
    test_rematch_does_not_arm_during_play();
    CHECK(p4_game_descriptor_valid(&p4_p4_air_hockey_game));
    CHECK(p4_p4_air_hockey_game.launcher_id == 115U);
    CHECK(sizeof(p4_air_hockey_state_t) <= P4_GAME_MAX_STATE_BYTES);
    uint16_t world_x = 0U;
    uint16_t world_y = 0U;
    p4_air_hockey_canonical_touch(1U, 40U, 77U, &world_x, &world_y);
    CHECK(world_x == 279U);
    CHECK(world_y == 77U);
    CHECK(p4_air_hockey_striker_skin(0U) ==
          P4_AIR_HOCKEY_STRIKER_CYAN);
    CHECK(p4_air_hockey_striker_skin(1U) ==
          P4_AIR_HOCKEY_STRIKER_MAGENTA);
    test_title_pause_and_gesture_lifecycle();
    test_active_performance_tape();
    test_fixed_step_presentation();
    test_client_presentation_jitter();
    test_physics_and_scoring();
    test_offline_lifecycle_and_render_bounds();
    test_two_console_host_authority_and_peer_loss();
    if (s_failures != 0) {
        fprintf(stderr, "%d P4 Air Hockey test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("P4 Air Hockey tests passed");
    return EXIT_SUCCESS;
}
