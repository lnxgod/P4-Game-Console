#include "platform/board.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    const platform_board_descriptor_t *board = platform_board_get();
    assert(board != NULL);
    assert(platform_board_is(
        PLATFORM_BOARD_ELECROW_CROWPANEL_ADVANCED_10));
    assert(strcmp(board->slug, "elecrow-crowpanel-advanced-10") == 0);
    assert(strcmp(board->vendor, "Elecrow") == 0);
    assert(strcmp(board->product, "CrowPanel Advanced 10.1-inch") == 0);
    assert(board->display_width == 1024U);
    assert(board->display_height == 600U);
    assert(board->has_touch);
    assert(board->has_usb_device_game_storage);
    assert(!board->has_integrated_usb_host_hub);
    return 0;
}
