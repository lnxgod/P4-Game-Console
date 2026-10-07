// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "platform/touch.h"

/* Tab5 Console OS foreground-task hooks. No peripheral ownership is moved. */
void console_os_debug_poll(void);
uint32_t console_os_debug_buttons(void);
/* Replaces a physical frame only while injecting a touch or its release. */
bool console_os_debug_touch(platform_touch_frame_t *frame);
