#!/bin/sh

set -eu

P4_APP=bringup
P4_PORT=
P4_FLASH_TARGET=flash
P4_RESUME_READBACK=false
P4_DEFER_LAUNCH_FOR_CAPTURE=false
P4_LAUNCH_RECEIPT=
P4_GAMEPAD_ARM_TOKEN_FILE=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --app) P4_APP=${2:-}; shift 2 ;;
        --port) P4_PORT=${2:-}; shift 2 ;;
        --app-only) P4_FLASH_TARGET=app-flash; shift ;;
        --resume-readback) P4_RESUME_READBACK=true; shift ;;
        --defer-launch-for-capture) P4_DEFER_LAUNCH_FOR_CAPTURE=true; shift ;;
        --launch-receipt) P4_LAUNCH_RECEIPT=${2:-}; shift 2 ;;
        --arm-token-file) P4_GAMEPAD_ARM_TOKEN_FILE=${2:-}; shift 2 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

if [ "$P4_RESUME_READBACK" = true ] && [ "$P4_FLASH_TARGET" != app-flash ]; then
    printf 'Refusing resume: --resume-readback requires explicit --app-only scope.\n' >&2
    exit 2
fi
if [ "$P4_DEFER_LAUNCH_FOR_CAPTURE" = true ] && \
   { [ "$P4_FLASH_TARGET" != app-flash ] || \
     { [ "$P4_APP" != audio_direct_diag ] && \
       [ "$P4_APP" != gamepad_diag ]; }; }; then
    printf 'Refusing deferred capture launch: it is scoped only to audio_direct_diag or gamepad_diag --app-only.\n' >&2
    exit 2
fi
if [ "$P4_APP" = gamepad_diag ] && \
   [ "$P4_FLASH_TARGET" = app-flash ] && \
   [ "$P4_DEFER_LAUNCH_FOR_CAPTURE" != true ]; then
    printf 'Refusing gamepad_diag app-only execution without the reset-safe UART capture path.\n' >&2
    exit 2
fi
if [ "$P4_APP" = audio_direct_diag ] && \
   [ "$P4_FLASH_TARGET" = app-flash ] && \
   [ "$P4_DEFER_LAUNCH_FOR_CAPTURE" != true ]; then
    printf 'Refusing audio_direct_diag app-only execution without the synchronized capture path.\n' >&2
    exit 2
fi
if [ "$P4_DEFER_LAUNCH_FOR_CAPTURE" = true ] && \
   [ -z "$P4_LAUNCH_RECEIPT" ]; then
    printf 'Refusing deferred capture launch: --launch-receipt is required.\n' >&2
    exit 2
fi
if [ -n "$P4_LAUNCH_RECEIPT" ] && \
   [ "$P4_DEFER_LAUNCH_FOR_CAPTURE" != true ]; then
    printf 'Refusing launch receipt without --defer-launch-for-capture.\n' >&2
    exit 2
fi
if [ -n "$P4_GAMEPAD_ARM_TOKEN_FILE" ] && \
   { [ "$P4_APP" != gamepad_diag ] || \
     [ "$P4_FLASH_TARGET" != app-flash ] || \
     [ "$P4_DEFER_LAUNCH_FOR_CAPTURE" != true ]; }; then
    printf 'Refusing ARM token: it is scoped only to the exact gamepad_diag app-only deferred-capture route.\n' >&2
    exit 2
fi
if [ "$P4_APP" = gamepad_diag ] && \
   [ "$P4_FLASH_TARGET" = app-flash ] && \
   [ "$P4_DEFER_LAUNCH_FOR_CAPTURE" = true ] && \
   [ -z "$P4_GAMEPAD_ARM_TOKEN_FILE" ]; then
    printf 'Refusing gamepad diagnostic: --arm-token-file is required before host or serial access.\n' >&2
    exit 2
fi
if [ "$P4_RESUME_READBACK" = true ] && [ "$P4_APP" != wad_provisioner ]; then
    printf 'Refusing resume: --resume-readback is currently scoped only to wad_provisioner.\n' >&2
    exit 2
fi

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/app-readback.sh"

P4_VERIFIED_APP_DIR=
P4_D23_RESERVATION_ACTIVE=false
P4_D23_CAPTURE_TOOL=
P4_D23_STATE=
P4_D23_AUTH=
P4_E3_RESERVATION_ACTIVE=false
P4_E3_STATE_TOOL=
P4_E3_STATE=
P4_E3_AUTH=
P4_GAMEPAD_RESERVATION_ACTIVE=false
P4_GAMEPAD_STATE_TOOL=
P4_GAMEPAD_CAPTURE_TOOL=
P4_GAMEPAD_INSTALL_TOOL=
P4_GAMEPAD_STATE=
P4_GAMEPAD_AUTH=
P4_GAMEPAD_OWNER_TOKEN_FILE=
P4_GAMEPAD_RECOVERY_DIR=
P4_GAMEPAD_ARM_SECRET_FILE=
P4_GAMEPAD_ARM_TOKEN_SHA256=
P4_E5_INSTALL_TOOL=
P4_E5_AUTH=
p4_destroy_gamepad_arm_secret() {
    if [ -z "${P4_GAMEPAD_RECOVERY_ROOT:-}" ] || \
       [ -z "${P4_GAMEPAD_RECOVERY_DIR:-}" ] || \
       [ -z "${P4_GAMEPAD_ARM_SECRET_FILE:-}" ]; then
        return 0
    fi
    python3 -c '
import os, pathlib, stat, sys
root = pathlib.Path(sys.argv[1]).resolve(strict=True)
recovery_path = pathlib.Path(sys.argv[2])
secret_path = pathlib.Path(sys.argv[3])
recovery_info = recovery_path.lstat()
recovery = recovery_path.resolve(strict=True)
if not (stat.S_ISDIR(recovery_info.st_mode)
        and not stat.S_ISLNK(recovery_info.st_mode)
        and recovery_info.st_uid == os.getuid()
        and stat.S_IMODE(recovery_info.st_mode) == 0o700
        and recovery.parent == root
        and recovery.name.startswith("gamepad-diag-recovery.")):
    raise SystemExit("refusing to destroy ARM secret outside the exact recovery bundle")
if secret_path.resolve(strict=False).parent != recovery or secret_path.name != "arm-token":
    raise SystemExit("refusing an unexpected ARM secret path")
try:
    expected = secret_path.lstat()
except FileNotFoundError:
    raise SystemExit(0)
if not (stat.S_ISREG(expected.st_mode) and not stat.S_ISLNK(expected.st_mode)
        and expected.st_uid == os.getuid()
        and stat.S_IMODE(expected.st_mode) == 0o600
        and expected.st_size == 65):
    raise SystemExit("refusing to unlink a non-private ARM secret file")
descriptor = os.open(secret_path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
try:
    opened = os.fstat(descriptor)
    current = secret_path.lstat()
    if (opened.st_dev, opened.st_ino) != (expected.st_dev, expected.st_ino) or (
        current.st_dev, current.st_ino
    ) != (expected.st_dev, expected.st_ino):
        raise SystemExit("ARM secret changed before destruction")
    secret_path.unlink()
finally:
    os.close(descriptor)
directory = os.open(recovery, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
try:
    os.fsync(directory)
finally:
    os.close(directory)
' "$P4_GAMEPAD_RECOVERY_ROOT" "$P4_GAMEPAD_RECOVERY_DIR" \
        "$P4_GAMEPAD_ARM_SECRET_FILE"
}
p4_remove_verified_app() {
    if [ -z "${P4_VERIFIED_APP_DIR:-}" ]; then
        return 0
    fi
    P4_VERIFIED_APP_PARENT=$(dirname -- "$P4_VERIFIED_APP_DIR")
    P4_VERIFIED_APP_NAME=$(basename -- "$P4_VERIFIED_APP_DIR")
    case "$P4_VERIFIED_APP_NAME" in
        p4-verified-app.*) ;;
        *)
            printf 'Refusing to remove unexpected verified-app path: %s\n' \
                "$P4_VERIFIED_APP_DIR" >&2
            return 1
            ;;
    esac
    if [ "$P4_VERIFIED_APP_PARENT" != "${P4_VERIFIED_APP_TMP_ROOT:-}" ]; then
        printf 'Refusing to remove verified-app path outside its temporary root: %s\n' \
            "$P4_VERIFIED_APP_DIR" >&2
        return 1
    fi
    rm -rf -- "$P4_VERIFIED_APP_DIR"
    P4_VERIFIED_APP_DIR=
}
p4_cleanup_flash_temps() {
    if [ "$P4_D23_RESERVATION_ACTIVE" = true ]; then
        if ! python3 "$P4_D23_CAPTURE_TOOL" fail-reservation \
            --state "$P4_D23_STATE" --authorization "$P4_D23_AUTH" \
            --reason flash-path-failed; then
            printf 'Warning: unable to persist terminal D2.3 reservation failure.\n' >&2
        fi
        P4_D23_RESERVATION_ACTIVE=false
    fi
    if [ "$P4_E3_RESERVATION_ACTIVE" = true ]; then
        if ! python3 "$P4_E3_STATE_TOOL" fail \
            --state "$P4_E3_STATE" --authorization "$P4_E3_AUTH" \
            --detail flash-path-failed-before-verified-launch; then
            printf 'Warning: unable to persist terminal E3 one-shot failure.\n' >&2
        fi
        P4_E3_RESERVATION_ACTIVE=false
    fi
    if [ "$P4_GAMEPAD_RESERVATION_ACTIVE" = true ]; then
        if [ -n "${P4_GAMEPAD_OWNER_TOKEN_FILE:-}" ] && \
           [ -f "$P4_GAMEPAD_OWNER_TOKEN_FILE" ]; then
            if ! python3 "$P4_GAMEPAD_STATE_TOOL" fail \
                --state "$P4_GAMEPAD_STATE" \
                --owner-token-file "$P4_GAMEPAD_OWNER_TOKEN_FILE" \
                --detail flash-path-failed-or-interrupted; then
                printf 'Warning: unable to persist gamepad one-shot failure/recovery state.\n' >&2
            fi
        fi
        if ! p4_destroy_gamepad_arm_secret; then
            printf 'Warning: unable to destroy the staged gamepad ARM secret.\n' >&2
        fi
        P4_GAMEPAD_RESERVATION_ACTIVE=false
    fi
    p4_remove_readback
    p4_remove_verified_app
}
p4_handle_hup() {
    trap - EXIT
    trap '' HUP INT TERM
    p4_cleanup_flash_temps
    exit 129
}
p4_handle_int() {
    trap - EXIT
    trap '' HUP INT TERM
    p4_cleanup_flash_temps
    exit 130
}
p4_handle_term() {
    trap - EXIT
    trap '' HUP INT TERM
    p4_cleanup_flash_temps
    exit 143
}
trap p4_cleanup_flash_temps EXIT
trap p4_handle_hup HUP
trap p4_handle_int INT
trap p4_handle_term TERM

p4_require_app "$P4_APP"
p4_require_flash_authorized "$P4_APP" "$P4_FLASH_TARGET"
if [ "$P4_APP" = doom_embedded_gamepad ]; then
    P4_E3_STATE_TOOL="$P4_SCRIPT_DIR/doom-e3-one-shot-state.py"
    P4_E3_STATE="$P4_PROJECT_ROOT/hardware/local-state/doom-embedded-gamepad-e3-one-shot.json"
    P4_E3_AUTH="$P4_PROJECT_ROOT/hardware/evidence/doom-embedded-gamepad-e3-one-shot-authorization.json"
    python3 "$P4_E3_STATE_TOOL" check-available \
        --state "$P4_E3_STATE" --authorization "$P4_E3_AUTH"
fi
if [ "$P4_APP" = gamepad_diag ]; then
    P4_GAMEPAD_STATE_TOOL="$P4_SCRIPT_DIR/gamepad-diag-one-shot-state.py"
    P4_GAMEPAD_CAPTURE_TOOL="$P4_SCRIPT_DIR/gamepad-diag-capture.py"
    P4_GAMEPAD_INSTALL_TOOL="$P4_SCRIPT_DIR/gamepad-diag-install.py"
    P4_GAMEPAD_STATE="$P4_PROJECT_ROOT/hardware/local-state/gamepad-diag-d1-one-shot.json"
    P4_GAMEPAD_AUTH="$P4_PROJECT_ROOT/hardware/evidence/gamepad-diag-d1-one-shot-authorization.json"
    python3 "$P4_GAMEPAD_CAPTURE_TOOL" check-receipt-path \
        --receipt "$P4_LAUNCH_RECEIPT"
    python3 "$P4_GAMEPAD_STATE_TOOL" check-available \
        --state "$P4_GAMEPAD_STATE" \
        --authorization "$P4_GAMEPAD_AUTH" \
        --project-root "$P4_PROJECT_ROOT"
    P4_GAMEPAD_ARM_TOKEN_SHA256=$(python3 "$P4_GAMEPAD_STATE_TOOL" \
        check-arm-secret \
        --path "$P4_GAMEPAD_ARM_TOKEN_FILE" \
        --authorization "$P4_GAMEPAD_AUTH" \
        --project-root "$P4_PROJECT_ROOT")
fi
if [ "$P4_APP" = doom_embedded_touch_audio ]; then
    P4_E5_INSTALL_TOOL="$P4_SCRIPT_DIR/doom-e5-install.py"
    P4_E5_AUTH="$P4_PROJECT_ROOT/hardware/evidence/doom-embedded-touch-audio-e5-persistent-demo-authorization.json"
fi
if [ "$P4_APP" != gamepad_diag ]; then
    p4_require_port "$P4_PORT"
fi
p4_activate_idf
python3 "$P4_SCRIPT_DIR/verify-metadata.py" --flash-preflight >/dev/null

# The reviewed device profile supplies identity and capacity independently of
# optional recovery images. Flashing never opens a firmware backup/manifest.
P4_DEVICE_PROFILE="$P4_PROJECT_ROOT/hardware/board-profile.json"
P4_DEVICE_FLASH_BYTES=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["measured"]["flash_bytes"])' "$P4_DEVICE_PROFILE")
P4_DEVICE_IDENTITY_EXPECTED=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["device_identity"]["sha256"])' "$P4_DEVICE_PROFILE")
P4_DEVICE_REVISION_EXPECTED=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["measured"]["chip_revision"])' "$P4_DEVICE_PROFILE")

P4_EXPECTED_FLASH_LABEL=$(python3 -c '
import sys
size = int(sys.argv[1])
unit = 1024 * 1024
if size <= 0 or size % unit:
    raise SystemExit("profile flash size is not a positive whole number of MiB")
print(f"{size // unit}MB")
' "$P4_DEVICE_FLASH_BYTES")
if [ "$P4_APP" = audio_direct_diag ]; then
    P4_D23_CAPTURE_TOOL="$P4_SCRIPT_DIR/capture-audio-direct-diag.py"
    P4_D23_STATE="$P4_PROJECT_ROOT/hardware/local-state/audio-direct-d23-attempt2.json"
    P4_D23_AUTH="$P4_PROJECT_ROOT/hardware/evidence/audio-direct-diag-d23-one-shot-authorization-attempt2.json"
    P4_D23_AUTH_OFFSET=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["offset"])' \
        "$P4_D23_AUTH")
    P4_D23_AUTH_BYTES=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["bytes"])' \
        "$P4_D23_AUTH")
    P4_D23_AUTH_HASH=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["sha256"])' \
        "$P4_D23_AUTH")
    P4_D23_PREFLIGHT_RUN=1
    while [ "$P4_D23_PREFLIGHT_RUN" -le 3 ]; do
        printf 'D2.3 host preflight run %s/3\n' "$P4_D23_PREFLIGHT_RUN"
        python3 "$P4_D23_CAPTURE_TOOL" preflight-host --port "$P4_PORT"
        P4_D23_PREFLIGHT_RUN=$((P4_D23_PREFLIGHT_RUN + 1))
    done
    python3 "$P4_D23_CAPTURE_TOOL" check-issuable \
        --state "$P4_D23_STATE" --authorization "$P4_D23_AUTH"
    P4_D23_RESERVATION_ACTIVE=true
    python3 "$P4_D23_CAPTURE_TOOL" reserve \
        --state "$P4_D23_STATE" --receipt "$P4_LAUNCH_RECEIPT" \
        --authorization "$P4_D23_AUTH" \
        --device-identity-sha256 "$P4_DEVICE_IDENTITY_EXPECTED" \
        --offset "$P4_D23_AUTH_OFFSET" --bytes "$P4_D23_AUTH_BYTES" \
        --sha256 "$P4_D23_AUTH_HASH"
fi
if [ "$P4_APP" = gamepad_diag ] || \
   [ "$P4_APP" = doom_embedded_touch_audio ]; then
    # The exclusive installer proves this exact identity on its one retained
    # UART descriptor. The expected value is used here only to reserve the
    # already policy-bound ledger; it is not a claim about a live device.
    P4_DEVICE_IDENTITY_ACTUAL=$P4_DEVICE_IDENTITY_EXPECTED
else
case "$P4_APP" in
    audio_direct_diag|sd_format_diag|wad_provisioner|doom_embedded|doom_embedded_gamepad|audio_diag)
        # A prior copy of a deferred-launch image may already be installed.
        # Keep all preflight probes in the ROM loader so it cannot start early.
        P4_PROBE_AFTER=no_reset
        ;;
    *) P4_PROBE_AFTER=hard_reset ;;
esac
P4_PROBE_OUTPUT=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
    --after "$P4_PROBE_AFTER" flash_id 2>&1) || {
    printf '%s\n' "$P4_PROBE_OUTPUT" | p4_redact_device_identifiers >&2
    printf 'Refusing to flash: live ESP32-P4 probe failed.\n' >&2
    exit 1
}
printf '%s\n' "$P4_PROBE_OUTPUT" | p4_redact_device_identifiers
if ! printf '%s\n' "$P4_PROBE_OUTPUT" | grep -F "Detected flash size: $P4_EXPECTED_FLASH_LABEL" >/dev/null; then
    printf 'Refusing to flash: live flash size does not match reviewed profile (%s).\n' \
        "$P4_EXPECTED_FLASH_LABEL" >&2
    exit 1
fi

if ! printf '%s\n' "$P4_PROBE_OUTPUT" | grep -F "(revision $P4_DEVICE_REVISION_EXPECTED)" >/dev/null; then
    printf 'Refusing to flash: live chip revision does not match reviewed profile.\n' >&2
    exit 1
fi

P4_DEVICE_IDENTITY_ACTUAL=$(p4_read_device_identity_hash "$P4_PORT" "$P4_PROBE_AFTER")
if [ "$P4_DEVICE_IDENTITY_ACTUAL" != "$P4_DEVICE_IDENTITY_EXPECTED" ]; then
    printf 'Refusing to flash: connected board identity does not match the reviewed device profile.\n' >&2
    exit 1
fi

P4_SECURITY_OUTPUT=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
    --after "$P4_PROBE_AFTER" get_security_info 2>&1) || {
    printf '%s\n' "$P4_SECURITY_OUTPUT" | p4_redact_device_identifiers >&2
    printf 'Refusing to flash: unable to read live security state.\n' >&2
    exit 1
}
printf '%s\n' "$P4_SECURITY_OUTPUT" | p4_redact_device_identifiers
if ! printf '%s\n' "$P4_SECURITY_OUTPUT" | grep -F 'Secure Boot: Disabled' >/dev/null || \
   ! printf '%s\n' "$P4_SECURITY_OUTPUT" | grep -F 'Flash Encryption: Disabled' >/dev/null; then
    printf 'Refusing to flash: secure boot or flash encryption is enabled; this unsigned image is not safe for that device.\n' >&2
    exit 1
fi
fi

if [ "$P4_APP" = gamepad_diag ]; then
    # Only the public SHA-256 digest enters the active build environment. The
    # private token stays in its exact 0600 file and is moved into the durable
    # recovery bundle only after the built artifact passes its verifier.
    P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256=$P4_GAMEPAD_ARM_TOKEN_SHA256
    export P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256
    "$P4_SCRIPT_DIR/build.sh" "$P4_APP"
    unset P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256
elif [ "$P4_APP" = usb_host_diag ]; then
    "$P4_SCRIPT_DIR/build-waveshare-usb-host-diag.sh"
else
    "$P4_SCRIPT_DIR/build.sh" "$P4_APP"
fi
if [ "$P4_APP" = usb_host_diag ]; then
    P4_BUILD_DIR="$P4_PROJECT_ROOT/apps/$P4_APP/build-waveshare-landscape"
else
    P4_BUILD_DIR="$P4_PROJECT_ROOT/apps/$P4_APP/build"
fi
P4_BUILT_APP_OFFSET=$(python3 -c '
import json, pathlib, sys
args = pathlib.Path(sys.argv[1]) / "flasher_args.json"
print(json.loads(args.read_text())["app"]["offset"])
' "$P4_BUILD_DIR")
P4_BUILT_APP_REL=$(python3 -c '
import json, pathlib, sys
args = pathlib.Path(sys.argv[1]) / "flasher_args.json"
print(json.loads(args.read_text())["app"]["file"])
' "$P4_BUILD_DIR")
P4_BUILT_APP_PATH="$P4_BUILD_DIR/$P4_BUILT_APP_REL"
if [ ! -f "$P4_BUILT_APP_PATH" ]; then
    printf 'Refusing to flash: built application binary is missing at %s\n' \
        "$P4_BUILT_APP_PATH" >&2
    exit 1
fi
if [ "$P4_APP" = display_diag ]; then
    python3 "$P4_SCRIPT_DIR/verify-display-diag.py" "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
if [ "$P4_APP" = framebuffer_diag ]; then
    python3 "$P4_SCRIPT_DIR/verify-framebuffer-diag.py" "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
if [ "$P4_APP" = storage_diag ]; then
    python3 "$P4_SCRIPT_DIR/verify-storage-diag.py" "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
if [ "$P4_APP" = wad_provisioner ]; then
    python3 "$P4_SCRIPT_DIR/verify-wad-provisioner.py" \
        "$P4_BUILD_DIR" "$P4_FLASH_TARGET" \
        "$P4_PROJECT_ROOT/local-data/doom/doom1.wad" \
        "$P4_PROJECT_ROOT/apps/wad_provisioner/partitions.csv"
fi
if [ "$P4_APP" = sd_format_diag ]; then
    python3 "$P4_SCRIPT_DIR/verify-sd-format-diag.py" \
        "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
if [ "$P4_APP" = doom_runtime ]; then
    python3 "$P4_SCRIPT_DIR/verify-doom-runtime.py" \
        "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
if [ "$P4_APP" = doom_embedded ]; then
    python3 "$P4_SCRIPT_DIR/verify-doom-embedded.py" \
        "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
if [ "$P4_APP" = doom_embedded_gamepad ]; then
    python3 "$P4_SCRIPT_DIR/verify-doom-embedded-gamepad.py" \
        "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
if [ "$P4_APP" = doom_embedded_touch_audio ]; then
    python3 "$P4_SCRIPT_DIR/verify-doom-embedded-touch-audio.py" \
        "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
if [ "$P4_APP" = gamepad_diag ]; then
    python3 "$P4_SCRIPT_DIR/verify-gamepad-diag.py" \
        "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
P4_VERIFIED_DIRECT_WRITE=false
if [ "$P4_APP" = doom_embedded_touch_audio ]; then
    if [ "$P4_FLASH_TARGET" != app-flash ]; then
        printf 'Refusing E5 write: only the exact app-only touch-only route is authorized.\n' >&2
        exit 1
    fi
    P4_E5_EXPECTED_OFFSET=0x10000
    P4_E5_EXPECTED_BYTES=4898400
    P4_E5_EXPECTED_HASH=68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8
    if [ "$P4_BUILT_APP_OFFSET" != "$P4_E5_EXPECTED_OFFSET" ]; then
        printf 'Refusing E5 write: application offset is not exact.\n' >&2
        exit 1
    fi
    P4_VERIFIED_APP_EXPECTED_BYTES=$P4_E5_EXPECTED_BYTES
    P4_VERIFIED_APP_EXPECTED_HASH=$P4_E5_EXPECTED_HASH
    P4_VERIFIED_APP_TMP_ROOT=${TMPDIR:-/tmp}
    P4_VERIFIED_APP_TMP_ROOT=${P4_VERIFIED_APP_TMP_ROOT%/}
    if [ -z "$P4_VERIFIED_APP_TMP_ROOT" ] || \
       [ ! -d "$P4_VERIFIED_APP_TMP_ROOT" ]; then
        printf 'Verified-app temporary root is unavailable: %s\n' \
            "${P4_VERIFIED_APP_TMP_ROOT:-<empty>}" >&2
        exit 1
    fi
    P4_VERIFIED_APP_DIR=$(mktemp -d \
        "$P4_VERIFIED_APP_TMP_ROOT/p4-verified-app.XXXXXX")
    chmod 700 "$P4_VERIFIED_APP_DIR"
    P4_VERIFIED_APP_PATH="$P4_VERIFIED_APP_DIR/application.bin"
    cp -- "$P4_BUILT_APP_PATH" "$P4_VERIFIED_APP_PATH"
    chmod 400 "$P4_VERIFIED_APP_PATH"
    P4_VERIFIED_APP_ACTUAL_BYTES=$(wc -c < "$P4_VERIFIED_APP_PATH" | tr -d ' ')
    P4_VERIFIED_APP_ACTUAL_HASH=$(p4_sha256_file "$P4_VERIFIED_APP_PATH")
    if [ "$P4_VERIFIED_APP_ACTUAL_BYTES" != "$P4_E5_EXPECTED_BYTES" ] || \
       [ "$P4_VERIFIED_APP_ACTUAL_HASH" != "$P4_E5_EXPECTED_HASH" ]; then
        printf 'Refusing E5 write: sealed application snapshot is not exact.\n' >&2
        exit 1
    fi
    P4_BUILT_APP_PATH="$P4_VERIFIED_APP_PATH"
    P4_VERIFIED_DIRECT_WRITE=true
    printf 'Exact E5 touch-only application snapshot sealed: bytes=%s sha256=%s\n' \
        "$P4_VERIFIED_APP_ACTUAL_BYTES" "$P4_VERIFIED_APP_ACTUAL_HASH"
fi
if [ "$P4_APP" = gamepad_diag ]; then
    if [ "$P4_FLASH_TARGET" != app-flash ] || \
       [ "$P4_DEFER_LAUNCH_FOR_CAPTURE" != true ]; then
        printf 'Refusing gamepad diagnostic: exact app-only deferred-capture route required.\n' >&2
        exit 1
    fi
    P4_GAMEPAD_EXPECTED_OFFSET=0x10000
    P4_GAMEPAD_EXPECTED_BYTES=301328
    P4_GAMEPAD_EXPECTED_HASH=ad197d70eca589cb104bb654e5d35178dbb2cefd8fa60cdd184cd8806b1350fb
    P4_GAMEPAD_AUTH_OFFSET=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["offset"])' \
        "$P4_GAMEPAD_AUTH")
    P4_GAMEPAD_AUTH_BYTES=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["bytes"])' \
        "$P4_GAMEPAD_AUTH")
    P4_GAMEPAD_AUTH_HASH=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["sha256"])' \
        "$P4_GAMEPAD_AUTH")
    if [ "$P4_GAMEPAD_AUTH_OFFSET" != "$P4_GAMEPAD_EXPECTED_OFFSET" ] || \
       [ "$P4_GAMEPAD_AUTH_BYTES" != "$P4_GAMEPAD_EXPECTED_BYTES" ] || \
       [ "$P4_GAMEPAD_AUTH_HASH" != "$P4_GAMEPAD_EXPECTED_HASH" ] || \
       [ "$P4_BUILT_APP_OFFSET" != "$P4_GAMEPAD_EXPECTED_OFFSET" ]; then
        printf 'Refusing gamepad diagnostic: literal authorization identity mismatch.\n' >&2
        exit 1
    fi
    P4_VERIFIED_APP_EXPECTED_BYTES=$P4_GAMEPAD_EXPECTED_BYTES
    P4_VERIFIED_APP_EXPECTED_HASH=$P4_GAMEPAD_EXPECTED_HASH
    P4_VERIFIED_APP_TMP_ROOT=${TMPDIR:-/tmp}
    P4_VERIFIED_APP_TMP_ROOT=${P4_VERIFIED_APP_TMP_ROOT%/}
    if [ -z "$P4_VERIFIED_APP_TMP_ROOT" ] || \
       [ ! -d "$P4_VERIFIED_APP_TMP_ROOT" ]; then
        printf 'Verified-app temporary root is unavailable: %s\n' \
            "${P4_VERIFIED_APP_TMP_ROOT:-<empty>}" >&2
        exit 1
    fi
    P4_VERIFIED_APP_DIR=$(mktemp -d \
        "$P4_VERIFIED_APP_TMP_ROOT/p4-verified-app.XXXXXX")
    chmod 700 "$P4_VERIFIED_APP_DIR"
    P4_VERIFIED_APP_PATH="$P4_VERIFIED_APP_DIR/application.bin"
    cp -- "$P4_BUILT_APP_PATH" "$P4_VERIFIED_APP_PATH"
    chmod 400 "$P4_VERIFIED_APP_PATH"
    P4_VERIFIED_APP_ACTUAL_BYTES=$(wc -c < "$P4_VERIFIED_APP_PATH" | tr -d ' ')
    P4_VERIFIED_APP_ACTUAL_HASH=$(p4_sha256_file "$P4_VERIFIED_APP_PATH")
    if [ "$P4_VERIFIED_APP_ACTUAL_BYTES" != "$P4_GAMEPAD_EXPECTED_BYTES" ] || \
       [ "$P4_VERIFIED_APP_ACTUAL_HASH" != "$P4_GAMEPAD_EXPECTED_HASH" ]; then
        printf 'Refusing gamepad diagnostic: sealed snapshot is not exact.\n' >&2
        exit 1
    fi
    P4_BUILT_APP_PATH="$P4_VERIFIED_APP_PATH"

    # Recovery material is deliberately outside the disposable verified-app
    # directory. Once the reservation exists, neither normal cleanup nor a
    # signal may remove the owner token or the pre-install flash snapshots.
    P4_GAMEPAD_RECOVERY_ROOT="$P4_PROJECT_ROOT/hardware/local-state"
    if [ ! -d "$P4_GAMEPAD_RECOVERY_ROOT" ] || \
       [ -L "$P4_GAMEPAD_RECOVERY_ROOT" ]; then
        printf 'Refusing gamepad diagnostic: durable local-state root is unavailable or unsafe.\n' >&2
        exit 1
    fi
    P4_GAMEPAD_PREVIOUS_UMASK=$(umask)
    umask 077
    P4_GAMEPAD_RECOVERY_DIR=$(mktemp -d \
        "$P4_GAMEPAD_RECOVERY_ROOT/gamepad-diag-recovery.XXXXXX")
    chmod 700 "$P4_GAMEPAD_RECOVERY_DIR"
    python3 -c '
import os, pathlib, stat, sys
directory = pathlib.Path(sys.argv[1])
parent = pathlib.Path(sys.argv[2]).resolve(strict=True)
info = directory.lstat()
resolved = directory.resolve(strict=True)
if not (stat.S_ISDIR(info.st_mode) and not stat.S_ISLNK(info.st_mode)
        and info.st_uid == os.getuid() and stat.S_IMODE(info.st_mode) == 0o700
        and resolved.parent == parent and resolved.name.startswith("gamepad-diag-recovery.")):
    raise SystemExit("new recovery directory is not the exact private local-state child")
for path in (resolved, parent):
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
' "$P4_GAMEPAD_RECOVERY_DIR" "$P4_GAMEPAD_RECOVERY_ROOT"

    P4_GAMEPAD_OWNER_TOKEN_FILE="$P4_GAMEPAD_RECOVERY_DIR/owner-token"
    P4_GAMEPAD_ARM_SECRET_FILE="$P4_GAMEPAD_RECOVERY_DIR/arm-token"
    python3 "$P4_GAMEPAD_STATE_TOOL" new-owner-token \
        --path "$P4_GAMEPAD_OWNER_TOKEN_FILE"
    # Arm cleanup before the atomic O_EXCL reservation. A pre-write interruption
    # becomes terminal-safe; once a write is attempted, the state helper leaves
    # a nonterminal restore-required ledger and this bundle remains available.
    P4_GAMEPAD_RESERVATION_ACTIVE=true
    python3 "$P4_GAMEPAD_STATE_TOOL" reserve \
        --state "$P4_GAMEPAD_STATE" \
        --authorization "$P4_GAMEPAD_AUTH" \
        --project-root "$P4_PROJECT_ROOT" \
        --owner-token-file "$P4_GAMEPAD_OWNER_TOKEN_FILE" \
        --device-identity-sha256 "$P4_DEVICE_IDENTITY_ACTUAL" \
        --offset "$P4_GAMEPAD_EXPECTED_OFFSET" \
        --bytes "$P4_GAMEPAD_EXPECTED_BYTES" \
        --sha256 "$P4_GAMEPAD_EXPECTED_HASH"

    umask "$P4_GAMEPAD_PREVIOUS_UMASK"
    P4_VERIFIED_DIRECT_WRITE=true
    printf 'Exact gamepad diagnostic sealed and install reservation durable: bytes=%s sha256=%s recovery=%s\n' \
        "$P4_VERIFIED_APP_ACTUAL_BYTES" "$P4_VERIFIED_APP_ACTUAL_HASH" \
        "$P4_GAMEPAD_RECOVERY_DIR"
fi
if [ "$P4_APP" = audio_diag ]; then
    python3 "$P4_SCRIPT_DIR/verify-audio-diag.py" \
        "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
if [ "$P4_APP" = audio_direct_diag ]; then
    python3 "$P4_SCRIPT_DIR/verify-audio-direct-diag.py" \
        "$P4_BUILD_DIR" "$P4_FLASH_TARGET"
fi
if [ "$P4_FLASH_TARGET" = app-flash ] && \
   [ "$P4_APP" != gamepad_diag ] && \
   [ "$P4_APP" != doom_embedded_touch_audio ]; then
    # Exclusive installers validate their layout on their retained UART handle.
    # Other app-only routes inspect only the small live partition table.
    P4_LAYOUT_APP_BYTES=$(wc -c < "$P4_BUILT_APP_PATH" | tr -d ' ')
    python3 "$P4_SCRIPT_DIR/verify-live-app-layout.py" \
        --port "$P4_PORT" --offset "$P4_BUILT_APP_OFFSET" \
        --bytes "$P4_LAYOUT_APP_BYTES" --flash-bytes "$P4_DEVICE_FLASH_BYTES"
fi

if [ "$P4_APP" = audio_direct_diag ]; then
    P4_AUDIO_DIRECT_AUTH="$P4_PROJECT_ROOT/hardware/evidence/audio-direct-diag-d23-one-shot-authorization-attempt2.json"
    P4_VERIFIED_APP_EXPECTED_BYTES=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["bytes"])' \
        "$P4_AUDIO_DIRECT_AUTH")
    P4_VERIFIED_APP_EXPECTED_HASH=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["sha256"])' \
        "$P4_AUDIO_DIRECT_AUTH")
    P4_VERIFIED_APP_TMP_ROOT=${TMPDIR:-/tmp}
    P4_VERIFIED_APP_TMP_ROOT=${P4_VERIFIED_APP_TMP_ROOT%/}
    if [ -z "$P4_VERIFIED_APP_TMP_ROOT" ] || \
       [ ! -d "$P4_VERIFIED_APP_TMP_ROOT" ]; then
        printf 'Verified-app temporary root is unavailable: %s\n' \
            "${P4_VERIFIED_APP_TMP_ROOT:-<empty>}" >&2
        exit 1
    fi
    P4_VERIFIED_APP_DIR=$(mktemp -d \
        "$P4_VERIFIED_APP_TMP_ROOT/p4-verified-app.XXXXXX")
    chmod 700 "$P4_VERIFIED_APP_DIR"
    P4_VERIFIED_APP_PATH="$P4_VERIFIED_APP_DIR/application.bin"
    cp -- "$P4_BUILT_APP_PATH" "$P4_VERIFIED_APP_PATH"
    chmod 400 "$P4_VERIFIED_APP_PATH"
    P4_VERIFIED_APP_ACTUAL_BYTES=$(wc -c < "$P4_VERIFIED_APP_PATH" | tr -d ' ')
    P4_VERIFIED_APP_ACTUAL_HASH=$(p4_sha256_file "$P4_VERIFIED_APP_PATH")
    if [ "$P4_VERIFIED_APP_ACTUAL_BYTES" != "$P4_VERIFIED_APP_EXPECTED_BYTES" ] || \
       [ "$P4_VERIFIED_APP_ACTUAL_HASH" != "$P4_VERIFIED_APP_EXPECTED_HASH" ]; then
        printf 'Refusing direct write: verified application snapshot does not match its authorization.\n' >&2
        exit 1
    fi
    P4_BUILT_APP_PATH="$P4_VERIFIED_APP_PATH"
    P4_VERIFIED_DIRECT_WRITE=true
    printf 'Exact application snapshot sealed: bytes=%s sha256=%s\n' \
        "$P4_VERIFIED_APP_ACTUAL_BYTES" "$P4_VERIFIED_APP_ACTUAL_HASH"
fi
if [ "$P4_APP" = doom_embedded_gamepad ]; then
    if [ "$P4_FLASH_TARGET" != app-flash ]; then
        printf 'Refusing E3 write: only the exact app-only route is authorized.\n' >&2
        exit 1
    fi
    P4_E3_EXPECTED_OFFSET=0x10000
    P4_E3_EXPECTED_BYTES=4927824
    P4_E3_EXPECTED_HASH=32814a97b72b3b227e2ace02393222178ceebf02d18a4f0b35bead1f4584ea24
    P4_E3_AUTH_OFFSET=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["offset"])' \
        "$P4_E3_AUTH")
    P4_E3_AUTH_BYTES=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["bytes"])' \
        "$P4_E3_AUTH")
    P4_E3_AUTH_HASH=$(python3 -c \
        'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["sha256"])' \
        "$P4_E3_AUTH")
    if [ "$P4_E3_AUTH_OFFSET" != "$P4_E3_EXPECTED_OFFSET" ] || \
       [ "$P4_E3_AUTH_BYTES" != "$P4_E3_EXPECTED_BYTES" ] || \
       [ "$P4_E3_AUTH_HASH" != "$P4_E3_EXPECTED_HASH" ] || \
       [ "$P4_BUILT_APP_OFFSET" != "$P4_E3_EXPECTED_OFFSET" ]; then
        printf 'Refusing E3 write: literal authorization identity mismatch.\n' >&2
        exit 1
    fi
    P4_VERIFIED_APP_EXPECTED_BYTES=$P4_E3_EXPECTED_BYTES
    P4_VERIFIED_APP_EXPECTED_HASH=$P4_E3_EXPECTED_HASH
    P4_VERIFIED_APP_TMP_ROOT=${TMPDIR:-/tmp}
    P4_VERIFIED_APP_TMP_ROOT=${P4_VERIFIED_APP_TMP_ROOT%/}
    if [ -z "$P4_VERIFIED_APP_TMP_ROOT" ] || \
       [ ! -d "$P4_VERIFIED_APP_TMP_ROOT" ]; then
        printf 'Verified-app temporary root is unavailable: %s\n' \
            "${P4_VERIFIED_APP_TMP_ROOT:-<empty>}" >&2
        exit 1
    fi
    P4_VERIFIED_APP_DIR=$(mktemp -d \
        "$P4_VERIFIED_APP_TMP_ROOT/p4-verified-app.XXXXXX")
    chmod 700 "$P4_VERIFIED_APP_DIR"
    P4_VERIFIED_APP_PATH="$P4_VERIFIED_APP_DIR/application.bin"
    cp -- "$P4_BUILT_APP_PATH" "$P4_VERIFIED_APP_PATH"
    chmod 400 "$P4_VERIFIED_APP_PATH"
    P4_VERIFIED_APP_ACTUAL_BYTES=$(wc -c < "$P4_VERIFIED_APP_PATH" | tr -d ' ')
    P4_VERIFIED_APP_ACTUAL_HASH=$(p4_sha256_file "$P4_VERIFIED_APP_PATH")
    if [ "$P4_VERIFIED_APP_ACTUAL_BYTES" != "$P4_E3_EXPECTED_BYTES" ] || \
       [ "$P4_VERIFIED_APP_ACTUAL_HASH" != "$P4_E3_EXPECTED_HASH" ]; then
        printf 'Refusing E3 write: sealed application snapshot is not exact.\n' >&2
        exit 1
    fi
    P4_BUILT_APP_PATH="$P4_VERIFIED_APP_PATH"
    # Arm cleanup before the helper creates durable state. If interrupted
    # between the atomic create and shell return, the trap still terminates it.
    P4_E3_RESERVATION_ACTIVE=true
    python3 "$P4_E3_STATE_TOOL" reserve \
        --state "$P4_E3_STATE" --authorization "$P4_E3_AUTH" \
        --device-identity-sha256 "$P4_DEVICE_IDENTITY_ACTUAL" \
        --offset "$P4_E3_EXPECTED_OFFSET" --bytes "$P4_E3_EXPECTED_BYTES" \
        --sha256 "$P4_E3_EXPECTED_HASH"
    P4_VERIFIED_DIRECT_WRITE=true
    printf 'Exact E3 application snapshot sealed and reserved: bytes=%s sha256=%s\n' \
        "$P4_VERIFIED_APP_ACTUAL_BYTES" "$P4_VERIFIED_APP_ACTUAL_HASH"
fi

if [ "$P4_RESUME_READBACK" = true ]; then
    printf 'Resume mode: application write skipped; verifying existing flash before deferred launch.\n'
elif [ "$P4_VERIFIED_DIRECT_WRITE" = true ]; then
    P4_VERIFIED_APP_PREWRITE_BYTES=$(wc -c < "$P4_BUILT_APP_PATH" | tr -d ' ')
    P4_VERIFIED_APP_PREWRITE_HASH=$(p4_sha256_file "$P4_BUILT_APP_PATH")
    if [ "$P4_VERIFIED_APP_PREWRITE_BYTES" != "$P4_VERIFIED_APP_EXPECTED_BYTES" ] || \
       [ "$P4_VERIFIED_APP_PREWRITE_HASH" != "$P4_VERIFIED_APP_EXPECTED_HASH" ]; then
        printf 'Refusing direct write: sealed application snapshot changed before write.\n' >&2
        exit 1
    fi
    if [ "$P4_APP" = gamepad_diag ]; then
        p4_require_port "$P4_PORT"
        P4_GAMEPAD_INSTALL_RESULT="$P4_VERIFIED_APP_DIR/install-result.json"
        # One Python process owns one exclusive UART descriptor from its first
        # target probe through recovery sealing, durable mark, write, exact
        # readback, installed transition, and receipt issuance. No esptool CLI
        # subprocess or reopened port exists inside this transaction.
        python3 "$P4_GAMEPAD_INSTALL_TOOL" \
            --port "$P4_PORT" \
            --artifact "$P4_BUILT_APP_PATH" \
            --arm-token-file "$P4_GAMEPAD_ARM_TOKEN_FILE" \
            --recovery-dir "$P4_GAMEPAD_RECOVERY_DIR" \
            --state "$P4_GAMEPAD_STATE" \
            --authorization "$P4_GAMEPAD_AUTH" \
            --project-root "$P4_PROJECT_ROOT" \
            --owner-token-file "$P4_GAMEPAD_OWNER_TOKEN_FILE" \
            --receipt "$P4_LAUNCH_RECEIPT" \
            --verifier "$P4_SCRIPT_DIR/verify-gamepad-diag.py" \
            --result "$P4_GAMEPAD_INSTALL_RESULT"
        P4_GAMEPAD_INSTALL_SUMMARY=$(python3 -c '
import json, pathlib, sys
value = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
expected = (sys.argv[2], int(sys.argv[3]), sys.argv[4])
if not (
    value.get("offset") == expected[0]
    and value.get("bytes") == expected[1]
    and value.get("sha256") == expected[2]
    and value.get("readback_bytes") == expected[1]
    and value.get("readback_sha256") == expected[2]
    and value.get("same_uart_handle") is True
    and value.get("exclusive_uart") is True
    and value.get("download_reset_count") == 1
    and value.get("connect_no_reset_attempts") == 1
    and value.get("write_attempt_count") == 1
    and value.get("write_after_action") == "no_reset"
    and value.get("readback_after_action") == "no_reset"
    and value.get("post_install_reset_count") == 0
):
    raise SystemExit("exclusive install result is not exact")
print(value["readback_chunks"], value["readback_max_chunk_bytes"],
      value["mutation_span_bytes"])
' "$P4_GAMEPAD_INSTALL_RESULT" "$P4_GAMEPAD_EXPECTED_OFFSET" \
            "$P4_GAMEPAD_EXPECTED_BYTES" "$P4_GAMEPAD_EXPECTED_HASH")
        set -- $P4_GAMEPAD_INSTALL_SUMMARY
        P4_GAMEPAD_READBACK_CHUNKS=$1
        P4_GAMEPAD_READBACK_MAX_CHUNK_BYTES=$2
        P4_GAMEPAD_MUTATION_SPAN=$3
        if [ "$P4_GAMEPAD_MUTATION_SPAN" != 311296 ]; then
            printf 'Exclusive gamepad install returned the wrong recovery span.\n' >&2
            exit 1
        fi
        P4_GAMEPAD_RESERVATION_ACTIVE=false
        printf 'Exclusive same-UART gamepad install verified: offset=%s bytes=%s chunks=%s chunk_bytes_max=%s sha256=%s recovery_span=%s\n' \
            "$P4_GAMEPAD_EXPECTED_OFFSET" "$P4_GAMEPAD_EXPECTED_BYTES" \
            "$P4_GAMEPAD_READBACK_CHUNKS" "$P4_GAMEPAD_READBACK_MAX_CHUNK_BYTES" \
            "$P4_GAMEPAD_EXPECTED_HASH" "$P4_GAMEPAD_MUTATION_SPAN"
        printf 'Verified gamepad_diag remains stopped in the RAM stub/ROM loader.\n'
        printf 'Reset-safe gamepad launch receipt: %s\n' "$P4_LAUNCH_RECEIPT"
        p4_cleanup_flash_temps
        trap - EXIT HUP INT TERM
        exit 0
    fi
    if [ "$P4_APP" = doom_embedded_touch_audio ]; then
        p4_require_port "$P4_PORT"
        P4_E5_INSTALL_RESULT="$P4_VERIFIED_APP_DIR/install-result.json"
        # Revalidate the complete frozen E5 policy/build graph immediately
        # before the one process that owns the sole authoritative UART probe,
        # write, readback, and application-launch reset.
        python3 "$P4_SCRIPT_DIR/verify-doom-embedded-touch-audio.py" \
            "$P4_BUILD_DIR" app-flash
        python3 "$P4_E5_INSTALL_TOOL" \
            --port "$P4_PORT" \
            --artifact "$P4_BUILT_APP_PATH" \
            --authorization "$P4_E5_AUTH" \
            --result "$P4_E5_INSTALL_RESULT"
        P4_E5_INSTALL_SUMMARY=$(python3 -c '
import json, pathlib, sys
value = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
if not (
    value.get("offset") == "0x10000"
    and value.get("artifact_bytes") == 4898400
    and value.get("artifact_sha256") == "68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8"
    and value.get("mutation_span_bytes") == 4898816
    and value.get("readback_bytes") == 4898816
    and value.get("readback_sha256") == value.get("mutation_span_sha256")
    and value.get("readback_max_chunk_bytes") <= 524288
    and value.get("same_uart_handle") is True
    and value.get("exclusive_uart") is True
    and value.get("connect_no_reset_attempts") == 1
    and value.get("exclusive_transaction_loader_entry_reset_count") == 1
    and value.get("exclusive_transaction_application_launch_reset_count") == 1
    and value.get("application_launch_count") == 1
    and value.get("stub_flash_finish_sync_count") == 1
    and value.get("soft_reset_count") == 0
    and value.get("esptool_run_count") == 0
    and value.get("uart_reopen_count") == 0
    and value.get("audio_runtime") is False
    and value.get("gpio30_access") is False
):
    raise SystemExit("exclusive E5 install result is not exact")
print(value["readback_chunks"], value["mutation_span_sha256"])
' "$P4_E5_INSTALL_RESULT")
        set -- $P4_E5_INSTALL_SUMMARY
        printf 'Exclusive same-UART E5 install verified and launched once: chunks=%s span_sha256=%s\n' \
            "$1" "$2"
        p4_cleanup_flash_temps
        trap - EXIT HUP INT TERM
        exit 0
    fi
    P4_DIRECT_WRITE_BEFORE=no_reset
    P4_FLASH_OUTPUT=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
        --baud 460800 --before "$P4_DIRECT_WRITE_BEFORE" --after no_reset write_flash \
        --flash_mode dio --flash_freq 80m --flash_size 16MB \
        "$P4_BUILT_APP_OFFSET" "$P4_BUILT_APP_PATH" 2>&1) || {
        printf '%s\n' "$P4_FLASH_OUTPUT" | p4_redact_device_identifiers >&2
        exit 1
    }
    printf '%s\n' "$P4_FLASH_OUTPUT" | p4_redact_device_identifiers
else
    P4_FLASH_OUTPUT=$(idf.py -C "$P4_PROJECT_ROOT/apps/$P4_APP" \
        -B "$P4_PROJECT_ROOT/apps/$P4_APP/build" -D IDF_TARGET=esp32p4 \
        -p "$P4_PORT" "$P4_FLASH_TARGET" 2>&1) || {
        printf '%s\n' "$P4_FLASH_OUTPUT" | p4_redact_device_identifiers >&2
        exit 1
    }
    printf '%s\n' "$P4_FLASH_OUTPUT" | p4_redact_device_identifiers
fi

P4_BUILT_APP_BYTES=$(wc -c < "$P4_BUILT_APP_PATH" | tr -d ' ')
P4_BUILT_APP_HASH=$(p4_sha256_file "$P4_BUILT_APP_PATH")
if [ "$P4_VERIFIED_DIRECT_WRITE" = true ]; then
    if [ "$P4_BUILT_APP_BYTES" != "$P4_VERIFIED_APP_EXPECTED_BYTES" ] || \
       [ "$P4_BUILT_APP_HASH" != "$P4_VERIFIED_APP_EXPECTED_HASH" ]; then
        printf 'Verified application snapshot changed during direct write.\n' >&2
        exit 1
    fi
fi
if [ "$P4_APP" = sd_format_diag ] || \
   [ "$P4_APP" = wad_provisioner ] || \
   [ "$P4_APP" = doom_embedded ] || \
   [ "$P4_APP" = doom_embedded_gamepad ] || \
   [ "$P4_APP" = audio_diag ]; then
    # These apps mutate hardware or start a visible/audible runtime immediately.
    # Keep the CPU in the ROM loader until exact readback has passed.
    P4_READBACK_AFTER=no_reset
else
    P4_READBACK_AFTER=hard_reset
fi
if [ "$P4_APP" = audio_direct_diag ]; then
    # The bounded tone must remain stopped until exact app readback passes.
    P4_READBACK_AFTER=no_reset
fi
P4_READBACK_BEFORE=default_reset
if [ "$P4_RESUME_READBACK" = true ]; then
    P4_READBACK_BAUDS='460800 230400 115200'
elif [ "$P4_BUILT_APP_BYTES" -gt "$P4_READBACK_CHUNK_BYTES" ]; then
    # 512 KiB reads at 460800 are proven stable on the connected CH340 path.
    P4_READBACK_BAUDS='460800 230400 115200'
else
    P4_READBACK_BAUDS='921600 460800 230400'
fi
if ! p4_verify_chunked_application_readback \
    "$P4_BUILT_APP_PATH" "$P4_BUILT_APP_OFFSET" "$P4_BUILT_APP_BYTES" \
    "$P4_PORT" "$P4_READBACK_AFTER" "$P4_READBACK_BAUDS" \
    "$P4_READBACK_BEFORE"; then
    printf 'Application readback failed at every bounded baud; image was not launched.\n' >&2
    exit 1
fi
if [ "$P4_READBACK_HASH" != "$P4_BUILT_APP_HASH" ]; then
    printf 'Ordered application readback SHA-256 mismatch; image was not launched.\n' >&2
    exit 1
fi
if [ "$P4_VERIFIED_DIRECT_WRITE" = true ]; then
    if [ "$P4_READBACK_ACTUAL_BYTES" != "$P4_VERIFIED_APP_EXPECTED_BYTES" ] || \
       [ "$P4_READBACK_HASH" != "$P4_VERIFIED_APP_EXPECTED_HASH" ]; then
        printf 'Authorized application readback identity mismatch; image was not launched.\n' >&2
        exit 1
    fi
fi
printf 'Application readback verified: offset=%s bytes=%s chunks=%s chunk_bytes_max=%s sha256=%s\n' \
    "$P4_BUILT_APP_OFFSET" "$P4_READBACK_ACTUAL_BYTES" \
    "$P4_READBACK_CHUNKS" "$P4_READBACK_CHUNK_BYTES" "$P4_READBACK_HASH"
if [ "$P4_APP" = sd_format_diag ] || \
   [ "$P4_APP" = wad_provisioner ] || \
   [ "$P4_APP" = doom_embedded ] || \
   [ "$P4_APP" = doom_embedded_gamepad ] || \
   [ "$P4_APP" = audio_diag ]; then
    P4_RUN_OUTPUT=$(esptool.py --chip esp32p4 --port "$P4_PORT" run 2>&1) || {
        printf '%s\n' "$P4_RUN_OUTPUT" | p4_redact_device_identifiers >&2
        printf '%s image verified, but its deferred launch failed.\n' "$P4_APP" >&2
        exit 1
    }
    printf '%s\n' "$P4_RUN_OUTPUT" | p4_redact_device_identifiers
    printf 'Verified %s launched exactly once after successful application readback.\n' \
        "$P4_APP"
    if [ "$P4_APP" = doom_embedded_gamepad ]; then
        python3 "$P4_E3_STATE_TOOL" complete \
            --state "$P4_E3_STATE" --authorization "$P4_E3_AUTH" \
            --detail exact-readback-passed-and-explicit-run-issued-once
        P4_E3_RESERVATION_ACTIVE=false
        printf 'E3 one-shot authorization consumed by verified launch.\n'
    fi
fi
if [ "$P4_APP" = audio_direct_diag ]; then
    P4_D23_READBACK_MAX_CHUNK_BYTES=$P4_READBACK_CHUNK_BYTES
    if [ "$P4_READBACK_ACTUAL_BYTES" -lt "$P4_D23_READBACK_MAX_CHUNK_BYTES" ]; then
        P4_D23_READBACK_MAX_CHUNK_BYTES=$P4_READBACK_ACTUAL_BYTES
    fi
    python3 "$P4_D23_CAPTURE_TOOL" emit-receipt \
        --receipt "$P4_LAUNCH_RECEIPT" --state "$P4_D23_STATE" \
        --port "$P4_PORT" \
        --device-identity-sha256 "$P4_DEVICE_IDENTITY_ACTUAL" \
        --offset "$P4_BUILT_APP_OFFSET" --bytes "$P4_BUILT_APP_BYTES" \
        --sha256 "$P4_BUILT_APP_HASH" \
        --readback-bytes "$P4_READBACK_ACTUAL_BYTES" \
        --readback-sha256 "$P4_READBACK_HASH" \
        --readback-chunks "$P4_READBACK_CHUNKS" \
        --readback-max-chunk-bytes "$P4_D23_READBACK_MAX_CHUNK_BYTES" \
        --authorization "$P4_D23_AUTH" \
        --verifier "$P4_SCRIPT_DIR/verify-audio-direct-diag.py" \
        --analyzer "$P4_SCRIPT_DIR/analyze-audio-tone.py"
    P4_D23_RESERVATION_ACTIVE=false
    printf 'Verified %s remains stopped in the ROM loader for synchronized capture.\n' \
        "$P4_APP"
    printf 'One-shot capture receipt: %s\n' "$P4_LAUNCH_RECEIPT"
    p4_cleanup_flash_temps
    trap - EXIT HUP INT TERM
    exit 0
fi
p4_cleanup_flash_temps
trap - EXIT HUP INT TERM
