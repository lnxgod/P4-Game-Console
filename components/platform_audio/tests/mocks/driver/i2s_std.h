#ifndef MOCK_DRIVER_I2S_STD_H
#define MOCK_DRIVER_I2S_STD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct mock_i2s_channel *i2s_chan_handle_t;
typedef int i2s_port_t;
typedef int gpio_num_t;

enum {
    I2S_ROLE_MASTER = 1,
    I2S_CLK_SRC_DEFAULT = 0,
    I2S_MCLK_MULTIPLE_256 = 256,
    I2S_DATA_BIT_WIDTH_16BIT = 16,
    I2S_SLOT_BIT_WIDTH_AUTO = 0,
    I2S_SLOT_MODE_STEREO = 2,
    I2S_STD_SLOT_BOTH = 3,
    I2S_GPIO_UNUSED = -1,
};

typedef struct {
    i2s_port_t controller_id;
    int role;
    uint32_t dma_desc_num;
    uint32_t dma_frame_num;
    bool auto_clear;
} i2s_chan_config_t;

#define I2S_CHANNEL_DEFAULT_CONFIG(controller_, role_)                         \
    ((i2s_chan_config_t){                                                      \
        .controller_id = (controller_),                                       \
        .role = (role_),                                                       \
        .dma_desc_num = 0U,                                                    \
        .dma_frame_num = 0U,                                                   \
        .auto_clear = false,                                                   \
    })

typedef struct {
    uint32_t sample_rate_hz;
    int clk_src;
    int mclk_multiple;
} i2s_std_clk_config_t;

typedef struct {
    int data_bit_width;
    int slot_bit_width;
    int slot_mode;
    int slot_mask;
    int ws_width;
    bool ws_pol;
    bool bit_shift;
    bool left_align;
    bool big_endian;
    bool bit_order_lsb;
} i2s_std_slot_config_t;

typedef struct {
    gpio_num_t mclk;
    gpio_num_t bclk;
    gpio_num_t ws;
    gpio_num_t dout;
    gpio_num_t din;
    struct {
        bool mclk_inv;
        bool bclk_inv;
        bool ws_inv;
    } invert_flags;
} i2s_std_gpio_config_t;

typedef struct {
    i2s_std_clk_config_t clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_std_gpio_config_t gpio_cfg;
} i2s_std_config_t;

esp_err_t i2s_new_channel(const i2s_chan_config_t *config,
                          i2s_chan_handle_t *tx_handle,
                          i2s_chan_handle_t *rx_handle);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t handle,
                                    const i2s_std_config_t *config);
esp_err_t i2s_channel_enable(i2s_chan_handle_t handle);
esp_err_t i2s_channel_write(i2s_chan_handle_t handle,
                            const void *src,
                            size_t size,
                            size_t *bytes_written,
                            uint32_t timeout_ms);
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t handle,
                                   const void *src,
                                   size_t size,
                                   size_t *bytes_loaded);
esp_err_t i2s_channel_disable(i2s_chan_handle_t handle);
esp_err_t i2s_del_channel(i2s_chan_handle_t handle);

#endif
