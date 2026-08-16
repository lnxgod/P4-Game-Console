#include "platform/board.h"

#include "sdkconfig.h"

#if defined(CONFIG_PLATFORM_BOARD_TARGET_ELECROW_10_1) && \
    defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
#error "exactly one platform board target must be selected"
#endif

#if !defined(CONFIG_PLATFORM_BOARD_TARGET_ELECROW_10_1) && \
    !defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
#error "a platform board target must be selected"
#endif

platform_board_kind_t platform_board_kind(void)
{
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    return PLATFORM_BOARD_WAVESHARE_4_3;
#else
    return PLATFORM_BOARD_ELECROW_10_1;
#endif
}

const char *platform_board_name(void)
{
#if defined(CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3)
    return "waveshare-esp32-p4-wifi6-touch-lcd-4.3";
#else
    return "elecrow-crowpanel-advanced-10.1";
#endif
}
