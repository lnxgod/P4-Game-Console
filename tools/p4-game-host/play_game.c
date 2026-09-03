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
#include "p4/achievements.h"
#include "p4/game.h"
#include "p4/game_save.h"
#include "p4/input.h"
#include "p4/signal_scan.h"

extern const p4_game_descriptor_t P4_HOST_GAME_DESCRIPTOR;

#ifndef P4_HOST_RESOURCE_PATH
#define P4_HOST_RESOURCE_PATH ""
#endif

enum {
    HOST_SCALE = 3,
    HOST_SERVICE_INTERVAL_MS = 16,
    HOST_RENDER_DIVISOR = 2,
    HOST_RENDER_TARGET_FPS =
        1000 / (HOST_SERVICE_INTERVAL_MS * HOST_RENDER_DIVISOR),
    HOST_MASTER_VOLUME_STEP = 8,
    HOST_AUDIO_FRAMES = 256,
    HOST_MAX_SMOKE_FRAMES = 100000,
};

typedef struct {
    SDL_AudioStream *stream;
    p4_audio_mixer_t mixer;
} host_audio_t;

typedef struct {
    p4_game_signal_snapshot_t snapshot;
    uint64_t focus_token;
    uint8_t focus_steps;
} host_signal_scan_t;

static bool host_request_signal_scan(void *context, uint64_t focus_token)
{
    static const uint8_t key[P4_SIGNAL_SCAN_KEY_BYTES] = {
        UINT8_C(0x42), UINT8_C(0x79), UINT8_C(0x74), UINT8_C(0x65),
        UINT8_C(0x42), UINT8_C(0x75), UINT8_C(0x64), UINT8_C(0x64),
        UINT8_C(0x79), UINT8_C(0x53), UINT8_C(0x65), UINT8_C(0x65),
        UINT8_C(0x64), UINT8_C(0x30), UINT8_C(0x30), UINT8_C(0x31),
    };
    static const char *const labels[] = {
        "SKY GARDEN", "LIBRARY MESH", "MOONLIGHT", "STAR PORT", "",
    };
    static const uint8_t bssids[][P4_SIGNAL_SCAN_BSSID_BYTES] = {
        {0x02U, 0x10U, 0x20U, 0x30U, 0x40U, 0x50U},
        {0x02U, 0x11U, 0x21U, 0x31U, 0x41U, 0x51U},
        {0x02U, 0x12U, 0x22U, 0x32U, 0x42U, 0x52U},
        {0x02U, 0x13U, 0x23U, 0x33U, 0x43U, 0x53U},
        {0x02U, 0x14U, 0x24U, 0x34U, 0x44U, 0x54U},
    };
    static const int8_t base_rssi[] = {-84, -73, -67, -78, -62};
    static const uint8_t channels[] = {1U, 6U, 11U, 36U, 44U};
    host_signal_scan_t *const scan = context;
    if (scan == NULL) {
        return false;
    }
    if (focus_token != scan->focus_token) {
        scan->focus_token = focus_token;
        scan->focus_steps = 0U;
    } else if (focus_token != 0U && scan->focus_steps < 7U) {
        ++scan->focus_steps;
    }
    if (focus_token != 0U && scan->focus_steps == 0U) {
        scan->focus_steps = 1U;
    }
    uint32_t generation = scan->snapshot.generation;
    if (generation != UINT32_MAX) {
        ++generation;
    }
    scan->snapshot = (p4_game_signal_snapshot_t){
        .generation = generation,
        .status = P4_GAME_SIGNAL_READY,
        .count = 5U,
    };
    for (size_t index = 0U; index < scan->snapshot.count; ++index) {
        const size_t label_bytes = strlen(labels[index]);
        if (!p4_signal_scan_make_game_signal(
                key, bssids[index], (const uint8_t *)labels[index],
                label_bytes, base_rssi[index], channels[index],
                index != 4U, &scan->snapshot.results[index])) {
            scan->snapshot = (p4_game_signal_snapshot_t){
                .generation = generation,
                .status = P4_GAME_SIGNAL_ERROR,
            };
            return false;
        }
        scan->snapshot.results[index].flags |= P4_GAME_SIGNAL_SIMULATED;
        if (scan->snapshot.results[index].token == focus_token) {
            int adjusted = (int)base_rssi[index] +
                (int)scan->focus_steps * 9;
            if (adjusted > -34) {
                adjusted = -34;
            }
            scan->snapshot.results[index].rssi_dbm = (int8_t)adjusted;
        }
    }
    return true;
}

static bool host_read_signal_scan(void *context,
                                  p4_game_signal_snapshot_t *snapshot)
{
    const host_signal_scan_t *const scan = context;
    if (scan == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = scan->snapshot;
    return true;
}

static uint8_t *load_resource(size_t *out_bytes)
{
    if (out_bytes == NULL || P4_HOST_RESOURCE_PATH[0] == '\0') {
        return NULL;
    }
    FILE *const file = fopen(P4_HOST_RESOURCE_PATH, "rb");
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return NULL;
    }
    const long file_bytes = ftell(file);
    if (file_bytes <= 0L || file_bytes > 8L * 1024L * 1024L ||
        fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    uint8_t *const data = malloc((size_t)file_bytes);
    if (data == NULL) {
        (void)fclose(file);
        return NULL;
    }
    const bool read_ok = fread(data, (size_t)file_bytes, 1U, file) == 1U;
    const bool close_ok = fclose(file) == 0;
    if (!read_ok || !close_ok) {
        free(data);
        return NULL;
    }
    *out_bytes = (size_t)file_bytes;
    return data;
}

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

static bool host_submit_pcm16_stereo(
    void *context,
    const int16_t *interleaved_stereo,
    size_t frame_count)
{
    host_audio_t *const audio = context;
    return audio != NULL && audio->stream != NULL &&
        p4_audio_mixer_submit_pcm16_stereo(
            &audio->mixer, interleaved_stereo, frame_count);
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
    if (!SDL_SetAudioStreamGain(
            audio->stream, (float)HOST_MASTER_VOLUME_STEP / 10.0F) ||
        !SDL_ResumeAudioStreamDevice(audio->stream)) {
        fprintf(stderr, "audio disabled: %s\n", SDL_GetError());
        SDL_DestroyAudioStream(audio->stream);
        audio->stream = NULL;
        return false;
    }
    return true;
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

static uint32_t button_for_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_UP: case SDLK_W: return P4_BUTTON_UP;
    case SDLK_DOWN: case SDLK_S: return P4_BUTTON_DOWN;
    case SDLK_LEFT: case SDLK_A: return P4_BUTTON_LEFT;
    case SDLK_RIGHT: case SDLK_D: return P4_BUTTON_RIGHT;
    case SDLK_SPACE: case SDLK_Z: return P4_BUTTON_A;
    case SDLK_X: case SDLK_LSHIFT: return P4_BUTTON_B;
    case SDLK_RETURN: case SDLK_P: return P4_BUTTON_START;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_Q:
        return P4_BUTTON_BACK;
    default: return 0U;
    }
}

static bool touch_from_window(SDL_Renderer *renderer,
                              float window_x,
                              float window_y,
                              uint16_t surface_width,
                              uint16_t surface_height,
                              p4_physical_touch_t *touch)
{
    if (touch == NULL) {
        return false;
    }
    float logical_x = 0.0F;
    float logical_y = 0.0F;
    if (!SDL_RenderCoordinatesFromWindow(
            renderer, window_x, window_y, &logical_x, &logical_y) ||
        logical_x < 0.0F || logical_y < 0.0F ||
        logical_x >= (float)surface_width ||
        logical_y >= (float)surface_height) {
        return false;
    }
    const uint16_t x = (uint16_t)logical_x;
    const uint16_t y = (uint16_t)logical_y;
    *touch = (p4_physical_touch_t){
        .x = (uint16_t)(P4_INPUT_VIEWPORT_LEFT +
                        ((uint32_t)x * P4_INPUT_VIEWPORT_WIDTH) /
                            surface_width),
        .y = (uint16_t)(P4_INPUT_VIEWPORT_TOP +
                        ((uint32_t)y * P4_INPUT_VIEWPORT_HEIGHT) /
                            surface_height),
    };
    return true;
}

static size_t mouse_touch(SDL_Renderer *renderer,
                          uint16_t surface_width,
                          uint16_t surface_height,
                          p4_physical_touch_t touch[1])
{
    float window_x = 0.0F;
    float window_y = 0.0F;
    const SDL_MouseButtonFlags state = SDL_GetMouseState(&window_x, &window_y);
    if ((state & SDL_BUTTON_LMASK) == 0U) {
        return 0U;
    }
    return touch_from_window(
        renderer, window_x, window_y,
        surface_width, surface_height, &touch[0])
        ? 1U : 0U;
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
    const uint32_t requested_capabilities =
        P4_HOST_GAME_DESCRIPTOR.required_capabilities |
        P4_HOST_GAME_DESCRIPTOR.optional_capabilities;
    const bool high_res =
        (requested_capabilities & P4_GAME_CAP_VIDEO_HIGH_RES) != 0U;
    const uint16_t surface_width = high_res
        ? P4_GAME_SURFACE_HIGH_RES_WIDTH : P4_GAME_SURFACE_WIDTH;
    const uint16_t surface_height = high_res
        ? P4_GAME_SURFACE_HIGH_RES_HEIGHT : P4_GAME_SURFACE_HEIGHT;
    const int window_scale = high_res ? 1 : HOST_SCALE;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_HIGH_PIXEL_DENSITY |
        (max_frames == 0U ? 0U : SDL_WINDOW_HIDDEN);
    SDL_Window *const window = SDL_CreateWindow(
        P4_HOST_GAME_DESCRIPTOR.title,
        (int)surface_width * window_scale,
        (int)surface_height * window_scale,
        window_flags);
    if (window == NULL) {
        fprintf(stderr, "window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_Renderer *const renderer = SDL_CreateRenderer(window, NULL);
    if (renderer == NULL ||
        !SDL_SetRenderLogicalPresentation(
            renderer, surface_width, surface_height,
            SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)) {
        fprintf(stderr, "renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_Texture *const texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        surface_width, surface_height);
    if (texture != NULL) {
        (void)SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    uint16_t *const pixels = calloc(
        (size_t)surface_width * surface_height,
        sizeof(*pixels));
    void *const state_memory = calloc(
        1U, P4_HOST_GAME_DESCRIPTOR.state_bytes);
    uint8_t *const save_workspace =
        malloc(P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES);
    if (texture == NULL || pixels == NULL || state_memory == NULL ||
        save_workspace == NULL) {
        fprintf(stderr, "host game allocation failed: %s\n", SDL_GetError());
        free(save_workspace);
        free(state_memory);
        free(pixels);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    size_t resource_bytes = 0U;
    uint8_t *const resource_data = load_resource(&resource_bytes);
    if (P4_HOST_RESOURCE_PATH[0] != '\0' && resource_data == NULL) {
        fprintf(stderr, "resource load failed: %s\n", P4_HOST_RESOURCE_PATH);
        free(save_workspace);
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
    p4_achievement_catalog_t achievements;
    p4_achievement_catalog_init(&achievements);
    host_signal_scan_t signal_scan = {
        .snapshot = {.status = P4_GAME_SIGNAL_IDLE},
    };
    p4_game_save_memory_t save_memory;
    if (!p4_game_save_memory_init(
            &save_memory, P4_HOST_GAME_DESCRIPTOR.id, save_workspace,
            P4_GAME_SAVE_MEMORY_WORKSPACE_BYTES)) {
        fprintf(stderr, "save service init failed\n");
        if (audio_ready) {
            SDL_DestroyAudioStream(audio.stream);
        }
        free(resource_data);
        free(save_workspace);
        free(state_memory);
        free(pixels);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            (high_res ? P4_GAME_CAP_VIDEO_HIGH_RES : 0U) |
            (audio_ready
                ? P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_AUDIO_STREAM : 0U) |
            (resource_data != NULL ? P4_GAME_CAP_STORAGE : 0U) |
            P4_GAME_CAP_SIGNAL_SCAN | P4_GAME_CAP_SAVE,
        .audio_context = audio_ready ? &audio : NULL,
        .game_id = P4_HOST_GAME_DESCRIPTOR.id,
        .play_tone = audio_ready ? host_play_tone : NULL,
        .submit_pcm16_stereo = audio_ready
            ? host_submit_pcm16_stereo : NULL,
        .stop_audio = audio_ready ? host_stop_audio : NULL,
        .achievement_context = &achievements,
        .unlock_achievement = p4_achievement_catalog_service_unlock,
        .resource_data = resource_data,
        .resource_bytes = resource_bytes,
        .resource_format_version = resource_data != NULL ? 1U : 0U,
        .signal_scan_context = &signal_scan,
        .request_signal_scan = host_request_signal_scan,
        .read_signal_scan = host_read_signal_scan,
        .save_context = &save_memory,
        .queue_save = p4_game_save_memory_queue,
        .read_save_status = p4_game_save_memory_read_status,
    };
    p4_game_instance_t instance = {0};
    if (!p4_game_instance_start(
            &instance, &P4_HOST_GAME_DESCRIPTOR, &services,
            state_memory, P4_HOST_GAME_DESCRIPTOR.state_bytes)) {
        fprintf(stderr, "game start failed\n");
        if (audio_ready) {
            SDL_DestroyAudioStream(audio.stream);
        }
        free(resource_data);
        free(save_workspace);
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
    printf("Surface: %ux%u RGB565 (touch input: %ux%u)\n",
           (unsigned)surface_width, (unsigned)surface_height,
           (unsigned)P4_GAME_SURFACE_WIDTH,
           (unsigned)P4_GAME_SURFACE_HEIGHT);
    printf("Arrows/WASD move | Space/Z A | X/Shift B | "
           "Enter/P Start | Esc/Backspace/Q Back | mouse = touch\n");
    printf("Timing: %d ms updates/audio | %d FPS render target | "
           "master volume %d/10\n",
           HOST_SERVICE_INTERVAL_MS, HOST_RENDER_TARGET_FPS,
           HOST_MASTER_VOLUME_STEP);

    p4_game_input_mapper_t mapper;
    p4_game_input_mapper_init(&mapper);
    p4_game_surface_t surface = {
        .pixels = pixels,
        .stride_pixels = surface_width,
        .width = surface_width,
        .height = surface_height,
    };
    bool running = true;
    uint32_t service_updates = 0U;
    uint32_t rendered_frames = 0U;
    unsigned render_phase = 0U;
    Uint64 previous_tick = SDL_GetTicks();
    while (running) {
        uint32_t pulsed_buttons = 0U;
        p4_physical_touch_t pulsed_touch = {0};
        bool pulsed_touch_valid = false;
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       !event.key.repeat) {
                pulsed_buttons |= button_for_key(event.key.key);
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                       event.button.button == SDL_BUTTON_LEFT) {
                pulsed_touch_valid = touch_from_window(
                    renderer, event.button.x, event.button.y,
                    surface_width, surface_height, &pulsed_touch);
            }
        }
        if (!running) {
            break;
        }
        Uint64 now = SDL_GetTicks();
        if (max_frames == 0U &&
            now - previous_tick < HOST_SERVICE_INTERVAL_MS) {
            SDL_Delay((Uint32)(HOST_SERVICE_INTERVAL_MS -
                               (now - previous_tick)));
            now = SDL_GetTicks();
        }
        uint64_t elapsed64 = max_frames == 0U
            ? now - previous_tick : HOST_SERVICE_INTERVAL_MS;
        previous_tick = now;
        if (elapsed64 == 0U) {
            elapsed64 = 1U;
        }
        if (elapsed64 > P4_GAME_MAX_FRAME_DELTA_MS) {
            elapsed64 = P4_GAME_MAX_FRAME_DELTA_MS;
        }

        p4_physical_touch_t touch[1];
        size_t touch_count = mouse_touch(
            renderer, surface_width, surface_height, touch);
        if (touch_count == 0U && pulsed_touch_valid) {
            touch[0] = pulsed_touch;
            touch_count = 1U;
        }
        p4_game_input_t input;
        p4_game_input_mapper_update(
            &mapper, true, touch, touch_count,
            digital_buttons() | pulsed_buttons, &input);
        const p4_game_result_t result = p4_game_instance_update(
            &instance, &input, (uint32_t)elapsed64);
        (void)p4_game_save_memory_process(
            &save_memory, P4_GAME_SAVE_MAX_SLOTS);
        if (result == P4_GAME_EXIT_TO_LAUNCHER) {
            running = false;
            continue;
        }
        if (result != P4_GAME_CONTINUE ||
            (audio_ready && !pump_audio(&audio))) {
            fprintf(stderr, "game update/audio failed: %s\n", SDL_GetError());
            running = false;
            continue;
        }
        if (service_updates != UINT32_MAX) {
            ++service_updates;
        }
        ++render_phase;
        if (render_phase < HOST_RENDER_DIVISOR) {
            continue;
        }
        render_phase = 0U;
        if (!p4_game_instance_render(&instance, &surface) ||
            !SDL_UpdateTexture(
                texture, NULL, pixels,
                (int)surface_width * (int)sizeof(*pixels)) ||
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
    if (achievements.count != 0U) {
        printf("Unlocked %zu achievement%s\n", achievements.count,
               achievements.count == 1U ? "" : "s");
    }
    if (audio_ready) {
        SDL_DestroyAudioStream(audio.stream);
    }
    printf("Stopped after %" PRIu32 " renders / %" PRIu32
           " service updates\n", rendered_frames, service_updates);
    free(state_memory);
    free(pixels);
    free(resource_data);
    free(save_workspace);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
