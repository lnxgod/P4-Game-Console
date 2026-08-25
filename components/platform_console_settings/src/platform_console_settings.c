// SPDX-License-Identifier: MIT

#include "platform/console_settings.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *const TAG = "p4_settings";
static const char *const SETTINGS_NAMESPACE = "p4_console";
static const char *const BOOT_VOLUME_KEY = "boot_volume";
static const char *const GAME_VOLUME_KEY = "game_volume";
static const char *const NODE_NAME_KEY = "node_name";
static const char *const BLE_CONTROLLER_ENABLED_KEY = "ble_pad_mode";
static const char *const CONTROLLER_MAPPING_KEY = "pad_map";
static const char *const VOLUME_POLICY_KEY = "volume_policy";
static const char *const USB_ENUM_PROBE_KEY = "usb_enum_probe";

enum {
    /* Apply the quieter room-friendly baseline once, then preserve UI edits. */
    VOLUME_POLICY_VERSION = 2,
};

static bool settings_ready(const platform_console_settings_t *settings);

bool platform_console_settings_volume_valid(uint8_t volume_step)
{
    return volume_step >= PLATFORM_CONSOLE_VOLUME_MIN &&
           volume_step <= PLATFORM_CONSOLE_VOLUME_MAX;
}

bool platform_console_settings_node_name_valid(const char *node_name)
{
    if (node_name == NULL) {
        return false;
    }
    const size_t length = strnlen(
        node_name, PLATFORM_CONSOLE_NODE_NAME_BYTES);
    if (length < 3U || length >= PLATFORM_CONSOLE_NODE_NAME_BYTES ||
        node_name[0] == '-' || node_name[length - 1U] == '-') {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        const char byte = node_name[index];
        if (!((byte >= 'A' && byte <= 'Z') ||
              (byte >= '0' && byte <= '9') || byte == '-')) {
            return false;
        }
    }
    return true;
}

static void set_default_node_name(platform_console_settings_t *settings)
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_BASE) == ESP_OK) {
        (void)snprintf(
            settings->node_name, sizeof(settings->node_name),
            "GC-P4-%02X%02X", mac[4], mac[5]);
    } else {
        memcpy(settings->node_name, "GC-P4-LOCAL", 12U);
    }
}

static void set_defaults(platform_console_settings_t *settings)
{
    *settings = (platform_console_settings_t) {
        .version = PLATFORM_CONSOLE_SETTINGS_VERSION,
        .size = (uint16_t)sizeof(*settings),
        .boot_volume_step = PLATFORM_CONSOLE_BOOT_VOLUME_DEFAULT,
        .game_volume_step = PLATFORM_CONSOLE_GAME_VOLUME_DEFAULT,
        .persistent = false,
        .ble_controller_enabled = true,
    };
    gamepad_button_mapping_default(&settings->controller_mapping);
    set_default_node_name(settings);
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

    char node_name[PLATFORM_CONSOLE_NODE_NAME_BYTES] = {0};
    size_t node_name_bytes = sizeof(node_name);
    result = nvs_get_str(
        handle, NODE_NAME_KEY, node_name, &node_name_bytes);
    if (result == ESP_OK &&
        platform_console_settings_node_name_valid(node_name)) {
        memcpy(settings->node_name, node_name, sizeof(node_name));
    } else if (result == ESP_ERR_NVS_NOT_FOUND || result == ESP_OK) {
        result = nvs_set_str(handle, NODE_NAME_KEY, settings->node_name);
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
    }
    if (result != ESP_OK) {
        nvs_close(handle);
        return result;
    }

    bool controller_settings_dirty = false;
    value = 0U;
    result = nvs_get_u8(handle, BLE_CONTROLLER_ENABLED_KEY, &value);
    if (result == ESP_OK && value <= 1U) {
        settings->ble_controller_enabled = value != 0U;
    } else if (result == ESP_ERR_NVS_NOT_FOUND || result == ESP_OK) {
        result = nvs_set_u8(
            handle, BLE_CONTROLLER_ENABLED_KEY,
            settings->ble_controller_enabled ? 1U : 0U);
        controller_settings_dirty = result == ESP_OK;
    }
    if (result != ESP_OK) {
        nvs_close(handle);
        return result;
    }

    gamepad_button_mapping_t mapping;
    memset(&mapping, 0, sizeof(mapping));
    size_t mapping_bytes = sizeof(mapping);
    result = nvs_get_blob(
        handle, CONTROLLER_MAPPING_KEY, &mapping, &mapping_bytes);
    if (result == ESP_OK && mapping_bytes == sizeof(mapping) &&
        gamepad_button_mapping_valid(&mapping)) {
        settings->controller_mapping = mapping;
    } else if (result == ESP_ERR_NVS_NOT_FOUND ||
               result == ESP_ERR_NVS_INVALID_LENGTH || result == ESP_OK) {
        result = nvs_set_blob(
            handle, CONTROLLER_MAPPING_KEY, &settings->controller_mapping,
            sizeof(settings->controller_mapping));
        controller_settings_dirty =
            controller_settings_dirty || result == ESP_OK;
    }
    if (result == ESP_OK && controller_settings_dirty) {
        result = nvs_commit(handle);
    }
    if (result != ESP_OK) {
        nvs_close(handle);
        return result;
    }

    nvs_close(handle);
    settings->persistent = true;
    ESP_LOGI(TAG,
             "P4_SETTINGS boot=%u game=%u node=%s ble_pad=%u "
             "source=%s persistent=1",
             settings->boot_volume_step, settings->game_volume_step,
             settings->node_name,
             settings->ble_controller_enabled ? 1U : 0U,
             migrated ? "volume-policy+nvs" : "nvs");
    return ESP_OK;
}

esp_err_t platform_console_settings_set_node_name(
    platform_console_settings_t *settings, const char *node_name)
{
    if (settings == NULL ||
        settings->version != PLATFORM_CONSOLE_SETTINGS_VERSION ||
        settings->size != (uint16_t)sizeof(*settings) ||
        !platform_console_settings_node_name_valid(node_name)) {
        return ESP_ERR_INVALID_ARG;
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
    result = nvs_set_str(handle, NODE_NAME_KEY, node_name);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result == ESP_OK) {
        (void)snprintf(settings->node_name,
                       sizeof(settings->node_name), "%s", node_name);
    }
    return result;
}

esp_err_t platform_console_settings_set_ble_controller_enabled(
    platform_console_settings_t *settings, bool enabled)
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
    result = nvs_set_u8(
        handle, BLE_CONTROLLER_ENABLED_KEY, enabled ? 1U : 0U);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result == ESP_OK) {
        settings->ble_controller_enabled = enabled;
    }
    return result;
}

esp_err_t platform_console_settings_set_controller_mapping(
    platform_console_settings_t *settings,
    const gamepad_button_mapping_t *mapping)
{
    if (!settings_ready(settings) ||
        !gamepad_button_mapping_valid(mapping)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(
        SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_blob(
        handle, CONTROLLER_MAPPING_KEY, mapping, sizeof(*mapping));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result == ESP_OK) {
        settings->controller_mapping = *mapping;
    }
    return result;
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
