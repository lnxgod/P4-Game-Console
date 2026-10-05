#ifndef PLATFORM_I2C_SHARED_BUS_H
#define PLATFORM_I2C_SHARED_BUS_H

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "platform/board.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_I2C_SHARED_PORT ((i2c_port_num_t)PLATFORM_BOARD_I2C_PORT)
#define PLATFORM_I2C_SHARED_SDA_GPIO PLATFORM_BOARD_I2C_SDA_GPIO
#define PLATFORM_I2C_SHARED_SCL_GPIO PLATFORM_BOARD_I2C_SCL_GPIO
#define PLATFORM_I2C_SHARED_CLOCK_HZ 100000U

typedef struct platform_i2c_shared platform_i2c_shared_t;

/**
 * Create the selected board's sole shared I2C bus owner.
 *
 * The caller must already own the board rail that powers VDDPST_5. Touch and
 * audio borrow the returned ESP-IDF handle and must release every device
 * before this object is destroyed. On Tab5 this object is a client wrapper:
 * the board service owns the persistent bus and destroying the wrapper does
 * not delete that bus or its expander clients.
 */
esp_err_t platform_i2c_shared_create(platform_i2c_shared_t **out_bus);

/** Return the borrowed ESP-IDF handle, or NULL for an invalid owner. */
i2c_master_bus_handle_t platform_i2c_shared_handle(
    const platform_i2c_shared_t *bus
);

/**
 * Probe one 7-bit address with a bounded timeout.
 *
 * This is a diagnostic availability check, not ownership of the device.
 */
esp_err_t platform_i2c_shared_probe(
    const platform_i2c_shared_t *bus,
    uint8_t address_7bit,
    int timeout_ms
);

/** Delete the shared bus after every borrower has released its devices. */
esp_err_t platform_i2c_shared_destroy(platform_i2c_shared_t **bus);

#ifdef __cplusplus
}
#endif

#endif
