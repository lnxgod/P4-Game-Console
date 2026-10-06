#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Compatibility rejection only; the retired installer implementation was removed.
printf '%s\n' 'Refusing retired Lua .P4CART installer: use the current native Console OS workflow. No device or content write was performed.' >&2
exit 2
