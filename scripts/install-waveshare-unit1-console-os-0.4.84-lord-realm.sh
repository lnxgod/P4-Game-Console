#!/bin/sh

set -eu

P4_MODE=${1:-}
[ "$#" -gt 0 ] && shift
P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" \
    "$P4_MODE" --unit unit1 "$@"
