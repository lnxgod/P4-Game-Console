#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
p4_activate_idf

if ! idf.py --list-targets | grep -Fx esp32p4 >/dev/null; then
    printf 'The pinned ESP-IDF installation does not provide the esp32p4 target.\n' >&2
    exit 1
fi

command -v cmake >/dev/null
command -v ninja >/dev/null
python3 -m json.tool "$P4_PROJECT_ROOT/toolchain.lock.json" >/dev/null
python3 -m json.tool "$P4_PROJECT_ROOT/hardware/board-profile.json" >/dev/null
python3 -m json.tool "$P4_PROJECT_ROOT/hardware/backups/manifest.json" >/dev/null
for P4_METADATA_FILE in \
    "$P4_PROJECT_ROOT"/hardware/test-runs/*.json \
    "$P4_PROJECT_ROOT"/third_party/*.json
do
    python3 -m json.tool "$P4_METADATA_FILE" >/dev/null
done
python3 "$P4_SCRIPT_DIR/verify-metadata.py"
python3 "$P4_SCRIPT_DIR/verify-board-profiles.py"

printf 'Environment OK: ESP-IDF %s (%s) at %s\n' \
    "$P4_PINNED_IDF_VERSION" "$P4_PINNED_IDF_TAG" "$P4_RESOLVED_IDF_PATH"
case "$P4_PINNED_IDF_COMMIT" in
    ''|pending-*) printf 'ESP-IDF commit lock is pending final checkout verification.\n' ;;
    *) printf 'ESP-IDF commit: %s\n' "$P4_PINNED_IDF_COMMIT" ;;
esac
printf 'Board profile status: '
python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["status"])' \
    "$P4_PROJECT_ROOT/hardware/board-profile.json"
