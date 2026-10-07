#!/bin/sh

# Isolated, receipt-only D2.4 app writer. This script never launches the app.
set -eu

P4_PORT=
P4_RECEIPT=
P4_DEFER=false
while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) P4_PORT=${2:-}; shift 2 ;;
        --launch-receipt) P4_RECEIPT=${2:-}; shift 2 ;;
        --defer-launch-for-capture) P4_DEFER=true; shift ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done
if [ "$P4_DEFER" != true ] || [ -z "$P4_RECEIPT" ]; then
    printf 'Refusing D2.4: --defer-launch-for-capture and --launch-receipt are required.\n' >&2
    exit 2
fi

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/app-readback.sh"

P4_APP=audio_level_diag
P4_BUILD_DIR="$P4_PROJECT_ROOT/apps/$P4_APP/build"
P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/audio-level-diag-d24-one-shot-authorization.json"
P4_STATE="$P4_PROJECT_ROOT/hardware/local-state/audio-level-d24.json"
P4_CAPTURE="$P4_SCRIPT_DIR/capture-audio-level-diag.py"
P4_VERIFIER="$P4_SCRIPT_DIR/verify-audio-level-diag.py"
P4_ANALYZER="$P4_SCRIPT_DIR/analyze-audio-level-tone.py"
P4_RESERVATION_ACTIVE=false
P4_SNAPSHOT_DIR=

p4_remove_snapshot() {
    if [ -z "${P4_SNAPSHOT_DIR:-}" ]; then
        return 0
    fi
    P4_SNAPSHOT_PARENT=$(dirname -- "$P4_SNAPSHOT_DIR")
    P4_SNAPSHOT_NAME=$(basename -- "$P4_SNAPSHOT_DIR")
    case "$P4_SNAPSHOT_NAME" in p4-d24-sealed.*) ;; *) return 1 ;; esac
    if [ "$P4_SNAPSHOT_PARENT" != "${P4_SNAPSHOT_ROOT:-}" ]; then
        return 1
    fi
    rm -rf -- "$P4_SNAPSHOT_DIR"
    P4_SNAPSHOT_DIR=
}
p4_cleanup() {
    if [ "$P4_RESERVATION_ACTIVE" = true ]; then
        if ! python3 "$P4_CAPTURE" fail-reservation --state "$P4_STATE" \
            --authorization "$P4_AUTH" --reason d24-flash-path-failed; then
            printf 'Warning: unable to persist terminal D2.4 reservation failure.\n' >&2
        fi
        P4_RESERVATION_ACTIVE=false
    fi
    p4_remove_readback
    p4_remove_snapshot
}
p4_signal() {
    P4_CODE=$1
    trap - EXIT HUP INT TERM
    p4_cleanup
    exit "$P4_CODE"
}
trap p4_cleanup EXIT
trap 'p4_signal 129' HUP
trap 'p4_signal 130' INT
trap 'p4_signal 143' TERM

p4_require_app "$P4_APP"
p4_require_port "$P4_PORT"
p4_activate_idf
python3 "$P4_SCRIPT_DIR/verify-metadata.py" --flash-preflight >/dev/null

# Build once, then seal the exact independently reviewed identity. There is no
# build or generic idf.py flash after this verifier passes.
"$P4_SCRIPT_DIR/build.sh" "$P4_APP"
python3 "$P4_VERIFIER" "$P4_BUILD_DIR" app-flash

P4_AUTH_OFFSET=$(python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["offset"])' \
    "$P4_AUTH")
P4_AUTH_BYTES=$(python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["bytes"])' \
    "$P4_AUTH")
P4_AUTH_HASH=$(python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["exact_artifact"]["sha256"])' \
    "$P4_AUTH")
P4_APP_REL=$(python3 -c \
    'import json,pathlib,sys; b=pathlib.Path(sys.argv[1]); print(json.loads((b/"flasher_args.json").read_text())["app"]["file"])' \
    "$P4_BUILD_DIR")
P4_BUILT_APP="$P4_BUILD_DIR/$P4_APP_REL"

P4_SNAPSHOT_ROOT=${TMPDIR:-/tmp}
P4_SNAPSHOT_ROOT=${P4_SNAPSHOT_ROOT%/}
case "$P4_SNAPSHOT_ROOT" in /tmp|/private/tmp|/private/var/folders/*) ;; *)
    printf 'Refusing unexpected D2.4 temporary root: %s\n' "$P4_SNAPSHOT_ROOT" >&2
    exit 1 ;;
esac
P4_SNAPSHOT_DIR=$(mktemp -d "$P4_SNAPSHOT_ROOT/p4-d24-sealed.XXXXXX")
chmod 700 "$P4_SNAPSHOT_DIR"
P4_SNAPSHOT="$P4_SNAPSHOT_DIR/application.bin"
cp -- "$P4_BUILT_APP" "$P4_SNAPSHOT"
chmod 400 "$P4_SNAPSHOT"
P4_SEALED_BYTES=$(wc -c < "$P4_SNAPSHOT" | tr -d ' ')
P4_SEALED_HASH=$(p4_sha256_file "$P4_SNAPSHOT")
if [ "$P4_SEALED_BYTES" != "$P4_AUTH_BYTES" ] || \
   [ "$P4_SEALED_HASH" != "$P4_AUTH_HASH" ]; then
    printf 'Refusing D2.4: sealed snapshot differs from authorization.\n' >&2
    exit 1
fi

# Three independent one-second microphone/serial preflights must complete
# before durable reservation or any board probe.
P4_PREFLIGHT_RUN=1
while [ "$P4_PREFLIGHT_RUN" -le 3 ]; do
    python3 "$P4_CAPTURE" preflight-host --port "$P4_PORT"
    P4_PREFLIGHT_RUN=$((P4_PREFLIGHT_RUN + 1))
done
python3 "$P4_CAPTURE" check-issuable --state "$P4_STATE" \
    --authorization "$P4_AUTH"

P4_DEVICE_PROFILE="$P4_PROJECT_ROOT/hardware/board-profile.json"
P4_DEVICE_IDENTITY_EXPECTED=$(python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["device_identity"]["sha256"])' \
    "$P4_DEVICE_PROFILE")
P4_DEVICE_FLASH_BYTES=$(python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["measured"]["flash_bytes"])' \
    "$P4_DEVICE_PROFILE")
P4_DEVICE_REVISION_EXPECTED=$(python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1]))["measured"]["chip_revision"])' \
    "$P4_DEVICE_PROFILE")

P4_RESERVATION_ACTIVE=true
python3 "$P4_CAPTURE" reserve --state "$P4_STATE" \
    --receipt "$P4_RECEIPT" --authorization "$P4_AUTH" \
    --device-identity-sha256 "$P4_DEVICE_IDENTITY_EXPECTED" \
    --offset "$P4_AUTH_OFFSET" --bytes "$P4_AUTH_BYTES" --sha256 "$P4_AUTH_HASH"

P4_PROBE=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
    --after no_reset flash_id 2>&1) || {
    printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers >&2
    exit 1
}
printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers
P4_EXPECTED_FLASH_LABEL=$(python3 -c \
    'import sys; n=int(sys.argv[1]); assert n>0 and n%(1024*1024)==0; print(f"{n//(1024*1024)}MB")' \
    "$P4_DEVICE_FLASH_BYTES")
printf '%s\n' "$P4_PROBE" | grep -F "Detected flash size: $P4_EXPECTED_FLASH_LABEL" >/dev/null || {
    printf 'Refusing D2.4: live flash size differs from reviewed profile.\n' >&2
    exit 1
}
printf '%s\n' "$P4_PROBE" | grep -F "(revision $P4_DEVICE_REVISION_EXPECTED)" >/dev/null || {
    printf 'Refusing D2.4: live chip revision differs from reviewed profile.\n' >&2
    exit 1
}
P4_DEVICE_IDENTITY_ACTUAL=$(p4_read_device_identity_hash "$P4_PORT" no_reset)
if [ "$P4_DEVICE_IDENTITY_ACTUAL" != "$P4_DEVICE_IDENTITY_EXPECTED" ]; then
    printf 'Refusing D2.4: connected device identity differs from reviewed profile.\n' >&2
    exit 1
fi
P4_SECURITY=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
    --after no_reset get_security_info 2>&1) || exit 1
printf '%s\n' "$P4_SECURITY" | p4_redact_device_identifiers
printf '%s\n' "$P4_SECURITY" | grep -F 'Secure Boot: Disabled' >/dev/null || exit 1
printf '%s\n' "$P4_SECURITY" | grep -F 'Flash Encryption: Disabled' >/dev/null || exit 1

python3 "$P4_SCRIPT_DIR/verify-live-app-layout.py" \
    --port "$P4_PORT" --offset "$P4_AUTH_OFFSET" \
    --bytes "$P4_AUTH_BYTES" --flash-bytes "$P4_DEVICE_FLASH_BYTES"

# Recheck the sealed snapshot immediately before and after the exact app-only
# write. The CPU remains in the ROM loader throughout.
python3 "$P4_VERIFIER" "$P4_BUILD_DIR" app-flash
if [ "$(wc -c < "$P4_SNAPSHOT" | tr -d ' ')" != "$P4_AUTH_BYTES" ] || \
   [ "$(p4_sha256_file "$P4_SNAPSHOT")" != "$P4_AUTH_HASH" ]; then
    printf 'Refusing D2.4: sealed snapshot changed before write.\n' >&2
    exit 1
fi
P4_WRITE=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
    --before default_reset --after no_reset write_flash \
    --flash_mode dio --flash_freq 80m --flash_size 16MB \
    "$P4_AUTH_OFFSET" "$P4_SNAPSHOT" 2>&1) || {
    printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers >&2
    exit 1
}
printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers
if [ "$(wc -c < "$P4_SNAPSHOT" | tr -d ' ')" != "$P4_AUTH_BYTES" ] || \
   [ "$(p4_sha256_file "$P4_SNAPSHOT")" != "$P4_AUTH_HASH" ]; then
    printf 'Refusing D2.4: sealed snapshot changed during write.\n' >&2
    exit 1
fi

if ! p4_verify_chunked_application_readback "$P4_SNAPSHOT" \
    "$P4_AUTH_OFFSET" "$P4_AUTH_BYTES" "$P4_PORT" no_reset \
    '460800 230400 115200'; then
    printf 'D2.4 exact readback failed; image was not launched.\n' >&2
    exit 1
fi
if [ "$P4_READBACK_ACTUAL_BYTES" != "$P4_AUTH_BYTES" ] || \
   [ "$P4_READBACK_HASH" != "$P4_AUTH_HASH" ]; then
    printf 'D2.4 readback identity mismatch; image was not launched.\n' >&2
    exit 1
fi
P4_MAX_CHUNK=$P4_READBACK_CHUNK_BYTES
if [ "$P4_READBACK_ACTUAL_BYTES" -lt "$P4_MAX_CHUNK" ]; then
    P4_MAX_CHUNK=$P4_READBACK_ACTUAL_BYTES
fi
# Revalidate the ACTIVE authorization and every bound host-tool byte after
# readback and immediately before issuing the launch receipt. The receipt then
# binds this same authorization hash for its later atomic capture consumption.
python3 "$P4_VERIFIER" "$P4_BUILD_DIR" app-flash
python3 "$P4_CAPTURE" emit-receipt --receipt "$P4_RECEIPT" \
    --state "$P4_STATE" --port "$P4_PORT" \
    --device-identity-sha256 "$P4_DEVICE_IDENTITY_ACTUAL" \
    --offset "$P4_AUTH_OFFSET" --bytes "$P4_AUTH_BYTES" --sha256 "$P4_AUTH_HASH" \
    --readback-bytes "$P4_READBACK_ACTUAL_BYTES" \
    --readback-sha256 "$P4_READBACK_HASH" \
    --readback-chunks "$P4_READBACK_CHUNKS" \
    --readback-max-chunk-bytes "$P4_MAX_CHUNK" \
    --authorization "$P4_AUTH" --verifier "$P4_VERIFIER" \
    --analyzer "$P4_ANALYZER"
P4_RESERVATION_ACTIVE=false
printf 'Verified audio_level_diag remains stopped in the ROM loader.\n'
printf 'One-shot D2.4 receipt: %s\n' "$P4_RECEIPT"
p4_cleanup
trap - EXIT HUP INT TERM
