#!/bin/sh

set -eu

P4_TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
P4_SCRIPT_DIR=$(CDPATH= cd -- "$P4_TEST_DIR/.." && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

P4_EXPECTED_ROOT=$(CDPATH= cd -- "$P4_SCRIPT_DIR/.." && pwd)
if [ "$P4_PROJECT_ROOT" != "$P4_EXPECTED_ROOT" ]; then
    printf 'project root mismatch: expected %s, found %s\n' \
        "$P4_EXPECTED_ROOT" "$P4_PROJECT_ROOT" >&2
    exit 1
fi

p4_require_app bringup
p4_require_flash_authorized bringup app-flash
if p4_require_flash_authorized bringup flash >/dev/null 2>&1; then
    printf 'bringup project flash was authorized\n' >&2
    exit 1
fi
if p4_require_flash_authorized doom app-flash >/dev/null 2>&1; then
    printf 'build-only Doom app flash was authorized\n' >&2
    exit 1
fi
if p4_require_flash_authorized doom_embedded_gamepad app-flash >/dev/null 2>&1; then
    printf 'inactive E3 one-shot app flash was authorized\n' >&2
    exit 1
fi
if p4_require_flash_authorized doom_embedded_gamepad flash >/dev/null 2>&1; then
    printf 'E3 full-project flash was authorized\n' >&2
    exit 1
fi
if p4_require_flash_authorized gamepad_diag app-flash >/dev/null 2>&1; then
    printf 'inactive gamepad diagnostic app flash was authorized\n' >&2
    exit 1
fi
if p4_require_flash_authorized gamepad_diag flash >/dev/null 2>&1; then
    printf 'gamepad diagnostic full-project flash was authorized\n' >&2
    exit 1
fi
if p4_require_flash_authorized doom_embedded_touch_audio app-flash >/dev/null 2>&1; then
    printf 'E6 sound successor app flash was authorized\n' >&2
    exit 1
fi
if p4_require_flash_authorized doom_embedded_touch_audio flash >/dev/null 2>&1; then
    printf 'E6 sound successor full-project flash was authorized\n' >&2
    exit 1
fi
if p4_require_flash_authorized bringup not-a-flash-target >/dev/null 2>&1; then
    printf 'unknown flash target was authorized\n' >&2
    exit 1
fi
if p4_require_app '../bringup' >/dev/null 2>&1; then
    printf 'path traversal app name was accepted\n' >&2
    exit 1
fi
if p4_require_app missing-app >/dev/null 2>&1; then
    printf 'missing app was accepted\n' >&2
    exit 1
fi

if p4_require_port '' >/dev/null 2>&1; then
    printf 'empty serial port was accepted\n' >&2
    exit 1
fi
if p4_require_port "$P4_PROJECT_ROOT/README.md" >/dev/null 2>&1; then
    printf 'regular file was accepted as a serial port\n' >&2
    exit 1
fi

P4_ACTUAL_HASH=$(p4_sha256_file "$P4_PROJECT_ROOT/README.md")
P4_EXPECTED_HASH=$(python3 -c '
import hashlib, pathlib, sys
print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())
' "$P4_PROJECT_ROOT/README.md")
if [ "$P4_ACTUAL_HASH" != "$P4_EXPECTED_HASH" ]; then
    printf 'SHA-256 helper mismatch\n' >&2
    exit 1
fi

P4_SYNTHETIC_IDENTITY_OUTPUT='esptool.py v4.10
MAC: 02:00:00:00:00:01'
P4_ACTUAL_IDENTITY_HASH=$(printf '%s\n' "$P4_SYNTHETIC_IDENTITY_OUTPUT" | \
    p4_identity_hash_from_esptool_output)
P4_EXPECTED_IDENTITY_HASH=$(python3 -c '
import hashlib
print(hashlib.sha256(b"020000000001").hexdigest())
')
if [ "$P4_ACTUAL_IDENTITY_HASH" != "$P4_EXPECTED_IDENTITY_HASH" ]; then
    printf 'device identity hash helper mismatch\n' >&2
    exit 1
fi
P4_DUPLICATE_IDENTITY_HASH=$(printf '%s\nMAC: 02:00:00:00:00:01\n' \
    "$P4_SYNTHETIC_IDENTITY_OUTPUT" | p4_identity_hash_from_esptool_output)
if [ "$P4_DUPLICATE_IDENTITY_HASH" != "$P4_EXPECTED_IDENTITY_HASH" ]; then
    printf 'duplicate consistent device identities were not accepted\n' >&2
    exit 1
fi
if printf 'no device identifier here\n' | \
    p4_identity_hash_from_esptool_output >/dev/null 2>&1; then
    printf 'device identity parser accepted missing identity\n' >&2
    exit 1
fi
if printf 'MAC: 02:00:00:00:00:01\nMAC: 02:00:00:00:00:02\n' | \
    p4_identity_hash_from_esptool_output >/dev/null 2>&1; then
    printf 'conflicting device identities were accepted\n' >&2
    exit 1
fi

P4_FLASH_SIZE_BYTES=$(printf 'Detected flash size: 16MB\n' | \
    p4_flash_size_bytes_from_esptool_output)
if [ "$P4_FLASH_SIZE_BYTES" != 16777216 ]; then
    printf 'detected flash-size parser mismatch\n' >&2
    exit 1
fi
if printf 'no flash size here\n' | \
    p4_flash_size_bytes_from_esptool_output >/dev/null 2>&1; then
    printf 'detected flash-size parser accepted missing size\n' >&2
    exit 1
fi

P4_REDACTED=$(printf '%s\n' "$P4_SYNTHETIC_IDENTITY_OUTPUT" | \
    p4_redact_device_identifiers)
if printf '%s\n' "$P4_REDACTED" | grep -F '02:00:00:00:00:01' >/dev/null || \
   ! printf '%s\n' "$P4_REDACTED" | grep -F 'MAC: [redacted]' >/dev/null; then
    printf 'device identifier redaction failed\n' >&2
    exit 1
fi

printf 'project-env tests: PASS\n'
