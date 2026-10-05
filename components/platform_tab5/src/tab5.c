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
static i2c_master_dev_handle_t s_expander_readback;
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
    const i2c_master_bus_config_t cfg = {
            .i2c_port = PLATFORM_BOARD_I2C_PORT,
            .sda_io_num = PLATFORM_BOARD_I2C_SDA_GPIO,
            .scl_io_num = PLATFORM_BOARD_I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
    };
    if (!s_bus) {
        ret = i2c_new_master_bus(&cfg, &s_bus);
    }
    if (ret == ESP_OK && !s_expander) {
        /* A USB warm reset can interrupt a transaction while the external
         * devices remain powered. Recover only during first board setup,
         * before touch/audio borrow the bus, never under live clients. The
         * vendor constructor removes its temporary device on failure. */
        for (unsigned attempt = 0U; attempt < 3U; ++attempt) {
            ret = esp_io_expander_new_i2c_pi4ioe5v6408(s_bus, 0x43, &s_expander);
            if (ret == ESP_OK || ret == ESP_ERR_NO_MEM ||
                ret == ESP_ERR_INVALID_ARG || attempt == 2U) break;
            const esp_err_t reset_result = i2c_master_bus_reset(s_bus);
            ESP_LOGW("tab5", "I2C_INIT_RETRY attempt=%u error=%s bus_reset=%s",
                     attempt + 2U, esp_err_to_name(ret), esp_err_to_name(reset_result));
            if (reset_result != ESP_OK) {
                /* A failed bus-clear leaves the controller unusable on some
                 * warm starts. No clients exist yet: discard that controller
                 * and retry with a new one. Never reset a borrowed live bus. */
                ret = i2c_del_master_bus(s_bus);
                if (ret != ESP_OK) break;
                s_bus = NULL;
                vTaskDelay(pdMS_TO_TICKS(50));
                ret = i2c_new_master_bus(&cfg, &s_bus);
                if (ret != ESP_OK) break;
                ESP_LOGW("tab5", "I2C_INIT_RECREATED attempt=%u", attempt + 2U);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
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
    /* PI4IOE5V6408 input status (0x0f) always reads zero for outputs.
     * Verify live direction/latch/drive registers instead of the driver's
     * cached values. This confirms control state, not an electrical pin
     * measurement. Datasheet DS40583 tables 4-6 and 10. */
    if (ret == ESP_OK && !s_expander_readback) {
        const i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x43,
            .scl_speed_hz = 400000,
        };
        ret = i2c_master_bus_add_device(s_bus, &cfg, &s_expander_readback);
    }
    uint8_t direction = 0, latch = 0, highz = 0;
    const uint8_t registers[] = {0x03, 0x05, 0x07};
    uint8_t *const values[] = {&direction, &latch, &highz};
    for (unsigned i = 0; i < 3 && ret == ESP_OK; ++i) {
        ret = i2c_master_transmit_receive(s_expander_readback,
            &registers[i], 1, values[i], 1, 100);
    }
    const uint8_t pin = IO_EXPANDER_PIN_NUM_1;
    if (ret == ESP_OK && (!(direction & pin) || (highz & pin) ||
                          !!(latch & pin) != enabled)) ret = ESP_FAIL;
    if (ret != ESP_OK) {
        ESP_LOGE("tab5", "SPEAKER_CONTROL_FAIL enable=%u dir=0x%02x latch=0x%02x highz=0x%02x error=%s",
                 (unsigned)enabled, direction, latch, highz, esp_err_to_name(ret));
    }
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
