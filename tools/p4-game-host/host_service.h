// SPDX-License-Identifier: MIT
#ifndef P4_HOST_SERVICE_H
#define P4_HOST_SERVICE_H
#include "p4/game.h"
// Consume exactly the audio time produced by each bounded game update.
enum { P4_HOST_SERVICE_SLICE_MS=16 };
typedef bool (*p4_host_audio_pump_fn)(void *context,uint32_t elapsed_ms);
p4_game_result_t p4_host_service_update(
    p4_game_instance_t *instance,const p4_game_input_t *input,uint32_t elapsed_ms,
    p4_host_audio_pump_fn pump,void *audio_context);
#endif
