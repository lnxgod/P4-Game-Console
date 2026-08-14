// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2026 ESP32-P4 badge platform contributors
 *
 * Build-only ESP-IDF seam for the GPL-covered doomgeneric application.
 * This deliberately does not start the engine: no WAD or hardware service is
 * connected at D0.5, and a successful link is not an on-device runtime claim.
 */

#include <stdbool.h>
#include <stdint.h>

#include "doomgeneric.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define P4_DOOM_MAX_SLEEP_MS UINT32_C(60000)

static const char *TAG = "p4_doom_probe";

/*
 * Keep a relocation to the engine entry point. doom_engine is linked as a
 * whole archive as well, so every selected upstream object participates in
 * the final ESP32-P4 link even though this build-only image never starts Doom.
 */
static void (*volatile const p4_doom_engine_anchor)(int, char **) =
    doomgeneric_Create;

void DG_Init(void)
{
    ESP_LOGI(TAG, "Doom platform seam initialized without hardware services");
}

void DG_DrawFrame(void)
{
    /* Future D1 code will submit the surface to the reusable video service. */
}

void DG_SleepMs(uint32_t milliseconds)
{
    const uint32_t bounded = milliseconds > P4_DOOM_MAX_SLEEP_MS
                                 ? P4_DOOM_MAX_SLEEP_MS
                                 : milliseconds;
    TickType_t ticks;

    if (bounded == 0) {
        taskYIELD();
        return;
    }

    ticks = pdMS_TO_TICKS(bounded);
    vTaskDelay(ticks > 0 ? ticks : 1);
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)((uint64_t)esp_timer_get_time() / UINT64_C(1000));
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (pressed != NULL) {
        *pressed = 0;
    }
    if (key != NULL) {
        *key = 0;
    }
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

void app_main(void)
{
    const bool engine_linked = p4_doom_engine_anchor != NULL;

    ESP_LOGI(TAG,
             "P4_DOOM_D05 BUILD_ONLY engine_linked=%s wad_embedded=false "
             "hardware_services=none runtime_supported=false",
             engine_linked ? "true" : "false");
    ESP_LOGW(TAG,
             "Doom execution is intentionally disabled until storage and "
             "video services pass D1 hardware gates");
}
