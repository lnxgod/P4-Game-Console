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

P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-two-unit-console-os-0.4.73-lord-realm-20260825-exact-unit-authorization.json"
P4_AUTH_EXPECTED=64c944ab2b2b6f073ca93ac14e831be8eec2bd5936d4a13f008ef0ed4b031cb4
P4_TRANSFER="$P4_PROJECT_ROOT/scripts/p4-transfer.py"
P4_LORD="$P4_PROJECT_ROOT/hardware/local-state/waveshare-console-os-0.4.73-lord-realm-fb58142/LORD.P4G"
P4_LORD_BYTES=168260
P4_LORD_HASH=f8bd0f55c2dc29a3364eeb223ed6043d277d53c28683dc28d88d2450c8c278e5
P4_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.lord-1.3.0-delivery-lock"

[ "$(p4_sha256_file "$P4_AUTH")" = "$P4_AUTH_EXPECTED" ] || {
    printf 'Refusing LORD delivery: exact-unit authorization changed.\n' >&2
    exit 1
}
[ -f "$P4_TRANSFER" ] || {
    printf 'Refusing LORD delivery: H1 transfer client is missing.\n' >&2
    exit 1
}
[ -f "$P4_LORD" ] || {
    printf 'Refusing LORD delivery: exact cartridge is missing.\n' >&2
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
    fb5814215cacb38f7b608e3b9c8ba01d0d3b2e0a HEAD || {
    printf 'Refusing LORD delivery: authorized source is not an ancestor.\n' >&2
    exit 1
}

if [ "$P4_CHECK_ONLY" = 1 ]; then
    [ -z "$P4_PORT" ] && [ -z "$P4_UNIT" ] || {
        printf -- '--check-only does not accept --unit or --port.\n' >&2
        exit 2
    }
    printf 'LORD 1.3.0 realm cartridge delivery checks PASS; no device access or write occurred.\n'
    exit 0
fi

case "$P4_UNIT" in
    unit1) P4_RECORDED_PORT=/dev/cu.wchusbserial5C371865781 ;;
    unit2) P4_RECORDED_PORT=/dev/cu.wchusbserial5B901593451 ;;
    *) printf 'Use --unit unit1 or --unit unit2.\n' >&2; exit 2 ;;
esac
p4_require_port "$P4_PORT"
[ "$P4_PORT" = "$P4_RECORDED_PORT" ] || {
    printf 'Refusing LORD delivery: port differs from the exact-unit record.\n' >&2
    exit 1
}

P4_FIRMWARE_EVIDENCE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-console-os-0.4.73-lord-realm-fb58142-$P4_UNIT"
P4_STARTUP_RAW="$P4_FIRMWARE_EVIDENCE/startup.raw"
P4_STARTUP_SUMMARY="$P4_FIRMWARE_EVIDENCE/startup.json"
python3 -c '
import hashlib, json, pathlib, sys
raw_path = pathlib.Path(sys.argv[1])
summary_path = pathlib.Path(sys.argv[2])
if not raw_path.is_file() or not summary_path.is_file():
    raise SystemExit("retained-UART acceptance evidence is missing")
raw = raw_path.read_bytes()
summary = json.loads(summary_path.read_text(encoding="utf-8"))
expected = {
    "result": "pass",
    "classification": "waveshare-console-os-0.4.73-retained-uart",
    "application_sha256":
        "c30ed4d987bda6021712dd132d441c0848ef2b0a24799ef3a6921e2814628206",
    "port": sys.argv[3],
    "raw_bytes": len(raw),
    "raw_sha256": hashlib.sha256(raw).hexdigest(),
}
for key, value in expected.items():
    if summary.get(key) != value:
        raise SystemExit("retained-UART acceptance differs for " + key)
' "$P4_STARTUP_RAW" "$P4_STARTUP_SUMMARY" "$P4_PORT"

P4_DELIVERY="$P4_PROJECT_ROOT/hardware/local-state/lord-1.3.0-realm-delivery-$P4_UNIT"
P4_READBACK="$P4_DELIVERY/LORD.P4G"
[ ! -e "$P4_LOCK_DIR" ] || {
    printf 'Refusing LORD delivery: another delivery route is active.\n' >&2
    exit 1
}
[ ! -e "$P4_DELIVERY" ] || {
    printf 'Refusing LORD delivery: %s evidence already exists.\n' \
        "$P4_UNIT" >&2
    exit 1
}
mkdir -m 700 "$P4_LOCK_DIR"
trap 'rmdir "$P4_LOCK_DIR" 2>/dev/null || true' EXIT HUP INT TERM
mkdir -m 700 "$P4_DELIVERY"

python3 "$P4_TRANSFER" push "$P4_LORD" --port "$P4_PORT" \
    --class p4g --remote-name LORD.P4G
python3 "$P4_TRANSFER" pull LORD.P4G "$P4_READBACK" --port "$P4_PORT" \
    --class p4g --replace
chmod 0400 "$P4_READBACK"

[ "$(wc -c < "$P4_READBACK" | tr -d ' ')" = "$P4_LORD_BYTES" ] || {
    printf 'LORD readback byte count differs.\n' >&2
    exit 1
}
[ "$(p4_sha256_file "$P4_READBACK")" = "$P4_LORD_HASH" ] || {
    printf 'LORD readback digest differs.\n' >&2
    exit 1
}
cmp -s "$P4_LORD" "$P4_READBACK" || {
    printf 'LORD readback is not byte-for-byte exact.\n' >&2
    exit 1
}

printf 'LORD 1.3.0 realm cartridge delivery PASS for %s.\n' "$P4_UNIT"
printf 'sha256=%s bytes=%s\n' "$P4_LORD_HASH" "$P4_LORD_BYTES"
