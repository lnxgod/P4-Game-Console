#ifndef P4_SCRIPT_GAME_API_H
#define P4_SCRIPT_GAME_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define P4_SCRIPT_GAME_API_VERSION UINT16_C(1)
#define P4_SCRIPT_TICK_HZ UINT32_C(60)
#define P4_SCRIPT_SCREEN_WIDTH UINT32_C(768)
#define P4_SCRIPT_SCREEN_HEIGHT UINT32_C(480)
#define P4_SCRIPT_MAX_PLAYERS UINT32_C(4)
#define P4_SCRIPT_TOUCH_MAX_POINTS UINT32_C(5)
#define P4_SCRIPT_RENDER_MAX_COMMANDS UINT32_C(256)
#define P4_SCRIPT_RENDER_PACKET_COUNT UINT32_C(3)
#define P4_SCRIPT_RENDER_MAX_TEXT_BYTES UINT32_C(96)
#define P4_SCRIPT_RENDER_TEXT_CAPACITY UINT32_C(4096)

typedef int32_t p4_script_status_t;
typedef uint32_t p4_script_asset_id_t;

#define P4_SCRIPT_STATUS_OK ((p4_script_status_t)0)
#define P4_SCRIPT_STATUS_INVALID_ARGUMENT ((p4_script_status_t)-1)
#define P4_SCRIPT_STATUS_INVALID_STATE ((p4_script_status_t)-2)
#define P4_SCRIPT_STATUS_LIMIT_REACHED ((p4_script_status_t)-3)
#define P4_SCRIPT_STATUS_WOULD_BLOCK ((p4_script_status_t)-4)
#define P4_SCRIPT_STATUS_BACKEND_FAILED ((p4_script_status_t)-5)
#define P4_SCRIPT_STATUS_TIMED_OUT ((p4_script_status_t)-6)
#define P4_SCRIPT_STATUS_SCRIPT_ERROR ((p4_script_status_t)-7)
#define P4_SCRIPT_STATUS_OUT_OF_MEMORY ((p4_script_status_t)-8)
#define P4_SCRIPT_STATUS_INTERRUPTED ((p4_script_status_t)-9)
#define P4_SCRIPT_STATUS_BAD_FORMAT ((p4_script_status_t)-10)

typedef enum {
    P4_SCRIPT_BUTTON_A = 0,
    P4_SCRIPT_BUTTON_B = 1,
    P4_SCRIPT_BUTTON_X = 2,
    P4_SCRIPT_BUTTON_Y = 3,
    P4_SCRIPT_BUTTON_LEFT_BUMPER = 4,
    P4_SCRIPT_BUTTON_RIGHT_BUMPER = 5,
    P4_SCRIPT_BUTTON_LEFT_STICK = 6,
    P4_SCRIPT_BUTTON_RIGHT_STICK = 7,
    P4_SCRIPT_BUTTON_START = 8,
    P4_SCRIPT_BUTTON_SELECT = 9,
    P4_SCRIPT_BUTTON_HOME = 10,
    P4_SCRIPT_BUTTON_TOUCH_PRIMARY = 11,
    P4_SCRIPT_BUTTON_TOUCH_SECONDARY = 12,
    P4_SCRIPT_BUTTON_COUNT = 13
} p4_script_button_t;

#define P4_SCRIPT_BUTTON_MASK ((UINT64_C(1) << P4_SCRIPT_BUTTON_COUNT) - UINT64_C(1))

typedef enum {
    P4_SCRIPT_DPAD_UP = 1U << 0,
    P4_SCRIPT_DPAD_RIGHT = 1U << 1,
    P4_SCRIPT_DPAD_DOWN = 1U << 2,
    P4_SCRIPT_DPAD_LEFT = 1U << 3
} p4_script_dpad_bits_t;

#define P4_SCRIPT_DPAD_MASK UINT8_C(0x0f)

typedef struct {
    int16_t x;
    int16_t y;
    uint8_t pressed;
    uint8_t reserved[3];
} p4_script_touch_point_t;

typedef struct {
    uint64_t sampled_at_us;
    uint64_t down;
    uint32_t source_epoch;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    uint16_t left_trigger;
    uint16_t right_trigger;
    uint8_t dpad;
    uint8_t connected;
    uint8_t touch_count;
    uint8_t reserved;
    p4_script_touch_point_t touches[P4_SCRIPT_TOUCH_MAX_POINTS];
} p4_script_raw_input_t;

typedef struct {
    uint64_t tick;
    uint64_t sampled_at_us;
    uint64_t down;
    uint64_t pressed;
    uint64_t released;
    uint32_t input_epoch;
    uint16_t version;
    uint16_t size;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    uint16_t left_trigger;
    uint16_t right_trigger;
    uint8_t dpad;
    uint8_t dpad_pressed;
    uint8_t dpad_released;
    uint8_t connected;
    uint8_t touch_count;
    uint8_t reserved[3];
    p4_script_touch_point_t touches[P4_SCRIPT_TOUCH_MAX_POINTS];
} p4_script_input_frame_t;

typedef struct {
    uint64_t tick;
    p4_script_input_frame_t input[P4_SCRIPT_MAX_PLAYERS];
    uint32_t generation;
    uint32_t dt_numerator;
    uint32_t dt_denominator;
    uint16_t version;
    uint16_t size;
} p4_script_tick_frame_t;

typedef enum {
    P4_SCRIPT_RENDER_COMMAND_CLEAR = 1,
    P4_SCRIPT_RENDER_COMMAND_RECT = 2,
    P4_SCRIPT_RENDER_COMMAND_SPRITE = 3,
    P4_SCRIPT_RENDER_COMMAND_LINE = 4,
    P4_SCRIPT_RENDER_COMMAND_CIRCLE = 5,
    P4_SCRIPT_RENDER_COMMAND_TEXT = 6
} p4_script_render_command_type_t;

#define P4_SCRIPT_RENDER_FLAG_FILLED UINT8_C(0x01)
#define P4_SCRIPT_RENDER_FLAG_SPRITE_FLIP_X UINT8_C(0x02)
#define P4_SCRIPT_RENDER_FLAG_SPRITE_FLIP_Y UINT8_C(0x04)

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    p4_script_asset_id_t asset_id;
    uint32_t frame;
    uint16_t color_rgb565;
    uint16_t size;
    uint8_t type;
    uint8_t flags;
    uint16_t reserved;
} p4_script_render_command_t;

typedef struct {
    uint64_t tick;
    uint32_t generation;
    uint32_t command_count;
    uint32_t text_bytes;
    uint16_t version;
    uint16_t flags;
    p4_script_render_command_t commands[P4_SCRIPT_RENDER_MAX_COMMANDS];
    uint8_t text[P4_SCRIPT_RENDER_TEXT_CAPACITY];
} p4_script_render_packet_t;

#ifdef __cplusplus
}
#endif

#endif
