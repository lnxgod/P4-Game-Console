/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SDL3 host adapter for the GPL quakegeneric engine. Quake game data remains
 * an ignored local input and is never part of this source file or executable.
 */

#include <SDL3/SDL.h>

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quakegeneric.h"
#include "quakedef.h"

#ifndef P4_QUAKE_DEFAULT_BASEDIR
#define P4_QUAKE_DEFAULT_BASEDIR "."
#endif

enum {
    P4_CONSOLE_WIDTH = 768,
    P4_CONSOLE_HEIGHT = 480,
    KEY_QUEUE_CAPACITY = 64,
    DMA_SAMPLE_COUNT = 32768,
    DMA_CHANNELS = 2,
    DMA_SAMPLE_RATE = 22050,
    MAX_HOST_ARGUMENTS = 64,
    MAX_SMOKE_FRAMES = 100000,
};

typedef struct {
    int key;
    bool down;
} queued_key_t;

typedef struct {
    bool headless;
    bool no_audio;
    uint32_t max_frames;
    const char *basedir;
    int quake_argc;
    char *quake_argv[MAX_HOST_ARGUMENTS];
} host_options_t;

static host_options_t s_options;
static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static SDL_Gamepad *s_gamepad;
static SDL_AudioStream *s_audio_stream;
static uint32_t *s_rgb_pixels;
static unsigned char s_palette[768];
static queued_key_t s_keys[KEY_QUEUE_CAPACITY];
static size_t s_key_head;
static size_t s_key_count;
static float s_joy_axes[QUAKEGENERIC_JOY_MAX_AXES];
static int s_mouse_x;
static int s_mouse_y;
static bool s_running = true;
static bool s_engine_called_quit;
static uint32_t s_frames;
static uint64_t s_last_frame_hash;

static dma_t s_dma;
static int16_t s_dma_samples[DMA_SAMPLE_COUNT];
static uint64_t s_submitted_audio_frames;

static uint64_t hash_frame(const uint32_t *pixels, size_t count)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t index = 0; index < count; ++index) {
        const uint32_t pixel = pixels[index];
        for (unsigned shift = 0; shift < 32U; shift += 8U) {
            hash ^= (pixel >> shift) & UINT32_C(0xff);
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static bool parse_u32(const char *text, uint32_t *value)
{
    if (text == NULL || value == NULL || *text == '\0') {
        return false;
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed == 0UL ||
        parsed > (unsigned long)MAX_SMOKE_FRAMES) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool append_quake_arg(host_options_t *options, char *argument)
{
    if (options->quake_argc >= MAX_HOST_ARGUMENTS) {
        return false;
    }
    options->quake_argv[options->quake_argc++] = argument;
    return true;
}

static void print_usage(const char *program)
{
    printf(
        "usage: %s [--basedir DIR] [--frames COUNT] [--headless] "
        "[--no-audio] [-- QUAKE_ARGS...]\n",
        program);
}

static bool parse_options(int argc, char **argv, host_options_t *options)
{
    if (argc < 1 || argv == NULL || options == NULL) {
        return false;
    }
    *options = (host_options_t){
        .basedir = P4_QUAKE_DEFAULT_BASEDIR,
        .quake_argc = 1,
        .quake_argv = {argv[0]},
    };
    bool passthrough = false;
    for (int index = 1; index < argc; ++index) {
        if (passthrough) {
            if (!append_quake_arg(options, argv[index])) {
                return false;
            }
            continue;
        }
        if (strcmp(argv[index], "--") == 0) {
            passthrough = true;
        } else if (strcmp(argv[index], "--headless") == 0) {
            options->headless = true;
        } else if (strcmp(argv[index], "--no-audio") == 0) {
            options->no_audio = true;
        } else if (strcmp(argv[index], "--frames") == 0) {
            if (++index >= argc || !parse_u32(argv[index], &options->max_frames)) {
                return false;
            }
        } else if (strcmp(argv[index], "--basedir") == 0) {
            if (++index >= argc || argv[index][0] == '\0') {
                return false;
            }
            options->basedir = argv[index];
        } else {
            return false;
        }
    }
    if (options->headless) {
        options->no_audio = true;
    }
    if (!append_quake_arg(options, "-basedir") ||
        !append_quake_arg(options, (char *)options->basedir)) {
        return false;
    }
    if (options->no_audio && !append_quake_arg(options, "-nosound")) {
        return false;
    }
    if (options->headless && !append_quake_arg(options, "-noudp")) {
        return false;
    }
    return true;
}

static bool push_key(bool down, int key)
{
    if (key <= 0 || s_key_count >= KEY_QUEUE_CAPACITY) {
        return false;
    }
    const size_t tail = (s_key_head + s_key_count) % KEY_QUEUE_CAPACITY;
    s_keys[tail] = (queued_key_t){.key = key, .down = down};
    ++s_key_count;
    return true;
}

static int quake_key(SDL_Keycode key)
{
    switch (key) {
        case SDLK_TAB: return K_TAB;
        case SDLK_RETURN: return K_ENTER;
        case SDLK_ESCAPE: return K_ESCAPE;
        case SDLK_SPACE: return K_SPACE;
        case SDLK_BACKSPACE: return K_BACKSPACE;
        case SDLK_UP: return K_UPARROW;
        case SDLK_DOWN: return K_DOWNARROW;
        case SDLK_LEFT: return K_LEFTARROW;
        case SDLK_RIGHT: return K_RIGHTARROW;
        case SDLK_LALT: case SDLK_RALT: return K_ALT;
        case SDLK_LCTRL: case SDLK_RCTRL: return K_CTRL;
        case SDLK_LSHIFT: case SDLK_RSHIFT: return K_SHIFT;
        case SDLK_F1: return K_F1;
        case SDLK_F2: return K_F2;
        case SDLK_F3: return K_F3;
        case SDLK_F4: return K_F4;
        case SDLK_F5: return K_F5;
        case SDLK_F6: return K_F6;
        case SDLK_F7: return K_F7;
        case SDLK_F8: return K_F8;
        case SDLK_F9: return K_F9;
        case SDLK_F10: return K_F10;
        case SDLK_F11: return K_F11;
        case SDLK_F12: return K_F12;
        case SDLK_INSERT: return K_INS;
        case SDLK_DELETE: return K_DEL;
        case SDLK_PAGEDOWN: return K_PGDN;
        case SDLK_PAGEUP: return K_PGUP;
        case SDLK_HOME: return K_HOME;
        case SDLK_END: return K_END;
        case SDLK_PAUSE: return K_PAUSE;
        default:
            if (key >= 32 && key <= 126) {
                return tolower((unsigned char)key);
            }
            return 0;
    }
}

static int quake_mouse_button(uint8_t button)
{
    switch (button) {
        case SDL_BUTTON_LEFT: return K_MOUSE1;
        case SDL_BUTTON_MIDDLE: return K_MOUSE3;
        case SDL_BUTTON_RIGHT: return K_MOUSE2;
        default: return 0;
    }
}

static int quake_gamepad_button(uint8_t button)
{
    switch ((SDL_GamepadButton)button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return K_CTRL;
        case SDL_GAMEPAD_BUTTON_EAST: return K_SPACE;
        case SDL_GAMEPAD_BUTTON_WEST: return K_ENTER;
        case SDL_GAMEPAD_BUTTON_NORTH: return K_AUX1;
        case SDL_GAMEPAD_BUTTON_START: return K_ESCAPE;
        case SDL_GAMEPAD_BUTTON_BACK: return K_TAB;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return K_UPARROW;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return K_DOWNARROW;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return K_LEFTARROW;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return K_RIGHTARROW;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return K_AUX2;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return K_AUX3;
        default: return 0;
    }
}

static void open_first_gamepad(void)
{
    if (s_gamepad != NULL) {
        return;
    }
    int count = 0;
    SDL_JoystickID *const gamepads = SDL_GetGamepads(&count);
    if (gamepads != NULL && count > 0) {
        s_gamepad = SDL_OpenGamepad(gamepads[0]);
    }
    SDL_free(gamepads);
}

static void pump_events(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                s_running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (!event.key.repeat) {
                    (void)push_key(
                        event.type == SDL_EVENT_KEY_DOWN,
                        quake_key(event.key.key));
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                s_mouse_x += (int)event.motion.xrel;
                s_mouse_y += (int)event.motion.yrel;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                (void)push_key(
                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                    quake_mouse_button(event.button.button));
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (event.wheel.y != 0.0F) {
                    const int key = event.wheel.y > 0.0F ? K_MWHEELUP : K_MWHEELDOWN;
                    (void)push_key(true, key);
                    (void)push_key(false, key);
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                open_first_gamepad();
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (s_gamepad != NULL &&
                    SDL_GetGamepadID(s_gamepad) == event.gdevice.which) {
                    SDL_CloseGamepad(s_gamepad);
                    s_gamepad = NULL;
                    memset(s_joy_axes, 0, sizeof(s_joy_axes));
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (event.gaxis.axis < QUAKEGENERIC_JOY_MAX_AXES) {
                    s_joy_axes[event.gaxis.axis] =
                        (float)event.gaxis.value / 32767.0F;
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                (void)push_key(
                    event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN,
                    quake_gamepad_button(event.gbutton.button));
                break;
            default:
                break;
        }
    }
}

static void destroy_platform(void)
{
    if (s_audio_stream != NULL) {
        SDL_DestroyAudioStream(s_audio_stream);
        s_audio_stream = NULL;
    }
    if (s_gamepad != NULL) {
        SDL_CloseGamepad(s_gamepad);
        s_gamepad = NULL;
    }
    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    s_texture = NULL;
    s_renderer = NULL;
    s_window = NULL;
    free(s_rgb_pixels);
    s_rgb_pixels = NULL;
    SDL_Quit();
}

void QG_Init(void)
{
    SDL_InitFlags flags = SDL_INIT_EVENTS | SDL_INIT_GAMEPAD;
    if (!s_options.headless) {
        flags |= SDL_INIT_VIDEO;
    }
    if (!s_options.no_audio) {
        flags |= SDL_INIT_AUDIO;
    }
    if (!SDL_Init(flags)) {
        fprintf(stderr, "P4_QUAKE SDL init failed: %s\n", SDL_GetError());
        s_running = false;
        return;
    }
    s_rgb_pixels = calloc(
        (size_t)QUAKEGENERIC_RES_X * QUAKEGENERIC_RES_Y,
        sizeof(*s_rgb_pixels));
    if (s_rgb_pixels == NULL) {
        fprintf(stderr, "P4_QUAKE frame allocation failed\n");
        s_running = false;
        return;
    }
    if (s_options.headless) {
        return;
    }
    s_window = SDL_CreateWindow(
        "P4 Console OS - Quake", P4_CONSOLE_WIDTH, P4_CONSOLE_HEIGHT,
        SDL_WINDOW_RESIZABLE);
    s_renderer = s_window != NULL ? SDL_CreateRenderer(s_window, NULL) : NULL;
    s_texture = s_renderer != NULL ? SDL_CreateTexture(
        s_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
        QUAKEGENERIC_RES_X, QUAKEGENERIC_RES_Y) : NULL;
    if (s_window == NULL || s_renderer == NULL || s_texture == NULL ||
        !SDL_SetWindowMinimumSize(s_window, P4_CONSOLE_WIDTH, P4_CONSOLE_HEIGHT) ||
        !SDL_SetRenderLogicalPresentation(
            s_renderer, QUAKEGENERIC_RES_X, QUAKEGENERIC_RES_Y,
            SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        fprintf(stderr, "P4_QUAKE video init failed: %s\n", SDL_GetError());
        s_running = false;
        return;
    }
    (void)SDL_SetRenderVSync(s_renderer, 1);
    (void)SDL_SetWindowRelativeMouseMode(s_window, true);
    open_first_gamepad();
}

void QG_Quit(void)
{
    s_engine_called_quit = true;
    s_running = false;
    destroy_platform();
}

void QG_DrawFrame(void *pixels)
{
    if (pixels == NULL || s_rgb_pixels == NULL) {
        s_running = false;
        return;
    }
    const uint8_t *const indexed = pixels;
    const size_t pixel_count =
        (size_t)QUAKEGENERIC_RES_X * QUAKEGENERIC_RES_Y;
    for (size_t index = 0; index < pixel_count; ++index) {
        const size_t palette_index = (size_t)indexed[index] * 3U;
        s_rgb_pixels[index] =
            ((uint32_t)s_palette[palette_index] << 16U) |
            ((uint32_t)s_palette[palette_index + 1U] << 8U) |
            (uint32_t)s_palette[palette_index + 2U];
    }
    s_last_frame_hash = hash_frame(s_rgb_pixels, pixel_count);
    ++s_frames;
    if (!s_options.headless && s_texture != NULL) {
        if (!SDL_UpdateTexture(
                s_texture, NULL, s_rgb_pixels,
                QUAKEGENERIC_RES_X * (int)sizeof(*s_rgb_pixels)) ||
            !SDL_RenderClear(s_renderer) ||
            !SDL_RenderTexture(s_renderer, s_texture, NULL, NULL) ||
            !SDL_RenderPresent(s_renderer)) {
            fprintf(stderr, "P4_QUAKE render failed: %s\n", SDL_GetError());
            s_running = false;
        }
    }
    if (s_options.max_frames > 0U && s_frames >= s_options.max_frames) {
        s_running = false;
    }
}

void QG_SetPalette(unsigned char palette[768])
{
    if (palette != NULL) {
        memcpy(s_palette, palette, sizeof(s_palette));
    }
}

int QG_GetKey(int *down, int *key)
{
    if (down == NULL || key == NULL || s_key_count == 0U) {
        return 0;
    }
    const queued_key_t event = s_keys[s_key_head];
    s_key_head = (s_key_head + 1U) % KEY_QUEUE_CAPACITY;
    --s_key_count;
    *down = event.down ? 1 : 0;
    *key = event.key;
    return 1;
}

void QG_GetMouseMove(int *x, int *y)
{
    if (x != NULL) {
        *x = s_mouse_x;
    }
    if (y != NULL) {
        *y = s_mouse_y;
    }
    s_mouse_x = 0;
    s_mouse_y = 0;
}

void QG_GetJoyAxes(float *axes)
{
    if (axes != NULL) {
        memcpy(axes, s_joy_axes, sizeof(s_joy_axes));
    }
}

qboolean SNDDMA_Init(void)
{
    if (s_options.no_audio || s_audio_stream != NULL) {
        return false;
    }
    const SDL_AudioSpec specification = {
        .format = SDL_AUDIO_S16,
        .channels = DMA_CHANNELS,
        .freq = DMA_SAMPLE_RATE,
    };
    s_audio_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &specification, NULL, NULL);
    if (s_audio_stream == NULL ||
        !SDL_SetAudioStreamGain(s_audio_stream, 0.8F) ||
        !SDL_ResumeAudioStreamDevice(s_audio_stream)) {
        fprintf(stderr, "P4_QUAKE audio disabled: %s\n", SDL_GetError());
        SDL_DestroyAudioStream(s_audio_stream);
        s_audio_stream = NULL;
        return false;
    }
    memset(s_dma_samples, 0, sizeof(s_dma_samples));
    s_dma = (dma_t){
        .gamealive = true,
        .soundalive = true,
        .splitbuffer = false,
        .channels = DMA_CHANNELS,
        .samples = DMA_SAMPLE_COUNT,
        .submission_chunk = 1,
        .samplepos = 0,
        .samplebits = 16,
        .speed = DMA_SAMPLE_RATE,
        .buffer = (unsigned char *)s_dma_samples,
    };
    s_submitted_audio_frames = 0U;
    shm = &s_dma;
    return true;
}

int SNDDMA_GetDMAPos(void)
{
    if (s_audio_stream == NULL || shm == NULL) {
        return 0;
    }
    const int queued_bytes = SDL_GetAudioStreamQueued(s_audio_stream);
    const uint64_t queued_frames = queued_bytes > 0
        ? (uint64_t)queued_bytes / (sizeof(int16_t) * DMA_CHANNELS)
        : 0U;
    const uint64_t played_frames = s_submitted_audio_frames > queued_frames
        ? s_submitted_audio_frames - queued_frames
        : 0U;
    const uint64_t ring_frames = DMA_SAMPLE_COUNT / DMA_CHANNELS;
    const int position = (int)((played_frames % ring_frames) * DMA_CHANNELS);
    s_dma.samplepos = position;
    return position;
}

void SNDDMA_Submit(void)
{
    if (s_audio_stream == NULL || shm == NULL || paintedtime < 0) {
        return;
    }
    uint64_t target = (uint64_t)(unsigned int)paintedtime;
    const uint64_t ring_frames = DMA_SAMPLE_COUNT / DMA_CHANNELS;
    if (target < s_submitted_audio_frames) {
        s_submitted_audio_frames = target;
    }
    if (target - s_submitted_audio_frames > ring_frames) {
        s_submitted_audio_frames = target - ring_frames;
    }
    while (s_submitted_audio_frames < target) {
        const uint64_t offset = s_submitted_audio_frames % ring_frames;
        uint64_t count = target - s_submitted_audio_frames;
        if (count > ring_frames - offset) {
            count = ring_frames - offset;
        }
        const size_t byte_count =
            (size_t)count * DMA_CHANNELS * sizeof(int16_t);
        if (!SDL_PutAudioStreamData(
                s_audio_stream,
                &s_dma_samples[(size_t)offset * DMA_CHANNELS],
                (int)byte_count)) {
            fprintf(stderr, "P4_QUAKE audio queue failed: %s\n", SDL_GetError());
            break;
        }
        s_submitted_audio_frames += count;
    }
}

void SNDDMA_Shutdown(void)
{
    if (s_audio_stream != NULL) {
        SDL_DestroyAudioStream(s_audio_stream);
        s_audio_stream = NULL;
    }
    memset(&s_dma, 0, sizeof(s_dma));
    s_submitted_audio_frames = 0U;
    shm = NULL;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    if (!parse_options(argc, argv, &s_options)) {
        print_usage(argv[0]);
        return 2;
    }

    QG_Create(s_options.quake_argc, s_options.quake_argv);
    uint64_t previous = SDL_GetTicksNS();
    uint64_t ticks = 0U;
    const uint64_t tick_limit = s_options.max_frames > 0U
        ? (uint64_t)s_options.max_frames * 16U + 240U
        : UINT64_MAX;
    while (s_running && ticks++ < tick_limit) {
        if (!s_options.headless) {
            pump_events();
        }
        double delta = 1.0 / 30.0;
        if (!s_options.headless) {
            const uint64_t now = SDL_GetTicksNS();
            delta = (double)(now - previous) / 1000000000.0;
            if (delta < 1.0 / 240.0) {
                SDL_Delay(1);
                continue;
            }
            previous = now;
            if (delta > 0.1) {
                delta = 0.1;
            }
        }
        QG_Tick(delta);
    }

    const bool smoke_pass = s_options.max_frames == 0U ||
        s_frames >= s_options.max_frames;
    if (!s_engine_called_quit) {
        Host_Shutdown();
        destroy_platform();
    }
    if (!smoke_pass) {
        fprintf(stderr, "P4_QUAKE_HOST FAIL frames=%" PRIu32 "\n", s_frames);
        return 1;
    }
    printf(
        "P4_QUAKE_HOST PASS frames=%" PRIu32 " frame_hash=%016" PRIx64
        " console=%dx%d engine=%dx%d audio=%s\n",
        s_frames, s_last_frame_hash, P4_CONSOLE_WIDTH, P4_CONSOLE_HEIGHT,
        QUAKEGENERIC_RES_X, QUAKEGENERIC_RES_Y,
        s_options.no_audio ? "disabled" : "sdl3");
    return 0;
}
