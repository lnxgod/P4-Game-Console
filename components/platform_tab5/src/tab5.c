// SPDX-License-Identifier: Apache-2.0
// Reset sequencing derived from Espressif esp-bsp, pinned in third_party/tab5-bsp.json.
#include "platform/tab5.h"
#include "platform/board.h"
#include "esp_io_expander_pi4ioe5v6408.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"

#if !CONFIG_P4_BOARD_M5STACK_TAB5
#error "platform_tab5 requires the dedicated Tab5 sdkconfig board selection"
#endif

static i2c_master_bus_handle_t s_bus;
static esp_io_expander_handle_t s_expander;
static StaticSemaphore_t s_lock_storage;
static SemaphoreHandle_t s_lock;
static platform_tab5_panel_t s_panel;
static esp_lcd_panel_io_handle_t s_probe_io;

/* Initialization is performed by display before other services start. */
esp_err_t platform_tab5_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t ret = ESP_OK;
    if (!s_bus) {
        const i2c_master_bus_config_t cfg = {
            .i2c_port = PLATFORM_BOARD_I2C_PORT,
            .sda_io_num = PLATFORM_BOARD_I2C_SDA_GPIO,
            .scl_io_num = PLATFORM_BOARD_I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        ret = i2c_new_master_bus(&cfg, &s_bus);
    }
    if (ret == ESP_OK && !s_expander) {
        ret = esp_io_expander_new_i2c_pi4ioe5v6408(s_bus, 0x43, &s_expander);
    }
    xSemaphoreGive(s_lock);
    return ret;
}
i2c_master_bus_handle_t platform_tab5_i2c(void) { return s_bus; }

static esp_err_t output_locked(uint32_t pin, bool level)
{
    /* Latch before output enable to avoid an unintended active pulse. */
    esp_err_t ret = esp_io_expander_set_level(s_expander, pin, level);
    if (ret == ESP_OK) ret = esp_io_expander_set_output_mode(s_expander, pin, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
    if (ret == ESP_OK) ret = esp_io_expander_set_dir(s_expander, pin, IO_EXPANDER_OUTPUT);
    return ret;
}
esp_err_t platform_tab5_speaker_enable(bool enabled)
{
    esp_err_t ret = platform_tab5_init();
    if (ret != ESP_OK) return ret;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    ret = output_locked(IO_EXPANDER_PIN_NUM_1, enabled);
    uint32_t actual = 0;
    if (ret == ESP_OK) ret = esp_io_expander_get_level(s_expander, IO_EXPANDER_PIN_NUM_1, &actual);
    if (ret == ESP_OK && !!(actual & IO_EXPANDER_PIN_NUM_1) != enabled) ret = ESP_FAIL;
    xSemaphoreGive(s_lock);
    return ret;
}
esp_err_t platform_tab5_display_reset(void)
{
    esp_err_t ret = platform_tab5_init();
    if (ret != ESP_OK) return ret;
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    /* The previous initialization releases LCD_RST as an input. The generic
     * expander setter refuses writes to inputs, so enter open-drain output
     * first. An old high latch is high impedance, never a driven-high reset. */
    ret = esp_io_expander_set_output_mode(s_expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_OUTPUT_MODE_OPEN_DRAIN);
    if (ret == ESP_OK) ret = esp_io_expander_set_dir(s_expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_OUTPUT);
    if (ret == ESP_OK) ret = esp_io_expander_set_level(s_expander, IO_EXPANDER_PIN_NUM_4, false);
    if (ret == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
        ret = esp_io_expander_set_pullupdown(s_expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_PULL_UP);
    }
    /* Do not drive LCD_RST high: ST712x shares this net with its I2C logic. */
    if (ret == ESP_OK) ret = esp_io_expander_set_dir(s_expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_INPUT);
    if (ret == ESP_OK) ret = esp_io_expander_set_output_mode(s_expander, IO_EXPANDER_PIN_NUM_4, IO_EXPANDER_OUTPUT_MODE_OPEN_DRAIN);
    if (ret == ESP_OK) ret = output_locked(IO_EXPANDER_PIN_NUM_5, true);
    xSemaphoreGive(s_lock);
    if (ret == ESP_OK) vTaskDelay(pdMS_TO_TICKS(500));
    return ret;
}
esp_err_t platform_tab5_panel_detect(platform_tab5_panel_t *out)
{
    if (!out || !s_bus) return ESP_ERR_INVALID_ARG;
    *out = TAB5_PANEL_UNKNOWN;
    if (s_probe_io) {
        esp_err_t cleanup=esp_lcd_panel_io_del(s_probe_io);
        if (cleanup!=ESP_OK) return cleanup;
        s_probe_io=NULL;
    }
    if (s_panel != TAB5_PANEL_UNKNOWN) { *out = s_panel; return ESP_OK; }
    if (i2c_master_probe(s_bus, 0x55, 100) == ESP_OK) {
        const esp_lcd_panel_io_i2c_config_t cfg = {
            .dev_addr = 0x55, .scl_speed_hz = 400000,
            .control_phase_bytes = 1, .lcd_cmd_bits = 16, .lcd_param_bits = 8,
            .flags.disable_control_phase = 1,
        };
        esp_err_t ret = esp_lcd_new_panel_io_i2c(s_bus, &cfg, &s_probe_io);
        if (ret != ESP_OK) return ret;
        uint8_t version = 0;
        ret = esp_lcd_panel_io_rx_param(s_probe_io, 0, &version, 1);
        esp_err_t cleanup = esp_lcd_panel_io_del(s_probe_io);
        if (cleanup==ESP_OK) s_probe_io=NULL;
        if (ret != ESP_OK) return ret;
        if (cleanup != ESP_OK) return cleanup;
        s_panel = version == 1 ? TAB5_PANEL_ST7121 : version == 3 ? TAB5_PANEL_ST7123 : TAB5_PANEL_UNKNOWN;
    } else if (i2c_master_probe(s_bus, 0x14, 100) == ESP_OK) {
        s_panel = TAB5_PANEL_ILI9881C;
    }
    *out = s_panel;
    if (s_panel == TAB5_PANEL_UNKNOWN) return ESP_ERR_NOT_SUPPORTED;
    ESP_LOGI("tab5", "TAB5 PANEL model=%s", platform_tab5_panel_name(s_panel));
    return ESP_OK;
}
const char *platform_tab5_panel_name(platform_tab5_panel_t panel)
{
    switch (panel) {
    case TAB5_PANEL_ILI9881C: return "ili9881c-gt911";
    case TAB5_PANEL_ST7123: return "st7123";
    case TAB5_PANEL_ST7121: return "st7121";
    default: return "unknown";
    }
}
