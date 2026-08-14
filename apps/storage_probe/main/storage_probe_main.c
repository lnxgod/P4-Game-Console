#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "platform/storage_probe.h"

static const char *const TAG = "p4_storage_probe";

static bool filesystem_result_is_conclusive(int32_t result)
{
    return result == 0 || result == 13;
}

static void log_profile(
    size_t profile_index,
    esp_err_t result,
    const platform_storage_probe_report_t *report
)
{
    ESP_LOGI(
        TAG,
        "P4_STORAGE_PROBE D1 PROFILE_RESULT index=%u requested_khz=%" PRIu32
        " real_khz=%" PRId32 " result=%s host=%s slot=%s card_init=%s"
        " card_status=%s raw=%s cleanup=%s width=1 ddr=false",
        (unsigned)profile_index,
        report->requested_frequency_khz,
        report->real_frequency_khz,
        esp_err_to_name(result),
        esp_err_to_name(report->host_init_result),
        esp_err_to_name(report->slot_init_result),
        esp_err_to_name(report->card_init_result),
        esp_err_to_name(report->card_status_result),
        esp_err_to_name(report->raw_read_result),
        esp_err_to_name(report->cleanup_result)
    );
    if (report->card_identity_valid) {
        ESP_LOGI(
            TAG,
            "P4_STORAGE_PROBE D1 CARD index=%u identity_sha256=%s capacity=%" PRIu64
            " sector=%" PRIu32,
            (unsigned)profile_index,
            report->card_identity_sha256,
            report->capacity_bytes,
            report->sector_size_bytes
        );
    }
    if (report->raw_reads_stable) {
        ESP_LOGI(
            TAG,
            "P4_STORAGE_PROBE D1 RAW_PASS index=%u reads=%" PRIu32
            " mbr_signature=%s valid_partitions=%" PRIu32
            " first_partition_type=0x%02" PRIx32 " first_partition_lba=%" PRIu64,
            (unsigned)profile_index,
            report->raw_reads_completed,
            report->mbr_signature_present ? "true" : "false",
            report->valid_mbr_partition_count,
            report->first_partition_type,
            report->first_partition_lba
        );
    }
    if (report->filesystem_attempted) {
        ESP_LOGI(
            TAG,
            "P4_STORAGE_PROBE D1 FILESYSTEM_RESULT index=%u code=%" PRId32
            " name=%s disk_reads=%" PRIu32 " io_result=%s blocked_writes=%" PRIu32,
            (unsigned)profile_index,
            report->filesystem_result,
            platform_storage_probe_filesystem_result_name(report->filesystem_result),
            report->filesystem_reads_completed,
            esp_err_to_name(report->filesystem_io_result),
            report->filesystem_blocked_write_attempts
        );
    }
}

void app_main(void)
{
    static const uint32_t frequencies_khz[] = {10000U, 5000U, 1000U};
    ESP_LOGW(
        TAG,
        "P4_STORAGE_PROBE D1 START mode=read-only-no-format-no-write"
        " profiles_khz=10000,5000,1000 width=1 ddr=false"
    );

    char expected_identity[PLATFORM_STORAGE_PROBE_ID_SHA256_HEX_LENGTH + 1U] = {0};
    bool identity_known = false;
    bool raw_profile_observed = false;
    int32_t last_filesystem_result = PLATFORM_STORAGE_PROBE_FS_NOT_RUN;
    uint32_t selected_frequency_khz = 0U;

    for (size_t index = 0U; index < sizeof(frequencies_khz) / sizeof(frequencies_khz[0]); ++index) {
        platform_storage_probe_report_t report;
        ESP_LOGI(
            TAG,
            "P4_STORAGE_PROBE D1 PROFILE_START index=%u requested_khz=%" PRIu32,
            (unsigned)index,
            frequencies_khz[index]
        );
        const esp_err_t result = platform_storage_probe_run_profile(frequencies_khz[index], &report);
        log_profile(index, result, &report);

        if (report.card_identity_valid) {
            if (!identity_known) {
                memcpy(expected_identity, report.card_identity_sha256, sizeof(expected_identity));
                identity_known = true;
            } else if (strcmp(expected_identity, report.card_identity_sha256) != 0) {
                ESP_LOGE(TAG, "P4_STORAGE_PROBE D1 FAIL reason=card-identity-changed-between-profiles");
                return;
            }
        }

        if (result == ESP_OK && report.raw_reads_stable && report.filesystem_attempted
            && report.filesystem_io_result == ESP_OK
            && report.filesystem_blocked_write_attempts == 0U) {
            raw_profile_observed = true;
            last_filesystem_result = report.filesystem_result;
            selected_frequency_khz = frequencies_khz[index];
            if (filesystem_result_is_conclusive(report.filesystem_result)) {
                break;
            }
        }
    }

    if (!raw_profile_observed) {
        ESP_LOGE(
            TAG,
            "P4_STORAGE_PROBE D1 FAIL reason=no-stable-raw-profile"
            " attempted_khz=10000,5000,1000 writes=0 formats=0"
        );
        return;
    }

    ESP_LOGI(
        TAG,
        "P4_STORAGE_PROBE D1 COMPLETE raw_interface=pass selected_khz=%" PRIu32
        " filesystem_code=%" PRId32 " filesystem_name=%s writes=0 formats=0",
        selected_frequency_khz,
        last_filesystem_result,
        platform_storage_probe_filesystem_result_name(last_filesystem_result)
    );
}
