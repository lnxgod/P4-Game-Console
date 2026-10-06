#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
const char *esp_err_to_name(int value);
void mock_log(const char *tag, const char *format, ...);
#define ESP_LOGI mock_log
#define ESP_LOGW mock_log
#define ESP_LOGE mock_log
#define PLATFORM_BOARD_I2C_PORT 1
#define PLATFORM_BOARD_I2C_SDA_GPIO 31
#define PLATFORM_BOARD_I2C_SCL_GPIO 32
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
typedef int StaticSemaphore_t;
typedef int *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *p);
int xSemaphoreTake(SemaphoreHandle_t p, unsigned timeout);
void xSemaphoreGive(SemaphoreHandle_t p);
void vTaskDelay(unsigned ticks);
typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
typedef struct {int i2c_port,sda_io_num,scl_io_num,clk_source,glitch_ignore_cnt; struct {bool enable_internal_pullup;} flags;} i2c_master_bus_config_t;
typedef struct {int dev_addr_length,device_address,scl_speed_hz;} i2c_device_config_t;
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *,i2c_master_bus_handle_t *);
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,const i2c_device_config_t *,i2c_master_dev_handle_t *);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t,const uint8_t *,size_t,uint8_t *,size_t,int);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t,int,int);
typedef void *esp_io_expander_handle_t;
#define IO_EXPANDER_PIN_NUM_1 2U
#define IO_EXPANDER_PIN_NUM_4 16U
#define IO_EXPANDER_PIN_NUM_5 32U
#define IO_EXPANDER_OUTPUT_MODE_PUSH_PULL 0
#define IO_EXPANDER_OUTPUT_MODE_OPEN_DRAIN 1
#define IO_EXPANDER_OUTPUT 1
#define IO_EXPANDER_INPUT 0
#define IO_EXPANDER_PULL_UP 1
esp_err_t esp_io_expander_new_i2c_pi4ioe5v6408(i2c_master_bus_handle_t,int,esp_io_expander_handle_t *);
esp_err_t esp_io_expander_set_level(esp_io_expander_handle_t,uint32_t,int);
esp_err_t esp_io_expander_get_level(esp_io_expander_handle_t,uint32_t,uint32_t *);
esp_err_t esp_io_expander_set_dir(esp_io_expander_handle_t,uint32_t,int);
esp_err_t esp_io_expander_set_output_mode(esp_io_expander_handle_t,uint32_t,int);
esp_err_t esp_io_expander_set_pullupdown(esp_io_expander_handle_t,uint32_t,int);
typedef void *esp_lcd_panel_io_handle_t;
typedef struct {int dev_addr,scl_speed_hz,control_phase_bytes,lcd_cmd_bits,lcd_param_bits;struct{bool disable_control_phase;}flags;} esp_lcd_panel_io_i2c_config_t;
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t);
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t,const esp_lcd_panel_io_i2c_config_t *,esp_lcd_panel_io_handle_t *);
esp_err_t esp_lcd_panel_io_rx_param(esp_lcd_panel_io_handle_t,int,void *,size_t);

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t,const uint8_t *,size_t,int);

#define ESP_ERR_NOT_FINISHED 0x10c
#define pdPASS 1
typedef void *TaskHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
int xTaskCreate(void (*entry)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
int64_t esp_timer_get_time(void);
