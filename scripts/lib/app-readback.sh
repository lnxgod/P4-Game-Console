#!/bin/sh

# This file is sourced by scripts/flash.sh and its host tests.

P4_READBACK_CHUNK_BYTES=524288
P4_READBACK_DIR=
P4_READBACK_HASH=
P4_READBACK_CHUNKS=0
P4_READBACK_ACTUAL_BYTES=0

p4_remove_readback() {
    if [ -z "${P4_READBACK_DIR:-}" ]; then
        return 0
    fi

    P4_READBACK_REMOVE_PARENT=$(dirname -- "$P4_READBACK_DIR")
    P4_READBACK_REMOVE_NAME=$(basename -- "$P4_READBACK_DIR")
    case "$P4_READBACK_REMOVE_NAME" in
        p4-app-readback.*) ;;
        *)
            printf 'Refusing to remove unexpected readback path: %s\n' \
                "$P4_READBACK_DIR" >&2
            return 1
            ;;
    esac
    if [ "$P4_READBACK_REMOVE_PARENT" != "${P4_READBACK_TMP_ROOT:-}" ]; then
        printf 'Refusing to remove readback path outside its temporary root: %s\n' \
            "$P4_READBACK_DIR" >&2
        return 1
    fi
    rm -rf -- "$P4_READBACK_DIR"
    P4_READBACK_DIR=
}

p4_verify_chunked_application_readback() {
    P4_READBACK_SOURCE=${1:-}
    P4_READBACK_FLASH_OFFSET=${2:-}
    P4_READBACK_EXPECTED_BYTES=${3:-}
    P4_READBACK_PORT=${4:-}
    P4_READBACK_AFTER=${5:-}
    P4_READBACK_BAUDS=${6:-}
    # Preserve the historical reset-to-loader behavior for every existing
    # caller. Safety-critical routes that already established the loader may
    # explicitly require no_reset as a seventh argument.
    P4_READBACK_BEFORE=${7:-default_reset}

    P4_READBACK_HASH=
    P4_READBACK_CHUNKS=0
    P4_READBACK_ACTUAL_BYTES=0

    if [ ! -f "$P4_READBACK_SOURCE" ]; then
        printf 'Readback source is not a regular file: %s\n' \
            "$P4_READBACK_SOURCE" >&2
        return 1
    fi
    case "$P4_READBACK_EXPECTED_BYTES" in
        ''|*[!0-9]*)
            printf 'Invalid application readback byte count: %s\n' \
                "${P4_READBACK_EXPECTED_BYTES:-<empty>}" >&2
            return 1
            ;;
    esac
    if [ "$P4_READBACK_EXPECTED_BYTES" -le 0 ]; then
        printf 'Application readback byte count must be positive.\n' >&2
        return 1
    fi
    P4_READBACK_SOURCE_BYTES=$(wc -c < "$P4_READBACK_SOURCE" | tr -d ' ')
    if [ "$P4_READBACK_SOURCE_BYTES" != "$P4_READBACK_EXPECTED_BYTES" ]; then
        printf 'Application source byte-count changed before readback: expected=%s actual=%s\n' \
            "$P4_READBACK_EXPECTED_BYTES" "$P4_READBACK_SOURCE_BYTES" >&2
        return 1
    fi
    case "$P4_READBACK_AFTER" in
        hard_reset|no_reset) ;;
        *)
            printf 'Invalid application readback after-action: %s\n' \
                "${P4_READBACK_AFTER:-<empty>}" >&2
            return 1
            ;;
    esac
    case "$P4_READBACK_BEFORE" in
        default_reset|no_reset) ;;
        *)
            printf 'Invalid application readback before-action: %s\n' \
                "${P4_READBACK_BEFORE:-<empty>}" >&2
            return 1
            ;;
    esac
    if [ -z "$P4_READBACK_PORT" ] || [ -z "$P4_READBACK_BAUDS" ]; then
        printf 'Application readback requires a port and bounded baud sequence.\n' >&2
        return 1
    fi
    case "$P4_READBACK_BAUDS" in
        '921600 460800 230400'|'460800 230400 115200') ;;
        *)
            printf 'Invalid application readback baud sequence: %s\n' \
                "$P4_READBACK_BAUDS" >&2
            return 1
            ;;
    esac

    if ! P4_READBACK_BASE_OFFSET=$(python3 -c '
import sys
try:
    value = int(sys.argv[1], 0)
except ValueError:
    raise SystemExit(1)
if value < 0:
    raise SystemExit(1)
print(value)
' "$P4_READBACK_FLASH_OFFSET"); then
        printf 'Invalid application readback flash offset: %s\n' \
            "${P4_READBACK_FLASH_OFFSET:-<empty>}" >&2
        return 1
    fi

    P4_READBACK_TMP_ROOT=${TMPDIR:-/tmp}
    P4_READBACK_TMP_ROOT=${P4_READBACK_TMP_ROOT%/}
    if [ -z "$P4_READBACK_TMP_ROOT" ] || [ ! -d "$P4_READBACK_TMP_ROOT" ]; then
        printf 'Application readback temporary root is unavailable: %s\n' \
            "${P4_READBACK_TMP_ROOT:-<empty>}" >&2
        return 1
    fi
    if ! P4_READBACK_DIR=$(mktemp -d "$P4_READBACK_TMP_ROOT/p4-app-readback.XXXXXX"); then
        printf 'Unable to create a fresh application readback directory.\n' >&2
        return 1
    fi
    if ! chmod 700 "$P4_READBACK_DIR"; then
        printf 'Unable to secure the application readback directory.\n' >&2
        return 1
    fi

    P4_READBACK_DONE_BYTES=0
    P4_READBACK_INDEX=0
    while [ "$P4_READBACK_DONE_BYTES" -lt "$P4_READBACK_EXPECTED_BYTES" ]; do
        P4_READBACK_REMAINING=$((P4_READBACK_EXPECTED_BYTES - P4_READBACK_DONE_BYTES))
        if [ "$P4_READBACK_REMAINING" -gt "$P4_READBACK_CHUNK_BYTES" ]; then
            P4_READBACK_THIS_BYTES=$P4_READBACK_CHUNK_BYTES
        else
            P4_READBACK_THIS_BYTES=$P4_READBACK_REMAINING
        fi
        P4_READBACK_THIS_OFFSET=$((P4_READBACK_BASE_OFFSET + P4_READBACK_DONE_BYTES))
        P4_READBACK_CANONICAL=$(printf '%s/chunk-%06d.bin' \
            "$P4_READBACK_DIR" "$P4_READBACK_INDEX")
        P4_READBACK_CHUNK_VERIFIED=false

        for P4_READBACK_BAUD in $P4_READBACK_BAUDS; do
            P4_READBACK_ATTEMPT=$(printf '%s/attempt-%06d-%s.bin' \
                "$P4_READBACK_DIR" "$P4_READBACK_INDEX" "$P4_READBACK_BAUD")
            if [ -e "$P4_READBACK_ATTEMPT" ]; then
                printf 'Refusing stale readback attempt file: %s\n' \
                    "$P4_READBACK_ATTEMPT" >&2
                return 1
            fi
            printf 'Application readback chunk attempt: index=%s offset=0x%x bytes=%s baud=%s before=%s after=%s\n' \
                "$P4_READBACK_INDEX" "$P4_READBACK_THIS_OFFSET" \
                "$P4_READBACK_THIS_BYTES" "$P4_READBACK_BAUD" \
                "$P4_READBACK_BEFORE" "$P4_READBACK_AFTER"
            if P4_READBACK_OUTPUT=$(esptool.py --chip esp32p4 \
                --port "$P4_READBACK_PORT" --baud "$P4_READBACK_BAUD" \
                --before "$P4_READBACK_BEFORE" \
                --after "$P4_READBACK_AFTER" read_flash \
                "$P4_READBACK_THIS_OFFSET" "$P4_READBACK_THIS_BYTES" \
                "$P4_READBACK_ATTEMPT" 2>&1); then
                printf '%s\n' "$P4_READBACK_OUTPUT" | p4_redact_device_identifiers
                if P4_READBACK_CHUNK_HASH=$(python3 \
                    "$P4_SCRIPT_DIR/verify-readback-chunks.py" chunk \
                    --source "$P4_READBACK_SOURCE" \
                    --source-offset "$P4_READBACK_DONE_BYTES" \
                    --bytes "$P4_READBACK_THIS_BYTES" \
                    --readback "$P4_READBACK_ATTEMPT" 2>&1); then
                    if ! mv "$P4_READBACK_ATTEMPT" "$P4_READBACK_CANONICAL"; then
                        printf 'Unable to seal verified readback chunk %s.\n' \
                            "$P4_READBACK_INDEX" >&2
                        return 1
                    fi
                    P4_READBACK_CHUNK_VERIFIED=true
                    printf 'Application readback chunk verified: index=%s bytes=%s sha256=%s\n' \
                        "$P4_READBACK_INDEX" "$P4_READBACK_THIS_BYTES" \
                        "$P4_READBACK_CHUNK_HASH"
                    break
                fi
                printf '%s\n' "$P4_READBACK_CHUNK_HASH" >&2
                printf 'Readback chunk verification failed at baud %s; retrying this chunk at lower baud.\n' \
                    "$P4_READBACK_BAUD" >&2
            else
                printf '%s\n' "$P4_READBACK_OUTPUT" | \
                    p4_redact_device_identifiers >&2
                printf 'Readback chunk transport failed at baud %s; retrying this chunk at lower baud.\n' \
                    "$P4_READBACK_BAUD" >&2
            fi
            rm -f -- "$P4_READBACK_ATTEMPT"
        done

        if [ "$P4_READBACK_CHUNK_VERIFIED" != true ]; then
            printf 'Application readback chunk %s failed at every bounded baud.\n' \
                "$P4_READBACK_INDEX" >&2
            return 1
        fi
        P4_READBACK_DONE_BYTES=$((P4_READBACK_DONE_BYTES + P4_READBACK_THIS_BYTES))
        P4_READBACK_INDEX=$((P4_READBACK_INDEX + 1))
    done

    if ! P4_READBACK_SUMMARY=$(python3 \
        "$P4_SCRIPT_DIR/verify-readback-chunks.py" aggregate \
        --directory "$P4_READBACK_DIR" --chunks "$P4_READBACK_INDEX" \
        --bytes "$P4_READBACK_EXPECTED_BYTES" \
        --chunk-bytes "$P4_READBACK_CHUNK_BYTES" 2>&1); then
        printf '%s\n' "$P4_READBACK_SUMMARY" >&2
        return 1
    fi
    P4_READBACK_ACTUAL_BYTES=${P4_READBACK_SUMMARY%% *}
    P4_READBACK_HASH=${P4_READBACK_SUMMARY#* }
    if [ "$P4_READBACK_ACTUAL_BYTES" != "$P4_READBACK_EXPECTED_BYTES" ]; then
        printf 'Ordered readback byte-count mismatch: expected=%s actual=%s\n' \
            "$P4_READBACK_EXPECTED_BYTES" "$P4_READBACK_ACTUAL_BYTES" >&2
        return 1
    fi
    P4_READBACK_CHUNKS=$P4_READBACK_INDEX
    return 0
}
