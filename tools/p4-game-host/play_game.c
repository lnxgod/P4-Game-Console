// SPDX-License-Identifier: MIT

#include <SDL3/SDL.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/audio.h"
#include "p4/game.h"
#include "p4/input.h"

extern const p4_game_descriptor_t P4_HOST_GAME_DESCRIPTOR;

enum {
    HOST_SCALE = 3,
    HOST_FRAME_MS = 16,
    HOST_AUDIO_FRAMES = 256,
    HOST_MAX_SMOKE_FRAMES = 100000,
};

typedef struct {
    SDL_AudioStream *stream;
    p4_audio_mixer_t mixer;
} host_audio_t;

static bool host_play_tone(void *context, const p4_tone_t *tone)
{
    host_audio_t *const audio = context;
    if (audio == NULL || audio->stream == NULL) {
        return false;
    }
    return p4_audio_mixer_play_tone(&audio->mixer, tone);
}

static void host_stop_audio(void *context)
{
    host_audio_t *const audio = context;
    if (audio == NULL || audio->stream == NULL) {
        return;
    }
    p4_audio_mixer_stop_all(&audio->mixer);
}

static bool start_audio(host_audio_t *audio)
{
    if (audio == NULL) {
        return false;
    }
    *audio = (host_audio_t){0};
    p4_audio_mixer_init(&audio->mixer);
    const SDL_AudioSpec requested = {
        .format = SDL_AUDIO_S16,
        .channels = P4_GAME_AUDIO_CHANNEL_COUNT,
        .freq = P4_GAME_AUDIO_SAMPLE_RATE_HZ,
    };
    audio->stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &requested, NULL, NULL);
    if (audio->stream == NULL) {
        fprintf(stderr, "audio disabled: %s\n", SDL_GetError());
        return false;
    }
    return SDL_ResumeAudioStreamDevice(audio->stream);
}

static bool pump_audio(host_audio_t *audio)
{
    if (audio == NULL || audio->stream == NULL) {
        return false;
    }
    int16_t samples[HOST_AUDIO_FRAMES * P4_GAME_AUDIO_CHANNEL_COUNT];
    if (!p4_audio_mixer_render(
            &audio->mixer, samples, HOST_AUDIO_FRAMES)) {
        return false;
    }
    return SDL_PutAudioStreamData(
        audio->stream, samples, (int)sizeof(samples));
}

static uint32_t digital_buttons(void)
{
    const bool *const keys = SDL_GetKeyboardState(NULL);
    uint32_t buttons = 0U;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) {
        buttons |= P4_BUTTON_UP;
    }
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) {
        buttons |= P4_BUTTON_DOWN;
    }
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) {
        buttons |= P4_BUTTON_LEFT;
    }
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) {
        buttons |= P4_BUTTON_RIGHT;
    }
    if (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_Z]) {
        buttons |= P4_BUTTON_A;
    }
    if (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_LSHIFT]) {
        buttons |= P4_BUTTON_B;
    }
    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_P]) {
        buttons |= P4_BUTTON_START;
    }
    if (keys[SDL_SCANCODE_ESCAPE] ||
        keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_Q]) {
        buttons |= P4_BUTTON_BACK;
    }
    return buttons;
}

static size_t mouse_touch(SDL_Renderer *renderer,
                          p4_physical_touch_t touch[1])
{
    float window_x = 0.0F;
    float window_y = 0.0F;
    const SDL_MouseButtonFlags state = SDL_GetMouseState(&window_x, &window_y);
    if ((state & SDL_BUTTON_LMASK) == 0U) {
        return 0U;
    }
    float logical_x = 0.0F;
    float logical_y = 0.0F;
    if (!SDL_RenderCoordinatesFromWindow(
            renderer, window_x, window_y, &logical_x, &logical_y) ||
        logical_x < 0.0F || logical_y < 0.0F ||
        logical_x >= (float)P4_GAME_SURFACE_WIDTH ||
        logical_y >= (float)P4_GAME_SURFACE_HEIGHT) {
        return 0U;
    }
    const uint16_t x = (uint16_t)logical_x;
    const uint16_t y = (uint16_t)logical_y;
    touch[0] = (p4_physical_touch_t){
        .x = (uint16_t)(P4_INPUT_VIEWPORT_LEFT +
                        ((uint32_t)x * P4_INPUT_VIEWPORT_WIDTH) /
                            P4_GAME_SURFACE_WIDTH),
        .y = (uint16_t)(P4_INPUT_VIEWPORT_TOP +
                        ((uint32_t)y * P4_INPUT_VIEWPORT_HEIGHT) /
                            P4_GAME_SURFACE_HEIGHT),
    };
    return 1U;
}

static bool parse_max_frames(int argc, char **argv, uint32_t *max_frames)
{
    if (max_frames == NULL) {
        return false;
    }
    *max_frames = 0U;
    if (argc == 1) {
        return true;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("usage: %s [--frames COUNT]\n", argv[0]);
        printf("keys: arrows/WASD move, Space/Z=A, X/Shift=B, "
               "Enter/P=Start, Esc/Backspace/Q=Back\n");
        exit(EXIT_SUCCESS);
    }
    if (argc != 3 || strcmp(argv[1], "--frames") != 0) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    const unsigned long value = strtoul(argv[2], &end, 10);
    if (errno != 0 || end == argv[2] || *end != '\0' || value == 0UL ||
        value > HOST_MAX_SMOKE_FRAMES) {
        return false;
    }
    *max_frames = (uint32_t)value;
    return true;
}

int main(int argc, char **argv)
{
    uint32_t max_frames = 0U;
    if (!parse_max_frames(argc, argv, &max_frames)) {
        fprintf(stderr, "usage: %s [--frames 1..%d]\n",
                argv[0], HOST_MAX_SMOKE_FRAMES);
        return EXIT_FAILURE;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_HIGH_PIXEL_DENSITY |
        (max_frames == 0U ? 0U : SDL_WINDOW_HIDDEN);
    SDL_Window *const window = SDL_CreateWindow(
        P4_HOST_GAME_DESCRIPTOR.title,
        P4_GAME_SURFACE_WIDTH * HOST_SCALE,
        P4_GAME_SURFACE_HEIGHT * HOST_SCALE,
        window_flags);
    if (window == NULL) {
        fprintf(stderr, "window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_Renderer *const renderer = SDL_CreateRenderer(window, NULL);
    if (renderer == NULL ||
        !SDL_SetRenderLogicalPresentation(
            renderer, P4_GAME_SURFACE_WIDTH,
            P4_GAME_SURFACE_HEIGHT,
            SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)) {
        fprintf(stderr, "renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_Texture *const texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        P4_GAME_SURFACE_WIDTH, P4_GAME_SURFACE_HEIGHT);
    if (texture != NULL) {
        (void)SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    uint16_t *const pixels = calloc(
        (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT,
        sizeof(*pixels));
    void *const state_memory = calloc(
        1U, P4_HOST_GAME_DESCRIPTOR.state_bytes);
    if (texture == NULL || pixels == NULL || state_memory == NULL) {
        fprintf(stderr, "host game allocation failed: %s\n", SDL_GetError());
        free(state_memory);
        free(pixels);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    host_audio_t audio;
    const bool audio_ready = start_audio(&audio);
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            (audio_ready ? P4_GAME_CAP_AUDIO_TONE : 0U),
        .audio_context = audio_ready ? &audio : NULL,
        .play_tone = audio_ready ? host_play_tone : NULL,
        .submit_pcm16_stereo = NULL,
        .stop_audio = audio_ready ? host_stop_audio : NULL,
    };
    p4_game_instance_t instance = {0};
    if (!p4_game_instance_start(
            &instance, &P4_HOST_GAME_DESCRIPTOR, &services,
            state_memory, P4_HOST_GAME_DESCRIPTOR.state_bytes)) {
        fprintf(stderr, "game start failed\n");
        if (audio_ready) {
            SDL_DestroyAudioStream(audio.stream);
        }
        free(state_memory);
        free(pixels);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    printf("Playing %s (%s)\n", P4_HOST_GAME_DESCRIPTOR.title,
           P4_HOST_GAME_DESCRIPTOR.id);
    printf("Arrows/WASD move | Space/Z A | X/Shift B | "
           "Enter/P Start | Esc/Backspace/Q Back | mouse = touch\n");

    p4_game_input_mapper_t mapper;
    p4_game_input_mapper_init(&mapper);
    p4_game_surface_t surface = {
        .pixels = pixels,
        .stride_pixels = P4_GAME_SURFACE_WIDTH,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    bool running = true;
    uint32_t rendered_frames = 0U;
    Uint64 previous_tick = SDL_GetTicks();
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        if (!running) {
            break;
        }
        Uint64 now = SDL_GetTicks();
        if (max_frames == 0U && now - previous_tick < HOST_FRAME_MS) {
            SDL_Delay((Uint32)(HOST_FRAME_MS - (now - previous_tick)));
            now = SDL_GetTicks();
        }
        uint64_t elapsed64 = max_frames == 0U
            ? now - previous_tick : HOST_FRAME_MS;
        previous_tick = now;
        if (elapsed64 == 0U) {
            elapsed64 = 1U;
        }
        if (elapsed64 > P4_GAME_MAX_FRAME_DELTA_MS) {
            elapsed64 = P4_GAME_MAX_FRAME_DELTA_MS;
        }

        p4_physical_touch_t touch[1];
        const size_t touch_count = mouse_touch(renderer, touch);
        p4_game_input_t input;
        p4_game_input_mapper_update(
            &mapper, true, touch, touch_count,
            digital_buttons(), &input);
        const p4_game_result_t result = p4_game_instance_update(
            &instance, &input, (uint32_t)elapsed64);
        if (result == P4_GAME_EXIT_TO_LAUNCHER) {
            running = false;
            continue;
        }
        if ((audio_ready && !pump_audio(&audio)) ||
            result != P4_GAME_CONTINUE ||
            !p4_game_instance_render(&instance, &surface) ||
            !SDL_UpdateTexture(
                texture, NULL, pixels,
                P4_GAME_SURFACE_WIDTH * (int)sizeof(*pixels)) ||
            !SDL_SetRenderDrawColor(renderer, 0U, 0U, 0U, 255U) ||
            !SDL_RenderClear(renderer) ||
            !SDL_RenderTexture(renderer, texture, NULL, NULL) ||
            !SDL_RenderPresent(renderer)) {
            fprintf(stderr, "game frame failed: %s\n", SDL_GetError());
            running = false;
            continue;
        }
        ++rendered_frames;
        if (max_frames != 0U && rendered_frames >= max_frames) {
            running = false;
        }
    }

    p4_game_instance_stop(&instance);
    if (audio_ready) {
        SDL_DestroyAudioStream(audio.stream);
    }
    printf("Stopped after %" PRIu32 " frames\n", rendered_frames);
    free(state_memory);
    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
