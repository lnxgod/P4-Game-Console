#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/display.h"
#include "platform/touch.h"
#include "platform_i2c_shared/bus.h"

#define TOUCH_DIAG_POLL_MS 16U
#define TOUCH_DIAG_HEARTBEAT_POLLS 125U
#define TOUCH_DIAG_SUBMIT_TIMEOUT_MS 250U
#define TOUCH_DIAG_FRAME_WIDTH PLATFORM_DISPLAY_GAME_WIDTH
#define TOUCH_DIAG_FRAME_HEIGHT PLATFORM_DISPLAY_GAME_HEIGHT

static const char *const TAG = "touch_diag";
static platform_i2c_shared_t *s_bus;
static platform_touch_t *s_touch;
static uint16_t *s_pixels;
static bool s_display_ready;

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)((((uint16_t)red & UINT16_C(0xf8)) << 8U) |
                      (((uint16_t)green & UINT16_C(0xfc)) << 3U) |
                      ((uint16_t)blue >> 3U));
}

static void put_pixel(uint16_t *pixels, int x, int y, uint16_t color)
{
    if (x >= 0 && x < TOUCH_DIAG_FRAME_WIDTH &&
        y >= 0 && y < TOUCH_DIAG_FRAME_HEIGHT) {
        pixels[(size_t)y * TOUCH_DIAG_FRAME_WIDTH + (size_t)x] = color;
    }
}

static uint8_t glyph_row(char glyph, unsigned row)
{
    static const uint8_t digits[10][5] = {
        {7U, 5U, 5U, 5U, 7U}, {2U, 6U, 2U, 2U, 7U},
        {7U, 1U, 7U, 4U, 7U}, {7U, 1U, 7U, 1U, 7U},
        {5U, 5U, 7U, 1U, 1U}, {7U, 4U, 7U, 1U, 7U},
        {7U, 4U, 7U, 5U, 7U}, {7U, 1U, 1U, 1U, 1U},
        {7U, 5U, 7U, 5U, 7U}, {7U, 5U, 7U, 1U, 7U},
    };
    if (row >= 5U) {
        return 0U;
    }
    if (glyph >= '0' && glyph <= '9') {
        return digits[(unsigned)(glyph - '0')][row];
    }
    if (glyph == 'X') {
        static const uint8_t x_rows[5] = {5U, 5U, 2U, 5U, 5U};
        return x_rows[row];
    }
    if (glyph == 'Y') {
        static const uint8_t y_rows[5] = {5U, 5U, 2U, 2U, 2U};
        return y_rows[row];
    }
    if (glyph == ':') {
        static const uint8_t colon_rows[5] = {0U, 2U, 0U, 2U, 0U};
        return colon_rows[row];
    }
    if (glyph == ',') {
        static const uint8_t comma_rows[5] = {0U, 0U, 0U, 2U, 4U};
        return comma_rows[row];
    }
    return 0U;
}

static void draw_text(uint16_t *pixels, int x, int y, const char *text,
                      uint16_t color)
{
    for (size_t character = 0U; text[character] != '\0'; ++character) {
        for (unsigned row = 0U; row < 5U; ++row) {
            const uint8_t bits = glyph_row(text[character], row);
            for (unsigned column = 0U; column < 3U; ++column) {
                if ((bits & (uint8_t)(1U << (2U - column))) != 0U) {
                    put_pixel(pixels, x + (int)(character * 4U) +
                              (int)column, y + (int)row, color);
                }
            }
        }
    }
}

static void draw_crosshair(uint16_t *pixels, int x, int y, uint16_t color)
{
    for (int delta = -8; delta <= 8; ++delta) {
        put_pixel(pixels, x + delta, y, color);
        put_pixel(pixels, x, y + delta, color);
    }
    for (int delta = -5; delta <= 5; ++delta) {
        put_pixel(pixels, x - 5, y + delta, color);
        put_pixel(pixels, x + 5, y + delta, color);
        put_pixel(pixels, x + delta, y - 5, color);
        put_pixel(pixels, x + delta, y + 5, color);
    }
}

static void draw_frame(uint16_t *pixels,
                       const platform_touch_frame_t *touch_frame)
{
    const uint16_t background = rgb565(4U, 10U, 18U);
    const uint16_t grid = rgb565(20U, 35U, 48U);
    const uint16_t text = rgb565(210U, 225U, 235U);
    const uint16_t invalid = rgb565(230U, 35U, 45U);
    static const uint16_t colors[PLATFORM_TOUCH_MAX_CONTACTS] = {
        UINT16_C(0xffe0), UINT16_C(0x07ff), UINT16_C(0xf81f),
        UINT16_C(0x07e0), UINT16_C(0xfd20),
    };
    for (size_t index = 0U;
         index < (size_t)TOUCH_DIAG_FRAME_WIDTH * TOUCH_DIAG_FRAME_HEIGHT;
         ++index) {
        pixels[index] = background;
    }
    for (int x = 0; x < TOUCH_DIAG_FRAME_WIDTH; x += 32) {
        for (int y = 0; y < TOUCH_DIAG_FRAME_HEIGHT; ++y) {
            put_pixel(pixels, x, y, grid);
        }
    }
    for (int y = 0; y < TOUCH_DIAG_FRAME_HEIGHT; y += 25) {
        for (int x = 0; x < TOUCH_DIAG_FRAME_WIDTH; ++x) {
            put_pixel(pixels, x, y, grid);
        }
    }
    if (touch_frame->valid == 0U) {
        for (int x = 0; x < TOUCH_DIAG_FRAME_WIDTH; ++x) {
            put_pixel(pixels, x, 0, invalid);
            put_pixel(pixels, x, 1, invalid);
        }
        return;
    }
    for (uint8_t index = 0U; index < touch_frame->contact_count; ++index) {
        const platform_touch_contact_t *const contact =
            &touch_frame->contacts[index];
        const int display_x = (int)(((uint32_t)contact->x *
            (TOUCH_DIAG_FRAME_WIDTH - 1U)) / (PLATFORM_TOUCH_WIDTH - 1U));
        const int display_y = (int)(((uint32_t)contact->y *
            (TOUCH_DIAG_FRAME_HEIGHT - 1U)) / (PLATFORM_TOUCH_HEIGHT - 1U));
        draw_crosshair(pixels, display_x, display_y, colors[index]);
        char coordinates[24];
        (void)snprintf(coordinates, sizeof(coordinates),
                       "%u:X%u,Y%u", (unsigned)index,
                       (unsigned)contact->x, (unsigned)contact->y);
        draw_text(pixels, 4, 4 + (int)index * 7,
                  coordinates, colors[index]);
    }
    if (touch_frame->contact_count == 0U) {
        draw_text(pixels, 4, 4, "0", text);
    }
}

static void halt_safe(const char *stage, esp_err_t error)
{
    esp_err_t cleanup = ESP_OK;
    if (s_display_ready) {
        (void)platform_display_set_brightness(0U);
    }
    if (s_touch != NULL) {
        cleanup = platform_touch_destroy(&s_touch);
    }
    if (cleanup == ESP_OK && s_bus != NULL) {
        cleanup = platform_i2c_shared_destroy(&s_bus);
    }
    if (cleanup == ESP_OK && s_display_ready) {
        cleanup = platform_display_deinit();
        if (cleanup == ESP_OK) {
            s_display_ready = false;
        }
    }
    free(s_pixels);
    s_pixels = NULL;
    ESP_LOGE(TAG,
             "P4_TOUCH T1 HALT stage=%s error=%s cleanup=%s neutral=1",
             stage, esp_err_to_name(error), esp_err_to_name(cleanup));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG,
             "P4_TOUCH T1 START source=elecrow-lesson05 primary=0x5d "
             "fallback=0x14 i2c_hz=%u reset_gpio=40 latch_gpio=42 "
             "runtime_isr=none",
             (unsigned)PLATFORM_TOUCH_I2C_CLOCK_HZ);
    esp_err_t result = platform_display_init();
    if (result != ESP_OK) {
        halt_safe("display-init", result);
    }
    s_display_ready = true;
    s_pixels = heap_caps_malloc(
        (size_t)TOUCH_DIAG_FRAME_WIDTH * TOUCH_DIAG_FRAME_HEIGHT *
        sizeof(*s_pixels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (s_pixels == NULL) {
        halt_safe("overlay-allocate", ESP_ERR_NO_MEM);
    }
    result = platform_i2c_shared_create(&s_bus);
    if (result != ESP_OK) {
        halt_safe("shared-i2c-create", result);
    }
    platform_touch_config_t config;
    platform_touch_config_init(&config, platform_i2c_shared_handle(s_bus));
    result = platform_touch_create(&config, &s_touch);
    if (result != ESP_OK) {
        halt_safe("gt911-create", result);
    }

    platform_touch_frame_t frame;
    platform_touch_frame_neutral(&frame);
    draw_frame(s_pixels, &frame);
    result = platform_display_submit_rgb565(
        s_pixels, TOUCH_DIAG_FRAME_WIDTH, TOUCH_DIAG_SUBMIT_TIMEOUT_MS
    );
    if (result != ESP_OK) {
        halt_safe("first-overlay", result);
    }
    result = platform_display_set_brightness(25U);
    if (result != ESP_OK) {
        halt_safe("backlight", result);
    }
    ESP_LOGI(TAG,
             "P4_TOUCH T1 READY resolution=%ux%u contacts=%u "
             "overlay=crosshair-coordinates brightness=25",
             (unsigned)PLATFORM_TOUCH_WIDTH, (unsigned)PLATFORM_TOUCH_HEIGHT,
             (unsigned)PLATFORM_TOUCH_MAX_CONTACTS);

    uint32_t poll_count = 0U;
    while (true) {
        result = platform_touch_poll(s_touch, &frame);
        if (result != ESP_OK) {
            ESP_LOGE(TAG,
                     "P4_TOUCH T1 READ_FAIL error=%s sequence=%" PRIu32
                     " neutral=1",
                     esp_err_to_name(result), frame.sequence);
        } else if (frame.contact_count > 0U) {
            for (uint8_t index = 0U; index < frame.contact_count; ++index) {
                ESP_LOGI(TAG,
                         "P4_TOUCH T1 CONTACT sequence=%" PRIu32
                         " index=%u x=%u y=%u strength=%u count=%u",
                         frame.sequence, (unsigned)index,
                         (unsigned)frame.contacts[index].x,
                         (unsigned)frame.contacts[index].y,
                         (unsigned)frame.contacts[index].strength,
                         (unsigned)frame.contact_count);
            }
        }
        draw_frame(s_pixels, &frame);
        const esp_err_t display_result = platform_display_submit_rgb565(
            s_pixels, TOUCH_DIAG_FRAME_WIDTH, TOUCH_DIAG_SUBMIT_TIMEOUT_MS
        );
        if (display_result != ESP_OK) {
            halt_safe("overlay-submit", display_result);
        }
        ++poll_count;
        if ((poll_count % TOUCH_DIAG_HEARTBEAT_POLLS) == 0U) {
            ESP_LOGI(TAG,
                     "P4_TOUCH T1 HEARTBEAT polls=%" PRIu32
                     " last_sequence=%" PRIu32 " last_valid=%u last_count=%u",
                     poll_count, frame.sequence, (unsigned)frame.valid,
                     (unsigned)frame.contact_count);
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_DIAG_POLL_MS));
    }
}
