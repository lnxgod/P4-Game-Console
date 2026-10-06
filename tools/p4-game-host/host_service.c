// SPDX-License-Identifier: MIT
#include "host_service.h"

p4_game_result_t p4_host_service_update(
    p4_game_instance_t *instance,const p4_game_input_t *input,uint32_t elapsed_ms,
    p4_host_audio_pump_fn pump,void *audio_context)
{
    if(instance==NULL || input==NULL) return P4_GAME_ERROR;
    if(elapsed_ms>P4_GAME_MAX_FRAME_DELTA_MS) elapsed_ms=P4_GAME_MAX_FRAME_DELTA_MS;
    p4_game_input_t step_input=*input;
    while(elapsed_ms>0U) {
        const uint32_t step=elapsed_ms>P4_HOST_SERVICE_SLICE_MS?
            P4_HOST_SERVICE_SLICE_MS:elapsed_ms;
        const p4_game_result_t result=p4_game_instance_update(instance,&step_input,step);
        if(result!=P4_GAME_CONTINUE) return result;
        // Drain between substeps. A late frame must not fill the 512-frame
        // producer FIFO before the consumer gets an opportunity to run.
        if(pump!=NULL && !pump(audio_context,step)) return P4_GAME_ERROR;
        elapsed_ms-=step;
        // Held controls/touch continue; button edges belong to the first step.
        step_input.pressed=0U;step_input.released=0U;
    }
    return P4_GAME_CONTINUE;
}
