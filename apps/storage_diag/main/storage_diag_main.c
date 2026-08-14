#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "platform/storage.h"

static const char *const TAG = "p4_storage_diag";

void app_main(void)
{
    ESP_LOGI(TAG, "P4_STORAGE D1 START mode=read-only-api format_on_failure=false");

    esp_err_t result = platform_storage_init();
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4_STORAGE D1 FAIL stage=mount error=%s",
            esp_err_to_name(result)
        );
        return;
    }

    platform_storage_card_info_t card_info;
    result = platform_storage_get_card_info(&card_info);
    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4_STORAGE D1 FAIL stage=card-info error=%s",
            esp_err_to_name(result)
        );
        (void)platform_storage_deinit();
        return;
    }
    ESP_LOGI(
        TAG,
        "P4_STORAGE D1 MOUNTED path=%s capacity=%" PRIu64
        " sector=%" PRIu32 " width=%" PRIu32 " configured_khz=%" PRIu32
        " real_khz=%" PRIu32,
        PLATFORM_STORAGE_MOUNT_POINT,
        card_info.capacity_bytes,
        card_info.sector_size_bytes,
        card_info.configured_bus_width,
        card_info.configured_frequency_khz,
        card_info.real_frequency_khz
    );

    platform_storage_wad_info_t wad_info;
    result = platform_storage_find_doom_shareware(&wad_info);
    if (result == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(
            TAG,
            "P4_STORAGE D1 STORAGE_PASS wad=absent checked=%s,%s next=wad_provisioner",
            PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH,
            PLATFORM_STORAGE_FALLBACK_DOOM_WAD_PATH
        );
    } else if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4_STORAGE D1 FAIL stage=wad-validation error=%s path=%s",
            esp_err_to_name(result),
            wad_info.path[0] != '\0' ? wad_info.path : "unknown"
        );
    } else {
        ESP_LOGI(
            TAG,
            "P4_STORAGE D1 PASS wad=%s path=%s bytes=%" PRIu64
            " sha256=%s lumps=%" PRIu32 " directory_offset=%" PRIu32,
            platform_storage_wad_id_name(wad_info.identity),
            wad_info.path,
            wad_info.size_bytes,
            wad_info.sha256,
            wad_info.lump_count,
            wad_info.directory_offset
        );
    }

    const esp_err_t unmount_result = platform_storage_deinit();
    if (unmount_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "P4_STORAGE D1 FAIL stage=unmount error=%s",
            esp_err_to_name(unmount_result)
        );
    }
}
