// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>

#include "p4/audio.h"
#include "p4/draw.h"
#include "p4/game.h"
#include "../src/frog_hop_internal.h"

extern const p4_game_descriptor_t p4_frog_hop_game;

enum {
    GUARD_WORDS = 29,
};

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

/* Explicit legacy renderer fixture; retained storage outlives every instance.
 * Maintained native admission continues to use p4_frog_hop_game unchanged. */
static const p4_game_descriptor_t *legacy_descriptor(void)
{
    static p4_game_descriptor_t descriptor;
    static bool initialized;
    if (!initialized) {
        descriptor = p4_frog_hop_game;
        descriptor.required_capabilities &= ~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
        descriptor.optional_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
        initialized = true;
    }
    return &descriptor;
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static bool start_game(p4_game_instance_t *instance, void *state_memory,
                       p4_audio_mixer_t *mixer, uint16_t width)
{
    p4_audio_mixer_init(mixer);
    static p4_game_services_t services;
    services = (p4_game_services_t){
        .available_capabilities = P4_GAME_CAP_VIDEO |
                              P4_GAME_CAP_CONTROLS |
                              P4_GAME_CAP_AUDIO_TONE | (width==768U?P4_GAME_CAP_VIDEO_HIGH_RES:0U),
        .audio_context = mixer,
        .play_tone = p4_audio_mixer_service_play_tone,
        .submit_pcm16_stereo = NULL,
        .stop_audio = p4_audio_mixer_service_stop,
    };
    *instance = (p4_game_instance_t){0};
    CHECK((p4_frog_hop_game.required_capabilities & P4_GAME_CAP_VIDEO_HIGH_RES) != 0U);
    CHECK((p4_frog_hop_game.optional_capabilities & P4_GAME_CAP_VIDEO_HIGH_RES) == 0U);
    if (width != 768U) {
        CHECK(!p4_game_instance_start(instance, &p4_frog_hop_game, &services,
                                     state_memory, p4_frog_hop_game.state_bytes));
    }
    return p4_game_instance_start(instance,
                                  width == 768U ? &p4_frog_hop_game : legacy_descriptor(),
                                  &services, state_memory,
                                  p4_frog_hop_game.state_bytes);
}

static void test_lifecycle_render_and_controls(uint16_t width, uint16_t height)
{
    const size_t stride=(size_t)width+11U;
    const size_t frame_words=stride*height;
    const size_t total_words=GUARD_WORDS+frame_words+GUARD_WORDS;
    uint16_t *const allocation = calloc(total_words, sizeof(*allocation));
    void *const state_memory = calloc(1U, p4_frog_hop_game.state_bytes);
    CHECK(allocation != NULL);
    CHECK(state_memory != NULL);
    if (allocation == NULL || state_memory == NULL) {
        free(state_memory);
        free(allocation);
        return;
    }
    for (size_t index = 0U; index < total_words; ++index) {
        allocation[index] = UINT16_C(0x5aa5);
    }
    p4_game_surface_t surface = {
        .pixels = allocation + GUARD_WORDS,
        .stride_pixels = stride,
        .width = width,
        .height = height,
    };
    p4_game_instance_t instance;
    p4_audio_mixer_t mixer;
    CHECK(p4_game_descriptor_valid(&p4_frog_hop_game));
    CHECK(p4_frog_hop_game.launcher_id == 105U);
    CHECK(start_game(&instance, state_memory, &mixer, width));
    CHECK(p4_game_instance_render(&instance, &surface));

    p4_game_input_t input = {
        .held = P4_BUTTON_A,
        .pressed = P4_BUTTON_A,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    uint32_t random = UINT32_C(0x6f726f67);
    const uint32_t playable_buttons = P4_BUTTON_MASK ^ P4_BUTTON_BACK;
    for (size_t iteration = 0U; iteration < 1000U; ++iteration) {
        input = (p4_game_input_t){
            .held = next_random(&random) & playable_buttons,
            .pressed = next_random(&random) & playable_buttons,
            .released = next_random(&random) & playable_buttons,
            .touch_valid = true,
        };
        CHECK(p4_game_instance_update(&instance, &input,
                                      next_random(&random) % 101U) ==
              P4_GAME_CONTINUE);
        if (iteration % 3U == 0U) {
            CHECK(p4_game_instance_render(&instance, &surface));
        }
    }
    p4_audio_mixer_stats_t audio_stats;
    p4_audio_mixer_get_stats(&mixer, &audio_stats);
    CHECK(audio_stats.tones_started != 0U);

    input = (p4_game_input_t){
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    for (size_t index = 0U; index < GUARD_WORDS; ++index) {
        CHECK(allocation[index] == UINT16_C(0x5aa5));
        CHECK(allocation[GUARD_WORDS + frame_words + index] ==
              UINT16_C(0x5aa5));
    }
    for (size_t row = 0U; row < height; ++row) {
        for (size_t column = width; column < stride;
             ++column) {
            CHECK(surface.pixels[row * stride + column] ==
                  UINT16_C(0x5aa5));
        }
    }
    p4_game_instance_stop(&instance);
    free(state_memory);
    free(allocation);
}

typedef struct { unsigned impacts; unsigned hops; } tone_probe_t;
static bool record_tone(void *context, const p4_tone_t *tone)
{
    tone_probe_t *probe=context;
    if(tone->frequency_hz==110U)++probe->impacts;
    if(tone->frequency_hz==440U)++probe->hops;
    return true;
}
static void test_collision_recovers(void)
{
    void *state=calloc(1U,p4_frog_hop_game.state_bytes);
    CHECK(state!=NULL);if(state==NULL)return;
    tone_probe_t probe={0};
    const p4_game_services_t services={
        .available_capabilities=P4_GAME_CAP_VIDEO|P4_GAME_CAP_CONTROLS|P4_GAME_CAP_AUDIO_TONE|P4_GAME_CAP_VIDEO_HIGH_RES,
        .audio_context=&probe,.play_tone=record_tone};
    p4_game_instance_t game={0};
    CHECK(p4_game_instance_start(&game,&p4_frog_hop_game,&services,state,p4_frog_hop_game.state_bytes));
    p4_game_input_t input={.held=P4_BUTTON_A,.pressed=P4_BUTTON_A};
    CHECK(p4_game_instance_update(&game,&input,20U)==P4_GAME_CONTINUE);
    /* The first upward hop enters the initial car at x160, deterministically. */
    input=(p4_game_input_t){.held=P4_BUTTON_UP,.pressed=P4_BUTTON_UP};
    CHECK(p4_game_instance_update(&game,&input,20U)==P4_GAME_CONTINUE);
    CHECK(probe.impacts==1U && probe.hops==1U);
    input=(p4_game_input_t){.held=P4_BUTTON_LEFT,.pressed=P4_BUTTON_LEFT};
    CHECK(p4_game_instance_update(&game,&input,20U)==P4_GAME_CONTINUE);
    CHECK(probe.hops==1U); /* Recovery blocks movement. */
    input=(p4_game_input_t){0};
    for(unsigned i=0;i<6U;++i)
        CHECK(p4_game_instance_update(&game,&input,100U)==P4_GAME_CONTINUE);
    input=(p4_game_input_t){.held=P4_BUTTON_LEFT,.pressed=P4_BUTTON_LEFT};
    CHECK(p4_game_instance_update(&game,&input,20U)==P4_GAME_CONTINUE);
    CHECK(probe.hops==2U && probe.impacts==1U); /* Player can move again. */
    p4_game_instance_stop(&game);free(state);
}

static void test_substep_motion_and_water_clock(void)
{
    frog_hop_state_t state;
    p4_audio_mixer_t mixer;
    p4_game_instance_t game;
    CHECK(start_game(&game,&state,&mixer,768U));
    p4_game_input_t input={.held=P4_BUTTON_A,.pressed=P4_BUTTON_A};
    CHECK(p4_game_instance_update(&game,&input,0U)==P4_GAME_CONTINUE);
    input=(p4_game_input_t){0};
    int previous=frog_hop_lane_visual_q8(&state,false,0U);
    int largest=0;
    for(unsigned t=1U;t<=2400U;++t){
        CHECK(p4_game_instance_update(&game,&input,1U)==P4_GAME_CONTINUE);
        const int now=frog_hop_lane_visual_q8(&state,false,0U);
        int delta=now-previous;
        if(delta<0)delta+=112*256; /* Circular lane identity is unchanged. */
        CHECK(delta>=0&&delta<=26);
        if(delta>largest)largest=delta;
        if(t>=20U)CHECK(delta>0||t==20U);
        previous=now;
    }
    CHECK(state.road_variants[0]!=0U); /* Crossed the wrapped lane seam. */
    const uint16_t water_clock=state.scenery_ms;
    input=(p4_game_input_t){.pressed=P4_BUTTON_LEFT,.held=P4_BUTTON_LEFT};
    CHECK(p4_game_instance_update(&game,&input,1U)==P4_GAME_CONTINUE);
    CHECK(state.scenery_ms==(uint16_t)(water_clock+1U));
    /* A supported frog and its log share exactly the same substep displacement. */
    state.frog_row=1U;state.frog_x=20;state.log_offsets[0]=0;state.hop_ms=0U;
    state.simulation_ms=0U;state.world_advanced=true;state.respawn_ms=0U;
    const int relative=frog_hop_visual_q8(&state,false)-frog_hop_lane_visual_q8(&state,true,0U);
    state.simulation_ms=13U;
    CHECK(frog_hop_visual_q8(&state,false)-frog_hop_lane_visual_q8(&state,true,0U)==relative);
    const int still=frog_hop_lane_visual_q8(&state,false,0U);
    state.paused=true;input=(p4_game_input_t){0};
    CHECK(p4_game_instance_update(&game,&input,100U)==P4_GAME_CONTINUE);
    CHECK(frog_hop_lane_visual_q8(&state,false,0U)==still);
    printf("Frog lane motion: 2400 x 1 ms samples, maximum Q8 delta %d (0.102 world px), original tick = 512; wrapped identity and carried frog alignment pass.\n",largest);
    /* Sample the actual 60 Hz presentation cadence after interpolation warmup. */
    state.paused=false;state.frog_row=9U;state.road_offsets[0]=-100;
    state.simulation_ms=0U;state.level=1U;state.world_advanced=true;
    input=(p4_game_input_t){0};
    unsigned raw_still=0U,visual_still=0U;int raw_max=0,visual_max=0;
    int raw=state.road_offsets[0]*256,visual=frog_hop_lane_visual_q8(&state,false,0U);
    for(unsigned frame=0U;frame<60U;++frame){
        CHECK(p4_game_instance_update(&game,&input,frame%3U==0U?16U:17U)==P4_GAME_CONTINUE);
        int next_raw=state.road_offsets[0]*256,next_visual=frog_hop_lane_visual_q8(&state,false,0U);
        int dr=next_raw-raw,dv=next_visual-visual;
        if(dr<0)dr+=112*256;if(dv<0)dv+=112*256;
        if(dr==0)++raw_still;if(dv==0)++visual_still;
        if(dr>raw_max)raw_max=dr;if(dv>visual_max)visual_max=dv;
        raw=next_raw;visual=next_visual;
    }
    CHECK(raw_still==10U&&visual_still==0U);
    CHECK(raw_max==512&&visual_max<=436);
    printf("Frog 60 Hz/60 frames: old static frames %u -> new %u; max Q8 displacement %d -> %d.\n",raw_still,visual_still,raw_max,visual_max);
    p4_game_instance_stop(&game);
}

int main(void)
{
    test_lifecycle_render_and_controls(320U,200U);
    test_lifecycle_render_and_controls(768U,480U);
    test_collision_recovers();
    test_substep_motion_and_water_clock();
    if (s_failures != 0) {
        fprintf(stderr, "%d Frog Hop test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("frog_hop tests passed");
    return EXIT_SUCCESS;
}
