#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

export P4_INSTALL_AUTH_UNIT1="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-console-os-0.4.98-ble-radio-handoff-20260831-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT1_SHA256=667410e587d36a6c79c5762ce5eefb19f612314ada0f80d07a579eaa92d3031c
export P4_INSTALL_AUTH_UNIT2="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-0.4.98-ble-radio-handoff-20260831-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT2_SHA256=63a85eb252e3b86c8903b51a619a115a04860722addd1a81c47ca5b3e02d4d8f
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-two-unit-console-os-0.4.98-ble-handoff-ebc0418"

P4_UNIT=
case " $* " in
    *" --unit unit1 "*) P4_UNIT=unit1 ;;
    *" --unit unit2 "*) P4_UNIT=unit2 ;;
    *)
        printf 'This exact-artifact route requires --unit unit1 or --unit unit2.\n' >&2
        exit 2
        ;;
esac

export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_UNIT-console-os-0.4.98-ble-radio-handoff-install-ebc0418"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-$P4_UNIT-console-os-0.4.98-ble-radio-handoff-install-lock-ebc0418"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage-0.4.97-span.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback-0.4.98-span.bin"

exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" "$@"
