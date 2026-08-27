#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

export P4_INSTALL_AUTH_UNIT1="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-console-os-0.4.85-lord-save-20260827-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT1_SHA256=3931721a2ecd420831f68cf2a9bf8c3c370f22d4ce09c8ddbd0f69a2a73df27b
export P4_INSTALL_AUTH_UNIT2="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-0.4.85-lord-save-20260827-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT2_SHA256=55fae61c49b2a9efcc7650ab00e2205af8da63aa5100cc0baeb892f9ef41c28a
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-two-unit-console-os-0.4.85-lord-save-aa4209d"

P4_UNIT=unit2
P4_ARGUMENTS="$*"
case " $P4_ARGUMENTS " in
    *" --unit unit1 "*) P4_UNIT=unit1 ;;
esac
export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_UNIT-console-os-0.4.85-install-aa4209d"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-$P4_UNIT-console-os-0.4.85-install-lock-aa4209d"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage-0.4.84-span.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback-0.4.85-span.bin"

exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" "$@"
