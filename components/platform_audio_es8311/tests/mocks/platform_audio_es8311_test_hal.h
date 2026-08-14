#ifndef PLATFORM_AUDIO_ES8311_TEST_HAL_H
#define PLATFORM_AUDIO_ES8311_TEST_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE 0
#define CONFIG_CODEC_ES8311_SUPPORT 1
#define CONFIG_PLATFORM_AUDIO_ES8311_ELECROW_10_1_BUILD_ONLY 1

#define ESP_CODEC_DEV_OK 0
#define ESP_CODEC_DEV_DRV_ERR ESP_FAIL
#define ESP_CODEC_DEV_INVALID_ARG ESP_ERR_INVALID_ARG
#define ESP_CODEC_DEV_NOT_SUPPORT ESP_ERR_NOT_SUPPORTED
#define ESP_CODEC_DEV_WRONG_STATE ESP_ERR_INVALID_STATE
#define ESP_CODEC_DEV_WRITE_FAIL 0x10d
#define ESP_CODEC_DEV_READ_FAIL 0x10e

typedef enum {
    ESP_CODEC_DEV_TYPE_NONE = 0,
    ESP_CODEC_DEV_TYPE_IN = 1,
    ESP_CODEC_DEV_TYPE_OUT = 2,
    ESP_CODEC_DEV_TYPE_IN_OUT = 3,
} esp_codec_dev_type_t;

typedef enum {
    ESP_CODEC_DEV_WORK_MODE_NONE = 0,
    ESP_CODEC_DEV_WORK_MODE_ADC = 1,
    ESP_CODEC_DEV_WORK_MODE_DAC = 2,
    ESP_CODEC_DEV_WORK_MODE_BOTH = 3,
} esp_codec_dec_work_mode_t;

typedef struct {
    uint8_t bits_per_sample;
    uint8_t channel;
    uint16_t channel_mask;
    uint32_t sample_rate;
    int mclk_multiple;
} esp_codec_dev_sample_info_t;

typedef struct {
    float pa_voltage;
    float codec_dac_voltage;
    float pa_gain;
} esp_codec_dev_hw_gain_t;

typedef struct audio_codec_ctrl_if_t audio_codec_ctrl_if_t;
struct audio_codec_ctrl_if_t {
    int (*open)(const audio_codec_ctrl_if_t *, void *, int);
    bool (*is_open)(const audio_codec_ctrl_if_t *);
    int (*read_reg)(const audio_codec_ctrl_if_t *, int, int, void *, int);
    int (*write_reg)(const audio_codec_ctrl_if_t *, int, int, void *, int);
    int (*close)(const audio_codec_ctrl_if_t *);
};

typedef struct audio_codec_data_if_t audio_codec_data_if_t;
struct audio_codec_data_if_t {
    int (*open)(const audio_codec_data_if_t *, void *, int);
    bool (*is_open)(const audio_codec_data_if_t *);
    int (*enable)(const audio_codec_data_if_t *, esp_codec_dev_type_t, bool);
    int (*set_fmt)(const audio_codec_data_if_t *, esp_codec_dev_type_t,
                   esp_codec_dev_sample_info_t *);
    int (*read)(const audio_codec_data_if_t *, uint8_t *, int);
    int (*write)(const audio_codec_data_if_t *, uint8_t *, int);
    int (*close)(const audio_codec_data_if_t *);
};

typedef struct audio_codec_if_t audio_codec_if_t;
struct audio_codec_if_t {
    int (*open)(const audio_codec_if_t *, void *, int);
    bool (*is_open)(const audio_codec_if_t *);
    int (*enable)(const audio_codec_if_t *, bool);
    int (*set_fs)(const audio_codec_if_t *, esp_codec_dev_sample_info_t *);
    int (*mute)(const audio_codec_if_t *, bool);
    int (*set_vol)(const audio_codec_if_t *, float);
    int (*set_mic_gain)(const audio_codec_if_t *, float);
    int (*set_mic_channel_gain)(const audio_codec_if_t *, uint16_t, float);
    int (*mute_mic)(const audio_codec_if_t *, bool);
    int (*set_reg)(const audio_codec_if_t *, int, int);
    int (*get_reg)(const audio_codec_if_t *, int, int *);
    void (*dump_reg)(const audio_codec_if_t *);
    int (*close)(const audio_codec_if_t *);
};

#define ES8311_CODEC_DEFAULT_ADDR 0x30
typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;
    const void *gpio_if;
    esp_codec_dec_work_mode_t codec_mode;
    int16_t pa_pin;
    bool pa_reverted;
    bool master_mode;
    bool use_mclk;
    bool digital_mic;
    bool invert_mclk;
    bool invert_sclk;
    esp_codec_dev_hw_gain_t hw_gain;
    bool no_dac_ref;
    uint16_t mclk_div;
} es8311_codec_cfg_t;

typedef void *esp_codec_dev_handle_t;
typedef struct {
    esp_codec_dev_type_t dev_type;
    const audio_codec_if_t *codec_if;
    const audio_codec_data_if_t *data_if;
} esp_codec_dev_cfg_t;

typedef struct mock_i2c_bus *i2c_master_bus_handle_t;
typedef struct mock_i2c_device *i2c_master_dev_handle_t;
typedef enum { I2C_ADDR_BIT_LEN_7 = 0 } i2c_addr_bit_len_t;
typedef struct {
    i2c_addr_bit_len_t dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
} i2c_device_config_t;

typedef int gpio_num_t;
typedef enum { GPIO_MODE_INPUT_OUTPUT = 3 } gpio_mode_t;
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
    bool auto_clear;
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
#define I2S_CLK_SRC_DEFAULT 0
#define I2S_MCLK_MULTIPLE_256 256
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_BIT_WIDTH_AUTO 0
#define I2S_SLOT_MODE_STEREO 2
#define I2S_STD_SLOT_BOTH 3
#define I2S_GPIO_UNUSED (-1)

typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(milliseconds_) ((TickType_t)(milliseconds_))

esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
int gpio_get_level(gpio_num_t pin);
esp_err_t gpio_config(const gpio_config_t *configuration);

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus,
                           uint16_t address,
                           int timeout_ms);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                    const i2c_device_config_t *configuration,
                                    i2c_master_dev_handle_t *out_device);
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                              const uint8_t *data,
                              size_t byte_count,
                              int timeout_ms);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                      const uint8_t *write_data,
                                      size_t write_bytes,
                                      uint8_t *read_data,
                                      size_t read_bytes,
                                      int timeout_ms);

esp_err_t i2s_new_channel(const i2s_chan_config_t *configuration,
                          i2s_chan_handle_t *out_tx,
                          i2s_chan_handle_t *out_rx);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t channel,
                                    const i2s_std_config_t *configuration);
esp_err_t i2s_channel_enable(i2s_chan_handle_t channel);
esp_err_t i2s_channel_disable(i2s_chan_handle_t channel);
esp_err_t i2s_channel_write(i2s_chan_handle_t channel,
                            const void *data,
                            size_t byte_count,
                            size_t *out_written,
                            uint32_t timeout_ms);
esp_err_t i2s_del_channel(i2s_chan_handle_t channel);
void vTaskDelay(TickType_t ticks);

const audio_codec_if_t *es8311_codec_new(es8311_codec_cfg_t *configuration);
esp_codec_dev_handle_t esp_codec_dev_new(esp_codec_dev_cfg_t *configuration);
int esp_codec_dev_open(esp_codec_dev_handle_t device,
                       esp_codec_dev_sample_info_t *format);
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t device, bool muted);
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t device, int volume);
int esp_codec_dev_close(esp_codec_dev_handle_t device);
void esp_codec_dev_delete(esp_codec_dev_handle_t device);
int audio_codec_delete_codec_if(const audio_codec_if_t *interface);

#endif
