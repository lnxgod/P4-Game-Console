// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Compile only pinned i_sound.c in its no-SDL mode. FEATURE_SOUND remains
 * enabled, so the engine binds DG_sound_module and DG_music_module from the
 * project-owned Doom audio component without pulling in SDL_mixer.
 */
#define __DJGPP__ 1
#include "i_sound.c"
