// SPDX-License-Identifier: MIT

#ifndef P4_CONSOLE_SHELL_H
#define P4_CONSOLE_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CONSOLE_SHELL_WIDTH = 320,
    CONSOLE_SHELL_HEIGHT = 200,
    CONSOLE_SHELL_PHYSICAL_WIDTH = 1024,
    CONSOLE_SHELL_PHYSICAL_HEIGHT = 600,
    CONSOLE_SHELL_VIEWPORT_LEFT = 32,
    CONSOLE_SHELL_VIEWPORT_SCALE = 3,
    CONSOLE_SHELL_APPS_PER_PAGE = 6,
    CONSOLE_SHELL_MAX_APPS = 32,
    CONSOLE_SHELL_MAX_CONTACTS = 5,
    CONSOLE_SHELL_TITLE_MAX_BYTES = 16,
    CONSOLE_SHELL_SUBTITLE_MAX_BYTES = 32,
};

typedef enum {
    CONSOLE_CAPABILITY_DISPLAY = UINT32_C(1) << 0U,
    CONSOLE_CAPABILITY_TOUCH = UINT32_C(1) << 1U,
    CONSOLE_CAPABILITY_AUDIO = UINT32_C(1) << 2U,
    CONSOLE_CAPABILITY_STORAGE = UINT32_C(1) << 3U,
} console_capability_t;

typedef enum {
    CONSOLE_PAGE_HOME = 0,
    CONSOLE_PAGE_EXTERNAL,
    CONSOLE_PAGE_COLORS,
    CONSOLE_PAGE_TOUCH,
    CONSOLE_PAGE_SYSTEM,
    CONSOLE_PAGE_AUDIO,
} console_page_t;

typedef struct {
    uint32_t id;
    const char *title;
    const char *subtitle;
    uint16_t accent_rgb565;
    uint32_t capabilities;
    console_page_t page;
    bool enabled;
} console_app_descriptor_t;

typedef struct {
    uint16_t x;
    uint16_t y;
} console_shell_contact_t;

typedef enum {
    CONSOLE_STORAGE_STARTING = 0,
    CONSOLE_STORAGE_READY,
    CONSOLE_STORAGE_USB_HOST,
    CONSOLE_STORAGE_FORMAT_REQUIRED,
    CONSOLE_STORAGE_MISSING,
    CONSOLE_STORAGE_INVALID,
    CONSOLE_STORAGE_LOCKED,
    CONSOLE_STORAGE_FAULT,
} console_shell_storage_state_t;

typedef struct {
    uint32_t uptime_seconds;
    uint32_t internal_free_kib;
    uint32_t psram_free_kib;
    uint32_t game_storage_kib;
    console_shell_storage_state_t game_storage_state;
    bool touch_ready;
    bool audio_handoff_ready;
    bool game_storage_usb_attached;
    bool doom_wad_ready;
} console_shell_runtime_info_t;

typedef enum {
    CONSOLE_ACTION_NONE = 0,
    CONSOLE_ACTION_PAGE_CHANGED,
    CONSOLE_ACTION_LAUNCH,
} console_action_type_t;

typedef struct {
    console_action_type_t type;
    uint32_t app_id;
} console_shell_action_t;

typedef struct {
    const console_app_descriptor_t *apps;
    size_t app_count;
    size_t selected_index;
    size_t home_page;
    size_t pressed_index;
    console_page_t page;
    uint32_t active_app_id;
    bool contact_down;
    bool press_active;
    bool dirty;
    uint32_t render_generation;
    console_shell_runtime_info_t runtime;
    console_shell_contact_t contacts[CONSOLE_SHELL_MAX_CONTACTS];
    size_t contact_count;
} console_shell_t;

/** Validate and initialize a static app registry. No memory is allocated. */
bool console_shell_init(console_shell_t *shell,
                        const console_app_descriptor_t *apps,
                        size_t app_count);

/**
 * Consume one complete physical 1024x600 touch snapshot.
 *
 * Invalid frames, more than one contact, out-of-viewport coordinates, and
 * contact movement between controls cancel the pending press. A launch or
 * page change is emitted only when one valid contact is released over the
 * control where it began.
 */
console_shell_action_t console_shell_handle_touch(
    console_shell_t *shell,
    bool valid,
    const console_shell_contact_t *contacts,
    size_t contact_count);

/** Update non-sensitive runtime and game-storage status. */
void console_shell_set_runtime_info(
    console_shell_t *shell,
    const console_shell_runtime_info_t *runtime);

/** Return to the launcher without synthesizing an app launch. */
void console_shell_show_home(console_shell_t *shell);

/** True when input or runtime state changed since the most recent render. */
bool console_shell_is_dirty(const console_shell_t *shell);

/**
 * Render one standard RGB565 320x200 frame into caller-owned memory.
 * `stride_pixels` must be at least 320. Rendering clears the dirty flag.
 */
bool console_shell_render_rgb565(console_shell_t *shell,
                                 uint16_t *pixels,
                                 size_t stride_pixels);

#ifdef __cplusplus
}
#endif

#endif
