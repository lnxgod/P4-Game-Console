// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio_lifecycle.h"

#include "doom/audio_runtime.h"

static esp_err_t first_error(const doom_e6_audio_release_result_t *result)
{
    if (result->stop_result != ESP_OK &&
        result->stop_result != ESP_ERR_INVALID_STATE) {
        return result->stop_result;
    }
    if (result->unbind_result != ESP_OK) {
        return result->unbind_result;
    }
    if (result->destroy_result != ESP_OK) {
        return result->destroy_result;
    }
    return result->recover_result;
}

esp_err_t doom_e6_audio_release(
    doom_e6_audio_lifecycle_t *lifecycle,
    doom_e6_audio_release_result_t *out_result)
{
    if (lifecycle == NULL || out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_result = (doom_e6_audio_release_result_t){
        .stop_result = ESP_OK,
        .unbind_result = ESP_OK,
        .destroy_result = ESP_OK,
        .recover_result = ESP_OK,
        .complete = false,
    };
    if (!lifecycle->hardware_touched) {
        lifecycle->released = true;
        out_result->complete = true;
        return ESP_OK;
    }

    bool worker_released = !lifecycle->runtime_bound;
    if (lifecycle->runtime_bound) {
        out_result->stop_result = doom_audio_runtime_stop();
        if (out_result->stop_result == ESP_OK ||
            out_result->stop_result == ESP_ERR_INVALID_STATE) {
            out_result->unbind_result = doom_audio_runtime_unbind();
            if (out_result->unbind_result == ESP_OK) {
                lifecycle->runtime_bound = false;
                worker_released = true;
            }
        }
    }

    if (worker_released && lifecycle->audio != NULL) {
        out_result->destroy_result = platform_audio_destroy(
            &lifecycle->audio);
    }
    if (worker_released && lifecycle->audio == NULL) {
        out_result->recover_result = platform_audio_recover();
        if (out_result->recover_result == ESP_OK) {
            lifecycle->safe_high_proven = true;
        }
    }
    lifecycle->released = worker_released && lifecycle->audio == NULL &&
        out_result->recover_result == ESP_OK &&
        lifecycle->safe_high_proven;
    out_result->complete = lifecycle->released;
    return lifecycle->released ? ESP_OK : first_error(out_result);
}
