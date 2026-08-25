#include "gamepad/snapshot.h"

#include <stdbool.h>
#include <string.h>

#define PLATFORM_GAMEPAD_KNOWN_CAPABILITIES                              \
    ((uint32_t)(GAMEPAD_CAP_BUTTONS | GAMEPAD_CAP_DPAD |                 \
                GAMEPAD_CAP_LEFT_STICK | GAMEPAD_CAP_RIGHT_STICK |       \
                GAMEPAD_CAP_LEFT_TRIGGER | GAMEPAD_CAP_RIGHT_TRIGGER))

static bool model_valid(const platform_gamepad_model_t *model)
{
    return model != NULL && model->initialized == 1U &&
           model->snapshot.version == PLATFORM_GAMEPAD_SNAPSHOT_VERSION &&
           model->snapshot.size == sizeof(model->snapshot) &&
           model->snapshot.state.version == GAMEPAD_STATE_VERSION &&
           model->snapshot.state.size == sizeof(model->snapshot.state) &&
           model->snapshot.state.connected <= 1U;
}

static bool identity_valid(const platform_gamepad_identity_t *identity)
{
    if (identity == NULL ||
        (identity->transport != PLATFORM_GAMEPAD_TRANSPORT_USB_HID &&
         identity->transport != PLATFORM_GAMEPAD_TRANSPORT_BLE_HID)) {
        return false;
    }

    uint8_t hash_or = 0U;
    for (size_t index = 0U;
         index < PLATFORM_GAMEPAD_DESCRIPTOR_SHA256_BYTES; ++index) {
        hash_or |= identity->descriptor_sha256[index];
    }
    return hash_or != 0U;
}

void platform_gamepad_model_init(platform_gamepad_model_t *model)
{
    if (model == NULL) {
        return;
    }
    memset(model, 0, sizeof(*model));
    model->initialized = 1U;
    model->snapshot.version = PLATFORM_GAMEPAD_SNAPSHOT_VERSION;
    model->snapshot.size = (uint16_t)sizeof(model->snapshot);
    gamepad_state_init(&model->snapshot.state);
}

gamepad_status_t platform_gamepad_model_copy(
    const platform_gamepad_model_t *model,
    platform_gamepad_snapshot_t *snapshot)
{
    if (!model_valid(model) || snapshot == NULL) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }
    *snapshot = model->snapshot;
    return GAMEPAD_OK;
}

gamepad_status_t platform_gamepad_model_connect(
    platform_gamepad_model_t *model,
    const platform_gamepad_identity_t *identity,
    uint32_t capabilities,
    uint64_t timestamp_us,
    uint32_t *session)
{
    if (!model_valid(model) || !identity_valid(identity) || session == NULL ||
        capabilities == 0U ||
        (capabilities & ~PLATFORM_GAMEPAD_KNOWN_CAPABILITIES) != 0U) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }
    if (model->snapshot.state.connected != 0U) {
        return GAMEPAD_ERR_INVALID_STATE;
    }

    const uint32_t previous_sequence = model->snapshot.state.sequence;
    model->snapshot.session++;
    if (model->snapshot.session == 0U) {
        model->snapshot.session = 1U;
    }
    model->snapshot.capabilities = capabilities;
    model->snapshot.identity = *identity;
    gamepad_state_init(&model->snapshot.state);
    model->snapshot.state.sequence = previous_sequence;
    const gamepad_status_t status =
        gamepad_state_connect(&model->snapshot.state, timestamp_us);
    if (status != GAMEPAD_OK) {
        return status;
    }
    *session = model->snapshot.session;
    return GAMEPAD_OK;
}

gamepad_status_t platform_gamepad_model_disconnect(
    platform_gamepad_model_t *model,
    uint32_t expected_session,
    uint64_t timestamp_us)
{
    if (!model_valid(model) || expected_session == 0U) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }
    if (model->snapshot.session != expected_session ||
        model->snapshot.state.connected == 0U) {
        return GAMEPAD_ERR_DISCONNECTED;
    }
    return gamepad_state_disconnect(&model->snapshot.state, timestamp_us);
}

gamepad_status_t platform_gamepad_model_commit_report(
    platform_gamepad_model_t *model,
    uint32_t expected_session,
    const gamepad_state_t *decoded_state)
{
    if (!model_valid(model) || decoded_state == NULL ||
        decoded_state->version != GAMEPAD_STATE_VERSION ||
        decoded_state->size != sizeof(*decoded_state) ||
        decoded_state->connected != 1U ||
        decoded_state->dpad >
            (GAMEPAD_DPAD_UP | GAMEPAD_DPAD_RIGHT | GAMEPAD_DPAD_DOWN |
             GAMEPAD_DPAD_LEFT)) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }
    if (expected_session == 0U || model->snapshot.session != expected_session ||
        model->snapshot.state.connected == 0U) {
        return GAMEPAD_ERR_DISCONNECTED;
    }

    const uint32_t expected_sequence = model->snapshot.state.sequence + 1U;
    if (decoded_state->sequence != expected_sequence ||
        decoded_state->timestamp_us < model->snapshot.state.timestamp_us) {
        return GAMEPAD_ERR_INVALID_STATE;
    }
    model->snapshot.state = *decoded_state;
    return GAMEPAD_OK;
}
