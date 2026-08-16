#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
p4_activate_idf

cd "$P4_PROJECT_ROOT/apps/console_os"
idf.py -B build-waveshare-landscape \
    -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.waveshare.defaults' \
    build
