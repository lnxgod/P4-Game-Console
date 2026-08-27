#!/bin/sh

set -eu

P4_PORT=
P4_UNIT=
P4_CHECK_ONLY=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) P4_PORT=${2:-}; shift 2 ;;
        --unit) P4_UNIT=${2:-}; shift 2 ;;
        --check-only) P4_CHECK_ONLY=1; shift ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

P4_TRANSFER="$P4_PROJECT_ROOT/scripts/p4-transfer.py"
P4_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-two-unit-console-os-0.4.85-lord-save-76266e0"
P4_LORD="$P4_RELEASE/LORD.P4G"
P4_LORD_BYTES=175572
P4_LORD_HASH=7c6ec15013d6944568dd1599ccbbba2f621ee3fe0e8b02d9ee0fd0a9d6aad3bd
P4_APPLICATION_HASH=71db139c36ce043b900e71e40ff5ca2ea431811da2ab60a0cef30d4ff0e5dfda
P4_SOURCE_COMMIT=76266e0d1d9d7782773c3ec7693b3c415408d32d

p4_check_common() {
    [ -f "$P4_TRANSFER" ] || {
        printf 'Refusing LORD delivery: H1 transfer client is missing.\n' >&2
        exit 1
    }
    [ -f "$P4_LORD" ] || {
        printf 'Refusing LORD delivery: sealed cartridge is missing.\n' >&2
        exit 1
    }
    [ "$(wc -c < "$P4_LORD" | tr -d ' ')" = "$P4_LORD_BYTES" ] || {
        printf 'Refusing LORD delivery: cartridge byte count changed.\n' >&2
        exit 1
    }
    [ "$(p4_sha256_file "$P4_LORD")" = "$P4_LORD_HASH" ] || {
        printf 'Refusing LORD delivery: cartridge digest changed.\n' >&2
        exit 1
    }
    [ "$(stat -f '%Lp' "$P4_LORD")" = 400 ] || {
        printf 'Refusing LORD delivery: cartridge is not sealed mode 0400.\n' >&2
        exit 1
    }
    git -C "$P4_PROJECT_ROOT" merge-base --is-ancestor \
        "$P4_SOURCE_COMMIT" HEAD || {
        printf 'Refusing LORD delivery: sealed source is not an ancestor.\n' >&2
        exit 1
    }
}

p4_select_unit() {
    case "$P4_UNIT" in
        unit1)
            P4_RECORDED_PORT=/dev/cu.wchusbserial5C371865781
            P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-console-os-0.4.85-lord-save-20260827-exact-unit-authorization.json"
            P4_AUTH_HASH=839b863940924ff37c26536265e3a2819c2248d53d2d14de3df69ce620617cb5
            ;;
        unit2)
            P4_RECORDED_PORT=/dev/cu.wchusbserial5B901593451
            P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-0.4.85-lord-save-20260827-exact-unit-authorization.json"
            P4_AUTH_HASH=b2c129fee7914415813687644240b967b0920fe54c70c872ac9f7a0ec5f9a1b6
            ;;
        *) printf 'Use --unit unit1 or --unit unit2.\n' >&2; exit 2 ;;
    esac
    [ "$(p4_sha256_file "$P4_AUTH")" = "$P4_AUTH_HASH" ] || {
        printf 'Refusing LORD delivery: exact-unit authorization changed.\n' >&2
        exit 1
    }
}

p4_check_common
if [ "$P4_CHECK_ONLY" = 1 ]; then
    [ -z "$P4_PORT" ] && [ -z "$P4_UNIT" ] || {
        printf -- '--check-only does not accept --unit or --port.\n' >&2
        exit 2
    }
    P4_UNIT=unit1
    p4_select_unit
    P4_UNIT=unit2
    p4_select_unit
    printf 'LORD 1.6.0 two-unit delivery checks PASS; no device access or write occurred.\n'
    exit 0
fi

p4_select_unit
p4_require_port "$P4_PORT"
[ "$P4_PORT" = "$P4_RECORDED_PORT" ] || {
    printf 'Refusing LORD delivery: port differs from the exact-unit record.\n' >&2
    exit 1
}

P4_STARTUP="$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_UNIT-console-os-0.4.85-startup-76266e0"
P4_RAW="$P4_STARTUP/startup.raw"
P4_SUMMARY="$P4_STARTUP/startup.json"
python3 - "$P4_RAW" "$P4_SUMMARY" "$P4_PORT" "$P4_APPLICATION_HASH" <<'PY'
import hashlib
import json
import pathlib
import sys

raw_path = pathlib.Path(sys.argv[1])
summary_path = pathlib.Path(sys.argv[2])
if not raw_path.is_file() or not summary_path.is_file():
    raise SystemExit("retained-UART acceptance evidence is missing")
raw = raw_path.read_bytes()
summary = json.loads(summary_path.read_text(encoding="utf-8"))
expected = {
    "result": "pass",
    "classification": "waveshare-console-os-0.4.85-retained-uart",
    "application_sha256": sys.argv[4],
    "port": sys.argv[3],
    "raw_bytes": len(raw),
    "raw_sha256": hashlib.sha256(raw).hexdigest(),
}
for key, value in expected.items():
    if summary.get(key) != value:
        raise SystemExit("retained-UART acceptance differs for " + key)
PY

P4_LOCK="$P4_PROJECT_ROOT/hardware/local-state/.lord-1.6.0-save-$P4_UNIT-delivery-lock"
P4_DELIVERY="$P4_PROJECT_ROOT/hardware/local-state/lord-1.6.0-save-delivery-$P4_UNIT-76266e0"
P4_READBACK="$P4_DELIVERY/LORD.P4G"
[ ! -e "$P4_LOCK" ] || {
    printf 'Refusing LORD delivery: another delivery route is active.\n' >&2
    exit 1
}
[ ! -e "$P4_DELIVERY" ] || {
    printf 'Refusing LORD delivery: evidence directory already exists.\n' >&2
    exit 1
}
mkdir -m 700 "$P4_LOCK"
trap 'rmdir "$P4_LOCK" 2>/dev/null || true' EXIT HUP INT TERM
mkdir -m 700 "$P4_DELIVERY"

python3 "$P4_TRANSFER" push "$P4_LORD" --port "$P4_PORT" \
    --class p4g --remote-name LORD.P4G
python3 "$P4_TRANSFER" pull LORD.P4G "$P4_READBACK" --port "$P4_PORT" \
    --class p4g --replace
chmod 0400 "$P4_READBACK"

[ "$(wc -c < "$P4_READBACK" | tr -d ' ')" = "$P4_LORD_BYTES" ]
[ "$(p4_sha256_file "$P4_READBACK")" = "$P4_LORD_HASH" ]
cmp -s "$P4_LORD" "$P4_READBACK"
printf 'LORD 1.6.0 exact delivery/readback PASS for %s.\n' "$P4_UNIT"
