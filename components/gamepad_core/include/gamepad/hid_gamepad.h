#ifndef GAMEPAD_HID_GAMEPAD_H
#define GAMEPAD_HID_GAMEPAD_H

#include "gamepad/gamepad.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hard bounds for all untrusted HID data. */
#define GAMEPAD_HID_LAYOUT_VERSION 1U
#define GAMEPAD_HID_MAX_DESCRIPTOR_BYTES 2048U
#define GAMEPAD_HID_MAX_REPORT_BYTES 1024U
#define GAMEPAD_HID_MAX_FIELDS 128U
#define GAMEPAD_HID_MAX_REPORTS 16U
#define GAMEPAD_HID_MAX_REPORT_COUNT 256U
#define GAMEPAD_HID_MAX_COLLECTION_DEPTH 16U
#define GAMEPAD_HID_MAX_GLOBAL_STACK_DEPTH 8U
#define GAMEPAD_HID_MAX_LOCAL_USAGES 64U
#define GAMEPAD_HID_DESCRIPTOR_SHA256_BYTES 32U

/* HID Usage Tables values used by the generic mapper. */
#define GAMEPAD_HID_USAGE_PAGE_GENERIC_DESKTOP 0x0001U
#define GAMEPAD_HID_USAGE_PAGE_BUTTON 0x0009U
#define GAMEPAD_HID_USAGE_JOYSTICK 0x0004U
#define GAMEPAD_HID_USAGE_GAME_PAD 0x0005U
#define GAMEPAD_HID_USAGE_MULTI_AXIS_CONTROLLER 0x0008U
#define GAMEPAD_HID_USAGE_X 0x0030U
#define GAMEPAD_HID_USAGE_Y 0x0031U
#define GAMEPAD_HID_USAGE_Z 0x0032U
#define GAMEPAD_HID_USAGE_RX 0x0033U
#define GAMEPAD_HID_USAGE_RY 0x0034U
#define GAMEPAD_HID_USAGE_RZ 0x0035U
#define GAMEPAD_HID_USAGE_HAT_SWITCH 0x0039U

/** Destination for one parsed HID input field. */
typedef enum {
    GAMEPAD_HID_MAP_IGNORE = 0,
    GAMEPAD_HID_MAP_LEFT_X,
    GAMEPAD_HID_MAP_LEFT_Y,
    GAMEPAD_HID_MAP_RIGHT_X,
    GAMEPAD_HID_MAP_RIGHT_Y,
    GAMEPAD_HID_MAP_LEFT_TRIGGER,
    GAMEPAD_HID_MAP_RIGHT_TRIGGER,
    GAMEPAD_HID_MAP_HAT,
    GAMEPAD_HID_MAP_BUTTON,
    GAMEPAD_HID_MAP_DPAD_X,
    GAMEPAD_HID_MAP_DPAD_Y,
} gamepad_hid_map_target_t;

typedef enum {
    GAMEPAD_HID_FIELD_NULL_STATE = 1U << 0,
    GAMEPAD_HID_FIELD_ARRAY = 1U << 1,
} gamepad_hid_field_flag_t;

/**
 * Exact device profiles applied only after VID, PID, and report-descriptor
 * SHA-256 all match. A profile may correct malformed usages, but it must not
 * infer controls that are not represented by the matched descriptor.
 */
typedef enum {
    GAMEPAD_HID_PROFILE_NONE = 0,
    GAMEPAD_HID_PROFILE_USB_GAMEPAD_0079_0011 = 1,
} gamepad_hid_profile_t;

/** One bounded input field extracted from a report descriptor. */
typedef struct {
    int32_t logical_min;
    int32_t logical_max;
    uint16_t usage_page;
    uint16_t usage;
    uint16_t usage_max;
    uint16_t bit_offset;
    uint8_t bit_size;
    uint8_t report_id;
    uint8_t map_target;
    uint8_t map_index;
    uint8_t flags;
    uint8_t reserved;
} gamepad_hid_field_t;

/** Expected input payload length for one HID Report ID. */
typedef struct {
    uint16_t payload_bits;
    uint16_t payload_bytes;
    uint8_t report_id;
    uint8_t reserved[3];
} gamepad_hid_report_t;

/**
 * Allocation-free parsed layout.
 *
 * Callers may inspect fields but should use gamepad_hid_set_field_mapping() to
 * change mappings so validation and capability reporting remain consistent.
 */
typedef struct {
    uint16_t version;
    uint16_t size;
    uint16_t field_count;
    uint8_t report_count;
    uint8_t uses_report_ids;
    uint8_t application_collection_count;
    uint8_t selected_application_usage;
    uint8_t reserved[2];
    gamepad_hid_report_t reports[GAMEPAD_HID_MAX_REPORTS];
    gamepad_hid_field_t fields[GAMEPAD_HID_MAX_FIELDS];
} gamepad_hid_layout_t;

/** Initialize a layout object before parsing or reuse. */
void gamepad_hid_layout_init(gamepad_hid_layout_t *layout);

/**
 * Parse the first top-level Joystick, Game Pad, or Multi-axis Application
 * Collection. Reports owned only by later application collections are ignored.
 *
 * The descriptor is untrusted. Parsing is allocation-free and rejects inputs
 * that exceed any public bound. On failure, `layout` is reset to an empty valid
 * object and contains no partially parsed fields.
 */
gamepad_status_t gamepad_hid_parse_descriptor(const uint8_t *descriptor,
                                               size_t descriptor_size,
                                               gamepad_hid_layout_t *layout);

/**
 * Override one generic field mapping for a device-specific profile.
 * `map_index` is used only for scalar GAMEPAD_HID_MAP_BUTTON fields and must
 * be below 64. Array fields may only be ignored or mapped as buttons; their
 * declared usage range selects the canonical button at decode time. Generic
 * button arrays support direct selectors (the logical range contains the usage
 * range, with any extra values treated as null when declared) and ordinal
 * selectors (logical and usage ranges have the same number of elements).
 */
gamepad_status_t gamepad_hid_set_field_mapping(gamepad_hid_layout_t *layout,
                                               size_t field_index,
                                               gamepad_hid_map_target_t target,
                                               uint8_t map_index);

/**
 * Apply a built-in profile selected by exact device and descriptor identity.
 *
 * A non-profiled VID/PID is not an error and leaves the generic layout
 * unchanged with `applied_profile` set to GAMEPAD_HID_PROFILE_NONE. A known
 * VID/PID whose descriptor hash or parsed layout does not match the profile is
 * rejected. All changes are transactional.
 */
gamepad_status_t gamepad_hid_apply_known_profile(
    uint16_t vendor_id,
    uint16_t product_id,
    const uint8_t descriptor_sha256[GAMEPAD_HID_DESCRIPTOR_SHA256_BYTES],
    gamepad_hid_layout_t *layout,
    gamepad_hid_profile_t *applied_profile);

/** Return a static diagnostic name for a profile value. */
const char *gamepad_hid_profile_name(gamepad_hid_profile_t profile);

/** Return canonical capabilities represented by the current field mappings. */
uint32_t gamepad_hid_capabilities(const gamepad_hid_layout_t *layout);

/**
 * Decode one raw HID input report into a connected canonical snapshot.
 *
 * Reports with IDs include the ID as byte zero. The byte count must exactly
 * match the descriptor-derived report size; short data and trailing data are
 * rejected. Decoding is transactional: an invalid report leaves `state`
 * unchanged.
 */
gamepad_status_t gamepad_hid_decode_report(const gamepad_hid_layout_t *layout,
                                           const uint8_t *report,
                                           size_t report_size,
                                           uint64_t timestamp_us,
                                           gamepad_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
