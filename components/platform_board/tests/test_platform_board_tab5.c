#include "platform/board.h"
#include <assert.h>
#include <string.h>
int main(void) {
    const platform_board_descriptor_t *b=platform_board_get();
    assert(b && b->id==PLATFORM_BOARD_M5STACK_TAB5);
    assert(!strcmp(b->slug,"m5stack-tab5") && !strcmp(b->vendor,"M5Stack"));
    assert(b->display_width==1280 && b->display_height==720);
    assert(b->flash_bytes==16777216 && b->psram_bytes==33554432);
    assert(b->has_touch && b->has_sd_card && b->has_speaker);
    assert(!b->has_usb_device_game_storage && !b->has_integrated_usb_host_hub);
    assert(PLATFORM_BOARD_I2C_SDA_GPIO==31 && PLATFORM_BOARD_I2C_SCL_GPIO==32);
    assert(PLATFORM_BOARD_LCD_RESET_GPIO==-1 && PLATFORM_BOARD_AUDIO_AMP_GPIO==-1);
    assert(PLATFORM_BOARD_GAME_VIEWPORT_WIDTH==1152 && PLATFORM_BOARD_GAME_MARGIN_LEFT==64);
    return 0;
}
