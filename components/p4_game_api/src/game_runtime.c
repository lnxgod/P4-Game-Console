// SPDX-License-Identifier: MIT

#include "p4/game.h"

#include <limits.h>
#include <string.h>

static size_t bounded_length(const char *text, size_t limit)
{
    if (text == NULL) {
        return limit;
    }
    size_t length = 0U;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static uint32_t known_capabilities(void)
{
    return P4_GAME_CAP_VIDEO |
        P4_GAME_CAP_CONTROLS |
        P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_AUDIO_STREAM |
        P4_GAME_CAP_STORAGE |
        P4_GAME_CAP_SIGNAL_SCAN |
        P4_GAME_CAP_SAVE |
        P4_GAME_CAP_TEXT_INPUT |
        P4_GAME_CAP_REALM |
        P4_GAME_CAP_MULTIPLAYER_SESSION |
        P4_GAME_CAP_MODULE_HANDOFF |
        P4_GAME_CAP_VECTOR_SCENES |
        P4_GAME_CAP_VIDEO_HIGH_RES |
        P4_GAME_CAP_DICE_ACCESSORY | P4_GAME_CAP_MOTION;
}

static uint32_t implemented_service_capabilities(void)
{
    return P4_GAME_CAP_VIDEO |
        P4_GAME_CAP_CONTROLS |
        P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_AUDIO_STREAM |
        P4_GAME_CAP_STORAGE |
        P4_GAME_CAP_SIGNAL_SCAN |
        P4_GAME_CAP_SAVE |
        P4_GAME_CAP_MULTIPLAYER_SESSION |
        P4_GAME_CAP_VIDEO_HIGH_RES |
        P4_GAME_CAP_DICE_ACCESSORY | P4_GAME_CAP_MOTION;
}

static bool save_snapshot_valid(const p4_game_services_t *services)
{
    if (services->save_bytes == 0U) {
        return services->save_data == NULL &&
            services->save_schema_version == 0U;
    }
    return services->save_data != NULL &&
        services->save_bytes <= P4_GAME_SAVE_MAX_BYTES &&
        services->save_schema_version != 0U &&
        services->save_sequence != 0U;
}

bool p4_game_descriptor_valid(const p4_game_descriptor_t *descriptor)
{
    if (descriptor == NULL ||
        descriptor->api_version != P4_GAME_API_VERSION ||
        descriptor->launcher_id == 0U ||
        bounded_length(descriptor->id, P4_GAME_ID_MAX_BYTES) == 0U ||
        bounded_length(descriptor->id, P4_GAME_ID_MAX_BYTES) >=
            P4_GAME_ID_MAX_BYTES ||
        bounded_length(descriptor->title, P4_GAME_TITLE_MAX_BYTES) == 0U ||
        bounded_length(descriptor->title, P4_GAME_TITLE_MAX_BYTES) >=
            P4_GAME_TITLE_MAX_BYTES ||
        bounded_length(descriptor->subtitle, P4_GAME_SUBTITLE_MAX_BYTES) >=
            P4_GAME_SUBTITLE_MAX_BYTES ||
        descriptor->state_bytes == 0U ||
        descriptor->state_bytes > P4_GAME_MAX_STATE_BYTES ||
        descriptor->update == NULL || descriptor->render == NULL) {
        return false;
    }
    const uint32_t all = descriptor->required_capabilities |
        descriptor->optional_capabilities;
    return (all & ~known_capabilities()) == 0U &&
        (descriptor->required_capabilities &
         descriptor->optional_capabilities) == 0U &&
        (descriptor->required_capabilities & P4_GAME_CAP_VIDEO) != 0U;
}

static bool services_valid(const p4_game_services_t *services)
{
    if (services == NULL ||
        (services->available_capabilities &
         ~implemented_service_capabilities()) != 0U) {
        return false;
    }
    if ((services->available_capabilities & P4_GAME_CAP_AUDIO_TONE) != 0U &&
        services->play_tone == NULL) {
        return false;
    }
    if ((services->available_capabilities & P4_GAME_CAP_AUDIO_STREAM) != 0U &&
        services->submit_pcm16_stereo == NULL) {
        return false;
    }
    if ((services->available_capabilities & P4_GAME_CAP_STORAGE) != 0U &&
        (services->resource_data == NULL || services->resource_bytes == 0U ||
         services->resource_format_version == 0U)) {
        return false;
    }
    if ((services->available_capabilities & P4_GAME_CAP_SIGNAL_SCAN) != 0U &&
        (services->signal_scan_context == NULL ||
         services->request_signal_scan == NULL ||
         services->read_signal_scan == NULL)) {
        return false;
    }
    if ((services->available_capabilities & P4_GAME_CAP_SAVE) != 0U &&
        (services->save_context == NULL || services->queue_save == NULL ||
         services->read_save_status == NULL ||
         !save_snapshot_valid(services))) {
        return false;
    }
    if ((services->available_capabilities &
         P4_GAME_CAP_MULTIPLAYER_SESSION) != 0U &&
        (services->multiplayer_context == NULL ||
         services->multiplayer_read_status == NULL ||
         services->multiplayer_send == NULL ||
         services->multiplayer_receive == NULL)) {
        return false;
    }
    if ((services->available_capabilities & P4_GAME_CAP_DICE_ACCESSORY) != 0U &&
        (services->dice_context == NULL || services->dice_exchange == NULL)) {
        return false;
    }
    if ((services->available_capabilities & P4_GAME_CAP_MOTION) != 0U &&
        services->read_motion == NULL) return false;
    if (services->multiplayer_profile != NULL &&
        (((services->available_capabilities &
           P4_GAME_CAP_MULTIPLAYER_SESSION) == 0U) ||
         !p4_game_multiplayer_profile_valid(
             services->multiplayer_profile))) {
        return false;
    }
    return true;
}

bool p4_game_instance_start(p4_game_instance_t *instance,
                            const p4_game_descriptor_t *descriptor,
                            const p4_game_services_t *services,
                            void *state_memory,
                            size_t state_memory_bytes)
{
    if (instance == NULL || instance->active ||
        !p4_game_descriptor_valid(descriptor) ||
        !services_valid(services) || state_memory == NULL ||
        state_memory_bytes < descriptor->state_bytes ||
        (descriptor->required_capabilities &
         ~services->available_capabilities) != 0U) {
        return false;
    }
    memset(instance, 0, sizeof(*instance));
    memset(state_memory, 0, descriptor->state_bytes);
    instance->descriptor = descriptor;
    instance->services = *services;
    instance->context = (p4_game_context_t){
        .state = state_memory,
        .state_bytes = descriptor->state_bytes,
        .services = &instance->services,
        .elapsed_ms = 0U,
        .frame_index = 0U,
    };
    instance->active = true;
    if (descriptor->start != NULL &&
        !descriptor->start(&instance->context)) {
        if (descriptor->stop != NULL) {
            descriptor->stop(&instance->context);
        }
        memset(instance, 0, sizeof(*instance));
        return false;
    }
    return true;
}

static bool input_valid(const p4_game_input_t *input)
{
    if (input == NULL || input->touch_count > 5U ||
        (input->held & ~P4_BUTTON_MASK) != 0U ||
        (input->pressed & ~P4_BUTTON_MASK) != 0U ||
        (input->released & ~P4_BUTTON_MASK) != 0U ||
        (!input->touch_valid && input->touch_count != 0U)) {
        return false;
    }
    for (size_t i = 0U; i < input->touch_count; ++i) {
        if (input->touches[i].x >= P4_GAME_SURFACE_WIDTH ||
            input->touches[i].y >= P4_GAME_SURFACE_HEIGHT) {
            return false;
        }
    }
    return true;
}

p4_game_result_t p4_game_instance_update(p4_game_instance_t *instance,
                                         const p4_game_input_t *input,
                                         uint32_t elapsed_ms)
{
    if (instance == NULL || !instance->active ||
        instance->descriptor == NULL || !input_valid(input)) {
        return P4_GAME_ERROR;
    }
    const uint32_t bounded_ms = elapsed_ms > P4_GAME_MAX_FRAME_DELTA_MS
        ? P4_GAME_MAX_FRAME_DELTA_MS : elapsed_ms;
    if (UINT64_MAX - instance->context.elapsed_ms < bounded_ms) {
        instance->context.elapsed_ms = UINT64_MAX;
    } else {
        instance->context.elapsed_ms += bounded_ms;
    }
    if (instance->context.frame_index != UINT32_MAX) {
        ++instance->context.frame_index;
    }
    const p4_game_result_t result = instance->descriptor->update(
        &instance->context, input, bounded_ms);
    if (result < P4_GAME_CONTINUE || result > P4_GAME_ERROR) {
        return P4_GAME_ERROR;
    }
    return result;
}

bool p4_game_instance_render(p4_game_instance_t *instance,
                             p4_game_surface_t *surface)
{
    if (instance == NULL || !instance->active ||
        instance->descriptor == NULL || surface == NULL ||
        surface->pixels == NULL) {
        return false;
    }
    const bool high_res =
        ((instance->descriptor->required_capabilities |
          instance->descriptor->optional_capabilities) &
         P4_GAME_CAP_VIDEO_HIGH_RES) != 0U &&
        (instance->services.available_capabilities &
         P4_GAME_CAP_VIDEO_HIGH_RES) != 0U;
    const uint16_t expected_width = high_res
        ? P4_GAME_SURFACE_HIGH_RES_WIDTH : P4_GAME_SURFACE_WIDTH;
    const uint16_t expected_height = high_res
        ? P4_GAME_SURFACE_HIGH_RES_HEIGHT : P4_GAME_SURFACE_HEIGHT;
    return surface->width == expected_width &&
        surface->height == expected_height &&
        surface->stride_pixels >= expected_width &&
        instance->descriptor->render(&instance->context, surface);
}

void p4_game_instance_stop(p4_game_instance_t *instance)
{
    if (instance == NULL || !instance->active ||
        instance->descriptor == NULL) {
        return;
    }
    if (instance->descriptor->stop != NULL) {
        instance->descriptor->stop(&instance->context);
    }
    p4_game_stop_audio(&instance->context);
    instance->active = false;
}

bool p4_game_play_tone(p4_game_context_t *context,
                       uint16_t frequency_hz,
                       uint16_t duration_ms,
                       uint8_t volume_step,
                       p4_waveform_t waveform)
{
    if (context == NULL || context->services == NULL ||
        (context->services->available_capabilities &
         P4_GAME_CAP_AUDIO_TONE) == 0U ||
        context->services->play_tone == NULL ||
        frequency_hz < 40U || frequency_hz > 4000U ||
        duration_ms == 0U || duration_ms > 5000U ||
        volume_step == 0U || volume_step > 10U ||
        waveform < P4_WAVE_SQUARE || waveform > P4_WAVE_TRIANGLE) {
        return false;
    }
    const p4_tone_t tone = {
        .frequency_hz = frequency_hz,
        .duration_ms = duration_ms,
        .volume_step = volume_step,
        .waveform = waveform,
    };
    return context->services->play_tone(
        context->services->audio_context, &tone);
}

bool p4_game_submit_pcm16_stereo(p4_game_context_t *context,
                                 const int16_t *interleaved_stereo,
                                 size_t frame_count)
{
    return context != NULL && context->services != NULL &&
        (context->services->available_capabilities &
         P4_GAME_CAP_AUDIO_STREAM) != 0U &&
        context->services->submit_pcm16_stereo != NULL &&
        interleaved_stereo != NULL && frame_count > 0U &&
        frame_count <= P4_GAME_MAX_AUDIO_STREAM_FRAMES &&
        context->services->submit_pcm16_stereo(
            context->services->audio_context,
            interleaved_stereo, frame_count);
}

bool p4_game_unlock_achievement(p4_game_context_t *context,
                                const char *achievement_id,
                                const char *title,
                                const char *description)
{
    if (context == NULL || context->services == NULL ||
        context->services->unlock_achievement == NULL ||
        context->services->achievement_context == NULL ||
        bounded_length(achievement_id, P4_GAME_ACHIEVEMENT_ID_MAX_BYTES) == 0U ||
        bounded_length(achievement_id, P4_GAME_ACHIEVEMENT_ID_MAX_BYTES) >=
            P4_GAME_ACHIEVEMENT_ID_MAX_BYTES ||
        bounded_length(title, P4_GAME_ACHIEVEMENT_TITLE_MAX_BYTES) == 0U ||
        bounded_length(title, P4_GAME_ACHIEVEMENT_TITLE_MAX_BYTES) >=
            P4_GAME_ACHIEVEMENT_TITLE_MAX_BYTES ||
        bounded_length(description,
                       P4_GAME_ACHIEVEMENT_DESCRIPTION_MAX_BYTES) == 0U ||
        bounded_length(description,
                       P4_GAME_ACHIEVEMENT_DESCRIPTION_MAX_BYTES) >=
            P4_GAME_ACHIEVEMENT_DESCRIPTION_MAX_BYTES) {
        return false;
    }
    const p4_game_achievement_t achievement = {
        .game_id = context->services->game_id,
        .id = achievement_id,
        .title = title,
        .description = description,
        .unlocked_at_elapsed_ms = context->elapsed_ms,
    };
    return bounded_length(achievement.game_id, P4_GAME_ID_MAX_BYTES) > 0U &&
        bounded_length(achievement.game_id, P4_GAME_ID_MAX_BYTES) <
            P4_GAME_ID_MAX_BYTES &&
        context->services->unlock_achievement(
            context->services->achievement_context, &achievement);
}

bool p4_game_request_signal_scan(p4_game_context_t *context,
                                 uint64_t focus_token)
{
    return context != NULL && context->services != NULL &&
        (context->services->available_capabilities &
         P4_GAME_CAP_SIGNAL_SCAN) != 0U &&
        context->services->request_signal_scan != NULL &&
        context->services->request_signal_scan(
            context->services->signal_scan_context, focus_token);
}

static bool signal_snapshot_valid(const p4_game_signal_snapshot_t *snapshot)
{
    if (snapshot == NULL || snapshot->status < P4_GAME_SIGNAL_IDLE ||
        snapshot->status > P4_GAME_SIGNAL_ERROR ||
        snapshot->count > P4_GAME_SIGNAL_MAX_RESULTS ||
        (snapshot->status != P4_GAME_SIGNAL_READY && snapshot->count != 0U)) {
        return false;
    }
    for (size_t index = 0U; index < snapshot->count; ++index) {
        const p4_game_signal_t *const signal = &snapshot->results[index];
        if (signal->token == 0U || signal->rssi_dbm > 0 ||
            signal->channel > 196U ||
            (signal->flags & ~(P4_GAME_SIGNAL_HIDDEN |
                               P4_GAME_SIGNAL_PROTECTED |
                               P4_GAME_SIGNAL_SIMULATED)) != 0U ||
            bounded_length(signal->label,
                           P4_GAME_SIGNAL_LABEL_MAX_BYTES) == 0U ||
            bounded_length(signal->label,
                           P4_GAME_SIGNAL_LABEL_MAX_BYTES) >=
                P4_GAME_SIGNAL_LABEL_MAX_BYTES) {
            return false;
        }
    }
    return true;
}

bool p4_game_read_signal_scan(p4_game_context_t *context,
                              p4_game_signal_snapshot_t *snapshot)
{
    if (context == NULL || context->services == NULL || snapshot == NULL ||
        (context->services->available_capabilities &
         P4_GAME_CAP_SIGNAL_SCAN) == 0U ||
        context->services->read_signal_scan == NULL) {
        return false;
    }
    p4_game_signal_snapshot_t candidate = {0};
    if (!context->services->read_signal_scan(
            context->services->signal_scan_context, &candidate) ||
        !signal_snapshot_valid(&candidate)) {
        return false;
    }
    *snapshot = candidate;
    return true;
}

static bool save_slot_id_valid(const char *slot_id)
{
    const size_t length = bounded_length(slot_id,
                                         P4_GAME_SAVE_SLOT_ID_BYTES);
    if (length == 0U || length >= P4_GAME_SAVE_SLOT_ID_BYTES) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char character = (unsigned char)slot_id[index];
        const bool alpha_numeric =
            (character >= (unsigned char)'A' &&
             character <= (unsigned char)'Z') ||
            (character >= (unsigned char)'a' &&
             character <= (unsigned char)'z') ||
            (character >= (unsigned char)'0' &&
             character <= (unsigned char)'9');
        if (!alpha_numeric &&
            (index == 0U ||
             (character != (unsigned char)'_' &&
              character != (unsigned char)'-'))) {
            return false;
        }
    }
    return true;
}

bool p4_game_queue_save(p4_game_context_t *context,
                        const char *slot_id,
                        uint32_t schema_version,
                        uint32_t expected_sequence,
                        const uint8_t *data,
                        size_t data_bytes,
                        p4_game_save_ticket_t *ticket_out)
{
    if (ticket_out != NULL) {
        *ticket_out = P4_GAME_SAVE_INVALID_TICKET;
    }
    if (context == NULL || context->services == NULL || ticket_out == NULL ||
        (context->services->available_capabilities & P4_GAME_CAP_SAVE) == 0U ||
        context->services->save_context == NULL ||
        context->services->queue_save == NULL ||
        !save_slot_id_valid(slot_id) || schema_version == 0U || data == NULL ||
        data_bytes == 0U || data_bytes > P4_GAME_SAVE_MAX_BYTES) {
        return false;
    }
    p4_game_save_ticket_t candidate = P4_GAME_SAVE_INVALID_TICKET;
    if (!context->services->queue_save(
            context->services->save_context, slot_id, schema_version,
            expected_sequence, data, data_bytes, &candidate) ||
        candidate == P4_GAME_SAVE_INVALID_TICKET) {
        return false;
    }
    *ticket_out = candidate;
    return true;
}

bool p4_game_read_save_status(p4_game_context_t *context,
                              p4_game_save_ticket_t ticket,
                              p4_game_save_status_t *status_out,
                              uint32_t *committed_sequence_out)
{
    if (status_out != NULL) {
        *status_out = P4_GAME_SAVE_NONE;
    }
    if (committed_sequence_out != NULL) {
        *committed_sequence_out = 0U;
    }
    if (context == NULL || context->services == NULL || status_out == NULL ||
        committed_sequence_out == NULL ||
        ticket == P4_GAME_SAVE_INVALID_TICKET ||
        (context->services->available_capabilities & P4_GAME_CAP_SAVE) == 0U ||
        context->services->save_context == NULL ||
        context->services->read_save_status == NULL) {
        return false;
    }
    p4_game_save_status_t candidate_status = P4_GAME_SAVE_NONE;
    uint32_t candidate_sequence = 0U;
    if (!context->services->read_save_status(
            context->services->save_context, ticket, &candidate_status,
            &candidate_sequence) || candidate_status < P4_GAME_SAVE_NONE ||
        candidate_status > P4_GAME_SAVE_ERROR ||
        (candidate_status == P4_GAME_SAVE_COMMITTED &&
         candidate_sequence == 0U)) {
        return false;
    }
    *status_out = candidate_status;
    *committed_sequence_out = candidate_sequence;
    return true;
}

static bool multiplayer_status_valid(
    const p4_game_multiplayer_status_t *status)
{
    if (status == NULL ||
        status->state < P4_GAME_MULTIPLAYER_OFFLINE ||
        status->state > P4_GAME_MULTIPLAYER_ERROR ||
        status->role < P4_GAME_MULTIPLAYER_ROLE_NONE ||
        status->role > P4_GAME_MULTIPLAYER_ROLE_CLIENT ||
        status->player_count > P4_GAME_MULTIPLAYER_MAX_PLAYERS ||
        status->local_player_slot >= P4_GAME_MULTIPLAYER_MAX_PLAYERS) {
        return false;
    }
    if (status->state == P4_GAME_MULTIPLAYER_OFFLINE) {
        return status->role == P4_GAME_MULTIPLAYER_ROLE_NONE &&
            status->player_count == 0U && status->session_seed == 0U;
    }
    if (status->role == P4_GAME_MULTIPLAYER_ROLE_NONE ||
        status->player_count == 0U || status->session_seed == 0U ||
        status->local_player_slot >= status->player_count) {
        return false;
    }
    return status->state != P4_GAME_MULTIPLAYER_CONNECTED ||
        status->player_count >= 2U;
}

bool p4_game_multiplayer_profile_valid(
    const p4_game_multiplayer_profile_t *profile)
{
    return profile != NULL &&
        profile->schema == P4_GAME_MULTIPLAYER_PROFILE_SCHEMA &&
        profile->style >= P4_GAME_MULTIPLAYER_STYLE_TURN_BASED &&
        profile->style <= P4_GAME_MULTIPLAYER_STYLE_LOCKSTEP &&
        profile->min_players >= 2U &&
        profile->min_players <= profile->max_players &&
        profile->max_players <= P4_GAME_MULTIPLAYER_MAX_PLAYERS &&
        profile->tick_rate_hz > 0U &&
        profile->tick_rate_hz <= P4_GAME_MULTIPLAYER_MAX_TICK_RATE_HZ &&
        profile->input_delay_ticks <=
            P4_GAME_MULTIPLAYER_MAX_INPUT_DELAY_TICKS &&
        (profile->style == P4_GAME_MULTIPLAYER_STYLE_LOCKSTEP ||
         profile->input_delay_ticks == 0U) &&
        profile->message_bytes > 0U &&
        profile->message_bytes <= P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES &&
        profile->protocol != 0U && profile->flags == 0U;
}

bool p4_game_multiplayer_profile_default(
    p4_game_multiplayer_style_t style,
    p4_game_multiplayer_profile_t *profile_out)
{
    if (profile_out == NULL ||
        style < P4_GAME_MULTIPLAYER_STYLE_TURN_BASED ||
        style > P4_GAME_MULTIPLAYER_STYLE_LOCKSTEP) {
        return false;
    }
    *profile_out = (p4_game_multiplayer_profile_t){
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = style,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = style == P4_GAME_MULTIPLAYER_STYLE_TURN_BASED
            ? 10U : (style == P4_GAME_MULTIPLAYER_STYLE_REALTIME
                ? 30U : 60U),
        .input_delay_ticks =
            style == P4_GAME_MULTIPLAYER_STYLE_LOCKSTEP ? 2U : 0U,
        .message_bytes = P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
        .protocol = 1U,
    };
    return true;
}

bool p4_game_multiplayer_read_profile(
    p4_game_context_t *context,
    p4_game_multiplayer_profile_t *profile_out)
{
    if (profile_out != NULL) {
        *profile_out = (p4_game_multiplayer_profile_t){0};
    }
    if (context == NULL || context->services == NULL ||
        profile_out == NULL ||
        (context->services->available_capabilities &
         P4_GAME_CAP_MULTIPLAYER_SESSION) == 0U) {
        return false;
    }
    if (p4_game_multiplayer_profile_valid(
            context->services->multiplayer_profile)) {
        *profile_out = *context->services->multiplayer_profile;
        return true;
    }
    return p4_game_multiplayer_profile_default(
        P4_GAME_MULTIPLAYER_STYLE_REALTIME, profile_out);
}

static size_t multiplayer_message_limit(const p4_game_context_t *context)
{
    if (context != NULL && context->services != NULL &&
        p4_game_multiplayer_profile_valid(
            context->services->multiplayer_profile)) {
        return context->services->multiplayer_profile->message_bytes;
    }
    return P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES;
}

bool p4_game_multiplayer_read_status(
    p4_game_context_t *context,
    p4_game_multiplayer_status_t *status_out)
{
    if (status_out != NULL) {
        *status_out = (p4_game_multiplayer_status_t){0};
    }
    if (context == NULL || context->services == NULL || status_out == NULL ||
        (context->services->available_capabilities &
         P4_GAME_CAP_MULTIPLAYER_SESSION) == 0U ||
        context->services->multiplayer_context == NULL ||
        context->services->multiplayer_read_status == NULL ||
        !context->services->multiplayer_read_status(
            context->services->multiplayer_context, status_out) ||
        !multiplayer_status_valid(status_out)) {
        if (status_out != NULL) {
            *status_out = (p4_game_multiplayer_status_t){0};
        }
        return false;
    }
    return true;
}

bool p4_game_multiplayer_send(
    p4_game_context_t *context,
    const uint8_t *data,
    size_t data_bytes)
{
    return context != NULL && context->services != NULL && data != NULL &&
        data_bytes != 0U &&
        data_bytes <= multiplayer_message_limit(context) &&
        (context->services->available_capabilities &
         P4_GAME_CAP_MULTIPLAYER_SESSION) != 0U &&
        context->services->multiplayer_context != NULL &&
        context->services->multiplayer_send != NULL &&
        context->services->multiplayer_send(
            context->services->multiplayer_context, data, data_bytes);
}

bool p4_game_multiplayer_receive(
    p4_game_context_t *context,
    p4_game_multiplayer_message_t *message_out)
{
    if (message_out != NULL) {
        *message_out = (p4_game_multiplayer_message_t){0};
    }
    if (context == NULL || context->services == NULL || message_out == NULL ||
        (context->services->available_capabilities &
         P4_GAME_CAP_MULTIPLAYER_SESSION) == 0U ||
        context->services->multiplayer_context == NULL ||
        context->services->multiplayer_receive == NULL ||
        !context->services->multiplayer_receive(
            context->services->multiplayer_context, message_out) ||
        message_out->sequence == 0U ||
        message_out->player_slot >= P4_GAME_MULTIPLAYER_MAX_PLAYERS ||
        message_out->bytes == 0U ||
        message_out->bytes > multiplayer_message_limit(context)) {
        if (message_out != NULL) {
            *message_out = (p4_game_multiplayer_message_t){0};
        }
        return false;
    }
    return true;
}

void p4_game_stop_audio(p4_game_context_t *context)
{
    if (context != NULL && context->services != NULL &&
        context->services->stop_audio != NULL) {
        context->services->stop_audio(context->services->audio_context);
    }
}

bool p4_game_dice_exchange(p4_game_context_t *context,
    const p4_dice_request_t *request, p4_dice_status_t *status)
{
    if (status) *status = (p4_dice_status_t){0};
    if (!context || !context->services || !request || !status ||
        !(context->services->available_capabilities & P4_GAME_CAP_DICE_ACCESSORY) ||
        !context->services->dice_exchange || !context->services->dice_context ||
        !request->token || request->player_slot >= 4 || !request->count ||
        request->count > P4_DICE_MAX || request->sides < 2 ||
        ((unsigned)request->held_mask >> request->count)) return false;
    size_t n=bounded_length(request->player_name,P4_DICE_NAME_BYTES);
    if (!n || n==P4_DICE_NAME_BYTES) return false;
    for (size_t i=0; i<n; ++i)
        if ((unsigned char)request->player_name[i]<32 ||
            (unsigned char)request->player_name[i]>126) return false;
    for (size_t i=0; i<P4_DICE_MAX; ++i)
        if (i<request->count ? (request->faces[i]<1 || request->faces[i]>request->sides)
                            : request->faces[i]!=0) return false;
    if (!context->services->dice_exchange(context->services->dice_context,request,status) ||
        status->phase < P4_DICE_OFFLINE || status->phase > P4_DICE_ROLLED ||
        (status->hold_changed && (!request->can_hold ||
         status->phase != P4_DICE_WAITING || status->hold_sequence == request->hold_ack ||
         ((unsigned)status->held_mask >> request->count))) ||
        (status->phase >= P4_DICE_WAITING &&
         (status->token != request->token || status->player_slot != request->player_slot))) {
        *status=(p4_dice_status_t){0}; return false;
    }
    return true;
}

bool p4_game_read_motion(p4_game_context_t *context, p4_game_motion_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    if (context == NULL || context->services == NULL ||
        !(context->services->available_capabilities & P4_GAME_CAP_MOTION) ||
        context->services->read_motion == NULL) return false;
    p4_game_motion_t sample = {0};
    if (!context->services->read_motion(context->services->motion_context, &sample) ||
        !sample.valid || sample.age_ms > 150U || sample.sequence == 0U) return false;
    for (unsigned i = 0; i < 3U; ++i) {
        if (sample.accel_mg[i] < -4000 || sample.accel_mg[i] > 4000 ||
            sample.gyro_mdps[i] < -2000000 || sample.gyro_mdps[i] > 2000000) return false;
    }
    *out = sample;
    return true;
}
