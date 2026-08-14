#ifndef PLATFORM_AUDIO_FACTORY_TEST_HAL_H
#define PLATFORM_AUDIO_FACTORY_TEST_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

#include "esp_err.h"

#define CONFIG_PLATFORM_AUDIO_FACTORY_ELECROW_10_1_BUILD_ONLY 1

typedef int gpio_num_t;
typedef enum {
    GPIO_MODE_INPUT_OUTPUT = 3,
} gpio_mode_t;
typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0

typedef struct mock_i2s_channel *i2s_chan_handle_t;
typedef int i2s_port_t;
typedef struct {
    int id;
    int role;
    uint32_t dma_desc_num;
    uint32_t dma_frame_num;
    union {
        bool auto_clear;
        bool auto_clear_after_cb;
    };
    bool auto_clear_before_cb;
    bool allow_pd;
    int intr_priority;
} i2s_chan_config_t;
#define I2S_ROLE_MASTER 1
#define I2S_CHANNEL_DEFAULT_CONFIG(port_, role_) \
    ((i2s_chan_config_t){.id = (port_), .role = (role_)})

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

typedef struct {
    uint32_t sample_rate_hz;
    int clk_src;
    int mclk_multiple;
    int dn_sample_mode;
    uint32_t bclk_div;
} i2s_pdm_rx_clk_config_t;
typedef struct {
    int data_bit_width;
    int slot_bit_width;
    int slot_mode;
    int slot_mask;
    int data_fmt;
    bool hp_en;
    float hp_cut_off_freq_hz;
    uint32_t amplify_num;
} i2s_pdm_rx_slot_config_t;
typedef struct {
    gpio_num_t clk;
    union {
        gpio_num_t din;
        gpio_num_t dins[4];
    };
    struct {
        bool clk_inv;
    } invert_flags;
} i2s_pdm_rx_gpio_config_t;
typedef struct {
    i2s_pdm_rx_clk_config_t clk_cfg;
    i2s_pdm_rx_slot_config_t slot_cfg;
    i2s_pdm_rx_gpio_config_t gpio_cfg;
} i2s_pdm_rx_config_t;

#define I2S_CLK_SRC_DEFAULT 0
#define I2S_MCLK_MULTIPLE_256 256
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_BIT_WIDTH_AUTO 0
#define I2S_SLOT_MODE_MONO 1
#define I2S_SLOT_MODE_STEREO 2
#define I2S_STD_SLOT_BOTH 3
#define I2S_PDM_SLOT_LEFT 1
#define I2S_PDM_DATA_FMT_PCM 1
#define I2S_PDM_DSR_8S 8
#define I2S_GPIO_UNUSED (-1)

typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(milliseconds_) ((TickType_t)(milliseconds_))

esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_config(const gpio_config_t *configuration);

esp_err_t i2s_new_channel(const i2s_chan_config_t *configuration,
                          i2s_chan_handle_t *out_tx,
                          i2s_chan_handle_t *out_rx);
esp_err_t i2s_channel_init_pdm_rx_mode(
    i2s_chan_handle_t channel,
    const i2s_pdm_rx_config_t *configuration);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t channel,
                                    const i2s_std_config_t *configuration);
esp_err_t i2s_channel_enable(i2s_chan_handle_t channel);
esp_err_t i2s_channel_disable(i2s_chan_handle_t channel);
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t channel,
                                   const void *data,
                                   size_t byte_count,
                                   size_t *out_loaded);
esp_err_t i2s_channel_write(i2s_chan_handle_t channel,
                            const void *data,
                            size_t byte_count,
                            size_t *out_written,
                            uint32_t timeout_ms);
esp_err_t i2s_del_channel(i2s_chan_handle_t channel);

void vTaskDelay(TickType_t ticks);
int64_t esp_timer_get_time(void);

#endif
