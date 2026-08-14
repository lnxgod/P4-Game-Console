#include "platform_usb_host/policy.h"

#include <ctype.h>
#include <string.h>

#define PLATFORM_USB_MIN_CURRENT_LIMIT_MA 100U
#define PLATFORM_USB_MAX_CURRENT_LIMIT_MA 500U

static bool boolean_byte_valid(uint8_t value)
{
    return value <= 1U;
}

static bool evidence_id_valid(const char *value, size_t capacity)
{
    const char *terminator = memchr(value, '\0', capacity);
    if (terminator == NULL || terminator == value) {
        return false;
    }

    for (const char *cursor = value; cursor < terminator; ++cursor) {
        const unsigned char character = (unsigned char)*cursor;
        if (!(isalnum(character) || character == '-' || character == '_' ||
              character == '.' || character == '/')) {
            return false;
        }
    }
    return true;
}

static bool evidence_sha256_valid(const char *value, size_t capacity)
{
    if (capacity != PLATFORM_USB_FIXTURE_EVIDENCE_SHA256_BYTES ||
        value[64] != '\0') {
        return false;
    }
    for (size_t index = 0; index < 64U; ++index) {
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

platform_usb_status_t platform_usb_fixture_evidence_validate(
    const platform_usb_fixture_evidence_t *evidence)
{
    if (evidence == NULL ||
        evidence->version != PLATFORM_USB_FIXTURE_EVIDENCE_VERSION ||
        evidence->size != sizeof(*evidence)) {
        return PLATFORM_USB_STATUS_INVALID_ARGUMENT;
    }

    const uint8_t flags[] = {
        evidence->externally_powered_vbus,
        evidence->current_limited,
        evidence->backfeed_blocked,
        evidence->common_ground,
        evidence->data_pair_direct,
        evidence->source_role_compliant,
        evidence->overcurrent_fault_visible,
        evidence->board_path_reviewed,
    };
    for (size_t index = 0; index < sizeof(flags) / sizeof(flags[0]); ++index) {
        if (!boolean_byte_valid(flags[index])) {
            return PLATFORM_USB_STATUS_INVALID_ARGUMENT;
        }
        if (flags[index] == 0U) {
            return PLATFORM_USB_STATUS_FIXTURE_REQUIRED;
        }
    }

    if (evidence->current_limit_ma < PLATFORM_USB_MIN_CURRENT_LIMIT_MA ||
        evidence->current_limit_ma > PLATFORM_USB_MAX_CURRENT_LIMIT_MA ||
        !evidence_id_valid(evidence->evidence_id,
                           sizeof(evidence->evidence_id)) ||
        !evidence_sha256_valid(evidence->evidence_sha256,
                               sizeof(evidence->evidence_sha256))) {
        return PLATFORM_USB_STATUS_FIXTURE_REQUIRED;
    }

    return PLATFORM_USB_STATUS_OK;
}

const char *platform_usb_status_name(platform_usb_status_t status)
{
    switch (status) {
    case PLATFORM_USB_STATUS_OK:
        return "ok";
    case PLATFORM_USB_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case PLATFORM_USB_STATUS_INVALID_STATE:
        return "invalid state";
    case PLATFORM_USB_STATUS_FIXTURE_REQUIRED:
        return "fixture evidence required";
    case PLATFORM_USB_STATUS_CLASS_BUSY:
        return "class lease busy";
    case PLATFORM_USB_STATUS_STALE_LEASE:
        return "stale class lease";
    case PLATFORM_USB_STATUS_LIMIT_EXCEEDED:
        return "limit exceeded";
    default:
        return "unknown status";
    }
}
