#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

export P4_INSTALL_AUTH_UNIT1="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-console-os-0.4.100-full-catalog-20260904-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT1_SHA256=fff9b78521338f9702c09da838e564b7965a895624fee4c0dd5016d47e41e504
export P4_INSTALL_AUTH_UNIT2="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-0.4.100-full-catalog-20260904-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT2_SHA256=081f1cc3721bae3895209a7f0fc3e6987637340435b33e68398d118c53f5f6e3
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-two-unit-console-os-0.4.100-full-catalog-53b5266"

P4_UNIT=
case " $* " in
    *" --unit unit1 "*) P4_UNIT=unit1 ;;
    *" --unit unit2 "*) P4_UNIT=unit2 ;;
    *)
        printf 'This exact-artifact route requires --unit unit1 or --unit unit2.\n' >&2
        exit 2
        ;;
esac

export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_UNIT-console-os-0.4.100-full-catalog-install-53b5266"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-$P4_UNIT-console-os-0.4.100-full-catalog-install-lock-53b5266"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage-0.4.99-span.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback-0.4.100-span.bin"

exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" "$@"
