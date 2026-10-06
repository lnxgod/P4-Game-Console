// SPDX-License-Identifier: MIT

#include "p4_games/space_invaders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/audio.h"
#include "p4/input.h"
#include "space_invaders_internal.h"

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static bool start_title_mode(p4_game_instance_t *instance,
                            space_invaders_state_t *state,
                            p4_audio_mixer_t *mixer, bool high_res)
{
    p4_audio_mixer_init(mixer);
    static p4_game_services_t services;
    services = (p4_game_services_t){
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_AUDIO_TONE |
                                  (high_res ? P4_GAME_CAP_VIDEO_HIGH_RES : 0U),
        .audio_context = mixer,
        .play_tone = p4_audio_mixer_service_play_tone,
        .submit_pcm16_stereo = NULL,
        .stop_audio = p4_audio_mixer_service_stop,
    };
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(
        instance, &p4_space_invaders_game, &services,
        state, sizeof(*state));
}

static bool start_game_mode(p4_game_instance_t *instance,
                            space_invaders_state_t *state,
                            p4_audio_mixer_t *mixer,bool high_res)
{
    if(!start_title_mode(instance,state,mixer,high_res))return false;
    p4_game_input_t input={.held=P4_BUTTON_A,.pressed=P4_BUTTON_A};
    if(p4_game_instance_update(instance,&input,0U)!=P4_GAME_CONTINUE)return false;
    input=(p4_game_input_t){0};
    return p4_game_instance_update(instance,&input,0U)==P4_GAME_CONTINUE;
}

static bool start_game(p4_game_instance_t *instance,
                       space_invaders_state_t *state,
                       p4_audio_mixer_t *mixer)
{
    return start_game_mode(instance, state, mixer, false);
}

static void test_descriptor_and_reset(void)
{
    CHECK(p4_game_descriptor_valid(&p4_space_invaders_game));
    CHECK(p4_space_invaders_game.launcher_id == 101U);
    space_invaders_state_t state;
    space_invaders_reset(&state);
    CHECK(state.intro);
    CHECK(state.alive_mask == UINT32_MAX);
    CHECK(state.invaders_remaining == SPACE_INVADER_COUNT);
    CHECK(state.lives == 3U);
    CHECK(state.wave == 1U);
    CHECK(state.player_x == 160);
    for (size_t i = 0U; i < SPACE_SHIELD_COUNT; ++i) {
        CHECK(state.shields[i] == UINT16_C(0x0fff));
    }
}

static void test_controls_sound_and_exit(void)
{
    p4_game_instance_t instance;
    space_invaders_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game(&instance, &state, &mixer));
    p4_game_input_t input = {
        .held = P4_BUTTON_A | P4_BUTTON_LEFT,
        .pressed = P4_BUTTON_A | P4_BUTTON_LEFT,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.player_projectiles[0].active);
    CHECK(state.player_projectiles[0].y < 130);
    CHECK(state.player_x == 158);
    p4_audio_mixer_stats_t stats;
    p4_audio_mixer_get_stats(&mixer, &stats);
    CHECK(stats.tones_started >= 2U);
    input = (p4_game_input_t){
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

static void test_hits_wave_and_player_damage(void)
{
    p4_game_instance_t instance;
    space_invaders_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game(&instance, &state, &mixer));
    state.alive_mask = UINT32_C(1);
    state.invaders_remaining = 1U;
    state.player_projectiles[0] = (space_projectile_t){
        .x = state.formation_x + 5,
        .y = state.formation_y + 6,
        .active = true,
    };
    p4_game_input_t input = {.touch_valid = true};
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.alive_mask == 0U);
    CHECK(state.invaders_remaining == 0U);
    CHECK(state.score == 40U);
    CHECK(state.wave_delay_ms != 0U);
    state.wave_delay_ms = 1U;
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.wave == 2U);
    CHECK(state.invaders_remaining == SPACE_INVADER_COUNT);
    state.enemy_projectiles[0] = (space_projectile_t){
        .x = state.player_x, .y = 131, .active = true,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.lives == 2U);
    CHECK(state.respawn_ms != 0U);
    p4_game_instance_stop(&instance);
}

static void test_render_bounds_and_fuzz(unsigned width, unsigned height)
{
    const size_t GUARD = 37U;
    const size_t STRIDE = width + 7U;
    const size_t WORDS = STRIDE * height;
    const size_t TOTAL = GUARD + WORDS + GUARD;
    uint16_t *const allocation = calloc(TOTAL, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation == NULL) {
        return;
    }
    for (size_t i = 0U; i < TOTAL; ++i) {
        allocation[i] = UINT16_C(0x5aa5);
    }
    p4_game_surface_t surface = {
        .pixels = allocation + GUARD,
        .stride_pixels = (uint16_t)STRIDE,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
    };
    p4_game_instance_t instance;
    space_invaders_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game_mode(&instance, &state, &mixer, width == 768U));
    state.intro=true;
    const space_invaders_state_t title_before=state;
    CHECK(p4_game_instance_render(&instance, &surface));
    CHECK(memcmp(&title_before,&state,sizeof(state))==0);
    state.intro=false;
    uint32_t random = UINT32_C(0x5aace123);
    for (size_t iteration = 0U; iteration < 5000U; ++iteration) {
        p4_game_input_t input = {
            .held = next_random(&random) & P4_BUTTON_MASK,
            .pressed = next_random(&random) & P4_BUTTON_MASK,
            .released = next_random(&random) & P4_BUTTON_MASK,
            .touch_valid = true,
        };
        const p4_game_result_t result = p4_game_instance_update(
            &instance, &input, next_random(&random) % 101U);
        CHECK(result >= P4_GAME_CONTINUE && result <= P4_GAME_ERROR);
        CHECK(state.player_x >= 97 && state.player_x <= 223);
        CHECK(state.invaders_remaining <= SPACE_INVADER_COUNT);
        CHECK(state.lives <= 3U);
        if (iteration % 7U == 0U) {
            CHECK(p4_game_instance_render(&instance, &surface));
        }
    }
    for (size_t i = 0U; i < GUARD; ++i) {
        CHECK(allocation[i] == UINT16_C(0x5aa5));
        CHECK(allocation[GUARD + WORDS + i] == UINT16_C(0x5aa5));
    }
    for (size_t row = 0U; row < height; ++row) {
        for (size_t column = width; column < STRIDE;
             ++column) {
            CHECK(surface.pixels[row * STRIDE + column] ==
                  UINT16_C(0x5aa5));
        }
    }
    p4_game_instance_stop(&instance);
    free(allocation);
}

static void test_presentation_tween_and_pause(void)
{
    p4_game_instance_t instance;
    space_invaders_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game(&instance, &state, &mixer));
    p4_game_input_t input = {0};
    state.formation_accumulator_ms = 469U;
    CHECK(p4_game_instance_update(&instance, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(state.formation_x == 102); /* Authoritative rules still take a 3-unit step. */
    CHECK(space_invaders_formation_visual_q8(&state, true) == 99 * 256);
    CHECK(p4_game_instance_update(&instance, &input, 16U) == P4_GAME_CONTINUE);
    const int midway = space_invaders_formation_visual_q8(&state, true);
    CHECK(midway > 99 * 256 && midway < 102 * 256);
    CHECK(state.formation_x == 102);
    input.pressed = P4_BUTTON_START;
    CHECK(p4_game_instance_update(&instance, &input, 16U) == P4_GAME_CONTINUE);
    const uint32_t clock = state.visual_clock_ms;
    input = (p4_game_input_t){0};
    CHECK(p4_game_instance_update(&instance, &input, 100U) == P4_GAME_CONTINUE);
    CHECK(state.visual_clock_ms == clock);
    CHECK(space_invaders_formation_visual_q8(&state, true) == midway);
    state.paused = false;
    for (unsigned i = 0U; i < 8U; ++i)
        CHECK(p4_game_instance_update(&instance, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(space_invaders_formation_visual_q8(&state, true) == 102 * 256);
    /* The shortest rule interval must finish its tween before the next step. */
    state.wave = UINT8_MAX;
    state.formation_from_x = 99;
    state.formation_x = 102;
    state.formation_tween_ms = 80U;
    CHECK(space_invaders_formation_visual_q8(&state, true) == 102 * 256);
    p4_game_instance_stop(&instance);
}

static void test_shield_holes_and_render_purity(unsigned width, unsigned height)
{
    p4_game_instance_t instance;
    space_invaders_state_t state;
    p4_audio_mixer_t mixer;
    CHECK(start_game_mode(&instance, &state, &mixer, width == 768U));
    uint16_t *pixels = calloc((size_t)width * height, sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels == NULL) return;
    p4_game_surface_t surface = {.pixels = pixels, .width = (uint16_t)width,
        .height = (uint16_t)height, .stride_pixels = (uint16_t)width};
    const space_invaders_state_t before = state;
    CHECK(p4_game_instance_render(&instance, &surface));
    CHECK(memcmp(&before, &state, sizeof(state)) == 0);
    uint16_t *intact = malloc((size_t)width * height * sizeof(*intact));
    uint16_t *missing = malloc((size_t)width * height * sizeof(*missing));
    CHECK(intact != NULL && missing != NULL);
    if (intact == NULL || missing == NULL) {
        free(intact); free(missing); free(pixels);
        p4_game_instance_stop(&instance);
        return;
    }
    memcpy(intact, pixels, (size_t)width * height * sizeof(*pixels));
    state.shields[0] &= (uint16_t)~UINT16_C(1);
    CHECK(p4_game_instance_render(&instance, &surface));
    memcpy(missing, pixels, (size_t)width * height * sizeof(*pixels));
    state.shields[0] = 0U;
    CHECK(p4_game_instance_render(&instance, &surface));
    /* Sample every pixel in the first cell, avoiding a resolution-specific
       opaque texel. A destroyed cell must show exactly the cleared background. */
    const unsigned left = (unsigned)((16 * 256 + (112 - 90) * 256 * 288 / 140) * (int)width / (320 * 256));
    const unsigned right = (unsigned)((16 * 256 + (116 - 90) * 256 * 288 / 140) * (int)width / (320 * 256));
    const unsigned top = (unsigned)((28 * 256 + (115 - 27) * 256 * 117 / 119) * (int)height / (200 * 256));
    const unsigned bottom = (unsigned)((28 * 256 + (118 - 27) * 256 * 117 / 119) * (int)height / (200 * 256));
    unsigned changed = 0U;
    for (unsigned y = top; y < bottom; ++y) {
        for (unsigned x = left; x < right; ++x) {
            const size_t at = (size_t)y * width + x;
            CHECK(missing[at] == pixels[at]);
            if (intact[at] != missing[at]) ++changed;
        }
    }
    CHECK(changed != 0U);
    /* The sprite's transparent outer corner must never acquire a backing box. */
    CHECK(intact[(size_t)top * width + left] == pixels[(size_t)top * width + left]);
    free(intact);
    free(missing);
    free(pixels);
    p4_game_instance_stop(&instance);
}

static void test_substep_motion(void)
{
    p4_game_instance_t game;space_invaders_state_t state;p4_audio_mixer_t mixer;
    CHECK(start_game(&game,&state,&mixer));
    state.player_x=110;state.previous_player_x=110;
    p4_game_input_t input={.held=P4_BUTTON_RIGHT};
    int previous=110*256,maximum=0;
    for(unsigned t=1U;t<=300U;++t){
        CHECK(p4_game_instance_update(&game,&input,1U)==P4_GAME_CONTINUE);
        const int current=space_invaders_motion_visual_q8(state.previous_player_x,state.player_x,state.simulation_accumulator_ms);
        const int delta=current-previous;
        CHECK(delta>=0&&delta<=32);
        if(t>16U)CHECK(delta==32);
        if(delta>maximum)maximum=delta;
        CHECK(state.player_x==110+(int)(t/16U)*2); /* Original rules unchanged. */
        previous=current;
    }
    /* A shot gets a valid birth sample and moves continuously between ticks. */
    state.shields[0]=state.shields[1]=state.shields[2]=0U;
    state.simulation_accumulator_ms=0U;
    input=(p4_game_input_t){.pressed=P4_BUTTON_A};
    CHECK(p4_game_instance_update(&game,&input,0U)==P4_GAME_CONTINUE);
    CHECK(state.player_projectiles[0].previous_y==state.player_projectiles[0].y);
    input=(p4_game_input_t){0};previous=state.player_projectiles[0].y*256;
    for(unsigned t=1U;t<=96U;++t){
        CHECK(p4_game_instance_update(&game,&input,1U)==P4_GAME_CONTINUE);
        const space_projectile_t *shot=&state.player_projectiles[0];
        CHECK(shot->active);
        const int current=space_invaders_motion_visual_q8(shot->previous_y,shot->y,state.simulation_accumulator_ms);
        CHECK(previous-current>=0&&previous-current<=64);
        if(t>16U)CHECK(previous-current==64);
        previous=current;
    }
    const int before=space_invaders_motion_visual_q8(state.previous_player_x,state.player_x,state.simulation_accumulator_ms);
    state.paused=true;
    CHECK(p4_game_instance_update(&game,&input,100U)==P4_GAME_CONTINUE);
    CHECK(space_invaders_motion_visual_q8(state.previous_player_x,state.player_x,state.simulation_accumulator_ms)==before);
    printf("Invaders motion: ship max Q8 delta %d per 1 ms (original tick 512); projectile max 64 (original tick 1024); rules/birth/pause preserved.\n",maximum);
    state.paused=false;state.player_x=100;state.previous_player_x=98;
    state.simulation_accumulator_ms=0U;input=(p4_game_input_t){.held=P4_BUTTON_RIGHT};
    int raw=state.player_x*256,visual=98*256,raw_max=0,visual_max=0;
    for(unsigned frame=0U;frame<50U;++frame){
        CHECK(p4_game_instance_update(&game,&input,frame%3U==0U?16U:17U)==P4_GAME_CONTINUE);
        const int next_raw=state.player_x*256;
        const int next_visual=space_invaders_motion_visual_q8(state.previous_player_x,state.player_x,state.simulation_accumulator_ms);
        const int dr=next_raw-raw,dv=next_visual-visual;
        if(dr>raw_max)raw_max=dr;if(dv>visual_max)visual_max=dv;
        raw=next_raw;visual=next_visual;
    }
    CHECK(raw_max==1024&&visual_max==544);
    printf("Invaders 60 Hz/50 frames: max Q8 ship displacement %d -> %d (about 20 -> 11 native pixels).\n",raw_max,visual_max);
    p4_game_instance_stop(&game);
}

static void test_held_fire_and_back_priority(void)
{
    for(unsigned button=0U;button<2U;++button){
        p4_game_instance_t game;space_invaders_state_t state;p4_audio_mixer_t mixer;
        CHECK(start_game(&game,&state,&mixer));
        state.shields[0]=state.shields[1]=state.shields[2]=0U;
        p4_game_input_t input={.held=button?P4_BUTTON_B:P4_BUTTON_A};
        unsigned launches=0U,last_launch=0U;
        for(unsigned tick=1U;tick<=63U;++tick){
            const uint32_t cooldown=state.player_cooldown_ms;
            CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);
            if(state.player_cooldown_ms>cooldown){
                if(last_launch)CHECK((tick-last_launch)*16U>=220U);
                last_launch=tick;++launches;
            }
            unsigned active=0U;
            for(unsigned i=0;i<SPACE_PLAYER_PROJECTILE_COUNT;++i)
                if(state.player_projectiles[i].active)++active;
            CHECK(active<=2U);
        }
        CHECK(launches==5U);
        input=(p4_game_input_t){0};
        for(unsigned tick=0U;tick<20U;++tick)
            CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);
        CHECK(state.player_cooldown_ms==0U);
        p4_game_instance_stop(&game);
    }
    for(unsigned phase=0U;phase<3U;++phase){
        p4_game_instance_t game;space_invaders_state_t state;p4_audio_mixer_t mixer;
        CHECK(start_game(&game,&state,&mixer));
        state.paused=phase==1U;state.game_over=phase==2U;state.score=90U;
        p4_game_input_t input={.held=P4_BUTTON_BACK|P4_BUTTON_A,
            .pressed=P4_BUTTON_BACK|P4_BUTTON_A};
        CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_EXIT_TO_LAUNCHER);
        CHECK(state.score==90U); /* Back must win over restart even at game over. */
        p4_game_instance_stop(&game);
    }
    puts("Held A/B: five launches in 1008 ms, >=220 ms cadence, <=2 slots, release stops firing; Back wins active/paused/game-over even with A.");
}

static p4_game_input_t mapped_touch(p4_game_input_mapper_t *mapper,int x,int y)
{
    p4_game_input_t input;
    p4_physical_touch_t touch={.x=(uint16_t)(P4_INPUT_VIEWPORT_LEFT+
        ((unsigned)x*P4_INPUT_VIEWPORT_WIDTH+319U)/320U),
        .y=(uint16_t)(P4_INPUT_VIEWPORT_TOP+
        ((unsigned)y*P4_INPUT_VIEWPORT_HEIGHT+199U)/200U)};
    p4_game_input_mapper_update(mapper,true,x<0?NULL:&touch,x<0?0U:1U,0U,&input);
    return input;
}

static void test_title_launch_and_touch(void)
{
    for(unsigned mode=0U;mode<2U;++mode){
        p4_game_instance_t game;space_invaders_state_t state;p4_audio_mixer_t mixer;
        CHECK(start_title_mode(&game,&state,&mixer,mode!=0U));
        const space_invaders_state_t initial=state;
        p4_game_input_t input={.held=P4_BUTTON_LEFT|P4_BUTTON_B,.pressed=P4_BUTTON_LEFT|P4_BUTTON_B};
        for(unsigned frame=0U;frame<200U;++frame){
            CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);
            input.pressed=0U;
        }
        CHECK(state.intro&&state.player_x==initial.player_x&&state.score==0U);
        CHECK(state.formation_x==initial.formation_x&&state.formation_y==initial.formation_y);
        CHECK(state.random_state==initial.random_state&&state.simulation_accumulator_ms==0U);
        CHECK(state.enemy_fire_accumulator_ms==0U&&state.formation_accumulator_ms==0U);
        CHECK(state.lives==3U&&state.player_cooldown_ms==0U);
        CHECK(memcmp(state.shields,initial.shields,sizeof(state.shields))==0);
        CHECK(memcmp(state.player_projectiles,initial.player_projectiles,sizeof(state.player_projectiles))==0);
        CHECK(memcmp(state.enemy_projectiles,initial.enemy_projectiles,sizeof(state.enemy_projectiles))==0);
        input=(p4_game_input_t){.held=P4_BUTTON_A,.pressed=P4_BUTTON_A};
        CHECK(p4_game_instance_update(&game,&input,100U)==P4_GAME_CONTINUE);
        CHECK(!state.intro&&!state.paused&&state.launch_input_blocked);
        CHECK(state.simulation_accumulator_ms==0U&&state.formation_accumulator_ms==0U);
        input.pressed=0U;
        for(unsigned i=0U;i<30U;++i)CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);
        CHECK(state.player_cooldown_ms==0U&&!state.player_projectiles[0].active);
        input=(p4_game_input_t){0};CHECK(p4_game_instance_update(&game,&input,0U)==P4_GAME_CONTINUE);
        input=(p4_game_input_t){.held=P4_BUTTON_A,.pressed=P4_BUTTON_A};
        CHECK(p4_game_instance_update(&game,&input,0U)==P4_GAME_CONTINUE);
        CHECK(state.player_projectiles[0].active&&state.player_cooldown_ms==220U);
        state.game_over=true;input=(p4_game_input_t){.held=P4_BUTTON_START,.pressed=P4_BUTTON_START};
        CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);
        CHECK(!state.intro&&!state.paused&&!state.game_over&&state.lives==3U);
        input.pressed=0U;CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);
        CHECK(!state.paused&&state.player_cooldown_ms==0U);
        p4_game_instance_stop(&game);

        CHECK(start_title_mode(&game,&state,&mixer,mode!=0U));
        p4_game_input_mapper_t mapper; p4_game_input_mapper_init(&mapper);
        /* Hidden old Start/A regions, and a drag into a control, are inert. */
        input=mapped_touch(&mapper,290,10);CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);CHECK(state.intro);
        input=mapped_touch(&mapper,244,165);CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);CHECK(state.intro);
        input=mapped_touch(&mapper,20,10);CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);CHECK(state.intro);
        input=mapped_touch(&mapper,-1,-1);CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);
        input=mapped_touch(&mapper,302,157);CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);CHECK(state.intro);
        input=mapped_touch(&mapper,-1,-1);CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);
        input=mapped_touch(&mapper,244,165);CHECK(p4_game_instance_update(&game,&input,100U)==P4_GAME_CONTINUE);
        CHECK(!state.intro&&state.launch_input_blocked&&!state.player_projectiles[0].active);
        input.pressed=0U;
        for(unsigned i=0U;i<20U;++i)CHECK(p4_game_instance_update(&game,&input,8U)==P4_GAME_CONTINUE);
        CHECK(state.player_cooldown_ms==0U); /* Same contact across service slices. */
        input=mapped_touch(&mapper,-1,-1);CHECK(p4_game_instance_update(&game,&input,8U)==P4_GAME_CONTINUE);
        input=mapped_touch(&mapper,286,158);CHECK(p4_game_instance_update(&game,&input,0U)==P4_GAME_CONTINUE);
        CHECK(state.player_projectiles[0].active);
        p4_game_instance_stop(&game);
        CHECK(start_title_mode(&game,&state,&mixer,mode!=0U));
        input=(p4_game_input_t){.pressed=P4_BUTTON_BACK|P4_BUTTON_A};
        CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_EXIT_TO_LAUNCHER);CHECK(state.intro);
        p4_game_instance_stop(&game);
        CHECK(start_title_mode(&game,&state,&mixer,mode!=0U));
        input=(p4_game_input_t){.pressed=P4_BUTTON_START,.held=P4_BUTTON_START};
        CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_CONTINUE);CHECK(!state.intro&&!state.paused);
        p4_game_instance_stop(&game);
        CHECK(start_title_mode(&game,&state,&mixer,mode!=0U));
        p4_game_input_mapper_init(&mapper);input=mapped_touch(&mapper,20,10);
        CHECK(p4_game_instance_update(&game,&input,16U)==P4_GAME_EXIT_TO_LAUNCHER);
        CHECK(state.intro);p4_game_instance_stop(&game);
    }
    puts("Title lifecycle: combat/RNG frozen while waiting; A/Start/canonical Play launch without firing; held launch blocks across slices until release; hidden touch zones and dragged-in actions ignored; retry and Back preserved at both resolutions.");
}

int main(void)
{
    test_descriptor_and_reset();
    test_title_launch_and_touch();
    test_held_fire_and_back_priority();
    test_substep_motion();
    test_controls_sound_and_exit();
    test_hits_wave_and_player_damage();
    test_presentation_tween_and_pause();
    test_shield_holes_and_render_purity(320U, 200U);
    test_shield_holes_and_render_purity(768U, 480U);
    test_render_bounds_and_fuzz(320U, 200U);
    test_render_bounds_and_fuzz(768U, 480U);
    if (s_failures != 0) {
        fprintf(stderr, "%d space-invaders test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("space-invaders tests passed");
    return EXIT_SUCCESS;
}
