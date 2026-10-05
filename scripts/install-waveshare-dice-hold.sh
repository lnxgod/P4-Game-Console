#!/bin/sh
set -eu
P4_DICE_UNIT=
P4_DICE_WANT_UNIT=0
for P4_DICE_ARG in "$@"; do
    if [ "$P4_DICE_WANT_UNIT" = 1 ]; then
        [ -z "$P4_DICE_UNIT" ] || { printf 'Specify --unit exactly once.\n' >&2; exit 2; }
        P4_DICE_UNIT=$P4_DICE_ARG
        P4_DICE_WANT_UNIT=0
    elif [ "$P4_DICE_ARG" = --unit ]; then
        P4_DICE_WANT_UNIT=1
    fi
done
[ "$P4_DICE_WANT_UNIT" = 0 ] || { printf 'Missing --unit value.\n' >&2; exit 2; }
case "$P4_DICE_UNIT" in
    unit1|unit2) ;;
    *) printf 'This frozen dice candidate requires --unit unit1 or --unit unit2.\n' >&2; exit 2 ;;
esac
P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$P4_SCRIPT_DIR/lib/project-env.sh"
export P4_INSTALL_AUTH_UNIT1="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-dice-hold-20260913-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT1_SHA256=7161e09406edf337fbd03b9dac430dd3b5501064ba418f4a71d833be5f2d0f3c
export P4_INSTALL_AUTH_UNIT2="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-dice-hold-20260913-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT2_SHA256=ee188f56d295bd7411f1f54b8407d34ec7a520b7bbc28cc2f2e063eba6abbd08
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-dice-hold-fe51c95"
export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_DICE_UNIT-dice-hold-install-fe51c95"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-$P4_DICE_UNIT-dice-hold-install-lock"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback.bin"
exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" "$@"
