#include "platform/board.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    const platform_board_descriptor_t *board = platform_board_get();
    assert(board != NULL);
    assert(platform_board_is(PLATFORM_BOARD_OLIMEX_ESP32_P4_PC_REV_B));
    assert(strcmp(board->slug, "olimex-esp32-p4-pc-rev-b") == 0);
    assert(strcmp(board->vendor, "Olimex") == 0);
    assert(strcmp(board->product, "ESP32-P4-PC") == 0);
    assert(strcmp(board->revision, "B") == 0);
    assert(board->display_width == 1280U);
    assert(board->display_height == 720U);
    assert(board->flash_bytes == 16U * 1024U * 1024U);
    assert(board->psram_bytes == 32U * 1024U * 1024U);
    assert(!board->has_touch);
    assert(board->has_hdmi);
    assert(board->has_sd_card);
    assert(!board->has_usb_device_game_storage);
    assert(board->has_integrated_usb_host_hub);
    return 0;
}
