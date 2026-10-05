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
export P4_INSTALL_AUTH_UNIT1="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-shared-dice-20260913-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT1_SHA256=740ddbbdb2a1dcb179f4351e57dab7962c0a131383886b22b77120fd2a5f8c6a
export P4_INSTALL_AUTH_UNIT2="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-shared-dice-20260913-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT2_SHA256=8b602b47ff05e46dc084c047c2569f72a3d71f91c0c4596103f0dc23ef2497b1
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-shared-dice-e2a49f9"
export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_DICE_UNIT-shared-dice-install-e2a49f9"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-$P4_DICE_UNIT-shared-dice-install-lock"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback.bin"
exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" "$@"
