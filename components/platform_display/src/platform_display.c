#include "platform/display.h"
#include "platform_display_layout.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "driver/gpio.h"
#include "driver/ledc.h"
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
#include "driver/ppa.h"
#include "esp_lcd_st7701.h"
#include "waveshare_st7701_init.h"
#else
#include "esp_lcd_ek79007.h"
#endif
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#pragma GCC diagnostic pop

static const char *TAG = "platform_display";

/*
 * Display-only constants authorized by hardware/evidence/
 * elecrow-10.1-display-path.json. GPIO29 and GPIO41 remain untouched.
 */
#define DISPLAY_DSI_BUS_ID 0
#define DISPLAY_DSI_DATA_LANES 2
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
#define DISPLAY_DSI_LANE_RATE_MBPS 500
#define DISPLAY_DPI_CLOCK_MHZ 30
#define DISPLAY_DPI_HBP 42
#define DISPLAY_DPI_HSYNC 12
#define DISPLAY_DPI_HFP 42
#define DISPLAY_DPI_VBP 2
#define DISPLAY_DPI_VSYNC 8
#define DISPLAY_DPI_VFP 60
#else
#define DISPLAY_DSI_LANE_RATE_MBPS 900
#define DISPLAY_DPI_CLOCK_MHZ 51
#define DISPLAY_DPI_HBP 160
#define DISPLAY_DPI_HSYNC 70
#define DISPLAY_DPI_HFP 160
#define DISPLAY_DPI_VBP 23
#define DISPLAY_DPI_VSYNC 10
#define DISPLAY_DPI_VFP 12
#endif
#define DISPLAY_DPHY_LDO_CHANNEL 3
#define DISPLAY_DPHY_LDO_MV 2500
#define DISPLAY_PANEL_LDO_CHANNEL 4
#define DISPLAY_PANEL_LDO_MV 3300
#define DISPLAY_BACKLIGHT_GPIO \
    ((gpio_num_t)PLATFORM_BOARD_LCD_BACKLIGHT_GPIO)
#define DISPLAY_BACKLIGHT_PWM_HZ 30000
#define DISPLAY_BACKLIGHT_DUTY_BITS LEDC_TIMER_11_BIT
#define DISPLAY_BACKLIGHT_MAX_DUTY 2047U
#define DISPLAY_FRAME_PIXELS \
    ((size_t)PLATFORM_DISPLAY_NATIVE_WIDTH * \
     (size_t)PLATFORM_DISPLAY_NATIVE_HEIGHT)
#define DISPLAY_FRAME_BYTES (DISPLAY_FRAME_PIXELS * sizeof(uint16_t))
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
#define DISPLAY_GAME_PPA_SCALE 2.375f
#define DISPLAY_GAME_PPA_OFFSET_X 2U
#define DISPLAY_GAME_PPA_OFFSET_Y 20U
#define DISPLAY_GAME_PPA_WIDTH 475U
#define DISPLAY_GAME_PPA_HEIGHT 760U
#endif

static esp_ldo_channel_handle_t s_dphy_ldo;
static esp_ldo_channel_handle_t s_panel_ldo;
static esp_lcd_dsi_bus_handle_t s_dsi_bus;
static esp_lcd_panel_io_handle_t s_dbi_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_backlight_ready;
static bool s_initialized;
static bool s_pattern_active;
static uint16_t *s_submit_frame;
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
static ppa_client_handle_t s_game_scaler;
static uint16_t *s_panel_frames[2];
static uint8_t s_active_panel_frame;
static bool s_accelerator_failure_logged;
#endif
static StaticSemaphore_t s_api_lock_storage;
static StaticSemaphore_t s_refresh_signal_storage;
static SemaphoreHandle_t s_api_lock;
static SemaphoreHandle_t s_refresh_signal;
static portMUX_TYPE s_sync_init_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_display_stats_t s_stats;

static bool ensure_sync_objects(void)
{
    taskENTER_CRITICAL(&s_sync_init_lock);
    if (s_api_lock == NULL) {
        s_api_lock = xSemaphoreCreateMutexStatic(&s_api_lock_storage);
    }
    if (s_refresh_signal == NULL) {
        s_refresh_signal =
            xSemaphoreCreateBinaryStatic(&s_refresh_signal_storage);
    }
    const bool ready = s_api_lock != NULL && s_refresh_signal != NULL;
    taskEXIT_CRITICAL(&s_sync_init_lock);
    return ready;
}

static TickType_t milliseconds_to_ticks(uint32_t milliseconds)
{
    if (milliseconds == 0U) {
        return 0;
    }

    uint64_t ticks =
        (((uint64_t)milliseconds * (uint64_t)configTICK_RATE_HZ) + 999U) /
        1000U;
    if (ticks > (uint64_t)portMAX_DELAY) {
        ticks = (uint64_t)portMAX_DELAY;
    }
    return (TickType_t)ticks;
}

static TickType_t remaining_ticks(TickType_t started, TickType_t budget)
{
    if (budget == portMAX_DELAY) {
        return portMAX_DELAY;
    }
    const TickType_t elapsed = xTaskGetTickCount() - started;
    return elapsed >= budget ? 0 : budget - elapsed;
}

static esp_err_t wait_for_refresh_after(uint32_t baseline,
                                        TickType_t started,
                                        TickType_t budget)
{
    while (__atomic_load_n(&s_stats.refresh_completions, __ATOMIC_ACQUIRE) ==
           baseline) {
        const TickType_t remaining = remaining_ticks(started, budget);
        if (remaining == 0 ||
            xSemaphoreTake(s_refresh_signal, remaining) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

static bool IRAM_ATTR on_refresh_done(esp_lcd_panel_handle_t panel,
                                      esp_lcd_dpi_panel_event_data_t *event,
                                      void *user_context)
{
    (void)panel;
    (void)event;
    (void)user_context;

    __atomic_fetch_add(&s_stats.refresh_completions, 1U, __ATOMIC_RELAXED);
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (s_refresh_signal != NULL) {
        (void)xSemaphoreGiveFromISR(s_refresh_signal,
                                    &higher_priority_task_woken);
    }
    return higher_priority_task_woken == pdTRUE;
}

static esp_err_t configure_backlight_dark(void)
{
    const gpio_config_t pin_config = {
        .pin_bit_mask = UINT64_C(1) << DISPLAY_BACKLIGHT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&pin_config);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_level(DISPLAY_BACKLIGHT_GPIO, 0);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = DISPLAY_BACKLIGHT_DUTY_BITS,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = DISPLAY_BACKLIGHT_PWM_HZ,
        .clk_cfg = LEDC_USE_PLL_DIV_CLK,
        .deconfigure = false,
    };
    err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = DISPLAY_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    err = ledc_channel_config(&channel_config);
    if (err == ESP_OK) {
        s_backlight_ready = true;
    }
    return err;
}

static void release_owned_resources(void)
{
    s_initialized = false;
    s_pattern_active = false;
    if (s_backlight_ready) {
        (void)ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        (void)ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        (void)ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        s_backlight_ready = false;
    }
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    if (s_game_scaler != NULL) {
        (void)ppa_unregister_client(s_game_scaler);
        s_game_scaler = NULL;
    }
    memset(s_panel_frames, 0, sizeof(s_panel_frames));
    s_active_panel_frame = 0U;
    s_accelerator_failure_logged = false;
#endif
    if (s_submit_frame != NULL) {
        heap_caps_free(s_submit_frame);
        s_submit_frame = NULL;
    }
    if (s_panel != NULL) {
        const esp_lcd_dpi_panel_event_callbacks_t callbacks = {0};
        (void)esp_lcd_dpi_panel_register_event_callbacks(s_panel, &callbacks,
                                                         NULL);
        (void)esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_dbi_io != NULL) {
        (void)esp_lcd_panel_io_del(s_dbi_io);
        s_dbi_io = NULL;
    }
    if (s_dsi_bus != NULL) {
        (void)esp_lcd_del_dsi_bus(s_dsi_bus);
        s_dsi_bus = NULL;
    }
    if (s_panel_ldo != NULL) {
        (void)esp_ldo_release_channel(s_panel_ldo);
        s_panel_ldo = NULL;
    }
    if (s_dphy_ldo != NULL) {
        (void)esp_ldo_release_channel(s_dphy_ldo);
        s_dphy_ldo = NULL;
    }
}

static esp_err_t set_brightness_locked(uint8_t percent)
{
    if (percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_backlight_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Zero is always allowed for fail-dark cleanup. Light is allowed only
     * after panel reset and initialization completed successfully. */
    if (percent != 0U && !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t duty = (DISPLAY_BACKLIGHT_MAX_DUTY * (uint32_t)percent) / 100U;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t platform_display_set_brightness(uint8_t percent)
{
    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = set_brightness_locked(percent);
    (void)xSemaphoreGive(s_api_lock);
    return err;
}

esp_err_t platform_display_init(void)
{
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
#if !CONFIG_PLATFORM_DISPLAY_WAVESHARE_4_3_BUILD_ONLY
    ESP_LOGE(TAG, "P4_DISPLAY AUTHORIZATION_DENIED board=waveshare-4.3");
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
#if !CONFIG_PLATFORM_DISPLAY_ELECROW_10_1_CROSS_REVISION_AUTHORIZED
    ESP_LOGE(TAG, "P4_DISPLAY M1 AUTHORIZATION_DENIED scope=display-only");
    return ESP_ERR_NOT_SUPPORTED;
#endif
#endif

    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_initialized) {
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "P4_DISPLAY M1 START profile=%s", platform_board_name());
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    ESP_LOGI(TAG,
             "P4_DISPLAY M1 SCOPE waveshare-st7701 backlight_gpio=%d "
             "reset_gpio=%d",
             PLATFORM_BOARD_LCD_BACKLIGHT_GPIO,
             PLATFORM_BOARD_LCD_RESET_GPIO);
#else
    ESP_LOGI(TAG, "P4_DISPLAY M1 SCOPE display-only gpio29=untouched gpio41=untouched");
#endif

    esp_err_t err = configure_backlight_dark();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=backlight-dark error=%s", esp_err_to_name(err));
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }
    ESP_LOGI(TAG, "P4_DISPLAY M1 BACKLIGHT_DARK gpio=%d pwm_hz=%u",
             PLATFORM_BOARD_LCD_BACKLIGHT_GPIO,
             (unsigned)DISPLAY_BACKLIGHT_PWM_HZ);

    const esp_ldo_channel_config_t dphy_ldo_config = {
        .chan_id = DISPLAY_DPHY_LDO_CHANNEL,
        .voltage_mv = DISPLAY_DPHY_LDO_MV,
    };
    err = esp_ldo_acquire_channel(&dphy_ldo_config, &s_dphy_ldo);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=ldo3 error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }

#if !CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    const esp_ldo_channel_config_t panel_ldo_config = {
        .chan_id = DISPLAY_PANEL_LDO_CHANNEL,
        .voltage_mv = DISPLAY_PANEL_LDO_MV,
    };
    err = esp_ldo_acquire_channel(&panel_ldo_config, &s_panel_ldo);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=ldo4 error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }
#endif
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    ESP_LOGI(TAG,
             "P4_DISPLAY M1 POWER_READY ldo3_mv=%u panel_rail=board-owned",
             (unsigned)DISPLAY_DPHY_LDO_MV);
#else
    ESP_LOGI(TAG, "P4_DISPLAY M1 POWER_READY ldo3_mv=%u ldo4_mv=%u",
             (unsigned)DISPLAY_DPHY_LDO_MV,
             (unsigned)DISPLAY_PANEL_LDO_MV);
#endif

    const esp_lcd_dsi_bus_config_t dsi_bus_config = {
        .bus_id = DISPLAY_DSI_BUS_ID,
        .num_data_lanes = DISPLAY_DSI_DATA_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = DISPLAY_DSI_LANE_RATE_MBPS,
    };
    err = esp_lcd_new_dsi_bus(&dsi_bus_config, &s_dsi_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=dsi-bus error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }

    const esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_config, &s_dbi_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=dbi-io error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }

    const esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DISPLAY_DPI_CLOCK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
        .num_fbs = 2,
#else
        .num_fbs = 1,
#endif
        .video_timing = {
            .h_size = PLATFORM_DISPLAY_NATIVE_WIDTH,
            .v_size = PLATFORM_DISPLAY_NATIVE_HEIGHT,
            .hsync_back_porch = DISPLAY_DPI_HBP,
            .hsync_pulse_width = DISPLAY_DPI_HSYNC,
            .hsync_front_porch = DISPLAY_DPI_HFP,
            .vsync_back_porch = DISPLAY_DPI_VBP,
            .vsync_pulse_width = DISPLAY_DPI_VSYNC,
            .vsync_front_porch = DISPLAY_DPI_VFP,
        },
        .flags.use_dma2d = false,
    };
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    const st7701_vendor_config_t vendor_config = {
        .init_cmds = waveshare_st7701_init_cmds,
        .init_cmds_size = sizeof(waveshare_st7701_init_cmds) /
            sizeof(waveshare_st7701_init_cmds[0]),
        .flags = {
            .use_mipi_interface = 1,
        },
        .mipi_config = {
            .dsi_bus = s_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
#else
    const ek79007_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = s_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = DISPLAY_DSI_DATA_LANES,
        },
    };
#endif
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = (gpio_num_t)PLATFORM_BOARD_LCD_RESET_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    err = esp_lcd_new_panel_st7701(s_dbi_io, &panel_config, &s_panel);
#else
    err = esp_lcd_new_panel_ek79007(s_dbi_io, &panel_config, &s_panel);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=panel-new error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }
    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=panel-reset error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=panel-init error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }

#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    err = esp_lcd_dpi_panel_get_frame_buffer(
        s_panel, 2U, (void **)&s_panel_frames[0],
        (void **)&s_panel_frames[1]);
    if (err != ESP_OK || s_panel_frames[0] == NULL ||
        s_panel_frames[1] == NULL) {
        ESP_LOGE(TAG,
                 "P4_DISPLAY M2 FAIL stage=panel-framebuffers error=%s",
                 esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    const ppa_client_config_t scaler_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1U,
    };
    err = ppa_register_client(&scaler_config, &s_game_scaler);
    if (err != ESP_OK) {
        s_game_scaler = NULL;
        ESP_LOGW(TAG,
                 "P4_DISPLAY GAME_ACCELERATOR ready=0 fallback=cpu error=%s",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG,
                 "P4_DISPLAY GAME_ACCELERATOR ready=1 engine=ppa-srm "
                 "source=320x200 target=475x760 rotation_ccw=90 "
                 "double_buffer=1");
    }
#endif

    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_refresh_done = on_refresh_done,
    };
    err = esp_lcd_dpi_panel_register_event_callbacks(s_panel, &callbacks, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M2 FAIL stage=refresh-callback error=%s",
                 esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG,
             "P4_DISPLAY M1 PANEL_READY native=%ux%u logical=%ux%u "
             "rotation_cw=%u format=rgb565 lane_mbps=%u dpi_mhz=%u board=%s",
             (unsigned)PLATFORM_DISPLAY_NATIVE_WIDTH,
             (unsigned)PLATFORM_DISPLAY_NATIVE_HEIGHT,
             (unsigned)PLATFORM_DISPLAY_WIDTH,
             (unsigned)PLATFORM_DISPLAY_HEIGHT,
             (unsigned)PLATFORM_DISPLAY_ROTATION_CW_DEGREES,
             DISPLAY_DSI_LANE_RATE_MBPS, DISPLAY_DPI_CLOCK_MHZ,
             platform_board_name());
    (void)xSemaphoreGive(s_api_lock);
    return ESP_OK;
}

esp_err_t platform_display_deinit(void)
{
    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_initialized && !s_backlight_ready && s_panel == NULL) {
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_INVALID_STATE;
    }
    release_owned_resources();
    (void)xSemaphoreGive(s_api_lock);
    return ESP_OK;
}

esp_err_t platform_display_show_pattern(platform_display_pattern_t pattern)
{
    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_initialized || s_panel == NULL) {
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_INVALID_STATE;
    }

    mipi_dsi_pattern_type_t dsi_pattern;
    const char *name;
    switch (pattern) {
    case PLATFORM_DISPLAY_PATTERN_COLOR_BARS_VERTICAL:
        dsi_pattern = MIPI_DSI_PATTERN_BAR_VERTICAL;
        name = "bars-vertical";
        break;
    case PLATFORM_DISPLAY_PATTERN_COLOR_BARS_HORIZONTAL:
        dsi_pattern = MIPI_DSI_PATTERN_BAR_HORIZONTAL;
        name = "bars-horizontal";
        break;
    case PLATFORM_DISPLAY_PATTERN_BER_VERTICAL:
        dsi_pattern = MIPI_DSI_PATTERN_BER_VERTICAL;
        name = "ber-vertical";
        break;
    case PLATFORM_DISPLAY_PATTERN_BLACK:
        dsi_pattern = MIPI_DSI_PATTERN_NONE;
        name = "black";
        break;
    default:
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_lcd_dpi_panel_set_pattern(s_panel, dsi_pattern);
    if (err == ESP_OK) {
        s_pattern_active = dsi_pattern != MIPI_DSI_PATTERN_NONE;
        ESP_LOGI(TAG, "P4_DISPLAY M1 PATTERN name=%s", name);
    }
    (void)xSemaphoreGive(s_api_lock);
    return err;
}

typedef bool (*display_layout_fn_t)(
    const uint16_t *, size_t, uint16_t *, size_t, size_t);

#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
static void clear_accelerated_game_margins(uint16_t *destination)
{
    const size_t row_pixels = PLATFORM_DISPLAY_NATIVE_WIDTH;
    memset(destination, 0,
           DISPLAY_GAME_PPA_OFFSET_Y * row_pixels * sizeof(*destination));
    memset(destination +
               (DISPLAY_GAME_PPA_OFFSET_Y + DISPLAY_GAME_PPA_HEIGHT) *
                   row_pixels,
           0,
           (PLATFORM_DISPLAY_NATIVE_HEIGHT - DISPLAY_GAME_PPA_OFFSET_Y -
            DISPLAY_GAME_PPA_HEIGHT) * row_pixels * sizeof(*destination));
    for (size_t y = DISPLAY_GAME_PPA_OFFSET_Y;
         y < DISPLAY_GAME_PPA_OFFSET_Y + DISPLAY_GAME_PPA_HEIGHT; ++y) {
        uint16_t *const row = destination + y * row_pixels;
        memset(row, 0, DISPLAY_GAME_PPA_OFFSET_X * sizeof(*row));
        memset(row + DISPLAY_GAME_PPA_OFFSET_X + DISPLAY_GAME_PPA_WIDTH, 0,
               (PLATFORM_DISPLAY_NATIVE_WIDTH - DISPLAY_GAME_PPA_OFFSET_X -
                DISPLAY_GAME_PPA_WIDTH) * sizeof(*row));
    }
}

static bool accelerate_game_frame(const uint16_t *source,
                                  size_t source_stride_pixels,
                                  uint16_t *destination)
{
    if (s_game_scaler == NULL || source_stride_pixels !=
            PLATFORM_DISPLAY_GAME_WIDTH || destination == NULL) {
        return false;
    }
    const ppa_srm_oper_config_t operation = {
        .in = {
            .buffer = source,
            .pic_w = PLATFORM_DISPLAY_GAME_WIDTH,
            .pic_h = PLATFORM_DISPLAY_GAME_HEIGHT,
            .block_w = PLATFORM_DISPLAY_GAME_WIDTH,
            .block_h = PLATFORM_DISPLAY_GAME_HEIGHT,
            .block_offset_x = 0U,
            .block_offset_y = 0U,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = destination,
            .buffer_size = (uint32_t)DISPLAY_FRAME_BYTES,
            .pic_w = PLATFORM_DISPLAY_NATIVE_WIDTH,
            .pic_h = PLATFORM_DISPLAY_NATIVE_HEIGHT,
            .block_offset_x = DISPLAY_GAME_PPA_OFFSET_X,
            .block_offset_y = DISPLAY_GAME_PPA_OFFSET_Y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_90,
        .scale_x = DISPLAY_GAME_PPA_SCALE,
        .scale_y = DISPLAY_GAME_PPA_SCALE,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    const esp_err_t result =
        ppa_do_scale_rotate_mirror(s_game_scaler, &operation);
    if (result == ESP_OK) {
        clear_accelerated_game_margins(destination);
        __atomic_fetch_add(
            &s_stats.accelerated_submits, 1U, __ATOMIC_RELAXED);
        return true;
    }
    __atomic_fetch_add(
        &s_stats.accelerator_failures, 1U, __ATOMIC_RELAXED);
    if (!s_accelerator_failure_logged) {
        s_accelerator_failure_logged = true;
        ESP_LOGW(TAG,
                 "P4_DISPLAY GAME_ACCELERATOR_FAIL error=%s fallback=cpu",
                 esp_err_to_name(result));
    }
    return false;
}
#endif

static esp_err_t submit_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    size_t minimum_stride,
    uint32_t timeout_ms,
    display_layout_fn_t layout)
{
    if (source == NULL || source_stride_pixels < minimum_stride ||
        layout == NULL) {
        __atomic_fetch_add(&s_stats.submit_failures, 1U, __ATOMIC_RELAXED);
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_sync_objects()) {
        __atomic_fetch_add(&s_stats.submit_failures, 1U, __ATOMIC_RELAXED);
        return ESP_ERR_NO_MEM;
    }

    const TickType_t budget = milliseconds_to_ticks(timeout_ms);
    const TickType_t started = xTaskGetTickCount();
    if (xSemaphoreTake(s_api_lock, budget) != pdTRUE) {
        __atomic_fetch_add(&s_stats.submit_timeouts, 1U, __ATOMIC_RELAXED);
        return ESP_ERR_TIMEOUT;
    }
    __atomic_fetch_add(&s_stats.submits_started, 1U, __ATOMIC_RELAXED);

    esp_err_t err = ESP_OK;
    if (!s_initialized || s_panel == NULL) {
        err = ESP_ERR_INVALID_STATE;
        goto fail;
    }

#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    const bool game_layout =
        layout == platform_display_layout_rgb565_320x200;
    const uint8_t target_index =
        (uint8_t)(s_active_panel_frame == 0U ? 1U : 0U);
    uint16_t *const output_frame = s_panel_frames[target_index];
    if (output_frame == NULL) {
        err = ESP_ERR_INVALID_STATE;
        goto fail_dark;
    }
    const bool accelerated = game_layout && accelerate_game_frame(
        source, source_stride_pixels, output_frame);
    if (!accelerated && !layout(
            source, source_stride_pixels, output_frame,
            PLATFORM_DISPLAY_NATIVE_WIDTH,
            PLATFORM_DISPLAY_NATIVE_HEIGHT)) {
        err = ESP_ERR_INVALID_ARG;
        goto fail;
    }
#else
    if (s_submit_frame == NULL) {
        s_submit_frame = heap_caps_malloc(
            DISPLAY_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_submit_frame == NULL) {
            err = ESP_ERR_NO_MEM;
            goto fail_dark;
        }
    }
    if (!layout(
            source, source_stride_pixels, s_submit_frame,
            PLATFORM_DISPLAY_NATIVE_WIDTH,
            PLATFORM_DISPLAY_NATIVE_HEIGHT)) {
        err = ESP_ERR_INVALID_ARG;
        goto fail;
    }
    uint16_t *const output_frame = s_submit_frame;
#endif

    if (s_pattern_active) {
        err = esp_lcd_dpi_panel_set_pattern(
            s_panel, MIPI_DSI_PATTERN_NONE);
        if (err != ESP_OK) {
            goto fail_dark;
        }
        s_pattern_active = false;
    }
    while (xSemaphoreTake(s_refresh_signal, 0) == pdTRUE) {
    }
    err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0,
                                    PLATFORM_DISPLAY_NATIVE_WIDTH,
                                    PLATFORM_DISPLAY_NATIVE_HEIGHT,
                                    output_frame);
    if (err != ESP_OK) {
        goto fail_dark;
    }
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    s_active_panel_frame = target_index;
#endif
    const uint32_t refresh_baseline =
        __atomic_load_n(&s_stats.refresh_completions, __ATOMIC_ACQUIRE);
    /*
     * CPU draw is a synchronous copy/cache-writeback. Capture the counter
     * after that copy returns, then qualify this submit on a later refresh.
     * The semaphore is only a wake hint: if VSYNC arrives between this atomic
     * load and xSemaphoreTake(), the monotonic counter prevents it from being
     * drained or lost. A VSYNC during the copy never qualifies the submit.
     */
    if (wait_for_refresh_after(refresh_baseline, started, budget) != ESP_OK) {
        __atomic_fetch_add(&s_stats.submit_timeouts, 1U, __ATOMIC_RELAXED);
        const uint32_t refresh_current = __atomic_load_n(
            &s_stats.refresh_completions, __ATOMIC_ACQUIRE);
        ESP_LOGE(TAG,
                 "P4_DISPLAY M2 SUBMIT_TIMEOUT timeout_ms=%" PRIu32
                 " refresh_baseline=%" PRIu32 " refresh_current=%" PRIu32
                 " backlight_preserved=1",
                 timeout_ms, refresh_baseline, refresh_current);
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_TIMEOUT;
    }

    __atomic_fetch_add(&s_stats.submits_completed, 1U, __ATOMIC_RELAXED);
    (void)xSemaphoreGive(s_api_lock);
    return ESP_OK;

fail_dark:
    (void)set_brightness_locked(0);
fail:
    __atomic_fetch_add(&s_stats.submit_failures, 1U, __ATOMIC_RELAXED);
    ESP_LOGE(TAG, "P4_DISPLAY M2 SUBMIT_FAIL error=%s",
             esp_err_to_name(err));
    (void)xSemaphoreGive(s_api_lock);
    return err;
}

esp_err_t platform_display_submit_rgb565(const uint16_t *source,
                                         size_t source_stride_pixels,
                                         uint32_t timeout_ms)
{
    return submit_rgb565(
        source, source_stride_pixels, PLATFORM_DISPLAY_GAME_WIDTH,
        timeout_ms, platform_display_layout_rgb565_320x200);
}

esp_err_t platform_display_submit_content_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms)
{
    return submit_rgb565(
        source, source_stride_pixels, PLATFORM_DISPLAY_CONTENT_WIDTH,
        timeout_ms, platform_display_layout_rgb565_768x480);
}

esp_err_t platform_display_get_stats(platform_display_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    out_stats->submits_started =
        __atomic_load_n(&s_stats.submits_started, __ATOMIC_RELAXED);
    out_stats->submits_completed =
        __atomic_load_n(&s_stats.submits_completed, __ATOMIC_RELAXED);
    out_stats->submit_timeouts =
        __atomic_load_n(&s_stats.submit_timeouts, __ATOMIC_RELAXED);
    out_stats->submit_failures =
        __atomic_load_n(&s_stats.submit_failures, __ATOMIC_RELAXED);
    out_stats->refresh_completions =
        __atomic_load_n(&s_stats.refresh_completions, __ATOMIC_RELAXED);
    out_stats->accelerated_submits =
        __atomic_load_n(&s_stats.accelerated_submits, __ATOMIC_RELAXED);
    out_stats->accelerator_failures =
        __atomic_load_n(&s_stats.accelerator_failures, __ATOMIC_RELAXED);
    out_stats->underrun_count_available = false;
    return ESP_OK;
}
