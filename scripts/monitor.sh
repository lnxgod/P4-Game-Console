#!/bin/sh

set -eu

P4_APP=bringup
P4_PORT=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --app) P4_APP=${2:-}; shift 2 ;;
        --port) P4_PORT=${2:-}; shift 2 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
p4_require_app "$P4_APP"
p4_require_port "$P4_PORT"
p4_activate_idf

idf.py -C "$P4_PROJECT_ROOT/apps/$P4_APP" \
    -B "$P4_PROJECT_ROOT/apps/$P4_APP/build" -D IDF_TARGET=esp32p4 \
    -p "$P4_PORT" monitor --timestamps
