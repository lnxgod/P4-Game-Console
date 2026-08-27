#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

export P4_INSTALL_AUTH_UNIT1="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-console-os-0.4.85-lord-save-20260827-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT1_SHA256=839b863940924ff37c26536265e3a2819c2248d53d2d14de3df69ce620617cb5
export P4_INSTALL_AUTH_UNIT2="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-0.4.85-lord-save-20260827-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT2_SHA256=b2c129fee7914415813687644240b967b0920fe54c70c872ac9f7a0ec5f9a1b6
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-two-unit-console-os-0.4.85-lord-save-76266e0"

P4_UNIT=unit2
P4_ARGUMENTS="$*"
case " $P4_ARGUMENTS " in
    *" --unit unit1 "*) P4_UNIT=unit1 ;;
esac
export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_UNIT-console-os-0.4.85-install-76266e0"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-$P4_UNIT-console-os-0.4.85-install-lock"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage-0.4.84-span.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback-0.4.85-span.bin"

exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" "$@"
