// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef CONSOLE_DOOM_P4MP_ADAPTER_H
#define CONSOLE_DOOM_P4MP_ADAPTER_H

#include "esp_err.h"
#include "p4/doom_multiplayer.h"
#include "p4/multiplayer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Transfer the already-connected Console OS P4MP session into Doom. */
esp_err_t p4_doom_p4mp_prepare(
    p4_mp_session_t *session,
    const p4_doom_mp_launch_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
