#include "platform/board.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    const platform_board_descriptor_t *board = platform_board_get();
    assert(board != NULL);
    assert(platform_board_is(
        PLATFORM_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3));
    assert(strcmp(board->slug,
                  "waveshare-esp32-p4-wifi6-touch-lcd-4.3") == 0);
    assert(strcmp(board->vendor, "Waveshare") == 0);
    assert(board->display_width == 800U);
    assert(board->display_height == 480U);
    assert(board->flash_bytes == 32U * 1024U * 1024U);
    assert(board->psram_bytes == 32U * 1024U * 1024U);
    assert(board->has_touch);
    assert(board->has_sd_card);
    assert(board->has_usb_device_game_storage);
    assert(!board->has_integrated_usb_host_hub);
    return 0;
}
