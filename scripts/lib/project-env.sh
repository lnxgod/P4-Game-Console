#!/bin/sh

set -eu

if [ -z "${P4_SCRIPT_DIR:-}" ]; then
    printf 'P4_SCRIPT_DIR must be set by the calling repository script.\n' >&2
    return 2 2>/dev/null || exit 2
fi

P4_PROJECT_ROOT=$(CDPATH= cd -- "$P4_SCRIPT_DIR/.." && pwd)
P4_PINNED_IDF_VERSION=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["esp_idf"]["version"])' "$P4_PROJECT_ROOT/toolchain.lock.json")
P4_PINNED_IDF_TAG=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["esp_idf"]["git_tag"])' "$P4_PROJECT_ROOT/toolchain.lock.json")
P4_PINNED_IDF_COMMIT=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["esp_idf"]["git_commit"])' "$P4_PROJECT_ROOT/toolchain.lock.json")

p4_find_idf_path() {
    if [ -n "${P4_IDF_PATH:-}" ]; then
        printf '%s\n' "$P4_IDF_PATH"
        return
    fi

    for p4_candidate in \
        "$P4_PROJECT_ROOT/.tools/esp-idf-v$P4_PINNED_IDF_VERSION" \
        "${HOME}/esp/esp-idf-v$P4_PINNED_IDF_VERSION"
    do
        if [ -f "$p4_candidate/export.sh" ]; then
            printf '%s\n' "$p4_candidate"
            return
        fi
    done

    if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/export.sh" ]; then
        printf '%s\n' "$IDF_PATH"
        return
    fi

    return 1
}

p4_configure_idf_tools_path() {
    # Honor even an explicitly empty override; otherwise reuse local tools
    # without changing Espressif's default for a checkout with no cache.
    if [ "${IDF_TOOLS_PATH+x}" != x ] && \
       [ -d "$P4_PROJECT_ROOT/.tools/espressif" ]; then
        IDF_TOOLS_PATH="$P4_PROJECT_ROOT/.tools/espressif"
        export IDF_TOOLS_PATH
    fi
}

p4_verify_idf_checkout() {
    P4_CHECKED_IDF_PATH=$1
    if [ -e "$P4_CHECKED_IDF_PATH/.git" ]; then
        P4_ACTIVE_IDF_TAG=$(git -C "$P4_CHECKED_IDF_PATH" describe --tags --exact-match 2>/dev/null || true)
        if [ "$P4_ACTIVE_IDF_TAG" != "$P4_PINNED_IDF_TAG" ]; then
            printf 'ESP-IDF tag mismatch: expected %s, found %s at %s\n' \
                "$P4_PINNED_IDF_TAG" "${P4_ACTIVE_IDF_TAG:-untagged}" "$P4_CHECKED_IDF_PATH" >&2
            return 1
        fi

        P4_IDF_TRACKED_CHANGES=$(git -C "$P4_CHECKED_IDF_PATH" status \
            --short --untracked-files=no --ignore-submodules=none) || return 1
        if [ -n "$P4_IDF_TRACKED_CHANGES" ]; then
            printf 'ESP-IDF checkout has tracked changes at %s; refusing an unlocked SDK.\n' \
                "$P4_CHECKED_IDF_PATH" >&2
            return 1
        fi

        case "$P4_PINNED_IDF_COMMIT" in
            ''|pending-*) ;;
            *)
                P4_ACTIVE_IDF_COMMIT=$(git -C "$P4_CHECKED_IDF_PATH" rev-parse HEAD) || return 1
                if [ "$P4_ACTIVE_IDF_COMMIT" != "$P4_PINNED_IDF_COMMIT" ]; then
                    printf 'ESP-IDF commit mismatch: expected %s, found %s at %s\n' \
                        "$P4_PINNED_IDF_COMMIT" "$P4_ACTIVE_IDF_COMMIT" "$P4_CHECKED_IDF_PATH" >&2
                    return 1
                fi
                ;;
        esac
        P4_IDF_SUBMODULE_STATUS=$(git -C "$P4_CHECKED_IDF_PATH" submodule status --recursive) || return 1
        P4_IDF_SUBMODULE_MISMATCH_PATTERN='^[+-U]'
        if [ "${2:-}" = --allow-missing-submodules ]; then
            P4_IDF_SUBMODULE_MISMATCH_PATTERN='^[+U]'
        fi
        if printf '%s\n' "$P4_IDF_SUBMODULE_STATUS" | grep -E "$P4_IDF_SUBMODULE_MISMATCH_PATTERN" >/dev/null; then
            printf 'ESP-IDF submodules are missing or do not match %s at %s.\n' \
                "$P4_PINNED_IDF_TAG" "$P4_CHECKED_IDF_PATH" >&2
            return 1
        fi
    elif [ -n "$P4_PINNED_IDF_COMMIT" ]; then
        case "$P4_PINNED_IDF_COMMIT" in
            pending-*) ;;
            *)
                printf 'ESP-IDF checkout at %s has no Git metadata; cannot verify locked commit.\n' \
                    "$P4_CHECKED_IDF_PATH" >&2
                return 1
                ;;
        esac
    fi
}

p4_activate_idf() {
    P4_RESOLVED_IDF_PATH=$(p4_find_idf_path) || {
        printf 'ESP-IDF %s was not found. Run make setup or set P4_IDF_PATH.\n' "$P4_PINNED_IDF_VERSION" >&2
        return 1
    }
    p4_verify_idf_checkout "$P4_RESOLVED_IDF_PATH" || return
    p4_configure_idf_tools_path

    # Espressif's export script intentionally sets the SDK environment.
    # shellcheck disable=SC1090
    . "$P4_RESOLVED_IDF_PATH/export.sh" >/dev/null
    export P4_RESOLVED_IDF_PATH

    P4_ACTIVE_IDF_VERSION=$(idf.py --version | awk '{print $2}' | sed 's/^v//')
    if [ "$P4_ACTIVE_IDF_VERSION" != "$P4_PINNED_IDF_VERSION" ]; then
        printf 'ESP-IDF mismatch: expected %s, found %s at %s\n' \
            "$P4_PINNED_IDF_VERSION" "$P4_ACTIVE_IDF_VERSION" "$P4_RESOLVED_IDF_PATH" >&2
        return 1
    fi
}

p4_require_port() {
    if [ -z "${1:-}" ]; then
        printf 'An explicit serial port is required (for example, --port /dev/cu.wchusbserial10).\n' >&2
        return 2
    fi

    if [ ! -c "$1" ]; then
        printf 'Serial port is not a character device: %s\n' "$1" >&2
        return 2
    fi
}

p4_require_app() {
    case "${1:-}" in
        ''|*[!A-Za-z0-9_-]*)
            printf 'Invalid app name: %s\n' "${1:-<empty>}" >&2
            return 2
            ;;
    esac

    if [ ! -f "$P4_PROJECT_ROOT/apps/$1/CMakeLists.txt" ]; then
        printf 'Unknown app: %s\n' "$1" >&2
        return 2
    fi
}

p4_require_flash_authorized() {
    P4_POLICY_APP=${1:-}
    P4_POLICY_TARGET=${2:-}
    p4_require_app "$P4_POLICY_APP" || return

    case "$P4_POLICY_TARGET" in
        app-flash) P4_POLICY_KEY=flash_app_authorized ;;
        flash) P4_POLICY_KEY=flash_project_authorized ;;
        *)
            printf 'Unknown flash target for policy: %s\n' "$P4_POLICY_TARGET" >&2
            return 2
            ;;
    esac

    P4_POLICY_FILE="$P4_PROJECT_ROOT/apps/$P4_POLICY_APP/app-metadata.json"
    if [ ! -f "$P4_POLICY_FILE" ]; then
        printf 'Refusing to flash %s: app metadata is missing at %s\n' \
            "$P4_POLICY_APP" "$P4_POLICY_FILE" >&2
        return 1
    fi

    python3 -c '
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
key = sys.argv[2]
app = sys.argv[3]
try:
    data = json.loads(path.read_text())
except (OSError, UnicodeError, json.JSONDecodeError) as error:
    raise SystemExit(f"Refusing to flash {app}: invalid app metadata: {error}")
value = data.get(key)
if type(value) is not bool:
    raise SystemExit(
        f"Refusing to flash {app}: app metadata key {key!r} must be boolean"
    )
if not value:
    raise SystemExit(
        f"Refusing to flash {app}: {key} is false; this is a build-only app"
    )
' "$P4_POLICY_FILE" "$P4_POLICY_KEY" "$P4_POLICY_APP"
}

p4_identity_hash_from_esptool_output() {
    python3 -c '
import hashlib
import re
import sys

text = sys.stdin.read()
matches = re.findall(
    r"(?im)^\s*MAC:\s*((?:[0-9a-f]{2}:){5}[0-9a-f]{2})\s*$",
    text,
)
normalized_matches = {match.replace(":", "").lower() for match in matches}
if len(normalized_matches) != 1:
    raise SystemExit("expected one consistent base MAC in esptool output")
normalized = normalized_matches.pop()
print(hashlib.sha256(normalized.encode("ascii")).hexdigest())
'
}

p4_flash_size_bytes_from_esptool_output() {
    python3 -c '
import re
import sys

text = sys.stdin.read()
matches = re.findall(
    r"(?im)^\s*Detected flash size:\s*([0-9]+)\s*([KMG]B)\s*$",
    text,
)
values = {
    int(count) * {"KB": 1024, "MB": 1024**2, "GB": 1024**3}[unit.upper()]
    for count, unit in matches
}
if len(values) != 1:
    raise SystemExit("expected one consistent detected flash size in esptool output")
print(values.pop())
'
}

p4_read_device_identity_hash() {
    p4_require_port "${1:-}" || return
    P4_IDENTITY_AFTER=${2:-hard_reset}
    P4_IDENTITY_BEFORE=${3:-default_reset}
    case "$P4_IDENTITY_AFTER" in
        hard_reset|no_reset) ;;
        *)
            printf 'Invalid device-identity after-action: %s\n' "$P4_IDENTITY_AFTER" >&2
            return 2
            ;;
    esac
    case "$P4_IDENTITY_BEFORE" in
        default_reset|usb_reset|no_reset) ;;
        *)
            printf 'Invalid device-identity before-action: %s\n' "$P4_IDENTITY_BEFORE" >&2
            return 2
            ;;
    esac
    P4_IDENTITY_PROBE_OUTPUT=$(esptool.py --chip esp32p4 --port "$1" \
        --before "$P4_IDENTITY_BEFORE" --after "$P4_IDENTITY_AFTER" \
        read_mac 2>&1) || {
        printf 'Unable to read the ESP32-P4 device identity. Raw probe output is suppressed.\n' >&2
        return 1
    }
    printf '%s\n' "$P4_IDENTITY_PROBE_OUTPUT" | p4_identity_hash_from_esptool_output
}

p4_redact_device_identifiers() {
    python3 -c '
import re
import sys

text = sys.stdin.read()
print(
    re.sub(
        r"(?i)(\bMAC(?: address)?:\s*)((?:[0-9a-f]{2}:){5}[0-9a-f]{2})",
        r"\1[redacted]",
        re.sub(
            r"(?i)(\bserial\s*\()((?:[0-9a-f]{2}:){5}[0-9a-f]{2})(\))",
            r"\1[redacted]\3",
            text,
        ),
    ),
    end="",
)
'
}

p4_sha256_file() {
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    elif command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        printf 'Neither shasum nor sha256sum is available.\n' >&2
        return 1
    fi
}
