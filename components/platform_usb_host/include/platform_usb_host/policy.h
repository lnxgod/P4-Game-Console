#ifndef PLATFORM_USB_HOST_POLICY_H
#define PLATFORM_USB_HOST_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLATFORM_USB_FIXTURE_EVIDENCE_VERSION 1U
#define PLATFORM_USB_FIXTURE_EVIDENCE_ID_BYTES 64U
#define PLATFORM_USB_FIXTURE_EVIDENCE_SHA256_BYTES 65U

/** Portable status values used by the policy and lifecycle model. */
typedef enum {
    PLATFORM_USB_STATUS_OK = 0,
    PLATFORM_USB_STATUS_INVALID_ARGUMENT = -1,
    PLATFORM_USB_STATUS_INVALID_STATE = -2,
    PLATFORM_USB_STATUS_FIXTURE_REQUIRED = -3,
    PLATFORM_USB_STATUS_CLASS_BUSY = -4,
    PLATFORM_USB_STATUS_STALE_LEASE = -5,
    PLATFORM_USB_STATUS_LIMIT_EXCEEDED = -6,
} platform_usb_status_t;

/**
 * Evidence required before the P4 HS root port may be enabled.
 *
 * This structure is deliberately explicit. A generic "USB works" boolean is
 * not enough for the sink-wired CrowPanel connector. Every flag must be exactly
 * zero or one, every flag must be one for validation to pass, and evidence_id
 * must name a reviewed record rather than a controller model.
 */
typedef struct {
    uint16_t version;
    uint16_t size;
    uint16_t current_limit_ma;
    uint8_t externally_powered_vbus;
    uint8_t current_limited;
    uint8_t backfeed_blocked;
    uint8_t common_ground;
    uint8_t data_pair_direct;
    uint8_t source_role_compliant;
    uint8_t overcurrent_fault_visible;
    uint8_t board_path_reviewed;
    char evidence_id[PLATFORM_USB_FIXTURE_EVIDENCE_ID_BYTES];
    char evidence_sha256[PLATFORM_USB_FIXTURE_EVIDENCE_SHA256_BYTES];
} platform_usb_fixture_evidence_t;

/** Validate a complete powered-host fixture record without touching hardware. */
platform_usb_status_t platform_usb_fixture_evidence_validate(
    const platform_usb_fixture_evidence_t *evidence);

/** Return a static diagnostic name for a portable USB platform status. */
const char *platform_usb_status_name(platform_usb_status_t status);

#ifdef __cplusplus
}
#endif

#endif
