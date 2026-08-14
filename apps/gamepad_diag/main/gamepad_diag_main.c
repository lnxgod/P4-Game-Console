#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include "esp_rom_uart.h"
#pragma GCC diagnostic pop
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gamepad_diag_arm_model.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"
#include "platform_gamepad_usb/platform_gamepad_usb.h"
#include "platform_usb_host/platform_usb_host.h"
#include "sdkconfig.h"

static const char *const TAG = "gamepad_diag";

#define GAMEPAD_DIAG_WINDOW_MS 120000U
#define GAMEPAD_DIAG_SERIAL_ATTACH_MS 2500U
#define GAMEPAD_DIAG_POLL_MS 20U
#define GAMEPAD_DIAG_STATE_LOG_MIN_MS 250U
#define GAMEPAD_DIAG_PROGRESS_MS 5000U
#define GAMEPAD_DIAG_STOP_TIMEOUT_MS 5000U
#define GAMEPAD_DIAG_ARM_TIMEOUT_MS 15000U
#define GAMEPAD_DIAG_ARM_DUPLICATE_GUARD_MS 100U

#ifndef P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX
#error "P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX must be provided by CMake"
#endif

#define GAMEPAD_DIAG_EXPECTED_VID 0x0079U
#define GAMEPAD_DIAG_EXPECTED_PID 0x0011U
#define GAMEPAD_DIAG_EXPECTED_CAPABILITIES                               \
    ((uint32_t)(GAMEPAD_CAP_BUTTONS | GAMEPAD_CAP_DPAD))

static const uint8_t EXPECTED_DESCRIPTOR_SHA256[] = {
    0x05, 0xa1, 0x51, 0xc9, 0x32, 0x36, 0x2f, 0xee,
    0x13, 0x50, 0x39, 0x05, 0x96, 0x28, 0x80, 0xce,
    0x75, 0x21, 0x2b, 0xbf, 0x8c, 0x43, 0xc9, 0x57,
    0x01, 0x52, 0x7b, 0x82, 0x90, 0x83, 0x31, 0x62,
};

static const char s_authorization_id[] = GAMEPAD_DIAG_AUTHORIZATION_ID;
static const char s_arm_token_sha256_hex[] =
    P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256_HEX;

_Static_assert(sizeof(EXPECTED_DESCRIPTOR_SHA256) ==
                   PLATFORM_GAMEPAD_DESCRIPTOR_SHA256_BYTES,
               "expected descriptor digest size must match the public API");
_Static_assert(sizeof(s_authorization_id) - 1U == 49U,
               "D1 authorization identity changed");
_Static_assert(sizeof(s_arm_token_sha256_hex) - 1U ==
                   GAMEPAD_DIAG_ARM_TOKEN_HEX_BYTES,
               "D1 host-arm token digest must remain exact");

static bool lower_hex_nibble(char value, uint8_t *nibble)
{
    if (nibble == NULL) {
        return false;
    }
    if (value >= '0' && value <= '9') {
        *nibble = (uint8_t)(value - '0');
        return true;
    }
    if (value >= 'a' && value <= 'f') {
        *nibble = (uint8_t)(value - 'a' + 10);
        return true;
    }
    return false;
}

static bool decode_lower_hex_digest(const char *hex,
                                    uint8_t digest[GAMEPAD_DIAG_ARM_DIGEST_BYTES])
{
    if (hex == NULL || digest == NULL) {
        return false;
    }
    uint8_t aggregate = 0U;
    for (size_t index = 0U; index < GAMEPAD_DIAG_ARM_DIGEST_BYTES; ++index) {
        uint8_t high = 0U;
        uint8_t low = 0U;
        if (!lower_hex_nibble(hex[index * 2U], &high) ||
            !lower_hex_nibble(hex[index * 2U + 1U], &low)) {
            return false;
        }
        digest[index] = (uint8_t)((high << 4U) | low);
        aggregate = (uint8_t)(aggregate | digest[index]);
    }
    return aggregate != 0U;
}

static gamepad_diag_arm_result_t __attribute__((noinline))
wait_for_host_arm(void)
{
    gamepad_diag_arm_model_t model;
    gamepad_diag_arm_model_init(&model);
    uint8_t expected_digest[GAMEPAD_DIAG_ARM_DIGEST_BYTES] = {0};
    gamepad_diag_arm_result_t result = GAMEPAD_DIAG_ARM_TIMEOUT;

    /* Discard ROM-loader/SLIP residue before publishing the capture boundary. */
    uint8_t discarded = 0U;
    while (esp_rom_output_rx_one_char(&discarded) == 0) {
    }
    ESP_LOGI(TAG,
             "GAMEPAD_D1_WAIT_ARM auth=%s timeout_ms=%u "
             "root_data_port_enabled=0",
             s_authorization_id, (unsigned)GAMEPAD_DIAG_ARM_TIMEOUT_MS);

    const int64_t deadline_us = esp_timer_get_time() +
        ((int64_t)GAMEPAD_DIAG_ARM_TIMEOUT_MS * INT64_C(1000));
    while (esp_timer_get_time() < deadline_us) {
        uint8_t received = 0U;
        if (esp_rom_output_rx_one_char(&received) != 0) {
            vTaskDelay(pdMS_TO_TICKS(1U));
            continue;
        }
        result = gamepad_diag_arm_model_feed(&model, received);
        if (result == GAMEPAD_DIAG_ARM_PENDING) {
            continue;
        }
        if (result == GAMEPAD_DIAG_ARM_FRAME_COMPLETE) {
            break;
        }
        goto cleanup;
    }
    if (result != GAMEPAD_DIAG_ARM_FRAME_COMPLETE) {
        result = gamepad_diag_arm_model_timeout(&model);
        goto cleanup;
    }
    if (!decode_lower_hex_digest(s_arm_token_sha256_hex, expected_digest)) {
        result = GAMEPAD_DIAG_ARM_DIGEST_CONFIG;
        goto cleanup;
    }
    result = gamepad_diag_arm_model_authorize(
        &model, expected_digest, mbedtls_sha256, mbedtls_ct_memcmp,
        mbedtls_platform_zeroize);
    if (result != GAMEPAD_DIAG_ARM_GUARDING) {
        goto cleanup;
    }

    const int64_t duplicate_deadline_us = esp_timer_get_time() +
        ((int64_t)GAMEPAD_DIAG_ARM_DUPLICATE_GUARD_MS * INT64_C(1000));
    while (esp_timer_get_time() < duplicate_deadline_us) {
        uint8_t duplicate = 0U;
        if (esp_rom_output_rx_one_char(&duplicate) == 0) {
            result = gamepad_diag_arm_model_feed(&model, duplicate);
            goto cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    result = gamepad_diag_arm_model_finish_guard(&model);

cleanup:
    gamepad_diag_arm_model_clear(&model, mbedtls_platform_zeroize);
    mbedtls_platform_zeroize(expected_digest, sizeof(expected_digest));
    return result;
}

static void __attribute__((noinline, noreturn))
halt_before_usb(const char *reason)
{
    ESP_LOGE(TAG,
             "GAMEPAD_D1_HALT stage=host-arm reason=%s "
             "root_data_port_enabled=0 automatic_retry=0",
             reason != NULL ? reason : "arm-internal");
    for (;;) {
        vTaskSuspend(NULL);
    }
}

static void __attribute__((noinline)) require_host_arm(void)
{
    const gamepad_diag_arm_result_t arm_result = wait_for_host_arm();
    if (arm_result != GAMEPAD_DIAG_ARM_ACCEPTED) {
        halt_before_usb(gamepad_diag_arm_failure_reason(arm_result));
    }
    ESP_LOGI(TAG,
             "GAMEPAD_D1_ARM_ACCEPTED auth=%s token_sha256=%s tx_count=1 "
             "root_data_port_enabled=0",
             s_authorization_id, s_arm_token_sha256_hex);
}

typedef struct {
    esp_err_t quiesce;
    esp_err_t gamepad_stop;
    esp_err_t final_snapshot;
    esp_err_t host_stop;
    bool gamepad_stop_attempted;
    bool host_stop_attempted;
    bool final_neutral;
    platform_gamepad_snapshot_t snapshot;
} gamepad_diag_cleanup_t;

static bool snapshot_matches_expected(
    const platform_gamepad_snapshot_t *snapshot)
{
    return snapshot != NULL && snapshot->state.connected == 1U &&
           snapshot->identity.transport == PLATFORM_GAMEPAD_TRANSPORT_USB_HID &&
           snapshot->identity.vendor_id == GAMEPAD_DIAG_EXPECTED_VID &&
           snapshot->identity.product_id == GAMEPAD_DIAG_EXPECTED_PID &&
           snapshot->capabilities == GAMEPAD_DIAG_EXPECTED_CAPABILITIES &&
           memcmp(snapshot->identity.descriptor_sha256,
                  EXPECTED_DESCRIPTOR_SHA256,
                  sizeof(EXPECTED_DESCRIPTOR_SHA256)) == 0;
}

static void log_state(const platform_gamepad_snapshot_t *snapshot)
{
    ESP_LOGI(TAG,
             "GAMEPAD_D1_STATE session=%" PRIu32 " sequence=%" PRIu32
             " connected=%u buttons=%016" PRIx64
             " dpad=%u lx=%d ly=%d rx=%d ry=%d lt=%u rt=%u",
             snapshot->session, snapshot->state.sequence,
             (unsigned)snapshot->state.connected, snapshot->state.buttons,
             (unsigned)snapshot->state.dpad, snapshot->state.left_x,
             snapshot->state.left_y, snapshot->state.right_x,
             snapshot->state.right_y,
             (unsigned)snapshot->state.left_trigger,
             (unsigned)snapshot->state.right_trigger);
}

static bool snapshot_has_active_input(
    const platform_gamepad_snapshot_t *snapshot)
{
    return snapshot != NULL && snapshot->state.connected == 1U &&
           (snapshot->state.buttons != 0U ||
            snapshot->state.dpad != GAMEPAD_DPAD_CENTERED);
}

static gamepad_diag_cleanup_t stop_stack(void)
{
    gamepad_diag_cleanup_t cleanup = {
        .quiesce = ESP_ERR_INVALID_STATE,
        .gamepad_stop = ESP_ERR_INVALID_STATE,
        .final_snapshot = ESP_ERR_INVALID_STATE,
        .host_stop = ESP_ERR_INVALID_STATE,
    };

    cleanup.quiesce = platform_usb_host_quiesce();
    if (cleanup.quiesce == ESP_OK) {
        cleanup.gamepad_stop_attempted = true;
        cleanup.gamepad_stop = platform_gamepad_usb_stop(
            pdMS_TO_TICKS(GAMEPAD_DIAG_STOP_TIMEOUT_MS));
    }
    if (cleanup.gamepad_stop == ESP_OK) {
        cleanup.final_snapshot =
            platform_gamepad_usb_get_snapshot(&cleanup.snapshot);
        cleanup.final_neutral = cleanup.final_snapshot == ESP_OK &&
                                cleanup.snapshot.state.connected == 0U &&
                                gamepad_state_is_neutral(&cleanup.snapshot.state);
        cleanup.host_stop_attempted = true;
        cleanup.host_stop = platform_usb_host_stop(
            pdMS_TO_TICKS(GAMEPAD_DIAG_STOP_TIMEOUT_MS));
    }

    ESP_LOGI(TAG,
             "GAMEPAD_D1_CLEANUP quiesce=%s gamepad_stop=%s "
             "gamepad_stop_attempted=%u final_snapshot=%s final_neutral=%u "
             "host_stop=%s host_stop_attempted=%u resources_retained=%u",
             esp_err_to_name(cleanup.quiesce),
             esp_err_to_name(cleanup.gamepad_stop),
             (unsigned)cleanup.gamepad_stop_attempted,
             esp_err_to_name(cleanup.final_snapshot),
             (unsigned)cleanup.final_neutral,
             esp_err_to_name(cleanup.host_stop),
             (unsigned)cleanup.host_stop_attempted,
             (unsigned)(cleanup.quiesce != ESP_OK ||
                        cleanup.gamepad_stop != ESP_OK ||
                        cleanup.host_stop != ESP_OK));
    return cleanup;
}

static bool cleanup_succeeded(const gamepad_diag_cleanup_t *cleanup)
{
    return cleanup != NULL && cleanup->quiesce == ESP_OK &&
           cleanup->gamepad_stop == ESP_OK && cleanup->final_neutral &&
           cleanup->host_stop == ESP_OK;
}

static void halt_with_retained_resources(void)
{
    ESP_LOGE(TAG,
             "GAMEPAD_D1_HALT reason=cleanup-incomplete "
             "resources=possibly-retained "
             "automatic_retry=0");
    for (;;) {
        vTaskSuspend(NULL);
    }
}

#if CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED
#define GAMEPAD_DIAG_FIXTURE_AUTHORIZED 1U
static const platform_usb_fixture_evidence_t FIXTURE_EVIDENCE = {
    .version = PLATFORM_USB_FIXTURE_EVIDENCE_VERSION,
    .size = (uint16_t)sizeof(platform_usb_fixture_evidence_t),
    .current_limit_ma = CONFIG_PLATFORM_USB_HOST_FIXTURE_CURRENT_LIMIT_MA,
    .externally_powered_vbus = CONFIG_PLATFORM_USB_HOST_FIXTURE_EXTERNAL_VBUS,
    .current_limited = CONFIG_PLATFORM_USB_HOST_FIXTURE_CURRENT_LIMITED,
    .backfeed_blocked = CONFIG_PLATFORM_USB_HOST_FIXTURE_BACKFEED_BLOCKED,
    .common_ground = CONFIG_PLATFORM_USB_HOST_FIXTURE_COMMON_GROUND,
    .data_pair_direct = CONFIG_PLATFORM_USB_HOST_FIXTURE_DATA_PAIR_DIRECT,
    .source_role_compliant =
        CONFIG_PLATFORM_USB_HOST_FIXTURE_SOURCE_ROLE_COMPLIANT,
    .overcurrent_fault_visible = CONFIG_PLATFORM_USB_HOST_FIXTURE_FAULT_VISIBLE,
    .board_path_reviewed = CONFIG_PLATFORM_USB_HOST_BOARD_PATH_REVIEWED,
    .evidence_id = CONFIG_PLATFORM_USB_HOST_FIXTURE_EVIDENCE_ID,
    .evidence_sha256 = CONFIG_PLATFORM_USB_HOST_FIXTURE_EVIDENCE_SHA256,
};
#else
#define GAMEPAD_DIAG_FIXTURE_AUTHORIZED 0U
static const platform_usb_fixture_evidence_t FIXTURE_EVIDENCE = {
    .version = PLATFORM_USB_FIXTURE_EVIDENCE_VERSION,
    .size = (uint16_t)sizeof(platform_usb_fixture_evidence_t),
    .current_limit_ma = 0U,
    .externally_powered_vbus = false,
    .current_limited = false,
    .backfeed_blocked = false,
    .common_ground = false,
    .data_pair_direct = false,
    .source_role_compliant = false,
    .overcurrent_fault_visible = false,
    .board_path_reviewed = false,
    .evidence_id = "UNAUTHORIZED",
    .evidence_sha256 = "",
};
#endif

/* Preserve the production path in the inert link while retaining a real load. */
static const volatile uint8_t s_runtime_authorization_gate =
    GAMEPAD_DIAG_FIXTURE_AUTHORIZED;

void app_main(void)
{
    /*
     * The one-shot runner resets and then attaches a monitor to the same UART.
     * Stay completely silent while that handle changes ownership so BOOT is a
     * reliable capture boundary rather than an esptool-coalesced early line.
     */
    vTaskDelay(pdMS_TO_TICKS(GAMEPAD_DIAG_SERIAL_ATTACH_MS));
    ESP_LOGI(TAG,
             "GAMEPAD_D1_SERIAL_ATTACH wait_ms=%u complete=1 "
             "root_data_port_enabled=0",
             (unsigned)GAMEPAD_DIAG_SERIAL_ATTACH_MS);
    ESP_LOGI(TAG,
             "GAMEPAD_D1_BOOT usb_component=1.5.0 hid_component=1.2.0 "
             "controller=p4-hs serial_attach_delay_ms=%u "
             "root_data_port_enabled=0",
             (unsigned)GAMEPAD_DIAG_SERIAL_ATTACH_MS);

    if (s_runtime_authorization_gate != 1U) {
        ESP_LOGW(TAG,
                 "GAMEPAD_D1_BLOCKED "
                 "reason=powered-backfeed-safe-fixture-not-authorized "
                 "root_data_port_enabled=0");
        return;
    }

    require_host_arm();

    /* No platform USB Host, HID, HCD, or root-port call may precede this. */
    esp_err_t result = platform_usb_host_start(&FIXTURE_EVIDENCE);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "GAMEPAD_D1_FAIL stage=host-start code=%s",
                 esp_err_to_name(result));
        halt_with_retained_resources();
    }
    result = platform_gamepad_usb_start();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "GAMEPAD_D1_FAIL stage=hid-start code=%s",
                 esp_err_to_name(result));
        const esp_err_t stop_result =
            platform_usb_host_stop(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "GAMEPAD_D1_HOST_ROLLBACK code=%s",
                 esp_err_to_name(stop_result));
        if (stop_result != ESP_OK) {
            halt_with_retained_resources();
        }
        return;
    }
    result = platform_usb_host_enable_root_port();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "GAMEPAD_D1_FAIL stage=root-port-enable code=%s",
                 esp_err_to_name(result));
        const gamepad_diag_cleanup_t cleanup = stop_stack();
        if (!cleanup_succeeded(&cleanup)) {
            halt_with_retained_resources();
        }
        return;
    }

    ESP_LOGI(TAG,
             "GAMEPAD_D1_READY tier=1-generic-hid fixture=%s "
             "evidence_sha256=%s external_vbus_fixture_owned=1 "
             "root_data_port_enabled=1 window_ms=%u",
             FIXTURE_EVIDENCE.evidence_id,
             FIXTURE_EVIDENCE.evidence_sha256,
             (unsigned)GAMEPAD_DIAG_WINDOW_MS);

    const TickType_t start_tick = xTaskGetTickCount();
    TickType_t last_state_log_tick = start_tick;
    TickType_t last_progress_tick = start_tick;
    uint32_t prior_sequence = UINT32_MAX;
    uint32_t first_session = 0U;
    uint32_t latest_session = 0U;
    uint32_t reports_at_latest_connect = 0U;
    bool previously_connected = false;
    bool saw_exact_connection = false;
    bool saw_active_input = false;
    bool saw_physical_disconnect = false;
    bool saw_disconnect_neutral = false;
    bool saw_held_disconnect_neutral = false;
    bool saw_reconnect = false;
    bool last_connected_state_active = false;
    bool runtime_failed = false;
    const char *failure_reason = "none";

    while ((xTaskGetTickCount() - start_tick) <
           pdMS_TO_TICKS(GAMEPAD_DIAG_WINDOW_MS)) {
        platform_gamepad_snapshot_t snapshot;
        const esp_err_t snapshot_result =
            platform_gamepad_usb_get_snapshot(&snapshot);
        if (snapshot_result != ESP_OK) {
            runtime_failed = true;
            failure_reason = "snapshot-read";
            ESP_LOGE(TAG, "GAMEPAD_D1_FAIL stage=snapshot-read code=%s",
                     esp_err_to_name(snapshot_result));
            break;
        }

        platform_gamepad_usb_stats_t stats;
        const esp_err_t stats_result = platform_gamepad_usb_get_stats(&stats);
        if (stats_result != ESP_OK) {
            runtime_failed = true;
            failure_reason = "stats-read";
            ESP_LOGE(TAG, "GAMEPAD_D1_FAIL stage=stats-read code=%s",
                     esp_err_to_name(stats_result));
            break;
        }
        if (stats.reports_dropped != 0U || stats.malformed_reports != 0U ||
            stats.callback_faults != 0U) {
            runtime_failed = true;
            failure_reason = "transport-fault";
            ESP_LOGE(TAG,
                     "GAMEPAD_D1_FAIL stage=transport-fault dropped=%" PRIu32
                     " malformed=%" PRIu32 " callback_faults=%" PRIu32,
                     stats.reports_dropped, stats.malformed_reports,
                     stats.callback_faults);
            break;
        }

        const bool connected = snapshot.state.connected == 1U;
        bool state_logged = false;
        if (connected &&
            (!previously_connected || snapshot.session != latest_session)) {
            if (!snapshot_matches_expected(&snapshot)) {
                runtime_failed = true;
                failure_reason = "identity-profile-mismatch";
                ESP_LOGE(TAG,
                         "GAMEPAD_D1_FAIL stage=identity-profile-mismatch "
                         "vid=%04x pid=%04x interface=%u transport=%u "
                         "capabilities=0x%08" PRIx32,
                         snapshot.identity.vendor_id,
                         snapshot.identity.product_id,
                         (unsigned)snapshot.identity.interface_number,
                         (unsigned)snapshot.identity.transport,
                         snapshot.capabilities);
                break;
            }
            latest_session = snapshot.session;
            reports_at_latest_connect = stats.reports_committed;
            if (!saw_exact_connection) {
                first_session = snapshot.session;
            } else if (saw_physical_disconnect &&
                       snapshot.session != first_session) {
                saw_reconnect = true;
            }
            saw_exact_connection = true;
            ESP_LOGI(TAG,
                     "GAMEPAD_D1_CONNECTED session=%" PRIu32
                     " vid=0079 pid=0011 interface=%u transport=usb-hid "
                     "descriptor_sha256="
                     "05a151c932362fee13503905962880ce75212bbf8c43c95701527b8290833162 "
                     "profile=usb-gamepad-0079-0011 capabilities=0x%08" PRIx32
                     " reconnect_after_disconnect=%u",
                     snapshot.session,
                     (unsigned)snapshot.identity.interface_number,
                     snapshot.capabilities, (unsigned)saw_reconnect);
            log_state(&snapshot);
            state_logged = true;
            prior_sequence = snapshot.state.sequence;
            last_state_log_tick = xTaskGetTickCount();
        }

        if (!connected && previously_connected) {
            const bool neutral = gamepad_state_is_neutral(&snapshot.state);
            saw_physical_disconnect = true;
            saw_disconnect_neutral = saw_disconnect_neutral || neutral;
            saw_held_disconnect_neutral =
                saw_held_disconnect_neutral ||
                (last_connected_state_active && neutral);
            ESP_LOGI(TAG,
                     "GAMEPAD_D1_DISCONNECTED session=%" PRIu32
                     " sequence=%" PRIu32 " source=device-event",
                     snapshot.session, snapshot.state.sequence);
            ESP_LOGI(TAG,
                     "GAMEPAD_D1_NEUTRAL session=%" PRIu32
                     " source=device-event neutral=%u held_input_before=%u",
                     snapshot.session, (unsigned)neutral,
                     (unsigned)last_connected_state_active);
            if (!neutral) {
                runtime_failed = true;
                failure_reason = "disconnect-not-neutral";
                ESP_LOGE(TAG,
                         "GAMEPAD_D1_FAIL stage=disconnect-neutral neutral=0");
                break;
            }
        }

        const TickType_t now_tick = xTaskGetTickCount();
        const bool active_input = snapshot_has_active_input(&snapshot);
        if (active_input && !saw_active_input) {
            saw_active_input = true;
            if (!state_logged) {
                log_state(&snapshot);
                state_logged = true;
                prior_sequence = snapshot.state.sequence;
                last_state_log_tick = now_tick;
            }
            ESP_LOGI(TAG,
                     "GAMEPAD_D1_ACTIVE_INPUT session=%" PRIu32
                     " sequence=%" PRIu32 " buttons=%016" PRIx64
                     " dpad=%u",
                     snapshot.session, snapshot.state.sequence,
                     snapshot.state.buttons, (unsigned)snapshot.state.dpad);
        }
        if (connected && snapshot.state.sequence != prior_sequence &&
            (now_tick - last_state_log_tick) >=
                pdMS_TO_TICKS(GAMEPAD_DIAG_STATE_LOG_MIN_MS)) {
            log_state(&snapshot);
            prior_sequence = snapshot.state.sequence;
            last_state_log_tick = now_tick;
        }
        if (connected) {
            last_connected_state_active = active_input;
        }

        if ((now_tick - last_progress_tick) >=
            pdMS_TO_TICKS(GAMEPAD_DIAG_PROGRESS_MS)) {
            ESP_LOGI(TAG,
                     "GAMEPAD_D1_PROGRESS elapsed_ms=%" PRIu32
                     " connected=%u exact=%u reports=%" PRIu32
                     " disconnect=%u reconnect=%u",
                     (uint32_t)((now_tick - start_tick) * portTICK_PERIOD_MS),
                     (unsigned)connected, (unsigned)saw_exact_connection,
                     stats.reports_committed,
                     (unsigned)saw_physical_disconnect,
                     (unsigned)saw_reconnect);
            last_progress_tick = now_tick;
        }

        previously_connected = connected;
        if (saw_reconnect &&
            stats.reports_committed > reports_at_latest_connect) {
            ESP_LOGI(TAG,
                     "GAMEPAD_D1_WINDOW_COMPLETE reason=disconnect-reconnect-report");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(GAMEPAD_DIAG_POLL_MS));
    }

    platform_gamepad_usb_stats_t final_stats = {0};
    esp_err_t final_stats_result =
        platform_gamepad_usb_get_stats(&final_stats);
    const bool connected_before_cleanup = previously_connected;
    const uint32_t session_before_cleanup = latest_session;
    const gamepad_diag_cleanup_t cleanup = stop_stack();

    platform_gamepad_usb_stats_t post_cleanup_stats;
    const esp_err_t post_cleanup_stats_result =
        platform_gamepad_usb_get_stats(&post_cleanup_stats);
    if (post_cleanup_stats_result == ESP_OK) {
        final_stats = post_cleanup_stats;
    } else {
        final_stats_result = post_cleanup_stats_result;
    }

    if (connected_before_cleanup && cleanup.final_snapshot == ESP_OK) {
        ESP_LOGI(TAG,
                 "GAMEPAD_D1_DISCONNECTED session=%" PRIu32
                 " sequence=%" PRIu32 " source=controlled-cleanup",
                 session_before_cleanup, cleanup.snapshot.state.sequence);
        ESP_LOGI(TAG,
                 "GAMEPAD_D1_NEUTRAL session=%" PRIu32
                 " source=controlled-cleanup neutral=%u held_input_before=%u",
                 session_before_cleanup, (unsigned)cleanup.final_neutral,
                 (unsigned)last_connected_state_active);
    }

    ESP_LOGI(TAG,
             "GAMEPAD_D1_STATS interfaces_seen=%" PRIu32
             " interfaces_rejected=%" PRIu32 " connections=%" PRIu32
             " disconnections=%" PRIu32 " reports_committed=%" PRIu32
             " reports_dropped=%" PRIu32 " malformed_reports=%" PRIu32
             " callback_faults=%" PRIu32,
             final_stats.interfaces_seen, final_stats.interfaces_rejected,
             final_stats.connections, final_stats.disconnections,
             final_stats.reports_committed, final_stats.reports_dropped,
             final_stats.malformed_reports, final_stats.callback_faults);

    const bool transport_clean = final_stats_result == ESP_OK &&
                                 final_stats.reports_dropped == 0U &&
                                 final_stats.malformed_reports == 0U &&
                                 final_stats.callback_faults == 0U;
    const bool passed = !runtime_failed && saw_exact_connection &&
                        final_stats.reports_committed > 0U && saw_active_input &&
                        transport_clean && cleanup_succeeded(&cleanup);
    if (!passed && !runtime_failed) {
        if (!saw_exact_connection) {
            failure_reason = "no-exact-controller";
        } else if (final_stats.reports_committed == 0U) {
            failure_reason = "no-input-report";
        } else if (!saw_active_input) {
            failure_reason = "no-active-input";
        } else if (!transport_clean) {
            failure_reason = "transport-fault";
        } else {
            failure_reason = "cleanup-failed";
        }
    }
    if (!passed) {
        ESP_LOGE(TAG, "GAMEPAD_D1_FAIL stage=result reason=%s",
                 failure_reason);
    }
    ESP_LOGI(TAG,
             "GAMEPAD_D1_RESULT result=%s reason=%s exact_identity_profile=%u "
             "report_seen=%u active_input_seen=%u initial_enumeration=%u "
             "physical_disconnect=%u disconnect_neutral=%u "
             "held_disconnect_neutral=%u reconnect=%u cleanup_neutral=%u "
             "cleanup_complete=%u",
             passed ? "PASS" : "FAIL", passed ? "none" : failure_reason,
             (unsigned)saw_exact_connection,
             (unsigned)(final_stats.reports_committed > 0U),
             (unsigned)saw_active_input,
             (unsigned)saw_exact_connection,
             (unsigned)saw_physical_disconnect,
             (unsigned)saw_disconnect_neutral,
             (unsigned)saw_held_disconnect_neutral,
             (unsigned)saw_reconnect,
             (unsigned)cleanup.final_neutral,
             (unsigned)cleanup_succeeded(&cleanup));

    if (!cleanup_succeeded(&cleanup)) {
        halt_with_retained_resources();
    }

    ESP_LOGI(TAG,
             "GAMEPAD_D1_TERMINAL state=halted root_data_port_enabled=0 "
             "resources_retained=0 automatic_retry=0");
    for (;;) {
        vTaskSuspend(NULL);
    }
}
