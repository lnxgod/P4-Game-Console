#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "platform/display.h"

enum {
    FRAME_WIDTH = PLATFORM_DISPLAY_GAME_WIDTH,
    FRAME_HEIGHT = PLATFORM_DISPLAY_GAME_HEIGHT,
    GLYPH_WIDTH = 5,
    GLYPH_HEIGHT = 7,
};

#define RGB565_RED UINT16_C(0xf800)
#define RGB565_GREEN UINT16_C(0x07e0)
#define RGB565_BLUE UINT16_C(0x001f)
#define RGB565_WHITE UINT16_C(0xffff)
#define RGB565_BLACK UINT16_C(0x0000)
#define RGB565_YELLOW UINT16_C(0xffe0)
#define RGB565_CYAN UINT16_C(0x07ff)
#define RGB565_MAGENTA UINT16_C(0xf81f)
#define SUBMIT_TIMEOUT_MS UINT32_C(100)

static const char *TAG = "framebuffer_diag";

static void halt_dark(const char *stage, esp_err_t error)
{
    const esp_err_t dark_error = platform_display_set_brightness(0);
    ESP_LOGE(TAG,
             "P4_FRAMEBUFFER M2 FAIL stage=%s error=%s fail_dark=%s",
             stage, esp_err_to_name(error), esp_err_to_name(dark_error));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static const uint8_t *glyph(char character)
{
    static const uint8_t blank[GLYPH_HEIGHT] = {0};
    static const uint8_t glyph_0[GLYPH_HEIGHT] = {14, 17, 19, 21, 25, 17, 14};
    static const uint8_t glyph_1[GLYPH_HEIGHT] = {4, 12, 4, 4, 4, 4, 14};
    static const uint8_t glyph_2[GLYPH_HEIGHT] = {14, 17, 1, 2, 4, 8, 31};
    static const uint8_t glyph_3[GLYPH_HEIGHT] = {30, 1, 1, 14, 1, 1, 30};
    static const uint8_t glyph_4[GLYPH_HEIGHT] = {2, 6, 10, 18, 31, 2, 2};
    static const uint8_t glyph_5[GLYPH_HEIGHT] = {31, 16, 16, 30, 1, 1, 30};
    static const uint8_t glyph_6[GLYPH_HEIGHT] = {14, 16, 16, 30, 17, 17, 14};
    static const uint8_t glyph_7[GLYPH_HEIGHT] = {31, 1, 2, 4, 8, 8, 8};
    static const uint8_t glyph_8[GLYPH_HEIGHT] = {14, 17, 17, 14, 17, 17, 14};
    static const uint8_t glyph_9[GLYPH_HEIGHT] = {14, 17, 17, 15, 1, 1, 14};
    static const uint8_t glyph_b[GLYPH_HEIGHT] = {30, 17, 17, 30, 17, 17, 30};
    static const uint8_t glyph_e[GLYPH_HEIGHT] = {31, 16, 16, 30, 16, 16, 31};
    static const uint8_t glyph_f[GLYPH_HEIGHT] = {31, 16, 16, 30, 16, 16, 16};
    static const uint8_t glyph_g[GLYPH_HEIGHT] = {14, 17, 16, 23, 17, 17, 15};
    static const uint8_t glyph_h[GLYPH_HEIGHT] = {17, 17, 17, 31, 17, 17, 17};
    static const uint8_t glyph_i[GLYPH_HEIGHT] = {14, 4, 4, 4, 4, 4, 14};
    static const uint8_t glyph_l[GLYPH_HEIGHT] = {16, 16, 16, 16, 16, 16, 31};
    static const uint8_t glyph_m[GLYPH_HEIGHT] = {17, 27, 21, 21, 17, 17, 17};
    static const uint8_t glyph_o[GLYPH_HEIGHT] = {14, 17, 17, 17, 17, 17, 14};
    static const uint8_t glyph_p[GLYPH_HEIGHT] = {30, 17, 17, 30, 16, 16, 16};
    static const uint8_t glyph_r[GLYPH_HEIGHT] = {30, 17, 17, 30, 20, 18, 17};
    static const uint8_t glyph_t[GLYPH_HEIGHT] = {31, 4, 4, 4, 4, 4, 4};
    switch (character) {
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case 'B': return glyph_b;
    case 'E': return glyph_e;
    case 'F': return glyph_f;
    case 'G': return glyph_g;
    case 'H': return glyph_h;
    case 'I': return glyph_i;
    case 'L': return glyph_l;
    case 'M': return glyph_m;
    case 'O': return glyph_o;
    case 'P': return glyph_p;
    case 'R': return glyph_r;
    case 'T': return glyph_t;
    default: return blank;
    }
}

static void put_pixel(uint16_t *frame, int x, int y, uint16_t color)
{
    if (x >= 0 && x < FRAME_WIDTH && y >= 0 && y < FRAME_HEIGHT) {
        frame[((size_t)y * FRAME_WIDTH) + (size_t)x] = color;
    }
}

static void draw_text(uint16_t *frame, int x, int y, const char *text,
                      uint16_t color)
{
    while (*text != '\0') {
        const uint8_t *rows = glyph(*text++);
        for (int row = 0; row < GLYPH_HEIGHT; ++row) {
            for (int column = 0; column < GLYPH_WIDTH; ++column) {
                if ((rows[row] & (uint8_t)(1U << (GLYPH_WIDTH - 1 - column))) != 0U) {
                    put_pixel(frame, x + column, y + row, color);
                }
            }
        }
        x += GLYPH_WIDTH + 1;
    }
}

static void draw_frame(uint16_t *frame, uint32_t frame_number)
{
    static const uint16_t fields[] = {
        RGB565_RED, RGB565_GREEN, RGB565_BLUE, RGB565_WHITE, RGB565_BLACK,
    };
    for (int y = 0; y < FRAME_HEIGHT; ++y) {
        for (int x = 0; x < FRAME_WIDTH; ++x) {
            uint16_t color = (uint16_t)((((x / 8) + (y / 8)) & 1) != 0
                                            ? UINT16_C(0x2104)
                                            : UINT16_C(0x4208));
            if (y < 36) {
                color = fields[(size_t)x / 64U];
            }
            frame[((size_t)y * FRAME_WIDTH) + (size_t)x] = color;
        }
    }

    for (int x = 0; x < FRAME_WIDTH; ++x) {
        put_pixel(frame, x, 0, RGB565_YELLOW);
        put_pixel(frame, x, FRAME_HEIGHT - 1, RGB565_YELLOW);
    }
    for (int y = 0; y < FRAME_HEIGHT; ++y) {
        put_pixel(frame, 0, y, RGB565_YELLOW);
        put_pixel(frame, FRAME_WIDTH - 1, y, RGB565_YELLOW);
    }

    const int moving_x = (int)(frame_number % (uint32_t)(FRAME_WIDTH - 12));
    for (int y = 45; y < 165; ++y) {
        for (int x = moving_x; x < moving_x + 12; ++x) {
            put_pixel(frame, x, y,
                      ((y + (int)frame_number) & 1) != 0
                          ? RGB565_WHITE
                          : RGB565_BLACK);
        }
    }

    draw_text(frame, 3, 39, "TL", RGB565_CYAN);
    draw_text(frame, 305, 39, "TR", RGB565_CYAN);
    draw_text(frame, 3, 190, "BL", RGB565_MAGENTA);
    draw_text(frame, 305, 190, "BR", RGB565_MAGENTA);
    draw_text(frame, 150, 39, "TOP", RGB565_YELLOW);
    draw_text(frame, 147, 190, "BOTTOM", RGB565_YELLOW);
    draw_text(frame, 3, 92, "LEFT", RGB565_WHITE);
    draw_text(frame, 288, 92, "RIGHT", RGB565_WHITE);

    char counter[11];
    counter[10] = '\0';
    uint32_t value = frame_number;
    for (int index = 9; index >= 0; --index) {
        counter[index] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    draw_text(frame, 126, 174, "FRAME", RGB565_WHITE);
    draw_text(frame, 158, 174, counter, RGB565_WHITE);
}

static void log_stats(uint32_t frame_number, const char *marker)
{
    platform_display_stats_t stats;
    const esp_err_t err = platform_display_get_stats(&stats);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_FRAMEBUFFER M2 STATS_FAIL error=%s",
                 esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG,
             "P4_FRAMEBUFFER M2 %s frame=%" PRIu32
             " submits=%" PRIu32 " completions=%" PRIu32
             " timeouts=%" PRIu32 " failures=%" PRIu32
             " refreshes=%" PRIu32 " underruns=%s",
             marker, frame_number, stats.submits_started,
             stats.submits_completed, stats.submit_timeouts,
             stats.submit_failures, stats.refresh_completions,
             stats.underrun_count_available ? "available" : "unavailable");
}

void app_main(void)
{
    ESP_LOGI(TAG,
             "P4_FRAMEBUFFER M2 START source=320x200 format=rgb565 "
             "scale=3 viewport=960x600 margins=32/32");
    esp_err_t err = platform_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_FRAMEBUFFER M2 FAIL stage=display-init error=%s",
                 esp_err_to_name(err));
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    uint16_t *frame = heap_caps_malloc(
        (size_t)FRAME_WIDTH * FRAME_HEIGHT * sizeof(*frame),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (frame == NULL) {
        halt_dark("source-allocate", ESP_ERR_NO_MEM);
    }

    uint32_t frame_number = 0;
    draw_frame(frame, frame_number);
    err = platform_display_submit_rgb565(frame, FRAME_WIDTH, 250);
    if (err != ESP_OK) {
        halt_dark("first-submit", err);
    }
    err = platform_display_set_brightness(25);
    if (err != ESP_OK) {
        halt_dark("backlight", err);
    }
    ESP_LOGI(TAG,
             "P4_FRAMEBUFFER M2 FIRST_FRAME fields=red-green-blue-white-black "
             "labels=corners-edges motion=high-contrast backlight_percent=25");

    for (;;) {
        ++frame_number;
        draw_frame(frame, frame_number);
        err = platform_display_submit_rgb565(frame, FRAME_WIDTH,
                                             SUBMIT_TIMEOUT_MS);
        if (err != ESP_OK) {
            log_stats(frame_number, "HALT");
            halt_dark("submit", err);
        }
        if ((frame_number % 60U) == 0U) {
            log_stats(frame_number, "HEARTBEAT");
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
