// SPDX-License-Identifier: MIT

#include "platform/console_settings.h"

#include <stddef.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *const TAG = "p4_settings";
static const char *const SETTINGS_NAMESPACE = "p4_console";
static const char *const BOOT_VOLUME_KEY = "boot_volume";
static const char *const GAME_VOLUME_KEY = "game_volume";
static const char *const VOLUME_POLICY_KEY = "volume_policy";
static const char *const USB_ENUM_PROBE_KEY = "usb_enum_probe";

enum {
    VOLUME_POLICY_VERSION = 1,
};

bool platform_console_settings_volume_valid(uint8_t volume_step)
{
    return volume_step >= PLATFORM_CONSOLE_VOLUME_MIN &&
           volume_step <= PLATFORM_CONSOLE_VOLUME_MAX;
}

static void set_defaults(platform_console_settings_t *settings)
{
    *settings = (platform_console_settings_t) {
        .version = PLATFORM_CONSOLE_SETTINGS_VERSION,
        .size = (uint16_t)sizeof(*settings),
        .boot_volume_step = PLATFORM_CONSOLE_BOOT_VOLUME_DEFAULT,
        .game_volume_step = PLATFORM_CONSOLE_GAME_VOLUME_DEFAULT,
        .persistent = false,
    };
}

static esp_err_t migrate_volume_policy(nvs_handle_t handle,
                                       platform_console_settings_t *settings,
                                       bool *migrated)
{
    if (settings == NULL || migrated == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *migrated = false;
    uint8_t version = 0U;
    esp_err_t result = nvs_get_u8(handle, VOLUME_POLICY_KEY, &version);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;
    }
    if (result != ESP_OK || version >= VOLUME_POLICY_VERSION) {
        return result;
    }

    result = nvs_set_u8(
        handle, BOOT_VOLUME_KEY, PLATFORM_CONSOLE_BOOT_VOLUME_DEFAULT);
    if (result == ESP_OK) {
        result = nvs_set_u8(
            handle, GAME_VOLUME_KEY, PLATFORM_CONSOLE_GAME_VOLUME_DEFAULT);
    }
    if (result == ESP_OK) {
        result = nvs_set_u8(
            handle, VOLUME_POLICY_KEY, VOLUME_POLICY_VERSION);
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    if (result == ESP_OK) {
        settings->boot_volume_step = PLATFORM_CONSOLE_BOOT_VOLUME_DEFAULT;
        settings->game_volume_step = PLATFORM_CONSOLE_GAME_VOLUME_DEFAULT;
        *migrated = true;
    }
    return result;
}

esp_err_t platform_console_settings_init(
    platform_console_settings_t *settings)
{
    if (settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    set_defaults(settings);

    const esp_err_t init_result = nvs_flash_init();
    if (init_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_SETTINGS defaults=1 persistence=0 init=%s",
                 esp_err_to_name(init_result));
        return init_result;
    }

    nvs_handle_t handle;
    esp_err_t result = nvs_open(
        SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_SETTINGS defaults=1 persistence=0 open=%s",
                 esp_err_to_name(result));
        return result;
    }

    bool migrated = false;
    result = migrate_volume_policy(handle, settings, &migrated);
    if (result != ESP_OK) {
        nvs_close(handle);
        ESP_LOGW(TAG,
                 "P4_SETTINGS defaults=1 persistence=0 migration=%s",
                 esp_err_to_name(result));
        return result;
    }
    if (migrated) {
        nvs_close(handle);
        settings->persistent = true;
        ESP_LOGI(TAG,
                 "P4_SETTINGS boot=%u game=%u source=volume-policy-%u "
                 "persistent=1",
                 settings->boot_volume_step, settings->game_volume_step,
                 (unsigned)VOLUME_POLICY_VERSION);
        return ESP_OK;
    }

    uint8_t value = 0U;
    result = nvs_get_u8(handle, BOOT_VOLUME_KEY, &value);
    if (result == ESP_OK && platform_console_settings_volume_valid(value)) {
        settings->boot_volume_step = value;
    } else if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return result;
    }

    value = 0U;
    result = nvs_get_u8(handle, GAME_VOLUME_KEY, &value);
    if (result == ESP_OK && platform_console_settings_volume_valid(value)) {
        settings->game_volume_step = value;
    } else if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return result;
    }

    nvs_close(handle);
    settings->persistent = true;
    ESP_LOGI(TAG,
             "P4_SETTINGS boot=%u game=%u source=nvs persistent=1",
             settings->boot_volume_step, settings->game_volume_step);
    return ESP_OK;
}

static esp_err_t persist_volume(platform_console_settings_t *settings,
                                const char *key,
                                uint8_t volume_step,
                                bool boot)
{
    if (settings == NULL || key == NULL ||
        settings->version != PLATFORM_CONSOLE_SETTINGS_VERSION ||
        settings->size != (uint16_t)sizeof(*settings) ||
        !platform_console_settings_volume_valid(volume_step)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (boot) {
        settings->boot_volume_step = volume_step;
    } else {
        settings->game_volume_step = volume_step;
    }
    if (!settings->persistent) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t result = nvs_open(
        SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_u8(handle, key, volume_step);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

esp_err_t platform_console_settings_set_boot_volume(
    platform_console_settings_t *settings, uint8_t volume_step)
{
    return persist_volume(
        settings, BOOT_VOLUME_KEY, volume_step, true);
}

esp_err_t platform_console_settings_set_game_volume(
    platform_console_settings_t *settings, uint8_t volume_step)
{
    return persist_volume(
        settings, GAME_VOLUME_KEY, volume_step, false);
}

static bool settings_ready(const platform_console_settings_t *settings)
{
    return settings != NULL &&
           settings->version == PLATFORM_CONSOLE_SETTINGS_VERSION &&
           settings->size == (uint16_t)sizeof(*settings) &&
           settings->persistent;
}

esp_err_t platform_console_settings_begin_usb_enum_probe(
    const platform_console_settings_t *settings,
    bool *out_allowed,
    bool *out_prior_boot_incomplete)
{
    if (out_allowed == NULL || out_prior_boot_incomplete == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_allowed = false;
    *out_prior_boot_incomplete = false;
    if (!settings_ready(settings)) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t result = nvs_open(
        SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }

    uint8_t armed = 0U;
    result = nvs_get_u8(handle, USB_ENUM_PROBE_KEY, &armed);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;
        armed = 0U;
    }
    if (result == ESP_OK && armed > 1U) {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK && armed == 1U) {
        *out_prior_boot_incomplete = true;
    } else if (result == ESP_OK) {
        result = nvs_set_u8(handle, USB_ENUM_PROBE_KEY, 1U);
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
        *out_allowed = result == ESP_OK;
    }
    nvs_close(handle);
    return result;
}

esp_err_t platform_console_settings_confirm_usb_enum_probe(
    const platform_console_settings_t *settings)
{
    if (!settings_ready(settings)) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(
        SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_u8(handle, USB_ENUM_PROBE_KEY, 0U);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}
