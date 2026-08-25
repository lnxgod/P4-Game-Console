// SPDX-License-Identifier: MIT

#include "platform/gamepad.h"

#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "freertos/FreeRTOS.h"
#pragma GCC diagnostic pop

enum {
    PLATFORM_GAMEPAD_PROVIDER_COUNT = 2,
};

typedef struct {
    platform_gamepad_transport_t transport;
    platform_gamepad_snapshot_provider_t provider;
} platform_gamepad_provider_entry_t;

static portMUX_TYPE s_provider_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_gamepad_provider_entry_t
    s_providers[PLATFORM_GAMEPAD_PROVIDER_COUNT];

static size_t provider_index(platform_gamepad_transport_t transport)
{
    switch (transport) {
    case PLATFORM_GAMEPAD_TRANSPORT_USB_HID:
        return 0U;
    case PLATFORM_GAMEPAD_TRANSPORT_BLE_HID:
        return 1U;
    case PLATFORM_GAMEPAD_TRANSPORT_NONE:
    default:
        return PLATFORM_GAMEPAD_PROVIDER_COUNT;
    }
}

static bool snapshot_valid(
    const platform_gamepad_snapshot_t *snapshot,
    platform_gamepad_transport_t transport)
{
    return snapshot != NULL &&
        snapshot->version == PLATFORM_GAMEPAD_SNAPSHOT_VERSION &&
        snapshot->size == sizeof(*snapshot) &&
        snapshot->state.version == GAMEPAD_STATE_VERSION &&
        snapshot->state.size == sizeof(snapshot->state) &&
        snapshot->state.connected <= 1U &&
        snapshot->identity.transport == (uint8_t)transport;
}

esp_err_t platform_gamepad_register_provider(
    platform_gamepad_transport_t transport,
    platform_gamepad_snapshot_provider_t provider)
{
    const size_t index = provider_index(transport);
    if (index >= PLATFORM_GAMEPAD_PROVIDER_COUNT || provider == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_provider_lock);
    s_providers[index] = (platform_gamepad_provider_entry_t){
        .transport = transport,
        .provider = provider,
    };
    portEXIT_CRITICAL(&s_provider_lock);
    return ESP_OK;
}

void platform_gamepad_unregister_provider(
    platform_gamepad_transport_t transport,
    platform_gamepad_snapshot_provider_t provider)
{
    const size_t index = provider_index(transport);
    if (index >= PLATFORM_GAMEPAD_PROVIDER_COUNT || provider == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_provider_lock);
    if (s_providers[index].provider == provider) {
        s_providers[index] = (platform_gamepad_provider_entry_t){0};
    }
    portEXIT_CRITICAL(&s_provider_lock);
}

esp_err_t platform_gamepad_get_snapshot(
    platform_gamepad_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(snapshot, 0, sizeof(*snapshot));

    platform_gamepad_provider_entry_t providers[
        PLATFORM_GAMEPAD_PROVIDER_COUNT];
    portENTER_CRITICAL(&s_provider_lock);
    memcpy(providers, s_providers, sizeof(providers));
    portEXIT_CRITICAL(&s_provider_lock);

    bool neutral_available = false;
    platform_gamepad_snapshot_t neutral = {0};
    esp_err_t last_error = ESP_ERR_INVALID_STATE;
    for (size_t index = 0U;
         index < PLATFORM_GAMEPAD_PROVIDER_COUNT; ++index) {
        if (providers[index].provider == NULL) {
            continue;
        }
        platform_gamepad_snapshot_t candidate;
        memset(&candidate, 0, sizeof(candidate));
        const esp_err_t result = providers[index].provider(&candidate);
        if (result != ESP_OK) {
            last_error = result;
            continue;
        }
        if (!snapshot_valid(&candidate, providers[index].transport)) {
            last_error = ESP_ERR_INVALID_RESPONSE;
            continue;
        }
        if (candidate.state.connected != 0U) {
            *snapshot = candidate;
            return ESP_OK;
        }
        if (!neutral_available) {
            neutral = candidate;
            neutral_available = true;
        }
    }
    if (neutral_available) {
        *snapshot = neutral;
        return ESP_OK;
    }
    return last_error;
}
