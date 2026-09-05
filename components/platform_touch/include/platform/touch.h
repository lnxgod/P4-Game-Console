#ifndef PLATFORM_TOUCH_H
#define PLATFORM_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_types.h"
#include "esp_err.h"
#include "platform/board.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_TOUCH_VERSION 1U
#define PLATFORM_TOUCH_WIDTH PLATFORM_BOARD_DISPLAY_WIDTH
#define PLATFORM_TOUCH_HEIGHT PLATFORM_BOARD_DISPLAY_HEIGHT
#define PLATFORM_TOUCH_NATIVE_WIDTH PLATFORM_BOARD_DISPLAY_NATIVE_WIDTH
#define PLATFORM_TOUCH_NATIVE_HEIGHT PLATFORM_BOARD_DISPLAY_NATIVE_HEIGHT
#define PLATFORM_TOUCH_ROTATION_CW_DEGREES \
    PLATFORM_BOARD_DISPLAY_ROTATION_CW_DEGREES
#define PLATFORM_TOUCH_MAX_CONTACTS 5U
#define PLATFORM_TOUCH_I2C_CLOCK_HZ 400000U
#define PLATFORM_TOUCH_GT911_PRIMARY_ADDRESS 0x5dU
#define PLATFORM_TOUCH_GT911_BACKUP_ADDRESS 0x14U
#define PLATFORM_TOUCH_RESET_GPIO PLATFORM_BOARD_TOUCH_RESET_GPIO
#define PLATFORM_TOUCH_INTERRUPT_GPIO PLATFORM_BOARD_TOUCH_INTERRUPT_GPIO
#define PLATFORM_TOUCH_GT911_CONFIG_BYTES 186U
#define PLATFORM_TOUCH_GT911_IDENTITY_BYTES 11U

typedef struct platform_touch platform_touch_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t strength;
    uint16_t reserved;
} platform_touch_contact_t;

typedef struct {
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    int64_t timestamp_us;
    uint8_t contact_count;
    uint8_t valid;
    uint8_t reserved[6];
    platform_touch_contact_t contacts[PLATFORM_TOUCH_MAX_CONTACTS];
} platform_touch_frame_t;

/** Read-only GT911 identity and configuration snapshot. */
typedef struct {
    uint8_t config[PLATFORM_TOUCH_GT911_CONFIG_BYTES];
    uint8_t identity[PLATFORM_TOUCH_GT911_IDENTITY_BYTES];
    uint8_t product_id[4];
    uint16_t firmware_version;
    uint16_t identity_x_resolution;
    uint16_t identity_y_resolution;
    uint8_t vendor_id;
    uint8_t config_version;
    uint16_t config_x_resolution;
    uint16_t config_y_resolution;
    uint8_t max_touch_points;
    uint8_t module_switch_1;
    uint8_t module_switch_2;
    uint8_t shake_count;
    uint8_t filter;
    uint8_t first_filter;
    uint8_t normal_filter;
    uint8_t large_touch;
    uint8_t noise_reduction;
    uint8_t screen_touch_level;
    uint8_t screen_release_level;
    uint8_t low_power_control;
    uint8_t refresh_rate;
    uint8_t refresh_n;
    uint8_t report_period_ms;
    uint8_t x_threshold;
    uint8_t y_threshold;
    uint8_t mini_filter;
    uint8_t config_checksum;
    uint8_t config_checksum_calculated;
    bool config_checksum_valid;
    bool config_fresh;
} platform_touch_gt911_info_t;

/**
 * Exact unit-3 binding and sealed original block for the reviewed restoration.
 *
 * `original_config` must be the complete reviewed 186-byte filter-8 block,
 * including checksum 0x79 and the captured Fresh=0 byte. It is not a generic
 * target configuration. Both arrays are also matched byte-for-byte against
 * the implementation's private reviewed unit-3 identity and baseline before
 * hardware is read, so co-mutating the request and controller cannot authorize
 * another unit or configuration.
 */
typedef struct {
    uint8_t expected_identity[PLATFORM_TOUCH_GT911_IDENTITY_BYTES];
    uint8_t original_config[PLATFORM_TOUCH_GT911_CONFIG_BYTES];
} platform_touch_gt911_restore_reviewed_baseline_request_t;

/** Outcome of the exact-unit reviewed GT911 baseline restoration. */
typedef struct {
    platform_touch_gt911_info_t before;
    platform_touch_gt911_info_t observed;
    bool changed;
    bool already_original;
    /** A complete-block transmission was attempted and may have persisted. */
    bool may_have_changed;
    /** Exact result also returned by the operation. */
    esp_err_t result;
} platform_touch_gt911_restore_reviewed_baseline_result_t;

typedef struct {
    /** Borrowed I2C1 handle. The caller retains and destroys the bus. */
    i2c_master_bus_handle_t bus;
    /** Primary GT911 7-bit address. Must be 0x5d; 0x14 fallback is automatic. */
    uint8_t address_7bit;
} platform_touch_config_t;

/** Fill the recommended 0x5d-primary polling configuration. */
void platform_touch_config_init(platform_touch_config_t *config,
                                i2c_master_bus_handle_t borrowed_bus);

/**
 * Initialize an isolated GT911 client on a caller-owned shared I2C1 bus.
 *
 * The pinned GT911 driver performs the Elecrow address-latch reset sequence on
 * GPIO40/GPIO42, first for 0x5d and then (after complete primary panel-IO
 * cleanup) for 0x14. GPIO42 is left as an input but no ISR is registered.
 *
 * On an ordinary failure, `*out_touch` remains NULL. If removal of an
 * already-created panel-IO device fails, a non-NULL teardown-only handle is
 * returned through `*out_touch`; the error is still returned and the caller
 * must retry platform_touch_destroy() before deleting the borrowed bus.
 * Polling that teardown-only handle fails closed.
 */
esp_err_t platform_touch_create(const platform_touch_config_t *config,
                                platform_touch_t **out_touch);

/**
 * Poll one complete logical-display touch frame with up to five contacts.
 *
 * This function is task-context-only and must be externally serialized with
 * create, destroy, read_info, and restore. It polls and never depends
 * on GPIO interrupts. On a rotated board target, native controller
 * coordinates are transformed into the same landscape coordinate space used
 * by games.
 * `out_frame` is initialized to an
 * invalid, zero-contact frame before touching hardware; therefore every error
 * releases all controls in a fail-closed consumer. `timestamp_us` is the
 * monotonic time when the GT911 data-ready status was observed. It remains
 * unchanged across polls which merely repeat the same report; if a new
 * report arrives between the pre-read status check and the pinned driver's
 * status read, a changed contact frame is stamped when its contents reach
 * software. Valid neutral frames always carry timestamp zero.
 */
esp_err_t platform_touch_poll(platform_touch_t *touch,
                              platform_touch_frame_t *out_frame);

/**
 * Read the complete reviewed GT911 identity/configuration blocks.
 *
 * This function is read-only and task-context-only. It must be externally
 * serialized with poll, restore, and destroy. It reads configuration
 * registers 0x8047..0x8100 (186 bytes) and identity registers 0x8140..0x814A
 * (11 bytes) through the owned panel IO. It never acknowledges touch data;
 * the normal poll path remains responsible for the driver's 0x814E status
 * acknowledgement. `out_info` is cleared before any I2C access.
 */
esp_err_t platform_touch_gt911_read_info(
    platform_touch_t *touch,
    platform_touch_gt911_info_t *out_info);

/**
 * Restore the exact reviewed GT911 baseline after the rejected filter-4 test.
 *
 * This deliberately unit-specific operation accepts only the request's exact
 * identity and either (a) its exact original filter-8/Fresh=0 block with
 * checksum 0x79, which is a no-op, or (b) the same block with only the low six
 * normal-filter bits changed to 4 and checksum changed to 0x7d. Every other
 * byte, including the upper filter bits and Fresh=0, must match exactly.
 *
 * For case (b), the complete sealed original block is transmitted with
 * Fresh=1 and read back after 11 ms. Fresh may be observed as 0 or 1 because
 * the controller can clear it after applying the block; all other 185 bytes
 * and the identity must match exactly. A write or readback failure is returned
 * without any retry or attempt to rewrite the rejected filter-4 state.
 * `out_result->may_have_changed` is set before transmission and
 * `out_result->result` always mirrors the return value once `out_result` has
 * been validated.
 *
 * This function is task-context-only and must be externally serialized with
 * create, destroy, poll, read_info, and other restore calls.
 */
esp_err_t platform_touch_gt911_restore_reviewed_baseline(
    platform_touch_t *touch,
    const platform_touch_gt911_restore_reviewed_baseline_request_t *request,
    platform_touch_gt911_restore_reviewed_baseline_result_t *out_result);

/** Initialize an explicit valid neutral frame without accessing hardware. */
void platform_touch_frame_neutral(platform_touch_frame_t *frame);

/**
 * Delete the GT911 driver and its I2C device, then clear the handle.
 *
 * The borrowed bus remains caller-owned and must outlive this call. This must
 * be externally serialized with poll, read_info, and restore. If either
 * cleanup stage fails, ownership is retained so the caller can retry before
 * deleting the shared bus.
 */
esp_err_t platform_touch_destroy(platform_touch_t **touch);

#ifdef __cplusplus
}
#endif

#endif
