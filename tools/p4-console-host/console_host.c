// SPDX-License-Identifier: MIT

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console/shell.h"

enum {
    HOST_WINDOW_WIDTH = CONSOLE_SHELL_WIDTH,
    HOST_WINDOW_HEIGHT = CONSOLE_SHELL_HEIGHT,
    HOST_TICK_MS = 16,
};

static const console_app_descriptor_t apps[] = {
#if defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
    {110U,"Byte Buddy","Platform adventure","GAMES/ARCADE",0,0,CONSOLE_PAGE_EXTERNAL,true,NULL,NULL,NULL},
    {111U,"Blast Circuit","Bomb arena","GAMES/ARCADE",0,0,CONSOLE_PAGE_EXTERNAL,true,NULL,NULL,NULL},
#endif
    {1U, "DOOM", "SHAREWARE 1.9", "GAMES/ACTION", UINT16_C(0xF904),
     CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH |
         CONSOLE_CAPABILITY_AUDIO,
     CONSOLE_PAGE_EXTERNAL, true, NULL, NULL, NULL},
    {100U, "MAZE CHASE", "ORIGINAL GAME", "GAMES/ARCADE",
     UINT16_C(0xFFE0), CONSOLE_CAPABILITY_DISPLAY |
         CONSOLE_CAPABILITY_TOUCH | CONSOLE_CAPABILITY_AUDIO,
     CONSOLE_PAGE_EXTERNAL, true, NULL, NULL, NULL},
    {101U, "SPACE INVADERS", "DEFEND THE P4", "GAMES/ARCADE",
     UINT16_C(0x07FF), CONSOLE_CAPABILITY_DISPLAY |
         CONSOLE_CAPABILITY_TOUCH | CONSOLE_CAPABILITY_AUDIO,
     CONSOLE_PAGE_EXTERNAL, true, NULL, NULL, NULL},
    {107U, "SOLITAIRE", "ORIGINAL KLONDIKE", "GAMES/PUZZLE",
     UINT16_C(0x07E0), CONSOLE_CAPABILITY_DISPLAY |
         CONSOLE_CAPABILITY_TOUCH | CONSOLE_CAPABILITY_AUDIO,
     CONSOLE_PAGE_EXTERNAL, true, NULL, NULL, NULL},
    {2U, "COLORS", "DISPLAY TEST", "SYSTEM", UINT16_C(0x5FFF),
     CONSOLE_CAPABILITY_DISPLAY, CONSOLE_PAGE_COLORS, true, NULL, NULL, NULL},
    {3U, "TOUCH", "POINTER CONTACTS", "SYSTEM", UINT16_C(0xFFE0),
     CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH,
     CONSOLE_PAGE_TOUCH, true, NULL, NULL, NULL},
    {4U, "SYSTEM", "HOST STATUS", "SYSTEM", UINT16_C(0x5FEA),
     CONSOLE_CAPABILITY_DISPLAY | CONSOLE_CAPABILITY_TOUCH,
     CONSOLE_PAGE_SYSTEM, true, NULL, NULL, NULL},
    {5U, "AUDIO", "MASTER VOLUME", "SYSTEM", UINT16_C(0xF81F),
     CONSOLE_CAPABILITY_AUDIO, CONSOLE_PAGE_AUDIO, true, NULL, NULL, NULL},
    {6U, "GAME MANAGER", "BUILTINS + CARTS", "SYSTEM", UINT16_C(0x07FF),
     CONSOLE_CAPABILITY_STORAGE, CONSOLE_PAGE_LIBRARY, true, NULL, NULL, NULL},
    {7U, "MULTIPLAYER", "LOCAL LOBBY", "SYSTEM", UINT16_C(0xFFE0),
     0U, CONSOLE_PAGE_MULTIPLAYER, true, NULL, NULL, NULL},
    {9U, "FILE MANAGER", "LIST / SORT / COPY", "SYSTEM", UINT16_C(0x07FF),
     CONSOLE_CAPABILITY_STORAGE, CONSOLE_PAGE_FILES, true, NULL, NULL, NULL},
    {10U, "SAVE MANAGER", "GAME SAVE SLOTS", "SYSTEM", UINT16_C(0xFFE0),
     CONSOLE_CAPABILITY_STORAGE, CONSOLE_PAGE_SAVES, true, NULL, NULL, NULL},
    {11U, "TERMINAL", "COMMANDS + SSH", "SYSTEM", UINT16_C(0x5FEA),
     CONSOLE_CAPABILITY_TOUCH, CONSOLE_PAGE_TERMINAL, true, NULL, NULL, NULL},
};

static bool parse_frames(int argc, char **argv, uint32_t *frames)
{
    *frames = 0U;
    if (argc == 1) {
        return true;
    }
    if (argc != 3 || strcmp(argv[1], "--frames") != 0) {
        return false;
    }
    char *end = NULL;
    const unsigned long value = strtoul(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || value == 0UL || value > 10000UL) {
        return false;
    }
    *frames = (uint32_t)value;
    return true;
}

static bool point_from_window(SDL_Renderer *renderer,
                              float window_x, float window_y,
                              console_shell_contact_t *point)
{
    float logical_x = 0.0F;
    float logical_y = 0.0F;
    if (point == NULL || !SDL_RenderCoordinatesFromWindow(
            renderer, window_x, window_y, &logical_x, &logical_y) ||
        logical_x < 0.0F || logical_y < 0.0F ||
        logical_x >= (float)CONSOLE_SHELL_WIDTH ||
        logical_y >= (float)CONSOLE_SHELL_HEIGHT) {
        return false;
    }
#if defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
    point->x=(uint16_t)logical_x;point->y=(uint16_t)logical_y;
#else
    point->x = (uint16_t)(CONSOLE_SHELL_VIEWPORT_LEFT +
        ((uint32_t)logical_x * CONSOLE_SHELL_VIEWPORT_WIDTH) /
            CONSOLE_SHELL_WIDTH);
    point->y = (uint16_t)(CONSOLE_SHELL_VIEWPORT_TOP +
        ((uint32_t)logical_y * CONSOLE_SHELL_VIEWPORT_HEIGHT) /
            CONSOLE_SHELL_HEIGHT);
#endif
    return true;
}

static void populate_desktop(console_shell_t *shell)
{
    const console_shell_runtime_info_t runtime = {
        .uptime_seconds = 1U,
        .internal_free_kib = 512U,
        .psram_free_kib = 32768U,
        .game_storage_kib = 32768U,
        .game_storage_state = CONSOLE_STORAGE_READY,
        .board_kind = CONSOLE_BOARD_HOST_PREVIEW,
        .touch_ready = true,
        .audio_handoff_ready = true,
        .content_scan_complete = true,
        .usb_content_ready = false,
        .valid_cart_count = 3U,
        .builtin_game_count = 10U,
        .multiplayer_core_ready = true,
        .physical_keyboard_ready = true,
        .keyboard_ready = true,
    };
    console_shell_set_runtime_info(shell, &runtime);
    p4_file_list_t files;
    p4_file_list_init(&files);
    (void)p4_file_list_add(&files, "GAMES", 0U,
                           P4_FILE_KIND_FOLDER, true);
    (void)p4_file_list_add(&files, "SAVES", 0U,
                           P4_FILE_KIND_FOLDER, true);
    (void)p4_file_list_add(&files, "MAZE.P4G", UINT64_C(18342),
                           P4_FILE_KIND_CARTRIDGE, true);
    (void)console_shell_set_file_list(shell, &files);
    p4_save_catalog_t saves;
    p4_save_catalog_init(&saves, false);
    (void)p4_save_catalog_add(
        &saves, "ORG.P4CONSOLE.SOLITAIRE", "AUTO", 768U, 1U);
    (void)console_shell_set_save_catalog(shell, &saves);
}

static void click_back(console_shell_t *shell)
{
    const console_shell_contact_t point = {
        .x = (uint16_t)(CONSOLE_SHELL_VIEWPORT_LEFT +
            10U * CONSOLE_SHELL_VIEWPORT_WIDTH / CONSOLE_SHELL_WIDTH),
        .y = (uint16_t)(CONSOLE_SHELL_VIEWPORT_TOP +
            10U * CONSOLE_SHELL_VIEWPORT_HEIGHT / CONSOLE_SHELL_HEIGHT),
    };
    (void)console_shell_handle_touch(shell, true, &point, 1U);
    (void)console_shell_handle_touch(shell, true, NULL, 0U);
}

int main(int argc, char **argv)
{
    uint32_t frame_limit = 0U;
    if (!parse_frames(argc, argv, &frame_limit)) {
        fprintf(stderr, "usage: %s [--frames 1..10000]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_HIGH_PIXEL_DENSITY |
        (frame_limit == 0U ? 0U : SDL_WINDOW_HIDDEN);
    SDL_Window *const window = SDL_CreateWindow(
        "P4 CONSOLE OS", HOST_WINDOW_WIDTH, HOST_WINDOW_HEIGHT, flags);
    SDL_Renderer *const renderer = window != NULL
        ? SDL_CreateRenderer(window, NULL) : NULL;
    if (renderer == NULL || !SDL_SetRenderLogicalPresentation(
            renderer, CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT,
            SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        fprintf(stderr, "SDL window failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    SDL_Texture *const texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        CONSOLE_SHELL_WIDTH, CONSOLE_SHELL_HEIGHT);
    uint16_t *const pixels = calloc(
        (size_t)CONSOLE_SHELL_WIDTH * CONSOLE_SHELL_HEIGHT, sizeof(*pixels));
    if (texture == NULL || pixels == NULL) {
        fprintf(stderr, "host allocation failed: %s\n", SDL_GetError());
        free(pixels);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    (void)SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    (void)SDL_StartTextInput(window);

    console_shell_t shell;
    if (!console_shell_init(&shell, apps, sizeof(apps) / sizeof(apps[0]))) {
        fputs("shell init failed\n", stderr);
        free(pixels);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    populate_desktop(&shell);
    bool running = true;
    uint32_t frames = 0U;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                       event.type == SDL_EVENT_MOUSE_MOTION) {
                const SDL_MouseButtonFlags buttons =
                    SDL_GetMouseState(NULL, NULL);
                if ((buttons & SDL_BUTTON_LMASK) != 0U) {
                    console_shell_contact_t point;
                    if (point_from_window(renderer, event.button.x,
                                          event.button.y, &point)) {
                        (void)console_shell_handle_touch(
                            &shell, true, &point, 1U);
                    }
                }
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                (void)console_shell_handle_touch(&shell, true, NULL, 0U);
            } else if (event.type == SDL_EVENT_TEXT_INPUT) {
                for (size_t index = 0U;
                     event.text.text[index] != '\0'; ++index) {
                    (void)console_shell_handle_text_key(
                        &shell, event.text.text[index]);
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN &&
                       !event.key.repeat) {
                if (event.key.key == SDLK_BACKSPACE) {
                    (void)console_shell_handle_text_key(&shell, '\b');
                } else if (event.key.key == SDLK_RETURN) {
                    (void)console_shell_handle_text_key(&shell, '\n');
                } else if (event.key.key == SDLK_ESCAPE) {
                    if (shell.page == CONSOLE_PAGE_HOME) {
                        running = false;
                    } else {
                        click_back(&shell);
                    }
                }
            }
        }
        if (console_shell_is_dirty(&shell)) {
            if (!console_shell_render_rgb565(
                    &shell, pixels, CONSOLE_SHELL_WIDTH) ||
                !SDL_UpdateTexture(texture, NULL, pixels,
                    CONSOLE_SHELL_WIDTH * (int)sizeof(*pixels))) {
                fprintf(stderr, "render failed: %s\n", SDL_GetError());
                running = false;
            }
        }
        if (!SDL_SetRenderDrawColor(renderer, 0U, 0U, 0U, 255U) ||
            !SDL_RenderClear(renderer) ||
            !SDL_RenderTexture(renderer, texture, NULL, NULL) ||
            !SDL_RenderPresent(renderer)) {
            fprintf(stderr, "present failed: %s\n", SDL_GetError());
            running = false;
        }
        ++frames;
        if (frame_limit != 0U && frames >= frame_limit) {
            running = false;
        } else if (frame_limit == 0U) {
            SDL_Delay(HOST_TICK_MS);
        }
    }
    SDL_StopTextInput(window);
    free(pixels);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    printf("Console OS host stopped after %u frames\n", (unsigned)frames);
    return EXIT_SUCCESS;
}
