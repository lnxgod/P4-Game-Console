#ifndef DOOM_VIDEO_DIMENSIONS_H
#define DOOM_VIDEO_DIMENSIONS_H

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

/* Touch coordinates remain canonical 320x200 input units. These dimensions
 * describe the actual engine raster, never a completed-frame scale factor. */
#ifndef DOOM_VIDEO_NATIVE_TAB5
#if defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5
#define DOOM_VIDEO_NATIVE_TAB5 1
#else
#define DOOM_VIDEO_NATIVE_TAB5 0
#endif
#endif
#if defined(CONFIG_P4_BOARD_M5STACK_TAB5) && CONFIG_P4_BOARD_M5STACK_TAB5 && !DOOM_VIDEO_NATIVE_TAB5
#error "Tab5 Doom requires a native 768x480 raster"
#endif
#if DOOM_VIDEO_NATIVE_TAB5
#define DOOM_VIDEO_WIDTH 768U
#define DOOM_VIDEO_HEIGHT 480U
#else
#define DOOM_VIDEO_WIDTH 320U
#define DOOM_VIDEO_HEIGHT 200U
#endif

#endif
