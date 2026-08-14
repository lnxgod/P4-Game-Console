#ifndef GAMEPAD_GAMEPAD_H
#define GAMEPAD_GAMEPAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAMEPAD_STATE_VERSION 1U

/**
 * Status values shared by the portable gamepad core.
 *
 * Zero is success. Errors are negative so callers can use `status < 0` for
 * failure without depending on a platform-specific error type.
 */
typedef enum {
    GAMEPAD_OK = 0,
    GAMEPAD_ERR_INVALID_ARGUMENT = -1,
    GAMEPAD_ERR_INVALID_STATE = -2,
    GAMEPAD_ERR_DESCRIPTOR_TOO_LARGE = -3,
    GAMEPAD_ERR_REPORT_TOO_LARGE = -4,
    GAMEPAD_ERR_TRUNCATED = -5,
    GAMEPAD_ERR_MALFORMED = -6,
    GAMEPAD_ERR_LIMIT_EXCEEDED = -7,
    GAMEPAD_ERR_UNSUPPORTED = -8,
    GAMEPAD_ERR_NO_GAMEPAD = -9,
    GAMEPAD_ERR_NO_MAPPABLE_INPUT = -10,
    GAMEPAD_ERR_REPORT_ID = -11,
    GAMEPAD_ERR_REPORT_SIZE = -12,
    GAMEPAD_ERR_DISCONNECTED = -13,
} gamepad_status_t;

/** Canonical button bit positions. */
typedef enum {
    GAMEPAD_BUTTON_SOUTH = 0,
    GAMEPAD_BUTTON_EAST = 1,
    GAMEPAD_BUTTON_WEST = 2,
    GAMEPAD_BUTTON_NORTH = 3,
    GAMEPAD_BUTTON_LEFT_SHOULDER = 4,
    GAMEPAD_BUTTON_RIGHT_SHOULDER = 5,
    GAMEPAD_BUTTON_BACK = 6,
    GAMEPAD_BUTTON_START = 7,
    GAMEPAD_BUTTON_GUIDE = 8,
    GAMEPAD_BUTTON_LEFT_STICK = 9,
    GAMEPAD_BUTTON_RIGHT_STICK = 10,
    GAMEPAD_BUTTON_MISC_1 = 11,
    GAMEPAD_BUTTON_PADDLE_1 = 12,
    GAMEPAD_BUTTON_PADDLE_2 = 13,
    GAMEPAD_BUTTON_PADDLE_3 = 14,
    GAMEPAD_BUTTON_PADDLE_4 = 15,
    GAMEPAD_BUTTON_TOUCHPAD = 16,
    GAMEPAD_BUTTON_COUNT = 64,
} gamepad_button_t;

#define GAMEPAD_BUTTON_MASK(button_) (UINT64_C(1) << (unsigned)(button_))

/** Canonical D-pad state. Diagonals are represented by two bits. */
typedef enum {
    GAMEPAD_DPAD_CENTERED = 0,
    GAMEPAD_DPAD_UP = 1U << 0,
    GAMEPAD_DPAD_RIGHT = 1U << 1,
    GAMEPAD_DPAD_DOWN = 1U << 2,
    GAMEPAD_DPAD_LEFT = 1U << 3,
} gamepad_dpad_t;

/** Capabilities derived from a parsed controller layout. */
typedef enum {
    GAMEPAD_CAP_BUTTONS = 1U << 0,
    GAMEPAD_CAP_DPAD = 1U << 1,
    GAMEPAD_CAP_LEFT_STICK = 1U << 2,
    GAMEPAD_CAP_RIGHT_STICK = 1U << 3,
    GAMEPAD_CAP_LEFT_TRIGGER = 1U << 4,
    GAMEPAD_CAP_RIGHT_TRIGGER = 1U << 5,
} gamepad_capability_t;

/**
 * Transport-independent gamepad snapshot.
 *
 * Stick axes span INT16_MIN..INT16_MAX. Triggers span 0..UINT16_MAX.
 * A disconnect always clears all controls before setting connected=false.
 * `sequence` advances for every accepted report and connection transition.
 * This value type does not provide synchronization; the owning platform
 * service must lock or atomically publish complete copies to concurrent readers.
 */
typedef struct {
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint64_t timestamp_us;
    uint64_t buttons;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    uint16_t left_trigger;
    uint16_t right_trigger;
    uint8_t dpad;
    uint8_t connected;
    uint8_t reserved[2];
} gamepad_state_t;

/** Initialize a state object to a valid, disconnected, neutral snapshot. */
void gamepad_state_init(gamepad_state_t *state);

/** Mark a controller connected and start it from a neutral snapshot. */
gamepad_status_t gamepad_state_connect(gamepad_state_t *state, uint64_t timestamp_us);

/** Neutralize every control and mark the controller disconnected before return. */
gamepad_status_t gamepad_state_disconnect(gamepad_state_t *state, uint64_t timestamp_us);

/** Return true only when every dynamic control is neutral. */
bool gamepad_state_is_neutral(const gamepad_state_t *state);

/** Return a static diagnostic name for a status value. */
const char *gamepad_status_name(gamepad_status_t status);

#ifdef __cplusplus
}
#endif

#endif
