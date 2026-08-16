#ifndef PLATFORM_TOUCH_H
#define PLATFORM_TOUCH_H

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
 * create/destroy. It polls and never depends on GPIO interrupts. On a rotated
 * board target, native controller coordinates are transformed into the same
 * landscape coordinate space used by games. `out_frame` is initialized to an
 * invalid, zero-contact frame before touching hardware; therefore every error
 * releases all controls in a fail-closed consumer.
 */
esp_err_t platform_touch_poll(platform_touch_t *touch,
                              platform_touch_frame_t *out_frame);

/** Initialize an explicit valid neutral frame without accessing hardware. */
void platform_touch_frame_neutral(platform_touch_frame_t *frame);

/**
 * Delete the GT911 driver and its I2C device, then clear the handle.
 *
 * The borrowed bus remains caller-owned and must outlive this call. If either
 * cleanup stage fails, ownership is retained so the caller can retry before
 * deleting the shared bus.
 */
esp_err_t platform_touch_destroy(platform_touch_t **touch);

#ifdef __cplusplus
}
#endif

#endif
